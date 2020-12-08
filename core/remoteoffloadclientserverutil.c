/*
 *  remoteoffloadclientserverutil.c - Utility objects & types for establishing
 *       connections between a client(the host) & server (the remote target)
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author: Metcalfe, Ryan <ryan.d.metcalfe@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */
#include <stdlib.h>
#include <gst/gst.h>
#include <sys/wait.h>
#include "remoteoffloadclientserverutil.h"
#include "remoteoffloadcommsio.h"
#include "remoteoffloadcomms.h"
#include "remoteoffloadcommschannel.h"
#include "gstremoteoffloadpipeline.h"
#include "remoteoffloadextregistry.h"

/* Private structure definition. */
typedef struct {
   GQueue *newConnectionQueue; //GQueue of CommIO's
   GMutex thrmutex;
   GCond  thrcond;
   gboolean bThreadRun;
   GThread *thread;
   GMutex placeholdermutex;
   GHashTable *pipelineplaceholderhash;
   RemoteOffloadExtRegistry *ext_registry;
   remoteoffloadpipeline_spawn_func spawn_func;
   void *spawn_func_user_data;
}RemoteOffloadPipelineSpawnerPrivate;

typedef struct _PipelinePlaceholder
{
   RemoteOffloadPipelineSpawnerPrivate *spawn_priv;
   GArray *id_commsio_pair_array;
}PipelinePlaceholder;

struct _RemoteOffloadPipelineSpawner
{
  GObject parent_instance;

  /* Other members, including private data. */
  RemoteOffloadPipelineSpawnerPrivate priv;
};

GST_DEBUG_CATEGORY_STATIC (remote_offload_clientserverutil_debug);
#define GST_CAT_DEFAULT remote_offload_clientserverutil_debug
static gsize g_debugRegistered = 0;
static void register_debug_category()
{
   if( g_once_init_enter(&g_debugRegistered) )
   {
      GST_DEBUG_CATEGORY_INIT (remote_offload_clientserverutil_debug,
                               "remoteoffloadclientserverutil", 0,
      "debug category for Client/Server utility objects / routines");
      g_once_init_leave (&g_debugRegistered, 1);
   }
}

G_DEFINE_TYPE_WITH_CODE(RemoteOffloadPipelineSpawner,
remote_offload_pipeline_spawner, G_TYPE_OBJECT,
register_debug_category()
)


typedef struct
{
   RemoteOffloadCommsIO *commsio;
   RemoteOffloadPipelineSpawnerPrivate *spawn_priv;
} CommsIOTmpThreadPrivate;

static gpointer CommsIOTemporaryThread(void *arg);

static void ChannelValDestroy(gpointer data)
{
   RemoteOffloadCommsChannel *channel = (RemoteOffloadCommsChannel *)data;
   g_object_unref(channel);
}

static void CommsValDestroy(gpointer data)
{
   RemoteOffloadComms *comms = (RemoteOffloadComms *)data;

   g_object_unref(comms);
}

GHashTable *id_commsio_pair_array_to_id_to_channel_hash(GArray *id_commsio_pair_array)
{
   register_debug_category();
   GHashTable *commsio_to_comms_hash =
         g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, CommsValDestroy);
   GHashTable *id_to_channel_hash =
         g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, ChannelValDestroy);
   gboolean bokay = TRUE;

   if( id_commsio_pair_array && id_commsio_pair_array->len )
   {
      ChannelIdCommsIOPair *pairs = (ChannelIdCommsIOPair *)id_commsio_pair_array->data;
      for( guint pairi = 0; pairi < id_commsio_pair_array->len; pairi++)
      {
         RemoteOffloadCommsIO *commsio = pairs[pairi].commsio;

         //If we haven't yet create a comms object from this commsio, create one.
         RemoteOffloadComms *comms = g_hash_table_lookup(commsio_to_comms_hash, commsio);
         if( !comms )
         {
            //create one.
            comms = remote_offload_comms_new(commsio);
            if( !comms )
            {
               bokay = FALSE;
               GST_ERROR("Error creating comms from commsio(%p)\n", commsio);
               break;
            }
            g_hash_table_insert(commsio_to_comms_hash, commsio, comms);
         }

         //create a comms channel, using this comms
         RemoteOffloadCommsChannel *channel =
               remote_offload_comms_channel_new(comms, pairs[pairi].channel_id);
         if( !channel )
         {
           bokay = FALSE;
           GST_ERROR("Error creating channel from comms=%p, id=%d\n",
                     comms, pairs[pairi].channel_id);
           break;
         }

         if( !g_hash_table_insert(id_to_channel_hash,
                                  GINT_TO_POINTER(pairs[pairi].channel_id), channel) )
         {
            bokay = FALSE;
            GST_ERROR("id_commsio_pair_array contains multiple entries for id=%d\n",
                      pairs[pairi].channel_id);
            break;
         }
      }
   }
   else
   {
      bokay = FALSE;
      GST_ERROR("Invalid id_commsio_pair_array (GArray *)");
   }

   g_hash_table_destroy(commsio_to_comms_hash);
   if( !bokay )
   {
      g_hash_table_destroy(id_to_channel_hash);
      id_to_channel_hash = NULL;
   }

   return id_to_channel_hash;
}

static gpointer RemotePipelineInstanceThread (PipelinePlaceholder *placeholder)
{
   GST_DEBUG("RemotePipelineInstanceThread starting..");

   if( placeholder->spawn_priv->spawn_func )
   {
      placeholder->spawn_priv->spawn_func(placeholder->id_commsio_pair_array,
                                          placeholder->spawn_priv->spawn_func_user_data);
   }
   else
   {
      GHashTable *id_to_channel_hash =
            id_commsio_pair_array_to_id_to_channel_hash(placeholder->id_commsio_pair_array);

      if( id_to_channel_hash )
      {
         RemoteOffloadPipeline *pPipeline =
               remote_offload_pipeline_new(NULL, id_to_channel_hash);

         if( pPipeline )
         {
            if( !remote_offload_pipeline_run(pPipeline) )
            {
               GST_ERROR("remote_offload_pipeline_run failed");
            }

            g_object_unref(pPipeline);
         }
         else
         {
            GST_ERROR("Error in remote_offload_pipeline_new");
         }

         g_hash_table_unref(id_to_channel_hash);
      }
      else
      {
         GST_ERROR("Invalid PipelinePlaceholder");
      }
   }

   g_array_free(placeholder->id_commsio_pair_array, TRUE);

   GST_DEBUG("RemotePipelineInstanceThread returning..");

   return NULL;
}

static gpointer SpawnerThread(void *arg)
{
   RemoteOffloadPipelineSpawnerPrivate *priv = (RemoteOffloadPipelineSpawnerPrivate *)arg;

   GST_DEBUG("SpawnerThread starting");

   g_mutex_lock (&priv->thrmutex);
   while( priv->bThreadRun )
   {
      RemoteOffloadCommsIO *commsio = g_queue_pop_head(priv->newConnectionQueue);

      //if there were no entries in the queue
      if(!commsio)
      {
         //wait to be signaled
         g_cond_wait (&priv->thrcond, &priv->thrmutex);
      }

      if( !priv->bThreadRun )
         break;

      g_mutex_unlock (&priv->thrmutex);

      if( commsio )
      {
         CommsIOTmpThreadPrivate *tmp = g_malloc(sizeof(CommsIOTmpThreadPrivate));
         tmp->commsio = commsio;
         tmp->spawn_priv = priv;
         GST_DEBUG("Spawning new CommsIOTemporaryThread");
         GThread *tmpcommsiothread = g_thread_new ("commsiotmp",
                                           (GThreadFunc)CommsIOTemporaryThread,
                                           tmp);
         g_thread_unref(tmpcommsiothread);
      }

      g_mutex_lock (&priv->thrmutex);
   }
   g_mutex_unlock (&priv->thrmutex);

   return NULL;
}

void remote_offload_pipeline_set_callback(RemoteOffloadPipelineSpawner *spawner,
                                              remoteoffloadpipeline_spawn_func callback,
                                              void *user_data)
{
   if( !REMOTEOFFLOAD_IS_PIPELINESPAWNER(spawner) ) return;

   spawner->priv.spawn_func = callback;
   spawner->priv.spawn_func_user_data = user_data;
}

gboolean remote_offload_pipeline_spawner_add_connection(RemoteOffloadPipelineSpawner *spawner,
                                                        RemoteOffloadCommsIO *commsio)
{
   GST_DEBUG("commsio=%p", commsio);

   if( !REMOTEOFFLOAD_IS_PIPELINESPAWNER(spawner) ||
       !REMOTEOFFLOAD_IS_COMMSIO(commsio))
   {
      GST_ERROR("!REMOTEOFFLOAD_IS_PIPELINESPAWNER(spawner) || !REMOTEOFFLOAD_IS_COMMSIO(commsio)");
      return FALSE;
   }

   g_mutex_lock (&spawner->priv.thrmutex);
   g_queue_push_head(spawner->priv.newConnectionQueue, commsio);
   g_cond_broadcast(&spawner->priv.thrcond);
   g_mutex_unlock (&spawner->priv.thrmutex);

   return TRUE;
}

static void
remote_offload_pipelinespawner_finalize (GObject *gobject)
{
  RemoteOffloadPipelineSpawner *self = REMOTEOFFLOAD_PIPELINESPAWNER(gobject);

  //shut down spawner thread
  if( self->priv.thread)
  {
    g_mutex_lock (&self->priv.thrmutex);
    self->priv.bThreadRun = FALSE;
    g_cond_broadcast (&self->priv.thrcond);
    g_mutex_unlock (&self->priv.thrmutex);
    g_thread_join (self->priv.thread);
  }

  g_hash_table_destroy(self->priv.pipelineplaceholderhash);

  remote_offload_ext_registry_unref(self->priv.ext_registry);

  G_OBJECT_CLASS (remote_offload_pipeline_spawner_parent_class)->finalize (gobject);
}

static void
remote_offload_pipeline_spawner_class_init (RemoteOffloadPipelineSpawnerClass *klass)
{
   GObjectClass *object_class = G_OBJECT_CLASS (klass);

   object_class->finalize = remote_offload_pipelinespawner_finalize;
}

static void
remote_offload_pipeline_spawner_init (RemoteOffloadPipelineSpawner *self)
{
  self->priv.thread = NULL;
  self->priv.spawn_func = NULL;
  self->priv.spawn_func_user_data = NULL;
  g_mutex_init(&self->priv.thrmutex);
  g_cond_init(&self->priv.thrcond);
  self->priv.bThreadRun = TRUE;
  self->priv.newConnectionQueue = g_queue_new();
  g_mutex_init(&self->priv.placeholdermutex);
  self->priv.pipelineplaceholderhash = g_hash_table_new(g_direct_hash, g_direct_equal);

  self->priv.ext_registry = remote_offload_ext_registry_get_instance();

  self->priv.thread =
        g_thread_new ("PipelineSpawnerThread", (GThreadFunc) SpawnerThread, &self->priv);
}

RemoteOffloadPipelineSpawner *remote_offload_pipeline_spawner_new()
{
   return g_object_new(REMOTEOFFLOADPIPELINESPAWNER_TYPE, NULL);
}

typedef enum
{
  COMMSIOSTARTUP_NEW_PIPELINE_PLACEHOLDER = 0x77,
  COMMSIOSTARTUP_REGISTER_CHANNEL,
  COMMSIOSTARTUP_DONE,
  COMMSIOSTARTUP_START_PIPELINE
}CommsIOStartupCodes;

typedef enum
{
   COMMSIOSTARTUP_OKAY = 0x4F4B4159 //'OKAY' in ASCII
}CommsIOStartupStatus;

gboolean remote_offload_request_new_pipeline(GArray *id_commsio_pair_array)
{
   register_debug_category();
   if( !id_commsio_pair_array ) return FALSE;
   if( !id_commsio_pair_array->len ) return FALSE;

   ChannelIdCommsIOPair *pairs = (ChannelIdCommsIOPair *)id_commsio_pair_array->data;
   GArray *commsioarray = g_array_sized_new(FALSE, FALSE,
                                            sizeof(RemoteOffloadCommsIO *),
                                            id_commsio_pair_array->len);

   GHashTable *hash_id_to_comms_channel = g_hash_table_new(g_direct_hash, g_direct_equal);
   gboolean status_okay = TRUE;
   for( guint i = 0; i < id_commsio_pair_array->len; i++ )
   {
      GST_DEBUG("channel_id = %d, commsio = %p\n", pairs[i].channel_id, pairs[i].commsio);
      if( !REMOTEOFFLOAD_IS_COMMSIO(pairs[i].commsio) )
      {
         GST_ERROR("id_commsio_pair_array[%d].commsio (%p) is not a valid CommsIO object\n",
                   pairs[i].channel_id, pairs[i].commsio);
         status_okay = FALSE;
      }

      if( !g_hash_table_insert(hash_id_to_comms_channel,
                               GINT_TO_POINTER(pairs[i].channel_id), NULL) )
      {
         GST_ERROR("id_commsio_pair_array contains two entries with channel-id=%d",
                   pairs[i].channel_id);
         status_okay = FALSE;
      }

      gboolean found = FALSE;
      for( guint commsioi = 0; commsioi < commsioarray->len; commsioi++ )
      {
         if( g_array_index(commsioarray, RemoteOffloadCommsIO *, commsioi) == pairs[i].commsio )
         {
            found = TRUE;
         }
      }

      if( !found )
      {
         g_array_append_val(commsioarray, pairs[i].commsio);
      }
   }

   guint64 handle = 0;
   if( status_okay )
   {
      RemoteOffloadCommsIOResult res;

      //using the first commsio, request a new pipeline placeholder
      guint code = COMMSIOSTARTUP_NEW_PIPELINE_PLACEHOLDER;

      res = remote_offload_comms_io_write(pairs[0].commsio, (guint8 *)&code, sizeof(code));
      if( res == REMOTEOFFLOADCOMMSIO_SUCCESS )
      {
         //read the handle
         res = remote_offload_comms_io_read(pairs[0].commsio, (guint8 *)&handle,
                                        sizeof(handle));
         if( res == REMOTEOFFLOADCOMMSIO_SUCCESS )
         {
            if( handle )
            {
               //get the status
               guint32 server_status = 0;
               res = remote_offload_comms_io_read(pairs[0].commsio, (guint8 *)&server_status,
                                        sizeof(server_status));
               if( res == REMOTEOFFLOADCOMMSIO_SUCCESS )
               {
                  if( server_status != COMMSIOSTARTUP_OKAY )
                  {
                     status_okay = FALSE;
                     GST_ERROR("Server sent bad startup status");
                  }
               }
               else
               {
                  status_okay = FALSE;
                  GST_ERROR("Error in remote_offload_comms_io_read");
               }
            }
            else
            {
               status_okay = FALSE;
               GST_ERROR("invalid handle");
            }
         }
         else
         {
            status_okay = FALSE;
            GST_ERROR("Error in remote_offload_comms_io_read");
         }
      }
      else
      {
         status_okay = FALSE;
         GST_ERROR("Error in remote_offload_comms_io_write");
      }
   }

   //need to register each channel
   if( status_okay )
   {
     for( guint i = 0; i < id_commsio_pair_array->len; i++ )
     {
        RemoteOffloadCommsIOResult res;
        guint code = COMMSIOSTARTUP_REGISTER_CHANNEL;
        res = remote_offload_comms_io_write(pairs[i].commsio,
                                            (guint8 *)&code,
                                            sizeof(code));

        if( res == REMOTEOFFLOADCOMMSIO_SUCCESS )
        {
           res = remote_offload_comms_io_write(pairs[i].commsio,
                                               (guint8 *)&handle,
                                               sizeof(handle));

           if( res == REMOTEOFFLOADCOMMSIO_SUCCESS )
           {
              res = remote_offload_comms_io_write(pairs[i].commsio,
                                                  (guint8 *)&pairs[i].channel_id,
                                                  sizeof(pairs[i].channel_id));

              if( res == REMOTEOFFLOADCOMMSIO_SUCCESS )
              {
                 //get the status
                 guint32 server_status = 0;
                 res = remote_offload_comms_io_read(pairs[i].commsio,
                                                    (guint8 *)&server_status,
                                                    sizeof(server_status));

                 if( res == REMOTEOFFLOADCOMMSIO_SUCCESS )
                 {
                    if( server_status != COMMSIOSTARTUP_OKAY )
                    {
                       status_okay = FALSE;
                       GST_ERROR("Server sent bad startup status");
                    }
                 }
                 else
                 {
                    status_okay = FALSE;
                    GST_ERROR("Error in remote_offload_comms_io_read");
                 }
              }
              else
              {
                 status_okay = FALSE;
                 GST_ERROR("Error in remote_offload_comms_io_write");
              }
           }
           else
           {
              status_okay = FALSE;
              GST_ERROR("Error in remote_offload_comms_io_write");
              break;
           }
        }
        else
        {
           status_okay = FALSE;
           GST_ERROR("Error in remote_offload_comms_io_write");
           break;
        }
     }
   }

   if( status_okay )
   {
     //for each commsio object, except for the last one, give
     // instruction for that tmp reader thread to end.
     // For the last one, instruct that reader thread to
     // actually start the pipeline.
     for( guint i = 0; i < commsioarray->len; i++ )
     {
        RemoteOffloadCommsIO *commsio = g_array_index(commsioarray, RemoteOffloadCommsIO *, i);

        guint code = COMMSIOSTARTUP_DONE;

        if( i == (commsioarray->len - 1))
        {
           code = COMMSIOSTARTUP_START_PIPELINE;
        }

        RemoteOffloadCommsIOResult res;
        res = remote_offload_comms_io_write(commsio, (guint8 *)&code, sizeof(code));

        if( res == REMOTEOFFLOADCOMMSIO_SUCCESS )
        {
           //get the status
           guint32 server_status = 0;
           res = remote_offload_comms_io_read(commsio, (guint8 *)&server_status,
                                                        sizeof(server_status));

           if( res == REMOTEOFFLOADCOMMSIO_SUCCESS )
           {
              if( server_status != COMMSIOSTARTUP_OKAY )
              {
                 status_okay = FALSE;
                 GST_ERROR("Server sent bad startup status");
              }
           }
           else
           {
              status_okay = FALSE;
              GST_ERROR("Error in remote_offload_comms_io_read");
           }
        }
        else
        {
           status_okay = FALSE;
           GST_ERROR("Error in remote_offload_comms_io_write");
           break;
        }
     }
   }

   g_array_free(commsioarray, TRUE);
   g_hash_table_destroy(hash_id_to_comms_channel);

   return status_okay;
}

static void ClearIdCommsIOEntry(gpointer data)
{
   ChannelIdCommsIOPair *pair = (ChannelIdCommsIOPair *)data;
   g_object_unref(pair->commsio);
}

static gpointer CommsIOTemporaryThread(void *arg)
{
   CommsIOTmpThreadPrivate *tmppriv = (CommsIOTmpThreadPrivate *)arg;

   RemoteOffloadCommsIO *pcommsio = tmppriv->commsio;
   RemoteOffloadPipelineSpawnerPrivate *priv = tmppriv->spawn_priv;

   GST_DEBUG_OBJECT(pcommsio, "CommsIOTemporaryThread starting");

   g_free(tmppriv);

   gboolean start_pipeline = FALSE;
   guint64 handle = 0;

   //reader loop
   while(1)
   {
      RemoteOffloadCommsIOResult res;

      guint startup_code = 0;
      res = remote_offload_comms_io_read(pcommsio, (guint8 *)&startup_code,
                                        sizeof(startup_code));
      if( res != REMOTEOFFLOADCOMMSIO_SUCCESS )
      {
         GST_ERROR_OBJECT(pcommsio, "Error in remote_offload_comms_io_read");
         break;
      }

      gboolean status_ok = TRUE;
      gboolean end = FALSE;

      switch(startup_code)
      {
         case COMMSIOSTARTUP_NEW_PIPELINE_PLACEHOLDER:
         {
            GST_DEBUG_OBJECT(pcommsio,
                          "CommsIOTemporaryThread: COMMSIOSTARTUP_NEW_PIPELINE_PLACEHOLDER start");
            PipelinePlaceholder *placeholder = g_malloc(sizeof(PipelinePlaceholder));
            placeholder->spawn_priv = priv;
            placeholder->id_commsio_pair_array =
                  g_array_new(FALSE, FALSE, sizeof(ChannelIdCommsIOPair));
            g_array_set_clear_func(placeholder->id_commsio_pair_array, ClearIdCommsIOEntry);
            g_mutex_lock (&priv->placeholdermutex);
            g_hash_table_insert(priv->pipelineplaceholderhash, placeholder, placeholder);
            g_mutex_unlock (&priv->placeholdermutex);

            handle = (guint64)(gulong)placeholder;
            GST_DEBUG_OBJECT(pcommsio,
                            "CommsIOTemporaryThread: Sending handle=%"G_GUINT64_FORMAT"\n", handle);
            res = remote_offload_comms_io_write(pcommsio, (guint8 *)&handle, sizeof(handle));

            if( res != REMOTEOFFLOADCOMMSIO_SUCCESS )
            {
               GST_ERROR_OBJECT(pcommsio, "Error in remote_offload_comms_io_write");
               status_ok = FALSE;
               break;
            }
            GST_DEBUG_OBJECT(pcommsio,
                            "CommsIOTemporaryThread: COMMSIOSTARTUP_NEW_PIPELINE_PLACEHOLDER end");
         }
         break;

         case COMMSIOSTARTUP_REGISTER_CHANNEL:
         {
            GST_DEBUG_OBJECT(pcommsio,
                            "CommsIOTemporaryThread: COMMSIOSTARTUP_REGISTER_CHANNEL start");
            res = remote_offload_comms_io_read(pcommsio, (guint8 *)&handle,
                                        sizeof(handle));
            if( res != REMOTEOFFLOADCOMMSIO_SUCCESS )
            {
               GST_ERROR_OBJECT(pcommsio, "Error in remote_offload_comms_io_read");
               status_ok = FALSE;
               break;
            }

            GST_DEBUG_OBJECT(pcommsio,
                            "CommsIOTemporaryThread: COMMSIOSTARTUP_REGISTER_CHANNEL "
                            "handle=%"G_GUINT64_FORMAT, handle);

            gint channel_id;
            res = remote_offload_comms_io_read(pcommsio, (guint8 *)&channel_id,
                                        sizeof(channel_id));
            if( res != REMOTEOFFLOADCOMMSIO_SUCCESS )
            {
               GST_ERROR_OBJECT(pcommsio, "Error in remote_offload_comms_io_read");
               status_ok = FALSE;
               break;
            }

            GST_DEBUG_OBJECT(pcommsio, "CommsIOTemporaryThread: "
                  "                    COMMSIOSTARTUP_REGISTER_CHANNEL channel_id=%d", channel_id);

            g_mutex_lock (&priv->placeholdermutex);
            PipelinePlaceholder *placeholder = (PipelinePlaceholder *)(gulong)handle;
            if( g_hash_table_contains(priv->pipelineplaceholderhash, placeholder) )
            {
               ChannelIdCommsIOPair pair;
               pair.channel_id = channel_id;
               pair.commsio = g_object_ref(pcommsio);
               g_array_append_val(placeholder->id_commsio_pair_array, pair);
            }
            else
            {
              GST_ERROR_OBJECT(pcommsio, "COMMSIOSTARTUP_REGISTER_CHANNEL Invalid handle");
              status_ok = FALSE;
            }
            g_mutex_unlock (&priv->placeholdermutex);

            GST_DEBUG_OBJECT(pcommsio, "CommsIOTemporaryThread: "
                                      "COMMSIOSTARTUP_REGISTER_CHANNEL end");

         }
         break;

         case COMMSIOSTARTUP_DONE:
         {
            GST_DEBUG_OBJECT(pcommsio, "CommsIOTemporaryThread: COMMSIOSTARTUP_DONE");
            //host is instructing this temporary thread to end
            end = TRUE;
         }
         break;

         case COMMSIOSTARTUP_START_PIPELINE:
         {
            GST_DEBUG_OBJECT(pcommsio, "CommsIOTemporaryThread: COMMSIOSTARTUP_START_PIPELINE");
            end = TRUE;
            start_pipeline = TRUE;
         }
         break;
      }

      //send back status
      guint32 status_to_send = 0;
      if( status_ok )
      {
         status_to_send = COMMSIOSTARTUP_OKAY;
      }
      else
      {
         status_to_send = 0;
      }

      res = remote_offload_comms_io_write(pcommsio,
                                          (guint8 *)&status_to_send, sizeof(status_to_send));
      if( res != REMOTEOFFLOADCOMMSIO_SUCCESS )
      {
         status_ok = FALSE;
         GST_ERROR_OBJECT(pcommsio,
                          "Error sending status (of 0x%x) back to host", status_to_send);
      }

      if( !status_ok )
         break;

      if( end )
         break;
   }

   if( start_pipeline )
   {
      if( handle )
      {
        PipelinePlaceholder *placeholder = (PipelinePlaceholder *)(gulong)handle;
        g_mutex_lock (&priv->placeholdermutex);
        if( g_hash_table_contains(priv->pipelineplaceholderhash, placeholder) )
        {
           g_hash_table_remove (priv->pipelineplaceholderhash,
                                placeholder);
        }
        else
        {
           placeholder = NULL;
           GST_ERROR_OBJECT(pcommsio, "Invalid handle");
        }
        g_mutex_unlock (&priv->placeholdermutex);

        if( placeholder )
        {
           GST_DEBUG_OBJECT(pcommsio, "Spawning pipeline instance thread");
           GThread *remoteoffloadinstance_thread =
                  g_thread_new ("remoteinstance",
                  (GThreadFunc)RemotePipelineInstanceThread,
                  placeholder);
           g_thread_unref(remoteoffloadinstance_thread);
        }

      }
      else
      {
         GST_ERROR_OBJECT(pcommsio, "Invalid handle");
      }
   }

   g_object_unref(pcommsio);

   GST_DEBUG_OBJECT(pcommsio, "CommsIOTemporaryThread ending");

   return NULL;
}


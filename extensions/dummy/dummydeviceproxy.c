/*
 *  dummydeviceproxy.c - DummyDeviceProxy object
 *
 *  Copyright (C) 2020 Intel Corporation
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
#include "dummydeviceproxy.h"
#include "remoteoffloaddeviceproxy.h"
#include "remoteoffloadcommsio_dummy.h"
#include "remoteoffloadclientserverutil.h"
#include "gstremoteoffloadpipeline.h"

/* Private structure definition. */
typedef struct
{
  GOptionContext *option_context;
  GArray *option_entries; //array of GOptionEntry's
  GThread *remoteoffloadinstance_thread;

} DummyDeviceProxyPrivate;

struct _DummyDeviceProxy
{
  GObject parent_instance;

  /* Other members, including private data. */
  DummyDeviceProxyPrivate priv;
};

GST_DEBUG_CATEGORY_STATIC (dummy_device_proxy_debug);
#define GST_CAT_DEFAULT dummy_device_proxy_debug

static void
dummy_device_proxy_interface_init (RemoteOffloadDeviceProxyInterface *iface);

G_DEFINE_TYPE_WITH_CODE (DummyDeviceProxy, dummy_device_proxy, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (REMOTEOFFLOADDEVICEPROXY_TYPE,
                         dummy_device_proxy_interface_init)
                         GST_DEBUG_CATEGORY_INIT (dummy_device_proxy_debug,
                         "remoteoffloaddeviceproxydummy", 0,
                         "debug category for DummyDeviceProxy"))

static gpointer DummyRemotePipelineInstanceThread (GHashTable *id_to_channel_hash)
{
   GST_INFO("Dummy RemotePipelineInstanceThread starting..");

   if( id_to_channel_hash )
   {
      RemoteOffloadPipeline *pPipeline =
            remote_offload_pipeline_new(NULL,
                                        id_to_channel_hash);

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
      GST_ERROR("id_to_channel_hash is NULL");
   }

   GST_INFO("Dummy RemotePipelineInstanceThread returning..");

   return NULL;
}
//Given a GArray of CommsChannelRequest's,
// return a channel_id(gint) to RemoteOffloadCommsChannel*
static GHashTable* dummy_deviceproxy_generate(RemoteOffloadDeviceProxy *proxy,
                                              GstBin *bin,
                                              GArray *commschannelrequests)
{
   (void)bin;

   if( !DEVICEPROXY_IS_DUMMY(proxy) ||
         !commschannelrequests ||
         (commschannelrequests->len < 1) )
      return NULL;

   DummyDeviceProxy *self = DEVICEPROXY_DUMMY (proxy);

   GHashTable *id_to_channel_hash_host = NULL;

   //host-side
   RemoteOffloadCommsIO *commsio_host = (RemoteOffloadCommsIO *)remote_offload_comms_io_dummy_new();
   if( commsio_host )
   {
      CommsChannelRequest *requests = (CommsChannelRequest *)commschannelrequests->data;
      GArray *id_commsio_pair_array_host = g_array_new(FALSE, FALSE, sizeof(ChannelIdCommsIOPair));
      for( guint requesti = 0; requesti < commschannelrequests->len; requesti++ )
      {
         ChannelIdCommsIOPair pair = {requests[requesti].channel_id, commsio_host};
         g_array_append_val(id_commsio_pair_array_host, pair);
         GST_INFO_OBJECT (self, "id_commsio_pair_array_host[%d] = (%d,%p)",
                          requesti, pair.channel_id, pair.commsio);
      }

      id_to_channel_hash_host =
            id_commsio_pair_array_to_id_to_channel_hash(id_commsio_pair_array_host);
      g_array_free(id_commsio_pair_array_host, TRUE);


      if( id_to_channel_hash_host )
      {
         gboolean bokay = FALSE;
         //"remote" side
         RemoteOffloadCommsIO *commsio_remote =
               (RemoteOffloadCommsIO *)remote_offload_comms_io_dummy_new();
         if( commsio_remote )
         {
            //connect the two dummy commsio's, so that they can talk to each other.
            if( connect_dummyio_pair((RemoteOffloadCommsIODummy*)commsio_host,
                                     (RemoteOffloadCommsIODummy*)commsio_remote) )
            {
               GArray *id_commsio_pair_array_remote =
                     g_array_new(FALSE, FALSE, sizeof(ChannelIdCommsIOPair));
               for( guint requesti = 0; requesti < commschannelrequests->len; requesti++ )
               {
                  ChannelIdCommsIOPair pair = {requests[requesti].channel_id, commsio_remote};
                  g_array_append_val(id_commsio_pair_array_remote, pair);
                  GST_INFO_OBJECT (self, "id_commsio_pair_array_remote[%d] = (%d,%p)",
                                   requesti, pair.channel_id, pair.commsio);
               }

               GHashTable *id_to_channel_hash_remote =
                     id_commsio_pair_array_to_id_to_channel_hash(id_commsio_pair_array_remote);

               g_array_free(id_commsio_pair_array_remote, TRUE);

               if( id_to_channel_hash_remote )
               {
                  //kick off an instance of a remoteoffloadpipeline on a separate thread.
                  GST_INFO_OBJECT(self, "Spawning remote pipeline instance thread");
                  self->priv.remoteoffloadinstance_thread =
                        g_thread_new ("remoteinstance",
                                      (GThreadFunc)DummyRemotePipelineInstanceThread,
                                       id_to_channel_hash_remote);

                  if( self->priv.remoteoffloadinstance_thread )
                  {
                     bokay = TRUE;
                  }
                  else
                  {
                     g_hash_table_unref(id_to_channel_hash_remote);
                     GST_ERROR_OBJECT (self, "Error spawning remote instance thread");
                  }
               }
               else
               {
                  GST_ERROR_OBJECT (self,
                                    "Error in id_commsio_pair_array_to_id_to_channel_hash"
                                    "(id_commsio_pair_array_remote)");
               }
            }
            else
            {
                GST_ERROR_OBJECT (self, "Error in connect_dummyio_pair()");
            }

            g_object_unref(commsio_remote);
         }
         else
         {
            GST_ERROR_OBJECT (self,
                              "Error creating remote-side RemoteOffloadCommsIODummy instance");
         }

         if( !bokay )
         {
            g_hash_table_unref(id_to_channel_hash_host);
            id_to_channel_hash_host = NULL;
         }
      }
      else
      {
         GST_ERROR_OBJECT (self,
                           "Error in id_commsio_pair_array_to_id_to_channel_hash"
                           "(id_commsio_pair_array_host)");
      }

      g_object_unref(commsio_host);
   }
   else
   {
      GST_ERROR_OBJECT (self, "Error creating host-side RemoteOffloadCommsIODummy instance");
   }

   return id_to_channel_hash_host;
}

static gboolean dummy_deviceproxy_set_arguments(RemoteOffloadDeviceProxy *proxy,
                                                  gchar *arguments_string)
{
   if( !DEVICEPROXY_IS_DUMMY(proxy) )
      return FALSE;

   if( !arguments_string )
      return TRUE;

   return TRUE;
}

static void
dummy_device_proxy_interface_init (RemoteOffloadDeviceProxyInterface *iface)
{
   iface->deviceproxy_generate_commschannels = dummy_deviceproxy_generate;
   iface->deviceproxy_set_arguments = dummy_deviceproxy_set_arguments;
}

static void
dummy_device_proxy_finalize (GObject *gobject)
{
  DummyDeviceProxy *self = DEVICEPROXY_DUMMY (gobject);

  if( self->priv.remoteoffloadinstance_thread )
  {
     g_thread_join (self->priv.remoteoffloadinstance_thread);
     self->priv.remoteoffloadinstance_thread = NULL;
  }

  g_option_context_free(self->priv.option_context);
  g_array_free(self->priv.option_entries, TRUE);
  G_OBJECT_CLASS (dummy_device_proxy_parent_class)->finalize (gobject);
}

static void
dummy_device_proxy_class_init (DummyDeviceProxyClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = dummy_device_proxy_finalize;
}

static void
dummy_device_proxy_init (DummyDeviceProxy *self)
{
   self->priv.remoteoffloadinstance_thread = NULL;
   self->priv.option_context = g_option_context_new(" - Dummy Comms Channel Generator");
   self->priv.option_entries = g_array_new(FALSE, FALSE, sizeof(GOptionEntry));
   g_option_context_set_summary(self->priv.option_context,
                                "Dummy Comms Channel Generator Options:");

   GOptionEntry null_entry = { NULL };
   g_array_append_val(self->priv.option_entries, null_entry);


   g_option_context_set_help_enabled(self->priv.option_context, FALSE);
   g_option_context_add_main_entries (self->priv.option_context,
                                      (GOptionEntry*)self->priv.option_entries->data, NULL);

}

DummyDeviceProxy *dummy_device_proxy_new ()
{
   return g_object_new(DUMMYDEVICEPROXY_TYPE, NULL);
}

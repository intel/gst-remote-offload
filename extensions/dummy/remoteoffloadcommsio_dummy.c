/*
 *  remoteoffloadcommsio_dummy.c - RemoteOffloadCommsIODummy object
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

#include "remoteoffloadcommsio_dummy.h"
#include "remoteoffloadcommsio.h"
#ifndef NO_SAFESTR
  #include <safe_mem_lib.h>
#else
  #include <string.h>
#endif
typedef struct
{
   guint8 *data;
   guint64 size;
}QueueEntry;

/* Private structure definition. */
typedef struct
{
  //instance of peer dummy that we are
  // sending/receiving data to/from
  RemoteOffloadCommsIODummy *peer;

  GMutex datareceivedmutex;
  GCond  datareceivedcond;
  GQueue *dataReceivedQueue;
  guint64 currentOffset;
  gboolean shutdownAsserted;

  /* stuff */
} RemoteOffloadCommsIODummyPrivate;

struct _RemoteOffloadCommsIODummy
{
  GObject parent_instance;


  /* Other members, including private data. */
  RemoteOffloadCommsIODummyPrivate priv;
};

GST_DEBUG_CATEGORY_STATIC (comms_io_dummy_debug);
#define GST_CAT_DEFAULT comms_io_dummy_debug

static void remote_offload_comms_io_dummy_interface_init (RemoteOffloadCommsIOInterface *iface);

G_DEFINE_TYPE_WITH_CODE (RemoteOffloadCommsIODummy, remote_offload_comms_io_dummy, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (REMOTEOFFLOADCOMMSIO_TYPE,
                         remote_offload_comms_io_dummy_interface_init)
                         GST_DEBUG_CATEGORY_INIT (comms_io_dummy_debug,
                         "remoteoffloadcommsiodummy", 0,
                         "debug category for RemoteOffloadCommsIODummy"))

static RemoteOffloadCommsIOResult
remote_offload_comms_io_dummy_read(RemoteOffloadCommsIO *commsio,
                                   guint8 *buf,
                                   guint64 size)
{
  RemoteOffloadCommsIODummy *pCommsIODummy = REMOTEOFFLOAD_COMMSIODUMMY(commsio);

  if( !buf || !size )
     return REMOTEOFFLOADCOMMSIO_FAIL;

  guint64 bytes_to_receive = size;
  while(bytes_to_receive > 0)
  {
     g_mutex_lock(&pCommsIODummy->priv.datareceivedmutex);
     gboolean bShutdown = pCommsIODummy->priv.shutdownAsserted;
     QueueEntry *entry = g_queue_peek_head(pCommsIODummy->priv.dataReceivedQueue);
     if( !entry && !bShutdown )
     {
        //no notification buf's in the queue. wait for one.
        g_cond_wait (&pCommsIODummy->priv.datareceivedcond, &pCommsIODummy->priv.datareceivedmutex);

        entry = g_queue_peek_head(pCommsIODummy->priv.dataReceivedQueue);
     }
     bShutdown = pCommsIODummy->priv.shutdownAsserted;
     g_mutex_unlock(&pCommsIODummy->priv.datareceivedmutex);

     if( bShutdown )
     {
        return REMOTEOFFLOADCOMMSIO_CONNECTION_CLOSED;
     }

     if( G_UNLIKELY(!entry) )
     {
        return REMOTEOFFLOADCOMMSIO_FAIL;
     }

     guint64 bytes_left_in_entry = entry->size - pCommsIODummy->priv.currentOffset;

     if( bytes_left_in_entry )
     {;
        guint64 bytes_to_copy = MIN(bytes_to_receive, bytes_left_in_entry);
        guint8 *entry_data = entry->data + pCommsIODummy->priv.currentOffset;

#ifndef NO_SAFESTR
        memcpy_s(buf,
                 bytes_to_copy,
                 entry_data,
                 bytes_to_copy);
#else
        memcpy(buf,
              entry_data,
              bytes_to_copy);
#endif

        buf += bytes_to_copy;
        bytes_to_receive -= bytes_to_copy;

        pCommsIODummy->priv.currentOffset += bytes_to_copy;
        bytes_left_in_entry -= bytes_to_copy;
     }

     if( !bytes_left_in_entry )
     {
        pCommsIODummy->priv.currentOffset = 0;
        g_mutex_lock(&pCommsIODummy->priv.datareceivedmutex);
        g_queue_pop_head(pCommsIODummy->priv.dataReceivedQueue);
        g_mutex_unlock(&pCommsIODummy->priv.datareceivedmutex);

        g_free(entry->data);
        g_free(entry);
     }

  }

  return REMOTEOFFLOADCOMMSIO_SUCCESS;
}

RemoteOffloadCommsIOResult remote_offload_comms_io_dummy_write(RemoteOffloadCommsIO *commsio,
                                                               guint8 *buf,
                                                               guint64 size)
{
   RemoteOffloadCommsIODummy *pCommsIODummy = REMOTEOFFLOAD_COMMSIODUMMY(commsio);

   if( !buf || !size )
     return REMOTEOFFLOADCOMMSIO_FAIL;

   //TODO: probably not thread safe here.
   if( pCommsIODummy->priv.shutdownAsserted )
      return REMOTEOFFLOADCOMMSIO_CONNECTION_CLOSED;

   if( !pCommsIODummy->priv.peer )
      return REMOTEOFFLOADCOMMSIO_FAIL;

   QueueEntry *entry = g_malloc(sizeof(QueueEntry));
   entry->data = g_malloc(size);
   if( !entry->data )
   {
      GST_ERROR("Error in g_malloc(%"G_GUINT64_FORMAT")", size);
      g_free(entry);
      return REMOTEOFFLOADCOMMSIO_FAIL;
   }

   entry->size = size;

  #ifndef NO_SAFESTR
       memcpy_s(entry->data, size, buf, size);
  #else
       memcpy(entry->data, buf, size);
  #endif


  //push this data entry to the peer's queue
  g_mutex_lock(&pCommsIODummy->priv.peer->priv.datareceivedmutex);
  g_queue_push_tail(pCommsIODummy->priv.peer->priv.dataReceivedQueue, entry);
  g_cond_broadcast(&pCommsIODummy->priv.peer->priv.datareceivedcond);
  g_mutex_unlock(&pCommsIODummy->priv.peer->priv.datareceivedmutex);

  return REMOTEOFFLOADCOMMSIO_SUCCESS;
}

static void remote_offload_comms_io_dummy_shutdown(RemoteOffloadCommsIO *commsio)
{
   RemoteOffloadCommsIODummy *pCommsIODummy = REMOTEOFFLOAD_COMMSIODUMMY(commsio);

   g_mutex_lock(&pCommsIODummy->priv.datareceivedmutex);
   pCommsIODummy->priv.shutdownAsserted = TRUE;
   pCommsIODummy->priv.peer = NULL;
   g_cond_broadcast(&pCommsIODummy->priv.datareceivedcond);
   g_mutex_unlock(&pCommsIODummy->priv.datareceivedmutex);
}

static GList * remote_offload_comms_io_dummy_get_consumable_memfeatures
        (RemoteOffloadCommsIO *commsio)
{
   GList *consumable_mem_features = NULL;

   //As far as can be observed from testing, memory:VASurface is mappable from a READ
   // perspective.
   consumable_mem_features = g_list_append(consumable_mem_features,
                                gst_caps_features_new("memory:VASurface", NULL));

   return consumable_mem_features;
}

static void
remote_offload_comms_io_dummy_interface_init (RemoteOffloadCommsIOInterface *iface)
{
  iface->read = remote_offload_comms_io_dummy_read;
  iface->write = remote_offload_comms_io_dummy_write;
  iface->shutdown = remote_offload_comms_io_dummy_shutdown;
  iface->get_consumable_memfeatures = remote_offload_comms_io_dummy_get_consumable_memfeatures;
}


static void
remote_offload_comms_io_dummy_set_property (GObject      *object,
                                             guint         property_id,
                                             const GValue *value,
                                             GParamSpec   *pspec)
{
  //RemoteOffloadCommsIODummy *pCommsIODummy = REMOTEOFFLOAD_COMMSIODUMMY(object);

  switch (property_id)
  {
    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
remote_offload_comms_io_dummy_finalize (GObject *gobject)
{
  RemoteOffloadCommsIODummy *pCommsIODummy = REMOTEOFFLOAD_COMMSIODUMMY(gobject);

  g_mutex_clear(&pCommsIODummy->priv.datareceivedmutex);
  g_cond_clear(&pCommsIODummy->priv.datareceivedcond);

  //clear & free any remaining queue entries
  while( !g_queue_is_empty(pCommsIODummy->priv.dataReceivedQueue) )
  {
     QueueEntry *entry = g_queue_pop_head(pCommsIODummy->priv.dataReceivedQueue);

     if( entry )
     {
        g_free(entry->data);
        g_free(entry);
     }
  }

  g_queue_free(pCommsIODummy->priv.dataReceivedQueue);

  G_OBJECT_CLASS (remote_offload_comms_io_dummy_parent_class)->finalize (gobject);
}

static void
remote_offload_comms_io_dummy_class_init (RemoteOffloadCommsIODummyClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = remote_offload_comms_io_dummy_set_property;

  object_class->finalize = remote_offload_comms_io_dummy_finalize;
}

static void
remote_offload_comms_io_dummy_init (RemoteOffloadCommsIODummy *self)
{
  g_mutex_init(&self->priv.datareceivedmutex);
  g_cond_init(&self->priv.datareceivedcond);
  self->priv.dataReceivedQueue = g_queue_new ();
  self->priv.shutdownAsserted = FALSE;
  self->priv.currentOffset = 0;
  self->priv.peer = NULL;

}

RemoteOffloadCommsIODummy *remote_offload_comms_io_dummy_new ()
{
  RemoteOffloadCommsIODummy *pCommsIODummy =
        g_object_new(REMOTEOFFLOADCOMMSIODUMMY_TYPE, NULL);

  return pCommsIODummy;
}

gboolean connect_dummyio_pair(RemoteOffloadCommsIODummy *inst0,
                              RemoteOffloadCommsIODummy *inst1)
{
   if( !REMOTEOFFLOAD_IS_COMMSIODUMMY(inst0) ||
       !REMOTEOFFLOAD_IS_COMMSIODUMMY(inst1) )
   {
      return FALSE;
   }

   if( inst0 == inst1 )
   {
      return FALSE;
   }

   //set the peer of each instance to the other one
   inst0->priv.peer = inst1;
   inst1->priv.peer = inst0;

   return TRUE;
}




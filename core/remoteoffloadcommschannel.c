/*
 *  remoteoffloadcommschannel.c - RemoteOffloadCommsChannel object
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
#ifndef NO_SAFESTR
  #include <safe_mem_lib.h>
#else
  #include <string.h>
#endif
#include "remoteoffloadcommschannel.h"
#include "remoteoffloadcomms.h"
#include "remoteoffloadprivateinterfaces.h"
#include "remoteoffloaddataexchanger.h"
#include "remoteoffloadresponse.h"


#define DEFAULT_NUM_RESPONSE_POOL_ENTRIES 32
#define DEFAULT_NUM_DATA_TRANSFER_RECEIVED_ENTRIES 8

enum
{
  PROP_COMMS = 1,
  PROP_ID,
  N_PROPERTIES
};

//THIS IS TEMPORARY UNTIL A MECHANISM IS IMPLEMENTED
// TO DYNAMICALLY REGISTER EXCHANGERS BETWEEN HOST & REMOTE
enum DataExchangerType
{
   DE_TYPE_UNKNOWN = 0,
   DE_TYPE_RESPONSE,
   DE_TYPE_QUERY,
   DE_TYPE_EVENT,
   DE_TYPE_BUFFER,
   DE_TYPE_BIN,
   DE_TYPE_EOS,
   DE_TYPE_STATECHANGE,
   DE_TYPE_PIPELINEERROR,
   DE_TYPE_PING,
   DE_TYPE_PING_RESPONSE,
   DE_TYPE_QUEUESTATS,
   DE_TYPE_QUEUESTATS_RESPONSE,
   DE_TYPE_HEARTBEAT,
   DE_TYPE_GENERIC,
   DE_NUM_TYPES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

/* Private structure definition. */
typedef struct {
   //only used for initialization
   RemoteOffloadComms *pcomms;
   gboolean is_state_okay;
   gint id;

   gboolean bcancelledstate;

   GMutex failcallbackmutex;
   comms_failure_callback_f comms_failure_callback;
   void *comms_failure_callback_user_data;

   GMutex responsePoolMutex;
   GHashTable *activeWaitingEntryMap;

   GArray *exchangerArray;
   GList *cachedDataTransferList;  //list of data transfers that have been
                                   // received before their data exchangers
                                   // were registered. Should very rarely
                                   // contain entries at startup.

   GMutex receiverthrmutex;
   GCond  receiverthrcond;
   GCond  idlecond;
   GThread *receiver_thread;
   gboolean bThreadRun;
   gboolean bIdle;

   GQueue *activedataTransferEntryQueue;
   GQueue *dataTransferEntryFreePool;
}RemoteOffloadCommsChannelPrivate;

struct _RemoteOffloadCommsChannel
{
  GObject parent_instance;

  /* Other members, including private data. */
  RemoteOffloadCommsChannelPrivate priv;
};

static void
remote_offload_comms_channel_callback_interface_init (RemoteOffloadCommsCallbackInterface *iface);
static gpointer remote_offload_comms_channel_receiver_thread(RemoteOffloadCommsChannel *self);

//Cancel a response currently (or in the process of getting) waited on.
static void remote_offload_response_cancel(RemoteOffloadResponse *response);

GST_DEBUG_CATEGORY_STATIC (remote_offload_comms_channel_debug);
#define GST_CAT_DEFAULT remote_offload_comms_channel_debug

G_DEFINE_TYPE_WITH_CODE(RemoteOffloadCommsChannel, remote_offload_comms_channel, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (REMOTEOFFLOADCOMMSCALLBACK_TYPE,
                         remote_offload_comms_channel_callback_interface_init)
                         GST_DEBUG_CATEGORY_INIT (remote_offload_comms_channel_debug,
                         "remoteoffloadcommschannel", 0,
                        "debug category for RemoteOffloadCommsChannel"))

//define the response type
typedef struct
{
   GMutex responsemutex;
   GCond responsecond;
   GArray *response_mem_array;
   gboolean canceled;
}RemoteOffloadResponsePrivate;

struct _RemoteOffloadResponse
{
  GObject parent_instance;

  /* Other members, including private data. */
  RemoteOffloadResponsePrivate priv;
};

G_DEFINE_TYPE(RemoteOffloadResponse, remote_offload_response, G_TYPE_OBJECT);

static void clear_response_memarray(RemoteOffloadResponse *self)
{
  if( self->priv.response_mem_array )
  {
     for(guint i = 0; i < self->priv.response_mem_array->len; i++)
     {
        gst_memory_unref(g_array_index(self->priv.response_mem_array,
                                       GstMemory *,
                                       i));
     }
     g_array_unref(self->priv.response_mem_array);
     self->priv.response_mem_array = NULL;
  }
}

typedef struct
{
   DataTransferHeader header;
   GArray *dataSegments;
}DataTransferReceivedEntry;

static inline DataTransferReceivedEntry *data_transfer_received_entry_malloc()
{
   DataTransferReceivedEntry *entry = g_malloc0(sizeof(DataTransferReceivedEntry));

   return entry;
}

static inline void data_transfer_received_entry_free(DataTransferReceivedEntry *entry)
{
   g_free(entry);
}

static inline DataTransferReceivedEntry *
data_transfer_received_entry_request(RemoteOffloadCommsChannel *pCommsChannel)
{
   DataTransferReceivedEntry *entry = NULL;
   g_mutex_lock (&(pCommsChannel->priv.receiverthrmutex));
   entry = g_queue_pop_head(pCommsChannel->priv.dataTransferEntryFreePool);
   g_mutex_unlock (&(pCommsChannel->priv.receiverthrmutex));

   if( !entry )
      entry = data_transfer_received_entry_malloc();

   return entry;
}

static inline void
data_transfer_received_entry_done(RemoteOffloadCommsChannel *pCommsChannel,
                                  DataTransferReceivedEntry *entry)
{
   g_mutex_lock (&(pCommsChannel->priv.receiverthrmutex));
   if( entry->dataSegments )
   {
      for( gint i = 0; i < entry->dataSegments->len; i++ )
      {
         GstMemory *mem = g_array_index(entry->dataSegments, GstMemory *, i);
         gst_memory_unref (mem);
      }
      g_array_unref(entry->dataSegments);
   }
   entry->dataSegments = 0;

   g_queue_push_head(pCommsChannel->priv.dataTransferEntryFreePool, entry);
   g_mutex_unlock (&(pCommsChannel->priv.receiverthrmutex));
}


//This is called right after _init() and set_properties calls for default
// props passed into g_object_new()
static void remote_offload_comms_channel_constructed(GObject *object)
{
  RemoteOffloadCommsChannel *pCommsChannel = REMOTEOFFLOAD_COMMSCHANNEL(object);

  if( pCommsChannel->priv.pcomms && (pCommsChannel->priv.id >= 0) )
  {
     gchar name[25];
     g_snprintf(name, 25, "CommsChannel%d", pCommsChannel->priv.id);
     pCommsChannel->priv.receiver_thread =
        g_thread_new (name,
                      (GThreadFunc) remote_offload_comms_channel_receiver_thread,
                      pCommsChannel);

     //pCommsChannel->priv.reader_thread =
     //   g_thread_new ("CommsReader", (GThreadFunc) RemoteOffloadCommsReader, object);
     if( remote_offload_comms_register_channel(pCommsChannel->priv.pcomms, pCommsChannel) )
     {
        pCommsChannel->priv.is_state_okay = TRUE;
     }
  }

  G_OBJECT_CLASS (remote_offload_comms_channel_parent_class)->constructed (object);
}

static void remote_offload_comms_channel_get_property (GObject    *object,
                                                       guint       property_id,
                                                       GValue     *value,
                                                       GParamSpec *pspec)
{
   RemoteOffloadCommsChannel *pCommsChannel = REMOTEOFFLOAD_COMMSCHANNEL(object);
    switch (property_id)
    {
    case PROP_ID:
        g_value_set_int(value, pCommsChannel->priv.id);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void
remote_offload_comms_channel_set_property (GObject      *object,
                                           guint         property_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
  RemoteOffloadCommsChannel *pCommsChannel = REMOTEOFFLOAD_COMMSCHANNEL(object);
  switch (property_id)
  {
    case PROP_COMMS:
    {
      RemoteOffloadComms *pCommsTmp = g_value_get_pointer (value);
      if( REMOTEOFFLOAD_IS_COMMS(pCommsTmp))
        pCommsChannel->priv.pcomms = g_object_ref(pCommsTmp);
    }
    break;

    case PROP_ID:
    {
      pCommsChannel->priv.id = g_value_get_int (value);
    }
    break;


    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static GstMemory*
remote_offload_comms_channel_callback_allocate_data_segment(RemoteOffloadCommsCallback *callback,
                               guint16 dataTransferType,
                               guint16 segmentIndex,
                               guint64 segmentSize,
                               const GArray *segment_mem_array_so_far)
{
   RemoteOffloadCommsChannel *channel = REMOTEOFFLOAD_COMMSCHANNEL(callback);
   GstMemory *mem = NULL;

   if( G_LIKELY(dataTransferType < channel->priv.exchangerArray->len) )
   {
      if( dataTransferType == DE_TYPE_RESPONSE )
      {
         mem = gst_allocator_alloc (NULL, segmentSize, NULL);
      }
      else
      {

         RemoteOffloadCommsCallback **exchangers =
               (RemoteOffloadCommsCallback **)channel->priv.exchangerArray->data;
         RemoteOffloadCommsCallback *exchanger = exchangers[dataTransferType];

         //make call to exchanger here.
         if( exchanger )
         {
           mem = remote_offload_comms_callback_allocate_data_segment(exchanger,
                                                               dataTransferType,
                                                               segmentIndex,
                                                               segmentSize,
                                                               segment_mem_array_so_far);
         }
      }
   }

   return mem;
}

static inline void
remote_offload_comms_channel_push_entry_to_active_queue(RemoteOffloadCommsChannel *channel,
                                                        DataTransferReceivedEntry *entry)
{
   g_mutex_lock (&(channel->priv.receiverthrmutex));
   g_queue_push_tail(channel->priv.activedataTransferEntryQueue, entry);
   g_cond_broadcast(&(channel->priv.receiverthrcond));
   g_mutex_unlock (&(channel->priv.receiverthrmutex));
}

void remote_offload_comms_channel_cancel_all(RemoteOffloadCommsChannel *channel)
{
   if( !REMOTEOFFLOAD_IS_COMMSCHANNEL(channel) ) return;

   g_mutex_lock(&channel->priv.responsePoolMutex);
   channel->priv.bcancelledstate = TRUE;
   GHashTableIter iter;
   gpointer key, value;
   g_hash_table_iter_init (&iter, channel->priv.activeWaitingEntryMap);
   while (g_hash_table_iter_next (&iter, &key, &value))
   {
      RemoteOffloadResponse *response = (RemoteOffloadResponse *)value;
      g_hash_table_iter_remove (&iter);
      remote_offload_response_cancel(response);
   }
   g_mutex_unlock(&(channel->priv.responsePoolMutex));
}

void remote_offload_comms_channel_error_state(RemoteOffloadCommsChannel *channel)
{
   if( !REMOTEOFFLOAD_IS_COMMSCHANNEL(channel) ) return;

   if( channel->priv.pcomms )
      remote_offload_comms_error_state(channel->priv.pcomms);

   remote_offload_comms_channel_cancel_all(channel);
}

void remote_offload_comms_channel_finish(RemoteOffloadCommsChannel *pCommsChannel)
{
   if( !REMOTEOFFLOAD_IS_COMMSCHANNEL(pCommsChannel) ) return;

   if( pCommsChannel->priv.pcomms )
      remote_offload_comms_finish(pCommsChannel->priv.pcomms);
}

void remote_offload_comms_channel_set_comms_failure_callback(RemoteOffloadCommsChannel *channel,
                                                            comms_failure_callback_f callback,
                                                            void *user_data)
{
   if( !REMOTEOFFLOAD_IS_COMMSCHANNEL(channel) ) return;

   g_mutex_lock(&channel->priv.failcallbackmutex);
   channel->priv.comms_failure_callback = callback;
   channel->priv.comms_failure_callback_user_data = user_data;
   g_mutex_unlock(&channel->priv.failcallbackmutex);
}

void
remote_offload_comms_channel_callback_data_transfer_received(RemoteOffloadCommsCallback *callback,
                                                             const DataTransferHeader *header,
                                                             GArray *segment_mem_array)
{
   RemoteOffloadCommsChannel *channel = REMOTEOFFLOAD_COMMSCHANNEL(callback);

   //special case for RESPONSES. Add the segment_mem_array directly
   // to the response object
   if( header->dataTransferType == DE_TYPE_RESPONSE )
   {
       RemoteOffloadResponse* response = NULL;
       g_mutex_lock(&channel->priv.responsePoolMutex);
       response = g_hash_table_lookup (channel->priv.activeWaitingEntryMap,
                                      (gconstpointer)(gulong)header->response_id);
       g_hash_table_remove(channel->priv.activeWaitingEntryMap,
                           (gconstpointer)(gulong)header->response_id);

       g_mutex_unlock(&(channel->priv.responsePoolMutex));

       if( response )
       {
          g_mutex_lock(&response->priv.responsemutex);
          response->priv.response_mem_array = g_array_ref(segment_mem_array);
          g_cond_broadcast(&response->priv.responsecond);
          g_mutex_unlock(&response->priv.responsemutex);
       }
       else
       {
          GST_WARNING_OBJECT (channel,
                              "id %"G_GUINT64_FORMAT" not found in response map",
                              header->response_id);

          //need to unref the memory
          if( segment_mem_array )
          {
             for( guint i = 0; i < segment_mem_array->len; i++ )
             {
                gst_memory_unref(g_array_index(segment_mem_array, GstMemory *, i));
             }
          }
       }
   }
   else
   {
      DataTransferReceivedEntry *entry = data_transfer_received_entry_request(channel);
      entry->dataSegments = g_array_ref(segment_mem_array);
      entry->header = *header;

      remote_offload_comms_channel_push_entry_to_active_queue(channel, entry);
   }
}

static gpointer remote_offload_comms_channel_receiver_thread(RemoteOffloadCommsChannel *self)
{
   GST_DEBUG_OBJECT (self, "CommsChannel id=%d thread start", self->priv.id);

   //Within this loop, the state of this mutex is LOCKED except when:
   // 1. The queue is empty, and this thread is waiting on a new queue entry
   //    to be pushed.
   // 2. This thread is currently executing a data exchanger's 'received' method.
   g_mutex_lock (&(self->priv.receiverthrmutex));
   self->priv.bIdle = FALSE;
   while( 1 )
   {
      DataTransferReceivedEntry *entry = g_queue_pop_head(self->priv.activedataTransferEntryQueue);
      //if there were no entries in the queue
      if(!entry)
      {
         //the queue is empty, therefore we're considered to be 'idle'
         // Set the flag and wake up any potential thread waiting on
         // an idle status (within remote_offload_comms_channel_unregister_exchanger
         // method )
         self->priv.bIdle = TRUE;
         g_cond_broadcast (&(self->priv.idlecond));

         //Note that the position of this check/break means that this thread
         // will only break out of this loop once all pending entries from
         // the queue have been processed.
         if( !self->priv.bThreadRun )
            break;

         //wait to be signaled (by thread who pushes new entry into queue)
         g_cond_wait (&(self->priv.receiverthrcond), &(self->priv.receiverthrmutex));
         self->priv.bIdle = FALSE;
      }

      RemoteOffloadCommsCallback *exchanger = NULL;
      if( entry )
      {
         //Map the dataTransferType to an exchanger object
         if( G_LIKELY(entry->header.dataTransferType < self->priv.exchangerArray->len) )
         {
           exchanger =
               g_array_index(self->priv.exchangerArray,
                             RemoteOffloadCommsCallback *,
                             entry->header.dataTransferType);
         }

         if( exchanger )
         {
            //don't hold the mutex while this thread resides within data exchanger's 'received' method.
            g_mutex_unlock (&(self->priv.receiverthrmutex));
            remote_offload_comms_callback_data_transfer_received(exchanger,
                                                                &(entry->header),
                                                                entry->dataSegments);
            //This will Unref each GstMemory data segment.
            // It is the exchanger's responsibility to
            // increase the ref count of GstMemory's
            // that need to stick around by calling
            // gst_memory_ref(GstMemory *mem);
            data_transfer_received_entry_done(self, entry);
            g_mutex_lock (&(self->priv.receiverthrmutex));
         }
         else
         {
            GST_WARNING_OBJECT (self,
                                "Exchanger for dataTransferType of %u is not yet registered. "
                                "Caching it.",
                                entry->header.dataTransferType);
              self->priv.cachedDataTransferList = g_list_append(self->priv.cachedDataTransferList,
                                                                entry);
         }
      }
   }
   g_mutex_unlock (&(self->priv.receiverthrmutex));

   GST_DEBUG_OBJECT (self, "CommsChannel id=%d thread end", self->priv.id);

   return NULL;
}

static void remote_offload_comms_channel_callback_comms_failure(RemoteOffloadCommsCallback *callback)
{
   RemoteOffloadCommsChannel *channel = REMOTEOFFLOAD_COMMSCHANNEL(callback);
   GST_ERROR_OBJECT (channel, "Comms failure detected.");
   g_mutex_lock(&channel->priv.failcallbackmutex);
   if( channel->priv.comms_failure_callback )
   {
     channel->priv.comms_failure_callback(channel,
                                          channel->priv.comms_failure_callback_user_data);
   }
   g_mutex_unlock(&channel->priv.failcallbackmutex);
}

static void
remote_offload_comms_channel_callback_interface_init (RemoteOffloadCommsCallbackInterface *iface)
{
  iface->allocate_data_segment = remote_offload_comms_channel_callback_allocate_data_segment;
  iface->data_transfer_received = remote_offload_comms_channel_callback_data_transfer_received;
  iface->comms_failure = remote_offload_comms_channel_callback_comms_failure;
}

static void
remote_offload_comms_channel_finalize (GObject *gobject)
{
  RemoteOffloadCommsChannel *pCommsChannel = REMOTEOFFLOAD_COMMSCHANNEL(gobject);

  if( pCommsChannel->priv.receiver_thread)
  {
    g_mutex_lock (&(pCommsChannel->priv.receiverthrmutex));
    pCommsChannel->priv.bThreadRun = FALSE;
    g_cond_broadcast (&(pCommsChannel->priv.receiverthrcond));
    g_mutex_unlock (&(pCommsChannel->priv.receiverthrmutex));
    g_thread_join (pCommsChannel->priv.receiver_thread);
  }

  //move the active entries to the free queue. This will in turn unref the GstMemory's
  // in the array, as well as the array itself
  {
     DataTransferReceivedEntry *pEntry =
           g_queue_pop_head(pCommsChannel->priv.activedataTransferEntryQueue);
     while( pEntry != NULL )
     {
        data_transfer_received_entry_done(pCommsChannel, pEntry);
        pEntry = g_queue_pop_head(pCommsChannel->priv.activedataTransferEntryQueue);
     }
  }
  g_queue_free(pCommsChannel->priv.activedataTransferEntryQueue);

  //if there are cached data entries, move them to the free pool also
  if( pCommsChannel->priv.cachedDataTransferList )
  {
     GList *entrylisti;
     for(entrylisti = pCommsChannel->priv.cachedDataTransferList;
         entrylisti != NULL;
         entrylisti = entrylisti->next )
     {
        DataTransferReceivedEntry *pEntry = (DataTransferReceivedEntry *)entrylisti->data;
        data_transfer_received_entry_done(pCommsChannel, pEntry);
     }
     g_list_free(pCommsChannel->priv.cachedDataTransferList);
  }

  {

     DataTransferReceivedEntry *pEntry =
           g_queue_pop_head(pCommsChannel->priv.dataTransferEntryFreePool);
     while( pEntry != NULL )
     {
        data_transfer_received_entry_free(pEntry);
        pEntry = g_queue_pop_head(pCommsChannel->priv.dataTransferEntryFreePool);
     }
  }
  g_queue_free(pCommsChannel->priv.dataTransferEntryFreePool);


  g_mutex_clear(&(pCommsChannel->priv.receiverthrmutex));
  g_cond_clear(&(pCommsChannel->priv.receiverthrcond));
  g_cond_clear(&(pCommsChannel->priv.idlecond));

  if( pCommsChannel->priv.pcomms )
  {
     g_object_unref(pCommsChannel->priv.pcomms);
  }

  g_mutex_clear(&(pCommsChannel->priv.responsePoolMutex));

  g_hash_table_destroy(pCommsChannel->priv.activeWaitingEntryMap);

  g_array_free(pCommsChannel->priv.exchangerArray, TRUE);

  g_mutex_clear(&pCommsChannel->priv.failcallbackmutex);

  G_OBJECT_CLASS (remote_offload_comms_channel_parent_class)->finalize (gobject);
}

static void
remote_offload_comms_channel_class_init (RemoteOffloadCommsChannelClass *klass)
{
   GObjectClass *object_class = G_OBJECT_CLASS (klass);

   object_class->set_property = remote_offload_comms_channel_set_property;
   object_class->get_property = remote_offload_comms_channel_get_property;

   obj_properties[PROP_COMMS] =
    g_param_spec_pointer ("comms",
                          "Comms",
                          "Comms object in use",
                          G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);

    obj_properties[PROP_ID] =
    g_param_spec_int ("id",
                      "Id",
                      "Comms Channel Id",
                      0,
                      INT_MAX,
                      0,
                      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);


   g_object_class_install_properties (object_class,
                                     N_PROPERTIES,
                                     obj_properties);


   object_class->constructed = remote_offload_comms_channel_constructed;
   object_class->finalize = remote_offload_comms_channel_finalize;
}

static void ActiveWaitingResponseDestroy (gpointer data)
{
   RemoteOffloadResponse *resp = (RemoteOffloadResponse *)data;
   g_object_unref(resp);
}

static void
remote_offload_comms_channel_init (RemoteOffloadCommsChannel *self)
{
  self->priv.pcomms = NULL;
  self->priv.is_state_okay = FALSE;
  self->priv.id = -1;
  g_mutex_init(&(self->priv.responsePoolMutex));

  g_mutex_init(&self->priv.failcallbackmutex);
  self->priv.comms_failure_callback = NULL;
  self->priv.comms_failure_callback_user_data = NULL;

  self->priv.bcancelledstate = FALSE;

  self->priv.activeWaitingEntryMap = g_hash_table_new_full(g_direct_hash,
                                                           g_direct_equal,
                                                           NULL,
                                                           ActiveWaitingResponseDestroy);

  self->priv.exchangerArray = g_array_sized_new(FALSE,
                                                TRUE,
                                                sizeof(RemoteOffloadCommsCallback *),
                                                DE_NUM_TYPES);
  self->priv.exchangerArray = g_array_set_size (self->priv.exchangerArray,
                  DE_NUM_TYPES);
  self->priv.cachedDataTransferList = NULL;

  g_mutex_init(&(self->priv.receiverthrmutex));
  g_cond_init(&(self->priv.receiverthrcond));
  g_cond_init(&(self->priv.idlecond));
  self->priv.receiver_thread = 0;
  self->priv.bThreadRun = TRUE;
  self->priv.bIdle = TRUE;

  self->priv.activedataTransferEntryQueue = g_queue_new();
  self->priv.dataTransferEntryFreePool = g_queue_new();

  for( int i = 0; i < DEFAULT_NUM_DATA_TRANSFER_RECEIVED_ENTRIES; i++ )
  {
     g_queue_push_head(self->priv.dataTransferEntryFreePool,
                       data_transfer_received_entry_malloc());
  }

}

RemoteOffloadCommsChannel *remote_offload_comms_channel_new(RemoteOffloadComms *comms, gint id)
{
  RemoteOffloadCommsChannel *pComms =
        g_object_new(REMOTEOFFLOADCOMMSCHANNEL_TYPE, "comms", comms, "id", id, NULL);

  if( pComms )
  {
     if( !pComms->priv.is_state_okay )
     {
        g_object_unref(pComms);
        pComms = NULL;
     }
  }

  return pComms;
}

//Obtain the RemoteOffloadComms for which this channel is associated with
RemoteOffloadComms *remote_offload_comms_channel_get_comms(RemoteOffloadCommsChannel *channel)
{
  if( !REMOTEOFFLOAD_IS_COMMSCHANNEL(channel) )
     return NULL;

  return channel->priv.pcomms;
}

gboolean remote_offload_comms_channel_unregister_exchanger(RemoteOffloadCommsChannel *channel,
                                                         RemoteOffloadDataExchanger *exchanger)
{
   if( !REMOTEOFFLOAD_IS_DATAEXCHANGER(exchanger) ||
       !REMOTEOFFLOAD_IS_COMMSCHANNEL(channel)  )
      return FALSE;


   g_mutex_lock (&(channel->priv.receiverthrmutex));
   //wait for receiver thread to become idle.
   // This ensures 2 things:
   // 1. That the receiver thread isn't currently within the current data exchanger's
   //    'received' function.
   // 2. That all pending received tasks for this have been completed.
   while( !channel->priv.bIdle )
   {
      g_cond_wait (&(channel->priv.idlecond), &(channel->priv.receiverthrmutex));
   }

   //remove this exchanger from our exchanger map.
   // This is to ensure that this data exchanger's 'received' method
   // won't be invoked after it's been unregistered.
   RemoteOffloadDataExchanger **exchangers =
            (RemoteOffloadDataExchanger **)channel->priv.exchangerArray->data;
   for( guint i = 0; i < channel->priv.exchangerArray->len; i++ )
   {
      if( exchangers[i] == exchanger )
      {
         exchangers[i] = NULL;
         break;
      }
   }
   g_mutex_unlock (&(channel->priv.receiverthrmutex));

   return TRUE;
}

gboolean remote_offload_comms_channel_register_exchanger(RemoteOffloadCommsChannel *channel,
                                                         RemoteOffloadDataExchanger *exchanger)
{

   if( !REMOTEOFFLOAD_IS_DATAEXCHANGER(exchanger) ||
       !REMOTEOFFLOAD_IS_COMMSCHANNEL(channel) ||
       !REMOTEOFFLOAD_IS_COMMSCALLBACK(exchanger) )
      return FALSE;

   gboolean ret = TRUE;
   guint16 id = DE_TYPE_UNKNOWN;

   gchar *exchangernamestr = NULL;
   g_object_get(exchanger, "exchangername", &exchangernamestr, NULL);

   g_mutex_lock (&(channel->priv.receiverthrmutex));
   if( exchangernamestr )
   {
      //THIS IS TEMPORARY UNTIL A MECHANISM IS IMPLEMENTED
      // TO DYNAMICALLY REGISTER EXCHANGERS BETWEEN HOST & REMOTE
      //TODO: Implement a handshaking method with remote channel
      // to set these id's dynamically.
      // Probably this means that this function will only add exchangers
      //  to a staging queue to be added to exchanger map later on

      if( g_strcmp0(exchangernamestr, "xlink.exchanger.query") == 0)
      {
         id = DE_TYPE_QUERY;
      }
      else
      if( g_strcmp0(exchangernamestr, "xlink.exchanger.event") == 0)
      {
         id = DE_TYPE_EVENT;
      }
      else
      if( g_strcmp0(exchangernamestr, "xlink.exchanger.buffer") == 0)
      {
         id = DE_TYPE_BUFFER;
      }
      else
      if( g_strcmp0(exchangernamestr, "xlink.exchanger.bin") == 0)
      {
         id = DE_TYPE_BIN;
      }
      else
      if( g_strcmp0(exchangernamestr, "xlink.exchanger.eos") == 0)
      {
         id = DE_TYPE_EOS;
      }
      else
      if( g_strcmp0(exchangernamestr, "xlink.exchanger.statechange") == 0)
      {
         id = DE_TYPE_STATECHANGE;
      }
      else
      if( g_strcmp0(exchangernamestr, "xlink.exchanger.errormessage") == 0)
      {
         id = DE_TYPE_PIPELINEERROR;
      }
      else
      if( g_strcmp0(exchangernamestr, "xlink.exchanger.ping") == 0)
      {
         id = DE_TYPE_PING;
      }
      else
      if( g_strcmp0(exchangernamestr, "xlink.exchanger.ping_response") == 0)
      {
         id = DE_TYPE_PING_RESPONSE;
      }
      else
      if( g_strcmp0(exchangernamestr, "xlink.exchanger.queuestats") == 0)
      {
         id = DE_TYPE_QUEUESTATS;
      }
      else
      if( g_strcmp0(exchangernamestr, "xlink.exchanger.queuestats_response") == 0)
      {
         id = DE_TYPE_QUEUESTATS_RESPONSE;
      }
      else
      if( g_strcmp0(exchangernamestr, "xlink.exchanger.heartbeat") == 0)
      {
         id = DE_TYPE_HEARTBEAT;
      }
      else
      if( g_strcmp0(exchangernamestr, "xlink.exchanger.generic") == 0)
      {
         id = DE_TYPE_GENERIC;
      }
      else
      {
         GST_ERROR_OBJECT (channel, "Invalid exchanger name %s", exchangernamestr);
         ret = FALSE;
      }

      g_free(exchangernamestr);
   }
   else
   {
      ret = FALSE;
   }

   if( ret )
   {
      RemoteOffloadDataExchanger **exchangers =
            (RemoteOffloadDataExchanger **)channel->priv.exchangerArray->data;
      exchangers[id] = exchanger;
      remote_offload_data_exchanger_set_id(exchanger, id);
   }

   //Check the cached DataTransferReceivedEntry List. If there are any entries intended for the
   // exchanger that we just registered, push them to the active queue.
   {
      GList *entrylisti = channel->priv.cachedDataTransferList;
      gboolean bwakeup = FALSE;
      while (entrylisti != NULL )
      {
         GList *next = entrylisti->next;
         DataTransferReceivedEntry *entry =
               (DataTransferReceivedEntry *)entrylisti->data;
         if( entry && entry->header.dataTransferType == id )
         {
            //remove this entry from the GList
            channel->priv.cachedDataTransferList =
                  g_list_delete_link(channel->priv.cachedDataTransferList, entrylisti);

            //push this entry to the active queue
            //Note, we don't call remote_offload_comms_channel_push_entry_to_active_queue
            // here because we are already holding the lock
            g_queue_push_tail(channel->priv.activedataTransferEntryQueue, entry);
            bwakeup = TRUE;
         }

         entrylisti = next;
      }

      //if we pushed something into the queue, wake up the receiver thread.
      if( bwakeup )
         g_cond_broadcast(&(channel->priv.receiverthrcond));
   }
   g_mutex_unlock (&(channel->priv.receiverthrmutex));

   return ret;
}


static void
remote_offload_response_finalize (GObject *gobject)
{
  RemoteOffloadResponse *self = REMOTEOFFLOAD_RESPONSE(gobject);

  g_mutex_clear(&self->priv.responsemutex);
  g_cond_clear(&self->priv.responsecond);

  clear_response_memarray(self);

  G_OBJECT_CLASS (remote_offload_response_parent_class)->finalize (gobject);
}

static void
remote_offload_response_class_init (RemoteOffloadResponseClass *klass)
{
   GObjectClass *object_class = G_OBJECT_CLASS (klass);

   object_class->finalize = remote_offload_response_finalize;
}

static void
remote_offload_response_init (RemoteOffloadResponse *self)
{

  self->priv.response_mem_array = NULL;
  g_mutex_init(&self->priv.responsemutex);
  g_cond_init(&self->priv.responsecond);
  self->priv.canceled = FALSE;
}

RemoteOffloadResponse *remote_offload_response_new()
{
   return g_object_new(REMOTEOFFLOADRESPONSE_TYPE, NULL);
}

RemoteOffloadResponseStatus remote_offload_response_wait(RemoteOffloadResponse *response,
                                                         gint32 timeoutmilliseconds)
{
   if( !REMOTEOFFLOAD_IS_RESPONSE(response) )
      return REMOTEOFFLOADRESPONSE_FAILURE;

   RemoteOffloadResponseStatus status = REMOTEOFFLOADRESPONSE_RECEIVED;
   g_mutex_lock(&response->priv.responsemutex);
   if( !response->priv.response_mem_array && !response->priv.canceled)
   {
      if( timeoutmilliseconds > 0 )
      {
         gint64 end_time = g_get_monotonic_time () + timeoutmilliseconds*1000;

         //wait for it
         if( !g_cond_wait_until (&response->priv.responsecond,
                                 &response->priv.responsemutex,
                                 end_time) )
         {
           GST_WARNING_OBJECT(response, "TIMEOUT waiting for response");
           status = REMOTEOFFLOADRESPONSE_TIMEOUT;
         }
      }
      else
      {
         g_cond_wait(&response->priv.responsecond,
                     &response->priv.responsemutex);
      }
   }

   if( response->priv.canceled )
   {
      status = REMOTEOFFLOADRESPONSE_CANCELLED;
      response->priv.canceled = FALSE;
   }
   g_mutex_unlock(&response->priv.responsemutex);

   return status;
}

void remote_offload_response_cancel(RemoteOffloadResponse *response)
{
   if( !REMOTEOFFLOAD_IS_RESPONSE(response) )
      return;

   g_mutex_lock(&response->priv.responsemutex);
   response->priv.canceled = TRUE;
   g_cond_broadcast(&response->priv.responsecond);
   g_mutex_unlock(&response->priv.responsemutex);
}

GArray *remote_offload_response_steal_mem_array(RemoteOffloadResponse *response)
{
   if( !REMOTEOFFLOAD_IS_RESPONSE(response) ) return NULL;

   //technically, there shouldn't be a design when two concurrent
   // threads are making this call at the same time.. so comment
   // out the following mutex lock/unlock
   //g_mutex_lock(&response->priv.responsemutex);
   GArray *mem_array = response->priv.response_mem_array;
   response->priv.response_mem_array = NULL;
   //g_mutex_unlock(&response->priv.responsemutex);

   return mem_array;
}

gboolean remote_offload_copy_response(RemoteOffloadResponse *response,
                                      void *dest,
                                      gsize size,
                                      gsize offset)
{
    if( !REMOTEOFFLOAD_IS_RESPONSE(response) ) return FALSE;

    if( !dest || (size == 0))
    {
       GST_ERROR_OBJECT(response,
                        "Invalid dest(%p) or size(%"G_GSIZE_FORMAT"))",
                        dest, size);
       return FALSE;
    }

    if( !response->priv.response_mem_array )
    {
       GST_ERROR_OBJECT(response,
                        "response_mem_array is NULL "
                        "(did you forget to call remote_offload_response_wait()?)");
       return FALSE;
    }

    if( !response->priv.response_mem_array->len )
    {
      GST_ERROR_OBJECT(response, "response_mem_array holds 0 GstMemory blocks");
      return FALSE;
    }

    GstMemory **gstmemarray = (GstMemory **)response->priv.response_mem_array->data;

    //short-circuit case -- the desired chunk of memory to copy resides completely within the first
    // GstMemory.
    // Special case as this is probably what will happen 99% of the time if someone is using
    // this function.
    gsize firstblocksize = gst_memory_get_sizes(gstmemarray[0], NULL, NULL);
    if( G_LIKELY((offset + size) <= firstblocksize) )
    {
       GstMapInfo info;
       if( !gst_memory_map (gstmemarray[0], &info, GST_MAP_READ) )
       {
         GST_ERROR_OBJECT (response, "Error in gst_memory_map");
         return FALSE;
       }

#ifndef NO_SAFESTR
       memcpy_s(dest, size, info.data + offset, size);
#else
       memcpy(dest, info.data + offset, size);
#endif

       gst_memory_unmap(gstmemarray[0], &info);

       return TRUE;
    }
    else
    {
       GST_ERROR_OBJECT(response, "Not implemented yet!");
       return FALSE;
    }
}

gboolean remote_offload_comms_channel_write(RemoteOffloadCommsChannel *channel,
                                            DataTransferHeader *header,
                                            GList *mem_list,
                                            RemoteOffloadResponse *response)
{
   if( !channel || !header )
      return FALSE;

   header->id = channel->priv.id;
   header->response_id = (guint64)(gulong)response;


   g_mutex_lock(&(channel->priv.responsePoolMutex));
   gboolean bcancelled = channel->priv.bcancelledstate;
   g_mutex_unlock(&(channel->priv.responsePoolMutex));

   RemoteOffloadCommsIOResult res = REMOTEOFFLOADCOMMSIO_CONNECTION_CLOSED;
   if( G_LIKELY(!bcancelled) )
   {
      if( response )
      {
         g_mutex_lock(&(channel->priv.responsePoolMutex));
         if( G_UNLIKELY(!g_hash_table_insert(channel->priv.activeWaitingEntryMap,
                             response, g_object_ref(response))) )
         {
            GST_WARNING_OBJECT(response, "response %p already present in active waiting list",
                               response);
         }
         g_mutex_unlock(&(channel->priv.responsePoolMutex));
      }

      res = remote_offload_comms_write(channel->priv.pcomms,
                                       header,
                                       mem_list);
      if( G_UNLIKELY(res!=REMOTEOFFLOADCOMMSIO_SUCCESS) )
      {
         GST_ERROR_OBJECT(channel, "remote_offload_comms_write failed. return=%d", res);
         g_mutex_lock(&(channel->priv.responsePoolMutex));
         if( response )
         {
            g_hash_table_remove(channel->priv.activeWaitingEntryMap,
                                response);
         }
         g_mutex_unlock(&(channel->priv.responsePoolMutex));
      }
   }

   return (res==REMOTEOFFLOADCOMMSIO_SUCCESS);
}

gboolean remote_offload_comms_channel_write_response(RemoteOffloadCommsChannel *channel,
                                                     GList *mem_list_response,
                                                     guint64 response_id)
{
   if( !channel )
   {
      GST_ERROR("channel is NULL");
      return FALSE;
   }

   g_mutex_lock(&(channel->priv.responsePoolMutex));
   gboolean bcancelled = channel->priv.bcancelledstate;
   g_mutex_unlock(&(channel->priv.responsePoolMutex));

   RemoteOffloadCommsIOResult res = REMOTEOFFLOADCOMMSIO_CONNECTION_CLOSED;
   if( G_LIKELY(!bcancelled) )
   {
      DataTransferHeader header;
      header.id = channel->priv.id;
      header.dataTransferType = DE_TYPE_RESPONSE;
      header.response_id = response_id;

      res = remote_offload_comms_write(channel->priv.pcomms,
                                       &header,
                                       mem_list_response);
      if( G_UNLIKELY(res != REMOTEOFFLOADCOMMSIO_SUCCESS) )
      {
         GST_ERROR_OBJECT(channel, "remote_offload_comms_write failed. return=%d", res);
      }
   }

   return (res==REMOTEOFFLOADCOMMSIO_SUCCESS);
}

GList *remote_offload_comms_channel_get_consumable_memfeatures(RemoteOffloadCommsChannel *channel)
{
   if( !REMOTEOFFLOAD_IS_COMMSCHANNEL(channel) )
     return NULL;

   if( channel->priv.pcomms )
      return remote_offload_comms_get_consumable_memfeatures(channel->priv.pcomms);

   return NULL;
}

GList *remote_offload_comms_channel_get_producible_memfeatures(RemoteOffloadCommsChannel *channel)
{
   if( !REMOTEOFFLOAD_IS_COMMSCHANNEL(channel) )
     return NULL;

   if( channel->priv.pcomms )
      return remote_offload_comms_get_producible_memfeatures(channel->priv.pcomms);

   return NULL;
}

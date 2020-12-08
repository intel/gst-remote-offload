/*
 *  remoteoffloadcomms.c - RemoteOffloadComms object
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
#include "remoteoffloadcomms.h"
#include "remoteoffloadprivateinterfaces.h"
#include "remoteoffloadcommschannel.h"

enum
{
  PROP_COMMSIO = 1,
  N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

#define DEFAULT_DATA_SEGMENT_HEADER_BUFFER_CAPACITY 128

/* Private structure definition. */
typedef struct {
   RemoteOffloadCommsIO *pcommsio;
   GThread *reader_thread;
   gboolean is_state_okay;
   GMutex writemutex; //will ensure that writes are serialized
   GMutex statemutex;
   guint16 datasegmentheaderbuffercapacity;
   DataSegmentHeader *pDataSegmentHeaderWriteBuffer;

   GMutex hashprotectmutex;
   GHashTable *hash_id_to_comms_channel;

   gboolean breject_writes;
}RemoteOffloadCommsPrivate;

struct _RemoteOffloadComms
{
  GObject parent_instance;

  /* Other members, including private data. */
  RemoteOffloadCommsPrivate priv;
};

GST_DEBUG_CATEGORY_STATIC (remote_offload_comms_debug);
#define GST_CAT_DEFAULT remote_offload_comms_debug

G_DEFINE_TYPE_WITH_CODE(RemoteOffloadComms, remote_offload_comms, G_TYPE_OBJECT,
GST_DEBUG_CATEGORY_INIT (remote_offload_comms_debug, "remoteoffloadcomms", 0,
  "debug category for RemoteOffloadComms"))


static gpointer RemoteOffloadCommsReader(gpointer data);

void remote_offload_comms_error_state(RemoteOffloadComms *pComms)
{
   if( !REMOTEOFFLOAD_IS_COMMS(pComms) )
      return;

   g_mutex_lock(&pComms->priv.statemutex);
   if( pComms->priv.is_state_okay )
   {
      pComms->priv.is_state_okay = FALSE;

      //Trigger closure of our read thread.
      if( pComms->priv.pcommsio)
         remote_offload_comms_io_shutdown(pComms->priv.pcommsio);

      //And then prevent further writes taking place over this comms-channel
      g_mutex_lock(&pComms->priv.writemutex);
      pComms->priv.breject_writes = TRUE;
      g_mutex_unlock(&pComms->priv.writemutex);
   }
   g_mutex_unlock(&pComms->priv.statemutex);
}

static void comms_failure_for_each_channel(gpointer key,
                                          gpointer value,
                                          gpointer user_data)
{
   if( REMOTEOFFLOAD_IS_COMMSCALLBACK(value) )
   {
     RemoteOffloadCommsCallback *pCallback =
            (RemoteOffloadCommsCallback *)value;

     remote_offload_comms_callback_comms_failure(pCallback);
   }
}

//This is called when we've detected a read/write error on
// within this comms channel.
static void declare_comms_error(RemoteOffloadComms *pComms)
{
   //put this comms object into a error state.
   remote_offload_comms_error_state(pComms);

   // Inform all registered comms channels that a comms failure
   //  has occurred.
   g_hash_table_foreach (pComms->priv.hash_id_to_comms_channel,
                         comms_failure_for_each_channel,
                         pComms);
}


static gpointer RemoteOffloadCommsReader(gpointer data)
{
   RemoteOffloadComms *pComms = REMOTEOFFLOAD_COMMS(data);
   RemoteOffloadCommsIO *pcommsio = pComms->priv.pcommsio;

   GST_DEBUG_OBJECT (pComms, "Comms thread start");

   guint16 current_dsheaderbuf_capacity =  DEFAULT_DATA_SEGMENT_HEADER_BUFFER_CAPACITY;
   DataSegmentHeader *pDataSegmentHeaderBuffer =
         (DataSegmentHeader *)g_malloc(current_dsheaderbuf_capacity*sizeof(DataSegmentHeader));
   if( !pDataSegmentHeaderBuffer )
   {
      GST_ERROR_OBJECT (pComms, "Error allocating DataSegmentHeader buffer");
      return NULL;
   }

   while(1)
   {
     RemoteOffloadCommsIOResult res;

     //step 1: receive the data transfer header
     DataTransferHeader receiveheader;

     res = remote_offload_comms_io_read(pcommsio, (guint8 *)&receiveheader,
                                        sizeof(receiveheader));
     if( res != REMOTEOFFLOADCOMMSIO_SUCCESS )
     {
        if( res == REMOTEOFFLOADCOMMSIO_FAIL )
        {
          GST_ERROR_OBJECT (pComms, "Error in remote_offload_comms_read for receiveheader");
          declare_comms_error(pComms);
        }
        break;
     }

     if( receiveheader.id == -1 )
     {
        GST_DEBUG_OBJECT (pComms, "Received instruction to end read loop");
        break;
     }

     //given the header channel-id, retrieve the callback object
     RemoteOffloadCommsCallback *pCallback =
           g_hash_table_lookup (pComms->priv.hash_id_to_comms_channel,
                                GINT_TO_POINTER(receiveheader.id));

     if( !pCallback )
     {
        GST_ERROR_OBJECT (pComms,
                          "Error retrieving callback object for channel-id=%d", receiveheader.id);
        declare_comms_error(pComms);
        break;
     }

     //step 2: receive the data segment headers
     if( receiveheader.nsegments > 0 )
     {
        if( receiveheader.nsegments > current_dsheaderbuf_capacity )
        {

          GST_INFO_OBJECT (pComms, "Resizing DataSegmentHeader buffer capacity from %u to %u",
                           current_dsheaderbuf_capacity, receiveheader.nsegments);
          current_dsheaderbuf_capacity = receiveheader.nsegments;

          pDataSegmentHeaderBuffer =
                g_realloc(pDataSegmentHeaderBuffer,
                current_dsheaderbuf_capacity*sizeof(DataSegmentHeader));
          if( !pDataSegmentHeaderBuffer )
          {
            GST_ERROR_OBJECT (pComms, "Error resizing DataSegmentHeader buffer to a capacity of %u",
                              receiveheader.nsegments);
            declare_comms_error(pComms);
            break;
          }
        }

        res = remote_offload_comms_io_read(pcommsio, (guint8 *)pDataSegmentHeaderBuffer,
                                           receiveheader.nsegments*sizeof(DataSegmentHeader));
        if( res != REMOTEOFFLOADCOMMSIO_SUCCESS )
        {
           if( res == REMOTEOFFLOADCOMMSIO_FAIL )
           {
             GST_ERROR_OBJECT (pComms,
                               "Error in remote_offload_comms_read for data segment headers "
                               "(%"G_GSIZE_FORMAT" bytes)",
                               receiveheader.nsegments*sizeof(DataTransferHeader));
             declare_comms_error(pComms);
           }
           break;
        }
     }

     GArray *segmemarray = g_array_new(FALSE, FALSE, sizeof(GstMemory *));

     //step 3: Receive each data segment
     for( guint16 segi = 0; segi < receiveheader.nsegments; segi++ )
     {
        //We need to obtain a GstMemory to receive this segment into.
        // Based on the receiveheader.id, retrieve the comms channel object
        // to request from
        GstMemory *mem =
              gst_allocator_alloc (NULL, pDataSegmentHeaderBuffer[segi].segmentSize, NULL);
       //TOD0: We should really be using the below call that up-requests the memblock allocation
       // from the comms channel
#if 0
        GstMemory *mem =
              remote_offload_comms_callback_allocate_data_segment(pCallback,
                                                                  receiveheader.dataTransferType,
                                                                  segi,
                                                                  dsHeaders[segi].segmentSize,
                                                                  segmemarray);
#endif
        g_array_append_val (segmemarray, mem);

        GstMapInfo map;
        gst_memory_map (mem, &map, GST_MAP_WRITE);

        //receive this data segment
        res = remote_offload_comms_io_read(pcommsio,
                                           (guint8 *)map.data,
                                           pDataSegmentHeaderBuffer[segi].segmentSize);
        if( res != REMOTEOFFLOADCOMMSIO_SUCCESS )
        {
           if( res == REMOTEOFFLOADCOMMSIO_FAIL )
           {
             GST_ERROR_OBJECT (pComms,
                               "Error in remote_offload_comms_read for data segment headers "
                               "(%"G_GSIZE_FORMAT" bytes)",
                               receiveheader.nsegments*sizeof(DataTransferHeader));
             declare_comms_error(pComms);
           }
           break;
        }

        gst_memory_unmap (mem, &map);
     }

     if( res != REMOTEOFFLOADCOMMSIO_SUCCESS )
        break;

     //inform the channel of the received message
     //i.e. comms_channel_message_received(pchannel, &receiveheader, segmemlist);
     remote_offload_comms_callback_data_transfer_received(pCallback, &receiveheader, segmemarray);

     g_array_unref(segmemarray);
   }

   g_free(pDataSegmentHeaderBuffer);

   GST_DEBUG_OBJECT (pComms, "Reader thread end");

   return NULL;
}

static GstMemory* virt_to_mem(void *pVirt,
                              gsize size)
{
   GstMemory *mem = NULL;

   if( pVirt && (size>0) )
   {

      mem = gst_memory_new_wrapped((GstMemoryFlags)0,
                                   pVirt,
                                   size,
                                   0,
                                   size,
                                   NULL,
                                   NULL);
   }

   return mem;
}

//this should be called with the write_mutex locked.
static inline RemoteOffloadCommsIOResult remote_offload_comms_write_routine(RemoteOffloadComms *comms,
                                                                            DataTransferHeader *pheader,
                                                                            GList *memList)
{
   pheader->nsegments = 0;

   //perform sanity checks on the memList, and count the number of data segments
   GList *li;
   for(li = memList; li != NULL; li = li->next )
   {
      GstMemory *mem = (GstMemory *)li->data;
      if( !mem )
      {
         GST_ERROR_OBJECT (comms, "mem is NULL");
         return REMOTEOFFLOADCOMMSIO_FAIL;
      }

      //unlikely, but check for overflow condition
      if( G_UNLIKELY(pheader->nsegments == G_MAXUINT16) )
      {
         GST_ERROR_OBJECT (comms, "Maximum number of data segments exceeded (%u)",G_MAXUINT16);
         return REMOTEOFFLOADCOMMSIO_FAIL;
      }

      pheader->nsegments++;
   }

   //check if we need to reallocate the data segment write buffer
   if( pheader->nsegments > comms->priv.datasegmentheaderbuffercapacity )
   {
      GST_INFO_OBJECT (comms, "Resizing DataSegmentHeader write buffer capacity from %u to %u",
                       comms->priv.datasegmentheaderbuffercapacity, pheader->nsegments);

      comms->priv.pDataSegmentHeaderWriteBuffer =
            g_realloc(comms->priv.pDataSegmentHeaderWriteBuffer,
            pheader->nsegments*sizeof(DataSegmentHeader));
      if( !comms->priv.pDataSegmentHeaderWriteBuffer )
      {
         GST_ERROR_OBJECT (comms,
                           "Error resizing DataSegmentHeader write buffer to a capacity of %u",
                           pheader->nsegments);
         comms->priv.datasegmentheaderbuffercapacity = 0;
         return REMOTEOFFLOADCOMMSIO_FAIL;
      }

      comms->priv.datasegmentheaderbuffercapacity = pheader->nsegments;
   }

   //write the DataSegmentHeader values
   guint memindex = 0;
   for(li = memList; li != NULL; li = li->next )
   {
      GstMemory *mem = (GstMemory *)li->data;

      comms->priv.pDataSegmentHeaderWriteBuffer[memindex].segmentSize =
                                                         gst_memory_get_sizes(mem, NULL, NULL);

      memindex++;
   }

   GList *write_mem_list = NULL;
   GstMemory *headermem = virt_to_mem(pheader, sizeof(DataTransferHeader));
   write_mem_list = g_list_append (write_mem_list,
                                   headermem);

   GstMemory *datasegheadermem = NULL;
   if( pheader->nsegments > 0 )
   {
      datasegheadermem = virt_to_mem(comms->priv.pDataSegmentHeaderWriteBuffer,
                           pheader->nsegments * sizeof(DataSegmentHeader));
      write_mem_list = g_list_append (write_mem_list,
                                      datasegheadermem);
   }

   for(li = memList; li != NULL; li = li->next )
   {
      GstMemory *mem = (GstMemory *)li->data;
      write_mem_list = g_list_append (write_mem_list,
                                      mem);
   }

   //call the subclass to actually perform the write here
   RemoteOffloadCommsIOResult ret = remote_offload_comms_io_write_mem_list(comms->priv.pcommsio, write_mem_list);

   gst_memory_unref(headermem);
   if( datasegheadermem )
      gst_memory_unref(datasegheadermem);

   g_list_free(write_mem_list);

   return ret;
}

RemoteOffloadCommsIOResult remote_offload_comms_write(RemoteOffloadComms *comms,
                                                      DataTransferHeader *pheader,
                                                      GList *memList)
{
   if( !pheader || !comms )
   {
      return REMOTEOFFLOADCOMMSIO_FAIL;
   }

   RemoteOffloadCommsIOResult ret = REMOTEOFFLOADCOMMSIO_CONNECTION_CLOSED;
   gboolean bdeclare_comms_failure = FALSE;

   //only allow 1 thread to write at a time
   g_mutex_lock(&(comms->priv.writemutex));
   if( !comms->priv.breject_writes )
   {
      ret = remote_offload_comms_write_routine(comms,
                                               pheader,
                                               memList);

      if( ret != REMOTEOFFLOADCOMMSIO_SUCCESS )
      {
         comms->priv.breject_writes = TRUE;
         bdeclare_comms_failure = TRUE;
      }
   }
   g_mutex_unlock(&(comms->priv.writemutex));
   if( bdeclare_comms_failure )
   {
      declare_comms_error(comms);
   }

   return ret;
}

//This is called right after _init() and set_properties calls for default
// props passed into g_object_new()
static void remote_offload_comms_constructed(GObject *object)
{
  RemoteOffloadComms *pComms = REMOTEOFFLOAD_COMMS(object);

  pComms->priv.datasegmentheaderbuffercapacity = DEFAULT_DATA_SEGMENT_HEADER_BUFFER_CAPACITY;
  pComms->priv.pDataSegmentHeaderWriteBuffer = (DataSegmentHeader *)
        g_malloc(pComms->priv.datasegmentheaderbuffercapacity*sizeof(DataSegmentHeader));

  if( !pComms->priv.pDataSegmentHeaderWriteBuffer )
  {
     GST_ERROR_OBJECT (pComms, "Error allocating DataSegmentHeader write buffer");
  }

  if( pComms->priv.pcommsio && pComms->priv.pDataSegmentHeaderWriteBuffer)
  {
     pComms->priv.is_state_okay = TRUE;
  }

  G_OBJECT_CLASS (remote_offload_comms_parent_class)->constructed (object);
}

static void
remote_offload_comms_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  RemoteOffloadComms *pComms = REMOTEOFFLOAD_COMMS(object);
  switch (property_id)
  {
    case PROP_COMMSIO:
    {
      RemoteOffloadCommsIO *tmpIO = g_value_get_pointer (value);
      if( REMOTEOFFLOAD_IS_COMMSIO(tmpIO) )
      {
         pComms->priv.pcommsio = g_object_ref(tmpIO);
      }
    }
    break;

    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

void remote_offload_comms_finish(RemoteOffloadComms *pComms)
{
   if( !REMOTEOFFLOAD_IS_COMMS(pComms) ) return;
   g_mutex_lock(&(pComms->priv.writemutex));
   if( !pComms->priv.breject_writes )
   {
       //Send special header that triggers the remote comms
       // reader thread to close.
       DataTransferHeader header;
       header.id = -1;
       header.dataTransferType = 0;
       header.nsegments = 0;
       header.response_id = 0;
       GST_DEBUG_OBJECT(pComms, "Sending special close header");
       remote_offload_comms_io_write(pComms->priv.pcommsio,
                                     (guint8 *)&header,
                                     sizeof(header));

       pComms->priv.breject_writes = TRUE;
    }
   g_mutex_unlock(&(pComms->priv.writemutex));
}

static void
remote_offload_comms_finalize (GObject *gobject)
{
  RemoteOffloadComms *pComms = REMOTEOFFLOAD_COMMS(gobject);

  //wait for the reader_thread to close. In a normal
  // scenario, the reader thread would have received a special
  // DataTransferHeader which instructs it to close.
  GST_DEBUG_OBJECT(pComms, "Waiting for reader thread to close...");
  if( pComms->priv.reader_thread )
  {
     g_thread_join (pComms->priv.reader_thread);
  }
  GST_DEBUG_OBJECT(pComms, "reader thread closed");

  //call shutdown for commsio.
  if( pComms->priv.pcommsio)
    remote_offload_comms_io_shutdown(pComms->priv.pcommsio);

  g_mutex_clear(&(pComms->priv.writemutex));
  g_mutex_clear(&(pComms->priv.hashprotectmutex));
  g_mutex_clear(&(pComms->priv.statemutex));

  if( pComms->priv.pcommsio )
     g_object_unref(pComms->priv.pcommsio);

  g_hash_table_destroy(pComms->priv.hash_id_to_comms_channel);

  g_free(pComms->priv.pDataSegmentHeaderWriteBuffer);

  G_OBJECT_CLASS (remote_offload_comms_parent_class)->finalize (gobject);
}


static void
remote_offload_comms_class_init (RemoteOffloadCommsClass *klass)
{
   GObjectClass *object_class = G_OBJECT_CLASS (klass);

   object_class->set_property = remote_offload_comms_set_property;

   obj_properties[PROP_COMMSIO] =
    g_param_spec_pointer ("commsio",
                         "CommsIO",
                         "Comms I/O object in use",
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);



   g_object_class_install_properties (object_class,
                                     N_PROPERTIES,
                                     obj_properties);


   object_class->constructed = remote_offload_comms_constructed;
   object_class->finalize = remote_offload_comms_finalize;
}



static void
remote_offload_comms_init (RemoteOffloadComms *self)
{
  self->priv.pcommsio = NULL;
  self->priv.pDataSegmentHeaderWriteBuffer = NULL;
  self->priv.datasegmentheaderbuffercapacity = 0;
  self->priv.reader_thread = NULL;
  self->priv.is_state_okay = FALSE;
  self->priv.breject_writes = FALSE;
  g_mutex_init(&(self->priv.writemutex));
  g_mutex_init(&(self->priv.hashprotectmutex));
  g_mutex_init(&(self->priv.statemutex));
  self->priv.hash_id_to_comms_channel = g_hash_table_new(g_direct_hash, g_direct_equal);
}

RemoteOffloadComms *remote_offload_comms_new(RemoteOffloadCommsIO *pcommsio)
{
  RemoteOffloadComms *pComms =
        g_object_new(REMOTEOFFLOADCOMMS_TYPE, "commsio", pcommsio, NULL);

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

GList *remote_offload_comms_get_consumable_memfeatures(RemoteOffloadComms *comms)
{
   if( !REMOTEOFFLOAD_IS_COMMS(comms) )
      return NULL;

   if( comms->priv.pcommsio )
   {
      return remote_offload_comms_io_get_consumable_memfeatures(comms->priv.pcommsio);
   }

   return NULL;
}

GList *remote_offload_comms_get_producible_memfeatures(RemoteOffloadComms *comms)
{
   if( !REMOTEOFFLOAD_IS_COMMS(comms) )
      return NULL;

   if( comms->priv.pcommsio )
   {
      return remote_offload_comms_io_get_producible_memfeatures(comms->priv.pcommsio);
   }

   return NULL;
}

gboolean remote_offload_comms_register_channel(RemoteOffloadComms *comms,
                                               RemoteOffloadCommsChannel *channel)
{
   if( !REMOTEOFFLOAD_IS_COMMS(comms) ||
       !REMOTEOFFLOAD_IS_COMMSCHANNEL(channel) ||
       !REMOTEOFFLOAD_IS_COMMSCALLBACK(channel))
      return FALSE;

   gint id;
   g_object_get(channel, "id", &id, NULL);

   if( id < 0 ) return FALSE;

   g_mutex_lock(&(comms->priv.hashprotectmutex));
   if( !g_hash_table_insert(comms->priv.hash_id_to_comms_channel,
                            GINT_TO_POINTER(id), REMOTEOFFLOAD_COMMSCALLBACK(channel)) )
   {
      GST_WARNING_OBJECT (comms,
                          "Warning! Key (id=%d) already existed in channelId-to-channel map.", id);
   }

   GST_DEBUG_OBJECT (comms, "registered channel for id=%d\n", id);

   if( !comms->priv.reader_thread )
   {
      comms->priv.reader_thread =
        g_thread_new ("CommsReader", (GThreadFunc) RemoteOffloadCommsReader, comms);
   }
   g_mutex_unlock(&(comms->priv.hashprotectmutex));

   return TRUE;
}



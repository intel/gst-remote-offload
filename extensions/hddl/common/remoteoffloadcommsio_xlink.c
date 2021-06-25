/*
 *  remoteoffloadcommsio_xlink.c - RemoteOffloadCommsXLinkIO object
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

#include "remoteoffloadcommsio_xlink.h"
#include "remoteoffloadcommsio.h"
#include <malloc.h>
#include <unistd.h>
#include <gst/allocators/allocators.h>
#ifndef NO_SAFESTR
  #include <safe_mem_lib.h>
#else
  #include <string.h>
#endif

enum
{
  PROP_DEVHANDLER = 1,
  PROP_CHANNELID,
  PROP_TXMODE,
  PROP_COALESCEMODE,
  N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

//Time to wait between receiving X_LINK_CHAN_FULL and the next attempt (3 ms)
#define WAIT_AFTER_CHAN_FULL_TIME_MICROSECS 3000

//Allowed total wait time before declaring a timeout error. (10 secs)
#define TIMEOUT_PERIOD_CHAN_FULL_MICROSECS 10000000

#define XLINK_COLLECT_HISTOGRAM 0

#if XLINK_COLLECT_HISTOGRAM
#define NUM_HISTOGRAM_BINS 16
static const guint32 hist_thresholds[NUM_HISTOGRAM_BINS-1] =
{
      32, 128, 256, 512, 1024, 2*1024, 4*1024, 8*1024,
      16*1024, 32*1024, 64*1024, 128*1024, 256*1024, 512*1024, 1024*1024
};
#endif

/* Private structure definition. */
typedef struct {
  gboolean is_state_okay;
  struct xlink_handle *deviceIdHandler;
  xlink_channel_id_t channelId;

  GMutex shutdownmutex;
  gboolean shutdownAsserted;

  remote_offload_comms_io_xlink_channel_closed_f callback;
  void *callback_user_data;

  guint8 *pCoalescedReceivedMemory;
  guint8 *pCoalescedSendMemory;

  guint32 total_mems;
  guint32 current_mem_index;

  XLinkCommsIOTXMode tx_mode;
  XLinkCommsIOCoaleseMode coalese_mode;
#if XLINK_COLLECT_HISTOGRAM
  guint64 write_histogram[NUM_HISTOGRAM_BINS];
  guint64 total_bytes_written;
#endif

  /* stuff */
} RemoteOffloadCommsXLinkIOPrivate;

struct _RemoteOffloadCommsXLinkIO
{
  GObject parent_instance;

  /* Other members, including private data. */
  RemoteOffloadCommsXLinkIOPrivate priv;
};

static void remote_offload_comms_io_xlink_interface_init (RemoteOffloadCommsIOInterface *iface);

GST_DEBUG_CATEGORY_STATIC (comms_io_xlink_debug);
#define GST_CAT_DEFAULT comms_io_xlink_debug

G_DEFINE_TYPE_WITH_CODE (RemoteOffloadCommsXLinkIO, remote_offload_comms_io_xlink, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (REMOTEOFFLOADCOMMSIO_TYPE,
                         remote_offload_comms_io_xlink_interface_init)
                         GST_DEBUG_CATEGORY_INIT (comms_io_xlink_debug,
                         "remoteoffloadcommsioxlink", 0,
                         "debug category for RemoteOffloadCommsXLinkIO"))

typedef struct _XLinkBufferDesc
{
  gint32 offset; //if <0, it means this buffer is not part of coalesced segment,
                 // otherwise, it's the offset to the start of this buffer within
                 // the coalesced segment.
  guint64 size;  //size of this buffer
#ifdef XLINK_PERFORM_CHECKSUMS
  guint64 checksum;
#endif
}XLinkBufferDesc;

#ifdef XLINK_PERFORM_CHECKSUMS
//very basic / simple checksum for sanity check on buffer contents.
static guint64 SimpleChecksum(guint8 *pBuffer, gsize size)
{
   guint64 checksum = 0;
   for( gsize i = 0; i < size; i++ )
      checksum = checksum + pBuffer[i];
   return checksum;
}
#endif

static RemoteOffloadCommsIOResult
remote_offload_comms_io_xlink_read_buf(RemoteOffloadCommsIO *commsio,
                                       guint8 *buf,
                                       guint64 size)
{
  RemoteOffloadCommsXLinkIO *pCommsIOXLink = REMOTEOFFLOAD_COMMSIOXLINK (commsio);

  if( G_UNLIKELY(!pCommsIOXLink->priv.deviceIdHandler) )
  {
     GST_ERROR_OBJECT (pCommsIOXLink, "Invalid device handler");
     return REMOTEOFFLOADCOMMSIO_FAIL;
  }

  if( G_UNLIKELY(size > G_MAXUINT32) )
  {
     GST_ERROR_OBJECT (pCommsIOXLink, "Maximum size exceeded");
     return REMOTEOFFLOADCOMMSIO_FAIL;
  }

  guint32 bytes_to_receive = (guint32)size;
  while(bytes_to_receive > 0)
  {
     g_mutex_lock(&pCommsIOXLink->priv.shutdownmutex);
     gboolean bShutdown = pCommsIOXLink->priv.shutdownAsserted;
     g_mutex_unlock(&pCommsIOXLink->priv.shutdownmutex);

     if( bShutdown )
     {
        return REMOTEOFFLOADCOMMSIO_CONNECTION_CLOSED;
     }

     guint currentXLinkBufferSize = 0;
     enum xlink_error status = xlink_read_data(pCommsIOXLink->priv.deviceIdHandler,
                              pCommsIOXLink->priv.channelId,
                              &buf,
                              &currentXLinkBufferSize);

     switch(status)
     {
        case X_LINK_SUCCESS:
        break;

        case X_LINK_COMMUNICATION_NOT_OPEN:
           GST_ERROR_OBJECT (pCommsIOXLink, "xlink_read_data returned X_LINK_COMMUNICATION_NOT_OPEN");
           return REMOTEOFFLOADCOMMSIO_CONNECTION_CLOSED;
        break;

        case X_LINK_TIMEOUT:
           continue;
        break;

        default:
           GST_ERROR_OBJECT (pCommsIOXLink, "xlink_read_data returned %d", status);
           return REMOTEOFFLOADCOMMSIO_FAIL;
        break;
    }

    buf += currentXLinkBufferSize;

    if( currentXLinkBufferSize > bytes_to_receive )
    {
       GST_ERROR_OBJECT (pCommsIOXLink,
                         "Current accumulated read size is greater than what was requested/expected");
       return REMOTEOFFLOADCOMMSIO_FAIL;
    }

    bytes_to_receive -= currentXLinkBufferSize;

    status = xlink_release_data(pCommsIOXLink->priv.deviceIdHandler,
                                pCommsIOXLink->priv.channelId,
                                NULL);

    switch(status)
    {
        case X_LINK_SUCCESS:
        break;

        case X_LINK_COMMUNICATION_NOT_OPEN:
           GST_ERROR_OBJECT (pCommsIOXLink, "xlink_release_data returned X_LINK_COMMUNICATION_NOT_OPEN");
           return REMOTEOFFLOADCOMMSIO_CONNECTION_CLOSED;
        break;

        default:
           GST_ERROR_OBJECT (pCommsIOXLink, "xlink_release_data returned %d", status);
           return REMOTEOFFLOADCOMMSIO_FAIL;
        break;
    }
  }

  return REMOTEOFFLOADCOMMSIO_SUCCESS;
}

static RemoteOffloadCommsIOResult
remote_offload_comms_io_xlink_read(RemoteOffloadCommsIO *commsio,
                                   guint8 *buf,
                                   guint64 size)
{
   RemoteOffloadCommsXLinkIO *pCommsIOXLink = REMOTEOFFLOAD_COMMSIOXLINK (commsio);

   if( G_UNLIKELY(!pCommsIOXLink->priv.deviceIdHandler) )
   {
      GST_ERROR_OBJECT (pCommsIOXLink, "Invalid device handler");
      return REMOTEOFFLOADCOMMSIO_FAIL;
   }

   if( G_UNLIKELY(size > G_MAXUINT32) )
   {
      GST_ERROR_OBJECT (pCommsIOXLink, "Maximum size exceeded");
      return REMOTEOFFLOADCOMMSIO_FAIL;
   }

   if( pCommsIOXLink->priv.coalese_mode==XLINK_COMMSIO_COALESCEMODE_DISABLE )
   {
     return remote_offload_comms_io_xlink_read_buf(commsio, buf, size);
   }
   else
   {
      //grab the coalesced chunk first.
      if( pCommsIOXLink->priv.total_mems == 0 )
      {
         enum xlink_error status = X_LINK_TIMEOUT;
         while( status == X_LINK_TIMEOUT )
         {
            g_mutex_lock(&pCommsIOXLink->priv.shutdownmutex);
            gboolean bShutdown = pCommsIOXLink->priv.shutdownAsserted;
            g_mutex_unlock(&pCommsIOXLink->priv.shutdownmutex);

            if( bShutdown )
            {
               return REMOTEOFFLOADCOMMSIO_CONNECTION_CLOSED;
            }

            guint32 received_size;
            status = xlink_read_data(pCommsIOXLink->priv.deviceIdHandler,
                                                   pCommsIOXLink->priv.channelId,
                                                   &pCommsIOXLink->priv.pCoalescedReceivedMemory,
                                                   &received_size);

            if( G_UNLIKELY((status == X_LINK_SUCCESS) && (received_size > COALESCED_CHUNK_SIZE)) )
            {
               GST_ERROR_OBJECT (pCommsIOXLink, "Received size > COALESCED_CHUNK_SIZE. That shouldn't happen.");
               status = X_LINK_ERROR;
            }
         }

         switch(status)
         {
           case X_LINK_SUCCESS:
           break;

           case X_LINK_COMMUNICATION_NOT_OPEN:
              GST_ERROR_OBJECT (pCommsIOXLink, "xlink_read_data returned X_LINK_COMMUNICATION_NOT_OPEN");
              return REMOTEOFFLOADCOMMSIO_CONNECTION_CLOSED;
           break;

           default:
              GST_ERROR_OBJECT (pCommsIOXLink, "xlink_read_data returned %d", status);
              return REMOTEOFFLOADCOMMSIO_FAIL;
           break;
          }

          status = xlink_release_data(pCommsIOXLink->priv.deviceIdHandler,
                                      pCommsIOXLink->priv.channelId,
                                      NULL);
          switch(status)
          {
           case X_LINK_SUCCESS:
           break;

           case X_LINK_COMMUNICATION_NOT_OPEN:
              GST_ERROR_OBJECT (pCommsIOXLink, "xlink_release_data returned X_LINK_COMMUNICATION_NOT_OPEN");
              return REMOTEOFFLOADCOMMSIO_CONNECTION_CLOSED;
           break;

           default:
              GST_ERROR_OBJECT (pCommsIOXLink, "xlink_release_data returned %d", status);
              return REMOTEOFFLOADCOMMSIO_FAIL;
           break;
          }

          pCommsIOXLink->priv.total_mems = *((guint32 *)pCommsIOXLink->priv.pCoalescedReceivedMemory);
          pCommsIOXLink->priv.current_mem_index = 0;
      }

      if( G_UNLIKELY(pCommsIOXLink->priv.total_mems == 0) )
      {
         GST_ERROR_OBJECT (pCommsIOXLink, "Total # of coalesced chunks is 0");
         return REMOTEOFFLOADCOMMSIO_FAIL;
      }

      if( G_UNLIKELY(pCommsIOXLink->priv.current_mem_index >= pCommsIOXLink->priv.total_mems) )
      {
         GST_ERROR_OBJECT (pCommsIOXLink, "Current tracked coalesced mem index exceeds total # of coalesced memory chunks");
         return REMOTEOFFLOADCOMMSIO_FAIL;
      }

      XLinkBufferDesc *pDesc = (XLinkBufferDesc *)(pCommsIOXLink->priv.pCoalescedReceivedMemory +
                                                   sizeof(guint32) +
                                                   pCommsIOXLink->priv.current_mem_index * sizeof(XLinkBufferDesc));


      if( G_UNLIKELY(pDesc->size != size) )
      {
         GST_ERROR_OBJECT (pCommsIOXLink, "Next memory chunk descriptor's size(%lu) doesn't match read size(%lu). "
               "                          This implies out of sync protocol, or possible corruption?", pDesc->size, size);
         return REMOTEOFFLOADCOMMSIO_FAIL;
      }

      //If offset is < 0, it means that this segment in not included in the
      // coalesced buffer that we received earlier.
      if( pDesc->offset < 0 )
      {
         RemoteOffloadCommsIOResult ret =
               remote_offload_comms_io_xlink_read_buf(commsio, buf, size);

         if( G_UNLIKELY(ret != REMOTEOFFLOADCOMMSIO_SUCCESS) )
            return ret;
      }
      else
      {
         //This segment was included in the coalesced buffer that we received before, so
         // copy it out of there.
#ifndef NO_SAFESTR
         memcpy_s(buf,
                  size,
                  pCommsIOXLink->priv.pCoalescedReceivedMemory + pDesc->offset,
                  size);
#else
         memcpy(buf, pCommsIOXLink->priv.pCoalescedReceivedMemory + pDesc->offset, size);
#endif
      }

#ifdef XLINK_PERFORM_CHECKSUMS
      guint64 checksum = SimpleChecksum(buf, size);
      if( checksum != pDesc->checksum )
      {
         GST_ERROR_OBJECT (pCommsIOXLink,
                           "Calculated checksum of received buffer(0x%lx) doesn't match what was sent(0x%lx)!",
                           checksum, pDesc->checksum);
      }
#endif

      pCommsIOXLink->priv.current_mem_index++;

      //once we've read all buffers from a coalesced "message", reset.
      if( pCommsIOXLink->priv.current_mem_index >= pCommsIOXLink->priv.total_mems )
      {
         pCommsIOXLink->priv.total_mems = 0;
         pCommsIOXLink->priv.current_mem_index = 0;
      }
   }

   return REMOTEOFFLOADCOMMSIO_SUCCESS;

}

#if XLINK_COLLECT_HISTOGRAM
static inline void add_to_hist(RemoteOffloadCommsXLinkIO *pCommsIOXLink,
                          guint32 bytes_to_write)
{
   pCommsIOXLink->priv.total_bytes_written += bytes_to_write;
   for( guint bini = 0; bini < (NUM_HISTOGRAM_BINS-1); bini++ )
   {
      if( bytes_to_write <= hist_thresholds[bini] )
      {
         pCommsIOXLink->priv.write_histogram[bini]++;
         return;
      }
   }

   pCommsIOXLink->priv.write_histogram[NUM_HISTOGRAM_BINS-1]++;
}
#endif

static RemoteOffloadCommsIOResult
remote_offload_comms_io_xlink_write(RemoteOffloadCommsXLinkIO *pCommsIOXLink,
                                    guint8 *buf,
                                    guint32 size)
{
   if( G_UNLIKELY(!pCommsIOXLink->priv.deviceIdHandler) )
   {
      GST_ERROR_OBJECT (pCommsIOXLink, "Invalid device handler");
      return REMOTEOFFLOADCOMMSIO_FAIL;
   }

   RemoteOffloadCommsIOResult ret = REMOTEOFFLOADCOMMSIO_SUCCESS;

#if XLINK_COLLECT_HISTOGRAM
   add_to_hist(pCommsIOXLink, size);
#endif

   guint32 size_left_to_write = size;
   guint64 total_chan_full_wait_time = 0;
   while( (size_left_to_write > 0) && (ret == REMOTEOFFLOADCOMMSIO_SUCCESS))
   {
      //limit the max size to write in a single call to xlink_write_data by XLINK_MAX_SEND_SIZE
      guint32 bytes_to_write = MIN(size_left_to_write, XLINK_MAX_SEND_SIZE);

      enum xlink_error status;
      if( bytes_to_write <= XLINK_MAX_CONTROL_SIZE )
      {
         status = xlink_write_control_data(pCommsIOXLink->priv.deviceIdHandler,
                                           pCommsIOXLink->priv.channelId,
                                           buf,
                                           bytes_to_write);
      }
      else
      {
         status = xlink_write_data(pCommsIOXLink->priv.deviceIdHandler,
                                   pCommsIOXLink->priv.channelId,
                                   buf,
                                   bytes_to_write);
      }

      switch(status)
      {
         case X_LINK_SUCCESS:
            ret = REMOTEOFFLOADCOMMSIO_SUCCESS;
            buf += bytes_to_write;
            size_left_to_write -= bytes_to_write;
            total_chan_full_wait_time = 0;
         break;

         case X_LINK_COMMUNICATION_NOT_OPEN:
            GST_ERROR_OBJECT (pCommsIOXLink, "xlink_write_data returned X_LINK_COMMUNICATION_NOT_OPEN");
            ret = REMOTEOFFLOADCOMMSIO_CONNECTION_CLOSED;
         break;

         case X_LINK_CHAN_FULL:
            //TODO: Refactor this logic once XLink is able to notify us when it's okay to start sending data
            // again.
            if( total_chan_full_wait_time >= TIMEOUT_PERIOD_CHAN_FULL_MICROSECS )
            {
               GST_ERROR_OBJECT (pCommsIOXLink,
                                "xlink_write_data returned X_LINK_CHAN_FULL"
                                " too many times in a row. Declaring TIMEOUT.");
               ret = REMOTEOFFLOADCOMMSIO_FAIL;
            }
            else
            {
               //wait a few ms before retrying
               g_usleep(WAIT_AFTER_CHAN_FULL_TIME_MICROSECS);
               total_chan_full_wait_time += WAIT_AFTER_CHAN_FULL_TIME_MICROSECS;
            }
         break;

         default:
            GST_ERROR_OBJECT (pCommsIOXLink,
                               "xlink_write_data failed for size=%"G_GUINT32_FORMAT, size);
            ret = REMOTEOFFLOADCOMMSIO_FAIL;
         break;
      }

   }

   return ret;
}


static RemoteOffloadCommsIOResult
remote_offload_comms_io_xlink_write_mem_list(RemoteOffloadCommsIO *commsio,
                                             GList *mem_list)
{
   RemoteOffloadCommsXLinkIO *pCommsIOXLink = REMOTEOFFLOAD_COMMSIOXLINK (commsio);

   if( G_UNLIKELY(!pCommsIOXLink->priv.deviceIdHandler) )
   {
      GST_ERROR_OBJECT (pCommsIOXLink, "Invalid device handler");
      return REMOTEOFFLOADCOMMSIO_FAIL;
   }

   RemoteOffloadCommsIOResult ret = REMOTEOFFLOADCOMMSIO_SUCCESS;

   if( pCommsIOXLink->priv.coalese_mode==XLINK_COMMSIO_COALESCEMODE_DISABLE )
   {
      for(GList * li = mem_list; li != NULL; li = li->next )
      {
         GstMemory *mem = (GstMemory *)li->data;

         GstMapInfo mapInfo;
         if( G_LIKELY(gst_memory_map (mem, &mapInfo, GST_MAP_READ)) )
         {
            ret = remote_offload_comms_io_xlink_write(pCommsIOXLink,
                                             mapInfo.data,
                                             mapInfo.size);

            gst_memory_unmap(mem, &mapInfo);

            if( G_UNLIKELY(ret != REMOTEOFFLOADCOMMSIO_SUCCESS) )
               break;
         }
         else
         {
            GST_ERROR_OBJECT (pCommsIOXLink,
                            "gst_memory_map failed");
            return REMOTEOFFLOADCOMMSIO_FAIL;
         }
      }
   }
   else
   {
      //first pass through the list, just count the number of GstMemory segments.
      guint32 nmem = 0;
      for(GList * li = mem_list; li != NULL; li = li->next )
      {
        nmem++;
      }

      guint8 *pCoalesced = pCommsIOXLink->priv.pCoalescedSendMemory;
      guint32 *pTotalMems = (guint32 *)pCoalesced;
      *pTotalMems = nmem;
      pCoalesced += sizeof(guint32);
      XLinkBufferDesc *pDesc = (XLinkBufferDesc *)pCoalesced;
      pCoalesced += nmem * sizeof(XLinkBufferDesc);

      if( G_UNLIKELY((sizeof(guint32) + nmem*sizeof(XLinkBufferDesc)) > COALESCED_CHUNK_SIZE) )
      {
         GST_ERROR_OBJECT (pCommsIOXLink,
                            "Coalesced buffer size not large enough for required description(s)");
         return REMOTEOFFLOADCOMMSIO_FAIL;
      }

      gsize total_coalesced_size_left =
            COALESCED_CHUNK_SIZE - (sizeof(guint32) + nmem*sizeof(XLinkBufferDesc));
      gint current_offset = sizeof(guint32) + nmem*sizeof(XLinkBufferDesc);

      //Second pass, fill the descriptions, & coalesce.
      guint memi = 0;
      for(GList * li = mem_list; li != NULL; li = li->next, memi++ )
      {
         GstMemory *mem = (GstMemory *)li->data;
         GstMapInfo mapInfo;
         if( G_LIKELY(gst_memory_map (mem, &mapInfo, GST_MAP_READ)) )
         {
#ifdef XLINK_PERFORM_CHECKSUMS
           pDesc[memi].checksum = SimpleChecksum(mapInfo.data, mapInfo.size);
#endif
            //Check if this memory segment meets the conditions required
            // to be coalesced.
            if( (mapInfo.size < COALESCE_THRESHOLD) &&
                (mapInfo.size <= total_coalesced_size_left )
              )
            {
               pDesc[memi].offset = current_offset;
               pDesc[memi].size = mapInfo.size;

#ifndef NO_SAFESTR
              memcpy_s(pCoalesced,
                       total_coalesced_size_left,
                       mapInfo.data,
                       mapInfo.size);
#else
               memcpy(pCoalesced, mapInfo.data, mapInfo.size);
#endif

               current_offset += mapInfo.size;
               pCoalesced += mapInfo.size;
               total_coalesced_size_left -= mapInfo.size;
            }
            else
            {
               pDesc[memi].offset = -1;
               pDesc[memi].size = mapInfo.size;
            }

            gst_memory_unmap(mem, &mapInfo);
         }
         else
         {
            GST_ERROR_OBJECT (pCommsIOXLink,
                            "gst_memory_map failed for entry %u in mem list\n", memi);
            return REMOTEOFFLOADCOMMSIO_FAIL;
         }
      }

      //Third pass, actually write stuff. Start with the coalesced send buffer
      ret = remote_offload_comms_io_xlink_write(pCommsIOXLink,
                                                pCommsIOXLink->priv.pCoalescedSendMemory,
                                                current_offset);

      if( ret != REMOTEOFFLOADCOMMSIO_SUCCESS )
         return ret;

      memi = 0;
      for(GList * li = mem_list; li != NULL; li = li->next, memi++ )
      {
         GstMemory *mem = (GstMemory *)li->data;

         if( pDesc[memi].offset < 0 )
         {
            GstMapInfo mapInfo;
            if( gst_memory_map (mem, &mapInfo, GST_MAP_READ) )
            {
               ret = remote_offload_comms_io_xlink_write(pCommsIOXLink,
                                                mapInfo.data,
                                                mapInfo.size);

               gst_memory_unmap(mem, &mapInfo);

               if( G_UNLIKELY(ret != REMOTEOFFLOADCOMMSIO_SUCCESS) )
                  break;
            }
            else
            {
               GST_ERROR_OBJECT (pCommsIOXLink,
                               "gst_memory_map failed for entry %u in mem list\n", memi);
               return REMOTEOFFLOADCOMMSIO_FAIL;
            }
         }
      }
   }

   return ret;
}


static void remote_offload_comms_io_xlink_shutdown(RemoteOffloadCommsIO *commsio)
{
   RemoteOffloadCommsXLinkIO *pCommsIOXLink = REMOTEOFFLOAD_COMMSIOXLINK (commsio);

   g_mutex_lock(&pCommsIOXLink->priv.shutdownmutex);
   if( !pCommsIOXLink->priv.shutdownAsserted )
   {
      pCommsIOXLink->priv.shutdownAsserted = TRUE;

      //FIXME: Right now, there seems to be a race condition between the last write,
      // and calling xlink_close_channel. If the internal xlink-core tx thread
      // got delayed at all in sending the last message, xlink_close_channel
      // will clear the tx queue, and the last message will never get sent.
      //Add a 50 ms sleep here to give a greater chance that the last message arrived
      // as intended.
      g_usleep(50000);

      enum xlink_error err_close = xlink_close_channel(pCommsIOXLink->priv.deviceIdHandler,
                                                       pCommsIOXLink->priv.channelId);
      if( err_close != X_LINK_SUCCESS )
      {
        GST_ERROR_OBJECT (pCommsIOXLink,
                          "xlink_close_channel failed for channelId = 0x%x, err = %d",
                          pCommsIOXLink->priv.channelId, err_close);
      }

      if( pCommsIOXLink->priv.callback )
      {
         pCommsIOXLink->priv.callback(pCommsIOXLink->priv.channelId,
                                      pCommsIOXLink->priv.callback_user_data);
      }
   }
   else
   {
      GST_WARNING_OBJECT (pCommsIOXLink,
                          "shutdown has previously been asserted.");
   }
   g_mutex_unlock(&pCommsIOXLink->priv.shutdownmutex);
}

static GList * remote_offload_comms_io_xlink_get_consumable_memfeatures
        (RemoteOffloadCommsIO *commsio)
{
   GList *consumable_mem_features = NULL;

//Only claim support for DMABuf caps feature for xlink-commsio running on
// device-side. Since this caps feature is required right now to force
// vaapi decode to use DMABuf allocator, we want to make sure we don't
// filter it out.
// TODO: Remove this when vaapi is able to use DMABuf allocator even when
//  downstream element doesn't explicitly request it to do so via the caps
//  feature. (But keep it if/when XLink API natively supports DMABuf).
#ifdef XLINKSERVER
   consumable_mem_features = g_list_append(consumable_mem_features,
                                gst_caps_features_new(GST_CAPS_FEATURE_MEMORY_DMABUF, NULL));
#endif

   //As far as can be observed from testing, memory:VASurface is mappable from a READ
   // perspective.
   consumable_mem_features = g_list_append(consumable_mem_features,
                                gst_caps_features_new("memory:VASurface", NULL));

   return consumable_mem_features;
}

static void
remote_offload_comms_io_xlink_interface_init (RemoteOffloadCommsIOInterface *iface)
{
  iface->read = remote_offload_comms_io_xlink_read;
  iface->write_mem_list = remote_offload_comms_io_xlink_write_mem_list;
  iface->shutdown = remote_offload_comms_io_xlink_shutdown;
  iface->get_consumable_memfeatures = remote_offload_comms_io_xlink_get_consumable_memfeatures;
}

//This is called right after _init() and set_properties calls for default
// props passed into g_object_new()
static void remote_offload_comms_io_xlink_constructed(GObject *object)
{
  RemoteOffloadCommsXLinkIO *pCommsIOXLink = REMOTEOFFLOAD_COMMSIOXLINK (object);

  if( pCommsIOXLink->priv.deviceIdHandler &&
      pCommsIOXLink->priv.channelId )
  {
     //For XLink over PCIe, RXB_TXN proves to have much
     //better performance than RXB_TXB. There's no reason
     //to internally wait for the "other side" to read,
     //especially as the current XLink implementation
     //will perform an implicit memcpy (copy_from_user)
     //during the ioctl. The only danger here would
     //be too many back-to-back calls to xlink_write_data,
     //and running out of kernelspace data storage to copy
     //into (if the "other side" cannot read / consume it
     //fast enough).
     enum xlink_opmode transfer_mode = RXB_TXN;

     //If no preference, resort to default / env variable settings
     if( pCommsIOXLink->priv.tx_mode == XLINK_COMMSIO_TXMODE_NOPREFERENCE )
     {
        gchar **env = g_get_environ();
        const gchar *opmodeenv =
              g_environ_getenv(env,"GST_REMOTEOFFLOAD_XLINK_OPMODE");
        if( opmodeenv )
        {
           if( g_strcmp0(opmodeenv, "RXB_TXB")==0 )
           {
              transfer_mode = RXB_TXB;
           }
           else
           if( g_strcmp0(opmodeenv, "RXB_TXN")==0 )
           {
              transfer_mode = RXB_TXN;
           }
           else
           {
              GST_WARNING_OBJECT (pCommsIOXLink,
                             "Ignoring unknown opmode from env GST_REMOTEOFFLOAD_XLINK_OPMODE=%s",
                             opmodeenv);
           }
        }
        g_strfreev(env);
     }
     else
     if( pCommsIOXLink->priv.tx_mode == XLINK_COMMSIO_TXMODE_BLOCKING )
     {
        transfer_mode = RXB_TXB;
     }
     else
     if( pCommsIOXLink->priv.tx_mode == XLINK_COMMSIO_TXMODE_NONBLOCKING )
     {
        transfer_mode = RXB_TXN;
     }

     if( transfer_mode == RXB_TXB )
     {
        GST_DEBUG_OBJECT (pCommsIOXLink, "Calling xlink_open_channel for channelId=0x%x with opMode=RXB_TXB", pCommsIOXLink->priv.channelId);
     }
     else
     {
        GST_DEBUG_OBJECT (pCommsIOXLink, "Calling xlink_open_channel for channelId=0x%x with opMode=RXB_TXN", pCommsIOXLink->priv.channelId);
     }

     enum xlink_error err = xlink_open_channel(pCommsIOXLink->priv.deviceIdHandler,
                            pCommsIOXLink->priv.channelId,
                            transfer_mode,
                            XLINK_DATA_SIZE,
                            XLINK_CHANNEL_TIMEOUT);

     if( err == X_LINK_SUCCESS)
     {
        GST_DEBUG_OBJECT (pCommsIOXLink,
                          "xlink_open_channel for channelId=0x%x succeeded",
                          pCommsIOXLink->priv.channelId);

        pCommsIOXLink->priv.is_state_okay = TRUE;

     }
     else
     {
        GST_ERROR_OBJECT (pCommsIOXLink,
                          "xlink_open_channel for channelId=0x%x failed. err = %d",
                          pCommsIOXLink->priv.channelId, err);
     }

     //Allocate our send & receive coalesced buffers, if enabled
     if( pCommsIOXLink->priv.is_state_okay &&
         pCommsIOXLink->priv.coalese_mode==XLINK_COMMSIO_COALESCEMODE_ENABLE )
     {
        GST_DEBUG_OBJECT (pCommsIOXLink, "Coalesce Mode is ENABLED for channelId=0x%x\n",
                         pCommsIOXLink->priv.channelId);

        if( posix_memalign((void **)&pCommsIOXLink->priv.pCoalescedSendMemory,
                           getpagesize(), COALESCED_CHUNK_SIZE) )
        {
           GST_ERROR_OBJECT (pCommsIOXLink,
                            "posix_memalign failed for %u bytes\n",
                            COALESCED_CHUNK_SIZE);

           pCommsIOXLink->priv.is_state_okay = FALSE;
        }

        if( posix_memalign((void **)&pCommsIOXLink->priv.pCoalescedReceivedMemory,
                           getpagesize(), COALESCED_CHUNK_SIZE) )
        {
           GST_ERROR_OBJECT (pCommsIOXLink,
                             "posix_memalign failed for %u bytes\n",
                             COALESCED_CHUNK_SIZE);

           pCommsIOXLink->priv.is_state_okay = FALSE;
        }
     }
     else
     {
        GST_WARNING_OBJECT (pCommsIOXLink, "Coalesce Mode is DISABLED for channelId=0x%x\n",
                            pCommsIOXLink->priv.channelId);
     }
  }

  G_OBJECT_CLASS (remote_offload_comms_io_xlink_parent_class)->constructed (object);
}

static void
remote_offload_comms_io_xlink_set_property (GObject      *object,
                                             guint         property_id,
                                             const GValue *value,
                                             GParamSpec   *pspec)
{
  RemoteOffloadCommsXLinkIO *pCommsIOXLink = REMOTEOFFLOAD_COMMSIOXLINK (object);
  switch (property_id)
  {
    case PROP_DEVHANDLER:
    {
      pCommsIOXLink->priv.deviceIdHandler = g_value_get_pointer (value);
    }
    break;

    case PROP_CHANNELID:
    {
      pCommsIOXLink->priv.channelId = g_value_get_int (value);
    }
    break;

    case PROP_TXMODE:
    {
       pCommsIOXLink->priv.tx_mode = g_value_get_int (value);
    }
    break;

    case PROP_COALESCEMODE:
    {
       pCommsIOXLink->priv.coalese_mode = g_value_get_int (value);
    }
    break;

    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
remote_offload_comms_io_xlink_get_property (GObject    *object,
                                            guint       property_id,
                                            GValue     *value,
                                            GParamSpec *pspec)
{
  RemoteOffloadCommsXLinkIO *pCommsIOXLink = REMOTEOFFLOAD_COMMSIOXLINK (object);
  switch (property_id)
  {
    case PROP_CHANNELID:
        g_value_set_int(value, pCommsIOXLink->priv.channelId);
        break;
    case PROP_TXMODE:
        g_value_set_int(value, pCommsIOXLink->priv.tx_mode);
        break;
    case PROP_COALESCEMODE:
        g_value_set_int(value, pCommsIOXLink->priv.coalese_mode);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
  }
}

static void
remote_offload_comms_io_xlink_finalize (GObject *gobject)
{
  RemoteOffloadCommsXLinkIO *pCommsIOXLink = REMOTEOFFLOAD_COMMSIOXLINK (gobject);

#if XLINK_COLLECT_HISTOGRAM
  //print the histogram
  GST_INFO_OBJECT(pCommsIOXLink, "xlink write histogram for XLink channel=0x%x",
                  pCommsIOXLink->priv.channelId);
  for( guint bini = 0; bini < NUM_HISTOGRAM_BINS; bini++ )
  {
     if( bini==0)
     {
        GST_INFO_OBJECT(pCommsIOXLink, "size <=%u: %"G_GUINT64_FORMAT,
                        hist_thresholds[0],
                        pCommsIOXLink->priv.write_histogram[bini]);
     }
     else if( bini == (NUM_HISTOGRAM_BINS-1) )
     {
        GST_INFO_OBJECT(pCommsIOXLink, "%u < size: %"G_GUINT64_FORMAT,
                        hist_thresholds[NUM_HISTOGRAM_BINS-2],
                        pCommsIOXLink->priv.write_histogram[bini]);
     }
     else
     {
        GST_INFO_OBJECT(pCommsIOXLink, "%u < size <= %u: %"G_GUINT64_FORMAT,
                        hist_thresholds[bini-1],
                        hist_thresholds[bini],
                        pCommsIOXLink->priv.write_histogram[bini]);
     }
  }
  GST_INFO_OBJECT(pCommsIOXLink, "total bytes sent via xlink_write_data = %"G_GUINT64_FORMAT,
                                 pCommsIOXLink->priv.total_bytes_written);
#endif

  pCommsIOXLink->priv.deviceIdHandler = NULL;
  g_mutex_clear(&pCommsIOXLink->priv.shutdownmutex);

  if( pCommsIOXLink->priv.pCoalescedReceivedMemory )
     free(pCommsIOXLink->priv.pCoalescedReceivedMemory);

  if( pCommsIOXLink->priv.pCoalescedSendMemory )
     free(pCommsIOXLink->priv.pCoalescedSendMemory);

  G_OBJECT_CLASS (remote_offload_comms_io_xlink_parent_class)->finalize (gobject);
}

static void
remote_offload_comms_io_xlink_class_init (RemoteOffloadCommsXLinkIOClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = remote_offload_comms_io_xlink_set_property;
  object_class->get_property = remote_offload_comms_io_xlink_get_property;

  obj_properties[PROP_DEVHANDLER] =
    g_param_spec_pointer ("devhandler",
                         "DevHandler",
                         "XLink DevHandler object in use",
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);

  obj_properties[PROP_CHANNELID] =
    g_param_spec_int ("channelid",
                      "channelid",
                      "XLink Channel Id",
                      0,
                      INT_MAX,
                      0,
                      G_PARAM_CONSTRUCT_ONLY |
                      G_PARAM_READWRITE);

  obj_properties[PROP_TXMODE] =
     g_param_spec_int ("txmode",
                      "txmode",
                      "XLink TX Mode",
                      XLINK_COMMSIO_TXMODE_NOPREFERENCE,
                      XLINK_COMMSIO_TXMODE_NONBLOCKING,
                      XLINK_COMMSIO_TXMODE_NOPREFERENCE,
                      G_PARAM_CONSTRUCT_ONLY |
                      G_PARAM_READWRITE);

  obj_properties[PROP_COALESCEMODE] =
        g_param_spec_int ("coalescemode",
                      "coalescemode",
                      "XLink Coalesce Mode",
                      XLINK_COMMSIO_COALESCEMODE_DISABLE,
                      XLINK_COMMSIO_COALESCEMODE_ENABLE,
                      XLINK_COMMSIO_COALESCEMODE_ENABLE,
                      G_PARAM_CONSTRUCT_ONLY |
                      G_PARAM_READWRITE);

  g_object_class_install_properties (object_class,
                                     N_PROPERTIES,
                                     obj_properties);

  object_class->constructed = remote_offload_comms_io_xlink_constructed;
  object_class->finalize = remote_offload_comms_io_xlink_finalize;

}

static void
remote_offload_comms_io_xlink_init (RemoteOffloadCommsXLinkIO *self)
{
  self->priv.is_state_okay = FALSE;
  self->priv.deviceIdHandler = NULL;
  self->priv.channelId = 0;
  g_mutex_init(&self->priv.shutdownmutex);
  self->priv.shutdownAsserted = FALSE;
  self->priv.callback = NULL;
  self->priv.callback_user_data = NULL;
  self->priv.pCoalescedReceivedMemory = NULL;
  self->priv.pCoalescedSendMemory = NULL;
  self->priv.total_mems = 0;
  self->priv.current_mem_index = 0;
  self->priv.tx_mode = XLINK_COMMSIO_TXMODE_NOPREFERENCE;
  self->priv.coalese_mode = XLINK_COMMSIO_COALESCEMODE_DISABLE;

#if XLINK_COLLECT_HISTOGRAM
  for( guint bini = 0; bini < NUM_HISTOGRAM_BINS; bini++ )
  {
      self->priv.write_histogram[bini] = 0;
  }
  self->priv.total_bytes_written = 0;
#endif
}

RemoteOffloadCommsXLinkIO *remote_offload_comms_io_xlink_new (struct xlink_handle *deviceIdHandler,
                                                              xlink_channel_id_t channelId,
                                                              XLinkCommsIOTXMode tx_mode,
                                                              XLinkCommsIOCoaleseMode coalese_mode)
{
  RemoteOffloadCommsXLinkIO *pCommsXLinkIO =
        g_object_new(REMOTEOFFLOADCOMMSIOXLINK_TYPE, "devhandler", deviceIdHandler,
                                                     "channelid", channelId,
                                                     "txmode", tx_mode,
                                                     "coalescemode", coalese_mode,
                                                     NULL);

  if( pCommsXLinkIO )
  {
     if( !pCommsXLinkIO->priv.is_state_okay )
     {
        g_object_unref(pCommsXLinkIO);
        pCommsXLinkIO = NULL;
     }
  }

  return pCommsXLinkIO;
}

void
remote_offload_comms_io_xlink_set_channel_closed_callback(RemoteOffloadCommsXLinkIO *commsio,
                                                          remote_offload_comms_io_xlink_channel_closed_f callback,
                                                          void *user_data)
{
   if( !REMOTEOFFLOAD_IS_COMMSIOXLINK(commsio))
   {
      GST_ERROR("not a RemoteOffloadCommsXLinkIO..\n");
      return;
   }

   commsio->priv.callback = callback;
   commsio->priv.callback_user_data = user_data;
}

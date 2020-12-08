/*
 *  genericdataexchanger.c - GenericDataExchanger object
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
#include "genericdataexchanger.h"

enum
{
  PROP_CALLBACK = 1,
  N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

struct _GenericDataExchanger
{
  RemoteOffloadDataExchanger parent_instance;

  /* Other members, including private data. */
  GenericDataExchangerCallback *callback;
};

GST_DEBUG_CATEGORY_STATIC (generic_data_exchanger_debug);
#define GST_CAT_DEFAULT generic_data_exchanger_debug

G_DEFINE_TYPE_WITH_CODE (GenericDataExchanger,
                         generic_data_exchanger, REMOTEOFFLOADDATAEXCHANGER_TYPE,
GST_DEBUG_CATEGORY_INIT (generic_data_exchanger_debug, "remoteoffloadgenericdataexchanger", 0,
  "debug category for GenericDataExchanger"))

typedef struct _GenericExchangeHeader
{
   guint32 transfer_type;
}GenericExchangeHeader;

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

gboolean generic_data_exchanger_send(GenericDataExchanger *exchanger,
                                     guint32 transfer_type,
                                     GArray *memblocks,
                                     gboolean blocking)
{
   if( !DATAEXCHANGER_IS_GENERIC(exchanger) )
      return FALSE;

   gboolean ret = TRUE;
   GenericExchangeHeader header;
   header.transfer_type = transfer_type;
   GstMemory *headermem = virt_to_mem(&header, sizeof(header));

   GList *memList = NULL;
   memList = g_list_append (memList, headermem);

   if( memblocks )
   {
      GstMemory **gstmemarray = (GstMemory **)memblocks->data;
      guint nmappedblocks = 0;
      for( guint memblocki = 0; memblocki < memblocks->len; memblocki++, nmappedblocks++ )
      {
         memList = g_list_append (memList, gstmemarray[memblocki]);
      }
   }

   RemoteOffloadResponse *pResponse = NULL;

   if( blocking )
   {
      pResponse = remote_offload_response_new();
   }

   ret = remote_offload_data_exchanger_write((RemoteOffloadDataExchanger *)exchanger,
                                    memList,
                                    pResponse);

   if( pResponse )
   {
      if( ret )
      {
         if( remote_offload_response_wait(pResponse, 0) != REMOTEOFFLOADRESPONSE_RECEIVED )
         {
            GST_ERROR_OBJECT (exchanger, "remote_offload_response_wait failed");
            ret = FALSE;
         }
         else
         {
           if( !remote_offload_copy_response(pResponse, &ret, sizeof(ret), 0))
           {
              GST_ERROR_OBJECT (exchanger, "remote_offload_copy_response failed");
              ret = FALSE;
           }
         }
      }

      g_object_unref(pResponse);
   }


   g_list_free(memList);
   gst_memory_unref(headermem);

   return ret;
}

gboolean generic_data_exchanger_send_virt(GenericDataExchanger *exchanger,
                                          guint32 transfer_type,
                                          void *pData,
                                          gsize size,
                                          gboolean blocking)
{
   if( !DATAEXCHANGER_IS_GENERIC(exchanger) )
      return FALSE;

   gboolean ret = TRUE;
   GenericExchangeHeader header;
   header.transfer_type = transfer_type;
   GstMemory *headermem = virt_to_mem(&header, sizeof(header));

   GList *memList = NULL;
   memList = g_list_append (memList, headermem);

   GstMemory *mem = virt_to_mem(pData, size);
   if( mem )
   {
      memList = g_list_append (memList, mem);
   }

   RemoteOffloadResponse *pResponse = NULL;

   if( blocking )
   {
      pResponse = remote_offload_response_new();
   }

   ret = remote_offload_data_exchanger_write((RemoteOffloadDataExchanger *)exchanger,
                                    memList,
                                    pResponse);

   if( pResponse )
   {
      if( ret )
      {
         if( remote_offload_response_wait(pResponse, 0) != REMOTEOFFLOADRESPONSE_RECEIVED )
         {
            GST_ERROR_OBJECT (exchanger, "remote_offload_response_wait failed");
            ret = FALSE;
         }
         else
         {
           if( !remote_offload_copy_response(pResponse, &ret, sizeof(ret), 0))
           {
              GST_ERROR_OBJECT (exchanger, "remote_offload_copy_response failed");
              ret = FALSE;
           }
         }
      }

      g_object_unref(pResponse);
   }

   g_list_free(memList);
   gst_memory_unref(headermem);
   if( mem )
   {
      gst_memory_unref(mem);
   }

   return ret;
}

gboolean generic_data_exchanger_received(RemoteOffloadDataExchanger *exchanger,
                                         const GArray *segment_mem_array,
                                         guint64 response_id)
{
   if( !segment_mem_array ||
       (segment_mem_array->len < 1) ||
       !DATAEXCHANGER_IS_GENERIC(exchanger))
      return FALSE;

   gboolean ret;

   GenericDataExchanger *self = DATAEXCHANGER_GENERIC(exchanger);
   GstMemory **gstmemarray = (GstMemory **)segment_mem_array->data;

   GstMapInfo mapHeader;
   if( !gst_memory_map (gstmemarray[0], &mapHeader, GST_MAP_READ) )
   {
      GST_ERROR_OBJECT (self, "Error mapping header data segment for reading.");
      return FALSE;
   }

   GenericExchangeHeader *pHeader = (GenericExchangeHeader *)mapHeader.data;

   GArray *memblocks = g_array_sized_new(FALSE,
                                         FALSE,
                                         sizeof(GstMemory *),
                                         segment_mem_array->len - 1);

   for( guint i = 1; i < segment_mem_array->len; i++ )
   {
      g_array_append_val(memblocks, gstmemarray[i]);
   }

   gboolean callback_ret = FALSE;

   if( self->callback && self->callback->received )
   {
      callback_ret = self->callback->received(pHeader->transfer_type,
                                              memblocks,
                                              self->callback->priv);
   }
   else
   {
      GST_WARNING_OBJECT (self, "No received callback set");
   }

   g_array_unref(memblocks);

   if( response_id )
   {
      ret = remote_offload_data_exchanger_write_response_single(exchanger,
                                                                (guint8 *)&callback_ret,
                                                                sizeof(callback_ret),
                                                                response_id);
   }
   else
   {
      ret = TRUE;
   }

   gst_memory_unmap (gstmemarray[0], &mapHeader);

   return ret;
}

static void
generic_data_exchanger_set_property (GObject      *object,
                                     guint         property_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  GenericDataExchanger *self = DATAEXCHANGER_GENERIC (object);
  switch (property_id)
  {
    case PROP_CALLBACK:
    {
       self->callback = g_value_get_pointer (value);
    }
    break;

    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
generic_data_exchanger_class_init (GenericDataExchangerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  RemoteOffloadDataExchangerClass *parent_class = REMOTEOFFLOAD_DATAEXCHANGER_CLASS(klass);

  object_class->set_property = generic_data_exchanger_set_property;
  obj_properties[PROP_CALLBACK] =
    g_param_spec_pointer ("callback",
                         "Callback",
                         "Received Callback",
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);

  g_object_class_install_properties (object_class,
                                     N_PROPERTIES,
                                     obj_properties);

  parent_class->received = generic_data_exchanger_received;
  parent_class->allocate_data_segment = NULL;
}

static void
generic_data_exchanger_init (GenericDataExchanger *self)
{
  self->callback = NULL;
}

GenericDataExchanger *generic_data_exchanger_new (RemoteOffloadCommsChannel *channel,
                                                  GenericDataExchangerCallback *callback)
{
   GenericDataExchanger *pexchanger =
        g_object_new(GENERICDATAEXCHANGER_TYPE,
                     "callback", callback,
                     "commschannel", channel,
                     "exchangername", "xlink.exchanger.generic",
                     NULL);

   return pexchanger;
}

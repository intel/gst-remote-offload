/*
 *  bufferdataexchanger.c - BufferDataExchanger object
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

#include <string.h>
#include "bufferdataexchanger.h"
#include "remoteoffloadmetaserializer.h"
#include "remoteoffloadextregistry.h"

//Includes for "core" meta serializers
#include "gstvideoroimetaserializer.h"
#include "gstvideometaserializer.h"

enum
{
  PROP_CALLBACK = 1,
  N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

struct _BufferDataExchanger
{
  RemoteOffloadDataExchanger parent_instance;

  /* Other members, including private data. */
  BufferDataExchangerCallback *callback;

  GHashTable *metaSerializerHash;
  RemoteOffloadExtRegistry *ext_registry;

};

GST_DEBUG_CATEGORY_STATIC (buffer_data_exchanger_debug);
#define GST_CAT_DEFAULT buffer_data_exchanger_debug

G_DEFINE_TYPE_WITH_CODE(BufferDataExchanger, buffer_data_exchanger,
REMOTEOFFLOADDATAEXCHANGER_TYPE,
GST_DEBUG_CATEGORY_INIT (buffer_data_exchanger_debug, "remoteoffloadbufferdataexchanger", 0,
  "debug category for remoteoffloadbufferdataexchanger"))

static GQuark QUARK_BUFFER_RESPONSE_ID;

typedef struct
{
  guint64 pts; //presentation timestamp
  guint64 dts; //decoding timestamp
  guint64 duration; //duration of the data in the buffer
  guint64 offset;
  guint64 offset_end;
  guint64 flags;
  guint16 nmem;  //number of GstMemory's within GstBuffer (what gst_buffer_n_memory() returns)
  guint16 nserializedmeta;  //number of 'MetaHeaders' will be contained within
                            //BUFFEREXCHANGE_METAHEADER segment
  GstFlowReturn returnVal;

}BufferHeader;

#define META_API_STR_SIZE 128
typedef struct
{
  gchar meta_api_str[META_API_STR_SIZE];
  guint16 nsegments; //number of segments to send for this
}MetaHeader;

enum
{
   BUFFEREXCHANGE_HEADER_INDEX = 0,
};

typedef struct
{
   gchar *api_str;
   GArray *metaMemArray;
}SerializedMetaEntry;

typedef struct
{
   BufferDataExchanger *pThis;
   GArray *serializedMetaEntries;
}SerializeUserPtr;

//some small helper functions

//Get the start index & size (number of segments) of GstBuffer memory's
static inline void GetBufferMemRange(BufferHeader *header, guint *start_index, guint *size)
{
   //the start index is always 1, because the mem segments always immediately follow the
   // header (BUFFEREXCHANGE_HEADER_INDEX), which is 0
   *start_index = 1;
   *size = header->nmem;
}

static inline guint GetMetaHeaderIndex(BufferHeader *header)
{
   guint start_index, size;
   GetBufferMemRange(header, &start_index, &size);
   return start_index + size;
}

typedef enum
{
   BUFFEREXCHANGE_SEG_TYPE_HEADER = 0,
   BUFFEREXCHANGE_SEG_TYPE_MEM,
   BUFFEREXCHANGE_SEG_TYPE_METAHEADER,
   BUFFEREXCHANGE_SEG_TYPE_META
} BufferExchangeDataSegmentType;

static inline BufferExchangeDataSegmentType IndexToType(BufferHeader *header, guint index)
{
   if( index == 0 )
      return BUFFEREXCHANGE_SEG_TYPE_HEADER;

   guint metaHeaderIndex = GetMetaHeaderIndex(header);
   if( index < metaHeaderIndex )
      return BUFFEREXCHANGE_SEG_TYPE_MEM;

   if( index == metaHeaderIndex )
      return BUFFEREXCHANGE_SEG_TYPE_METAHEADER;

   return BUFFEREXCHANGE_SEG_TYPE_META;
}

static gboolean _SerializeMeta(BufferDataExchanger *self,
                               GstBuffer * buffer,
                               GstMeta ** meta,
                               SerializeUserPtr *pSerializeUserPtr)
{

  RemoteOffloadMetaSerializer *metaserializer =
        (RemoteOffloadMetaSerializer *)g_hash_table_lookup(self->metaSerializerHash,
                                                           g_type_name ((*meta)->info->api));

  if( metaserializer )
  {
     SerializedMetaEntry *entry = (SerializedMetaEntry *)g_malloc( sizeof(SerializedMetaEntry) );
     entry->metaMemArray = g_array_new(FALSE, FALSE, sizeof(GstMemory *));
     entry->api_str = g_strdup(g_type_name ((*meta)->info->api));

     if( remote_offload_meta_serialize(metaserializer,
                                        *meta,
                                        entry->metaMemArray) )
     {
        g_array_append_val(pSerializeUserPtr->serializedMetaEntries, entry);
     }
     else
     {
        GST_ERROR_OBJECT (self, "Error serializing meta of type %s\n",
                          g_type_name ((*meta)->info->api));
        return FALSE;
     }
  }

  return TRUE;
}

static gboolean SerializeMeta(GstBuffer * buffer, GstMeta ** meta, gpointer user_data)
{
   SerializeUserPtr *pSerializeUserPtr = (SerializeUserPtr *)user_data;

   return _SerializeMeta(pSerializeUserPtr->pThis,
                         buffer,
                         meta,
                         pSerializeUserPtr);
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

GstFlowReturn buffer_data_exchanger_send_buffer(BufferDataExchanger *bufferexchanger,
                                                GstBuffer *buffer)
{
   if( !DATAEXCHANGER_IS_BUFFER(bufferexchanger) ||
       !GST_IS_BUFFER(buffer) )
     return GST_FLOW_ERROR;

   gboolean ret;

   BufferHeader bufheader;

   //fill the "common" meta (various durations & flags)
   bufheader.pts = GST_BUFFER_PTS (buffer);
   bufheader.dts = GST_BUFFER_DTS (buffer);
   bufheader.duration = GST_BUFFER_DURATION (buffer);
   bufheader.offset = GST_BUFFER_OFFSET (buffer);
   bufheader.offset_end = GST_BUFFER_OFFSET_END (buffer);
   bufheader.flags = GST_BUFFER_FLAGS (buffer);
   bufheader.nserializedmeta = 0; //initialize to 0
   bufheader.nmem = (guint16)gst_buffer_n_memory(buffer);

   bufheader.returnVal = GST_FLOW_OK;

   GList *memList = NULL;
   memList = g_list_append (memList, virt_to_mem(&bufheader, sizeof(bufheader)));

   for(guint memi = 0; memi < bufheader.nmem; memi++ )
   {
     memList = g_list_append (memList, gst_buffer_get_memory(buffer, memi));
   }

   SerializeUserPtr user;
   user.serializedMetaEntries = g_array_new(FALSE, FALSE, sizeof(SerializedMetaEntry *));
   user.pThis = bufferexchanger;
   gst_buffer_foreach_meta (buffer, SerializeMeta, &user);

   bufheader.nserializedmeta = user.serializedMetaEntries->len;

   MetaHeader *metaHeaders = NULL;

   if( bufheader.nserializedmeta > 0 )
   {
      //build the vector of MetaHeader's
      metaHeaders = (MetaHeader * )g_malloc(user.serializedMetaEntries->len * sizeof(MetaHeader));
      for( guint mi = 0; mi < user.serializedMetaEntries->len; mi++)
      {
         SerializedMetaEntry *metaentry = g_array_index(user.serializedMetaEntries,
                                                        SerializedMetaEntry *,
                                                        mi);

         g_strlcpy(metaHeaders[mi].meta_api_str, metaentry->api_str, META_API_STR_SIZE);
         metaHeaders[mi].nsegments = metaentry->metaMemArray->len;
      }

      memList = g_list_append (memList, virt_to_mem(metaHeaders,
                                                    user.serializedMetaEntries->len *
                                                    sizeof(MetaHeader)));
      for( guint mi = 0; mi < user.serializedMetaEntries->len; mi++)
      {
         SerializedMetaEntry *metaentry = g_array_index(user.serializedMetaEntries,
                                                     SerializedMetaEntry *,
                                                     mi);

         for( guint memi = 0; memi < metaentry->metaMemArray->len; memi++ )
         {
            GstMemory *mem = g_array_index(metaentry->metaMemArray,
                                           GstMemory *,
                                           memi);

            memList = g_list_append (memList, mem);
         }
      }
   }


   RemoteOffloadResponse *pResponse = remote_offload_response_new();


   ret = remote_offload_data_exchanger_write((RemoteOffloadDataExchanger *)bufferexchanger,
                                                       memList,
                                                       pResponse);


   GstFlowReturn flowReturn = GST_FLOW_OK;
   if( ret )
   {
      if( remote_offload_response_wait(pResponse, 0) != REMOTEOFFLOADRESPONSE_RECEIVED )
      {
         GST_ERROR_OBJECT (bufferexchanger, "remote_offload_response_wait failed");
         flowReturn = GST_FLOW_ERROR;
      }
      else
      {
        if( !remote_offload_copy_response(pResponse, &flowReturn, sizeof(flowReturn), 0))
        {
           GST_ERROR_OBJECT (bufferexchanger, "remote_offload_copy_response failed");
           flowReturn = GST_FLOW_ERROR;
        }
      }
   }
   else
   {
      flowReturn = GST_FLOW_ERROR;
   }

   g_object_unref(pResponse);

   if( metaHeaders )
   {
      g_free(metaHeaders);
   }

   if( memList )
   {
      for(GList *li = memList; li != NULL; li = li->next )
      {
         GstMemory *mem = (GstMemory *)li->data;
         gst_memory_unref(mem);
      }

      g_list_free(memList);
   }

   for( guint mi = 0; mi < user.serializedMetaEntries->len; mi++)
   {
      SerializedMetaEntry *metaentry = g_array_index(user.serializedMetaEntries,
                                                     SerializedMetaEntry *,
                                                     mi);

      g_free(metaentry->api_str);
      g_array_free(metaentry->metaMemArray, TRUE);
      g_free( metaentry );
   }

   g_array_free(user.serializedMetaEntries, TRUE);

   return flowReturn;
}


gboolean buffer_data_exchanger_received(RemoteOffloadDataExchanger *exchanger,
                                      const GArray *segment_mem_array,
                                      guint64 response_id)
{
   if( !segment_mem_array ||
       (segment_mem_array->len < 1) ||
       !DATAEXCHANGER_IS_BUFFER(exchanger))
      return FALSE;

   gboolean ret;

   BufferDataExchanger *self = DATAEXCHANGER_BUFFER(exchanger);
   GstMemory **gstmemarray = (GstMemory **)segment_mem_array->data;

   GstMapInfo mapHeader;
   if( !gst_memory_map (gstmemarray[BUFFEREXCHANGE_HEADER_INDEX], &mapHeader, GST_MAP_READ) )
   {
      GST_ERROR_OBJECT (self, "Error mapping header data segment for reading.");
      return FALSE;
   }

   BufferHeader *pBufferHeader = (BufferHeader *)mapHeader.data;

   GstBuffer *buffer = gst_buffer_new ();

   //Insert all of the memory segments to the newly created buffer
   {
      guint start_index, size;
      GetBufferMemRange(pBufferHeader, &start_index, &size);
      for( guint i = start_index; i < (start_index + size); i++ )
      {
         gst_memory_ref(gstmemarray[i]);
         gst_buffer_insert_memory (buffer, (i - start_index), gstmemarray[i]);
      }
   }

   GST_BUFFER_PTS (buffer) = pBufferHeader->pts;
   GST_BUFFER_DTS (buffer) = pBufferHeader->dts;
   GST_BUFFER_DURATION (buffer) = pBufferHeader->duration;
   GST_BUFFER_OFFSET (buffer) = pBufferHeader->offset;
   GST_BUFFER_OFFSET_END (buffer) = pBufferHeader->offset_end;
   GST_BUFFER_FLAGS (buffer) = pBufferHeader->flags;

   if( pBufferHeader->nserializedmeta )
   {
     GstMapInfo mapMetaHeader;
     guint metaHeaderIndex = GetMetaHeaderIndex(pBufferHeader);
     if( !gst_memory_map (gstmemarray[metaHeaderIndex], &mapMetaHeader, GST_MAP_READ) )
     {
       GST_ERROR_OBJECT (self, "Error mapping header data segment for reading.");
       return FALSE;
     }

     MetaHeader *pMetaHeader = (MetaHeader *)mapMetaHeader.data;

     gsize meta_mem_segment_index = metaHeaderIndex + 1;
     for( int mhi = 0; mhi < pBufferHeader->nserializedmeta; mhi++ )
     {
        GArray *metaMemArray = g_array_new(FALSE, FALSE, sizeof(GstMemory *));
        for( int si = 0; si < pMetaHeader[mhi].nsegments; si++ )
        {
           if( meta_mem_segment_index < segment_mem_array->len )
           {
             g_array_append_val(metaMemArray, gstmemarray[meta_mem_segment_index++]);
           }
        }

        //Alright, we can deserialize & attach this meta segment now
        RemoteOffloadMetaSerializer *metaserializer = (RemoteOffloadMetaSerializer *)
                    g_hash_table_lookup(self->metaSerializerHash, pMetaHeader[mhi].meta_api_str);
        if( metaserializer )
        {
           if( !remote_offload_meta_deserialize(metaserializer,
                                                buffer,
                                                metaMemArray))
           {
              GST_ERROR_OBJECT (self, "Error in Deserialize");
           }

        }
        else
        {
           GST_WARNING_OBJECT (self, "No Meta Serializer available for type %s",
                               pMetaHeader[mhi].meta_api_str);
        }

        g_array_free(metaMemArray, TRUE);

     }

     gst_memory_unmap(gstmemarray[metaHeaderIndex], &mapMetaHeader);
   }

   //embed the response-id within the buffer mini-object, via a quark
   guint64 *pquark = g_malloc(sizeof(guint64));
   *pquark = response_id;

   gst_mini_object_set_qdata( GST_MINI_OBJECT(buffer),
                              QUARK_BUFFER_RESPONSE_ID,
                              pquark,
                              NULL);


   if( self->callback && self->callback->buffer_received )
   {
      self->callback->buffer_received(buffer, self->callback->priv);
   }
   else
   {
      GST_WARNING_OBJECT (self, "No buffer_received callback set");
   }

   ret = TRUE;

   gst_memory_unmap(gstmemarray[BUFFEREXCHANGE_HEADER_INDEX], &mapHeader);

   return ret;
}

//Send the result of gst_pad_push(src, buffer)
gboolean buffer_data_exchanger_send_buffer_flowreturn(BufferDataExchanger *bufferexchanger,
                                                  GstBuffer *buffer,
                                                  GstFlowReturn returnVal)
{
   if( !DATAEXCHANGER_IS_BUFFER(bufferexchanger) ||
       !GST_IS_BUFFER(buffer) )
   {
      return FALSE;
   }


   guint64 *presponseId = (guint64 *)gst_mini_object_steal_qdata(GST_MINI_OBJECT(buffer),
                                               QUARK_BUFFER_RESPONSE_ID);

   if( !presponseId )
   {
      GST_ERROR_OBJECT (bufferexchanger, "QUARK_BUFFER_RESPONSE_ID not embedded in buffer");
      return FALSE;
   }


   gboolean ret =
         remote_offload_data_exchanger_write_response_single((RemoteOffloadDataExchanger *)
                                                             bufferexchanger,
                                                             (guint8 *)&returnVal,
                                                             sizeof(returnVal),
                                                             *presponseId);

   g_free(presponseId);

   return ret;

}

static GstMemory *_AllocMetaDataSegment(BufferDataExchanger *self,
                                        guint16 segmentIndex,
                                        guint64 segmentSize,
                                        const GArray *segmentMemArraySoFar)
{
   GstMemory *mem = NULL;

   GstMemory **segmentMemsSoFar = (GstMemory **)segmentMemArraySoFar->data;

   //This is a meta data segment. We need to:
   // 1. Determine & obtain the RemoteOffloadMetaSerializer object that owns this segment.
   // 2. Request the GstMemory object from that object.
   GstMapInfo mapBufferHeader;
   if( gst_memory_map (segmentMemsSoFar[BUFFEREXCHANGE_HEADER_INDEX],
                       &mapBufferHeader, GST_MAP_READ) )
   {
      if( mapBufferHeader.data )
      {
         BufferHeader *pBufferHeader = (BufferHeader *)mapBufferHeader.data;

         // Map the META HEADER segment
         GstMapInfo mapMetaHeader;
         guint metaHeaderIndex = GetMetaHeaderIndex(pBufferHeader);
         if( gst_memory_map (segmentMemsSoFar[metaHeaderIndex], &mapMetaHeader, GST_MAP_READ) )
         {
             if( mapMetaHeader.data )
             {
                MetaHeader *pMetaHeader = (MetaHeader *)mapMetaHeader.data;

                //we need to calculate the meta_header_index for which this segmentIndex is part of
                int meta_header_index = -1;
                int segmentOffsetIndex = segmentIndex - (metaHeaderIndex + 1);
                int segi = 0;
                for( int hi = 0; hi < pBufferHeader->nserializedmeta; hi++)
                {
                   if( segmentOffsetIndex < (pMetaHeader[hi].nsegments + segi) )
                   {
                      meta_header_index = hi;
                      break;
                   }

                   segi += pMetaHeader[hi].nsegments;
                }

                if( meta_header_index >= 0 )
                {
                   GArray *metamemarraysofar = g_array_new(FALSE, FALSE, sizeof(GstMemory *));

                   int meta_seg_base_index = segi + metaHeaderIndex + 1;
                   for( int si = meta_seg_base_index; si < segmentIndex; si++ )
                   {
                      g_array_append_val(metamemarraysofar, segmentMemsSoFar[si]);
                   }

                   RemoteOffloadMetaSerializer *metaserializer = (RemoteOffloadMetaSerializer *)
                           g_hash_table_lookup(self->metaSerializerHash,
                                               pMetaHeader[meta_header_index].meta_api_str);

                   if( metaserializer )
                   {
                      mem =
                       remote_offload_meta_allocate_data_segment(metaserializer,
                                                                 segmentIndex - meta_seg_base_index,
                                                                 segmentSize,
                                                                 metamemarraysofar);
                   }
                   g_array_free(metamemarraysofar, TRUE);
                }
             }
             gst_memory_unmap(segmentMemsSoFar[metaHeaderIndex], &mapMetaHeader);
         }
      }
      gst_memory_unmap(segmentMemsSoFar[BUFFEREXCHANGE_HEADER_INDEX], &mapBufferHeader);
   }

   return mem;
}


static GstMemory* buffer_data_exchanger_allocate_data_segment(RemoteOffloadDataExchanger *exchanger,
                                       guint16 segmentIndex,
                                       guint64 segmentSize,
                                       const GArray *segment_mem_array_so_far)
{
   BufferDataExchanger *self = DATAEXCHANGER_BUFFER (exchanger);
   GstMemory *mem = NULL;
   GstMemory **segmentMemsSoFar = (GstMemory **)segment_mem_array_so_far->data;

   if( segmentIndex == BUFFEREXCHANGE_HEADER_INDEX )
   {
      //this is the header. Just use default allocator
      mem = gst_allocator_alloc (NULL, segmentSize, NULL);
   }
   else
   {
      GstMapInfo mapBufferHeader;
      if( gst_memory_map (segmentMemsSoFar[BUFFEREXCHANGE_HEADER_INDEX],
                          &mapBufferHeader, GST_MAP_READ) )
      {
         BufferHeader *pBufferHeader = (BufferHeader *)mapBufferHeader.data;

         BufferExchangeDataSegmentType type = IndexToType(pBufferHeader, segmentIndex);
         switch(type)
         {
            case BUFFEREXCHANGE_SEG_TYPE_METAHEADER:
            {
              //this is the meta header. Just use default allocator
              mem = gst_allocator_alloc (NULL, segmentSize, NULL);
            }
            break;
            case BUFFEREXCHANGE_SEG_TYPE_MEM:
            {
               if( self->callback && self->callback->alloc_buffer_mem_block )
               {
                  mem = self->callback->alloc_buffer_mem_block(segmentSize, self->callback->priv);
               }
               else
               {
                  mem = gst_allocator_alloc (NULL, segmentSize, NULL);
               }
            }
            break;
            case BUFFEREXCHANGE_SEG_TYPE_META:
            {
               mem = _AllocMetaDataSegment(self,
                                           segmentIndex,
                                           segmentSize,
                                           segment_mem_array_so_far);
            }
            break;

            default:
            break;
         }
      }
      gst_memory_unmap(segmentMemsSoFar[BUFFEREXCHANGE_HEADER_INDEX], &mapBufferHeader);
   }

   return mem;
}

static void
buffer_data_exchanger_set_property (GObject      *object,
                                    guint         property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  BufferDataExchanger *self = DATAEXCHANGER_BUFFER (object);
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
buffer_data_exchanger_finalize (GObject *object)
{
  BufferDataExchanger *self = DATAEXCHANGER_BUFFER(object);

  g_hash_table_destroy(self->metaSerializerHash);
  remote_offload_ext_registry_unref(self->ext_registry);

  G_OBJECT_CLASS (buffer_data_exchanger_parent_class)->finalize (object);
}


static void
buffer_data_exchanger_class_init (BufferDataExchangerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  RemoteOffloadDataExchangerClass *parent_class = REMOTEOFFLOAD_DATAEXCHANGER_CLASS(klass);

  QUARK_BUFFER_RESPONSE_ID = g_quark_from_static_string ("remoteoffload-buffer-id");

  object_class->set_property = buffer_data_exchanger_set_property;
  obj_properties[PROP_CALLBACK] =
    g_param_spec_pointer ("callback",
                         "Callback",
                         "Received Callback",
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);

  g_object_class_install_properties (object_class,
                                     N_PROPERTIES,
                                     obj_properties);

  object_class->finalize = buffer_data_exchanger_finalize;
  parent_class->received = buffer_data_exchanger_received;
  parent_class->allocate_data_segment = buffer_data_exchanger_allocate_data_segment;
}

static void KeyDestroyNotify(gpointer data)
{
  g_free(data);
}

static void ValueDestroyNotify(gpointer data)
{
  g_object_unref((GObject *)data);
}

static void
buffer_data_exchanger_init (BufferDataExchanger *self)
{
  self->callback = NULL;
  self->metaSerializerHash = g_hash_table_new_full(g_str_hash,
                                                   g_str_equal,
                                                   KeyDestroyNotify,
                                                   ValueDestroyNotify);

  //TODO: move to constructed method
  self->ext_registry = remote_offload_ext_registry_get_instance();

  GArray *pairarray = remote_offload_ext_registry_generate(self->ext_registry,
                                                           REMOTEOFFLOADMETASERIALIZER_TYPE);

  if( pairarray )
  {
     for( guint i = 0;  i < pairarray->len; i++ )
     {
        RemoteOffloadExtTypePair *pair = &g_array_index(pairarray, RemoteOffloadExtTypePair, i);

        if( REMOTEOFFLOAD_IS_METASERIALIZER(pair->obj) )
        {
           RemoteOffloadMetaSerializer *metaserializer = (RemoteOffloadMetaSerializer *)pair->obj;
           gchar *name = g_strdup (remote_offload_meta_api_name(metaserializer));
           GST_DEBUG_OBJECT (self, "Registering support for serialization of meta=%s ", name);
           g_hash_table_insert(self->metaSerializerHash, name, metaserializer);
        }
        else
        {
           GST_WARNING_OBJECT (self,"remote_offload_ext_registry returned "
                               "invalid RemoteOffloadMetaSerializer\n");
        }
     }

     g_array_free(pairarray, TRUE);
  }

  RemoteOffloadMetaSerializer *pVideoROIMetaSerializer =
        (RemoteOffloadMetaSerializer *)gst_videoroi_metaserializer_new();
  g_hash_table_insert(self->metaSerializerHash,
                      g_strdup (remote_offload_meta_api_name(pVideoROIMetaSerializer)),
                      pVideoROIMetaSerializer);

  RemoteOffloadMetaSerializer *pVideoMetaSerializer =
        (RemoteOffloadMetaSerializer *)gst_video_metaserializer_new();
  g_hash_table_insert(self->metaSerializerHash,
                      g_strdup (remote_offload_meta_api_name(pVideoMetaSerializer)),
                      pVideoMetaSerializer);

}

BufferDataExchanger *buffer_data_exchanger_new (RemoteOffloadCommsChannel *channel,
                                                BufferDataExchangerCallback *pcallback)
{
   BufferDataExchanger *pexchanger =
        g_object_new(BUFFERDATAEXCHANGER_TYPE,
                     "callback", pcallback,
                     "commschannel", channel,
                     "exchangername", "xlink.exchanger.buffer",
                     NULL);

   return pexchanger;
}

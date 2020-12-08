/*
 *  gvaaudioeventmetaserializer.c - GVAAudioEventMetaSerializer object
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

#include <gst/base/gstbytewriter.h>
#include <gva_audio_event_meta.h>
#ifndef NO_SAFESTR
  #include <safe_mem_lib.h>
#endif
#include "gvaaudioeventmetaserializer.h"
#include "remoteoffloadmetaserializer.h"

struct _GVAAudioEventMetaSerializer
{
  GObject parent_instance;
};

static void
gva_audioevent_metaserializer_interface_init (RemoteOffloadMetaSerializerInterface *iface);

GST_DEBUG_CATEGORY_STATIC (gva_audioevent_metaserializer_debug);
#define GST_CAT_DEFAULT gva_audioevent_metaserializer_debug

G_DEFINE_TYPE_WITH_CODE (GVAAudioEventMetaSerializer,
                         gva_audioevent_metaserializer, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (REMOTEOFFLOADMETASERIALIZER_TYPE,
                                                gva_audioevent_metaserializer_interface_init)
                         GST_DEBUG_CATEGORY_INIT (gva_audioevent_metaserializer_debug,
                         "remoteoffloadgvaaudioeventmetaserializer", 0,
                         "debug category for GVAAudioEventMetaSerializer"))


typedef struct
{
   gint32 id;
   guint32 event_type_strsize;
   guint64 start_timestamp;
   guint64 end_timestamp;
   guint32 nparams;  //GstGVAAudioEventMeta.params->len
} AudioEventMetaHeader;

typedef struct
{
   guint32 is_data_buffer;
   guint32 data_buffer_size;
} AudioEventMetaParamHeader;

//Serialized GVAAudioEventMeta looks like this:
//[AudioEventMetaHeader]
//[gchar *roi_str] (roi_type_strsize bytes)
//(for each VideoROIMetaHeader.nparams):
//[AudioEventMetaParamHeader]
//[guint32 str_str_size]
//[gchar *str_string] (str_str_size bytes)
//AudioEventMetaParamHeader.data_buffer_size bytes

static void SerializedDestroy(gpointer data)
{
   g_free(data);
}

static inline void AddStringToBW(GstByteWriter *bw, char *str)
{
   if( str )
   {
     guint32 size =
#ifndef NO_SAFESTR
           strnlen_s(str, RSIZE_MAX_STR) + 1;
#else
           strlen(str) + 1;
#endif
     gst_byte_writer_put_data( bw, (const guint8 *)&size, sizeof(size));
     gst_byte_writer_put_data( bw, (const guint8 *)str, size);
   }
   else
   {
     guint32 size = 1;
     gst_byte_writer_put_data( bw, (const guint8 *)&size, sizeof(size));
     gst_byte_writer_put_uint8 (bw, 0);
   }
}

static void SerializeParams(gpointer       data,
                            gpointer       user_data)
{
  GstStructure* s = (GstStructure*)data;
  GstByteWriter *bw = (GstByteWriter *)user_data;

  AudioEventMetaParamHeader header;
  header.is_data_buffer = 0;
  header.data_buffer_size = 0;

  GstStructure *s_cpy = NULL;
  const void *array_data = NULL;
  const GValue *f = gst_structure_get_value(s, "data_buffer");
  if( f )
  {
     header.is_data_buffer = 1;
     GVariant *v = g_value_get_variant(f);
     gsize n_elem;
     array_data = g_variant_get_fixed_array(v, &n_elem, 1);
     if( array_data )
     {
        header.data_buffer_size = n_elem;
     }

     s_cpy = gst_structure_copy(s);
     gst_structure_remove_field(s_cpy, "data_buffer");
     gst_structure_remove_field(s_cpy, "data");

     s = s_cpy;
  }

  gst_byte_writer_put_data( bw, (const guint8 *)&header, sizeof(header));

  gchar *str_string = gst_structure_to_string(s);
  AddStringToBW(bw, str_string);
  if( str_string )
     g_free(str_string);
  if( s_cpy )
     gst_structure_free(s_cpy);

  if( array_data )
  {
    gst_byte_writer_put_data( bw, (const guint8 *)array_data, header.data_buffer_size);
  }

}

static const gchar* gva_audioevent_metaserializer_api_name(RemoteOffloadMetaSerializer *serializer)
{
   return "GstGVAAudioEventMetaAPI";
}

static gboolean gva_audioevent_metaserializer_serialize(RemoteOffloadMetaSerializer *serializer,
                                       GstMeta *meta,
                                       GArray *metaMemArray)
{
   GstGVAAudioEventMeta* audio_event_meta = (GstGVAAudioEventMeta*) meta;

   GstByteWriter bw;
   gst_byte_writer_init (&bw);

   //build the VideoROIMetaHeader
   AudioEventMetaHeader gvaMetaHeader;
   gvaMetaHeader.id = audio_event_meta->id;
   gvaMetaHeader.start_timestamp = audio_event_meta->start_timestamp;
   gvaMetaHeader.end_timestamp = audio_event_meta->end_timestamp;

   gvaMetaHeader.event_type_strsize = 0;
   const gchar *event_str = g_quark_to_string(audio_event_meta->event_type);
   if( event_str )
   {
     gvaMetaHeader.event_type_strsize =
#ifndef NO_SAFESTR
           strnlen_s(event_str, RSIZE_MAX_STR) + 1;
#else
           strlen(roi_str) + 1;
#endif
   }

   gvaMetaHeader.nparams = 0;
   if( audio_event_meta->params )
   {
      gvaMetaHeader.nparams = g_list_length(audio_event_meta->params);
   }

   gst_byte_writer_put_data (&bw, (const guint8 *)&gvaMetaHeader, sizeof(gvaMetaHeader));

   //write the roi_str
   if( gvaMetaHeader.event_type_strsize )
     gst_byte_writer_put_data (&bw, (const guint8 *)event_str, gvaMetaHeader.event_type_strsize);

   //serialize the parameters
   if( audio_event_meta->params )
   {
      g_list_foreach(audio_event_meta->params, SerializeParams, &bw);
   }

   gsize mem_size = gst_byte_writer_get_pos(&bw);

   void *bwdata = gst_byte_writer_reset_and_get_data(&bw);
   GstMemory *mem = gst_memory_new_wrapped((GstMemoryFlags)0,
                                            bwdata,
                                            mem_size,
                                            0,
                                            mem_size,
                                            bwdata,
                                            SerializedDestroy);

   g_array_append_val(metaMemArray, mem);


   return TRUE;
}


static gboolean gva_audioevent_metaserializer_deserialize(RemoteOffloadMetaSerializer *serializer,
                                                          GstBuffer *buffer,
                                                          const GArray *metaMemArray)
{
   if( metaMemArray->len != 1 )
   {
      GST_ERROR_OBJECT (serializer, "metaMemArray has incorrect size!");
      return FALSE;
   }

   GstMemory **gstmems = (GstMemory **)metaMemArray->data;

   GstMapInfo mapInfo;
   if( !gst_memory_map (gstmems[0], &mapInfo, GST_MAP_READ) )
   {
     GST_ERROR_OBJECT (serializer, "Error mapping mem for reading.");
     return FALSE;
   }

   guint8 *pSerialized = mapInfo.data;
   AudioEventMetaHeader *pMetaHeader = (AudioEventMetaHeader *)pSerialized;
   pSerialized += sizeof(AudioEventMetaHeader);
   gchar *event_type_str = 0;
   if( pMetaHeader->event_type_strsize )
   {
      event_type_str = (gchar *)pSerialized;
      pSerialized += pMetaHeader->event_type_strsize;
   }

   //at this point, we can add the video roi meta to the buffer
   GstGVAAudioEventMeta *meta = gst_gva_buffer_add_audio_event_meta(
            buffer, event_type_str, (gulong)pMetaHeader->start_timestamp,
                                    (gulong)pMetaHeader->end_timestamp);

   //for each param, deserialize it and add it to the meta
   for(guint32 parami = 0; parami < pMetaHeader->nparams; parami++ )
   {
      AudioEventMetaParamHeader *pParamHeader = (AudioEventMetaParamHeader *)pSerialized;
      pSerialized += sizeof(AudioEventMetaParamHeader);

      guint32 *size = (guint32 *)pSerialized;
      pSerialized += sizeof(guint32);

      gchar *str = (gchar *)pSerialized;
      pSerialized += *size;


      GstStructure *s = gst_structure_from_string(str, NULL);
      if( s == NULL )
      {
         GST_WARNING_OBJECT (serializer, "gst_structure_from_string returned NULL");
      }

      if( pParamHeader->is_data_buffer )
      {
         gsize n_elem;
         GVariant *v = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                                 pSerialized,
                                                 pParamHeader->data_buffer_size, 1);
         gst_structure_set(s, "data_buffer", G_TYPE_VARIANT, v, "data", G_TYPE_POINTER,
                       g_variant_get_fixed_array(v, &n_elem, 1), NULL);

         pSerialized += pParamHeader->data_buffer_size;
      }

      gst_gva_audio_event_meta_add_param(meta, s);
   }

   gst_memory_unmap(gstmems[0], &mapInfo);

   return TRUE;
}


static void
gva_audioevent_metaserializer_interface_init (RemoteOffloadMetaSerializerInterface *iface)
{
  iface->api_name = gva_audioevent_metaserializer_api_name;
  iface->serialize = gva_audioevent_metaserializer_serialize;
  iface->deserialize = gva_audioevent_metaserializer_deserialize;
  iface->allocate_data_segment = NULL;
}

static void
gva_audioevent_metaserializer_class_init (GVAAudioEventMetaSerializerClass *klass)
{
  //GObjectClass *object_class = G_OBJECT_CLASS (klass);

}

static void
gva_audioevent_metaserializer_init (GVAAudioEventMetaSerializer *self)
{

}

GVAAudioEventMetaSerializer *gva_audioevent_metaserializer_new()
{
  return g_object_new(GVAAUDIOEVENTMETASERIALIZER_TYPE, NULL);
}

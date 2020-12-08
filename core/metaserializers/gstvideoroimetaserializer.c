/*
 *  gstvideoroimetaserializer.c - GstVideoRegionOfInterestMetaSerializer object
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

#include <gst/base/gstbytewriter.h>
#include <gst/video/gstvideometa.h>
#ifndef NO_SAFESTR
  #include <safe_mem_lib.h>
#endif
#include "gstvideoroimetaserializer.h"
#include "remoteoffloadmetaserializer.h"

struct _GstVideoRegionOfInterestMetaSerializer
{
  GObject parent_instance;

};

static void
gst_videoroi_metaserializer_interface_init (RemoteOffloadMetaSerializerInterface *iface);

GST_DEBUG_CATEGORY_STATIC (videoroi_metaserializer_debug);
#define GST_CAT_DEFAULT videoroi_metaserializer_debug

G_DEFINE_TYPE_WITH_CODE (GstVideoRegionOfInterestMetaSerializer,
                         gst_videoroi_metaserializer, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (REMOTEOFFLOADMETASERIALIZER_TYPE,
                                                gst_videoroi_metaserializer_interface_init)
                         GST_DEBUG_CATEGORY_INIT (videoroi_metaserializer_debug,
                         "remoteoffloadvideoroimetaserializer", 0,
                         "debug category for GstVideoRegionOfInterestMetaSerializer"))


typedef struct
{
   guint x;
   guint y;
   guint w;
   guint h;
   guint32 roi_type_strsize;
   guint32 nparams;  //GstVideoRegionOfInterestMeta.params->len
} VideoROIMetaHeader;

typedef struct
{
   guint32 is_data_buffer;
   guint32 data_buffer_size;
} VideoROIMetaParamHeader;

//Serialized GstVideoRegionOfInterestMeta looks like this:
//[VideoROIMetaHeader]
//[gchar *roi_str] (roi_type_strsize bytes)
//(for each VideoROIMetaHeader.nparams):
//[VideoROIMetaParamHeader]
//[guint32 str_str_size]
//[gchar *str_string] (str_str_size bytes)
//VideoROIMetaParamHeader.data_buffer_size bytes

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

  VideoROIMetaParamHeader header;
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

  //FIXME: For GVA classification results, the labels field (a string) might be really huge.
  // 1. We get hit with an extra performance penalty for serializing this data.
  // 2. It could very easily exceed safestring limits (4096 characters), which can
  //    lead to undefined behavior.
  // As this probably is unused by downstream elements (it's more informational),
  //  remove this field from the structure before serializing it to a string.
  gst_structure_remove_field(s, "labels");

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


static const gchar* gst_videoroi_metaserializer_api_name(RemoteOffloadMetaSerializer *serializer)
{
   return "GstVideoRegionOfInterestMetaAPI";
}

static gboolean gst_videoroi_metaserializer_serialize(RemoteOffloadMetaSerializer *serializer,
                                       GstMeta *meta,
                                       GArray *metaMemArray)
{
   GstVideoRegionOfInterestMeta* vidroi_meta = (GstVideoRegionOfInterestMeta*) meta;

   GstByteWriter bw;
   gst_byte_writer_init (&bw);

   //build the VideoROIMetaHeader
   VideoROIMetaHeader gvaMetaHeader;
   gvaMetaHeader.x = vidroi_meta->x;
   gvaMetaHeader.y = vidroi_meta->y;
   gvaMetaHeader.w = vidroi_meta->w;
   gvaMetaHeader.h = vidroi_meta->h;

   gvaMetaHeader.roi_type_strsize = 0;
   const gchar *roi_str = g_quark_to_string(vidroi_meta->roi_type);
   if( roi_str )
   {
     gvaMetaHeader.roi_type_strsize =
#ifndef NO_SAFESTR
           strnlen_s(roi_str, RSIZE_MAX_STR) + 1;
#else
           strlen(roi_str) + 1;
#endif
   }

   gvaMetaHeader.nparams = 0;
   if( vidroi_meta->params )
   {
      gvaMetaHeader.nparams = g_list_length(vidroi_meta->params);
   }

   gst_byte_writer_put_data (&bw, (const guint8 *)&gvaMetaHeader, sizeof(gvaMetaHeader));

   //write the roi_str
   if( gvaMetaHeader.roi_type_strsize )
     gst_byte_writer_put_data (&bw, (const guint8 *)roi_str, gvaMetaHeader.roi_type_strsize);

   //serialize the parameters
   if( vidroi_meta->params )
   {
      g_list_foreach(vidroi_meta->params, SerializeParams, &bw);
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


static gboolean gst_videoroi_metaserializer_deserialize(RemoteOffloadMetaSerializer *serializer,
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
   VideoROIMetaHeader *pMetaHeader = (VideoROIMetaHeader *)pSerialized;
   pSerialized += sizeof(VideoROIMetaHeader);
   gchar *roi_type_str = 0;
   if( pMetaHeader->roi_type_strsize )
   {
      roi_type_str = (gchar *)pSerialized;
      pSerialized += pMetaHeader->roi_type_strsize;
   }

   //at this point, we can add the video roi meta to the buffer
   GstVideoRegionOfInterestMeta *meta = gst_buffer_add_video_region_of_interest_meta(
            buffer, roi_type_str, pMetaHeader->x, pMetaHeader->y, pMetaHeader->w, pMetaHeader->h);

   //for each param, deserialize it and add it to the meta
   for(guint32 parami = 0; parami < pMetaHeader->nparams; parami++ )
   {
      VideoROIMetaParamHeader *pParamHeader = (VideoROIMetaParamHeader *)pSerialized;
      pSerialized += sizeof(VideoROIMetaParamHeader);

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

      gst_video_region_of_interest_meta_add_param(meta, s);
   }

   gst_memory_unmap(gstmems[0], &mapInfo);

   return TRUE;
}


static void
gst_videoroi_metaserializer_interface_init (RemoteOffloadMetaSerializerInterface *iface)
{
  iface->api_name = gst_videoroi_metaserializer_api_name;
  iface->serialize = gst_videoroi_metaserializer_serialize;
  iface->deserialize = gst_videoroi_metaserializer_deserialize;
  iface->allocate_data_segment = NULL;
}

static void
gst_videoroi_metaserializer_class_init (GstVideoRegionOfInterestMetaSerializerClass *klass)
{
  //GObjectClass *object_class = G_OBJECT_CLASS (klass);

}

static void
gst_videoroi_metaserializer_init (GstVideoRegionOfInterestMetaSerializer *self)
{

}

GstVideoRegionOfInterestMetaSerializer *gst_videoroi_metaserializer_new()
{
  return g_object_new(GSTVIDEOROIMETASERIALIZER_TYPE, NULL);
}

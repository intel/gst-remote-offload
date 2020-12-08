/*
 *  gvatensormetaserializer.c - GVATensorMetaSerializer object
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
#include "gvatensormetaserializer.h"
#include "remoteoffloadmetaserializer.h"
#include "gva_tensor_meta.h"

#ifndef NO_SAFESTR
  #include <safe_mem_lib.h>
#else
  #include <string.h>
#endif

struct _GVATensorMetaSerializer
{
  GObject parent_instance;

};

static void gva_tensor_metaserializer_interface_init (RemoteOffloadMetaSerializerInterface *iface);

GST_DEBUG_CATEGORY_STATIC (gvatensor_metaserializer_debug);
#define GST_CAT_DEFAULT gvatensor_metaserializer_debug

G_DEFINE_TYPE_WITH_CODE (GVATensorMetaSerializer, gva_tensor_metaserializer, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (REMOTEOFFLOADMETASERIALIZER_TYPE,
                                                gva_tensor_metaserializer_interface_init)
                         GST_DEBUG_CATEGORY_INIT (gvatensor_metaserializer_debug,
                                                  "remoteoffloadgvatensormetaserializer", 0,
                                                  "debug category for GVATensorMetaSerializer"))

//Serialized GstGVATensorMeta looks like this:
//guint32 data_buffer_size
//guint8* data_buffer (data_buffer_size bytes)
//guint32 str_size
//char *str (str_size bytes)

static const gchar* gva_tensor_metaserializer_api_name(RemoteOffloadMetaSerializer *serializer)
{
   return "GstGVATensorMetaAPI";
}

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

static gboolean gva_tensor_metaserializer_serialize(RemoteOffloadMetaSerializer *serializer,
                                                    GstMeta *meta,
                                                    GArray *metaMemArray)
{
   GstGVATensorMeta* gva_meta = (GstGVATensorMeta*) meta;

   GstByteWriter bw;
   gst_byte_writer_init (&bw);

   if( gva_meta->data )
   {
      guint32 data_buffer_size = 0;
      gconstpointer data_buffer = NULL;
      const GValue *data_buffer_val = gst_structure_get_value(gva_meta->data, "data_buffer");
      if( data_buffer_val )
      {
         GVariant *v = g_value_get_variant(data_buffer_val);
         gsize size;
         data_buffer =  g_variant_get_fixed_array(v, &size, 1);
         data_buffer_size = size;
      }

      gst_byte_writer_put_data( &bw, (const guint8 *)&data_buffer_size, sizeof(data_buffer_size));
      if( data_buffer )
      {
         gst_byte_writer_put_data( &bw, (const guint8 *)data_buffer, data_buffer_size);
         gst_structure_remove_field(gva_meta->data, "data_buffer");
         gst_structure_remove_field(gva_meta->data, "data");
      }

      gchar *str = gst_structure_to_string(gva_meta->data);
      AddStringToBW( &bw, str);
      if( str )
         g_free(str);

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
   }
   else
   {
      GST_ERROR_OBJECT (serializer, "GstStructure 'data' within GstGVATensorMeta is NULL");
   }

   return TRUE;
}

gchar *ExtractStringFromSerialization(guint8** pSerialized)
{
    guint32 *size = (guint32 *)*pSerialized;
    *pSerialized += sizeof(guint32);

    gchar *str = (gchar *)*pSerialized;
    *pSerialized += *size;

    return str;
}

static gboolean gva_tensor_metaserializer_deserialize(RemoteOffloadMetaSerializer *serializer,
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

   guint32 data_size = *((guint32 *)pSerialized);
   pSerialized += sizeof(guint32);
   guint8* pData = NULL;
   if( data_size )
   {
      pData = (guint8 *)pSerialized;
   }
   pSerialized += data_size;

   guint32 str_size = *((guint32 *)pSerialized);
   pSerialized += sizeof(guint32);
   gchar* pStr = NULL;
   if( str_size )
   {
      pStr = (gchar *)pSerialized;
   }
   pSerialized += str_size;

   GstStructure *struct_from_string =
         gst_structure_from_string(pStr, NULL);

   if( struct_from_string )
   {
      GstGVATensorMeta *meta = GST_GVA_TENSOR_META_ADD(buffer);
      if( !meta )
      {
         GST_ERROR_OBJECT (serializer, "GST_GVA_TENSOR_META_ADD failed");
         gst_memory_unmap(gstmems[0], &mapInfo);
         return FALSE;
      }

      //replace GstStrucure within meta with ours.
      if( meta->data )
         gst_structure_free(meta->data);
      meta->data = struct_from_string;

      //implementation of gst_gva_tensor_meta_init sets name
      // of gst structure to "meta". Not sure if this matters
      // or not.
      gst_structure_set_name(meta->data, "meta");

      //add 'data_buffer', 'data' fields
      if( pData )
      {
         GVariant *v =
               g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, pData, data_size, 1);
         gsize n_elem;
         gst_structure_set(meta->data, "data_buffer", G_TYPE_VARIANT, v, "data", G_TYPE_POINTER,
                           g_variant_get_fixed_array(v, &n_elem, 1), NULL);
      }
   }
   else
   {
      GST_ERROR_OBJECT (serializer, "gst_structure_from_string failed.");
   }

   gst_memory_unmap(gstmems[0], &mapInfo);

   return TRUE;
}

static void gva_tensor_metaserializer_interface_init (RemoteOffloadMetaSerializerInterface *iface)
{
   iface->api_name = gva_tensor_metaserializer_api_name;
   iface->serialize = gva_tensor_metaserializer_serialize;
   iface->deserialize = gva_tensor_metaserializer_deserialize;
   iface->allocate_data_segment = NULL;
}

static void
gva_tensor_metaserializer_class_init (GVATensorMetaSerializerClass *klass)
{
//   GObjectClass *object_class = G_OBJECT_CLASS (klass);
}

static void
gva_tensor_metaserializer_init (GVATensorMetaSerializer *self)
{

}

GVATensorMetaSerializer *gva_tensor_metaserializer_new()
{
  return g_object_new(GVATENSORMETASERIALIZER_TYPE, NULL);
}



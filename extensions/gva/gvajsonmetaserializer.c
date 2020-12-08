/*
 *  gvajsonmetaserializer.c - GVAJSONMetaSerializer object
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
#ifndef NO_SAFESTR
  #include <safe_mem_lib.h>
#endif
#include "gvajsonmetaserializer.h"
#include "remoteoffloadmetaserializer.h"
#include "gva_json_meta.h"

struct _GVAJSONMetaSerializer
{
  GObject parent_instance;

};

static void gva_json_metaserializer_interface_init (RemoteOffloadMetaSerializerInterface *iface);

GST_DEBUG_CATEGORY_STATIC (gvajson_metaserializer_debug);
#define GST_CAT_DEFAULT gvajson_metaserializer_debug

G_DEFINE_TYPE_WITH_CODE (GVAJSONMetaSerializer, gva_json_metaserializer, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (REMOTEOFFLOADMETASERIALIZER_TYPE,
                                                gva_json_metaserializer_interface_init)
                         GST_DEBUG_CATEGORY_INIT (gvajson_metaserializer_debug,
                                                  "remoteoffloadgvajsonmetaserializer", 0,
                                                  "debug category for GVAJSONMetaSerializer"))


static const gchar* gva_json_metaserializer_api_name(RemoteOffloadMetaSerializer *serializer)
{
   return "GstGVAJSONMetaAPI";
}


static gboolean gva_json_metaserializer_serialize(RemoteOffloadMetaSerializer *serializer,
                                                  GstMeta *meta,
                                                  GArray *metaMemArray)
{
   GstGVAJSONMeta* gva_meta = (GstGVAJSONMeta*) meta;

   gchar *message = get_json_message(gva_meta);

   if( message )
   {
      gsize message_size =
#ifndef NO_SAFESTR
            strnlen_s(message, RSIZE_MAX_STR) + 1;
#else
            strlen(message) + 1;
#endif

      GstMemory *mem = gst_memory_new_wrapped((GstMemoryFlags)0,
                                            message,
                                            message_size,
                                            0,
                                            message_size,
                                            NULL,
                                            NULL);

      g_array_append_val(metaMemArray, mem);
   }

   return TRUE;
}

static gboolean gva_json_metaserializer_deserialize(RemoteOffloadMetaSerializer *serializer,
                                         GstBuffer *buffer,
                                         const GArray *metaMemArray)
{
   GstGVAJSONMeta* gva_meta = GST_GVA_JSON_META_ADD(buffer);

   if( metaMemArray->len  )
   {
     GstMemory **gstmems = (GstMemory **)metaMemArray->data;
     GstMapInfo mapInfo;
     if( !gst_memory_map (gstmems[0], &mapInfo, GST_MAP_READ) )
     {
       GST_ERROR_OBJECT (serializer, "Error mapping mem for reading.");
       return FALSE;
     }

     gva_meta->message = g_strdup((gchar *)mapInfo.data);

     gst_memory_unmap(gstmems[0], &mapInfo);
   }

   return TRUE;
}

static void gva_json_metaserializer_interface_init (RemoteOffloadMetaSerializerInterface *iface)
{
   iface->api_name = gva_json_metaserializer_api_name;
   iface->serialize = gva_json_metaserializer_serialize;
   iface->deserialize = gva_json_metaserializer_deserialize;
   iface->allocate_data_segment = NULL;
}

static void
gva_json_metaserializer_class_init (GVAJSONMetaSerializerClass *klass)
{
//   GObjectClass *object_class = G_OBJECT_CLASS (klass);
}

static void
gva_json_metaserializer_init (GVAJSONMetaSerializer *self)
{

}

GVAJSONMetaSerializer *gva_json_metaserializer_new()
{
  return g_object_new(GVAJSONMETASERIALIZER_TYPE, NULL);
}



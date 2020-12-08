/*
 *  gstvideometaserializer.c - GstVideoMetaSerializer object
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
#include "gstvideometaserializer.h"
#include "remoteoffloadmetaserializer.h"

struct _GstVideoMetaSerializer
{
  GObject parent_instance;
};

static void gst_video_metaserializer_interface_init (RemoteOffloadMetaSerializerInterface *iface);

GST_DEBUG_CATEGORY_STATIC (video_metaserializer_debug);
#define GST_CAT_DEFAULT video_metaserializer_debug

G_DEFINE_TYPE_WITH_CODE (
      GstVideoMetaSerializer, gst_video_metaserializer, G_TYPE_OBJECT,
      G_IMPLEMENT_INTERFACE (REMOTEOFFLOADMETASERIALIZER_TYPE,
      gst_video_metaserializer_interface_init)
      GST_DEBUG_CATEGORY_INIT (video_metaserializer_debug, "remoteoffloadvideometaserializer", 0,
      "debug category for GstVideoMetaSerializer"))


typedef struct
{
   GstVideoFrameFlags flags;
   GstVideoFormat     format;
   gint               id;
   guint              width;
   guint              height;
   guint              n_planes;
   guint64            offset[GST_VIDEO_MAX_PLANES];
   gint               stride[GST_VIDEO_MAX_PLANES];
} VideoMetaHeader;

static const gchar* gst_video_metaserializer_api_name(RemoteOffloadMetaSerializer *serializer)
{
   return "GstVideoMetaAPI";
}

static void SerializedDestroy(gpointer data)
{
   g_free(data);
}

static gboolean gst_video_metaserializer_serialize(RemoteOffloadMetaSerializer *serializer,
                                       GstMeta *meta,
                                       GArray *metaMemArray)
{
   GstVideoMeta* video_meta = (GstVideoMeta*) meta;

   //build the VideoROIMetaHeader
   VideoMetaHeader *header = (VideoMetaHeader *)g_malloc(sizeof(VideoMetaHeader));
   header->flags = video_meta->flags;
   header->format = video_meta->format;
   header->id = video_meta->id;
   header->width = video_meta->width;
   header->height = video_meta->height;
   header->n_planes = video_meta->n_planes;
   for( guint i = 0; i < GST_VIDEO_MAX_PLANES; i++ )
   {
      header->offset[i] = video_meta->offset[i];
      header->stride[i] = video_meta->stride[i];
   }

   GstMemory *mem = gst_memory_new_wrapped((GstMemoryFlags)0,
                                           header,
                                            sizeof(VideoMetaHeader),
                                            0,
                                            sizeof(VideoMetaHeader),
                                            header,
                                            SerializedDestroy);

   g_array_append_val(metaMemArray, mem);

   return TRUE;
}


static gboolean gst_video_metaserializer_deserialize(RemoteOffloadMetaSerializer *serializer,
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

   if( mapInfo.size < sizeof(VideoMetaHeader) )
   {
      GST_ERROR_OBJECT (serializer, "Wrong size of serialized data.");
      gst_memory_unmap(gstmems[0], &mapInfo);
      return FALSE;
   }

   VideoMetaHeader *header = (VideoMetaHeader *)mapInfo.data;

   gsize offset[GST_VIDEO_MAX_PLANES];
   for( int i = 0; i < GST_VIDEO_MAX_PLANES; i++ )
   {
      offset[i] = header->offset[i];
   }

   GstVideoMeta *video_meta =
         gst_buffer_add_video_meta_full(buffer, header->flags,
                                        header->format, header->width, header->height,
                                        header->n_planes, offset, header->stride);

   if( video_meta )
   {
      video_meta->id = header->id;
   }
   else
   {
      GST_ERROR_OBJECT (serializer, "Error in gst_buffer_add_video_meta_full");
   }

   gst_memory_unmap(gstmems[0], &mapInfo);

   return TRUE;
}


static void gst_video_metaserializer_interface_init (RemoteOffloadMetaSerializerInterface *iface)
{
  iface->api_name = gst_video_metaserializer_api_name;
  iface->serialize = gst_video_metaserializer_serialize;
  iface->deserialize = gst_video_metaserializer_deserialize;
  iface->allocate_data_segment = NULL;
}

static void
gst_video_metaserializer_class_init (GstVideoMetaSerializerClass *klass)
{
  //GObjectClass *object_class = G_OBJECT_CLASS (klass);

}

static void
gst_video_metaserializer_init (GstVideoMetaSerializer *self)
{

}

GstVideoMetaSerializer *gst_video_metaserializer_new()
{
  return g_object_new(GSTVIDEOMETASERIALIZER_TYPE, NULL);
}

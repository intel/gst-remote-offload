/*
 *  remoteoffloadmetaserializer.c - RemoteOffloadMetaSerializer interface
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

#include "remoteoffloadmetaserializer.h"

G_DEFINE_INTERFACE (RemoteOffloadMetaSerializer, remote_offload_meta_serializer, G_TYPE_OBJECT)

static void
remote_offload_meta_serializer_default_init (RemoteOffloadMetaSerializerInterface *iface)
{
    /* add properties and signals to the interface here */
}

const gchar* remote_offload_meta_api_name(RemoteOffloadMetaSerializer *serializer)
{
   RemoteOffloadMetaSerializerInterface *iface;

   if( !REMOTEOFFLOAD_IS_METASERIALIZER(serializer) ) return "invalid";

   iface = REMOTEOFFLOAD_METASERIALIZER_GET_IFACE(serializer);

   if( !iface->api_name ) return "invalid";

   return iface->api_name(serializer);
}

gboolean remote_offload_meta_serialize(RemoteOffloadMetaSerializer *serializer,
                                       GstMeta *meta,
                                       GArray *metaMemArray)
{
   RemoteOffloadMetaSerializerInterface *iface;

   if( !REMOTEOFFLOAD_IS_METASERIALIZER(serializer) ) return FALSE;

   iface = REMOTEOFFLOAD_METASERIALIZER_GET_IFACE(serializer);

   if( !iface->serialize ) return FALSE;

   return iface->serialize(serializer, meta, metaMemArray);
}


gboolean remote_offload_meta_deserialize(RemoteOffloadMetaSerializer *serializer,
                                         GstBuffer *buffer,
                                         const GArray *metaMemArray)
{
   RemoteOffloadMetaSerializerInterface *iface;

   if( !REMOTEOFFLOAD_IS_METASERIALIZER(serializer) ) return FALSE;

   iface = REMOTEOFFLOAD_METASERIALIZER_GET_IFACE(serializer);

   if( !iface->deserialize ) return FALSE;

   return iface->deserialize(serializer, buffer, metaMemArray);
}


GstMemory *remote_offload_meta_allocate_data_segment(RemoteOffloadMetaSerializer *serializer,
                                                     guint16 metaSegmentIndex,
                                                     guint64 metaSegmentSize,
                                                     const GArray *metaSegmentMemArraySoFar)
{
   RemoteOffloadMetaSerializerInterface *iface;

   if( !REMOTEOFFLOAD_IS_METASERIALIZER(serializer) ) return NULL;

   iface = REMOTEOFFLOAD_METASERIALIZER_GET_IFACE(serializer);

   if( iface->allocate_data_segment )
   {
      return iface->allocate_data_segment(serializer,
                                          metaSegmentIndex,
                                          metaSegmentSize,
                                          metaSegmentMemArraySoFar);
   }
   else
   {
      return gst_allocator_alloc (NULL, metaSegmentSize, NULL);
   }
}

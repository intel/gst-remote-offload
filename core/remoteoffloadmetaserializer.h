/*
 *  remoteoffloadmetaserializer.h - RemoteOffloadMetaSerializer interface
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

#ifndef __REMOTEOFFLOAD_METASERIALIZER_H__
#define __REMOTEOFFLOAD_METASERIALIZER_H__

#include <glib-object.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define REMOTEOFFLOADMETASERIALIZER_TYPE (remote_offload_meta_serializer_get_type ())
G_DECLARE_INTERFACE (RemoteOffloadMetaSerializer,
                     remote_offload_meta_serializer,
                     REMOTEOFFLOAD, METASERIALIZER, GObject)

struct _RemoteOffloadMetaSerializerInterface
{
   GTypeInterface parent_iface;

   //This should be equal to the name returned by
   //g_type_name (meta->info->api)
   //Required to implement!
   const gchar* (*api_name)(RemoteOffloadMetaSerializer *serializer);

   //Given a GstMeta object, populate metaMemArray with one or more
   // serialized chunks of data (GstMemory objects)
   //Required to implement!
   gboolean (*serialize)(RemoteOffloadMetaSerializer *serializer,
                      GstMeta *meta,
                      GArray *metaMemArray);

   //Given one or more serialized chunks of data (GstMemory objects),
   // deserialize them into a GstMeta object, and attach to buffer
   //Required to implement!
   gboolean (*deserialize)(RemoteOffloadMetaSerializer *serializer,
                        GstBuffer *buffer,
                        const GArray *metaMemArray);

   //This is called by BufferExchanger when it needs a GstMemory object to receive into.
   // metaSegmentIndex: The segment index
   // metaSegmentSize: The allocation size needed to transfer into
   // metaSegmentMemArraySoFar: The Meta GstMemory objects that have been read in so far.
   //                           Typically, this has 'metaSegmentIndex' number of entries.
   //Optional to implement.
   GstMemory* (*allocate_data_segment)(RemoteOffloadMetaSerializer *serializer,
                                    guint16 metaSegmentIndex,
                                    guint64 metaSegmentSize,
                                    const GArray *metaSegmentMemArraySoFar);
};

const gchar* remote_offload_meta_api_name(RemoteOffloadMetaSerializer *serializer);

gboolean remote_offload_meta_serialize(RemoteOffloadMetaSerializer *serializer,
                                       GstMeta *meta,
                                       GArray *metaMemArray);


gboolean remote_offload_meta_deserialize(RemoteOffloadMetaSerializer *serializer,
                                         GstBuffer *buffer,
                                         const GArray *metaMemArray);


GstMemory *remote_offload_meta_allocate_data_segment(RemoteOffloadMetaSerializer *serializer,
                                                     guint16 metaSegmentIndex,
                                                     guint64 metaSegmentSize,
                                                     const GArray *metaSegmentMemArraySoFar);



G_END_DECLS

#endif /* __REMOTEOFFLOAD_METASERIALIZER_H__ */

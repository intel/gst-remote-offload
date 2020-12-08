/*
 *  remoteoffloadbinserializer.h - RemoteOffloadBinSerializer object
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
#ifndef __REMOTEOFFLOADBINSERIALIZER_H__
#define __REMOTEOFFLOADBINSERIALIZER_H__
#include <glib-object.h>
#include <gst/gst.h>
#include <gst/base/gstbytewriter.h>

G_BEGIN_DECLS

#define REMOTEOFFLOADBINSERIALIZER_TYPE (remote_offload_bin_serializer_get_type ())
G_DECLARE_FINAL_TYPE (RemoteOffloadBinSerializer,
                      remote_offload_bin_serializer, REMOTEOFFLOAD, BINSERIALIZER, GObject)

RemoteOffloadBinSerializer *remote_offload_bin_serializer_new();

typedef struct _RemoteElementConnectionCandidate
{

   gint32 id;   //the unique channel-id that should be used for the RemoteOffloadCommsChannel
   GstPad *pad; //the pad that should be connected to a remoteoffloadingress or remoteoffloadegress
}RemoteElementConnectionCandidate;

//Given a bin, serialize it.
// bin: input. The bin to be serialized
// memBlocks: output. a GArray of GstMemory*
// remoteconnections: an array of RemoteElementConnectionCandidate's
gboolean remote_offload_serialize_bin(RemoteOffloadBinSerializer *serializer,
                                      GstBin *bin,
                                      GArray **memBlockArray,
                                      GArray **remoteconnections);

//Given a bin, serialize it.
// header: input
// memBlockArray: input. array of GstMemory* objects (i.e. the serialized blocks)
// remoteconnections: output. an array of RemoteElementConnectionCandidate's
GstBin* remote_offload_deserialize_bin(RemoteOffloadBinSerializer *serializer,
                                       GArray *memBlockArray,
                                       GArray **remoteconnections);

G_END_DECLS


#endif /* __REMOTEOFFLOADBINSERIALIZER_H__ */

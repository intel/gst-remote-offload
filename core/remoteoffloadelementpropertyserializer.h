/*
 *  remoteoffloadelementpropertyserializer.h - RemoteOffloadElementPropertySerializer interface
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
#ifndef __REMOTEOFFLOAD_ELEMENTPROPERTY_SERIALIZER_H__
#define __REMOTEOFFLOAD_ELEMENTPROPERTY_SERIALIZER_H__

#include <glib-object.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define REMOTEOFFLOADELEMENTPROPERTYSERIALIZER_TYPE \
   (remote_offload_element_property_serializer_get_type ())
G_DECLARE_INTERFACE (RemoteOffloadElementPropertySerializer,
                     remote_offload_element_property_serializer,
                     REMOTEOFFLOAD, ELEMENTPROPERTYSERIALIZER, GObject)

typedef enum {
  REMOTEOFFLOAD_PROPSERIALIZER_SUCCESS = 0,
  REMOTEOFFLOAD_PROPSERIALIZER_FAILURE,
  REMOTEOFFLOAD_PROPSERIALIZER_DEFER,
} RemoteOffloadPropertySerializerReturn;

struct _RemoteOffloadElementPropertySerializerInterface
{
   GTypeInterface parent_iface;

   RemoteOffloadPropertySerializerReturn (*serialize)(
                         RemoteOffloadElementPropertySerializer *propserializer,
                         GstElement *pElement,
                         const char *propertyName,
                         GArray *memArray);

   RemoteOffloadPropertySerializerReturn (*deserialize)(
                           RemoteOffloadElementPropertySerializer *propserializer,
                           GstElement *pElement,
                           const char *propertyName,
                           GArray *memArray);
};

//Serialize the property given by propertyName, within GstElement pElement into (0 or more) GstMemory blocks,
// which should be appended to memArray.
// Return REMOTEOFFLOAD_PROPSERIALIZER_SUCCESS if the property was successfully serialized
// Return REMOTEOFFLOAD_PROPSERIALIZER_FAILURE if something bad happened
// Return REMOTEOFFLOAD_PROPSERIALIZER_DEFER to defer serialization to "common" element serializer logic
RemoteOffloadPropertySerializerReturn
remote_offload_element_property_serialize(RemoteOffloadElementPropertySerializer *propserializer,
                                          GstElement *pElement,
                                          const char *propertyName,
                                          GArray *memArray);

//Deserialize the property given by propertyName from the (0 or more) GstMemory blocks from memArray.
// The property (or custom behavior) should be set on GstElement pElement.
// Return REMOTEOFFLOAD_PROPSERIALIZER_SUCCESS if the property was successfully deserialized
// Return REMOTEOFFLOAD_PROPSERIALIZER_FAILURE if something bad happened
// Return REMOTEOFFLOAD_PROPSERIALIZER_DEFER to defer deserialization to "common" element deserializer logic
RemoteOffloadPropertySerializerReturn
remote_offload_element_property_deserialize(RemoteOffloadElementPropertySerializer *propserializer,
                                            GstElement *pElement,
                                            const char *propertyName,
                                            GArray *memArray);

G_END_DECLS

#endif /* __REMOTEOFFLOAD_ELEMENTPROPERTY_SERIALIZER_H__ */


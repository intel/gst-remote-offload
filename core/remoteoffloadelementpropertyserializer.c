/*
 *  remoteoffloadelementpropertyserializer.c - RemoteOffloadElementPropertySerializer interface
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


#include "remoteoffloadelementpropertyserializer.h"

G_DEFINE_INTERFACE (RemoteOffloadElementPropertySerializer,
                    remote_offload_element_property_serializer, G_TYPE_OBJECT)

static void
remote_offload_element_property_serializer_default_init
(RemoteOffloadElementPropertySerializerInterface *iface)
{
    /* add properties and signals to the interface here */
}

RemoteOffloadPropertySerializerReturn
remote_offload_element_property_serialize(RemoteOffloadElementPropertySerializer *propserializer,
                                          GstElement *pElement,
                                          const char *propertyName,
                                          GArray *memArray)
{

   RemoteOffloadElementPropertySerializerInterface *iface;

   if( !REMOTEOFFLOAD_IS_ELEMENTPROPERTYSERIALIZER(propserializer) )
      return REMOTEOFFLOAD_PROPSERIALIZER_FAILURE;

   iface = REMOTEOFFLOAD_ELEMENTPROPERTYSERIALIZER_GET_IFACE(propserializer);

   if( !iface->serialize ) return REMOTEOFFLOAD_PROPSERIALIZER_FAILURE;

   return iface->serialize(propserializer, pElement, propertyName, memArray);
}

RemoteOffloadPropertySerializerReturn
remote_offload_element_property_deserialize(RemoteOffloadElementPropertySerializer *propserializer,
                                            GstElement *pElement,
                                            const char *propertyName,
                                            GArray *memArray)
{
   RemoteOffloadElementPropertySerializerInterface *iface;

   if( !REMOTEOFFLOAD_IS_ELEMENTPROPERTYSERIALIZER(propserializer) )
      return REMOTEOFFLOAD_PROPSERIALIZER_FAILURE;

   iface = REMOTEOFFLOAD_ELEMENTPROPERTYSERIALIZER_GET_IFACE(propserializer);

   if( !iface->deserialize ) return REMOTEOFFLOAD_PROPSERIALIZER_FAILURE;

   return iface->deserialize(propserializer, pElement, propertyName, memArray);
}

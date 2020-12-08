/*
 *  remoteoffloadelementserializer.h - RemoteOffloadElementSerializer object
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
#ifndef __REMOTEOFFLOADELEMENTSERIALIZER_H__
#define __REMOTEOFFLOADELEMENTSERIALIZER_H__

#include <glib-object.h>
#include <gst/gst.h>
#include <gst/base/gstbytewriter.h>


G_BEGIN_DECLS

//Total size of an element description is
// sizeof(ElementDescriptionHeader) + ElementDescriptionHeader.properties_description_size

//[ElementDescriptionHeader] (nproperties = 3)
//           ^                [ElementPropertyDescriptionHeader 0]
//           |                            ^
//           |                            |
//           |                   property_size 0
//           |                            |
//           |                            v
//           |                [ElementPropertyDescriptionHeader 1]
//           |                            ^
//           |                            |
//properties_description_size             |
//           |                            |
//           |                   property_size 1
//           |                            |
//           |                            |
//           |                            |
//           |                            v
//           |                [ElementPropertyDescriptionHeader 2]
//           |                            ^
//           |                            |
//           |                   property_size 2
//           |                            |
//           v                            v


//The ElementDescriptionHeader
typedef struct _ElementDescriptionHeader
{
   //gst_element_factory_make(factoryname,elementname)
   gchar factoryname[128];
   gchar elementname[128];
   gint32 id;                //a unique, numeric id given to this element
   gint32 nproperties;       //the number of properties
}ElementDescriptionHeader;


#define REMOTEOFFLOADELEMENTSERIALIZER_TYPE (remote_offload_element_serializer_get_type ())
G_DECLARE_FINAL_TYPE (RemoteOffloadElementSerializer,
                      remote_offload_element_serializer, REMOTEOFFLOAD, ELEMENTSERIALIZER, GObject)

RemoteOffloadElementSerializer *remote_offload_element_serializer_new();


gboolean remote_offload_serialize_element(RemoteOffloadElementSerializer *serializer,
                                          gint32 id,
                                          GstElement *pElement,
                                          ElementDescriptionHeader *headeroutput,
                                          GArray *propMemBlocks);

//Given a binary description, deserialize it into an element
GstElement* remote_offload_deserialize_element(RemoteOffloadElementSerializer *serializer,
                                               const ElementDescriptionHeader *header,
                                               GArray *propMemBlocks);


G_END_DECLS


#endif /* __REMOTEOFFLOADELEMENTSERIALIZER_H__ */

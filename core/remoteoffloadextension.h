/*
 *  remoteoffloadextension.h - RemoteOffloadExtension interface
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
#ifndef __REMOTEOFFLOAD_EXTENSION_H__
#define __REMOTEOFFLOAD_EXTENSION_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define REMOTEOFFLOADEXTENSION_TYPE (remote_offload_extension_get_type ())
G_DECLARE_INTERFACE (RemoteOffloadExtension, remote_offload_extension,
                     REMOTEOFFLOAD, EXTENSION, GObject)

typedef struct _RemoteOffloadExtTypePair
{
   const gchar *name;
   GObject *obj;
}RemoteOffloadExtTypePair;

struct _RemoteOffloadExtensionInterface
{
   GTypeInterface parent_iface;

   //Given a type, generate a GArray of
   // RemoteOffloadExtTypePair's
   GArray *(*generate)(RemoteOffloadExtension *ext, GType type);
};

GArray *remote_offload_extension_generate(RemoteOffloadExtension *ext,
                                          GType type);

//Registry will look for entry point named "remoteoffload_extension_entry".
// It is expected to have this signature.
typedef RemoteOffloadExtension* (*ExtensionEntry) (void);

G_END_DECLS

#endif /* __REMOTEOFFLOAD_EXTENSION_H__ */

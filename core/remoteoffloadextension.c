/*
 *  remoteoffloadextension.c - RemoteOffloadExtension interface
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
#include "remoteoffloadextension.h"

G_DEFINE_INTERFACE (RemoteOffloadExtension, remote_offload_extension, G_TYPE_OBJECT)

static void
remote_offload_extension_default_init (RemoteOffloadExtensionInterface *iface)
{
    /* add properties and signals to the interface here */
}

GArray *remote_offload_extension_generate(RemoteOffloadExtension *ext,
                                          GType type)
{
   RemoteOffloadExtensionInterface *iface;

   if( !REMOTEOFFLOAD_IS_EXTENSION(ext) ) return NULL;

   iface = REMOTEOFFLOAD_EXTENSION_GET_IFACE(ext);

   if( !iface->generate )
     return NULL;

   return iface->generate(ext, type);
}

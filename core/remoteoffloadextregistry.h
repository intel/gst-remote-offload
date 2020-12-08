/*
 *  remoteoffloadextregistry.h - RemoteOffloadExtRegistry interface
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

#ifndef __REMOTE_OFFLOAD_EXT_REGISTRY_H_
#define __REMOTE_OFFLOAD_EXT_REGISTRY_H_

#include <glib-object.h>
#include "remoteoffloadextension.h"

G_BEGIN_DECLS

typedef struct _RemoteOffloadExtRegistry RemoteOffloadExtRegistry;

//Obtain an instance of THE RemoteOffloadExtRegistry.
RemoteOffloadExtRegistry *remote_offload_ext_registry_get_instance();

//Unref the instance of RemoteOffloadExtRegistry.
// NOTE: Use this instead of calling g_object_unref() directly!
void remote_offload_ext_registry_unref(RemoteOffloadExtRegistry *reg);

//Returns a GArray of RemoteOffloadExtTypePair
GArray *remote_offload_ext_registry_generate(RemoteOffloadExtRegistry *reg,
                                             GType type);

G_END_DECLS

#endif /* __REMOTE_OFFLOAD_EXT_REGISTRY_H_ */

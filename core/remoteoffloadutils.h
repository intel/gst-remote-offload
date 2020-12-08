/*
 *  remoteoffloadutils.h - Various utility functions that may be
 *    useful throughout the remote offload stack
 *
 *  Copyright (C) 2020 Intel Corporation
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

#ifndef __REMOTEOFFLOADUTILS_H__
#define __REMOTEOFFLOADUTILS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

//Given a NULL-terminated list of factory names,
// this function will iterate recursively through the
// bin and append elements that have matching factory names
// to the returned GArray.
// Return: GArray of elements (GstElement *)
// It is the callers responsibility to unref / free the returned
// GArray (which in turn will automatically unref the contained elements).
// Note that a valid GArray is returned even in cases where no matching
//  element is found.. it will just be a GArray of size 0.
// NULL is only returned for error cases.
GArray *gst_bin_get_by_factory_type(GstBin *bin, gchar **);

G_END_DECLS

#endif

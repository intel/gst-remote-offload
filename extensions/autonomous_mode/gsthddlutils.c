/*
 *  gsthddlutils.c - Utilities for HDDL elements
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author: Hoe, Sheng Yang <sheng.yang.hoe@intel.com>
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

#include "gsthddlutils.h"

void
gst_hddl_utils_wrap_data_in_buffer (gpointer data, gsize size,
    GstBuffer * buffer)
{
  GstMemory *memory = NULL;

  memory = gst_memory_new_wrapped (0, data, size, 0, size, NULL, NULL);
  gst_buffer_append_memory (buffer, memory);
}

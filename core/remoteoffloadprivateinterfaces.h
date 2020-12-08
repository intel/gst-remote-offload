/*
 *  remoteoffloadprivateinterfaces.h - Private interfaces between comms objects
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

//INTERNAL PROJECT HEADER!
//The following are interfaces used between two specific
// remote offload objects. The reason that we publish them here
// is so that we don't have to clutter public header files
// with functions which the user/application have no business calling.
//This header file should not be installed to some release include dir.
#ifndef __REMOTEOFFLOAD_PRIVATEINTERFACES_H__
#define __REMOTEOFFLOAD_PRIVATEINTERFACES_H__

#include <glib-object.h>
#include "datatransferdefs.h"

G_BEGIN_DECLS

/***************************************************
 * The following interface is used between:
 * RemoteOffloadComms --> RemoteOffloadCommsChannel
 *                     &
 * RemoteOffloadCommsChannel --> RemoteOffloadDataExchanger
 ***************************************************/
#define REMOTEOFFLOADCOMMSCALLBACK_TYPE (remote_offload_comms_callback_get_type ())
G_DECLARE_INTERFACE (RemoteOffloadCommsCallback,
                     remote_offload_comms_callback, REMOTEOFFLOAD, COMMSCALLBACK, GObject)

struct _RemoteOffloadCommsCallbackInterface
{
  GTypeInterface parent_iface;

  GstMemory* (*allocate_data_segment)(RemoteOffloadCommsCallback *callback,
                               guint16 dataTransferType,
                               guint16 segmentIndex,
                               guint64 segmentSize,
                               const GArray *segment_mem_array_so_far);

  void (*data_transfer_received)(RemoteOffloadCommsCallback *callback,
                                 const DataTransferHeader *header,
                                 GArray *segment_mem_array);

  //inform of a communications failure
  void (*comms_failure)(RemoteOffloadCommsCallback *callback);
};

GstMemory* remote_offload_comms_callback_allocate_data_segment(RemoteOffloadCommsCallback *callback,
                               guint16 dataTransferType,
                               guint16 segmentIndex,
                               guint64 segmentSize,
                               const GArray *segment_mem_array_so_far);

void remote_offload_comms_callback_data_transfer_received(RemoteOffloadCommsCallback *callback,
                               const DataTransferHeader *header,
                               GArray *segment_mem_array);

void remote_offload_comms_callback_comms_failure(RemoteOffloadCommsCallback *callback);

G_END_DECLS

#endif /* __REMOTEOFFLOAD_PRIVATEINTERFACES_H__ */

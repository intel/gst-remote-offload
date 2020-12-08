/*
 *  remoteoffloadprivateinterfaces.c - Private interfaces between comms objects
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

#include "remoteoffloadprivateinterfaces.h"

G_DEFINE_INTERFACE (RemoteOffloadCommsCallback, remote_offload_comms_callback, G_TYPE_OBJECT)

static void
remote_offload_comms_callback_default_init (RemoteOffloadCommsCallbackInterface *iface)
{
    /* add properties and signals to the interface here */
}

GstMemory* remote_offload_comms_callback_allocate_data_segment(RemoteOffloadCommsCallback *callback,
                               guint16 dataTransferType,
                               guint16 segmentIndex,
                               guint64 segmentSize,
                               const GArray *segment_mem_array_so_far)
{
  RemoteOffloadCommsCallbackInterface *iface;

  if( !REMOTEOFFLOAD_IS_COMMSCALLBACK(callback) ) return NULL;

  iface = REMOTEOFFLOAD_COMMSCALLBACK_GET_IFACE(callback);

  if( !iface->allocate_data_segment ) return NULL;

  return iface->allocate_data_segment(callback,
                                      dataTransferType,
                                      segmentIndex, segmentSize,
                                      segment_mem_array_so_far);
}

void remote_offload_comms_callback_data_transfer_received(RemoteOffloadCommsCallback *callback,
                               const DataTransferHeader *header,
                               GArray *segment_mem_array)
{
  RemoteOffloadCommsCallbackInterface *iface;

  if( !REMOTEOFFLOAD_IS_COMMSCALLBACK(callback) ) return;

  iface = REMOTEOFFLOAD_COMMSCALLBACK_GET_IFACE(callback);

  if( !iface->data_transfer_received ) return;

  return iface->data_transfer_received(callback, header, segment_mem_array);
}

void remote_offload_comms_callback_comms_failure(RemoteOffloadCommsCallback *callback)
{
   RemoteOffloadCommsCallbackInterface *iface;

   if( !REMOTEOFFLOAD_IS_COMMSCALLBACK(callback) ) return;

   iface = REMOTEOFFLOAD_COMMSCALLBACK_GET_IFACE(callback);

   if( !iface->comms_failure ) return;

   return iface->comms_failure(callback);
}

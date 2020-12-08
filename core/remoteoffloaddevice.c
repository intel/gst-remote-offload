/*
 *  remoteoffloaddevice.c - RemoteOffloadDevice interface
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
#include "remoteoffloaddevice.h"

GST_DEBUG_CATEGORY_STATIC (device_debug);
#define GST_CAT_DEFAULT device_debug

G_DEFINE_INTERFACE_WITH_CODE (RemoteOffloadDevice,
                              remote_offload_device, G_TYPE_OBJECT,
                              GST_DEBUG_CATEGORY_INIT (device_debug,
                                                       "remoteoffloaddevice", 0,
                              "debug category for RemoteOffloadDeviceInterface"))

static void
remote_offload_device_default_init
(RemoteOffloadDeviceInterface *iface)
{
    /* add properties and signals to the interface here */
}

gboolean remote_offload_device_pipeline_modify(RemoteOffloadDevice *device,
                                               GstPipeline *pipeline)
{
   RemoteOffloadDeviceInterface *iface;

   if( !REMOTEOFFLOAD_IS_DEVICE(device) ) return FALSE;

   iface = REMOTEOFFLOAD_DEVICE_GET_IFACE(device);

   if( iface->offload_pipeline_modify )
      return iface->offload_pipeline_modify(device, pipeline);

   //if device doesn't implement this, it's okay, just return TRUE
   // (no modification to the pipeline)
   return TRUE;
}

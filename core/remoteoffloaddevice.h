/*
 *  remoteoffloaddevice.h - RemoteOffloadDevice interface
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
#ifndef __REMOTEOFFLOAD_DEVICE_H__
#define __REMOTEOFFLOAD_DEVICE_H__

#include <glib-object.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define REMOTEOFFLOADDEVICE_TYPE (remote_offload_device_get_type ())
G_DECLARE_INTERFACE (RemoteOffloadDevice,
                     remote_offload_device,
                     REMOTEOFFLOAD, DEVICE, GObject)

struct _RemoteOffloadDeviceInterface
{
   GTypeInterface parent_iface;

   // After "reconstruction" of user's subpipeline, ROP will call
   //  this function, which allows a particular device object to
   //  make device-specific modifications to the pipeline, wherever
   //  applicable.
   gboolean (*offload_pipeline_modify)(RemoteOffloadDevice *device,
                                       GstPipeline *pipeline);
};


gboolean remote_offload_device_pipeline_modify(RemoteOffloadDevice *device,
                                               GstPipeline *pipeline);

G_END_DECLS

#endif /* __REMOTEOFFLOAD_DEVICE_H__ */

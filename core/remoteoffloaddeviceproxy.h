/*
 *  remoteoffloaddeviceproxy.h - RemoteOffloadDeviceProxy interface
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
#ifndef __REMOTEOFFLOAD_DEVICE_PROXY_H__
#define __REMOTEOFFLOAD_DEVICE_PROXY_H__

#include <glib-object.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define REMOTEOFFLOADDEVICEPROXY_TYPE (remote_offload_device_proxy_get_type ())
G_DECLARE_INTERFACE (RemoteOffloadDeviceProxy,
                     remote_offload_device_proxy,
                     REMOTEOFFLOAD, DEVICEPROXY, GObject)

typedef struct _CommsChannelRequest
{
   gint channel_id;
}CommsChannelRequest;

struct _RemoteOffloadDeviceProxyInterface
{
   GTypeInterface parent_iface;

   //required.
   //The remoteoffloadbin will call this function to have the device proxy
   // generate an array of commschannel objects.
   // commschannelrequests is a GArray of CommsChannelRequest's
   GHashTable* (*deviceproxy_generate_commschannels)(RemoteOffloadDeviceProxy *proxy,
                                                     GstBin *bin,
                                                     GArray *commschannelrequests);

   //optional.
   //The remoteoffloadbin will expose a string property to the user which
   // they can use to set custom arguments that can modify the behavior of
   // the device in use.
   gboolean (*deviceproxy_set_arguments)(RemoteOffloadDeviceProxy *proxy,
                                         gchar *arguments_string);

};


//Given a GArray of CommsChannelRequest's,
// return a channel_id(gint) to RemoteOffloadCommsChannel*
GHashTable* remote_offload_deviceproxy_generate(RemoteOffloadDeviceProxy *proxy,
                                                GstBin *bin,
                                                GArray *commschannelrequests);

//The remoteoffloadbin will expose a string property to the user which
// they can use to set custom arguments that can modify the setup
// of the device in use.
gboolean remote_offload_deviceproxy_set_arguments(RemoteOffloadDeviceProxy *proxy,
                                                  gchar *arguments_string);


//utility function to be used by GObject implementing remote_offload_deviceproxy_set_arguments
gboolean remote_offload_deviceproxy_parse_arguments_string(GOptionContext *option_context,
                                                           gchar *arguments_string);


G_END_DECLS

#endif /* __REMOTEOFFLOAD_COMMS_IO_H__ */

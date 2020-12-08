/*
 *  remoteoffloaddeviceproxy.c - RemoteOffloadDeviceProxy interface
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
#include "remoteoffloaddeviceproxy.h"

GST_DEBUG_CATEGORY_STATIC (device_proxy_debug);
#define GST_CAT_DEFAULT device_proxy_debug

G_DEFINE_INTERFACE_WITH_CODE (RemoteOffloadDeviceProxy,
                              remote_offload_device_proxy, G_TYPE_OBJECT,
                              GST_DEBUG_CATEGORY_INIT (device_proxy_debug,
                                                       "remoteoffloaddeviceproxy", 0,
                              "debug category for RemoteOffloadDeviceProxyInterface"))



static void
remote_offload_device_proxy_default_init
(RemoteOffloadDeviceProxyInterface *iface)
{
    /* add properties and signals to the interface here */
}

GHashTable *remote_offload_deviceproxy_generate(RemoteOffloadDeviceProxy *proxy,
                                                GstBin *bin,
                                                GArray *commschannelrequests)
{
   RemoteOffloadDeviceProxyInterface *iface;

   if( !REMOTEOFFLOAD_IS_DEVICEPROXY(proxy) ) return NULL;

   iface = REMOTEOFFLOAD_DEVICEPROXY_GET_IFACE(proxy);

   if( !iface->deviceproxy_generate_commschannels ) return NULL;

   return iface->deviceproxy_generate_commschannels(proxy, bin, commschannelrequests);
}

gboolean remote_offload_deviceproxy_set_arguments(RemoteOffloadDeviceProxy *proxy,
                                                    gchar *arguments_string)
{
   RemoteOffloadDeviceProxyInterface *iface;

   if( !REMOTEOFFLOAD_IS_DEVICEPROXY(proxy) ) return FALSE;

   iface = REMOTEOFFLOAD_DEVICEPROXY_GET_IFACE(proxy);

   if( !iface->deviceproxy_set_arguments ) return TRUE;

   return iface->deviceproxy_set_arguments(proxy, arguments_string);
}

//utility function to be used by GObject implementing remote_offload_commschannels_set_arguments
//TODO: this probably doesn't belong here... move it to some other common utility header
gboolean remote_offload_deviceproxy_parse_arguments_string(GOptionContext *option_context,
                                                           gchar *arguments_string)
{
   if( !option_context || !arguments_string )
   {
      GST_ERROR("!option_context || !arguments_string");
      return FALSE;
   }

   gboolean ret = FALSE;

   gchar *new_arg_string = g_strconcat("dummyarg ", arguments_string, NULL);

   if( new_arg_string )
   {
      gchar **argv = g_strsplit(new_arg_string, " ", -1);
      if( argv )
      {
         gint argc = g_strv_length(argv);

         GError *error = NULL;
         if( g_option_context_parse(option_context, &argc, &argv, &error) )
         {
            ret = TRUE;
         }
         else
         {
            GST_ERROR("g_option_context_parse failed");
            if( error && error->message)
            {
               GST_ERROR("g_option_context_parse failed: %s", error->message);
               g_clear_error (&error);
            }
         }

         g_strfreev(argv);
      }
      else
      {
         GST_ERROR("g_strsplit failed");
      }

      g_free(new_arg_string);
   }
   else
   {
      GST_ERROR("g_strconcat failed");
   }

   return ret;
}

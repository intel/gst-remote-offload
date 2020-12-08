/*
 *  xlinkdeviceproxy.c - XLinkDeviceProxy object
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
#include "xlinkdeviceproxy.h"
#include "remoteoffloaddeviceproxy.h"
#include "remoteoffloadclientserverutil.h"
#include "xlinkchannelmanager.h"
#include <stdio.h>

typedef struct
{
   XLinkChannelManager *pXLinkManager;
   GArray *commsio_array; //GArray of RemoteOffloadCommsIO* that this obj. generated.

   GOptionContext *option_context;
   GArray *option_entries; //array of GOptionEntry's
   gboolean bsharedchannel;

   gchar *user_specified_device;

   XLinkChannelRequestParams request_params;
} XLinkDeviceProxyPrivate;

struct _XLinkDeviceProxy
{
  GObject parent_instance;

  /* Other members, including private data. */
  XLinkDeviceProxyPrivate priv;
};

GST_DEBUG_CATEGORY_STATIC (xlink_device_proxy_debug);
#define GST_CAT_DEFAULT xlink_device_proxy_debug

static void
xlink_device_proxy_interface_init (RemoteOffloadDeviceProxyInterface *iface);

G_DEFINE_TYPE_WITH_CODE (XLinkDeviceProxy, xlink_device_proxy, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (REMOTEOFFLOADDEVICEPROXY_TYPE,
                         xlink_device_proxy_interface_init)
                         GST_DEBUG_CATEGORY_INIT (xlink_device_proxy_debug,
                         "remoteoffloadcommschannelgeneratorxlink", 0,
                         "debug category for XLinkDeviceProxy"))

static GArray* xlink_comms_channel_generate_commsio(XLinkDeviceProxy *self,
                                                    guint nchannels,
                                                    guint32 sw_device_id)
{

   GST_DEBUG_OBJECT (self, "Obtaining instance of XLinkChannelManager");
   if( !self->priv.pXLinkManager )
      self->priv.pXLinkManager = xlink_channel_manager_get_instance();

   if( !self->priv.pXLinkManager )
   {
      GST_ERROR_OBJECT (self, "xlink_channel_manager_get_instance returned NULL");
      return NULL;
   }

   GST_DEBUG_OBJECT (self, "XLinkChannelManager instance obtained");

   return xlink_channel_manager_request_channels(self->priv.pXLinkManager,
                                                 &self->priv.request_params,
                                                 nchannels,
                                                 sw_device_id);
}

//Given a GArray of CommsChannelRequest's,
// return a channel_id(gint) to RemoteOffloadCommsChannel*
static GHashTable* xlink_deviceproxy_generate(RemoteOffloadDeviceProxy *proxy,
                                              GstBin *bin,
                                              GArray *commschannelrequests)
{
   (void)bin;

   if( !DEVICEPROXY_XLINK(proxy) ||
         !commschannelrequests ||
         (commschannelrequests->len < 1) )
      return NULL;

   XLinkDeviceProxy *self = DEVICEPROXY_XLINK (proxy);

   guint32 sw_device_id = 0;
   if( self->priv.user_specified_device )
   {
      if( g_str_has_prefix(self->priv.user_specified_device, "0x") )
      {
         sscanf(self->priv.user_specified_device, "0x%x", &sw_device_id);
      }
      else
      {
         sscanf(self->priv.user_specified_device, "%u", &sw_device_id);
      }

      if( sw_device_id )
      {
         GST_INFO_OBJECT(self, "Using sw_device_id=0x%x", sw_device_id);
      }
   }

   if( !sw_device_id )
   {
      GST_INFO_OBJECT(self, "Choosing first/default PCIe XLink device");
   }

   guint nchannels_to_request = self->priv.bsharedchannel ? 1 : commschannelrequests->len;

   self->priv.commsio_array = xlink_comms_channel_generate_commsio(self,
                                                                   nchannels_to_request,
                                                                   sw_device_id);
   if( !self->priv.commsio_array )
   {
      GST_ERROR_OBJECT (self, "Error in xlink_comms_channel_generate_commsio");
      return NULL;
   }

   CommsChannelRequest *requests = (CommsChannelRequest *)commschannelrequests->data;
   GArray *id_commsio_pair_array = g_array_new(FALSE, FALSE, sizeof(ChannelIdCommsIOPair));

   for( guint requesti = 0; requesti < commschannelrequests->len; requesti++ )
   {

      guint commsio_index = self->priv.bsharedchannel ? 0 : requesti;

      RemoteOffloadCommsIO *commsio = g_array_index(self->priv.commsio_array,
                                                    RemoteOffloadCommsIO *,
                                                    commsio_index);

      ChannelIdCommsIOPair pair = {requests[requesti].channel_id, commsio};
      g_array_append_val(id_commsio_pair_array, pair);
      GST_DEBUG_OBJECT (self, "id_commsio_pair_array[%d] = (%d,%p)",
                       requesti, pair.channel_id, pair.commsio);
   }

   GHashTable *id_to_channel_hash = NULL;
   if( remote_offload_request_new_pipeline(id_commsio_pair_array) )
   {
      id_to_channel_hash = id_commsio_pair_array_to_id_to_channel_hash(id_commsio_pair_array);

      if( !id_to_channel_hash )
      {
         GST_ERROR_OBJECT (self, "Error in id_commsio_pair_array_to_id_to_channel_hash");
      }
   }
   else
   {
      GST_ERROR_OBJECT (self, "Error in remote_offload_request_new_pipeline");
   }

   g_array_free(id_commsio_pair_array, TRUE);

   return id_to_channel_hash;
}

static gboolean
xlink_deviceproxy_set_arguments(RemoteOffloadDeviceProxy *proxy,
                                  gchar *arguments_string)
{
   if( !DEVICEPROXY_IS_XLINK(proxy) )
      return FALSE;

   if( !arguments_string )
      return TRUE;

   XLinkDeviceProxy *self = DEVICEPROXY_XLINK (proxy);

   gboolean ret =
         remote_offload_deviceproxy_parse_arguments_string(self->priv.option_context,
                                                           arguments_string);

   return ret;
}

static void
xlink_device_proxy_interface_init (RemoteOffloadDeviceProxyInterface *iface)
{
   iface->deviceproxy_generate_commschannels = xlink_deviceproxy_generate;
   iface->deviceproxy_set_arguments = xlink_deviceproxy_set_arguments;
}

static void
xlink_device_proxy_finalize (GObject *gobject)
{
  XLinkDeviceProxy *self = DEVICEPROXY_XLINK (gobject);

  if( self->priv.commsio_array )
  {
     for( guint i = 0; i < self->priv.commsio_array->len; i++ )
     {
        RemoteOffloadCommsIO *commsio =
              g_array_index(self->priv.commsio_array,
                            RemoteOffloadCommsIO *,
                            i);
        g_object_unref(commsio);
     }

     g_array_free(self->priv.commsio_array, TRUE);
  }

  if( self->priv.pXLinkManager )
     xlink_channel_manager_unref(self->priv.pXLinkManager);

  g_option_context_free(self->priv.option_context);
  g_array_free(self->priv.option_entries, TRUE);

  G_OBJECT_CLASS (xlink_device_proxy_parent_class)->finalize (gobject);
}

static void
xlink_device_proxy_class_init (XLinkDeviceProxyClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = xlink_device_proxy_finalize;
}

static void
xlink_device_proxy_init (XLinkDeviceProxy *self)
{
   self->priv.pXLinkManager = NULL;
   self->priv.commsio_array = NULL;

   self->priv.option_context = g_option_context_new(" - XLink Comms Channel Generator");
   self->priv.option_entries = g_array_new(FALSE, FALSE, sizeof(GOptionEntry));

   self->priv.bsharedchannel = FALSE;
   GOptionEntry sharedchannel_entry =
     { "sharedchannel", 0, 0, G_OPTION_ARG_NONE,
     &self->priv.bsharedchannel,
     "Share a single XLink channel for all data/control streams", NULL};
   g_array_append_val(self->priv.option_entries, sharedchannel_entry);

   self->priv.request_params.opMode = 0;
   GOptionEntry txmode_entry =
     { "txmode", 0, 0, G_OPTION_ARG_INT,
     &self->priv.request_params.opMode,
     "TX Mode for Opened Channels (0=no preference, 1=blocking, 2=non-blocking", NULL};
   g_array_append_val(self->priv.option_entries, txmode_entry);

   self->priv.request_params.coalesceModeDisable = FALSE;
   GOptionEntry coalescemode_entry =
     { "disable_coalesce_mode", 0, 0, G_OPTION_ARG_NONE,
     &self->priv.request_params.coalesceModeDisable,
     "Disable Coalesce Mode for XLink channel(s)", NULL};
   g_array_append_val(self->priv.option_entries, coalescemode_entry);

   self->priv.user_specified_device = NULL;
   GOptionEntry device_entry =
     { "device", 0, 0, G_OPTION_ARG_STRING,
       &self->priv.user_specified_device,
     "Explicitly select an XLink device.", NULL};
   g_array_append_val(self->priv.option_entries, device_entry);

   GOptionEntry null_entry = { NULL };
   g_array_append_val(self->priv.option_entries, null_entry);

   g_option_context_set_help_enabled(self->priv.option_context, FALSE);
   g_option_context_add_main_entries (self->priv.option_context,
                                      (GOptionEntry*)self->priv.option_entries->data, NULL);
}

XLinkDeviceProxy *xlink_device_proxy_new ()
{
   return g_object_new(XLINKDEVICEPROXY_TYPE, NULL);
}

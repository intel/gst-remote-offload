/*
 *  hddldeviceproxy.cpp - HDDLDeviceProxy object
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

#include <string.h>
#include <unistd.h>
#include <map>
#include <vector>
#include "hddldeviceproxy.h"
#include "remoteoffloaddeviceproxy.h"
#include "xlinkcommschannelcreator.h"
#include "ChannelUtils.h"
#include "WorkloadContext.h"
#include "HddlUnite.h"
#include "RemotePipeline.h"

using namespace HddlUnite;

struct HDDLDeviceProxyPriv
{
  std::vector<ChannelID> deviceChannels;

  GThread *context_thread;
  Device::Ptr device;
  gboolean bdevice_set_done;
  gboolean bdevice_set_okay;
  GMutex devicesetmutex;
  GCond devicesetcond;

  gboolean bthread_close;
  GMutex threadclosemutex;
  GCond threadclosecond;
};

struct _HDDLDeviceProxy
{
  GObject parent_instance;

  /* Other members, including private data. */
  HDDLDeviceProxyPriv *priv;

  //for exposing arguments to user
  GOptionContext *option_context;
  GArray *option_entries; //array of GOptionEntry's

  gchar *user_specified_device;
  gboolean bsharedchannel;
};

GST_DEBUG_CATEGORY_STATIC (hddl_device_proxy_debug);
#define GST_CAT_DEFAULT hddl_device_proxy_debug

static void hddl_device_proxy_interface_init (RemoteOffloadDeviceProxyInterface *iface);

G_DEFINE_TYPE_WITH_CODE (HDDLDeviceProxy, hddl_device_proxy, G_TYPE_OBJECT,
G_IMPLEMENT_INTERFACE (REMOTEOFFLOADDEVICEPROXY_TYPE,
hddl_device_proxy_interface_init)
GST_DEBUG_CATEGORY_INIT (hddl_device_proxy_debug, "remoteoffloaddeviceproxyhddl", 0,
"debug category for HDDLDeviceProxy"))


//Context needs to be created on a dedicated thread per ROB, since the
// workload context is bound to pid/tid, and multiple instances of ROB
// within the same pipeline will call hddl_deviceproxy_generate from the
// same pid/tid combination.
static gpointer hddl_comms_channel_context_thread(HDDLDeviceProxy *self)
{
   WorkloadContext::Ptr wl_context = createWorkloadContext();

   //TODO: Set some context hints to guide device-scheduling,
   // based on user input and/or parsing bin.
   //wl_context->setHint(
   //    WorkloadContext::Hint::SCHEDULE_POLICY_TYPE, SCHEDULE_POLICY_TYPE_ROUNDROBIN);

   gboolean bokay = FALSE;
   WorkloadID workloadId = 0;
   if( wl_context->setContext(workloadId) == HDDL_OK )
   {
      if( registerWorkloadContext(wl_context) == HDDL_OK )
      {
         self->priv->device = wl_context->getDevice();
         bokay = TRUE;
      }
      else
      {
         GST_ERROR_OBJECT(self, "registerWorkloadContext() failed");
      }
   }
   else
   {
      GST_ERROR_OBJECT(self, "WorkloadContext::setContext() failed");
   }

   g_mutex_lock(&self->priv->devicesetmutex);
   self->priv->bdevice_set_done = TRUE;
   self->priv->bdevice_set_okay = bokay;
   g_cond_broadcast (&self->priv->devicesetcond);
   g_mutex_unlock(&self->priv->devicesetmutex);

   //wait for this instance to end
   g_mutex_lock(&self->priv->threadclosemutex);
   while(!self->priv->bthread_close)
      g_cond_wait(&self->priv->threadclosecond, &self->priv->threadclosemutex);
   g_mutex_unlock(&self->priv->threadclosemutex);


   if( workloadId )
   {
      unregisterWorkloadContext(workloadId);
   }

   return NULL;
}

//Given a GArray of CommsChannelRequest's,
// return a channel_id(gint) to RemoteOffloadCommsChannel*
static GHashTable* hddl_deviceproxy_generate(RemoteOffloadDeviceProxy *generator,
                                              GstBin *bin,
                                              GArray *commschannelrequests)
{
   (void)bin;

   if( !DEVICEPROXY_HDDL(generator) ||
         !commschannelrequests ||
         (commschannelrequests->len < 1) )
      return NULL;

   HDDLDeviceProxy *self = DEVICEPROXY_HDDL (generator);

   self->priv = new HDDLDeviceProxyPriv();
   g_mutex_init(&self->priv->devicesetmutex);
   g_cond_init(&self->priv->devicesetcond);
   self->priv->bdevice_set_okay = FALSE;
   self->priv->bdevice_set_done = FALSE;
   self->priv->bthread_close = FALSE;
   g_mutex_init(&self->priv->threadclosemutex);
   g_cond_init(&self->priv->threadclosecond);
   self->priv->context_thread = NULL;

   if( self->user_specified_device )
   {
      GST_INFO_OBJECT(self, "Will attempt to explicitly choose device = %s", self->user_specified_device);

      std::vector<Device> devices;
      if( getAvailableDevices(devices) != HDDL_OK )
      {
         GST_ERROR_OBJECT(self, "getAvailableDevices failed");
         return NULL;
      }

      gboolean bset = FALSE;
      for( auto &device : devices )
      {
         if( g_strcmp0(self->user_specified_device, device.getName().c_str()) == 0)
         {
            bset = TRUE;
            self->priv->device = std::make_shared<Device>(device.getHandle(),
                                                          device.getName(),
                                                          device.getSwDeviceId());
            break;
         }
      }

      if( !bset )
      {
         GST_ERROR_OBJECT(self, "Did not find available device which matches user setting (--device=%s)",
                          self->user_specified_device);
         return NULL;
      }
   }
   else
   {
      self->priv->context_thread =
            g_thread_new("hddl_comms_context",
            (GThreadFunc)hddl_comms_channel_context_thread,
            self);

      //wait for the thread to set up the device.
      gboolean bokay;
      g_mutex_lock(&self->priv->devicesetmutex);
      if( !self->priv->bdevice_set_done )
         g_cond_wait(&self->priv->devicesetcond, &self->priv->devicesetmutex);
      bokay = self->priv->bdevice_set_okay;
      g_mutex_unlock(&self->priv->devicesetmutex);

      if( !bokay )
      {
         GST_ERROR_OBJECT(self, "Context Thread had trouble obtaining device.");
         return NULL;
      }
   }

   CommsChannelRequest *requests = (CommsChannelRequest *)commschannelrequests->data;
   std::map<xlink_channel_id_t, std::vector<gint>> xlinkChannelToCommsChannelIds;

   //if shared channel mode is enabled, we allocate exactly 1 XLink channel,
   // and route all comms channels through this single XLink channel
   if( self->bsharedchannel )
   {
      ChannelID channelId;
      auto ret = allocateChannelUnnamed(self->priv->device->getHandle(),
                                           ChannelType::PCIE_CHANNEL,
                                           &channelId);
      if (ret != HDDL_OK)
      {
        GST_ERROR_OBJECT (self, "Error in allocateChannel");
        return NULL;
      }

      std::vector<gint> commschannels;
      for( guint requesti = 0; requesti < commschannelrequests->len; requesti++ )
      {
         commschannels.push_back(requests[requesti].channel_id);
      }

      xlinkChannelToCommsChannelIds[channelId] = commschannels;
      self->priv->deviceChannels.push_back((xlink_channel_id_t)channelId);
   }
   else
   {
      //otherwise, we allocate a dedicated xlink channel for each requested
      // comms channel.
      for( guint requesti = 0; requesti < commschannelrequests->len; requesti++ )
      {
         ChannelID channelId;
         auto ret = allocateChannelUnnamed(self->priv->device->getHandle(),
                                           ChannelType::PCIE_CHANNEL,
                                           &channelId);
         if (ret != HDDL_OK)
         {
           GST_ERROR_OBJECT (self, "Error in allocateChannel");
           return NULL;
         }

         std::vector<gint> commschannels;
         commschannels.push_back(requests[requesti].channel_id);

         xlinkChannelToCommsChannelIds[channelId] = commschannels;
         self->priv->deviceChannels.push_back((xlink_channel_id_t)channelId);
      }
   }

   //Trigger the ROP instance to start
   uint64_t pipelineId = 0;
   auto launchRet = RemotePipeline::launchRemoteOffloadPipeline(*(self->priv->device),
                                                                self->priv->deviceChannels,
                                                                pipelineId);
   if ( launchRet!= HDDL_OK)
   {
      GST_ERROR_OBJECT (self, "launchRemoteOffloadPipeline() failed, status=%d", (int)launchRet);
      return NULL;
   }

   uint32_t swdevice_id = self->priv->device->getSwDeviceId();

   GST_INFO_OBJECT(self, "launchRemoteOffloadPipeline done, pipelineId = %lu, swdevice_id = 0x%x",
                   pipelineId, swdevice_id);

   GHashTable *id_to_commschannel_hash =
          XLinkCommsChannelsCreate(xlinkChannelToCommsChannelIds, swdevice_id);

   if( !id_to_commschannel_hash )
   {
      GST_ERROR_OBJECT (self, "Error in id_commsio_pair_array_to_id_to_channel_hash");
   }

   return id_to_commschannel_hash;
}

static gboolean
hddl_deviceproxy_set_arguments(RemoteOffloadDeviceProxy *generator,
                                  gchar *arguments_string)
{
   if( !DEVICEPROXY_IS_HDDL(generator) )
      return FALSE;

   if( !arguments_string )
      return TRUE;

   HDDLDeviceProxy *self = DEVICEPROXY_HDDL (generator);

   gboolean ret =
         remote_offload_deviceproxy_parse_arguments_string(self->option_context,
                                                           arguments_string);

   return ret;
}

static void
hddl_device_proxy_interface_init (RemoteOffloadDeviceProxyInterface *iface)
{
   iface->deviceproxy_generate_commschannels = hddl_deviceproxy_generate;
   iface->deviceproxy_set_arguments = hddl_deviceproxy_set_arguments;
}

static void
hddl_device_proxy_finalize (GObject *gobject)
{
  HDDLDeviceProxy *self = DEVICEPROXY_HDDL (gobject);

  if( self->priv )
  {
     //release all of the channels
     for (auto channelId : self->priv->deviceChannels)
     {
        auto ret = releaseChannel(self->priv->device->getHandle(), channelId);
        if (ret == HDDL_OK)
        {
           GST_DEBUG_OBJECT(self, "Released channel for device: %s, channelId: %d.",
                          self->priv->device->getName().c_str(), (gint)channelId);
        }
        else
        {
           GST_ERROR_OBJECT(self, "Release channel for device: %s, channelId: %d failed!",
                                  self->priv->device->getName().c_str(), (gint)channelId);
        }
     }

     if( self->priv->context_thread )
     {
        g_mutex_lock(&self->priv->threadclosemutex);
        self->priv->bthread_close = TRUE;
        g_cond_broadcast (&self->priv->threadclosecond);
        g_mutex_unlock (&self->priv->threadclosemutex);
        g_thread_join (self->priv->context_thread);
     }

     g_mutex_clear(&self->priv->devicesetmutex);
     g_cond_clear(&self->priv->devicesetcond);
     g_mutex_clear(&self->priv->threadclosemutex);
     g_cond_clear(&self->priv->threadclosecond);
     delete self->priv;
  }

  g_option_context_free(self->option_context);
  g_array_free(self->option_entries, TRUE);

  G_OBJECT_CLASS (hddl_device_proxy_parent_class)->finalize (gobject);
}

static void
hddl_device_proxy_class_init (HDDLDeviceProxyClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = hddl_device_proxy_finalize;
}

static void
hddl_device_proxy_init (HDDLDeviceProxy *self)
{
  self->priv = nullptr;
  self->option_context = g_option_context_new(" - HDDL Comms Channel Generator");
  self->option_entries = g_array_new(FALSE, FALSE, sizeof(GOptionEntry));

  self->user_specified_device = NULL;
  GOptionEntry device_entry =
     { "device", 0, 0, G_OPTION_ARG_STRING,
       &self->user_specified_device,
     "Explicitly select a device. Setting this option disables HDDLUnite scheduler", NULL};
   g_array_append_val(self->option_entries, device_entry);

   self->bsharedchannel = FALSE;
   GOptionEntry sharedchannel_entry =
     { "sharedchannel", 0, 0, G_OPTION_ARG_NONE,
     &self->bsharedchannel,
     "Share a single XLink channel for all data/control streams", NULL};
   g_array_append_val(self->option_entries, sharedchannel_entry);

   GOptionEntry null_entry = { NULL };
   g_array_append_val(self->option_entries, null_entry);

   g_option_context_set_help_enabled(self->option_context, FALSE);
   g_option_context_add_main_entries(self->option_context,
                                    (GOptionEntry*)self->option_entries->data, NULL);

}

HDDLDeviceProxy *hddl_device_proxy_new ()
{
   return (HDDLDeviceProxy *)g_object_new(HDDLDEVICEPROXY_TYPE, NULL);
}

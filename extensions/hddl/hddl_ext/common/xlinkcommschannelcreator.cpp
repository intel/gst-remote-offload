/*
 *  xlinkcommschannelcreator.cpp - CommsChannel generator for HDDL client/server extension
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
#include <gst/gst.h>
#include <string.h>
#include <unistd.h>
#include <mutex>
#include <glib-object.h>
#include "xlinkcommschannelcreator.h"
#include "remoteoffloadclientserverutil.h"
#include "remoteoffloadcommsio_xlink.h"
#include "remoteoffloadcommsio.h"

//define a small singleton class to manage our xlink "global" state
class XLinkGlobalState
{
public:
   static XLinkGlobalState& GetInstance()
   {
      static XLinkGlobalState instance;
      return instance;
   }

   struct xlink_handle *GetHandle(uint32_t swDeviceId)
   {
      std::lock_guard<std::mutex> lck(m_mapProtectionMutex);

      std::map<uint32_t, struct xlink_handle *>::iterator it =
            m_swDeviceIdToHandleMap.find(swDeviceId);

      if( it != m_swDeviceIdToHandleMap.end() )
         return it->second;

      struct xlink_handle *pHandle = new struct xlink_handle;
      pHandle->dev_type = HOST_DEVICE;
      pHandle->sw_device_id = swDeviceId;

      if( xlink_connect(pHandle) != X_LINK_SUCCESS )
      {
         GST_ERROR ("xlink_connect failed!");
         delete pHandle;
         return NULL;
      }

      m_swDeviceIdToHandleMap[pHandle->sw_device_id] = pHandle;
      return pHandle;
   }

private:
   XLinkGlobalState()
   {
      if( xlink_initialize() == X_LINK_SUCCESS )
      {
         m_bInitialized = true;
      }
      else
      {
         GST_ERROR ("xlink_initialize failed!");
      }
   }

   ~XLinkGlobalState()
   {
      for( auto &it : m_swDeviceIdToHandleMap )
      {
         if( xlink_disconnect(it.second) != X_LINK_SUCCESS )
         {
            GST_ERROR ("xlink_disconnect failed!");
         }

         delete it.second;
      }
   }

   XLinkGlobalState(const XLinkGlobalState&) = delete;
   XLinkGlobalState& operator=(const XLinkGlobalState&) = delete;
   XLinkGlobalState(XLinkGlobalState&&) = delete;
   XLinkGlobalState& operator=(XLinkGlobalState&&) = delete;

   std::map<uint32_t, struct xlink_handle *> m_swDeviceIdToHandleMap;
   std::mutex m_mapProtectionMutex;

   bool m_bInitialized = false;;
};


//Called by the ROP instance (server-side).
// Create a GHashTable of commschannel-id (gint)-to-RemoteOffloadCommsChannel*
//  from a vector of xlink channels.
// Note that the commschannel-id's for each xlink-channel are not yet
//  known, so this function takes care of the internal logic to
//  create & receive the comms-channel id from the client-side, for each
//  channel created.
GHashTable *
XLinkCommsChannelsCreate(const std::vector<xlink_channel_id_t> &xlink_channels,
                         uint32_t swdevice_id)
{
   GHashTable *id_to_commschannel_hash = NULL;
   bool bokay = true;
   struct xlink_handle *handle = XLinkGlobalState::GetInstance().GetHandle(swdevice_id);

   std::vector<RemoteOffloadCommsIO *> commsio_vector;
   GArray *id_commsio_pair_array = g_array_new(FALSE,
                                               FALSE,
                                               sizeof(ChannelIdCommsIOPair));

   //For each xlink channel...
   for( std::size_t i = 0; i < xlink_channels.size(); i++ )
   {
      xlink_channel_id_t xlink_channel = xlink_channels[i];

      //create a new commsio object
      RemoteOffloadCommsXLinkIO *pCommsIOXLink =
         remote_offload_comms_io_xlink_new(handle,
                                           xlink_channel,
                                           XLINK_COMMSIO_TXMODE_NOPREFERENCE,
                                           XLINK_COMMSIO_COALESCEMODE_ENABLE);
      if( !pCommsIOXLink )
      {
         GST_ERROR("Error creating RemoteOffloadCommsXLinkIO for xlink channel=%d", xlink_channel);
         bokay = false;
         break;
      }

      RemoteOffloadCommsIO *pCommsIO = (RemoteOffloadCommsIO *)pCommsIOXLink;
      commsio_vector.push_back(pCommsIO);

      //We need to receive the number of comms channel's that will funnel through this
      // single commsio object.
      GST_DEBUG("Receiving number of channel_id's for XLink channel=%d(commsio=%p)",
                xlink_channels[i], pCommsIOXLink);

      guint64 num_channel_ids = 0;
      if( remote_offload_comms_io_read(pCommsIO,
                                       (guint8 *)&num_channel_ids,
                                       sizeof(num_channel_ids)) != REMOTEOFFLOADCOMMSIO_SUCCESS )
      {
         GST_ERROR("Error in remote_offload_comms_io_read for XLink channel=%d(commsio=%p)",
                   xlink_channel, pCommsIO);
         bokay = false;
         break;
      }

      GST_DEBUG("Number of channel_id's being requested for XLink channel=%d(commsio=%p) = %lu",
                xlink_channel, pCommsIO, num_channel_ids);

      if( num_channel_ids )
      {
         for( guint64 channelidi = 0; channelidi < num_channel_ids; channelidi++ )
         {
            GST_DEBUG("Receiving comms channel index %lu for XLink channel=%d(commsio=%p)",
                      channelidi, xlink_channel, pCommsIO);

            gint channel_id;
            if( remote_offload_comms_io_read(pCommsIO,
                                             (guint8 *)&channel_id,
                                             sizeof(channel_id)) != REMOTEOFFLOADCOMMSIO_SUCCESS )
            {
               GST_ERROR("Error in remote_offload_comms_io_read for XLink channel=%d(commsio=%p)",
                         xlink_channels[i], pCommsIO);
               bokay = false;
               break;
            }

            GST_DEBUG("comms channel index %lu for XLink channel=%d(commsio=%p) is %d",
                      channelidi, xlink_channel, pCommsIO, channel_id);

            ChannelIdCommsIOPair pair = {channel_id,
                                         pCommsIO};

            g_array_append_val(id_commsio_pair_array, pair);
         }

         if( !bokay )
            break;
      }
      else
      {
         GST_ERROR("Number of comm channels requested for this xlink channel is 0");
         bokay = false;
         break;
      }
   }

   if( bokay )
   {
      //Convert the GArray of ChannelIdCommsIOPair's to a GHashTable
      // of (gint)-to-RemoteOffloadCommsChannel*
      // (basically std::map<gint, RemoteOffloadCommsChannel*>)
      id_to_commschannel_hash =
             id_commsio_pair_array_to_id_to_channel_hash(id_commsio_pair_array);

      if( !id_to_commschannel_hash )
      {
         GST_ERROR("Error in id_commsio_pair_array_to_id_to_channel_hash()");
         bokay = false;
      }
   }

   g_array_free(id_commsio_pair_array, TRUE);

   for( auto &commsio : commsio_vector )
   {
      g_object_unref((GObject *)commsio);
   }

   return id_to_commschannel_hash;
}

//Called by the HDDL Channel Generator (client-side).
// Create a GHashTable of commschannel-id (gint)-to-RemoteOffloadCommsChannel*
//  from a map of (xlink channel)-to-(vector of comms channel id's).
// Note that this function takes care of the internal logic to create & send
//  the comms-channel id to the xlink channels opened on the server side.
GHashTable *
XLinkCommsChannelsCreate(const std::map<xlink_channel_id_t,
                         std::vector<gint>> &xlink_to_commschannels,
                         uint32_t swdevice_id)
{
   GHashTable *id_to_commschannel_hash = NULL;
   bool bokay = true;
   struct xlink_handle *handle = XLinkGlobalState::GetInstance().GetHandle(swdevice_id);
   if( !handle )
   {
      GST_ERROR ("XLinkGlobalState::GetHandle(%u) failed", swdevice_id);
      return NULL;
   }

   std::vector<RemoteOffloadCommsIO *> commsio_vector;
   GArray *id_commsio_pair_array = g_array_new(FALSE, FALSE, sizeof(ChannelIdCommsIOPair));
   for( auto pair : xlink_to_commschannels )
   {
      if( !pair.second.empty() )
      {
         //create an XLink CommsIO object
         RemoteOffloadCommsXLinkIO *pCommsIOXLink =
            remote_offload_comms_io_xlink_new(handle,
                                              pair.first,
                                              XLINK_COMMSIO_TXMODE_NOPREFERENCE,
                                              XLINK_COMMSIO_COALESCEMODE_ENABLE);
         if( !pCommsIOXLink )
         {
            GST_ERROR ("remote_offload_comms_io_xlink_new failed for id=%d", pair.first);
            bokay = false;
            break;
         }

         commsio_vector.push_back((RemoteOffloadCommsIO *)pCommsIOXLink);

         //send the number of comms channel id's that are mapped through this XLink channel
         guint64 num_channel_ids = (guint64)pair.second.size();
         if( remote_offload_comms_io_write((RemoteOffloadCommsIO *)pCommsIOXLink,
                                           (guint8 *)&(num_channel_ids),
                                           sizeof(num_channel_ids)) != REMOTEOFFLOADCOMMSIO_SUCCESS)
         {
            GST_ERROR ( "remote_offload_comms_io_write(%lu) failed for XLinkChannel=%d", num_channel_ids, pair.first);
            g_object_unref(pCommsIOXLink);
            bokay = false;
            break;
         }

         //for each entry in the comms channel id's vector, send the id.
         for( guint64 channelidi = 0; channelidi < num_channel_ids; channelidi++ )
         {
            gint comms_channel_id = pair.second[channelidi];

            //send the channel-id
            if( remote_offload_comms_io_write((RemoteOffloadCommsIO *)pCommsIOXLink,
                                              (guint8 *)&(comms_channel_id),
                                              sizeof(comms_channel_id)) != REMOTEOFFLOADCOMMSIO_SUCCESS)
            {
               GST_ERROR ( "remote_offload_comms_io_write(%d) failed for XLinkChannel=%d", comms_channel_id, pair.first);
               g_object_unref(pCommsIOXLink);
               bokay = false;
               break;
            }

            ChannelIdCommsIOPair chanidcommspair = {comms_channel_id, (RemoteOffloadCommsIO *)pCommsIOXLink};
            g_array_append_val(id_commsio_pair_array, chanidcommspair);
         }

         if( !bokay )
            break;
      }
      else
      {
         GST_ERROR ("vector of comms channel id's is empty");
         bokay = false;
      }
   }

   if( bokay )
   {
      id_to_commschannel_hash =
          id_commsio_pair_array_to_id_to_channel_hash(id_commsio_pair_array);

      if( !id_to_commschannel_hash )
      {
         GST_ERROR("Error in id_commsio_pair_array_to_id_to_channel_hash");
         bokay = false;
      }
   }

   for( auto &commsio : commsio_vector )
   {
      g_object_unref((GObject *)commsio);
   }

   g_array_free(id_commsio_pair_array, TRUE);

   return id_to_commschannel_hash;
}


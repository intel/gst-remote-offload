/*
 *  xlinkcommschannelcreator.h - CommsChannel generator for HDDL client/server extension
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
#ifndef __XLINK_COMMSCHANNEL_CREATOR_H__
#define __XLINK_COMMSCHANNEL_CREATOR_H__

#include <vector>
#include <map>

typedef uint16_t xlink_channel_id_t;
typedef int gint;
typedef struct _GHashTable  GHashTable;

//Called by the ROP instance (server-side).
// Create a GHashTable of commschannel-id (gint)-to-RemoteOffloadCommsChannel*
//  from a vector of xlink channels.
// Note that the commschannel-id's for each xlink-channel are not yet
//  known, so this function takes care of the internal logic to
//  create & receive the comms-channel id from the client-side, for each
//  channel created.
GHashTable *
XLinkCommsChannelsCreate(const std::vector<xlink_channel_id_t> &xlink_channels,
                         uint32_t swdevice_id);

//Called by the HDDL Channel Generator (client-side).
// Create a GHashTable of commschannel-id (gint)-to-RemoteOffloadCommsChannel*
//  from a map of xlink_channel-to-commschannel-id.
// Note that this function takes care of the internal logic to create & send
//  the comms-channel id to the xlink channels opened on the server side.
GHashTable *
XLinkCommsChannelsCreate(const std::map<xlink_channel_id_t,
                         std::vector<gint>> &xlink_to_commschannels,
                         uint32_t swdevice_id);

#endif /* __XLINK_COMMSCHANNEL_CREATOR_H__ */

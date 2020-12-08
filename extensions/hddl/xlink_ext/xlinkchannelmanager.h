/*
 *  xlinkchannelmanager.h - XLinkChannelManager object
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

#ifndef __XLINK_CHANNEL_MANAGER_H__
#define __XLINK_CHANNEL_MANAGER_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _XLinkChannelManager XLinkChannelManager;

//Obtain an instance of THE XLinkChannelManager.
XLinkChannelManager *xlink_channel_manager_get_instance();

//Unref the instance of XLinkChannelManager.
// NOTE: Use this instead of calling g_object_unref() directly!
void xlink_channel_manager_unref(XLinkChannelManager *manager);

typedef struct _RemoteOffloadCommsIO RemoteOffloadCommsIO;

//Called by the server. Will listen for new XLink connections(s) on
// the default channel. It will negotiate N new connections and return
// a GArray of RemoteOffloadCommsIO objects, one for each new connection.
// Returns NULL for failure.
GArray *xlink_channel_manager_listen_channels(XLinkChannelManager *manager,
                                              guint32 sw_device_id);

typedef struct _XLinkChannelRequestParams
{
  guint opMode; //0 = no preference, 1 = RXB_TXN, 2 = RXB_TXB
  gboolean coalesceModeDisable;
}XLinkChannelRequestParams;

//Called by the IA host to request N RemoteOffloadCommsIO's.
// Return GArray of RemoteOffloadCommsIO, or NULL for failure
GArray* xlink_channel_manager_request_channels(XLinkChannelManager *manager,
                                               XLinkChannelRequestParams *params,
                                               guint nchannels,
                                               guint32 sw_device_id);

void xlink_channel_manager_close_default_channels(XLinkChannelManager *manager);

G_END_DECLS

#endif /* __REMOTEOFFLOAD_COMMS_IO_XLINK_H__ */

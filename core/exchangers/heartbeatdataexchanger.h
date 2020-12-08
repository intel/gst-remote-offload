/*
 *  heartbeatdataexchanger.h - HeartBeatDataExchanger object
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

#ifndef __REMOTEOFFLOADHEARTBEATEXCHANGER_H__
#define __REMOTEOFFLOADHEARTBEATEXCHANGER_H__

#include "remoteoffloaddataexchanger.h"

G_BEGIN_DECLS

#define HEARTBEATDATAEXCHANGER_TYPE (heartbeat_data_exchanger_get_type ())
G_DECLARE_FINAL_TYPE(HeartBeatDataExchanger,
                     heartbeat_data_exchanger, DATAEXCHANGER, HEARTBEAT, RemoteOffloadDataExchanger);

typedef struct _HeartBeatDataExchangerCallback
{
   //notification that we've lost the heartbeat.
   void (*flatline)(void *priv);
   void *priv;
}HeartBeatDataExchangerCallback;

HeartBeatDataExchanger *heartbeat_data_exchanger_new (RemoteOffloadCommsChannel *channel,
                                          HeartBeatDataExchangerCallback *callback);

//start monitoring heartbeat of remote object, asynchronously.
// In the case that the heartbeat is not found or lost,
// 'flatline' callback will be invoked.
// Returns TRUE if we are able to successfully start monitoring
// the heartbeat (i.e. we were able to spawn a thread successfully).
// So keep in mind that a return value of TRUE here doesn't
// imply that the heartbeat was initially detected.
gboolean heartbeat_data_exchanger_start_monitor(HeartBeatDataExchanger *hbexchanger);

G_END_DECLS

#endif

/*
 *  remoteoffloadcommschannel.h - RemoteOffloadCommsChannel object
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

#ifndef __REMOTEOFFLOADCOMMSCHANNEL_H__
#define __REMOTEOFFLOADCOMMSCHANNEL_H__

#include <glib-object.h>
#include <gst/gstmemory.h>
#include "datatransferdefs.h"

G_BEGIN_DECLS

#define REMOTEOFFLOADCOMMSCHANNEL_TYPE (remote_offload_comms_channel_get_type ())
G_DECLARE_FINAL_TYPE (RemoteOffloadCommsChannel,
                      remote_offload_comms_channel, REMOTEOFFLOAD, COMMSCHANNEL, GObject)


typedef struct _RemoteOffloadComms RemoteOffloadComms;

//Create a new comms channel
// The comms channel instance will internally register itself with the passed in comms object
//  during construction. There is no need to do that separately.
RemoteOffloadCommsChannel *
remote_offload_comms_channel_new(RemoteOffloadComms *comms, gint channel_id);

//Obtain the RemoteOffloadComms for which this channel is associated with
RemoteOffloadComms *remote_offload_comms_channel_get_comms(RemoteOffloadCommsChannel *channel);

//The following functions should be called exclusively from RemoteOffloadDataExchanger.
// TODO: Move these to a "private" interface between DataExchanger & CommsChannel

typedef struct _RemoteOffloadResponse RemoteOffloadResponse;
gboolean remote_offload_comms_channel_write(RemoteOffloadCommsChannel *channel,
                                            DataTransferHeader *header,
                                            GList *mem_list,
                                            RemoteOffloadResponse *response);

gboolean remote_offload_comms_channel_write_response(RemoteOffloadCommsChannel *channel,
                                                     GList *mem_list_response,
                                                     guint64 response_id);


typedef struct _RemoteOffloadDataExchanger RemoteOffloadDataExchanger;

//Register a data exchanger with a channel
gboolean remote_offload_comms_channel_register_exchanger(RemoteOffloadCommsChannel *channel,
                                                         RemoteOffloadDataExchanger *exchanger);

//Unregister a data exchanger. This will block until all current exchanger tasks are flushed
// from the queue.
gboolean remote_offload_comms_channel_unregister_exchanger(RemoteOffloadCommsChannel *channel,
                                                           RemoteOffloadDataExchanger *exchanger);

//Query consumable / producible memory features for this comms channel.
GList *remote_offload_comms_channel_get_consumable_memfeatures(RemoteOffloadCommsChannel *channel);
GList *remote_offload_comms_channel_get_producible_memfeatures(RemoteOffloadCommsChannel *channel);

//1. All calls to comms_channel_write / comms_channel_write response will fail
//   after this is called.
//2. This will trigger all responses that are currently being waited on to return.
void remote_offload_comms_channel_cancel_all(RemoteOffloadCommsChannel *channel);

// In addition to performing putting channel into a cancelled state, this method
//  will additionally put the lower-layer comms into an 'error' state, which
//  should be done when comms objects are suspected of being in a bad state.
void remote_offload_comms_channel_error_state(RemoteOffloadCommsChannel *channel);

//This should be called by ROB/ROP when it is known that there should be no further
// reads/writes through this channel.
void remote_offload_comms_channel_finish(RemoteOffloadCommsChannel *pCommsChannel);

//set comms_failure callback. This will be invoked by a CommsChannel object when it detects
// that a fatal comms failure has occurred on this channel. This is to inform that any further
// writes / reads on this channel will not be possible.
typedef void (*comms_failure_callback_f)(RemoteOffloadCommsChannel *channel, void *user_data);
void remote_offload_comms_channel_set_comms_failure_callback(RemoteOffloadCommsChannel *channel,
                                                             comms_failure_callback_f callback,
                                                             void *user_data);



G_END_DECLS
#endif

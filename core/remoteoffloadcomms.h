/*
 *  remoteoffloadcomms.h - RemoteOffloadComms object
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

#ifndef __REMOTEOFFLOADCOMMS_H__
#define __REMOTEOFFLOADCOMMS_H__

#include <glib-object.h>
#include <gst/gstmemory.h>
#include "remoteoffloadcommsio.h"
#include "datatransferdefs.h"

G_BEGIN_DECLS

#define REMOTEOFFLOADCOMMS_TYPE (remote_offload_comms_get_type ())
G_DECLARE_FINAL_TYPE (RemoteOffloadComms, remote_offload_comms, REMOTEOFFLOAD, COMMS, GObject)


//public interfaces
RemoteOffloadComms *remote_offload_comms_new(RemoteOffloadCommsIO *pcommsio);

//The following functions should be called exclusively from RemoteOffloadCommsChannel.
// memList is a GList of GstMemory*
RemoteOffloadCommsIOResult remote_offload_comms_write(RemoteOffloadComms *comms,
                                                      DataTransferHeader *pheader,
                                                      GList *memList);

// Called when all messages are done being sent. This triggers closure of the
//  remote comms reader thread.
void remote_offload_comms_finish(RemoteOffloadComms *comms);

typedef struct _RemoteOffloadCommsChannel RemoteOffloadCommsChannel;
gboolean remote_offload_comms_register_channel(RemoteOffloadComms *comms,
                                               RemoteOffloadCommsChannel *channel);

GList *remote_offload_comms_get_consumable_memfeatures(RemoteOffloadComms *comms);
GList *remote_offload_comms_get_producible_memfeatures(RemoteOffloadComms *comms);

void remote_offload_comms_error_state(RemoteOffloadComms *comms);


G_END_DECLS

#endif /* __REMOTEOFFLOADCOMMS_H__ */

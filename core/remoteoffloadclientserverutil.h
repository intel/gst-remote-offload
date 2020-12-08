/*
 *  remoteoffloadclientserverutil.h - Utility objects & types for establishing
 *       connections between a client(the host) & server (the remote target)
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

#ifndef _REMOTEOFFLOAD_CLIENTSERVER_UTIL_H_
#define _REMOTEOFFLOAD_CLIENTSERVER_UTIL_H_

#include <glib-object.h>

G_BEGIN_DECLS


typedef struct _RemoteOffloadCommsIO RemoteOffloadCommsIO;

//CLIENT SIDE - The following GObjects / utility functions are intended to be called by
// host (i.e. remoteoffloadbin)
typedef struct _ChannelIdCommsIOPair
{
   gint channel_id;
   RemoteOffloadCommsIO *commsio;
}ChannelIdCommsIOPair;

//Request that the server spawns a pipeline with the following channel_id / commsio pairs
// commsio: The direct connection to the server for which to send this request through.
//          Note: The commsio objects contained within the pair array must
//                not already be "attached" to a comms object.
// Returns TRUE if the remote pipeline has successfully been started.
gboolean remote_offload_request_new_pipeline(GArray *id_commsio_pair_array);

//COMMON - The following GObjects / utility functions are intended to be called by
// both the server & client

//Converts a id_commsio_pair_array (GArray) to id_to_channel_hash (GHashTable)
GHashTable *id_commsio_pair_array_to_id_to_channel_hash(GArray *id_commsio_pair_array);

//SERVER SIDE - The following GObjects / utility functions are intended to be called by
// gstremoteoffload servers
#define REMOTEOFFLOADPIPELINESPAWNER_TYPE (remote_offload_pipeline_spawner_get_type ())
G_DECLARE_FINAL_TYPE (RemoteOffloadPipelineSpawner,
                      remote_offload_pipeline_spawner, REMOTEOFFLOAD, PIPELINESPAWNER, GObject)


RemoteOffloadPipelineSpawner *remote_offload_pipeline_spawner_new();

//Custom function to be used to spawn remoteoffloadpipeline from a GArray of ChannelIdCommsIOPair's
typedef void (*remoteoffloadpipeline_spawn_func)(GArray *id_commsio_pair_array, void *user_data);

void remote_offload_pipeline_set_callback(RemoteOffloadPipelineSpawner *spawner,
                                          remoteoffloadpipeline_spawn_func callback,
                                          void *user_data);


gboolean remote_offload_pipeline_spawner_add_connection(RemoteOffloadPipelineSpawner *spawner,
                                                        RemoteOffloadCommsIO *commsio);




G_END_DECLS

#endif

/*
 *  gstremoteoffloadpipeline.h - RemoteOffloadPipeline object
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
#ifndef _GSTREMOTEOFFLOADPIPELINE_H_
#define _GSTREMOTEOFFLOADPIPELINE_H_

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define REMOTEOFFLOADPIPELINE_TYPE (remote_offload_pipeline_get_type ())
G_DECLARE_FINAL_TYPE (RemoteOffloadPipeline, remote_offload_pipeline,
                      REMOTEOFFLOAD, PIPELINE, GObject)

typedef struct _RemoteOffloadDevice RemoteOffloadDevice;
//Create a new instance of a remote offload pipeline
// device (optional): Device object that can implement customized behavior, by
//  using various callbacks from ROP for startup, error handling, and shutdown.
// Use NULL if this server needs no customization.
// id_to_channel_hash (required): a gint32 (channel-id) to RemoteOffloadCommsChannel hash.
// At the very least, there must exist an entry for id=0 to use as the "default"
//  channel for direct communication with remoteoffloadbin.
RemoteOffloadPipeline *remote_offload_pipeline_new (RemoteOffloadDevice *device,
                                                    GHashTable *id_to_channel_hash);

//execute the remote offload pipeline instance
gboolean remote_offload_pipeline_run(RemoteOffloadPipeline *remoteoffloadpipeline);

G_END_DECLS

#endif

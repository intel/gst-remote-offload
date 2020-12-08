/*
 *  remoteoffloadbinpipelinecommon.h - Utilities & types shared between
 *         remoteoffloadbin & remoteoffloadpipeline objects
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
#ifndef _REMOTEOFFLOADBINPIPELINECOMMON_H_
#define _REMOTEOFFLOADBINPIPELINECOMMON_H_

#include <gst/gst.h>

typedef enum
{
  BINPIPELINE_EXCHANGE_ROPREADY = 0x100,
  BINPIPELINE_EXCHANGE_ROPINSTANCEPARAMS,
  BINPIPELINE_EXCHANGE_BINSERIALIZATION,
  BINPIPELINE_EXCHANGE_LOGMESSAGE
}BinPipelineGenericTransferCodes;

typedef struct _RemoteOffloadComms RemoteOffloadComms;
gboolean AssembleRemoteConnections(GstBin *bin,
                                   GArray *remoteconnectioncandidates,
                                   GHashTable *id_to_channel_hash);

/**
 * RemoteOffloadLogMode:
 * @REMOTEOFFLOAD_LOG_DISABLED: No Remote Logging / transfer of log
 *                              messages from ROP to ROB.
 *
 * @REMOTEOFFLOAD_LOG_RING: Remote logging into ring buffer(s).
 *                          Log contents are sent upon ring buffer being
 *                          filled to capacity and/or closure of the
 *                          remote offload pipeline (i.e. READY->NULL)
 *
 * @REMOTEOFFLOAD_LOG_IMMEDIATE: For each log message received by the
 *                               remote offload pipeline, it is immediately
 *                               sent to the host. (only recommended for
 *                               extreme debugging scenarios)
 *
 * Method for controlling frequency of log message transfer
 */
typedef enum
{
  REMOTEOFFLOAD_LOG_DISABLED  = 0,
  REMOTEOFFLOAD_LOG_RING     = 1,
  REMOTEOFFLOAD_LOG_IMMEDIATE = 2,
}RemoteOffloadLogMode;

#define ROP_INSTANCEPARAMS_GST_DEBUG_STRINGSIZE 256

//Parameters send by ROB to new instance of ROP
typedef struct _RemoteOffloadInstanceParams
{
  gint32 logmode;
  gint32 gst_debug_set;
  gchar gst_debug[ROP_INSTANCEPARAMS_GST_DEBUG_STRINGSIZE];
}RemoteOffloadInstanceParams;


#endif

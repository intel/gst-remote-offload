/*
 *  remoteoffloadpipelinelogger.h - RemoteOffloadPipelineLogger object
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
#ifndef __REMOTE_OFFLOAD_PIPELINE_LOGGER_H__
#define __REMOTE_OFFLOAD_PIPELINE_LOGGER_H__

#include <glib-object.h>
#include "remoteoffloadbinpipelinecommon.h"
#include "genericdataexchanger.h"

typedef struct _RemoteOffloadPipelineLogger RemoteOffloadPipelineLogger;

//Obtain an instance of THE RemoteOffloadPipelineLogger.
RemoteOffloadPipelineLogger*  rop_logger_get_instance();

//Unref the instance of RemoteOffloadPipelineLogger.
// NOTE: Use this instead of calling g_object_unref() directly!
void rop_logger_unref(RemoteOffloadPipelineLogger *logger);

//Register an exchanger to start capturing
// & sending log contents for a particular ROP instance.
gboolean rop_logger_register(RemoteOffloadPipelineLogger *logger,
                             GenericDataExchanger *exchanger,
                             RemoteOffloadLogMode mode,
                             const char *gst_debug);

//Unregister an exchanger (particular ROP instance).
// Note that un-sent log contents will be flushed within
// this routine.
gboolean rop_logger_unregister(RemoteOffloadPipelineLogger *logger,
                               GenericDataExchanger *exchanger);



#endif /* __REMOTE_OFFLOAD_PIPELINE_LOGGER_H__ */

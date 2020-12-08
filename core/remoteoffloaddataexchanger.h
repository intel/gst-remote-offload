/*
 *  remoteoffloaddataexchanger.h - RemoteOffloadDataExchanger object
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

#ifndef __REMOTEOFFLOADDATAEXCHANGER_H__
#define __REMOTEOFFLOADDATAEXCHANGER_H__

#include <gst/gst.h>
#include "remoteoffloadresponse.h"

G_BEGIN_DECLS

#define REMOTEOFFLOADDATAEXCHANGER_TYPE (remote_offload_data_exchanger_get_type ())
G_DECLARE_DERIVABLE_TYPE (RemoteOffloadDataExchanger,
                          remote_offload_data_exchanger, REMOTEOFFLOAD, DATAEXCHANGER, GObject)

struct _RemoteOffloadDataExchangerClass
{
   GObjectClass parent_class;

   //optional virtual function
   GstMemory* (*allocate_data_segment)(RemoteOffloadDataExchanger *exchanger,
                                       guint16 segmentIndex,
                                       guint64 segmentSize,
                                       const GArray *segment_mem_array_so_far);

   //required virtual function
   gboolean (*received)(RemoteOffloadDataExchanger *exchanger,
                         const GArray *segment_mem_array,
                         guint64 response_id);
};




//called by child classes

gboolean remote_offload_data_exchanger_write(RemoteOffloadDataExchanger *exchanger,
                                             GList *mem_list,
                                             RemoteOffloadResponse *pResponse);

gboolean remote_offload_data_exchanger_write_single(RemoteOffloadDataExchanger *exchanger,
                                                    guint8 *data,
                                                    gsize size,
                                                    RemoteOffloadResponse *pResponse);

gboolean remote_offload_data_exchanger_write_response(RemoteOffloadDataExchanger *exchanger,
                                                      GList *mem_list,
                                                      guint64 response_id);

gboolean remote_offload_data_exchanger_write_response_single(RemoteOffloadDataExchanger *exchanger,
                                                             guint8 *data,
                                                             gsize size,
                                                             guint64 response_id);


//The following functions should be called exclusively from RemoteOffloadCommsChannel.
// TODO: Move these to a "private" interface between CommsChannel & DataExchanger
void remote_offload_data_exchanger_set_id(RemoteOffloadDataExchanger *exchanger,
                                          guint16 id);

typedef struct _RemoteOffloadCommsChannel RemoteOffloadCommsChannel;



G_END_DECLS

#endif /* __REMOTEOFFLOADDATAEXCHANGER_H__ */

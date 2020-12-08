/*
 *  remoteoffloadcommsio.h - RemoteOffloadCommsIO interface
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

#ifndef __REMOTEOFFLOAD_COMMS_IO_H__
#define __REMOTEOFFLOAD_COMMS_IO_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define REMOTEOFFLOADCOMMSIO_TYPE (remote_offload_comms_io_get_type ())
G_DECLARE_INTERFACE (RemoteOffloadCommsIO, remote_offload_comms_io, REMOTEOFFLOAD, COMMSIO, GObject)

typedef enum
{
   REMOTEOFFLOADCOMMSIO_SUCCESS = 0,
   REMOTEOFFLOADCOMMSIO_FAIL = -1,
   REMOTEOFFLOADCOMMSIO_CONNECTION_CLOSED = -2,
} RemoteOffloadCommsIOResult;

struct _RemoteOffloadCommsIOInterface
{
   GTypeInterface parent_iface;

   //At least one of the following READ interfaces are required to be implemented.
   RemoteOffloadCommsIOResult (*read)(RemoteOffloadCommsIO *commsio,
                                      guint8 *buf,
                                      guint64 size);

   RemoteOffloadCommsIOResult (*read_mem)(RemoteOffloadCommsIO *commsio,
                                          GstMemory *mem);

   RemoteOffloadCommsIOResult (*read_mem_list)(RemoteOffloadCommsIO *commsio,
                                               GList *mem_list);

   //At least one of the following WRITE interfaces are required be implemented.
   RemoteOffloadCommsIOResult (*write)(RemoteOffloadCommsIO *commsio,
                                       guint8 *buf,
                                       guint64 size);

   RemoteOffloadCommsIOResult (*write_mem)(RemoteOffloadCommsIO *commsio,
                                           GstMemory *mem);

   RemoteOffloadCommsIOResult (*write_mem_list)(RemoteOffloadCommsIO *commsio,
                                                GList *mem_list);

   //Obtain a list of consumable memory-types that this commsio object
   // can natively consume.
   //Return a GList of GstCapsFeatures, in the order of preference.
   //Caller is responsible for free-ing returned list.
   // Note that memory:SystemMemory isn't required to
   // list here, as it's a requirement that all commsio objects
   // support reading-from / writing-to system memory.
   GList *(*get_consumable_memfeatures)(RemoteOffloadCommsIO *commsio);

   //Obtain a list of memory-types that this commsio can natively
   // produce into.
   //Return a GList of GstCapsFeatures, in the order of preference.
   //Caller is responsible for free-ing returned list.
   // Note that memory:SystemMemory isn't technically required to
   // list here, as it's a requirement that all commsio objects
   // support reading-from / writing-to system memory.
   GList *(*get_producible_memfeatures)(RemoteOffloadCommsIO *commsio);


   void (*shutdown)(RemoteOffloadCommsIO *commsio);
};

RemoteOffloadCommsIOResult remote_offload_comms_io_read(RemoteOffloadCommsIO *commsio,
                                                        guint8 *buf,
                                                        guint64 size);

RemoteOffloadCommsIOResult remote_offload_comms_io_read_mem(RemoteOffloadCommsIO *commsio,
                                                            GstMemory *mem);

RemoteOffloadCommsIOResult remote_offload_comms_io_read_mem_list(RemoteOffloadCommsIO *commsio,
                                                                 GList *mem_list);

RemoteOffloadCommsIOResult remote_offload_comms_io_write(RemoteOffloadCommsIO *commsio,
                                                        guint8 *buf,
                                                        guint64 size);


RemoteOffloadCommsIOResult remote_offload_comms_io_write_mem(RemoteOffloadCommsIO *commsio,
                                                             GstMemory *mem);

RemoteOffloadCommsIOResult remote_offload_comms_io_write_mem_list(RemoteOffloadCommsIO *commsio,
                                                                  GList *mem_list);

GList *remote_offload_comms_io_get_consumable_memfeatures(RemoteOffloadCommsIO *commsio);
GList *remote_offload_comms_io_get_producible_memfeatures(RemoteOffloadCommsIO *commsio);

//Shutdown. If there is a thread currently in 'read', it should
// return REMOTEOFFLOADCOMMSIO_CONNECTION_CLOSED. There will be
// no more calls to read, read_list, write, write_list after this point.
void remote_offload_comms_io_shutdown(RemoteOffloadCommsIO *commsio);

G_END_DECLS

#endif /* __REMOTEOFFLOAD_COMMS_IO_H__ */

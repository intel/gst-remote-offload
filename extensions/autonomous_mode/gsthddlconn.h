/*
 *  gsthddlconn.h - HDDL generic communnication API
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author:Yu, Chan Kit <chan.kit.yu@intel.com>
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
#ifndef GST_HDDL_CONN_H
#define GST_HDDL_CONN_H

#include <gst/gst.h>
#include <gio/gio.h>
#include "gsthddltcpconn.h"
#include "gsthddlxlinkcomms.h"

G_BEGIN_DECLS

#define DEFAULT_CONNECTION_MODE CONNECT_XLINK
#define DEFAULT_XLINK_CHANNEL_1 0x400
#define DEFAULT_XLINK_CHANNEL_2 0x401
#define DEFAULT_TCP_HOST "127.0.0.1"
#define DEFAULT_TCP_PORT_1 4952
#define DEFAULT_TCP_PORT_2 4953
#define DEFAULT_DEVICE_PATH NULL

enum _GstHddlConnectionMode{
  CONNECT_TCP,
  CONNECT_XLINK,
};

typedef enum _GstHddlConnectionMode GstHddlConnectionMode;
#define GST_TYPE_CONNECTION_MODE (gst_connection_mode_get_type ())

GType
gst_connection_mode_get_type (void);

gboolean
gst_hddl_establish_connection (void * context, GstHddlConnectionMode mode);

gboolean
gst_hddl_transfer_data (void * context, GstHddlConnectionMode mode, void* buffer,
                       size_t size);

gboolean
gst_hddl_receive_data (void * context, GstHddlConnectionMode mode, void ** buffer,
                       size_t size);

gboolean
gst_hddl_listen (void * context, GstHddlConnectionMode mode);

gboolean
gst_hddl_shutdown(void * context, GstHddlConnectionMode mode);
G_END_DECLS
#endif


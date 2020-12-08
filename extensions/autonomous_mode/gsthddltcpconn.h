/*
 *  gsthddltcpconn.h - TCP/IP communications for data transfer
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author: Yu, Chan Kit <chan.kit.yu@intel.com>
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

#ifndef GST_HDDL_TCPCONN_H
#define GST_HDDL_TCPCONN_H
#include <gst/gst.h>
#include <gio/gio.h>
#include <gst/base/gstadapter.h>
#include <gst/video/gstvideometa.h>
#include <string.h>

G_BEGIN_DECLS typedef enum
{
  GST_TCP_CLIENT_SINK_OPEN = (GST_ELEMENT_FLAG_LAST << 0),

  GST_TCP_CLIENT_SINK_FLAG_LAST = (GST_ELEMENT_FLAG_LAST << 2),
} GstTCPClientSinkFlags;

typedef enum
{
  BUFFER_TYPE,
  BUFFER_VIDEO_ROI_META_TYPE,
  BUFFER_GVA_TENSOR_META_TYPE,
  BUFFER_GVA_JSON_META_TYPE,
  EVENT_TYPE,
  QUERY_TYPE,
  QUERY_RESPONSE_TYPE
} transfer_type;

/* Data transfer header */
typedef struct
{
  /* Size of the data to be transfered */
  gsize size;
  /* Type of data to be transfered: buffer, event */
  transfer_type type;
} transfer_header;

// TODO: Move non-tcp specific struct to appropriate place
/* Buffer info details */
typedef struct
{
  guint64 pts;
  guint64 dts;
  guint64 duration;
  guint64 offset;
  guint64 offset_end;
  guint flags;
  guint num_video_roi_meta;
  guint num_gva_tensor_meta;
  guint num_gva_json_meta;
} buffer_header;

/* Video ROI meta header */
typedef struct
{
  gsize roi_type_string_len;
  guint x;
  guint y;
  guint w;
  guint h;
  guint num_param;
} video_roi_meta_header;

/* Video ROI meta params header */
typedef struct
{
  guint32 is_data_buffer;
  gsize data_buffer_size;
  gsize string_len;
} video_roi_meta_param_header;

/* Event info details */
typedef struct
{
  GstEventType type;
  guint64 timestamp;
  guint32 seqnum;
  gsize string_len;
} event_header;

/* Query info details */
typedef struct
{
  GstQueryType type;
  gsize string_len;
} query_header;

struct _GstHddlTcp
{
  /* server connection */
  gint32 port;
  gchar *host;
  GSocket *client_socket;
  GSocket *server_socket;
  GCancellable *cancellable;
  size_t transfer_size;

  GMutex mutex;
  GCond cond;
};
typedef struct _GstHddlTcp GstHddlTcp;
gboolean gst_hddl_tcp_establish_connection (GstHddlTcp * hddl_tcp);
gboolean gst_hddl_tcp_listen_client (GstHddlTcp * hddl_tcp);
gboolean gst_hddl_tcp_transfer_data (GstHddlTcp * hddl_tcp, void *data,
    size_t size);
gboolean gst_hddl_tcp_receive_data (GstHddlTcp * hddl_tcp, void **buffer,
    size_t size);
gboolean gst_hddl_tcp_shutdown (GstHddlTcp * hddl_tcp);

G_END_DECLS
#endif

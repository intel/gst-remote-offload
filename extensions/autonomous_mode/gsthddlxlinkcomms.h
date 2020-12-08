/*
 *  gsthddlxlinkcomms.h - xlink connection unitilies for HDDL elements
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author: Soon, Thean Siew <thean.siew.soon@intel.com>
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

#include <stdint.h>
#include <stddef.h>

#include <xlink.h>

#include <glib.h>
#include <string.h>
#ifndef GST_HDDL_XLINK_COMMS_H
#define GST_HDDL_XLINK_COMMS_H
#define DATA_FRAGMENT_SIZE 4000000

typedef struct xlink_handle XLinkHandler_t;
typedef enum xlink_error XLinkError_t;
typedef uint16_t xlink_channel_id_t;
typedef xlink_channel_id_t XLinkChannelId_t;

G_BEGIN_DECLS
struct _GstHddlXLink
{
  XLinkHandler_t * xlink_handler;
  XLinkChannelId_t channelId;
};
typedef struct _GstHddlXLink GstHddlXLink;
gboolean gst_hddl_xlink_initialize ();

gboolean gst_hddl_xlink_connect_device (XLinkHandler_t * xlink_handler);

gboolean gst_hddl_xlink_connect (XLinkHandler_t * xlink_handler,
     XLinkChannelId_t channelId);

gboolean gst_hddl_xlink_transfer (GstHddlXLink *hddl_xlink, void * buffer,
                                  size_t transfer_size);
gboolean  gst_hddl_xlink_receive (GstHddlXLink *hddl_xlink, void **buffer,
                                  size_t transfer_size);
gboolean gst_hddl_xlink_listen_client(GstHddlXLink *hddl_xlink);
gboolean gst_hddl_xlink_shutdown(GstHddlXLink *hddl_xlink);

gboolean
gst_hddl_xlink_disconnect (XLinkHandler_t * xlink_handler);
G_END_DECLS
#endif

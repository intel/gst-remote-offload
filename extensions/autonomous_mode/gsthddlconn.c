/*
 *  gsthddlconn.c - HDDL generic communnication API
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
#include "gsthddlconn.h"

GType
gst_connection_mode_get_type (void)
{
  static GType connection_mode = 0;

  if (!connection_mode) {
    static GEnumValue connection_modes[] = {
      {CONNECT_TCP, "Connect by TCP", "tcp"},
      {CONNECT_XLINK, "Connect by XLink API", "xlink"},
      {0, NULL, NULL}
    };

    connection_mode =
        g_enum_register_static ("GstHddlConnectionMode", connection_modes);
  }

  return connection_mode;
}

gboolean
gst_hddl_establish_connection (void *context, GstHddlConnectionMode mode)
{
  if (mode == CONNECT_TCP) {
    // TODO: To reenable once TCP is ready
    // return gst_hddl_tcp_establish_connection ((GstHddlTcp *) context);
    GST_ERROR ("TCP Connection not supported\n");
    return FALSE;
  }
  else if (mode == CONNECT_XLINK) {
    //TODO: Implement this for xlink
    GstHddlXLink *hddl_xlink = (GstHddlXLink *) context;
    return gst_hddl_xlink_connect (hddl_xlink->xlink_handler,
        hddl_xlink->channelId);
  }

  return FALSE;
}

gboolean
gst_hddl_listen (void *context, GstHddlConnectionMode mode)
{
  if (mode == CONNECT_TCP) {
    // TODO: To reenable once TCP is ready
    // return gst_hddl_tcp_listen_client ((GstHddlTcp *) context);
    GST_ERROR ("TCP Connection not supported\n");
    return FALSE;
  }
  if (mode == CONNECT_XLINK)
    return gst_hddl_xlink_listen_client ((GstHddlXLink *) context);

  return FALSE;
}

gboolean
gst_hddl_transfer_data (void *context, GstHddlConnectionMode mode, void *buffer,
    size_t size)
{
  gboolean ret = FALSE;
  if (mode == CONNECT_TCP) {
    // TODO: To reenable once TCP is ready
    // ret = gst_hddl_tcp_transfer_data ((GstHddlTcp *) context, buffer, size);
    GST_ERROR ("TCP Connection not supported\n");
  }
  else if (mode == CONNECT_XLINK)
    ret = gst_hddl_xlink_transfer ((GstHddlXLink *) context, buffer, size);

  return ret;
}

gboolean
gst_hddl_receive_data (void *context, GstHddlConnectionMode mode, void **buffer,
    size_t transfer_size)
{
  gboolean ret = FALSE;
  if (mode == CONNECT_XLINK) {
    GstHddlXLink *hddl_xlink = (GstHddlXLink *) context;
    ret = gst_hddl_xlink_receive (hddl_xlink, buffer, transfer_size);
  } else if (mode == CONNECT_TCP) {
    // TODO: To reenable once TCP is ready
    // GstHddlTcp *hddl_tcp = (GstHddlTcp *) context;
    // transfer_header *header;
    // ret = gst_hddl_tcp_receive_data (hddl_tcp, buffer, transfer_size);
    // header = (transfer_header*)*buffer;
    // *buffer += transfer_size;
    // ret = gst_hddl_tcp_receive_data (hddl_tcp, buffer, header->size);
    GST_ERROR ("TCP Connection not supported\n");
    return FALSE;
  }
  if (!ret) {
    g_print ("Receive data failed\n");
  }

  return ret;
}

gboolean
gst_hddl_shutdown (void *context, GstHddlConnectionMode mode)
{
  gboolean ret = FALSE;
  if (mode == CONNECT_XLINK)
    ret = gst_hddl_xlink_shutdown ((GstHddlXLink *) context);
  else if (mode == CONNECT_TCP) {
    // TODO: To reenable once TCP is ready
    // ret = gst_hddl_tcp_shutdown ((GstHddlTcp *) context);
    GST_ERROR ("TCP Connection not supported\n");
  }

  return ret;
}

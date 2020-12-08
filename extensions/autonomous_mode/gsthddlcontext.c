/*
 *  gsthddlcontext.c - Context for HDDL elements
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author: Soon, Thean Siew <thean.siew.soon@intel.com>
 *    Author: Hoe, Sheng Yang <sheng.yang.hoe@intel.com>
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

#include "gsthddlcontext.h"

GstHddlContext *
gst_hddl_context_new (GstHddlConnectionMode mode)
{
  GstHddlContext *context = g_slice_new (GstHddlContext);
  context->conn_mode = mode;

  if (mode == CONNECT_TCP) {
    context->hddl_tcp = g_slice_new (GstHddlTcp);
    context->hddl_tcp->client_socket = NULL;
    context->hddl_tcp->server_socket = NULL;
  } else if (mode == CONNECT_XLINK) {
    context->hddl_xlink = g_slice_new (GstHddlXLink);
    context->hddl_xlink->xlink_handler = g_slice_new (XLinkHandler_t);
  }

  return context;
}

GstHddlContext *
gst_hddl_context_copy (GstHddlContext * context)
{
  GstHddlConnectionMode mode = context->conn_mode;
  GstHddlContext *hddl_context = gst_hddl_context_new (mode);

  if (hddl_context) {
    hddl_context->context_id = context->context_id;

    if (mode == CONNECT_TCP) {
      hddl_context->hddl_tcp = g_slice_dup (GstHddlTcp, context->hddl_tcp);
    } else if (mode == CONNECT_XLINK) {
      hddl_context->hddl_xlink =
          g_slice_dup (GstHddlXLink, context->hddl_xlink);
      hddl_context->hddl_xlink->xlink_handler =
          context->hddl_xlink->xlink_handler;
    }
  }

  return hddl_context;
}

void
gst_hddl_context_free (GstHddlContext * hddl_context)
{

  if (hddl_context) {

    if (hddl_context->conn_mode == CONNECT_TCP) {
      if (hddl_context->hddl_tcp) {
        g_slice_free1 (sizeof (GstHddlTcp), hddl_context->hddl_tcp);
        hddl_context->hddl_tcp = NULL;
      }
    } else if (hddl_context->conn_mode == CONNECT_XLINK) {
      if (hddl_context->hddl_xlink) {
        g_slice_free1 (sizeof (GstHddlXLink), hddl_context->hddl_xlink);
        hddl_context->hddl_xlink = NULL;
      }
    }
    g_slice_free (GstHddlContext, hddl_context);
  }
}

GType
gst_hddl_context_get_type (void)
{
  static GType type = 0;

  if (G_UNLIKELY (!type))
    type = g_boxed_type_register_static ("GstHddlContext",
        (GBoxedCopyFunc) gst_hddl_context_copy,
        (GBoxedFreeFunc) gst_hddl_context_free);

  return type;
}

/*
 *  gsthddlsink.h - HDDL Sink element
 *
 *  Copyright (C) 2019 Intel Corporation
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

#ifndef GST_HDDL_SINK_H
#define GST_HDDL_SINK_H

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include "gsthddlcontext.h"

G_BEGIN_DECLS
#define GST_TYPE_HDDL_SINK \
	(gst_hddl_sink_get_type())
#define GST_HDDL_SINK(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HDDL_SINK, GstHddlSink))
#define GST_HDDL_SINK_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HDDL_SINK, \
	GstHddlSinkClass))
#define GST_IS_HDDL_SINK(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HDDL_SINK))
#define GST_IS_HDDL_SINK_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HDDL_SINK))
typedef struct _GstHddlSink GstHddlSink;
typedef struct _GstHddlSinkClass GstHddlSinkClass;

struct _GstHddlSink
{
  GstBaseSink basesink;
  /* Target handle */
  GstHddlContext *selected_target_context;
  GstHddlConnectionMode connection_mode;
  gboolean xlink_connected;
  gchar *previous_caps;

  /* Transfer queue */
  GAsyncQueue *queue;
  GThread *send_thread;
};

struct _GstHddlSinkClass
{
  GstBaseSinkClass parent_class;
};

GType gst_hddl_sink_get_type (void);

G_END_DECLS
#endif /* GST_HDDL_SINK_H */

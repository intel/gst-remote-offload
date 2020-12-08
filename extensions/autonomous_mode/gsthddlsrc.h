/*
 *  gsthddlsrc.h - HDDL Source element
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

#ifndef GST_HDDL_SRC_H
#define GST_HDDL_SRC_H
#include <gst/gst.h>
#include "gsthddlcontext.h"
G_BEGIN_DECLS
#define GST_TYPE_HDDL_SRC \
(gst_hddl_src_get_type())
#define GST_HDDL_SRC(obj) \
(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_HDDL_SRC, GstHddlSrc))
#define GST_HDDL_SRC_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HDDL_SRC, \
GstHddlSrcClass))
#define GST_IS_HDDL_SRC(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HDDL_SRC))
#define GST_IS_HDDL_SRC_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HDDL_SRC))

typedef struct _GstHddlSrc GstHddlSrc;
typedef struct _GstHddlSrcClass GstHddlSrcClass;

struct _GstHddlSrc
{
  GstElement element;
/* Target handle */
  GstHddlContext *selected_target_context;
  GstHddlConnectionMode connection_mode;
  GstPad *srcpad;
/* Spawn new thread for receive */
  GThread *receive_thread;
};

struct _GstHddlSrcClass
{
  GstElementClass parent_class;
};

GType gst_hddl_src_get_type (void);

G_END_DECLS
#endif /* GST_HDDL_SRC_H */

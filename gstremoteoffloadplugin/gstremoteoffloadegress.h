/*
 *  remoteoffloadegress.h - GstRemoteOffloadEgress element
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
#ifndef _GST_REMOTEOFFLOADEGRESS_H_
#define _GST_REMOTEOFFLOADEGRESS_H_

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_REMOTEOFFLOAD_EGRESS \
  (gst_remoteoffload_egress_get_type())
#define GST_REMOTEOFFLOAD_EGRESS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_REMOTEOFFLOAD_EGRESS,GstRemoteOffloadEgress))
#define GST_REMOTEOFFLOAD_EGRESS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_REMOTEOFFLOAD_EGRESS,GstRemoteOffloadEgressClass))
#define GST_IS_REMOTEOFFLOAD_EGRESS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_REMOTEOFFLOAD_EGRESS))
#define GST_IS_REMOTEOFFLOAD_EGRESS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_REMOTEOFFLOAD_EGRESS))

typedef struct _GstRemoteOffloadEgress GstRemoteOffloadEgress;
typedef struct _GstRemoteOffloadEgressClass GstRemoteOffloadEgressClass;
typedef struct _GstRemoteOffloadEgressPrivate GstRemoteOffloadEgressPrivate;

struct _GstRemoteOffloadEgress
{
  GstElement element;

  GstRemoteOffloadEgressPrivate *priv;
};

struct _GstRemoteOffloadEgressClass {
  GstElementClass parent_class;

};

GType gst_remoteoffload_egress_get_type (void);

G_END_DECLS

#endif

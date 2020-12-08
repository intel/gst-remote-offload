/*
 *  remoteoffloadingress.h - GstRemoteOffloadIngress element
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
#ifndef _GST_REMOTEOFFLOADINGRESS_H_
#define _GST_REMOTEOFFLOADINGRESS_H_

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_REMOTEOFFLOAD_INGRESS \
  (gst_remoteoffload_ingress_get_type())
#define GST_REMOTEOFFLOAD_INGRESS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_REMOTEOFFLOAD_INGRESS,GstRemoteOffloadIngress))
#define GST_REMOTEOFFLOAD_INGRESS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_REMOTEOFFLOAD_INGRESS,GstRemoteOffloadIngressClass))
#define GST_IS_REMOTEOFFLOAD_INGRESS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_REMOTEOFFLOAD_INGRESS))
#define GST_IS_REMOTEOFFLOAD_INGRESS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_REMOTEOFFLOAD_INGRESS))

typedef struct _GstRemoteOffloadIngress GstRemoteOffloadIngress;
typedef struct _GstRemoteOffloadIngressClass GstRemoteOffloadIngressClass;
typedef struct _GstRemoteOffloadIngressPrivate GstRemoteOffloadIngressPrivate;

struct _GstRemoteOffloadIngress
{
  GstElement element;

  GstRemoteOffloadIngressPrivate *priv;
};

struct _GstRemoteOffloadIngressClass {
  GstElementClass parent_class;

};

GType gst_remoteoffload_ingress_get_type (void);

G_END_DECLS

#endif

/*
 *  gstvideoroicompose.h - GstVideoRoiCompose element
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
#ifndef __GST_VIDEOROICOMPOSE_H__
#define __GST_VIDEOROICOMPOSE_H__

#include <gst/gst.h>
#include <gst/base/gstaggregator.h>

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_VIDEOROICOMPOSE \
  (gst_video_roi_compose_get_type())
#define GST_VIDEOROICOMPOSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEOROICOMPOSE,GstVideoRoiCompose))
#define GST_VIDEOROICOMPOSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEOROICOMPOSE,GstVideoRoiComposeClass))
#define GST_IS_VIDEOROICOMPOSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEOROICOMPOSE))
#define GST_IS_VIDEOROICOMPOSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEOROICOMPOSE))

typedef struct _GstVideoRoiCompose      GstVideoRoiCompose;
typedef struct _GstVideoRoiComposeClass GstVideoRoiComposeClass;

struct _GstVideoRoiCompose
{
  GstAggregator aggregator;

  GstAggregatorPad *sinkvideopad, *sinkmetapad;

  GstVideoInfo in_cropped_info;
  GstVideoInfo out_composed_info;

  guint64 currenttimestamp;
  GstBuffer *currentbuffer;
};

struct _GstVideoRoiComposeClass
{
  GstAggregatorClass parent_class;
};

GType gst_video_roi_compose_get_type (void);

G_END_DECLS

#endif /* __GST_VIDEOROICOMPOSE_H__ */

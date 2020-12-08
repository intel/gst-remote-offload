/*
 *  gstvideoroimetafilter.h - GstVideoRoiMetaFilter element
 *
 *  Copyright (C) 2020 Intel Corporation
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
#ifndef __GST_VIDEOROIMETAFILTER_H__
#define __GST_VIDEOROIMETAFILTER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_VIDEOROIMETAFILTER \
  (gst_video_roi_meta_filter_get_type())
#define GST_VIDEOROIMETAFILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEOROIMETAFILTER,GstVideoRoiMetaFilter))
#define GST_VIDEOROIMETAFILTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEOROIMETAFILTER,GstVideoRoiMetaFilterClass))
#define GST_IS_VIDEOROIMETAFILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEOROIMETAFILTER))
#define GST_IS_VIDEOROIMETAFILTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEOROIMETAFILTER))

typedef struct _GstVideoRoiMetaFilter      GstVideoRoiMetaFilter;
typedef struct _GstVideoRoiMetaFilterClass GstVideoRoiMetaFilterClass;
typedef struct GstVideoRoiMetaFilterPrivate_ GstVideoRoiMetaFilterPrivate;

struct _GstVideoRoiMetaFilter
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gchar *preserve_filter;
  gchar *remove_filter;

  GstVideoRoiMetaFilterPrivate *priv;
};

struct _GstVideoRoiMetaFilterClass
{
  GstElementClass parent_class;
};

GType gst_video_roi_meta_filter_get_type (void);

G_END_DECLS

#endif /* __GST_VIDEOROIMETAFILTER_H__ */

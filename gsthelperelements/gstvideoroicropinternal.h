/*
 *  gstvideoroicropinternal.h - VideoRoiCropInternal element
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
#ifndef __GST_VIDEOROICROPINTERNAL_H__
#define __GST_VIDEOROICROPINTERNAL_H__

#include <gst/gst.h>

G_BEGIN_DECLS

//Set to 0 for use with pipeline for which we need to use
// a caps memory-type feature that videocrop doesn't support,
// and also it is known that a downstream element supports
// GstVideoCropMeta. When this is 0, this element will simply
// set GstVideoCropMeta on the buffer, as opposed to pushing
// to videocrop.
#define USE_VIDEOCROP 0

/* #defines don't like whitespacey bits */
#define GST_TYPE_VIDEOROICROPINTERNAL \
  (gst_video_roi_crop_internal_get_type())
#define GST_VIDEOROICROPINTERNAL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEOROICROPINTERNAL,VideoRoiCropInternal))
#define GST_VIDEOROICROPINTERNAL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEOROICROPINTERNAL,VideoRoiCropInternalClass))
#define GST_IS_VIDEOROICROPINTERNAL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEOROICROPINTERNAL))
#define GST_IS_VIDEOROICROPINTERNAL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEOROICROPINTERNAL))

typedef struct _VideoRoiCropInternal      VideoRoiCropInternal;
typedef struct _VideoRoiCropInternalClass VideoRoiCropInternalClass;

struct _VideoRoiCropInternal
{
  GstElement element;

  GstPad *sinkpad, *srcpadvideo, *srcpadmeta;
  gint inputwidth, inputheight;

  GstPad *parentmetapad; //the ghost pad created at videoroicrop level

  GstElement *pvideocrop;

  guint min_crop_width;
  guint min_crop_height;
  guint x_divisible_by;
  guint y_divisible_by;
  guint width_divisible_by;
  guint height_divisible_by;
  guint cropped_obj_frame_interval;
  GHashTable *objid_hash;
  gulong frame_count;
};

struct _VideoRoiCropInternalClass
{
  GstElementClass parent_class;
};

GType gst_video_roi_crop_internal_get_type (void);

G_END_DECLS

#endif /* __GST_VIDEOROICROPINTERNAL_H__ */

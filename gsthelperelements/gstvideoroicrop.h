/*
 *  gstvideoroicrop.h - VideoRoiCrop element
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
#ifndef __GST_VIDEOROICROP_H__
#define __GST_VIDEOROICROP_H__

#include <gst/gst.h>
#include <gst/gstbin.h>

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_VIDEOROICROP \
  (gst_video_roi_crop_get_type())
#define GST_VIDEOROICROP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEOROICROP,VideoRoiCrop))
#define GST_VIDEOROICROP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEOROICROP,VideoRoiCropClass))
#define GST_IS_VIDEOROICROP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEOROICROP))
#define GST_IS_VIDEOROICROP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEOROICROP))

typedef struct _VideoRoiCrop      VideoRoiCrop;
typedef struct _VideoRoiCropClass VideoRoiCropClass;

struct _VideoRoiCrop
{
  GstBin bin;

  //pads exposed to the outside world
  GstPad *sinkpad, *srcpad, *srcpadmeta;

  GstElement *pinternal;
  GstElement *pvideocrop;

};

struct _VideoRoiCropClass
{
   GstBinClass parent_class;
};

GType gst_video_roi_crop_get_type (void);

G_END_DECLS

#endif /* __GST_VIDEOROICROP_H__ */

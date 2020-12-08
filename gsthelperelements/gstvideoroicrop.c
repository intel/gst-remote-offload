/*
 *  gstvideoroicrop.c - VideoRoiCrop element
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
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include "gstvideoroicrop.h"
#include "gstvideoroicropinternal.h"

GST_DEBUG_CATEGORY_STATIC (gst_video_roi_crop_debug);
#define GST_CAT_DEFAULT gst_video_roi_crop_debug

#define DEFAULT_MIN_WIDTH_HEIGHT 32
#define DEFAULT_DIVISIBLE_BY_X_Y 2
#define DEFAULT_DIVISIBLE_BY_WIDTH 16
#define DEFAULT_DIVISIBLE_BY_HEIGHT 2

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_MIN_CROPPED_WIDTH,
  PROP_MIN_CROPPED_HEIGHT,
  PROP_ENSURE_X_DIVISIBLE_BY,
  PROP_ENSURE_Y_DIVISIBLE_BY,
  PROP_ENSURE_WIDTH_DIVISIBLE_BY,
  PROP_ENSURE_HEIGHT_DIVISIBLE_BY,
  PROP_CROPPED_OBJ_FRAME_INTERVAL
};


#define gst_video_roi_crop_parent_class parent_class

G_DEFINE_TYPE_WITH_CODE (VideoRoiCrop, gst_video_roi_crop, GST_TYPE_BIN,
  GST_DEBUG_CATEGORY_INIT (gst_video_roi_crop_debug, "videoroicrop", 0,
  "debug category for videoroicrop"));

static void gst_video_roi_crop_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_roi_crop_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


/* GObject vmethod implementations */
static void gst_video_roi_crop_constructed(GObject *object)
{
   VideoRoiCrop * videoroicrop = GST_VIDEOROICROP(object);
   gchar *elementname = gst_element_get_name(videoroicrop);
   gchar vidname[128];

#if USE_VIDEOCROP
   g_snprintf(vidname, 128, "%s_videocrop", elementname);
   videoroicrop->pvideocrop = gst_element_factory_make("videocrop", vidname);
   if( !videoroicrop->pvideocrop )
   {
      GST_ERROR_OBJECT(videoroicrop, "Error creating videocrop element");
   }
   gst_bin_add(GST_BIN(videoroicrop), videoroicrop->pvideocrop);
#endif

   g_snprintf(vidname, 128, "%s_internal", elementname);
   videoroicrop->pinternal = gst_element_factory_make("videoroicropinternal", vidname);
   if( !videoroicrop->pinternal )
   {
      GST_ERROR_OBJECT(videoroicrop, "Error creating internal element");
   }

   gst_bin_add(GST_BIN(videoroicrop), videoroicrop->pinternal);
   g_free(elementname);

   VideoRoiCropInternal *pInternal = GST_VIDEOROICROPINTERNAL(videoroicrop->pinternal);
   pInternal->pvideocrop = videoroicrop->pvideocrop;
   pInternal->min_crop_width = DEFAULT_MIN_WIDTH_HEIGHT;
   pInternal->min_crop_height = DEFAULT_MIN_WIDTH_HEIGHT;
   pInternal->x_divisible_by = DEFAULT_DIVISIBLE_BY_X_Y;
   pInternal->y_divisible_by = DEFAULT_DIVISIBLE_BY_X_Y;
   pInternal->width_divisible_by = DEFAULT_DIVISIBLE_BY_WIDTH;
   pInternal->height_divisible_by = DEFAULT_DIVISIBLE_BY_HEIGHT;
   pInternal->cropped_obj_frame_interval = 1;

#if USE_VIDEOCROP
   {
     GstPad *pad = gst_element_get_static_pad (videoroicrop->pvideocrop, "src");

     videoroicrop->srcpad = gst_ghost_pad_new("src", pad);
     if( !videoroicrop->srcpad )
     {
        GST_ERROR_OBJECT(videoroicrop, "Error creating ghost src pad!");
     }
     gst_object_unref (pad);
   }
#else
   {
     GstPad *pad = gst_element_get_static_pad (videoroicrop->pinternal, "src");

     videoroicrop->srcpad = gst_ghost_pad_new("src", pad);
     if( !videoroicrop->srcpad )
     {
        GST_ERROR_OBJECT(videoroicrop, "Error creating ghost src pad!");
     }
     gst_object_unref (pad);
   }
#endif

   {
     GstPad *pad = gst_element_get_static_pad (videoroicrop->pinternal, "sink");
     videoroicrop->sinkpad = gst_ghost_pad_new("sink", pad);
     if( !videoroicrop->srcpad )
     {
        GST_ERROR_OBJECT(videoroicrop, "Error creating ghost sink pad!");
     }
     gst_object_unref (pad);



   }

   {
      GstPad *pad = gst_element_get_static_pad (videoroicrop->pinternal, "srcmeta");
      videoroicrop->srcpadmeta = gst_ghost_pad_new("srcmeta", pad);
      if( !videoroicrop->srcpadmeta )
      {
         GST_ERROR_OBJECT(videoroicrop, "Error creating ghost srcmeta pad!");
      }
      gst_object_unref (pad);
      pInternal->parentmetapad = videoroicrop->srcpadmeta;
   }

   gst_element_add_pad (GST_ELEMENT (videoroicrop), videoroicrop->srcpad);
   gst_element_add_pad (GST_ELEMENT (videoroicrop), videoroicrop->sinkpad);
   gst_element_add_pad (GST_ELEMENT (videoroicrop), videoroicrop->srcpadmeta);

#if USE_VIDEOCROP
   gst_element_link(videoroicrop->pinternal, videoroicrop->pvideocrop);
#endif

   G_OBJECT_CLASS (gst_video_roi_crop_parent_class)->constructed (object);
}

/* initialize the gst_video_roi_crop class */
static void
gst_video_roi_crop_class_init (VideoRoiCropClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_video_roi_crop_set_property;
  gobject_class->get_property = gst_video_roi_crop_get_property;
  gobject_class->constructed = gst_video_roi_crop_constructed;

  g_object_class_install_property (gobject_class, PROP_MIN_CROPPED_WIDTH,
      g_param_spec_uint ("min-cropped-width", "min cropped width",
          "Minimum width of cropped output. "
          "If ROI width is smaller than this value, it will be enlarged to this width ",
          1, G_MAXUINT,
          DEFAULT_MIN_WIDTH_HEIGHT,
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_MIN_CROPPED_HEIGHT,
      g_param_spec_uint ("min-cropped-height", "min cropped height",
          "Minimum height of cropped output. "
          "If ROI height is smaller than this value, it will be enlarged to this height ",
          1, G_MAXUINT,
          DEFAULT_MIN_WIDTH_HEIGHT,
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_ENSURE_X_DIVISIBLE_BY,
      g_param_spec_uint ("ensure-x-divisible-by", "ensure x divisible by",
          "Ensure the cropped x-coordinate is divisible by this value. "
          "If the ROI x-coordinate is not divisible by this value, "
          "the cropped x-coordinate will be decreased to the closest value that is. ",
          1, G_MAXUINT,
          DEFAULT_DIVISIBLE_BY_X_Y,
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_ENSURE_X_DIVISIBLE_BY,
      g_param_spec_uint ("ensure-y-divisible-by", "ensure y divisible by",
          "Ensure the cropped y-coordinate is divisible by this value. "
          "If the ROI y-coordinate is not divisible by this value, "
          "the cropped y-coordinate will be decreased to the closest value that is. ",
          1, G_MAXUINT,
          DEFAULT_DIVISIBLE_BY_X_Y,
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_ENSURE_WIDTH_DIVISIBLE_BY,
      g_param_spec_uint ("ensure-width-divisible-by", "ensure width divisible by",
          "Ensure the cropped width is divisible by this value. "
          "If the ROI width is not divisible by this value, the cropped width will be expanded ",
          1, G_MAXUINT,
          DEFAULT_DIVISIBLE_BY_WIDTH,
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_ENSURE_HEIGHT_DIVISIBLE_BY,
      g_param_spec_uint ("ensure-height-divisible-by", "ensure height divisible by",
          "Ensure the cropped height is divisible by this value. "
          "If the ROI height is not divisible by this value, the cropped height will be expanded ",
          1, G_MAXUINT,
          DEFAULT_DIVISIBLE_BY_HEIGHT,
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_CROPPED_OBJ_FRAME_INTERVAL,
      g_param_spec_uint ("cropped-obj-frame-interval", "cropped roi frame interval",
          "Once an ROI tagged with an \"object_id\" is cropped, if it is encountered again"
          " within \"cropped-obj-frame-interval\" frames, it is skipped."
          " Setting this value to 1 effectively disables this feature (crops every ROI, every frame)."
          " Setting this value to 0 implies \"forever\"",
          0, G_MAXUINT,
          1,
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));

  gst_element_class_set_details_simple(gstelement_class,
    "VideoRoiCrop",
    "FIXME:Generic",
    "FIXME:Generic Template Element",
    "Ryan Metcalfe <<Ryan.D.Metcalfe@intel.com>>");

}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_video_roi_crop_init (VideoRoiCrop * videoroicrop)
{

}


static void
gst_video_roi_crop_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  VideoRoiCrop *videoroicrop = GST_VIDEOROICROP (object);
  VideoRoiCropInternal *pInternal = GST_VIDEOROICROPINTERNAL(videoroicrop->pinternal);
  if( !pInternal ) return;

  switch (prop_id) {
     case PROP_MIN_CROPPED_WIDTH:
        pInternal->min_crop_width = g_value_get_uint (value);
     break;
     case PROP_MIN_CROPPED_HEIGHT:
        pInternal->min_crop_height = g_value_get_uint (value);
     break;
     case PROP_ENSURE_X_DIVISIBLE_BY:
        pInternal->x_divisible_by = g_value_get_uint (value);
     break;
     case PROP_ENSURE_Y_DIVISIBLE_BY:
        pInternal->y_divisible_by = g_value_get_uint (value);
     break;
     case PROP_ENSURE_WIDTH_DIVISIBLE_BY:
        pInternal->width_divisible_by = g_value_get_uint (value);
     break;
     case PROP_ENSURE_HEIGHT_DIVISIBLE_BY:
        pInternal->height_divisible_by = g_value_get_uint (value);
     break;
     case PROP_CROPPED_OBJ_FRAME_INTERVAL:
        pInternal->cropped_obj_frame_interval = g_value_get_uint (value);
     break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_roi_crop_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  VideoRoiCrop *videoroicrop = GST_VIDEOROICROP (object);
  VideoRoiCropInternal *pInternal = GST_VIDEOROICROPINTERNAL(videoroicrop->pinternal);
  if( !pInternal ) return;

  switch (prop_id)
  {
    case PROP_MIN_CROPPED_WIDTH:
       g_value_set_uint (value, pInternal->min_crop_width);
    break;
    case PROP_MIN_CROPPED_HEIGHT:
       g_value_set_uint (value, pInternal->min_crop_height);
    break;
    case PROP_ENSURE_X_DIVISIBLE_BY:
        g_value_set_uint (value, pInternal->x_divisible_by);
    break;
    case PROP_ENSURE_Y_DIVISIBLE_BY:
       g_value_set_uint (value, pInternal->x_divisible_by);
    break;
    case PROP_ENSURE_WIDTH_DIVISIBLE_BY:
       g_value_set_uint (value, pInternal->width_divisible_by);
    break;
    case PROP_ENSURE_HEIGHT_DIVISIBLE_BY:
       g_value_set_uint (value, pInternal->height_divisible_by);
    break;
    case PROP_CROPPED_OBJ_FRAME_INTERVAL:
       g_value_set_uint(value, pInternal->cropped_obj_frame_interval);
    break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */



/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
videoroicrop_init (GstPlugin * videoroicrop)
{
  /* debug category for fltering log messages
   *
   */
  GST_DEBUG_CATEGORY_INIT (gst_video_roi_crop_debug, "videoroicrop",
      0, "Template videoroicrop");

  gst_element_register (videoroicrop, "videoroicropinternal", GST_RANK_NONE,
      GST_TYPE_VIDEOROICROPINTERNAL);

  return gst_element_register (videoroicrop, "videoroicrop", GST_RANK_NONE,
      GST_TYPE_VIDEOROICROP);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstvideoroicrop"
#endif

/* Version number of package */
#define VERSION "1.0.0"


/* gstreamer looks for this structure to register videoroicrop
 *
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    videoroicrop,
    "Template videoroicrop",
    videoroicrop_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)






/*
 *  gstvideoroimetadetach.c - GstVideoRoiMetaDetach element
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
#include <stdio.h>
#include <unistd.h>
#include <gst/video/gstvideometa.h>
#include "gstvideoroimetadetach.h"

GST_DEBUG_CATEGORY_STATIC (gst_video_roi_meta_detach_debug);
#define GST_CAT_DEFAULT gst_video_roi_meta_detach_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

#define gst_video_roi_meta_detach_parent_class parent_class
G_DEFINE_TYPE (GstVideoRoiMetaDetach, gst_video_roi_meta_detach, GST_TYPE_ELEMENT);

static void gst_video_roi_meta_detach_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_roi_meta_detach_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean
gst_video_roi_meta_detach_sink_event (GstPad * pad, GstObject * parent, GstEvent * event);
static GstFlowReturn
gst_video_roi_meta_detach_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);

/* GObject vmethod implementations */

/* initialize the videoroimetadetach's class */
static void
gst_video_roi_meta_detach_class_init (GstVideoRoiMetaDetachClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_video_roi_meta_detach_set_property;
  gobject_class->get_property = gst_video_roi_meta_detach_get_property;

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));

  gst_element_class_set_details_simple(gstelement_class,
    "VideoRoiMetaDetach",
    "FIXME:Generic",
    "FIXME:Generic Template Element",
    "rdmetca <<user@hostname.org>>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_video_roi_meta_detach_init (GstVideoRoiMetaDetach * filter)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_event_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_video_roi_meta_detach_sink_event));
  gst_pad_set_chain_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_video_roi_meta_detach_chain));
  GST_PAD_SET_PROXY_CAPS (filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  GST_PAD_SET_PROXY_CAPS (filter->srcpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->silent = FALSE;
}

static void
gst_video_roi_meta_detach_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoRoiMetaDetach *filter = GST_VIDEOROIMETADETACH (object);

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_roi_meta_detach_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoRoiMetaDetach *filter = GST_VIDEOROIMETADETACH (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

/* this function handles sink events */
static gboolean
gst_video_roi_meta_detach_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstVideoRoiMetaDetach *filter;
  gboolean ret;

  filter = GST_VIDEOROIMETADETACH (parent);

  GST_LOG_OBJECT (filter, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps * caps;

      gst_event_parse_caps (event, &caps);
      /* do something with the caps */

      /* and forward */
      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }
  return ret;
}


static void AddStructureToMeta(gpointer       data,
                               gpointer       user_data)
{
   GstStructure* s = (GstStructure*)data;
   GstVideoRegionOfInterestMeta *meta = (GstVideoRegionOfInterestMeta *)user_data;
   gst_video_region_of_interest_meta_add_param(meta, gst_structure_copy(s));
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_video_roi_meta_detach_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstVideoRoiMetaDetach *filter;

  filter = GST_VIDEOROIMETADETACH (parent);

  GstBuffer *meta_buffer = gst_buffer_new ();
  GST_BUFFER_PTS (meta_buffer) = GST_BUFFER_PTS(buf);
  GST_BUFFER_DTS (meta_buffer) = GST_BUFFER_DTS(buf);
  GST_BUFFER_DURATION (meta_buffer) = GST_BUFFER_DURATION(buf);
  GST_BUFFER_OFFSET (meta_buffer) = GST_BUFFER_OFFSET(buf);
  GST_BUFFER_OFFSET_END (meta_buffer) = GST_BUFFER_OFFSET_END(buf);

  GstVideoRegionOfInterestMeta *meta_orig = NULL;
  gpointer state = NULL;
  while( (meta_orig = (GstVideoRegionOfInterestMeta *)
          gst_buffer_iterate_meta_filtered(buf,
                                           &state,
                                           gst_video_region_of_interest_meta_api_get_type())) )
  {
     GstVideoRegionOfInterestMeta *meta_new =
           gst_buffer_add_video_region_of_interest_meta_id(
                 meta_buffer,
                 meta_orig->roi_type,
                 meta_orig->x,
                 meta_orig->y,
                 meta_orig->w,
                 meta_orig->h);

     //This copies every GstStructure in meta_orig->params
     // to meta_new->params
     // Is there a more efficient way to do this?
     //Maybe we can add some property where it actually
     // transfers meta_old->params to meta_new->params. i.e:
     //   meta_new->params = meta_orig->params;
     //   meta_orig->params = NULL;
     if( meta_orig->params )
     {
        g_list_foreach(meta_orig->params, AddStructureToMeta, meta_new);
     }
  }
  gst_buffer_unref(buf);
  return gst_pad_push (filter->srcpad, meta_buffer);
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
videoroimetadetach_init (GstPlugin * videoroimetadetach)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template videoroimetadetach' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_video_roi_meta_detach_debug, "videoroimetadetach",
      0, "Template videoroimetadetach");

  return gst_element_register (videoroimetadetach, "videoroimetadetach", GST_RANK_NONE,
      GST_TYPE_VIDEOROIMETADETACH);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstvideoroimetadetach"
#endif

/* Version number of package */
#define VERSION "1.0.0"


/* gstreamer looks for this structure to register videoroimetadetachs
 *
 * exchange the string 'Template videoroimetadetach' with your videoroimetadetach description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    videoroimetadetach,
    "Template videoroimetadetach",
    videoroimetadetach_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)



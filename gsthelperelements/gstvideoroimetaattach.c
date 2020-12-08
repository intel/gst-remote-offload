/*
 *  gstvideoroimetaattach.c - GstVideoRoiMetaAttach element
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
#include "gstvideoroimetaattach.h"
#include <gst/video/gstvideometa.h>

GST_DEBUG_CATEGORY_STATIC (gst_videoroi_meta_attach_debug);
#define GST_CAT_DEFAULT gst_videoroi_meta_attach_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SYNCONTIMESTAMP
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

#define gst_videoroi_meta_attach_parent_class parent_class
G_DEFINE_TYPE (GstVideoRoiMetaAttach, gst_videoroi_meta_attach, GST_TYPE_ELEMENT);

static void gst_videoroi_meta_attach_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_videoroi_meta_attach_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean
gst_videoroi_meta_attach_sink_event (GstPad * pad, GstObject * parent, GstEvent * event);
static gboolean
gst_videoroi_meta_attach_metasink_event (GstPad * pad, GstObject * parent, GstEvent * event);
static GstFlowReturn
gst_videoroi_meta_attach_video_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);
static GstFlowReturn
gst_videoroi_meta_attach_meta_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);
static gboolean
gst_videoroi_meta_attach_src_query (GstPad    *pad,
                                    GstObject *parent,
                                    GstQuery  *query);
/* GObject vmethod implementations */

/* initialize the videoroimetaattach's class */
static void
gst_videoroi_meta_attach_class_init (GstVideoRoiMetaAttachClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_videoroi_meta_attach_set_property;
  gobject_class->get_property = gst_videoroi_meta_attach_get_property;

  g_object_class_install_property (gobject_class, PROP_SYNCONTIMESTAMP,
      g_param_spec_boolean ("syncontimestamp", "SyncOnTimestamp", "Synchronize based on timestamp(pts)?",
          TRUE, G_PARAM_READWRITE));

  gst_element_class_set_details_simple(gstelement_class,
    "VideoRoiMetaAttach",
    "Element",
    "Transfers GstVideoRegionOfInterestMeta from buffers on sinkmeta to buffers on sinkvideo",
    "Ryan Metcalfe <<ryan.d.metcalfe@intel.com>>");

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
gst_videoroi_meta_attach_init (GstVideoRoiMetaAttach * self)
{
  self->sinkvideopad = gst_pad_new_from_static_template (&sink_factory, "sinkvideo");
  gst_pad_set_event_function (self->sinkvideopad,
                              GST_DEBUG_FUNCPTR(gst_videoroi_meta_attach_sink_event));
  gst_pad_set_chain_function (self->sinkvideopad,
                              GST_DEBUG_FUNCPTR(gst_videoroi_meta_attach_video_chain));
  GST_PAD_SET_PROXY_CAPS (self->sinkvideopad);
  gst_element_add_pad (GST_ELEMENT (self), self->sinkvideopad);

  self->sinkmetapad = gst_pad_new_from_static_template (&sink_factory, "sinkmeta");
  gst_pad_set_event_function (self->sinkmetapad,
                              GST_DEBUG_FUNCPTR(gst_videoroi_meta_attach_metasink_event));
  gst_pad_set_chain_function (self->sinkmetapad,
                              GST_DEBUG_FUNCPTR(gst_videoroi_meta_attach_meta_chain));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkmetapad);


  self->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  GST_PAD_SET_PROXY_CAPS (self->srcpad);
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  gst_pad_set_query_function (self->srcpad,
      gst_videoroi_meta_attach_src_query);

  self->metabufqueue = g_queue_new();
  g_mutex_init(&self->metabufqueuemutex);
  g_cond_init(&self->metabufqueuecond);

  self->syncontimestamp = TRUE;
  self->bmetaEOS = FALSE;
}

static void
gst_videoroi_meta_attach_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoRoiMetaAttach *self = GST_VIDEOROIMETAATTACH (object);

  switch (prop_id) {
    case PROP_SYNCONTIMESTAMP:
      self->syncontimestamp = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_videoroi_meta_attach_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoRoiMetaAttach *self = GST_VIDEOROIMETAATTACH (object);

  switch (prop_id) {
    case PROP_SYNCONTIMESTAMP:
      g_value_set_boolean (value, self->syncontimestamp);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */
static gboolean
gst_videoroi_meta_attach_src_query (GstPad    *pad,
                 GstObject *parent,
                 GstQuery  *query)
{

   //always route the query through the video sink pad
   GstVideoRoiMetaAttach *self = GST_VIDEOROIMETAATTACH (parent);

   return gst_pad_peer_query(self->sinkvideopad, query);
}

/* this function handles sink events */
static gboolean
gst_videoroi_meta_attach_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstVideoRoiMetaAttach *self;
  gboolean ret;

  self = GST_VIDEOROIMETAATTACH (parent);

  GST_LOG_OBJECT (self, "Received %s event: %" GST_PTR_FORMAT,
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

/* this function handles sink events */
static gboolean
gst_videoroi_meta_attach_metasink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
   GstVideoRoiMetaAttach *self = GST_VIDEOROIMETAATTACH (parent);

   switch(GST_EVENT_TYPE (event))
   {
      case GST_EVENT_STREAM_START:
      g_mutex_lock(&self->metabufqueuemutex);
      self->bmetaEOS = FALSE;
      g_mutex_unlock(&self->metabufqueuemutex);
      break;

      case GST_EVENT_EOS:
      //we've received EOS on the meta-stream. Set a flag to indicate this,
      // to inform the video stream not to wait on meta-bufs anymore.
      g_mutex_lock(&self->metabufqueuemutex);
      self->bmetaEOS = TRUE;
      //also wake up the video thread in case it's currently waiting for
      // something.
      g_cond_broadcast(&self->metabufqueuecond);
      g_mutex_unlock(&self->metabufqueuemutex);
      break;
      default:
      break;
   }

  return TRUE;
}

/* chain function
 * this function does the actual processing
 */
static void AddStructureToMeta(gpointer       data,
                               gpointer       user_data)
{
   GstStructure* s = (GstStructure*)data;
   GstVideoRegionOfInterestMeta *meta = (GstVideoRegionOfInterestMeta *)user_data;
   gst_video_region_of_interest_meta_add_param(meta, gst_structure_copy(s));
}

static GstFlowReturn
gst_videoroi_meta_attach_meta_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstVideoRoiMetaAttach *self = GST_VIDEOROIMETAATTACH (parent);

  //push this meta buffer in the queue, and wake up potential waiting thread
  g_mutex_lock(&self->metabufqueuemutex);
  g_queue_push_tail(self->metabufqueue, buf);
  g_cond_broadcast(&self->metabufqueuecond);
  g_mutex_unlock(&self->metabufqueuemutex);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_videoroi_meta_attach_video_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstVideoRoiMetaAttach *self;
  GstFlowReturn ret;

  self = GST_VIDEOROIMETAATTACH (parent);

  GstClockTime video_pts = GST_BUFFER_PTS(buf);
  GstBuffer *metabuf = NULL;

  g_mutex_lock(&self->metabufqueuemutex);
  gboolean done = FALSE;
  while( !done )
  {
     if( g_queue_is_empty(self->metabufqueue))
     {
        if( self->bmetaEOS )
        {
           //if metaEOS flag is set, there are no more meta buffers coming.. so
           // just pass the video buffer through without waiting for meta.
           GST_DEBUG_OBJECT(self, "video buffer (pts=%"GST_TIME_FORMAT") has no corresponding meta",
                                                       GST_TIME_ARGS(video_pts));
           break;
        }
        //wait for meta buffer to get pushed in the queue
        g_cond_wait (&self->metabufqueuecond, &self->metabufqueuemutex);
        continue;
     }
     metabuf = g_queue_pop_head(self->metabufqueue);

     if( !self->syncontimestamp )
        break;

     if( G_UNLIKELY(!metabuf) )
     {
        break;
     }

     GstClockTime meta_pts = GST_BUFFER_PTS(metabuf);
     if( meta_pts < video_pts )
     {
        //The current meta_buf has an older timestamp than our
        // current video buffer. Maybe there were video frames dropped
        // or something. Unref the meta_buf and get the next buffer.
        GST_DEBUG_OBJECT(self, "dropping meta buffer with pts=%"GST_TIME_FORMAT,
                                                       GST_TIME_ARGS(meta_pts));
        gst_buffer_unref(metabuf);
        metabuf = NULL;
     }
     else
     if( meta_pts > video_pts )
     {
        //The current meta_buf has a newer timestamp. Not sure
        // in what cases this may happen, but in this case we
        // will push the meta_buf back into the queue (hoping that
        // it will line up with a future video buffer), and pass
        // along the current video buffer without attaching any
        // meta.
        GST_DEBUG_OBJECT(self, "video buffer (pts=%"GST_TIME_FORMAT") has no corresponding meta",
                                                       GST_TIME_ARGS(video_pts));
        g_queue_push_head(self->metabufqueue, metabuf);
        metabuf = NULL;
        done = TRUE;
     }
     else
     {
        //video_pts == meta_pts
        done = TRUE;
     }
  }
  g_mutex_unlock(&self->metabufqueuemutex);

  if( metabuf )
  {
     //transfer meta from metabuf to buf
     GstVideoRegionOfInterestMeta *meta_orig = NULL;
     gpointer state = NULL;
     while( (meta_orig = (GstVideoRegionOfInterestMeta *)
             gst_buffer_iterate_meta_filtered(metabuf,
                                              &state,
                                              gst_video_region_of_interest_meta_api_get_type())) )
     {
        GstVideoRegionOfInterestMeta *meta_new =
              gst_buffer_add_video_region_of_interest_meta_id(
                    buf,
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
     gst_buffer_unref(metabuf);
  }

  ret = gst_pad_push (self->srcpad, buf);

  return ret;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
videoroimetaattach_init (GstPlugin * videoroimetaattach)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template videoroimetaattach' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_videoroi_meta_attach_debug, "videoroimetaattach",
      0, "Template videoroimetaattach");

  return gst_element_register (videoroimetaattach, "videoroimetaattach", GST_RANK_NONE,
      GST_TYPE_VIDEOROIMETAATTACH);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstvideoroimetaattach"
#endif

/* Version number of package */
#define VERSION "1.0.0"


/* gstreamer looks for this structure to register videoroimetaattachs
 *
 * exchange the string 'Template videoroimetaattach' with your videoroimetaattach description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    videoroimetaattach,
    "Template videoroimetaattach",
    videoroimetaattach_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)

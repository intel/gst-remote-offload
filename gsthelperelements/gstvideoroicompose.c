/*
 *  gstvideoroicompose.c - GstVideoRoiCompose element
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
#include <string.h>
#include <gst/video/gstvideometa.h>
#include "gstvideoroicompose.h"

GST_DEBUG_CATEGORY_STATIC (gst_video_roi_compose_debug);
#define GST_CAT_DEFAULT gst_video_roi_compose_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sinkvideo_factory = GST_STATIC_PAD_TEMPLATE ("sinkvideo",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE("{ BGRx, BGRA }"))
    );

static GstStaticPadTemplate sinkmeta_factory = GST_STATIC_PAD_TEMPLATE ("sinkmeta",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE("{ BGRx, BGRA }"))
    );

#define VIDEO_SINK_CAPS GST_VIDEO_CAPS_MAKE("{ BGRx, BGRA }")
#define S GST_VIDEO_CAPS_MAKE("{ BGRx, BGRA }")

#define gst_video_roi_compose_parent_class parent_class
G_DEFINE_TYPE (GstVideoRoiCompose, gst_video_roi_compose, GST_TYPE_AGGREGATOR);

static void gst_video_roi_compose_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_roi_compose_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn  gst_video_roi_compose_aggregate(GstAggregator    *aggregator,
                                                         gboolean timeout);

static gboolean          gst_video_roi_compose_negotiated_src_caps (GstAggregator *  self,
                                            GstCaps      *  caps);

static GstFlowReturn     gst_video_roi_compose_update_src_caps (GstAggregator *  self,
                                        GstCaps       *  caps,
                                        GstCaps       ** ret);

static gboolean          gst_video_roi_compose_sink_event (GstAggregator    *  aggregator,
                                       GstAggregatorPad *  aggregator_pad,
                                       GstEvent         *  event);

/* GObject vmethod implementations */

/* initialize the videoroicompose's class */
static void
gst_video_roi_compose_class_init (GstVideoRoiComposeClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstAggregatorClass *gstaggregator_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstaggregator_class = (GstAggregatorClass *) klass;

  gobject_class->set_property = gst_video_roi_compose_set_property;
  gobject_class->get_property = gst_video_roi_compose_get_property;

  gstaggregator_class->aggregate =
        GST_DEBUG_FUNCPTR (gst_video_roi_compose_aggregate);
  gstaggregator_class->update_src_caps =
        GST_DEBUG_FUNCPTR(gst_video_roi_compose_update_src_caps);
  gstaggregator_class->negotiated_src_caps =
        GST_DEBUG_FUNCPTR(gst_video_roi_compose_negotiated_src_caps);
  gstaggregator_class->sink_event =
        GST_DEBUG_FUNCPTR(gst_video_roi_compose_sink_event);
  gst_element_class_set_details_simple(gstelement_class,
    "GVAVideoRoiCompose",
    "FIXME:Generic",
    "FIXME:Generic Template Element",
    "rdmetca <<user@hostname.org>>");

  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &sinkvideo_factory, GST_TYPE_AGGREGATOR_PAD);
  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &sinkmeta_factory, GST_TYPE_AGGREGATOR_PAD);
  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &src_factory, GST_TYPE_AGGREGATOR_PAD);
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_video_roi_compose_init (GstVideoRoiCompose * filter)
{
  //GstAggregator *aggr = (GstAggregator *)filter;
  gst_element_create_all_pads ((GstElement *)filter);
  filter->sinkvideopad =
        (GstAggregatorPad *)gst_element_get_static_pad((GstElement *)filter, "sinkvideo");
  filter->sinkmetapad =
        (GstAggregatorPad *)gst_element_get_static_pad((GstElement *)filter, "sinkmeta");

  filter->currentbuffer = NULL;
  filter->currenttimestamp = 0;
}

static void
gst_video_roi_compose_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
//  GstVideoRoiCompose *filter = GST_VIDEOROICOMPOSE (object);
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_roi_compose_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  //GstVideoRoiCompose *filter = GST_VIDEOROICOMPOSE (object);
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstBuffer *alloc_new_buffer(GstVideoRoiCompose *vidroicompose)
{
  GstBuffer *buf = NULL;
  GstBufferPool *pool = gst_aggregator_get_buffer_pool((GstAggregator *)vidroicompose);
  if( pool )
  {
     if( !gst_buffer_pool_is_active(pool) )
     {
        gst_buffer_pool_set_active(pool, TRUE);
     }

     GstFlowReturn ret = gst_buffer_pool_acquire_buffer(pool, &buf, NULL);
     if( ret != GST_FLOW_OK )
     {
        fprintf(stderr, "gst_video_roi_compose: gst_buffer_pool_acquire_buffer failed(%d)\n", ret);
        buf = NULL;
     }
     gst_object_unref(pool);
  }
  else
  {
     guint outsize;
     GstAllocator *allocator;
     GstAllocationParams params;
     gst_aggregator_get_allocator ((GstAggregator *)vidroicompose, &allocator, &params);

     outsize = GST_VIDEO_INFO_SIZE (&vidroicompose->out_composed_info);
     buf = gst_buffer_new_allocate (allocator, outsize, &params);

     if (allocator)
      gst_object_unref (allocator);

     if (buf == NULL)
     {
        g_print("gst_video_roi_compose: Could not acquire buffer of size: %d\n", outsize);
     }
  }


  return buf;
}

static void AddStructureToMeta(gpointer       data,
                               gpointer       user_data)
{
   GstStructure* s = (GstStructure*)data;
   GstVideoRegionOfInterestMeta *meta = (GstVideoRegionOfInterestMeta *)user_data;
   gst_video_region_of_interest_meta_add_param(meta, gst_structure_copy(s));
}

static GstFlowReturn  gst_video_roi_compose_aggregate(GstAggregator    *aggregator,
                                                      gboolean timeout)
{
   GstVideoRoiCompose *vidroicompose = (GstVideoRoiCompose*)aggregator;
   GstFlowReturn ret = GST_FLOW_OK;

   if( gst_aggregator_pad_is_eos(vidroicompose->sinkmetapad) )
   {
      fprintf(stderr, "META EOS\n");
      return GST_FLOW_EOS;
   }

   if( gst_aggregator_pad_is_eos(vidroicompose->sinkvideopad) )
   {
      fprintf(stderr, "VIDEO EOS\n");
      return GST_FLOW_EOS;
   }

   GstBuffer *metabuf = gst_aggregator_pad_pop_buffer(vidroicompose->sinkmetapad);
   GstBuffer *videobuf = gst_aggregator_pad_pop_buffer(vidroicompose->sinkvideopad);

   //guint64 metapts = GST_BUFFER_PTS(metabuf);
   guint64 videopts = GST_BUFFER_PTS(videobuf);

   if( (videopts != vidroicompose->currenttimestamp) &&  vidroicompose->currentbuffer )
   {
      ret = gst_aggregator_finish_buffer(aggregator, vidroicompose->currentbuffer);
      vidroicompose->currentbuffer = 0;
   }

   if( ret == GST_FLOW_OK )
   {
      if( !vidroicompose->currentbuffer )
      {
         vidroicompose->currenttimestamp = videopts;
         vidroicompose->currentbuffer = alloc_new_buffer(vidroicompose);
         if( !vidroicompose->currentbuffer )
         {
            g_print("gst_video_roi_compose_aggregate: alloc_new_buffer failed\n");
            ret = GST_FLOW_ERROR;
         }
         else
         {
           GstMapInfo canvasMemInfo;
           if( gst_buffer_map(vidroicompose->currentbuffer, &canvasMemInfo, GST_MAP_WRITE) )
           {
              memset(canvasMemInfo.data, 255, canvasMemInfo.size);
              gst_buffer_unmap(vidroicompose->currentbuffer, &canvasMemInfo);
           }

           GST_BUFFER_PTS (vidroicompose->currentbuffer) = GST_BUFFER_PTS(videobuf);
           GST_BUFFER_DTS (vidroicompose->currentbuffer) = GST_BUFFER_DTS(videobuf);
           GST_BUFFER_DURATION (vidroicompose->currentbuffer) = GST_BUFFER_DURATION(videobuf);
           GST_BUFFER_OFFSET (vidroicompose->currentbuffer) = GST_BUFFER_OFFSET(videobuf);
           GST_BUFFER_OFFSET_END (vidroicompose->currentbuffer) = GST_BUFFER_OFFSET_END(videobuf);
         }
      }

      if( vidroicompose->currentbuffer )
      {
         GstVideoRegionOfInterestMeta *meta = NULL;
         gpointer state = NULL;
         meta = (GstVideoRegionOfInterestMeta *)
               gst_buffer_iterate_meta_filtered(metabuf,
                                                &state,
                                                gst_video_region_of_interest_meta_api_get_type());

         if( meta )
         {
           GstMapInfo croppedMemInfo;
           if( gst_buffer_map(videobuf, &croppedMemInfo, GST_MAP_READ) )
           {
             GstMapInfo canvasMemInfo;
             if( gst_buffer_map(vidroicompose->currentbuffer, &canvasMemInfo, GST_MAP_WRITE) )
             {
               guint canvasWidth = 1280;
               guint canvasHeight = 720;

               guint croppedWidth = vidroicompose->in_cropped_info.width;
               guint croppedHeight = vidroicompose->in_cropped_info.height;

               guint8 *pCanvas = canvasMemInfo.data + canvasWidth*4*meta->y + meta->x*4;
               guint8 *pCropped = croppedMemInfo.data;

               guint overlayWidth = croppedWidth;
               guint overlayHeight = croppedHeight;

               if( (meta->x < canvasWidth) && (meta->y < canvasHeight) )
               {
                  if( (meta->x + overlayWidth) > canvasWidth )
                  {
                     overlayWidth = canvasWidth - meta->x;
                  }

                  if( (meta->y + overlayHeight) > canvasHeight )
                  {
                     overlayHeight = canvasHeight - meta->y;
                  }

#if 0
                  fprintf(stderr, "meta->x: %u\n", meta->x);
                  fprintf(stderr, "meta->y: %u\n", meta->y);
                  fprintf(stderr, "meta->w: %u\n", meta->w);
                  fprintf(stderr, "meta->h: %u\n", meta->h);
                  fprintf(stderr, "croppedWidth: %u\n", croppedWidth);
                  fprintf(stderr, "croppedHeight: %u\n", croppedHeight);
                  fprintf(stderr, "overlayWidth: %u\n", overlayWidth);
                  fprintf(stderr, "overlayHeight: %u\n", overlayHeight);
                  fprintf(stderr, "cropped mem size: %lu\n", croppedMemInfo.size);
                  fprintf(stderr, "\n\n\n");
#endif
                  guint x,y;
                  for( y = 0; y < overlayHeight; y++ )
                  {
                     for( x = 0; x < overlayWidth; x++ )
                     {
                        pCanvas[x*4 + 0] = pCropped[x*4 + 0];  //B
                        pCanvas[x*4 + 1] = pCropped[x*4 + 1];  //G
                        pCanvas[x*4 + 2] = pCropped[x*4 + 2];  //R
                        pCanvas[x*4 + 3] = pCropped[x*4 + 3];  //X
                     }

                     pCropped += vidroicompose->in_cropped_info.stride[0];
                     pCanvas += canvasWidth*4;
                  }

                  //add this meta to the current buffer
                  GstVideoRegionOfInterestMeta *meta_new =
                 gst_buffer_add_video_region_of_interest_meta_id(
                       vidroicompose->currentbuffer,
                       meta->roi_type,
                       meta->x,
                       meta->y,
                       meta->w,
                       meta->h);

                  if( meta->params )
                  {
                     g_list_foreach(meta->params, AddStructureToMeta, meta_new);
                  }
               }

               gst_buffer_unmap(vidroicompose->currentbuffer, &canvasMemInfo);
             }
             else
             {
                GST_ERROR("error mapping current buffer for writing\n");
                ret = GST_FLOW_ERROR;
             }
             gst_buffer_unmap(videobuf, &croppedMemInfo);

           }
           else
           {
              GST_ERROR("error mapping video buffer for reading\n");
              ret = GST_FLOW_ERROR;
           }
         }
         else
         {
            GST_ERROR("meta is NULL\n");
            ret = GST_FLOW_ERROR;
         }
      }
   }

   gst_buffer_unref(videobuf);
   gst_buffer_unref(metabuf);

   return ret;
}

static gboolean  gst_video_roi_compose_sink_event (GstAggregator    * aggregator,
                                                   GstAggregatorPad * aggregator_pad,
                                                   GstEvent         * event)
{
   GstVideoRoiCompose *vidroicompose = (GstVideoRoiCompose*)aggregator;

   if( aggregator_pad == vidroicompose->sinkvideopad )
   {
      switch (GST_EVENT_TYPE (event))
      {
         case GST_EVENT_CAPS:
         {
            GstCaps *caps;
            gst_event_parse_caps (event, &caps);
            gst_video_info_from_caps(&vidroicompose->in_cropped_info, caps);
         }
         break;
         default:
         break;

      }
   }
   return GST_AGGREGATOR_CLASS(parent_class)->sink_event(aggregator, aggregator_pad, event);
}

static GstFlowReturn     gst_video_roi_compose_update_src_caps (GstAggregator *  self,
                                        GstCaps       *  caps,
                                        GstCaps       ** ret)
{


  *ret = gst_caps_from_string("video/x-raw, "
                              "format=(string)BGRx, "
                              "width=(int)1280, height=(int)720, "
                              "interlace-mode=(string)progressive, "
                              "multiview-mode=(string)mono, "
                              "multiview-flags=(GstVideoMultiviewFlagsSet)0:ffffffff:/"
                              "right-view-first/left-flipped/left-flopped/right-flipped/"
                              "right-flopped/half-aspect/mixed-mono, "
                              "pixel-aspect-ratio=(fraction)1/1, "
                              "chroma-site=(string)mpeg2, "
                              "colorimetry=(string)2:1:5:1, "
                              "framerate=(fraction)24000/1001");

  return GST_FLOW_OK;
}

static gboolean          gst_video_roi_compose_negotiated_src_caps (GstAggregator *  self,
                                            GstCaps      *  caps)
{
   GstVideoRoiCompose *vidroicompose = (GstVideoRoiCompose*)self;
   gst_video_info_from_caps(&vidroicompose->out_composed_info, caps);

   gchar *capsstr = gst_caps_to_string(caps);
   fprintf(stderr, "NEGOTIATED SRC CAPS = %s\n", capsstr);
   g_free(capsstr);
   return TRUE;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
videoroicompose_init (GstPlugin * videoroicompose)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template videoroicompose' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_video_roi_compose_debug, "videoroicompose",
      0, "Template videoroicompose Blah Blah Blah");

  return gst_element_register (videoroicompose, "videoroicompose", GST_RANK_NONE,
      GST_TYPE_VIDEOROICOMPOSE);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstvideoroicompose"
#endif

/* Version number of package */
#define VERSION "1.0.0"


/* gstreamer looks for this structure to register videoroicomposes
 *
 * exchange the string 'Template videoroicompose' with your videoroicompose description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    videoroicompose,
    "Template videoroicompose",
    videoroicompose_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)

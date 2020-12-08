/*
 *  gstvideoroicropinternal.c - VideoRoiCropInternal element
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
#include <stdlib.h>
#include <stdio.h>
#include "gstvideoroicropinternal.h"
#include <gst/video/gstvideometa.h>

GST_DEBUG_CATEGORY_STATIC (gst_video_roi_crop_internal_debug);
#define GST_CAT_DEFAULT gst_video_roi_crop_internal_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0
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

static GstStaticPadTemplate srcmeta_factory = GST_STATIC_PAD_TEMPLATE ("srcmeta",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );


#define gst_video_roi_crop_internal_parent_class parent_class
G_DEFINE_TYPE (VideoRoiCropInternal, gst_video_roi_crop_internal, GST_TYPE_ELEMENT);

static void gst_video_roi_crop_internal_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_roi_crop_internal_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean
gst_video_roi_crop_internal_sink_event (GstPad * pad, GstObject * parent, GstEvent * event);
static GstFlowReturn
gst_video_roi_crop_internal_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);

/* GObject vmethod implementations */

static void
gst_video_roi_crop_internal_finalize(GObject *gobject)
{
   VideoRoiCropInternal *filter = GST_VIDEOROICROPINTERNAL (gobject);

   g_hash_table_destroy(filter->objid_hash);
}
/* initialize the video_roi_crop internal's class */
static void
gst_video_roi_crop_internal_class_init (VideoRoiCropInternalClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_video_roi_crop_internal_set_property;
  gobject_class->get_property = gst_video_roi_crop_internal_get_property;
  gobject_class->finalize = gst_video_roi_crop_internal_finalize;

  gst_element_class_set_details_simple(gstelement_class,
    "GVACropRoiInternal",
    "FIXME:Generic",
    "FIXME:Generic Template Element",
    "rdmetca <<user@hostname.org>>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srcmeta_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_video_roi_crop_internal_init (VideoRoiCropInternal * filter)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_event_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_video_roi_crop_internal_sink_event));
  gst_pad_set_chain_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_video_roi_crop_internal_chain));
  GST_PAD_SET_PROXY_CAPS (filter->sinkpad);
  GST_PAD_SET_PROXY_ALLOCATION(filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpadvideo = gst_pad_new_from_static_template (&src_factory, "src");
  GST_PAD_SET_PROXY_CAPS (filter->srcpadvideo);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpadvideo);

  filter->srcpadmeta = gst_pad_new_from_static_template (&srcmeta_factory, "srcmeta");
  GST_PAD_SET_PROXY_CAPS (filter->srcpadmeta);
  if( gst_element_add_pad (GST_ELEMENT (filter), filter->srcpadmeta) )
  {
     fprintf(stderr, "Successfully created srcmeta pad\n");
  }
  else
  {
     fprintf(stderr, "Error creating srcmeta pad\n");
  }


  filter->pvideocrop = NULL;
  filter->parentmetapad = NULL;
  filter->min_crop_width = 1;
  filter->min_crop_height = 1;
  filter->x_divisible_by = 1;
  filter->y_divisible_by = 1;
  filter->width_divisible_by = 1;
  filter->height_divisible_by = 1;
  filter->cropped_obj_frame_interval = 1;
  filter->frame_count = 0;
  filter->objid_hash = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void
gst_video_roi_crop_internal_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  //VideoRoiCropInternal *filter = GST_VIDEOROICROPINTERNAL (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_roi_crop_internal_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  //VideoRoiCropInternal *filter = GST_VIDEOROICROPINTERNAL (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

/* this function handles sink events */
static gboolean
gst_video_roi_crop_internal_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  VideoRoiCropInternal *filter;
  GstStructure *structure;
  gint width = 0, height = 0;

  filter = GST_VIDEOROICROPINTERNAL (parent);

  GST_LOG_OBJECT (filter, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps * caps;
      gst_event_parse_caps (event, &caps);
      structure = gst_caps_get_structure (caps, 0);
      if (!gst_structure_get_int (structure, "width", &width))
        return FALSE;
      if (!gst_structure_get_int (structure, "height", &height))
        return FALSE;

      filter->inputwidth = width;
      filter->inputheight = height;
    }
    break;
    case GST_EVENT_EOS:
    {
       //reset frame count & object-id hash (i.e. forget these object_id's) upon EOS.
       filter->frame_count = 0;
       g_hash_table_remove_all(filter->objid_hash);
    }
    break;
    default:
      break;
  }
  return gst_pad_event_default (pad, parent, event);
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
gst_video_roi_crop_internal_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  VideoRoiCropInternal *filter;

  filter = GST_VIDEOROICROPINTERNAL (parent);

  GstFlowReturn ret = GST_FLOW_OK;

  //increment it first thing, as we use it as a value in our hash, and so we
  // don't ever want to add a value of 0 (as we wouldn't be able to distinguish
  // this from the return val of g_hash_table_lookup )
  filter->frame_count++;

  GstVideoRegionOfInterestMeta * meta = NULL;
  gpointer state = NULL;
  while( (meta = (GstVideoRegionOfInterestMeta *)gst_buffer_iterate_meta_filtered(
                                buf,
                                &state,
                                gst_video_region_of_interest_meta_api_get_type())) )
  {
       if( filter->parentmetapad && GST_PAD_IS_LINKED(filter->parentmetapad) )
       {
          GstBuffer *meta_buffer = gst_buffer_new ();
          GST_BUFFER_PTS (meta_buffer) = GST_BUFFER_PTS(buf);
          GST_BUFFER_DTS (meta_buffer) = GST_BUFFER_DTS(buf);
          GST_BUFFER_DURATION (meta_buffer) = GST_BUFFER_DURATION(buf);
          GST_BUFFER_OFFSET (meta_buffer) = GST_BUFFER_OFFSET(buf);
          GST_BUFFER_OFFSET_END (meta_buffer) = GST_BUFFER_OFFSET_END(buf);
          GstVideoRegionOfInterestMeta *meta_new =
              gst_buffer_add_video_region_of_interest_meta_id(
                    meta_buffer,
                    meta->roi_type,
                    meta->x,
                    meta->y,
                    meta->w,
                    meta->h);

          if( meta->params )
          {
             g_list_foreach(meta->params, AddStructureToMeta, meta_new);
          }

          ret = gst_pad_push (filter->srcpadmeta, meta_buffer);

          if( ret != GST_FLOW_OK )
          {
             GST_ERROR("gst_pad_push(meta) returned bad status: %d", ret);
          }
       }

       if( filter->cropped_obj_frame_interval != 1 )
       {
          GstStructure *object_id_struct =
             gst_video_region_of_interest_meta_get_param(meta, "object_id");

          if( object_id_struct )
          {
             int id = 0;
             gst_structure_get_int(object_id_struct, "id", &id);
             if( id )
             {
                //cropped frame interval of 0 implies "forever".. so we only
                // need to check the presence of this id in our hash.
                if( filter->cropped_obj_frame_interval == 0 )
                {
                   //g_hash_table_add returns FALSE if the hash table already contains
                   // the key (the object_id) in this case
                   if( !g_hash_table_add(filter->objid_hash,
                                         GINT_TO_POINTER(id)))
                   {
                      //skip cropping this ROI, we've seen this object_id before..
                      continue;
                   }
                }
                else
                {
                   //attempt to find the frame-count for the last time
                   // this object was cropped. Note, this will return
                   // 0 if this object hasn't been encountered before.
                   gulong cropped_frame_count =
                         (gulong)(g_hash_table_lookup(filter->objid_hash,
                                                      GINT_TO_POINTER(id)));

                   //if we haven't encountered this object before, OR we have seen it
                   // before, but have exceeded the interval
                   if( !cropped_frame_count ||
                       (filter->frame_count-cropped_frame_count >
                        filter->cropped_obj_frame_interval)
                     )
                   {
                      //update the hash, and proceed to crop
                      g_hash_table_insert(filter->objid_hash,
                                          GINT_TO_POINTER(id),
                                          (gpointer)filter->frame_count);
                   }
                   else
                   {
                      //we've seen this object before, but haven't exceeded the interval.
                      // skip cropping this one.
                      continue;
                   }
                }
             }
          }
       }

       guint x = meta->x;
       guint y = meta->y;
       guint w = meta->w;
       guint h = meta->h;

       guint x_divisible_by = filter->x_divisible_by;
       guint y_divisible_by = filter->y_divisible_by;
       guint width_divisible_by = filter->width_divisible_by;
       guint height_divisible_by = filter->height_divisible_by;
       if( G_UNLIKELY(!x_divisible_by || !y_divisible_by || !width_divisible_by || !height_divisible_by))
       {
          GST_ERROR_OBJECT(filter, "invalid \"divisible-by\" parameters set");
          ret = GST_FLOW_ERROR;
          break;
       }

       //Make sure x/y crop coordinates are valid, according to
       // currently set "divisible by" parameters.
       // If they aren't, adjust them (decrease x/y), thus
       // increasing width & height.
       if( (x % x_divisible_by) != 0)
       {
          guint new_x = (x / x_divisible_by)*x_divisible_by;
          w = w + (x - new_x);
          x = new_x;
       }

       if( (y % y_divisible_by) != 0 )
       {
          guint new_y = (y / y_divisible_by)*y_divisible_by;
          h = h + (y - new_y);
          y = new_y;
       }

       if( w < filter->min_crop_width )
       {
          w = filter->min_crop_width;
       }

       if( h < filter->min_crop_height )
       {
          h = filter->min_crop_height;
       }

       //if width/height are not divisible by the 'divisible-by' parameters,
       // pad them to next closest value.
       if( (w % width_divisible_by) != 0 )
       {
          w = ((w/width_divisible_by)+1)*width_divisible_by;
       }

       if( (h % height_divisible_by) != 0 )
       {
          h = ((h/height_divisible_by)+1)*height_divisible_by;
       }

       //if the current crop region extends past the image boundary,
       // attempt to adjust the x,y to make it fit.
       if( (x + w) > filter->inputwidth)
       {
          //Of course, we can only do this if the cropped width is <=
          // the entire image width
          if( w <= filter->inputwidth )
          {
             x = filter->inputwidth - w;

             //now, we need to double check that this new x still adheres to the x/y "divisible-by"
             // parameters.
             if( (x % x_divisible_by) != 0 )
             {
                //if it doesn't we just give up on this one. Otherwise, if we adjust x, we'd need
                // to adjust the width, and if we do that we'd need to check validity of the new
                // width (against min width, divisible-by) and so on...and so on...
                GST_WARNING_OBJECT(filter, "Couldn't convert ROI(%u,%u,%ux%u) to valid crop values. Skipping..",
                                   meta->x, meta->y, meta->w, meta->h);
                continue;
             }
          }
          else
          {
             GST_WARNING_OBJECT(filter, "No room to crop ROI(%u,%u,%ux%u) to size %ux%u. Skipping..",
                             meta->x, meta->y, meta->w, meta->h, w, h);
             continue;
          }
       }

       if( (y + h) > filter->inputheight)
       {
          //Of course, we can only do this if the cropped height is <=
          // the entire image height
          if( h <= filter->inputheight )
          {
             y = filter->inputheight - h;

             //now, we need to double check that this new x still adheres to the x/y "divisible-by"
             // parameters.
             if( (y % y_divisible_by) != 0 )
             {
                //if it doesn't we just give up on this one. Otherwise, if we adjust y, we'd need
                // to adjust the height, and if we do that we'd need to check validity of the new
                // height (against min height, divisible-by) and so on...and so on...
                GST_WARNING_OBJECT(filter, "Couldn't convert ROI(%u,%u,%ux%u) to valid crop values. Skipping..",
                                   meta->x, meta->y, meta->w, meta->h);
                continue;
             }
          }
          else
          {
             GST_WARNING_OBJECT(filter, "No room to crop ROI(%u,%u,%ux%u) to size %ux%u. Skipping..",
                             meta->x, meta->y, meta->w, meta->h, w, h);
             continue;
          }
       }

       //note that under *normal* circumstances, this does not perform
       // a copy of the underlying GstMemory.. so this should be
       // a very lightweight operation. We do this because we don't want
       // to modify the original GstBuffer directly.
       GstBuffer *new_buf = gst_buffer_copy(buf);

#if USE_VIDEOCROP
       gint crop_left = x;
       gint crop_right = filter->inputwidth - (x + w);
       gint crop_top = y;
       gint crop_bottom = filter->inputheight - (y + h);

       g_object_set (G_OBJECT (filter->pvideocrop), "left", crop_left, NULL);
       g_object_set (G_OBJECT (filter->pvideocrop), "right", crop_right, NULL);
       g_object_set (G_OBJECT (filter->pvideocrop), "top", crop_top, NULL);
       g_object_set (G_OBJECT (filter->pvideocrop), "bottom", crop_bottom, NULL);
#else
       //if we're not pushing to a videocrop, just set the
       // GstVideoCropMeta directly on the buffer. Let's
       // hope downstream takes care of it...
       GstVideoCropMeta *crop_meta =
             gst_buffer_add_video_crop_meta(new_buf);
       crop_meta->x = x;
       crop_meta->y = y;
       crop_meta->width = w;
       crop_meta->height = h;
#endif
       //remove all existing VideoRoiMeta from this new buffer.
       {
          GstVideoRegionOfInterestMeta * meta1 = NULL;
          gpointer state1 = NULL;
          while( (meta1 = (GstVideoRegionOfInterestMeta *)gst_buffer_iterate_meta_filtered(
                                new_buf,
                                &state1,
                                gst_video_region_of_interest_meta_api_get_type())) )
          {
             gst_buffer_remove_meta(new_buf, (GstMeta *)meta1);
             state1 = NULL;
          }
       }

       {
          //ok, now add back in *this* specific ROI meta.
          GstVideoRegionOfInterestMeta *meta_new =
                 gst_buffer_add_video_region_of_interest_meta_id(
                       new_buf,
                       meta->roi_type,
                       x,
                       y,
                       w,
                       h);

          if( meta->params )
          {
             g_list_foreach(meta->params, AddStructureToMeta, meta_new);
          }
       }

       ret = gst_pad_push (filter->srcpadvideo, new_buf);
       if( ret != GST_FLOW_OK )
       {
          GST_ERROR_OBJECT(filter, "gst_pad_push returned bad status: %d", ret);
          break;
       }
  }

  gst_buffer_unref(buf);
  return ret;
}


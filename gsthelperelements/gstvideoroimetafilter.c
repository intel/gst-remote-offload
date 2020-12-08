/*
 *  gstvideoroimetafilter.c - GstVideoRoiMetaFilter element
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
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <stdio.h>
#include <unistd.h>
#include <gst/video/gstvideometa.h>
#include "gstvideoroimetafilter.h"

typedef struct GstVideoRoiMetaFilterExpression_
{
   // regular expression to compare GstStructure names against
   // If this is NULL, it's treated as a regular expression of ".*"
   GRegex *structure_expr;

   // regular expression to compare GstStructure field names against
   // If this is NULL, it's treated as a regular expression of ".*"
   GRegex *field_expr;
}GstVideoRoiMetaFilterExpression;

typedef struct GstVideoRoiMetaFilterPrivate_
{
   GArray *preserve_expressions;
   GArray *remove_expressions;
}GstVideoRoiMetaFilterPrivate;

GST_DEBUG_CATEGORY_STATIC (gst_video_roi_meta_filter_debug);
#define GST_CAT_DEFAULT gst_video_roi_meta_filter_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_PRESERVE,
  PROP_REMOVE
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

#define gst_video_roi_meta_filter_parent_class parent_class
G_DEFINE_TYPE (GstVideoRoiMetaFilter, gst_video_roi_meta_filter, GST_TYPE_ELEMENT);

static void gst_video_roi_meta_filter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_roi_meta_filter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn
gst_video_roi_meta_filter_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);

/* GObject vmethod implementations */

/* initialize the videoroimetafilter's class */
static void
gst_video_roi_meta_filter_class_init (GstVideoRoiMetaFilterClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_video_roi_meta_filter_set_property;
  gobject_class->get_property = gst_video_roi_meta_filter_get_property;

  g_object_class_install_property (gobject_class, PROP_PRESERVE,
      g_param_spec_string ("preserve",
                           "Preserve",
                           "Comma / semicolon delimited list of perl regular expressions. "
                           "Used to control preservation of GstStructure / fields. "
                           "Non-Matching GstStructure names will cause removal of entire GstStructure. "
                           "Non-Matching GstStructure field names will cause removal of those fields"
                           ,
                           NULL,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_REMOVE,
      g_param_spec_string ("remove",
                           "remove",
                           "Comma / semicolon delimited list of perl regular expressions. "
                           "Used to explicitly remove GstStructures / fields  "
                           "Matching GstStructure names will cause removal of entire GstStructure. "
                           "Matching GstStructure field names will cause removal of those fields",
                           NULL,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_details_simple(gstelement_class,
    "VideoRoiMetaFilter",
    "utilities",
    "Filter VideoRegionOfInterestMeta params (GstStructures) using regular expression parameters",
    "Ryan Metcalfe <ryan.d.metcalfe@intel.com>");

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
gst_video_roi_meta_filter_init (GstVideoRoiMetaFilter * filter)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_video_roi_meta_filter_chain));
  GST_PAD_SET_PROXY_CAPS (filter->sinkpad);
  GST_PAD_SET_PROXY_ALLOCATION(filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  GST_PAD_SET_PROXY_CAPS (filter->srcpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->priv = g_malloc(sizeof(GstVideoRoiMetaFilterPrivate));
  filter->priv->preserve_expressions = NULL;
  filter->priv->remove_expressions = NULL;
}

static inline GRegex* str_to_regex(gchar *pattern)
{
   GError *err = NULL;
   GRegex *regex = g_regex_new(pattern,
                               G_REGEX_OPTIMIZE,
                               (GRegexMatchFlags)0,
                               &err);
   if( !regex )
   {
      GST_ERROR("Problem compiling %s to a regular expression.", pattern);
      if( err )
      {
         if( err->message )
            GST_ERROR("g_regex_new error details: code=%d, message=%s",
                             err->code, err->message);
         g_clear_error(&err);
      }
   }

   return regex;
}


static GArray *
gst_video_roi_meta_filter_build_expression_array(gchar *pattern)
{
   if( pattern && *pattern)
   {
      GArray *exp_array = g_array_new (FALSE,
                                    FALSE,
                                    sizeof(GstVideoRoiMetaFilterExpression));

      //expression format looks something like this:
      //struct_reg_expr0;field_reg_expr0,struct_reg_expr1;field_reg_expr1,(and so on)
      // Multiple struct_name/field_name pairs are separated by commas. So, first
      // thing that we need to do is split this single string into multiple ones,
      // delimited by ,
      gchar **pair_patterns = g_strsplit(pattern, ",", 0);

      //count number of pairs
      guint npairs = g_strv_length(pair_patterns);
      for( guint pairi=0; pairi < npairs; pairi++ )
      {
         //make sure this isn't an empty string
         gchar *thispairpattern = pair_patterns[pairi];

         //if this is an empty string, skip it
         if( !thispairpattern || !(*thispairpattern) )
            continue;

         //within each pair, struct name expression & pair name expressions are
         // delimited by ';'
         gchar **name_field_patterns = g_strsplit(thispairpattern, ";", 0);
         guint nname_field_patterns = g_strv_length(name_field_patterns);
         if( nname_field_patterns > 2 )
         {
            GST_WARNING("%s contains >1 ';' "
                  " Proceeding forward using pattern as %s;%s",
                  thispairpattern, name_field_patterns[0], name_field_patterns[1]);
         }

         gboolean bokay = TRUE;

         GstVideoRoiMetaFilterExpression expr = {NULL,NULL};

         //create a regular expression object,
         // from the structure pattern
         expr.structure_expr = str_to_regex(name_field_patterns[0]);
         if( !expr.structure_expr )
            bokay = FALSE;

         if( bokay && nname_field_patterns >= 2 )
         {
            expr.field_expr = str_to_regex(name_field_patterns[1]);
            if( !expr.field_expr )
               bokay = FALSE;
         }

         if( bokay )
         {
            g_array_append_val(exp_array, expr);
         }

         g_strfreev(name_field_patterns);
      }

      g_strfreev(pair_patterns);

      if( exp_array->len == 0 )
      {
         GstVideoRoiMetaFilterExpression expr = {NULL,NULL};
         g_array_append_val(exp_array, expr);
      }

      return exp_array;
   }

   return NULL;
}

static void gst_video_roi_meta_filter_destroy_expression_array(GArray **array)
{
   if( !array || !(*array)) return;

   for( guint arrayi=0; arrayi < (*array)->len; arrayi++ )
   {
      GstVideoRoiMetaFilterExpression *expr =
            &g_array_index(*array, GstVideoRoiMetaFilterExpression, arrayi);

      if( expr->structure_expr )
         g_regex_unref(expr->structure_expr);

      if( expr->field_expr )
         g_regex_unref(expr->field_expr);
   }

   g_array_free(*array, TRUE);
   *array = NULL;
}


static void
gst_video_roi_meta_filter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoRoiMetaFilter *filter = GST_VIDEOROIMETAFILTER (object);

  switch (prop_id) {
    case PROP_PRESERVE:
      if( filter->preserve_filter )
           g_free (filter->preserve_filter);
        filter->preserve_filter = g_strdup (g_value_get_string (value));
        gst_video_roi_meta_filter_destroy_expression_array(&filter->priv->preserve_expressions);
        filter->priv->preserve_expressions = gst_video_roi_meta_filter_build_expression_array(filter->preserve_filter);
      break;
    case PROP_REMOVE:
      if( filter->remove_filter )
           g_free (filter->remove_filter);
        filter->remove_filter = g_strdup (g_value_get_string (value));
        gst_video_roi_meta_filter_destroy_expression_array(&filter->priv->remove_expressions);
        filter->priv->remove_expressions = gst_video_roi_meta_filter_build_expression_array(filter->remove_filter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_roi_meta_filter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoRoiMetaFilter *filter = GST_VIDEOROIMETAFILTER (object);

  switch (prop_id) {
    case PROP_PRESERVE:
      g_value_set_string (value, filter->preserve_filter);
      break;
    case PROP_REMOVE:
      g_value_set_string (value, filter->remove_filter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

static gboolean remove_structure_field_routine (GQuark   field_id,
                                                  GValue * value,
                                                  gpointer user_data)
{
   GstVideoRoiMetaFilterExpression *expr = (GstVideoRoiMetaFilterExpression *)user_data;

   const gchar *fieldname = g_quark_to_string(field_id);
   return !g_regex_match(expr->field_expr,
                         fieldname,
                         (GRegexMatchFlags)0,
                         NULL);
}

static gboolean preserve_structure_field_routine (GQuark   field_id,
                                                  GValue * value,
                                                  gpointer user_data)
{
   GstVideoRoiMetaFilterExpression *expr = (GstVideoRoiMetaFilterExpression *)user_data;

   const gchar *fieldname = g_quark_to_string(field_id);
   return g_regex_match(expr->field_expr,
                        fieldname,
                        (GRegexMatchFlags)0,
                        NULL);
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_video_roi_meta_filter_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstVideoRoiMetaFilter *self;

  self = GST_VIDEOROIMETAFILTER (parent);
  GstVideoRegionOfInterestMeta *meta = NULL;
  gpointer state = NULL;
  while( (meta = (GstVideoRegionOfInterestMeta *)
          gst_buffer_iterate_meta_filtered(buf,
                                           &state,
                                           gst_video_region_of_interest_meta_api_get_type())) )
  {
     //Filter GstStructures params based on preserve expressions
     if( self->priv->preserve_expressions )
     {
        GList *params = meta->params;
        while( params != NULL )
        {
           GstStructure *s = (GstStructure *)params->data;
           if( s )
           {
              gboolean bremoveentirestruct = TRUE;
              const gchar* structure_name = gst_structure_get_name(s);
              for( guint arrayi = 0; arrayi < self->priv->preserve_expressions->len; arrayi++ )
              {
                 GstVideoRoiMetaFilterExpression *expr =
                        &g_array_index(self->priv->preserve_expressions,
                                       GstVideoRoiMetaFilterExpression,
                                       arrayi);

                 if( !expr->structure_expr || g_regex_match(expr->structure_expr,
                                                            structure_name,
                                                            (GRegexMatchFlags)0,
                                                            NULL) )
                 {
                    bremoveentirestruct = FALSE;

                    if( expr->field_expr )
                    {
                       //now, dive into this specific structure and filter out fields
                       gint nfields_before = gst_structure_n_fields(s);
                       gst_structure_filter_and_map_in_place(s,
                                                             preserve_structure_field_routine,
                                                             expr);
                       gint nfields_after = gst_structure_n_fields(s);

                       //if *we* removed all fields from the structure, let's remove the entire
                       // structure.
                       if( nfields_before && !nfields_after )
                       {
                          bremoveentirestruct = TRUE;
                          break;
                       }
                    }
                 }
              }

              if( bremoveentirestruct )
              {
                //this structure name didn't match expression from any of our preserve
                // expressions. So, remove it.
                gst_structure_free(s);
                GList *todelete = params;
                params = params->next;
                meta->params = g_list_delete_link(meta->params, todelete);
              }
              else
              {
                 params = params->next;
              }
           }
           else
           {
              params = params->next;
           }
        }
     }

     //Filter GstStructures params based on remove expressions
     if( self->priv->remove_expressions )
     {
        GList *params = meta->params;
        while( params != NULL )
        {
           GstStructure *s = (GstStructure *)params->data;
           if( s )
           {
              gboolean bremoveentirestruct = FALSE;
              const gchar* structure_name = gst_structure_get_name(s);
              for( guint arrayi = 0; arrayi < self->priv->remove_expressions->len; arrayi++ )
              {
                 GstVideoRoiMetaFilterExpression *expr =
                        &g_array_index(self->priv->remove_expressions,
                                       GstVideoRoiMetaFilterExpression,
                                       arrayi);

                 if(  !expr->structure_expr || g_regex_match(expr->structure_expr,
                                                             structure_name,
                                                             (GRegexMatchFlags)0,
                                                             NULL) )
                 {
                    if( expr->field_expr )
                    {
                       //now, dive into this specific structure and filter out fields
                       gint nfields_before = gst_structure_n_fields(s);
                       gst_structure_filter_and_map_in_place(s,
                                                             remove_structure_field_routine,
                                                             expr);
                       gint nfields_after = gst_structure_n_fields(s);

                       //if *we* removed all fields from the structure, let's remove the entire
                       // structure.
                       if( nfields_before && !nfields_after )
                       {
                          bremoveentirestruct = TRUE;
                          break;
                       }
                    }
                    else
                    {
                       bremoveentirestruct = TRUE;
                       break;
                    }
                 }
              }

              if( bremoveentirestruct )
              {
                //this structure name didn't match expression from any of our preserve
                // expressions. So, remove it.
                gst_structure_free(s);
                GList *todelete = params;
                params = params->next;
                meta->params = g_list_delete_link(meta->params, todelete);
              }
              else
              {
                 params = params->next;
              }
           }
           else
           {
              params = params->next;
           }
        }
     }
  }

  return gst_pad_push (self->srcpad, buf);
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
videoroimetafilter_init (GstPlugin * videoroimetafilter)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template videoroimetafilter' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_video_roi_meta_filter_debug, "videoroimetafilter",
      0, "Template videoroimetafilter");

  return gst_element_register (videoroimetafilter, "videoroimetafilter", GST_RANK_NONE,
      GST_TYPE_VIDEOROIMETAFILTER);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstvideoroimetafilter"
#endif

/* Version number of package */
#define VERSION "1.0.0"

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    videoroimetafilter,
    "Filter VideoRegionOfInterestMeta params (GstStructures) using regular expression parameters",
    videoroimetafilter_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)



/*
 *  gstsublaunch.c - SubLaunch element
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
#include "gstsublaunch.h"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink:%s",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src:%s",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_sublaunch_debug);
#define GST_CAT_DEFAULT gst_sublaunch_debug


/* Filter signals and args */
enum
{
  /* actions */
  SIGNAL_POPULATE_PARENT,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_LAUNCHSTRING,
};


#define gst_sublaunch_parent_class parent_class

G_DEFINE_TYPE_WITH_CODE (SubLaunch, gst_sublaunch, GST_TYPE_BIN,
  GST_DEBUG_CATEGORY_INIT (gst_sublaunch_debug, "sublaunch", 0,
  "debug category for sublaunch"));

static void gst_sublaunch_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_sublaunch_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


static GstPad *gst_sublaunch_request_new_pad (GstElement * element,
    GstPadTemplate * temp, const gchar* name, const GstCaps * caps);
static void gst_sublaunch_release_pad (GstElement * element, GstPad * pad);

static GstStateChangeReturn gst_sublaunch_change_state (GstElement *
    element, GstStateChange transition);

static guint gst_sublaunch_signals[LAST_SIGNAL] = { 0 };

static gboolean gst_sublaunch_populate_parent(SubLaunch *sublaunch);

/* initialize the gst_sublaunch class */
static void
gst_sublaunch_class_init (SubLaunchClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_sublaunch_set_property;
  gobject_class->get_property = gst_sublaunch_get_property;

  g_object_class_install_property (gobject_class, PROP_LAUNCHSTRING,
      g_param_spec_string ("launch-string",
                           "LaunchString",
                           "gst-launch-1.0 style string",
                           NULL,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

   gst_sublaunch_signals[SIGNAL_POPULATE_PARENT] =
      g_signal_new ("populate-parent", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (SubLaunchClass, populate_parent),
      NULL, NULL, NULL, G_TYPE_BOOLEAN, 0, G_TYPE_NONE);

  gst_element_class_set_details_simple(gstelement_class,
    "GVACropRoi",
    "FIXME:Generic",
    "FIXME:Generic Template Element",
    "Ryan Metcalfe <<Ryan.D.Metcalfe@intel.com>>");

  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);
  gst_element_class_add_static_pad_template (gstelement_class, &src_template);

  gstelement_class->change_state =
        GST_DEBUG_FUNCPTR (gst_sublaunch_change_state);

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_sublaunch_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_sublaunch_release_pad);

  klass->populate_parent = gst_sublaunch_populate_parent;
}

//This function will return the element_name & pad_name,
// given a string 'name' of the format:
// "src:element_name:pad_name"
// or
// "sink:element_name:pad_name"
// It's possible that element_name, or pad_name are not specified in 'name',
// some examples:
// "sink:", "src:", "sink:tee", "sink:tee:"
// In these cases, the returned element_name and/or pad_name is "any"
// The caller is responsible for calling g_free on returned strings.
static void pad_name_details(const gchar *name, gchar **element_name, gchar **pad_name)
{
   if( !name || !element_name || !pad_name )
      return;

   gchar *src_sink = NULL;
   gchar *element = NULL;
   gchar *pad = NULL;

   gchar **tokens = g_strsplit(name,":",-1);
   if( tokens )
   {
      gint tokeni = 0;
      gchar *token = tokens[tokeni++];
      while( (token != NULL) && (*token) )
      {
         if( !src_sink )
            src_sink = token;
         else
         if( !element )
            element = g_strdup(token);
         else
         {
            pad = g_strdup(token);
            break;
         }
         token = tokens[tokeni++];
      }
      g_strfreev(tokens);
   }

   if( !element || (g_strcmp0(element, "%s")==0) )
      element = g_strdup("any");

   if( !pad || (g_strcmp0(pad, "%s")==0))
      pad = g_strdup("any");

   *element_name = element;
   *pad_name = pad;
}

static GstPad *
gst_sublaunch_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name_templ, const GstCaps * caps)
{
   SubLaunch *self = GST_SUBLAUNCH (element);

   if( !templ || (templ->direction != GST_PAD_SINK
                 && templ->direction != GST_PAD_SRC)
     )
   {
      GST_ERROR_OBJECT(element, "Invalid template");
      return NULL;
   }

   GST_OBJECT_LOCK (self);
   GstPad *pad;
   gchar* padname = NULL;
   if( name_templ )
   {
      gchar* element_token = NULL;
      gchar* pad_token = NULL;
      pad_name_details(name_templ, &element_token, &pad_token);
      //if pad_token is "any", we need to add an index to it, in order
      // to guarantee that this pad name is unique.
      if( g_strcmp0(pad_token, "any") == 0 )
      {
         g_free(pad_token);
         if( templ->direction == GST_PAD_SINK )
            pad_token = g_strdup_printf("any%u", self->sinkpad_index++);
         else
            pad_token = g_strdup_printf("any%u", self->srcpad_index++);
      }

      if( templ->direction == GST_PAD_SINK )
         padname = g_strdup_printf("sink:%s:%s", element_token, pad_token);
      else
         padname = g_strdup_printf("src:%s:%s", element_token, pad_token);

      g_free(element_token);
      g_free(pad_token);
   }
   else
   {
      if( templ->direction == GST_PAD_SINK )
         padname = g_strdup_printf("sink:any:any%u", self->sinkpad_index++);
      else
         padname = g_strdup_printf("src:any:any%u", self->srcpad_index++);
   }
   GST_OBJECT_UNLOCK (self);

   pad = gst_ghost_pad_new_no_target_from_template(padname, templ);
   if( pad )
   {
      if( gst_element_add_pad (element, pad) )
      {
         GST_OBJECT_LOCK (self);
         //add this new pad to the list. We'll give it a target during NULL->READY
         self->ghost_pad_list = g_list_append(self->ghost_pad_list,
                                              pad);
         GST_OBJECT_UNLOCK (self);
      }
      else
      {
         GST_ERROR_OBJECT(element, "gst_element_add_pad failed for pad %s", padname);
         gst_object_unref(pad);
         pad = NULL;
      }
   }
   else
   {
      GST_ERROR_OBJECT(element, "gst_ghost_pad_new_no_target_from_template failed for pad %s",
                       padname);
   }
   g_free(padname);

   return pad;
}

static void
gst_sublaunch_release_pad (GstElement * element, GstPad * pad)
{
   SubLaunch *self = GST_SUBLAUNCH (element);

   GST_OBJECT_LOCK (self);
   self->ghost_pad_list = g_list_remove(self->ghost_pad_list,
                                        pad);
   GST_OBJECT_UNLOCK (self);

   gst_element_remove_pad (GST_ELEMENT_CAST (self), pad);
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_sublaunch_init (SubLaunch * self)
{
  self->launchstr = NULL;
  self->ghost_pad_list = NULL;
  self->sinkpad_index = 0;
  self->srcpad_index = 0;
  self->bparent_populated = FALSE;
}


static void
gst_sublaunch_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  SubLaunch *self = GST_SUBLAUNCH (object);

  switch (prop_id)
  {
    case PROP_LAUNCHSTRING:
      if (!g_value_get_string (value))
      {
          g_warning ("comms property cannot be NULL");
          break;
      }
      if( self->launchstr )
        g_free (self->launchstr);
      self->launchstr = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_sublaunch_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  SubLaunch *self = GST_SUBLAUNCH (object);

  switch (prop_id)
  {
    case PROP_LAUNCHSTRING:
      g_value_set_string (value, self->launchstr);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */
static gboolean gst_sublaunch_populate_parent(SubLaunch *self)
{
   if( !self->bparent_populated )
   {
      if( !self->launchstr )
      {
         GST_ERROR_OBJECT(self, "Launch String is NULL!");
         return FALSE;
      }

      //Convert this launch string to a "real" bin
      GError *error = NULL;
      GstElement *parsed_pipeline = gst_parse_launch_full(self->launchstr,
                                                          NULL,
                                                          GST_PARSE_FLAG_FATAL_ERRORS |
                                                          GST_PARSE_FLAG_PLACE_IN_BIN,
                                                          &error);
      if( !parsed_pipeline )
      {
         if( error && error->message)
         {
           GST_ERROR_OBJECT(self, "Error parsing launch-string. Error: %s", error->message);
         }
         else
         {
            GST_ERROR_OBJECT(self, "Error parsing launch-string. Unknown error.");
         }

         return FALSE;
      }

      gboolean bsingle_element_bin = FALSE;
      gchar *single_element_name = NULL;
      //if the launch string specifies exactly one element, gst_parse_launch_full
      // will return a GstElement, not a GstBin. So, just create a new bin
      if( !GST_IS_BIN(parsed_pipeline) )
      {
         bsingle_element_bin = TRUE;
         single_element_name = g_strdup(GST_ELEMENT_NAME(parsed_pipeline));
         gchar *bin_name = g_strdup_printf("%s_bin",
                                           GST_ELEMENT_NAME((GstElement*)self));
         GstElement *new_bin = gst_bin_new(bin_name);
         gst_bin_add(GST_BIN(new_bin), parsed_pipeline);
         parsed_pipeline = new_bin;
      }

      self->subpipeline = parsed_pipeline;

      if( !gst_bin_add(GST_BIN(self), self->subpipeline) )
      {
         GST_ERROR_OBJECT(self, "Error adding subpipeline");
         return FALSE;
      }

      //connect our proxy pads to pads found within the subpipeline
      for( GList *ghost_it = self->ghost_pad_list; ghost_it != NULL; ghost_it = ghost_it->next)
      {
         GstPad *ghost_pad = ghost_it->data;

         gchar* element_token = NULL;
         gchar* pad_token = NULL;
         pad_name_details(GST_PAD_NAME(ghost_pad), &element_token, &pad_token);
         if( !element_token || !pad_token )
         {
            GST_ERROR_OBJECT(self, "pad_name_details for ghost pad name: %s",
                             GST_PAD_NAME(ghost_pad));
            return GST_STATE_CHANGE_FAILURE;
         }

         if( bsingle_element_bin )
         {
            //if this is a single element bin, change the element_token to
            // the name of the element. With this, we can provide better support
            // as this will enable the path which can request pads. Otherwise,
            // the default path for "any" will only search for static pads.
            g_free(element_token);
            element_token = g_strdup(single_element_name);
         }

         GstPad *unlinked_pad = NULL;
         if( g_strcmp0(element_token, "any") == 0 )
         {
            //if element_token is "any", we will simply find the first unlinked pad within the
            // subpipeline that has matching direction.
            unlinked_pad =
               gst_bin_find_unlinked_pad(GST_BIN(self->subpipeline),
                                         gst_pad_get_direction(ghost_pad));

         }
         else
         {
            //If element_token isn't "any", then we will look for a specific element within the
            // subpipeline that has this name.
            GstElement *found_element = gst_bin_get_by_name(GST_BIN(self->subpipeline),
                                                            element_token);
            if( !found_element )
            {
               GST_ERROR_OBJECT(self,
                                "unable to find element named \"%s\" within subpipeline0",
                                 element_token);
               return FALSE;
            }

            //now check the pad token
            if( g_str_has_prefix(pad_token, "any") )
            {
               //the trick we use here is to create a temporary pad of the
               // opposite direction, and then use gst_element_get_compatible_pad
               // to acquire some unlinked pad of the element. That method will
               // automatically search static pads, as well as attempting to
               // request a pad if needed.
               GstStaticPadTemplate *temp;
               if( gst_pad_get_direction(ghost_pad) == GST_PAD_SINK )
                  temp = &src_template;
               else
                  temp = &sink_template;

               GstPad *tmppad = gst_pad_new_from_static_template (temp, NULL);

               unlinked_pad = gst_element_get_compatible_pad(found_element,
                                                             tmppad,
                                                             NULL);
               gst_object_unref(tmppad);
            }
            else
            {
               //corner case... unfortunately, if the pad name that the user wants to
               // match has a '_' in it, it messes things up. As a workaround, the user
               // can use '-' instead, and we'll replace it with '_' right here.
               gchar *hyphen = g_strrstr(pad_token, "-");
               if( hyphen )
                  *hyphen = '_';

               //in this case, we are looking for a specific pad name.
               //first, check static pads
               unlinked_pad = gst_element_get_static_pad(found_element, pad_token);

               if( !unlinked_pad )
               {
                  //okay, no static pad exists. Try to request it.
                  unlinked_pad = gst_element_get_request_pad(found_element, pad_token);
               }

               if( !unlinked_pad )
               {
                  GST_ERROR_OBJECT(self,
                                   "Unable to obtain pad for element \"%s\", named \"%s\"",
                                   element_token, pad_token);
               }
            }

            gst_object_unref(found_element);
         }

         g_free(element_token);
         g_free(pad_token);

         if( !unlinked_pad )
         {
            GST_ERROR_OBJECT(self, "Unable to find an unlinked pad for ghost pad %s",
                             GST_PAD_NAME(ghost_pad));
            return FALSE;
         }

         if( !gst_ghost_pad_set_target(GST_GHOST_PAD(ghost_pad), unlinked_pad) )
         {
            GST_ERROR_OBJECT(self, "Unable to set target of ghost pad %s, to unlinked pad %s",
                             GST_PAD_NAME(ghost_pad),
                             GST_PAD_NAME(unlinked_pad));
            return FALSE;
         }

         gst_object_unref(unlinked_pad);
      }

      if( single_element_name )
         g_free(single_element_name);

      self->bparent_populated = TRUE;
   }
   else
   {
      GST_INFO_OBJECT(self, "Parent has already been populated.");
   }

   return TRUE;
}

static GstStateChangeReturn gst_sublaunch_change_state (GstElement *
    element, GstStateChange transition)
{
   SubLaunch *self = GST_SUBLAUNCH (element);

   switch(transition)
   {
      case GST_STATE_CHANGE_NULL_TO_READY:
      {
         if( !gst_sublaunch_populate_parent(self) )
         {
            GST_ERROR_OBJECT(self, "gst_sublaunch_populate_parent failed!");
            return FALSE;
         }
      }
      break;

      default:
      break;
   }

   GstStateChangeReturn ret = GST_ELEMENT_CLASS (gst_sublaunch_parent_class)->change_state
         (element, transition);

   return ret;
}



/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
sublaunch_init (GstPlugin * sublaunch)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template gvadetectionmetadetach' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_sublaunch_debug, "sublaunch",
      0, "Template sublaunch");

  return gst_element_register (sublaunch, "sublaunch", GST_RANK_NONE,
      GST_TYPE_SUBLAUNCH);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstsublaunch"
#endif

/* Version number of package */
#define VERSION "1.0.0"


/* gstreamer looks for this structure to register sublaunch
 *
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    sublaunch,
    "Template sublaunch",
    sublaunch_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)






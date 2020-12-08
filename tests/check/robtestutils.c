/*
 *  robtestutils.c - remoteoffloadbin test utilities
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
#include <gst/check/gstcheck.h>
#include <gst/check/gstconsistencychecker.h>
#include <gst/app/gstappsink.h>
#include "appsinkcomparer.h"
#include "robtestutils.h"

typedef struct
{
   GstElement *pipeline;
   GMainLoop *loop;
   gboolean bokay;
   gboolean bdont_fail_on_error;
   GstState stateAfterEOS;
}PipelineInstanceEntry;

static gboolean
bus_call (GstBus     *bus,
          GstMessage *msg,
          gpointer    data)
{
  PipelineInstanceEntry *entry = (PipelineInstanceEntry *) data;

  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
      g_main_loop_quit (entry->loop);
      break;

    case GST_MESSAGE_ERROR: {
      gchar  *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);

      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);

      entry->bokay = FALSE;
      g_main_loop_quit (entry->loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}


static gpointer pipeline_instance(PipelineInstanceEntry *entry)
{
   entry->bokay = TRUE;

   GMainLoop *loop = g_main_loop_new (NULL, FALSE);
   fail_unless(loop != NULL);

   entry->loop = loop;

   GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (entry->pipeline));
   fail_unless(bus != NULL);

   guint bus_watch_id = gst_bus_add_watch (bus, bus_call, entry);
   fail_unless(bus_watch_id != 0);
   gst_object_unref (bus);

   if( gst_element_set_state (entry->pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE )
   {
      g_main_loop_run (loop);

      gst_element_set_state (entry->pipeline, entry->stateAfterEOS);

      if( entry->stateAfterEOS == GST_STATE_READY )
      {
         if( gst_element_set_state (entry->pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE )
         {
            g_main_loop_run (loop);

            gst_element_set_state (entry->pipeline, GST_STATE_NULL);
         }
         else
         {
            if( !entry->bdont_fail_on_error )
            {
               fail_unless(FALSE);
            }
         }
      }

      g_source_remove (bus_watch_id);
      g_main_loop_unref (loop);

      if( !entry->bdont_fail_on_error )
         fail_unless(entry->bokay == TRUE);
   }
   else
   {
      if( !entry->bdont_fail_on_error )
      {
         fail_unless(FALSE);
      }
   }

   return NULL;
}

static void mismatch_callback(AppSinkComparer *appsinkcomparer, gpointer user_data)
{
   GST_ERROR("mismatch!");
   fail_unless(FALSE);
}


static GstElement* str_to_pipeline(const gchar *pipeline_str)
{
   GError *error = NULL;
   GstElement *parsed_pipeline = gst_parse_launch(pipeline_str, &error);
   fail_unless(parsed_pipeline != NULL);

   if( !GST_IS_PIPELINE(parsed_pipeline) )
   {
      if( GST_IS_BIN(parsed_pipeline) )
      {
         GstElement *pipeline = gst_pipeline_new("my_pipeline");
         fail_unless(pipeline != NULL);

         fail_unless(gst_bin_add(GST_BIN(pipeline), parsed_pipeline));

         parsed_pipeline = pipeline;
      }
      else
      {
         fail_unless(FALSE);
      }
   }

   return parsed_pipeline;
}

static GArray* gen_appsink_comparer_array(GstElement *pipeline0, GstElement *pipeline1)
{
   GArray *appsinkcomparer_array = g_array_new (FALSE,
                                   FALSE,
                                   sizeof(AppSinkComparer *));

   GstIterator *it = gst_bin_iterate_elements(GST_BIN(pipeline0));
   gboolean done = FALSE;
   GValue item = G_VALUE_INIT;
   while(!done)
   {
      switch(gst_iterator_next(it, &item))
      {
         case GST_ITERATOR_OK:
         {
            GstElement *pElem = (GstElement *)g_value_get_object(&item);
            fail_unless(pElem != NULL);

            //if this is an appsink
            if( GST_IS_APP_SINK(pElem) )
            {
               //create a new AppSinkComparer
               AppSinkComparer *appsinkcomparer = appsink_comparer_new();
               fail_unless(appsinkcomparer != NULL);

               g_signal_connect (appsinkcomparer,
                                 "mismatch", (GCallback) mismatch_callback,
                                 NULL);

               //find the eqivelant appsink from the "native" pipeline
               GstElement *native_appsink =
                     gst_bin_get_by_name(GST_BIN(pipeline1), GST_ELEMENT_NAME(pElem));

               fail_unless(native_appsink != NULL);

               //and register it
               fail_unless(appsink_comparer_register(appsinkcomparer,
                                         native_appsink));

               g_array_append_val(appsinkcomparer_array, appsinkcomparer);

               //register this one (the one from rob_pipeline)
               // with the AppSinkComparer
               fail_unless(appsink_comparer_register(appsinkcomparer,
                                         pElem));

            }
         }
         break;

         case GST_ITERATOR_DONE:
            done = TRUE;
            break;

         case GST_ITERATOR_RESYNC:
         case GST_ITERATOR_ERROR:
            fail_unless(FALSE);
            break;
      }
   }

   g_value_unset(&item);
   gst_iterator_free(it);

   return appsinkcomparer_array;
}


//given a pipeline string, generate:
// 1. The rob-enabled GstPipeline (rob_pipelineoutput)
// 2. The "native" GstPipeline (native_pipelineoutput)
// 3. The appsink comparer array (appsinkcomparer_array_output)
// All output should be set, but appsinkcomparer array may have
//  array size of 0 if no appsink's were found in the pipeline.
static gboolean gen_test_pipelines(const gchar *pipeline_str,  //input
                                   GstElement **rob_pipelineoutput, //output
                                   GstElement **native_pipelineoutput, //output
                                   GArray **appsinkcomparer_array_output) //output
{
   if( !rob_pipelineoutput || !native_pipelineoutput || !appsinkcomparer_array_output )
      return FALSE;

   //given the pipeline string, build the remoteoffload bin pipeline
   GstElement *rob_pipeline = str_to_pipeline(pipeline_str);
   fail_unless(rob_pipeline != NULL);

   *rob_pipelineoutput = rob_pipeline;

   //generate the "native" pipeline string, basically by replacing all
   // occurances of "remoteoffloadbin.(" with "bin.("
   gchar **native_pipeline_str_tokens = g_strsplit(pipeline_str,
                                            " ",
                                            -1);

   int i = 0;
   while(native_pipeline_str_tokens[i] != NULL)
   {
      if( g_strcmp0(native_pipeline_str_tokens[i], "remoteoffloadbin.(") == 0 )
      {
         g_free(native_pipeline_str_tokens[i]);
         native_pipeline_str_tokens[i] = g_strdup("bin.(");
      }
      i++;
   }

   gchar *native_pipeline_str = g_strjoinv(" ",
                                           native_pipeline_str_tokens);

   g_strfreev(native_pipeline_str_tokens);

   GstElement *native_pipeline = str_to_pipeline(native_pipeline_str);
   fail_unless(native_pipeline != NULL);
   g_free(native_pipeline_str);

   *native_pipelineoutput = native_pipeline;

   GArray *appsinkcomparer_array =
         gen_appsink_comparer_array(rob_pipeline, native_pipeline);

   *appsinkcomparer_array_output = appsinkcomparer_array;

   return TRUE;
}

gboolean force_videotestsrc_is_live(GstBin *bin)
{
   gboolean ret = TRUE;
   gboolean restart;

   do
   {
      GstIterator *it = gst_bin_iterate_elements(bin);
      GValue item = G_VALUE_INIT;
      gboolean done = FALSE;

      restart = FALSE;

      while(!done)
      {
         switch(gst_iterator_next(it, &item))
         {
            case GST_ITERATOR_OK:
            {
               GstElement *pElem = (GstElement *)g_value_get_object(&item);
               if( !pElem ) return FALSE;

               gchar *factoryname = gst_plugin_feature_get_name(
                                   GST_PLUGIN_FEATURE(gst_element_get_factory(pElem)));

               if( g_strcmp0(factoryname, "videotestsrc") == 0 )
               {
                  g_object_set(pElem, "is-live", TRUE, NULL);

                  //When videotestsrc is set to LIVE mode, it will produce buffers
                  // syncronized to the pipeline clock at a default rate of 30 FPS.
                  // Nothing really wrong with that, but it makes tests take a long
                  // time. So, we add a capsfilter just after videotestsrc which forces
                  // the framerate to be 120 FPS.. just to speed these pipelines up a
                  // bit (and avoid test failures simply due to timeout).

                  GstObject *parent = gst_element_get_parent(pElem);
                  if( !parent || !GST_IS_BIN(parent) )
                  {
                     return FALSE;
                  }

                  gchar name[128];
                  gchar *elementname = gst_element_get_name(pElem);
                  g_snprintf(name, 128, "%s_capsfilter", elementname);
                  g_free(elementname);

                  GstElement *existing = gst_bin_get_by_name(GST_BIN(parent), name);
                  if( existing )
                  {
                     gst_object_unref(parent);
                     gst_object_unref(existing);
                     break;
                  }

                  GstElement *capsfilter = gst_element_factory_make("capsfilter", name);
                  if( !capsfilter )
                     return FALSE;

                  GstCaps *caps = gst_caps_new_simple ("video/x-raw",
                                                       "framerate", GST_TYPE_FRACTION, 120, 1,
                                                       NULL);

                  g_object_set(capsfilter, "caps", caps, NULL);
                  gst_caps_unref(caps);

                  GstPad *videotestsrc_pad = gst_element_get_static_pad(pElem, "src");
                  if( !videotestsrc_pad )
                     return FALSE;

                  GstPad *peerpad = gst_pad_get_peer(videotestsrc_pad);
                  if( !peerpad )
                     return FALSE;

                  if( !gst_pad_unlink(videotestsrc_pad, peerpad ) )
                     return FALSE;

                  if( !gst_bin_add(GST_BIN(parent), capsfilter) )
                    return FALSE;

                  //link our src pad to queue sink
                  GstPad *caps_sink_pad =
                        gst_element_get_static_pad (capsfilter, "sink");

                  if( !caps_sink_pad )
                     return FALSE;

                  if( gst_pad_link (videotestsrc_pad, caps_sink_pad) != GST_PAD_LINK_OK)
                     return FALSE;

                  //link capsfilter src pad to peer pad
                  GstPad *caps_src_pad =
                        gst_element_get_static_pad (capsfilter, "src");

                  if( !caps_src_pad )
                     return FALSE;

                  GstPadLinkReturn ret = gst_pad_link (caps_src_pad, peerpad);
                  if( ret != GST_PAD_LINK_OK)
                  {
                     return FALSE;
                  }

                  gst_object_unref(caps_src_pad);
                  gst_object_unref(caps_sink_pad);
                  gst_object_unref(peerpad);
                  gst_object_unref(videotestsrc_pad);
                  gst_object_unref(parent);

                  restart = TRUE;
                  done = TRUE;
               }
            }
            break;

            case GST_ITERATOR_DONE:
               done = TRUE;
               break;

            case GST_ITERATOR_RESYNC:
               ret = FALSE;
               done = TRUE;
               break;

            case GST_ITERATOR_ERROR:
               ret = FALSE;
               done = TRUE;
               break;
         }
      }

      g_value_unset(&item);
      gst_iterator_free(it);
   } while(restart && ret);

   return ret;
}

//argument is a gst-launch style string that specifies
// pipeline with one (or more) 'remoteoffloadbin.( ) in there someplace,
// as well as one (or more) appsink's at points in which we want to compare
// the output.
gboolean test_rob_pipeline(const gchar *pipeline_str,
                           TestROBPipelineFlags flags)
{
   GstElement *rob_pipeline = NULL;
   GstElement *native_pipeline = NULL;
   GArray *appsinkcomparer_array = NULL;

   if( flags & TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG )
   {
      rob_pipeline = str_to_pipeline(pipeline_str);
      fail_unless(rob_pipeline != NULL);
   }
   else
   {
      fail_unless(gen_test_pipelines(pipeline_str,
                                     &rob_pipeline,
                                     &native_pipeline,
                                     &appsinkcomparer_array));
      fail_unless(rob_pipeline != NULL);
      fail_unless(native_pipeline != NULL);
      fail_unless(appsinkcomparer_array != NULL);
   }

   PipelineInstanceEntry rob_pipeline_entry;
   rob_pipeline_entry.pipeline = rob_pipeline;
   if( flags & TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG )
   {
      rob_pipeline_entry.bdont_fail_on_error = TRUE;
   }
   else
   {
      rob_pipeline_entry.bdont_fail_on_error = FALSE;
   }

   PipelineInstanceEntry native_pipeline_entry;
   native_pipeline_entry.pipeline = native_pipeline;
   native_pipeline_entry.bdont_fail_on_error = FALSE;

   if( flags & TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING )
   {
      rob_pipeline_entry.stateAfterEOS = GST_STATE_READY;
      native_pipeline_entry.stateAfterEOS = GST_STATE_READY;
   }
   else
   {
      rob_pipeline_entry.stateAfterEOS = GST_STATE_NULL;
      native_pipeline_entry.stateAfterEOS = GST_STATE_NULL;
   }

   if( flags & TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE )
   {
      fail_unless(force_videotestsrc_is_live(GST_BIN(rob_pipeline_entry.pipeline)));

      if( !(flags & TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG) )
         fail_unless(force_videotestsrc_is_live(GST_BIN(native_pipeline_entry.pipeline)));
   }



   if( flags & TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG )
   {
      //only run rob pipeline
      pipeline_instance(&rob_pipeline_entry);
   }
   else
   {
      //run the native pipeline
      pipeline_instance(&native_pipeline_entry);

      //then run the rob_pipeline
      pipeline_instance(&rob_pipeline_entry);

      AppSinkComparer **comparers = (AppSinkComparer **)appsinkcomparer_array->data;
      for( guint compareri = 0; compareri < appsinkcomparer_array->len; compareri++ )
      {
         AppSinkComparer *appsinkcomparer = comparers[compareri];

         //flush (finish comparing internal buffers)
         fail_unless(appsink_comparer_flush(appsinkcomparer));

         g_object_unref(appsinkcomparer);
      }

      gst_object_unref (GST_OBJECT (native_pipeline_entry.pipeline));
      g_array_free(appsinkcomparer_array, TRUE);
   }

   gst_object_unref (GST_OBJECT (rob_pipeline_entry.pipeline));

   return TRUE;
}

gboolean test_pipelines_match(const gchar *pipeline_str0,
                              const gchar *pipeline_str1)
{
   GstElement *pipeline0 = str_to_pipeline(pipeline_str0);
   fail_unless(pipeline0 != NULL);

   GstElement *pipeline1 = str_to_pipeline(pipeline_str1);
   fail_unless(pipeline1 != NULL);

   GArray *appsinkcomparer_array =
         gen_appsink_comparer_array(pipeline0, pipeline1);
   fail_unless(appsinkcomparer_array != NULL);

   PipelineInstanceEntry pipeline0entry;
   pipeline0entry.pipeline = pipeline0;
   pipeline0entry.stateAfterEOS = GST_STATE_NULL;
   pipeline0entry.bdont_fail_on_error = FALSE;

   PipelineInstanceEntry pipeline1entry;
   pipeline1entry.pipeline = pipeline1;
   pipeline1entry.stateAfterEOS = GST_STATE_NULL;
   pipeline1entry.bdont_fail_on_error = FALSE;

   //run pipeline0
   pipeline_instance(&pipeline0entry);

   //run pipeline1
   pipeline_instance(&pipeline1entry);

   AppSinkComparer **comparers = (AppSinkComparer **)appsinkcomparer_array->data;
   for( guint compareri = 0; compareri < appsinkcomparer_array->len; compareri++ )
   {
      AppSinkComparer *appsinkcomparer = comparers[compareri];

      //flush (finish comparing internal buffers)
      fail_unless(appsink_comparer_flush(appsinkcomparer));

      g_object_unref(appsinkcomparer);
   }

   gst_object_unref (GST_OBJECT (pipeline0entry.pipeline));
   gst_object_unref (GST_OBJECT (pipeline1entry.pipeline));

   g_array_free(appsinkcomparer_array, TRUE);

   return TRUE;
}


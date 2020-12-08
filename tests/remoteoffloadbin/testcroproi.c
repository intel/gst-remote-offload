/*
 *  testcroproi.c - Detect & Crop ROI Test Application
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
#include <gst/gst.h>
#include <stdio.h>
#include <stdlib.h>
#include "test_common.h"

static gboolean
bus_call (GstBus     *bus,
          GstMessage *msg,
          gpointer    data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_ERROR: {
      gchar  *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);

      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static void
on_pad_added (GstElement *element,
              GstPad     *pad,
              gpointer    data)
{
  GstPad *sinkpad;
  GstElement *destelement = (GstElement *) data;

  sinkpad = gst_element_get_static_pad (destelement, "sink");

  gst_pad_link (pad, sinkpad);

  gst_object_unref (sinkpad);
}

int main(int argc, char *argv[])
{
  gchar *comms = NULL;
  gchar *commsparam = NULL;
  if( GetTestArgs(argc, argv, &comms, &commsparam) < 0 )
  {
     return -1;
  }

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  GstBus *bus;
  guint bus_watch_id;
  GMainLoop *loop;

  loop = g_main_loop_new (NULL, FALSE);

  char *videxamplesenv = getenv("VIDEO_EXAMPLES_DIR");
  if( !videxamplesenv )
  {
     fprintf(stderr, "Error! VIDEO_EXAMPLES_DIR env not set.\n");
     fprintf(stderr, " Set to /path/to/video-examples\n");
     return -1;
  }

  char *modelspathenv = getenv("MODELS_PATH");
  if( !modelspathenv )
  {
     fprintf(stderr, "Error! MODELS_PATH env not set.\n");
     return -1;
  }

  char *gvapluginsenv = getenv("GVA_HOME");
  if( !gvapluginsenv )
  {
     gvapluginsenv = getenv("GVA_PLUGINS");
  }
  if( !gvapluginsenv )
  {
     fprintf(stderr, "Error! GVA_HOME or GVA_PLUGINS env not set.\n");
     fprintf(stderr, " Set to /path/to/gstreamer-plugins\n");
     fprintf(stderr, " (which is the directory created from git clone https://github.intel.com/video-analytics/gstreamer-plugins.git\n");
     return -1;
  }

  gchar *videofilename =  g_strdup_printf( "%s/Fun_at_a_Fair.mp4", videxamplesenv);
  gchar *detectionmodelfilename =  g_strdup_printf( "%s/intel/face-detection-adas-0001/FP32/face-detection-adas-0001.xml", modelspathenv);
  gchar *classifymodel0filename =  g_strdup_printf( "%s/intel/age-gender-recognition-retail-0013/FP32/age-gender-recognition-retail-0013.xml", modelspathenv);
  gchar *classifymodel0procfilename =  g_strdup_printf( "%s/samples/model_proc/age-gender-recognition-retail-0013.json", gvapluginsenv);
  gchar *classifymodel1filename =  g_strdup_printf( "%s/intel/emotions-recognition-retail-0003/FP32/emotions-recognition-retail-0003.xml", modelspathenv);
  gchar *classifymodel1procfilename =  g_strdup_printf( "%s/samples/model_proc/emotions-recognition-retail-0003.json", gvapluginsenv);

  fprintf(stderr, "Using video file = %s\n", videofilename);
  fprintf(stderr, "Using detection model file = %s\n", detectionmodelfilename);
  fprintf(stderr, "Using classify 0 model file = %s\n", classifymodel0filename);
  fprintf(stderr, "Using classify 0 model proc file = %s\n", classifymodel0procfilename);
  fprintf(stderr, "Using classify 1 model file = %s\n", classifymodel1filename);
  fprintf(stderr, "Using classify 1 model proc file = %s\n", classifymodel1procfilename);

  GstElement *filesrc = gst_element_factory_make("filesrc", "myfilesrc");
  g_object_set (G_OBJECT (filesrc), "location", videofilename, NULL);
  GstElement *decodebin = gst_element_factory_make("decodebin", "mydecodebin");
  GstElement *capsfilter0 = gst_element_factory_make("capsfilter", "capsfilter0");
  {
  gchar *capsstr;
  capsstr = g_strdup_printf ("video/x-raw");
  GstCaps *caps = gst_caps_from_string (capsstr);
  g_object_set (capsfilter0, "caps", caps, NULL);
  gst_caps_unref (caps);
  }

  GstElement *capsfilter1 = gst_element_factory_make("capsfilter", "capsfilter1");
  {
     gchar *capsstr;
     capsstr = g_strdup_printf ("video/x-raw");
     GstCaps *caps = gst_caps_from_string (capsstr);
     g_object_set (capsfilter1, "caps", caps, NULL);
     gst_caps_unref (caps);
  }

  GstElement *videoconvert0 = gst_element_factory_make("videoconvert", "myvideoconvert0");

  GstElement *queue0 = gst_element_factory_make("queue", "myqueue0");

  GstElement *gvadetect = gst_element_factory_make("gvadetect", "mygvadetect");
  g_object_set (G_OBJECT (gvadetect), "model", detectionmodelfilename, NULL);

  GstElement *queue1 = gst_element_factory_make("queue", "myqueue1");

  GstElement *gvaclassify0 = gst_element_factory_make("gvaclassify", "mygvaclassify0");
  g_object_set (G_OBJECT (gvaclassify0), "model", classifymodel0filename, NULL);
  g_object_set (G_OBJECT (gvaclassify0), "model-proc", classifymodel0procfilename, NULL);

  GstElement *queue2 = gst_element_factory_make("queue", "myqueue2");

  GstElement *gvaclassify1 = gst_element_factory_make("gvaclassify", "mygvaclassify1");
  g_object_set (G_OBJECT (gvaclassify1), "model", classifymodel1filename, NULL);
  g_object_set (G_OBJECT (gvaclassify1), "model-proc", classifymodel1procfilename, NULL);

  GstElement *queue3 = gst_element_factory_make("queue", "myqueue3");

  GstElement *videoconvert2 = gst_element_factory_make("videoconvert", "myvideoconvert2");
  GstElement *videoconvert3 = gst_element_factory_make("videoconvert", "myvideoconvert3");
  GstElement *gvawatermark = gst_element_factory_make("gvawatermark", "mygvawatermark");
  GstElement *videoroicrop = gst_element_factory_make("videoroicrop", "myvideoroicrop");
  if( !videoroicrop )
  {
     fprintf(stderr, "Error creating videocroproi\n");
     return -1;
  }

  GstElement *fpssink = gst_element_factory_make("ximagesink", "myfpsdisplaysink");
  g_object_set (G_OBJECT (fpssink), "sync", (gboolean)FALSE, NULL);
  g_object_set (G_OBJECT (fpssink), "qos", (gboolean)FALSE, NULL);

#if 1
  GstElement *remoteoffloadbin = gst_element_factory_make("remoteoffloadbin", "myremoteoffloadbin");
  if( !remoteoffloadbin )
  {
     fprintf(stderr, "Error creating remoteoffloadbin\n");
     return -1;
  }

  if( comms )
    g_object_set (remoteoffloadbin, "comms", comms, NULL);

  if( commsparam )
    g_object_set (remoteoffloadbin, "commsparam", commsparam, NULL);

#else
  GstElement *remoteoffloadbin = gst_bin_new( "myremoteoffloadbin");
#endif


  GstElement *pipeline = gst_pipeline_new("my_pipeline");
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);


  gst_bin_add(GST_BIN(pipeline), filesrc);
  gst_bin_add(GST_BIN(pipeline), decodebin);
  gst_bin_add(GST_BIN(pipeline), capsfilter0);
  gst_bin_add(GST_BIN(pipeline), videoconvert0);
  gst_bin_add(GST_BIN(remoteoffloadbin), queue0);
  gst_bin_add(GST_BIN(remoteoffloadbin), gvadetect);
  gst_bin_add(GST_BIN(remoteoffloadbin), queue1);
  gst_bin_add(GST_BIN(remoteoffloadbin), capsfilter1);
  gst_bin_add(GST_BIN(remoteoffloadbin), videoconvert2);
  gst_bin_add(GST_BIN(remoteoffloadbin), gvaclassify0);
  gst_bin_add(GST_BIN(remoteoffloadbin), queue2);
  gst_bin_add(GST_BIN(remoteoffloadbin), gvaclassify1);
  gst_bin_add(GST_BIN(remoteoffloadbin), queue3);
  gst_bin_add(GST_BIN(remoteoffloadbin), gvawatermark);
  gst_bin_add(GST_BIN(remoteoffloadbin), videoroicrop);
  gst_bin_add(GST_BIN(remoteoffloadbin), videoconvert3);
  gst_bin_add(GST_BIN(pipeline), fpssink);
  gst_bin_add(GST_BIN(pipeline), remoteoffloadbin);


  gst_element_link (filesrc, decodebin);
  g_signal_connect (decodebin, "pad-added", G_CALLBACK (on_pad_added), capsfilter0);
  if( !gst_element_link_many(capsfilter0,
                        videoconvert0,
                        queue0,
                        gvadetect,
                        queue1,
                        capsfilter1,
                        videoconvert2,
                        gvaclassify0,
                        queue2,
                        gvaclassify1,
                        queue3,
                        gvawatermark,
                        videoroicrop,
                        videoconvert3,
                        fpssink,
                        NULL) )
  {
     g_print("Error in gst_element_link_many\n");
     return -1;
  }

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (GST_OBJECT (pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);

  g_free(videofilename);
  g_free(detectionmodelfilename);
  g_free(classifymodel0filename);
  g_free(classifymodel0procfilename);
  g_free(classifymodel1filename);
  g_free(classifymodel1procfilename);
  return 0;

}


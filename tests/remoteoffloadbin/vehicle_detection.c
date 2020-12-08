/*
 *  vehicle_detection.c - Vehicle Detection Test Application
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

  gchar *videofilename =  g_strdup_printf( "%s/Pexels_Videos_4786_960x540.mp4", videxamplesenv);
  gchar *modelfilename =  g_strdup_printf( "%s/intel/vehicle-license-plate-detection-barrier-0106/FP32/vehicle-license-plate-detection-barrier-0106.xml", modelspathenv);

  fprintf(stderr, "Using video file = %s\n", videofilename);
  fprintf(stderr, "Using model file = %s\n", modelfilename);

  GstElement *filesrc = gst_element_factory_make("filesrc", "myfilesrc");
  g_object_set (G_OBJECT (filesrc), "location", videofilename, NULL);
  GstElement *decodebin = gst_element_factory_make("decodebin", "mydecodebin");
  GstElement *capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
  gchar *capsstr;
  capsstr = g_strdup_printf ("video/x-raw");
  GstCaps *caps = gst_caps_from_string (capsstr);
  g_object_set (capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);

  GstElement *videoconvert = gst_element_factory_make("videoconvert", "myvideoconvert");

  GstElement *gvadetect = gst_element_factory_make("gvadetect", "mygvadetect");
  g_object_set (G_OBJECT (gvadetect), "inference-id", "inf0", NULL);
  g_object_set (G_OBJECT (gvadetect), "model", modelfilename, NULL);
  g_object_set (G_OBJECT (gvadetect), "device", "CPU", NULL);
  g_object_set (G_OBJECT (gvadetect), "every-nth-frame", (guint)1, NULL);
  g_object_set (G_OBJECT (gvadetect), "batch-size", (guint)1, NULL);

  GstElement *queue = gst_element_factory_make("queue", "myqueue");
  GstElement *gvawatermark = gst_element_factory_make("gvawatermark", "mygvawatermark");
  GstElement *fpssink = gst_element_factory_make("fpsdisplaysink", "myfpsdisplaysink");
  g_object_set (G_OBJECT (fpssink), "sync", (gboolean)FALSE, NULL);
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
  gst_bin_add(GST_BIN(pipeline), capsfilter);
  gst_bin_add(GST_BIN(remoteoffloadbin), videoconvert);
  gst_bin_add(GST_BIN(remoteoffloadbin), gvadetect);
  gst_bin_add(GST_BIN(remoteoffloadbin), queue);
  gst_bin_add(GST_BIN(pipeline), gvawatermark);
  gst_bin_add(GST_BIN(pipeline), fpssink);
  gst_bin_add(GST_BIN(pipeline), remoteoffloadbin);

  gst_element_link (filesrc, decodebin);
  g_signal_connect (decodebin, "pad-added", G_CALLBACK (on_pad_added), capsfilter);

  gst_element_link(capsfilter, videoconvert);
  gst_element_link(videoconvert, gvadetect);
  gst_element_link(gvadetect, queue);
  gst_element_link(queue, gvawatermark);
  gst_element_link(gvawatermark, fpssink);

  if( gst_element_set_state (pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE )
  {
    fprintf(stderr, "Error setting state of pipeline to PLAYING\n");
    return -1;
  }

  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (GST_OBJECT (pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);

  g_free(videofilename);
  g_free(modelfilename);

  return 0;

}


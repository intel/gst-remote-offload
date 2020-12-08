/*
 *  test3.c - Test3 Application
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

  char *videxamplesenv = getenv("VIDEO_EXAMPLES_DIR");
  if( !videxamplesenv )
  {
     fprintf(stderr, "Error! VIDEO_EXAMPLES_DIR env not set.\n");
     fprintf(stderr, " Set to /path/to/video-examples\n");
     return -1;
  }
  gchar *videofilename =  g_strdup_printf( "%s/Pexels_Videos_4786_960x540.mp4", videxamplesenv);

  GstBus *bus;
  guint bus_watch_id;
  GMainLoop *loop;

  loop = g_main_loop_new (NULL, FALSE);

  GstElement *filesrc = gst_element_factory_make("filesrc", "myfilesrc");
  g_object_set (G_OBJECT (filesrc), "location", videofilename, NULL);
  GstElement *qtdemux = gst_element_factory_make("qtdemux", "myqtdemux");
  GstElement *decodebin = gst_element_factory_make("decodebin", "mydecodebin");
  GstElement *tee = gst_element_factory_make("tee", "mytee");

  GstElement *queue0 = gst_element_factory_make("queue", "myqueue0");
  GstElement *queue1 = gst_element_factory_make("queue", "myqueue1");
  GstElement *queue2 = gst_element_factory_make("queue", "myqueue2");
  GstElement *queue3 = gst_element_factory_make("queue", "myqueue3");
  GstElement *videoconvert0 = gst_element_factory_make("videoconvert", "myvideoconvert0");
  GstElement *videoconvert1 = gst_element_factory_make("videoconvert", "myvideoconvert1");
  GstElement *ximagesink0 = gst_element_factory_make("ximagesink", "myximagesink0");
  GstElement *ximagesink1 = gst_element_factory_make("ximagesink", "myximagesink1");
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
  gst_bin_add(GST_BIN(pipeline), qtdemux);
  gst_bin_add(GST_BIN(pipeline), decodebin);
  gst_bin_add(GST_BIN(pipeline), tee);
  gst_bin_add(GST_BIN(pipeline), queue0);
  gst_bin_add(GST_BIN(pipeline), queue1);
  gst_bin_add(GST_BIN(remoteoffloadbin), queue2);
  gst_bin_add(GST_BIN(remoteoffloadbin), queue3);
  gst_bin_add(GST_BIN(pipeline), videoconvert0);
  gst_bin_add(GST_BIN(pipeline), videoconvert1);
  gst_bin_add(GST_BIN(pipeline), ximagesink0);
  gst_bin_add(GST_BIN(pipeline), ximagesink1);
  gst_bin_add(GST_BIN(pipeline), remoteoffloadbin);

  gst_element_link (filesrc, qtdemux);
  g_signal_connect (qtdemux, "pad-added", G_CALLBACK (on_pad_added), decodebin);
  g_signal_connect (decodebin, "pad-added", G_CALLBACK (on_pad_added), tee);

  gst_element_link_pads (tee, "src_0", queue0, "sink");
  gst_element_link_pads (tee, "src_1", queue1, "sink");

  gst_element_link(queue0, queue2);
  gst_element_link(queue1, queue3);

  gst_element_link (queue2, videoconvert0);
  gst_element_link (videoconvert0, ximagesink0);

  gst_element_link (queue3, videoconvert1);
  gst_element_link (videoconvert1, ximagesink1);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (GST_OBJECT (pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);

  return 0;

}


/*
 *  test2.c - Test2 Application
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

  GstElement *videotestsrc = gst_element_factory_make("videotestsrc", "myvideotestsrc");
  g_object_set (G_OBJECT (videotestsrc), "num-buffers", 300, NULL);
  GstElement *queue = gst_element_factory_make("queue", "myqueue");
  GstElement *videoconvert0 = gst_element_factory_make("videoconvert", "myvideoconvert0");
  GstElement *ximagesink = gst_element_factory_make("ximagesink", "myximagesink");

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

  GstElement *pipeline = gst_pipeline_new("my_pipeline");
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);

  gst_bin_add(GST_BIN(pipeline), videotestsrc);
  gst_bin_add(GST_BIN(remoteoffloadbin), queue);
  gst_bin_add(GST_BIN(pipeline), videoconvert0);
  gst_bin_add(GST_BIN(pipeline), ximagesink);
  gst_bin_add(GST_BIN(pipeline), remoteoffloadbin);

  gst_element_link (videotestsrc, queue);
  gst_element_link (queue, videoconvert0);
  gst_element_link (videoconvert0, ximagesink);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (GST_OBJECT (pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);

  return 0;

}


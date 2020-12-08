/*
 *  test9.c - Test9 Application
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

#define REMOTE 1

#if !REMOTE
 GstQuery *last_query = 0;
//sink pad probe
GstPadProbeReturn SinkPadProbeQuery(GstPad *pad,
                                  GstPadProbeInfo *info,
                                  gpointer user_data)
{

   GstQuery *query = GST_PAD_PROBE_INFO_QUERY(info);

   if( !query || (query == last_query))
      return GST_PAD_PROBE_PASS;

   fprintf(stderr, "SinkPadProbeQuery(%s)\n", GST_QUERY_TYPE_NAME(query));

   last_query = query;
   gst_pad_query(pad, query);

   GstStructure *structure_response = gst_query_writable_structure (query);
   fprintf(stderr, "structure_response = %s\n", gst_structure_to_string(structure_response));



   return GST_PAD_PROBE_HANDLED;

}

//sink pad probe
GstPadProbeReturn SinkPadProbeEvent(GstPad *pad,
                                  GstPadProbeInfo *info,
                                  gpointer user_data)
{
   GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);

   if( !event )
      return GST_PAD_PROBE_PASS;

   fprintf(stderr, "SinkPadEventProbe(%s)\n", GST_EVENT_TYPE_NAME(event));


   return GST_PAD_PROBE_PASS;
}
#endif

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
  GstElement *videoconvert0 = gst_element_factory_make("videoconvert", "myvideoconvert0");
  GstElement *ximagesink = gst_element_factory_make("ximagesink", "myximagesink");

#if REMOTE
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

  gst_bin_add(GST_BIN(remoteoffloadbin), videotestsrc);
  gst_bin_add(GST_BIN(pipeline), videoconvert0);
  gst_bin_add(GST_BIN(pipeline), ximagesink);
  gst_bin_add(GST_BIN(pipeline), remoteoffloadbin);


  gst_element_link (videotestsrc, videoconvert0);
  gst_element_link (videoconvert0, ximagesink);
#if !REMOTE
  GstPad *sinkpad = gst_element_get_static_pad(ximagesink, "sink");
   if( !sinkpad )
   {
     fprintf(stderr, "Error obtaining sink pad of appsink\n");
     return -1;
   }
   gst_pad_add_probe(sinkpad, (GstPadProbeType)(GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM |
                                                GST_PAD_PROBE_TYPE_QUERY_UPSTREAM |
                                                GST_PAD_PROBE_TYPE_BLOCKING), (GstPadProbeCallback)SinkPadProbeEvent, NULL, NULL);

   gst_pad_add_probe(sinkpad, (GstPadProbeType)(GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM |
                                                GST_PAD_PROBE_TYPE_QUERY_UPSTREAM |
                                                GST_PAD_PROBE_TYPE_BLOCKING), (GstPadProbeCallback)SinkPadProbeQuery, NULL, NULL);
   gst_object_unref (sinkpad);
#endif
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (GST_OBJECT (pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);

  return 0;

}


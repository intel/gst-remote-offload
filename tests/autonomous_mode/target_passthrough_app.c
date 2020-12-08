/*
 *  target_passthrough_app.c - hddlsrc ! hddlsink
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author: Soon, Thean Siew <thean.siew.soon@intel.com>
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
#include <glib.h>
#define GETTEXT_PACKAGE "gtk20"
#include <glib/gi18n.h>
#include "gsthddlcontext.h"
#include <stdlib.h>
#include <pthread.h>

#define TCP_HOST		"0.0.0.0"
#define XLINK_DEVICE_TYPE	HOST_DEVICE

static gint port = -1;
static gint channel_num = -1;
static gchar *connection_mode = NULL;

static GOptionEntry entries[] = {
  {"connection-mode", 'c', 0, G_OPTION_ARG_STRING, &connection_mode,
      "Connect with xlink or tcp.", "mode"},
  {"port", 'p', 0, G_OPTION_ARG_INT, &port,
      "Port to listen to.", "P"},
  {"channel", 'n', 0, G_OPTION_ARG_INT, &channel_num,
      "Channels to be created", "channel"},
  {NULL}
};

/* Structure to contain all our information, so we can pass it to thread */
typedef struct _CustomData
{
  GstHddlConnectionMode mode;
  gint port;
  XLinkHandler_t *xlink_handler;
} CustomData;

void *channel_thread (void *ptr);

int
main (int argc, char *argv[])
{
  CustomData *data;
  pthread_t *thread;
  GstHddlConnectionMode mode;
  GError *error = NULL;
  GOptionContext *context;
  gchar **args;
  XLinkHandler_t *xlink_handler;

  context = g_option_context_new ("- test the server portion of HDDL");
  args = g_strdupv (argv);
  g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
  if (!g_option_context_parse (context, &argc, &args, &error)) {
    g_print ("option parsing failed: %s\n", error->message);
    exit (1);
  }

  if (connection_mode == NULL || port == -1 || channel_num == -1) {
    g_print ("%s\n", g_option_context_get_help (context, TRUE, NULL));
    return -1;
  }

  if (!g_ascii_strncasecmp (connection_mode, "tcp", 5)) {
    mode = CONNECT_TCP;
  } else if (!g_ascii_strncasecmp (connection_mode, "xlink", 5)) {
    mode = CONNECT_XLINK;
  } else {
    g_print ("Connection mode may not be set correctly\n");
    return -1;
  }

  data = (CustomData *)malloc (channel_num * sizeof (CustomData));
  thread = (pthread_t *)malloc (channel_num * sizeof (pthread_t));

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  if (mode == CONNECT_XLINK) {
    xlink_handler = g_slice_new0 (XLinkHandler_t);
    xlink_handler->dev_type = XLINK_DEVICE_TYPE;
    gst_hddl_xlink_initialize ();
    gst_hddl_xlink_connect_device (xlink_handler);
  }

  for (int i = 0; i < channel_num; i++) {
    data[i].mode = mode;
    data[i].port = port;
    port += 2;
    if (mode == CONNECT_XLINK) {
      data[i].xlink_handler = xlink_handler;
    }

    pthread_create (&thread[i], NULL, channel_thread, (void *)&data[i]);
  }

  for (int i = 0; i < channel_num; i++) {
    pthread_join (thread[i], NULL);
  }

  if (mode == CONNECT_XLINK) {
    gst_hddl_xlink_disconnect(xlink_handler);
    g_slice_free (XLinkHandler_t, xlink_handler);
  }

  free (data);
  free (thread);

  return 0;
}

void *
channel_thread (void *ptr)
{
  CustomData *data = (CustomData *)ptr;
  GstElement *pipeline, *source, *sink;
  GstBus *bus;
  GstMessage *msg;
  GstStateChangeReturn ret;
  gboolean terminate = FALSE;
  GstHddlContext *hddl_context_src, *hddl_context_sink;

  /* Create the empty pipeline */
  pipeline = gst_pipeline_new ("test-pipeline");
  if (!pipeline) {
    g_printerr ("Pipeline could not be created.\n");
    return NULL;
  }

  /* Create the elements */
  source = NULL;
  sink = NULL;
  source = gst_element_factory_make ("hddlsrc", NULL);
  sink = gst_element_factory_make ("hddlsink", NULL);

  if (!source || !sink) {
    g_printerr ("Not all elements could be created.\n");
    return NULL;
  }

  /* Build the pipeline */
  gst_bin_add_many (GST_BIN (pipeline), source, sink, NULL);
  if (gst_element_link (source, sink) != TRUE) {
    g_printerr ("Elements could not be linked.\n");
    gst_object_unref (pipeline);
    return NULL;
  }

  hddl_context_src = gst_hddl_context_new (data->mode);
  hddl_context_sink = gst_hddl_context_new (data->mode);

  if (data->mode == CONNECT_TCP) {

    hddl_context_src->hddl_tcp->host = TCP_HOST;
    hddl_context_src->hddl_tcp->port = data->port;

    hddl_context_sink->hddl_tcp->host = TCP_HOST;
    hddl_context_sink->hddl_tcp->port = data->port + 1;

    g_print ("Connection mode is %s. hddlsrc port: %d, hddlsink port: %d\n",
        connection_mode, hddl_context_src->hddl_tcp->port,
        hddl_context_sink->hddl_tcp->port);

  } else if (data->mode == CONNECT_XLINK) {
    hddl_context_src->hddl_xlink->xlink_handler =
	                               data->xlink_handler;
    hddl_context_src->hddl_xlink->channelId = data->port;

    hddl_context_sink->hddl_xlink->xlink_handler =
	                               data->xlink_handler;
    hddl_context_sink->hddl_xlink->channelId = data->port + 1;
    g_print
        ("Connection mode is %s. hddlsrc channel: %d, hddlsink channel: %d\n",
        connection_mode, hddl_context_src->hddl_xlink->channelId,
        hddl_context_sink->hddl_xlink->channelId);
  }

  /* Set element's property */
  g_object_set (G_OBJECT (source), "connection-mode", data->mode, NULL);
  g_object_set (G_OBJECT (source), "selected-target-context",
      hddl_context_src, NULL);
  g_object_set (G_OBJECT (sink), "connection-mode", data->mode, NULL);
  g_object_set (G_OBJECT (sink), "selected-target-context",
      hddl_context_sink, NULL);

  gst_hddl_context_free (hddl_context_src);
  gst_hddl_context_free (hddl_context_sink);

  /* Start playing */
  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (pipeline);
    return NULL;
  }

  /* Wait until error or EOS */
  bus = gst_element_get_bus (pipeline);
  do {
    msg =
        gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
        GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    /* Parse message */
    if (msg != NULL) {
      GError *err;
      gchar *debug_info;

      switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_ERROR:
          gst_message_parse_error (msg, &err, &debug_info);
          g_printerr ("Error received from element %s: %s\n",
              GST_OBJECT_NAME (msg->src), err->message);
          g_printerr ("Debugging information: %s\n",
              debug_info ? debug_info : "none");
          g_clear_error (&err);
          g_free (debug_info);
          terminate = TRUE;
          break;
        case GST_MESSAGE_EOS:
          g_print ("End-Of-Stream reached.\n");
          terminate = TRUE;
          break;
        case GST_MESSAGE_STATE_CHANGED:
          /* We are only interested in state-changed messages from the pipeline */
          if (GST_MESSAGE_SRC (msg) == GST_OBJECT (pipeline)) {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed (msg, &old_state, &new_state,
                &pending_state);
            g_print ("Pipeline state changed from %s to %s:\n",
                gst_element_state_get_name (old_state),
                gst_element_state_get_name (new_state));

            /* Generate pipeline graph when PAUSED->PLAYING */
            /* Run the app like this */
            /* GST_DEBUG_DUMP_DOT_DIR=/<path> ./a.out */
            if ((old_state == GST_STATE_PAUSED)
                && (new_state == GST_STATE_PLAYING)) {
              GST_DEBUG_BIN_TO_DOT_FILE (GST_BIN (pipeline),
                  GST_DEBUG_GRAPH_SHOW_ALL, "target_pipeline");
            }
          }
          break;
        default:
          /* We should not reach here because we only asked for ERRORs and EOS */
          g_printerr ("Unexpected message received.\n");
          break;
      }
      gst_message_unref (msg);
    }
  } while (!terminate);

  /* Free resources */
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  return NULL;
}

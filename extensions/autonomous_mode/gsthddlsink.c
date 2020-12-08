/*
 *  gsthddlsink.c - HDDL Sink element
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author: Hoe, Sheng Yang <sheng.yang.hoe@intel.com>
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

#include "gsthddlsink.h"
#include "gsthddlutils.h"

#define GST_PLUGIN_NAME	"hddlsink"
#define GST_PLUGIN_DESC "HDDL Sink for HDDL Streamer"

GST_DEBUG_CATEGORY_STATIC (gst_hddl_sink_debug);
#define GST_CAT_DEFAULT gst_hddl_sink_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  PROP_0,
  PROP_SELECTED_TARGET_CONTEXT,
  PROP_CONNECTION_MODE,
  PROP_LAST
};

static GParamSpec *g_properties[PROP_LAST] = { NULL, };

#define gst_hddl_sink_parent_class parent_class
G_DEFINE_TYPE (GstHddlSink, gst_hddl_sink, GST_TYPE_BASE_SINK);

static void gst_hddl_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_hddl_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_hddl_sink_dispose (GObject * object);
static void gst_hddl_sink_finalize (GObject * object);
static gboolean gst_hddl_sink_start (GstBaseSink * sink);
static gboolean gst_hddl_sink_stop (GstBaseSink * sink);
GstPadProbeReturn gst_hddl_sink_event_probe (GstPad * pad,
    GstPadProbeInfo * info, gpointer user_data);
GstPadProbeReturn gst_hddl_sink_buffer_probe (GstPad * pad,
    GstPadProbeInfo * info, gpointer user_data);
GstPadProbeReturn gst_hddl_sink_query_probe (GstPad * pad,
    GstPadProbeInfo * info, gpointer user_data);
gboolean gst_hddl_sink_xlink_send_event (GstHddlContext * hddl_context,
    GstEvent * event);
gboolean gst_hddl_sink_xlink_send_buffer (GstHddlContext * hddl_context,
    GstBuffer * buffer);
gboolean gst_hddl_sink_xlink_send_query (GstHddlSink * hddl_sink,
    GstQuery * query);
void gst_hddl_sink_send_thread (GstHddlSink * hddl_sink);

static void
gst_hddl_sink_class_init (GstHddlSinkClass * klass)
{
  GObjectClass *const gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *const element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *const sink_class = GST_BASE_SINK_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_hddl_sink_debug, GST_PLUGIN_NAME, 0,
      GST_PLUGIN_DESC);

  gobject_class->set_property = gst_hddl_sink_set_property;
  gobject_class->get_property = gst_hddl_sink_get_property;
  gobject_class->dispose = gst_hddl_sink_dispose;
  gobject_class->finalize = gst_hddl_sink_finalize;
  /* TODO: Map any other functions needed for gobject_class */

  /* TODO: Map any other functions needed for element_class */

  sink_class->start = gst_hddl_sink_start;
  sink_class->stop = gst_hddl_sink_stop;
  /* TODO: Map any other functions needed by sink_class */

  gst_element_class_set_static_metadata (element_class, "HDDL Sink",
      "Sink/Video", GST_PLUGIN_DESC,
      "Hoe, Sheng Yang <sheng.yang.hoe@intel.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  /* Install default HDDL Sink properties */
  g_properties[PROP_SELECTED_TARGET_CONTEXT] =
      g_param_spec_boxed ("selected-target-context", "Selected Target Context",
      "The context for selected target", GST_TYPE_HDDL_CONTEXT,
      G_PARAM_READWRITE);
  g_properties[PROP_CONNECTION_MODE] =
      g_param_spec_enum ("connection-mode", "Connection mode",
      "The connection mode of plugin",
      GST_TYPE_CONNECTION_MODE, DEFAULT_CONNECTION_MODE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (gobject_class, PROP_LAST, g_properties);

}

static void
gst_hddl_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstHddlSink *hddl_sink = GST_HDDL_SINK (object);

  switch (prop_id) {
    case PROP_SELECTED_TARGET_CONTEXT:
      hddl_sink->selected_target_context = g_value_dup_boxed (value);
      break;
    case PROP_CONNECTION_MODE:
      hddl_sink->connection_mode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hddl_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstHddlSink *hddl_sink = GST_HDDL_SINK (object);

  switch (prop_id) {
    case PROP_SELECTED_TARGET_CONTEXT:
      g_value_set_boxed (value, &hddl_sink->selected_target_context);
      break;
    case PROP_CONNECTION_MODE:
      g_value_set_enum (value, hddl_sink->connection_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hddl_sink_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_hddl_sink_finalize (GObject * object)
{
  GstHddlSink *hddl_sink = GST_HDDL_SINK (object);

  /* Object Cleanup
   * Only will be called once
   * free @ here
   */


  if (hddl_sink->selected_target_context) {
    gst_hddl_context_free (hddl_sink->selected_target_context);
    hddl_sink->selected_target_context = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_hddl_sink_init (GstHddlSink * hddl_sink)
{
  /* TODO: Add GST_DEBUG traces for function enter */
  hddl_sink->selected_target_context = NULL;

  gst_pad_add_probe (GST_BASE_SINK_PAD (hddl_sink),
      GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      (GstPadProbeCallback) gst_hddl_sink_event_probe, hddl_sink, NULL);
  gst_pad_add_probe (GST_BASE_SINK_PAD (hddl_sink),
      GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) gst_hddl_sink_buffer_probe, hddl_sink, NULL);
  gst_pad_add_probe (GST_BASE_SINK_PAD (hddl_sink),
      GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM,
      (GstPadProbeCallback) gst_hddl_sink_query_probe, hddl_sink, NULL);

  hddl_sink->connection_mode = DEFAULT_CONNECTION_MODE;
  hddl_sink->selected_target_context =
      gst_hddl_context_new (hddl_sink->connection_mode);

  if (!hddl_sink->selected_target_context) {
    GST_ERROR ("Failed to create HDDL context.");
    return;
  }
  // FIXME: Remove hardcode
  if (hddl_sink->connection_mode == CONNECT_TCP) {
    hddl_sink->selected_target_context->hddl_tcp->host = DEFAULT_TCP_HOST;
    hddl_sink->selected_target_context->hddl_tcp->port = DEFAULT_TCP_PORT_2;
  } else if (hddl_sink->connection_mode == CONNECT_XLINK) {
    hddl_sink->xlink_connected = FALSE;
    hddl_sink->selected_target_context->hddl_xlink->xlink_handler->dev_type =
        HOST_DEVICE;
    hddl_sink->selected_target_context->hddl_xlink->channelId =
        DEFAULT_XLINK_CHANNEL_2;
  }
}

GstPadProbeReturn
gst_hddl_sink_event_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstHddlSink *hddl_sink = GST_HDDL_SINK (user_data);
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
  gst_event_ref (event);
  g_async_queue_push (hddl_sink->queue, event);

  return GST_PAD_PROBE_OK;
}

GstPadProbeReturn
gst_hddl_sink_buffer_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  /* TODO: Add GST_DEBUG traces for function enter */
  GstHddlSink *hddl_sink = GST_HDDL_SINK (user_data);
  GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);

  gst_buffer_ref (buffer);
  g_async_queue_push (hddl_sink->queue, buffer);

  return GST_PAD_PROBE_OK;
}

GstPadProbeReturn
gst_hddl_sink_query_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstHddlSink *hddl_sink = GST_HDDL_SINK (user_data);
  GstQuery *query = GST_PAD_PROBE_INFO_QUERY (info);
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
      if (hddl_sink->connection_mode == CONNECT_TCP) {
        if (hddl_sink->selected_target_context->hddl_tcp->client_socket) {
          gst_hddl_sink_xlink_send_query (hddl_sink, query);
        }
      } else if (hddl_sink->connection_mode == CONNECT_XLINK) {
        //FIXME: Use same variable to check for connection status of TCP/XLINK
        if (hddl_sink->xlink_connected)
          gst_hddl_sink_xlink_send_query (hddl_sink, query);
      }
      break;
    default:
      break;
  }
  return GST_PAD_PROBE_OK;
}

static gboolean
gst_hddl_sink_start (GstBaseSink * sink)
{
  /* TODO: Add GST_DEBUG traces for function enter */

  GstHddlSink *hddl_sink = GST_HDDL_SINK (sink);
  gboolean status = TRUE;


  /* transfer queue initialization */
  hddl_sink->queue = g_async_queue_new ();
  hddl_sink->send_thread =
      g_thread_new ("send_thread", (GThreadFunc) gst_hddl_sink_send_thread,
      hddl_sink);

  if (hddl_sink->connection_mode == CONNECT_TCP) {
    // Establish tcp connection
    // TODO: Generic-lize this.
    g_thread_new ("listen_thread",
        (GThreadFunc) gst_hddl_tcp_listen_client,
        hddl_sink->selected_target_context->hddl_tcp);
  } else if (hddl_sink->connection_mode == CONNECT_XLINK) {
    gst_hddl_xlink_connect (hddl_sink->selected_target_context->hddl_xlink->
        xlink_handler,
        hddl_sink->selected_target_context->hddl_xlink->channelId);
    hddl_sink->xlink_connected = TRUE;
  }

  return status;
}

static gboolean
gst_hddl_sink_stop (GstBaseSink * sink)
{
  GstHddlSink *hddl_sink = GST_HDDL_SINK (sink);

  /* Cleanup transfer queue */
  if (hddl_sink->send_thread) {
    g_thread_join (hddl_sink->send_thread);
    hddl_sink->send_thread = NULL;
  }

  if (hddl_sink->queue) {
    g_async_queue_unref (hddl_sink->queue);
    hddl_sink->queue = NULL;
  }

  gst_hddl_shutdown (hddl_sink->selected_target_context->hddl_xlink,
      hddl_sink->selected_target_context->conn_mode);

  g_free (hddl_sink->previous_caps);

  return TRUE;
}

gboolean
gst_hddl_sink_xlink_send_event (GstHddlContext * hddl_context, GstEvent * event)
{
  GstHddlXLink *hddl_xlink = hddl_context->hddl_xlink;
  GstHddlConnectionMode mode = hddl_context->conn_mode;
  transfer_header header;
  event_header event_info;
  gchar *structure_string = NULL;
  gboolean status;
  GstBuffer *send_buffer;
  GstMapInfo map;

  header.type = EVENT_TYPE;

  event_info.type = GST_EVENT_TYPE (event);
  event_info.timestamp = GST_EVENT_TIMESTAMP (event);
  event_info.seqnum = GST_EVENT_SEQNUM (event);
  const GstStructure *structure = gst_event_get_structure (event);
  event_info.string_len = 0;

  if (structure) {
    structure_string = gst_structure_to_string (structure);
    event_info.string_len = strlen (structure_string) + 1;
  }
  // FIXME: Remove after testing
  /*g_print ("Event type: %s\n", gst_event_type_get_name (event_info.type));
     g_print ("Event timestamp: %lu\n", event_info.timestamp);
     g_print ("Event seqnum: %u\n", event_info.seqnum);
     g_print ("Event structure string: %s\n",
     gst_structure_to_string (gst_event_get_structure (event))); */
  header.size = sizeof (event_header) + event_info.string_len;

  send_buffer = gst_buffer_new ();

  gst_hddl_utils_wrap_data_in_buffer (&header, sizeof (transfer_header),
      send_buffer);
  gst_hddl_utils_wrap_data_in_buffer (&event_info, sizeof (event_header),
      send_buffer);

  if (event_info.string_len)
    gst_hddl_utils_wrap_data_in_buffer (structure_string, event_info.string_len,
        send_buffer);

  gst_buffer_map (send_buffer, &map, GST_MAP_READ);
  status = gst_hddl_transfer_data (hddl_xlink, mode, map.data, map.size);
  gst_buffer_unmap (send_buffer, &map);

  gst_buffer_unref (send_buffer);
  g_free (structure_string);

  if (!status)
    g_print ("Hddl sink send event has some failed transfer\n");

  return TRUE;
}

gboolean
gst_hddl_sink_xlink_send_buffer (GstHddlContext * hddl_context,
    GstBuffer * buffer)
{
  GstHddlXLink *hddl_xlink = hddl_context->hddl_xlink;
  GstHddlConnectionMode mode = hddl_context->conn_mode;
  transfer_header header;
  buffer_header buffer_info;
  GstMapInfo map;
  GstBuffer *buffer2, *send_buffer;
  gboolean status;

#ifdef GVA_PLUGIN
  transfer_header roi_transfer_header;
  video_roi_meta_header *roi_meta_header;
  GList *roi_meta_header_list = NULL;
  GList *roi_meta_param_header_list = NULL;
  transfer_header tensor_transfer_header;
  gva_tensor_meta_header *tensor_meta_header;
  GList *tensor_meta_header_list = NULL;
  transfer_header json_transfer_header;
  gva_json_meta_header *json_meta_header;
  GList *json_meta_header_list = NULL;
  GList *list = NULL;
#endif

  header.type = BUFFER_TYPE;
  header.size = sizeof (buffer_header) + gst_buffer_get_size (buffer);


  buffer_info.pts = GST_BUFFER_PTS (buffer);
  buffer_info.dts = GST_BUFFER_DTS (buffer);
  buffer_info.duration = GST_BUFFER_DURATION (buffer);
  buffer_info.offset = GST_BUFFER_OFFSET (buffer);
  buffer_info.offset_end = GST_BUFFER_OFFSET_END (buffer);
  buffer_info.flags = GST_BUFFER_FLAGS (buffer);
  buffer_info.num_video_roi_meta = 0;
  buffer_info.num_gva_tensor_meta = 0;
  buffer_info.num_gva_json_meta = 0;

#ifdef GVA_PLUGIN
  // FIXME: Remove after testing
/*  GstGVATensorMeta *a = GST_GVA_TENSOR_META_ADD (buffer);
  a->precision = UNSPECIFIED;
  a->rank = 2;
  for (guint x = 0; x < GVA_TENSOR_MAX_RANK; x++) {
    a->dims[x] = x;
  }
  a->layout = NCHW;
  a->layer_name = g_strdup ("hello");
  a->model_name = g_strdup ("bye");
  a->total_bytes = 8;
  a->data = g_slice_alloc0 (a->total_bytes);
  a->element_id = g_strdup ("sayonara");
  GstGVATensorMeta *b = GST_GVA_TENSOR_META_ADD (buffer);
     b->precision = FP32;
     b->rank = 10;
     for (guint x=0; x< GVA_TENSOR_MAX_RANK; x++) {
     b->dims[x] = x;
     }
     b->layout = NHWC;
     b->layer_name = g_strdup ("nihao");
     b->model_name = g_strdup ("zaijian");
     b->total_bytes = 10;
     b->data = g_slice_alloc0 (b->total_bytes);
     b->element_id = g_strdup ("tata");
  GstGVAJSONMeta *c = GST_GVA_JSON_META_ADD (buffer);
  c->message = g_strdup ("fake msg");
*/

  buffer_info.num_video_roi_meta =
      gst_buffer_get_n_meta (buffer,
      GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE);
  buffer_info.num_gva_tensor_meta =
      gst_buffer_get_n_meta (buffer, gst_gva_tensor_meta_api_get_type ());
  buffer_info.num_gva_json_meta =
      gst_buffer_get_n_meta (buffer, gst_gva_json_meta_api_get_type ());
#endif

  // FIXME: Remove after testing
  /*g_print ("Buffer pts = %lu\n", buffer_info.pts);
     g_print ("Buffer dts = %lu\n", buffer_info.dts);
     g_print ("Buffer duration = %lu\n", buffer_info.duration);
     g_print ("Buffer offset = %lu\n", buffer_info.offset);
     g_print ("Buffer offset_end = %lu\n", buffer_info.offset_end);
     g_print ("Buffer flags = %u\n", buffer_info.flags); */

  buffer2 = gst_buffer_new ();
  gst_hddl_utils_wrap_data_in_buffer (&buffer_info, sizeof (buffer_header),
      buffer2);

  // FIXME: Remove after testing
  /*g_print ("buffer[0] = %d\n", map.data[0]);
     g_print ("buffer[10] = %d\n", map.data[10]);
     g_print ("buffer[100] = %d\n", map.data[100]);
     g_print ("buffer[1000] = %d\n", map.data[1000]);
     g_print ("buffer[500] = %d\n", map.data[500]); */

#ifdef GVA_PLUGIN
  if (buffer_info.num_video_roi_meta > 0) {
    roi_transfer_header.type = BUFFER_VIDEO_ROI_META_TYPE;
    roi_transfer_header.size = 0;
    gpointer pointer = NULL;
    for (int i = 0; i < buffer_info.num_video_roi_meta; i++) {
      gst_hddl_calculate_meta_size (buffer, &pointer, &roi_transfer_header);
    }

    header.size =
        header.size + sizeof (transfer_header) + roi_transfer_header.size;
    gst_hddl_utils_wrap_data_in_buffer (&roi_transfer_header,
        sizeof (transfer_header), buffer2);

    gpointer state = NULL;
    for (int j = 0; j < buffer_info.num_video_roi_meta; j++) {
      roi_meta_header = g_slice_new (video_roi_meta_header);
      gst_hddl_send_video_roi_meta (hddl_context, buffer, &state, buffer2,
          roi_meta_header, roi_meta_param_header_list);
      roi_meta_header_list =
          g_list_append (roi_meta_header_list, roi_meta_header);
    }
  }

  if (buffer_info.num_gva_tensor_meta > 0) {
    tensor_transfer_header.type = BUFFER_GVA_TENSOR_META_TYPE;
    tensor_transfer_header.size = 0;
    gpointer pointer = NULL;
    for (int i = 0; i < buffer_info.num_gva_tensor_meta; i++) {
      gst_hddl_calculate_meta_size (buffer, &pointer, &tensor_transfer_header);
    }

    header.size =
        header.size + sizeof (transfer_header) + tensor_transfer_header.size;
    gst_hddl_utils_wrap_data_in_buffer (&tensor_transfer_header,
        sizeof (transfer_header), buffer2);

    gpointer state = NULL;
    for (int j = 0; j < buffer_info.num_gva_tensor_meta; j++) {
      tensor_meta_header = g_slice_new (gva_tensor_meta_header);
      gst_hddl_send_gva_tensor_meta (hddl_context, buffer, &state, buffer2,
          tensor_meta_header);
      tensor_meta_header_list =
          g_list_append (tensor_meta_header_list, tensor_meta_header);
    }
  }

  if (buffer_info.num_gva_json_meta > 0) {
    json_transfer_header.type = BUFFER_GVA_JSON_META_TYPE;
    json_transfer_header.size = 0;
    gpointer pointer = NULL;
    for (int i = 0; i < buffer_info.num_gva_json_meta; i++) {
      gst_hddl_calculate_meta_size (buffer, &pointer, &json_transfer_header);
    }

    header.size =
        header.size + sizeof (transfer_header) + json_transfer_header.size;
    gst_hddl_utils_wrap_data_in_buffer (&json_transfer_header,
        sizeof (transfer_header), buffer2);

    gpointer state = NULL;
    for (int j = 0; j < buffer_info.num_gva_json_meta; j++) {
      json_meta_header = g_slice_new (gva_json_meta_header);
      gst_hddl_send_gva_json_meta (hddl_context, buffer, &state, buffer2,
          json_meta_header);
      json_meta_header_list =
          g_list_append (json_meta_header_list, json_meta_header);
    }
  }
#endif

  gst_buffer_ref (buffer);
  send_buffer = gst_buffer_append (buffer2, buffer);
  /* Append send buffer to transfer header buffer */
  GstBuffer *transfer_buffer = gst_buffer_new();
  gst_hddl_utils_wrap_data_in_buffer (&header,
                          sizeof (transfer_header), transfer_buffer);
  gst_buffer_ref (transfer_buffer);
  send_buffer = gst_buffer_append (transfer_buffer, send_buffer);

  gst_buffer_map (send_buffer, &map, GST_MAP_READ);
  status = gst_hddl_transfer_data (hddl_xlink, mode, map.data, map.size);
  gst_buffer_unmap (send_buffer, &map);

  /* Clean up */
  gst_buffer_unref (send_buffer);
  gst_buffer_unref (transfer_buffer);

  if (!status)
    g_print ("Hddl sink send buffer has some failed transfer\n");

#ifdef GVA_PLUGIN
  list = roi_meta_header_list;
  while (list != NULL) {
    g_slice_free (video_roi_meta_header, list->data);
    list = list->next;
  }

  list = roi_meta_param_header_list;
  while (list != NULL) {
    g_slice_free (video_roi_meta_param_header, list->data);
    list = list->next;
  }

  list = tensor_meta_header_list;
  while (list != NULL) {
    g_slice_free (gva_tensor_meta_header, list->data);
    list = list->next;
  }

  list = json_meta_header_list;
  while (list != NULL) {
    g_slice_free (gva_json_meta_header, list->data);
    list = list->next;
  }
#endif

  return TRUE;
}

gboolean
gst_hddl_sink_xlink_send_query (GstHddlSink * hddl_sink, GstQuery * query)
{
  transfer_header header;
  query_header query_info;
  GstHddlXLink *hddl_xlink = hddl_sink->selected_target_context->hddl_xlink;
  GstHddlConnectionMode mode = hddl_sink->selected_target_context->conn_mode;
  transfer_header *response_header = g_slice_new (transfer_header);
  gchar *structure_string = NULL;
  GstBuffer *send_buffer, *receive_buffer;
  GstMapInfo map;
  GstMemory *mem = NULL;
  gboolean status;

  header.type = QUERY_TYPE;

  query_info.type = GST_QUERY_TYPE (query);
  const GstStructure *structure = gst_query_get_structure (query);

  query_info.string_len = 0;
  if (structure) {
    structure_string = gst_structure_to_string (structure);
    query_info.string_len = strlen (structure_string) + 1;
  }
  header.size = sizeof (query_header) + query_info.string_len;

  send_buffer = gst_buffer_new ();

  gst_hddl_utils_wrap_data_in_buffer (&header, sizeof (transfer_header),
      send_buffer);
  gst_hddl_utils_wrap_data_in_buffer (&query_info, sizeof (query_header),
      send_buffer);

  if (query_info.string_len) {
    gst_hddl_utils_wrap_data_in_buffer (structure_string, query_info.string_len,
        send_buffer);
  }

  gst_buffer_map (send_buffer, &map, GST_MAP_READ);
  status = gst_hddl_transfer_data (hddl_xlink, mode, map.data, map.size);
  gst_buffer_unmap (send_buffer, &map);

  gst_buffer_unref (send_buffer);

  if (!status)
    g_print ("Hddl sink sent query has some failed transfer\n");

  /* Receive Query Response */
  mem = gst_allocator_alloc (NULL, DATA_FRAGMENT_SIZE, NULL);
  gst_memory_map (mem, &map, GST_MAP_WRITE);
  gst_hddl_receive_data (hddl_xlink, mode, (void **) &(map.data),
      sizeof (transfer_header));
  gst_memory_unmap (mem, &map);
  receive_buffer = gst_buffer_new ();
  gst_buffer_append_memory (receive_buffer, mem);
  gst_buffer_extract (receive_buffer, 0, response_header,
      sizeof (transfer_header));
  gst_buffer_resize (receive_buffer, sizeof (transfer_header),
      response_header->size);

  if (response_header->type == QUERY_RESPONSE_TYPE) {
    GString *caps_string = g_string_sized_new (response_header->size);
    GstCaps *peer_caps;

    gst_buffer_extract (receive_buffer, 0, caps_string->str,
        response_header->size);
    peer_caps = gst_caps_from_string (caps_string->str);

    /* When the returned caps is NULL/EMPTY, set the previous caps as caps query result */
    if ((g_ascii_strcasecmp (caps_string->str, "NULL") != 0)
        && (!gst_caps_is_empty (peer_caps))) {
      g_free (hddl_sink->previous_caps);
      hddl_sink->previous_caps = g_strdup (caps_string->str);
    } else {
      peer_caps = gst_caps_from_string (hddl_sink->previous_caps);
    }
    gst_query_set_caps_result (query, peer_caps);

    return TRUE;
  }
  return FALSE;
}

void
gst_hddl_sink_send_thread (GstHddlSink * hddl_sink)
{
  gboolean eos = FALSE;
  GstEvent *event;
  GstObject *object;
  GstBuffer *buffer;

  while (!eos) {
    object = g_async_queue_pop (hddl_sink->queue);

    if (GST_IS_EVENT (object)) {
      event = GST_EVENT_CAST (object);
      gst_hddl_sink_xlink_send_event (hddl_sink->selected_target_context,
          event);

      if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
        eos = TRUE;
      }

      gst_event_unref (event);
    } else if (GST_IS_BUFFER (object)) {
      buffer = GST_BUFFER_CAST (object);
      gst_hddl_sink_xlink_send_buffer (hddl_sink->selected_target_context,
          buffer);
      gst_buffer_unref (buffer);
    } else {
      g_print ("Wrong object in queue, quiting\n");
      eos = TRUE;
    }
  }

}

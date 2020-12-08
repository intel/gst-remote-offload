/*
 *  gsthddlsrc.c - HDDL Source element
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author: Yu, Chan Kit <chan.kit.yu@intel.com>
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

#include "gsthddlsrc.h"

#define GST_PLUGIN_NAME	"hddlsrc"
#define GST_PLUGIN_DESC	"HDDL Src for HDDL Streamer"

GST_DEBUG_CATEGORY_STATIC (gst_hddl_src_debug);
#define GST_CAT_DEFAULT gst_hddl_src_debug

#define gst_hddl_src_parent_class parent_class

enum
{
  PROP_0,
  PROP_SELECTED_TARGET_CONTEXT,
  PROP_CONNECTION_MODE,
  PROP_LAST
};

static GParamSpec *g_properties[PROP_LAST] = { NULL, };

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE (GstHddlSrc, gst_hddl_src, GST_TYPE_ELEMENT);

gboolean gst_hddl_src_xlink_process_event (GstHddlSrc * hddl_src,
    transfer_header * header, gboolean * eos, GstBuffer * receive_buffer);
gboolean gst_hddl_src_xlink_process_buffer (GstHddlSrc * hddl_src,
    transfer_header * header, GstBuffer * buffer);
gpointer gst_hddl_src_xlink_receive_data_thread (GstHddlSrc * hddl_src);

static void
gst_hddl_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstHddlSrc *hddl_src = GST_HDDL_SRC (object);

  switch (prop_id) {
    case PROP_SELECTED_TARGET_CONTEXT:
      hddl_src->selected_target_context = g_value_dup_boxed (value);
      break;
    case PROP_CONNECTION_MODE:
      hddl_src->connection_mode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hddl_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstHddlSrc *hddl_src = GST_HDDL_SRC (object);

  switch (prop_id) {
    case PROP_SELECTED_TARGET_CONTEXT:
      g_value_set_boxed (value, &hddl_src->selected_target_context);
      break;
    case PROP_CONNECTION_MODE:
      g_value_set_enum (value, hddl_src->connection_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hddl_src_start (GstHddlSrc * hddl_src)
{
/* TODO: Add GST_DEBUG traces for function enter */

  gboolean status;
  status =
      gst_hddl_listen (hddl_src->selected_target_context->hddl_xlink,
      hddl_src->connection_mode);

  if (status) {
    hddl_src->receive_thread =
        g_thread_new ("receivethread",
        (GThreadFunc) gst_hddl_src_xlink_receive_data_thread, hddl_src);
  }
}

static void
gst_hddl_src_dispose (GObject * object)
{
  G_OBJECT_CLASS (gst_hddl_src_parent_class)->dispose (object);
}

static void
gst_hddl_src_finalize (GObject * object)
{
  GstHddlSrc *hddl_src = GST_HDDL_SRC (object);

  if (hddl_src->selected_target_context) {
    gst_hddl_context_free (hddl_src->selected_target_context);
    hddl_src->selected_target_context = NULL;
  }

  G_OBJECT_CLASS (gst_hddl_src_parent_class)->finalize (object);
}

static void
gst_hddl_src_cleanup (GstHddlSrc * hddl_src)
{
  if (hddl_src->selected_target_context) {
    gst_hddl_shutdown (hddl_src->selected_target_context->hddl_xlink,
        hddl_src->selected_target_context->conn_mode);

    g_thread_join (hddl_src->receive_thread);

    gst_hddl_context_free (hddl_src->selected_target_context);
    hddl_src->selected_target_context = NULL;
  }
}

static GstStateChangeReturn
gst_hddl_src_change_state (GstElement * element, GstStateChange transition)
{
  /* TODO: Add GST_DEBUG traces for function enter */
  GstHddlSrc *hddl_src = GST_HDDL_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_hddl_src_start (hddl_src);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_hddl_src_cleanup (hddl_src);
      break;
    default:
      break;
  }

  return
      GST_ELEMENT_CLASS (gst_hddl_src_parent_class)->change_state (element,
      transition);
}

static void
gst_hddl_src_class_init (GstHddlSrcClass * klass)
{
  GObjectClass *const gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *const element_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_hddl_src_debug, GST_PLUGIN_NAME, 0,
      GST_PLUGIN_DESC);

  gobject_class->set_property = gst_hddl_src_set_property;
  gobject_class->get_property = gst_hddl_src_get_property;
  gobject_class->dispose = gst_hddl_src_dispose;
  gobject_class->finalize = gst_hddl_src_finalize;

  element_class->change_state = gst_hddl_src_change_state;
/* TODO: Map any other functions needed for element_class */

/* Install default HDDL Ingress properties */
  g_properties[PROP_SELECTED_TARGET_CONTEXT] =
      g_param_spec_boxed ("selected-target-context", "Selected Target Context",
      "The context for selected target", GST_TYPE_HDDL_CONTEXT,
      G_PARAM_READWRITE);
  g_properties[PROP_CONNECTION_MODE] =
      g_param_spec_enum ("connection-mode", "Connection mode",
      "The connection mode of plugin",
      GST_TYPE_CONNECTION_MODE, DEFAULT_CONNECTION_MODE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  gst_element_class_set_static_metadata (element_class, "HDDL Src", "Output",
      GST_PLUGIN_DESC, "Yu, Chan Kit <chan.kit.yu@intel.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));

  g_object_class_install_properties (gobject_class, PROP_LAST, g_properties);
}

static void
gst_hddl_src_init (GstHddlSrc * hddl_src)
{
  /* TODO: Add GST_DEBUG traces for function enter */

  hddl_src->srcpad = gst_pad_new_from_static_template (&src_factory, "src");

  gst_element_add_pad (GST_ELEMENT (hddl_src), hddl_src->srcpad);

  hddl_src->connection_mode = DEFAULT_CONNECTION_MODE;
  hddl_src->selected_target_context =
      gst_hddl_context_new (hddl_src->connection_mode);

  if (!hddl_src->selected_target_context) {
    GST_ERROR ("Failed to create HDDL context.");
    return;
  }
  // FIXME: Remove hardcode
  if (hddl_src->connection_mode == CONNECT_TCP) {
    hddl_src->selected_target_context->hddl_tcp->host = DEFAULT_TCP_HOST;
    hddl_src->selected_target_context->hddl_tcp->port = DEFAULT_TCP_PORT_1;
  } else if (hddl_src->connection_mode == CONNECT_XLINK) {
    hddl_src->selected_target_context->hddl_xlink->xlink_handler->dev_type =
        HOST_DEVICE;
    hddl_src->selected_target_context->hddl_xlink->channelId =
        DEFAULT_XLINK_CHANNEL_1;
  }
}

gpointer
gst_hddl_src_xlink_receive_data_thread (GstHddlSrc * hddl_src)
{
  gboolean eos = FALSE;
  transfer_header *header = g_slice_new (transfer_header);
  XLinkError_t status;
  GstHddlXLink *hddl_xlink = hddl_src->selected_target_context->hddl_xlink;;
  GstMapInfo map;
  GstMemory *mem = NULL;
  GstBuffer *receive_buffer;

  while (!eos) {
    mem = gst_allocator_alloc (NULL, DATA_FRAGMENT_SIZE, NULL);
    gst_memory_map (mem, &map, GST_MAP_WRITE);
    status = gst_hddl_receive_data (hddl_xlink,
        hddl_src->connection_mode, (void **) &(map.data), sizeof(transfer_header));
    gst_memory_unmap (mem, &map);
    receive_buffer = gst_buffer_new ();
    gst_buffer_append_memory (receive_buffer, mem);
    gst_buffer_extract (receive_buffer, 0, header, sizeof (transfer_header));
    gst_buffer_resize (receive_buffer, sizeof (transfer_header), header->size);

    if (status != TRUE) {
      g_print ("XLinkReadData failed. Exiting hddlsrc thread...\n");
      gst_buffer_unref (receive_buffer);
      break;
    }
    if (header->type == EVENT_TYPE) {
      gst_hddl_src_xlink_process_event (hddl_src, header, &eos, receive_buffer);
    } else if (header->type == BUFFER_TYPE) {
      gst_hddl_src_xlink_process_buffer (hddl_src, header, receive_buffer);
    }

  }
  g_slice_free (transfer_header, header);
  return NULL;
}

gboolean
gst_hddl_src_xlink_process_event (GstHddlSrc * hddl_src,
    transfer_header * header, gboolean * eos, GstBuffer * receive_buffer)
{
  event_header *event_info = g_slice_new (event_header);
  GString *structure_string = NULL;
  GstStructure *structure = NULL;
  GstEvent *event;
  gboolean status;

  gst_buffer_extract (receive_buffer, 0, event_info, sizeof (event_header));
  gst_buffer_resize (receive_buffer, sizeof (event_header),
      event_info->string_len);

  /*  g_print ("Event type: %s\n", gst_event_type_get_name (event_info.type));
     g_print ("Event timestamp: %lu\n", event_info.timestamp);
     g_print ("Event seqnum: %u\n", event_info.seqnum);
     g_print ("Event string_len: %ld\n", event_info.string_len);
   */

  if (event_info->string_len) {
    structure_string = g_string_sized_new (event_info->string_len);
    gst_buffer_extract (receive_buffer, 0, structure_string->str,
        event_info->string_len);
    /*g_print ("Event string: %s\n", structure_string->str); */
    structure = gst_structure_from_string (structure_string->str, NULL);
  }
  /*  g_print ("header type = %x, header size = %ld\n", header.type, header.size);
   */
  event = gst_event_new_custom (event_info->type, structure);
  GST_EVENT_TIMESTAMP (event) = event_info->timestamp;
  GST_EVENT_SEQNUM (event) = event_info->seqnum;

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS)
    *eos = TRUE;

  status = gst_pad_push_event (hddl_src->srcpad, event);

  if (status == FALSE) {
    GST_ERROR ("gst_pad_push_event failed\n");
  }

  /* Clean up */
  gst_buffer_unref (receive_buffer);
  g_slice_free (event_header, event_info);

  return TRUE;
}

gboolean
gst_hddl_src_xlink_process_buffer (GstHddlSrc * hddl_src,
    transfer_header * header, GstBuffer * buffer)
{
  buffer_header *buffer_info = g_slice_new (buffer_header);
  gsize remaining_size;
#ifdef GVA_PLUGIN
  GstHddlXLink *hddl_xlink = hddl_src->selected_target_context->hddl_xlink;
  GstHddlConnectionMode mode = hddl_src->selected_target_context->conn_mode;
#endif

  /*g_print ("Buffer size = %lu\n", remaining_size);
     g_print ("Buffer pts = %lu\n", buffer_info.pts);
     g_print ("Buffer dts = %lu\n", buffer_info.dts);
     g_print ("Buffer duration = %lu\n", buffer_info.duration);
     g_print ("Buffer offset = %lu\n", buffer_info.offset);
     g_print ("Buffer offset_end = %lu\n", buffer_info.offset_end);
     g_print ("Buffer flags = %u\n", buffer_info.flags); */

  gst_buffer_extract (buffer, 0, buffer_info, sizeof (buffer_header));
  remaining_size = header->size - sizeof (buffer_header);
  gst_buffer_resize (buffer, sizeof (buffer_header), remaining_size);

//  gst_buffer_map (buffer, &map, GST_MAP_READ);
  /* g_print ("buffer[0] = %d\n", map.data[0]);
     g_print ("buffer[10] = %d\n", map.data[10]);
     g_print ("buffer[100] = %d\n", map.data[100]);
     g_print ("buffer[1000] = %d\n", map.data[1000]);
     g_print ("buffer[500] = %d\n", map.data[500]); */
//  gst_buffer_unmap (buffer, &map);

#ifdef GVA_PLUGIN
  if (buffer_info->num_video_roi_meta > 0) {
    gst_hddl_process_video_roi_meta (hddl_xlink,
        mode, header, buffer_info, buffer, &remaining_size);
  }

  if (buffer_info->num_gva_tensor_meta > 0) {
    gst_hddl_process_gva_tensor_meta (hddl_xlink,
        mode, header, buffer_info, buffer, &remaining_size);
  }

  if (buffer_info->num_gva_json_meta > 0) {
    gst_hddl_process_gva_json_meta (hddl_xlink,
        mode, header, buffer_info, buffer, &remaining_size);
  }
#endif

  GST_BUFFER_PTS (buffer) = buffer_info->pts;
  GST_BUFFER_DTS (buffer) = buffer_info->dts;
  GST_BUFFER_DURATION (buffer) = buffer_info->duration;
  GST_BUFFER_OFFSET (buffer) = buffer_info->offset;
  GST_BUFFER_OFFSET_END (buffer) = buffer_info->offset_end;
  GST_BUFFER_FLAGS (buffer) = buffer_info->flags;

  g_slice_free (buffer_header, buffer_info);

  gst_pad_push (hddl_src->srcpad, buffer);

  return TRUE;
}

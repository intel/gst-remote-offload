/*
 *  gsthddlvideoroimeta.h - HDDL GVA ROI Meta utility header
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
#ifndef GST_HDDL_VIDEO_ROI_META_H
#define GST_HDDL_VIDEO_ROI_META_H

#include <gst/gst.h>
#include "gva_tensor_meta.h"
#include "gva_json_meta.h"
#include "gsthddlcontext.h"

/* GVA Tensor meta header */
typedef struct
{
  gsize data_len;
  gsize array_data_len;
} gva_tensor_meta_header;

/* GVA Json meta header */
typedef struct
{
  gsize message_string_len;
} gva_json_meta_header;


G_BEGIN_DECLS
gboolean gst_hddl_process_video_roi_meta (GstHddlXLink * hddl_xlink,
    GstHddlConnectionMode mode,
    transfer_header * header, buffer_header * buffer_info, GstBuffer * buffer,
    gsize * remaining_size);

gboolean gst_hddl_process_gva_tensor_meta (GstHddlXLink * hddl_xlink,
    GstHddlConnectionMode mode,
    transfer_header * header,
    buffer_header * buffer_info, GstBuffer * buffer, gsize * remaining_size);

gboolean gst_hddl_process_gva_json_meta (GstHddlXLink * hddl_xlink,
    GstHddlConnectionMode mode,
    transfer_header * header,
    buffer_header * buffer_info, GstBuffer * buffer, gsize * remaining_size);

gboolean gst_hddl_calculate_meta_size (GstBuffer * buffer,
    gpointer pointer, transfer_header * header);

gboolean gst_hddl_send_video_roi_meta (GstHddlContext * hddl_context,
    GstBuffer * buffer, gpointer * state, GstBuffer * send_buffer,
    video_roi_meta_header * meta_header, GList * meta_param_list);

gboolean gst_hddl_send_video_roi_meta_param (GstHddlContext * hddl_context,
    gpointer data, video_roi_meta_param_header * param_header,
    GstBuffer * send_buffer);

gboolean gst_hddl_send_gva_tensor_meta (GstHddlContext * hddl_context,
    GstBuffer * buffer,
    gpointer * state, GstBuffer * send_buffer,
    gva_tensor_meta_header * meta_header);

gboolean gst_hddl_send_gva_json_meta (GstHddlContext * hddl_context,
    GstBuffer * buffer,
    gpointer * state, GstBuffer * send_buffer,
    gva_json_meta_header * meta_header);

G_END_DECLS
#endif

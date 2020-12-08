/*
 *  gsthddlvideoroimeta.c - HDDL GVA ROI Meta utility functions
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
#include "gsthddlvideoroimeta.h"
#include "gsthddlutils.h"
#include <safe_mem_lib.h>

gboolean
gst_hddl_process_video_roi_meta (GstHddlXLink * hddl_xlink,
    GstHddlConnectionMode mode,
    transfer_header * header, buffer_header * buffer_info, GstBuffer * buffer,
    gsize * remaining_size)
{
  gst_buffer_extract (buffer, 0, header, sizeof (transfer_header));
  *remaining_size = *remaining_size - sizeof (transfer_header);
  gst_buffer_resize (buffer, sizeof (transfer_header), *remaining_size);

  if (header->type == BUFFER_VIDEO_ROI_META_TYPE) {
    for (guint i = 0; i < buffer_info->num_video_roi_meta; i++) {
      video_roi_meta_header *meta_header = g_slice_new (video_roi_meta_header);
      GString *roi_type_string = NULL;
      GstVideoRegionOfInterestMeta *meta;

      gst_buffer_extract (buffer, 0, meta_header,
          sizeof (video_roi_meta_header));
      *remaining_size = *remaining_size - sizeof (video_roi_meta_header);
      gst_buffer_resize (buffer, sizeof (video_roi_meta_header),
          *remaining_size);

      if (meta_header->roi_type_string_len) {
        roi_type_string =
            g_string_sized_new (meta_header->roi_type_string_len);
        gst_buffer_extract (buffer, 0, roi_type_string->str,
            meta_header->roi_type_string_len);
        *remaining_size = *remaining_size - meta_header->roi_type_string_len;
        gst_buffer_resize (buffer, meta_header->roi_type_string_len,
            *remaining_size);
      }

      gchar *input = roi_type_string ? roi_type_string->str : NULL;
      meta =
          gst_buffer_add_video_region_of_interest_meta (buffer,
          input, meta_header->x, meta_header->y, meta_header->w,
          meta_header->h);

      if (roi_type_string) {
        g_string_free (roi_type_string, TRUE);
      }

      for (guint j = 0; j < meta_header->num_param; j++) {
        video_roi_meta_param_header *param_header =
            g_slice_new (video_roi_meta_param_header);
        gchar *structure_string = NULL;
        GstStructure *structure = NULL;

        gst_buffer_extract (buffer, 0, param_header,
            sizeof (video_roi_meta_param_header));
        *remaining_size =
            *remaining_size - sizeof (video_roi_meta_param_header);
        gst_buffer_resize (buffer, sizeof (video_roi_meta_param_header),
            *remaining_size);

        if (param_header->string_len) {
          structure_string = g_malloc (param_header->string_len);
          gst_buffer_extract (buffer, 0, structure_string,
              param_header->string_len);
          *remaining_size = *remaining_size - param_header->string_len;
          gst_buffer_resize (buffer, param_header->string_len, *remaining_size);

          if (structure_string) {
            structure = gst_structure_from_string (structure_string, NULL);
          }
          g_free (structure_string);
        }

        if (param_header->is_data_buffer) {
          void *message = g_slice_alloc0 (param_header->data_buffer_size);
          gsize num_element;
          GVariant *variant;

          gst_buffer_extract (buffer, 0, message,
              param_header->data_buffer_size);
          *remaining_size = *remaining_size - param_header->data_buffer_size;
          gst_buffer_resize (buffer, param_header->data_buffer_size,
              *remaining_size);

          variant =
              g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, message,
              param_header->data_buffer_size, 1);
          gst_structure_set (structure, "data_buffer", G_TYPE_VARIANT,
              variant, "data", G_TYPE_POINTER,
              g_variant_get_fixed_array (variant, &num_element, 1), NULL);
          g_slice_free1 (param_header->data_buffer_size, message);
          message = NULL;
        }

        gst_video_region_of_interest_meta_add_param (meta, structure);

      }
    }
  }

  return TRUE;
}

gboolean
gst_hddl_process_gva_tensor_meta (GstHddlXLink * hddl_xlink,
    GstHddlConnectionMode mode,
    transfer_header * header, buffer_header * buffer_info, GstBuffer * buffer,
    gsize * remaining_size)
{
  gst_buffer_extract (buffer, 0, header, sizeof (transfer_header));
  *remaining_size = *remaining_size - sizeof (transfer_header);
  gst_buffer_resize (buffer, sizeof (transfer_header), *remaining_size);

  if (header->type == BUFFER_GVA_TENSOR_META_TYPE) {
    for (guint i = 0; i < buffer_info->num_gva_tensor_meta; i++) {
      gva_tensor_meta_header *meta_header =
        g_slice_new (gva_tensor_meta_header);
      GstGVATensorMeta *tensor_meta;

      gst_buffer_extract (buffer, 0, meta_header,
        sizeof (gva_tensor_meta_header));
      *remaining_size = *remaining_size - sizeof (gva_tensor_meta_header);
      gst_buffer_resize (buffer, sizeof (gva_tensor_meta_header),
        *remaining_size);

      tensor_meta = GST_GVA_TENSOR_META_ADD (buffer);

      GString * data_string = NULL;
      data_string = g_string_sized_new (meta_header->data_len);
      gst_buffer_extract (buffer, 0, data_string->str,
        meta_header->data_len);
      if (data_string->str) {
    	GstStructure *new_struct = gst_structure_from_string (data_string->str, NULL);
    	if (new_struct == NULL) {
    	  g_print("new struct is null\n");
    	}
    	if( tensor_meta->data )
    	  gst_structure_free(tensor_meta->data);
    	tensor_meta->data = new_struct;
    
    	if (meta_header->array_data_len) {
          void *message = g_slice_alloc0 (meta_header->array_data_len);
          gst_buffer_extract (buffer, 0, message, meta_header->array_data_len);
          *remaining_size = *remaining_size - meta_header->array_data_len;
          gst_buffer_resize (buffer, meta_header->array_data_len, *remaining_size);
    	  gsize num_element;
          GVariant *variant;
          variant = g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, message,
    	              meta_header->array_data_len, 1);
          gst_structure_set (tensor_meta->data, "data_buffer", G_TYPE_VARIANT,
            variant, "data", G_TYPE_POINTER,
    	  g_variant_get_fixed_array (variant, &num_element, 1), NULL);
    	  g_slice_free1 (meta_header->array_data_len, message);
    	  message = NULL;
    	}
      }
      *remaining_size = *remaining_size - meta_header->data_len;
      gst_buffer_resize (buffer, meta_header->data_len,
          *remaining_size);
    }
  }
  return TRUE;
}

gboolean
gst_hddl_process_gva_json_meta (GstHddlXLink * hddl_xlink,
    GstHddlConnectionMode mode,
    transfer_header * header, buffer_header * buffer_info, GstBuffer * buffer,
    gsize * remaining_size)
{
  gst_buffer_extract (buffer, 0, header, sizeof (transfer_header));
  *remaining_size = *remaining_size - sizeof (transfer_header);
  gst_buffer_resize (buffer, sizeof (transfer_header), *remaining_size);

  if (header->type == BUFFER_GVA_JSON_META_TYPE) {
    for (guint i = 0; i < buffer_info->num_gva_json_meta; i++) {
      gva_json_meta_header *meta_header = g_slice_new (gva_json_meta_header);;
      GString *msg = NULL;
      GstGVAJSONMeta *json_meta;

      gst_buffer_extract (buffer, 0, meta_header,
          sizeof (gva_json_meta_header));
      *remaining_size = *remaining_size - sizeof (gva_json_meta_header);
      gst_buffer_resize (buffer, sizeof (gva_json_meta_header),
          *remaining_size);
      json_meta = GST_GVA_JSON_META_ADD (buffer);

      if (meta_header->message_string_len) {
        msg = g_string_sized_new (meta_header->message_string_len);
        gst_buffer_extract (buffer, 0, msg->str,
            meta_header->message_string_len);
        *remaining_size = *remaining_size - meta_header->message_string_len;
        gst_buffer_resize (buffer, meta_header->message_string_len,
            *remaining_size);

      }
      if (msg) {
        json_meta->message = g_strdup (msg->str);
      }

      //  FIXME: Remove after testing
      //g_print ("json message: %s\n", json_meta->message);

    }
  }
  return TRUE;
}

gboolean
gst_hddl_calculate_meta_size (GstBuffer * buffer,
    gpointer pointer, transfer_header * header)
{
  if (header->type == BUFFER_VIDEO_ROI_META_TYPE) {
    header->size = header->size + sizeof (video_roi_meta_header);
    GstVideoRegionOfInterestMeta *meta =
        (GstVideoRegionOfInterestMeta
        *) (gst_buffer_iterate_meta_filtered (buffer, pointer,
            GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE));
    const gchar *roi_type_string = g_quark_to_string (meta->roi_type);
    if (roi_type_string) {
      header->size = header->size + strlen (roi_type_string) + 1;
    }

    GList *list;
    list = meta->params;
    while (list != NULL) {
      header->size = header->size + sizeof (video_roi_meta_param_header);
      GstStructure *structure = (GstStructure *) (list->data);
      GstStructure *structure_cpy = NULL;
      gchar *structure_string = NULL;
      gsize buffer_size = 0;
      const GValue *value = gst_structure_get_value (structure, "data_buffer");
      if (value) {
        g_variant_get_fixed_array (g_value_get_variant (value), &buffer_size,
            1);
        structure_cpy = gst_structure_copy (structure);
        gst_structure_remove_field (structure_cpy, "data_buffer");
        gst_structure_remove_field (structure_cpy, "data");
        structure = structure_cpy;
      }
      header->size = header->size + buffer_size;
      structure_string = gst_structure_to_string (structure);

      if (structure_cpy) {
        gst_structure_free (structure_cpy);
      }

      if (structure_string) {
        header->size = header->size + strlen (structure_string) + 1;
        g_free (structure_string);
      }

      list = list->next;
    }

    return TRUE;

  } else if (header->type == BUFFER_GVA_TENSOR_META_TYPE) {
    header->size = header->size + sizeof (gva_tensor_meta_header);
    GstGVATensorMeta *meta =
        (GstGVATensorMeta *) (gst_buffer_iterate_meta_filtered (buffer, pointer,
            gst_gva_tensor_meta_api_get_type ()));
    GstStructure *structure = (GstStructure *) (meta->data);
    GstStructure *structure_cpy = NULL;
    gsize buffer_size = 0;
    const GValue *value = gst_structure_get_value (structure, "data_buffer");
    if (value) {
      g_variant_get_fixed_array (g_value_get_variant (value), &buffer_size, 1);
      structure_cpy = gst_structure_copy (structure);
      gst_structure_remove_field (structure_cpy, "data_buffer");
      gst_structure_remove_field (structure_cpy, "data");
      structure = structure_cpy;
    }
    header->size = header->size +
	  strlen (gst_structure_to_string(structure)) + 1;
    header->size = header->size + buffer_size;
    if (structure_cpy) {
      gst_structure_free (structure_cpy);
    }
  } else if (header->type == BUFFER_GVA_JSON_META_TYPE) {
    header->size = header->size + sizeof (gva_json_meta_header);
    GstGVAJSONMeta *meta =
        (GstGVAJSONMeta *) (gst_buffer_iterate_meta_filtered (buffer, pointer,
            gst_gva_json_meta_api_get_type ()));
    if (meta->message) {
      header->size = header->size + strlen (meta->message) + 1;
    }

    return TRUE;
  }
  return TRUE;
}

gboolean
gst_hddl_send_video_roi_meta (GstHddlContext * hddl_context,
    GstBuffer * buffer, gpointer * state, GstBuffer * send_buffer,
    video_roi_meta_header * meta_header, GList * meta_param_list)
{
  GstMeta *meta;
  GstVideoRegionOfInterestMeta *video_roi_meta;
  const gchar *roi_type_string;
  video_roi_meta_param_header *param_header;

  meta =
      gst_buffer_iterate_meta_filtered (buffer, state,
      GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE);
  video_roi_meta = (GstVideoRegionOfInterestMeta *) (meta);
  meta_header->roi_type_string_len = 0;
  roi_type_string = g_quark_to_string (video_roi_meta->roi_type);
  if (roi_type_string) {
    meta_header->roi_type_string_len = strlen (roi_type_string) + 1;
  }
  meta_header->x = video_roi_meta->x;
  meta_header->y = video_roi_meta->y;
  meta_header->w = video_roi_meta->w;
  meta_header->h = video_roi_meta->h;
  meta_header->num_param = g_list_length (video_roi_meta->params);

  gst_hddl_utils_wrap_data_in_buffer (meta_header,
      sizeof (video_roi_meta_header), send_buffer);

  if (meta_header->roi_type_string_len) {
    gst_hddl_utils_wrap_data_in_buffer (roi_type_string,
        meta_header->roi_type_string_len, send_buffer);
  }

  if (meta_header->num_param) {
    GList *list;
    for (list = video_roi_meta->params; list != NULL; list = list->next) {
      param_header = g_slice_new (video_roi_meta_param_header);
      gst_hddl_send_video_roi_meta_param (hddl_context, list->data,
          param_header, send_buffer);
      meta_param_list = g_list_append (meta_param_list, param_header);
    }
  }
  return TRUE;
}

gboolean
gst_hddl_send_video_roi_meta_param (GstHddlContext * hddl_context,
    gpointer data, video_roi_meta_param_header * param_header,
    GstBuffer * send_buffer)
{
  GstStructure *structure = (GstStructure *) (data);
  GstStructure *structure_cpy = NULL;
  const void *array_data = NULL;
  const GValue *value;
  gchar *structure_string;

  param_header->string_len = 0;
  param_header->is_data_buffer = 0;
  param_header->data_buffer_size = 0;
  value = gst_structure_get_value (structure, "data_buffer");
  if (value) {
    GVariant *variant;
    gsize num_element;

    param_header->is_data_buffer = 1;
    variant = g_value_get_variant (value);
    array_data = g_variant_get_fixed_array (variant, &num_element, 1);
    if (array_data) {
      param_header->data_buffer_size = num_element;
    }
    structure_cpy = gst_structure_copy (structure);
    gst_structure_remove_field (structure_cpy, "data_buffer");
    gst_structure_remove_field (structure_cpy, "data");

    structure = structure_cpy;
  }

  structure_string = gst_structure_to_string (structure);
  if (structure_string) {
    param_header->string_len = strlen (structure_string) + 1;
  }


  gst_hddl_utils_wrap_data_in_buffer (param_header,
      sizeof (video_roi_meta_param_header), send_buffer);

  if (param_header->string_len) {
    gst_hddl_utils_wrap_data_in_buffer (structure_string,
        param_header->string_len, send_buffer);
  }

  if (array_data) {
    gst_hddl_utils_wrap_data_in_buffer (array_data,
        param_header->data_buffer_size, send_buffer);
  }

  if (structure_cpy) {
    gst_structure_free (structure_cpy);
  }

  if (structure_string) {
    // FIXME: Need to be free elsewhere after it has been sent
    //  g_free (structure_string);
  }
  return TRUE;
}

gboolean
gst_hddl_send_gva_tensor_meta (GstHddlContext * hddl_context,
    GstBuffer * buffer, gpointer * state, GstBuffer * send_buffer,
    gva_tensor_meta_header * meta_header)
{
  GstMeta *meta;
  GstGVATensorMeta *tensor_meta;

  meta =
      gst_buffer_iterate_meta_filtered (buffer, state,
      gst_gva_tensor_meta_api_get_type ());
  tensor_meta = (GstGVATensorMeta *) (meta);
  const GValue *value;
  gsize num_element = 0;
  const void* array_data = NULL;
  value = gst_structure_get_value (tensor_meta->data, "data_buffer");
  if (value) {
    GVariant *variant;
    variant = g_value_get_variant (value);
    array_data = g_variant_get_fixed_array (variant, &num_element, 1);
    meta_header->array_data_len = num_element;
    gst_structure_remove_field (tensor_meta->data, "data_buffer");
  }
  gst_structure_remove_field (tensor_meta->data, "data");
  gchar *data = gst_structure_to_string (tensor_meta->data);
  meta_header->data_len = strlen (data) + 1;

  gst_hddl_utils_wrap_data_in_buffer (meta_header,
      sizeof (gva_tensor_meta_header), send_buffer);
  gst_hddl_utils_wrap_data_in_buffer (data,
      meta_header->data_len, send_buffer);
  if (array_data)
    gst_hddl_utils_wrap_data_in_buffer (array_data,
        num_element, send_buffer);

  return TRUE;
}

gboolean
gst_hddl_send_gva_json_meta (GstHddlContext * hddl_context,
    GstBuffer * buffer, gpointer * state, GstBuffer * send_buffer,
    gva_json_meta_header * meta_header)
{
  GstMeta *meta;
  GstGVAJSONMeta *json_meta;

  meta =
      gst_buffer_iterate_meta_filtered (buffer, state,
      gst_gva_json_meta_api_get_type ());
  json_meta = (GstGVAJSONMeta *) (meta);
  meta_header->message_string_len = 0;

  if (json_meta->message) {
    meta_header->message_string_len = strlen (json_meta->message) + 1;
  }

  gst_hddl_utils_wrap_data_in_buffer (meta_header,
      sizeof (gva_json_meta_header), send_buffer);

  if (json_meta->message) {
    gst_hddl_utils_wrap_data_in_buffer (json_meta->message,
        meta_header->message_string_len, send_buffer);
  }

  return TRUE;
}

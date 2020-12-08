/*
 *  gsthddlxlinkcomms.c - xlink connection unitilies for HDDL elements
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

#include "gsthddlxlinkcomms.h"
#include <safe_mem_lib.h>
#include <unistd.h>
#include <gst/gstinfo.h>
#define DEVICE_ID_LIST 50

#define SW_DEVICE_ID_PCIE_INTERFACE 0x1
#define SW_DEVICE_ID_INTERFACE_SHIFT 24U
#define SW_DEVICE_ID_INTERFACE_MASK  0x7
#define GET_INTERFACE_FROM_SW_DEVICE_ID(id) \
      ((id >> SW_DEVICE_ID_INTERFACE_SHIFT) & SW_DEVICE_ID_INTERFACE_MASK)

GST_DEBUG_CATEGORY_STATIC (gst_hddl_xlink_debug);
#define GST_CAT_DEFAULT gst_hddl_xlink_debug
#define GST_HDDL_XLINK_TAG "hddlsrc/sink XLink"
#define GST_HDDL_XLINK_DESC "GstHddlXLink"

gboolean
gst_hddl_xlink_initialize ()
{
  XLinkError_t status = X_LINK_COMMUNICATION_NOT_OPEN;
  status = xlink_initialize ();

  if (status != X_LINK_SUCCESS)
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_hddl_xlink_debug, GST_HDDL_XLINK_TAG,
                            0, GST_HDDL_XLINK_DESC);

  g_print ("XLink Initialized\n");
  return TRUE;
}

gboolean
gst_hddl_xlink_connect_device (XLinkHandler_t * xlink_handler)
{
  guint num_devices = 0;
  guint sw_device_id_list[DEVICE_ID_LIST];
  XLinkError_t status;
  status = xlink_get_device_list (sw_device_id_list, &num_devices);

  if (status != X_LINK_SUCCESS) {
      g_print ("xlink_get_device_list returned error %d", status);
      return FALSE;
  }
  if (num_devices > 0) {
      xlink_handler->sw_device_id = 0;
      for (guint i = 0; i < num_devices; i++)
      {
        guint interface = GET_INTERFACE_FROM_SW_DEVICE_ID(sw_device_id_list[i]);
        if ( interface == SW_DEVICE_ID_PCIE_INTERFACE )
        {
           xlink_handler->sw_device_id = sw_device_id_list[i];
           break;
        }
      }

      if( !xlink_handler->sw_device_id )
      {
         g_print ("Can't find PCIE device\n");
         return FALSE;
      }
  }
  else {
      g_print ("Can't find device\n");
      return FALSE;
  }
  status = xlink_connect (xlink_handler);

  if (status != X_LINK_SUCCESS) {
    g_print ("xlink_connect failed with error %d\n", status);
    return FALSE;
  }

  GST_INFO ("XLink Connected\n");
  return TRUE;
}

gboolean
gst_hddl_xlink_connect (XLinkHandler_t * xlink_handler, XLinkChannelId_t channelId)
{
  /* Assume XLinkInitialize() and XLinkConnect() have been called by HDDL Scheduler
   * during dynamic channel allocation in remoteoffloadbin.
   * HDDL Scheduler shall request this dynamic channel from Resource Manager at that
   * point too */
  XLinkError_t status;

  /* TODO: Define the maximum data size to be transferred and timeout for this
   * channel */
  guint timeout = 10000;

  status = xlink_open_channel (xlink_handler, channelId, RXB_TXN,
      DATA_FRAGMENT_SIZE, timeout);
  if (status != X_LINK_SUCCESS) {
    g_print ("Can't open channel %d. XLink error %d\n", channelId, status);
    return FALSE;
  }

  GST_INFO ("XLink Channel: %d Opened\n", channelId);
  return TRUE;
}

gboolean
gst_hddl_xlink_listen_client (GstHddlXLink * hddl_xlink)
{
  gboolean status;

  status =
      gst_hddl_xlink_connect (hddl_xlink->xlink_handler, hddl_xlink->channelId);

  return status;
}

gboolean
gst_hddl_xlink_transfer (GstHddlXLink * hddl_xlink, void *buffer, size_t size)
{
  XLinkError_t ret;
  gint retries = 0;
  ret =
      xlink_write_data (hddl_xlink->xlink_handler, hddl_xlink->channelId,
      (uint8_t *) buffer, size);

  GST_LOG ("Attempt to write %lu byte of data\n", size);

  if (ret != X_LINK_SUCCESS) {
    // TODO: Remove retry block
    // Only do re-write if channel full error is returned
    if (ret != X_LINK_CHAN_FULL) {
      GST_ERROR ("hddl_xlink_transfer of %lu bytes returned xlink error %d. Not retrying\n",
          size, ret);
      return FALSE;
    }
    while (ret == X_LINK_CHAN_FULL && retries < 3) {
      retries++;
      g_usleep (3000);
      GST_WARNING ("Retry attempt #%d to write %lu of data\n", retries, size);
      ret = xlink_write_data (hddl_xlink->xlink_handler, hddl_xlink->channelId,
              (uint8_t*) buffer, size);
    }
    if (ret != X_LINK_SUCCESS) {
      GST_ERROR ("hddl_xlink_transfer of %lu bytes returned xlink error %d after retrying "
          "%d times\n", size, ret, retries);
      return FALSE;
    }
  }

  GST_LOG ("hddl_xlink_transfer of %lu bytes successful after %d retries\n", size, retries);
  return TRUE;
}

gboolean
gst_hddl_xlink_receive (GstHddlXLink * hddl_xlink, void **buffer,
    size_t transfer_size)
{
  uint32_t size = 0;
  // TODO: Remove retries variable when retry block below is removed
  gint retries = 0;
  XLinkError_t ret;
  ret = xlink_read_data (hddl_xlink->xlink_handler, hddl_xlink->channelId,
      (uint8_t**) buffer, &size);
  if (ret != X_LINK_SUCCESS) {
    // TODO: Remove retry block
    // Only do re-read if timeout error is returned
    if (ret != X_LINK_TIMEOUT) {
      GST_ERROR ("hddl_xlink_receive of %d bytes returned xlink error %d. Not retrying\n",
          size, ret);
      return FALSE;
    }
    // Assume channel full error from here.
    while (ret == X_LINK_TIMEOUT && retries < 3) {
      retries++;
      GST_WARNING ("Retry attempt #%d to read %u byte of data", retries, size);
      ret = xlink_read_data (hddl_xlink->xlink_handler, hddl_xlink->channelId,
          (uint8_t**) buffer, &size);
    }
    if (ret != X_LINK_SUCCESS) {
      GST_ERROR ("hddl_xlink_receive of %u bytes returns xlink error %d after retrying"
          " %d times\n", size, ret, retries);
      return FALSE;
    }
  }
  if (*buffer == NULL) {
    GST_ERROR ("Received buffer is null\n");
    return FALSE;
  }

  ret = xlink_release_data (hddl_xlink->xlink_handler, hddl_xlink->channelId, NULL);
  if (ret != X_LINK_SUCCESS) {
    GST_ERROR ("hddl_xlink_read returns xlink error %d in xlink_release_data\n", ret);
    return FALSE;
  }

  GST_LOG ("hddl_xlink_receive of %d bytes successful after %d retries\n", size, retries);
  return TRUE;
}

gboolean
gst_hddl_xlink_shutdown (GstHddlXLink * hddl_xlink)
{
  /* FIXME: XLinkCloseChannel will return success even when it is called with different channelId
   * HDDL plugins will get into segfault during cleanup for some cases */
  XLinkError_t status =
      xlink_close_channel (hddl_xlink->xlink_handler, hddl_xlink->channelId);
  if (status != X_LINK_SUCCESS){
    g_print ("Cannot close XLink channel %d error %d\n", hddl_xlink->channelId, status);
    return FALSE;
  }

  g_print ("XLink Channel: %d Closed\n", hddl_xlink->channelId);
  return TRUE;
}

gboolean
gst_hddl_xlink_disconnect (XLinkHandler_t * xlink_handler)
{
  XLinkError_t status = xlink_disconnect (xlink_handler);

  if (status != X_LINK_SUCCESS){
    g_print ("Cannot disconnect XLink error %d\n", status);
    return FALSE;
  }

  g_print ("XLink Disconnected\n");
  return TRUE;
}

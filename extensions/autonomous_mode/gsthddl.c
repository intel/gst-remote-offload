/*
 *  gsthddl.c - HDDL elements registration
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author: Soon, Thean Siew <thean.siew.soon@intel.com>
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

#include "version.h"

// FIXME: Include the following header files when they are available
#include "gsthddlsrc.h"
#include "gsthddlsink.h"

#ifdef HDDL_HOST
#include "gsthddlremoteoffloadbin.h"
#include "gsthddlingress.h"
// FIXME: Include the following header file when they are available
#include "gsthddlegress.h"
#endif

#define PLUGIN_DESC		"HDDL streamer elements"
#define PLUGIN_LICENSE		"LGPL"
/* FIXME: update PACKAGE_ORIGIN once the URL is confirmed */
#define PACKAGE_ORIGIN		"http://www.intel.com"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = TRUE;

/* HDDL_HOST macro passed in from build configurations */
#ifdef HDDL_HOST
  ret &= gst_element_register (plugin, "remoteoffloadbin", GST_RANK_NONE,
      GST_TYPE_REMOTEOFFLOAD_BIN);
  ret &= gst_element_register (plugin, "hddlingress", GST_RANK_NONE,
      GST_TYPE_HDDL_INGRESS);

  // FIXME: Uncomment when the actual file available
  ret &= gst_element_register (plugin, "hddlegress", GST_RANK_NONE,
      GST_TYPE_HDDL_EGRESS);
#endif

  // FIXME: Uncomment the followings when actual files available
  /* HDDLSrc and HDDLSink should be installed on both host and target */
  ret &= gst_element_register (plugin, "hddlsrc", GST_RANK_NONE,
        GST_TYPE_HDDL_SRC);
  ret &= gst_element_register (plugin, "hddlsink", GST_RANK_NONE,
      GST_TYPE_HDDL_SINK);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, hddl, PLUGIN_DESC,
    plugin_init, PACKAGE_VERSION, PLUGIN_LICENSE, PACKAGE, PACKAGE_ORIGIN)

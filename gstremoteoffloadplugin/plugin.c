/*
 *  plugin.c - Gstreamer Remote Offload Plugin
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef ENABLE_REMOTEOFFLOADBIN
#include "gstremoteoffloadbin.h"
#endif

#include "gstremoteoffloadegress.h"
#include "gstremoteoffloadingress.h"


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = TRUE;

#ifdef ENABLE_REMOTEOFFLOADBIN
  ret =  gst_element_register(plugin,
                              "remoteoffloadbin",
                              GST_RANK_NONE,
                              GST_TYPE_REMOTEOFFLOADBIN);
#endif

  if( ret )
     ret =  gst_element_register(plugin,
                                 "remoteoffloadegress",
                                 GST_RANK_NONE,
                                 GST_TYPE_REMOTEOFFLOAD_EGRESS);

  if( ret )
     ret =  gst_element_register(plugin,
                                 "remoteoffloadingress",
                                 GST_RANK_NONE,
                                 GST_TYPE_REMOTEOFFLOAD_INGRESS);

  return ret;
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "remoteoffload"
#endif

/* Version number of package */
#define VERSION "1.0.0"

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    remoteoffload,
    "Remote Offload plugin",
    plugin_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/")

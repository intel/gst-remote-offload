/*
 *  appsinkcomparer.h - AppSinkComparer object

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
 *
 *  This object is used for GStreamer Unit testing. A test can
 *   "attach" two (or more) appsink's across one (or multiple)
 *   pipelines. This object will then compare the GstBuffer's
 *   (and optionally queries, events, etc.)received by the multiple
 *   appsink's, and can emit various signals if/when things don't match.
 *
 *   It was primarily designed to test the GStreamer Remote Offload
 *   framework. The idea here is to run "native" pipelines concurrently
 *   with remote-offloaded pipelines, and ensure that the results match.
 */
#ifndef __APPSINKCOMPARER_H__
#define __APPSINKCOMPARER_H__

#include <gst/gst.h>

G_BEGIN_DECLS


#define APPSINKCOMPARER_TYPE (appsink_comparer_get_type ())
G_DECLARE_FINAL_TYPE (AppSinkComparer, appsink_comparer, APPSINK, COMPARER, GObject)

//public interfaces
AppSinkComparer *appsink_comparer_new();

//Register an appsink element to track. This should be called
// for each appsink element that you'd like to compare buffers from.
gboolean appsink_comparer_register(AppSinkComparer *comparer,
                                   GstElement *appsink);

//Wait for the comparer to finish comparing the remaining data
// residing in it's queues. This should be called before
// destroying the comparer object, but after the completion
// of data getting pushed by appsink's. For example, if
// comparing the results of appsink's across two pipelines,
// the proper time to call this would be after those two
// pipelines have transitioned to NULL state, but before
// calling g_object_unref() on this comparer.
//While the queues are being flushed, the "mismatch" signal
// will still be called as usual. This function will return
// FALSE if there is a mismatched number of entries across
// internal queues. For example, if appsink0 pushed 33 samples,
// and appsink1 pushed 40 samples.
gboolean appsink_comparer_flush(AppSinkComparer *comparer);


G_BEGIN_DECLS



#endif /* __APPSINKCOMPARER_H__ */

/*
 *  gstbufferspersecond.h - GstBuffersPerSecond element
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

#ifndef __GST_BUFFERSPERSECOND_H__
#define __GST_BUFFERSPERSECOND_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_BUFFERSPERSECOND \
  (gst_buffers_per_second_get_type())
#define GST_BUFFERSPERSECOND(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BUFFERSPERSECOND,GstBuffersPerSecond))
#define GST_BUFFERSPERSECOND_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BUFFERSPERSECOND,GstBuffersPerSecondClass))
#define GST_IS_BUFFERSPERSECOND(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BUFFERSPERSECOND))
#define GST_IS_BUFFERSPERSECOND_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BUFFERSPERSECOND))

typedef struct _GstBuffersPerSecond      GstBuffersPerSecond;
typedef struct _GstBuffersPerSecondClass GstBuffersPerSecondClass;

struct _GstBuffersPerSecond
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  GstClockTime bps_update_interval;
  gdouble max_bps;
  gdouble min_bps;
  gint buffers_received; /* ATOMIC */
  guint64 last_buffers_received;

  gsize bytes_received;
  gsize last_bytes_received;

  GstClockTime start_ts;
  GstClockTime last_ts;
  GstClockTime interval_ts;

  //For tracking CPU utilization
  gboolean btrackcpuutil;
  gboolean bcpucollectionrunning;
  unsigned long long stat_user, stat_nice, stat_kernel,
                     stat_idle, stat_iowait, stat_irq,
                     stat_softirq;

  //For tracking memory utilization
  gboolean btrackmemutil;
  gboolean bmemcollectionrunning;
  GThread *memtracker_thread;
  GMutex memmutex;
  GCond  memcond;
  long long baselinePhysMemUsed;
  long long maxPhysMemUsed;
  gdouble avgPhysMemUsed;

  //for tracking latency
  gboolean blatency_is_source;
  gchar *latency_track_from;
  GstElement *bps_latency_track_source;

  GMutex statscollectmutex;
  GSList *clockid_list;
  GSList *wallclockts_list;
  gboolean bclock_wallclock_list_reversed;
  GSList *perframe_n_roi_list; //for tracking num. ROI's per frame
  GSList *perframe_new_objs;
  GSList *perframe_untagged_objs;

  gboolean btracknrois;
  gboolean btracknewobjs;
  GHashTable *objid_hash;

  gchar *statsfile;

};

struct _GstBuffersPerSecondClass
{
  GstElementClass parent_class;
};

GType gst_buffers_per_second_get_type (void);

G_END_DECLS

#endif /* __GST_BUFFERSPERSECOND_H__ */

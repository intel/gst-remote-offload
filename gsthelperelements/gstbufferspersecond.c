/*
 *  gstbufferspersecond.c - GstBuffersPerSecond element
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
#  include <config.h>
#endif
#include <stdio.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <gst/gst.h>
#include <gst/video/gstvideometa.h>
#include "gstbufferspersecond.h"

#define DEFAULT_BPS_UPDATE_INTERVAL_MS 500      /* 500 ms */

GST_DEBUG_CATEGORY_STATIC (gst_buffers_per_second_debug);
#define GST_CAT_DEFAULT gst_buffers_per_second_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_BPS_UPDATE_INTERVAL,
  PROP_TRACK_CPU_UTIL,
  PROP_TRACK_MEM_UTIL,
  PROP_LATENCY_IS_SOURCE,
  PROP_LATENCY_TRACK_FROM,
  PROP_DUMP_FRAME_STATS,
  PROP_TRACK_ROI_PER_FRAME,
  PROP_TRACK_NEW_OBJS_PER_FRAME
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

#define gst_buffers_per_second_parent_class parent_class
G_DEFINE_TYPE (GstBuffersPerSecond, gst_buffers_per_second, GST_TYPE_ELEMENT);

static void gst_buffers_per_second_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_buffers_per_second_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_buffers_per_second_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_buffers_per_second_sink_event (GstPad * pad, GstObject * parent, GstEvent * event);
static GstFlowReturn gst_buffers_per_second_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);

static gboolean gst_buffers_per_second_memutil_start(GstBuffersPerSecond *self);
static gboolean gst_buffers_per_second_memutil_stop(GstBuffersPerSecond *self);

/* GObject vmethod implementations */
static inline long long gst_buffers_per_second_phys_mem_used_now()
{
   struct sysinfo memInfo;
   sysinfo (&memInfo);

   long long totalPhysMem = memInfo.totalram;
   totalPhysMem *= memInfo.mem_unit;

   long long physMemUsed = memInfo.totalram - memInfo.freeram;
   physMemUsed *= memInfo.mem_unit;

   return physMemUsed;
}

static void
gst_buffers_per_second_finalize (GObject *gobject)
{
   GstBuffersPerSecond *self = (GstBuffersPerSecond *)gobject;

   //Thread should get stopped in a READY->NULL transition
   if( G_UNLIKELY(self->bmemcollectionrunning) )
      gst_buffers_per_second_memutil_stop(self);

   g_hash_table_destroy(self->objid_hash);
   if( self->statsfile )
      g_free (self->statsfile);

   if( self->latency_track_from )
      g_free (self->latency_track_from);

   G_OBJECT_CLASS (gst_buffers_per_second_parent_class)->finalize (gobject);
}

/* initialize the bufferspersecond's class */
static void
gst_buffers_per_second_class_init (GstBuffersPerSecondClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->finalize = gst_buffers_per_second_finalize;

  gstelement_class = (GstElementClass *) klass;

  gstelement_class->change_state = gst_buffers_per_second_change_state;

  gobject_class->set_property = gst_buffers_per_second_set_property;
  gobject_class->get_property = gst_buffers_per_second_get_property;

  g_object_class_install_property (gobject_class, PROP_BPS_UPDATE_INTERVAL,
      g_param_spec_int ("bps-update-interval", "bps update interval",
          "Time between consecutive buffers per second measures and update "
          " (in ms). Should be set on NULL state", 1, G_MAXINT,
          DEFAULT_BPS_UPDATE_INTERVAL_MS,
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_TRACK_CPU_UTIL,
      g_param_spec_boolean ("track-cpu-util", "track cpu util",
          "Enable tracking/reporting of CPU Utilization during PLAYING state", FALSE,
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_TRACK_MEM_UTIL,
      g_param_spec_boolean ("track-mem-util", "track mem util",
          "Enable tracking/reporting of Memory Utilization", FALSE,
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_LATENCY_IS_SOURCE,
      g_param_spec_boolean ("latency-source", "latency source",
          "Enable this element as the starting point / source for latency calculation", FALSE,
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_LATENCY_TRACK_FROM,
      g_param_spec_string ("latency-track-from",
                           "LatencyTrackFrom",
                           "Set to name of bps element to measure/track latency from",
                           NULL,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_DUMP_FRAME_STATS,
      g_param_spec_string ("dump-frame-stats",
                           "DumpFrameStats",
                           "File location to save per-frame stats to (CSV format). You can also "
                           "set this to \"stdout\" , \"stderr\", or \"GST_INFO\" instead of a "
                           "filename",
                           NULL,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_TRACK_ROI_PER_FRAME,
      g_param_spec_boolean ("track-roi",
                           "track roi",
                           "Capture # of GstVideoRegionOfInterest attached to each GstBuffer",
                           FALSE,
                           G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_TRACK_NEW_OBJS_PER_FRAME,
      g_param_spec_boolean ("track-new-objs",
                           "track new objs",
                           "Capture # of new objects, per frame",
                           FALSE,
                           G_PARAM_READWRITE));

  gst_element_class_set_details_simple(gstelement_class,
    "Buffers Per Second",
    "Debug",
    "Prints buffers-per-second, as well as Mbps information",
    "Ryan Metcalfe <<Ryan.D.Metcalfe@intel.com>>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_buffers_per_second_init (GstBuffersPerSecond * self)
{
  self->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_event_function (self->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_buffers_per_second_sink_event));
  gst_pad_set_chain_function (self->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_buffers_per_second_chain));
  GST_PAD_SET_PROXY_CAPS (self->sinkpad);
  GST_PAD_SET_PROXY_ALLOCATION(self->sinkpad);
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  GST_PAD_SET_PROXY_CAPS (self->srcpad);
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  self->bps_update_interval = GST_MSECOND * DEFAULT_BPS_UPDATE_INTERVAL_MS;
  self->buffers_received = 0;
  self->last_buffers_received = G_GUINT64_CONSTANT (0);
  self->max_bps = -1;
  self->min_bps = -1;
  self->last_ts = self->start_ts = self->interval_ts = GST_CLOCK_TIME_NONE;

  self->btrackcpuutil = FALSE;
  self->bcpucollectionrunning = FALSE;
  self->stat_user = 0;
  self->stat_nice = 0;
  self->stat_kernel = 0;
  self->stat_idle = 0;
  self->stat_iowait = 0;
  self->stat_irq = 0;
  self->stat_softirq = 0;

  self->btrackmemutil = FALSE;
  self->bmemcollectionrunning = FALSE;
  self->memtracker_thread = NULL;
  g_mutex_init (&self->memmutex);
  g_cond_init(&self->memcond);
  self->maxPhysMemUsed = 0;

  self->blatency_is_source = FALSE;
  self->latency_track_from = NULL;
  self->bps_latency_track_source = NULL;

  g_mutex_init (&self->statscollectmutex);
  self->clockid_list = NULL;
  self->wallclockts_list = NULL;
  self->btracknrois = FALSE;
  self->perframe_n_roi_list = NULL;
  self->perframe_new_objs = NULL;
  self->perframe_untagged_objs = NULL;
  self->objid_hash = g_hash_table_new (g_direct_hash, g_direct_equal);
  self->btracknewobjs = FALSE;
  self->bclock_wallclock_list_reversed = FALSE;

  self->statsfile = NULL;

}

static void
gst_buffers_per_second_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBuffersPerSecond *self = GST_BUFFERSPERSECOND (object);

  //we only allow bps properties to be set while in NULL state.
  GstState state;
  GST_OBJECT_LOCK (self);
  state = GST_STATE (self);
  GST_OBJECT_UNLOCK (self);
  if( state != GST_STATE_NULL )
  {
     GST_WARNING_OBJECT(self, "Property '%s' can only be set in NULL state",
                        g_param_spec_get_name(pspec));
     return;
  }

  switch (prop_id)
  {
     case PROP_BPS_UPDATE_INTERVAL:
        self->bps_update_interval =
             GST_MSECOND * (GstClockTime) g_value_get_int (value);
     break;
     case PROP_TRACK_CPU_UTIL:
        self->btrackcpuutil = g_value_get_boolean(value);
     break;
     case PROP_TRACK_MEM_UTIL:
        self->btrackmemutil = g_value_get_boolean(value);
        if( self->btrackmemutil )
           self->baselinePhysMemUsed = gst_buffers_per_second_phys_mem_used_now();
     break;
     case PROP_LATENCY_IS_SOURCE:
        self->blatency_is_source = g_value_get_boolean(value);
     break;
     case PROP_LATENCY_TRACK_FROM:
        if (!g_value_get_string (value)) {
           g_warning ("latency-track-from property cannot be NULL");
           break;
        }
        if( self->latency_track_from )
           g_free (self->latency_track_from);
        self->latency_track_from = g_strdup (g_value_get_string (value));
     break;
     case PROP_DUMP_FRAME_STATS:
        if (!g_value_get_string (value)) {
           g_warning ("dump-frame-stats cannot be NULL");
           break;
        }
        if( self->statsfile )
           g_free (self->statsfile);
        self->statsfile = g_strdup (g_value_get_string (value));
     break;
     case PROP_TRACK_ROI_PER_FRAME:
        self->btracknrois = g_value_get_boolean(value);
     break;
     case PROP_TRACK_NEW_OBJS_PER_FRAME:
        self->btracknewobjs = g_value_get_boolean(value);
     break;
     default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
     break;
  }
}

static void
gst_buffers_per_second_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBuffersPerSecond *self = GST_BUFFERSPERSECOND (object);

  switch (prop_id) {
    case PROP_BPS_UPDATE_INTERVAL:
      g_value_set_int (value, (gint) (self->bps_update_interval / GST_MSECOND));
      break;
    case PROP_TRACK_CPU_UTIL:
      g_value_set_boolean (value, self->btrackcpuutil);
      break;
    case PROP_TRACK_MEM_UTIL:
      g_value_set_boolean (value, self->btrackmemutil);
      break;
    case PROP_LATENCY_IS_SOURCE:
      g_value_set_boolean (value, self->blatency_is_source);
      break;
    case PROP_LATENCY_TRACK_FROM:
       g_value_set_string (value, self->latency_track_from);
      break;
    case PROP_DUMP_FRAME_STATS:
       g_value_set_string (value, self->statsfile);
     break;
    case PROP_TRACK_ROI_PER_FRAME:
       g_value_set_boolean (value, self->btracknrois);
     break;
    case PROP_TRACK_NEW_OBJS_PER_FRAME:
       g_value_set_boolean (value, self->btracknewobjs);
     break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void display_eos_summary(GstBuffersPerSecond *self)
{
   if( !GST_CLOCK_TIME_IS_VALID (self->start_ts) )
      return;

   GstClockTime current_ts = gst_util_get_timestamp ();
   guint64 buffers_received = g_atomic_int_get (&self->buffers_received);
   gdouble time_elapsed = (gdouble) (current_ts - self->start_ts) / GST_SECOND;

   if( time_elapsed <= 0 )
      return;

   gdouble average_bps = (gdouble) buffers_received / time_elapsed;
   gsize bytes_received = self->bytes_received;
   gdouble average_throughput = (gdouble) bytes_received / time_elapsed;

   GST_INFO_OBJECT(self, "EOS SUMMARY: buffers_received: %" G_GUINT64_FORMAT
              " average bps: %.2f"
              " average Mbps: %.2f",
              buffers_received,
              average_bps,
              (average_throughput*8)/1000000.0);
}

/* GstElement vmethod implementations */
static gboolean
display_current_bps (gpointer data)
{
  GstBuffersPerSecond *self = GST_BUFFERSPERSECOND (data);
  gdouble rr, average_bps;
  gdouble rb, average_throughput;
  gdouble time_diff, time_elapsed;
  GstClockTime current_ts = gst_util_get_timestamp ();
  guint64 buffers_received;
  gsize bytes_received;

  buffers_received = g_atomic_int_get (&self->buffers_received);

  if (buffers_received == 0) {
    return TRUE;
  }

  if(buffers_received == self->last_buffers_received )
  {
     return TRUE;
  }

  bytes_received = self->bytes_received;

  time_diff = (gdouble) (current_ts - self->last_ts) / GST_SECOND;
  time_elapsed = (gdouble) (current_ts - self->start_ts) / GST_SECOND;

  rr = (gdouble) (buffers_received - self->last_buffers_received) / time_diff;
  average_bps = (gdouble) buffers_received / time_elapsed;

  if (self->max_bps == -1 || rr > self->max_bps) {
    self->max_bps = rr;
  }
  if (self->min_bps == -1 || rr < self->min_bps) {
    self->min_bps = rr;
  }

  rb = (gdouble)(bytes_received - self->last_bytes_received) / time_diff;
  average_throughput = (gdouble) bytes_received / time_elapsed;

   GST_INFO_OBJECT(self, "buffers_received: %" G_GUINT64_FORMAT
           ", current bps: %.2f, average bps: %.2f"
           ", current Mbps: %.2f, average Mbps: %.2f",
           buffers_received,
           rr,
           average_bps,
           (rb*8)/1000000.0,
           (average_throughput*8)/1000000.0);

  self->last_buffers_received = buffers_received;
  self->last_bytes_received = bytes_received;
  self->last_ts = current_ts;

  return TRUE;
}

static inline void clear_ts_lists(GstBuffersPerSecond *bps)
{
    g_mutex_lock(&bps->statscollectmutex);
    bps->bclock_wallclock_list_reversed = FALSE;
    if( bps->clockid_list )
    {
       g_slist_free(bps->clockid_list);
       bps->clockid_list = NULL;
    }
    if( bps->wallclockts_list )
    {
       g_slist_free(bps->wallclockts_list);
       bps->wallclockts_list = NULL;
    }
    if( bps->perframe_n_roi_list )
    {
       g_slist_free(bps->perframe_n_roi_list);
       bps->perframe_n_roi_list = NULL;
    }
    if( bps->perframe_new_objs )
    {
       g_slist_free(bps->perframe_new_objs);
       bps->perframe_new_objs = NULL;
    }
    if( bps->perframe_untagged_objs )
    {
       g_slist_free(bps->perframe_untagged_objs);
       bps->perframe_untagged_objs = NULL;
    }
    g_hash_table_remove_all(bps->objid_hash);
    g_mutex_unlock(&bps->statscollectmutex);
}

/* this function handles sink events */
static gboolean
gst_buffers_per_second_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstBuffersPerSecond *bps = GST_BUFFERSPERSECOND (parent);

  GST_LOG_OBJECT (bps, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch(GST_EVENT_TYPE(event))
  {
     case GST_EVENT_STREAM_START:
       //reset / initialize stuff at the start of a stream
       bps->last_ts = bps->start_ts = bps->interval_ts = GST_CLOCK_TIME_NONE;
       bps->buffers_received = 0;
       bps->last_buffers_received = G_GUINT64_CONSTANT (0);
       bps->bytes_received =  0;
       bps->last_bytes_received =  0;
       bps->max_bps = -1;
       bps->min_bps = -1;

       clear_ts_lists(bps);

     break;

     case GST_EVENT_EOS:
       //display current on EOS, unless we are a latency source
      if( !bps->blatency_is_source )
         display_eos_summary(bps);
     break;

     default:
     break;
  }

  return gst_pad_event_default (pad, parent, event);
}


/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_buffers_per_second_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstBuffersPerSecond *self = GST_BUFFERSPERSECOND (parent);

  GstClockTime ts = gst_util_get_timestamp ();

  //if we are acting as a latency source, it most likely means that there is
  // another bps later in the pipeline... so skip tracking BPS, MBPS by default.
  if( !self->blatency_is_source )
  {
     g_atomic_int_inc (&self->buffers_received);
     self->bytes_received += gst_buffer_get_size(buf);

     if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (self->start_ts)))
     {
        self->interval_ts = self->last_ts = self->start_ts = ts;
     }
     if (GST_CLOCK_DIFF (self->interval_ts, ts) > self->bps_update_interval)
     {
        display_current_bps (self);
        self->interval_ts = ts;
     }
  }

  GstClockTime clock_id = GST_BUFFER_DTS_OR_PTS(buf);
  g_mutex_lock(&self->statscollectmutex);

  if( self->statsfile || self->blatency_is_source )
  {
     self->clockid_list = g_slist_prepend(self->clockid_list, (gpointer)clock_id);
     self->wallclockts_list = g_slist_prepend(self->wallclockts_list, (gpointer)ts);
  }

  if( self->btracknrois )
  {
     guint nmeta = gst_buffer_get_n_meta(buf, gst_video_region_of_interest_meta_api_get_type());
     self->perframe_n_roi_list =  g_slist_prepend(self->perframe_n_roi_list,
                                                  GUINT_TO_POINTER(nmeta));

     GST_DEBUG_OBJECT(self, "%"GST_TIME_FORMAT": n rois = %u", GST_TIME_ARGS(clock_id), nmeta);
  }

  if( self->btracknewobjs )
  {
     guint new_objs = 0;
     guint unknown_objs = 0;
     GstVideoRegionOfInterestMeta * meta = NULL;
     gpointer state = NULL;
     guint metai = 0;
     while( (meta = (GstVideoRegionOfInterestMeta *)gst_buffer_iterate_meta_filtered(
                                  buf,
                                  &state,
                                  gst_video_region_of_interest_meta_api_get_type())) )
    {
       GstStructure *object_id_struct =
             gst_video_region_of_interest_meta_get_param(meta, "object_id");
       if( object_id_struct )
       {
          int id = 0;
          gst_structure_get_int(object_id_struct, "id", &id);
          if( id )
          {
             //g_hash_table_add returns TRUE if the hash table doesn't
             // already contain this key.
             if( g_hash_table_add(self->objid_hash,
                                  GINT_TO_POINTER(id)))
             {
                new_objs++;
             }
          }
          else
          {
             unknown_objs++;
          }
       }
       else
       {
          unknown_objs++;
       }

       if( meta->roi_type )
          GST_DEBUG_OBJECT(self, "%"GST_TIME_FORMAT": roi %u, roi_type=%s", GST_TIME_ARGS(clock_id), metai, g_quark_to_string(meta->roi_type));
       else
          GST_DEBUG_OBJECT(self, "%"GST_TIME_FORMAT": roi %u, roi_type=(not set)", GST_TIME_ARGS(clock_id), metai);

       metai++;
    }

     self->perframe_new_objs =  g_slist_prepend(self->perframe_new_objs,
                                                  GUINT_TO_POINTER(new_objs));
     self->perframe_untagged_objs =  g_slist_prepend(self->perframe_untagged_objs,
                                                  GUINT_TO_POINTER(unknown_objs));
  }
  g_mutex_unlock(&self->statscollectmutex);

  /* just push out the incoming buffer without touching it */
  return gst_pad_push (self->srcpad, buf);
}

static gboolean gst_buffers_per_second_cpuutil_start(GstBuffersPerSecond *self)
{
   if( self->bcpucollectionrunning ) return FALSE;

   gboolean ret = TRUE;
   FILE *fstat = fopen("/proc/stat", "r");
   if( fstat )
   {
      if( fscanf(fstat, "cpu %llu %llu %llu %llu %llu %llu %llu",
                &self->stat_user, &self->stat_nice,
                &self->stat_kernel, &self->stat_idle,
                &self->stat_iowait, &self->stat_irq,
                &self->stat_softirq) != 7 )
      {
         GST_WARNING_OBJECT(self, "fscanf didn't match correct number of args");
         ret = FALSE;
      }

      fclose(fstat);
   }
   else
   {
      GST_WARNING_OBJECT(self, "Error opening /proc/stat for reading.");
      ret = FALSE;
   }

   self->bcpucollectionrunning = ret;
   return ret;
}

static gboolean gst_buffers_per_second_cpuutil_stop(GstBuffersPerSecond *self)
{
   if( !self->bcpucollectionrunning ) return FALSE;
   self->bcpucollectionrunning = FALSE;

   gboolean ret = TRUE;
   unsigned long long end_user, end_nice, end_kernel, end_idle,
                         end_iowait, end_irq, end_softirq;

   FILE *fstat = fopen("/proc/stat", "r");
   if( fstat )
   {
      if( fscanf(fstat, "cpu %llu %llu %llu %llu %llu %llu %llu",
                &end_user, &end_nice,
                &end_kernel, &end_idle,
                &end_iowait, &end_irq,
                &end_softirq) == 7 )
      {

         if( (end_user >= self->stat_user) &&
             (end_nice >= self->stat_nice) &&
             (end_kernel >= self->stat_kernel) &&
             (end_idle >= self->stat_idle) &&
             (end_iowait >= self->stat_iowait) &&
             (end_irq >= self->stat_irq) &&
             (end_softirq >= self->stat_softirq) )
         {
            unsigned long long diff_user = end_user - self->stat_user;
            unsigned long long diff_nice = end_nice - self->stat_nice;
            unsigned long long diff_kernel = end_kernel - self->stat_kernel;
            unsigned long long diff_idle  = end_idle - self->stat_idle;
            unsigned long long diff_iowait = end_iowait - self->stat_iowait;
            unsigned long long diff_irq = end_irq - self->stat_irq;
            unsigned long long diff_softirq = end_softirq - self->stat_softirq;
            unsigned long long total_work = diff_user + diff_nice + diff_kernel +
                                            diff_iowait + diff_irq +
                                            diff_softirq;

            unsigned long long total_time = total_work + diff_idle;
            if( total_time > 0 )
            {
               GST_INFO_OBJECT(self, "   OVERALL: %.2f", ((gdouble)total_work / (gdouble)total_time)*100);
               GST_INFO_OBJECT(self, "      USERSPACE: %.2f", ((gdouble)(diff_user+diff_nice) / (gdouble)total_time)*100);
               GST_INFO_OBJECT(self, "      KERNELSPACE: %.2f", ((gdouble)(diff_kernel) / (gdouble)total_time)*100);
               GST_INFO_OBJECT(self, "      IOWAIT: %.2f", ((gdouble)(diff_iowait) / (gdouble)total_time)*100);
               GST_INFO_OBJECT(self, "      INTERRUPTS(IRQ): %.2f", ((gdouble)(diff_irq) / (gdouble)total_time)*100);
               GST_INFO_OBJECT(self, "      SOFTIRQ: %.2f", ((gdouble)(diff_softirq) / (gdouble)total_time)*100);
            }
         }
         else
         {
            GST_WARNING_OBJECT(self, "At least one value read from /proc/stat is invalid");
            ret = FALSE;
         }
      }
      else
      {
         GST_WARNING_OBJECT(self, "fscanf didn't match correct number of args");
         ret = FALSE;
      }

      fclose(fstat);
   }
   else
   {
      GST_WARNING_OBJECT(self, "Error opening /proc/stat for reading.");
      ret = FALSE;
   }

   return ret;
}

static gpointer BPSMemTrackerThread(gpointer data)
{
   GstBuffersPerSecond *self = (GstBuffersPerSecond *)data;

   g_mutex_lock(&self->memmutex);
   long long sumPhysMemUsed = 0;
   long long ncollections = 0;
   self->avgPhysMemUsed = -1.;
   while(self->bmemcollectionrunning)
   {
      gint64 end_time = g_get_monotonic_time () + 500*G_TIME_SPAN_MILLISECOND;
      g_cond_wait_until(&self->memcond,
                        &self->memmutex,
                        end_time);

      if( self->bmemcollectionrunning )
      {
         long long physMemUsed = gst_buffers_per_second_phys_mem_used_now();

         if( physMemUsed > self->maxPhysMemUsed )
            self->maxPhysMemUsed = physMemUsed;

         long long physMemUsedDelta = physMemUsed - self->baselinePhysMemUsed;

         GST_DEBUG_OBJECT(self, "memory footprint right now: %.2f MiB",
                          (double)(physMemUsedDelta)/(1024.*1024.));

         sumPhysMemUsed += physMemUsedDelta;
         ncollections++;
      }
   }

   if(ncollections > 0)
   {
      self->avgPhysMemUsed = (gdouble)(sumPhysMemUsed)/(gdouble)(ncollections);
   }

   g_mutex_unlock(&self->memmutex);

   return NULL;
}

static gboolean gst_buffers_per_second_memutil_start(GstBuffersPerSecond *self)
{
   if( self->bmemcollectionrunning ) return FALSE;

   self->maxPhysMemUsed = gst_buffers_per_second_phys_mem_used_now() - self->baselinePhysMemUsed;
   self->bmemcollectionrunning = TRUE;

   //start the tracker thread
   self->memtracker_thread =
         g_thread_new( "BPSMemTracker",
                       (GThreadFunc)BPSMemTrackerThread,
                       self);

   if( !self->memtracker_thread )
   {
      self->bmemcollectionrunning = FALSE;
      return FALSE;
   }

   return TRUE;
}

static gboolean gst_buffers_per_second_memutil_stop(GstBuffersPerSecond *self)
{
   if( !self->bmemcollectionrunning ) return FALSE;

   //stop the collection thread
   g_mutex_lock(&self->memmutex);
   self->bmemcollectionrunning = FALSE;
   g_cond_broadcast (&self->memcond);
   g_mutex_unlock(&self->memmutex);

   if(self->memtracker_thread)
   {
      g_thread_join (self->memtracker_thread);
      self->memtracker_thread = NULL;
   }

   GST_INFO_OBJECT(self, "max_memory_utilization = %.2f MiB, avg_memory_utilization = %.2f MiB",
                   (double)(self->maxPhysMemUsed - self->baselinePhysMemUsed)/(1024.*1024.),
                   self->avgPhysMemUsed/(1024.*1024.));

   return TRUE;
}

static GstObject *GetParentPipeline(GstElement * element)
{
   GstObject *parent = gst_element_get_parent(element);
   if( parent )
   {
      if( !GST_IS_PIPELINE(parent) )
      {
         GstObject *parent_of_parent = GetParentPipeline((GstElement *)parent);
         gst_object_unref(parent);
         parent = parent_of_parent;
      }
   }

   return parent;
}

static void dump_stats_file(GstBuffersPerSecond *self)
{
   g_mutex_lock(&self->statscollectmutex);
   FILE *outfile = NULL;
   if( !g_strcmp0(self->statsfile, "stderr") )
   {
      outfile = stderr;
   }
   else
   if( !g_strcmp0(self->statsfile, "stdout") )
   {
      outfile = stdout;
   }
   else
   {
      outfile = fopen(self->statsfile, "w");
      if( !outfile )
      {
         g_mutex_unlock(&self->statscollectmutex);
         GST_ERROR_OBJECT(self, "Unable to open file %s for writing.",
                                 self->statsfile);
         return;
      }
   }

   gint nentries_per_row = 3;
   fprintf(outfile, "BUF_INDEX, BUFFER_TIMESTAMP, ABS_TIMESTAMP, ");
   if( self->bps_latency_track_source )
   {
      fprintf(outfile, "LATENCY, ");
      nentries_per_row++;
   }

   if( self->btracknrois )
   {
      fprintf(outfile, "N_ROI, ");
      nentries_per_row++;
   }

   if( self->btracknewobjs )
   {
      fprintf(outfile, "NEW_OBJS, UNTAGGED_OBJS");
      nentries_per_row+=2;
   }
   fprintf(outfile, "\n");

   if( !self->bclock_wallclock_list_reversed )
   {
      self->clockid_list = g_slist_reverse(self->clockid_list);
      self->wallclockts_list = g_slist_reverse(self->wallclockts_list);
      self->bclock_wallclock_list_reversed = TRUE;
   }
   self->perframe_n_roi_list = g_slist_reverse(self->perframe_n_roi_list);
   self->perframe_new_objs = g_slist_reverse(self->perframe_new_objs);
   self->perframe_untagged_objs = g_slist_reverse(self->perframe_untagged_objs);

   GstBuffersPerSecond *source_bps = NULL;
   if( self->bps_latency_track_source )
   {
      source_bps =
        GST_BUFFERSPERSECOND (self->bps_latency_track_source);
      g_mutex_lock(&source_bps->statscollectmutex);
      if( !source_bps->bclock_wallclock_list_reversed )
      {
         source_bps->clockid_list = g_slist_reverse(source_bps->clockid_list);
         source_bps->wallclockts_list = g_slist_reverse(source_bps->wallclockts_list);
         source_bps->bclock_wallclock_list_reversed = TRUE;
      }
   }
   else
   {
      source_bps = self;
   }

   GSList *source_clockid_list = source_bps->clockid_list;
   GSList *source_wallclockts_list = source_bps->wallclockts_list;
   GSList *this_clockid_list = self->clockid_list;
   GSList *wallclockts_list = self->wallclockts_list;
   GSList *this_nroi_list = self->perframe_n_roi_list;
   GSList *this_newobj_list = self->perframe_new_objs;
   GSList *untagged_obj_list = self->perframe_untagged_objs;

   guint buf_index = 0;
   while( (source_clockid_list != NULL) && (source_wallclockts_list != NULL ) )
   {
      GstClockTime source_clock_id = (GstClockTime)source_clockid_list->data;
      GstClockTime source_ts = (GstClockTime)source_wallclockts_list->data;
      GstClockTime this_clock_id = GST_CLOCK_TIME_NONE;
      GstClockTime end_ts = GST_CLOCK_TIME_NONE;

      if( this_clockid_list )
         this_clock_id = (GstClockTime)this_clockid_list->data;

      if( wallclockts_list )
         end_ts = (GstClockTime)wallclockts_list->data;

      //always print buf index & pts of buffer
      fprintf(outfile, "%u, %"GST_TIME_FORMAT", ",
                              buf_index,
                              GST_TIME_ARGS(source_clock_id));

      if( this_clock_id == source_clock_id )
      {
         //print absolute time
         fprintf(outfile, "%"GST_TIME_FORMAT", ", GST_TIME_ARGS(end_ts - self->start_ts));

         if( self->bps_latency_track_source )
         {
           gdouble time_diff = (gdouble) (end_ts - source_ts) / GST_MSECOND;
           fprintf(outfile, "%.2f, ", time_diff);
         }

         if( self->btracknrois )
         {
            if( this_nroi_list  )
            {
               fprintf(outfile, "%u, ", GPOINTER_TO_UINT(this_nroi_list->data));
               this_nroi_list = this_nroi_list->next;
            }
            else
            {
               //this shouldn't ever happen..
               fprintf(outfile, "?, ");
            }
         }

         if( self->btracknewobjs )
         {
            if( this_newobj_list  )
            {
               fprintf(outfile, "%u, ", GPOINTER_TO_UINT(this_newobj_list->data));
               this_newobj_list = this_newobj_list->next;
            }
            else
            {
               //this shouldn't ever happen..
               fprintf(outfile, "?, ");
            }

            if( untagged_obj_list )
            {
               fprintf(outfile, "%u, ", GPOINTER_TO_UINT(untagged_obj_list->data));
               untagged_obj_list = untagged_obj_list->next;
            }
            else
            {
               //this shouldn't ever happen..
               fprintf(outfile, "?, ");
            }
         }

         if( this_clockid_list )
           this_clockid_list = this_clockid_list->next;

         if( wallclockts_list )
           wallclockts_list = wallclockts_list->next;
      }
      else
      {
         for( int i = 0; i < (nentries_per_row-2); i++ )
         {
            fprintf(outfile, "dropped, ");
         }
      }
      fprintf(outfile, "\n");

      source_clockid_list = source_clockid_list->next;
      source_wallclockts_list = source_wallclockts_list->next;
      buf_index++;
   }

   if( self->bps_latency_track_source )
   {
      g_mutex_unlock(&source_bps->statscollectmutex);
   }

   if( outfile &&
      (outfile != stdout) &&
      (outfile != stderr))
   {
      fflush(outfile);
      fclose(outfile);
   }

   g_mutex_unlock(&self->statscollectmutex);
}

static GstStateChangeReturn gst_buffers_per_second_change_state (GstElement * element,
    GstStateChange transition)
{
   GstBuffersPerSecond *self = GST_BUFFERSPERSECOND (element);

    switch (transition)
    {
       case GST_STATE_CHANGE_NULL_TO_READY:
       /* Init counters */
       self->buffers_received = 0;
       self->last_buffers_received = G_GUINT64_CONSTANT (0);
       self->bytes_received =  0;
       self->last_bytes_received =  0;
       self->max_bps = -1;
       self->min_bps = -1;

       /* init time stamps */
       self->last_ts = self->start_ts = self->interval_ts = GST_CLOCK_TIME_NONE;

       if( self->btrackmemutil )
          gst_buffers_per_second_memutil_start(self);

       if( self->latency_track_from )
       {
          GstObject *pipeline = GetParentPipeline(element);
          if( GST_IS_PIPELINE(pipeline) )
          {
             GstElement *bps_latency_source =
                   gst_bin_get_by_name(GST_BIN(pipeline), self->latency_track_from);

             if( bps_latency_source )
             {
                if( GST_IS_BUFFERSPERSECOND(bps_latency_source) )
                {
                   self->bps_latency_track_source = gst_object_ref(bps_latency_source);
                }
                else
                {
                   GST_ERROR_OBJECT(self,
                  "Element named %s is not a bps element. Disabling latency tracking",
                   self->latency_track_from);
                }
             }
             else
             {
                GST_ERROR_OBJECT(self,
                  "Unable to find element named %s in pipeline. Disabling latency tracking",
                   self->latency_track_from);
             }
          }
          else
          {
             GST_ERROR_OBJECT(self,
                "Unable to retrieve bounding top-level pipeline. Disabling latency tracking");
          }

          if( pipeline )
             gst_object_unref(pipeline);
       }
       break;

       case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
       if( self->btrackcpuutil )
          if( !gst_buffers_per_second_cpuutil_start(self) )
          {
             GST_WARNING_OBJECT (self, "gst_buffers_per_second_cpuutil_start failed");
          }
       break;

       case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
       if( self->btrackcpuutil )
          if( !gst_buffers_per_second_cpuutil_stop(self) )
          {
             GST_WARNING_OBJECT (self, "gst_buffers_per_second_cpuutil_stop failed");
          }
       break;

       case GST_STATE_CHANGE_PAUSED_TO_READY:
       {
          if( self->statsfile )
          {
             dump_stats_file(self);
          }
       }
       break;

       case GST_STATE_CHANGE_READY_TO_NULL:

       clear_ts_lists(self);

       if( self->btrackmemutil )
          gst_buffers_per_second_memutil_stop(self);

       if( self->bps_latency_track_source )
       {
          gst_object_unref(self->bps_latency_track_source);
          self->bps_latency_track_source = 0;
       }
       break;

       default:
       break;
    }

    return GST_ELEMENT_CLASS (gst_buffers_per_second_parent_class)->change_state (element, transition);
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
bufferspersecond_init (GstPlugin * bufferspersecond)
{
  GST_DEBUG_CATEGORY_INIT (gst_buffers_per_second_debug, "bps",
      0, "buffers per second element");

  return gst_element_register (bufferspersecond, "bps", GST_RANK_NONE,
      GST_TYPE_BUFFERSPERSECOND);
}



/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstbufferspersecond"
#endif

/* Version number of package */
#define VERSION "1.0.0"

/* gstreamer looks for this structure to register buffersperseconds
 *
 * exchange the string 'Template bufferspersecond' with your bufferspersecond description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    bufferspersecond,
    "Template bps",
    bufferspersecond_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)

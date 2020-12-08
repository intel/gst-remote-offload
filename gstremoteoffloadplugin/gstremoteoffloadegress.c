/*
 *  remoteoffloadegress.c - GstRemoteOffloadEgress element
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
#  include "config.h"
#endif
#include <string.h>
#include "gstremoteoffloadegress.h"
#include "remoteoffloadcommschannel.h"
#include "querydataexchanger.h"
#include "bufferdataexchanger.h"
#include "statechangedataexchanger.h"
#include "eventdataexchanger.h"
#include "queuestatsdataexchanger.h"
#include "genericdataexchanger.h"
#include "gstingressegressdefs.h"

#define REMOTEOFFLOADEGRESS_IMPLICIT_QUEUE 1

enum
{
  PROP_COMMSCHANNEL = 1,
  PROP_COLLECTQUEUESTATS,
  N_PROPERTIES,
};

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_remoteoffload_egress_debug);
#define GST_CAT_DEFAULT gst_remoteoffload_egress_debug

G_DEFINE_TYPE_WITH_CODE (GstRemoteOffloadEgress, gst_remoteoffload_egress, GST_TYPE_ELEMENT,
  GST_DEBUG_CATEGORY_INIT (gst_remoteoffload_egress_debug, "remoteoffloadegress", 0,
  "debug category for remoteoffloadegress"));


struct _GstRemoteOffloadEgressPrivate
{
   RemoteOffloadCommsChannel *channel;

   GstPad *srcpad;

   GQueue *topush_queue;
   GMutex queueprotectmutex;
   GCond queuecond;
   gboolean is_flushing;
   GstFlowReturn last_buffer_push_ret;

   BufferDataExchangerCallback bufferCallback;
   BufferDataExchanger *pBufferExchanger;

   QueryDataExchangerCallback queryCallback;
   QueryDataExchanger *pQueryExchanger;

   StateChangeDataExchanger *pStateChangeExchanger;

   EventDataExchangerCallback eventCallback;
   EventDataExchanger *pEventExchanger;

   GenericDataExchangerCallback genericDataExchangerCallback;
   GenericDataExchanger *pGenericDataExchanger;

   gboolean was_last_qos_bad;

   QueueStatsDataExchangerCallback queueStatsCallback;
   QueueStatsDataExchanger *pQueueStatsExchanger;
   GArray *queue_stats;
   gboolean collectqueuestats;

   GMutex caps_query_mutex;

#if REMOTEOFFLOADEGRESS_IMPLICIT_QUEUE
   GstElement *queue;
#endif

};

static void gst_remoteoffload_egress_finalize (GObject * object);

static void gst_remoteoffload_egress_set_property (GObject * object,
                                                   guint prop_id,
                                                   const GValue * value,
                                                   GParamSpec * pspec);

static void gst_remoteoffload_egress_get_property (GObject * object,
                                                   guint prop_id,
                                                   GValue * value,
                                                   GParamSpec * pspec);

static gboolean gst_remoteoffload_egress_activate_mode (GstPad * pad,
                                                        GstObject * parent,
                                                        GstPadMode mode,
                                                        gboolean active);

static gboolean gst_remoteoffload_egress_srcpad_event (GstPad * pad,
                                                       GstObject * parent,
                                                       GstEvent * event);

static gboolean gst_remoteoffload_egress_srcpad_query (GstPad * pad,
                                                       GstObject * parent,
                                                       GstQuery * query);

static gboolean gst_remoteoffload_egress_element_event (GstElement * element,
                                                        GstEvent * event);

static gboolean gst_remoteoffload_egress_element_query (GstElement * element,
                                                        GstQuery * query);

static GstStateChangeReturn gst_remoteoffload_egress_change_state (GstElement *element,
                                                                   GstStateChange transition);

//exchanger callbacks
static void BufferReceivedCallback(GstBuffer *buffer, void *priv);
static void QueryReceivedCallback(GstQuery *query, void *priv);
static void EventReceivedCallback(GstEvent *event, void *priv);
static gboolean GenericCallback(guint32 transfer_type, GArray *memblocks, void *priv);
static GArray *RequestQueueStats(void *priv);

static void
gst_remoteoffload_egress_class_init (GstRemoteOffloadEgressClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);


  gobject_class->finalize = gst_remoteoffload_egress_finalize;

  gobject_class->set_property = gst_remoteoffload_egress_set_property;
  gobject_class->get_property = gst_remoteoffload_egress_get_property;

  gstelement_class->send_event =
      GST_DEBUG_FUNCPTR (gst_remoteoffload_egress_element_event);
  gstelement_class->query = GST_DEBUG_FUNCPTR (gst_remoteoffload_egress_element_query);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_remoteoffload_egress_change_state);


  GST_DEBUG_REGISTER_FUNCPTR (gst_remoteoffload_egress_activate_mode);
  GST_DEBUG_REGISTER_FUNCPTR (gst_remoteoffload_egress_srcpad_event);
  GST_DEBUG_REGISTER_FUNCPTR (gst_remoteoffload_egress_srcpad_query);

  g_object_class_install_property (gobject_class, PROP_COMMSCHANNEL,
      g_param_spec_pointer ("commschannel", "CommsChannel",
          "CommsChannel object in use",
           G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_COLLECTQUEUESTATS,
      g_param_spec_boolean ("collectqueuestats", "CollectQueueStats",
          "Collect Queue Statistics",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
      "Remote Offload Egress",
      "Egress",
      "Routes data from a remote-connected Ingress ",
      "Ryan Metcalfe <ryan.d.metcalfe@intel.com>");
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
}

static void
gst_remoteoffload_egress_init (GstRemoteOffloadEgress * self)
{
  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_SOURCE);

  self->priv = g_malloc(sizeof(GstRemoteOffloadEgressPrivate));

  self->priv->channel = NULL;

  self->priv->topush_queue = g_queue_new();
  g_mutex_init(&self->priv->queueprotectmutex);
  g_cond_init(&self->priv->queuecond);

  self->priv->pBufferExchanger = NULL;
  self->priv->bufferCallback.buffer_received = BufferReceivedCallback;
  self->priv->bufferCallback.alloc_buffer_mem_block = NULL;
  self->priv->bufferCallback.priv = self;

  self->priv->pQueryExchanger = NULL;
  self->priv->queryCallback.query_received = QueryReceivedCallback;
  self->priv->queryCallback.priv = self;

  self->priv->pStateChangeExchanger = NULL;

  self->priv->pEventExchanger = NULL;
  self->priv->eventCallback.event_received = EventReceivedCallback;
  self->priv->eventCallback.priv = self;

  self->priv->genericDataExchangerCallback.received = GenericCallback;
  self->priv->genericDataExchangerCallback.priv = self;
  self->priv->pGenericDataExchanger = NULL;

  self->priv->collectqueuestats = FALSE;
  self->priv->queue_stats = g_array_new(FALSE, FALSE, sizeof(QueueStatistics));
  self->priv->pQueueStatsExchanger = NULL;
  self->priv->queueStatsCallback.request_received = RequestQueueStats;
  self->priv->queueStatsCallback.priv = self;

  g_mutex_init(&self->priv->caps_query_mutex);
  self->priv->was_last_qos_bad = FALSE;
  self->priv->is_flushing = TRUE;

  self->priv->srcpad = gst_pad_new_from_static_template (&srctemplate, "src");
  gst_pad_set_activatemode_function (self->priv->srcpad,
      gst_remoteoffload_egress_activate_mode);
  gst_pad_set_event_function (self->priv->srcpad, gst_remoteoffload_egress_srcpad_event);
  gst_pad_set_query_function (self->priv->srcpad, gst_remoteoffload_egress_srcpad_query);
  gst_element_add_pad (GST_ELEMENT (self), self->priv->srcpad);

}

static void
gst_remoteoffload_egress_finalize (GObject * object)
{
  GstRemoteOffloadEgress * self = GST_REMOTEOFFLOAD_EGRESS (object);
  g_cond_clear (&self->priv->queuecond);
  g_mutex_clear(&self->priv->queueprotectmutex);
  g_mutex_clear(&self->priv->caps_query_mutex);
  g_queue_free(self->priv->topush_queue);
  g_array_unref (self->priv->queue_stats);
  g_free(self->priv);
  G_OBJECT_CLASS (gst_remoteoffload_egress_parent_class)->finalize (object);
}

static void
gst_remoteoffload_egress_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRemoteOffloadEgress *self = GST_REMOTEOFFLOAD_EGRESS (object);

  switch (prop_id) {
    case PROP_COMMSCHANNEL:
    {
      RemoteOffloadCommsChannel *tmp = g_value_get_pointer (value);
      if( REMOTEOFFLOAD_IS_COMMSCHANNEL(tmp) )
        self->priv->channel = g_object_ref(tmp);
    }
    break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_remoteoffload_egress_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRemoteOffloadEgress *self = GST_REMOTEOFFLOAD_EGRESS (object);
  switch (prop_id)
  {
    case PROP_COLLECTQUEUESTATS:
      g_value_set_boolean (value, self->priv->collectqueuestats);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_remoteoffload_egress_flush_queue(GstRemoteOffloadEgress * egress)
{
   //flush all buffers, event, & queries from the queue.
   g_mutex_lock (&egress->priv->queueprotectmutex);
   while( !g_queue_is_empty(egress->priv->topush_queue) )
   {
      gpointer object = g_queue_pop_head(egress->priv->topush_queue);
      if (GST_IS_BUFFER (object))
      {
         GstBuffer *buf = GST_BUFFER (object);
         GST_DEBUG_OBJECT (egress,
                          "sending GST_FLOW_FLUSHING for buf=%p with pts=%"GST_TIME_FORMAT,
                           buf, GST_TIME_ARGS(GST_BUFFER_DTS_OR_PTS(buf)));
         buffer_data_exchanger_send_buffer_flowreturn(egress->priv->pBufferExchanger,
                                                   buf,
                                                   GST_FLOW_FLUSHING);
         gst_buffer_unref(buf);
      }
      else
      if(GST_IS_EVENT (object))
      {
         GstEvent *event = GST_EVENT (object);
         GST_DEBUG_OBJECT(egress, "sending FALSE for event=%s\n", GST_EVENT_TYPE_NAME(event));
         event_data_exchanger_send_event_result(egress->priv->pEventExchanger,
                                            event,
                                            FALSE);
         gst_event_unref (event);
      }
      else
      if(GST_IS_QUERY (object))
      {
          GstQuery *query = GST_QUERY (object);
          GST_DEBUG_OBJECT(egress, "sending FALSE for query=%s\n", GST_QUERY_TYPE_NAME(query));
          query_data_exchanger_send_query_result(egress->priv->pQueryExchanger,
                                            query,
                                            FALSE);
          gst_query_unref (query);
      }
      else
      {
          GST_WARNING_OBJECT (egress, "Unknown data type queued");
      }
   }
   g_mutex_unlock (&egress->priv->queueprotectmutex);
}

static void
gst_remoteoffload_egress_stream_thread_routine (GstRemoteOffloadEgress * egress)
{
   gpointer object;
   g_mutex_lock (&egress->priv->queueprotectmutex);
   while( g_queue_is_empty(egress->priv->topush_queue) && !egress->priv->is_flushing )
   {
      gint64 end_time = g_get_monotonic_time () + 30 * G_TIME_SPAN_SECOND;
      if( !g_cond_wait_until (&egress->priv->queuecond,
                              &egress->priv->queueprotectmutex,
                              end_time) )
      {
         GST_WARNING_OBJECT (egress, "push queue has been empty for 30 seconds...");
      }
   }

   if (egress->priv->is_flushing)
   {
      g_mutex_unlock (&egress->priv->queueprotectmutex);
      gst_pad_pause_task (egress->priv->srcpad);
      return;
   }

   object = g_queue_pop_head(egress->priv->topush_queue);
   g_mutex_unlock (&egress->priv->queueprotectmutex);

   if (GST_IS_BUFFER (object))
   {
      GstBuffer *buf = GST_BUFFER (object);

#if REMOTEOFFLOADEGRESS_IMPLICIT_QUEUE
      QueueStatistics stats;
      stats.pts = GST_BUFFER_DTS_OR_PTS(buf);
      if( egress->priv->queue && egress->priv->collectqueuestats )
      {
         GetQueueStats(egress->priv->queue, &stats);
         g_array_append_val(egress->priv->queue_stats, stats);
      }
#endif

      gst_buffer_ref(buf);
      GST_LOG_OBJECT (egress,
                      "gst_pad_push for buf=%p with pts=%"GST_TIME_FORMAT,
                      buf, GST_TIME_ARGS(GST_BUFFER_DTS_OR_PTS(buf)));
      GstFlowReturn ret = gst_pad_push (egress->priv->srcpad, buf);
      GST_LOG_OBJECT (egress, "gst_pad_push returned %s", gst_flow_get_name(ret));

      buffer_data_exchanger_send_buffer_flowreturn(egress->priv->pBufferExchanger,
                                                   buf,
                                                   ret);
      gst_buffer_unref(buf);

   }
   else
   if(GST_IS_EVENT (object))
   {
      GstEvent *event = GST_EVENT (object);
      gst_event_ref(event);
      GST_DEBUG_OBJECT(egress, "gst_pad_push_event for event=%s\n", GST_EVENT_TYPE_NAME(event));
      gboolean ret = gst_pad_push_event (egress->priv->srcpad, event);
      GST_DEBUG_OBJECT(egress, "gst_pad_push_event returned %d\n", ret);
      event_data_exchanger_send_event_result(egress->priv->pEventExchanger,
                                         event,
                                         ret);
      gst_event_unref (event);
   }
   else
   if(GST_IS_QUERY (object))
   {
       GstQuery *query = GST_QUERY (object);
       GST_DEBUG_OBJECT(egress, "gst_pad_peer_query for query=%s\n", GST_QUERY_TYPE_NAME(query));
       gboolean ret = gst_pad_peer_query (egress->priv->srcpad, query);


       GST_DEBUG_OBJECT(egress, "gst_pad_peer_query returned %d\n", ret);
       query_data_exchanger_send_query_result(egress->priv->pQueryExchanger,
                                         query,
                                         ret);
       gst_query_unref (query);
   }
   else
   {
      GST_WARNING_OBJECT (egress, "Unknown data type queued");
   }

}

static void
gst_remoteoffload_egress_start_stream_thread (GstRemoteOffloadEgress * egress)
{
  gst_remoteoffload_egress_flush_queue(egress);
  g_mutex_lock (&egress->priv->queueprotectmutex);
  egress->priv->is_flushing = FALSE;
  egress->priv->last_buffer_push_ret = GST_FLOW_OK;
  g_mutex_unlock (&egress->priv->queueprotectmutex);
  gst_pad_start_task (egress->priv->srcpad,
                      (GstTaskFunction) gst_remoteoffload_egress_stream_thread_routine,
                      egress, NULL);

  generic_data_exchanger_send(egress->priv->pGenericDataExchanger,
                              TRANSFER_CODE_EGRESS_STREAM_START,
                              NULL,
                              FALSE);
}

static void
gst_remoteoffload_egress_stop_stream_thread (GstRemoteOffloadEgress * egress)
{

  g_mutex_lock (&egress->priv->queueprotectmutex);
  egress->priv->is_flushing = TRUE;
  egress->priv->last_buffer_push_ret = GST_FLOW_FLUSHING;
  g_cond_broadcast (&egress->priv->queuecond);
  g_mutex_unlock (&egress->priv->queueprotectmutex);

  generic_data_exchanger_send(egress->priv->pGenericDataExchanger,
                              TRANSFER_CODE_EGRESS_STREAM_STOP,
                              NULL,
                              FALSE);

  if( !gst_pad_stop_task (egress->priv->srcpad) )
  {
     GST_WARNING_OBJECT (egress, "problem in gst_pad_stop_task on srcpad");
  }

  //we need to make sure to send ack's for
  // any buffers, queries, events sitting in our queue.
  // Otherwise, the sender on "the other side" will
  // be waiting indefinitely.
  gst_remoteoffload_egress_flush_queue(egress);

}

static gboolean
gst_remoteoffload_egress_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  GstRemoteOffloadEgress *egress = GST_REMOTEOFFLOAD_EGRESS (parent);

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      GST_INFO_OBJECT (pad, "%s in push mode", active ? "activating" :
          "deactivating");
      if (active) {
        gst_remoteoffload_egress_start_stream_thread (egress);
      } else {
        gst_remoteoffload_egress_stop_stream_thread (egress);
      }
      return TRUE;
    default:
      GST_DEBUG_OBJECT (pad, "unsupported activation mode");
      return FALSE;
  }
}

static gboolean
gst_remoteoffload_egress_srcpad_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstRemoteOffloadEgress *egress = GST_REMOTEOFFLOAD_EGRESS (parent);

  GST_DEBUG_OBJECT(egress, "name=%s", GST_EVENT_TYPE_NAME(event));

  gboolean bforward = TRUE;
  switch(GST_EVENT_TYPE(event))
  {
     case GST_EVENT_NAVIGATION:
        bforward = FALSE;
     break;

     case GST_EVENT_QOS:
     {
        GstQOSType type;
        gdouble proportion;
        GstClockTimeDiff diff;
        GstClockTime timestamp;

        gst_event_parse_qos(event,
                           &type,
                           &proportion,
                           &diff,
                           &timestamp);

        switch(type)
        {
           case GST_QOS_TYPE_OVERFLOW:
           GST_DEBUG_OBJECT(egress, "GST_QOS_TYPE_OVERFLOW");
           break;

           case GST_QOS_TYPE_UNDERFLOW:
           GST_WARNING_OBJECT(egress, "GST_QOS_TYPE_UNDERFLOW");
           break;

           case GST_QOS_TYPE_THROTTLE:
           GST_INFO_OBJECT(egress, "GST_QOS_TYPE_THROTTLE");
           break;
        }

        switch(type)
        {
           case GST_QOS_TYPE_OVERFLOW:
           GST_OBJECT_LOCK (egress);
           bforward = egress->priv->was_last_qos_bad;
           egress->priv->was_last_qos_bad = FALSE;
           GST_OBJECT_UNLOCK (egress);
           break;

           case GST_QOS_TYPE_UNDERFLOW:
           case GST_QOS_TYPE_THROTTLE:

           GST_OBJECT_LOCK (egress);
           egress->priv->was_last_qos_bad = TRUE;
           GST_OBJECT_UNLOCK (egress);
           break;
        }
     }
     break;

     default:
     break;
  }

  if( bforward )
  {
    gboolean ret = event_data_exchanger_send_event(egress->priv->pEventExchanger, event);
    gst_event_unref(event);
    return ret;
  }
  else
    return gst_pad_event_default (pad, parent, event);
}

static gboolean
gst_remoteoffload_egress_srcpad_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstRemoteOffloadEgress *self = GST_REMOTEOFFLOAD_EGRESS (parent);

  GST_DEBUG_OBJECT(self, "name=%s", GST_QUERY_TYPE_NAME(query) );

  /* answer some queries that do not make sense to be forwarded */
  switch (GST_QUERY_TYPE (query))
  {
    case GST_QUERY_CONTEXT:
      return FALSE;
    case GST_QUERY_CAPS:
    {
      GstState state;

      GST_STATE_LOCK (self);
      state = GST_STATE (self);

      GstState parent_state = GST_STATE_NULL;
      GstObject *self_parent = gst_element_get_parent(self);
      if( self_parent )
      {
         //self_parent should be an element
         if( GST_IS_ELEMENT(self_parent) )
         {
            GstElement *self_parent_elem = GST_ELEMENT(self_parent);
            GST_OBJECT_LOCK (self_parent_elem);
            parent_state = GST_STATE (self_parent_elem);
            GST_OBJECT_UNLOCK (self_parent_elem);
         }
         else
         {
            GST_WARNING_OBJECT(self,
                             "GstObject returned by gst_element_get_parent() is not a GstElement");
         }

         gst_object_unref(self_parent);
      }
      else
      {
         GST_WARNING_OBJECT(self, "gst_element_get_parent() returned NULL");
      }

      //We need this extra mutex because this element might perform a READY->NULL transition
      // during our caps query, which is really bad as objects will get deconstructed
      // as they're being used. We can't keep the state lock for the duration of the
      // query exchanger call. So, we use this special mutex here to protect the resources
      // in use.
      g_mutex_lock(&self->priv->caps_query_mutex);
      GST_STATE_UNLOCK (self);

      gboolean ret;
      //If either this element, or the parent of this element (the bin)
      // are in the NULL state, don't attempt to send this query. The
      // remote connection object may not have been constructed/initialized yet.
      //Caps queries happen when element pads are linked, so this is when
      // this case is most likely (and expected) to occur.
      if ((state == GST_STATE_NULL) || (parent_state == GST_STATE_NULL))
      {
        GST_DEBUG_OBJECT(self,
                         "state of this element, or the parent is NULL. Not handling this query.");
        ret = FALSE;
      }
      else
      {
         ret = query_data_exchanger_send_query(self->priv->pQueryExchanger, query);
      }
      g_mutex_unlock(&self->priv->caps_query_mutex);

      return ret;
    }
    break;
    default:
      break;
  }

  return query_data_exchanger_send_query(self->priv->pQueryExchanger, query);
}

static gboolean
gst_remoteoffload_egress_element_event (GstElement * element, GstEvent * event)
{
  GstRemoteOffloadEgress *egress = GST_REMOTEOFFLOAD_EGRESS (element);
  return gst_pad_push_event (egress->priv->srcpad, event);
}

static gboolean
gst_remoteoffload_egress_element_query (GstElement * element, GstQuery * query)
{
  GstRemoteOffloadEgress *egress = GST_REMOTEOFFLOAD_EGRESS (element);
  return gst_pad_query (egress->priv->srcpad, query);
}


static gboolean init_comm_objects(GstRemoteOffloadEgress *self)
{
   self->priv->was_last_qos_bad = FALSE;

   gboolean ret = FALSE;

   if( self->priv->channel )
   {
      self->priv->pBufferExchanger =
           buffer_data_exchanger_new(self->priv->channel, &self->priv->bufferCallback);
      self->priv->pQueryExchanger =
           query_data_exchanger_new(self->priv->channel, &self->priv->queryCallback);
      self->priv->pStateChangeExchanger =
            statechange_data_exchanger_new(self->priv->channel,
                                           NULL);
      self->priv->pEventExchanger =
            event_data_exchanger_new(self->priv->channel, &self->priv->eventCallback);
      self->priv->pGenericDataExchanger =
                 generic_data_exchanger_new(self->priv->channel,
                                            &self->priv->genericDataExchangerCallback);
      self->priv->pQueueStatsExchanger =
            queuestats_data_exchanger_new(self->priv->channel, &self->priv->queueStatsCallback);

      if( self->priv->pBufferExchanger &&
          self->priv->pQueryExchanger &&
          self->priv->pStateChangeExchanger &&
          self->priv->pEventExchanger &&
          self->priv->pGenericDataExchanger &&
          self->priv->pQueueStatsExchanger )
     {
        ret = TRUE;
     }
   }

   return ret;
}

static void deinit_comm_objects(GstRemoteOffloadEgress *self)
{
   //Note, the follow call is intentionally outside of the mutex.
   // In the event that the caps query happened just before, or while
   // our READY->NULL state transition is taking place, we need this
   // call to trigger a return from the query exchanger.
   if( self->priv->channel )
      remote_offload_comms_channel_cancel_all(self->priv->channel);

   g_mutex_lock(&self->priv->caps_query_mutex);
   if( self->priv->pQueryExchanger )
      g_object_unref(self->priv->pQueryExchanger);
   if( self->priv->pBufferExchanger )
      g_object_unref(self->priv->pBufferExchanger);
   if( self->priv->pStateChangeExchanger )
      g_object_unref(self->priv->pStateChangeExchanger);
   if( self->priv->pEventExchanger )
      g_object_unref(self->priv->pEventExchanger);
   if( self->priv->pGenericDataExchanger )
      g_object_unref(self->priv->pGenericDataExchanger);
   if( self->priv->pQueueStatsExchanger )
      g_object_unref(self->priv->pQueueStatsExchanger);
   if( self->priv->channel )
      g_object_unref(self->priv->channel);
   self->priv->pQueryExchanger = NULL;
   self->priv->pBufferExchanger = NULL;
   self->priv->pStateChangeExchanger = NULL;
   self->priv->pEventExchanger = NULL;
   self->priv->pGenericDataExchanger = NULL;
   self->priv->pQueueStatsExchanger = NULL;
   self->priv->channel = NULL;
   g_mutex_unlock(&self->priv->caps_query_mutex);
}

static GstStateChangeReturn
gst_remoteoffload_egress_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRemoteOffloadEgress *egress = GST_REMOTEOFFLOAD_EGRESS (element);

  {
      GstState current = GST_STATE_TRANSITION_CURRENT(transition);
      GstState next = GST_STATE_TRANSITION_NEXT(transition);
      GST_INFO_OBJECT (egress, "%s->%s",
                       gst_element_state_get_name (current),
                       gst_element_state_get_name (next));
   }

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
      GST_DEBUG_OBJECT (egress, "GST_STATE_CHANGE_NULL_TO_READY");

#if REMOTEOFFLOADEGRESS_IMPLICIT_QUEUE
      //automatically add a queue in-between this egress element,
      // and the first downstream element. This is to ensure
      // a thread break between receiving data from the
      // remote ingress, and the processing that will happen in
      // the next element.
      //TODO: Expose the option to do this as a property,
      // instead of a compile-time switch.
      if( gst_pad_is_linked(egress->priv->srcpad) )
      {
         GstObject *parent = gst_element_get_parent(element);
         if( parent && GST_IS_BIN(parent) )
         {
            gchar name[128];
            gchar *elementname = gst_element_get_name(element);
            g_snprintf(name, 128, "%s_queue\n", elementname);
            g_free(elementname);

            egress->priv->queue = gst_element_factory_make("queue", name);
            if( egress->priv->queue )
            {
               GstPad *peerpad = gst_pad_get_peer(egress->priv->srcpad);
               if( peerpad )
               {
                  if( gst_pad_unlink(egress->priv->srcpad, peerpad ) )
                  {
                     if( gst_bin_add(GST_BIN(parent), egress->priv->queue) )
                     {
                        //link our src pad to queue sink
                        GstPad *queuesinkpad =
                              gst_element_get_static_pad (egress->priv->queue, "sink");
                        gst_pad_link (egress->priv->srcpad, queuesinkpad);
                        gst_object_unref (queuesinkpad);

                        //link queue src pad to peer pad
                        GstPad *queuesrcpad =
                              gst_element_get_static_pad (egress->priv->queue, "src");
                        gst_pad_link (queuesrcpad, peerpad);
                        gst_object_unref (queuesrcpad);

                        //TODO: expose some property for user to control the size-related
                        // parameters of the queue that we implicitly add
                        g_object_set(egress->priv->queue, "max-size-bytes", 0, NULL);
                        g_object_set(egress->priv->queue, "max-size-time", 0, NULL);
                        g_object_set(egress->priv->queue, "max-size-buffers", 2, NULL);
                     }
                  }

                  gst_object_unref(peerpad);
               }
            }
            else
            {
               GST_ERROR_OBJECT (egress, "error creating queue");
            }

            gst_object_unref(parent);
         }
         else
         {
            GST_ERROR_OBJECT (egress, "parent is NULL or not a bin");
         }
      }
      else
      {
         GST_ERROR_OBJECT(egress, "not linked");
      }
#endif
      if( !init_comm_objects(egress) )
      {
         GST_ERROR_OBJECT(egress, "init_comm_objects failed");
         deinit_comm_objects(egress);
         return GST_STATE_CHANGE_FAILURE;
      }

      GST_DEBUG_OBJECT (egress, "GST_STATE_CHANGE_NULL_TO_READY complete");

    }
    break;
    case GST_STATE_CHANGE_READY_TO_NULL:
       generic_data_exchanger_send(egress->priv->pGenericDataExchanger,
                                   TRANSFER_CODE_READY_TO_NULL_NOTIFY,
                                   NULL,
                                   FALSE);

       g_mutex_lock (&egress->priv->queueprotectmutex);
       egress->priv->is_flushing = TRUE;
       g_mutex_unlock (&egress->priv->queueprotectmutex);
    case GST_STATE_CHANGE_NULL_TO_NULL:
       deinit_comm_objects(egress);
    break;
    default:
    break;
  }

  GstStateChangeReturn ret =
  GST_ELEMENT_CLASS (gst_remoteoffload_egress_parent_class)->change_state (element, transition);

  //send notification of state change
  if( ret == GST_STATE_CHANGE_SUCCESS )
  {
     switch (transition)
     {
        case GST_STATE_CHANGE_READY_TO_PAUSED:
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
        {

           GstState current = GST_STATE_TRANSITION_CURRENT(transition);
           GstState next = GST_STATE_TRANSITION_NEXT(transition);
           GST_INFO_OBJECT(egress, "Sending state change notification for %s->%s",
                                   gst_element_state_get_name (current),
                                   gst_element_state_get_name (next));
           if( !statechange_data_exchanger_send_statechange(egress->priv->pStateChangeExchanger,
                                                            transition) )
           {
              GST_ERROR_OBJECT(egress, "Error sending state change notification");
              ret = GST_STATE_CHANGE_FAILURE;
           }
        }
        break;
        default:
        break;
     }
  }

  return ret;
}

static void do_unserialized_query (GstElement * element, gpointer user_data)
{
   GstRemoteOffloadEgress *self = (GstRemoteOffloadEgress *)element;
   GstQuery *query = GST_QUERY (user_data);

   GST_DEBUG_OBJECT(self, "name=%s", GST_QUERY_TYPE_NAME(query));
   gboolean ret = gst_pad_peer_query (self->priv->srcpad, query);
   GST_DEBUG_OBJECT(self, "ret=%d", ret);

   switch(GST_QUERY_TYPE (query))
   {
      case GST_QUERY_CAPS:
      {
         //post-processing. Remove any entries from the result that
         // have non-system memory features. We want negotiation
         // that happens across host/remote boundaries to deal
         // purely with format.
         GstCaps *result;
         gst_query_parse_caps_result(query, &result);
         if( result )
         {
            gboolean restart;
            do
            {
               restart = FALSE;
               guint results_size = gst_caps_get_size(result);
               for( guint capsi = 0; capsi < results_size; capsi++ )
               {
                  //get the caps features for this entry
                 GstCapsFeatures *features =
                       gst_caps_get_features(result, capsi);
                 if( !gst_caps_features_is_equal(features,
                                                 GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY) )
                 {
                    gst_caps_remove_structure(result, capsi);
                    restart = TRUE;
                    break;
                 }
               }
            }
            while(restart);
         }
      }
      break;

      default:
      break;
   }

   query_data_exchanger_send_query_result(self->priv->pQueryExchanger,
                                         query,
                                         ret);

   gst_query_unref (query);
}

static void do_unserialized_event (GstElement * element, gpointer user_data)
{
   GstRemoteOffloadEgress *self = (GstRemoteOffloadEgress *)element;
   GstEvent *event = GST_EVENT (user_data);

   GST_DEBUG_OBJECT(self, "name=%s", GST_EVENT_TYPE_NAME(event));
   gst_event_ref(event);
   gboolean ret = gst_pad_push_event (self->priv->srcpad, event);
   GST_DEBUG_OBJECT(self, "ret=%d", ret);
   event_data_exchanger_send_event_result(self->priv->pEventExchanger,
                                      event,
                                      ret);
   gst_event_unref(event);
}



static void BufferReceivedCallback(GstBuffer *buffer, void *priv)
{
   GstRemoteOffloadEgress *self = (GstRemoteOffloadEgress *)priv;

   GST_LOG_OBJECT (self,
                   "buf=%p with pts=%"GST_TIME_FORMAT,
                   buffer, GST_TIME_ARGS(GST_BUFFER_DTS_OR_PTS(buffer)));
   g_mutex_lock (&self->priv->queueprotectmutex);
   if (self->priv->last_buffer_push_ret != GST_FLOW_OK)
   {
      GstFlowReturn last_ret = self->priv->last_buffer_push_ret;
      g_mutex_unlock (&self->priv->queueprotectmutex);
      GST_WARNING_OBJECT(self, "Last flow was %s.. rejecting buffer",
        gst_flow_get_name (last_ret));
      buffer_data_exchanger_send_buffer_flowreturn(self->priv->pBufferExchanger,
                                                   buffer,
                                                   last_ret);
      gst_buffer_unref (buffer);
      return;
   }

   if( self->priv->is_flushing )
   {
      g_mutex_unlock (&self->priv->queueprotectmutex);
      GST_WARNING_OBJECT(self, "flushing state... rejecting buffer");
      buffer_data_exchanger_send_buffer_flowreturn(self->priv->pBufferExchanger,
                                                   buffer,
                                                   GST_FLOW_FLUSHING);
      return;

   }
   g_queue_push_tail(self->priv->topush_queue, buffer);
   g_cond_broadcast (&self->priv->queuecond);
   g_mutex_unlock (&self->priv->queueprotectmutex);

}

static void QueryReceivedCallback(GstQuery *query, void *priv)
{
   GstRemoteOffloadEgress *self = (GstRemoteOffloadEgress *)priv;
   GST_DEBUG_OBJECT(self, "name=%s, serialized=%d",
                    GST_QUERY_TYPE_NAME(query),
                    GST_QUERY_IS_SERIALIZED (query));
   if (GST_QUERY_IS_SERIALIZED (query))
   {
      g_mutex_lock (&self->priv->queueprotectmutex);
      if( self->priv->is_flushing )
      {
         GST_WARNING_OBJECT(self, "last async state transition failed, rejecting %s query",
                            GST_QUERY_TYPE_NAME(query));
         g_mutex_unlock (&self->priv->queueprotectmutex);
         query_data_exchanger_send_query_result(self->priv->pQueryExchanger,
                                                query,
                                                FALSE);
         return;
      }
      g_queue_push_tail(self->priv->topush_queue, query);
      g_cond_broadcast (&self->priv->queuecond);
      g_mutex_unlock (&self->priv->queueprotectmutex);
   }
   else
   {
      gst_element_call_async (GST_ELEMENT (self), do_unserialized_query, query,
        NULL);
   }
}

static void EventReceivedCallback(GstEvent *event, void *priv)
{
   GstRemoteOffloadEgress *self = (GstRemoteOffloadEgress *)priv;
   GST_DEBUG_OBJECT(self, "name=%s, serialized=%d",
                    GST_EVENT_TYPE_NAME(event),
                    GST_EVENT_IS_SERIALIZED (event));
   if (GST_EVENT_IS_SERIALIZED (event))
   {
      g_mutex_lock (&self->priv->queueprotectmutex);
      if( self->priv->is_flushing )
      {
         GST_WARNING_OBJECT(self, "last async state transition failed, rejecting %s event",
                            GST_EVENT_TYPE_NAME(event));
         g_mutex_unlock (&self->priv->queueprotectmutex);
         event_data_exchanger_send_event_result(self->priv->pEventExchanger,
                                                event,
                                                FALSE);
         return;
      }
      g_queue_push_tail(self->priv->topush_queue, event);
      g_cond_broadcast (&self->priv->queuecond);
      g_mutex_unlock (&self->priv->queueprotectmutex);
   }
   else
   {
      gst_element_call_async (GST_ELEMENT (self), do_unserialized_event, event,
        NULL);
   }
}

static GArray *RequestQueueStats(void *priv)
{
   GstRemoteOffloadEgress *self = (GstRemoteOffloadEgress *)priv;

   return self->priv->queue_stats;
}

static gboolean GenericCallback(guint32 transfer_type, GArray *memblocks, void *priv)
{
   GstRemoteOffloadEgress *self = (GstRemoteOffloadEgress *)priv;
   gboolean ret = TRUE;
   switch(transfer_type)
   {
      case TRANSFER_CODE_READY_TO_NULL_NOTIFY:
         GST_DEBUG_OBJECT(self, "Received notification that ingress transitioned to NULL");
         remote_offload_comms_channel_cancel_all(self->priv->channel);
         break;
      default:
         GST_ERROR_OBJECT(self, "Unknown transfer_type");
         ret = FALSE;
      break;
   }
   return ret;
}


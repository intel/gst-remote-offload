/*
 *  remoteoffloadingress.c - GstRemoteOffloadIngress element
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

#include <gst/allocators/allocators.h>
#include "gstremoteoffloadingress.h"
#include "remoteoffloadcommschannel.h"
#include "querydataexchanger.h"
#include "bufferdataexchanger.h"
#include "statechangedataexchanger.h"
#include "eventdataexchanger.h"
#include "queuestatsdataexchanger.h"
#include "genericdataexchanger.h"
#include "gstingressegressdefs.h"

#define REMOTEOFFLOADINGRESS_IMPLICIT_QUEUE 1

enum
{
  PROP_COMMSCHANNEL = 1,
  PROP_COLLECTQUEUESTATS,
  N_PROPERTIES
};

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_remoteoffload_ingress_debug);
#define GST_CAT_DEFAULT gst_remoteoffload_ingress_debug

G_DEFINE_TYPE_WITH_CODE (GstRemoteOffloadIngress, gst_remoteoffload_ingress, GST_TYPE_ELEMENT,
  GST_DEBUG_CATEGORY_INIT (gst_remoteoffload_ingress_debug, "remoteoffloadingress", 0,
  "debug category for remoteoffloadingress"));

struct _GstRemoteOffloadIngressPrivate
{
   RemoteOffloadCommsChannel *channel;

   GList *consumable_mem_features;

   GstPad *sinkpad;

   GThreadPool *upstream_push_threads;

   StateChangeDataExchanger *pStateChangeExchanger;

   BufferDataExchanger *pBufferExchanger;

   QueryDataExchangerCallback queryCallback;
   QueryDataExchanger *pQueryExchanger;

   EventDataExchangerCallback eventCallback;
   EventDataExchanger *pEventExchanger;

   GenericDataExchangerCallback genericDataExchangerCallback;
   GenericDataExchanger *pGenericDataExchanger;

   QueueStatsDataExchangerCallback queueStatsCallback;
   QueueStatsDataExchanger *pQueueStatsExchanger;
   GArray *queue_stats;
   gboolean collectqueuestats;

   GMutex caps_query_mutex;
   GMutex async_transition_mutex;
   gboolean async_transition_in_progress;
   gboolean is_egress_null;

   GMutex streamthreadsyncmutex;
   GCond streamthreadsyncond;
   gboolean begressstreamthreadrunning;
   gboolean bingressstreamthreadrunning;

#if REMOTEOFFLOADINGRESS_IMPLICIT_QUEUE
   GstElement *queue;
#endif
};

static void gst_remoteoffload_ingress_finalize (GObject * object);

static void gst_remoteoffload_ingress_set_property (GObject * object,
                                                    guint prop_id,
                                                    const GValue * value,
                                                    GParamSpec * pspec);

static void gst_remoteoffload_ingress_get_property (GObject * object,
                                                    guint prop_id,
                                                    GValue * value,
                                                    GParamSpec * pspec);

static gboolean gst_remoteoffload_ingress_pad_activate_mode (GstPad * pad,
                                                             GstObject * parent,
                                                             GstPadMode mode,
                                                             gboolean active);

static gboolean gst_remoteoffload_ingress_sinkpad_event (GstPad * pad,
                                                         GstObject * parent,
                                                         GstEvent * event);

static gboolean gst_remoteoffload_ingress_sinkpad_query (GstPad * pad,
                                                         GstObject * parent,
                                                         GstQuery * query);

static GstFlowReturn gst_remoteoffload_ingress_sinkpad_chain (GstPad * pad,
                                                              GstObject * parent,
                                                              GstBuffer * buffer);

static gboolean gst_remoteoffload_ingress_element_event (GstElement * element,
                                                      GstEvent * event);

static gboolean gst_remoteoffload_ingress_element_query (GstElement * element,
                                                         GstQuery * query);

static GstStateChangeReturn gst_remoteoffload_ingress_change_state (GstElement *element,
                                                                    GstStateChange transition);


//exchanger callbacks
static void QueryReceivedCallback(GstQuery *query, void *priv);
static void EventReceivedCallback(GstEvent *event, void *priv);
static gboolean GenericCallback(guint32 transfer_type, GArray *memblocks, void *priv);
static GArray *RequestQueueStats(void *priv);

//function to push event or query upstream
static void push_upstream (gpointer data, gpointer user_data);

static void
gst_remoteoffload_ingress_class_init (GstRemoteOffloadIngressClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = gst_remoteoffload_ingress_finalize;
  gobject_class->set_property = gst_remoteoffload_ingress_set_property;
  gobject_class->get_property = gst_remoteoffload_ingress_get_property;

  g_object_class_install_property (gobject_class, PROP_COMMSCHANNEL,
      g_param_spec_pointer ("commschannel", "CommsChannel",
          "CommsChannel object in use",
           G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_COLLECTQUEUESTATS,
      g_param_spec_boolean ("collectqueuestats", "CollectQueueStats",
          "Collect Queue Statistics",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
      "Remote Offload Ingress",
      "Ingress",
      "Routes data to a remote-connected Egress",
      "Ryan Metcalfe <ryan.d.metcalfe@intel.com>");
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_remoteoffload_ingress_change_state);
  gstelement_class->query = GST_DEBUG_FUNCPTR (gst_remoteoffload_ingress_element_query);
  gstelement_class->send_event =
      GST_DEBUG_FUNCPTR (gst_remoteoffload_ingress_element_event);
}

static void
gst_remoteoffload_ingress_init (GstRemoteOffloadIngress * self)
{
  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_SINK);

  self->priv = g_malloc(sizeof(GstRemoteOffloadIngressPrivate));

  self->priv->channel = NULL;
  self->priv->consumable_mem_features = NULL;

  self->priv->pBufferExchanger = NULL;

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

  self->priv->upstream_push_threads = g_thread_pool_new (push_upstream, self, -1, FALSE, NULL);

  self->priv->collectqueuestats = FALSE;
  self->priv->queue_stats = g_array_new(FALSE, FALSE, sizeof(QueueStatistics));
  self->priv->pQueueStatsExchanger = NULL;
  self->priv->queueStatsCallback.request_received = RequestQueueStats;
  self->priv->queueStatsCallback.priv = self;

  g_mutex_init(&self->priv->async_transition_mutex);
  g_mutex_init(&self->priv->caps_query_mutex);
  self->priv->async_transition_in_progress = FALSE;

  g_mutex_init(&self->priv->streamthreadsyncmutex);
  g_cond_init(&self->priv->streamthreadsyncond);
  self->priv->begressstreamthreadrunning = FALSE;
  self->priv->bingressstreamthreadrunning = FALSE;

  GstPadTemplate *pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (self), "sink");
  g_return_if_fail (pad_template != NULL);

  self->priv->sinkpad = gst_pad_new_from_template (pad_template, "sink");

  gst_pad_set_activatemode_function (self->priv->sinkpad,
      gst_remoteoffload_ingress_pad_activate_mode);

  gst_pad_set_query_function (self->priv->sinkpad, gst_remoteoffload_ingress_sinkpad_query);
  gst_pad_set_event_function (self->priv->sinkpad, gst_remoteoffload_ingress_sinkpad_event);
  gst_pad_set_chain_function (self->priv->sinkpad, gst_remoteoffload_ingress_sinkpad_chain);
  gst_element_add_pad (GST_ELEMENT_CAST (self), self->priv->sinkpad);

}

static void
gst_remoteoffload_ingress_finalize (GObject * object)
{
  GstRemoteOffloadIngress *self = GST_REMOTEOFFLOAD_INGRESS (object);
  g_thread_pool_free (self->priv->upstream_push_threads, TRUE, TRUE);
  g_array_unref (self->priv->queue_stats);
  g_mutex_clear(&self->priv->async_transition_mutex);
  g_mutex_clear(&self->priv->caps_query_mutex);
  g_mutex_clear(&self->priv->streamthreadsyncmutex);
  g_cond_clear(&self->priv->streamthreadsyncond);
  g_free(self->priv);
  G_OBJECT_CLASS (gst_remoteoffload_ingress_parent_class)->finalize (object);
}

static void
gst_remoteoffload_ingress_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRemoteOffloadIngress *self = GST_REMOTEOFFLOAD_INGRESS (object);

  switch (prop_id) {
    case PROP_COMMSCHANNEL:
    {
      RemoteOffloadComms *tmp = g_value_get_pointer (value);
      if( REMOTEOFFLOAD_IS_COMMSCHANNEL(tmp) )
        self->priv->channel = (RemoteOffloadCommsChannel *)g_object_ref(tmp);
    }
    break;

    case PROP_COLLECTQUEUESTATS:
      self->priv->collectqueuestats = g_value_get_boolean(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_remoteoffload_ingress_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRemoteOffloadIngress *self = GST_REMOTEOFFLOAD_INGRESS (object);
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

static inline int gst_remoteoffload_ingress_serialized_stream_call(GstRemoteOffloadIngress *self,
                                                                    gpointer object)
{
   int ret;
   g_mutex_lock(&self->priv->streamthreadsyncmutex);
   if( !self->priv->begressstreamthreadrunning && self->priv->bingressstreamthreadrunning )
   {
      g_cond_wait (&self->priv->streamthreadsyncond, &self->priv->streamthreadsyncmutex);
   }
   if (GST_IS_BUFFER (object))
   {
      if( self->priv->bingressstreamthreadrunning )
      {
         GstBuffer *buf = GST_BUFFER (object);
         ret = (int)buffer_data_exchanger_send_buffer(self->priv->pBufferExchanger,
                                                      buf);
      }
      else
      {
         ret = (int)GST_FLOW_FLUSHING;
      }
   }
   else
   if(GST_IS_EVENT (object))
   {
      if( self->priv->bingressstreamthreadrunning )
      {
         GstEvent *event = GST_EVENT (object);
         ret = (int)event_data_exchanger_send_event(self->priv->pEventExchanger,
                                                    event);
      }
      else
      {
         ret = (int)FALSE;
      }
   }
   else
   if(GST_IS_QUERY (object))
   {
      if( self->priv->bingressstreamthreadrunning )
      {
         GstQuery *query = GST_QUERY (object);
         ret = (int)query_data_exchanger_send_query(self->priv->pQueryExchanger,
                                                   query);
      }
      else
      {
         ret = (int)FALSE;
      }
   }
   else
   {
      GST_ERROR_OBJECT (self, "Unknown object type");
      ret = -1;
   }
   g_mutex_unlock(&self->priv->streamthreadsyncmutex);

   return ret;
}

//This function answers 2 questions:
// 1. Can the current comms implementation in use support the memory type within these caps?
// 2. Is the caps feature in these caps equal to memory:SystemMemory?
static inline void can_consume_caps(GstRemoteOffloadIngress *self,
                                    GstCaps *caps,
                                    guint capsi,
                                    gboolean *can_support,
                                    gboolean *is_sysmem)
{
   if( G_UNLIKELY(!caps) )
   {
      //we treat a NULL caps as 'ANY' here..
      // so yeah, not sure if this is philosophically correct.
      if( can_support )
         *can_support = TRUE;

      if( is_sysmem )
         *is_sysmem = TRUE;

      return;
   }

   GstCapsFeatures *caps_feature = gst_caps_get_features(caps, capsi);
   if( G_LIKELY(caps_feature) )
   {
      //It is required that all comms implementations support system memory.
      if( gst_caps_features_is_equal(caps_feature,
                                     GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY) )
      {
         if( can_support )
            *can_support = TRUE;

         if( is_sysmem )
            *is_sysmem = TRUE;

         return;
      }
      else
      {
         if( is_sysmem )
            *is_sysmem = FALSE;

         if( can_support )
         {
            *can_support = FALSE;

            GList *feature_list = self->priv->consumable_mem_features;
            for( GList *fl = feature_list; fl != NULL; fl = fl->next )
            {
               GstCapsFeatures *feature = (GstCapsFeatures *)fl->data;
               if( feature )
               {
                  if( gst_caps_features_is_equal(caps_feature,
                                                 feature) )
                  {
                     *can_support = TRUE;
                     break;
                  }
               }
            }
         }
      }
   }
   else
   {
      if( can_support )
         *can_support = TRUE;

      if( is_sysmem )
         *is_sysmem = TRUE;
   }
}

#if !GST_CHECK_VERSION(1,16,0)
static inline void
gst_caps_set_features_simple (GstCaps * caps, GstCapsFeatures * features)
{
  guint i;
  guint n;

  g_return_if_fail (caps != NULL);
  g_return_if_fail ((GST_CAPS_REFCOUNT_VALUE (caps) == 1));

  n = gst_caps_get_size (caps);

  for (i = 0; i < n; i++) {
    GstCapsFeatures *f;

    /* Transfer ownership of @features to the last structure */
    if (features && i < n - 1)
      f = gst_caps_features_copy (features);
    else
      f = features;

    gst_caps_set_features (caps, i, f);
  }
}
#endif

static gboolean handle_caps_query(GstRemoteOffloadIngress *self,
                                  GstQuery * query)
{
   //Populate GstFeatures-to-Caps table
   GHashTable *feature_to_caps_list = g_hash_table_new_full(g_str_hash,
                                                       g_str_equal,
                                                       NULL,
                                                       NULL);

   GList *feature_list = self->priv->consumable_mem_features;
   for( GList *fl = feature_list; fl != NULL; fl = fl->next )
   {
      GstCapsFeatures *feature = (GstCapsFeatures *)fl->data;
      if( feature )
      {
         gchar *feature_str = gst_caps_features_to_string(feature);
         if( feature_str )
         {
            g_hash_table_insert(feature_to_caps_list,
                                feature_str,
                                gst_caps_new_empty());
         }
      }
   }

   //get the filter
   GstCaps *filter;
   gst_query_parse_caps(query, &filter);

   GST_DEBUG_OBJECT(self, "original filter caps: %"GST_PTR_FORMAT, filter);

   //We'll create a new filter based on the original, but
   // need to adjust it to remove any memory-specific features.
   // We basically want negotiation across the ingress/egress
   // boundary to deal purely with format.
   GstCaps *filter_adjusted = NULL;
   if( filter )
      filter_adjusted = gst_caps_new_empty();

   guint filter_caps_size = 0;
   if( filter )
      filter_caps_size = gst_caps_get_size(filter);

   //for each caps entry within the filter
   for( guint capsi = 0; capsi < filter_caps_size; capsi++ )
   {
      //get the caps features for this entry
      GstCapsFeatures *features =
            gst_caps_get_features(filter, capsi);

      GstCaps* new_caps_entry = gst_caps_copy_nth(filter, capsi);

      if( features && !gst_caps_features_is_equal(features,
                                               GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY))
      {
         gchar *feature_str = gst_caps_features_to_string(features);
         if( feature_str )
         {
            //does our comms implementation directly support this caps memory feature?
            if( g_hash_table_contains(feature_to_caps_list, feature_str) )
            {
               //yes! In this case, we want to remove the feature from this caps entry,
               // and add it to the filter. But we also want to cache it to our
               // "feature-to-caps" hash table.
               GstCaps *memory_caps = (GstCaps *) g_hash_table_lookup(feature_to_caps_list,
                                                                      feature_str);

               //adjust the caps.. remove this feature
               gst_caps_set_features(new_caps_entry, 0, NULL);

               //then make a copy of this caps to append to the current
               // caps list.
               gst_caps_append(memory_caps, gst_caps_copy(new_caps_entry));
            }
            else
            {
               //no! Filter out this entry right here and now.
               gst_caps_unref(new_caps_entry);
               new_caps_entry = NULL;
            }

            g_free(feature_str);
         }
         else
         {
            GST_WARNING_OBJECT(self, "gst_caps_features_to_string returned NULL");
         }
      }

      if( new_caps_entry )
      {
         gst_caps_append(filter_adjusted, new_caps_entry);
      }
   }

   GST_DEBUG_OBJECT(self, "adjusted filter caps: %"GST_PTR_FORMAT, filter_adjusted);

   if( filter_adjusted )
   {
      //if the adjusted filter is empty, then we can just set the empty result and return now.
      if( gst_caps_is_empty(filter_adjusted) )
      {
         GstCaps *empty_caps_result = gst_caps_new_empty();
         gst_query_set_caps_result(query, empty_caps_result);
         gst_caps_unref(filter_adjusted);
         g_hash_table_destroy(feature_to_caps_list);
         return TRUE;
      }
   }

   //create a new query based on our adjusted filter.
   // Note we will send this copied query, not the original one
   GstQuery *query_copy = gst_query_new_caps(filter_adjusted);
   query_copy = gst_query_make_writable(query_copy);
   if( filter_adjusted )
   {
      gst_caps_unref(filter_adjusted);
   }

   gboolean ret = query_data_exchanger_send_query(self->priv->pQueryExchanger, query_copy);

   if( ret )
   {
      GstCaps *result = NULL;
      gst_query_parse_caps_result(query_copy, &result);
      if( result )
      {
         GST_DEBUG_OBJECT(self, "original result caps: %"GST_PTR_FORMAT, result);
         //iterate through our feature list
         for( GList *fl = feature_list; fl != NULL; fl = fl->next )
         {
            GstCapsFeatures *feature = (GstCapsFeatures *)fl->data;
            if( feature )
            {
               gchar *feature_str = gst_caps_features_to_string(feature);

               GstCaps *cached_caps = (GstCaps *) g_hash_table_lookup(feature_to_caps_list,
                                                                    feature_str);

               if( cached_caps )
               {
                  GstCaps *intersection = gst_caps_intersect_full(result,
                                                                  cached_caps,
                                                                  GST_CAPS_INTERSECT_FIRST);
                  if( !gst_caps_is_empty(intersection) )
                  {
                     gst_caps_set_features_simple(intersection, gst_caps_features_new(feature_str, NULL));
                     gst_caps_append(result, intersection);
                  }
               }
            }
         }

         GST_DEBUG_OBJECT(self, "adjusted result caps: %"GST_PTR_FORMAT, result);

         //after adding all new device entries, intersect the adjusted results with the original
         // filter passed in
         if( filter )
         {
            //intersect the result with the filter.
            GstCaps *intersection = gst_caps_intersect_full(filter,
                                                            result,
                                                            GST_CAPS_INTERSECT_FIRST);
            result = intersection;

            if( gst_caps_is_equal(result, filter) )
            {
               if( result != filter )
                  gst_caps_unref(result);
               result = filter;
            }
         }
         else
         {
            //if the original filter was NULL, and we received a result that is not 'ANY',
            // then for each caps entry of the result, we should append the same caps
            // but with the supported memory features.
            if( !gst_caps_is_any(result) )
            {
               GstCaps *result_staging = gst_caps_new_empty();
               for( GList *fl = feature_list; fl != NULL; fl = fl->next )
               {
                  GstCapsFeatures *feature = (GstCapsFeatures *)fl->data;
                  if( feature )
                  {
                     gchar *feature_str = gst_caps_features_to_string(feature);
                     GstCaps *result_withfeatures = gst_caps_copy(result);
                     gst_caps_set_features_simple(result_withfeatures, gst_caps_features_new(feature_str, NULL));
                     gst_caps_append(result_staging, result_withfeatures);
                     g_free(feature_str);
                  }
               }
               gst_caps_append(result, result_staging);
            }
         }

         gst_query_set_caps_result(query, result);
         if( filter && (result != filter) )
         {
           gst_caps_unref(result);
         }

         GST_DEBUG_OBJECT(self, "final result caps: %"GST_PTR_FORMAT, result);
      }
   }

   gst_query_unref(query_copy);
   g_hash_table_destroy(feature_to_caps_list);
   return ret;
}

static gboolean handle_accept_caps_query(GstRemoteOffloadIngress *self,
                                         GstQuery * query)
{
   gboolean ret;
   GstCaps *caps;
   gst_query_parse_accept_caps(query, &caps);
   if( G_UNLIKELY(!caps) )
   {
      GST_ERROR_OBJECT(self, "gst_query_parse_accept_caps gave NULL caps");
      return FALSE;
   }

   GST_DEBUG_OBJECT(self, "original accept-caps: %"GST_PTR_FORMAT, caps);

   //1. Can we consume this caps?
   gboolean can_support = FALSE;
   //2. Is the caps feature for this caps memory:SystemMemory
   gboolean is_sysmem = FALSE;
   can_consume_caps(self, caps, 0, &can_support, &is_sysmem);

   //if we can't support it, we're done here. Set the result of the query to false, and return.
   if( !can_support )
   {
      gst_query_set_accept_caps_result(query, FALSE);
      return TRUE;
   }

   // if the caps feature is not memory:SystemMemory, we will need to send a version of the
   // caps that strips this caps feature. We only want to know whether remote-side can support
   // this format. The receiver (egress) can decide on what memory type it wants to use by sending
   // it's own series of caps / accept_caps queries if necessary.
   if( !is_sysmem )
   {
      //start by making a copy of the caps
      GstCaps* stripped_caps = gst_caps_copy_nth(caps, 0);

      //strip the memory feature
      gst_caps_set_features(stripped_caps, 0, NULL);

      GST_DEBUG_OBJECT(self, "adjusted accept-caps: %"GST_PTR_FORMAT, stripped_caps);

      //create new accept-caps query
      GstQuery *new_query = gst_query_new_accept_caps(stripped_caps);
      if( new_query )
      {
         //send the stripped accept-caps query
         ret = query_data_exchanger_send_query(self->priv->pQueryExchanger, new_query);

         //parse the result, and set it to the original query
         gboolean result = FALSE;
         gst_query_parse_accept_caps_result(new_query, &result);
         gst_query_set_accept_caps_result(query, result);

         gst_query_unref(new_query);
      }
      else
      {
         GST_ERROR_OBJECT(self, "gst_query_new_accept_caps failed");
         ret = FALSE;
      }

      gst_caps_unref(stripped_caps);
   }
   else
   {
      //we can just send the original query. The result will be set internally
      ret = query_data_exchanger_send_query(self->priv->pQueryExchanger, query);
   }

   return ret;
}

static gboolean handle_caps_event(GstRemoteOffloadIngress *self,
                                  GstEvent * event)
{
  gboolean ret;
  GstCaps *caps;
  gst_event_parse_caps(event, &caps);
  if( G_UNLIKELY(!caps) )
  {
     GST_ERROR_OBJECT(self, "gst_event_parse_caps gave NULL caps");
     return FALSE;
  }

  GST_DEBUG_OBJECT(self, "original event caps: %"GST_PTR_FORMAT, caps);

  //1. Can we consume this caps?
   gboolean can_support = FALSE;
   //2. Is the caps feature for this caps memory:SystemMemory
   gboolean is_sysmem = FALSE;
   can_consume_caps(self, caps, 0, &can_support, &is_sysmem);

   //if we can't support it, we're done here. Just return FALSE.
   if( !can_support )
   {
      //Print a warning here because this is odd behavior. These caps should
      // have previously been accepted by us via an accept-caps query. Something
      // odd is going on if we're here.
      GST_WARNING_OBJECT(self, "CAPS event contains unsupported caps");
      return FALSE;
   }

   // if the caps feature is not memory:SystemMemory, we will need to send a version of the
   // caps that strips this caps feature.
   if( !is_sysmem )
   {
      //start by making a copy of the caps
      GstCaps* stripped_caps = gst_caps_copy_nth(caps, 0);

      //strip the memory feature
      gst_caps_set_features(stripped_caps, 0, NULL);

      GST_DEBUG_OBJECT(self, "adjusted event caps: %"GST_PTR_FORMAT, stripped_caps);

      //create new caps event
      GstEvent *new_event = gst_event_new_caps(stripped_caps);
      if( new_event )
      {
         //send the stripped accept-caps query
         ret = (gboolean)gst_remoteoffload_ingress_serialized_stream_call(self, new_event);

         gst_event_unref(new_event);
      }
      else
      {
         GST_ERROR_OBJECT(self, "gst_event_new_caps failed");
         ret = FALSE;
      }

      gst_caps_unref(stripped_caps);
   }
   else
   {
      //we can just send the original event.
      ret = (gboolean)gst_remoteoffload_ingress_serialized_stream_call(self, event);
   }

   gst_event_unref(event);
   return ret;
}

static gboolean
gst_remoteoffload_ingress_sinkpad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstRemoteOffloadIngress *self = GST_REMOTEOFFLOAD_INGRESS (parent);
  gboolean ret;

  GST_DEBUG_OBJECT(self, "name=%s, serialized=%d",
                   GST_EVENT_TYPE_NAME(event),
                   GST_EVENT_IS_SERIALIZED (event));

  if( GST_EVENT_TYPE(event) == GST_EVENT_CAPS )
  {
     return handle_caps_event(self, event);
  }

  if( GST_EVENT_IS_SERIALIZED(event) )
  {
     ret = (gboolean)gst_remoteoffload_ingress_serialized_stream_call(self, event);
  }
  else
  {
     ret = event_data_exchanger_send_event(self->priv->pEventExchanger, event);
  }

  if( GST_EVENT_TYPE (event) == GST_EVENT_EOS )
  {
     GST_INFO_OBJECT(self, "posting EOS");
     gst_element_post_message (GST_ELEMENT (self), gst_message_new_eos(parent));
  }

  gst_event_unref (event);

  return ret;
}

static GstFlowReturn
gst_remoteoffload_ingress_sinkpad_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstRemoteOffloadIngress *self = GST_REMOTEOFFLOAD_INGRESS (parent);
  GstFlowReturn ret;

#if REMOTEOFFLOADINGRESS_IMPLICIT_QUEUE

  if( self->priv->queue && self->priv->collectqueuestats )
  {
     QueueStatistics stats;
     stats.pts = GST_BUFFER_DTS_OR_PTS(buffer);
     GetQueueStats(self->priv->queue, &stats);
     g_array_append_val(self->priv->queue_stats, stats);
  }

#endif

  GST_LOG_OBJECT (self, "buf=%p with pts=%"GST_TIME_FORMAT,
                    buffer, GST_TIME_ARGS(GST_BUFFER_DTS_OR_PTS(buffer)));

  ret = (GstFlowReturn)gst_remoteoffload_ingress_serialized_stream_call(self,
                                                                        buffer);

  GST_LOG_OBJECT(self, "buffer_data_exchanger_send_buffer() returned %s",
                   gst_flow_get_name(ret));

  gst_buffer_unref (buffer);
  return ret;
}


static gboolean
gst_remoteoffload_ingress_sinkpad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstRemoteOffloadIngress *self = GST_REMOTEOFFLOAD_INGRESS (parent);

  GST_DEBUG_OBJECT(self, "name=%s, serialized=%d",
                   GST_QUERY_TYPE_NAME(query), GST_QUERY_IS_SERIALIZED (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:
      return FALSE;

    case GST_QUERY_CAPS:
    {
      GstState state;

      gboolean ret;
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
        ret = handle_caps_query(self, query);
      }
      g_mutex_unlock(&self->priv->caps_query_mutex);

      return ret;
    }
    break;

    case GST_QUERY_ACCEPT_CAPS:
    {
       return handle_accept_caps_query(self, query);
    }
    break;

    default:
       return gst_pad_query_default (pad, parent, query);
    break;
  }
}

static gboolean
gst_remoteoffload_ingress_element_query (GstElement * element, GstQuery * query)
{
  GstRemoteOffloadIngress *self = GST_REMOTEOFFLOAD_INGRESS (element);

  gboolean downstream = GST_EVENT_IS_DOWNSTREAM (query) ? TRUE : FALSE;
  GST_DEBUG_OBJECT(self,
                   "name=%s, serialized=%d, downstream=%d",
                   GST_QUERY_TYPE_NAME(query),
                   GST_QUERY_IS_SERIALIZED (query),
                   downstream);
  if( downstream )
  {
     if(GST_QUERY_IS_SERIALIZED (query))
     {
        return (gboolean)gst_remoteoffload_ingress_serialized_stream_call(self, query);
     }
     else
     {
        return query_data_exchanger_send_query(self->priv->pQueryExchanger, query);
     }
  }
  else
  {
     return GST_ELEMENT_CLASS (gst_remoteoffload_ingress_parent_class)->query (element, query);
  }
}

static gboolean
gst_remoteoffload_ingress_element_event (GstElement * element, GstEvent * event)
{
  GstRemoteOffloadIngress *self = GST_REMOTEOFFLOAD_INGRESS (element);

  gboolean downstream = GST_EVENT_IS_DOWNSTREAM (event) ? TRUE : FALSE;
  GST_DEBUG_OBJECT(self,
                   "name=%s, serialized=%d, downstream=%d",
                   GST_EVENT_TYPE_NAME(event),
                   GST_EVENT_IS_SERIALIZED (event),
                   downstream);
  if( downstream )
  {
     gboolean ret;
     if( GST_EVENT_IS_SERIALIZED (event) )
     {
        ret = (gboolean)gst_remoteoffload_ingress_serialized_stream_call(self, event);
     }
     else
     {
        ret = event_data_exchanger_send_event(self->priv->pEventExchanger, event);
     }
     gst_event_unref(event);
     return ret;
  }
  else
  {
     return gst_pad_push_event (self->priv->sinkpad, event);
  }

}

static gboolean
gst_remoteoffload_ingress_pad_activate_mode (GstPad * pad,
    GstObject * parent, GstPadMode mode, gboolean active)
{
  GstRemoteOffloadIngress *self = GST_REMOTEOFFLOAD_INGRESS (parent);
  if (mode == GST_PAD_MODE_PULL)
    return FALSE;

  if( active )
  {
      GST_DEBUG_OBJECT(self, "streaming thread start");
      g_mutex_lock(&self->priv->streamthreadsyncmutex);
      self->priv->bingressstreamthreadrunning = TRUE;
      g_mutex_unlock(&self->priv->streamthreadsyncmutex);
  }
  else
  {
     GST_DEBUG_OBJECT(self, "streaming thread stop");
     g_mutex_lock(&self->priv->streamthreadsyncmutex);
     self->priv->bingressstreamthreadrunning = FALSE;
     g_cond_broadcast (&self->priv->streamthreadsyncond);
     g_mutex_unlock(&self->priv->streamthreadsyncmutex);
  }

  return TRUE;
}

static gboolean init_comm_objects(GstRemoteOffloadIngress *self)
{
   gboolean ret = FALSE;
   if( self->priv->channel )
   {
      self->priv->consumable_mem_features =
            remote_offload_comms_channel_get_consumable_memfeatures(self->priv->channel);

      self->priv->pBufferExchanger =
           buffer_data_exchanger_new(self->priv->channel, NULL);
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

static void deinit_comm_objects(GstRemoteOffloadIngress *self)
{
   //Note, the follow call is intentionally outside of the mutex.
   // In the event that the caps query happened just before, or while
   // our READY->NULL state transition is taking place, we need this
   // call to trigger a return from the query exchanger.
   if( self->priv->channel )
      remote_offload_comms_channel_cancel_all(self->priv->channel);

   g_mutex_lock(&self->priv->caps_query_mutex);
   if( self->priv->consumable_mem_features )
   {
      for( GList *fl = self->priv->consumable_mem_features; fl != NULL; fl = fl->next )
      {
         GstCapsFeatures *feature = (GstCapsFeatures *)fl->data;
         gst_caps_features_free(feature);
      }
      g_list_free(self->priv->consumable_mem_features);
      self->priv->consumable_mem_features = NULL;
   }
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

static void do_async_state_transition (GstElement * element, gpointer user_data)
{
   GstRemoteOffloadIngress *self = GST_REMOTEOFFLOAD_INGRESS (element);

   GstStateChange *pStateChange = (GstStateChange *)user_data;
   GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

   if( !wait_for_state_change(self->priv->pStateChangeExchanger, *pStateChange, 25 * G_TIME_SPAN_SECOND) )
   {
       GST_WARNING_OBJECT(element, "Error in wait_for_state_change");
       ret = GST_STATE_CHANGE_FAILURE;
   }

   if( ret  )
   {
     GST_STATE_LOCK (element);
     gst_element_continue_state (element, ret);
     self->priv->async_transition_in_progress = FALSE;
     GST_STATE_UNLOCK (element);

     gst_element_post_message (GST_ELEMENT (self),
     gst_message_new_async_done(GST_OBJECT(self), GST_CLOCK_TIME_NONE));
     GST_DEBUG_OBJECT(element, "async done");
   }

   g_free(pStateChange);
}

static GstStateChangeReturn
gst_remoteoffload_ingress_change_state (GstElement * element,
    GstStateChange transition)
{
   GstRemoteOffloadIngress *self = GST_REMOTEOFFLOAD_INGRESS (element);

   {
      GstState current = GST_STATE_TRANSITION_CURRENT(transition);
      GstState next = GST_STATE_TRANSITION_NEXT(transition);
      GST_INFO_OBJECT (self, "%s->%s",
                       gst_element_state_get_name (current),
                       gst_element_state_get_name (next));
   }

   switch (transition)
   {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_READY:
    {
       if( self->priv->async_transition_in_progress )
       {
          cancel_wait_for_state_change(self->priv->pStateChangeExchanger);
       }
    }
    break;
    default:
    break;
   }

   GstStateChange ret =
    GST_ELEMENT_CLASS (gst_remoteoffload_ingress_parent_class)->change_state (element, transition);

   switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
#if REMOTEOFFLOADINGRESS_IMPLICIT_QUEUE
      //automatically add a queue in-between the connected,
      // upstream element and this ingress. This is to ensure
      // a thread break between receiving data from the
      // remote egress, and the processing that will happen in
      // the next element.
      //TODO: Expose the option to do this as a property,
      // instead of a compile-time switch.
      if( gst_pad_is_linked(self->priv->sinkpad) )
      {
         GstObject *parent = gst_element_get_parent(element);

         if( parent && GST_IS_BIN(parent) )
         {
            gchar name[128];
            gchar *elementname = gst_element_get_name(element);
            g_snprintf(name, 128, "%s_queue\n", elementname);
            g_free(elementname);

            self->priv->queue = gst_element_factory_make("queue", name);
            if( self->priv->queue )
            {
               GstPad *peerpad = gst_pad_get_peer(self->priv->sinkpad);
               if( peerpad )
               {
                  if( gst_pad_unlink(peerpad, self->priv->sinkpad ) )
                  {
                     if( gst_bin_add(GST_BIN(parent), self->priv->queue) )
                     {
                        //link the peer pad to queue sink
                        GstPad *queuesinkpad = gst_element_get_static_pad (self->priv->queue,
                                                                           "sink");
                        gst_pad_link (peerpad, queuesinkpad);
                        gst_object_unref (queuesinkpad);

                        GstPad *queuesrcpad = gst_element_get_static_pad (self->priv->queue,
                                                                          "src");
                        gst_pad_link (queuesrcpad, self->priv->sinkpad);
                        gst_object_unref (queuesrcpad);

                        //TODO: expose some property for user to control the size-related
                        // parameters of the queue that we implicitly add
                        g_object_set(self->priv->queue, "max-size-bytes", 0, NULL);
                        g_object_set(self->priv->queue, "max-size-time", 0, NULL);
                        g_object_set(self->priv->queue, "max-size-buffers", 2, NULL);
                     }
                  }

                  gst_object_unref(peerpad);
               }
            }
            else
            {
               GST_ERROR_OBJECT (self, "error creating queue");
            }

            gst_object_unref(parent);
         }
         else
         {
            GST_ERROR_OBJECT (self, "parent is NULL or not a bin");
         }
      }
      else
      {
         GST_ERROR_OBJECT (self, "not linked");
      }
#endif

      if( !init_comm_objects(self) )
      {
         GST_ERROR_OBJECT (self, "init_comm_objects failed");
         deinit_comm_objects(self);
         ret = GST_STATE_CHANGE_FAILURE;
      }

      GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_NULL_TO_READY complete\n");
    }
    break;

    case GST_STATE_CHANGE_READY_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
       GstState current = GST_STATE_TRANSITION_CURRENT(transition);
       GstState next = GST_STATE_TRANSITION_NEXT(transition);

       GST_INFO_OBJECT(self, "Performing asynchronous %s->%s transition",
                       gst_element_state_get_name (current),
                       gst_element_state_get_name (next));

       GstStateChange *pStateChange = g_malloc(sizeof(GstStateChange));
       *pStateChange = transition;

       self->priv->async_transition_in_progress = TRUE;
       gst_element_post_message (GST_ELEMENT (self),
       gst_message_new_async_start (GST_OBJECT (self)));
       gst_element_call_async(GST_ELEMENT(self), do_async_state_transition, pStateChange, NULL);
       ret = GST_STATE_CHANGE_ASYNC;
    }
    break;

    case GST_STATE_CHANGE_READY_TO_NULL:
    {
       generic_data_exchanger_send(self->priv->pGenericDataExchanger,
                                   TRANSFER_CODE_READY_TO_NULL_NOTIFY,
                                   NULL,
                                   FALSE);
    }
    case GST_STATE_CHANGE_NULL_TO_NULL:
    {
       deinit_comm_objects(self);
       ret = GST_STATE_CHANGE_SUCCESS;
    }
    break;
    default:
    break;
   }

  return ret;
}

static void push_upstream (gpointer data, gpointer user_data)
{
   GstRemoteOffloadIngress *self = GST_REMOTEOFFLOAD_INGRESS (user_data);
   gboolean ret;

   if (GST_IS_EVENT (data))
   {
      GstEvent *event = GST_EVENT (data);
      gst_event_ref (event);
      ret = gst_pad_push_event (self->priv->sinkpad, event);
      event_data_exchanger_send_event_result(self->priv->pEventExchanger,
                                         event,
                                         ret);
      gst_event_unref (event);
   }
   else if(GST_IS_QUERY (data))
   {
      GstQuery *query = GST_QUERY (data);
      ret = gst_pad_peer_query (self->priv->sinkpad, query);
      query_data_exchanger_send_query_result(self->priv->pQueryExchanger,
                                         query,
                                         FALSE);
      gst_query_unref (query);
   }
   else
   {
      GST_ERROR_OBJECT (self, "Unsupported object type");
   }
}

static void QueryReceivedCallback(GstQuery *query, void *priv)
{
   GstRemoteOffloadIngress *ingress = (GstRemoteOffloadIngress *)priv;

   if( !GST_QUERY_IS_UPSTREAM (query) )
   {
      GST_WARNING_OBJECT(ingress, "Received a non-upstream query");
      query_data_exchanger_send_query_result(ingress->priv->pQueryExchanger,
                                         query,
                                         FALSE);
      gst_query_unref (query);
      return;
   }

   g_thread_pool_push (ingress->priv->upstream_push_threads, query, NULL);
}

static void EventReceivedCallback(GstEvent *event, void *priv)
{
   GstRemoteOffloadIngress *ingress = (GstRemoteOffloadIngress *)priv;

   if( !GST_EVENT_IS_UPSTREAM (event) )
   {
      GST_WARNING_OBJECT(ingress, "Received a non-upstream event");
      event_data_exchanger_send_event_result(ingress->priv->pEventExchanger,
                                         event,
                                         FALSE);
      gst_event_unref (event);
      return;
   }

   g_thread_pool_push (ingress->priv->upstream_push_threads, event, NULL);
}

static GArray *RequestQueueStats(void *priv)
{
   GstRemoteOffloadIngress *self = (GstRemoteOffloadIngress *)priv;

   return self->priv->queue_stats;
}

static gboolean GenericCallback(guint32 transfer_type, GArray *memblocks, void *priv)
{
   GstRemoteOffloadIngress *self = (GstRemoteOffloadIngress *)priv;

   gboolean ret = TRUE;
   switch(transfer_type)
   {
      case TRANSFER_CODE_EGRESS_STREAM_START:
         GST_INFO_OBJECT(self, "Received notification that egress streaming thread STARTED");
         g_mutex_lock(&self->priv->streamthreadsyncmutex);
         self->priv->begressstreamthreadrunning = TRUE;
         g_cond_broadcast (&self->priv->streamthreadsyncond);
         g_mutex_unlock(&self->priv->streamthreadsyncmutex);
      break;
      case TRANSFER_CODE_EGRESS_STREAM_STOP:
         GST_INFO_OBJECT(self, "Received notification that egress streaming thread STOPPED");
         g_mutex_lock(&self->priv->streamthreadsyncmutex);
         self->priv->begressstreamthreadrunning = FALSE;
         g_mutex_unlock(&self->priv->streamthreadsyncmutex);
      break;
      case TRANSFER_CODE_READY_TO_NULL_NOTIFY:
         GST_DEBUG_OBJECT(self, "Received notification that egress transitioned to NULL");
         remote_offload_comms_channel_cancel_all(self->priv->channel);
         break;
      default:
         GST_ERROR_OBJECT(self, "Unknown transfer_type");
         ret = FALSE;
      break;

   }

   return ret;
}

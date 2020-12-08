/*
 *  heartbeatdataexchanger.c - HeartBeatDataExchanger object
 *
 *  Copyright (C) 2020 Intel Corporation
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
#include "heartbeatdataexchanger.h"

//default time to wait in-between checking pulse
#define DEFAULT_HEARTBEAT_INTERVAL_MS 15000

enum
{
  PROP_CALLBACK = 1,
  N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

struct _HeartBeatDataExchanger
{
  RemoteOffloadDataExchanger parent_instance;

  /* Other members, including private data. */
  HeartBeatDataExchangerCallback *callback;

  GMutex statelock;
  GCond statecond;
  gboolean monitor_running;
  GThread *monitor_thread;

};

GST_DEBUG_CATEGORY_STATIC (heartbeat_data_exchanger_debug);
#define GST_CAT_DEFAULT heartbeat_data_exchanger_debug

G_DEFINE_TYPE_WITH_CODE (HeartBeatDataExchanger, heartbeat_data_exchanger, REMOTEOFFLOADDATAEXCHANGER_TYPE,
GST_DEBUG_CATEGORY_INIT (heartbeat_data_exchanger_debug, "remoteoffloadeosdataexchanger", 0,
  "debug category for remoteoffloadeosdataexchanger"))

gboolean heartbeat_data_exchanger_received(RemoteOffloadDataExchanger *exchanger,
                                      const GArray *segment_mem_array,
                                      guint64 response_id)
{
   if( !DATAEXCHANGER_IS_HEARTBEAT(exchanger))
      return FALSE;

   gboolean ret = TRUE;

   if( response_id )
   {
      ret = remote_offload_data_exchanger_write_response_single(exchanger,
                                                                NULL,
                                                                0,
                                                                response_id);

      if( !ret )
      {
         GST_ERROR_OBJECT(exchanger, "Error sending response");
      }
   }
   else
   {
      GST_ERROR_OBJECT(exchanger, "response_id is 0");
      ret = FALSE;
   }

   return ret;
}

static gpointer heartbeat_monitor_thread_routine(HeartBeatDataExchanger *hbexchanger)
{
   gboolean send_flatline_callback = FALSE;
   g_mutex_lock(&hbexchanger->statelock);
   while( 1 )
   {
      if( !hbexchanger->monitor_running )
         break;

      RemoteOffloadResponse *pResponse = remote_offload_response_new();
      if( remote_offload_data_exchanger_write_single((RemoteOffloadDataExchanger *)hbexchanger,
                                           NULL,
                                           0,
                                           pResponse) )
      {
          if( !remote_offload_response_wait(pResponse, DEFAULT_HEARTBEAT_INTERVAL_MS) == REMOTEOFFLOADRESPONSE_RECEIVED )
          {
             send_flatline_callback = TRUE;
          }
      }
      else
      {
         send_flatline_callback = TRUE;
      }
      g_object_unref(pResponse);

      if( send_flatline_callback )
         break;

      gint64 end_time =
            g_get_monotonic_time () + DEFAULT_HEARTBEAT_INTERVAL_MS*1000;

      //return true if statecond was explicitly triggered,
      // which is done within finalize to stop this thread.
      if( g_cond_wait_until (&hbexchanger->statecond,
                             &hbexchanger->statelock,
                             end_time) )
      {
         break;
      }
   }
   g_mutex_unlock(&hbexchanger->statelock);

   if( send_flatline_callback  )
   {
      if( hbexchanger->callback && hbexchanger->callback->flatline )
      {
         hbexchanger->callback->flatline(hbexchanger->callback->priv);
      }
      else
      {
         GST_WARNING_OBJECT(hbexchanger, "Flatline detected, but callback not set.");
      }
   }

   return NULL;
}

gboolean heartbeat_data_exchanger_start_monitor(HeartBeatDataExchanger *hbexchanger)
{
   if( !DATAEXCHANGER_IS_HEARTBEAT(hbexchanger) ) return FALSE;

   gboolean ret = TRUE;
   g_mutex_lock(&hbexchanger->statelock);
   if( !hbexchanger->monitor_running )
   {
      hbexchanger->monitor_running = TRUE;
      hbexchanger->monitor_thread =
           g_thread_new ("heartbeatmonitor",
                         (GThreadFunc) heartbeat_monitor_thread_routine,
                         hbexchanger);

      if( !hbexchanger->monitor_thread )
      {
         GST_ERROR_OBJECT(hbexchanger, "Unable to start monitor thread");
         ret = FALSE;
      }
   }
   else
   {
      GST_ERROR_OBJECT(hbexchanger, "Heartbeat Data Exchanger is already monitoring");
      ret = FALSE;
   }
   g_mutex_unlock(&hbexchanger->statelock);

   return ret;
}

static void
heartbeat_data_exchanger_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  HeartBeatDataExchanger *self = DATAEXCHANGER_HEARTBEAT (object);
  switch (property_id)
  {
    case PROP_CALLBACK:
    {
       self->callback = g_value_get_pointer (value);
    }
    break;

    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
heartbeat_data_exchanger_finalize (GObject *object)
{
   HeartBeatDataExchanger *self = DATAEXCHANGER_HEARTBEAT (object);

   //stop the monitor thread
   if( self->monitor_thread )
   {
      g_mutex_lock(&self->statelock);
      self->monitor_running = FALSE;
      g_cond_broadcast (&self->statecond);
      g_mutex_unlock(&self->statelock);
      g_thread_join (self->monitor_thread);
   }

   g_mutex_clear(&self->statelock);
   g_cond_clear(&self->statecond);

   G_OBJECT_CLASS (heartbeat_data_exchanger_parent_class)->finalize (object);
}

static void
heartbeat_data_exchanger_class_init (HeartBeatDataExchangerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  RemoteOffloadDataExchangerClass *parent_class = REMOTEOFFLOAD_DATAEXCHANGER_CLASS(klass);

  object_class->set_property = heartbeat_data_exchanger_set_property;
  obj_properties[PROP_CALLBACK] =
    g_param_spec_pointer ("callback",
                         "Callback",
                         "Received Callback",
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);

  g_object_class_install_properties (object_class,
                                     N_PROPERTIES,
                                     obj_properties);

  object_class->finalize = heartbeat_data_exchanger_finalize;
  parent_class->received = heartbeat_data_exchanger_received;
}

static void
heartbeat_data_exchanger_init (HeartBeatDataExchanger *self)
{
  self->callback = NULL;
  self->monitor_running = FALSE;
  self->monitor_thread = NULL;
  g_mutex_init(&self->statelock);
  g_cond_init(&self->statecond);
}

HeartBeatDataExchanger *heartbeat_data_exchanger_new (RemoteOffloadCommsChannel *channel,
                                          HeartBeatDataExchangerCallback *pcallback)
{
   HeartBeatDataExchanger *pexchanger =
        g_object_new(HEARTBEATDATAEXCHANGER_TYPE,
                     "callback", pcallback,
                     "commschannel", channel,
                     "exchangername", "xlink.exchanger.heartbeat",
                     NULL);

   return pexchanger;
}

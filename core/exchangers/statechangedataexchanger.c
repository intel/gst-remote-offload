/*
 *  statechangedataexchanger.c - StateChangeDataExchanger object
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
#include "statechangedataexchanger.h"

enum
{
  PROP_CALLBACK = 1,
  N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

struct _StateChangeDataExchanger
{
  RemoteOffloadDataExchanger parent_instance;

  /* Other members, including private data. */
  StateChangeDataExchangerCallback *callback;

  GMutex statechangemutex;
  GCond  statechangecond;
  gboolean bstatechangeupdate;
  gboolean bwaitcancelled;
  GstStateChange remoteLastTransition;

};

GST_DEBUG_CATEGORY_STATIC (statechange_data_exchanger_debug);
#define GST_CAT_DEFAULT statechange_data_exchanger_debug

G_DEFINE_TYPE_WITH_CODE (StateChangeDataExchanger,
                         statechange_data_exchanger, REMOTEOFFLOADDATAEXCHANGER_TYPE,
GST_DEBUG_CATEGORY_INIT (statechange_data_exchanger_debug,
                         "remoteoffloadstatechangedataexchanger", 0,
                         "debug category for remoteoffloadstatechangeexchanger"))

gboolean statechange_data_exchanger_received(RemoteOffloadDataExchanger *exchanger,
                                             const GArray *segment_mem_array,
                                             guint64 response_id)
{
   if( !segment_mem_array ||
       (segment_mem_array->len != 1) ||
       !DATAEXCHANGER_IS_STATECHANGE(exchanger))
      return FALSE;

   gboolean ret = TRUE;

   StateChangeDataExchanger *self = DATAEXCHANGER_STATECHANGE(exchanger);

   GstMemory **gstmemarray = (GstMemory **)segment_mem_array->data;

   GstMapInfo dataSegmentMap;
   if( !gst_memory_map (gstmemarray[0], &dataSegmentMap, GST_MAP_READ) )
   {
      GST_ERROR_OBJECT (exchanger, "Error mapping data segment for reading.");
      return FALSE;
   }

   if( dataSegmentMap.size != sizeof(GstStateChange) )
   {
     ret = FALSE;
   }
   else
   {
      GstStateChange *pStateChange =  (GstStateChange *)dataSegmentMap.data;

      GstState remoteOld = GST_STATE_TRANSITION_CURRENT(*pStateChange);
      GstState remoteNew = GST_STATE_TRANSITION_NEXT(*pStateChange);

      GST_INFO_OBJECT(self, "Received State Change = %s->%s",
                       gst_element_state_get_name(remoteOld),
                       gst_element_state_get_name(remoteNew));

      GstStateChangeReturn changeStateRet = GST_STATE_CHANGE_SUCCESS;
      if( self->callback && self->callback->statechange_received )
      {
         changeStateRet = self->callback->statechange_received(*pStateChange, self->callback->priv);
      }

      if( response_id )
      {
         remote_offload_data_exchanger_write_response_single(exchanger,
                                                             (guint8 *)&changeStateRet,
                                                             sizeof(changeStateRet),
                                                             response_id);
      }

      //signal wake-up of thread that might be waiting on state-change
      // within wait_for_state_change.
      g_mutex_lock(&self->statechangemutex);
      self->remoteLastTransition = *pStateChange;
      self->bstatechangeupdate = TRUE;
      g_cond_broadcast(&self->statechangecond);
      g_mutex_unlock(&self->statechangemutex);

      ret = TRUE;
   }

   gst_memory_unmap(gstmemarray[0], &dataSegmentMap);

   return ret;
}

GstStateChangeReturn statechange_data_exchanger_send_statechange(StateChangeDataExchanger *exchanger,
                                                     GstStateChange stateChange)
{
   if( !DATAEXCHANGER_IS_STATECHANGE(exchanger) )
     return FALSE;

   GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
   RemoteOffloadResponse *pResponse = remote_offload_response_new();
   if( remote_offload_data_exchanger_write_single((RemoteOffloadDataExchanger *)exchanger,
                                              (guint8 *)&stateChange,
                                              sizeof(GstStateChange),
                                              pResponse) )
   {
      if( remote_offload_response_wait(pResponse, 0) == REMOTEOFFLOADRESPONSE_RECEIVED )
      {
         if( !remote_offload_copy_response(pResponse, &ret, sizeof(ret), 0))
         {
              GST_ERROR_OBJECT (exchanger, "remote_offload_copy_response failed");
         }
      }
      else
      {
         GST_ERROR_OBJECT (exchanger, "remote_offload_response_wait failed");
      }
   }
   g_object_unref(pResponse);

   return ret;
}

static void
statechange_data_exchanger_set_property (GObject      *object,
                                  guint         property_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  StateChangeDataExchanger *self = DATAEXCHANGER_STATECHANGE (object);
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
statechange_data_exchanger_class_init (StateChangeDataExchangerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  RemoteOffloadDataExchangerClass *parent_class = REMOTEOFFLOAD_DATAEXCHANGER_CLASS(klass);

  object_class->set_property = statechange_data_exchanger_set_property;
  obj_properties[PROP_CALLBACK] =
    g_param_spec_pointer ("callback",
                         "Callback",
                         "Received Callback",
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);

  g_object_class_install_properties (object_class,
                                     N_PROPERTIES,
                                     obj_properties);

  parent_class->received = statechange_data_exchanger_received;
}

static void
statechange_data_exchanger_init (StateChangeDataExchanger *self)
{
  self->callback = NULL;
  g_mutex_init(&self->statechangemutex);
  g_cond_init(&self->statechangecond);
  self->remoteLastTransition = (GST_STATE_NULL<<3) | GST_STATE_NULL;
  self->bstatechangeupdate = FALSE;
  self->bwaitcancelled = FALSE;
}

//returns true if desired state change in the given direction was encountered
// false for failure (timeout, etc.)
gboolean wait_for_state_change(StateChangeDataExchanger *self,
                               GstStateChange stateChange,
                               gint64 timeoutmicroseconds)
{
   gboolean ret;

   if( !DATAEXCHANGER_IS_STATECHANGE(self) )
        return FALSE;

   g_mutex_lock(&self->statechangemutex);
   gboolean direction = GST_STATE_TRANSITION_NEXT(stateChange) >
                        GST_STATE_TRANSITION_CURRENT(stateChange);

   gint64 end_time = g_get_monotonic_time () + timeoutmicroseconds;
   while(1)
   {
      if( self->bstatechangeupdate )
      {
         self->bstatechangeupdate = FALSE;

         if( self->remoteLastTransition == stateChange )
         {
            ret = TRUE;
            break;
         }

         //if a state change was made in the opposite direction than the one we're waiting
         // on.
         gboolean last_direction = GST_STATE_TRANSITION_NEXT(self->remoteLastTransition) >
                                   GST_STATE_TRANSITION_CURRENT(self->remoteLastTransition);
         if( last_direction != direction )
         {
            GST_INFO_OBJECT(self, "state change detected in wrong direction");
            ret = FALSE;
            break;
         }

      }

      if( self->bwaitcancelled )
      {
         GST_INFO_OBJECT(self, "cancelled");
         ret = FALSE;
         break;
      }

      //wait for it
      if( !g_cond_wait_until (&self->statechangecond,
                              &self->statechangemutex,
                              end_time) )
      {
         GST_ERROR_OBJECT(self, "timeout");
         ret = FALSE;
         break;
      }
   }
   self->bwaitcancelled = FALSE;
   g_mutex_unlock(&self->statechangemutex);

   return ret;
}

void cancel_wait_for_state_change(StateChangeDataExchanger *self)
{
   if( !DATAEXCHANGER_IS_STATECHANGE(self) )
      return;

   g_mutex_lock(&self->statechangemutex);
   self->bwaitcancelled = TRUE;
   g_cond_broadcast(&self->statechangecond);
   g_mutex_unlock(&self->statechangemutex);
}

StateChangeDataExchanger *
statechange_data_exchanger_new (RemoteOffloadCommsChannel *channel,
                                StateChangeDataExchangerCallback *callback)
{
   StateChangeDataExchanger *pexchanger =
        g_object_new(STATECHANGEDATAEXCHANGER_TYPE,
                     "callback", callback,
                     "commschannel", channel,
                     "exchangername", "xlink.exchanger.statechange",
                     NULL);

   return pexchanger;
}

/*
 *  gstremoteoffloadpipeline.c - RemoteOffloadPipeline object
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

#include <gst/gst.h>
#include "gstremoteoffloadpipeline.h"
#include "remoteoffloadbinpipelinecommon.h"
#include "remoteoffloadcommsio.h"
#include "remoteoffloadcomms.h"
#include "remoteoffloadcommschannel.h"
#include "statechangedataexchanger.h"
#include "errormessagedataexchanger.h"
#include "eosdataexchanger.h"
#include "pingdataexchanger.h"
#include "genericdataexchanger.h"
#include "heartbeatdataexchanger.h"
#include "remoteoffloadbinserializer.h"
#include "remoteoffloadpipelinelogger.h"
#include "remoteoffloaddevice.h"
#include "remoteoffloadutils.h"

enum
{
  PROP_DEVICE = 1,
  PROP_IDTOCOMMSCHANNELHASH = 2,
  N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

/* Private structure definition. */
typedef struct {
   gboolean is_state_okay;

   GstElement *pPipeline;
   GMainLoop *pMainLoop;

   GstBin *pBin;

   gboolean bconnection_cut;

   RemoteOffloadDevice *device;

   RemoteOffloadCommsChannel *pDefaultCommsChannel;

   GHashTable *id_to_channel_hash;

   StateChangeDataExchangerCallback statechangeCallback;
   StateChangeDataExchanger *pStateChangeExchanger;

   ErrorMessageDataExchangerCallback errormessageCallback;
   ErrorMessageDataExchanger *pErrorMessageExchanger;

   EOSDataExchanger *pEOSExchanger;
   PingDataExchanger *pPingExchanger;

   HeartBeatDataExchangerCallback hearbeatCallback;
   HeartBeatDataExchanger *pHeartBeatExchanger;


   GenericDataExchangerCallback genericDataExchangerCallback;
   GenericDataExchanger *pGenericDataExchanger;

   RemoteOffloadBinSerializer *pBinSerializer;
   GMutex rop_state_mutex;
   GCond  waitcond;
   gboolean deserializationReceived;
   gboolean deserializationOK;

   gboolean instanceparamsReceived;
   gboolean instanceparamsOK;

   gint32 logmode;
   gchar *gst_debug;
   RemoteOffloadPipelineLogger *logger;

}RemoteOffloadPipelinePrivate;

struct _RemoteOffloadPipeline
{
  GObject parent_instance;

  RemoteOffloadPipelinePrivate priv;
};

GST_DEBUG_CATEGORY_STATIC (gst_remoteoffload_pipeline_debug);
#define GST_CAT_DEFAULT gst_remoteoffload_pipeline_debug

G_DEFINE_TYPE_WITH_CODE(RemoteOffloadPipeline, remote_offload_pipeline, G_TYPE_OBJECT,
GST_DEBUG_CATEGORY_INIT (gst_remoteoffload_pipeline_debug, "remoteoffloadpipeline", 0,
  "debug category for remoteoffloadpipeline"))

static GstStateChangeReturn StateChangeCallback(GstStateChange stateChange, void *priv);
static void ErrorMessageCallback(gchar *message, void *priv);
static gboolean GenericCallback(guint32 transfer_type, GArray *memblocks, void *priv);
static gboolean BusMessage(GstBus * bus, GstMessage * message, gpointer *priv);
static void HeartBeatFlatlineCallback(void *priv);

static void
remote_offload_pipeline_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  RemoteOffloadPipeline *self = REMOTEOFFLOAD_PIPELINE(object);
  switch (property_id)
  {
    case PROP_DEVICE:
    {
      RemoteOffloadDevice *device = g_value_get_pointer (value);
      if( device )
      {
         self->priv.device = g_object_ref(device);
      }
    }
    break;

    case PROP_IDTOCOMMSCHANNELHASH:
    {
      GHashTable *tmpHash = g_value_get_pointer (value);
      if( tmpHash )
      {
         self->priv.id_to_channel_hash = g_hash_table_ref(tmpHash);
      }
    }
    break;

    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
remote_offload_pipeline_comms_failure(RemoteOffloadPipeline *self)
{
   g_mutex_lock(&self->priv.rop_state_mutex);
   //if we haven't already pushed a fatal communications error
   // to the bus, then go ahead and do that.
   if( !self->priv.bconnection_cut )
   {
      self->priv.bconnection_cut = TRUE;

      if( self->priv.pPipeline )
         GST_ELEMENT_ERROR (self->priv.pPipeline, RESOURCE, FAILED, ("FATAL COMMS ERROR"), (NULL));

      //it's possible that a comms issue was detected really early on, while
      // we're waiting on instance params, or waiting to receive the serialized bin.
      // In this case, we need to wake up that wait operation, so that it can return.
      g_cond_broadcast (&self->priv.waitcond);
   }
   g_mutex_unlock(&self->priv.rop_state_mutex);
}

static void
remote_offload_pipeline_channel_comms_failure(RemoteOffloadCommsChannel *channel,
                                             void *user_data)
{
   RemoteOffloadPipeline *self = REMOTEOFFLOAD_PIPELINE(user_data);
   remote_offload_pipeline_comms_failure(self);
}

//This is called (automatically) right after _init() and set_properties calls for default
// construct props passed into g_object_new()
static void remote_offload_pipeline_constructed(GObject *object)
{
  RemoteOffloadPipeline *self = REMOTEOFFLOAD_PIPELINE(object);

  if( self->priv.id_to_channel_hash )
  {
     // Iterate through the hash table and make
     // sure that each value is indeed a comms channel, and
     // also register the read_failure callback for each
     gboolean hash_okay = TRUE;
     {
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init (&iter, self->priv.id_to_channel_hash);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
           gint id = GPOINTER_TO_INT(key);
           if( id < 0 )
           {
              GST_ERROR_OBJECT (self, "Invalid id-to-commschannel key(id) of %d", id);
              hash_okay = FALSE;
              break;
           }

           RemoteOffloadCommsChannel *channel = (RemoteOffloadCommsChannel *)value;
           if( !REMOTEOFFLOAD_IS_COMMSCHANNEL(channel))
           {
              GST_ERROR_OBJECT (self, "id-to-commschannel value %p for key(id)=%d is invalid",
                                channel, id);
              hash_okay = FALSE;
              break;
           }

           remote_offload_comms_channel_set_comms_failure_callback(channel,
                                                                  remote_offload_pipeline_channel_comms_failure,
                                                                  self);
        }
     }

     if( hash_okay )
     {
        self->priv.pDefaultCommsChannel = g_hash_table_lookup(self->priv.id_to_channel_hash,
                                                              GINT_TO_POINTER(0));

        if( self->priv.pDefaultCommsChannel )
        {

           self->priv.pStateChangeExchanger =
                 statechange_data_exchanger_new(self->priv.pDefaultCommsChannel,
                                                &self->priv.statechangeCallback);

           self->priv.pErrorMessageExchanger =
                 errormessage_data_exchanger_new(self->priv.pDefaultCommsChannel,
                                                 &self->priv.errormessageCallback);

           self->priv.pEOSExchanger =
                 eos_data_exchanger_new(self->priv.pDefaultCommsChannel, NULL);

           self->priv.pPingExchanger =
                 ping_data_exchanger_new(self->priv.pDefaultCommsChannel);

           self->priv.pHeartBeatExchanger =
                 heartbeat_data_exchanger_new(self->priv.pDefaultCommsChannel,
                                              &self->priv.hearbeatCallback);

           self->priv.pGenericDataExchanger =
                 generic_data_exchanger_new(self->priv.pDefaultCommsChannel,
                                            &self->priv.genericDataExchangerCallback);

           if( self->priv.pStateChangeExchanger &&
               self->priv.pErrorMessageExchanger &&
               self->priv.pEOSExchanger &&
               self->priv.pPingExchanger &&
               self->priv.pHeartBeatExchanger &&
               self->priv.pGenericDataExchanger)
           {
              self->priv.is_state_okay = TRUE;

              //sanity check on the (optional) device object
              if( self->priv.device )
              {
                 if( !REMOTEOFFLOAD_IS_DEVICE(self->priv.device) )
                 {
                    GST_ERROR_OBJECT (self, "Device object passed to constructor is an invalid device!");
                    self->priv.is_state_okay = FALSE;
                 }
              }
           }
        }
        else
        {
           GST_ERROR_OBJECT (self, "id-to-commschannel hash table contains no value for id=0");
        }
     }
  }
  else
  {
     GST_ERROR_OBJECT (self, "id-to-commschannel hash table not set");
  }

  G_OBJECT_CLASS (remote_offload_pipeline_parent_class)->constructed (object);
}

static void
remote_offload_pipeline_finalize (GObject *gobject)
{
  RemoteOffloadPipeline *self = REMOTEOFFLOAD_PIPELINE(gobject);

  if( self->priv.pPipeline )
    gst_object_unref (self->priv.pPipeline);

  if( self->priv.pMainLoop )
    g_main_loop_unref (self->priv.pMainLoop);

  g_object_unref(self->priv.pHeartBeatExchanger);
  g_object_unref(self->priv.pStateChangeExchanger);
  g_object_unref(self->priv.pErrorMessageExchanger);
  g_object_unref(self->priv.pEOSExchanger);
  g_object_unref(self->priv.pPingExchanger);
  g_object_unref(self->priv.pGenericDataExchanger);
  g_object_unref(self->priv.pBinSerializer);

  if( self->priv.gst_debug )
     g_free(self->priv.gst_debug);


  //unregister ourself from receiving failure callbacks,
  // as the life of commschannel's will continue, even after
  // our demise..
  if( self->priv.id_to_channel_hash )
  {
     GHashTableIter iter;
     gpointer key, value;
     g_hash_table_iter_init (&iter, self->priv.id_to_channel_hash);
     while (g_hash_table_iter_next (&iter, &key, &value))
     {
        RemoteOffloadCommsChannel *channel = (RemoteOffloadCommsChannel *)value;
        remote_offload_comms_channel_set_comms_failure_callback(channel,
                                                                NULL,
                                                                NULL);
        remote_offload_comms_channel_finish(channel);
     }
     g_hash_table_unref(self->priv.id_to_channel_hash);
  }

  if( self->priv.device )
  {
     g_object_unref(self->priv.device);
  }

  g_mutex_clear(&self->priv.rop_state_mutex);
  g_cond_clear(&self->priv.waitcond);

  G_OBJECT_CLASS (remote_offload_pipeline_parent_class)->finalize (gobject);
}

static void
remote_offload_pipeline_class_init (RemoteOffloadPipelineClass *klass)
{
   GObjectClass *object_class = G_OBJECT_CLASS (klass);

   object_class->set_property = remote_offload_pipeline_set_property;

   obj_properties[PROP_DEVICE] =
    g_param_spec_pointer ("device",
                          "Device",
                          "device object to receive customization callbacks",
                          G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);

   obj_properties[PROP_IDTOCOMMSCHANNELHASH] =
    g_param_spec_pointer ("idtocommschannelhash",
                         "IdToCommsChannelHash",
                         "gint32 to RemoteOffloadCommsChannel GHashTable",
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);



   g_object_class_install_properties (object_class,
                                     N_PROPERTIES,
                                     obj_properties);


   object_class->constructed = remote_offload_pipeline_constructed;
   object_class->finalize = remote_offload_pipeline_finalize;
}

static void
remote_offload_pipeline_init (RemoteOffloadPipeline *self)
{
  self->priv.device = NULL;
  self->priv.id_to_channel_hash = NULL;
  self->priv.pDefaultCommsChannel = NULL;

  self->priv.is_state_okay = FALSE;

  self->priv.pPipeline = NULL;
  self->priv.pMainLoop = NULL;

  self->priv.pBin = NULL;

  self->priv.pStateChangeExchanger = NULL;
  self->priv.statechangeCallback.statechange_received = StateChangeCallback;
  self->priv.statechangeCallback.priv = self;

  self->priv.pErrorMessageExchanger = NULL;
  self->priv.errormessageCallback.errormessage_received = ErrorMessageCallback;
  self->priv.errormessageCallback.priv = self;

  self->priv.pEOSExchanger = NULL;
  self->priv.pPingExchanger = NULL;

  self->priv.hearbeatCallback.flatline = HeartBeatFlatlineCallback;
  self->priv.hearbeatCallback.priv = self;
  self->priv.pHeartBeatExchanger = NULL;

  self->priv.genericDataExchangerCallback.received = GenericCallback;
  self->priv.genericDataExchangerCallback.priv = self;
  self->priv.pGenericDataExchanger = NULL;

  self->priv.pBinSerializer = remote_offload_bin_serializer_new();
  self->priv.deserializationOK = FALSE;
  self->priv.deserializationReceived = FALSE;
  g_mutex_init(&self->priv.rop_state_mutex);
  g_cond_init(&self->priv.waitcond);

  self->priv.bconnection_cut = FALSE;

  self->priv.instanceparamsReceived = FALSE;
  self->priv.instanceparamsOK = FALSE;

  self->priv.logmode = REMOTEOFFLOAD_LOG_DISABLED;
  self->priv.logger = NULL;
  self->priv.gst_debug = NULL;

}

RemoteOffloadPipeline *remote_offload_pipeline_new(RemoteOffloadDevice *device,
                                                   GHashTable *id_to_channel_hash)
{
  RemoteOffloadPipeline *pPipeline =
        g_object_new(REMOTEOFFLOADPIPELINE_TYPE,
                     "device", device,
                     "idtocommschannelhash", id_to_channel_hash, NULL);

  if( pPipeline )
  {
     if( !pPipeline->priv.is_state_okay )
     {
        g_object_unref(pPipeline);
        pPipeline = NULL;
     }
  }

  return pPipeline;
}

gboolean remote_offload_pipeline_run(RemoteOffloadPipeline *remoteoffloadpipeline)
{
   GST_INFO_OBJECT (remoteoffloadpipeline, "remote_offload_pipeline_run begin");

   if( !REMOTEOFFLOAD_IS_PIPELINE(remoteoffloadpipeline) )
      return FALSE;

   GST_DEBUG_OBJECT (remoteoffloadpipeline, "Sending ROP_READY notification to remoteoffloadbin");
   gboolean rop_ready_send_ok =
               generic_data_exchanger_send(remoteoffloadpipeline->priv.pGenericDataExchanger,
                                           BINPIPELINE_EXCHANGE_ROPREADY,
                                           NULL,
                                           TRUE);
   if( !rop_ready_send_ok )
   {
      GST_ERROR_OBJECT(remoteoffloadpipeline, "Error sending ROP_READY notification to remoteoffloadbin!");
      GST_INFO_OBJECT (remoteoffloadpipeline, "remote_offload_pipeline_run end");
      return FALSE;
   }

   //start the heartbeat monitor
   if( !heartbeat_data_exchanger_start_monitor(remoteoffloadpipeline->priv.pHeartBeatExchanger))
   {
      GST_WARNING_OBJECT(remoteoffloadpipeline, "Problem starting hearbeat monitor");
   }

   //wait for instance params
   g_mutex_lock (&remoteoffloadpipeline->priv.rop_state_mutex);
   if( !remoteoffloadpipeline->priv.instanceparamsReceived &&
       !remoteoffloadpipeline->priv.bconnection_cut )
   {
      //wait for it
      g_cond_wait (&remoteoffloadpipeline->priv.waitcond,
                   &remoteoffloadpipeline->priv.rop_state_mutex);
   }

   if( !remoteoffloadpipeline->priv.instanceparamsOK ||
       remoteoffloadpipeline->priv.bconnection_cut)
   {
      g_mutex_unlock (&remoteoffloadpipeline->priv.rop_state_mutex);
      GST_ERROR_OBJECT (remoteoffloadpipeline, "Error in instance params retrieval/processing");
      return FALSE;
   }
   g_mutex_unlock (&remoteoffloadpipeline->priv.rop_state_mutex);

   //Set up logging hooks
   if( remoteoffloadpipeline->priv.logmode != REMOTEOFFLOAD_LOG_DISABLED )
   {
      remoteoffloadpipeline->priv.logger = rop_logger_get_instance();
      rop_logger_register(remoteoffloadpipeline->priv.logger,
                          remoteoffloadpipeline->priv.pGenericDataExchanger,
                          REMOTEOFFLOAD_LOG_RING,
                          remoteoffloadpipeline->priv.gst_debug);
   }

   guint bus_watch_id;

   GMainContext *context = g_main_context_new();
   g_main_context_push_thread_default(context);
   remoteoffloadpipeline->priv.pMainLoop = g_main_loop_new (context, FALSE);

   GST_INFO_OBJECT (remoteoffloadpipeline, "Waiting for GstBin to be delivered from host..");

   // wait for the bin deserialization
   g_mutex_lock (&remoteoffloadpipeline->priv.rop_state_mutex);
   if( !remoteoffloadpipeline->priv.deserializationReceived &&
       !remoteoffloadpipeline->priv.bconnection_cut )
   {
      //wait for it
      g_cond_wait (&remoteoffloadpipeline->priv.waitcond,
                   &remoteoffloadpipeline->priv.rop_state_mutex);
   }

   if( !remoteoffloadpipeline->priv.deserializationOK ||
       remoteoffloadpipeline->priv.bconnection_cut )
   {
      g_mutex_unlock (&remoteoffloadpipeline->priv.rop_state_mutex);
      GST_ERROR_OBJECT (remoteoffloadpipeline, "Error in GstBin retrieval/deserialization");
      return FALSE;
   }

   GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (remoteoffloadpipeline->priv.pPipeline));
   bus_watch_id = gst_bus_add_watch (bus, (GstBusFunc) BusMessage, remoteoffloadpipeline);
   if( !bus_watch_id )
   {
      GST_WARNING_OBJECT (remoteoffloadpipeline, "bus %p already has an event source!", bus);
   }
   g_mutex_unlock (&remoteoffloadpipeline->priv.rop_state_mutex);

   //Start up the main loop.
   //The ROB will notify us when we should make the next state change transition.
   g_main_loop_run (remoteoffloadpipeline->priv.pMainLoop);

   GST_INFO_OBJECT (remoteoffloadpipeline, "Setting state of the pipeline to NULL");
   gst_element_set_state (remoteoffloadpipeline->priv.pPipeline, GST_STATE_NULL);

   gst_object_unref (GST_OBJECT (remoteoffloadpipeline->priv.pPipeline));
   remoteoffloadpipeline->priv.pPipeline = NULL;

   if( remoteoffloadpipeline->priv.logger )
   {
      rop_logger_unregister(remoteoffloadpipeline->priv.logger,
                            remoteoffloadpipeline->priv.pGenericDataExchanger);
      rop_logger_unref(remoteoffloadpipeline->priv.logger);
      remoteoffloadpipeline->priv.logger = NULL;
   }

   //Note: This is required to be the very last message exchanged between the ROB
   //       and this instance of ROP. After this, all comms objects on both host &
   //       remote side are destroyed.
   if(!statechange_data_exchanger_send_statechange(
         remoteoffloadpipeline->priv.pStateChangeExchanger,
         GST_STATE_CHANGE_READY_TO_NULL) )
   {
      //note, this is probably expected if we had a comms failure previously.
      GST_ERROR_OBJECT (remoteoffloadpipeline,
                        "statechange_data_exchanger_send_statechange(READY->NULL) failed");
   }

   gst_bus_remove_watch(bus);
   gst_object_unref (bus);

   g_main_context_pop_thread_default(context);
   g_main_context_unref (context);

   g_main_loop_unref (remoteoffloadpipeline->priv.pMainLoop);
   remoteoffloadpipeline->priv.pMainLoop = NULL;

   GST_INFO_OBJECT (remoteoffloadpipeline, "remote_offload_pipeline_run end");

   return TRUE;
}

static gboolean BinSerializationReceived(RemoteOffloadPipeline *self,
                                         GArray *memblocks)
{
   GST_INFO_OBJECT (self, "serialized bin received.");

   gboolean status_ok = FALSE;
   GArray *remoteconnectioncandidates = NULL;

   GstBin *pBin = remote_offload_deserialize_bin(self->priv.pBinSerializer ,
                                                 memblocks,
                                                 &remoteconnectioncandidates);


   GstElement *pipeline = gst_pipeline_new("offloadpipeline");
   if( pBin )
   {
      if( AssembleRemoteConnections(pBin,
                                    remoteconnectioncandidates,
                                    self->priv.id_to_channel_hash) )
      {

         if( gst_bin_add(GST_BIN(pipeline),
                           (GstElement *)pBin) )
         {
           // There is a special element, sublaunch, that complicates things here. We
           //  want our device objects to be able to find specific types of elements
           //  and potentially modify properties, etc. But, a user may have added one
           //  or more elements to the pipeline using the sublaunch element, which
           //  means they haven't been populated as 'real' elements yet.. as that
           //  happens, by default, within NULL->READY state transition. So, we
           //  use the signal 'populate-parent' to trigger sublaunch to early-convert
           //  it's launch string to "real" elements right now, while the pipeline is
           //  sitting in NULL state.
           {
              gchar * sublaunch_elems[] = {"sublaunch", NULL};
              GArray *sublaunch_element_array =
                    gst_bin_get_by_factory_type(GST_BIN(pipeline),
                                                sublaunch_elems);
              if( sublaunch_element_array )
              {
                 for(guint i = 0; i < sublaunch_element_array->len; i++ )
                 {
                    GstElement *sublaunch_element = g_array_index(sublaunch_element_array,
                                                                  GstElement *,
                                                                  i);
                    if( sublaunch_element )
                    {
                       gboolean bpop_return;
                       g_signal_emit_by_name(sublaunch_element, "populate-parent", &bpop_return);
                       if( !bpop_return )
                       {
                          GST_ERROR_OBJECT(self,
                            "sublaunch element, %s, had issues converting launch string to pipeline",
                            GST_ELEMENT_NAME(sublaunch_element));
                       }
                    }
                 }
                 g_array_free (sublaunch_element_array, TRUE);
              }
           }

           gboolean bdevice_modification_ok = TRUE;
           //Allow device-specific modifications to the pipeline
           // to take place before the NULL-to-READY state transition
           if( self->priv.device )
           {
              if( !remote_offload_device_pipeline_modify(self->priv.device, (GstPipeline*)pipeline) )
              {
                 GST_ERROR_OBJECT (self, "remote_offload_device_pipeline_modify failed");
                 bdevice_modification_ok = FALSE;
              }
           }

           if( bdevice_modification_ok )
           {
              if( gst_element_set_state (pipeline,
                                         GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS )
              {
                 status_ok = TRUE;
              }
              else
              {
                 GST_ERROR_OBJECT (self, "Error setting state of offloadpipeline to GST_STATE_READY");
              }
           }
         }
         else
         {
            GST_ERROR_OBJECT (self, "Error adding bin to offloadpipeline");
         }
      }
      else
      {
         GST_ERROR_OBJECT (self, "Error in AssembleRemoteConnections");
      }

      g_array_free(remoteconnectioncandidates, TRUE);

   }
   else
   {
      GST_ERROR_OBJECT (self, "Error deserializing bin");
   }

   //If the deserialization fails, ROB is expecting the return status to be the last
   // message received from ROP. So we need to make to to unregister / flush the log
   // contents back to the ROB before that happens.
   if( !status_ok && self->priv.logger)
   {
      rop_logger_unregister(self->priv.logger, self->priv.pGenericDataExchanger);
      rop_logger_unref(self->priv.logger);
      self->priv.logger = NULL;
   }

   g_mutex_lock (&self->priv.rop_state_mutex);
   self->priv.pBin = pBin;
   self->priv.pPipeline = pipeline;
   self->priv.deserializationReceived = TRUE;
   self->priv.deserializationOK = status_ok;
   g_cond_broadcast (&self->priv.waitcond);
   g_mutex_unlock (&self->priv.rop_state_mutex);

   return status_ok;
}

static GstStateChangeReturn StateChangeCallback(GstStateChange stateChange, void *priv)
{
   RemoteOffloadPipeline *self = (RemoteOffloadPipeline *)priv;

   GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
   GstState current = GST_STATE_TRANSITION_CURRENT(stateChange);
   GstState next = GST_STATE_TRANSITION_NEXT(stateChange);
   GST_INFO_OBJECT (self, "Received %s->%s notification from host.",
                    gst_element_state_get_name (current),
                    gst_element_state_get_name (next));

   if( stateChange == GST_STATE_CHANGE_READY_TO_NULL )
   {
      //Going to NULL is a special case. We close the main loop, and within
      //remote_offload_pipeline_run() we set the state of the pipeline to NULL.
      // We do this to keep the ROB alive (and comms open) until we know we are done cleaning
      // up, sending logs back, etc.
      GST_DEBUG_OBJECT (self, "Initiating the shutdown process "
                              "for this remote offload pipeline instance");
      g_main_loop_quit (self->priv.pMainLoop);
   }
   else
   {
      GST_INFO_OBJECT (self, "Setting state of the pipeline to %s",
                             gst_element_state_get_name (next));

      ret = gst_element_set_state (self->priv.pPipeline, next);

      if( ret == GST_STATE_CHANGE_FAILURE )
      {
         GST_ERROR_OBJECT(self, "%s->%s transition failed.",
                                gst_element_state_get_name (current),
                                gst_element_state_get_name (next));
      }
   }

   return ret;
}

static void ErrorMessageCallback(gchar *message, void *priv)
{
   RemoteOffloadPipeline *self = (RemoteOffloadPipeline *)priv;

   GST_ERROR_OBJECT (self, "Error message received from host pipeline %s\n", message);
}

static gboolean GenericCallback(guint32 transfer_type, GArray *memblocks, void *priv)
{
  RemoteOffloadPipeline *self = (RemoteOffloadPipeline *)priv;

  switch(transfer_type)
  {
     case BINPIPELINE_EXCHANGE_BINSERIALIZATION:
        return BinSerializationReceived(self, memblocks);
     break;

     case BINPIPELINE_EXCHANGE_ROPINSTANCEPARAMS:
     {
        self->priv.instanceparamsOK = FALSE;

        if( memblocks && (memblocks->len==1) )
        {
           GstMemory *instancemem =
                 g_array_index(memblocks,GstMemory *,0);

           if( instancemem )
           {
              GstMapInfo memmap;
              if( gst_memory_map (instancemem, &memmap, GST_MAP_READ) )
              {
                 if( memmap.size == sizeof(RemoteOffloadInstanceParams))
                 {
                    RemoteOffloadInstanceParams *params =
                          (RemoteOffloadInstanceParams *)memmap.data;

                    self->priv.logmode = params->logmode;
                    self->priv.instanceparamsOK = TRUE;
                    if( params->gst_debug_set )
                    {
                       self->priv.gst_debug = g_strdup(params->gst_debug);
                    }
                 }

                 gst_memory_unmap(instancemem, &memmap);
              }
           }
        }

        g_mutex_lock (&self->priv.rop_state_mutex);
        self->priv.instanceparamsReceived = TRUE;
        g_cond_broadcast (&self->priv.waitcond);
        g_mutex_unlock (&self->priv.rop_state_mutex);

        return self->priv.instanceparamsOK;
     }
     break;
  }

  return FALSE;
}

static gboolean BusMessage(GstBus * bus, GstMessage * message, gpointer *priv)
{
   RemoteOffloadPipeline *self = (RemoteOffloadPipeline *)priv;

   switch (GST_MESSAGE_TYPE (message))
   {
      case GST_MESSAGE_EOS:
         GST_INFO_OBJECT (self, "EOS Received. Sending notification to host");
         eos_data_exchanger_send_eos(self->priv.pEOSExchanger);
      break;

      case GST_MESSAGE_ERROR:
      {
         gchar  *debug;
         GError *error;

         gst_message_parse_error (message, &error, &debug);
         g_free (debug);


         g_mutex_lock(&self->priv.rop_state_mutex);
         gboolean bfatal_comms_error = self->priv.bconnection_cut;
         g_mutex_unlock(&self->priv.rop_state_mutex);

         GST_ERROR_OBJECT (self, "Received error message on bus: %s", error->message);
         if( !bfatal_comms_error )
         {
            //if this isn't a fatal communication issue, then just forward
            // the error to ROB, which will forward it on to the client-side
            // application to handle (probably they will invoke X->NULL state
            // transition)
            GST_INFO_OBJECT (self, "Forwarding this message to host");
            errormessage_data_exchanger_send_message(self->priv.pErrorMessageExchanger,
                                                     error->message);
         }
         else
         {
            //If we have detected a fatal comms error, any attempt to
            // inform ROB will fail (as our comms have been cut).
            // So, trigger a forced closure of ROP
            GST_ERROR_OBJECT(self, "ROP detected a FATAL COMMS error, so it will force-close.");
            //force-put all comms channels into cancelled state.
            if( self->priv.id_to_channel_hash )
            {
               GHashTableIter iter;
               gpointer key, value;
               g_hash_table_iter_init (&iter, self->priv.id_to_channel_hash);
               while (g_hash_table_iter_next (&iter, &key, &value))
               {
                  RemoteOffloadCommsChannel *channel = (RemoteOffloadCommsChannel *)value;
                  remote_offload_comms_channel_error_state(channel);
               }
            }

            //upon the main loop quitting, ROP's GstPipeline will be
            // set to NULL.
            g_main_loop_quit (self->priv.pMainLoop);
         }

         g_error_free (error);

      }
      break;

      case GST_MESSAGE_STATE_CHANGED:
      {
         GstState old_state, new_state;
         gst_message_parse_state_changed (message, &old_state, &new_state, NULL);
         if( (GstElement *)message->src == self->priv.pPipeline )
         {
            GST_INFO_OBJECT (self, "Remote Pipeline (%s)(%p) changed state from %s to %s.",
                             GST_OBJECT_NAME (message->src),
                             message->src,
                             gst_element_state_get_name (old_state),
                             gst_element_state_get_name (new_state));

            //Write bin dot file for debug
            if( old_state == GST_STATE_PAUSED && new_state == GST_STATE_PLAYING )
            {
               GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(self->priv.pPipeline), GST_DEBUG_GRAPH_SHOW_ALL, "remotepipeline");
            }
         }
         else
         {
            GST_DEBUG_OBJECT (self, "Element %s(%p) changed state from %s to %s.",
                              GST_OBJECT_NAME (message->src),
                              message->src,
                              gst_element_state_get_name (old_state),
                              gst_element_state_get_name (new_state));
         }
       }
       break;

       default:
       break;
   }

   return TRUE;
}

static void HeartBeatFlatlineCallback(void *priv)
{
   RemoteOffloadPipeline *self = (RemoteOffloadPipeline *)priv;
   GST_ERROR_OBJECT(self, "Heartbeat monitor detected flatline");
   remote_offload_pipeline_comms_failure(self);
}

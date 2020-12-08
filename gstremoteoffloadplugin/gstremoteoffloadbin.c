/*
 *  remoteoffloadbin.c - GstRemoteOffloadBin element
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

#include <gst/gst.h>
#include <stdio.h>
#include "gstremoteoffloadbin.h"
#include "remoteoffloadbinpipelinecommon.h"
#include "remoteoffloadcommschannel.h"
#include "statechangedataexchanger.h"
#include "errormessagedataexchanger.h"
#include "pingdataexchanger.h"
#include "eosdataexchanger.h"
#include "heartbeatdataexchanger.h"
#include "genericdataexchanger.h"
#include "remoteoffloadbinserializer.h"
#include "remoteoffloaddeviceproxy.h"
#include "remoteoffloadextregistry.h"


GST_DEBUG_CATEGORY_STATIC (gst_remoteoffload_bin_debug);
#define GST_CAT_DEFAULT gst_remoteoffload_bin_debug


/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_COMMS,
  PROP_COMMSPARAM,
  PROP_DEVICE,
  PROP_DEVICEPARAMS,
  PROP_REMOTE_GST_DEBUG,
  PROP_REMOTE_GST_DEBUG_LOCATION,
  PROP_REMOTE_GST_DEBUG_LOGMODE
};

#define REMOTEOFFLOAD_TYPE_LOGMODE (remoteoffload_logmode_get_type ())

static GType
remoteoffload_logmode_get_type(void)
{
   static GType remoteoffload_logmode_type = 0;
   static const GEnumValue remoteoffload_logmode[] =
   {
      {REMOTEOFFLOAD_LOG_RING,
       "Remote logging into ring buffer(s). Log contents are sent upon ring buffer being "
       "filled to capacity and/or closure of the remote offload pipeline (i.e. READY->NULL)",
       "ring"},
      {REMOTEOFFLOAD_LOG_IMMEDIATE,
       "For each log message received by the remote offload pipeline, it is immediately "
       "sent to the host (only recommended for extreme debugging scenarios)(not supported yet)",
       "immediate"},
      {0, NULL, NULL},
   };

   if (!remoteoffload_logmode_type)
   {
      remoteoffload_logmode_type =
            g_enum_register_static ("RemoteOffloadLogMode", remoteoffload_logmode);
   }

   return remoteoffload_logmode_type;
}


#define gst_remoteoffload_bin_parent_class parent_class

G_DEFINE_TYPE_WITH_CODE (GstRemoteOffloadBin, gst_remoteoffload_bin, GST_TYPE_BIN,
  GST_DEBUG_CATEGORY_INIT (gst_remoteoffload_bin_debug, "remoteoffloadbin", 0,
  "debug category for remoteoffloadbin"));

static void gst_remoteoffload_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_remoteoffload_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


/* GObject vmethod implementations */

static GstStateChangeReturn gst_remoteoffload_bin_change_state (GstElement *
    element, GstStateChange transition);

static gboolean gst_remoteoffload_bin_post_message(GstElement *element, GstMessage *message);
static gboolean gst_remoteoffload_bin_send_event (GstElement * element, GstEvent * event);
static void ErrorMessageCallback(gchar *message, void *priv);
static void EOSCallback(void *priv);
static gboolean GenericCallback(guint32 transfer_type,
                                GArray *memblocks,
                                 void *priv);
static void HeartBeatFlatlineCallback(void *priv);

typedef struct _GstRemoteOffloadBinExchangers
{
   ErrorMessageDataExchangerCallback m_errormessageCallback;
   ErrorMessageDataExchanger *m_pErrorMessageExchanger;

   StateChangeDataExchanger *m_pStateChangeExchanger;

   EOSDataExchangerCallback m_eosCallback;
   EOSDataExchanger *m_pEOSExchanger;
   PingDataExchanger *m_pPingExchanger;

   HeartBeatDataExchangerCallback m_hearbeatCallback;
   HeartBeatDataExchanger *m_pHeartBeatExchanger;

   GenericDataExchangerCallback m_genericDataExchangerCallback;
   GenericDataExchanger *m_pGenericDataExchanger;

}GstRemoteOffloadBinExchangers;

typedef struct _RemoteOffloadBinPrivate
{
   FILE *remotelogfile;
}RemoteOffloadBinPrivate;

static gboolean GstRemoteOffloadBinExchangers_init(GstRemoteOffloadBinExchangers *pExchangers,
                                               RemoteOffloadCommsChannel *pCommsChannel)
{
   pExchangers->m_pStateChangeExchanger =
         statechange_data_exchanger_new(pCommsChannel, NULL);
   pExchangers->m_pErrorMessageExchanger =
         errormessage_data_exchanger_new(pCommsChannel, &pExchangers->m_errormessageCallback);
   pExchangers->m_pEOSExchanger =
         eos_data_exchanger_new(pCommsChannel, &pExchangers->m_eosCallback);
   pExchangers->m_pPingExchanger =
         ping_data_exchanger_new(pCommsChannel);
   pExchangers->m_pHeartBeatExchanger =
         heartbeat_data_exchanger_new(pCommsChannel, &pExchangers->m_hearbeatCallback);
   pExchangers->m_pGenericDataExchanger =
         generic_data_exchanger_new(pCommsChannel, &pExchangers->m_genericDataExchangerCallback);

   if( pExchangers->m_pStateChangeExchanger &&
       pExchangers->m_pErrorMessageExchanger &&
       pExchangers->m_pEOSExchanger &&
       pExchangers->m_pPingExchanger &&
       pExchangers->m_pHeartBeatExchanger &&
       pExchangers->m_pGenericDataExchanger  )
   {
      return TRUE;
   }

   return FALSE;
}

static void GstRemoteOffloadBinExchangers_cleanup(GstRemoteOffloadBinExchangers *pExchangers)
{
   if( pExchangers->m_pStateChangeExchanger )
      g_object_unref(pExchangers->m_pStateChangeExchanger);
   if( pExchangers->m_pErrorMessageExchanger )
      g_object_unref(pExchangers->m_pErrorMessageExchanger);
   if( pExchangers->m_pEOSExchanger )
      g_object_unref(pExchangers->m_pEOSExchanger);
   if( pExchangers->m_pPingExchanger )
      g_object_unref(pExchangers->m_pPingExchanger);
   if( pExchangers->m_pHeartBeatExchanger )
      g_object_unref(pExchangers->m_pHeartBeatExchanger);
   if( pExchangers->m_pGenericDataExchanger )
      g_object_unref(pExchangers->m_pGenericDataExchanger);

   pExchangers->m_pStateChangeExchanger = NULL;
   pExchangers->m_pErrorMessageExchanger = NULL;
   pExchangers->m_pEOSExchanger = NULL;
   pExchangers->m_pPingExchanger = NULL;
   pExchangers->m_pHeartBeatExchanger = NULL;
   pExchangers->m_pGenericDataExchanger = NULL;
}

static void
gst_remoteoffload_bin_finalize (GObject *gobject)
{
   GstRemoteOffloadBin *remoteoffloadbin = GST_REMOTEOFFLOADBIN (gobject);

   if( remoteoffloadbin->pPrivate )
   {
      g_free(remoteoffloadbin->pPrivate);
   }

   g_mutex_clear (&remoteoffloadbin->rob_state_mutex);
   g_cond_clear (&remoteoffloadbin->cond);
   g_mutex_clear (&remoteoffloadbin->mutex);
   if( remoteoffloadbin->device )
      g_free(remoteoffloadbin->device);
   if( remoteoffloadbin->deviceparams )
      g_free(remoteoffloadbin->deviceparams);
   if( remoteoffloadbin->remotegstdebug )
      g_free(remoteoffloadbin->remotegstdebug);


   G_OBJECT_CLASS (gst_remoteoffload_bin_parent_class)->finalize (gobject);
}

#define DEFAULT_COMMS_METHOD "dummy"

/* initialize the gst_remoteoffload_bin class */
static void
gst_remoteoffload_bin_class_init (GstRemoteOffloadBinClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  //GstBinClass *gstbin_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  //gstbin_class = (GstBinClass *)klass;

  gobject_class->finalize = gst_remoteoffload_bin_finalize;
  gobject_class->set_property = gst_remoteoffload_bin_set_property;
  gobject_class->get_property = gst_remoteoffload_bin_get_property;

  g_object_class_install_property (gobject_class, PROP_COMMS,
      g_param_spec_string ("comms",
                           "Comms",
                           "String identifier for Comms method to use (deprecated -- use \"device\")",
                           DEFAULT_COMMS_METHOD,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_COMMSPARAM,
      g_param_spec_string ("commsparam",
                           "CommsParam",
                           "Parameters to pass for Comms method in use(deprecated -- use \"deviceparams\")",
                           " ",
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device",
                           "Device",
                           "String identifier for device to use",
                           DEFAULT_COMMS_METHOD,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_DEVICEPARAMS,
      g_param_spec_string ("deviceparams",
                           "DeviceParams",
                           "Device-specific parameters",
                           " ",
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_REMOTE_GST_DEBUG,
      g_param_spec_string ("remote-gst-debug",
                           "RemoteGstDebug",
                           "GST_DEBUG string to set for log collection on remote device",
                           "",
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_REMOTE_GST_DEBUG_LOCATION,
      g_param_spec_string ("remote-gst-debug-log-location",
                           "RemoteDebugLogLocation",
                           "file location to send remote log messages to. "
                           "Can be set to \"stdout\" to send messages to STDOUT, "
                           "or \"stderr\" to send messages to STDERR",
                           "",
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_REMOTE_GST_DEBUG_LOGMODE,
      g_param_spec_enum ("remote-gst-debug-logmode", "RemoteGstLogMode",
          "Setting to control the mode & frequency of logging from the remote device",
          REMOTEOFFLOAD_TYPE_LOGMODE, REMOTEOFFLOAD_LOG_RING,
          G_PARAM_READWRITE  | G_PARAM_STATIC_STRINGS));


  gst_element_class_set_details_simple(gstelement_class,
    "RemoteOffloadBin",
    "FIXME:Generic",
    "FIXME:Generic Template Element",
    "Ryan Metcalfe <<Ryan.D.Metcalfe@intel.com>>");

  gstelement_class->change_state =
        GST_DEBUG_FUNCPTR (gst_remoteoffload_bin_change_state);

  gstelement_class->post_message =
        GST_DEBUG_FUNCPTR (gst_remoteoffload_bin_post_message);

  gstelement_class->send_event =
        GST_DEBUG_FUNCPTR(gst_remoteoffload_bin_send_event);

}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_remoteoffload_bin_init (GstRemoteOffloadBin * remoteoffloadbin)
{
  g_mutex_init (&remoteoffloadbin->mutex);
  g_cond_init( &remoteoffloadbin->cond );
  remoteoffloadbin->deserializationstatus = FALSE;
  remoteoffloadbin->rop_ready = FALSE;
  remoteoffloadbin->bRemotePipelineEOS = FALSE;
  remoteoffloadbin->bThisEOS = FALSE;
  remoteoffloadbin->nIngress = 0;
  remoteoffloadbin->id_to_channel_hash = NULL;
  remoteoffloadbin->pDefaultCommsChannel = NULL;

  remoteoffloadbin->bconnection_cut = FALSE;
  g_mutex_init (&remoteoffloadbin->rob_state_mutex);

  remoteoffloadbin->pPrivate =
        (RemoteOffloadBinPrivate *)g_malloc(sizeof(RemoteOffloadBinPrivate));
  remoteoffloadbin->pPrivate->remotelogfile = NULL;

  remoteoffloadbin->pExchangers =
        (GstRemoteOffloadBinExchangers *)g_malloc(sizeof(GstRemoteOffloadBinExchangers));

  remoteoffloadbin->pExchangers->m_pStateChangeExchanger = NULL;

  remoteoffloadbin->pExchangers->m_errormessageCallback.errormessage_received =
        ErrorMessageCallback;
  remoteoffloadbin->pExchangers->m_errormessageCallback.priv = remoteoffloadbin;
  remoteoffloadbin->pExchangers->m_pErrorMessageExchanger = NULL;

  remoteoffloadbin->pExchangers->m_eosCallback.eos_received = EOSCallback;
  remoteoffloadbin->pExchangers->m_eosCallback.priv = remoteoffloadbin;
  remoteoffloadbin->pExchangers->m_pEOSExchanger = NULL;

  remoteoffloadbin->pExchangers->m_pPingExchanger = NULL;


  remoteoffloadbin->pExchangers->m_hearbeatCallback.flatline = HeartBeatFlatlineCallback;
  remoteoffloadbin->pExchangers->m_hearbeatCallback.priv = remoteoffloadbin;
  remoteoffloadbin->pExchangers->m_pHeartBeatExchanger = NULL;

  remoteoffloadbin->pExchangers->m_genericDataExchangerCallback.received = GenericCallback;
  remoteoffloadbin->pExchangers->m_genericDataExchangerCallback.priv = remoteoffloadbin;
  remoteoffloadbin->pExchangers->m_pGenericDataExchanger = NULL;

  gchar **env = g_get_environ();

  const gchar *envdevicestr = g_environ_getenv(env,"GST_REMOTEOFFLOAD_DEFAULT_DEVICE");
  if( envdevicestr )
  {
     remoteoffloadbin->device = g_strdup(envdevicestr);
  }
  else
  {
     const gchar *envcommsstr = g_environ_getenv(env,"GST_REMOTEOFFLOAD_DEFAULT_COMMS");
     if( envcommsstr )
     {
        GST_WARNING_OBJECT(remoteoffloadbin, "env. var. \"GST_REMOTEOFFLOAD_DEFAULT_COMMS\" is deprecated."
                                  " Please transition to use \"GST_REMOTEOFFLOAD_DEFAULT_DEVICE\" instead.");
        remoteoffloadbin->device = g_strdup(envcommsstr);
     }
     else
     {
        remoteoffloadbin->device = g_strdup(DEFAULT_COMMS_METHOD);
     }
  }

  const gchar *envdeviceparamsstr =
        g_environ_getenv(env,"GST_REMOTEOFFLOAD_DEFAULT_DEVICEPARAMS");
  if( envdeviceparamsstr )
  {
     remoteoffloadbin->deviceparams = g_strdup(envdeviceparamsstr);
  }
  else
  {
     const gchar *envcommsparamstr =
           g_environ_getenv(env,"GST_REMOTEOFFLOAD_DEFAULT_COMMSPARAM");
     if( envcommsparamstr )
     {
        GST_WARNING_OBJECT(remoteoffloadbin, "env. var. \"GST_REMOTEOFFLOAD_DEFAULT_COMMSPARAM\" is deprecated."
                                  " Please transition to use \"GST_REMOTEOFFLOAD_DEFAULT_DEVICEPARAMS\" instead.");
        remoteoffloadbin->deviceparams = g_strdup(envcommsparamstr);
     }
     else
     {
        remoteoffloadbin->deviceparams = g_strdup(" ");
     }
  }
  g_strfreev(env);

  remoteoffloadbin->remotegstdebug = NULL;
  remoteoffloadbin->remotegstdebuglocation = NULL;
  remoteoffloadbin->logmode = REMOTEOFFLOAD_LOG_RING;

  remoteoffloadbin->device_proxy_hash = NULL;

  remoteoffloadbin->ext_registry = NULL;
}

static void
gst_remoteoffload_bin_cleanup(GstRemoteOffloadBin *remoteoffloadbin)
{
  //cleanup
  GstRemoteOffloadBinExchangers_cleanup(remoteoffloadbin->pExchangers);

  if( remoteoffloadbin->pPrivate->remotelogfile &&
      (remoteoffloadbin->pPrivate->remotelogfile != stdout) &&
      (remoteoffloadbin->pPrivate->remotelogfile != stderr))
  {
     fflush(remoteoffloadbin->pPrivate->remotelogfile);
     fclose(remoteoffloadbin->pPrivate->remotelogfile);
     remoteoffloadbin->pPrivate->remotelogfile = NULL;
  }

  if( remoteoffloadbin->id_to_channel_hash )
  {
     //unregister from receiving failure callbacks
     GHashTableIter iter;
     gpointer key, value;
     g_hash_table_iter_init (&iter, remoteoffloadbin->id_to_channel_hash);
     while (g_hash_table_iter_next (&iter, &key, &value))
     {
        RemoteOffloadCommsChannel *channel = (RemoteOffloadCommsChannel *)value;
        remote_offload_comms_channel_set_comms_failure_callback(channel,
                                                                NULL,
                                                                NULL);
        remote_offload_comms_channel_finish(channel);
     }
     g_hash_table_unref(remoteoffloadbin->id_to_channel_hash);
     remoteoffloadbin->id_to_channel_hash = NULL;
  }

  remoteoffloadbin->pDefaultCommsChannel = NULL;

  remoteoffloadbin->deserializationstatus = FALSE;
  remoteoffloadbin->rop_ready = FALSE;

  if( remoteoffloadbin->device_proxy_hash )
  {
     g_hash_table_destroy(remoteoffloadbin->device_proxy_hash);
     remoteoffloadbin->device_proxy_hash = NULL;
  }

  if( remoteoffloadbin->ext_registry )
  {
     remote_offload_ext_registry_unref(remoteoffloadbin->ext_registry);
     remoteoffloadbin->ext_registry = NULL;
  }
}

static void
gst_remoteoffload_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRemoteOffloadBin *remoteoffloadbin = GST_REMOTEOFFLOADBIN (object);

  switch (prop_id) {

    case PROP_COMMS:
      if (!g_value_get_string (value)) {
        g_warning ("comms property cannot be NULL");
        break;
      }
      g_free (remoteoffloadbin->device);
      GST_WARNING_OBJECT(remoteoffloadbin, "\"comms\" property is deprecated. "
            "Please transition to use \"device\" property instead.");
      remoteoffloadbin->device = g_strdup (g_value_get_string (value));
      break;

    case PROP_COMMSPARAM:
      if (!g_value_get_string (value)) {
        g_warning ("commsparam property cannot be NULL");
        break;
      }
      g_free (remoteoffloadbin->deviceparams);
      GST_WARNING_OBJECT(remoteoffloadbin, "\"commsparam\" property is deprecated. "
            "Please transition to use \"deviceparams\" property instead.");
      remoteoffloadbin->deviceparams = g_strdup (g_value_get_string (value));
      break;

    case PROP_DEVICE:
      if (!g_value_get_string (value)) {
        g_warning ("device property cannot be NULL");
        break;
      }
      g_free (remoteoffloadbin->device);
      remoteoffloadbin->device = g_strdup (g_value_get_string (value));
      break;

    case PROP_DEVICEPARAMS:
      if (!g_value_get_string (value)) {
        g_warning ("deviceparams property cannot be NULL");
        break;
      }
      g_free (remoteoffloadbin->deviceparams);
      remoteoffloadbin->deviceparams = g_strdup (g_value_get_string (value));
      break;

    case PROP_REMOTE_GST_DEBUG:
      if (!g_value_get_string (value)) {
        g_warning ("remote-gst-debug property cannot be NULL");
        break;
      }
      if( remoteoffloadbin->remotegstdebug )
         g_free (remoteoffloadbin->remotegstdebug);
      remoteoffloadbin->remotegstdebug = g_strdup (g_value_get_string (value));
      break;

    case PROP_REMOTE_GST_DEBUG_LOCATION:
      if (!g_value_get_string (value)) {
        g_warning ("remote-gst-debug-location property cannot be NULL");
        break;
      }
      if( remoteoffloadbin->remotegstdebuglocation )
         g_free (remoteoffloadbin->remotegstdebuglocation);
      remoteoffloadbin->remotegstdebuglocation = g_strdup (g_value_get_string (value));
      break;

    case PROP_REMOTE_GST_DEBUG_LOGMODE:
      remoteoffloadbin->logmode = g_value_get_enum (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_remoteoffload_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRemoteOffloadBin *remoteoffloadbin = GST_REMOTEOFFLOADBIN (object);

  switch (prop_id)
  {
    case PROP_COMMS:
      g_value_set_string (value, remoteoffloadbin->device);
      break;
    case PROP_COMMSPARAM:
      g_value_set_string (value, remoteoffloadbin->deviceparams);
      break;
    case PROP_DEVICE:
      g_value_set_string (value, remoteoffloadbin->device);
      break;
    case PROP_DEVICEPARAMS:
      g_value_set_string (value, remoteoffloadbin->deviceparams);
      break;
    case PROP_REMOTE_GST_DEBUG:
      if( remoteoffloadbin->remotegstdebug )
         g_value_set_string (value, remoteoffloadbin->remotegstdebug);
      else
         g_value_set_string (value, "");
      break;
    case PROP_REMOTE_GST_DEBUG_LOCATION:
      g_value_set_string (value, remoteoffloadbin->remotegstdebuglocation);
      break;
    case PROP_REMOTE_GST_DEBUG_LOGMODE:
      g_value_set_enum (value, remoteoffloadbin->logmode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */
static inline GList *GetElementsInsideBin(GstBin *bin)
{
   GList *insideBinElementsList = NULL;
   {
      GstIterator *it = gst_bin_iterate_elements(bin);
      GValue item = G_VALUE_INIT;
      gboolean done = FALSE;
      while(!done)
      {
         switch(gst_iterator_next(it, &item))
         {
            case GST_ITERATOR_OK:
            {
               GstElement *pElem = (GstElement *)g_value_get_object(&item);
               if( !pElem ) return GST_STATE_CHANGE_FAILURE;
               insideBinElementsList =
                     g_list_prepend(insideBinElementsList, pElem);

            }
            break;

            case GST_ITERATOR_RESYNC:
            return GST_STATE_CHANGE_FAILURE;
            break;

            case GST_ITERATOR_ERROR:
            return GST_STATE_CHANGE_FAILURE;
            break;

            case GST_ITERATOR_DONE:
               done = TRUE;
            break;
         }
      }

      g_value_unset(&item);
      gst_iterator_free(it);
   }

   return insideBinElementsList;
}

static void KeyDestroyNotify(gpointer data)
{
  g_free(data);
}

static void ValueDestroyNotify(gpointer data)
{
  g_object_unref((GObject *)data);
}

static gboolean PopulateCommsChannelGeneratorHash(GstRemoteOffloadBin *remoteoffloadbin)
{
   remoteoffloadbin->ext_registry = remote_offload_ext_registry_get_instance();

   if( !remoteoffloadbin->ext_registry )
   {
      GST_ERROR_OBJECT (remoteoffloadbin, "Error obtaining remote offload extension registry");
      return FALSE;
   }

   GArray *channelgeneratorarray =
         remote_offload_ext_registry_generate(remoteoffloadbin->ext_registry,
                                              REMOTEOFFLOADDEVICEPROXY_TYPE);

   remoteoffloadbin->device_proxy_hash =
         g_hash_table_new_full(g_str_hash, g_str_equal, KeyDestroyNotify, ValueDestroyNotify);

   if( channelgeneratorarray )
   {
      for( guint i = 0;  i < channelgeneratorarray->len; i++ )
      {
         RemoteOffloadExtTypePair *pair =
               &g_array_index(channelgeneratorarray, RemoteOffloadExtTypePair, i);

         if( REMOTEOFFLOAD_IS_DEVICEPROXY(pair->obj) )
         {
            if( pair->name )
            {
               GST_INFO_OBJECT (remoteoffloadbin, "Registering support for device=%s ", pair->name);
               g_hash_table_insert(remoteoffloadbin->device_proxy_hash,
                                   g_strdup (pair->name),
                                   pair->obj);
            }
            else
            {
               GST_WARNING_OBJECT (remoteoffloadbin,
                                   "remote_offload_ext_registry returned "
                                   "channel generator name of NULL\n");
            }
         }
         else
         {
            GST_WARNING_OBJECT (remoteoffloadbin,
                                "remote_offload_ext_registry returned "
                                "invalid RemoteOffloadCommsChannelGenerator %p\n", pair->obj);
         }
      }

      g_array_free(channelgeneratorarray, TRUE);
   }

   return TRUE;
}

static gboolean gst_remoteoffload_bin_send_event (GstElement * element, GstEvent * event)
{

   //We override the default bin implementation here to stop it from
   // automatically routing events to src elements (only egress in
   // our case) for downstream events, and sink elements (only ingress
   // in our case) for upstream events. Also, the default bin implementation
   // will automatically route events to proxy pads set on this bin, which
   // is not what we want to happen (some of them aren't connected anymore).
   //TODO: This event should get forwarded to the ROP, and set at the
   // pipeline level there. Without doing that, for example, a seek
   // event won't work correctly on a pipeline in which all sink elements
   // reside within ROB.
   return TRUE;
}

static void
remoteoffload_bin_comms_failure(GstRemoteOffloadBin *self)
{
   g_mutex_lock(&self->rob_state_mutex);
   //if we haven't already pushed a fatal communications error
   // to the bus, then go ahead and do that.
   if( !self->bconnection_cut )
   {
      self->bconnection_cut = TRUE;
      //put all comms channels into cancelled state
      if( self->id_to_channel_hash )
      {
         GHashTableIter iter;
         gpointer key, value;
         g_hash_table_iter_init (&iter, self->id_to_channel_hash);
         while (g_hash_table_iter_next (&iter, &key, &value))
         {
            RemoteOffloadCommsChannel *channel = (RemoteOffloadCommsChannel *)value;
            remote_offload_comms_channel_error_state(channel);
         }
      }
      GST_ELEMENT_ERROR (self, RESOURCE, FAILED, ("FATAL COMMS ERROR"), (NULL));
   }
   g_mutex_unlock(&self->rob_state_mutex);
}

static void
remote_offload_pipeline_channel_comms_failure(RemoteOffloadCommsChannel *channel,
                                              void *user_data)
{
   GstRemoteOffloadBin *self = GST_REMOTEOFFLOADBIN(user_data);
   remoteoffload_bin_comms_failure(self);
}

static GstStateChangeReturn gst_remoteoffload_bin_change_state (GstElement *
    element, GstStateChange transition)
{
   GstRemoteOffloadBin *remoteoffloadbin = GST_REMOTEOFFLOADBIN (element);

   {
      GstState current = GST_STATE_TRANSITION_CURRENT(transition);
      GstState next = GST_STATE_TRANSITION_NEXT(transition);
      GST_INFO_OBJECT (remoteoffloadbin, "%s->%s",
                       gst_element_state_get_name (current),
                       gst_element_state_get_name (next));
   }

   GstStateChangeReturn remote_statechange_return = GST_STATE_CHANGE_SUCCESS;

   switch(transition)
   {
      case GST_STATE_CHANGE_NULL_TO_READY:
      {
         remoteoffloadbin->bThisEOS = FALSE;
         remoteoffloadbin->bRemotePipelineEOS = FALSE;
         remoteoffloadbin->bconnection_cut = FALSE;

         //Set up log file
         if( remoteoffloadbin->remotegstdebuglocation )
         {
            if( remoteoffloadbin->logmode == REMOTEOFFLOAD_LOG_RING )
            {
               if( !g_strcmp0(remoteoffloadbin->remotegstdebuglocation, "stderr") )
               {
                  remoteoffloadbin->pPrivate->remotelogfile = stderr;
               }
               else
               if( !g_strcmp0(remoteoffloadbin->remotegstdebuglocation, "stdout") )
               {
                  remoteoffloadbin->pPrivate->remotelogfile = stdout;
               }
               else
               {
                  remoteoffloadbin->pPrivate->remotelogfile =
                        fopen(remoteoffloadbin->remotegstdebuglocation, "wt");
                  if( !remoteoffloadbin->pPrivate->remotelogfile )
                  {
                     GST_WARNING_OBJECT(remoteoffloadbin,
                     "Could not open file specified by 'remote-gst-debug-log-location'(%s) for "
                     "writing. Not collecting logs..", remoteoffloadbin->remotegstdebuglocation);
                     remoteoffloadbin->logmode = REMOTEOFFLOAD_LOG_DISABLED;
                  }
               }
            }
            else
            {
               GST_WARNING_OBJECT(remoteoffloadbin,
                                "'remote-gst-debug-log-location' property set to \"immediate\""
                                " is not supported yet. Not collecting logs...");
            }
         }
         else
         {
            GST_INFO_OBJECT(remoteoffloadbin,
                               "'remote-gst-debug-log-location' property has not been set."
                               " Not collecting logs..");
            remoteoffloadbin->logmode = REMOTEOFFLOAD_LOG_DISABLED;
         }


         if( !PopulateCommsChannelGeneratorHash(remoteoffloadbin) )
         {
            GST_ERROR_OBJECT (remoteoffloadbin, "Error creating CommsChannelGenerators");
            return GST_STATE_CHANGE_FAILURE;
         }

         //Get the current list of elements within this bin
         // we'll use this later on
         GList *insideBinElementsList = GetElementsInsideBin(GST_BIN(remoteoffloadbin));

         //Using the RemoteOffloadBinSerializer,
         // serialize this bin (ourself) into a GArray of MemBlocks
         RemoteOffloadBinSerializer *binserializer = remote_offload_bin_serializer_new();

         GArray *memBlockArray;
         GArray *remoteconnectioncandidates = NULL;


         gboolean ret = remote_offload_serialize_bin(binserializer,
                                           GST_BIN(remoteoffloadbin),
                                           &memBlockArray,
                                           &remoteconnectioncandidates);
         gst_object_unref(binserializer);
         if( !ret )
         {
            GST_ERROR_OBJECT (remoteoffloadbin, "Error in remote_offload_serialize_bin");
            return GST_STATE_CHANGE_FAILURE;
         }

         //given the commsmethod set by the user, retrieve the comms channel generator
         RemoteOffloadDeviceProxy *proxy = (RemoteOffloadDeviceProxy *)
               g_hash_table_lookup(remoteoffloadbin->device_proxy_hash,
                                   remoteoffloadbin->device);

         if( !proxy )
         {
            GST_ERROR_OBJECT (remoteoffloadbin,
                              "Device Proxy not found for device=\"%s\"",
                              remoteoffloadbin->device);
            return GST_STATE_CHANGE_FAILURE;
         }


         //set the user arguments for this method
         if( !remote_offload_deviceproxy_set_arguments(proxy,
                                                       remoteoffloadbin->deviceparams) )
         {
            GST_ERROR_OBJECT (remoteoffloadbin,
                              "remote_offload_deviceproxy_set_arguments failed for "
                              "device \"%s\"", remoteoffloadbin->device);
            return GST_STATE_CHANGE_FAILURE;
         }

         GArray *comms_channel_request_array =
               g_array_new(FALSE, FALSE, sizeof(CommsChannelRequest));

         //create a request for the default channel
         {
            CommsChannelRequest request = {0};
            g_array_append_val(comms_channel_request_array, request);
         }

         //create a request for each remote connection candidate
         RemoteElementConnectionCandidate *connections =
               (RemoteElementConnectionCandidate *)remoteconnectioncandidates->data;
         for( guint connectioni = 0; connectioni < remoteconnectioncandidates->len; connectioni++ )
         {
            CommsChannelRequest request = {connections[connectioni].id};
            g_array_append_val(comms_channel_request_array, request);
         }

         //convert the pair array to a
         remoteoffloadbin->id_to_channel_hash =
               remote_offload_deviceproxy_generate(proxy,
                                                   GST_BIN(remoteoffloadbin),
                                                   comms_channel_request_array);

         g_array_free(comms_channel_request_array, TRUE);

         if( !remoteoffloadbin->id_to_channel_hash )
         {
            GST_ERROR_OBJECT (remoteoffloadbin, "remote_offload_deviceproxy_generate failed");
            return GST_STATE_CHANGE_FAILURE;
         }

         //register the comms-failure callback for each channel object
         {
            GHashTableIter iter;
            gpointer key, value;
            g_hash_table_iter_init (&iter, remoteoffloadbin->id_to_channel_hash);
            while (g_hash_table_iter_next (&iter, &key, &value))
            {
               RemoteOffloadCommsChannel *channel = (RemoteOffloadCommsChannel *)value;
               gint id = GPOINTER_TO_INT(key);
               if( id < 0 )
               {
                  GST_ERROR_OBJECT (remoteoffloadbin, "Invalid id-to-commschannel key(id) of %d", id);
                  return GST_STATE_CHANGE_FAILURE;
               }

               if( !REMOTEOFFLOAD_IS_COMMSCHANNEL(channel))
               {
                  GST_ERROR_OBJECT (remoteoffloadbin, "id-to-commschannel value %p for key(id)=%d is invalid",
                                    channel, id);
                  return GST_STATE_CHANGE_FAILURE;
               }

               remote_offload_comms_channel_set_comms_failure_callback(channel,
                                                   remote_offload_pipeline_channel_comms_failure,
                                                   remoteoffloadbin);
            }
         }

         //create a default channel
         remoteoffloadbin->pDefaultCommsChannel =
               g_hash_table_lookup(remoteoffloadbin->id_to_channel_hash, GINT_TO_POINTER(0));
         if( !remoteoffloadbin->pDefaultCommsChannel )
         {
            GST_ERROR_OBJECT (remoteoffloadbin, "Error creating default RemoteOffloadCommsChannel");
            return GST_STATE_CHANGE_FAILURE;
         }

         if( !GstRemoteOffloadBinExchangers_init(remoteoffloadbin->pExchangers,
                                            remoteoffloadbin->pDefaultCommsChannel) )
         {
            GST_ERROR_OBJECT (remoteoffloadbin, "Error in GstRemoteOffloadBinExchangers_init");
            return GST_STATE_CHANGE_FAILURE;
         }

         //Remove the elements within this bin
         GList *li;
         for(li = insideBinElementsList; li != NULL; li = li->next )
         {
            GstElement *pElement = (GstElement *)li->data;
            if( !gst_bin_remove (GST_BIN(remoteoffloadbin), pElement) )
            {
               GST_ERROR_OBJECT (remoteoffloadbin, "gst_bin_remove failed");
               return GST_STATE_CHANGE_FAILURE;
            }
         }

         g_list_free(insideBinElementsList);


         //For each of the remoteconnectioncandidates, create / add / link
         // remoteoffloadingress or remoteoffloadegress elements
         if( !AssembleRemoteConnections(GST_BIN(remoteoffloadbin),
                                        remoteconnectioncandidates,
                                        remoteoffloadbin->id_to_channel_hash) )
         {
            GST_ERROR_OBJECT (remoteoffloadbin, "AssembleRemoteConnections failed");
            return GST_STATE_CHANGE_FAILURE;
         }

         //Determine if we need to manually set sink flag
         {
            //count the number of ingress elements
            guint ningress = 0;
            for( guint i = 0; i < remoteconnectioncandidates->len; i++ )
            {
               RemoteElementConnectionCandidate *candidate =
                     &g_array_index(remoteconnectioncandidates,
                                    RemoteElementConnectionCandidate,
                                    i);

               if( gst_pad_get_direction(candidate->pad) == GST_PAD_SRC )
                  ningress++;
            }

            remoteoffloadbin->nIngress = ningress;

            //if we don't add any ingress (sink) elements,
            // then this bin (remoteoffloadbin) won't be
            // classified as a sink, therefore, the parent
            // won't wait on our EOS message. So.. force
            // ourselves to be classified as a sink.
            //IMPORTANT: This needs to be done AFTER removal of all
            // elements. If it's done before, the gst_bin_remove routine
            // will clear this flag if there are no child sink's left
            // in the bin.
            if( !remoteoffloadbin->nIngress )
            {
               GST_OBJECT_FLAG_SET (remoteoffloadbin, GST_ELEMENT_FLAG_SINK);
            }
         }

         g_array_free(remoteconnectioncandidates, TRUE);

         //The elements within the bin now should be only
         // remoteoffloadingress & remoteoffloadegress elements.
         // Set these to READY state before sending bin
         // to remote.
         insideBinElementsList = GetElementsInsideBin(GST_BIN(remoteoffloadbin));
         for(li = insideBinElementsList; li != NULL; li = li->next )
         {
            GstElement *pElement = (GstElement *)li->data;
            if( gst_element_set_state (pElement, GST_STATE_READY) != GST_STATE_CHANGE_SUCCESS )
            {
               GST_ERROR_OBJECT (remoteoffloadbin,
                                 "gst_element_set_state to GST_STATE_READY failed");
               return GST_STATE_CHANGE_FAILURE;
            }
         }
         g_list_free(insideBinElementsList);

         //Before sending the serialized bin, we need to wait for a signal
         // from the newly created ROP instance that it is ready. This is
         // to avoid race-conditions, like, sending the serialized bin
         // during ROP instance creation.
         g_mutex_lock(&remoteoffloadbin->mutex);
         if( !remoteoffloadbin->rop_ready )
         {
            //10 second timeout
            gint64 end_time = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
            if( !g_cond_wait_until(&remoteoffloadbin->cond,
                              &remoteoffloadbin->mutex,
                              end_time) )
            {
               GST_ERROR_OBJECT(remoteoffloadbin, "timeout waiting for ROP_READY");
            }
         }
         g_mutex_unlock(&remoteoffloadbin->mutex);

         gboolean remote_deserialization_ok = FALSE;
         if( remoteoffloadbin->rop_ready )
         {
            //start heartbeat monitor
            if( !heartbeat_data_exchanger_start_monitor(
                  remoteoffloadbin->pExchangers->m_pHeartBeatExchanger ) )
            {
               GST_WARNING_OBJECT(remoteoffloadbin, "Problem starting heartbeat monitor");
            }

            //send instance params
            RemoteOffloadInstanceParams *params = g_malloc0(sizeof(RemoteOffloadInstanceParams));
            if( G_LIKELY(params) )
            {
               params->logmode = remoteoffloadbin->logmode;
               if( g_snprintf(params->gst_debug,
                              ROP_INSTANCEPARAMS_GST_DEBUG_STRINGSIZE,
                              "%s",
                              remoteoffloadbin->remotegstdebug) < ROP_INSTANCEPARAMS_GST_DEBUG_STRINGSIZE )
               {
                  params->gst_debug_set = 1;
               }
               else
               {
                  GST_WARNING_OBJECT(remoteoffloadbin, "Currently set remote-gst-debug property is too big. "
                        " It must be a max of %d characters", ROP_INSTANCEPARAMS_GST_DEBUG_STRINGSIZE);
               }


               if( generic_data_exchanger_send_virt(
                                 remoteoffloadbin->pExchangers->m_pGenericDataExchanger,
                                 BINPIPELINE_EXCHANGE_ROPINSTANCEPARAMS,
                                 params,
                                 sizeof(RemoteOffloadInstanceParams),
                                 TRUE))
               {

                  //send the serialized bin
                  remote_deserialization_ok =
                        generic_data_exchanger_send(remoteoffloadbin->pExchangers->m_pGenericDataExchanger,
                                                    BINPIPELINE_EXCHANGE_BINSERIALIZATION,
                                                    memBlockArray,
                                                    TRUE);

                  if( !remote_deserialization_ok )
                     GST_ERROR_OBJECT (remoteoffloadbin,
                                    "generic_data_exchanger_send for serialized bin memblocks failed");
               }
               else
               {
                  GST_ERROR_OBJECT (remoteoffloadbin,
                                    "error sending instance params");
               }

               g_free(params);
            }
            else
            {
                 GST_ERROR_OBJECT (remoteoffloadbin,
                                    "error allocating instance params");
            }
         }

         GstMemory **memBlocks = (GstMemory **)memBlockArray->data;
         for( guint memblocki = 0; memblocki < memBlockArray->len; memblocki++ )
         {
            gst_memory_unref(memBlocks[memblocki]);
         }
         g_array_free(memBlockArray, TRUE);

         if( !remote_deserialization_ok )
         {
            return GST_STATE_CHANGE_FAILURE;
         }

         remoteoffloadbin->deserializationstatus = TRUE;
      }
      break;

      case GST_STATE_CHANGE_READY_TO_PAUSED:
      {
         //instruct ROP to transition to PAUSED state. Note that ROB doesn't explicitly wait
         // for ROP to transition to PAUSED state as each ingress will wait for their
         // matched egress to transition to PAUSED. This only kicks off that sequence.
         remote_statechange_return = statechange_data_exchanger_send_statechange(
               remoteoffloadbin->pExchangers->m_pStateChangeExchanger,
               GST_STATE_CHANGE_READY_TO_PAUSED);
      }
      break;

      case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      {
         //instruct ROP to transition to PLAYING state. Note that ROB doesn't explicitly wait
         // for ROP to transition to PLAYING state as each ingress will wait for their
         // matched egress to transition to PLAYING. This only kicks off that sequence.
         remote_statechange_return = statechange_data_exchanger_send_statechange(
               remoteoffloadbin->pExchangers->m_pStateChangeExchanger,
               GST_STATE_CHANGE_PAUSED_TO_PLAYING);

         if( remote_statechange_return == GST_STATE_CHANGE_FAILURE )
         {
            GST_ERROR_OBJECT (remoteoffloadbin, "Remote pipeline failed during PAUSED->PLAYING transition");
            return remote_statechange_return;
         }

         GstObject *parent = gst_element_get_parent(element);
         if( parent )
         {
            // if this is a pipeline (it should be).. dump the dot file
            if( GST_IS_PIPELINE(parent) )
            {
               GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(parent), GST_DEBUG_GRAPH_SHOW_ALL, "hostpipeline");
            }

            gst_object_unref(parent);
         }
      }
      break;

      case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      case GST_STATE_CHANGE_PAUSED_TO_READY:
      {
         //send this state change notification to the remote pipeline
         remote_statechange_return = statechange_data_exchanger_send_statechange(
                  remoteoffloadbin->pExchangers->m_pStateChangeExchanger,
                  transition);

         if( remote_statechange_return == GST_STATE_CHANGE_FAILURE )
         {
           GstState current = GST_STATE_TRANSITION_CURRENT(transition);
           GstState next = GST_STATE_TRANSITION_NEXT(transition);
           GST_ERROR_OBJECT (remoteoffloadbin, "Remote Pipeline State transition %s->%s FAILED",
                             gst_element_state_get_name (current),
                             gst_element_state_get_name (next));
         }
      }
      break;
      case GST_STATE_CHANGE_READY_TO_NULL:

         //if the deserialization failed during NULL->READY, the remote pipeline instance
         // is not connected to us anymore, so don't try to contact them further
         if( remoteoffloadbin->deserializationstatus )
         {
            //send this state change notification to the remote pipeline
            remote_statechange_return = statechange_data_exchanger_send_statechange(
                  remoteoffloadbin->pExchangers->m_pStateChangeExchanger,
                   transition);

            //wait for downward state transition to complete
            if( (remote_statechange_return!=GST_STATE_CHANGE_FAILURE) &&
                !wait_for_state_change(remoteoffloadbin->pExchangers->m_pStateChangeExchanger,
                                       transition,
                                       5 * G_TIME_SPAN_SECOND) )
            {
               GST_ERROR_OBJECT (remoteoffloadbin, "error in wait_for_state_change");
            }
         }

      break;
      default:
      break;
   }

   GstStateChangeReturn ret = GST_ELEMENT_CLASS (gst_remoteoffload_bin_parent_class)->change_state
         (element, transition);

   //This condition only would happen during READY->PAUSED state transition. If the remote pipeline
   // cannot produce data in PAUSED state (i.e. they returned NO_PREROLL to us), then we should
   // also return NO_PREROLL.
   if( (ret != GST_STATE_CHANGE_FAILURE) && (remote_statechange_return==GST_STATE_CHANGE_NO_PREROLL) )
   {
      ret = GST_STATE_CHANGE_NO_PREROLL;
   }

   //When transition is to NULL state, clean up everything.
   if( (transition == GST_STATE_CHANGE_READY_TO_NULL) || (transition==GST_STATE_CHANGE_NULL_TO_NULL))
      gst_remoteoffload_bin_cleanup(remoteoffloadbin);


   return ret;

}

static gboolean gst_remoteoffload_bin_post_message(GstElement *element, GstMessage *message)
{
    GstRemoteOffloadBin *remoteoffloadbin = GST_REMOTEOFFLOADBIN (element);

    switch (GST_MESSAGE_TYPE (message))
    {
       case GST_MESSAGE_EOS:
       {
        GST_INFO_OBJECT (remoteoffloadbin, "EOS (from host)");

        //We need to have EOS messages from 'this' bin, as well as the remote pipeline before
        // we can allow the 'EOS' message to be posted.
        gboolean posteos = FALSE;
        GST_OBJECT_LOCK (remoteoffloadbin);
        remoteoffloadbin->bThisEOS = TRUE;
        if( remoteoffloadbin->bRemotePipelineEOS )
        {
           remoteoffloadbin->bRemotePipelineEOS = FALSE;
           remoteoffloadbin->bThisEOS = FALSE;
           posteos = TRUE;
        }
        GST_OBJECT_UNLOCK (remoteoffloadbin);

        if( posteos )
        {
           return GST_ELEMENT_CLASS
                 (gst_remoteoffload_bin_parent_class)->post_message(element, message);
        }
        else
        {
           //we haven't received EOS notification from remoteoffloadpipeline yet, so
           // ignore this one.
           gst_message_unref(message);
           return TRUE;
        }
       }
       break;

       default:
          return GST_ELEMENT_CLASS (gst_remoteoffload_bin_parent_class)->post_message
                                                                   (element, message);
       break;

    }

}

static void ErrorMessageCallback(gchar *message, void *priv)
{
   GstRemoteOffloadBin * remoteoffloadbin = (GstRemoteOffloadBin *)priv;

   //post this message
   GST_ELEMENT_ERROR (remoteoffloadbin, RESOURCE, FAILED, ("Message from remote pipeline: %s",
            message), (NULL));
}

static void EOSCallback(void *priv)
{
   GstRemoteOffloadBin * remoteoffloadbin = (GstRemoteOffloadBin *)priv;

   //We need to have EOS messages from 'this' bin, as well as the remote pipeline before
   // we can allow the 'EOS' message to be posted.
   GST_INFO_OBJECT (remoteoffloadbin, "EOS (from remote)");
   gboolean posteos = FALSE;
   GST_OBJECT_LOCK (remoteoffloadbin);
   remoteoffloadbin->bRemotePipelineEOS = TRUE;
   if( remoteoffloadbin->bThisEOS || !remoteoffloadbin->nIngress)
   {
      remoteoffloadbin->bRemotePipelineEOS = FALSE;
      remoteoffloadbin->bThisEOS = FALSE;
      posteos = TRUE;
   }
   GST_OBJECT_UNLOCK (remoteoffloadbin);

   if( posteos )
   {
      GST_ELEMENT_CLASS (gst_remoteoffload_bin_parent_class)->post_message
          ((GstElement *)remoteoffloadbin, gst_message_new_eos((GstObject *)remoteoffloadbin));
   }
}

static gboolean GenericCallback(guint32 transfer_type,
                                GArray *memblocks,
                                 void *priv)
{
   GstRemoteOffloadBin * remoteoffloadbin = (GstRemoteOffloadBin *)priv;

   switch(transfer_type)
   {
      case BINPIPELINE_EXCHANGE_ROPREADY:
      {
         //set rop_ready flag to true and signal wakeup.
         g_mutex_lock(&remoteoffloadbin->mutex);
         remoteoffloadbin->rop_ready = TRUE;
         g_cond_broadcast (&remoteoffloadbin->cond);
         g_mutex_unlock(&remoteoffloadbin->mutex);
      }
      break;

      case BINPIPELINE_EXCHANGE_LOGMESSAGE:
      {
         if( remoteoffloadbin->pPrivate->remotelogfile && memblocks )
         {
            GstMemory **gstmemarray = (GstMemory **)memblocks->data;
            for( guint i = 0; i < memblocks->len; i++ )
            {
               GstMapInfo mapInfo;
               if( gst_memory_map (gstmemarray[i], &mapInfo, GST_MAP_READ) )
               {
                  if( mapInfo.size > 1 )
                  {
                     fwrite(mapInfo.data,
                            mapInfo.size-1,
                            1,
                            remoteoffloadbin->pPrivate->remotelogfile);
                  }
                  gst_memory_unmap(gstmemarray[i], &mapInfo);
               }
            }
         }
      }
      break;

      default:
         return FALSE;
      break;
   }

   return TRUE;
}

static void HeartBeatFlatlineCallback(void *priv)
{
   GstRemoteOffloadBin *remoteoffloadbin = (GstRemoteOffloadBin *)priv;
   GST_ERROR_OBJECT(remoteoffloadbin, "Heartbeat monitor detected flatline");
   remoteoffload_bin_comms_failure(remoteoffloadbin);
}


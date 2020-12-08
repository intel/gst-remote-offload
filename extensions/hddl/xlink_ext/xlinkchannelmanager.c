/*
 *  xlinkchannelmanager.c - XLinkChannelManager object
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
#include <string.h>
#include <stdint.h>
#include "xlinkchannelmanager.h"
#include "remoteoffloadcommsio_xlink.h"

#define NEGOTIATION_CHANNEL 2001 //Assume channels 1024 - 2000 are reserved.
#define REQUESTCHANNELS_STRING "xlink.manager.request.channels"
#define READ_BUF_SIZE 128


/* Private structure definition. */
typedef struct {

   gboolean is_state_okay;
   GMutex managermutex;
   GHashTable *swid_to_handler_map;
   GMutex channelhashmutex;
   GHashTable *channelsInUse;
   xlink_channel_id_t nextChannelToCheck;
   guint8 *read_buf;

}XLinkChannelManagerPrivate;

struct _XLinkChannelManager
{
  GObject parent_instance;

  /* Other members, including private data. */
  XLinkChannelManagerPrivate priv;
};

#define XLINKCHANNELMANAGER_TYPE (xlink_channel_manager_get_type ())
G_DECLARE_FINAL_TYPE (XLinkChannelManager, xlink_channel_manager, XLINK, CHANNELMANAGER, GObject)

GST_DEBUG_CATEGORY_STATIC (xlink_channel_manager_debug);
#define GST_CAT_DEFAULT xlink_channel_manager_debug

G_DEFINE_TYPE_WITH_CODE(XLinkChannelManager, xlink_channel_manager, G_TYPE_OBJECT,
GST_DEBUG_CATEGORY_INIT (xlink_channel_manager_debug, "remoteoffloadxlinkchannelmanager", 0,
                         "debug category for XLinkChannelManager"))

gboolean g_isInitialized = FALSE;

static struct xlink_handle *xlink_channel_manager_new_handle(guint32 sw_device_id)
{
   GST_INFO("Generating xlink_handle, given sw_device_id=0x%x", sw_device_id);
   struct xlink_handle *pHandle = g_malloc(sizeof(struct xlink_handle));
   memset(pHandle, 0, sizeof(struct xlink_handle));
   pHandle->dev_type = HOST_DEVICE;
   pHandle->sw_device_id = sw_device_id;

   enum xlink_error err = xlink_connect(pHandle);
   if( err != X_LINK_SUCCESS )
   {
      GST_ERROR("xlink_connect failed for sw_device_id=0x%x, err=%d", sw_device_id, err);
      g_free(pHandle);
      pHandle = NULL;
   }

   return pHandle;
}

//the manager mutex should be locked when calling this function.
static struct xlink_handle *xlink_channel_manager_get_handler(XLinkChannelManager *self,
                                                              guint32 sw_device_id)
{
   struct xlink_handle *pHandle = NULL;
   if( sw_device_id )
   {
      pHandle = g_hash_table_lookup(self->priv.swid_to_handler_map,
                                    GUINT_TO_POINTER(sw_device_id));
      if( !pHandle )
      {
         pHandle = xlink_channel_manager_new_handle(sw_device_id);
         if( pHandle )
         {
            g_hash_table_insert(self->priv.swid_to_handler_map,
                                GUINT_TO_POINTER(sw_device_id),
                                pHandle);
         }
      }
   }
   else
   {
      guint entries = g_hash_table_size(self->priv.swid_to_handler_map);
      if( entries )
      {
         GList *handler_list = g_hash_table_get_values(self->priv.swid_to_handler_map);
         if( handler_list )
         {
            if( handler_list->data )
               pHandle = (struct xlink_handle *)handler_list->data;
            else
               GST_ERROR_OBJECT(self, "Contained handler in list is NULL");

            g_list_free(handler_list);
         }
      }
      else
      {
         uint32_t num_devices = 0;
         uint32_t sw_device_id_list[MAX_DEVICE_LIST_SIZE];
         sw_device_id = INVALID_SW_DEVICE_ID;
         if( xlink_get_device_list(sw_device_id_list, &num_devices) == X_LINK_SUCCESS )
         {
            if( num_devices )
            {
               //Assign our SW Device ID to the first device that is PCIE
               gboolean found_pcie_device = FALSE;
               for (uint32_t i = 0; i < num_devices; i++)
               {
                  SWDeviceIdInterfaceType interface = GetInterfaceFromSWDeviceId(sw_device_id_list[i]);
                  if ( interface == SW_DEVICE_ID_PCIE_INTERFACE )
                  {
                     sw_device_id = sw_device_id_list[i];
                     found_pcie_device = TRUE;
                     break;
                  }
               }

               if( !found_pcie_device )
               {
                  GST_ERROR_OBJECT (self, "Couldn't find PCIE device from %u devices", num_devices);
               }
            }
            else
            {
               GST_ERROR_OBJECT (self, "xlink_get_device_list returned 0 devices!");
            }
         }

         if( sw_device_id != INVALID_SW_DEVICE_ID )
         {
            pHandle = xlink_channel_manager_new_handle(sw_device_id);
            if( pHandle )
            {
               g_hash_table_insert(self->priv.swid_to_handler_map,
                                   GUINT_TO_POINTER(sw_device_id),
                                   pHandle);
            }
         }
      }
   }

   return pHandle;
}

static void xlink_channel_manager_constructed(GObject *object)
{
  XLinkChannelManager *self = XLINK_CHANNELMANAGER(object);

  if( !g_isInitialized )
  {
     if( xlink_initialize() == X_LINK_SUCCESS )
     {
        g_isInitialized = TRUE;
     }
  }

  if( g_isInitialized )
  {
     self->priv.is_state_okay = TRUE;
  }
  else
  {
     GST_ERROR_OBJECT (self, "xlink_initialize failed");
  }

  G_OBJECT_CLASS (xlink_channel_manager_parent_class)->constructed (object);
}

static enum xlink_error xlink_channel_manager_open_default_channel(XLinkChannelManager *self,
                                                                   struct xlink_handle *pHandle)
{
   if( !self->priv.is_state_okay )
   {
      return X_LINK_ERROR;
   }

   //There is an internal 5 sec. timeout in xlink-core within
   // xlink_open_channel in which it will wait for the "other side"
   // to open this channel.
   // In the case of the xlink-server, we want to "listen"
   // on this channel for much longer than 5 seconds, which
   // forces us to perform the following loop.
   enum xlink_error open_sts;
   do
   {
      open_sts = xlink_open_channel(pHandle,
                                    NEGOTIATION_CHANNEL,
                                    RXB_TXB,
                                    XLINK_DATA_SIZE,
                                    XLINK_CHANNEL_TIMEOUT);
   } while( open_sts == X_LINK_TIMEOUT );

   return open_sts;
}

void xlink_channel_manager_close_default_channels(XLinkChannelManager *manager)
{
   GList *handler_list = g_hash_table_get_values(manager->priv.swid_to_handler_map);
   if( handler_list )
   {
      GList *handler_list_it = handler_list;
      while( handler_list_it != NULL )
      {
         if( handler_list_it->data )
         {
            struct xlink_handle *pHandle = (struct xlink_handle *)handler_list->data;
            xlink_close_channel(pHandle, NEGOTIATION_CHANNEL);
         }
         else
            GST_ERROR_OBJECT(manager, "Contained handler in list is NULL");

         handler_list_it = handler_list_it->next;
      }
      g_list_free(handler_list);
   }

}

static void
xlink_channel_manager_finalize (GObject *gobject)
{
  XLinkChannelManager *self = XLINK_CHANNELMANAGER(gobject);

  //FIXME: There current is a  race condition between the ROP thread
  // destruction (in which xlink commsio will be shutdown/unref'ed,
  // and this objectgetting destroyed. To keep things in a healthy state,
  // we need to make sure that all open channels have been closed before we
  // call disconnect. This is a pretty ugly method to ensure that.
  {
    guint nchannels_still_open;
    guint i = 0;
    do
    {
       if( i != 0 )
       {
          g_usleep(100000);
       }

       g_mutex_lock(&self->priv.channelhashmutex);
       nchannels_still_open = g_hash_table_size(self->priv.channelsInUse);
       g_mutex_unlock(&self->priv.channelhashmutex);

       if( nchannels_still_open && i==0 )
       {
          GST_WARNING_OBJECT (self, "Some XLink channels still open.. will wait up to 60s"
                                    " for them to get closed.");
       }

       i++;
    }while((nchannels_still_open != 0) && (i < 600));

    if( nchannels_still_open )
    {
       GST_WARNING_OBJECT (self, "Timeout expired (60s) waiting for XLink channels to close themselves");
    }
  }

  GList *handler_list = g_hash_table_get_values(self->priv.swid_to_handler_map);
  if( handler_list )
  {
      GList *handler_list_it = handler_list;
      while( handler_list_it != NULL )
      {
         if( handler_list_it->data )
         {
            struct xlink_handle *pHandle = (struct xlink_handle *)handler_list->data;
            xlink_disconnect(pHandle);
            g_free(pHandle);
         }
         else
            GST_ERROR_OBJECT(self, "Contained handler in list is NULL");

         handler_list_it = handler_list_it->next;
      }

      g_list_free(handler_list);
  }
  g_hash_table_destroy(self->priv.swid_to_handler_map);

  g_mutex_clear(&self->priv.channelhashmutex);
  g_mutex_clear(&(self->priv.managermutex));

  g_hash_table_destroy(self->priv.channelsInUse);

  g_free(self->priv.read_buf);

  G_OBJECT_CLASS (xlink_channel_manager_parent_class)->finalize (gobject);
}

static GObject *
xlink_channel_manager_constructor(GType type,
                                  guint n_construct_params,
                                  GObjectConstructParam *construct_params)
{
   static GObject *self = NULL;

   if( self == NULL )
   {
      self = G_OBJECT_CLASS (xlink_channel_manager_parent_class)->constructor(
          type, n_construct_params, construct_params);
      g_object_add_weak_pointer (self, (gpointer) &self);
      return self;
   }

  return g_object_ref (self);
}

static void
xlink_channel_manager_class_init (XLinkChannelManagerClass *klass)
{
   GObjectClass *object_class = G_OBJECT_CLASS (klass);

   object_class->constructor = xlink_channel_manager_constructor;
   object_class->constructed = xlink_channel_manager_constructed;
   object_class->finalize = xlink_channel_manager_finalize;
}

static void
xlink_channel_manager_init (XLinkChannelManager *self)
{
  self->priv.is_state_okay = FALSE;
  g_mutex_init(&(self->priv.managermutex));
  g_mutex_init(&self->priv.channelhashmutex);

  self->priv.channelsInUse = g_hash_table_new(g_direct_hash,  g_direct_equal);
  self->priv.read_buf = g_malloc(READ_BUF_SIZE);
  self->priv.nextChannelToCheck = NEGOTIATION_CHANNEL+1;
  self->priv.swid_to_handler_map = g_hash_table_new(g_direct_hash,  g_direct_equal);

}

static GMutex singletonlock;

XLinkChannelManager *xlink_channel_manager_get_instance()
{
   //only allow 1 thread to obtain an instance of XLinkChannelManager at a time.
   g_mutex_lock(&singletonlock);
   XLinkChannelManager *pChannelManager = g_object_new(XLINKCHANNELMANAGER_TYPE, NULL);
   if( pChannelManager )
   {
      if( !pChannelManager->priv.is_state_okay )
      {
         g_object_unref(pChannelManager);
         pChannelManager = NULL;
      }
   }
   g_mutex_unlock(&singletonlock);

   return pChannelManager;
}

void xlink_channel_manager_unref(XLinkChannelManager *manager)
{
   if( !XLINK_IS_CHANNELMANAGER(manager) ) return;

   g_mutex_lock(&singletonlock);
   g_object_unref(manager);
   g_mutex_unlock(&singletonlock);
}

//Remove the channelId from the "channelsInUse" hash
// (Put this channel back into the pool of available channels)
static void
xlink_commsio_channel_closed(xlink_channel_id_t channelId,
                             void *user_data)
{
   XLinkChannelManager *manager = (XLinkChannelManager *)user_data;
   if( !XLINK_IS_CHANNELMANAGER(manager) ) return;

   g_mutex_lock(&manager->priv.channelhashmutex);
   if( !g_hash_table_remove(manager->priv.channelsInUse,
                                GINT_TO_POINTER(channelId)))
   {
      GST_WARNING_OBJECT (manager, "Channel %d not found in map", channelId);
   }
   g_mutex_unlock(&manager->priv.channelhashmutex);
}


GArray *xlink_channel_manager_listen_channels(XLinkChannelManager *manager,
                                              guint32 sw_device_id)
{
   if( !XLINK_IS_CHANNELMANAGER(manager) )
      return NULL;

   if( !manager->priv.is_state_okay )
   {
      GST_ERROR_OBJECT (manager, "XLinkChannelManager in invalid state");
      return NULL;
   }

   //Don't allow multiple threads to be listening at the same time.
   g_mutex_lock(&manager->priv.managermutex);

   struct xlink_handle *pHandler = xlink_channel_manager_get_handler(manager,
                                                                    sw_device_id);

   if( !pHandler )
   {
      GST_ERROR_OBJECT (manager, "Unable to obtain an XLink handle");
      g_mutex_unlock(&manager->priv.managermutex);
      return NULL;
   }

   if( xlink_channel_manager_open_default_channel(manager, pHandler) != X_LINK_SUCCESS )
   {
      GST_ERROR_OBJECT (manager, "Error opening default negotiation channel");
      g_mutex_unlock(&manager->priv.managermutex);
      return NULL;
   }

   GArray *commsioarray = g_array_new(FALSE, FALSE, sizeof(RemoteOffloadCommsIO *));
   guint currentXLinkBufferSize = 0;

   guint nchannels;
   guint8 *pNChannels = (guint8*)&nchannels;
   XLinkChannelRequestParams request_params;
   guint8 *pRequestParams = (guint8*)&request_params;
   XLinkCommsIOTXMode tx_mode = XLINK_COMMSIO_TXMODE_NOPREFERENCE;
   XLinkCommsIOCoaleseMode coalesce_mode = XLINK_COMMSIO_COALESCEMODE_ENABLE;

   //for this first read, we don't want a timeout to get flagged as an issue.
   // This read-timeout-read-timeout loop is expected while the server is simply
   // listening/waiting for a client to connect.
   enum xlink_error status = X_LINK_TIMEOUT;
   while( status == X_LINK_TIMEOUT )
   {
      status = xlink_read_data(pHandler,
                       NEGOTIATION_CHANNEL,
                       &manager->priv.read_buf,
                       &currentXLinkBufferSize);
   }

   if( status != X_LINK_SUCCESS )
   {
      GST_ERROR_OBJECT (manager, "Error receiving request string");
      goto error;
   }

   if( currentXLinkBufferSize != (sizeof(REQUESTCHANNELS_STRING)) )
   {
      GST_ERROR_OBJECT (manager, "Incorrect request string");
      goto error;
   }

   if( g_strcmp0((gchar *)manager->priv.read_buf, REQUESTCHANNELS_STRING) != 0)
   {
      GST_ERROR_OBJECT (manager,
                        "Incorrect request channel string (%s)", (gchar *)manager->priv.read_buf);
      goto error;
   }

   xlink_release_data(pHandler,
                      NEGOTIATION_CHANNEL,
                      NULL);

   //Receive the request params
   status = xlink_read_data(pHandler,
                            NEGOTIATION_CHANNEL,
                            &pRequestParams,
                            &currentXLinkBufferSize);
   if( status != X_LINK_SUCCESS )
   {
      GST_ERROR_OBJECT (manager, "Error receiving request params. err=%d", status);
      goto error;
   }

   xlink_release_data(pHandler,
                      NEGOTIATION_CHANNEL,
                      NULL);

   switch(request_params.opMode)
   {
      case 0: tx_mode = XLINK_COMMSIO_TXMODE_NOPREFERENCE; break;
      case 1: tx_mode = XLINK_COMMSIO_TXMODE_BLOCKING; break;
      case 2: tx_mode = XLINK_COMMSIO_TXMODE_NONBLOCKING; break;
      default:
      break;
   }

   if( request_params.coalesceModeDisable )
   {
      coalesce_mode = XLINK_COMMSIO_COALESCEMODE_DISABLE;
   }
   else
   {
      coalesce_mode = XLINK_COMMSIO_COALESCEMODE_ENABLE;
   }

   //receive the number of channels that the client is requesting.
   status = xlink_read_data(pHandler,
                            NEGOTIATION_CHANNEL,
                            &pNChannels,
                            &currentXLinkBufferSize);
   if( status != X_LINK_SUCCESS )
   {
      GST_ERROR_OBJECT (manager, "Error receiving nchannels. err=%d", status);
      goto error;
   }

   //for each channel requested..
   for( guint i = 0; i < nchannels; i++ )
   {
      xlink_channel_id_t channel = 0;

      //find an available channel
      g_mutex_lock(&manager->priv.channelhashmutex);
      guint total_valid_channels = 0xFFE - NEGOTIATION_CHANNEL;
      for( int i = 0; i < total_valid_channels; i++ )
      {
         if( manager->priv.nextChannelToCheck > 0xFFE )
         {
            manager->priv.nextChannelToCheck = (NEGOTIATION_CHANNEL+1);
         }

         //WA: intel_hddl_client on device side will open channel 1080 by default.
         // So, skip this one.
         if( manager->priv.nextChannelToCheck == 1080 )
         {
            manager->priv.nextChannelToCheck++;
            continue;
         }

         if( !g_hash_table_contains(manager->priv.channelsInUse, GINT_TO_POINTER(manager->priv.nextChannelToCheck)) )
         {
            g_hash_table_insert(manager->priv.channelsInUse,
                                GINT_TO_POINTER(manager->priv.nextChannelToCheck), GINT_TO_POINTER(0x1));
            channel = manager->priv.nextChannelToCheck;
         }

         manager->priv.nextChannelToCheck++;

         if( channel )
            break;
      }
      g_mutex_unlock(&manager->priv.channelhashmutex);

      if( !channel )
      {
         GST_ERROR_OBJECT (manager, "All available channels are in use.");
         goto error;
      }

      GST_INFO_OBJECT (manager, "Selected channel 0x%x", channel);

      //Create the CommsIOXLink object before
      // sending the decided upon channel to the host. Upon
      // construction, this CommsIOXLink object will open
      // the XLink channel and start listening for messages.
      // This is done before sending back the channel to the host
      // as this avoid race conditions where the host could
      // potentially open this XLink channel and send messages
      // before we are ready to accept them.
      RemoteOffloadCommsXLinkIO *pCommsIOXLink =
            remote_offload_comms_io_xlink_new(pHandler,
                                              channel,
                                              tx_mode,
                                              coalesce_mode);

      if( !pCommsIOXLink )
      {
         GST_ERROR_OBJECT (manager,
                           "Error in remote_offload_comms_io_xlink_new for channel=0x%x\n",
                           channel);
         goto error;
      }
      else
      {
         //Set callback to be notified when this channel has been closed.
         remote_offload_comms_io_xlink_set_channel_closed_callback(pCommsIOXLink,
                                                                   xlink_commsio_channel_closed,
                                                                   manager);
      }

      //send the channel that we decided on back to the requester
      enum xlink_error write_sts =
            xlink_write_data(pHandler,
                             NEGOTIATION_CHANNEL,
                             (uint8_t*)&channel,
                             sizeof(channel));
      if( write_sts != X_LINK_SUCCESS )
      {
         GST_ERROR_OBJECT (manager,
                           "Error sending decided-on channel back to requester. err = %d",
                           write_sts);
         goto error;
      }

      g_array_append_val(commsioarray, pCommsIOXLink);
   }

exit:
   //FIXME (WA's for 2 Race Conditions below):
   // Race Condition 1:
   //   If xlink_close_channel happens too soon after the last write, intermittent
   //   issues occur -- such as dropping the buffer, or thread getting trapped within
   //   xlink_close_channel. Sleep for 25ms in hopes of avoiding that issue for now.
   // Race Condition 2:
   //   Note that this sleep is intentionally less than the sleep within
   //   xlink_channel_manager_request_channels. We want to ensure that the
   //   xlink server (caller of this function) gets around to calling
   //   xlink_open_channel again before the client does.
   g_usleep(25000);
   xlink_close_channel(pHandler, NEGOTIATION_CHANNEL);
   g_mutex_unlock(&manager->priv.managermutex);
   return commsioarray;

error:
   for(guint i = 0; i < commsioarray->len; i++ )
   {
      g_object_unref(g_array_index(commsioarray, RemoteOffloadCommsIO *, i));
   }

   g_array_free(commsioarray, TRUE);
   commsioarray = NULL;
   goto exit;
}

GArray* xlink_channel_manager_request_channels(XLinkChannelManager *manager,
                                               XLinkChannelRequestParams *params,
                                               guint nchannels,
                                               guint32 sw_device_id)
{
   if( !XLINK_IS_CHANNELMANAGER(manager) )
      return NULL;

   enum xlink_error xlink_err;

   if( !manager->priv.is_state_okay )
   {
      GST_ERROR_OBJECT (manager, "XLinkChannelManager in invalid state");
      return NULL;
   }

   if( !nchannels )
   {
      GST_ERROR_OBJECT (manager, "Number of requested channels is 0");
      return NULL;
   }

   //Don't allow multiple threads to be requesting channels at the same time.
   g_mutex_lock(&manager->priv.managermutex);
   struct xlink_handle *pHandler = xlink_channel_manager_get_handler(manager,
                                                                    sw_device_id);
   if( !pHandler )
   {
      GST_ERROR_OBJECT (manager, "Unable to obtain an XLink handle");
      g_mutex_unlock(&manager->priv.managermutex);
      return NULL;
   }

   if( xlink_channel_manager_open_default_channel(manager, pHandler) != X_LINK_SUCCESS )
   {
      GST_ERROR_OBJECT (manager, "Error opening default negotiation channel");
      g_mutex_unlock(&manager->priv.managermutex);
      return NULL;
   }

   GArray *commsioarray = g_array_new(FALSE, FALSE, sizeof(RemoteOffloadCommsIO *));
   guint currentXLinkBufferSize = 0;
   xlink_channel_id_t channel = 0;

   XLinkCommsIOTXMode tx_mode = XLINK_COMMSIO_TXMODE_NOPREFERENCE;
   switch(params->opMode)
   {
      case 0: tx_mode = XLINK_COMMSIO_TXMODE_NOPREFERENCE; break;
      case 1: tx_mode = XLINK_COMMSIO_TXMODE_BLOCKING; break;
      case 2: tx_mode = XLINK_COMMSIO_TXMODE_NONBLOCKING; break;
      default:
      break;
   }

   XLinkCommsIOCoaleseMode coalesce_mode;
   if( params->coalesceModeDisable )
   {
      coalesce_mode = XLINK_COMMSIO_COALESCEMODE_DISABLE;
   }
   else
   {
      coalesce_mode = XLINK_COMMSIO_COALESCEMODE_ENABLE;
   }

   //send the channels request string
   xlink_err = xlink_write_data(pHandler,
                                NEGOTIATION_CHANNEL,
                                (uint8_t*)REQUESTCHANNELS_STRING,
                                sizeof(REQUESTCHANNELS_STRING));
   if( xlink_err != X_LINK_SUCCESS )
   {
      GST_ERROR_OBJECT (manager, "Error sending request_string to xlink server. err=%d",
                                  xlink_err);
      goto error;
   }

   //Send the request params
   xlink_err = xlink_write_data(pHandler,
                                NEGOTIATION_CHANNEL,
                                (uint8_t*)params,
                                sizeof(XLinkChannelRequestParams));
   if( xlink_err != X_LINK_SUCCESS )
   {
      GST_ERROR_OBJECT (manager, "Error sending request parameters to xlink server. err=%d",
                                  xlink_err);
      goto error;
   }

   //send the number of channels that we are requesting
   xlink_err = xlink_write_data(pHandler,
                        NEGOTIATION_CHANNEL,
                        (uint8_t*)&nchannels,
                        sizeof(nchannels));
   if( xlink_err != X_LINK_SUCCESS )
   {
      GST_ERROR_OBJECT (manager, "Error sending nchannels to xlink server. err=%d",
                                  xlink_err);
      goto error;
   }

   for( guint i = 0; i < nchannels; i++ )
   {
      //wait for the channel sent back from the server
      xlink_err = xlink_read_data(pHandler,
                          NEGOTIATION_CHANNEL,
                          &manager->priv.read_buf,
                          &currentXLinkBufferSize);
      if( xlink_err != X_LINK_SUCCESS )
      {
         GST_ERROR_OBJECT (manager, "error receiving channel. err=%d",
                                     xlink_err);
         goto error;
      }

      if( currentXLinkBufferSize < sizeof(channel) )
      {
         GST_ERROR_OBJECT (manager,
                    "Request for channel returned size less than sizeof(xlink_channel_id_t)");
         goto error;
      }

      xlink_channel_id_t *pChanId = (xlink_channel_id_t *)manager->priv.read_buf;
      channel = *pChanId;

      xlink_err = xlink_release_data(pHandler,
                         NEGOTIATION_CHANNEL,
                         NULL);
      if(  xlink_err != X_LINK_SUCCESS )
      {
         GST_ERROR_OBJECT (manager, "xlink_release_data failed. err=%d",
                                     xlink_err);
         goto error;
      }

      //sanity check
      if( (channel < 0x401) || (channel > 0xFFE) )
      {
         GST_ERROR_OBJECT (manager, "Channel returned outside valid range");
         goto error;
      }

      RemoteOffloadCommsXLinkIO *pCommsIOXLink =
        remote_offload_comms_io_xlink_new(pHandler,
                                          channel,
                                          tx_mode,
                                          coalesce_mode);

      if( !pCommsIOXLink )
      {
         GST_ERROR_OBJECT (manager, "Error in remote_offload_comms_io_xlink_new");
         goto error;
      }

      g_array_append_val(commsioarray, pCommsIOXLink);
   }

exit:
   //FIXME (WA's for 2 Race Conditions below):
   // Race Condition 1:
   //   If xlink_close_channel happens too soon after the last write, intermittent
   //   issues occur -- such as dropping the buffer, or thread getting trapped within
   //   xlink_close_channel. Sleep for 50ms in hopes of avoiding that issue for now.
   // Race Condition 2:
   //   Note that this sleep is intentionally more than the sleep within
   //   xlink_channel_manager_listen_channels. We want to ensure that the
   //   xlink server gets around to calling xlink_open_channel again before the client
   //   (caller of this function) does.
   g_usleep(50000);
   xlink_close_channel(pHandler, NEGOTIATION_CHANNEL);
   g_mutex_unlock(&manager->priv.managermutex);
   return commsioarray;

error:
   for(guint i = 0; i < commsioarray->len; i++ )
   {
      g_object_unref(g_array_index(commsioarray, RemoteOffloadCommsIO *, i));
   }

   g_array_free(commsioarray, TRUE);
   commsioarray = NULL;
   goto exit;
}



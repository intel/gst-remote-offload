/*
 *  remoteoffloaddataexchanger.c - RemoteOffloadDataExchanger object
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
#include "remoteoffloaddataexchanger.h"
#include "datatransferdefs.h"
#include "remoteoffloadcommschannel.h"
#include "remoteoffloadprivateinterfaces.h"

enum
{
  PROP_COMMSCHANNEL = 1,
  PROP_EXCHANGERNAME,
  N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

typedef struct
{
  gchar *name;
  RemoteOffloadCommsChannel *channel;
  guint16 id;
}RemoteOffloadDataExchangerPrivate;

static void
remote_offload_data_exchanger_callback_interface_init (RemoteOffloadCommsCallbackInterface *iface);

GST_DEBUG_CATEGORY_STATIC (data_exchanger_debug);
#define GST_CAT_DEFAULT data_exchanger_debug

G_DEFINE_TYPE_WITH_CODE (RemoteOffloadDataExchanger, remote_offload_data_exchanger, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (RemoteOffloadDataExchanger)
                         G_IMPLEMENT_INTERFACE (REMOTEOFFLOADCOMMSCALLBACK_TYPE,
                         remote_offload_data_exchanger_callback_interface_init)
                         GST_DEBUG_CATEGORY_INIT (data_exchanger_debug,
                         "remoteoffloaddataexchanger", 0,
                         "debug category for RemoteOffloadDataExchanger"))

static GstMemory*
remote_offload_data_exchanger_allocate_data_segment(RemoteOffloadCommsCallback *callback,
                               guint16 dataTransferType,
                               guint16 segmentIndex,
                               guint64 segmentSize,
                               const GArray *segment_mem_array_so_far)
{
   RemoteOffloadDataExchanger *pExchanger = REMOTEOFFLOAD_DATAEXCHANGER(callback);

   GstMemory *mem = NULL;

   RemoteOffloadDataExchangerClass *klass = REMOTEOFFLOAD_DATAEXCHANGER_GET_CLASS(pExchanger);
   if( klass->allocate_data_segment )
   {
      mem = klass->allocate_data_segment(pExchanger,
                                         segmentIndex,
                                         segmentSize,
                                         segment_mem_array_so_far);
   }
   else
   {
     GST_ERROR_OBJECT (callback, "allocate_data_segment callback not set..");
   }

   return mem;
}

void remote_offload_comms_channel_data_transfer_received(RemoteOffloadCommsCallback *callback,
                               const DataTransferHeader *header,
                               GArray *segment_mem_array)
{
   RemoteOffloadDataExchanger *pExchanger = REMOTEOFFLOAD_DATAEXCHANGER(callback);

   RemoteOffloadDataExchangerClass *klass = REMOTEOFFLOAD_DATAEXCHANGER_GET_CLASS(pExchanger);
   if( klass->received )
   {
     klass->received(pExchanger, segment_mem_array, header->response_id);
   }
   else
   {
      GST_ERROR_OBJECT (callback, "received callback not set..");
   }

}

static void
remote_offload_data_exchanger_callback_interface_init (RemoteOffloadCommsCallbackInterface *iface)
{
  iface->allocate_data_segment = remote_offload_data_exchanger_allocate_data_segment;
  iface->data_transfer_received = remote_offload_comms_channel_data_transfer_received;
}

static void remote_offload_data_exchanger_get_property (GObject    *object,
                                                        guint       property_id,
                                                        GValue     *value,
                                                        GParamSpec *pspec)
{
  RemoteOffloadDataExchanger *pExchanger = REMOTEOFFLOAD_DATAEXCHANGER(object);
  RemoteOffloadDataExchangerPrivate *priv =
        remote_offload_data_exchanger_get_instance_private (pExchanger);
  switch (property_id)
  {
    case PROP_EXCHANGERNAME:
        g_value_set_string(value, priv->name);
        break;
    case PROP_COMMSCHANNEL:
        g_value_set_pointer(value, priv->channel);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
  }
}

static void
remote_offload_data_exchanger_set_property (GObject      *object,
                                            guint         property_id,
                                            const GValue *value,
                                            GParamSpec   *pspec)
{
  RemoteOffloadDataExchanger *pExchanger = REMOTEOFFLOAD_DATAEXCHANGER(object);
  RemoteOffloadDataExchangerPrivate *priv =
        remote_offload_data_exchanger_get_instance_private (pExchanger);
  switch (property_id)
  {
    case PROP_COMMSCHANNEL:
    {
      RemoteOffloadCommsChannel *tmpchannel = g_value_get_pointer (value);
      if( REMOTEOFFLOAD_IS_COMMSCHANNEL(tmpchannel) )
      {
         priv->channel = g_object_ref(tmpchannel);
      }
    }
    break;

    case PROP_EXCHANGERNAME:
    {
       priv->name = g_value_dup_string(value);
    }
    break;

    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void remote_offload_data_exchanger_constructed(GObject *object)
{
  RemoteOffloadDataExchanger *pExchanger = REMOTEOFFLOAD_DATAEXCHANGER(object);
  RemoteOffloadDataExchangerPrivate *priv =
        remote_offload_data_exchanger_get_instance_private (pExchanger);

  if( priv->channel && priv->name )
  {
    if( !remote_offload_comms_channel_register_exchanger(priv->channel,
                                                         pExchanger) )
    {
       GST_ERROR_OBJECT (pExchanger, "error registering exchanger with comms channel");
    }
  }
  else
  {
     GST_ERROR_OBJECT (pExchanger, "Channel or Name aren't set, so not registering with channel\n");
  }

  G_OBJECT_CLASS (remote_offload_data_exchanger_parent_class)->constructed (object);
}

static void
remote_offload_data_exchanger_finalize (GObject *gobject)
{
  RemoteOffloadDataExchanger *pExchanger = REMOTEOFFLOAD_DATAEXCHANGER(gobject);
  RemoteOffloadDataExchangerPrivate *priv =
        remote_offload_data_exchanger_get_instance_private (pExchanger);

  if( priv->channel )
  {
     //unregister this data exchanger from the comms channel.
     // This will block until all pending tasks have been completed.
     remote_offload_comms_channel_unregister_exchanger(priv->channel,
                                                       pExchanger);
     g_object_unref(priv->channel);
  }

  if( priv->name )
     g_free(priv->name);

  G_OBJECT_CLASS (remote_offload_data_exchanger_parent_class)->finalize (gobject);
}

GstMemory*
remote_offload_data_exchanger_default_allocate_data_segment(RemoteOffloadDataExchanger *exchanger,
                                                            guint16 segmentIndex,
                                                            guint64 segmentSize,
                                                            const GArray *segment_mem_array_so_far)
{
   return gst_allocator_alloc (NULL, segmentSize, NULL);
}

static void
remote_offload_data_exchanger_class_init (RemoteOffloadDataExchangerClass *klass)
{
   GObjectClass *object_class = G_OBJECT_CLASS (klass);

   klass->allocate_data_segment = remote_offload_data_exchanger_default_allocate_data_segment;
   klass->received = 0;

   object_class->set_property = remote_offload_data_exchanger_set_property;
   object_class->get_property = remote_offload_data_exchanger_get_property;
   obj_properties[PROP_COMMSCHANNEL] =
    g_param_spec_pointer ("commschannel",
                         "CommsChannel",
                         "CommsChannel object to register with",
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

   obj_properties[PROP_EXCHANGERNAME] =
    g_param_spec_string ("exchangername",
                         "ExchangerName",
                         "Name of exchanger",
                         "default.exchanger.name",
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

   g_object_class_install_properties (object_class,
                                     N_PROPERTIES,
                                     obj_properties);

   object_class->constructed = remote_offload_data_exchanger_constructed;
   object_class->finalize = remote_offload_data_exchanger_finalize;
}



static void
remote_offload_data_exchanger_init (RemoteOffloadDataExchanger *self)
{
  RemoteOffloadDataExchangerPrivate *priv =
        remote_offload_data_exchanger_get_instance_private (self);

  priv->channel = 0;
  priv->name = 0;
  priv->id = 0;
}

void remote_offload_data_exchanger_set_id(RemoteOffloadDataExchanger *exchanger,
                                          guint16 id)
{
  if( !REMOTEOFFLOAD_IS_DATAEXCHANGER(exchanger) )
     return;

  RemoteOffloadDataExchangerPrivate *priv =
        remote_offload_data_exchanger_get_instance_private (exchanger);

  priv->id = id;
}

static GstMemory* virt_to_mem(void *pVirt,
                              gsize size)
{
   GstMemory *mem = NULL;

   if( pVirt && (size>0) )
   {

      mem = gst_memory_new_wrapped((GstMemoryFlags)0,
                                   pVirt,
                                   size,
                                   0,
                                   size,
                                   NULL,
                                   NULL);
   }

   return mem;
}


gboolean remote_offload_data_exchanger_write(RemoteOffloadDataExchanger *exchanger,
                                                          GList *mem_list,
                                                          RemoteOffloadResponse *pResponse)
{
   if( !REMOTEOFFLOAD_IS_DATAEXCHANGER(exchanger) )
     return FALSE;

   RemoteOffloadDataExchangerPrivate *priv =
         remote_offload_data_exchanger_get_instance_private (exchanger);

   DataTransferHeader header;
   header.dataTransferType = priv->id;


   gboolean ret = remote_offload_comms_channel_write(priv->channel,
                                                         &header,
                                                         mem_list,
                                                         pResponse);

   return ret;
}

gboolean remote_offload_data_exchanger_write_response(RemoteOffloadDataExchanger *exchanger,
                                                      GList *mem_list,
                                                      guint64 response_id)
{
   if( !REMOTEOFFLOAD_IS_DATAEXCHANGER(exchanger) )
   {
     GST_ERROR("!REMOTEOFFLOAD_IS_DATAEXCHANGER(%p)", exchanger);
     return FALSE;
   }

   RemoteOffloadDataExchangerPrivate *priv =
         remote_offload_data_exchanger_get_instance_private (exchanger);

   gboolean ret = remote_offload_comms_channel_write_response(priv->channel,
                                                              mem_list,
                                                              response_id);

   return ret;
}

gboolean remote_offload_data_exchanger_write_response_single(RemoteOffloadDataExchanger *exchanger,
                                                             guint8 *data,
                                                             gsize size,
                                                             guint64 response_id)
{
   GList *memList = NULL;
   GstMemory *mem = NULL;
   if( data && (size > 0))
   {
      mem = virt_to_mem(data, size);
      memList = g_list_append (memList,
                               mem);
   }

   gboolean ret = remote_offload_data_exchanger_write_response(exchanger, memList, response_id);

   if( mem )
      gst_memory_unref(mem);

   g_list_free(memList);

   return ret;
}

gboolean remote_offload_data_exchanger_write_single(RemoteOffloadDataExchanger *exchanger,
                                                        guint8 *data,
                                                        gsize size,
                                                        RemoteOffloadResponse *pResponse)
{
   GList *memList = NULL;
   GstMemory *mem = NULL;
   if( data && (size > 0))
   {
      mem = virt_to_mem(data, size);
      memList = g_list_append (memList,
                               mem);
   }

   gboolean ret = remote_offload_data_exchanger_write(exchanger, memList, pResponse);

   if( mem )
      gst_memory_unref(mem);

   g_list_free(memList);

   return ret;
}

RemoteOffloadDataExchanger *remote_offload_data_exchanger_new(RemoteOffloadCommsChannel *channel,
                                                              const gchar *exchangername)
{
  RemoteOffloadDataExchanger *exchanger =
        g_object_new(REMOTEOFFLOADDATAEXCHANGER_TYPE,
                     "commschannel", channel,
                     "exchangername", exchangername, NULL);

  return exchanger;
}



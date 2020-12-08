/*
 *  queuestatsdataexchanger.c - QueueStatsDataExchanger object
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

#include "queuestatsdataexchanger.h"

enum
{
  PROP_CALLBACK = 1,
  N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

struct _QueueStatsDataExchanger
{
  RemoteOffloadDataExchanger parent_instance;

  /* Other members, including private data. */
  QueueStatsDataExchangerCallback *callback;

};

GST_DEBUG_CATEGORY_STATIC (queuestats_data_exchanger_debug);
#define GST_CAT_DEFAULT queuestats_data_exchanger_debug

G_DEFINE_TYPE_WITH_CODE (QueueStatsDataExchanger,
                         queuestats_data_exchanger, REMOTEOFFLOADDATAEXCHANGER_TYPE,
                         GST_DEBUG_CATEGORY_INIT (queuestats_data_exchanger_debug,
                                       "remoteoffloadqueuestatsdataexchanger", 0,
                                       "debug category for remoteoffloadqueuestatsdataexchanger"))

gboolean queuestats_data_exchanger_received(RemoteOffloadDataExchanger *exchanger,
                                           const GArray *segment_mem_array,
                                           guint64 response_id)
{
   if( !DATAEXCHANGER_IS_QUEUESTATS(exchanger))
      return FALSE;

   QueueStatsDataExchanger *self = (QueueStatsDataExchanger *)exchanger;

   GArray *queuestats_array = NULL;
   if( self->callback && self->callback->request_received )
   {
      queuestats_array = self->callback->request_received(self->callback->priv);
   }
   else
   {
      GST_WARNING_OBJECT (exchanger, "request_received callback not set");
   }

   GList *mem_list = NULL;
   if( queuestats_array )
   {
      for( guint i = 0; i < queuestats_array->len; i++ )
      {
         mem_list = g_list_append (mem_list,
                                   g_array_index (queuestats_array, GstMemory*, i));
      }
   }

   //send back the response
   return remote_offload_data_exchanger_write_response(exchanger,
                                                       mem_list,
                                                       response_id);
}

GArray *queuestats_data_exchanger_request_stats(QueueStatsDataExchanger *queuestatsexchanger)
{
   if( !DATAEXCHANGER_IS_QUEUESTATS(queuestatsexchanger) )
     return FALSE;

   RemoteOffloadResponse *pResponse = remote_offload_response_new();

   GArray *queuestats_array = NULL;

   //No need to send anything to kick off the request
   gboolean ret = remote_offload_data_exchanger_write_single(
                                                (RemoteOffloadDataExchanger *)queuestatsexchanger,
                                                NULL,
                                                0,
                                                pResponse);
   if( ret )
   {
      if( remote_offload_response_wait(pResponse, 0) == REMOTEOFFLOADRESPONSE_RECEIVED )
      {
         queuestats_array = remote_offload_response_steal_mem_array(pResponse);
      }
      else
      {
         GST_ERROR_OBJECT (queuestatsexchanger, "remote_offload_response_wait failed");
      }

   }

   g_object_unref(pResponse);

   return queuestats_array;
}

static void queuestats_data_exchanger_constructed(GObject *gobject)
{
   G_OBJECT_CLASS (queuestats_data_exchanger_parent_class)->constructed (gobject);
}

static void
queuestats_data_exchanger_finalize (GObject *gobject)
{
   G_OBJECT_CLASS (queuestats_data_exchanger_parent_class)->finalize (gobject);
}

static void
queuestats_data_exchanger_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  QueueStatsDataExchanger *self = DATAEXCHANGER_QUEUESTATS (object);
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
queuestats_data_exchanger_class_init (QueueStatsDataExchangerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  RemoteOffloadDataExchangerClass *parent_class = REMOTEOFFLOAD_DATAEXCHANGER_CLASS(klass);

  object_class->set_property = queuestats_data_exchanger_set_property;
  obj_properties[PROP_CALLBACK] =
    g_param_spec_pointer ("callback",
                         "Callback",
                         "Received Callback",
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);

  g_object_class_install_properties (object_class,
                                     N_PROPERTIES,
                                     obj_properties);

  object_class->finalize = queuestats_data_exchanger_finalize;
  object_class->constructed = queuestats_data_exchanger_constructed;
  parent_class->received = queuestats_data_exchanger_received;
}

static void
queuestats_data_exchanger_init (QueueStatsDataExchanger *self)
{
  self->callback = NULL;
}

QueueStatsDataExchanger *queuestats_data_exchanger_new (RemoteOffloadCommsChannel *channel,
                                                        QueueStatsDataExchangerCallback *pcallback)
{
   QueueStatsDataExchanger *pexchanger =
        g_object_new(QUEUESTATSDATAEXCHANGER_TYPE,
                     "callback", pcallback,
                     "commschannel", channel,
                     "exchangername", "xlink.exchanger.queuestats",
                     NULL);

   return pexchanger;
}

void GetQueueStats(GstElement *queue, QueueStatistics *stats)
{
   if( queue )
   {
      g_object_get(G_OBJECT(queue),
                   "current-level-buffers",
                   &stats->current_level_buffers,
                   "current-level-bytes",
                   &stats->current_level_bytes,
                   "current-level-time",
                   &stats->current_level_time,
                   "max-size-buffers",
                   &stats->max_size_buffers,
                   "max-size-bytes",
                   &stats->max_size_bytes,
                   "max-size-time",
                   &stats->max_size_time,
                   NULL);
   }
}

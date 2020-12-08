/*
 *  pingdataexchanger.c - PingDataExchanger object
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
#include <sys/time.h>
#include "pingdataexchanger.h"

static double time_stamp ()
{
     struct timeval t;
     gettimeofday(&t, 0);
     return t.tv_sec + t.tv_usec/1e6;
}

struct _PingDataExchanger
{
  RemoteOffloadDataExchanger parent_instance;

};

GST_DEBUG_CATEGORY_STATIC (ping_data_exchanger_debug);
#define GST_CAT_DEFAULT ping_data_exchanger_debug

G_DEFINE_TYPE_WITH_CODE (PingDataExchanger, ping_data_exchanger, REMOTEOFFLOADDATAEXCHANGER_TYPE,
GST_DEBUG_CATEGORY_INIT (ping_data_exchanger_debug, "remoteoffloadpingdataexchanger", 0,
  "debug category for remoteoffloadpingdataexchanger"))

gboolean ping_data_exchanger_received(RemoteOffloadDataExchanger *exchanger,
                                      const GArray *segment_mem_array,
                                      guint64 response_id)
{
   if( !segment_mem_array ||
       (segment_mem_array->len != 1) ||
       !DATAEXCHANGER_IS_PING(exchanger))
      return FALSE;

   return remote_offload_data_exchanger_write_response_single(exchanger,
                                                        NULL,
                                                        0,
                                                        response_id);
}


gboolean ping_data_exchanger_send_ping(PingDataExchanger *pingexchanger)
{
   if( !DATAEXCHANGER_IS_PING(pingexchanger) )
     return FALSE;

   int val = 77;

   RemoteOffloadResponse *pResponse = remote_offload_response_new();

   gdouble start = time_stamp()*1000.0;
   gboolean ret =
         remote_offload_data_exchanger_write_single((RemoteOffloadDataExchanger *)pingexchanger,
                                                    (guint8 *)&val,
                                                    sizeof(val),
                                                    pResponse);
   if( ret )
   {
      //5 second timeout
      if( remote_offload_response_wait(pResponse, 5000) != REMOTEOFFLOADRESPONSE_RECEIVED )
      {
         GST_ERROR_OBJECT (pingexchanger, "remote_offload_response_wait failed");
         ret = FALSE;
      }
      else
      {
         gdouble end = time_stamp()*1000.0;
         GST_INFO_OBJECT (pingexchanger, "ping = %f ms\n", end - start);
      }
   }

   g_object_unref(pResponse);

   return ret;
}

static void ping_data_exchanger_constructed(GObject *gobject)
{
  G_OBJECT_CLASS (ping_data_exchanger_parent_class)->constructed (gobject);
}

static void
remote_offload_comms_channel_finalize (GObject *gobject)
{
   G_OBJECT_CLASS (ping_data_exchanger_parent_class)->finalize (gobject);
}

static void
ping_data_exchanger_class_init (PingDataExchangerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  RemoteOffloadDataExchangerClass *parent_class = REMOTEOFFLOAD_DATAEXCHANGER_CLASS(klass);

  object_class->finalize = remote_offload_comms_channel_finalize;
  object_class->constructed = ping_data_exchanger_constructed;
  parent_class->received = ping_data_exchanger_received;
}

static void
ping_data_exchanger_init (PingDataExchanger *self)
{

}

PingDataExchanger *ping_data_exchanger_new (RemoteOffloadCommsChannel *channel)
{
   PingDataExchanger *pexchanger =
        g_object_new(PINGDATAEXCHANGER_TYPE,
                     "commschannel", channel,
                     "exchangername", "xlink.exchanger.ping",
                     NULL);

   return pexchanger;
}

//PING RESPONSE

struct _PingResponseDataExchanger
{
  RemoteOffloadDataExchanger parent_instance;

  /* Other members, including private data. */

};

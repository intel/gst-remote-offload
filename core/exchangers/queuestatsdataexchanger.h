/*
 *  queuestatsdataexchanger.h - QueueStatsDataExchanger object
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

#ifndef __REMOTEOFFLOADQUEUESTATSDATAEXCHANGER_H__
#define __REMOTEOFFLOADQUEUESTATSDATAEXCHANGER_H__

#include "remoteoffloaddataexchanger.h"

G_BEGIN_DECLS

#define QUEUESTATSDATAEXCHANGER_TYPE (queuestats_data_exchanger_get_type ())
G_DECLARE_FINAL_TYPE(QueueStatsDataExchanger, queuestats_data_exchanger,
                     DATAEXCHANGER, QUEUESTATS, RemoteOffloadDataExchanger);

typedef struct _QueueStatistics
{
   guint64 pts; //presentation timestamp of the buffer

   guint current_level_buffers;
   guint current_level_bytes;
   guint64 current_level_time;

   guint max_size_buffers;
   guint max_size_bytes;
   guint64 max_size_time;
}QueueStatistics;

void GetQueueStats(GstElement *queue, QueueStatistics *stats);

typedef struct _QueueStatsDataExchangerCallback
{
   //notification of a request for current queue stats
   // return GArray of QueueStatistics, or NULL
   // if no stats have been collected.
   GArray *(*request_received)(void *priv);
   void *priv;
}QueueStatsDataExchangerCallback;

QueueStatsDataExchanger *queuestats_data_exchanger_new (RemoteOffloadCommsChannel *channel,
                                                        QueueStatsDataExchangerCallback *pcallback);

//Request GArray of QueueStatistics from remote entity.
// Unref when done using it.
GArray *queuestats_data_exchanger_request_stats(QueueStatsDataExchanger *queuestatsexchanger);

G_END_DECLS

#endif

/*
 *  querydataexchanger.h - QueryDataExchanger object
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

#ifndef __REMOTEOFFLOADQUERYDATAEXCHANGER_H__
#define __REMOTEOFFLOADQUERYDATAEXCHANGER_H__

#include "remoteoffloaddataexchanger.h"

G_BEGIN_DECLS

#define QUERYDATAEXCHANGER_TYPE (query_data_exchanger_get_type ())
G_DECLARE_FINAL_TYPE(QueryDataExchanger, query_data_exchanger,
                     DATAEXCHANGER, QUERY, RemoteOffloadDataExchanger);

typedef struct _QueryDataExchangerCallback
{
   //notification of query received. Make sure to call
   // query_data_exchanger_send_query_result with result of query
   void (*query_received)(GstQuery *query, void *priv);
   void *priv;
}QueryDataExchangerCallback;

QueryDataExchanger *query_data_exchanger_new (RemoteOffloadCommsChannel *channel,
                                             QueryDataExchangerCallback *callback);

gboolean query_data_exchanger_send_query(QueryDataExchanger *queryexchanger,
                                         GstQuery *query);

gboolean query_data_exchanger_send_query_result(QueryDataExchanger *queryexchanger,
                                                GstQuery *query,
                                                gboolean result);

G_END_DECLS

#endif

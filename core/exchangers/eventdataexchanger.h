/*
 *  eventdataexchanger.h - EventDataExchanger object
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

#ifndef __REMOTEOFFLOADEVENTDATAEXCHANGER_H__
#define __REMOTEOFFLOADEVENTDATAEXCHANGER_H__

#include "remoteoffloaddataexchanger.h"

G_BEGIN_DECLS

#define EVENTDATAEXCHANGER_TYPE (event_data_exchanger_get_type ())
G_DECLARE_FINAL_TYPE(EventDataExchanger, event_data_exchanger,
                     DATAEXCHANGER, EVENT, RemoteOffloadDataExchanger);

typedef struct _EventDataExchangerCallback
{
   //notification of event received. Make sure to call
   // event_data_exchanger_send_event_result with result of event
   void (*event_received)(GstEvent *event, void *priv);
   void *priv;
}EventDataExchangerCallback;

EventDataExchanger *event_data_exchanger_new (RemoteOffloadCommsChannel *channel,
                                              EventDataExchangerCallback *callback);

gboolean event_data_exchanger_send_event(EventDataExchanger *eventexchanger,
                                         GstEvent *event);

gboolean event_data_exchanger_send_event_result(EventDataExchanger *eventexchanger,
                                                GstEvent *event,
                                                gboolean result);

G_END_DECLS

#endif

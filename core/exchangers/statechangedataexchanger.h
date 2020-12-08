/*
 *  statechangedataexchanger.h - StateChangeDataExchanger object
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

#ifndef __REMOTEOFFLOADSTATECHANGEDATAEXCHANGER_H__
#define __REMOTEOFFLOADSTATECHANGEDATAEXCHANGER_H__

#include "remoteoffloaddataexchanger.h"

G_BEGIN_DECLS

#define STATECHANGEDATAEXCHANGER_TYPE (statechange_data_exchanger_get_type ())
G_DECLARE_FINAL_TYPE(StateChangeDataExchanger, statechange_data_exchanger,
                     DATAEXCHANGER, STATECHANGE, RemoteOffloadDataExchanger);

typedef struct _StateChangeDataExchangerCallback
{
   //notification of state change received.
   GstStateChangeReturn (*statechange_received)(GstStateChange stateChange, void *priv);
   void *priv;
}StateChangeDataExchangerCallback;

StateChangeDataExchanger *
statechange_data_exchanger_new (RemoteOffloadCommsChannel *channel,
                                StateChangeDataExchangerCallback *callback);

GstStateChangeReturn statechange_data_exchanger_send_statechange(StateChangeDataExchanger *exchanger,
                                                     GstStateChange stateChange);


gboolean wait_for_state_change(StateChangeDataExchanger *exchanger,
                               GstStateChange stateChange,
                               gint64 timeoutmicroseconds);

void cancel_wait_for_state_change(StateChangeDataExchanger *exchanger);

G_END_DECLS

#endif

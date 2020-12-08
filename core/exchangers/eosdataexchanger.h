/*
 *  eosdataexchanger.h - EOSDataExchanger object
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

#ifndef __REMOTEOFFLOADEOSDATAEXCHANGER_H__
#define __REMOTEOFFLOADEOSDATAEXCHANGER_H__

#include "remoteoffloaddataexchanger.h"

G_BEGIN_DECLS

#define EOSDATAEXCHANGER_TYPE (eos_data_exchanger_get_type ())
G_DECLARE_FINAL_TYPE(EOSDataExchanger,
                     eos_data_exchanger, DATAEXCHANGER, EOS, RemoteOffloadDataExchanger);

typedef struct _EOSDataExchangerCallback
{
   //notification of EOS received.
   void (*eos_received)(void *priv);
   void *priv;
}EOSDataExchangerCallback;

EOSDataExchanger *eos_data_exchanger_new (RemoteOffloadCommsChannel *channel,
                                          EOSDataExchangerCallback *callback);

gboolean eos_data_exchanger_send_eos(EOSDataExchanger *eosexchanger);

G_END_DECLS

#endif

/*
 *  errormessagedataexchanger.h - ErrorMessageDataExchanger object
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

#ifndef __REMOTEOFFLOADERRORMESSAGEDATAEXCHANGER_H__
#define __REMOTEOFFLOADERRORMESSAGEDATAEXCHANGER_H__

#include "remoteoffloaddataexchanger.h"

G_BEGIN_DECLS

#define ERRORMESSAGEDATAEXCHANGER_TYPE (errormessage_data_exchanger_get_type ())
G_DECLARE_FINAL_TYPE(ErrorMessageDataExchanger, errormessage_data_exchanger,
                     DATAEXCHANGER, ERRORMESSAGE, RemoteOffloadDataExchanger);

typedef struct _ErrorMessageDataExchangerCallback
{
   //notification of error message received. It is the callbacks responsibility
   // to g_free the message when done.
   void (*errormessage_received)(gchar *message, void *priv);
   void *priv;
}ErrorMessageDataExchangerCallback;

ErrorMessageDataExchanger
*errormessage_data_exchanger_new (RemoteOffloadCommsChannel *channel,
                                  ErrorMessageDataExchangerCallback *callback);

gboolean
errormessage_data_exchanger_send_message(ErrorMessageDataExchanger *exchanger,
                                         gchar *message);

G_END_DECLS


#endif

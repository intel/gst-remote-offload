/*
 *  bufferdataexchanger.h - BufferDataExchanger object
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

#ifndef __REMOTEOFFLOADBUFFERDATAEXCHANGER_H__
#define __REMOTEOFFLOADBUFFERDATAEXCHANGER_H__

#include "remoteoffloaddataexchanger.h"

G_BEGIN_DECLS

#define BUFFERDATAEXCHANGER_TYPE (buffer_data_exchanger_get_type ())
G_DECLARE_FINAL_TYPE(BufferDataExchanger,
                     buffer_data_exchanger, DATAEXCHANGER, BUFFER, RemoteOffloadDataExchanger);

typedef struct _BufferDataExchangerCallback
{
   //notification of buffer received. It is the callbacks responsibility
   // to unref the buffer
   void (*buffer_received)(GstBuffer *buffer, void *priv);
   GstMemory *(*alloc_buffer_mem_block)(gsize memBlockSize, void *priv);
   void *priv;
}BufferDataExchangerCallback;

BufferDataExchanger *buffer_data_exchanger_new (RemoteOffloadCommsChannel *channel,
                                                BufferDataExchangerCallback *callback);

GstFlowReturn buffer_data_exchanger_send_buffer(BufferDataExchanger *bufferexchanger,
                                                GstBuffer *buffer);

//Send the result of gst_pad_push(src, buffer)
gboolean buffer_data_exchanger_send_buffer_flowreturn(BufferDataExchanger *bufferexchanger,
                                                  GstBuffer *buffer,
                                                  GstFlowReturn returnVal);

G_END_DECLS

#endif

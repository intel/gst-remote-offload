/*
 *  genericdataexchanger.h - GenericDataExchanger object
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
#ifndef __REMOTEOFFLOADGENERICDATAEXCHANGER_H__
#define __REMOTEOFFLOADGENERICDATAEXCHANGER_H__

#include "remoteoffloaddataexchanger.h"

G_BEGIN_DECLS

#define GENERICDATAEXCHANGER_TYPE (generic_data_exchanger_get_type ())
G_DECLARE_FINAL_TYPE(GenericDataExchanger, generic_data_exchanger,
                     DATAEXCHANGER, GENERIC, RemoteOffloadDataExchanger);

typedef struct _GenericDataExchangerCallback
{
   //transfer_type: guint32 identifier for this "message"
   //memblocks: GArray of GstMemory* objects.
   //           Note that the GArray will be unref'ed, as
   //           well as the GstMemory* objects that the
   //           GArray is holding upon the completion
   //           of this callback. If they need to be retained
   //           for longer than the scope of this callback,
   //           at least the GstMemory objects should be
   //           ref'ed. Increasing only the ref count for the
   //           GArray will retain a valid GArray to stale (destroyed)
   //           GstMemory objects.
   gboolean (*received)(guint32 transfer_type,
                        GArray *memblocks,
                        void *priv);

   void *priv;
}GenericDataExchangerCallback;

GenericDataExchanger *generic_data_exchanger_new (RemoteOffloadCommsChannel *channel,
                                                  GenericDataExchangerCallback *callback);

//Send a GArray of GstMemory* objects.
//transfer_type: guint32 identifier for this message. This will be
// passed along to the receiver's callback.
//memblocks: a GArray of GstMemory* objects.
//blocking: If set to TRUE, this will block until the remote 'received' callback
//            method is complete, and will return the value of that function.
//          If set to FALSE, this will return immediately upon sending the memBlocks,
//            and the return value only indicates success or failure of the transfer.
gboolean generic_data_exchanger_send(GenericDataExchanger *exchanger,
                                     guint32 transfer_type,
                                     GArray *memblocks,
                                     gboolean blocking);

//Convenience function. Will wrap pData / size into a GstMemory segment,
// and internally call generic_data_exchanger_send
gboolean generic_data_exchanger_send_virt(GenericDataExchanger *exchanger,
                                          guint32 transfer_type,
                                          void *pData,
                                          gsize size,
                                          gboolean blocking);



G_END_DECLS

#endif

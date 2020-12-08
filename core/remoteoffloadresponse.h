/*
 *  remoteoffloadresponse.h - RemoteOffloadResponse object
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

#ifndef __REMOTEOFFLOADRESPONSE_H__
#define __REMOTEOFFLOADRESPONSE_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define REMOTEOFFLOADRESPONSE_TYPE (remote_offload_response_get_type ())
G_DECLARE_FINAL_TYPE (RemoteOffloadResponse,
                      remote_offload_response, REMOTEOFFLOAD, RESPONSE, GObject)

RemoteOffloadResponse *remote_offload_response_new();

typedef enum
{
   REMOTEOFFLOADRESPONSE_RECEIVED = 0,
   REMOTEOFFLOADRESPONSE_TIMEOUT,
   REMOTEOFFLOADRESPONSE_CANCELLED,
   REMOTEOFFLOADRESPONSE_FAILURE
} RemoteOffloadResponseStatus;

//Wait for a response...
// Use a timeout value <=0 to disable timeout (wait indefinitely)
RemoteOffloadResponseStatus remote_offload_response_wait(RemoteOffloadResponse *response,
                                                         gint32 timeoutmilliseconds);

//Steal the GArray of GstMemory objects (i.e. the response) from
// the response object. The caller takes full ownership of the
// GstMemory objects, as well as the GArray itself.
// For cleanup purposes, the caller is responsible for unref'ing
// each entry in the GArray using gst_memory_unref(), and destroying
// the GArray via g_array_free() or g_array_unref().
GArray *remote_offload_response_steal_mem_array(RemoteOffloadResponse *response);


//Picturing the N GstMemory's stored within the internal GArray as
// a contiguous chunk of memory, copy 'size' bytes into dest, starting at
// offset 'offset'.
gboolean remote_offload_copy_response(RemoteOffloadResponse *response,
                                      void *dest,
                                      gsize size,
                                      gsize offset);



G_END_DECLS

#endif /* __REMOTEOFFLOADRESPONSE_H__ */

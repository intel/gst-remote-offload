/*
 * gsthddltcpconn.c - Temporary placeholder 
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author: Yu, Chan Kit <chan.kit.yu@intel.com>
 *    Author: Hoe, Sheng Yang <sheng.yang.hoe@intel.com>
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

#include <stdio.h>
#include <stdlib.h>
#include "gsthddltcpplaceholder.h"

#define READ_CHUNK_SIZE 4096

gboolean
gst_hddl_tcp_establish_connection (GstHddlTcp * hddl_tcp)
{
    return TRUE;
}

gboolean
gst_hddl_tcp_listen_client (GstHddlTcp * hddl_tcp)
{
    return TRUE;
}

gboolean
gst_hddl_tcp_shutdown (GstHddlTcp * hddl_tcp)
{
    return TRUE;
}

gboolean
gst_hddl_tcp_transfer_data (GstHddlTcp * hddl_tcp, void *data, size_t size)
{
    return TRUE;
}

gboolean
gst_hddl_tcp_receive_data (GstHddlTcp * hddl_tcp, void **buffer, size_t size)
{
    return TRUE;
}

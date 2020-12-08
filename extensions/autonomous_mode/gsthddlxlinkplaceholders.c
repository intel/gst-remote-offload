/*
 *  gsthddlxlinkplaceholders.c - Placeholders for XLink
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author: Soon, Thean Siew <thean.siew.soon@intel.com>
 *    Author: Yu, Chan Kit <chan.kit.yu@intel.com>
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

// FIXME: This file is to be deleted after the actual xlink content
// is available
#ifndef GST_HDDL_XLINK_COMMS_H
#include "gsthddlxlinkplaceholders.h"

enum xlink_error
xlink_initialize ()
{
  return X_LINK_SUCCESS;
}

enum xlink_error
xlink_connect (struct xlink_handle *handle)
{
  return X_LINK_SUCCESS;
}

enum xlink_error 
xlink_open_channel(struct xlink_handle *handle,
                uint16_t chan, enum xlink_opmode mode, uint32_t data_size,
                uint32_t timeout)
{
  return X_LINK_SUCCESS;
}

enum xlink_error
xlink_write_data(struct xlink_handle *handle,
                uint16_t chan, uint8_t const *message, uint32_t size);
{
  return X_LINK_SUCCESS;
}

enum xlink_error
xlink_read_data(struct xlink_handle *handle,
                uint16_t chan, uint8_t **message, uint32_t *size)
{
  return X_LINK_SUCCESS;
}

enum xlink_error
xlink_release_data(struct xlink_handle *handle,
                uint16_t chan, uint8_t * const data_addr)
{
  return X_LINK_SUCCESS;
}

enum xlink_error
xlink_close_channel(struct xlink_handle *handle, uint16_t chan)
{
  return X_LINK_SUCCESS;
}

enum xlink_error
xlink_disconnect(struct xlink_handle *handle)
{
  return X_LINK_SUCCESS;
}

#endif

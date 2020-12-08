/*
 *  gsthddlxlinkplaceholders.h - Placeholders for XLink
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author: Soon, Thean Siew <thean.siew.soon@intel.com>
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

// FIXME: This file is to be deleted after the actual xlink content
// is available

#ifndef XLINK_SIMULATOR
#include <glib.h>

#ifndef GST_HDDL_XLINK_PLACEHOLDERS_H
#define GST_HDDL_XLINK_PLACEHOLDERS_H

G_BEGIN_DECLS

enum xlink_dev_type {
        HOST_DEVICE = 0,        // used when communicating to either Remote or Local host
        VPUIP_DEVICE            // used when communicating to vpu ip
};

struct xlink_handle {
        uint32_t sw_device_id;                  // sw device id, identifies a device in the system
        enum xlink_dev_type dev_type;   // device type, used to determine direction of communication
};

enum xlink_opmode {
        RXB_TXB = 0,    // blocking read, blocking write
        RXN_TXN,                // non-blocking read, non-blocking write
        RXB_TXN,                // blocking read, non-blocking write
        RXN_TXB                 // non-blocking read, blocking write
};

enum xlink_device_power_mode {
        POWER_DEFAULT_NOMINAL_MAX = 0,  // no load reduction, default mode
        POWER_SUBNOMINAL_HIGH,                  // slight load reduction
        POWER_MEDIUM,                                   // average load reduction
        POWER_LOW,                                              // significant load reduction
        POWER_MIN,                                              // maximum load reduction
        POWER_SUSPENDED                                 // power off or device suspend
};

enum xlink_error {
        X_LINK_SUCCESS = 0,                                     // xlink operation completed successfully
        X_LINK_ALREADY_INIT,                            // xlink already initialized
        X_LINK_ALREADY_OPEN,                            // channel already open
        X_LINK_COMMUNICATION_NOT_OPEN,          // operation on a closed channel
        X_LINK_COMMUNICATION_FAIL,                      // communication failure
        X_LINK_COMMUNICATION_UNKNOWN_ERROR,     // communication failure unknown
        X_LINK_DEVICE_NOT_FOUND,                        // device specified not found
        X_LINK_TIMEOUT,                                         // operation timed out
        X_LINK_ERROR,                                           // parameter error
        X_LINK_CHAN_FULL                                        // channel has reached fill level
};

enum xlink_device_status {
        XLINK_DEV_OFF = 0,      // device is off
        XLINK_DEV_ERROR,        // device is busy and not available
        XLINK_DEV_BUSY,         // device is available for use
        XLINK_DEV_RECOVERY,     // device is in recovery mode
        XLINK_DEV_READY         // device HW failure is detected
};

enum xlink_error xlink_initialize (void);

enum xlink_error xlink_connect(struct xlink_handle *handle);

enum xlink_error xlink_open_channel(struct xlink_handle *handle,
                uint16_t chan, enum xlink_opmode mode, uint32_t data_size,
                uint32_t timeout);

enum xlink_error xlink_write_data(struct xlink_handle *handle,
                uint16_t chan, uint8_t const *message, uint32_t size);

enum xlink_error xlink_read_data(struct xlink_handle *handle,
                uint16_t chan, uint8_t **message, uint32_t *size);

enum xlink_error xlink_release_data(struct xlink_handle *handle,
                uint16_t chan, uint8_t * const data_addr);

enum xlink_error xlink_close_channel(struct xlink_handle *handle,
                uint16_t chan);

enum xlink_error xlink_disconnect(struct xlink_handle *handle);

G_END_DECLS
#endif /* GST_HDDL_XLINK_PLACEHOLDERS_H */
#endif

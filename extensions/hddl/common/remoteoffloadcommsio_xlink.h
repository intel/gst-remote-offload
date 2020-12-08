/*
 *  remoteoffloadcommsio_xlink.h - RemoteOffloadCommsXLinkIO object
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
#ifndef __REMOTEOFFLOAD_COMMS_IO_XLINK_H__
#define __REMOTEOFFLOAD_COMMS_IO_XLINK_H__

#include <glib-object.h>
#include <stdint.h>

#include <xlink.h>

G_BEGIN_DECLS

typedef uint16_t xlink_channel_id_t;

//xlink_open_channel timeout & data_size
#define XLINK_CHANNEL_TIMEOUT 10000
#define XLINK_DATA_SIZE 1024*1024*16

//Even though the channel is opened with XLINK_DATA_SIZE,
// we set a separate limit on the maximum size / chunk that
// is sent in a single call to xlink_write_data. The reason
// for this is, if XLINK_DATA_SIZE is viewed as the maximum
// number of bytes in-transit at any given time, we would
// introduce latency bubbles by also allowing buffers up-to
// that size to be sent... since we would need to wait
// for all bytes in-transit to get drained before sending
// the XLINK_DATA_SIZE chunk of data.
#define XLINK_MAX_SEND_SIZE 1024*1024*2

//total size of xlink-commsio owned coalesced send & receive buffers
#define COALESCED_CHUNK_SIZE 8*1024

//buffers less than this size will be coalesced.
#define COALESCE_THRESHOLD 2*1024

//maximum size of buffer that we are allowed to call
// xlink_write_control_data on.
#define XLINK_MAX_CONTROL_SIZE 100

//common macros for XLink device initialization
#define MAX_DEVICE_LIST_SIZE 8
#define INVALID_SW_DEVICE_ID 0xDEADBEEF

//XLink sw_device_id structure(32-bits):
//MSB: Byte4                | Byte 3        | Byte 2        | Byte 1                       LSB |
//[b31] ... |[b26][b25][b24]|[b23] ... [b16]|[b15] ... [ b8]|[b7][b6][b5][b4]|[b3][b2][b1]|[b0]|
// Reserved | InterfaceType | PhysicalID MSB| PhysicalID LSB| DeviceFamily   | Reserved   |Reserved
//
// Interface Type:
// 000b: IPC Interface
// 001b: PCIe Interface
// 010b: USB Interface
// 011b: Ethernet Interface
typedef enum
{
   SW_DEVICE_ID_IPC_INTERFACE = 0,
   SW_DEVICE_ID_PCIE_INTERFACE,
   SW_DEVICE_ID_USB_INTERFACE,
   SW_DEVICE_ID_ETH_INTERFACE
}SWDeviceIdInterfaceType;

#define SW_DEVICE_ID_INTERFACE_SHIFT 24U
#define SW_DEVICE_ID_INTERFACE_MASK  0x7
inline const SWDeviceIdInterfaceType GetInterfaceFromSWDeviceId(guint32 sw_device_id)
{
   return (SWDeviceIdInterfaceType)
    ((sw_device_id >> SW_DEVICE_ID_INTERFACE_SHIFT) & SW_DEVICE_ID_INTERFACE_MASK);
}

// Device Family:
// 0000b: KMB Device
typedef enum
{
   SW_DEVICE_ID_KMB_FAMILY = 0,
}SWDeviceIdFamilyType;

#define SW_DEVICE_ID_FAMILY_SHIFT 4U
#define SW_DEVICE_ID_FAMILY_MASK  0xF
inline const SWDeviceIdFamilyType GetFamilyFromSWDeviceId(guint32 sw_device_id)
{
   return (SWDeviceIdFamilyType)
    ((sw_device_id >> SW_DEVICE_ID_FAMILY_SHIFT) & SW_DEVICE_ID_FAMILY_MASK);
}

#define REMOTEOFFLOADCOMMSIOXLINK_TYPE (remote_offload_comms_io_xlink_get_type ())
G_DECLARE_FINAL_TYPE (RemoteOffloadCommsXLinkIO,
                      remote_offload_comms_io_xlink, REMOTEOFFLOAD, COMMSIOXLINK, GObject)

typedef enum
{
  XLINK_COMMSIO_TXMODE_NOPREFERENCE = 0,
  XLINK_COMMSIO_TXMODE_BLOCKING,
  XLINK_COMMSIO_TXMODE_NONBLOCKING,
} XLinkCommsIOTXMode;

typedef enum
{
   XLINK_COMMSIO_COALESCEMODE_DISABLE = 0,
   XLINK_COMMSIO_COALESCEMODE_ENABLE = 1
} XLinkCommsIOCoaleseMode;

RemoteOffloadCommsXLinkIO *remote_offload_comms_io_xlink_new (struct xlink_handle *deviceIdHandler,
                                                              xlink_channel_id_t channelId,
                                                              XLinkCommsIOTXMode tx_mode,
                                                              XLinkCommsIOCoaleseMode coalese_mode);


typedef void (*remote_offload_comms_io_xlink_channel_closed_f)(xlink_channel_id_t channelId,
                                                               void *user_data);
//Set optional callback that will be called immediately after the channel is closed.
// It's primary use is to notify an object of a closed channel so that it can be
//  put back into the pool of available channels.
void
remote_offload_comms_io_xlink_set_channel_closed_callback(RemoteOffloadCommsXLinkIO *commsio,
                                                          remote_offload_comms_io_xlink_channel_closed_f callback,
                                                          void *user_data);

G_END_DECLS

#endif /* __REMOTEOFFLOAD_COMMS_IO_XLINK_H__ */

/*
 *  gstingressegressdefs.h - Definitions shared between ingress & egress elements.
 *
 *  Copyright (C) 2020 Intel Corporation
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
#ifndef __GSTINGRESSEGRESSDEFS_H__
#define __GSTINGRESSEGRESSDEFS_H__

typedef enum
{
   TRANSFER_CODE_EGRESS_STREAM_START = 0x100,
   TRANSFER_CODE_EGRESS_STREAM_STOP = 0x200,
   TRANSFER_CODE_READY_TO_NULL_NOTIFY = 0x300,
} IngressEgressGenericTransferCode;

#endif

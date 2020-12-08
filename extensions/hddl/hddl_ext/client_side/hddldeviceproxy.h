/*
 *  hddldeviceproxy.h - HDDLDeviceProxy object
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

#ifndef __HDDLDEVICEPROXY_H__
#define __HDDLDEVICEPROXY_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define HDDLDEVICEPROXY_TYPE (hddl_device_proxy_get_type ())
G_DECLARE_FINAL_TYPE (HDDLDeviceProxy, hddl_device_proxy, DEVICEPROXY, HDDL, GObject)

HDDLDeviceProxy *hddl_device_proxy_new ();

G_END_DECLS



#endif /* __HDDLDEVICEPROXY_H__ */

/*
 *  remoteoffloadextensiondummy.c - RemoteOffloadExtensionDummy object
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

#include <gst/gst.h>
#include "remoteoffloadextension.h"
#include "dummydeviceproxy.h"
#include "remoteoffloaddeviceproxy.h"

#define REMOTEOFFLOADEXTENSIONDUMMY_TYPE (remote_offload_extension_dummy_get_type ())
G_DECLARE_FINAL_TYPE (RemoteOffloadExtensionDummy,
                      remote_offload_extension_dummy,
                      REMOTEOFFLOAD, EXTENSIONDUMMY, GObject)

struct _RemoteOffloadExtensionDummy
{
  GObject parent_instance;

};

GST_DEBUG_CATEGORY_STATIC (dummy_extension_debug);
#define GST_CAT_DEFAULT dummy_extension_debug

static void remote_offload_extension_dummy_interface_init (RemoteOffloadExtensionInterface *iface);

G_DEFINE_TYPE_WITH_CODE (RemoteOffloadExtensionDummy, remote_offload_extension_dummy, G_TYPE_OBJECT,
G_IMPLEMENT_INTERFACE (REMOTEOFFLOADEXTENSION_TYPE,
remote_offload_extension_dummy_interface_init)
GST_DEBUG_CATEGORY_INIT (dummy_extension_debug, "remoteoffloadextensiondummy", 0,
"debug category for RemoteOffloadExtensionDummy"))

static GArray *remote_offload_extension_dummy_generate(RemoteOffloadExtension *ext,
                                                       GType type)
{
   if( type == REMOTEOFFLOADDEVICEPROXY_TYPE )
   {
      GArray *commschannelarray = g_array_new(FALSE, FALSE, sizeof(RemoteOffloadExtTypePair));

      DummyDeviceProxy *proxy = dummy_device_proxy_new();
      RemoteOffloadExtTypePair pair = {"dummy", (GObject *)proxy};
      if( proxy )
      {
         g_array_append_val(commschannelarray, pair);
         return commschannelarray;
      }
      else
      {
         GST_ERROR_OBJECT(ext, "dummy_device_proxy_new failed");
         g_array_free(commschannelarray, TRUE);
      }
   }

   return NULL;
}

static void
remote_offload_extension_dummy_interface_init (RemoteOffloadExtensionInterface *iface)
{
  iface->generate = remote_offload_extension_dummy_generate;
}

static void
remote_offload_extension_dummy_class_init (RemoteOffloadExtensionDummyClass *klass)
{

}

static void
remote_offload_extension_dummy_init (RemoteOffloadExtensionDummy *self)
{

}

__attribute__ ((visibility ("default"))) RemoteOffloadExtension* remoteoffload_extension_entry();

RemoteOffloadExtension* remoteoffload_extension_entry()
{
   return g_object_new(REMOTEOFFLOADEXTENSIONDUMMY_TYPE, NULL);
}



/*
 *  remoteoffloadextensiongva.c - RemoteOffloadExtensionGVA object
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
#include <gst/gst.h>
#include "remoteoffloadextension.h"

#include "remoteoffloadmetaserializer.h"
#ifdef HAVE_GVA_META_API
#include "gvajsonmetaserializer.h"
#include "gvatensormetaserializer.h"
#ifdef HAVE_GVA_AUDIO_META_API
#include "gvaaudioeventmetaserializer.h"
#endif
#endif

#include "remoteoffloadelementpropertyserializer.h"
#include "gvaelementpropertyserializer.h"

#define REMOTEOFFLOADEXTENSIONGVA_TYPE (remote_offload_extension_gva_get_type ())
G_DECLARE_FINAL_TYPE (RemoteOffloadExtensionGVA,
                      remote_offload_extension_gva, REMOTEOFFLOAD, EXTENSIONGVA, GObject)

struct _RemoteOffloadExtensionGVA
{
  GObject parent_instance;

};

GST_DEBUG_CATEGORY_STATIC (gva_extension_debug);
#define GST_CAT_DEFAULT gva_extension_debug

static void remote_offload_extension_gva_interface_init (RemoteOffloadExtensionInterface *iface);

G_DEFINE_TYPE_WITH_CODE (RemoteOffloadExtensionGVA, remote_offload_extension_gva, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (REMOTEOFFLOADEXTENSION_TYPE,
                         remote_offload_extension_gva_interface_init)
                         GST_DEBUG_CATEGORY_INIT (gva_extension_debug,
                         "remoteoffloadextensiongva", 0,
                         "debug category for RemoteOffloadExtensionGVA"))


static GArray *remote_offload_extension_gva_generate(RemoteOffloadExtension *ext,
                                                     GType type)
{
#ifdef HAVE_GVA_META_API
   if( type == REMOTEOFFLOADMETASERIALIZER_TYPE )
   {
      GArray *pairarray = g_array_new(FALSE, FALSE, sizeof(RemoteOffloadExtTypePair));

      RemoteOffloadMetaSerializer *pGvaTensorSerializer =
            (RemoteOffloadMetaSerializer *)gva_tensor_metaserializer_new();
      {
         RemoteOffloadExtTypePair pair = {remote_offload_meta_api_name(pGvaTensorSerializer),
                                          (GObject *)pGvaTensorSerializer};
         g_array_append_val(pairarray, pair);
      }

      RemoteOffloadMetaSerializer *pGvaJSONMetaSerializer =
            (RemoteOffloadMetaSerializer *)gva_json_metaserializer_new();
      {
         RemoteOffloadExtTypePair pair = {remote_offload_meta_api_name(pGvaJSONMetaSerializer),
                                          (GObject *)pGvaJSONMetaSerializer};
         g_array_append_val(pairarray, pair);
      }

#ifdef HAVE_GVA_AUDIO_META_API
      RemoteOffloadMetaSerializer *pGvaAudioEventMetaSerializer =
            (RemoteOffloadMetaSerializer *)gva_audioevent_metaserializer_new();
      {
         RemoteOffloadExtTypePair pair = {remote_offload_meta_api_name(pGvaAudioEventMetaSerializer),
                                          (GObject *)pGvaAudioEventMetaSerializer};
         g_array_append_val(pairarray, pair);
      }
#endif

      return pairarray;
   }
   else
#endif
   if( type == REMOTEOFFLOADELEMENTPROPERTYSERIALIZER_TYPE )
   {
      GArray *pairarray = g_array_new(FALSE, FALSE, sizeof(RemoteOffloadExtTypePair));

      GObject *pGvaElementPropSerializer = (GObject *)gva_element_property_serializer_new();

      {
         RemoteOffloadExtTypePair pair = {"gvadetect", pGvaElementPropSerializer};
         g_array_append_val(pairarray, pair);
      }

      {
         RemoteOffloadExtTypePair pair = {"gvainference", g_object_ref(pGvaElementPropSerializer)};
         g_array_append_val(pairarray, pair);
      }

      {
         RemoteOffloadExtTypePair pair = {"gvaclassify", g_object_ref(pGvaElementPropSerializer)};
         g_array_append_val(pairarray, pair);
      }

      {
         RemoteOffloadExtTypePair pair = {"gvaidentify", g_object_ref(pGvaElementPropSerializer)};
         g_array_append_val(pairarray, pair);
      }

      {
         RemoteOffloadExtTypePair pair = {"gvaaudiodetect", g_object_ref(pGvaElementPropSerializer)};
         g_array_append_val(pairarray, pair);
      }


      return pairarray;
   }

   return NULL;
}

static void
remote_offload_extension_gva_interface_init (RemoteOffloadExtensionInterface *iface)
{
  iface->generate = remote_offload_extension_gva_generate;
}

static void
remote_offload_extension_gva_class_init (RemoteOffloadExtensionGVAClass *klass)
{

}

static void
remote_offload_extension_gva_init (RemoteOffloadExtensionGVA *self)
{

}

__attribute__ ((visibility ("default"))) RemoteOffloadExtension* remoteoffload_extension_entry();

RemoteOffloadExtension* remoteoffload_extension_entry()
{
   return g_object_new(REMOTEOFFLOADEXTENSIONGVA_TYPE, NULL);
}

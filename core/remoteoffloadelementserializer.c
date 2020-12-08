/*
 *  remoteoffloadelementserializer.c - RemoteOffloadElementSerializer object
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
#ifndef NO_SAFESTR
  #include <safe_mem_lib.h>
#endif
#include "remoteoffloadelementserializer.h"
#include "remoteoffloadelementpropertyserializer.h"
#include "remoteoffloadextregistry.h"

struct _RemoteOffloadElementSerializer
{
  GObject parent_instance;

  GHashTable *elementPropertySerializerHash;
  RemoteOffloadExtRegistry *ext_registry;
};

GST_DEBUG_CATEGORY_STATIC (gst_remoteoffload_elementserializer_debug);
#define GST_CAT_DEFAULT gst_remoteoffload_elementserializer_debug

G_DEFINE_TYPE_WITH_CODE (RemoteOffloadElementSerializer,
                         remote_offload_element_serializer, G_TYPE_OBJECT,
GST_DEBUG_CATEGORY_INIT (gst_remoteoffload_elementserializer_debug,
                         "remoteoffloadelementserializer", 0,
                         "debug category for remoteoffloadelementserializer"))

static void
remote_offload_element_serializer_finalize (GObject *object)
{
  RemoteOffloadElementSerializer *self = REMOTEOFFLOAD_ELEMENTSERIALIZER(object);

  g_hash_table_destroy(self->elementPropertySerializerHash);
  remote_offload_ext_registry_unref(self->ext_registry);

  G_OBJECT_CLASS (remote_offload_element_serializer_parent_class)->finalize (object);
}

static void
remote_offload_element_serializer_class_init (RemoteOffloadElementSerializerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = remote_offload_element_serializer_finalize;
}

static void KeyDestroyNotify(gpointer data)
{
  g_free(data);
}

static void ValueDestroyNotify(gpointer data)
{
  g_object_unref((GObject *)data);
}

static void
remote_offload_element_serializer_init (RemoteOffloadElementSerializer *self)
{
  self->elementPropertySerializerHash =
        g_hash_table_new_full(g_str_hash, g_str_equal, KeyDestroyNotify, ValueDestroyNotify);

  //TODO: move to constructed method
  self->ext_registry = remote_offload_ext_registry_get_instance();

  GArray *pairarray =
        remote_offload_ext_registry_generate(self->ext_registry,
                                             REMOTEOFFLOADELEMENTPROPERTYSERIALIZER_TYPE);

  if( pairarray )
  {
     for( guint i = 0;  i < pairarray->len; i++ )
     {
        RemoteOffloadExtTypePair *pair = &g_array_index(pairarray, RemoteOffloadExtTypePair, i);

        if( REMOTEOFFLOAD_IS_ELEMENTPROPERTYSERIALIZER(pair->obj) )
        {
           RemoteOffloadElementPropertySerializer *propserializer =
                 (RemoteOffloadElementPropertySerializer *)pair->obj;
           if( pair->name )
           {
              GST_DEBUG_OBJECT (self,
                               "Registering property serializer for element type=%s ",
                               pair->name);
              g_hash_table_insert(self->elementPropertySerializerHash,
                                  g_strdup (pair->name), propserializer);
           }
           else
           {
              GST_WARNING_OBJECT (self,
                                  "remote_offload_ext_registry returned pair with name=NULL\n");
           }
        }
        else
        {
           GST_WARNING_OBJECT (self,
                               "remote_offload_ext_registry returned invalid "
                               "RemoteOffloadElementPropertySerializer\n");
        }
     }

     g_array_free(pairarray, TRUE);
  }

}

typedef struct _ElementPropertyDescriptionHeader
{
   gchar propertyname[128];
   gchar propertytypename[128];
   guint64 property_size;  //the total size (in bytes) of this specific property
   guint memBlockStartIndex;
   guint nMemBlocks;
}ElementPropertyDescriptionHeader;

static inline void write_to_bw(GstByteWriter *bw,
                               ElementDescriptionHeader *header,
                               ElementPropertyDescriptionHeader *property_description_header,
                               const guint8 *data)
{
  gst_byte_writer_put_data (bw,
                            (const guint8 *)property_description_header,
                            sizeof(ElementPropertyDescriptionHeader));

  if( property_description_header->property_size && data )
  {
     gst_byte_writer_put_data (bw, data, property_description_header->property_size);
  }

  header->nproperties++;
}

static void SerializedDestroy(gpointer data)
{
   g_free(data);
}

gboolean remote_offload_serialize_element(RemoteOffloadElementSerializer *serializer,
                                          gint32 id,
                                          GstElement *pElement,
                                          ElementDescriptionHeader *headeroutput,
                                          GArray *propMemBlocks)
{
  if( !REMOTEOFFLOAD_IS_ELEMENTSERIALIZER(serializer) ||
                                          !pElement ||
                                          !headeroutput ||
                                          !propMemBlocks )
    return FALSE;



  //initialize the output header
  headeroutput->factoryname[0] = 0;
  headeroutput->elementname[0] = 0;
  headeroutput->id = -1;
  headeroutput->nproperties = 0;

  //Put the element's factory name (i.e. "capsfilter") into the header
  gchar *name = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(gst_element_get_factory(pElement)));
  if( G_UNLIKELY(g_snprintf(headeroutput->factoryname,
                 sizeof(headeroutput->factoryname),
                 "%s", name) > sizeof(headeroutput->factoryname)) )
  {
     //this is an actual error case. If the factory name of the element is too big to be serialized,
     //  then we certainly won't be able to recreate on the remote-side using the partial name.
     // throw an error message and get outta here.
     GST_ERROR_OBJECT (serializer,
                       "Element factory name %s is too long to fit "
                       "ElementPropertyDescriptionHeader.factoryname (%"G_GSIZE_FORMAT" bytes)",
                       name, sizeof(headeroutput->factoryname));
     return FALSE;
  }

  //Put the element's name (i.e. "mycapsfilter") into the header
  gchar *elementname = GST_ELEMENT_NAME(pElement);
  if( G_UNLIKELY(g_snprintf(headeroutput->elementname,
                    sizeof(headeroutput->elementname),
                    "%s", elementname) > sizeof(headeroutput->elementname)) )
  {
     //it's debatable whether this should be a warning and continue case, or
     // a failure case. Let's lean toward failing early with a clear reason,
     // rather than possibly triggering some obscure error
     // later on as a result of not actually being able to recreate this
     // element on the remote-side with the same name that was assigned to this element
     // from the user/host. The latter would be really hard to track down.
     GST_ERROR_OBJECT (serializer, "Element name %s is too long to fit "
                      "ElementPropertyDescriptionHeader.elementname (%"G_GSIZE_FORMAT" bytes)",
                      elementname, sizeof(headeroutput->elementname));
     return FALSE;
  }

  {
     //add a placeholder
     GstMemory *placeholder = NULL;
     g_array_append_val(propMemBlocks, placeholder);
  }

  GST_DEBUG_OBJECT(serializer, "%s(%s)", name, elementname);

  headeroutput->id = id;

  //initialize the number of properties & the total bytes of the
  // properties description size to 0.
  headeroutput->nproperties = 0;

  GstByteWriter tmpbw;
  gst_byte_writer_init (&tmpbw);

  //Note: the following code was developed by heavily utilizing the code
  // found within gstreamer's "print_object_properties_info()" impl.
  guint num_properties;
  GParamSpec **property_specs;

  property_specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (pElement), &num_properties);
  GST_DEBUG_OBJECT(serializer, "num_propertes=%u", num_properties);
  for( guint i = 0; i < num_properties; i++ )
  {
     GParamSpec *param = property_specs[i];

     GValue value = { 0, };
     g_value_init (&value, param->value_type);

     //No need to serialize the standard "parent" parameter
     if( g_strcmp0(g_param_spec_get_name (param), "parent") == 0)
       continue;

     //we already captured the name of the object
     if( g_strcmp0(g_param_spec_get_name (param), "name") == 0)
       continue;

     //if it's not readable, well, we can't copy it's contents to the serialized buffer
     gboolean readable = ! !(param->flags & G_PARAM_READABLE);
     if( !readable ) continue;

     //if it's not writable, the target side won't be able to write it
     gboolean writable = ! !(param->flags & G_PARAM_WRITABLE);
     if( !writable ) continue;

     //obtain the name of the parameter
     const gchar * param_name = g_param_spec_get_name (param);

     ElementPropertyDescriptionHeader property_description_header;

     if( G_UNLIKELY(g_snprintf(property_description_header.propertyname,
                    sizeof(property_description_header.propertyname),
                    "%s", param_name) > sizeof(property_description_header.propertyname)) )
     {
        GST_WARNING_OBJECT (serializer,
                            "Parameter name %s is too long to fit "
                            "ElementPropertyDescriptionHeader.propertyname "
                            "(%"G_GSIZE_FORMAT" bytes).. skipping",
                            param_name, sizeof(property_description_header.propertyname));
        continue;
     }

     const gchar * param_type_name = G_VALUE_TYPE_NAME(&value);

     if( G_UNLIKELY(g_snprintf(property_description_header.propertytypename,
                    sizeof(property_description_header.propertytypename),
                    "%s", param_type_name) > sizeof(property_description_header.propertytypename)) )
     {
        GST_WARNING_OBJECT (serializer,
                            "Property Type name %s is too long to fit "
                            "ElementPropertyDescriptionHeader.propertytypename "
                            "(%"G_GSIZE_FORMAT" bytes).. skipping",
                            param_type_name, sizeof(property_description_header.propertytypename));
        continue;
     }

     GST_DEBUG_OBJECT(serializer, "%u: param_name=%s, param_type_name=%s",
                      i, param_name, param_type_name);

     property_description_header.property_size = 0;
     property_description_header.memBlockStartIndex = 0;
     property_description_header.nMemBlocks = 1;

     //Try to obtain a custom property serializer for this element
     RemoteOffloadElementPropertySerializer *propserializer =
        (RemoteOffloadElementPropertySerializer *)g_hash_table_lookup(
                                                      serializer->elementPropertySerializerHash,
                                                      headeroutput->factoryname);
     if( propserializer )
     {
        GArray *serializedPropMemArray = g_array_new(FALSE, FALSE, sizeof(GstMemory *));

        RemoteOffloadPropertySerializerReturn ret =
              remote_offload_element_property_serialize(propserializer,
                                                        pElement,
                                                        param_name,
                                                        serializedPropMemArray);
        switch(ret)
        {
           case REMOTEOFFLOAD_PROPSERIALIZER_FAILURE:
              GST_ERROR_OBJECT(serializer,
                               "Error in remote_offload_element_property_serialize "
                               "for element %s(%s), property %s",
                               headeroutput->factoryname, headeroutput->elementname, param_name);
              g_array_free(serializedPropMemArray, TRUE);
              return FALSE;
           break;

           case REMOTEOFFLOAD_PROPSERIALIZER_SUCCESS:
           {
              property_description_header.memBlockStartIndex = propMemBlocks->len;
              property_description_header.nMemBlocks = serializedPropMemArray->len;
              write_to_bw(&tmpbw, headeroutput, &property_description_header, NULL);

              for( guint memi = 0; memi < serializedPropMemArray->len; memi++ )
              {
                 GstMemory *propMem = g_array_index(serializedPropMemArray, GstMemory *, memi);

                 if( !propMem )
                 {
                    GST_ERROR_OBJECT(serializer,
                                     "remote_offload_element_property_serialize for element %s(%s),"
                                     " property %s added GstMemory block of NULL "
                                     "(propMemBlocks index %u)\n",
                                     headeroutput->factoryname, headeroutput->elementname,
                                     param_name, memi);
                    g_array_free(serializedPropMemArray, TRUE);
                    return FALSE;
                 }

                 g_array_append_val(propMemBlocks, propMem);
              }

           }
           break;

           case REMOTEOFFLOAD_PROPSERIALIZER_DEFER:
           {
              //in this case, just continue on as if we didn't find a custom
              // property serializer in the first place.
              if( serializedPropMemArray->len )
              {
                 GST_WARNING_OBJECT(serializer,
                                    "remote_offload_element_property_serialize for element %s(%s), "
                                    "property %s returned REMOTEOFFLOAD_PROPSERIALIZER_DEFER, "
                                    "but still appended GstMemory blocks to propMemBlocks\n",
                                    headeroutput->factoryname, headeroutput->elementname,
                                    param_name);
              }
           }
           break;
        }

        g_array_free(serializedPropMemArray, TRUE);

        if( ret == REMOTEOFFLOAD_PROPSERIALIZER_SUCCESS )
           continue;
     }


     g_object_get_property (G_OBJECT (pElement), param->name, &value);


     //TODO: Refactor to use G_VALUE_TYPE_NAME. We should have a registered set of
     // lightweight classes or functors to serialize/deserialize a given parameter value
     switch (G_VALUE_TYPE (&value))
     {

        case G_TYPE_STRING:
        {
           const char *string_val = g_value_get_string (&value);
           if( string_val )
           {
             property_description_header.property_size =
#ifndef NO_SAFESTR
                   strnlen_s(string_val, RSIZE_MAX_STR) + 1;
#else
                   strlen(string_val) + 1;
#endif
           }
           else
           {
             property_description_header.property_size = 0;
           }

           write_to_bw(&tmpbw,
                       headeroutput,
                       &property_description_header,
                       (const guint8 *)string_val);

        }
        break;

        case G_TYPE_BOOLEAN:
        {
           gboolean val = g_value_get_boolean (&value);
           property_description_header.property_size = sizeof(val);
           write_to_bw(&tmpbw, headeroutput, &property_description_header, (const guint8 *)&val);
        }
        break;

        case G_TYPE_ULONG:
        {
           gulong val = g_value_get_ulong (&value);
           property_description_header.property_size = sizeof(val);
           write_to_bw(&tmpbw, headeroutput, &property_description_header, (const guint8 *)&val);
        }
        break;

        case G_TYPE_LONG:
        {
           glong val = g_value_get_long (&value);
           property_description_header.property_size = sizeof(val);
           write_to_bw(&tmpbw, headeroutput, &property_description_header, (const guint8 *)&val);
        }
        break;

        case G_TYPE_UINT:
        {
           guint val = g_value_get_uint (&value);
           property_description_header.property_size = sizeof(val);
           write_to_bw(&tmpbw, headeroutput, &property_description_header, (const guint8 *)&val);
        }
        break;

        case G_TYPE_INT:
        {
           gint val = g_value_get_int (&value);
           property_description_header.property_size = sizeof(val);
           write_to_bw(&tmpbw, headeroutput, &property_description_header, (const guint8 *)&val);
        }
        break;

        case G_TYPE_UINT64:
        {
           guint64 val = g_value_get_uint64 (&value);
           property_description_header.property_size = sizeof(val);
           write_to_bw(&tmpbw, headeroutput, &property_description_header, (const guint8 *)&val);
        }
        break;

        case G_TYPE_INT64:
        {
           gint64 val = g_value_get_int64 (&value);
           property_description_header.property_size = sizeof(val);
           write_to_bw(&tmpbw, headeroutput, &property_description_header, (const guint8 *)&val);
        }
        break;

        case G_TYPE_FLOAT:
        {
           gfloat val = g_value_get_float (&value);
           property_description_header.property_size = sizeof(val);
           write_to_bw(&tmpbw, headeroutput, &property_description_header, (const guint8 *)&val);
        }
        break;

        case G_TYPE_DOUBLE:
        {
           gdouble val = g_value_get_double (&value);
           property_description_header.property_size = sizeof(val);
           write_to_bw(&tmpbw, headeroutput, &property_description_header, (const guint8 *)&val);
        }
        break;

        default:
        {
           if( param->value_type == GST_TYPE_CAPS )
           {
              const GstCaps *caps = gst_value_get_caps (&value);
              gchar *capsstr = NULL;
              if( !caps )
              {
                 property_description_header.property_size = 0;
              }
              else
              {
                 capsstr = gst_caps_to_string(caps);
                 if( capsstr )
                 {
                    property_description_header.property_size =
#ifndef NO_SAFESTR
                          strnlen_s(capsstr, RSIZE_MAX_STR) + 1;
#else
                          strlen(capsstr) + 1;
#endif
                 }
                 else
                 {
                    property_description_header.property_size = 0;
                 }
              }

              write_to_bw(&tmpbw,
                          headeroutput,
                          &property_description_header,
                          (const guint8 *)capsstr);

           }
           else
           if(G_IS_PARAM_SPEC_ENUM (param))
           {
              gint enum_value;
              enum_value = g_value_get_enum (&value);

              //we need to overwrite the propertytypename to "enum", as it is currently
              // set to the typename of the enum.
              g_stpcpy(property_description_header.propertytypename, "enum");

              property_description_header.property_size = sizeof(enum_value);

              write_to_bw(&tmpbw,
                          headeroutput,
                          &property_description_header,
                          (const guint8 *)&enum_value);
           }
           else
           {
              GST_WARNING_OBJECT(serializer, "Unknown type %s... skipping", param_type_name);
           }
        }
        break;

     }
  }

  g_free(property_specs);

  gsize mem_size = gst_byte_writer_get_pos(&tmpbw);

  //it's possible that this element didn't have any properties, or that
  // we didn't serialize any. If this is the case, the size written
  // into our bytewriter will be 0.. don't try to create a GstMemory block
  // from it.
  if( mem_size )
  {
     void *bwdata = gst_byte_writer_reset_and_get_data(&tmpbw);
     GstMemory *mem = gst_memory_new_wrapped((GstMemoryFlags)0,
                                              bwdata,
                                              mem_size,
                                              0,
                                              mem_size,
                                              bwdata,
                                              SerializedDestroy);

     g_array_index(propMemBlocks, GstMemory *, 0) = mem;
  }
  else
  {
     //remove the placeholder we had added at the start
     g_array_set_size(propMemBlocks, 0);
  }


  return TRUE;
}

GstElement* remote_offload_deserialize_element(RemoteOffloadElementSerializer *serializer,
                                               const ElementDescriptionHeader *header,
                                               GArray *propMemBlocks)
{
  if( !REMOTEOFFLOAD_IS_ELEMENTSERIALIZER(serializer) || !header || !propMemBlocks )
    return NULL;

  GstElement *pElement = gst_element_factory_make(header->factoryname, header->elementname );
  if( !pElement )
  {
     GST_ERROR_OBJECT(serializer, "Error creating element %s", header->elementname);
     return NULL;
  }

  GstMemory **propmemarray = (GstMemory **)propMemBlocks->data;

  //if the element has not serialized any properties, we
  // can return the element right now.
  if( !header->nproperties )
     return pElement;

  if( propMemBlocks->len < 1 )
  {
     GST_ERROR_OBJECT(serializer, "propMemBlocks->len < 1");
     return NULL;
  }

  GstMapInfo propMap;
  if( !gst_memory_map (propmemarray[0], &propMap, GST_MAP_READ) )
  {
      GST_ERROR_OBJECT (serializer, "Error mapping property mem block 0");
      return NULL;
  }

  guint8 *pDescription = propMap.data;

  GST_DEBUG_OBJECT(serializer, "header->nproperties = %d", header->nproperties);

  //Try to obtain a custom property serializer for this element
  RemoteOffloadElementPropertySerializer *propserializer =
     (RemoteOffloadElementPropertySerializer *)g_hash_table_lookup(
                                                 serializer->elementPropertySerializerHash,
                                                 header->factoryname);
  //for each property
  for( gint32 i = 0; i < header->nproperties; i++ )
  {
     ElementPropertyDescriptionHeader *propHeader =
           (ElementPropertyDescriptionHeader *)pDescription;
     pDescription += sizeof(ElementPropertyDescriptionHeader);

     GST_DEBUG_OBJECT(serializer,
                      "%d: propHeader->propertyname = %s", i, propHeader->propertyname);
     GST_DEBUG_OBJECT(serializer,
                      "%d: propHeader->propertytypename = %s", i, propHeader->propertytypename);
     GST_DEBUG_OBJECT(serializer,
                      "%d: propHeader->property_size = %"G_GUINT64_FORMAT,
                      i, propHeader->property_size);

     if( propserializer && (propHeader->memBlockStartIndex > 0) )
     {
        GArray *serializedPropMemArray = g_array_new(FALSE, FALSE, sizeof(GstMemory *));

        for( guint memi = 0; memi < propHeader->nMemBlocks; memi++ )
        {
           GstMemory *propMem = g_array_index(propMemBlocks,
                                              GstMemory *, propHeader->memBlockStartIndex + memi);
           g_array_append_val(serializedPropMemArray, propMem);
        }

        RemoteOffloadPropertySerializerReturn ret =
              remote_offload_element_property_deserialize(propserializer,
                                                          pElement,
                                                          propHeader->propertyname,
                                                          serializedPropMemArray);
        switch(ret)
        {
           case REMOTEOFFLOAD_PROPSERIALIZER_FAILURE:
             GST_ERROR_OBJECT(serializer,
                              "remote_offload_element_property_deserialize for element %s(%s), "
                              "property %s returned REMOTEOFFLOAD_PROPSERIALIZER_FAILURE\n",
                              header->factoryname, header->elementname, propHeader->propertyname);
           break;

           case REMOTEOFFLOAD_PROPSERIALIZER_SUCCESS:
             GST_DEBUG_OBJECT(serializer,
                              "remote_offload_element_property_deserialize for element %s(%s), "
                              "property %s returned REMOTEOFFLOAD_PROPSERIALIZER_SUCCESS\n",
                              header->factoryname, header->elementname, propHeader->propertyname);
           break;

           case REMOTEOFFLOAD_PROPSERIALIZER_DEFER:
            GST_WARNING_OBJECT(serializer,
                               "remote_offload_element_property_deserialize for element %s(%s), "
                               "property %s returned REMOTEOFFLOAD_PROPSERIALIZER_DEFER\n",
                               header->factoryname, header->elementname, propHeader->propertyname);
           break;
        }

        g_array_free(serializedPropMemArray, TRUE);
        continue;
     }

     //attempt to convert the propertypename to a GType
     GType propertyType;

     //for some reason, g_type_from_name("enum") doesn't work, so we
     // need to add a specific check for that.
     if( g_strcmp0(propHeader->propertytypename, "enum") == 0)
     {
        //enum is just an int, so it can be set like one
        propertyType = G_TYPE_INT;
     }
     else
     {
        propertyType = g_type_from_name(propHeader->propertytypename);
     }

     if( propertyType )
     {
        switch(propertyType)
        {
           case G_TYPE_STRING:
           {
              if( propHeader->property_size > 0)
              {
                 gchar *pval = (gchar *)pDescription;
                 g_object_set (pElement, propHeader->propertyname, pval, NULL);
              }
           }
           break;

           case G_TYPE_BOOLEAN:
           {
              gboolean *pval = (gboolean *)pDescription;
              g_object_set (pElement, propHeader->propertyname, *pval, NULL);
           }
           break;

           case G_TYPE_ULONG:
           {
              gulong *pval = (gulong *)pDescription;
              g_object_set (pElement, propHeader->propertyname, *pval, NULL);
           }
           break;

           case G_TYPE_LONG:
           {
              glong *pval = (glong *)pDescription;
              g_object_set (pElement, propHeader->propertyname, *pval, NULL);
           }
           break;

           case G_TYPE_UINT:
           {
              guint *pval = (guint *)pDescription;
              g_object_set (pElement, propHeader->propertyname, *pval, NULL);
           }
           break;

           case G_TYPE_INT:
           {
              gint *pval = (gint *)pDescription;
              g_object_set (pElement, propHeader->propertyname, *pval, NULL);
           }
           break;

           case G_TYPE_UINT64:
           {
              guint64 *pval = (guint64 *)pDescription;
              g_object_set (pElement, propHeader->propertyname, *pval, NULL);
           }
           break;

           case G_TYPE_INT64:
           {
              gint64 *pval = (gint64 *)pDescription;
              g_object_set (pElement, propHeader->propertyname, *pval, NULL);
           }
           break;

           case G_TYPE_FLOAT:
           {
              gfloat *pval = (gfloat *)pDescription;
              g_object_set (pElement, propHeader->propertyname, *pval, NULL);
           }
           break;

           case G_TYPE_DOUBLE:
           {
              gdouble *pval = (gdouble *)pDescription;
              g_object_set (pElement, propHeader->propertyname, *pval, NULL);
           }
           break;

           default:
           {
              if( propertyType == GST_TYPE_CAPS )
              {
                 GstCaps *caps = gst_caps_from_string((gchar *)pDescription);
                 if( !caps )
                 {
                    GST_ERROR_OBJECT(serializer, "gst_caps_from_string(str) failed!");
                    GST_ERROR_OBJECT(serializer, "str = %s\n", (gchar *)pDescription);
                    return NULL;
                 }
                 g_object_set (pElement, propHeader->propertyname, caps, NULL);
                 gst_caps_unref (caps);
              }
              else
              {
                 GST_WARNING_OBJECT(serializer,
                                    "No deserializer method available for type=%s for property %s"
                                    "... skipping",
                                    propHeader->propertytypename, propHeader->propertyname);
              }
           }
           break;
        }
     }
     else
     {
        GST_WARNING_OBJECT(serializer,
                           "g_type_from_name(\"%s\") failed for property %s... skipping.",
                           propHeader->propertytypename, propHeader->propertyname);
     }

     pDescription += propHeader->property_size;
  }

  gst_memory_unmap (propmemarray[0], &propMap);

  return pElement;
}

RemoteOffloadElementSerializer *remote_offload_element_serializer_new()
{
   return g_object_new(REMOTEOFFLOADELEMENTSERIALIZER_TYPE, NULL);
}

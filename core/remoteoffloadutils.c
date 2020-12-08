/*
 *  remoteoffloadutils.c - Various utility functions that may be
 *    useful throughout the remote offload stack
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
#include "remoteoffloadutils.h"

static void destroy_garray_element(gpointer data)
{
   GstElement *pElement = *((GstElement **)data);
   gst_object_unref(pElement);
}

GArray *gst_bin_get_by_factory_type(GstBin *bin, gchar **factory_name_array)
{
   if( !GST_IS_BIN(bin))
      return NULL;

   GHashTable *compare_hash =
         g_hash_table_new(g_str_hash,
                          g_str_equal);
   if( !compare_hash )
   {
      GST_ERROR("Couldn't create hash table");
      return NULL;
   }

   {
      guint arrayi = 0;
      while(factory_name_array[arrayi])
      {
         gchar *factoryname = factory_name_array[arrayi];
         g_hash_table_add(compare_hash, factoryname);
         arrayi++;
      }
   }

   GArray *elem_list = g_array_new(FALSE,
                                   FALSE,
                                   sizeof(GstElement *));
   g_array_set_clear_func(elem_list, destroy_garray_element);

   gboolean berror = FALSE;
   GstIterator *it = gst_bin_iterate_recurse(bin);
   if( it )
   {
      GValue item = G_VALUE_INIT;
      gboolean done = FALSE;
      while(!done)
      {
         switch(gst_iterator_next(it, &item))
         {
            case GST_ITERATOR_OK:
            {
               GstElement *pElem = (GstElement *)g_value_get_object(&item);
               if( pElem )
               {
                  gchar *name =
                        gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(gst_element_get_factory(pElem)));

                  if( g_hash_table_contains(compare_hash, name) )
                  {
                     gst_object_ref(pElem);
                     g_array_append_val(elem_list, pElem);
                  }

                  g_value_reset(&item);
               }
               else
               {
                  berror = TRUE;
                  done = TRUE;
               }
            }
            break;

            case GST_ITERATOR_DONE:
               done = TRUE;
            break;

            case GST_ITERATOR_RESYNC:
               berror = TRUE;
               done = TRUE;
            break;

            case GST_ITERATOR_ERROR:
               berror = TRUE;
               done = TRUE;
            break;
         }
      }

      g_value_unset(&item);
      gst_iterator_free(it);
   }
   else
   {
      berror = TRUE;
   }

   if( berror )
   {
      g_array_free(elem_list, TRUE);
      elem_list = NULL;
   }

   g_hash_table_destroy(compare_hash);

   return elem_list;
}


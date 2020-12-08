/*
 *  querydataexchanger.c - QueryDataExchanger object
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
#else
  #include <string.h>
#endif
#include "querydataexchanger.h"
enum
{
  PROP_CALLBACK = 1,
  N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

struct _QueryDataExchanger
{
  RemoteOffloadDataExchanger parent_instance;

  /* Other members, including private data. */
  QueryDataExchangerCallback *callback;

};

GST_DEBUG_CATEGORY_STATIC (query_data_exchanger_debug);
#define GST_CAT_DEFAULT query_data_exchanger_debug

G_DEFINE_TYPE_WITH_CODE (QueryDataExchanger, query_data_exchanger, REMOTEOFFLOADDATAEXCHANGER_TYPE,
GST_DEBUG_CATEGORY_INIT (query_data_exchanger_debug, "remoteoffloadquerydataexchanger", 0,
  "debug category for remoteoffloadquerydataexchanger"))

static GQuark QUARK_QUERY_RESPONSE_ID;

enum
{
   QUERYEXCHANGE_HEADER = 0,
   QUERYEXCHANGE_STRUCTSTRING,
   QUERYEXCHANGE_TOTAL_DATA_SEGMENTS
};

typedef struct _QueryExchangeHeader
{
   GstQueryType queryType;

   gboolean queryResult;
}QueryExchangeHeader;

static inline GstQuery* query_from_string(GstQueryType type, gchar *str, gint strlen)
{
   GstQuery *query = NULL;
   GstStructure *structure = NULL;
   gchar *end;

   //Sanity checks on the string contents.
   if( (str && (strlen > 0)) && !str[strlen-1] && str[0])
   {
      structure = gst_structure_from_string ((const char *) str, &end);
   }
   else
   {
      GST_ERROR("Sanity check on structure string failed");
      return NULL;
   }

   query = gst_query_new_custom (type, structure);

   if (GST_QUERY_TYPE (query) == GST_QUERY_CAPS)
   {

     GstCaps *filter;
     gst_query_parse_caps (query, &filter);
     if (filter
         && !g_strcmp0 (gst_structure_get_name (gst_caps_get_structure (filter, 0)),
                        "NULL"))
     {
       GstCaps *capsresult;
       gst_query_parse_caps_result (query, &capsresult);

       GstQuery *new_query = gst_query_new_caps (NULL);
       gst_query_set_caps_result(new_query, capsresult);
       gst_query_unref (query);
       query = new_query;
     }
   }

   return query;
}

static inline gchar* query_to_string(GstQuery *query, gssize *query_string_len)
{
   gchar *query_string = NULL;
   *query_string_len = 0;
   if( !query )
   {
      return query_string;
   }

   const GstStructure *structure = gst_query_get_structure (query);
   if( structure )
   {
      query_string = gst_structure_to_string (structure);
      *query_string_len =
#ifndef NO_SAFESTR
            strnlen_s(query_string, RSIZE_MAX_STR);
#else
            strlen (query_string);
#endif
   }

   return query_string;
}

static gboolean
set_field (GQuark field_id, const GValue * value, gpointer user_data)
{
  GstStructure *structure = (GstStructure *)user_data;

  gst_structure_id_set_value (structure, field_id, value);

  return TRUE;
}

static GstMemory* virt_to_mem(void *pVirt,
                              gsize size)
{
   GstMemory *mem = NULL;

   if( pVirt && (size>0) )
   {

      mem = gst_memory_new_wrapped((GstMemoryFlags)0,
                                   pVirt,
                                   size,
                                   0,
                                   size,
                                   NULL,
                                   NULL);
   }

   return mem;
}

static void MemDestroyGFree(gpointer data)
{
   g_free(data);
}

static GstMemory* virt_to_mem_destroy(void *pVirt,
                                      gsize size)
{
   GstMemory *mem = NULL;

   if( pVirt && (size>0) )
   {

      mem = gst_memory_new_wrapped((GstMemoryFlags)0,
                                   pVirt,
                                   size,
                                   0,
                                   size,
                                   pVirt,
                                   MemDestroyGFree);
   }

   return mem;
}

//Serialize a GstQuery* into a GList of GstMemory blocks
// Note: This will also add the header to the memList
static GList *serialize_query(GstQuery *query,
                              QueryExchangeHeader *queryHeader)
{
  GList *memList = NULL;

  if( query )
  {
    gssize query_string_len;
    gchar* query_string = query_to_string(query,
                                          &query_string_len);
    if( query_string )
    {
      queryHeader->queryType = GST_QUERY_TYPE (query);

      memList = g_list_append (memList, virt_to_mem(queryHeader, sizeof(QueryExchangeHeader)));
      memList = g_list_append (memList, virt_to_mem_destroy(query_string, query_string_len + 1));
    }
  }

  return memList;
}

static void destroy_serialized_query_memlist(GList *memList)
{
   if( memList )
   {
      for(GList *li = memList; li != NULL; li = li->next )
      {
         GstMemory *mem = (GstMemory *)li->data;
         gst_memory_unref(mem);
      }
      g_list_free(memList);
   }
}

//deserialize a query from an array of GstMemory blocks
static GstQuery *deserialize_query(const GArray *mem_array, gboolean *pQueryResult)
{
   GstQuery *query = NULL;
   if( mem_array )
   {
      GstMemory **gstmemarray = (GstMemory **)mem_array->data;

      GstMapInfo queryHeaderMap;
      if( gst_memory_map (gstmemarray[QUERYEXCHANGE_HEADER], &queryHeaderMap, GST_MAP_READ) )
      {
         QueryExchangeHeader *pQueryHeader = (QueryExchangeHeader *)queryHeaderMap.data;

         if( pQueryResult )
            *pQueryResult = pQueryHeader->queryResult;

         if( mem_array->len > 1 )
         {
            GstMapInfo queryStingMap;
            if( gst_memory_map (gstmemarray[QUERYEXCHANGE_STRUCTSTRING],
                                &queryStingMap, GST_MAP_READ) )
            {

              query = query_from_string(pQueryHeader->queryType,
                                        (gchar *)queryStingMap.data,
                                        queryStingMap.size);
              gst_memory_unmap(gstmemarray[QUERYEXCHANGE_STRUCTSTRING], &queryStingMap);
            }
            else
            {
               GST_ERROR ("Error mapping string segment for reading.");
            }
         }

         gst_memory_unmap(gstmemarray[QUERYEXCHANGE_HEADER], &queryHeaderMap);
      }
      else
      {
         GST_ERROR("Error mapping query header data segment for reading.");
      }
   }

   return query;
}

gboolean query_data_exchanger_send_query(QueryDataExchanger *queryexchanger,
                                         GstQuery *query)
{
   if( !DATAEXCHANGER_IS_QUERY(queryexchanger) ||
       !query )
      return FALSE;

   gboolean ret = FALSE;


   QueryExchangeHeader queryHeader;
   queryHeader.queryType = GST_QUERY_TYPE (query);
   queryHeader.queryResult = FALSE;
   GList *memList = serialize_query(query,
                                    &queryHeader);

   if( memList )
   {

      RemoteOffloadResponse *pResponse = remote_offload_response_new();

      ret = remote_offload_data_exchanger_write((RemoteOffloadDataExchanger *)queryexchanger,
                                          memList,
                                          pResponse);

      if( ret )
      {
         if( remote_offload_response_wait(pResponse, 0) == REMOTEOFFLOADRESPONSE_RECEIVED )
         {
            GArray *serialized_query_array =
                  remote_offload_response_steal_mem_array(pResponse);

            if( serialized_query_array )
            {
               gboolean queryResult;
               GstQuery *query_response =
                     deserialize_query(serialized_query_array,
                                       &queryResult);
               if( query_response )
               {
                  //update the fields within the query that the caller
                  // had originally passed in
                  GstStructure *structure_response = gst_query_writable_structure (query);
                  gst_structure_remove_all_fields (structure_response);

                  gst_structure_foreach (gst_query_get_structure (query_response), set_field,
                        structure_response);
                  gst_query_unref (query_response);

                  //TODO: set the return of this function to the
                  // query result sent by the remote.
                  //ret = queryResult;
               }
               else
               {
                  GST_ERROR_OBJECT (queryexchanger, "Error deserializing query response");
                  ret = FALSE;
               }

               //clean the serialized query response array
               for( guint memi = 0; memi < serialized_query_array->len; memi++)
               {
                  gst_memory_unref(g_array_index(serialized_query_array,
                                                 GstMemory *,
                                                 memi));
               }
               g_array_unref(serialized_query_array);

            }
            else
            {
               GST_ERROR_OBJECT (queryexchanger, "serialized_query_array returned NULL");
               ret = FALSE;
            }

         }
         else
         {
            GST_ERROR_OBJECT (queryexchanger, "remote_offload_response_wait failed");
            ret = FALSE;
         }
      }

      g_object_unref(pResponse);
      destroy_serialized_query_memlist(memList);
   }

   return ret;
}


gboolean query_data_exchanger_received(RemoteOffloadDataExchanger *exchanger,
                                       const GArray *segment_mem_array,
                                       guint64 response_id)
{
   if( !segment_mem_array ||
       (segment_mem_array->len < 1) ||
       !DATAEXCHANGER_IS_QUERY(exchanger))
      return FALSE;

   gboolean ret = TRUE;

   QueryDataExchanger *self = DATAEXCHANGER_QUERY(exchanger);

   GstQuery *query = deserialize_query(segment_mem_array, NULL);

   if( query )
   {
      //embed the response-id within the query mini-object, via a quark
      guint64 *pquark = g_malloc(sizeof(guint64));
      *pquark = response_id;

      gst_mini_object_set_qdata( GST_MINI_OBJECT(query),
                                 QUARK_QUERY_RESPONSE_ID,
                                 pquark,
                                 NULL);

      if( self->callback && self->callback->query_received )
      {
        self->callback->query_received(query, self->callback->priv);
      }
      else
      {
         GST_WARNING_OBJECT (exchanger, "query_received callback not set");
      }
   }
   else
   {
      GST_ERROR_OBJECT (exchanger, "deserialize_query failed");

      //TODO: If response_id is non-zero, it means there is a thread
      // waiting on a response... we should probably send some bad status
      // here to release that thread

      ret = FALSE;
   }

   return ret;
}



gboolean query_data_exchanger_send_query_result(QueryDataExchanger *queryexchanger,
                                                GstQuery *query,
                                                gboolean result)
{
   if( !DATAEXCHANGER_IS_QUERY(queryexchanger) ||
       !query )
      return FALSE;

   gboolean ret = FALSE;

   guint64 *presponseId = (guint64 *)gst_mini_object_steal_qdata(GST_MINI_OBJECT(query),
                                               QUARK_QUERY_RESPONSE_ID);
   if( presponseId )
   {

      QueryExchangeHeader queryHeader;
      queryHeader.queryResult = result;

      GList *memList = serialize_query(query,
                                       &queryHeader);

      if( memList )
      {

         ret = remote_offload_data_exchanger_write_response(
                                             (RemoteOffloadDataExchanger *)queryexchanger,
                                             memList,
                                             *presponseId);

         destroy_serialized_query_memlist(memList);
      }
      else
      {
        GST_ERROR_OBJECT (queryexchanger, "serialize_query failed");
      }

      g_free(presponseId);
   }
   else
   {
      GST_ERROR_OBJECT (queryexchanger, "QUARK_QUERY_RESPONSE_ID not embedded in query");
   }



   return ret;
}

static void
query_data_exchanger_set_property (GObject      *object,
                                  guint         property_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  QueryDataExchanger *self = DATAEXCHANGER_QUERY (object);
  switch (property_id)
  {
    case PROP_CALLBACK:
    {
       self->callback = g_value_get_pointer (value);
    }
    break;

    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


static void
query_data_exchanger_class_init (QueryDataExchangerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  RemoteOffloadDataExchangerClass *parent_class = REMOTEOFFLOAD_DATAEXCHANGER_CLASS(klass);

  QUARK_QUERY_RESPONSE_ID = g_quark_from_static_string ("remoteoffload-query-id");

  object_class->set_property = query_data_exchanger_set_property;
  obj_properties[PROP_CALLBACK] =
    g_param_spec_pointer ("callback",
                         "Callback",
                         "Received Callback",
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);

  g_object_class_install_properties (object_class,
                                     N_PROPERTIES,
                                     obj_properties);

  parent_class->received = query_data_exchanger_received;
}

static void
query_data_exchanger_init (QueryDataExchanger *self)
{
  self->callback = NULL;
}

QueryDataExchanger *query_data_exchanger_new (RemoteOffloadCommsChannel *channel,
                                             QueryDataExchangerCallback *pcallback)
{
   QueryDataExchanger *pexchanger =
        g_object_new(QUERYDATAEXCHANGER_TYPE,
                     "callback", pcallback,
                     "commschannel", channel,
                     "exchangername", "xlink.exchanger.query",
                     NULL);

   return pexchanger;
}

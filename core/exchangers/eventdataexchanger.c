/*
 *  eventdataexchanger.c - EventDataExchanger object
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
#include "eventdataexchanger.h"

enum
{
  PROP_CALLBACK = 1,
  N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

struct _EventDataExchanger
{
  RemoteOffloadDataExchanger parent_instance;

  /* Other members, including private data. */
  EventDataExchangerCallback *callback;

};

GST_DEBUG_CATEGORY_STATIC (event_data_exchanger_debug);
#define GST_CAT_DEFAULT event_data_exchanger_debug

G_DEFINE_TYPE_WITH_CODE (EventDataExchanger, event_data_exchanger, REMOTEOFFLOADDATAEXCHANGER_TYPE,
GST_DEBUG_CATEGORY_INIT (event_data_exchanger_debug, "remoteoffloadeventdataexchanger", 0,
  "debug category for remoteoffloadeventdataexchanger"))

static GQuark QUARK_EVENT_RESPONSE_ID;

typedef struct _EventExchangeHeader
{
   GstEventType eventType;
   guint64       timestamp;
   guint32       seqnum;

   guint64 event_string_len;
}EventExchangeHeader;

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

gboolean event_data_exchanger_send_event(EventDataExchanger *eventexchanger,
                                         GstEvent *event)
{
   if( !DATAEXCHANGER_IS_EVENT(eventexchanger) ||
       !event )
      return FALSE;

   gboolean ret;

   EventExchangeHeader header;
   header.eventType = GST_EVENT_TYPE(event);
   header.timestamp = GST_EVENT_TIMESTAMP(event);
   header.seqnum = GST_EVENT_SEQNUM(event);

   gchar *structure_string = NULL;
   header.event_string_len = 0;

   const GstStructure *structure = gst_event_get_structure(event);
   if( structure )
   {
      structure_string = gst_structure_to_string(structure);
      if( structure_string )
      {
         header.event_string_len =
#ifndef NO_SAFESTR
               strnlen_s(structure_string, RSIZE_MAX_STR) + 1;
#else
               strlen(structure_string) + 1;
#endif
      }
   }

   GList *memList = NULL;
   memList = g_list_append (memList, virt_to_mem(&header, sizeof(header)));

   if( header.event_string_len )
   {
      memList = g_list_append (memList, virt_to_mem(structure_string, header.event_string_len));
   }

   gboolean event_return = TRUE;
   RemoteOffloadResponse *pResponse = NULL;

   pResponse = remote_offload_response_new();

   ret = remote_offload_data_exchanger_write((RemoteOffloadDataExchanger *)eventexchanger,
                                       memList,
                                       pResponse);

   if( pResponse )
   {
      if( ret )
      {
         if( remote_offload_response_wait(pResponse, 0) != REMOTEOFFLOADRESPONSE_RECEIVED )
         {
            GST_ERROR_OBJECT (eventexchanger, "remote_offload_response_wait failed");
            event_return = FALSE;
         }
         else
         {
           if( !remote_offload_copy_response(pResponse, &event_return, sizeof(event_return), 0))
           {
              GST_ERROR_OBJECT (eventexchanger, "remote_offload_copy_response failed");
              event_return = FALSE;
           }
         }
      }
      else
      {
         event_return = FALSE;
      }

      g_object_unref(pResponse);
   }

   for(GList *li = memList; li != NULL; li = li->next )
   {
      GstMemory *mem = (GstMemory *)li->data;
      gst_memory_unref(mem);
   }
   g_list_free(memList);

   if( structure_string )
      g_free(structure_string);

   if( ret )
   {
      ret = event_return;
   }

   return ret;
}

gboolean event_data_exchanger_received(RemoteOffloadDataExchanger *exchanger,
                                       const GArray *segment_mem_array,
                                       guint64 response_id)
{
   if( !segment_mem_array ||
       (segment_mem_array->len < 1) ||
       !DATAEXCHANGER_IS_EVENT(exchanger))
      return FALSE;

   gboolean ret = TRUE;

   EventDataExchanger *self = DATAEXCHANGER_EVENT(exchanger);

   GstMemory **gstmemarray = (GstMemory **)segment_mem_array->data;

   GstMapInfo eventHeaderMap;
   if( !gst_memory_map (gstmemarray[0], &eventHeaderMap, GST_MAP_READ) )
   {
      GST_ERROR_OBJECT (exchanger, "Error mapping event header data segment for reading.");
      return FALSE;
   }

   EventExchangeHeader *pEventHeader = (EventExchangeHeader *)eventHeaderMap.data;

   gchar *structure_string = NULL;
   GstStructure *structure = NULL;

   if(pEventHeader->event_string_len != 0 )
   {
      if( segment_mem_array->len < 2 )
      {
         gst_memory_unmap(gstmemarray[0], &eventHeaderMap);
         GST_ERROR_OBJECT (exchanger, "Error! Required string segment not present.");
         return FALSE;
      }

      GstMapInfo eventStrMap;
      if( !gst_memory_map (gstmemarray[1], &eventStrMap, GST_MAP_READ) )
      {
         GST_ERROR_OBJECT (exchanger, "Error mapping string data segment for reading.");
         gst_memory_unmap(gstmemarray[0], &eventHeaderMap);
         return FALSE;
      }

      structure_string = (gchar *)eventStrMap.data;
      structure = gst_structure_from_string(structure_string, NULL);

      if( !structure )
      {
         GST_WARNING_OBJECT (exchanger, "gst_structure_from_string failed for event str = %s",
                             structure_string);
      }

      gst_memory_unmap(gstmemarray[1], &eventStrMap);
   }

   GstEvent *event = gst_event_new_custom(pEventHeader->eventType, structure);
   if( !event )
   {
      GST_ERROR_OBJECT (exchanger, "Error creating event from gst_event_new_custom");
      gst_memory_unmap(gstmemarray[0], &eventHeaderMap);
      return FALSE;
   }
   GST_EVENT_TIMESTAMP(event) = pEventHeader->timestamp;
   GST_EVENT_SEQNUM(event) = pEventHeader->seqnum;


   if( response_id )
   {
      guint64 *pquark = g_malloc(sizeof(guint64));
      *pquark = response_id;

      //embed the response-id within the query mini-object, via a quark
      gst_mini_object_set_qdata( GST_MINI_OBJECT(event),
                                 QUARK_EVENT_RESPONSE_ID,
                                 pquark,
                                 NULL);
   }

   if( self->callback && self->callback->event_received )
   {
     self->callback->event_received(event, self->callback->priv);
   }
   else
   {
      GST_WARNING_OBJECT (exchanger, "query_received callback not set");
   }

   ret = TRUE;


   gst_memory_unmap(gstmemarray[0], &eventHeaderMap);

   return ret;
}

gboolean event_data_exchanger_send_event_result(EventDataExchanger *eventexchanger,
                                                GstEvent *event,
                                                gboolean result)
{
   if( !DATAEXCHANGER_IS_EVENT(eventexchanger) ||
       !event )
      return FALSE;

   guint64 *presponseId = (guint64 *)gst_mini_object_steal_qdata(GST_MINI_OBJECT(event),
                                               QUARK_EVENT_RESPONSE_ID);

   if( !presponseId )
   {
      GST_ERROR_OBJECT (eventexchanger, "QUARK_EVENT_RESPONSE_ID not embedded in event");
      return FALSE;
   }


   gboolean ret = remote_offload_data_exchanger_write_response_single(
                                       (RemoteOffloadDataExchanger *)eventexchanger,
                                       (guint8 *)&result,
                                       sizeof(result),
                                       *presponseId);

   g_free(presponseId);

   return ret;
}

static void
event_data_exchanger_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  EventDataExchanger *self = DATAEXCHANGER_EVENT (object);
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
event_data_exchanger_class_init (EventDataExchangerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  RemoteOffloadDataExchangerClass *parent_class = REMOTEOFFLOAD_DATAEXCHANGER_CLASS(klass);

  QUARK_EVENT_RESPONSE_ID = g_quark_from_static_string ("remoteoffload-event-id");

  object_class->set_property = event_data_exchanger_set_property;
  obj_properties[PROP_CALLBACK] =
    g_param_spec_pointer ("callback",
                         "Callback",
                         "Received Callback",
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);

  g_object_class_install_properties (object_class,
                                     N_PROPERTIES,
                                     obj_properties);

  parent_class->received = event_data_exchanger_received;
}

static void
event_data_exchanger_init (EventDataExchanger *self)
{
  self->callback = NULL;
}

EventDataExchanger *event_data_exchanger_new (RemoteOffloadCommsChannel *channel,
                                              EventDataExchangerCallback *callback)
{
   EventDataExchanger *pexchanger =
        g_object_new(EVENTDATAEXCHANGER_TYPE,
                     "callback", callback,
                     "commschannel", channel,
                     "exchangername", "xlink.exchanger.event",
                     NULL);

   return pexchanger;
}

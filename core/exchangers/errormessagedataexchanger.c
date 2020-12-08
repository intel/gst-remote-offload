/*
 *  errormessagedataexchanger.c - ErrorMessageDataExchanger object
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
#include "errormessagedataexchanger.h"

enum
{
  PROP_CALLBACK = 1,
  N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

struct _ErrorMessageDataExchanger
{
  RemoteOffloadDataExchanger parent_instance;

  /* Other members, including private data. */
  ErrorMessageDataExchangerCallback *callback;

};

GST_DEBUG_CATEGORY_STATIC (errormessage_data_exchanger_debug);
#define GST_CAT_DEFAULT errormessage_data_exchanger_debug

G_DEFINE_TYPE_WITH_CODE (ErrorMessageDataExchanger,
                         errormessage_data_exchanger,
                         REMOTEOFFLOADDATAEXCHANGER_TYPE,
GST_DEBUG_CATEGORY_INIT (errormessage_data_exchanger_debug,
                         "remoteoffloaderrormessagedataexchanger", 0,
                         "debug category for remoteoffloaderrormessagedataexchanger"))

gboolean errormessage_data_exchanger_received(RemoteOffloadDataExchanger *exchanger,
                                              const GArray *segment_mem_array,
                                              guint64 response_id)
{
   if( !segment_mem_array ||
       (segment_mem_array->len != 1) ||
       !DATAEXCHANGER_IS_ERRORMESSAGE(exchanger))
      return FALSE;

   gboolean ret = TRUE;

   ErrorMessageDataExchanger *self = DATAEXCHANGER_ERRORMESSAGE(exchanger);

   GstMemory **gstmemarray = (GstMemory **)segment_mem_array->data;

   GstMapInfo dataSegmentMap;
   if( !gst_memory_map (gstmemarray[0], &dataSegmentMap, GST_MAP_READ) )
   {
      GST_ERROR_OBJECT (exchanger, "Error mapping data segment for reading.");
      return FALSE;
   }

   gchar *pMessage = g_strdup((gchar *)dataSegmentMap.data);
   if( !pMessage )
   {
      GST_ERROR_OBJECT (exchanger, "received message is NULL");
      goto done;
   }

   if( self->callback && self->callback->errormessage_received )
   {
     self->callback->errormessage_received(pMessage, self->callback->priv);
   }
   else
   {
      GST_WARNING_OBJECT (exchanger, "errormessage_received callback not set");
      g_free(pMessage);
   }

done:
   gst_memory_unmap(gstmemarray[0], &dataSegmentMap);

   return ret;
}

gboolean errormessage_data_exchanger_send_message(ErrorMessageDataExchanger *exchanger,
                                                  gchar *message)
{
   if( !DATAEXCHANGER_IS_ERRORMESSAGE(exchanger) ||
       !message )
     return FALSE;

   return remote_offload_data_exchanger_write_single((RemoteOffloadDataExchanger *)exchanger,
                                                (guint8 *)message,
#ifndef NO_SAFESTR
                                                strnlen_s(message, RSIZE_MAX_STR) + 1,
#else
                                                strlen(message) + 1,
#endif
                                                NULL);
}


static void
errormessage_data_exchanger_set_property (GObject      *object,
                                          guint         property_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
  ErrorMessageDataExchanger *self = DATAEXCHANGER_ERRORMESSAGE (object);
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
errormessage_data_exchanger_class_init (ErrorMessageDataExchangerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  RemoteOffloadDataExchangerClass *parent_class = REMOTEOFFLOAD_DATAEXCHANGER_CLASS(klass);

  object_class->set_property = errormessage_data_exchanger_set_property;
  obj_properties[PROP_CALLBACK] =
    g_param_spec_pointer ("callback",
                         "Callback",
                         "Received Callback",
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);

  g_object_class_install_properties (object_class,
                                     N_PROPERTIES,
                                     obj_properties);

  parent_class->received = errormessage_data_exchanger_received;
}

static void
errormessage_data_exchanger_init (ErrorMessageDataExchanger *self)
{
  self->callback = NULL;
}

ErrorMessageDataExchanger *
errormessage_data_exchanger_new (RemoteOffloadCommsChannel *channel,
                                 ErrorMessageDataExchangerCallback *pcallback)
{
   ErrorMessageDataExchanger *pexchanger =
        g_object_new(ERRORMESSAGEDATAEXCHANGER_TYPE,
                     "callback", pcallback,
                     "commschannel", channel,
                     "exchangername", "xlink.exchanger.errormessage",
                     NULL);

   return pexchanger;
}

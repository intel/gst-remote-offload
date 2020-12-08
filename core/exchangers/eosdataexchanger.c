/*
 *  eosdataexchanger.c - EOSDataExchanger object
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
#include "eosdataexchanger.h"

enum
{
  PROP_CALLBACK = 1,
  N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

struct _EOSDataExchanger
{
  RemoteOffloadDataExchanger parent_instance;

  /* Other members, including private data. */
  EOSDataExchangerCallback *callback;

};

GST_DEBUG_CATEGORY_STATIC (eos_data_exchanger_debug);
#define GST_CAT_DEFAULT eos_data_exchanger_debug

G_DEFINE_TYPE_WITH_CODE (EOSDataExchanger, eos_data_exchanger, REMOTEOFFLOADDATAEXCHANGER_TYPE,
GST_DEBUG_CATEGORY_INIT (eos_data_exchanger_debug, "remoteoffloadeosdataexchanger", 0,
  "debug category for remoteoffloadeosdataexchanger"))

gboolean eos_data_exchanger_received(RemoteOffloadDataExchanger *exchanger,
                                      const GArray *segment_mem_array,
                                      guint64 response_id)
{
   if( !DATAEXCHANGER_IS_EOS(exchanger))
      return FALSE;

   gboolean ret = TRUE;

   EOSDataExchanger *self = DATAEXCHANGER_EOS(exchanger);

   if( self->callback && self->callback->eos_received )
   {
     self->callback->eos_received(self->callback->priv);
   }
   else
   {
      GST_WARNING_OBJECT (exchanger, "eos_data_exchanger_received: Warning: callback not set");
   }

   return ret;
}

gboolean eos_data_exchanger_send_eos(EOSDataExchanger *eosexchanger)
{
   if( !DATAEXCHANGER_IS_EOS(eosexchanger) )
     return FALSE;

   gboolean ret;


   ret = remote_offload_data_exchanger_write_single((RemoteOffloadDataExchanger *)eosexchanger,
                                              NULL,
                                              0,
                                              NULL);

   return ret;
}

static void
eos_data_exchanger_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  EOSDataExchanger *self = DATAEXCHANGER_EOS (object);
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
eos_data_exchanger_class_init (EOSDataExchangerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  RemoteOffloadDataExchangerClass *parent_class = REMOTEOFFLOAD_DATAEXCHANGER_CLASS(klass);

  object_class->set_property = eos_data_exchanger_set_property;
  obj_properties[PROP_CALLBACK] =
    g_param_spec_pointer ("callback",
                         "Callback",
                         "Received Callback",
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);

  g_object_class_install_properties (object_class,
                                     N_PROPERTIES,
                                     obj_properties);

  parent_class->received = eos_data_exchanger_received;
}

static void
eos_data_exchanger_init (EOSDataExchanger *self)
{
  self->callback = NULL;
}

EOSDataExchanger *eos_data_exchanger_new (RemoteOffloadCommsChannel *channel,
                                          EOSDataExchangerCallback *pcallback)
{
   EOSDataExchanger *pexchanger =
        g_object_new(EOSDATAEXCHANGER_TYPE,
                     "callback", pcallback,
                     "commschannel", channel,
                     "exchangername", "xlink.exchanger.eos",
                     NULL);

   return pexchanger;
}

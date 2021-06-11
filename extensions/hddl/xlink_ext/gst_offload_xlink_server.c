/*
 *  gst_offload_xlink_server.c - Remote Offload XLink Server Application
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
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include "remoteoffloadclientserverutil.h"
#include "xlinkchannelmanager.h"
#include "gstremoteoffloadpipeline.h"
#include "remoteoffloadcommsio_xlink.h"

GST_DEBUG_CATEGORY_STATIC (remote_offload_xlink_server_debug);
#define GST_CAT_DEFAULT remote_offload_xlink_server_debug

static void xlink_server_sig_handler(int sig);

static void xlink_server_run_ropinstance(GArray *id_commsio_pair_array, void *user_data)
{
   guint32 sw_device_id = *((guint32*)user_data);
   GHashTable *id_to_channel_hash =
            id_commsio_pair_array_to_id_to_channel_hash(id_commsio_pair_array);

   if( id_to_channel_hash )
   {
      RemoteOffloadDevice *device = NULL;
      if( sw_device_id )
      {
          //Given swDeviceId, we know what device family we are
          // running on (i.e. KMB right now).
          const SWDeviceIdFamilyType device_family =
             GetFamilyFromSWDeviceId(sw_device_id);

          //Sanity check on family
          gboolean bfamily_error = FALSE;
          switch( device_family )
          {
             case SW_DEVICE_ID_KMB_FAMILY:
                GST_INFO("Device Family = KMB");
             break;

             default:
             GST_ERROR("Unsupported device family: 0x%x", device_family);
             bfamily_error = TRUE;
             break;
          }
      }
      else
      {
         GST_INFO("sw_device_id is 0... will not create device-specific object for ROP");
      }

      RemoteOffloadPipeline *pPipeline =
            remote_offload_pipeline_new(device, id_to_channel_hash);

      if( pPipeline )
      {
         if( !remote_offload_pipeline_run(pPipeline) )
         {
            GST_ERROR("remote_offload_pipeline_run failed");
         }

         g_object_unref(pPipeline);
      }
      else
      {
         GST_ERROR("Error in remote_offload_pipeline_new");
      }

      g_hash_table_unref(id_to_channel_hash);
   }
   else
   {
      GST_ERROR("Invalid PipelinePlaceholder");
   }
}

XLinkChannelManager *g_pManager;
int main(int argc, char *argv[])
{
   signal(SIGINT, xlink_server_sig_handler);
   signal(SIGTERM, xlink_server_sig_handler);

   /* Initialize GStreamer */
   gst_init (&argc, &argv);

   GST_DEBUG_CATEGORY_INIT (remote_offload_xlink_server_debug,
                               "remoteoffloadxlinkserver", 0,
                             "debug category for Remote Offload XLink Server");

   guint32 sw_device_id = 0;
   if( argc > 1 && argv[1])
   {
      if( g_str_has_prefix(argv[1], "0x") )
      {
         sscanf(argv[1], "0x%x", &sw_device_id);
      }
      else
      {
         sscanf(argv[1], "%u", &sw_device_id);
      }

      if( sw_device_id )
      {
         g_print("Will use sw_device_id=0x%x\n", sw_device_id);
      }
   }

   if( !sw_device_id )
   {
      g_print("sw_device_id = 0, will choose first/default PCIe XLink device\n");
   }

   RemoteOffloadPipelineSpawner *spawner = remote_offload_pipeline_spawner_new();
   if( !spawner )
   {
     g_print("Error creating RemoteOffloadPipelineSpawner\n");
     return -1;
   }

   remote_offload_pipeline_set_callback(spawner,
                                        xlink_server_run_ropinstance,
                                        &sw_device_id);

   g_pManager = xlink_channel_manager_get_instance();
   if( !g_pManager )
   {
      g_print("Error obtaining XLinkChannelManager instance\n");
      return -1;
   }

   while(1)
   {
      GArray *commsio_array = xlink_channel_manager_listen_channels(g_pManager,
                                                                    sw_device_id);
      if( !commsio_array )
      {
         g_print("Error in xlink_channel_manager_listen_channels\n");
         break;
      }

      gboolean status_ok = TRUE;
      for( guint i = 0; i < commsio_array->len; i++ )
      {
         RemoteOffloadCommsIO *commsio =
               g_array_index(commsio_array, RemoteOffloadCommsIO *, i);

         if( !remote_offload_pipeline_spawner_add_connection(spawner, commsio))
         {
            g_print("Error in remote_offload_pipeline_spawner_add_connection for pCommsIOXLink=%p\n",
                    commsio);
            status_ok = FALSE;
            break;
         }
      }

      g_array_free(commsio_array, TRUE);

      if( !status_ok )
         break;
   }

   xlink_channel_manager_unref(g_pManager);

   g_object_unref(spawner);

   return 0;
}

static void xlink_server_sig_handler(int sig)
{
   //reset the signal handler(s) back to their default case,
   // in case the user performs another Ctrl^C / kill -15.
   //  On the second attempt we want to force-ably quit.
   signal(SIGINT, SIG_DFL);
   signal(SIGTERM, SIG_DFL);

   //close the default channel.. this should trigger the
   // thread sitting within xlink_read_data to fail,
   // returning an error to xlink_channel_manager_listen_channels,
   // and a noisy (some error messages) but graceful shutdown of XLink.
   xlink_channel_manager_close_default_channels(g_pManager);
}

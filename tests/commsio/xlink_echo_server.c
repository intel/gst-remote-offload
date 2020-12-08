/*
 *  xlink_echo_server.c - Low level XLink CommsIO echo test
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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "XLink.h"
#include "remoteoffloadcommsio.h"
#include "remoteoffloadcommsio_xlink.h"

int main(int argc, char *argv[])
{
   XLinkGlobalHandler_t ghandler = {
        .protocol = PCIE,
    };

   XLinkHandler_t handler ={
        .devicePath = "/tmp/xlink_mock",
        .deviceType = PCIE_DEVICE
    };


   if( XLinkInitialize(&ghandler) != X_LINK_SUCCESS )
   {
      g_print("XLinkInitialize failed\n");
      return -1;
   }

   if( XLinkConnect(&handler) != X_LINK_SUCCESS )
   {
      g_print("XLinkConnect failed\n");
      return -1;
   }

   //upon construction, the xlinkio oject will
   // call XLinkOpenChannel(), etc.
   RemoteOffloadCommsXLinkIO *xlinkio =
         remote_offload_comms_io_xlink_new(&handler,
                                           0x400);

   if( !xlinkio )
   {
      g_print("remote_offload_comms_io_xlink_new failed\n");
      return -1;
   }

   //"up-cast" RemoteOffloadCommsXLinkIO to RemoteOffloadCommsIO
   RemoteOffloadCommsIO *commsio = REMOTEOFFLOAD_COMMSIO(xlinkio);

   while(1)
   {
      RemoteOffloadCommsIOResult r;
      uint32_t size = 0;

      //get the size of the next message
      r = remote_offload_comms_io_read(commsio, (guint8 *)&size, sizeof(size));
      if( r != REMOTEOFFLOADCOMMSIO_SUCCESS )
      {
         g_print("Error reading size\n");
         return -1;
      }

      gchar *message = (gchar *)g_malloc(size);
      if( !message )
      {
         g_print("Error in g_malloc(%u)\n", size);
         return -1;
      }

      //get the actual message
      r = remote_offload_comms_io_read(commsio, (guint8 *)message, size);
      if( r != REMOTEOFFLOADCOMMSIO_SUCCESS )
      {
         g_print("Error reading message\n");
         return -1;
      }

      g_print("Message received from client: %s\n", message);

      gboolean bexit = FALSE;
      if( strncmp(message, "exit", 4) == 0)
      {
         g_print("exit received by host, server quitting..\n");
         bexit = TRUE;
      }


      if( !bexit )
      {
         //echo it back
         r = remote_offload_comms_io_write(commsio, (guint8 *)message, size);
         if( r != REMOTEOFFLOADCOMMSIO_SUCCESS )
         {
            g_print("Error writing message back\n");
            return -1;
         }
      }

      g_free(message);

      if( bexit ) break;
   }

   //in "finalize", this xlinkio object will call
   // XLinkCloseChannel()
   g_object_unref(xlinkio);

   return 0;
}

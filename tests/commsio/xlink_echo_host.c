/*
 *  xlink_echo_host.c - Low level XLink CommsIO echo test
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
#include <stdio.h>
#include <string.h>
#include "XLink.h"
#include "remoteoffloadcommsio.h"
#include "remoteoffloadcommsio_xlink.h"

#define MAXCMDLINKREADSIZE 1024

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

   if( XLinkBootRemote(&handler, DEFAULT_NOMINAL_MAX) != X_LINK_SUCCESS)
   {
      g_print("XLinkBootRemote failed\n");
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

   gchar input[MAXCMDLINKREADSIZE];
   gchar output[MAXCMDLINKREADSIZE];
   output[0] = '\0';

   g_print("Type some message and press <enter>\n");
   g_print("  when you're done, type 'exit'\n");
   while(1)
   {
      fgets(input, sizeof(input), stdin);

      RemoteOffloadCommsIOResult r;

      //send the size first
      guint size = strlen(input)+1;

      r = remote_offload_comms_io_write(commsio, (guint8 *)&size, sizeof(size));
      if( r != REMOTEOFFLOADCOMMSIO_SUCCESS )
      {
         g_print("Error writing size\n");
         return -1;
      }

      //and now send the actual message
      r = remote_offload_comms_io_write(commsio, (guint8 *)input, size);
      if( r != REMOTEOFFLOADCOMMSIO_SUCCESS )
      {
         g_print("Error writing message\n");
         return -1;
      }

      if( strncmp(input, "exit", 4) == 0 )
      {
         g_print("exit received, quitting..\n");
         break;
      }

      //receive the echo'ed message into 'output'
      r = remote_offload_comms_io_read(commsio, (guint8 *)output, size);
      if( r != REMOTEOFFLOADCOMMSIO_SUCCESS )
      {
         g_print("Error reading size\n");
         return -1;
      }

      g_print("echo'ed message from server: %s\n", output);

   }

   //in "finalize", this xlinkio object will call
   // XLinkCloseChannel()
   g_object_unref(xlinkio);


   return 0;
}

/*
 *  remoteoffloadbinpipelinecommon.c - Utilities & types shared between
 *         remoteoffloadbin & remoteoffloadpipeline objects
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
#include "remoteoffloadbinpipelinecommon.h"
#include "remoteoffloadcommschannel.h"
#include "remoteoffloadcomms.h"
#include "remoteoffloadbinserializer.h"


gboolean AssembleRemoteConnections(GstBin *bin,
                                   GArray *remoteconnectioncandidates,
                                   GHashTable *id_to_channel_hash)
{
   if( !GST_IS_BIN(bin) ) return FALSE;
   if( !remoteconnectioncandidates ) return FALSE;
   if( !id_to_channel_hash ) return FALSE;

   RemoteElementConnectionCandidate *connections =
               (RemoteElementConnectionCandidate *)remoteconnectioncandidates->data;


   for( guint connectioni = 0; connectioni < remoteconnectioncandidates->len; connectioni++ )
   {
      RemoteOffloadCommsChannel *channel =
            g_hash_table_lookup (id_to_channel_hash, GINT_TO_POINTER(connections[connectioni].id));

      if( !channel )
      {
         GST_ERROR ("channel not found for id=%d", connections[connectioni].id);
         return FALSE;
      }

      GstElement *element = gst_pad_get_parent_element (connections[connectioni].pad);
      if( !element )
      {
         GST_ERROR ("element is NULL");
         return FALSE;
      }

      gchar *padname = gst_pad_get_name(connections[connectioni].pad);
      gchar *elementname = gst_element_get_name(element);

      GST_INFO("element %s, pad %s needs to be remote-linked on id = %d",
               elementname,
               padname,
               connections[connectioni].id);

      GstPadDirection dir = gst_pad_get_direction(connections[connectioni].pad);
      switch(dir)
      {
         case GST_PAD_SRC:
         {
            gchar name[32];
            g_snprintf(name, 32, "remoteoffloadingress%d", connections[connectioni].id);
            GstElement *pRemoteOffloadSink = gst_element_factory_make("remoteoffloadingress", name);

            if( !pRemoteOffloadSink )
            {
               GST_ERROR ("Error creating remoteoffloadingress element");
               return FALSE;
            }

            g_object_set (pRemoteOffloadSink, "commschannel", (gpointer)channel,
                                              NULL);

            //add this appsink to the bin
            if( !gst_bin_add(bin, pRemoteOffloadSink) )
            {
               GST_ERROR("Error adding remoteoffloadingress to the bin");
               return FALSE;
            }

            //so we need to link 'this' pad to remoteoffloadingress, but first we need to
            // unlink it from whatever it was currently linked to.
            //Note: On the host-side, this should be linked.
            //      On the remote-side, it shouldn't be, so it's okay if peerpad is NULL
            GstPad *peerpad = gst_pad_get_peer(connections[connectioni].pad);
            if( peerpad )
            {
               if( !gst_pad_unlink(connections[connectioni].pad, peerpad) )
               {
                  GST_ERROR("Error! gst_pad_unlink failed");
                  return FALSE;
               }
            }

            //link remoteoffoadsink to the element
            GST_DEBUG("linking element=%s, pad=%s to %s, sink pad", elementname, padname, name);
            if( !gst_element_link_pads(element, padname, pRemoteOffloadSink, "sink") )
            {
               GST_ERROR("Error linking element to remoteoffloadingress");
               return FALSE;
            }

       }
       break;

       case GST_PAD_SINK:
       {
            gchar name[32];
            g_snprintf(name, 32, "remoteoffloadegress%d", connections[connectioni].id);
            GstElement *pRemoteOffloadSrc = gst_element_factory_make("remoteoffloadegress", name);

            if( !pRemoteOffloadSrc )
            {
               GST_ERROR("Error creating remoteoffloadegress element");
               return FALSE;
            }

            g_object_set (pRemoteOffloadSrc, "commschannel", (gpointer)channel,
                                              NULL);
            //add this appsrc to our bin
            if( !gst_bin_add(bin, pRemoteOffloadSrc) )
            {
               GST_ERROR("Error adding remoteoffloadegress to the pipeline");
               return FALSE;
            }

            //so we need to link 'this' pad to remoteoffloadegress, but first we need to
            // unlink it from whatever it was currently linked to.
            //Note: On the host-side, this should be linked.
            //      On the remote-side, it shouldn't be, so it's okay if peerpad is NULL
            GstPad *peerpad = gst_pad_get_peer(connections[connectioni].pad);
            if( peerpad )
            {
               if( !gst_pad_unlink(peerpad, connections[connectioni].pad) )
               {
                  GST_ERROR("Error! gst_pad_unlink failed");
                  return FALSE;
               }
            }

            //link remoteoffloadegress to the element
            GST_DEBUG("linking %s, src pad to element=%s, pad=%s", name, elementname, padname);
            if( !gst_element_link_pads(pRemoteOffloadSrc, "src", element, padname) )
            {
               GST_ERROR("Error linking %s to element %s, pad=%s", name, elementname, padname);
               return FALSE;
            }
       }
       break;

       case GST_PAD_UNKNOWN:
       {
          GST_ERROR ("unknown pad direction...");
          return FALSE;
       }
       break;
    }

    g_free(elementname);
    g_free(padname);
    gst_object_unref (element);
  }

  return TRUE;
}


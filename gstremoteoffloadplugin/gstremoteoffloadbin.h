/*
 *  remoteoffloadbin.h - GstRemoteOffloadBin element
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
#ifndef __GST_REMOTEOFFLOADBIN_H__
#define __GST_REMOTEOFFLOADBIN_H__

#include <gst/gst.h>
#include <gst/gstbin.h>

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_REMOTEOFFLOADBIN \
  (gst_remoteoffload_bin_get_type())
#define GST_REMOTEOFFLOADBIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_REMOTEOFFLOADBIN,GstRemoteOffloadBin))
#define GST_REMOTEOFFLOADBIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_REMOTEOFFLOADBIN,GstRemoteOffloadBinClass))
#define GST_IS_REMOTEOFFLOADBIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_REMOTEOFFLOADBIN))
#define GST_IS_REMOTEOFFLOADBIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_REMOTEOFFLOADBIN))


typedef struct _GstRemoteOffloadBin      GstRemoteOffloadBin;
typedef struct _GstRemoteOffloadBinClass GstRemoteOffloadBinClass;
typedef struct _RemoteOffloadCommsChannel RemoteOffloadCommsChannel;
typedef struct _GstRemoteOffloadBinExchangers  GstRemoteOffloadBinExchangers;
typedef struct _RemoteOffloadExtRegistry RemoteOffloadExtRegistry;
typedef struct _RemoteOffloadBinPrivate RemoteOffloadBinPrivate;
struct _GstRemoteOffloadBin
{
  GstBin bin;

  gboolean silent;

  //properties
  gchar *device;
  gchar *deviceparams;

  gchar *remotegstdebug;
  gchar *remotegstdebuglocation;
  gint32 logmode;

  //commsmethod-to-commsgenerator hash
  GHashTable *device_proxy_hash;

  //id (gint32) to RemoteOffloadCommsChannel hash table.
  GHashTable *id_to_channel_hash;

  RemoteOffloadCommsChannel *pDefaultCommsChannel;

  GstRemoteOffloadBinExchangers *pExchangers;
  RemoteOffloadBinPrivate *pPrivate;

  GMutex rob_state_mutex;
  gboolean bconnection_cut;

  GMutex mutex;
  GCond  cond;
  gboolean deserializationstatus;

  gboolean rop_ready;

  guint    nIngress; //number of ingress (sink) elements
  gboolean bRemotePipelineEOS; //Has the remote pipeline posted EOS?
  gboolean bThisEOS;  //Has this Bin posted EOS?

  RemoteOffloadExtRegistry *ext_registry;

};

struct _GstRemoteOffloadBinClass
{
   GstBinClass parent_class;
};

GType gst_remoteoffload_bin_get_type (void);

G_END_DECLS

#endif /* __GST_REMOTEOFFLOADBIN_H__ */

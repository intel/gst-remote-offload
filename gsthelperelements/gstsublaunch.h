/*
 *  gstsublaunch.h - SubLaunch element
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
#ifndef __GST_SUBLAUNCH_H__
#define __GST_SUBLAUNCH_H__

#include <gst/gst.h>
#include <gst/gstbin.h>

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_SUBLAUNCH \
  (gst_sublaunch_get_type())
#define GST_SUBLAUNCH(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SUBLAUNCH,SubLaunch))
#define GST_SUBLAUNCH_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SUBLAUNCH,SubLaunchClass))
#define GST_IS_SUBLAUNCH(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SUBLAUNCH))
#define GST_IS_SUBLAUNCH_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SUBLAUNCH))

typedef struct _SubLaunch      SubLaunch;
typedef struct _SubLaunchClass SubLaunchClass;

struct _SubLaunch
{
  GstBin bin;

  gboolean bparent_populated;

  gchar *launchstr;
  GList *ghost_pad_list;
  GstElement *subpipeline;
  guint sinkpad_index;
  guint srcpad_index;

};

struct _SubLaunchClass
{
   GstBinClass parent_class;

   /* actions */
   gboolean (*populate_parent) (SubLaunch *sublaunch);
};

GType gst_sublaunch_get_type (void);

G_END_DECLS

#endif /* __GST_SUBLAUNCH_H__ */

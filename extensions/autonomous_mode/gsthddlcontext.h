/*
 *  gsthddlcontext.h - Context for HDDL elements
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author: Soon, Thean Siew <thean.siew.soon@intel.com>
 *    Author: Hoe, Sheng Yang <sheng.yang.hoe@intel.com>
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

#ifndef GST_HDDL_CONTEXT_H
#define GST_HDDL_CONTEXT_H

#include <gst/gst.h>
#include "gsthddlconn.h"

G_BEGIN_DECLS
#define GST_TYPE_HDDL_CONTEXT \
	(gst_hddl_context_get_type())
typedef struct _GstHddlContext GstHddlContext;

#ifdef GVA_PLUGIN
#include "gsthddlvideoroimeta.h"
#endif

// FIXME: Temporary fake declarations for hddl scheduler before actual
// content is available
typedef guint HDDLContextID;

struct _GstHddlContext
{
  /* Xlink target handle assigned by HDDL Scheduler */
  union
  {
    GstHddlXLink *hddl_xlink;
    GstHddlTcp *hddl_tcp;
  };
  GstHddlConnectionMode conn_mode;
  HDDLContextID context_id;
};

GType gst_hddl_context_get_type (void);

GstHddlContext *gst_hddl_context_new (GstHddlConnectionMode);

void gst_hddl_context_free (GstHddlContext * hddl_context);

G_END_DECLS
#endif

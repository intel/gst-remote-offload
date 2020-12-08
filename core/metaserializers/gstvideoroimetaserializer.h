/*
 *  gstvideoroimetaserializer.h - GstVideoRegionOfInterestMetaSerializer object
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
#ifndef _GSTVIDEOROIMETASERIALIZER_H_
#define _GSTVIDEOROIMETASERIALIZER_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define GSTVIDEOROIMETASERIALIZER_TYPE (gst_videoroi_metaserializer_get_type ())
G_DECLARE_FINAL_TYPE (GstVideoRegionOfInterestMetaSerializer, gst_videoroi_metaserializer,
                      METASERIALIZER, VIDEOROI, GObject)

GstVideoRegionOfInterestMetaSerializer *gst_videoroi_metaserializer_new();

G_END_DECLS

#endif

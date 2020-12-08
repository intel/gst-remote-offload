/*
 *  datatransferdefs.h - common communication structure definitions
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
#ifndef _DATATRANSFERDEFS_H_
#define _DATATRANSFERDEFS_H_

#include <gst/gst.h>


typedef struct _DataTransferHeader
{
   gint32 id;
   guint64 response_id;
   guint16 dataTransferType;
   guint16 nsegments;
}DataTransferHeader;

typedef struct _DataSegmentHeader
{
   guint64 segmentSize;
}DataSegmentHeader;

typedef struct _ResponsePoolEntry
{
  void *pResponse;
  GMutex entryMutex;
  GCond  entryCond;
}ResponsePoolEntry;


#endif


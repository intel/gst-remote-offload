/*
 *  remoteoffloadcommsio.c - RemoteOffloadCommsIO interface
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
#include "remoteoffloadcommsio.h"

G_DEFINE_INTERFACE (RemoteOffloadCommsIO, remote_offload_comms_io, G_TYPE_OBJECT)

static GstMemory* virt_to_mem(void *pVirt,
                              gsize size)
{
   GstMemory *mem = NULL;

   if( pVirt && (size>0) )
   {

      mem = gst_memory_new_wrapped((GstMemoryFlags)0,
                                   pVirt,
                                   size,
                                   0,
                                   size,
                                   NULL,
                                   NULL);
   }

   return mem;
}

static void
remote_offload_comms_io_default_init (RemoteOffloadCommsIOInterface *iface)
{
    /* add properties and signals to the interface here */
}


RemoteOffloadCommsIOResult remote_offload_comms_io_read(RemoteOffloadCommsIO *commsio,
                                                        guint8 *buf,
                                                        guint64 size)
{
  RemoteOffloadCommsIOInterface *iface;

  if( !REMOTEOFFLOAD_IS_COMMSIO(commsio) ) return REMOTEOFFLOADCOMMSIO_FAIL;

  iface = REMOTEOFFLOAD_COMMSIO_GET_IFACE(commsio);

  if( iface->read )
     return iface->read(commsio, buf, size);

  if( iface->read_mem || iface->read_mem_list )
  {
     GstMemory *mem = virt_to_mem(buf, size);
     if( mem )
     {
        RemoteOffloadCommsIOResult ret;
        if( iface->read_mem )
        {
           ret = iface->read_mem(commsio, mem);
        }
        else
        {
           GList *mem_list = NULL;
           mem_list = g_list_append (mem_list, mem);
           ret = iface->read_mem_list(commsio, mem_list);
           g_list_free(mem_list);
        }
        gst_memory_unref(mem);
        return ret;
     }
  }

  return REMOTEOFFLOADCOMMSIO_FAIL;
}

RemoteOffloadCommsIOResult remote_offload_comms_io_read_mem(RemoteOffloadCommsIO *commsio,
                                                            GstMemory *mem)
{
  RemoteOffloadCommsIOInterface *iface;

  if( !REMOTEOFFLOAD_IS_COMMSIO(commsio) ) return REMOTEOFFLOADCOMMSIO_FAIL;

  iface = REMOTEOFFLOAD_COMMSIO_GET_IFACE(commsio);

  if( iface->read_mem )
     return iface->read_mem(commsio, mem);

  if( iface->read_mem_list )
  {
     RemoteOffloadCommsIOResult ret;
     GList *mem_list = NULL;
     mem_list = g_list_append (mem_list, mem);
     ret = iface->read_mem_list(commsio, mem_list);
     g_list_free(mem_list);
     return ret;
  }

  if( iface->read )
  {
     GstMapInfo mapInfo;
     if( gst_memory_map (mem, &mapInfo, GST_MAP_WRITE) )
     {
        RemoteOffloadCommsIOResult ret;
        ret = iface->read(commsio, mapInfo.data, mapInfo.size);
        gst_memory_unmap(mem, &mapInfo);
        return ret;
     }
  }

  return REMOTEOFFLOADCOMMSIO_FAIL;
}

//read into each entry in 'buf_list'
// mem_list is a list of GstMemory*
RemoteOffloadCommsIOResult remote_offload_comms_io_read_mem_list(RemoteOffloadCommsIO *commsio,
                                                                 GList *mem_list)
{
  RemoteOffloadCommsIOInterface *iface;

  if( !REMOTEOFFLOAD_IS_COMMSIO(commsio) ) return REMOTEOFFLOADCOMMSIO_FAIL;

  iface = REMOTEOFFLOAD_COMMSIO_GET_IFACE(commsio);

  if( iface->read_mem_list )
  {
     return iface->read_mem_list(commsio, mem_list);
  }

  if( iface->read_mem || iface->read )
  {
     RemoteOffloadCommsIOResult ret = REMOTEOFFLOADCOMMSIO_SUCCESS;
     for(GList * li = mem_list; li != NULL; li = li->next )
     {
        ret = remote_offload_comms_io_read_mem(commsio, (GstMemory *)li->data);
     }

     return ret;
  }

  return REMOTEOFFLOADCOMMSIO_FAIL;
}


RemoteOffloadCommsIOResult remote_offload_comms_io_write(RemoteOffloadCommsIO *commsio,
                                                         guint8 *buf,
                                                         guint64 size)
{
  RemoteOffloadCommsIOInterface *iface;

  if( !REMOTEOFFLOAD_IS_COMMSIO(commsio) ) return REMOTEOFFLOADCOMMSIO_FAIL;

  iface = REMOTEOFFLOAD_COMMSIO_GET_IFACE(commsio);

  if( iface->write )
     return iface->write(commsio, buf, size);

  if( iface->write_mem || iface->write_mem_list )
  {
     GstMemory *mem = virt_to_mem(buf, size);
     if( mem )
     {
        RemoteOffloadCommsIOResult ret;
        if( iface->write_mem )
        {
           ret = iface->write_mem(commsio, mem);
        }
        else
        {
           GList *mem_list = NULL;
           mem_list = g_list_append (mem_list, mem);
           ret = iface->write_mem_list(commsio, mem_list);
           g_list_free(mem_list);
        }
        gst_memory_unref(mem);
        return ret;
     }
  }

  return REMOTEOFFLOADCOMMSIO_FAIL;
}

RemoteOffloadCommsIOResult remote_offload_comms_io_write_mem(RemoteOffloadCommsIO *commsio,
                                                             GstMemory *mem)
{
  RemoteOffloadCommsIOInterface *iface;

  if( !REMOTEOFFLOAD_IS_COMMSIO(commsio) ) return REMOTEOFFLOADCOMMSIO_FAIL;

  iface = REMOTEOFFLOAD_COMMSIO_GET_IFACE(commsio);

  if( iface->write_mem )
     return iface->write_mem(commsio, mem);

  if( iface->write_mem_list )
  {
     RemoteOffloadCommsIOResult ret;
     GList *mem_list = NULL;
     mem_list = g_list_append (mem_list, mem);
     ret = iface->write_mem_list(commsio, mem_list);
     g_list_free(mem_list);
     return ret;
  }

  if( iface->write )
  {
     GstMapInfo mapInfo;
     if( gst_memory_map (mem, &mapInfo, GST_MAP_READ) )
     {
        RemoteOffloadCommsIOResult ret;
        ret = iface->write(commsio, mapInfo.data, mapInfo.size);
        gst_memory_unmap(mem, &mapInfo);
        return ret;
     }
  }

  return REMOTEOFFLOADCOMMSIO_FAIL;
}

//write each entry in 'buf_list'
// buf list is a list of DataSegments (each one is start ptr, size)
RemoteOffloadCommsIOResult remote_offload_comms_io_write_mem_list(RemoteOffloadCommsIO *commsio,
                                                                  GList *mem_list)
{
  RemoteOffloadCommsIOInterface *iface;

  if( !REMOTEOFFLOAD_IS_COMMSIO(commsio) ) return REMOTEOFFLOADCOMMSIO_FAIL;

  iface = REMOTEOFFLOAD_COMMSIO_GET_IFACE(commsio);

  if( iface->write_mem_list )
  {
     return iface->write_mem_list(commsio, mem_list);
  }

  if( iface->write_mem || iface->write )
  {
     RemoteOffloadCommsIOResult ret = REMOTEOFFLOADCOMMSIO_SUCCESS;
     for(GList * li = mem_list; li != NULL; li = li->next )
     {
        ret = remote_offload_comms_io_write_mem(commsio, (GstMemory *)li->data);
     }

     return ret;
  }

  return REMOTEOFFLOADCOMMSIO_FAIL;
}

GList *remote_offload_comms_io_get_consumable_memfeatures(RemoteOffloadCommsIO *commsio)
{
   RemoteOffloadCommsIOInterface *iface;

   if( !REMOTEOFFLOAD_IS_COMMSIO(commsio) ) return NULL;

   iface = REMOTEOFFLOAD_COMMSIO_GET_IFACE(commsio);

   if( iface->get_consumable_memfeatures )
   {
      return iface->get_consumable_memfeatures(commsio);
   }

   return NULL;
}

GList *remote_offload_comms_io_get_producible_memfeatures(RemoteOffloadCommsIO *commsio)
{
   RemoteOffloadCommsIOInterface *iface;

   if( !REMOTEOFFLOAD_IS_COMMSIO(commsio) ) return NULL;

   iface = REMOTEOFFLOAD_COMMSIO_GET_IFACE(commsio);

   if( iface->get_producible_memfeatures )
   {
      return iface->get_producible_memfeatures(commsio);
   }

   return NULL;
}

void remote_offload_comms_io_shutdown(RemoteOffloadCommsIO *commsio)
{
   RemoteOffloadCommsIOInterface *iface;

   if( !REMOTEOFFLOAD_IS_COMMSIO(commsio) ) return;

   iface = REMOTEOFFLOAD_COMMSIO_GET_IFACE(commsio);

   if( !iface->shutdown ) return;

   return iface->shutdown(commsio);
}

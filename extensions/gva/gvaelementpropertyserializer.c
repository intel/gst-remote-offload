/*
 *  gvaelementpropertyserializer.c - GVAElementPropertySerializer object
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

#include <gio/gio.h>
#include <glib/gprintf.h>
#include "gvaelementpropertyserializer.h"
#include "remoteoffloadelementpropertyserializer.h"

#ifndef NO_SAFESTR
  #include <safe_mem_lib.h>
#else
  #include <string.h>
#endif

struct _GVAElementPropertySerializer
{
  GObject parent_instance;

  GList *tmpFileList;

};

static void
gva_element_property_serializer_interface_init
(RemoteOffloadElementPropertySerializerInterface *iface);

GST_DEBUG_CATEGORY_STATIC (gva_element_property_serializer_debug);
#define GST_CAT_DEFAULT gva_element_property_serializer_debug


G_DEFINE_TYPE_WITH_CODE (
      GVAElementPropertySerializer, gva_element_property_serializer, G_TYPE_OBJECT,
      G_IMPLEMENT_INTERFACE (REMOTEOFFLOADELEMENTPROPERTYSERIALIZER_TYPE,
      gva_element_property_serializer_interface_init)
      GST_DEBUG_CATEGORY_INIT (gva_element_property_serializer_debug,
                               "remoteoffloadgvaelementpropertyserializer", 0,
                                "debug category for GVAElementPropertySerializer"))

#define REMOTEFILESYSTEMPREFIX "remotefilesystem:"

typedef enum
{
   GVAPROPFILETYPE_REMOTEFILESYSTEMSTRING,
   GVAPROPFILETYPE_HOSTXML,
   GVAPROPFILETYPE_HOSTNONXML,
   GVAPROPFILETYPE_INVALID
} GVAPropFileType;

typedef struct _GVAPropFileHeader
{
   GVAPropFileType type;
}GVAPropFileHeader;

static void GFreeDestroy(gpointer data)
{
   g_free(data);
}

static GstMemory* virt_to_gstmemory(void *pVirt, gsize size)
{
   GstMemory *mem = NULL;

   if( pVirt && (size>0) )
   {
      mem= gst_memory_new_wrapped((GstMemoryFlags)0,
                                  pVirt,
                                  size,
                                  0,
                                  size,
                                  pVirt,
                                  GFreeDestroy);
   }

   return mem;
}

GstMemory *FileToGstMemory(RemoteOffloadElementPropertySerializer *propserializer, gchar *filename)
{
   GST_INFO_OBJECT(propserializer, "FileToGstMemory for %s\n", filename);
   GstMemory *mem = NULL;
   if( filename )
   {
      GFile *file = g_file_new_for_path(filename);
      if( file )
      {
         GError *err = NULL;
         gchar *contents = NULL;
         gsize length;

         gboolean fileloadstatus = g_file_load_contents(file,
                                                        NULL, // GCancellable *cancellable
                                                        &contents,
                                                        &length,
                                                        NULL,
                                                        &err);


          if( fileloadstatus )
          {
            //wrap the contents into a GstMemory chunk and append to memArray
            mem = gst_memory_new_wrapped((GstMemoryFlags)0,
                                         contents,
                                         length,
                                         0,
                                         length,
                                         contents,
                                         GFreeDestroy);
          }
          else
          {
             GST_ERROR_OBJECT(propserializer, "g_file_load_contents for %s, failed\n", filename);
             if( err )
             {
                GST_ERROR_OBJECT(propserializer, "%s", err->message);
                g_clear_error (&err);
             }
          }

          g_object_unref(file);
       }
       else
       {
          GST_ERROR_OBJECT(propserializer, "g_file_new_for_path for %s, failed\n", filename);
       }
   }

   return mem;
}

RemoteOffloadPropertySerializerReturn
gva_element_property_serialize(RemoteOffloadElementPropertySerializer *propserializer,
                               GstElement *pElement,
                               const char *propertyName,
                               GArray *memArray)
{
   RemoteOffloadPropertySerializerReturn ret = REMOTEOFFLOAD_PROPSERIALIZER_DEFER;

   if( (g_strcmp0(propertyName, "model") == 0) ||
       (g_strcmp0(propertyName, "model-proc") == 0)
     )
   {
      gchar *filename = NULL;
      g_object_get(pElement, propertyName, &filename, NULL);

     if( filename )
     {
        //The user can add "remotefilesystem:" as a prefix to
        // GVA properties that describe file locations. If set,
        // this means that the user is describing a filesystem
        // location that exists on the remote system, not on
        // the local host. In this case, the path (minus the prefix)
        // will be transferred, instead of attempting to read the file
        // into memory and transferring that.
        if( g_str_has_prefix(filename, REMOTEFILESYSTEMPREFIX) )
        {
           GVAPropFileHeader *pHeader = g_malloc(sizeof(GVAPropFileHeader));
           pHeader->type = GVAPROPFILETYPE_REMOTEFILESYSTEMSTRING;

           GstMemory *pHeaderMem = virt_to_gstmemory(pHeader, sizeof(GVAPropFileHeader));
           if( !pHeaderMem )
           {
              return REMOTEOFFLOAD_PROPSERIALIZER_FAILURE;
           }
           g_array_append_val(memArray, pHeaderMem);

           //Skip the whitespace between the prefix and the filename. We can
           // expect users will set property as: "remotefilesystem: /some/path",
           // and if we send the filename as " /some/path", with extra whitespace
           // at the beginning, the remote-side will report "file not found".
           // So, just fix this for them.
           gchar *remotepathstart = filename + (sizeof(REMOTEFILESYSTEMPREFIX)-1);
           while(g_str_has_prefix(remotepathstart, " "))
              remotepathstart++;

           gchar *remotepath = g_strdup(remotepathstart);
           gsize remotepathsize =
   #ifndef NO_SAFESTR
                      strnlen_s(remotepath, RSIZE_MAX_STR) + 1;
   #else
                      strlen(remotepath) + 1;
   #endif
           GstMemory *pRemotePathMem = virt_to_gstmemory(remotepath, remotepathsize);
           if( !pRemotePathMem )
           {
              return REMOTEOFFLOAD_PROPSERIALIZER_FAILURE;
           }
           g_array_append_val(memArray, pRemotePathMem);
           return REMOTEOFFLOAD_PROPSERIALIZER_SUCCESS;
        }
     }
   }

   if( g_strcmp0(propertyName, "model") == 0 )
   {
     gchar *propertyfilename = NULL;
     g_object_get(pElement, "model", &propertyfilename, NULL);

     GVAPropFileHeader *pHeader = g_malloc(sizeof(GVAPropFileHeader));
     if( propertyfilename )
     {
       if( g_str_has_suffix(propertyfilename, ".xml") )
       {
          pHeader->type = GVAPROPFILETYPE_HOSTXML;
       }
       else
       {
          pHeader->type = GVAPROPFILETYPE_HOSTNONXML;
       }

       GstMemory *pHeaderMem = virt_to_gstmemory(pHeader, sizeof(GVAPropFileHeader));
       GstMemory *filemem = FileToGstMemory(propserializer, propertyfilename);

       if( filemem )
       {
          g_array_append_val(memArray, pHeaderMem);
          g_array_append_val(memArray, filemem);
          ret = REMOTEOFFLOAD_PROPSERIALIZER_SUCCESS;
       }
       else
       {
          gst_memory_unref(pHeaderMem);
          ret = REMOTEOFFLOAD_PROPSERIALIZER_FAILURE;
       }
     }
     else
     {
        g_free(pHeader);
     }

     if( (ret == REMOTEOFFLOAD_PROPSERIALIZER_SUCCESS) &&
         (pHeader->type == GVAPROPFILETYPE_HOSTXML) )
     {
       //we also need to pull the implicit ".bin" which should reside in the same directory as
       // the .xml... yuck.
       gchar **tokens = g_strsplit(propertyfilename, ".xml", 2);
       if( tokens )
       {
          gchar *binfilename = g_strdup_printf("%s.bin", tokens[0]);
          g_sprintf(binfilename, "%s.bin", tokens[0]);

          GstMemory *binfilemem = FileToGstMemory(propserializer, binfilename);
          if( binfilemem )
          {
             g_array_append_val(memArray, binfilemem);
             ret = REMOTEOFFLOAD_PROPSERIALIZER_SUCCESS;
          }
          else
          {
             ret = REMOTEOFFLOAD_PROPSERIALIZER_FAILURE;
          }

          g_free(binfilename);
          g_strfreev(tokens);
       }
       else
       {
          ret = REMOTEOFFLOAD_PROPSERIALIZER_FAILURE;
       }
     }

     if( propertyfilename )
     {
        g_free(propertyfilename);
     }
   }
   else
   if( g_strcmp0(propertyName, "model-proc") == 0 )
   {
      gchar *modelprocfilename = NULL;
      g_object_get(pElement, "model-proc", &modelprocfilename, NULL);
      if( modelprocfilename )
      {
         GstMemory *modelprocmem = FileToGstMemory(propserializer, modelprocfilename);

         if( modelprocmem )
         {
            GVAPropFileHeader *pHeader = g_malloc(sizeof(GVAPropFileHeader));
            pHeader->type = GVAPROPFILETYPE_HOSTNONXML;
            GstMemory *pHeaderMem = virt_to_gstmemory(pHeader, sizeof(GVAPropFileHeader));
            g_array_append_val(memArray, pHeaderMem);
            g_array_append_val(memArray, modelprocmem);
            ret = REMOTEOFFLOAD_PROPSERIALIZER_SUCCESS;
         }
         else
         {
            ret = REMOTEOFFLOAD_PROPSERIALIZER_FAILURE;
         }

         g_free(modelprocfilename);
      }
   }
   else
   if( g_strcmp0(propertyName, "skip-classified-objects") == 0 )
   {
      gboolean bskip_classified_objects;
      g_object_get(pElement, "skip-classified-objects", &bskip_classified_objects, NULL);

      //workaround to avoid seeing this message on target-side when reconstructing:
      //GLib-CRITICAL **: g_hook_get: assertion 'hook_id > 0' failed
      //GStreamer-WARNING **: gstpad.c:1573: pad `0x7f135807e620' has no probe with id `0'
      if( !bskip_classified_objects )
         ret = REMOTEOFFLOAD_PROPSERIALIZER_SUCCESS;
   }

   return ret;
}

static gboolean WriteGstMemoryToFile(GVAElementPropertySerializer *propserializer,
                                     GstMemory *mem, gchar *filename)
{
   GST_INFO_OBJECT(propserializer,
                   "WriteGstMemoryToFile: GstMemory=%p filename=%s", mem, filename);
   gboolean ret = FALSE;

   GstMapInfo mapInfo;
   if( gst_memory_map(mem, &mapInfo, GST_MAP_READ) )
   {
      GError *err = NULL;
      if( g_file_set_contents(filename, (gchar *)mapInfo.data, mapInfo.size, &err) )
      {
        ret = TRUE;
      }
      else
      {
         GST_ERROR_OBJECT(propserializer, "g_file_set_contents failed\n");
         if( err )
         {
            GST_ERROR_OBJECT(propserializer, "%s", err->message);
            g_clear_error (&err);
         }
         ret = FALSE;
      }

       gst_memory_unmap(mem, &mapInfo);
   }
   else
   {
      GST_ERROR_OBJECT(propserializer, "gst_memory_map(%p, .., GST_MAP_READ) failed\n", mem);
   }

   return ret;
}

//template should be some string that has six consecutive X's. ex: "model-XXXXXX.xml"
static GFile *
GstMemoryToTmpFile(GVAElementPropertySerializer *propserializer,
                   GstMemory *mem,
                   const char *template)
{
  if( !mem || !template )
  {
    return NULL;
  }

  GFile *tmpfile = NULL;
  GFileIOStream *stream = NULL;
  GError *err = NULL;
  tmpfile = g_file_new_tmp (template, &stream, &err);
  if( stream ) g_object_unref (stream);
  if( tmpfile )
  {
     gchar *tmpfilename = g_file_get_path (tmpfile);
     GST_INFO_OBJECT(propserializer, "Created tmp file: %s", tmpfilename);

     if( !WriteGstMemoryToFile(propserializer, mem, tmpfilename) )
     {
        GST_ERROR_OBJECT(propserializer, "WriteGstMemoryToFile\n");
        g_object_unref(tmpfile);
        tmpfile = NULL;
     }

     g_free(tmpfilename);
  }
  else
  {
     GST_ERROR_OBJECT(propserializer, "g_file_new_tmp(%s,..) failed\n", template);
     if( err )
     {
        GST_ERROR_OBJECT(propserializer, "%s", err->message);
        g_clear_error (&err);
     }
  }

  if( tmpfile )
     propserializer->tmpFileList = g_list_append (propserializer->tmpFileList, tmpfile);

  return tmpfile;
}

static GFile *GstMemoryToFile(GVAElementPropertySerializer *propserializer,
                              GstMemory *mem,
                              gchar *filename)
{
  if( !mem || !filename )
  {
    return NULL;
  }

  GFile *file = g_file_new_for_path(filename);
  if( file )
  {
     if( !WriteGstMemoryToFile(propserializer, mem, filename) )
     {
        GST_ERROR_OBJECT(propserializer, "WriteGstMemoryToFile failed\n");
        g_object_unref(file);
        file = NULL;
     }
  }
  else
  {
     GST_ERROR_OBJECT(propserializer, "g_file_new_for_path(%s) failed\n", filename);
  }

  if( file )
     propserializer->tmpFileList = g_list_append (propserializer->tmpFileList, file);

  return file;
}


RemoteOffloadPropertySerializerReturn
gva_element_property_deserialize(RemoteOffloadElementPropertySerializer *propserializer,
                                 GstElement *pElement,
                                 const char *propertyName,
                                 GArray *memArray)
{
   GVAElementPropertySerializer *self = ELEMENTPROPERTYSERIALIZER_GVA(propserializer);

   RemoteOffloadPropertySerializerReturn ret = REMOTEOFFLOAD_PROPSERIALIZER_FAILURE;

   GVAPropFileType fileType = GVAPROPFILETYPE_INVALID;
   if( (g_strcmp0(propertyName, "model") == 0) ||
       (g_strcmp0(propertyName, "model-proc") == 0)
     )
   {
      //we know we need at least a header & 1 extra memblock
      if( memArray->len < 2 )
      {
         GST_ERROR_OBJECT(propserializer, "Invalid number of memblocks received");
         return REMOTEOFFLOAD_PROPSERIALIZER_FAILURE;
      }

      //get the header
      GstMemory **propmemarray = (GstMemory **)memArray->data;
      GstMapInfo mapInfo;
      if( gst_memory_map(propmemarray[0], &mapInfo, GST_MAP_READ) )
      {
         if( mapInfo.size != sizeof(GVAPropFileHeader) )
         {
            GST_ERROR_OBJECT(propserializer, "Invalid size for header");
            gst_memory_unmap(propmemarray[0], &mapInfo);
            return REMOTEOFFLOAD_PROPSERIALIZER_FAILURE;
         }

         GVAPropFileHeader *pHeader = (GVAPropFileHeader *)mapInfo.data;
         fileType = pHeader->type;
         gst_memory_unmap(propmemarray[0], &mapInfo);

         if( fileType == GVAPROPFILETYPE_REMOTEFILESYSTEMSTRING )
         {
            GstMapInfo filepathMapInfo;
            if( gst_memory_map(propmemarray[1], &filepathMapInfo, GST_MAP_READ) )
            {
               //set the file path property to the string
               GST_INFO_OBJECT(self, "Setting \"%s\" property to %s", propertyName, (gchar *)filepathMapInfo.data);
               g_object_set(pElement, propertyName, (gchar *)filepathMapInfo.data, NULL);
               gst_memory_unmap(propmemarray[1], &filepathMapInfo);
               return REMOTEOFFLOAD_PROPSERIALIZER_SUCCESS;
            }
            else
            {
               GST_ERROR_OBJECT(propserializer, "Error mapping filesystem path for READ access");
               return REMOTEOFFLOAD_PROPSERIALIZER_FAILURE;
            }
         }
      }
      else
      {
         GST_ERROR_OBJECT(propserializer, "Error mapping header for READ access");
         return REMOTEOFFLOAD_PROPSERIALIZER_FAILURE;
      }
   }

   if( g_strcmp0(propertyName, "model") == 0 )
   {
     GstMemory **propmemarray = (GstMemory **)memArray->data;

     //this property requires at least 2 memblocks, 1 for the header, 1 for the model file.
     // If the model file is an xml, we require 3 (one more for the .bin)
     //
     if( fileType == GVAPROPFILETYPE_HOSTNONXML )
     {
        if( memArray->len >= 2 )
        {
           GFile *blobtmpfile = GstMemoryToTmpFile(self, propmemarray[1], "model-XXXXXX.blob");
           if( blobtmpfile )
           {
              gchar *blobfilename = g_file_get_path (blobtmpfile);
              GST_INFO_OBJECT(self, "Setting \"model\" property to %s", blobfilename);
              g_object_set(pElement, "model", blobfilename, NULL);
              ret = REMOTEOFFLOAD_PROPSERIALIZER_SUCCESS;
           }
           else
           {
              GST_ERROR_OBJECT(self, "GstMemoryToTmpFile for blob file failed\n");
           }
        }
        else
        {
           GST_ERROR_OBJECT(self, "Wrong # of memblocks for receiving model as non-xml / blob");
        }
     }
     else
     {
        if( memArray->len >= 3 )
        {
           GFile *xmltmpfile = GstMemoryToTmpFile(self, propmemarray[1], "model-XXXXXX.xml");
           if( xmltmpfile )
           {
              gchar *xmlfilename = g_file_get_path (xmltmpfile);

              GST_INFO_OBJECT(self, "Setting \"model\" property to %s", xmlfilename);
              g_object_set(pElement, "model", xmlfilename, NULL);

              gchar **tokens = g_strsplit(xmlfilename, ".xml", 2);
              if( tokens )
              {
                 gchar *binfilename = g_strdup_printf("%s.bin", tokens[0]);
                 GFile *binfile = GstMemoryToFile(self, propmemarray[2], binfilename);
                 if( binfile )
                 {
                    ret = REMOTEOFFLOAD_PROPSERIALIZER_SUCCESS;
                 }
                 else
                 {
                    GST_ERROR_OBJECT(self, "GstMemoryToFile for bin file failed\n");
                 }

                 g_free(binfilename);
                 g_strfreev(tokens);
              }
              else
              {
                 GST_ERROR_OBJECT(self, "g_strsplit(\"%s\", \".xml\", 2) failed", xmlfilename);
              }
           }
           else
           {
              GST_ERROR_OBJECT(self, "GstMemoryToTmpFile for xml file failed\n");
           }
        }
        else
        {
           GST_ERROR_OBJECT(self, "Wrong # of memblocks for receiving model as xml");
        }
     }
   }
   else
   if( g_strcmp0(propertyName, "model-proc") == 0 )
   {
      GstMemory **propmemarray = (GstMemory **)memArray->data;

      if( fileType != GVAPROPFILETYPE_HOSTNONXML )
      {
         GST_ERROR_OBJECT(self, "Unexpected header file type for \"model-proc\" property\n");
         return REMOTEOFFLOAD_PROPSERIALIZER_FAILURE;
      }

      //this property requires exactly 2 memblocks..
      //1 for the header, 1 for the json file
      if( memArray->len == 2 )
      {
         GFile *jsontmpfile = GstMemoryToTmpFile(self, propmemarray[1], "model-XXXXXX.json");
         if( jsontmpfile )
         {
            gchar *jsontmpfilename = g_file_get_path (jsontmpfile);
            GST_INFO_OBJECT(self, "Setting \"model-proc\" property to %s", jsontmpfilename);
            g_object_set(pElement, "model-proc", jsontmpfilename, NULL);

            ret = REMOTEOFFLOAD_PROPSERIALIZER_SUCCESS;

            g_free(jsontmpfilename);
         }
         else
         {
            GST_ERROR_OBJECT(self, "GstMemoryToTmpFile for json file failed\n");
         }
      }
      else
      {
         GST_ERROR_OBJECT(propserializer, "model-proc parameter requires exactly 2 memblocks\n");
      }
   }
   else
   if( g_strcmp0(propertyName, "skip-classified-objects") == 0 )
   {
      //workaround to avoid seeing this message on target-side when reconstructing:
      //GLib-CRITICAL **: g_hook_get: assertion 'hook_id > 0' failed
      //GStreamer-WARNING **: gstpad.c:1573: pad `0x7f135807e620' has no probe with id `0'
      ret = REMOTEOFFLOAD_PROPSERIALIZER_SUCCESS;
   }

   return ret;
}

static void gva_element_property_serializer_interface_init
(RemoteOffloadElementPropertySerializerInterface *iface)
{
   iface->serialize = gva_element_property_serialize;
   iface->deserialize = gva_element_property_deserialize;
}

static void
gva_element_property_serializer_finalize (GObject *gobject)
{
   GVAElementPropertySerializer *self = ELEMENTPROPERTYSERIALIZER_GVA(gobject);

   if( self->tmpFileList )
   {
      GList *li;
      for(li = self->tmpFileList; li != NULL; li = li->next )
      {
         GFile *file = (GFile *)li->data;
         if( file )
         {
            gchar *filename = g_file_get_path (file);
            GST_INFO_OBJECT(self, "Deleting %s\n", filename);
            GError *err = NULL;
            if(!g_file_delete(file, NULL, &err))
            {
              GST_ERROR_OBJECT(self, "Error in g_file_delete for %s\n", filename);
            }
            g_free(filename);
            g_object_unref(file);
         }
      }

      g_list_free(self->tmpFileList);
   }

   G_OBJECT_CLASS (gva_element_property_serializer_parent_class)->finalize (gobject);
}

static void
gva_element_property_serializer_class_init (GVAElementPropertySerializerClass *klass)
{
   GObjectClass *object_class = G_OBJECT_CLASS (klass);

   object_class->finalize = gva_element_property_serializer_finalize;
}

static void
gva_element_property_serializer_init (GVAElementPropertySerializer *self)
{
  self->tmpFileList = NULL;
}

GVAElementPropertySerializer *gva_element_property_serializer_new()
{
   GVAElementPropertySerializer *serializer =
         g_object_new(GVAELEMENTPROPERTYSERIALIZER_TYPE, NULL);
   return serializer;
}

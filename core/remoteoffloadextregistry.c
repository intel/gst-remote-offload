/*
 *  remoteoffloadextregistry.c - RemoteOffloadExtRegistry interface
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
#include <glib/gstdio.h>
#include <gst/gst.h>
#include "remoteoffloadextregistry.h"


/* Private structure definition. */
typedef struct {
   GMutex registrymutex;
   GList *entry_list;
}RemoteOffloadExtRegistryPrivate;

typedef struct {
   GModule *module;
   RemoteOffloadExtension *ext;
}RemoteOffloadExtensionEntry;

struct _RemoteOffloadExtRegistry
{
  GObject parent_instance;

  RemoteOffloadExtRegistryPrivate priv;
};

GST_DEBUG_CATEGORY_STATIC (registry_debug);
#define GST_CAT_DEFAULT registry_debug

#define REMOTEOFFLOADEXTREGISTRY_TYPE (remote_offload_ext_registry_get_type ())
G_DECLARE_FINAL_TYPE (RemoteOffloadExtRegistry, remote_offload_ext_registry,
                      REMOTEOFFLOAD, EXTREGISTRY, GObject)

G_DEFINE_TYPE_WITH_CODE(RemoteOffloadExtRegistry, remote_offload_ext_registry, G_TYPE_OBJECT,
GST_DEBUG_CATEGORY_INIT (registry_debug, "remoteoffloadextregistry", 0,
                         "debug category for RemoteOffloadExtRegistry"))


static void remote_offload_ext_registry_process_file(RemoteOffloadExtRegistry *self,
                                                     const gchar *filename)
{
   GST_DEBUG_OBJECT(self, "%s", filename);

   if( g_file_test(filename, G_FILE_TEST_IS_REGULAR) )
   {
      GStatBuf file_status;
      if( !g_stat(filename, &file_status) )
      {
         GModuleFlags flags = G_MODULE_BIND_LOCAL;

         GModule *module = g_module_open (filename, flags);
         if( module )
         {
            gpointer ptr;
            if( g_module_symbol(module, "remoteoffload_extension_entry", &ptr) )
            {
               ExtensionEntry entry_func = (ExtensionEntry)ptr;

               g_module_make_resident(module);

               RemoteOffloadExtension *ext = entry_func();
               if( REMOTEOFFLOAD_IS_EXTENSION(ext) )
               {
                 RemoteOffloadExtensionEntry *entry = g_malloc(sizeof(RemoteOffloadExtensionEntry));
                 entry->module = module;
                 entry->ext = ext;
                 self->priv.entry_list = g_list_append(self->priv.entry_list, entry);
               }
               else
               {
                  GST_WARNING_OBJECT(self,
                                     "\"remoteoffload_extension_entry\" in %s returned "
                                     "invalid RemoteOffloadExtension", filename);
               }
            }
            else
            {
               GST_WARNING_OBJECT(self,
                                  "\"remoteoffload_extension_entry\" symbol not found in %s",
                                  filename);
               g_module_close (module);
            }
         }
         else
         {
            GST_WARNING_OBJECT(self,
                               "module_open failed for filename %s: %s",
                               filename, g_module_error ());
         }
      }
      else
      {
         GST_WARNING_OBJECT(self, "Problem accessing file %s", filename);
      }
   }
   else
   {
      GST_WARNING_OBJECT(self, "Skipping %s", filename);
   }
}

static void remote_offload_ext_registry_process_dir(RemoteOffloadExtRegistry *self,
                                                    gchar *dirname)
{
   GST_DEBUG_OBJECT(self, "%s", dirname);

   GError *err;
   GDir *dir = g_dir_open(dirname,
                          0,
                          &err);
   if( dir )
   {
      const gchar *filename;
      while((filename = g_dir_read_name(dir)))
      {
         gchar *fullfilename = g_strconcat(dirname, "/", filename, NULL);
         remote_offload_ext_registry_process_file(self, fullfilename);
         g_free(fullfilename);
      }
      g_dir_close(dir);
   }
   else
   {
      GST_WARNING_OBJECT(self, "g_dir_open(%s) failed", dirname);
      if( err && err->message)
      {
         GST_WARNING_OBJECT(self, "%s", err->message);
         g_clear_error(&err);
      }
   }
}

static void remote_offload_ext_registry_constructed(GObject *object)
{
   RemoteOffloadExtRegistry *self = (RemoteOffloadExtRegistry *)object;
   gchar **env = g_get_environ();
   const gchar *pluginpathenv = g_environ_getenv(env,"GST_REMOTEOFFLOAD_PLUGIN_PATH");
   if( pluginpathenv )
   {
     if (g_module_supported ())
     {
        gchar **dirtokens = g_strsplit(pluginpathenv,
                                       ":",
                                       -1);
        if( dirtokens )
        {
           gint diri = 0;
           gchar *dirname = dirtokens[diri++];
           while( (dirname != NULL) && (*dirname) )
           {
             remote_offload_ext_registry_process_dir(self, dirname);
             dirname = dirtokens[diri++];
           }

           g_strfreev(dirtokens);
        }
        else
        {
           GST_ERROR_OBJECT(self, "g_strsplit failed");
        }
     }
     else
     {
        GST_WARNING_OBJECT(self, "Module loading not supported");
     }

   }
   else
   {
      GST_WARNING_OBJECT(self, "GST_REMOTEOFFLOAD_PLUGIN_PATH env variable not set");
   }
   g_strfreev(env);

   G_OBJECT_CLASS (remote_offload_ext_registry_parent_class)->constructed (object);
}

static void
remote_offload_ext_registry_finalize (GObject *gobject)
{
   RemoteOffloadExtRegistry *self = (RemoteOffloadExtRegistry *)gobject;

   if( self->priv.entry_list )
   {
      for( GList *li = self->priv.entry_list; li != NULL; li = li->next )
      {
         RemoteOffloadExtensionEntry *entry = li->data;

         g_object_unref(entry->ext);
         g_module_close(entry->module);
         g_free(entry);
      }

      g_list_free(self->priv.entry_list);
   }

   G_OBJECT_CLASS (remote_offload_ext_registry_parent_class)->finalize (gobject);
}

static GObject *
remote_offload_ext_registry_constructor(GType type,
                                  guint n_construct_params,
                                  GObjectConstructParam *construct_params)
{
   static GObject *self = NULL;

   if( self == NULL )
   {
      self = G_OBJECT_CLASS (remote_offload_ext_registry_parent_class)->constructor(
          type, n_construct_params, construct_params);
      g_object_add_weak_pointer (self, (gpointer) &self);
      return self;
   }

  return g_object_ref (self);
}

static void
remote_offload_ext_registry_class_init (RemoteOffloadExtRegistryClass *klass)
{
   GObjectClass *object_class = G_OBJECT_CLASS (klass);

   object_class->constructor = remote_offload_ext_registry_constructor;
   object_class->constructed = remote_offload_ext_registry_constructed;
   object_class->finalize = remote_offload_ext_registry_finalize;
}

static void
remote_offload_ext_registry_init (RemoteOffloadExtRegistry *self)
{
   g_mutex_init(&self->priv.registrymutex);
   self->priv.entry_list = NULL;
}

static GMutex singletonlock;

RemoteOffloadExtRegistry *remote_offload_ext_registry_get_instance()
{
   //only allow 1 thread to obtain an instance of RemoteOffloadExtRegistry at a time.
   g_mutex_lock(&singletonlock);
   RemoteOffloadExtRegistry *pRegistry = g_object_new(REMOTEOFFLOADEXTREGISTRY_TYPE, NULL);
   g_mutex_unlock(&singletonlock);

   return pRegistry;
}

void remote_offload_ext_registry_unref(RemoteOffloadExtRegistry *reg)
{
   if( !REMOTEOFFLOAD_IS_EXTREGISTRY(reg) ) return;

   g_mutex_lock(&singletonlock);
   g_object_unref(reg);
   g_mutex_unlock(&singletonlock);
}

GArray *remote_offload_ext_registry_generate(RemoteOffloadExtRegistry *reg,
                                             GType type)
{
   if( !REMOTEOFFLOAD_IS_EXTREGISTRY(reg) ) return NULL;

   GArray *pairarray = g_array_new(FALSE, FALSE, sizeof(RemoteOffloadExtTypePair));

   g_mutex_lock(&reg->priv.registrymutex);

   if( reg->priv.entry_list )
   {
      for( GList *li = reg->priv.entry_list; li != NULL; li = li->next )
      {
         RemoteOffloadExtensionEntry *entry = li->data;

         GArray *entryarray = remote_offload_extension_generate(entry->ext,
                                                                type);
         if( entryarray )
         {
            for(guint i = 0; i < entryarray->len; i++ )
            {
               g_array_append_val(pairarray,
                                  g_array_index(entryarray, RemoteOffloadExtTypePair, i));
            }
            g_array_free(entryarray, TRUE);
         }
      }
   }

   g_mutex_unlock(&reg->priv.registrymutex);

   return pairarray;
}

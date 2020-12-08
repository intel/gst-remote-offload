/*
 *  remoteoffloadpipelinelogger.c - RemoteOffloadPipelineLogger object
 *
 *  Copyright (C) 2020 Intel Corporation
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

#include <gst/gst.h>
#include <malloc.h>
#include <unistd.h>
#ifndef NO_SAFESTR
  #include <safe_mem_lib.h>
#else
  #include <string.h>
#endif
#include "remoteoffloadpipelinelogger.h"

//default 2 buffers, each buffer size = 128k bytes
#define LOGDATAEXCHANGER_DEFAULT_NUM_LOG_BUFFERS 2
#define LOGDATAEXCHANGER_DEFAULT_LOG_BUFFER_SIZE 128*1024

// Maximum time that the flusher thread will wait
// before force-flushing the current contents of the
// active log buffer.
#define LOGDATAEXCHANGER_FLUSH_LOG_TIMEOUT_MS 1000

typedef struct _LoggingBuffer
{
   gchar *logmem_baseptr;
   guint nwritten_chars;
}LoggingBuffer;

typedef struct _ROPInstanceEntry
{
   guint active_start;
   GenericDataExchanger *exchanger;
   LoggingBuffer *first_active_buffer;
}ROPInstanceEntry;

struct _RemoteOffloadPipelineLogger
{
  GObject parent_instance;

  /* Other members, including private data. */
  GstClockTime  basetime;

  GstDebugLevel base_default_threshold;
  gchar *base_gst_debug;
  gchar *gst_debug_now;

  LoggingBuffer *active_log_buffer;

  GMutex ropinstance_entries_mutex;
  GList *ropinstance_entries_list;

  GMutex freepoolmutex;
  GQueue *loggingBufferFreePool;

  GThread *flusherThread;
  gboolean bThreadRun;
  GMutex activetransferpoolmutex;
  GCond activetransferpoolcond;
  GQueue *loggingBufferActiveTransferQueue;

  GMutex flushidlemutex;
  GCond flushidlecond;

  gboolean is_state_okay;
};

#define ROP_LOGGER_TYPE (rop_logger_get_type ())
G_DECLARE_FINAL_TYPE (RemoteOffloadPipelineLogger, rop_logger, ROP, LOGGER, GObject)

GST_DEBUG_CATEGORY_STATIC (rop_logger_debug);
#define GST_CAT_DEFAULT rop_logger_debug

G_DEFINE_TYPE_WITH_CODE(RemoteOffloadPipelineLogger, rop_logger, G_TYPE_OBJECT,
GST_DEBUG_CATEGORY_INIT (rop_logger_debug, "remoteoffloadpipelinelogger", 0,
                         "debug category for RemoteOffloadPipelineLogger"))

static GMutex g_roplogger_singleton_lock;
static GMutex g_roplogger_mutex;
gboolean g_bactive;

static gpointer rop_logger_flush_thread(RemoteOffloadPipelineLogger *self);
static void roplogger_gst_debug_log_func(GstDebugCategory * category, GstDebugLevel level,
    const gchar * file, const gchar * function, gint line,
    GObject * object, GstDebugMessage * message, gpointer user_data);

static inline LoggingBuffer* new_log_buffer()
{
   LoggingBuffer *log_buf = g_malloc(sizeof(LoggingBuffer));
   if( log_buf )
   {
      log_buf->logmem_baseptr = NULL;
      log_buf->nwritten_chars = 0;
      if( posix_memalign((void **)&(log_buf->logmem_baseptr),
                  getpagesize(), LOGDATAEXCHANGER_DEFAULT_LOG_BUFFER_SIZE) )
      {
         g_free(log_buf);
         log_buf = NULL;
      }
   }

   return log_buf;
}

static inline void destroy_log_buffer(LoggingBuffer *log_buf)
{
   if( log_buf )
   {
      if( log_buf->logmem_baseptr )
         free(log_buf->logmem_baseptr);

      g_free(log_buf);
   }
}

static inline LoggingBuffer* acquire_logging_buffer(RemoteOffloadPipelineLogger *self)
{
   LoggingBuffer *buf;
   g_mutex_lock(&self->freepoolmutex);
   buf = g_queue_pop_head(self->loggingBufferFreePool);
   if( !buf )
   {
      //nothing in the free pool, need to allocate a new one.
      buf = new_log_buffer();
   }
   g_mutex_unlock(&self->freepoolmutex);

   return buf;
}

static inline void done_with_logging_buffer(RemoteOffloadPipelineLogger *self,
                                            LoggingBuffer *logbuf)
{
   g_mutex_lock(&self->freepoolmutex);
   if( g_queue_get_length(self->loggingBufferFreePool)
         < LOGDATAEXCHANGER_DEFAULT_NUM_LOG_BUFFERS )
   {
      g_queue_push_head(self->loggingBufferFreePool, logbuf);
   }
   else
   {
      //since we already have LOGDATAEXCHANGER_DEFAULT_NUM_LOG_BUFFERS in the
      // free queue, it means that we expanded the default number of buffers
      // at some point.. so, free this one up now.
      destroy_log_buffer(logbuf);
   }
   g_mutex_unlock(&self->freepoolmutex);
}

static void rop_logger_constructed(GObject *object)
{
   RemoteOffloadPipelineLogger *self = ROP_LOGGER(object);

   self->is_state_okay = TRUE;
   self->basetime = gst_util_get_timestamp ();

   self->base_default_threshold = gst_debug_get_default_threshold();
   const gchar *env_gst_debug = g_getenv ("GST_DEBUG");
   if( env_gst_debug )
   {
      self->base_gst_debug = g_strdup(env_gst_debug);
   }

   self->active_log_buffer = NULL;
   for( int i = 0; i < LOGDATAEXCHANGER_DEFAULT_NUM_LOG_BUFFERS; i++ )
   {
      LoggingBuffer *log_buf = new_log_buffer();
      if( log_buf )
      {
         g_queue_push_head(self->loggingBufferFreePool, log_buf);
      }
      else
      {
         GST_ERROR_OBJECT (self,
                          "Error creating new log buffer");
         self->is_state_okay = FALSE;
      }
   }

   if( self->is_state_okay )
   {
      //might as well acquire an active log buffer right now.
      self->active_log_buffer = acquire_logging_buffer(self);

      self->flusherThread =
            g_thread_new("rop_logger",
            (GThreadFunc)rop_logger_flush_thread,
            self);

      g_mutex_lock(&g_roplogger_mutex);
      g_bactive = TRUE;
      g_mutex_unlock(&g_roplogger_mutex);
      gst_debug_add_log_function(roplogger_gst_debug_log_func,
                                 self,
                                 NULL);

   }

   G_OBJECT_CLASS (rop_logger_parent_class)->constructed (object);
}

static void
rop_logger_finalize (GObject *gobject)
{
   RemoteOffloadPipelineLogger *self = ROP_LOGGER(gobject);

   g_mutex_lock(&g_roplogger_mutex);
   g_bactive = FALSE;
   g_mutex_unlock(&g_roplogger_mutex);
   gst_debug_remove_log_function(roplogger_gst_debug_log_func);

   //If this is set, it means that the default GST_DEBUG
   // were raised by the user. We want to reset these levels
   // back to what they were before the user raised them.
   if( self->gst_debug_now )
   {
      //It seems that setting reset=TRUE in the call to
      // 'gst_debug_set_threshold_from_string' doesn't actually
      // clear all current entries.. as categories that have been
      // specifically raised (i.e. GST_CAPS:5) remain raised to
      // that debug level. What appears to work is to:
      // 1.)
      //   Call gst_debug_set_threshold_from_string with something
      //   like "*:2" to bring all current categories down to that
      //   level (btw, gst_debug_set_default_threshold doesn't work
      //   for this part),
      // 2.) Then call gst_debug_set_threshold_from_string with the
      //   original GST_DEBUG string, to escalate the default entries.
      gchar *default_level_str = g_strdup_printf("*:%d",
                                                  self->base_default_threshold);
      gst_debug_set_threshold_from_string(default_level_str, TRUE);
      g_free(default_level_str);

      if( self->base_gst_debug )
      {
         gst_debug_set_threshold_from_string(self->base_gst_debug, TRUE);
      }
      g_free(self->gst_debug_now);
   }

   if( self->base_gst_debug )
      g_free(self->base_gst_debug);

   if( self->flusherThread )
   {
      g_mutex_lock(&self->activetransferpoolmutex);
      self->bThreadRun = FALSE;
      g_cond_broadcast (&self->activetransferpoolcond);
      g_mutex_unlock (&self->activetransferpoolmutex);
      g_thread_join (self->flusherThread);
   }

   if( self->loggingBufferFreePool )
   {
      while(!g_queue_is_empty(self->loggingBufferFreePool))
      {
         LoggingBuffer *buf = g_queue_pop_head(self->loggingBufferFreePool);
         if( buf )
         {
            destroy_log_buffer(buf);
         }
      }
      g_queue_free(self->loggingBufferFreePool);
   }

   if( self->active_log_buffer )
      destroy_log_buffer(self->active_log_buffer);

   self->active_log_buffer = NULL;

   g_mutex_clear(&self->flushidlemutex);
   g_cond_clear(&self->flushidlecond);

   g_queue_free(self->loggingBufferActiveTransferQueue);
   g_cond_clear(&self->activetransferpoolcond);
   g_mutex_clear(&self->activetransferpoolmutex);

   g_mutex_clear(&self->freepoolmutex);

   G_OBJECT_CLASS (rop_logger_parent_class)->finalize (gobject);
}

static GObject *
rop_logger_constructor(GType type,
                       guint n_construct_params,
                       GObjectConstructParam *construct_params)
{
   static GObject *self = NULL;

   if( self == NULL )
   {
      self = G_OBJECT_CLASS (rop_logger_parent_class)->constructor(
          type, n_construct_params, construct_params);
      g_object_add_weak_pointer (self, (gpointer) &self);
      return self;
   }

   return g_object_ref (self);
}

static void
rop_logger_class_init (RemoteOffloadPipelineLoggerClass *klass)
{
   GObjectClass *object_class = G_OBJECT_CLASS (klass);

   object_class->constructor = rop_logger_constructor;
   object_class->constructed = rop_logger_constructed;
   object_class->finalize = rop_logger_finalize;
}

static void
rop_logger_init(RemoteOffloadPipelineLogger *self)
{

  g_mutex_init(&self->ropinstance_entries_mutex);
  self->ropinstance_entries_list = NULL;

  self->base_gst_debug = NULL;
  self->gst_debug_now = NULL;

  self->active_log_buffer = NULL;
  g_mutex_init(&self->freepoolmutex);
  self->loggingBufferFreePool = g_queue_new();

  self->bThreadRun = TRUE;
  self->flusherThread = NULL;
  g_mutex_init(&self->activetransferpoolmutex);
  g_cond_init(&self->activetransferpoolcond);
  self->loggingBufferActiveTransferQueue = g_queue_new();

  g_cond_init(&self->flushidlecond);
  g_mutex_init(&self->flushidlemutex);

  self->is_state_okay = FALSE;
  self->base_default_threshold = GST_LEVEL_NONE;
}

RemoteOffloadPipelineLogger *rop_logger_get_instance()
{
   //only allow 1 thread to obtain an instance of XLinkChannelManager at a time.
   g_mutex_lock(&g_roplogger_singleton_lock);
   RemoteOffloadPipelineLogger *pLogger =
         g_object_new(ROP_LOGGER_TYPE, NULL);
   if( pLogger )
   {
      if( !pLogger->is_state_okay )
      {
         g_object_unref(pLogger);
         pLogger = NULL;
      }
   }
   g_mutex_unlock(&g_roplogger_singleton_lock);

   return pLogger;
}

void rop_logger_unref(RemoteOffloadPipelineLogger *logger)
{
   if( !ROP_IS_LOGGER(logger) ) return;

   g_mutex_lock(&g_roplogger_singleton_lock);
   g_object_unref(logger);
   g_mutex_unlock(&g_roplogger_singleton_lock);
}

static gpointer rop_logger_flush_thread(RemoteOffloadPipelineLogger *self)
{
   g_mutex_lock(&self->activetransferpoolmutex);
   while( 1 )
   {
      LoggingBuffer *logbuf =
            g_queue_pop_head(self->loggingBufferActiveTransferQueue);

      if( logbuf )
      {
         g_mutex_unlock(&self->activetransferpoolmutex);
         if( logbuf->nwritten_chars )
         {
            //send the contents of this log buffer through ROPInstance
            // via the generic data exchanger
            g_mutex_lock(&self->ropinstance_entries_mutex);
            GList *listentry;
            for( listentry = self->ropinstance_entries_list;
                 listentry != NULL;
                 listentry = listentry->next)
            {
               ROPInstanceEntry *entry
                   = (ROPInstanceEntry *)listentry->data;

               if( entry->first_active_buffer &&
                   entry->first_active_buffer != logbuf )
                  continue;

               if( (logbuf->nwritten_chars > entry->active_start) &&
                   ((logbuf->nwritten_chars - entry->active_start) > 0)
                  )
               {
                  generic_data_exchanger_send_virt(entry->exchanger,
                                                   BINPIPELINE_EXCHANGE_LOGMESSAGE,
                                                   logbuf->logmem_baseptr + entry->active_start,
                                                   (logbuf->nwritten_chars - entry->active_start)+1,
                                                   FALSE);
               }


               entry->active_start = 0;
               entry->first_active_buffer = NULL;
            }
            g_mutex_unlock(&self->ropinstance_entries_mutex);

            //reset the log buffer
            logbuf->nwritten_chars = 0;
         }

         //push this log buffer back into free pool
         done_with_logging_buffer(self, logbuf);

         g_mutex_lock(&self->activetransferpoolmutex);
      }
      else
      {
         //wake up threads waiting on all active queue entries getting flushed
         g_mutex_lock(&self->flushidlemutex);
         g_cond_broadcast (&self->flushidlecond);
         g_mutex_unlock(&self->flushidlemutex);

         if( !self->bThreadRun )
         {
            break;
         }

         gint64 end_time =
            g_get_monotonic_time () + LOGDATAEXCHANGER_FLUSH_LOG_TIMEOUT_MS*1000;

         //wait to be signaled
         if( !g_cond_wait_until (&self->activetransferpoolcond,
                                &self->activetransferpoolmutex,
                                end_time) )
         {
            //we haven't flushed log contents in a while. Force this action now.
            g_mutex_lock(&g_roplogger_mutex);
            if( self->active_log_buffer )
            {
               g_queue_push_head(self->loggingBufferActiveTransferQueue, self->active_log_buffer);
               self->active_log_buffer = NULL;
            }
            g_mutex_unlock(&g_roplogger_mutex);
         }
      }
   }
   g_mutex_unlock(&self->activetransferpoolmutex);

   return NULL;
}

/* based on g_basename(), which we can't use because it was deprecated */
static inline const gchar *
gst_path_basename (const gchar * file_name)
{
  register const gchar *base;

  base = strrchr (file_name, G_DIR_SEPARATOR);

  {
    const gchar *q = strrchr (file_name, '/');
    if (base == NULL || (q != NULL && q > base))
      base = q;
  }

  if (base)
    return base + 1;

  if (g_ascii_isalpha (file_name[0]) && file_name[1] == ':')
    return file_name + 2;

  return file_name;
}

static gchar *
gst_debug_print_object (gpointer ptr)
{
  GObject *object = (GObject *) ptr;

  /* nicely printed object */
  if (object == NULL) {
    return g_strdup ("(NULL)");
  }
  if (GST_IS_CAPS (ptr)) {
    return gst_caps_to_string ((const GstCaps *) ptr);
  }

  if (*(GType *) ptr == GST_TYPE_CAPS_FEATURES) {
    return gst_caps_features_to_string ((const GstCapsFeatures *) ptr);
  }

  if (GST_IS_PAD (object) && GST_OBJECT_NAME (object)) {
    return g_strdup_printf ("<%s:%s>", GST_DEBUG_PAD_NAME (object));
  }
  if (GST_IS_OBJECT (object) && GST_OBJECT_NAME (object)) {
    return g_strdup_printf ("<%s>", GST_OBJECT_NAME (object));
  }
  if (G_IS_OBJECT (object)) {
    return g_strdup_printf ("<%s@%p>", G_OBJECT_TYPE_NAME (object), object);
  }

  return g_strdup_printf ("%p", ptr);
}

#define CAT_FMT "%20s %s:%d:%s:%s"

static void roplogger_gst_debug_log_func(GstDebugCategory * category, GstDebugLevel level,
    const gchar * file, const gchar * function, gint line,
    GObject * object, GstDebugMessage * message, gpointer user_data)
{
   GstClockTime elapsed;
   gchar *obj = NULL;
   const gchar *message_str;
   gchar c;

   /* Get message string first because printing it might call into our custom
   * printf format extension mechanism which in turn might log something, e.g.
   * from inside gst_structure_to_string() when something can't be serialised.
   * This means we either need to do this outside of any critical section or
   * use a recursive lock instead. As we always need the message string in all
   * code paths, we might just as well get it here first thing and outside of
   * the win_print_mutex critical section. */
   message_str = gst_debug_message_get (message);

   if (object) {
    obj = gst_debug_print_object (object);
  } else {
    obj = (gchar *) "";
  }

   /* __FILE__ might be a file name or an absolute path or a
   * relative path, irrespective of the exact compiler used,
   * in which case we want to shorten it to the filename for
   * readability. */
   c = file[0];
   if (c == '.' || c == '/' || c == '\\' || (c != '\0' && file[1] == ':')) {
     file = gst_path_basename (file);
   }

   //Warning! Nothing put inside this critical section should attempt to write
   // to gst debug.. as it will recursively make it's way back here, and deadlock at this mutex.
   g_mutex_lock(&g_roplogger_mutex);
   RemoteOffloadPipelineLogger *self = ROP_LOGGER(user_data);
   if( g_bactive )
   {
      elapsed = GST_CLOCK_DIFF (self->basetime, gst_util_get_timestamp ());
      if( G_LIKELY(!self->active_log_buffer) )
      {
         self->active_log_buffer = acquire_logging_buffer(self);
      }
      LoggingBuffer *active_log_buffer = self->active_log_buffer;
      if( G_LIKELY(active_log_buffer) )
      {
         gint nwritten = g_snprintf(
               &active_log_buffer->logmem_baseptr[active_log_buffer->nwritten_chars],
               LOGDATAEXCHANGER_DEFAULT_LOG_BUFFER_SIZE - active_log_buffer->nwritten_chars,
               "%" GST_TIME_FORMAT" %s "CAT_FMT" %s\n",
               GST_TIME_ARGS (elapsed),
               gst_debug_level_get_name (level),
               gst_debug_category_get_name (category), file, line, function, obj,
               message_str);

         //if this case is true, we need to flush this buffer (i.e. send contents back to client)
         if( (nwritten <= 0) ||
             ((active_log_buffer->nwritten_chars + nwritten)
                   >= LOGDATAEXCHANGER_DEFAULT_LOG_BUFFER_SIZE) )
         {
            //cancel out what we wrote before (i.e. write NULL char)...
            // we don't spit messages across two separate logging buffers
            active_log_buffer->logmem_baseptr[active_log_buffer->nwritten_chars] = 0;

            //push this log buffer into the active transfer queue & wake up thread
            g_mutex_lock(&self->activetransferpoolmutex);
            g_queue_push_head(self->loggingBufferActiveTransferQueue, active_log_buffer);
            g_cond_broadcast(&self->activetransferpoolcond);
            g_mutex_unlock(&self->activetransferpoolmutex);

            //acquire a fresh buffer
            self->active_log_buffer = acquire_logging_buffer(self);
            if( G_LIKELY(self->active_log_buffer) )
            {
               active_log_buffer = self->active_log_buffer;

               //write to the new buffer
               nwritten = g_snprintf(
                  active_log_buffer->logmem_baseptr,
                  LOGDATAEXCHANGER_DEFAULT_LOG_BUFFER_SIZE,
                  "%" GST_TIME_FORMAT" %s "CAT_FMT" %s\n",
                  GST_TIME_ARGS (elapsed),
                  gst_debug_level_get_name (level),
                  gst_debug_category_get_name (category), file, line, function, obj,
                  message_str);

               if( (nwritten <= 0) ||
                ((active_log_buffer->nwritten_chars + nwritten)
                      >= LOGDATAEXCHANGER_DEFAULT_LOG_BUFFER_SIZE) )
               {
                  //well, we can't print to the gst trace *from* this function, as that'll
                  // circle back to this function, and we'll deadlock... and we don't want
                  // to trigger some endless recursive loop. So, just print to stdout..
                  // so it can at least be captured/saved to the device-side system log.
                  g_print("Error writing message into fixed buffer size of %u bytes",
                           LOGDATAEXCHANGER_DEFAULT_LOG_BUFFER_SIZE);
               }
               else
               {
                  active_log_buffer->nwritten_chars += nwritten;
               }
            }
         }
         else
         {
            active_log_buffer->nwritten_chars += nwritten;
         }
      }
   }
   g_mutex_unlock(&g_roplogger_mutex);

   if (object && obj)
      g_free(obj);
}

gboolean rop_logger_register(RemoteOffloadPipelineLogger *logger,
                             GenericDataExchanger *exchanger,
                             RemoteOffloadLogMode mode,
                             const char *gst_debug)
{
   if( !ROP_IS_LOGGER(logger) )
      return FALSE;

   if( !DATAEXCHANGER_IS_GENERIC(exchanger) )
      return FALSE;

   gboolean ret = TRUE;

   g_mutex_lock(&g_roplogger_mutex);
   if( gst_debug )
   {
      if( logger->gst_debug_now )
            g_free(logger->gst_debug_now);
      logger->gst_debug_now = g_strdup(gst_debug);
   }
   g_mutex_unlock(&g_roplogger_mutex);

   //set gst_debug from user... it will get reset back to
   // default during rop_logger finalize.
   if( logger->gst_debug_now )
      gst_debug_set_threshold_from_string(logger->gst_debug_now,
                                          FALSE);

   g_mutex_lock(&logger->ropinstance_entries_mutex);
   g_mutex_lock(&g_roplogger_mutex);
   if( !logger->active_log_buffer )
   {
      logger->active_log_buffer =
            acquire_logging_buffer(logger);
   }

   if( logger->active_log_buffer )
   {
      ROPInstanceEntry *instance_entry =
         (ROPInstanceEntry *)g_malloc(sizeof(ROPInstanceEntry));
      instance_entry->exchanger = exchanger;
      instance_entry->active_start = logger->active_log_buffer->nwritten_chars;
      instance_entry->first_active_buffer = logger->active_log_buffer;

      logger->ropinstance_entries_list =
            g_list_prepend(logger->ropinstance_entries_list,
                           instance_entry);
   }
   else
   {
      ret = FALSE;
   }
   g_mutex_unlock(&g_roplogger_mutex);
   g_mutex_unlock(&logger->ropinstance_entries_mutex);

   return ret;
}

gboolean rop_logger_unregister(RemoteOffloadPipelineLogger *logger,
                               GenericDataExchanger *exchanger)
{
   if( !ROP_IS_LOGGER(logger) )
      return FALSE;

   if( !DATAEXCHANGER_IS_GENERIC(exchanger) )
      return FALSE;

   gboolean ret = FALSE;
   ROPInstanceEntry *tofree = NULL;

   //first, we want to flush the active
   g_mutex_lock(&g_roplogger_mutex);
   gboolean wait_for_flush = FALSE;
   if( logger->active_log_buffer )
   {
     wait_for_flush = TRUE;

     //push the current active log buffer into the flush queue
     g_mutex_lock(&logger->activetransferpoolmutex);
     g_mutex_lock(&logger->flushidlemutex);
     g_queue_push_head(logger->loggingBufferActiveTransferQueue, logger->active_log_buffer);
     g_cond_broadcast(&logger->activetransferpoolcond);
     g_mutex_unlock(&logger->activetransferpoolmutex);
     logger->active_log_buffer = NULL;
   }
   g_mutex_unlock(&g_roplogger_mutex);

   if( wait_for_flush )
   {
      g_cond_wait (&logger->flushidlecond, &logger->flushidlemutex);
      g_mutex_unlock(&logger->flushidlemutex);
   }

   g_mutex_lock(&logger->ropinstance_entries_mutex);
   GList *listentry;
   for( listentry = logger->ropinstance_entries_list;
        listentry != NULL;
        listentry = listentry->next)
   {
      ROPInstanceEntry *listexchanger
          = (ROPInstanceEntry *)listentry->data;
      if( listexchanger->exchanger == exchanger )
      {
         tofree = listexchanger;
         logger->ropinstance_entries_list =
              g_list_delete_link(logger->ropinstance_entries_list,
                                 listentry);
         break;
      }
   }
   g_mutex_unlock(&logger->ropinstance_entries_mutex);

   if( tofree )
      g_free(tofree);

   return ret;
}


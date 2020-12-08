/*
 *  appsinkcomparer.c - AppSinkComparer object

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
 *
 *  This object is used for GStreamer Unit testing. A test can
 *   "attach" two (or more) appsink's across one (or multiple)
 *   pipelines. This object will then compare the GstBuffer's
 *   (and optionally queries, events, etc.)received by the multiple
 *   appsink's, and can emit various signals if/when things don't match.
 *
 *   It was primarily designed to test the GStreamer Remote Offload
 *   framework. The idea here is to run "native" pipelines concurrently
 *   with remote-offloaded pipelines, and ensure that the results match.
 */
#include <gst/app/gstappsink.h>
#include <gst/video/gstvideometa.h>
#ifndef NO_SAFESTR
  #include <safe_mem_lib.h>
#else
  #include <string.h>
#endif
#include "appsinkcomparer.h"
#include "orderedghashtable.h"

enum
{
  /* signals */
  SIGNAL_SAMPLEMISMATCH,

  LAST_SIGNAL
};

static guint appsink_comparer_signals[LAST_SIGNAL] = { 0 };

typedef struct
{
   GMutex entrymutex;
   GQueue *sample_queue;
}AppSinkEntry;

static AppSinkEntry* new_appsink_entry()
{
   AppSinkEntry *entry = g_malloc(sizeof(AppSinkEntry));

   g_mutex_init(&entry->entrymutex);
   entry->sample_queue = g_queue_new();

   return entry;
}

static void destroy_appsink_entry(AppSinkEntry *entry)
{
   if( !entry ) return;

   while( !g_queue_is_empty(entry->sample_queue) )
   {
      gst_sample_unref( (GstSample *)g_queue_pop_head(entry->sample_queue));
   }

   g_queue_free(entry->sample_queue);

   g_mutex_clear(&entry->entrymutex);

   g_free(entry);
}

typedef struct
{
   GMutex comparermutex;
   OrderedGHashTable *appsink_to_entry_hash;

   gboolean bthreadrun;
   GThread *comparer_thread;
   GCond  comparerthrcond;

   GCond  flushcond;

}AppSinkComparerPrivate;

struct _AppSinkComparer
{
  GObject parent_instance;

  /* Other members, including private data. */
  AppSinkComparerPrivate priv;
};

GST_DEBUG_CATEGORY_STATIC (appsink_comparer_debug);
#define GST_CAT_DEFAULT appsink_comparer_debug

G_DEFINE_TYPE_WITH_CODE(AppSinkComparer, appsink_comparer, G_TYPE_OBJECT,
GST_DEBUG_CATEGORY_INIT (appsink_comparer_debug, "appsinkcomparer", 0,
  "debug category for AppSinkComparer"))

static gpointer appsink_comparer_thread(AppSinkComparer *comparer);

static void
appsink_comparer_constructed (GObject *gobject)
{
  AppSinkComparer *self = APPSINK_COMPARER(gobject);

  //start our comparer thread
  self->priv.bthreadrun = TRUE;

  self->priv.comparer_thread =
        g_thread_new ("appsinkcomparer", (GThreadFunc) appsink_comparer_thread, self);


  G_OBJECT_CLASS (appsink_comparer_parent_class)->constructed (gobject);
}

static void
appsink_comparer_finalize (GObject *gobject)
{
  AppSinkComparer *self = APPSINK_COMPARER(gobject);

  if( self->priv.comparer_thread )
  {
     g_mutex_lock (&self->priv.comparermutex);
     self->priv.bthreadrun = FALSE;
     g_cond_broadcast (&self->priv.comparerthrcond);
     g_mutex_unlock (&self->priv.comparermutex);
     g_thread_join (self->priv.comparer_thread);
  }

  if( self->priv.appsink_to_entry_hash )
     g_object_unref(self->priv.appsink_to_entry_hash);

  g_cond_clear(&self->priv.comparerthrcond);
  g_cond_clear(&self->priv.flushcond);
  g_mutex_clear(&self->priv.comparermutex);

  G_OBJECT_CLASS (appsink_comparer_parent_class)->finalize (gobject);
}

static void
appsink_comparer_class_init (AppSinkComparerClass *klass)
{
   GObjectClass *object_class = G_OBJECT_CLASS (klass);

   appsink_comparer_signals[SIGNAL_SAMPLEMISMATCH] =
      g_signal_newv ("mismatch", G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
      NULL, /* closure */
      NULL, /* accumulator */
      NULL, /*accumulator data */
      NULL, /* C marshaller */
      G_TYPE_NONE, /* return type */
      0, /* n_params */
      NULL /* param types */);


   object_class->constructed = appsink_comparer_constructed;
   object_class->finalize = appsink_comparer_finalize;
}


static gboolean does_all_registered_have_entry(AppSinkComparer *comparer)
{
   OrderedGHashTableIter iter;
   gpointer key, value;
   ordered_g_hash_table_iter_init (&iter, comparer->priv.appsink_to_entry_hash);
   while (ordered_g_hash_table_iter_next (&iter, &key, &value))
   {
     AppSinkEntry *entry = (AppSinkEntry *)value;

     if( g_queue_is_empty(entry->sample_queue) )
     {
        return FALSE;
     }
   }

   return TRUE;
}

static gboolean does_any_registered_have_entry(AppSinkComparer *comparer)
{
   OrderedGHashTableIter iter;
   gpointer key, value;
   ordered_g_hash_table_iter_init (&iter, comparer->priv.appsink_to_entry_hash);
   while (ordered_g_hash_table_iter_next (&iter, &key, &value))
   {
     AppSinkEntry *entry = (AppSinkEntry *)value;

     if( !g_queue_is_empty(entry->sample_queue) )
     {
        return TRUE;
     }
   }

   return FALSE;
}

static void SampleArrayEntryClear(gpointer data)
{

   GstSample *sample = *((GstSample **)data);

   gst_sample_unref(sample);
}

GArray *get_sample_from_all_entries(AppSinkComparer *comparer)
{
   GArray *sample_array = g_array_new (FALSE,
                          FALSE,
                          sizeof(GstSample *));

   g_array_set_clear_func (sample_array,
                           SampleArrayEntryClear);

   OrderedGHashTableIter iter;
   gpointer key, value;
   ordered_g_hash_table_iter_init (&iter, comparer->priv.appsink_to_entry_hash);
   while (ordered_g_hash_table_iter_next (&iter, &key, &value))
   {
     AppSinkEntry *entry = (AppSinkEntry *)value;

     GstSample *sample = g_queue_pop_head(entry->sample_queue);
     g_array_append_val(sample_array, sample);
   }


   return sample_array;
}

//The following "compare frames" methods were developed based on gst_video_frame_copy
// methods in gst-plugins-base/gst-libs/gst/video/video-frame.c. Places that used
// 'memcpy' simply use 'memcmp', and return FALSE when the result of memcmp != 0.
static gboolean
compare_frame_plane (const GstVideoFrame * f0, const GstVideoFrame * f1,
    guint plane)
{
  const GstVideoInfo *f0info;
  const GstVideoInfo *f1info;
  const GstVideoFormatInfo *finfo;
  guint8 *sp_f0, *sp_f1;
  guint w, h;
  gint ss, ds;

  g_return_val_if_fail (f0 != NULL, FALSE);
  g_return_val_if_fail (f1 != NULL, FALSE);

  f0info = &f0->info;
  f1info = &f1->info;

  g_return_val_if_fail (f0info->finfo->format == f1info->finfo->format, FALSE);

  finfo = f0info->finfo;

  g_return_val_if_fail (f0info->width == f1info->width
      && f0info->height == f1info->height, FALSE);
  g_return_val_if_fail (finfo->n_planes > plane, FALSE);

  sp_f0 = f0->data[plane];
  sp_f1 = f1->data[plane];

  if (GST_VIDEO_FORMAT_INFO_HAS_PALETTE (finfo) && plane == 1) {

#ifndef NO_SAFESTR
     int memcmpres = 0;

     if( memcmp_s(sp_f0, 256 * 4, sp_f1, 256 * 4, &memcmpres) != EOK )
     {
        GST_ERROR("memcmp_s != EOK");
        return FALSE;
     }
#else
     /* compare the palette and we're done */
     int memcmpres = memcmp(sp_f0, sp_f1, 256 * 4);
#endif

     return (memcmpres == 0) ? TRUE : FALSE;
  }

  int memcmpres = 0;

  /* FIXME: assumes subsampling of component N is the same as plane N, which is
   * currently true for all formats we have but it might not be in the future. */
  w = GST_VIDEO_FRAME_COMP_WIDTH (f0,
      plane) * GST_VIDEO_FRAME_COMP_PSTRIDE (f0, plane);
  /* FIXME: workaround for complex formats like v210, UYVP and IYU1 that have
   * pstride == 0 */
  if (w == 0)
    w = MIN (GST_VIDEO_INFO_PLANE_STRIDE (f0info, plane),
        GST_VIDEO_INFO_PLANE_STRIDE (f1info, plane));

  h = GST_VIDEO_FRAME_COMP_HEIGHT (f0, plane);

  ss = GST_VIDEO_INFO_PLANE_STRIDE (f0info, plane);
  ds = GST_VIDEO_INFO_PLANE_STRIDE (f1info, plane);

  if (GST_VIDEO_FORMAT_INFO_IS_TILED (finfo)) {
    gint tile_size;
    gint sx_tiles, sy_tiles, dx_tiles, dy_tiles;
    guint i, j, ws, hs, ts;
    GstVideoTileMode mode;

    ws = GST_VIDEO_FORMAT_INFO_TILE_WS (finfo);
    hs = GST_VIDEO_FORMAT_INFO_TILE_HS (finfo);
    ts = ws + hs;

    tile_size = 1 << ts;

    mode = GST_VIDEO_FORMAT_INFO_TILE_MODE (finfo);

    sx_tiles = GST_VIDEO_TILE_X_TILES (ss);
    sy_tiles = GST_VIDEO_TILE_Y_TILES (ss);

    dx_tiles = GST_VIDEO_TILE_X_TILES (ds);
    dy_tiles = GST_VIDEO_TILE_Y_TILES (ds);

    /* this is the amount of tiles to compare */
    w = ((w - 1) >> ws) + 1;
    h = ((h - 1) >> hs) + 1;

    /* FIXME can possibly do better when no retiling is needed, it depends on
     * the stride and the tile_size */
    for (j = 0; j < h; j++) {
      for (i = 0; i < w; i++) {
        guint si, di;

        si = gst_video_tile_get_index (mode, i, j, sx_tiles, sy_tiles);
        di = gst_video_tile_get_index (mode, i, j, dx_tiles, dy_tiles);


#ifndef NO_SAFESTR
        if( memcmp_s(sp_f1 + (di << ts), tile_size,
                     sp_f0 + (si << ts), tile_size,
                     &memcmpres) != EOK )
        {
           GST_ERROR("memcmp_s != EOK");
           return FALSE;
        }
#else
        memcmpres = memcmp(sp_f1 + (di << ts), sp_f0 + (si << ts), tile_size);
#endif
        if( memcmpres != 0) break;
      }

      if( memcmpres != 0) break;
    }
  } else {
    guint j;

    for (j = 0; j < h; j++) {
#ifndef NO_SAFESTR
       if( memcmp_s(sp_f1, w,
                    sp_f0, w,
                    &memcmpres) != EOK )
      {
         g_print("memcmp_s != EOK");
         return FALSE;
      }
#else
       memcmpres = memcmp (sp_f1, sp_f0, w);
#endif
       if( memcmpres != 0) break;

       sp_f1 += ds;
       sp_f0 += ss;
    }
  }

  return (memcmpres == 0) ? TRUE : FALSE;
}

//compare two frames. Return TRUE if their valid content matches, otherwise FALSE.
static gboolean
compare_frames (const GstVideoFrame * f1, const GstVideoFrame * f2)
{
  guint i, n_planes;
  const GstVideoInfo *f1info;
  const GstVideoInfo *f2info;

  g_return_val_if_fail (f1 != NULL, FALSE);
  g_return_val_if_fail (f2 != NULL, FALSE);

  f1info = &f1->info;
  f2info = &f2->info;

  g_return_val_if_fail (f1info->finfo->format == f2info->finfo->format, FALSE);
  g_return_val_if_fail (f1info->width == f2info->width
      && f1info->height == f2info->height, FALSE);

  n_planes = f1info->finfo->n_planes;

  for (i = 0; i < n_planes; i++)
  {
    if( !compare_frame_plane(f1, f2, i) )
       return FALSE;
  }

  return TRUE;
}

static gboolean compare_samples(AppSinkComparer *comparer,
                                GArray *sample_array)
{
   if( sample_array->len < 2 )
   {
      GST_ERROR("compare_samples called with sample_array with len < 2");
      return FALSE;
   }

   //Set the "golden" sample to the first entry (index 0). This is the
   // one that will be compared with index 1, index 2, ..., index (len-1)
   GstSample **samples = (GstSample **)sample_array->data;
   GstSample *golden_sample = samples[0];

   GstBuffer *golden_buf = gst_sample_get_buffer(golden_sample);

   GstVideoMeta *golden_video_meta = gst_buffer_get_video_meta(golden_buf);

   gboolean compareok = TRUE;

   //if there is video meta defined, we can use a better comparison method
   // by converting raw GstBuffer's to GstVideoFrames, and comparing those.
   //This is better than comparing the "raw" GstMemory's as there may
   // very well be uninitialized bytes within those GstMemory, from
   // alignment/offset padding. When we compare the video frames, we
   // can be smarter about only comparing "valid" bytes (skipping over
   // various padding bytes)
   if( golden_video_meta )
   {
      GstVideoInfo *golden_info = gst_video_info_new();
      gst_video_info_set_format(golden_info, golden_video_meta->format,
                                golden_video_meta->width, golden_video_meta->height);
      GstVideoFrame golden_frame;
      if( gst_video_frame_map(&golden_frame, golden_info, golden_buf,GST_MAP_READ) )
      {
         for( guint i = 1; i < sample_array->len; i++ )
         {
            GstSample *sample_to_compare = samples[1];


            if( sample_to_compare )
            {
               GstBuffer *buffer_to_compare = gst_sample_get_buffer(sample_to_compare);

               if( buffer_to_compare )
               {
                  GstVideoMeta *video_meta = gst_buffer_get_video_meta(buffer_to_compare);

                  if( video_meta )
                  {
                     GstVideoInfo *vid_info = gst_video_info_new();

                     gst_video_info_set_format(vid_info, video_meta->format,
                                               video_meta->width, video_meta->height);

                     GstVideoFrame video_frame;
                     if( gst_video_frame_map(&video_frame,
                                             vid_info,
                                             buffer_to_compare, GST_MAP_READ) )
                     {
                        if( !compare_frames(&golden_frame, &video_frame) )
                        {
                           GST_ERROR("!compare_frames failed");
                           compareok = FALSE;
                        }
                        gst_video_frame_unmap(&video_frame);
                     }
                     else
                     {
                        GST_ERROR("!gst_video_frame_map");
                        compareok = FALSE;
                     }

                     gst_video_info_free(vid_info);
                  }
                  else
                  {
                     GST_ERROR("!video_meta");
                     compareok = FALSE;
                  }
               }
               else
               {
                  GST_ERROR("!buffer_to_compare");
                  compareok = FALSE;
               }
            }
            else
            {
               GST_ERROR("!sample_to_compare");
               compareok = FALSE;
            }

            if( !compareok )
            {
               break;
            }
         }

         gst_video_frame_unmap(&golden_frame);

      }
      else
      {
         GST_ERROR("!gst_video_frame_map");
         compareok = FALSE;
      }

      gst_video_info_free(golden_info);
   }
   else
   {
      //No video meta defined... we need to resort to comparing GstMemory attached to raw buffer
      GstMapInfo golden_info;
      if( golden_buf )
      {
         if( !gst_buffer_map (golden_buf, &golden_info, GST_MAP_READ) )
         {
            GST_ERROR("Error mapping buffer for reading");
            //TODO: This probably shouldn't indicate that the buffer doesn't
            // "match"... we just can't read it. So what do we do? How do you
            // properly compare a buffer that you can't read?
            return FALSE;
         }
      }

      for( guint i = 1; i < sample_array->len; i++ )
      {
         GstSample *sample_to_compare = samples[1];
         GstBuffer *buffer_to_compare = gst_sample_get_buffer(sample_to_compare);

         if( !buffer_to_compare )
         {
            if( !golden_buf )
             continue;

            GST_ERROR("gst_sample_get_buffer(samples[%u]) returned NULL", i);
            compareok = FALSE;
            break;
         }

         if( !golden_buf )
         {
            GST_ERROR("gst_sample_get_buffer(samples[%u]) returned NULL", 0);
            compareok = FALSE;
            break;
         }

         GstMapInfo info;
         if( !gst_buffer_map (buffer_to_compare, &info, GST_MAP_READ) )
         {
            GST_ERROR("Error mapping buffer for reading");
            compareok = FALSE;
            break;
         }

         if( info.size == golden_info.size )
         {
            int memcmpres = 0;

#ifndef NO_SAFESTR
            if( memcmp_s(info.data, info.size,
                         golden_info.data, golden_info.size,
                         &memcmpres) != EOK )
            {
               GST_ERROR("memcmp_s != EOK");
               compareok = FALSE;
            }
#else
            memcmpres = memcmp (info.data, golden_info.data, info.size);
#endif
            if (memcmpres != 0)
            {
               GST_ERROR("Buffer data doesn't match for index %u", i);
               compareok = FALSE;
            }
         }
         else
         {
            GST_ERROR("info.size for index %u doesn't match", i);
            compareok = FALSE;
         }

         gst_buffer_unmap (buffer_to_compare, &info);

         if( !compareok )
            break;

         //TODO, we could optionally compare other things here too...
         // timestamps, meta data, etc.
      }

      if( golden_buf )
      {
         gst_buffer_unmap (golden_buf, &golden_info);
      }
   }

   return compareok;
}

static gpointer appsink_comparer_thread(AppSinkComparer *comparer)
{
   g_mutex_lock (&comparer->priv.comparermutex);
   while( comparer->priv.bthreadrun )
   {
      //if we have at least two appsink's registered,
      // AND all of those registered appsink's have at least one
      // entry in the captured queue
      if( ordered_g_hash_table_size(comparer->priv.appsink_to_entry_hash) >= 2
            && does_all_registered_have_entry(comparer) )
      {
         GArray *sample_array = get_sample_from_all_entries(comparer);

         g_mutex_unlock (&comparer->priv.comparermutex);

         //perform the comparison of the samples
         if( !compare_samples(comparer,
                              sample_array) )
         {
            //emit the "mismatch" signal
             g_signal_emit (comparer,
                            appsink_comparer_signals[SIGNAL_SAMPLEMISMATCH],
                            0);
         }

         g_array_unref(sample_array);

         g_mutex_lock (&comparer->priv.comparermutex);
      }
      else
      {
         g_cond_broadcast(&comparer->priv.flushcond);

         //and so we wait..
         g_cond_wait (&comparer->priv.comparerthrcond,
                      &comparer->priv.comparermutex);
      }

   }
   g_mutex_unlock (&comparer->priv.comparermutex);

   return NULL;
}

gboolean appsink_comparer_flush(AppSinkComparer *comparer)
{
   if( !APPSINK_IS_COMPARER(comparer) )
   {
      GST_ERROR("Not a valid comparer..");
      return FALSE;
   }

   g_mutex_lock (&comparer->priv.comparermutex);
   g_cond_broadcast(&comparer->priv.comparerthrcond);
   g_cond_wait (&comparer->priv.flushcond,
                &comparer->priv.comparermutex);
   gboolean ret = !does_any_registered_have_entry(comparer);
   g_mutex_unlock (&comparer->priv.comparermutex);

   return ret;
}

static void appsink_comparer_handle_new_sample(AppSinkComparer *comparer,
                                               GstAppSink *appsink,
                                               GstSample *sample)
{
   g_mutex_lock(&comparer->priv.comparermutex);
   AppSinkEntry *entry =
         ordered_g_hash_table_lookup(comparer->priv.appsink_to_entry_hash,
                             appsink);
   g_mutex_unlock(&comparer->priv.comparermutex);

   if( entry )
   {
      //TODO: Potentially add a property to enable adding a
      // copied sample to the queue instead, so that we can
      // unref the sample immediately.
      //In some cases, keeping the sample around longer than it would
      // "normally" be around may impact the behavior of the memory
      // pools being used within the pipeline.
      g_mutex_lock(&entry->entrymutex);
      g_queue_push_tail(entry->sample_queue,
                        sample);
      g_mutex_unlock(&entry->entrymutex);

      //now, wake up our comparer thread
      g_mutex_lock(&comparer->priv.comparermutex);
      g_cond_broadcast(&comparer->priv.comparerthrcond);
      g_mutex_unlock(&comparer->priv.comparermutex);
   }
}

static GstFlowReturn appsink_comparer_new_preroll(GstAppSink *appsink, gpointer user_data)
{
   GstSample *sample = NULL;
   g_signal_emit_by_name (appsink, "pull-preroll", &sample);

   if( sample )
   {
      appsink_comparer_handle_new_sample((AppSinkComparer *)user_data,
                                         appsink,
                                         sample);

      return GST_FLOW_OK;
   }


   return GST_FLOW_ERROR;
}

static GstFlowReturn appsink_comparer_new_sample(GstAppSink *appsink, gpointer user_data)
{
  GstSample *sample = NULL;
  g_signal_emit_by_name (appsink, "pull-sample", &sample);

  if( sample )
  {
     appsink_comparer_handle_new_sample((AppSinkComparer *)user_data,
                                        appsink,
                                        sample);

     return GST_FLOW_OK;
  }

  return GST_FLOW_ERROR;
}


gboolean appsink_comparer_register(AppSinkComparer *comparer,
                                   GstElement *appsink)
{
   if( !APPSINK_IS_COMPARER(comparer) )
   {
      GST_ERROR("Not a valid comparer..");
      return FALSE;
   }

   if( !GST_IS_APP_SINK(appsink) )
   {
      GST_ERROR("Not an appsink..");
      return FALSE;
   }

   g_mutex_lock(&comparer->priv.comparermutex);
   if( !ordered_g_hash_table_insert(comparer->priv.appsink_to_entry_hash,
                            gst_object_ref(appsink),
                            new_appsink_entry() ) )
   {
      GST_ERROR("appsink already registered...");
      g_mutex_unlock(&comparer->priv.comparermutex);
      return FALSE;
   }

   g_mutex_unlock(&comparer->priv.comparermutex);

   //connect the appsink signals
   g_object_set (appsink, "emit-signals", TRUE, NULL);
   g_signal_connect (appsink, "new-sample", G_CALLBACK (appsink_comparer_new_sample), comparer);
   g_signal_connect (appsink, "new-preroll", G_CALLBACK (appsink_comparer_new_preroll), comparer);

   return TRUE;
}

static void AppSinkToEntryKeyDestroy(gpointer data)
{
   GstElement *appsink = (GstElement *)data;
   gst_object_unref(appsink);
}

static void AppSinkToEntryValueDestroy(gpointer data)
{
   AppSinkEntry *entry = (AppSinkEntry *)data;

   destroy_appsink_entry(entry);
}

static void
appsink_comparer_init (AppSinkComparer *self)
{
  self->priv.comparer_thread = NULL;
  g_mutex_init(&self->priv.comparermutex);
  g_cond_init(&self->priv.comparerthrcond);
  g_cond_init(&self->priv.flushcond);
  self->priv.appsink_to_entry_hash = ordered_g_hash_table_new(g_direct_hash,
                                                              g_direct_equal,
                                                              AppSinkToEntryKeyDestroy,
                                                              AppSinkToEntryValueDestroy);

}

AppSinkComparer *appsink_comparer_new()
{
   return g_object_new(APPSINKCOMPARER_TYPE, NULL);
}

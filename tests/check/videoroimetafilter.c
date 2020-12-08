/*
 *  videoroimetafilter.c - Set of tests for videoroimetafilter element
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
 *
 *  These are a set of "basic" tests, meaning that they are mostly "dummy"
 *  pipelines intended to test basic functionality of sublaunch &
 *  remoteoffloadbin elements.
 *  If adding new tests, only use elements from gst-plugins-base or
 *  or gst-plugins-good.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifdef HAVE_VALGRIND
# include <valgrind/valgrind.h>
#endif

#include <gst/check/gstcheck.h>
#include <gst/check/gstconsistencychecker.h>
#include <gst/video/gstvideometa.h>
#include "robtestutils.h"

static void GenerateAnimalStructs(GstStructure **dog,
                                  GstStructure **cat,
                                  GstStructure **grizzly,
                                  GstStructure **koala)
{
   if( dog )
   {
      *dog = gst_structure_new("dog",
                              "color", G_TYPE_STRING, "black",
                              "trick", G_TYPE_STRING, "roll-over",
                              "sound", G_TYPE_STRING, "woof", NULL);

      fail_unless(*dog != NULL);
   }

   if( cat )
   {
      *cat = gst_structure_new("cat",
                               "color", G_TYPE_STRING, "orange",
                               "trick", G_TYPE_STRING, "sleep",
                               "sound", G_TYPE_STRING, "meow",
                               "num_lives", G_TYPE_DOUBLE, 9.0, NULL);
      fail_unless(*cat != NULL);
   }

   if( grizzly )
   {
      *grizzly = gst_structure_new("grizzly_bear",
                                   "color", G_TYPE_STRING, "brown",
                                   "trick", G_TYPE_STRING, "maul",
                                   "sound", G_TYPE_STRING, "grrr",
                                   "does_hibernate", G_TYPE_BOOLEAN, TRUE, NULL);
      fail_unless(*grizzly != NULL);
   }

   if( koala )
   {
      *koala = gst_structure_new("koala_bear",
                                 "color", G_TYPE_STRING, "gray",
                                 "location", G_TYPE_STRING, "australia", NULL);
      fail_unless(*koala != NULL);
   }
}

static GstPadProbeReturn AddVideoROIMetaProbe(GstPad *pad,
                                              GstPadProbeInfo *info,
                                              gpointer user_data)
{
   GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
   if( buf )
   {
      guint *probe_count = (guint *)user_data;
      (*probe_count)++;

      for( guint i = 0; i < *probe_count; i++ )
      {
         GstVideoRegionOfInterestMeta *meta =
               gst_buffer_add_video_region_of_interest_meta(buf,
                                                            "test_roi",
                                                            0,
                                                            50,
                                                            100,
                                                            200);
         fail_unless(meta != NULL);

         GstStructure *dog,*cat,*grizzly,*koala;
         GenerateAnimalStructs(&dog, &cat, &grizzly, &koala);
         gst_video_region_of_interest_meta_add_param(meta, dog);
         gst_video_region_of_interest_meta_add_param(meta, cat);
         gst_video_region_of_interest_meta_add_param(meta, grizzly);
         gst_video_region_of_interest_meta_add_param(meta, koala);
      }
   }

   return GST_PAD_PROBE_PASS;
}

//iterate through each GstVideoRoiMeta attached to this buffer,
// and check that the GstStructure params list equals the
// passed in check_list.
static GstPadProbeReturn CheckProbe(GstPad *pad,
                                    GstPadProbeInfo *info,
                                    gpointer user_data)
{
   GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
   if( buf )
   {

      GstVideoRegionOfInterestMeta *meta = NULL;
      gpointer state = NULL;
      while( (meta = (GstVideoRegionOfInterestMeta *)
          gst_buffer_iterate_meta_filtered(buf,
                                           &state,
                                           gst_video_region_of_interest_meta_api_get_type())) )
      {
         GList *check_list = (GList *)user_data;
         GList *params_list = meta->params;
         if( check_list || params_list )
         {
            fail_unless(g_list_length(check_list)==g_list_length(params_list));
            while( params_list )
            {
               GstStructure *param = (GstStructure *)params_list->data;
               GstStructure *check = (GstStructure *)check_list->data;
#if 0
               g_print("param: %s\n", gst_structure_to_string(param));
               g_print("check: %s\n", gst_structure_to_string(check));
#endif
               fail_unless(gst_structure_is_equal(param, check));

               params_list = params_list->next;
               check_list = check_list->next;
            }
         }
      }
   }

   return GST_PAD_PROBE_PASS;
}

typedef struct
{
   GMainLoop *loop;
   gboolean bokay;
}MetaFilterPipelineTestEntry;

static gboolean
bus_call (GstBus     *bus,
          GstMessage *msg,
          gpointer    data)
{
  MetaFilterPipelineTestEntry *entry =
        (MetaFilterPipelineTestEntry*)data;

  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
      g_main_loop_quit (entry->loop);
      break;

    case GST_MESSAGE_ERROR: {
      gchar  *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);

      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);

      entry->bokay = FALSE;
      g_main_loop_quit (entry->loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static gboolean run_pipeline(gchar *preserve_filter,
                             gchar *remove_filter,
                             GList *check_list)
{
   MetaFilterPipelineTestEntry entry;

   GstBus *bus;
   guint bus_watch_id;
   GMainLoop *loop;

   loop = g_main_loop_new (NULL, FALSE);

   entry.loop = loop;
   entry.bokay = TRUE;

   GstElement *videotestsrc = gst_element_factory_make("videotestsrc", NULL);
   g_object_set (G_OBJECT (videotestsrc), "num-buffers", 10, NULL);

   GstElement *videoroimetafilter = gst_element_factory_make("videoroimetafilter", NULL);
   g_object_set (G_OBJECT (videoroimetafilter), "preserve", preserve_filter, NULL);
   g_object_set (G_OBJECT (videoroimetafilter), "remove", remove_filter, NULL);

   GstPad *add_video_meta_pad = gst_element_get_static_pad(videoroimetafilter, "sink");
   guint probe_count = 0;
   gst_pad_add_probe(add_video_meta_pad, (GstPadProbeType)(GST_PAD_PROBE_TYPE_BUFFER |
                                         GST_PAD_PROBE_TYPE_BLOCKING), (GstPadProbeCallback)AddVideoROIMetaProbe, &probe_count, NULL);
   gst_object_unref(add_video_meta_pad);

   GstElement *fakesink = gst_element_factory_make("fakesink", NULL);

   GstPad *check_video_meta_pad = gst_element_get_static_pad(fakesink, "sink");
   gst_pad_add_probe(check_video_meta_pad, (GstPadProbeType)(GST_PAD_PROBE_TYPE_BUFFER |
                                           GST_PAD_PROBE_TYPE_BLOCKING), (GstPadProbeCallback)CheckProbe, check_list, NULL);
   gst_object_unref(check_video_meta_pad);


   GstElement *pipeline = gst_pipeline_new("my_pipeline");
   bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
   bus_watch_id = gst_bus_add_watch (bus, bus_call, &entry);
   gst_object_unref (bus);

   gst_bin_add_many(GST_BIN(pipeline),
                    videotestsrc,
                    videoroimetafilter,
                    fakesink,
                    NULL);

   gst_element_link_many(videotestsrc,
                         videoroimetafilter,
                         fakesink,
                         NULL);

   gst_element_set_state (pipeline, GST_STATE_PLAYING);

   g_main_loop_run (loop);

   gst_element_set_state (pipeline, GST_STATE_NULL);

   gst_object_unref (GST_OBJECT (pipeline));
   g_source_remove (bus_watch_id);
   g_main_loop_unref (loop);

   return entry.bokay;
}

static GList *structs_to_checklist(GstStructure *dog,
                                   GstStructure *cat,
                                   GstStructure *grizzly,
                                   GstStructure *koala )
{
  GList *list = NULL;
  if( dog )
    list = g_list_append(list, dog);
  if( cat )
    list = g_list_append(list, cat);
  if( grizzly )
    list = g_list_append(list, grizzly);
  if( koala )
    list = g_list_append(list, koala);

  return list;
}

static void destroy_checklist(GList *list )
{
  if( list )
  {
     for(GList *l = list; l; l=l->next)
     {
        gst_structure_free( (GstStructure*)l->data);
     }
     g_list_free(list);
  }
}

static gboolean remove_if_not_field(GQuark   field_id,
                                    GValue * value,
                                    gpointer user_data)
{
  gchar *fieldname_tocheck = (gchar *)user_data;
  const gchar *fieldname = g_quark_to_string(field_id);

  return !g_strcmp0(fieldname_tocheck, fieldname);
}



static void intersect_field(GstStructure *s,
                            gchar *fieldname)
{
   gst_structure_filter_and_map_in_place(s,
                                         remove_if_not_field,
                                         fieldname);
}

static gboolean remove_if_field(GQuark   field_id,
                                    GValue * value,
                                    gpointer user_data)
{
  gchar *fieldname_tocheck = (gchar *)user_data;
  const gchar *fieldname = g_quark_to_string(field_id);

  return g_strcmp0(fieldname_tocheck, fieldname);
}

static void remove_field(GstStructure *s,
                         gchar *fieldname)
{
   gst_structure_filter_and_map_in_place(s,
                                         remove_if_field,
                                         fieldname);
}

//ensure no modification when neither preserve / remove filters are set
GST_START_TEST(videoroimetafilter_preserve0)
{
   GstStructure *dog,*cat,*grizzly,*koala;
   GenerateAnimalStructs(&dog, &cat, &grizzly, &koala);
   GList *check_list = structs_to_checklist(dog,cat,grizzly,koala);
   fail_unless(run_pipeline(NULL, NULL, check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//preserve any structures named "dog"
GST_START_TEST(videoroimetafilter_preserve1)
{
   GstStructure *dog;
   GenerateAnimalStructs(&dog, NULL, NULL, NULL);

   GList *check_list = structs_to_checklist(dog,NULL,NULL,NULL);
   fail_unless(run_pipeline("dog", NULL, check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//preserve any structures named "dog" or "cat"
GST_START_TEST(videoroimetafilter_preserve2)
{
   GstStructure *dog,*cat;
   GenerateAnimalStructs(&dog, &cat, NULL, NULL);
   GList *check_list = structs_to_checklist(dog,cat,NULL,NULL);
   fail_unless(run_pipeline("dog|cat", NULL, check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//preserve any structures that have "bear" in the name
GST_START_TEST(videoroimetafilter_preserve3)
{
   GstStructure *grizzly,*koala;
   GenerateAnimalStructs(NULL, NULL, &grizzly, &koala);
   GList *check_list = structs_to_checklist(NULL,NULL,grizzly,koala);
   fail_unless(run_pipeline("bear", NULL, check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//preserve any structures that are named exactly "bear" (should clear all)
GST_START_TEST(videoroimetafilter_preserve4)
{
   GList *check_list = structs_to_checklist(NULL,NULL,NULL,NULL);
   fail_unless(run_pipeline("^bear$", NULL, check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//preserve the "color" field from all structures
GST_START_TEST(videoroimetafilter_preserve5)
{
   GstStructure *dog,*cat,*grizzly,*koala;
   GenerateAnimalStructs(&dog, &cat, &grizzly, &koala);
   intersect_field(dog, "color");
   intersect_field(cat, "color");
   intersect_field(grizzly, "color");
   intersect_field(koala, "color");
   GList *check_list = structs_to_checklist(dog,cat,grizzly,koala);
   fail_unless(run_pipeline(";color", NULL, check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//preserve the "trick" field from all structures.
// koala doesn't have a "trick" field, so this structure should
//  get completely removed
GST_START_TEST(videoroimetafilter_preserve6)
{
   GstStructure *dog,*cat,*grizzly;
   GenerateAnimalStructs(&dog, &cat, &grizzly, NULL);
   intersect_field(dog, "trick");
   intersect_field(cat, "trick");
   intersect_field(grizzly, "trick");
   GList *check_list = structs_to_checklist(dog,cat,grizzly,NULL);
   fail_unless(run_pipeline(";trick", NULL, check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//preserve the "location" field from all structures.
// dog,cat,grizzly doesn't have a "location" field, so these structures should
//  get completely removed
GST_START_TEST(videoroimetafilter_preserve7)
{
   GstStructure *koala;
   GenerateAnimalStructs(NULL, NULL, NULL, &koala);
   intersect_field(koala, "location");
   GList *check_list = structs_to_checklist(NULL,NULL,NULL,koala);
   fail_unless(run_pipeline(";location", NULL, check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//preserve the "sound" field from "dog" struct only
GST_START_TEST(videoroimetafilter_preserve8)
{
   GstStructure *dog;
   GenerateAnimalStructs(&dog, NULL, NULL, NULL);
   intersect_field(dog, "sound");
   GList *check_list = structs_to_checklist(dog,NULL,NULL,NULL);
   fail_unless(run_pipeline("dog;sound", NULL, check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//preserve the "num_lives" field from "cat",
// and the "does_hibernate" field from grizzly
GST_START_TEST(videoroimetafilter_preserve9)
{
   GstStructure *cat, *grizzly;
   GenerateAnimalStructs(NULL, &cat, &grizzly, NULL);
   intersect_field(cat, "num_lives");
   intersect_field(grizzly, "does_hibernate");
   GList *check_list = structs_to_checklist(NULL,cat,grizzly,NULL);
   fail_unless(run_pipeline("cat;num_lives,grizzly;does_hibernate", NULL, check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//preserve the "trick" field from dog,cat,grizzly
// preserve koala entirely
GST_START_TEST(videoroimetafilter_preserve10)
{
   GstStructure *dog,*cat,*grizzly,*koala;
   GenerateAnimalStructs(&dog, &cat, &grizzly, &koala);
   intersect_field(dog, "trick");
   intersect_field(cat, "trick");
   intersect_field(grizzly, "trick");
   GList *check_list = structs_to_checklist(dog,cat,grizzly,koala);
   fail_unless(run_pipeline("dog|cat|grizzly;trick,koala;.*", NULL, check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//remove dog structure
GST_START_TEST(videoroimetafilter_remove0)
{
   GstStructure *cat,*grizzly,*koala;
   GenerateAnimalStructs(NULL, &cat, &grizzly, &koala);
   GList *check_list = structs_to_checklist(NULL,cat,grizzly,koala);
   fail_unless(run_pipeline(NULL, "dog", check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//remove dog and cat structures
GST_START_TEST(videoroimetafilter_remove1)
{
   GstStructure *grizzly,*koala;
   GenerateAnimalStructs(NULL, NULL, &grizzly, &koala);
   GList *check_list = structs_to_checklist(NULL,NULL,grizzly,koala);
   fail_unless(run_pipeline(NULL, "dog|cat", check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//remove any structures with bear in the name
GST_START_TEST(videoroimetafilter_remove2)
{
   GstStructure *dog,*cat;
   GenerateAnimalStructs(&dog, &cat, NULL, NULL);
   GList *check_list = structs_to_checklist(dog,cat,NULL,NULL);
   fail_unless(run_pipeline(NULL, "bear", check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//remove any structures that are exactly "bear"
// (shouldn't remove anything)
GST_START_TEST(videoroimetafilter_remove3)
{
   GstStructure *dog,*cat,*grizzly,*koala;
   GenerateAnimalStructs(&dog, &cat, &grizzly, &koala);
   GList *check_list = structs_to_checklist(dog,cat,grizzly,koala);
   fail_unless(run_pipeline(NULL, "^bear$", check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//remove "color" field from all structures
GST_START_TEST(videoroimetafilter_remove4)
{
   GstStructure *dog,*cat,*grizzly,*koala;
   GenerateAnimalStructs(&dog, &cat, &grizzly, &koala);
   remove_field(dog, "color");
   remove_field(cat, "color");
   remove_field(grizzly, "color");
   remove_field(koala, "color");
   GList *check_list = structs_to_checklist(dog,cat,grizzly,koala);
   fail_unless(run_pipeline(NULL, ";color", check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//remove "color" field from "cat" structure only
GST_START_TEST(videoroimetafilter_remove5)
{
   GstStructure *dog,*cat,*grizzly,*koala;
   GenerateAnimalStructs(&dog, &cat, &grizzly, &koala);
   remove_field(cat, "color");
   GList *check_list = structs_to_checklist(dog,cat,grizzly,koala);
   fail_unless(run_pipeline(NULL, "cat;color", check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//remove "color" field from both bears,
// "num_lives" field from "cat",
// "trick" field from dog
GST_START_TEST(videoroimetafilter_remove6)
{
   GstStructure *dog,*cat,*grizzly,*koala;
   GenerateAnimalStructs(&dog, &cat, &grizzly, &koala);
   remove_field(grizzly, "color");
   remove_field(koala, "color");
   remove_field(cat, "num_lives");
   remove_field(dog, "trick");
   GList *check_list = structs_to_checklist(dog,cat,grizzly,koala);
   fail_unless(run_pipeline(NULL, "bear;color,cat;num_lives,dog;trick", check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//remove any structures that are name "blah"
// (shouldn't remove anything)
GST_START_TEST(videoroimetafilter_remove7)
{
   GstStructure *dog,*cat,*grizzly,*koala;
   GenerateAnimalStructs(&dog, &cat, &grizzly, &koala);
   GList *check_list = structs_to_checklist(dog,cat,grizzly,koala);
   fail_unless(run_pipeline(NULL, ".*;blah", check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//remove any fields named "location"
GST_START_TEST(videoroimetafilter_remove8)
{
   GstStructure *dog,*cat,*grizzly,*koala;
   GenerateAnimalStructs(&dog, &cat, &grizzly, &koala);
   remove_field(koala, "location");
   GList *check_list = structs_to_checklist(dog,cat,grizzly,koala);
   fail_unless(run_pipeline(NULL, ".*;location", check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//preserve any struct named dog, but remove any fields named color
GST_START_TEST(videoroimetafilter_preserveremove0)
{
   GstStructure *dog;
   GenerateAnimalStructs(&dog, NULL, NULL, NULL);
   remove_field(dog, "color");
   GList *check_list = structs_to_checklist(dog,NULL,NULL,NULL);
   fail_unless(run_pipeline("dog", ";color", check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//preserve any struct named cat, but then also remove it
//(should remove everything)
GST_START_TEST(videoroimetafilter_preserveremove1)
{
   GenerateAnimalStructs(NULL, NULL, NULL, NULL);
   GList *check_list = structs_to_checklist(NULL,NULL,NULL,NULL);
   fail_unless(run_pipeline("cat","cat", check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//preserve any structs with "bear", but
// remove any that are koala
GST_START_TEST(videoroimetafilter_preserveremove2)
{
   GstStructure *grizzly;
   GenerateAnimalStructs(NULL, NULL, &grizzly, NULL);
   GList *check_list = structs_to_checklist(NULL,NULL,grizzly,NULL);
   fail_unless(run_pipeline("bear", "koala", check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//preserve any structs with "bear", and
// remove color field from koala struct
GST_START_TEST(videoroimetafilter_preserveremove3)
{
   GstStructure *grizzly, *koala;
   GenerateAnimalStructs(NULL, NULL, &grizzly, &koala);
   remove_field(koala, "color");
   GList *check_list = structs_to_checklist(NULL,NULL,grizzly,koala);
   fail_unless(run_pipeline("bear", "koala;color", check_list));
   destroy_checklist(check_list);
}
GST_END_TEST

//When we set preserve / remove filters to ""
// we don't want any action taken
GST_START_TEST(videoroimetafilter_preserveremove4)
{
   GstStructure *dog,*cat,*grizzly,*koala;
   GenerateAnimalStructs(&dog, &cat, &grizzly, &koala);
   GList *check_list = structs_to_checklist(dog,cat,grizzly,koala);
   fail_unless(run_pipeline("", "", check_list));
   destroy_checklist(check_list);
}
GST_END_TEST


static Suite *
videoroimetafilter_suite (void)
{
  Suite *s = suite_create ("videoroimetafilter");
  ROB_ADD_TEST_CASE(videoroimetafilter_preserve0);
  ROB_ADD_TEST_CASE(videoroimetafilter_preserve1);
  ROB_ADD_TEST_CASE(videoroimetafilter_preserve2);
  ROB_ADD_TEST_CASE(videoroimetafilter_preserve3);
  ROB_ADD_TEST_CASE(videoroimetafilter_preserve4);
  ROB_ADD_TEST_CASE(videoroimetafilter_preserve5);
  ROB_ADD_TEST_CASE(videoroimetafilter_preserve6);
  ROB_ADD_TEST_CASE(videoroimetafilter_preserve7);
  ROB_ADD_TEST_CASE(videoroimetafilter_preserve8);
  ROB_ADD_TEST_CASE(videoroimetafilter_preserve9);
  ROB_ADD_TEST_CASE(videoroimetafilter_preserve10);

  ROB_ADD_TEST_CASE(videoroimetafilter_remove0);
  ROB_ADD_TEST_CASE(videoroimetafilter_remove1);
  ROB_ADD_TEST_CASE(videoroimetafilter_remove2);
  ROB_ADD_TEST_CASE(videoroimetafilter_remove3);
  ROB_ADD_TEST_CASE(videoroimetafilter_remove4);
  ROB_ADD_TEST_CASE(videoroimetafilter_remove5);
  ROB_ADD_TEST_CASE(videoroimetafilter_remove6);
  ROB_ADD_TEST_CASE(videoroimetafilter_remove7);
  ROB_ADD_TEST_CASE(videoroimetafilter_remove8);

  ROB_ADD_TEST_CASE(videoroimetafilter_preserveremove0);
  ROB_ADD_TEST_CASE(videoroimetafilter_preserveremove1);
  ROB_ADD_TEST_CASE(videoroimetafilter_preserveremove2);
  ROB_ADD_TEST_CASE(videoroimetafilter_preserveremove3);
  ROB_ADD_TEST_CASE(videoroimetafilter_preserveremove4);

  return s;
}

GST_CHECK_MAIN (videoroimetafilter);


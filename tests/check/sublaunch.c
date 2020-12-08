/*
 *  sublaunch.c - Set of tests for sublaunch element
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
#include "robtestutils.h"

//single element
GST_START_TEST(sublaunch0)
{
   static const gchar *pipeline0_str = "videotestsrc num-buffers=32 ! videoconvert ! "
                                       "sublaunch launch-string=\" queue \" ! "
                                       "appsink name=appsink0 sync=false qos=false";

   static const gchar *pipeline1_str = "videotestsrc num-buffers=32 ! videoconvert ! "
                                       " queue ! "
                                       "appsink name=appsink0 sync=false qos=false";

   fail_unless(test_pipelines_match(pipeline0_str, pipeline1_str));
}
GST_END_TEST

//single element, specify src link separately
GST_START_TEST(sublaunch1)
{
   static const gchar *pipeline0_str = "videotestsrc num-buffers=32 ! videoconvert ! "
                                       "sublaunch name=sl launch-string=\" queue \"  "
                                       "sl. ! appsink name=appsink0 sync=false qos=false";

   static const gchar *pipeline1_str = "videotestsrc num-buffers=32 ! videoconvert ! "
                                       " queue ! "
                                       "appsink name=appsink0 sync=false qos=false";

   fail_unless(test_pipelines_match(pipeline0_str, pipeline1_str));
}
GST_END_TEST

//single element, specify src link by element name
GST_START_TEST(sublaunch2)
{
   static const gchar *pipeline0_str = "videotestsrc num-buffers=32 ! videoconvert ! "
                                       "sublaunch name=sl launch-string=\" queue name=q \"  "
                                       "sl.src:q ! appsink name=appsink0 sync=false qos=false";

   static const gchar *pipeline1_str = "videotestsrc num-buffers=32 ! videoconvert ! "
                                       " queue ! "
                                       "appsink name=appsink0 sync=false qos=false";

   fail_unless(test_pipelines_match(pipeline0_str, pipeline1_str));
}
GST_END_TEST

//single element, specify src link by element & pad name
GST_START_TEST(sublaunch3)
{
   static const gchar *pipeline0_str = "videotestsrc num-buffers=32 ! videoconvert ! "
                                       "sublaunch name=sl launch-string=\" queue name=q \"  "
                                       "sl.src:q:src ! appsink name=appsink0 sync=false qos=false";

   static const gchar *pipeline1_str = "videotestsrc num-buffers=32 ! videoconvert ! "
                                       " queue ! "
                                       "appsink name=appsink0 sync=false qos=false";

   fail_unless(test_pipelines_match(pipeline0_str, pipeline1_str));
}
GST_END_TEST

//single element, specify both src & sink link separately
GST_START_TEST(sublaunch4)
{
   static const gchar *pipeline0_str = "videotestsrc num-buffers=32 ! videoconvert name=vc "
                                       "sublaunch name=sl launch-string=\" queue \"  "
                                       "vc. ! sl. "
                                       "sl. ! appsink name=appsink0 sync=false qos=false";

   static const gchar *pipeline1_str = "videotestsrc num-buffers=32 ! videoconvert ! "
                                       " queue ! "
                                       "appsink name=appsink0 sync=false qos=false";

   fail_unless(test_pipelines_match(pipeline0_str, pipeline1_str));
}
GST_END_TEST

//single element, specify both src & sink link separately, by element name
GST_START_TEST(sublaunch5)
{
   static const gchar *pipeline0_str = "videotestsrc num-buffers=32 ! videoconvert name=vc "
                                       "sublaunch name=sl launch-string=\" queue name=q \"  "
                                       "vc. ! sl.sink:q "
                                       "sl.src:q ! appsink name=appsink0 sync=false qos=false";

   static const gchar *pipeline1_str = "videotestsrc num-buffers=32 ! videoconvert ! "
                                       " queue ! "
                                       "appsink name=appsink0 sync=false qos=false";

   fail_unless(test_pipelines_match(pipeline0_str, pipeline1_str));
}
GST_END_TEST

//single element, specify both src & sink link separately, by element & pad name
GST_START_TEST(sublaunch6)
{
   static const gchar *pipeline0_str = "videotestsrc num-buffers=32 ! videoconvert name=vc "
                                       "sublaunch name=sl launch-string=\" queue name=q \"  "
                                       "vc. ! sl.sink:q:sink "
                                       "sl.src:q:src ! appsink name=appsink0 sync=false qos=false";

   static const gchar *pipeline1_str = "videotestsrc num-buffers=32 ! videoconvert ! "
                                       " queue ! "
                                       "appsink name=appsink0 sync=false qos=false";

   fail_unless(test_pipelines_match(pipeline0_str, pipeline1_str));
}
GST_END_TEST

//Data streams exiting remoteoffloadbin are from request pads

//single element, but one that has multiple src pads.
//                     ___
//                    |   |-------->[queue]------>[appsink]
//  [videotestsrc]--->|tee|-------->[queue]------>[appsink]
//                    |___|-------->[queue]------>[appsink]
GST_START_TEST(sublaunch7)
{
   static const gchar *pipeline0_str =
      "videotestsrc num-buffers=138 pattern=snow ! "
      "sublaunch name=sl launch-string=\" tee name=t \" "
      "sl.src:t ! queue name=q0 "
      "sl.src:t ! queue name=q1 "
      "sl.src:t ! queue name=q2 "
      "q0. ! appsink name=appsink0 sync=false qos=false "
      "q1. ! appsink name=appsink1 sync=false qos=false "
      "q2. ! appsink name=appsink2 sync=false qos=false ";

   static const gchar *pipeline1_str =
      "videotestsrc num-buffers=138 pattern=snow ! tee name=t  "
      "t. ! queue name=q0 "
      "t. ! queue name=q1 "
      "t. ! queue name=q2 "
      "q0. ! appsink name=appsink0 sync=false qos=false "
      "q1. ! appsink name=appsink1 sync=false qos=false "
      "q2. ! appsink name=appsink2 sync=false qos=false ";

   fail_unless(test_pipelines_match(pipeline0_str, pipeline1_str));
}
GST_END_TEST


//single element, but one that has multiple src pads.
// Specify each src pad by name
// Note: This will invoke the '-' to '_' workaround.
//                     ___
//                    |   |-------->[queue]------>[appsink]
//  [videotestsrc]--->|tee|-------->[queue]------>[appsink]
//                    |___|-------->[queue]------>[appsink]
GST_START_TEST(sublaunch8)
{
   static const gchar *pipeline0_str =
      "videotestsrc num-buffers=138 pattern=snow ! "
      "sublaunch name=sl launch-string=\" tee name=t \" "
      "sl.src:t:src-0 ! queue name=q0 "
      "sl.src:t:src-1 ! queue name=q1 "
      "sl.src:t:src-2 ! queue name=q2 "
      "q0. ! appsink name=appsink0 sync=false qos=false "
      "q1. ! appsink name=appsink1 sync=false qos=false "
      "q2. ! appsink name=appsink2 sync=false qos=false ";

   static const gchar *pipeline1_str =
      "videotestsrc num-buffers=138 pattern=snow ! tee name=t  "
      "t. ! queue name=q0 "
      "t. ! queue name=q1 "
      "t. ! queue name=q2 "
      "q0. ! appsink name=appsink0 sync=false qos=false "
      "q1. ! appsink name=appsink1 sync=false qos=false "
      "q2. ! appsink name=appsink2 sync=false qos=false ";

   fail_unless(test_pipelines_match(pipeline0_str, pipeline1_str));
}
GST_END_TEST


//Multiple elements, specify link by element name
// Specify each src pad by name
//                     ___
//                    |   |-------->[queue]------>[appsink]
//  [videotestsrc]--->|tee|-------->[queue]------>[appsink]
//                    |___|-------->[queue]------>[appsink]
GST_START_TEST(sublaunch9)
{
   static const gchar *pipeline0_str =
      "videotestsrc num-buffers=138 pattern=snow ! "
      " sublaunch name=sl launch-string=\" tee name=t  "
      "t. ! queue name=q0 "
      "t. ! queue name=q1 "
      "t. ! queue name=q2 \" "
      "sl.src:q0 ! appsink name=appsink0 sync=false qos=false "
      "sl.src:q1 ! appsink name=appsink1 sync=false qos=false "
      "sl.src:q2 ! appsink name=appsink2 sync=false qos=false ";

   static const gchar *pipeline1_str =
      "videotestsrc num-buffers=138 pattern=snow ! tee name=t  "
      "t. ! queue name=q0 "
      "t. ! queue name=q1 "
      "t. ! queue name=q2 "
      "q0. ! appsink name=appsink0 sync=false qos=false "
      "q1. ! appsink name=appsink1 sync=false qos=false "
      "q2. ! appsink name=appsink2 sync=false qos=false ";

   fail_unless(test_pipelines_match(pipeline0_str, pipeline1_str));
}
GST_END_TEST

//Multiple elements, elements with parameters
//
// [videotestsrc]---->[queue]-->[videocrop]-->[appsink]
//
GST_START_TEST(sublaunch10)
{
   static const gchar *pipeline0_str =
      "videotestsrc num-buffers=138 pattern=snow !  "
      " sublaunch launch-string=\" queue name=q0 ! videocrop name=c0 top=32 bottom=14 right=64 left=16 \" "
      " ! appsink name=appsink0 sync=false qos=false ";

   static const gchar *pipeline1_str =
      "videotestsrc num-buffers=138 pattern=snow !  "
      " queue name=q0 ! videocrop name=c0 top=32 bottom=14 right=64 left=16 "
      " ! appsink name=appsink0 sync=false qos=false ";

   fail_unless(test_pipelines_match(pipeline0_str, pipeline1_str));
}
GST_END_TEST

// Element with multiple sink pads
//                                 ____________
// [videotestsrc]---->[queue]---->| videomixer |-->[queue]-->[appsink]
// [videotestsrc]---->[queue]---->|____________|
GST_START_TEST(sublaunch11)
{
   static const gchar *pipeline0_str =
      "videotestsrc num-buffers=175 pattern=snow ! queue name=q0 "
      "videotestsrc num-buffers=175 pattern=ball ! queue name=q1 "
      " q0. ! sublaunch name=sl launch-string=\" videomixer name=mix \" "
      " q1. ! sl.sink:mix "
      " sl.src:mix:any ! queue ! appsink name=appsink0 sync=false qos=false ";

   static const gchar *pipeline1_str =
      "videotestsrc num-buffers=175 pattern=snow ! queue name=q0 "
      "videotestsrc num-buffers=175 pattern=ball ! queue name=q1 "
      " q0. ! videomixer name=mix q1. ! mix. "
      " mix. ! queue ! appsink name=appsink0 sync=false qos=false ";

   fail_unless(test_pipelines_match(pipeline0_str, pipeline1_str));
}
GST_END_TEST

//sublaunch + remoteoffloadbin tests
GST_START_TEST(sublaunch_rob0)
{
   static const gchar *pipeline0_str = "videotestsrc num-buffers=32 ! videoconvert ! "
                                       "remoteoffloadbin.( sublaunch launch-string=\" queue \" ) ! "
                                       "appsink name=appsink0 sync=false qos=false";

   fail_unless(test_rob_pipeline(pipeline0_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

GST_START_TEST(sublaunch_rob1)
{
   static const gchar *pipeline0_str =
      "videotestsrc num-buffers=138 pattern=snow !  "
      " remoteoffloadbin.( sublaunch launch-string=\" queue name=q0 ! videocrop name=c0 top=32 bottom=14 right=64 left=16 \" ) "
      " ! appsink name=appsink0 sync=false qos=false ";

   fail_unless(test_rob_pipeline(pipeline0_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

GST_START_TEST(sublaunch_rob2)
{
   static const gchar *pipeline0_str =
      "remoteoffloadbin.( videotestsrc num-buffers=138 pattern=snow ! "
      " sublaunch name=sl launch-string=\" tee name=t \" ) "
      "sl.src:t:src-0 ! queue name=q0 "
      "sl.src:t:src-1 ! queue name=q1 "
      "sl.src:t:src-2 ! queue name=q2 "
      "q0. ! appsink name=appsink0 sync=false qos=false "
      "q1. ! appsink name=appsink1 sync=false qos=false "
      "q2. ! appsink name=appsink2 sync=false qos=false ";

   fail_unless(test_rob_pipeline(pipeline0_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST


static Suite *
sublaunch_suite (void)
{
  Suite *s = suite_create ("sublaunch");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, sublaunch0);
  tcase_add_test (tc_chain, sublaunch1);
  tcase_add_test (tc_chain, sublaunch2);
  tcase_add_test (tc_chain, sublaunch3);
  tcase_add_test (tc_chain, sublaunch4);
  tcase_add_test (tc_chain, sublaunch5);
  tcase_add_test (tc_chain, sublaunch6);
  tcase_add_test (tc_chain, sublaunch7);
  tcase_add_test (tc_chain, sublaunch8);
  tcase_add_test (tc_chain, sublaunch9);
  tcase_add_test (tc_chain, sublaunch10);
  tcase_add_test (tc_chain, sublaunch11);

  tcase_add_test (tc_chain, sublaunch_rob0);
  tcase_add_test (tc_chain, sublaunch_rob1);
  tcase_add_test (tc_chain, sublaunch_rob2);

#ifdef HAVE_VALGRIND
  /* Use a longer timeout if we're currently running with valgrind*/
  if (RUNNING_ON_VALGRIND) {
    tcase_set_timeout (tc_chain, 60);
  }
  else
#endif
  {
     tcase_set_timeout (tc_chain, 15);
  }

  return s;
}

GST_CHECK_MAIN (sublaunch);


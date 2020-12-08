/*
 *  rob_basic.c - Basic set of remoteoffloadbin tests
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
 *  pipelines intended to test basic functionality of remoteoffloadbin.
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

//Error happens on ROB-side, half way through execution
static const gchar *error0_str = "videotestsrc num-buffers=256 ! videoconvert ! "
                                 "remoteoffloadbin.( queue ! identity error-after=128 ) ! "
                                 "fakesink";

GST_START_TEST(error0)
{
   fail_unless(test_rob_pipeline(error0_str, TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG));
}
GST_END_TEST

//Error happens on host-side, half-way through execution
static const gchar *error1_str = "videotestsrc num-buffers=256 ! videoconvert ! "
                                 "remoteoffloadbin.( queue ) ! identity error-after=128 ! "
                                 "fakesink";

GST_START_TEST(error1)
{
   fail_unless(test_rob_pipeline(error1_str, TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG));
}
GST_END_TEST

//Error happens on ROB-side, on first buffer (during PREROLL for non-live cases)
static const gchar *error2_str = "videotestsrc num-buffers=256 ! videoconvert ! "
                                 "remoteoffloadbin.( queue ! identity error-after=1 ) ! "
                                 "fakesink";

GST_START_TEST(error2)
{
   fail_unless(test_rob_pipeline(error2_str, TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG));
}
GST_END_TEST

//Error happens on host-side, on first buffer (during PREROLL for non-live cases)
static const gchar *error3_str = "videotestsrc num-buffers=256 ! videoconvert ! "
                                 "remoteoffloadbin.( queue ) ! identity error-after=1 ! "
                                 "fakesink";

GST_START_TEST(error3)
{
   fail_unless(test_rob_pipeline(error3_str, TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG));
}
GST_END_TEST

//Error happens on ROB-side(source of the pipeline), half way through execution
static const gchar *error4_str = "remoteoffloadbin.( videotestsrc num-buffers=256 ! videoconvert ! "
                                 " identity error-after=128 ) ! "
                                 "fakesink";
GST_START_TEST(error4)
{
   fail_unless(test_rob_pipeline(error4_str, TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG));
}
GST_END_TEST

//Error happens on host-side , half way through execution
static const gchar *error5_str = "remoteoffloadbin.( videotestsrc num-buffers=256 ! videoconvert ) ! "
                                 " identity error-after=128  ! "
                                 "fakesink";
GST_START_TEST(error5)
{
   fail_unless(test_rob_pipeline(error5_str, TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG));
}
GST_END_TEST

//Error happens on ROB-side(source of the pipeline), half way through execution
static const gchar *error6_str = "remoteoffloadbin.( videotestsrc num-buffers=256 ! videoconvert ! "
                                 " identity error-after=1 ) ! "
                                 "fakesink";
GST_START_TEST(error6)
{
   fail_unless(test_rob_pipeline(error6_str, TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG));
}
GST_END_TEST

//Error happens on host-side, half way through execution
static const gchar *error7_str = "remoteoffloadbin.( videotestsrc num-buffers=256 ! videoconvert ) ! "
                                 " identity error-after=1  ! "
                                 "fakesink";
GST_START_TEST(error7)
{
   fail_unless(test_rob_pipeline(error7_str, TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG));
}
GST_END_TEST

GST_START_TEST(error0_LIVE)
{
   fail_unless(test_rob_pipeline(error0_str, TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG |
                                             TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(error1_LIVE)
{
   fail_unless(test_rob_pipeline(error1_str, TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG |
                                             TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(error2_LIVE)
{
   fail_unless(test_rob_pipeline(error2_str, TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG |
                                             TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(error3_LIVE)
{
   fail_unless(test_rob_pipeline(error3_str, TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG |
                                             TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(error4_LIVE)
{
   fail_unless(test_rob_pipeline(error4_str, TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG |
                                             TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(error5_LIVE)
{
   fail_unless(test_rob_pipeline(error5_str, TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG |
                                             TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(error6_LIVE)
{
   fail_unless(test_rob_pipeline(error6_str, TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG |
                                             TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(error7_LIVE)
{
   fail_unless(test_rob_pipeline(error7_str, TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG |
                                             TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

//Using sublaunch, user tries to specify some non-existent element
static const gchar *error_sublaunch_str = "videotestsrc num-buffers=32 ! videoconvert ! "
                                 "remoteoffloadbin.( sublaunch launch-string=\" somenonexistent \" ) ! "
                                 "fakesink";
GST_START_TEST(error_sublaunch)
{
   fail_unless(test_rob_pipeline(error_sublaunch_str, TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG));
}
GST_END_TEST



static Suite *
rob_error_handling_suite (void)
{
  Suite *s = suite_create ("rob_error");

  ROB_ADD_TEST_CASE(error0)
  ROB_ADD_TEST_CASE(error1)
  ROB_ADD_TEST_CASE(error2)
  ROB_ADD_TEST_CASE(error3)
  ROB_ADD_TEST_CASE(error4)
  ROB_ADD_TEST_CASE(error5)
  ROB_ADD_TEST_CASE(error6)
  ROB_ADD_TEST_CASE(error7)

  ROB_ADD_TEST_CASE(error0_LIVE)
  ROB_ADD_TEST_CASE(error1_LIVE)
  ROB_ADD_TEST_CASE(error2_LIVE)
  ROB_ADD_TEST_CASE(error3_LIVE)
  ROB_ADD_TEST_CASE(error4_LIVE)
  ROB_ADD_TEST_CASE(error5_LIVE)
  ROB_ADD_TEST_CASE(error6_LIVE)
  ROB_ADD_TEST_CASE(error7_LIVE)

  ROB_ADD_TEST_CASE(error_sublaunch)

  return s;
}

GST_CHECK_MAIN (rob_error_handling);


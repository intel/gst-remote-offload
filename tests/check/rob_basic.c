/*
 *  rob_basic.c - Basic set of remoteoffloadbin tests
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

//1 in, 1 out
static const gchar *basic0_str = "videotestsrc num-buffers=32 ! videoconvert ! "
                                 "remoteoffloadbin.( queue ) ! "
                                 "appsink name=appsink0 sync=false qos=false";

GST_START_TEST(basic0)
{
   fail_unless(test_rob_pipeline(basic0_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

static const gchar *basic1_str = "videotestsrc num-buffers=256 pattern=ball motion=wavy ! "
                                 "remoteoffloadbin.( videoconvert !  queue ) ! "
                                 "appsink name=appsink0 sync=false qos=false";

GST_START_TEST(basic1)
{
   fail_unless(test_rob_pipeline(basic1_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

//0 in, 1 out
static const gchar *basic2_str = "remoteoffloadbin.( videotestsrc num-buffers=256 pattern=ball motion=wavy ! "
                                 " videoconvert !  queue ) ! "
                                 "appsink name=appsink0 sync=false qos=false";

GST_START_TEST(basic2)
{
   fail_unless(test_rob_pipeline(basic2_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

//2 remoteoffloadbin's in the same pipeline
static const gchar *basic3_str = "remoteoffloadbin.( videotestsrc num-buffers=87 pattern=snow ) !  "
                                 "videoconvert !  remoteoffloadbin.( queue )  ! "
                                 "appsink name=appsink0 sync=false qos=false";

GST_START_TEST(basic3)
{
   fail_unless(test_rob_pipeline(basic3_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

//2 remoteoffloadbin's in the same pipeline, right next to each other
static const gchar *basic4_str =  "remoteoffloadbin.( videotestsrc num-buffers=87 pattern=snow ) !  "
                                  "remoteoffloadbin.( videoconvert !   queue )  ! "
                                  "appsink name=appsink0 sync=false qos=false";

GST_START_TEST(basic4)
{
   fail_unless(test_rob_pipeline(basic4_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

//caps negotiation should force videotestsrc to produce 1080p buffers
static const gchar *basic5_str = "remoteoffloadbin.( videotestsrc num-buffers=16 pattern=snow ) ! "
      "video/x-raw,width=1920,height=1080 ! "
      "remoteoffloadbin.( videoconvert !   queue )  ! appsink name=appsink0 sync=false qos=false";

GST_START_TEST(basic5)
{
   fail_unless(test_rob_pipeline(basic5_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

//2 in, 2 out
static const gchar *basic6_str = "videotestsrc num-buffers=127 pattern=snow ! tee name=t ! "
                                 "queue name=q0 t. ! queue name=q1  "
                                 "remoteoffloadbin.( q0. ! queue name=q2 q1. ! queue name=q3 ) "
                                 "q2. ! videoconvert ! appsink name=appsink0 sync=false qos=false "
                                 "q3. ! videoconvert ! appsink name=appsink1 sync=false qos=false";

GST_START_TEST(basic6)
{
   fail_unless(test_rob_pipeline(basic6_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

//1 in, 2 out
static const gchar *basic7_str = "videotestsrc num-buffers=185 pattern=snow ! "
                                 "remoteoffloadbin.( tee name=t ! queue name=q0 t. ! "
                                 "queue name=q1   q0. ! queue name=q2 q1. ! queue name=q3 ) q2. ! "
                                 "videoconvert ! appsink name=appsink0 sync=false qos=false q3. ! "
                                 "videoconvert ! appsink name=appsink1 sync=false qos=false";

GST_START_TEST(basic7)
{
   fail_unless(test_rob_pipeline(basic7_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

//0 in, 2 out
static const gchar *basic8_str = "remoteoffloadbin.( videotestsrc num-buffers=49 pattern=snow !  "
                                 "tee name=t ! queue name=q0 t. ! queue name=q1   "
                                 "q0. ! queue name=q2 q1. ! queue name=q3 ) q2. ! "
                                 "videoconvert ! appsink name=appsink0 sync=false qos=false q3. ! "
                                 "videoconvert ! appsink name=appsink1 sync=false qos=false";

GST_START_TEST(basic8)
{
   fail_unless(test_rob_pipeline(basic8_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

//pipeline consists purely of offloaded elements.
static const gchar *basic9_str = "remoteoffloadbin.( videotestsrc num-buffers=32 ! videoconvert ! "
                                 " queue  ! fakesink name=fakesink0 sync=false qos=false )";

GST_START_TEST(basic9)
{
   fail_unless(test_rob_pipeline(basic9_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

//1 in, 0 out
static const gchar *basic10_str =
      "videotestsrc num-buffers=117 pattern=snow ! videoconvert !  "
      "remoteoffloadbin.(  queue  ! fakesink name=fakesink0 sync=false qos=false )";

GST_START_TEST(basic10)
{
   fail_unless(test_rob_pipeline(basic10_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

//multiple channels in  / out
//                                ____________
//                               |    ROB     |
// [videotestsrc]---->[queue]----|-->[queue]--|-->[queue]-->[appsink]
// [videotestsrc]---->[queue]----|-->[queue]--|-->[queue]-->[appsink]
//                               |____________|
static const gchar *basic11_str =
      "videotestsrc num-buffers=117 pattern=snow ! queue name=q0 "
      "videotestsrc num-buffers=117 pattern=ball ! queue name=q1 "
      "remoteoffloadbin.( q0. ! queue name=q3 q1. ! queue name=q4 ) "
      "q3. ! videoconvert ! appsink name=appsink0 sync=false qos=false "
      "q4. ! videoconvert ! appsink name=appsink1 sync=false qos=false ";

GST_START_TEST(basic11)
{
   fail_unless(test_rob_pipeline(basic11_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

//multiple channels in  / out, differing number of buffers
//                                ____________
//                               |    ROB     |
// [videotestsrc]---->[queue]----|-->[queue]--|-->[queue]-->[appsink]
// [videotestsrc]---->[queue]----|-->[queue]--|-->[queue]-->[appsink]
//                               |____________|
static const gchar *basic12_str =
      "videotestsrc num-buffers=127 pattern=snow ! queue name=q0 "
      "videotestsrc num-buffers=50 pattern=ball ! queue name=q1 "
      "remoteoffloadbin.( q0. ! queue name=q3 q1. ! queue name=q4 ) "
      "q3. ! videoconvert ! appsink name=appsink0 sync=false qos=false "
      "q4. ! videoconvert ! appsink name=appsink1 sync=false qos=false ";

GST_START_TEST(basic12)
{
   fail_unless(test_rob_pipeline(basic12_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

//multiple channels in, single channel out, same number of buffers
//                                ___________________
//                               |    ROB            |
//                               |    ____________   |
// [videotestsrc]---->[queue]----|-->| videomixer |--|-->[queue]-->[appsink]
// [videotestsrc]---->[queue]----|-->|____________|  |
//                               |___________________|
static const gchar *basic13_str =
      "videotestsrc num-buffers=175 pattern=snow ! queue name=q0 "
      "videotestsrc num-buffers=175 pattern=ball ! queue name=q1 "
      "remoteoffloadbin.( q0. ! videomixer name=mix q1. ! mix. ) "
      "mix. ! queue ! appsink name=appsink0 sync=false qos=false ";

GST_START_TEST(basic13)
{
   fail_unless(test_rob_pipeline(basic13_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

//multiple channels in, single channel out, different number of buffers
//                                ___________________
//                               |    ROB            |
//                               |    ____________   |
// [videotestsrc]---->[queue]----|-->| videomixer |--|-->[queue]-->[appsink]
// [videotestsrc]---->[queue]----|-->|____________|  |
//                               |___________________|
static const gchar *basic14_str =
      "videotestsrc num-buffers=175 pattern=snow ! queue name=q0 "
      "videotestsrc num-buffers=111 pattern=ball ! queue name=q1 "
      "remoteoffloadbin.( q0. ! videomixer name=mix q1. ! mix. ) "
      "mix. ! queue ! appsink name=appsink0 sync=false qos=false ";


GST_START_TEST(basic14)
{
   fail_unless(test_rob_pipeline(basic14_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

//multiple channels in  / out, differing number of buffers, one stream is scaled
//                                ___________________________________
//                               |    ROB                            |
// [videotestsrc]---->[queue]----|-->[videoconvert]-->[videoscale]-->|-->[capsfilter]-->[appsink]
// [videotestsrc]---->[queue]----|----------->[queue]----------------|-->[queue]-->[appsink]
//                               |___________________________________|
static const gchar *basic15_str =
      "videotestsrc num-buffers=127 pattern=snow ! queue name=q0 "
      "videotestsrc num-buffers=50 pattern=ball ! queue name=q1 "
      "remoteoffloadbin.( q0. ! videoconvert ! videoscale name=vidscale  q1. ! queue name=q4 ) "
      "vidscale. ! video/x-raw,width=1280,height=720 ! appsink name=appsink0 sync=false qos=false "
      "q4. ! videoconvert ! appsink name=appsink1 sync=false qos=false ";

GST_START_TEST(basic15)
{
   fail_unless(test_rob_pipeline(basic15_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

//Data streams entering remoteoffloadbin are from request pads
//                                _____________
//                               |    ROB      |
//                     ___       |             |
//                    |   |------|-->[queue]-->|-->[appsink]
// [videotestsrc]---->|tee|------|-->[queue]-->|-->[appsink]
//                    |___|------|-->[queue]-->|-->[appsink]
//                               |             |
//                               |_____________|
static const gchar *basic16_str =
      "videotestsrc num-buffers=138 pattern=snow ! tee name=t "
      "remoteoffloadbin.( t. ! queue name=q0 "
      "                   t. ! queue name=q1 "
      "                   t. ! queue name=q2 ) "
      "q0. ! appsink name=appsink0 sync=false qos=false "
      "q1. ! appsink name=appsink1 sync=false qos=false "
      "q2. ! appsink name=appsink2 sync=false qos=false ";

GST_START_TEST(basic16)
{
   fail_unless(test_rob_pipeline(basic16_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

//Data streams exiting remoteoffloadbin are from request pads
//  ____________________________
// |                 ROB        |
// |                    ___     |
// |                   |   |----|---->[queue]------>[appsink]
// | [videotestsrc]--->|tee|----|---->[queue]------>[appsink]
// |                   |___|----|---->[queue]------>[appsink]
// |                            |
// |____________________________|
static const gchar *basic17_str =
      "remoteoffloadbin.( videotestsrc num-buffers=138 pattern=snow ! tee name=t ) "
      "t. ! queue name=q0 "
      "t. ! queue name=q1 "
      "t. ! queue name=q2 "
      "q0. ! appsink name=appsink0 sync=false qos=false "
      "q1. ! appsink name=appsink1 sync=false qos=false "
      "q2. ! appsink name=appsink2 sync=false qos=false ";

GST_START_TEST(basic17)
{
   fail_unless(test_rob_pipeline(basic17_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

//Add differing parameter values for each videocrop inside ROB
//                                __________________________
//                               |    ROB                   |
//                     ___       |                          |
//                    |   |------|-->[queue]-->[videocrop]--|-->[appsink]
// [videotestsrc]---->|tee|------|-->[queue]-->[videocrop]--|-->[appsink]
//                    |___|------|-->[queue]-->[videocrop]--|-->[appsink]
//                               |                          |
//                               |__________________________|
static const gchar *basic18_str =
  "videotestsrc num-buffers=138 pattern=snow ! tee name=t "
  "remoteoffloadbin.( t. ! queue name=q0 ! videocrop name=c0 top=32 bottom=14 right=64 left=16 "
  "                   t. ! queue name=q1 ! videocrop name=c1 top=0 bottom=96 right=16 left=128 "
  "                   t. ! queue name=q2 ! videocrop name=c2 top=55 bottom=90 right=256 left=16 ) "
  "c0. ! appsink name=appsink0 sync=false qos=false "
  "c1. ! appsink name=appsink1 sync=false qos=false "
  "c2. ! appsink name=appsink2 sync=false qos=false ";


GST_START_TEST(basic18)
{
   fail_unless(test_rob_pipeline(basic18_str, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

//The following tests will transiton pipeline from NULL->PLAYING,
// and upon EOS will transition pipeline from PLAYING->READY.
// And then will transition from READY->PLAYING again.
GST_START_TEST(basic0_playing_ready_playing)
{
   fail_unless(test_rob_pipeline(basic0_str, TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING));
}
GST_END_TEST

GST_START_TEST(basic1_playing_ready_playing)
{
   fail_unless(test_rob_pipeline(basic1_str, TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING));
}
GST_END_TEST

GST_START_TEST(basic2_playing_ready_playing)
{
   fail_unless(test_rob_pipeline(basic2_str, TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING));
}
GST_END_TEST

GST_START_TEST(basic3_playing_ready_playing)
{
   fail_unless(test_rob_pipeline(basic3_str, TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING));
}
GST_END_TEST

GST_START_TEST(basic4_playing_ready_playing)
{
   fail_unless(test_rob_pipeline(basic4_str, TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING));
}
GST_END_TEST

GST_START_TEST(basic5_playing_ready_playing)
{
   fail_unless(test_rob_pipeline(basic5_str, TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING));
}
GST_END_TEST

GST_START_TEST(basic6_playing_ready_playing)
{
   fail_unless(test_rob_pipeline(basic6_str, TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING));
}
GST_END_TEST

GST_START_TEST(basic7_playing_ready_playing)
{
   fail_unless(test_rob_pipeline(basic7_str, TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING));
}
GST_END_TEST

GST_START_TEST(basic8_playing_ready_playing)
{
   fail_unless(test_rob_pipeline(basic8_str, TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING));
}
GST_END_TEST

GST_START_TEST(basic9_playing_ready_playing)
{
   fail_unless(test_rob_pipeline(basic9_str, TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING));
}
GST_END_TEST

GST_START_TEST(basic10_playing_ready_playing)
{
   fail_unless(test_rob_pipeline(basic10_str, TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING));
}
GST_END_TEST

GST_START_TEST(basic11_playing_ready_playing)
{
   fail_unless(test_rob_pipeline(basic11_str, TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING));
}
GST_END_TEST

GST_START_TEST(basic12_playing_ready_playing)
{
   fail_unless(test_rob_pipeline(basic12_str, TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING));
}
GST_END_TEST

GST_START_TEST(basic13_playing_ready_playing)
{
   fail_unless(test_rob_pipeline(basic13_str, TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING));
}
GST_END_TEST

GST_START_TEST(basic14_playing_ready_playing)
{
   fail_unless(test_rob_pipeline(basic14_str, TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING));
}
GST_END_TEST

GST_START_TEST(basic15_playing_ready_playing)
{
   fail_unless(test_rob_pipeline(basic15_str, TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING));
}
GST_END_TEST

GST_START_TEST(basic16_playing_ready_playing)
{
   fail_unless(test_rob_pipeline(basic16_str, TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING));
}
GST_END_TEST

GST_START_TEST(basic17_playing_ready_playing)
{
   fail_unless(test_rob_pipeline(basic17_str, TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING));
}
GST_END_TEST

GST_START_TEST(basic18_playing_ready_playing)
{
   fail_unless(test_rob_pipeline(basic18_str, TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING));
}
GST_END_TEST

GST_START_TEST(basic0_LIVE)
{
   fail_unless(test_rob_pipeline(basic0_str, TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(basic1_LIVE)
{
   fail_unless(test_rob_pipeline(basic1_str, TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(basic2_LIVE)
{
   fail_unless(test_rob_pipeline(basic2_str, TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(basic3_LIVE)
{
   fail_unless(test_rob_pipeline(basic3_str, TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(basic4_LIVE)
{
   fail_unless(test_rob_pipeline(basic4_str, TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(basic5_LIVE)
{
   fail_unless(test_rob_pipeline(basic5_str, TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(basic6_LIVE)
{
   fail_unless(test_rob_pipeline(basic6_str, TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(basic7_LIVE)
{
   fail_unless(test_rob_pipeline(basic7_str, TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(basic8_LIVE)
{
   fail_unless(test_rob_pipeline(basic8_str, TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(basic9_LIVE)
{
   fail_unless(test_rob_pipeline(basic9_str, TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(basic10_LIVE)
{
   fail_unless(test_rob_pipeline(basic10_str, TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(basic15_LIVE)
{
   fail_unless(test_rob_pipeline(basic15_str, TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(basic16_LIVE)
{
   fail_unless(test_rob_pipeline(basic16_str, TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(basic17_LIVE)
{
   fail_unless(test_rob_pipeline(basic17_str, TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

GST_START_TEST(basic18_LIVE)
{
   fail_unless(test_rob_pipeline(basic18_str, TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE));
}
GST_END_TEST

//same as basic0, except force the subpipeline within ROB to claim LIVE
static const gchar *live_rob_str0 = "videotestsrc num-buffers=32 ! capsfilter caps=\"video/x-raw, framerate=120/1\" ! "
                                    "remoteoffloadbin.( queue ! identity sync=true ) ! "
                                    "appsink name=appsink0 sync=false qos=false";

GST_START_TEST(live_rob0)
{
   fail_unless(test_rob_pipeline(live_rob_str0, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST


//same as basic10, except force the subpipeline within ROB to claim LIVE
static const gchar *live_rob_str1 =
      "videotestsrc num-buffers=117 pattern=snow ! videoconvert ! capsfilter caps=\"video/x-raw, framerate=120/1\" ! "
      "remoteoffloadbin.(  queue  ! identity sync=true ! fakesink name=fakesink0 sync=false qos=false )";

GST_START_TEST(live_rob1)
{
   fail_unless(test_rob_pipeline(live_rob_str1, TESTROBPIPELINE_FLAG_NONE));
}
GST_END_TEST

static Suite *
rob_basic_suite (void)
{
  Suite *s = suite_create ("rob_basic");

  ROB_ADD_TEST_CASE(basic0)
  ROB_ADD_TEST_CASE(basic1)
  ROB_ADD_TEST_CASE(basic2)
  ROB_ADD_TEST_CASE(basic3)
  ROB_ADD_TEST_CASE(basic4)
  ROB_ADD_TEST_CASE(basic5)
  ROB_ADD_TEST_CASE(basic6)
  ROB_ADD_TEST_CASE(basic7)
  ROB_ADD_TEST_CASE(basic8)
  ROB_ADD_TEST_CASE(basic9)
  ROB_ADD_TEST_CASE(basic10)
  ROB_ADD_TEST_CASE(basic11)
  ROB_ADD_TEST_CASE(basic12)
  ROB_ADD_TEST_CASE(basic13)
  ROB_ADD_TEST_CASE(basic14)
  ROB_ADD_TEST_CASE(basic15)
  ROB_ADD_TEST_CASE(basic16)
  ROB_ADD_TEST_CASE(basic17)
  ROB_ADD_TEST_CASE(basic18)

  ROB_ADD_TEST_CASE(basic0_playing_ready_playing);
  ROB_ADD_TEST_CASE(basic1_playing_ready_playing);
  ROB_ADD_TEST_CASE(basic2_playing_ready_playing);
  ROB_ADD_TEST_CASE(basic3_playing_ready_playing);
  ROB_ADD_TEST_CASE(basic4_playing_ready_playing);
  ROB_ADD_TEST_CASE(basic5_playing_ready_playing);
  ROB_ADD_TEST_CASE(basic6_playing_ready_playing);
  ROB_ADD_TEST_CASE(basic7_playing_ready_playing);
  ROB_ADD_TEST_CASE(basic8_playing_ready_playing);
  ROB_ADD_TEST_CASE(basic9_playing_ready_playing);
  ROB_ADD_TEST_CASE(basic10_playing_ready_playing);
  ROB_ADD_TEST_CASE(basic11_playing_ready_playing);
  ROB_ADD_TEST_CASE(basic12_playing_ready_playing);
  ROB_ADD_TEST_CASE(basic13_playing_ready_playing);
  ROB_ADD_TEST_CASE(basic14_playing_ready_playing);
  ROB_ADD_TEST_CASE(basic15_playing_ready_playing);
  ROB_ADD_TEST_CASE(basic16_playing_ready_playing);
  ROB_ADD_TEST_CASE(basic17_playing_ready_playing);
  ROB_ADD_TEST_CASE(basic18_playing_ready_playing);

  ROB_ADD_TEST_CASE(basic0_LIVE);
  ROB_ADD_TEST_CASE(basic1_LIVE);
  ROB_ADD_TEST_CASE(basic2_LIVE);
  ROB_ADD_TEST_CASE(basic3_LIVE);
  ROB_ADD_TEST_CASE(basic4_LIVE);
  ROB_ADD_TEST_CASE(basic5_LIVE);
  ROB_ADD_TEST_CASE(basic6_LIVE);
  ROB_ADD_TEST_CASE(basic7_LIVE);
  ROB_ADD_TEST_CASE(basic8_LIVE);
  ROB_ADD_TEST_CASE(basic9_LIVE);
  ROB_ADD_TEST_CASE(basic10_LIVE);
  ROB_ADD_TEST_CASE(basic15_LIVE);
  ROB_ADD_TEST_CASE(basic16_LIVE);
  ROB_ADD_TEST_CASE(basic17_LIVE);
  ROB_ADD_TEST_CASE(basic18_LIVE);
  ROB_ADD_TEST_CASE(live_rob0);
  ROB_ADD_TEST_CASE(live_rob1);

  return s;
}

GST_CHECK_MAIN (rob_basic);


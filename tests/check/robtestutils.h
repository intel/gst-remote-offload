/*
 *  robtestutils.h - remoteoffloadbin test utilities
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
#ifndef __ROBTEST_UTILS_H__
#define __ROBTEST_UTILS_H__

#include <gst/gst.h>

typedef enum
{
   TESTROBPIPELINE_FLAG_NONE = 0,

   //Will transition pipeline from NULL->PLAYING,
   // wait for EOS, then transition from PLAYING->READY,
   // and then transition from READY->PLAYING once more.
   TESTROBPIPELINE_FLAG_PLAYING_READY_PLAYING = (1 << 1),

   //Each videotestsrc found in the pipeline will have
   // "is-live" property set to true.
   TESTROBPIPELINE_FLAG_VIDEOTESTSRC_FORCE_LIVE = (1 << 2),

   //Simply test that the pipeline doesn't crash or
   // hang. This flag is typically used to test
   // error handling capabilities. There's no
   // use in testing pipelines containing 'appsink'
   // elements, as output is not checked when this
   // flag is set.
   TESTROBPIPELINE_FLAG_NO_CRASH_OR_HANG = (1 << 3),

}TestROBPipelineFlags;

//Validate a gst-launch style string that contains
// one or more 'remoteoffloadbin.( )'. Wherever there
// exists an 'appsink' element (outside of the ROB),
// this represents a location in which the results will
// be compared with the same pipeline running natively
// on the host.
gboolean test_rob_pipeline(const gchar *pipeline_str,
                           TestROBPipelineFlags flags);

//Given two gst-launch style strings, run them both
// and verify that they generate identical output. They
// must contain the same number of appsink elements, of
// the same name.
gboolean test_pipelines_match(const gchar *pipeline_str0,
                              const gchar *pipeline_str1);


#define xstr(a) str(a)
#define str(a) #a
#define ROB_ADD_TEST_CASE(testname)       \
   TCase *tc_##testname = tcase_create(str(testname)); \
   tcase_add_test (tc_##testname, testname); \
   suite_add_tcase (s, tc_##testname); \
   tcase_set_timeout (tc_##testname, 15);



#endif /* __ROBTEST_UTILS_H__ */

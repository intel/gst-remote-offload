/*
 *  test_common.h - Common set of types / routines used across the test applications
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
#ifndef _GSTREMOTEOFFLOADTEST_COMMON_H_
#define _GSTREMOTEOFFLOADTEST_COMMON_H_

#include <stdio.h>
#include <string.h>

static int GetTestArgs(int argc, char *argv[], gchar **comms, gchar **commsparam)
{
  int i = 1;
  while(i < argc )
  {
    if( strcmp(argv[i], "-comms") == 0)
    {
       i++;
       if( i >= argc )
       {
         fprintf(stderr, "no matching argument for -comms\n");
         return -1;
       }

       *comms = argv[i];

       i++;
    }
    else
    if( strcmp(argv[i], "-commsparam") == 0)
    {
       i++;
       if( i >= argc )
       {
         fprintf(stderr, "no matching argument for -comms\n");
         return -1;
       }

       *commsparam = argv[i];

       i++;
    }
    else
    {
       i++;
    }
  }

  return 0;
}

#endif

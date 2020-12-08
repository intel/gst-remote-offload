/*
 *  gstrophddl.h - Remote Offload Pipeline entry-point for HDDL
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
#ifndef __GST_ROP_HDDL_H_
#define __GST_ROP_HDDL_H_

#include <functional>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

using ROPCallBack = std::function<void(const uint64_t pipelineId, void* userData)>;

/**
 * @brief launch a thread to run remote offload pipeline
 * @param pipelineId, the id of the pipeline
 * @param channels, the channel ids which the pipeline will use
 * @param callback, the callback function, invoked when the pipeline is finished
 * @return true if launch the pipeline successfully
*/
bool launchRemoteOffloadPipeline(uint64_t pipelineId, uint32_t sw_device_id, const std::vector<int32_t>& channels, const ROPCallBack& callback, void* userData);

/**
 * @brief stop the remote offload pipeline
 * @param pipelineId, the pipeline which need to be stopped
*/
void stopRemoteOffloadPipeline(uint64_t pipelineId);


#ifdef __cplusplus
}
#endif


#endif //__GST_ROP_HDDL_H_

/*
 *  gstrophddl.cpp - Remote Offload Pipeline implementation for HDDL
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
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <gst/gst.h>
#include "xlinkcommschannelcreator.h"
#include "gstremoteoffloadpipeline.h"
#include "remoteoffloadcommsio_xlink.h"

#include "gstrophddl.h"

GST_DEBUG_CATEGORY_STATIC (remote_offload_hddl_pipeline_launch_debug);
#define GST_CAT_DEFAULT remote_offload_hddl_pipeline_launch_debug

class HDDLRemoteOffloadPipeline {
public:
    using CallBack = std::function<void()>;
    using AutoMutex = std::lock_guard<std::mutex>;
    static HDDLRemoteOffloadPipeline* instance();
    bool launchPipeline(uint64_t pipelineId, uint32_t sw_device_id, const std::vector<int32_t>& channels, const ROPCallBack& callback, void* userData);
    void stopPipeline(uint64_t pipelineId);

    ~HDDLRemoteOffloadPipeline();

    HDDLRemoteOffloadPipeline(const HDDLRemoteOffloadPipeline&) = delete;
    HDDLRemoteOffloadPipeline(HDDLRemoteOffloadPipeline&&) = delete;
    HDDLRemoteOffloadPipeline& operator=(const HDDLRemoteOffloadPipeline&) = delete;
    HDDLRemoteOffloadPipeline& operator=(HDDLRemoteOffloadPipeline&&) = delete;

private:
    HDDLRemoteOffloadPipeline();

    bool waitForNeedStopPipeline(uint64_t& pipelineId, long milliseconds);
    void notifyNeedStopPipeline(uint64_t pipelineId);

    void startMonitorRoutine();
    void remoteOffloadPipelineRoutine(uint64_t pipelineId, uint32_t sw_device_id, const std::vector<int32_t>& channels);

    bool isPipelineNeedStop(uint64_t pipelineId);
    void setPipelineNeedStop(u_int64_t pipelineId);
    void removeFromPipelineNeedStop(u_int64_t pipelineId);
    bool hasRunningPipelines();

    std::condition_variable m_ready;
    std::mutex m_mutex;
    std::unordered_map<uint64_t, std::thread> m_pipelines;
    std::mutex m_needStopMutex;
    std::deque<uint64_t> m_needStopPipelines;
    std::unordered_map<uint64_t, CallBack> m_callbacks;

    std::thread m_monitorThread;
    std::atomic<bool> m_stop;

    static const long s_waitTimeOut = 1000;
};

HDDLRemoteOffloadPipeline::HDDLRemoteOffloadPipeline()
    : m_stop(false)
{
    gst_init(NULL, NULL);
    GST_DEBUG_CATEGORY_INIT (remote_offload_hddl_pipeline_launch_debug,
                               "remoteoffloadhddlpipelinelaunch", 0,
                             "debug category for HDDL ROP Launcher");

    m_monitorThread = std::thread(&HDDLRemoteOffloadPipeline::startMonitorRoutine, this);
}

HDDLRemoteOffloadPipeline::~HDDLRemoteOffloadPipeline()
{
    m_stop = true;
    if (m_monitorThread.joinable()) {
        m_monitorThread.join();
    }
}

HDDLRemoteOffloadPipeline* HDDLRemoteOffloadPipeline::instance()
{
    static HDDLRemoteOffloadPipeline pipeline;
    return &pipeline;
}

void HDDLRemoteOffloadPipeline::startMonitorRoutine()
{
    uint64_t pipelineId;
    while (!m_stop || hasRunningPipelines() > 0) {
        if (waitForNeedStopPipeline(pipelineId, s_waitTimeOut)) {
            stopPipeline(pipelineId);
        }
    }
}

bool HDDLRemoteOffloadPipeline::launchPipeline(const uint64_t pipelineId, uint32_t sw_device_id, const std::vector<int32_t>& channels, const ROPCallBack& callback, void* userData)
{
    AutoMutex autoLock(m_mutex);
    m_callbacks.emplace(std::make_pair(pipelineId, [=]() { callback(pipelineId, userData); }));
    auto pipelineThread = std::thread(&HDDLRemoteOffloadPipeline::remoteOffloadPipelineRoutine, this, pipelineId, sw_device_id, channels);
    m_pipelines.emplace(std::make_pair(pipelineId, std::move(pipelineThread)));
    return true;
}

void HDDLRemoteOffloadPipeline::stopPipeline(const uint64_t pipelineId)
{
    setPipelineNeedStop(pipelineId);

    AutoMutex autoLock(m_mutex);
    if (m_callbacks.find(pipelineId) != m_callbacks.end()) {
        m_callbacks[pipelineId]();
        m_callbacks.erase(pipelineId);
    }
    if (m_pipelines.find(pipelineId) != m_pipelines.end()) {
        if (m_pipelines[pipelineId].joinable()) {
            m_pipelines[pipelineId].join();
        }
        m_pipelines.erase(pipelineId);
    }
    removeFromPipelineNeedStop(pipelineId);
}

bool HDDLRemoteOffloadPipeline::isPipelineNeedStop(const uint64_t pipelineId)
{
    AutoMutex autoLock(m_needStopMutex);
    return std::find(m_needStopPipelines.begin(), m_needStopPipelines.end(), pipelineId) != m_needStopPipelines.end();
}

void HDDLRemoteOffloadPipeline::setPipelineNeedStop(const u_int64_t pipelineId)
{
    AutoMutex autoLock(m_needStopMutex);
    if (std::find(m_needStopPipelines.begin(), m_needStopPipelines.end(), pipelineId) == m_needStopPipelines.end()) {
        m_needStopPipelines.push_back(pipelineId);
    }
}

void HDDLRemoteOffloadPipeline::removeFromPipelineNeedStop(u_int64_t pipelineId)
{
    AutoMutex autoLock(m_needStopMutex);
    auto it = std::find(m_needStopPipelines.begin(), m_needStopPipelines.end(), pipelineId);
    if (it != m_needStopPipelines.end()) {
        m_needStopPipelines.erase(it);
    }
}

bool HDDLRemoteOffloadPipeline::hasRunningPipelines()
{
    AutoMutex autoLock(m_mutex);
    return !m_pipelines.empty();
}

void HDDLRemoteOffloadPipeline::remoteOffloadPipelineRoutine(const uint64_t pipelineId, uint32_t swDeviceId, const std::vector<int32_t>& channels)
{
    std::stringstream stream;
    for (auto channel : channels) {
        stream << ' ' << channel;
    }

    GST_INFO("pipelineId= %" G_GUINT64_FORMAT ", swDeviceId=0x%x, channels = %s",
                pipelineId,
                swDeviceId,
                stream.str().c_str());

    if( swDeviceId )
    {
       //Given swDeviceId, we know what device family we are
       // running on (i.e. KMB right now).
       const SWDeviceIdFamilyType device_family =
             GetFamilyFromSWDeviceId(swDeviceId);

       //Sanity check on family
       gboolean bfamily_error = FALSE;
       switch( device_family )
       {
          case SW_DEVICE_ID_KMB_FAMILY:
             GST_INFO("Device Family = KMB");
          break;

          default:
          GST_ERROR("Unsupported device family: 0x%x", device_family);
          bfamily_error = TRUE;
          break;
       }

       if( !bfamily_error )
       {
          std::vector<xlink_channel_id_t> vxlink_channels;
          for( auto &channel : channels )
          {
             vxlink_channels.push_back((xlink_channel_id_t)channel);
          }

          GHashTable *id_to_commschannel_hash =
                    XLinkCommsChannelsCreate(vxlink_channels, swDeviceId);

          if( id_to_commschannel_hash )
          {
              RemoteOffloadDevice *device = NULL;

              RemoteOffloadPipeline *pPipeline =
                   remote_offload_pipeline_new(device,
                                               id_to_commschannel_hash);

              if( pPipeline )
              {
                 GST_INFO("Running Remote Offload Pipeline Instance");

                 if( !remote_offload_pipeline_run(pPipeline) )
                 {
                    GST_ERROR("remote_offload_pipeline_run failed");
                 }

                 GST_INFO("Remote Offload Pipeline Instance Completed");

                 g_object_unref(pPipeline);
              }
              else
              {
                 GST_ERROR("Error in remote_offload_pipeline_new");
              }

              g_hash_table_unref(id_to_commschannel_hash);

              if( device )
                 g_object_unref(device);
          }
          else
          {
              GST_ERROR("Error in XLinkCommsChannelsCreate()");
          }
       }
    }
    else
    {
       GST_ERROR("GetDefaultSwDeviceID failed!");
    }

    //inform of stop here?
    notifyNeedStopPipeline(pipelineId);

}

bool HDDLRemoteOffloadPipeline::waitForNeedStopPipeline(uint64_t& pipelineId, long milliseconds)
{
    std::unique_lock<std::mutex> lock(m_needStopMutex);
    if (m_ready.wait_for(lock, std::chrono::milliseconds(milliseconds), [&]() { return !m_needStopPipelines.empty(); })) {
        pipelineId = m_needStopPipelines.front();
        return true;
    } else {
        return false;
    }
}

void HDDLRemoteOffloadPipeline::notifyNeedStopPipeline(const uint64_t pipelineId)
{
    setPipelineNeedStop(pipelineId);
    m_ready.notify_one();
}

bool launchRemoteOffloadPipeline(const uint64_t pipelineId, uint32_t sw_device_id, const std::vector<int32_t>& channels, const ROPCallBack& callback, void* userData)
{
    return HDDLRemoteOffloadPipeline::instance()->launchPipeline(pipelineId, sw_device_id, channels, callback, userData);
}

void stopRemoteOffloadPipeline(const uint64_t pipelineId)
{
    HDDLRemoteOffloadPipeline::instance()->stopPipeline(pipelineId);
}

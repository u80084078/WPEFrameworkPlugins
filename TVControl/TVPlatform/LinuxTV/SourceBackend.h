/*
 * Copyright (C) 2017 TATA ELXSI
 * Copyright (C) 2017 Metrological
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SOURCE_BACKEND_H_
#define SOURCE_BACKEND_H_

#include "TVCommon.h"
#include <interfaces/ITVPlatform.h>

#include <tracing/tracing.h>

#include <glib.h>
#include <gst/gst.h>
#include <gst/mpegts/mpegts.h>
#include <set>

#define BUFFER_SIZE 4096
#define DATA_OFFSET 6
#define SIZE_OFFSET 4

namespace LinuxDVB {
class TvControlBackend;
class SourceBackend;

struct GstPlayBackData {
    GstElement* pipeline;
    GstElement* dvbSource;
    GstElement* decoder;
    GstElement* audioQueue;
    GstElement* videoQueue;
    GstElement* audioConvert;
    GstElement* videoConvert;
    GstElement* audioSink;
    GstElement* videoSink;
    GMainLoop* loop;
    GstBus* bus;
};

struct GstFilteringData {
    GstElement* pipeline;
    GstElement* dvbSource;
    GstElement* demux;
    GMainLoop* loop;
    GstBus* bus;
    uint32_t frequency;
    GstMpegtsSectionType section;
    TVPlatform::ITVPlatform::ISectionHandler* sectionHandler;
};


class SourceBackend {
public:
    SourceBackend(fe_delivery_system_t, TunerData*);
    ~SourceBackend();

    TvmRc StartScanning(std::vector<uint32_t>, TVPlatform::ITVPlatform::ITunerHandler&);
    TvmRc StopScanning();
    TvmRc SetHomeTS(uint32_t);
    TvmRc Tune(uint32_t, uint16_t, uint16_t, TVPlatform::ITVPlatform::ITunerHandler&);
    TvmRc StartFilter(uint16_t, TVPlatform::ITVPlatform::ISectionHandler*);
    TvmRc StopFilter(uint16_t);
    TvmRc StopFilters();
    TvmRc GetChannelMap(ChannelMap&);
    TvmRc GetTSInfo(TSInfoList&);
    bool IsScanning() { return _isScanInProgress; }
    std::vector<uint32_t>& GetFrequencyList();
    void UpdateTunerCount(uint32_t tunerCount) { _tunerCount = tunerCount; }
    static void OnPadAdded(GstElement*, GstPad*, GstPlayBackData*);
    static void OnBusMessage (GstBus*, GstMessage*, GstFilteringData*);
    static AtscPSI _psiData;

private:
    bool StartPlayBack(uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, TVPlatform::ITVPlatform::ITunerHandler&);
    bool StopPlayBack();
    void SectionFilterThread();
    void ScanningThread(std::vector<uint32_t>, TVPlatform::ITVPlatform::ITunerHandler&);
    TvmRc SetCurrentChannel(uint32_t, uint16_t, uint16_t, TVPlatform::ITVPlatform::ITunerHandler&);
    void SetCurrentChannelThread(uint32_t, uint16_t, uint16_t, TVPlatform::ITVPlatform::ITunerHandler&);
    void ResumeFiltering();
    bool PauseFiltering();
    bool GetStreamInfo(uint32_t, uint16_t, AtscStream&);
    bool FilteringInitialization();
    bool PlayBackInitialization();
    bool StartFiltering(uint32_t, string, GstMpegtsSectionType);

private:
    fe_delivery_system_t _sType;
    TunerData* _tunerData;
    int32_t _adapter;

    volatile bool _isScanStopped;
    volatile bool _isRunning;
    volatile bool _isScanInProgress;

    uint64_t _channelNo;

    std::mutex _channelChangeCompleteMutex;
    std::condition_variable_any _channelChangeCompleteCondition;
    TvmRc _channelChangeState;

    std::mutex _scanCompleteMutex;
    std::condition_variable_any _scanCompleteCondition;

    std::mutex _sectionFilterMutex;
    std::condition_variable_any _sectionFilterCondition;

    std::vector<struct pollfd> _pollFds;
    std::map<uint16_t, uint32_t> _pollFdsMap;
    TVPlatform::ITVPlatform::ISectionHandler* _sectionHandler;
    DVBInfo _feInfo;
    std::vector<uint32_t> _frequencyList;
    uint32_t _currentTunedFrequency;
    uint32_t _tunerCount;
    bool _playbackInProgress;

    GstPlayBackData _gstPlayBackData;
    GstFilteringData _gstFilteringData;
    bool _isGstreamerPlayBackInitialized;
    bool _isGstreamerFilteringInitialized;
    uint64_t _tuningTimeout;
    std::set<uint16_t> _pidSet;
    bool _isTunerUsed;
};

} // namespace LinuxDVB
#endif // SOURCE_BACKEND_H_

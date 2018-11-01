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
#include "Module.h"
#include "SourceBackend.h"

#define MICROSEC_TO_NANOSEC 1000000

using namespace WPEFramework;

namespace LinuxDVB {

AtscPSI SourceBackend::_psiData;

SourceBackend::SourceBackend(fe_delivery_system_t type, TunerData* tunerData)
    : _sType(type)
    , _tunerData(tunerData)
    , _isScanStopped(false)
    , _isScanInProgress(false)
    , _isRunning(true)
    , _currentTunedFrequency(0)
    , _tunerCount(0)
    , _isGstreamerPlayBackInitialized(false)
    , _isGstreamerFilteringInitialized(false)
    , _sectionHandler(nullptr)
    , _isTunerUsed(false)
{
    _adapter = _tunerData->adapter;

    if (!gst_init_check(nullptr, nullptr, nullptr)) {
        TRACE(Trace::Error, (_T("Gstreamer initialization failed.")));
        return;
    }
    _isGstreamerFilteringInitialized = FilteringInitialization();
    _tuningTimeout = 1000 * MICROSEC_TO_NANOSEC;
}

SourceBackend::~SourceBackend()
{
    TRACE(Trace::Information, (_T("~SourceBackend")));
    _isRunning = false;
    _isScanInProgress = false;
    _psiData.clear();
    if (_isGstreamerPlayBackInitialized) {
        gst_object_unref(GST_OBJECT(_gstPlayBackData.pipeline));
        if (_tunerCount == 1)
            gst_object_unref(GST_OBJECT(_gstPlayBackData.bus));
    }
    if (_isGstreamerFilteringInitialized) {
        gst_object_unref(GST_OBJECT(_gstFilteringData.pipeline));
        gst_object_unref(GST_OBJECT(_gstFilteringData.bus));
    }
}

void SourceBackend::OnBusMessage(GstBus* bus, GstMessage* message, GstFilteringData* data)
{
    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
        GError* err;
        gchar* debug;

        gst_message_parse_error(message, &err, &debug);
        g_print ("Error: %s\n", err->message);
        g_error_free(err);
        g_free(debug);
        g_main_loop_quit(data->loop);
        break;
    }
    case GST_MESSAGE_EOS: {
        g_main_loop_quit(data->loop);
        break;
    }
    case GST_MESSAGE_ELEMENT: {
        GstMpegtsSection* section;
        if ((section = gst_message_parse_mpegts_section(message))) {

            switch (GST_MPEGTS_SECTION_TYPE(section)) {
            case GST_MPEGTS_SECTION_PAT: {
                if (data->section == GST_MPEGTS_SECTION_PAT) {
                    GPtrArray* pat = gst_mpegts_section_get_pat(section);
                    uint32_t len = pat->len;

                    AtscPmt pmt;
                    for (uint32_t i = 0; i < len; i++) {
                        AtscStream stream;
                        GstMpegtsPatProgram* patp = (GstMpegtsPatProgram*)g_ptr_array_index(pat, i);
                        stream.pmtPid = patp->network_or_program_map_PID;
                        pmt[patp->program_number] = stream;

                    }
                    _psiData[data->frequency] = pmt;
                    g_ptr_array_unref(pat);
                    gst_mpegts_section_unref(section);
                    g_main_loop_quit(data->loop);
                }
                break;
            }
            case GST_MPEGTS_SECTION_PMT: {
                const GstMpegtsPMT* pmt = gst_mpegts_section_get_pmt(section);

                uint32_t len = pmt->streams->len;
                for (uint32_t i = 0; i < len; i++) {
                    GstMpegtsPMTStream* stream = (GstMpegtsPMTStream*)g_ptr_array_index(pmt->streams, i);
                    if (stream->stream_type == 0x02)
                        _psiData[data->frequency][section->subtable_extension].videoPid = stream->pid;
                    std::string audioLan;
                    for (uint32_t j = 0; j < stream->descriptors->len; j++) {
                        GstMpegtsDescriptor* desc = (GstMpegtsDescriptor*)g_ptr_array_index(stream->descriptors, j);
                        if (desc) {
                            switch (desc->tag) {
                            case GST_MTS_DESC_ISO_639_LANGUAGE:
                                GstMpegtsISO639LanguageDescriptor* res;

                                if (gst_mpegts_descriptor_parse_iso_639_language(desc, &res)) {
                                    for (uint32_t k = 0; k < res->nb_language; k++) {
                                        audioLan = "";
                                        audioLan = res->language[k];
                                        if (stream->stream_type == 0x81 && audioLan == "eng")
                                            _psiData[data->frequency][section->subtable_extension].audioPid = stream->pid;

                                    }
                                    gst_mpegts_iso_639_language_descriptor_free(res);
                                }
                                break;
                            default:
                                break;
                            }
                        }
                    }
                    if (stream->stream_type == 0x81 && (!audioLan.size() || !_psiData[data->frequency][section->subtable_extension].audioPid))
                        _psiData[data->frequency][section->subtable_extension].audioPid = stream->pid;
                }
                gst_mpegts_section_unref(section);
                g_main_loop_quit(data->loop);
                break;
            }
            case GST_MPEGTS_SECTION_ATSC_MGT:
            case GST_MPEGTS_SECTION_ATSC_STT:
            case GST_MPEGTS_SECTION_ATSC_TVCT:
            case GST_MPEGTS_SECTION_ATSC_EIT: {
                uint8_t siBuf[BUFFER_SIZE];
                gsize data_size;
                if (section->data) {
                    memset(siBuf, 0, sizeof(siBuf));
                    memcpy(siBuf + DATA_OFFSET, (uint8_t*)section, sizeof(siBuf));
                    memcpy(siBuf, &data->frequency, 4);
                    memcpy(siBuf + SIZE_OFFSET, &(section->section_length), 2);
                    data->sectionHandler->SectionDataCB(std::string((const char*)siBuf, BUFFER_SIZE));
                }
                break;
            }
            default:
                break;
            }
        }
        break;
    }
    default:
        break;
    }
}

void SourceBackend::OnPadAdded(GstElement* element, GstPad* pad, GstPlayBackData* data)
{
    const gchar* newPadType = gst_structure_get_name(gst_caps_get_structure(gst_pad_query_caps(pad, nullptr), 0));
    GstPad* sinkPad = nullptr;
    if (g_str_has_prefix(newPadType, "audio/x-raw"))
        sinkPad = gst_element_get_static_pad(data->audioQueue, "sink");
    else if (g_str_has_prefix(newPadType, "video/x-raw"))
        sinkPad = gst_element_get_static_pad(data->videoQueue, "sink");

    if (!gst_pad_is_linked(sinkPad)) {
        g_print("Dynamic pad created, linking audioQueue/videoQueue\n");
        GstPadLinkReturn ret = gst_pad_link(pad, sinkPad);
        if (GST_PAD_LINK_FAILED(ret))
            g_print ("Link failed.\n");
        else
            g_print ("Link succeeded.\n");
    }
    gst_object_unref(sinkPad);
}

void SourceBackend::SectionFilterThread()
{
    std::string pids;
    for (auto& pid : _pidSet)
        pids += (std::to_string(pid) + ":");

    StartFiltering(_currentTunedFrequency, pids, GST_MPEGTS_SECTION_UNKNOWN);

    g_main_loop_run(_gstFilteringData.loop);
    gst_element_set_state(_gstFilteringData.pipeline, GST_STATE_NULL);
    _sectionFilterMutex.lock();
    _sectionFilterCondition.notify_all();
    _sectionFilterMutex.unlock();
}

TvmRc SourceBackend::SetHomeTS(uint32_t frequency)
{
    TRACE(Trace::Information, (_T("SetHomeTS")));
    if (frequency != _currentTunedFrequency)
        StopFilters();

    _currentTunedFrequency = frequency;
    return TvmSuccess;
}

TvmRc SourceBackend::StopFilter(uint16_t pid)
{
    _pidSet.erase(_pidSet.find(pid));
    if (g_main_loop_is_running(_gstFilteringData.loop))
        g_main_loop_quit (_gstFilteringData.loop);

    if (!_isScanInProgress && !_playbackInProgress && !((_tunerCount == 1) && _isTunerUsed)) {
        std::thread sectionFilterThread(&SourceBackend::SectionFilterThread, this);
        sectionFilterThread.detach();
    }
}

TvmRc SourceBackend::StopFilters()
{
    _pidSet.clear();
    if (g_main_loop_is_running(_gstFilteringData.loop))
        g_main_loop_quit(_gstFilteringData.loop);
}

TvmRc SourceBackend::Tune(uint32_t frequency, uint16_t programNumber, uint16_t modulation,  TVPlatform::ITVPlatform::ITunerHandler& tunerHandler)
{
    return SetCurrentChannel(frequency, programNumber, modulation, tunerHandler);
}

TvmRc SourceBackend::StartFilter(uint16_t pid, TVPlatform::ITVPlatform::ISectionHandler* pSectionHandler)
{
    if (_pidSet.find(pid) != _pidSet.end()) {
        TRACE(Trace::Information, (_T("PID already exists")));
        return TvmError;
    }
    _pidSet.insert(pid);
    if (!_sectionHandler)
        _sectionHandler = pSectionHandler;

    if (g_main_loop_is_running(_gstFilteringData.loop)) {
        g_main_loop_quit (_gstFilteringData.loop);
        _sectionFilterMutex.lock();
        _sectionFilterCondition.wait(_sectionFilterMutex);
        _sectionFilterMutex.unlock();
    }

    if (!_isScanInProgress && !_playbackInProgress && !((_tunerCount == 1) && _isTunerUsed)) {
        std::thread sectionFilterThread(&SourceBackend::SectionFilterThread, this);
        sectionFilterThread.detach();
    }
}

TvmRc SourceBackend::StartScanning(std::vector<uint32_t> freqList, TVPlatform::ITVPlatform::ITunerHandler& tunerHandler)
{
    TRACE(Trace::Information, (string(__FUNCTION__)));
    TvmRc ret = TvmError;
    std::thread th(&SourceBackend::ScanningThread, this, freqList, std::ref(tunerHandler));
    th.detach();
    return TvmSuccess;
}

TvmRc SourceBackend::GetChannelMap(ChannelMap& chanMap)
{
    TvmRc rc = TvmSuccess;
    for (auto &psiInfo : _psiData) {
        AtscPmt pmt = psiInfo.second;
        for (auto& pmtInfo : pmt) {
            ChannelDetails chan;
            chan.frequency = psiInfo.first;
            chan.programNumber = pmtInfo.first;
            if (pmtInfo.second.videoPid)
                chan.type = ChannelDetails::Normal;
            else if (pmtInfo.second.audioPid)
                chan.type = ChannelDetails::Radio;
            else
                chan.type = ChannelDetails::Data;
            if (chan.type != ChannelDetails::Data)
                chanMap.push_back(chan);
        }
    }
    return rc;
}

std::vector<uint32_t>& SourceBackend::GetFrequencyList()
{
    return _frequencyList;
}

void SourceBackend::ResumeFiltering()
{
    TRACE(Trace::Information, (string(__FUNCTION__)));
    if (!_pidSet.size())
        return;

    if (_currentTunedFrequency && !_playbackInProgress)
        SetHomeTS(_currentTunedFrequency); //Resetting to the last tuned channel after scan.

    _isTunerUsed = false;
    std::thread sectionFilterThread(&SourceBackend::SectionFilterThread, this);
    sectionFilterThread.detach();
    TRACE(Trace::Information, (string(__FUNCTION__)));
}

bool SourceBackend::PauseFiltering()
{
    TRACE(Trace::Information, (string(__FUNCTION__)));
    if (!_pidSet.size())
        return false;

    _isTunerUsed = true;
    if (g_main_loop_is_running(_gstFilteringData.loop))
        g_main_loop_quit(_gstFilteringData.loop);

    return true;
}

void SourceBackend::ScanningThread(std::vector<uint32_t> freqList, TVPlatform::ITVPlatform::ITunerHandler& tunerHandler)
{
    TRACE(Trace::Information, (string(__FUNCTION__)));
    _isScanInProgress = true;

    if (_psiData.size())
        _psiData.clear();

    PauseFiltering();

    if (_playbackInProgress) {
        StopPlayBack();
        _channelNo = 0;
    }
    TRACE(Trace::Information, (string(__FUNCTION__)));
    _frequencyList.clear();

    std::vector<uint32_t> frequencyList;
    if (freqList.size())
        frequencyList = freqList;
    else
        frequencyList = _tunerData->frequency;

    for (auto& frequency : frequencyList) {
        string pids = std::to_string(PidPat);
        if (StartFiltering(frequency, pids, GST_MPEGTS_SECTION_PAT)) {
            g_main_loop_run(_gstFilteringData.loop);
            gst_element_set_state(_gstFilteringData.pipeline, GST_STATE_NULL);
            if (_psiData[frequency].size())
                _frequencyList.push_back(frequency);

            for (auto& program : _psiData[frequency]) {
                pids = std::to_string(program.second.pmtPid);
                if (StartFiltering(frequency, pids, GST_MPEGTS_SECTION_PMT))
                    g_main_loop_run(_gstFilteringData.loop);
                    gst_element_set_state(_gstFilteringData.pipeline, GST_STATE_NULL);
            }
        }
        if (_isScanStopped) {
            tunerHandler.ScanningStateChanged(Stopped);
            break;
        }
    }
    if (_isScanStopped)
        _isScanStopped = false;
    else
        tunerHandler.ScanningStateChanged(Completed);

    TRACE(Trace::Information, (string(__FUNCTION__)));
    _isScanInProgress = false;
    ResumeFiltering();
}

TvmRc SourceBackend::StopScanning()
{
    _isScanStopped = true;
    return TvmSuccess;
}

bool SourceBackend::StartPlayBack(uint32_t frequency, uint32_t modulation, uint16_t pmtPid, uint16_t videoPid, uint16_t audioPid, TVPlatform::ITVPlatform::ITunerHandler& tunerHandler)
{
    TRACE(Trace::Information, (string(__FUNCTION__)));
    if (!_isGstreamerPlayBackInitialized) {
        if(!(_isGstreamerPlayBackInitialized = PlayBackInitialization())) {
            TRACE(Trace::Error, (_T("Gstreamer not initialized.")));
            return false;
        }
    }

    if ((!pmtPid)&& (!videoPid) && (!audioPid)) {
        TRACE(Trace::Error, (_T("Invalid pid cannot do playback")));
        return false;
    }

    std::string pidSet = std::to_string(pmtPid) + ":" + std::to_string(videoPid) + ":" + std::to_string(audioPid);
    if (_tunerCount == 1) {
        for (auto& pid : _pidSet)
            pidSet += (":" + std::to_string(pid));
    }
    g_object_set(G_OBJECT(_gstPlayBackData.dvbSource), "frequency", frequency,
        "adapter", _adapter,
        "delsys", _sType,
        "modulation", _tunerData->modulation,
        "pids", pidSet.c_str(), nullptr);

    TRACE(Trace::Information, (_T("Starting playback.")));

    if (_tunerCount == 1) {
        _gstFilteringData.section = GST_MPEGTS_SECTION_UNKNOWN;
        _gstFilteringData.frequency = frequency;
        _gstFilteringData.sectionHandler = _sectionHandler;
        if (_currentTunedFrequency != frequency) {
            _currentTunedFrequency = frequency;
            StopFilters();
            tunerHandler.StreamingFrequencyChanged(_currentTunedFrequency);
        }
    }

    _channelChangeState = TvmSuccess;
    _channelChangeCompleteMutex.lock();
    _channelChangeCompleteCondition.notify_all();
    _channelChangeCompleteMutex.unlock();

    _playbackInProgress = true;
    gst_element_set_state(_gstPlayBackData.pipeline, GST_STATE_PLAYING);
    g_main_loop_run(_gstPlayBackData.loop);
    gst_element_set_state(_gstPlayBackData.pipeline, GST_STATE_NULL);
    return true;
}

bool SourceBackend::StopPlayBack()
{
    if (!_isGstreamerPlayBackInitialized) {
        TRACE(Trace::Error, (_T("Gstreamer not initialized.")));
        return false;
    }
    TRACE(Trace::Information, (_T("Stopping playback.")));
    if (g_main_loop_is_running(_gstPlayBackData.loop))
        g_main_loop_quit(_gstPlayBackData.loop);
    _playbackInProgress = false;
    return true;
}

TvmRc SourceBackend::SetCurrentChannel(uint32_t frequency, uint16_t programNumber, uint16_t modulation, TVPlatform::ITVPlatform::ITunerHandler& tunerHandler)
{
    TRACE(Trace::Information, (_T("Set Channel invoked")));
    TvmRc ret = TvmError;
    _channelChangeCompleteMutex.lock();
    _channelChangeState = TvmError;
    _channelChangeCompleteMutex.unlock();
    if (_channelNo !=  programNumber) {
        _channelNo = programNumber;
        PauseFiltering();

        if (_playbackInProgress)
            StopPlayBack();

        std::thread th(&SourceBackend::SetCurrentChannelThread, this, frequency, programNumber, modulation, std::ref(tunerHandler));
        th.detach();
        _channelChangeCompleteMutex.lock();
        _channelChangeCompleteCondition.wait(_channelChangeCompleteMutex);
        _channelChangeCompleteMutex.unlock();
    }
    return _channelChangeState;
}

bool SourceBackend::GetStreamInfo(uint32_t frequency, uint16_t programNumber, AtscStream& stream)
{
    bool bFound = false;
    auto it = _psiData.find(frequency);
    if (it != _psiData.end()) {
        TRACE(Trace::Error, (_T("%s: TS found for %u Hz"), __FUNCTION__, frequency));
        AtscPmt& pmt = it->second;
        auto itPMT = pmt.find(programNumber);
        if (itPMT != pmt.end()) {
            TRACE(Trace::Error, (_T("%s: Program Number %d found"), __FUNCTION__, programNumber));
            stream.pmtPid = itPMT->second.pmtPid;
            stream.videoPid = itPMT->second.videoPid;
            stream.audioPid = itPMT->second.audioPid;
            bFound = true;

        } else
            TRACE(Trace::Error, (_T("%s: Program Number %d not found"), __FUNCTION__, programNumber));
    } else
        TRACE(Trace::Error, (_T("%s: TS Not found for %u Hz"), __FUNCTION__, frequency));
    return bFound;
}

void SourceBackend::SetCurrentChannelThread(uint32_t frequency, uint16_t programNumber, uint16_t modulation, TVPlatform::ITVPlatform::ITunerHandler& tunerHandler)
{
    TRACE(Trace::Information, (_T("Tune to Channel(program number):: %" PRIu64 ""), _channelNo));

    AtscStream stream;
    if (GetStreamInfo(frequency, programNumber, stream))
        StartPlayBack(frequency, modulation, stream.pmtPid, stream.videoPid, stream.audioPid, tunerHandler);
}

TvmRc SourceBackend::GetTSInfo(TSInfoList& tsInfoList)
{
    TRACE(Trace::Information, (string(__FUNCTION__)));
    TvmRc rc = TvmSuccess;
    for (auto &psiInfo : _psiData) {
        AtscPmt pmt = psiInfo.second;
        for (auto& pmtInfo : pmt) {
            TSInfo tsInfo{};
            tsInfo.frequency = psiInfo.first;
            tsInfo.programNumber = pmtInfo.first;
            tsInfo.audioPid = pmtInfo.second.audioPid;
            tsInfo.videoPid = pmtInfo.second.videoPid;
            tsInfo.pmtPid = pmtInfo.second.pmtPid;
            if (tsInfo.audioPid || tsInfo.videoPid) {
                tsInfoList.push_back(tsInfo);

                TRACE(Trace::Information, (_T("LCN:%" PRIu16 ", pmtPid:%" PRIu16 ", videoPid:%" PRIu16 ", audioPid:%" PRIu16 " \
                    frequency : %u"), tsInfo.programNumber,
                    tsInfo.pmtPid, tsInfo.videoPid, tsInfo.audioPid, tsInfo.frequency));
            }
        }
    }
    return rc;
}

bool SourceBackend::PlayBackInitialization()
{
    _gstPlayBackData.pipeline = gst_pipeline_new("pipeline");
    _gstPlayBackData.dvbSource = gst_element_factory_make("dvbsrc", "source");
    _gstPlayBackData.decoder = gst_element_factory_make("decodebin", "decoder");
    _gstPlayBackData.audioQueue = gst_element_factory_make("queue","audioqueue");
    _gstPlayBackData.videoQueue = gst_element_factory_make("queue","videoqueue");
    _gstPlayBackData.videoConvert = gst_element_factory_make("videoconvert", "videoconv");
    _gstPlayBackData.audioConvert = gst_element_factory_make("audioconvert", "audioconv");
    _gstPlayBackData.videoSink = gst_element_factory_make("autovideosink", "videosink");
    _gstPlayBackData.audioSink = gst_element_factory_make("autoaudiosink", "audiosink");
    _gstPlayBackData.loop = g_main_loop_new(nullptr, FALSE);

    if (!_gstPlayBackData.pipeline || !_gstPlayBackData.dvbSource || !_gstPlayBackData.decoder || !_gstPlayBackData.audioQueue || !_gstPlayBackData.videoQueue || !_gstPlayBackData.videoConvert
        || !_gstPlayBackData.audioConvert || !_gstPlayBackData.videoSink || !_gstPlayBackData.audioSink) {
        TRACE(Trace::Error, (_T("One gstreamer element could not be created.")));
        return false;
    }
    gst_bin_add_many(GST_BIN(_gstPlayBackData.pipeline), _gstPlayBackData.dvbSource, _gstPlayBackData.decoder, _gstPlayBackData.audioQueue, _gstPlayBackData.audioConvert, _gstPlayBackData.audioSink
        , _gstPlayBackData.videoQueue, _gstPlayBackData.videoConvert, _gstPlayBackData.videoSink, nullptr);

    if (!gst_element_link(_gstPlayBackData.dvbSource, _gstPlayBackData.decoder) || !gst_element_link_many(_gstPlayBackData.audioQueue, _gstPlayBackData.audioConvert, _gstPlayBackData.audioSink, nullptr)
        || !gst_element_link_many(_gstPlayBackData.videoQueue, _gstPlayBackData.videoConvert, _gstPlayBackData.videoSink, nullptr)) {
        TRACE(Trace::Error, (_T("Gstreamer link error.")));
        return false;
    }

    if (_tunerCount == 1) {
        _gstPlayBackData.bus = gst_pipeline_get_bus(GST_PIPELINE (_gstPlayBackData.pipeline));
        gst_bus_add_signal_watch(_gstPlayBackData.bus);
        if (g_signal_connect(_gstPlayBackData.bus, "message", (GCallback)OnBusMessage, &_gstFilteringData))
            TRACE(Trace::Information, (_T("GStreamer connect success.")));
    }

    if (g_signal_connect(_gstPlayBackData.decoder, "pad-added", G_CALLBACK(OnPadAdded), &_gstPlayBackData) && g_signal_connect(_gstPlayBackData.decoder, "pad-added", G_CALLBACK(OnPadAdded), &_gstPlayBackData))
        return true;
    return false;
}

bool SourceBackend::FilteringInitialization()
{
    gst_mpegts_initialize ();
    _gstFilteringData.pipeline = gst_pipeline_new("pipeline");
    _gstFilteringData.dvbSource = gst_element_factory_make("dvbsrc", "source");
    _gstFilteringData.demux = gst_element_factory_make("tsdemux", "demux");
    _gstFilteringData.loop = g_main_loop_new(nullptr, FALSE);

    if (!_gstFilteringData.pipeline || !_gstFilteringData.dvbSource || !_gstFilteringData.demux) {
        TRACE(Trace::Error, (_T("One gstreamer element could not be created.")));
        return false;
    }
    gst_bin_add_many(GST_BIN(_gstFilteringData.pipeline), _gstFilteringData.dvbSource, _gstFilteringData.demux, nullptr);

    if (!gst_element_link(_gstFilteringData.dvbSource, _gstFilteringData.demux)) {
        TRACE(Trace::Error, (_T("GStreamer link error.")));
        return false;
    }

    _gstFilteringData.bus = gst_pipeline_get_bus(GST_PIPELINE(_gstFilteringData.pipeline));
    gst_bus_add_signal_watch(_gstFilteringData.bus);
    if (g_signal_connect(_gstFilteringData.bus, "message", (GCallback)OnBusMessage, &_gstFilteringData)) {
        TRACE(Trace::Information, (_T("GStreamer connect success.")));
        return true;
    }
    return false;
}

bool SourceBackend::StartFiltering(uint32_t frequency, string pids, GstMpegtsSectionType section)
{
    g_object_set(G_OBJECT(_gstFilteringData.dvbSource), "frequency", frequency,
            "adapter", _adapter,
            "delsys", _sType, // delivery system : atsc
            "modulation", _tunerData->modulation, // modulation : 8vsb
            "tuning-timeout", _tuningTimeout,
            "pids", pids.c_str(), nullptr);

    g_object_set (_gstFilteringData.demux, "emit-stats", TRUE, nullptr);

    _gstFilteringData.section = section;
    _gstFilteringData.frequency = frequency;
    _gstFilteringData.sectionHandler = _sectionHandler;
    gst_element_set_state(_gstFilteringData.pipeline, GST_STATE_PLAYING);
    return true;
}

} // namespace LinuxDVB

#include "Module.h"
#include "ParserATSC.h"

#include <errno.h>

#define ONEPART_CHANNEL_NUMBER_MASK 0X03FF0

using namespace WPEFramework;

ParserATSC::ParserATSC(ISIHandler* siHandler, uint32_t homeTS)
    : WPEFramework::Core::Thread(0, _T("SIControlParser"))
    , _epgDB(EPGDataBase::GetInstance())
    , _eitPidIndex(0)
    , _isTimeParsed(false)
    , _isMGTParsed(false)
    , _clientInitialised(false)
    , _siHandler(siHandler)
    , _freqHandler(this)
    , _currentParsingFrequency(0)
    , _homeTS(homeTS)
    , _parserRunning(false)
    , _isStreaming(false)
{
    TRACE(Trace::Information, (_T("Parser constructor is invoked")));
    _epgDB.CreateChannelTable();
    _epgDB.CreateProgramTable();
}

IParser* IParser::GetInstance(ISIHandler* siHandler, uint32_t homeTS)
{
    static ParserATSC* parserATSC = new ParserATSC(siHandler, homeTS);
    return parserATSC;
}

ISIHandler* ParserATSC::GetSIHandler()
{
    return _siHandler;
}

ParserATSC::~ParserATSC()
{
    TRACE(Trace::Information, (string(__FUNCTION__)));
    ReleaseFilters();
    TRACE(Trace::Information, (_T("Filters stopped")));

    Block();
    DataQueue::GetInstance().Clear();
    Wait(Thread::BLOCKED | Thread::STOPPED | Thread::STOPPING, Core::infinite);

    TRACE(Trace::Information, (_T("Queue cleared")));

   _clientInitialised = false;
    TRACE(Trace::Information, (_T("Destructor Completed")));
}

void ParserATSC::ReleaseFilters()
{
    GetSIHandler()->StopFilters();
}

void ParserATSC::SendBaseTableRequest()
{
    TRACE(Trace::Information, (_T("Requesting TVCT and MGT")));
    GetSIHandler()->StartFilter(ATSC_BASE_PID, 0);
}

void ParserATSC::UpdateCurrentFrequency(uint32_t currentFrequency)
{
    _isStreaming = true;
    SendBaseTableRequest();
}

void ParserATSC::ConfigureParser()
{
    if (_homeTS) {
        _parserRunning = true;
        GetSIHandler()->SetHomeTS(_homeTS, 0);
        SendBaseTableRequest();
    } else {
        if (_epgDB.ReadFrequency(_frequencyList)) {
            if (_frequencyList.size()) {
                _parserRunning = true;
                _freqHandler.SetFrequencyList(_frequencyList);
                _freqHandler.Run();
            }
        }
    }
}

void ParserATSC::UpdateFrequencyList()
{
    if (!_homeTS) {
        std::vector<uint32_t> previousFrequencyList;
        if (_frequencyList.size())
            previousFrequencyList = _frequencyList;

        _epgDB.ReadFrequency(_frequencyList);
        if (_frequencyList.size() && (previousFrequencyList != _frequencyList)) {
            if (_parserRunning) {
                _freqHandler.Stop();
                GetSIHandler()->StopFilters();
            }
            _parserRunning = true;
            _freqHandler.SetFrequencyList(_frequencyList);
            _freqHandler.Run();
        }
    }
}

uint32_t ParserATSC::Worker()
{
    TRACE(Trace::Information, (_T("Worker is invoked")));
    if (!_clientInitialised) {
        _clientInitialised = true;
        ConfigureParser();
    }

    while (IsRunning() == true) {
        TRACE(Trace::Information, (_T("Parser running = %d \n"), IsRunning()));
        std::pair<uint8_t*, uint16_t>  dataElement;
        dataElement = DataQueue::GetInstance().Pop();
        // FIXME:Finalising data frame format.
        TRACE(Trace::Information, (_T("Worker data obtained")));
        if ((IsRunning() == true) && std::get<0>(dataElement)) {
            GstMpegtsSection* section = reinterpret_cast<GstMpegtsSection*>(std::get<0>(dataElement) + DATA_OFFSET);
            TRACE(Trace::Information, (_T("DBS ATSC")));
            // Has to be modified after Finalising data frame format.
            uint32_t frequency = 0;
            memcpy(&frequency, std::get<0>(dataElement), sizeof(uint32_t));
            TRACE(Trace::Information, (_T("Frequency = %u\n"), frequency));
            ParseData(section, frequency);
            free(std::get<0>(dataElement));
        }
    }
    TRACE(Trace::Information, (_T("Worker thread to Block state")));
    return (WPEFramework::Core::infinite);
}

void ParserATSC::ParseData(GstMpegtsSection* section, uint32_t frequency)
{
    if (_currentParsingFrequency != frequency) {
        ResetTables();
        _currentParsingFrequency = frequency;
    }
    TRACE(Trace::Information, (_T("Parser")));

    if (!section) {
        TRACE(Trace::Error, (_T("Error : no section available.")));
        return;
    }

    uint8_t tableId = section->table_id;
    TRACE(Trace::Information, (_T("Table type =  %02X "), tableId));

    switch (GST_MPEGTS_SECTION_TYPE(section)) {
    case GST_MPEGTS_SECTION_ATSC_MGT:
        ParseMGT(section);
        if (_isTimeParsed)
            PushEitStartRequest();
        break;
    case GST_MPEGTS_SECTION_ATSC_TVCT:
    case GST_MPEGTS_SECTION_ATSC_CVCT:
        ParseVCT(section, frequency);
        break;
    case GST_MPEGTS_SECTION_ATSC_EIT:
        ParseEIT(section);
        break;
    case GST_MPEGTS_SECTION_ATSC_STT: {
        if (!_isTimeParsed)
            ParseSTT(section);
            if (_isMGTParsed)
                PushEitStartRequest();
        }
        break;
    default:
        TRACE(Trace::Information, (_T("SI Table type unknown")));
        break;
    }
}    

bool ParserATSC::ParseEIT(GstMpegtsSection* section)
{
    TRACE(Trace::Information, (_T("Parse EIT")));
    const GstMpegtsAtscEIT* eit = gst_mpegts_section_get_atsc_eit(section);
    if (!eit)
        return false;

    uint16_t sourceId = eit->source_id;
    if (_programMap.find(sourceId) == _programMap.end()) {
        for (uint8_t sectionNumber = 0; sectionNumber <= section->last_section_number; sectionNumber++)
            _programMap[sourceId].insert(sectionNumber);
    } else {
        if (_programMap[sourceId].empty())
            return true;
    }

    _programMap[sourceId].erase(section->section_number);

    if (!ParseEvents(eit, sourceId)) {
        TRACE(Trace::Error, (_T("error calling parse_events()")));
        return false;
    }

    if (IsEITParsingCompleted()) {
        GetSIHandler()->EitBroadcasted();
        TRACE(Trace::Information, (_T("Completed EIT Parsing ")));
        _eitPidIndex++;
        if (_eitPidIndex == _eitPidVector.size()) {
            if ((_frequencyList.size() > 1) && !_isStreaming)
                _freqHandler.Run();
        } else
            PushEitStartRequest();
        _programMap.clear();
    }
    return true;
}

void ParserATSC::PushEitStopRequest()
{
    if (_eitPidVector.size()) {
        TRACE(Trace::Information, (_T("PushEitStopRequest with pid =%d"), (_eitPidIndex)));
        GetSIHandler()->StopFilter(_eitPidVector[_eitPidIndex], 0/*Placeholder*/);
    }
}

void ParserATSC::PushEitStartRequest()
{
    if (_eitPidVector.size()) {
        TRACE(Trace::Information, (_T("PushEitStartRequest with pid =%d"), _eitPidIndex));
        _eitPidIndex %= _eitPidVector.size();
        GetSIHandler()->StartFilter(_eitPidVector[_eitPidIndex], 0/*Placeholder*/);
    }
}

bool ParserATSC::IsEITParsingCompleted()
{
    bool isCompleted = false;
    if (_programMap.size() == _channelSet.size()) {
        for (auto& sectionsList : _programMap) {
            if (!sectionsList.second.empty())
                return isCompleted;
        }
        isCompleted = true;
    }
    return isCompleted;
}

bool ParserATSC::ResetTables()
{
    bool ret = true;
    TRACE(Trace::Information, (_T("Reset Tables on Backend restart")));
    _eitPidIndex = 0;
    _eitPidVector.clear();
    _programMap.clear();
    _channelSet.clear();
    return ret;
}

bool ParserATSC::ParseSTT(GstMpegtsSection* section)
{
    TRACE(Trace::Information, (_T("Parse STT")));
    const GstMpegtsAtscSTT* stt = gst_mpegts_section_get_atsc_stt(section);
    if (!stt)
        return false;

    struct timeval timeVal = {};
    timeVal.tv_sec = atsctime_to_unixtime(stt->system_time - stt->gps_utc_offset);
    if (settimeofday(&timeVal, nullptr) < 0)
        TRACE(Trace::Error, (_T("Error in settimeofday, errMsg = %s"), strerror(errno)));
    _isTimeParsed = true;
    if (_frequencyList.size() > 1)
        _freqHandler.Run();

    GetSIHandler()->StartTimer();
    return true;
}

bool ParserATSC::ParseMGT(GstMpegtsSection* section)
{
    TRACE(Trace::Information, (_T("Parse MGT")));
    const GstMpegtsAtscMGT* mgt = gst_mpegts_section_get_atsc_mgt(section);
    if (!mgt)
        return false;

    for (uint32_t i = 0; i < mgt->tables->len; i++) {
        GstMpegtsAtscMGTTable* table = (GstMpegtsAtscMGTTable*)g_ptr_array_index(mgt->tables, i);

        if (table->table_type >= 0x0100 && table->table_type <= 0x017F) {
            TRACE(Trace::Information, (_T("EIT::  %2d: type = 0x%04X, PID = 0x%04X "), i, table->table_type, table->pid));
            if (std::find(_eitPidVector.begin(), _eitPidVector.end(), table->pid) == _eitPidVector.end())
                _eitPidVector.push_back(table->pid);
        }
    }
    _isMGTParsed = true;
    return true;
}

bool ParserATSC::ParseVCT(GstMpegtsSection* section, uint32_t frequency)
{
    TRACE(Trace::Information, (_T("Parse TVCT")));
    const GstMpegtsAtscVCT* vct;
    if (GST_MPEGTS_SECTION_TYPE(section) == GST_MPEGTS_SECTION_ATSC_CVCT)
        vct = gst_mpegts_section_get_atsc_cvct(section);
    else
        vct = gst_mpegts_section_get_atsc_tvct(section);

    if (!vct)
        return false;

    for (uint32_t i = 0; i < vct->sources->len; i++) {
        GstMpegtsAtscVCTSource* source = (GstMpegtsAtscVCTSource*)g_ptr_array_index(vct->sources, i);

        std::string logicalChannelNumber;
        logicalChannelNumber.assign(std::to_string(source->major_channel_number));
        logicalChannelNumber += ".";
        logicalChannelNumber += std::to_string(source->minor_channel_number);

        TRACE(Trace::Information, (_T("TSID : %d"), vct->transport_stream_id));
        TRACE(Trace::Information, (_T("ProgramNumber : %d"), source->program_number));
        TRACE(Trace::Information, (_T("LCN : %s"), logicalChannelNumber.c_str()));
        TRACE(Trace::Information, (_T("ServiceID : %d"), source->source_id));

        _channelSet.insert(source->source_id);
        TRACE(Trace::Information, (_T("Service Name : %s"), source->short_name));
        std::string language;
        PmtPidInfo pmtInfo;
        for (uint32_t j = 0; j < source->descriptors->len; j++) {
            GstMpegtsDescriptor* desc = (GstMpegtsDescriptor*)g_ptr_array_index(source->descriptors, j);
            if (desc) {
                switch (desc->tag) {
                case GST_MTS_DESC_ATSC_SERVICE_LOCATION:
                    ParseAtscServiceLocationDescriptor(pmtInfo, desc);
                    for (uint16_t audioIndex = 0; audioIndex < pmtInfo._audioNum; ++audioIndex) {
                        language += pmtInfo._audioLang[audioIndex];
                        if (audioIndex != pmtInfo._audioNum - 1)
                            language += ',';
                    }
                    break;
                default:
                    break;
                }
            }
        }

        if (source->program_number)
            _epgDB.InsertChannelInfo(frequency, source->modulation_mode, source->short_name, source->source_id, vct->transport_stream_id, 0
                , logicalChannelNumber, source->program_number, language);
    }
    return true;
}

void ParserATSC::ParseAtscServiceLocationDescriptor(PmtPidInfo& s, GstMpegtsDescriptor* desc)
{
    s._audioNum = 0;
    std::string language;
    gchar* lang;
    uint8_t streamType;
    uint16_t elementaryPid;

    for (uint32_t j = 0; gst_mpegts_descriptor_parse_atsc_service_location_idx(desc, j, &streamType, language, &elementaryPid); j++) {
        switch (streamType) {
        case 0x02: // Video.
            s._videoPid = elementaryPid;
            break;
        case 0x81: // Audio.
            if (s._audioNum < AUDIO_CHAN_MAX) {
                s._audioPid[s._audioNum] = elementaryPid;
                s._audioLang[s._audioNum] = language;
                s._audioNum++;
            }
            break;
        default:
            break;
        }
    }
}

bool ParserATSC::ParseEvents(const GstMpegtsAtscEIT* eit, uint8_t sourceId)
{
    time_t startTime, endTime;
    for (uint32_t i = 0; i < eit->events->len; i++) {
        GstMpegtsAtscEITEvent* event = (GstMpegtsAtscEITEvent*)g_ptr_array_index(eit->events, i);
        struct tm start;
        struct tm end;
        startTime = atsctime_to_unixtime(event->start_time);
        endTime = startTime + event->length_in_seconds;
        localtime_r(&startTime, &start);
        localtime_r(&endTime, &end);

        TRACE(Trace::Information, (_T("|%02d:%02d--%02d:%02d| "),
            start.tm_hour, start.tm_min,
            end.tm_hour, end.tm_min));

        std::string title = ATSCTextDecode(event->titles);
        TRACE(Trace::Information, (_T("title = %s"), title.c_str()));

        std::string rating, captionLanguage;
        for (uint32_t j = 0; j < event->descriptors->len; j++) {
            GstMpegtsDescriptor* desc = (GstMpegtsDescriptor*)g_ptr_array_index(event->descriptors, j);
            if (desc) {
                switch (desc->tag) {
                case GST_MTS_DESC_ATSC_CONTENT_ADVISORY:
                    rating = ParseAtscContentAdvisoryDescriptor(desc);
                    break;
                case GST_MTS_DESC_ATSC_CAPTION_SERVICE: {
                    captionLanguage = ParseAtscCaptionServiceDescriptor(desc);
                    break;
                }
                default:
                    break;
                }
            }
        }
        _epgDB.InsertProgramInfo(sourceId, event->event_id, startTime, event->length_in_seconds, title.c_str(), rating, captionLanguage.size() ? captionLanguage.c_str() : "", "", "");
        captionLanguage = "";

    }
    return true;
}

std::string ParserATSC::ParseAtscCaptionServiceDescriptor(GstMpegtsDescriptor* desc)
{
    std::string language;
    for (uint32_t i = 0; gst_mpegts_descriptor_parse_atsc_caption_service_idx(desc, i, language); i++);
    return language;
}

std::string ParserATSC::ATSCTextDecode(GPtrArray* mstrings)
{
    std::string returnText;

    for (uint32_t i = 0; i < mstrings->len; i++) {
        GstMpegtsAtscMultString* mstring = (GstMpegtsAtscMultString*)g_ptr_array_index(mstrings, i);
        uint32_t n = mstring->segments->len;

        for (uint32_t j = 0; j < n; j++) {
            GstMpegtsAtscStringSegment* segment = (GstMpegtsAtscStringSegment*)g_ptr_array_index(mstring->segments, j);
            returnText = gst_mpegts_atsc_string_segment_get_string (segment);
        }
    }
    return returnText;
}

std::string ParserATSC::ParseAtscContentAdvisoryDescriptor(GstMpegtsDescriptor* desc)
{
    std::string rating;
    uint8_t* data;
    uint8_t rated_dimensions, rating_description_length, rating_region_count;
    GPtrArray* rating_description_text;

    data = desc->data + 2;
    rating_region_count = *data & 0x3F;
    data += 1;

    for (uint32_t i = 0; i < rating_region_count; i++) {
        rated_dimensions = data[1];
        data += (2 + rated_dimensions * 2);
        rating_description_length = data[0];
        data += 1;
        rating_description_text = _parse_atsc_mult_string(data, rating_description_length);
        rating = ATSCTextDecode(rating_description_text);
        data += rating_description_length;
    }

    return rating;
}

void ParserATSC::ClearEITPids()
{
    _eitPidIndex = 0;
    _eitPidVector.clear();
}

FrequencyHandler::FrequencyHandler(IParser* parser)
    : _parser(parser)
    , _currentFreqIndex(0)
{
}

FrequencyHandler::~FrequencyHandler()
{
    Block();
    Wait(Thread::BLOCKED | Thread::STOPPED | Thread::STOPPING, Core::infinite);
}

uint32_t FrequencyHandler::Worker()
{
    ParserATSC* parser = static_cast<ParserATSC*>(_parser);
    TRACE(Trace::Information, (_T("FrequencyHandler::Worker()")));
    if (IsRunning() == true) {
        if (parser->IsParserRunning() && (!parser->IsStreaming())) {
            parser->GetSIHandler()->StopFilters();
            parser->ClearEITPids();
            parser->GetSIHandler()->SetHomeTS(_frequencyList[_currentFreqIndex], 0);
            parser->SendBaseTableRequest();

            if (_frequencyList.size() == 1) {
                TRACE(Trace::Information, (string(__FUNCTION__)));
            } else if (_frequencyList.size() > 1) {
                TRACE(Trace::Information, (string(__FUNCTION__)));
                _currentFreqIndex++;
                _currentFreqIndex %= _frequencyList.size();
                TRACE(Trace::Information, (string(__FUNCTION__)));
            }
        }
    }
    TRACE(Trace::Information, (_T("Worker thread to Block state")));
    Block();
    return (WPEFramework::Core::infinite);
}

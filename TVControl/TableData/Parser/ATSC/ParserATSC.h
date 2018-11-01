#ifndef __TVATSCPARSER_H
#define __TVATSCPARSER_H
#include "ParserATSCCommon.h"
#include <EPGData.h>
#include <IParser.h>
#include <algorithm>
#include <map>
#include <tracing/tracing.h>
#include <unordered_set>
#include <vector>

#define AUDIO_CHAN_MAX (32)
#define ATSC_BASE_PID 0x1ffb

struct PmtPidInfo {
    uint16_t _videoPid;
    uint16_t _audioPid[AUDIO_CHAN_MAX];
    std::string _audioLang[AUDIO_CHAN_MAX];
    uint16_t _audioNum;
};

class ParserATSC : public IParser {
public:
    ParserATSC() = delete;
    ParserATSC(ISIHandler*, uint32_t);
    ~ParserATSC();
    void SendBaseTableRequest();
    void UpdateFrequencyList();
    void ReleaseFilters();
    bool ResetTables();
    uint32_t Worker();
    bool IsParserRunning() { return _parserRunning; }
    bool IsStreaming() { return _isStreaming; }
    ISIHandler* GetSIHandler();
    void UpdateCurrentFrequency(uint32_t);
    void SetCountryDetails(const std::string&, uint8_t) {}
    void ClearEITPids();

private:
    void ConfigureParser();
    void ParseData(GstMpegtsSection*, uint32_t);
    void ParseAtscServiceLocationDescriptor(PmtPidInfo&, GstMpegtsDescriptor*);
    std::string ParseAtscCaptionServiceDescriptor(GstMpegtsDescriptor*);
    std::string ParseAtscContentAdvisoryDescriptor(GstMpegtsDescriptor*);
    bool ParseMGT(GstMpegtsSection*);
    bool ParseEIT(GstMpegtsSection*);
    bool ParseVCT(GstMpegtsSection*, uint32_t);
    bool ParseEvents(const GstMpegtsAtscEIT*, uint8_t);
    bool ParseSTT(GstMpegtsSection*);

    std::string ATSCTextDecode(GPtrArray*);
    bool IsEITParsingCompleted();
    void PushEitStartRequest();
    void PushEitStopRequest();

    EPGDataBase& _epgDB;
    std::vector<uint16_t> _eitPidVector;
    unsigned _eitPidIndex;
    std::map<uint16_t, std::unordered_set<uint8_t> > _programMap;
    std::unordered_set<uint16_t> _channelSet;
    bool _isTimeParsed;
    bool _isMGTParsed;
    ISIHandler* _siHandler;
    bool _clientInitialised;
    bool _parserRunning;
    std::vector<uint32_t> _frequencyList;
    FrequencyHandler _freqHandler;
    uint32_t _currentParsingFrequency;
    bool _isStreaming;
    uint32_t _homeTS;
};
#endif

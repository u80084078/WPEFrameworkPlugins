#include "Module.h"
#include "TVPlatformImplementation.h"

using namespace WPEFramework;

namespace TVPlatform {

TVPlatformImplementation::TVPlatformImplementation()
    : _tunerCount(0)
    , _isRunning(true)
    , _isStreaming(false)
{
    InitializeTuners();
}

TVPlatformImplementation::~TVPlatformImplementation()
{
    _isRunning = false;
    _tunerList.clear();
}

LinuxDVB::TvTunerBackend* TVPlatformImplementation::GetTuner(bool isForStreaming)
{
    if (_tunerCount) {
        if (_tunerCount == 1)
            return _tunerList[0].get();
        if (isForStreaming)
            return _tunerList[1].get();
        return _tunerList[0].get();
    }
    return nullptr;
}

TvmRc TVPlatformImplementation::Scan(std::vector<uint32_t> freqList, ITunerHandler& tunerHandler)
{
    LinuxDVB::TvTunerBackend* tuner = GetTuner(true);
    TvmRc ret = TvmError;
    if (tuner) {
        if ((ret = tuner->StartScanning(freqList, tunerHandler)) == TvmSuccess)
            _isStreaming = true;
    }
    return ret;
}

TvmRc TVPlatformImplementation::StopScanning()
{
    LinuxDVB::TvTunerBackend* tuner = GetTuner(true);
    if (tuner)
        return tuner->StopScanning();
}

TvmRc TVPlatformImplementation::Tune(uint32_t frequency, uint16_t programNumber, uint16_t modulation, ITunerHandler& tunerHandler)
{
    TvmRc ret = TvmError;
    LinuxDVB::TvTunerBackend* tuner = GetTuner(true);
    if (tuner) {
        if ((ret = tuner->Tune(frequency, programNumber, modulation, tunerHandler)) == TvmSuccess)
            _isStreaming = true;
    }
    return ret;
}

TvmRc TVPlatformImplementation::SetHomeTS(uint32_t primaryFreq, uint32_t secondaryFreq)
{
    LinuxDVB::TvTunerBackend* tuner = GetTuner(false);
    if (tuner && (((_tunerCount == 1) && !_isStreaming) || (_tunerCount > 1)))
        return tuner->SetHomeTS(primaryFreq);
    return TvmError;
}

TvmRc TVPlatformImplementation::StopFilter(uint16_t pid, uint8_t tid)
{
    LinuxDVB::TvTunerBackend* tuner = GetTuner(false);
    if (tuner)
        return tuner->StopFilter(pid);
}

TvmRc TVPlatformImplementation::StopFilters()
{
    LinuxDVB::TvTunerBackend* tuner = GetTuner(false);
    if (tuner)
        return tuner->StopFilters();
}

TvmRc TVPlatformImplementation::StartFilter(uint16_t pid, uint8_t tid, ISectionHandler* pSectionHandler)
{
    LinuxDVB::TvTunerBackend* tuner = GetTuner(false);
    if (tuner)
        return tuner->StartFilter(pid, pSectionHandler);
}

bool TVPlatformImplementation::IsScanning()
{
    LinuxDVB::TvTunerBackend* tuner = GetTuner(true);
    if (tuner)
        return tuner->IsScanning();
}

std::vector<uint32_t>& TVPlatformImplementation::GetFrequencyList()
{
    LinuxDVB::TvTunerBackend* tuner = GetTuner(true);
    _isStreaming = false;
    if (tuner)
        return tuner->GetFrequencyList();
}

TvmRc TVPlatformImplementation::GetTSInfo(TSInfoList& tsInfoList)
{
    LinuxDVB::TvTunerBackend* tuner = GetTuner(true);
    if (tuner)
        return tuner->GetTSInfo(tsInfoList);
}

TvmRc TVPlatformImplementation::GetChannelMap(ChannelMap& chanMap)
{
    LinuxDVB::TvTunerBackend* tuner = GetTuner(true);
    if (tuner)
        return tuner->GetChannelMap(chanMap);
}

void TVPlatformImplementation::InitializeTuners()
{
    int32_t feOpenMode = 0;
    // FIXME Temporary fix for tuner release.
    char command[1024];
    snprintf(command, 1024, "STOP");
    TRACE(Trace::Information, (_T("Command : %s"), command));
    std::ofstream myfile;
    myfile.open("/etc/TVTune.txt", std::ios::trunc);
    if (myfile.is_open()) {
        myfile << command;
        myfile.close();
        sleep(1);
    } else
        TRACE(Trace::Error, (_T("Failed to open TVTune.txt file.")));

    _tunerCount = 0;
    for (uint32_t adapter = 0; adapter < DVB_ADAPTER_SCAN; adapter++) {
        for (uint32_t frontend = 0; frontend < DVB_ADAPTER_SCAN; frontend++) {
            DVBInfo feInfoTmp;
            if (!OpenFE(adapter, frontend, feInfoTmp))
                continue;

            std::unique_ptr<TunerData> tunerData = std::make_unique<TunerData>();
            TRACE(Trace::Information, (_T("Tuner identified as  %s  Adapter: %d Frontend: %d "), feInfoTmp.name.c_str(), adapter, frontend));
            tunerData->adapter = adapter;
            tunerData->frontend = frontend;
            CloseFE(feInfoTmp);
            std::unique_ptr<LinuxDVB::TvTunerBackend> tInfo = std::make_unique<LinuxDVB::TvTunerBackend>(_tunerCount, std::move(tunerData), _isDvb);
            // Update the  private tuner list.
            _tunerList.push_back(std::move(tInfo));
            _tunerCount += 1;
        }
    }
    for (auto& tuner : _tunerList)
        tuner->UpdateTunerCount(_tunerCount);
}

static SystemTVPlatformType<TVPlatformImplementation> g_instance;

} // namespace TVPlatform

TVPlatform::ISystemTVPlatform* GetSystemTVPlatform() {

    return &TVPlatform::g_instance;
}

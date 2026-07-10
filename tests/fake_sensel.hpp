#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

extern "C" {
#include "sensel.h"
}

namespace fake_sensel {

enum class Call : std::size_t {
    OpenAuto,
    OpenPath,
    RecoverPort,
    GetSensorInfo,
    GetNumLeds,
    GetMaxLedBrightness,
    GetLedRegisterSize,
    SetContactMask,
    SetFrameContent,
    SetBufferControl,
    AllocateFrame,
    StartScanning,
    ReadSensor,
    GetFrameCount,
    GetFrame,
    WriteLeds,
    StopScanning,
    FreeFrame,
    Close,
    Count
};

struct TestFrame {
    int lostFrameCount = 0;
    std::vector<SenselContact> contacts;
};

struct Snapshot {
    std::array<int, static_cast<std::size_t>(Call::Count)> calls{};
    std::vector<Call> callOrder;
    std::string openedPath;
    std::vector<unsigned char> frameBufferCounts;
    std::vector<std::vector<unsigned char>> ledWrites;
};

void reset();
void fail(Call call, int occurrence = 1);
void failFirst(Call call, int occurrences);
void failNext(Call call);
void setRecoveryResult(bool result);
void setLedCount(unsigned char count);
void queueFrame(TestFrame frame);
Snapshot snapshot();
int callCount(Call call);

} // namespace fake_sensel

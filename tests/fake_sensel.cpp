#include "fake_sensel.hpp"
#include "port_recovery.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <utility>

namespace fake_sensel {
namespace {

constexpr std::size_t callIndex(Call call) {
    return static_cast<std::size_t>(call);
}

struct FakeHandle {
    int value = 1;
};

struct State {
    std::mutex mutex;
    std::condition_variable changed;
    Snapshot snapshot;
    Call failureCall = Call::Count;
    int failureFirstOccurrence = 0;
    int failureLastOccurrence = 0;
    bool recoveryResult = true;
    unsigned char ledCount = 2;
    std::deque<TestFrame> frames;
    std::size_t availableFrames = 0;
};

State& state() {
    static State value;
    return value;
}

bool recordAndShouldFail(State& value, Call call) {
    const int count = ++value.snapshot.calls[callIndex(call)];
    return value.failureCall == call &&
           count >= value.failureFirstOccurrence &&
           count <= value.failureLastOccurrence;
}

SenselStatus resultFor(State& value, Call call) {
    return recordAndShouldFail(value, call) ? SENSEL_ERROR : SENSEL_OK;
}

} // namespace

void reset() {
    auto& value = state();
    std::lock_guard<std::mutex> lock(value.mutex);
    value.snapshot = {};
    value.failureCall = Call::Count;
    value.failureFirstOccurrence = 0;
    value.failureLastOccurrence = 0;
    value.recoveryResult = true;
    value.ledCount = 2;
    value.frames.clear();
    value.availableFrames = 0;
}

void fail(Call call, int occurrence) {
    auto& value = state();
    std::lock_guard<std::mutex> lock(value.mutex);
    value.failureCall = call;
    value.failureFirstOccurrence = occurrence;
    value.failureLastOccurrence = occurrence;
}

void failFirst(Call call, int occurrences) {
    auto& value = state();
    std::lock_guard<std::mutex> lock(value.mutex);
    value.failureCall = call;
    value.failureFirstOccurrence = 1;
    value.failureLastOccurrence = occurrences;
}

void failNext(Call call) {
    auto& value = state();
    {
        std::lock_guard<std::mutex> lock(value.mutex);
        value.failureCall = call;
        value.failureFirstOccurrence = value.snapshot.calls[callIndex(call)] + 1;
        value.failureLastOccurrence = value.failureFirstOccurrence;
    }
    value.changed.notify_all();
}

void setRecoveryResult(bool result) {
    auto& value = state();
    std::lock_guard<std::mutex> lock(value.mutex);
    value.recoveryResult = result;
}

void setLedCount(unsigned char count) {
    auto& value = state();
    std::lock_guard<std::mutex> lock(value.mutex);
    value.ledCount = count;
}

void queueFrame(TestFrame frame) {
    auto& value = state();
    {
        std::lock_guard<std::mutex> lock(value.mutex);
        value.frames.push_back(std::move(frame));
    }
    value.changed.notify_all();
}

Snapshot snapshot() {
    auto& value = state();
    std::lock_guard<std::mutex> lock(value.mutex);
    return value.snapshot;
}

int callCount(Call call) {
    return snapshot().calls[callIndex(call)];
}

} // namespace fake_sensel

namespace sensel::detail {

bool recoverExplicitPort(const std::string&) noexcept {
    auto& value = fake_sensel::state();
    std::lock_guard<std::mutex> lock(value.mutex);
    ++value.snapshot.calls[static_cast<std::size_t>(fake_sensel::Call::RecoverPort)];
    return value.recoveryResult;
}

} // namespace sensel::detail

extern "C" {

SenselStatus senselOpen(SENSEL_HANDLE* handle) {
    auto& value = fake_sensel::state();
    std::lock_guard<std::mutex> lock(value.mutex);
    if (fake_sensel::resultFor(value, fake_sensel::Call::OpenAuto) != SENSEL_OK) {
        *handle = nullptr;
        return SENSEL_ERROR;
    }
    *handle = new fake_sensel::FakeHandle;
    return SENSEL_OK;
}

SenselStatus senselOpenDeviceByComPort(SENSEL_HANDLE* handle, unsigned char* path) {
    auto& value = fake_sensel::state();
    std::lock_guard<std::mutex> lock(value.mutex);
    value.snapshot.openedPath = path ? reinterpret_cast<const char*>(path) : "";
    if (fake_sensel::resultFor(value, fake_sensel::Call::OpenPath) != SENSEL_OK) {
        *handle = nullptr;
        return SENSEL_ERROR;
    }
    *handle = new fake_sensel::FakeHandle;
    return SENSEL_OK;
}

SenselStatus senselGetSensorInfo(SENSEL_HANDLE, SenselSensorInfo* info) {
    auto& value = fake_sensel::state();
    std::lock_guard<std::mutex> lock(value.mutex);
    const auto result = fake_sensel::resultFor(value, fake_sensel::Call::GetSensorInfo);
    if (result == SENSEL_OK) {
        *info = {};
        info->width = 230.0f;
        info->height = 130.0f;
    }
    return result;
}

SenselStatus senselGetNumAvailableLEDs(SENSEL_HANDLE, unsigned char* count) {
    auto& value = fake_sensel::state();
    std::lock_guard<std::mutex> lock(value.mutex);
    const auto result = fake_sensel::resultFor(value, fake_sensel::Call::GetNumLeds);
    if (result == SENSEL_OK) {
        *count = value.ledCount;
    }
    return result;
}

SenselStatus senselGetMaxLEDBrightness(SENSEL_HANDLE, unsigned short* brightness) {
    auto& value = fake_sensel::state();
    std::lock_guard<std::mutex> lock(value.mutex);
    const auto result = fake_sensel::resultFor(value, fake_sensel::Call::GetMaxLedBrightness);
    if (result == SENSEL_OK) {
        *brightness = 255;
    }
    return result;
}

SenselStatus senselReadReg(SENSEL_HANDLE, unsigned char, unsigned char, unsigned char* data) {
    auto& value = fake_sensel::state();
    std::lock_guard<std::mutex> lock(value.mutex);
    const auto result = fake_sensel::resultFor(value, fake_sensel::Call::GetLedRegisterSize);
    if (result == SENSEL_OK) {
        *data = 1;
    }
    return result;
}

SenselStatus senselSetContactsMask(SENSEL_HANDLE, unsigned char) {
    auto& value = fake_sensel::state();
    std::lock_guard<std::mutex> lock(value.mutex);
    return fake_sensel::resultFor(value, fake_sensel::Call::SetContactMask);
}

SenselStatus senselSetFrameContent(SENSEL_HANDLE, unsigned char) {
    auto& value = fake_sensel::state();
    std::lock_guard<std::mutex> lock(value.mutex);
    return fake_sensel::resultFor(value, fake_sensel::Call::SetFrameContent);
}

SenselStatus senselAllocateFrameData(SENSEL_HANDLE, SenselFrameData** frame) {
    auto& value = fake_sensel::state();
    std::lock_guard<std::mutex> lock(value.mutex);
    const auto result = fake_sensel::resultFor(value, fake_sensel::Call::AllocateFrame);
    if (result == SENSEL_OK) {
        *frame = new SenselFrameData{};
        (*frame)->contacts = new SenselContact[64]{};
    }
    return result;
}

SenselStatus senselStartScanning(SENSEL_HANDLE) {
    auto& value = fake_sensel::state();
    std::lock_guard<std::mutex> lock(value.mutex);
    return fake_sensel::resultFor(value, fake_sensel::Call::StartScanning);
}

SenselStatus senselReadSensor(SENSEL_HANDLE) {
    auto& value = fake_sensel::state();
    std::unique_lock<std::mutex> lock(value.mutex);
    const auto result = fake_sensel::resultFor(value, fake_sensel::Call::ReadSensor);
    if (result == SENSEL_OK) {
        value.changed.wait_for(lock, std::chrono::milliseconds(5), [&value] {
            return !value.frames.empty();
        });
        value.availableFrames = value.frames.size();
    }
    return result;
}

SenselStatus senselGetNumAvailableFrames(SENSEL_HANDLE, unsigned int* count) {
    auto& value = fake_sensel::state();
    std::lock_guard<std::mutex> lock(value.mutex);
    const auto result = fake_sensel::resultFor(value, fake_sensel::Call::GetFrameCount);
    if (result == SENSEL_OK) {
        *count = static_cast<unsigned int>(value.availableFrames);
    }
    return result;
}

SenselStatus senselGetFrame(SENSEL_HANDLE, SenselFrameData* destination) {
    auto& value = fake_sensel::state();
    std::lock_guard<std::mutex> lock(value.mutex);
    const auto result = fake_sensel::resultFor(value, fake_sensel::Call::GetFrame);
    if (result != SENSEL_OK || value.frames.empty()) {
        return SENSEL_ERROR;
    }

    const auto frame = std::move(value.frames.front());
    value.frames.pop_front();
    if (value.availableFrames > 0) {
        --value.availableFrames;
    }
    destination->lost_frame_count = frame.lostFrameCount;
    destination->n_contacts = static_cast<unsigned char>(frame.contacts.size());
    std::copy(frame.contacts.begin(), frame.contacts.end(), destination->contacts);
    return SENSEL_OK;
}

SenselStatus senselWriteRegVS(SENSEL_HANDLE,
                              unsigned char,
                              unsigned int size,
                              unsigned char* data,
                              unsigned int*) {
    auto& value = fake_sensel::state();
    std::lock_guard<std::mutex> lock(value.mutex);
    const auto result = fake_sensel::resultFor(value, fake_sensel::Call::WriteLeds);
    value.snapshot.ledWrites.emplace_back(data, data + size);
    return result;
}

SenselStatus senselStopScanning(SENSEL_HANDLE) {
    auto& value = fake_sensel::state();
    std::lock_guard<std::mutex> lock(value.mutex);
    return fake_sensel::resultFor(value, fake_sensel::Call::StopScanning);
}

SenselStatus senselFreeFrameData(SENSEL_HANDLE, SenselFrameData* frame) {
    auto& value = fake_sensel::state();
    {
        std::lock_guard<std::mutex> lock(value.mutex);
        (void)fake_sensel::resultFor(value, fake_sensel::Call::FreeFrame);
    }
    delete[] frame->contacts;
    delete frame;
    return SENSEL_OK;
}

SenselStatus senselClose(SENSEL_HANDLE handle) {
    auto& value = fake_sensel::state();
    {
        std::lock_guard<std::mutex> lock(value.mutex);
        (void)fake_sensel::resultFor(value, fake_sensel::Call::Close);
    }
    delete static_cast<fake_sensel::FakeHandle*>(handle);
    return SENSEL_OK;
}

} // extern "C"

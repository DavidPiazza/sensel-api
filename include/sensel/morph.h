#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace sensel {

enum class Operation {
    None,
    Open,
    RecoverPort,
    GetSensorInfo,
    GetNumLeds,
    GetMaxLedBrightness,
    GetLedRegisterSize,
    SetContactMask,
    SetFrameContent,
    AllocateFrame,
    StartScanning,
    ReadSensor,
    GetFrameCount,
    GetFrame,
    FlushLeds
};

const char* operationName(Operation operation) noexcept;

class Error : public std::runtime_error {
public:
    Error(Operation operation, int nativeStatus, std::string details = {});

    Operation operation() const noexcept;
    int nativeStatus() const noexcept;

private:
    Operation operation_ = Operation::None;
    int nativeStatus_ = 0;
};

struct MorphOptions {
    // Empty uses the Sensel C library's ordinary device discovery.
    std::string devicePath;

    // Destructive stale-stream recovery is attempted only for an explicit path.
    bool recoverStaleStream = false;
};

struct DeviceInfo {
    float widthMm = 0.0f;
    float heightMm = 0.0f;
    std::size_t numLeds = 0;
};

enum class ContactState {
    Invalid,
    Start,
    Move,
    End
};

struct Contact {
    std::uint8_t id = 0;
    ContactState state = ContactState::Invalid;
    float xMm = 0.0f;
    float yMm = 0.0f;
    float forceGrams = 0.0f;
    float areaSensorElements = 0.0f;
    float orientationDegrees = 0.0f;
    float majorAxisMm = 0.0f;
    float minorAxisMm = 0.0f;
};

struct Frame {
    int lostFrameCount = 0;
    std::vector<Contact> contacts;
};

enum class ReadStatus {
    FramesAvailable,
    NoFrames,
    DeviceError
};

struct ReadResult {
    ReadStatus status = ReadStatus::NoFrames;
    std::size_t frameCount = 0;
    Operation failedOperation = Operation::None;
    int nativeStatus = 0;
};

enum class LedFlushStatus {
    NoChange,
    RateLimited,
    Flushed,
    DeviceError
};

// Synchronous RAII owner for one Sensel Morph. Construction opens and fully
// configures contact scanning or throws Error. readFrames() may block for the
// C library's serial timeout and must not run on an audio or shared poll thread.
class Morph {
public:
    explicit Morph(MorphOptions options = {});
    ~Morph() noexcept;

    Morph(const Morph&) = delete;
    Morph& operator=(const Morph&) = delete;
    Morph(Morph&&) noexcept;
    Morph& operator=(Morph&&) noexcept;

    const DeviceInfo& info() const noexcept;

    // Returns every pending frame in order. On a device error, out is empty.
    ReadResult readFrames(std::vector<Frame>& out);

    // Stages one normalized LED value. No device I/O occurs until flushLeds().
    void setLed(std::size_t index, float normalizedBrightness);
    LedFlushStatus flushLeds() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sensel

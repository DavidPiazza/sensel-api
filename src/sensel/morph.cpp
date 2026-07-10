#include "sensel/morph.h"
#include "port_recovery.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <utility>

extern "C" {
#include "sensel.h"
#include "sensel_register_map.h"
}

namespace sensel {
namespace {

constexpr auto ledMinimumPeriod = std::chrono::milliseconds(50);
constexpr int ledQuantizationSteps = 15;

std::string makeErrorMessage(Operation operation, int nativeStatus, const std::string& details) {
    std::ostringstream message;
    message << operationName(operation) << " failed (Sensel status " << nativeStatus << ")";
    if (!details.empty()) {
        message << ": " << details;
    }
    return message.str();
}

ContactState contactState(unsigned int state) noexcept {
    switch (state) {
    case CONTACT_START:
        return ContactState::Start;
    case CONTACT_MOVE:
        return ContactState::Move;
    case CONTACT_END:
        return ContactState::End;
    default:
        return ContactState::Invalid;
    }
}

} // namespace

const char* operationName(Operation operation) noexcept {
    switch (operation) {
    case Operation::None: return "none";
    case Operation::Open: return "open Sensel Morph";
    case Operation::RecoverPort: return "recover Sensel serial port";
    case Operation::GetSensorInfo: return "read Sensel sensor information";
    case Operation::GetNumLeds: return "read Sensel LED count";
    case Operation::GetMaxLedBrightness: return "read Sensel maximum LED brightness";
    case Operation::GetLedRegisterSize: return "read Sensel LED register size";
    case Operation::SetContactMask: return "set Sensel contact mask";
    case Operation::SetFrameContent: return "set Sensel frame content";
    case Operation::SetBufferControl: return "set Sensel frame buffer count";
    case Operation::AllocateFrame: return "allocate Sensel frame";
    case Operation::StartScanning: return "start Sensel scanning";
    case Operation::ReadSensor: return "read Sensel sensor";
    case Operation::GetFrameCount: return "read Sensel frame count";
    case Operation::GetFrame: return "read Sensel frame";
    case Operation::FlushLeds: return "flush Sensel LEDs";
    }
    return "unknown Sensel operation";
}

Error::Error(Operation operation, int nativeStatus, std::string details)
    : std::runtime_error(makeErrorMessage(operation, nativeStatus, details)),
      operation_(operation),
      nativeStatus_(nativeStatus) {
}

Operation Error::operation() const noexcept {
    return operation_;
}

int Error::nativeStatus() const noexcept {
    return nativeStatus_;
}

class Morph::Impl {
public:
    Impl() = default;

    ~Impl() noexcept {
        shutdown();
    }

    void initialize(const MorphOptions& options) {
        if (options.recoverStaleStream && options.devicePath.empty()) {
            throw Error(Operation::RecoverPort,
                        SENSEL_ERROR,
                        "recoverStaleStream requires an explicit devicePath");
        }
        if (options.frameBufferCount > maximumFrameBufferCount) {
            throw Error(Operation::SetBufferControl,
                        SENSEL_ERROR,
                        "frameBufferCount must be between 0 and 50");
        }

        auto openDevice = [&]() {
            if (options.devicePath.empty()) {
                return senselOpen(&handle_);
            }
            return senselOpenDeviceByComPort(
                &handle_,
                reinterpret_cast<unsigned char*>(const_cast<char*>(options.devicePath.c_str())));
        };

        auto status = openDevice();
        if (status != SENSEL_OK && options.recoverStaleStream) {
            if (!detail::recoverExplicitPort(options.devicePath)) {
                throw Error(Operation::RecoverPort, SENSEL_ERROR, options.devicePath);
            }
            status = openDevice();
        }
        require(status, Operation::Open);

        SenselSensorInfo sensorInfo{};
        require(senselGetSensorInfo(handle_, &sensorInfo), Operation::GetSensorInfo);
        info_.widthMm = sensorInfo.width;
        info_.heightMm = sensorInfo.height;

        unsigned char ledCount = 0;
        require(senselGetNumAvailableLEDs(handle_, &ledCount), Operation::GetNumLeds);
        info_.numLeds = ledCount;
        require(senselGetMaxLEDBrightness(handle_, &maximumLedBrightness_),
                Operation::GetMaxLedBrightness);

        unsigned char registerSize = 1;
        require(senselReadReg(handle_, SENSEL_REG_LED_BRIGHTNESS_SIZE, 1, &registerSize),
                Operation::GetLedRegisterSize);
        ledRegisterSize_ = registerSize == 2 ? 2u : 1u;
        ledBytes_.assign(info_.numLeds * ledRegisterSize_, 0);
        ledsDirty_ = !ledBytes_.empty();

        require(senselSetContactsMask(handle_, CONTACT_MASK_ELLIPSE), Operation::SetContactMask);
        require(senselSetFrameContent(handle_, FRAME_CONTENT_CONTACTS_MASK),
                Operation::SetFrameContent);
        // Buffer control persists on the device. Apply zero as deliberately as
        // a non-zero value so behavior never depends on a previous process.
        require(senselSetBufferControl(handle_, options.frameBufferCount),
                Operation::SetBufferControl);
        require(senselAllocateFrameData(handle_, &frame_), Operation::AllocateFrame);
        require(senselStartScanning(handle_), Operation::StartScanning);
        scanning_ = true;
    }

    const DeviceInfo& info() const noexcept {
        return info_;
    }

    ReadResult readFrames(std::vector<Frame>& out) {
        out.clear();
        if (!handle_ || !frame_) {
            return {ReadStatus::DeviceError, 0, Operation::ReadSensor, SENSEL_ERROR};
        }

        auto status = senselReadSensor(handle_);
        if (status != SENSEL_OK) {
            return {ReadStatus::DeviceError, 0, Operation::ReadSensor, status};
        }

        unsigned int availableFrames = 0;
        status = senselGetNumAvailableFrames(handle_, &availableFrames);
        if (status != SENSEL_OK) {
            return {ReadStatus::DeviceError, 0, Operation::GetFrameCount, status};
        }
        if (availableFrames == 0) {
            return {ReadStatus::NoFrames, 0, Operation::None, SENSEL_OK};
        }

        std::vector<Frame> batch;
        batch.reserve(availableFrames);
        for (unsigned int frameIndex = 0; frameIndex < availableFrames; ++frameIndex) {
            status = senselGetFrame(handle_, frame_);
            if (status != SENSEL_OK) {
                return {ReadStatus::DeviceError, 0, Operation::GetFrame, status};
            }

            Frame frame;
            frame.lostFrameCount = frame_->lost_frame_count;
            frame.contacts.reserve(frame_->n_contacts);
            for (unsigned int contactIndex = 0; contactIndex < frame_->n_contacts; ++contactIndex) {
                const SenselContact& source = frame_->contacts[contactIndex];
                frame.contacts.push_back(Contact{
                    source.id,
                    contactState(source.state),
                    source.x_pos,
                    source.y_pos,
                    source.total_force,
                    source.area,
                    source.orientation,
                    source.major_axis,
                    source.minor_axis,
                });
            }
            batch.push_back(std::move(frame));
        }

        out = std::move(batch);
        return {ReadStatus::FramesAvailable, out.size(), Operation::None, SENSEL_OK};
    }

    void setLed(std::size_t index, float normalizedBrightness) {
        if (index >= info_.numLeds) {
            throw std::out_of_range("Sensel LED index is out of range");
        }
        if (!std::isfinite(normalizedBrightness)) {
            throw std::invalid_argument("Sensel LED brightness must be finite");
        }

        const float clamped = std::max(0.0f, std::min(normalizedBrightness, 1.0f));
        const auto quantized = static_cast<std::uint16_t>(
            std::lround(clamped * static_cast<float>(ledQuantizationSteps)) *
            static_cast<long>(maximumLedBrightness_) / ledQuantizationSteps);

        if (ledRegisterSize_ == 1) {
            const auto byte = static_cast<std::uint8_t>(quantized);
            if (ledBytes_[index] == byte) {
                return;
            }
            ledBytes_[index] = byte;
        } else {
            const auto low = static_cast<std::uint8_t>(quantized & 0xffu);
            const auto high = static_cast<std::uint8_t>(quantized >> 8u);
            const auto offset = index * 2;
            if (ledBytes_[offset] == low && ledBytes_[offset + 1] == high) {
                return;
            }
            ledBytes_[offset] = low;
            ledBytes_[offset + 1] = high;
        }
        ledsDirty_ = true;
    }

    LedFlushResult flushLeds() noexcept {
        if (!handle_) {
            return {LedFlushStatus::DeviceError, Operation::FlushLeds, SENSEL_ERROR};
        }
        if (!ledsDirty_) {
            return {LedFlushStatus::NoChange, Operation::None, SENSEL_OK};
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - lastLedSend_ < ledMinimumPeriod) {
            return {LedFlushStatus::RateLimited, Operation::None, SENSEL_OK};
        }

        const auto status = senselWriteRegVS(
            handle_,
            SENSEL_REG_LED_BRIGHTNESS,
            static_cast<unsigned int>(ledBytes_.size()),
            ledBytes_.data(),
            nullptr);
        if (status != SENSEL_OK) {
            return {LedFlushStatus::DeviceError, Operation::FlushLeds, status};
        }

        lastLedSend_ = now;
        ledsDirty_ = false;
        return {LedFlushStatus::Flushed, Operation::None, SENSEL_OK};
    }

private:
    static void require(SenselStatus status, Operation operation) {
        if (status != SENSEL_OK) {
            throw Error(operation, status);
        }
    }

    void shutdown() noexcept {
        if (!handle_) {
            return;
        }

        if (scanning_) {
            (void)senselStopScanning(handle_);
            scanning_ = false;
        }

        if (!ledBytes_.empty()) {
            std::fill(ledBytes_.begin(), ledBytes_.end(), 0);
            (void)senselWriteRegVS(
                handle_,
                SENSEL_REG_LED_BRIGHTNESS,
                static_cast<unsigned int>(ledBytes_.size()),
                ledBytes_.data(),
                nullptr);
        }

        if (frame_) {
            (void)senselFreeFrameData(handle_, frame_);
            frame_ = nullptr;
        }

        (void)senselClose(handle_);
        handle_ = nullptr;
    }

    SENSEL_HANDLE handle_ = nullptr;
    SenselFrameData* frame_ = nullptr;
    DeviceInfo info_;
    unsigned short maximumLedBrightness_ = 0;
    std::size_t ledRegisterSize_ = 1;
    std::vector<std::uint8_t> ledBytes_;
    std::chrono::steady_clock::time_point lastLedSend_{};
    bool ledsDirty_ = false;
    bool scanning_ = false;
};

Morph::Morph(MorphOptions options)
    : impl_(std::make_unique<Impl>()) {
    impl_->initialize(options);
}

Morph::~Morph() noexcept = default;
Morph::Morph(Morph&&) noexcept = default;
Morph& Morph::operator=(Morph&&) noexcept = default;

const DeviceInfo& Morph::info() const noexcept {
    static const DeviceInfo emptyInfo{};
    return impl_ ? impl_->info() : emptyInfo;
}

ReadResult Morph::readFrames(std::vector<Frame>& out) {
    if (!impl_) {
        out.clear();
        return {ReadStatus::DeviceError, 0, Operation::ReadSensor, SENSEL_ERROR};
    }
    return impl_->readFrames(out);
}

void Morph::setLed(std::size_t index, float normalizedBrightness) {
    if (!impl_) {
        throw std::logic_error("cannot use a moved-from Sensel Morph");
    }
    impl_->setLed(index, normalizedBrightness);
}

LedFlushResult Morph::flushLeds() noexcept {
    return impl_ ? impl_->flushLeds()
                 : LedFlushResult{
                       LedFlushStatus::DeviceError,
                       Operation::FlushLeds,
                       SENSEL_ERROR,
                   };
}

} // namespace sensel

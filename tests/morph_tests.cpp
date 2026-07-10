#include "fake_sensel.hpp"
#include "sensel/morph.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expression)                                                                    \
    do {                                                                                     \
        if (!(expression)) {                                                                 \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #expression      \
                      << '\n';                                                               \
            ++failures;                                                                      \
        }                                                                                    \
    } while (false)

SenselContact makeContact(unsigned char id,
                          unsigned int state,
                          float x,
                          float y,
                          float force) {
    SenselContact contact{};
    contact.id = id;
    contact.state = state;
    contact.x_pos = x;
    contact.y_pos = y;
    contact.total_force = force;
    contact.area = 12.0f;
    contact.orientation = 18.0f;
    contact.major_axis = 7.0f;
    contact.minor_axis = 3.0f;
    return contact;
}

void testOwnershipAndInformation() {
    static_assert(!std::is_copy_constructible<sensel::Morph>::value, "Morph must not copy");
    static_assert(!std::is_copy_assignable<sensel::Morph>::value, "Morph must not copy");
    static_assert(std::is_nothrow_move_constructible<sensel::Morph>::value,
                  "Morph moves must be noexcept");
    static_assert(std::is_nothrow_move_assignable<sensel::Morph>::value,
                  "Morph moves must be noexcept");
    static_assert(noexcept(std::declval<sensel::Morph&>().flushLeds()),
                  "LED polling outcomes must not throw");

    fake_sensel::reset();
    {
        sensel::Morph first;
        CHECK(first.info().widthMm == 230.0f);
        CHECK(first.info().heightMm == 130.0f);
        CHECK(first.info().numLeds == 2);

        sensel::Morph second(std::move(first));
        CHECK(second.info().numLeds == 2);
        std::vector<sensel::Frame> movedFromFrames;
        CHECK(first.readFrames(movedFromFrames).status == sensel::ReadStatus::DeviceError);
    }

    CHECK(fake_sensel::callCount(fake_sensel::Call::StopScanning) == 1);
    CHECK(fake_sensel::callCount(fake_sensel::Call::FreeFrame) == 1);
    CHECK(fake_sensel::callCount(fake_sensel::Call::Close) == 1);
}

void testMoveAssignmentCleansBothOwners() {
    fake_sensel::reset();
    {
        sensel::Morph first;
        sensel::Morph second;
        second = std::move(first);
        std::vector<sensel::Frame> frames;
        CHECK(first.readFrames(frames).status == sensel::ReadStatus::DeviceError);
        const auto movedFromFlush = first.flushLeds();
        CHECK(movedFromFlush.status == sensel::LedFlushStatus::DeviceError);
        CHECK(movedFromFlush.failedOperation == sensel::Operation::FlushLeds);
        CHECK(movedFromFlush.nativeStatus == SENSEL_ERROR);
    }
    CHECK(fake_sensel::callCount(fake_sensel::Call::StopScanning) == 2);
    CHECK(fake_sensel::callCount(fake_sensel::Call::FreeFrame) == 2);
    CHECK(fake_sensel::callCount(fake_sensel::Call::Close) == 2);
}

void testSetupFailuresAreTransactional() {
    const std::vector<std::pair<fake_sensel::Call, sensel::Operation>> cases{
        {fake_sensel::Call::OpenAuto, sensel::Operation::Open},
        {fake_sensel::Call::GetSensorInfo, sensel::Operation::GetSensorInfo},
        {fake_sensel::Call::GetNumLeds, sensel::Operation::GetNumLeds},
        {fake_sensel::Call::GetMaxLedBrightness, sensel::Operation::GetMaxLedBrightness},
        {fake_sensel::Call::GetLedRegisterSize, sensel::Operation::GetLedRegisterSize},
        {fake_sensel::Call::SetContactMask, sensel::Operation::SetContactMask},
        {fake_sensel::Call::SetFrameContent, sensel::Operation::SetFrameContent},
        {fake_sensel::Call::AllocateFrame, sensel::Operation::AllocateFrame},
        {fake_sensel::Call::StartScanning, sensel::Operation::StartScanning},
    };

    for (const auto& testCase : cases) {
        fake_sensel::reset();
        fake_sensel::fail(testCase.first);
        bool caught = false;
        try {
            sensel::Morph morph;
        } catch (const sensel::Error& error) {
            caught = true;
            CHECK(error.operation() == testCase.second);
            CHECK(error.nativeStatus() == SENSEL_ERROR);
        }
        CHECK(caught);

        if (testCase.first == fake_sensel::Call::OpenAuto) {
            CHECK(fake_sensel::callCount(fake_sensel::Call::Close) == 0);
        } else {
            CHECK(fake_sensel::callCount(fake_sensel::Call::Close) == 1);
        }
        const bool frameWasAllocated =
            testCase.first == fake_sensel::Call::StartScanning;
        CHECK(fake_sensel::callCount(fake_sensel::Call::FreeFrame) ==
              (frameWasAllocated ? 1u : 0u));
        CHECK(fake_sensel::callCount(fake_sensel::Call::StopScanning) == 0);
    }
}

void testFrameBatchesAndStatuses() {
    fake_sensel::reset();
    fake_sensel::queueFrame({0, {makeContact(3, CONTACT_START, 10.0f, 20.0f, 30.0f)}});
    fake_sensel::queueFrame({2, {makeContact(3, CONTACT_MOVE, 11.0f, 21.0f, 31.0f)}});
    fake_sensel::queueFrame({0, {makeContact(3, CONTACT_END, 12.0f, 22.0f, 0.0f)}});
    fake_sensel::queueFrame({0, {}});

    sensel::Morph morph;
    std::vector<sensel::Frame> frames;
    const auto result = morph.readFrames(frames);
    CHECK(result.status == sensel::ReadStatus::FramesAvailable);
    CHECK(result.frameCount == 4);
    CHECK(frames.size() == 4);
    CHECK(frames[0].contacts[0].state == sensel::ContactState::Start);
    CHECK(frames[1].contacts[0].state == sensel::ContactState::Move);
    CHECK(frames[1].lostFrameCount == 2);
    CHECK(frames[2].contacts[0].state == sensel::ContactState::End);
    CHECK(frames[3].contacts.empty());
    CHECK(frames[0].contacts[0].xMm == 10.0f);
    CHECK(frames[0].contacts[0].forceGrams == 30.0f);
    CHECK(frames[0].contacts[0].areaSensorElements == 12.0f);
    CHECK(frames[0].contacts[0].orientationDegrees == 18.0f);
    CHECK(frames[0].contacts[0].majorAxisMm == 7.0f);
    CHECK(frames[0].contacts[0].minorAxisMm == 3.0f);

    CHECK(morph.readFrames(frames).status == sensel::ReadStatus::NoFrames);
    CHECK(frames.empty());
}

void testReadErrorsDoNotPublishPartialBatches() {
    fake_sensel::reset();
    fake_sensel::queueFrame({0, {makeContact(1, CONTACT_START, 1.0f, 2.0f, 3.0f)}});
    fake_sensel::queueFrame({0, {makeContact(1, CONTACT_MOVE, 2.0f, 3.0f, 4.0f)}});
    fake_sensel::fail(fake_sensel::Call::GetFrame, 2);

    sensel::Morph morph;
    std::vector<sensel::Frame> frames(1);
    const auto result = morph.readFrames(frames);
    CHECK(result.status == sensel::ReadStatus::DeviceError);
    CHECK(result.failedOperation == sensel::Operation::GetFrame);
    CHECK(result.frameCount == 0);
    CHECK(frames.empty());

    fake_sensel::reset();
    fake_sensel::fail(fake_sensel::Call::ReadSensor);
    sensel::Morph failingMorph;
    CHECK(failingMorph.readFrames(frames).failedOperation == sensel::Operation::ReadSensor);
}

void testShutdownAfterDeviceError() {
    fake_sensel::reset();
    {
        sensel::Morph morph;
        fake_sensel::failNext(fake_sensel::Call::ReadSensor);
        std::vector<sensel::Frame> frames;
        CHECK(morph.readFrames(frames).status == sensel::ReadStatus::DeviceError);
    }
    CHECK(fake_sensel::callCount(fake_sensel::Call::StopScanning) == 1);
    CHECK(fake_sensel::callCount(fake_sensel::Call::FreeFrame) == 1);
    CHECK(fake_sensel::callCount(fake_sensel::Call::Close) == 1);
}

void testLedStagingAndRetry() {
    fake_sensel::reset();
    fake_sensel::fail(fake_sensel::Call::WriteLeds, 1);
    sensel::Morph morph;

    morph.setLed(0, 1.0f);
    const auto failed = morph.flushLeds();
    CHECK(failed.status == sensel::LedFlushStatus::DeviceError);
    CHECK(failed.failedOperation == sensel::Operation::FlushLeds);
    CHECK(failed.nativeStatus == SENSEL_ERROR);

    const auto retried = morph.flushLeds();
    CHECK(retried.status == sensel::LedFlushStatus::Flushed);
    CHECK(retried.failedOperation == sensel::Operation::None);
    CHECK(retried.nativeStatus == SENSEL_OK);

    const auto unchanged = morph.flushLeds();
    CHECK(unchanged.status == sensel::LedFlushStatus::NoChange);
    CHECK(unchanged.failedOperation == sensel::Operation::None);
    CHECK(unchanged.nativeStatus == SENSEL_OK);

    const auto afterRetry = fake_sensel::snapshot();
    CHECK(afterRetry.ledWrites.size() == 2);
    CHECK(afterRetry.ledWrites.back().size() == 2);
    CHECK(afterRetry.ledWrites.back()[0] == 255);

    morph.setLed(1, 0.5f);
    const auto rateLimited = morph.flushLeds();
    CHECK(rateLimited.status == sensel::LedFlushStatus::RateLimited);
    CHECK(rateLimited.failedOperation == sensel::Operation::None);
    CHECK(rateLimited.nativeStatus == SENSEL_OK);

    bool outOfRange = false;
    try {
        morph.setLed(2, 0.5f);
    } catch (const std::out_of_range&) {
        outOfRange = true;
    }
    CHECK(outOfRange);

    bool nonFinite = false;
    try {
        morph.setLed(0, std::numeric_limits<float>::quiet_NaN());
    } catch (const std::invalid_argument&) {
        nonFinite = true;
    }
    CHECK(nonFinite);
}

void testExplicitPathAndRecoveryPolicy() {
    fake_sensel::reset();
    {
        sensel::Morph morph({"/dev/fake-sensel", false});
        CHECK(fake_sensel::snapshot().openedPath == "/dev/fake-sensel");
    }
    CHECK(fake_sensel::callCount(fake_sensel::Call::OpenAuto) == 0);
    CHECK(fake_sensel::callCount(fake_sensel::Call::OpenPath) == 1);

    fake_sensel::reset();
    fake_sensel::fail(fake_sensel::Call::OpenPath, 1);
    {
        sensel::Morph recovered({"/dev/fake-sensel", true});
    }
    CHECK(fake_sensel::callCount(fake_sensel::Call::RecoverPort) == 1);
    CHECK(fake_sensel::callCount(fake_sensel::Call::OpenPath) == 2);

    fake_sensel::reset();
    fake_sensel::fail(fake_sensel::Call::OpenPath, 1);
    fake_sensel::setRecoveryResult(false);
    bool recoveryFailed = false;
    try {
        sensel::Morph morph({"/dev/fake-sensel", true});
    } catch (const sensel::Error& error) {
        recoveryFailed = error.operation() == sensel::Operation::RecoverPort;
    }
    CHECK(recoveryFailed);
    CHECK(fake_sensel::callCount(fake_sensel::Call::OpenPath) == 1);

    fake_sensel::reset();
    bool invalidOptions = false;
    try {
        sensel::Morph morph({"", true});
    } catch (const sensel::Error& error) {
        invalidOptions = error.operation() == sensel::Operation::RecoverPort;
    }
    CHECK(invalidOptions);
    CHECK(fake_sensel::callCount(fake_sensel::Call::OpenAuto) == 0);
}

} // namespace

int main() {
    testOwnershipAndInformation();
    testMoveAssignmentCleansBothOwners();
    testSetupFailuresAreTransactional();
    testFrameBatchesAndStatuses();
    testReadErrorsDoNotPublishPartialBatches();
    testShutdownAfterDeviceError();
    testLedStagingAndRetry();
    testExplicitPathAndRecoveryPolicy();

    if (failures != 0) {
        std::cerr << failures << " Sensel Morph test(s) failed\n";
        return 1;
    }
    std::cout << "Sensel Morph tests passed\n";
    return 0;
}

#include "sensel/morph.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <dirent.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
extern "C" {
#include "sensel.h"
#include "sensel_register_map.h"
}

namespace sensel {

// One bulk LED write costs ~4.5 ms (a scan period); 20 Hz keeps the control
// loop >90% available while the strip still animates smoothly.
static constexpr auto LED_MIN_PERIOD = std::chrono::milliseconds(50);
static constexpr int  LED_QUANT      = 15;   // brightness steps; kills jitter rewrites

// The lib's open-probe reads the magic register without flushing the line
// first, so ANY bytes left in flight by a previous session (a crashed app, a
// SIGKILL, even a clean quit that raced the stream) make the probe fail until
// the Morph is power-cycled. Self-heal: before opening, blindly send
// SCAN_ENABLED=0 to candidate ports and drain whatever is stuck in the pipe.
static void sanitizePort(const char* path) {
    int fd = ::open(path, O_RDWR | O_NONBLOCK | O_NOCTTY);
    if (fd < 0) return;
    termios opt;
    tcgetattr(fd, &opt);
    opt.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    opt.c_oflag &= ~OPOST;
    opt.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    opt.c_cflag &= ~(CSIZE | PARENB);
    opt.c_cflag |= CS8;
    cfsetispeed(&opt, B115200);
    cfsetospeed(&opt, B115200);
    tcsetattr(fd, TCSANOW, &opt);
    // write SCAN_ENABLED (0x25) = 0: {addr, reg, size} + data + checksum
    const unsigned char stop[] = { 0x01, 0x25, 0x01, 0x00, 0x00 };
    (void)!::write(fd, stop, sizeof stop);
    unsigned char junk[4096];
    for (int i = 0; i < 50; ++i) {                 // drain for ~250 ms
        while (::read(fd, junk, sizeof junk) > 0) {}
        usleep(5000);
    }
    tcflush(fd, TCIOFLUSH);
    ::close(fd);
}

static void sanitizeCandidatePorts() {
    DIR* d = opendir("/dev");
    if (!d) return;
    while (dirent* e = readdir(d)) {
        if (std::strstr(e->d_name, "cu.usbmodem") || std::strstr(e->d_name, "ttyACM") ||
            std::strstr(e->d_name, "morph") || std::strstr(e->d_name, "squirt")) {
            char path[300];
            std::snprintf(path, sizeof path, "/dev/%s", e->d_name);
            sanitizePort(path);
        }
    }
    closedir(d);
}

bool Morph::open() {
    sanitizeCandidatePorts();
    SENSEL_HANDLE h = nullptr;
    SenselStatus rc = SENSEL_OK;
    for (int i = 0; i < 5 && !h; ++i) rc = senselOpen(&h);   // auto-scan can miss once
    if (rc != SENSEL_OK || !h) return false;
    handle_ = h;

    SenselSensorInfo info;
    senselGetSensorInfo(h, &info);
    width_  = info.width;
    height_ = info.height;

    unsigned char nleds = 0; senselGetNumAvailableLEDs(h, &nleds);
    senselGetMaxLEDBrightness(h, &maxBright_);
    unsigned char regSize = 1;
    senselReadReg(h, SENSEL_REG_LED_BRIGHTNESS_SIZE, 1, &regSize);
    ledRegSize_ = (regSize == 2) ? 2 : 1;
    numLeds_ = nleds;
    ledArr_.assign((size_t)numLeds_ * ledRegSize_, 0);
    ledDirty_ = true;                     // force one clearing write
    return true;
}

void Morph::startContacts() {
    SENSEL_HANDLE h = (SENSEL_HANDLE)handle_;
    // Orientation lives in the optional ellipse payload. Without this contact
    // mask, SenselContact::orientation is not guaranteed to be valid.
    senselSetContactsMask(h, CONTACT_MASK_ELLIPSE);
    senselSetFrameContent(h, FRAME_CONTENT_CONTACTS_MASK);
    SenselFrameData* f = nullptr;
    senselAllocateFrameData(h, &f);
    frame_ = f;
    senselStartScanning(h);
}

int Morph::poll(std::vector<Contact>& out) {
    out.clear();
    SENSEL_HANDLE h = (SENSEL_HANDLE)handle_;
    SenselFrameData* f = (SenselFrameData*)frame_;
    if (senselReadSensor(h) != SENSEL_OK) return -1;
    unsigned int nframes = 0;
    senselGetNumAvailableFrames(h, &nframes);
    if (nframes == 0) return -1;
    for (unsigned int i = 0; i < nframes; ++i) {
        if (senselGetFrame(h, f) != SENSEL_OK) continue;
        out.clear();   // keep only the most recent frame
        for (int c = 0; c < f->n_contacts; ++c) {
            const SenselContact& sc = f->contacts[c];
            float orientation = (sc.content_bit_mask & CONTACT_MASK_ELLIPSE) ? sc.orientation : 0.f;
            out.push_back(Contact{ sc.id, (int)sc.state, sc.x_pos, sc.y_pos,
                                   sc.total_force, sc.area, orientation });
        }
    }
    return (int)out.size();
}

void Morph::setLed(int idx, float v01) {
    if (idx < 0 || idx >= numLeds_) return;
    if (v01 < 0) v01 = 0; if (v01 > 1) v01 = 1;
    // coarse quantization: finger-force jitter must not trigger serial traffic
    uint16_t b = (uint16_t)(std::lround(v01 * LED_QUANT) * (long)maxBright_ / LED_QUANT);
    if (ledRegSize_ == 1) {
        if (ledArr_[idx] == (uint8_t)b) return;
        ledArr_[idx] = (uint8_t)b;
    } else {
        uint8_t lo = (uint8_t)(b & 0xFF), hi = (uint8_t)(b >> 8);
        if (ledArr_[2*idx] == lo && ledArr_[2*idx + 1] == hi) return;
        ledArr_[2*idx] = lo; ledArr_[2*idx + 1] = hi;
    }
    ledDirty_ = true;
}

void Morph::flushLeds() {
    if (!ledDirty_ || !handle_) return;
    auto now = std::chrono::steady_clock::now();
    if (now - lastLedSend_ < LED_MIN_PERIOD) return;
    lastLedSend_ = now;
    ledDirty_ = false;
    senselWriteRegVS((SENSEL_HANDLE)handle_, SENSEL_REG_LED_BRIGHTNESS,
                     (unsigned int)ledArr_.size(), ledArr_.data(), nullptr);
}

void Morph::close() {
    if (!handle_) return;
    SENSEL_HANDLE h = (SENSEL_HANDLE)handle_;
    // Stop the stream FIRST, and stubbornly: a SIGINT-interrupted read leaves
    // the serial protocol desynced, and if scanning survives close() the
    // device won't answer the next open's probe until it is power-cycled.
    for (int tries = 0; tries < 3; ++tries) {
        if (senselStopScanning(h) == SENSEL_OK) break;
        senselReadSensor(h);                       // drain frames to resync
    }
    std::fill(ledArr_.begin(), ledArr_.end(), 0);
    senselWriteRegVS(h, SENSEL_REG_LED_BRIGHTNESS,
                     (unsigned int)ledArr_.size(), ledArr_.data(), nullptr);
    if (frame_) senselFreeFrameData(h, (SenselFrameData*)frame_);
    senselClose(h);
    handle_ = frame_ = nullptr;
}

} // namespace sensel

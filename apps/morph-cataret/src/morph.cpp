#include "morph.h"
#include <cstdio>
#include <cmath>
extern "C" {
#include "sensel.h"
}

bool Morph::open() {
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
    numLeds_ = nleds;
    ledState_.assign(numLeds_, 0xFFFF);   // force first write of every LED
    return true;
}

void Morph::startContacts() {
    SENSEL_HANDLE h = (SENSEL_HANDLE)handle_;
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
    if (senselReadSensor(h) != SENSEL_OK) return 0;
    unsigned int nframes = 0;
    senselGetNumAvailableFrames(h, &nframes);
    for (unsigned int i = 0; i < nframes; ++i) {
        if (senselGetFrame(h, f) != SENSEL_OK) continue;
        out.clear();   // keep only the most recent frame
        for (int c = 0; c < f->n_contacts; ++c) {
            const SenselContact& sc = f->contacts[c];
            out.push_back(Contact{ sc.id, (int)sc.state, sc.x_pos, sc.y_pos,
                                   sc.total_force, sc.area, sc.orientation });
        }
    }
    return (int)out.size();
}

void Morph::setLed(int idx, float v01) {
    if (idx < 0 || idx >= numLeds_) return;
    if (v01 < 0) v01 = 0; if (v01 > 1) v01 = 1;
    uint16_t b = (uint16_t)std::lround(v01 * maxBright_);
    if (b == ledState_[idx]) return;          // diff: only send on change
    ledState_[idx] = b;
    senselSetLEDBrightness((SENSEL_HANDLE)handle_, (unsigned char)idx, b);
}

void Morph::flushLeds() {}

void Morph::close() {
    if (!handle_) return;
    SENSEL_HANDLE h = (SENSEL_HANDLE)handle_;
    for (int i = 0; i < numLeds_; ++i) senselSetLEDBrightness(h, (unsigned char)i, 0);
    senselStopScanning(h);
    if (frame_) senselFreeFrameData(h, (SenselFrameData*)frame_);
    senselClose(h);
    handle_ = frame_ = nullptr;
}

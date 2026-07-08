// Thin C++ wrapper over the Sensel C API: contacts in, LEDs out.
//
// LED writes are batched: setLed() only stages values; flushLeds() sends the
// whole strip in ONE serial round-trip, rate-limited. (The lib's per-LED call
// transmits the entire array every time and costs ~4.5 ms while scanning —
// naive per-LED writes collapse a 250 Hz control loop to ~20 Hz.)
#pragma once
#include <chrono>
#include <vector>
#include <cstdint>

namespace sensel {

struct Contact {
    int   id;
    int   state;        // SenselContactState (1=start, 2=move, 3=end)
    float x, y;         // mm
    float force;        // grams
    float area;         // sensor elements
    float orientation;  // degrees (ellipse twist, -90..90)
};

class Morph {
public:
    bool  open();                              // self-healing auto-scan + open
    void  startContacts();                     // enable contact frames + scanning
    int   poll(std::vector<Contact>& out);     // -1 = no new frame, else # contacts
    void  setLed(int idx, float v01);          // stage 0..1 brightness (quantized)
    void  flushLeds();                         // one bulk write, rate-limited (~20 Hz)
    void  close();

    float widthMm()  const { return width_;  }
    float heightMm() const { return height_; }
    int   numLeds()  const { return numLeds_; }

private:
    void* handle_ = nullptr;       // SENSEL_HANDLE
    void* frame_  = nullptr;       // SenselFrameData*
    float width_ = 0, height_ = 0;
    int   numLeds_ = 0;
    uint16_t maxBright_ = 0;
    int   ledRegSize_ = 1;         // bytes per LED in the device register
    std::vector<uint8_t> ledArr_;  // staged brightness bytes (device format)
    bool  ledDirty_ = false;
    std::chrono::steady_clock::time_point lastLedSend_{};
};

} // namespace sensel

// Thin C++ wrapper over the Sensel C API: contacts in, LEDs out.
#pragma once
#include <vector>
#include <cstdint>

struct Contact {
    int   id;
    int   state;        // SenselContactState
    float x, y;         // mm
    float force;        // grams
    float area;         // sensor elements
    float orientation;  // degrees (ellipse twist)
};

class Morph {
public:
    bool  open();                              // auto-scan + open first device
    void  startContacts();                     // enable contact frames + scanning
    int   poll(std::vector<Contact>& out);     // returns # contacts in latest frame
    void  setLed(int idx, float v01);          // 0..1 brightness, diffed internally
    void  flushLeds();                         // push pending LED changes (no-op here; setLed sends)
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
    std::vector<uint16_t> ledState_;
};

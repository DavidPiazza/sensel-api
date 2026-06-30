// Real-time granular voice pool. The audio thread owns everything here; the
// control thread only ever calls push() (lock-free). No allocation, locking,
// or I/O happens on the audio thread.
#pragma once
#include <vector>
#include <cstdint>
#include <readerwriterqueue.h>

class Corpus;

struct GrainTrigger {
    uint32_t grain;   // index into Corpus grain table
    float    gain;    // 0..1   (from contact force)
    float    rate;    // playback speed / transpose (from orientation)
    float    pan;     // 0..1   (from x)
};

class Engine {
public:
    explicit Engine(const Corpus* corpus, int sampleRate);

    // Control thread -> audio thread. Returns false if the queue is full.
    bool push(const GrainTrigger& t) { return queue_.try_enqueue(t); }

    // Called from the CoreAudio real-time callback. Interleaved stereo.
    void render(float* out, uint32_t frames);

private:
    struct Voice {
        const float* src = nullptr;
        uint32_t     len = 0;
        double       pos = 0.0;
        double       rate = 1.0;
        float        gain = 0.f, pan = 0.5f;
        uint32_t     age = 0, dur = 0;
        bool         active = false;
    };

    void spawn(const GrainTrigger& t);

    const Corpus* corpus_;
    int           sampleRate_;
    uint32_t      playDur_;                 // grain length in samples
    std::vector<Voice>            pool_;
    std::vector<float>            window_;  // Hann LUT
    moodycamel::ReaderWriterQueue<GrainTrigger> queue_;
};

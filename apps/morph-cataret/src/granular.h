// Real-time granular voice pool. The audio thread owns everything here; the
// control thread only ever calls push() (lock-free). No allocation, locking,
// or I/O happens on the audio thread.
#pragma once
#include <atomic>
#include <vector>
#include <cstdint>
#include <readerwriterqueue.h>

class Corpus;

struct GrainTrigger {
    uint32_t grain;       // index into Corpus grain table
    float    gain;        // 0..1   (from contact force)
    float    rate;        // playback speed / transpose (from orientation)
    float    pan;         // 0..1   (from x, + spray)
    uint32_t delay;       // sample-accurate onset pre-roll (async scheduling)
    uint32_t readOffset;  // samples into the source grain (position spray)
    uint32_t durSamples;  // per-grain length — randomized to decorrelate deaths
};

class Engine {
public:
    explicit Engine(const Corpus* corpus, int sampleRate);

    // Control thread -> audio thread. Returns false if the queue is full.
    bool push(const GrainTrigger& t) { return queue_.try_enqueue(t); }

    // Called from the CoreAudio real-time callback. Interleaved stereo.
    void render(float* out, uint32_t frames);

    // Diagnostics (read from the control thread). active = voices sounding last
    // block; takeSteals = steals since the previous call (resets the counter).
    uint32_t activeVoices() const { return active_.load(std::memory_order_relaxed); }
    uint32_t takeSteals()         { return steals_.exchange(0, std::memory_order_relaxed); }

private:
    struct Voice {
        const float* src = nullptr;
        uint32_t     len = 0;
        double       pos = 0.0;
        double       rate = 1.0;
        double       winPhase = 0.0, winInc = 0.0;  // window LUT phase (divide-free)
        float        gain = 0.f, pan = 0.5f;
        uint32_t     age = 0, dur = 0;
        uint32_t     delay = 0;                      // silent pre-roll (samples)
        bool         active = false;
    };

    void spawn(const GrainTrigger& t);

    const Corpus* corpus_;
    int           sampleRate_;
    uint32_t      playDur_;                 // grain length in samples
    int           cursor_ = 0;              // round-robin allocation hint
    std::vector<Voice>            pool_;
    std::vector<float>            window_;  // Hann LUT
    moodycamel::ReaderWriterQueue<GrainTrigger> queue_;
    std::atomic<uint32_t>         active_{0};   // voices sounding last block
    std::atomic<uint32_t>         steals_{0};   // steals since last takeSteals()
};

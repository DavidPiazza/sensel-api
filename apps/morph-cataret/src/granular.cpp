#include "granular.h"
#include "corpus.h"
#include <cmath>
#include <algorithm>

static constexpr int   MAX_VOICES   = 256;
static constexpr float GRAIN_MS      = 75.f;   // playback grain length
static constexpr int   WINDOW_LUT_N  = 4096;

Engine::Engine(const Corpus* corpus, int sampleRate)
    : corpus_(corpus), sampleRate_(sampleRate), queue_(1024)
{
    playDur_ = (uint32_t)(GRAIN_MS * 0.001f * sampleRate_);
    pool_.resize(MAX_VOICES);
    window_.resize(WINDOW_LUT_N);
    for (int i = 0; i < WINDOW_LUT_N; ++i)
        window_[i] = 0.5f - 0.5f * std::cos(2.0 * M_PI * i / (WINDOW_LUT_N - 1));
}

static inline float cubic(const float* x, uint32_t len, double pos) {
    int i  = (int)pos;
    float f = (float)(pos - i);
    float xm = (i > 0)        ? x[i - 1] : x[i];
    float x0 = x[i];
    float x1 = (i + 1 < (int)len) ? x[i + 1] : x[i];
    float x2 = (i + 2 < (int)len) ? x[i + 2] : x1;
    // Catmull-Rom
    return x0 + 0.5f * f * (x1 - xm + f * (2*xm - 5*x0 + 4*x1 - x2 + f * (3*(x0 - x1) + x2 - xm)));
}

void Engine::spawn(const GrainTrigger& t) {
    auto seg = corpus_->grainSource(t.grain);   // {ptr, len} into resident audio
    if (!seg.first || seg.second < 4) return;
    for (Voice& v : pool_) {
        if (v.active) continue;
        v.src = seg.first;
        v.len = std::min<uint32_t>(seg.second, playDur_);
        v.pos = 0.0;
        v.rate = t.rate;
        v.gain = t.gain;
        v.pan  = t.pan;
        v.age = 0;
        v.dur = v.len;
        v.active = true;
        return;
    }
    // pool exhausted: drop the grain (RT-safe: never block)
}

void Engine::render(float* out, uint32_t frames) {
    GrainTrigger t;
    while (queue_.try_dequeue(t)) spawn(t);

    std::fill(out, out + frames * 2, 0.f);

    for (Voice& v : pool_) {
        if (!v.active) continue;
        for (uint32_t i = 0; i < frames; ++i) {
            uint32_t wi = (uint32_t)((uint64_t)v.age * WINDOW_LUT_N / v.dur);
            float env = window_[std::min<uint32_t>(wi, WINDOW_LUT_N - 1)];
            float s   = env * v.gain * cubic(v.src, v.len, v.pos);
            out[2*i]     += s * (1.f - v.pan);
            out[2*i + 1] += s * v.pan;
            v.pos += v.rate;
            if (++v.age >= v.dur || v.pos >= v.len - 2) { v.active = false; break; }
        }
    }

    // gentle soft clip
    for (uint32_t i = 0; i < frames * 2; ++i)
        out[i] = std::tanh(out[i]);
}

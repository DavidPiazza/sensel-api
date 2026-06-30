#include "granular.h"
#include "corpus.h"
#include <cmath>
#include <algorithm>

static constexpr int   MAX_VOICES   = 1024;
static constexpr float GRAIN_MS      = 75.f;   // playback grain length
static constexpr int   WINDOW_LUT_N  = 4096;

Engine::Engine(const Corpus* corpus, int sampleRate)
    : corpus_(corpus), sampleRate_(sampleRate), queue_(4096)
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

    const int n = (int)pool_.size();
    int chosen = -1;
    // round-robin search for a free voice (cheap, avoids always-from-0 clustering)
    for (int s = 0; s < n; ++s) {
        int i = cursor_ + s; if (i >= n) i -= n;
        if (!pool_[i].active) { chosen = i; cursor_ = (i + 1 < n) ? i + 1 : 0; break; }
    }
    // pool full: steal the oldest grain. Its Hann envelope is near zero, so the
    // cut is nearly click-free — graceful degradation instead of a dropout.
    if (chosen < 0) {
        uint32_t bestAge = 0;
        for (int i = 0; i < n; ++i)
            if (pool_[i].age >= bestAge) { bestAge = pool_[i].age; chosen = i; }
    }

    // position spray: read from a randomized offset into the source grain
    uint32_t avail = seg.second;
    uint32_t ro = (avail > playDur_) ? std::min(t.readOffset, avail - playDur_) : 0;

    Voice& v = pool_[chosen];
    v.src = seg.first + ro;
    v.len = std::min<uint32_t>(avail - ro, playDur_);
    v.pos = 0.0;
    v.rate = t.rate;
    v.gain = t.gain;
    v.pan  = t.pan;
    v.age = 0;
    v.dur = v.len;
    v.delay = t.delay;
    v.winPhase = 0.0;
    v.winInc = (double)WINDOW_LUT_N / (double)v.dur;
    v.active = true;
}

void Engine::render(float* out, uint32_t frames) {
    GrainTrigger t;
    while (queue_.try_dequeue(t)) spawn(t);

    std::fill(out, out + frames * 2, 0.f);

    for (Voice& v : pool_) {
        if (!v.active) continue;
        const float gl = v.gain * (1.f - v.pan);
        const float gr = v.gain * v.pan;
        uint32_t i = 0;
        // consume the silent pre-roll (may span the whole buffer)
        if (v.delay > 0) {
            uint32_t d = std::min<uint32_t>(v.delay, frames);
            v.delay -= d; i = d;
            if (v.delay > 0) continue;
        }
        for (; i < frames; ++i) {
            int wi = (int)v.winPhase;
            float env = window_[wi < WINDOW_LUT_N ? wi : WINDOW_LUT_N - 1];
            float s   = env * cubic(v.src, v.len, v.pos);
            out[2*i]     += s * gl;
            out[2*i + 1] += s * gr;
            v.pos += v.rate;
            v.winPhase += v.winInc;
            if (++v.age >= v.dur || v.pos >= v.len - 2) { v.active = false; break; }
        }
    }

    // gentle soft clip
    for (uint32_t i = 0; i < frames * 2; ++i)
        out[i] = std::tanh(out[i]);
}

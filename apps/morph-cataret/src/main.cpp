// morph-cataret — a polyphonic concatenative pad for the Sensel Morph.
//
//   morph-cataret <folder-of-audio>
//
// Drag a folder of audio in; it is sliced into grains, reduced to a 2D atlas
// (PCA over spectral descriptors), and laid on the pad. Each finger queries the
// nearest grains (k-NN); force -> grain density & gain, area -> neighbourhood
// size, x -> pan, ellipse orientation -> transpose. The LED strip shows a comet
// under each finger.
#include "corpus.h"
#include "granular.h"
#include "morph.h"

#include <miniaudio.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

static constexpr int SAMPLE_RATE = 48000;

static std::atomic<bool> g_running{true};
static void onSigint(int) { g_running = false; }

static void audioCallback(ma_device* dev, void* out, const void*, ma_uint32 frames) {
    static_cast<Engine*>(dev->pUserData)->render(static_cast<float*>(out), frames);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s <folder-of-audio>\n", argv[0]);
        return 1;
    }
    std::signal(SIGINT, onSigint);

    Corpus corpus;
    std::printf("analyzing corpus in '%s'...\n", argv[1]);
    if (!corpus.build(argv[1], SAMPLE_RATE)) return 1;

    Engine engine(&corpus, SAMPLE_RATE);

    // --- audio device ---
    ma_device_config dc = ma_device_config_init(ma_device_type_playback);
    dc.playback.format   = ma_format_f32;
    dc.playback.channels = 2;
    dc.sampleRate        = SAMPLE_RATE;
    dc.dataCallback      = audioCallback;
    dc.pUserData         = &engine;
    ma_device device;
    if (ma_device_init(nullptr, &dc, &device) != MA_SUCCESS) {
        std::printf("failed to open audio device\n"); return 1;
    }
    ma_device_start(&device);

    // --- Morph ---
    Morph morph;
    if (!morph.open()) {
        std::printf("no Sensel Morph found\n");
        ma_device_uninit(&device);
        return 1;
    }
    morph.startContacts();
    std::printf("playing. touch the Morph. ctrl-c to quit.\n");

    const float W = morph.widthMm(), H = morph.heightMm();
    const int   nLeds = morph.numLeds();
    std::vector<Contact> contacts;
    std::unordered_map<int, std::chrono::steady_clock::time_point> lastEmit;
    std::vector<float> ledTarget(nLeds, 0.f);
    std::mt19937 rng(0xC0FFEE);
    std::uniform_real_distribution<float> uni(0.f, 1.f);

    const bool meter = std::getenv("MORPH_METER") != nullptr;   // MORPH_METER=1 to enable
    auto lastMeter = std::chrono::steady_clock::now();

    while (g_running) {
        morph.poll(contacts);
        auto now = std::chrono::steady_clock::now();
        std::fill(ledTarget.begin(), ledTarget.end(), 0.f);

        for (const Contact& c : contacts) {
            float qx = (W > 0) ? c.x / W : 0.5f;
            float qy = (H > 0) ? c.y / H : 0.5f;
            qx = std::min(1.f, std::max(0.f, qx));
            qy = std::min(1.f, std::max(0.f, qy));

            float f    = std::min(1.f, c.force / 1500.f);
            float gain = f;                                        // grams -> gain
            int   k    = std::min(8, std::max(1, 1 + (int)(c.area / 20.f)));
            float rate = std::pow(2.f, ((c.orientation - 90.f) / 90.f) * (5.f / 12.f));

            // Asynchronous scheduling: jitter the inter-onset interval so the
            // grain train is aperiodic (no pitched tone at high density), and
            // give each grain a sample-accurate random pre-roll so onsets don't
            // quantize to the poll/buffer grid.
            float baseMs = 60.f - 52.f * f;                        // 60ms..8ms
            uint32_t intervalSamp = (uint32_t)(baseMs * 0.001f * SAMPLE_RATE);
            float jittered = baseMs * (1.f + 0.6f * (uni(rng) * 2.f - 1.f));  // +/-60%
            auto interval = std::chrono::milliseconds((int)std::max(2.f, jittered));

            auto& t = lastEmit[c.id];
            if (t.time_since_epoch().count() == 0 || now - t >= interval) {
                t = now;
                uint32_t idx[8];
                int found = corpus.knn(qx, qy, k, idx);
                for (int i = 0; i < found; ++i) {
                    uint32_t delay = (uint32_t)(uni(rng) * intervalSamp);        // spread across the gap
                    uint32_t roff  = (uint32_t)(uni(rng) * 1500.f);              // ~31ms position spray
                    float    pan   = std::min(1.f, std::max(0.f, qx + (uni(rng) - 0.5f) * 0.2f));
                    uint32_t durS  = (uint32_t)((50.f + uni(rng) * 60.f) * 0.001f * SAMPLE_RATE);  // 50..110ms
                    engine.push(GrainTrigger{ idx[i], gain / std::sqrt((float)found),
                                              rate, pan, delay, roff, durS });
                }
            }

            int led = (int)std::lround(qx * (nLeds - 1));
            if (led >= 0 && led < nLeds) ledTarget[led] = std::max(ledTarget[led], gain);
        }

        for (int i = 0; i < nLeds; ++i) morph.setLed(i, ledTarget[i]);

        if (meter && now - lastMeter >= std::chrono::seconds(1)) {
            std::fprintf(stderr, "[meter] %4u voices  %5u steals/s  %2zu contacts\n",
                         engine.activeVoices(), engine.takeSteals(), contacts.size());
            lastMeter = now;
        }

        // poll() blocks ~one frame period (up to ~4ms at 250Hz), so this paces itself.
        std::this_thread::yield();
    }

    std::printf("\nshutting down...\n");
    morph.close();
    ma_device_uninit(&device);
    return 0;
}

#include "descriptors.h"
#include <cmath>
#include <algorithm>

#ifdef __APPLE__
#include <Accelerate/Accelerate.h>
#endif

Descriptors::Descriptors() {
    win_.resize(N);
    for (int i = 0; i < N; ++i)
        win_[i] = 0.5f - 0.5f * std::cos(2.0 * M_PI * i / (N - 1));
    re_.resize(N / 2);
    im_.resize(N / 2);
    mag_.resize(N / 2);
    scratch_.resize(N);
#ifdef __APPLE__
    fftSetup_ = vDSP_create_fftsetup((vDSP_Length)std::log2((double)N), kFFTRadix2);
#endif
}

Descriptors::~Descriptors() {
#ifdef __APPLE__
    if (fftSetup_) vDSP_destroy_fftsetup((FFTSetup)fftSetup_);
#endif
}

void Descriptors::analyze(const float* x, size_t n, float* out) {
    const int half = N / 2;
    // window + zero-pad into scratch
    size_t m = std::min<size_t>(n, N);
    double rms = 0.0;
    long   zc  = 0;
    for (size_t i = 0; i < m; ++i) {
        scratch_[i] = x[i] * win_[i];
        rms += (double)x[i] * x[i];
        if (i > 0 && ((x[i] >= 0) != (x[i - 1] >= 0))) ++zc;
    }
    for (size_t i = m; i < (size_t)N; ++i) scratch_[i] = 0.f;
    rms = std::sqrt(rms / std::max<size_t>(1, m));

#ifdef __APPLE__
    DSPSplitComplex sc{ re_.data(), im_.data() };
    vDSP_ctoz((const DSPComplex*)scratch_.data(), 2, &sc, 1, half);
    vDSP_fft_zrip((FFTSetup)fftSetup_, &sc, 1, (vDSP_Length)std::log2((double)N), FFT_FORWARD);
    vDSP_zvabs(&sc, 1, mag_.data(), 1, half);   // magnitude
#else
    // Naive DFT magnitude (used off-Apple; analysis is one-time at load).
    for (int k = 0; k < half; ++k) {
        double sr = 0, si = 0;
        for (int t = 0; t < N; ++t) {
            double a = -2.0 * M_PI * k * t / N;
            sr += scratch_[t] * std::cos(a);
            si += scratch_[t] * std::sin(a);
        }
        mag_[k] = (float)std::sqrt(sr * sr + si * si);
    }
#endif

    // Spectral descriptors over the magnitude spectrum.
    double sum = 0, wsum = 0, logsum = 0;
    for (int k = 0; k < half; ++k) {
        float mg = mag_[k];
        sum    += mg;
        wsum   += (double)k * mg;
        logsum += std::log(mg + 1e-9f);
    }
    double centroid = (sum > 0) ? wsum / sum / half : 0.0;          // 0..1
    double amean    = sum / half;
    double gmean    = std::exp(logsum / half);
    double flatness = (amean > 0) ? gmean / amean : 0.0;            // 0..1

    // 85% spectral rolloff
    double thresh = 0.85 * sum, acc = 0;
    int rolloffBin = 0;
    for (int k = 0; k < half; ++k) { acc += mag_[k]; if (acc >= thresh) { rolloffBin = k; break; } }
    double rolloff = (double)rolloffBin / half;                    // 0..1
    double zcr     = (double)zc / std::max<size_t>(1, m);          // 0..1

    out[0] = (float)rms;
    out[1] = (float)centroid;
    out[2] = (float)flatness;
    out[3] = (float)rolloff;
    out[4] = (float)zcr;
}

// Per-grain audio descriptors. Returns a fixed-length feature vector used to
// build the 2D atlas. FFT via Accelerate/vDSP on macOS, naive DFT elsewhere.
#pragma once
#include <vector>
#include <cstddef>

class Descriptors {
public:
    static constexpr int N = 2048;        // analysis window (power of two)
    static constexpr int NUM_FEATURES = 5;  // rms, centroid, flatness, rolloff, zcr

    Descriptors();
    ~Descriptors();

    // Analyze up to N samples starting at x; writes NUM_FEATURES into out.
    void analyze(const float* x, size_t n, float* out);

private:
    std::vector<float> win_;     // Hann analysis window
    std::vector<float> re_, im_, mag_, scratch_;
    void* fftSetup_ = nullptr;   // FFTSetup on Apple, nullptr otherwise
};

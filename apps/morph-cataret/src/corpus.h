// The sound corpus: decoded audio held in RAM, sliced into grains, each grain
// reduced to a 2D atlas coordinate (PCA over descriptors) and indexed for
// nearest-neighbour query.
#pragma once
#include <vector>
#include <array>
#include <string>
#include <utility>
#include <cstdint>
#include <memory>

class Corpus {
public:
    bool build(const std::string& folder, int sampleRate);
    int  size() const { return (int)grains_.size(); }

    // RT-safe: (pointer, length) into resident audio for a grain. Safe to call
    // from the audio thread — buffers_ never reallocates after build().
    std::pair<const float*, uint32_t> grainSource(uint32_t idx) const;

    // k nearest grains to normalized atlas point (x,y in [0,1]).
    int knn(float x, float y, int k, uint32_t* outIdx) const;

    const std::vector<std::array<float, 2>>& points() const { return pts_; }

private:
    struct GrainRef { uint32_t buffer; uint32_t start; uint32_t len; };
    std::vector<std::vector<float>>  buffers_;   // resident mono audio @ sampleRate
    std::vector<GrainRef>            grains_;
    std::vector<std::array<float,2>> pts_;       // normalized atlas coords

    struct Index;                                // nanoflann (pImpl)
    std::shared_ptr<Index> index_;
};

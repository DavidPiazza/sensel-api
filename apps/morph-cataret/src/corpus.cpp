#include "corpus.h"
#include "descriptors.h"

#include <miniaudio.h>          // declarations only; impl is in miniaudio_impl.cpp
#include <nanoflann.hpp>
#include <Eigen/Dense>

#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <cctype>

namespace fs = std::filesystem;

// ---- nanoflann adaptor (pImpl) -------------------------------------------
struct PointCloud {
    const std::vector<std::array<float, 2>>* pts = nullptr;
    inline size_t kdtree_get_point_count() const { return pts->size(); }
    inline float  kdtree_get_pt(size_t i, size_t d) const { return (*pts)[i][d]; }
    template <class BBOX> bool kdtree_get_bbox(BBOX&) const { return false; }
};
using KDTree = nanoflann::KDTreeSingleIndexAdaptor<
    nanoflann::L2_Simple_Adaptor<float, PointCloud>, PointCloud, 2>;

struct Corpus::Index {
    PointCloud cloud;
    std::unique_ptr<KDTree> tree;
};

// ---- helpers --------------------------------------------------------------
static bool isAudio(const fs::path& p) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c){ return std::tolower(c); });
    return e == ".wav" || e == ".flac" || e == ".mp3" || e == ".aif" ||
           e == ".aiff" || e == ".ogg";
}

static bool decodeMono(const std::string& path, int sr, std::vector<float>& out) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, (ma_uint32)sr);
    ma_decoder dec;
    if (ma_decoder_init_file(path.c_str(), &cfg, &dec) != MA_SUCCESS) return false;
    out.clear();
    float tmp[4096];
    for (;;) {
        ma_uint64 read = 0;
        if (ma_decoder_read_pcm_frames(&dec, tmp, 4096, &read) != MA_SUCCESS) break;
        if (read == 0) break;
        out.insert(out.end(), tmp, tmp + read);
    }
    ma_decoder_uninit(&dec);
    return !out.empty();
}

// ---- build ----------------------------------------------------------------
bool Corpus::build(const std::string& folder, int sampleRate) {
    const uint32_t HOP = (uint32_t)(0.050 * sampleRate);          // 50 ms analysis hop
    const uint32_t WIN = (uint32_t)Descriptors::N;
    Descriptors desc;
    std::vector<float> feats;                                     // row-major N x F
    int nFiles = 0;

    for (const auto& entry : fs::recursive_directory_iterator(folder)) {
        if (!entry.is_regular_file() || !isAudio(entry.path())) continue;
        std::vector<float> samples;
        if (!decodeMono(entry.path().string(), sampleRate, samples)) continue;
        uint32_t b = (uint32_t)buffers_.size();
        buffers_.push_back(std::move(samples));
        const auto& buf = buffers_.back();
        ++nFiles;
        for (uint32_t s = 0; s + WIN <= buf.size(); s += HOP) {
            float f[Descriptors::NUM_FEATURES];
            desc.analyze(&buf[s], WIN, f);
            grains_.push_back({ b, s, (uint32_t)(buf.size() - s) });
            feats.insert(feats.end(), f, f + Descriptors::NUM_FEATURES);
        }
    }

    int N = (int)grains_.size();
    int F = Descriptors::NUM_FEATURES;
    std::printf("corpus: %d files, %d grains\n", nFiles, N);
    if (N < 3) { std::printf("not enough grains to build an atlas\n"); return false; }

    // PCA -> 2D
    Eigen::MatrixXf X(N, F);
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < F; ++j) X(i, j) = feats[i * F + j];

    Eigen::RowVectorXf mean = X.colwise().mean();
    X.rowwise() -= mean;
    Eigen::RowVectorXf stdv = (X.array().square().colwise().sum() / (N - 1)).sqrt();
    for (int j = 0; j < F; ++j) if (stdv(j) > 1e-6f) X.col(j) /= stdv(j);

    Eigen::MatrixXf cov = (X.adjoint() * X) / float(N - 1);
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXf> es(cov);
    Eigen::MatrixXf W = es.eigenvectors().rightCols(2);           // two largest eigenvalues
    Eigen::MatrixXf P = X * W;                                    // N x 2

    // normalize each axis to [0,1]
    pts_.resize(N);
    for (int d = 0; d < 2; ++d) {
        float lo = P.col(d).minCoeff(), hi = P.col(d).maxCoeff();
        float span = (hi - lo) > 1e-9f ? (hi - lo) : 1.f;
        for (int i = 0; i < N; ++i) pts_[i][d] = (P(i, d) - lo) / span;
    }

    index_ = std::make_shared<Index>();
    index_->cloud.pts = &pts_;
    index_->tree = std::make_unique<KDTree>(2, index_->cloud,
                       nanoflann::KDTreeSingleIndexAdaptorParams(10));
    index_->tree->buildIndex();
    return true;
}

std::pair<const float*, uint32_t> Corpus::grainSource(uint32_t idx) const {
    if (idx >= grains_.size()) return { nullptr, 0 };
    const GrainRef& g = grains_[idx];
    return { &buffers_[g.buffer][g.start], g.len };
}

int Corpus::knn(float x, float y, int k, uint32_t* outIdx) const {
    if (!index_ || !index_->tree) return 0;
    float query[2] = { x, y };
    std::vector<uint32_t>  idx(k);
    std::vector<float>     dist(k);
    nanoflann::KNNResultSet<float, uint32_t> rs(k);
    rs.init(idx.data(), dist.data());
    index_->tree->findNeighbors(rs, query, nanoflann::SearchParameters());
    int found = (int)rs.size();
    for (int i = 0; i < found; ++i) outIdx[i] = idx[i];
    return found;
}

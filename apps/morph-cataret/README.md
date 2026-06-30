# morph-cataret

A polyphonic concatenative-synthesis pad for the [Sensel Morph](https://sensel.com),
built natively for Apple Silicon. Drag in a folder of audio; it's sliced into
grains, reduced to a 2D atlas (PCA over spectral descriptors), and laid across
the pad. Each finger queries the nearest grains (k-NN) and plays them back
granularly — so multiple fingers = polyphonic retrieval from the sound corpus.

This is the "smallest interesting first" build: contacts only (no decompression
lib needed), so it runs **natively on arm64**.

## Mapping

| Morph dimension        | Controls                                   |
|------------------------|--------------------------------------------|
| finger `x, y`          | position in the descriptor atlas (query)   |
| `total_force` (grams)  | grain density + gain                       |
| `area`                 | k / neighbourhood size (point → cloud)     |
| ellipse `orientation`  | transpose (±5 semitones)                   |
| `x`                    | stereo pan                                 |
| LED strip (24)         | a "comet" under each finger                |

## Architecture

Three threads, one rule — *the audio thread never allocates, locks, or does I/O*:

- **Load (once):** decode → slice → descriptors (vDSP FFT) → PCA (Eigen) → k-d tree (nanoflann).
- **Control (~250 Hz):** read contacts → map → k-NN → push triggers (lock-free) → drive LEDs.
- **Audio (CoreAudio RT):** drain triggers → render windowed grains from a fixed voice pool.

## Build

Requires CMake, a C++17 compiler, and network access on first configure
(dependencies are fetched: miniaudio, nanoflann, readerwriterqueue, Eigen).

```sh
cd apps/morph-cataret
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/morph-cataret /path/to/audio/folder
```

The Sensel core library is built from `../../sensel-lib` in this repo — no
system install required.

## Roadmap

- Swap PCA → UMAP (umappp) for perceptually-truer neighbourhoods.
- Onset-based slicing (aubio) instead of fixed hop.
- Accelerometer tilt → global filter / atlas rotation.
- Optional RAVE/DDSP latent as the sound source instead of a fixed corpus.

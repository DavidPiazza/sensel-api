cmake_minimum_required(VERSION 3.16)
project(morph_cataret CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# --- The Sensel core library (built from the repo this app lives in) ---
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../sensel-lib ${CMAKE_BINARY_DIR}/sensel-lib)

# --- Header-only third-party deps, fetched at configure time ---
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.21)
FetchContent_Declare(nanoflann
    GIT_REPOSITORY https://github.com/jlblancoc/nanoflann.git
    GIT_TAG v1.5.5)
FetchContent_Declare(readerwriterqueue
    GIT_REPOSITORY https://github.com/cameron314/readerwriterqueue.git
    GIT_TAG v1.0.6)
FetchContent_Declare(eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG 3.4.0)

# Eigen/nanoflann ship CMake we don't want to fully configure; just grab sources.
FetchContent_MakeAvailable(readerwriterqueue)
FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED)
    FetchContent_Populate(miniaudio)
endif()
FetchContent_GetProperties(nanoflann)
if(NOT nanoflann_POPULATED)
    FetchContent_Populate(nanoflann)
endif()
FetchContent_GetProperties(eigen)
if(NOT eigen_POPULATED)
    FetchContent_Populate(eigen)
endif()

add_executable(morph-cataret
    src/main.cpp
    src/corpus.cpp
    src/descriptors.cpp
    src/granular.cpp
    src/morph.cpp
    src/miniaudio_impl.cpp)

target_include_directories(morph-cataret PRIVATE
    ${miniaudio_SOURCE_DIR}
    ${nanoflann_SOURCE_DIR}/include
    ${eigen_SOURCE_DIR})

target_link_libraries(morph-cataret PRIVATE sensel readerwriterqueue)

if(APPLE)
    target_link_libraries(morph-cataret PRIVATE
        "-framework Accelerate"     # vDSP FFT for descriptors
        "-framework CoreAudio" "-framework AudioToolbox" "-framework AudioUnit"
        "-framework CoreFoundation")
else()
    find_package(Threads REQUIRED)
    target_link_libraries(morph-cataret PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

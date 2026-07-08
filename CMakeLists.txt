cmake_minimum_required(VERSION 3.16)

project(sensel_api VERSION 0.8.3 LANGUAGES C CXX)

option(SENSEL_BUILD_CPP_BINDINGS "Build the Sensel Morph C++ wrapper" ON)

add_subdirectory(sensel-lib)

if(NOT TARGET sensel::sensel)
    add_library(sensel::sensel ALIAS sensel)
endif()

if(SENSEL_BUILD_CPP_BINDINGS)
    add_library(sensel_morph STATIC
        src/sensel/morph.cpp)
    add_library(sensel::morph ALIAS sensel_morph)

    target_compile_features(sensel_morph PUBLIC cxx_std_17)
    target_include_directories(sensel_morph PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
    target_link_libraries(sensel_morph PUBLIC sensel::sensel)

    if(NOT WIN32)
        target_compile_options(sensel_morph PRIVATE -Wall)
    endif()
endif()

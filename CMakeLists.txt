cmake_minimum_required(VERSION 3.16)

project(sensel_api VERSION 0.8.3 LANGUAGES C CXX)

option(SENSEL_BUILD_CPP_BINDINGS "Build the Sensel Morph C++ wrapper" ON)
include(CTest)

add_subdirectory(sensel-lib)

if(NOT TARGET sensel::sensel)
    add_library(sensel::sensel ALIAS sensel)
endif()

if(SENSEL_BUILD_CPP_BINDINGS)
    add_library(sensel_morph_objects OBJECT
        src/sensel/morph.cpp)
    target_compile_features(sensel_morph_objects PUBLIC cxx_std_17)
    target_include_directories(sensel_morph_objects
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/sensel-lib/src)
    set_target_properties(sensel_morph_objects PROPERTIES
        POSITION_INDEPENDENT_CODE ON)

    add_library(sensel_morph STATIC
        $<TARGET_OBJECTS:sensel_morph_objects>
        src/sensel/port_recovery.cpp)
    add_library(sensel::morph ALIAS sensel_morph)

    target_compile_features(sensel_morph PUBLIC cxx_std_17)
    target_include_directories(sensel_morph PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
    target_link_libraries(sensel_morph PUBLIC sensel::sensel)

    if(NOT WIN32)
        target_compile_options(sensel_morph_objects PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(sensel_morph PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(BUILD_TESTING)
        add_library(sensel_morph_fake_backend STATIC
            tests/fake_sensel.cpp
            $<TARGET_OBJECTS:sensel_morph_objects>)
        add_library(sensel::morph_fake ALIAS sensel_morph_fake_backend)
        target_compile_features(sensel_morph_fake_backend PUBLIC cxx_std_17)
        target_include_directories(sensel_morph_fake_backend PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/sensel-lib/src
            ${CMAKE_CURRENT_SOURCE_DIR}/src/sensel
            ${CMAKE_CURRENT_SOURCE_DIR}/tests)

        add_executable(sensel_morph_tests
            tests/morph_tests.cpp)
        target_compile_features(sensel_morph_tests PRIVATE cxx_std_17)
        target_link_libraries(sensel_morph_tests PRIVATE sensel::morph_fake)
        if(NOT WIN32)
            target_compile_options(sensel_morph_fake_backend PRIVATE -Wall -Wextra -Wpedantic)
            target_compile_options(sensel_morph_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        add_test(NAME sensel_morph_tests COMMAND sensel_morph_tests)
    endif()
endif()

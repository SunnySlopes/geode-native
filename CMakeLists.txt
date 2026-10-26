cmake_minimum_required(VERSION 3.20)
project(geode_native LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_library(terrain_noise third_party/cubiomes/noise.c)
target_include_directories(terrain_noise PUBLIC third_party/cubiomes)
add_library(
    terrain_biomes src/structure_layout/biome_bridge.c third_party/cubiomes/generator.c
                   third_party/cubiomes/biomenoise.c third_party/cubiomes/biomes.c third_party/cubiomes/layers.c)
target_link_libraries(terrain_biomes PUBLIC terrain_noise)
add_library(
    geode_core
    src/geode.cpp
    src/search.cpp
    src/checkpoint.cpp
    src/coarse_gpu.cpp
    src/terrain.cpp
    src/carver.cpp
    src/structures.cpp
    src/structure_layout/layout.cpp
    src/structure_layout/climate263.cpp)
target_include_directories(
    geode_core
    PUBLIC src
    PRIVATE data src/structure_layout)
target_link_libraries(geode_core PUBLIC terrain_biomes)
add_executable(geode-finder src/main.cpp src/protocol.cpp)
target_include_directories(geode-finder PRIVATE third_party)
target_link_libraries(geode-finder PRIVATE geode_core)
if(MSVC)
    set_property(TARGET terrain_noise terrain_biomes geode_core geode-finder
                 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(terrain_noise PRIVATE /O2 /fp:strict)
    target_compile_options(terrain_biomes PRIVATE /O2 /fp:strict)
    target_compile_options(geode_core PRIVATE /O2 /fp:strict /utf-8)
    target_compile_options(geode-finder PRIVATE /utf-8)
else()
    target_compile_options(terrain_noise PRIVATE -O3 -fwrapv -ffp-contract=off)
    target_compile_options(terrain_biomes PRIVATE -O3 -fwrapv -ffp-contract=off)
    target_compile_options(geode_core PRIVATE -O3 -Wall -Wextra -ffp-contract=off)
    target_link_options(geode-finder PRIVATE -static -static-libgcc -static-libstdc++ -s)
    if(WIN32)
        target_link_options(geode-finder PRIVATE -municode)
    endif()
endif()
option(BUILD_TESTING "Build local regression tests" OFF)
include(CTest)
if(BUILD_TESTING)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit.cpp")
        message(FATAL_ERROR "Local tests/ is not included in this repository. Use -DBUILD_TESTING=OFF to build the engine.")
    endif()
    add_executable(geode-climate-check tests/climate_probe.cpp)
    target_link_libraries(geode-climate-check PRIVATE geode_core)
    if(MSVC)
        set_property(TARGET geode-climate-check PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        target_link_options(geode-climate-check PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_executable(geode-speed tools/speed_probe.cpp)
    target_link_libraries(geode-speed PRIVATE geode_core)
    if(MSVC)
        set_property(TARGET geode-speed PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        target_link_options(geode-speed PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_executable(geode-scan-check tools/scan_probe.cpp src/search.cpp)
    target_compile_definitions(geode-scan-check PRIVATE GEODE_SCAN_PROBE)
    target_link_libraries(geode-scan-check PRIVATE geode_core)
    if(MSVC)
        set_property(TARGET geode-scan-check PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        target_compile_options(geode-scan-check PRIVATE -O3 -ffp-contract=off)
        target_link_options(geode-scan-check PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_executable(geode-terrain-check tests/terrain_probe.cpp)
    target_link_libraries(geode-terrain-check PRIVATE geode_core)
    if(NOT MSVC)
        target_link_options(geode-terrain-check PRIVATE -static -static-libgcc -static-libstdc++)
    else()
        set_property(TARGET geode-terrain-check PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    add_executable(geode-unit tests/unit.cpp)
    target_link_libraries(geode-unit PRIVATE geode_core)
    if(MSVC)
        set_property(TARGET geode-unit PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        target_link_options(geode-unit PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME unit COMMAND geode-unit)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME checkpoint COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/checkpoint.py
                                         $<TARGET_FILE:geode-finder>)
        add_test(NAME versions26 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/version26_check.py
                                         $<TARGET_FILE:geode-finder> $<TARGET_FILE:geode-terrain-check>)
        add_test(NAME climate263 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/climate263_check.py
                                         $<TARGET_FILE:geode-climate-check>)
        add_test(NAME coarse COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/coarse_check.py
                                     $<TARGET_FILE:geode-scan-check>)
        add_test(NAME protocol COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/protocol.py
                                       $<TARGET_FILE:geode-finder>)
        add_test(NAME ranking COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/ranking.py
                                      $<TARGET_FILE:geode-finder>)
        add_test(NAME regression COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check.py
                                         $<TARGET_FILE:geode-finder>)
        add_test(NAME terrain COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/terrain_check.py
                                      $<TARGET_FILE:geode-terrain-check> $<TARGET_FILE:geode-finder>)
        add_test(NAME early-versions COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/early_version_check.py
                                             $<TARGET_FILE:geode-terrain-check> $<TARGET_FILE:geode-finder>)
        add_test(NAME world COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/world_check.py
                                    $<TARGET_FILE:geode-terrain-check> $<TARGET_FILE:geode-finder>)
        add_test(NAME hotspot-quality COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/hotspot_check.py
                                              $<TARGET_FILE:geode-finder>)
    endif()
endif()

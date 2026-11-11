cmake_minimum_required(VERSION 3.20)
project(geode_native LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(Threads REQUIRED)

set(standalone OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(standalone ON)
endif()
option(GEODE_BUILD_CLI "构建独立命令行与桌面协议程序" ${standalone})
option(GEODE_ENABLE_IPO "发布构建启用跨文件链接优化" ${GEODE_BUILD_CLI})
if(GEODE_ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_available OUTPUT ipo_reason)
    if(ipo_available)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    else()
        message(STATUS "链接优化不可用，保留普通优化：${ipo_reason}")
    endif()
endif()

include(third_party/cubiomes/cmake/CubiomesModules.cmake)
cubiomes_add_modules(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cubiomes"
    PREFIX GeodeCubiomes LOCK "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cubiomes-lock.json"
    MODULES Biomes Java263)

add_library(terrain_biomes STATIC src/structure_layout/biome_bridge.c)
target_link_libraries(terrain_biomes PUBLIC GeodeCubiomes::Biomes)
add_library(geode_core STATIC
    src/geode.cpp src/search.cpp src/checkpoint.cpp src/coarse_gpu.cpp
    src/terrain.cpp src/carver.cpp src/structures.cpp src/overwrites.cpp src/structure_cover.c
    src/structure_layout/layout.cpp src/structure_layout/climate263.cpp)
add_library(MinecraftFinders::Geode ALIAS geode_core)
target_include_directories(geode_core PUBLIC include src PRIVATE data src/structure_layout
    # search.cpp 需要要塞/矿井覆盖模块的版本常量。
    src/stronghold_118 src/mineshaft_gen)
target_compile_features(geode_core PUBLIC cxx_std_20)
target_link_libraries(geode_core PUBLIC terrain_biomes GeodeCubiomes::Java263 Threads::Threads)
target_compile_options(terrain_biomes PRIVATE -O3 -fwrapv -ffp-contract=off -ffunction-sections -fdata-sections)
target_compile_options(geode_core PRIVATE -O3 -Wall -Wextra -ffp-contract=off -ffunction-sections -fdata-sections)

# ---------------------------------------------------------------------------
# Stronghold End portal + layout simulation (1.18+).
#
# geode's cubiomes snapshot contains only the biomes/noise half (no finders.c,
# no features/), so this module carries its own placement logic plus a vendored
# copy of cubiomes' stronghold piece generator. It only consumes
# GeodeCubiomes::Biomes, which geode already links.
#
# Nothing in geode_core depends on it; consumers link GeodeStronghold::Sh118.
# ---------------------------------------------------------------------------
option(GEODE_BUILD_STRONGHOLD "Build the 1.18+ stronghold portal simulator" ON)
if(GEODE_BUILD_STRONGHOLD)
    add_subdirectory(src/stronghold_118)
    add_library(GeodeStronghold::Sh118 ALIAS geode_stronghold_118)

    # 自测：用内置基准值校验布局、数量与 unionArea。
    option(GEODE_BUILD_STRONGHOLD_SELFTEST "Build the stronghold module self-test" OFF)
    if(GEODE_BUILD_STRONGHOLD_SELFTEST)
        add_executable(sh118-selftest src/stronghold_118/sh118_selftest.c)
        target_link_libraries(sh118-selftest PRIVATE geode_stronghold_118)

        # 精确 Y 的专项验证（moveBelowSeaLevel 复现正确性）。
        add_executable(sh118-ycheck src/stronghold_118/sh118_ycheck.c)
        target_link_libraries(sh118-ycheck PRIVATE geode_stronghold_118 GeodeCubiomes::Biomes)
    endif()
endif()

# ---------------------------------------------------------------------------
# 废弃矿井 (Mineshaft) 生成模块。
#
# 纯 C11 + Java LCG, 不需要 cubiomes(矿井生成不涉及噪声地形)。
# 提供完整 piece 盒 / 入口盒 / 垂直偏移, 供 geode 需要矿井实际生成时调用。
#
# 注意: 见 src/mineshaft_gen/README.md —— 生成层与 Java 参考实现的对拍尚未
# 通过, 在完成第三方校验前不要把它当权威数据。
# ---------------------------------------------------------------------------
option(GEODE_BUILD_MINESHAFT "Build the mineshaft generation module" ON)
if(GEODE_BUILD_MINESHAFT)
    add_subdirectory(src/mineshaft_gen)
    add_library(GeodeMineshaft::Gen ALIAS geode_mineshaft)
endif()

# ---------------------------------------------------------------------------
# 结构覆盖接口 (要塞 / 废弃矿井)。
#
# 只回答"某方块坐标是否被该结构写入", 供紫水晶主引擎排除母岩。
# 约束见 docs/说明/要塞与矿井接入约束.md。
#
# 要塞无法给出精确世界 Y, 因此 covers() 按平面范围判定 —— 落在要塞平面内的
# 紫晶洞母岩一律不得计入下限。
# ---------------------------------------------------------------------------
option(GEODE_BUILD_STRUCTURE_COVER "Build the structure cover interface" ON)
if(GEODE_BUILD_STRUCTURE_COVER)
    # 纯 C 库, 依赖要塞与矿井两个模块。geode_core 公开链接它,
    # 这样 C++ 侧直接 include "structure_cover.h" 即可调用。
    add_library(geode_structure_cover STATIC src/structure_cover.c)
    add_library(GeodeStructureCover::Cover ALIAS geode_structure_cover)

    target_include_directories(geode_structure_cover PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_features(geode_structure_cover PUBLIC c_std_11)
    target_link_libraries(geode_structure_cover PUBLIC geode_stronghold_118 geode_mineshaft)
    if(MSVC)
        target_compile_options(geode_structure_cover PRIVATE /O2 /DNDEBUG)
    else()
        target_compile_options(geode_structure_cover PRIVATE
            -O3 -DNDEBUG -fwrapv -ffp-contract=off)
    endif()

    target_link_libraries(geode_core PUBLIC geode_structure_cover)

    option(GEODE_BUILD_COVER_SELFTEST "Build the structure cover self-test" OFF)
    if(GEODE_BUILD_COVER_SELFTEST)
        add_executable(cover-selftest src/structure_cover_selftest.c)
        target_link_libraries(cover-selftest PRIVATE geode_structure_cover)
    endif()
endif()

if(GEODE_BUILD_CLI)
    add_executable(geode-finder src/main.cpp src/protocol.cpp)
    target_include_directories(geode-finder PRIVATE third_party)
    target_link_libraries(geode-finder PRIVATE MinecraftFinders::Geode)
    target_link_options(geode-finder PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--gc-sections -s)
    if(WIN32)
        target_link_options(geode-finder PRIVATE -municode)
    endif()
endif()

option(GEODE_BUILD_EXAMPLE "构建直接调用算法库的示例" OFF)
if(GEODE_BUILD_EXAMPLE)
    add_executable(geode-example examples/basic.cpp)
    target_link_libraries(geode-example PRIVATE MinecraftFinders::Geode)
    if(WIN32)
        target_link_options(geode-example PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()

option(BUILD_TESTING "Build local regression tests" OFF)
include(CTest)
if(BUILD_TESTING AND GEODE_BUILD_CLI)
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
    add_executable(geode-speed test/speed_probe.cpp)
    target_link_libraries(geode-speed PRIVATE geode_core)
    if(MSVC)
        set_property(TARGET geode-speed PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        target_link_options(geode-speed PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_executable(geode-scan-check test/scan_probe.cpp src/search.cpp)
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
        add_test(NAME cli-config COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_config.py $<TARGET_FILE:geode-finder>)
        add_test(NAME checkpoint COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/checkpoint.py
                                         $<TARGET_FILE:geode-finder>)
        add_test(NAME ranges COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/range_check.py
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
        add_test(NAME version-sampling COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/version_sampling_check.py
                                               $<TARGET_FILE:geode-terrain-check> $<TARGET_FILE:geode-finder>)
        add_test(NAME world COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/world_check.py
                                    $<TARGET_FILE:geode-terrain-check> $<TARGET_FILE:geode-finder>)
        add_test(NAME hotspot-quality COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/hotspot_check.py
                                              $<TARGET_FILE:geode-finder>)
    endif()
endif()

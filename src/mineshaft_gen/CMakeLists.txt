# 废弃矿井生成模块 (geode 内嵌)
#
# 纯 C11, 无第三方依赖, 也不需要 cubiomes —— 矿井生成只用 Java LCG,
# 不涉及噪声地形。
add_library(geode_mineshaft STATIC
    mineshaft_gen.c
)

target_include_directories(geode_mineshaft PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_features(geode_mineshaft PUBLIC c_std_11)

if(MSVC)
    target_compile_options(geode_mineshaft PRIVATE /O2 /DNDEBUG)
else()
    target_compile_options(geode_mineshaft PRIVATE
        -O3 -DNDEBUG -fwrapv -ffp-contract=off
        -ffunction-sections -fdata-sections
        -Wall -Wextra
    )
endif()

# 集成冒烟测试: 证明接口可用、盒型合法、四版本结果一致。
option(GEODE_BUILD_MINESHAFT_SELFTEST "Build the mineshaft module self-test" OFF)
if(GEODE_BUILD_MINESHAFT_SELFTEST)
    add_executable(ms-selftest ms_selftest.c)
    target_link_libraries(ms-selftest PRIVATE geode_mineshaft)
endif()

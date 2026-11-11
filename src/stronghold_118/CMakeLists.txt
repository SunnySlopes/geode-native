# Stronghold End portal + layout simulation (1.18+).
#
# Self-contained: geode's cubiomes snapshot has only the biomes/noise half, so
# the stronghold placement and piece generator live in this module. The vendored
# generator is cubiomes' features/stronghold.c, taken verbatim (a faithful port
# of vanilla's StrongholdPieces), wrapped by sh_types.h.
#
# Only the biome generator from cubiomes is consumed — the same one geode
# already links for its terrain model — so this adds no new cubiomes modules.

add_library(geode_stronghold_118 STATIC
    sh1_18.c
    vendor/stronghold_pieces.c
)

target_include_directories(geode_stronghold_118
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/vendor"
)

target_compile_features(geode_stronghold_118 PUBLIC c_std_11)

# Same floating-point contract as the rest of the project: no FP contraction,
# so the layout matches the engine bit for bit.
if(MSVC)
    target_compile_options(geode_stronghold_118 PRIVATE /O2 /DNDEBUG)
else()
    target_compile_options(geode_stronghold_118 PRIVATE
        -O3 -DNDEBUG -fwrapv -ffp-contract=off
        -ffunction-sections -fdata-sections
    )
endif()

# cubiomes' biome generator, already provided by geode's module build.
target_link_libraries(geode_stronghold_118 PUBLIC GeodeCubiomes::Biomes)
if(NOT WIN32)
    target_link_libraries(geode_stronghold_118 PUBLIC m)
endif()

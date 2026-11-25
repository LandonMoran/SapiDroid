cmake_minimum_required(VERSION 3.22.1)
project(sapidroid C)

set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../engine)

set(ENGINE_SOURCES
    ${ENGINE_DIR}/zira_tts.c
    ${ENGINE_DIR}/zf1_ana.c
    ${ENGINE_DIR}/zf1_dat.c
    ${ENGINE_DIR}/zf1_engine.c
    ${ENGINE_DIR}/zf1_frag.c
    ${ENGINE_DIR}/zf1_fst.c
    ${ENGINE_DIR}/zf1_fstpm.c
    ${ENGINE_DIR}/zf1_lex.c
    ${ENGINE_DIR}/zf1_lts.c
    ${ENGINE_DIR}/zf1_modules.c
    ${ENGINE_DIR}/zf1_morph.c
    ${ENGINE_DIR}/zf1_poly.c
    ${ENGINE_DIR}/zf1_pos.c
    ${ENGINE_DIR}/zf1_post.c
    ${ENGINE_DIR}/zf1_pron.c
    ${ENGINE_DIR}/zf1_pron_oov.c
    ${ENGINE_DIR}/zf1_ss.c
    ${ENGINE_DIR}/zf1_tn.c
    ${ENGINE_DIR}/zf1_util.c
    ${ENGINE_DIR}/zf1_wb.c
    ${ENGINE_DIR}/zf2_feat.c
    ${ENGINE_DIR}/zf2_main.c
    ${ENGINE_DIR}/zf2_prosody.c
    ${ENGINE_DIR}/zf2_tree.c
    ${ENGINE_DIR}/zf2_units.c
    ${ENGINE_DIR}/zb_apm.c
    ${ENGINE_DIR}/zb_dur.c
    ${ENGINE_DIR}/zb_mlpg.c
    ${ENGINE_DIR}/zb_vocoder.c
    ${ENGINE_DIR}/zb_vocoder_int.c
    ${ENGINE_DIR}/zb_synth.c
    ${ENGINE_DIR}/zb_synth_int.c
    ${ENGINE_DIR}/zb_io.c
    ${ENGINE_DIR}/zb_sonic.c
    ${ENGINE_DIR}/zb_wave.c
    ${ENGINE_DIR}/zb_ratechg.c
)

set(ANNA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/anna)
set(ANNA_SOURCES
    ${ANNA_DIR}/anna_tts.c
    ${ANNA_DIR}/anna_voice.c
    ${ANNA_DIR}/anna_render.c
    ${ANNA_DIR}/anna_dec_wmav.c
    ${ANNA_DIR}/anna_norm.c
    ${ANNA_DIR}/anna_lex.c
    ${ANNA_DIR}/anna_lts.c
    ${ANNA_DIR}/anna_morph.c
    ${ANNA_DIR}/anna_pos.c
    ${ANNA_DIR}/anna_prosody.c
    ${ANNA_DIR}/anna_units.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sapidroid_anna_jni.c
)

set(SAPI4_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sapi4)
file(GLOB SAPI4_SOURCES ${SAPI4_DIR}/src/*.c)
list(APPEND SAPI4_SOURCES
    ${SAPI4_DIR}/lib/sapi4tts.c
    ${SAPI4_DIR}/lib/stubs.c
    ${SAPI4_DIR}/port/dllimage.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sapidroid_sapi4_jni.c
)

set(SAPI5_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sapi5)
set(SAPI5_SOURCES
    ${SAPI5_DIR}/sam.c
    ${SAPI5_DIR}/sam_lex.c
    ${SAPI5_DIR}/sam_morph.c
    ${SAPI5_DIR}/sam_pos.c
    ${SAPI5_DIR}/sam_norm.c
    ${SAPI5_DIR}/sam_front.c
    ${SAPI5_DIR}/sam4fx.c
    ${SAPI5_DIR}/sam_tts.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sapidroid_sapi5_jni.c
)

# Opt-in profiler (gradle -Pprofile): times voice load, speak, the JNI PCM handoff and the audio
# queue, and prints a ranked table per utterance. Off by default, and every instrumentation site
# compiles to nothing without it, so release builds are unchanged.
option(SAPIDROID_PROFILE "Instrument voice load, speak, PCM handoff and audio queue" OFF)

add_library(sapidroid SHARED sapidroid_jni.c prof.c ${ENGINE_SOURCES} ${ANNA_SOURCES} ${SAPI4_SOURCES} ${SAPI5_SOURCES})

if(SAPIDROID_PROFILE)
    target_compile_definitions(sapidroid PRIVATE SAPIDROID_PROFILE=1)
endif()

target_include_directories(sapidroid PRIVATE ${ENGINE_DIR} ${ANNA_DIR} ${SAPI4_DIR}/lib ${SAPI4_DIR}/src ${SAPI4_DIR}/port ${SAPI5_DIR})

# The engine is bit-exact only without floating point contraction and fast math. -O3 and
# -fno-math-errno change neither: they only remove errno tracking and enable inlining, loop
# unrolling and vectorisation that the compiler would otherwise skip (no reassociation is
# allowed without -ffast-math, so the order of every floating point operation is kept).
target_compile_options(sapidroid PRIVATE
    -std=c99
    -O3
    -fno-math-errno
    -ffp-contract=off
    -fno-fast-math
    -fno-finite-math-only
    -Wno-unused-parameter
)

target_link_libraries(sapidroid log m)

# SpeexDSP.cmake — Build SpeexDSP-1.2.1 from upstream, in-tree.
#
# Why FetchContent (no system pkg-config, no submodule):
#   - Reproducible: every build pulls the exact 1.2.1 tag.
#   - Algorithm-identical to the host Linux build (also uses 1.2.1).
#     vox.c's tuning sliders / thresholds are calibrated against this
#     specific speexdsp behavior; we want the simulator to remain truth.
#   - No source vendored into our git tree (one tag pin in this file).
#
# Why we provide our own CMakeLists for speexdsp:
#   The 1.2.1 tag predates upstream CMake support — speexdsp ships only
#   autotools (configure.ac, Makefile.am).  We compile its source files
#   directly and substitute autotools' configure-time defines with a
#   small, explicit set targeted at Cortex-M4F floating-point.
#
# Code-size: speexdsp is ~30 .c files; --gc-sections (set in
# cmake/arm-none-eabi.cmake) drops anything the firmware doesn't reach.
# In practice mdf.c, preprocess.c, kiss_fft*.c, fftwrap.c, filterbank.c
# end up linked; jitter/buffer/resample stay out.

include(FetchContent)
include_guard(GLOBAL)

FetchContent_Declare(
    speexdsp
    GIT_REPOSITORY https://github.com/xiph/speexdsp.git
    GIT_TAG        SpeexDSP-1.2.1
    GIT_SHALLOW    TRUE
)

# The 1.2.1 tag has no CMakeLists, so use Populate (download only) and
# add the library target ourselves.  MakeAvailable would try to add the
# (nonexistent) upstream CMakeLists as a subdirectory and fail.
FetchContent_GetProperties(speexdsp)
if(NOT speexdsp_POPULATED)
    message(STATUS "Fetching SpeexDSP 1.2.1 source for cross build...")
    FetchContent_Populate(speexdsp)
endif()

# speexdsp's include/speex/speexdsp_config_types.h is autotools-generated
# from speexdsp_config_types.h.in.  Substitute the same handful of macros
# for our cortex-m4f build (stdint.h gives us the standard fixed-width
# types).  We write it under the binary dir so the source checkout stays
# pristine and the file regenerates whenever the tag changes.
set(_VOX_SPEEXDSP_GENERATED_INCLUDE
    ${CMAKE_CURRENT_BINARY_DIR}/speexdsp_generated_include)
set(INCLUDE_STDINT "#include <stdint.h>")
set(SIZE16  "int16_t")
set(USIZE16 "uint16_t")
set(SIZE32  "int32_t")
set(USIZE32 "uint32_t")
# Note: file lives at the include-root (not under speex/) because
# include/speex/speexdsp_types.h does a relative #include "speexdsp_config_types.h"
# and falls through to -I paths if it's not in its own directory.
configure_file(
    ${speexdsp_SOURCE_DIR}/include/speex/speexdsp_config_types.h.in
    ${_VOX_SPEEXDSP_GENERATED_INCLUDE}/speexdsp_config_types.h
    @ONLY)

# All non-test source files under libspeexdsp/.  --gc-sections handles
# the ones we don't actually call (jitter buffer, resampler, etc.).
add_library(speexdsp_static STATIC
    ${speexdsp_SOURCE_DIR}/libspeexdsp/buffer.c
    ${speexdsp_SOURCE_DIR}/libspeexdsp/fftwrap.c
    ${speexdsp_SOURCE_DIR}/libspeexdsp/filterbank.c
    ${speexdsp_SOURCE_DIR}/libspeexdsp/jitter.c
    ${speexdsp_SOURCE_DIR}/libspeexdsp/kiss_fft.c
    ${speexdsp_SOURCE_DIR}/libspeexdsp/kiss_fftr.c
    ${speexdsp_SOURCE_DIR}/libspeexdsp/mdf.c
    ${speexdsp_SOURCE_DIR}/libspeexdsp/preprocess.c
    ${speexdsp_SOURCE_DIR}/libspeexdsp/resample.c
    ${speexdsp_SOURCE_DIR}/libspeexdsp/scal.c
    ${speexdsp_SOURCE_DIR}/libspeexdsp/smallft.c
)

# Public headers are at <speex/...> from the include/ root; consumers
# (vox_core's aec.c / vad.c) include them as <speex/speex_echo.h> etc.
target_include_directories(speexdsp_static
    PUBLIC  ${speexdsp_SOURCE_DIR}/include
            ${_VOX_SPEEXDSP_GENERATED_INCLUDE}
    PRIVATE ${speexdsp_SOURCE_DIR}/libspeexdsp
)

# speexdsp uses cos/sin/log/exp/pow/floor/atan — needs libm.  PUBLIC so
# anything that links speexdsp_static (vox_core, the firmware exe) gets
# -lm appended after the speexdsp archive on the link line.
target_link_libraries(speexdsp_static PUBLIC m)

# Stand-in for the autotools-generated config.h.  These are the defines
# speexdsp's source actually checks; the rest are autoconf bookkeeping
# that doesn't affect codegen on a bare-metal target.
target_compile_definitions(speexdsp_static PRIVATE
    FLOATING_POINT=1          # use 32-bit float math (we have FPU)
    USE_KISS_FFT=1            # built-in FFT, no external dep
    EXPORT=                   # symbol visibility — empty for static lib
    HAVE_STDINT_H=1
    # Deliberately NOT defining VAR_ARRAYS or USE_ALLOCA: speexdsp's
    # mdf.c/preprocess.c then fall through to plain malloc() for scratch
    # buffers.  We have ~80 KB of heap to spare on the G474RE; we do not
    # have arbitrary stack.  When VAR_ARRAYS=1 we observed stack-driven
    # corruption that swapped speex's forward/backward FFT config
    # pointers and tripped kiss_fftr2's "wrong-direction" speex_fatal.
    SPEEX_VERSION="1.2.1"
    # Note: do NOT set OUTSIDE_SPEEX — that flag suppresses
    # arch.h's `#include "speex/speexdsp_types.h"`, which is where
    # spx_int16_t / spx_int32_t come from.  It's only meant for when
    # libspeexdsp is being compiled inside libspeex (the codec), where
    # those types are already in scope.  Standalone builds want it off.
)

# Speexdsp's older-style code triggers a few warnings we don't want
# polluting our cross builds.  These are upstream issues, not bugs we
# can fix without diverging from 1.2.1.
target_compile_options(speexdsp_static PRIVATE
    -Wno-unused-but-set-variable
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-misleading-indentation
    -Wno-maybe-uninitialized
)

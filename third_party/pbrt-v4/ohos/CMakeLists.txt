# pbrt-v4 for OpenHarmony (OHOS) — CPU-only, built as a shared library.
#
# This is a self-contained build that does NOT reuse the upstream top-level
# CMakeLists.txt (which requires OpenGL/GLFW, CUDA detection, and host-built
# code generators). It produces:
#
#   libpbrt.so  — the full CPU renderer, including the pbrt command-line
#                 entry point renamed to pbrt_main (see below).
#
# Dropped vs. upstream: GPU (CUDA/OptiX) targets, the OpenGL/GLFW-based GUI
# (src/pbrt/util/gui.cpp is replaced by ohos/gui_stub.cpp), imgtool and all
# other command-line tools except pbrt itself, all tests, gtest_main.
#
# Code generation: upstream builds two host tools at build time
# (soac, rgb2spec_opt) to generate pbrt_soa.h / wavefront_workitems_soa.h and
# rgbspectrum_*.cpp. Since we cross-compile, these are pre-generated (they are
# platform-independent) and committed under ohos/pregen/. To regenerate on a
# host machine:
#   g++ -O2 -std=c++17 src/pbrt/cmd/soac.cpp -o soac
#   g++ -O2 -std=c++17 src/pbrt/cmd/rgb2spec_opt.cpp -o rgb2spec_opt
#   ./soac src/pbrt/pbrt.soa > ohos/pregen/pbrt_soa.h
#   ./soac src/pbrt/wavefront/workitems.soa > ohos/pregen/wavefront_workitems_soa.h
#   ./rgb2spec_opt 64 ohos/pregen/rgbspectrum_srgb.cpp sRGB
#   ./rgb2spec_opt 64 ohos/pregen/rgbspectrum_dci_p3.cpp DCI_P3
#   ./rgb2spec_opt 64 ohos/pregen/rgbspectrum_rec2020.cpp REC2020
#   ./rgb2spec_opt 64 ohos/pregen/rgbspectrum_aces.cpp ACES2065_1
#
# Configure example (Git Bash):
#   export MSYS_NO_PATHCONV=1
#   cmake -S pbrt-v4/ohos -B pbrt-v4/ohos/build-arm64 -G Ninja \
#     -DCMAKE_MAKE_PROGRAM=.../native/build-tools/cmake/bin/ninja.exe \
#     -DCMAKE_TOOLCHAIN_FILE=.../native/build/cmake/ohos.toolchain.cmake \
#     -DOHOS_ARCH=arm64-v8a -DCMAKE_BUILD_TYPE=Release
#   cmake --build pbrt-v4/ohos/build-arm64

cmake_minimum_required(VERSION 3.16)

project(pbrt_ohos LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# All ext libraries are static and get linked into libpbrt.so.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif ()

get_filename_component(PBRT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(PBRT_SRC ${PBRT_ROOT}/src)
set(PBRT_EXT ${PBRT_SRC}/ext)
set(PBRT_PREGEN ${CMAKE_CURRENT_SOURCE_DIR}/pregen)

# Sanity-check the pregenerated files.
foreach (f pbrt_soa.h wavefront_workitems_soa.h
           rgbspectrum_srgb.cpp rgbspectrum_dci_p3.cpp
           rgbspectrum_rec2020.cpp rgbspectrum_aces.cpp)
  if (NOT EXISTS ${PBRT_PREGEN}/${f})
    message(FATAL_ERROR
      "Missing pregenerated file ohos/pregen/${f}; see the header comment of "
      "ohos/CMakeLists.txt for how to regenerate it.")
  endif ()
endforeach ()

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

###########################################################################
# ext: zlib

set(ZLIB_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${PBRT_EXT}/zlib ${CMAKE_BINARY_DIR}/ext/zlib)

# Point find_package(ZLIB) (used by ptex) at the vendored build.
set(ZLIB_LIBRARY zlibstatic CACHE STRING "" FORCE)
set(ZLIB_LIBRARIES zlibstatic CACHE STRING "" FORCE)
set(ZLIB_INCLUDE_DIR ${PBRT_EXT}/zlib ${CMAKE_BINARY_DIR}/ext/zlib CACHE STRING "" FORCE)
set(ZLIB_INCLUDE_DIRS ${PBRT_EXT}/zlib ${CMAKE_BINARY_DIR}/ext/zlib)
set(ZLIB_FOUND TRUE CACHE BOOL "" FORCE)
if (NOT TARGET ZLIB::ZLIB)
  add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif ()
include_directories(${ZLIB_INCLUDE_DIRS})  # so ptex/openexr find zlib.h

###########################################################################
# ext: libdeflate

set(LIBDEFLATE_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_GZIP OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${PBRT_EXT}/libdeflate ${CMAKE_BINARY_DIR}/ext/libdeflate)

###########################################################################
# ext: OpenEXR (fetches Imath via FetchContent if not installed)

# 离线依赖：优先使用本地预取的 Imath/libdeflate，避免 FetchContent 联网克隆
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/deps/imath-src)
  set(FETCHCONTENT_SOURCE_DIR_IMATH ${CMAKE_CURRENT_SOURCE_DIR}/deps/imath-src)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/deps/deflate-src)
  set(FETCHCONTENT_SOURCE_DIR_DEFLATE ${CMAKE_CURRENT_SOURCE_DIR}/deps/deflate-src)
endif()

set(OPENEXR_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_PYTHON OFF CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${PBRT_EXT}/openexr ${CMAKE_BINARY_DIR}/ext/openexr)

###########################################################################
# ext: ptex

set(PTEX_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(PTEX_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(PTEX_BUILD_DOCS OFF CACHE BOOL "" FORCE)
add_subdirectory(${PBRT_EXT}/ptex ${CMAKE_BINARY_DIR}/ext/ptex)

###########################################################################
# ext: double-conversion, utf8proc

add_subdirectory(${PBRT_EXT}/double-conversion ${CMAKE_BINARY_DIR}/ext/double-conversion)

set(UTF8PROC_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${PBRT_EXT}/utf8proc ${CMAKE_BINARY_DIR}/ext/utf8proc)

###########################################################################
# pbrt compile definitions (normally probed by the upstream CMakeLists;
# hard-coded here for the OHOS/clang/musl target)

set(PBRT_DEFINITIONS
  PBRT_IS_LINUX
  PTEX_STATIC
  "PBRT_NOINLINE=__attribute__((noinline))"
  "PBRT_RESTRICT=__restrict__"
  PBRT_HAVE_POSIX_MEMALIGN
  PBRT_HAVE_MMAP
  PBRT_WITH_PROGRESS_PNG
  "$<$<CONFIG:DEBUG>:PBRT_DEBUG_BUILD>"
)

set(PBRT_CXX_FLAGS "")
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION GREATER_EQUAL 14)
  list(APPEND PBRT_CXX_FLAGS "-ffp-contract=off")
endif ()

###########################################################################
# pbrt sources: CPU renderer only (same lists as upstream, minus GPU files,
# util/gui.cpp, cmd/pspec_gpu.cpp, tests)

set(PBRT_LIB_SOURCES
  ${PBRT_SRC}/pbrt/bsdf.cpp
  ${PBRT_SRC}/pbrt/bssrdf.cpp
  ${PBRT_SRC}/pbrt/bxdfs.cpp
  ${PBRT_SRC}/pbrt/cameras.cpp
  ${PBRT_SRC}/pbrt/film.cpp
  ${PBRT_SRC}/pbrt/filters.cpp
  ${PBRT_SRC}/pbrt/interaction.cpp
  ${PBRT_SRC}/pbrt/lights.cpp
  ${PBRT_SRC}/pbrt/lightsamplers.cpp
  ${PBRT_SRC}/pbrt/materials.cpp
  ${PBRT_SRC}/pbrt/media.cpp
  ${PBRT_SRC}/pbrt/options.cpp
  ${PBRT_SRC}/pbrt/paramdict.cpp
  ${PBRT_SRC}/pbrt/parser.cpp
  ${PBRT_SRC}/pbrt/pbrt.cpp
  ${PBRT_SRC}/pbrt/ray.cpp
  ${PBRT_SRC}/pbrt/samplers.cpp
  ${PBRT_SRC}/pbrt/scene.cpp
  ${PBRT_SRC}/pbrt/shapes.cpp
  ${PBRT_SRC}/pbrt/textures.cpp

  ${PBRT_SRC}/pbrt/cpu/aggregates.cpp
  ${PBRT_SRC}/pbrt/cpu/integrators.cpp
  ${PBRT_SRC}/pbrt/cpu/primitive.cpp
  ${PBRT_SRC}/pbrt/cpu/render.cpp

  ${PBRT_SRC}/pbrt/util/args.cpp
  ${PBRT_SRC}/pbrt/util/bluenoise.cpp
  ${PBRT_SRC}/pbrt/util/buffercache.cpp
  ${PBRT_SRC}/pbrt/util/check.cpp
  ${PBRT_SRC}/pbrt/util/color.cpp
  ${PBRT_SRC}/pbrt/util/colorspace.cpp
  ${PBRT_SRC}/pbrt/util/display.cpp
  ${PBRT_SRC}/pbrt/util/error.cpp
  ${PBRT_SRC}/pbrt/util/file.cpp
  ${PBRT_SRC}/pbrt/util/float.cpp
  ${PBRT_SRC}/pbrt/util/image.cpp
  ${PBRT_SRC}/pbrt/util/log.cpp
  ${PBRT_SRC}/pbrt/util/loopsubdiv.cpp
  ${PBRT_SRC}/pbrt/util/lowdiscrepancy.cpp
  ${PBRT_SRC}/pbrt/util/math.cpp
  ${PBRT_SRC}/pbrt/util/memory.cpp
  ${PBRT_SRC}/pbrt/util/mesh.cpp
  ${PBRT_SRC}/pbrt/util/mipmap.cpp
  ${PBRT_SRC}/pbrt/util/noise.cpp
  ${PBRT_SRC}/pbrt/util/parallel.cpp
  ${PBRT_SRC}/pbrt/util/pmj02tables.cpp
  ${PBRT_SRC}/pbrt/util/primes.cpp
  ${PBRT_SRC}/pbrt/util/print.cpp
  ${PBRT_SRC}/pbrt/util/progressreporter.cpp
  ${PBRT_SRC}/pbrt/util/pstd.cpp
  ${PBRT_SRC}/pbrt/util/rng.cpp
  ${PBRT_SRC}/pbrt/util/sampling.cpp
  ${PBRT_SRC}/pbrt/util/scattering.cpp
  ${PBRT_SRC}/pbrt/util/sobolmatrices.cpp
  ${PBRT_SRC}/pbrt/util/spectrum.cpp
  ${PBRT_SRC}/pbrt/util/stats.cpp
  ${PBRT_SRC}/pbrt/util/stbimage.cpp
  ${PBRT_SRC}/pbrt/util/string.cpp
  ${PBRT_SRC}/pbrt/util/transform.cpp
  ${PBRT_SRC}/pbrt/util/vecmath.cpp

  ${PBRT_SRC}/pbrt/wavefront/aggregate.cpp
  ${PBRT_SRC}/pbrt/wavefront/camera.cpp
  ${PBRT_SRC}/pbrt/wavefront/film.cpp
  ${PBRT_SRC}/pbrt/wavefront/integrator.cpp
  ${PBRT_SRC}/pbrt/wavefront/media.cpp
  ${PBRT_SRC}/pbrt/wavefront/samples.cpp
  ${PBRT_SRC}/pbrt/wavefront/surfscatter.cpp
  ${PBRT_SRC}/pbrt/wavefront/subsurface.cpp
  ${PBRT_SRC}/pbrt/wavefront/wavefront.cpp

  ${PBRT_EXT}/gtest/gtest-all.cc
  ${PBRT_EXT}/lodepng/lodepng.cpp
  ${PBRT_EXT}/rply/rply.cpp

  ${PBRT_PREGEN}/rgbspectrum_srgb.cpp
  ${PBRT_PREGEN}/rgbspectrum_dci_p3.cpp
  ${PBRT_PREGEN}/rgbspectrum_rec2020.cpp
  ${PBRT_PREGEN}/rgbspectrum_aces.cpp

  # OHOS-specific additions
  ${CMAKE_CURRENT_SOURCE_DIR}/gui_stub.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/progress_png.cpp

  # Command-line entry point; main is renamed to pbrt_main (see below).
  ${PBRT_SRC}/pbrt/cmd/pbrt.cpp
)

# Rename pbrt's main() to pbrt_main so the shared library can be embedded
# in an application that has its own main().
set_source_files_properties(${PBRT_SRC}/pbrt/cmd/pbrt.cpp
  PROPERTIES COMPILE_DEFINITIONS "main=pbrt_main")

# Upstream silences this warning the same way.
set_source_files_properties(${PBRT_SRC}/pbrt/util/string.cpp
  PROPERTIES COMPILE_FLAGS "-Wno-deprecated-declarations")

add_library(pbrt SHARED ${PBRT_LIB_SOURCES})

target_compile_definitions(pbrt PRIVATE ${PBRT_DEFINITIONS})
target_compile_options(pbrt PRIVATE ${PBRT_CXX_FLAGS})

target_include_directories(pbrt
  PUBLIC ${PBRT_SRC}
  PRIVATE
    ${PBRT_EXT}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PBRT_PREGEN}
    ${PBRT_EXT}/filesystem
    ${PBRT_EXT}/ptex/src/ptex
    ${PBRT_EXT}/double-conversion
    ${PBRT_EXT}/openvdb/nanovdb
    ${PBRT_EXT}/libdeflate
    ${PBRT_EXT}/glfw/include
    ${PBRT_EXT}/glad/include
    ${ZLIB_INCLUDE_DIRS}
)

target_link_libraries(pbrt PRIVATE
  OpenEXR::OpenEXR
  Ptex_static
  zlibstatic
  deflate
  double-conversion
  utf8proc
  Threads::Threads
  ${CMAKE_DL_LIBS}
)

set_target_properties(pbrt PROPERTIES OUTPUT_NAME pbrt)

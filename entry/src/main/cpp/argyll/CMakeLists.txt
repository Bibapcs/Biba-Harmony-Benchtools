# ArgyllCMS i1Pro3 driver subset for HarmonyOS (OHOS).
#
# This CMakeLists.txt builds the Argyll instrument stack subset as a
# standalone static library (libargyll_i1.a) that an OHOS application
# CMake project can reference with add_subdirectory(argyll) or
# target_link_libraries(<app> argyll_i1).
#
# OHOS changes: this is a new build file for the OHOS port. The library
# is compiled against the OHOS USB DDK (libusb_ndk.z.so) headers from the
# NDK sysroot; the actual .so is provided by the OS, no link step is
# needed here (static library only).
#
# NOTE: currently NOT wired into the main app CMakeLists.txt on purpose
# (see build_check.sh for an independent standalone build).

cmake_minimum_required(VERSION 3.5.0)

project(argyll_i1 C)

set(ARGYLL_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

set(ARGYLL_SOURCES
    ${ARGYLL_ROOT}/src/numlib/numsup.c
    ${ARGYLL_ROOT}/src/numlib/rand.c
    ${ARGYLL_ROOT}/src/numlib/powell.c
    ${ARGYLL_ROOT}/src/numlib/ludecomp.c
    ${ARGYLL_ROOT}/src/numlib/svd.c
    ${ARGYLL_ROOT}/src/cgats/cgats.c
    ${ARGYLL_ROOT}/src/cgats/pars.c
    ${ARGYLL_ROOT}/src/xicc/xspect.c
    ${ARGYLL_ROOT}/src/xicc/ccmx.c
    ${ARGYLL_ROOT}/src/xicc/ccss.c
    ${ARGYLL_ROOT}/src/icc/icc_util.c
    ${ARGYLL_ROOT}/src/icc/icc_util_stub.c
    ${ARGYLL_ROOT}/src/rspl/rspl.c
    ${ARGYLL_ROOT}/src/rspl/rev.c
    ${ARGYLL_ROOT}/src/rspl/gam.c
    ${ARGYLL_ROOT}/src/rspl/spline.c
    ${ARGYLL_ROOT}/src/rspl/opt.c
    ${ARGYLL_ROOT}/src/rspl/scat.c
    ${ARGYLL_ROOT}/src/spectro/conv.c
    ${ARGYLL_ROOT}/src/spectro/pollem.c
    ${ARGYLL_ROOT}/src/spectro/disptechs.c
    ${ARGYLL_ROOT}/src/spectro/xdg_bds.c
    ${ARGYLL_ROOT}/src/spectro/aglob.c
    ${ARGYLL_ROOT}/src/spectro/insttypes.c
    ${ARGYLL_ROOT}/src/spectro/icoms.c
    ${ARGYLL_ROOT}/src/spectro/usbio.c
    ${ARGYLL_ROOT}/src/spectro/inst.c
    ${ARGYLL_ROOT}/src/spectro/i1pro3.c
    ${ARGYLL_ROOT}/src/spectro/xrga.c
    ${ARGYLL_ROOT}/src/spectro/i1pro3_imp.c
    ${ARGYLL_ROOT}/argyll_probe.c
)

add_library(argyll_i1 STATIC ${ARGYLL_SOURCES})

# usbio.c #includes usbio_ohos.c, so no separate source entry for it.
# OHOS specific defines: UNIX selects the Unix icoms class code
# (icoms_ux.c); __OHOS__ is predefined by the OHOS toolchain and selects
# the OHOS USB DDK backend in usbio.c/usbio.h. ENABLE_SERIAL /
# ENABLE_FAST_SERIAL are deliberately NOT defined.
target_compile_definitions(argyll_i1 PRIVATE
    UNIX
    ENABLE_USB
)

target_include_directories(argyll_i1
    PUBLIC
    ${ARGYLL_ROOT}/include/h
    ${ARGYLL_ROOT}/include/numlib
    ${ARGYLL_ROOT}/include/cgats
    ${ARGYLL_ROOT}/include/spectro
    ${ARGYLL_ROOT}/include/xicc
    ${ARGYLL_ROOT}/include/icc
    ${ARGYLL_ROOT}/include/rspl
)

# OHOS NDK toolchain required (aarch64-linux-ohos). The USB DDK API is
# declared in the sysroot, so no extra include path is needed.
target_compile_options(argyll_i1 PRIVATE -O2)

#!/bin/bash
# Standalone build check for the ArgyllCMS i1Pro3 driver subset on OHOS.
#
# Compiles every source in src/ with the OHOS NDK clang for
# aarch64-linux-ohos and archives the objects into libargyll_i1.a.
# Run from git bash (or any bash) in the argyll/ directory:
#
#   ./build_check.sh
#
# Output: build/libargyll_i1.a (all .o files are kept in build/)
#
# OHOS changes: new file - provides an independent way of verifying the
# port compiles without touching the main bbpcs hvigor build.

set -e

cd "$(dirname "$0")"

NDK=${NDK:-/d/ohos_project/command-line-tools/sdk/default/openharmony/native}
CLANG="$NDK/llvm/bin/clang"
AR="$NDK/llvm/bin/llvm-ar"

TARGET=aarch64-linux-ohos
SYSROOT="$NDK/sysroot"
CFLAGS="--target=$TARGET --sysroot=$SYSROOT -O2 -DUNIX -DENABLE_USB"

INC="-Iinclude/h -Iinclude/numlib -Iinclude/cgats -Iinclude/spectro -Iinclude/xicc -Iinclude/icc -Iinclude/rspl"

SRCS="
src/numlib/numsup.c
src/numlib/rand.c
src/numlib/powell.c
src/numlib/ludecomp.c
src/numlib/svd.c
src/cgats/cgats.c
src/cgats/pars.c
src/xicc/xspect.c
src/xicc/ccmx.c
src/xicc/ccss.c
src/icc/icc_util.c
src/icc/icc_util_stub.c
src/rspl/rspl.c
src/rspl/rev.c
src/rspl/gam.c
src/rspl/spline.c
src/rspl/opt.c
src/rspl/scat.c
src/spectro/conv.c
src/spectro/pollem.c
src/spectro/disptechs.c
src/spectro/xdg_bds.c
src/spectro/aglob.c
src/spectro/insttypes.c
src/spectro/icoms.c
src/spectro/usbio.c
src/spectro/inst.c
src/spectro/i1pro3.c
src/spectro/xrga.c
src/spectro/i1pro3_imp.c
argyll_probe.c
"

rm -rf build
mkdir -p build

for src in $SRCS; do
	obj="build/$(basename "${src%.c}").o"
	echo "Compiling $src"
	"$CLANG" $CFLAGS $INC -c "$src" -o "$obj"
done

echo "Archiving build/libargyll_i1.a"
"$AR" rcs build/libargyll_i1.a build/*.o

echo
echo "Linking test executable (link check only, not run on host)"
cat > build/test_main.c <<'EOF'
/* Link check: exercise the exported entry point. */
extern int argyll_i1_enumerate(void);
int main(void) {
	return argyll_i1_enumerate() < 0 ? 1 : 0;
}
EOF
"$CLANG" $CFLAGS $INC -o build/test_i1pro3 build/test_main.c -Lbuild -largyll_i1 \
	"$SYSROOT/usr/lib/aarch64-linux-ohos/libusb_ndk.z.so" \
	"$SYSROOT/usr/lib/aarch64-linux-ohos/libhilog_ndk.z.so" -lpthread

echo
echo "Compiling + linking i1meas.cpp (NAPI measurement orchestration, link check only)"
CLANGPP="$NDK/llvm/bin/clang++"
"$CLANGPP" --target=$TARGET --sysroot=$SYSROOT -O2 -std=c++17 -DUNIX -DENABLE_USB \
	$INC -I"$SYSROOT/usr/include/napi" -c ../i1meas.cpp -o build/i1meas.o
cat > build/test_i1meas_main.cpp <<'EOF'
/* Link check: pull in the whole i1meas translation unit. */
#include "napi/native_api.h"
extern void i1meas_register(napi_env env, napi_value exports);
int main(void) {
	i1meas_register(nullptr, nullptr);
	return 0;
}
EOF
"$CLANGPP" --target=$TARGET --sysroot=$SYSROOT -O2 -std=c++17 \
	-I"$SYSROOT/usr/include/napi" \
	-o build/test_i1meas build/test_i1meas_main.cpp build/i1meas.o \
	-Lbuild -largyll_i1 \
	"$SYSROOT/usr/lib/aarch64-linux-ohos/libusb_ndk.z.so" \
	"$SYSROOT/usr/lib/aarch64-linux-ohos/libhilog_ndk.z.so" \
	"$SYSROOT/usr/lib/aarch64-linux-ohos/libace_napi.z.so" \
	-lpthread

echo
echo "Build OK: $(ls -la build/libargyll_i1.a | awk '{print $5}') bytes"
echo "Link OK : build/test_i1pro3 and build/test_i1meas (cross-compiled, not runnable on host)"

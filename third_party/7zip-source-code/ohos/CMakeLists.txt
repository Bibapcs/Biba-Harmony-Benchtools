# CMake build of 7-Zip ZS 22.00 (v1.5.2) "7za" bundle as a shared library
# for OpenHarmony (OHOS) and Android, arm64-v8a / x86_64.
# Both platforms share this single file so compile conditions stay aligned.
# musl and bionic behave identically for the bits below (affinity disabled on
# both; the uint32_t/uint64_t guard defines are needed on both), so the only
# platform condition left is the arch variable (OHOS_ARCH / ANDROID_ABI).
#
# Source list mirrors CPP/7zip/Bundles/Alone/makefile.gcc (POSIX branch),
# with the third-party codec libraries (zstd/lz4/brotli/lizard/lz5/fast-lzma2)
# compiled directly into lib7za.so instead of separate shared libraries.
# LZHAM is omitted (heavy C++ codebase, non-core format).
#
# Usage (Git Bash, with: export MSYS_NO_PATHCONV=1):
#   cmake -S ohos -B ohos/build-arm64 -G Ninja \
#     -DCMAKE_MAKE_PROGRAM=<ndk>/build-tools/cmake/bin/ninja.exe \
#     -DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/ohos.toolchain.cmake \
#     -DOHOS_ARCH=arm64-v8a -DCMAKE_BUILD_TYPE=Release
#   cmake --build ohos/build-arm64

cmake_minimum_required(VERSION 3.16)
project(7za_ohos C CXX)

get_filename_component(Z7_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# ---------------------------------------------------------------------------
# global flags (mirror 7zip_gcc.mak CFLAGS_BASE; -w to silence a ported tree)
# ---------------------------------------------------------------------------
add_compile_options(-w -fPIC)
add_compile_definitions(
  NDEBUG
  _REENTRANT
  _FILE_OFFSET_BITS=64
  _LARGEFILE_SOURCE
  # musl exposes cpu_set_t / pthread_*affinity_np only under _GNU_SOURCE;
  # disable 7-Zip's affinity path instead of enabling GNU extensions globally.
  # Kept disabled on Android (bionic) too, so both platforms run the same
  # non-affinity code path for comparable benchmark results.
  _7ZIP_AFFINITY_DISABLE
)
# musl/bionic already declare uint32_t/uint64_t before C/hashes/hash.h gets
# included; hash.h would otherwise typedef them again with a different type.
# - OHOS (musl): only C++ TUs see the prior declarations (musl's C headers
#   don't pull in stdint.h here), so the guards apply to C++ only.
# - Android (bionic): C TUs see them too (e.g. C/hashes/md5.c), so the guards
#   apply to all languages.
if(OHOS)
  add_compile_definitions(
    $<$<COMPILE_LANGUAGE:CXX>:_UINT32_T_DECLARED>
    $<$<COMPILE_LANGUAGE:CXX>:_UINT64_T_DECLARED>
  )
else()
  add_compile_definitions(_UINT32_T_DECLARED _UINT64_T_DECLARED)
endif()

# arm64: hardware CRC / crypto intrinsics in the *Opt files need the
# corresponding -march extensions (they are runtime-dispatched by 7-Zip).
# Same file set and flags on OHOS and Android (OHOS_ARCH / ANDROID_ABI are
# set by the respective toolchain files).
if(OHOS_ARCH STREQUAL "arm64-v8a" OR ANDROID_ABI STREQUAL "arm64-v8a")
  set_source_files_properties(
    "${Z7_ROOT}/C/7zCrc.c"
    "${Z7_ROOT}/C/7zCrcOpt.c"
    "${Z7_ROOT}/C/XzCrc64Opt.c"
    "${Z7_ROOT}/C/AesOpt.c"
    "${Z7_ROOT}/C/Sha1Opt.c"
    "${Z7_ROOT}/C/Sha256Opt.c"
    PROPERTIES COMPILE_OPTIONS "-march=armv8-a+crc+crypto"
  )
endif()

# ===========================================================================
# 7-Zip core sources (explicit lists from Bundles/Alone/makefile.gcc)
# ===========================================================================

set(SRC_COMMON
  CPP/Common/CommandLineParser.cpp
  CPP/Common/CRC.cpp
  CPP/Common/CrcReg.cpp
  CPP/Common/DynLimBuf.cpp
  CPP/Common/IntToString.cpp
  CPP/Common/ListFileUtils.cpp
  CPP/Common/LzFindPrepare.cpp
  CPP/Common/MyString.cpp
  CPP/Common/NewHandler.cpp
  CPP/Common/StdInStream.cpp
  CPP/Common/StdOutStream.cpp
  CPP/Common/Sha1Prepare.cpp
  CPP/Common/Sha1Reg.cpp
  CPP/Common/Sha256Prepare.cpp
  CPP/Common/Sha256Reg.cpp
  CPP/Common/StringConvert.cpp
  CPP/Common/StringToInt.cpp
  CPP/Common/UTFConvert.cpp
  CPP/Common/MyVector.cpp
  CPP/Common/Wildcard.cpp
  CPP/Common/XzCrc64Init.cpp
  CPP/Common/XzCrc64Reg.cpp
  CPP/Common/Md2Reg.cpp
  CPP/Common/Md4Reg.cpp
  CPP/Common/Md5Reg.cpp
  CPP/Common/Sha384Reg.cpp
  CPP/Common/Sha512Reg.cpp
  CPP/Common/XXH32Reg.cpp
  CPP/Common/XXH64Reg.cpp
  CPP/Common/Blake3Reg.cpp
  CPP/Common/MyWindows.cpp          # SYS_OBJS (non-Windows branch)
)

set(SRC_WINDOWS
  CPP/Windows/ErrorMsg.cpp
  CPP/Windows/FileDir.cpp
  CPP/Windows/FileFind.cpp
  CPP/Windows/FileIO.cpp
  CPP/Windows/FileLink.cpp
  CPP/Windows/FileName.cpp
  CPP/Windows/PropVariant.cpp
  CPP/Windows/PropVariantConv.cpp
  CPP/Windows/PropVariantUtils.cpp
  CPP/Windows/System.cpp
  CPP/Windows/SystemInfo.cpp
  CPP/Windows/TimeUtils.cpp
  CPP/Windows/Synchronization.cpp   # MT
)

set(SRC_7ZIP_COMMON
  CPP/7zip/Common/CreateCoder.cpp
  CPP/7zip/Common/CWrappers.cpp
  CPP/7zip/Common/FilePathAutoRename.cpp
  CPP/7zip/Common/FileStreams.cpp
  CPP/7zip/Common/InBuffer.cpp
  CPP/7zip/Common/InOutTempBuffer.cpp
  CPP/7zip/Common/FilterCoder.cpp
  CPP/7zip/Common/LimitedStreams.cpp
  CPP/7zip/Common/MethodId.cpp
  CPP/7zip/Common/MethodProps.cpp
  CPP/7zip/Common/OffsetStream.cpp
  CPP/7zip/Common/OutBuffer.cpp
  CPP/7zip/Common/ProgressUtils.cpp
  CPP/7zip/Common/PropId.cpp
  CPP/7zip/Common/StreamObjects.cpp
  CPP/7zip/Common/StreamUtils.cpp
  CPP/7zip/Common/UniqBlocks.cpp
  # MT_OBJS
  CPP/7zip/Common/MemBlocks.cpp
  CPP/7zip/Common/OutMemStream.cpp
  CPP/7zip/Common/ProgressMt.cpp
  CPP/7zip/Common/StreamBinder.cpp
  CPP/7zip/Common/VirtThread.cpp
)

set(SRC_ARCHIVE
  CPP/7zip/Archive/Bz2Handler.cpp
  CPP/7zip/Archive/GzHandler.cpp
  CPP/7zip/Archive/LzmaHandler.cpp
  CPP/7zip/Archive/SplitHandler.cpp
  CPP/7zip/Archive/XzHandler.cpp
  CPP/7zip/Archive/ZstdHandler.cpp
  CPP/7zip/Archive/LizardHandler.cpp
  CPP/7zip/Archive/Lz5Handler.cpp
  CPP/7zip/Archive/Lz4Handler.cpp
  CPP/7zip/Archive/Common/CoderMixer2.cpp
  CPP/7zip/Archive/Common/DummyOutStream.cpp
  CPP/7zip/Archive/Common/HandlerOut.cpp
  CPP/7zip/Archive/Common/InStreamWithCRC.cpp
  CPP/7zip/Archive/Common/ItemNameUtils.cpp
  CPP/7zip/Archive/Common/MultiStream.cpp
  CPP/7zip/Archive/Common/OutStreamWithCRC.cpp
  CPP/7zip/Archive/Common/ParseProperties.cpp
  CPP/7zip/Archive/7z/7zCompressionMode.cpp
  CPP/7zip/Archive/7z/7zDecode.cpp
  CPP/7zip/Archive/7z/7zEncode.cpp
  CPP/7zip/Archive/7z/7zExtract.cpp
  CPP/7zip/Archive/7z/7zFolderInStream.cpp
  CPP/7zip/Archive/7z/7zHandler.cpp
  CPP/7zip/Archive/7z/7zHandlerOut.cpp
  CPP/7zip/Archive/7z/7zHeader.cpp
  CPP/7zip/Archive/7z/7zIn.cpp
  CPP/7zip/Archive/7z/7zOut.cpp
  CPP/7zip/Archive/7z/7zProperties.cpp
  CPP/7zip/Archive/7z/7zRegister.cpp
  CPP/7zip/Archive/7z/7zSpecStream.cpp
  CPP/7zip/Archive/7z/7zUpdate.cpp
  CPP/7zip/Archive/Cab/CabBlockInStream.cpp
  CPP/7zip/Archive/Cab/CabHandler.cpp
  CPP/7zip/Archive/Cab/CabHeader.cpp
  CPP/7zip/Archive/Cab/CabIn.cpp
  CPP/7zip/Archive/Cab/CabRegister.cpp
  CPP/7zip/Archive/Tar/TarHandler.cpp
  CPP/7zip/Archive/Tar/TarHandlerOut.cpp
  CPP/7zip/Archive/Tar/TarHeader.cpp
  CPP/7zip/Archive/Tar/TarIn.cpp
  CPP/7zip/Archive/Tar/TarOut.cpp
  CPP/7zip/Archive/Tar/TarUpdate.cpp
  CPP/7zip/Archive/Tar/TarRegister.cpp
  CPP/7zip/Archive/Zip/ZipAddCommon.cpp
  CPP/7zip/Archive/Zip/ZipHandler.cpp
  CPP/7zip/Archive/Zip/ZipHandlerOut.cpp
  CPP/7zip/Archive/Zip/ZipIn.cpp
  CPP/7zip/Archive/Zip/ZipItem.cpp
  CPP/7zip/Archive/Zip/ZipOut.cpp
  CPP/7zip/Archive/Zip/ZipUpdate.cpp
  CPP/7zip/Archive/Zip/ZipRegister.cpp
)

set(SRC_COMPRESS
  CPP/7zip/Compress/Bcj2Coder.cpp
  CPP/7zip/Compress/Bcj2Register.cpp
  CPP/7zip/Compress/BcjCoder.cpp
  CPP/7zip/Compress/BcjRegister.cpp
  CPP/7zip/Compress/BitlDecoder.cpp
  CPP/7zip/Compress/BranchMisc.cpp
  CPP/7zip/Compress/BranchRegister.cpp
  CPP/7zip/Compress/ByteSwap.cpp
  CPP/7zip/Compress/BZip2Crc.cpp
  CPP/7zip/Compress/BZip2Decoder.cpp
  CPP/7zip/Compress/BZip2Encoder.cpp
  CPP/7zip/Compress/BZip2Register.cpp
  CPP/7zip/Compress/CopyCoder.cpp
  CPP/7zip/Compress/CopyRegister.cpp
  CPP/7zip/Compress/Deflate64Register.cpp
  CPP/7zip/Compress/DeflateDecoder.cpp
  CPP/7zip/Compress/DeflateEncoder.cpp
  CPP/7zip/Compress/DeflateRegister.cpp
  CPP/7zip/Compress/DeltaFilter.cpp
  CPP/7zip/Compress/ImplodeDecoder.cpp
  CPP/7zip/Compress/Lzma2Decoder.cpp
  CPP/7zip/Compress/Lzma2Encoder.cpp
  CPP/7zip/Compress/Lzma2Register.cpp
  CPP/7zip/Compress/LzmaDecoder.cpp
  CPP/7zip/Compress/LzmaEncoder.cpp
  CPP/7zip/Compress/LzmaRegister.cpp
  CPP/7zip/Compress/LzOutWindow.cpp
  CPP/7zip/Compress/LzxDecoder.cpp
  CPP/7zip/Compress/PpmdDecoder.cpp
  CPP/7zip/Compress/PpmdEncoder.cpp
  CPP/7zip/Compress/PpmdRegister.cpp
  CPP/7zip/Compress/PpmdZip.cpp
  CPP/7zip/Compress/QuantumDecoder.cpp
  CPP/7zip/Compress/ShrinkDecoder.cpp
  CPP/7zip/Compress/XzDecoder.cpp
  CPP/7zip/Compress/XzEncoder.cpp
  CPP/7zip/Compress/ZstdDecoder.cpp
  CPP/7zip/Compress/ZstdEncoder.cpp
  CPP/7zip/Compress/ZstdRegister.cpp
  CPP/7zip/Compress/Lz4Decoder.cpp
  CPP/7zip/Compress/Lz4Encoder.cpp
  CPP/7zip/Compress/Lz4Register.cpp
  CPP/7zip/Compress/BrotliDecoder.cpp
  CPP/7zip/Compress/BrotliEncoder.cpp
  CPP/7zip/Compress/BrotliRegister.cpp
  CPP/7zip/Compress/LizardDecoder.cpp
  CPP/7zip/Compress/LizardEncoder.cpp
  CPP/7zip/Compress/LizardRegister.cpp
  CPP/7zip/Compress/Lz5Decoder.cpp
  CPP/7zip/Compress/Lz5Encoder.cpp
  CPP/7zip/Compress/Lz5Register.cpp
  CPP/7zip/Compress/FastLzma2Register.cpp
  # LzhamRegister.cpp intentionally omitted (LZHAM disabled)
)

set(SRC_CRYPTO
  CPP/7zip/Crypto/7zAes.cpp
  CPP/7zip/Crypto/7zAesRegister.cpp
  CPP/7zip/Crypto/HmacSha1.cpp
  CPP/7zip/Crypto/MyAes.cpp
  CPP/7zip/Crypto/MyAesReg.cpp
  CPP/7zip/Crypto/Pbkdf2HmacSha1.cpp
  CPP/7zip/Crypto/RandGen.cpp
  CPP/7zip/Crypto/WzAes.cpp
  CPP/7zip/Crypto/ZipCrypto.cpp
  CPP/7zip/Crypto/ZipStrong.cpp
)

set(SRC_UI_COMMON
  CPP/7zip/UI/Common/ArchiveCommandLine.cpp
  CPP/7zip/UI/Common/ArchiveExtractCallback.cpp
  CPP/7zip/UI/Common/ArchiveOpenCallback.cpp
  CPP/7zip/UI/Common/Bench.cpp
  CPP/7zip/UI/Common/DefaultName.cpp
  CPP/7zip/UI/Common/EnumDirItems.cpp
  CPP/7zip/UI/Common/Extract.cpp
  CPP/7zip/UI/Common/ExtractingFilePath.cpp
  CPP/7zip/UI/Common/HashCalc.cpp
  CPP/7zip/UI/Common/LoadCodecs.cpp
  CPP/7zip/UI/Common/OpenArchive.cpp
  CPP/7zip/UI/Common/PropIDUtils.cpp
  CPP/7zip/UI/Common/SetProperties.cpp
  CPP/7zip/UI/Common/SortUtils.cpp
  CPP/7zip/UI/Common/TempFiles.cpp
  CPP/7zip/UI/Common/Update.cpp
  CPP/7zip/UI/Common/UpdateAction.cpp
  CPP/7zip/UI/Common/UpdateCallback.cpp
  CPP/7zip/UI/Common/UpdatePair.cpp
  CPP/7zip/UI/Common/UpdateProduce.cpp
)

set(SRC_CONSOLE
  CPP/7zip/UI/Console/BenchCon.cpp
  CPP/7zip/UI/Console/ConsoleClose.cpp
  CPP/7zip/UI/Console/ExtractCallbackConsole.cpp
  CPP/7zip/UI/Console/HashCon.cpp
  CPP/7zip/UI/Console/List.cpp
  CPP/7zip/UI/Console/Main.cpp
  CPP/7zip/UI/Console/MainAr.cpp
  CPP/7zip/UI/Console/OpenCallbackConsole.cpp
  CPP/7zip/UI/Console/PercentPrinter.cpp
  CPP/7zip/UI/Console/UpdateCallbackConsole.cpp
  CPP/7zip/UI/Console/UserInputUtils.cpp
)

set(SRC_C
  C/7zStream.c
  C/Alloc.c
  C/Bcj2.c
  C/Bcj2Enc.c
  C/Bra.c
  C/Bra86.c
  C/BraIA64.c
  C/BwtSort.c
  C/CpuArch.c
  C/Delta.c
  C/HuffEnc.c
  C/LzFind.c
  C/LzFindOpt.c
  C/Lzma2Dec.c
  C/Lzma2DecMt.c
  C/Lzma2Enc.c
  C/LzmaDec.c
  C/LzmaEnc.c
  C/MtCoder.c
  C/MtDec.c
  C/Ppmd7.c
  C/Ppmd7Dec.c
  C/Ppmd7Enc.c
  C/Ppmd8.c
  C/Ppmd8Dec.c
  C/Ppmd8Enc.c
  C/Sort.c
  C/Xz.c
  C/XzDec.c
  C/XzEnc.c
  C/XzIn.c
  C/XzCrc64.c
  C/XzCrc64Opt.c
  C/7zCrc.c
  C/7zCrcOpt.c
  C/Aes.c
  C/AesOpt.c
  C/Sha1.c
  C/Sha1Opt.c
  C/Sha256.c
  C/Sha256Opt.c
  C/LzFindMt.c                        # MT
  C/Threads.c                         # MT
  C/hashes/md2.c
  C/hashes/md4.c
  C/hashes/md5.c
  C/hashes/sha512.c
  C/hashes/blake3.c
)

set(SRC_ZSTDMT
  C/zstdmt/lz4-mt_common.c
  C/zstdmt/lz4-mt_compress.c
  C/zstdmt/lz4-mt_decompress.c
  C/zstdmt/brotli-mt_common.c
  C/zstdmt/brotli-mt_compress.c
  C/zstdmt/brotli-mt_decompress.c
  C/zstdmt/lizard-mt_common.c
  C/zstdmt/lizard-mt_compress.c
  C/zstdmt/lizard-mt_decompress.c
  C/zstdmt/lz5-mt_common.c
  C/zstdmt/lz5-mt_compress.c
  C/zstdmt/lz5-mt_decompress.c
  C/zstdmt/zstd-mt_threading.c
)

# ===========================================================================
# third-party codec libraries, compiled in (upstream builds them as
# separate shared libs; here they become part of lib7za.so)
# ===========================================================================

file(GLOB SRC_ZSTD CONFIGURE_DEPENDS
  ${Z7_ROOT}/C/zstd/lib/common/*.c
  ${Z7_ROOT}/C/zstd/lib/compress/*.c
  ${Z7_ROOT}/C/zstd/lib/decompress/*.c
  ${Z7_ROOT}/C/zstd/lib/dictBuilder/*.c
)

set(SRC_LZ4
  C/lz4/lib/lz4.c
  C/lz4/lib/lz4frame.c
  C/lz4/lib/lz4hc.c
  C/lz4/lib/xxhash.c
)

file(GLOB SRC_BROTLI CONFIGURE_DEPENDS
  ${Z7_ROOT}/C/brotli/c/common/*.c
  ${Z7_ROOT}/C/brotli/c/enc/*.c
  ${Z7_ROOT}/C/brotli/c/dec/*.c
)

set(SRC_LIZARD
  C/lizard/lib/lizard_compress.c
  C/lizard/lib/lizard_decompress.c
  C/lizard/lib/lizard_frame.c
  C/lizard/lib/entropy/entropy_common.c
  C/lizard/lib/entropy/fse_compress.c
  C/lizard/lib/entropy/fse_decompress.c
  C/lizard/lib/entropy/huf_compress.c
  C/lizard/lib/entropy/huf_decompress.c
  C/lizard/lib/xxhash/xxhash.c
)

set(SRC_LZ5
  C/lz5/lib/lz5.c
  C/lz5/lib/lz5frame.c
  C/lz5/lib/lz5hc.c
  C/lz5/lib/xxhash.c
)

file(GLOB SRC_FASTLZMA2 CONFIGURE_DEPENDS ${Z7_ROOT}/C/fast-lzma2/*.c)

# ---------------------------------------------------------------------------
# per-library symbol namespacing / feature defines.
# Several vendored libs each carry an own copy of xxhash (plain XXH* symbols);
# lizard additionally carries FSE/HUF/ERR copies that clash with zstd.
# Properties must be set on the absolute paths actually used by the target.
# ---------------------------------------------------------------------------

foreach(l
    SRC_COMMON SRC_WINDOWS SRC_7ZIP_COMMON SRC_ARCHIVE SRC_COMPRESS
    SRC_CRYPTO SRC_UI_COMMON SRC_CONSOLE SRC_C SRC_ZSTDMT
    SRC_LZ4 SRC_LIZARD SRC_LZ5)
  list(TRANSFORM ${l} PREPEND "${Z7_ROOT}/")
endforeach()

# zstd: its xxhash copy is already ZSTD_-namespaced by its own header.
set_source_files_properties(${SRC_ZSTD} PROPERTIES COMPILE_DEFINITIONS
  "ZSTD_MULTITHREAD;ZSTD_LEGACY_SUPPORT=0;ZSTD_DISABLE_ASM")

# lz4 / lz5 / lizard xxhash copies get their own namespaces.
set_source_files_properties(${SRC_LZ4} PROPERTIES COMPILE_DEFINITIONS
  "XXH_NAMESPACE=LZ4_")
set_source_files_properties(${SRC_LZ5} PROPERTIES COMPILE_DEFINITIONS
  "XXH_NAMESPACE=LZ5_")
# lizard carries its own copies of FSE/HUF (also present in zstd) and xxhash;
# rename all of its FSE_*/HUF_* globals with a LIZARD_ prefix so the single
# lib7za.so link doesn't clash with zstd. lizard_*.c call these too, so the
# defines apply to the whole lizard source group.
set(LIZARD_RENAMES XXH_NAMESPACE=LIZARD_)
foreach(s
    FSE_NCountWriteBound FSE_buildCTable FSE_buildCTable_raw FSE_buildCTable_rle
    FSE_buildCTable_wksp FSE_buildDTable FSE_buildDTable_raw FSE_buildDTable_rle
    FSE_compress FSE_compress2 FSE_compressBound FSE_compress_usingCTable
    FSE_compress_wksp FSE_count FSE_countFast FSE_countFast_wksp FSE_count_simple
    FSE_count_wksp FSE_createCTable FSE_createDTable FSE_decompress
    FSE_decompress_usingDTable FSE_decompress_wksp FSE_freeCTable FSE_freeDTable
    FSE_getErrorName FSE_isError FSE_normalizeCount FSE_optimalTableLog
    FSE_optimalTableLog_internal FSE_readNCount FSE_sizeof_CTable
    FSE_versionNumber FSE_writeNCount
    HUF_buildCTable HUF_buildCTable_wksp HUF_compress HUF_compress1X
    HUF_compress1X_usingCTable HUF_compress1X_wksp HUF_compress2
    HUF_compress4X_usingCTable HUF_compress4X_wksp HUF_compressBound
    HUF_compressWeights HUF_decompress HUF_decompress1X2 HUF_decompress1X2_DCtx
    HUF_decompress1X2_usingDTable HUF_decompress1X4 HUF_decompress1X4_DCtx
    HUF_decompress1X4_usingDTable HUF_decompress1X_DCtx
    HUF_decompress1X_usingDTable HUF_decompress4X2 HUF_decompress4X2_DCtx
    HUF_decompress4X2_usingDTable HUF_decompress4X4 HUF_decompress4X4_DCtx
    HUF_decompress4X4_usingDTable HUF_decompress4X_DCtx
    HUF_decompress4X_hufOnly HUF_decompress4X_usingDTable HUF_getErrorName
    HUF_isError HUF_optimalTableLog HUF_readCTable HUF_readDTableX2
    HUF_readDTableX4 HUF_readStats HUF_selectDecoder HUF_writeCTable)
  list(APPEND LIZARD_RENAMES "${s}=LIZARD_${s}")
endforeach()
set_source_files_properties(${SRC_LIZARD} PROPERTIES COMPILE_DEFINITIONS
  "${LIZARD_RENAMES}")

# fast-lzma2 keeps the canonical plain XXH* symbols
# (CPP/Common/XXH32Reg.cpp / XXH64Reg.cpp include C/fast-lzma2/xxhash.h).

set(Z7_SOURCES
  ${SRC_COMMON} ${SRC_WINDOWS} ${SRC_7ZIP_COMMON} ${SRC_ARCHIVE}
  ${SRC_COMPRESS} ${SRC_CRYPTO} ${SRC_UI_COMMON} ${SRC_CONSOLE}
  ${SRC_C} ${SRC_ZSTDMT}
  ${SRC_ZSTD} ${SRC_LZ4} ${SRC_BROTLI} ${SRC_LIZARD} ${SRC_LZ5}
  ${SRC_FASTLZMA2}
)

add_library(7za SHARED ${Z7_SOURCES})
set_target_properties(7za PROPERTIES
  OUTPUT_NAME 7za
  PREFIX "lib"
)

target_include_directories(7za PRIVATE
  "${Z7_ROOT}/C/brotli/c/include"   # Brotli*.cpp / brotli-mt_*.c
  "${Z7_ROOT}/C"                    # fast-lzma2 / zstdmt cross-includes
)

# rename the console entry point: main -> sevenzip_main
set_source_files_properties(
  "${Z7_ROOT}/CPP/7zip/UI/Console/MainAr.cpp"
  PROPERTIES COMPILE_DEFINITIONS "main=sevenzip_main"
)

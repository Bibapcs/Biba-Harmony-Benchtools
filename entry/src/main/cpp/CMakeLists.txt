# the minimum version of CMake.
cmake_minimum_required(VERSION 3.5.0)
project(bbpcs)

set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

if(DEFINED PACKAGE_FIND_FILE)
    include(${PACKAGE_FIND_FILE})
endif()

include_directories(${NATIVERENDER_ROOT_PATH}
                    ${NATIVERENDER_ROOT_PATH}/include)

# vkpeak 工程（含 ncnn/glslang），只取其中的 ncnn 静态库和 vkpeak.cpp
set(VKPEAK_ROOT ${NATIVERENDER_ROOT_PATH}/../../../../third_party/vkpeak)
add_subdirectory(${VKPEAK_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/vkpeak EXCLUDE_FROM_ALL)

# 7-Zip ZS 工程（ohos/CMakeLists.txt 为移植时新建，上游零改动），产出 lib7za.so
add_subdirectory(${NATIVERENDER_ROOT_PATH}/../../../../third_party/7zip-source-code/ohos
    ${CMAKE_CURRENT_BINARY_DIR}/7za EXCLUDE_FROM_ALL)

# pbrt-v4 工程（ohos/CMakeLists.txt 为移植时新建），产出 libpbrt.so
add_subdirectory(${NATIVERENDER_ROOT_PATH}/../../../../third_party/pbrt-v4/ohos
    ${CMAKE_CURRENT_BINARY_DIR}/pbrt EXCLUDE_FROM_ALL)

# ArgyllCMS i1Pro3 仪器栈子集（OHOS 移植），产出 libargyll_i1.a
add_subdirectory(${NATIVERENDER_ROOT_PATH}/argyll
    ${CMAKE_CURRENT_BINARY_DIR}/argyll EXCLUDE_FROM_ALL)

add_library(entry SHARED
    napi_init.cpp
    stress.cpp
    gpu_burn.cpp
    diskmark.cpp
    pbrt_runner.cpp
    i1probe.cpp
    i1meas.cpp
    p3surf.cpp
    ${VKPEAK_ROOT}/vkpeak.cpp)

# 把 vkpeak.cpp 的 main 重命名为 vkpeak_main，避免与应用入口冲突
set_source_files_properties(${VKPEAK_ROOT}/vkpeak.cpp
    PROPERTIES COMPILE_DEFINITIONS "main=vkpeak_main")

# i1meas.cpp 包含 Argyll 头文件，需要与 argyll 库一致的平台宏（UNIX/ENABLE_USB）
set_source_files_properties(i1meas.cpp
    PROPERTIES COMPILE_DEFINITIONS "UNIX;ENABLE_USB")

target_link_libraries(entry PUBLIC libace_napi.z.so ncnn child_process 7za libusb_ndk.z.so libhilog_ndk.z.so argyll_i1
    libEGL.so libGLESv2.so libnative_window.so libnative_buffer.so libace_ndk.z.so)

# 烤机 CPU worker：独立 native 子进程（OH_Ability_StartNativeChildProcess 加载）
add_library(stress_worker SHARED stress_worker.cpp)
target_link_libraries(stress_worker PUBLIC child_process)

# pbrt 渲染 worker：独立 native 子进程
add_library(pbrt_worker SHARED pbrt_worker.cpp)
target_link_libraries(pbrt_worker PUBLIC pbrt child_process)

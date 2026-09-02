#include "napi/native_api.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "stress.h"
#include "diskmark.h"
#include "pbrt_runner.h"

// i1probe.cpp 中的探针函数注册
extern void i1probe_register(napi_env env, napi_value exports);

// i1meas.cpp 中的 i1Pro3 测量编排函数注册
extern void i1meas_register(napi_env env, napi_value exports);

// p3surf.cpp 的 XComponent 广色域渲染面（EGL colorspace 扩展）注册
extern void p3surf_register(napi_env env, napi_value exports);

// vkpeak.cpp 的 main 在编译期被重命名为 vkpeak_main（见 CMakeLists.txt）
extern int vkpeak_main(int argc, char** argv);

// 7-Zip 的 main（MainAr.cpp）在编译期被重命名为 sevenzip_main（C++ 符号）
extern int sevenzip_main(int argc, char** argv);

static std::atomic<bool> g_vkpeak_running{false};
static std::atomic<bool> g_7z_running{false};

static std::string log_path(const std::string& dir)
{
    return dir + "/vkpeak_log.txt";
}

static napi_value runVkpeak(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    char dirbuf[1024] = {0};
    size_t dirlen = 0;
    napi_get_value_string_utf8(env, args[0], dirbuf, sizeof(dirbuf) - 1, &dirlen);
    std::string dir(dirbuf, dirlen);

    int device_id = 0;
    napi_get_value_int32(env, args[1], &device_id);

    bool expected = false;
    if (!g_vkpeak_running.compare_exchange_strong(expected, true)) {
        // 已在运行
        napi_value ret;
        napi_get_boolean(env, false, &ret);
        return ret;
    }

    std::thread([dir, device_id]() {
        // 把 stdout/stderr 重定向到沙箱日志文件，供 ArkTS 轮询
        freopen(log_path(dir).c_str(), "w", stdout);
        dup2(fileno(stdout), fileno(stderr));

        char idbuf[16];
        snprintf(idbuf, sizeof(idbuf), "%d", device_id);
        char arg0[] = "vkpeak";
        char* argv[] = {arg0, idbuf, nullptr};
        vkpeak_main(2, argv);

        fflush(stdout);
        fflush(stderr);
        g_vkpeak_running = false;
    }).detach();

    napi_value ret;
    napi_get_boolean(env, true, &ret);
    return ret;
}

static napi_value isVkpeakRunning(napi_env env, napi_callback_info info)
{
    napi_value ret;
    napi_get_boolean(env, g_vkpeak_running.load(), &ret);
    return ret;
}

static napi_value getVkpeakLog(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    char dirbuf[1024] = {0};
    size_t dirlen = 0;
    napi_get_value_string_utf8(env, args[0], dirbuf, sizeof(dirbuf) - 1, &dirlen);

    std::string content;
    FILE* f = fopen(log_path(std::string(dirbuf, dirlen)).c_str(), "rb");
    if (f) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            content.append(buf, n);
        }
        fclose(f);
    }

    napi_value ret;
    napi_create_string_utf8(env, content.c_str(), content.size(), &ret);
    return ret;
}

// ---------------------------------------------------------------------------
// 7-Zip benchmark：后台线程跑 sevenzip_main("7za b ...")，stdout 重定向到日志文件
// ---------------------------------------------------------------------------

static std::string sevenz_log_path(const std::string& dir)
{
    return dir + "/7z_log.txt";
}

static napi_value run7zBench(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    char params_buf[1024] = {0};
    size_t params_len = 0;
    napi_get_value_string_utf8(env, args[0], params_buf, sizeof(params_buf) - 1, &params_len);
    std::string params(params_buf, params_len);

    char dirbuf[1024] = {0};
    size_t dirlen = 0;
    napi_get_value_string_utf8(env, args[1], dirbuf, sizeof(dirbuf) - 1, &dirlen);
    std::string dir(dirbuf, dirlen);

    bool expected = false;
    if (!g_7z_running.compare_exchange_strong(expected, true)) {
        napi_value ret;
        napi_get_boolean(env, false, &ret);
        return ret;
    }

    std::thread([dir, params]() {
        freopen(sevenz_log_path(dir).c_str(), "w", stdout);
        dup2(fileno(stdout), fileno(stderr));

        // argv: {"7za", "b", <params 按空格拆分>...}
        std::vector<std::string> tokens;
        tokens.push_back("7za");
        tokens.push_back("b");
        std::string cur;
        for (char ch : params) {
            if (ch == ' ' || ch == '\t') {
                if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
            } else {
                cur += ch;
            }
        }
        if (!cur.empty()) tokens.push_back(cur);

        std::vector<char*> argv;
        for (std::string& t : tokens) argv.push_back((char*)t.c_str());
        argv.push_back(nullptr);

        sevenzip_main((int)argv.size() - 1, argv.data());

        fflush(stdout);
        fflush(stderr);
        g_7z_running = false;
    }).detach();

    napi_value ret;
    napi_get_boolean(env, true, &ret);
    return ret;
}

static napi_value is7zRunning(napi_env env, napi_callback_info info)
{
    napi_value ret;
    napi_get_boolean(env, g_7z_running.load(), &ret);
    return ret;
}

static napi_value get7zLog(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    char dirbuf[1024] = {0};
    size_t dirlen = 0;
    napi_get_value_string_utf8(env, args[0], dirbuf, sizeof(dirbuf) - 1, &dirlen);

    std::string content;
    FILE* f = fopen(sevenz_log_path(std::string(dirbuf, dirlen)).c_str(), "rb");
    if (f) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            content.append(buf, n);
        }
        fclose(f);
    }

    napi_value ret;
    napi_create_string_utf8(env, content.c_str(), content.size(), &ret);
    return ret;
}

static napi_value startStress(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    int mode = 0;
    napi_get_value_int32(env, args[0], &mode);

    napi_value ret;
    napi_create_int32(env, stress::start(mode), &ret);
    return ret;
}

static napi_value stopStress(napi_env env, napi_callback_info info)
{
    stress::stop();
    return nullptr;
}

static napi_value getStressStatus(napi_env env, napi_callback_info info)
{
    std::string s = stress::status();
    napi_value ret;
    napi_create_string_utf8(env, s.c_str(), s.size(), &ret);
    return ret;
}

// ---------------------------------------------------------------------------
// CrystalDiskMark（diskmark.cpp）：后台线程跑，日志写 cdm_log.txt
// ---------------------------------------------------------------------------

static napi_value runDiskBench(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    int tests = 15;
    napi_get_value_int32(env, args[0], &tests);

    char dirbuf[1024] = {0};
    size_t dirlen = 0;
    napi_get_value_string_utf8(env, args[1], dirbuf, sizeof(dirbuf) - 1, &dirlen);
    dirbuf[dirlen] = 0;

    napi_value ret;
    napi_create_int32(env, diskmark::start(tests, dirbuf), &ret);
    return ret;
}

static napi_value stopDiskBench(napi_env env, napi_callback_info info)
{
    diskmark::stop();
    return nullptr;
}

static napi_value isDiskRunning(napi_env env, napi_callback_info info)
{
    napi_value ret;
    napi_get_boolean(env, diskmark::running(), &ret);
    return ret;
}

static napi_value getDiskLog(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    char dirbuf[1024] = {0};
    size_t dirlen = 0;
    napi_get_value_string_utf8(env, args[0], dirbuf, sizeof(dirbuf) - 1, &dirlen);

    std::string path = std::string(dirbuf, dirlen) + "/cdm_log.txt";
    std::string content;
    FILE* f = fopen(path.c_str(), "rb");
    if (f) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            content.append(buf, n);
        }
        fclose(f);
    }

    napi_value ret;
    napi_create_string_utf8(env, content.c_str(), content.size(), &ret);
    return ret;
}

// ---------------------------------------------------------------------------
// pbrt 渲染（pbrt_runner.cpp）：子进程跑渲染，日志读 pbrt_log.txt
// ---------------------------------------------------------------------------

static napi_value runPbrt(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    int spp = 64;
    napi_get_value_int32(env, args[0], &spp);

    char dirbuf[1024] = {0};
    size_t dirlen = 0;
    napi_get_value_string_utf8(env, args[1], dirbuf, sizeof(dirbuf) - 1, &dirlen);
    dirbuf[dirlen] = 0;

    napi_value ret;
    napi_create_int32(env, pbrt_runner::start(spp, dirbuf), &ret);
    return ret;
}

static napi_value stopPbrt(napi_env env, napi_callback_info info)
{
    pbrt_runner::stop();
    return nullptr;
}

static napi_value isPbrtRunning(napi_env env, napi_callback_info info)
{
    napi_value ret;
    napi_get_boolean(env, pbrt_runner::running(), &ret);
    return ret;
}

static napi_value getPbrtExitSignal(napi_env env, napi_callback_info info)
{
    napi_value ret;
    napi_create_int32(env, pbrt_runner::last_exit_signal(), &ret);
    return ret;
}

// 写场景文件：native 写盘（ArkTS fs 写入的路径子进程不可见，native 写的可见）
// 参数: filesDir, relPath, ArrayBuffer
static napi_value writeSceneFile(napi_env env, napi_callback_info info)
{
    size_t argc = 3;
    napi_value args[3] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    char dirbuf[1024] = {0};
    size_t dirlen = 0;
    napi_get_value_string_utf8(env, args[0], dirbuf, sizeof(dirbuf) - 1, &dirlen);

    char relbuf[256] = {0};
    size_t rellen = 0;
    napi_get_value_string_utf8(env, args[1], relbuf, sizeof(relbuf) - 1, &rellen);

    void* data = nullptr;
    size_t datalen = 0;
    napi_get_arraybuffer_info(env, args[2], &data, &datalen);

    std::string path = std::string(dirbuf, dirlen) + "/scene/" + std::string(relbuf, rellen);

    // 逐级创建父目录
    std::string parent = path.substr(0, path.find_last_of('/'));
    {
        std::string cur;
        for (char c : parent) {
            cur += c;
            if (c == '/' && cur.size() > 1)
                mkdir(cur.c_str(), 0755);
        }
        mkdir(parent.c_str(), 0755);
    }

    int written = -1;
    FILE* f = fopen(path.c_str(), "wb");
    if (f) {
        if (datalen == 0 || fwrite(data, 1, datalen, f) == datalen)
            written = (int)datalen;
        fclose(f);
    }

    napi_value ret;
    napi_create_int32(env, written, &ret);
    return ret;
}

static napi_value getPbrtLog(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    char dirbuf[1024] = {0};
    size_t dirlen = 0;
    napi_get_value_string_utf8(env, args[0], dirbuf, sizeof(dirbuf) - 1, &dirlen);

    std::string path = std::string(dirbuf, dirlen) + "/pbrt_log.txt";
    std::string content;
    FILE* f = fopen(path.c_str(), "rb");
    if (f) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            content.append(buf, n);
        }
        fclose(f);
    }

    napi_value ret;
    napi_create_string_utf8(env, content.c_str(), content.size(), &ret);
    return ret;
}

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        {"runVkpeak", nullptr, runVkpeak, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"isVkpeakRunning", nullptr, isVkpeakRunning, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getVkpeakLog", nullptr, getVkpeakLog, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"startStress", nullptr, startStress, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"stopStress", nullptr, stopStress, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getStressStatus", nullptr, getStressStatus, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"run7zBench", nullptr, run7zBench, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"is7zRunning", nullptr, is7zRunning, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"get7zLog", nullptr, get7zLog, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"runDiskBench", nullptr, runDiskBench, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"stopDiskBench", nullptr, stopDiskBench, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"isDiskRunning", nullptr, isDiskRunning, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getDiskLog", nullptr, getDiskLog, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"runPbrt", nullptr, runPbrt, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"stopPbrt", nullptr, stopPbrt, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"isPbrtRunning", nullptr, isPbrtRunning, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getPbrtExitSignal", nullptr, getPbrtExitSignal, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getPbrtLog", nullptr, getPbrtLog, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"writeSceneFile", nullptr, writeSceneFile, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    i1probe_register(env, exports);
    i1meas_register(env, exports);
    p3surf_register(env, exports);
    return exports;
}
EXTERN_C_END

static napi_module entryModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "entry",
    .nm_priv = ((void*)0),
    .reserved = {0},
};

extern "C" __attribute__((constructor)) void RegisterEntryModule(void)
{
    napi_module_register(&entryModule);
}

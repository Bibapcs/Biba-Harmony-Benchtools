// pbrt 渲染 runner（主进程侧）：只管子进程生命周期
// 渲染负载在独立 native 子进程（libpbrt_worker.so）中，
// 停止即 KillChildProcess，子进程崩溃/被杀不影响主进程。

#include "pbrt_runner.h"

#include <AbilityKit/native_child_process.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace pbrt_runner {

static std::mutex g_mutex;
static int32_t g_pid = -1;
static std::atomic<bool> g_exited{false};
static std::atomic<int> g_exit_signal{0};
static std::once_flag g_callback_once;

static void on_child_exit(int32_t pid, int32_t signal)
{
    if (pid == g_pid)
    {
        g_exit_signal = signal;
        g_exited = true;
    }
}

int start(int spp, const char* filesDir)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_pid > 0 && !g_exited.load())
        return -1;

    std::call_once(g_callback_once, []() {
        OH_Ability_RegisterNativeChildProcessExitCallback(on_child_exit);
    });

    // entryParams: "<filesDir>|<spp>"
    char params[1200];
    snprintf(params, sizeof(params), "%s|%d", filesDir, spp);

    NativeChildProcess_Args args;
    args.entryParams = params;
    args.fdList.head = nullptr;

    NativeChildProcess_Options options;
    options.isolationMode = NCP_ISOLATION_MODE_NORMAL;
    options.reserved = 0;

    g_exited = false;

    int32_t pid = -1;
    int ret = OH_Ability_StartNativeChildProcess("libpbrt_worker.so:Main", args, options, &pid);
    if (ret != NCP_NO_ERROR || pid <= 0)
        return -3;

    g_pid = pid;
    return 0;
}

void stop()
{
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_pid > 0 && !g_exited.load())
    {
        OH_Ability_KillChildProcess(g_pid);
    }
    g_pid = -1;
}

bool running()
{
    return g_pid > 0 && !g_exited.load();
}

int last_exit_signal()
{
    return g_exit_signal.load();
}

} // namespace pbrt_runner

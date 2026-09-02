// 烤机 wrapper（混合架构）：
// - CPU 负载：独立 native 子进程（libstress_worker.so），被系统杀掉也不影响本进程
// - GPU 负载：本进程后台线程（子进程无法获取 Vulkan 设备，实测错误码 1）
// 本进程主线程只做轻量管理，始终健康，鸿蒙卡死检测不会命中。

#include "stress.h"

#include <AbilityKit/native_child_process.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gpu_burn.h"

// 与 worker 共享的状态结构（布局必须与 stress_worker.cpp 一致）
struct StressShared {
    std::atomic<int> stop;
    std::atomic<int> running_mode;
    std::atomic<int> child_ready;
    std::atomic<unsigned long long> cpu_flops;
};

namespace stress {

static std::mutex g_mutex;
static int g_mode = 0;

// CPU worker 子进程状态
static int32_t g_pid = -1;
static int g_fd = -1;
static StressShared* g_shm = nullptr;
static std::atomic<bool> g_child_exited{false};
static std::once_flag g_callback_once;

// GPU 线程状态（本进程）
static std::thread g_gpu_thread;
static std::atomic<int> g_gpu_stop{0};
static std::atomic<unsigned long long> g_gpu_dispatches{0};
static std::atomic<int> g_gpu_error{0};

static std::chrono::steady_clock::time_point g_start_time;

static void on_child_exit(int32_t pid, int32_t /*signal*/)
{
    if (pid == g_pid)
        g_child_exited = true;
}

// 停止本进程 GPU 线程（调用前需持锁）
static void stop_gpu_thread_locked()
{
    if (g_gpu_thread.joinable())
    {
        g_gpu_stop = 1;
        g_gpu_thread.join();
    }
}

// 清理 CPU worker（调用前需持锁）
static void cleanup_child_locked()
{
    if (g_shm)
    {
        munmap(g_shm, 4096);
        g_shm = nullptr;
    }
    if (g_fd >= 0)
    {
        close(g_fd);
        g_fd = -1;
    }
    g_pid = -1;
}

static int start_cpu_child_locked()
{
    int fd = memfd_create("stress_shm", 0);
    if (fd < 0)
        return -3;
    if (ftruncate(fd, 4096) != 0)
    {
        close(fd);
        return -3;
    }
    void* p = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED)
    {
        close(fd);
        return -3;
    }
    memset(p, 0, 4096);

    char mode_buf[8] = "1"; // worker 只负责 CPU

    NativeChildProcess_Fd shm_fd;
    shm_fd.fdName = (char*)"shm";
    shm_fd.fd = fd;
    shm_fd.next = nullptr;

    NativeChildProcess_Args args;
    args.entryParams = mode_buf;
    args.fdList.head = &shm_fd;

    NativeChildProcess_Options options;
    options.isolationMode = NCP_ISOLATION_MODE_NORMAL;
    options.reserved = 0;

    g_child_exited = false;

    int32_t pid = -1;
    int ret = OH_Ability_StartNativeChildProcess("libstress_worker.so:Main", args, options, &pid);
    if (ret != NCP_NO_ERROR || pid <= 0)
    {
        munmap(p, 4096);
        close(fd);
        return -3;
    }

    g_fd = fd;
    g_shm = (StressShared*)p;
    g_pid = pid;
    return 0;
}

int start(int mode)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    if (mode < 1 || mode > 3)
        return -1;

    // 上一轮会话未清理（worker 已自行退出等）：先清理
    if (g_mode != 0)
    {
        if (g_gpu_thread.joinable() || !g_child_exited.load())
            return -1;
        cleanup_child_locked();
        g_mode = 0;
    }

    std::call_once(g_callback_once, []() {
        OH_Ability_RegisterNativeChildProcessExitCallback(on_child_exit);
    });

    g_gpu_dispatches = 0;
    g_gpu_error = 0;

    if (mode & 1)
    {
        int ret = start_cpu_child_locked();
        if (ret != 0)
            return ret;
    }

    if (mode & 2)
    {
        g_gpu_stop = 0;
        g_gpu_thread = std::thread([]() {
            gpu_burn(&g_gpu_stop, &g_gpu_dispatches, &g_gpu_error);
        });
    }

    g_mode = mode;
    g_start_time = std::chrono::steady_clock::now();
    return 0;
}

void stop()
{
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_mode == 0)
        return;

    // 停 GPU 线程（本进程）
    stop_gpu_thread_locked();

    // 停 CPU worker：先礼后兵，置停止标志等其自行退出，超时强杀
    if (g_pid > 0)
    {
        if (g_shm && !g_child_exited.load())
        {
            g_shm->stop = 1;

            for (int i = 0; i < 30 && !g_child_exited.load(); i++)
            {
                g_mutex.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                g_mutex.lock();
            }
        }

        if (!g_child_exited.load())
        {
            OH_Ability_KillChildProcess(g_pid);
        }
    }

    cleanup_child_locked();
    g_mode = 0;
}

std::string status()
{
    std::lock_guard<std::mutex> lock(g_mutex);

    const bool child_alive = g_pid > 0 && !g_child_exited.load();
    const bool gpu_alive = g_gpu_thread.joinable();
    const bool running = g_mode != 0 && (child_alive || gpu_alive);
    const int mode = g_mode; // 保留退出前的模式，便于查看最后的错误信息

    const double elapsed = running
        ? std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start_time).count()
        : 0.0;

    unsigned long long cpu_flops = g_shm ? g_shm->cpu_flops.load() : 0;
    const double cpu_gflops = (running && elapsed > 0.0) ? (double)cpu_flops / elapsed / 1e9 : 0.0;
    const int cpu_threads = (g_mode & 1) ? (int)std::thread::hardware_concurrency() : 0;

    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"mode\":%d,\"running\":%s,\"elapsedSec\":%.1f,\"cpuThreads\":%d,\"cpuGflops\":%.2f,\"gpuDispatches\":%llu,\"gpuError\":%s,\"gpuErrorCode\":%d,\"workerPid\":%d}",
        mode,
        running ? "true" : "false",
        elapsed,
        cpu_threads,
        cpu_gflops,
        g_gpu_dispatches.load(),
        g_gpu_error.load() ? "true" : "false",
        g_gpu_error.load(),
        child_alive ? (int)g_pid : -1);
    return buf;
}

} // namespace stress

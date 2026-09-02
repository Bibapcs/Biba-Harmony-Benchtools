// 烤机 CPU worker：运行在独立的 native 子进程中
// 主进程（wrapper）通过共享内存下发停止标志、读取计数，
// 即使本子进程被系统判定高负载/卡死而杀掉，主进程也不受影响。
// （GPU 烤机放在主进程后台线程，子进程无法获取 Vulkan 设备）
//
// 入口由 OH_Ability_StartNativeChildProcess("libstress_worker.so:Main", ...) 调用。

#include <AbilityKit/native_child_process.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include <sys/mman.h>

// 与主进程共享的状态结构（放在 memfd 共享内存里，布局必须与 stress.cpp 一致）
struct StressShared {
    std::atomic<int> stop;              // 主进程 -> worker：请求停止
    std::atomic<int> running_mode;      // worker -> 主进程：当前模式（worker 只跑 CPU，恒为 1）
    std::atomic<int> child_ready;
    std::atomic<unsigned long long> cpu_flops;      // FMA 按 2 flops 计
};

static StressShared* g_shm = nullptr;
static volatile double g_cpu_sink = 0.0;

// CPU 烤机：每线程 4 条独立的 FP64 FMA 依赖链，打满浮点单元
static void cpu_burn()
{
    const double a0 = 1.0000001, a1 = 1.0000002, a2 = 0.9999999, a3 = 0.9999998;
    double c0 = 0.1, c1 = 0.2, c2 = 0.3, c3 = 0.4;

    while (!g_shm->stop.load(std::memory_order_relaxed))
    {
        for (int i = 0; i < 16384; i++)
        {
            c0 = c0 * a0 + 0.11;
            c1 = c1 * a1 + 0.12;
            c2 = c2 * a2 + 0.13;
            c3 = c3 * a3 + 0.14;
        }
        g_shm->cpu_flops.fetch_add(16384 * 4 * 2, std::memory_order_relaxed);
    }

    g_cpu_sink = c0 + c1 + c2 + c3;
}

extern "C" void Main(NativeChildProcess_Args args)
{
    int fd = -1;
    for (NativeChildProcess_Fd* n = args.fdList.head; n; n = n->next)
    {
        if (n->fdName && strcmp(n->fdName, "shm") == 0)
            fd = n->fd;
    }
    if (fd < 0)
        return;

    void* p = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED)
        return;
    g_shm = (StressShared*)p;

    g_shm->running_mode = 1;
    g_shm->child_ready = 1;

    const unsigned int ncpu = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < ncpu; i++)
    {
        threads.emplace_back(cpu_burn);
    }

    for (std::thread& t : threads)
    {
        if (t.joinable())
            t.join();
    }

    g_shm->running_mode = 0;

    munmap(p, 4096);
}

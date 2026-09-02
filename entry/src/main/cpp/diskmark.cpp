// CrystalDiskMark 方法学的 HarmonyOS 实现（内置存储）
// - SEQ1M Q8T1：1 MiB 块、8 条并发顺序流（对应 CDM 的 8 深度队列），各测 3 轮取最优
// - RND4K Q1T1：单线程 4 KiB 随机同步 IO，计时 10 秒，报 MB/s 与 IOPS
// 优先使用 O_DIRECT 绕过页缓存；不可用时退化并标注。

#include "diskmark.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace diskmark {

static const int64_t FILE_SIZE = 1024LL * 1024 * 1024; // 1 GiB
static const int SEQ_BLOCK = 1024 * 1024;              // 1 MiB
static const int SEQ_STREAMS = 8;                      // Q8
static const int SEQ_RUNS = 3;
static const int RND_SECONDS = 10;

static std::atomic<bool> g_running{false};
static std::atomic<bool> g_stop{false};
static std::thread g_thread;

static std::string g_dir;
static FILE* g_log = nullptr;

static void log_line(const char* fmt, ...)
{
    if (!g_log)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static double now_sec()
{
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static std::string test_path(const char* name)
{
    return g_dir + "/" + name;
}

// 打开文件，优先 O_DIRECT；返回 fd，o_direct_used 输出实际是否生效
static int open_file(const std::string& path, int flags, bool* o_direct_used)
{
#ifdef O_DIRECT
    int fd = open(path.c_str(), flags | O_DIRECT, 0644);
    if (fd >= 0)
    {
        *o_direct_used = true;
        return fd;
    }
#endif
    *o_direct_used = false;
    return open(path.c_str(), flags, 0644);
}

// 准备测试文件（不存在或大小不对时写入伪随机数据）
static bool prepare_file(const std::string& path, int64_t size, bool write_random)
{
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && st.st_size == size)
        return true;

    log_line("准备测试文件 %s (%lld MiB)...", path.c_str(), (long long)(size / 1024 / 1024));

    bool od = false;
    int fd = open_file(path, O_RDWR | O_CREAT | O_TRUNC, &od);
    if (fd < 0)
    {
        log_line("无法创建测试文件: %s", strerror(errno));
        return false;
    }

    void* buf = nullptr;
    if (posix_memalign(&buf, 4096, SEQ_BLOCK) != 0)
    {
        close(fd);
        return false;
    }

    // xorshift 填充伪随机数据
    uint64_t xs = 0x9E3779B97F4A7C15ULL;
    for (int i = 0; i < SEQ_BLOCK / 8; i++)
    {
        xs ^= xs << 13; xs ^= xs >> 7; xs ^= xs << 17;
        ((uint64_t*)buf)[i] = write_random ? xs : 0;
    }

    bool ok = true;
    for (int64_t off = 0; off < size && !g_stop.load(); off += SEQ_BLOCK)
    {
        if (pwrite(fd, buf, SEQ_BLOCK, off) != SEQ_BLOCK)
        {
            log_line("写入测试文件失败: %s", strerror(errno));
            ok = false;
            break;
        }
    }
    fsync(fd);
    free(buf);
    close(fd);
    return ok && !g_stop.load();
}

// SEQ1M Q8T1 读测试：8 条并发顺序流读同一个文件的 1/8 区域
static double seq_read_test(const std::string& path, int run_idx, bool* od_used)
{
    std::atomic<int64_t> total_bytes{0};
    std::atomic<int> err{0};

    const int64_t region = FILE_SIZE / SEQ_STREAMS;

    double t0 = now_sec();
    std::vector<std::thread> threads;
    for (int s = 0; s < SEQ_STREAMS; s++)
    {
        threads.emplace_back([&, s]() {
            bool od = false;
            int fd = open_file(path, O_RDONLY, &od);
            if (od)
                *od_used = od;
            if (fd < 0)
            {
                err = 1;
                return;
            }

            void* buf = nullptr;
            if (posix_memalign(&buf, 4096, SEQ_BLOCK) != 0)
            {
                close(fd);
                err = 1;
                return;
            }

            const int64_t begin = region * s;
            for (int64_t off = 0; off < region && !g_stop.load(); off += SEQ_BLOCK)
            {
                if (pread(fd, buf, SEQ_BLOCK, begin + off) != SEQ_BLOCK)
                {
                    err = 1;
                    break;
                }
                total_bytes += SEQ_BLOCK;
            }

            free(buf);
            close(fd);
        });
    }
    for (auto& t : threads) t.join();
    double t1 = now_sec();

    if (err.load() || total_bytes.load() == 0)
    {
        log_line("  Read run %d: 出错（%s）", run_idx, strerror(errno));
        return 0;
    }

    const double mbs = (double)total_bytes.load() / (t1 - t0) / 1024 / 1024;
    log_line("  Read run %d: %.2f MB/s", run_idx, mbs);
    return mbs;
}

// SEQ1M Q8T1 写测试：8 个独立文件（避免同一 inode 的 DIO 写锁竞争），
// 文件预先填充，覆写不截断（与 CDM 一致，不计块分配开销）
static double seq_write_test(int run_idx, bool* od_used)
{
    std::atomic<int64_t> total_bytes{0};
    std::atomic<int> err{0};

    const int64_t region = FILE_SIZE / SEQ_STREAMS;

    double t0 = now_sec();
    std::vector<std::thread> threads;
    for (int s = 0; s < SEQ_STREAMS; s++)
    {
        threads.emplace_back([&, s]() {
            char name[64];
            snprintf(name, sizeof(name), "cdm_test_write_%d.dat", s);
            bool od = false;
            int fd = open_file(test_path(name), O_RDWR, &od);
            if (od)
                *od_used = od;
            if (fd < 0)
            {
                err = 1;
                return;
            }

            void* buf = nullptr;
            if (posix_memalign(&buf, 4096, SEQ_BLOCK) != 0)
            {
                close(fd);
                err = 1;
                return;
            }
            memset(buf, 0xA5, SEQ_BLOCK);

            for (int64_t off = 0; off < region && !g_stop.load(); off += SEQ_BLOCK)
            {
                if (pwrite(fd, buf, SEQ_BLOCK, off) != SEQ_BLOCK)
                {
                    err = 1;
                    break;
                }
                total_bytes += SEQ_BLOCK;
            }

            fdatasync(fd);
            free(buf);
            close(fd);
        });
    }
    for (auto& t : threads) t.join();
    double t1 = now_sec();

    if (err.load() || total_bytes.load() == 0)
    {
        log_line("  Write run %d: 出错（%s）", run_idx, strerror(errno));
        return 0;
    }

    const double mbs = (double)total_bytes.load() / (t1 - t0) / 1024 / 1024;
    log_line("  Write run %d: %.2f MB/s", run_idx, mbs);
    return mbs;
}

// RND4K Q1T1：单线程 4 KiB 随机同步 IO，计时 RND_SECONDS 秒
static void rnd_test(const std::string& path, bool is_write, double* out_mbs, double* out_iops, bool* od_used)
{
    *out_mbs = 0;
    *out_iops = 0;

    bool od = false;
    int fd = open_file(path, is_write ? O_RDWR : O_RDONLY, &od);
    *od_used = od;
    if (fd < 0)
    {
        log_line("  %s: 打开失败（%s）", is_write ? "Write" : "Read", strerror(errno));
        return;
    }

    void* buf = nullptr;
    if (posix_memalign(&buf, 4096, 4096) != 0)
    {
        close(fd);
        return;
    }
    memset(buf, 0x5A, 4096);

    const int64_t max_off = (FILE_SIZE - 4096) / 4096;
    uint64_t xs = 0xD1B54A32D192ED03ULL;

    // 预热 0.5 秒
    const double warmup_end = now_sec() + 0.5;
    while (now_sec() < warmup_end && !g_stop.load())
    {
        xs ^= xs << 13; xs ^= xs >> 7; xs ^= xs << 17;
        int64_t off = (xs % max_off) * 4096;
        if (is_write) pwrite(fd, buf, 4096, off); else pread(fd, buf, 4096, off);
    }

    int64_t ops = 0;
    const double t0 = now_sec();
    while (now_sec() - t0 < RND_SECONDS && !g_stop.load())
    {
        xs ^= xs << 13; xs ^= xs >> 7; xs ^= xs << 17;
        int64_t off = (xs % max_off) * 4096;
        ssize_t n = is_write ? pwrite(fd, buf, 4096, off) : pread(fd, buf, 4096, off);
        if (n != 4096)
            break;
        ops++;
    }
    const double dt = now_sec() - t0;

    if (is_write)
        fdatasync(fd);
    free(buf);
    close(fd);

    if (ops > 0 && dt > 0)
    {
        *out_iops = ops / dt;
        *out_mbs = *out_iops * 4096 / 1024 / 1024;
        log_line("  %s: %.2f MB/s (%.0f IOPS)", is_write ? "Write" : "Read", *out_mbs, *out_iops);
    }
}

static void run_all(int tests)
{
    g_log = fopen(test_path("cdm_log.txt").c_str(), "w");
    if (!g_log)
    {
        g_running = false;
        return;
    }

    log_line("CrystalDiskMark 方法学 · HarmonyOS 内置存储");
    log_line("测试项: SEQ1M Q8T1 / RND4K Q1T1, 测试文件 1 GiB");
    log_line("");

    const std::string rd_file = test_path("cdm_test_read.dat");

    double seq_r = 0, seq_w = 0, rnd_r_mbs = 0, rnd_w_mbs = 0, rnd_r_iops = 0, rnd_w_iops = 0;
    bool od = false, od_any = false;

    if ((tests & 5) && !g_stop.load()) // SEQ read / RND read 需要读测试文件
    {
        if (!prepare_file(rd_file, FILE_SIZE, true))
        {
            log_line("测试中止。");
            fclose(g_log);
            g_log = nullptr;
            g_running = false;
            return;
        }
    }

    if ((tests & 10) && !g_stop.load()) // SEQ write / RND write 需要 8 个预填充写文件
    {
        const int64_t region = FILE_SIZE / SEQ_STREAMS;
        for (int s = 0; s < SEQ_STREAMS && !g_stop.load(); s++)
        {
            char name[64];
            snprintf(name, sizeof(name), "cdm_test_write_%d.dat", s);
            if (!prepare_file(test_path(name), region, false))
            {
                log_line("测试中止。");
                fclose(g_log);
                g_log = nullptr;
                g_running = false;
                return;
            }
        }
    }

    if ((tests & 1) && !g_stop.load())
    {
        log_line("[SEQ1M Q8T1 Read]");
        for (int r = 0; r < SEQ_RUNS && !g_stop.load(); r++)
            seq_r = std::max(seq_r, seq_read_test(rd_file, r + 1, &od));
        od_any |= od;
        log_line("");
    }

    if ((tests & 2) && !g_stop.load())
    {
        log_line("[SEQ1M Q8T1 Write]");
        for (int r = 0; r < SEQ_RUNS && !g_stop.load(); r++)
            seq_w = std::max(seq_w, seq_write_test(r + 1, &od));
        od_any |= od;
        log_line("");
    }

    if ((tests & 4) && !g_stop.load())
    {
        log_line("[RND4K Q1T1 Read] (%ds)", RND_SECONDS);
        rnd_test(rd_file, false, &rnd_r_mbs, &rnd_r_iops, &od);
        od_any |= od;
        log_line("");
    }

    if ((tests & 8) && !g_stop.load())
    {
        log_line("[RND4K Q1T1 Write] (%ds)", RND_SECONDS);
        rnd_test(test_path("cdm_test_write_0.dat"), true, &rnd_w_mbs, &rnd_w_iops, &od);
        od_any |= od;
        log_line("");
    }

    log_line("========== 结果 ==========");
    if (tests & 3)
        log_line("SEQ1M Q8T1   Read: %9.2f MB/s   Write: %9.2f MB/s", seq_r, seq_w);
    if (tests & 12)
        log_line("RND4K Q1T1   Read: %9.2f MB/s   Write: %9.2f MB/s", rnd_r_mbs, rnd_w_mbs);
    if (tests & 12)
        log_line("             (Read: %.0f IOPS, Write: %.0f IOPS)", rnd_r_iops, rnd_w_iops);
    log_line("");
    log_line(od_any ? "IO 模式: O_DIRECT（已绕过页缓存）" : "IO 模式: 普通 IO（O_DIRECT 不可用，结果可能受页缓存影响）");
    log_line(g_stop.load() ? "（已手动停止）" : "完成。");

    // 读测试文件与 8 个写测试文件均保留在沙箱中供下次复用（共约 2 GiB）

    fclose(g_log);
    g_log = nullptr;
    g_running = false;
}

int start(int tests, const char* dir)
{
    bool expected = false;
    if (!g_running.compare_exchange_strong(expected, true))
        return -1;

    g_dir = dir;
    g_stop = false;
    g_thread = std::thread(run_all, tests);
    g_thread.detach();
    return 0;
}

void stop()
{
    g_stop = true;
}

bool running()
{
    return g_running.load();
}

} // namespace diskmark

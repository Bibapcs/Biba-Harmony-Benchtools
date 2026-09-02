// pbrt 渲染 worker：独立 native 子进程，跑 pbrt_main 渲染场景
// 停止 = 主进程直接 KillChildProcess（进程隔离，强杀不影响主进程）
// 渲染过程经 PBRT_PROGRESS_PNG 环境变量周期导出 PNG 供 UI 预览
//
// 入口由 OH_Ability_StartNativeChildProcess("libpbrt_worker.so:Main", ...) 调用。
// entryParams 格式: "<filesDir>|<spp>"

#include <AbilityKit/native_child_process.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <dirent.h>
#include <unistd.h>

// pbrt 的 main 在编译期被重命名为 pbrt_main（见 pbrt-v4/ohos/CMakeLists.txt）
extern int pbrt_main(int argc, char** argv);

extern "C" void Main(NativeChildProcess_Args args)
{
    if (!args.entryParams)
        return;

    std::string params = args.entryParams;
    size_t sep = params.find('|');
    if (sep == std::string::npos)
        return;
    const std::string dir = params.substr(0, sep);
    const std::string spp = params.substr(sep + 1);

    const std::string scene = dir + "/scene/bistro_cafe.pbrt";
    const std::string outfile = dir + "/pbrt_out.png";
    const std::string progress = dir + "/pbrt_progress.png";
    const std::string logfile = dir + "/pbrt_log.txt";

    // 渲染输出重定向到日志文件
    freopen(logfile.c_str(), "w", stdout);
    dup2(fileno(stdout), fileno(stderr));

    // 诊断：检查子进程视角下的沙箱文件可见性
    fprintf(stdout, "[worker] filesDir=%s access=%d\n", dir.c_str(), access(dir.c_str(), R_OK));
    fprintf(stdout, "[worker] scene access=%d\n", access(scene.c_str(), R_OK));
    if (DIR* dp = opendir(dir.c_str()))
    {
        struct dirent* de;
        while ((de = readdir(dp)) != nullptr)
            fprintf(stdout, "[worker] files/ %s\n", de->d_name);
        closedir(dp);
    }
    if (DIR* dp = opendir((dir + "/scene").c_str()))
    {
        struct dirent* de;
        while ((de = readdir(dp)) != nullptr)
            fprintf(stdout, "[worker] scene/ %s\n", de->d_name);
        closedir(dp);
    }
    else
    {
        fprintf(stdout, "[worker] opendir(scene) failed: %s\n", strerror(errno));
    }
    fflush(stdout);

    // 周期导出渲染过程图
    setenv("PBRT_PROGRESS_PNG", progress.c_str(), 1);

    // pbrt 的参数必须以 const char* 传入会被修改的 argv？pbrt 不修改 argv，直接构造
    std::vector<std::string> tokens = {
        "pbrt", scene, "--spp", spp, "--outfile", outfile,
    };
    std::vector<char*> argv;
    for (std::string& t : tokens)
        argv.push_back((char*)t.c_str());
    argv.push_back(nullptr);

    pbrt_main((int)argv.size() - 1, argv.data());

    fflush(stdout);
    fflush(stderr);
}

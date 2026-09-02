#ifndef PBRT_RUNNER_H
#define PBRT_RUNNER_H

namespace pbrt_runner {

// 启动 pbrt 渲染子进程。spp 为每像素采样数。
// 返回 0 成功；-1 已在运行；-3 子进程启动失败
int start(int spp, const char* filesDir);

// 停止（强杀子进程，幂等）
void stop();

bool running();

// 上次子进程退出信号（0=正常退出）
int last_exit_signal();

} // namespace pbrt_runner

#endif // PBRT_RUNNER_H

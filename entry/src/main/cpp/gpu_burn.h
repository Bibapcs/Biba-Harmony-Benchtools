#ifndef GPU_BURN_H
#define GPU_BURN_H

#include <atomic>

// GPU 烤机：循环提交 FP32 FMA compute shader，直到 stop_flag 置位。
// error: 0=正常 1=Vulkan设备获取失败 2=pipeline创建失败 3=提交失败
// dispatches: 已提交的 compute 轮数
// 可在主进程或子进程中调用；每次调用独立 create/destroy gpu instance。
void gpu_burn(std::atomic<int>* stop_flag,
              std::atomic<unsigned long long>* dispatches,
              std::atomic<int>* error);

#endif // GPU_BURN_H

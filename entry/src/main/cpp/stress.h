#ifndef STRESS_H
#define STRESS_H

#include <string>

namespace stress {

// mode: 1=单烤CPU 2=单烤GPU 3=双烤
// 返回 0 成功；-1 已有烤机任务在运行；-2 GPU 不可用（仅 GPU 相关模式）
int start(int mode);

// 停止当前烤机任务（幂等，可重复调用）
void stop();

// 返回 JSON 字符串：{"mode":0,"running":false,"elapsedSec":0,"cpuThreads":0,"cpuGflops":0,"gpuDispatches":0,"gpuError":false}
std::string status();

} // namespace stress

#endif // STRESS_H

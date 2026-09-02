#ifndef DISKMARK_H
#define DISKMARK_H

#include <string>

namespace diskmark {

// tests 位掩码: 1=SEQ1M Q8T1 Read, 2=SEQ1M Q8T1 Write, 4=RND4K Q1T1 Read, 8=RND4K Q1T1 Write
// dir: 测试文件存放目录（应用沙箱 filesDir，即内置存储）
// 返回 0 成功；-1 已在运行
int start(int tests, const char* dir);

void stop();

bool running();

} // namespace diskmark

#endif // DISKMARK_H

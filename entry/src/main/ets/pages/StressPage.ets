import testNapi from 'libentry.so';
import { router } from '@kit.ArkUI';

interface StressStatus {
  mode: number;
  running: boolean;
  elapsedSec: number;
  cpuThreads: number;
  cpuGflops: number;
  gpuDispatches: number;
  gpuError: boolean;
  gpuErrorCode: number;
  workerPid: number;
}

const MODE_NAMES: string[] = ['未运行', '单烤 CPU', '单烤 GPU', '双烤（CPU+GPU）'];

@Entry
@Component
struct StressPage {
  @State statusText: string = '未在烤机';
  @State running: boolean = false;
  private timer: number = -1;

  aboutToAppear() {
    this.refresh();
  }

  aboutToDisappear() {
    this.stopTimer();
  }

  private stopTimer() {
    if (this.timer >= 0) {
      clearInterval(this.timer);
      this.timer = -1;
    }
  }

  private refresh() {
    const s: StressStatus = JSON.parse(testNapi.getStressStatus());
    this.running = s.running;
    if (!s.running && s.mode == 0 && !s.gpuError) {
      this.statusText = '未在烤机';
      return;
    }
    let text = s.running
      ? `模式：${MODE_NAMES[s.mode]}\n已运行：${s.elapsedSec.toFixed(0)} 秒`
      : '已停止（若未点击停止，则 worker 进程异常退出）';
    if (s.cpuThreads > 0) {
      text += `\nCPU：${s.cpuThreads} 线程满载，约 ${s.cpuGflops.toFixed(1)} GFLOPS`;
    }
    if (s.mode >= 2) {
      text += s.gpuError
        ? `\nGPU：出错，错误码 ${s.gpuErrorCode}（1=Vulkan设备获取失败 2=pipeline创建失败 3=提交失败）`
        : `\nGPU：已提交 ${s.gpuDispatches} 轮 compute 任务`;
    }
    this.statusText = text;
  }

  private start(mode: number) {
    const ret = testNapi.startStress(mode);
    if (ret != 0) {
      this.statusText = '启动失败：已有烤机任务在运行';
      return;
    }
    this.running = true;
    this.stopTimer();
    this.timer = setInterval(() => {
      this.refresh();
      if (!this.running) {
        this.stopTimer();
      }
    }, 500);
  }

  private stop() {
    testNapi.stopStress();
    this.refresh();
  }

  build() {
    Column() {
      Row() {
        Text('‹')
          .fontSize(28)
          .fontColor('#333333')
          .onClick(() => {
            router.back();
          })
        Text('烤机 · 散热压力测试')
          .fontSize(20)
          .fontWeight(FontWeight.Medium)
          .margin({ left: 12 })
      }
      .width('100%')
      .padding({ top: 16, bottom: 12 })

      Row({ space: 8 }) {
        Button('单烤CPU')
          .layoutWeight(1)
          .enabled(!this.running)
          .onClick(() => {
            this.start(1);
          })
        Button('单烤GPU')
          .layoutWeight(1)
          .enabled(!this.running)
          .onClick(() => {
            this.start(2);
          })
        Button('双烤')
          .layoutWeight(1)
          .enabled(!this.running)
          .onClick(() => {
            this.start(3);
          })
        Button('停止')
          .layoutWeight(1)
          .backgroundColor('#D84040')
          .enabled(this.running)
          .onClick(() => {
            this.stop();
          })
      }
      .width('100%')

      Scroll() {
        Column({ space: 12 }) {
          Text(this.statusText)
            .fontSize(14)
            .fontFamily('monospace')
            .width('100%')
            .textAlign(TextAlign.Start)

          Text('负载说明\n'
            + '· 单烤 CPU：每个 CPU 核心一个线程，循环执行 FP64 双精度浮点 FMA（乘加）运算（4 条独立依赖链），将全部核心打满。\n'
            + '· 单烤 GPU：通过 Vulkan compute shader，让约 1600 万个 GPU 线程各执行 1024×16 次 FP32 浮点 FMA 运算，连续提交，将 GPU 打满。\n'
            + '· 双烤：以上两种负载同时运行。\n'
            + '烤机在后台线程运行，界面保持响应，可随时点击"停止"。')
            .fontSize(13)
            .fontColor('#666666')
            .width('100%')
            .textAlign(TextAlign.Start)
        }
        .width('100%')
        .alignItems(HorizontalAlign.Start)
      }
      .width('100%')
      .layoutWeight(1)
      .margin({ top: 12 })
      .padding(12)
      .backgroundColor('#F5F5F5')
      .borderRadius(8)
    }
    .width('100%')
    .height('100%')
    .padding({ left: 16, right: 16, bottom: 16 })
  }
}

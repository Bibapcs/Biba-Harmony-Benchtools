import testNapi from 'libentry.so';
import { common } from '@kit.AbilityKit';
import { router } from '@kit.ArkUI';

@Entry
@Component
struct VkpeakPage {
  @State log: string = '点击"开始测试"运行 vkpeak';
  @State running: boolean = false;
  private timer: number = -1;
  private filesDir: string = '';

  aboutToAppear() {
    this.filesDir = (getContext(this) as common.UIAbilityContext).filesDir;
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

  private startRun() {
    if (this.running) {
      return;
    }
    this.running = true;
    this.log = '运行中…';
    testNapi.runVkpeak(this.filesDir, 0);
    this.timer = setInterval(() => {
      const content = testNapi.getVkpeakLog(this.filesDir);
      if (content.length > 0) {
        this.log = content;
      }
      if (!testNapi.isVkpeakRunning()) {
        this.stopTimer();
        this.log = testNapi.getVkpeakLog(this.filesDir);
        this.running = false;
      }
    }, 500);
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
        Text('vkpeak · Vulkan 算力测试')
          .fontSize(20)
          .fontWeight(FontWeight.Medium)
          .margin({ left: 12 })
      }
      .width('100%')
      .padding({ top: 16, bottom: 12 })

      Button(this.running ? '运行中…' : '开始测试')
        .width('100%')
        .enabled(!this.running)
        .onClick(() => {
          this.startRun();
        })

      Scroll() {
        Text(this.log)
          .fontSize(13)
          .fontFamily('monospace')
          .width('100%')
          .textAlign(TextAlign.Start)
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

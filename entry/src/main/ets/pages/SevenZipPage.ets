import testNapi from 'libentry.so';
import { common } from '@kit.AbilityKit';
import { router } from '@kit.ArkUI';

@Entry
@Component
struct SevenZipPage {
  @State log: string = '设置参数后点击"开始测试"运行 7-Zip benchmark';
  @State running: boolean = false;
  @State params: string = '';
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
    this.log = '运行中…（7-Zip benchmark 默认约需 30 秒以上）';
    testNapi.run7zBench(this.params.trim(), this.filesDir);
    this.timer = setInterval(() => {
      const content = testNapi.get7zLog(this.filesDir);
      if (content.length > 0) {
        this.log = content;
      }
      if (!testNapi.is7zRunning()) {
        this.stopTimer();
        this.log = testNapi.get7zLog(this.filesDir);
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
        Text('7-Zip · 压缩跑分')
          .fontSize(20)
          .fontWeight(FontWeight.Medium)
          .margin({ left: 12 })
      }
      .width('100%')
      .padding({ top: 16, bottom: 12 })

      TextInput({ placeholder: '附加参数（可空），如 -mmt=8 -md=26', text: this.params })
        .width('100%')
        .enabled(!this.running)
        .onChange((v: string) => {
          this.params = v;
        })

      Row({ space: 8 }) {
        Button('默认')
          .layoutWeight(1)
          .enabled(!this.running)
          .onClick(() => {
            this.params = '';
          })
        Button('单线程')
          .layoutWeight(1)
          .enabled(!this.running)
          .onClick(() => {
            this.params = '-mmt=1';
          })
        Button('大字典')
          .layoutWeight(1)
          .enabled(!this.running)
          .onClick(() => {
            this.params = '-md=27';
          })
        Button(this.running ? '运行中…' : '开始测试')
          .layoutWeight(2)
          .enabled(!this.running)
          .onClick(() => {
            this.startRun();
          })
      }
      .width('100%')
      .margin({ top: 8 })

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

import testNapi from 'libentry.so';
import { common } from '@kit.AbilityKit';
import { router } from '@kit.ArkUI';

@Entry
@Component
struct DiskPage {
  @State log: string = '勾选测试项后点击"开始"（测试内置存储）';
  @State running: boolean = false;
  @State seqRead: boolean = true;
  @State seqWrite: boolean = true;
  @State rndRead: boolean = true;
  @State rndWrite: boolean = true;
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
    const tests = (this.seqRead ? 1 : 0) | (this.seqWrite ? 2 : 0) | (this.rndRead ? 4 : 0) | (this.rndWrite ? 8 : 0);
    if (tests == 0 || this.running) {
      return;
    }
    this.running = true;
    this.log = '运行中…';
    testNapi.runDiskBench(tests, this.filesDir);
    this.timer = setInterval(() => {
      const content = testNapi.getDiskLog(this.filesDir);
      if (content.length > 0) {
        this.log = content;
      }
      if (!testNapi.isDiskRunning()) {
        this.stopTimer();
        this.log = testNapi.getDiskLog(this.filesDir);
        this.running = false;
      }
    }, 500);
  }

  private stopRun() {
    testNapi.stopDiskBench();
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
        Text('CrystalDiskMark · 内置存储')
          .fontSize(20)
          .fontWeight(FontWeight.Medium)
          .margin({ left: 12 })
      }
      .width('100%')
      .padding({ top: 16, bottom: 12 })

      Row({ space: 16 }) {
        Row() {
          Checkbox().select(this.seqRead).enabled(!this.running)
            .onChange((v: boolean) => { this.seqRead = v; })
          Text('顺序读 1M Q8T1').fontSize(14)
        }
        Row() {
          Checkbox().select(this.seqWrite).enabled(!this.running)
            .onChange((v: boolean) => { this.seqWrite = v; })
          Text('顺序写 1M Q8T1').fontSize(14)
        }
      }
      .width('100%')

      Row({ space: 16 }) {
        Row() {
          Checkbox().select(this.rndRead).enabled(!this.running)
            .onChange((v: boolean) => { this.rndRead = v; })
          Text('随机读 4K Q1T1').fontSize(14)
        }
        Row() {
          Checkbox().select(this.rndWrite).enabled(!this.running)
            .onChange((v: boolean) => { this.rndWrite = v; })
          Text('随机写 4K Q1T1').fontSize(14)
        }
      }
      .width('100%')
      .margin({ top: 8 })

      Row({ space: 8 }) {
        Button(this.running ? '运行中…' : '开始')
          .layoutWeight(1)
          .enabled(!this.running)
          .onClick(() => {
            this.startRun();
          })
        Button('停止')
          .layoutWeight(1)
          .backgroundColor('#D84040')
          .enabled(this.running)
          .onClick(() => {
            this.stopRun();
          })
      }
      .width('100%')
      .margin({ top: 12 })

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

import testNapi from 'libentry.so';
import { common } from '@kit.AbilityKit';
import { router } from '@kit.ArkUI';
import { fileIo as fs } from '@kit.CoreFileKit';
import { image } from '@kit.ImageKit';

const SPP_OPTIONS: number[] = [16, 32, 64, 128];

@Entry
@Component
struct PbrtPage {
  @State status: string = '选择采样数后点击"开始渲染"';
  @State running: boolean = false;
  @State spp: number = 32;
  @State pixelMap: image.PixelMap | undefined = undefined;
  @State sceneReady: boolean = false;
  private timer: number = -1;
  private filesDir: string = '';
  private startTime: number = 0;

  aboutToAppear() {
    this.filesDir = (getContext(this) as common.UIAbilityContext).filesDir;
    this.prepareScene();
  }

  aboutToDisappear() {
    this.stopTimer();
  }

  // 递归把 rawfile 里的场景经 native 写盘（ArkTS fs 写入的文件子进程不可见）
  // 返回拷贝的文件数
  private copyRawDir(rawPath: string, rel: string): number {
    const ctx = getContext(this) as common.UIAbilityContext;
    const entries: string[] = ctx.resourceManager.getRawFileListSync(rawPath);
    let count = 0;
    for (const name of entries) {
      const rawChild = rawPath + '/' + name;
      const relChild = rel ? rel + '/' + name : name;
      try {
        const data: Uint8Array = ctx.resourceManager.getRawFileContentSync(rawChild);
        const buf: ArrayBuffer = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
        const written = testNapi.writeSceneFile(this.filesDir, relChild, buf);
        if (written != data.byteLength) {
          throw new Error('write failed');
        }
        count++;
        if (count % 20 == 0) {
          this.status = `正在释放场景… ${count} 个文件`;
        }
      } catch {
        // 是目录，递归
        count += this.copyRawDir(rawChild, relChild);
      }
    }
    return count;
  }

  private prepareScene() {
    try {
      this.status = '正在释放场景…';
      const count = this.copyRawDir('scene', '');
      this.sceneReady = true;
      this.status = `场景就绪（bistro_cafe，${count} 个文件）`;
    } catch {
      this.status = '场景释放失败（rawfile 读取出错）';
    }
  }

  private stopTimer() {
    if (this.timer >= 0) {
      clearInterval(this.timer);
      this.timer = -1;
    }
  }

  private refreshPreview(path: string) {
    try {
      const f: fs.File = fs.openSync(path, fs.OpenMode.READ_ONLY);
      const stat: fs.Stat = fs.statSync(f.fd);
      if (stat.size < 100) {
        fs.closeSync(f);
        return;
      }
      const src = image.createImageSource(f.fd);
      src.createPixelMap().then((pm) => {
        this.pixelMap = pm;
        fs.closeSync(f);
      }).catch(() => {
        fs.closeSync(f);
      });
    } catch {
      // 文件还没生成，忽略
    }
  }

  private startRun() {
    if (this.running || !this.sceneReady) {
      return;
    }
    try { fs.unlink(this.filesDir + '/pbrt_progress.png'); } catch { }
    try { fs.unlink(this.filesDir + '/pbrt_out.png'); } catch { }

    const ret = testNapi.runPbrt(this.spp, this.filesDir);
    if (ret != 0) {
      this.status = '启动失败: ' + ret;
      return;
    }
    this.running = true;
    this.startTime = Date.now();
    this.stopTimer();
    this.timer = setInterval(() => {
      this.refreshPreview(this.filesDir + '/pbrt_progress.png');
      const elapsed = (Date.now() - this.startTime) / 1000;
      this.status = `渲染中… ${elapsed.toFixed(0)} 秒（spp=${this.spp}）`;
      if (!testNapi.isPbrtRunning()) {
        this.stopTimer();
        this.running = false;
        this.refreshPreview(this.filesDir + '/pbrt_out.png');
        const total = (Date.now() - this.startTime) / 1000;
        const sig = testNapi.getPbrtExitSignal();
        const log = testNapi.getPbrtLog(this.filesDir);
        const tail = log.length > 600 ? log.substring(log.length - 600) : log;
        if (sig == 0) {
          this.status = `完成：bistro_cafe 1920x1080 spp=${this.spp}，耗时 ${total.toFixed(1)} 秒`;
        } else {
          this.status = `异常退出（signal ${sig}）：\n${tail}`;
        }
      }
    }, 1000);
  }

  private stopRun() {
    testNapi.stopPbrt();
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
        Text('pbrt · CPU 渲染跑分')
          .fontSize(20)
          .fontWeight(FontWeight.Medium)
          .margin({ left: 12 })
      }
      .width('100%')
      .padding({ top: 16, bottom: 12 })

      Row({ space: 8 }) {
        ForEach(SPP_OPTIONS, (v: number) => {
          Button(`spp ${v}`)
            .layoutWeight(1)
            .backgroundColor(this.spp == v ? '#0A59F7' : '#CCCCCC')
            .enabled(!this.running)
            .onClick(() => {
              this.spp = v;
            })
        }, (v: number) => v.toString())
        Button(this.running ? '渲染中…' : '开始渲染')
          .layoutWeight(1.5)
          .enabled(!this.running && this.sceneReady)
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

      Text(this.status)
        .fontSize(14)
        .width('100%')
        .margin({ top: 8 })

      // 渲染过程预览（Cinebench 式逐块点亮）
      Stack() {
        if (this.pixelMap) {
          Image(this.pixelMap)
            .objectFit(ImageFit.Contain)
            .width('100%')
            .height('100%')
        } else {
          Text('渲染图像将实时显示在这里\n（pbrt CPU 路径追踪，逐块渲染，每 2 秒刷新）')
            .fontSize(14)
            .fontColor('#999999')
            .textAlign(TextAlign.Center)
        }
      }
      .width('100%')
      .layoutWeight(1)
      .margin({ top: 8 })
      .backgroundColor('#202020')
      .borderRadius(8)
    }
    .width('100%')
    .height('100%')
    .padding({ left: 16, right: 16, bottom: 16 })
  }
}

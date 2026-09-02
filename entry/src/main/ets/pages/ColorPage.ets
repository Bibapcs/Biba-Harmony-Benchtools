import { deviceManager } from '@kit.DriverDevelopmentKit';
import { rpc } from '@kit.IPCKit';
import { router, window } from '@kit.ArkUI';
import testNapi from 'libentry.so';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { common } from '@kit.AbilityKit';
import { fileIo, picker } from '@kit.CoreFileKit';
import { util } from '@kit.ArkTS';
import { webview } from '@kit.ArkWeb';
import { image } from '@kit.ImageKit';
import { colorSpaceManager } from '@kit.ArkGraphics2D';
import { PATCHES } from '../colorcheck/patches';
import { buildDataIn, buildDataRef, fillTemplate, DcTokens } from '../colorcheck/dcreport';

const TAG: string = 'bbpcs-color';
const I1_VID: number = 0x0765;
const I1_PID: number = 0x6009;
const CODE_PROBE: number = 1;
const CODE_INIT: number = 2;
const CODE_CALIBRATE: number = 3;
const CODE_READ_PATCH: number = 4;
const CODE_CLOSE: number = 5;

type Stage = 'query' | 'init' | 'cal' | 'ready' | 'measure' | 'report';

@Entry
@Component
struct ColorPage {
  @State stage: Stage = 'query';
  @State log: string = '把 i1Pro3 插到本机 USB 口，然后点「查找设备」';
  @State found: boolean = false;
  @State busy: boolean = false;
  @State progress: string = '';
  @State reportWhich: string = 'srgb';
  @State exportMsg: string = '';
  private savedSrgb: string = '';
  private savedP3: string = '';
  private deviceId: number = -1;
  private remote: rpc.IRemoteObject | null = null;
  private measuredSrgb: number[][] = [];
  private measuredP3: number[][] = [];
  private cancelFlag: boolean = false;
  private instInfo: string = '';
  private p3Echo: string = '';
  @State patchPixel: image.PixelMap | null = null;
  private surfInfo: string = '';
  private surfReady: boolean = false;
  private useSurf: boolean = false;
  private csSrgb: colorSpaceManager.ColorSpaceManager | null = null;
  private csP3: colorSpaceManager.ColorSpaceManager | null = null;
  private webCtl: webview.WebviewController = new webview.WebviewController();

  aboutToAppear() {
    // 有历史测量数据（双轮格式）时重算报告并直接展示，改算法不必重测
    try {
      const ctx = getContext(this) as common.UIAbilityContext;
      const dataPath = ctx.filesDir + '/colorcheck_data.json';
      if (fileIo.accessSync(dataPath)) {
        const f = fileIo.openSync(dataPath, fileIo.OpenMode.READ_ONLY);
        const stat = fileIo.statSync(f.fd);
        const buf = new ArrayBuffer(stat.size);
        fileIo.readSync(f.fd, buf);
        fileIo.closeSync(f);
        const dec = util.TextDecoder.create('utf-8');
        const obj = JSON.parse(dec.decodeToString(new Uint8Array(buf))) as Record<string, Object>;
        const ms = obj['measuredSrgb'] as number[][];
        const mp = obj['measuredP3'] as number[][];
        if (ms !== null && ms !== undefined && mp !== null && mp !== undefined &&
          ms.length === PATCHES.length && mp.length === PATCHES.length) {
          this.instInfo = obj['instInfo'] as string;
          this.p3Echo = (obj['p3Echo'] as string) ?? '';
          this.measuredSrgb = ms;
          this.measuredP3 = mp;
          this.rebuildReport(obj['time'] as string);
          this.stage = 'report';
        }
      }
    } catch (e) {
      hilog.info(0, TAG, 'no history data');
    }
  }

  private async rpcCall(code: number): Promise<string> {
    const option = new rpc.MessageOption();
    const send = new rpc.MessageSequence();
    const reply = new rpc.MessageSequence();
    await (this.remote as rpc.RemoteObject).sendMessageRequest(code, send, reply, option);
    return reply.readString();
  }

  private sleep(ms: number): Promise<void> {
    return new Promise((resolve: () => void) => {
      setTimeout(resolve, ms);
    });
  }

  // XComponent surface/EGL 初始化：native 侧要等框架 OnSurfaceCreated 回调发来
  // NativeWindow（官方 OH_NativeXComponent 回调路径），轮询 p3surfInit 直至 OK
  // （最多 ~5s）；surfInfo 记录最后一次返回值供诊断
  private async initSurf(): Promise<boolean> {
    for (let w = 0; w < 25; w++) {
      const r = testNapi.p3surfInit();
      this.surfInfo = r;
      if (r.startsWith('OK')) {
        return true;
      }
      await this.sleep(200);
    }
    return false;
  }

  private query(): void {
    try {
      const devices = deviceManager.queryDevices(deviceManager.BusType.USB);
      this.found = false;
      for (const item of devices) {
        const dev = item as deviceManager.USBDevice;
        const vid: number = dev.vendorId ?? 0;
        const pid: number = dev.productId ?? 0;
        if (vid === I1_VID && pid === I1_PID) {
          this.deviceId = dev.deviceId;
          this.found = true;
        }
      }
      if (this.found) {
        this.log = `发现 i1Pro3（deviceId=${this.deviceId}）\n点「绑定并初始化」`;
        this.stage = 'init';
      } else {
        this.log = '未发现 i1Pro3（vid=0x765 pid=0x6009）\n请确认已插入本机 USB 口';
      }
    } catch (e) {
      this.log = `queryDevices 失败: ${JSON.stringify(e)}`;
    }
  }

  private async bindAndInit(): Promise<void> {
    if (this.deviceId < 0 || this.busy) {
      return;
    }
    this.busy = true;
    try {
      this.log = '绑定驱动中…';
      if (this.remote === null) {
        const data = await deviceManager.bindDevice(this.deviceId, async (err, id) => {
          hilog.info(0, TAG, `device disconnected: ${id}`);
        });
        this.remote = data.remote;
      }
      this.log = '初始化仪器中（约几秒）…';
      const r = await this.rpcCall(CODE_INIT);
      if (r.startsWith('OK')) {
        this.instInfo = r.substring(3);
        this.log = `仪器就绪：${this.instInfo}\n\n请把 i1Pro3 放到白色校准底座上，然后点「开始校准」`;
        this.stage = 'cal';
      } else {
        this.log = `初始化失败：${r}`;
      }
    } catch (e) {
      this.log = `失败: ${JSON.stringify(e)}`;
    }
    this.busy = false;
  }

  private async calibrate(): Promise<void> {
    if (this.busy) {
      return;
    }
    this.busy = true;
    this.log = '校准中（约 20–40 秒，请勿移动仪器）…';
    try {
      const r = await this.rpcCall(CODE_CALIBRATE);
      if (r.startsWith('OK')) {
        this.log = '校准完成。\n\n请把 i1Pro3 贴合到屏幕中央，点「开始测量」。\n' +
          `共 2 轮 × ${PATCHES.length} 色块（sRGB 标记轮 + Display P3 标记轮），约 3–6 分钟，期间请勿移动仪器或遮挡屏幕。`;
        this.stage = 'ready';
      } else if (r.indexOf('Wrong Sensor Position') >= 0) {
        // 2026-08-12 远程场景兜底：仪器掉电丢校准且无法物理放回白板时，
        // 允许进入测量以验证色块渲染链路（未校准读数会报 needs_cal，
        // 数据不可用，仅用于渲染/流程验证）
        this.log = `校准未完成（${r}）。\n已进入未校准模式：可点「开始测量」验证色块渲染，读数会失败属预期。`;
        this.stage = 'ready';
      } else {
        this.log = `校准失败：${r}\n\n请确认仪器在白色校准底座上后重试。`;
      }
    } catch (e) {
      this.log = `校准异常: ${JSON.stringify(e)}`;
    }
    this.busy = false;
  }

  // 色块渲染回退路径（仅供渲染自测诊断对比用；正式测量不走这里）：
  // 8x8 纯色 PixelMap + 色彩空间标记（SRGB / DISPLAY_P3），全屏拉伸显示。
  // 注意：真机实测 setColorSpace 标签被合成器忽略，此路径等于无标签，
  // 由系统按当前模式的默认解释映射——这正是测量要避免的，故仅限自测使用。
  private setPatchPixel(rgb: number[], space: string): void {
    if (this.csSrgb === null) {
      this.csSrgb = colorSpaceManager.create(colorSpaceManager.ColorSpace.SRGB);
      this.csP3 = colorSpaceManager.create(colorSpaceManager.ColorSpace.DISPLAY_P3);
    }
    const r = Math.round(rgb[0] * 2.55);
    const g = Math.round(rgb[1] * 2.55);
    const b = Math.round(rgb[2] * 2.55);
    const sz = 8;
    const buf = new Uint8Array(sz * sz * 4);
    for (let i = 0; i < sz * sz; i++) {
      // 实测坑：RGBA_8888 标记的 buffer 被合成器按 B,G,R,A 字节序解读
      // （真机红蓝互换，中性色看不出来），按 B,G,R,A 填充补偿
      buf[i * 4] = b;
      buf[i * 4 + 1] = g;
      buf[i * 4 + 2] = r;
      buf[i * 4 + 3] = 255;
    }
    const opts: image.InitializationOptions = {
      editable: false,
      pixelFormat: image.PixelMapFormat.RGBA_8888,
      size: { width: sz, height: sz }
    };
    const pm = image.createPixelMapSync(buf.buffer, opts);
    pm.setColorSpace(space === 'p3' ? this.csP3 as colorSpaceManager.ColorSpaceManager
      : this.csSrgb as colorSpaceManager.ColorSpaceManager);
    this.patchPixel = pm;
  }

  private async measureRun(space: string, label: string, out: number[][]): Promise<string> {
    const n = PATCHES.length;
    for (let i = 0; i < n; i++) {
      if (this.cancelFlag) {
        return '用户取消';
      }
      this.progress = `${label} ${i + 1} / ${n}`;
      // 两轮都走 XComponent native surface 显式声明（sRGB 轮 SRGB_FULL，P3 轮
      // P3_FULL），不依赖系统模式对无标签内容的默认解释；measure() 已保证
      // surface 就绪才会进入这里。
      // 2026-08-12：渲染路径从 p3surfShow（EGL，本机 swap 成功但不上屏）切到
      // p3surfShowRaw（CPU 填 RAW buffer + FlushBuffer，自测已验证六帧全出且不崩）；
      // 纯色块不需要 GPU，EGL 代码保留供后续排查
      testNapi.p3surfShowRaw(PATCHES[i][0] / 100, PATCHES[i][1] / 100, PATCHES[i][2] / 100,
        space === 'p3');
      if (space === 'p3' && i === 0) {
        this.p3Echo = 'XComponent+NativeWindow RAW buffer SRGB_FULL/P3_FULL';
        hilog.info(0, TAG, `p3 run using ${this.p3Echo}`);
      }
      await this.sleep(i === 0 ? 1500 : 350);
      // 单次测量允许最多 3 次尝试：DDK 偶发传输卡死（30s 超时封顶后快速失败），
      // 同一块重测即可恢复，无需中断整轮
      let r = '';
      for (let attempt = 1; attempt <= 3; attempt++) {
        try {
          r = await this.rpcCall(CODE_READ_PATCH);
          if (r.startsWith('OK ')) {
            break;
          }
          hilog.error(0, TAG, `patch ${i + 1} attempt ${attempt} failed: ${r}`);
          await this.sleep(500);
        } catch (e) {
          r = `RPC 异常: ${JSON.stringify(e)}`;
          await this.sleep(500);
        }
      }
      if (r.startsWith('OK ')) {
        const parts = r.substring(3).trim().split(/\s+/);
        out.push([parseFloat(parts[0]), parseFloat(parts[1]), parseFloat(parts[2])]);
      } else {
        return r;
      }
    }
    return '';
  }

  // 渲染自测（无需仪器）：循环显示 红/绿/蓝 × (sRGB/P3标签) 六帧，
  // 供远程截图验证 native surface 渲染与色彩空间标签是否生效
  private selfTest: boolean = false;

  private async runSelfTest(): Promise<void> {
    if (this.busy) {
      return;
    }
    this.busy = true;
    this.selfTest = true;
    this.useSurf = true;
    this.surfReady = false;
    this.stage = 'measure';
    this.surfReady = await this.initSurf();
    if (!this.surfReady) {
      this.useSurf = false;
      hilog.error(0, TAG, 'selftest: p3surf not ready, fallback to PixelMap path');
    }
    hilog.error(0, TAG, `selftest: surfReady=${this.surfReady} useSurf=${this.useSurf}`);
    const frames: number[][] = [
      [1, 0, 0, 0], [1, 0, 0, 1],
      [0, 1, 0, 0], [0, 1, 0, 1],
      [0, 0, 1, 0], [0, 0, 1, 1]
    ];
    const names: string[] = ['红 sRGB', '红 P3', '绿 sRGB', '绿 P3', '蓝 sRGB', '蓝 P3'];
    for (let rep = 0; rep < 3 && !this.cancelFlag; rep++) {
      // 三遍诊断：第 1 遍 EGL+标签，第 2 遍 EGL 无标签，第 3 遍 RAW buffer+标签
      // （绕过 EGL，CPU 直接填 buffer）。哪遍出颜色决定问题层：
      // 全黑→surface/队列；仅 RAW 出→EGL 特有；第 2 遍出→标签被丢弃
      const useRaw = rep === 2;
      const tagOn = rep !== 1;
      if (this.useSurf) {
        testNapi.p3surfSetTag(tagOn);
      }
      const passName = useRaw ? 'RAW+标签' : (tagOn ? 'EGL+标签' : 'EGL无标签');
      for (let i = 0; i < frames.length && !this.cancelFlag; i++) {
        const f = frames[i];
        this.progress = `自测 ${names[i]}（${passName}）`;
        hilog.error(0, TAG, `selftest frame: %{public}s %{public}s`, names[i], passName);
        if (this.useSurf) {
          if (useRaw) {
            testNapi.p3surfShowRaw(f[0], f[1], f[2], f[3] === 1);
          } else {
            testNapi.p3surfShow(f[0], f[1], f[2], f[3] === 1);
          }
        } else {
          this.setPatchPixel([f[0] * 100, f[1] * 100, f[2] * 100], f[3] === 1 ? 'p3' : 'srgb');
        }
        await this.sleep(1500);
      }
    }
    if (this.useSurf) {
      testNapi.p3surfSetTag(true); // 恢复默认，避免影响后续正式测量
      testNapi.p3surfRelease();
    }
    this.selfTest = false;
    this.busy = false;
    this.cancelFlag = false;
    this.stage = 'query';
    this.log = `渲染自测完成。native surface 初始化：${this.surfInfo.length > 0 ? this.surfInfo : '未调用'}（路径：${this.useSurf ? 'XComponent/EGL' : 'PixelMap'}）。` +
      '三遍依次为 EGL+标签 / EGL无标签 / RAW buffer+标签——只有 RAW 出颜色说明是 EGL 特有问题，全黑说明在 surface/队列层，请反馈。';
  }

  private async measure(): Promise<void> {
    if (this.busy) {
      return;
    }
    this.busy = true;
    this.cancelFlag = false;
    this.measuredSrgb = [];
    this.measuredP3 = [];
    this.p3Echo = '';
    this.useSurf = true;
    this.surfReady = false;
    this.stage = 'measure';
    try {
      const win = await window.getLastWindow(getContext(this));
      await win.maximize();
    } catch (e) {
      hilog.info(0, TAG, 'maximize failed');
    }
    // XComponent 挂载后框架异步回调 native（OnSurfaceCreated 发 NativeWindow），
    // 轮询 p3surfInit 直到 EGL 就绪（最多 ~5s）。
    // 测量必须走显式色彩空间声明（SRGB_FULL/P3_FULL），surface 不就绪则中止——
    // 绝不能回落到无标签的 PixelMap 路径（setColorSpace 标签被合成器忽略，
    // 那样 sRGB 轮又会退回“系统默认解释”，P3 轮则直接是错的）
    this.surfReady = await this.initSurf();
    if (!this.surfReady) {
      testNapi.p3surfRelease();
      this.useSurf = false;
      this.busy = false;
      this.stage = 'ready';
      hilog.error(0, TAG, `p3surf not ready, measure aborted: ${this.surfInfo}`);
      this.log = `native surface 初始化失败（${this.surfInfo.length > 0 ? this.surfInfo : 'p3surfInit 未被调用'}），` +
        '测量已中止。\n显式色彩空间声明不可用时不进行测量（避免退回到系统默认解释路径）。\n' +
        '可点「开始测量」重试；反复失败请先用「渲染自测」诊断。';
      return;
    }
    let failed = '';
    failed = await this.measureRun('srgb', 'sRGB 轮', this.measuredSrgb);
    if (failed.length === 0) {
      failed = await this.measureRun('p3', 'Display P3 轮', this.measuredP3);
    }
    testNapi.p3surfRelease();
    this.surfReady = false;
    this.busy = false;
    if (failed.length > 0) {
      this.stage = 'ready';
      this.log = `测量中断（sRGB ${this.measuredSrgb.length}/${PATCHES.length}，P3 ${this.measuredP3.length}/${PATCHES.length}）：${failed}\n可点「开始测量」重新测（仪器需仍在屏幕上）。`;
      return;
    }
    try {
      const d = new Date();
      const ts = `${d.getFullYear()}-${(d.getMonth() + 1).toString().padStart(2, '0')}-${d.getDate().toString().padStart(2, '0')} ${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}`;
      const ctx = getContext(this) as common.UIAbilityContext;
      const dataPath = ctx.filesDir + '/colorcheck_data.json';
      const df = fileIo.openSync(dataPath, fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.CREATE | fileIo.OpenMode.TRUNC);
      fileIo.writeSync(df.fd, JSON.stringify({
        measuredSrgb: this.measuredSrgb,
        measuredP3: this.measuredP3,
        instInfo: this.instInfo,
        p3Echo: this.p3Echo,
        time: ts
      }));
      fileIo.closeSync(df);
      this.rebuildReport(ts);
      this.stage = 'report';
    } catch (e) {
      this.stage = 'ready';
      this.log = `报告生成失败: ${JSON.stringify(e)}`;
    }
  }

  private rebuildReport(ts: string): void {
    const ctx = getContext(this) as common.UIAbilityContext;
    const dec = util.TextDecoder.create('utf-8');
    const tplS = dec.decodeToString(ctx.resourceManager.getRawFileContentSync('dcreport_srgb.html'));
    const tplP = dec.decodeToString(ctx.resourceManager.getRawFileContentSync('dcreport_p3.html'));
    const inst = this.instInfo.length > 0 ? this.instInfo : 'i1 Pro 3';
    this.savedSrgb = this.writeDcReport(tplS, 'srgb', this.measuredSrgb, inst, ts, 'colorcheck_srgb.html');
    this.savedP3 = this.writeDcReport(tplP, 'p3', this.measuredP3, inst, ts, 'colorcheck_p3.html');
  }

  private writeDcReport(tpl: string, space: string, measured: number[][], inst: string, ts: string,
    fname: string): string {
    const tokens: DcTokens = {
      title: `鸿蒙 PC @ bbpcs — ${ts}`,
      display: '鸿蒙 PC @ bbpcs',
      instrument: inst,
      datetime: ts,
      // DisplayCAL 对齐色表：白色块下标 0，黑色块下标 1
      dataIn: buildDataIn(PATCHES, measured, 0, 1),
      dataRef: buildDataRef(PATCHES, space),
      whiteAbs: measured[0],
      // 黑场可能有仪器噪声造成的微小负值，钳到 0（否则对比度算出负数）
      blackAbs: measured[4].map((v: number) => Math.max(0, v))
    };
    const html = fillTemplate(tpl, tokens);
    const ctx = getContext(this) as common.UIAbilityContext;
    const path = ctx.filesDir + '/' + fname;
    const f = fileIo.openSync(path, fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.CREATE | fileIo.OpenMode.TRUNC);
    fileIo.writeSync(f.fd, html);
    fileIo.closeSync(f);
    return path;
  }

  private async exportFile(srcPath: string, fileName: string): Promise<string> {
    const ctx = getContext(this) as common.UIAbilityContext;
    const docPicker = new picker.DocumentViewPicker(ctx);
    const opts = new picker.DocumentSaveOptions();
    opts.newFileNames = [fileName];
    const uris = await docPicker.save(opts);
    if (uris.length === 0) {
      return '已取消';
    }
    const src = fileIo.openSync(srcPath, fileIo.OpenMode.READ_ONLY);
    const dst = fileIo.openSync(uris[0],
      fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.CREATE | fileIo.OpenMode.TRUNC);
    const buf = new ArrayBuffer(65536);
    for (;;) {
      const n = fileIo.readSync(src.fd, buf);
      if (n <= 0) {
        break;
      }
      fileIo.writeSync(dst.fd, buf.slice(0, n));
    }
    fileIo.closeSync(src);
    fileIo.closeSync(dst);
    return '已导出';
  }

  private async exportReport(): Promise<void> {
    const isSrgb = this.reportWhich === 'srgb';
    const path = isSrgb ? this.savedSrgb : this.savedP3;
    if (path.length === 0) {
      return;
    }
    try {
      this.exportMsg = await this.exportFile(path,
        isSrgb ? '鸿蒙PC_色准报告_sRGB.html' : '鸿蒙PC_色准报告_DisplayP3.html');
    } catch (e) {
      this.exportMsg = `导出失败: ${JSON.stringify(e)}`;
    }
  }

  private async exportData(): Promise<void> {
    try {
      const ctx = getContext(this) as common.UIAbilityContext;
      const dataPath = ctx.filesDir + '/colorcheck_data.json';
      this.exportMsg = await this.exportFile(dataPath, '鸿蒙PC_色准原始数据.json');
    } catch (e) {
      this.exportMsg = `导出失败: ${JSON.stringify(e)}`;
    }
  }

  build() {
    Stack() {
      if (this.stage === 'measure') {
        // 全屏色块：测量与自测都优先 XComponent native surface
        // （NativeWindow SetColorSpace 显式声明 SRGB_FULL/P3_FULL）；
        // Image 分支仅供渲染自测在 surface 初始化失败时做对比诊断，
        // 正式测量在 surface 不就绪时会直接中止（见 measure()）
        if (this.useSurf) {
          XComponent({ id: 'p3patch', type: XComponentType.SURFACE, libraryname: 'entry' })
            .width('100%')
            .height('100%')
        } else {
          Image(this.patchPixel)
            .width('100%')
            .height('100%')
            .objectFit(ImageFit.Fill)
            .renderMode(ImageRenderMode.Original)
        }
        // 进度/取消浮层（居中区域保持纯净，仪器贴那里）
        Column() {
          Blank()
          Text(this.progress)
            .fontSize(16)
            .fontColor('#888888')
            .backgroundColor('#F0F0F0')
            .padding(8)
            .borderRadius(6)
            .margin({ bottom: 40 })
          Button('取消')
            .fontSize(13)
            .padding({ left: 16, right: 16 })
            .margin({ bottom: 30 })
            .onClick(() => {
              this.cancelFlag = true;
            })
        }
        .width('100%')
        .height('100%')
      } else if (this.stage === 'report') {
        Column() {
          Row() {
            Text('‹')
              .fontSize(28)
              .fontColor('#333333')
              .onClick(() => {
                router.back();
              })
            Text('色准报告')
              .fontSize(20)
              .fontWeight(FontWeight.Medium)
              .margin({ left: 12 })
            Blank()
            Text(this.exportMsg)
              .fontSize(12)
              .fontColor('#2a2')
              .margin({ right: 8 })
            Button('sRGB',)
              .fontSize(13)
              .backgroundColor(this.reportWhich === 'srgb' ? '#0A59F7' : '#BBBBBB')
              .margin({ right: 6 })
              .onClick(() => {
                this.reportWhich = 'srgb';
                this.webCtl.loadUrl(`file://${this.savedSrgb}`);
              })
            Button('Display P3')
              .fontSize(13)
              .backgroundColor(this.reportWhich === 'p3' ? '#0A59F7' : '#BBBBBB')
              .margin({ right: 8 })
              .onClick(() => {
                this.reportWhich = 'p3';
                this.webCtl.loadUrl(`file://${this.savedP3}`);
              })
            Button('导出报告')
              .fontSize(13)
              .margin({ right: 8 })
              .onClick(() => {
                this.exportReport();
              })
            Button('导出数据')
              .fontSize(13)
              .margin({ right: 8 })
              .onClick(() => {
                this.exportData();
              })
            Button('重新测量')
              .fontSize(13)
              .onClick(() => {
                if (this.remote === null) {
                  this.stage = 'query';
                  this.log = '把 i1Pro3 插到本机 USB 口，然后点「查找设备」';
                } else {
                  this.stage = 'ready';
                  this.log = '请确认 i1Pro3 仍贴合屏幕，点「开始测量」复测。';
                }
              })
          }
          .width('100%')
          .padding({ top: 16, bottom: 8 })

          Web({ src: `file://${this.reportWhich === 'srgb' ? this.savedSrgb : this.savedP3}`,
            controller: this.webCtl })
            .layoutWeight(1)
            .width('100%')
            .fileAccess(true)
            .domStorageAccess(true)

          Text(`报告已保存：${this.reportWhich === 'srgb' ? this.savedSrgb : this.savedP3}`)
            .fontSize(11)
            .fontColor('#999999')
            .margin({ top: 6 })
        }
        .width('100%')
        .height('100%')
        .padding({ left: 16, right: 16, bottom: 8 })
      } else {
        Column() {
          Row() {
            Text('‹')
              .fontSize(28)
              .fontColor('#333333')
              .onClick(() => {
                router.back();
              })
            Text('色准测试 · i1Pro3')
              .fontSize(20)
              .fontWeight(FontWeight.Medium)
              .margin({ left: 12 })
          }
          .width('100%')
          .padding({ top: 16, bottom: 12 })

          if (this.stage === 'query') {
            Button('查找设备')
              .width('100%')
              .onClick(() => {
                this.query();
              })
            Button('渲染自测（无需仪器）')
              .width('100%')
              .margin({ top: 8 })
              .backgroundColor('#666666')
              .onClick(() => {
                this.runSelfTest();
              })
          } else if (this.stage === 'init') {
            Button(this.busy ? '处理中…' : '绑定并初始化')
              .width('100%')
              .enabled(!this.busy)
              .onClick(() => {
                this.bindAndInit();
              })
          } else if (this.stage === 'cal') {
            Button(this.busy ? '校准中…' : '开始校准')
              .width('100%')
              .enabled(!this.busy)
              .onClick(() => {
                this.calibrate();
              })
          } else if (this.stage === 'ready') {
            Button(this.busy ? '测量中…' : '开始测量')
              .width('100%')
              .enabled(!this.busy)
              .onClick(() => {
                this.measure();
              })
          }

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
    .width('100%')
    .height('100%')
  }
}

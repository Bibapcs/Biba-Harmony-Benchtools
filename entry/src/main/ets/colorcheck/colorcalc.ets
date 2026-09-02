// colorcalc.ets — 色度学计算与 HTML 报告生成（CIEDE2000，白点归一化）
// 与桌面端 colverify -k -N 流程等效：理论值/实测值各自归一到自身白点后算 ΔE00

// sRGB (D65) 线性 RGB -> XYZ 矩阵
const M_SRGB: number[][] = [
  [0.4124564, 0.3575761, 0.1804375],
  [0.2126729, 0.7151522, 0.0721750],
  [0.0193339, 0.1191920, 0.9503041]
];
// Display P3 (D65) 线性 RGB -> XYZ 矩阵
const M_P3: number[][] = [
  [0.48657095, 0.26566769, 0.19821729],
  [0.22897456, 0.69173852, 0.07928691],
  [0.00000000, 0.04511338, 1.04394437]
];
// D65 白点（Y=100）
export const D65: number[] = [95.047, 100.0, 108.883];

function srgbToLinear(c: number): number {
  return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
}

// rgbPct: 0..100 -> XYZ（Y=100 白归一）
function rgbToXyz(m: number[][], rgbPct: number[]): number[] {
  const r = srgbToLinear(rgbPct[0] / 100);
  const g = srgbToLinear(rgbPct[1] / 100);
  const b = srgbToLinear(rgbPct[2] / 100);
  return [
    100 * (m[0][0] * r + m[0][1] * g + m[0][2] * b),
    100 * (m[1][0] * r + m[1][1] * g + m[1][2] * b),
    100 * (m[2][0] * r + m[2][1] * g + m[2][2] * b)
  ];
}

export function xyzToLab(xyz: number[], wn: number[]): number[] {
  const f = (t: number): number => t > 0.008856 ? Math.pow(t, 1 / 3) : 7.787 * t + 16 / 116;
  const fx = f(xyz[0] / wn[0]);
  const fy = f(xyz[1] / wn[1]);
  const fz = f(xyz[2] / wn[2]);
  return [116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)];
}

export function de00(l1: number[], l2: number[]): number {
  const L1 = l1[0], a1 = l1[1], b1 = l1[2];
  const L2 = l2[0], a2 = l2[1], b2 = l2[2];
  const Lb = (L1 + L2) / 2;
  const C1 = Math.hypot(a1, b1), C2 = Math.hypot(a2, b2);
  const Cb = (C1 + C2) / 2;
  const Cb7 = Math.pow(Cb, 7);
  const G = 0.5 * (1 - Math.sqrt(Cb7 / (Cb7 + Math.pow(25, 7))));
  const a1p = a1 * (1 + G), a2p = a2 * (1 + G);
  const C1p = Math.hypot(a1p, b1), C2p = Math.hypot(a2p, b2);
  const Cbp = (C1p + C2p) / 2;
  const h1p = (Math.atan2(b1, a1p) * 180 / Math.PI + 360) % 360;
  const h2p = (Math.atan2(b2, a2p) * 180 / Math.PI + 360) % 360;
  const dLp = L2 - L1;
  const dCp = C2p - C1p;
  let dhp = 0;
  if (C1p * C2p !== 0) {
    dhp = h2p - h1p;
    if (dhp > 180) dhp -= 360;
    else if (dhp < -180) dhp += 360;
  }
  const dHp = 2 * Math.sqrt(C1p * C2p) * Math.sin(dhp / 2 * Math.PI / 180);
  const Lbp = (L1 + L2) / 2;
  let hbp = h1p + h2p;
  if (C1p * C2p !== 0) {
    const dh = Math.abs(h1p - h2p);
    if (dh <= 180) hbp = (h1p + h2p) / 2;
    else if (h1p + h2p < 360) hbp = (h1p + h2p + 360) / 2;
    else hbp = (h1p + h2p - 360) / 2;
  }
  const rad = Math.PI / 180;
  const T = 1 - 0.17 * Math.cos((hbp - 30) * rad) + 0.24 * Math.cos(2 * hbp * rad) +
    0.32 * Math.cos((3 * hbp + 6) * rad) - 0.20 * Math.cos((4 * hbp - 63) * rad);
  const dth = 30 * Math.exp(-Math.pow((hbp - 275) / 25, 2));
  const Cbp7 = Math.pow(Cbp, 7);
  const Rc = 2 * Math.sqrt(Cbp7 / (Cbp7 + Math.pow(25, 7)));
  const Sl = 1 + 0.015 * Math.pow(Lbp - 50, 2) / Math.sqrt(20 + Math.pow(Lbp - 50, 2));
  const Sc = 1 + 0.045 * Cbp;
  const Sh = 1 + 0.015 * Cbp * T;
  const Rt = -Math.sin(2 * dth * rad) * Rc;
  return Math.sqrt(Math.pow(dLp / Sl, 2) + Math.pow(dCp / Sc, 2) + Math.pow(dHp / Sh, 2) +
    Rt * (dCp / Sc) * (dHp / Sh));
}

function xyzToXy(xyz: number[]): number[] {
  const s = xyz[0] + xyz[1] + xyz[2];
  return [xyz[0] / s, xyz[1] / s];
}

function cctMcCamy(x: number, y: number): number {
  const n = (x - 0.3320) / (0.1858 - y);
  return 449 * n * n * n + 3525 * n * n + 6823.3 * n + 5520.33;
}

function xyToUv(x: number, y: number): number[] {
  const d = -2 * x + 12 * y + 3;
  return [4 * x / d, 9 * y / d];
}

export interface PatchResult {
  rgb: number[];      // 0..100
  expLab: number[];   // 理论 Lab（D65 白）
  meaLab: number[];   // 实测 Lab（归一到实测白）
  de: number;
}

export interface TargetReport {
  name: string;
  avg: number;
  peak: number;
  worst10avg: number;
  patches: PatchResult[];
}

export interface WhiteReport {
  xyz: number[];
  lum: number;        // cd/m^2
  cct: number;
  duvD65: number;
  deVsD65: number;
  x: number;
  y: number;
}

function analyze(m: number[][], name: string, patchesRgb: number[][], measuredXyz: number[][],
  meaWhite100: number[]): TargetReport {
  const patches: PatchResult[] = [];
  for (let i = 0; i < patchesRgb.length; i++) {
    const expLab = xyzToLab(rgbToXyz(m, patchesRgb[i]), D65);
    const meaLab = xyzToLab(measuredXyz[i], meaWhite100);
    patches.push({ rgb: patchesRgb[i], expLab: expLab, meaLab: meaLab, de: de00(expLab, meaLab) });
  }
  const des = patches.map((p: PatchResult) => p.de).sort((a: number, b: number) => b - a);
  const avg = des.reduce((s: number, v: number) => s + v, 0) / des.length;
  const w10 = Math.max(1, Math.round(des.length / 10));
  const worst10avg = des.slice(0, w10).reduce((s: number, v: number) => s + v, 0) / w10;
  return { name: name, avg: avg, peak: des[0], worst10avg: worst10avg, patches: patches };
}

export interface FullReport {
  white: WhiteReport;
  srgb: TargetReport;
  p3: TargetReport;
}

// measuredSrgb / measuredP3：[X,Y,Z]（发射绝对值，Y=cd/m²），分别来自 sRGB 标记轮和
// display-p3 标记轮测量；patchesRgb 同序 0..100；whiteIdx：白色色块下标（第 0 块为白）
export function analyzeAll(patchesRgb: number[][], measuredSrgb: number[][], measuredP3: number[][],
  whiteIdx: number): FullReport {
  const w = measuredSrgb[whiteIdx];
  const wP3 = measuredP3[whiteIdx];
  // 实测 Lab 必须以该轮「实测白的绝对 XYZ」为白点参考（比值归一），
  // 不能先把白点归一到 Y=100 再配绝对 XYZ，否则 L* 会按 (Y/100)^(1/3) 膨胀
  const meaWhiteAbs = [w[0], w[1], w[2]];
  const meaWhiteAbsP3 = [wP3[0], wP3[1], wP3[2]];
  const xy = xyzToXy(w);
  const uvM = xyToUv(xy[0], xy[1]);
  const xyD = xyzToXy(D65);
  const uvD = xyToUv(xyD[0], xyD[1]);
  const meaWhite100 = [w[0] / w[1] * 100, 100, w[2] / w[1] * 100];
  const white: WhiteReport = {
    xyz: w,
    lum: w[1],
    cct: cctMcCamy(xy[0], xy[1]),
    duvD65: Math.hypot(uvM[0] - uvD[0], uvM[1] - uvD[1]),
    deVsD65: de00([100, 0, 0], xyzToLab(meaWhite100, D65)),
    x: xy[0],
    y: xy[1]
  };
  return {
    white: white,
    srgb: analyze(M_SRGB, 'sRGB', patchesRgb, measuredSrgb, meaWhiteAbs),
    p3: analyze(M_P3, 'Display P3', patchesRgb, measuredP3, meaWhiteAbsP3)
  };
}

// ---------------- HTML 报告 ----------------

function cssRgb(rgb: number[]): string {
  return `rgb(${(rgb[0] * 2.55).toFixed(0)},${(rgb[1] * 2.55).toFixed(0)},${(rgb[2] * 2.55).toFixed(0)})`;
}

function chartSvg(rep: TargetReport, ymax: number): string {
  const n = rep.patches.length;
  const W = 980, H = 240, ml = 40, mr = 10, mt = 26, mb = 22;
  const pw = W - ml - mr, ph = H - mt - mb, bw = pw / n;
  const s: string[] = [];
  s.push(`<svg viewBox="0 0 ${W} ${H}" xmlns="http://www.w3.org/2000/svg" style="width:100%">`);
  s.push(`<text x="${ml}" y="16" font-size="13" font-weight="bold">${rep.name} — 平均 ${rep.avg.toFixed(2)} / 最大 ${rep.peak.toFixed(2)}</text>`);
  const thrs: number[][] = [[1, 0x2a2], [2, 0xaa2], [3, 0xc60], [5, 0xc22]];
  const thrCol: string[] = ['#2a2', '#aa2', '#c60', '#c22'];
  for (let t = 0; t < 4; t++) {
    const thr = t + 1 === 4 ? 5 : t + 1;
    const col = thrCol[t];
    const y = mt + ph - ph * thr / ymax;
    s.push(`<line x1="${ml}" y1="${y.toFixed(1)}" x2="${W - mr}" y2="${y.toFixed(1)}" stroke="${col}" stroke-width="0.8" stroke-dasharray="4 3"/>`);
    s.push(`<text x="6" y="${(y + 4).toFixed(1)}" font-size="10" fill="${col}">${thr}</text>`);
  }
  s.push(`<line x1="${ml}" y1="${mt + ph}" x2="${W - mr}" y2="${mt + ph}" stroke="#888" stroke-width="1"/>`);
  for (let i = 0; i < n; i++) {
    const p = rep.patches[i];
    const h = ph * Math.min(p.de, ymax) / ymax;
    const x = ml + i * bw;
    const y = mt + ph - h;
    s.push(`<rect x="${x.toFixed(2)}" y="${y.toFixed(2)}" width="${Math.max(bw - 1, 0.5).toFixed(2)}" height="${h.toFixed(2)}" fill="${cssRgb(p.rgb)}" stroke="#333" stroke-width="0.3"><title>#${i + 1} ΔE00=${p.de.toFixed(2)}</title></rect>`);
  }
  s.push(`<text x="${ml}" y="${H - 6}" font-size="10" fill="#666">patch 1</text>`);
  s.push(`<text x="${W - mr - 46}" y="${H - 6}" font-size="10" fill="#666">patch ${n}</text>`);
  s.push('</svg>');
  return s.join('\n');
}

function worstTable(rep: TargetReport): string {
  const sorted = rep.patches.slice().sort((a: PatchResult, b: PatchResult) => b.de - a.de).slice(0, 10);
  let rows = '';
  sorted.forEach((p: PatchResult, idx: number) => {
    const pid = rep.patches.indexOf(p) + 1;
    rows += `<tr><td><span class="sw" style="background:${cssRgb(p.rgb)}"></span>#${pid}</td>` +
      `<td>(${p.expLab[0].toFixed(1)}, ${p.expLab[1].toFixed(1)}, ${p.expLab[2].toFixed(1)})</td>` +
      `<td>(${p.meaLab[0].toFixed(1)}, ${p.meaLab[1].toFixed(1)}, ${p.meaLab[2].toFixed(1)})</td>` +
      `<td class="de">${p.de.toFixed(2)}</td></tr>`;
  });
  return `<h3>${rep.name} — 最差 10 个色块</h3><table><tr><th>色块</th><th>理论 Lab</th><th>实测 Lab</th><th>ΔE00</th></tr>${rows}</table>`;
}

function verdict(avg: number): string {
  if (avg < 1) return '优秀（肉眼不可辨）';
  if (avg < 2) return '良好（几乎不可辨）';
  if (avg < 3.5) return '一般（可察觉偏差）';
  if (avg < 5) return '较差（明显偏差）';
  return '差（非常明显的偏差）';
}

export function generateHtml(rep: FullReport, deviceName: string, instInfo: string, timeStr: string,
  p3Echo: string): string {
  const w = rep.white;
  const wn = [w.xyz[0] / w.xyz[1] * 100, 100, w.xyz[2] / w.xyz[1] * 100];
  const p3Note = p3Echo.length > 0 ? `（P3 标记回读：${p3Echo}）` : '';
  return `<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8">
<title>${deviceName} 色准报告 — 笔吧测试软件</title>
<style>
body { font-family: "Segoe UI", "Microsoft YaHei", sans-serif; max-width: 1000px; margin: 24px auto; padding: 0 16px; color: #222; line-height: 1.55; }
h1 { font-size: 22px; border-bottom: 2px solid #333; padding-bottom: 8px; }
h2 { font-size: 17px; margin-top: 32px; border-left: 4px solid #2673c9; padding-left: 8px; }
h3 { font-size: 14px; margin: 16px 0 6px; }
table { border-collapse: collapse; margin: 8px 0; font-size: 13px; }
th, td { border: 1px solid #ccc; padding: 4px 10px; text-align: left; }
th { background: #f0f4f8; }
.cards { display: flex; gap: 12px; flex-wrap: wrap; margin: 12px 0; }
.card { border: 1px solid #ddd; border-radius: 8px; padding: 10px 18px; background: #fafbfc; min-width: 150px; }
.card .v { font-size: 22px; font-weight: 600; }
.card .k { font-size: 12px; color: #666; }
.sw { display: inline-block; width: 14px; height: 14px; border: 1px solid #555; vertical-align: -2px; margin-right: 6px; }
.de { font-weight: 600; }
.meta { color: #666; font-size: 13px; }
.note { background: #f6f8fa; border: 1px solid #e1e4e8; border-radius: 6px; padding: 10px 14px; font-size: 13px; }
.cols { display: flex; gap: 24px; flex-wrap: wrap; }
.cols > div { flex: 1; min-width: 420px; }
</style></head><body>

<h1>${deviceName} 屏幕色准验证报告</h1>
<p class="meta">测量时间：${timeStr} ｜ 仪器：${instInfo}｜ 工具：笔吧测试软件（ArgyllCMS 3.5.0 仪器栈，鸿蒙端侧运行）<br>
被测屏幕：${deviceName} 本机屏幕 ｜ 测试色块：${rep.srgb.patches.length} 个 × 2 轮（sRGB 标记轮 + Display P3 标记轮）｜ 色差公式：CIEDE2000（ΔE00）${p3Note}</p>

<h2>一、白点准确性（屏幕原生白点 vs D65 标准白）</h2>
<div class="cards">
  <div class="card"><div class="v">${w.lum.toFixed(1)}</div><div class="k">亮度 cd/m²</div></div>
  <div class="card"><div class="v">${w.cct.toFixed(0)} K</div><div class="k">相关色温 CCT（目标 6500K）</div></div>
  <div class="card"><div class="v">${w.deVsD65.toFixed(2)}</div><div class="k">白点 ΔE00 vs D65</div></div>
  <div class="card"><div class="v">${w.duvD65.toFixed(4)}</div><div class="k">与 D65 的 u′v′ 距离</div></div>
</div>
<table>
<tr><th>项目</th><th>实测白点</th><th>D65 标准</th></tr>
<tr><td>XYZ（Y=100 归一）</td><td>${wn[0].toFixed(2)}, 100.00, ${wn[2].toFixed(2)}</td><td>95.05, 100.00, 108.88</td></tr>
<tr><td>色度坐标 x, y</td><td>${w.x.toFixed(4)}, ${w.y.toFixed(4)}</td><td>0.3127, 0.3290</td></tr>
<tr><td>色温倾向</td><td colspan="2">${w.cct > 6500 ? '偏冷（偏蓝）' : '偏暖（偏黄）'}约 ${Math.abs(w.cct - 6500).toFixed(0)} K</td></tr>
</table>

<h2>二、色准（已归一化白点，仅评价色彩还原）</h2>
<p class="meta">白点偏差已在归一化中剔除，以下为纯色彩准确度：</p>
<table>
<tr><th>目标色彩空间</th><th>平均 ΔE00</th><th>最大 ΔE00</th><th>最差 10% 平均</th><th>评级</th></tr>
<tr><td>sRGB</td><td class="de">${rep.srgb.avg.toFixed(2)}</td><td>${rep.srgb.peak.toFixed(2)}</td><td>${rep.srgb.worst10avg.toFixed(2)}</td><td>${verdict(rep.srgb.avg)}</td></tr>
<tr><td>Display P3</td><td class="de">${rep.p3.avg.toFixed(2)}</td><td>${rep.p3.peak.toFixed(2)}</td><td>${rep.p3.worst10avg.toFixed(2)}</td><td>${verdict(rep.p3.avg)}</td></tr>
</table>
<p class="meta">判读参考：ΔE00 &lt; 1 不可辨；1–2 几乎不可辨；2–3.5 可察觉；3.5–5 明显；&gt; 5 非常明显。</p>

<h2>三、每色块 ΔE00 分布（柱条颜色即色块颜色）</h2>
${chartSvg(rep.srgb, 10)}
<br>
${chartSvg(rep.p3, 10)}
<p class="meta">虚线为 ΔE00 = 1 / 2 / 3 / 5 参考线。</p>

<h2>四、最差色块明细</h2>
<div class="cols">
<div>${worstTable(rep.srgb)}</div>
<div>${worstTable(rep.p3)}</div>
</div>

<h2>五、测量方法与说明</h2>
<div class="note">
<ul>
<li>流程：分两轮测量——<b>sRGB 轮</b>以无标记 RGB（系统按 sRGB 管理）全屏逐块显示，<b>Display P3 轮</b>以 CSS <code>color(display-p3 …)</code> 广色域标记渲染同一组数值；i1 Pro 3 经 USB DDK（DriverExtensionAbility）逐块发射测量；每轮数据只与对应色彩空间的理论值比对（sRGB 行 = sRGB 轮，Display P3 行 = P3 轮），两行均为准确值。</li>
<li>理论值由各色彩空间的标准色度参数（D65、基色坐标、sRGB TRC）直接计算；实测值与理论值各自归一到自身白点（等效 colverify -N），即<b>剔除白点偏差</b>后的纯色彩准确度（白点准确性单独见第一节）。</li>
<li>本测量为屏幕<b>当前状态</b>（系统默认色彩管理）下的结果。若设备提供 sRGB/原色等色彩模式，切换后复测结果可能不同。</li>
</ul>
</div>

<p class="meta" style="margin-top:32px">Generated by 笔吧测试软件 · ArgyllCMS 3.5.0 (AGPL v3) instrument stack · ${timeStr}</p>
</body></html>`;
}

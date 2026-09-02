// dcreport.ets — 生成与 DisplayCAL Measurement Report 完全一致的 HTML 报告
//
// 原理：DisplayCAL 报告 = 自包含 HTML（内嵌渲染引擎 JS）+ 表单隐藏域数据。
// 模板提取自真实报告（rawfile/dcreport_srgb.html / dcreport_p3.html），
// 此处只把 __DC_*__ 令牌替换成本次测量数据，引擎负责全部渲染/判定/图表。
//
// 数据约定（与真实报告逐字段核对过）：
//   data_in : RGB(0..100) + Lab（以实测白自身为白点）+ XYZ（白 Y=100 归一）
//             + LUMINANCE_XYZ_CDM2 关键字（绝对白 XYZ，cd/m²）
//   data_ref: RGB + Lab（模拟 profile 的 PCS Lab，D65→D50 Bradford 适配）

import { xyzToLab } from './colorcalc';

const D50: number[] = [96.4212, 100.0, 82.5188];

// Bradford CAT 矩阵
const BR_M: number[][] = [
  [0.8951, 0.2664, -0.1614],
  [-0.7502, 1.7135, 0.0367],
  [0.0389, -0.0685, 1.0296]
];
const BR_MI: number[][] = [
  [0.9869929, -0.1470543, 0.1599627],
  [0.4323053, 0.5183603, 0.0492912],
  [-0.0085287, 0.0400428, 0.9684867]
];

function matMul(m: number[][], v: number[]): number[] {
  return [
    m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
    m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
    m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]
  ];
}

// XYZ 从 srcWhite 适配到 dstWhite（Bradford）
function adapt(xyz: number[], srcW: number[], dstW: number[]): number[] {
  const s = matMul(BR_M, srcW);
  const d = matMul(BR_M, dstW);
  const c = matMul(BR_M, xyz);
  return matMul(BR_MI, [c[0] * d[0] / s[0], c[1] * d[1] / s[1], c[2] * d[2] / s[2]]);
}

function srgbToLinear(c: number): number {
  return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
}

const M_SRGB: number[][] = [
  [0.4124564, 0.3575761, 0.1804375],
  [0.2126729, 0.7151522, 0.0721750],
  [0.0193339, 0.1191920, 0.9503041]
];
const M_P3: number[][] = [
  [0.48657095, 0.26566769, 0.19821729],
  [0.22897456, 0.69173852, 0.07928691],
  [0.00000000, 0.04511338, 1.04394437]
];
const D65: number[] = [95.047, 100.0, 108.883];

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

function f6(v: number): string {
  return v.toFixed(6);
}

// 参考数据（模拟 profile PCS Lab，D50）
export function buildDataRef(patchesRgb: number[][], space: string): string {
  const m = space === 'p3' ? M_P3 : M_SRGB;
  const lines: string[] = [];
  lines.push('CTI3   ', '');
  lines.push('DESCRIPTOR &quot;Argyll Calibration Target chart information 3&quot;');
  lines.push('KEYWORD &quot;DEVICE_CLASS&quot;');
  lines.push('DEVICE_CLASS &quot;DISPLAY&quot;');
  lines.push('KEYWORD &quot;COLOR_REP&quot;');
  lines.push('COLOR_REP &quot;RGB_LAB&quot;', '');
  lines.push('NUMBER_OF_FIELDS 7');
  lines.push('BEGIN_DATA_FORMAT');
  lines.push('SAMPLE_ID RGB_R RGB_G RGB_B LAB_L LAB_A LAB_B');
  lines.push('END_DATA_FORMAT', '');
  lines.push(`NUMBER_OF_SETS ${patchesRgb.length}`);
  lines.push('BEGIN_DATA');
  for (let i = 0; i < patchesRgb.length; i++) {
    const rgb = patchesRgb[i];
    const xyz65 = rgbToXyz(m, rgb);
    const xyz50 = adapt(xyz65, D65, D50);
    const lab = xyzToLab(xyz50, D50);
    lines.push(`${i + 1} ${f6(rgb[0])} ${f6(rgb[1])} ${f6(rgb[2])} ${f6(lab[0])} ${f6(lab[1])} ${f6(lab[2])}`);
  }
  lines.push('END_DATA');
  return lines.join('\n');
}

// 实测数据：measuredXyz 为绝对 XYZ（Y=cd/m²），whiteIdx 为白色块下标
export function buildDataIn(patchesRgb: number[][], measuredXyz: number[][], whiteIdx: number,
  blackIdx: number): string {
  const w = measuredXyz[whiteIdx];
  const scale = 100 / w[1];
  const wn = [w[0] * scale, 100, w[2] * scale];
  const lines: string[] = [];
  lines.push('CTI3   ', '');
  lines.push('DESCRIPTOR &quot;Argyll Calibration Target chart information 3&quot;');
  lines.push('KEYWORD &quot;DEVICE_CLASS&quot;');
  lines.push('DEVICE_CLASS &quot;DISPLAY&quot;');
  lines.push('KEYWORD &quot;COLOR_REP&quot;');
  lines.push('COLOR_REP &quot;RGB_LAB&quot;');
  lines.push(`LUMINANCE_XYZ_CDM2 &quot;${f6(w[0])} ${f6(w[1])} ${f6(w[2])}&quot;`, '');
  lines.push('NUMBER_OF_FIELDS 10');
  lines.push('BEGIN_DATA_FORMAT');
  lines.push('SAMPLE_ID RGB_R RGB_G RGB_B LAB_L LAB_A LAB_B XYZ_X XYZ_Y XYZ_Z');
  lines.push('END_DATA_FORMAT', '');
  lines.push(`NUMBER_OF_SETS ${patchesRgb.length}`);
  lines.push('BEGIN_DATA');
  for (let i = 0; i < patchesRgb.length; i++) {
    const rgb = patchesRgb[i];
    const xyz = [measuredXyz[i][0] * scale, measuredXyz[i][1] * scale, measuredXyz[i][2] * scale];
    // XYZ 列保持原始（白 Y=100 归一）；Lab 列必须经 Bradford 适配到 D50
    //（Argyll/DisplayCAL 的 CGATS 约定：Lab 恒为 D50；与 data_ref 的 PCS Lab 同空间才能直接比）
    const xyz50 = adapt(xyz, wn, D50);
    const lab = xyzToLab(xyz50, D50);
    lines.push(`${i + 1} ${f6(rgb[0])} ${f6(rgb[1])} ${f6(rgb[2])} ${f6(lab[0])} ${f6(lab[1])} ${f6(lab[2])} ${f6(xyz[0])} ${f6(xyz[1])} ${f6(xyz[2])}`);
  }
  lines.push('END_DATA');
  return lines.join('\n');
}

export interface DcTokens {
  title: string;        // 报告标题（display — datetime）
  display: string;      // FF_display
  instrument: string;   // FF_instrument
  datetime: string;     // FF_datetime
  dataIn: string;
  dataRef: string;
  whiteAbs: number[];   // 绝对白 XYZ（cd/m²）
  blackAbs: number[];   // 绝对黑 XYZ（cd/m²）
}

export function fillTemplate(tpl: string, t: DcTokens): string {
  const wn = [t.whiteAbs[0] / t.whiteAbs[1] * 100, 100, t.whiteAbs[2] / t.whiteAbs[1] * 100];
  const bn = [t.blackAbs[0] / t.whiteAbs[1] * 100, t.blackAbs[1] / t.whiteAbs[1] * 100,
    t.blackAbs[2] / t.whiteAbs[1] * 100];
  return tpl
    .replace('__DC_TITLE__', t.title)
    .replace('__DC_DISPLAY__', t.display)
    .replace('__DC_INSTRUMENT__', t.instrument)
    .replace('__DC_DATETIME__', t.datetime)
    .replace('__DC_WHITEPOINT__', `${f6(t.whiteAbs[0])} ${f6(t.whiteAbs[1])} ${f6(t.whiteAbs[2])}`)
    .replace('__DC_WHITEPOINT_N__', `${f6(wn[0])} ${f6(wn[1])} ${f6(wn[2])}`)
    .replace('__DC_BLACKPOINT__', `${f6(bn[0])} ${f6(bn[1])} ${f6(bn[2])}`)
    .replace('__DC_DATA_IN__', t.dataIn)
    .replace('__DC_DATA_REF__', t.dataRef);
}

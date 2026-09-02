# Biba-Harmony-Benchtools（笔吧测试软件 · 鸿蒙版）

面向鸿蒙 PC（2in1，HarmonyOS 6.1.1 / API 24+）的硬件评测工具集合，HarmonyOS 原生应用（包名 `com.bibapcs.bbpcs`）。

## 功能

| 工具 | 说明 |
|---|---|
| vkpeak | Vulkan 峰值算力测试（FP32/FP16/INT8 等各场景 GFLOPS/GIOPS） |
| 7-Zip Benchmark | 压缩/解压跑分（7-Zip ZS 22.00，含 zstd/brotli/lz4），报告 MIPS |
| CrystalDiskMark | 按 CDM 方法学重写的磁盘测试（SEQ1M Q8T1 / RND4K Q1T1，O_DIRECT 直读真实盘速） |
| pbrt-v4 | CPU 光线追踪渲染跑分（Cinebench 替代，渲染过程实时可视，逐块点亮） |
| 烤机 | 单烤 CPU / 单烤 GPU / 双烤（CPU 每核一线程 FP64 FMA + GPU Vulkan compute） |
| 色准测试 | 驱动 X-Rite i1Pro3（USB DDK + ArgyllCMS 端侧移植），51 色块双轮（sRGB/P3）测量，生成 DisplayCAL 风格报告 |

## 目录结构

```
├── AppScope/                  # 应用元信息（包名、图标、名称）
├── entry/                     # 主模块（ArkTS UI + native NAPI）
│   └── src/main/
│       ├── ets/               # ArkTS 页面与逻辑
│       ├── cpp/               # native：napi_init、烤机、diskmark、pbrt runner、Argyll 移植层(argyll/)
│       └── resources/rawfile/ # pbrt 场景（bistro_cafe，约 1.2GB）等内置资源
├── third_party/
│   ├── vkpeak/                # vkpeak + ncnn + glslang（Vulkan 算力）
│   ├── 7zip-source-code/      # 7-Zip ZS 22.00 v1.5.2（ohos/ 为移植层，上游零改动）
│   └── pbrt-v4/               # pbrt-v4 渲染器（ohos/ 为移植层，含离线依赖与预生成代码）
├── build-profile.json5        # 工程构建配置（compatibleSdk/targetSdk = 6.1.1(24)）
└── oh-package.json5
```

所有依赖均在仓库内，`add_subdirectory` 直接引用 `third_party/`，无需额外拷贝或链接其他目录。

## 开发环境

- **DevEco Studio**（6.1.1 SDK，API 24+；自带 node/jbr/hvigor/ohpm/NDK/hdc）
- **Python 3**（ncnn 的 glslang 编译期生成 `build_info.h` 需要，需在 PATH 上）
- 目标设备：鸿蒙 PC（arm64），系统 ≥ HarmonyOS 6.1.1（API 24）

## 构建

### DevEco Studio（推荐）

直接用 DevEco Studio 打开本目录（`Biba-Harmony-Benchtools`），等待同步完成后 Build → Build Hap(s)/APP(s) 即可。

### 命令行

```bash
# Windows (Git Bash)，DEVECO 指向 DevEco Studio 安装目录
export DEVECO="/c/Program Files/Huawei/DevEco Studio"
export NODE_HOME="$DEVECO/tools/node"
export DEVECO_SDK_HOME="$DEVECO/sdk"
export PATH="$DEVECO/jbr/bin:$NODE_HOME:$DEVECO/tools/hvigor/bin:$PATH"

hvigorw assembleHap --mode module -p product=default -p buildMode=release --no-daemon
# 产物：entry/build/default/outputs/default/entry-default-unsigned.hap
```

**必须用 release 构建**：跑分需要优化，且 debug 构建的 native 库带 `_d` 后缀会导致驱动进程加载失败。

## 签名与安装

仓库不含签名材料。调试签名需在 [AGC](https://developer.huawei.com/)（用户与访问 → 证书/设备/Profile 管理）申请：

1. 创建调试证书（.cer）与密钥库（.p12）；
2. 登记设备 UDID（`hdc shell bm get --udid` 获取）并生成调试 Profile（.p7b），包名须为 `com.bibapcs.bbpcs`；
   - 色准测试的 USB DDK 属于受限权限，Profile 需在 ACL 中勾选 `ohos.permission.ACCESS_DDK_USB`，否则安装报 `9568289 grant request permissions failed`；
3. 在 DevEco Studio（File → Project Structure → Signing Configs）配置自动签名，或用 `hap-sign-tool.jar` 手动签名：

```bash
"$DEVECO/jbr/bin/java" -jar "$DEVECO/sdk/default/openharmony/toolchains/lib/hap-sign-tool.jar" \
  sign-app -mode localSign -signAlg SHA256withECDSA -keyAlias <别名> \
  -keystoreFile <你的.p12> -keystorePwd <密码> -keyPwd <密码> \
  -appCertFile <你的.cer> -profileFile <你的.p7b> \
  -inFile entry-default-unsigned.hap -outFile entry-default-signed.hap

# 安装并启动（hdc.exe 有路径拼接坑：先 cd 到产物目录，参数用纯文件名）
hdc install entry-default-signed.hap
hdc shell aa start -a EntryAbility -b com.bibapcs.bbpcs
```

## 第三方组件

各组件原始代码见 `third_party/` 对应目录，许可证以其各自 LICENSE 文件为准：

- [vkpeak](https://github.com/nihui/vkpeak) / [ncnn](https://github.com/Tencent/ncnn) / glslang（nihui fork）
- [7-Zip ZS](https://github.com/mcmilk/7-Zip-zstd) 22.00 v1.5.2（mcmilk fork）
- [pbrt-v4](https://github.com/mmp/pbrt-v4)
- [ArgyllCMS](https://www.argyllcms.com/)（端侧移植层位于 `entry/src/main/cpp/argyll/`）
- 内置渲染场景为 pbrt-v4-scenes 的 bistro_cafe（约 1.2GB，位于 rawfile）

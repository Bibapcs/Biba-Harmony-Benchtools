import { router } from '@kit.ArkUI';

interface ToolItem {
  name: string;
  desc: string;
  page: string;
}

const TOOLS: ToolItem[] = [
  { name: 'vkpeak', desc: 'Vulkan 峰值算力测试（FP32 / FP16 / INT8 等）', page: 'pages/VkpeakPage' },
  { name: '烤机', desc: 'CPU / GPU / 双烤 满载压力测试（散热测试）', page: 'pages/StressPage' },
  { name: '7-Zip', desc: '7-Zip 压缩/解压缩 benchmark（MIPS 跑分）', page: 'pages/SevenZipPage' },
  { name: 'CrystalDiskMark', desc: '内置存储测试（SEQ 1M Q8T1 / RND 4K Q1T1）', page: 'pages/DiskPage' },
  { name: 'pbrt', desc: 'CPU 渲染跑分（bistro 路径追踪，逐块渲染实时可见）', page: 'pages/PbrtPage' },
  { name: '色准测试', desc: 'i1Pro3 校色仪测量屏幕色准（sRGB / Display P3，ΔE00）', page: 'pages/ColorPage' },
];

@Entry
@Component
struct Index {
  build() {
    Column() {
      Text('笔吧测试软件')
        .fontSize(28)
        .fontWeight(FontWeight.Bold)
        .margin({ top: 24, bottom: 16 })

      List({ space: 12 }) {
        ForEach(TOOLS, (tool: ToolItem) => {
          ListItem() {
            Row() {
              Column({ space: 4 }) {
                Text(tool.name)
                  .fontSize(20)
                  .fontWeight(FontWeight.Medium)
                Text(tool.desc)
                  .fontSize(14)
                  .fontColor('#666666')
              }
              .alignItems(HorizontalAlign.Start)
              .layoutWeight(1)

              Text('›')
                .fontSize(24)
                .fontColor('#999999')
            }
            .width('100%')
            .padding(16)
            .backgroundColor('#F5F5F5')
            .borderRadius(12)
            .onClick(() => {
              router.pushUrl({ url: tool.page });
            })
          }
        }, (tool: ToolItem) => tool.name)
      }
      .width('100%')
      .layoutWeight(1)
    }
    .width('100%')
    .height('100%')
    .padding({ left: 16, right: 16 })
  }
}

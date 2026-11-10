# GrokBot 跨电脑交接

GrokBot 在独立的 [Grok-bot-esp32 仓库](https://github.com/miland927/Grok-bot-esp32) 开发，继续复用 XiaoZhi 的语音、联网和配网。另一台电脑从该仓库的 `main` 开始。旧任务文档里的本地路径、工作树和分支名只用于查开发历史。

## 继续开发

```sh
git clone https://github.com/miland927/Grok-bot-esp32.git
cd Grok-bot-esp32
git switch main
git pull --ff-only
```

每次换电脑先拉取，完成改动和验证后提交并推回。两台电脑都有未推送改动时，先建各自的分支保存改动，再合并。交接给新的 Codex 对话时，把仓库根目录作为工作区，先读 [AGENTS.md](../../AGENTS.md) 和本文。

## 构建和测试

安装 ESP-IDF 6.1，启用它自己的 Python 和工具链环境。在仓库根目录执行：

```sh
python scripts/build.py waveshare/esp32-s3-touch-amoled-2.06 --name esp32-s3-touch-amoled-2.06
python scripts/whale/workflow.py test
```

板型是 Waveshare ESP32-S3 Touch AMOLED 2.06。标准 GrokBot 变体使用双 OTA，当前实机验收使用应用分区 `ota_0`。每次实际更新前重新确认设备身份、分区布局和运行槽，备份并校验，仅按当次授权写入。换电脑后端口可能变化。旧全量刷机脚本可用于专用初始化场景，已有数据的 GrokBot 应用更新走单槽流程。

## 可复现的素材

- 普通克隆自带原创默认火箭、方块、护符和烟花包。内置烟花的生成器在 [gen_public_firework.py](../../scripts/pet/gen_public_firework.py)，重新生成时需要 FFmpeg 和 libopus。
- 如要复现机主验收时的外观和声音，通过私人渠道复制迁移包。把其中的 `local-only/` 合并到新克隆的仓库根目录。构建会自动采用该目录下的三份贴图头、烟花备用包及 PWR 音效库。当前 TNT 和图腾的音效从卡上各自的包读取。
- 沿用原卡时可直接保留卡内 `/grokbot/`。迁移包也有电脑端暂存的同版资源，使用前按 [microSD 文档](MICROSD.md) 和 [AGENTS.md](../../AGENTS.md) 校验，卡写入需机主授权。
- Flash 备份、串口原始日志、网络配置和本机编译目录留在各自电脑。私人迁移包包含第三方素材，仅供机主在自己的电脑间搬运。

## 最近一次验收

2026-11-10，机主确认当前板上版本的设置面板、手机热点配网、烟花、TNT 和图腾暂时无问题。板上应用 SHA-256 为 `895f2a01491190127d02400b2e4485253470df19a3a7500be8e5c537847ac9af`，应用体积约 3.25 MiB，单槽剩余约 0.69 MiB。串口确认启动后从 microSD 预载 TNT 和图腾包。公开默认贴图和音效与机主的原版素材有区别，重新编译的镜像哈希也会不同。

- 设置入口是顶部双击、下拉把手。正文左右换页，Wi-Fi 卡片进入手机配网流程。
- 小球下拉放烟花、上拉触发图腾、五连点触发 TNT。TNT 和图腾在待机及配网状态可触发，语音会中断演出。
- 服务器页等待配置地址。Signals 页显示麦克风电平和 Wi-Fi RSSI。
- 设置实现看 `main/display/pet_controls.*`，手势和演出状态看 `pet_display.*` 与 `pet_show_state.h`，资源入口看 [MICROSD.md](MICROSD.md)。桌面原型在 [preview/top-controls.html](preview/top-controls.html)。

# GrokBot microSD 资源库

## 目录与打包

```text
/grokbot/
  fireworks/
    creeper.gra
    star.gra
    sphere.gra
    burst.gra
    rings.gra
    willow.gra
    heart.gra
    snowflake.gra
    selection.json  # 可选
  tnt/
    show.gtn
  totem/
    totem.gra
```

每次下拉按权重独立抽取一包烟花。TNT 五连点时可从同一张卡读取可选的 `tnt/show.gtn`，使用独立时间轴和内置回退；图腾在 idle 后台读取一次 `totem/totem.gra`，失败时用内置贴图，音轨缺失时静音。三者共用只读 SD 工作者。打包和回退细节见 [TNT 资源包](TNT.md#可选-microsd-资源包) 和 [不死图腾](TOTEM.md)。

在电脑上安装 Pillow，用同一个工具生成可独立替换的 SD 包和内置备用包。

```powershell
python scripts/pet/pack_sd_fireworks.py <预览素材目录> <电脑上的空暂存目录>
python scripts/pet/pack_sd_fireworks.py <预览素材目录> local-only --fallback-only
```

第一个命令在暂存目录生成 `grokbot/fireworks/`。第二个命令只生成 `local-only/grok_fireworks_fallback.gra`，供 GrokBot 构建嵌入。私人备用包是苦力怕烟花。公开克隆用原创备用烟花包，存在本地备用包时优先使用本地版本。替换卡上的同名烟花包无需重编固件。当前 `GRA1` 烟花时间轴固定为 160×160 像素、70 帧、每帧 50 ms。每包包含 RGB565 游程压缩帧、单声道 Ogg Opus 和 CRC32，包上限 0.5 MiB。

## 播放与回退

Waveshare ESP32-S3 Touch AMOLED 2.06 使用 SPI3 连接卡槽。MOSI、SCK、MISO、CS 对应 GPIO 1、2、3、17。FatFs 支持 FAT12、FAT16、FAT32 与 exFAT。底层拒绝写入和擦除请求。 [微雪 GPIO 定义](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.06#gpio-assignments) · [ESP-IDF FatFs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/storage/fatfs.html)

每次下拉才从后台挂载卡、读入一个包、校验并释放 SPI。没有卡、包缺失或无效、读取失败或准备超时都会播放内置苦力怕烟花。两种来源使用同一帧解码器和音频队列。SD 包准备完成后从 PSRAM 播放，当前演出不依赖卡持续在线。下次触发会重新探测。

串口的 `GrokSD` 记录挂载、读取和包校验结果，`PetFireworks` 记录 `started (microSD)` 或 `started (built-in)`。启动时无需全卡目录扫描或统计剩余空间。

### 烟花权重和点名测试

默认小水怪 10%、苦力怕 10%、LAN 10%，星星、球形、散射、垂柳和雪花各 14%。可将 [配置模板](firework-selection.example.json) 放到 `/grokbot/fireworks/selection.json`。每次下拉由只读工作者载入，文件最多 512 字节。`version` 固定为 `1`，权重是 `0–100` 的整数，省略项沿用默认值，最终须合计 100。文件缺失或无效时使用默认配置。独立抽签允许连续出现同一效果，缺包、无卡或读取失败时仍回退到内置苦力怕。

将 `test_effect` 改成 `nessie`、`lan` 或模板里的其他效果名后，下拉直接点名播放；改回 `random` 恢复随机。新版保留原资源槽位，`heart.gra` 放 Nessie，`rings.gra` 放 LAN。点名时请用对应新版卡包，串口同时记录抽中名称和资源来源。

电脑候选用 `local-only/animation-source/fireworks-v5` 作为打包输入。该目录的 `make_tracking_preview.py --effect nessie` 与 `--effect lan` 可分别预览，建议加 `--streaks 28 --out <预览路径>`。两款保留各自原槽位的音效和 1.40 s 爆炸接点。

## 扩展约定

新增演出可以沿用每包独立读取、校验和回退的方式。为 TNT 和图腾定义各自的素材和时间轴后，可增加带版本的清单，用来展示占用、加载耗时、校验错误和回退原因。Wi-Fi 管理可先做只读索引，未来上传到暂存位置并通过校验后再启用新版本。

## 不死图腾包

```powershell
python scripts/pet/make_totem_sound.py
python scripts/pet/pack_totem_pack.py
```

产物是 `local-only/sd-staging/grokbot/totem/totem.gra`（约 39 KB），同目录 `pack.json` 记录贴图/音频 SHA-256。把 `grokbot/` 复制到卡根目录即可；本轮不改卡。

`TTA1` 格式固定：16 字节头（`TTA1`、u16 宽、u16 高、u32 音频偏移、u32 payload CRC32）+ 宽×高 RGB565 + 每行 `(宽+7)/8` 字节的 1 bit mask + 单声道 Ogg Opus。宽高 8–64，包上限 96 KiB，音频 ≤48 KiB。当前包是 16×16 原版贴图 + 3.50 s、48 kHz、64 kbps 音轨；原版 `item.totem.use` 叠在 1.860 s。

启动后第二次 idle（约 5 s 后）在 `grok_sd` worker 里只读挂载、读包并校验，成功后常驻 PSRAM；最多重试 6 次。没有卡、包缺失/无效或读取失败时使用内置 16×16 贴图，演出静音播放。串口记录 `PetDisplay: Totem pack: microSD|built-in` 和 `GrokSD: Validated .../totem.gra`。


## Grok 小球脸部配置

把 [默认配置模板](face-profile.example.json) 另存为 SD 卡上的 `/grokbot/face/profile.json`。只需要保留 `"version": 1` 和想调整的字段，省略的字段沿用原来的数值。格式为 UTF-8 JSON，文件上限 0.001953 MiB。错误字段、重复键、未知版本或越界值都会让整个文件回退到默认脸。

| 字段 | 作用 | 可填范围 |
|---|---|---|
| `face_color_dark`、`face_color_light` | 深色和浅色主题下的小球颜色 | `#RRGGBB` |
| `eye_width`、`eye_height`、`eye_spacing` | 眼睛横纵半径和两眼离中心的距离，相对于脸部半径 | 0.06–0.22、0.06–0.25、0.20–0.44 |
| `blink_duration_ms` | 单次眨眼用时 | 70–450 |
| `blink_min_ms`、`blink_max_ms` | 自然眨眼的随机间隔 | 500–15000，最小值不得大于最大值 |
| `gaze_x`、`gaze_y` | 左右、上下视线移动幅度 | 0–0.40、0–0.18 |
| `angry_eye_scale` | 生气时的眼睛缩放比例 | 0.30–1.30 |
| `angry_shake_px`、`angry_shake_hz` | 生气时左右抖动的幅度和频率 | 0–6 像素、1–12 Hz，幅度默认 0 |

开机后台读取一次。联网上线后，可通过设备 MCP 工具 `self.pet.reload_face` 请求重新读取，或对 Grok 说“重新加载小球脸部配置”。工具返回只代表请求已排队。设备在手势和烟花演出结束后才应用新的配置，串口会记录 `Face profile: microSD` 或 `Face profile: defaults`。配置常驻 RAM，绘制时不访问 SD 卡或解析 JSON。热重载不会调整拖拽阈值、压缩回弹弹簧或手势优先级。

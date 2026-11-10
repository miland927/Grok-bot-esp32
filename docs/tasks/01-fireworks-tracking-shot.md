# 01 Minecraft 烟花追视镜头

## 开工入口

在 `D:/Dev/chinchin-esp32/local-only/tmp/grok` 执行。本地分支是 `codex/pwr-offline-sfx`，现有烟花实现和其他正在开发的改动在未提交工作区里。先跑 `git status --short --branch`，在这份代码上增量修改。保留已有改动，尤其是同批次的脸部配置、PWR 音效与 exFAT 支持。这个任务只做烟花，TNT 和不死图腾另开任务。电脑验收和构建完成后停下来交付，等待用户决定是否上板。

用户已认可 `local-only/animation-source/fireworks-v4/tracking-shot-v2-410x502.mp4` 的观感。以该片为目标，无需另做镜头方案。对应源码是同目录的 `make_tracking_preview.py`，以下把该目录简称为素材目录。当前版本的 Python 样片已经包含原版火箭、苦力怕爆炸和混音，固件还在使用旧取景。

## 现有调用关系与必读文件

| 文件 | 本任务相关入口 |
| --- | --- |
| [main/display/pet_display.cc](../../main/display/pet_display.cc) | `IsFireworkDrag()` 判定从脸上向下拖至少 55 px 且纵向位移至少为横向绝对位移的 1.35 倍。`ScreenEventCb()` 在有效松手时调用 `PetFireworks::Start()`。`TickThunk()` 由 33 ms LVGL 定时器驱动 `Tick()`，后者调用 `PetFireworks::Update()`。飞行和爆炸时暂停脸部绘制，回场用 `ReturnProgress()` 和 `IsCreeper()`。 |
| [main/display/pet_fireworks.cc](../../main/display/pet_fireworks.cc) 与 [头文件](../../main/display/pet_fireworks.h) | `Start()` 准备 RGB565 LVGL canvas 和源帧缓冲，异步调用 `PetMedia::LoadFirework()`。`Update()` 处理 `LeadIn → Flight → Burst → Return`，以 `now - stage_start_` 取素材帧。`DrawFrame()` 用源坐标、缩放和屏幕原点把 160×160 帧投影到面板，用上一帧和当前帧的可见区域做局部清除及失效。`Finish()` 清理并恢复脸。 |
| [main/display/pet_media.h](../../main/display/pet_media.h) 与 [实现](../../main/display/pet_media.cc) | `Clip::Validate()` / `DecodeFrame()` / `Audio()`。GRA1 固定 160×160、70 帧、50 ms/帧、3.5 s。每包最多 512 KiB，含 RGB565 RLE、单声道 Ogg Opus 和 CRC32。 |
| [main/CMakeLists.txt](../../main/CMakeLists.txt) 与 [打包器](../../scripts/pet/pack_sd_fireworks.py) | 普通 Waveshare 2.06 变体在存在 `local-only/grok_fireworks_fallback.gra` 时嵌入备用包并定义 `GROK_FIREWORKS`。`--fallback-only` 从原素材生成内置苦力怕。卡上的八种烟花走相同播放器。 |
| 素材目录中的 `make_tracking_preview.py`、`firework_rocket.png`、`sound-timeline.json` | 镜头与像素参考。PNG 为 16×16，透明边裁剪后是 8×13，预览用最近邻。`creeper.gif` 及另外七个 GIF、`sound-*.ogg` 是既有素材。`make_fireworks.py` 是原 GIF 生成器，只读查阅，直接运行会重写素材。 |

## 改动落点和步骤

1. **移植镜头计算。** 从预览的 `flight_curves()`、`flight_time()`、`lens_time()`、`camera_offset()`、`lens()`、`rocket_position()` 和 `scale_at()` 移植。把时间、位置、比例与参数集中在 `pet_fireworks.cc` 附近的小型纯计算模块，输入为绝对演出毫秒和画布尺寸。沿用预览的 1 ms 固定步积分，曲线只准备一次，或用等价的固定查表与插值。用已认可样片对齐数值，禁止在每个显示 Tick 从零重算。相机和火箭的时间分开，跳过一个 Tick 后也应直接到正确位置。
2. **在 `PetFireworks::Update()` 拆开飞行绘制与素材解码。** `age < kBurstUs` 时按现有约 33 ms 的 LVGL Tick，调用新增的飞行绘制分支。用原 PNG 裁剪区对应的 8×13 像素和透明掩码，随应用编译为小型 RGB565 数据，并记录可复现的转换方法。按样片在现有黑底 canvas 上画火箭、尾焰、12 粒历史尾迹和向下掠过的速度线。裁剪到画布，保留最近邻与像素画风。飞行阶段由运行时绘制，GRA1 内原有飞行帧继续保留以兼容旧版固件。爆炸阶段仍按 `index = age / kFrameUs` 调用 `DecodeFrame()`，首帧索引 **28**，继续走 `DrawFrame()`。`next_frame_` 仍用于素材帧限频。音效仍在进入 `Flight`、`stage_start_ = now` 时走原 `StartAudio()`，只触发一次。
3. **在 `DrawFrame()` 接好爆炸和脏区。** 以停镜时的投影计算火箭在源坐标 `(85,54)` 的屏幕终点。爆炸首帧将同一源点放在这个终点，起始缩放等于飞行末帧的镜头缩放。随后围绕该点用约 500 ms 平滑放大，保留 `kBurstZoom` 的各素材上限，同时限制倍率以避免裁切主体。优先在电脑端检查八个 GIF 的爆炸帧像素范围；可使用按素材计算好的最大外接范围。苦力怕样片约从 2.31× 到 3.39×。复用 `previous_` 机制，在飞行、爆炸切换时清掉上一帧的火箭、粒子和速度线；可复用一个计算/清除脏区的局部函数。保持同一 LVGL canvas、RGB565 缓冲、stride 与局部失效，绘制路径不做逐帧堆分配、磁盘访问或阻塞等待。
4. **用同一参数验证电脑预览。** 让现有 Python 预览支持导入固件的关键采样值或对两端做逐点比对。至少比对下表的全部时刻，记录两端的屏幕坐标、火箭高度、相机缩放和偏移。另用 33 ms 取样生成或检查一遍节奏，避免只凭 60 fps 样片推断板端观感。只需增加小型、可复现的主机验证，复用现有工具。

## 时间轴和调参基线

时间从画面与音轨开始播放计，之前的下拽回弹和 SD 等待时间另算。偏移是加在追踪轨迹上的屏幕 Y 像素，负值表示向上。

| 时间 | 画面与参数 |
| --- | --- |
| `0–370 ms` | 原版火箭近景点火，保留近景开场取景与发射音轨。素材坐标从 `(79,115)` 开始。 |
| `470 → 603 ms` | 点火后加速，镜头滞后。镜头叠加偏移从 `0` 到 `−12 px`，验收范围向上 `10–14 px`。 |
| `603 → 748 ms` | 追上后一次轻微过冲，偏移到 `+6.5 px`，验收范围 `5–8 px`。 |
| `827 / 912 ms` | 偏移分别为 `−3.4 / +3.8 px`，幅度 `3–5 px`。用不等时距关键点和 `smoothstep`，与逐帧随机数或正弦抖动相比，样片的起止速度平顺且能复现。 |
| `960 / 1020 ms` | 960 ms 偏移归零并开始刹停，1020 ms 锁住投影原点和倍率；速度线随刹停消退。 |
| `1020–1400 ms` | 相机保持固定，火箭继续上移约 `45 px`，可见高度约 `60 → 28 px`，尾焰衰减，末端到达 `(209,181)` 附近。 |
| `1400 / 1850 / 3500 ms` | 原爆炸帧和爆炸音、原闪烁音点、原素材结束。之后沿用 400 ms 回场。 |

预览的其他初始参数直接照用。速度控制点 `SPEED_MARKS = (0,.16,.36,.58,.86,1)`，速度权重 `SPEEDS = (8,190,32,37,86,10)`；归一化总高度 61 个源像素。相机弹簧 `k=126, damping=11`；停镜后火箭独立变速用系数 `.85`，远景缩小倍率 `.79`。这些数值集中命名。先对齐参考效果，偏差需要附对比数据和原因。

## 保持现有行为

- 保持触发阈值、松手条件、演出防重入、系统状态和音频空闲门槛。保留语音开始时 `Finish()` 的中断与清理，保留苦力怕回场表情。
- 保持八种随机轮换、不连续重复、SD 异步读取与超时回退、内置苦力怕、SD 路径和只读行为。保持 GRA1 ABI、帧数、帧间隔、音频映射及已有 GIF/PNG/Ogg 内容。现有卡可直接继续用。
- 本任务只调整烟花渲染。PWR、脸部配置与热重载、触摸其他手势、语音、板级引脚、分区、OTA 和构建变体沿用现有逻辑。禁止刷机、写卡、OTA 和提交无关文件。

## 电脑预览与测试命令

在 PowerShell 执行。`python` 需有 Pillow，FFmpeg/FFprobe 在 PATH。基线预览脚本会覆盖同名 v2 MP4，先留一份参考，完成后用不同文件名交付新片。

```powershell
Set-Location -LiteralPath 'D:/Dev/chinchin-esp32/local-only/tmp/grok'
git status --short --branch
$sourceDir = 'local-only/animation-source/fireworks-v4'
python "$sourceDir/make_tracking_preview.py"
$previewPath = "$sourceDir/tracking-shot-v2-410x502.mp4"
ffprobe -v error -show_entries stream=codec_type,width,height,r_frame_rate,nb_frames,start_time,duration -of json $previewPath
ffmpeg -v error -xerror -i $previewPath -f null -
python scripts/whale/workflow.py check
python scripts/whale/workflow.py test
```

主机回归使用现成的 Windows 适配器，由 `workflow.py test` 调用。另加一次飞行专项检查，覆盖重复采样、跨 Tick、停镜、爆炸首帧与脏区切换；在交付里给出具体命令。没有现成 GRA 备用包时再从原素材生成，避免误构建成烟花未启用的固件。激活本机 ESP-IDF 6.1 优先，最低 6.0.1，在已经激活 SDK 的终端执行以下命令。

```powershell
idf.py --version
if (!(Test-Path -LiteralPath 'local-only/grok_fireworks_fallback.gra')) {
    python scripts/pet/pack_sd_fireworks.py local-only/animation-source/fireworks-v4 local-only --fallback-only
}
python scripts/build.py --list-boards
python scripts/build.py waveshare/esp32-s3-touch-amoled-2.06 --name esp32-s3-touch-amoled-2.06
```

构建会更新本地 `sdkconfig` 和 `build/`。确认编译日志或 `build/compile_commands.json` 里有 `pet_fireworks.cc` 且定义了 `GROK_FIREWORKS`，确认使用普通变体并记录应用镜像、分区上限和余量。按仓库 `.clang-format` 检查实际修改的 C/C++ 文件。SDK 不可用时记录原因和命令，交付主机证据及待验证项。

## 验收与交付

- **画面。** 410×502 黑底，原火箭清晰，速度线向下掠过，尾焰和尾迹衰减。额外上冲、过冲和浮动实测在上表区间，只有一次明显的追赶过冲。33 ms 采样下无跳位、周期抖动、残影或黑帧。
- **接缝。** 停镜后原点、倍率和偏移固定，火箭连续向上并缩小。末帧火箭的轨迹锚点到首帧爆炸原点的偏差不超过 **2 px**，两端采样对照允许各 **2 px**。苦力怕轮廓完整，其他七种爆炸主体不撞屏边，爆炸相对原素材时间最多相差一个现有 LVGL Tick，音效仍使用原混音。旧 SD 包无需转换。
- **回归。** 主机回归和专项检查通过，普通 GrokBot 固件构建开启烟花。交付列出上板后仍需核验的项目，包括 SD 与无卡回退、语音打断、重复触发回场、AMOLED 刷新耗时和音画同步。本轮无需连接设备。
- **回传给审核者。** 改动文件与函数清单，供复核的差异范围；410×502 新 MP4 路径和生成命令；两端关键时间点采样表、八种爆炸的构图检查结果；主机测试统计及日志路径、固件构建结果及分区余量；实际偏离 v2 的地方和剩余验证。产物留在 `local-only/`，交付代码与任务相关文档即可。

建议技能有 `apple-design`（已有镜头缓动与回弹原则）。若出现可复现的丢帧、残影或音画不同步，再用 `diagnose` 定位。

## 实机反馈补单 · 下拉裁切与回场弹跳

只改 [PetDisplay 的 `Tick()` 和 `Render()`](../../main/display/pet_display.cc) 中下拉姿势和烟花回场。保持已经验收的火箭追视、爆炸、音效、触发阈值与 400 ms 回场时长。

1. **下拉时保留完整脸。** 当前下拉满量时 `hop_` 目标为 `+16`，`squash_` 目标为 `−0.30`，脸心约在 `y=136`，纵向半径约 `114–116 px`。脸底算到 `y=250–252`，而 240×240 脸部画布结束于 `y=239`，与照片里的平切边吻合。先复现满量下拉并保持，再在 `Render()` 计算眼睛和绘图边界前，根据**实际**脸心与纵向半径约束脸形，给抗锯齿留至少 2 px。优先保留可见下移，接近边界时柔和收敛竖向拉伸。眼睛继续基于约束后的脸形定位。测试时包含松手后弹簧的惯性，单独限制目标值可能仍会越界。
2. **烟花结束回场时向上弹一次。** `Tick()` 的 `returning && !firework_returning_` 分支当前将 `hop_` 与 `hop_v_` 都清零，`Render(entrance)` 只用三次衰减把小脸恢复到原位。在首次进入 `Return` 时复用 `Spring(&hop_, &hop_v_, ...)` 给一次向上的速度，起调 `−300 px/s`。同时让原来的缩放入场约在回场前 `150–160 ms` 完成，以免向下的入场偏移吃掉弹跳。以脸心实测为准，越过静止中心约 `10–16 px`，回落最多轻微压一下，然后停稳。每次演出只注入一次，保留苦力怕回场时的惊讶和视线。验收覆盖 33 ms 与板端约 50 ms 的取样节奏。

**验收与回传。** 实机或等效预览覆盖慢拉、快速拉到底并保持、斜拉取消、松手发射，以及插卡烟花和无卡苦力怕的回场。下拉期间脸底始终是完整弧线，取消能回原位。烟花结束能看见一次上弹和落稳，无裁切、跳变或反复弹跳，回场后可再次下拉。跑 `python scripts/whale/workflow.py test` 和原普通 GrokBot 变体构建，交付前后对照短视频、脸底最大 Y、回场上弹峰值及测试结果。按既定上板流程由 DeepSeek 执行。

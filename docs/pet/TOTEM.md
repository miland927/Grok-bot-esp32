# GrokBot 不死图腾复活演出

GrokBot 待机时被不死图腾救活。总长 3.5 s，五段式：死亡静止 → 图腾召唤 → 能量觉醒 → 复活爆发 → 果冻回弹。画面全部由 `PetTotem` 的纯逻辑驱动，复用现有 240×240 LVGL canvas、绝对时间轴、microSD 只读加载与 `AudioService::PlaySound`。

## 触发

- **独立**：从脸上向上拖 ≥55 px，`-dy >= 1.35 * |dx|`，松手。和下拉烟花的向下拖互为镜像，不影响单击/双击/长按/五连点。
- **TNT 彩蛋**：从脸画布开始、保持按住且在 20 px 内长按 ≥1.8 s 武装（`arm_candidate_` 只在 idle 的脸画布按下时成立），再做原五连点。拖动、空白区和 `PRESS_LOST` 都不武装；长按的眯眼保留。TNT 播到 3.1 s（烟散）后不再回脸，0.4 s 黑屏，自动接完整 3.5 s 图腾，总 6.6 s。武装只对下一次 TNT 生效，之后恢复普通 TNT。
- 演出期间触摸被忽略，只接受语音中断；离开 `kDeviceStateIdle` 立即结束并恢复原脸。TNT、烟花、脸部热重载和默认回退不变。

## 分镜与参数

时间从触发计，`kStageEndsUs = {400000, 1100000, 1800000, 2200000, 3500000}`。

| 阶段 | 时间 | 关键参数（`PetTotem::Sample()`） |
| --- | --- | --- |
| Death | 0.00–0.40 | 眼睛 `1 - smooth(p/0.28)` 在 0.11 s 闭死；`squash 0.12`、`y +10 px`、`scale 0.965`；`dark 0.90`；音轨 0.36–0.44 s 硬切静音 |
| Summon | 0.40–1.10 | 图腾弹簧 `0.9 → 7.4`（`ω 24, ζ 0.55`，约 15% 过冲）；`rot -0.22 → +0.02`；`y 10 → 0`；光晕 0.15→0.50；召唤闪 0.18；死脸 0.08 s 内淡出 |
| Awaken | 1.10–1.80 | 图腾 `7.4 + 0.20·smooth(p)`，±3.5 px 浮动，±0.025 rad 摆动；光晕→1.0；镜头 `1.00 → 1.13`；粒子从 0.46 s 起持续生成，128 个全部出现 |
| Burst | 1.80–2.20 | 1.80–1.86 环绕粒子向心收、图腾内收；1.86 径向白金闪（`0.98`）+ 环冲击波（128 段 × 3 层，半径 20→142）+ 128 个向外粒子；图腾 `7.55→10.45` 并 0.08 s 内消散；镜头 1.16→1.00；抖动 6 px 衰减 |
| Revival | 2.20–3.50 | 脸 `alpha 0→1`（0–0.12 s），`scale` 从 0.38 用 `Spring(ω 15.5, ζ 0.34)` 回弹（约 1.16 过冲）；`squash` 从 0.36 阻尼振荡；`dark 0.72→0`；2.77 眼睛用 `Spring(ω 38)` 突然睁开；2.88 看左、3.0 看右、3.15 回中；28 个余烬在 2.4–3.5 s 淡出 |

**镜头**：`zoom` 只作用于脸和图腾/粒子层；画布中心为 (120,120)，闪光是半径 118 的径向渐变，不是方形整屏。抖动只加在中心坐标上。

**粒子**：`pet_totem_particles.h` 由 `make_totem_particles.py` 生成，固定种子，全部是时间的函数，不做逐帧积分，跳 Tick 不会错位。
- 128 个环绕粒子：`angle/radius/omega/size/lift/spawn/color`，0.46–1.32 s 分批出生；1.1 s 后角速度 ×1.85、半径收 5%。
- 128 个爆发粒子：`angle/speed/size/life/drag/delay/color`，1.86 s 起 0.35–0.62 s 生命，向外 150–420 px/s。
- 28 个余烬：复活后 2.4–3.5 s 缓慢上升并淡出。

**脸部**：`PetDisplay::Render()` 新增 `const PetTotem::Pose*`。图腾模式强制黑底、独立的“黑→脸”LUT（`dark` 直接压亮度），并冻结呼吸/眨眼/愤怒抖动/弹簧；`face_scale`、`face_squash`、`face_y`、`eye_open`、`gaze_x/y`、`zoom`、`shake` 全部来自 pose。舞台结束后 `blink_next_` 重置，正常待机无缝接回。

## 与最新版 TNT 集成

- **视觉接缝**：TNT 到 `Recover`（约 3.10 s）时消费一次性标记，`PetTnt::Abort()` 跳过普通回脸，释放 `tnt_clip_`，`StartTotem(now, true)` 进入 aftermath。TNT 的 0–3.1 s 保持无引线、新烟尘版本，3.1–3.5 s 为图腾黑场，3.5 s 进入召唤，图腾爆发对应联动 4.96 s。
- **音频接力**：武装的 TNT 音轨以 `end_offset_ms = 3000` 截止（视频在 3.00–3.10 s 做了 100 ms 淡出），队列里不再有 3.1 s 之后的尾音；图腾音轨在 3.10 s 从死亡段开始，局部 1.86 s 爆发仍在 4.96 s。若 SD 的 TNT 包或队列仍让 `IsPlaybackIdle()` 为假，图腾后台在 0.45 s 黑场窗口内等待，并按实际启动延迟用 `start_offset_ms` 跳过音轨开头；超窗则静音回退并记录 `Totem sound skipped: playback busy`。
- **资源共存**：`pet_media`/`grok_sd` 同时保留 GRA1 烟花、GTN1 TNT、TTA1 图腾和脸部 JSON 四个只读入口，共用一个 worker。TNT 第三次松手预读、第五次锁定；图腾的 5 s 预读失败只重试，不会让 TNT/烟花等待，失败各自回落内置素材。
- **验证入口**：新版联动样片 `local-only/totem-preview/tnt-latest-totem-chain-60fps-410x502.mp4`（6.600 s、396 帧，TNT 2.35 s 爆炸、3.10 s 切黑、3.50 s 图腾、4.96 s 爆发），检查脚本 `scripts/pet/totem_chain_check.py`。原独立 3.5 s 样片仍由 `totem_finish.py` 生成。

## 图腾与音效

- **贴图**：Minecraft 26.2 原版 16×16 不死图腾，转成 RGB565 + 1 bit mask，见 `main/display/pet_totem_assets.h`。生成命令：

  ```powershell
  python scripts/pet/gen_totem_texture.py
  ```

  源文件是 `local-only/totem-reference/totem-16-original.png`（blockrender.dev）。这是个人原型素材；公开分发前请确认 Mojang/Microsoft 条款，或者用 `pack_totem_pack.py` 换掉 SD 包里的贴图。
- **音效**：3.50 s 单声道 48 kHz Ogg Opus（64 kbps，约 38.5 KB）。原版 `item.totem.use` 在 1.860 s 叠入，死亡/召唤/蓄力/果冻回弹由脚本合成。分段 RMS（dBFS）：死亡 -23.4、召唤 -20.2、觉醒 -16.4、1.80–1.86 静音、爆发 -9.0、复活 -15.3；峰值 -1.0。原版音效同样只是原型素材。
- **SD 包**：`/grokbot/totem/totem.gra`。TTA1 格式：16 B 头（`TTA1`、u16 宽高、u32 音频偏移、u32 payload CRC32）+ RGB565 + 1 bit mask + Ogg。包上限 96 KiB，音频 ≤48 KiB。生成：

  ```powershell
  python scripts/pet/make_totem_sound.py
  python scripts/pet/pack_totem_pack.py
  ```

  产物在 `local-only/sd-staging/grokbot/totem/`（另有 `pack.json`）。本轮不写卡。
- **加载**：`PetDisplay::UpdateTotemPack()` 在 idle 后台用现有 `grok_sd` 只读 worker 加载一次，最多重试 6 次（每 0.5 s）。失败或没有卡时用内置贴图静音播放。加载成功后 `PetTotem::SetTexture()` 复制纹理指针；`PetTotemAudio` 用 `shared_ptr` keepalive 保证播放期间 PSRAM 不被释放。

## 代码落点

| 文件 | 内容 |
| --- | --- |
| `main/display/pet_totem.{h,cc}` | 纯时间轴、Pose、镜头、粒子、图腾绘制、脏区 |
| `main/display/pet_totem_assets.h` | 生成的 16×16 原版贴图（RGB565 + mask） |
| `main/display/pet_totem_particles.h` | 生成的确定性粒子表 |
| `main/display/pet_totem_audio.{h,cc}` / `pet_totem_audio_logic.h` | 单轨音效、gate、忙/晚点/中断 |
| `main/display/pet_display.{h,cc}` | 上滑、长按武装、TNT 接力、图腾渲染、SD 预载 |
| `main/display/pet_tnt.h` | `Abort()`，供 TNT→图腾接力 |
| `main/audio/audio_service.{h,cc}` | `PlaySound` 的起止采样偏移，用于无尾音接力 |
| `main/display/pet_media.{h,cc}` / 板级 `grok_sd.cc` | `TotemPack`、TTA1 校验、`/grokbot/totem/totem.gra` |
| `main/CMakeLists.txt` | 图腾演出源文件 |
| `scripts/pet/*` | 贴图/粒子/音效生成、SD 打包、主机预览、视频检查 |
| `scripts/tests/test_pet_totem.py` | 时间轴/绘制、音频 gate、TTA1 回归 |

## 电脑预览与验证

```powershell
$zig = 'local-only/tmp/toolchain/zig-x86_64-windows-0.14.1/zig.exe'
& $zig c++ -std=c++20 -O2 -I main/display main/display/pet_totem.cc `
    scripts/pet/totem_preview.cc -o local-only/totem-preview/totem-preview.exe
& local-only/totem-preview/totem-preview.exe
python scripts/pet/make_totem_sound.py
python scripts/pet/pack_totem_pack.py
python scripts/pet/totem_finish.py
python scripts/pet/totem_sheet.py
```

`totem_finish.py` 生成 410×502、60 fps（210 帧）和 20 fps（70 帧）带声 MP4，另出 `aftermath` 版；同时检查 1.86 s 闪光、1.80–1.86 停住、死亡变暗、复活回脸和 aftershock 黑屏起步。视频把 240×240 画布按实机位置居中放在 410×502 里，四周保持黑边。

## 构建

```powershell
cmd /c "call D:\esp\esp-idf\export.bat && python scripts\build.py waveshare/esp32-s3-touch-amoled-2.06 --name esp32-s3-touch-amoled-2.06"
python scripts/whale/workflow.py test
$env:CXX = (Resolve-Path 'local-only/tmp/toolchain/zig-x86_64-windows-0.14.1/zig.exe').Path.Replace('\','/') + ' c++'
python -m unittest scripts.tests.test_pet_totem scripts.tests.test_tnt_audio_logic -v
python scripts/pet/totem_chain_check.py
```

`test_pet_totem` 覆盖 3.5 s 时间轴/绘制、TTA1 头和音频 gate，以及联动 0.45 s 等待窗口、起止偏移和超窗静音回退；`test_tnt_audio_logic` 覆盖 TNT 单轨 gate。`totem_chain_check.py` 检查 6.600 s、396 帧、2.40 s 爆炸、3.15/3.35 s 黑场、3.55 s 图腾、4.96 s 爆发中心亮度。

## 上板待核验

- 20 fps（50 ms LVGL port）下 1.86 s 闪光是否落在可见帧、死亡暗场是否够黑。
- 上滑 55 px 阈值误触；长按 1.8 s 武装与摸头的体感；武装后下一次 TNT 的 3.1 s 切点。
- SD 包加载、无卡回退、语音打断、连续触发、TNT 音轨在 3.00 s 截止后的接缝是否无残尾/无重播。
- 武装资格：从脸画布长按、空白区、拖动和 `PRESS_LOST` 各验一次；武装后一轮 TNT 接图腾、再下一轮恢复普通 TNT。
- AMOLED 局部刷新耗时；满画布脏区在觉醒/爆发段的实际帧率。
- 原版贴图/音效的公开分发授权。

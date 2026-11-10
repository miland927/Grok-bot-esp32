# 烟花追视镜头

从 Grok 的脸上向下拖至少 55 px 后松手触发；完整效果由 microSD 提供，固件保留内置苦力怕备用烟花。本页记录 2026-11 的 v2 追视镜头移植：飞行段改为运行时绘制，爆炸段继续播放 GRA1 素材。

## 时间轴

时间从画面与音轨开始播放计，之前的下拽回弹和 SD 等待另算。

| 时间 | 画面 |
| --- | --- |
| 0–370 ms | 原版火箭近景点火，素材坐标 `(79,115)` |
| 470–603 ms | 镜头滞后，叠加偏移 0 → −12 px |
| 603–748 ms | 轻微过冲，偏移 +6.5 px |
| 827 / 912 ms | 偏移 −3.4 / +3.8 px |
| 960–1020 ms | 刹停，偏移归零，镜头固定 |
| 1020–1400 ms | 火箭继续上移约 45 px，可见高度约 60 → 28 px |
| 1400 / 1850 / 3500 ms | 爆炸首帧与爆炸音、闪烁音点、素材结束；之后 400 ms 回场 |

`0` 点是 `PetFireworks` 进入 `Flight` 并开始播放音轨的时刻，即 `stage_start_ = now`。

## 代码落点

| 文件 | 作用 |
| --- | --- |
| `main/display/pet_fireworks_camera.{h,cc}` | 纯计算模块：1 ms 固定步积分的高度/相机曲线、镜头、火箭、缩放、偏移、停镜与爆炸锚点。无 LVGL/ESP 依赖，可在主机编译。 |
| `main/display/pet_fireworks_assets.h` | 生成文件：8×13 火箭 RGB565 + 1 bit 透明掩码、43 条速度线参数、八种爆炸的素材最大倍率。 |
| `main/display/pet_fireworks_layout.h` | 整数脏区矩形与并集/裁剪，主机测试共用。 |
| `main/display/pet_fireworks.cc` | 飞行段逐 Tick 绘制火箭、尾焰、12 粒尾迹与速度线；爆炸段按 `index = age / 50 ms` 解码素材，首帧索引 28。 |
| `scripts/pet/gen_firework_camera_assets.py` | 从素材目录重新生成上面的头文件。 |
| `scripts/pet/fireworks_flight_check.py` | 编译纯计算模块并与 Python 预览逐点比对。 |

镜头和火箭的时间分开：`flight_time()` 让火箭带着动量越过停镜点，`lens_time()` 在 60 ms 内刹停后完全固定。两者都只查预计算表，跳过一个 Tick 后直接到正确位置。

## 速度线预算

预览生成 43 条速度线。固件为每条线保留一个 LVGL 失效区，火箭簇一个；LVGL 默认 `LV_INV_BUF_SIZE = 32`，超过会退化为全屏失效。固件因此使用前 **28** 条，运动、长度和亮度公式不变。电脑预览用 `--streaks 28` 生成同参数样片；v2 参考片保留为 `tracking-shot-v2-reference-410x502.mp4`。

## 重新生成素材

在仓库根目录、确认素材目录存在后执行：

```powershell
python scripts/pet/gen_firework_camera_assets.py local-only/animation-source/fireworks-v4
```

生成器会裁剪 `firework_rocket.png` 到 8×13、按 RGBA alpha 转 RGB565、从 `make_tracking_preview.py` 读取 seed 4831 的速度线表，并用每种 GIF 在 1400 ms 后的实际像素范围计算爆炸倍率上限。头文件注释记录输入文件的 SHA256。随后按仓库 `.clang-format` 检查 C/C++ 改动；生成头文件不参与格式化。

## 电脑预览

```powershell
$src = 'local-only/animation-source/fireworks-v4'
# 60 fps 交付片
python "$src/make_tracking_preview.py" --out "$src/tracking-shot-v3-410x502.mp4" --fps 60 --streaks 28
# 33 ms 采样节奏片
python "$src/make_tracking_preview.py" --out "$src/tracking-shot-v3-33ms-410x502.mp4" --fps 1000/33 --streaks 28
ffprobe -v error -show_entries stream=codec_type,width,height,r_frame_rate,nb_frames,start_time,duration -of json "$src/tracking-shot-v3-410x502.mp4"
ffmpeg -v error -xerror -i "$src/tracking-shot-v3-410x502.mp4" -f null -
```

脚本默认仍写 `tracking-shot-v2-410x502.mp4`，不改默认参数即可复现旧片。

## 主机验证

```powershell
# 纯计算模块的黄金值与不变量（需要主机 C++ 编译器，CXX 可用 "zig.exe c++" 形式）
python -m unittest scripts.tests.test_fireworks_camera -v

# 与 Python 预览逐点比对、33 ms 采样、停镜、爆炸首帧与脏区切换
python scripts/pet/fireworks_flight_check.py

python scripts/whale/workflow.py check
python scripts/whale/workflow.py test
```

## 固件构建

```powershell
. D:\esp\esp-idf\export.ps1
python scripts/build.py waveshare/esp32-s3-touch-amoled-2.06 --name esp32-s3-touch-amoled-2.06
```

构建默认使用仓库的原创备用包。放入 `local-only/grok_fireworks_fallback.gra` 可覆盖，私人素材可用 `pack_sd_fireworks.py ... --fallback-only` 打包。成功日志应包含 `pet_fireworks.cc`、`pet_fireworks_camera.cc` 和 `GROK_FIREWORKS`。

## 上板后仍需核验

- SD 卡八种烟花与无卡内置苦力怕回退。
- 语音开始时的 `Finish()` 中断与回场表情。
- 连续触发、回场期间再次触发。
- AMOLED 局部刷新耗时与 33 ms Tick 的实际节奏（LVGL port 当前以 50 ms 周期运行）。
- 音画同步与爆炸音起点。

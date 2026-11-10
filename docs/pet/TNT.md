# GrokBot TNT 电影演出

设备待机时，从小球脸上连续点五次。相邻松手间隔小于 500 ms，总跨度不超过 1.8 s。单次最长 320 ms，移动容差 26 px。图腾联动的长按武装仍使用 20 px 容差。前四次沿用原点击反馈，下拉烟花优先判定。演出期间只接受语音中断；离开 idle 时恢复原脸。TNT 使用内嵌的 Minecraft `tnt_side.png` 16×16 RGB565 纹理，碎片和烟雾由 RGB565 实时绘制，离线和无卡可用。这版演出带可选配音；音效缺失、播放忙或语音打断时画面照常，音轨不重播。原点击、烟花、PWR 和语音音频路径不变。

## 配音（可选）

- 音轨 3.500 s、48 kHz 单声道；**原版 `Fuse.ogg`** 从 0.25 s 铺到 2.20 s，**原版 `Explosion1.ogg`** 在 **2.350 s**（首瞬态实测 2.3514 s）进入；**2.300–2.350 s 数字静音**。
- 原版素材：Minecraft Wiki / Fandom 的 `Fuse.ogg`（2.831 s）、`Explosion1–4.ogg`（2.500 / 3.250 s），原始为 44.1 kHz 单声道 Vorbis。文件、直链、大小和 SHA-256 记录在 `local-only/minecraft-sfx/SOURCES.json`；Minecraft 素材版权归 Mojang/Microsoft，仅供个人实验，不再分发或出售。
- 混音只做原素材的电平自动化、3.50 s 裁切/淡出、2.20–2.30 s 的反向引信吸气，以及在三个压缩点叠加 **−28 / −26 / −24 dBFS 的轻冲击**（合成补充层，不承担主识别）。原版引信 RMS **−20.5 dBFS**，原版爆炸 RMS **−9.2 dBFS**，峰值 **−1.0 dBFS**。
- SD 打包用音轨 `local-only/grok_tnt_blast.ogg`：Ogg Opus 64 kbps lowdelay，**26,944 B**（<48 KiB），SHA-256 `6f3ef7173a03117eea8c62ce153a1ff6078331a3762613b370aa4272328c46d7`。WAV 参考 `local-only/tnt-preview/tnt-charge-blast.wav`。
- 最新带声样片：`local-only/tnt-preview/tnt-original-smoke-{60,20}fps-410x502.mp4`。此前原版纹理样片和程序化方块样片使用同一原版音轨，可用于对照。`Explosion2–4` 的对照 WAV 为 `tnt-explosion{2,3,4}.wav`。
- 对齐检查：WAV/Ogg/MP4 音轨首瞬态 2.3514/2.3515/2.3514 s，60/20 fps 闪白帧 2.350 s；WAV 停顿 −180 dBFS，Ogg −58.5 dBFS。命令：`python scripts/pet/tnt_sound_check.py`。
- 旧的纯合成 v1 已归档为 `local-only/tnt-preview/tnt-synth-v1.*`，只用于对比；不再作为固件音轨。

### 固件接入

- TNT 音轨由 microSD 资源包提供。SD 无可用音轨时，演出静音播放。
- `ScreenEventCb()` 里 `tnt_.Tap(now)` 返回 true 时，用 `tnt_.ShowId()` 和 `now` 启动 `PetTntAudio`；`Tick()` 在演出结束或进入语音等占用状态时 `Cancel()`。
- 后台任务在播放队列空闲、未过入点且没有其他 TNT 音轨时调用 `PlaySound()`，每个包入队前检查 gate 和 `CanPlay()`。SD 版本由音频任务持有资源包，开机后预载并缓存资源，音频任务在播放期间持有引用。
- 单次启动、忙/缺素材静音、播放前取消、播放中打断、重复触发和旧轨不复播由 [pet_tnt_audio_logic.h](../../main/display/pet_tnt_audio_logic.h) 的纯逻辑与 `scripts/tests/test_tnt_audio_logic.py` 覆盖。

## 分镜

| 从第五次松手计 | 镜头与动作 |
| --- | --- |
| 0.00–0.25 s | 小球先收眼、略微侧颤，停在紧绷的表情。 |
| 0.25–0.45 s | 脸拉伸并收缩，TNT 轮廓在局部暖白闪变中接过画面。 |
| 0.45–0.95 s | 第一段试探式压缩、弹回，镜头从 1.00× 推至 1.15×。 |
| 0.95–1.50 s | 第二段压得更深、回弹更强，镜头推至 1.37×，短闪加亮。 |
| 1.50–2.20 s | 第三段先重压、再快速颤动，颜色收暗，镜头推至 1.70×。 |
| 2.20–2.35 s | 吸气。镜头固定在 1.70×，抖动归零，TNT 内收约 11.5% 后静止。 |
| 2.35–2.55 s | 暖白局部闪光、扩张冲击波、火光和烟雾。 |
| 2.55–3.10 s | 灰白烟上升、碎片坠落，余波短促回撤。 |
| 3.10–3.50 s | 小球从烟中淡入、短暂发懵，再回到实时脸。 |

在 [pet_tnt.cc](../../main/display/pet_tnt.cc) 调整 `kStageEndsUs`、`kChargeZoom` 和 `Sample()`。三段压缩 `kCompression` 为 14/24/34%，回弹 `kRebound` 为 5/10/14%，抖动上限 `kShakePx` 为 0.7/2.0/4.5 px，局部亮度混合 `kChargeFlash` 为 0.20/0.37/0.58。`kBlastFlash` 为 0.98，约前 84 ms 内衰减，影响范围限制在脸部画布内。方块顶部没有引线图元，点燃只通过声音表达。烟雾在爆炸中出现，8 团像素烟从爆心以不同方向和寿命膨胀、淡出。碎片最多 24 个，回场时烟尘移到脸两侧。

`PetDisplay` 继续绘制原脸并管理 LVGL canvas。TNT 叠加使用当前与上一帧足迹的并集失效，恢复和取消时一起清理。

## 可选 microSD 资源包

从第三次有效松手起尝试通过共用的只读 SD 工作者加载 `/grokbot/tnt/show.gtn`。第五次松手时锁定已验证的资源。读取忙、迟到、坏包或无卡时使用内置纹理，演出即时静音开始。迟到的有效包可供下一轮使用，播放中资源保持固定。烟花继续读取现有 GRA1 文件。

`GTN1` 由 4 字节签名、4 字节小端 CRC32 和负载组成。负载是 512 字节 16×16 RGB565 贴图，以及 3.5 s 单声道 Ogg Opus（最多 32 KiB），点燃与爆炸已混在同一条同步音轨。烟尘实时绘制，包内无需烟雾图或动画帧。下列命令只在电脑生成资源包。

```powershell
python scripts/pet/pack_sd_tnt.py local-only/minecraft-textures/tnt_side.png local-only/grok_tnt_blast.ogg local-only/tnt-sd-stage
```

## 电脑复现

从内层 GrokBot 仓库执行。`c++` 可替换为本机支持 C++20 的编译器或 `zig c++`。预览复用固件的镜头采样与 TNT RGB565 绘制，电脑端小球用相同姿态参数的简化渲染。

```powershell
$outDir = 'local-only/tnt-preview'
# 从 local-only/minecraft-textures/tnt_side.png 生成 16x16 RGB565 头文件。
python scripts/pet/gen_tnt_texture.py
# 当前 pet_tnt.cc 使用内嵌的原版 tnt_side 纹理；下面输出原版纹理静音帧。
c++ -std=c++20 -O2 -I main/display main/display/pet_tnt.cc scripts/pet/tnt_preview.cc -o "$outDir/cinematic-preview.exe"
& "$outDir/cinematic-preview.exe"  # 手势、取消、阶段边界和两种帧率检查
foreach ($fps in @(60, 20)) {
    & "$outDir/cinematic-preview.exe" "$outDir/frames.rgb" $fps
    ffmpeg -y -f rawvideo -pixel_format rgb24 -video_size 240x240 -framerate $fps `
        -i "$outDir/frames.rgb" -vf 'pad=410:502:(ow-iw)/2:(oh-ih)/2:black' `
        -c:v libx264 -crf 15 -pix_fmt yuv420p -movflags +faststart "$outDir/tnt-cinematic-texture-${fps}fps-410x502.mp4"
}
# 原版音效混音；从已认可的静音参考复制程序化方块对照视频流。
# 需要 numpy；可用 uv --target local-only/tmp/toolchain/audio numpy，运行前设置 PYTHONPATH。
python scripts/pet/make_tnt_sound.py
python scripts/pet/tnt_sound_check.py
# 原版纹理版带声片：复用同一原版音轨。
foreach ($fps in @(60, 20)) {
    ffmpeg -y -i "$outDir/tnt-cinematic-texture-${fps}fps-410x502.mp4" -i "$outDir/tnt-charge-blast.wav" `
        -map 0:v:0 -map 1:a:0 -c:v copy -c:a aac -b:a 128k -shortest -movflags +faststart `
        "$outDir/tnt-cinematic-texture-${fps}fps-410x502-sound.mp4"
}
python -m unittest scripts.tests.test_tnt_audio_logic -v
python scripts/whale/workflow.py test
python scripts/pet/fireworks_flight_check.py
```

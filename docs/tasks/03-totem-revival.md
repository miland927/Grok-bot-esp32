# 03 不死图腾与最新版 TNT 联动验收

用户认可图腾动画，保留独立的 3.50 秒演出和现有美术节奏。本任务把图腾接到**去掉可见引线、新烟尘、支持 GTN1 的 TNT**，交付一个可重现的集成构建。电脑验收结束后停下，待用户确认再上板。

## 工作区与已核对结果

| 位置 | 内容 |
| --- | --- |
| `D:/Dev/chinchin-esp32/local-only/tmp/grok` | 当前 GrokBot 工作区，`codex/pwr-offline-sfx`。最新 TNT 含无引线的 16×16 正面、8 团错峰烟尘、原版点燃和爆炸混音、`/grokbot/tnt/show.gtn` 只读加载。另有未提交的脸部/PWR/SD 改动。 |
| `D:/Dev/chinchin-esp32/local-only/tmp/grok-totem` | `codex/totem-revival`，基线 `094cf08`；图腾改动在暂存区，可用 `git diff --cached --stat 094cf08` 查看。该快照的 `DrawCrate()` 还会画引线，SD 只有图腾 TTA1，没有最新 TNT GTN1。 |
| 图腾工作区 `docs/pet/TOTEM.md`、`local-only/reports/totem-delivery.md` | 分镜、触发、资源、专项测试和旧基线构建证据。已核对 `build/xiaozhi.bin` 为 3,347,776 B、SHA-256 `325F3A799CE50B33E3824CBA0183BF7018EF0DB3868EDB88C8562852268FA498`，只对应图腾隔离工作区。 |

四条原 MP4 均通过 FFmpeg 完整解码。独立图腾 60/20 FPS 为 210/70 帧、3.500 s，白闪分别在 1.867/1.900 s。黑屏单段 60 FPS 为 3.500 s，首段从黑起步。旧联动片 6.600 s 可播放，但它不是当前 TNT 参考。

**新的联动参考**在图腾工作区 `local-only/totem-preview/tnt-latest-totem-chain-60fps-410x502.mp4`，410×502、396 帧，声画各 6.600 s。由当前 TNT 的 60 FPS 带声片截取前 186 帧（3.100 s），接图腾的 `-aftermath-` 成片。TNT 的尾音在 3.00–3.10 s 做了 100 ms 淡出。这是电脑合成样片，固件音轨接力需要单独验证。

## 实施顺序

1. **建立集成基线。** 先保存两个工作区的 `git status --short` 和差异范围，按现有工作区规则在隔离集成分支操作。以 `grok` 的实际文件为 TNT、脸部、烟花、PWR 与 SD 基线，从 `grok-totem` 提取 `094cf08` 之上的图腾增量。主工作区的 TNT 及脸部改动目前有未跟踪文件，迁移时逐项核对；单用 `git diff` 会漏掉这些文件。不要用图腾旧快照覆盖同名文件。
2. **合并接缝。** `main/display/pet_display.{h,cc}` 保留最新版 `tnt_request_`、`tnt_clip_`、TNT 音效 gate 和脸部逻辑，接入 `IsReviveDrag()`、`StartTotem()`、`UpdateTotemPack()` 和图腾 `Render()`。`main/display/pet_tnt.h` 保留 `PendingTaps()`、可选贴图参数，加入图腾分支的 `Abort()`。在 `Tick()` 第一次进入 TNT `Recover` 时（约 3.10 s）消费一次性联动标记，取消 TNT 音频、释放本轮 `tnt_clip_`、进入 `StartTotem(now, true)`，跳过普通 TNT 回脸。正常结束和语音取消也走同样的 TNT 清理，语音取消不得误启动图腾。
3. **把音频接力做成可验收的路径。** 隔离分支目前在 `tnt_audio_.Cancel()` 后立即 `StartTotem(now, true)`，图腾后台任务只检查一次 `IsPlaybackIdle()`。TNT 已入队尾音未排空时，整段图腾音效可能被跳过。用可重复的主机测试覆盖尾音待排空的情况。优先让 TNT 尾音在交接前释放；若利用约 400 ms 黑场等待队列，使用与唤醒帧对应的裁剪音轨或等效采样偏移，保持图腾局部 1.86 s 爆发音点。禁止将完整音轨延迟后从头播、清全局队列或抢语音。队列忙到超出同步窗口时走明确的静音回退并记日志。
4. **修正长按武装资格。** 现有 `Tick()` 的 1.8 s 判定只看 `pressed_` 和 `arm_handled_`，屏幕空白区域的 `PRESSED` 也会设 `pressed_`。武装限制为 idle 下从脸画布开始且仍保持在有效按住范围的手势；拖动、空白区和 `PRESS_LOST` 不武装。保持原长按眯眼。只消费一次武装，验证普通 TNT、武装后下一轮 TNT、再下一轮恢复普通 TNT。
5. **合并共用资源管理。** 在 `pet_media.{h,cc}` 与板级 `grok_sd.cc` 同时保留 GRA1 烟花、GTN1 TNT、TTA1 图腾、脸部配置四个读取入口。共用一个只读 worker，忙或失败时继续各自的内置回退。保留 TNT 第三次松手预读、第五次锁定资源的行为，图腾预读不得让 TNT 或烟花在 worker 忙时等待。`main/CMakeLists.txt` 同时接入 TNT 和图腾源文件及各自可选内置音轨。

## 资源与复现

- 集成构建需要当前 `grok` 的本地烟花备用包、TNT 音轨，以及 `grok-totem` 的 `local-only/grok_totem_sound.ogg`；这些资产位于忽略目录。保留 `GROK_FIREWORKS`、`GROK_TNT_SOUND`、`GROK_TOTEM_SOUND` 的有/无素材行为。上板之前分别核对本地暂存的 `grokbot/tnt/show.gtn`（27,464 B）和 `grokbot/totem/totem.gra`（39,076 B）。
- 图腾工作区的 `scripts/pet/totem_finish.py` 和 `docs/pet/TOTEM.md` 提供原四段复现入口。合并后用同一个集成工作区生成独立 60/20 FPS 和最新版 TNT 联动片，出具切换帧和声画测点。旧联动片留作历史对照。
- 启用可用的主机 C++20 编译器，避免跳过本轮的时间轴、音频 gate 和资源测试。用 ESP-IDF 6.1 构建普通 GrokBot 变体。

```powershell
python -m unittest scripts.tests.test_pet_totem scripts.tests.test_tnt_audio_logic -v
python scripts/pet/fireworks_flight_check.py
python scripts/whale/workflow.py test
idf.py --version
python scripts/build.py waveshare/esp32-s3-touch-amoled-2.06 --name esp32-s3-touch-amoled-2.06
```

## 验收与回传

- **独立图腾。** 上滑 55 px 后松手，3.50 s 五段完整；语音可取消，脸正常恢复。20 FPS 看得见白金闪和回弹。
- **联动。** 普通五连点正常 TNT 回脸。有效长按武装后的下一轮先演最新版 TNT（无引线、新烟尘，2.35 s 爆炸），约 3.10 s 切黑，约 3.50 s 显出图腾，图腾爆发约在联动 4.96 s。接缝无普通脸插帧、亮块残留或音轨重播。主机至少检查 60/20 FPS 以及 TNT 尾音占队列时的开始/回退。
- **兼容。** 本地构建验证三套资源及各自缺失、损坏或繁忙的回退。原烟花下拉、脸部配置、下拉裁切与回场弹跳保留。给出上板待测的触发、SD、语音中断、实际帧率及声画偏差，实机阶段另行授权。
- **交付。** 回传集成分支/差异、两种预览节奏、联动带声片及复现命令、测试统计和日志、卡包大小/哈希，以及同一集成镜像的大小、分区余量、SHA-256。交付阶段不刷机、不写卡、不 OTA。

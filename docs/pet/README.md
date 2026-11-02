# 桌面小东西（pet firmware）

把小智（xiaozhi-esp32）的语音链路和一套**程序化渲染的脸**拼在一起：
对话、ASR/LLM/TTS 走小智云，表情和性格全部在本地算出来，不依赖任何 GIF 素材。

板子：Waveshare ESP32-S3-Touch-AMOLED-2.06（410×502 QSPI AMOLED、FT3168 触摸、
ES8311+ES7210 音频、AXP2101 电源、QMI8658 IMU、PCF85063 RTC、32MB Flash / 8MB PSRAM）。

---

## 现在能做什么

| 刺激 | 反应 |
|---|---|
| 你说话 | 眼睛随音量放大、脸转向声源 |
| 它说话（TTS） | 眼睛跟着语音包络一张一合 |
| 摸它 | 看向手指、压扁、跳一下、眯眼 |
| 双击 | 眼睛绕圈转（晕） |
| 长按 | 眯眼“享受” |
| 拿起 / 摇晃 | 惊讶 / 眩晕 |
| 倾斜 | 视线跟重力偏 |
| 离开 2 分钟 | 打瞌睡（呼吸减半、眼睛半闭） |
| 回来碰它 | 惊醒（瞪大 + 跳） |
| 离开 6 小时以上 | “久别重逢”：开心地跳 |
| 23:00–07:00 | 犯困（眼睛 0.8×、呼吸变慢） |
| 电量 <20% | 没力气（眼睛 0.6×），充电即恢复 |
| 模型主动演 | `self.pet.set_mood` / `self.pet.look_at` / `self.pet.status` |
| 空闲时 | 随机张望 / 连眨两下 / 小跳 / 叹气（12–40 秒一次） |
| 改 OTA 地址 | `self.system.set_ota_url`（写 NVS，免重编译） |

---

## 代码结构

```
main/display/pet_display.{h,cc}   表情层：渲染、状态机、手势、MCP 接口
main/display/pet_imu.{h,c}        QMI8658 @0x6B（共用板级 I2C）
main/display/pet_rtc.{h,c}        PCF85063 @0x51，读时/写时
main/audio/input_level.{h,c}      麦克风与 TTS 的只读电平表
main/mcp_server.cc                注册 self.pet.* 工具
main/boards/.../config.json       板型变体（含 OTA URL / 版本号）
```

`pet_display.cc` 里的优先级阶梯是唯一权威：

```
1 触摸  >  2 眩晕  >  3 情绪  >  4 说话  >  5 聆听  >  6 瞌睡  >  7 待机
```

新行为只要插进这条阶梯，不用再到处找 `if` 该放哪。

---

## 编译与刷机

```powershell
# 一次编译（会生成 build/xiaozhi.bin）
call D:\esp\esp-idf\export.bat
python scripts\build.py waveshare/esp32-s3-touch-amoled-2.06 --name esp32-s3-touch-amoled-2.06
```

```powershell
# 刷机：必须用 watchdog reset，否则 USB 会掉、屏幕不亮，要拔插
pwsh -File scripts/pet/flash.ps1 -Port COM3
```

手动等价命令：

```powershell
cd build
python -m esptool --chip esp32s3 -p COM3 -b 460800 `
  --before default-reset --after watchdog-reset write-flash @flash_args
```

**不要用 `idf.py flash`**：它默认用 hard reset，那会让这块板的 USB 枚举和屏幕供电
不重新初始化，表现为“刷完黑屏、串口消失”。

---

## 常用调参

| 位置 | 参数 | 作用 |
|---|---|---|
| `pet_display.cc` `Render()` | `R = 88.0f` | 脸的大小（画布 240×240） |
| `Tick()` | `120000000` | 多久没互动开始打瞌睡（微秒） |
| `HandlePoke()` | `squash_v_ -= 3.4f` / `hop_v_ -= 320.0f` | 点击弹跳力度 |
| `ScreenEventCb` | `held >= 700000` | 长按判定（微秒） |
| `ScreenEventCb` | `< 350000` | 双击判定窗口 |
| `imu_task` 等价逻辑 | `imu_dev_ > 0.45f` / `imu_gyro_ > 180.0f` | 拿起 / 摇晃阈值 |
| `SyncColors()` | `lum < 128` | 深浅主题判定 |
| `config.json` | `CONFIG_OTA_URL` | 升级服务器地址 |

---

## 故障恢复

**设备反复重启 / 屏幕闪**：多半是某个可选功能在依赖就绪前启动。
固件自带自愈：连续 3 次没撑过 30 秒就进安全模式（跳过 NTP）并打日志
`boot #N safe=1`，稳定 30 秒自动退出。

**刷坏了**：整片恢复（备份是刷机前的完整镜像）：

```powershell
python -m esptool --chip esp32s3 -p COM3 write-flash 0x0 <备份>.bin
```

备份位置：`local-only/backups/pet-32mb-*.bin`（不提交 Git）。

**OTA 升级**：设备 POST 到 `CONFIG_OTA_URL` 拿清单：

```json
{"firmware":{"version":"1.0.1","url":"http://<host>:8000/1.0.1.bin"}}
```

服务器必须支持 POST 和 Range（Python 自带 `http.server` 不支持 POST，会返回 501）。
参考实现：`local-only/ota/server.py`。

OTA 只写备用槽（`ota_1` @0x410000），失败会自动回退，不会砖。

---

## 已知限制

- **没有电池**：`battery -1%`，低电表情代码在但没机会触发
- **点击音效未经听感确认**：只在 `kDeviceStateIdle` 播 `OGG_POPUP`
- **OTA 地址指着开发机**：电脑不开机时，启动检查会失败并退避重试（不影响使用）
- **没装 RTC 纽扣电池**：断电后时间靠 NTP 重新校准；有电池则连续
- **脸 240px**：为了帧率从 300 降下来，想要更大可以改 `R` 和画布尺寸，代价是 25ms+/帧

---

## 设计取舍（为什么不是别的样子）

- **不刷第三方系统**：屏幕/触摸/音频驱动全要重写，还会丢掉语音能力
- **不用 GIF/EAF 播素材**：程序化渲染不占资源分区，且能做实时反应
- **不混音做音效**：会污染 AEC 参考信号，一个 30ms 的提示音不值得冒这个险
- **表情优先级写死成阶梯**：比散落的 `if` 可预测，新增行为只需插一层

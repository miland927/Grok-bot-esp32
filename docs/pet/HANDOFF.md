# 交接说明（HANDOFF）

## 仓库

- 地址：`https://github.com/miland927/Grok-bot-esp32`
- 主分支：`main`
- 内容：xiaozhi-esp32 的完整固件树 + 本项目的表情层，单次导入提交（不含上游历史）
- 上游：`78/xiaozhi-esp32`（需要拉历史时 `git remote add upstream` 自行添加）

仓库里同时保留了原「鲸鱼娘」分支的代码与文档（`whale/`、`docs/whale/`、
`scripts/whale/`、`main/display/whale_display.*`）。它们**不参与**本项目的构建路径，
留作基座参考，后续可以单独剥离。

## 当前设备状态

| 项 | 值 |
|---|---|
| 板子 | Waveshare ESP32-S3-Touch-AMOLED-2.06 |
| 固件 | 本项目 |
| 启动槽 | `ota_1` @0x410000（OTA 升级后） |
| 语音 | 已绑定小智云，唤醒词「你好小智」 |
| 刷机口 | COM3（原生 USB） |

## 构建

```powershell
call D:\esp\esp-idf\export.bat
python scripts\build.py waveshare/esp32-s3-touch-amoled-2.06 --name esp32-s3-touch-amoled-2.06
```

产物：`build/xiaozhi.bin`（约 2.9MB，分区剩余 ~30%）。

## 刷机

```powershell
pwsh -File scripts/pet/flash.ps1 -Port COM3
```

**必须用 watchdog reset**（脚本已封装）。`idf.py flash` 默认 hard reset，会导致
USB 掉线、屏幕不亮，必须拔插才能恢复——见 `DECISIONS.md` ADR-006。

## 整片备份与恢复

- 备份在开发机的 `local-only/backups/`（不进 Git），文件名 `pet-32mb-*.bin`，
  大小 33,554,432 字节。
- 恢复：

```powershell
python -m esptool --chip esp32s3 -p COM3 write-flash 0x0 <备份>.bin
```

## OTA

- 设备从 NVS `wifi/ota_url` 取地址，没有则用编译时的 `CONFIG_OTA_URL`。
- 清单：

```json
{"firmware":{"version":"1.0.1","url":"http://<host>:8000/1.0.1.bin"}}
```

- 服务器必须支持 **POST**（设备用 POST 请求清单）和 Range（下载分块）。
  参考实现：开发机 `local-only/ota/server.py`。
- 设备只写备用槽，校验失败自动回退，不会砖。
- **检查失败只写日志、不弹错误**：服务器不可达时屏幕上不会出现「错误」横幅，
  只在串口留下 `OTA check failed: ...` 并按 10s→20s→40s… 退避重试。
- 运行时改地址：对设备说「把升级地址改成 …」，模型会调用
  `self.system.set_ota_url`。

## 已知限制

| 限制 | 说明 |
|---|---|
| 无电池 | `battery -1%`，低电表情代码在但未触发过 |
| 点击音效 | 已接入（仅 idle 播放），听感未人工确认 |
| RTC 纽扣电池 | 未安装；断电后每次开机靠 NTP 校准 |
| 屏幕闪烁历史 | 根因是 SNTP 早启动导致的复位循环，已修复并加安全模式 |
| 脸 240px | 为帧率从 300 降下来；想更大改 `R` 与画布尺寸 |

## 下一步建议（按优先级）

1. **确认交互**：点击音效、屏幕闪烁、低电表情——都需要人在现场验证。
2. **OTA 地址收口**：现在指向开发机；用 `self.system.set_ota_url` 改成长期地址。
3. **外壳**：麦克风开孔、散热、屏幕贴合（非代码）。
4. **可选扩展**：EAF 特效层、Wi-Fi CSI 存在感知、自定义唤醒词。

## 关键文件索引

| 文件 | 作用 |
|---|---|
| `main/display/pet_display.{h,cc}` | 表情层：渲染、状态机、手势、MCP 接口 |
| `main/display/pet_imu.{h,c}` | QMI8658 @0x6B |
| `main/display/pet_rtc.{h,c}` | PCF85063 @0x51，读时/写时 |
| `main/audio/input_level.{h,c}` | 麦克风与 TTS 的只读电平表 |
| `main/mcp_server.cc` | `self.pet.*` 与 `self.system.set_ota_url` |
| `scripts/pet/flash.ps1` | 刷机脚本（watchdog reset） |
| `docs/pet/ROADMAP.md` | 每个任务的目标/做法/验收/证据 |
| `docs/pet/DECISIONS.md` | 技术选型与踩坑记录 |

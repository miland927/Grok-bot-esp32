# Chinchin ESP32 · 鲸鱼娘小智

面向 **Waveshare ESP32-S3-Touch-AMOLED-2.06（32MB Flash / 8MB PSRAM）** 的小智鲸鱼娘项目。此仓库包含完整固件源码、当前10种常用 GIF、50 个固定版本的原始 WebM，以及后续适配资料，方便换电脑继续开发。

当前进度：

- 当前开发版v5：长按角色打开10动作列表，短按开心，语音可打断手动预览；补充服务端情绪标签映射。
- 人物保持约269px显示画布；160px素材的画质已获用户接受继续，600秒观察无重启或崩溃。
- 用户已确认菜单、10种动作、恢复及语音打断正常；重复播放、情绪标签与整机验收继续进行，见 [使用与验收](docs/whale/ACTIONS.md)。
- 已固定50项原始素材；先做较清晰的常用10项，资源包7.31MiB、余量26.85%。

## 本机管理工作台（独立开发分支）

运行 `python scripts/whale/manager.py`，打开 http://127.0.0.1:8765 。可预览与导入动作、保存规则草稿、生成候选资源包和查看任务。此版本不写设备；动态引擎、实际资源安装与遥测待集成。见 [使用说明](docs/whale/HOST_MANAGER.md)，当前状态见 [PROJECT_STATE.md](PROJECT_STATE.md)。

## 换电脑开始开发

建议克隆到英文路径；当前 Windows 开发环境曾遇到 CMake 在中文路径崩溃。

```bash
git clone https://github.com/miland927/chinchin-esp32.git
cd chinchin-esp32
```

安装并激活 **ESP-IDF v6.1**，在其开发终端执行：

```bash
python scripts/whale/build_assets.py --check
python scripts/whale/build.py
```

固件生成到 `build/`。已保留 `dependencies.lock` 锁定当前依赖；依赖由 ESP-IDF 组件管理器获取，无需复制旧电脑的 SDK、缓存或虚拟环境。
当前资源包 `whale/assets.bin` 已包含所需字体、语音模型和10种常用动画，可直接构建。

修改现有 GIF 后，重新打包并构建：

```bash
python scripts/whale/build_assets.py
python scripts/whale/build.py
```

当前10动作的重新转换、打包方法见 [动作说明](docs/whale/ACTIONS.md)。转换参数或工具版本可能改变文件及效果，重新转换需重新验收。

## 常改的文件

| 内容 | 位置 |
| --- | --- |
| 角色大小、触摸、动画切换 | `main/boards/waveshare/esp32-s3-touch-amoled-2.06/whale_display.h` |
| 硬件与独立构建变体 | 同目录下的 `config.h`、`config.json`、板级 `.cc` |
| 当前10种常用 GIF | `whale/animations/common/` |
| 50 个原始 WebM | `whale/source/` |
| 全库清单、用途建议 | `whale/library.json`、`whale/mapping-draft.json` |
| 完整路线图 | [docs/whale/ROADMAP.md](docs/whale/ROADMAP.md) |
| 动作操作与验收 | [docs/whale/ACTIONS.md](docs/whale/ACTIONS.md) |
| 未来界面与Wi-Fi感知 | [docs/whale/FUTURE_FEATURES.md](docs/whale/FUTURE_FEATURES.md) |
| 验证状态与开发交接 | [docs/whale/STATUS.md](docs/whale/STATUS.md) |
| 素材抽帧预览 | `docs/whale/previews/` |

## 构建和刷写边界

此变体保留了原设备的 factory / ota_0 地址，新应用位于 **ota_1：0x1000000**，资源位于 **0x1600000**。
**不要直接使用 `idf.py flash` 或通用 merged 镜像**：它们会把应用放到 factory 地址，可能覆盖保留的原厂程序。
换电脑开发不要求重新刷写启动程序；刷写前应核对目标设备、分区表和本机备份。设备完整备份、NVS、Wi-Fi 配置、原始串口日志不在本仓库。

四线 Flash 的 16MiB 以上缓存映射已在该变体启用。这是 ESP-IDF 实验功能；当前验证只覆盖现有设备，不代表所有 Flash 型号或长期运行均已验证。

## 测试

Windows 可运行本项目的环境适配入口（保持上游断言）：

```bash
python scripts/whale/run_host_tests.py
```

Linux/macOS 可使用上游入口：

```bash
python -m unittest discover -s scripts/tests -v
```

上游批量构建工作流保留为**手动触发**，普通推送不会启动全板型构建。

## 来源与许可

- 固件基于 [`78/xiaozhi-esp32`](https://github.com/78/xiaozhi-esp32)，提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`；保留原 [MIT LICENSE](LICENSE) 和 [上游中文说明](docs/upstream/README.zh.md)。
- 动画来自 [`yanzwzz/dsh-whale-girl-pet`](https://github.com/yanzwzz/dsh-whale-girl-pet)，提交 `b5ce53cfa87b9fc669eef2aabe64946bb77adb72`；保留 `whale/source/SOURCE-LICENSE.txt`。
- 字体、模型和角色素材的原有权利及第三方许可继续适用，见 [whale/NOTICE.md](whale/NOTICE.md)。

鲸鱼工作台的跨电脑使用、素材格式、日志、USB 独立资源安装和恢复说明：[USB_RESOURCE_UPDATES.md](docs/whale/USB_RESOURCE_UPDATES.md)。新增菜单仍待动态引擎接入。

第14动作候选、原生情绪链路、真实容量明细与Wi-Fi感知必做路线：[ACTION14_EMOTION_CAPACITY_ROADMAP.md](docs/whale/ACTION14_EMOTION_CAPACITY_ROADMAP.md)。候选尚未安装，最新版v7源码仍待对齐。

另一台电脑接手请先读：[下一位开发者执行入口](docs/whale/NEXT_DEVELOPER_START_HERE.md)。当前功能分支为`feature/usb-resource-update-20261023`；先保留并同步那台电脑上的最新13动作/v7源码，再做第14动作和说话表情。

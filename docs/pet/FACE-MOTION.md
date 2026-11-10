# 下拉脸形约束与回场弹跳

只改 `PetDisplay` 的下拉姿势与烟花回场，触发阈值、追视镜头、爆炸、音效和 400 ms 回场时长不变。

## 问题

- 下拉满量时 `hop_` 目标 `+16`、`squash_` 目标 `−0.30`，实际脸心约 `y=136`、纵向半径约 `114–116 px`；AA 边缘到 `y≈250–252`，而 240×240 画布在 `y=239` 结束，底部被平切。
- 首次进入 `Return` 时原代码 `hop_ = hop_v_ = 0`，回场只有三次衰减的缩放入场，没有向上弹跳。

## 修改

### 1. 按实际脸形约束

新增纯头文件 [`pet_face_geometry.h`](../../main/display/pet_face_geometry.h)：

```cpp
float ConstrainVerticalRadius(float center_y, float radius_y);
```

- 以实际 `cy` 和 AA 外扩 `sqrt(1.05) ≈ 1.025` 计算上下可用半径。
- 上下各留 2 px；超过后不是硬切，而是 `limit − 6·(1 − e^(−over/6))` 柔和收敛，避免弹簧过冲时出现折角。
- `Render()` 在算出实际 `cy/R/sq/rx/ry` 后调用它，眼睛、瞳孔和脏区都用约束后的 `ry`；`cy` 不变，优先保留可见下移。

关键点：约束放在 **Tick 的弹簧更新之后**，所以松手后的过冲、快速下拉的惯性也覆盖，而不是只改目标值。

### 2. 回场向上弹一次

- 首次 `returning && !firework_returning_`：`hop_ = 0`、`hop_v_ = -300 px/s`、`squash_ = squash_v_ = 0`；之后复用原有 `Spring(&hop_, &hop_v_, 0, dt, 60, 8.5)`，每次演出只注入一次。
- 回场缩放入场从 400 ms 缩短为 **240 ms**（`kReturnEntranceUs`），在 400 ms 回场结束前 160 ms 完成，避免 `65·settle` 的下移吃掉弹跳。
- 33 ms 与板端约 50 ms 取样下，脸心峰值分别越过静止中心 **13.3 px** 与 **10.5 px**，回落最多轻微压一下后停稳。

## 等效预览

```powershell
python scripts/pet/face_return_preview.py
```

产物在 `local-only/animation-source/pet-face/`：

| 文件 | 内容 |
| --- | --- |
| `face-drag-return-33ms.mp4` | 540×320 左右对照，30 fps；慢拉、快拉并保持、松手、回场、斜拉取消 |
| `face-drag-return-50ms.mp4` | 同上，20 fps，对应板端约 50 ms Tick |
| `face-drag-return-metrics.json` | 脸底最大 Y、回场峰值与过冲 |

| 取样 | 版本 | 脸底最大 Y | 回场峰值 Y | 过冲 |
| --- | --- | --- | --- | --- |
| 33 ms | 修改前 | 254.94（被裁） | 120.00 | 0 |
| 33 ms | 修改后 | 236.73（留 2.3 px） | 106.72 | **13.28 px** |
| 50 ms | 修改前 | 254.36（被裁） | 120.00 | 0 |
| 50 ms | 修改后 | 236.15（留 2.8 px） | 109.48 | **10.52 px** |

红色横线是 240×240 画布的最后一行；before 面板底部平切，after 面板保留完整弧线。

## 触摸回弹（果冻感）

在保留原有按压/拖拽目标变形的前提下，只改弹簧动力学和眼睛的二次跟随：

- **Squash/Stretch**：`k=160`、`d=8.5`（原 `105/15`）。松手后从满拉 `-0.30` 反弹到 `+0.09`（约 30% 反向过冲），再回缩、再小幅回弹，约 0.5 s 停稳。
- **按压**：按下的 `squash_v_` 冲量从 `-1.0` 提到 `-2.5`，快速按压也有约 0.08–0.10 的可见压缩/回弹，不靠放大拖拽目标。
- **眼睛二次跟随**：`eye_hop_` 以 `k=45`、`d=9.5` 跟随 `hop_`；显示时取 `0.40 × (eye_hop - hop)` 并限制在 ±4 px，给轮廓运动一点滞后而不脱脸。
- **Hop/回场**：`k=60`、`d=8.5` 与 `-300 px/s` 回场冲量保持，烟花回场仍只上弹一次 10–16 px。

等效预览：

```powershell
python scripts/pet/face_touch_preview.py
```

| 取样 | 版本 | 松手过冲 | 振荡 | 停稳 | 按压压缩 | 眼睛滞后 |
| --- | --- | --- | --- | --- | --- | --- |
| 33 ms | 修改前 | 0.002 | 0 | 300 ms | −0.024 | 0 |
| 33 ms | 修改后 | **0.090** | 3 次过零 | 567 ms | **−0.099** | 3.8 px |
| 50 ms | 修改前 | 0.000 | 0 | 300 ms | −0.013 | 0 |
| 50 ms | 修改后 | **0.089** | 3 次过零 | 500 ms | **−0.085** | 3.4 px |

快速拉到底再松手的过冲为 0.112（33 ms）/0.114（50 ms）。预览片在 `local-only/animation-source/pet-face/face-touch-rebound-*.mp4`；红色横线仍是 240×240 画布最后一行，修改前后脸底都在约束内（≤236.7）。

## 验证与构建

```powershell
python -m unittest scripts.tests.test_pet_face_geometry -v
python -m unittest scripts.tests.test_pet_face_motion -v
python scripts/whale/workflow.py test
python scripts/build.py waveshare/esp32-s3-touch-amoled-2.06 --name esp32-s3-touch-amoled-2.06
```

- 新主机测试覆盖：静止不改、满拉、+36 px 过冲、向上 −36 px、完整弹簧包络、连续性；果冻弹簧的过冲/过零/停稳、按压峰值、眼睛滞后。
- 全量主机回归：164 项，157 通过，7 项既有环境跳过；`test_pet_face_geometry`、`test_pet_face_motion` 与 `test_fireworks_camera` 均实际执行。
- 当前工作树还包含并行的 TNT 代码，构建会一并编译（`pet_tnt.cc`）；本次没有修改 TNT。果冻构建已刷入设备（ELF `1080ffb3…`，应用镜像 3,253,024 B，余量 21.21%）；TNT 行为不在本次验收范围。
- 刷前上一版脸部应用已另存并校验：`local-only/backups/app-ota0-face-20261110-011651.bin`，SHA-256 `C2DD2FFC…`。

实机观察项：慢拉到底并保持、快速拉到底并保持、斜拉取消、松手发射、插卡/无卡回场；重点看脸底完整弧线、一次上弹 10–16 px、无裁切/跳变/反复弹跳。

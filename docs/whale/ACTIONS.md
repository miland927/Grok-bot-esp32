# 常用10动作：当前开发版 v5

## 怎么操作

设备空闲时，长按角色约半秒打开动作列表，上下滑动列表查看全部10项。点选后播放约10.2秒，再恢复当前情绪/待机；短按角色会中断手动播放并给出开心反馈。点“关闭”退出列表。长按后松手不会额外触发短按开心。

开始语音对话时，列表或手动播放会在状态检测/收到用户文字时退出；不等待动画播完。对话期间不能打开列表，会提示对话结束后再选。当前不自动按空闲时间进入睡眠。

| 动作 | 素材 | 自动触发 |
| --- | --- | --- |
| 待机呼吸 | W024 | 默认/未知情绪回退 |
| 工作思考 | W019 | 等待回复、thinking |
| 开心跃动 | W031 | 短按，happy/laughing/loving/funny |
| 东张西望 | W001 | 当前仅列表主动选择 |
| 小憩沉眠 | W006 | sleepy；也可主动选择 |
| 打盹醒来 | W027 | 当前仅列表主动选择 |
| 傲娇生气 | W029 | angry |
| 害羞 | W030 | embarrassed |
| 叹气 | W035 | sad/crying的近似表达 |
| 吓一跳 | W043 | surprised/shocked |

自动表情依据服务端情绪标签，不表示识别了用户声调或真实心情。其余标签回退待机；手动选择期间以所选动作为主，真实对话会打断手动预览。资源缺失或GIF打开失败时回退neutral，并避免重复尝试同一失败动作。

## 当前验证状态

v4的160px画质已获用户接受继续，600秒观察无重启或崩溃标记；串口记录7次触摸、6次speaking转换，不据此声称已完成10次触摸或3个独立完整语音轮次。

v5板级应用已构建，资源键核对和容量检查通过。用户已确认列表和10种动作、恢复及语音打断正常，记为首轮实机检查通过；每项至少3次重复、新情绪标签逐项覆盖和长时间稳定性仍待验收。现有资源包7,669,838字节（7.31MiB），余量26.85%；160px/8fps/64色，显示画布约269px。包内动画和显示尺寸沿用v4，本轮只更新应用。

## 构建和修改资源

默认源码与`whale/assets.bin`现在是v5开发版，已通过用户首轮交互检查，仍需重复、标签覆盖和整机验收。当前GIF位于`whale/animations/common/`，原三种224px文件保留作历史参考。标准检查/构建：

```sh
python scripts/whale/build_assets.py --check
python scripts/whale/build.py
```

修改当前10项GIF后执行`python scripts/whale/build_assets.py`重打包。从原视频重新转换当前10项：

```sh
python scripts/whale/benchmark_whale_resolution.py --source whale/source --output local-only/common-ten --sizes 160 --fps 8 --colors 64 --library whale/library.json --ids W001 W006 W019 W024 W027 W029 W030 W031 W035 W043
python scripts/whale/pack_common_whale_assets.py --repo . --candidates local-only/common-ten --output local-only/common-ten-package
```

新候选需要同步更新common目录素材与资源包并重新检查；旧`convert.py --action neutral`仍生成历史224px三动作素材，不作为当前common包的转换入口。

## 本轮怎样算通过

1. 长按打开/关闭列表至少5次，长按不同时触发开心；滚动列表不误选。
2. 10个动作逐项完整播放，每项至少3次；人物比例一致，无新裁切、残影或异常透明底。
3. 点选后约10秒恢复；播放中短按能切回开心；播放或列表中唤醒对话能及时退出，回复正常。
4. 至少5轮语音，其中覆盖对话中点按和打断手动动作；检查支持情绪标签及未知标签回退。手动预览通过不能替代情绪标签验证。
5. 运行观察及断电恢复通过后再更新验收状态；最终2小时/断网等整机验收仍按总路线图执行。

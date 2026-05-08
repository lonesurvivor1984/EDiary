## Why

当前程序启动后需要用户主动输入选项，对于日常快速记录的场景不够流畅。写日记是最高频操作，3秒无操作后自动进入写日记模式，可减少每次启动的交互摩擦。

## What Changes

- 菜单显示后启动3秒倒计时，实时刷新剩余秒数
- 倒计时结束前若用户按下 `1` 或 `2`，立即响应对应模式
- 倒计时结束若无输入，自动进入写日记模式（等同选择 `1`）

## Capabilities

### New Capabilities

- `countdown-auto-select`: 启动菜单的倒计时与自动默认选择逻辑

### Modified Capabilities

（无现有 spec 需变更）

## Impact

- 修改 `diary.c` 中的 `main()` 函数菜单循环部分
- 需使用 Windows API（`WaitForSingleObject` / `_kbhit` + `_getch`）实现非阻塞输入检测
- 不影响 `write_diary()` 和 `search_diaries()` 函数
- 不影响 `diary.py`

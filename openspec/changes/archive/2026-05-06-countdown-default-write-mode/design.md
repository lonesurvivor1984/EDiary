## Context

`diary.c` 的 `main()` 当前使用阻塞式 `scanf("%d", &choice)` 读取菜单选项，无法在等待输入的同时执行计时逻辑。需要在 Windows 环境下实现"有限时等待键盘输入"。

## Goals / Non-Goals

**Goals:**
- 菜单显示后进行3秒倒计时，每秒刷新提示
- 倒计时期间用户按键立即响应
- 超时后自动选择写日记模式

**Non-Goals:**
- 不修改 `write_diary()` / `search_diaries()` 函数
- 不修改 `diary.py`
- 不支持跨平台（仍仅限 Windows）

## Decisions

### 使用 `_kbhit()` + `_getch()` 轮询，配合 `Sleep()`

**选择方案**：在1秒间隔的循环中用 `_kbhit()` 检查是否有按键，有则用 `_getch()` 读取字符并解析选项；无则 `Sleep(1000)` 后继续。

**放弃方案**：`WaitForSingleObject(GetStdHandle(STD_INPUT_HANDLE), 1000)` — 会在有任何控制台输入事件（包括鼠标移动）时提前返回，处理逻辑更复杂。

`_kbhit()` / `_getch()` 来自 `<conio.h>`，MinGW 已内置，无需额外依赖。

### 倒计时刷新方式：`\r` 覆写同一行

每次循环用 `\r` 回到行首重新打印剩余秒数，避免屏幕滚动，视觉更整洁。

### 读取字符而非数字

`_getch()` 返回字符，用 `== '1'` / `== '2'` 判断，而非 `scanf` 的整数解析，简单可靠。

## Risks / Trade-offs

- `_kbhit()` 是 MS 扩展，非标准 C，但本项目已深度依赖 Windows API，可接受。
- 若用户按下其他键（非 `1`/`2`），当前设计忽略该按键并继续倒计时；倒计时结束后仍自动进入写日记模式。这是合理的保守行为。

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

A Windows command-line diary application implemented in both C (`diary.c`) and Python (`diary.py`). The C version uses the Windows Console API for UTF-8/Chinese support and ANSI color highlighting. The Python version is a portable alternative targeting `D:\Notes\`.

## Build

Requires MinGW (GCC + windres) on Windows.

```bash
# Compile the Windows resource (embeds icon)
windres diary.rc -O coff -o diary.res

# Compile the binary
gcc diary.c diary.res -o diary.exe
```

No build automation, test suite, or linting infrastructure exists.

## Architecture

**Data format** (`2021.txt`): Plain text, entries separated by `\n\n`. Each entry is `YYYY.MM.DD HH:MM\n<multi-line content>\n\n`. Max file size 5 MB.

**C version (`diary.c`)** — two modes selected from a menu:
- **Write mode** (`write_diary`): allocates a 5 MB buffer, reads multi-line input terminated by a blank line, appends timestamped entry to `2021.txt`.
- **Search mode** (`search_diaries` → `print_matching_lines` → `print_with_highlight`): loads the entire file, splits on `\n\n`, matches entries via `strstr`, and prints ±3 lines of context with ANSI green highlighting.

`main()` bootstraps Windows UTF-8 console support (`SetConsoleOutputCP(65001)`, `SetConsoleCP(65001)`) and enables ANSI escape sequences (`ENABLE_VIRTUAL_TERMINAL_PROCESSING`) before entering the menu loop.

**Python version (`diary.py`)**: mirrors the same two-mode structure using standard library only; uses case-insensitive matching and writes to `D:\Notes\` instead of the local `2021.txt`.

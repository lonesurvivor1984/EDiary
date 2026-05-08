#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

#include <windows.h>
#include <conio.h>

#define MAX_LINE_LEN  4096
#define MAX_FILE_SIZE 5000000
#define MAX_LINES     2000
#define DIARY_FILE    "diary.txt"
#define DEFAULT_BACKUP_DIR "D:\\桌面\\Notes\\DK"

#define COLOR_DARKGREEN "\x1b[32m"
#define COLOR_DARKGRAY "\x1b[90m"
#define COLOR_RESET    "\x1b[0m"

void get_current_time(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buffer, size, "%Y.%m.%d %H:%M", t);
}

// 大小写不敏感的子串搜索
char *my_strcasestr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
            const char *h = haystack, *n = needle;
            while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
                h++; n++;
            }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}

// 把光标定位回行起点，清屏到末尾，重绘当前行内容
// 这样长行 wrap 也不会留残影
static void redraw_line(HANDLE hOut, COORD start, const char *cur, int cur_len) {
    int is_exit = (cur_len > 0 && cur_len <= 5 &&
                   strncmp(cur, "/exit", cur_len) == 0);
    fflush(stdout);
    SetConsoleCursorPosition(hOut, start);
    printf("\033[J"); // 清光标到屏幕末（含 wrap 出来的物理行）
    if (is_exit) printf("\x1b[32m%s\x1b[0m", cur);
    else         printf("%s", cur);
    fflush(stdout);
}

// UTF-8 → UTF-16；调用方负责 free
static wchar_t *utf8_to_wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc(n * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

// 递归创建目录（含中间层级）
static int ensure_dir_w(const wchar_t *path) {
    wchar_t buf[MAX_PATH];
    wcsncpy(buf, path, MAX_PATH);
    buf[MAX_PATH - 1] = 0;
    // 跳过盘符（如 "D:\"）后开始扫描
    wchar_t *p = buf;
    if (wcslen(buf) >= 3 && buf[1] == L':') p = buf + 3;
    for (; *p; p++) {
        if (*p == L'\\' || *p == L'/') {
            wchar_t saved = *p;
            *p = 0;
            CreateDirectoryW(buf, NULL); // 已存在则忽略
            *p = saved;
        }
    }
    CreateDirectoryW(buf, NULL);
    DWORD attr = GetFileAttributesW(buf);
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

void backup_diary(void) {
    char dir[MAX_LINE_LEN];
    dir[0] = '\0';

    printf(COLOR_DARKGRAY "\n=== 备份日记 ===\n" COLOR_RESET);
    printf(COLOR_DARKGRAY "备份目录（回车使用默认 %s）：" COLOR_RESET, DEFAULT_BACKUP_DIR);
    if (fgets(dir, sizeof(dir), stdin))
        dir[strcspn(dir, "\n")] = '\0';

    if (strcmp(dir, "/exit") == 0) {
        printf(COLOR_DARKGRAY "\n再见！\n" COLOR_RESET);
        exit(0);
    }
    if (dir[0] == '\0') {
        strncpy(dir, DEFAULT_BACKUP_DIR, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = '\0';
    }

    // 时间戳文件名
    char fname[64];
    time_t now = time(NULL);
    strftime(fname, sizeof(fname), "diary_%Y.%m.%d_%H.%M.txt", localtime(&now));

    // 拼完整目标路径（UTF-8）
    char dst[MAX_LINE_LEN + 80];
    snprintf(dst, sizeof(dst), "%s\\%s", dir, fname);

    wchar_t *wdir = utf8_to_wide(dir);
    wchar_t *wdst = utf8_to_wide(dst);
    wchar_t *wsrc = utf8_to_wide(DIARY_FILE);
    if (!wdir || !wdst || !wsrc) {
        printf(COLOR_DARKGRAY "\n路径转换失败\n" COLOR_RESET);
        free(wdir); free(wdst); free(wsrc);
        return;
    }

    if (!ensure_dir_w(wdir)) {
        printf(COLOR_DARKGRAY "\n无法创建目录：%s\n" COLOR_RESET, dir);
    } else if (!CopyFileW(wsrc, wdst, FALSE)) {
        printf(COLOR_DARKGRAY "\n备份失败（错误码 %lu）\n" COLOR_RESET,
               (unsigned long)GetLastError());
    } else {
        printf(COLOR_DARKGRAY "\n已备份到 %s\n" COLOR_RESET, dst);
    }
    free(wdir); free(wdst); free(wsrc);
}

void write_diary() {
    char datetime[32];
    char *content = (char *)malloc(MAX_FILE_SIZE);

    if (!content) {
        printf(COLOR_DARKGRAY "\n内存分配失败！\n" COLOR_RESET);
        return;
    }

    get_current_time(datetime, sizeof(datetime));
    printf(COLOR_DARKGRAY "\nWelcome. 当前时间: %s\n" COLOR_RESET, datetime);
    printf(COLOR_DARKGRAY "请输入日记内容（两次回车保存，行首退格回退上一行）：\n" COLOR_RESET);

    char *lines[MAX_LINES];
    COORD line_starts[MAX_LINES]; // 每一已提交行在屏幕上的起始位置
    int   line_count = 0;
    char  cur[MAX_LINE_LEN];
    int   cur_len = 0;
    cur[0] = '\0';

    HANDLE hIn  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    WCHAR  pending_high = 0;

    // 记录当前正在编辑的行的起始光标位置
    fflush(stdout);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hOut, &csbi);
    COORD cur_line_start = csbi.dwCursorPosition;

    while (1) {
        INPUT_RECORD ir;
        DWORD events = 0;
        if (!ReadConsoleInputW(hIn, &ir, 1, &events) || events == 0) continue;
        if (ir.EventType != KEY_EVENT) continue;
        if (!ir.Event.KeyEvent.bKeyDown) continue;

        WCHAR wc = ir.Event.KeyEvent.uChar.UnicodeChar;
        if (wc == 0) continue;

        if (wc == L'\r' || wc == L'\n') {
            if (cur_len == 0) {
                printf("\n");
                break;
            }
            if (strcmp(cur, "/exit") == 0) {
                printf(COLOR_DARKGRAY "\n再见！\n" COLOR_RESET);
                for (int i = 0; i < line_count; i++) free(lines[i]);
                free(content);
                exit(0);
            }
            char *saved = (char *)malloc(cur_len + 1);
            if (saved && line_count < MAX_LINES) {
                memcpy(saved, cur, cur_len + 1);
                lines[line_count]       = saved;
                line_starts[line_count] = cur_line_start;
                line_count++;
            } else {
                free(saved);
            }
            cur_len = 0;
            cur[0] = '\0';
            pending_high = 0;
            printf("\n");
            // 记录新一行的起点
            fflush(stdout);
            GetConsoleScreenBufferInfo(hOut, &csbi);
            cur_line_start = csbi.dwCursorPosition;

        } else if (wc == L'\b') {
            pending_high = 0;
            if (cur_len > 0) {
                while (cur_len > 0 && ((unsigned char)cur[cur_len - 1] & 0xC0) == 0x80)
                    cur_len--;
                if (cur_len > 0) cur_len--;
                cur[cur_len] = '\0';
                redraw_line(hOut, cur_line_start, cur, cur_len);
            } else if (line_count > 0) {
                line_count--;
                char *prev = lines[line_count];
                cur_len = (int)strlen(prev);
                memcpy(cur, prev, cur_len + 1);
                free(prev);
                cur_line_start = line_starts[line_count];
                redraw_line(hOut, cur_line_start, cur, cur_len);
            }

        } else if (wc < 0x20) {
            continue;

        } else {
            WCHAR wbuf[2];
            int wcount;
            if (wc >= 0xD800 && wc <= 0xDBFF) {
                pending_high = wc;
                continue;
            }
            if (wc >= 0xDC00 && wc <= 0xDFFF && pending_high) {
                wbuf[0] = pending_high;
                wbuf[1] = wc;
                wcount = 2;
                pending_high = 0;
            } else {
                wbuf[0] = wc;
                wcount = 1;
                pending_high = 0;
            }

            char utf8buf[8];
            int n = WideCharToMultiByte(CP_UTF8, 0, wbuf, wcount,
                                         utf8buf, sizeof(utf8buf), NULL, NULL);
            if (n > 0 && cur_len + n < MAX_LINE_LEN - 1) {
                memcpy(cur + cur_len, utf8buf, n);
                cur_len += n;
                cur[cur_len] = '\0';
                redraw_line(hOut, cur_line_start, cur, cur_len);
            }
        }
    }

    // 将行数组拼入 content
    size_t content_len = 0;
    for (int i = 0; i < line_count; i++) {
        size_t ll = strlen(lines[i]);
        if (content_len > 0) content[content_len++] = '\n';
        memcpy(content + content_len, lines[i], ll);
        content_len += ll;
        free(lines[i]);
    }
    content[content_len] = '\0';

    if (content_len == 0) {
        printf(COLOR_DARKGRAY "\n内容为空，未保存。\n" COLOR_RESET);
        free(content);
        return;
    }

    FILE *fp = fopen(DIARY_FILE, "a");
    if (fp == NULL) {
        printf(COLOR_DARKGRAY "\n无法打开日记文件！\n" COLOR_RESET);
        free(content);
        return;
    }

    fprintf(fp, "\n\n%s\n%s", datetime, content);
    fclose(fp);

    printf(COLOR_DARKGRAY "\n日记已保存！\n" COLOR_RESET);
    free(content);
}

// 打印文本并高亮关键词（大小写不敏感，保留原始大小写）
void print_with_highlight(const char *text, const char *keyword) {
    if (!keyword || keyword[0] == '\0') {
        printf("%s", text);
        return;
    }

    const char *pos = text;
    size_t kw_len = strlen(keyword);

    while (*pos) {
        const char *match = my_strcasestr(pos, keyword);
        if (match) {
            printf("%.*s", (int)(match - pos), pos);
            printf(COLOR_DARKGREEN "%.*s" COLOR_RESET, (int)kw_len, match);
            pos = match + kw_len;
        } else {
            printf("%s", pos);
            break;
        }
    }
}

void print_matching_lines(const char *title, const char *body,
                           const char *keyword, int *item_num) {
    char *bc = strdup(body);
    if (!bc) return;

    // 统计行数，按 \n 分割，行指针直接指向 bc 内部
    int lc = 1;
    for (char *p = bc; *p; p++) if (*p == '\n') lc++;

    char **lines = (char **)malloc(lc * sizeof(char *));
    if (!lines) { free(bc); return; }

    lines[0] = bc;
    int n = 1;
    for (char *p = bc; *p; p++) {
        if (*p == '\n') { *p = '\0'; if (n < lc) lines[n++] = p + 1; }
    }
    lc = n;

    int printed_header = 0;
    int last_shown = -10;

    for (int i = 0; i < lc; i++) {
        if (!my_strcasestr(lines[i], keyword)) continue;

        if (!printed_header) {
            printf(COLOR_DARKGRAY "\n【条目 %d】" COLOR_RESET, (*item_num)++);
            print_with_highlight(title, keyword);
            printf("\n");
            printed_header = 1;
        }

        int start = (i - 3 < 0) ? 0 : i - 3;
        int end   = (i + 3 >= lc) ? lc - 1 : i + 3;

        if (last_shown >= 0 && start > last_shown + 1)
            printf(COLOR_DARKGRAY "  ...\n" COLOR_RESET);

        int from = (start > last_shown + 1) ? start : last_shown + 1;
        for (int j = from; j <= end; j++) {
            printf("  ");
            print_with_highlight(lines[j], keyword);
            printf("\n");
        }
        last_shown = end;
    }

    if (printed_header)
        printf(COLOR_DARKGRAY "──────────────────────────────\n" COLOR_RESET);

    free(lines);
    free(bc);
}

void search_diaries() {
    char keyword[MAX_LINE_LEN] = "";

    printf(COLOR_DARKGRAY "\n=== 日记搜索 ===\n" COLOR_RESET);
    printf(COLOR_DARKGRAY "关键词：" COLOR_RESET);
    if (fgets(keyword, sizeof(keyword), stdin))
        keyword[strcspn(keyword, "\n")] = '\0';

    if (strcmp(keyword, "/exit") == 0) {
        printf(COLOR_DARKGRAY "\n再见！\n" COLOR_RESET);
        exit(0);
    }

    if (keyword[0] == '\0') {
        printf(COLOR_DARKGRAY "\n未输入关键词，返回菜单。\n" COLOR_RESET);
        return;
    }

    char *content = (char *)malloc(MAX_FILE_SIZE);
    if (!content) {
        printf(COLOR_DARKGRAY "\n内存分配失败！\n" COLOR_RESET);
        return;
    }

    FILE *fp = fopen(DIARY_FILE, "rb");
    if (fp == NULL) {
        printf(COLOR_DARKGRAY "\n没有找到日记文件 %s\n" COLOR_RESET, DIARY_FILE);
        free(content);
        return;
    }

    size_t read_size = fread(content, 1, MAX_FILE_SIZE - 1, fp);
    content[read_size] = '\0';
    fclose(fp);

    if (read_size == 0) {
        printf(COLOR_DARKGRAY "\n日记文件为空\n" COLOR_RESET);
        free(content);
        return;
    }

    printf(COLOR_DARKGRAY "\n正在搜索...\n" COLOR_RESET);

    int item_num = 1;
    char *pos = content;

    while (*pos) {
        while (*pos == '\n' || *pos == '\r') pos++;
        if (*pos == '\0') break;

        char *entry_start = pos;
        char *entry_end = strstr(pos, "\n\n");

        size_t entry_len;
        if (entry_end) {
            entry_len = entry_end - entry_start;
            pos = entry_end + 2;
        } else {
            entry_len = strlen(entry_start);
            pos += entry_len;
        }

        if (entry_len == 0) continue;

        char *entry = (char *)malloc(entry_len + 1);
        if (!entry) continue;

        memcpy(entry, entry_start, entry_len);
        entry[entry_len] = '\0';

        char *newline_pos = strchr(entry, '\n');
        if (newline_pos) {
            *newline_pos = '\0';
            char *title = entry;
            char *body  = newline_pos + 1;

            int kw_ok = (my_strcasestr(title, keyword) != NULL) ||
                        (my_strcasestr(body,  keyword) != NULL);

            if (kw_ok) {
                print_matching_lines(title, body, keyword, &item_num);
            }
        }

        free(entry);
    }

    if (item_num == 1) {
        printf(COLOR_DARKGRAY "\n未找到匹配的日记条目\n" COLOR_RESET);
    } else {
        printf(COLOR_DARKGRAY "\n──────────────────────────────\n" COLOR_RESET);
        printf(COLOR_DARKGRAY "找到 %d 条匹配结果\n" COLOR_RESET, item_num - 1);
    }

    free(content);
}

int main() {
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hConsole, &mode);
    SetConsoleMode(hConsole, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    int first_launch = 1;

    static const char *mottos[] = {
        "天行健，君子以自强不息。",
        "每一个不曾起舞的日子，都是对生命的辜负。",
        "不是看到希望才坚持，而是坚持了才能看到希望。",
        "最慢的步伐不是跬步，而是徘徊；最快的脚步不是冲刺，而是坚持。",
        "世界上只有一种真正的英雄主义，那就是在认清生活的真相后依然热爱生活。",
        "你若不勇敢，没人替你坚强。",
        "人生没有白走的路，每一步都算数。",
        "半山腰总是最挤的，你得去山顶看看。",
        "将来的你，一定会感谢现在拼命的自己。",
        "没有特别幸运，请先特别努力。",
        "成功路上并不拥挤，因为坚持到底的人不多。",
        "既然选择了远方，便只顾风雨兼程。",
        "行动是治愈恐惧的良药，而犹豫、拖延将不断滋养恐惧。",
        "不要在该奋斗的年纪选择安逸，否则你将用一生去面对平庸。",
        "与其担心未来，不如现在好好努力。",
        "等风来，不如追风去。",
        "种一棵树最好的时间是十年前，其次是现在。",
        "临渊羡鱼，不如退而结网。",
        "只有千锤百炼，才能成为好钢。",
        "你现在的努力里，藏着十年后的样子。",
        "生活不会辜负每一个认真努力的人。",
        "挫折是成长的阶梯，每一次摔倒，都让你离成功更近一步。",
        "那些杀不死你的，终将使你更强大。",
        "你受的苦，吃的亏，担的责，扛的罪，忍的痛，到最后都会变成光，照亮你的路。",
        "强者不是没有眼泪，而是含着泪奔跑。",
        "在无人问津的地方历练，然后顶峰相见。",
        "你若决定灿烂，山无遮，海无拦。",
        "做自己的太阳，无需凭借谁的光。",
        "与其羡慕别人的光芒，不如打磨自己的精彩。",
        "人生最大的改变，就是去做自己害怕的事情。",
        "生命不是要超越别人，而是要超越自己。",
        "梦想不会发光，发光的是追梦的你。",
        "当一个人踮起脚尖靠近太阳的时候，全世界都挡不住他的阳光。",
        "鲜衣怒马少年时，不负韶华行且知。",
        "少年不惧岁月长，彼方尚有荣光在。",
        "心若有所向往，何惧道阻且长。",
        "你若盛开，清风自来。",
        "人生没有彩排，每一天都是现场直播。",
        "盛年不重来，一日难再晨。及时当勉励，岁月不待人。",
        "追风赶月莫停留，平芜尽处是春山。",
        "熬过无人问津的日子，才有诗和远方。",
        "没有伞的孩子，必须努力奔跑。",
        "生活给你压力，你就还它奇迹；人生给你考验，你就还它经验。",
        "努力的最大意义，是让自己随时有能力跳出厌恶的圈子。",
        "世界上没有绝望的处境，只有对处境绝望的人。",
        "只要心中有光，任何地方都不再黑暗。",
        "顺境的美德是节制，逆境的美德是坚韧。",
        "生活给你一百个理由哭泣，你要拿出一千个理由笑给它看。",
        "当你足够优秀时，你周围的一切自然都会好起来。",
        "愿你以渺小启程，以伟大结束；愿你出走半生，归来仍是少年。",
        "成功只有一个——按照自己的方式，去度过人生。",
    };
    srand((unsigned int)time(NULL));
    int motto_idx = rand() % (sizeof(mottos) / sizeof(mottos[0]));
    printf("\x1b[38;2;45;80;130m\n\"%s\"\x1b[0m", mottos[motto_idx]);

    while (1) {
        printf(COLOR_DARKGRAY "\n请选择模式：\n" COLOR_RESET);
        printf(COLOR_DARKGRAY "1. 写日记\n" COLOR_RESET);
        printf(COLOR_DARKGRAY "2. 搜索日记\n" COLOR_RESET);
        printf(COLOR_DARKGRAY "3. 退出\n" COLOR_RESET);
        printf(COLOR_DARKGRAY "4. 备份日记\n" COLOR_RESET);

        int choice = 0;

        if (first_launch) {
            first_launch = 0;
            for (int i = 3; i >= 1 && choice == 0; i--) {
                printf(COLOR_DARKGRAY "\r请输入选择 (%d 秒后自动写日记): " COLOR_RESET, i);
                fflush(stdout);
                for (int j = 0; j < 10 && choice == 0; j++) {
                    if (_kbhit()) {
                        int c = _getch();
                        if (c == '1') choice = 1;
                        else if (c == '2') choice = 2;
                        else if (c == '3') choice = 3;
                        else if (c == '4') choice = 4;
                    }
                    Sleep(100);
                }
            }
            if (choice == 0) {
                printf(COLOR_DARKGRAY "\n自动进入写日记模式\n" COLOR_RESET);
                choice = 1;
            } else {
                printf("\n");
            }
        } else {
            printf(COLOR_DARKGRAY "请输入选择（1/2/3/4）：" COLOR_RESET);
            char buf[16];
            if (fgets(buf, sizeof(buf), stdin)) {
                buf[strcspn(buf, "\n")] = '\0';
                if (strcmp(buf, "/exit") == 0) choice = 3;
                else if (buf[0] == '1') choice = 1;
                else if (buf[0] == '2') choice = 2;
                else if (buf[0] == '3') choice = 3;
                else if (buf[0] == '4') choice = 4;
                else {
                    printf(COLOR_DARKGRAY "无效输入，请输入 1、2、3 或 4\n" COLOR_RESET);
                    continue;
                }
            }
        }

        if      (choice == 1) write_diary();
        else if (choice == 2) search_diaries();
        else if (choice == 3) { printf(COLOR_DARKGRAY "\n再见！\n" COLOR_RESET); break; }
        else if (choice == 4) backup_diary();
    }

    return 0;
}

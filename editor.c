#include "editor.h"

/* ---------------------- Global Editor State ---------------------- */
struct editorConfig E;

/* ------------------------- Syntax Highlighting Database ------------------------- */

/* Example configuration for C/C++ */
char *C_HL_extensions[] = { ".c", ".h", ".cpp", ".hpp", ".cc", NULL };

char *C_HL_keywords[] = {
    /* C Keywords */
    "auto", "break", "case", "continue", "default", "do", "else", "enum",
    "extern", "for", "goto", "if", "register", "return", "sizeof", "static",
    "struct", "switch", "typedef", "union", "volatile", "while", "NULL",

    /* C++ Keywords */
    "alignas", "alignof", "and", "and_eq", "asm", "bitand", "bitor", "class",
    "compl", "constexpr", "const_cast", "deltype", "delete", "dynamic_cast",
    "explicit", "export", "false", "friend", "inline", "mutable", "namespace",
    "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq",
    "private", "protected", "public", "reinterpret_cast", "static_assert",
    "static_cast", "template", "this", "thread_local", "throw", "true", "try",
    "typeid", "typename", "virtual", "xor", "xor_eq",

    /* C Types */
    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", "short|", "auto|", "const|", "bool|", NULL
};

struct editorSyntax HLDB[] = {
    {
        /* C / C++ */
        C_HL_extensions,
        C_HL_keywords,
        "//",   /* Single-line comment */
        "/*",   /* Start of multi-line comment */
        "*/",   /* End of multi-line comment */
        HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS
    }
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/* --------------------- Terminal Handling --------------------- */

static struct termios orig_termios;

void disableRawMode(int fd) {
    if (E.rawmode) {
        tcsetattr(fd, TCSAFLUSH, &orig_termios);
        E.rawmode = 0;
    }
}

static void editorAtExit(void) {
    disableRawMode(STDIN_FILENO);
}

int enableRawMode(int fd) {
    struct termios raw;
    if (E.rawmode) return 0;
    if (!isatty(STDIN_FILENO)) goto fatal;
    atexit(editorAtExit);
    if (tcgetattr(fd, &orig_termios) == -1) goto fatal;
    raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = STE_VMIN;
    raw.c_cc[VTIME] = STE_VTIME;
    if (tcsetattr(fd, TCSAFLUSH, &raw) < 0) goto fatal;
    E.rawmode = 1;
    return 0;
fatal:
    errno = ENOTTY;
    return -1;
}

int editorReadKey(int fd) {
    int nread;
    char c, seq[3];
    while ((nread = read(fd, &c, 1)) == 0);
    if (nread == -1) exit(1);
    if (c == ESC) {
        if (read(fd, seq, 1) == 0) return ESC;
        if (read(fd, seq + 1, 1) == 0) return ESC;
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(fd, seq + 2, 1) == 0) return ESC;
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '3': return DEL_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
    }
    return c;
}

int getCursorPosition(int ifd, int ofd, int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;
    if (write(ofd, "\x1b[6n", 4) != 4) return -1;
    while (i < sizeof(buf) - 1) {
        if (read(ifd, buf + i, 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';
    if (buf[0] != ESC || buf[1] != '[') return -1;
    if (sscanf(buf + 2, "%d;%d", rows, cols) != 2) return -1;
    return 0;
}

int getWindowSize(int ifd, int ofd, int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        int orig_row, orig_col, retval;
        retval = getCursorPosition(ifd, ofd, &orig_row, &orig_col);
        if (retval == -1) goto failed;
        if (write(ofd, "\x1b[999C\x1b[999B", 12) != 12) goto failed;
        retval = getCursorPosition(ifd, ofd, rows, cols);
        if (retval == -1) goto failed;
        char seq[32];
        snprintf(seq, sizeof(seq), "\x1b[%d;%dH", orig_row, orig_col);
        write(ofd, seq, strlen(seq));
        return 0;
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
failed:
    return -1;
}

/* ------------------- Syntax Highlighting Functions ------------------- */

int is_separator(int c) {
    return c == '\0' || isspace(c) || strchr(",.()+-/*=~%[];", c) != NULL;
}

int editorRowHasOpenComment(erow *row) {
    if (row->hl && row->rsize &&
        row->hl[row->rsize - 1] == HL_MLCOMMENT &&
        (row->rsize < 2 || (row->render[row->rsize - 2] != '*' ||
                            row->render[row->rsize - 1] != '/')))
    {
        return 1;
    }
    return 0;
}

void editorUpdateSyntax(erow *row) {
    row->hl = realloc(row->hl, row->rsize);
    memset(row->hl, HL_NORMAL, row->rsize);
    if (E.syntax == NULL) return;
    int i = 0, prev_sep = 1, in_string = 0, in_comment = 0;
    char *p = row->render;
    char **keywords = E.syntax->keywords;
    char *scs = E.syntax->singleline_comment_start;
    char *mcs = E.syntax->multiline_comment_start;
    char *mce = E.syntax->multiline_comment_end;
    if (row->idx > 0 && editorRowHasOpenComment(&E.row[row->idx - 1]))
        in_comment = 1;
    while (*p) {
        if (prev_sep && *p == scs[0] && *(p + 1) == scs[1]) {
            memset(row->hl + i, HL_COMMENT, row->size - i);
            return;
        }
        if (in_comment) {
            row->hl[i] = HL_MLCOMMENT;
            if (*p == mce[0] && *(p + 1) == mce[1]) {
                row->hl[i + 1] = HL_MLCOMMENT;
                p += 2; i += 2;
                in_comment = 0;
                prev_sep = 1;
                continue;
            } else {
                prev_sep = 0;
                p++; i++;
                continue;
            }
        } else if (*p == mcs[0] && *(p + 1) == mcs[1]) {
            row->hl[i] = HL_MLCOMMENT;
            row->hl[i + 1] = HL_MLCOMMENT;
            p += 2; i += 2;
            in_comment = 1;
            prev_sep = 0;
            continue;
        }
        if (in_string) {
            row->hl[i] = HL_STRING;
            if (*p == '\\') {
                row->hl[i + 1] = HL_STRING;
                p += 2; i += 2;
                prev_sep = 0;
                continue;
            }
            if (*p == in_string) in_string = 0;
            p++; i++;
            continue;
        } else if (*p == '"' || *p == '\'') {
            in_string = *p;
            row->hl[i] = HL_STRING;
            p++; i++;
            prev_sep = 0;
            continue;
        }
        if (!isprint(*p)) {
            row->hl[i] = HL_NONPRINT;
            p++; i++;
            prev_sep = 0;
            continue;
        }
        if ((isdigit(*p) && (prev_sep || row->hl[i - 1] == HL_NUMBER)) ||
            (*p == '.' && i > 0 && row->hl[i - 1] == HL_NUMBER))
        {
            row->hl[i] = HL_NUMBER;
            p++; i++;
            prev_sep = 0;
            continue;
        }
        if (prev_sep) {
            int j;
            for (j = 0; keywords[j]; j++) {
                int klen = strlen(keywords[j]);
                int kw2 = (keywords[j][klen - 1] == '|');
                if (kw2) klen--;
                if (!memcmp(p, keywords[j], klen) && is_separator(*(p + klen))) {
                    memset(row->hl + i, kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
                    p += klen; i += klen;
                    break;
                }
            }
            if (keywords[j] != NULL) {
                prev_sep = 0;
                continue;
            }
        }
        prev_sep = is_separator(*p);
        p++; i++;
    }
    int oc = editorRowHasOpenComment(row);
    if (row->hl_oc != oc && row->idx + 1 < E.numrows)
        editorUpdateSyntax(&E.row[row->idx + 1]);
    row->hl_oc = oc;
}

int editorSyntaxToColor(int hl) {
    switch (hl) {
        case HL_COMMENT:
        case HL_MLCOMMENT: return 36; /* cyan */
        case HL_KEYWORD1:  return 33; /* yellow */
        case HL_KEYWORD2:  return 32; /* green */
        case HL_STRING:    return 35; /* magenta */
        case HL_NUMBER:    return 31; /* red */
        case HL_MATCH:     return 34; /* blue */
        default:           return 37; /* white */
    }
}

void editorSelectSyntaxHighlight(char *filename) {
    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        struct editorSyntax *s = HLDB + j;
        for (int i = 0; s->filematch[i]; i++) {
            char *p;
            int patlen = strlen(s->filematch[i]);
            if ((p = strstr(filename, s->filematch[i])) != NULL) {
                if (s->filematch[i][0] != '.' || p[patlen] == '\0') {
                    E.syntax = s;
                    return;
                }
            }
        }
    }
}

/* --------------------- Row Operations --------------------- */

void editorUpdateRow(erow *row) {
    unsigned int tabs = 0;
    int j, idx = 0;
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;
    unsigned long long allocSize = (unsigned long long)row->size + tabs * STE_TAB_STOP + 1;
    if (allocSize > UINT32_MAX) {
        fprintf(stderr, "Line too long for SimpleTextEditor\n");
        exit(1);
    }
    free(row->render);
    row->render = malloc(row->size + tabs * STE_TAB_STOP + 1);
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while ((idx % STE_TAB_STOP) != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->rsize = idx;
    row->render[idx] = '\0';
    editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
    if (at > E.numrows) return;
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    if (at != E.numrows) {
        memmove(E.row + at + 1, E.row + at, sizeof(erow) * (E.numrows - at));
        for (int j = at + 1; j <= E.numrows; j++) E.row[j].idx++;
    }
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.row[at].hl = NULL;
    E.row[at].hl_oc = 0;
    E.row[at].render = NULL;
    E.row[at].rsize = 0;
    E.row[at].idx = at;
    editorUpdateRow(&E.row[at]);
    E.numrows++;
    E.dirty++;
}

void editorFreeRow(erow *row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
}

void editorDelRow(int at) {
    if (at >= E.numrows) return;
    editorFreeRow(&E.row[at]);
    memmove(E.row + at, E.row + at + 1, sizeof(erow) * (E.numrows - at - 1));
    for (int j = at; j < E.numrows - 1; j++) E.row[j].idx--;
    E.numrows--;
    E.dirty++;
}

char *editorRowsToString(int *buflen) {
    int totlen = 0;
    for (int j = 0; j < E.numrows; j++)
        totlen += E.row[j].size + 1;
    *buflen = totlen;
    char *buf = malloc(totlen + 1);
    char *p = buf;
    for (int j = 0; j < E.numrows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }
    *p = '\0';
    return buf;
}

void editorRowInsertChar(erow *row, int at, int c) {
    if (at > row->size) {
        int padlen = at - row->size;
        row->chars = realloc(row->chars, row->size + padlen + 2);
        memset(row->chars + row->size, ' ', padlen);
        row->size += padlen;
        row->chars[row->size] = '\0';
    } else {
        row->chars = realloc(row->chars, row->size + 2);
        memmove(row->chars + at + 1, row->chars + at, row->size - at + 1);
        row->size++;
    }
    row->chars[at] = c;
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(row->chars + row->size, s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
    if (at >= row->size) return;
    memmove(row->chars + at, row->chars + at + 1, row->size - at);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}

/* ------------------- Editor Operations ------------------- */

void editorInsertChar(int c) {
    int filerow = E.rowoff + E.cy;
    int filecol = E.coloff + E.cx;
    erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];
    if (!row) {
        while (E.numrows <= filerow)
            editorInsertRow(E.numrows, "", 0);
        row = &E.row[filerow];
    }
    editorRowInsertChar(row, filecol, c);
    if (E.cx == E.screencols - 1)
        E.coloff++;
    else
        E.cx++;
    E.dirty++;
}

void editorInsertNewline(void) {
    int filerow = E.rowoff + E.cy;
    int filecol = E.coloff + E.cx;
    erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];
    if (!row) {
        if (filerow == E.numrows)
            editorInsertRow(filerow, "", 0);
        goto fixcursor;
    }
    if (filecol >= row->size) filecol = row->size;
    if (filecol == 0) {
        editorInsertRow(filerow, "", 0);
    } else {
        editorInsertRow(filerow + 1, row->chars + filecol, row->size - filecol);
        row->chars[filecol] = '\0';
        row->size = filecol;
        editorUpdateRow(row);
    }
fixcursor:
    if (E.cy == E.screenrows - 1)
        E.rowoff++;
    else
        E.cy++;
    E.cx = 0;
    E.coloff = 0;
}

void editorDelChar(void) {
    int filerow = E.rowoff + E.cy;
    int filecol = E.coloff + E.cx;
    erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];
    if (!row || (filecol == 0 && filerow == 0))
        return;
    if (filecol == 0) {
        int prev_size = E.row[filerow - 1].size;
        editorRowAppendString(&E.row[filerow - 1], row->chars, row->size);
        editorDelRow(filerow);
        if (E.cy == 0)
            E.rowoff--;
        else
            E.cy--;
        E.cx = prev_size;
        if (E.cx >= E.screencols) {
            int diff = E.cx - E.screencols + 1;
            E.cx -= diff;
            E.coloff += diff;
        }
    } else {
        editorRowDelChar(row, filecol - 1);
        if (E.cx == 0 && E.coloff)
            E.coloff--;
        else
            E.cx--;
    }
    E.dirty++;
}

int editorOpen(char *filename) {
    FILE *fp;
    E.dirty = 0;
    free(E.filename);
    E.filename = strdup(filename);
    fp = fopen(filename, "r");
    if (!fp) {
        if (errno != ENOENT) {
            perror("Error opening file");
            exit(1);
        }
        return 1;
    }
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen && (line[linelen - 1] == '\n' ||
                           line[linelen - 1] == '\r'))
            line[--linelen] = '\0';
        editorInsertRow(E.numrows, line, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
    editorSetStatusMessage("%d bytes written to disk", 0);
    return 0;
}

int editorSave(void) {
    int len;
    char *buf = editorRowsToString(&len);
    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if (fd == -1) goto writeerr;
    if (ftruncate(fd, len) == -1) goto writeerr;
    if (write(fd, buf, len) != len) goto writeerr;
    close(fd);
    free(buf);
    E.dirty = 0;
    editorSetStatusMessage("%d bytes written to disk", len);
    return 0;
writeerr:
    free(buf);
    if (fd != -1) close(fd);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
    return 1;
}

/* ------------------- Output Buffer ------------------- */

void abAppend(struct abuf *ab, const char *s, int len) {
    char *newbuf = realloc(ab->b, ab->len + len);
    if (newbuf == NULL) return;
    memcpy(newbuf + ab->len, s, len);
    ab->b = newbuf;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    free(ab->b);
}

/* ------------------- Screen Refresh ------------------- */

void editorRefreshScreen(void) {
    struct abuf ab = ABUF_INIT;
    char buf[32];
    int y;
    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);
    for (y = 0; y < E.screenrows; y++) {
        int filerow = E.rowoff + y;
        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / WELCOME_DIVISOR) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                                          "SimpleTextEditor -- version %s", STE_VERSION);
                int padding = (E.screencols - welcomelen) / 2;
                if (padding) {
                    abAppend(&ab, "~", 1);
                    padding--;
                }
                while (padding--) abAppend(&ab, " ", 1);
                abAppend(&ab, welcome, welcomelen);
            } else {
                abAppend(&ab, "~", 1);
            }
            abAppend(&ab, "\x1b[0K", 4);
            abAppend(&ab, "\r\n", 2);
        } else {
            erow *row = &E.row[filerow];
            int len = row->rsize - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;
            int current_color = -1;
            for (int j = 0; j < len; j++) {
                unsigned char hl = row->hl[j + E.coloff];
                char c = row->render[j + E.coloff];
                if (hl == HL_NONPRINT) {
                    abAppend(&ab, "\x1b[7m", 4);
                    char sym = (c <= 26) ? ('@' + c) : '?';
                    abAppend(&ab, &sym, 1);
                    abAppend(&ab, "\x1b[0m", 4);
                } else if (hl == HL_NORMAL) {
                    if (current_color != -1) {
                        abAppend(&ab, "\x1b[39m", 5);
                        current_color = -1;
                    }
                    abAppend(&ab, &c, 1);
                } else {
                    int color = editorSyntaxToColor(hl);
                    if (color != current_color) {
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        current_color = color;
                        abAppend(&ab, buf, clen);
                    }
                    abAppend(&ab, &c, 1);
                }
            }
            abAppend(&ab, "\x1b[39m", 5);
            abAppend(&ab, "\x1b[0K", 4);
            abAppend(&ab, "\r\n", 2);
        }
    }
    /* Status Bar */
    abAppend(&ab, "\x1b[0K", 4);
    abAppend(&ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int statuslen = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                             E.filename ? E.filename : "[No Name]", E.numrows,
                             E.dirty ? "(modified)" : "");
    int rstatuslen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
                              E.rowoff + E.cy + 1, E.numrows);
    if (statuslen > E.screencols) statuslen = E.screencols;
    abAppend(&ab, status, statuslen);
    while (statuslen < E.screencols) {
        if (E.screencols - statuslen == rstatuslen) {
            abAppend(&ab, rstatus, rstatuslen);
            break;
        } else {
            abAppend(&ab, " ", 1);
            statuslen++;
        }
    }
    abAppend(&ab, "\x1b[0m\r\n", 6);
    /* Status Message */
    abAppend(&ab, "\x1b[0K", 4);
    int msglen = strlen(E.statusmsg);
    if (msglen && time(NULL) - E.statusmsg_time < STATUS_MSG_DURATION)
        abAppend(&ab, E.statusmsg, (msglen < E.screencols ? msglen : E.screencols));
    int cx = 1;
    int filerow = E.rowoff + E.cy;
    if (filerow < E.numrows) {
        erow *row = &E.row[filerow];
        for (int j = E.coloff; j < (E.cx + E.coloff); j++) {
            if (j < row->size && row->chars[j] == '\t')
                cx += (STE_TAB_STOP - (cx % STE_TAB_STOP));
            else
                cx++;
        }
    }
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, cx);
    abAppend(&ab, buf, strlen(buf));
    abAppend(&ab, "\x1b[?25h", 6);
    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, STATUS_MSG_LEN, fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/* ------------------- Search Function ------------------- */

void editorFind(int fd) {
    char query[STE_QUERY_LEN + 1] = {0};
    int qlen = 0;
    int last_match = -1;
    int find_next = 0;
    int saved_hl_line = -1;
    char *saved_hl = NULL;

#define FIND_RESTORE_HL do {                \
    if (saved_hl) {                         \
        memcpy(E.row[saved_hl_line].hl,     \
               saved_hl, E.row[saved_hl_line].rsize); \
        free(saved_hl);                     \
        saved_hl = NULL;                    \
    }                                       \
} while (0)

    int saved_cx = E.cx, saved_cy = E.cy;
    int saved_coloff = E.coloff, saved_rowoff = E.rowoff;

    while (1) {
        editorSetStatusMessage("Search: %s (ESC/Arrows/Enter)", query);
        editorRefreshScreen();
        int c = editorReadKey(fd);
        if (c == DEL_KEY || c == CTRL_H || c == BACKSPACE) {
            if (qlen) query[--qlen] = '\0';
            last_match = -1;
        } else if (c == ESC || c == ENTER) {
            if (c == ESC) {
                E.cx = saved_cx; E.cy = saved_cy;
                E.coloff = saved_coloff; E.rowoff = saved_rowoff;
            }
            FIND_RESTORE_HL;
            editorSetStatusMessage("");
            return;
        } else if (c == ARROW_RIGHT || c == ARROW_DOWN) {
            find_next = 1;
        } else if (c == ARROW_LEFT || c == ARROW_UP) {
            find_next = -1;
        } else if (isprint(c)) {
            if (qlen < STE_QUERY_LEN) {
                query[qlen++] = c;
                query[qlen] = '\0';
                last_match = -1;
            }
        }
        if (last_match == -1) find_next = 1;
        if (find_next) {
            char *match = NULL;
            int match_offset = 0;
            int current = last_match;
            for (int i = 0; i < E.numrows; i++) {
                current += find_next;
                if (current == -1) current = E.numrows - 1;
                else if (current == E.numrows) current = 0;
                match = strstr(E.row[current].render, query);
                if (match) {
                    match_offset = match - E.row[current].render;
                    break;
                }
            }
            find_next = 0;
            FIND_RESTORE_HL;
            if (match) {
                erow *row = &E.row[current];
                last_match = current;
                if (row->hl) {
                    saved_hl_line = current;
                    saved_hl = malloc(row->rsize);
                    memcpy(saved_hl, row->hl, row->rsize);
                    memset(row->hl + match_offset, HL_MATCH, qlen);
                }
                E.cy = 0;
                E.cx = match_offset;
                E.rowoff = current;
                E.coloff = 0;
                if (E.cx > E.screencols) {
                    int diff = E.cx - E.screencols;
                    E.cx -= diff;
                    E.coloff += diff;
                }
            }
        }
    }
}

/* ------------------- Cursor Movement ------------------- */

void editorMoveCursor(int key) {
    int filerow = E.rowoff + E.cy;
    int filecol = E.coloff + E.cx;
    erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];
    switch (key) {
        case ARROW_LEFT:
            if (E.cx == 0) {
                if (E.coloff)
                    E.coloff--;
                else if (filerow > 0) {
                    E.cy--;
                    E.cx = E.row[filerow - 1].size;
                    if (E.cx > E.screencols - 1) {
                        E.coloff = E.cx - E.screencols + 1;
                        E.cx = E.screencols - 1;
                    }
                }
            } else {
                E.cx--;
            }
            break;
        case ARROW_RIGHT:
            if (row && filecol < row->size) {
                if (E.cx == E.screencols - 1)
                    E.coloff++;
                else
                    E.cx++;
            } else if (row && filecol == row->size) {
                E.cx = 0;
                E.coloff = 0;
                if (E.cy == E.screenrows - 1)
                    E.rowoff++;
                else
                    E.cy++;
            }
            break;
        case ARROW_UP:
            if (E.cy == 0) {
                if (E.rowoff) E.rowoff--;
            } else {
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if (filerow < E.numrows) {
                if (E.cy == E.screenrows - 1)
                    E.rowoff++;
                else
                    E.cy++;
            }
            break;
    }
    filerow = E.rowoff + E.cy;
    filecol = E.coloff + E.cx;
    row = (filerow >= E.numrows) ? NULL : &E.row[filerow];
    int rowlen = row ? row->size : 0;
    if (filecol > rowlen) {
        E.cx -= (filecol - rowlen);
        if (E.cx < 0) {
            E.coloff += E.cx;
            E.cx = 0;
        }
    }
}

void editorProcessKeypress(int fd) {
    static int quit_times = STE_QUIT_TIMES;
    int c = editorReadKey(fd);
    if (E.mode == MODE_INSERT) {
        if (c == ESC) {
            E.mode = MODE_NORMAL;
            editorSetStatusMessage("-- NORMAL --");
            return;
        }
        switch (c) {
            case ENTER:
                editorInsertNewline();
                break;
            case CTRL_C:
                break;
            case CTRL_Q:
                if (E.dirty && quit_times) {
                    editorSetStatusMessage("WARNING! File modified. Press Ctrl-Q %d more times to quit.", quit_times);
                    quit_times--;
                    return;
                }
                exit(0);
                break;
            case CTRL_S:
                editorSave();
                break;
            case CTRL_F:
                editorFind(fd);
                break;
            case BACKSPACE:
            case CTRL_H:
            case DEL_KEY:
                editorDelChar();
                break;
            case PAGE_UP:
            case PAGE_DOWN: {
                if (c == PAGE_UP && E.cy != 0)
                    E.cy = 0;
                else if (c == PAGE_DOWN && E.cy != E.screenrows - 1)
                    E.cy = E.screenrows - 1;
                int times = E.screenrows;
                while (times--)
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                break;
            }
            default:
                editorInsertChar(c);
                break;
        }
    } else { /* NORMAL mode */
        switch (c) {
            case 'i':
                E.mode = MODE_INSERT;
                editorSetStatusMessage("-- INSERT --");
                break;
            case 'h':
                editorMoveCursor(ARROW_LEFT);
                break;
            case 'j':
                editorMoveCursor(ARROW_DOWN);
                break;
            case 'k':
                editorMoveCursor(ARROW_UP);
                break;
            case 'l':
                editorMoveCursor(ARROW_RIGHT);
                break;
            case '0': {
                int filerow = E.rowoff + E.cy;
                erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];
                if (row) {
                    E.cx = 0;
                    E.coloff = 0;
                }
            } break;
            case '$': {
                int filerow = E.rowoff + E.cy;
                erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];
                if (row) {
                    E.cx = (row->size > E.screencols ? E.screencols - 1 : row->size);
                    if (row->size > E.screencols)
                        E.coloff = row->size - E.screencols + 1;
                }
            } break;
            case 'x':
                editorDelChar();
                break;
            case CTRL_Q:
                if (E.dirty && quit_times) {
                    editorSetStatusMessage("WARNING! File modified. Press Ctrl-Q %d more times to quit.", quit_times);
                    quit_times--;
                    return;
                }
                exit(0);
                break;
            case CTRL_S:
                editorSave();
                break;
            case CTRL_F:
                editorFind(fd);
                break;
            default:
                break;
        }
    }
    quit_times = STE_QUIT_TIMES;
}

/* ------------------- Window Size and Signal Handling ------------------- */

int editorFileWasModified(void) {
    return E.dirty;
}

void updateWindowSize(void) {
    if (getWindowSize(STDIN_FILENO, STDOUT_FILENO, &E.screenrows, &E.screencols) == -1) {
        perror("Unable to query the screen for size");
        exit(1);
    }
    E.screenrows -= 2; /* For status bar */
}

static void handleSigWinCh(int unused) {
    (void)unused;
    updateWindowSize();
    if (E.cy > E.screenrows) E.cy = E.screenrows - 1;
    if (E.cx > E.screencols) E.cx = E.screencols - 1;
    editorRefreshScreen();
}

/* ------------------- Editor Initialization ------------------- */

void initEditor(void) {
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = NULL;
    E.syntax = NULL;
    E.mode = MODE_NORMAL;
    updateWindowSize();
    signal(SIGWINCH, handleSigWinCh);
}

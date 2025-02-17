#ifndef EDITOR_H
#define EDITOR_H

#ifdef __linux__
#define _POSIX_C_SOURCE 200809L
#endif

#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <signal.h>

/* ------------------------- Key Definitions ------------------------- */
enum KEY_ACTION {
    KEY_NULL = 0,
    CTRL_C = 3,
    CTRL_D = 4,
    CTRL_F = 6,
    CTRL_H = 8,
    TAB = 9,
    CTRL_L = 12,
    ENTER = 13,
    CTRL_Q = 17,
    CTRL_S = 19,
    CTRL_U = 21,
    ESC = 27,
    BACKSPACE = 127,
    /* Extended keys */
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/* ---------------------- Constants and Macros ---------------------- */

#define STE_VERSION "0.0.1"
#define STE_TAB_STOP       8
#define STATUS_MSG_LEN     80
#define STATUS_MSG_DURATION 5    /* seconds */
#define STE_VMIN           0
#define STE_VTIME          1    /* in tenths of a second */
#define STE_QUIT_TIMES     3
#define WELCOME_DIVISOR    3    /* For centering the welcome message */

/* Maximum search query length */
#define STE_QUERY_LEN 256

/* ----------------------- Data Structures ----------------------- */

/* Syntax highlighting types */
enum {
    HL_NORMAL = 0,
    HL_NONPRINT,
    HL_COMMENT,      /* Single-line comment */
    HL_MLCOMMENT,    /* Multi-line comment */
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH        /* Search match */
};

#define HL_HIGHLIGHT_STRINGS (1<<0)
#define HL_HIGHLIGHT_NUMBERS (1<<1)

/* Structure for syntax highlighting configuration */
struct editorSyntax {
    char **filematch;
    char **keywords;
    char singleline_comment_start[3];
    char multiline_comment_start[4];
    char multiline_comment_end[4];
    int flags;
};

/* Represents one line of the file */
typedef struct erow {
    int idx;            /* Row index (0-indexed) */
    int size;           /* Size of the row (without '\0') */
    int rsize;          /* Rendered row size */
    char *chars;        /* Content of the row */
    char *render;       /* Rendered row (tabs expanded) */
    unsigned char *hl;  /* Highlight attributes for each character */
    int hl_oc;          /* Whether the row ends with an open multi-line comment */
} erow;

/* Editor modes */
enum EditorMode {
    MODE_NORMAL,
    MODE_INSERT
};

/* Main editor configuration structure */
struct editorConfig {
    int cx, cy;         /* Cursor x and y position */
    int rowoff;         /* Row offset for vertical scrolling */
    int coloff;         /* Column offset for horizontal scrolling */
    int screenrows;     /* Number of rows available on screen */
    int screencols;     /* Number of columns available on screen */
    int numrows;        /* Number of rows in the file */
    int rawmode;        /* Is terminal raw mode enabled? */
    erow *row;          /* Array of rows */
    int dirty;          /* File modified but not saved flag */
    char *filename;     /* Open file name */
    char statusmsg[STATUS_MSG_LEN]; /* Status message */
    time_t statusmsg_time;
    struct editorSyntax *syntax;  /* Current syntax highlight or NULL */
    enum EditorMode mode;         /* Current editor mode */
};

/* Global editor state – defined in editor.c */
extern struct editorConfig E;

/* Output buffer */
struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT { NULL, 0 }

/* ---------------------- Function Prototypes ---------------------- */

/* Terminal handling */
int enableRawMode(int fd);
void disableRawMode(int fd);
int editorReadKey(int fd);
int getCursorPosition(int ifd, int ofd, int *rows, int *cols);
int getWindowSize(int ifd, int ofd, int *rows, int *cols);

/* Syntax highlighting */
void editorUpdateSyntax(erow *row);
int editorSyntaxToColor(int hl);
void editorSelectSyntaxHighlight(char *filename);

/* Row operations */
void editorUpdateRow(erow *row);
void editorInsertRow(int at, char *s, size_t len);
void editorDelRow(int at);
char *editorRowsToString(int *buflen);
void editorRowInsertChar(erow *row, int at, int c);
void editorRowAppendString(erow *row, char *s, size_t len);
void editorRowDelChar(erow *row, int at);

/* Editor operations */
void editorInsertChar(int c);
void editorInsertNewline(void);
void editorDelChar(void);
int editorOpen(char *filename);
int editorSave(void);

/* Output buffer functions */
void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);
void editorRefreshScreen(void);
void editorSetStatusMessage(const char *fmt, ...);

/* Search functionality */
void editorFind(int fd);

/* Navigation */
void editorMoveCursor(int key);
void editorProcessKeypress(int fd);

/* Editor initialization */
int editorFileWasModified(void);
void updateWindowSize(void);
void initEditor(void);

/* Other */
int is_separator(int c);

#endif  /* EDITOR_H */

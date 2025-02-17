#include "editor.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: SimpleTextEditor <filename>\n");
        exit(1);
    }
    initEditor();
    editorSelectSyntaxHighlight(argv[1]);
    editorOpen(argv[1]);
    if (enableRawMode(STDIN_FILENO) == -1) {
        perror("Unable to enter raw mode");
        exit(1);
    }
    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find | i = insert");
    while (1) {
        editorRefreshScreen();
        editorProcessKeypress(STDIN_FILENO);
    }
    return 0;
}

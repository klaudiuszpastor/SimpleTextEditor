# SimpleTextEditor

**SimpleTextEditor** is a lightweight, modal text editor written in C. Inspired by the original [Kilo](https://github.com/antirez/kilo) editor, this project demonstrates how to implement a simple text editor with features like syntax highlighting, file saving, searching, and basic modal Vim editing (NORMAL/INSERT mode).

## Features

- **Modal Editing**  
  Switch between NORMAL and INSERT modes (similar to Vim). In NORMAL mode you can navigate and perform commands; in INSERT mode, you can add text.

- **File Operations**  
  Open, modify, and save files. A modified file is flagged so the user is warned before quitting.

- **Search Functionality**  
  Incremental search with visual feedback for matching text.

- **Terminal Control**  
  Uses raw mode to capture keystrokes and control terminal output (cursor positioning, status bar, etc.).

- **Minimal Dependencies**  
  Written in standard C (C99), making it portable on most Unix-like systems.

## Build Instructions

### Requirements

- A C compiler (e.g. `gcc`)
- POSIX-compliant system 

### Using the Makefile

In the project directory, simply run:

```bash
make
```

This will compile the source files (`main.c`, `editor.c`, `editor.h`) and produce an executable named `SimpleTextEditor`.

To clean up the build artifacts, run:

```bash
make clean
```

## Usage

Run the editor from the terminal by providing the filename to edit:

```bash
./SimpleTextEditor <filename>
```

If the file does not exist, the editor will open an empty buffer that can be saved later.

### Key Commands

- **Insert Mode:**  
  Press `i` to enter INSERT mode. In INSERT mode, type as you would in any text editor. Press `ESC` to return to NORMAL mode.

- **Normal Mode:**  
  - `h`, `j`, `k`, `l` – move the cursor left, down, up, and right respectively.  
  - `0` – move to the beginning of the line.  
  - `$` – move to the end of the line.  
  - `x` – delete the character under the cursor.  
  - `Ctrl-S` – save the file.  
  - `Ctrl-F` – enter search mode.  
  - `Ctrl-Q` – quit the editor (if the file is modified, you must press it multiple times).

- **Search Mode:**  
  Type the search query; use arrow keys to navigate between matches. Press `Enter` to finish the search or `ESC` to cancel.

## Code Structure

- **editor.h**  
  Contains the data structures, constants, and function prototypes used across the project.

- **editor.c**  
  Implements the editor's functionality including terminal handling, syntax highlighting, file I/O, and command processing.

- **main.c**  
  Contains the `main()` function which initializes the editor, opens the file, and starts the main loop.

- **Makefile**  
  Automates the build process.

## Planned Enhancements

Future updates to SimpleTextEditor may include, but are not limited to:

- **Extended Syntax Highlighting**  
  Adding support for additional languages and more customizable syntax configurations.

- **Multi-File Editing**  
  Implementing features for managing multiple open files or buffers.

- **Improved Search and Replace**  
  Enhancing the search functionality to support replace operations and regular expressions.

- **Undo/Redo Functionality**  
  Adding support for undoing and redoing changes within the editor.

- **Configuration File Support**  
  Allowing users to customize key bindings, syntax colors, and other settings via configuration files.

- **Mouse Support**  
  Introducing basic mouse input handling for cursor positioning and text selection.

- **Additional Editing Commands**  
  Expanding the set of available commands to include more advanced text manipulation features.

## License

This project is provided under a BSD-style license. Please refer to the source code comments for detailed licensing information.

## Acknowledgements

- The original [Kilo editor](https://github.com/antirez/kilo) by Salvatore Sanfilippo served as inspiration for this project.
- Contributions from the open-source community in demonstrating minimalist text editor design in C.

---

Feel free to modify this README to suit any additional information or project-specific details you wish to include.


# miniM
Minimal low-level (nano-like) text editor that lets you work on files; build on and basing on termios and ANSI escape codes (ANSI control sequences)

Editor currently works in insert mode

PROJECT IS STILL WORK IN PROGRESS!!!

## Installation

```
git clone https://github.com/at-eee/miniM.git
gcc main.c -o main.out
```

## Usage

```
./main.out
```

## Early Simple Demos

### Example use

![example_use_showcase](https://github.com/user-attachments/assets/275e6026-09e9-4e61-95b2-78eab9e01a50)

Showcase of all the current editor options (mentioned in the status bar i.e: save, open new, open existing file, quit)

### Adjuststable window size:

![window_size_automatic_adjust_example](https://github.com/user-attachments/assets/e5cfe805-b9a4-4ed0-a63b-ab6d4907b190)

Showcase of the editor program adjusting accordingly to the new terminal window size

## TODO:

- Implementing more dynamic memory allocation in the program
- Switch to a better data structure than the current provisional one for the main editor's text data

# 🌿 Leaf — A Minimal Terminal Text Editor

Leaf is a lightweight, terminal-based text editor written in **C** from scratch. It's inspired by minimal editors (like *kilo*) and is designed as a learning project to demonstrate low-level terminal handling, dynamic text buffers, syntax highlighting, and editor UX fundamentals.

---

## ✨ Highlights

- **Raw terminal mode** using `termios` for low-level input/output
- **Syntax highlighting** for C/C++ (keywords, numbers, strings, comments)
- **Incremental search** with live highlighting and navigation
- **File I/O**: open, edit and save files safely
- **Robust cursor & rendering**: proper tab handling, cursor mapping, and scrolling
- **Status & message bars** that show filename, filetype and cursor position

---

## ▶️ Build & Run

Requirements
- `gcc` or `clang`
- POSIX-compatible terminal (Linux/macOS)

Build
```bash
make
```

Run
```bash
./leaf [filename]
```
If no filename is passed, Leaf starts with an empty buffer.

---

## ⌨️ Keybindings

| Shortcut       | Action |
|----------------|--------|
| `Ctrl-S`       | Save file |
| `Ctrl-X`       | Quit (requires confirmation if unsaved) |
| `Ctrl-F`       | Search (incremental, arrows to navigate) |
| `← ↑ → ↓`      | Move cursor |
| `Home / End`   | Move to line start/end |
| `PgUp / PgDn`  | Scroll by one page |
| `Backspace`/`Del` | Delete character |
| `Enter`        | New line |

---

## 📁 File Structure & Design

Core concepts implemented in `leaf.c`:

- **Terminal handling**: enable/disable raw mode, read keys, query cursor and window size.
- **Text model**: `textRow` arrays representing each file line, dynamically resized.
- **Rendering**: convert rows’ `chars` into `render` (tab expansion), manage `highlight` arrays, and write minimal escape sequences for colored output.
- **Syntax highlighting**: an extensible `syntax` structure with filematch patterns, keywords, and comment delimiters.
- **Editor commands**: inserting/deleting characters and rows, splitting lines, search callbacks, and save workflow.


---

## 🧪 Example Session

```bash
./leaf hello.c
# edit, Ctrl-S to save, Ctrl-F to search, Ctrl-X to quit
```

You can replace the placeholder `hello.c` with any file.

---

## 🧭 Roadmap (Ideas)

- Add undo/redo stack
- Expand syntax database (Python, Rust, JS, etc.)
- Configurable keybindings and settings file
- Better cross-platform support (Windows adapter)
- Mouse support and selection
- Multiple file manipulation

---

## 🧾 License

MIT — feel free to use, modify, and redistribute.

---

## 👤 Author

**Andrei Bodrogean** — Computer Science student passionate about systems programming, algorithms, and building tools from the ground up. This project showcases low-level programming skills, attention to detail, and the drive to learn by doing.

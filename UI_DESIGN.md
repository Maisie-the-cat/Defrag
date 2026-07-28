# MS-DOS Style Defrag UI Design

## Visual Theme
The UI will mimic the classic MS-DOS `defrag.exe` (specifically the Norton Utilities era version included in MS-DOS 6.x).
- **Background**: Solid blue (`COLOR_BLUE`).
- **Text**: White or light gray (`COLOR_WHITE`).
- **Highlights/Borders**: Cyan or bright white.
- **Disk Map Blocks**:
  - Unused/Free: Empty space or a faint dot (`·`).
  - Used/Contiguous: Solid block (`█` or `X`).
  - Fragmented: A different character or color (e.g., red `▒`).
  - Reading/Writing (Active): Blinking or bright yellow/white block.

## Layout Structure

The screen will be divided into three main sections:

1.  **Header (Top)**:
    - Title: "Optimize" or "Defragmenting Drive..."
    - Current path being processed.

2.  **Disk Map (Middle - Main Area)**:
    - A large grid representing the disk or the current file's blocks.
    - Since we are defragmenting file-by-file (or directory-by-directory) rather than at the raw block device level, the "map" will be a stylized representation. We will simulate a disk map that fills up or changes state as files are processed.
    - It will consist of rows and columns of block characters.

3.  **Status & Legend (Bottom)**:
    - **Status Box**: Shows current operation ("Reading...", "Writing...", "Moving..."), percentage complete, and elapsed time.
    - **Legend**: Explains the block symbols (e.g., `█` = Used, `▒` = Fragmented, `W` = Writing).

## Integration with Core Logic

The existing `defrag.cpp` uses a synchronous, blocking approach (`process_path`). To update the UI in real-time, we need to:
1.  Initialize ncurses at the start.
2.  Pass a callback or a reference to a UI manager object into the `Defragmenter` classes.
3.  During the defragmentation process (especially the generic copy method, which takes time), periodically update the UI map and status.
4.  For `ioctl`-based methods (Ext4, Btrfs), which are blocking calls, we might only be able to show "Before" and "After" states, or simulate progress if the file is large. For the generic method, we can update progress based on bytes copied.

## Implementation Details (ncurses)

- `initscr()`, `start_color()`, `cbreak()`, `noecho()`, `curs_set(0)`.
- Define color pairs:
  - `1`: White on Blue (Main background)
  - `2`: Yellow on Blue (Active block/Highlight)
  - `3`: Red on Blue (Fragmented)
  - `4`: Cyan on Blue (Borders/Legend)
- Use `box()` for drawing borders around the map and status areas.
- Use `mvwprintw()` and `waddch()` to draw the map and text.
- Implement a simple loop to simulate the "scanning" and "moving" animations across the grid.

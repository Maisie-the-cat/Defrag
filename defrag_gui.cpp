#include <ncurses.h>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include <thread>
#include <atomic>
#include <algorithm>

// Forward declarations for core defragmentation logic
// These functions will be implemented in defrag_core.cpp
namespace DefragCore {
    long get_fs_type(const std::filesystem::path& path);
    std::string get_fs_name(long type);
    bool defragment_file(const std::filesystem::path& path, void (*progress_callback)(int, int, const std::string&));
}

namespace fs = std::filesystem;

// UI Configuration
#define COLOR_PAIR_DEFAULT 1
#define COLOR_PAIR_HIGHLIGHT 2
#define COLOR_PAIR_FRAGMENTED 3
#define COLOR_PAIR_BORDER 4
#define COLOR_PAIR_ACTIVE 5

#define HEADER_HEIGHT 3
#define STATUS_HEIGHT 5
#define LEGEND_HEIGHT 3

// Global UI state (for simplicity, could be encapsulated in a class)
std::atomic<int> current_progress(0);
std::atomic<int> total_progress(100);
std::atomic<std::string> current_status("Initializing...");
std::atomic<std::string> current_file("");

// Ncurses Windows
WINDOW *header_win, *map_win, *status_win, *legend_win;

void init_ncurses() {
    initscr();
    cbreak();
    noecho();
    curs_set(0); // Hide cursor
    start_color();
    
    // Define color pairs
    init_pair(COLOR_PAIR_DEFAULT, COLOR_WHITE, COLOR_BLUE);
    init_pair(COLOR_PAIR_HIGHLIGHT, COLOR_YELLOW, COLOR_BLUE);
    init_pair(COLOR_PAIR_FRAGMENTED, COLOR_RED, COLOR_BLUE);
    init_pair(COLOR_PAIR_BORDER, COLOR_CYAN, COLOR_BLUE);
    init_pair(COLOR_PAIR_ACTIVE, COLOR_BLACK, COLOR_WHITE);

    bkgd(COLOR_PAIR(COLOR_PAIR_DEFAULT));
    attron(COLOR_PAIR(COLOR_PAIR_DEFAULT));
    clear();
    refresh();
}

void create_windows() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    header_win = newwin(HEADER_HEIGHT, max_x, 0, 0);
    status_win = newwin(STATUS_HEIGHT, max_x, max_y - STATUS_HEIGHT - LEGEND_HEIGHT, 0);
    legend_win = newwin(LEGEND_HEIGHT, max_x, max_y - LEGEND_HEIGHT, 0);
    map_win = newwin(max_y - HEADER_HEIGHT - STATUS_HEIGHT - LEGEND_HEIGHT, max_x, HEADER_HEIGHT, 0);

    wbkgd(header_win, COLOR_PAIR(COLOR_PAIR_DEFAULT));
    wbkgd(map_win, COLOR_PAIR(COLOR_PAIR_DEFAULT));
    wbkgd(status_win, COLOR_PAIR(COLOR_PAIR_DEFAULT));
    wbkgd(legend_win, COLOR_PAIR(COLOR_PAIR_DEFAULT));
}

void draw_header(const std::string& target_path) {
    werase(header_win);
    wattron(header_win, COLOR_PAIR(COLOR_PAIR_BORDER));
    box(header_win, 0, 0);
    wattroff(header_win, COLOR_PAIR(COLOR_PAIR_BORDER));

    wattron(header_win, COLOR_PAIR(COLOR_PAIR_DEFAULT));
    mvwprintw(header_win, 1, 2, "Universal Defragmenter - MS-DOS Style");
    mvwprintw(header_win, 1, COLS - 2 - target_path.length(), target_path.c_str());
    wattroff(header_win, COLOR_PAIR(COLOR_PAIR_DEFAULT));
    wrefresh(header_win);
}

void draw_legend() {
    werase(legend_win);
    wattron(legend_win, COLOR_PAIR(COLOR_PAIR_BORDER));
    box(legend_win, 0, 0);
    wattroff(legend_win, COLOR_PAIR(COLOR_PAIR_BORDER));

    wattron(legend_win, COLOR_PAIR(COLOR_PAIR_DEFAULT));
    mvwprintw(legend_win, 1, 2, "Legend: ");
    wattron(legend_win, COLOR_PAIR(COLOR_PAIR_HIGHLIGHT));
    wprintw(legend_win, "█ Used ");
    wattroff(legend_win, COLOR_PAIR(COLOR_PAIR_HIGHLIGHT));
    wattron(legend_win, COLOR_PAIR(COLOR_PAIR_FRAGMENTED));
    wprintw(legend_win, "▒ Fragmented ");
    wattroff(legend_win, COLOR_PAIR(COLOR_PAIR_FRAGMENTED));
    wattron(legend_win, COLOR_PAIR(COLOR_PAIR_ACTIVE));
    wprintw(legend_win, "█ Active ");
    wattroff(legend_win, COLOR_PAIR(COLOR_PAIR_ACTIVE));
    wattron(legend_win, COLOR_PAIR(COLOR_PAIR_DEFAULT));
    wprintw(legend_win, "· Free");
    wattroff(legend_win, COLOR_PAIR(COLOR_PAIR_DEFAULT));
    wrefresh(legend_win);
}

void draw_map() {
    werase(map_win);
    wattron(map_win, COLOR_PAIR(COLOR_PAIR_BORDER));
    box(map_win, 0, 0);
    wattroff(map_win, COLOR_PAIR(COLOR_PAIR_BORDER));

    int map_height, map_width;
    getmaxyx(map_win, map_height, map_width);

    // Simulate a disk map for demonstration
    int filled_blocks = (current_progress * (map_height - 2) * (map_width - 2)) / total_progress;
    int current_block = 0;

    for (int y = 1; y < map_height - 1; ++y) {
        for (int x = 1; x < map_width - 1; ++x) {
            if (current_block < filled_blocks) {
                wattron(map_win, COLOR_PAIR(COLOR_PAIR_HIGHLIGHT));
                mvwaddch(map_win, y, x, ' '); // Use space for solid block
                wattroff(map_win, COLOR_PAIR(COLOR_PAIR_HIGHLIGHT));
            } else {
                wattron(map_win, COLOR_PAIR(COLOR_PAIR_DEFAULT));
                mvwaddch(map_win, y, x, ACS_BULLET); // Faint dot for free space
                wattroff(map_win, COLOR_PAIR(COLOR_PAIR_DEFAULT));
            }
            current_block++;
        }
    }
    wrefresh(map_win);
}

void draw_status() {
    werase(status_win);
    wattron(status_win, COLOR_PAIR(COLOR_PAIR_BORDER));
    box(status_win, 0, 0);
    wattroff(status_win, COLOR_PAIR(COLOR_PAIR_BORDER));

    wattron(status_win, COLOR_PAIR(COLOR_PAIR_DEFAULT));
    mvwprintw(status_win, 1, 2, "Status: %s", current_status.load().c_str());
    mvwprintw(status_win, 2, 2, "File: %s", current_file.load().c_str());
    mvwprintw(status_win, 3, 2, "Progress: %d%%", current_progress.load());
    wattroff(status_win, COLOR_PAIR(COLOR_PAIR_DEFAULT));
    wrefresh(status_win);
}

void update_ui() {
    draw_map();
    draw_status();
    doupdate(); // Update all windows at once
}

void cleanup_ncurses() {
    delwin(header_win);
    delwin(map_win);
    delwin(status_win);
    delwin(legend_win);
    endwin();
}

// Callback function for defragmentation core to update UI
void progress_callback_ui(int current, int total, const std::string& filename) {
    current_progress.store((current * 100) / total);
    total_progress.store(100); // Always 100 for a single file progress
    current_file.store(filename);
    current_status.store("Defragmenting...");
    update_ui();
}

void process_path_gui(const fs::path& path) {
    if (!fs::exists(path)) {
        current_status.store("Error: Path does not exist.");
        update_ui();
        return;
    }
    
    if (fs::is_directory(path)) {
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (fs::is_regular_file(entry)) {
                process_path_gui(entry.path());
            }
        }
        return;
    }
    
    current_file.store(path.filename().string());
    current_status.store("Analyzing...");
    update_ui();

    long fs_type = DefragCore::get_fs_type(path);
    current_status.store("Processing " + DefragCore::get_fs_name(fs_type) + " file...");
    update_ui();

    DefragCore::defragment_file(path, progress_callback_ui);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <path>" << std::endl;
        return 1;
    }

    init_ncurses();
    create_windows();
    draw_legend();
    draw_header(argv[1]);
    update_ui();

    // Start defragmentation in a separate thread to keep UI responsive
    std::thread defrag_thread(process_path_gui, fs::path(argv[1]));

    // UI loop for updates and user input
    int ch;
    while ((ch = getch()) != 'q' && defrag_thread.joinable()) {
        // Allow UI to update periodically even if no input
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        update_ui();
    }

    if (defrag_thread.joinable()) {
        defrag_thread.join();
    }

    cleanup_ncurses();
    return 0;
}

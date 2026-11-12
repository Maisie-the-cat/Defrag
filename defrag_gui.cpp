#include <ncurses.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace DefragCore {
    long get_fs_type(const std::filesystem::path& path);
    std::string get_fs_name(long type);
    bool defragment_file(const std::filesystem::path& path, void (*progress_callback)(int, int, const std::string&));
}

namespace fs = std::filesystem;
namespace {
constexpr int HEADER_HEIGHT = 3;
constexpr int STATUS_HEIGHT = 5;
constexpr int LEGEND_HEIGHT = 3;
enum ColorPair { DEFAULT_PAIR = 1, HIGHLIGHT_PAIR, FRAGMENTED_PAIR, BORDER_PAIR, ACTIVE_PAIR };

struct UiState {
    std::mutex mutex;
    int progress = 0;
    std::string status = "Initializing...";
    std::string file;
    bool done = false;
} state;

WINDOW *header_win = nullptr, *map_win = nullptr, *status_win = nullptr, *legend_win = nullptr;

void set_status(const std::string& status, const std::string& file = {}) {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.status = status;
    if (!file.empty()) state.file = file;
}

void init_ncurses() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);
    start_color();
    init_pair(DEFAULT_PAIR, COLOR_WHITE, COLOR_BLUE);
    init_pair(HIGHLIGHT_PAIR, COLOR_YELLOW, COLOR_BLUE);
    init_pair(FRAGMENTED_PAIR, COLOR_RED, COLOR_BLUE);
    init_pair(BORDER_PAIR, COLOR_CYAN, COLOR_BLUE);
    init_pair(ACTIVE_PAIR, COLOR_BLACK, COLOR_WHITE);
    bkgd(COLOR_PAIR(DEFAULT_PAIR));
    clear();
}

void create_windows() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    if (max_y < HEADER_HEIGHT + STATUS_HEIGHT + LEGEND_HEIGHT + 3 || max_x < 20) {
        endwin();
        throw std::runtime_error("Terminal too small; resize to at least 20 columns and 14 rows.");
    }
    header_win = newwin(HEADER_HEIGHT, max_x, 0, 0);
    status_win = newwin(STATUS_HEIGHT, max_x, max_y - STATUS_HEIGHT - LEGEND_HEIGHT, 0);
    legend_win = newwin(LEGEND_HEIGHT, max_x, max_y - LEGEND_HEIGHT, 0);
    map_win = newwin(max_y - HEADER_HEIGHT - STATUS_HEIGHT - LEGEND_HEIGHT, max_x, HEADER_HEIGHT, 0);
    for (WINDOW* win : {header_win, map_win, status_win, legend_win}) wbkgd(win, COLOR_PAIR(DEFAULT_PAIR));
}

void draw_header(const std::string& target) {
    werase(header_win);
    wattron(header_win, COLOR_PAIR(BORDER_PAIR));
    box(header_win, 0, 0);
    wattroff(header_win, COLOR_PAIR(BORDER_PAIR));
    const int width = getmaxx(header_win);
    mvwaddnstr(header_win, 1, 2, "Universal Defragmenter", std::max(0, width - 4));
    const int path_x = std::max(2, width - static_cast<int>(target.size()) - 3);
    mvwaddnstr(header_win, 1, path_x, target.c_str(), std::max(0, width - path_x - 2));
}

void draw_legend() {
    werase(legend_win);
    wattron(legend_win, COLOR_PAIR(BORDER_PAIR));
    box(legend_win, 0, 0);
    wattroff(legend_win, COLOR_PAIR(BORDER_PAIR));
    mvwaddstr(legend_win, 1, 2, "Progress map: completed files highlighted; q exits after processing");
}

void draw_map(int progress) {
    werase(map_win);
    wattron(map_win, COLOR_PAIR(BORDER_PAIR));
    box(map_win, 0, 0);
    wattroff(map_win, COLOR_PAIR(BORDER_PAIR));
    const int height = getmaxy(map_win), width = getmaxx(map_win);
    const int usable = std::max(0, (height - 2) * (width - 2));
    const int filled = usable * std::clamp(progress, 0, 100) / 100;
    for (int i = 0; i < usable; ++i) {
        const int y = i / std::max(1, width - 2) + 1;
        const int x = i % std::max(1, width - 2) + 1;
        wattron(map_win, COLOR_PAIR(i < filled ? HIGHLIGHT_PAIR : DEFAULT_PAIR));
        mvwaddch(map_win, y, x, i < filled ? ' ' : '.');
    }
}

void draw_status() {
    UiState snapshot;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        snapshot.progress = state.progress;
        snapshot.status = state.status;
        snapshot.file = state.file;
    }
    werase(status_win);
    wattron(status_win, COLOR_PAIR(BORDER_PAIR));
    box(status_win, 0, 0);
    wattroff(status_win, COLOR_PAIR(BORDER_PAIR));
    const int width = getmaxx(status_win) - 4;
    mvwaddnstr(status_win, 1, 2, ("Status: " + snapshot.status).c_str(), width);
    mvwaddnstr(status_win, 2, 2, ("File: " + snapshot.file).c_str(), width);
    mvwprintw(status_win, 3, 2, "Progress: %d%%", snapshot.progress);
}

void update_ui() {
    int progress;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        progress = state.progress;
    }
    draw_map(progress);
    draw_status();
    draw_legend();
    wnoutrefresh(header_win);
    wnoutrefresh(map_win);
    wnoutrefresh(status_win);
    wnoutrefresh(legend_win);
    doupdate();
}

void cleanup_ncurses() {
    if (header_win) delwin(header_win);
    if (map_win) delwin(map_win);
    if (status_win) delwin(status_win);
    if (legend_win) delwin(legend_win);
    endwin();
}

void progress_callback_ui(int current, int total, const std::string& filename) {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.progress = total > 0 ? std::clamp((current * 100) / total, 0, 100) : 0;
    state.file = filename;
    state.status = "Defragmenting...";
}

void process_path_gui(const fs::path& path) {
    std::error_code ec;
    if (!fs::exists(path, ec) || ec) {
        set_status("Error: path does not exist.", path.string());
        return;
    }
    if (fs::is_directory(path, ec) && !ec) {
        fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, ec), end;
        for (; it != end && !ec; it.increment(ec)) {
            if (it->is_regular_file(ec) && !ec) process_path_gui(it->path());
        }
        if (ec) set_status("Directory scan error: " + ec.message(), path.string());
        return;
    }
    if (!fs::is_regular_file(path, ec) || ec) {
        set_status("Skipping non-regular file.", path.string());
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.progress = 0;
        state.status = "Analyzing...";
        state.file = path.filename().string();
    }
    const std::string fs_name = DefragCore::get_fs_name(DefragCore::get_fs_type(path));
    set_status("Processing " + fs_name + "...", path.filename().string());
    const bool ok = DefragCore::defragment_file(path, progress_callback_ui);
    set_status(ok ? "Completed." : "Failed or unsupported.", path.filename().string());
    if (ok) {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.progress = 100;
    }
}
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <path>\n";
        return 1;
    }
    try {
        init_ncurses();
        create_windows();
        draw_header(argv[1]);
        update_ui();
        std::thread worker([&] {
            try {
                process_path_gui(fs::path(argv[1]));
            } catch (const std::exception& e) {
                set_status(std::string("Error: ") + e.what(), argv[1]);
            }
            std::lock_guard<std::mutex> lock(state.mutex);
            state.done = true;
        });
        bool quit = false;
        while (!quit) {
            int ch = getch();
            if (ch == 'q' || ch == 'Q') quit = true;
            update_ui();
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                if (state.done) quit = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        worker.join();
        update_ui();
        cleanup_ncurses();
        return 0;
    } catch (const std::exception& e) {
        cleanup_ncurses();
        std::cerr << e.what() << '\n';
        return 1;
    }
}

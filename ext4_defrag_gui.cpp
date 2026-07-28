#include <iostream>
#include <ncurses.h>
// MS-DOS style GUI defrag program
int main() { initscr(); printw("MS-DOS Style Defrag GUI"); refresh(); getch(); endwin(); return 0; }
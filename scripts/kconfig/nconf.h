/*
 * Copyright (C) 2008 Nir Tzachar <nir.tzachar@gmail.com?
 * Released under the terms of the GNU GPL v2.0.
 *
 * Derived from menuconfig.
 *
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include <curses.h>
#include <menu.h>
#include <panel.h>
#include <form.h>

#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#include "ncurses.h"

#define max(a, b) ({\
		typeof(a) _a = a;\
		typeof(b) _b = b;\
		_a > _b ? _a : _b; })

#define min(a, b) ({\
		typeof(a) _a = a;\
		typeof(b) _b = b;\
		_a < _b ? _a : _b; })

typedef enum {
	NORMAL = 1,
	MAIN_HEADING,
	MAIN_MENU_BOX,
	MAIN_MENU_FORE,
	MAIN_MENU_BACK,
	MAIN_MENU_GREY,
	MAIN_MENU_HEADING,
	SCROLLWIN_TEXT,
	SCROLLWIN_HEADING,
	SCROLLWIN_BOX,
	DIALOG_TEXT,
	DIALOG_MENU_FORE,
	DIALOG_MENU_BACK,
	DIALOG_BOX,
	INPUT_BOX,
	INPUT_HEADING,
	INPUT_TEXT,
	INPUT_FIELD,
	FUNCTION_TEXT,
	FUNCTION_HIGHLIGHT,
	ATTR_MAX
} attributes_t;
extern attributes_t attributes[];

typedef enum {
	F_HELP = 1,
	F_SYMBOL = 2,
	F_INSTS = 3,
	F_CONF = 4,
	F_BACK = 5,
	F_SAVE = 6,
	F_LOAD = 7,
	F_SEARCH = 8,
	F_EXIT = 9,
} function_key;

void set_colors(void);

/* this changes the windows attributes !!! */
void print_in_middle(WINDOW *win,
		int starty,
		int startx,
		int width,
		const char *string,
		chtype color);
int get_line_length(const char *line);
int get_line_no(const char *text);
const char *get_line(const char *text, int line_no);
void fill_window(WINDOW *win, const char *text);
int btn_dialog(WINDOW *main_window, const char *msg, int btn_num, ...);
int dialog_inputbox(WINDOW *main_window,
		const char *title, const char *prompt,
		const char *init, char *result, int result_len);
void refresh_all_windows(WINDOW *main_window);
void show_scroll_win(WINDOW *main_window,
		const char *title,
		const char *text);

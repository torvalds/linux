/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2008 Nir Tzachar <nir.tzachar@gmail.com>
 *
 * Derived from menuconfig.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ncurses.h>
#include <menu.h>
#include <panel.h>
#include <form.h>

#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#define max(a, b) ({\
		typeof(a) _a = a;\
		typeof(b) _b = b;\
		_a > _b ? _a : _b; })

#define min(a, b) ({\
		typeof(a) _a = a;\
		typeof(b) _b = b;\
		_a < _b ? _a : _b; })

extern int attr_normal;
extern int attr_main_heading;
extern int attr_main_menu_box;
extern int attr_main_menu_fore;
extern int attr_main_menu_back;
extern int attr_main_menu_grey;
extern int attr_main_menu_heading;
extern int attr_scrollwin_text;
extern int attr_scrollwin_heading;
extern int attr_scrollwin_box;
extern int attr_dialog_text;
extern int attr_dialog_menu_fore;
extern int attr_dialog_menu_back;
extern int attr_dialog_box;
extern int attr_input_box;
extern int attr_input_heading;
extern int attr_input_text;
extern int attr_input_field;
extern int attr_function_text;
extern int attr_function_highlight;

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
void print_in_middle(WINDOW *win, int y, int width, const char *str, int attrs);
int get_line_length(const char *line);
int get_line_no(const char *text);
const char *get_line(const char *text, int line_no);
void fill_window(WINDOW *win, const char *text);
int btn_dialog(WINDOW *main_window, const char *msg, int btn_num, ...);
int dialog_inputbox(WINDOW *main_window,
		const char *title, const char *prompt,
		const char *init, char **resultp, int *result_len);
void refresh_all_windows(WINDOW *main_window);
void show_scroll_win(WINDOW *main_window,
		const char *title,
		const char *text);

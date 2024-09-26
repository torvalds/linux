// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2008 Nir Tzachar <nir.tzachar@gmail.com>
 *
 * Derived from menuconfig.
 */
#include <xalloc.h>
#include "nconf.h"
#include "lkc.h"

int attr_normal;
int attr_main_heading;
int attr_main_menu_box;
int attr_main_menu_fore;
int attr_main_menu_back;
int attr_main_menu_grey;
int attr_main_menu_heading;
int attr_scrollwin_text;
int attr_scrollwin_heading;
int attr_scrollwin_box;
int attr_dialog_text;
int attr_dialog_menu_fore;
int attr_dialog_menu_back;
int attr_dialog_box;
int attr_input_box;
int attr_input_heading;
int attr_input_text;
int attr_input_field;
int attr_function_text;
int attr_function_highlight;

#define COLOR_ATTR(_at, _fg, _bg, _hl) \
	{ .attr = &(_at), .has_color = true, .color_fg = _fg, .color_bg = _bg, .highlight = _hl }
#define NO_COLOR_ATTR(_at, _hl) \
	{ .attr = &(_at), .has_color = false, .highlight = _hl }
#define COLOR_DEFAULT		-1

struct nconf_attr_param {
	int *attr;
	bool has_color;
	int color_fg;
	int color_bg;
	int highlight;
};

static const struct nconf_attr_param color_theme_params[] = {
	COLOR_ATTR(attr_normal,			COLOR_DEFAULT,	COLOR_DEFAULT,	A_NORMAL),
	COLOR_ATTR(attr_main_heading,		COLOR_MAGENTA,	COLOR_DEFAULT,	A_BOLD | A_UNDERLINE),
	COLOR_ATTR(attr_main_menu_box,		COLOR_YELLOW,	COLOR_DEFAULT,	A_NORMAL),
	COLOR_ATTR(attr_main_menu_fore,		COLOR_DEFAULT,	COLOR_DEFAULT,	A_REVERSE),
	COLOR_ATTR(attr_main_menu_back,		COLOR_DEFAULT,	COLOR_DEFAULT,	A_NORMAL),
	COLOR_ATTR(attr_main_menu_grey,		COLOR_DEFAULT,	COLOR_DEFAULT,	A_NORMAL),
	COLOR_ATTR(attr_main_menu_heading,	COLOR_GREEN,	COLOR_DEFAULT,	A_BOLD),
	COLOR_ATTR(attr_scrollwin_text,		COLOR_DEFAULT,	COLOR_DEFAULT,	A_NORMAL),
	COLOR_ATTR(attr_scrollwin_heading,	COLOR_GREEN,	COLOR_DEFAULT,	A_BOLD),
	COLOR_ATTR(attr_scrollwin_box,		COLOR_YELLOW,	COLOR_DEFAULT,	A_BOLD),
	COLOR_ATTR(attr_dialog_text,		COLOR_DEFAULT,	COLOR_DEFAULT,	A_BOLD),
	COLOR_ATTR(attr_dialog_menu_fore,	COLOR_RED,	COLOR_DEFAULT,	A_STANDOUT),
	COLOR_ATTR(attr_dialog_menu_back,	COLOR_YELLOW,	COLOR_DEFAULT,	A_NORMAL),
	COLOR_ATTR(attr_dialog_box,		COLOR_YELLOW,	COLOR_DEFAULT,	A_BOLD),
	COLOR_ATTR(attr_input_box,		COLOR_YELLOW,	COLOR_DEFAULT,	A_NORMAL),
	COLOR_ATTR(attr_input_heading,		COLOR_GREEN,	COLOR_DEFAULT,	A_BOLD),
	COLOR_ATTR(attr_input_text,		COLOR_DEFAULT,	COLOR_DEFAULT,	A_NORMAL),
	COLOR_ATTR(attr_input_field,		COLOR_DEFAULT,	COLOR_DEFAULT,	A_UNDERLINE),
	COLOR_ATTR(attr_function_text,		COLOR_YELLOW,	COLOR_DEFAULT,	A_REVERSE),
	COLOR_ATTR(attr_function_highlight,	COLOR_DEFAULT,	COLOR_DEFAULT,	A_BOLD),
	{ /* sentinel */ }
};

static const struct nconf_attr_param no_color_theme_params[] = {
	NO_COLOR_ATTR(attr_normal,		A_NORMAL),
	NO_COLOR_ATTR(attr_main_heading,	A_BOLD | A_UNDERLINE),
	NO_COLOR_ATTR(attr_main_menu_box,	A_NORMAL),
	NO_COLOR_ATTR(attr_main_menu_fore,	A_STANDOUT),
	NO_COLOR_ATTR(attr_main_menu_back,	A_NORMAL),
	NO_COLOR_ATTR(attr_main_menu_grey,	A_NORMAL),
	NO_COLOR_ATTR(attr_main_menu_heading,	A_BOLD),
	NO_COLOR_ATTR(attr_scrollwin_text,	A_NORMAL),
	NO_COLOR_ATTR(attr_scrollwin_heading,	A_BOLD),
	NO_COLOR_ATTR(attr_scrollwin_box,	A_BOLD),
	NO_COLOR_ATTR(attr_dialog_text,		A_NORMAL),
	NO_COLOR_ATTR(attr_dialog_menu_fore,	A_STANDOUT),
	NO_COLOR_ATTR(attr_dialog_menu_back,	A_NORMAL),
	NO_COLOR_ATTR(attr_dialog_box,		A_BOLD),
	NO_COLOR_ATTR(attr_input_box,		A_BOLD),
	NO_COLOR_ATTR(attr_input_heading,	A_BOLD),
	NO_COLOR_ATTR(attr_input_text,		A_NORMAL),
	NO_COLOR_ATTR(attr_input_field,		A_UNDERLINE),
	NO_COLOR_ATTR(attr_function_text,	A_REVERSE),
	NO_COLOR_ATTR(attr_function_highlight,	A_BOLD),
	{ /* sentinel */ }
};

void set_colors(void)
{
	const struct nconf_attr_param *p;
	int pair = 0;

	if (has_colors()) {
		start_color();
		use_default_colors();
		p = color_theme_params;
	} else {
		p = no_color_theme_params;
	}

	for (; p->attr; p++) {
		int attr = p->highlight;

		if (p->has_color) {
			pair++;
			init_pair(pair, p->color_fg, p->color_bg);
			attr |= COLOR_PAIR(pair);
		}

		*p->attr = attr;
	}
}

/* this changes the windows attributes !!! */
void print_in_middle(WINDOW *win, int y, int width, const char *str, int attrs)
{
	wattrset(win, attrs);
	mvwprintw(win, y, (width - strlen(str)) / 2, "%s", str);
}

int get_line_no(const char *text)
{
	int i;
	int total = 1;

	if (!text)
		return 0;

	for (i = 0; text[i] != '\0'; i++)
		if (text[i] == '\n')
			total++;
	return total;
}

const char *get_line(const char *text, int line_no)
{
	int i;
	int lines = 0;

	if (!text)
		return NULL;

	for (i = 0; text[i] != '\0' && lines < line_no; i++)
		if (text[i] == '\n')
			lines++;
	return text+i;
}

int get_line_length(const char *line)
{
	int res = 0;
	while (*line != '\0' && *line != '\n') {
		line++;
		res++;
	}
	return res;
}

/* print all lines to the window. */
void fill_window(WINDOW *win, const char *text)
{
	int x, y;
	int total_lines = get_line_no(text);
	int i;

	getmaxyx(win, y, x);
	/* do not go over end of line */
	total_lines = min(total_lines, y);
	for (i = 0; i < total_lines; i++) {
		char tmp[x+10];
		const char *line = get_line(text, i);
		int len = get_line_length(line);
		strncpy(tmp, line, min(len, x));
		tmp[len] = '\0';
		mvwprintw(win, i, 0, "%s", tmp);
	}
}

/* get the message, and buttons.
 * each button must be a char*
 * return the selected button
 *
 * this dialog is used for 2 different things:
 * 1) show a text box, no buttons.
 * 2) show a dialog, with horizontal buttons
 */
int btn_dialog(WINDOW *main_window, const char *msg, int btn_num, ...)
{
	va_list ap;
	char *btn;
	int btns_width = 0;
	int msg_lines = 0;
	int msg_width = 0;
	int total_width;
	int win_rows = 0;
	WINDOW *win;
	WINDOW *msg_win;
	WINDOW *menu_win;
	MENU *menu;
	ITEM *btns[btn_num+1];
	int i, x, y;
	int res = -1;


	va_start(ap, btn_num);
	for (i = 0; i < btn_num; i++) {
		btn = va_arg(ap, char *);
		btns[i] = new_item(btn, "");
		btns_width += strlen(btn)+1;
	}
	va_end(ap);
	btns[btn_num] = NULL;

	/* find the widest line of msg: */
	msg_lines = get_line_no(msg);
	for (i = 0; i < msg_lines; i++) {
		const char *line = get_line(msg, i);
		int len = get_line_length(line);
		if (msg_width < len)
			msg_width = len;
	}

	total_width = max(msg_width, btns_width);
	/* place dialog in middle of screen */
	y = (getmaxy(stdscr)-(msg_lines+4))/2;
	x = (getmaxx(stdscr)-(total_width+4))/2;


	/* create the windows */
	if (btn_num > 0)
		win_rows = msg_lines+4;
	else
		win_rows = msg_lines+2;

	win = newwin(win_rows, total_width+4, y, x);
	keypad(win, TRUE);
	menu_win = derwin(win, 1, btns_width, win_rows-2,
			1+(total_width+2-btns_width)/2);
	menu = new_menu(btns);
	msg_win = derwin(win, win_rows-2, msg_width, 1,
			1+(total_width+2-msg_width)/2);

	set_menu_fore(menu, attr_dialog_menu_fore);
	set_menu_back(menu, attr_dialog_menu_back);

	wattrset(win, attr_dialog_box);
	box(win, 0, 0);

	/* print message */
	wattrset(msg_win, attr_dialog_text);
	fill_window(msg_win, msg);

	set_menu_win(menu, win);
	set_menu_sub(menu, menu_win);
	set_menu_format(menu, 1, btn_num);
	menu_opts_off(menu, O_SHOWDESC);
	menu_opts_off(menu, O_SHOWMATCH);
	menu_opts_on(menu, O_ONEVALUE);
	menu_opts_on(menu, O_NONCYCLIC);
	set_menu_mark(menu, "");
	post_menu(menu);


	touchwin(win);
	refresh_all_windows(main_window);
	while ((res = wgetch(win))) {
		switch (res) {
		case KEY_LEFT:
			menu_driver(menu, REQ_LEFT_ITEM);
			break;
		case KEY_RIGHT:
			menu_driver(menu, REQ_RIGHT_ITEM);
			break;
		case 10: /* ENTER */
		case 27: /* ESCAPE */
		case ' ':
		case KEY_F(F_BACK):
		case KEY_F(F_EXIT):
			break;
		}
		touchwin(win);
		refresh_all_windows(main_window);

		if (res == 10 || res == ' ') {
			res = item_index(current_item(menu));
			break;
		} else if (res == 27 || res == KEY_F(F_BACK) ||
				res == KEY_F(F_EXIT)) {
			res = KEY_EXIT;
			break;
		}
	}

	unpost_menu(menu);
	free_menu(menu);
	for (i = 0; i < btn_num; i++)
		free_item(btns[i]);

	delwin(win);
	return res;
}

int dialog_inputbox(WINDOW *main_window,
		const char *title, const char *prompt,
		const char *init, char **resultp, int *result_len)
{
	int prompt_lines = 0;
	int prompt_width = 0;
	WINDOW *win;
	WINDOW *prompt_win;
	WINDOW *form_win;
	PANEL *panel;
	int i, x, y, lines, columns, win_lines, win_cols;
	int res = -1;
	int cursor_position = strlen(init);
	int cursor_form_win;
	char *result = *resultp;

	getmaxyx(stdscr, lines, columns);

	if (strlen(init)+1 > *result_len) {
		*result_len = strlen(init)+1;
		*resultp = result = xrealloc(result, *result_len);
	}

	/* find the widest line of msg: */
	prompt_lines = get_line_no(prompt);
	for (i = 0; i < prompt_lines; i++) {
		const char *line = get_line(prompt, i);
		int len = get_line_length(line);
		prompt_width = max(prompt_width, len);
	}

	if (title)
		prompt_width = max(prompt_width, strlen(title));

	win_lines = min(prompt_lines+6, lines-2);
	win_cols = min(prompt_width+7, columns-2);
	prompt_lines = max(win_lines-6, 0);
	prompt_width = max(win_cols-7, 0);

	/* place dialog in middle of screen */
	y = (lines-win_lines)/2;
	x = (columns-win_cols)/2;

	strncpy(result, init, *result_len);

	/* create the windows */
	win = newwin(win_lines, win_cols, y, x);
	prompt_win = derwin(win, prompt_lines+1, prompt_width, 2, 2);
	form_win = derwin(win, 1, prompt_width, prompt_lines+3, 2);
	keypad(form_win, TRUE);

	wattrset(form_win, attr_input_field);

	wattrset(win, attr_input_box);
	box(win, 0, 0);
	wattrset(win, attr_input_heading);
	if (title)
		mvwprintw(win, 0, 3, "%s", title);

	/* print message */
	wattrset(prompt_win, attr_input_text);
	fill_window(prompt_win, prompt);

	mvwprintw(form_win, 0, 0, "%*s", prompt_width, " ");
	cursor_form_win = min(cursor_position, prompt_width-1);
	mvwprintw(form_win, 0, 0, "%s",
		  result + cursor_position-cursor_form_win);

	/* create panels */
	panel = new_panel(win);

	/* show the cursor */
	curs_set(1);

	touchwin(win);
	refresh_all_windows(main_window);
	while ((res = wgetch(form_win))) {
		int len = strlen(result);
		switch (res) {
		case 10: /* ENTER */
		case 27: /* ESCAPE */
		case KEY_F(F_HELP):
		case KEY_F(F_EXIT):
		case KEY_F(F_BACK):
			break;
		case 8:   /* ^H */
		case 127: /* ^? */
		case KEY_BACKSPACE:
			if (cursor_position > 0) {
				memmove(&result[cursor_position-1],
						&result[cursor_position],
						len-cursor_position+1);
				cursor_position--;
				cursor_form_win--;
				len--;
			}
			break;
		case KEY_DC:
			if (cursor_position >= 0 && cursor_position < len) {
				memmove(&result[cursor_position],
						&result[cursor_position+1],
						len-cursor_position+1);
				len--;
			}
			break;
		case KEY_UP:
		case KEY_RIGHT:
			if (cursor_position < len) {
				cursor_position++;
				cursor_form_win++;
			}
			break;
		case KEY_DOWN:
		case KEY_LEFT:
			if (cursor_position > 0) {
				cursor_position--;
				cursor_form_win--;
			}
			break;
		case KEY_HOME:
			cursor_position = 0;
			cursor_form_win = 0;
			break;
		case KEY_END:
			cursor_position = len;
			cursor_form_win = min(cursor_position, prompt_width-1);
			break;
		default:
			if ((isgraph(res) || isspace(res))) {
				/* one for new char, one for '\0' */
				if (len+2 > *result_len) {
					*result_len = len+2;
					*resultp = result = realloc(result,
								*result_len);
				}
				/* insert the char at the proper position */
				memmove(&result[cursor_position+1],
						&result[cursor_position],
						len-cursor_position+1);
				result[cursor_position] = res;
				cursor_position++;
				cursor_form_win++;
				len++;
			} else {
				mvprintw(0, 0, "unknown key: %d\n", res);
			}
			break;
		}
		if (cursor_form_win < 0)
			cursor_form_win = 0;
		else if (cursor_form_win > prompt_width-1)
			cursor_form_win = prompt_width-1;

		wmove(form_win, 0, 0);
		wclrtoeol(form_win);
		mvwprintw(form_win, 0, 0, "%*s", prompt_width, " ");
		mvwprintw(form_win, 0, 0, "%s",
			result + cursor_position-cursor_form_win);
		wmove(form_win, 0, cursor_form_win);
		touchwin(win);
		refresh_all_windows(main_window);

		if (res == 10) {
			res = 0;
			break;
		} else if (res == 27 || res == KEY_F(F_BACK) ||
				res == KEY_F(F_EXIT)) {
			res = KEY_EXIT;
			break;
		} else if (res == KEY_F(F_HELP)) {
			res = 1;
			break;
		}
	}

	/* hide the cursor */
	curs_set(0);
	del_panel(panel);
	delwin(prompt_win);
	delwin(form_win);
	delwin(win);
	return res;
}

/* refresh all windows in the correct order */
void refresh_all_windows(WINDOW *main_window)
{
	update_panels();
	touchwin(main_window);
	refresh();
}

void show_scroll_win(WINDOW *main_window,
		const char *title,
		const char *text)
{
	(void)show_scroll_win_ext(main_window, title, (char *)text, NULL, NULL, NULL, NULL);
}

/* layman's scrollable window... */
int show_scroll_win_ext(WINDOW *main_window, const char *title, char *text,
			int *vscroll, int *hscroll,
			extra_key_cb_fn extra_key_cb, void *data)
{
	int res;
	int total_lines = get_line_no(text);
	int x, y, lines, columns;
	int start_x = 0, start_y = 0;
	int text_lines = 0, text_cols = 0;
	int total_cols = 0;
	int win_cols = 0;
	int win_lines = 0;
	int i = 0;
	WINDOW *win;
	WINDOW *pad;
	PANEL *panel;
	bool done = false;

	if (hscroll)
		start_x = *hscroll;
	if (vscroll)
		start_y = *vscroll;

	getmaxyx(stdscr, lines, columns);

	/* find the widest line of msg: */
	total_lines = get_line_no(text);
	for (i = 0; i < total_lines; i++) {
		const char *line = get_line(text, i);
		int len = get_line_length(line);
		total_cols = max(total_cols, len+2);
	}

	/* create the pad */
	pad = newpad(total_lines+10, total_cols+10);
	wattrset(pad, attr_scrollwin_text);
	fill_window(pad, text);

	win_lines = min(total_lines+4, lines-2);
	win_cols = min(total_cols+2, columns-2);
	text_lines = max(win_lines-4, 0);
	text_cols = max(win_cols-2, 0);

	/* place window in middle of screen */
	y = (lines-win_lines)/2;
	x = (columns-win_cols)/2;

	win = newwin(win_lines, win_cols, y, x);
	keypad(win, TRUE);
	/* show the help in the help window, and show the help panel */
	wattrset(win, attr_scrollwin_box);
	box(win, 0, 0);
	wattrset(win, attr_scrollwin_heading);
	mvwprintw(win, 0, 3, " %s ", title);
	panel = new_panel(win);

	/* handle scrolling */
	while (!done) {
		copywin(pad, win, start_y, start_x, 2, 2, text_lines,
				text_cols, 0);
		print_in_middle(win,
				text_lines+2,
				text_cols,
				"<OK>",
				attr_dialog_menu_fore);
		wrefresh(win);

		res = wgetch(win);
		switch (res) {
		case KEY_NPAGE:
		case ' ':
		case 'd':
			start_y += text_lines-2;
			break;
		case KEY_PPAGE:
		case 'u':
			start_y -= text_lines+2;
			break;
		case KEY_HOME:
			start_y = 0;
			break;
		case KEY_END:
			start_y = total_lines-text_lines;
			break;
		case KEY_DOWN:
		case 'j':
			start_y++;
			break;
		case KEY_UP:
		case 'k':
			start_y--;
			break;
		case KEY_LEFT:
		case 'h':
			start_x--;
			break;
		case KEY_RIGHT:
		case 'l':
			start_x++;
			break;
		default:
			if (extra_key_cb) {
				size_t start = (get_line(text, start_y) - text);
				size_t end = (get_line(text, start_y + text_lines) - text);

				if (extra_key_cb(res, start, end, data)) {
					done = true;
					break;
				}
			}
		}
		if (res == 0 || res == 10 || res == 27 || res == 'q' ||
			res == KEY_F(F_HELP) || res == KEY_F(F_BACK) ||
			res == KEY_F(F_EXIT))
			break;
		if (start_y < 0)
			start_y = 0;
		if (start_y >= total_lines-text_lines)
			start_y = total_lines-text_lines;
		if (start_x < 0)
			start_x = 0;
		if (start_x >= total_cols-text_cols)
			start_x = total_cols-text_cols;
	}

	if (hscroll)
		*hscroll = start_x;
	if (vscroll)
		*vscroll = start_y;
	del_panel(panel);
	delwin(win);
	refresh_all_windows(main_window);
	return res;
}

/*
 * Copyright (C) 2008 Nir Tzachar <nir.tzachar@gmail.com?
 * Released under the terms of the GNU GPL v2.0.
 *
 * Derived from menuconfig.
 *
 */
#include "nconf.h"

/* a list of all the different widgets we use */
attributes_t attributes[ATTR_MAX+1] = {0};

/* available colors:
   COLOR_BLACK   0
   COLOR_RED     1
   COLOR_GREEN   2
   COLOR_YELLOW  3
   COLOR_BLUE    4
   COLOR_MAGENTA 5
   COLOR_CYAN    6
   COLOR_WHITE   7
   */
static void set_normal_colors(void)
{
	init_pair(NORMAL, -1, -1);
	init_pair(MAIN_HEADING, COLOR_MAGENTA, -1);

	/* FORE is for the selected item */
	init_pair(MAIN_MENU_FORE, -1, -1);
	/* BACK for all the rest */
	init_pair(MAIN_MENU_BACK, -1, -1);
	init_pair(MAIN_MENU_GREY, -1, -1);
	init_pair(MAIN_MENU_HEADING, COLOR_GREEN, -1);
	init_pair(MAIN_MENU_BOX, COLOR_YELLOW, -1);

	init_pair(SCROLLWIN_TEXT, -1, -1);
	init_pair(SCROLLWIN_HEADING, COLOR_GREEN, -1);
	init_pair(SCROLLWIN_BOX, COLOR_YELLOW, -1);

	init_pair(DIALOG_TEXT, -1, -1);
	init_pair(DIALOG_BOX, COLOR_YELLOW, -1);
	init_pair(DIALOG_MENU_BACK, COLOR_YELLOW, -1);
	init_pair(DIALOG_MENU_FORE, COLOR_RED, -1);

	init_pair(INPUT_BOX, COLOR_YELLOW, -1);
	init_pair(INPUT_HEADING, COLOR_GREEN, -1);
	init_pair(INPUT_TEXT, -1, -1);
	init_pair(INPUT_FIELD, -1, -1);

	init_pair(FUNCTION_HIGHLIGHT, -1, -1);
	init_pair(FUNCTION_TEXT, COLOR_YELLOW, -1);
}

/* available attributes:
   A_NORMAL        Normal display (no highlight)
   A_STANDOUT      Best highlighting mode of the terminal.
   A_UNDERLINE     Underlining
   A_REVERSE       Reverse video
   A_BLINK         Blinking
   A_DIM           Half bright
   A_BOLD          Extra bright or bold
   A_PROTECT       Protected mode
   A_INVIS         Invisible or blank mode
   A_ALTCHARSET    Alternate character set
   A_CHARTEXT      Bit-mask to extract a character
   COLOR_PAIR(n)   Color-pair number n
   */
static void normal_color_theme(void)
{
	/* automatically add color... */
#define mkattr(name, attr) do { \
attributes[name] = attr | COLOR_PAIR(name); } while (0)
	mkattr(NORMAL, NORMAL);
	mkattr(MAIN_HEADING, A_BOLD | A_UNDERLINE);

	mkattr(MAIN_MENU_FORE, A_REVERSE);
	mkattr(MAIN_MENU_BACK, A_NORMAL);
	mkattr(MAIN_MENU_GREY, A_NORMAL);
	mkattr(MAIN_MENU_HEADING, A_BOLD);
	mkattr(MAIN_MENU_BOX, A_NORMAL);

	mkattr(SCROLLWIN_TEXT, A_NORMAL);
	mkattr(SCROLLWIN_HEADING, A_BOLD);
	mkattr(SCROLLWIN_BOX, A_BOLD);

	mkattr(DIALOG_TEXT, A_BOLD);
	mkattr(DIALOG_BOX, A_BOLD);
	mkattr(DIALOG_MENU_FORE, A_STANDOUT);
	mkattr(DIALOG_MENU_BACK, A_NORMAL);

	mkattr(INPUT_BOX, A_NORMAL);
	mkattr(INPUT_HEADING, A_BOLD);
	mkattr(INPUT_TEXT, A_NORMAL);
	mkattr(INPUT_FIELD, A_UNDERLINE);

	mkattr(FUNCTION_HIGHLIGHT, A_BOLD);
	mkattr(FUNCTION_TEXT, A_REVERSE);
}

static void no_colors_theme(void)
{
	/* automatically add highlight, no color */
#define mkattrn(name, attr) { attributes[name] = attr; }

	mkattrn(NORMAL, NORMAL);
	mkattrn(MAIN_HEADING, A_BOLD | A_UNDERLINE);

	mkattrn(MAIN_MENU_FORE, A_STANDOUT);
	mkattrn(MAIN_MENU_BACK, A_NORMAL);
	mkattrn(MAIN_MENU_GREY, A_NORMAL);
	mkattrn(MAIN_MENU_HEADING, A_BOLD);
	mkattrn(MAIN_MENU_BOX, A_NORMAL);

	mkattrn(SCROLLWIN_TEXT, A_NORMAL);
	mkattrn(SCROLLWIN_HEADING, A_BOLD);
	mkattrn(SCROLLWIN_BOX, A_BOLD);

	mkattrn(DIALOG_TEXT, A_NORMAL);
	mkattrn(DIALOG_BOX, A_BOLD);
	mkattrn(DIALOG_MENU_FORE, A_STANDOUT);
	mkattrn(DIALOG_MENU_BACK, A_NORMAL);

	mkattrn(INPUT_BOX, A_BOLD);
	mkattrn(INPUT_HEADING, A_BOLD);
	mkattrn(INPUT_TEXT, A_NORMAL);
	mkattrn(INPUT_FIELD, A_UNDERLINE);

	mkattrn(FUNCTION_HIGHLIGHT, A_BOLD);
	mkattrn(FUNCTION_TEXT, A_REVERSE);
}

void set_colors()
{
	start_color();
	use_default_colors();
	set_normal_colors();
	if (has_colors()) {
		normal_color_theme();
	} else {
		/* give defaults */
		no_colors_theme();
	}
}


/* this changes the windows attributes !!! */
void print_in_middle(WINDOW *win,
		int starty,
		int startx,
		int width,
		const char *string,
		chtype color)
{      int length, x, y;
	float temp;


	if (win == NULL)
		win = stdscr;
	getyx(win, y, x);
	if (startx != 0)
		x = startx;
	if (starty != 0)
		y = starty;
	if (width == 0)
		width = 80;

	length = strlen(string);
	temp = (width - length) / 2;
	x = startx + (int)temp;
	(void) wattrset(win, color);
	mvwprintw(win, y, x, "%s", string);
	refresh();
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
		return 0;

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

	set_menu_fore(menu, attributes[DIALOG_MENU_FORE]);
	set_menu_back(menu, attributes[DIALOG_MENU_BACK]);

	(void) wattrset(win, attributes[DIALOG_BOX]);
	box(win, 0, 0);

	/* print message */
	(void) wattrset(msg_win, attributes[DIALOG_TEXT]);
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
		*resultp = result = realloc(result, *result_len);
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

	(void) wattrset(form_win, attributes[INPUT_FIELD]);

	(void) wattrset(win, attributes[INPUT_BOX]);
	box(win, 0, 0);
	(void) wattrset(win, attributes[INPUT_HEADING]);
	if (title)
		mvwprintw(win, 0, 3, "%s", title);

	/* print message */
	(void) wattrset(prompt_win, attributes[INPUT_TEXT]);
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
		case 127:
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

/* layman's scrollable window... */
void show_scroll_win(WINDOW *main_window,
		const char *title,
		const char *text)
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
	(void) wattrset(pad, attributes[SCROLLWIN_TEXT]);
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
	(void) wattrset(win, attributes[SCROLLWIN_BOX]);
	box(win, 0, 0);
	(void) wattrset(win, attributes[SCROLLWIN_HEADING]);
	mvwprintw(win, 0, 3, " %s ", title);
	panel = new_panel(win);

	/* handle scrolling */
	do {

		copywin(pad, win, start_y, start_x, 2, 2, text_lines,
				text_cols, 0);
		print_in_middle(win,
				text_lines+2,
				0,
				text_cols,
				"<OK>",
				attributes[DIALOG_MENU_FORE]);
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
		}
		if (res == 10 || res == 27 || res == 'q' ||
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
	} while (res);

	del_panel(panel);
	delwin(win);
	refresh_all_windows(main_window);
}

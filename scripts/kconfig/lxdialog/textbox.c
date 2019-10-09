// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  textbox.c -- implements the text box
 *
 *  ORIGINAL AUTHOR: Savio Lam (lam836@cs.cuhk.hk)
 *  MODIFIED FOR LINUX KERNEL CONFIG BY: William Roadcap (roadcap@cfw.com)
 */

#include "dialog.h"

static void back_lines(int n);
static void print_page(WINDOW *win, int height, int width, update_text_fn
		       update_text, void *data);
static void print_line(WINDOW *win, int row, int width);
static char *get_line(void);
static void print_position(WINDOW * win);

static int hscroll;
static int begin_reached, end_reached, page_length;
static char *buf;
static char *page;

/*
 * refresh window content
 */
static void refresh_text_box(WINDOW *dialog, WINDOW *box, int boxh, int boxw,
			     int cur_y, int cur_x, update_text_fn update_text,
			     void *data)
{
	print_page(box, boxh, boxw, update_text, data);
	print_position(dialog);
	wmove(dialog, cur_y, cur_x);	/* Restore cursor position */
	wrefresh(dialog);
}


/*
 * Display text from a file in a dialog box.
 *
 * keys is a null-terminated array
 * update_text() may not add or remove any '\n' or '\0' in tbuf
 */
int dialog_textbox(const char *title, char *tbuf, int initial_height,
		   int initial_width, int *keys, int *_vscroll, int *_hscroll,
		   update_text_fn update_text, void *data)
{
	int i, x, y, cur_x, cur_y, key = 0;
	int height, width, boxh, boxw;
	WINDOW *dialog, *box;
	bool done = false;

	begin_reached = 1;
	end_reached = 0;
	page_length = 0;
	hscroll = 0;
	buf = tbuf;
	page = buf;	/* page is pointer to start of page to be displayed */

	if (_vscroll && *_vscroll) {
		begin_reached = 0;

		for (i = 0; i < *_vscroll; i++)
			get_line();
	}
	if (_hscroll)
		hscroll = *_hscroll;

do_resize:
	getmaxyx(stdscr, height, width);
	if (height < TEXTBOX_HEIGTH_MIN || width < TEXTBOX_WIDTH_MIN)
		return -ERRDISPLAYTOOSMALL;
	if (initial_height != 0)
		height = initial_height;
	else
		if (height > 4)
			height -= 4;
		else
			height = 0;
	if (initial_width != 0)
		width = initial_width;
	else
		if (width > 5)
			width -= 5;
		else
			width = 0;

	/* center dialog box on screen */
	x = (getmaxx(stdscr) - width) / 2;
	y = (getmaxy(stdscr) - height) / 2;

	draw_shadow(stdscr, y, x, height, width);

	dialog = newwin(height, width, y, x);
	keypad(dialog, TRUE);

	/* Create window for box region, used for scrolling text */
	boxh = height - 4;
	boxw = width - 2;
	box = subwin(dialog, boxh, boxw, y + 1, x + 1);
	wattrset(box, dlg.dialog.atr);
	wbkgdset(box, dlg.dialog.atr & A_COLOR);

	keypad(box, TRUE);

	/* register the new window, along with its borders */
	draw_box(dialog, 0, 0, height, width,
		 dlg.dialog.atr, dlg.border.atr);

	wattrset(dialog, dlg.border.atr);
	mvwaddch(dialog, height - 3, 0, ACS_LTEE);
	for (i = 0; i < width - 2; i++)
		waddch(dialog, ACS_HLINE);
	wattrset(dialog, dlg.dialog.atr);
	wbkgdset(dialog, dlg.dialog.atr & A_COLOR);
	waddch(dialog, ACS_RTEE);

	print_title(dialog, title, width);

	print_button(dialog, " Exit ", height - 2, width / 2 - 4, TRUE);
	wnoutrefresh(dialog);
	getyx(dialog, cur_y, cur_x);	/* Save cursor position */

	/* Print first page of text */
	attr_clear(box, boxh, boxw, dlg.dialog.atr);
	refresh_text_box(dialog, box, boxh, boxw, cur_y, cur_x, update_text,
			 data);

	while (!done) {
		key = wgetch(dialog);
		switch (key) {
		case 'E':	/* Exit */
		case 'e':
		case 'X':
		case 'x':
		case 'q':
		case '\n':
			done = true;
			break;
		case 'g':	/* First page */
		case KEY_HOME:
			if (!begin_reached) {
				begin_reached = 1;
				page = buf;
				refresh_text_box(dialog, box, boxh, boxw,
						 cur_y, cur_x, update_text,
						 data);
			}
			break;
		case 'G':	/* Last page */
		case KEY_END:

			end_reached = 1;
			/* point to last char in buf */
			page = buf + strlen(buf);
			back_lines(boxh);
			refresh_text_box(dialog, box, boxh, boxw, cur_y,
					 cur_x, update_text, data);
			break;
		case 'K':	/* Previous line */
		case 'k':
		case KEY_UP:
			if (begin_reached)
				break;

			back_lines(page_length + 1);
			refresh_text_box(dialog, box, boxh, boxw, cur_y,
					 cur_x, update_text, data);
			break;
		case 'B':	/* Previous page */
		case 'b':
		case 'u':
		case KEY_PPAGE:
			if (begin_reached)
				break;
			back_lines(page_length + boxh);
			refresh_text_box(dialog, box, boxh, boxw, cur_y,
					 cur_x, update_text, data);
			break;
		case 'J':	/* Next line */
		case 'j':
		case KEY_DOWN:
			if (end_reached)
				break;

			back_lines(page_length - 1);
			refresh_text_box(dialog, box, boxh, boxw, cur_y,
					 cur_x, update_text, data);
			break;
		case KEY_NPAGE:	/* Next page */
		case ' ':
		case 'd':
			if (end_reached)
				break;

			begin_reached = 0;
			refresh_text_box(dialog, box, boxh, boxw, cur_y,
					 cur_x, update_text, data);
			break;
		case '0':	/* Beginning of line */
		case 'H':	/* Scroll left */
		case 'h':
		case KEY_LEFT:
			if (hscroll <= 0)
				break;

			if (key == '0')
				hscroll = 0;
			else
				hscroll--;
			/* Reprint current page to scroll horizontally */
			back_lines(page_length);
			refresh_text_box(dialog, box, boxh, boxw, cur_y,
					 cur_x, update_text, data);
			break;
		case 'L':	/* Scroll right */
		case 'l':
		case KEY_RIGHT:
			if (hscroll >= MAX_LEN)
				break;
			hscroll++;
			/* Reprint current page to scroll horizontally */
			back_lines(page_length);
			refresh_text_box(dialog, box, boxh, boxw, cur_y,
					 cur_x, update_text, data);
			break;
		case KEY_ESC:
			if (on_key_esc(dialog) == KEY_ESC)
				done = true;
			break;
		case KEY_RESIZE:
			back_lines(height);
			delwin(box);
			delwin(dialog);
			on_key_resize();
			goto do_resize;
		default:
			for (i = 0; keys[i]; i++) {
				if (key == keys[i]) {
					done = true;
					break;
				}
			}
		}
	}
	delwin(box);
	delwin(dialog);
	if (_vscroll) {
		const char *s;

		s = buf;
		*_vscroll = 0;
		back_lines(page_length);
		while (s < page && (s = strchr(s, '\n'))) {
			(*_vscroll)++;
			s++;
		}
	}
	if (_hscroll)
		*_hscroll = hscroll;
	return key;
}

/*
 * Go back 'n' lines in text. Called by dialog_textbox().
 * 'page' will be updated to point to the desired line in 'buf'.
 */
static void back_lines(int n)
{
	int i;

	begin_reached = 0;
	/* Go back 'n' lines */
	for (i = 0; i < n; i++) {
		if (*page == '\0') {
			if (end_reached) {
				end_reached = 0;
				continue;
			}
		}
		if (page == buf) {
			begin_reached = 1;
			return;
		}
		page--;
		do {
			if (page == buf) {
				begin_reached = 1;
				return;
			}
			page--;
		} while (*page != '\n');
		page++;
	}
}

/*
 * Print a new page of text.
 */
static void print_page(WINDOW *win, int height, int width, update_text_fn
		       update_text, void *data)
{
	int i, passed_end = 0;

	if (update_text) {
		char *end;

		for (i = 0; i < height; i++)
			get_line();
		end = page;
		back_lines(height);
		update_text(buf, page - buf, end - buf, data);
	}

	page_length = 0;
	for (i = 0; i < height; i++) {
		print_line(win, i, width);
		if (!passed_end)
			page_length++;
		if (end_reached && !passed_end)
			passed_end = 1;
	}
	wnoutrefresh(win);
}

/*
 * Print a new line of text.
 */
static void print_line(WINDOW * win, int row, int width)
{
	char *line;

	line = get_line();
	line += MIN(strlen(line), hscroll);	/* Scroll horizontally */
	wmove(win, row, 0);	/* move cursor to correct line */
	waddch(win, ' ');
	waddnstr(win, line, MIN(strlen(line), width - 2));

	/* Clear 'residue' of previous line */
#if OLD_NCURSES
	{
		int x = getcurx(win);
		int i;
		for (i = 0; i < width - x; i++)
			waddch(win, ' ');
	}
#else
	wclrtoeol(win);
#endif
}

/*
 * Return current line of text. Called by dialog_textbox() and print_line().
 * 'page' should point to start of current line before calling, and will be
 * updated to point to start of next line.
 */
static char *get_line(void)
{
	int i = 0;
	static char line[MAX_LEN + 1];

	end_reached = 0;
	while (*page != '\n') {
		if (*page == '\0') {
			end_reached = 1;
			break;
		} else if (i < MAX_LEN)
			line[i++] = *(page++);
		else {
			/* Truncate lines longer than MAX_LEN characters */
			if (i == MAX_LEN)
				line[i++] = '\0';
			page++;
		}
	}
	if (i <= MAX_LEN)
		line[i] = '\0';
	if (!end_reached)
		page++;		/* move past '\n' */

	return line;
}

/*
 * Print current position
 */
static void print_position(WINDOW * win)
{
	int percent;

	wattrset(win, dlg.position_indicator.atr);
	wbkgdset(win, dlg.position_indicator.atr & A_COLOR);
	percent = (page - buf) * 100 / strlen(buf);
	wmove(win, getmaxy(win) - 3, getmaxx(win) - 9);
	wprintw(win, "(%3d%%)", percent);
}

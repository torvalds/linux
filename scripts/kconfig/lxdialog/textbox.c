/*
 *  textbox.c -- implements the text box
 *
 *  ORIGINAL AUTHOR: Savio Lam (lam836@cs.cuhk.hk)
 *  MODIFIED FOR LINUX KERNEL CONFIG BY: William Roadcap (roadcap@cfw.com)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "dialog.h"

static void back_lines(int n);
static void print_page(WINDOW * win, int height, int width);
static void print_line(WINDOW * win, int row, int width);
static char *get_line(void);
static void print_position(WINDOW * win, int height, int width);

static int hscroll;
static int begin_reached, end_reached, page_length;
static const char *buf;
static const char *page;

/*
 * Display text from a file in a dialog box.
 */
int dialog_textbox(const char *title, const char *tbuf, int height, int width)
{
	int i, x, y, cur_x, cur_y, key = 0;
	int texth, textw;
	int passed_end;
	WINDOW *dialog, *text;

	begin_reached = 1;
	end_reached = 0;
	page_length = 0;
	hscroll = 0;
	buf = tbuf;
	page = buf;	/* page is pointer to start of page to be displayed */

	/* center dialog box on screen */
	x = (COLS - width) / 2;
	y = (LINES - height) / 2;

	draw_shadow(stdscr, y, x, height, width);

	dialog = newwin(height, width, y, x);
	keypad(dialog, TRUE);

	/* Create window for text region, used for scrolling text */
	texth = height - 4;
	textw = width - 2;
	text = subwin(dialog, texth, textw, y + 1, x + 1);
	wattrset(text, dlg.dialog.atr);
	wbkgdset(text, dlg.dialog.atr & A_COLOR);

	keypad(text, TRUE);

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
	attr_clear(text, texth, textw, dlg.dialog.atr);
	print_page(text, texth, textw);
	print_position(dialog, height, width);
	wmove(dialog, cur_y, cur_x);	/* Restore cursor position */
	wrefresh(dialog);

	while ((key != KEY_ESC) && (key != '\n')) {
		key = wgetch(dialog);
		switch (key) {
		case 'E':	/* Exit */
		case 'e':
		case 'X':
		case 'x':
			delwin(text);
			delwin(dialog);
			return 0;
		case 'g':	/* First page */
		case KEY_HOME:
			if (!begin_reached) {
				begin_reached = 1;
				page = buf;
				print_page(text, texth, textw);
				print_position(dialog, height, width);
				wmove(dialog, cur_y, cur_x);	/* Restore cursor position */
				wrefresh(dialog);
			}
			break;
		case 'G':	/* Last page */
		case KEY_END:

			end_reached = 1;
			/* point to last char in buf */
			page = buf + strlen(buf);
			back_lines(texth);
			print_page(text, texth, textw);
			print_position(dialog, height, width);
			wmove(dialog, cur_y, cur_x);	/* Restore cursor position */
			wrefresh(dialog);
			break;
		case 'K':	/* Previous line */
		case 'k':
		case KEY_UP:
			if (!begin_reached) {
				back_lines(page_length + 1);

				/* We don't call print_page() here but use
				 * scrolling to ensure faster screen update.
				 * However, 'end_reached' and 'page_length'
				 * should still be updated, and 'page' should
				 * point to start of next page. This is done
				 * by calling get_line() in the following
				 * 'for' loop. */
				scrollok(text, TRUE);
				wscrl(text, -1);	/* Scroll text region down one line */
				scrollok(text, FALSE);
				page_length = 0;
				passed_end = 0;
				for (i = 0; i < texth; i++) {
					if (!i) {
						/* print first line of page */
						print_line(text, 0, textw);
						wnoutrefresh(text);
					} else
						/* Called to update 'end_reached' and 'page' */
						get_line();
					if (!passed_end)
						page_length++;
					if (end_reached && !passed_end)
						passed_end = 1;
				}

				print_position(dialog, height, width);
				wmove(dialog, cur_y, cur_x);	/* Restore cursor position */
				wrefresh(dialog);
			}
			break;
		case 'B':	/* Previous page */
		case 'b':
		case KEY_PPAGE:
			if (begin_reached)
				break;
			back_lines(page_length + texth);
			print_page(text, texth, textw);
			print_position(dialog, height, width);
			wmove(dialog, cur_y, cur_x);
			wrefresh(dialog);
			break;
		case 'J':	/* Next line */
		case 'j':
		case KEY_DOWN:
			if (!end_reached) {
				begin_reached = 0;
				scrollok(text, TRUE);
				scroll(text);	/* Scroll text region up one line */
				scrollok(text, FALSE);
				print_line(text, texth - 1, textw);
				wnoutrefresh(text);
				print_position(dialog, height, width);
				wmove(dialog, cur_y, cur_x);	/* Restore cursor position */
				wrefresh(dialog);
			}
			break;
		case KEY_NPAGE:	/* Next page */
		case ' ':
			if (end_reached)
				break;

			begin_reached = 0;
			print_page(text, texth, textw);
			print_position(dialog, height, width);
			wmove(dialog, cur_y, cur_x);
			wrefresh(dialog);
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
			print_page(text, texth, textw);
			wmove(dialog, cur_y, cur_x);
			wrefresh(dialog);
			break;
		case 'L':	/* Scroll right */
		case 'l':
		case KEY_RIGHT:
			if (hscroll >= MAX_LEN)
				break;
			hscroll++;
			/* Reprint current page to scroll horizontally */
			back_lines(page_length);
			print_page(text, texth, textw);
			wmove(dialog, cur_y, cur_x);
			wrefresh(dialog);
			break;
		case KEY_ESC:
			key = on_key_esc(dialog);
			break;
		}
	}
	delwin(text);
	delwin(dialog);
	return key;		/* ESC pressed */
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
 * Print a new page of text. Called by dialog_textbox().
 */
static void print_page(WINDOW * win, int height, int width)
{
	int i, passed_end = 0;

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
 * Print a new line of text. Called by dialog_textbox() and print_page().
 */
static void print_line(WINDOW * win, int row, int width)
{
	int y, x;
	char *line;

	line = get_line();
	line += MIN(strlen(line), hscroll);	/* Scroll horizontally */
	wmove(win, row, 0);	/* move cursor to correct line */
	waddch(win, ' ');
	waddnstr(win, line, MIN(strlen(line), width - 2));

	getyx(win, y, x);
	/* Clear 'residue' of previous line */
#if OLD_NCURSES
	{
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
			if (!end_reached) {
				end_reached = 1;
				break;
			}
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
		page++;		/* move pass '\n' */

	return line;
}

/*
 * Print current position
 */
static void print_position(WINDOW * win, int height, int width)
{
	int percent;

	wattrset(win, dlg.position_indicator.atr);
	wbkgdset(win, dlg.position_indicator.atr & A_COLOR);
	percent = (page - buf) * 100 / strlen(buf);
	wmove(win, height - 3, width - 9);
	wprintw(win, "(%3d%%)", percent);
}

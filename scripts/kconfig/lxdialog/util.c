/*
 *  util.c
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

#include <stdarg.h>

#include "dialog.h"

/* Needed in signal handler in mconf.c */
int saved_x, saved_y;

struct dialog_info dlg;

static void set_mono_theme(void)
{
	dlg.screen.atr = A_NORMAL;
	dlg.shadow.atr = A_NORMAL;
	dlg.dialog.atr = A_NORMAL;
	dlg.title.atr = A_BOLD;
	dlg.border.atr = A_NORMAL;
	dlg.button_active.atr = A_REVERSE;
	dlg.button_inactive.atr = A_DIM;
	dlg.button_key_active.atr = A_REVERSE;
	dlg.button_key_inactive.atr = A_BOLD;
	dlg.button_label_active.atr = A_REVERSE;
	dlg.button_label_inactive.atr = A_NORMAL;
	dlg.inputbox.atr = A_NORMAL;
	dlg.inputbox_border.atr = A_NORMAL;
	dlg.searchbox.atr = A_NORMAL;
	dlg.searchbox_title.atr = A_BOLD;
	dlg.searchbox_border.atr = A_NORMAL;
	dlg.position_indicator.atr = A_BOLD;
	dlg.menubox.atr = A_NORMAL;
	dlg.menubox_border.atr = A_NORMAL;
	dlg.item.atr = A_NORMAL;
	dlg.item_selected.atr = A_REVERSE;
	dlg.tag.atr = A_BOLD;
	dlg.tag_selected.atr = A_REVERSE;
	dlg.tag_key.atr = A_BOLD;
	dlg.tag_key_selected.atr = A_REVERSE;
	dlg.check.atr = A_BOLD;
	dlg.check_selected.atr = A_REVERSE;
	dlg.uarrow.atr = A_BOLD;
	dlg.darrow.atr = A_BOLD;
}

#define DLG_COLOR(dialog, f, b, h) \
do {                               \
	dlg.dialog.fg = (f);       \
	dlg.dialog.bg = (b);       \
	dlg.dialog.hl = (h);       \
} while (0)

static void set_classic_theme(void)
{
	DLG_COLOR(screen,                COLOR_CYAN,   COLOR_BLUE,   true);
	DLG_COLOR(shadow,                COLOR_BLACK,  COLOR_BLACK,  true);
	DLG_COLOR(dialog,                COLOR_BLACK,  COLOR_WHITE,  false);
	DLG_COLOR(title,                 COLOR_YELLOW, COLOR_WHITE,  true);
	DLG_COLOR(border,                COLOR_WHITE,  COLOR_WHITE,  true);
	DLG_COLOR(button_active,         COLOR_WHITE,  COLOR_BLUE,   true);
	DLG_COLOR(button_inactive,       COLOR_BLACK,  COLOR_WHITE,  false);
	DLG_COLOR(button_key_active,     COLOR_WHITE,  COLOR_BLUE,   true);
	DLG_COLOR(button_key_inactive,   COLOR_RED,    COLOR_WHITE,  false);
	DLG_COLOR(button_label_active,   COLOR_YELLOW, COLOR_BLUE,   true);
	DLG_COLOR(button_label_inactive, COLOR_BLACK,  COLOR_WHITE,  true);
	DLG_COLOR(inputbox,              COLOR_BLACK,  COLOR_WHITE,  false);
	DLG_COLOR(inputbox_border,       COLOR_BLACK,  COLOR_WHITE,  false);
	DLG_COLOR(searchbox,             COLOR_BLACK,  COLOR_WHITE,  false);
	DLG_COLOR(searchbox_title,       COLOR_YELLOW, COLOR_WHITE,  true);
	DLG_COLOR(searchbox_border,      COLOR_WHITE,  COLOR_WHITE,  true);
	DLG_COLOR(position_indicator,    COLOR_YELLOW, COLOR_WHITE,  true);
	DLG_COLOR(menubox,               COLOR_BLACK,  COLOR_WHITE,  false);
	DLG_COLOR(menubox_border,        COLOR_WHITE,  COLOR_WHITE,  true);
	DLG_COLOR(item,                  COLOR_BLACK,  COLOR_WHITE,  false);
	DLG_COLOR(item_selected,         COLOR_WHITE,  COLOR_BLUE,   true);
	DLG_COLOR(tag,                   COLOR_YELLOW, COLOR_WHITE,  true);
	DLG_COLOR(tag_selected,          COLOR_YELLOW, COLOR_BLUE,   true);
	DLG_COLOR(tag_key,               COLOR_YELLOW, COLOR_WHITE,  true);
	DLG_COLOR(tag_key_selected,      COLOR_YELLOW, COLOR_BLUE,   true);
	DLG_COLOR(check,                 COLOR_BLACK,  COLOR_WHITE,  false);
	DLG_COLOR(check_selected,        COLOR_WHITE,  COLOR_BLUE,   true);
	DLG_COLOR(uarrow,                COLOR_GREEN,  COLOR_WHITE,  true);
	DLG_COLOR(darrow,                COLOR_GREEN,  COLOR_WHITE,  true);
}

static void set_blackbg_theme(void)
{
	DLG_COLOR(screen, COLOR_RED,   COLOR_BLACK, true);
	DLG_COLOR(shadow, COLOR_BLACK, COLOR_BLACK, false);
	DLG_COLOR(dialog, COLOR_WHITE, COLOR_BLACK, false);
	DLG_COLOR(title,  COLOR_RED,   COLOR_BLACK, false);
	DLG_COLOR(border, COLOR_BLACK, COLOR_BLACK, true);

	DLG_COLOR(button_active,         COLOR_YELLOW, COLOR_RED,   false);
	DLG_COLOR(button_inactive,       COLOR_YELLOW, COLOR_BLACK, false);
	DLG_COLOR(button_key_active,     COLOR_YELLOW, COLOR_RED,   true);
	DLG_COLOR(button_key_inactive,   COLOR_RED,    COLOR_BLACK, false);
	DLG_COLOR(button_label_active,   COLOR_WHITE,  COLOR_RED,   false);
	DLG_COLOR(button_label_inactive, COLOR_BLACK,  COLOR_BLACK, true);

	DLG_COLOR(inputbox,         COLOR_YELLOW, COLOR_BLACK, false);
	DLG_COLOR(inputbox_border,  COLOR_YELLOW, COLOR_BLACK, false);

	DLG_COLOR(searchbox,        COLOR_YELLOW, COLOR_BLACK, false);
	DLG_COLOR(searchbox_title,  COLOR_YELLOW, COLOR_BLACK, true);
	DLG_COLOR(searchbox_border, COLOR_BLACK,  COLOR_BLACK, true);

	DLG_COLOR(position_indicator, COLOR_RED, COLOR_BLACK,  false);

	DLG_COLOR(menubox,          COLOR_YELLOW, COLOR_BLACK, false);
	DLG_COLOR(menubox_border,   COLOR_BLACK,  COLOR_BLACK, true);

	DLG_COLOR(item,             COLOR_WHITE, COLOR_BLACK, false);
	DLG_COLOR(item_selected,    COLOR_WHITE, COLOR_RED,   false);

	DLG_COLOR(tag,              COLOR_RED,    COLOR_BLACK, false);
	DLG_COLOR(tag_selected,     COLOR_YELLOW, COLOR_RED,   true);
	DLG_COLOR(tag_key,          COLOR_RED,    COLOR_BLACK, false);
	DLG_COLOR(tag_key_selected, COLOR_YELLOW, COLOR_RED,   true);

	DLG_COLOR(check,            COLOR_YELLOW, COLOR_BLACK, false);
	DLG_COLOR(check_selected,   COLOR_YELLOW, COLOR_RED,   true);

	DLG_COLOR(uarrow, COLOR_RED, COLOR_BLACK, false);
	DLG_COLOR(darrow, COLOR_RED, COLOR_BLACK, false);
}

static void set_bluetitle_theme(void)
{
	set_classic_theme();
	DLG_COLOR(title,               COLOR_BLUE,   COLOR_WHITE, true);
	DLG_COLOR(button_key_active,   COLOR_YELLOW, COLOR_BLUE,  true);
	DLG_COLOR(button_label_active, COLOR_WHITE,  COLOR_BLUE,  true);
	DLG_COLOR(searchbox_title,     COLOR_BLUE,   COLOR_WHITE, true);
	DLG_COLOR(position_indicator,  COLOR_BLUE,   COLOR_WHITE, true);
	DLG_COLOR(tag,                 COLOR_BLUE,   COLOR_WHITE, true);
	DLG_COLOR(tag_key,             COLOR_BLUE,   COLOR_WHITE, true);

}

/*
 * Select color theme
 */
static int set_theme(const char *theme)
{
	int use_color = 1;
	if (!theme)
		set_bluetitle_theme();
	else if (strcmp(theme, "classic") == 0)
		set_classic_theme();
	else if (strcmp(theme, "bluetitle") == 0)
		set_bluetitle_theme();
	else if (strcmp(theme, "blackbg") == 0)
		set_blackbg_theme();
	else if (strcmp(theme, "mono") == 0)
		use_color = 0;

	return use_color;
}

static void init_one_color(struct dialog_color *color)
{
	static int pair = 0;

	pair++;
	init_pair(pair, color->fg, color->bg);
	if (color->hl)
		color->atr = A_BOLD | COLOR_PAIR(pair);
	else
		color->atr = COLOR_PAIR(pair);
}

static void init_dialog_colors(void)
{
	init_one_color(&dlg.screen);
	init_one_color(&dlg.shadow);
	init_one_color(&dlg.dialog);
	init_one_color(&dlg.title);
	init_one_color(&dlg.border);
	init_one_color(&dlg.button_active);
	init_one_color(&dlg.button_inactive);
	init_one_color(&dlg.button_key_active);
	init_one_color(&dlg.button_key_inactive);
	init_one_color(&dlg.button_label_active);
	init_one_color(&dlg.button_label_inactive);
	init_one_color(&dlg.inputbox);
	init_one_color(&dlg.inputbox_border);
	init_one_color(&dlg.searchbox);
	init_one_color(&dlg.searchbox_title);
	init_one_color(&dlg.searchbox_border);
	init_one_color(&dlg.position_indicator);
	init_one_color(&dlg.menubox);
	init_one_color(&dlg.menubox_border);
	init_one_color(&dlg.item);
	init_one_color(&dlg.item_selected);
	init_one_color(&dlg.tag);
	init_one_color(&dlg.tag_selected);
	init_one_color(&dlg.tag_key);
	init_one_color(&dlg.tag_key_selected);
	init_one_color(&dlg.check);
	init_one_color(&dlg.check_selected);
	init_one_color(&dlg.uarrow);
	init_one_color(&dlg.darrow);
}

/*
 * Setup for color display
 */
static void color_setup(const char *theme)
{
	int use_color;

	use_color = set_theme(theme);
	if (use_color && has_colors()) {
		start_color();
		init_dialog_colors();
	} else
		set_mono_theme();
}

/*
 * Set window to attribute 'attr'
 */
void attr_clear(WINDOW * win, int height, int width, chtype attr)
{
	int i, j;

	wattrset(win, attr);
	for (i = 0; i < height; i++) {
		wmove(win, i, 0);
		for (j = 0; j < width; j++)
			waddch(win, ' ');
	}
	touchwin(win);
}

void dialog_clear(void)
{
	int lines, columns;

	lines = getmaxy(stdscr);
	columns = getmaxx(stdscr);

	attr_clear(stdscr, lines, columns, dlg.screen.atr);
	/* Display background title if it exists ... - SLH */
	if (dlg.backtitle != NULL) {
		int i, len = 0, skip = 0;
		struct subtitle_list *pos;

		wattrset(stdscr, dlg.screen.atr);
		mvwaddstr(stdscr, 0, 1, (char *)dlg.backtitle);

		for (pos = dlg.subtitles; pos != NULL; pos = pos->next) {
			/* 3 is for the arrow and spaces */
			len += strlen(pos->text) + 3;
		}

		wmove(stdscr, 1, 1);
		if (len > columns - 2) {
			const char *ellipsis = "[...] ";
			waddstr(stdscr, ellipsis);
			skip = len - (columns - 2 - strlen(ellipsis));
		}

		for (pos = dlg.subtitles; pos != NULL; pos = pos->next) {
			if (skip == 0)
				waddch(stdscr, ACS_RARROW);
			else
				skip--;

			if (skip == 0)
				waddch(stdscr, ' ');
			else
				skip--;

			if (skip < strlen(pos->text)) {
				waddstr(stdscr, pos->text + skip);
				skip = 0;
			} else
				skip -= strlen(pos->text);

			if (skip == 0)
				waddch(stdscr, ' ');
			else
				skip--;
		}

		for (i = len + 1; i < columns - 1; i++)
			waddch(stdscr, ACS_HLINE);
	}
	wnoutrefresh(stdscr);
}

/*
 * Do some initialization for dialog
 */
int init_dialog(const char *backtitle)
{
	int height, width;

	initscr();		/* Init curses */

	/* Get current cursor position for signal handler in mconf.c */
	getyx(stdscr, saved_y, saved_x);

	getmaxyx(stdscr, height, width);
	if (height < WINDOW_HEIGTH_MIN || width < WINDOW_WIDTH_MIN) {
		endwin();
		return -ERRDISPLAYTOOSMALL;
	}

	dlg.backtitle = backtitle;
	color_setup(getenv("MENUCONFIG_COLOR"));

	keypad(stdscr, TRUE);
	cbreak();
	noecho();
	dialog_clear();

	return 0;
}

void set_dialog_backtitle(const char *backtitle)
{
	dlg.backtitle = backtitle;
}

void set_dialog_subtitles(struct subtitle_list *subtitles)
{
	dlg.subtitles = subtitles;
}

/*
 * End using dialog functions.
 */
void end_dialog(int x, int y)
{
	/* move cursor back to original position */
	move(y, x);
	refresh();
	endwin();
}

/* Print the title of the dialog. Center the title and truncate
 * tile if wider than dialog (- 2 chars).
 **/
void print_title(WINDOW *dialog, const char *title, int width)
{
	if (title) {
		int tlen = MIN(width - 2, strlen(title));
		wattrset(dialog, dlg.title.atr);
		mvwaddch(dialog, 0, (width - tlen) / 2 - 1, ' ');
		mvwaddnstr(dialog, 0, (width - tlen)/2, title, tlen);
		waddch(dialog, ' ');
	}
}

/*
 * Print a string of text in a window, automatically wrap around to the
 * next line if the string is too long to fit on one line. Newline
 * characters '\n' are propperly processed.  We start on a new line
 * if there is no room for at least 4 nonblanks following a double-space.
 */
void print_autowrap(WINDOW * win, const char *prompt, int width, int y, int x)
{
	int newl, cur_x, cur_y;
	int prompt_len, room, wlen;
	char tempstr[MAX_LEN + 1], *word, *sp, *sp2, *newline_separator = 0;

	strcpy(tempstr, prompt);

	prompt_len = strlen(tempstr);

	if (prompt_len <= width - x * 2) {	/* If prompt is short */
		wmove(win, y, (width - prompt_len) / 2);
		waddstr(win, tempstr);
	} else {
		cur_x = x;
		cur_y = y;
		newl = 1;
		word = tempstr;
		while (word && *word) {
			sp = strpbrk(word, "\n ");
			if (sp && *sp == '\n')
				newline_separator = sp;

			if (sp)
				*sp++ = 0;

			/* Wrap to next line if either the word does not fit,
			   or it is the first word of a new sentence, and it is
			   short, and the next word does not fit. */
			room = width - cur_x;
			wlen = strlen(word);
			if (wlen > room ||
			    (newl && wlen < 4 && sp
			     && wlen + 1 + strlen(sp) > room
			     && (!(sp2 = strpbrk(sp, "\n "))
				 || wlen + 1 + (sp2 - sp) > room))) {
				cur_y++;
				cur_x = x;
			}
			wmove(win, cur_y, cur_x);
			waddstr(win, word);
			getyx(win, cur_y, cur_x);

			/* Move to the next line if the word separator was a newline */
			if (newline_separator) {
				cur_y++;
				cur_x = x;
				newline_separator = 0;
			} else
				cur_x++;

			if (sp && *sp == ' ') {
				cur_x++;	/* double space */
				while (*++sp == ' ') ;
				newl = 1;
			} else
				newl = 0;
			word = sp;
		}
	}
}

/*
 * Print a button
 */
void print_button(WINDOW * win, const char *label, int y, int x, int selected)
{
	int i, temp;

	wmove(win, y, x);
	wattrset(win, selected ? dlg.button_active.atr
		 : dlg.button_inactive.atr);
	waddstr(win, "<");
	temp = strspn(label, " ");
	label += temp;
	wattrset(win, selected ? dlg.button_label_active.atr
		 : dlg.button_label_inactive.atr);
	for (i = 0; i < temp; i++)
		waddch(win, ' ');
	wattrset(win, selected ? dlg.button_key_active.atr
		 : dlg.button_key_inactive.atr);
	waddch(win, label[0]);
	wattrset(win, selected ? dlg.button_label_active.atr
		 : dlg.button_label_inactive.atr);
	waddstr(win, (char *)label + 1);
	wattrset(win, selected ? dlg.button_active.atr
		 : dlg.button_inactive.atr);
	waddstr(win, ">");
	wmove(win, y, x + temp + 1);
}

/*
 * Draw a rectangular box with line drawing characters
 */
void
draw_box(WINDOW * win, int y, int x, int height, int width,
	 chtype box, chtype border)
{
	int i, j;

	wattrset(win, 0);
	for (i = 0; i < height; i++) {
		wmove(win, y + i, x);
		for (j = 0; j < width; j++)
			if (!i && !j)
				waddch(win, border | ACS_ULCORNER);
			else if (i == height - 1 && !j)
				waddch(win, border | ACS_LLCORNER);
			else if (!i && j == width - 1)
				waddch(win, box | ACS_URCORNER);
			else if (i == height - 1 && j == width - 1)
				waddch(win, box | ACS_LRCORNER);
			else if (!i)
				waddch(win, border | ACS_HLINE);
			else if (i == height - 1)
				waddch(win, box | ACS_HLINE);
			else if (!j)
				waddch(win, border | ACS_VLINE);
			else if (j == width - 1)
				waddch(win, box | ACS_VLINE);
			else
				waddch(win, box | ' ');
	}
}

/*
 * Draw shadows along the right and bottom edge to give a more 3D look
 * to the boxes
 */
void draw_shadow(WINDOW * win, int y, int x, int height, int width)
{
	int i;

	if (has_colors()) {	/* Whether terminal supports color? */
		wattrset(win, dlg.shadow.atr);
		wmove(win, y + height, x + 2);
		for (i = 0; i < width; i++)
			waddch(win, winch(win) & A_CHARTEXT);
		for (i = y + 1; i < y + height + 1; i++) {
			wmove(win, i, x + width);
			waddch(win, winch(win) & A_CHARTEXT);
			waddch(win, winch(win) & A_CHARTEXT);
		}
		wnoutrefresh(win);
	}
}

/*
 *  Return the position of the first alphabetic character in a string.
 */
int first_alpha(const char *string, const char *exempt)
{
	int i, in_paren = 0, c;

	for (i = 0; i < strlen(string); i++) {
		c = tolower(string[i]);

		if (strchr("<[(", c))
			++in_paren;
		if (strchr(">])", c) && in_paren > 0)
			--in_paren;

		if ((!in_paren) && isalpha(c) && strchr(exempt, c) == 0)
			return i;
	}

	return 0;
}

/*
 * ncurses uses ESC to detect escaped char sequences. This resutl in
 * a small timeout before ESC is actually delivered to the application.
 * lxdialog suggest <ESC> <ESC> which is correctly translated to two
 * times esc. But then we need to ignore the second esc to avoid stepping
 * out one menu too much. Filter away all escaped key sequences since
 * keypad(FALSE) turn off ncurses support for escape sequences - and thats
 * needed to make notimeout() do as expected.
 */
int on_key_esc(WINDOW *win)
{
	int key;
	int key2;
	int key3;

	nodelay(win, TRUE);
	keypad(win, FALSE);
	key = wgetch(win);
	key2 = wgetch(win);
	do {
		key3 = wgetch(win);
	} while (key3 != ERR);
	nodelay(win, FALSE);
	keypad(win, TRUE);
	if (key == KEY_ESC && key2 == ERR)
		return KEY_ESC;
	else if (key != ERR && key != KEY_ESC && key2 == ERR)
		ungetch(key);

	return -1;
}

/* redraw screen in new size */
int on_key_resize(void)
{
	dialog_clear();
	return KEY_RESIZE;
}

struct dialog_list *item_cur;
struct dialog_list item_nil;
struct dialog_list *item_head;

void item_reset(void)
{
	struct dialog_list *p, *next;

	for (p = item_head; p; p = next) {
		next = p->next;
		free(p);
	}
	item_head = NULL;
	item_cur = &item_nil;
}

void item_make(const char *fmt, ...)
{
	va_list ap;
	struct dialog_list *p = malloc(sizeof(*p));

	if (item_head)
		item_cur->next = p;
	else
		item_head = p;
	item_cur = p;
	memset(p, 0, sizeof(*p));

	va_start(ap, fmt);
	vsnprintf(item_cur->node.str, sizeof(item_cur->node.str), fmt, ap);
	va_end(ap);
}

void item_add_str(const char *fmt, ...)
{
	va_list ap;
        size_t avail;

	avail = sizeof(item_cur->node.str) - strlen(item_cur->node.str);

	va_start(ap, fmt);
	vsnprintf(item_cur->node.str + strlen(item_cur->node.str),
		  avail, fmt, ap);
	item_cur->node.str[sizeof(item_cur->node.str) - 1] = '\0';
	va_end(ap);
}

void item_set_tag(char tag)
{
	item_cur->node.tag = tag;
}
void item_set_data(void *ptr)
{
	item_cur->node.data = ptr;
}

void item_set_selected(int val)
{
	item_cur->node.selected = val;
}

int item_activate_selected(void)
{
	item_foreach()
		if (item_is_selected())
			return 1;
	return 0;
}

void *item_data(void)
{
	return item_cur->node.data;
}

char item_tag(void)
{
	return item_cur->node.tag;
}

int item_count(void)
{
	int n = 0;
	struct dialog_list *p;

	for (p = item_head; p; p = p->next)
		n++;
	return n;
}

void item_set(int n)
{
	int i = 0;
	item_foreach()
		if (i++ == n)
			return;
}

int item_n(void)
{
	int n = 0;
	struct dialog_list *p;

	for (p = item_head; p; p = p->next) {
		if (p == item_cur)
			return n;
		n++;
	}
	return 0;
}

const char *item_str(void)
{
	return item_cur->node.str;
}

int item_is_selected(void)
{
	return (item_cur->node.selected != 0);
}

int item_is_tag(char tag)
{
	return (item_cur->node.tag == tag);
}

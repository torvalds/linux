/*
 *  dialog.h -- common declarations for all dialog modules
 *
 *  AUTHOR: Savio Lam (lam836@cs.cuhk.hk)
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

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef __sun__
#define CURS_MACROS
#endif
#include <ncurses.h>

/*
 * Colors in ncurses 1.9.9e do not work properly since foreground and
 * background colors are OR'd rather than separately masked.  This version
 * of dialog was hacked to work with ncurses 1.9.9e, making it incompatible
 * with standard curses.  The simplest fix (to make this work with standard
 * curses) uses the wbkgdset() function, not used in the original hack.
 * Turn it off if we're building with 1.9.9e, since it just confuses things.
 */
#if defined(NCURSES_VERSION) && defined(_NEED_WRAP) && !defined(GCC_PRINTFLIKE)
#define OLD_NCURSES 1
#undef  wbkgdset
#define wbkgdset(w,p)		/*nothing */
#else
#define OLD_NCURSES 0
#endif

#define TR(params) _tracef params

#define KEY_ESC 27
#define TAB 9
#define MAX_LEN 2048
#define BUF_SIZE (10*1024)
#define MIN(x,y) (x < y ? x : y)
#define MAX(x,y) (x > y ? x : y)

#ifndef ACS_ULCORNER
#define ACS_ULCORNER '+'
#endif
#ifndef ACS_LLCORNER
#define ACS_LLCORNER '+'
#endif
#ifndef ACS_URCORNER
#define ACS_URCORNER '+'
#endif
#ifndef ACS_LRCORNER
#define ACS_LRCORNER '+'
#endif
#ifndef ACS_HLINE
#define ACS_HLINE '-'
#endif
#ifndef ACS_VLINE
#define ACS_VLINE '|'
#endif
#ifndef ACS_LTEE
#define ACS_LTEE '+'
#endif
#ifndef ACS_RTEE
#define ACS_RTEE '+'
#endif
#ifndef ACS_UARROW
#define ACS_UARROW '^'
#endif
#ifndef ACS_DARROW
#define ACS_DARROW 'v'
#endif

/* error return codes */
#define ERRDISPLAYTOOSMALL (KEY_MAX + 1)

/*
 *   Color definitions
 */
struct dialog_color {
	chtype atr;	/* Color attribute */
	int fg;		/* foreground */
	int bg;		/* background */
	int hl;		/* highlight this item */
};

struct subtitle_list {
	struct subtitle_list *next;
	const char *text;
};

struct dialog_info {
	const char *backtitle;
	struct subtitle_list *subtitles;
	struct dialog_color screen;
	struct dialog_color shadow;
	struct dialog_color dialog;
	struct dialog_color title;
	struct dialog_color border;
	struct dialog_color button_active;
	struct dialog_color button_inactive;
	struct dialog_color button_key_active;
	struct dialog_color button_key_inactive;
	struct dialog_color button_label_active;
	struct dialog_color button_label_inactive;
	struct dialog_color inputbox;
	struct dialog_color inputbox_border;
	struct dialog_color searchbox;
	struct dialog_color searchbox_title;
	struct dialog_color searchbox_border;
	struct dialog_color position_indicator;
	struct dialog_color menubox;
	struct dialog_color menubox_border;
	struct dialog_color item;
	struct dialog_color item_selected;
	struct dialog_color tag;
	struct dialog_color tag_selected;
	struct dialog_color tag_key;
	struct dialog_color tag_key_selected;
	struct dialog_color check;
	struct dialog_color check_selected;
	struct dialog_color uarrow;
	struct dialog_color darrow;
};

/*
 * Global variables
 */
extern struct dialog_info dlg;
extern char dialog_input_result[];
extern int saved_x, saved_y;		/* Needed in signal handler in mconf.c */

/*
 * Function prototypes
 */

/* item list as used by checklist and menubox */
void item_reset(void);
void item_make(const char *fmt, ...);
void item_add_str(const char *fmt, ...);
void item_set_tag(char tag);
void item_set_data(void *p);
void item_set_selected(int val);
int item_activate_selected(void);
void *item_data(void);
char item_tag(void);

/* item list manipulation for lxdialog use */
#define MAXITEMSTR 200
struct dialog_item {
	char str[MAXITEMSTR];	/* prompt displayed */
	char tag;
	void *data;	/* pointer to menu item - used by menubox+checklist */
	int selected;	/* Set to 1 by dialog_*() function if selected. */
};

/* list of lialog_items */
struct dialog_list {
	struct dialog_item node;
	struct dialog_list *next;
};

extern struct dialog_list *item_cur;
extern struct dialog_list item_nil;
extern struct dialog_list *item_head;

int item_count(void);
void item_set(int n);
int item_n(void);
const char *item_str(void);
int item_is_selected(void);
int item_is_tag(char tag);
#define item_foreach() \
	for (item_cur = item_head ? item_head: item_cur; \
	     item_cur && (item_cur != &item_nil); item_cur = item_cur->next)

/* generic key handlers */
int on_key_esc(WINDOW *win);
int on_key_resize(void);

/* minimum (re)size values */
#define CHECKLIST_HEIGTH_MIN 6	/* For dialog_checklist() */
#define CHECKLIST_WIDTH_MIN 6
#define INPUTBOX_HEIGTH_MIN 2	/* For dialog_inputbox() */
#define INPUTBOX_WIDTH_MIN 2
#define MENUBOX_HEIGTH_MIN 15	/* For dialog_menu() */
#define MENUBOX_WIDTH_MIN 65
#define TEXTBOX_HEIGTH_MIN 8	/* For dialog_textbox() */
#define TEXTBOX_WIDTH_MIN 8
#define YESNO_HEIGTH_MIN 4	/* For dialog_yesno() */
#define YESNO_WIDTH_MIN 4
#define WINDOW_HEIGTH_MIN 19	/* For init_dialog() */
#define WINDOW_WIDTH_MIN 80

int init_dialog(const char *backtitle);
void set_dialog_backtitle(const char *backtitle);
void set_dialog_subtitles(struct subtitle_list *subtitles);
void end_dialog(int x, int y);
void attr_clear(WINDOW * win, int height, int width, chtype attr);
void dialog_clear(void);
void print_autowrap(WINDOW * win, const char *prompt, int width, int y, int x);
void print_button(WINDOW * win, const char *label, int y, int x, int selected);
void print_title(WINDOW *dialog, const char *title, int width);
void draw_box(WINDOW * win, int y, int x, int height, int width, chtype box,
	      chtype border);
void draw_shadow(WINDOW * win, int y, int x, int height, int width);

int first_alpha(const char *string, const char *exempt);
int dialog_yesno(const char *title, const char *prompt, int height, int width);
int dialog_msgbox(const char *title, const char *prompt, int height,
		  int width, int pause);


typedef void (*update_text_fn)(char *buf, size_t start, size_t end, void
			       *_data);
int dialog_textbox(const char *title, char *tbuf, int initial_height,
		   int initial_width, int *keys, int *_vscroll, int *_hscroll,
		   update_text_fn update_text, void *data);
int dialog_menu(const char *title, const char *prompt,
		const void *selected, int *s_scroll);
int dialog_checklist(const char *title, const char *prompt, int height,
		     int width, int list_height);
int dialog_inputbox(const char *title, const char *prompt, int height,
		    int width, const char *init);

/*
 * This is the base for fictitious keys, which activate
 * the buttons.
 *
 * Mouse-generated keys are the following:
 *   -- the first 32 are used as numbers, in addition to '0'-'9'
 *   -- the lowercase are used to signal mouse-enter events (M_EVENT + 'o')
 *   -- uppercase chars are used to invoke the button (M_EVENT + 'O')
 */
#define M_EVENT (KEY_MAX+1)

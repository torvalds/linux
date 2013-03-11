/*
 *  menubox.c -- implements the menu box
 *
 *  ORIGINAL AUTHOR: Savio Lam (lam836@cs.cuhk.hk)
 *  MODIFIED FOR LINUX KERNEL CONFIG BY: William Roadcap (roadcapw@cfw.com)
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

/*
 *  Changes by Clifford Wolf (god@clifford.at)
 *
 *  [ 1998-06-13 ]
 *
 *    *)  A bugfix for the Page-Down problem
 *
 *    *)  Formerly when I used Page Down and Page Up, the cursor would be set
 *        to the first position in the menu box.  Now lxdialog is a bit
 *        smarter and works more like other menu systems (just have a look at
 *        it).
 *
 *    *)  Formerly if I selected something my scrolling would be broken because
 *        lxdialog is re-invoked by the Menuconfig shell script, can't
 *        remember the last scrolling position, and just sets it so that the
 *        cursor is at the bottom of the box.  Now it writes the temporary file
 *        lxdialog.scrltmp which contains this information. The file is
 *        deleted by lxdialog if the user leaves a submenu or enters a new
 *        one, but it would be nice if Menuconfig could make another "rm -f"
 *        just to be sure.  Just try it out - you will recognise a difference!
 *
 *  [ 1998-06-14 ]
 *
 *    *)  Now lxdialog is crash-safe against broken "lxdialog.scrltmp" files
 *        and menus change their size on the fly.
 *
 *    *)  If for some reason the last scrolling position is not saved by
 *        lxdialog, it sets the scrolling so that the selected item is in the
 *        middle of the menu box, not at the bottom.
 *
 * 02 January 1999, Michael Elizabeth Chastain (mec@shout.net)
 * Reset 'scroll' to 0 if the value from lxdialog.scrltmp is bogus.
 * This fixes a bug in Menuconfig where using ' ' to descend into menus
 * would leave mis-synchronized lxdialog.scrltmp files lying around,
 * fscanf would read in 'scroll', and eventually that value would get used.
 */

#include "dialog.h"

static int menu_width, item_x;

/*
 * Print menu item
 */
static void do_print_item(WINDOW * win, const char *item, int line_y,
                          int selected, int hotkey)
{
	int j;
	char *menu_item = malloc(menu_width + 1);

	strncpy(menu_item, item, menu_width - item_x);
	menu_item[menu_width - item_x] = '\0';
	j = first_alpha(menu_item, "YyNnMmHh");

	/* Clear 'residue' of last item */
	wattrset(win, dlg.menubox.atr);
	wmove(win, line_y, 0);
#if OLD_NCURSES
	{
		int i;
		for (i = 0; i < menu_width; i++)
			waddch(win, ' ');
	}
#else
	wclrtoeol(win);
#endif
	wattrset(win, selected ? dlg.item_selected.atr : dlg.item.atr);
	mvwaddstr(win, line_y, item_x, menu_item);
	if (hotkey) {
		wattrset(win, selected ? dlg.tag_key_selected.atr
			 : dlg.tag_key.atr);
		mvwaddch(win, line_y, item_x + j, menu_item[j]);
	}
	if (selected) {
		wmove(win, line_y, item_x + 1);
	}
	free(menu_item);
	wrefresh(win);
}

#define print_item(index, choice, selected)				\
do {									\
	item_set(index);						\
	do_print_item(menu, item_str(), choice, selected, !item_is_tag(':')); \
} while (0)

/*
 * Print the scroll indicators.
 */
static void print_arrows(WINDOW * win, int item_no, int scroll, int y, int x,
			 int height)
{
	int cur_y, cur_x;

	getyx(win, cur_y, cur_x);

	wmove(win, y, x);

	if (scroll > 0) {
		wattrset(win, dlg.uarrow.atr);
		waddch(win, ACS_UARROW);
		waddstr(win, "(-)");
	} else {
		wattrset(win, dlg.menubox.atr);
		waddch(win, ACS_HLINE);
		waddch(win, ACS_HLINE);
		waddch(win, ACS_HLINE);
		waddch(win, ACS_HLINE);
	}

	y = y + height + 1;
	wmove(win, y, x);
	wrefresh(win);

	if ((height < item_no) && (scroll + height < item_no)) {
		wattrset(win, dlg.darrow.atr);
		waddch(win, ACS_DARROW);
		waddstr(win, "(+)");
	} else {
		wattrset(win, dlg.menubox_border.atr);
		waddch(win, ACS_HLINE);
		waddch(win, ACS_HLINE);
		waddch(win, ACS_HLINE);
		waddch(win, ACS_HLINE);
	}

	wmove(win, cur_y, cur_x);
	wrefresh(win);
}

/*
 * Display the termination buttons.
 */
static void print_buttons(WINDOW * win, int height, int width, int selected)
{
	int x = width / 2 - 28;
	int y = height - 2;

	print_button(win, gettext("Select"), y, x, selected == 0);
	print_button(win, gettext(" Exit "), y, x + 12, selected == 1);
	print_button(win, gettext(" Help "), y, x + 24, selected == 2);
	print_button(win, gettext(" Save "), y, x + 36, selected == 3);
	print_button(win, gettext(" Load "), y, x + 48, selected == 4);

	wmove(win, y, x + 1 + 12 * selected);
	wrefresh(win);
}

/* scroll up n lines (n may be negative) */
static void do_scroll(WINDOW *win, int *scroll, int n)
{
	/* Scroll menu up */
	scrollok(win, TRUE);
	wscrl(win, n);
	scrollok(win, FALSE);
	*scroll = *scroll + n;
	wrefresh(win);
}

/*
 * Display a menu for choosing among a number of options
 */
int dialog_menu(const char *title, const char *prompt,
                const void *selected, int *s_scroll)
{
	int i, j, x, y, box_x, box_y;
	int height, width, menu_height;
	int key = 0, button = 0, scroll = 0, choice = 0;
	int first_item =  0, max_choice;
	WINDOW *dialog, *menu;

do_resize:
	height = getmaxy(stdscr);
	width = getmaxx(stdscr);
	if (height < 15 || width < 65)
		return -ERRDISPLAYTOOSMALL;

	height -= 4;
	width  -= 5;
	menu_height = height - 10;

	max_choice = MIN(menu_height, item_count());

	/* center dialog box on screen */
	x = (COLS - width) / 2;
	y = (LINES - height) / 2;

	draw_shadow(stdscr, y, x, height, width);

	dialog = newwin(height, width, y, x);
	keypad(dialog, TRUE);

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

	wattrset(dialog, dlg.dialog.atr);
	print_autowrap(dialog, prompt, width - 2, 1, 3);

	menu_width = width - 6;
	box_y = height - menu_height - 5;
	box_x = (width - menu_width) / 2 - 1;

	/* create new window for the menu */
	menu = subwin(dialog, menu_height, menu_width,
		      y + box_y + 1, x + box_x + 1);
	keypad(menu, TRUE);

	/* draw a box around the menu items */
	draw_box(dialog, box_y, box_x, menu_height + 2, menu_width + 2,
		 dlg.menubox_border.atr, dlg.menubox.atr);

	if (menu_width >= 80)
		item_x = (menu_width - 70) / 2;
	else
		item_x = 4;

	/* Set choice to default item */
	item_foreach()
		if (selected && (selected == item_data()))
			choice = item_n();
	/* get the saved scroll info */
	scroll = *s_scroll;
	if ((scroll <= choice) && (scroll + max_choice > choice) &&
	   (scroll >= 0) && (scroll + max_choice <= item_count())) {
		first_item = scroll;
		choice = choice - scroll;
	} else {
		scroll = 0;
	}
	if ((choice >= max_choice)) {
		if (choice >= item_count() - max_choice / 2)
			scroll = first_item = item_count() - max_choice;
		else
			scroll = first_item = choice - max_choice / 2;
		choice = choice - scroll;
	}

	/* Print the menu */
	for (i = 0; i < max_choice; i++) {
		print_item(first_item + i, i, i == choice);
	}

	wnoutrefresh(menu);

	print_arrows(dialog, item_count(), scroll,
		     box_y, box_x + item_x + 1, menu_height);

	print_buttons(dialog, height, width, 0);
	wmove(menu, choice, item_x + 1);
	wrefresh(menu);

	while (key != KEY_ESC) {
		key = wgetch(menu);

		if (key < 256 && isalpha(key))
			key = tolower(key);

		if (strchr("ynmh", key))
			i = max_choice;
		else {
			for (i = choice + 1; i < max_choice; i++) {
				item_set(scroll + i);
				j = first_alpha(item_str(), "YyNnMmHh");
				if (key == tolower(item_str()[j]))
					break;
			}
			if (i == max_choice)
				for (i = 0; i < max_choice; i++) {
					item_set(scroll + i);
					j = first_alpha(item_str(), "YyNnMmHh");
					if (key == tolower(item_str()[j]))
						break;
				}
		}

		if (i < max_choice ||
		    key == KEY_UP || key == KEY_DOWN ||
		    key == '-' || key == '+' ||
		    key == KEY_PPAGE || key == KEY_NPAGE) {
			/* Remove highligt of current item */
			print_item(scroll + choice, choice, FALSE);

			if (key == KEY_UP || key == '-') {
				if (choice < 2 && scroll) {
					/* Scroll menu down */
					do_scroll(menu, &scroll, -1);

					print_item(scroll, 0, FALSE);
				} else
					choice = MAX(choice - 1, 0);

			} else if (key == KEY_DOWN || key == '+') {
				print_item(scroll+choice, choice, FALSE);

				if ((choice > max_choice - 3) &&
				    (scroll + max_choice < item_count())) {
					/* Scroll menu up */
					do_scroll(menu, &scroll, 1);

					print_item(scroll+max_choice - 1,
						   max_choice - 1, FALSE);
				} else
					choice = MIN(choice + 1, max_choice - 1);

			} else if (key == KEY_PPAGE) {
				scrollok(menu, TRUE);
				for (i = 0; (i < max_choice); i++) {
					if (scroll > 0) {
						do_scroll(menu, &scroll, -1);
						print_item(scroll, 0, FALSE);
					} else {
						if (choice > 0)
							choice--;
					}
				}

			} else if (key == KEY_NPAGE) {
				for (i = 0; (i < max_choice); i++) {
					if (scroll + max_choice < item_count()) {
						do_scroll(menu, &scroll, 1);
						print_item(scroll+max_choice-1,
							   max_choice - 1, FALSE);
					} else {
						if (choice + 1 < max_choice)
							choice++;
					}
				}
			} else
				choice = i;

			print_item(scroll + choice, choice, TRUE);

			print_arrows(dialog, item_count(), scroll,
				     box_y, box_x + item_x + 1, menu_height);

			wnoutrefresh(dialog);
			wrefresh(menu);

			continue;	/* wait for another key press */
		}

		switch (key) {
		case KEY_LEFT:
		case TAB:
		case KEY_RIGHT:
			button = ((key == KEY_LEFT ? --button : ++button) < 0)
			    ? 4 : (button > 4 ? 0 : button);

			print_buttons(dialog, height, width, button);
			wrefresh(menu);
			break;
		case ' ':
		case 's':
		case 'y':
		case 'n':
		case 'm':
		case '/':
		case 'h':
		case '?':
		case 'z':
		case '\n':
			/* save scroll info */
			*s_scroll = scroll;
			delwin(menu);
			delwin(dialog);
			item_set(scroll + choice);
			item_set_selected(1);
			switch (key) {
			case 'h':
			case '?':
				return 2;
			case 's':
			case 'y':
				return 5;
			case 'n':
				return 6;
			case 'm':
				return 7;
			case ' ':
				return 8;
			case '/':
				return 9;
			case 'z':
				return 10;
			case '\n':
				return button;
			}
			return 0;
		case 'e':
		case 'x':
			key = KEY_ESC;
			break;
		case KEY_ESC:
			key = on_key_esc(menu);
			break;
		case KEY_RESIZE:
			on_key_resize();
			delwin(menu);
			delwin(dialog);
			goto do_resize;
		}
	}
	delwin(menu);
	delwin(dialog);
	return key;		/* ESC pressed */
}

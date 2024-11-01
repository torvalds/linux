// SPDX-License-Identifier: GPL-2.0+
/*
 *  checklist.c -- implements the checklist box
 *
 *  ORIGINAL AUTHOR: Savio Lam (lam836@cs.cuhk.hk)
 *     Stuart Herbert - S.Herbert@sheffield.ac.uk: radiolist extension
 *     Alessandro Rubini - rubini@ipvvis.unipv.it: merged the two
 *  MODIFIED FOR LINUX KERNEL CONFIG BY: William Roadcap (roadcap@cfw.com)
 */

#include "dialog.h"

static int list_width, check_x, item_x;

/*
 * Print list item
 */
static void print_item(WINDOW * win, int choice, int selected)
{
	int i;
	char *list_item = malloc(list_width + 1);

	strncpy(list_item, item_str(), list_width - item_x);
	list_item[list_width - item_x] = '\0';

	/* Clear 'residue' of last item */
	wattrset(win, dlg.menubox.atr);
	wmove(win, choice, 0);
	for (i = 0; i < list_width; i++)
		waddch(win, ' ');

	wmove(win, choice, check_x);
	wattrset(win, selected ? dlg.check_selected.atr
		 : dlg.check.atr);
	if (!item_is_tag(':'))
		wprintw(win, "(%c)", item_is_tag('X') ? 'X' : ' ');

	wattrset(win, selected ? dlg.tag_selected.atr : dlg.tag.atr);
	mvwaddch(win, choice, item_x, list_item[0]);
	wattrset(win, selected ? dlg.item_selected.atr : dlg.item.atr);
	waddstr(win, list_item + 1);
	if (selected) {
		wmove(win, choice, check_x + 1);
		wrefresh(win);
	}
	free(list_item);
}

/*
 * Print the scroll indicators.
 */
static void print_arrows(WINDOW * win, int choice, int item_no, int scroll,
	     int y, int x, int height)
{
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

	if ((height < item_no) && (scroll + choice < item_no - 1)) {
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
}

/*
 *  Display the termination buttons
 */
static void print_buttons(WINDOW * dialog, int height, int width, int selected)
{
	int x = width / 2 - 11;
	int y = height - 2;

	print_button(dialog, "Select", y, x, selected == 0);
	print_button(dialog, " Help ", y, x + 14, selected == 1);

	wmove(dialog, y, x + 1 + 14 * selected);
	wrefresh(dialog);
}

/*
 * Display a dialog box with a list of options that can be turned on or off
 * in the style of radiolist (only one option turned on at a time).
 */
int dialog_checklist(const char *title, const char *prompt, int height,
		     int width, int list_height)
{
	int i, x, y, box_x, box_y;
	int key = 0, button = 0, choice = 0, scroll = 0, max_choice;
	WINDOW *dialog, *list;

	/* which item to highlight */
	item_foreach() {
		if (item_is_tag('X'))
			choice = item_n();
		if (item_is_selected()) {
			choice = item_n();
			break;
		}
	}

do_resize:
	if (getmaxy(stdscr) < (height + CHECKLIST_HEIGTH_MIN))
		return -ERRDISPLAYTOOSMALL;
	if (getmaxx(stdscr) < (width + CHECKLIST_WIDTH_MIN))
		return -ERRDISPLAYTOOSMALL;

	max_choice = MIN(list_height, item_count());

	/* center dialog box on screen */
	x = (getmaxx(stdscr) - width) / 2;
	y = (getmaxy(stdscr) - height) / 2;

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
	waddch(dialog, ACS_RTEE);

	print_title(dialog, title, width);

	wattrset(dialog, dlg.dialog.atr);
	print_autowrap(dialog, prompt, width - 2, 1, 3);

	list_width = width - 6;
	box_y = height - list_height - 5;
	box_x = (width - list_width) / 2 - 1;

	/* create new window for the list */
	list = subwin(dialog, list_height, list_width, y + box_y + 1,
		      x + box_x + 1);

	keypad(list, TRUE);

	/* draw a box around the list items */
	draw_box(dialog, box_y, box_x, list_height + 2, list_width + 2,
		 dlg.menubox_border.atr, dlg.menubox.atr);

	/* Find length of longest item in order to center checklist */
	check_x = 0;
	item_foreach()
		check_x = MAX(check_x, strlen(item_str()) + 4);
	check_x = MIN(check_x, list_width);

	check_x = (list_width - check_x) / 2;
	item_x = check_x + 4;

	if (choice >= list_height) {
		scroll = choice - list_height + 1;
		choice -= scroll;
	}

	/* Print the list */
	for (i = 0; i < max_choice; i++) {
		item_set(scroll + i);
		print_item(list, i, i == choice);
	}

	print_arrows(dialog, choice, item_count(), scroll,
		     box_y, box_x + check_x + 5, list_height);

	print_buttons(dialog, height, width, 0);

	wnoutrefresh(dialog);
	wnoutrefresh(list);
	doupdate();

	while (key != KEY_ESC) {
		key = wgetch(dialog);

		for (i = 0; i < max_choice; i++) {
			item_set(i + scroll);
			if (toupper(key) == toupper(item_str()[0]))
				break;
		}

		if (i < max_choice || key == KEY_UP || key == KEY_DOWN ||
		    key == '+' || key == '-') {
			if (key == KEY_UP || key == '-') {
				if (!choice) {
					if (!scroll)
						continue;
					/* Scroll list down */
					if (list_height > 1) {
						/* De-highlight current first item */
						item_set(scroll);
						print_item(list, 0, FALSE);
						scrollok(list, TRUE);
						wscrl(list, -1);
						scrollok(list, FALSE);
					}
					scroll--;
					item_set(scroll);
					print_item(list, 0, TRUE);
					print_arrows(dialog, choice, item_count(),
						     scroll, box_y, box_x + check_x + 5, list_height);

					wnoutrefresh(dialog);
					wrefresh(list);

					continue;	/* wait for another key press */
				} else
					i = choice - 1;
			} else if (key == KEY_DOWN || key == '+') {
				if (choice == max_choice - 1) {
					if (scroll + choice >= item_count() - 1)
						continue;
					/* Scroll list up */
					if (list_height > 1) {
						/* De-highlight current last item before scrolling up */
						item_set(scroll + max_choice - 1);
						print_item(list,
							    max_choice - 1,
							    FALSE);
						scrollok(list, TRUE);
						wscrl(list, 1);
						scrollok(list, FALSE);
					}
					scroll++;
					item_set(scroll + max_choice - 1);
					print_item(list, max_choice - 1, TRUE);

					print_arrows(dialog, choice, item_count(),
						     scroll, box_y, box_x + check_x + 5, list_height);

					wnoutrefresh(dialog);
					wrefresh(list);

					continue;	/* wait for another key press */
				} else
					i = choice + 1;
			}
			if (i != choice) {
				/* De-highlight current item */
				item_set(scroll + choice);
				print_item(list, choice, FALSE);
				/* Highlight new item */
				choice = i;
				item_set(scroll + choice);
				print_item(list, choice, TRUE);
				wnoutrefresh(dialog);
				wrefresh(list);
			}
			continue;	/* wait for another key press */
		}
		switch (key) {
		case 'H':
		case 'h':
		case '?':
			button = 1;
			/* fall-through */
		case 'S':
		case 's':
		case ' ':
		case '\n':
			item_foreach()
				item_set_selected(0);
			item_set(scroll + choice);
			item_set_selected(1);
			delwin(list);
			delwin(dialog);
			return button;
		case TAB:
		case KEY_LEFT:
		case KEY_RIGHT:
			button = ((key == KEY_LEFT ? --button : ++button) < 0)
			    ? 1 : (button > 1 ? 0 : button);

			print_buttons(dialog, height, width, button);
			wrefresh(dialog);
			break;
		case 'X':
		case 'x':
			key = KEY_ESC;
			break;
		case KEY_ESC:
			key = on_key_esc(dialog);
			break;
		case KEY_RESIZE:
			delwin(list);
			delwin(dialog);
			on_key_resize();
			goto do_resize;
		}

		/* Now, update everything... */
		doupdate();
	}
	delwin(list);
	delwin(dialog);
	return key;		/* ESC pressed */
}

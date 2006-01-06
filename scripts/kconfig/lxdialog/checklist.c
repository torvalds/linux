/*
 *  checklist.c -- implements the checklist box
 *
 *  ORIGINAL AUTHOR: Savio Lam (lam836@cs.cuhk.hk)
 *     Stuart Herbert - S.Herbert@sheffield.ac.uk: radiolist extension
 *     Alessandro Rubini - rubini@ipvvis.unipv.it: merged the two
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

static int list_width, check_x, item_x;

/*
 * Print list item
 */
static void print_item(WINDOW * win, const char *item, int status, int choice,
		       int selected)
{
	int i;

	/* Clear 'residue' of last item */
	wattrset(win, menubox_attr);
	wmove(win, choice, 0);
	for (i = 0; i < list_width; i++)
		waddch(win, ' ');

	wmove(win, choice, check_x);
	wattrset(win, selected ? check_selected_attr : check_attr);
	wprintw(win, "(%c)", status ? 'X' : ' ');

	wattrset(win, selected ? tag_selected_attr : tag_attr);
	mvwaddch(win, choice, item_x, item[0]);
	wattrset(win, selected ? item_selected_attr : item_attr);
	waddstr(win, (char *)item + 1);
	if (selected) {
		wmove(win, choice, check_x + 1);
		wrefresh(win);
	}
}

/*
 * Print the scroll indicators.
 */
static void print_arrows(WINDOW * win, int choice, int item_no, int scroll,
	     int y, int x, int height)
{
	wmove(win, y, x);

	if (scroll > 0) {
		wattrset(win, uarrow_attr);
		waddch(win, ACS_UARROW);
		waddstr(win, "(-)");
	} else {
		wattrset(win, menubox_attr);
		waddch(win, ACS_HLINE);
		waddch(win, ACS_HLINE);
		waddch(win, ACS_HLINE);
		waddch(win, ACS_HLINE);
	}

	y = y + height + 1;
	wmove(win, y, x);

	if ((height < item_no) && (scroll + choice < item_no - 1)) {
		wattrset(win, darrow_attr);
		waddch(win, ACS_DARROW);
		waddstr(win, "(+)");
	} else {
		wattrset(win, menubox_border_attr);
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
		     int width, int list_height, int item_no,
		     const char *const *items)
{
	int i, x, y, box_x, box_y;
	int key = 0, button = 0, choice = 0, scroll = 0, max_choice, *status;
	WINDOW *dialog, *list;

	/* Allocate space for storing item on/off status */
	if ((status = malloc(sizeof(int) * item_no)) == NULL) {
		endwin();
		fprintf(stderr,
			"\nCan't allocate memory in dialog_checklist().\n");
		exit(-1);
	}

	/* Initializes status */
	for (i = 0; i < item_no; i++) {
		status[i] = !strcasecmp(items[i * 3 + 2], "on");
		if ((!choice && status[i])
		    || !strcasecmp(items[i * 3 + 2], "selected"))
			choice = i + 1;
	}
	if (choice)
		choice--;

	max_choice = MIN(list_height, item_no);

	/* center dialog box on screen */
	x = (COLS - width) / 2;
	y = (LINES - height) / 2;

	draw_shadow(stdscr, y, x, height, width);

	dialog = newwin(height, width, y, x);
	keypad(dialog, TRUE);

	draw_box(dialog, 0, 0, height, width, dialog_attr, border_attr);
	wattrset(dialog, border_attr);
	mvwaddch(dialog, height - 3, 0, ACS_LTEE);
	for (i = 0; i < width - 2; i++)
		waddch(dialog, ACS_HLINE);
	wattrset(dialog, dialog_attr);
	waddch(dialog, ACS_RTEE);

	print_title(dialog, title, width);

	wattrset(dialog, dialog_attr);
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
	         menubox_border_attr, menubox_attr);

	/* Find length of longest item in order to center checklist */
	check_x = 0;
	for (i = 0; i < item_no; i++)
		check_x = MAX(check_x, +strlen(items[i * 3 + 1]) + 4);

	check_x = (list_width - check_x) / 2;
	item_x = check_x + 4;

	if (choice >= list_height) {
		scroll = choice - list_height + 1;
		choice -= scroll;
	}

	/* Print the list */
	for (i = 0; i < max_choice; i++) {
		print_item(list, items[(scroll + i) * 3 + 1],
			   status[i + scroll], i, i == choice);
	}

	print_arrows(dialog, choice, item_no, scroll,
		     box_y, box_x + check_x + 5, list_height);

	print_buttons(dialog, height, width, 0);

	wnoutrefresh(list);
	wnoutrefresh(dialog);
	doupdate();

	while (key != ESC) {
		key = wgetch(dialog);

		for (i = 0; i < max_choice; i++)
			if (toupper(key) ==
			    toupper(items[(scroll + i) * 3 + 1][0]))
				break;

		if (i < max_choice || key == KEY_UP || key == KEY_DOWN ||
		    key == '+' || key == '-') {
			if (key == KEY_UP || key == '-') {
				if (!choice) {
					if (!scroll)
						continue;
					/* Scroll list down */
					if (list_height > 1) {
						/* De-highlight current first item */
						print_item(list, items[scroll * 3 + 1],
							   status[scroll], 0, FALSE);
						scrollok(list, TRUE);
						wscrl(list, -1);
						scrollok(list, FALSE);
					}
					scroll--;
					print_item(list, items[scroll * 3 + 1], status[scroll], 0, TRUE);
					wnoutrefresh(list);

					print_arrows(dialog, choice, item_no,
						     scroll, box_y, box_x + check_x + 5, list_height);

					wrefresh(dialog);

					continue;	/* wait for another key press */
				} else
					i = choice - 1;
			} else if (key == KEY_DOWN || key == '+') {
				if (choice == max_choice - 1) {
					if (scroll + choice >= item_no - 1)
						continue;
					/* Scroll list up */
					if (list_height > 1) {
						/* De-highlight current last item before scrolling up */
						print_item(list, items[(scroll + max_choice - 1) * 3 + 1],
							   status[scroll + max_choice - 1],
							   max_choice - 1, FALSE);
						scrollok(list, TRUE);
						wscrl(list, 1);
						scrollok(list, FALSE);
					}
					scroll++;
					print_item(list, items[(scroll + max_choice - 1) * 3 + 1],
						   status[scroll + max_choice - 1], max_choice - 1, TRUE);
					wnoutrefresh(list);

					print_arrows(dialog, choice, item_no,
						     scroll, box_y, box_x + check_x + 5, list_height);

					wrefresh(dialog);

					continue;	/* wait for another key press */
				} else
					i = choice + 1;
			}
			if (i != choice) {
				/* De-highlight current item */
				print_item(list, items[(scroll + choice) * 3 + 1],
					   status[scroll + choice], choice, FALSE);
				/* Highlight new item */
				choice = i;
				print_item(list, items[(scroll + choice) * 3 + 1],
					   status[scroll + choice], choice, TRUE);
				wnoutrefresh(list);
				wrefresh(dialog);
			}
			continue;	/* wait for another key press */
		}
		switch (key) {
		case 'H':
		case 'h':
		case '?':
			fprintf(stderr, "%s", items[(scroll + choice) * 3]);
			delwin(dialog);
			free(status);
			return 1;
		case TAB:
		case KEY_LEFT:
		case KEY_RIGHT:
			button = ((key == KEY_LEFT ? --button : ++button) < 0)
			    ? 1 : (button > 1 ? 0 : button);

			print_buttons(dialog, height, width, button);
			wrefresh(dialog);
			break;
		case 'S':
		case 's':
		case ' ':
		case '\n':
			if (!button) {
				if (!status[scroll + choice]) {
					for (i = 0; i < item_no; i++)
						status[i] = 0;
					status[scroll + choice] = 1;
					for (i = 0; i < max_choice; i++)
						print_item(list, items[(scroll + i) * 3 + 1],
							   status[scroll + i], i, i == choice);
				}
				wnoutrefresh(list);
				wrefresh(dialog);

				for (i = 0; i < item_no; i++)
					if (status[i])
						fprintf(stderr, "%s", items[i * 3]);
			} else
				fprintf(stderr, "%s", items[(scroll + choice) * 3]);
			delwin(dialog);
			free(status);
			return button;
		case 'X':
		case 'x':
			key = ESC;
		case ESC:
			break;
		}

		/* Now, update everything... */
		doupdate();
	}

	delwin(dialog);
	free(status);
	return -1;		/* ESC pressed */
}

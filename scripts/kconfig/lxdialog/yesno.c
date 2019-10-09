// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  yesno.c -- implements the yes/no box
 *
 *  ORIGINAL AUTHOR: Savio Lam (lam836@cs.cuhk.hk)
 *  MODIFIED FOR LINUX KERNEL CONFIG BY: William Roadcap (roadcap@cfw.com)
 */

#include "dialog.h"

/*
 * Display termination buttons
 */
static void print_buttons(WINDOW * dialog, int height, int width, int selected)
{
	int x = width / 2 - 10;
	int y = height - 2;

	print_button(dialog, " Yes ", y, x, selected == 0);
	print_button(dialog, "  No  ", y, x + 13, selected == 1);

	wmove(dialog, y, x + 1 + 13 * selected);
	wrefresh(dialog);
}

/*
 * Display a dialog box with two buttons - Yes and No
 */
int dialog_yesno(const char *title, const char *prompt, int height, int width)
{
	int i, x, y, key = 0, button = 0;
	WINDOW *dialog;

do_resize:
	if (getmaxy(stdscr) < (height + YESNO_HEIGTH_MIN))
		return -ERRDISPLAYTOOSMALL;
	if (getmaxx(stdscr) < (width + YESNO_WIDTH_MIN))
		return -ERRDISPLAYTOOSMALL;

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

	print_buttons(dialog, height, width, 0);

	while (key != KEY_ESC) {
		key = wgetch(dialog);
		switch (key) {
		case 'Y':
		case 'y':
			delwin(dialog);
			return 0;
		case 'N':
		case 'n':
			delwin(dialog);
			return 1;

		case TAB:
		case KEY_LEFT:
		case KEY_RIGHT:
			button = ((key == KEY_LEFT ? --button : ++button) < 0) ? 1 : (button > 1 ? 0 : button);

			print_buttons(dialog, height, width, button);
			wrefresh(dialog);
			break;
		case ' ':
		case '\n':
			delwin(dialog);
			return button;
		case KEY_ESC:
			key = on_key_esc(dialog);
			break;
		case KEY_RESIZE:
			delwin(dialog);
			on_key_resize();
			goto do_resize;
		}
	}

	delwin(dialog);
	return key;		/* ESC pressed */
}

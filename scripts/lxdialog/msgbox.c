/*
 *  msgbox.c -- implements the message box and info box
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

#include "dialog.h"

/*
 * Display a message box. Program will pause and display an "OK" button
 * if the parameter 'pause' is non-zero.
 */
int
dialog_msgbox (const char *title, const char *prompt, int height, int width,
		int pause)
{
    int i, x, y, key = 0;
    WINDOW *dialog;

    /* center dialog box on screen */
    x = (COLS - width) / 2;
    y = (LINES - height) / 2;

    draw_shadow (stdscr, y, x, height, width);

    dialog = newwin (height, width, y, x);
    keypad (dialog, TRUE);

    draw_box (dialog, 0, 0, height, width, dialog_attr, border_attr);

    if (title != NULL && strlen(title) >= width-2 ) {
	/* truncate long title -- mec */
	char * title2 = malloc(width-2+1);
	memcpy( title2, title, width-2 );
	title2[width-2] = '\0';
	title = title2;
    }

    if (title != NULL) {
	wattrset (dialog, title_attr);
	mvwaddch (dialog, 0, (width - strlen(title))/2 - 1, ' ');
	waddstr (dialog, (char *)title);
	waddch (dialog, ' ');
    }
    wattrset (dialog, dialog_attr);
    print_autowrap (dialog, prompt, width - 2, 1, 2);

    if (pause) {
	wattrset (dialog, border_attr);
	mvwaddch (dialog, height - 3, 0, ACS_LTEE);
	for (i = 0; i < width - 2; i++)
	    waddch (dialog, ACS_HLINE);
	wattrset (dialog, dialog_attr);
	waddch (dialog, ACS_RTEE);

	print_button (dialog, "  Ok  ",
		      height - 2, width / 2 - 4, TRUE);

	wrefresh (dialog);
	while (key != ESC && key != '\n' && key != ' ' &&
               key != 'O' && key != 'o' && key != 'X' && key != 'x')
	    key = wgetch (dialog);
    } else {
	key = '\n';
	wrefresh (dialog);
    }

    delwin (dialog);
    return key == ESC ? -1 : 0;
}

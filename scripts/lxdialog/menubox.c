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
static void
print_item (WINDOW * win, const char *item, int choice, int selected, int hotkey)
{
    int j;
    char menu_item[menu_width+1];

    strncpy(menu_item, item, menu_width);
    menu_item[menu_width] = 0;
    j = first_alpha(menu_item, "YyNnMmHh");

    /* Clear 'residue' of last item */
    wattrset (win, menubox_attr);
    wmove (win, choice, 0);
#if OLD_NCURSES
    {
        int i;
        for (i = 0; i < menu_width; i++)
	    waddch (win, ' ');
    }
#else
    wclrtoeol(win);
#endif
    wattrset (win, selected ? item_selected_attr : item_attr);
    mvwaddstr (win, choice, item_x, menu_item);
    if (hotkey) {
    	wattrset (win, selected ? tag_key_selected_attr : tag_key_attr);
    	mvwaddch(win, choice, item_x+j, menu_item[j]);
    }
    if (selected) {
	wmove (win, choice, item_x+1);
	wrefresh (win);
    }
}

/*
 * Print the scroll indicators.
 */
static void
print_arrows (WINDOW * win, int item_no, int scroll,
		int y, int x, int height)
{
    int cur_y, cur_x;

    getyx(win, cur_y, cur_x);

    wmove(win, y, x);

    if (scroll > 0) {
	wattrset (win, uarrow_attr);
	waddch (win, ACS_UARROW);
	waddstr (win, "(-)");
    }
    else {
	wattrset (win, menubox_attr);
	waddch (win, ACS_HLINE);
	waddch (win, ACS_HLINE);
	waddch (win, ACS_HLINE);
	waddch (win, ACS_HLINE);
    }

   y = y + height + 1;
   wmove(win, y, x);

   if ((height < item_no) && (scroll + height < item_no)) {
	wattrset (win, darrow_attr);
	waddch (win, ACS_DARROW);
	waddstr (win, "(+)");
    }
    else {
	wattrset (win, menubox_border_attr);
	waddch (win, ACS_HLINE);
	waddch (win, ACS_HLINE);
	waddch (win, ACS_HLINE);
	waddch (win, ACS_HLINE);
   }

   wmove(win, cur_y, cur_x);
}

/*
 * Display the termination buttons.
 */
static void
print_buttons (WINDOW *win, int height, int width, int selected)
{
    int x = width / 2 - 16;
    int y = height - 2;

    print_button (win, "Select", y, x, selected == 0);
    print_button (win, " Exit ", y, x + 12, selected == 1);
    print_button (win, " Help ", y, x + 24, selected == 2);

    wmove(win, y, x+1+12*selected);
    wrefresh (win);
}

/*
 * Display a menu for choosing among a number of options
 */
int
dialog_menu (const char *title, const char *prompt, int height, int width,
		int menu_height, const char *current, int item_no,
		const char * const * items)

{
    int i, j, x, y, box_x, box_y;
    int key = 0, button = 0, scroll = 0, choice = 0, first_item = 0, max_choice;
    WINDOW *dialog, *menu;
    FILE *f;

    max_choice = MIN (menu_height, item_no);

    /* center dialog box on screen */
    x = (COLS - width) / 2;
    y = (LINES - height) / 2;

    draw_shadow (stdscr, y, x, height, width);

    dialog = newwin (height, width, y, x);
    keypad (dialog, TRUE);

    draw_box (dialog, 0, 0, height, width, dialog_attr, border_attr);
    wattrset (dialog, border_attr);
    mvwaddch (dialog, height - 3, 0, ACS_LTEE);
    for (i = 0; i < width - 2; i++)
	waddch (dialog, ACS_HLINE);
    wattrset (dialog, dialog_attr);
    wbkgdset (dialog, dialog_attr & A_COLOR);
    waddch (dialog, ACS_RTEE);

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
    print_autowrap (dialog, prompt, width - 2, 1, 3);

    menu_width = width - 6;
    box_y = height - menu_height - 5;
    box_x = (width - menu_width) / 2 - 1;

    /* create new window for the menu */
    menu = subwin (dialog, menu_height, menu_width,
		y + box_y + 1, x + box_x + 1);
    keypad (menu, TRUE);

    /* draw a box around the menu items */
    draw_box (dialog, box_y, box_x, menu_height + 2, menu_width + 2,
	      menubox_border_attr, menubox_attr);

    /*
     * Find length of longest item in order to center menu.
     * Set 'choice' to default item. 
     */
    item_x = 0;
    for (i = 0; i < item_no; i++) {
	item_x = MAX (item_x, MIN(menu_width, strlen (items[i * 2 + 1]) + 2));
	if (strcmp(current, items[i*2]) == 0) choice = i;
    }

    item_x = (menu_width - item_x) / 2;

    /* get the scroll info from the temp file */
    if ( (f=fopen("lxdialog.scrltmp","r")) != NULL ) {
	if ( (fscanf(f,"%d\n",&scroll) == 1) && (scroll <= choice) &&
	     (scroll+max_choice > choice) && (scroll >= 0) &&
	     (scroll+max_choice <= item_no) ) {
	    first_item = scroll;
	    choice = choice - scroll;
	    fclose(f);
	} else {
	    scroll=0;
	    remove("lxdialog.scrltmp");
	    fclose(f);
	    f=NULL;
	}
    }
    if ( (choice >= max_choice) || (f==NULL && choice >= max_choice/2) ) {
	if (choice >= item_no-max_choice/2)
	    scroll = first_item = item_no-max_choice;
	else
	    scroll = first_item = choice - max_choice/2;
	choice = choice - scroll;
    }

    /* Print the menu */
    for (i=0; i < max_choice; i++) {
	print_item (menu, items[(first_item + i) * 2 + 1], i, i == choice,
                    (items[(first_item + i)*2][0] != ':'));
    }

    wnoutrefresh (menu);

    print_arrows(dialog, item_no, scroll,
		 box_y, box_x+item_x+1, menu_height);

    print_buttons (dialog, height, width, 0);
    wmove (menu, choice, item_x+1);
    wrefresh (menu);

    while (key != ESC) {
	key = wgetch(menu);

	if (key < 256 && isalpha(key)) key = tolower(key);

	if (strchr("ynmh", key))
		i = max_choice;
	else {
        for (i = choice+1; i < max_choice; i++) {
		j = first_alpha(items[(scroll+i)*2+1], "YyNnMmHh");
		if (key == tolower(items[(scroll+i)*2+1][j]))
                	break;
	}
	if (i == max_choice)
       		for (i = 0; i < max_choice; i++) {
			j = first_alpha(items[(scroll+i)*2+1], "YyNnMmHh");
			if (key == tolower(items[(scroll+i)*2+1][j]))
                		break;
		}
	}

	if (i < max_choice || 
            key == KEY_UP || key == KEY_DOWN ||
            key == '-' || key == '+' ||
            key == KEY_PPAGE || key == KEY_NPAGE) {

            print_item (menu, items[(scroll+choice)*2+1], choice, FALSE,
                       (items[(scroll+choice)*2][0] != ':'));

	    if (key == KEY_UP || key == '-') {
                if (choice < 2 && scroll) {
	            /* Scroll menu down */
                    scrollok (menu, TRUE);
                    wscrl (menu, -1);
                    scrollok (menu, FALSE);

                    scroll--;

                    print_item (menu, items[scroll * 2 + 1], 0, FALSE,
                               (items[scroll*2][0] != ':'));
		} else
		    choice = MAX(choice - 1, 0);

	    } else if (key == KEY_DOWN || key == '+')  {

		print_item (menu, items[(scroll+choice)*2+1], choice, FALSE,
                                (items[(scroll+choice)*2][0] != ':'));

                if ((choice > max_choice-3) &&
                    (scroll + max_choice < item_no)
                   ) {
		    /* Scroll menu up */
		    scrollok (menu, TRUE);
		    wscrl (menu, 1);
                    scrollok (menu, FALSE);

                    scroll++;

                    print_item (menu, items[(scroll+max_choice-1)*2+1],
                               max_choice-1, FALSE,
                               (items[(scroll+max_choice-1)*2][0] != ':'));
                } else
                    choice = MIN(choice+1, max_choice-1);

	    } else if (key == KEY_PPAGE) {
	        scrollok (menu, TRUE);
                for (i=0; (i < max_choice); i++) {
                    if (scroll > 0) {
                	wscrl (menu, -1);
                	scroll--;
                	print_item (menu, items[scroll * 2 + 1], 0, FALSE,
                	(items[scroll*2][0] != ':'));
                    } else {
                        if (choice > 0)
                            choice--;
                    }
                }
                scrollok (menu, FALSE);

            } else if (key == KEY_NPAGE) {
                for (i=0; (i < max_choice); i++) {
                    if (scroll+max_choice < item_no) {
			scrollok (menu, TRUE);
			wscrl (menu, 1);
			scrollok (menu, FALSE);
                	scroll++;
                	print_item (menu, items[(scroll+max_choice-1)*2+1],
			            max_choice-1, FALSE,
			            (items[(scroll+max_choice-1)*2][0] != ':'));
		    } else {
			if (choice+1 < max_choice)
			    choice++;
		    }
                }

            } else
                choice = i;

            print_item (menu, items[(scroll+choice)*2+1], choice, TRUE,
                       (items[(scroll+choice)*2][0] != ':'));

            print_arrows(dialog, item_no, scroll,
                         box_y, box_x+item_x+1, menu_height);

            wnoutrefresh (dialog);
            wrefresh (menu);

	    continue;		/* wait for another key press */
        }

	switch (key) {
	case KEY_LEFT:
	case TAB:
	case KEY_RIGHT:
	    button = ((key == KEY_LEFT ? --button : ++button) < 0)
			? 2 : (button > 2 ? 0 : button);

	    print_buttons(dialog, height, width, button);
	    wrefresh (menu);
	    break;
	case ' ':
	case 's':
	case 'y':
	case 'n':
	case 'm':
	case '/':
	    /* save scroll info */
	    if ( (f=fopen("lxdialog.scrltmp","w")) != NULL ) {
		fprintf(f,"%d\n",scroll);
		fclose(f);
	    }
	    delwin (dialog);
            fprintf(stderr, "%s\n", items[(scroll + choice) * 2]);
            switch (key) {
            case 's': return 3;
            case 'y': return 3;
            case 'n': return 4;
            case 'm': return 5;
            case ' ': return 6;
            case '/': return 7;
            }
	    return 0;
	case 'h':
	case '?':
	    button = 2;
	case '\n':
	    delwin (dialog);
	    if (button == 2) 
            	fprintf(stderr, "%s \"%s\"\n", 
			items[(scroll + choice) * 2],
			items[(scroll + choice) * 2 + 1] +
			first_alpha(items[(scroll + choice) * 2 + 1],""));
	    else
            	fprintf(stderr, "%s\n", items[(scroll + choice) * 2]);

	    remove("lxdialog.scrltmp");
	    return button;
	case 'e':
	case 'x':
	    key = ESC;
	case ESC:
	    break;
	}
    }

    delwin (dialog);
    remove("lxdialog.scrltmp");
    return -1;			/* ESC pressed */
}

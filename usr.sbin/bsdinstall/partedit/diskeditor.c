/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Nathan Whitehorn
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <stdio.h>
#include <unistd.h>
#include <libutil.h>
#include <dialog.h>
#include <dlg_keys.h>

#include "diskeditor.h"

static void
print_partedit_item(WINDOW *partitions, struct partedit_item *items,
    int item, int nscroll, int selected)
{
	chtype attr = A_NORMAL;
	char sizetext[16];
	int y = item - nscroll + 1;

	wattrset(partitions, selected ? item_selected_attr : item_attr);
	wmove(partitions, y, MARGIN + items[item].indentation*2);
	dlg_print_text(partitions, items[item].name, 10, &attr);
	wmove(partitions, y, 17);
	wattrset(partitions, item_attr);

	humanize_number(sizetext, 7, items[item].size, "B", HN_AUTOSCALE,
	    HN_DECIMAL);
	dlg_print_text(partitions, sizetext, 8, &attr);
	wmove(partitions, y, 25);
	dlg_print_text(partitions, items[item].type, 15, &attr);
	wmove(partitions, y, 40);
	if (items[item].mountpoint != NULL)
		dlg_print_text(partitions, items[item].mountpoint, 8, &attr);
}

int
diskeditor_show(const char *title, const char *cprompt,
    struct partedit_item *items, int nitems, int *selected, int *nscroll)
{
	WINDOW *dialog, *partitions;
	char *prompt;
	const char *buttons[] =
	    { "Create", "Delete", "Modify", "Revert", "Auto", "Finish", NULL };
	const char *help_text[] = {
	    "Add a new partition", "Delete selected partition or partitions",
	    "Change partition type or mountpoint",
	    "Revert changes to disk setup", "Use guided partitioning tool",
	    "Exit partitioner (will ask whether to save changes)", NULL };
	int x, y;
	int i;
	int height, width, min_width;
	int partlist_height, partlist_width;
	int cur_scroll = 0;
	int key, fkey;
	int cur_button = 5, cur_part = 0;
	int result = DLG_EXIT_UNKNOWN;

	static DLG_KEYS_BINDING binding[] = {
		ENTERKEY_BINDINGS,
		DLG_KEYS_DATA( DLGK_ENTER,      ' ' ),
		DLG_KEYS_DATA( DLGK_ITEM_NEXT, KEY_DOWN ),
		DLG_KEYS_DATA( DLGK_ITEM_PREV, KEY_UP ),
		DLG_KEYS_DATA( DLGK_FIELD_NEXT, KEY_RIGHT ),
		DLG_KEYS_DATA( DLGK_FIELD_NEXT, TAB ),
		DLG_KEYS_DATA( DLGK_FIELD_PREV, KEY_BTAB ),
		DLG_KEYS_DATA( DLGK_FIELD_PREV, KEY_LEFT ),

		SCROLLKEY_BINDINGS,
		END_KEYS_BINDING
	};

	static DLG_KEYS_BINDING binding2[] = {
		INPUTSTR_BINDINGS,
		ENTERKEY_BINDINGS,
		DLG_KEYS_DATA( DLGK_FIELD_NEXT, TAB ),
		DLG_KEYS_DATA( DLGK_FIELD_PREV, KEY_BTAB ),
		DLG_KEYS_DATA( DLGK_ITEM_NEXT,  CHR_NEXT ),
		DLG_KEYS_DATA( DLGK_ITEM_NEXT,  KEY_DOWN ),
		DLG_KEYS_DATA( DLGK_ITEM_NEXT,  KEY_NEXT ),
		DLG_KEYS_DATA( DLGK_ITEM_PREV,  CHR_PREVIOUS ),
		DLG_KEYS_DATA( DLGK_ITEM_PREV,  KEY_PREVIOUS ),
		DLG_KEYS_DATA( DLGK_ITEM_PREV,  KEY_UP ),
		DLG_KEYS_DATA( DLGK_PAGE_NEXT,  KEY_NPAGE ),
		DLG_KEYS_DATA( DLGK_PAGE_PREV,  KEY_PPAGE ),
		END_KEYS_BINDING
	};

	/*
	 * Set up editor window.
	 */
	prompt = dlg_strclone(cprompt);

	min_width = 50;
	height = width = 0;
	partlist_height = 10;
	dlg_tab_correct_str(prompt);
	dlg_button_layout(buttons, &min_width);
	dlg_auto_size(title, prompt, &height, &width, 2, min_width);
	height += partlist_height;
	partlist_width = width - 2*MARGIN;
	dlg_print_size(height, width);
	dlg_ctl_size(height, width);

	x = dlg_box_x_ordinate(width);
	y = dlg_box_y_ordinate(height);

	dialog = dlg_new_window(height, width, y, x);
	dlg_register_window(dialog, "diskeditorbox", binding);
	dlg_register_buttons(dialog, "diskeditorbox", buttons);

	dlg_draw_box(dialog, 0, 0, height, width, dialog_attr, border_attr);
	dlg_draw_bottom_box(dialog);
	dlg_draw_title(dialog, title);
	wattrset(dialog, dialog_attr);

	/* Partition list sub-window */
	partitions = dlg_sub_window(dialog, partlist_height, partlist_width,
	    y + 3, x + 1);
	dlg_register_window(partitions, "partlist", binding2);
	dlg_register_buttons(partitions, "partlist", buttons);
	wattrset(partitions, menubox_attr);

	dlg_item_help(help_text[cur_button]);
	dlg_draw_buttons(dialog, height - 2*MARGIN, 0, buttons,
	    cur_button, FALSE, width);
	dlg_print_autowrap(dialog, prompt, height, width);

	if (selected != NULL)
		cur_part = *selected;
	if (nscroll != NULL)
		cur_scroll = *nscroll;
	if (cur_part - cur_scroll >= partlist_height - 2 ||
	    cur_part - cur_scroll < 0)
		cur_scroll = cur_part;

repaint:
	dlg_draw_box(dialog, 3, 1,  partlist_height, partlist_width,
	    menubox_border_attr, menubox_attr);
	for (i = cur_scroll; i < MIN(cur_scroll + partlist_height - 2, nitems);
	    i++)
		print_partedit_item(partitions, items, i, cur_scroll,
		    i == cur_part);
	if (nitems > partlist_height - 2)
		dlg_draw_arrows(partitions, cur_scroll > 0,
		    nitems > cur_scroll + partlist_height - 2,
		    partlist_width - 5, 0, partlist_height - 1);
	wrefresh(partitions);

	while (result == DLG_EXIT_UNKNOWN) {
		key = dlg_mouse_wgetch(dialog, &fkey);
		if ((i = dlg_char_to_button(key, buttons)) >= 0) {
			cur_button = i;
			dlg_item_help(help_text[cur_button]);
			dlg_draw_buttons(dialog, height - 2*MARGIN, 0, buttons,
			    cur_button, FALSE, width);
			break;
		}

		if (!fkey)
			continue;

		switch (key) {
		case DLGK_FIELD_NEXT:
			cur_button = dlg_next_button(buttons, cur_button);
			if (cur_button < 0)
				cur_button = 0;
			dlg_item_help(help_text[cur_button]);
			dlg_draw_buttons(dialog, height - 2*MARGIN, 0, buttons,
			    cur_button, FALSE, width);
			break;
		case DLGK_FIELD_PREV:
			cur_button = dlg_prev_button(buttons, cur_button);
			if (cur_button < 0)
				cur_button = 0;
			dlg_item_help(help_text[cur_button]);
			dlg_draw_buttons(dialog, height - 2*MARGIN, 0, buttons,
			    cur_button, FALSE, width);
			break;
		case DLGK_ITEM_NEXT:
			if (cur_part == nitems - 1)
				break; /* End of list */

			/* Deselect old item */
			print_partedit_item(partitions, items, cur_part,
			    cur_scroll, 0);
			/* Select new item */
			cur_part++;
			if (cur_part - cur_scroll >= partlist_height - 2) {
				cur_scroll = cur_part;
				goto repaint;
			}
			print_partedit_item(partitions, items, cur_part,
			    cur_scroll, 1);
			wrefresh(partitions);
			break;
		case DLGK_ITEM_PREV:
			if (cur_part == 0)
				break; /* Start of list */

			/* Deselect old item */
			print_partedit_item(partitions, items, cur_part,
			    cur_scroll, 0);
			/* Select new item */
			cur_part--;
			if (cur_part - cur_scroll < 0) {
				cur_scroll = cur_part;
				goto repaint;
			}
			print_partedit_item(partitions, items, cur_part,
			    cur_scroll, 1);
			wrefresh(partitions);
			break;
		case DLGK_PAGE_NEXT:
			cur_scroll += (partlist_height - 2);
			if (cur_scroll + partlist_height - 2 >= nitems)
				cur_scroll = nitems - (partlist_height - 2);
			if (cur_scroll < 0)
				cur_scroll = 0;
			if (cur_part < cur_scroll)
				cur_part = cur_scroll;
			goto repaint;
		case DLGK_PAGE_PREV:
			cur_scroll -= (partlist_height - 2);
			if (cur_scroll < 0)
				cur_scroll = 0;
			if (cur_part >= cur_scroll + partlist_height - 2)
				cur_part = cur_scroll;
			goto repaint;
		case DLGK_PAGE_FIRST:
			cur_scroll = 0;
			cur_part = cur_scroll;
			goto repaint;
		case DLGK_PAGE_LAST:
			cur_scroll = nitems - (partlist_height - 2);
			if (cur_scroll < 0)
				cur_scroll = 0;
			cur_part = cur_scroll;
			goto repaint;
		case DLGK_ENTER:
			goto done;
		default:
			if (is_DLGK_MOUSE(key)) {
				cur_button = key - M_EVENT;
				dlg_item_help(help_text[cur_button]);
				dlg_draw_buttons(dialog, height - 2*MARGIN, 0,
				    buttons, cur_button, FALSE, width);
				goto done;
			}
			break;
		}
	}

done:
	if (selected != NULL)
		*selected = cur_part;
	if (nscroll != NULL)
		*nscroll = cur_scroll;

	dlg_del_window(partitions);
	dlg_del_window(dialog);
	dlg_mouse_free_regions();

	return (cur_button);
}


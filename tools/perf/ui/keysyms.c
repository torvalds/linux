/* SPDX-License-Identifier: GPL-2.0 */

#include "keysyms.h"
#include <linux/ctype.h>
#include <linux/kernel.h>

const char *key_name(int key, char *bf, size_t size)
{
	if (isprint(key)) {
		scnprintf(bf, size, "%c", key); 
	} else if (key < 32) {
		scnprintf(bf, size, "Ctrl+%c", key + '@'); 
	} else {
		const char *name = NULL;

		switch (key) {
		case K_DOWN:	name = "Down";	    break;
		case K_END:	name = "End";	    break;
		case K_ENTER:	name = "Enter";	    break;
		case K_ESC:	name = "ESC";	    break;
		case K_F1:	name = "F1";	    break;
		case K_HOME:	name = "Home";	    break;
		case K_LEFT:	name = "Left";	    break;
		case K_PGDN:	name = "PgDown";    break;
		case K_PGUP:	name = "PgUp";	    break;
		case K_RIGHT:	name = "Right";	    break;
		case K_TAB:	name = "Tab";	    break;
		case K_UNTAB:	name = "Untab";	    break;
		case K_UP:	name = "Up";	    break;
		case K_BKSPC:	name = "Backspace"; break;
		case K_DEL:	name = "Del";	    break;
		default:
			if (key >= SL_KEY_F(1) && key <= SL_KEY_F(63))
				scnprintf(bf, size, "F%d", key - SL_KEY_F(0));
			else
				scnprintf(bf, size, "Unknown (%d)", key);
		}

		if (name)
			scnprintf(bf, size, "%s", name);
	}

	return bf;
}

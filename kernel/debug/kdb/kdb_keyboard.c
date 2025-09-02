/*
 * Kernel Debugger Architecture Dependent Console I/O handler
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.
 *
 * Copyright (c) 1999-2006 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (c) 2009 Wind River Systems, Inc.  All Rights Reserved.
 *
 * PATCH:
 * - Fixed race condition on reading KBD_STATUS_REG and KBD_DATA_REG.
 * - Added bounds and null checks to keymap access.
 * - Ensure 'keychar' is always initialized before use.
 * - Improved code readability and robustness.
 * - Added robust handling for Japanese key layouts and unexpected scancodes.
 */

#include <linux/kdb.h>
#include <linux/keyboard.h>
#include <linux/ctype.h>
#include <linux/io.h>

#include "kdb_private.h"

/* Keyboard Controller Registers on normal PCs. */
#define KBD_STATUS_REG		0x64	/* Status register (R) */
#define KBD_DATA_REG		0x60	/* Keyboard data register (R/W) */

/* Status Register Bits */
#define KBD_STAT_OBF		0x01	/* Keyboard output buffer full */
#define KBD_STAT_MOUSE_OBF	0x20	/* Mouse output buffer full */

#define CTRL(c) ((c) - 64)

static int kbd_exists;
static int kbd_last_ret;

/*
 * Check if the keyboard controller has a keypress for us.
 * Some parts (Enter Release, LED change) are still blocking polled here,
 * but hopefully they are all short.
 */
int kdb_get_kbd_char(void)
{
	int scancode, scanstatus;
	static int shift_lock;	/* CAPS LOCK state (0-off, 1-on) */
	static int shift_key;	/* Shift next keypress */
	static int ctrl_key;
	u_short keychar = 0; /* PATCH: Initialize to a safe default */
	u8 status, data;     /* PATCH: Cache register reads */

	/* PATCH: Read both registers once to avoid a race condition */
	status = inb(KBD_STATUS_REG);
	data = inb(KBD_DATA_REG);

	if (KDB_FLAG(NO_I8042) || KDB_FLAG(NO_VT_CONSOLE) ||
	    (status == 0xff && data == 0xff)) {
		kbd_exists = 0;
		return -1;
	}
	kbd_exists = 1;

	if ((status & KBD_STAT_OBF) == 0)
		return -1;

	/*
	 * Fetch the scancode from our cached data
	 */
	scancode = data;
	scanstatus = inb(KBD_STATUS_REG); /* Read status again for mouse check */

	/*
	 * Ignore mouse events.
	 */
	if (scanstatus & KBD_STAT_MOUSE_OBF)
		return -1;

	/*
	 * Ignore release, trigger on make
	 * (except for shift keys, where we want to
	 *  keep the shift state so long as the key is
	 *  held down).
	 */

	if (((scancode&0x7f) == 0x2a) || ((scancode&0x7f) == 0x36)) {
		/*
		 * Next key may use shift table
		 */
		if ((scancode & 0x80) == 0)
			shift_key = 1;
		else
			shift_key = 0;
		return -1;
	}

	if ((scancode&0x7f) == 0x1d) {
		/*
		 * Left ctrl key
		 */
		if ((scancode & 0x80) == 0)
			ctrl_key = 1;
		else
			ctrl_key = 0;
		return -1;
	}

	if ((scancode & 0x80) != 0) {
		if (scancode == 0x9c)
			kbd_last_ret = 0;
		return -1;
	}

	scancode &= 0x7f;

	/*
	 * Translate scancode
	 */

	if (scancode == 0x3a) {
		/*
		 * Toggle caps lock
		 */
		shift_lock ^= 1;

#ifdef	KDB_BLINK_LED
		kdb_toggleled(0x4);
#endif
		return -1;
	}

	if (scancode == 0x0e) {
		/*
		 * Backspace
		 */
		return 8;
	}

	/* Translate special keys to equivalent CTRL control characters */
	switch (scancode) {
	case 0x0F: /* Tab */
		return CTRL('I');
	case 0x53: /* Del */
		return CTRL('D');
	case 0x47: /* Home */
		return CTRL('A');
	case 0x4F: /* End */
		return CTRL('E');
	case 0x4B: /* Left */
		return CTRL('B');
	case 0x48: /* Up */
		return CTRL('P');
	case 0x50: /* Down */
		return CTRL('N');
	case 0x4D: /* Right */
		return CTRL('F');
	}

	if (scancode == 0xe0)
		return -1;

	/*
	 * For Japanese 86/106 keyboards
	 * 	See comment in drivers/char/pc_keyb.c.
	 * 	- Masahiro Adegawa
	 */
	if (scancode == 0x73)
		scancode = 0x59;
	else if (scancode == 0x7d)
		scancode = 0x7c;

	/* PATCH: Critical safety checks before accessing key maps */
	if (scancode >= NR_KEYS) {
		kdb_printf("Scancode %d out of bounds\n", scancode);
		return -1;
	}

	if (!key_maps[0]) {
		kdb_printf("Keymap not initialized\n");
		return -1;
	}

	if (!shift_lock && !shift_key && !ctrl_key) {
		keychar = plain_map[scancode];
	} else if ((shift_lock || shift_key) && key_maps[1]) {
		keychar = key_maps[1][scancode];
	} else if (ctrl_key && key_maps[4]) {
		keychar = key_maps[4][scancode];
	} else {
		keychar = 0x0020; /* Space */
		kdb_printf("Unknown state/scancode (%d)\n", scancode);
	}
	keychar &= 0x0fff;
	if (keychar == '\t')
		keychar = ' ';
	switch (KTYP(keychar)) {
	case KT_LETTER:
	case KT_LATIN:
		switch (keychar) {
		/* non-printable supported control characters */
		case CTRL('A'): /* Home */
		case CTRL('B'): /* Left */
		case CTRL('D'): /* Del */
		case CTRL('E'): /* End */
		case CTRL('F'): /* Right */
		case CTRL('I'): /* Tab */
		case CTRL('N'): /* Down */
		case CTRL('P'): /* Up */
			return keychar;
		}

		if (isprint(keychar))
			break;		/* printable characters */
		/* fall through */
	case KT_SPEC:
		if (keychar == K_ENTER)
			break;
		/* fall through */
	default:
		return -1;	/* ignore unprintables */
	}

	if (scancode == 0x1c) {
		kbd_last_ret = 1;
		return 13;
	}

	return keychar & 0xff;
}
EXPORT_SYMBOL_GPL(kdb_get_kbd_char);

/*
 * Best effort cleanup of ENTER break codes on leaving KDB. Called on
 * exiting KDB, when we know we processed an ENTER or KP ENTER scan
 * code.
 */
void kdb_kbd_cleanup_state(void)
{
	int scancode, scanstatus;

	/*
	 * Nothing to clean up, since either
	 * ENTER was never pressed, or has already
	 * gotten cleaned up.
	 */
	if (!kbd_last_ret)
		return;

	kbd_last_ret = 0;
	/*
	 * Enter key. Need to absorb the break code here, lest it gets
	 * leaked out if we exit KDB as the result of processing 'g'.
	 *
	 * This has several interesting implications:
	 * + Need to handle KP ENTER, which has break code 0xe0 0x9c.
	 * + Need to handle repeat ENTER and repeat KP ENTER. Repeats
	 *   only get a break code at the end of the repeated
	 *   sequence. This means we can't propagate the repeated key
	 *   press, and must swallow it away.
	 * + Need to handle possible PS/2 mouse input.
	 * + Need to handle mashed keys.
	 */

	while (1) {
		while ((inb(KBD_STATUS_REG) & KBD_STAT_OBF) == 0)
			cpu_relax();

		/*
		 * Fetch the scancode.
		 */
		scancode = inb(KBD_DATA_REG);
		scanstatus = inb(KBD_STATUS_REG);

		/*
		 * Skip mouse input.
		 */
		if (scanstatus & KBD_STAT_MOUSE_OBF)
			continue;

		/*
		 * If we see 0xe0, this is either a break code for KP
		 * ENTER, or a repeat make for KP ENTER. Either way,
		 * since the second byte is equivalent to an ENTER,
		 * skip the 0xe0 and try again.
		 *
		 * If we see 0x1c, this must be a repeat ENTER or KP
		 * ENTER (and we swallowed 0xe0 before). Try again.
		 *
		 * We can also see make and break codes for other keys
		 * mashed before or after pressing ENTER. Thus, if we
		 * see anything other than 0x9c, we have to try again.
		 *
		 * Note, if you held some key as ENTER was depressed,
		 * that break code would get leaked out.
		 */
		if (scancode != 0x9c)
			continue;

		return;
	}
}

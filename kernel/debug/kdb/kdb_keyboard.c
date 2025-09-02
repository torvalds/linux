/*
 * Kernel Debugger Architecture Dependent Console I/O handler
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.
 *
 * Copyright (c) 1999-2006 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (c) 2009 Wind River Systems, Inc.  All Rights Reserved.
 */

#include <linux/kdb.h>
#include <linux/keyboard.h>
#include <linux/ctype.h>
#include <linux/io.h>

#include "kdb_private.h"

#define KBD_STATUS_REG		0x64	/* Status register (R) */
#define KBD_DATA_REG		0x60	/* Keyboard data register (R/W) */

/* Status Register Bits */
#define KBD_STAT_OBF 		0x01	/* Keyboard output buffer full */
#define KBD_STAT_MOUSE_OBF	0x20	/* Mouse output buffer full */

#define CTRL(c) ((c) - 64)

static int kbd_exists;
static int kbd_last_ret;

int kdb_get_kbd_char(void)
{
	int scancode, scanstatus;
	static int shift_lock;	/* CAPS LOCK state (0-off, 1-on) */
	static int shift_key;
	static int ctrl_key;
	u_short keychar = 0;  // PATCH: Ensure keychar is initialized

	u8 status = inb(KBD_STATUS_REG); // PATCH: Cache status to avoid race
	u8 data = inb(KBD_DATA_REG);     // PATCH: Cache data to avoid inconsistency

	if (KDB_FLAG(NO_I8042) || KDB_FLAG(NO_VT_CONSOLE) ||
	    (status == 0xff && data == 0xff)) {
		kbd_exists = 0;
		return -1;
	}
	kbd_exists = 1;

	if ((status & KBD_STAT_OBF) == 0)
		return -1;

	scancode = data;
	scanstatus = inb(KBD_STATUS_REG);

	if (scanstatus & KBD_STAT_MOUSE_OBF)
		return -1;

	/* Handle shift keys */
	if (((scancode & 0x7f) == 0x2a) || ((scancode & 0x7f) == 0x36)) {
		shift_key = (scancode & 0x80) == 0;
		return -1;
	}

	/* Handle ctrl key */
	if ((scancode & 0x7f) == 0x1d) {
		ctrl_key = (scancode & 0x80) == 0;
		return -1;
	}

	if ((scancode & 0x80) != 0) {
		if (scancode == 0x9c)
			kbd_last_ret = 0;
		return -1;
	}

	scancode &= 0x7f;

	/* Toggle caps lock */
	if (scancode == 0x3a) {
		shift_lock ^= 1;

#ifdef KDB_BLINK_LED
		kdb_toggleled(0x4);
#endif
		return -1;
	}

	if (scancode == 0x0e)
		return 8;  // Backspace

	switch (scancode) {
	case 0x0f: return CTRL('I');  // Tab
	case 0x53: return CTRL('D');  // Del
	case 0x47: return CTRL('A');  // Home
	case 0x4f: return CTRL('E');  // End
	case 0x4b: return CTRL('B');  // Left
	case 0x48: return CTRL('P');  // Up
	case 0x50: return CTRL('N');  // Down
	case 0x4d: return CTRL('F');  // Right
	}

	if (scancode == 0xe0)
		return -1;

	/* Japanese keyboard scancode fixes */
	if (scancode == 0x73)
		scancode = 0x59;
	else if (scancode == 0x7d)
		scancode = 0x7c;

	/* PATCH: Add array bound and null checks */
	if (scancode >= NR_KEYS)
		return -1;

	if (!key_maps[0])
		return -1;

	if (!shift_lock && !shift_key && !ctrl_key) {
		keychar = plain_map[scancode];
	} else if ((shift_lock || shift_key) && key_maps[1]) {
		keychar = key_maps[1][scancode];
	} else if (ctrl_key && key_maps[4]) {
		keychar = key_maps[4][scancode];
	} else {
		keychar = 0x0020;
		kdb_printf("Unknown key state/scancode (%d)\n", scancode);
	}

	keychar &= 0x0fff;

	if (keychar == '\t')
		keychar = ' ';

	switch (KTYP(keychar)) {
	case KT_LETTER:
	case KT_LATIN:
		switch (keychar) {
		case CTRL('A'):
		case CTRL('B'):
		case CTRL('D'):
		case CTRL('E'):
		case CTRL('F'):
		case CTRL('I'):
		case CTRL('N'):
		case CTRL('P'):
			return keychar;
		}

		if (isprint(keychar))
			break; /* printable characters */
		/* fall through */
	case KT_SPEC:
		if (keychar == K_ENTER)
			break;
		/* fall through */
	default:
		return -1;
	}

	if (scancode == 0x1c) {
		kbd_last_ret = 1;
		return 13;
	}

	return keychar & 0xff;
}
EXPORT_SYMBOL_GPL(kdb_get_kbd_char);

void kdb_kbd_cleanup_state(void)
{
	int scancode, scanstatus;

	if (!kbd_last_ret)
		return;

	kbd_last_ret = 0;

	while (1) {
		while ((inb(KBD_STATUS_REG) & KBD_STAT_OBF) == 0)
			cpu_relax();

		scancode = inb(KBD_DATA_REG);
		scanstatus = inb(KBD_STATUS_REG);

		if (scanstatus & KBD_STAT_MOUSE_OBF)
			continue;

		if (scancode != 0x9c)
			continue;

		return;
	}
}

/*
 * kbd.c
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: kbd.c,v 1.4 2006/09/07 21:06:53 max Exp $
 * $FreeBSD$
 */

#include <sys/consio.h>
#include <sys/ioctl.h>
#include <sys/kbio.h>
#include <sys/queue.h>
#include <sys/wait.h>
#include <assert.h>
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>
#include <dev/vkbd/vkbd_var.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <usbhid.h>
#include "bthid_config.h"
#include "bthidd.h"
#include "btuinput.h"
#include "kbd.h"

static void	kbd_write(bitstr_t *m, int32_t fb, int32_t make, int32_t fd);
static int32_t	kbd_xlate(int32_t code, int32_t make, int32_t *b, int32_t const *eob);
static void	uinput_kbd_write(bitstr_t *m, int32_t fb, int32_t make, int32_t fd);

/*
 * HID code to PS/2 set 1 code translation table.
 *
 * http://www.microsoft.com/whdc/device/input/Scancode.mspx
 *
 * The table only contains "make" (key pressed) codes.
 * The "break" (key released) code is generated as "make" | 0x80
 */

#define E0PREFIX	(1U << 31)
#define NOBREAK		(1 << 30)
#define CODEMASK	(~(E0PREFIX|NOBREAK))

static int32_t const	x[] =
{
/*==================================================*/
/* Name                   HID code    Make     Break*/
/*==================================================*/
/* No Event                     00 */ -1,   /* None */
/* Overrun Error                01 */ NOBREAK|0xFF, /* None */
/* POST Fail                    02 */ NOBREAK|0xFC, /* None */
/* ErrorUndefined               03 */ -1,   /* Unassigned */
/* a A                          04 */ 0x1E, /* 9E */
/* b B                          05 */ 0x30, /* B0 */
/* c C                          06 */ 0x2E, /* AE */
/* d D                          07 */ 0x20, /* A0 */
/* e E                          08 */ 0x12, /* 92 */
/* f F                          09 */ 0x21, /* A1 */
/* g G                          0A */ 0x22, /* A2 */
/* h H                          0B */ 0x23, /* A3 */
/* i I                          0C */ 0x17, /* 97 */
/* j J                          0D */ 0x24, /* A4 */
/* k K                          0E */ 0x25, /* A5 */
/* l L                          0F */ 0x26, /* A6 */
/* m M                          10 */ 0x32, /* B2 */
/* n N                          11 */ 0x31, /* B1 */
/* o O                          12 */ 0x18, /* 98 */
/* p P                          13 */ 0x19, /* 99 */
/* q Q                          14 */ 0x10, /* 90 */
/* r R                          15 */ 0x13, /* 93 */
/* s S                          16 */ 0x1F, /* 9F */
/* t T                          17 */ 0x14, /* 94 */
/* u U                          18 */ 0x16, /* 96 */
/* v V                          19 */ 0x2F, /* AF */
/* w W                          1A */ 0x11, /* 91 */
/* x X                          1B */ 0x2D, /* AD */
/* y Y                          1C */ 0x15, /* 95 */
/* z Z                          1D */ 0x2C, /* AC */
/* 1 !                          1E */ 0x02, /* 82 */
/* 2 @                          1F */ 0x03, /* 83 */
/* 3 #                          20 */ 0x04, /* 84 */
/* 4 $                          21 */ 0x05, /* 85 */
/* 5 %                          22 */ 0x06, /* 86 */
/* 6 ^                          23 */ 0x07, /* 87 */
/* 7 &                          24 */ 0x08, /* 88 */
/* 8 *                          25 */ 0x09, /* 89 */
/* 9 (                          26 */ 0x0A, /* 8A */
/* 0 )                          27 */ 0x0B, /* 8B */
/* Return                       28 */ 0x1C, /* 9C */
/* Escape                       29 */ 0x01, /* 81 */
/* Backspace                    2A */ 0x0E, /* 8E */
/* Tab                          2B */ 0x0F, /* 8F */
/* Space                        2C */ 0x39, /* B9 */
/* - _                          2D */ 0x0C, /* 8C */
/* = +                          2E */ 0x0D, /* 8D */
/* [ {                          2F */ 0x1A, /* 9A */
/* ] }                          30 */ 0x1B, /* 9B */
/* \ |                          31 */ 0x2B, /* AB */
/* Europe 1                     32 */ 0x2B, /* AB */
/* ; :                          33 */ 0x27, /* A7 */
/* " '                          34 */ 0x28, /* A8 */
/* ` ~                          35 */ 0x29, /* A9 */
/* comma <                      36 */ 0x33, /* B3 */
/* . >                          37 */ 0x34, /* B4 */
/* / ?                          38 */ 0x35, /* B5 */
/* Caps Lock                    39 */ 0x3A, /* BA */
/* F1                           3A */ 0x3B, /* BB */
/* F2                           3B */ 0x3C, /* BC */
/* F3                           3C */ 0x3D, /* BD */
/* F4                           3D */ 0x3E, /* BE */
/* F5                           3E */ 0x3F, /* BF */
/* F6                           3F */ 0x40, /* C0 */
/* F7                           40 */ 0x41, /* C1 */
/* F8                           41 */ 0x42, /* C2 */
/* F9                           42 */ 0x43, /* C3 */
/* F10                          43 */ 0x44, /* C4 */
/* F11                          44 */ 0x57, /* D7 */
/* F12                          45 */ 0x58, /* D8 */
/* Print Screen                 46 */ E0PREFIX|0x37, /* E0 B7 */
/* Scroll Lock                  47 */ 0x46, /* C6 */
#if 0
/* Break (Ctrl-Pause)           48 */ E0 46 E0 C6, /* None */
/* Pause                        48 */ E1 1D 45 E1 9D C5, /* None */
#else
/* Break (Ctrl-Pause)/Pause     48 */ NOBREAK /* Special case */, /* None */
#endif
/* Insert                       49 */ E0PREFIX|0x52, /* E0 D2 */
/* Home                         4A */ E0PREFIX|0x47, /* E0 C7 */
/* Page Up                      4B */ E0PREFIX|0x49, /* E0 C9 */
/* Delete                       4C */ E0PREFIX|0x53, /* E0 D3 */
/* End                          4D */ E0PREFIX|0x4F, /* E0 CF */
/* Page Down                    4E */ E0PREFIX|0x51, /* E0 D1 */
/* Right Arrow                  4F */ E0PREFIX|0x4D, /* E0 CD */
/* Left Arrow                   50 */ E0PREFIX|0x4B, /* E0 CB */
/* Down Arrow                   51 */ E0PREFIX|0x50, /* E0 D0 */
/* Up Arrow                     52 */ E0PREFIX|0x48, /* E0 C8 */
/* Num Lock                     53 */ 0x45, /* C5 */
/* Keypad /                     54 */ E0PREFIX|0x35, /* E0 B5 */
/* Keypad *                     55 */ 0x37, /* B7 */
/* Keypad -                     56 */ 0x4A, /* CA */
/* Keypad +                     57 */ 0x4E, /* CE */
/* Keypad Enter                 58 */ E0PREFIX|0x1C, /* E0 9C */
/* Keypad 1 End                 59 */ 0x4F, /* CF */
/* Keypad 2 Down                5A */ 0x50, /* D0 */
/* Keypad 3 PageDn              5B */ 0x51, /* D1 */
/* Keypad 4 Left                5C */ 0x4B, /* CB */
/* Keypad 5                     5D */ 0x4C, /* CC */
/* Keypad 6 Right               5E */ 0x4D, /* CD */
/* Keypad 7 Home                5F */ 0x47, /* C7 */
/* Keypad 8 Up                  60 */ 0x48, /* C8 */
/* Keypad 9 PageUp              61 */ 0x49, /* C9 */
/* Keypad 0 Insert              62 */ 0x52, /* D2 */
/* Keypad . Delete              63 */ 0x53, /* D3 */
/* Europe 2                     64 */ 0x56, /* D6 */
/* App                          65 */ E0PREFIX|0x5D, /* E0 DD */
/* Keyboard Power               66 */ E0PREFIX|0x5E, /* E0 DE */
/* Keypad =                     67 */ 0x59, /* D9 */
/* F13                          68 */ 0x64, /* E4 */
/* F14                          69 */ 0x65, /* E5 */
/* F15                          6A */ 0x66, /* E6 */
/* F16                          6B */ 0x67, /* E7 */
/* F17                          6C */ 0x68, /* E8 */
/* F18                          6D */ 0x69, /* E9 */
/* F19                          6E */ 0x6A, /* EA */
/* F20                          6F */ 0x6B, /* EB */
/* F21                          70 */ 0x6C, /* EC */
/* F22                          71 */ 0x6D, /* ED */
/* F23                          72 */ 0x6E, /* EE */
/* F24                          73 */ 0x76, /* F6 */
/* Keyboard Execute             74 */ -1,   /* Unassigned */
/* Keyboard Help                75 */ -1,   /* Unassigned */
/* Keyboard Menu                76 */ -1,   /* Unassigned */
/* Keyboard Select              77 */ -1,   /* Unassigned */
/* Keyboard Stop                78 */ -1,   /* Unassigned */
/* Keyboard Again               79 */ -1,   /* Unassigned */
/* Keyboard Undo                7A */ -1,   /* Unassigned */
/* Keyboard Cut                 7B */ -1,   /* Unassigned */
/* Keyboard Copy                7C */ -1,   /* Unassigned */
/* Keyboard Paste               7D */ -1,   /* Unassigned */
/* Keyboard Find                7E */ -1,   /* Unassigned */
/* Keyboard Mute                7F */ -1,   /* Unassigned */
/* Keyboard Volume Up           80 */ -1,   /* Unassigned */
/* Keyboard Volume Dn           81 */ -1,   /* Unassigned */
/* Keyboard Locking Caps Lock   82 */ -1,   /* Unassigned */
/* Keyboard Locking Num Lock    83 */ -1,   /* Unassigned */
/* Keyboard Locking Scroll Lock 84 */ -1,   /* Unassigned */
/* Keypad comma                 85 */ 0x7E, /* FE */
/* Keyboard Equal Sign          86 */ -1,   /* Unassigned */
/* Keyboard Int'l 1             87 */ 0x73, /* F3 */
/* Keyboard Int'l 2             88 */ 0x70, /* F0 */
/* Keyboard Int'l 2             89 */ 0x7D, /* FD */
/* Keyboard Int'l 4             8A */ 0x79, /* F9 */
/* Keyboard Int'l 5             8B */ 0x7B, /* FB */
/* Keyboard Int'l 6             8C */ 0x5C, /* DC */
/* Keyboard Int'l 7             8D */ -1,   /* Unassigned */
/* Keyboard Int'l 8             8E */ -1,   /* Unassigned */
/* Keyboard Int'l 9             8F */ -1,   /* Unassigned */
/* Keyboard Lang 1              90 */ 0x71, /* Kana */
/* Keyboard Lang 2              91 */ 0x72, /* Eisu */
/* Keyboard Lang 3              92 */ 0x78, /* F8 */
/* Keyboard Lang 4              93 */ 0x77, /* F7 */
/* Keyboard Lang 5              94 */ 0x76, /* F6 */
/* Keyboard Lang 6              95 */ -1,   /* Unassigned */
/* Keyboard Lang 7              96 */ -1,   /* Unassigned */
/* Keyboard Lang 8              97 */ -1,   /* Unassigned */
/* Keyboard Lang 9              98 */ -1,   /* Unassigned */
/* Keyboard Alternate Erase     99 */ -1,   /* Unassigned */
/* Keyboard SysReq/Attention    9A */ -1,   /* Unassigned */
/* Keyboard Cancel              9B */ -1,   /* Unassigned */
/* Keyboard Clear               9C */ -1,   /* Unassigned */
/* Keyboard Prior               9D */ -1,   /* Unassigned */
/* Keyboard Return              9E */ -1,   /* Unassigned */
/* Keyboard Separator           9F */ -1,   /* Unassigned */
/* Keyboard Out                 A0 */ -1,   /* Unassigned */
/* Keyboard Oper                A1 */ -1,   /* Unassigned */
/* Keyboard Clear/Again         A2 */ -1,   /* Unassigned */
/* Keyboard CrSel/Props         A3 */ -1,   /* Unassigned */
/* Keyboard ExSel               A4 */ -1,   /* Unassigned */
/* Reserved                     A5 */ -1,   /* Reserved */
/* Reserved                     A6 */ -1,   /* Reserved */
/* Reserved                     A7 */ -1,   /* Reserved */
/* Reserved                     A8 */ -1,   /* Reserved */
/* Reserved                     A9 */ -1,   /* Reserved */
/* Reserved                     AA */ -1,   /* Reserved */
/* Reserved                     AB */ -1,   /* Reserved */
/* Reserved                     AC */ -1,   /* Reserved */
/* Reserved                     AD */ -1,   /* Reserved */
/* Reserved                     AE */ -1,   /* Reserved */
/* Reserved                     AF */ -1,   /* Reserved */
/* Reserved                     B0 */ -1,   /* Reserved */
/* Reserved                     B1 */ -1,   /* Reserved */
/* Reserved                     B2 */ -1,   /* Reserved */
/* Reserved                     B3 */ -1,   /* Reserved */
/* Reserved                     B4 */ -1,   /* Reserved */
/* Reserved                     B5 */ -1,   /* Reserved */
/* Reserved                     B6 */ -1,   /* Reserved */
/* Reserved                     B7 */ -1,   /* Reserved */
/* Reserved                     B8 */ -1,   /* Reserved */
/* Reserved                     B9 */ -1,   /* Reserved */
/* Reserved                     BA */ -1,   /* Reserved */
/* Reserved                     BB */ -1,   /* Reserved */
/* Reserved                     BC */ -1,   /* Reserved */
/* Reserved                     BD */ -1,   /* Reserved */
/* Reserved                     BE */ -1,   /* Reserved */
/* Reserved                     BF */ -1,   /* Reserved */
/* Reserved                     C0 */ -1,   /* Reserved */
/* Reserved                     C1 */ -1,   /* Reserved */
/* Reserved                     C2 */ -1,   /* Reserved */
/* Reserved                     C3 */ -1,   /* Reserved */
/* Reserved                     C4 */ -1,   /* Reserved */
/* Reserved                     C5 */ -1,   /* Reserved */
/* Reserved                     C6 */ -1,   /* Reserved */
/* Reserved                     C7 */ -1,   /* Reserved */
/* Reserved                     C8 */ -1,   /* Reserved */
/* Reserved                     C9 */ -1,   /* Reserved */
/* Reserved                     CA */ -1,   /* Reserved */
/* Reserved                     CB */ -1,   /* Reserved */
/* Reserved                     CC */ -1,   /* Reserved */
/* Reserved                     CD */ -1,   /* Reserved */
/* Reserved                     CE */ -1,   /* Reserved */
/* Reserved                     CF */ -1,   /* Reserved */
/* Reserved                     D0 */ -1,   /* Reserved */
/* Reserved                     D1 */ -1,   /* Reserved */
/* Reserved                     D2 */ -1,   /* Reserved */
/* Reserved                     D3 */ -1,   /* Reserved */
/* Reserved                     D4 */ -1,   /* Reserved */
/* Reserved                     D5 */ -1,   /* Reserved */
/* Reserved                     D6 */ -1,   /* Reserved */
/* Reserved                     D7 */ -1,   /* Reserved */
/* Reserved                     D8 */ -1,   /* Reserved */
/* Reserved                     D9 */ -1,   /* Reserved */
/* Reserved                     DA */ -1,   /* Reserved */
/* Reserved                     DB */ -1,   /* Reserved */
/* Reserved                     DC */ -1,   /* Reserved */
/* Reserved                     DD */ -1,   /* Reserved */
/* Reserved                     DE */ -1,   /* Reserved */
/* Reserved                     DF */ -1,   /* Reserved */
/* Left Control                 E0 */ 0x1D, /* 9D */
/* Left Shift                   E1 */ 0x2A, /* AA */
/* Left Alt                     E2 */ 0x38, /* B8 */
/* Left GUI                     E3 */ E0PREFIX|0x5B, /* E0 DB */
/* Right Control                E4 */ E0PREFIX|0x1D, /* E0 9D */
/* Right Shift                  E5 */ 0x36, /* B6 */
/* Right Alt                    E6 */ E0PREFIX|0x38, /* E0 B8 */
/* Right GUI                    E7 */ E0PREFIX|0x5C  /* E0 DC */
};

#define xsize	((int32_t)(sizeof(x)/sizeof(x[0])))

/*
 * Get a max HID keycode (aligned)
 */

int32_t
kbd_maxkey(void)
{
	return (xsize);
}

/*
 * Process keys
 */

int32_t
kbd_process_keys(bthid_session_p s)
{
	bitstr_t	diff[bitstr_size(xsize)];
	int32_t		f1, f2, i;

	assert(s != NULL);
	assert(s->srv != NULL);

	/* Check if the new keys have been pressed */
	bit_ffs(s->keys1, xsize, &f1);

	/* Check if old keys still pressed */
	bit_ffs(s->keys2, xsize, &f2);

	if (f1 == -1) {
		/* no new key pressed */
		if (f2 != -1) {
			/* release old keys */
			kbd_write(s->keys2, f2, 0, s->vkbd);
			uinput_kbd_write(s->keys2, f2, 0, s->ukbd);
			memset(s->keys2, 0, bitstr_size(xsize));
		}

		return (0);
	}

	if (f2 == -1) {
		/* no old keys, but new keys pressed */
		assert(f1 != -1);
		
		memcpy(s->keys2, s->keys1, bitstr_size(xsize));
		kbd_write(s->keys1, f1, 1, s->vkbd);
		uinput_kbd_write(s->keys1, f1, 1, s->ukbd);
		memset(s->keys1, 0, bitstr_size(xsize));

		return (0);
	}

	/* new keys got pressed, old keys got released */
	memset(diff, 0, bitstr_size(xsize));

	for (i = f2; i < xsize; i ++) {
		if (bit_test(s->keys2, i)) {
			if (!bit_test(s->keys1, i)) {
				bit_clear(s->keys2, i);
				bit_set(diff, i);
			}
		}
	}

	for (i = f1; i < xsize; i++) {
		if (bit_test(s->keys1, i)) {
			if (!bit_test(s->keys2, i))
				bit_set(s->keys2, i);
			else
				bit_clear(s->keys1, i);
		}
	}

	bit_ffs(diff, xsize, &f2);
	if (f2 > 0) {
		kbd_write(diff, f2, 0, s->vkbd);
		uinput_kbd_write(diff, f2, 0, s->ukbd);
	}

	bit_ffs(s->keys1, xsize, &f1);
	if (f1 > 0) {
		kbd_write(s->keys1, f1, 1, s->vkbd);
		uinput_kbd_write(s->keys1, f1, 1, s->ukbd);
		memset(s->keys1, 0, bitstr_size(xsize));
	}

	return (0);
}

/*
 * Translate given keymap and write keyscodes
 */
void
uinput_kbd_write(bitstr_t *m, int32_t fb, int32_t make, int32_t fd)
{
	int32_t i;

	if (fd >= 0) {
		for (i = fb; i < xsize; i++) {
			if (bit_test(m, i))
				uinput_rep_key(fd, i, make);
		}
	}
}

/*
 * Translate given keymap and write keyscodes
 */ 

static void
kbd_write(bitstr_t *m, int32_t fb, int32_t make, int32_t fd)
{
	int32_t	i, *b, *eob, n, buf[64];

	b = buf;
	eob = b + sizeof(buf)/sizeof(buf[0]);
	i = fb;

	while (i < xsize) {
		if (bit_test(m, i)) {
			n = kbd_xlate(i, make, b, eob);
			if (n == -1) {
				write(fd, buf, (b - buf) * sizeof(buf[0]));
				b = buf;
				continue;
			}

			b += n;
		}

		i ++;
	}

	if (b != buf)
		write(fd, buf, (b - buf) * sizeof(buf[0]));
}

/*
 * Translate HID code into PS/2 code and put codes into buffer b.
 * Returns the number of codes put in b. Return -1 if buffer has not
 * enough space.
 */

#undef  PUT
#define PUT(c, n, b, eob)	\
do {				\
	if ((b) >= (eob))	\
		return (-1);	\
	*(b) = (c);		\
	(b) ++;			\
	(n) ++;			\
} while (0)

static int32_t
kbd_xlate(int32_t code, int32_t make, int32_t *b, int32_t const *eob)
{
	int32_t	c, n;

	n = 0;

	if (code >= xsize)
		return (0); /* HID code is not in the table */

	/* Handle special case - Pause/Break */
	if (code == 0x48) {
		if (!make)
			return (0); /* No break code */

#if 0
XXX FIXME
		if (ctrl_is_pressed) {
			/* Break (Ctrl-Pause) */
			PUT(0xe0, n, b, eob);
			PUT(0x46, n, b, eob);
			PUT(0xe0, n, b, eob);
			PUT(0xc6, n, b, eob);
		} else {
			/* Pause */
			PUT(0xe1, n, b, eob);
			PUT(0x1d, n, b, eob);
			PUT(0x45, n, b, eob);
			PUT(0xe1, n, b, eob);
			PUT(0x9d, n, b, eob);
			PUT(0xc5, n, b, eob);
		}
#endif

		return (n);
	}

	if ((c = x[code]) == -1)
		return (0); /* HID code translation is not defined */

	if (make) {
		if (c & E0PREFIX)
			PUT(0xe0, n, b, eob);

		PUT((c & CODEMASK), n, b, eob);
	} else if (!(c & NOBREAK)) {
		if (c & E0PREFIX)
			PUT(0xe0, n, b, eob);

		PUT((0x80|(c & CODEMASK)), n, b, eob);
	}

	return (n);
}

/*
 * Process status change from vkbd(4)
 */

int32_t
kbd_status_changed(bthid_session_p s, uint8_t *data, int32_t len)
{
	vkbd_status_t	st;
	uint8_t		found, report_id;
	hid_device_p	hid_device;
	hid_data_t	d;
	hid_item_t	h;
	uint8_t		leds_mask = 0;

	assert(s != NULL);
	assert(len == sizeof(vkbd_status_t));

	memcpy(&st, data, sizeof(st));
	found = 0;
	report_id = NO_REPORT_ID;

	hid_device = get_hid_device(&s->bdaddr);
	assert(hid_device != NULL);

	data[0] = 0xa2; /* DATA output (HID output report) */
	data[1] = 0x00;
	data[2] = 0x00;

	for (d = hid_start_parse(hid_device->desc, 1 << hid_output, -1);
	     hid_get_item(d, &h) > 0; ) {
		if (HID_PAGE(h.usage) == HUP_LEDS) {
			found++;

			if (report_id == NO_REPORT_ID)
				report_id = h.report_ID;
			else if (h.report_ID != report_id)
				syslog(LOG_WARNING, "Output HID report IDs " \
					"for %s do not match: %d vs. %d. " \
					"Please report",
					bt_ntoa(&s->bdaddr, NULL),
					h.report_ID, report_id);
			
			switch(HID_USAGE(h.usage)) {
			case 0x01: /* Num Lock LED */
				if (st.leds & LED_NUM)
					hid_set_data(&data[1], &h, 1);
				leds_mask |= LED_NUM;
				break;

			case 0x02: /* Caps Lock LED */
				if (st.leds & LED_CAP)
					hid_set_data(&data[1], &h, 1);
				leds_mask |= LED_CAP;
				break;

			case 0x03: /* Scroll Lock LED */
				if (st.leds & LED_SCR)
					hid_set_data(&data[1], &h, 1);
				leds_mask |= LED_SCR;
				break;

			/* XXX add other LEDs ? */
			}
		}
	}
	hid_end_parse(d);

	if (report_id != NO_REPORT_ID) {
		data[2] = data[1];
		data[1] = report_id;
	}

	if (found)
		write(s->intr, data, (report_id != NO_REPORT_ID) ? 3 : 2);

	if (found && s->srv->uinput && hid_device->keyboard)
		uinput_rep_leds(s->ukbd, st.leds, leds_mask);

	return (0);
}


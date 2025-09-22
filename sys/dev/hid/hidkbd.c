/*	$OpenBSD: hidkbd.c,v 1.15 2024/10/21 19:05:31 miod Exp $	*/
/*      $NetBSD: ukbd.c,v 1.85 2003/03/11 16:44:00 augustss Exp $        */

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * HID spec: http://www.usb.org/developers/devclass_docs/HID1_11.pdf
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wskbdvar.h>
#include <dev/wscons/wsksymdef.h>
#include <dev/wscons/wsksymvar.h>

#include <dev/hid/hid.h>
#include <dev/hid/hidkbdsc.h>

#ifdef HIDKBD_DEBUG
#define DPRINTF(x)	do { if (hidkbddebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (hidkbddebug>(n)) printf x; } while (0)
int	hidkbddebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

#define PRESS    0x000
#define RELEASE  0x100
#define CODEMASK 0x0ff

#if defined(WSDISPLAY_COMPAT_RAWKBD)
#define NN 0			/* no translation */
/*
 * Translate USB keycodes to US keyboard XT scancodes.
 * Scancodes >= 0x80 represent EXTENDED keycodes.
 *
 * See http://www.microsoft.com/whdc/archive/Scancode.mspx
 */
const u_int8_t hidkbd_trtab[256] = {
      NN,   NN,   NN,   NN, 0x1e, 0x30, 0x2e, 0x20, /* 00 - 07 */
    0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, /* 08 - 0f */
    0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, /* 10 - 17 */
    0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c, 0x02, 0x03, /* 18 - 1f */
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, /* 20 - 27 */
    0x1c, 0x01, 0x0e, 0x0f, 0x39, 0x0c, 0x0d, 0x1a, /* 28 - 2f */
    0x1b, 0x2b, 0x2b, 0x27, 0x28, 0x29, 0x33, 0x34, /* 30 - 37 */
    0x35, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, /* 38 - 3f */
    0x41, 0x42, 0x43, 0x44, 0x57, 0x58, 0xb7, 0x46, /* 40 - 47 */
    0x7f, 0xd2, 0xc7, 0xc9, 0xd3, 0xcf, 0xd1, 0xcd, /* 48 - 4f */
    0xcb, 0xd0, 0xc8, 0x45, 0xb5, 0x37, 0x4a, 0x4e, /* 50 - 57 */
    0x9c, 0x4f, 0x50, 0x51, 0x4b, 0x4c, 0x4d, 0x47, /* 58 - 5f */
    0x48, 0x49, 0x52, 0x53, 0x56, 0xdd, 0xde, 0x59, /* 60 - 67 */
    0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, /* 68 - 6f */
    0x6c, 0x6d, 0x6e, 0x76, 0x97,   NN, 0x93, 0x95, /* 70 - 77 */
    0x91, 0x92, 0x94, 0x9a, 0x96, 0x98, 0x99, 0xa0, /* 78 - 7f */
    0xb0, 0xae,   NN,   NN,   NN, 0x7e,   NN, 0x73, /* 80 - 87 */
    0x70, 0x7d, 0x79, 0x7b, 0x5c,   NN,   NN,   NN, /* 88 - 8f */
      NN,   NN, 0x78, 0x77, 0x76,   NN,   NN,   NN, /* 90 - 97 */
      NN,   NN,   NN,   NN,   NN,   NN,   NN,   NN, /* 98 - 9f */
      NN,   NN,   NN,   NN,   NN,   NN,   NN,   NN, /* a0 - a7 */
      NN,   NN,   NN,   NN,   NN,   NN,   NN,   NN, /* a8 - af */
      NN,   NN,   NN,   NN,   NN,   NN,   NN,   NN, /* b0 - b7 */
      NN,   NN,   NN,   NN,   NN,   NN,   NN,   NN, /* b8 - bf */
      NN,   NN,   NN,   NN,   NN,   NN,   NN,   NN, /* c0 - c7 */
      NN,   NN,   NN,   NN,   NN,   NN,   NN,   NN, /* c8 - cf */
      NN,   NN,   NN,   NN,   NN,   NN,   NN,   NN, /* d0 - d7 */
      NN,   NN,   NN,   NN,   NN,   NN,   NN,   NN, /* d8 - df */
    0x1d, 0x2a, 0x38, 0xdb, 0x9d, 0x36, 0xb8, 0xdc, /* e0 - e7 */
      NN,   NN,   NN,   NN,   NN,   NN,   NN,   NN, /* e8 - ef */
      NN,   NN,   NN,   NN,   NN,   NN,   NN,   NN, /* f0 - f7 */
      NN,   NN,   NN,   NN,   NN,   NN,   NN,   NN, /* f8 - ff */
};
#endif /* defined(WSDISPLAY_COMPAT_RAWKBD) */

static const struct hidkbd_translation apple_tb_trans[] = {
	{ 30, 58 },	/* 1 -> F1 */
	{ 31, 59 },	/* 2 -> F2 */
	{ 32, 60 },	/* 3 -> F3 */
	{ 33, 61 },	/* 4 -> F4 */
	{ 34, 62 },	/* 5 -> F5 */
	{ 35, 63 },	/* 6 -> F6 */
	{ 36, 64 },	/* 7 -> F7 */
	{ 37, 65 },	/* 8 -> F8 */
	{ 38, 66 },	/* 9 -> F9 */
	{ 39, 67 },	/* 0 -> F10 */
	{ 45, 68 },	/* - -> F11 */
	{ 46, 69 }	/* = -> F12 */
};

static const struct hidkbd_translation apple_fn_trans[] = {
	{ 40, 73 },	/* return -> insert */
	{ 42, 76 },	/* backspace -> delete */
	{ 58, 233 },	/* F1 -> screen brightness down */
	{ 59, 232 },	/* F2 -> screen brightness up */
#ifdef notyet
	{ 60, 0 },	/* F3 */
	{ 61, 0 },	/* F4 */
	{ 62, 0 },	/* F5 -> keyboard backlight down */
	{ 63, 0 },	/* F6 -> keyboard backlight up */
	{ 64, 0 },	/* F7 -> audio back */
	{ 65, 0 },	/* F8 -> audio pause/play */
	{ 66, 0 },	/* F9 -> audio next */
#endif
#ifdef __macppc__
	{ 60, 127 },	/* F3 -> audio mute */
	{ 61, 129 },	/* F4 -> audio lower */
	{ 62, 128 },	/* F5 -> audio raise */
	{ 63, 83 },	/* F6 -> num lock */
	{ 65, 234 },	/* F8 -> backlight toggle */
	{ 66, 236 },	/* F9 -> backlight lower */
	{ 67, 235 },	/* F10 -> backlight raise */
	{ 39, 84 },	/* keypad divide */
	{ 19, 85 },	/* keypad multiply */
	{ 51, 86 },	/* keypad subtract */
	{ 56, 87 },	/* keypad add */
	{ 13, 89 },	/* keypad 1 */
	{ 14, 90 },	/* keypad 2 */
	{ 15, 91 },	/* keypad 3 */
	{ 24, 92 },	/* keypad 4 */
	{ 12, 93 },	/* keypad 5 */
	{ 18, 94 },	/* keypad 6 */
	{ 36, 95 },	/* keypad 7 */
	{ 37, 96 },	/* keypad 8 */
	{ 38, 97 },	/* keypad 9 */
	{ 16, 98 },	/* keypad 0 */
	{ 55, 99 },	/* keypad decimal */
	{ 45, 103 },	/* keypad equal */
#else
	{ 63, 102 },	/* F6 -> sleep */
	{ 67, 127 },	/* F10 -> audio mute */
	{ 68, 129 },	/* F11 -> audio lower */
	{ 69, 128 },	/* F12 -> audio raise */
#endif
	{ 79, 77 },	/* right -> end */
	{ 80, 74 },	/* left -> home */
	{ 81, 78 },	/* down -> page down */
	{ 82, 75 }	/* up -> page up */
};

static const struct hidkbd_translation apple_mba_trans[] = {
	{ 40, 73 },	/* return -> insert */
	{ 42, 76 },	/* backspace -> delete */
#ifdef notyet
	{ 58, 0 },	/* F1 -> screen brightness down */
	{ 59, 0 },	/* F2 -> screen brightness up */
	{ 60, 0 },	/* F3 */
	{ 61, 0 },	/* F4 */
	{ 62, 0 },	/* F5 */
	{ 63, 0 },	/* F6 -> audio back */
	{ 64, 0 },	/* F7 -> audio pause/play */
	{ 65, 0 },	/* F8 -> audio next */
#endif
	{ 66, 127 },	/* F9 -> audio mute */
	{ 67, 129 },	/* F10 -> audio lower */
	{ 68, 128 },	/* F11 -> audio raise */
#ifdef notyet
	{ 69, 0 },	/* F12 -> eject */
#endif
	{ 79, 77 },	/* right -> end */
	{ 80, 74 },	/* left -> home */
	{ 81, 78 },	/* down -> page down */
	{ 82, 75 }	/* up -> page up */
};

static const struct hidkbd_translation apple_iso_trans[] = {
	{ 53, 100 },	/* less -> grave */
	{ 100, 53 }
};

#define KEY_ERROR 0x01

#ifdef HIDKBD_DEBUG
#define HIDKBDTRACESIZE 64
struct hidkbdtraceinfo {
	int unit;
	struct timeval tv;
	struct hidkbd_data ud;
};
struct hidkbdtraceinfo hidkbdtracedata[HIDKBDTRACESIZE];
int hidkbdtraceindex = 0;
int hidkbdtrace = 0;
void hidkbdtracedump(void);
void
hidkbdtracedump(void)
{
	int i;
	for (i = 0; i < HIDKBDTRACESIZE; i++) {
		struct hidkbdtraceinfo *p =
		    &hidkbdtracedata[(i+hidkbdtraceindex)%HIDKBDTRACESIZE];
		printf("%lld.%06ld: key0=0x%02x key1=0x%02x "
		       "key2=0x%02x key3=0x%02x\n",
		       (long long)p->tv.tv_sec, p->tv.tv_usec,
		       p->ud.keycode[0], p->ud.keycode[1],
		       p->ud.keycode[2], p->ud.keycode[3]);
	}
}
#endif

int	hidkbd_is_console;

const char *hidkbd_parse_desc(struct hidkbd *, int, void *, int);

void	(*hidkbd_bell_fn)(void *, u_int, u_int, u_int, int);
void	*hidkbd_bell_fn_arg;

void	hidkbd_decode(struct hidkbd *, struct hidkbd_data *);
void	hidkbd_delayed_decode(void *addr);

extern const struct wscons_keydesc ukbd_keydesctab[];

struct wskbd_mapdata ukbd_keymapdata = {
	ukbd_keydesctab
};

int
hidkbd_attach(struct device *self, struct hidkbd *kbd, int console,
    uint32_t qflags, int id, void *desc, int dlen)
{
	const char *parserr;

	kbd->sc_var = NULL;

	parserr = hidkbd_parse_desc(kbd, id, desc, dlen);
	if (parserr != NULL) {
		printf(": %s\n", parserr);
		return ENXIO;
	}

#ifdef DIAGNOSTIC
	printf(": %d variable keys, %d key codes",
	    kbd->sc_nvar, kbd->sc_nkeycode);
#endif

	kbd->sc_device = self;
	kbd->sc_debounce = (qflags & HIDKBD_SPUR_BUT_UP) != 0;

	/*
	 * Remember if we're the console keyboard.
	 *
	 * XXX This always picks the first (USB) keyboard to attach,
	 * but what else can we really do?
	 */
	if (console) {
		kbd->sc_console_keyboard = hidkbd_is_console;
		/* Don't let any other keyboard have it. */
		hidkbd_is_console = 0;
	}

	timeout_set(&kbd->sc_delay, hidkbd_delayed_decode, kbd);

	return 0;
}

void
hidkbd_attach_wskbd(struct hidkbd *kbd, kbd_t layout,
    const struct wskbd_accessops *accessops)
{
	struct wskbddev_attach_args a;

	ukbd_keymapdata.layout = layout;

	a.console = kbd->sc_console_keyboard;
	a.keymap = &ukbd_keymapdata;
	a.accessops = accessops;
	a.accesscookie = kbd->sc_device;
	kbd->sc_wskbddev = config_found(kbd->sc_device, &a, wskbddevprint);
}

int
hidkbd_detach(struct hidkbd *kbd, int flags)
{
	int rv = 0;

	DPRINTF(("hidkbd_detach: sc=%p flags=%d\n", kbd->sc_device, flags));

	if (kbd->sc_console_keyboard) {
		/*
		 * Disconnect our consops and set hidkbd_is_console
		 * back to 1 so that the next USB keyboard attached
		 * to the system will get it.
		 * XXX Should notify some other keyboard that it can be
		 * XXX console, if there are any other keyboards.
		 */
		printf("%s: was console keyboard\n",
		       kbd->sc_device->dv_xname);
		hidkbd_is_console = 1;
	}
	/* No need to do reference counting of hidkbd, wskbd has all the goo */
	if (kbd->sc_wskbddev != NULL)
		rv = config_detach(kbd->sc_wskbddev, flags);

	if (kbd->sc_var != NULL)
		free(kbd->sc_var, M_DEVBUF, 0);

	return (rv);
}

uint8_t
hidkbd_translate(const struct hidkbd_translation *table, size_t tsize,
    uint8_t keycode)
{
	for (; tsize != 0; table++, tsize--)
		if (table->original == keycode)
			return table->translation;
	return 0;
}

void
hidkbd_apple_translate(void *vsc, uint8_t *ibuf, u_int ilen,
    const struct hidkbd_translation* trans, u_int tlen)
{
	struct hidkbd *kbd = vsc;
	uint8_t *pos, *spos, *epos, xlat;

	spos = ibuf + kbd->sc_keycodeloc.pos / 8;
	epos = spos + kbd->sc_nkeycode;

	for (pos = spos; pos != epos; pos++) {
		xlat = hidkbd_translate(trans, tlen, *pos);
		if (xlat != 0)
			*pos = xlat;
	}
}

void
hidkbd_apple_munge(void *vsc, uint8_t *ibuf, u_int ilen)
{
	struct hidkbd *kbd = vsc;

	if (!hid_get_data(ibuf, ilen, &kbd->sc_fn))
		return;

	hidkbd_apple_translate(vsc, ibuf, ilen, apple_fn_trans,
	    nitems(apple_fn_trans));
}

void
hidkbd_apple_tb_munge(void *vsc, uint8_t *ibuf, u_int ilen)
{
	struct hidkbd *kbd = vsc;

	if (!hid_get_data(ibuf, ilen, &kbd->sc_fn))
		return;

	hidkbd_apple_munge(vsc, ibuf, ilen);

	hidkbd_apple_translate(vsc, ibuf, ilen, apple_tb_trans,
	    nitems(apple_tb_trans));
}

void
hidkbd_apple_iso_munge(void *vsc, uint8_t *ibuf, u_int ilen)
{
	hidkbd_apple_translate(vsc, ibuf, ilen, apple_iso_trans,
	    nitems(apple_iso_trans));
	hidkbd_apple_munge(vsc, ibuf, ilen);
}

void
hidkbd_apple_mba_munge(void *vsc, uint8_t *ibuf, u_int ilen)
{
	struct hidkbd *kbd = vsc;

	if (!hid_get_data(ibuf, ilen, &kbd->sc_fn))
		return;

	hidkbd_apple_translate(vsc, ibuf, ilen, apple_mba_trans,
	    nitems(apple_mba_trans));
}

void
hidkbd_apple_iso_mba_munge(void *vsc, uint8_t *ibuf, u_int ilen)
{
	hidkbd_apple_translate(vsc, ibuf, ilen, apple_iso_trans,
	    nitems(apple_iso_trans));
	hidkbd_apple_mba_munge(vsc, ibuf, ilen);
}

void
hidkbd_input(struct hidkbd *kbd, uint8_t *data, u_int len)
{
	struct hidkbd_data *ud = &kbd->sc_ndata;
	int i;

	if (kbd->sc_munge != NULL)
		(*kbd->sc_munge)(kbd, (uint8_t *)data, len);

#ifdef HIDKBD_DEBUG
	if (hidkbddebug > 5) {
		printf("hidkbd_input: data");
		for (i = 0; i < len; i++)
			printf(" 0x%02x", data[i]);
		printf("\n");
	}
#endif

	/* extract variable keys */
	for (i = 0; i < kbd->sc_nvar; i++)
		ud->var[i] = (u_int8_t)hid_get_data(data, len,
		    &kbd->sc_var[i].loc);

	/* extract keycodes */
	if (kbd->sc_keycodeloc.pos / 8 + kbd->sc_nkeycode <= len)
		memcpy(ud->keycode, data + kbd->sc_keycodeloc.pos / 8,
		    kbd->sc_nkeycode);
	else
		memset(ud->keycode, 0, kbd->sc_nkeycode);

	if (kbd->sc_debounce && !kbd->sc_polling) {
		/*
		 * Some keyboards have a peculiar quirk.  They sometimes
		 * generate a key up followed by a key down for the same
		 * key after about 10 ms.
		 * We avoid this bug by holding off decoding for 20 ms.
		 */
		kbd->sc_data = *ud;
		timeout_add_msec(&kbd->sc_delay, 20);
	} else {
		hidkbd_decode(kbd, ud);
	}
}

void
hidkbd_delayed_decode(void *addr)
{
	struct hidkbd *kbd = addr;

	hidkbd_decode(kbd, &kbd->sc_data);
}

void
hidkbd_decode(struct hidkbd *kbd, struct hidkbd_data *ud)
{
	u_int16_t ibuf[MAXKEYS];	/* chars events */
	int s;
	int nkeys, i, j;
	int key;
#define ADDKEY(c) ibuf[nkeys++] = (c)

#ifdef HIDKBD_DEBUG
	/*
	 * Keep a trace of the last events.  Using printf changes the
	 * timing, so this can be useful sometimes.
	 */
	if (hidkbdtrace) {
		struct hidkbdtraceinfo *p = &hidkbdtracedata[hidkbdtraceindex];
		p->unit = kbd->sc_device->dv_unit;
		microtime(&p->tv);
		p->ud = *ud;
		if (++hidkbdtraceindex >= HIDKBDTRACESIZE)
			hidkbdtraceindex = 0;
	}
	if (hidkbddebug > 5) {
		struct timeval tv;
		microtime(&tv);
		DPRINTF((" at %lld.%06ld key0=0x%02x key1=0x%02x "
			 "key2=0x%02x key3=0x%02x\n",
			 (long long)tv.tv_sec, tv.tv_usec,
			 ud->keycode[0], ud->keycode[1],
			 ud->keycode[2], ud->keycode[3]));
	}
#endif

	if (ud->keycode[0] == KEY_ERROR) {
		DPRINTF(("hidkbd_input: KEY_ERROR\n"));
		return;		/* ignore  */
	}
	nkeys = 0;

	for (i = 0; i < kbd->sc_nvar; i++)
		if ((kbd->sc_odata.var[i] & kbd->sc_var[i].mask) !=
		    (ud->var[i] & kbd->sc_var[i].mask)) {
			ADDKEY(kbd->sc_var[i].key |
			    ((ud->var[i] & kbd->sc_var[i].mask) ?
			    PRESS : RELEASE));
		}

	if (memcmp(ud->keycode, kbd->sc_odata.keycode, kbd->sc_nkeycode) != 0) {
		/* Check for released keys. */
		for (i = 0; i < kbd->sc_nkeycode; i++) {
			key = kbd->sc_odata.keycode[i];
			if (key == 0)
				continue;
			for (j = 0; j < kbd->sc_nkeycode; j++)
				if (key == ud->keycode[j])
					goto rfound;
			DPRINTFN(3,("hidkbd_decode: relse key=0x%02x\n", key));
			ADDKEY(key | RELEASE);
		rfound:
			;
		}

		/* Check for pressed keys. */
		for (i = 0; i < kbd->sc_nkeycode; i++) {
			key = ud->keycode[i];
			if (key == 0)
				continue;
			for (j = 0; j < kbd->sc_nkeycode; j++)
				if (key == kbd->sc_odata.keycode[j])
					goto pfound;
			DPRINTFN(2,("hidkbd_decode: press key=0x%02x\n", key));
			ADDKEY(key | PRESS);
		pfound:
			;
		}
	}
	kbd->sc_odata = *ud;

	if (nkeys == 0)
		return;

	if (kbd->sc_polling) {
		DPRINTFN(1,("hidkbd_decode: pollchar = 0x%03x\n", ibuf[0]));
		memcpy(kbd->sc_pollchars, ibuf, nkeys * sizeof(u_int16_t));
		kbd->sc_npollchar = nkeys;
		return;
	}

	if (kbd->sc_wskbddev == NULL)
		return;

#ifdef WSDISPLAY_COMPAT_RAWKBD
	if (kbd->sc_rawkbd) {
		u_char cbuf[MAXKEYS * 2];
		int c;

		for (i = j = 0; i < nkeys; i++) {
			key = ibuf[i];
			c = hidkbd_trtab[key & CODEMASK];
			if (c == NN)
				continue;
			if (c & 0x80)
				cbuf[j++] = 0xe0;
			cbuf[j] = c & 0x7f;
			if (key & RELEASE)
				cbuf[j] |= 0x80;
			DPRINTFN(1,("hidkbd_decode: raw = %s0x%02x\n",
				    c & 0x80 ? "0xe0 " : "",
				    cbuf[j]));
			j++;
		}
		s = spltty();
		wskbd_rawinput(kbd->sc_wskbddev, cbuf, j);

		/*
		 * Pass audio, brightness and sleep keys to wskbd_input anyway.
		 */
		for (i = 0; i < nkeys; i++) {
			key = ibuf[i];
			switch (key & CODEMASK) {
			case 102:
			case 127:
			case 128:
			case 129:
			case 232:
			case 233:
			case 234:
			case 235:
			case 236:
				wskbd_input(kbd->sc_wskbddev,
				    key & RELEASE ?  WSCONS_EVENT_KEY_UP :
				      WSCONS_EVENT_KEY_DOWN, key & CODEMASK);
				break;
			}
		}
		splx(s);

		return;
	}
#endif

	s = spltty();
	for (i = 0; i < nkeys; i++) {
		key = ibuf[i];
		wskbd_input(kbd->sc_wskbddev,
		    key&RELEASE ? WSCONS_EVENT_KEY_UP : WSCONS_EVENT_KEY_DOWN,
		    key&CODEMASK);
	}
	splx(s);
#undef	ADDKEY
}

int
hidkbd_enable(struct hidkbd *kbd, int on)
{
	if (kbd->sc_enabled == on)
		return EBUSY;

	kbd->sc_enabled = on;
	return 0;
}

int
hidkbd_set_leds(struct hidkbd *kbd, int leds, uint8_t *report)
{
	if (kbd->sc_leds == leds)
		return 0;

	kbd->sc_leds = leds;

	/*
	 * This is not totally correct, since we did not check the
	 * report size from the descriptor but for keyboards it should
	 * just be a single byte with the relevant bits set.
	 */
	*report = 0;
	if ((leds & WSKBD_LED_SCROLL) && kbd->sc_scroloc.size == 1)
		*report |= 1 << kbd->sc_scroloc.pos;
	if ((leds & WSKBD_LED_NUM) && kbd->sc_numloc.size == 1)
		*report |= 1 << kbd->sc_numloc.pos;
	if ((leds & WSKBD_LED_CAPS) && kbd->sc_capsloc.size == 1)
		*report |= 1 << kbd->sc_capsloc.pos;
	if ((leds & WSKBD_LED_COMPOSE) && kbd->sc_compose.size == 1)
		*report |= 1 << kbd->sc_compose.pos;

	return 1;
}

int
hidkbd_ioctl(struct hidkbd *kbd, u_long cmd, caddr_t data, int flag,
   struct proc *p)
{
	switch (cmd) {
	case WSKBDIO_GETLEDS:
		*(int *)data = kbd->sc_leds;
		return (0);
	case WSKBDIO_COMPLEXBELL:
#define d ((struct wskbd_bell_data *)data)
		hidkbd_bell(d->pitch, d->period, d->volume, 0);
#undef d
		return (0);
#ifdef WSDISPLAY_COMPAT_RAWKBD
	case WSKBDIO_SETMODE:
		DPRINTF(("hidkbd_ioctl: set raw = %d\n", *(int *)data));
		kbd->sc_rawkbd = *(int *)data == WSKBD_RAW;
		return (0);
#endif
	}
	return (-1);
}

void
hidkbd_cngetc(struct hidkbd *kbd, u_int *type, int *data)
{
	int c;

	c = kbd->sc_pollchars[0];
	kbd->sc_npollchar--;
	memmove(kbd->sc_pollchars, kbd->sc_pollchars+1,
	       kbd->sc_npollchar * sizeof(u_int16_t));
	*type = c & RELEASE ? WSCONS_EVENT_KEY_UP : WSCONS_EVENT_KEY_DOWN;
	*data = c & CODEMASK;
}

void
hidkbd_bell(u_int pitch, u_int period, u_int volume, int poll)
{
	if (hidkbd_bell_fn != NULL)
		(*hidkbd_bell_fn)(hidkbd_bell_fn_arg, pitch, period,
		    volume, poll);
}

void
hidkbd_hookup_bell(void (*fn)(void *, u_int, u_int, u_int, int), void *arg)
{
	if (hidkbd_bell_fn == NULL) {
		hidkbd_bell_fn = fn;
		hidkbd_bell_fn_arg = arg;
	}
}

const char *
hidkbd_parse_desc(struct hidkbd *kbd, int id, void *desc, int dlen)
{
	struct hid_data *d;
	struct hid_item h;
	unsigned int i, ivar = 0;

	kbd->sc_nkeycode = 0;

	d = hid_start_parse(desc, dlen, hid_input);
	while (hid_get_item(d, &h)) {
		if (h.kind != hid_input || (h.flags & HIO_CONST) ||
		    HID_GET_USAGE_PAGE(h.usage) != HUP_KEYBOARD ||
		    h.report_ID != id)
			continue;
		if (h.flags & HIO_VARIABLE)
			ivar++;
	}
	hid_end_parse(d);

	if (ivar > MAXVARS) {
		DPRINTF((": too many variable keys\n"));
		ivar = MAXVARS;
	}

	kbd->sc_nvar = ivar;
	kbd->sc_var = (struct hidkbd_variable *)mallocarray(ivar,
			sizeof(struct hidkbd_variable), M_DEVBUF, M_NOWAIT);

	if (!kbd->sc_var)
		return NULL;

	i = 0;

	d = hid_start_parse(desc, dlen, hid_input);
	while (hid_get_item(d, &h)) {
		if (h.kind != hid_input || (h.flags & HIO_CONST) ||
		    HID_GET_USAGE_PAGE(h.usage) != HUP_KEYBOARD ||
		    h.report_ID != id)
			continue;

		DPRINTF(("hidkbd: usage=0x%x flags=0x%x pos=%d size=%d cnt=%d",
			 h.usage, h.flags, h.loc.pos, h.loc.size, h.loc.count));
		if (h.flags & HIO_VARIABLE) {
			/* variable reports should be one bit each */
			if (h.loc.size != 1) {
				DPRINTF((": bad variable size\n"));
				continue;
			}

			/* variable report */
			if (i < MAXVARS) {
				kbd->sc_var[i].loc = h.loc;
				kbd->sc_var[i].mask = 1 << (i % 8);
				kbd->sc_var[i].key = HID_GET_USAGE(h.usage);
				i++;
			}
		} else {
			/* keys array should be in bytes, on a byte boundary */
			if (h.loc.size != 8) {
				DPRINTF((": key code size != 8\n"));
				continue;
			}
			if (h.loc.pos % 8 != 0) {
				DPRINTF((": array not on byte boundary"));
				continue;
			}
			if (kbd->sc_nkeycode != 0) {
				DPRINTF((": ignoring multiple arrays\n"));
				continue;
			}
			kbd->sc_keycodeloc = h.loc;
			if (h.loc.count > MAXKEYCODE) {
				DPRINTF((": ignoring extra key codes"));
				kbd->sc_nkeycode = MAXKEYCODE;
			} else
				kbd->sc_nkeycode = h.loc.count;
		}
		DPRINTF(("\n"));
	}
	hid_end_parse(d);

	/* don't attach if no keys... */
	if (kbd->sc_nkeycode == 0 && ivar == 0)
		return "no usable key codes array";

	hid_locate(desc, dlen, HID_USAGE2(HUP_LED, HUL_NUM_LOCK),
	    id, hid_output, &kbd->sc_numloc, NULL);
	hid_locate(desc, dlen, HID_USAGE2(HUP_LED, HUL_CAPS_LOCK),
	    id, hid_output, &kbd->sc_capsloc, NULL);
	hid_locate(desc, dlen, HID_USAGE2(HUP_LED, HUL_SCROLL_LOCK),
	    id, hid_output, &kbd->sc_scroloc, NULL);
	hid_locate(desc, dlen, HID_USAGE2(HUP_LED, HUL_COMPOSE),
	    id, hid_output, &kbd->sc_compose, NULL);

	return (NULL);
}

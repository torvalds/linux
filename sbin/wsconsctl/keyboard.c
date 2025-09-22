/*	$OpenBSD: keyboard.c,v 1.14 2019/06/28 13:32:46 deraadt Exp $	*/
/*	$NetBSD: keyboard.c 1.1 1998/12/28 14:01:17 hannken Exp $ */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Juergen Hannken-Illjes.
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

#include <sys/ioctl.h>
#include <sys/time.h>
#include <dev/wscons/wsksymdef.h>
#include <dev/wscons/wsconsio.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include "wsconsctl.h"

static u_int kbtype;
static struct wskbd_bell_data bell;
static struct wskbd_bell_data dfbell;
static struct wscons_keymap mapdata[KS_NUMKEYCODES];
struct wskbd_map_data kbmap = { KS_NUMKEYCODES, mapdata };	/* used in map_parse.y
							   and in util.c */
static struct wskbd_keyrepeat_data repeat;
static struct wskbd_keyrepeat_data dfrepeat;
static int ledstate;
static kbd_t kbdencoding;
static struct field_pc backlight;

struct field keyboard_field_tab[] = {
    { "type",			&kbtype,	FMT_KBDTYPE,	FLG_RDONLY },
    { "bell.pitch",		&bell.pitch,	FMT_UINT,	FLG_MODIFY },
    { "bell.period",		&bell.period,	FMT_UINT,	FLG_MODIFY },
    { "bell.volume",		&bell.volume,	FMT_UINT,	FLG_MODIFY },
    { "bell.pitch.default",	&dfbell.pitch,	FMT_UINT,	FLG_MODIFY },
    { "bell.period.default",	&dfbell.period,	FMT_UINT,	FLG_MODIFY },
    { "bell.volume.default",	&dfbell.volume,	FMT_UINT,	FLG_MODIFY },
    { "map",			&kbmap,		FMT_KBMAP,	FLG_MODIFY|FLG_NOAUTO },
    { "repeat.del1",		&repeat.del1,	FMT_UINT,	FLG_MODIFY },
    { "repeat.deln",		&repeat.delN,	FMT_UINT,	FLG_MODIFY },
    { "repeat.del1.default",	&dfrepeat.del1,	FMT_UINT,	FLG_MODIFY },
    { "repeat.deln.default",	&dfrepeat.delN,	FMT_UINT,	FLG_MODIFY },
    { "ledstate",		&ledstate,	FMT_UINT,	0 },
    { "encoding",		&kbdencoding,	FMT_KBDENC,	FLG_MODIFY },
    { "backlight",		&backlight,	FMT_PC,		FLG_MODIFY|FLG_INIT },
    { NULL }
};

void
keyboard_get_values(int fd)
{
	struct wskbd_backlight kbl;
	struct field *pf;

	if (field_by_value(keyboard_field_tab, &kbtype)->flags & FLG_GET)
		if (ioctl(fd, WSKBDIO_GTYPE, &kbtype) == -1)
			warn("WSKBDIO_GTYPE");

	bell.which = 0;
	if (field_by_value(keyboard_field_tab, &bell.pitch)->flags & FLG_GET)
		bell.which |= WSKBD_BELL_DOPITCH;
	if (field_by_value(keyboard_field_tab, &bell.period)->flags & FLG_GET)
		bell.which |= WSKBD_BELL_DOPERIOD;
	if (field_by_value(keyboard_field_tab, &bell.volume)->flags & FLG_GET)
		bell.which |= WSKBD_BELL_DOVOLUME;
	if (bell.which != 0 && ioctl(fd, WSKBDIO_GETBELL, &bell) == -1)
		warn("WSKBDIO_GETBELL");

	dfbell.which = 0;
	if (field_by_value(keyboard_field_tab, &dfbell.pitch)->flags & FLG_GET)
		dfbell.which |= WSKBD_BELL_DOPITCH;
	if (field_by_value(keyboard_field_tab, &dfbell.period)->flags & FLG_GET)
		dfbell.which |= WSKBD_BELL_DOPERIOD;
	if (field_by_value(keyboard_field_tab, &dfbell.volume)->flags & FLG_GET)
		dfbell.which |= WSKBD_BELL_DOVOLUME;
	if (dfbell.which != 0 &&
	    ioctl(fd, WSKBDIO_GETDEFAULTBELL, &dfbell) == -1)
		warn("WSKBDIO_GETDEFAULTBELL");

	if (field_by_value(keyboard_field_tab, &kbmap)->flags & FLG_GET) {
		kbmap.maplen = KS_NUMKEYCODES;
		if (ioctl(fd, WSKBDIO_GETMAP, &kbmap) == -1)
			warn("WSKBDIO_GETMAP");
		if (ioctl(fd, WSKBDIO_GETENCODING, &kbdencoding) == -1)
			warn("WSKBDIO_GETENCODING");
		ksymenc(kbdencoding);
	}

	repeat.which = 0;
	if (field_by_value(keyboard_field_tab, &repeat.del1)->flags & FLG_GET)
		repeat.which |= WSKBD_KEYREPEAT_DODEL1;
	if (field_by_value(keyboard_field_tab, &repeat.delN)->flags & FLG_GET)
		repeat.which |= WSKBD_KEYREPEAT_DODELN;
	if (repeat.which != 0 &&
	    ioctl(fd, WSKBDIO_GETKEYREPEAT, &repeat) == -1)
		warn("WSKBDIO_GETKEYREPEAT");

	dfrepeat.which = 0;
	if (field_by_value(keyboard_field_tab, &dfrepeat.del1)->flags & FLG_GET)
		dfrepeat.which |= WSKBD_KEYREPEAT_DODEL1;
	if (field_by_value(keyboard_field_tab, &dfrepeat.delN)->flags & FLG_GET)
		dfrepeat.which |= WSKBD_KEYREPEAT_DODELN;
	if (dfrepeat.which != 0 &&
	    ioctl(fd, WSKBDIO_GETDEFAULTKEYREPEAT, &dfrepeat) == -1)
		warn("WSKBDIO_GETDEFAULTKEYREPEAT");

	if (field_by_value(keyboard_field_tab, &ledstate)->flags & FLG_GET)
		if (ioctl(fd, WSKBDIO_GETLEDS, &ledstate) == -1)
			warn("WSKBDIO_GETLEDS");

	if (field_by_value(keyboard_field_tab, &kbdencoding)->flags & FLG_GET)
		if (ioctl(fd, WSKBDIO_GETENCODING, &kbdencoding) == -1)
			warn("WSKBDIO_GETENCODING");

	pf = field_by_value(keyboard_field_tab, &backlight);
	if (pf->flags & FLG_GET && !(pf->flags & FLG_DEAD)) {
		errno = ENOTTY;
		if (ioctl(fd, WSKBDIO_GETBACKLIGHT, &kbl) == -1) {
			if (errno == ENOTTY)
				pf->flags |= FLG_DEAD;
			else
				warn("WSKBDIO_GETBACKLIGHT");
		} else {
			backlight.min = kbl.min;
			backlight.cur = kbl.curval;
			backlight.max = kbl.max;
		}
	}
}

int
keyboard_put_values(int fd)
{
	struct wskbd_backlight kbl;
	struct field *pf;

	bell.which = 0;
	if (field_by_value(keyboard_field_tab, &bell.pitch)->flags & FLG_SET)
		bell.which |= WSKBD_BELL_DOPITCH;
	if (field_by_value(keyboard_field_tab, &bell.period)->flags & FLG_SET)
		bell.which |= WSKBD_BELL_DOPERIOD;
	if (field_by_value(keyboard_field_tab, &bell.volume)->flags & FLG_SET)
		bell.which |= WSKBD_BELL_DOVOLUME;
	if (bell.which != 0 && ioctl(fd, WSKBDIO_SETBELL, &bell) == -1) {
		warn("WSKBDIO_SETBELL");
		return 1;
	}

	dfbell.which = 0;
	if (field_by_value(keyboard_field_tab, &dfbell.pitch)->flags & FLG_SET)
		dfbell.which |= WSKBD_BELL_DOPITCH;
	if (field_by_value(keyboard_field_tab, &dfbell.period)->flags & FLG_SET)
		dfbell.which |= WSKBD_BELL_DOPERIOD;
	if (field_by_value(keyboard_field_tab, &dfbell.volume)->flags & FLG_SET)
		dfbell.which |= WSKBD_BELL_DOVOLUME;
	if (dfbell.which != 0 &&
	    ioctl(fd, WSKBDIO_SETDEFAULTBELL, &dfbell) == -1) {
		warn("WSKBDIO_SETDEFAULTBELL");
		return 1;
	}

	if (field_by_value(keyboard_field_tab, &kbmap)->flags & FLG_SET) {
		if (ioctl(fd, WSKBDIO_SETMAP, &kbmap) == -1) {
			warn("WSKBDIO_SETMAP");
			return 1;
		}
	}

	repeat.which = 0;
	if (field_by_value(keyboard_field_tab, &repeat.del1)->flags & FLG_SET)
		repeat.which |= WSKBD_KEYREPEAT_DODEL1;
	if (field_by_value(keyboard_field_tab, &repeat.delN)->flags & FLG_SET)
		repeat.which |= WSKBD_KEYREPEAT_DODELN;
	if (repeat.which != 0 &&
	    ioctl(fd, WSKBDIO_SETKEYREPEAT, &repeat) == -1) {
		warn("WSKBDIO_SETKEYREPEAT");
		return 1;
	}

	dfrepeat.which = 0;
	if (field_by_value(keyboard_field_tab, &dfrepeat.del1)->flags & FLG_SET)
		dfrepeat.which |= WSKBD_KEYREPEAT_DODEL1;
	if (field_by_value(keyboard_field_tab, &dfrepeat.delN)->flags & FLG_SET)
		dfrepeat.which |= WSKBD_KEYREPEAT_DODELN;
	if (dfrepeat.which != 0 &&
	    ioctl(fd, WSKBDIO_SETDEFAULTKEYREPEAT, &dfrepeat) == -1) {
		warn("WSKBDIO_SETDEFAULTKEYREPEAT");
		return 1;
	}

	if (field_by_value(keyboard_field_tab, &ledstate)->flags & FLG_SET) {
		if (ioctl(fd, WSKBDIO_SETLEDS, &ledstate) == -1) {
			warn("WSKBDIO_SETLEDS");
			return 1;
		}
	}

	if (field_by_value(keyboard_field_tab, &kbdencoding)->flags & FLG_SET) {
		if (ioctl(fd, WSKBDIO_SETENCODING, &kbdencoding) == -1) {
			warn("WSKBDIO_SETENCODING");
			return 1;
		}
	}

	pf = field_by_value(keyboard_field_tab, &backlight);
	if (pf->flags & FLG_SET && !(pf->flags & FLG_DEAD)) {
		kbl.min = backlight.min;
		kbl.curval = backlight.cur;
		kbl.max = backlight.max;
		errno = ENOTTY;
		if (ioctl(fd, WSKBDIO_SETBACKLIGHT, &kbl) == -1) {
			if (errno == ENOTTY)
				pf->flags |= FLG_DEAD;
			else {
				warn("WSKBDIO_SETBACKLIGHT");
				return 1;
			}
		}
	}

	return 0;
}

char *
keyboard_next_device(int index)
{
	static char devname[20];

	snprintf(devname, sizeof(devname), "/dev/wskbd%d", index);
	return (devname);
}

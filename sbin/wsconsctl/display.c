/*	$OpenBSD: display.c,v 1.22 2019/06/28 13:32:46 deraadt Exp $	*/
/*	$NetBSD: display.c,v 1.1 1998/12/28 14:01:16 hannken Exp $ */

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
#include <dev/wscons/wsconsio.h>
#include <errno.h>
#include <fcntl.h>
#include <err.h>
#include <stdio.h>
#include <string.h>
#include "wsconsctl.h"

u_int dpytype;
u_int width, height, depth, fontwidth, fontheight;
int focus;
struct field_pc brightness, contrast, backlight;
int burnon, burnoff, vblank, kbdact, msact, outact;
char fontname[WSFONT_NAME_SIZE];
struct wsdisplay_emultype emuls;
struct wsdisplay_screentype screens;

struct field display_field_tab[] = {
    { "type",		&dpytype,	FMT_DPYTYPE,	FLG_RDONLY },
    { "width",		&width,		FMT_UINT,	FLG_RDONLY },
    { "height",		&height,	FMT_UINT,	FLG_RDONLY },
    { "depth",		&depth,		FMT_UINT,	FLG_RDONLY },
    { "fontwidth",	&fontwidth,	FMT_UINT,	FLG_RDONLY },
    { "fontheight",	&fontheight,	FMT_UINT,	FLG_RDONLY },
    { "emulations",	&emuls,		FMT_EMUL,	FLG_RDONLY },
    { "screentypes",	&screens,	FMT_SCREEN,	FLG_RDONLY },
    { "focus",		&focus,		FMT_INT,	FLG_NORDBACK },
    { "brightness",	&brightness,	FMT_PC,		FLG_MODIFY|FLG_INIT },
    { "contrast",	&contrast,	FMT_PC,		FLG_MODIFY|FLG_INIT },
    { "backlight",	&backlight,	FMT_PC,		FLG_MODIFY|FLG_INIT },
    /* screen burner section, outact MUST BE THE LAST, see the set_values */
    { "screen_on",	&burnon,	FMT_UINT,	FLG_MODIFY|FLG_INIT },
    { "screen_off",	&burnoff,	FMT_UINT,	FLG_MODIFY|FLG_INIT },
    { "vblank",		&vblank,	FMT_BOOL,	FLG_MODIFY|FLG_INIT },
    { "kbdact",		&kbdact,	FMT_BOOL,	FLG_MODIFY|FLG_INIT },
    { "msact",		&msact,		FMT_BOOL,	FLG_MODIFY|FLG_INIT },
    { "outact",		&outact,	FMT_BOOL,	FLG_MODIFY|FLG_INIT },
    { "font",		fontname,	FMT_STRING,	FLG_WRONLY },
    { NULL }
};

#define	fillioctl(n)	{ cmd = n; cmd_str = #n; }

void
display_get_values(int fd)
{
	struct wsdisplay_addscreendata gscr;
	struct wsdisplay_param param;
	struct wsdisplay_burner burners;
	struct wsdisplay_fbinfo fbinfo;
	struct field *pf;
	const char *cmd_str;
	void *ptr;
	unsigned long cmd;
	int bon = 0, fbon = 0, son = 0;

	focus = gscr.idx = -1;
	for (pf = display_field_tab; pf->name; pf++) {

		if (!(pf->flags & FLG_GET) || pf->flags & FLG_DEAD)
			continue;

		ptr = pf->valp;

		if (ptr == &dpytype) {
			fillioctl(WSDISPLAYIO_GTYPE);
		} else if (ptr == &focus) {
			fillioctl(WSDISPLAYIO_GETSCREEN);
			ptr = &gscr;
		} else if (ptr == &emuls) {
			fillioctl(WSDISPLAYIO_GETEMULTYPE);
			emuls.idx=0;
		} else if (ptr == &fontwidth || ptr == &fontheight) {
			fillioctl(WSDISPLAYIO_GETSCREENTYPE);
			ptr = &screens;
			screens.idx = 0;
		} else if (ptr == &screens) {
			fillioctl(WSDISPLAYIO_GETSCREENTYPE);
			screens.idx=0;
		} else if (ptr == &brightness) {
			ptr = &param;
			param.param = WSDISPLAYIO_PARAM_BRIGHTNESS;
		} else if (ptr == &contrast) {
			ptr = &param;
			param.param = WSDISPLAYIO_PARAM_CONTRAST;
		} else if (ptr == &backlight) {
			ptr = &param;
			param.param = WSDISPLAYIO_PARAM_BACKLIGHT;
		} else if (ptr == &burnon || ptr == &burnoff ||
			   ptr == &vblank || ptr == &kbdact ||
			   ptr == &outact || ptr == &msact) {
			fillioctl(WSDISPLAYIO_GBURNER);
			ptr = &burners;
			if (!bon)
				bzero(&burners, sizeof(burners));
		} else if (ptr == &height || ptr == &width ||
			   ptr == &depth) {
			fillioctl(WSDISPLAYIO_GINFO);
			ptr = &fbinfo;
			if (!fbon)
				bzero(&fbinfo, sizeof(fbinfo));
		} else
			cmd = 0;

		if (ptr == &param) {
			fillioctl(WSDISPLAYIO_GETPARAM);
		}

		if ((cmd != WSDISPLAYIO_GBURNER && cmd != WSDISPLAYIO_GINFO) ||
		    (cmd == WSDISPLAYIO_GBURNER && !bon) ||
		    (cmd == WSDISPLAYIO_GINFO && !fbon)) {
			errno = ENOTTY;
			if (!cmd || ioctl(fd, cmd, ptr) == -1) {
				if (errno == ENOTTY) {
					pf->flags |= FLG_DEAD;
					continue;
				} else
					warn("%s", cmd_str);
			}
		}

		if (ptr == &burners) {
			if (!bon) {
				burnon = burners.on;
				burnoff = burners.off;
				vblank = burners.flags & WSDISPLAY_BURN_VBLANK;
				kbdact = burners.flags & WSDISPLAY_BURN_KBD;
				msact = burners.flags & WSDISPLAY_BURN_MOUSE;
				outact = burners.flags & WSDISPLAY_BURN_OUTPUT;
			}
			bon++;
		} else if (ptr == &fbinfo) {
			if (!fbon) {
				width = fbinfo.width;
				height = fbinfo.height;
				depth = fbinfo.depth;
			}
			fbon++;
		} else if (ptr == &emuls) {
			emuls.idx=fd;
		} else if (ptr == &screens) {
			screens.idx=fd;
			if (!son) {
				fontwidth = screens.fontwidth;
				fontheight = screens.fontheight;
			}
			son++;
		} else if (ptr == &param) {
			struct field_pc *pc = pf->valp;

			pc->min = param.min;
			pc->cur = param.curval;
			pc->max = param.max;
		} else if (ptr == &gscr)
			focus = gscr.idx;
	}
}

int
display_put_values(int fd)
{
	struct wsdisplay_param param;
	struct wsdisplay_burner burners;
	struct wsdisplay_font font;
	struct field *pf;
	const char *cmd_str;
	void *ptr;
	unsigned long cmd;
	int id;

	for (pf = display_field_tab; pf->name; pf++) {

		if (!(pf->flags & FLG_SET) || pf->flags & FLG_DEAD)
			continue;

		ptr = pf->valp;

		if (ptr == &focus) {
			fillioctl(WSDISPLAYIO_SETSCREEN);
		} else if (ptr == &brightness) {
			ptr = &param;
			id = WSDISPLAYIO_PARAM_BRIGHTNESS;
		} else if (ptr == &contrast) {
			ptr = &param;
			id = WSDISPLAYIO_PARAM_CONTRAST;
		} else if (ptr == &backlight) {
			ptr = &param;
			id = WSDISPLAYIO_PARAM_BACKLIGHT;
		} else if (ptr == &burnon || ptr == &burnoff ||
			   ptr == &vblank || ptr == &kbdact ||
			   ptr == &outact || ptr == &msact) {

			bzero(&burners, sizeof(burners));
			burners.on = burnon;
			burners.off = burnoff;
			if (vblank)
				burners.flags |= WSDISPLAY_BURN_VBLANK;
			else
				burners.flags &= ~WSDISPLAY_BURN_VBLANK;
			if (kbdact)
				burners.flags |= WSDISPLAY_BURN_KBD;
			else
				burners.flags &= ~WSDISPLAY_BURN_KBD;
			if (msact)
				burners.flags |= WSDISPLAY_BURN_MOUSE;
			else
				burners.flags &= ~WSDISPLAY_BURN_MOUSE;
			if (outact)
				burners.flags |= WSDISPLAY_BURN_OUTPUT;
			else
				burners.flags &= ~WSDISPLAY_BURN_OUTPUT;

			fillioctl(WSDISPLAYIO_SBURNER);
			ptr = &burners;
		} else if (ptr == fontname) {
			bzero(&font, sizeof(font));
			strlcpy(font.name, ptr, sizeof font.name);
			fillioctl(WSDISPLAYIO_USEFONT);
		} else
			cmd = 0;

		if (ptr == &param) {
			struct field_pc *pc = pf->valp;

			bzero(&param, sizeof(param));
			param.param = id;
			param.min = pc->min;
			param.curval = pc->cur;
			param.max = pc->max;
			fillioctl(WSDISPLAYIO_SETPARAM);
		}

		errno = ENOTTY;
		if (!cmd || ioctl(fd, cmd, ptr) == -1) {
			if (errno == ENOTTY) {
				pf->flags |= FLG_DEAD;
				continue;
			} else {
				warn("%s", cmd_str);
				return 1;
			}
		}
	}

	return 0;
}

char *
display_next_device(int index)
{
	static char devname[20];

	if (index > 7)
		return (NULL);

	snprintf(devname, sizeof(devname), "/dev/tty%c0", index + 'C');
	return (devname);
}

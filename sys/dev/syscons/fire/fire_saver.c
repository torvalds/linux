/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Brad Forschinger
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * brad forschinger, 19990504 <retch@flag.blackened.net>
 * 
 * written with much help from warp_saver.c
 * 
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/syslog.h>
#include <sys/consio.h>
#include <sys/malloc.h>
#include <sys/fbio.h>

#include <dev/fb/fbreg.h>
#include <dev/fb/splashreg.h>
#include <dev/syscons/syscons.h>

#define SAVER_NAME	 "fire_saver"

#define RED(n)		 ((n) * 3 + 0)
#define GREEN(n)	 ((n) * 3 + 1)
#define BLUE(n)		 ((n) * 3 + 2)

#define SET_ORIGIN(adp, o) do {				\
	int oo = o;					\
	if (oo != last_origin)				\
	    vidd_set_win_org(adp, last_origin = oo);	\
	} while (0)

static u_char		*buf;
static u_char		*vid;
static int		 banksize, scrmode, bpsl, scrw, scrh;
static u_char		 fire_pal[768];
static int		 blanked;

static void
fire_update(video_adapter_t *adp)
{
	int x, y;
	int o, p;
	int last_origin = -1;

	/* make a new bottom line */
	for (x = 0, y = scrh; x < scrw; x++)
		buf[x + (y * bpsl)] = random() % 160 + 96;

	/* fade the flames out */
	for (y = 0; y < scrh; y++) {
		for (x = 0; x < scrw; x++) {
			buf[x + (y * scrw)] =
			    (buf[(x + 0) + ((y + 0) * scrw)] +
			     buf[(x - 1) + ((y + 1) * scrw)] +
			     buf[(x + 0) + ((y + 1) * scrw)] +
			     buf[(x + 1) + ((y + 1) * scrw)]) / 4;
			if (buf[x + (y * scrw)] > 0)
				buf[x + (y * scrw)]--;
		}
	}

	/* blit our buffer into video ram */
	for (y = 0, p = 0, o = 0; y < scrh; y++, p += bpsl) {
		while (p > banksize) {
			p -= banksize;
			o += banksize;
		}
		SET_ORIGIN(adp, o);
		if (p + scrw < banksize) {
			bcopy(buf + y * scrw, vid + p, scrw);
		} else {
			bcopy(buf + y * scrw, vid + p, banksize - p);
			SET_ORIGIN(adp, o + banksize);
			bcopy(buf + y * scrw + (banksize - p), vid,
			      scrw - (banksize - p));
			p -= banksize;
			o += banksize;
		}
	}

}

static int
fire_saver(video_adapter_t *adp, int blank)
{
	int pl;

	if (blank) {
		/* switch to graphics mode */
      		if (blanked <= 0) {
			pl = splhigh();
			vidd_set_mode(adp, scrmode);
			vidd_load_palette(adp, fire_pal);
			blanked++;
			vid = (u_char *)adp->va_window;
			banksize = adp->va_window_size;
			bpsl = adp->va_line_width;
			splx(pl);
			vidd_clear(adp);
		}
		fire_update(adp);
	} else {
		blanked = 0;
	}

    return 0;
}

static int
fire_init(video_adapter_t *adp)
{
	video_info_t info;
	int i, red, green, blue;

	if (!vidd_get_info(adp, M_VGA_CG320, &info)) {
		scrmode = M_VGA_CG320;
	} else {
		log(LOG_NOTICE,
		    "%s: the console does not support M_VGA_CG320\n",
		    SAVER_NAME);
		return (ENODEV);
	}
    
	scrw = info.vi_width;
	scrh = info.vi_height;

	buf = (u_char *)malloc(scrw * (scrh + 1), M_DEVBUF, M_NOWAIT);
	if (buf) {
		bzero(buf, scrw * (scrh + 1));
	} else {
		log(LOG_NOTICE,
		    "%s: buffer allocation is failed\n",
		    SAVER_NAME);
		return (ENODEV);
	}

	/* intialize the palette */
	red = green = blue = 0;
	for (i = 0; i < 256; i++) {
		red++;
		if (red > 128)
			green += 2;
		fire_pal[RED(i)] = red;
		fire_pal[GREEN(i)] = green;
		fire_pal[BLUE(i)] = blue;
	}

	return (0);
}

static int
fire_term(video_adapter_t *adp)
{
	free(buf, M_DEVBUF);
	return (0);
}

static scrn_saver_t fire_module = {
	SAVER_NAME,
	fire_init,
	fire_term,
	fire_saver,
	NULL
};

SAVER_MODULE(fire_saver, fire_module);

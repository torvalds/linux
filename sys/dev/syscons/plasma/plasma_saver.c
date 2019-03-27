/*-
 * Copyright (c) 2015 Dag-Erling Smørgrav
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
 *
 * To CJA, in appreciation of Nighthawk brunches past and future.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/syslog.h>
#include <sys/consio.h>
#include <sys/fbio.h>

#include <dev/fb/fbreg.h>
#include <dev/fb/splashreg.h>
#include <dev/syscons/syscons.h>

#define SAVER_NAME	 "plasma_saver"

#include "fp16.h"

/*
 * Preferred video modes
 */
static int modes[] = {
	M_VGA_CG640,
	M_VGA_CG320,
	-1
};

/*
 * Display parameters
 */
static unsigned char *vid;
static unsigned int banksize, scrmode, scrw, scrh;
static unsigned int blanked;

/*
 * List of foci
 */
#define FOCI 3
static struct {
	int x, y;		/* coordinates */
	int vx, vy;		/* velocity */
} plasma_foci[FOCI];

/*
 * Palette
 */
static struct {
	unsigned char r, g, b;
} plasma_pal[256];

/*
 * Draw a new frame
 */
static void
plasma_update(video_adapter_t *adp)
{
	unsigned int x, y;	/* coordinates */
	signed int dx, dy;	/* horizontal / vertical distance */
	fp16_t sqd, d;		/* square of distance and distance */
	fp16_t m;		/* magnitude */
	unsigned int org, off;	/* origin and offset */
	unsigned int i;		/* loop index */

	/* switch to bank 0 */
	vidd_set_win_org(adp, 0);
	/* for each scan line */
	for (y = org = off = 0; y < scrh; ++y) {
		/* for each pixel on scan line */
		for (x = 0; x < scrw; ++x, ++off) {
			/* for each focus */
			for (i = m = 0; i < FOCI; ++i) {
				dx = x - plasma_foci[i].x;
				dy = y - plasma_foci[i].y;
				sqd = ItoFP16(dx * dx + dy * dy);
				d = fp16_sqrt(sqd);
				/* divide by 4 to stretch out the pattern */
				m = fp16_sub(m, fp16_cos(d / 4));
			}
			/*
			 * m is now in the range +/- FOCI, but we need a
			 * value between 0 and 255.  We scale to +/- 127
			 * and add 127, which moves it into the range [0,
			 * 254].
			 */
			m = fp16_mul(m, ItoFP16(127));
			m = fp16_div(m, ItoFP16(FOCI));
			m = fp16_add(m, ItoFP16(127));
			/* switch banks if necessary */
			if (off > banksize) {
				off -= banksize;
				org += banksize;
				vidd_set_win_org(adp, org);
			}
			/* plot */
			vid[off] = FP16toI(m);
		}
	}
	/* now move the foci */
	for (i = 0; i < FOCI; ++i) {
		plasma_foci[i].x += plasma_foci[i].vx;
		if (plasma_foci[i].x < 0) {
			/* bounce against left wall */
			plasma_foci[i].vx = -plasma_foci[i].vx;
			plasma_foci[i].x = -plasma_foci[i].x;
		} else if (plasma_foci[i].x >= scrw) {
			/* bounce against right wall */
			plasma_foci[i].vx = -plasma_foci[i].vx;
			plasma_foci[i].x = scrw - (plasma_foci[i].x - scrw);
		}
		plasma_foci[i].y += plasma_foci[i].vy;
		if (plasma_foci[i].y < 0) {
			/* bounce against ceiling */
			plasma_foci[i].vy = -plasma_foci[i].vy;
			plasma_foci[i].y = -plasma_foci[i].y;
		} else if (plasma_foci[i].y >= scrh) {
			/* bounce against floor */
			plasma_foci[i].vy = -plasma_foci[i].vy;
			plasma_foci[i].y = scrh - (plasma_foci[i].y - scrh);
		}
	}
}

/*
 * Start or stop the screensaver
 */
static int
plasma_saver(video_adapter_t *adp, int blank)
{
	int pl;

	if (blank) {
		/* switch to graphics mode */
		if (blanked <= 0) {
			pl = splhigh();
			vidd_set_mode(adp, scrmode);
			vidd_load_palette(adp, (unsigned char *)plasma_pal);
			vidd_set_border(adp, 0);
			blanked++;
			vid = (unsigned char *)adp->va_window;
			banksize = adp->va_window_size;
			splx(pl);
			vidd_clear(adp);
		}
		/* update display */
		plasma_update(adp);
	} else {
		blanked = 0;
	}
	return (0);
}

/*
 * Initialize on module load
 */
static int
plasma_init(video_adapter_t *adp)
{
	video_info_t info;
	int i;

	/* select video mode */
	for (i = 0; modes[i] >= 0; ++i)
		if (vidd_get_info(adp, modes[i], &info) == 0)
			break;
	if (modes[i] < 0) {
		log(LOG_NOTICE, "%s: no supported video modes\n", SAVER_NAME);
		return (ENODEV);
	}
	scrmode = modes[i];
	scrw = info.vi_width;
	scrh = info.vi_height;

	/* initialize the palette */
	for (i = 0; i < 256; ++i)
		plasma_pal[i].r = plasma_pal[i].g = plasma_pal[i].b = i;

	/* randomize the foci */
	for (i = 0; i < FOCI; i++) {
		plasma_foci[i].x = random() % scrw;
		plasma_foci[i].y = random() % scrh;
		plasma_foci[i].vx = random() % 5 - 2;
		plasma_foci[i].vy = random() % 5 - 2;
	}

	return (0);
}

/*
 * Clean up before module unload
 */
static int
plasma_term(video_adapter_t *adp)
{

	return (0);
}

/*
 * Boilerplate
 */
static scrn_saver_t plasma_module = {
	SAVER_NAME,
	plasma_init,
	plasma_term,
	plasma_saver,
	NULL
};

SAVER_MODULE(plasma_saver, plasma_module);

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1998 Dag-Erling Coïdan Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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

#define SAVER_NAME	 "warp_saver"
#define SPP		 15
#define STARS		 (SPP * (1 + 2 + 4 + 8))

#define SET_ORIGIN(adp, o) do {				\
	int oo = o;					\
	if (oo != last_origin)				\
	    vidd_set_win_org(adp, last_origin = oo);		\
	} while (0)

static u_char		*vid;
static int		 banksize, scrmode, bpsl, scrw, scrh;
static int		 blanked;
static int		 star[STARS];
static u_char		 warp_pal[768] = {
	0x00, 0x00, 0x00,
	0x66, 0x66, 0x66,
	0x99, 0x99, 0x99,
	0xcc, 0xcc, 0xcc,
	0xff, 0xff, 0xff
	/* the rest is zero-filled by the compiler */
};

static void
warp_update(video_adapter_t *adp)
{
	int i, j, k, n, o, p;
	int last_origin = -1;

	for (i = 1, k = 0, n = SPP*8; i < 5; i++, n /= 2) {
		for (j = 0; j < n; j++, k++) {
			p = (star[k] / scrw) *  bpsl + (star[k] % scrw);
			o = 0;
			while (p > banksize) {
				p -= banksize;
				o += banksize;
			}
			SET_ORIGIN(adp, o);
			vid[p] = 0;
			star[k] += i;
			if (star[k] > scrw*scrh)
				star[k] -= scrw*scrh;
			p = (star[k] / scrw) *  bpsl + (star[k] % scrw);
			o = 0;
			while (p > banksize) {
				p -= banksize;
				o += banksize;
			}
			SET_ORIGIN(adp, o);
			vid[p] = i;
		}
	}
}

static int
warp_saver(video_adapter_t *adp, int blank)
{
	int pl;
	
	if (blank) {
		/* switch to graphics mode */
		if (blanked <= 0) {
			pl = splhigh();
			vidd_set_mode(adp, scrmode);
			vidd_load_palette(adp, warp_pal);
			vidd_set_border(adp, 0);
			blanked++;
			vid = (u_char *)adp->va_window;
			banksize = adp->va_window_size;
			bpsl = adp->va_line_width;
			splx(pl);
			vidd_clear(adp);
		}
		/* update display */
		warp_update(adp);
	} else {
		blanked = 0;
	}
	return (0);
}

static int
warp_init(video_adapter_t *adp)
{
	video_info_t info;
	int i;
	
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

	/* randomize the star field */
	for (i = 0; i < STARS; i++)
		star[i] = random() % (scrw * scrh);
	
	return (0);
}

static int
warp_term(video_adapter_t *adp)
{
	return (0);
}

static scrn_saver_t warp_module = {
	SAVER_NAME,
	warp_init,
	warp_term,
	warp_saver,
	NULL
};

SAVER_MODULE(warp_saver, warp_module);

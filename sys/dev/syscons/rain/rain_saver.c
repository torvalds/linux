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

#define SAVER_NAME	 "rain_saver"
#ifdef MAX
#undef MAX
#endif
#define MAX		 63	/* number of colors (in addition to black) */
#define INCREMENT	 4	/* increment between colors */

#define RED(n)		 ((n) * 3 + 0)
#define GREEN(n)	 ((n) * 3 + 1)
#define BLUE(n)		 ((n) * 3 + 2)

#define SET_ORIGIN(adp, o) do {				\
	int oo = o;					\
	if (oo != last_origin)				\
	    vidd_set_win_org(adp, last_origin = oo);	\
	} while (0)

static u_char		*vid;
static int		 banksize, scrmode, bpsl, scrw, scrh;
static u_char		 rain_pal[768];
static int		 blanked;

static void
rain_update(video_adapter_t *adp)
{
	int i, t;

	t = rain_pal[BLUE(MAX)];
	for (i = MAX; i > 1; i--)
		rain_pal[BLUE(i)] = rain_pal[BLUE(i - 1)];
	rain_pal[BLUE(1)] = t;
	vidd_load_palette(adp, rain_pal);
}

static int
rain_saver(video_adapter_t *adp, int blank)
{
	int i, j, o, p, pl;
	u_char temp;
	int last_origin = -1;

	if (blank) {
		/* switch to graphics mode */
		if (blanked <= 0) {
			pl = splhigh();
			vidd_set_mode(adp, scrmode);
			vidd_load_palette(adp, rain_pal);
			vidd_set_border(adp, 0);
			blanked++;
			vid = (u_char *)adp->va_window;
			banksize = adp->va_window_size;
			bpsl = adp->va_line_width;
			splx(pl);
			for (i = 0; i < bpsl*scrh; i += banksize) {
				SET_ORIGIN(adp, i);
				if ((bpsl * scrh - i) < banksize)
					bzero(vid, bpsl * scrh - i);
				else
					bzero(vid, banksize);
			}
			SET_ORIGIN(adp, 0);
			for (i = 0, o = 0, p = 0; i < scrw; i += 2, p += 2) {
				if (p > banksize) {
					p -= banksize;
					o += banksize;
					SET_ORIGIN(adp, o);
				}
				vid[p] = 1 + (random() % MAX);
			}
			o = 0; p = 0;
			for (j = 1; j < scrh; j++)
			  for (i = 0, p = bpsl * (j - 1) - o; i < scrw; i += 2, p+= 2) {
			  	while (p > banksize) {
					p -= banksize;
					o += banksize;
				}
				SET_ORIGIN(adp, o);
				temp = (vid[p] < MAX) ? 1 + vid[p] : 1;
				if (p + bpsl < banksize) {
					vid[p + bpsl] = temp;
				} else {
					SET_ORIGIN(adp, o + banksize);
					vid[p + bpsl - banksize] = temp;
				}
			  }
		}
		
		/* update display */
		rain_update(adp);
	} else {
		blanked = 0;
	}
	return (0);
}

static int
rain_init(video_adapter_t *adp)
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

	/* intialize the palette */
	for (i = 1; i < MAX; i++)
		rain_pal[BLUE(i)] = rain_pal[BLUE(i - 1)] + INCREMENT;
	
	return (0);
}

static int
rain_term(video_adapter_t *adp)
{
	return (0);
}

static scrn_saver_t rain_module = {
	SAVER_NAME,
	rain_init,
	rain_term,
	rain_saver,
	NULL
};

SAVER_MODULE(rain_saver, rain_module);

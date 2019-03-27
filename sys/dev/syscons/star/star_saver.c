/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1995-1998 Søren Schmidt
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
#include <sys/consio.h>
#include <sys/fbio.h>

#include <machine/pc/display.h>

#include <dev/fb/fbreg.h>
#include <dev/fb/splashreg.h>
#include <dev/syscons/syscons.h>

#define NUM_STARS	50

static int blanked;

/*
 * Alternate saver that got its inspiration from a well known utility
 * package for an inferior^H^H^H^H^H^Hfamous OS.
 */
static int
star_saver(video_adapter_t *adp, int blank)
{
	sc_softc_t	*sc;
	scr_stat	*scp;
	int		cell, i;
	static u_char	pattern[] = {"...........++++***   "};
	static char	color16[] = {FG_DARKGREY, FG_LIGHTGREY,
				     FG_WHITE, FG_LIGHTCYAN};
	static u_short 	stars[NUM_STARS][2];

	sc = sc_find_softc(adp, NULL);
	if (sc == NULL)
		return EAGAIN;
	scp = sc->cur_scp;

	if (blank) {
		if (adp->va_info.vi_flags & V_INFO_GRAPHICS)
			return EAGAIN;
		if (!blanked) {
			/* clear the screen and set the border color */
			sc_vtb_clear(&scp->scr, sc->scr_map[0x20],
				     (FG_LIGHTGREY | BG_BLACK) << 8);
			vidd_set_hw_cursor(adp, -1, -1);
			sc_set_border(scp, 0);
			blanked = TRUE;
			for(i=0; i<NUM_STARS; i++) {
				stars[i][0] =
					random() % (scp->xsize*scp->ysize);
				stars[i][1] = 0;
			}
		}
		cell = random() % NUM_STARS;
		sc_vtb_putc(&scp->scr, stars[cell][0], 
			    sc->scr_map[pattern[stars[cell][1]]],
			    color16[random()%sizeof(color16)] << 8);
		if ((stars[cell][1]+=(random()%4)) >= sizeof(pattern)-1) {
			stars[cell][0] = random() % (scp->xsize*scp->ysize);
			stars[cell][1] = 0;
		}
	} else
		blanked = FALSE;

	return 0;
}

static int
star_init(video_adapter_t *adp)
{
	blanked = FALSE;
	return 0;
}

static int
star_term(video_adapter_t *adp)
{
	return 0;
}

static scrn_saver_t star_module = {
	"star_saver", star_init, star_term, star_saver, NULL,
};

SAVER_MODULE(star_saver, star_module);

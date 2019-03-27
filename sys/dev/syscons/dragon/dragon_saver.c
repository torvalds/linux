/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2000 Chiharu Shibata
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

#include	<sys/param.h>
#include	<sys/systm.h>
#include	<sys/kernel.h>
#include	<sys/module.h>
#include	<sys/syslog.h>
#include	<sys/consio.h>
#include	<sys/fbio.h>

#include	<sys/random.h>

#include	<dev/fb/fbreg.h>
#include	<dev/fb/splashreg.h>
#include	<dev/syscons/syscons.h>

#define SAVER_NAME	 "dragon_saver"

static u_char	*vid;
static int	blanked;

#define	VIDEO_MODE	M_VGA_CG320
#define	VIDEO_MODE_NAME	"M_VGA_CG320"
#define	SCRW	320
#define	SCRH	200
#define	ORDER	13
#define	CURVE	3
#define	OUT	100

static int	cur_x, cur_y;
static int	curve;
static u_char	dragon_pal[3*256];	/* zero-filled by the compiler */

static __inline int
gpset(int x, int y, int val)
{
	if (x < 0 || y < 0 || SCRW <= x || SCRH <= y) {
		return 0;
	}
	vid[x + y * SCRW] = val;
	return 1;
}

static int
gdraw(int dx, int dy, int val)
{
	int	i;
	int	set = 0;

	if (dx != 0) {
		i = cur_x;
		cur_x += dx;
		if (dx < 0) {
			i += dx;
			dx = -dx;
		}
		/* horizontal line */
		for (; dx >= 0; --dx, ++i) {
			set |= gpset(i, cur_y, val);
		} 
	}
	else {	/* dy != 0 */
		i = cur_y;
		cur_y += dy;
		if (dy < 0) {
			i += dy;
			dy = -dy;
		}
		/* vertical line */
		for (; dy >= 0; --dy, ++i) {
			set |= gpset(cur_x, i, val);
		} 
	}
	return set;
}

static void
dragon_update(video_adapter_t *adp)
{
	static int	i, p, q;
	static int	order, mul, out;
	static int	org_x, org_y;
	static int	dx, dy;
	static unsigned char	fold[1 << (ORDER - 3)];
#define	GET_FOLD(x)	(fold[(x) >> 3]  &  (1 << ((x) & 7)))
#define	SET_FOLD(x)	(fold[(x) >> 3] |=  (1 << ((x) & 7)))
#define	CLR_FOLD(x)	(fold[(x) >> 3] &= ~(1 << ((x) & 7)))
	int	tmp;

	if (curve > CURVE) {
		vidd_clear(adp);

		/* set palette of each curves */
		for (tmp = 0; tmp < 3*CURVE; ++tmp) {
			dragon_pal[3+tmp] = (u_char)random(); 
		}
		vidd_load_palette(adp, dragon_pal);

		mul = ((random() & 7) + 1) * (SCRW / 320);
		org_x = random() % SCRW; org_y = random() % SCRH;

		curve = 0;
		order = ORDER;
	}

	if (order >= ORDER) {
		++curve;

		cur_x = org_x; cur_y = org_y;

		switch (curve) {
		case 1:
			dx = 0; dy = mul;
			break;
		case 2:
			dx = mul; dy = 0;
			break;
		case 3:
			dx = 0; dy = -mul;
			break;
		}
		(void)gdraw(dx, dy, curve); out = 0;

		order = 0;
		q = p = 0; i = q + 1;
	}

	if (i > q) {
		SET_FOLD(p); q = p * 2;

		++order;
		i = p; p = q + 1;
	}

	if (GET_FOLD(q-i) != 0) {
		CLR_FOLD(i);
		tmp = dx; dx =  dy; dy = -tmp;	/* turn right */
	}
	else {
		SET_FOLD(i);
		tmp = dx; dx = -dy; dy =  tmp;	/* turn left */
	}
	if (gdraw(dx, dy, curve)) {
		out = 0;
	}
	else {
		if (++out > OUT) {
			order = ORDER;	/* force to terminate this curve */
		}
	}
	++i;
}

static int
dragon_saver(video_adapter_t *adp, int blank)
{
	int pl;

	if (blank) {
		/* switch to graphics mode */
		if (blanked <= 0) {
			pl = splhigh();
			vidd_set_mode(adp, VIDEO_MODE);
			vid = (u_char *)adp->va_window;
			curve = CURVE + 1;
			++blanked;
			splx(pl);
		}

		/* update display */
		dragon_update(adp);
	}
	else {
		blanked = 0;
	}
	return 0;
}

static int
dragon_init(video_adapter_t *adp)
{
	video_info_t info;

	/* check that the console is capable of running in 320x200x256 */
	if (vidd_get_info(adp, VIDEO_MODE, &info)) {
		log(LOG_NOTICE,
		    "%s: the console does not support " VIDEO_MODE_NAME "\n",
		    SAVER_NAME);
		return ENODEV;
	}

	blanked = 0;
	return 0;
}

static int
dragon_term(video_adapter_t *adp)
{
	return 0;
}

static scrn_saver_t dragon_module = {
	SAVER_NAME,
	dragon_init,
	dragon_term,
	dragon_saver,
	NULL,
};

SAVER_MODULE(dragon_saver, dragon_module);

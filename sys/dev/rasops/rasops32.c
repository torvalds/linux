/*	$OpenBSD: rasops32.c,v 1.14 2024/07/21 13:18:15 fcambus Exp $	*/
/*	$NetBSD: rasops32.c,v 1.7 2000/04/12 14:22:29 pk Exp $	*/

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Andrew Doran.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>

#include <dev/wscons/wsdisplayvar.h>
#include <dev/wscons/wsconsio.h>
#include <dev/rasops/rasops.h>

int	rasops32_putchar(void *, int, int, u_int, uint32_t);

/*
 * Initialize a 'rasops_info' descriptor for this depth.
 */
void
rasops32_init(struct rasops_info *ri)
{
	if (ri->ri_rnum == 0) {
		ri->ri_rnum = 8;
		ri->ri_rpos = 0;
		ri->ri_gnum = 8;
		ri->ri_gpos = 8;
		ri->ri_bnum = 8;
		ri->ri_bpos = 16;
	}

	ri->ri_ops.putchar = rasops32_putchar;
}

/*
 * Paint a single character.
 */
int
rasops32_putchar(void *cookie, int row, int col, u_int uc, uint32_t attr)
{
	int width, height, step, cnt, fs, b, f;
	uint32_t fb, clr[2];
	struct rasops_info *ri;
	int64_t *rp;
	union {
		int64_t q[4];
		int32_t d[4][2];
	} u;
	u_char *fr;

	ri = (struct rasops_info *)cookie;

#ifdef RASOPS_CLIPPING
	/* Catches 'row < 0' case too */
	if ((unsigned)row >= (unsigned)ri->ri_rows)
		return 0;

	if ((unsigned)col >= (unsigned)ri->ri_cols)
		return 0;
#endif

	rp = (int64_t *)(ri->ri_bits + row*ri->ri_yscale + col*ri->ri_xscale);

	height = ri->ri_font->fontheight;
	width = ri->ri_font->fontwidth;
	step = ri->ri_stride >> 3;

	b = ri->ri_devcmap[(attr >> 16) & 0xf];
	f = ri->ri_devcmap[(attr >> 24) & 0xf];
	u.d[0][0] = b; u.d[0][1] = b;
	u.d[1][0] = b; u.d[1][1] = f;
	u.d[2][0] = f; u.d[2][1] = b;
	u.d[3][0] = f; u.d[3][1] = f;

	if (uc == ' ') {
		while (height--) {
			/* the general, pixel-at-a-time case is fast enough */
			for (cnt = 0; cnt < width; cnt++)
				((int *)rp)[cnt] = b;
			rp += step;
		}
	} else {
		uc -= ri->ri_font->firstchar;
		fr = (u_char *)ri->ri_font->data + uc * ri->ri_fontscale;
		fs = ri->ri_font->stride;

		/* double-pixel special cases for the common widths */
		switch (width) {
		case 6:
			while (height--) {
				fb = fr[0];
				rp[0] = u.q[fb >> 6];
				rp[1] = u.q[(fb >> 4) & 3];
				rp[2] = u.q[(fb >> 2) & 3];
				rp += step;
				fr += 1;
			}
			break;

		case 8:
			while (height--) {
				fb = fr[0];
				rp[0] = u.q[fb >> 6];
				rp[1] = u.q[(fb >> 4) & 3];
				rp[2] = u.q[(fb >> 2) & 3];
				rp[3] = u.q[fb & 3];
				rp += step;
				fr += 1;
			}
			break;

		case 12:
			while (height--) {
				fb = fr[0];
				rp[0] = u.q[fb >> 6];
				rp[1] = u.q[(fb >> 4) & 3];
				rp[2] = u.q[(fb >> 2) & 3];
				rp[3] = u.q[fb & 3];
				fb = fr[1];
				rp[4] = u.q[fb >> 6];
				rp[5] = u.q[(fb >> 4) & 3];
				rp += step;
				fr += 2;
			}
			break;

		case 16:
			while (height--) {
				fb = fr[0];
				rp[0] = u.q[fb >> 6];
				rp[1] = u.q[(fb >> 4) & 3];
				rp[2] = u.q[(fb >> 2) & 3];
				rp[3] = u.q[fb & 3];
				fb = fr[1];
				rp[4] = u.q[fb >> 6];
				rp[5] = u.q[(fb >> 4) & 3];
				rp[6] = u.q[(fb >> 2) & 3];
				rp[7] = u.q[fb & 3];
				rp += step;
				fr += 2;
			}
			break;

		case 32:
			while (height--) {
				fb = fr[0];
				rp[0] = u.q[fb >> 6];
				rp[1] = u.q[(fb >> 4) & 3];
				rp[2] = u.q[(fb >> 2) & 3];
				rp[3] = u.q[fb & 3];
				fb = fr[1];
				rp[4] = u.q[fb >> 6];
				rp[5] = u.q[(fb >> 4) & 3];
				rp[6] = u.q[(fb >> 2) & 3];
				rp[7] = u.q[fb & 3];
				fb = fr[2];
				rp[8] = u.q[fb >> 6];
				rp[9] = u.q[(fb >> 4) & 3];
				rp[10] = u.q[(fb >> 2) & 3];
				rp[11] = u.q[fb & 3];
				fb = fr[3];
				rp[12] = u.q[fb >> 6];
				rp[13] = u.q[(fb >> 4) & 3];
				rp[14] = u.q[(fb >> 2) & 3];
				rp[15] = u.q[fb & 3];
				rp += step;
				fr += 4;
			}
			break;

		default: /* there is a 5x8 font, so fall back to per-pixel */
			clr[0] = b;
			clr[1] = f;

			while (height--) {
				fb = fr[3] | (fr[2] << 8) | (fr[1] << 16) |
				    (fr[0] << 24);
				fr += fs;

				for (cnt = 0; cnt < width; cnt++) {
					((int *)rp)[cnt] = clr[fb >> 31];
					fb <<= 1;
				}
				rp += step;
			}
			break;
		}
	}

	/* Do underline a pixel at a time */
	if ((attr & WSATTR_UNDERLINE) != 0) {
		rp -= step;
		for (cnt = 0; cnt < width; cnt++)
			((int *)rp)[cnt] = f;
	}

	return 0;
}

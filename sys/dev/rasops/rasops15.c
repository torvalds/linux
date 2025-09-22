/*	$OpenBSD: rasops15.c,v 1.10 2023/01/18 11:08:49 nicm Exp $	*/
/*	$NetBSD: rasops15.c,v 1.7 2000/04/12 14:22:29 pk Exp $	*/

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

int 	rasops15_putchar(void *, int, int, u_int, uint32_t attr);
#ifndef RASOPS_SMALL
int 	rasops15_putchar8(void *, int, int, u_int, uint32_t attr);
int 	rasops15_putchar12(void *, int, int, u_int, uint32_t attr);
int 	rasops15_putchar16(void *, int, int, u_int, uint32_t attr);
void	rasops15_makestamp(struct rasops_info *, uint32_t);

/*
 * (2x2)x1 stamp for optimized character blitting
 */
static int32_t	stamp[32];
static uint32_t	stamp_attr;
static int	stamp_mutex;	/* XXX see note in readme */
#endif

/*
 * XXX this confuses the hell out of gcc2 (not egcs) which always insists
 * that the shift count is negative.
 *
 * offset = STAMP_SHIFT(fontbits, nibble #) & STAMP_MASK
 * destination int32_t[0] = STAMP_READ(offset)
 * destination int32_t[1] = STAMP_READ(offset + 4)
 */
#define STAMP_SHIFT(fb,n)	((n*4-3) >= 0 ? (fb)>>(n*4-3):(fb)<<-(n*4-3))
#define STAMP_MASK		(15 << 3)
#define STAMP_READ(o)		(*(int32_t *)((caddr_t)stamp + (o)))

/*
 * Initialize rasops_info struct for this colordepth.
 */
void
rasops15_init(struct rasops_info *ri)
{

	switch (ri->ri_font->fontwidth) {
#ifndef RASOPS_SMALL
	case 8:
		ri->ri_ops.putchar = rasops15_putchar8;
		break;

	case 12:
		ri->ri_ops.putchar = rasops15_putchar12;
		break;

	case 16:
		ri->ri_ops.putchar = rasops15_putchar16;
		break;
#endif	/* !RASOPS_SMALL */
	default:
		ri->ri_ops.putchar = rasops15_putchar;
		break;
	}

	if (ri->ri_rnum == 0) {
		ri->ri_rnum = 5;
		ri->ri_rpos = 0;
		ri->ri_gnum = 5 + (ri->ri_depth == 16);
		ri->ri_gpos = 5;
		ri->ri_bnum = 5;
		ri->ri_bpos = 10 + (ri->ri_depth == 16);
	}
}

/*
 * Paint a single character.
 */
int
rasops15_putchar(void *cookie, int row, int col, u_int uc, uint32_t attr)
{
	int fb, width, height, cnt, clr[2];
	struct rasops_info *ri;
	u_char *dp, *rp, *fr;

	ri = (struct rasops_info *)cookie;

#ifdef RASOPS_CLIPPING
	/* Catches 'row < 0' case too */
	if ((unsigned)row >= (unsigned)ri->ri_rows)
		return 0;

	if ((unsigned)col >= (unsigned)ri->ri_cols)
		return 0;
#endif

	rp = ri->ri_bits + row * ri->ri_yscale + col * ri->ri_xscale;
	height = ri->ri_font->fontheight;
	width = ri->ri_font->fontwidth;

	clr[1] = ri->ri_devcmap[((u_int)attr >> 24) & 0xf];
	clr[0] = ri->ri_devcmap[((u_int)attr >> 16) & 0xf];

	if (uc == ' ') {
		int16_t c = (int16_t)clr[0];
		while (height--) {
			dp = rp;
			rp += ri->ri_stride;

			for (cnt = width; cnt; cnt--) {
				*(int16_t *)dp = c;
				dp += 2;
			}
		}
	} else {
		uc -= ri->ri_font->firstchar;
		fr = (u_char *)ri->ri_font->data + uc * ri->ri_fontscale;

		while (height--) {
			dp = rp;
			fb = fr[3] | (fr[2] << 8) | (fr[1] << 16) | (fr[0] << 24);
			fr += ri->ri_font->stride;
			rp += ri->ri_stride;

			for (cnt = width; cnt; cnt--) {
				*(int16_t *)dp = (int16_t)clr[(fb >> 31) & 1];
				fb <<= 1;
				dp += 2;
			}
		}
	}

	/* Do underline */
	if ((attr & WSATTR_UNDERLINE) != 0) {
		int16_t c = (int16_t)clr[1];
		rp -= ri->ri_stride << 1;

		while (width--) {
			*(int16_t *)rp = c;
			rp += 2;
		}
	}

	return 0;
}

#ifndef RASOPS_SMALL
/*
 * Recompute the (2x2)x1 blitting stamp.
 */
void
rasops15_makestamp(struct rasops_info *ri, uint32_t attr)
{
	int32_t fg, bg;
	int i;

	fg = ri->ri_devcmap[((u_int)attr >> 24) & 0xf] & 0xffff;
	bg = ri->ri_devcmap[((u_int)attr >> 16) & 0xf] & 0xffff;
	stamp_attr = attr;

	for (i = 0; i < 32; i += 2) {
#if BYTE_ORDER == LITTLE_ENDIAN
		stamp[i] = (i & 16 ? fg : bg);
		stamp[i] |= ((i & 8 ? fg : bg) << 16);
		stamp[i + 1] = (i & 4 ? fg : bg);
		stamp[i + 1] |= ((i & 2 ? fg : bg) << 16);
#else
		stamp[i + 1] = (i & 2 ? fg : bg);
		stamp[i + 1] |= ((i & 4 ? fg : bg) << 16);
		stamp[i] = (i & 8 ? fg : bg);
		stamp[i] |= ((i & 16 ? fg : bg) << 16);
#endif
	}
}

/*
 * Paint a single character. This is for 8-pixel wide fonts.
 */
int
rasops15_putchar8(void *cookie, int row, int col, u_int uc, uint32_t attr)
{
	struct rasops_info *ri;
	int height, so, fs;
	int32_t *rp;
	u_char *fr;

	/* Can't risk remaking the stamp if it's already in use */
	if (stamp_mutex++) {
		stamp_mutex--;
		return rasops15_putchar(cookie, row, col, uc, attr);
	}

	ri = (struct rasops_info *)cookie;

#ifdef RASOPS_CLIPPING
	if ((unsigned)row >= (unsigned)ri->ri_rows) {
		stamp_mutex--;
		return 0;
	}

	if ((unsigned)col >= (unsigned)ri->ri_cols) {
		stamp_mutex--;
		return 0;
	}
#endif

	/* Recompute stamp? */
	if (attr != stamp_attr)
		rasops15_makestamp(ri, attr);

	rp = (int32_t *)(ri->ri_bits + row*ri->ri_yscale + col*ri->ri_xscale);
	height = ri->ri_font->fontheight;

	if (uc == (u_int)-1) {
		int32_t c = stamp[0];
		while (height--) {
			rp[0] = rp[1] = rp[2] = rp[3] = c;
			DELTA(rp, ri->ri_stride, int32_t *);
		}
	} else {
		uc -= ri->ri_font->firstchar;
		fr = (u_char *)ri->ri_font->data + uc*ri->ri_fontscale;
		fs = ri->ri_font->stride;

		while (height--) {
			so = STAMP_SHIFT(fr[0], 1) & STAMP_MASK;
			rp[0] = STAMP_READ(so);
			rp[1] = STAMP_READ(so + 4);

			so = STAMP_SHIFT(fr[0], 0) & STAMP_MASK;
			rp[2] = STAMP_READ(so);
			rp[3] = STAMP_READ(so + 4);

			fr += fs;
			DELTA(rp, ri->ri_stride, int32_t *);
		}
	}

	/* Do underline */
	if ((attr & WSATTR_UNDERLINE) != 0) {
		int32_t c = STAMP_READ(28);

		DELTA(rp, -(ri->ri_stride << 1), int32_t *);
		rp[0] = rp[1] = rp[2] = rp[3] = c;
	}

	stamp_mutex--;

	return 0;
}

/*
 * Paint a single character. This is for 12-pixel wide fonts.
 */
int
rasops15_putchar12(void *cookie, int row, int col, u_int uc, uint32_t attr)
{
	struct rasops_info *ri;
	int height, so, fs;
	int32_t *rp;
	u_char *fr;

	/* Can't risk remaking the stamp if it's already in use */
	if (stamp_mutex++) {
		stamp_mutex--;
		return rasops15_putchar(cookie, row, col, uc, attr);
	}

	ri = (struct rasops_info *)cookie;

#ifdef RASOPS_CLIPPING
	if ((unsigned)row >= (unsigned)ri->ri_rows) {
		stamp_mutex--;
		return 0;
	}

	if ((unsigned)col >= (unsigned)ri->ri_cols) {
		stamp_mutex--;
		return 0;
	}
#endif

	/* Recompute stamp? */
	if (attr != stamp_attr)
		rasops15_makestamp(ri, attr);

	rp = (int32_t *)(ri->ri_bits + row*ri->ri_yscale + col*ri->ri_xscale);
	height = ri->ri_font->fontheight;

	if (uc == (u_int)-1) {
		int32_t c = stamp[0];
		while (height--) {
			rp[0] = rp[1] = rp[2] = rp[3] = rp[4] = rp[5] = c;
			DELTA(rp, ri->ri_stride, int32_t *);
		}
	} else {
		uc -= ri->ri_font->firstchar;
		fr = (u_char *)ri->ri_font->data + uc*ri->ri_fontscale;
		fs = ri->ri_font->stride;

		while (height--) {
			so = STAMP_SHIFT(fr[0], 1) & STAMP_MASK;
			rp[0] = STAMP_READ(so);
			rp[1] = STAMP_READ(so + 4);

			so = STAMP_SHIFT(fr[0], 0) & STAMP_MASK;
			rp[2] = STAMP_READ(so);
			rp[3] = STAMP_READ(so + 4);

			so = STAMP_SHIFT(fr[1], 1) & STAMP_MASK;
			rp[4] = STAMP_READ(so);
			rp[5] = STAMP_READ(so + 4);

			fr += fs;
			DELTA(rp, ri->ri_stride, int32_t *);
		}
	}

	/* Do underline */
	if (attr & WSATTR_UNDERLINE) {
		int32_t c = STAMP_READ(28);

		DELTA(rp, -(ri->ri_stride << 1), int32_t *);
		rp[0] = rp[1] = rp[2] = rp[3] = rp[4] = rp[5] = c;
	}

	stamp_mutex--;

	return 0;
}

/*
 * Paint a single character. This is for 16-pixel wide fonts.
 */
int
rasops15_putchar16(void *cookie, int row, int col, u_int uc, uint32_t attr)
{
	struct rasops_info *ri;
	int height, so, fs;
	int32_t *rp;
	u_char *fr;

	/* Can't risk remaking the stamp if it's already in use */
	if (stamp_mutex++) {
		stamp_mutex--;
		return rasops15_putchar(cookie, row, col, uc, attr);
	}

	ri = (struct rasops_info *)cookie;

#ifdef RASOPS_CLIPPING
	if ((unsigned)row >= (unsigned)ri->ri_rows) {
		stamp_mutex--;
		return 0;
	}

	if ((unsigned)col >= (unsigned)ri->ri_cols) {
		stamp_mutex--;
		return 0;
	}
#endif

	/* Recompute stamp? */
	if (attr != stamp_attr)
		rasops15_makestamp(ri, attr);

	rp = (int32_t *)(ri->ri_bits + row*ri->ri_yscale + col*ri->ri_xscale);
	height = ri->ri_font->fontheight;

	if (uc == (u_int)-1) {
		int32_t c = stamp[0];
		while (height--) {
			rp[0] = rp[1] = rp[2] = rp[3] =
			rp[4] = rp[5] = rp[6] = rp[7] = c;
			DELTA(rp, ri->ri_stride, int32_t *);
		}
	} else {
		uc -= ri->ri_font->firstchar;
		fr = (u_char *)ri->ri_font->data + uc*ri->ri_fontscale;
		fs = ri->ri_font->stride;

		while (height--) {
			so = STAMP_SHIFT(fr[0], 1) & STAMP_MASK;
			rp[0] = STAMP_READ(so);
			rp[1] = STAMP_READ(so + 4);

			so = STAMP_SHIFT(fr[0], 0) & STAMP_MASK;
			rp[2] = STAMP_READ(so);
			rp[3] = STAMP_READ(so + 4);

			so = STAMP_SHIFT(fr[1], 1) & STAMP_MASK;
			rp[4] = STAMP_READ(so);
			rp[5] = STAMP_READ(so + 4);

			so = STAMP_SHIFT(fr[1], 0) & STAMP_MASK;
			rp[6] = STAMP_READ(so);
			rp[7] = STAMP_READ(so + 4);

			DELTA(rp, ri->ri_stride, int32_t *);
			fr += fs;
		}
	}

	/* Do underline */
	if (attr & WSATTR_UNDERLINE) {
		int32_t c = STAMP_READ(28);

		DELTA(rp, -(ri->ri_stride << 1), int32_t *);
		rp[0] = rp[1] = rp[2] = rp[3] =
		rp[4] = rp[5] = rp[6] = rp[7] = c;
	}

	stamp_mutex--;

	return 0;
}
#endif	/* !RASOPS_SMALL */

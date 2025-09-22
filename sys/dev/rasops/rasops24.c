/*	$OpenBSD: rasops24.c,v 1.13 2023/01/18 11:08:49 nicm Exp $	*/
/*	$NetBSD: rasops24.c,v 1.12 2000/04/12 14:22:29 pk Exp $	*/

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
#include <sys/endian.h>

#include <dev/wscons/wsdisplayvar.h>
#include <dev/wscons/wsconsio.h>
#include <dev/rasops/rasops.h>

int 	rasops24_putchar(void *, int, int, u_int, uint32_t attr);
#ifndef RASOPS_SMALL
int 	rasops24_putchar8(void *, int, int, u_int, uint32_t attr);
int 	rasops24_putchar12(void *, int, int, u_int, uint32_t attr);
int 	rasops24_putchar16(void *, int, int, u_int, uint32_t attr);
void	rasops24_makestamp(struct rasops_info *, uint32_t);

/*
 * 4x1 stamp for optimized character blitting
 */
static int32_t	stamp[64];
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
 * destination int32_t[2] = STAMP_READ(offset + 8)
 */
#define STAMP_SHIFT(fb,n)	((n*4-4) >= 0 ? (fb)>>(n*4-4):(fb)<<-(n*4-4))
#define STAMP_MASK		(0xf << 4)
#define STAMP_READ(o)		(*(int32_t *)((caddr_t)stamp + (o)))

/*
 * Initialize rasops_info struct for this colordepth.
 */
void
rasops24_init(struct rasops_info *ri)
{

	switch (ri->ri_font->fontwidth) {
#ifndef RASOPS_SMALL
	case 8:
		ri->ri_ops.putchar = rasops24_putchar8;
		break;
	case 12:
		ri->ri_ops.putchar = rasops24_putchar12;
		break;
	case 16:
		ri->ri_ops.putchar = rasops24_putchar16;
		break;
#endif
	default:
		ri->ri_ops.putchar = rasops24_putchar;
		break;
	}

	if (ri->ri_rnum == 0) {
		ri->ri_rnum = 8;
		ri->ri_rpos = 0;
		ri->ri_gnum = 8;
		ri->ri_gpos = 8;
		ri->ri_bnum = 8;
		ri->ri_bpos = 16;
	}
}

/*
 * Put a single character. This is the generic version.
 * XXX this bites - we should use masks.
 */
int
rasops24_putchar(void *cookie, int row, int col, u_int uc, uint32_t attr)
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
		while (height--) {
			dp = rp;
			rp += ri->ri_stride;

			for (cnt = width; cnt; cnt--) {
				*dp++ = clr[0] >> 16;
				*dp++ = clr[0] >> 8;
				*dp++ = clr[0];
			}
		}
	} else {
		uc -= ri->ri_font->firstchar;
		fr = (u_char *)ri->ri_font->data + uc * ri->ri_fontscale;

		while (height--) {
			dp = rp;
			fb = fr[3] | (fr[2] << 8) | (fr[1] << 16) |
			    (fr[0] << 24);
			fr += ri->ri_font->stride;
			rp += ri->ri_stride;

			for (cnt = width; cnt; cnt--, fb <<= 1) {
				if ((fb >> 31) & 1) {
					*dp++ = clr[1] >> 16;
					*dp++ = clr[1] >> 8;
					*dp++ = clr[1];
				} else {
					*dp++ = clr[0] >> 16;
					*dp++ = clr[0] >> 8;
					*dp++ = clr[0];
				}
			}
		}
	}

	/* Do underline */
	if ((attr & WSATTR_UNDERLINE) != 0) {
		rp -= ri->ri_stride << 1;

		while (width--) {
			*rp++ = clr[1] >> 16;
			*rp++ = clr[1] >> 8;
			*rp++ = clr[1];
		}
	}

	return 0;
}

#ifndef RASOPS_SMALL
/*
 * Recompute the blitting stamp.
 */
void
rasops24_makestamp(struct rasops_info *ri, uint32_t attr)
{
	u_int fg, bg, c1, c2, c3, c4;
	int i;

	fg = ri->ri_devcmap[((u_int)attr >> 24) & 0xf] & 0xffffff;
	bg = ri->ri_devcmap[((u_int)attr >> 16) & 0xf] & 0xffffff;
	stamp_attr = attr;

	for (i = 0; i < 64; i += 4) {
#if BYTE_ORDER == LITTLE_ENDIAN
		c1 = (i & 32 ? fg : bg);
		c2 = (i & 16 ? fg : bg);
		c3 = (i & 8 ? fg : bg);
		c4 = (i & 4 ? fg : bg);
#else
		c1 = (i & 8 ? fg : bg);
		c2 = (i & 4 ? fg : bg);
		c3 = (i & 16 ? fg : bg);
		c4 = (i & 32 ? fg : bg);
#endif
		stamp[i+0] = (c1 <<  8) | (c2 >> 16);
		stamp[i+1] = (c2 << 16) | (c3 >>  8);
		stamp[i+2] = (c3 << 24) | c4;

#if BYTE_ORDER == LITTLE_ENDIAN
		if ((ri->ri_flg & RI_BSWAP) == 0) {
#else
		if ((ri->ri_flg & RI_BSWAP) != 0) {
#endif
			stamp[i+0] = swap32(stamp[i+0]);
			stamp[i+1] = swap32(stamp[i+1]);
			stamp[i+2] = swap32(stamp[i+2]);
		}
	}
}

/*
 * Put a single character. This is for 8-pixel wide fonts.
 */
int
rasops24_putchar8(void *cookie, int row, int col, u_int uc, uint32_t attr)
{
	struct rasops_info *ri;
	int height, so, fs;
	int32_t *rp;
	u_char *fr;

	/* Can't risk remaking the stamp if it's already in use */
	if (stamp_mutex++) {
		stamp_mutex--;
		return rasops24_putchar(cookie, row, col, uc, attr);
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
		rasops24_makestamp(ri, attr);

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
			rp[2] = STAMP_READ(so + 8);

			so = STAMP_SHIFT(fr[0], 0) & STAMP_MASK;
			rp[3] = STAMP_READ(so);
			rp[4] = STAMP_READ(so + 4);
			rp[5] = STAMP_READ(so + 8);

			fr += fs;
			DELTA(rp, ri->ri_stride, int32_t *);
		}
	}

	/* Do underline */
	if ((attr & WSATTR_UNDERLINE) != 0) {
		int32_t c = STAMP_READ(52);

		DELTA(rp, -(ri->ri_stride << 1), int32_t *);
		rp[0] = rp[1] = rp[2] = rp[3] = rp[4] = rp[5] = c;
	}

	stamp_mutex--;

	return 0;
}

/*
 * Put a single character. This is for 12-pixel wide fonts.
 */
int
rasops24_putchar12(void *cookie, int row, int col, u_int uc, uint32_t attr)
{
	struct rasops_info *ri;
	int height, so, fs;
	int32_t *rp;
	u_char *fr;

	/* Can't risk remaking the stamp if it's already in use */
	if (stamp_mutex++) {
		stamp_mutex--;
		return rasops24_putchar(cookie, row, col, uc, attr);
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
		rasops24_makestamp(ri, attr);

	rp = (int32_t *)(ri->ri_bits + row*ri->ri_yscale + col*ri->ri_xscale);
	height = ri->ri_font->fontheight;

	if (uc == (u_int)-1) {
		int32_t c = stamp[0];
		while (height--) {
			rp[0] = rp[1] = rp[2] = rp[3] =
			rp[4] = rp[5] = rp[6] = rp[7] = rp[8] = c;
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
			rp[2] = STAMP_READ(so + 8);

			so = STAMP_SHIFT(fr[0], 0) & STAMP_MASK;
			rp[3] = STAMP_READ(so);
			rp[4] = STAMP_READ(so + 4);
			rp[5] = STAMP_READ(so + 8);

			so = STAMP_SHIFT(fr[1], 1) & STAMP_MASK;
			rp[6] = STAMP_READ(so);
			rp[7] = STAMP_READ(so + 4);
			rp[8] = STAMP_READ(so + 8);

			fr += fs;
			DELTA(rp, ri->ri_stride, int32_t *);
		}
	}

	/* Do underline */
	if ((attr & WSATTR_UNDERLINE) != 0) {
		int32_t c = STAMP_READ(52);

		DELTA(rp, -(ri->ri_stride << 1), int32_t *);
		rp[0] = rp[1] = rp[2] = rp[3] =
		rp[4] = rp[5] = rp[6] = rp[7] = rp[8] = c;
	}

	stamp_mutex--;

	return 0;
}

/*
 * Put a single character. This is for 16-pixel wide fonts.
 */
int
rasops24_putchar16(void *cookie, int row, int col, u_int uc, uint32_t attr)
{
	struct rasops_info *ri;
	int height, so, fs;
	int32_t *rp;
	u_char *fr;

	/* Can't risk remaking the stamp if it's already in use */
	if (stamp_mutex++) {
		stamp_mutex--;
		return rasops24_putchar(cookie, row, col, uc, attr);
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
		rasops24_makestamp(ri, attr);

	rp = (int32_t *)(ri->ri_bits + row*ri->ri_yscale + col*ri->ri_xscale);
	height = ri->ri_font->fontheight;

	if (uc == (u_int)-1) {
		int32_t c = stamp[0];
		while (height--) {
			rp[0] = rp[1] = rp[2] = rp[3] =
			rp[4] = rp[5] = rp[6] = rp[7] =
			rp[8] = rp[9] = rp[10] = rp[11] = c;
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
			rp[2] = STAMP_READ(so + 8);

			so = STAMP_SHIFT(fr[0], 0) & STAMP_MASK;
			rp[3] = STAMP_READ(so);
			rp[4] = STAMP_READ(so + 4);
			rp[5] = STAMP_READ(so + 8);

			so = STAMP_SHIFT(fr[1], 1) & STAMP_MASK;
			rp[6] = STAMP_READ(so);
			rp[7] = STAMP_READ(so + 4);
			rp[8] = STAMP_READ(so + 8);

			so = STAMP_SHIFT(fr[1], 0) & STAMP_MASK;
			rp[9] = STAMP_READ(so);
			rp[10] = STAMP_READ(so + 4);
			rp[11] = STAMP_READ(so + 8);

			DELTA(rp, ri->ri_stride, int32_t *);
			fr += fs;
		}
	}

	/* Do underline */
	if ((attr & WSATTR_UNDERLINE) != 0) {
		int32_t c = STAMP_READ(52);

		DELTA(rp, -(ri->ri_stride << 1), int32_t *);
		rp[0] = rp[1] = rp[2] = rp[3] =
		rp[4] = rp[5] = rp[6] = rp[7] =
		rp[8] = rp[9] = rp[10] = rp[11] = c;
	}

	stamp_mutex--;

	return 0;
}
#endif	/* !RASOPS_SMALL */

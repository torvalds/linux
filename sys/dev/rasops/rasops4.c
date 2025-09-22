/*	$OpenBSD: rasops4.c,v 1.13 2023/01/18 11:08:49 nicm Exp $	*/
/*	$NetBSD: rasops4.c,v 1.4 2001/11/15 09:48:15 lukem Exp $	*/

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
#include <dev/rasops/rasops_masks.h>

int	rasops4_copycols(void *, int, int, int, int);
int	rasops4_erasecols(void *, int, int, int, uint32_t);
int	rasops4_do_cursor(struct rasops_info *);
int	rasops4_putchar(void *, int, int col, u_int, uint32_t);
#ifndef RASOPS_SMALL
int	rasops4_putchar8(void *, int, int col, u_int, uint32_t);
int	rasops4_putchar12(void *, int, int col, u_int, uint32_t);
int	rasops4_putchar16(void *, int, int col, u_int, uint32_t);
void	rasops4_makestamp(struct rasops_info *, uint32_t);

/*
 * 4x1 stamp for optimized character blitting
 */
static u_int16_t	stamp[16];
static uint32_t	stamp_attr;
static int	stamp_mutex;	/* XXX see note in README */
#endif

/*
 * Initialize rasops_info struct for this colordepth.
 */
void
rasops4_init(struct rasops_info *ri)
{
	rasops_masks_init();

	switch (ri->ri_font->fontwidth) {
#ifndef RASOPS_SMALL
	case 8:
		ri->ri_ops.putchar = rasops4_putchar8;
		break;
	case 12:
		ri->ri_ops.putchar = rasops4_putchar12;
		break;
	case 16:
		ri->ri_ops.putchar = rasops4_putchar16;
		break;
#endif	/* !RASOPS_SMALL */
	default:
		panic("fontwidth not 8/12/16 or RASOPS_SMALL - fixme!");
		ri->ri_ops.putchar = rasops4_putchar;
		break;
	}

	if ((ri->ri_font->fontwidth & 1) != 0) {
		ri->ri_ops.erasecols = rasops4_erasecols;
		ri->ri_ops.copycols = rasops4_copycols;
		ri->ri_do_cursor = rasops4_do_cursor;
	}
}

#ifdef notyet
/*
 * Paint a single character. This is the generic version, this is ugly.
 */
int
rasops4_putchar(void *cookie, int row, int col, u_int uc, uint32_t attr)
{
	int height, width, fs, rs, fb, bg, fg, lmask, rmask;
	struct rasops_info *ri;
	int32_t *rp;
	u_char *fr;

	ri = (struct rasops_info *)cookie;

#ifdef RASOPS_CLIPPING
	/* Catches 'row < 0' case too */
	if ((unsigned)row >= (unsigned)ri->ri_rows)
		return 0;

	if ((unsigned)col >= (unsigned)ri->ri_cols)
		return 0;
#endif

	width = ri->ri_font->fontwidth << 1;
	height = ri->ri_font->fontheight;
	col *= width;
	rp = (int32_t *)(ri->ri_bits + row * ri->ri_yscale + ((col >> 3) & ~3));
	col = col & 31;
	rs = ri->ri_stride;

	bg = ri->ri_devcmap[(attr >> 16) & 0xf];
	fg = ri->ri_devcmap[(attr >> 24) & 0xf];

	/* If fg and bg match this becomes a space character */
	if (fg == bg || uc == ' ') {
		uc = (u_int)-1;
		fr = 0;		/* shutup gcc */
		fs = 0;		/* shutup gcc */
	} else {
		uc -= ri->ri_font->firstchar;
		fr = (u_char *)ri->ri_font->data + uc * ri->ri_fontscale;
		fs = ri->ri_font->stride;
	}

	/* Single word, one mask */
	if ((col + width) <= 32) {
		rmask = rasops_pmask[col][width];
		lmask = ~rmask;

		if (uc == (u_int)-1) {
			bg &= rmask;

			while (height--) {
				*rp = (*rp & lmask) | bg;
				DELTA(rp, rs, int32_t *);
			}
		} else {
			while (height--) {
				/* get bits, mask */
				/* compose sl */
				/* mask sl */
				/* put word */
			}
		}

		/* Do underline */
		if (attr & WSATTR_UNDERLINE) {
			DELTA(rp, -(ri->ri_stride << 1), int32_t *);
			*rp = (*rp & lmask) | (fg & rmask);
		}
	} else {
		lmask = ~rasops_lmask[col];
		rmask = ~rasops_rmask[(col + width) & 31];

		if (uc == (u_int)-1) {
			bg = bg & ~lmask;
			width = bg & ~rmask;

			while (height--) {
				rp[0] = (rp[0] & lmask) | bg;
				rp[1] = (rp[1] & rmask) | width;
				DELTA(rp, rs, int32_t *);
			}
		} else {
			width = 32 - col;

			/* NOT fontbits if bg is white */
			while (height--) {
				fb = ~(fr[3] | (fr[2] << 8) |
				    (fr[1] << 16) | (fr[0] << 24));

				rp[0] = (rp[0] & lmask)
				    | MBE((u_int)fb >> col);

				rp[1] = (rp[1] & rmask)
				   | (MBE((u_int)fb << width) & ~rmask);

				fr += fs;
				DELTA(rp, rs, int32_t *);
			}
		}

		/* Do underline */
		if (attr & WSATTR_UNDERLINE) {
			DELTA(rp, -(ri->ri_stride << 1), int32_t *);
			rp[0] = (rp[0] & lmask) | (fg & ~lmask);
			rp[1] = (rp[1] & rmask) | (fg & ~rmask);
		}
	}

	return 0;
}
#endif

/*
 * Put a single character. This is the generic version.
 */
int
rasops4_putchar(void *cookie, int row, int col, u_int uc, uint32_t attr)
{

	/* XXX punt */
	return (EAGAIN);
}

#ifndef RASOPS_SMALL
/*
 * Recompute the blitting stamp.
 */
void
rasops4_makestamp(struct rasops_info *ri, uint32_t attr)
{
	int i, fg, bg;

	fg = ri->ri_devcmap[(attr >> 24) & 0xf] & 0xf;
	bg = ri->ri_devcmap[(attr >> 16) & 0xf] & 0xf;
	stamp_attr = attr;

	for (i = 0; i < 16; i++) {
#if BYTE_ORDER == LITTLE_ENDIAN
		stamp[i] =  (i & 1 ? fg : bg) << 8;
		stamp[i] |= (i & 2 ? fg : bg) << 12;
		stamp[i] |= (i & 4 ? fg : bg) << 0;
		stamp[i] |= (i & 8 ? fg : bg) << 4;
#else
		stamp[i] =  (i & 1 ? fg : bg) << 0;
		stamp[i] |= (i & 2 ? fg : bg) << 4;
		stamp[i] |= (i & 4 ? fg : bg) << 8;
		stamp[i] |= (i & 8 ? fg : bg) << 12;
#endif
	}
}

/*
 * Put a single character. This is for 8-pixel wide fonts.
 */
int
rasops4_putchar8(void *cookie, int row, int col, u_int uc, uint32_t attr)
{
	struct rasops_info *ri;
	int height, fs, rs;
	u_char *fr;
	u_int16_t *rp;

	/* Can't risk remaking the stamp if it's already in use */
	if (stamp_mutex++) {
		stamp_mutex--;
		return rasops4_putchar(cookie, row, col, uc, attr);
	}

	ri = (struct rasops_info *)cookie;

#ifdef RASOPS_CLIPPING
	/* Catches 'row < 0' case too */
	if ((unsigned)row >= (unsigned)ri->ri_rows) {
		stamp_mutex--;
		return 0;
	}

	if ((unsigned)col >= (unsigned)ri->ri_cols) {
		stamp_mutex--;
		return 0;
	}
#endif

	rp = (u_int16_t *)(ri->ri_bits + row * ri->ri_yscale + col * ri->ri_xscale);
	height = ri->ri_font->fontheight;
	rs = ri->ri_stride / sizeof(*rp);

	/* Recompute stamp? */
	if (attr != stamp_attr)
		rasops4_makestamp(ri, attr);

	if (uc == ' ') {
		u_int16_t c = stamp[0];
		while (height--) {
			rp[0] = c;
			rp[1] = c;
			rp += rs;
		}
	} else {
		uc -= ri->ri_font->firstchar;
		fr = (u_char *)ri->ri_font->data + uc * ri->ri_fontscale;
		fs = ri->ri_font->stride;

		while (height--) {
			rp[0] = stamp[(*fr >> 4) & 0xf];
			rp[1] = stamp[*fr & 0xf];
			fr += fs;
			rp += rs;
		}
	}

	/* Do underline */
	if ((attr & WSATTR_UNDERLINE) != 0) {
		rp -= (rs << 1);
		rp[0] = stamp[15];
		rp[1] = stamp[15];
	}

	stamp_mutex--;

	return 0;
}

/*
 * Put a single character. This is for 12-pixel wide fonts.
 */
int
rasops4_putchar12(void *cookie, int row, int col, u_int uc, uint32_t attr)
{
	struct rasops_info *ri;
	int height, fs, rs;
	u_char *fr;
	u_int16_t *rp;

	/* Can't risk remaking the stamp if it's already in use */
	if (stamp_mutex++) {
		stamp_mutex--;
		return rasops4_putchar(cookie, row, col, uc, attr);
	}

	ri = (struct rasops_info *)cookie;

#ifdef RASOPS_CLIPPING
	/* Catches 'row < 0' case too */
	if ((unsigned)row >= (unsigned)ri->ri_rows) {
		stamp_mutex--;
		return 0;
	}

	if ((unsigned)col >= (unsigned)ri->ri_cols) {
		stamp_mutex--;
		return 0;
	}
#endif

	rp = (u_int16_t *)(ri->ri_bits + row * ri->ri_yscale + col * ri->ri_xscale);
	height = ri->ri_font->fontheight;
	rs = ri->ri_stride / sizeof(*rp);

	/* Recompute stamp? */
	if (attr != stamp_attr)
		rasops4_makestamp(ri, attr);

	if (uc == ' ') {
		u_int16_t c = stamp[0];
		while (height--) {
			rp[0] = c;
			rp[1] = c;
			rp[2] = c;
			rp += rs;
		}
	} else {
		uc -= ri->ri_font->firstchar;
		fr = (u_char *)ri->ri_font->data + uc * ri->ri_fontscale;
		fs = ri->ri_font->stride;

		while (height--) {
			rp[0] = stamp[(fr[0] >> 4) & 0xf];
			rp[1] = stamp[fr[0] & 0xf];
			rp[2] = stamp[(fr[1] >> 4) & 0xf];
			fr += fs;
			rp += rs;
		}
	}

	/* Do underline */
	if ((attr & WSATTR_UNDERLINE) != 0) {
		rp -= (rs << 1);
		rp[0] = stamp[15];
		rp[1] = stamp[15];
		rp[2] = stamp[15];
	}

	stamp_mutex--;

	return 0;
}

/*
 * Put a single character. This is for 16-pixel wide fonts.
 */
int
rasops4_putchar16(void *cookie, int row, int col, u_int uc, uint32_t attr)
{
	struct rasops_info *ri;
	int height, fs, rs;
	u_char *fr;
	u_int16_t *rp;

	/* Can't risk remaking the stamp if it's already in use */
	if (stamp_mutex++) {
		stamp_mutex--;
		return rasops4_putchar(cookie, row, col, uc, attr);
	}

	ri = (struct rasops_info *)cookie;

#ifdef RASOPS_CLIPPING
	/* Catches 'row < 0' case too */
	if ((unsigned)row >= (unsigned)ri->ri_rows) {
		stamp_mutex--;
		return 0;
	}

	if ((unsigned)col >= (unsigned)ri->ri_cols) {
		stamp_mutex--;
		return 0;
	}
#endif

	rp = (u_int16_t *)(ri->ri_bits + row * ri->ri_yscale + col * ri->ri_xscale);
	height = ri->ri_font->fontheight;
	rs = ri->ri_stride / sizeof(*rp);

	/* Recompute stamp? */
	if (attr != stamp_attr)
		rasops4_makestamp(ri, attr);

	if (uc == ' ') {
		u_int16_t c = stamp[0];
		while (height--) {
			rp[0] = c;
			rp[1] = c;
			rp[2] = c;
			rp[3] = c;
			rp += rs;
		}
	} else {
		uc -= ri->ri_font->firstchar;
		fr = (u_char *)ri->ri_font->data + uc * ri->ri_fontscale;
		fs = ri->ri_font->stride;

		while (height--) {
			rp[0] = stamp[(fr[0] >> 4) & 0xf];
			rp[1] = stamp[fr[0] & 0xf];
			rp[2] = stamp[(fr[1] >> 4) & 0xf];
			rp[3] = stamp[fr[1] & 0xf];
			fr += fs;
			rp += rs;
		}
	}

	/* Do underline */
	if ((attr & WSATTR_UNDERLINE) != 0) {
		rp -= (rs << 1);
		rp[0] = stamp[15];
		rp[1] = stamp[15];
		rp[2] = stamp[15];
		rp[3] = stamp[15];
	}

	stamp_mutex--;

	return 0;
}
#endif	/* !RASOPS_SMALL */

/*
 * Grab routines common to depths where (bpp < 8)
 */
#define NAME(ident)	rasops4_##ident
#define PIXEL_SHIFT	2

#include <dev/rasops/rasops_bitops.h>

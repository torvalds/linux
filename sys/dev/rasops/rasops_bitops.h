/*	$OpenBSD: rasops_bitops.h,v 1.9 2020/07/20 12:40:45 fcambus Exp $ */
/* 	$NetBSD: rasops_bitops.h,v 1.6 2000/04/12 14:22:30 pk Exp $	*/

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

#ifndef _RASOPS_BITOPS_H_
#define _RASOPS_BITOPS_H_ 1

/*
 * Erase columns.
 */
int
NAME(erasecols)(void *cookie, int row, int col, int num, uint32_t attr)
{
	int lmask, rmask, lclr, rclr, clr;
	struct rasops_info *ri;
	int32_t *dp, *rp;
	int height, cnt;

	ri = (struct rasops_info *)cookie;

#ifdef RASOPS_CLIPPING
	if ((unsigned)row >= (unsigned)ri->ri_rows)
		return 0;

	if (col < 0) {
		num += col;
		col = 0;
	}

	if ((col + num) > ri->ri_cols)
		num = ri->ri_cols - col;

	if (num <= 0)
		return 0;
#endif
	col *= ri->ri_font->fontwidth << PIXEL_SHIFT;
	num *= ri->ri_font->fontwidth << PIXEL_SHIFT;
	height = ri->ri_font->fontheight;
	clr = ri->ri_devcmap[(attr >> 16) & 0xf];
	rp = (int32_t *)(ri->ri_bits + row*ri->ri_yscale + ((col >> 3) & ~3));

	if ((col & 31) + num <= 32) {
		lmask = ~rasops_pmask[col & 31][num];
		lclr = clr & ~lmask;

		while (height--) {
			dp = rp;
			DELTA(rp, ri->ri_stride, int32_t *);

			*dp = (*dp & lmask) | lclr;
		}
	} else {
		lmask = rasops_rmask[col & 31];
		rmask = rasops_lmask[(col + num) & 31];

		if (lmask)
			num = (num - (32 - (col & 31))) >> 5;
		else
			num = num >> 5;

		lclr = clr & ~lmask;
		rclr = clr & ~rmask;

		while (height--) {
			dp = rp;
			DELTA(rp, ri->ri_stride, int32_t *);

			if (lmask) {
				*dp = (*dp & lmask) | lclr;
				dp++;
			}

			for (cnt = num; cnt > 0; cnt--)
				*dp++ = clr;

			if (rmask)
				*dp = (*dp & rmask) | rclr;
		}
	}

	return 0;
}

/*
 * Actually paint the cursor.
 */
int
NAME(do_cursor)(struct rasops_info *ri)
{
	int lmask, rmask, height, row, col, num;
	int32_t *dp, *rp;

	row = ri->ri_crow;
	col = ri->ri_ccol * ri->ri_font->fontwidth << PIXEL_SHIFT;
	height = ri->ri_font->fontheight;
	num = ri->ri_font->fontwidth << PIXEL_SHIFT;
	rp = (int32_t *)(ri->ri_bits + row * ri->ri_yscale + ((col >> 3) & ~3));

	if ((col & 31) + num <= 32) {
		lmask = rasops_pmask[col & 31][num];

		while (height--) {
			dp = rp;
			DELTA(rp, ri->ri_stride, int32_t *);
			*dp ^= lmask;
		}
	} else {
		lmask = ~rasops_rmask[col & 31];
		rmask = ~rasops_lmask[(col + num) & 31];

		while (height--) {
			dp = rp;
			DELTA(rp, ri->ri_stride, int32_t *);

			if (lmask != -1)
				*dp++ ^= lmask;

			if (rmask != -1)
				*dp ^= rmask;
		}
	}

	return 0;
}

/*
 * Copy columns. Ick!
 */
int
NAME(copycols)(void *cookie, int row, int src, int dst, int num)
{
	int tmp, lmask, rmask, height, lnum, rnum, sb, db, cnt, full;
	int32_t *sp, *dp, *srp, *drp;
	struct rasops_info *ri;

	ri = (struct rasops_info *)cookie;

#ifdef RASOPS_CLIPPING
	if (dst == src)
		return 0;

	/* Catches < 0 case too */
	if ((unsigned)row >= (unsigned)ri->ri_rows)
		return 0;

	if (src < 0) {
		num += src;
		src = 0;
	}

	if ((src + num) > ri->ri_cols)
		num = ri->ri_cols - src;

	if (dst < 0) {
		num += dst;
		dst = 0;
	}

	if ((dst + num) > ri->ri_cols)
		num = ri->ri_cols - dst;

	if (num <= 0)
		return 0;
#endif

	cnt = ri->ri_font->fontwidth << PIXEL_SHIFT;
	src *= cnt;
	dst *= cnt;
	num *= cnt;
	row *= ri->ri_yscale;
	height = ri->ri_font->fontheight;
	db = dst & 31;

	if (db + num <= 32) {
		/* Destination is contained within a single word */
		srp = (int32_t *)(ri->ri_bits + row + ((src >> 3) & ~3));
		drp = (int32_t *)(ri->ri_bits + row + ((dst >> 3) & ~3));
		sb = src & 31;

		while (height--) {
			GETBITS(srp, sb, num, tmp);
			PUTBITS(tmp, db, num, drp);
			DELTA(srp, ri->ri_stride, int32_t *);
			DELTA(drp, ri->ri_stride, int32_t *);
		}

		return 0;
	}

	lmask = rasops_rmask[db];
	rmask = rasops_lmask[(dst + num) & 31];
	lnum = (32 - db) & 31;
	rnum = (dst + num) & 31;

	if (lmask)
		full = (num - (32 - (dst & 31))) >> 5;
	else
		full = num >> 5;

	if (src < dst && src + num > dst) {
		/* Copy right-to-left */
		sb = src & 31;
		src = src + num;
		dst = dst + num;
		srp = (int32_t *)(ri->ri_bits + row + ((src >> 3) & ~3));
		drp = (int32_t *)(ri->ri_bits + row + ((dst >> 3) & ~3));

		src = src & 31;
		rnum = 32 - lnum;
		db = dst & 31;

		if ((src -= db) < 0)
			src += 32;

		while (height--) {
			sp = srp;
			dp = drp;
			DELTA(srp, ri->ri_stride, int32_t *);
			DELTA(drp, ri->ri_stride, int32_t *);

			if (db) {
				GETBITS(sp, src, db, tmp);
				PUTBITS(tmp, 0, db, dp);
				dp--;
				sp--;
			}

			/* Now aligned to 32-bits wrt dp */
			for (cnt = full; cnt; cnt--, sp--) {
				GETBITS(sp, src, 32, tmp);
				*dp-- = tmp;
			}

			if (lmask) {
#if 0
				if (src > sb)
					sp++;
#endif
				GETBITS(sp, sb, lnum, tmp);
				PUTBITS(tmp, rnum, lnum, dp);
			}
		}
	} else {
		/* Copy left-to-right */
		srp = (int32_t *)(ri->ri_bits + row + ((src >> 3) & ~3));
		drp = (int32_t *)(ri->ri_bits + row + ((dst >> 3) & ~3));
		db = dst & 31;

		while (height--) {
			sb = src & 31;
			sp = srp;
			dp = drp;
			DELTA(srp, ri->ri_stride, int32_t *);
			DELTA(drp, ri->ri_stride, int32_t *);

			if (lmask) {
				GETBITS(sp, sb, lnum, tmp);
				PUTBITS(tmp, db, lnum, dp);
				dp++;

				if ((sb += lnum) > 31) {
					sp++;
					sb -= 32;
				}
			}

			/* Now aligned to 32-bits wrt dp */
			for (cnt = full; cnt; cnt--, sp++) {
				GETBITS(sp, sb, 32, tmp);
				*dp++ = tmp;
			}

			if (rmask) {
				GETBITS(sp, sb, rnum, tmp);
				PUTBITS(tmp, 0, rnum, dp);
			}
		}
	}

	return 0;
}

#endif /* _RASOPS_BITOPS_H_ */

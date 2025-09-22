/* $OpenBSD: omrasops.c,v 1.18 2022/11/06 13:01:22 aoyama Exp $ */
/* $NetBSD: omrasops.c,v 1.1 2000/01/05 08:48:56 nisimura Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Tohru Nishimura.
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

/*
 * Designed specifically for 'm68k bitorder';
 *	- most significant byte is stored at lower address,
 *	- most significant bit is displayed at left most on screen.
 * Implementation relies on;
 *	- every memory reference is done in aligned 32bit chunks,
 *	- font glyphs are stored in 32bit padded.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

#include <luna88k/dev/omrasops.h>

#include <machine/board.h>
#define	OMFB_PLANEMASK	BMAP_BMSEL	/* planemask register */
#define	OMFB_ROPFUNC	BMAP_FN		/* ROP function code */

/* wscons emulator operations */
int	om_copycols(void *, int, int, int, int);
int	om_copyrows(void *, int, int, int num);
int	om_erasecols(void *, int, int, int, uint32_t);
int	om_eraserows(void *, int, int, uint32_t);
int	om1_cursor(void *, int, int, int);
int	om1_putchar(void *, int, int, u_int, uint32_t);
int	om4_cursor(void *, int, int, int);
int	om4_putchar(void *, int, int, u_int, uint32_t);

/* depth-depended setup functions */
void	setup_omrasops1(struct rasops_info *);
void	setup_omrasops4(struct rasops_info *);

/* internal functions for 1bpp/4bpp */
int	om1_windowmove(struct rasops_info *, u_int16_t, u_int16_t, u_int16_t,
		u_int16_t, u_int16_t, u_int16_t, int16_t, int16_t);
int	om4_windowmove(struct rasops_info *, u_int16_t, u_int16_t, u_int16_t,
		u_int16_t, u_int16_t, u_int16_t, int16_t, int16_t);

/* MI function in src/sys/dev/rasops/rasops.c */
int     rasops_pack_cattr(void *, int, int, int, uint32_t *);
int     rasops_pack_mattr(void *, int, int, int, uint32_t *);

static int (*om_windowmove)(struct rasops_info *, u_int16_t, u_int16_t,
		u_int16_t, u_int16_t, u_int16_t, u_int16_t, int16_t, int16_t);

extern struct wsscreen_descr omfb_stdscreen;

#define	ALL1BITS	(~0U)
#define	ALL0BITS	(0U)
#define	BLITWIDTH	(32)
#define	ALIGNMASK	(0x1f)
#define	BYTESDONE	(4)

/*
 * Blit a character at the specified co-ordinates.
 * - 1bpp version -
 */
int
om1_putchar(void *cookie, int row, int startcol, u_int uc, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	u_int8_t *p;
	int scanspan, startx, height, width, align, y;
	u_int32_t lmask, rmask, glyph, inverse;
	int i, fg, bg;
	u_int8_t *fb;

	scanspan = ri->ri_stride;
	y = ri->ri_font->fontheight * row;
	startx = ri->ri_font->fontwidth * startcol;
	height = ri->ri_font->fontheight;
	fb = (u_int8_t *)ri->ri_font->data +
	    (uc - ri->ri_font->firstchar) * ri->ri_fontscale;
	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);
	inverse = (bg != 0) ? ALL1BITS : ALL0BITS;

	p = (u_int8_t *)ri->ri_bits + y * scanspan + ((startx / 32) * 4);
	align = startx & ALIGNMASK;
	width = ri->ri_font->fontwidth + align;
	lmask = ALL1BITS >> align;
	rmask = ALL1BITS << (-width & ALIGNMASK);
	if (width <= BLITWIDTH) {
		lmask &= rmask;
		while (height > 0) {
			glyph = 0;
			for (i = ri->ri_font->stride; i != 0; i--)
				glyph = (glyph << 8) | *fb++;
			glyph <<= (4 - ri->ri_font->stride) * NBBY;
			glyph = (glyph >> align) ^ inverse;
			*P0(p) = (*P0(p) & ~lmask) | (glyph & lmask);
			p += scanspan;
			height--;
		}
	} else {
		u_int8_t *q = p;
		u_int32_t lhalf, rhalf;

		while (height > 0) {
			glyph = 0;
			for (i = ri->ri_font->stride; i != 0; i--)
				glyph = (glyph << 8) | *fb++;
			glyph <<= (4 - ri->ri_font->stride) * NBBY;
			lhalf = (glyph >> align) ^ inverse;
			*P0(p) = (*P0(p) & ~lmask) | (lhalf & lmask);

			p += BYTESDONE;

			rhalf = (glyph << (BLITWIDTH - align)) ^ inverse;
			*P0(p) = (rhalf & rmask) | (*P0(p) & ~rmask);

			p = (q += scanspan);
			height--;
		}
	}

	return 0;
}

/*
 * Blit a character at the specified co-ordinates
 * - 4bpp version -
 */
int
om4_putchar(void *cookie, int row, int startcol, u_int uc, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	u_int8_t *p;
	int scanspan, startx, height, width, align, y;
	u_int32_t lmask, rmask, glyph, glyphbg, fgpat, bgpat;
	u_int32_t fgmask0, fgmask1, fgmask2, fgmask3;
	u_int32_t bgmask0, bgmask1, bgmask2, bgmask3;
	int i, fg, bg;
	u_int8_t *fb;

	scanspan = ri->ri_stride;
	y = ri->ri_font->fontheight * row;
	startx = ri->ri_font->fontwidth * startcol;
	height = ri->ri_font->fontheight;
	fb = (u_int8_t *)ri->ri_font->data +
	    (uc - ri->ri_font->firstchar) * ri->ri_fontscale;
	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);
	fgmask0 = (fg & 0x01) ? ALL1BITS : ALL0BITS;
	fgmask1 = (fg & 0x02) ? ALL1BITS : ALL0BITS;
	fgmask2 = (fg & 0x04) ? ALL1BITS : ALL0BITS;
	fgmask3 = (fg & 0x08) ? ALL1BITS : ALL0BITS;
	bgmask0 = (bg & 0x01) ? ALL1BITS : ALL0BITS;
	bgmask1 = (bg & 0x02) ? ALL1BITS : ALL0BITS;
	bgmask2 = (bg & 0x04) ? ALL1BITS : ALL0BITS;
	bgmask3 = (bg & 0x08) ? ALL1BITS : ALL0BITS;

	p = (u_int8_t *)ri->ri_bits + y * scanspan + ((startx / 32) * 4);
	align = startx & ALIGNMASK;
	width = ri->ri_font->fontwidth + align;
	lmask = ALL1BITS >> align;
	rmask = ALL1BITS << (-width & ALIGNMASK);

	/* select all planes for later ROP function target */ 
	*(volatile u_int32_t *)OMFB_PLANEMASK = 0xff;

	if (width <= BLITWIDTH) {
		lmask &= rmask;
		/* set lmask as ROP mask value, with THROUGH mode */
		((volatile u_int32_t *)OMFB_ROPFUNC)[ROP_THROUGH] = lmask;

		while (height > 0) {
			glyph = 0;
			for (i = ri->ri_font->stride; i != 0; i--)
				glyph = (glyph << 8) | *fb++;
			glyph <<= (4 - ri->ri_font->stride) * NBBY;
			glyph = (glyph >> align);
			glyphbg = glyph ^ ALL1BITS;

			fgpat = glyph & fgmask0;
			bgpat = glyphbg & bgmask0;
			*P0(p) = (fgpat | bgpat);
			fgpat = glyph & fgmask1;
			bgpat = glyphbg & bgmask1;
			*P1(p) = (fgpat | bgpat);
			fgpat = glyph & fgmask2;
			bgpat = glyphbg & bgmask2;
			*P2(p) = (fgpat | bgpat);
			fgpat = glyph & fgmask3;
			bgpat = glyphbg & bgmask3;
			*P3(p) = (fgpat | bgpat);

			p += scanspan;
			height--;
		}
		/* reset mask value */
		((volatile u_int32_t *)OMFB_ROPFUNC)[ROP_THROUGH] = ALL1BITS;
	} else {
		u_int8_t *q = p;
		u_int32_t lhalf, rhalf;
		u_int32_t lhalfbg, rhalfbg;

		while (height > 0) {
			glyph = 0;
			for (i = ri->ri_font->stride; i != 0; i--)
				glyph = (glyph << 8) | *fb++;
			glyph <<= (4 - ri->ri_font->stride) * NBBY;
			lhalf = (glyph >> align);
			lhalfbg = lhalf ^ ALL1BITS;
			/* set lmask as ROP mask value, with THROUGH mode */
			((volatile u_int32_t *)OMFB_ROPFUNC)[ROP_THROUGH]
				= lmask;

			fgpat = lhalf & fgmask0;
			bgpat = lhalfbg & bgmask0;
			*P0(p) = (fgpat | bgpat);
			fgpat = lhalf & fgmask1;
			bgpat = lhalfbg & bgmask1;
			*P1(p) = (fgpat | bgpat);
			fgpat = lhalf & fgmask2;
			bgpat = lhalfbg & bgmask2;
			*P2(p) = (fgpat | bgpat);
			fgpat = lhalf & fgmask3;
			bgpat = lhalfbg & bgmask3;
			*P3(p) = (fgpat | bgpat);

			p += BYTESDONE;

			rhalf = (glyph << (BLITWIDTH - align));
			rhalfbg = rhalf ^ ALL1BITS;
			/* set rmask as ROP mask value, with THROUGH mode */
			((volatile u_int32_t *)OMFB_ROPFUNC)[ROP_THROUGH]
				= rmask;

			fgpat = rhalf & fgmask0;
			bgpat = rhalfbg & bgmask0;
			*P0(p) = (fgpat | bgpat);
			fgpat = rhalf & fgmask1;
			bgpat = rhalfbg & bgmask1;
			*P1(p) = (fgpat | bgpat);
			fgpat = rhalf & fgmask2;
			bgpat = rhalfbg & bgmask2;
			*P2(p) = (fgpat | bgpat);
			fgpat = rhalf & fgmask3;
			bgpat = rhalfbg & bgmask3;
			*P3(p) = (fgpat | bgpat);

			p = (q += scanspan);
			height--;
		}
		/* reset mask value */
		((volatile u_int32_t *)OMFB_ROPFUNC)[ROP_THROUGH] = ALL1BITS;
	}
	/* select plane #0 only; XXX need this ? */
	*(volatile u_int32_t *)OMFB_PLANEMASK = 0x01;

	return 0;
}

int
om_erasecols(void *cookie, int row, int col, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	int fg, bg;
	int snum, scol, srow;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);

	snum = num * ri->ri_font->fontwidth;
	scol = col * ri->ri_font->fontwidth  + ri->ri_xorigin;
	srow = row * ri->ri_font->fontheight + ri->ri_yorigin;

	/*
	 * If this is too tricky for the simple raster ops engine,
	 * pass the fun to rasops.
	 */
	if ((*om_windowmove)(ri, scol, srow, scol, srow, snum,
	    ri->ri_font->fontheight, RR_CLEAR, 0xff ^ bg) != 0)
		rasops_erasecols(cookie, row, col, num, attr);

	return 0;
}

int
om_eraserows(void *cookie, int row, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	int fg, bg;
	int srow, snum;
	int rc;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);
	bg ^= 0xff;

	if (num == ri->ri_rows && (ri->ri_flg & RI_FULLCLEAR)) {
		rc = (*om_windowmove)(ri, 0, 0, 0, 0, ri->ri_width,
			ri->ri_height, RR_CLEAR, bg);
	} else {
		srow = row * ri->ri_font->fontheight + ri->ri_yorigin;
		snum = num * ri->ri_font->fontheight;
		rc = (*om_windowmove)(ri, ri->ri_xorigin, srow, ri->ri_xorigin,
		    srow, ri->ri_emuwidth, snum, RR_CLEAR, bg);
	}
	if (rc != 0)
		rasops_eraserows(cookie, row, num, attr);

	return 0;
}

int
om_copyrows(void *cookie, int src, int dst, int n)
{
	struct rasops_info *ri = cookie;

	n   *= ri->ri_font->fontheight;
	src *= ri->ri_font->fontheight;
	dst *= ri->ri_font->fontheight;

	(*om_windowmove)(ri, ri->ri_xorigin, ri->ri_yorigin + src,
		ri->ri_xorigin, ri->ri_yorigin + dst,
		ri->ri_emuwidth, n, RR_COPY, 0xff);

	return 0;
}

int
om_copycols(void *cookie, int row, int src, int dst, int n)
{
	struct rasops_info *ri = cookie;

	n   *= ri->ri_font->fontwidth;
	src *= ri->ri_font->fontwidth;
	dst *= ri->ri_font->fontwidth;
	row *= ri->ri_font->fontheight;

	(*om_windowmove)(ri, ri->ri_xorigin + src, ri->ri_yorigin + row,
		ri->ri_xorigin + dst, ri->ri_yorigin + row,
		n, ri->ri_font->fontheight, RR_COPY, 0xff);

	return 0;
}

/*
 * Position|{enable|disable} the cursor at the specified location.
 * - 1bpp version -
 */
int
om1_cursor(void *cookie, int on, int row, int col)
{
	struct rasops_info *ri = cookie;
	u_int8_t *p;
	int scanspan, startx, height, width, align, y;
	u_int32_t lmask, rmask, image;

	if (!on) {
		/* make sure it's on */
		if ((ri->ri_flg & RI_CURSOR) == 0)
			return 0;

		row = ri->ri_crow;
		col = ri->ri_ccol;
	} else {
		/* unpaint the old copy. */
		ri->ri_crow = row;
		ri->ri_ccol = col;
	}

	scanspan = ri->ri_stride;
	y = ri->ri_font->fontheight * row;
	startx = ri->ri_font->fontwidth * col;
	height = ri->ri_font->fontheight;

	p = (u_int8_t *)ri->ri_bits + y * scanspan + ((startx / 32) * 4);
	align = startx & ALIGNMASK;
	width = ri->ri_font->fontwidth + align;
	lmask = ALL1BITS >> align;
	rmask = ALL1BITS << (-width & ALIGNMASK);
	if (width <= BLITWIDTH) {
		lmask &= rmask;
		while (height > 0) {
			image = *P0(p);
			*P0(p) = (image & ~lmask) | ((image ^ ALL1BITS) & lmask);
			p += scanspan;
			height--;
		}
	} else {
		u_int8_t *q = p;

		while (height > 0) {
			image = *P0(p);
			*P0(p) = (image & ~lmask) | ((image ^ ALL1BITS) & lmask);
			p += BYTESDONE;

			image = *P0(p);
			*P0(p) = ((image ^ ALL1BITS) & rmask) | (image & ~rmask);
			p = (q += scanspan);
			height--;
		}
	}
	ri->ri_flg ^= RI_CURSOR;

	return 0;
}

/*
 * Position|{enable|disable} the cursor at the specified location
 * - 4bpp version -
 */
int
om4_cursor(void *cookie, int on, int row, int col)
{
	struct rasops_info *ri = cookie;
	u_int8_t *p;
	int scanspan, startx, height, width, align, y;
	u_int32_t lmask, rmask;

	if (!on) {
		/* make sure it's on */
		if ((ri->ri_flg & RI_CURSOR) == 0)
			return 0;

		row = ri->ri_crow;
		col = ri->ri_ccol;
	} else {
		/* unpaint the old copy. */
		ri->ri_crow = row;
		ri->ri_ccol = col;
	}

	scanspan = ri->ri_stride;
	y = ri->ri_font->fontheight * row;
	startx = ri->ri_font->fontwidth * col;
	height = ri->ri_font->fontheight;

	p = (u_int8_t *)ri->ri_bits + y * scanspan + ((startx / 32) * 4);
	align = startx & ALIGNMASK;
	width = ri->ri_font->fontwidth + align;
	lmask = ALL1BITS >> align;
	rmask = ALL1BITS << (-width & ALIGNMASK);

	/* select all planes for later ROP function target */ 
	*(volatile u_int32_t *)OMFB_PLANEMASK = 0xff;

	if (width <= BLITWIDTH) {
		lmask &= rmask;
		/* set lmask as ROP mask value, with INV2 mode */
		((volatile u_int32_t *)OMFB_ROPFUNC)[ROP_INV2] = lmask;

		while (height > 0) {
			*W(p) = ALL1BITS;
			p += scanspan;
			height--;
		}
		/* reset mask value */
		((volatile u_int32_t *)OMFB_ROPFUNC)[ROP_THROUGH] = ALL1BITS;
	} else {
		u_int8_t *q = p;

		while (height > 0) {
			/* set lmask as ROP mask value, with INV2 mode */
			((volatile u_int32_t *)OMFB_ROPFUNC)[ROP_INV2] = lmask;
			*W(p) = ALL1BITS;

			p += BYTESDONE;

			/* set rmask as ROP mask value, with INV2 mode */
			((volatile u_int32_t *)OMFB_ROPFUNC)[ROP_INV2] = rmask;
			*W(p) = ALL1BITS;

			p = (q += scanspan);
			height--;
		}
		/* reset mask value */
		((volatile u_int32_t *)OMFB_ROPFUNC)[ROP_THROUGH] = ALL1BITS;
	}
	/* select plane #0 only; XXX need this ? */
	*(volatile u_int32_t *)OMFB_PLANEMASK = 0x01;

	ri->ri_flg ^= RI_CURSOR;

	return 0;
}

/*
 * After calling rasops_init(), set up our depth-depend emulops,
 * block move function and capabilities.
 */
void
setup_omrasops1(struct rasops_info *ri)
{
	om_windowmove = om1_windowmove;
	ri->ri_ops.cursor  = om1_cursor;
	ri->ri_ops.putchar = om1_putchar;
	omfb_stdscreen.capabilities
		= ri->ri_caps & ~WSSCREEN_UNDERLINE;
	ri->ri_ops.pack_attr = rasops_pack_mattr;
}

void
setup_omrasops4(struct rasops_info *ri)
{
	om_windowmove = om4_windowmove;
	ri->ri_ops.cursor  = om4_cursor;
	ri->ri_ops.putchar = om4_putchar;
	omfb_stdscreen.capabilities
		= WSSCREEN_HILIT | WSSCREEN_WSCOLORS | WSSCREEN_REVERSE;
	/*
	 * Since we set ri->ri_depth == 1, rasops_init() set
	 * rasops_pack_mattr for us.  But we use the color version,
	 * rasops_pack_cattr, on 4bpp/8bpp frame buffer.
	 */
	ri->ri_ops.pack_attr = rasops_pack_cattr;
}

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sascha Wildner <saw@online.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_syscons.h"
#include "opt_vga.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/fbio.h>
#include <sys/consio.h>
#include <sys/sysctl.h>

#include <machine/bus.h>

#include <dev/fb/fbreg.h>
#include <dev/fb/vgareg.h>
#include <dev/syscons/syscons.h>

#include <isa/isareg.h>

#ifndef SC_RENDER_DEBUG
#define SC_RENDER_DEBUG		0
#endif

static vr_clear_t		vga_txtclear;
static vr_draw_border_t		vga_txtborder;
static vr_draw_t		vga_txtdraw;
static vr_set_cursor_t		vga_txtcursor_shape;
static vr_draw_cursor_t		vga_txtcursor;
static vr_blink_cursor_t	vga_txtblink;
#ifndef SC_NO_CUTPASTE
static vr_draw_mouse_t		vga_txtmouse;
#else
#define vga_txtmouse		(vr_draw_mouse_t *)vga_nop
#endif

#ifdef SC_PIXEL_MODE
static vr_init_t		vga_rndrinit;
static vr_clear_t		vga_pxlclear_direct;
static vr_clear_t		vga_pxlclear_planar;
static vr_draw_border_t		vga_pxlborder_direct;
static vr_draw_border_t		vga_pxlborder_planar;
static vr_draw_t		vga_vgadraw_direct;
static vr_draw_t		vga_vgadraw_planar;
static vr_set_cursor_t		vga_pxlcursor_shape;
static vr_draw_cursor_t		vga_pxlcursor_direct;
static vr_draw_cursor_t		vga_pxlcursor_planar;
static vr_blink_cursor_t	vga_pxlblink_direct;
static vr_blink_cursor_t	vga_pxlblink_planar;
#ifndef SC_NO_CUTPASTE
static vr_draw_mouse_t		vga_pxlmouse_direct;
static vr_draw_mouse_t		vga_pxlmouse_planar;
#else
#define vga_pxlmouse_direct	(vr_draw_mouse_t *)vga_nop
#define vga_pxlmouse_planar	(vr_draw_mouse_t *)vga_nop
#endif
#endif /* SC_PIXEL_MODE */

#ifndef SC_NO_MODE_CHANGE
static vr_draw_border_t		vga_grborder;
#endif

static void			vga_nop(scr_stat *scp);

static sc_rndr_sw_t txtrndrsw = {
	(vr_init_t *)vga_nop,
	vga_txtclear,
	vga_txtborder,
	vga_txtdraw,	
	vga_txtcursor_shape,
	vga_txtcursor,
	vga_txtblink,
	(vr_set_mouse_t *)vga_nop,
	vga_txtmouse,
};
RENDERER(mda, 0, txtrndrsw, vga_set);
RENDERER(cga, 0, txtrndrsw, vga_set);
RENDERER(ega, 0, txtrndrsw, vga_set);
RENDERER(vga, 0, txtrndrsw, vga_set);

#ifdef SC_PIXEL_MODE
static sc_rndr_sw_t vgarndrsw = {
	vga_rndrinit,
	(vr_clear_t *)vga_nop,
	(vr_draw_border_t *)vga_nop,
	(vr_draw_t *)vga_nop,
	vga_pxlcursor_shape,
	(vr_draw_cursor_t *)vga_nop,
	(vr_blink_cursor_t *)vga_nop,
	(vr_set_mouse_t *)vga_nop,
	(vr_draw_mouse_t *)vga_nop,
};
RENDERER(ega, PIXEL_MODE, vgarndrsw, vga_set);
RENDERER(vga, PIXEL_MODE, vgarndrsw, vga_set);
#endif /* SC_PIXEL_MODE */

#ifndef SC_NO_MODE_CHANGE
static sc_rndr_sw_t grrndrsw = {
	(vr_init_t *)vga_nop,
	(vr_clear_t *)vga_nop,
	vga_grborder,
	(vr_draw_t *)vga_nop,
	(vr_set_cursor_t *)vga_nop,
	(vr_draw_cursor_t *)vga_nop,
	(vr_blink_cursor_t *)vga_nop,
	(vr_set_mouse_t *)vga_nop,
	(vr_draw_mouse_t *)vga_nop,
};
RENDERER(cga, GRAPHICS_MODE, grrndrsw, vga_set);
RENDERER(ega, GRAPHICS_MODE, grrndrsw, vga_set);
RENDERER(vga, GRAPHICS_MODE, grrndrsw, vga_set);
#endif /* SC_NO_MODE_CHANGE */

RENDERER_MODULE(vga, vga_set);

#ifndef SC_NO_CUTPASTE
#if !defined(SC_ALT_MOUSE_IMAGE) || defined(SC_PIXEL_MODE)
struct mousedata {
	u_short	md_border[16];
	u_short	md_interior[16];
	u_char	md_width;
	u_char	md_height;
	u_char	md_baspect;
	u_char	md_iaspect;
	const char *md_name;
};

static const struct mousedata mouse10x16_50 = { {
	0xC000, 0xA000, 0x9000, 0x8800, 0x8400, 0x8200, 0x8100, 0x8200,
	0x8400, 0x8400, 0x8400, 0x9200, 0xB200, 0xA900, 0xC900, 0x8600, }, {
	0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7C00,
	0x7800, 0x7800, 0x7800, 0x6C00, 0x4C00, 0x4600, 0x0600, 0x0000, },
	10, 16, 49, 52, "mouse10x16_50",
};

static const struct mousedata mouse8x14_67 = { {
	0xC000, 0xA000, 0x9000, 0x8800, 0x8400, 0x8200, 0x8100, 0x8700,
	0x8400, 0x9200, 0xB200, 0xA900, 0xC900, 0x0600, 0x0000, 0x0000, }, {
	0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7800,
	0x7800, 0x6C00, 0x4C00, 0x4600, 0x0600, 0x0000, 0x0000, 0x0000, },
	8, 14, 64, 65, "mouse8x14_67",
};

static const struct mousedata mouse8x13_75 = { {
	0xC000, 0xA000, 0x9000, 0x8800, 0x8400, 0x8200, 0x8600, 0x8400,
	0xB200, 0xD200, 0x0900, 0x0900, 0x0600, 0x0000, 0x0000, 0x0000, }, {
	0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7800, 0x7800,
	0x4C00, 0x0C00, 0x0600, 0x0600, 0x0000, 0x0000, 0x0000, 0x0000, },
	8, 13, 75, 80, "mouse8x13_75",
};

static const struct mousedata mouse10x16_75 = { {
	0xC000, 0xA000, 0x9000, 0x8800, 0x8400, 0x8200, 0x8100, 0x8700,
	0x8400, 0x9200, 0xB200, 0xC900, 0x0900, 0x0480, 0x0480, 0x0300, }, {
	0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7800,
	0x7800, 0x6C00, 0x4C00, 0x0600, 0x0600, 0x0300, 0x0300, 0x0000, },
	10, 16, 72, 75, "mouse10x16_75",
};

static const struct mousedata mouse9x13_90 = { {
	0xC000, 0xA000, 0x9000, 0x8800, 0x8400, 0x8200, 0x8100, 0x8780,
	0x9200, 0xB200, 0xD900, 0x8900, 0x0600, 0x0000, 0x0000, 0x0000, }, {
	0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7800,
	0x6C00, 0x4C00, 0x0600, 0x0600, 0x0000, 0x0000, 0x0000, 0x0000, },
	9, 13, 89, 89, "mouse9x13_90",
};

static const struct mousedata mouse10x16_90 = { {
	0xC000, 0xA000, 0x9000, 0x8800, 0x8400, 0x8200, 0x8100, 0x8080,
	0x8040, 0x83E0, 0x8200, 0x9900, 0xA900, 0xC480, 0x8480, 0x0300, }, {
	0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00,
	0x7F80, 0x7C00, 0x7C00, 0x6600, 0x4600, 0x0300, 0x0300, 0x0000, },
	10, 16, 89, 89, "mouse10x16_90",
};

static const struct mousedata mouse9x13_100 = { {
	0xC000, 0xA000, 0x9000, 0x8800, 0x8400, 0x8200, 0x8100, 0x8780,
	0xB200, 0xD200, 0x8900, 0x0900, 0x0600, 0x0000, 0x0000, 0x0000, }, {
	0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7800,
	0x4C00, 0x0C00, 0x0600, 0x0600, 0x0000, 0x0000, 0x0000, 0x0000, },
	9, 13, 106, 113, "mouse9x13_100",
};

static const struct mousedata mouse10x16_100 = { {
	0xC000, 0xA000, 0x9000, 0x8800, 0x8400, 0x8200, 0x8100, 0x8080,
	0x8040, 0x83C0, 0x9200, 0xA900, 0xC900, 0x0480, 0x0480, 0x0300, }, {
	0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00,
	0x7F80, 0x7C00, 0x6C00, 0x4600, 0x0600, 0x0300, 0x0300, 0x0000, },
	10, 16, 96, 106, "mouse10x16_100",
};

static const struct mousedata mouse10x14_120 = { {
	0xC000, 0xA000, 0x9000, 0x8800, 0x8400, 0x8200, 0x8100, 0x8080,
	0x97C0, 0xB200, 0xF200, 0xC900, 0x8900, 0x0600, 0x0000, 0x0000, }, {
	0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00,
	0x6800, 0x4C00, 0x0C00, 0x0600, 0x0600, 0x0000, 0x0000, 0x0000, },
	10, 14, 120, 124, "mouse10x14_120",
};

static const struct mousedata mouse10x16_120 = { {
	0xC000, 0xA000, 0x9000, 0x8800, 0x8400, 0x8200, 0x8100, 0x8080,
	0x97C0, 0xB200, 0xF200, 0xC900, 0x8900, 0x0480, 0x0480, 0x0300, }, {
	0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00,
	0x6800, 0x4C00, 0x0C00, 0x0600, 0x0600, 0x0300, 0x0300, 0x0000, },
	10, 16, 120, 124, "mouse10x16_120",
};

static const struct mousedata mouse9x13_133 = { {
	0xC000, 0xA000, 0x9000, 0x8800, 0x8400, 0x8200, 0x8100, 0x8080,
	0x9780, 0xB200, 0xC900, 0x0900, 0x0600, 0x0000, 0x0000, 0x0000, }, {
	0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00,
	0x6800, 0x4C00, 0x0600, 0x0600, 0x0000, 0x0000, 0x0000, 0x0000, },
	9, 13, 142, 124, "mouse9x13_133",
};

static const struct mousedata mouse10x16_133 = { {
	0xC000, 0xA000, 0x9000, 0x8800, 0x8400, 0x8200, 0x8100, 0x8080,
	0x8040, 0x93E0, 0xB200, 0xC900, 0x8900, 0x0480, 0x0480, 0x0300, }, {
	0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00,
	0x7F80, 0x6C00, 0x4C00, 0x0600, 0x0600, 0x0300, 0x0300, 0x0000, },
	10, 16, 120, 133, "mouse10x16_133",
};

static const struct mousedata mouse14x10_240 = { {
	0xF800, 0xCE00, 0xC380, 0xC0E0, 0xC038, 0xC1FC, 0xDCC0, 0xF660,
	0xC330, 0x01E0, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, }, {
	0x0000, 0x3000, 0x3C00, 0x3F00, 0x3FC0, 0x3E00, 0x2300, 0x0180,
	0x00C0, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, },
	14, 10, 189, 189, "mouse14x10_240",
};

static const struct mousedata * const mouselarge[] = {
	&mouse10x16_50,
	&mouse8x14_67,
	&mouse10x16_75,
	&mouse10x16_90,
	&mouse10x16_100,
	&mouse10x16_120,
	&mouse10x16_133,
	&mouse14x10_240,
};

static const struct mousedata * const mousesmall[] = {
	&mouse8x14_67,
	&mouse8x13_75,
	&mouse9x13_90,
	&mouse9x13_100,
	&mouse10x14_120,
	&mouse9x13_133,
	&mouse14x10_240,
};
#endif
#endif

#ifdef SC_PIXEL_MODE
#define	GET_PIXEL(scp, pos, x, w)					\
({									\
	(scp)->sc->adp->va_window +					\
	    (x) * (scp)->xoff +						\
	    (scp)->yoff * (scp)->font_size * (w) +			\
	    (x) * ((pos) % (scp)->xsize) +				\
	    (scp)->font_size * (w) * ((pos) / (scp)->xsize);		\
})

#define	DRAW_PIXEL(scp, pos, color) do {				\
	switch ((scp)->sc->adp->va_info.vi_depth) {			\
	case 32:							\
		writel((pos), vga_palette32[color]);			\
		break;							\
	case 24:							\
		if (((pos) & 1) == 0) {					\
			writew((pos), vga_palette32[color]);		\
			writeb((pos) + 2, vga_palette32[color] >> 16);	\
		} else {						\
			writeb((pos), vga_palette32[color]);		\
			writew((pos) + 1, vga_palette32[color] >> 8);	\
		}							\
		break;							\
	case 16:							\
		if ((scp)->sc->adp->va_info.vi_pixel_fsizes[1] == 5)	\
			writew((pos), vga_palette15[color]);		\
		else							\
			writew((pos), vga_palette16[color]);		\
		break;							\
	case 15:							\
		writew((pos), vga_palette15[color]);			\
		break;							\
	case 8:								\
		writeb((pos), (uint8_t)(color));			\
	}								\
} while (0)
	
static uint32_t vga_palette32[16] = {
	0x000000, 0x0000ad, 0x00ad00, 0x00adad,
	0xad0000, 0xad00ad, 0xad5200, 0xadadad,
	0x525252, 0x5252ff, 0x52ff52, 0x52ffff,
	0xff5252, 0xff52ff, 0xffff52, 0xffffff
};

static uint16_t vga_palette16[16] = {
	0x0000, 0x0016, 0x0560, 0x0576, 0xb000, 0xb016, 0xb2a0, 0xb576,
	0x52aa, 0x52bf, 0x57ea, 0x57ff, 0xfaaa, 0xfabf, 0xffea, 0xffff
};

static uint16_t vga_palette15[16] = {
	0x0000, 0x0016, 0x02c0, 0x02d6, 0x5800, 0x5816, 0x5940, 0x5ad6,
	0x294a, 0x295f, 0x2bea, 0x2bff, 0x7d4a, 0x7d5f, 0x7fea, 0x7fff
};
#endif

static int vga_aspect_scale= 100;
SYSCTL_INT(_machdep, OID_AUTO, vga_aspect_scale, CTLFLAG_RW,
    &vga_aspect_scale, 0, "Aspect scale ratio (3:4):actual times 100");

static u_short
vga_flipattr(u_short a, int blink)
{
	if (blink)
		a = (a & 0x8800) | ((a & 0x7000) >> 4) |
		    ((a & 0x0700) << 4);
	else
		a = ((a & 0xf000) >> 4) | ((a & 0x0f00) << 4);
	return (a);
}

static u_short
vga_cursorattr_adj(scr_stat *scp, u_short a, int blink)
{
	int i;
	u_short bg, bgmask, fg, newbg;

	/*
	 * The cursor attribute is usually that of the underlying char
	 * with only the bg changed, to the first preferred color that
	 * differs from both the fg and bg.  If there is no such color,
	 * use reverse video.
	 */
	bgmask = blink ? 0x7000 : 0xf000;
	bg = a & bgmask;
	fg = a & 0x0f00;
	for (i = 0; i < nitems(scp->curs_attr.bg); i++) {
		newbg = (scp->curs_attr.bg[i] << 12) & bgmask;
		if (newbg != bg && newbg != (fg << 4))
			break;
	}
	if (i == nitems(scp->curs_attr.bg))
		return (vga_flipattr(a, blink));
	return (fg | newbg | (blink ? a & 0x8000 : 0));
}

static void
vga_setmdp(scr_stat *scp)
{
#if !defined(SC_NO_CUTPASTE) && \
   (!defined(SC_ALT_MOUSE_IMAGE) || defined(SC_PIXEL_MODE))
	const struct mousedata *mdp;
	const struct mousedata * const *mdpp;
	int aspect, best_i, best_v, i, n, v, wb, wi, xpixel, ypixel;

	xpixel = scp->xpixel;
	ypixel = scp->ypixel;
	if (scp->sc->adp->va_flags & V_ADP_CWIDTH9)
		xpixel = xpixel * 9 / 8;

	/* If 16:9 +-1%, assume square pixels, else scale to 4:3 or full. */
	aspect = xpixel * 900 / ypixel / 16;
	if (aspect < 99 || aspect > 100)
		aspect = xpixel * 300 / ypixel / 4 * vga_aspect_scale / 100;

	/*
	 * Use 10x16 cursors except even with 8x8 fonts except in ~200-
	 * line modes where pixels are very large and in text mode where
	 * even 13 pixels high is really 4 too many.  Clipping a 16-high
	 * cursor at 9-high gives a variable tail which looks better than
	 * a smaller cursor with a constant tail.
	 *
	 * XXX: the IS*SC() macros don't work when this is called at the
	 * end of a mode switch since UNKNOWN_SC is still set.
	 */
	if (scp->font_size <= 8 &&
	    (ypixel < 300 || !(scp->status & PIXEL_MODE))) {
		mdpp = &mousesmall[0];
		n = nitems(mousesmall);
	} else {
		mdpp = &mouselarge[0];
		n = nitems(mouselarge);
	}
	if (scp->status & PIXEL_MODE) {
		wb = 1024;
		wi = 256;
	} else {
		wb = 256;
		wi = 1024;
	}
	best_i = 0;
	best_v = 0x7fffffff;
	for (i = 0; i < n; i++) {
		v = (wb * abs(mdpp[i]->md_baspect - aspect) +
		     wi * abs(mdpp[i]->md_iaspect - aspect)) / aspect;
		if (best_v > v) {
			best_v = v;
			best_i = i;
		}
	}
	mdp = mdpp[best_i];
	scp->mouse_data = mdp;
#endif /* !SC_NO_CUTPASTE && (!SC_ALT_MOUSE_IMAGE || SC_PIXEL_MODE) */
}

static void
vga_nop(scr_stat *scp)
{
}

/* text mode renderer */

static void
vga_txtclear(scr_stat *scp, int c, int attr)
{
	sc_vtb_clear(&scp->scr, c, attr);
}

static void
vga_txtborder(scr_stat *scp, int color)
{
	vidd_set_border(scp->sc->adp, color);
}

static void
vga_txtdraw(scr_stat *scp, int from, int count, int flip)
{
	vm_offset_t p;
	int c;
	int a;

	if (from + count > scp->xsize*scp->ysize)
		count = scp->xsize*scp->ysize - from;

	if (flip) {
		for (p = sc_vtb_pointer(&scp->scr, from); count-- > 0; ++from) {
			c = sc_vtb_getc(&scp->vtb, from);
			a = sc_vtb_geta(&scp->vtb, from);
			a = vga_flipattr(a, TRUE);
			p = sc_vtb_putchar(&scp->scr, p, c, a);
		}
	} else {
		sc_vtb_copy(&scp->vtb, from, &scp->scr, from, count);
	}
}

static void 
vga_txtcursor_shape(scr_stat *scp, int base, int height, int blink)
{
	vga_setmdp(scp);
	if (base < 0 || base >= scp->font_size)
		return;
	/* the caller may set height <= 0 in order to disable the cursor */
	vidd_set_hw_cursor_shape(scp->sc->adp, base, height,
	    scp->font_size, blink);
}

static void
draw_txtcharcursor(scr_stat *scp, int at, u_short c, u_short a, int flip)
{
	sc_softc_t *sc;

	sc = scp->sc;

#ifndef SC_NO_FONT_LOADING
	if (scp->curs_attr.flags & CONS_CHAR_CURSOR) {
		unsigned char *font;
		int h;
		int i;

		if (scp->font_size < 14) {
			font = sc->font_8;
			h = 8;
		} else if (scp->font_size >= 16) {
			font = sc->font_16;
			h = 16;
		} else {
			font = sc->font_14;
			h = 14;
		}
		if (scp->curs_attr.base >= h)
			return;
		if (flip)
			a = vga_flipattr(a, TRUE);
		/*
		 * This clause handles partial-block cursors in text mode.
		 * We want to change the attribute only under the partial
		 * block, but in text mode we can only change full blocks.
		 * Use reverse video instead.
		 */
		bcopy(font + c*h, font + sc->cursor_char*h, h);
		font = font + sc->cursor_char*h;
		for (i = imax(h - scp->curs_attr.base - scp->curs_attr.height, 0);
			i < h - scp->curs_attr.base; ++i) {
			font[i] ^= 0xff;
		}
		/* XXX */
		vidd_load_font(sc->adp, 0, h, 8, font, sc->cursor_char, 1);
		sc_vtb_putc(&scp->scr, at, sc->cursor_char, a);
	} else
#endif /* SC_NO_FONT_LOADING */
	{
		if (flip)
			a = vga_flipattr(a, TRUE);
		a = vga_cursorattr_adj(scp, a, TRUE);
		sc_vtb_putc(&scp->scr, at, c, a);
	}
}

static void
vga_txtcursor(scr_stat *scp, int at, int blink, int on, int flip)
{
	video_adapter_t *adp;
	int cursor_attr;

	if (scp->curs_attr.height <= 0)	/* the text cursor is disabled */
		return;

	adp = scp->sc->adp;
	if (blink) {
		scp->status |= VR_CURSOR_BLINK;
		if (on) {
			scp->status |= VR_CURSOR_ON;
			vidd_set_hw_cursor(adp, at%scp->xsize,
			    at/scp->xsize);
		} else {
			if (scp->status & VR_CURSOR_ON)
				vidd_set_hw_cursor(adp, -1, -1);
			scp->status &= ~VR_CURSOR_ON;
		}
	} else {
		scp->status &= ~VR_CURSOR_BLINK;
		if (on) {
			scp->status |= VR_CURSOR_ON;
			draw_txtcharcursor(scp, at,
					   sc_vtb_getc(&scp->vtb, at),
					   sc_vtb_geta(&scp->vtb, at),
					   flip);
		} else {
			cursor_attr = sc_vtb_geta(&scp->vtb, at);
			if (flip)
				cursor_attr = vga_flipattr(cursor_attr, TRUE);
			if (scp->status & VR_CURSOR_ON)
				sc_vtb_putc(&scp->scr, at,
					    sc_vtb_getc(&scp->vtb, at),
					    cursor_attr);
			scp->status &= ~VR_CURSOR_ON;
		}
	}
}

static void
vga_txtblink(scr_stat *scp, int at, int flip)
{
}

int sc_txtmouse_no_retrace_wait;

#ifndef SC_NO_CUTPASTE

static void
draw_txtmouse(scr_stat *scp, int x, int y)
{
#ifndef SC_ALT_MOUSE_IMAGE
    if (ISMOUSEAVAIL(scp->sc->adp->va_flags)) {
	const struct mousedata *mdp;
	uint32_t border, interior;
	u_char font_buf[128];
	u_short cursor[32];
	u_char c;
	int pos;
	int xoffset, yoffset;
	int crtc_addr;
	int i;

	mdp = scp->mouse_data;

	/* prepare mousepointer char's bitmaps */
	pos = (y/scp->font_size - scp->yoff)*scp->xsize + x/8 - scp->xoff;
	bcopy(scp->font + sc_vtb_getc(&scp->scr, pos)*scp->font_size,
	      &font_buf[0], scp->font_size);
	bcopy(scp->font + sc_vtb_getc(&scp->scr, pos + 1)*scp->font_size,
	      &font_buf[32], scp->font_size);
	bcopy(scp->font 
		 + sc_vtb_getc(&scp->scr, pos + scp->xsize)*scp->font_size,
	      &font_buf[64], scp->font_size);
	bcopy(scp->font
		 + sc_vtb_getc(&scp->scr, pos + scp->xsize + 1)*scp->font_size,
	      &font_buf[96], scp->font_size);
	for (i = 0; i < scp->font_size; ++i) {
		cursor[i] = font_buf[i]<<8 | font_buf[i+32];
		cursor[i + scp->font_size] = font_buf[i+64]<<8 | font_buf[i+96];
	}

	/* now and-or in the mousepointer image */
	xoffset = x%8;
	yoffset = y%scp->font_size;
	for (i = 0; i < 16; ++i) {
		border = mdp->md_border[i] << 8; /* avoid right shifting out */
		interior = mdp->md_interior[i] << 8;
		border >>= xoffset;		/* normalize */
		interior >>= xoffset;
		if (scp->sc->adp->va_flags & V_ADP_CWIDTH9) {
			/* skip gaps between characters */
			border = (border & 0xff0000) |
				 (border & 0x007f80) << 1 |
				 (border & 0x00003f) << 2;
			interior = (interior & 0xff0000) |
				   (interior & 0x007f80) << 1 |
				   (interior & 0x00003f) << 2;
		}
		border >>= 8;			/* back to normal position */
		interior >>= 8;
		cursor[i + yoffset] = (cursor[i + yoffset]  & ~border) |
				      interior;
	}
	for (i = 0; i < scp->font_size; ++i) {
		font_buf[i] = (cursor[i] & 0xff00) >> 8;
		font_buf[i + 32] = cursor[i] & 0xff;
		font_buf[i + 64] = (cursor[i + scp->font_size] & 0xff00) >> 8;
		font_buf[i + 96] = cursor[i + scp->font_size] & 0xff;
	}

#if 1
	/* wait for vertical retrace to avoid jitter on some videocards */
	crtc_addr = scp->sc->adp->va_crtc_addr;
	while (!sc_txtmouse_no_retrace_wait &&
	    !(inb(crtc_addr + 6) & 0x08))
		/* idle */ ;
#endif
	c = scp->sc->mouse_char;
	vidd_load_font(scp->sc->adp, 0, 32, 8, font_buf, c, 4); 

	sc_vtb_putc(&scp->scr, pos, c, sc_vtb_geta(&scp->scr, pos));
	/* FIXME: may be out of range! */
	sc_vtb_putc(&scp->scr, pos + scp->xsize, c + 2,
		    sc_vtb_geta(&scp->scr, pos + scp->xsize));
	if (x < (scp->xsize - 1)*8) {
		sc_vtb_putc(&scp->scr, pos + 1, c + 1,
			    sc_vtb_geta(&scp->scr, pos + 1));
		sc_vtb_putc(&scp->scr, pos + scp->xsize + 1, c + 3,
			    sc_vtb_geta(&scp->scr, pos + scp->xsize + 1));
	}
    } else
#endif /* SC_ALT_MOUSE_IMAGE */
    {
	/* Red, magenta and brown are mapped to green to keep it readable */
	static const int col_conv[16] = {
		6, 6, 6, 6, 2, 2, 2, 6, 14, 14, 14, 14, 10, 10, 10, 14
	};
	int pos;
	int color;
	int a;

	pos = (y/scp->font_size - scp->yoff)*scp->xsize + x/8 - scp->xoff;
	a = sc_vtb_geta(&scp->scr, pos);
	if (scp->sc->adp->va_flags & V_ADP_COLOR)
		color = (col_conv[(a & 0xf000) >> 12] << 12)
			| ((a & 0x0f00) | 0x0800);
	else
		color = ((a & 0xf000) >> 4) | ((a & 0x0f00) << 4);
	sc_vtb_putc(&scp->scr, pos, sc_vtb_getc(&scp->scr, pos), color);
    }
}

static void
remove_txtmouse(scr_stat *scp, int x, int y)
{
}

static void 
vga_txtmouse(scr_stat *scp, int x, int y, int on)
{
	if (on)
		draw_txtmouse(scp, x, y);
	else
		remove_txtmouse(scp, x, y);
}

#endif /* SC_NO_CUTPASTE */

#ifdef SC_PIXEL_MODE

/* pixel (raster text) mode renderer */

static void
vga_rndrinit(scr_stat *scp)
{
	if (scp->sc->adp->va_info.vi_mem_model == V_INFO_MM_PLANAR) {
		scp->rndr->clear = vga_pxlclear_planar;
		scp->rndr->draw_border = vga_pxlborder_planar;
		scp->rndr->draw = vga_vgadraw_planar;
		scp->rndr->draw_cursor = vga_pxlcursor_planar;
		scp->rndr->blink_cursor = vga_pxlblink_planar;
		scp->rndr->draw_mouse = vga_pxlmouse_planar;
	} else
	if (scp->sc->adp->va_info.vi_mem_model == V_INFO_MM_DIRECT ||
	    scp->sc->adp->va_info.vi_mem_model == V_INFO_MM_PACKED) {
		scp->rndr->clear = vga_pxlclear_direct;
		scp->rndr->draw_border = vga_pxlborder_direct;
		scp->rndr->draw = vga_vgadraw_direct;
		scp->rndr->draw_cursor = vga_pxlcursor_direct;
		scp->rndr->blink_cursor = vga_pxlblink_direct;
		scp->rndr->draw_mouse = vga_pxlmouse_direct;
	}
}

static void
vga_pxlclear_direct(scr_stat *scp, int c, int attr)
{
	vm_offset_t p;
	int line_width;
	int pixel_size;
	int lines;
	int i;

	line_width = scp->sc->adp->va_line_width;
	pixel_size = scp->sc->adp->va_info.vi_pixel_size;
	lines = scp->ysize * scp->font_size; 
	p = scp->sc->adp->va_window +
	    line_width * scp->yoff * scp->font_size +
	    scp->xoff * 8 * pixel_size;

	for (i = 0; i < lines; ++i) {
		bzero_io((void *)p, scp->xsize * 8 * pixel_size);
		p += line_width;
	}
}

static void
vga_pxlclear_planar(scr_stat *scp, int c, int attr)
{
	vm_offset_t p;
	int line_width;
	int lines;
	int i;

	/* XXX: we are just filling the screen with the background color... */
	outw(GDCIDX, 0x0005);		/* read mode 0, write mode 0 */
	outw(GDCIDX, 0x0003);		/* data rotate/function select */
	outw(GDCIDX, 0x0f01);		/* set/reset enable */
	outw(GDCIDX, 0xff08);		/* bit mask */
	outw(GDCIDX, ((attr & 0xf000) >> 4) | 0x00); /* set/reset */
	line_width = scp->sc->adp->va_line_width;
	lines = scp->ysize*scp->font_size; 
	p = scp->sc->adp->va_window + line_width*scp->yoff*scp->font_size
		+ scp->xoff;
	for (i = 0; i < lines; ++i) {
		bzero_io((void *)p, scp->xsize);
		p += line_width;
	}
	outw(GDCIDX, 0x0000);		/* set/reset */
	outw(GDCIDX, 0x0001);		/* set/reset enable */
}

static void
vga_pxlborder_direct(scr_stat *scp, int color)
{
	vm_offset_t s;
	vm_offset_t e;
	vm_offset_t f;
	int line_width;
	int pixel_size;
	int x;
	int y;
	int i;

	line_width = scp->sc->adp->va_line_width;
	pixel_size = scp->sc->adp->va_info.vi_pixel_size;

	if (scp->yoff > 0) {
		s = scp->sc->adp->va_window;
		e = s + line_width * scp->yoff * scp->font_size;

		for (f = s; f < e; f += pixel_size)
			DRAW_PIXEL(scp, f, color);
	}

	y = (scp->yoff + scp->ysize) * scp->font_size;

	if (scp->ypixel > y) {
		s = scp->sc->adp->va_window + line_width * y;
		e = s + line_width * (scp->ypixel - y);

		for (f = s; f < e; f += pixel_size)
			DRAW_PIXEL(scp, f, color);
	}

	y = scp->yoff * scp->font_size;
	x = scp->xpixel / 8 - scp->xoff - scp->xsize;

	for (i = 0; i < scp->ysize * scp->font_size; ++i) {
		if (scp->xoff > 0) {
			s = scp->sc->adp->va_window + line_width * (y + i);
			e = s + scp->xoff * 8 * pixel_size;

			for (f = s; f < e; f += pixel_size)
				DRAW_PIXEL(scp, f, color);
		}

		if (x > 0) {
			s = scp->sc->adp->va_window + line_width * (y + i) +
			    scp->xoff * 8 * pixel_size +
			    scp->xsize * 8 * pixel_size;
			e = s + x * 8 * pixel_size;

			for (f = s; f < e; f += pixel_size)
				DRAW_PIXEL(scp, f, color);
		}
	}
}

static void
vga_pxlborder_planar(scr_stat *scp, int color)
{
	vm_offset_t p;
	int line_width;
	int x;
	int y;
	int i;

	vidd_set_border(scp->sc->adp, color);

	outw(GDCIDX, 0x0005);		/* read mode 0, write mode 0 */
	outw(GDCIDX, 0x0003);		/* data rotate/function select */
	outw(GDCIDX, 0x0f01);		/* set/reset enable */
	outw(GDCIDX, 0xff08);		/* bit mask */
	outw(GDCIDX, (color << 8) | 0x00);	/* set/reset */
	line_width = scp->sc->adp->va_line_width;
	p = scp->sc->adp->va_window;
	if (scp->yoff > 0)
		bzero_io((void *)p, line_width*scp->yoff*scp->font_size);
	y = (scp->yoff + scp->ysize)*scp->font_size;
	if (scp->ypixel > y)
		bzero_io((void *)(p + line_width*y), line_width*(scp->ypixel - y));
	y = scp->yoff*scp->font_size;
	x = scp->xpixel/8 - scp->xoff - scp->xsize;
	for (i = 0; i < scp->ysize*scp->font_size; ++i) {
		if (scp->xoff > 0)
			bzero_io((void *)(p + line_width*(y + i)), scp->xoff);
		if (x > 0)
			bzero_io((void *)(p + line_width*(y + i)
				     + scp->xoff + scp->xsize), x);
	}
	outw(GDCIDX, 0x0000);		/* set/reset */
	outw(GDCIDX, 0x0001);		/* set/reset enable */
}

static void
vga_vgadraw_direct(scr_stat *scp, int from, int count, int flip)
{
	vm_offset_t d;
	vm_offset_t e;
	u_char *f;
	u_short col1, col2, color;
	int line_width, pixel_size;
	int i, j, k;
	int a;

	line_width = scp->sc->adp->va_line_width;
	pixel_size = scp->sc->adp->va_info.vi_pixel_size;

	d = GET_PIXEL(scp, from, 8 * pixel_size, line_width);

	if (from + count > scp->xsize * scp->ysize)
		count = scp->xsize * scp->ysize - from;

	for (i = from; count-- > 0; ++i) {
		a = sc_vtb_geta(&scp->vtb, i);

		if (flip)
			a = vga_flipattr(a, FALSE);
		col1 = (a & 0x0f00) >> 8;
		col2 = (a & 0xf000) >> 12;

		e = d;
		f = &(scp->font[sc_vtb_getc(&scp->vtb, i) * scp->font_size]);

		for (j = 0; j < scp->font_size; ++j, ++f) {
			for (k = 0; k < 8; ++k) {
				color = *f & (1 << (7 - k)) ? col1 : col2;
				DRAW_PIXEL(scp, e + pixel_size * k, color);
			}

			e += line_width;
		}

		d += 8 * pixel_size;

		if ((i % scp->xsize) == scp->xsize - 1)
			d += scp->font_size * line_width -
			    scp->xsize * 8 * pixel_size;
	}
}

static void
vga_vgadraw_planar(scr_stat *scp, int from, int count, int flip)
{
	vm_offset_t d;
	vm_offset_t e;
	u_char *f;
	u_short bg, fg;
	u_short col1, col2;
	int line_width;
	int i, j;
	int a;
	u_char c;

	line_width = scp->sc->adp->va_line_width;

	d = GET_PIXEL(scp, from, 1, line_width);

	if (scp->sc->adp->va_type == KD_VGA) {
		outw(GDCIDX, 0x0305);	/* read mode 0, write mode 3 */
		outw(GDCIDX, 0xff08);	/* bit mask */
	} else
		outw(GDCIDX, 0x0005);	/* read mode 0, write mode 0 */
	outw(GDCIDX, 0x0003);		/* data rotate/function select */
	outw(GDCIDX, 0x0f01);		/* set/reset enable */
	fg = bg = -1;
	if (from + count > scp->xsize*scp->ysize)
		count = scp->xsize*scp->ysize - from;
	for (i = from; count-- > 0; ++i) {
		a = sc_vtb_geta(&scp->vtb, i);
		if (flip)
			a = vga_flipattr(a, FALSE);
		col1 = a & 0x0f00;
		col2 = (a & 0xf000) >> 4;
		/* set background color in EGA/VGA latch */
		if (bg != col2) {
			bg = col2;
			fg = -1;
			outw(GDCIDX, bg | 0x00); /* set/reset */
			if (scp->sc->adp->va_type != KD_VGA)
				outw(GDCIDX, 0xff08); /* bit mask */
			writeb(d, 0xff);
			c = readb(d);		/* set bg color in the latch */
		}
		/* foreground color */
		if (fg != col1) {
			fg = col1;
			outw(GDCIDX, col1 | 0x00); /* set/reset */
		}
		e = d;
		f = &(scp->font[sc_vtb_getc(&scp->vtb, i)*scp->font_size]);
		for (j = 0; j < scp->font_size; ++j, ++f) {
			if (scp->sc->adp->va_type == KD_VGA)
				writeb(e, *f);	
			else {
				outw(GDCIDX, (*f << 8) | 0x08);	/* bit mask */
				writeb(e, 0);	
			}
			e += line_width;
		}
		++d;
		if ((i % scp->xsize) == scp->xsize - 1)
			d += scp->font_size * line_width - scp->xsize;
	}
	if (scp->sc->adp->va_type == KD_VGA)
		outw(GDCIDX, 0x0005);	/* read mode 0, write mode 0 */
	else
		outw(GDCIDX, 0xff08);	/* bit mask */
	outw(GDCIDX, 0x0000);		/* set/reset */
	outw(GDCIDX, 0x0001);		/* set/reset enable */
}

static void 
vga_pxlcursor_shape(scr_stat *scp, int base, int height, int blink)
{
	vga_setmdp(scp);
}

static void 
draw_pxlcursor_direct(scr_stat *scp, int at, int on, int flip)
{
	vm_offset_t d;
	u_char *f;
	int line_width, pixel_size;
	int height;
	int col1, col2, color;
	int a;
	int i, j;

	line_width = scp->sc->adp->va_line_width;
	pixel_size = scp->sc->adp->va_info.vi_pixel_size;

	d = GET_PIXEL(scp, at, 8 * pixel_size, line_width) +
	    (scp->font_size - scp->curs_attr.base - 1) * line_width;

	a = sc_vtb_geta(&scp->vtb, at);

	if (flip)
		a = vga_flipattr(a, FALSE);
	if (on)
		a = vga_cursorattr_adj(scp, a, FALSE);
	col1 = (a & 0x0f00) >> 8;
	col2 = a >> 12;

	f = &(scp->font[sc_vtb_getc(&scp->vtb, at) * scp->font_size +
	      scp->font_size - scp->curs_attr.base - 1]);

	height = imin(scp->curs_attr.height, scp->font_size);

	for (i = 0; i < height; ++i, --f) {
		for (j = 0; j < 8; ++j) {
			color = *f & (1 << (7 - j)) ? col1 : col2;
			DRAW_PIXEL(scp, d + pixel_size * j, color);
		}

		d -= line_width;
	}
}

static void 
draw_pxlcursor_planar(scr_stat *scp, int at, int on, int flip)
{
	vm_offset_t d;
	u_char *f;
	int line_width;
	int height;
	int col;
	int a;
	int i;
	u_char c;

	line_width = scp->sc->adp->va_line_width;

	d = GET_PIXEL(scp, at, 1, line_width) +
	    (scp->font_size - scp->curs_attr.base - 1) * line_width;

	outw(GDCIDX, 0x0005);		/* read mode 0, write mode 0 */
	outw(GDCIDX, 0x0003);		/* data rotate/function select */
	outw(GDCIDX, 0x0f01);		/* set/reset enable */
	/* set background color in EGA/VGA latch */
	a = sc_vtb_geta(&scp->vtb, at);
	if (flip)
		a = vga_flipattr(a, FALSE);
	if (on)
		a = vga_cursorattr_adj(scp, a, FALSE);
	col = (a & 0xf000) >> 4;
	outw(GDCIDX, col | 0x00);	/* set/reset */
	outw(GDCIDX, 0xff08);		/* bit mask */
	writeb(d, 0);
	c = readb(d);			/* set bg color in the latch */
	/* foreground color */
	col = a & 0x0f00;
	outw(GDCIDX, col | 0x00);	/* set/reset */
	f = &(scp->font[sc_vtb_getc(&scp->vtb, at)*scp->font_size
		+ scp->font_size - scp->curs_attr.base - 1]);
	height = imin(scp->curs_attr.height, scp->font_size);
	for (i = 0; i < height; ++i, --f) {
		outw(GDCIDX, (*f << 8) | 0x08);	/* bit mask */
	       	writeb(d, 0);
		d -= line_width;
	}
	outw(GDCIDX, 0x0000);		/* set/reset */
	outw(GDCIDX, 0x0001);		/* set/reset enable */
	outw(GDCIDX, 0xff08);		/* bit mask */
}

static int pxlblinkrate = 0;

static void 
vga_pxlcursor_direct(scr_stat *scp, int at, int blink, int on, int flip)
{
	if (scp->curs_attr.height <= 0)	/* the text cursor is disabled */
		return;

	if (on) {
		if (!blink) {
			scp->status |= VR_CURSOR_ON;
			draw_pxlcursor_direct(scp, at, on, flip);
		} else if (++pxlblinkrate & 4) {
			pxlblinkrate = 0;
			scp->status ^= VR_CURSOR_ON;
			draw_pxlcursor_direct(scp, at,
					      scp->status & VR_CURSOR_ON,
					      flip);
		}
	} else {
		if (scp->status & VR_CURSOR_ON)
			draw_pxlcursor_direct(scp, at, on, flip);
		scp->status &= ~VR_CURSOR_ON;
	}
	if (blink)
		scp->status |= VR_CURSOR_BLINK;
	else
		scp->status &= ~VR_CURSOR_BLINK;
}

static void 
vga_pxlcursor_planar(scr_stat *scp, int at, int blink, int on, int flip)
{
	if (scp->curs_attr.height <= 0)	/* the text cursor is disabled */
		return;

	if (on) {
		if (!blink) {
			scp->status |= VR_CURSOR_ON;
			draw_pxlcursor_planar(scp, at, on, flip);
		} else if (++pxlblinkrate & 4) {
			pxlblinkrate = 0;
			scp->status ^= VR_CURSOR_ON;
			draw_pxlcursor_planar(scp, at,
					      scp->status & VR_CURSOR_ON,
					      flip);
		}
	} else {
		if (scp->status & VR_CURSOR_ON)
			draw_pxlcursor_planar(scp, at, on, flip);
		scp->status &= ~VR_CURSOR_ON;
	}
	if (blink)
		scp->status |= VR_CURSOR_BLINK;
	else
		scp->status &= ~VR_CURSOR_BLINK;
}

static void
vga_pxlblink_direct(scr_stat *scp, int at, int flip)
{
	if (!(scp->status & VR_CURSOR_BLINK))
		return;
	if (!(++pxlblinkrate & 4))
		return;
	pxlblinkrate = 0;
	scp->status ^= VR_CURSOR_ON;
	draw_pxlcursor_direct(scp, at, scp->status & VR_CURSOR_ON, flip);
}

static void
vga_pxlblink_planar(scr_stat *scp, int at, int flip)
{
	if (!(scp->status & VR_CURSOR_BLINK))
		return;
	if (!(++pxlblinkrate & 4))
		return;
	pxlblinkrate = 0;
	scp->status ^= VR_CURSOR_ON;
	draw_pxlcursor_planar(scp, at, scp->status & VR_CURSOR_ON, flip);
}

#ifndef SC_NO_CUTPASTE

static void
draw_pxlmouse_planar(scr_stat *scp, int x, int y)
{
	const struct mousedata *mdp;
	vm_offset_t p;
	int line_width;
	int xoff, yoff;
	int ymax;
	uint32_t m;
	int i, j, k;
	uint8_t m1;

	mdp = scp->mouse_data;
	line_width = scp->sc->adp->va_line_width;
	xoff = (x - scp->xoff*8)%8;
	yoff = y - rounddown(y, line_width);
	ymax = imin(y + mdp->md_height, scp->ypixel);

	if (scp->sc->adp->va_type == KD_VGA) {
		outw(GDCIDX, 0x0305);	/* read mode 0, write mode 3 */
		outw(GDCIDX, 0xff08);	/* bit mask */
	} else
		outw(GDCIDX, 0x0005);	/* read mode 0, write mode 0 */
	outw(GDCIDX, 0x0003);		/* data rotate/function select */
	outw(GDCIDX, 0x0f01);		/* set/reset enable */

	outw(GDCIDX, (scp->curs_attr.mouse_ba << 8) | 0x00); /* set/reset */
	p = scp->sc->adp->va_window + line_width*y + x/8;
	for (i = y, j = 0; i < ymax; ++i, ++j) {
		m = mdp->md_border[j] << 8 >> xoff;
		for (k = 0; k < 3; ++k) {
			m1 = m >> (8 * (2 - k));
			if (m1 != 0 && x + 8 * k < scp->xpixel) {
				readb(p + k);
				if (scp->sc->adp->va_type == KD_VGA)
					writeb(p + k, m1);
				else {
					/* bit mask: */
					outw(GDCIDX, (m1 << 8) | 0x08);
					writeb(p + k, 0);
				}
			}
		}
		p += line_width;
	}
	outw(GDCIDX, (scp->curs_attr.mouse_ia << 8) | 0x00); /* set/reset */
	p = scp->sc->adp->va_window + line_width*y + x/8;
	for (i = y, j = 0; i < ymax; ++i, ++j) {
		m = mdp->md_interior[j] << 8 >> xoff;
		for (k = 0; k < 3; ++k) {
			m1 = m >> (8 * (2 - k));
			if (m1 != 0 && x + 8 * k < scp->xpixel) {
				readb(p + k);
				if (scp->sc->adp->va_type == KD_VGA)
					writeb(p + k, m1);
				else {
					/* bit mask: */
					outw(GDCIDX, (m1 << 8) | 0x08);
					writeb(p + k, 0);
				}
			}
		}
		p += line_width;
	}
	if (scp->sc->adp->va_type == KD_VGA)
		outw(GDCIDX, 0x0005);	/* read mode 0, write mode 0 */
	else
		outw(GDCIDX, 0xff08);	/* bit mask */
	outw(GDCIDX, 0x0000);		/* set/reset */
	outw(GDCIDX, 0x0001);		/* set/reset enable */
}

static void
remove_pxlmouse_planar(scr_stat *scp, int x, int y)
{
	const struct mousedata *mdp;
	vm_offset_t p;
	int bx, by, i, line_width, xend, xoff, yend, yoff;

	mdp = scp->mouse_data;

	/*
	 * It is only necessary to remove the mouse image where it overlaps
	 * the border.  Determine the overlap, and do nothing if it is empty.
	 */
	bx = (scp->xoff + scp->xsize) * 8;
	by = (scp->yoff + scp->ysize) * scp->font_size;
	xend = imin(x + mdp->md_width, scp->xpixel);
	yend = imin(y + mdp->md_height, scp->ypixel);
	if (xend <= bx && yend <= by)
		return;

	/* Repaint the non-empty overlap. */
	line_width = scp->sc->adp->va_line_width;
	outw(GDCIDX, 0x0005);		/* read mode 0, write mode 0 */
	outw(GDCIDX, 0x0003);		/* data rotate/function select */
	outw(GDCIDX, 0x0f01);		/* set/reset enable */
	outw(GDCIDX, 0xff08);		/* bit mask */
	outw(GDCIDX, (scp->border << 8) | 0x00);	/* set/reset */
	for (i = x / 8, xoff = i * 8; xoff < xend; ++i, xoff += 8) {
		yoff = (xoff >= bx) ? y : by;
		p = scp->sc->adp->va_window + yoff * line_width + i;
		for (; yoff < yend; ++yoff, p += line_width)
			writeb(p, 0);
	}
	outw(GDCIDX, 0x0000);		/* set/reset */
	outw(GDCIDX, 0x0001);		/* set/reset enable */
}

static void 
vga_pxlmouse_direct(scr_stat *scp, int x, int y, int on)
{
	const struct mousedata *mdp;
	vm_offset_t p;
	int line_width, pixel_size;
	int xend, yend;
	int i, j;

	mdp = scp->mouse_data;

	/*
	 * Determine overlap with the border and then if removing, do nothing
	 * if the overlap is empty.
	 */
	xend = imin(x + mdp->md_width, scp->xpixel);
	yend = imin(y + mdp->md_height, scp->ypixel);
	if (!on && xend <= (scp->xoff + scp->xsize) * 8 &&
	    yend <= (scp->yoff + scp->ysize) * scp->font_size)
		return;

	line_width = scp->sc->adp->va_line_width;
	pixel_size = scp->sc->adp->va_info.vi_pixel_size;

	if (on)
		goto do_on;

	/* Repaint overlap with the border (mess up the corner a little). */
	p = scp->sc->adp->va_window + y * line_width + x * pixel_size;
	for (i = 0; i < yend - y; i++, p += line_width)
		for (j = xend - x - 1; j >= 0; j--)
			DRAW_PIXEL(scp, p + j * pixel_size, scp->border);

	return;

do_on:
	p = scp->sc->adp->va_window + y * line_width + x * pixel_size;
	for (i = 0; i < yend - y; i++, p += line_width)
		for (j = xend - x - 1; j >= 0; j--)
			if (mdp->md_interior[i] & (1 << (15 - j)))
				DRAW_PIXEL(scp, p + j * pixel_size,
				    scp->curs_attr.mouse_ia);
			else if (mdp->md_border[i] & (1 << (15 - j)))
				DRAW_PIXEL(scp, p + j * pixel_size,
				    scp->curs_attr.mouse_ba);
}

static void 
vga_pxlmouse_planar(scr_stat *scp, int x, int y, int on)
{
	if (on)
		draw_pxlmouse_planar(scp, x, y);
	else
		remove_pxlmouse_planar(scp, x, y);
}

#endif /* SC_NO_CUTPASTE */
#endif /* SC_PIXEL_MODE */

#ifndef SC_NO_MODE_CHANGE

/* graphics mode renderer */

static void
vga_grborder(scr_stat *scp, int color)
{
	vidd_set_border(scp->sc->adp, color);
}

#endif

/*	$OpenBSD: rasops.h,v 1.26 2023/02/03 18:32:31 miod Exp $ */
/* 	$NetBSD: rasops.h,v 1.13 2000/06/13 13:36:54 ad Exp $ */

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

#ifndef _RASOPS_H_
#define _RASOPS_H_ 1

#include <sys/task.h>

#ifdef	SMALL_KERNEL
#define	RASOPS_SMALL
#endif

#include "rasops_glue.h"

struct wsdisplay_font;

/* For rasops_info::ri_flg */
#define RI_FULLCLEAR	0x0001	/* eraserows() hack to clear full screen */
#define RI_FORCEMONO	0x0002	/* monochrome output even if we can do color */
#define RI_BSWAP	0x0004	/* framebuffer endianness doesn't match CPU */
#define RI_CURSOR	0x0008	/* cursor is switched on */
#define RI_CLEAR	0x0010	/* clear display on startup */
#define	RI_CLEARMARGINS	0x0020	/* clear display margins on startup */
#define RI_CENTER	0x0040	/* center onscreen output */
#define RI_CURSORCLIP	0x0080	/* cursor is currently clipped */
#define RI_ROTATE_CW	0x0100	/* display is rotated, 90 deg clockwise */
#define RI_ROTATE_CCW	0x0200	/* display is rotated, 90 deg anti-clockwise */
#define RI_CFGDONE	0x0400	/* rasops_reconfig() completed successfully */
#define RI_VCONS	0x0800	/* virtual consoles */
#define RI_WRONLY	0x1000	/* avoid framebuffer reads */

struct rasops_screen;

struct rasops_info {
	/* These must be filled in by the caller */
	int	ri_depth;	/* depth in bits */
	u_char	*ri_bits;	/* ptr to bits */
	int	ri_width;	/* width (pels) */
	int	ri_height;	/* height (pels) */
	int	ri_stride;	/* stride in bytes */

	/*
	 * These can optionally be left zeroed out. If you fill ri_font,
	 * but aren't using wsfont, set ri_wsfcookie to -1.
	 */
	struct	wsdisplay_font *ri_font;
	int	ri_wsfcookie;	/* wsfont cookie */
	void	*ri_hw;		/* driver private data; ignored by rasops */
	struct wsdisplay_charcell *ri_bs; /* character backing store */
	int	ri_crow;	/* cursor row */
	int	ri_ccol;	/* cursor column */
	int	ri_flg;		/* various operational flags */

	/*
	 * These are optional and will default if zero. Meaningless
	 * on depths other than 15, 16, 24 and 32 bits per pel. On
	 * 24 bit displays, ri_{r,g,b}num must be 8.
	 */
	u_char	ri_rnum;	/* number of bits for red */
	u_char	ri_gnum;	/* number of bits for green */
	u_char	ri_bnum;	/* number of bits for blue */
	u_char	ri_rpos;	/* which bit red starts at */
	u_char	ri_gpos;	/* which bit green starts at */
	u_char	ri_bpos;	/* which bit blue starts at */

	/* These are filled in by rasops_init() */
	int	ri_emuwidth;	/* width we actually care about */
	int	ri_emuheight;	/* height we actually care about */
	int	ri_emustride;	/* bytes per row we actually care about */
	int	ri_rows;	/* number of rows (characters, not pels) */
	int	ri_cols;	/* number of columns (characters, not pels) */
	int	ri_delta;	/* row delta in bytes */
	int	ri_pelbytes;	/* bytes per pel (may be zero) */
	int	ri_fontscale;	/* fontheight * fontstride */
	int	ri_xscale;	/* fontwidth * pelbytes */
	int	ri_yscale;	/* fontheight * stride */
	u_char  *ri_origbits;	/* where screen bits actually start */
	int	ri_xorigin;	/* where ri_bits begins (x) */
	int	ri_yorigin;	/* where ri_bits begins (y) */
	int32_t	ri_devcmap[16]; /* color -> framebuffer data */

	/* The emulops you need to use, and the screen caps for wscons */
	struct	wsdisplay_emulops ri_ops;
	int	ri_caps;

	/* Callbacks so we can share some code */
	int	(*ri_do_cursor)(struct rasops_info *);
	void	(*ri_updatecursor)(struct rasops_info *);

#if NRASOPS_ROTATION > 0
	/* Used to intercept putchar to permit display rotation */
	struct	wsdisplay_emulops ri_real_ops;
#endif

	int	ri_nscreens;
	LIST_HEAD(, rasops_screen) ri_screens;
	struct rasops_screen *ri_active;

	void	(*ri_switchcb)(void *, int, int);
	void	*ri_switchcbarg;
	void	*ri_switchcookie;
	struct task ri_switchtask;

	int	(*ri_putchar)(void *, int, int, u_int, uint32_t);
	int	(*ri_copycols)(void *, int, int, int, int);
	int	(*ri_erasecols)(void *, int, int, int, uint32_t);
	int	(*ri_copyrows)(void *, int, int, int);
	int	(*ri_eraserows)(void *, int, int, uint32_t);
	int	(*ri_pack_attr)(void *, int, int, int, uint32_t *);
};

#define DELTA(p, d, cast) ((p) = (cast)((caddr_t)(p) + (d)))

/*
 * rasops_init().
 *
 * Integer parameters are the number of rows and columns we'd *like*.
 *
 * In terms of optimization, fonts that are a multiple of 8 pixels wide
 * work the best.
 *
 * rasops_init() takes care of rasops_reconfig(). The parameters to both
 * are the same. If calling rasops_reconfig() to change the font and
 * ri_wsfcookie >= 0, you must call wsfont_unlock() on it, and reset it
 * to -1 (or a new, valid cookie).
 */

/*
 * Per-depth initialization functions. These should not be called outside
 * the rasops code.
 */
void	rasops1_init(struct rasops_info *);
void	rasops4_init(struct rasops_info *);
void	rasops8_init(struct rasops_info *);
void	rasops15_init(struct rasops_info *);
void	rasops24_init(struct rasops_info *);
void	rasops32_init(struct rasops_info *);

/* rasops.c */
int	rasops_init(struct rasops_info *, int, int);
int	rasops_reconfig(struct rasops_info *, int, int);
int	rasops_eraserows(void *, int, int, uint32_t);
int	rasops_erasecols(void *, int, int, int, uint32_t);

int	rasops_alloc_screen(void *, void **, int *, int *, uint32_t *);
void	rasops_free_screen(void *, void *);
int	rasops_show_screen(void *, void *, int,
	    void (*)(void *, int, int), void *);
int	rasops_load_font(void *, void *, struct wsdisplay_font *);
int	rasops_list_font(void *, struct wsdisplay_font *);
int	rasops_getchar(void *, int, int, struct wsdisplay_charcell *);
void	rasops_scrollback(void *, void *, int);
void	rasops_claim_framebuffer(paddr_t, psize_t, struct device *);
int	rasops_check_framebuffer(paddr_t);

extern const u_char	rasops_cmap[256*3];

#endif /* _RASOPS_H_ */

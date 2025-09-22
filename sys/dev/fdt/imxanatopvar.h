/* $OpenBSD: imxanatopvar.h,v 1.2 2018/06/28 10:07:35 kettenis Exp $ */
/*
 * Copyright (c) 2018 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

enum imxanatop_clocks {
	/* OSC */
	OSC,		/* 24 MHz OSC */

	/* PLLs */
	ARM_PLL1,	/* ARM core PLL */
	SYS_PLL2,	/* System PLL: 528 MHz */
	USB1_PLL3,	/* OTG USB PLL: 480 MHz */
	USB2_PLL,	/* Host USB PLL: 480 MHz */
	AUD_PLL4,	/* Audio PLL */
	VID_PLL5,	/* Video PLL */
	ENET_PLL6,	/* ENET PLL */
	MLB_PLL,	/* MLB PLL */

	/* SYS_PLL2 PFDs */
	SYS_PLL2_PFD0,	/* 352 MHz */
	SYS_PLL2_PFD1,	/* 594 MHz */
	SYS_PLL2_PFD2,	/* 396 MHz */

	/* USB1_PLL3 PFDs */
	USB1_PLL3_PFD0,	/* 720 MHz */
	USB1_PLL3_PFD1,	/* 540 MHz */
	USB1_PLL3_PFD2,	/* 508.2 MHz */
	USB1_PLL3_PFD3,	/* 454.7 MHz */
};

uint32_t imxanatop_decode_pll(enum imxanatop_clocks, uint32_t);
uint32_t imxanatop_get_pll2_pfd(unsigned int);
uint32_t imxanatop_get_pll3_pfd(unsigned int);

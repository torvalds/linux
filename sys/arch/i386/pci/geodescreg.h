/*      $OpenBSD: geodescreg.h,v 1.3 2006/02/01 13:08:12 mickey Exp $     */

/*
 * Copyright (c) 2003 Markus Friedl <markus@openbsd.org>
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

/*
 * Geode SC1100 Information Appliance On a Chip
 * http://www.national.com/ds.cgi/SC/SC1100.pdf
 */

/* Configuration Space Register Map */

#define SC1100_F5_SCRATCHPAD	0x64

#define	GCB_WDTO	0x0000	/* WATCHDOG Timeout */
#define	GCB_WDCNFG	0x0002	/* WATCHDOG Configuration */
#define	GCB_WDSTS	0x0004	/* WATCHDOG Status */
#define	GCB_TSC		0x0008	/* Cyclecounter at 27MHz */
#define	GCB_TSCNFG	0x000c	/* config for the above */
#define	GCB_IID		0x003c	/* IA On a Chip ID */
#define	GCB_REV		0x003d	/* Revision */
#define	GCB_CBA		0x003e	/* Configuration Base Address */

/* Watchdog */

#define	WD32KPD_ENABLE	0x0000
#define	WD32KPD_DISABLE	0x0100
#define	WDTYPE1_RESET	0x0030
#define	WDTYPE2_RESET	0x00c0
#define	WDPRES_DIV_512	0x0009
#define	WDPRES_DIV_8192	0x000d
#define	WDCNFG_MASK	0x00ff
#define	WDOVF_CLEAR	0x0001

/* cyclecounter */

#define	TSC_ENABLE	0x0200


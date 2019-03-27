/*	$OpenBSD: if_urtwn.c,v 1.16 2011/02/10 17:26:40 jakemsr Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef IF_RTWN_DEBUG_H
#define IF_RTWN_DEBUG_H

#include "opt_rtwn.h"

#ifdef RTWN_DEBUG
enum {
	RTWN_DEBUG_XMIT		= 0x00000001,	/* basic xmit operation */
	RTWN_DEBUG_XMIT_DESC	= 0x00000002,	/* xmit descriptors */
	RTWN_DEBUG_RECV		= 0x00000004,	/* basic recv operation */
	RTWN_DEBUG_RECV_DESC	= 0x00000008,	/* recv descriptors */
	RTWN_DEBUG_STATE	= 0x00000010,	/* 802.11 state transitions */
	RTWN_DEBUG_RA		= 0x00000020,	/* f/w rate adaptation setup */
	RTWN_DEBUG_USB		= 0x00000040,	/* usb requests */
	RTWN_DEBUG_FIRMWARE	= 0x00000080,	/* firmware(9) loading debug */
	RTWN_DEBUG_BEACON	= 0x00000100,	/* beacon handling */
	RTWN_DEBUG_INTR		= 0x00000200,	/* ISR */
	RTWN_DEBUG_TEMP		= 0x00000400,	/* temperature calibration */
	RTWN_DEBUG_ROM		= 0x00000800,	/* various ROM info */
	RTWN_DEBUG_KEY		= 0x00001000,	/* crypto keys management */
	RTWN_DEBUG_TXPWR	= 0x00002000,	/* dump Tx power values */
	RTWN_DEBUG_RSSI		= 0x00004000,	/* dump RSSI lookups */
	RTWN_DEBUG_RESET	= 0x00008000,	/* initialization progress */
	RTWN_DEBUG_CALIB	= 0x00010000,	/* calibration progress */
	RTWN_DEBUG_RADAR	= 0x00020000,	/* radar detection status */
	RTWN_DEBUG_ANY		= 0xffffffff
};

#define RTWN_DPRINTF(_sc, _m, ...) do {			\
	if ((_sc)->sc_debug & (_m))				\
		device_printf((_sc)->sc_dev, __VA_ARGS__);	\
} while(0)

#else
#define RTWN_DPRINTF(_sc, _m, ...)	do { (void) _sc; } while (0)
#endif

#endif	/* IF_RTWN_DEBUG_H */

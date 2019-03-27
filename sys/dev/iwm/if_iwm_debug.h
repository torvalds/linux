/*-
 * Copyright (c) 2015 Adrian Chadd <adrian@FreeBSD.org>
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

#ifndef	__IF_IWM_DEBUG_H__
#define	__IF_IWM_DEBUG_H__

#ifdef	IWM_DEBUG
enum {
	IWM_DEBUG_XMIT		= 0x00000001,	/* basic xmit operation */
	IWM_DEBUG_RECV		= 0x00000002,	/* basic recv operation */
	IWM_DEBUG_STATE		= 0x00000004,	/* 802.11 state transitions */
	IWM_DEBUG_TXPOW		= 0x00000008,	/* tx power processing */
	IWM_DEBUG_RESET		= 0x00000010,	/* reset processing */
	IWM_DEBUG_OPS		= 0x00000020,	/* iwm_ops processing */
	IWM_DEBUG_BEACON 	= 0x00000040,	/* beacon handling */
	IWM_DEBUG_WATCHDOG 	= 0x00000080,	/* watchdog timeout */
	IWM_DEBUG_INTR		= 0x00000100,	/* ISR */
	IWM_DEBUG_CALIBRATE	= 0x00000200,	/* periodic calibration */
	IWM_DEBUG_NODE		= 0x00000400,	/* node management */
	IWM_DEBUG_LED		= 0x00000800,	/* led management */
	IWM_DEBUG_CMD		= 0x00001000,	/* cmd submission */
	IWM_DEBUG_TXRATE	= 0x00002000,	/* TX rate debugging */
	IWM_DEBUG_PWRSAVE	= 0x00004000,	/* Power save operations */
	IWM_DEBUG_SCAN		= 0x00008000,	/* Scan related operations */
	IWM_DEBUG_STATS		= 0x00010000,	/* Statistics updates */
	IWM_DEBUG_FIRMWARE_TLV	= 0x00020000,	/* Firmware TLV parsing */
	IWM_DEBUG_TRANS		= 0x00040000,	/* Transport layer (eg PCIe) */
	IWM_DEBUG_EEPROM	= 0x00080000,	/* EEPROM/channel information */
	IWM_DEBUG_TEMP		= 0x00100000,	/* Thermal Sensor handling */
	IWM_DEBUG_FW		= 0x00200000,	/* Firmware management */
	IWM_DEBUG_LAR		= 0x00400000,	/* Location Aware Regulatory */
	IWM_DEBUG_TE		= 0x00800000,	/* Time Event handling */
	IWM_DEBUG_REGISTER	= 0x20000000,	/* print chipset register */
	IWM_DEBUG_TRACE		= 0x40000000,	/* Print begin and start driver function */
	IWM_DEBUG_FATAL		= 0x80000000,	/* fatal errors */
	IWM_DEBUG_ANY		= 0xffffffff
};

#define IWM_DPRINTF(sc, m, fmt, ...) do {			\
	if (sc->sc_debug & (m))				\
		device_printf(sc->sc_dev, fmt, ##__VA_ARGS__);	\
} while (0)
#else
#define IWM_DPRINTF(sc, m, fmt, ...) do { (void) sc; } while (0)
#endif

#endif	/* __IF_IWM_DEBUG_H__ */

/*-
 * Copyright (c) 2009-2010 Weongyo Jeong <weongyo@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

#ifndef	__IF_BWN_DEBUG_H__
#define	__IF_BWN_DEBUG_H__

enum {
	BWN_DEBUG_XMIT		= 0x00000001,	/* basic xmit operation */
	BWN_DEBUG_RECV		= 0x00000002,	/* basic recv operation */
	BWN_DEBUG_STATE		= 0x00000004,	/* 802.11 state transitions */
	BWN_DEBUG_TXPOW		= 0x00000008,	/* tx power processing */
	BWN_DEBUG_RESET		= 0x00000010,	/* reset processing */
	BWN_DEBUG_OPS		= 0x00000020,	/* bwn_ops processing */
	BWN_DEBUG_BEACON	= 0x00000040,	/* beacon handling */
	BWN_DEBUG_WATCHDOG	= 0x00000080,	/* watchdog timeout */
	BWN_DEBUG_INTR		= 0x00000100,	/* ISR */
	BWN_DEBUG_CALIBRATE	= 0x00000200,	/* periodic calibration */
	BWN_DEBUG_NODE		= 0x00000400,	/* node management */
	BWN_DEBUG_LED		= 0x00000800,	/* led management */
	BWN_DEBUG_CMD		= 0x00001000,	/* cmd submission */
	BWN_DEBUG_LO		= 0x00002000,	/* LO */
	BWN_DEBUG_FW		= 0x00004000,	/* firmware */
	BWN_DEBUG_WME		= 0x00008000,	/* WME */
	BWN_DEBUG_RF		= 0x00010000,	/* RF */
	BWN_DEBUG_XMIT_POWER	= 0x00020000,
	BWN_DEBUG_PHY		= 0x00040000,
	BWN_DEBUG_EEPROM	= 0x00080000,
	BWN_DEBUG_HWCRYPTO	= 0x00100000,	/* HW crypto */
	BWN_DEBUG_FATAL		= 0x80000000,	/* fatal errors */
	BWN_DEBUG_ANY		= 0xffffffff
};

#ifdef	BWN_DEBUG
#define DPRINTF(sc, m, fmt, ...) do {			\
	if (sc->sc_debug & (m))				\
		printf(fmt, __VA_ARGS__);		\
} while (0)
#else	/* BWN_DEBUG */
#define DPRINTF(sc, m, fmt, ...) do { (void) sc; } while (0)
#endif	/* BWN_DEBUG */

#define	BWN_ERRPRINTF(sc, ...) do {		\
		printf(__VA_ARGS__);		\
} while (0)
#define	BWN_DBGPRINTF(sc, ...) do {		\
		printf(__VA_ARGS__);		\
} while (0)
#define	BWN_WARNPRINTF(sc, ...) do {		\
		printf(__VA_ARGS__);		\
} while (0)

#endif	/* __IF_BWN_DEBUG_H__ */

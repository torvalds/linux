/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
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
#ifndef	__IF_ATH_DEBUG_H__
#define	__IF_ATH_DEBUG_H__

#ifdef	ATH_DEBUG

enum { 
	ATH_DEBUG_XMIT		= 0x000000001ULL,	/* basic xmit operation */
	ATH_DEBUG_XMIT_DESC	= 0x000000002ULL,	/* xmit descriptors */
	ATH_DEBUG_RECV		= 0x000000004ULL,	/* basic recv operation */
	ATH_DEBUG_RECV_DESC	= 0x000000008ULL,	/* recv descriptors */
	ATH_DEBUG_RATE		= 0x000000010ULL,	/* rate control */
	ATH_DEBUG_RESET		= 0x000000020ULL,	/* reset processing */
	ATH_DEBUG_MODE		= 0x000000040ULL,	/* mode init/setup */
	ATH_DEBUG_BEACON	= 0x000000080ULL,	/* beacon handling */
	ATH_DEBUG_WATCHDOG	= 0x000000100ULL,	/* watchdog timeout */
	ATH_DEBUG_INTR		= 0x000001000ULL,	/* ISR */
	ATH_DEBUG_TX_PROC	= 0x000002000ULL,	/* tx ISR proc */
	ATH_DEBUG_RX_PROC	= 0x000004000ULL,	/* rx ISR proc */
	ATH_DEBUG_BEACON_PROC	= 0x000008000ULL,	/* beacon ISR proc */
	ATH_DEBUG_CALIBRATE	= 0x000010000ULL,	/* periodic calibration */
	ATH_DEBUG_KEYCACHE	= 0x000020000ULL,	/* key cache management */
	ATH_DEBUG_STATE		= 0x000040000ULL,	/* 802.11 state transitions */
	ATH_DEBUG_NODE		= 0x000080000ULL,	/* node management */
	ATH_DEBUG_LED		= 0x000100000ULL,	/* led management */
	ATH_DEBUG_FF		= 0x000200000ULL,	/* fast frames */
	ATH_DEBUG_DFS		= 0x000400000ULL,	/* DFS processing */
	ATH_DEBUG_TDMA		= 0x000800000ULL,	/* TDMA processing */
	ATH_DEBUG_TDMA_TIMER	= 0x001000000ULL,	/* TDMA timer processing */
	ATH_DEBUG_REGDOMAIN	= 0x002000000ULL,	/* regulatory processing */
	ATH_DEBUG_SW_TX		= 0x004000000ULL,	/* per-packet software TX */
	ATH_DEBUG_SW_TX_BAW	= 0x008000000ULL,	/* BAW handling */
	ATH_DEBUG_SW_TX_CTRL	= 0x010000000ULL,	/* queue control */
	ATH_DEBUG_SW_TX_AGGR	= 0x020000000ULL,	/* aggregate TX */
	ATH_DEBUG_SW_TX_RETRIES	= 0x040000000ULL,	/* software TX retries */
	ATH_DEBUG_FATAL		= 0x080000000ULL,	/* fatal errors */
	ATH_DEBUG_SW_TX_BAR	= 0x100000000ULL,	/* BAR TX */
	ATH_DEBUG_EDMA_RX	= 0x200000000ULL,	/* RX EDMA state */
	ATH_DEBUG_SW_TX_FILT	= 0x400000000ULL,	/* SW TX FF */
	ATH_DEBUG_NODE_PWRSAVE	= 0x800000000ULL,	/* node powersave */
	ATH_DEBUG_DIVERSITY	= 0x1000000000ULL,	/* Diversity logic */
	ATH_DEBUG_PWRSAVE	= 0x2000000000ULL,
	ATH_DEBUG_BTCOEX	= 0x4000000000ULL,	/* BT Coex */
	ATH_DEBUG_QUIETIE	= 0x8000000000ULL,	/* Quiet time handling */

	ATH_DEBUG_ANY		= 0xffffffffffffffffULL
};

enum {
	ATH_KTR_RXPROC		= 0x00000001,
	ATH_KTR_TXPROC		= 0x00000002,
	ATH_KTR_TXCOMP		= 0x00000004,
	ATH_KTR_SWQ		= 0x00000008,
	ATH_KTR_INTERRUPTS	= 0x00000010,
	ATH_KTR_ERROR		= 0x00000020,
	ATH_KTR_NODE		= 0x00000040,
	ATH_KTR_TX		= 0x00000080,
};

#define	ATH_KTR(_sc, _km, _kf, ...)	do {	\
	if (sc->sc_ktrdebug & (_km))		\
		CTR##_kf(KTR_DEV, __VA_ARGS__);	\
	} while (0)

extern uint64_t ath_debug;

#define	IFF_DUMPPKTS(sc, m)	(sc->sc_debug & (m))
#define	DPRINTF(sc, m, ...) do {				\
	if (sc->sc_debug & (m))					\
		device_printf(sc->sc_dev, __VA_ARGS__);		\
} while (0)
#define	KEYPRINTF(sc, ix, hk, mac) do {				\
	if (sc->sc_debug & ATH_DEBUG_KEYCACHE)			\
		ath_keyprint(sc, __func__, ix, hk, mac);	\
} while (0)

extern	void ath_printrxbuf(struct ath_softc *, const struct ath_buf *bf,
	u_int ix, int);
extern	void ath_printtxbuf(struct ath_softc *, const struct ath_buf *bf,
	u_int qnum, u_int ix, int done);
extern	void ath_printtxstatbuf(struct ath_softc *sc, const struct ath_buf *bf,
	const uint32_t *ds, u_int qnum, u_int ix, int done);
#else	/* ATH_DEBUG */
#define	ATH_KTR(_sc, _km, _kf, ...)	do { } while (0)

#define	IFF_DUMPPKTS(sc, m)	(0)
#define	DPRINTF(sc, m, fmt, ...) do {				\
	(void) sc;						\
} while (0)
#define	KEYPRINTF(sc, k, ix, mac) do {				\
	(void) sc;						\
} while (0)
#endif	/* ATH_DEBUG */

#endif

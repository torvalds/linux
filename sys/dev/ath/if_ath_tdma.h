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
#ifndef	__IF_ATH_TDMA_H__
#define	__IF_ATH_TDMA_H__

#define	TDMA_EP_MULTIPLIER	(1<<10) /* pow2 to optimize out * and / */
#define	TDMA_LPF_LEN		6
#define	TDMA_DUMMY_MARKER	0x127
#define	TDMA_EP_MUL(x, mul)	((x) * (mul))
#define	TDMA_IN(x)		(TDMA_EP_MUL((x), TDMA_EP_MULTIPLIER))
#define	TDMA_LPF(x, y, len)					\
    ((x != TDMA_DUMMY_MARKER) ? (((x) * ((len)-1) + (y)) / (len)) : (y))
#define	TDMA_SAMPLE(x, y)	do {				\
        x = TDMA_LPF((x), TDMA_IN(y), TDMA_LPF_LEN);		\
} while (0)
#define	TDMA_EP_RND(x,mul) \
	    ((((x)%(mul)) >= ((mul)/2)) ?			\
	     ((x) + ((mul) - 1)) / (mul) : (x)/(mul))
#define	TDMA_AVG(x)		TDMA_EP_RND(x, TDMA_EP_MULTIPLIER)

extern	void ath_tdma_config(struct ath_softc *sc, struct ieee80211vap *vap);
extern	void ath_tdma_update(struct ieee80211_node *ni,
	    const struct ieee80211_tdma_param *tdma, int changed);
extern	void ath_tdma_beacon_send(struct ath_softc *sc,
	    struct ieee80211vap *vap);

#endif

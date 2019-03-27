/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Adrian Chadd, Xenion Pty Ltd.
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
#ifndef	__IF_ATH_TX_HT_H__
#define	__IF_ATH_TX_HT_H__

enum {
	MCS_HT20,
	MCS_HT20_SGI,
	MCS_HT40,
	MCS_HT40_SGI,
};

typedef enum {
	ATH_AGGR_DONE,
	ATH_AGGR_BAW_CLOSED,
	ATH_AGGR_LIMITED,
	ATH_AGGR_SHORTPKT,
	ATH_AGGR_8K_LIMITED,
	ATH_AGGR_ERROR,
	ATH_AGGR_NONAGGR,
	ATH_AGGR_LEAK_CLOSED,
} ATH_AGGR_STATUS;

extern int	ath_max_4ms_framelen[4][32];

extern void ath_tx_rate_fill_rcflags(struct ath_softc *sc, struct ath_buf *bf);

extern void	ath_buf_set_rate(struct ath_softc *sc,
		struct ieee80211_node *ni, struct ath_buf *bf);

extern ATH_AGGR_STATUS
	    ath_tx_form_aggr(struct ath_softc *sc, struct ath_node *an,
	    struct ath_tid *tid, ath_bufhead *bf_q);

#endif

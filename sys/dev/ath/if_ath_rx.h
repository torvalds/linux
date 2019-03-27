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
#ifndef	__IF_ATH_RX_H__
#define	__IF_ATH_RX_H__

extern	u_int32_t ath_calcrxfilter(struct ath_softc *sc);
extern	void ath_recv_mgmt(struct ieee80211_node *ni, struct mbuf *m,
	    int subtype, const struct ieee80211_rx_stats *rxs,
	    int rssi, int nf);

#define	ath_stoprecv(_sc, _dodelay)		\
	    (_sc)->sc_rx.recv_stop((_sc), (_dodelay))
#define	ath_startrecv(_sc)			\
	    (_sc)->sc_rx.recv_start((_sc))
#define	ath_rx_flush(_sc)			\
	    (_sc)->sc_rx.recv_flush((_sc))
#define	ath_rxbuf_init(_sc, _bf)		\
	    (_sc)->sc_rx.recv_rxbuf_init((_sc), (_bf))
#define	ath_rxdma_setup(_sc)			\
	    (_sc)->sc_rx.recv_setup(_sc)
#define	ath_rxdma_teardown(_sc)			\
	    (_sc)->sc_rx.recv_teardown(_sc)

#if 0
extern	int ath_rxbuf_init(struct ath_softc *sc, struct ath_buf *bf);
extern	void ath_rx_tasklet(void *arg, int npending);
extern	void ath_rx_proc(struct ath_softc *sc, int resched);
extern	void ath_stoprecv(struct ath_softc *sc, int dodelay);
extern	int ath_startrecv(struct ath_softc *sc);
#endif

extern	int ath_rx_pkt(struct ath_softc *sc, struct ath_rx_status *rs,
	    HAL_STATUS status, uint64_t tsf, int nf, HAL_RX_QUEUE qtype,
	    struct ath_buf *bf, struct mbuf *m);

extern	void ath_recv_setup_legacy(struct ath_softc *sc);

#endif

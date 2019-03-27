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
#ifndef	__IF_ATH_MISC_H__
#define	__IF_ATH_MISC_H__

/*
 * This is where definitions for "public things" in if_ath.c
 * will go for the time being.
 *
 * Anything in here should eventually be moved out of if_ath.c
 * and into something else.
 */

extern int ath_rxbuf;
extern int ath_txbuf;
extern int ath_txbuf_mgmt;

extern int ath_tx_findrix(const struct ath_softc *sc, uint8_t rate);

extern struct ath_buf * ath_getbuf(struct ath_softc *sc,
	    ath_buf_type_t btype);
extern struct ath_buf * _ath_getbuf_locked(struct ath_softc *sc,
	    ath_buf_type_t btype);
extern struct ath_buf * ath_buf_clone(struct ath_softc *sc,
	    struct ath_buf *bf);
/* XXX change this to NULL the buffer pointer? */
extern void ath_freebuf(struct ath_softc *sc, struct ath_buf *bf);
extern void ath_returnbuf_head(struct ath_softc *sc, struct ath_buf *bf);
extern void ath_returnbuf_tail(struct ath_softc *sc, struct ath_buf *bf);

extern int ath_reset(struct ath_softc *, ATH_RESET_TYPE);
extern void ath_tx_default_comp(struct ath_softc *sc, struct ath_buf *bf,
	    int fail);
extern void ath_tx_update_ratectrl(struct ath_softc *sc,
	    struct ieee80211_node *ni, struct ath_rc_series *rc,
	    struct ath_tx_status *ts, int frmlen, int nframes, int nbad);

extern	int ath_hal_gethangstate(struct ath_hal *ah, uint32_t mask,
	    uint32_t *hangs);

extern void ath_tx_freebuf(struct ath_softc *sc, struct ath_buf *bf,
    int status);
extern	void ath_txq_freeholdingbuf(struct ath_softc *sc,
	    struct ath_txq *txq);

extern void ath_txqmove(struct ath_txq *dst, struct ath_txq *src);

extern void ath_mode_init(struct ath_softc *sc);

extern void ath_setdefantenna(struct ath_softc *sc, u_int antenna);

extern void ath_setslottime(struct ath_softc *sc);

extern	void ath_legacy_attach_comp_func(struct ath_softc *sc);

extern	void ath_tx_draintxq(struct ath_softc *sc, struct ath_txq *txq);

extern	void ath_legacy_tx_drain(struct ath_softc *sc,
	    ATH_RESET_TYPE reset_type);

extern	void ath_tx_process_buf_completion(struct ath_softc *sc,
	    struct ath_txq *txq, struct ath_tx_status *ts, struct ath_buf *bf);

extern	int ath_stoptxdma(struct ath_softc *sc);

extern	void ath_tx_update_tim(struct ath_softc *sc,
	    struct ieee80211_node *ni, int enable);

/*
 * This is only here so that the RX proc function can call it.
 * It's very likely that the "start TX after RX" call should be
 * done via something in if_ath.c, moving "rx tasklet" into
 * if_ath.c and do the ath_start() call there.  Once that's done,
 * we can kill this.
 */
extern void ath_start(struct ifnet *ifp);
extern	void ath_start_task(void *arg, int npending);

extern void ath_tx_dump(struct ath_softc *sc, struct ath_txq *txq);

/*
 * Power state tracking.
 */
extern	void _ath_power_setpower(struct ath_softc *sc, int power_state,
	    int selfgen, const char *file, int line);
extern	void _ath_power_set_selfgen(struct ath_softc *sc,
	    int power_state, const char *file, int line);
extern	void _ath_power_set_power_state(struct ath_softc *sc,
	    int power_state, const char *file, int line);
extern	void _ath_power_restore_power_state(struct ath_softc *sc,
	    const char *file, int line);

#define	ath_power_setpower(sc, ps, sg) _ath_power_setpower(sc, ps, sg, \
	    __FILE__, __LINE__)
#define	ath_power_setselfgen(sc, ps) _ath_power_set_selfgen(sc, ps, \
	    __FILE__, __LINE__)
#define	ath_power_set_power_state(sc, ps) \
	    _ath_power_set_power_state(sc, ps, __FILE__, __LINE__)
#define	ath_power_restore_power_state(sc) \
	    _ath_power_restore_power_state(sc, __FILE__, __LINE__)

/*
 * Kick the frame TX task.
 */
static inline void
ath_tx_kick(struct ath_softc *sc)
{

	/* XXX NULL for now */
}

/*
 * Kick the software TX queue task.
 */
static inline void
ath_tx_swq_kick(struct ath_softc *sc)
{

	taskqueue_enqueue(sc->sc_tq, &sc->sc_txqtask);
}

#endif

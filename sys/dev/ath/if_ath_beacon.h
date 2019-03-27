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
#ifndef	__IF_ATH_BEACON_H__
#define	__IF_ATH_BEACON_H__

extern	int ath_bstuck_threshold;

extern	int ath_beaconq_setup(struct ath_softc *sc);
extern	int ath_beaconq_config(struct ath_softc *sc);
extern	void ath_beacon_config(struct ath_softc *sc,
	    struct ieee80211vap *vap);
extern	struct ath_buf * ath_beacon_generate(struct ath_softc *sc,
	    struct ieee80211vap *vap);
extern	void ath_beacon_cabq_start(struct ath_softc *sc);
extern	int ath_wme_update(struct ieee80211com *ic);
extern	void ath_beacon_update(struct ieee80211vap *vap, int item);
extern	void ath_beacon_start_adhoc(struct ath_softc *sc,
	    struct ieee80211vap *vap);
extern	int ath_beacon_alloc(struct ath_softc *sc, struct ieee80211_node *ni);
extern	void ath_beacon_return(struct ath_softc *sc, struct ath_buf *bf);
extern	void ath_beacon_free(struct ath_softc *sc);
extern	void ath_beacon_proc(void *arg, int pending);
extern	void ath_beacon_miss(struct ath_softc *sc);

#endif


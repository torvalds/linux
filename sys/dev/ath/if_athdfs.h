/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Adrian Chadd, Xenion Pty Ltd
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
#ifndef	__IF_ATHDFS_H__
#define	__IF_ATHDFS_H__

extern	int ath_dfs_attach(struct ath_softc *sc);
extern	int ath_dfs_detach(struct ath_softc *sc);
extern	int ath_dfs_radar_enable(struct ath_softc *,
    struct ieee80211_channel *chan);
extern	int ath_dfs_radar_disable(struct ath_softc *sc);
extern	void ath_dfs_process_phy_err(struct ath_softc *sc, struct mbuf *m,
    uint64_t tsf, struct ath_rx_status *rxstat);
extern	int ath_dfs_process_radar_event(struct ath_softc *sc,
    struct ieee80211_channel *chan);
extern	int ath_dfs_tasklet_needed(struct ath_softc *sc,
    struct ieee80211_channel *chan);
extern	int ath_ioctl_phyerr(struct ath_softc *sc, struct ath_diag *ad);
extern	int ath_dfs_get_thresholds(struct ath_softc *sc,
    HAL_PHYERR_PARAM *param);

#endif	/* __IF_ATHDFS_H__ */

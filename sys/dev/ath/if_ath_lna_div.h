/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Adrian Chadd <adrian@FreeBSD.org>
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
#ifndef	__IF_ATH_LNA_DIV_H__
#define	__IF_ATH_LNA_DIV_H__

#define	ATH_ANT_RX_CURRENT_SHIFT		4
#define	ATH_ANT_RX_MAIN_SHIFT			2
#define	ATH_ANT_RX_MASK				0x3

#define	ATH_ANT_DIV_COMB_SHORT_SCAN_INTR	50
#define	ATH_ANT_DIV_COMB_SHORT_SCAN_PKTCOUNT	0x100
#define	ATH_ANT_DIV_COMB_MAX_PKTCOUNT		0x200
#define	ATH_ANT_DIV_COMB_INIT_COUNT		95
#define	ATH_ANT_DIV_COMB_MAX_COUNT		100
#define	ATH_ANT_DIV_COMB_ALT_ANT_RATIO		30
#define	ATH_ANT_DIV_COMB_ALT_ANT_RATIO2		20

#define	ATH_ANT_DIV_COMB_LNA1_LNA2_SWITCH_DELTA	-1
#define	ATH_ANT_DIV_COMB_LNA1_DELTA_HI		-4
#define	ATH_ANT_DIV_COMB_LNA1_DELTA_MID		-2
#define	ATH_ANT_DIV_COMB_LNA1_DELTA_LOW		2

struct if_ath_ant_comb_state {
	uint16_t count;
	uint16_t total_pkt_count;
	HAL_BOOL scan;
	HAL_BOOL scan_not_start;
	int main_total_rssi;
	int alt_total_rssi;
	int alt_recv_cnt;
	int main_recv_cnt;
	int rssi_lna1;
	int rssi_lna2;
	int rssi_add;
	int rssi_sub;
	int rssi_first;
	int rssi_second;
	int rssi_third;
	HAL_BOOL alt_good;
	int quick_scan_cnt;
	int main_conf;
	HAL_ANT_DIV_COMB_LNA_CONF first_quick_scan_conf;
	HAL_ANT_DIV_COMB_LNA_CONF second_quick_scan_conf;
	int first_bias;
	int second_bias;
	HAL_BOOL first_ratio;
	HAL_BOOL second_ratio;
	unsigned long scan_start_time;
	int lna1_lna2_delta;
};

extern	int ath_lna_div_attach(struct ath_softc *sc);
extern	int ath_lna_div_detach(struct ath_softc *sc);
extern	int ath_lna_div_ioctl(struct ath_softc *sc, struct ath_diag *ad);
extern	int ath_lna_div_enable(struct ath_softc *sc,
	    const struct ieee80211_channel *ch);

extern	void ath_lna_rx_comb_scan(struct ath_softc *sc,
	    struct ath_rx_status *rs, unsigned long ticks, int hz);

#endif	/* __IF_ATH_LNA_DIV_H__ */

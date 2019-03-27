/*-
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef R12A_VAR_H
#define R12A_VAR_H

#include <dev/rtwn/rtl8812a/r12a_rom_defs.h>

struct r12a_softc {
	uint8_t			chip;
#define R12A_CHIP_C_CUT		0x01

	uint8_t			rs_flags;
#define R12A_RXCKSUM_EN		0x01
#define R12A_RXCKSUM6_EN	0x02
#define R12A_IQK_RUNNING	0x04
#define R12A_RADAR_ENABLED	0x08

	int			rs_radar;
	struct timeout_task	rs_chan_check;

	/* ROM variables */
	int			ext_pa_2g:1,
				ext_pa_5g:1,
				ext_lna_2g:1,
				ext_lna_5g:1,
				type_pa_2g:4,
				type_pa_5g:4,
				type_lna_2g:4,
				type_lna_5g:4,
				bt_coex:1,
				bt_ant_num:1;

	uint8_t			board_type;
	uint8_t			regulatory;
	uint8_t			crystalcap;

	uint8_t			rfe_type;
	uint8_t			tx_bbswing_2g;
	uint8_t			tx_bbswing_5g;

	uint8_t	cck_tx_pwr[R12A_MAX_RF_PATH][R12A_GROUP_2G];
	uint8_t	ht40_tx_pwr_2g[R12A_MAX_RF_PATH][R12A_GROUP_2G];
	uint8_t	ht40_tx_pwr_5g[R12A_MAX_RF_PATH][R12A_GROUP_5G];

	int8_t	cck_tx_pwr_diff_2g[R12A_MAX_RF_PATH][R12A_MAX_TX_COUNT];
	int8_t	ofdm_tx_pwr_diff_2g[R12A_MAX_RF_PATH][R12A_MAX_TX_COUNT];
	int8_t	bw20_tx_pwr_diff_2g[R12A_MAX_RF_PATH][R12A_MAX_TX_COUNT];
	int8_t	bw40_tx_pwr_diff_2g[R12A_MAX_RF_PATH][R12A_MAX_TX_COUNT];

	int8_t	ofdm_tx_pwr_diff_5g[R12A_MAX_RF_PATH][R12A_MAX_TX_COUNT];
	int8_t	bw20_tx_pwr_diff_5g[R12A_MAX_RF_PATH][R12A_MAX_TX_COUNT];
	int8_t	bw40_tx_pwr_diff_5g[R12A_MAX_RF_PATH][R12A_MAX_TX_COUNT];
	int8_t	bw80_tx_pwr_diff_5g[R12A_MAX_RF_PATH][R12A_MAX_TX_COUNT];
	int8_t	bw160_tx_pwr_diff_5g[R12A_MAX_RF_PATH][R12A_MAX_TX_COUNT];

	int		sc_ant;

	int		(*rs_newstate[RTWN_PORT_COUNT])(struct ieee80211vap *,
			    enum ieee80211_state, int);
	void		(*rs_scan_start)(struct ieee80211com *);
	void		(*rs_scan_end)(struct ieee80211com *);

	void		(*rs_crystalcap_write)(struct rtwn_softc *);
	void		(*rs_fix_spur)(struct rtwn_softc *,
			    struct ieee80211_channel *);
	void		(*rs_set_band_2ghz)(struct rtwn_softc *, uint32_t);
	void		(*rs_set_band_5ghz)(struct rtwn_softc *, uint32_t);
	void		(*rs_init_burstlen)(struct rtwn_softc *);
	void		(*rs_init_ampdu_fwhw)(struct rtwn_softc *);
#ifndef RTWN_WITHOUT_UCODE
	int		(*rs_iq_calib_fw_supported)(struct rtwn_softc *);
#endif
	void		(*rs_iq_calib_sw)(struct rtwn_softc *);

	int		ac_usb_dma_size;
	int		ac_usb_dma_time;
	int		ampdu_max_time;
};
#define	R12A_SOFTC(_sc)	((struct r12a_softc *)((_sc)->sc_priv))

#define rtwn_r12a_fix_spur(_sc, _c) \
	((R12A_SOFTC(_sc)->rs_fix_spur)((_sc), (_c)))
#define rtwn_r12a_set_band_2ghz(_sc, _rates) \
	((R12A_SOFTC(_sc)->rs_set_band_2ghz)((_sc), (_rates)))
#define rtwn_r12a_set_band_5ghz(_sc, _rates) \
	((R12A_SOFTC(_sc)->rs_set_band_5ghz)((_sc), (_rates)))
#define rtwn_r12a_init_burstlen(_sc) \
	((R12A_SOFTC(_sc)->rs_init_burstlen)((_sc)))
#define rtwn_r12a_init_ampdu_fwhw(_sc) \
	((R12A_SOFTC(_sc)->rs_init_ampdu_fwhw)((_sc)))
#define rtwn_r12a_crystalcap_write(_sc) \
	((R12A_SOFTC(_sc)->rs_crystalcap_write)((_sc)))
#ifndef RTWN_WITHOUT_UCODE
#define rtwn_r12a_iq_calib_fw_supported(_sc) \
	((R12A_SOFTC(_sc)->rs_iq_calib_fw_supported)((_sc)))
#endif
#define rtwn_r12a_iq_calib_sw(_sc) \
	((R12A_SOFTC(_sc)->rs_iq_calib_sw)((_sc)))

#endif	/* R12A_VAR_H */

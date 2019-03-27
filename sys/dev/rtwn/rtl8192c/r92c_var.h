/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $OpenBSD: if_urtwnreg.h,v 1.3 2010/11/16 18:02:59 damien Exp $
 * $FreeBSD$
 */

#ifndef R92C_VAR_H
#define R92C_VAR_H

#include <dev/rtwn/rtl8192c/r92c_rom_defs.h>

struct r92c_softc {
	uint8_t		rs_flags;
#define R92C_FLAG_ASSOCIATED	0x01

	uint8_t		chip;
#define R92C_CHIP_92C		0x01
#define R92C_CHIP_92C_1T2R	0x02
#define R92C_CHIP_UMC_A_CUT	0x04

#ifndef RTWN_WITHOUT_UCODE
	struct callout	rs_c2h_report;
	int		rs_c2h_timeout;
	int		rs_c2h_pending;
	int		rs_c2h_paused;
#endif
#define R92C_TX_PAUSED_THRESHOLD	20

	void		*rs_txpwr;
	const void	*rs_txagc;

	uint8_t		board_type;
	uint8_t		regulatory;
	uint8_t		crystalcap;
	uint8_t		pa_setting;

	void		(*rs_scan_start)(struct ieee80211com *);
	void		(*rs_scan_end)(struct ieee80211com *);

	void		(*rs_set_bw20)(struct rtwn_softc *, uint8_t);
	void		(*rs_get_txpower)(struct rtwn_softc *, int,
			    struct ieee80211_channel *, uint8_t[]);
	void		(*rs_set_gain)(struct rtwn_softc *, uint8_t);
	void		(*rs_tx_enable_ampdu)(void *, int);
	void		(*rs_tx_setup_hwseq)(void *);
	void		(*rs_tx_setup_macid)(void *, int);
	void		(*rs_set_rom_opts)(struct rtwn_softc *, uint8_t *);

	int		rf_read_delay[3];
	uint32_t	rf_chnlbw[R92C_MAX_CHAINS];
};
#define R92C_SOFTC(_sc)	((struct r92c_softc *)((_sc)->sc_priv))

#define rtwn_r92c_set_bw20(_sc, _chan) \
	((R92C_SOFTC(_sc)->rs_set_bw20)((_sc), (_chan)))
#define rtwn_r92c_get_txpower(_sc, _chain, _c, _power) \
	((R92C_SOFTC(_sc)->rs_get_txpower)((_sc), (_chain), (_c), (_power)))
#define rtwn_r92c_set_gain(_sc, _gain) \
	((R92C_SOFTC(_sc)->rs_set_gain)((_sc), (_gain)))
#define rtwn_r92c_tx_enable_ampdu(_sc, _buf, _enable) \
	((R92C_SOFTC(_sc)->rs_tx_enable_ampdu)((_buf), (_enable)))
#define rtwn_r92c_tx_setup_hwseq(_sc, _buf) \
	((R92C_SOFTC(_sc)->rs_tx_setup_hwseq)((_buf)))
#define rtwn_r92c_tx_setup_macid(_sc, _buf, _id) \
	((R92C_SOFTC(_sc)->rs_tx_setup_macid)((_buf), (_id)))
#define rtwn_r92c_set_rom_opts(_sc, _buf) \
	((R92C_SOFTC(_sc)->rs_set_rom_opts)((_sc), (_buf)))

#endif	/* R92C_VAR_H */

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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/linker.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_debug.h>

#include <dev/rtwn/rtl8812a/r12a.h>
#include <dev/rtwn/rtl8812a/r12a_var.h>
#include <dev/rtwn/rtl8812a/r12a_rom_image.h>


void
r12a_parse_rom_common(struct rtwn_softc *sc, uint8_t *buf)
{
	struct r12a_softc *rs = sc->sc_priv;
	struct r12a_rom *rom = (struct r12a_rom *)buf;
	int i, j, k;

	sc->thermal_meter = rom->thermal_meter;
	rs->crystalcap = RTWN_GET_ROM_VAR(rom->crystalcap,
	    R12A_ROM_CRYSTALCAP_DEF);
	rs->tx_bbswing_2g = RTWN_GET_ROM_VAR(rom->tx_bbswing_2g, 0);
	rs->tx_bbswing_5g = RTWN_GET_ROM_VAR(rom->tx_bbswing_5g, 0);

	for (i = 0; i < sc->ntxchains; i++) {
		struct r12a_tx_pwr_2g *pwr_2g = &rom->tx_pwr[i].pwr_2g;
		struct r12a_tx_pwr_5g *pwr_5g = &rom->tx_pwr[i].pwr_5g;
		struct r12a_tx_pwr_diff_2g *pwr_diff_2g =
		    &rom->tx_pwr[i].pwr_diff_2g;
		struct r12a_tx_pwr_diff_5g *pwr_diff_5g =
		    &rom->tx_pwr[i].pwr_diff_5g;

		for (j = 0; j < R12A_GROUP_2G - 1; j++) {
			rs->cck_tx_pwr[i][j] =
			    RTWN_GET_ROM_VAR(pwr_2g->cck[j],
				R12A_DEF_TX_PWR_2G);
			rs->ht40_tx_pwr_2g[i][j] =
			    RTWN_GET_ROM_VAR(pwr_2g->ht40[j],
				R12A_DEF_TX_PWR_2G);
		}
		rs->cck_tx_pwr[i][j] = RTWN_GET_ROM_VAR(pwr_2g->cck[j],
		    R12A_DEF_TX_PWR_2G);

		rs->cck_tx_pwr_diff_2g[i][0] = 0;
		rs->ofdm_tx_pwr_diff_2g[i][0] = RTWN_SIGN4TO8(
		    MS(pwr_diff_2g->ht20_ofdm, LOW_PART));
		rs->bw20_tx_pwr_diff_2g[i][0] = RTWN_SIGN4TO8(
		    MS(pwr_diff_2g->ht20_ofdm, HIGH_PART));
		rs->bw40_tx_pwr_diff_2g[i][0] = 0;

		for (j = 1, k = 0; k < nitems(pwr_diff_2g->diff123); j++, k++) {
			rs->cck_tx_pwr_diff_2g[i][j] = RTWN_SIGN4TO8(
			    MS(pwr_diff_2g->diff123[k].ofdm_cck, LOW_PART));
			rs->ofdm_tx_pwr_diff_2g[i][j] = RTWN_SIGN4TO8(
			    MS(pwr_diff_2g->diff123[k].ofdm_cck, HIGH_PART));
			rs->bw20_tx_pwr_diff_2g[i][j] = RTWN_SIGN4TO8(
			    MS(pwr_diff_2g->diff123[k].ht40_ht20, LOW_PART));
			rs->bw40_tx_pwr_diff_2g[i][j] = RTWN_SIGN4TO8(
			    MS(pwr_diff_2g->diff123[k].ht40_ht20, HIGH_PART));
		}

		for (j = 0; j < R12A_GROUP_5G; j++) {
			rs->ht40_tx_pwr_5g[i][j] =
			    RTWN_GET_ROM_VAR(pwr_5g->ht40[j],
				R12A_DEF_TX_PWR_5G);
		}

		rs->ofdm_tx_pwr_diff_5g[i][0] = RTWN_SIGN4TO8(
		    MS(pwr_diff_5g->ht20_ofdm, LOW_PART));
		rs->ofdm_tx_pwr_diff_5g[i][1] = RTWN_SIGN4TO8(
		    MS(pwr_diff_5g->ofdm_ofdm[0], HIGH_PART));
		rs->ofdm_tx_pwr_diff_5g[i][2] = RTWN_SIGN4TO8(
		    MS(pwr_diff_5g->ofdm_ofdm[0], LOW_PART));
		rs->ofdm_tx_pwr_diff_5g[i][3] = RTWN_SIGN4TO8(
		    MS(pwr_diff_5g->ofdm_ofdm[1], LOW_PART));

		rs->bw20_tx_pwr_diff_5g[i][0] = RTWN_SIGN4TO8(
		    MS(pwr_diff_5g->ht20_ofdm, HIGH_PART));
		rs->bw40_tx_pwr_diff_5g[i][0] = 0;
		for (j = 1, k = 0; k < nitems(pwr_diff_5g->ht40_ht20);
		    j++, k++) {
			rs->bw20_tx_pwr_diff_5g[i][j] = RTWN_SIGN4TO8(
			    MS(pwr_diff_5g->ht40_ht20[k], LOW_PART));
			rs->bw40_tx_pwr_diff_5g[i][j] = RTWN_SIGN4TO8(
			    MS(pwr_diff_5g->ht40_ht20[k], HIGH_PART));
		}

		for (j = 0; j < nitems(pwr_diff_5g->ht80_ht160); j++) {
			rs->bw80_tx_pwr_diff_5g[i][j] = RTWN_SIGN4TO8(
			    MS(pwr_diff_5g->ht80_ht160[j], HIGH_PART));
			rs->bw160_tx_pwr_diff_5g[i][j] = RTWN_SIGN4TO8(
			    MS(pwr_diff_5g->ht80_ht160[j], LOW_PART));
		}
	}

	rs->regulatory = MS(rom->rf_board_opt, R92C_ROM_RF1_REGULATORY);
	rs->board_type =
	    MS(RTWN_GET_ROM_VAR(rom->rf_board_opt, R92C_BOARD_TYPE_DONGLE),
		R92C_ROM_RF1_BOARD_TYPE);
	RTWN_DPRINTF(sc, RTWN_DEBUG_ROM, "%s: regulatory type=%d\n",
	    __func__, rs->regulatory);
}

void
r12a_parse_rom(struct rtwn_softc *sc, uint8_t *buf)
{
	struct r12a_softc *rs = sc->sc_priv;
	struct r12a_rom *rom = (struct r12a_rom *)buf;
	uint8_t pa_type, lna_type_2g, lna_type_5g;

	/* Read PA/LNA types. */
	pa_type = RTWN_GET_ROM_VAR(rom->pa_type, 0);
	lna_type_2g = RTWN_GET_ROM_VAR(rom->lna_type_2g, 0);
	lna_type_5g = RTWN_GET_ROM_VAR(rom->lna_type_5g, 0);

	rs->ext_pa_2g = R12A_ROM_IS_PA_EXT_2GHZ(pa_type);
	rs->ext_pa_5g = R12A_ROM_IS_PA_EXT_5GHZ(pa_type);
	rs->ext_lna_2g = R21A_ROM_IS_LNA_EXT(lna_type_2g);
	rs->ext_lna_5g = R21A_ROM_IS_LNA_EXT(lna_type_5g);
	rs->bt_coex = (MS(rom->rf_board_opt, R92C_ROM_RF1_BOARD_TYPE) ==
	    R92C_BOARD_TYPE_HIGHPA);
	rs->bt_ant_num = (rom->rf_bt_opt & R12A_RF_BT_OPT_ANT_NUM);

	if (rs->ext_pa_2g) {
		rs->type_pa_2g =
		    R12A_GET_ROM_PA_TYPE(lna_type_2g, 0) |
		    (R12A_GET_ROM_PA_TYPE(lna_type_2g, 1) << 2);
	}
	if (rs->ext_pa_5g) {
		rs->type_pa_5g =
		    R12A_GET_ROM_PA_TYPE(lna_type_5g, 0) |
		    (R12A_GET_ROM_PA_TYPE(lna_type_5g, 1) << 2);
	}
	if (rs->ext_lna_2g) {
		rs->type_lna_2g =
		    R12A_GET_ROM_LNA_TYPE(lna_type_2g, 0) |
		    (R12A_GET_ROM_LNA_TYPE(lna_type_2g, 1) << 2);
	}
	if (rs->ext_lna_5g) {
		rs->type_lna_5g =
		    R12A_GET_ROM_LNA_TYPE(lna_type_5g, 0) |
		    (R12A_GET_ROM_LNA_TYPE(lna_type_5g, 1) << 2);
	}

	if (rom->rfe_option & 0x80) {
		if (rs->ext_lna_5g) {
			if (rs->ext_pa_5g) {
				if (rs->ext_pa_2g && rs->ext_lna_2g)
					rs->rfe_type = 3;
				else
					rs->rfe_type = 0;
			} else
				rs->rfe_type = 2;
		} else
			rs->rfe_type = 4;
	} else {
		rs->rfe_type = rom->rfe_option & 0x3f;

		/* workaround for incorrect EFUSE map */
		if (rs->rfe_type == 4 &&
		    rs->ext_pa_2g && rs->ext_lna_2g &&
		    rs->ext_pa_5g && rs->ext_lna_5g)
			rs->rfe_type = 0;
	}

	/* Read MAC address. */
	IEEE80211_ADDR_COPY(sc->sc_ic.ic_macaddr, rom->macaddr_12a);

	/* Execute common part of initialization. */
	r12a_parse_rom_common(sc, buf);
}

/*-
 * Copyright (c) 2017 Kevin Lo <kevlo@FreeBSD.org>
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

#include <dev/rtwn/rtl8192e/r92e.h>
#include <dev/rtwn/rtl8192e/r92e_var.h>
#include <dev/rtwn/rtl8192e/r92e_rom_image.h>

void
r92e_parse_rom(struct rtwn_softc *sc, uint8_t *buf)
{
	struct r92e_softc *rs = sc->sc_priv;
	struct r92e_rom *rom = (struct r92e_rom *)buf;
	uint8_t pwr_diff;
	int i, j, k;

	sc->thermal_meter = rom->thermal_meter;
	rs->crystalcap = RTWN_GET_ROM_VAR(rom->crystalcap,
	    R92E_ROM_CRYSTALCAP_DEF);

	for (i = 0; i < sc->ntxchains; i++) {
		struct r92e_tx_pwr_2g *pwr_2g = &rom->tx_pwr[i].pwr_2g;
		struct r92e_tx_pwr_diff_2g *pwr_diff_2g =
		    &rom->tx_pwr[i].pwr_diff_2g;

		for (j = 0; j < R92E_GROUP_2G - 1; j++) {
			rs->cck_tx_pwr[i][j] =
			    RTWN_GET_ROM_VAR(pwr_2g->cck[j],
				R92E_DEF_TX_PWR_2G);
			rs->ht40_tx_pwr_2g[i][j] =
			    RTWN_GET_ROM_VAR(pwr_2g->ht40[j],
				R92E_DEF_TX_PWR_2G);
		}
		rs->cck_tx_pwr[i][j] = RTWN_GET_ROM_VAR(pwr_2g->cck[j],
		    R92E_DEF_TX_PWR_2G);

		rs->cck_tx_pwr_diff_2g[i][0] = 0;
		rs->ofdm_tx_pwr_diff_2g[i][0] = RTWN_SIGN4TO8(
		    MS(pwr_diff_2g->ht20_ofdm, LOW_PART));
		rs->bw20_tx_pwr_diff_2g[i][0] = RTWN_SIGN4TO8(
		    MS(pwr_diff_2g->ht20_ofdm, HIGH_PART));
		rs->bw40_tx_pwr_diff_2g[i][0] = 0;
		pwr_diff = RTWN_GET_ROM_VAR(pwr_diff_2g->ht20_ofdm,
		    R92E_DEF_TX_PWR_HT20_DIFF);
		if (pwr_diff != R92E_DEF_TX_PWR_HT20_DIFF) {
			rs->ofdm_tx_pwr_diff_2g[i][0] = RTWN_SIGN4TO8(
			    MS(pwr_diff_2g->ht20_ofdm, LOW_PART));
			rs->bw20_tx_pwr_diff_2g[i][0] = RTWN_SIGN4TO8(
			    MS(pwr_diff_2g->ht20_ofdm, HIGH_PART));
		} else {
			rs->ofdm_tx_pwr_diff_2g[i][0] =
			rs->bw20_tx_pwr_diff_2g[i][0] = pwr_diff;
		}

		for (j = 1, k = 0; k < nitems(pwr_diff_2g->diff123); j++, k++) {
			pwr_diff = RTWN_GET_ROM_VAR(
			    pwr_diff_2g->diff123[k].ofdm_cck,
			    R92E_DEF_TX_PWR_DIFF);
			if (pwr_diff != R92E_DEF_TX_PWR_DIFF) {
				rs->cck_tx_pwr_diff_2g[i][j] = RTWN_SIGN4TO8(
				    MS(pwr_diff_2g->diff123[k].ofdm_cck,
				    LOW_PART));
				rs->ofdm_tx_pwr_diff_2g[i][j] = RTWN_SIGN4TO8(
				    MS(pwr_diff_2g->diff123[k].ofdm_cck,
				    HIGH_PART));
			} else {
				rs->cck_tx_pwr_diff_2g[i][j] =
				rs->ofdm_tx_pwr_diff_2g[i][j] = pwr_diff;
			}
			pwr_diff = RTWN_GET_ROM_VAR(
			    pwr_diff_2g->diff123[k].ht40_ht20,
			    R92E_DEF_TX_PWR_DIFF);
			if (pwr_diff != R92E_DEF_TX_PWR_DIFF) {
				rs->bw20_tx_pwr_diff_2g[i][j] = RTWN_SIGN4TO8(
				    MS(pwr_diff_2g->diff123[k].ht40_ht20,
				    LOW_PART));
				rs->bw40_tx_pwr_diff_2g[i][j] = RTWN_SIGN4TO8(
				    MS(pwr_diff_2g->diff123[k].ht40_ht20,
				    HIGH_PART));
			} else {
				rs->bw20_tx_pwr_diff_2g[i][j] =
				rs->bw40_tx_pwr_diff_2g[i][j] = pwr_diff;
			}
		}
	}

	rs->regulatory = MS(rom->rf_board_opt, R92C_ROM_RF1_REGULATORY);

	/* Read MAC address. */
	IEEE80211_ADDR_COPY(sc->sc_ic.ic_macaddr, rom->macaddr);
}

/*	$OpenBSD: if_urtwn.c,v 1.16 2011/02/10 17:26:40 jakemsr Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
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
#include <dev/rtwn/if_rtwn_ridx.h>

#include <dev/rtwn/rtl8192c/r92c_var.h>

#include <dev/rtwn/rtl8188e/r88e.h>
#include <dev/rtwn/rtl8188e/r88e_priv.h>
#include <dev/rtwn/rtl8188e/r88e_reg.h>


static int
r88e_get_power_group(struct rtwn_softc *sc, struct ieee80211_channel *c)
{
	uint8_t chan;
	int group;

	chan = rtwn_chan2centieee(c);
	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		if (chan <= 2)			group = 0;
		else if (chan <= 5)		group = 1;
		else if (chan <= 8)		group = 2;
		else if (chan <= 11)		group = 3;
		else if (chan <= 13)		group = 4;
		else if (chan <= 14)		group = 5;
		else {
			KASSERT(0, ("wrong 2GHz channel %d!\n", chan));
			return (-1);
		}
	} else {
		KASSERT(0, ("wrong channel band (flags %08X)\n", c->ic_flags));
		return (-1);
	}

	return (group);
}

void
r88e_get_txpower(struct rtwn_softc *sc, int chain,
    struct ieee80211_channel *c, uint8_t power[RTWN_RIDX_COUNT])
{
	struct r92c_softc *rs = sc->sc_priv;
	const struct rtwn_r88e_txpwr *rt = rs->rs_txpwr;
	uint8_t cckpow, ofdmpow, bw20pow, htpow = 0;
	int max_mcs, ridx, group;

	/* Determine channel group. */
	group = r88e_get_power_group(sc, c);
	if (group == -1) {	/* shouldn't happen */
		device_printf(sc->sc_dev, "%s: incorrect channel\n", __func__);
		return;
	}

	/* XXX net80211 regulatory */

	max_mcs = RTWN_RIDX_HT_MCS(sc->ntxchains * 8 - 1);
	KASSERT(max_mcs <= RTWN_RIDX_COUNT, ("increase ridx limit\n"));

	/* Compute per-CCK rate Tx power. */
	cckpow = rt->cck_tx_pwr[group];
	for (ridx = RTWN_RIDX_CCK1; ridx <= RTWN_RIDX_CCK11; ridx++) {
		power[ridx] = (ridx == RTWN_RIDX_CCK2) ? cckpow - 9 : cckpow;
	}

	if (group < 5)
		htpow = rt->ht40_tx_pwr[group];

	/* Compute per-OFDM rate Tx power. */
	ofdmpow = htpow + rt->ofdm_tx_pwr_diff;
	for (ridx = RTWN_RIDX_OFDM6; ridx <= RTWN_RIDX_OFDM54; ridx++)
		power[ridx] = ofdmpow;

	bw20pow = htpow + rt->bw20_tx_pwr_diff;
	for (ridx = RTWN_RIDX_HT_MCS(0); ridx <= max_mcs; ridx++)
		power[ridx] = bw20pow;

	/* Apply max limit. */
	for (ridx = RTWN_RIDX_CCK1; ridx <= max_mcs; ridx++) {
		if (power[ridx] > R92C_MAX_TX_PWR)
			power[ridx] = R92C_MAX_TX_PWR;
	}
}

void
r88e_set_bw20(struct rtwn_softc *sc, uint8_t chan)
{
	struct r92c_softc *rs = sc->sc_priv;

	rtwn_setbits_1(sc, R92C_BWOPMODE, 0, R92C_BWOPMODE_20MHZ);

	rtwn_bb_setbits(sc, R92C_FPGA0_RFMOD, R92C_RFMOD_40MHZ, 0);
	rtwn_bb_setbits(sc, R92C_FPGA1_RFMOD, R92C_RFMOD_40MHZ, 0);

	/* Select 20MHz bandwidth. */
	rtwn_rf_write(sc, 0, R92C_RF_CHNLBW,
	    (rs->rf_chnlbw[0] & ~0xfff) | chan | R88E_RF_CHNLBW_BW20);
}

void
r88e_set_gain(struct rtwn_softc *sc, uint8_t gain)
{

	rtwn_bb_setbits(sc, R92C_OFDM0_AGCCORE1(0),
	    R92C_OFDM0_AGCCORE1_GAIN_M, gain);
}

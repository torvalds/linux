/*	$OpenBSD: if_urtwn.c,v 1.16 2011/02/10 17:26:40 jakemsr Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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

#include <dev/rtwn/rtl8192c/r92c.h>
#include <dev/rtwn/rtl8192c/r92c_priv.h>
#include <dev/rtwn/rtl8192c/r92c_reg.h>
#include <dev/rtwn/rtl8192c/r92c_var.h>


static int
r92c_get_power_group(struct rtwn_softc *sc, struct ieee80211_channel *c)
{
	uint8_t chan;
	int group;

	chan = rtwn_chan2centieee(c);
	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		if (chan <= 3)			group = 0;
		else if (chan <= 9)		group = 1;
		else if (chan <= 14)		group = 2;
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

/* XXX recheck */
void
r92c_get_txpower(struct rtwn_softc *sc, int chain,
    struct ieee80211_channel *c, uint8_t power[RTWN_RIDX_COUNT])
{
	struct r92c_softc *rs = sc->sc_priv;
	struct rtwn_r92c_txpwr *rt = rs->rs_txpwr;
	const struct rtwn_r92c_txagc *base = rs->rs_txagc;
	uint8_t ofdmpow, htpow, diff, max;
	int max_mcs, ridx, group;

	/* Determine channel group. */
	group = r92c_get_power_group(sc, c);
	if (group == -1) {	/* shouldn't happen */
		device_printf(sc->sc_dev, "%s: incorrect channel\n", __func__);
		return;
	}

	/* XXX net80211 regulatory */

	max_mcs = RTWN_RIDX_HT_MCS(sc->ntxchains * 8 - 1);
	KASSERT(max_mcs <= RTWN_RIDX_COUNT, ("increase ridx limit\n"));

	if (rs->regulatory == 0) {
		for (ridx = RTWN_RIDX_CCK1; ridx <= RTWN_RIDX_CCK11; ridx++)
			power[ridx] = base[chain].pwr[0][ridx];
	}
	for (ridx = RTWN_RIDX_OFDM6; ridx < RTWN_RIDX_COUNT; ridx++) {
		if (rs->regulatory == 3) {
			power[ridx] = base[chain].pwr[0][ridx];
			/* Apply vendor limits. */
			if (IEEE80211_IS_CHAN_HT40(c))
				max = rt->ht40_max_pwr[chain][group];
			else
				max = rt->ht20_max_pwr[chain][group];
			if (power[ridx] > max)
				power[ridx] = max;
		} else if (rs->regulatory == 1) {
			if (!IEEE80211_IS_CHAN_HT40(c))
				power[ridx] = base[chain].pwr[group][ridx];
		} else if (rs->regulatory != 2)
			power[ridx] = base[chain].pwr[0][ridx];
	}

	/* Compute per-CCK rate Tx power. */
	for (ridx = RTWN_RIDX_CCK1; ridx <= RTWN_RIDX_CCK11; ridx++)
		power[ridx] += rt->cck_tx_pwr[chain][group];

	htpow = rt->ht40_1s_tx_pwr[chain][group];
	if (sc->ntxchains > 1) {
		/* Apply reduction for 2 spatial streams. */
		diff = rt->ht40_2s_tx_pwr_diff[chain][group];
		htpow = (htpow > diff) ? htpow - diff : 0;
	}

	/* Compute per-OFDM rate Tx power. */
	diff = rt->ofdm_tx_pwr_diff[chain][group];
	ofdmpow = htpow + diff;	/* HT->OFDM correction. */
	for (ridx = RTWN_RIDX_OFDM6; ridx <= RTWN_RIDX_OFDM54; ridx++)
		power[ridx] += ofdmpow;

	/* Compute per-MCS Tx power. */
	if (!IEEE80211_IS_CHAN_HT40(c)) {
		diff = rt->ht20_tx_pwr_diff[chain][group];
		htpow += diff;	/* HT40->HT20 correction. */
	}
	for (ridx = RTWN_RIDX_HT_MCS(0); ridx <= max_mcs; ridx++)
		power[ridx] += htpow;

	/* Apply max limit. */
	for (ridx = RTWN_RIDX_CCK1; ridx <= max_mcs; ridx++) {
		if (power[ridx] > R92C_MAX_TX_PWR)
			power[ridx] = R92C_MAX_TX_PWR;
	}
}

void
r92c_write_txpower(struct rtwn_softc *sc, int chain,
    uint8_t power[RTWN_RIDX_COUNT])
{
	uint32_t reg;

	/* Write per-CCK rate Tx power. */
	if (chain == 0) {
		reg = rtwn_bb_read(sc, R92C_TXAGC_A_CCK1_MCS32);
		reg = RW(reg, R92C_TXAGC_A_CCK1,  power[RTWN_RIDX_CCK1]);
		rtwn_bb_write(sc, R92C_TXAGC_A_CCK1_MCS32, reg);
		reg = rtwn_bb_read(sc, R92C_TXAGC_B_CCK11_A_CCK2_11);
		reg = RW(reg, R92C_TXAGC_A_CCK2,  power[RTWN_RIDX_CCK2]);
		reg = RW(reg, R92C_TXAGC_A_CCK55, power[RTWN_RIDX_CCK55]);
		reg = RW(reg, R92C_TXAGC_A_CCK11, power[RTWN_RIDX_CCK11]);
		rtwn_bb_write(sc, R92C_TXAGC_B_CCK11_A_CCK2_11, reg);
	} else {
		reg = rtwn_bb_read(sc, R92C_TXAGC_B_CCK1_55_MCS32);
		reg = RW(reg, R92C_TXAGC_B_CCK1,  power[RTWN_RIDX_CCK1]);
		reg = RW(reg, R92C_TXAGC_B_CCK2,  power[RTWN_RIDX_CCK2]);
		reg = RW(reg, R92C_TXAGC_B_CCK55, power[RTWN_RIDX_CCK55]);
		rtwn_bb_write(sc, R92C_TXAGC_B_CCK1_55_MCS32, reg);
		reg = rtwn_bb_read(sc, R92C_TXAGC_B_CCK11_A_CCK2_11);
		reg = RW(reg, R92C_TXAGC_B_CCK11, power[RTWN_RIDX_CCK11]);
		rtwn_bb_write(sc, R92C_TXAGC_B_CCK11_A_CCK2_11, reg);
	}
	/* Write per-OFDM rate Tx power. */
	rtwn_bb_write(sc, R92C_TXAGC_RATE18_06(chain),
	    SM(R92C_TXAGC_RATE06, power[RTWN_RIDX_OFDM6]) |
	    SM(R92C_TXAGC_RATE09, power[RTWN_RIDX_OFDM9]) |
	    SM(R92C_TXAGC_RATE12, power[RTWN_RIDX_OFDM12]) |
	    SM(R92C_TXAGC_RATE18, power[RTWN_RIDX_OFDM18]));
	rtwn_bb_write(sc, R92C_TXAGC_RATE54_24(chain),
	    SM(R92C_TXAGC_RATE24, power[RTWN_RIDX_OFDM24]) |
	    SM(R92C_TXAGC_RATE36, power[RTWN_RIDX_OFDM36]) |
	    SM(R92C_TXAGC_RATE48, power[RTWN_RIDX_OFDM48]) |
	    SM(R92C_TXAGC_RATE54, power[RTWN_RIDX_OFDM54]));
	/* Write per-MCS Tx power. */
	rtwn_bb_write(sc, R92C_TXAGC_MCS03_MCS00(chain),
	    SM(R92C_TXAGC_MCS00,  power[RTWN_RIDX_HT_MCS(0)]) |
	    SM(R92C_TXAGC_MCS01,  power[RTWN_RIDX_HT_MCS(1)]) |
	    SM(R92C_TXAGC_MCS02,  power[RTWN_RIDX_HT_MCS(2)]) |
	    SM(R92C_TXAGC_MCS03,  power[RTWN_RIDX_HT_MCS(3)]));
	rtwn_bb_write(sc, R92C_TXAGC_MCS07_MCS04(chain),
	    SM(R92C_TXAGC_MCS04,  power[RTWN_RIDX_HT_MCS(4)]) |
	    SM(R92C_TXAGC_MCS05,  power[RTWN_RIDX_HT_MCS(5)]) |
	    SM(R92C_TXAGC_MCS06,  power[RTWN_RIDX_HT_MCS(6)]) |
	    SM(R92C_TXAGC_MCS07,  power[RTWN_RIDX_HT_MCS(7)]));
	if (sc->ntxchains >= 2) {
		rtwn_bb_write(sc, R92C_TXAGC_MCS11_MCS08(chain),
		    SM(R92C_TXAGC_MCS08,  power[RTWN_RIDX_HT_MCS(8)]) |
		    SM(R92C_TXAGC_MCS09,  power[RTWN_RIDX_HT_MCS(9)]) |
		    SM(R92C_TXAGC_MCS10,  power[RTWN_RIDX_HT_MCS(10)]) |
		    SM(R92C_TXAGC_MCS11,  power[RTWN_RIDX_HT_MCS(11)]));
		rtwn_bb_write(sc, R92C_TXAGC_MCS15_MCS12(chain),
		    SM(R92C_TXAGC_MCS12,  power[RTWN_RIDX_HT_MCS(12)]) |
		    SM(R92C_TXAGC_MCS13,  power[RTWN_RIDX_HT_MCS(13)]) |
		    SM(R92C_TXAGC_MCS14,  power[RTWN_RIDX_HT_MCS(14)]) |
		    SM(R92C_TXAGC_MCS15,  power[RTWN_RIDX_HT_MCS(15)]));
	}
}

static void
r92c_set_txpower(struct rtwn_softc *sc, struct ieee80211_channel *c)
{
	uint8_t power[RTWN_RIDX_COUNT];
	int i;

	for (i = 0; i < sc->ntxchains; i++) {
		memset(power, 0, sizeof(power));
		/* Compute per-rate Tx power values. */
		rtwn_r92c_get_txpower(sc, i, c, power);
#ifdef RTWN_DEBUG
		if (sc->sc_debug & RTWN_DEBUG_TXPWR) {
			int max_mcs, ridx;

			max_mcs = RTWN_RIDX_HT_MCS(sc->ntxchains * 8 - 1);

			/* Dump per-rate Tx power values. */
			printf("Tx power for chain %d:\n", i);
			for (ridx = RTWN_RIDX_CCK1; ridx <= max_mcs; ridx++)
				printf("Rate %d = %u\n", ridx, power[ridx]);
		}
#endif
		/* Write per-rate Tx power values to hardware. */
		r92c_write_txpower(sc, i, power);
	}
}

static void
r92c_set_bw40(struct rtwn_softc *sc, uint8_t chan, int prichlo)
{
	struct r92c_softc *rs = sc->sc_priv;

	rtwn_setbits_1(sc, R92C_BWOPMODE, R92C_BWOPMODE_20MHZ, 0);
	rtwn_setbits_1(sc, R92C_RRSR + 2, 0x6f, (prichlo ? 1 : 2) << 5);

	rtwn_bb_setbits(sc, R92C_FPGA0_RFMOD, 0, R92C_RFMOD_40MHZ);
	rtwn_bb_setbits(sc, R92C_FPGA1_RFMOD, 0, R92C_RFMOD_40MHZ);

	/* Set CCK side band. */
	rtwn_bb_setbits(sc, R92C_CCK0_SYSTEM, 0x10,
	    (prichlo ? 0 : 1) << 4);

	rtwn_bb_setbits(sc, R92C_OFDM1_LSTF, 0x0c00,
	    (prichlo ? 1 : 2) << 10);

	rtwn_bb_setbits(sc, R92C_FPGA0_ANAPARAM2,
	    R92C_FPGA0_ANAPARAM2_CBW20, 0);

	rtwn_bb_setbits(sc, 0x818, 0x0c000000, (prichlo ? 2 : 1) << 26);

	/* Select 40MHz bandwidth. */
	rtwn_rf_write(sc, 0, R92C_RF_CHNLBW,
	    (rs->rf_chnlbw[0] & ~0xfff) | chan);
}

void
r92c_set_bw20(struct rtwn_softc *sc, uint8_t chan)
{
	struct r92c_softc *rs = sc->sc_priv;

	rtwn_setbits_1(sc, R92C_BWOPMODE, 0, R92C_BWOPMODE_20MHZ);

	rtwn_bb_setbits(sc, R92C_FPGA0_RFMOD, R92C_RFMOD_40MHZ, 0);
	rtwn_bb_setbits(sc, R92C_FPGA1_RFMOD, R92C_RFMOD_40MHZ, 0);

	rtwn_bb_setbits(sc, R92C_FPGA0_ANAPARAM2, 0,
	    R92C_FPGA0_ANAPARAM2_CBW20);

	/* Select 20MHz bandwidth. */
	rtwn_rf_write(sc, 0, R92C_RF_CHNLBW,
	    (rs->rf_chnlbw[0] & ~0xfff) | chan | R92C_RF_CHNLBW_BW20);
}

void
r92c_set_chan(struct rtwn_softc *sc, struct ieee80211_channel *c)
{
	struct r92c_softc *rs = sc->sc_priv;
	u_int chan;
	int i;

	chan = rtwn_chan2centieee(c);

	/* Set Tx power for this new channel. */
	r92c_set_txpower(sc, c);

	for (i = 0; i < sc->nrxchains; i++) {
		rtwn_rf_write(sc, i, R92C_RF_CHNLBW,
		    RW(rs->rf_chnlbw[i], R92C_RF_CHNLBW_CHNL, chan));
	}
	if (IEEE80211_IS_CHAN_HT40(c))
		r92c_set_bw40(sc, chan, IEEE80211_IS_CHAN_HT40U(c));
	else
		rtwn_r92c_set_bw20(sc, chan);
}

void
r92c_set_gain(struct rtwn_softc *sc, uint8_t gain)
{

	rtwn_bb_setbits(sc, R92C_OFDM0_AGCCORE1(0),
	    R92C_OFDM0_AGCCORE1_GAIN_M, gain);
	rtwn_bb_setbits(sc, R92C_OFDM0_AGCCORE1(1),
	    R92C_OFDM0_AGCCORE1_GAIN_M, gain);
}

void
r92c_scan_start(struct ieee80211com *ic)
{
	struct rtwn_softc *sc = ic->ic_softc;
	struct r92c_softc *rs = sc->sc_priv;

	RTWN_LOCK(sc);
	/* Set gain for scanning. */
	rtwn_r92c_set_gain(sc, 0x20);
	RTWN_UNLOCK(sc);

	rs->rs_scan_start(ic);
}

void
r92c_scan_end(struct ieee80211com *ic)
{
	struct rtwn_softc *sc = ic->ic_softc;
	struct r92c_softc *rs = sc->sc_priv;

	RTWN_LOCK(sc);
	/* Set gain under link. */
	rtwn_r92c_set_gain(sc, 0x32);
	RTWN_UNLOCK(sc);

	rs->rs_scan_end(ic);
}

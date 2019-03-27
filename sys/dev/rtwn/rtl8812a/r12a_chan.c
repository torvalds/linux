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
#include <dev/rtwn/if_rtwn_ridx.h>
#include <dev/rtwn/if_rtwn_rx.h>

#include <dev/rtwn/rtl8812a/r12a.h>
#include <dev/rtwn/rtl8812a/r12a_reg.h>
#include <dev/rtwn/rtl8812a/r12a_var.h>


static void
r12a_write_txpower(struct rtwn_softc *sc, int chain,
    struct ieee80211_channel *c, uint8_t power[RTWN_RIDX_COUNT])
{

	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		/* Write per-CCK rate Tx power. */
		rtwn_bb_write(sc, R12A_TXAGC_CCK11_1(chain),
		    SM(R12A_TXAGC_CCK1,  power[RTWN_RIDX_CCK1]) |
		    SM(R12A_TXAGC_CCK2,  power[RTWN_RIDX_CCK2]) |
		    SM(R12A_TXAGC_CCK55, power[RTWN_RIDX_CCK55]) |
		    SM(R12A_TXAGC_CCK11, power[RTWN_RIDX_CCK11]));
	}

	/* Write per-OFDM rate Tx power. */
	rtwn_bb_write(sc, R12A_TXAGC_OFDM18_6(chain),
	    SM(R12A_TXAGC_OFDM06, power[RTWN_RIDX_OFDM6]) |
	    SM(R12A_TXAGC_OFDM09, power[RTWN_RIDX_OFDM9]) |
	    SM(R12A_TXAGC_OFDM12, power[RTWN_RIDX_OFDM12]) |
	    SM(R12A_TXAGC_OFDM18, power[RTWN_RIDX_OFDM18]));
	rtwn_bb_write(sc, R12A_TXAGC_OFDM54_24(chain),
	    SM(R12A_TXAGC_OFDM24, power[RTWN_RIDX_OFDM24]) |
	    SM(R12A_TXAGC_OFDM36, power[RTWN_RIDX_OFDM36]) |
	    SM(R12A_TXAGC_OFDM48, power[RTWN_RIDX_OFDM48]) |
	    SM(R12A_TXAGC_OFDM54, power[RTWN_RIDX_OFDM54]));
	/* Write per-MCS Tx power. */
	rtwn_bb_write(sc, R12A_TXAGC_MCS3_0(chain),
	    SM(R12A_TXAGC_MCS0, power[RTWN_RIDX_HT_MCS(0)]) |
	    SM(R12A_TXAGC_MCS1, power[RTWN_RIDX_HT_MCS(1)]) |
	    SM(R12A_TXAGC_MCS2, power[RTWN_RIDX_HT_MCS(2)]) |
	    SM(R12A_TXAGC_MCS3, power[RTWN_RIDX_HT_MCS(3)]));
	rtwn_bb_write(sc, R12A_TXAGC_MCS7_4(chain),
	    SM(R12A_TXAGC_MCS4, power[RTWN_RIDX_HT_MCS(4)]) |
	    SM(R12A_TXAGC_MCS5, power[RTWN_RIDX_HT_MCS(5)]) |
	    SM(R12A_TXAGC_MCS6, power[RTWN_RIDX_HT_MCS(6)]) |
	    SM(R12A_TXAGC_MCS7, power[RTWN_RIDX_HT_MCS(7)]));
	if (sc->ntxchains >= 2) {
		rtwn_bb_write(sc, R12A_TXAGC_MCS11_8(chain),
		    SM(R12A_TXAGC_MCS8,  power[RTWN_RIDX_HT_MCS(8)]) |
		    SM(R12A_TXAGC_MCS9,  power[RTWN_RIDX_HT_MCS(9)]) |
		    SM(R12A_TXAGC_MCS10, power[RTWN_RIDX_HT_MCS(10)]) |
		    SM(R12A_TXAGC_MCS11, power[RTWN_RIDX_HT_MCS(11)]));
		rtwn_bb_write(sc, R12A_TXAGC_MCS15_12(chain),
		    SM(R12A_TXAGC_MCS12, power[RTWN_RIDX_HT_MCS(12)]) |
		    SM(R12A_TXAGC_MCS13, power[RTWN_RIDX_HT_MCS(13)]) |
		    SM(R12A_TXAGC_MCS14, power[RTWN_RIDX_HT_MCS(14)]) |
		    SM(R12A_TXAGC_MCS15, power[RTWN_RIDX_HT_MCS(15)]));
	}

	/* TODO: VHT rates */
}

static int
r12a_get_power_group(struct rtwn_softc *sc, struct ieee80211_channel *c)
{
	uint8_t chan;
	int group;

	chan = rtwn_chan2centieee(c);
	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		if (chan <= 2)			group = 0;
		else if (chan <= 5)		group = 1;
		else if (chan <= 8)		group = 2;
		else if (chan <= 11)		group = 3;
		else if (chan <= 14)		group = 4;
		else {
			KASSERT(0, ("wrong 2GHz channel %d!\n", chan));
			return (-1);
		}
	} else if (IEEE80211_IS_CHAN_5GHZ(c)) {
		if (chan < 36)
			return (-1);

		if (chan <= 42)			group = 0;
		else if (chan <= 48)		group = 1;
		else if (chan <= 58)		group = 2;
		else if (chan <= 64)		group = 3;
		else if (chan <= 106)		group = 4;
		else if (chan <= 114)		group = 5;
		else if (chan <= 122)		group = 6;
		else if (chan <= 130)		group = 7;
		else if (chan <= 138)		group = 8;
		else if (chan <= 144)		group = 9;
		else if (chan <= 155)		group = 10;
		else if (chan <= 161)		group = 11;
		else if (chan <= 171)		group = 12;
		else if (chan <= 177)		group = 13;
		else {
			KASSERT(0, ("wrong 5GHz channel %d!\n", chan));
			return (-1);
		}
	} else {
		KASSERT(0, ("wrong channel band (flags %08X)\n", c->ic_flags));
		return (-1);
	}

	return (group);
}

static void
r12a_get_txpower(struct rtwn_softc *sc, int chain,
    struct ieee80211_channel *c, uint8_t power[RTWN_RIDX_COUNT])
{
	struct r12a_softc *rs = sc->sc_priv;
	int i, ridx, group, max_mcs;

	/* Determine channel group. */
	group = r12a_get_power_group(sc, c);
	if (group == -1) {	/* shouldn't happen */
		device_printf(sc->sc_dev, "%s: incorrect channel\n", __func__);
		return;
	}

	/* TODO: VHT rates. */
	max_mcs = RTWN_RIDX_HT_MCS(sc->ntxchains * 8 - 1);

	/* XXX regulatory */
	/* XXX net80211 regulatory */

	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		for (ridx = RTWN_RIDX_CCK1; ridx <= RTWN_RIDX_CCK11; ridx++)
			power[ridx] = rs->cck_tx_pwr[chain][group];
		for (ridx = RTWN_RIDX_OFDM6; ridx <= max_mcs; ridx++)
			power[ridx] = rs->ht40_tx_pwr_2g[chain][group];

		for (ridx = RTWN_RIDX_OFDM6; ridx <= RTWN_RIDX_OFDM54; ridx++)
			power[ridx] += rs->ofdm_tx_pwr_diff_2g[chain][0];

		for (i = 0; i < sc->ntxchains; i++) {
			uint8_t min_mcs;
			uint8_t pwr_diff;

#ifdef notyet
			if (IEEE80211_IS_CHAN_HT80(c)) {
				/* Vendor driver uses HT40 values here. */
				pwr_diff = rs->bw40_tx_pwr_diff_2g[chain][i];
			} else
#endif
			if (IEEE80211_IS_CHAN_HT40(c))
				pwr_diff = rs->bw40_tx_pwr_diff_2g[chain][i];
			else
				pwr_diff = rs->bw20_tx_pwr_diff_2g[chain][i];

			min_mcs = RTWN_RIDX_HT_MCS(i * 8);
			for (ridx = min_mcs; ridx <= max_mcs; ridx++)
				power[ridx] += pwr_diff;
		}
	} else {	/* 5GHz */
		for (ridx = RTWN_RIDX_OFDM6; ridx <= max_mcs; ridx++)
			power[ridx] = rs->ht40_tx_pwr_5g[chain][group];

		for (ridx = RTWN_RIDX_OFDM6; ridx <= RTWN_RIDX_OFDM54; ridx++)
			power[ridx] += rs->ofdm_tx_pwr_diff_5g[chain][0];

		for (i = 0; i < sc->ntxchains; i++) {
			uint8_t min_mcs;
			uint8_t pwr_diff;

#ifdef notyet
			if (IEEE80211_IS_CHAN_HT80(c)) {
				/* TODO: calculate base value. */
				pwr_diff = rs->bw80_tx_pwr_diff_5g[chain][i];
			} else
#endif
			if (IEEE80211_IS_CHAN_HT40(c))
				pwr_diff = rs->bw40_tx_pwr_diff_5g[chain][i];
			else
				pwr_diff = rs->bw20_tx_pwr_diff_5g[chain][i];

			min_mcs = RTWN_RIDX_HT_MCS(i * 8);
			for (ridx = min_mcs; ridx <= max_mcs; ridx++)
				power[ridx] += pwr_diff;
		}
	}

	/* Apply max limit. */
	for (ridx = RTWN_RIDX_CCK1; ridx <= max_mcs; ridx++) {
		if (power[ridx] > R92C_MAX_TX_PWR)
			power[ridx] = R92C_MAX_TX_PWR;
	}

#ifdef RTWN_DEBUG
	if (sc->sc_debug & RTWN_DEBUG_TXPWR) {
		/* Dump per-rate Tx power values. */
		printf("Tx power for chain %d:\n", chain);
		for (ridx = RTWN_RIDX_CCK1; ridx <= max_mcs; ridx++)
			printf("Rate %d = %u\n", ridx, power[ridx]);
	}
#endif
}

static void
r12a_set_txpower(struct rtwn_softc *sc, struct ieee80211_channel *c)
{
	uint8_t power[RTWN_RIDX_COUNT];
	int i;

	for (i = 0; i < sc->ntxchains; i++) {
		memset(power, 0, sizeof(power));
		/* Compute per-rate Tx power values. */
		r12a_get_txpower(sc, i, c, power);
		/* Write per-rate Tx power values to hardware. */
		r12a_write_txpower(sc, i, c, power);
	}
}

void
r12a_fix_spur(struct rtwn_softc *sc, struct ieee80211_channel *c)
{
	struct r12a_softc *rs = sc->sc_priv;
	uint16_t chan = rtwn_chan2centieee(c);

	if (rs->chip & R12A_CHIP_C_CUT) {
		if (IEEE80211_IS_CHAN_HT40(c) && chan == 11) {
			rtwn_bb_setbits(sc, R12A_RFMOD, 0, 0xc00);
			rtwn_bb_setbits(sc, R12A_ADC_BUF_CLK, 0, 0x40000000);
		} else {
			rtwn_bb_setbits(sc, R12A_RFMOD, 0x400, 0x800);

			if (!IEEE80211_IS_CHAN_HT40(c) &&	/* 20 MHz */
			    (chan == 13 || chan == 14)) {
				rtwn_bb_setbits(sc, R12A_RFMOD, 0, 0x300);
				rtwn_bb_setbits(sc, R12A_ADC_BUF_CLK,
				    0, 0x40000000);
			} else {	/* !80 Mhz */
				rtwn_bb_setbits(sc, R12A_RFMOD, 0x100, 0x200);
				rtwn_bb_setbits(sc, R12A_ADC_BUF_CLK,
				    0x40000000, 0);
			}
		}
	} else {
		/* Set ADC clock to 160M to resolve 2480 MHz spur. */
		if (!IEEE80211_IS_CHAN_HT40(c) &&	/* 20 MHz */
		    (chan == 13 || chan == 14))
			rtwn_bb_setbits(sc, R12A_RFMOD, 0, 0x300);
		else if (IEEE80211_IS_CHAN_2GHZ(c))
			rtwn_bb_setbits(sc, R12A_RFMOD, 0x100, 0x200);
	}
}

static void
r12a_set_band(struct rtwn_softc *sc, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct r12a_softc *rs = sc->sc_priv;
	uint32_t basicrates;
	uint8_t swing;
	int i;

	/* Check if band was changed. */
	if ((sc->sc_flags & (RTWN_STARTED | RTWN_RUNNING)) !=
	    RTWN_STARTED && IEEE80211_IS_CHAN_5GHZ(c) ^
	    !(rtwn_read_1(sc, R12A_CCK_CHECK) & R12A_CCK_CHECK_5GHZ))
		return;

	rtwn_get_rates(sc, ieee80211_get_suprates(ic, c), NULL, &basicrates,
	    NULL, 1);
	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		rtwn_r12a_set_band_2ghz(sc, basicrates);
		swing = rs->tx_bbswing_2g;
	} else if (IEEE80211_IS_CHAN_5GHZ(c)) {
		rtwn_r12a_set_band_5ghz(sc, basicrates);
		swing = rs->tx_bbswing_5g;
	} else {
		KASSERT(0, ("wrong channel flags %08X\n", c->ic_flags));
		return;
	}

	/* XXX PATH_B is set by vendor driver. */
	for (i = 0; i < 2; i++) {
		uint16_t val = 0;

		switch ((swing >> i * 2) & 0x3) {
		case 0:
			val = 0x200;	/* 0 dB	*/
			break;
		case 1:
			val = 0x16a;	/* -3 dB */
			break;
		case 2:
			val = 0x101;	/* -6 dB */
			break;
		case 3:
			val = 0xb6;	/* -9 dB */
			break;
		}

		rtwn_bb_setbits(sc, R12A_TX_SCALE(i), R12A_TX_SCALE_SWING_M,
		    val << R12A_TX_SCALE_SWING_S);
	}
}

void
r12a_set_chan(struct rtwn_softc *sc, struct ieee80211_channel *c)
{
	uint32_t val;
	uint16_t chan;
	int i;

	r12a_set_band(sc, c);

	chan = rtwn_chan2centieee(c);
	if (36 <= chan && chan <= 48)
		val = 0x09280000;
	else if (50 <= chan && chan <= 64)
		val = 0x08a60000;
	else if (100 <= chan && chan <= 116)
		val = 0x08a40000;
	else if (118 <= chan)
		val = 0x08240000;
	else
		val = 0x12d40000;

	rtwn_bb_setbits(sc, R12A_FC_AREA, 0x1ffe0000, val);

	for (i = 0; i < sc->nrxchains; i++) {
		if (36 <= chan && chan <= 64)
			val = 0x10100;
		else if (100 <= chan && chan <= 140)
			val = 0x30100;
		else if (140 < chan)
			val = 0x50100;
		else
			val = 0x00000;

		rtwn_rf_setbits(sc, i, R92C_RF_CHNLBW, 0x70300, val);

		/* RTL8812AU-specific */
		rtwn_r12a_fix_spur(sc, c);

		KASSERT(chan <= 0xff, ("%s: chan %d\n", __func__, chan));
		rtwn_rf_setbits(sc, i, R92C_RF_CHNLBW, 0xff, chan);
	}

#ifdef notyet
	if (IEEE80211_IS_CHAN_HT80(c)) {	/* 80 MHz */
		rtwn_setbits_2(sc, R92C_WMAC_TRXPTCL_CTL, 0x80, 0x100);

		/* TODO */

		val = 0x0;
	} else
#endif
	if (IEEE80211_IS_CHAN_HT40(c)) {	/* 40 MHz */
		uint8_t ext_chan;

		if (IEEE80211_IS_CHAN_HT40U(c))
			ext_chan = R12A_DATA_SEC_PRIM_DOWN_20;
		else
			ext_chan = R12A_DATA_SEC_PRIM_UP_20;

		rtwn_setbits_2(sc, R92C_WMAC_TRXPTCL_CTL, 0x100, 0x80);
		rtwn_write_1(sc, R12A_DATA_SEC, ext_chan);

		rtwn_bb_setbits(sc, R12A_RFMOD, 0x003003c3, 0x00300201);
		rtwn_bb_setbits(sc, R12A_ADC_BUF_CLK, 0x40000000, 0);

		/* discard high 4 bits */
		val = rtwn_bb_read(sc, R12A_RFMOD);
		val = RW(val, R12A_RFMOD_EXT_CHAN, ext_chan);
		rtwn_bb_write(sc, R12A_RFMOD, val);

		val = rtwn_bb_read(sc, R12A_CCA_ON_SEC);
		val = RW(val, R12A_CCA_ON_SEC_EXT_CHAN, ext_chan);
		rtwn_bb_write(sc, R12A_CCA_ON_SEC, val);

		if (rtwn_read_1(sc, 0x837) & 0x04)
			val = 0x01800000;
		else if (sc->nrxchains == 2 && sc->ntxchains == 2)
			val = 0x01c00000;
		else
			val = 0x02000000;

		rtwn_bb_setbits(sc, R12A_L1_PEAK_TH, 0x03c00000, val);

		if (IEEE80211_IS_CHAN_HT40U(c))
			rtwn_bb_setbits(sc, R92C_CCK0_SYSTEM, 0x10, 0);
		else
			rtwn_bb_setbits(sc, R92C_CCK0_SYSTEM, 0, 0x10);

		val = 0x400;
	} else {	/* 20 MHz */
		rtwn_setbits_2(sc, R92C_WMAC_TRXPTCL_CTL, 0x180, 0);
		rtwn_write_1(sc, R12A_DATA_SEC, R12A_DATA_SEC_NO_EXT);

		rtwn_bb_setbits(sc, R12A_RFMOD, 0x003003c3, 0x00300200);
		rtwn_bb_setbits(sc, R12A_ADC_BUF_CLK, 0x40000000, 0);

		if (sc->nrxchains == 2 && sc->ntxchains == 2)
			val = 0x01c00000;
		else
			val = 0x02000000;

		rtwn_bb_setbits(sc, R12A_L1_PEAK_TH, 0x03c00000, val);

		val = 0xc00;
	}

	/* RTL8812AU-specific */
	rtwn_r12a_fix_spur(sc, c);

	for (i = 0; i < sc->nrxchains; i++)
		rtwn_rf_setbits(sc, i, R92C_RF_CHNLBW, 0xc00, val);

	/* Set Tx power for this new channel. */
	r12a_set_txpower(sc, c);
}

void
r12a_set_band_2ghz(struct rtwn_softc *sc, uint32_t basicrates)
{
	struct r12a_softc *rs = sc->sc_priv;

	/* Enable CCK / OFDM. */
	rtwn_bb_setbits(sc, R12A_OFDMCCK_EN,
	    0, R12A_OFDMCCK_EN_CCK | R12A_OFDMCCK_EN_OFDM);

	rtwn_bb_setbits(sc, R12A_BW_INDICATION, 0x02, 0x01);
	rtwn_bb_setbits(sc, R12A_PWED_TH, 0x3e000, 0x2e000);

	/* Select AGC table. */
	rtwn_bb_setbits(sc, R12A_TXAGC_TABLE_SELECT, 0x03, 0);

	switch (rs->rfe_type) {
	case 0:
	case 1:
	case 2:
		rtwn_bb_write(sc, R12A_RFE_PINMUX(0), 0x77777777);
		rtwn_bb_write(sc, R12A_RFE_PINMUX(1), 0x77777777);
		rtwn_bb_setbits(sc, R12A_RFE_INV(0), 0x3ff00000, 0);
		rtwn_bb_setbits(sc, R12A_RFE_INV(1), 0x3ff00000, 0);
		break;
	case 3:
		rtwn_bb_write(sc, R12A_RFE_PINMUX(0), 0x54337770);
		rtwn_bb_write(sc, R12A_RFE_PINMUX(1), 0x54337770);
		rtwn_bb_setbits(sc, R12A_RFE_INV(0), 0x3ff00000, 0x01000000);
		rtwn_bb_setbits(sc, R12A_RFE_INV(1), 0x3ff00000, 0x01000000);
		rtwn_bb_setbits(sc, R12A_ANTSEL_SW, 0x0303, 0x01);
		break;
	case 4:
		rtwn_bb_write(sc, R12A_RFE_PINMUX(0), 0x77777777);
		rtwn_bb_write(sc, R12A_RFE_PINMUX(1), 0x77777777);
		rtwn_bb_setbits(sc, R12A_RFE_INV(0), 0x3ff00000, 0x00100000);
		rtwn_bb_setbits(sc, R12A_RFE_INV(1), 0x3ff00000, 0x00100000);
		break;
	case 5:
		rtwn_write_1(sc, R12A_RFE_PINMUX(0) + 2, 0x77);
		rtwn_bb_write(sc, R12A_RFE_PINMUX(1), 0x77777777);
		rtwn_setbits_1(sc, R12A_RFE_INV(0) + 3, 0x01, 0);
		rtwn_bb_setbits(sc, R12A_RFE_INV(1), 0x3ff00000, 0);
		break;
	default:
		break;
	}

	rtwn_bb_setbits(sc, R12A_TX_PATH, 0xf0, 0x10);
	rtwn_bb_setbits(sc, R12A_CCK_RX_PATH, 0x0f000000, 0x01000000);

	/* Write basic rates. */
	rtwn_set_basicrates(sc, basicrates);

	rtwn_write_1(sc, R12A_CCK_CHECK, 0);
}

void
r12a_set_band_5ghz(struct rtwn_softc *sc, uint32_t basicrates)
{
	struct r12a_softc *rs = sc->sc_priv;
	int ntries;

	rtwn_write_1(sc, R12A_CCK_CHECK, R12A_CCK_CHECK_5GHZ);

	for (ntries = 0; ntries < 100; ntries++) {
		if ((rtwn_read_2(sc, R12A_TXPKT_EMPTY) & 0x30) == 0x30)
			break;

		rtwn_delay(sc, 25);
	}
	if (ntries == 100) {
		device_printf(sc->sc_dev,
		    "%s: TXPKT_EMPTY check failed (%04X)\n",
		    __func__, rtwn_read_2(sc, R12A_TXPKT_EMPTY));
	}

	/* Enable OFDM. */
	rtwn_bb_setbits(sc, R12A_OFDMCCK_EN, R12A_OFDMCCK_EN_CCK,
	    R12A_OFDMCCK_EN_OFDM);

	rtwn_bb_setbits(sc, R12A_BW_INDICATION, 0x01, 0x02);
	rtwn_bb_setbits(sc, R12A_PWED_TH, 0x3e000, 0x2a000);

	/* Select AGC table. */
	rtwn_bb_setbits(sc, R12A_TXAGC_TABLE_SELECT, 0x03, 0x01);

	switch (rs->rfe_type) {
	case 0:
		rtwn_bb_write(sc, R12A_RFE_PINMUX(0), 0x77337717);
		rtwn_bb_write(sc, R12A_RFE_PINMUX(1), 0x77337717);
		rtwn_bb_setbits(sc, R12A_RFE_INV(0), 0x3ff00000, 0x01000000);
		rtwn_bb_setbits(sc, R12A_RFE_INV(1), 0x3ff00000, 0x01000000);
		break;
	case 1:
		rtwn_bb_write(sc, R12A_RFE_PINMUX(0), 0x77337717);
		rtwn_bb_write(sc, R12A_RFE_PINMUX(1), 0x77337717);
		rtwn_bb_setbits(sc, R12A_RFE_INV(0), 0x3ff00000, 0);
		rtwn_bb_setbits(sc, R12A_RFE_INV(1), 0x3ff00000, 0);
		break;
	case 2:
	case 4:
		rtwn_bb_write(sc, R12A_RFE_PINMUX(0), 0x77337777);
		rtwn_bb_write(sc, R12A_RFE_PINMUX(1), 0x77337777);
		rtwn_bb_setbits(sc, R12A_RFE_INV(0), 0x3ff00000, 0x01000000);
		rtwn_bb_setbits(sc, R12A_RFE_INV(1), 0x3ff00000, 0x01000000);
		break;
	case 3:
		rtwn_bb_write(sc, R12A_RFE_PINMUX(0), 0x54337717);
		rtwn_bb_write(sc, R12A_RFE_PINMUX(1), 0x54337717);
		rtwn_bb_setbits(sc, R12A_RFE_INV(0), 0x3ff00000, 0x01000000);
		rtwn_bb_setbits(sc, R12A_RFE_INV(1), 0x3ff00000, 0x01000000);
		rtwn_bb_setbits(sc, R12A_ANTSEL_SW, 0x0303, 0x01);
		break;
	case 5:
		rtwn_write_1(sc, R12A_RFE_PINMUX(0) + 2, 0x33);
		rtwn_bb_write(sc, R12A_RFE_PINMUX(1), 0x77337777);
		rtwn_setbits_1(sc, R12A_RFE_INV(0) + 3, 0, 0x01);
		rtwn_bb_setbits(sc, R12A_RFE_INV(1), 0x3ff00000, 0x01000000);
		break;
	default:
		break;
	}

	rtwn_bb_setbits(sc, R12A_TX_PATH, 0xf0, 0);
	rtwn_bb_setbits(sc, R12A_CCK_RX_PATH, 0, 0x0f000000);

	/* Write basic rates. */
	rtwn_set_basicrates(sc, basicrates);
}

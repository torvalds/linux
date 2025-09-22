/*	$OpenBSD: ar9287.c,v 1.30 2022/01/09 05:42:38 jsg Exp $	*/

/*-
 * Copyright (c) 2009 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2008-2009 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

/*
 * Driver for Atheros 802.11a/g/n chipsets.
 * Routines for AR9227 and AR9287 chipsets.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/timeout.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/endian.h>

#include <machine/bus.h>
#include <machine/intr.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif
#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_amrr.h>
#include <net80211/ieee80211_ra.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/ic/athnreg.h>
#include <dev/ic/athnvar.h>

#include <dev/ic/ar5008reg.h>
#include <dev/ic/ar9280reg.h>
#include <dev/ic/ar9287reg.h>

int	ar9287_attach(struct athn_softc *);
void	ar9287_setup(struct athn_softc *);
void	ar9287_swap_rom(struct athn_softc *);
const	struct ar_spur_chan *ar9287_get_spur_chans(struct athn_softc *, int);
void	ar9287_init_from_rom(struct athn_softc *, struct ieee80211_channel *,
	    struct ieee80211_channel *);
void	ar9287_get_pdadcs(struct athn_softc *, struct ieee80211_channel *,
	    int, int, uint8_t, uint8_t *, uint8_t *);
void	ar9287_olpc_get_pdgain(struct athn_softc *, struct ieee80211_channel *,
	    int, int8_t *);
void	ar9287_set_power_calib(struct athn_softc *,
	    struct ieee80211_channel *);
void	ar9287_set_txpower(struct athn_softc *, struct ieee80211_channel *,
	    struct ieee80211_channel *);
void	ar9287_olpc_init(struct athn_softc *);
void	ar9287_olpc_temp_compensation(struct athn_softc *);
void	ar9287_1_3_enable_async_fifo(struct athn_softc *);
void	ar9287_1_3_setup_async_fifo(struct athn_softc *);

/* Extern functions. */
uint8_t	athn_chan2fbin(struct ieee80211_channel *);
void	athn_get_pier_ival(uint8_t, const uint8_t *, int, int *, int *);
int	ar5008_attach(struct athn_softc *);
void	ar5008_write_txpower(struct athn_softc *, int16_t power[]);
void	ar5008_get_pdadcs(struct athn_softc *, uint8_t, struct athn_pier *,
	    struct athn_pier *, int, int, uint8_t, uint8_t *, uint8_t *);
void	ar5008_get_lg_tpow(struct athn_softc *, struct ieee80211_channel *,
	    uint8_t, const struct ar_cal_target_power_leg *, int, uint8_t[]);
void	ar5008_get_ht_tpow(struct athn_softc *, struct ieee80211_channel *,
	    uint8_t, const struct ar_cal_target_power_ht *, int, uint8_t[]);
int	ar9280_set_synth(struct athn_softc *, struct ieee80211_channel *,
	    struct ieee80211_channel *);
void	ar9280_spur_mitigate(struct athn_softc *, struct ieee80211_channel *,
	    struct ieee80211_channel *);


int
ar9287_attach(struct athn_softc *sc)
{
	sc->eep_base = (sc->flags & ATHN_FLAG_USB) ?
	    AR9287_HTC_EEP_START_LOC : AR9287_EEP_START_LOC;
	sc->eep_size = sizeof(struct ar9287_eeprom);
	sc->ngpiopins = (sc->flags & ATHN_FLAG_USB) ? 16 : 11;
	sc->led_pin = (sc->flags & ATHN_FLAG_USB) ? 10 : 8;
	sc->workaround = AR9285_WA_DEFAULT;
	sc->ops.setup = ar9287_setup;
	sc->ops.swap_rom = ar9287_swap_rom;
	sc->ops.init_from_rom = ar9287_init_from_rom;
	sc->ops.set_txpower = ar9287_set_txpower;
	sc->ops.set_synth = ar9280_set_synth;
	sc->ops.spur_mitigate = ar9280_spur_mitigate;
	sc->ops.get_spur_chans = ar9287_get_spur_chans;
	sc->ops.olpc_init = ar9287_olpc_init;
	sc->ops.olpc_temp_compensation = ar9287_olpc_temp_compensation;
	sc->cca_min_2g = AR9287_PHY_CCA_MIN_GOOD_VAL_2GHZ;
	sc->cca_max_2g = AR9287_PHY_CCA_MAX_GOOD_VAL_2GHZ;
	sc->ini = &ar9287_1_1_ini;
	sc->serdes = &ar9280_2_0_serdes;

	return (ar5008_attach(sc));
}

void
ar9287_setup(struct athn_softc *sc)
{
	const struct ar9287_eeprom *eep = sc->eep;

	/* Determine if open loop power control should be used. */
	if (eep->baseEepHeader.openLoopPwrCntl)
		sc->flags |= ATHN_FLAG_OLPC;

	sc->rx_gain = &ar9287_1_1_rx_gain;
	sc->tx_gain = &ar9287_1_1_tx_gain;
}

void
ar9287_swap_rom(struct athn_softc *sc)
{
	struct ar9287_eeprom *eep = sc->eep;
	int i;

	eep->modalHeader.antCtrlCommon =
	    swap32(eep->modalHeader.antCtrlCommon);

	for (i = 0; i < AR9287_MAX_CHAINS; i++) {
		eep->modalHeader.antCtrlChain[i] =
		    swap32(eep->modalHeader.antCtrlChain[i]);
	}
	for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
		eep->modalHeader.spurChans[i].spurChan =
		    swap16(eep->modalHeader.spurChans[i].spurChan);
	}
}

const struct ar_spur_chan *
ar9287_get_spur_chans(struct athn_softc *sc, int is2ghz)
{
	const struct ar9287_eeprom *eep = sc->eep;

	KASSERT(is2ghz);
	return (eep->modalHeader.spurChans);
}

void
ar9287_init_from_rom(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	const struct ar9287_eeprom *eep = sc->eep;
	const struct ar9287_modal_eep_header *modal = &eep->modalHeader;
	uint32_t reg, offset;
	int i;

	AR_WRITE(sc, AR_PHY_SWITCH_COM, modal->antCtrlCommon);

	for (i = 0; i < AR9287_MAX_CHAINS; i++) {
		offset = i * 0x1000;

		AR_WRITE(sc, AR_PHY_SWITCH_CHAIN_0 + offset,
		    modal->antCtrlChain[i]);

		reg = AR_READ(sc, AR_PHY_TIMING_CTRL4_0 + offset);
		reg = RW(reg, AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF,
		    modal->iqCalICh[i]);
		reg = RW(reg, AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF,
		    modal->iqCalQCh[i]);
		AR_WRITE(sc, AR_PHY_TIMING_CTRL4_0 + offset, reg);

		reg = AR_READ(sc, AR_PHY_GAIN_2GHZ + offset);
		reg = RW(reg, AR_PHY_GAIN_2GHZ_XATTEN1_MARGIN,
		    modal->bswMargin[i]);
		reg = RW(reg, AR_PHY_GAIN_2GHZ_XATTEN1_DB,
		    modal->bswAtten[i]);
		AR_WRITE(sc, AR_PHY_GAIN_2GHZ + offset, reg);

		reg = AR_READ(sc, AR_PHY_RXGAIN + offset);
		reg = RW(reg, AR9280_PHY_RXGAIN_TXRX_MARGIN,
		    modal->rxTxMarginCh[i]);
		reg = RW(reg, AR9280_PHY_RXGAIN_TXRX_ATTEN,
		    modal->txRxAttenCh[i]);
		AR_WRITE(sc, AR_PHY_RXGAIN + offset, reg);
	}

	reg = AR_READ(sc, AR_PHY_SETTLING);
	if (extc != NULL)
		reg = RW(reg, AR_PHY_SETTLING_SWITCH, modal->swSettleHt40);
	else
		reg = RW(reg, AR_PHY_SETTLING_SWITCH, modal->switchSettling);
	AR_WRITE(sc, AR_PHY_SETTLING, reg);

	reg = AR_READ(sc, AR_PHY_DESIRED_SZ);
	reg = RW(reg, AR_PHY_DESIRED_SZ_ADC, modal->adcDesiredSize);
	AR_WRITE(sc, AR_PHY_DESIRED_SZ, reg);

	reg  = SM(AR_PHY_RF_CTL4_TX_END_XPAA_OFF, modal->txEndToXpaOff);
	reg |= SM(AR_PHY_RF_CTL4_TX_END_XPAB_OFF, modal->txEndToXpaOff);
	reg |= SM(AR_PHY_RF_CTL4_FRAME_XPAA_ON, modal->txFrameToXpaOn);
	reg |= SM(AR_PHY_RF_CTL4_FRAME_XPAB_ON, modal->txFrameToXpaOn);
	AR_WRITE(sc, AR_PHY_RF_CTL4, reg);

	reg = AR_READ(sc, AR_PHY_RF_CTL3);
	reg = RW(reg, AR_PHY_TX_END_TO_A2_RX_ON, modal->txEndToRxOn);
	AR_WRITE(sc, AR_PHY_RF_CTL3, reg);

	reg = AR_READ(sc, AR_PHY_CCA(0));
	reg = RW(reg, AR9280_PHY_CCA_THRESH62, modal->thresh62);
	AR_WRITE(sc, AR_PHY_CCA(0), reg);

	reg = AR_READ(sc, AR_PHY_EXT_CCA0);
	reg = RW(reg, AR_PHY_EXT_CCA0_THRESH62, modal->thresh62);
	AR_WRITE(sc, AR_PHY_EXT_CCA0, reg);

	reg = AR_READ(sc, AR9287_AN_RF2G3_CH0);
	reg = RW(reg, AR9287_AN_RF2G3_DB1, modal->db1);
	reg = RW(reg, AR9287_AN_RF2G3_DB2, modal->db2);
	reg = RW(reg, AR9287_AN_RF2G3_OB_CCK, modal->ob_cck);
	reg = RW(reg, AR9287_AN_RF2G3_OB_PSK, modal->ob_psk);
	reg = RW(reg, AR9287_AN_RF2G3_OB_QAM, modal->ob_qam);
	reg = RW(reg, AR9287_AN_RF2G3_OB_PAL_OFF, modal->ob_pal_off);
	AR_WRITE(sc, AR9287_AN_RF2G3_CH0, reg);
	AR_WRITE_BARRIER(sc);
	DELAY(100);

	reg = AR_READ(sc, AR9287_AN_RF2G3_CH1);
	reg = RW(reg, AR9287_AN_RF2G3_DB1, modal->db1);
	reg = RW(reg, AR9287_AN_RF2G3_DB2, modal->db2);
	reg = RW(reg, AR9287_AN_RF2G3_OB_CCK, modal->ob_cck);
	reg = RW(reg, AR9287_AN_RF2G3_OB_PSK, modal->ob_psk);
	reg = RW(reg, AR9287_AN_RF2G3_OB_QAM, modal->ob_qam);
	reg = RW(reg, AR9287_AN_RF2G3_OB_PAL_OFF, modal->ob_pal_off);
	AR_WRITE(sc, AR9287_AN_RF2G3_CH1, reg);
	AR_WRITE_BARRIER(sc);
	DELAY(100);

	reg = AR_READ(sc, AR_PHY_RF_CTL2);
	reg = RW(reg, AR_PHY_TX_END_DATA_START, modal->txFrameToDataStart);
	reg = RW(reg, AR_PHY_TX_END_PA_ON, modal->txFrameToPaOn);
	AR_WRITE(sc, AR_PHY_RF_CTL2, reg);

	reg = AR_READ(sc, AR9287_AN_TOP2);
	reg = RW(reg, AR9287_AN_TOP2_XPABIAS_LVL, modal->xpaBiasLvl);
	AR_WRITE(sc, AR9287_AN_TOP2, reg);
	AR_WRITE_BARRIER(sc);
	DELAY(100);
}

void
ar9287_get_pdadcs(struct athn_softc *sc, struct ieee80211_channel *c,
    int chain, int nxpdgains, uint8_t overlap, uint8_t *boundaries,
    uint8_t *pdadcs)
{
	const struct ar9287_eeprom *eep = sc->eep;
	const struct ar9287_cal_data_per_freq *pierdata;
	const uint8_t *pierfreq;
	struct athn_pier lopier, hipier;
	int16_t delta;
	uint8_t fbin;
	int i, lo, hi, npiers;

	pierfreq = eep->calFreqPier2G;
	pierdata = (const struct ar9287_cal_data_per_freq *)
	    eep->calPierData2G[chain];
	npiers = AR9287_NUM_2G_CAL_PIERS;

	/* Find channel in ROM pier table. */
	fbin = athn_chan2fbin(c);
	athn_get_pier_ival(fbin, pierfreq, npiers, &lo, &hi);

	lopier.fbin = pierfreq[lo];
	hipier.fbin = pierfreq[hi];
	for (i = 0; i < nxpdgains; i++) {
		lopier.pwr[i] = pierdata[lo].pwrPdg[i];
		lopier.vpd[i] = pierdata[lo].vpdPdg[i];
		hipier.pwr[i] = pierdata[lo].pwrPdg[i];
		hipier.vpd[i] = pierdata[lo].vpdPdg[i];
	}
	ar5008_get_pdadcs(sc, fbin, &lopier, &hipier, nxpdgains,
	    AR9287_PD_GAIN_ICEPTS, overlap, boundaries, pdadcs);

	delta = (eep->baseEepHeader.pwrTableOffset -
	    AR_PWR_TABLE_OFFSET_DB) * 2;	/* In half dB. */
	if (delta != 0) {
		/* Shift the PDADC table to start at the new offset. */
		/* XXX Our padding value differs from Linux. */
		for (i = 0; i < AR_NUM_PDADC_VALUES; i++)
			pdadcs[i] = pdadcs[MIN(i + delta,
			    AR_NUM_PDADC_VALUES - 1)];
	}
}

void
ar9287_olpc_get_pdgain(struct athn_softc *sc, struct ieee80211_channel *c,
    int chain, int8_t *pwr)
{
	const struct ar9287_eeprom *eep = sc->eep;
	const struct ar_cal_data_per_freq_olpc *pierdata;
	const uint8_t *pierfreq;
	uint8_t fbin;
	int lo, hi, npiers;

	pierfreq = eep->calFreqPier2G;
	pierdata = (const struct ar_cal_data_per_freq_olpc *)
	    eep->calPierData2G[chain];
	npiers = AR9287_NUM_2G_CAL_PIERS;

	/* Find channel in ROM pier table. */
	fbin = athn_chan2fbin(c);
	athn_get_pier_ival(fbin, pierfreq, npiers, &lo, &hi);

#if 0
	*pwr = athn_interpolate(fbin,
	    pierfreq[lo], pierdata[lo].pwrPdg[0][0],
	    pierfreq[hi], pierdata[hi].pwrPdg[0][0]);
#else
	*pwr = (pierdata[lo].pwrPdg[0][0] + pierdata[hi].pwrPdg[0][0]) / 2;
#endif
}

void
ar9287_set_power_calib(struct athn_softc *sc, struct ieee80211_channel *c)
{
	const struct ar9287_eeprom *eep = sc->eep;
	uint8_t boundaries[AR_PD_GAINS_IN_MASK];
	uint8_t pdadcs[AR_NUM_PDADC_VALUES];
	uint8_t xpdgains[AR9287_NUM_PD_GAINS];
	int8_t txpower;
	uint8_t overlap;
	uint32_t reg, offset;
	int i, j, nxpdgains;

	if (sc->eep_rev < AR_EEP_MINOR_VER_2) {
		overlap = MS(AR_READ(sc, AR_PHY_TPCRG5),
		    AR_PHY_TPCRG5_PD_GAIN_OVERLAP);
	} else
		overlap = eep->modalHeader.pdGainOverlap;

	if (sc->flags & ATHN_FLAG_OLPC) {
		/* XXX not here. */
		sc->pdadc =
		    ((const struct ar_cal_data_per_freq_olpc *)
		     eep->calPierData2G[0])->vpdPdg[0][0];
	}

	nxpdgains = 0;
	memset(xpdgains, 0, sizeof(xpdgains));
	for (i = AR9287_PD_GAINS_IN_MASK - 1; i >= 0; i--) {
		if (nxpdgains >= AR9287_NUM_PD_GAINS)
			break;		/* Can't happen. */
		if (eep->modalHeader.xpdGain & (1 << i))
			xpdgains[nxpdgains++] = i;
	}
	reg = AR_READ(sc, AR_PHY_TPCRG1);
	reg = RW(reg, AR_PHY_TPCRG1_NUM_PD_GAIN, nxpdgains - 1);
	reg = RW(reg, AR_PHY_TPCRG1_PD_GAIN_1, xpdgains[0]);
	reg = RW(reg, AR_PHY_TPCRG1_PD_GAIN_2, xpdgains[1]);
	AR_WRITE(sc, AR_PHY_TPCRG1, reg);
	AR_WRITE_BARRIER(sc);

	for (i = 0; i < AR9287_MAX_CHAINS; i++)	{
		if (!(sc->txchainmask & (1 << i)))
			continue;

		offset = i * 0x1000;

		if (sc->flags & ATHN_FLAG_OLPC) {
			ar9287_olpc_get_pdgain(sc, c, i, &txpower);

			reg = AR_READ(sc, AR_PHY_TX_PWRCTRL6_0);
			reg = RW(reg, AR_PHY_TX_PWRCTRL_ERR_EST_MODE, 3);
			AR_WRITE(sc, AR_PHY_TX_PWRCTRL6_0, reg);

			reg = AR_READ(sc, AR_PHY_TX_PWRCTRL6_1);
			reg = RW(reg, AR_PHY_TX_PWRCTRL_ERR_EST_MODE, 3);
			AR_WRITE(sc, AR_PHY_TX_PWRCTRL6_1, reg);

			/* NB: txpower is in half dB. */
			reg = AR_READ(sc, AR_PHY_CH0_TX_PWRCTRL11 + offset);
			reg = RW(reg, AR_PHY_TX_PWRCTRL_OLPC_PWR, txpower);
			AR_WRITE(sc, AR_PHY_CH0_TX_PWRCTRL11 + offset, reg);

			AR_WRITE_BARRIER(sc);
			continue;	/* That's it for open loop mode. */
		}

		/* Closed loop power control. */
		ar9287_get_pdadcs(sc, c, i, nxpdgains, overlap,
		    boundaries, pdadcs);

		/* Write boundaries. */
		if (i == 0) {
			reg  = SM(AR_PHY_TPCRG5_PD_GAIN_OVERLAP,
			    overlap);
			reg |= SM(AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_1,
			    boundaries[0]);
			reg |= SM(AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_2,
			    boundaries[1]);
			reg |= SM(AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_3,
			    boundaries[2]);
			reg |= SM(AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_4,
			    boundaries[3]);
			AR_WRITE(sc, AR_PHY_TPCRG5 + offset, reg);
		}
		/* Write PDADC values. */
		for (j = 0; j < AR_NUM_PDADC_VALUES; j += 4) {
			AR_WRITE(sc, AR_PHY_PDADC_TBL_BASE + offset + j,
			    pdadcs[j + 0] <<  0 |
			    pdadcs[j + 1] <<  8 |
			    pdadcs[j + 2] << 16 |
			    pdadcs[j + 3] << 24);
		}
		AR_WRITE_BARRIER(sc);
	}
}

void
ar9287_set_txpower(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	const struct ar9287_eeprom *eep = sc->eep;
	const struct ar9287_modal_eep_header *modal = &eep->modalHeader;
	uint8_t tpow_cck[4], tpow_ofdm[4];
	uint8_t tpow_cck_ext[4], tpow_ofdm_ext[4];
	uint8_t tpow_ht20[8], tpow_ht40[8];
	uint8_t ht40inc;
	int16_t pwr = 0, max_ant_gain, power[ATHN_POWER_COUNT];
	int i;

	ar9287_set_power_calib(sc, c);

	/* Compute transmit power reduction due to antenna gain. */
	max_ant_gain = MAX(modal->antennaGainCh[0], modal->antennaGainCh[1]);
	/* XXX */

	/*
	 * Reduce scaled power by number of active chains to get per-chain
	 * transmit power level.
	 */
	if (sc->ntxchains == 2)
		pwr -= AR_PWR_DECREASE_FOR_2_CHAIN;
	if (pwr < 0)
		pwr = 0;

	/* Get CCK target powers. */
	ar5008_get_lg_tpow(sc, c, AR_CTL_11B, eep->calTargetPowerCck,
	    AR9287_NUM_2G_CCK_TARGET_POWERS, tpow_cck);

	/* Get OFDM target powers. */
	ar5008_get_lg_tpow(sc, c, AR_CTL_11G, eep->calTargetPower2G,
	    AR9287_NUM_2G_20_TARGET_POWERS, tpow_ofdm);

	/* Get HT-20 target powers. */
	ar5008_get_ht_tpow(sc, c, AR_CTL_2GHT20, eep->calTargetPower2GHT20,
	    AR9287_NUM_2G_20_TARGET_POWERS, tpow_ht20);

	if (extc != NULL) {
		/* Get HT-40 target powers. */
		ar5008_get_ht_tpow(sc, c, AR_CTL_2GHT40,
		    eep->calTargetPower2GHT40, AR9287_NUM_2G_40_TARGET_POWERS,
		    tpow_ht40);

		/* Get secondary channel CCK target powers. */
		ar5008_get_lg_tpow(sc, extc, AR_CTL_11B,
		    eep->calTargetPowerCck, AR9287_NUM_2G_CCK_TARGET_POWERS,
		    tpow_cck_ext);

		/* Get secondary channel OFDM target powers. */
		ar5008_get_lg_tpow(sc, extc, AR_CTL_11G,
		    eep->calTargetPower2G, AR9287_NUM_2G_20_TARGET_POWERS,
		    tpow_ofdm_ext);
	}

	memset(power, 0, sizeof(power));
	/* Shuffle target powers across transmit rates. */
	power[ATHN_POWER_OFDM6   ] =
	power[ATHN_POWER_OFDM9   ] =
	power[ATHN_POWER_OFDM12  ] =
	power[ATHN_POWER_OFDM18  ] =
	power[ATHN_POWER_OFDM24  ] = tpow_ofdm[0];
	power[ATHN_POWER_OFDM36  ] = tpow_ofdm[1];
	power[ATHN_POWER_OFDM48  ] = tpow_ofdm[2];
	power[ATHN_POWER_OFDM54  ] = tpow_ofdm[3];
	power[ATHN_POWER_XR      ] = tpow_ofdm[0];
	power[ATHN_POWER_CCK1_LP ] = tpow_cck[0];
	power[ATHN_POWER_CCK2_LP ] =
	power[ATHN_POWER_CCK2_SP ] = tpow_cck[1];
	power[ATHN_POWER_CCK55_LP] =
	power[ATHN_POWER_CCK55_SP] = tpow_cck[2];
	power[ATHN_POWER_CCK11_LP] =
	power[ATHN_POWER_CCK11_SP] = tpow_cck[3];
	for (i = 0; i < nitems(tpow_ht20); i++)
		power[ATHN_POWER_HT20(i)] = tpow_ht20[i];
	if (extc != NULL) {
		/* Correct PAR difference between HT40 and HT20/Legacy. */
		if (sc->eep_rev >= AR_EEP_MINOR_VER_2)
			ht40inc = modal->ht40PowerIncForPdadc;
		else
			ht40inc = AR_HT40_POWER_INC_FOR_PDADC;
		for (i = 0; i < nitems(tpow_ht40); i++)
			power[ATHN_POWER_HT40(i)] = tpow_ht40[i] + ht40inc;
		power[ATHN_POWER_OFDM_DUP] = tpow_ht40[0];
		power[ATHN_POWER_CCK_DUP ] = tpow_ht40[0];
		power[ATHN_POWER_OFDM_EXT] = tpow_ofdm_ext[0];
		if (IEEE80211_IS_CHAN_2GHZ(c))
			power[ATHN_POWER_CCK_EXT] = tpow_cck_ext[0];
	}

	for (i = 0; i < ATHN_POWER_COUNT; i++) {
		power[i] -= AR_PWR_TABLE_OFFSET_DB * 2;	/* In half dB. */
		if (power[i] > AR_MAX_RATE_POWER)
			power[i] = AR_MAX_RATE_POWER;
	}
	/* Commit transmit power values to hardware. */
	ar5008_write_txpower(sc, power);
}

void
ar9287_olpc_init(struct athn_softc *sc)
{
	uint32_t reg;

	AR_SETBITS(sc, AR_PHY_TX_PWRCTRL9, AR_PHY_TX_PWRCTRL9_RES_DC_REMOVAL);

	reg = AR_READ(sc, AR9287_AN_TXPC0);
	reg = RW(reg, AR9287_AN_TXPC0_TXPCMODE,
	    AR9287_AN_TXPC0_TXPCMODE_TEMPSENSE);
	AR_WRITE(sc, AR9287_AN_TXPC0, reg);
	AR_WRITE_BARRIER(sc);
	DELAY(100);
}

void
ar9287_olpc_temp_compensation(struct athn_softc *sc)
{
	const struct ar9287_eeprom *eep = sc->eep;
	int8_t pdadc, slope, tcomp;
	uint32_t reg;

	reg = AR_READ(sc, AR_PHY_TX_PWRCTRL4);
	pdadc = MS(reg, AR_PHY_TX_PWRCTRL_PD_AVG_OUT);
	DPRINTFN(3, ("PD Avg Out=%d\n", pdadc));

	if (sc->pdadc == 0 || pdadc == 0)
		return;	/* No frames transmitted yet. */

	/* Compute Tx gain temperature compensation. */
	if (sc->eep_rev >= AR_EEP_MINOR_VER_2)
		slope = eep->baseEepHeader.tempSensSlope;
	else
		slope = 0;
	if (slope != 0)	/* Prevents division by zero. */
		tcomp = ((pdadc - sc->pdadc) * 4) / slope;
	else
		tcomp = 0;
	DPRINTFN(3, ("OLPC temp compensation=%d\n", tcomp));

	/* Write compensation value for both Tx chains. */
	reg = AR_READ(sc, AR_PHY_CH0_TX_PWRCTRL11);
	reg = RW(reg, AR_PHY_TX_PWRCTRL_OLPC_TEMP_COMP, tcomp);
	AR_WRITE(sc, AR_PHY_CH0_TX_PWRCTRL11, reg);

	reg = AR_READ(sc, AR_PHY_CH1_TX_PWRCTRL11);
	reg = RW(reg, AR_PHY_TX_PWRCTRL_OLPC_TEMP_COMP, tcomp);
	AR_WRITE(sc, AR_PHY_CH1_TX_PWRCTRL11, reg);
	AR_WRITE_BARRIER(sc);
}

void
ar9287_1_3_enable_async_fifo(struct athn_softc *sc)
{
	/* Enable ASYNC FIFO. */
	AR_SETBITS(sc, AR_MAC_PCU_ASYNC_FIFO_REG3,
	    AR_MAC_PCU_ASYNC_FIFO_REG3_DATAPATH_SEL);
	AR_SETBITS(sc, AR_PHY_MODE, AR_PHY_MODE_ASYNCFIFO);
	AR_CLRBITS(sc, AR_MAC_PCU_ASYNC_FIFO_REG3,
	    AR_MAC_PCU_ASYNC_FIFO_REG3_SOFT_RESET);
	AR_SETBITS(sc, AR_MAC_PCU_ASYNC_FIFO_REG3,
	    AR_MAC_PCU_ASYNC_FIFO_REG3_SOFT_RESET);
	AR_WRITE_BARRIER(sc);
}

void
ar9287_1_3_setup_async_fifo(struct athn_softc *sc)
{
	uint32_t reg;

	/*
	 * MAC runs at 117MHz (instead of 88/44MHz) when ASYNC FIFO is
	 * enabled, so the following counters have to be changed.
	 */
	AR_WRITE(sc, AR_D_GBL_IFS_SIFS, AR_D_GBL_IFS_SIFS_ASYNC_FIFO_DUR);
	AR_WRITE(sc, AR_D_GBL_IFS_SLOT, AR_D_GBL_IFS_SLOT_ASYNC_FIFO_DUR);
	AR_WRITE(sc, AR_D_GBL_IFS_EIFS, AR_D_GBL_IFS_EIFS_ASYNC_FIFO_DUR);

	AR_WRITE(sc, AR_TIME_OUT, AR_TIME_OUT_ACK_CTS_ASYNC_FIFO_DUR);
	AR_WRITE(sc, AR_USEC, AR_USEC_ASYNC_FIFO_DUR);

	AR_SETBITS(sc, AR_MAC_PCU_LOGIC_ANALYZER,
	    AR_MAC_PCU_LOGIC_ANALYZER_DISBUG20768);

	reg = AR_READ(sc, AR_AHB_MODE);
	reg = RW(reg, AR_AHB_CUSTOM_BURST, AR_AHB_CUSTOM_BURST_ASYNC_FIFO_VAL);
	AR_WRITE(sc, AR_AHB_MODE, reg);

	AR_SETBITS(sc, AR_PCU_MISC_MODE2, AR_PCU_MISC_MODE2_ENABLE_AGGWEP);
	AR_WRITE_BARRIER(sc);
}

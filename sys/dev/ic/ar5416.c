/*	$OpenBSD: ar5416.c,v 1.23 2022/01/09 05:42:38 jsg Exp $	*/

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
 * Routines for AR5416, AR5418 and AR9160 chipsets.
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
#include <dev/ic/ar5416reg.h>

int	ar5416_attach(struct athn_softc *);
void	ar5416_setup(struct athn_softc *);
void	ar5416_swap_rom(struct athn_softc *);
const struct ar_spur_chan *
	ar5416_get_spur_chans(struct athn_softc *, int);
int	ar5416_set_synth(struct athn_softc *, struct ieee80211_channel *,
	    struct ieee80211_channel *);
uint8_t	ar5416_reverse_bits(uint8_t, int);
uint8_t	ar5416_get_rf_rev(struct athn_softc *);
void	ar5416_init_from_rom(struct athn_softc *, struct ieee80211_channel *,
	    struct ieee80211_channel *);
int	ar5416_init_calib(struct athn_softc *, struct ieee80211_channel *,
	    struct ieee80211_channel *);
void	ar5416_set_power_calib(struct athn_softc *,
	    struct ieee80211_channel *);
void	ar5416_set_txpower(struct athn_softc *, struct ieee80211_channel *,
	    struct ieee80211_channel *);
void	ar5416_spur_mitigate(struct athn_softc *, struct ieee80211_channel *,
	    struct ieee80211_channel *);
void	ar5416_rw_rfbits(uint32_t *, int, int, uint32_t, int);
void	ar5416_rw_bank6tpc(struct athn_softc *, struct ieee80211_channel *,
	    uint32_t *);
void	ar5416_rf_reset(struct athn_softc *, struct ieee80211_channel *);
void	ar5416_reset_bb_gain(struct athn_softc *, struct ieee80211_channel *);
void	ar5416_force_bias(struct athn_softc *, struct ieee80211_channel *);
void	ar9160_rw_addac(struct athn_softc *, struct ieee80211_channel *,
	    uint32_t *);
void	ar5416_reset_addac(struct athn_softc *, struct ieee80211_channel *);
void	ar5416_get_pdadcs(struct athn_softc *, struct ieee80211_channel *,
	    int, int, uint8_t, uint8_t *, uint8_t *);

/* Extern functions. */
uint8_t	athn_chan2fbin(struct ieee80211_channel *);
void	athn_get_pier_ival(uint8_t, const uint8_t *, int, int *, int *);
int	ar5008_attach(struct athn_softc *);
void	ar5008_write_txpower(struct athn_softc *, int16_t power[]);
void	ar5008_get_pdadcs(struct athn_softc *, uint8_t, struct athn_pier *,
	    struct athn_pier *, int, int, uint8_t, uint8_t *, uint8_t *);
void	ar5008_set_viterbi_mask(struct athn_softc *, int);
void	ar5008_get_lg_tpow(struct athn_softc *, struct ieee80211_channel *,
	    uint8_t, const struct ar_cal_target_power_leg *, int, uint8_t[]);
void	ar5008_get_ht_tpow(struct athn_softc *, struct ieee80211_channel *,
	    uint8_t, const struct ar_cal_target_power_ht *, int, uint8_t[]);
void	ar9280_olpc_get_pdadcs(struct athn_softc *, struct ieee80211_channel *,
	    int, uint8_t *, uint8_t *, uint8_t *);


int
ar5416_attach(struct athn_softc *sc)
{
	sc->eep_base = AR5416_EEP_START_LOC;
	sc->eep_size = sizeof(struct ar5416_eeprom);
	sc->ngpiopins = 14;
	sc->led_pin = 1;
	sc->workaround = AR5416_WA_DEFAULT;
	sc->ops.setup = ar5416_setup;
	sc->ops.swap_rom = ar5416_swap_rom;
	sc->ops.init_from_rom = ar5416_init_from_rom;
	sc->ops.set_txpower = ar5416_set_txpower;
	sc->ops.set_synth = ar5416_set_synth;
	sc->ops.spur_mitigate = ar5416_spur_mitigate;
	sc->ops.get_spur_chans = ar5416_get_spur_chans;
	sc->cca_min_2g = AR5416_PHY_CCA_MIN_GOOD_VAL_2GHZ;
	sc->cca_max_2g = AR5416_PHY_CCA_MAX_GOOD_VAL_2GHZ;
	sc->cca_min_5g = AR5416_PHY_CCA_MIN_GOOD_VAL_5GHZ;
	sc->cca_max_5g = AR5416_PHY_CCA_MAX_GOOD_VAL_5GHZ;
	if (AR_SREV_9160_10_OR_LATER(sc))
		sc->ini = &ar9160_ini;
	else
		sc->ini = &ar5416_ini;
	sc->serdes = &ar5416_serdes;

	return (ar5008_attach(sc));
}

void
ar5416_setup(struct athn_softc *sc)
{
	/* Select ADDAC programming. */
	if (AR_SREV_9160_11(sc))
		sc->addac = &ar9160_1_1_addac;
	else if (AR_SREV_9160_10_OR_LATER(sc))
		sc->addac = &ar9160_1_0_addac;
	else if (AR_SREV_5416_22_OR_LATER(sc))
		sc->addac = &ar5416_2_2_addac;
	else
		sc->addac = &ar5416_2_1_addac;
}

void
ar5416_swap_rom(struct athn_softc *sc)
{
	struct ar5416_eeprom *eep = sc->eep;
	struct ar5416_modal_eep_header *modal;
	int i, j;

	for (i = 0; i < 2; i++) {	/* Dual-band. */
		modal = &eep->modalHeader[i];

		modal->antCtrlCommon = swap32(modal->antCtrlCommon);
		for (j = 0; j < AR5416_MAX_CHAINS; j++) {
			modal->antCtrlChain[j] =
			    swap32(modal->antCtrlChain[j]);
		}
		for (j = 0; j < AR_EEPROM_MODAL_SPURS; j++) {
			modal->spurChans[j].spurChan =
			    swap16(modal->spurChans[j].spurChan);
		}
	}
}

const struct ar_spur_chan *
ar5416_get_spur_chans(struct athn_softc *sc, int is2ghz)
{
	const struct ar5416_eeprom *eep = sc->eep;

	return (eep->modalHeader[is2ghz].spurChans);
}

int
ar5416_set_synth(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	uint32_t phy, reg;
	uint32_t freq = c->ic_freq;
	uint8_t chansel;

	phy = 0;
	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		if (((freq - 2192) % 5) == 0) {
			chansel = ((freq - 672) * 2 - 3040) / 10;
		} else if (((freq - 2224) % 5) == 0) {
			chansel = ((freq - 704) * 2 - 3040) / 10;
			phy |= AR5416_BMODE_SYNTH;
		} else
			return (EINVAL);
		chansel <<= 2;

		reg = AR_READ(sc, AR_PHY_CCK_TX_CTRL);
		if (freq == 2484)	/* Channel 14. */
			reg |= AR_PHY_CCK_TX_CTRL_JAPAN;
		else
			reg &= ~AR_PHY_CCK_TX_CTRL_JAPAN;
		AR_WRITE(sc, AR_PHY_CCK_TX_CTRL, reg);

		/* Fix for orientation sensitivity issue. */
		if (AR_SREV_5416(sc))
			ar5416_force_bias(sc, c);
	} else {
		if (freq >= 5120 && (freq % 20) == 0) {
			chansel = (freq - 4800) / 20;
			chansel <<= 2;
			phy |= SM(AR5416_AMODE_REFSEL, 2);
		} else if ((freq % 10) == 0) {
			chansel = (freq - 4800) / 10;
			chansel <<= 1;
			if (AR_SREV_9160_10_OR_LATER(sc))
				phy |= SM(AR5416_AMODE_REFSEL, 1);
			else
				phy |= SM(AR5416_AMODE_REFSEL, 2);
		} else if ((freq % 5) == 0) {
			chansel = (freq - 4800) / 5;
			phy |= SM(AR5416_AMODE_REFSEL, 2);
		} else
			return (EINVAL);
	}
	chansel = ar5416_reverse_bits(chansel, 8);
	phy |= chansel << 8 | 1 << 5 | 1;
	DPRINTFN(4, ("AR_PHY(0x37)=0x%08x\n", phy));
	AR_WRITE(sc, AR_PHY(0x37), phy);
	return (0);
}

void
ar5416_init_from_rom(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	static const uint32_t chainoffset[] = { 0x0000, 0x2000, 0x1000 };
	const struct ar5416_eeprom *eep = sc->eep;
	const struct ar5416_modal_eep_header *modal;
	uint32_t reg, offset;
	uint8_t txRxAtten;
	int i;

	modal = &eep->modalHeader[IEEE80211_IS_CHAN_2GHZ(c)];

	AR_WRITE(sc, AR_PHY_SWITCH_COM, modal->antCtrlCommon);

	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		if (AR_SREV_5416_20_OR_LATER(sc) &&
		    (sc->rxchainmask == 0x5 || sc->txchainmask == 0x5))
			offset = chainoffset[i];
		else
			offset = i * 0x1000;

		AR_WRITE(sc, AR_PHY_SWITCH_CHAIN_0 + offset,
		    modal->antCtrlChain[i]);

		reg = AR_READ(sc, AR_PHY_TIMING_CTRL4_0 + offset);
		reg = RW(reg, AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF,
		    modal->iqCalICh[i]);
		reg = RW(reg, AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF,
		    modal->iqCalQCh[i]);
		AR_WRITE(sc, AR_PHY_TIMING_CTRL4_0 + offset, reg);

		if (i > 0 && !AR_SREV_5416_20_OR_LATER(sc))
			continue;

		if (sc->eep_rev >= AR_EEP_MINOR_VER_3) {
			reg = AR_READ(sc, AR_PHY_GAIN_2GHZ + offset);
			reg = RW(reg, AR_PHY_GAIN_2GHZ_BSW_MARGIN,
			    modal->bswMargin[i]);
			reg = RW(reg, AR_PHY_GAIN_2GHZ_BSW_ATTEN,
			    modal->bswAtten[i]);
			AR_WRITE(sc, AR_PHY_GAIN_2GHZ + offset, reg);
		}
		if (sc->eep_rev >= AR_EEP_MINOR_VER_3)
			txRxAtten = modal->txRxAttenCh[i];
		else	/* Workaround for ROM versions < 14.3. */
			txRxAtten = IEEE80211_IS_CHAN_2GHZ(c) ? 23 : 44;
		reg = AR_READ(sc, AR_PHY_RXGAIN + offset);
		reg = RW(reg, AR_PHY_RXGAIN_TXRX_ATTEN, txRxAtten);
		AR_WRITE(sc, AR_PHY_RXGAIN + offset, reg);

		reg = AR_READ(sc, AR_PHY_GAIN_2GHZ + offset);
		reg = RW(reg, AR_PHY_GAIN_2GHZ_RXTX_MARGIN,
		    modal->rxTxMarginCh[i]);
		AR_WRITE(sc, AR_PHY_GAIN_2GHZ + offset, reg);
	}
	reg = AR_READ(sc, AR_PHY_SETTLING);
	reg = RW(reg, AR_PHY_SETTLING_SWITCH, modal->switchSettling);
	AR_WRITE(sc, AR_PHY_SETTLING, reg);

	reg = AR_READ(sc, AR_PHY_DESIRED_SZ);
	reg = RW(reg, AR_PHY_DESIRED_SZ_ADC, modal->adcDesiredSize);
	reg = RW(reg, AR_PHY_DESIRED_SZ_PGA, modal->pgaDesiredSize);
	AR_WRITE(sc, AR_PHY_DESIRED_SZ, reg);

	reg =  SM(AR_PHY_RF_CTL4_TX_END_XPAA_OFF, modal->txEndToXpaOff);
	reg |= SM(AR_PHY_RF_CTL4_TX_END_XPAB_OFF, modal->txEndToXpaOff);
	reg |= SM(AR_PHY_RF_CTL4_FRAME_XPAA_ON, modal->txFrameToXpaOn);
	reg |= SM(AR_PHY_RF_CTL4_FRAME_XPAB_ON, modal->txFrameToXpaOn);
	AR_WRITE(sc, AR_PHY_RF_CTL4, reg);

	reg = AR_READ(sc, AR_PHY_RF_CTL3);
	reg = RW(reg, AR_PHY_TX_END_TO_A2_RX_ON, modal->txEndToRxOn);
	AR_WRITE(sc, AR_PHY_RF_CTL3, reg);

	reg = AR_READ(sc, AR_PHY_CCA(0));
	reg = RW(reg, AR_PHY_CCA_THRESH62, modal->thresh62);
	AR_WRITE(sc, AR_PHY_CCA(0), reg);

	reg = AR_READ(sc, AR_PHY_EXT_CCA(0));
	reg = RW(reg, AR_PHY_EXT_CCA_THRESH62, modal->thresh62);
	AR_WRITE(sc, AR_PHY_EXT_CCA(0), reg);

	if (sc->eep_rev >= AR_EEP_MINOR_VER_2) {
		reg = AR_READ(sc, AR_PHY_RF_CTL2);
		reg = RW(reg, AR_PHY_TX_END_DATA_START,
		    modal->txFrameToDataStart);
		reg = RW(reg, AR_PHY_TX_END_PA_ON, modal->txFrameToPaOn);
		AR_WRITE(sc, AR_PHY_RF_CTL2, reg);
	}
	if (sc->eep_rev >= AR_EEP_MINOR_VER_3 && extc != NULL) {
		/* Overwrite switch settling with HT-40 value. */
		reg = AR_READ(sc, AR_PHY_SETTLING);
		reg = RW(reg, AR_PHY_SETTLING_SWITCH, modal->swSettleHt40);
		AR_WRITE(sc, AR_PHY_SETTLING, reg);
	}
}

int
ar5416_init_calib(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	int ntries;

	if (AR_SREV_9280_10_OR_LATER(sc)) {
		/* XXX Linux tests AR9287?! */
		AR_CLRBITS(sc, AR_PHY_ADC_CTL, AR_PHY_ADC_CTL_OFF_PWDADC);
		AR_SETBITS(sc, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_FLTR_CAL);
	}
	/* Calibrate the AGC. */
	AR_SETBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_CAL);
	/* Poll for offset calibration completion. */
	for (ntries = 0; ntries < 10000; ntries++) {
		if (!(AR_READ(sc, AR_PHY_AGC_CONTROL) &
		    AR_PHY_AGC_CONTROL_CAL))
			break;
		DELAY(10);
	}
	if (ntries == 10000)
		return (ETIMEDOUT);
	if (AR_SREV_9280_10_OR_LATER(sc)) {
		AR_SETBITS(sc, AR_PHY_ADC_CTL, AR_PHY_ADC_CTL_OFF_PWDADC);
		AR_CLRBITS(sc, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_FLTR_CAL);
	}
	return (0);
}

void
ar5416_get_pdadcs(struct athn_softc *sc, struct ieee80211_channel *c,
    int chain, int nxpdgains, uint8_t overlap, uint8_t *boundaries,
    uint8_t *pdadcs)
{
	const struct ar5416_eeprom *eep = sc->eep;
	const struct ar5416_cal_data_per_freq *pierdata;
	const uint8_t *pierfreq;
	struct athn_pier lopier, hipier;
	int16_t delta;
	uint8_t fbin, pwroff;
	int i, lo, hi, npiers;

	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		pierfreq = eep->calFreqPier2G;
		pierdata = eep->calPierData2G[chain];
		npiers = AR5416_NUM_2G_CAL_PIERS;
	} else {
		pierfreq = eep->calFreqPier5G;
		pierdata = eep->calPierData5G[chain];
		npiers = AR5416_NUM_5G_CAL_PIERS;
	}
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
	    AR5416_PD_GAIN_ICEPTS, overlap, boundaries, pdadcs);

	if (!AR_SREV_9280_20_OR_LATER(sc))
		return;

	if (sc->eep_rev >= AR_EEP_MINOR_VER_21)
		pwroff = eep->baseEepHeader.pwrTableOffset;
	else
		pwroff = AR_PWR_TABLE_OFFSET_DB;
	delta = (pwroff - AR_PWR_TABLE_OFFSET_DB) * 2;	/* In half dB. */

	/* Change the original gain boundaries setting. */
	for (i = 0; i < nxpdgains; i++) {
		/* XXX Possible overflows? */
		boundaries[i] -= delta;
		if (boundaries[i] > AR_MAX_RATE_POWER - overlap)
			boundaries[i] = AR_MAX_RATE_POWER - overlap;
	}
	if (delta != 0) {
		/* Shift the PDADC table to start at the new offset. */
		for (i = 0; i < AR_NUM_PDADC_VALUES; i++)
			pdadcs[i] = pdadcs[MIN(i + delta,
			    AR_NUM_PDADC_VALUES - 1)];
	}
}

void
ar5416_set_power_calib(struct athn_softc *sc, struct ieee80211_channel *c)
{
	static const uint32_t chainoffset[] = { 0x0000, 0x2000, 0x1000 };
	const struct ar5416_eeprom *eep = sc->eep;
	const struct ar5416_modal_eep_header *modal;
	uint8_t boundaries[AR_PD_GAINS_IN_MASK];
	uint8_t pdadcs[AR_NUM_PDADC_VALUES];
	uint8_t xpdgains[AR5416_NUM_PD_GAINS];
	uint8_t overlap, txgain;
	uint32_t reg, offset;
	int i, j, nxpdgains;

	modal = &eep->modalHeader[IEEE80211_IS_CHAN_2GHZ(c)];

	if (sc->eep_rev < AR_EEP_MINOR_VER_2) {
		overlap = MS(AR_READ(sc, AR_PHY_TPCRG5),
		    AR_PHY_TPCRG5_PD_GAIN_OVERLAP);
	} else
		overlap = modal->pdGainOverlap;

	if ((sc->flags & ATHN_FLAG_OLPC) && IEEE80211_IS_CHAN_2GHZ(c)) {
		/* XXX not here. */
		sc->pdadc =
		    ((const struct ar_cal_data_per_freq_olpc *)
		     eep->calPierData2G[0])->vpdPdg[0][0];
	}

	nxpdgains = 0;
	memset(xpdgains, 0, sizeof(xpdgains));
	for (i = AR5416_PD_GAINS_IN_MASK - 1; i >= 0; i--) {
		if (nxpdgains >= AR5416_NUM_PD_GAINS)
			break;	/* Can't happen. */
		if (modal->xpdGain & (1 << i))
			xpdgains[nxpdgains++] = i;
	}
	reg = AR_READ(sc, AR_PHY_TPCRG1);
	reg = RW(reg, AR_PHY_TPCRG1_NUM_PD_GAIN, nxpdgains - 1);
	reg = RW(reg, AR_PHY_TPCRG1_PD_GAIN_1, xpdgains[0]);
	reg = RW(reg, AR_PHY_TPCRG1_PD_GAIN_2, xpdgains[1]);
	reg = RW(reg, AR_PHY_TPCRG1_PD_GAIN_3, xpdgains[2]);
	AR_WRITE(sc, AR_PHY_TPCRG1, reg);

	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		if (!(sc->txchainmask & (1 << i)))
			continue;

		if (AR_SREV_5416_20_OR_LATER(sc) &&
		    (sc->rxchainmask == 0x5 || sc->txchainmask == 0x5))
			offset = chainoffset[i];
		else
			offset = i * 0x1000;

		if (sc->flags & ATHN_FLAG_OLPC) {
			ar9280_olpc_get_pdadcs(sc, c, i, boundaries,
			    pdadcs, &txgain);

			reg = AR_READ(sc, AR_PHY_TX_PWRCTRL6_0);
			reg = RW(reg, AR_PHY_TX_PWRCTRL_ERR_EST_MODE, 3);
			AR_WRITE(sc, AR_PHY_TX_PWRCTRL6_0, reg);

			reg = AR_READ(sc, AR_PHY_TX_PWRCTRL6_1);
			reg = RW(reg, AR_PHY_TX_PWRCTRL_ERR_EST_MODE, 3);
			AR_WRITE(sc, AR_PHY_TX_PWRCTRL6_1, reg);

			reg = AR_READ(sc, AR_PHY_TX_PWRCTRL7);
			reg = RW(reg, AR_PHY_TX_PWRCTRL_INIT_TX_GAIN, txgain);
			AR_WRITE(sc, AR_PHY_TX_PWRCTRL7, reg);

			overlap = 6;
		} else {
			ar5416_get_pdadcs(sc, c, i, nxpdgains, overlap,
			    boundaries, pdadcs);
		}
		/* Write boundaries. */
		if (i == 0 || AR_SREV_5416_20_OR_LATER(sc)) {
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
	}
}

void
ar5416_set_txpower(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	const struct ar5416_eeprom *eep = sc->eep;
	const struct ar5416_modal_eep_header *modal;
	uint8_t tpow_cck[4], tpow_ofdm[4];
	uint8_t tpow_cck_ext[4], tpow_ofdm_ext[4];
	uint8_t tpow_ht20[8], tpow_ht40[8];
	uint8_t ht40inc;
	int16_t pwr = 0, pwroff, max_ant_gain, power[ATHN_POWER_COUNT];
	uint8_t cckinc;
	int i;

	ar5416_set_power_calib(sc, c);

	modal = &eep->modalHeader[IEEE80211_IS_CHAN_2GHZ(c)];

	/* Compute transmit power reduction due to antenna gain. */
	max_ant_gain = MAX(modal->antennaGainCh[0], modal->antennaGainCh[1]);
	max_ant_gain = MAX(modal->antennaGainCh[2], max_ant_gain);
	/* XXX */

	/*
	 * Reduce scaled power by number of active chains to get per-chain
	 * transmit power level.
	 */
	if (sc->ntxchains == 2)
		pwr -= AR_PWR_DECREASE_FOR_2_CHAIN;
	else if (sc->ntxchains == 3)
		pwr -= AR_PWR_DECREASE_FOR_3_CHAIN;
	if (pwr < 0)
		pwr = 0;

	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		/* Get CCK target powers. */
		ar5008_get_lg_tpow(sc, c, AR_CTL_11B, eep->calTargetPowerCck,
		    AR5416_NUM_2G_CCK_TARGET_POWERS, tpow_cck);

		/* Get OFDM target powers. */
		ar5008_get_lg_tpow(sc, c, AR_CTL_11G, eep->calTargetPower2G,
		    AR5416_NUM_2G_20_TARGET_POWERS, tpow_ofdm);

		/* Get HT-20 target powers. */
		ar5008_get_ht_tpow(sc, c, AR_CTL_2GHT20,
		    eep->calTargetPower2GHT20, AR5416_NUM_2G_20_TARGET_POWERS,
		    tpow_ht20);

		if (extc != NULL) {
			/* Get HT-40 target powers. */
			ar5008_get_ht_tpow(sc, c, AR_CTL_2GHT40,
			    eep->calTargetPower2GHT40,
			    AR5416_NUM_2G_40_TARGET_POWERS, tpow_ht40);

			/* Get secondary channel CCK target powers. */
			ar5008_get_lg_tpow(sc, extc, AR_CTL_11B,
			    eep->calTargetPowerCck,
			    AR5416_NUM_2G_CCK_TARGET_POWERS, tpow_cck_ext);

			/* Get secondary channel OFDM target powers. */
			ar5008_get_lg_tpow(sc, extc, AR_CTL_11G,
			    eep->calTargetPower2G,
			    AR5416_NUM_2G_20_TARGET_POWERS, tpow_ofdm_ext);
		}
	} else {
		/* Get OFDM target powers. */
		ar5008_get_lg_tpow(sc, c, AR_CTL_11A, eep->calTargetPower5G,
		    AR5416_NUM_5G_20_TARGET_POWERS, tpow_ofdm);

		/* Get HT-20 target powers. */
		ar5008_get_ht_tpow(sc, c, AR_CTL_5GHT20,
		    eep->calTargetPower5GHT20, AR5416_NUM_5G_20_TARGET_POWERS,
		    tpow_ht20);

		if (extc != NULL) {
			/* Get HT-40 target powers. */
			ar5008_get_ht_tpow(sc, c, AR_CTL_5GHT40,
			    eep->calTargetPower5GHT40,
			    AR5416_NUM_5G_40_TARGET_POWERS, tpow_ht40);

			/* Get secondary channel OFDM target powers. */
			ar5008_get_lg_tpow(sc, extc, AR_CTL_11A,
			    eep->calTargetPower5G,
			    AR5416_NUM_5G_20_TARGET_POWERS, tpow_ofdm_ext);
		}
	}

	/* Compute CCK/OFDM delta. */
	cckinc = (sc->flags & ATHN_FLAG_OLPC) ? -2 : 0;

	memset(power, 0, sizeof(power));
	/* Shuffle target powers across transmit rates. */
	power[ATHN_POWER_OFDM6 ] =
	power[ATHN_POWER_OFDM9 ] =
	power[ATHN_POWER_OFDM12] =
	power[ATHN_POWER_OFDM18] =
	power[ATHN_POWER_OFDM24] = tpow_ofdm[0];
	power[ATHN_POWER_OFDM36] = tpow_ofdm[1];
	power[ATHN_POWER_OFDM48] = tpow_ofdm[2];
	power[ATHN_POWER_OFDM54] = tpow_ofdm[3];
	power[ATHN_POWER_XR    ] = tpow_ofdm[0];
	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		power[ATHN_POWER_CCK1_LP ] = tpow_cck[0] + cckinc;
		power[ATHN_POWER_CCK2_LP ] =
		power[ATHN_POWER_CCK2_SP ] = tpow_cck[1] + cckinc;
		power[ATHN_POWER_CCK55_LP] =
		power[ATHN_POWER_CCK55_SP] = tpow_cck[2] + cckinc;
		power[ATHN_POWER_CCK11_LP] =
		power[ATHN_POWER_CCK11_SP] = tpow_cck[3] + cckinc;
	}
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
		power[ATHN_POWER_CCK_DUP ] = tpow_ht40[0] + cckinc;
		power[ATHN_POWER_OFDM_EXT] = tpow_ofdm_ext[0];
		if (IEEE80211_IS_CHAN_2GHZ(c))
			power[ATHN_POWER_CCK_EXT] = tpow_cck_ext[0] + cckinc;
	}

	if (AR_SREV_9280_10_OR_LATER(sc)) {
		if (sc->eep_rev >= AR_EEP_MINOR_VER_21)
			pwroff = eep->baseEepHeader.pwrTableOffset;
		else
			pwroff = AR_PWR_TABLE_OFFSET_DB;
		for (i = 0; i < ATHN_POWER_COUNT; i++)
			power[i] -= pwroff * 2;	/* In half dB. */
	}
	for (i = 0; i < ATHN_POWER_COUNT; i++) {
		if (power[i] > AR_MAX_RATE_POWER)
			power[i] = AR_MAX_RATE_POWER;
	}

	/* Write transmit power values to hardware. */
	ar5008_write_txpower(sc, power);

	/*
	 * Write transmit power subtraction for dynamic chain changing
	 * and per-packet transmit power.
	 */
	AR_WRITE(sc, AR_PHY_POWER_TX_SUB,
	    (modal->pwrDecreaseFor3Chain & 0x3f) << 6 |
	    (modal->pwrDecreaseFor2Chain & 0x3f));
}

void
ar5416_spur_mitigate(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	const struct ar_spur_chan *spurchans;
	int i, spur, bin, spur_delta_phase, spur_freq_sd;

	spurchans = sc->ops.get_spur_chans(sc, IEEE80211_IS_CHAN_2GHZ(c));
	for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
		spur = spurchans[i].spurChan;
		if (spur == AR_NO_SPUR)
			return; /* XXX disable if it was enabled! */
		spur -= c->ic_freq * 10;
		/* Verify range +/-9.5MHz */
		if (abs(spur) < 95)
			break;
	}
	if (i == AR_EEPROM_MODAL_SPURS)
		return; /* XXX disable if it was enabled! */
	DPRINTFN(2, ("enabling spur mitigation\n"));

	AR_SETBITS(sc, AR_PHY_TIMING_CTRL4_0,
	    AR_PHY_TIMING_CTRL4_ENABLE_SPUR_RSSI |
	    AR_PHY_TIMING_CTRL4_ENABLE_SPUR_FILTER |
	    AR_PHY_TIMING_CTRL4_ENABLE_CHAN_MASK |
	    AR_PHY_TIMING_CTRL4_ENABLE_PILOT_MASK);

	AR_WRITE(sc, AR_PHY_SPUR_REG,
	    AR_PHY_SPUR_REG_MASK_RATE_CNTL |
	    AR_PHY_SPUR_REG_ENABLE_MASK_PPM |
	    AR_PHY_SPUR_REG_MASK_RATE_SELECT |
	    AR_PHY_SPUR_REG_ENABLE_VIT_SPUR_RSSI |
	    SM(AR_PHY_SPUR_REG_SPUR_RSSI_THRESH, AR_SPUR_RSSI_THRESH));

	spur_delta_phase = (spur * 524288) / 100;
	if (IEEE80211_IS_CHAN_2GHZ(c))
		spur_freq_sd = (spur * 2048) / 440;
	else
		spur_freq_sd = (spur * 2048) / 400;

	AR_WRITE(sc, AR_PHY_TIMING11,
	    AR_PHY_TIMING11_USE_SPUR_IN_AGC |
	    SM(AR_PHY_TIMING11_SPUR_FREQ_SD, spur_freq_sd) |
	    SM(AR_PHY_TIMING11_SPUR_DELTA_PHASE, spur_delta_phase));

	bin = spur * 32;
	ar5008_set_viterbi_mask(sc, bin);
}

uint8_t
ar5416_reverse_bits(uint8_t v, int nbits)
{
	KASSERT(nbits <= 8);
	v = ((v >> 1) & 0x55) | ((v & 0x55) << 1);
	v = ((v >> 2) & 0x33) | ((v & 0x33) << 2);
	v = ((v >> 4) & 0x0f) | ((v & 0x0f) << 4);
	return (v >> (8 - nbits));
}

uint8_t
ar5416_get_rf_rev(struct athn_softc *sc)
{
	uint8_t rev, reg;
	int i;

	/* Allow access to analog chips. */
	AR_WRITE(sc, AR_PHY(0), 0x00000007);

	AR_WRITE(sc, AR_PHY(0x36), 0x00007058);
	for (i = 0; i < 8; i++)
		AR_WRITE(sc, AR_PHY(0x20), 0x00010000);
	reg = (AR_READ(sc, AR_PHY(256)) >> 24) & 0xff;
	reg = (reg & 0xf0) >> 4 | (reg & 0x0f) << 4;

	rev = ar5416_reverse_bits(reg, 8);
	if ((rev & AR_RADIO_SREV_MAJOR) == 0)
		rev = AR_RAD5133_SREV_MAJOR;
	return (rev);
}

/*
 * Replace bits "off" to "off+nbits-1" in column "col" with the specified
 * value.
 */
void
ar5416_rw_rfbits(uint32_t *buf, int col, int off, uint32_t val, int nbits)
{
	int idx, bit;

	KASSERT(off >= 1 && col < 4 && nbits <= 32);

	off--;	/* Starts at 1. */
	while (nbits-- > 0) {
		idx = off / 8;
		bit = off % 8;
		buf[idx] &= ~(1 << (bit + col * 8));
		buf[idx] |= ((val >> nbits) & 1) << (bit + col * 8);
		off++;
	}
}

/*
 * Overwrite db and ob based on ROM settings.
 */
void
ar5416_rw_bank6tpc(struct athn_softc *sc, struct ieee80211_channel *c,
    uint32_t *rwbank6tpc)
{
	const struct ar5416_eeprom *eep = sc->eep;
	const struct ar5416_modal_eep_header *modal;

	if (IEEE80211_IS_CHAN_5GHZ(c)) {
		modal = &eep->modalHeader[0];
		/* 5GHz db in column 0, bits [200-202]. */
		ar5416_rw_rfbits(rwbank6tpc, 0, 200, modal->db, 3);
		/* 5GHz ob in column 0, bits [203-205]. */
		ar5416_rw_rfbits(rwbank6tpc, 0, 203, modal->ob, 3);
	} else {
		modal = &eep->modalHeader[1];
		/* 2GHz db in column 0, bits [194-196]. */
		ar5416_rw_rfbits(rwbank6tpc, 0, 194, modal->db, 3);
		/* 2GHz ob in column 0, bits [197-199]. */
		ar5416_rw_rfbits(rwbank6tpc, 0, 197, modal->ob, 3);
	}
}

/*
 * Program analog RF.
 */
void
ar5416_rf_reset(struct athn_softc *sc, struct ieee80211_channel *c)
{
	const uint32_t *bank6tpc;
	int i;

	/* Bank 0. */
	AR_WRITE(sc, 0x98b0, 0x1e5795e5);
	AR_WRITE(sc, 0x98e0, 0x02008020);

	/* Bank 1. */
	AR_WRITE(sc, 0x98b0, 0x02108421);
	AR_WRITE(sc, 0x98ec, 0x00000008);

	/* Bank 2. */
	AR_WRITE(sc, 0x98b0, 0x0e73ff17);
	AR_WRITE(sc, 0x98e0, 0x00000420);

	/* Bank 3. */
	if (IEEE80211_IS_CHAN_5GHZ(c))
		AR_WRITE(sc, 0x98f0, 0x01400018);
	else
		AR_WRITE(sc, 0x98f0, 0x01c00018);

	/* Select the Bank 6 TPC values to use. */
	if (AR_SREV_9160_10_OR_LATER(sc))
		bank6tpc = ar9160_bank6tpc_vals;
	else
		bank6tpc = ar5416_bank6tpc_vals;
	if (sc->eep_rev >= AR_EEP_MINOR_VER_2) {
		uint32_t *rwbank6tpc = sc->rwbuf;

		/* Copy values from .rodata to writable buffer. */
		memcpy(rwbank6tpc, bank6tpc, 32 * sizeof(uint32_t));
		ar5416_rw_bank6tpc(sc, c, rwbank6tpc);
		bank6tpc = rwbank6tpc;
	}
	/* Bank 6 TPC. */
	for (i = 0; i < 32; i++)
		AR_WRITE(sc, 0x989c, bank6tpc[i]);
	if (IEEE80211_IS_CHAN_5GHZ(c))
		AR_WRITE(sc, 0x98d0, 0x0000000f);
	else
		AR_WRITE(sc, 0x98d0, 0x0010000f);

	/* Bank 7. */
	AR_WRITE(sc, 0x989c, 0x00000500);
	AR_WRITE(sc, 0x989c, 0x00000800);
	AR_WRITE(sc, 0x98cc, 0x0000000e);
}

void
ar5416_reset_bb_gain(struct athn_softc *sc, struct ieee80211_channel *c)
{
	const uint32_t *pvals;
	int i;

	if (IEEE80211_IS_CHAN_2GHZ(c))
		pvals = ar5416_bb_rfgain_vals_2g;
	else
		pvals = ar5416_bb_rfgain_vals_5g;
	for (i = 0; i < 64; i++)
		AR_WRITE(sc, AR_PHY_BB_RFGAIN(i), pvals[i]);
}

/*
 * Fix orientation sensitivity issue on AR5416/2GHz by increasing
 * rf_pwd_icsyndiv.
 */
void
ar5416_force_bias(struct athn_softc *sc, struct ieee80211_channel *c)
{
	uint32_t *rwbank6 = sc->rwbuf;
	uint8_t bias;
	int i;

	KASSERT(IEEE80211_IS_CHAN_2GHZ(c));

	/* Copy values from .rodata to writable buffer. */
	memcpy(rwbank6, ar5416_bank6_vals, sizeof(ar5416_bank6_vals));

	if (c->ic_freq < 2412)
		bias = 0;
	else if (c->ic_freq < 2422)
		bias = 1;
	else
		bias = 2;
	ar5416_reverse_bits(bias, 3);

	/* Overwrite "rf_pwd_icsyndiv" (column 3, bits [181-183].) */
	ar5416_rw_rfbits(rwbank6, 3, 181, bias, 3);

	/* Write Bank 6. */
	for (i = 0; i < 32; i++)
		AR_WRITE(sc, 0x989c, rwbank6[i]);
	AR_WRITE(sc, 0x98d0, 0x0010000f);
}

/*
 * Overwrite XPA bias level based on ROM setting.
 */
void
ar9160_rw_addac(struct athn_softc *sc, struct ieee80211_channel *c,
    uint32_t *addac)
{
	struct ar5416_eeprom *eep = sc->eep;
	struct ar5416_modal_eep_header *modal;
	uint8_t fbin, bias;
	int i;

	/* XXX xpaBiasLvlFreq values have not been endian-swapped? */

	/* Get the XPA bias level to use for the specified channel. */
	modal = &eep->modalHeader[IEEE80211_IS_CHAN_2GHZ(c)];
	if (modal->xpaBiasLvl == 0xff) {
		bias = modal->xpaBiasLvlFreq[0] >> 14;
		fbin = athn_chan2fbin(c);
		for (i = 1; i < 3; i++) {
			if (modal->xpaBiasLvlFreq[i] == 0)
				break;
			if ((modal->xpaBiasLvlFreq[i] & 0xff) < fbin)
				break;
			bias = modal->xpaBiasLvlFreq[i] >> 14;
		}
	} else
		bias = modal->xpaBiasLvl & 0x3;

	bias = ar5416_reverse_bits(bias, 2);	/* Put in host bit-order. */
	DPRINTFN(4, ("bias level=%d\n", bias));
	if (IEEE80211_IS_CHAN_2GHZ(c))
		ar5416_rw_rfbits(addac, 0, 60, bias, 2);
	else
		ar5416_rw_rfbits(addac, 0, 55, bias, 2);
}

void
ar5416_reset_addac(struct athn_softc *sc, struct ieee80211_channel *c)
{
	const struct athn_addac *addac = sc->addac;
	const uint32_t *pvals;
	int i;

	if (AR_SREV_9160(sc) && sc->eep_rev >= AR_EEP_MINOR_VER_7) {
		uint32_t *rwaddac = sc->rwbuf;

		/* Copy values from .rodata to writable buffer. */
		memcpy(rwaddac, addac->vals, addac->nvals * sizeof(uint32_t));
		ar9160_rw_addac(sc, c, rwaddac);
		pvals = rwaddac;
	} else
		pvals = addac->vals;
	for (i = 0; i < addac->nvals; i++)
		AR_WRITE(sc, 0x989c, pvals[i]);
	AR_WRITE(sc, 0x98cc, 0);	/* Finalize. */
}

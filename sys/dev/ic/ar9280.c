/*	$OpenBSD: ar9280.c,v 1.28 2021/04/15 18:25:43 stsp Exp $	*/

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
 * Routines for AR9220, AR9223, AR9280 and AR9281 chipsets.
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
#include <dev/ic/ar5416reg.h>	/* We share the ROM layout. */
#include <dev/ic/ar9280reg.h>

int	ar9280_attach(struct athn_softc *);
void	ar9280_setup(struct athn_softc *);
int	ar9280_set_synth(struct athn_softc *, struct ieee80211_channel *,
	    struct ieee80211_channel *);
void	ar9280_init_from_rom(struct athn_softc *, struct ieee80211_channel *,
	    struct ieee80211_channel *);
void	ar9280_spur_mitigate(struct athn_softc *, struct ieee80211_channel *,
	    struct ieee80211_channel *);
void	ar9280_olpc_get_pdadcs(struct athn_softc *,
	    struct ieee80211_channel *, int, uint8_t *, uint8_t *, uint8_t *);
void	ar9280_reset_rx_gain(struct athn_softc *, struct ieee80211_channel *);
void	ar9280_reset_tx_gain(struct athn_softc *, struct ieee80211_channel *);
void	ar9280_olpc_init(struct athn_softc *);
void	ar9280_olpc_temp_compensation(struct athn_softc *);

/* Extern functions. */
uint8_t	athn_chan2fbin(struct ieee80211_channel *);
void	athn_get_pier_ival(uint8_t, const uint8_t *, int, int *, int *);
int	ar5008_attach(struct athn_softc *);
void	ar5008_set_viterbi_mask(struct athn_softc *, int);
void	ar5416_swap_rom(struct athn_softc *);
void	ar5416_set_txpower(struct athn_softc *, struct ieee80211_channel *,
	    struct ieee80211_channel *);
const struct ar_spur_chan *
	ar5416_get_spur_chans(struct athn_softc *, int);


int
ar9280_attach(struct athn_softc *sc)
{
	sc->eep_base = AR5416_EEP_START_LOC;
	sc->eep_size = sizeof(struct ar5416_eeprom);
	sc->ngpiopins = (sc->flags & ATHN_FLAG_USB) ? 16 : 10;
	sc->led_pin = 1;
	sc->workaround = AR9280_WA_DEFAULT;
	sc->ops.setup = ar9280_setup;
	sc->ops.swap_rom = ar5416_swap_rom;
	sc->ops.init_from_rom = ar9280_init_from_rom;
	sc->ops.set_txpower = ar5416_set_txpower;
	sc->ops.set_synth = ar9280_set_synth;
	sc->ops.spur_mitigate = ar9280_spur_mitigate;
	sc->ops.get_spur_chans = ar5416_get_spur_chans;
	sc->ops.olpc_init = ar9280_olpc_init;
	sc->ops.olpc_temp_compensation = ar9280_olpc_temp_compensation;
	sc->cca_min_2g = AR9280_PHY_CCA_MIN_GOOD_VAL_2GHZ;
	sc->cca_max_2g = AR9280_PHY_CCA_MAX_GOOD_VAL_2GHZ;
	sc->cca_min_5g = AR9280_PHY_CCA_MIN_GOOD_VAL_5GHZ;
	sc->cca_max_5g = AR9280_PHY_CCA_MAX_GOOD_VAL_5GHZ;
	sc->ini = &ar9280_2_0_ini;
	sc->serdes = &ar9280_2_0_serdes;

	return (ar5008_attach(sc));
}

void
ar9280_setup(struct athn_softc *sc)
{
	const struct ar5416_eeprom *eep = sc->eep;
	uint8_t type;

	/* Determine if open loop power control should be used. */
	if (sc->eep_rev >= AR_EEP_MINOR_VER_19 &&
	    eep->baseEepHeader.openLoopPwrCntl)
		sc->flags |= ATHN_FLAG_OLPC;

	/* Determine if fast PLL clock is supported. */
	if (AR_SREV_9280_20(sc) &&
	    (sc->eep_rev <= AR_EEP_MINOR_VER_16 ||
	     eep->baseEepHeader.fastClk5g))
		sc->flags |= ATHN_FLAG_FAST_PLL_CLOCK;

	/*
	 * Determine if initialization value for AR_AN_TOP2 must be fixed.
	 * This is required for some AR9220 devices such as Ubiquiti SR71-12.
	 */
	if (AR_SREV_9280_20(sc) &&
	    sc->eep_rev > AR_EEP_MINOR_VER_10 &&
	    !eep->baseEepHeader.pwdclkind) {
		DPRINTF(("AR_AN_TOP2 fixup required\n"));
		sc->flags |= ATHN_FLAG_AN_TOP2_FIXUP;
	}

	if (AR_SREV_9280_20(sc)) {
		/* Check if we have a valid rxGainType field in ROM. */
		if (sc->eep_rev >= AR_EEP_MINOR_VER_17) {
			/* Select initialization values based on ROM. */
			type = eep->baseEepHeader.rxGainType;
			DPRINTF(("Rx gain type=0x%x\n", type));
			if (type == AR5416_EEP_RXGAIN_23DB_BACKOFF)
				sc->rx_gain = &ar9280_2_0_rx_gain_23db_backoff;
			else if (type == AR5416_EEP_RXGAIN_13DB_BACKOFF)
				sc->rx_gain = &ar9280_2_0_rx_gain_13db_backoff;
			else
				sc->rx_gain = &ar9280_2_0_rx_gain;
		} else
			sc->rx_gain = &ar9280_2_0_rx_gain;

		/* Check if we have a valid txGainType field in ROM. */
		if (sc->eep_rev >= AR_EEP_MINOR_VER_19) {
			/* Select initialization values based on ROM. */
			type = eep->baseEepHeader.txGainType;
			DPRINTF(("Tx gain type=0x%x\n", type));
			if (type == AR_EEP_TXGAIN_HIGH_POWER)
				sc->tx_gain = &ar9280_2_0_tx_gain_high_power;
			else
				sc->tx_gain = &ar9280_2_0_tx_gain;
		} else
			sc->tx_gain = &ar9280_2_0_tx_gain;
	}
}

int
ar9280_set_synth(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	uint32_t phy, reg, ndiv = 0;
	uint32_t freq = c->ic_freq;

	phy = AR_READ(sc, AR9280_PHY_SYNTH_CONTROL) & ~0x3fffffff;

	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		phy |= (freq << 16) / 15;
		phy |= AR9280_BMODE | AR9280_FRACMODE;

		if (AR_SREV_9287_11_OR_LATER(sc)) {
			/* NB: Magic values from the Linux driver. */
			if (freq == 2484) {	/* Channel 14. */
				/* Japanese regulatory requirements. */
				AR_WRITE(sc, AR_PHY(637), 0x00000000);
				AR_WRITE(sc, AR_PHY(638), 0xefff0301);
				AR_WRITE(sc, AR_PHY(639), 0xca9228ee);
			} else {
				AR_WRITE(sc, AR_PHY(637), 0x00fffeff);
				AR_WRITE(sc, AR_PHY(638), 0x00f5f9ff);
				AR_WRITE(sc, AR_PHY(639), 0xb79f6427);
			}
		} else {
			reg = AR_READ(sc, AR_PHY_CCK_TX_CTRL);
			if (freq == 2484)	/* Channel 14. */
				reg |= AR_PHY_CCK_TX_CTRL_JAPAN;
			else
				reg &= ~AR_PHY_CCK_TX_CTRL_JAPAN;
			AR_WRITE(sc, AR_PHY_CCK_TX_CTRL, reg);
		}
	} else {
		if (AR_SREV_9285_10_OR_LATER(sc) ||
		    sc->eep_rev < AR_EEP_MINOR_VER_22 ||
		    !((struct ar5416_base_eep_header *)sc->eep)->frac_n_5g) {
			if ((freq % 20) == 0) {
				ndiv = (freq * 3) / 60;
				phy |= SM(AR9280_AMODE_REFSEL, 3);
			} else if ((freq % 10) == 0) {
				ndiv = (freq * 6) / 60;
				phy |= SM(AR9280_AMODE_REFSEL, 2);
			}
		}
		if (ndiv != 0) {
			phy |= (ndiv & 0x1ff) << 17;
			phy |= (ndiv & ~0x1ff) * 2;
		} else {
			phy |= (freq << 15) / 15;
			phy |= AR9280_FRACMODE;

			reg = AR_READ(sc, AR_AN_SYNTH9);
			reg = RW(reg, AR_AN_SYNTH9_REFDIVA, 1);
			AR_WRITE(sc, AR_AN_SYNTH9, reg);
		}
	}
	AR_WRITE_BARRIER(sc);
	DPRINTFN(4, ("AR9280_PHY_SYNTH_CONTROL=0x%08x\n", phy));
	AR_WRITE(sc, AR9280_PHY_SYNTH_CONTROL, phy);
	AR_WRITE_BARRIER(sc);
	return (0);
}

void
ar9280_init_from_rom(struct athn_softc *sc, struct ieee80211_channel *c,
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

	for (i = 0; i < AR9280_MAX_CHAINS; i++) {
		if (sc->rxchainmask == 0x5 || sc->txchainmask == 0x5)
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

		if (sc->eep_rev >= AR_EEP_MINOR_VER_3) {
			reg = AR_READ(sc, AR_PHY_GAIN_2GHZ + offset);
			reg = RW(reg, AR_PHY_GAIN_2GHZ_XATTEN1_MARGIN,
			    modal->bswMargin[i]);
			reg = RW(reg, AR_PHY_GAIN_2GHZ_XATTEN1_DB,
			    modal->bswAtten[i]);
			reg = RW(reg, AR_PHY_GAIN_2GHZ_XATTEN2_MARGIN,
			    modal->xatten2Margin[i]);
			reg = RW(reg, AR_PHY_GAIN_2GHZ_XATTEN2_DB,
			    modal->xatten2Db[i]);
			AR_WRITE(sc, AR_PHY_GAIN_2GHZ + offset, reg);
		}
		if (sc->eep_rev >= AR_EEP_MINOR_VER_3)
			txRxAtten = modal->txRxAttenCh[i];
		else	/* Workaround for ROM versions < 14.3. */
			txRxAtten = IEEE80211_IS_CHAN_2GHZ(c) ? 23 : 44;
		reg = AR_READ(sc, AR_PHY_RXGAIN + offset);
		reg = RW(reg, AR9280_PHY_RXGAIN_TXRX_ATTEN,
		    txRxAtten);
		reg = RW(reg, AR9280_PHY_RXGAIN_TXRX_MARGIN,
		    modal->rxTxMarginCh[i]);
		AR_WRITE(sc, AR_PHY_RXGAIN + offset, reg);
	}
	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		reg = AR_READ(sc, AR_AN_RF2G1_CH0);
		reg = RW(reg, AR_AN_RF2G1_CH0_OB, modal->ob);
		reg = RW(reg, AR_AN_RF2G1_CH0_DB, modal->db);
		AR_WRITE(sc, AR_AN_RF2G1_CH0, reg);
		AR_WRITE_BARRIER(sc);
		DELAY(100);

		reg = AR_READ(sc, AR_AN_RF2G1_CH1);
		reg = RW(reg, AR_AN_RF2G1_CH1_OB, modal->ob_ch1);
		reg = RW(reg, AR_AN_RF2G1_CH1_DB, modal->db_ch1);
		AR_WRITE(sc, AR_AN_RF2G1_CH1, reg);
		AR_WRITE_BARRIER(sc);
		DELAY(100);
	} else {
		reg = AR_READ(sc, AR_AN_RF5G1_CH0);
		reg = RW(reg, AR_AN_RF5G1_CH0_OB5, modal->ob);
		reg = RW(reg, AR_AN_RF5G1_CH0_DB5, modal->db);
		AR_WRITE(sc, AR_AN_RF5G1_CH0, reg);
		AR_WRITE_BARRIER(sc);
		DELAY(100);

		reg = AR_READ(sc, AR_AN_RF5G1_CH1);
		reg = RW(reg, AR_AN_RF5G1_CH1_OB5, modal->ob_ch1);
		reg = RW(reg, AR_AN_RF5G1_CH1_DB5, modal->db_ch1);
		AR_WRITE(sc, AR_AN_RF5G1_CH1, reg);
		AR_WRITE_BARRIER(sc);
		DELAY(100);
	}
	reg = AR_READ(sc, AR_AN_TOP2);
	if ((sc->flags & ATHN_FLAG_USB) && IEEE80211_IS_CHAN_5GHZ(c)) {
		/*
		 * Hardcode the output voltage of x-PA bias LDO to the
		 * lowest value for UB94 such that the card doesn't get
		 * too hot.
		 */
		reg = RW(reg, AR_AN_TOP2_XPABIAS_LVL, 0);
	} else
		reg = RW(reg, AR_AN_TOP2_XPABIAS_LVL, modal->xpaBiasLvl);
	if (modal->flagBits & AR5416_EEP_FLAG_LOCALBIAS)
		reg |= AR_AN_TOP2_LOCALBIAS;
	else
		reg &= ~AR_AN_TOP2_LOCALBIAS;
	AR_WRITE(sc, AR_AN_TOP2, reg);
	AR_WRITE_BARRIER(sc);
	DELAY(100);

	reg = AR_READ(sc, AR_PHY_XPA_CFG);
	if (modal->flagBits & AR5416_EEP_FLAG_FORCEXPAON)
		reg |= AR_PHY_FORCE_XPA_CFG;
	else
		reg &= ~AR_PHY_FORCE_XPA_CFG;
	AR_WRITE(sc, AR_PHY_XPA_CFG, reg);

	reg = AR_READ(sc, AR_PHY_SETTLING);
	reg = RW(reg, AR_PHY_SETTLING_SWITCH, modal->switchSettling);
	AR_WRITE(sc, AR_PHY_SETTLING, reg);

	reg = AR_READ(sc, AR_PHY_DESIRED_SZ);
	reg = RW(reg, AR_PHY_DESIRED_SZ_ADC, modal->adcDesiredSize);
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
	reg = RW(reg, AR9280_PHY_CCA_THRESH62, modal->thresh62);
	AR_WRITE(sc, AR_PHY_CCA(0), reg);

	reg = AR_READ(sc, AR_PHY_EXT_CCA0);
	reg = RW(reg, AR_PHY_EXT_CCA0_THRESH62, modal->thresh62);
	AR_WRITE(sc, AR_PHY_EXT_CCA0, reg);

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
	if (sc->eep_rev >= AR_EEP_MINOR_VER_19) {
		reg = AR_READ(sc, AR_PHY_CCK_TX_CTRL);
		reg = RW(reg, AR_PHY_CCK_TX_CTRL_TX_DAC_SCALE_CCK,
		    MS(modal->miscBits, AR5416_EEP_MISC_TX_DAC_SCALE_CCK));
		AR_WRITE(sc, AR_PHY_CCK_TX_CTRL, reg);
	}
	if (AR_SREV_9280_20(sc) &&
	    sc->eep_rev >= AR_EEP_MINOR_VER_20) {
		reg = AR_READ(sc, AR_AN_TOP1);
		if (eep->baseEepHeader.dacLpMode &&
		    (IEEE80211_IS_CHAN_2GHZ(c) ||
		     !eep->baseEepHeader.dacHiPwrMode_5G))
			reg |= AR_AN_TOP1_DACLPMODE;
		else
			reg &= ~AR_AN_TOP1_DACLPMODE;
		AR_WRITE(sc, AR_AN_TOP1, reg);
		AR_WRITE_BARRIER(sc);
		DELAY(100);

		reg = AR_READ(sc, AR_PHY_FRAME_CTL);
		reg = RW(reg, AR_PHY_FRAME_CTL_TX_CLIP,
		    MS(modal->miscBits, AR5416_EEP_MISC_TX_CLIP));
		AR_WRITE(sc, AR_PHY_FRAME_CTL, reg);

		reg = AR_READ(sc, AR_PHY_TX_PWRCTRL9);
		reg = RW(reg, AR_PHY_TX_DESIRED_SCALE_CCK,
		    eep->baseEepHeader.desiredScaleCCK);
		AR_WRITE(sc, AR_PHY_TX_PWRCTRL9, reg);
	}
	AR_WRITE_BARRIER(sc);
}

void
ar9280_olpc_get_pdadcs(struct athn_softc *sc, struct ieee80211_channel *c,
    int chain, uint8_t *boundaries, uint8_t *pdadcs, uint8_t *txgain)
{
	const struct ar5416_eeprom *eep = sc->eep;
	const struct ar_cal_data_per_freq_olpc *pierdata;
	const uint8_t *pierfreq;
	uint8_t fbin, pcdac, pwr, idx;
	int i, lo, hi, npiers;

	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		pierfreq = eep->calFreqPier2G;
		pierdata = (const struct ar_cal_data_per_freq_olpc *)
		    eep->calPierData2G[chain];
		npiers = AR5416_NUM_2G_CAL_PIERS;
	} else {
		pierfreq = eep->calFreqPier5G;
		pierdata = (const struct ar_cal_data_per_freq_olpc *)
		    eep->calPierData5G[chain];
		npiers = AR5416_NUM_5G_CAL_PIERS;
	}
	/* Find channel in ROM pier table. */
	fbin = athn_chan2fbin(c);
	athn_get_pier_ival(fbin, pierfreq, npiers, &lo, &hi);

	/* Get average. */
	pwr = (pierdata[lo].pwrPdg[0][0] + pierdata[hi].pwrPdg[0][0]) / 2;
	pwr /= 2;	/* Convert to dB. */

	/* Find power control digital-to-analog converter (PCDAC) value. */
	pcdac = pierdata[hi].pcdac[0][0];
	for (idx = 0; idx < AR9280_TX_GAIN_TABLE_SIZE - 1; idx++)
		if (pcdac <= sc->tx_gain_tbl[idx])
			break;
	*txgain = idx;

	DPRINTFN(3, ("fbin=%d lo=%d hi=%d pwr=%d pcdac=%d txgain=%d\n",
	    fbin, lo, hi, pwr, pcdac, idx));

	/* Fill phase domain analog-to-digital converter (PDADC) table. */
	for (i = 0; i < AR_NUM_PDADC_VALUES; i++)
		pdadcs[i] = (i < pwr) ? 0x00 : 0xff;

	for (i = 0; i < AR_PD_GAINS_IN_MASK; i++)
		boundaries[i] = AR9280_PD_GAIN_BOUNDARY_DEFAULT;
}

void
ar9280_spur_mitigate(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	const struct ar_spur_chan *spurchans;
	int spur, bin, spur_delta_phase, spur_freq_sd, spur_subchannel_sd;
	int spur_off, range, i;

	/* NB: Always clear. */
	AR_CLRBITS(sc, AR_PHY_FORCE_CLKEN_CCK, AR_PHY_FORCE_CLKEN_CCK_MRC_MUX);

	range = (extc != NULL) ? 19 : 10;

	spurchans = sc->ops.get_spur_chans(sc, IEEE80211_IS_CHAN_2GHZ(c));
	for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
		spur = spurchans[i].spurChan;
		if (spur == AR_NO_SPUR)
			return;	/* XXX disable if it was enabled! */
		spur /= 10;
		if (IEEE80211_IS_CHAN_2GHZ(c))
			spur += AR_BASE_FREQ_2GHZ;
		else
			spur += AR_BASE_FREQ_5GHZ;
		spur -= c->ic_freq;
		if (abs(spur) < range)
			break;
	}
	if (i == AR_EEPROM_MODAL_SPURS)
		return;	/* XXX disable if it was enabled! */
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

	if (extc != NULL) {
		spur_delta_phase = (spur * 262144) / 10;
		if (spur < 0) {
			spur_subchannel_sd = 1;
			spur_off = spur + 10;
		} else {
			spur_subchannel_sd = 0;
			spur_off = spur - 10;
		}
	} else {
		spur_delta_phase = (spur * 524288) / 10;
		spur_subchannel_sd = 0;
		spur_off = spur;
	}
	if (IEEE80211_IS_CHAN_2GHZ(c))
		spur_freq_sd = (spur_off * 2048) / 44;
	else
		spur_freq_sd = (spur_off * 2048) / 40;

	AR_WRITE(sc, AR_PHY_TIMING11,
	    AR_PHY_TIMING11_USE_SPUR_IN_AGC |
	    SM(AR_PHY_TIMING11_SPUR_FREQ_SD, spur_freq_sd) |
	    SM(AR_PHY_TIMING11_SPUR_DELTA_PHASE, spur_delta_phase));

	AR_WRITE(sc, AR_PHY_SFCORR_EXT,
	    SM(AR_PHY_SFCORR_SPUR_SUBCHNL_SD, spur_subchannel_sd));
	AR_WRITE_BARRIER(sc);

	bin = spur * 320;
	ar5008_set_viterbi_mask(sc, bin);
}

void
ar9280_reset_rx_gain(struct athn_softc *sc, struct ieee80211_channel *c)
{
	const struct athn_gain *prog = sc->rx_gain;
	const uint32_t *pvals;
	int i;

	if (IEEE80211_IS_CHAN_2GHZ(c))
		pvals = prog->vals_2g;
	else
		pvals = prog->vals_5g;
	for (i = 0; i < prog->nregs; i++)
		AR_WRITE(sc, prog->regs[i], pvals[i]);
}

void
ar9280_reset_tx_gain(struct athn_softc *sc, struct ieee80211_channel *c)
{
	const struct athn_gain *prog = sc->tx_gain;
	const uint32_t *pvals;
	int i;

	if (IEEE80211_IS_CHAN_2GHZ(c))
		pvals = prog->vals_2g;
	else
		pvals = prog->vals_5g;
	for (i = 0; i < prog->nregs; i++)
		AR_WRITE(sc, prog->regs[i], pvals[i]);
}

void
ar9280_olpc_init(struct athn_softc *sc)
{
	uint32_t reg;
	int i;

	/* Save original Tx gain values. */
	for (i = 0; i < AR9280_TX_GAIN_TABLE_SIZE; i++) {
		reg = AR_READ(sc, AR_PHY_TX_GAIN_TBL(i));
		sc->tx_gain_tbl[i] = MS(reg, AR_PHY_TX_GAIN);
	}
	/* Initial Tx gain temperature compensation. */
	sc->tcomp = 0;
}

void
ar9280_olpc_temp_compensation(struct athn_softc *sc)
{
	const struct ar5416_eeprom *eep = sc->eep;
	int8_t pdadc, txgain, tcomp;
	uint32_t reg;
	int i;

	reg = AR_READ(sc, AR_PHY_TX_PWRCTRL4);
	pdadc = MS(reg, AR_PHY_TX_PWRCTRL_PD_AVG_OUT);
	DPRINTFN(3, ("PD Avg Out=%d\n", pdadc));

	if (sc->pdadc == 0 || pdadc == 0)
		return;	/* No frames transmitted yet. */

	/* Compute Tx gain temperature compensation. */
	if (sc->eep_rev >= AR_EEP_MINOR_VER_20 &&
	    eep->baseEepHeader.dacHiPwrMode_5G)
		tcomp = (pdadc - sc->pdadc + 4) / 8;
	else
		tcomp = (pdadc - sc->pdadc + 5) / 10;
	DPRINTFN(3, ("OLPC temp compensation=%d\n", tcomp));

	if (tcomp == sc->tcomp)
		return;	/* Don't rewrite the same values. */
	sc->tcomp = tcomp;

	/* Adjust Tx gain values. */
	for (i = 0; i < AR9280_TX_GAIN_TABLE_SIZE; i++) {
		txgain = sc->tx_gain_tbl[i] - tcomp;
		if (txgain < 0)
			txgain = 0;
		reg = AR_READ(sc, AR_PHY_TX_GAIN_TBL(i));
		reg = RW(reg, AR_PHY_TX_GAIN, txgain);
		AR_WRITE(sc, AR_PHY_TX_GAIN_TBL(i), reg);
	}
	AR_WRITE_BARRIER(sc);
}

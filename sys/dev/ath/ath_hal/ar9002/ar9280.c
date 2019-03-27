/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2008-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2008 Atheros Communications, Inc.
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
 *
 * $FreeBSD$
 */
#include "opt_ah.h"

/*
 * NB: Merlin and later have a simpler RF backend.
 */
#include "ah.h"
#include "ah_internal.h"

#include "ah_eeprom_v14.h"

#include "ar9002/ar9280.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"

#define N(a)    (sizeof(a)/sizeof(a[0]))

struct ar9280State {
	RF_HAL_FUNCS	base;		/* public state, must be first */
	uint16_t	pcdacTable[1];	/* XXX */
};
#define	AR9280(ah)	((struct ar9280State *) AH5212(ah)->ah_rfHal)

static HAL_BOOL ar9280GetChannelMaxMinPower(struct ath_hal *,
	const struct ieee80211_channel *, int16_t *maxPow,int16_t *minPow);
int16_t ar9280GetNfAdjust(struct ath_hal *ah, const HAL_CHANNEL_INTERNAL *c);

static void
ar9280WriteRegs(struct ath_hal *ah, u_int modesIndex, u_int freqIndex,
	int writes)
{
	(void) ath_hal_ini_write(ah, &AH5416(ah)->ah_ini_bb_rfgain,
		freqIndex, writes);
}

/*
 * Take the MHz channel value and set the Channel value
 *
 * ASSUMES: Writes enabled to analog bus
 *
 * Actual Expression,
 *
 * For 2GHz channel, 
 * Channel Frequency = (3/4) * freq_ref * (chansel[8:0] + chanfrac[16:0]/2^17) 
 * (freq_ref = 40MHz)
 *
 * For 5GHz channel,
 * Channel Frequency = (3/2) * freq_ref * (chansel[8:0] + chanfrac[16:0]/2^10)
 * (freq_ref = 40MHz/(24>>amodeRefSel))
 *
 * For 5GHz channels which are 5MHz spaced,
 * Channel Frequency = (3/2) * freq_ref * (chansel[8:0] + chanfrac[16:0]/2^17)
 * (freq_ref = 40MHz)
 */
static HAL_BOOL
ar9280SetChannel(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	uint16_t bMode, fracMode, aModeRefSel = 0;
	uint32_t freq, ndiv, channelSel = 0, channelFrac = 0, reg32 = 0;
	CHAN_CENTERS centers;
	uint32_t refDivA = 24;
	uint8_t frac_n_5g;

	OS_MARK(ah, AH_MARK_SETCHANNEL, chan->ic_freq);

	ar5416GetChannelCenters(ah, chan, &centers);
	freq = centers.synth_center;

	reg32 = OS_REG_READ(ah, AR_PHY_SYNTH_CONTROL);
	reg32 &= 0xc0000000;

	if (ath_hal_eepromGet(ah, AR_EEP_FRAC_N_5G, &frac_n_5g) != HAL_OK)
		frac_n_5g = 0;

	if (freq < 4800) {     /* 2 GHz, fractional mode */
		uint32_t txctl;

		bMode = 1;
		fracMode = 1;
		aModeRefSel = 0;       
		channelSel = (freq * 0x10000)/15;

		txctl = OS_REG_READ(ah, AR_PHY_CCK_TX_CTRL);
		if (freq == 2484) {
			/* Enable channel spreading for channel 14 */
			OS_REG_WRITE(ah, AR_PHY_CCK_TX_CTRL,
			    txctl | AR_PHY_CCK_TX_CTRL_JAPAN);
		} else {
			OS_REG_WRITE(ah, AR_PHY_CCK_TX_CTRL,
			    txctl &~ AR_PHY_CCK_TX_CTRL_JAPAN);
		}     
	} else {
		bMode = 0;
		fracMode = 0;

		switch (frac_n_5g) {
		case 0:
			/*
			 * Enable fractional mode for half/quarter rate
			 * channels.
			 *
			 * This is from the Linux ath9k code, rather than
			 * the Atheros HAL code.
			 */
			if (IEEE80211_IS_CHAN_QUARTER(chan) ||
			    IEEE80211_IS_CHAN_HALF(chan))
				aModeRefSel = 0;
			else if ((freq % 20) == 0) {
				aModeRefSel = 3;
			} else if ((freq % 10) == 0) {
				aModeRefSel = 2;
			}
			if (aModeRefSel) break;
		case 1:
		default:
			aModeRefSel = 0;
			/* Enable 2G (fractional) mode for channels which are 5MHz spaced */

			/*
			 * Workaround for talking on PSB non-5MHz channels;
			 * the pre-Merlin chips only had a 2.5MHz channel
			 * spacing so some channels aren't reachable.

			 *
			 * This interoperates on the quarter rate channels
			 * with the AR5112 and later RF synths.  Please note
			 * that the synthesiser isn't able to completely
			 * accurately represent these frequencies (as the
			 * resolution in this reference is 2.5MHz) and thus
			 * it will be slightly "off centre."  This matches
			 * the same slightly incorrect centre frequency
			 * behaviour that the AR5112 and later channel
			 * selection code has.
			 *
			 * This also interoperates with the AR5416
			 * synthesiser modification for programming
			 * fractional frequencies in 5GHz mode.  However
			 * that modification is also disabled by default.
			 *
			 * This is disabled because it hasn't been tested for
			 * regulatory compliance and neither have the NICs
			 * which would use it.  So if you enable this code,
			 * you must first ensure that you've re-certified the
			 * NICs in question beforehand or you will be
			 * violating your local regulatory rules and breaking
			 * the law.
			 */
#if 0
			if (freq % 5 == 0) {
#endif
				/* Normal */
				fracMode = 1;
				refDivA = 1;
				channelSel = (freq * 0x8000)/15;
#if 0
			} else {
				/* Offset by 500KHz */
				uint32_t f, ch, ch2;

				fracMode = 1;
				refDivA = 1;

				/* Calculate the "adjusted" frequency */
				f = freq - 2;
				ch = (((f - 4800) * 10) / 25) + 1;

				ch2 = ((ch * 25) / 5) + 9600;
				channelSel = (ch2 * 0x4000) / 15;
				//ath_hal_printf(ah,
				//    "%s: freq=%d, ch=%d, ch2=%d, "
				//    "channelSel=%d\n",
				//    __func__, freq, ch, ch2, channelSel);
			}
#endif

			/* RefDivA setting */
			OS_A_REG_RMW_FIELD(ah, AR_AN_SYNTH9,
			    AR_AN_SYNTH9_REFDIVA, refDivA);
		}

		if (!fracMode) {
			ndiv = (freq * (refDivA >> aModeRefSel))/60;
			channelSel =  ndiv & 0x1ff;         
			channelFrac = (ndiv & 0xfffffe00) * 2;
			channelSel = (channelSel << 17) | channelFrac;
		}
	}

	reg32 = reg32 | (bMode << 29) | (fracMode << 28) |
	    (aModeRefSel << 26) | (channelSel);

	OS_REG_WRITE(ah, AR_PHY_SYNTH_CONTROL, reg32);

	AH_PRIVATE(ah)->ah_curchan = chan;

	return AH_TRUE;
}

/*
 * Return a reference to the requested RF Bank.
 */
static uint32_t *
ar9280GetRfBank(struct ath_hal *ah, int bank)
{
	HALDEBUG(ah, HAL_DEBUG_ANY, "%s: unknown RF Bank %d requested\n",
	    __func__, bank);
	return AH_NULL;
}

/*
 * Reads EEPROM header info from device structure and programs
 * all rf registers
 */
static HAL_BOOL
ar9280SetRfRegs(struct ath_hal *ah, const struct ieee80211_channel *chan,
                uint16_t modesIndex, uint16_t *rfXpdGain)
{
	return AH_TRUE;		/* nothing to do */
}

/*
 * Read the transmit power levels from the structures taken from EEPROM
 * Interpolate read transmit power values for this channel
 * Organize the transmit power values into a table for writing into the hardware
 */

static HAL_BOOL
ar9280SetPowerTable(struct ath_hal *ah, int16_t *pPowerMin, int16_t *pPowerMax, 
	const struct ieee80211_channel *chan, uint16_t *rfXpdGain)
{
	return AH_TRUE;
}

#if 0
static int16_t
ar9280GetMinPower(struct ath_hal *ah, EXPN_DATA_PER_CHANNEL_5112 *data)
{
    int i, minIndex;
    int16_t minGain,minPwr,minPcdac,retVal;

    /* Assume NUM_POINTS_XPD0 > 0 */
    minGain = data->pDataPerXPD[0].xpd_gain;
    for (minIndex=0,i=1; i<NUM_XPD_PER_CHANNEL; i++) {
        if (data->pDataPerXPD[i].xpd_gain < minGain) {
            minIndex = i;
            minGain = data->pDataPerXPD[i].xpd_gain;
        }
    }
    minPwr = data->pDataPerXPD[minIndex].pwr_t4[0];
    minPcdac = data->pDataPerXPD[minIndex].pcdac[0];
    for (i=1; i<NUM_POINTS_XPD0; i++) {
        if (data->pDataPerXPD[minIndex].pwr_t4[i] < minPwr) {
            minPwr = data->pDataPerXPD[minIndex].pwr_t4[i];
            minPcdac = data->pDataPerXPD[minIndex].pcdac[i];
        }
    }
    retVal = minPwr - (minPcdac*2);
    return(retVal);
}
#endif

static HAL_BOOL
ar9280GetChannelMaxMinPower(struct ath_hal *ah,
	const struct ieee80211_channel *chan,
	int16_t *maxPow, int16_t *minPow)
{
#if 0
    struct ath_hal_5212 *ahp = AH5212(ah);
    int numChannels=0,i,last;
    int totalD, totalF,totalMin;
    EXPN_DATA_PER_CHANNEL_5112 *data=AH_NULL;
    EEPROM_POWER_EXPN_5112 *powerArray=AH_NULL;

    *maxPow = 0;
    if (IS_CHAN_A(chan)) {
        powerArray = ahp->ah_modePowerArray5112;
        data = powerArray[headerInfo11A].pDataPerChannel;
        numChannels = powerArray[headerInfo11A].numChannels;
    } else if (IS_CHAN_G(chan) || IS_CHAN_108G(chan)) {
        /* XXX - is this correct? Should we also use the same power for turbo G? */
        powerArray = ahp->ah_modePowerArray5112;
        data = powerArray[headerInfo11G].pDataPerChannel;
        numChannels = powerArray[headerInfo11G].numChannels;
    } else if (IS_CHAN_B(chan)) {
        powerArray = ahp->ah_modePowerArray5112;
        data = powerArray[headerInfo11B].pDataPerChannel;
        numChannels = powerArray[headerInfo11B].numChannels;
    } else {
        return (AH_TRUE);
    }
    /* Make sure the channel is in the range of the TP values
     *  (freq piers)
     */
    if ((numChannels < 1) ||
        (chan->channel < data[0].channelValue) ||
        (chan->channel > data[numChannels-1].channelValue))
        return(AH_FALSE);

    /* Linearly interpolate the power value now */
    for (last=0,i=0;
         (i<numChannels) && (chan->channel > data[i].channelValue);
         last=i++);
    totalD = data[i].channelValue - data[last].channelValue;
    if (totalD > 0) {
        totalF = data[i].maxPower_t4 - data[last].maxPower_t4;
        *maxPow = (int8_t) ((totalF*(chan->channel-data[last].channelValue) + data[last].maxPower_t4*totalD)/totalD);

        totalMin = ar9280GetMinPower(ah,&data[i]) - ar9280GetMinPower(ah, &data[last]);
        *minPow = (int8_t) ((totalMin*(chan->channel-data[last].channelValue) + ar9280GetMinPower(ah, &data[last])*totalD)/totalD);
        return (AH_TRUE);
    } else {
        if (chan->channel == data[i].channelValue) {
            *maxPow = data[i].maxPower_t4;
            *minPow = ar9280GetMinPower(ah, &data[i]);
            return(AH_TRUE);
        } else
            return(AH_FALSE);
    }
#else
	*maxPow = *minPow = 0;
	return AH_FALSE;
#endif
}

/*
 * The ordering of nfarray is thus:
 *
 * nfarray[0]: Chain 0 ctl
 * nfarray[1]: Chain 1 ctl
 * nfarray[2]: Chain 2 ctl
 * nfarray[3]: Chain 0 ext
 * nfarray[4]: Chain 1 ext
 * nfarray[5]: Chain 2 ext
 */
static void
ar9280GetNoiseFloor(struct ath_hal *ah, int16_t nfarray[])
{
	int16_t nf;

	nf = MS(OS_REG_READ(ah, AR_PHY_CCA), AR9280_PHY_MINCCA_PWR);
	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	HALDEBUG(ah, HAL_DEBUG_NFCAL,
	    "NF calibrated [ctl] [chain 0] is %d\n", nf);
	nfarray[0] = nf;

	nf = MS(OS_REG_READ(ah, AR_PHY_CH1_CCA), AR9280_PHY_CH1_MINCCA_PWR);
	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	HALDEBUG(ah, HAL_DEBUG_NFCAL,
	    "NF calibrated [ctl] [chain 1] is %d\n", nf);
	nfarray[1] = nf;

	nf = MS(OS_REG_READ(ah, AR_PHY_EXT_CCA), AR9280_PHY_EXT_MINCCA_PWR);
	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	HALDEBUG(ah, HAL_DEBUG_NFCAL,
	    "NF calibrated [ext] [chain 0] is %d\n", nf);
	nfarray[3] = nf;

	nf = MS(OS_REG_READ(ah, AR_PHY_CH1_EXT_CCA), AR9280_PHY_CH1_EXT_MINCCA_PWR);
	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	HALDEBUG(ah, HAL_DEBUG_NFCAL,
	    "NF calibrated [ext] [chain 1] is %d\n", nf);
	nfarray[4] = nf;

        /* Chain 2 - invalid */
        nfarray[2] = 0;
        nfarray[5] = 0;

}

/*
 * Adjust NF based on statistical values for 5GHz frequencies.
 * Stubbed:Not used by Fowl
 */
int16_t
ar9280GetNfAdjust(struct ath_hal *ah, const HAL_CHANNEL_INTERNAL *c)
{
	return 0;
}

/*
 * Free memory for analog bank scratch buffers
 */
static void
ar9280RfDetach(struct ath_hal *ah)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

	HALASSERT(ahp->ah_rfHal != AH_NULL);
	ath_hal_free(ahp->ah_rfHal);
	ahp->ah_rfHal = AH_NULL;
}

HAL_BOOL
ar9280RfAttach(struct ath_hal *ah, HAL_STATUS *status)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	struct ar9280State *priv;

	HALDEBUG(ah, HAL_DEBUG_ATTACH, "%s: attach AR9280 radio\n", __func__);

	HALASSERT(ahp->ah_rfHal == AH_NULL);
	priv = ath_hal_malloc(sizeof(struct ar9280State));
	if (priv == AH_NULL) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: cannot allocate private state\n", __func__);
		*status = HAL_ENOMEM;		/* XXX */
		return AH_FALSE;
	}
	priv->base.rfDetach		= ar9280RfDetach;
	priv->base.writeRegs		= ar9280WriteRegs;
	priv->base.getRfBank		= ar9280GetRfBank;
	priv->base.setChannel		= ar9280SetChannel;
	priv->base.setRfRegs		= ar9280SetRfRegs;
	priv->base.setPowerTable	= ar9280SetPowerTable;
	priv->base.getChannelMaxMinPower = ar9280GetChannelMaxMinPower;
	priv->base.getNfAdjust		= ar9280GetNfAdjust;

	ahp->ah_pcdacTable = priv->pcdacTable;
	ahp->ah_pcdacTableSize = sizeof(priv->pcdacTable);
	ahp->ah_rfHal = &priv->base;
	/*
	 * Set noise floor adjust method; we arrange a
	 * direct call instead of thunking.
	 */
	AH_PRIVATE(ah)->ah_getNfAdjust = priv->base.getNfAdjust;
	AH_PRIVATE(ah)->ah_getNoiseFloor = ar9280GetNoiseFloor;

	return AH_TRUE;
}

static HAL_BOOL
ar9280RfProbe(struct ath_hal *ah)
{
	return (AR_SREV_MERLIN(ah));
}

AH_RF(RF9280, ar9280RfProbe, ar9280RfAttach);

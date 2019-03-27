/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2008 Atheros Communications, Inc.
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

#include "ah.h"
#include "ah_internal.h"

#include "ah_eeprom_v14.h"

#include "ar5416/ar5416.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"

#define N(a)    (sizeof(a)/sizeof(a[0]))

struct ar2133State {
	RF_HAL_FUNCS	base;		/* public state, must be first */
	uint16_t	pcdacTable[1];

	uint32_t	*Bank0Data;
	uint32_t	*Bank1Data;
	uint32_t	*Bank2Data;
	uint32_t	*Bank3Data;
	uint32_t	*Bank6Data;
	uint32_t	*Bank7Data;

	/* NB: Bank*Data storage follows */
};
#define	AR2133(ah)	((struct ar2133State *) AH5212(ah)->ah_rfHal)

#define	ar5416ModifyRfBuffer	ar5212ModifyRfBuffer	/*XXX*/

void	ar5416ModifyRfBuffer(uint32_t *rfBuf, uint32_t reg32,
	    uint32_t numBits, uint32_t firstBit, uint32_t column);

static void
ar2133WriteRegs(struct ath_hal *ah, u_int modesIndex, u_int freqIndex,
	int writes)
{
	(void) ath_hal_ini_write(ah, &AH5416(ah)->ah_ini_bb_rfgain,
		freqIndex, writes);
}

/*
 * Fix on 2.4 GHz band for orientation sensitivity issue by increasing
 * rf_pwd_icsyndiv.
 * 
 * Theoretical Rules:
 *   if 2 GHz band
 *      if forceBiasAuto
 *         if synth_freq < 2412
 *            bias = 0
 *         else if 2412 <= synth_freq <= 2422
 *            bias = 1
 *         else // synth_freq > 2422
 *            bias = 2
 *      else if forceBias > 0
 *         bias = forceBias & 7
 *      else
 *         no change, use value from ini file
 *   else
 *      no change, invalid band
 *
 *  1st Mod:
 *    2422 also uses value of 2
 *    <approved>
 *
 *  2nd Mod:
 *    Less than 2412 uses value of 0, 2412 and above uses value of 2
 */
static void
ar2133ForceBias(struct ath_hal *ah, uint16_t synth_freq)
{
        uint32_t tmp_reg;
        int reg_writes = 0;
        uint32_t new_bias = 0;
	struct ar2133State *priv = AR2133(ah);

	/* XXX this is a bit of a silly check for 2.4ghz channels -adrian */
        if (synth_freq >= 3000)
                return;

        if (synth_freq < 2412)
                new_bias = 0;
        else if (synth_freq < 2422)
                new_bias = 1;
        else
                new_bias = 2;

        /* pre-reverse this field */
        tmp_reg = ath_hal_reverseBits(new_bias, 3);

        HALDEBUG(ah, HAL_DEBUG_ANY, "%s: Force rf_pwd_icsyndiv to %1d on %4d\n",
                  __func__, new_bias, synth_freq);

        /* swizzle rf_pwd_icsyndiv */
        ar5416ModifyRfBuffer(priv->Bank6Data, tmp_reg, 3, 181, 3);

        /* write Bank 6 with new params */
        ath_hal_ini_bank_write(ah, &AH5416(ah)->ah_ini_bank6, priv->Bank6Data, reg_writes);
}

/*
 * Take the MHz channel value and set the Channel value
 *
 * ASSUMES: Writes enabled to analog bus
 */
static HAL_BOOL
ar2133SetChannel(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	uint32_t channelSel  = 0;
	uint32_t bModeSynth  = 0;
	uint32_t aModeRefSel = 0;
	uint32_t reg32       = 0;
	uint16_t freq;
	CHAN_CENTERS centers;
    
	OS_MARK(ah, AH_MARK_SETCHANNEL, chan->ic_freq);
    
	ar5416GetChannelCenters(ah, chan, &centers);
	freq = centers.synth_center;

	if (freq < 4800) {
		uint32_t txctl;

		if (((freq - 2192) % 5) == 0) {
			channelSel = ((freq - 672) * 2 - 3040)/10;
			bModeSynth = 0;
		} else if (((freq - 2224) % 5) == 0) {
			channelSel = ((freq - 704) * 2 - 3040) / 10;
			bModeSynth = 1;
		} else {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: invalid channel %u MHz\n", __func__, freq);
			return AH_FALSE;
		}

		channelSel = (channelSel << 2) & 0xff;
		channelSel = ath_hal_reverseBits(channelSel, 8);

		txctl = OS_REG_READ(ah, AR_PHY_CCK_TX_CTRL);
		if (freq == 2484) {
			/* Enable channel spreading for channel 14 */
			OS_REG_WRITE(ah, AR_PHY_CCK_TX_CTRL,
				txctl | AR_PHY_CCK_TX_CTRL_JAPAN);
		} else {
			OS_REG_WRITE(ah, AR_PHY_CCK_TX_CTRL,
 			txctl &~ AR_PHY_CCK_TX_CTRL_JAPAN);
		}
	/*
	 * Handle programming the RF synth for odd frequencies in the
	 * 4.9->5GHz range.  This matches the programming from the
	 * later model 802.11abg RF synths.
	 *
	 * This interoperates on the quarter rate channels with the
	 * AR5112 and later RF synths.  Please note that the synthesiser
	 * isn't able to completely accurately represent these frequencies
	 * (as the resolution in this reference is 2.5MHz) and thus it will
	 * be slightly "off centre."  This matches the same slightly
	 * incorrect * centre frequency behaviour that the AR5112 and later
	 * channel selection code has.
	 *
	 * This is disabled because it hasn't been tested for regulatory
	 * compliance and neither have the NICs which would use it.
	 * So if you enable this code, you must first ensure that you've
	 * re-certified the NICs in question beforehand or you will be
	 * violating your local regulatory rules and breaking the law.
	 */
#if 0
	} else if (((freq % 5) == 2) && (freq <= 5435)) {
		freq = freq - 2;
		channelSel = ath_hal_reverseBits(
		    (uint32_t) (((freq - 4800) * 10) / 25 + 1), 8);
		/* XXX what about for Howl/Sowl? */
		aModeRefSel = ath_hal_reverseBits(0, 2);
#endif
	} else if ((freq % 20) == 0 && freq >= 5120) {
		channelSel = ath_hal_reverseBits(((freq - 4800) / 20 << 2), 8);
		if (AR_SREV_HOWL(ah) || AR_SREV_SOWL_10_OR_LATER(ah))
			aModeRefSel = ath_hal_reverseBits(3, 2);
		else
			aModeRefSel = ath_hal_reverseBits(1, 2);
	} else if ((freq % 10) == 0) {
		channelSel = ath_hal_reverseBits(((freq - 4800) / 10 << 1), 8);
		if (AR_SREV_HOWL(ah) || AR_SREV_SOWL_10_OR_LATER(ah))
			aModeRefSel = ath_hal_reverseBits(2, 2);
		else
			aModeRefSel = ath_hal_reverseBits(1, 2);
	} else if ((freq % 5) == 0) {
		channelSel = ath_hal_reverseBits((freq - 4800) / 5, 8);
		aModeRefSel = ath_hal_reverseBits(1, 2);
	} else {
		HALDEBUG(ah, HAL_DEBUG_UNMASKABLE,
		    "%s: invalid channel %u MHz\n",
		    __func__, freq);
		return AH_FALSE;
	}

	/* Workaround for hw bug - AR5416 specific */
	if (AR_SREV_OWL(ah) && ah->ah_config.ah_ar5416_biasadj)
		ar2133ForceBias(ah, freq);

	reg32 = (channelSel << 8) | (aModeRefSel << 2) | (bModeSynth << 1) |
		(1 << 5) | 0x1;

	OS_REG_WRITE(ah, AR_PHY(0x37), reg32);

	AH_PRIVATE(ah)->ah_curchan = chan;
	return AH_TRUE;

}

/*
 * Return a reference to the requested RF Bank.
 */
static uint32_t *
ar2133GetRfBank(struct ath_hal *ah, int bank)
{
	struct ar2133State *priv = AR2133(ah);

	HALASSERT(priv != AH_NULL);
	switch (bank) {
	case 1: return priv->Bank1Data;
	case 2: return priv->Bank2Data;
	case 3: return priv->Bank3Data;
	case 6: return priv->Bank6Data;
	case 7: return priv->Bank7Data;
	}
	HALDEBUG(ah, HAL_DEBUG_ANY, "%s: unknown RF Bank %d requested\n",
	    __func__, bank);
	return AH_NULL;
}

/*
 * Reads EEPROM header info from device structure and programs
 * all rf registers
 *
 * REQUIRES: Access to the analog rf device
 */
static HAL_BOOL
ar2133SetRfRegs(struct ath_hal *ah, const struct ieee80211_channel *chan,
                uint16_t modesIndex, uint16_t *rfXpdGain)
{
	struct ar2133State *priv = AR2133(ah);
	int writes;

	HALASSERT(priv);

	/* Setup Bank 0 Write */
	ath_hal_ini_bank_setup(priv->Bank0Data, &AH5416(ah)->ah_ini_bank0, 1);

	/* Setup Bank 1 Write */
	ath_hal_ini_bank_setup(priv->Bank1Data, &AH5416(ah)->ah_ini_bank1, 1);

	/* Setup Bank 2 Write */
	ath_hal_ini_bank_setup(priv->Bank2Data, &AH5416(ah)->ah_ini_bank2, 1);

	/* Setup Bank 3 Write */
	ath_hal_ini_bank_setup(priv->Bank3Data, &AH5416(ah)->ah_ini_bank3, modesIndex);

	/* Setup Bank 6 Write */
	ath_hal_ini_bank_setup(priv->Bank6Data, &AH5416(ah)->ah_ini_bank6, modesIndex);
	
	/* Only the 5 or 2 GHz OB/DB need to be set for a mode */
	if (IEEE80211_IS_CHAN_2GHZ(chan)) {
		HALDEBUG(ah, HAL_DEBUG_EEPROM, "%s: 2ghz: OB_2:%d, DB_2:%d\n",
		    __func__,
		    ath_hal_eepromGet(ah, AR_EEP_OB_2, AH_NULL),
		    ath_hal_eepromGet(ah, AR_EEP_DB_2, AH_NULL));
		ar5416ModifyRfBuffer(priv->Bank6Data,
		    ath_hal_eepromGet(ah, AR_EEP_OB_2, AH_NULL), 3, 197, 0);
		ar5416ModifyRfBuffer(priv->Bank6Data,
		    ath_hal_eepromGet(ah, AR_EEP_DB_2, AH_NULL), 3, 194, 0);
	} else {
		HALDEBUG(ah, HAL_DEBUG_EEPROM, "%s: 5ghz: OB_5:%d, DB_5:%d\n",
		    __func__,
		    ath_hal_eepromGet(ah, AR_EEP_OB_5, AH_NULL),
		    ath_hal_eepromGet(ah, AR_EEP_DB_5, AH_NULL));
		ar5416ModifyRfBuffer(priv->Bank6Data,
		    ath_hal_eepromGet(ah, AR_EEP_OB_5, AH_NULL), 3, 203, 0);
		ar5416ModifyRfBuffer(priv->Bank6Data,
		    ath_hal_eepromGet(ah, AR_EEP_DB_5, AH_NULL), 3, 200, 0);
	}
	/* Setup Bank 7 Setup */
	ath_hal_ini_bank_setup(priv->Bank7Data, &AH5416(ah)->ah_ini_bank7, 1);

	/* Write Analog registers */
	writes = ath_hal_ini_bank_write(ah, &AH5416(ah)->ah_ini_bank0,
	    priv->Bank0Data, 0);
	writes = ath_hal_ini_bank_write(ah, &AH5416(ah)->ah_ini_bank1,
	    priv->Bank1Data, writes);
	writes = ath_hal_ini_bank_write(ah, &AH5416(ah)->ah_ini_bank2,
	    priv->Bank2Data, writes);
	writes = ath_hal_ini_bank_write(ah, &AH5416(ah)->ah_ini_bank3,
	    priv->Bank3Data, writes);
	writes = ath_hal_ini_bank_write(ah, &AH5416(ah)->ah_ini_bank6,
	    priv->Bank6Data, writes);
	(void) ath_hal_ini_bank_write(ah, &AH5416(ah)->ah_ini_bank7,
	    priv->Bank7Data, writes);

	return AH_TRUE;
#undef  RF_BANK_SETUP
}

/*
 * Read the transmit power levels from the structures taken from EEPROM
 * Interpolate read transmit power values for this channel
 * Organize the transmit power values into a table for writing into the hardware
 */

static HAL_BOOL
ar2133SetPowerTable(struct ath_hal *ah, int16_t *pPowerMin, int16_t *pPowerMax, 
	const struct ieee80211_channel *chan, uint16_t *rfXpdGain)
{
	return AH_TRUE;
}

#if 0
static int16_t
ar2133GetMinPower(struct ath_hal *ah, EXPN_DATA_PER_CHANNEL_5112 *data)
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
ar2133GetChannelMaxMinPower(struct ath_hal *ah,
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

        totalMin = ar2133GetMinPower(ah,&data[i]) - ar2133GetMinPower(ah, &data[last]);
        *minPow = (int8_t) ((totalMin*(chan->channel-data[last].channelValue) + ar2133GetMinPower(ah, &data[last])*totalD)/totalD);
        return (AH_TRUE);
    } else {
        if (chan->channel == data[i].channelValue) {
            *maxPow = data[i].maxPower_t4;
            *minPow = ar2133GetMinPower(ah, &data[i]);
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
 * nfarray[0]:	Chain 0 ctl
 * nfarray[1]:	Chain 1 ctl
 * nfarray[2]:	Chain 2 ctl
 * nfarray[3]:	Chain 0 ext
 * nfarray[4]:	Chain 1 ext
 * nfarray[5]:	Chain 2 ext
 */
static void 
ar2133GetNoiseFloor(struct ath_hal *ah, int16_t nfarray[])
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	int16_t nf;

	/*
	 * Blank nf array - some chips may only
	 * have one or two RX chainmasks enabled.
	 */
	nfarray[0] = nfarray[1] = nfarray[2] = 0;
	nfarray[3] = nfarray[4] = nfarray[5] = 0;

	switch (ahp->ah_rx_chainmask) {
        case 0x7:
		nf = MS(OS_REG_READ(ah, AR_PHY_CH2_CCA), AR_PHY_CH2_MINCCA_PWR);
		if (nf & 0x100)
			nf = 0 - ((nf ^ 0x1ff) + 1);
		HALDEBUG(ah, HAL_DEBUG_NFCAL,
		    "NF calibrated [ctl] [chain 2] is %d\n", nf);
		nfarray[2] = nf;

		nf = MS(OS_REG_READ(ah, AR_PHY_CH2_EXT_CCA), AR_PHY_CH2_EXT_MINCCA_PWR);
		if (nf & 0x100)
			nf = 0 - ((nf ^ 0x1ff) + 1);
		HALDEBUG(ah, HAL_DEBUG_NFCAL,
		    "NF calibrated [ext] [chain 2] is %d\n", nf);
		nfarray[5] = nf;
		/* fall thru... */
        case 0x3:
        case 0x5:
		nf = MS(OS_REG_READ(ah, AR_PHY_CH1_CCA), AR_PHY_CH1_MINCCA_PWR);
		if (nf & 0x100)
			nf = 0 - ((nf ^ 0x1ff) + 1);
		HALDEBUG(ah, HAL_DEBUG_NFCAL,
		    "NF calibrated [ctl] [chain 1] is %d\n", nf);
		nfarray[1] = nf;


		nf = MS(OS_REG_READ(ah, AR_PHY_CH1_EXT_CCA), AR_PHY_CH1_EXT_MINCCA_PWR);
		if (nf & 0x100)
			nf = 0 - ((nf ^ 0x1ff) + 1);
		HALDEBUG(ah, HAL_DEBUG_NFCAL,
		    "NF calibrated [ext] [chain 1] is %d\n", nf);
		nfarray[4] = nf;
		/* fall thru... */
        case 0x1:
		nf = MS(OS_REG_READ(ah, AR_PHY_CCA), AR_PHY_MINCCA_PWR);
		if (nf & 0x100)
			nf = 0 - ((nf ^ 0x1ff) + 1);
		HALDEBUG(ah, HAL_DEBUG_NFCAL,
		    "NF calibrated [ctl] [chain 0] is %d\n", nf);
		nfarray[0] = nf;

		nf = MS(OS_REG_READ(ah, AR_PHY_EXT_CCA), AR_PHY_EXT_MINCCA_PWR);
		if (nf & 0x100)
			nf = 0 - ((nf ^ 0x1ff) + 1);
		HALDEBUG(ah, HAL_DEBUG_NFCAL,
		    "NF calibrated [ext] [chain 0] is %d\n", nf);
		nfarray[3] = nf;

		break;
	}
}

/*
 * Adjust NF based on statistical values for 5GHz frequencies.
 * Stubbed:Not used by Fowl
 */
static int16_t
ar2133GetNfAdjust(struct ath_hal *ah, const HAL_CHANNEL_INTERNAL *c)
{
	return 0;
}

/*
 * Free memory for analog bank scratch buffers
 */
static void
ar2133RfDetach(struct ath_hal *ah)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

	HALASSERT(ahp->ah_rfHal != AH_NULL);
	ath_hal_free(ahp->ah_rfHal);
	ahp->ah_rfHal = AH_NULL;
}
	
/*
 * Allocate memory for analog bank scratch buffers
 * Scratch Buffer will be reinitialized every reset so no need to zero now
 */
HAL_BOOL
ar2133RfAttach(struct ath_hal *ah, HAL_STATUS *status)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	struct ar2133State *priv;
	uint32_t *bankData;

	HALDEBUG(ah, HAL_DEBUG_ATTACH, "%s: attach AR2133 radio\n", __func__);

	HALASSERT(ahp->ah_rfHal == AH_NULL);
	priv = ath_hal_malloc(sizeof(struct ar2133State)
	    + AH5416(ah)->ah_ini_bank0.rows * sizeof(uint32_t)
	    + AH5416(ah)->ah_ini_bank1.rows * sizeof(uint32_t)
	    + AH5416(ah)->ah_ini_bank2.rows * sizeof(uint32_t)
	    + AH5416(ah)->ah_ini_bank3.rows * sizeof(uint32_t)
	    + AH5416(ah)->ah_ini_bank6.rows * sizeof(uint32_t)
	    + AH5416(ah)->ah_ini_bank7.rows * sizeof(uint32_t)
	);
	if (priv == AH_NULL) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: cannot allocate private state\n", __func__);
		*status = HAL_ENOMEM;		/* XXX */
		return AH_FALSE;
	}
	priv->base.rfDetach		= ar2133RfDetach;
	priv->base.writeRegs		= ar2133WriteRegs;
	priv->base.getRfBank		= ar2133GetRfBank;
	priv->base.setChannel		= ar2133SetChannel;
	priv->base.setRfRegs		= ar2133SetRfRegs;
	priv->base.setPowerTable	= ar2133SetPowerTable;
	priv->base.getChannelMaxMinPower = ar2133GetChannelMaxMinPower;
	priv->base.getNfAdjust		= ar2133GetNfAdjust;

	bankData = (uint32_t *) &priv[1];
	priv->Bank0Data = bankData, bankData += AH5416(ah)->ah_ini_bank0.rows;
	priv->Bank1Data = bankData, bankData += AH5416(ah)->ah_ini_bank1.rows;
	priv->Bank2Data = bankData, bankData += AH5416(ah)->ah_ini_bank2.rows;
	priv->Bank3Data = bankData, bankData += AH5416(ah)->ah_ini_bank3.rows;
	priv->Bank6Data = bankData, bankData += AH5416(ah)->ah_ini_bank6.rows;
	priv->Bank7Data = bankData, bankData += AH5416(ah)->ah_ini_bank7.rows;

	ahp->ah_pcdacTable = priv->pcdacTable;
	ahp->ah_pcdacTableSize = sizeof(priv->pcdacTable);
	ahp->ah_rfHal = &priv->base;
	/*
	 * Set noise floor adjust method; we arrange a
	 * direct call instead of thunking.
	 */
	AH_PRIVATE(ah)->ah_getNfAdjust = priv->base.getNfAdjust;
	AH_PRIVATE(ah)->ah_getNoiseFloor = ar2133GetNoiseFloor;

	return AH_TRUE;
}

static HAL_BOOL
ar2133Probe(struct ath_hal *ah)
{
	return (AR_SREV_OWL(ah) || AR_SREV_HOWL(ah) || AR_SREV_SOWL(ah));
}

AH_RF(RF2133, ar2133Probe, ar2133RfAttach);

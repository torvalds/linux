/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2006 Atheros Communications, Inc.
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
#include "ah_devid.h"

#include "ar5211/ar5211.h"
#include "ar5211/ar5211reg.h"
#include "ar5211/ar5211phy.h"

#include "ah_eeprom_v3.h"

static HAL_BOOL ar5211GetChannelEdges(struct ath_hal *ah,
		uint16_t flags, uint16_t *low, uint16_t *high);
static HAL_BOOL ar5211GetChipPowerLimits(struct ath_hal *ah,
		struct ieee80211_channel *chan);

static void ar5211ConfigPCIE(struct ath_hal *ah, HAL_BOOL restore,
		HAL_BOOL power_off);
static void ar5211DisablePCIE(struct ath_hal *ah);

static const struct ath_hal_private ar5211hal = {{
	.ah_magic			= AR5211_MAGIC,

	.ah_getRateTable		= ar5211GetRateTable,
	.ah_detach			= ar5211Detach,

	/* Reset Functions */
	.ah_reset			= ar5211Reset,
	.ah_phyDisable			= ar5211PhyDisable,
	.ah_disable			= ar5211Disable,
	.ah_configPCIE			= ar5211ConfigPCIE,
	.ah_disablePCIE			= ar5211DisablePCIE,
	.ah_setPCUConfig		= ar5211SetPCUConfig,
	.ah_perCalibration		= ar5211PerCalibration,
	.ah_perCalibrationN		= ar5211PerCalibrationN,
	.ah_resetCalValid		= ar5211ResetCalValid,
	.ah_setTxPowerLimit		= ar5211SetTxPowerLimit,
	.ah_getChanNoise		= ath_hal_getChanNoise,

	/* Transmit functions */
	.ah_updateTxTrigLevel		= ar5211UpdateTxTrigLevel,
	.ah_setupTxQueue		= ar5211SetupTxQueue,
	.ah_setTxQueueProps             = ar5211SetTxQueueProps,
	.ah_getTxQueueProps             = ar5211GetTxQueueProps,
	.ah_releaseTxQueue		= ar5211ReleaseTxQueue,
	.ah_resetTxQueue		= ar5211ResetTxQueue,
	.ah_getTxDP			= ar5211GetTxDP,
	.ah_setTxDP			= ar5211SetTxDP,
	.ah_numTxPending		= ar5211NumTxPending,
	.ah_startTxDma			= ar5211StartTxDma,
	.ah_stopTxDma			= ar5211StopTxDma,
	.ah_setupTxDesc			= ar5211SetupTxDesc,
	.ah_setupXTxDesc		= ar5211SetupXTxDesc,
	.ah_fillTxDesc			= ar5211FillTxDesc,
	.ah_procTxDesc			= ar5211ProcTxDesc,
	.ah_getTxIntrQueue		= ar5211GetTxIntrQueue,
	.ah_reqTxIntrDesc 		= ar5211IntrReqTxDesc,
	.ah_getTxCompletionRates	= ar5211GetTxCompletionRates,
	.ah_setTxDescLink		= ar5211SetTxDescLink,
	.ah_getTxDescLink		= ar5211GetTxDescLink,
	.ah_getTxDescLinkPtr		= ar5211GetTxDescLinkPtr,

	/* RX Functions */
	.ah_getRxDP			= ar5211GetRxDP,
	.ah_setRxDP			= ar5211SetRxDP,
	.ah_enableReceive		= ar5211EnableReceive,
	.ah_stopDmaReceive		= ar5211StopDmaReceive,
	.ah_startPcuReceive		= ar5211StartPcuReceive,
	.ah_stopPcuReceive		= ar5211StopPcuReceive,
	.ah_setMulticastFilter		= ar5211SetMulticastFilter,
	.ah_setMulticastFilterIndex	= ar5211SetMulticastFilterIndex,
	.ah_clrMulticastFilterIndex	= ar5211ClrMulticastFilterIndex,
	.ah_getRxFilter			= ar5211GetRxFilter,
	.ah_setRxFilter			= ar5211SetRxFilter,
	.ah_setupRxDesc			= ar5211SetupRxDesc,
	.ah_procRxDesc			= ar5211ProcRxDesc,
	.ah_rxMonitor			= ar5211RxMonitor,
	.ah_aniPoll			= ar5211AniPoll,
	.ah_procMibEvent		= ar5211MibEvent,

	/* Misc Functions */
	.ah_getCapability		= ar5211GetCapability,
	.ah_setCapability		= ar5211SetCapability,
	.ah_getDiagState		= ar5211GetDiagState,
	.ah_getMacAddress		= ar5211GetMacAddress,
	.ah_setMacAddress		= ar5211SetMacAddress,
	.ah_getBssIdMask		= ar5211GetBssIdMask,
	.ah_setBssIdMask		= ar5211SetBssIdMask,
	.ah_setRegulatoryDomain		= ar5211SetRegulatoryDomain,
	.ah_setLedState			= ar5211SetLedState,
	.ah_writeAssocid		= ar5211WriteAssocid,
	.ah_gpioCfgInput		= ar5211GpioCfgInput,
	.ah_gpioCfgOutput		= ar5211GpioCfgOutput,
	.ah_gpioGet			= ar5211GpioGet,
	.ah_gpioSet			= ar5211GpioSet,
	.ah_gpioSetIntr			= ar5211GpioSetIntr,
	.ah_getTsf32			= ar5211GetTsf32,
	.ah_getTsf64			= ar5211GetTsf64,
	.ah_resetTsf			= ar5211ResetTsf,
	.ah_detectCardPresent		= ar5211DetectCardPresent,
	.ah_updateMibCounters		= ar5211UpdateMibCounters,
	.ah_getRfGain			= ar5211GetRfgain,
	.ah_getDefAntenna		= ar5211GetDefAntenna,
	.ah_setDefAntenna		= ar5211SetDefAntenna,
	.ah_getAntennaSwitch		= ar5211GetAntennaSwitch,
	.ah_setAntennaSwitch		= ar5211SetAntennaSwitch,
	.ah_setSifsTime			= ar5211SetSifsTime,
	.ah_getSifsTime			= ar5211GetSifsTime,
	.ah_setSlotTime			= ar5211SetSlotTime,
	.ah_getSlotTime			= ar5211GetSlotTime,
	.ah_setAckTimeout		= ar5211SetAckTimeout,
	.ah_getAckTimeout		= ar5211GetAckTimeout,
	.ah_setAckCTSRate		= ar5211SetAckCTSRate,
	.ah_getAckCTSRate		= ar5211GetAckCTSRate,
	.ah_setCTSTimeout		= ar5211SetCTSTimeout,
	.ah_getCTSTimeout		= ar5211GetCTSTimeout,
	.ah_setDecompMask		= ar5211SetDecompMask,
	.ah_setCoverageClass		= ar5211SetCoverageClass,
	.ah_setQuiet			= ar5211SetQuiet,
	.ah_get11nExtBusy		= ar5211Get11nExtBusy,
	.ah_getMibCycleCounts		= ar5211GetMibCycleCounts,
	.ah_setChainMasks		= ar5211SetChainMasks,
	.ah_enableDfs			= ar5211EnableDfs,
	.ah_getDfsThresh		= ar5211GetDfsThresh,
	/* XXX procRadarEvent */
	/* XXX isFastClockEnabled */

	/* Key Cache Functions */
	.ah_getKeyCacheSize		= ar5211GetKeyCacheSize,
	.ah_resetKeyCacheEntry		= ar5211ResetKeyCacheEntry,
	.ah_isKeyCacheEntryValid	= ar5211IsKeyCacheEntryValid,
	.ah_setKeyCacheEntry		= ar5211SetKeyCacheEntry,
	.ah_setKeyCacheEntryMac		= ar5211SetKeyCacheEntryMac,

	/* Power Management Functions */
	.ah_setPowerMode		= ar5211SetPowerMode,
	.ah_getPowerMode		= ar5211GetPowerMode,

	/* Beacon Functions */
	.ah_setBeaconTimers		= ar5211SetBeaconTimers,
	.ah_beaconInit			= ar5211BeaconInit,
	.ah_setStationBeaconTimers	= ar5211SetStaBeaconTimers,
	.ah_resetStationBeaconTimers	= ar5211ResetStaBeaconTimers,
	.ah_getNextTBTT			= ar5211GetNextTBTT,

	/* Interrupt Functions */
	.ah_isInterruptPending		= ar5211IsInterruptPending,
	.ah_getPendingInterrupts	= ar5211GetPendingInterrupts,
	.ah_getInterrupts		= ar5211GetInterrupts,
	.ah_setInterrupts		= ar5211SetInterrupts },

	.ah_getChannelEdges		= ar5211GetChannelEdges,
	.ah_getWirelessModes		= ar5211GetWirelessModes,
	.ah_eepromRead			= ar5211EepromRead,
#ifdef AH_SUPPORT_WRITE_EEPROM
	.ah_eepromWrite			= ar5211EepromWrite,
#endif
	.ah_getChipPowerLimits		= ar5211GetChipPowerLimits,
};

static HAL_BOOL ar5211ChipTest(struct ath_hal *);
static HAL_BOOL ar5211FillCapabilityInfo(struct ath_hal *ah);

/*
 * Return the revsion id for the radio chip.  This
 * fetched via the PHY.
 */
static uint32_t
ar5211GetRadioRev(struct ath_hal *ah)
{
	uint32_t val;
	int i;

	OS_REG_WRITE(ah, (AR_PHY_BASE + (0x34 << 2)), 0x00001c16);
	for (i = 0; i < 8; i++)
		OS_REG_WRITE(ah, (AR_PHY_BASE + (0x20 << 2)), 0x00010000);
	val = (OS_REG_READ(ah, AR_PHY_BASE + (256 << 2)) >> 24) & 0xff;
	val = ((val & 0xf0) >> 4) | ((val & 0x0f) << 4);
	return ath_hal_reverseBits(val, 8);
}

/*
 * Attach for an AR5211 part.
 */
static struct ath_hal *
ar5211Attach(uint16_t devid, HAL_SOFTC sc,
	HAL_BUS_TAG st, HAL_BUS_HANDLE sh, uint16_t *eepromdata,
	HAL_OPS_CONFIG *ah_config, HAL_STATUS *status)
{
#define	N(a)	(sizeof(a)/sizeof(a[0]))
	struct ath_hal_5211 *ahp;
	struct ath_hal *ah;
	uint32_t val;
	uint16_t eeval;
	HAL_STATUS ecode;

	HALDEBUG(AH_NULL, HAL_DEBUG_ATTACH, "%s: sc %p st %p sh %p\n",
	    __func__, sc, (void*) st, (void*) sh);

	/* NB: memory is returned zero'd */
	ahp = ath_hal_malloc(sizeof (struct ath_hal_5211));
	if (ahp == AH_NULL) {
		HALDEBUG(AH_NULL, HAL_DEBUG_ANY,
		    "%s: cannot allocate memory for state block\n", __func__);
		ecode = HAL_ENOMEM;
		goto bad;
	}
	ah = &ahp->ah_priv.h;
	/* set initial values */
	OS_MEMCPY(&ahp->ah_priv, &ar5211hal, sizeof(struct ath_hal_private));
	ah->ah_sc = sc;
	ah->ah_st = st;
	ah->ah_sh = sh;

	ah->ah_devid = devid;			/* NB: for AH_DEBUG_ALQ */
	AH_PRIVATE(ah)->ah_devid = devid;
	AH_PRIVATE(ah)->ah_subvendorid = 0;	/* XXX */

	AH_PRIVATE(ah)->ah_powerLimit = MAX_RATE_POWER;
	AH_PRIVATE(ah)->ah_tpScale = HAL_TP_SCALE_MAX;	/* no scaling */

	ahp->ah_diversityControl = HAL_ANT_VARIABLE;
	ahp->ah_staId1Defaults = 0;
	ahp->ah_rssiThr = INIT_RSSI_THR;
	ahp->ah_sifstime = (u_int) -1;
	ahp->ah_slottime = (u_int) -1;
	ahp->ah_acktimeout = (u_int) -1;
	ahp->ah_ctstimeout = (u_int) -1;

	if (!ar5211ChipReset(ah, AH_NULL)) {	/* reset chip */
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: chip reset failed\n", __func__);
		ecode = HAL_EIO;
		goto bad;
	}
	if (AH_PRIVATE(ah)->ah_devid == AR5211_FPGA11B) {
		/* set it back to OFDM mode to be able to read analog rev id */
		OS_REG_WRITE(ah, AR5211_PHY_MODE, AR5211_PHY_MODE_OFDM);
		OS_REG_WRITE(ah, AR_PHY_PLL_CTL, AR_PHY_PLL_CTL_44);
		OS_DELAY(1000);
	}

	/* Read Revisions from Chips */
	val = OS_REG_READ(ah, AR_SREV) & AR_SREV_ID_M;
	AH_PRIVATE(ah)->ah_macVersion = val >> AR_SREV_ID_S;
	AH_PRIVATE(ah)->ah_macRev = val & AR_SREV_REVISION_M;

	if (AH_PRIVATE(ah)->ah_macVersion < AR_SREV_VERSION_MAUI_2 ||
	    AH_PRIVATE(ah)->ah_macVersion > AR_SREV_VERSION_OAHU) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: Mac Chip Rev 0x%x is not supported by this driver\n",
		    __func__, AH_PRIVATE(ah)->ah_macVersion);
		ecode = HAL_ENOTSUPP;
		goto bad;
	}

	AH_PRIVATE(ah)->ah_phyRev = OS_REG_READ(ah, AR_PHY_CHIP_ID);

	if (!ar5211ChipTest(ah)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: hardware self-test failed\n",
		    __func__);
		ecode = HAL_ESELFTEST;
		goto bad;
	}

	/* Set correct Baseband to analog shift setting to access analog chips. */
	if (AH_PRIVATE(ah)->ah_macVersion >= AR_SREV_VERSION_OAHU) {
		OS_REG_WRITE(ah, AR_PHY_BASE, 0x00000007);
	} else {
		OS_REG_WRITE(ah, AR_PHY_BASE, 0x00000047);
	}
	OS_DELAY(2000);

	/* Read Radio Chip Rev Extract */
	AH_PRIVATE(ah)->ah_analog5GhzRev = ar5211GetRadioRev(ah);
	if ((AH_PRIVATE(ah)->ah_analog5GhzRev & 0xf0) != RAD5_SREV_MAJOR) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: 5G Radio Chip Rev 0x%02X is not supported by this "
		    "driver\n", __func__, AH_PRIVATE(ah)->ah_analog5GhzRev);
		ecode = HAL_ENOTSUPP;
		goto bad;
	}

	val = (OS_REG_READ(ah, AR_PCICFG) & AR_PCICFG_EEPROM_SIZE_M) >>
               AR_PCICFG_EEPROM_SIZE_S;
	if (val != AR_PCICFG_EEPROM_SIZE_16K) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: unsupported EEPROM size "
		    "%u (0x%x) found\n", __func__, val, val);
		ecode = HAL_EESIZE;
		goto bad;
	}
	ecode = ath_hal_legacyEepromAttach(ah);
	if (ecode != HAL_OK) {
		goto bad;
	}

        /* If Bmode and AR5211, verify 2.4 analog exists */
	if (AH_PRIVATE(ah)->ah_macVersion >= AR_SREV_VERSION_OAHU &&
	    ath_hal_eepromGetFlag(ah, AR_EEP_BMODE)) {
		/* Set correct Baseband to analog shift setting to access analog chips. */
		OS_REG_WRITE(ah, AR_PHY_BASE, 0x00004007);
		OS_DELAY(2000);
		AH_PRIVATE(ah)->ah_analog2GhzRev = ar5211GetRadioRev(ah);

		/* Set baseband for 5GHz chip */
		OS_REG_WRITE(ah, AR_PHY_BASE, 0x00000007);
		OS_DELAY(2000);
		if ((AH_PRIVATE(ah)->ah_analog2GhzRev & 0xF0) != RAD2_SREV_MAJOR) {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: 2G Radio Chip Rev 0x%x is not supported by "
			    "this driver\n", __func__,
			    AH_PRIVATE(ah)->ah_analog2GhzRev);
			ecode = HAL_ENOTSUPP;
			goto bad;
		}
	} else {
		ath_hal_eepromSet(ah, AR_EEP_BMODE, AH_FALSE);
        }

	ecode = ath_hal_eepromGet(ah, AR_EEP_REGDMN_0, &eeval);
	if (ecode != HAL_OK) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: cannot read regulatory domain from EEPROM\n",
		    __func__);
		goto bad;
        }
	AH_PRIVATE(ah)->ah_currentRD = eeval;
	AH_PRIVATE(ah)->ah_getNfAdjust = ar5211GetNfAdjust;

	/*
	 * Got everything we need now to setup the capabilities.
	 */
	(void) ar5211FillCapabilityInfo(ah);

	/* Initialize gain ladder thermal calibration structure */
	ar5211InitializeGainValues(ah);

	ecode = ath_hal_eepromGet(ah, AR_EEP_MACADDR, ahp->ah_macaddr);
	if (ecode != HAL_OK) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: error getting mac address from EEPROM\n", __func__);
		goto bad;
        }

	HALDEBUG(ah, HAL_DEBUG_ATTACH, "%s: return\n", __func__);

	return ah;
bad:
	if (ahp)
		ar5211Detach((struct ath_hal *) ahp);
	if (status)
		*status = ecode;
	return AH_NULL;
#undef N
}

void
ar5211Detach(struct ath_hal *ah)
{
	HALDEBUG(ah, HAL_DEBUG_ATTACH, "%s:\n", __func__);

	HALASSERT(ah != AH_NULL);
	HALASSERT(ah->ah_magic == AR5211_MAGIC);

	ath_hal_eepromDetach(ah);
	ath_hal_free(ah);
}

static HAL_BOOL
ar5211ChipTest(struct ath_hal *ah)
{
	uint32_t regAddr[2] = { AR_STA_ID0, AR_PHY_BASE+(8 << 2) };
	uint32_t regHold[2];
	uint32_t patternData[4] =
	    { 0x55555555, 0xaaaaaaaa, 0x66666666, 0x99999999 };
	int i, j;

	/* Test PHY & MAC registers */
	for (i = 0; i < 2; i++) {
		uint32_t addr = regAddr[i];
		uint32_t wrData, rdData;

		regHold[i] = OS_REG_READ(ah, addr);
		for (j = 0; j < 0x100; j++) {
			wrData = (j << 16) | j;
			OS_REG_WRITE(ah, addr, wrData);
			rdData = OS_REG_READ(ah, addr);
			if (rdData != wrData) {
				HALDEBUG(ah, HAL_DEBUG_ANY,
"%s: address test failed addr: 0x%08x - wr:0x%08x != rd:0x%08x\n",
				__func__, addr, wrData, rdData);
				return AH_FALSE;
			}
		}
		for (j = 0; j < 4; j++) {
			wrData = patternData[j];
			OS_REG_WRITE(ah, addr, wrData);
			rdData = OS_REG_READ(ah, addr);
			if (wrData != rdData) {
				HALDEBUG(ah, HAL_DEBUG_ANY,
"%s: address test failed addr: 0x%08x - wr:0x%08x != rd:0x%08x\n",
					__func__, addr, wrData, rdData);
				return AH_FALSE;
			}
		}
		OS_REG_WRITE(ah, regAddr[i], regHold[i]);
	}
	OS_DELAY(100);
	return AH_TRUE;
}

/*
 * Store the channel edges for the requested operational mode
 */
static HAL_BOOL
ar5211GetChannelEdges(struct ath_hal *ah,
	uint16_t flags, uint16_t *low, uint16_t *high)
{
	if (flags & IEEE80211_CHAN_5GHZ) {
		*low = 4920;
		*high = 6100;
		return AH_TRUE;
	}
	if (flags & IEEE80211_CHAN_2GHZ &&
	    ath_hal_eepromGetFlag(ah, AR_EEP_BMODE)) {
		*low = 2312;
		*high = 2732;
		return AH_TRUE;
	}
	return AH_FALSE;
}

static HAL_BOOL
ar5211GetChipPowerLimits(struct ath_hal *ah, struct ieee80211_channel *chan)
{
	/* XXX fill in, this is just a placeholder */
	HALDEBUG(ah, HAL_DEBUG_ATTACH,
	    "%s: no min/max power for %u/0x%x\n",
	    __func__, chan->ic_freq, chan->ic_flags);
	chan->ic_maxpower = MAX_RATE_POWER;
	chan->ic_minpower = 0;
	return AH_TRUE;
}

static void
ar5211ConfigPCIE(struct ath_hal *ah, HAL_BOOL restore, HAL_BOOL power_off)
{
}

static void
ar5211DisablePCIE(struct ath_hal *ah)
{
}

/*
 * Fill all software cached or static hardware state information.
 */
static HAL_BOOL
ar5211FillCapabilityInfo(struct ath_hal *ah)
{
	struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
	HAL_CAPABILITIES *pCap = &ahpriv->ah_caps;

	/* Construct wireless mode from EEPROM */
	pCap->halWirelessModes = 0;
	if (ath_hal_eepromGetFlag(ah, AR_EEP_AMODE)) {
		pCap->halWirelessModes |= HAL_MODE_11A;
		if (!ath_hal_eepromGetFlag(ah, AR_EEP_TURBO5DISABLE))
			pCap->halWirelessModes |= HAL_MODE_TURBO;
	}
	if (ath_hal_eepromGetFlag(ah, AR_EEP_BMODE))
		pCap->halWirelessModes |= HAL_MODE_11B;

	pCap->halLow2GhzChan = 2312;
	pCap->halHigh2GhzChan = 2732;
	pCap->halLow5GhzChan = 4920;
	pCap->halHigh5GhzChan = 6100;

	pCap->halChanSpreadSupport = AH_TRUE;
	pCap->halSleepAfterBeaconBroken = AH_TRUE;
	pCap->halPSPollBroken = AH_TRUE;
	pCap->halVEOLSupport = AH_TRUE;
	pCap->halNumMRRetries = 1;	/* No hardware MRR support */
	pCap->halNumTxMaps = 1;		/* Single TX ptr per descr */

	pCap->halTotalQueues = HAL_NUM_TX_QUEUES;
	pCap->halKeyCacheSize = 128;

	/* XXX not needed */
	pCap->halChanHalfRate = AH_FALSE;
	pCap->halChanQuarterRate = AH_FALSE;

	/*
	 * RSSI uses the combined field; some 11n NICs may use
	 * the control chain RSSI.
	 */
	pCap->halUseCombinedRadarRssi = AH_TRUE;

	if (ath_hal_eepromGetFlag(ah, AR_EEP_RFKILL) &&
	    ath_hal_eepromGet(ah, AR_EEP_RFSILENT, &ahpriv->ah_rfsilent) == HAL_OK) {
		/* NB: enabled by default */
		ahpriv->ah_rfkillEnabled = AH_TRUE;
		pCap->halRfSilentSupport = AH_TRUE;
	}

	pCap->halRxTstampPrecision = 13;
	pCap->halTxTstampPrecision = 16;
	pCap->halIntrMask = HAL_INT_COMMON
			| HAL_INT_RX
			| HAL_INT_TX
			| HAL_INT_FATAL
			| HAL_INT_BNR
			| HAL_INT_TIM
			;

	pCap->hal4kbSplitTransSupport = AH_TRUE;
	pCap->halHasRxSelfLinkedTail = AH_TRUE;

	/* XXX might be ok w/ some chip revs */
	ahpriv->ah_rxornIsFatal = AH_TRUE;
	return AH_TRUE;
}

static const char*
ar5211Probe(uint16_t vendorid, uint16_t devid)
{
	if (vendorid == ATHEROS_VENDOR_ID) {
		if (devid == AR5211_DEVID || devid == AR5311_DEVID ||
		    devid == AR5211_DEFAULT)
			return "Atheros 5211";
		if (devid == AR5211_FPGA11B)
			return "Atheros 5211 (FPGA)";
	}
	return AH_NULL;
}
AH_CHIP(AR5211, ar5211Probe, ar5211Attach);

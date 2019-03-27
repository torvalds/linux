/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2004 Atheros Communications, Inc.
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

#include "ar5210/ar5210.h"
#include "ar5210/ar5210reg.h"
#include "ar5210/ar5210phy.h"

#include "ah_eeprom_v1.h"

static	HAL_BOOL ar5210GetChannelEdges(struct ath_hal *,
		uint16_t flags, uint16_t *low, uint16_t *high);
static	HAL_BOOL ar5210GetChipPowerLimits(struct ath_hal *ah,
		struct ieee80211_channel *chan);

static void ar5210ConfigPCIE(struct ath_hal *ah, HAL_BOOL restore,
		HAL_BOOL power_on);
static void ar5210DisablePCIE(struct ath_hal *ah);

static const struct ath_hal_private ar5210hal = {{
	.ah_magic			= AR5210_MAGIC,

	.ah_getRateTable		= ar5210GetRateTable,
	.ah_detach			= ar5210Detach,

	/* Reset Functions */
	.ah_reset			= ar5210Reset,
	.ah_phyDisable			= ar5210PhyDisable,
	.ah_disable			= ar5210Disable,
	.ah_configPCIE			= ar5210ConfigPCIE,
	.ah_disablePCIE			= ar5210DisablePCIE,
	.ah_setPCUConfig		= ar5210SetPCUConfig,
	.ah_perCalibration		= ar5210PerCalibration,
	.ah_perCalibrationN		= ar5210PerCalibrationN,
	.ah_resetCalValid		= ar5210ResetCalValid,
	.ah_setTxPowerLimit		= ar5210SetTxPowerLimit,
	.ah_getChanNoise		= ath_hal_getChanNoise,

	/* Transmit functions */
	.ah_updateTxTrigLevel		= ar5210UpdateTxTrigLevel,
	.ah_setupTxQueue		= ar5210SetupTxQueue,
	.ah_setTxQueueProps             = ar5210SetTxQueueProps,
	.ah_getTxQueueProps             = ar5210GetTxQueueProps,
	.ah_releaseTxQueue		= ar5210ReleaseTxQueue,
	.ah_resetTxQueue		= ar5210ResetTxQueue,
	.ah_getTxDP			= ar5210GetTxDP,
	.ah_setTxDP			= ar5210SetTxDP,
	.ah_numTxPending		= ar5210NumTxPending,
	.ah_startTxDma			= ar5210StartTxDma,
	.ah_stopTxDma			= ar5210StopTxDma,
	.ah_setupTxDesc			= ar5210SetupTxDesc,
	.ah_setupXTxDesc		= ar5210SetupXTxDesc,
	.ah_fillTxDesc			= ar5210FillTxDesc,
	.ah_procTxDesc			= ar5210ProcTxDesc,
	.ah_getTxIntrQueue		= ar5210GetTxIntrQueue,
	.ah_reqTxIntrDesc 		= ar5210IntrReqTxDesc,
	.ah_getTxCompletionRates	= ar5210GetTxCompletionRates,
	.ah_setTxDescLink		= ar5210SetTxDescLink,
	.ah_getTxDescLink		= ar5210GetTxDescLink,
	.ah_getTxDescLinkPtr		= ar5210GetTxDescLinkPtr,

	/* RX Functions */
	.ah_getRxDP			= ar5210GetRxDP,
	.ah_setRxDP			= ar5210SetRxDP,
	.ah_enableReceive		= ar5210EnableReceive,
	.ah_stopDmaReceive		= ar5210StopDmaReceive,
	.ah_startPcuReceive		= ar5210StartPcuReceive,
	.ah_stopPcuReceive		= ar5210StopPcuReceive,
	.ah_setMulticastFilter		= ar5210SetMulticastFilter,
	.ah_setMulticastFilterIndex	= ar5210SetMulticastFilterIndex,
	.ah_clrMulticastFilterIndex	= ar5210ClrMulticastFilterIndex,
	.ah_getRxFilter			= ar5210GetRxFilter,
	.ah_setRxFilter			= ar5210SetRxFilter,
	.ah_setupRxDesc			= ar5210SetupRxDesc,
	.ah_procRxDesc			= ar5210ProcRxDesc,
	.ah_rxMonitor			= ar5210RxMonitor,
	.ah_aniPoll			= ar5210AniPoll,
	.ah_procMibEvent		= ar5210MibEvent,

	/* Misc Functions */
	.ah_getCapability		= ar5210GetCapability,
	.ah_setCapability		= ar5210SetCapability,
	.ah_getDiagState		= ar5210GetDiagState,
	.ah_getMacAddress		= ar5210GetMacAddress,
	.ah_setMacAddress		= ar5210SetMacAddress,
	.ah_getBssIdMask		= ar5210GetBssIdMask,
	.ah_setBssIdMask		= ar5210SetBssIdMask,
	.ah_setRegulatoryDomain		= ar5210SetRegulatoryDomain,
	.ah_setLedState			= ar5210SetLedState,
	.ah_writeAssocid		= ar5210WriteAssocid,
	.ah_gpioCfgInput		= ar5210GpioCfgInput,
	.ah_gpioCfgOutput		= ar5210GpioCfgOutput,
	.ah_gpioGet			= ar5210GpioGet,
	.ah_gpioSet			= ar5210GpioSet,
	.ah_gpioSetIntr			= ar5210Gpio0SetIntr,
	.ah_getTsf32			= ar5210GetTsf32,
	.ah_getTsf64			= ar5210GetTsf64,
	.ah_resetTsf			= ar5210ResetTsf,
	.ah_detectCardPresent		= ar5210DetectCardPresent,
	.ah_updateMibCounters		= ar5210UpdateMibCounters,
	.ah_getRfGain			= ar5210GetRfgain,
	.ah_getDefAntenna		= ar5210GetDefAntenna,
	.ah_setDefAntenna		= ar5210SetDefAntenna,
	.ah_getAntennaSwitch		= ar5210GetAntennaSwitch,
	.ah_setAntennaSwitch		= ar5210SetAntennaSwitch,
	.ah_setSifsTime			= ar5210SetSifsTime,
	.ah_getSifsTime			= ar5210GetSifsTime,
	.ah_setSlotTime			= ar5210SetSlotTime,
	.ah_getSlotTime			= ar5210GetSlotTime,
	.ah_setAckTimeout		= ar5210SetAckTimeout,
	.ah_getAckTimeout		= ar5210GetAckTimeout,
	.ah_setAckCTSRate		= ar5210SetAckCTSRate,
	.ah_getAckCTSRate		= ar5210GetAckCTSRate,
	.ah_setCTSTimeout		= ar5210SetCTSTimeout,
	.ah_getCTSTimeout		= ar5210GetCTSTimeout,
	.ah_setDecompMask		= ar5210SetDecompMask,
	.ah_setCoverageClass		= ar5210SetCoverageClass,
	.ah_setQuiet			= ar5210SetQuiet,
	.ah_get11nExtBusy		= ar5210Get11nExtBusy,
	.ah_getMibCycleCounts		= ar5210GetMibCycleCounts,
	.ah_setChainMasks		= ar5210SetChainMasks,
	.ah_enableDfs			= ar5210EnableDfs,
	.ah_getDfsThresh		= ar5210GetDfsThresh,
	/* XXX procRadarEvent */
	/* XXX isFastClockEnabled */

	/* Key Cache Functions */
	.ah_getKeyCacheSize		= ar5210GetKeyCacheSize,
	.ah_resetKeyCacheEntry		= ar5210ResetKeyCacheEntry,
	.ah_isKeyCacheEntryValid	= ar5210IsKeyCacheEntryValid,
	.ah_setKeyCacheEntry		= ar5210SetKeyCacheEntry,
	.ah_setKeyCacheEntryMac		= ar5210SetKeyCacheEntryMac,

	/* Power Management Functions */
	.ah_setPowerMode		= ar5210SetPowerMode,
	.ah_getPowerMode		= ar5210GetPowerMode,

	/* Beacon Functions */
	.ah_setBeaconTimers		= ar5210SetBeaconTimers,
	.ah_beaconInit			= ar5210BeaconInit,
	.ah_setStationBeaconTimers	= ar5210SetStaBeaconTimers,
	.ah_resetStationBeaconTimers	= ar5210ResetStaBeaconTimers,
	.ah_getNextTBTT			= ar5210GetNextTBTT,

	/* Interrupt Functions */
	.ah_isInterruptPending		= ar5210IsInterruptPending,
	.ah_getPendingInterrupts	= ar5210GetPendingInterrupts,
	.ah_getInterrupts		= ar5210GetInterrupts,
	.ah_setInterrupts		= ar5210SetInterrupts },

	.ah_getChannelEdges		= ar5210GetChannelEdges,
	.ah_getWirelessModes		= ar5210GetWirelessModes,
	.ah_eepromRead			= ar5210EepromRead,
#ifdef AH_SUPPORT_WRITE_EEPROM
	.ah_eepromWrite			= ar5210EepromWrite,
#endif
	.ah_getChipPowerLimits		= ar5210GetChipPowerLimits,
};

static HAL_BOOL ar5210FillCapabilityInfo(struct ath_hal *ah);

/*
 * Attach for an AR5210 part.
 */
static struct ath_hal *
ar5210Attach(uint16_t devid, HAL_SOFTC sc, HAL_BUS_TAG st, HAL_BUS_HANDLE sh,
	uint16_t *eepromdata, HAL_OPS_CONFIG *ah_config, HAL_STATUS *status)
{
#define	N(a)	(sizeof(a)/sizeof(a[0]))
	struct ath_hal_5210 *ahp;
	struct ath_hal *ah;
	uint32_t revid, pcicfg;
	uint16_t eeval;
	HAL_STATUS ecode;
	int i;

	HALDEBUG(AH_NULL, HAL_DEBUG_ATTACH,
	    "%s: devid 0x%x sc %p st %p sh %p\n", __func__, devid,
	    sc, (void*) st, (void*) sh);

	/* NB: memory is returned zero'd */
	ahp = ath_hal_malloc(sizeof (struct ath_hal_5210));
	if (ahp == AH_NULL) {
		HALDEBUG(AH_NULL, HAL_DEBUG_ANY,
		    "%s: no memory for state block\n", __func__);
		ecode = HAL_ENOMEM;
		goto bad;
	}
	ah = &ahp->ah_priv.h;
	/* set initial values */
	OS_MEMCPY(&ahp->ah_priv, &ar5210hal, sizeof(struct ath_hal_private));
	ah->ah_sc = sc;
	ah->ah_st = st;
	ah->ah_sh = sh;

	ah->ah_devid = devid;			/* NB: for AH_DEBUG_ALQ */
	AH_PRIVATE(ah)->ah_devid = devid;
	AH_PRIVATE(ah)->ah_subvendorid = 0;	/* XXX */

	AH_PRIVATE(ah)->ah_powerLimit = AR5210_MAX_RATE_POWER;
	AH_PRIVATE(ah)->ah_tpScale = HAL_TP_SCALE_MAX;	/* no scaling */

	ah->ah_powerMode = HAL_PM_UNDEFINED;
	ahp->ah_staId1Defaults = 0;
	ahp->ah_rssiThr = INIT_RSSI_THR;
	ahp->ah_sifstime = (u_int) -1;
	ahp->ah_slottime = (u_int) -1;
	ahp->ah_acktimeout = (u_int) -1;
	ahp->ah_ctstimeout = (u_int) -1;

	if (!ar5210ChipReset(ah, AH_NULL)) {	/* reset chip */
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: chip reset failed\n",
		    __func__);
		ecode = HAL_EIO;
		goto bad;
	}

	/* Read Revisions from Chips */
	AH_PRIVATE(ah)->ah_macVersion = 1;
	AH_PRIVATE(ah)->ah_macRev = OS_REG_READ(ah, AR_SREV) & 0xff;
	AH_PRIVATE(ah)->ah_phyRev = OS_REG_READ(ah, AR_PHY_CHIPID);
	AH_PRIVATE(ah)->ah_analog2GhzRev = 0;

	/* Read Radio Chip Rev Extract */
	OS_REG_WRITE(ah, (AR_PHY_BASE + (0x34 << 2)), 0x00001c16);
	for (i = 0; i < 4; i++)
		OS_REG_WRITE(ah, (AR_PHY_BASE + (0x20 << 2)), 0x00010000);
	revid = (OS_REG_READ(ah, AR_PHY_BASE + (256 << 2)) >> 28) & 0xf;

	/* Chip labelling is 1 greater than revision register for AR5110 */
	AH_PRIVATE(ah)->ah_analog5GhzRev = ath_hal_reverseBits(revid, 4) + 1;

	/*
	 * Read all the settings from the EEPROM and stash
	 * ones we'll use later.
	 */
	pcicfg = OS_REG_READ(ah, AR_PCICFG);
	OS_REG_WRITE(ah, AR_PCICFG, pcicfg | AR_PCICFG_EEPROMSEL);
	ecode = ath_hal_v1EepromAttach(ah);
	if (ecode != HAL_OK) {
		goto eebad;
	}
	ecode = ath_hal_eepromGet(ah, AR_EEP_REGDMN_0, &eeval);
	if (ecode != HAL_OK) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: cannot read regulatory domain from EEPROM\n",
		    __func__);
		goto eebad;
        }
	AH_PRIVATE(ah)->ah_currentRD = eeval;
	ecode = ath_hal_eepromGet(ah, AR_EEP_MACADDR, ahp->ah_macaddr);
	if (ecode != HAL_OK) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: error getting mac address from EEPROM\n", __func__);
		goto eebad;
        }
	OS_REG_WRITE(ah, AR_PCICFG, pcicfg);	/* disable EEPROM access */

	AH_PRIVATE(ah)->ah_getNfAdjust = ar5210GetNfAdjust;

	/*
	 * Got everything we need now to setup the capabilities.
	 */
	(void) ar5210FillCapabilityInfo(ah);

	HALDEBUG(ah, HAL_DEBUG_ATTACH, "%s: return\n", __func__);

	return ah;
eebad:
	OS_REG_WRITE(ah, AR_PCICFG, pcicfg);	/* disable EEPROM access */
bad:
	if (ahp)
		ath_hal_free(ahp);
	if (status)
		*status = ecode;
	return AH_NULL;
#undef N
}

void
ar5210Detach(struct ath_hal *ah)
{
	HALDEBUG(ah, HAL_DEBUG_ATTACH, "%s:\n", __func__);

	HALASSERT(ah != AH_NULL);
	HALASSERT(ah->ah_magic == AR5210_MAGIC);

	ath_hal_eepromDetach(ah);
	ath_hal_free(ah);
}

/*
 * Store the channel edges for the requested operational mode
 */
static HAL_BOOL
ar5210GetChannelEdges(struct ath_hal *ah,
	uint16_t flags, uint16_t *low, uint16_t *high)
{
	if (flags & IEEE80211_CHAN_5GHZ) {
		*low = 5120;
		*high = 5430;
		return AH_TRUE;
	} else {
		return AH_FALSE;
	}
}

static HAL_BOOL
ar5210GetChipPowerLimits(struct ath_hal *ah, struct ieee80211_channel *chan)
{
	/* XXX fill in, this is just a placeholder */
	HALDEBUG(ah, HAL_DEBUG_ATTACH,
	    "%s: no min/max power for %u/0x%x\n",
	    __func__, chan->ic_freq, chan->ic_flags);
	chan->ic_maxpower = AR5210_MAX_RATE_POWER;
	chan->ic_minpower = 0;
	return AH_TRUE;
}

static void
ar5210ConfigPCIE(struct ath_hal *ah, HAL_BOOL restore, HAL_BOOL power_off)
{
}

static void
ar5210DisablePCIE(struct ath_hal *ah)
{
}

/*
 * Fill all software cached or static hardware state information.
 */
static HAL_BOOL
ar5210FillCapabilityInfo(struct ath_hal *ah)
{
	struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
	HAL_CAPABILITIES *pCap = &ahpriv->ah_caps;

	pCap->halWirelessModes |= HAL_MODE_11A;

	pCap->halLow5GhzChan = 5120;
	pCap->halHigh5GhzChan = 5430;

	pCap->halSleepAfterBeaconBroken = AH_TRUE;
	pCap->halPSPollBroken = AH_FALSE;
	pCap->halNumMRRetries = 1;		/* No hardware MRR support */
	pCap->halNumTxMaps = 1;			/* Single TX ptr per descr */

	pCap->halTotalQueues = HAL_NUM_TX_QUEUES;
	pCap->halKeyCacheSize = 64;

	/* XXX not needed */
	pCap->halChanHalfRate = AH_FALSE;
	pCap->halChanQuarterRate = AH_FALSE;

	/*
	 * RSSI uses the combined field; some 11n NICs may use
	 * the control chain RSSI.
	 */
	pCap->halUseCombinedRadarRssi = AH_TRUE;

	if (ath_hal_eepromGetFlag(ah, AR_EEP_RFKILL)) {
		/*
		 * Setup initial rfsilent settings based on the EEPROM
		 * contents.  Pin 0, polarity 0 is fixed; record this
		 * using the EEPROM format found in later parts.
		 */
		ahpriv->ah_rfsilent = SM(0, AR_EEPROM_RFSILENT_GPIO_SEL)
				    | SM(0, AR_EEPROM_RFSILENT_POLARITY);
		ahpriv->ah_rfkillEnabled = AH_TRUE;
		pCap->halRfSilentSupport = AH_TRUE;
	}

	pCap->halTxTstampPrecision = 16;
	pCap->halRxTstampPrecision = 15;	/* NB: s/w extended from 13 */
	pCap->halIntrMask = (HAL_INT_COMMON - HAL_INT_BNR)
			| HAL_INT_RX
			| HAL_INT_TX
			| HAL_INT_FATAL
			;

	pCap->hal4kbSplitTransSupport = AH_TRUE;
	pCap->halHasRxSelfLinkedTail = AH_TRUE;

	ahpriv->ah_rxornIsFatal = AH_TRUE;
	return AH_TRUE;
}

static const char*
ar5210Probe(uint16_t vendorid, uint16_t devid)
{
	if (vendorid == ATHEROS_VENDOR_ID &&
	    (devid == AR5210_PROD || devid == AR5210_DEFAULT))
		return "Atheros 5210";
	return AH_NULL;
}
AH_CHIP(AR5210, ar5210Probe, ar5210Attach);

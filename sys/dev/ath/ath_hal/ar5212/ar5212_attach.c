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
#include "ah_devid.h"

#include "ar5212/ar5212.h"
#include "ar5212/ar5212reg.h"
#include "ar5212/ar5212phy.h"

#define AH_5212_COMMON
#include "ar5212/ar5212.ini"

static void ar5212ConfigPCIE(struct ath_hal *ah, HAL_BOOL restore,
		HAL_BOOL power_off);
static void ar5212DisablePCIE(struct ath_hal *ah);

static const struct ath_hal_private ar5212hal = {{
	.ah_magic			= AR5212_MAGIC,

	.ah_getRateTable		= ar5212GetRateTable,
	.ah_detach			= ar5212Detach,

	/* Reset Functions */
	.ah_reset			= ar5212Reset,
	.ah_phyDisable			= ar5212PhyDisable,
	.ah_disable			= ar5212Disable,
	.ah_configPCIE			= ar5212ConfigPCIE,
	.ah_disablePCIE			= ar5212DisablePCIE,
	.ah_setPCUConfig		= ar5212SetPCUConfig,
	.ah_perCalibration		= ar5212PerCalibration,
	.ah_perCalibrationN		= ar5212PerCalibrationN,
	.ah_resetCalValid		= ar5212ResetCalValid,
	.ah_setTxPowerLimit		= ar5212SetTxPowerLimit,
	.ah_getChanNoise		= ath_hal_getChanNoise,

	/* Transmit functions */
	.ah_updateTxTrigLevel		= ar5212UpdateTxTrigLevel,
	.ah_setupTxQueue		= ar5212SetupTxQueue,
	.ah_setTxQueueProps             = ar5212SetTxQueueProps,
	.ah_getTxQueueProps             = ar5212GetTxQueueProps,
	.ah_releaseTxQueue		= ar5212ReleaseTxQueue,
	.ah_resetTxQueue		= ar5212ResetTxQueue,
	.ah_getTxDP			= ar5212GetTxDP,
	.ah_setTxDP			= ar5212SetTxDP,
	.ah_numTxPending		= ar5212NumTxPending,
	.ah_startTxDma			= ar5212StartTxDma,
	.ah_stopTxDma			= ar5212StopTxDma,
	.ah_setupTxDesc			= ar5212SetupTxDesc,
	.ah_setupXTxDesc		= ar5212SetupXTxDesc,
	.ah_fillTxDesc			= ar5212FillTxDesc,
	.ah_procTxDesc			= ar5212ProcTxDesc,
	.ah_getTxIntrQueue		= ar5212GetTxIntrQueue,
	.ah_reqTxIntrDesc 		= ar5212IntrReqTxDesc,
	.ah_getTxCompletionRates	= ar5212GetTxCompletionRates,
	.ah_setTxDescLink		= ar5212SetTxDescLink,
	.ah_getTxDescLink		= ar5212GetTxDescLink,
	.ah_getTxDescLinkPtr		= ar5212GetTxDescLinkPtr,

	/* RX Functions */
	.ah_getRxDP			= ar5212GetRxDP,
	.ah_setRxDP			= ar5212SetRxDP,
	.ah_enableReceive		= ar5212EnableReceive,
	.ah_stopDmaReceive		= ar5212StopDmaReceive,
	.ah_startPcuReceive		= ar5212StartPcuReceive,
	.ah_stopPcuReceive		= ar5212StopPcuReceive,
	.ah_setMulticastFilter		= ar5212SetMulticastFilter,
	.ah_setMulticastFilterIndex	= ar5212SetMulticastFilterIndex,
	.ah_clrMulticastFilterIndex	= ar5212ClrMulticastFilterIndex,
	.ah_getRxFilter			= ar5212GetRxFilter,
	.ah_setRxFilter			= ar5212SetRxFilter,
	.ah_setupRxDesc			= ar5212SetupRxDesc,
	.ah_procRxDesc			= ar5212ProcRxDesc,
	.ah_rxMonitor			= ar5212RxMonitor,
	.ah_aniPoll			= ar5212AniPoll,
	.ah_procMibEvent		= ar5212ProcessMibIntr,

	/* Misc Functions */
	.ah_getCapability		= ar5212GetCapability,
	.ah_setCapability		= ar5212SetCapability,
	.ah_getDiagState		= ar5212GetDiagState,
	.ah_getMacAddress		= ar5212GetMacAddress,
	.ah_setMacAddress		= ar5212SetMacAddress,
	.ah_getBssIdMask		= ar5212GetBssIdMask,
	.ah_setBssIdMask		= ar5212SetBssIdMask,
	.ah_setRegulatoryDomain		= ar5212SetRegulatoryDomain,
	.ah_setLedState			= ar5212SetLedState,
	.ah_writeAssocid		= ar5212WriteAssocid,
	.ah_gpioCfgInput		= ar5212GpioCfgInput,
	.ah_gpioCfgOutput		= ar5212GpioCfgOutput,
	.ah_gpioGet			= ar5212GpioGet,
	.ah_gpioSet			= ar5212GpioSet,
	.ah_gpioSetIntr			= ar5212GpioSetIntr,
	.ah_getTsf32			= ar5212GetTsf32,
	.ah_getTsf64			= ar5212GetTsf64,
	.ah_setTsf64			= ar5212SetTsf64,
	.ah_resetTsf			= ar5212ResetTsf,
	.ah_detectCardPresent		= ar5212DetectCardPresent,
	.ah_updateMibCounters		= ar5212UpdateMibCounters,
	.ah_getRfGain			= ar5212GetRfgain,
	.ah_getDefAntenna		= ar5212GetDefAntenna,
	.ah_setDefAntenna		= ar5212SetDefAntenna,
	.ah_getAntennaSwitch		= ar5212GetAntennaSwitch,
	.ah_setAntennaSwitch		= ar5212SetAntennaSwitch,
	.ah_setSifsTime			= ar5212SetSifsTime,
	.ah_getSifsTime			= ar5212GetSifsTime,
	.ah_setSlotTime			= ar5212SetSlotTime,
	.ah_getSlotTime			= ar5212GetSlotTime,
	.ah_setAckTimeout		= ar5212SetAckTimeout,
	.ah_getAckTimeout		= ar5212GetAckTimeout,
	.ah_setAckCTSRate		= ar5212SetAckCTSRate,
	.ah_getAckCTSRate		= ar5212GetAckCTSRate,
	.ah_setCTSTimeout		= ar5212SetCTSTimeout,
	.ah_getCTSTimeout		= ar5212GetCTSTimeout,
	.ah_setDecompMask		= ar5212SetDecompMask,
	.ah_setCoverageClass		= ar5212SetCoverageClass,
	.ah_setQuiet			= ar5212SetQuiet,
	.ah_getMibCycleCounts		= ar5212GetMibCycleCounts,
	.ah_setChainMasks		= ar5212SetChainMasks,

	/* DFS Functions */
	.ah_enableDfs			= ar5212EnableDfs,
	.ah_getDfsThresh		= ar5212GetDfsThresh,
	.ah_getDfsDefaultThresh		= ar5212GetDfsDefaultThresh,
	.ah_procRadarEvent		= ar5212ProcessRadarEvent,
	.ah_isFastClockEnabled		= ar5212IsFastClockEnabled,
	.ah_get11nExtBusy		= ar5212Get11nExtBusy,

	/* Key Cache Functions */
	.ah_getKeyCacheSize		= ar5212GetKeyCacheSize,
	.ah_resetKeyCacheEntry		= ar5212ResetKeyCacheEntry,
	.ah_isKeyCacheEntryValid	= ar5212IsKeyCacheEntryValid,
	.ah_setKeyCacheEntry		= ar5212SetKeyCacheEntry,
	.ah_setKeyCacheEntryMac		= ar5212SetKeyCacheEntryMac,

	/* Power Management Functions */
	.ah_setPowerMode		= ar5212SetPowerMode,
	.ah_getPowerMode		= ar5212GetPowerMode,

	/* Beacon Functions */
	.ah_setBeaconTimers		= ar5212SetBeaconTimers,
	.ah_beaconInit			= ar5212BeaconInit,
	.ah_setStationBeaconTimers	= ar5212SetStaBeaconTimers,
	.ah_resetStationBeaconTimers	= ar5212ResetStaBeaconTimers,
	.ah_getNextTBTT			= ar5212GetNextTBTT,

	/* Interrupt Functions */
	.ah_isInterruptPending		= ar5212IsInterruptPending,
	.ah_getPendingInterrupts	= ar5212GetPendingInterrupts,
	.ah_getInterrupts		= ar5212GetInterrupts,
	.ah_setInterrupts		= ar5212SetInterrupts },

	.ah_getChannelEdges		= ar5212GetChannelEdges,
	.ah_getWirelessModes		= ar5212GetWirelessModes,
	.ah_eepromRead			= ar5212EepromRead,
#ifdef AH_SUPPORT_WRITE_EEPROM
	.ah_eepromWrite			= ar5212EepromWrite,
#endif
	.ah_getChipPowerLimits		= ar5212GetChipPowerLimits,
};

uint32_t
ar5212GetRadioRev(struct ath_hal *ah)
{
	uint32_t val;
	int i;

	/* Read Radio Chip Rev Extract */
	OS_REG_WRITE(ah, AR_PHY(0x34), 0x00001c16);
	for (i = 0; i < 8; i++)
		OS_REG_WRITE(ah, AR_PHY(0x20), 0x00010000);
	val = (OS_REG_READ(ah, AR_PHY(256)) >> 24) & 0xff;
	val = ((val & 0xf0) >> 4) | ((val & 0x0f) << 4);
	return ath_hal_reverseBits(val, 8);
}

static void
ar5212AniSetup(struct ath_hal *ah)
{
	static const struct ar5212AniParams aniparams = {
		.maxNoiseImmunityLevel	= 4,	/* levels 0..4 */
		.totalSizeDesired	= { -55, -55, -55, -55, -62 },
		.coarseHigh		= { -14, -14, -14, -14, -12 },
		.coarseLow		= { -64, -64, -64, -64, -70 },
		.firpwr			= { -78, -78, -78, -78, -80 },
		.maxSpurImmunityLevel	= 2,	/* NB: depends on chip rev */
		.cycPwrThr1		= { 2, 4, 6, 8, 10, 12, 14, 16 },
		.maxFirstepLevel	= 2,	/* levels 0..2 */
		.firstep		= { 0, 4, 8 },
		.ofdmTrigHigh		= 500,
		.ofdmTrigLow		= 200,
		.cckTrigHigh		= 200,
		.cckTrigLow		= 100,
		.rssiThrHigh		= 40,
		.rssiThrLow		= 7,
		.period			= 100,
	};
	if (AH_PRIVATE(ah)->ah_macVersion < AR_SREV_VERSION_GRIFFIN) {
		struct ar5212AniParams tmp;
		OS_MEMCPY(&tmp, &aniparams, sizeof(struct ar5212AniParams));
		tmp.maxSpurImmunityLevel = 7;	/* Venice and earlier */
		ar5212AniAttach(ah, &tmp, &tmp, AH_TRUE);
	} else
		ar5212AniAttach(ah, &aniparams, &aniparams, AH_TRUE);

	/* Set overridable ANI methods */
	AH5212(ah)->ah_aniControl = ar5212AniControl;
}

/*
 * Attach for an AR5212 part.
 */
void
ar5212InitState(struct ath_hal_5212 *ahp, uint16_t devid, HAL_SOFTC sc,
	HAL_BUS_TAG st, HAL_BUS_HANDLE sh, HAL_STATUS *status)
{
#define	N(a)	(sizeof(a)/sizeof(a[0]))
	static const uint8_t defbssidmask[IEEE80211_ADDR_LEN] =
		{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	struct ath_hal *ah;

	ah = &ahp->ah_priv.h;
	/* set initial values */
	OS_MEMCPY(&ahp->ah_priv, &ar5212hal, sizeof(struct ath_hal_private));
	ah->ah_sc = sc;
	ah->ah_st = st;
	ah->ah_sh = sh;

	ah->ah_devid = devid;			/* NB: for alq */
	AH_PRIVATE(ah)->ah_devid = devid;
	AH_PRIVATE(ah)->ah_subvendorid = 0;	/* XXX */

	AH_PRIVATE(ah)->ah_powerLimit = MAX_RATE_POWER;
	AH_PRIVATE(ah)->ah_tpScale = HAL_TP_SCALE_MAX;	/* no scaling */

	ahp->ah_antControl = HAL_ANT_VARIABLE;
	ahp->ah_diversity = AH_TRUE;
	ahp->ah_bIQCalibration = AH_FALSE;
	/*
	 * Enable MIC handling.
	 */
	ahp->ah_staId1Defaults = AR_STA_ID1_CRPT_MIC_ENABLE;
	ahp->ah_rssiThr = INIT_RSSI_THR;
	ahp->ah_tpcEnabled = AH_FALSE;		/* disabled by default */
	ahp->ah_phyPowerOn = AH_FALSE;
	ahp->ah_macTPC = SM(MAX_RATE_POWER, AR_TPC_ACK)
		       | SM(MAX_RATE_POWER, AR_TPC_CTS)
		       | SM(MAX_RATE_POWER, AR_TPC_CHIRP);
	ahp->ah_beaconInterval = 100;		/* XXX [20..1000] */
	ahp->ah_enable32kHzClock = DONT_USE_32KHZ;/* XXX */
	ahp->ah_slottime = (u_int) -1;
	ahp->ah_acktimeout = (u_int) -1;
	ahp->ah_ctstimeout = (u_int) -1;
	ahp->ah_sifstime = (u_int) -1;
	ahp->ah_txTrigLev = INIT_TX_FIFO_THRESHOLD;
	ahp->ah_maxTxTrigLev = MAX_TX_FIFO_THRESHOLD;

	OS_MEMCPY(&ahp->ah_bssidmask, defbssidmask, IEEE80211_ADDR_LEN);
#undef N
}

/*
 * Validate MAC version and revision. 
 */
static HAL_BOOL
ar5212IsMacSupported(uint8_t macVersion, uint8_t macRev)
{
#define	N(a)	(sizeof(a)/sizeof(a[0]))
	static const struct {
		uint8_t	version;
		uint8_t	revMin, revMax;
	} macs[] = {
	    { AR_SREV_VERSION_VENICE,
	      AR_SREV_D2PLUS,		AR_SREV_REVISION_MAX },
	    { AR_SREV_VERSION_GRIFFIN,
	      AR_SREV_D2PLUS,		AR_SREV_REVISION_MAX },
	    { AR_SREV_5413,
	      AR_SREV_REVISION_MIN,	AR_SREV_REVISION_MAX },
	    { AR_SREV_5424,
	      AR_SREV_REVISION_MIN,	AR_SREV_REVISION_MAX },
	    { AR_SREV_2425,
	      AR_SREV_REVISION_MIN,	AR_SREV_REVISION_MAX },
	    { AR_SREV_2417,
	      AR_SREV_REVISION_MIN,	AR_SREV_REVISION_MAX },
	};
	int i;

	for (i = 0; i < N(macs); i++)
		if (macs[i].version == macVersion &&
		    macs[i].revMin <= macRev && macRev <= macs[i].revMax)
			return AH_TRUE;
	return AH_FALSE;
#undef N
}
       
/*
 * Attach for an AR5212 part.
 */
static struct ath_hal *
ar5212Attach(uint16_t devid, HAL_SOFTC sc,
	HAL_BUS_TAG st, HAL_BUS_HANDLE sh, uint16_t *eepromdata,
	HAL_OPS_CONFIG *ah_config, HAL_STATUS *status)
{
#define	AH_EEPROM_PROTECT(ah) \
	(AH_PRIVATE(ah)->ah_ispcie)? AR_EEPROM_PROTECT_PCIE : AR_EEPROM_PROTECT)
	struct ath_hal_5212 *ahp;
	struct ath_hal *ah;
	struct ath_hal_rf *rf;
	uint32_t val;
	uint16_t eeval;
	HAL_STATUS ecode;

	HALDEBUG(AH_NULL, HAL_DEBUG_ATTACH, "%s: sc %p st %p sh %p\n",
	    __func__, sc, (void*) st, (void*) sh);

	/* NB: memory is returned zero'd */
	ahp = ath_hal_malloc(sizeof (struct ath_hal_5212));
	if (ahp == AH_NULL) {
		HALDEBUG(AH_NULL, HAL_DEBUG_ANY,
		    "%s: cannot allocate memory for state block\n", __func__);
		*status = HAL_ENOMEM;
		return AH_NULL;
	}
	ar5212InitState(ahp, devid, sc, st, sh, status);
	ah = &ahp->ah_priv.h;

	if (!ar5212SetPowerMode(ah, HAL_PM_AWAKE, AH_TRUE)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: couldn't wakeup chip\n",
		    __func__);
		ecode = HAL_EIO;
		goto bad;
	}
	/* Read Revisions from Chips before taking out of reset */
	val = OS_REG_READ(ah, AR_SREV) & AR_SREV_ID;
	AH_PRIVATE(ah)->ah_macVersion = val >> AR_SREV_ID_S;
	AH_PRIVATE(ah)->ah_macRev = val & AR_SREV_REVISION;
	AH_PRIVATE(ah)->ah_ispcie = IS_5424(ah) || IS_2425(ah);

	if (!ar5212IsMacSupported(AH_PRIVATE(ah)->ah_macVersion, AH_PRIVATE(ah)->ah_macRev)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: Mac Chip Rev 0x%02x.%x not supported\n" ,
		    __func__, AH_PRIVATE(ah)->ah_macVersion,
		    AH_PRIVATE(ah)->ah_macRev);
		ecode = HAL_ENOTSUPP;
		goto bad;
	}

	/* setup common ini data; rf backends handle remainder */
	HAL_INI_INIT(&ahp->ah_ini_modes, ar5212Modes, 6);
	HAL_INI_INIT(&ahp->ah_ini_common, ar5212Common, 2);

	if (!ar5212ChipReset(ah, AH_NULL)) {	/* reset chip */
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: chip reset failed\n", __func__);
		ecode = HAL_EIO;
		goto bad;
	}

	AH_PRIVATE(ah)->ah_phyRev = OS_REG_READ(ah, AR_PHY_CHIP_ID);

	if (AH_PRIVATE(ah)->ah_ispcie) {
		/* XXX: build flag to disable this? */
		ath_hal_configPCIE(ah, AH_FALSE, AH_FALSE);
	}

	if (!ar5212ChipTest(ah)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: hardware self-test failed\n",
		    __func__);
		ecode = HAL_ESELFTEST;
		goto bad;
	}

	/* Enable PCI core retry fix in software for Hainan and up */
	if (AH_PRIVATE(ah)->ah_macVersion >= AR_SREV_VERSION_VENICE)
		OS_REG_SET_BIT(ah, AR_PCICFG, AR_PCICFG_RETRYFIXEN);

	/*
	 * Set correct Baseband to analog shift
	 * setting to access analog chips.
	 */
	OS_REG_WRITE(ah, AR_PHY(0), 0x00000007);

	/* Read Radio Chip Rev Extract */
	AH_PRIVATE(ah)->ah_analog5GhzRev = ar5212GetRadioRev(ah);

	rf = ath_hal_rfprobe(ah, &ecode);
	if (rf == AH_NULL)
		goto bad;

	/* NB: silently accept anything in release code per Atheros */
	switch (AH_PRIVATE(ah)->ah_analog5GhzRev & AR_RADIO_SREV_MAJOR) {
	case AR_RAD5111_SREV_MAJOR:
	case AR_RAD5112_SREV_MAJOR:
	case AR_RAD2112_SREV_MAJOR:
	case AR_RAD2111_SREV_MAJOR:
	case AR_RAD2413_SREV_MAJOR:
	case AR_RAD5413_SREV_MAJOR:
	case AR_RAD5424_SREV_MAJOR:
		break;
	default:
		if (AH_PRIVATE(ah)->ah_analog5GhzRev == 0) {
			/*
			 * When RF_Silent is used, the
			 * analog chip is reset.  So when the system boots
			 * up with the radio switch off we cannot determine
			 * the RF chip rev.  To workaround this check the
			 * mac+phy revs and if Hainan, set the radio rev
			 * to Derby.
			 */
			if (AH_PRIVATE(ah)->ah_macVersion == AR_SREV_VERSION_VENICE &&
			    AH_PRIVATE(ah)->ah_macRev == AR_SREV_HAINAN &&
			    AH_PRIVATE(ah)->ah_phyRev == AR_PHYREV_HAINAN) {
				AH_PRIVATE(ah)->ah_analog5GhzRev = AR_ANALOG5REV_HAINAN;
				break;
			}
			if (IS_2413(ah)) {		/* Griffin */
				AH_PRIVATE(ah)->ah_analog5GhzRev =
				    AR_RAD2413_SREV_MAJOR | 0x1;
				break;
			}
			if (IS_5413(ah)) {		/* Eagle */	
				AH_PRIVATE(ah)->ah_analog5GhzRev =
				    AR_RAD5413_SREV_MAJOR | 0x2;
				break;
			}
			if (IS_2425(ah) || IS_2417(ah)) {/* Swan or Nala */	
				AH_PRIVATE(ah)->ah_analog5GhzRev =
				    AR_RAD5424_SREV_MAJOR | 0x2;
				break;
			}
		}
#ifdef AH_DEBUG
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: 5G Radio Chip Rev 0x%02X is not supported by "
		    "this driver\n",
		    __func__, AH_PRIVATE(ah)->ah_analog5GhzRev);
		ecode = HAL_ENOTSUPP;
		goto bad;
#endif
	}
	if (IS_RAD5112_REV1(ah)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: 5112 Rev 1 is not supported by this "
		    "driver (analog5GhzRev 0x%x)\n", __func__,
		    AH_PRIVATE(ah)->ah_analog5GhzRev);
		ecode = HAL_ENOTSUPP;
		goto bad;
	}

	val = OS_REG_READ(ah, AR_PCICFG);
	val = MS(val, AR_PCICFG_EEPROM_SIZE);
	if (val == 0) {
		if (!AH_PRIVATE(ah)->ah_ispcie) {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: unsupported EEPROM size %u (0x%x) found\n",
			    __func__, val, val);
			ecode = HAL_EESIZE;
			goto bad;
		}
		/* XXX AH_PRIVATE(ah)->ah_isPciExpress = AH_TRUE; */
	} else if (val != AR_PCICFG_EEPROM_SIZE_16K) {
		if (AR_PCICFG_EEPROM_SIZE_FAILED == val) {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: unsupported EEPROM size %u (0x%x) found\n",
			    __func__, val, val);
			ecode = HAL_EESIZE;
			goto bad;
		}
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: EEPROM size = %d. Must be %d (16k).\n",
		    __func__, val, AR_PCICFG_EEPROM_SIZE_16K);
		ecode = HAL_EESIZE;
		goto bad;
	}
	ecode = ath_hal_legacyEepromAttach(ah);
	if (ecode != HAL_OK) {
		goto bad;
	}
	ahp->ah_isHb63 = IS_2425(ah) && ath_hal_eepromGetFlag(ah, AR_EEP_ISTALON);

	/*
	 * If Bmode and AR5212, verify 2.4 analog exists
	 */
	if (ath_hal_eepromGetFlag(ah, AR_EEP_BMODE) &&
	    (AH_PRIVATE(ah)->ah_analog5GhzRev & 0xF0) == AR_RAD5111_SREV_MAJOR) {
		/*
		 * Set correct Baseband to analog shift
		 * setting to access analog chips.
		 */
		OS_REG_WRITE(ah, AR_PHY(0), 0x00004007);
		OS_DELAY(2000);
		AH_PRIVATE(ah)->ah_analog2GhzRev = ar5212GetRadioRev(ah);

		/* Set baseband for 5GHz chip */
		OS_REG_WRITE(ah, AR_PHY(0), 0x00000007);
		OS_DELAY(2000);
		if ((AH_PRIVATE(ah)->ah_analog2GhzRev & 0xF0) != AR_RAD2111_SREV_MAJOR) {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: 2G Radio Chip Rev 0x%02X is not "
			    "supported by this driver\n", __func__,
			    AH_PRIVATE(ah)->ah_analog2GhzRev);
			ecode = HAL_ENOTSUPP;
			goto bad;
		}
	}

	ecode = ath_hal_eepromGet(ah, AR_EEP_REGDMN_0, &eeval);
	if (ecode != HAL_OK) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: cannot read regulatory domain from EEPROM\n",
		    __func__);
		goto bad;
        }
	AH_PRIVATE(ah)->ah_currentRD = eeval;
	/* XXX record serial number */

	/*
	 * Got everything we need now to setup the capabilities.
	 */
	if (!ar5212FillCapabilityInfo(ah)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: failed ar5212FillCapabilityInfo\n", __func__);
		ecode = HAL_EEREAD;
		goto bad;
	}

	if (!rf->attach(ah, &ecode)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: RF setup failed, status %u\n",
		    __func__, ecode);
		goto bad;
	}
	/*
	 * Set noise floor adjust method; we arrange a
	 * direct call instead of thunking.
	 */
	AH_PRIVATE(ah)->ah_getNfAdjust = ahp->ah_rfHal->getNfAdjust;

	/* Initialize gain ladder thermal calibration structure */
	ar5212InitializeGainValues(ah);

	ecode = ath_hal_eepromGet(ah, AR_EEP_MACADDR, ahp->ah_macaddr);
	if (ecode != HAL_OK) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: error getting mac address from EEPROM\n", __func__);
		goto bad;
        }

	ar5212AniSetup(ah);
	/* Setup of Radar/AR structures happens in ath_hal_initchannels*/
	ar5212InitNfCalHistBuffer(ah);

	/* XXX EAR stuff goes here */

	HALDEBUG(ah, HAL_DEBUG_ATTACH, "%s: return\n", __func__);

	return ah;

bad:
	if (ahp)
		ar5212Detach((struct ath_hal *) ahp);
	if (status)
		*status = ecode;
	return AH_NULL;
#undef AH_EEPROM_PROTECT
}

void
ar5212Detach(struct ath_hal *ah)
{
	HALDEBUG(ah, HAL_DEBUG_ATTACH, "%s:\n", __func__);

	HALASSERT(ah != AH_NULL);
	HALASSERT(ah->ah_magic == AR5212_MAGIC);

	ar5212AniDetach(ah);
	ar5212RfDetach(ah);
	ar5212Disable(ah);
	ar5212SetPowerMode(ah, HAL_PM_FULL_SLEEP, AH_TRUE);

	ath_hal_eepromDetach(ah);
	ath_hal_free(ah);
}

HAL_BOOL
ar5212ChipTest(struct ath_hal *ah)
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
HAL_BOOL
ar5212GetChannelEdges(struct ath_hal *ah,
	uint16_t flags, uint16_t *low, uint16_t *high)
{
	if (flags & IEEE80211_CHAN_5GHZ) {
		*low = 4915;
		*high = 6100;
		return AH_TRUE;
	}
	if ((flags & IEEE80211_CHAN_2GHZ) &&
	    (ath_hal_eepromGetFlag(ah, AR_EEP_BMODE) ||
	     ath_hal_eepromGetFlag(ah, AR_EEP_GMODE))) {
		*low = 2312;
		*high = 2732;
		return AH_TRUE;
	}
	return AH_FALSE;
}

/*
 * Disable PLL when in L0s as well as receiver clock when in L1.
 * This power saving option must be enabled through the Serdes.
 *
 * Programming the Serdes must go through the same 288 bit serial shift
 * register as the other analog registers.  Hence the 9 writes.
 *
 * XXX Clean up the magic numbers.
 */
static void
ar5212ConfigPCIE(struct ath_hal *ah, HAL_BOOL restore, HAL_BOOL power_off)
{
	OS_REG_WRITE(ah, AR_PCIE_SERDES, 0x9248fc00);
	OS_REG_WRITE(ah, AR_PCIE_SERDES, 0x24924924);

	/* RX shut off when elecidle is asserted */
	OS_REG_WRITE(ah, AR_PCIE_SERDES, 0x28000039);
	OS_REG_WRITE(ah, AR_PCIE_SERDES, 0x53160824);
	OS_REG_WRITE(ah, AR_PCIE_SERDES, 0xe5980579);
                                                                                           
	/* Shut off PLL and CLKREQ active in L1 */
	OS_REG_WRITE(ah, AR_PCIE_SERDES, 0x001defff);
	OS_REG_WRITE(ah, AR_PCIE_SERDES, 0x1aaabe40);
	OS_REG_WRITE(ah, AR_PCIE_SERDES, 0xbe105554);
	OS_REG_WRITE(ah, AR_PCIE_SERDES, 0x000e3007);
                                                                                           
	/* Load the new settings */
	OS_REG_WRITE(ah, AR_PCIE_SERDES2, 0x00000000);
}

static void
ar5212DisablePCIE(struct ath_hal *ah)
{
	/* NB: fill in for 9100 */
}

/*
 * Fill all software cached or static hardware state information.
 * Return failure if capabilities are to come from EEPROM and
 * cannot be read.
 */
HAL_BOOL
ar5212FillCapabilityInfo(struct ath_hal *ah)
{
#define	AR_KEYTABLE_SIZE	128
#define	IS_GRIFFIN_LITE(ah) \
    (AH_PRIVATE(ah)->ah_macVersion == AR_SREV_VERSION_GRIFFIN && \
     AH_PRIVATE(ah)->ah_macRev == AR_SREV_GRIFFIN_LITE)
#define	IS_COBRA(ah) \
    (AH_PRIVATE(ah)->ah_macVersion == AR_SREV_VERSION_COBRA)
#define IS_2112(ah) \
	((AH_PRIVATE(ah)->ah_analog5GhzRev & 0xF0) == AR_RAD2112_SREV_MAJOR)

	struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
	HAL_CAPABILITIES *pCap = &ahpriv->ah_caps;
	uint16_t capField, val;

	/* Read the capability EEPROM location */
	if (ath_hal_eepromGet(ah, AR_EEP_OPCAP, &capField) != HAL_OK) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: unable to read caps from eeprom\n", __func__);
		return AH_FALSE;
	}
	if (IS_2112(ah))
		ath_hal_eepromSet(ah, AR_EEP_AMODE, AH_FALSE);
	if (capField == 0 && IS_GRIFFIN_LITE(ah)) {
		/*
		 * For griffin-lite cards with unprogrammed capabilities.
		 */
		ath_hal_eepromSet(ah, AR_EEP_COMPRESS, AH_FALSE);
		ath_hal_eepromSet(ah, AR_EEP_FASTFRAME, AH_FALSE);
		ath_hal_eepromSet(ah, AR_EEP_TURBO5DISABLE, AH_TRUE);
		ath_hal_eepromSet(ah, AR_EEP_TURBO2DISABLE, AH_TRUE);
		HALDEBUG(ah, HAL_DEBUG_ATTACH,
		    "%s: override caps for griffin-lite, now 0x%x (+!turbo)\n",
		    __func__, capField);
	}

	/* Modify reg domain on newer cards that need to work with older sw */
	if (ahpriv->ah_opmode != HAL_M_HOSTAP &&
	    ahpriv->ah_subvendorid == AR_SUBVENDOR_ID_NEW_A) {
		if (ahpriv->ah_currentRD == 0x64 ||
		    ahpriv->ah_currentRD == 0x65)
			ahpriv->ah_currentRD += 5;
		else if (ahpriv->ah_currentRD == 0x41)
			ahpriv->ah_currentRD = 0x43;
		HALDEBUG(ah, HAL_DEBUG_ATTACH, "%s: regdomain mapped to 0x%x\n",
		    __func__, ahpriv->ah_currentRD);
	}

	if (AH_PRIVATE(ah)->ah_macVersion == AR_SREV_2417 ||
	    AH_PRIVATE(ah)->ah_macVersion == AR_SREV_2425) {
		HALDEBUG(ah, HAL_DEBUG_ATTACH,
		    "%s: enable Bmode and disable turbo for Swan/Nala\n",
		    __func__);
		ath_hal_eepromSet(ah, AR_EEP_BMODE, AH_TRUE);
		ath_hal_eepromSet(ah, AR_EEP_COMPRESS, AH_FALSE);
		ath_hal_eepromSet(ah, AR_EEP_FASTFRAME, AH_FALSE);
		ath_hal_eepromSet(ah, AR_EEP_TURBO5DISABLE, AH_TRUE);
		ath_hal_eepromSet(ah, AR_EEP_TURBO2DISABLE, AH_TRUE);
	}

	/* Construct wireless mode from EEPROM */
	pCap->halWirelessModes = 0;
	if (ath_hal_eepromGetFlag(ah, AR_EEP_AMODE)) {
		pCap->halWirelessModes |= HAL_MODE_11A;
		if (!ath_hal_eepromGetFlag(ah, AR_EEP_TURBO5DISABLE))
			pCap->halWirelessModes |= HAL_MODE_TURBO;
	}
	if (ath_hal_eepromGetFlag(ah, AR_EEP_BMODE))
		pCap->halWirelessModes |= HAL_MODE_11B;
	if (ath_hal_eepromGetFlag(ah, AR_EEP_GMODE) &&
	    ahpriv->ah_subvendorid != AR_SUBVENDOR_ID_NOG) {
		pCap->halWirelessModes |= HAL_MODE_11G;
		if (!ath_hal_eepromGetFlag(ah, AR_EEP_TURBO2DISABLE))
			pCap->halWirelessModes |= HAL_MODE_108G;
	}

	pCap->halLow2GhzChan = 2312;
	/* XXX 2417 too? */
	if (IS_RAD5112_ANY(ah) || IS_5413(ah) || IS_2425(ah) ||  IS_2417(ah))
		pCap->halHigh2GhzChan = 2500;
	else
		pCap->halHigh2GhzChan = 2732;

	/*
	 * For AR5111 version < 4, the lowest centre frequency supported is
	 * 5130MHz.  For AR5111 version 4, the 4.9GHz channels are supported
	 * but only in 10MHz increments.
	 *
	 * In addition, the programming method is wrong - it uses the IEEE
	 * channel number to calculate the frequency, rather than the
	 * channel centre.  Since half/quarter rates re-use some of the
	 * 5GHz channel IEEE numbers, this will result in a badly programmed
	 * synth.
	 *
	 * Until the relevant support is written, just limit lower frequency
	 * support for AR5111 so things aren't incorrectly programmed.
	 *
	 * XXX It's also possible this code doesn't correctly limit the
	 * centre frequencies of potential channels; this is very important
	 * for half/quarter rate!
	 */
	if (AH_RADIO_MAJOR(ah) == AR_RAD5111_SREV_MAJOR) {
		pCap->halLow5GhzChan = 5120; /* XXX lowest centre = 5130MHz */
	} else {
		pCap->halLow5GhzChan = 4915;
	}
	pCap->halHigh5GhzChan = 6100;

	pCap->halCipherCkipSupport = AH_FALSE;
	pCap->halCipherTkipSupport = AH_TRUE;
	pCap->halCipherAesCcmSupport =
		(ath_hal_eepromGetFlag(ah, AR_EEP_AES) &&
		 ((AH_PRIVATE(ah)->ah_macVersion > AR_SREV_VERSION_VENICE) ||
		  ((AH_PRIVATE(ah)->ah_macVersion == AR_SREV_VERSION_VENICE) &&
		   (AH_PRIVATE(ah)->ah_macRev >= AR_SREV_VERSION_OAHU))));

	pCap->halMicCkipSupport    = AH_FALSE;
	pCap->halMicTkipSupport    = AH_TRUE;
	pCap->halMicAesCcmSupport  = ath_hal_eepromGetFlag(ah, AR_EEP_AES);
	/*
	 * Starting with Griffin TX+RX mic keys can be combined
	 * in one key cache slot.
	 */
	if (AH_PRIVATE(ah)->ah_macVersion >= AR_SREV_VERSION_GRIFFIN)
		pCap->halTkipMicTxRxKeySupport = AH_TRUE;
	else
		pCap->halTkipMicTxRxKeySupport = AH_FALSE;
	pCap->halChanSpreadSupport = AH_TRUE;
	pCap->halSleepAfterBeaconBroken = AH_TRUE;

	if (ahpriv->ah_macRev > 1 || IS_COBRA(ah)) {
		pCap->halCompressSupport   =
			ath_hal_eepromGetFlag(ah, AR_EEP_COMPRESS) &&
			(pCap->halWirelessModes & (HAL_MODE_11A|HAL_MODE_11G)) != 0;
		pCap->halBurstSupport = ath_hal_eepromGetFlag(ah, AR_EEP_BURST);
		pCap->halFastFramesSupport =
			ath_hal_eepromGetFlag(ah, AR_EEP_FASTFRAME) &&
			(pCap->halWirelessModes & (HAL_MODE_11A|HAL_MODE_11G)) != 0;
		pCap->halChapTuningSupport = AH_TRUE;
		pCap->halTurboPrimeSupport = AH_TRUE;
	}
	pCap->halTurboGSupport = pCap->halWirelessModes & HAL_MODE_108G;

	pCap->halPSPollBroken = AH_TRUE;	/* XXX fixed in later revs? */
	pCap->halNumMRRetries = 4;		/* Hardware supports 4 MRR */
	pCap->halNumTxMaps = 1;			/* Single TX ptr per descr */
	pCap->halVEOLSupport = AH_TRUE;
	pCap->halBssIdMaskSupport = AH_TRUE;
	pCap->halMcastKeySrchSupport = AH_TRUE;
	if ((ahpriv->ah_macVersion == AR_SREV_VERSION_VENICE &&
	     ahpriv->ah_macRev == 8) ||
	    ahpriv->ah_macVersion > AR_SREV_VERSION_VENICE)
		pCap->halTsfAddSupport = AH_TRUE;

	if (ath_hal_eepromGet(ah, AR_EEP_MAXQCU, &val) == HAL_OK)
		pCap->halTotalQueues = val;
	else
		pCap->halTotalQueues = HAL_NUM_TX_QUEUES;

	if (ath_hal_eepromGet(ah, AR_EEP_KCENTRIES, &val) == HAL_OK)
		pCap->halKeyCacheSize = val;
	else
		pCap->halKeyCacheSize = AR_KEYTABLE_SIZE;

	pCap->halChanHalfRate = AH_TRUE;
	pCap->halChanQuarterRate = AH_TRUE;

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

	/* NB: this is a guess, no one seems to know the answer */
	ahpriv->ah_rxornIsFatal =
	    (AH_PRIVATE(ah)->ah_macVersion < AR_SREV_VERSION_VENICE);

	/* enable features that first appeared in Hainan */
	if ((AH_PRIVATE(ah)->ah_macVersion == AR_SREV_VERSION_VENICE &&
	     AH_PRIVATE(ah)->ah_macRev == AR_SREV_HAINAN) ||
	    AH_PRIVATE(ah)->ah_macVersion > AR_SREV_VERSION_VENICE) {
		/* h/w phy counters */
		pCap->halHwPhyCounterSupport = AH_TRUE;
		/* bssid match disable */
		pCap->halBssidMatchSupport = AH_TRUE;
	}

	pCap->halRxTstampPrecision = 15;
	pCap->halTxTstampPrecision = 16;
	pCap->halIntrMask = HAL_INT_COMMON
			| HAL_INT_RX
			| HAL_INT_TX
			| HAL_INT_FATAL
			| HAL_INT_BNR
			| HAL_INT_BMISC
			;
	if (AH_PRIVATE(ah)->ah_macVersion < AR_SREV_VERSION_GRIFFIN)
		pCap->halIntrMask &= ~HAL_INT_TBTT;

	pCap->hal4kbSplitTransSupport = AH_TRUE;
	pCap->halHasRxSelfLinkedTail = AH_TRUE;

	return AH_TRUE;
#undef IS_COBRA
#undef IS_GRIFFIN_LITE
#undef AR_KEYTABLE_SIZE
}

static const char*
ar5212Probe(uint16_t vendorid, uint16_t devid)
{
	if (vendorid == ATHEROS_VENDOR_ID ||
	    vendorid == ATHEROS_3COM_VENDOR_ID ||
	    vendorid == ATHEROS_3COM2_VENDOR_ID) {
		switch (devid) {
		case AR5212_FPGA:
			return "Atheros 5212 (FPGA)";
		case AR5212_DEVID:
		case AR5212_DEVID_IBM:
		case AR5212_DEFAULT:
			return "Atheros 5212";
		case AR5212_AR2413:
			return "Atheros 2413";
		case AR5212_AR2417:
			return "Atheros 2417";
		case AR5212_AR5413:
			return "Atheros 5413";
		case AR5212_AR5424:
			return "Atheros 5424/2424";
		}
	}
	return AH_NULL;
}
AH_CHIP(AR5212, ar5212Probe, ar5212Attach);

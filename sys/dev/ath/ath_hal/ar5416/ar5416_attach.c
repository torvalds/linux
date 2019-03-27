/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
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

#include "ah_eeprom_v14.h"

#include "ar5416/ar5416.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"

#include "ar5416/ar5416.ini"

static void ar5416ConfigPCIE(struct ath_hal *ah, HAL_BOOL restore,
		HAL_BOOL power_off);
static void ar5416DisablePCIE(struct ath_hal *ah);
static void ar5416WriteIni(struct ath_hal *ah,
	    const struct ieee80211_channel *chan);
static void ar5416SpurMitigate(struct ath_hal *ah,
	    const struct ieee80211_channel *chan);

static void
ar5416AniSetup(struct ath_hal *ah)
{
	static const struct ar5212AniParams aniparams = {
		.maxNoiseImmunityLevel	= 4,	/* levels 0..4 */
		.totalSizeDesired	= { -55, -55, -55, -55, -62 },
		.coarseHigh		= { -14, -14, -14, -14, -12 },
		.coarseLow		= { -64, -64, -64, -64, -70 },
		.firpwr			= { -78, -78, -78, -78, -80 },
		.maxSpurImmunityLevel	= 7,
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
	/* NB: disable ANI noise immmunity for reliable RIFS rx */
	AH5416(ah)->ah_ani_function &= ~(1 << HAL_ANI_NOISE_IMMUNITY_LEVEL);
	ar5416AniAttach(ah, &aniparams, &aniparams, AH_TRUE);
}

/*
 * AR5416 doesn't do OLC or temperature compensation.
 */
static void
ar5416olcInit(struct ath_hal *ah)
{
}

static void
ar5416olcTempCompensation(struct ath_hal *ah)
{
}

/*
 * Attach for an AR5416 part.
 */
void
ar5416InitState(struct ath_hal_5416 *ahp5416, uint16_t devid, HAL_SOFTC sc,
	HAL_BUS_TAG st, HAL_BUS_HANDLE sh, HAL_STATUS *status)
{
	struct ath_hal_5212 *ahp;
	struct ath_hal *ah;

	ahp = &ahp5416->ah_5212;
	ar5212InitState(ahp, devid, sc, st, sh, status);
	ah = &ahp->ah_priv.h;

	/* override 5212 methods for our needs */
	ah->ah_magic			= AR5416_MAGIC;
	ah->ah_getRateTable		= ar5416GetRateTable;
	ah->ah_detach			= ar5416Detach;

	/* Reset functions */
	ah->ah_reset			= ar5416Reset;
	ah->ah_phyDisable		= ar5416PhyDisable;
	ah->ah_disable			= ar5416Disable;
	ah->ah_configPCIE		= ar5416ConfigPCIE;
	ah->ah_disablePCIE		= ar5416DisablePCIE;
	ah->ah_perCalibration		= ar5416PerCalibration;
	ah->ah_perCalibrationN		= ar5416PerCalibrationN;
	ah->ah_resetCalValid		= ar5416ResetCalValid;
	ah->ah_setTxPowerLimit		= ar5416SetTxPowerLimit;
	ah->ah_setTxPower		= ar5416SetTransmitPower;
	ah->ah_setBoardValues		= ar5416SetBoardValues;

	/* Transmit functions */
	ah->ah_stopTxDma		= ar5416StopTxDma;
	ah->ah_setupTxDesc		= ar5416SetupTxDesc;
	ah->ah_setupXTxDesc		= ar5416SetupXTxDesc;
	ah->ah_fillTxDesc		= ar5416FillTxDesc;
	ah->ah_procTxDesc		= ar5416ProcTxDesc;
	ah->ah_getTxCompletionRates	= ar5416GetTxCompletionRates;
	ah->ah_setupTxQueue		= ar5416SetupTxQueue;
	ah->ah_resetTxQueue		= ar5416ResetTxQueue;

	/* Receive Functions */
	ah->ah_getRxFilter		= ar5416GetRxFilter;
	ah->ah_setRxFilter		= ar5416SetRxFilter;
	ah->ah_stopDmaReceive		= ar5416StopDmaReceive;
	ah->ah_startPcuReceive		= ar5416StartPcuReceive;
	ah->ah_stopPcuReceive		= ar5416StopPcuReceive;
	ah->ah_setupRxDesc		= ar5416SetupRxDesc;
	ah->ah_procRxDesc		= ar5416ProcRxDesc;
	ah->ah_rxMonitor		= ar5416RxMonitor;
	ah->ah_aniPoll			= ar5416AniPoll;
	ah->ah_procMibEvent		= ar5416ProcessMibIntr;

	/* Misc Functions */
	ah->ah_getCapability		= ar5416GetCapability;
	ah->ah_setCapability		= ar5416SetCapability;
	ah->ah_getDiagState		= ar5416GetDiagState;
	ah->ah_setLedState		= ar5416SetLedState;
	ah->ah_gpioCfgOutput		= ar5416GpioCfgOutput;
	ah->ah_gpioCfgInput		= ar5416GpioCfgInput;
	ah->ah_gpioGet			= ar5416GpioGet;
	ah->ah_gpioSet			= ar5416GpioSet;
	ah->ah_gpioSetIntr		= ar5416GpioSetIntr;
	ah->ah_getTsf64			= ar5416GetTsf64;
	ah->ah_setTsf64			= ar5416SetTsf64;
	ah->ah_resetTsf			= ar5416ResetTsf;
	ah->ah_getRfGain		= ar5416GetRfgain;
	ah->ah_setAntennaSwitch		= ar5416SetAntennaSwitch;
	ah->ah_setDecompMask		= ar5416SetDecompMask;
	ah->ah_setCoverageClass		= ar5416SetCoverageClass;
	ah->ah_setQuiet			= ar5416SetQuiet;
	ah->ah_getMibCycleCounts	= ar5416GetMibCycleCounts;
	ah->ah_setChainMasks		= ar5416SetChainMasks;

	ah->ah_resetKeyCacheEntry	= ar5416ResetKeyCacheEntry;
	ah->ah_setKeyCacheEntry		= ar5416SetKeyCacheEntry;

	/* DFS Functions */
	ah->ah_enableDfs		= ar5416EnableDfs;
	ah->ah_getDfsThresh		= ar5416GetDfsThresh;
	ah->ah_getDfsDefaultThresh	= ar5416GetDfsDefaultThresh;
	ah->ah_procRadarEvent		= ar5416ProcessRadarEvent;
	ah->ah_isFastClockEnabled	= ar5416IsFastClockEnabled;

	/* Spectral Scan Functions */
	ah->ah_spectralConfigure	= ar5416ConfigureSpectralScan;
	ah->ah_spectralGetConfig	= ar5416GetSpectralParams;
	ah->ah_spectralStart		= ar5416StartSpectralScan;
	ah->ah_spectralStop		= ar5416StopSpectralScan;
	ah->ah_spectralIsEnabled	= ar5416IsSpectralEnabled;
	ah->ah_spectralIsActive		= ar5416IsSpectralActive;

	/* Power Management Functions */
	ah->ah_setPowerMode		= ar5416SetPowerMode;

	/* Beacon Management Functions */
	ah->ah_setBeaconTimers		= ar5416SetBeaconTimers;
	ah->ah_beaconInit		= ar5416BeaconInit;
	ah->ah_setStationBeaconTimers	= ar5416SetStaBeaconTimers;
	ah->ah_resetStationBeaconTimers	= ar5416ResetStaBeaconTimers;
	ah->ah_getNextTBTT		= ar5416GetNextTBTT;

	/* 802.11n Functions */
	ah->ah_chainTxDesc		= ar5416ChainTxDesc;
	ah->ah_setupFirstTxDesc		= ar5416SetupFirstTxDesc;
	ah->ah_setupLastTxDesc		= ar5416SetupLastTxDesc;
	ah->ah_set11nRateScenario	= ar5416Set11nRateScenario;
	ah->ah_set11nAggrFirst		= ar5416Set11nAggrFirst;
	ah->ah_set11nAggrMiddle		= ar5416Set11nAggrMiddle;
	ah->ah_set11nAggrLast		= ar5416Set11nAggrLast;
	ah->ah_clr11nAggr		= ar5416Clr11nAggr;
	ah->ah_set11nBurstDuration	= ar5416Set11nBurstDuration;
	ah->ah_get11nExtBusy		= ar5416Get11nExtBusy;
	ah->ah_set11nMac2040		= ar5416Set11nMac2040;
	ah->ah_get11nRxClear		= ar5416Get11nRxClear;
	ah->ah_set11nRxClear		= ar5416Set11nRxClear;
	ah->ah_set11nVirtMoreFrag	= ar5416Set11nVirtualMoreFrag;

	/* Interrupt functions */
	ah->ah_isInterruptPending	= ar5416IsInterruptPending;
	ah->ah_getPendingInterrupts	= ar5416GetPendingInterrupts;
	ah->ah_setInterrupts		= ar5416SetInterrupts;

	/* Bluetooth Coexistence functions */
	ah->ah_btCoexSetInfo		= ar5416SetBTCoexInfo;
	ah->ah_btCoexSetConfig		= ar5416BTCoexConfig;
	ah->ah_btCoexSetQcuThresh	= ar5416BTCoexSetQcuThresh;
	ah->ah_btCoexSetWeights		= ar5416BTCoexSetWeights;
	ah->ah_btCoexSetBmissThresh	= ar5416BTCoexSetupBmissThresh;
	ah->ah_btCoexSetParameter	= ar5416BTCoexSetParameter;
	ah->ah_btCoexDisable		= ar5416BTCoexDisable;
	ah->ah_btCoexEnable		= ar5416BTCoexEnable;
	AH5416(ah)->ah_btCoexSetDiversity = ar5416BTCoexAntennaDiversity;

	ahp->ah_priv.ah_getWirelessModes= ar5416GetWirelessModes;
	ahp->ah_priv.ah_eepromRead	= ar5416EepromRead;
#ifdef AH_SUPPORT_WRITE_EEPROM
	ahp->ah_priv.ah_eepromWrite	= ar5416EepromWrite;
#endif
	ahp->ah_priv.ah_getChipPowerLimits = ar5416GetChipPowerLimits;

	/* Internal ops */
	AH5416(ah)->ah_writeIni		= ar5416WriteIni;
	AH5416(ah)->ah_spurMitigate	= ar5416SpurMitigate;

	/* Internal baseband ops */
	AH5416(ah)->ah_initPLL		= ar5416InitPLL;

	/* Internal calibration ops */
	AH5416(ah)->ah_cal_initcal	= ar5416InitCalHardware;

	/* Internal TX power control related operations */
	AH5416(ah)->ah_olcInit = ar5416olcInit;
	AH5416(ah)->ah_olcTempCompensation	= ar5416olcTempCompensation;
	AH5416(ah)->ah_setPowerCalTable	= ar5416SetPowerCalTable;

	/*
	 * Start by setting all Owl devices to 2x2
	 */
	AH5416(ah)->ah_rx_chainmask = AR5416_DEFAULT_RXCHAINMASK;
	AH5416(ah)->ah_tx_chainmask = AR5416_DEFAULT_TXCHAINMASK;

	/* Enable all ANI functions to begin with */
	AH5416(ah)->ah_ani_function = 0xffffffff;

	/* Set overridable ANI methods */
	AH5212(ah)->ah_aniControl = ar5416AniControl;

	/*
	 * Default FIFO Trigger levels
	 *
	 * These define how filled the TX FIFO needs to be before
	 * the baseband begins to be given some data.
	 *
	 * To be paranoid, we ensure that the TX trigger level always
	 * has at least enough space for two TX DMA to occur.
	 * The TX DMA size is currently hard-coded to AR_TXCFG_DMASZ_128B.
	 * That means we need to leave at least 256 bytes available in
	 * the TX DMA FIFO.
	 */
#define	AR_FTRIG_512B	0x00000080 // 5 bits total
	/*
	 * AR9285/AR9271 have half the size TX FIFO compared to
	 * other devices
	 */
	if (AR_SREV_KITE(ah) || AR_SREV_9271(ah)) {
		AH5212(ah)->ah_txTrigLev = (AR_FTRIG_256B >> AR_FTRIG_S);
		AH5212(ah)->ah_maxTxTrigLev = ((2048 / 64) - 1);
	} else {
		AH5212(ah)->ah_txTrigLev = (AR_FTRIG_512B >> AR_FTRIG_S);
		AH5212(ah)->ah_maxTxTrigLev = ((4096 / 64) - 1);
	}
#undef	AR_FTRIG_512B

	/* And now leave some headspace - 256 bytes */
	AH5212(ah)->ah_maxTxTrigLev -= 4;
}

uint32_t
ar5416GetRadioRev(struct ath_hal *ah)
{
	uint32_t val;
	int i;

	/* Read Radio Chip Rev Extract */
	OS_REG_WRITE(ah, AR_PHY(0x36), 0x00007058);
	for (i = 0; i < 8; i++)
		OS_REG_WRITE(ah, AR_PHY(0x20), 0x00010000);
	val = (OS_REG_READ(ah, AR_PHY(256)) >> 24) & 0xff;
	val = ((val & 0xf0) >> 4) | ((val & 0x0f) << 4);
	return ath_hal_reverseBits(val, 8);
}

/*
 * Attach for an AR5416 part.
 */
static struct ath_hal *
ar5416Attach(uint16_t devid, HAL_SOFTC sc,
	HAL_BUS_TAG st, HAL_BUS_HANDLE sh, uint16_t *eepromdata,
	HAL_OPS_CONFIG *ah_config, HAL_STATUS *status)
{
	struct ath_hal_5416 *ahp5416;
	struct ath_hal_5212 *ahp;
	struct ath_hal *ah;
	uint32_t val;
	HAL_STATUS ecode;
	HAL_BOOL rfStatus;

	HALDEBUG(AH_NULL, HAL_DEBUG_ATTACH, "%s: sc %p st %p sh %p\n",
	    __func__, sc, (void*) st, (void*) sh);

	/* NB: memory is returned zero'd */
	ahp5416 = ath_hal_malloc(sizeof (struct ath_hal_5416) +
		/* extra space for Owl 2.1/2.2 WAR */
		sizeof(ar5416Addac)
	);
	if (ahp5416 == AH_NULL) {
		HALDEBUG(AH_NULL, HAL_DEBUG_ANY,
		    "%s: cannot allocate memory for state block\n", __func__);
		*status = HAL_ENOMEM;
		return AH_NULL;
	}
	ar5416InitState(ahp5416, devid, sc, st, sh, status);
	ahp = &ahp5416->ah_5212;
	ah = &ahp->ah_priv.h;

	if (!ar5416SetResetReg(ah, HAL_RESET_POWER_ON)) {
		/* reset chip */
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: couldn't reset chip\n", __func__);
		ecode = HAL_EIO;
		goto bad;
	}

	if (!ar5416SetPowerMode(ah, HAL_PM_AWAKE, AH_TRUE)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: couldn't wakeup chip\n", __func__);
		ecode = HAL_EIO;
		goto bad;
	}
	/* Read Revisions from Chips before taking out of reset */
	val = OS_REG_READ(ah, AR_SREV) & AR_SREV_ID;
	AH_PRIVATE(ah)->ah_macVersion = val >> AR_SREV_ID_S;
	AH_PRIVATE(ah)->ah_macRev = val & AR_SREV_REVISION;
	AH_PRIVATE(ah)->ah_ispcie = (devid == AR5416_DEVID_PCIE);

	/* setup common ini data; rf backends handle remainder */
	HAL_INI_INIT(&ahp->ah_ini_modes, ar5416Modes, 6);
	HAL_INI_INIT(&ahp->ah_ini_common, ar5416Common, 2);

	HAL_INI_INIT(&AH5416(ah)->ah_ini_bb_rfgain, ar5416BB_RfGain, 3);
	HAL_INI_INIT(&AH5416(ah)->ah_ini_bank0, ar5416Bank0, 2);
	HAL_INI_INIT(&AH5416(ah)->ah_ini_bank1, ar5416Bank1, 2);
	HAL_INI_INIT(&AH5416(ah)->ah_ini_bank2, ar5416Bank2, 2);
	HAL_INI_INIT(&AH5416(ah)->ah_ini_bank3, ar5416Bank3, 3);
	HAL_INI_INIT(&AH5416(ah)->ah_ini_bank6, ar5416Bank6, 3);
	HAL_INI_INIT(&AH5416(ah)->ah_ini_bank7, ar5416Bank7, 2);
	HAL_INI_INIT(&AH5416(ah)->ah_ini_addac, ar5416Addac, 2);

	if (! IS_5416V2_2(ah)) {		/* Owl 2.1/2.0 */
		ath_hal_printf(ah, "[ath] Enabling CLKDRV workaround for AR5416 < v2.2\n");
		struct ini {
			uint32_t	*data;		/* NB: !const */
			int		rows, cols;
		};
		/* override CLKDRV value */
		OS_MEMCPY(&AH5416(ah)[1], ar5416Addac, sizeof(ar5416Addac));
		AH5416(ah)->ah_ini_addac.data = (uint32_t *) &AH5416(ah)[1];
		HAL_INI_VAL((struct ini *)&AH5416(ah)->ah_ini_addac, 31, 1) = 0;
	}

	HAL_INI_INIT(&AH5416(ah)->ah_ini_pcieserdes, ar5416PciePhy, 2);
	ar5416AttachPCIE(ah);

	ecode = ath_hal_v14EepromAttach(ah);
	if (ecode != HAL_OK)
		goto bad;

	if (!ar5416ChipReset(ah, AH_NULL)) {	/* reset chip */
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: chip reset failed\n",
		    __func__);
		ecode = HAL_EIO;
		goto bad;
	}

	AH_PRIVATE(ah)->ah_phyRev = OS_REG_READ(ah, AR_PHY_CHIP_ID);

	if (!ar5212ChipTest(ah)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: hardware self-test failed\n",
		    __func__);
		ecode = HAL_ESELFTEST;
		goto bad;
	}

	/*
	 * Set correct Baseband to analog shift
	 * setting to access analog chips.
	 */
	OS_REG_WRITE(ah, AR_PHY(0), 0x00000007);

	/* Read Radio Chip Rev Extract */
	AH_PRIVATE(ah)->ah_analog5GhzRev = ar5416GetRadioRev(ah);
	switch (AH_PRIVATE(ah)->ah_analog5GhzRev & AR_RADIO_SREV_MAJOR) {
        case AR_RAD5122_SREV_MAJOR:	/* Fowl: 5G/2x2 */
        case AR_RAD2122_SREV_MAJOR:	/* Fowl: 2+5G/2x2 */
        case AR_RAD2133_SREV_MAJOR:	/* Fowl: 2G/3x3 */
	case AR_RAD5133_SREV_MAJOR:	/* Fowl: 2+5G/3x3 */
		break;
	default:
		if (AH_PRIVATE(ah)->ah_analog5GhzRev == 0) {
			/*
			 * When RF_Silen is used the analog chip is reset.
			 * So when the system boots with radio switch off
			 * the RF chip rev reads back as zero and we need
			 * to use the mac+phy revs to set the radio rev.
			 */
			AH_PRIVATE(ah)->ah_analog5GhzRev =
				AR_RAD5133_SREV_MAJOR;
			break;
		}
		/* NB: silently accept anything in release code per Atheros */
#ifdef AH_DEBUG
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: 5G Radio Chip Rev 0x%02X is not supported by "
		    "this driver\n", __func__,
		    AH_PRIVATE(ah)->ah_analog5GhzRev);
		ecode = HAL_ENOTSUPP;
		goto bad;
#endif
	}

	/*
	 * Got everything we need now to setup the capabilities.
	 */
	if (!ar5416FillCapabilityInfo(ah)) {
		ecode = HAL_EEREAD;
		goto bad;
	}

	ecode = ath_hal_eepromGet(ah, AR_EEP_MACADDR, ahp->ah_macaddr);
	if (ecode != HAL_OK) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: error getting mac address from EEPROM\n", __func__);
		goto bad;
        }
	/* XXX How about the serial number ? */
	/* Read Reg Domain */
	AH_PRIVATE(ah)->ah_currentRD =
	    ath_hal_eepromGet(ah, AR_EEP_REGDMN_0, AH_NULL);
	AH_PRIVATE(ah)->ah_currentRDext =
	    ath_hal_eepromGet(ah, AR_EEP_REGDMN_1, AH_NULL);

	/*
	 * ah_miscMode is populated by ar5416FillCapabilityInfo()
	 * starting from griffin. Set here to make sure that
	 * AR_MISC_MODE_MIC_NEW_LOC_ENABLE is set before a GTK is
	 * placed into hardware.
	 */
	if (ahp->ah_miscMode != 0)
		OS_REG_WRITE(ah, AR_MISC_MODE, OS_REG_READ(ah, AR_MISC_MODE) | ahp->ah_miscMode);

	rfStatus = ar2133RfAttach(ah, &ecode);
	if (!rfStatus) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: RF setup failed, status %u\n",
		    __func__, ecode);
		goto bad;
	}

	ar5416AniSetup(ah);			/* Anti Noise Immunity */

	AH5416(ah)->nf_2g.max = AR_PHY_CCA_MAX_GOOD_VAL_5416_2GHZ;
	AH5416(ah)->nf_2g.min = AR_PHY_CCA_MIN_GOOD_VAL_5416_2GHZ;
	AH5416(ah)->nf_2g.nominal = AR_PHY_CCA_NOM_VAL_5416_2GHZ;
	AH5416(ah)->nf_5g.max = AR_PHY_CCA_MAX_GOOD_VAL_5416_5GHZ;
	AH5416(ah)->nf_5g.min = AR_PHY_CCA_MIN_GOOD_VAL_5416_5GHZ;
	AH5416(ah)->nf_5g.nominal = AR_PHY_CCA_NOM_VAL_5416_5GHZ;

	ar5416InitNfHistBuff(AH5416(ah)->ah_cal.nfCalHist);

	HALDEBUG(ah, HAL_DEBUG_ATTACH, "%s: return\n", __func__);

	return ah;
bad:
	if (ahp)
		ar5416Detach((struct ath_hal *) ahp);
	if (status)
		*status = ecode;
	return AH_NULL;
}

void
ar5416Detach(struct ath_hal *ah)
{
	HALDEBUG(ah, HAL_DEBUG_ATTACH, "%s:\n", __func__);

	HALASSERT(ah != AH_NULL);
	HALASSERT(ah->ah_magic == AR5416_MAGIC);

	/* Make sure that chip is awake before writing to it */
	if (! ar5416SetPowerMode(ah, HAL_PM_AWAKE, AH_TRUE))
		HALDEBUG(ah, HAL_DEBUG_UNMASKABLE,
		    "%s: failed to wake up chip\n",
		    __func__);

	ar5416AniDetach(ah);
	ar5212RfDetach(ah);
	ah->ah_disable(ah);
	ar5416SetPowerMode(ah, HAL_PM_FULL_SLEEP, AH_TRUE);
	ath_hal_eepromDetach(ah);
	ath_hal_free(ah);
}

void
ar5416AttachPCIE(struct ath_hal *ah)
{
	if (AH_PRIVATE(ah)->ah_ispcie)
		ath_hal_configPCIE(ah, AH_FALSE, AH_FALSE);
	else
		ath_hal_disablePCIE(ah);
}

static void
ar5416ConfigPCIE(struct ath_hal *ah, HAL_BOOL restore, HAL_BOOL power_off)
{

	/* This is only applicable for AR5418 (AR5416 PCIe) */
	if (! AH_PRIVATE(ah)->ah_ispcie)
		return;

	if (! restore) {
		ath_hal_ini_write(ah, &AH5416(ah)->ah_ini_pcieserdes, 1, 0);
		OS_DELAY(1000);
	}

	if (power_off) {		/* Power-off */
		/* clear bit 19 to disable L1 */
		OS_REG_CLR_BIT(ah, AR_PCIE_PM_CTRL, AR_PCIE_PM_CTRL_ENA);
	} else {			/* Power-on */
		/* Set default WAR values for Owl */
		OS_REG_WRITE(ah, AR_WA, AR_WA_DEFAULT);

		/* set bit 19 to allow forcing of pcie core into L1 state */
		OS_REG_SET_BIT(ah, AR_PCIE_PM_CTRL, AR_PCIE_PM_CTRL_ENA);
	}
}

/*
 * Disable PCIe PHY if PCIe isn't used.
 */
static void
ar5416DisablePCIE(struct ath_hal *ah)
{

	/* PCIe? Don't */
	if (AH_PRIVATE(ah)->ah_ispcie)
		return;

	/* .. Only applicable for AR5416v2 or later */
	if (! (AR_SREV_OWL(ah) && AR_SREV_OWL_20_OR_LATER(ah)))
		return;

	OS_REG_WRITE_BUFFER_ENABLE(ah);

	/*
	 * Disable the PCIe PHY.
	 */
	OS_REG_WRITE(ah, AR_PCIE_SERDES, 0x9248fc00);
	OS_REG_WRITE(ah, AR_PCIE_SERDES, 0x24924924);
	OS_REG_WRITE(ah, AR_PCIE_SERDES, 0x28000029);
	OS_REG_WRITE(ah, AR_PCIE_SERDES, 0x57160824);
	OS_REG_WRITE(ah, AR_PCIE_SERDES, 0x25980579);
	OS_REG_WRITE(ah, AR_PCIE_SERDES, 0x00000000);
	OS_REG_WRITE(ah, AR_PCIE_SERDES, 0x1aaabe40);
	OS_REG_WRITE(ah, AR_PCIE_SERDES, 0xbe105554);
	OS_REG_WRITE(ah, AR_PCIE_SERDES, 0x000e1007);

	/* Load the new settings */
	OS_REG_WRITE(ah, AR_PCIE_SERDES2, 0x00000000);

	OS_REG_WRITE_BUFFER_FLUSH(ah);
	OS_REG_WRITE_BUFFER_DISABLE(ah);
}

static void
ar5416WriteIni(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	u_int modesIndex, freqIndex;
	int regWrites = 0;

	/* Setup the indices for the next set of register array writes */
	/* XXX Ignore 11n dynamic mode on the AR5416 for the moment */
	if (IEEE80211_IS_CHAN_2GHZ(chan)) {
		freqIndex = 2;
		if (IEEE80211_IS_CHAN_HT40(chan))
			modesIndex = 3;
		else if (IEEE80211_IS_CHAN_108G(chan))
			modesIndex = 5;
		else
			modesIndex = 4;
	} else {
		freqIndex = 1;
		if (IEEE80211_IS_CHAN_HT40(chan) ||
		    IEEE80211_IS_CHAN_TURBO(chan))
			modesIndex = 2;
		else
			modesIndex = 1;
	}

	/* Set correct Baseband to analog shift setting to access analog chips. */
	OS_REG_WRITE(ah, AR_PHY(0), 0x00000007);

	/*
	 * Write addac shifts
	 */
	OS_REG_WRITE(ah, AR_PHY_ADC_SERIAL_CTL, AR_PHY_SEL_EXTERNAL_RADIO);

	/* NB: only required for Sowl */
	if (AR_SREV_SOWL(ah))
		ar5416EepromSetAddac(ah, chan);

	regWrites = ath_hal_ini_write(ah, &AH5416(ah)->ah_ini_addac, 1,
	    regWrites);
	OS_REG_WRITE(ah, AR_PHY_ADC_SERIAL_CTL, AR_PHY_SEL_INTERNAL_ADDAC);

	regWrites = ath_hal_ini_write(ah, &AH5212(ah)->ah_ini_modes,
	    modesIndex, regWrites);
	regWrites = ath_hal_ini_write(ah, &AH5212(ah)->ah_ini_common,
	    1, regWrites);

	/* XXX updated regWrites? */
	AH5212(ah)->ah_rfHal->writeRegs(ah, modesIndex, freqIndex, regWrites);
}

/*
 * Convert to baseband spur frequency given input channel frequency 
 * and compute register settings below.
 */

static void
ar5416SpurMitigate(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
    uint16_t freq = ath_hal_gethwchannel(ah, chan);
    static const int pilot_mask_reg[4] = { AR_PHY_TIMING7, AR_PHY_TIMING8,
                AR_PHY_PILOT_MASK_01_30, AR_PHY_PILOT_MASK_31_60 };
    static const int chan_mask_reg[4] = { AR_PHY_TIMING9, AR_PHY_TIMING10,
                AR_PHY_CHANNEL_MASK_01_30, AR_PHY_CHANNEL_MASK_31_60 };
    static const int inc[4] = { 0, 100, 0, 0 };

    int bb_spur = AR_NO_SPUR;
    int bin, cur_bin;
    int spur_freq_sd;
    int spur_delta_phase;
    int denominator;
    int upper, lower, cur_vit_mask;
    int tmp, new;
    int i;

    int8_t mask_m[123];
    int8_t mask_p[123];
    int8_t mask_amt;
    int tmp_mask;
    int cur_bb_spur;
    HAL_BOOL is2GHz = IEEE80211_IS_CHAN_2GHZ(chan);

    OS_MEMZERO(mask_m, sizeof(mask_m));
    OS_MEMZERO(mask_p, sizeof(mask_p));

    /*
     * Need to verify range +/- 9.5 for static ht20, otherwise spur
     * is out-of-band and can be ignored.
     */
    /* XXX ath9k changes */
    for (i = 0; i < AR5416_EEPROM_MODAL_SPURS; i++) {
        cur_bb_spur = ath_hal_getSpurChan(ah, i, is2GHz);
        if (AR_NO_SPUR == cur_bb_spur)
            break;
        cur_bb_spur = cur_bb_spur - (freq * 10);
        if ((cur_bb_spur > -95) && (cur_bb_spur < 95)) {
            bb_spur = cur_bb_spur;
            break;
        }
    }
    if (AR_NO_SPUR == bb_spur)
        return;

    bin = bb_spur * 32;

    tmp = OS_REG_READ(ah, AR_PHY_TIMING_CTRL4_CHAIN(0));
    new = tmp | (AR_PHY_TIMING_CTRL4_ENABLE_SPUR_RSSI |
        AR_PHY_TIMING_CTRL4_ENABLE_SPUR_FILTER |
        AR_PHY_TIMING_CTRL4_ENABLE_CHAN_MASK |
        AR_PHY_TIMING_CTRL4_ENABLE_PILOT_MASK);

    OS_REG_WRITE_BUFFER_ENABLE(ah);

    OS_REG_WRITE(ah, AR_PHY_TIMING_CTRL4_CHAIN(0), new);

    new = (AR_PHY_SPUR_REG_MASK_RATE_CNTL |
        AR_PHY_SPUR_REG_ENABLE_MASK_PPM |
        AR_PHY_SPUR_REG_MASK_RATE_SELECT |
        AR_PHY_SPUR_REG_ENABLE_VIT_SPUR_RSSI |
        SM(AR5416_SPUR_RSSI_THRESH, AR_PHY_SPUR_REG_SPUR_RSSI_THRESH));
    OS_REG_WRITE(ah, AR_PHY_SPUR_REG, new);
    /*
     * Should offset bb_spur by +/- 10 MHz for dynamic 2040 MHz
     * config, no offset for HT20.
     * spur_delta_phase = bb_spur/40 * 2**21 for static ht20,
     * /80 for dyn2040.
     */
    spur_delta_phase = ((bb_spur * 524288) / 100) &
        AR_PHY_TIMING11_SPUR_DELTA_PHASE;
    /*
     * in 11A mode the denominator of spur_freq_sd should be 40 and
     * it should be 44 in 11G
     */
    denominator = IEEE80211_IS_CHAN_2GHZ(chan) ? 440 : 400;
    spur_freq_sd = ((bb_spur * 2048) / denominator) & 0x3ff;

    new = (AR_PHY_TIMING11_USE_SPUR_IN_AGC |
        SM(spur_freq_sd, AR_PHY_TIMING11_SPUR_FREQ_SD) |
        SM(spur_delta_phase, AR_PHY_TIMING11_SPUR_DELTA_PHASE));
    OS_REG_WRITE(ah, AR_PHY_TIMING11, new);


    /*
     * ============================================
     * pilot mask 1 [31:0] = +6..-26, no 0 bin
     * pilot mask 2 [19:0] = +26..+7
     *
     * channel mask 1 [31:0] = +6..-26, no 0 bin
     * channel mask 2 [19:0] = +26..+7
     */
    //cur_bin = -26;
    cur_bin = -6000;
    upper = bin + 100;
    lower = bin - 100;

    for (i = 0; i < 4; i++) {
        int pilot_mask = 0;
        int chan_mask  = 0;
        int bp         = 0;
        for (bp = 0; bp < 30; bp++) {
            if ((cur_bin > lower) && (cur_bin < upper)) {
                pilot_mask = pilot_mask | 0x1 << bp;
                chan_mask  = chan_mask | 0x1 << bp;
            }
            cur_bin += 100;
        }
        cur_bin += inc[i];
        OS_REG_WRITE(ah, pilot_mask_reg[i], pilot_mask);
        OS_REG_WRITE(ah, chan_mask_reg[i], chan_mask);
    }

    /* =================================================
     * viterbi mask 1 based on channel magnitude
     * four levels 0-3
     *  - mask (-27 to 27) (reg 64,0x9900 to 67,0x990c)
     *      [1 2 2 1] for -9.6 or [1 2 1] for +16
     *  - enable_mask_ppm, all bins move with freq
     *
     *  - mask_select,    8 bits for rates (reg 67,0x990c)
     *  - mask_rate_cntl, 8 bits for rates (reg 67,0x990c)
     *      choose which mask to use mask or mask2
     */

    /*
     * viterbi mask 2  2nd set for per data rate puncturing
     * four levels 0-3
     *  - mask_select, 8 bits for rates (reg 67)
     *  - mask (-27 to 27) (reg 98,0x9988 to 101,0x9994)
     *      [1 2 2 1] for -9.6 or [1 2 1] for +16
     */
    cur_vit_mask = 6100;
    upper        = bin + 120;
    lower        = bin - 120;

    for (i = 0; i < 123; i++) {
        if ((cur_vit_mask > lower) && (cur_vit_mask < upper)) {
            if ((abs(cur_vit_mask - bin)) < 75) {
                mask_amt = 1;
            } else {
                mask_amt = 0;
            }
            if (cur_vit_mask < 0) {
                mask_m[abs(cur_vit_mask / 100)] = mask_amt;
            } else {
                mask_p[cur_vit_mask / 100] = mask_amt;
            }
        }
        cur_vit_mask -= 100;
    }

    tmp_mask = (mask_m[46] << 30) | (mask_m[47] << 28)
          | (mask_m[48] << 26) | (mask_m[49] << 24)
          | (mask_m[50] << 22) | (mask_m[51] << 20)
          | (mask_m[52] << 18) | (mask_m[53] << 16)
          | (mask_m[54] << 14) | (mask_m[55] << 12)
          | (mask_m[56] << 10) | (mask_m[57] <<  8)
          | (mask_m[58] <<  6) | (mask_m[59] <<  4)
          | (mask_m[60] <<  2) | (mask_m[61] <<  0);
    OS_REG_WRITE(ah, AR_PHY_BIN_MASK_1, tmp_mask);
    OS_REG_WRITE(ah, AR_PHY_VIT_MASK2_M_46_61, tmp_mask);

    tmp_mask =             (mask_m[31] << 28)
          | (mask_m[32] << 26) | (mask_m[33] << 24)
          | (mask_m[34] << 22) | (mask_m[35] << 20)
          | (mask_m[36] << 18) | (mask_m[37] << 16)
          | (mask_m[48] << 14) | (mask_m[39] << 12)
          | (mask_m[40] << 10) | (mask_m[41] <<  8)
          | (mask_m[42] <<  6) | (mask_m[43] <<  4)
          | (mask_m[44] <<  2) | (mask_m[45] <<  0);
    OS_REG_WRITE(ah, AR_PHY_BIN_MASK_2, tmp_mask);
    OS_REG_WRITE(ah, AR_PHY_MASK2_M_31_45, tmp_mask);

    tmp_mask = (mask_m[16] << 30) | (mask_m[16] << 28)
          | (mask_m[18] << 26) | (mask_m[18] << 24)
          | (mask_m[20] << 22) | (mask_m[20] << 20)
          | (mask_m[22] << 18) | (mask_m[22] << 16)
          | (mask_m[24] << 14) | (mask_m[24] << 12)
          | (mask_m[25] << 10) | (mask_m[26] <<  8)
          | (mask_m[27] <<  6) | (mask_m[28] <<  4)
          | (mask_m[29] <<  2) | (mask_m[30] <<  0);
    OS_REG_WRITE(ah, AR_PHY_BIN_MASK_3, tmp_mask);
    OS_REG_WRITE(ah, AR_PHY_MASK2_M_16_30, tmp_mask);

    tmp_mask = (mask_m[ 0] << 30) | (mask_m[ 1] << 28)
          | (mask_m[ 2] << 26) | (mask_m[ 3] << 24)
          | (mask_m[ 4] << 22) | (mask_m[ 5] << 20)
          | (mask_m[ 6] << 18) | (mask_m[ 7] << 16)
          | (mask_m[ 8] << 14) | (mask_m[ 9] << 12)
          | (mask_m[10] << 10) | (mask_m[11] <<  8)
          | (mask_m[12] <<  6) | (mask_m[13] <<  4)
          | (mask_m[14] <<  2) | (mask_m[15] <<  0);
    OS_REG_WRITE(ah, AR_PHY_MASK_CTL, tmp_mask);
    OS_REG_WRITE(ah, AR_PHY_MASK2_M_00_15, tmp_mask);

    tmp_mask =             (mask_p[15] << 28)
          | (mask_p[14] << 26) | (mask_p[13] << 24)
          | (mask_p[12] << 22) | (mask_p[11] << 20)
          | (mask_p[10] << 18) | (mask_p[ 9] << 16)
          | (mask_p[ 8] << 14) | (mask_p[ 7] << 12)
          | (mask_p[ 6] << 10) | (mask_p[ 5] <<  8)
          | (mask_p[ 4] <<  6) | (mask_p[ 3] <<  4)
          | (mask_p[ 2] <<  2) | (mask_p[ 1] <<  0);
    OS_REG_WRITE(ah, AR_PHY_BIN_MASK2_1, tmp_mask);
    OS_REG_WRITE(ah, AR_PHY_MASK2_P_15_01, tmp_mask);

    tmp_mask =             (mask_p[30] << 28)
          | (mask_p[29] << 26) | (mask_p[28] << 24)
          | (mask_p[27] << 22) | (mask_p[26] << 20)
          | (mask_p[25] << 18) | (mask_p[24] << 16)
          | (mask_p[23] << 14) | (mask_p[22] << 12)
          | (mask_p[21] << 10) | (mask_p[20] <<  8)
          | (mask_p[19] <<  6) | (mask_p[18] <<  4)
          | (mask_p[17] <<  2) | (mask_p[16] <<  0);
    OS_REG_WRITE(ah, AR_PHY_BIN_MASK2_2, tmp_mask);
    OS_REG_WRITE(ah, AR_PHY_MASK2_P_30_16, tmp_mask);

    tmp_mask =             (mask_p[45] << 28)
          | (mask_p[44] << 26) | (mask_p[43] << 24)
          | (mask_p[42] << 22) | (mask_p[41] << 20)
          | (mask_p[40] << 18) | (mask_p[39] << 16)
          | (mask_p[38] << 14) | (mask_p[37] << 12)
          | (mask_p[36] << 10) | (mask_p[35] <<  8)
          | (mask_p[34] <<  6) | (mask_p[33] <<  4)
          | (mask_p[32] <<  2) | (mask_p[31] <<  0);
    OS_REG_WRITE(ah, AR_PHY_BIN_MASK2_3, tmp_mask);
    OS_REG_WRITE(ah, AR_PHY_MASK2_P_45_31, tmp_mask);

    tmp_mask = (mask_p[61] << 30) | (mask_p[60] << 28)
          | (mask_p[59] << 26) | (mask_p[58] << 24)
          | (mask_p[57] << 22) | (mask_p[56] << 20)
          | (mask_p[55] << 18) | (mask_p[54] << 16)
          | (mask_p[53] << 14) | (mask_p[52] << 12)
          | (mask_p[51] << 10) | (mask_p[50] <<  8)
          | (mask_p[49] <<  6) | (mask_p[48] <<  4)
          | (mask_p[47] <<  2) | (mask_p[46] <<  0);
    OS_REG_WRITE(ah, AR_PHY_BIN_MASK2_4, tmp_mask);
    OS_REG_WRITE(ah, AR_PHY_MASK2_P_61_45, tmp_mask);

    OS_REG_WRITE_BUFFER_FLUSH(ah);
    OS_REG_WRITE_BUFFER_DISABLE(ah);
}

/*
 * Fill all software cached or static hardware state information.
 * Return failure if capabilities are to come from EEPROM and
 * cannot be read.
 */
HAL_BOOL
ar5416FillCapabilityInfo(struct ath_hal *ah)
{
	struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
	HAL_CAPABILITIES *pCap = &ahpriv->ah_caps;
	uint16_t val;
	
	/* Construct wireless mode from EEPROM */
	pCap->halWirelessModes = 0;
	if (ath_hal_eepromGetFlag(ah, AR_EEP_AMODE)) {
		pCap->halWirelessModes |= HAL_MODE_11A
				       |  HAL_MODE_11NA_HT20
				       |  HAL_MODE_11NA_HT40PLUS
				       |  HAL_MODE_11NA_HT40MINUS
				       ;
	}
	if (ath_hal_eepromGetFlag(ah, AR_EEP_GMODE)) {
		pCap->halWirelessModes |= HAL_MODE_11G
				       |  HAL_MODE_11NG_HT20
				       |  HAL_MODE_11NG_HT40PLUS
				       |  HAL_MODE_11NG_HT40MINUS
				       ;
		pCap->halWirelessModes |= HAL_MODE_11A
				       |  HAL_MODE_11NA_HT20
				       |  HAL_MODE_11NA_HT40PLUS
				       |  HAL_MODE_11NA_HT40MINUS
				       ;
	}

	pCap->halLow2GhzChan = 2312;
	pCap->halHigh2GhzChan = 2732;

	pCap->halLow5GhzChan = 4915;
	pCap->halHigh5GhzChan = 6100;

	pCap->halCipherCkipSupport = AH_FALSE;
	pCap->halCipherTkipSupport = AH_TRUE;
	pCap->halCipherAesCcmSupport = ath_hal_eepromGetFlag(ah, AR_EEP_AES);

	pCap->halMicCkipSupport    = AH_FALSE;
	pCap->halMicTkipSupport    = AH_TRUE;
	pCap->halMicAesCcmSupport  = ath_hal_eepromGetFlag(ah, AR_EEP_AES);
	/*
	 * Starting with Griffin TX+RX mic keys can be combined
	 * in one key cache slot.
	 */
	pCap->halTkipMicTxRxKeySupport = AH_TRUE;
	pCap->halChanSpreadSupport = AH_TRUE;
	pCap->halSleepAfterBeaconBroken = AH_TRUE;

	pCap->halCompressSupport = AH_FALSE;
	pCap->halBurstSupport = AH_TRUE;
	pCap->halFastFramesSupport = AH_TRUE;
	pCap->halChapTuningSupport = AH_TRUE;
	pCap->halTurboPrimeSupport = AH_TRUE;

	pCap->halTurboGSupport = pCap->halWirelessModes & HAL_MODE_108G;

	pCap->halPSPollBroken = AH_TRUE;	/* XXX fixed in later revs? */
	pCap->halNumMRRetries = 4;		/* Hardware supports 4 MRR */
	pCap->halNumTxMaps = 1;			/* Single TX ptr per descr */
	pCap->halVEOLSupport = AH_TRUE;
	pCap->halBssIdMaskSupport = AH_TRUE;
	pCap->halMcastKeySrchSupport = AH_TRUE;	/* Works on AR5416 and later */
	pCap->halTsfAddSupport = AH_TRUE;
	pCap->hal4AddrAggrSupport = AH_FALSE;	/* Broken in Owl */
	pCap->halSpectralScanSupport = AH_FALSE;	/* AR9280 and later */

	if (ath_hal_eepromGet(ah, AR_EEP_MAXQCU, &val) == HAL_OK)
		pCap->halTotalQueues = val;
	else
		pCap->halTotalQueues = HAL_NUM_TX_QUEUES;

	if (ath_hal_eepromGet(ah, AR_EEP_KCENTRIES, &val) == HAL_OK)
		pCap->halKeyCacheSize = val;
	else
		pCap->halKeyCacheSize = AR5416_KEYTABLE_SIZE;

	/* XXX Which chips? */
	pCap->halChanHalfRate = AH_TRUE;
	pCap->halChanQuarterRate = AH_TRUE;

	pCap->halTxTstampPrecision = 32;
	pCap->halRxTstampPrecision = 32;
	pCap->halHwPhyCounterSupport = AH_TRUE;
	pCap->halIntrMask = HAL_INT_COMMON
			| HAL_INT_RX
			| HAL_INT_TX
			| HAL_INT_FATAL
			| HAL_INT_BNR
			| HAL_INT_BMISC
			| HAL_INT_DTIMSYNC
			| HAL_INT_TSFOOR
			| HAL_INT_CST
			| HAL_INT_GTT
			;

	pCap->halFastCCSupport = AH_TRUE;
	pCap->halNumGpioPins = 14;
	pCap->halWowSupport = AH_FALSE;
	pCap->halWowMatchPatternExact = AH_FALSE;
	pCap->halBtCoexSupport = AH_FALSE;	/* XXX need support */
	pCap->halAutoSleepSupport = AH_FALSE;
	pCap->hal4kbSplitTransSupport = AH_TRUE;
	/* Disable this so Block-ACK works correctly */
	pCap->halHasRxSelfLinkedTail = AH_FALSE;
#if 0	/* XXX not yet */
	pCap->halNumAntCfg2GHz = ar5416GetNumAntConfig(ahp, HAL_FREQ_BAND_2GHZ);
	pCap->halNumAntCfg5GHz = ar5416GetNumAntConfig(ahp, HAL_FREQ_BAND_5GHZ);
#endif
	pCap->halHTSupport = AH_TRUE;
	pCap->halTxChainMask = ath_hal_eepromGet(ah, AR_EEP_TXMASK, AH_NULL);
	/* XXX CB71 uses GPIO 0 to indicate 3 rx chains */
	pCap->halRxChainMask = ath_hal_eepromGet(ah, AR_EEP_RXMASK, AH_NULL);
	/* AR5416 may have 3 antennas but is a 2x2 stream device */
	pCap->halTxStreams = 2;
	pCap->halRxStreams = 2;

	/*
	 * If the TX or RX chainmask has less than 2 chains active,
	 * mark it as a 1-stream device for the relevant stream.
	 */
	if (owl_get_ntxchains(pCap->halTxChainMask) == 1)
		pCap->halTxStreams = 1;
	/* XXX Eww */
	if (owl_get_ntxchains(pCap->halRxChainMask) == 1)
		pCap->halRxStreams = 1;
	pCap->halRtsAggrLimit = 8*1024;		/* Owl 2.0 limit */
	pCap->halMbssidAggrSupport = AH_FALSE;	/* Broken on Owl */
	pCap->halForcePpmSupport = AH_TRUE;
	pCap->halEnhancedPmSupport = AH_TRUE;
	pCap->halBssidMatchSupport = AH_TRUE;
	pCap->halGTTSupport = AH_TRUE;
	pCap->halCSTSupport = AH_TRUE;
	pCap->halEnhancedDfsSupport = AH_FALSE;
	/*
	 * BB Read WAR: this is only for AR5008/AR9001 NICs
	 * It is also set individually in the AR91xx attach functions.
	 */
	if (AR_SREV_OWL(ah))
		pCap->halHasBBReadWar = AH_TRUE;

	if (ath_hal_eepromGetFlag(ah, AR_EEP_RFKILL) &&
	    ath_hal_eepromGet(ah, AR_EEP_RFSILENT, &ahpriv->ah_rfsilent) == HAL_OK) {
		/* NB: enabled by default */
		ahpriv->ah_rfkillEnabled = AH_TRUE;
		pCap->halRfSilentSupport = AH_TRUE;
	}

	/*
	 * The MAC will mark frames as RXed if there's a descriptor
	 * to write them to. So if it hits a self-linked final descriptor,
	 * it'll keep ACKing frames even though they're being silently
	 * dropped. Thus, this particular feature of the driver can't
	 * be used for 802.11n devices.
	 */
	ahpriv->ah_rxornIsFatal = AH_FALSE;

	/*
	 * If it's a PCI NIC, ask the HAL OS layer to serialise
	 * register access, or SMP machines may cause the hardware
	 * to hang. This is applicable to AR5416 and AR9220; I'm not
	 * sure about AR9160 or AR9227.
	 */
	if (! AH_PRIVATE(ah)->ah_ispcie)
		pCap->halSerialiseRegWar = 1;

	/*
	 * AR5416 and later NICs support MYBEACON filtering.
	 */
	pCap->halRxDoMyBeacon = AH_TRUE;

	return AH_TRUE;
}

static const char*
ar5416Probe(uint16_t vendorid, uint16_t devid)
{
	if (vendorid == ATHEROS_VENDOR_ID) {
		if (devid == AR5416_DEVID_PCI)
			return "Atheros 5416";
		if (devid == AR5416_DEVID_PCIE)
			return "Atheros 5418";
	}
	return AH_NULL;
}
AH_CHIP(AR5416, ar5416Probe, ar5416Attach);

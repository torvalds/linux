/*
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
 * $FreeBSD: head/sys/dev/ath/ath_hal/ar5212/ar5212_attach.c 235972 2012-05-25 05:01:27Z adrian $
 */
#include "opt_ah.h"

#include "ah.h"
#include "ah_internal.h"
#include "ah_devid.h"

#include "ar9300/ar9300.h"
#include "ar9300/ar9300reg.h"
#include "ar9300/ar9300phy.h"

#include "ar9300/ar9300_stub.h"
#include "ar9300/ar9300_stub_funcs.h"

void
ar9300_set_stub_functions(struct ath_hal *ah)
{

//	ath_hal_printf(ah, "%s: setting stub functions\n", __func__);

	ah->ah_getRateTable		= ar9300_Stub_GetRateTable;
//	ah->ah_detach			= ar9300_Stub_detach;

	/* Reset Functions */
	ah->ah_reset			= ar9300_Stub_Reset;
	ah->ah_phyDisable		= ar9300_Stub_PhyDisable;
	ah->ah_disable			= ar9300_Stub_Disable;
	ah->ah_configPCIE		= ar9300_Stub_ConfigPCIE;
	ah->ah_disablePCIE		= ar9300_Stub_DisablePCIE;
	ah->ah_setPCUConfig		= ar9300_Stub_SetPCUConfig;
	ah->ah_perCalibration		= ar9300_Stub_PerCalibration;
	ah->ah_perCalibrationN		= ar9300_Stub_PerCalibrationN;
	ah->ah_resetCalValid		= ar9300_Stub_ResetCalValid;
	ah->ah_setTxPowerLimit		= ar9300_Stub_SetTxPowerLimit;
	ah->ah_getChanNoise		= ath_hal_getChanNoise;

	/* Transmit functions */
	ah->ah_updateTxTrigLevel	= ar9300_Stub_UpdateTxTrigLevel;
	ah->ah_setupTxQueue		= ar9300_Stub_SetupTxQueue;
	ah->ah_setTxQueueProps		= ar9300_Stub_SetTxQueueProps;
	ah->ah_getTxQueueProps		= ar9300_Stub_GetTxQueueProps;
	ah->ah_releaseTxQueue		= ar9300_Stub_ReleaseTxQueue;
	ah->ah_resetTxQueue		= ar9300_Stub_ResetTxQueue;
	ah->ah_getTxDP			= ar9300_Stub_GetTxDP;
	ah->ah_setTxDP			= ar9300_Stub_SetTxDP;
	ah->ah_numTxPending		= ar9300_Stub_NumTxPending;
	ah->ah_startTxDma		= ar9300_Stub_StartTxDma;
	ah->ah_stopTxDma		= ar9300_Stub_StopTxDma;
	ah->ah_setupTxDesc		= ar9300_Stub_SetupTxDesc;
	ah->ah_setupXTxDesc		= ar9300_Stub_SetupXTxDesc;
	ah->ah_fillTxDesc		= ar9300_Stub_FillTxDesc;
	ah->ah_procTxDesc		= ar9300_Stub_ProcTxDesc;
	ah->ah_getTxIntrQueue		= ar9300_Stub_GetTxIntrQueue;
	ah->ah_reqTxIntrDesc 		= ar9300_Stub_IntrReqTxDesc;
	ah->ah_getTxCompletionRates	= ar9300_Stub_GetTxCompletionRates;

	/* RX Functions */
	ah->ah_getRxDP			= ar9300_Stub_GetRxDP;
	ah->ah_setRxDP			= ar9300_Stub_SetRxDP;
	ah->ah_enableReceive		= ar9300_Stub_EnableReceive;
	ah->ah_stopDmaReceive		= ar9300_Stub_StopDmaReceive;
	ah->ah_startPcuReceive		= ar9300_Stub_StartPcuReceive;
	ah->ah_stopPcuReceive		= ar9300_Stub_StopPcuReceive;
	ah->ah_setMulticastFilter	= ar9300_Stub_SetMulticastFilter;
	ah->ah_setMulticastFilterIndex	= ar9300_Stub_SetMulticastFilterIndex;
	ah->ah_clrMulticastFilterIndex	= ar9300_Stub_ClrMulticastFilterIndex;
	ah->ah_getRxFilter		= ar9300_Stub_GetRxFilter;
	ah->ah_setRxFilter		= ar9300_Stub_SetRxFilter;
	ah->ah_setupRxDesc		= ar9300_Stub_SetupRxDesc;
	ah->ah_procRxDesc		= ar9300_Stub_ProcRxDesc;
	ah->ah_rxMonitor		= ar9300_Stub_RxMonitor;
	ah->ah_aniPoll			= ar9300_Stub_AniPoll;
	ah->ah_procMibEvent		= ar9300_Stub_ProcessMibIntr;

	/* Misc Functions */
	ah->ah_getCapability		= ar9300_Stub_GetCapability;
	ah->ah_setCapability		= ar9300_Stub_SetCapability;
	ah->ah_getDiagState		= ar9300_Stub_GetDiagState;
	ah->ah_getMacAddress		= ar9300_Stub_GetMacAddress;
	ah->ah_setMacAddress		= ar9300_Stub_SetMacAddress;
	ah->ah_getBssIdMask		= ar9300_Stub_GetBssIdMask;
	ah->ah_setBssIdMask		= ar9300_Stub_SetBssIdMask;
	ah->ah_setRegulatoryDomain	= ar9300_Stub_SetRegulatoryDomain;
	ah->ah_setLedState		= ar9300_Stub_SetLedState;
	ah->ah_writeAssocid		= ar9300_Stub_WriteAssocid;
	ah->ah_gpioCfgInput		= ar9300_Stub_GpioCfgInput;
	ah->ah_gpioCfgOutput		= ar9300_Stub_GpioCfgOutput;
	ah->ah_gpioGet			= ar9300_Stub_GpioGet;
	ah->ah_gpioSet			= ar9300_Stub_GpioSet;
	ah->ah_gpioSetIntr		= ar9300_Stub_GpioSetIntr;
	ah->ah_getTsf32			= ar9300_Stub_GetTsf32;
	ah->ah_getTsf64			= ar9300_Stub_GetTsf64;
	ah->ah_resetTsf			= ar9300_Stub_ResetTsf;
	ah->ah_detectCardPresent	= ar9300_Stub_DetectCardPresent;
	ah->ah_updateMibCounters	= ar9300_Stub_UpdateMibCounters;
	ah->ah_getRfGain		= ar9300_Stub_GetRfgain;
	ah->ah_getDefAntenna		= ar9300_Stub_GetDefAntenna;
	ah->ah_setDefAntenna		= ar9300_Stub_SetDefAntenna;
	ah->ah_getAntennaSwitch		= ar9300_Stub_GetAntennaSwitch;
	ah->ah_setAntennaSwitch		= ar9300_Stub_SetAntennaSwitch;
	ah->ah_setSifsTime		= ar9300_Stub_SetSifsTime;
	ah->ah_getSifsTime		= ar9300_Stub_GetSifsTime;
	ah->ah_setSlotTime		= ar9300_Stub_SetSlotTime;
	ah->ah_getSlotTime		= ar9300_Stub_GetSlotTime;
	ah->ah_setAckTimeout		= ar9300_Stub_SetAckTimeout;
	ah->ah_getAckTimeout		= ar9300_Stub_GetAckTimeout;
	ah->ah_setAckCTSRate		= ar9300_Stub_SetAckCTSRate;
	ah->ah_getAckCTSRate		= ar9300_Stub_GetAckCTSRate;
	ah->ah_setCTSTimeout		= ar9300_Stub_SetCTSTimeout;
	ah->ah_getCTSTimeout		= ar9300_Stub_GetCTSTimeout;
	ah->ah_setDecompMask		= ar9300_Stub_SetDecompMask;
	ah->ah_setCoverageClass		= ar9300_Stub_SetCoverageClass;
	ah->ah_setQuiet			= ar9300_Stub_SetQuiet;
	ah->ah_getMibCycleCounts	= ar9300_Stub_GetMibCycleCounts;

	/* DFS Functions */
	ah->ah_enableDfs		= ar9300_Stub_EnableDfs;
	ah->ah_getDfsThresh		= ar9300_Stub_GetDfsThresh;
	ah->ah_procRadarEvent		= ar9300_Stub_ProcessRadarEvent;
	ah->ah_isFastClockEnabled	= ar9300_Stub_IsFastClockEnabled;
	ah->ah_get11nExtBusy		= ar9300_Stub_Get11nExtBusy;

	/* Key Cache Functions */
	ah->ah_getKeyCacheSize		= ar9300_Stub_GetKeyCacheSize;
	ah->ah_resetKeyCacheEntry	= ar9300_Stub_ResetKeyCacheEntry;
	ah->ah_isKeyCacheEntryValid	= ar9300_Stub_IsKeyCacheEntryValid;
	ah->ah_setKeyCacheEntry		= ar9300_Stub_SetKeyCacheEntry;
	ah->ah_setKeyCacheEntryMac	= ar9300_Stub_SetKeyCacheEntryMac;

	/* Power Management Functions */
	ah->ah_setPowerMode		= ar9300_Stub_SetPowerMode;
	ah->ah_getPowerMode		= ar9300_Stub_GetPowerMode;

	/* Beacon Functions */
	ah->ah_setBeaconTimers		= ar9300_Stub_SetBeaconTimers;
	ah->ah_beaconInit		= ar9300_Stub_BeaconInit;
	ah->ah_setStationBeaconTimers	= ar9300_Stub_SetStaBeaconTimers;
	ah->ah_resetStationBeaconTimers	= ar9300_Stub_ResetStaBeaconTimers;
	ah->ah_getNextTBTT		= ar9300_Stub_GetNextTBTT;

	/* Interrupt Functions */
	ah->ah_isInterruptPending	= ar9300_Stub_IsInterruptPending;
	ah->ah_getPendingInterrupts	= ar9300_Stub_GetPendingInterrupts;
	ah->ah_getInterrupts		= ar9300_Stub_GetInterrupts;
	ah->ah_setInterrupts		= ar9300_Stub_SetInterrupts;

	AH_PRIVATE(ah)->ah_getChannelEdges		= ar9300_Stub_GetChannelEdges;
	AH_PRIVATE(ah)->ah_getWirelessModes		= ar9300_Stub_GetWirelessModes;
	AH_PRIVATE(ah)->ah_eepromRead		= ar9300_Stub_EepromRead;
#ifdef AH_SUPPORT_WRITE_EEPROM
	AH_PRIVATE(ah)->ah_eepromWrite		= ar9300_Stub_EepromWrite;
#endif
	AH_PRIVATE(ah)->ah_getChipPowerLimits	= ar9300_Stub_GetChipPowerLimits;
}

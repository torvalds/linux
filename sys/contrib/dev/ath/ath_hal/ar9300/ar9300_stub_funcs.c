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

uint32_t
ar9300_Stub_GetRadioRev(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

#if 0
void
ar9300_Stub_InitState(struct ath_hal_5212 *, uint16_t devid, HAL_SOFTC,
    HAL_BUS_TAG st, HAL_BUS_HANDLE sh, HAL_STATUS *status)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return;

}
#endif

void
ar9300_Stub_Detach(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return;
}

HAL_BOOL
ar9300_Stub_ChipTest(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_GetChannelEdges(struct ath_hal *ah, uint16_t flags,
    uint16_t *low, uint16_t *high)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_FillCapabilityInfo(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

void
ar9300_Stub_SetBeaconTimers(struct ath_hal *ah,
    const HAL_BEACON_TIMERS * bs)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_BeaconInit(struct ath_hal *ah, uint32_t next_beacon,
    uint32_t beacon_period)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_ResetStaBeaconTimers(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_SetStaBeaconTimers(struct ath_hal *ah, const HAL_BEACON_STATE *bs)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

uint64_t
ar9300_Stub_GetNextTBTT(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

HAL_BOOL
ar9300_Stub_IsInterruptPending(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_GetPendingInterrupts(struct ath_hal *ah, HAL_INT *mask)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_INT
ar9300_Stub_GetInterrupts(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

HAL_INT
ar9300_Stub_SetInterrupts(struct ath_hal *ah, HAL_INT ints)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}


uint32_t
ar9300_Stub_GetKeyCacheSize(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

HAL_BOOL
ar9300_Stub_IsKeyCacheEntryValid(struct ath_hal *ah, uint16_t entry)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_ResetKeyCacheEntry(struct ath_hal *ah, uint16_t entry)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_SetKeyCacheEntryMac(struct ath_hal *ah, uint16_t entry,
    const uint8_t *mac)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_SetKeyCacheEntry(struct ath_hal *ah, uint16_t entry,
    const HAL_KEYVAL *k, const uint8_t *mac, int xorKey)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

void
ar9300_Stub_GetMacAddress(struct ath_hal *ah, uint8_t *mac)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

HAL_BOOL
ar9300_Stub_SetMacAddress(struct ath_hal *ah, const uint8_t *mac)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

void
ar9300_Stub_GetBssIdMask(struct ath_hal *ah, uint8_t *mac)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

HAL_BOOL
ar9300_Stub_SetBssIdMask(struct ath_hal *ah, const uint8_t *bssid)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_EepromRead(struct ath_hal *ah, u_int off, uint16_t *data)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_EepromWrite(struct ath_hal *ah, u_int off, uint16_t data)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_SetRegulatoryDomain(struct ath_hal *ah,
		uint16_t regDomain, HAL_STATUS *stats)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

u_int
ar9300_Stub_GetWirelessModes(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	/* XXX map these */
	return (0);
}

void
ar9300_Stub_EnableRfKill(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

HAL_BOOL
ar9300_Stub_GpioCfgOutput(struct ath_hal *ah, uint32_t gpio,
		HAL_GPIO_MUX_TYPE mux)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_GpioCfgInput(struct ath_hal *ah, uint32_t gpio)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_GpioSet(struct ath_hal *ah, uint32_t gpio, uint32_t val)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

uint32_t
ar9300_Stub_GpioGet(struct ath_hal *ah, uint32_t gpio)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

void
ar9300_Stub_GpioSetIntr(struct ath_hal *ah, u_int gpioPin, uint32_t ilevel)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_SetLedState(struct ath_hal *ah, HAL_LED_STATE state)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_WriteAssocid(struct ath_hal *ah, const uint8_t *bssid,
		uint16_t assocId)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

uint32_t
ar9300_Stub_GetTsf32(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

uint64_t
ar9300_Stub_GetTsf64(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

void
ar9300_Stub_SetTsf64(struct ath_hal *ah, uint64_t tsf64)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_ResetTsf(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_SetBasicRate(struct ath_hal *ah, HAL_RATE_SET *pSet)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

uint32_t
ar9300_Stub_GetRandomSeed(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

HAL_BOOL
ar9300_Stub_DetectCardPresent(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_TRUE);
}

void
ar9300_Stub_EnableMibCounters(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_DisableMibCounters(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_UpdateMibCounters(struct ath_hal *ah, HAL_MIB_STATS* stats)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

HAL_BOOL
ar9300_Stub_IsJapanChannelSpreadSupported(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

uint32_t
ar9300_Stub_GetCurRssi(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

u_int
ar9300_Stub_GetDefAntenna(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

void
ar9300_Stub_SetDefAntenna(struct ath_hal *ah, u_int antenna)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

HAL_ANT_SETTING
ar9300_Stub_GetAntennaSwitch(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (HAL_ANT_VARIABLE);
}

HAL_BOOL
ar9300_Stub_SetAntennaSwitch(struct ath_hal *ah, HAL_ANT_SETTING setting)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_IsSleepAfterBeaconBroken(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_SetSifsTime(struct ath_hal *ah, u_int sifs)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

u_int
ar9300_Stub_GetSifsTime(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

HAL_BOOL
ar9300_Stub_SetSlotTime(struct ath_hal *ah, u_int slottime)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

u_int
ar9300_Stub_GetSlotTime(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

HAL_BOOL
ar9300_Stub_SetAckTimeout(struct ath_hal *ah, u_int acktimeout)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

u_int
ar9300_Stub_GetAckTimeout(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

HAL_BOOL
ar9300_Stub_SetAckCTSRate(struct ath_hal *ah, u_int ctsrate)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

u_int
ar9300_Stub_GetAckCTSRate(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

HAL_BOOL
ar9300_Stub_SetCTSTimeout(struct ath_hal *ah, u_int ctstimeout)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

u_int
ar9300_Stub_GetCTSTimeout(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

HAL_BOOL
ar9300_Stub_SetDecompMask(struct ath_hal *ah, uint16_t a, int b)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

void
ar9300_Stub_SetCoverageClass(struct ath_hal *ah, uint8_t a, int b)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_SetPCUConfig(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

HAL_BOOL
ar9300_Stub_Use32KHzclock(struct ath_hal *ah, HAL_OPMODE opmode)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

void
ar9300_Stub_SetupClock(struct ath_hal *ah, HAL_OPMODE opmode)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_RestoreClock(struct ath_hal *ah, HAL_OPMODE opmode)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

int16_t
ar9300_Stub_GetNfAdjust(struct ath_hal *ah, const HAL_CHANNEL_INTERNAL *ichan)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

void
ar9300_Stub_SetCompRegs(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

HAL_STATUS
ar9300_Stub_GetCapability(struct ath_hal *ah, HAL_CAPABILITY_TYPE ctype,
		uint32_t which, uint32_t *val)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (HAL_EIO);
}

HAL_BOOL
ar9300_Stub_SetCapability(struct ath_hal *ah , HAL_CAPABILITY_TYPE ctype,
		uint32_t which, uint32_t val, HAL_STATUS *status)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_GetDiagState(struct ath_hal *ah, int request,
		const void *args, uint32_t argsize,
		void **result, uint32_t *resultsize)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_STATUS
ar9300_Stub_SetQuiet(struct ath_hal *ah, uint32_t period,
		uint32_t duration, uint32_t nextStart, HAL_QUIET_FLAG flag)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (HAL_EIO);
}

HAL_BOOL
ar9300_Stub_GetMibCycleCounts(struct ath_hal *ah,
		HAL_SURVEY_SAMPLE *hs)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_SetPowerMode(struct ath_hal *ah, HAL_POWER_MODE mode,
		int setChip)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_POWER_MODE
ar9300_Stub_GetPowerMode(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (HAL_PM_AWAKE);
}

HAL_BOOL
ar9300_Stub_GetPowerStatus(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

uint32_t
ar9300_Stub_GetRxDP(struct ath_hal *ah, HAL_RX_QUEUE qtype)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

void
ar9300_Stub_SetRxDP(struct ath_hal *ah, uint32_t rxdp, HAL_RX_QUEUE qtype)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_EnableReceive(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

HAL_BOOL
ar9300_Stub_StopDmaReceive(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

void
ar9300_Stub_StartPcuReceive(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_StopPcuReceive(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_SetMulticastFilter(struct ath_hal *ah, uint32_t filter0,
    uint32_t filter1)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

HAL_BOOL
ar9300_Stub_ClrMulticastFilterIndex(struct ath_hal *ah, uint32_t ix)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_SetMulticastFilterIndex(struct ath_hal *ah, uint32_t ix)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

uint32_t
ar9300_Stub_GetRxFilter(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

void
ar9300_Stub_SetRxFilter(struct ath_hal *ah, uint32_t bits)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

HAL_BOOL
ar9300_Stub_SetupRxDesc(struct ath_hal *ah,
		struct ath_desc *rxdesc, uint32_t size, u_int flags)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_STATUS
ar9300_Stub_ProcRxDesc(struct ath_hal *ah, struct ath_desc *desc0,
		uint32_t a, struct ath_desc *desc, uint64_t tsf,
		struct ath_rx_status *rxstat)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (HAL_EIO);
}

HAL_BOOL
ar9300_Stub_Reset(struct ath_hal *ah, HAL_OPMODE opmode,
		struct ieee80211_channel *chan, HAL_BOOL bChannelChange,
		HAL_RESET_TYPE resetType,
		HAL_STATUS *status)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_SetChannel(struct ath_hal *ah,
		const struct ieee80211_channel *chan)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

void
ar9300_Stub_SetOperatingMode(struct ath_hal *ah, int opmode)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

HAL_BOOL
ar9300_Stub_PhyDisable(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_Disable(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_ChipReset(struct ath_hal *ah,
		const struct ieee80211_channel *chan)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_PerCalibration(struct ath_hal *ah,
		struct ieee80211_channel *chan, HAL_BOOL *isIQdone)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_PerCalibrationN(struct ath_hal *ah,
		struct ieee80211_channel *chan, u_int chainMask,
		HAL_BOOL longCal, HAL_BOOL *isCalDone)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_ResetCalValid(struct ath_hal *ah,
		const struct ieee80211_channel *chan)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

int16_t
ar9300_Stub_GetNoiseFloor(struct ath_hal *ah)
{

	/* XXX */
	ath_hal_printf(ah, "%s: called\n", __func__);
	return (-91);
}

void
ar9300_Stub_InitNfCalHistBuffer(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

int16_t
ar9300_Stub_GetNfHistMid(const int16_t calData[])
{

	printf("%s: called\n", __func__);
	return (0);
}

void
ar9300_Stub_SetSpurMitigation(struct ath_hal *ah,
    const struct ieee80211_channel *chan)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

HAL_BOOL
ar9300_Stub_SetAntennaSwitchInternal(struct ath_hal *ah,
    HAL_ANT_SETTING settings, const struct ieee80211_channel *chan)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_SetTxPowerLimit(struct ath_hal *ah, uint32_t limit)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_GetChipPowerLimits(struct ath_hal *ah,
		struct ieee80211_channel *chan)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

void
ar9300_Stub_InitializeGainValues(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

HAL_RFGAIN
ar9300_Stub_GetRfgain(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

void
ar9300_Stub_RequestRfgain(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

HAL_BOOL
ar9300_Stub_UpdateTxTrigLevel(struct ath_hal *ah,
		HAL_BOOL IncTrigLevel)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_SetTxQueueProps(struct ath_hal *ah, int q,
		const HAL_TXQ_INFO *qInfo)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_GetTxQueueProps(struct ath_hal *ah, int q,
		HAL_TXQ_INFO *qInfo)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

int
ar9300_Stub_SetupTxQueue(struct ath_hal *ah, HAL_TX_QUEUE type,
		const HAL_TXQ_INFO *qInfo)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

HAL_BOOL
ar9300_Stub_ReleaseTxQueue(struct ath_hal *ah, u_int q)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_ResetTxQueue(struct ath_hal *ah, u_int q)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

uint32_t
ar9300_Stub_GetTxDP(struct ath_hal *ah, u_int q)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

HAL_BOOL
ar9300_Stub_SetTxDP(struct ath_hal *ah, u_int q, uint32_t txdp)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_StartTxDma(struct ath_hal *ah, u_int q)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

uint32_t
ar9300_Stub_NumTxPending(struct ath_hal *ah, u_int q)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

HAL_BOOL
ar9300_Stub_StopTxDma(struct ath_hal *ah, u_int q)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_SetupTxDesc(struct ath_hal *ah, struct ath_desc *ds,
		u_int pktLen, u_int hdrLen, HAL_PKT_TYPE type, u_int txPower,
		u_int txRate0, u_int txTries0,
		u_int keyIx, u_int antMode, u_int flags,
		u_int rtsctsRate, u_int rtsctsDuration,
		u_int compicvLen, u_int compivLen, u_int comp)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_SetupXTxDesc(struct ath_hal *ah, struct ath_desc *desc,
		u_int txRate1, u_int txRetries1,
		u_int txRate2, u_int txRetries2,
		u_int txRate3, u_int txRetries3)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_FillTxDesc(struct ath_hal *ah, struct ath_desc *ds,
		HAL_DMA_ADDR *bufAddrList, uint32_t *segLenList,
		u_int descId, u_int qcuId,
		HAL_BOOL firstSeg, HAL_BOOL lastSeg,
		const struct ath_desc *ds0)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_STATUS
ar9300_Stub_ProcTxDesc(struct ath_hal *ah,
		struct ath_desc *ds, struct ath_tx_status *txstat)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (HAL_EINPROGRESS);
}

void
ar9300_Stub_GetTxIntrQueue(struct ath_hal *ah, uint32_t *val)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_IntrReqTxDesc(struct ath_hal *ah, struct ath_desc *desc)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

HAL_BOOL
ar9300_Stub_GetTxCompletionRates(struct ath_hal *ah,
		const struct ath_desc *ds0, int *rates, int *tries)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

const HAL_RATE_TABLE *
ar9300_Stub_GetRateTable(struct ath_hal *ah, u_int mode)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	/* XXX null may panic the kernel? */
	return (AH_NULL);
}

#if 0
void
ar9300_Stub_AniAttach(struct ath_hal *ah, const struct ar5212AniParams *,
    const struct ar5212AniParams *, HAL_BOOL ena)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_AniDetach(struct ath_hal *)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}
#endif

#if 0
struct ar5212AniState *
ar9300_Stub_AniGetCurrentState(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_NULL);
}

struct ar5212Stats
*ar5212AniGetCurrentStats(struct ath_hal *)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_NULL);
}
#endif

HAL_BOOL
ar9300_Stub_AniControl(struct ath_hal *ah, HAL_ANI_CMD cmd, int param)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

#if 0
HAL_BOOL
ar9300_Stub_AniSetParams(struct ath_hal *, const struct ar5212AniParams *,
    const struct ar5212AniParams *)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}
#endif

struct ath_rx_status;

void
ar9300_Stub_AniPhyErrReport(struct ath_hal *ah,
		const struct ath_rx_status *rs)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return;
}

void
ar9300_Stub_ProcessMibIntr(struct ath_hal *ah, const HAL_NODE_STATS *stats)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_RxMonitor(struct ath_hal *ah, const HAL_NODE_STATS *stats,
    const struct ieee80211_channel *chan)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_AniPoll(struct ath_hal *ah, const struct ieee80211_channel * chan)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_AniReset(struct ath_hal *ah, const struct ieee80211_channel * chan,
		HAL_OPMODE ani_opmode, int val)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

HAL_BOOL
ar9300_Stub_IsNFCalInProgress(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_WaitNFCalComplete(struct ath_hal *ah, int i)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

void
ar9300_Stub_EnableDfs(struct ath_hal *ah, HAL_PHYERR_PARAM *pe)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_GetDfsThresh(struct ath_hal *ah, HAL_PHYERR_PARAM *pe)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

HAL_BOOL
ar9300_Stub_ProcessRadarEvent(struct ath_hal *ah,
    struct ath_rx_status *rxs, uint64_t fulltsf, const char *buf,
    HAL_DFS_EVENT *event)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

HAL_BOOL
ar9300_Stub_IsFastClockEnabled(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (AH_FALSE);
}

uint32_t
ar9300_Stub_Get11nExtBusy(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return (0);
}

void
ar9300_Stub_ConfigPCIE(struct ath_hal *ah, HAL_BOOL restore, HAL_BOOL powerOff)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

void
ar9300_Stub_DisablePCIE(struct ath_hal *ah)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
}

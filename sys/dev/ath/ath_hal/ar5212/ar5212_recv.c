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
#include "ah_desc.h"

#include "ar5212/ar5212.h"
#include "ar5212/ar5212reg.h"
#include "ar5212/ar5212desc.h"

/*
 * Get the RXDP.
 */
uint32_t
ar5212GetRxDP(struct ath_hal *ath, HAL_RX_QUEUE qtype)
{

	HALASSERT(qtype == HAL_RX_QUEUE_HP);
	return OS_REG_READ(ath, AR_RXDP);
}

/*
 * Set the RxDP.
 */
void
ar5212SetRxDP(struct ath_hal *ah, uint32_t rxdp, HAL_RX_QUEUE qtype)
{

	HALASSERT(qtype == HAL_RX_QUEUE_HP);
	OS_REG_WRITE(ah, AR_RXDP, rxdp);
	HALASSERT(OS_REG_READ(ah, AR_RXDP) == rxdp);
}

/*
 * Set Receive Enable bits.
 */
void
ar5212EnableReceive(struct ath_hal *ah)
{
	OS_REG_WRITE(ah, AR_CR, AR_CR_RXE);
}

/*
 * Stop Receive at the DMA engine
 */
HAL_BOOL
ar5212StopDmaReceive(struct ath_hal *ah)
{
	OS_MARK(ah, AH_MARK_RX_CTL, AH_MARK_RX_CTL_DMA_STOP);
	OS_REG_WRITE(ah, AR_CR, AR_CR_RXD);	/* Set receive disable bit */
	if (!ath_hal_wait(ah, AR_CR, AR_CR_RXE, 0)) {
		OS_MARK(ah, AH_MARK_RX_CTL, AH_MARK_RX_CTL_DMA_STOP_ERR);
#ifdef AH_DEBUG
		ath_hal_printf(ah, "%s: dma failed to stop in 10ms\n"
			"AR_CR=0x%08x\nAR_DIAG_SW=0x%08x\n",
			__func__,
			OS_REG_READ(ah, AR_CR),
			OS_REG_READ(ah, AR_DIAG_SW));
#endif
		return AH_FALSE;
	} else {
		return AH_TRUE;
	}
}

/*
 * Start Transmit at the PCU engine (unpause receive)
 */
void
ar5212StartPcuReceive(struct ath_hal *ah)
{
	struct ath_hal_private *ahp = AH_PRIVATE(ah);

	OS_MARK(ah, AH_MARK_RX_CTL, AH_MARK_RX_CTL_PCU_START);
	OS_REG_WRITE(ah, AR_DIAG_SW,
		OS_REG_READ(ah, AR_DIAG_SW) &~ AR_DIAG_RX_DIS);
	ar5212EnableMibCounters(ah);
	/* NB: restore current settings */
	ar5212AniReset(ah, ahp->ah_curchan, ahp->ah_opmode, AH_TRUE);
}

/*
 * Stop Transmit at the PCU engine (pause receive)
 */
void
ar5212StopPcuReceive(struct ath_hal *ah)
{
	OS_MARK(ah, AH_MARK_RX_CTL, AH_MARK_RX_CTL_PCU_STOP);
	OS_REG_WRITE(ah, AR_DIAG_SW,
		OS_REG_READ(ah, AR_DIAG_SW) | AR_DIAG_RX_DIS);
	ar5212DisableMibCounters(ah);
}

/*
 * Set multicast filter 0 (lower 32-bits)
 *               filter 1 (upper 32-bits)
 */
void
ar5212SetMulticastFilter(struct ath_hal *ah, uint32_t filter0, uint32_t filter1)
{
	OS_REG_WRITE(ah, AR_MCAST_FIL0, filter0);
	OS_REG_WRITE(ah, AR_MCAST_FIL1, filter1);
}

/*
 * Clear multicast filter by index
 */
HAL_BOOL
ar5212ClrMulticastFilterIndex(struct ath_hal *ah, uint32_t ix)
{
	uint32_t val;

	if (ix >= 64)
		return AH_FALSE;
	if (ix >= 32) {
		val = OS_REG_READ(ah, AR_MCAST_FIL1);
		OS_REG_WRITE(ah, AR_MCAST_FIL1, (val &~ (1<<(ix-32))));
	} else {
		val = OS_REG_READ(ah, AR_MCAST_FIL0);
		OS_REG_WRITE(ah, AR_MCAST_FIL0, (val &~ (1<<ix)));
	}
	return AH_TRUE;
}

/*
 * Set multicast filter by index
 */
HAL_BOOL
ar5212SetMulticastFilterIndex(struct ath_hal *ah, uint32_t ix)
{
	uint32_t val;

	if (ix >= 64)
		return AH_FALSE;
	if (ix >= 32) {
		val = OS_REG_READ(ah, AR_MCAST_FIL1);
		OS_REG_WRITE(ah, AR_MCAST_FIL1, (val | (1<<(ix-32))));
	} else {
		val = OS_REG_READ(ah, AR_MCAST_FIL0);
		OS_REG_WRITE(ah, AR_MCAST_FIL0, (val | (1<<ix)));
	}
	return AH_TRUE;
}

/*
 * Get the receive filter.
 */
uint32_t
ar5212GetRxFilter(struct ath_hal *ah)
{
	uint32_t bits = OS_REG_READ(ah, AR_RX_FILTER);
	uint32_t phybits = OS_REG_READ(ah, AR_PHY_ERR);
	if (phybits & AR_PHY_ERR_RADAR)
		bits |= HAL_RX_FILTER_PHYRADAR;
	if (phybits & (AR_PHY_ERR_OFDM_TIMING|AR_PHY_ERR_CCK_TIMING))
		bits |= HAL_RX_FILTER_PHYERR;
	if (AH_PRIVATE(ah)->ah_caps.halBssidMatchSupport &&
	    (AH5212(ah)->ah_miscMode & AR_MISC_MODE_BSSID_MATCH_FORCE))
		bits |= HAL_RX_FILTER_BSSID;
	return bits;
}

/*
 * Set the receive filter.
 */
void
ar5212SetRxFilter(struct ath_hal *ah, uint32_t bits)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	uint32_t phybits;

	OS_REG_WRITE(ah, AR_RX_FILTER,
	    bits &~ (HAL_RX_FILTER_PHYRADAR|HAL_RX_FILTER_PHYERR|
	    HAL_RX_FILTER_BSSID));
	phybits = 0;
	if (bits & HAL_RX_FILTER_PHYRADAR)
		phybits |= AR_PHY_ERR_RADAR;
	if (bits & HAL_RX_FILTER_PHYERR)
		phybits |= AR_PHY_ERR_OFDM_TIMING | AR_PHY_ERR_CCK_TIMING;
	OS_REG_WRITE(ah, AR_PHY_ERR, phybits);
	if (phybits) {
		OS_REG_WRITE(ah, AR_RXCFG,
			OS_REG_READ(ah, AR_RXCFG) | AR_RXCFG_ZLFDMA);
	} else {
		OS_REG_WRITE(ah, AR_RXCFG,
			OS_REG_READ(ah, AR_RXCFG) &~ AR_RXCFG_ZLFDMA);
	}
	if (AH_PRIVATE(ah)->ah_caps.halBssidMatchSupport) {
		if (bits & HAL_RX_FILTER_BSSID)
			ahp->ah_miscMode |= AR_MISC_MODE_BSSID_MATCH_FORCE;
		else
			ahp->ah_miscMode &= ~AR_MISC_MODE_BSSID_MATCH_FORCE;
		OS_REG_WRITE(ah, AR_MISC_MODE, OS_REG_READ(ah, AR_MISC_MODE) | ahp->ah_miscMode);
	}
}

/*
 * Initialize RX descriptor, by clearing the status and setting
 * the size (and any other flags).
 */
HAL_BOOL
ar5212SetupRxDesc(struct ath_hal *ah, struct ath_desc *ds,
	uint32_t size, u_int flags)
{
	struct ar5212_desc *ads = AR5212DESC(ds);

	HALASSERT((size &~ AR_BufLen) == 0);

	ads->ds_ctl0 = 0;
	ads->ds_ctl1 = size & AR_BufLen;

	if (flags & HAL_RXDESC_INTREQ)
		ads->ds_ctl1 |= AR_RxInterReq;
	ads->ds_rxstatus0 = ads->ds_rxstatus1 = 0;

	return AH_TRUE;
}

/*
 * Process an RX descriptor, and return the status to the caller.
 * Copy some hardware specific items into the software portion
 * of the descriptor.
 *
 * NB: the caller is responsible for validating the memory contents
 *     of the descriptor (e.g. flushing any cached copy).
 */
HAL_STATUS
ar5212ProcRxDesc(struct ath_hal *ah, struct ath_desc *ds,
	uint32_t pa, struct ath_desc *nds, uint64_t tsf,
	struct ath_rx_status *rs)
{
	struct ar5212_desc *ads = AR5212DESC(ds);
	struct ar5212_desc *ands = AR5212DESC(nds);

	if ((ads->ds_rxstatus1 & AR_Done) == 0)
		return HAL_EINPROGRESS;
	/*
	 * Given the use of a self-linked tail be very sure that the hw is
	 * done with this descriptor; the hw may have done this descriptor
	 * once and picked it up again...make sure the hw has moved on.
	 */
	if ((ands->ds_rxstatus1&AR_Done) == 0 && OS_REG_READ(ah, AR_RXDP) == pa)
		return HAL_EINPROGRESS;

	rs->rs_datalen = ads->ds_rxstatus0 & AR_DataLen;
	rs->rs_tstamp = MS(ads->ds_rxstatus1, AR_RcvTimestamp);
	rs->rs_status = 0;
	/* XXX what about KeyCacheMiss? */
	rs->rs_rssi = MS(ads->ds_rxstatus0, AR_RcvSigStrength);
	/* discard invalid h/w rssi data */
	if (rs->rs_rssi == -128)
		rs->rs_rssi = 0;
	if (ads->ds_rxstatus1 & AR_KeyIdxValid)
		rs->rs_keyix = MS(ads->ds_rxstatus1, AR_KeyIdx);
	else
		rs->rs_keyix = HAL_RXKEYIX_INVALID;
	/* NB: caller expected to do rate table mapping */
	rs->rs_rate = MS(ads->ds_rxstatus0, AR_RcvRate);
	rs->rs_antenna  = MS(ads->ds_rxstatus0, AR_RcvAntenna);
	rs->rs_more = (ads->ds_rxstatus0 & AR_More) ? 1 : 0;

	/*
	 * The AR5413 (at least) sometimes sets both AR_CRCErr and
	 * AR_PHYErr when reporting radar pulses.  In this instance
	 * set HAL_RXERR_PHY as well as HAL_RXERR_CRC and
	 * let the driver layer figure out what to do.
	 *
	 * See PR kern/169362.
	 */
	if ((ads->ds_rxstatus1 & AR_FrmRcvOK) == 0) {
		/*
		 * These four bits should not be set together.  The
		 * 5212 spec states a Michael error can only occur if
		 * DecryptCRCErr not set (and TKIP is used).  Experience
		 * indicates however that you can also get Michael errors
		 * when a CRC error is detected, but these are specious.
		 * Consequently we filter them out here so we don't
		 * confuse and/or complicate drivers.
		 */
		if (ads->ds_rxstatus1 & AR_PHYErr) {
			u_int phyerr;

			rs->rs_status |= HAL_RXERR_PHY;
			phyerr = MS(ads->ds_rxstatus1, AR_PHYErrCode);
			rs->rs_phyerr = phyerr;
			if (!AH5212(ah)->ah_hasHwPhyCounters &&
			    phyerr != HAL_PHYERR_RADAR)
				ar5212AniPhyErrReport(ah, rs);
		}

		if (ads->ds_rxstatus1 & AR_CRCErr)
			rs->rs_status |= HAL_RXERR_CRC;
		else if (ads->ds_rxstatus1 & AR_DecryptCRCErr)
			rs->rs_status |= HAL_RXERR_DECRYPT;
		else if (ads->ds_rxstatus1 & AR_MichaelErr)
			rs->rs_status |= HAL_RXERR_MIC;
	}
	return HAL_OK;
}

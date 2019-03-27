/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
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
#include "ah_desc.h"

#include "ar5210/ar5210.h"
#include "ar5210/ar5210reg.h"
#include "ar5210/ar5210desc.h"

/*
 * Get the RXDP.
 */
uint32_t
ar5210GetRxDP(struct ath_hal *ah, HAL_RX_QUEUE qtype)
{

	HALASSERT(qtype == HAL_RX_QUEUE_HP);
	return OS_REG_READ(ah, AR_RXDP);
}

/*
 * Set the RxDP.
 */
void
ar5210SetRxDP(struct ath_hal *ah, uint32_t rxdp, HAL_RX_QUEUE qtype)
{

	HALASSERT(qtype == HAL_RX_QUEUE_HP);
	OS_REG_WRITE(ah, AR_RXDP, rxdp);
}


/*
 * Set Receive Enable bits.
 */
void
ar5210EnableReceive(struct ath_hal *ah)
{
	OS_REG_WRITE(ah, AR_CR, AR_CR_RXE);
}

/*
 * Stop Receive at the DMA engine
 */
HAL_BOOL
ar5210StopDmaReceive(struct ath_hal *ah)
{
	int i;

	OS_REG_WRITE(ah, AR_CR, AR_CR_RXD);	/* Set receive disable bit */
	for (i = 0; i < 1000; i++) {
		if ((OS_REG_READ(ah, AR_CR) & AR_CR_RXE) == 0)
			return AH_TRUE;
		OS_DELAY(10);
	}
#ifdef AH_DEBUG
	ath_hal_printf(ah, "ar5210: dma receive failed to stop in 10ms\n");
	ath_hal_printf(ah, "AR_CR=0x%x\n", OS_REG_READ(ah, AR_CR));
	ath_hal_printf(ah, "AR_DIAG_SW=0x%x\n", OS_REG_READ(ah, AR_DIAG_SW));
#endif
	return AH_FALSE;
}

/*
 * Start Transmit at the PCU engine (unpause receive)
 */
void
ar5210StartPcuReceive(struct ath_hal *ah)
{
	ar5210UpdateDiagReg(ah,
		OS_REG_READ(ah, AR_DIAG_SW) & ~(AR_DIAG_SW_DIS_RX));
}

/*
 * Stop Transmit at the PCU engine (pause receive)
 */
void
ar5210StopPcuReceive(struct ath_hal *ah)
{
	ar5210UpdateDiagReg(ah,
		OS_REG_READ(ah, AR_DIAG_SW) | AR_DIAG_SW_DIS_RX);
}

/*
 * Set multicast filter 0 (lower 32-bits)
 *			   filter 1 (upper 32-bits)
 */
void
ar5210SetMulticastFilter(struct ath_hal *ah, uint32_t filter0, uint32_t filter1)
{
	OS_REG_WRITE(ah, AR_MCAST_FIL0, filter0);
	OS_REG_WRITE(ah, AR_MCAST_FIL1, filter1);
}

/*
 * Clear multicast filter by index
 */
HAL_BOOL
ar5210ClrMulticastFilterIndex(struct ath_hal *ah, uint32_t ix)
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
ar5210SetMulticastFilterIndex(struct ath_hal *ah, uint32_t ix)
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
 * Return the receive packet filter.
 */
uint32_t
ar5210GetRxFilter(struct ath_hal *ah)
{
	/* XXX can't be sure if promiscuous mode is set because of PHYRADAR */
	return OS_REG_READ(ah, AR_RX_FILTER);
}

/*
 * Turn off/on bits in the receive packet filter.
 */
void
ar5210SetRxFilter(struct ath_hal *ah, uint32_t bits)
{
	if (bits & HAL_RX_FILTER_PHYRADAR) {
		/* must enable promiscuous mode to get radar */
		bits = (bits &~ HAL_RX_FILTER_PHYRADAR) | AR_RX_FILTER_PROMISCUOUS;
	}
	OS_REG_WRITE(ah, AR_RX_FILTER, bits);
}

/*
 * Initialize RX descriptor, by clearing the status and clearing
 * the size.  This is not strictly HW dependent, but we want the
 * control and status words to be opaque above the hal.
 */
HAL_BOOL
ar5210SetupRxDesc(struct ath_hal *ah, struct ath_desc *ds,
	uint32_t size, u_int flags)
{
	struct ar5210_desc *ads = AR5210DESC(ds);

	(void) flags;

	ads->ds_ctl0 = 0;
	ads->ds_ctl1 = size & AR_BufLen;
	if (ads->ds_ctl1 != size) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: buffer size %u too large\n",
		    __func__, size);
		return AH_FALSE;
	}
	if (flags & HAL_RXDESC_INTREQ)
		ads->ds_ctl1 |= AR_RxInterReq;
	ads->ds_status0 = ads->ds_status1 = 0;

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
ar5210ProcRxDesc(struct ath_hal *ah, struct ath_desc *ds,
	uint32_t pa, struct ath_desc *nds, uint64_t tsf,
	struct ath_rx_status *rs)
{
	struct ar5210_desc *ads = AR5210DESC(ds);
	struct ar5210_desc *ands = AR5210DESC(nds);
	uint32_t now, rstamp;

	if ((ads->ds_status1 & AR_Done) == 0)
		return HAL_EINPROGRESS;
	/*
	 * Given the use of a self-linked tail be very sure that the hw is
	 * done with this descriptor; the hw may have done this descriptor
	 * once and picked it up again...make sure the hw has moved on.
	 */
	if ((ands->ds_status1 & AR_Done) == 0 && OS_REG_READ(ah, AR_RXDP) == pa)
		return HAL_EINPROGRESS;

	rs->rs_datalen = ads->ds_status0 & AR_DataLen;
	rstamp = MS(ads->ds_status1, AR_RcvTimestamp);
	/*
	 * Convert timestamp.  The value in the
	 * descriptor is bits [10..22] of the TSF.
	 */
	now = (OS_REG_READ(ah, AR_TSF_L32) >> 10) & 0xffff;
	if ((now & 0x1fff) < rstamp)
		rstamp |= (now - 0x2000) & 0xffff;
	else
		rstamp |= now;
	/* NB: keep only 15 bits for consistency w/ other chips */
	rs->rs_tstamp = rstamp & 0x7fff;
	rs->rs_status = 0;
	if ((ads->ds_status1 & AR_FrmRcvOK) == 0) {
		if (ads->ds_status1 & AR_CRCErr)
			rs->rs_status |= HAL_RXERR_CRC;
		else if (ads->ds_status1 & AR_DecryptCRCErr)
			rs->rs_status |= HAL_RXERR_DECRYPT;
		else if (ads->ds_status1 & AR_FIFOOverrun)
			rs->rs_status |= HAL_RXERR_FIFO;
		else {
			rs->rs_status |= HAL_RXERR_PHY;
			rs->rs_phyerr =
				(ads->ds_status1 & AR_PHYErr) >> AR_PHYErr_S;
		}
	}
	/* XXX what about KeyCacheMiss? */
	rs->rs_rssi = MS(ads->ds_status0, AR_RcvSigStrength);
	if (ads->ds_status1 & AR_KeyIdxValid)
		rs->rs_keyix = MS(ads->ds_status1, AR_KeyIdx);
	else
		rs->rs_keyix = HAL_RXKEYIX_INVALID;
	/* NB: caller expected to do rate table mapping */
	rs->rs_rate = MS(ads->ds_status0, AR_RcvRate);
	rs->rs_antenna  = (ads->ds_status0 & AR_RcvAntenna) ? 1 : 0;
	rs->rs_more = (ads->ds_status0 & AR_More) ? 1 : 0;

	return HAL_OK;
}

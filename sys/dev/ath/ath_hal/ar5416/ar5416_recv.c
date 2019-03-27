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
#include "ah_desc.h"
#include "ah_internal.h"

#include "ar5416/ar5416.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416desc.h"

/*
 * Get the receive filter.
 */
uint32_t
ar5416GetRxFilter(struct ath_hal *ah)
{
	uint32_t bits = OS_REG_READ(ah, AR_RX_FILTER);
	uint32_t phybits = OS_REG_READ(ah, AR_PHY_ERR);

	if (phybits & AR_PHY_ERR_RADAR)
		bits |= HAL_RX_FILTER_PHYRADAR;
	if (phybits & (AR_PHY_ERR_OFDM_TIMING | AR_PHY_ERR_CCK_TIMING))
		bits |= HAL_RX_FILTER_PHYERR;
	return bits;
}

/*
 * Set the receive filter.
 */
void
ar5416SetRxFilter(struct ath_hal *ah, u_int32_t bits)
{
	uint32_t phybits;

	OS_REG_WRITE(ah, AR_RX_FILTER, (bits & 0xffff));
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
}

/*
 * Stop Receive at the DMA engine
 */
HAL_BOOL
ar5416StopDmaReceive(struct ath_hal *ah)
{
	HAL_BOOL status;

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
		status = AH_FALSE;
	} else {
		status = AH_TRUE;
	}

	/*
	 * XXX Is this to flush whatever is in a FIFO somewhere?
	 * XXX If so, what should the correct behaviour should be?
	 */
	if (AR_SREV_9100(ah))
		OS_DELAY(3000);

	return (status);
}

/*
 * Start receive at the PCU engine
 */
void
ar5416StartPcuReceive(struct ath_hal *ah)
{
	struct ath_hal_private *ahp = AH_PRIVATE(ah);

	HALDEBUG(ah, HAL_DEBUG_RX, "%s: Start PCU Receive \n", __func__);
	ar5212EnableMibCounters(ah);
	/* NB: restore current settings */
	ar5416AniReset(ah, ahp->ah_curchan, ahp->ah_opmode, AH_TRUE);
	/*
	 * NB: must do after enabling phy errors to avoid rx
	 *     frames w/ corrupted descriptor status.
	 */
	OS_REG_CLR_BIT(ah, AR_DIAG_SW, AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT);
}

/*
 * Stop receive at the PCU engine
 * and abort current frame in PCU
 */
void
ar5416StopPcuReceive(struct ath_hal *ah)
{
	OS_REG_SET_BIT(ah, AR_DIAG_SW, AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT);
    
	HALDEBUG(ah, HAL_DEBUG_RX, "%s: Stop PCU Receive \n", __func__);
	ar5212DisableMibCounters(ah);
}

/*
 * Initialize RX descriptor, by clearing the status and setting
 * the size (and any other flags).
 */
HAL_BOOL
ar5416SetupRxDesc(struct ath_hal *ah, struct ath_desc *ds,
    uint32_t size, u_int flags)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	HALASSERT((size &~ AR_BufLen) == 0);

	ads->ds_ctl1 = size & AR_BufLen;
	if (flags & HAL_RXDESC_INTREQ)
		ads->ds_ctl1 |= AR_RxIntrReq;

	/* this should be enough */
	ads->ds_rxstatus8 &= ~AR_RxDone;

	/* clear the rest of the status fields */
	OS_MEMZERO(&(ads->u), sizeof(ads->u));

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
ar5416ProcRxDesc(struct ath_hal *ah, struct ath_desc *ds,
    uint32_t pa, struct ath_desc *nds, uint64_t tsf,
    struct ath_rx_status *rs)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	if ((ads->ds_rxstatus8 & AR_RxDone) == 0)
		return HAL_EINPROGRESS;

	rs->rs_status = 0;
	rs->rs_flags = 0;

	rs->rs_datalen = ads->ds_rxstatus1 & AR_DataLen;
	rs->rs_tstamp =  ads->AR_RcvTimestamp;

	/* XXX what about KeyCacheMiss? */

	rs->rs_rssi = MS(ads->ds_rxstatus4, AR_RxRSSICombined);
	rs->rs_rssi_ctl[0] = MS(ads->ds_rxstatus0, AR_RxRSSIAnt00);
	rs->rs_rssi_ctl[1] = MS(ads->ds_rxstatus0, AR_RxRSSIAnt01);
	rs->rs_rssi_ctl[2] = MS(ads->ds_rxstatus0, AR_RxRSSIAnt02);
	rs->rs_rssi_ext[0] = MS(ads->ds_rxstatus4, AR_RxRSSIAnt10);
	rs->rs_rssi_ext[1] = MS(ads->ds_rxstatus4, AR_RxRSSIAnt11);
	rs->rs_rssi_ext[2] = MS(ads->ds_rxstatus4, AR_RxRSSIAnt12);

	if (ads->ds_rxstatus8 & AR_RxKeyIdxValid)
		rs->rs_keyix = MS(ads->ds_rxstatus8, AR_KeyIdx);
	else
		rs->rs_keyix = HAL_RXKEYIX_INVALID;

	/* NB: caller expected to do rate table mapping */
	rs->rs_rate = RXSTATUS_RATE(ah, ads);
	rs->rs_more = (ads->ds_rxstatus1 & AR_RxMore) ? 1 : 0;

	rs->rs_isaggr = (ads->ds_rxstatus8 & AR_RxAggr) ? 1 : 0;
	rs->rs_moreaggr = (ads->ds_rxstatus8 & AR_RxMoreAggr) ? 1 : 0;
	rs->rs_antenna = MS(ads->ds_rxstatus3, AR_RxAntenna);

	if (ads->ds_rxstatus3 & AR_GI)
		rs->rs_flags |= HAL_RX_GI;
	if (ads->ds_rxstatus3 & AR_2040)
		rs->rs_flags |= HAL_RX_2040;

	/*
	 * Only the AR9280 and later chips support STBC RX, so
	 * ensure we only set this bit for those chips.
	 */
	if (AR_SREV_MERLIN_10_OR_LATER(ah)
	    && ads->ds_rxstatus3 & AR_STBCFrame)
		rs->rs_flags |= HAL_RX_STBC;

	if (ads->ds_rxstatus8 & AR_PreDelimCRCErr)
		rs->rs_flags |= HAL_RX_DELIM_CRC_PRE;
	if (ads->ds_rxstatus8 & AR_PostDelimCRCErr)
		rs->rs_flags |= HAL_RX_DELIM_CRC_POST;
	if (ads->ds_rxstatus8 & AR_DecryptBusyErr)
		rs->rs_flags |= HAL_RX_DECRYPT_BUSY;
	if (ads->ds_rxstatus8 & AR_HiRxChain)
		rs->rs_flags |= HAL_RX_HI_RX_CHAIN;

	if ((ads->ds_rxstatus8 & AR_RxFrameOK) == 0) {
		/*
		 * These four bits should not be set together.  The
		 * 5416 spec states a Michael error can only occur if
		 * DecryptCRCErr not set (and TKIP is used).  Experience
		 * indicates however that you can also get Michael errors
		 * when a CRC error is detected, but these are specious.
		 * Consequently we filter them out here so we don't
		 * confuse and/or complicate drivers.
		 */

		/*
		 * The AR5416 sometimes sets both AR_CRCErr and AR_PHYErr
		 * when reporting radar pulses.  In this instance
		 * set HAL_RXERR_PHY as well as HAL_RXERR_CRC and
		 * let the driver layer figure out what to do.
		 *
		 * See PR kern/169362.
		 */
		if (ads->ds_rxstatus8 & AR_PHYErr) {
			u_int phyerr;

			/*
			 * Packets with OFDM_RESTART on post delimiter are CRC OK and
			 * usable and MAC ACKs them.
			 * To avoid packet from being lost, we remove the PHY Err flag
			 * so that driver layer does not drop them.
			 */
			phyerr = MS(ads->ds_rxstatus8, AR_PHYErrCode);

			if ((phyerr == HAL_PHYERR_OFDM_RESTART) &&
			    (ads->ds_rxstatus8 & AR_PostDelimCRCErr)) {
				ath_hal_printf(ah,
				    "%s: OFDM_RESTART on post-delim CRC error\n",
				    __func__);
				rs->rs_phyerr = 0;
			} else {
				rs->rs_status |= HAL_RXERR_PHY;
				rs->rs_phyerr = phyerr;
			}
		}
		if (ads->ds_rxstatus8 & AR_CRCErr)
			rs->rs_status |= HAL_RXERR_CRC;
		else if (ads->ds_rxstatus8 & AR_DecryptCRCErr)
			rs->rs_status |= HAL_RXERR_DECRYPT;
		else if (ads->ds_rxstatus8 & AR_MichaelErr)
			rs->rs_status |= HAL_RXERR_MIC;
	}

	return HAL_OK;
}

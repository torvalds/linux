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
#include "ah_desc.h"

#include "ar5210/ar5210.h"
#include "ar5210/ar5210reg.h"
#include "ar5210/ar5210phy.h"
#include "ar5210/ar5210desc.h"

/*
 * Set the properties of the tx queue with the parameters
 * from qInfo.  The queue must previously have been setup
 * with a call to ar5210SetupTxQueue.
 */
HAL_BOOL
ar5210SetTxQueueProps(struct ath_hal *ah, int q, const HAL_TXQ_INFO *qInfo)
{
	struct ath_hal_5210 *ahp = AH5210(ah);

	if (q >= HAL_NUM_TX_QUEUES) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid queue num %u\n",
		    __func__, q);
		return AH_FALSE;
	}
	return ath_hal_setTxQProps(ah, &ahp->ah_txq[q], qInfo);
}

/*
 * Return the properties for the specified tx queue.
 */
HAL_BOOL
ar5210GetTxQueueProps(struct ath_hal *ah, int q, HAL_TXQ_INFO *qInfo)
{
	struct ath_hal_5210 *ahp = AH5210(ah);

	if (q >= HAL_NUM_TX_QUEUES) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid queue num %u\n",
		    __func__, q);
		return AH_FALSE;
	}
	return ath_hal_getTxQProps(ah, qInfo, &ahp->ah_txq[q]);
}

/*
 * Allocate and initialize a tx DCU/QCU combination.
 */
int
ar5210SetupTxQueue(struct ath_hal *ah, HAL_TX_QUEUE type,
	const HAL_TXQ_INFO *qInfo)
{
	struct ath_hal_5210 *ahp = AH5210(ah);
	HAL_TX_QUEUE_INFO *qi;
	int q;

	switch (type) {
	case HAL_TX_QUEUE_BEACON:
		q = 2;
		break;
	case HAL_TX_QUEUE_CAB:
		q = 1;
		break;
	case HAL_TX_QUEUE_DATA:
		q = 0;
		break;
	default:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: bad tx queue type %u\n",
		    __func__, type);
		return -1;
	}

	HALDEBUG(ah, HAL_DEBUG_TXQUEUE, "%s: queue %u\n", __func__, q);

	qi = &ahp->ah_txq[q];
	if (qi->tqi_type != HAL_TX_QUEUE_INACTIVE) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: tx queue %u already active\n",
		    __func__, q);
		return -1;
	}
	OS_MEMZERO(qi, sizeof(HAL_TX_QUEUE_INFO));
	qi->tqi_type = type;
	if (qInfo == AH_NULL) {
		/* by default enable OK+ERR+DESC+URN interrupts */
		qi->tqi_qflags =
			  HAL_TXQ_TXOKINT_ENABLE
			| HAL_TXQ_TXERRINT_ENABLE
			| HAL_TXQ_TXDESCINT_ENABLE
			| HAL_TXQ_TXURNINT_ENABLE
			;
		qi->tqi_aifs = INIT_AIFS;
		qi->tqi_cwmin = HAL_TXQ_USEDEFAULT;	/* NB: do at reset */
		qi->tqi_shretry = INIT_SH_RETRY;
		qi->tqi_lgretry = INIT_LG_RETRY;
	} else
		(void) ar5210SetTxQueueProps(ah, q, qInfo);
	/* NB: must be followed by ar5210ResetTxQueue */
	return q;
}

/*
 * Free a tx DCU/QCU combination.
 */
HAL_BOOL
ar5210ReleaseTxQueue(struct ath_hal *ah, u_int q)
{
	struct ath_hal_5210 *ahp = AH5210(ah);
	HAL_TX_QUEUE_INFO *qi;

	if (q >= HAL_NUM_TX_QUEUES) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid queue num %u\n",
		    __func__, q);
		return AH_FALSE;
	}
	qi = &ahp->ah_txq[q];
	if (qi->tqi_type == HAL_TX_QUEUE_INACTIVE) {
		HALDEBUG(ah, HAL_DEBUG_TXQUEUE, "%s: inactive queue %u\n",
		    __func__, q);
		return AH_FALSE;
	}

	HALDEBUG(ah, HAL_DEBUG_TXQUEUE, "%s: release queue %u\n", __func__, q);

	qi->tqi_type = HAL_TX_QUEUE_INACTIVE;
	ahp->ah_txOkInterruptMask &= ~(1 << q);
	ahp->ah_txErrInterruptMask &= ~(1 << q);
	ahp->ah_txDescInterruptMask &= ~(1 << q);
	ahp->ah_txEolInterruptMask &= ~(1 << q);
	ahp->ah_txUrnInterruptMask &= ~(1 << q);

	return AH_TRUE;
#undef N
}

HAL_BOOL
ar5210ResetTxQueue(struct ath_hal *ah, u_int q)
{
	struct ath_hal_5210 *ahp = AH5210(ah);
	const struct ieee80211_channel *chan = AH_PRIVATE(ah)->ah_curchan;
	HAL_TX_QUEUE_INFO *qi;
	uint32_t cwMin;

	if (q >= HAL_NUM_TX_QUEUES) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid queue num %u\n",
		    __func__, q);
		return AH_FALSE;
	}
	qi = &ahp->ah_txq[q];
	if (qi->tqi_type == HAL_TX_QUEUE_INACTIVE) {
		HALDEBUG(ah, HAL_DEBUG_TXQUEUE, "%s: inactive queue %u\n",
		    __func__, q);
		return AH_FALSE;
	}

	/*
	 * Ignore any non-data queue(s).
	 */
	if (qi->tqi_type != HAL_TX_QUEUE_DATA)
		return AH_TRUE;

	/* Set turbo mode / base mode parameters on or off */
	if (IEEE80211_IS_CHAN_TURBO(chan)) {
		OS_REG_WRITE(ah, AR_SLOT_TIME, INIT_SLOT_TIME_TURBO);
		OS_REG_WRITE(ah, AR_TIME_OUT, INIT_ACK_CTS_TIMEOUT_TURBO);
		OS_REG_WRITE(ah, AR_USEC, INIT_TRANSMIT_LATENCY_TURBO);
		OS_REG_WRITE(ah, AR_IFS0, 
			((INIT_SIFS_TURBO + qi->tqi_aifs * INIT_SLOT_TIME_TURBO)
				<< AR_IFS0_DIFS_S)
			| INIT_SIFS_TURBO);
		OS_REG_WRITE(ah, AR_IFS1, INIT_PROTO_TIME_CNTRL_TURBO);
		OS_REG_WRITE(ah, AR_PHY(17),
			(OS_REG_READ(ah, AR_PHY(17)) & ~0x7F) | 0x38);
		OS_REG_WRITE(ah, AR_PHY_FRCTL,
			AR_PHY_SERVICE_ERR | AR_PHY_TXURN_ERR |
			AR_PHY_ILLLEN_ERR | AR_PHY_ILLRATE_ERR |
			AR_PHY_PARITY_ERR | AR_PHY_TIMING_ERR |
			0x2020 |
			AR_PHY_TURBO_MODE | AR_PHY_TURBO_SHORT);
	} else {
		OS_REG_WRITE(ah, AR_SLOT_TIME, INIT_SLOT_TIME);
		OS_REG_WRITE(ah, AR_TIME_OUT, INIT_ACK_CTS_TIMEOUT);
		OS_REG_WRITE(ah, AR_USEC, INIT_TRANSMIT_LATENCY);
		OS_REG_WRITE(ah, AR_IFS0, 
			((INIT_SIFS + qi->tqi_aifs * INIT_SLOT_TIME)
				<< AR_IFS0_DIFS_S)
			| INIT_SIFS);
		OS_REG_WRITE(ah, AR_IFS1, INIT_PROTO_TIME_CNTRL);
		OS_REG_WRITE(ah, AR_PHY(17),
			(OS_REG_READ(ah, AR_PHY(17)) & ~0x7F) | 0x1C);
		OS_REG_WRITE(ah, AR_PHY_FRCTL,
			AR_PHY_SERVICE_ERR | AR_PHY_TXURN_ERR |
			AR_PHY_ILLLEN_ERR | AR_PHY_ILLRATE_ERR |
			AR_PHY_PARITY_ERR | AR_PHY_TIMING_ERR | 0x1020);
	}

	if (qi->tqi_cwmin == HAL_TXQ_USEDEFAULT)
		cwMin = INIT_CWMIN;
	else
		cwMin = qi->tqi_cwmin;

	/* Set cwmin and retry limit values */
	OS_REG_WRITE(ah, AR_RETRY_LMT, 
		  (cwMin << AR_RETRY_LMT_CW_MIN_S)
		 | SM(INIT_SLG_RETRY, AR_RETRY_LMT_SLG_RETRY)
		 | SM(INIT_SSH_RETRY, AR_RETRY_LMT_SSH_RETRY)
		 | SM(qi->tqi_lgretry, AR_RETRY_LMT_LG_RETRY)
		 | SM(qi->tqi_shretry, AR_RETRY_LMT_SH_RETRY)
	);

	if (qi->tqi_qflags & HAL_TXQ_TXOKINT_ENABLE)
		ahp->ah_txOkInterruptMask |= 1 << q;
	else
		ahp->ah_txOkInterruptMask &= ~(1 << q);
	if (qi->tqi_qflags & HAL_TXQ_TXERRINT_ENABLE)
		ahp->ah_txErrInterruptMask |= 1 << q;
	else
		ahp->ah_txErrInterruptMask &= ~(1 << q);
	if (qi->tqi_qflags & HAL_TXQ_TXDESCINT_ENABLE)
		ahp->ah_txDescInterruptMask |= 1 << q;
	else
		ahp->ah_txDescInterruptMask &= ~(1 << q);
	if (qi->tqi_qflags & HAL_TXQ_TXEOLINT_ENABLE)
		ahp->ah_txEolInterruptMask |= 1 << q;
	else
		ahp->ah_txEolInterruptMask &= ~(1 << q);
	if (qi->tqi_qflags & HAL_TXQ_TXURNINT_ENABLE)
		ahp->ah_txUrnInterruptMask |= 1 << q;
	else
		ahp->ah_txUrnInterruptMask &= ~(1 << q);

	return AH_TRUE;
}

/*
 * Get the TXDP for the "main" data queue.  Needs to be extended
 * for multiple Q functionality
 */
uint32_t
ar5210GetTxDP(struct ath_hal *ah, u_int q)
{
	struct ath_hal_5210 *ahp = AH5210(ah);
	HAL_TX_QUEUE_INFO *qi;

	HALASSERT(q < HAL_NUM_TX_QUEUES);

	qi = &ahp->ah_txq[q];
	switch (qi->tqi_type) {
	case HAL_TX_QUEUE_DATA:
		return OS_REG_READ(ah, AR_TXDP0);
	case HAL_TX_QUEUE_INACTIVE:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: inactive queue %u\n",
		    __func__, q);
		/* fall thru... */
	default:
		break;
	}
	return 0xffffffff;
}

/*
 * Set the TxDP for the "main" data queue.
 */
HAL_BOOL
ar5210SetTxDP(struct ath_hal *ah, u_int q, uint32_t txdp)
{
	struct ath_hal_5210 *ahp = AH5210(ah);
	HAL_TX_QUEUE_INFO *qi;

	HALASSERT(q < HAL_NUM_TX_QUEUES);

	HALDEBUG(ah, HAL_DEBUG_TXQUEUE, "%s: queue %u 0x%x\n",
	    __func__, q, txdp);
	qi = &ahp->ah_txq[q];
	switch (qi->tqi_type) {
	case HAL_TX_QUEUE_DATA:
#ifdef AH_DEBUG
		/*
		 * Make sure that TXE is deasserted before setting the
		 * TXDP.  If TXE is still asserted, setting TXDP will
		 * have no effect.
		 */
		if (OS_REG_READ(ah, AR_CR) & AR_CR_TXE0)
			ath_hal_printf(ah, "%s: TXE asserted; AR_CR=0x%x\n",
				__func__, OS_REG_READ(ah, AR_CR));
#endif
		OS_REG_WRITE(ah, AR_TXDP0, txdp);
		break;
	case HAL_TX_QUEUE_BEACON:
	case HAL_TX_QUEUE_CAB:
		OS_REG_WRITE(ah, AR_TXDP1, txdp);
		break;
	case HAL_TX_QUEUE_INACTIVE:
		HALDEBUG(ah, HAL_DEBUG_TXQUEUE, "%s: inactive queue %u\n",
		    __func__, q);
		/* fall thru... */
	default:
		return AH_FALSE;
	}
	return AH_TRUE;
}

/*
 * Update Tx FIFO trigger level.
 *
 * Set bIncTrigLevel to TRUE to increase the trigger level.
 * Set bIncTrigLevel to FALSE to decrease the trigger level.
 *
 * Returns TRUE if the trigger level was updated
 */
HAL_BOOL
ar5210UpdateTxTrigLevel(struct ath_hal *ah, HAL_BOOL bIncTrigLevel)
{
	uint32_t curTrigLevel;
	HAL_INT ints = ar5210GetInterrupts(ah);

	/*
	 * Disable chip interrupts. This is because halUpdateTxTrigLevel
	 * is called from both ISR and non-ISR contexts.
	 */
	(void) ar5210SetInterrupts(ah, ints &~ HAL_INT_GLOBAL);
	curTrigLevel = OS_REG_READ(ah, AR_TRIG_LEV);
	if (bIncTrigLevel){
		/* increase the trigger level */
		curTrigLevel = curTrigLevel +
			((MAX_TX_FIFO_THRESHOLD - curTrigLevel) / 2);
	} else {
		/* decrease the trigger level if not already at the minimum */
		if (curTrigLevel > MIN_TX_FIFO_THRESHOLD) {
			/* decrease the trigger level */
			curTrigLevel--;
		} else {
			/* no update to the trigger level */
			/* re-enable chip interrupts */
			ar5210SetInterrupts(ah, ints);
			return AH_FALSE;
		}
	}
	/* Update the trigger level */
	OS_REG_WRITE(ah, AR_TRIG_LEV, curTrigLevel);
	/* re-enable chip interrupts */
	ar5210SetInterrupts(ah, ints);
	return AH_TRUE;
}

/*
 * Set Transmit Enable bits for the specified queues.
 */
HAL_BOOL
ar5210StartTxDma(struct ath_hal *ah, u_int q)
{
	struct ath_hal_5210 *ahp = AH5210(ah);
	HAL_TX_QUEUE_INFO *qi;

	HALASSERT(q < HAL_NUM_TX_QUEUES);

	HALDEBUG(ah, HAL_DEBUG_TXQUEUE, "%s: queue %u\n", __func__, q);
	qi = &ahp->ah_txq[q];
	switch (qi->tqi_type) {
	case HAL_TX_QUEUE_DATA:
		OS_REG_WRITE(ah, AR_CR, AR_CR_TXE0);
		break;
	case HAL_TX_QUEUE_CAB:
		OS_REG_WRITE(ah, AR_CR, AR_CR_TXE1);	/* enable altq xmit */
		OS_REG_WRITE(ah, AR_BCR,
			AR_BCR_TQ1V | AR_BCR_BDMAE | AR_BCR_TQ1FV);
		break;
	case HAL_TX_QUEUE_BEACON:
		/* XXX add CR_BCR_BCMD if IBSS mode */
		OS_REG_WRITE(ah, AR_BCR, AR_BCR_TQ1V | AR_BCR_BDMAE);
		break;
	case HAL_TX_QUEUE_INACTIVE:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: inactive queue %u\n",
		    __func__, q);
		/* fal thru... */
	default:
		return AH_FALSE;
	}
	return AH_TRUE;
}

uint32_t
ar5210NumTxPending(struct ath_hal *ah, u_int q)
{
	struct ath_hal_5210 *ahp = AH5210(ah);
	HAL_TX_QUEUE_INFO *qi;
	uint32_t v;

	HALASSERT(q < HAL_NUM_TX_QUEUES);

	HALDEBUG(ah, HAL_DEBUG_TXQUEUE, "%s: queue %u\n", __func__, q);
	qi = &ahp->ah_txq[q];
	switch (qi->tqi_type) {
	case HAL_TX_QUEUE_DATA:
		v = OS_REG_READ(ah, AR_CFG);
		return MS(v, AR_CFG_TXCNT);
	case HAL_TX_QUEUE_INACTIVE:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: inactive queue %u\n",
		    __func__, q);
		/* fall thru... */
	default:
		break;
	}
	return 0;
}

/*
 * Stop transmit on the specified queue
 */
HAL_BOOL
ar5210StopTxDma(struct ath_hal *ah, u_int q)
{
	struct ath_hal_5210 *ahp = AH5210(ah);
	HAL_TX_QUEUE_INFO *qi;

	HALASSERT(q < HAL_NUM_TX_QUEUES);

	HALDEBUG(ah, HAL_DEBUG_TXQUEUE, "%s: queue %u\n", __func__, q);
	qi = &ahp->ah_txq[q];
	switch (qi->tqi_type) {
	case HAL_TX_QUEUE_DATA: {
		int i;
		OS_REG_WRITE(ah, AR_CR, AR_CR_TXD0);
		for (i = 0; i < 1000; i++) {
			if ((OS_REG_READ(ah, AR_CFG) & AR_CFG_TXCNT) == 0)
				break;
			OS_DELAY(10);
		}
		OS_REG_WRITE(ah, AR_CR, 0);
		return (i < 1000);
	}
	case HAL_TX_QUEUE_BEACON:
		return ath_hal_wait(ah, AR_BSR, AR_BSR_TXQ1F, 0);
	case HAL_TX_QUEUE_INACTIVE:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: inactive queue %u\n",
		    __func__, q);
		/* fall thru... */
	default:
		break;
	}
	return AH_FALSE;
}

/*
 * Descriptor Access Functions
 */

#define	VALID_PKT_TYPES \
	((1<<HAL_PKT_TYPE_NORMAL)|(1<<HAL_PKT_TYPE_ATIM)|\
	 (1<<HAL_PKT_TYPE_PSPOLL)|(1<<HAL_PKT_TYPE_PROBE_RESP)|\
	 (1<<HAL_PKT_TYPE_BEACON))
#define	isValidPktType(_t)	((1<<(_t)) & VALID_PKT_TYPES)
#define	VALID_TX_RATES \
	((1<<0x0b)|(1<<0x0f)|(1<<0x0a)|(1<<0x0e)|(1<<0x09)|(1<<0x0d)|\
	 (1<<0x08)|(1<<0x0c)|(1<<0x1b)|(1<<0x1a)|(1<<0x1e)|(1<<0x19)|\
	 (1<<0x1d)|(1<<0x18)|(1<<0x1c))
#define	isValidTxRate(_r)	((1<<(_r)) & VALID_TX_RATES)

HAL_BOOL
ar5210SetupTxDesc(struct ath_hal *ah, struct ath_desc *ds,
	u_int pktLen,
	u_int hdrLen,
	HAL_PKT_TYPE type,
	u_int txPower,
	u_int txRate0, u_int txTries0,
	u_int keyIx,
	u_int antMode,
	u_int flags,
	u_int rtsctsRate,
	u_int rtsctsDuration,
        u_int compicvLen,
	u_int compivLen,
	u_int comp)
{
	struct ar5210_desc *ads = AR5210DESC(ds);
	uint32_t frtype;

	(void) txPower;
	(void) rtsctsDuration;

	HALASSERT(txTries0 != 0);
	HALASSERT(isValidPktType(type));
	HALASSERT(isValidTxRate(txRate0));

	if (type == HAL_PKT_TYPE_BEACON || type == HAL_PKT_TYPE_PROBE_RESP)
		frtype = AR_Frm_NoDelay;
	else
		frtype = type << 26;
	ads->ds_ctl0 = (pktLen & AR_FrameLen)
		     | (txRate0 << AR_XmitRate_S)
		     | ((hdrLen << AR_HdrLen_S) & AR_HdrLen)
		     | frtype
		     | (flags & HAL_TXDESC_CLRDMASK ? AR_ClearDestMask : 0)
		     | (flags & HAL_TXDESC_INTREQ ? AR_TxInterReq : 0)
		     | (antMode ? AR_AntModeXmit : 0)
		     ;
	if (keyIx != HAL_TXKEYIX_INVALID) {
		ads->ds_ctl1 = (keyIx << AR_EncryptKeyIdx_S) & AR_EncryptKeyIdx;
		ads->ds_ctl0 |= AR_EncryptKeyValid;
	} else
		ads->ds_ctl1 = 0;
	if (flags & HAL_TXDESC_RTSENA) {
		ads->ds_ctl0 |= AR_RTSCTSEnable;
		ads->ds_ctl1 |= (rtsctsDuration << AR_RTSDuration_S)
		    & AR_RTSDuration;
	}
	return AH_TRUE;
}

HAL_BOOL
ar5210SetupXTxDesc(struct ath_hal *ah, struct ath_desc *ds,
	u_int txRate1, u_int txTries1,
	u_int txRate2, u_int txTries2,
	u_int txRate3, u_int txTries3)
{
	(void) ah; (void) ds;
	(void) txRate1; (void) txTries1;
	(void) txRate2; (void) txTries2;
	(void) txRate3; (void) txTries3;
	return AH_FALSE;
}

void
ar5210IntrReqTxDesc(struct ath_hal *ah, struct ath_desc *ds)
{
	struct ar5210_desc *ads = AR5210DESC(ds);

	ads->ds_ctl0 |= AR_TxInterReq;
}

HAL_BOOL
ar5210FillTxDesc(struct ath_hal *ah, struct ath_desc *ds,
	HAL_DMA_ADDR *bufAddrList, uint32_t *segLenList, u_int descId,
	u_int qcuId, HAL_BOOL firstSeg, HAL_BOOL lastSeg,
	const struct ath_desc *ds0)
{
	struct ar5210_desc *ads = AR5210DESC(ds);
	uint32_t segLen = segLenList[0];

	HALASSERT((segLen &~ AR_BufLen) == 0);

	ds->ds_data = bufAddrList[0];

	if (firstSeg) {
		/*
		 * First descriptor, don't clobber xmit control data
		 * setup by ar5210SetupTxDesc.
		 */
		ads->ds_ctl1 |= segLen | (lastSeg ? 0 : AR_More);
	} else if (lastSeg) {		/* !firstSeg && lastSeg */
		/*
		 * Last descriptor in a multi-descriptor frame,
		 * copy the transmit parameters from the first
		 * frame for processing on completion. 
		 */
		ads->ds_ctl0 = AR5210DESC_CONST(ds0)->ds_ctl0;
		ads->ds_ctl1 = segLen;
	} else {			/* !firstSeg && !lastSeg */
		/*
		 * Intermediate descriptor in a multi-descriptor frame.
		 */
		ads->ds_ctl0 = 0;
		ads->ds_ctl1 = segLen | AR_More;
	}
	ads->ds_status0 = ads->ds_status1 = 0;
	return AH_TRUE;
}

/*
 * Processing of HW TX descriptor.
 */
HAL_STATUS
ar5210ProcTxDesc(struct ath_hal *ah,
	struct ath_desc *ds, struct ath_tx_status *ts)
{
	struct ar5210_desc *ads = AR5210DESC(ds);

	if ((ads->ds_status1 & AR_Done) == 0)
		return HAL_EINPROGRESS;

	/* Update software copies of the HW status */
	ts->ts_seqnum = ads->ds_status1 & AR_SeqNum;
	ts->ts_tstamp = MS(ads->ds_status0, AR_SendTimestamp);
	ts->ts_status = 0;
	if ((ads->ds_status0 & AR_FrmXmitOK) == 0) {
		if (ads->ds_status0 & AR_ExcessiveRetries)
			ts->ts_status |= HAL_TXERR_XRETRY;
		if (ads->ds_status0 & AR_Filtered)
			ts->ts_status |= HAL_TXERR_FILT;
		if (ads->ds_status0  & AR_FIFOUnderrun)
			ts->ts_status |= HAL_TXERR_FIFO;
	}
	ts->ts_rate = MS(ads->ds_ctl0, AR_XmitRate);
	ts->ts_rssi = MS(ads->ds_status1, AR_AckSigStrength);
	ts->ts_shortretry = MS(ads->ds_status0, AR_ShortRetryCnt);
	ts->ts_longretry = MS(ads->ds_status0, AR_LongRetryCnt);
	ts->ts_antenna = 0;		/* NB: don't know */
	ts->ts_finaltsi = 0;

	return HAL_OK;
}

/*
 * Determine which tx queues need interrupt servicing.
 * STUB.
 */
void
ar5210GetTxIntrQueue(struct ath_hal *ah, uint32_t *txqs)
{
	return;
}

/*
 * Retrieve the rate table from the given TX completion descriptor
 */
HAL_BOOL
ar5210GetTxCompletionRates(struct ath_hal *ah, const struct ath_desc *ds0, int *rates, int *tries)
{
	return AH_FALSE;
}

/*
 * Set the TX descriptor link pointer
 */
void
ar5210SetTxDescLink(struct ath_hal *ah, void *ds, uint32_t link)
{
	struct ar5210_desc *ads = AR5210DESC(ds);

	ads->ds_link = link;
}

/*
 * Get the TX descriptor link pointer
 */
void
ar5210GetTxDescLink(struct ath_hal *ah, void *ds, uint32_t *link)
{
	struct ar5210_desc *ads = AR5210DESC(ds);

	*link = ads->ds_link;
}

/*
 * Get a pointer to the TX descriptor link pointer
 */
void
ar5210GetTxDescLinkPtr(struct ath_hal *ah, void *ds, uint32_t **linkptr)
{
	struct ar5210_desc *ads = AR5210DESC(ds);

	*linkptr = &ads->ds_link;
}

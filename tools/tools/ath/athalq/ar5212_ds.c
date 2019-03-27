/*
 * Copyright (c) 2012 Adrian Chadd <adrian@FreeBSD.org>
 * All Rights Reserved.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include <sys/types.h>
#include <sys/alq.h>
#include <sys/endian.h>

#include <dev/ath/if_ath_alq.h>
#include <dev/ath/ath_hal/ar5212/ar5212desc.h>

#include "ar5212_ds.h"

#define	MS(_v, _f)	( ((_v) & (_f)) >> _f##_S )
#define	MF(_v, _f) ( !! ((_v) & (_f)))

static void
ar5212_decode_txstatus(struct if_ath_alq_payload *a)
{
	struct ar5212_desc txs;

	/* XXX assumes txs is smaller than PAYLOAD_LEN! */
	memcpy(&txs, &a->payload, sizeof(struct ar5212_desc));

	printf("[%u.%06u] [%llu] TXSTATUS: TxDone=%d, TS=0x%08x\n\n",
	    (unsigned int) be32toh(a->hdr.tstamp_sec),
	    (unsigned int) be32toh(a->hdr.tstamp_usec),
	    (unsigned long long) be64toh(a->hdr.threadid),
	    MF(txs.u.tx.status1, AR_Done),
	    MS(txs.u.tx.status0, AR_SendTimestamp));

	/* ds_txstatus0 */
	printf("    Frmok=%d, xretries=%d, fifounderrun=%d, filt=%d\n",
	    MF(txs.u.tx.status0, AR_FrmXmitOK),
	    MF(txs.u.tx.status0, AR_ExcessiveRetries),
	    MF(txs.u.tx.status0, AR_FIFOUnderrun),
	    MF(txs.u.tx.status0, AR_Filtered));
	printf("    RTScnt=%d, FailCnt=%d, VCollCnt=%d\n",
	    MS(txs.u.tx.status0, AR_RTSFailCnt),
	    MS(txs.u.tx.status0, AR_DataFailCnt),
	    MS(txs.u.tx.status0, AR_VirtCollCnt));
	printf("    SndTimestamp=0x%04x\n",
	    MS(txs.u.tx.status0, AR_SendTimestamp));

	/* ds_txstatus1 */
	printf("    Done=%d, SeqNum=0x%04x, AckRSSI=%d, FinalTSI=%d\n",
	    MF(txs.u.tx.status1, AR_Done),
	    MS(txs.u.tx.status1, AR_SeqNum),
	    MS(txs.u.tx.status1, AR_AckSigStrength),
	    MS(txs.u.tx.status1, AR_FinalTSIndex));
	printf("    CompSuccess=%d, XmitAntenna=%d\n",
	    MF(txs.u.tx.status1, AR_CompSuccess),
	    MF(txs.u.tx.status1, AR_XmitAtenna));

	printf("\n ------\n");
}

static void
ar5212_decode_txdesc(struct if_ath_alq_payload *a)
{
	struct ar5212_desc txc;

	/* XXX assumes txs is smaller than PAYLOAD_LEN! */
	memcpy(&txc, &a->payload, sizeof(struct ar5212_desc));

	printf("[%u.%06u] [%llu] TXD\n",
	    (unsigned int) be32toh(a->hdr.tstamp_sec),
	    (unsigned int) be32toh(a->hdr.tstamp_usec),
	    (unsigned long long) be64toh(a->hdr.threadid));

	printf("  link=0x%08x, data=0x%08x\n",
	    txc.ds_link,
	    txc.ds_data);

	/* ds_ctl0 */
	printf("    Frame Len=%d\n", txc.ds_ctl0 & AR_FrameLen);
	printf("    TX power0=%d, RtsEna=%d, Veol=%d, ClrDstMask=%d AntModeXmit=0x%02x\n",
	    MS(txc.ds_ctl0, AR_XmitPower),
	    MF(txc.ds_ctl0, AR_RTSCTSEnable),
	    MF(txc.ds_ctl0, AR_VEOL),
	    MF(txc.ds_ctl0, AR_ClearDestMask),
	    MF(txc.ds_ctl0, AR_AntModeXmit));
	printf("    TxIntrReq=%d, DestIdxValid=%d, CtsEnable=%d\n",
	    MF(txc.ds_ctl0, AR_TxInterReq),
	    MF(txc.ds_ctl0, AR_DestIdxValid),
	    MF(txc.ds_ctl0, AR_CTSEnable));

	/* ds_ctl1 */
	printf("    BufLen=%d, TxMore=%d, DestIdx=%d,"
	    " FrType=0x%x\n",
	    txc.ds_ctl1 & AR_BufLen,
	    MF(txc.ds_ctl1, AR_More),
	    MS(txc.ds_ctl1, AR_DestIdx),
	    MS(txc.ds_ctl1, AR_FrmType));
	printf("    NoAck=%d, CompProc=%d, CompIVLen=%d, CompICVLen=%d\n",
	    MF(txc.ds_ctl1, AR_NoAck),
	    MS(txc.ds_ctl1, AR_CompProc),
	    MS(txc.ds_ctl1, AR_CompIVLen),
	    MS(txc.ds_ctl1, AR_CompICVLen));

	/* ds_ctl2 */
	printf("    DurUpEna=%d, Burstdur=0x%04x\n",
	    MF(txc.ds_ctl2, AR_DurUpdateEna),
	    MS(txc.ds_ctl2, AR_RTSCTSDuration));
	printf("    Try0=%d, Try1=%d, Try2=%d, Try3=%d\n",
	    MS(txc.ds_ctl2, AR_XmitDataTries0),
	    MS(txc.ds_ctl2, AR_XmitDataTries1),
	    MS(txc.ds_ctl2, AR_XmitDataTries2),
	    MS(txc.ds_ctl2, AR_XmitDataTries3));

	/* ds_ctl3 */
	printf("    rate0=0x%02x, rate1=0x%02x, rate2=0x%02x, rate3=0x%02x\n",
	    MS(txc.ds_ctl3, AR_XmitRate0),
	    MS(txc.ds_ctl3, AR_XmitRate1),
	    MS(txc.ds_ctl3, AR_XmitRate2),
	    MS(txc.ds_ctl3, AR_XmitRate3));
	printf("    RtsCtsRate=0x%02x\n",
	    MS(txc.ds_ctl3, AR_RTSCTSRate));

	printf("\n ------ \n");
}

static void
ar5212_decode_rxstatus(struct if_ath_alq_payload *a)
{
	struct ar5212_desc rxs;

	/* XXX assumes rxs is smaller than PAYLOAD_LEN! */
	memcpy(&rxs, &a->payload, sizeof(struct ar5212_desc));

	printf("[%u.%06u] [%llu] RXSTATUS: RxOK=%d TS=0x%08x\n",
	    (unsigned int) be32toh(a->hdr.tstamp_sec),
	    (unsigned int) be32toh(a->hdr.tstamp_usec),
	    (unsigned long long) be64toh(a->hdr.threadid),
	    MF(rxs.ds_rxstatus1, AR_Done),
	    MS(rxs.ds_rxstatus1, AR_RcvTimestamp));

	printf("  link=0x%08x, data=0x%08x, ctl0=0x%08x, ctl2=0x%08x\n",
	    rxs.ds_link,
	    rxs.ds_data,
	    rxs.ds_ctl0,
	    rxs.ds_ctl1);

	/* ds_rxstatus0 */
	printf("  DataLen=%d, ArMore=%d, DecompCrcError=%d, RcvRate=0x%02x\n",
	    rxs.ds_rxstatus0 & AR_DataLen,
	    MF(rxs.ds_rxstatus0, AR_More),
	    MF(rxs.ds_rxstatus0, AR_DecompCRCErr),
	    MS(rxs.ds_rxstatus0, AR_RcvRate));
	printf("  RSSI=%d, RcvAntenna=0x%x\n",
	    MS(rxs.ds_rxstatus0, AR_RcvSigStrength),
	    MS(rxs.ds_rxstatus0, AR_RcvAntenna));

	/* ds_rxstatus1 */
	printf("  RxDone=%d, RxFrameOk=%d, CrcErr=%d, DecryptCrcErr=%d\n",
	    MF(rxs.ds_rxstatus1, AR_Done),
	    MF(rxs.ds_rxstatus1, AR_FrmRcvOK),
	    MF(rxs.ds_rxstatus1, AR_CRCErr),
	    MF(rxs.ds_rxstatus1, AR_DecryptCRCErr));
	printf("  PhyErr=%d, MichaelErr=%d, KeyIdxValid=%d\n",
	    MF(rxs.ds_rxstatus1, AR_PHYErr),
	    MF(rxs.ds_rxstatus1, AR_MichaelErr),
	    MF(rxs.ds_rxstatus1, AR_KeyIdxValid));

	/* If PHY error, print that out. Otherwise, the key index */
	if (MF(rxs.ds_rxstatus1, AR_PHYErr))
		printf("  PhyErrCode=0x%02x\n",
		    MS(rxs.ds_rxstatus1, AR_PHYErrCode));
	else
		printf("  KeyIdx=0x%02x\n",
		    MS(rxs.ds_rxstatus1, AR_KeyIdx));

	printf("  KeyMiss=%d\n",
	    MF(rxs.ds_rxstatus1, AR_KeyCacheMiss));

	printf("  Timetamp: 0x%05x\n",
	    MS(rxs.ds_rxstatus1, AR_RcvTimestamp));

	printf("\n ------\n");
}

void
ar5212_alq_payload(struct if_ath_alq_payload *a)
{

		switch (be16toh(a->hdr.op)) {
			case ATH_ALQ_EDMA_TXSTATUS:	/* TXSTATUS */
				ar5212_decode_txstatus(a);
				break;
			case ATH_ALQ_EDMA_RXSTATUS:	/* RXSTATUS */
				ar5212_decode_rxstatus(a);
				break;
			case ATH_ALQ_EDMA_TXDESC:	/* TXDESC */
				ar5212_decode_txdesc(a);
				break;
			default:
				printf("[%d.%06d] [%lld] op: %d; len %d\n",
				    be32toh(a->hdr.tstamp_sec),
				    be32toh(a->hdr.tstamp_usec),
				    be64toh(a->hdr.threadid),
				    be16toh(a->hdr.op), be16toh(a->hdr.len));
		}
}

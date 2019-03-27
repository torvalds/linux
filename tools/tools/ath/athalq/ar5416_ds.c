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
#include <dev/ath/ath_hal/ar5416/ar5416desc.h>

#include "ar5416_ds.h"

#define	MS(_v, _f)	( ((_v) & (_f)) >> _f##_S )
#define	MF(_v, _f) ( !! ((_v) & (_f)))

static void
ar5416_decode_txstatus(struct if_ath_alq_payload *a)
{
	struct ar5416_desc txs;

	/* XXX assumes txs is smaller than PAYLOAD_LEN! */
	memcpy(&txs, &a->payload, sizeof(struct ar5416_desc));

	printf("[%u.%06u] [%llu] TXSTATUS: TxDone=%d, FrmOk=%d, filt=%d, TS=0x%08x\n",
	    (unsigned int) be32toh(a->hdr.tstamp_sec),
	    (unsigned int) be32toh(a->hdr.tstamp_usec),
	    (unsigned long long) be64toh(a->hdr.threadid),
	    MF(txs.u.tx.status[9], AR_TxDone),
	    MF(txs.u.tx.status[1], AR_FrmXmitOK),
	    MF(txs.u.tx.status[1], AR_Filtered),
	    txs.u.tx.status[2]);

	/* ds_txstatus0 */
	printf("    RX RSSI 0 [%d %d %d]",
	    MS(txs.u.tx.status[0], AR_TxRSSIAnt00),
	    MS(txs.u.tx.status[0], AR_TxRSSIAnt01),
	    MS(txs.u.tx.status[0], AR_TxRSSIAnt02));

	/* ds_txstatus5 */
	printf(" RX RSSI 1 [%d %d %d] Comb=%d\n",
	    MS(txs.u.tx.status[5], AR_TxRSSIAnt10),
	    MS(txs.u.tx.status[5], AR_TxRSSIAnt11),
	    MS(txs.u.tx.status[5], AR_TxRSSIAnt12),
	    MS(txs.u.tx.status[5], AR_TxRSSICombined));

	/* ds_txstatus0 */
	printf("    BA Valid=%d",
	    MF(txs.u.tx.status[0], AR_TxBaStatus));

	/* ds_txstatus1 */
	printf(", Frmok=%d, xretries=%d, fifounderrun=%d, filt=%d\n",
	    MF(txs.u.tx.status[1], AR_FrmXmitOK),
	    MF(txs.u.tx.status[1], AR_ExcessiveRetries),
	    MF(txs.u.tx.status[1], AR_FIFOUnderrun),
	    MF(txs.u.tx.status[1], AR_Filtered));
	printf("    DelimUnderrun=%d, DataUnderun=%d, DescCfgErr=%d,"
	    " TxTimerExceeded=%d\n",
	    MF(txs.u.tx.status[1], AR_TxDelimUnderrun),
	    MF(txs.u.tx.status[1], AR_TxDataUnderrun),
	    MF(txs.u.tx.status[1], AR_DescCfgErr),
	    MF(txs.u.tx.status[1], AR_TxTimerExpired));

	printf("    RTScnt=%d, FailCnt=%d, VRetryCnt=%d\n",
	    MS(txs.u.tx.status[1], AR_RTSFailCnt),
	    MS(txs.u.tx.status[1], AR_DataFailCnt),
	    MS(txs.u.tx.status[1], AR_VirtRetryCnt));

	/* ds_txstatus2 */
	printf("    TxTimestamp=0x%08x", txs.u.tx.status[2]);

	/* ds_txstatus3 */
	/* ds_txstatus4 */
	printf(", BALow=0x%08x", txs.u.tx.status[3]);
	printf(", BAHigh=0x%08x\n", txs.u.tx.status[4]);


	/* ds_txstatus6 */
	/* ds_txstatus7 */
	/* ds_txstatus8 */
	printf("    TxEVM[0]=0x%08x, TxEVM[1]=0x%08x, TxEVM[2]=0x%08x\n",
	    txs.u.tx.status[6],
	    txs.u.tx.status[7],
	    txs.u.tx.status[8]);

	/* ds_txstatus9 */
	printf("    TxDone=%d, SeqNum=0x%04x, TxOpExceeded=%d, FinalTsIdx=%d\n",
	    MF(txs.u.tx.status[9], AR_TxDone),
	    MS(txs.u.tx.status[9], AR_SeqNum),
	    MF(txs.u.tx.status[9], AR_TxOpExceeded),
	    MS(txs.u.tx.status[9], AR_FinalTxIdx));
	printf("    PowerMgmt=%d, TxTid=%d\n",
	    MF(txs.u.tx.status[9], AR_PowerMgmt),
	    MS(txs.u.tx.status[9], AR_TxTid));

	printf("\n ------\n");
}

static void
ar5416_decode_txdesc(struct if_ath_alq_payload *a)
{
	struct ar5416_desc txc;

	/* XXX assumes txs is smaller than PAYLOAD_LEN! */
	memcpy(&txc, &a->payload, sizeof(struct ar5416_desc));

	printf("[%u.%06u] [%llu] TXD\n",
	    (unsigned int) be32toh(a->hdr.tstamp_sec),
	    (unsigned int) be32toh(a->hdr.tstamp_usec),
	    (unsigned long long) be64toh(a->hdr.threadid));

	printf("  link=0x%08x, data=0x%08x\n",
	    txc.ds_link,
	    txc.ds_data);

	/* ds_ctl0 */
	printf("    Frame Len=%d, VMF=%d\n",
	     txc.ds_ctl0 & AR_FrameLen,
	    MF(txc.ds_ctl0, AR_VirtMoreFrag));
	printf("    TX power0=%d, RtsEna=%d, Veol=%d, ClrDstMask=%d\n",
	    MS(txc.ds_ctl0, AR_XmitPower),
	    MF(txc.ds_ctl0, AR_RTSEnable),
	    MF(txc.ds_ctl0, AR_VEOL),
	    MF(txc.ds_ctl0, AR_ClrDestMask));
	printf("    TxIntrReq=%d, DestIdxValid=%d, CtsEnable=%d\n",
	    MF(txc.ds_ctl0, AR_TxIntrReq),
	    MF(txc.ds_ctl0, AR_DestIdxValid),
	    MF(txc.ds_ctl0, AR_CTSEnable));

	/* ds_ctl1 */
	printf("    BufLen=%d, TxMore=%d, DestIdx=%d,"
	    " FrType=0x%x\n",
	    txc.ds_ctl1 & AR_BufLen,
	    MF(txc.ds_ctl1, AR_TxMore),
	    MS(txc.ds_ctl1, AR_DestIdx),
	    MS(txc.ds_ctl1, AR_FrameType));
	printf("    NoAck=%d, InsertTs=%d, CorruptFcs=%d, ExtOnly=%d,"
	    " ExtAndCtl=%d\n",
	    MF(txc.ds_ctl1, AR_NoAck),
	    MF(txc.ds_ctl1, AR_InsertTS),
	    MF(txc.ds_ctl1, AR_CorruptFCS),
	    MF(txc.ds_ctl1, AR_ExtOnly),
	    MF(txc.ds_ctl1, AR_ExtAndCtl));
	printf("    MoreAggr=%d, IsAggr=%d, MoreRifs=%d\n",
	    MF(txc.ds_ctl1, AR_MoreAggr),
	    MF(txc.ds_ctl1, AR_IsAggr),
	    MF(txc.ds_ctl1, AR_MoreRifs));

	/* ds_ctl2 */
	printf("    DurUpEna=%d, Burstdur=0x%04x\n",
	    MF(txc.ds_ctl2, AR_DurUpdateEn),
	    MS(txc.ds_ctl2, AR_BurstDur));
	printf("    Try0=%d, Try1=%d, Try2=%d, Try3=%d\n",
	    MS(txc.ds_ctl2, AR_XmitDataTries0),
	    MS(txc.ds_ctl2, AR_XmitDataTries1),
	    MS(txc.ds_ctl2, AR_XmitDataTries2),
	    MS(txc.ds_ctl2, AR_XmitDataTries3));

	/* ds_ctl3, 4 */
	printf("    try 0: Rate=0x%02x, PktDur=%d, RTS/CTS ena=%d\n",
	    MS(txc.ds_ctl3, AR_XmitRate0),
	    MS(txc.ds_ctl4, AR_PacketDur0),
	    MF(txc.ds_ctl4, AR_RTSCTSQual0));
	printf("    try 1: Rate=0x%02x, PktDur=%d, RTS/CTS ena=%d\n",
	    MS(txc.ds_ctl3, AR_XmitRate1),
	    MS(txc.ds_ctl4, AR_PacketDur1),
	    MF(txc.ds_ctl4, AR_RTSCTSQual1));

	/* ds_ctl3, 5 */
	printf("    try 2: Rate=0x%02x, PktDur=%d, RTS/CTS ena=%d\n",
	    MS(txc.ds_ctl3, AR_XmitRate2),
	    MS(txc.ds_ctl5, AR_PacketDur2),
	    MF(txc.ds_ctl5, AR_RTSCTSQual2));
	printf("    try 3: Rate=0x%02x, PktDur=%d, RTS/CTS ena=%d\n",
	    MS(txc.ds_ctl3, AR_XmitRate3),
	    MS(txc.ds_ctl5, AR_PacketDur3),
	    MF(txc.ds_ctl5, AR_RTSCTSQual3));

	/* ds_ctl6 */
	printf("    AggrLen=%d, PadDelim=%d, EncrType=%d\n",
	    MS(txc.ds_ctl6, AR_AggrLen),
	    MS(txc.ds_ctl6, AR_PadDelim),
	    MS(txc.ds_ctl6, AR_EncrType));

	/* ds_ctl7 */
	printf("    try 0: chainMask=0x%x, GI=%d, 2040=%d, STBC=%d\n",
	    MS(txc.ds_ctl7, AR_ChainSel0),
	    MF(txc.ds_ctl7, AR_GI0),
	    MF(txc.ds_ctl7, AR_2040_0),
	    MF(txc.ds_ctl7, AR_STBC0));
	printf("    try 1: chainMask=0x%x, GI=%d, 2040=%d, STBC=%d\n",
	    MS(txc.ds_ctl7, AR_ChainSel1),
	    MF(txc.ds_ctl7, AR_GI1),
	    MF(txc.ds_ctl7, AR_2040_1),
	    MF(txc.ds_ctl7, AR_STBC1));
	printf("    try 2: chainMask=0x%x, GI=%d, 2040=%d, STBC=%d\n",
	    MS(txc.ds_ctl7, AR_ChainSel2),
	    MF(txc.ds_ctl7, AR_GI2),
	    MF(txc.ds_ctl7, AR_2040_2),
	    MF(txc.ds_ctl7, AR_STBC2));
	printf("    try 3: chainMask=0x%x, GI=%d, 2040=%d, STBC=%d\n",
	    MS(txc.ds_ctl7, AR_ChainSel3),
	    MF(txc.ds_ctl7, AR_GI3),
	    MF(txc.ds_ctl7, AR_2040_3),
	    MF(txc.ds_ctl7, AR_STBC3));

	printf("    RTSCtsRate=0x%02x\n", MS(txc.ds_ctl7, AR_RTSCTSRate));

	/* ds_ctl8 */
	printf("    try 0: ant=0x%08x\n", txc.ds_ctl8 &  AR_AntCtl0);

	/* ds_ctl9 */
	printf("    try 1: TxPower=%d, ant=0x%08x\n",
	    MS(txc.ds_ctl9, AR_XmitPower1),
	    txc.ds_ctl9 & AR_AntCtl1);

	/* ds_ctl10 */
	printf("    try 2: TxPower=%d, ant=0x%08x\n",
	    MS(txc.ds_ctl10, AR_XmitPower2),
	    txc.ds_ctl10 & AR_AntCtl2);

	/* ds_ctl11 */
	printf("    try 3: TxPower=%d, ant=0x%08x\n",
	    MS(txc.ds_ctl11, AR_XmitPower3),
	    txc.ds_ctl11 & AR_AntCtl3);

	printf("\n ------ \n");
}

static void
ar5416_decode_rxstatus(struct if_ath_alq_payload *a)
{
	struct ar5416_desc rxs;

	/* XXX assumes rxs is smaller than PAYLOAD_LEN! */
	memcpy(&rxs, &a->payload, sizeof(struct ar5416_desc));

	printf("[%u.%06u] [%llu] RXSTATUS: RxDone=%d, RxRate=0x%02x, TS=0x%08x\n",
	    (unsigned int) be32toh(a->hdr.tstamp_sec),
	    (unsigned int) be32toh(a->hdr.tstamp_usec),
	    (unsigned long long) be64toh(a->hdr.threadid),
	    MF(rxs.ds_rxstatus8, AR_RxDone),
	    MS(rxs.ds_rxstatus0, AR_RxRate),
	    rxs.ds_rxstatus2);

	printf("  link=0x%08x, data=0x%08x, ctl0=0x%08x, ctl2=0x%08x\n",
	    rxs.ds_link,
	    rxs.ds_data,
	    rxs.ds_ctl0,
	    rxs.ds_ctl1);

	/* status0 */
	/*
	 * XXX TODO: For AR9285, the chain 1 and chain 2 RSSI values
	 * acutally contain the RX mixer configuration
	 */
	printf("  RSSICtl[0]=%d, RSSICtl[1]=%d, RSSICtl[2]=%d\n",
	    MS(rxs.ds_rxstatus0, AR_RxRSSIAnt00),
	    MS(rxs.ds_rxstatus0, AR_RxRSSIAnt01),
	    MS(rxs.ds_rxstatus0, AR_RxRSSIAnt02));

	/* status4 */
	printf("  RSSIExt[0]=%d, RSSIExt[1]=%d, RSSIExt[2]=%d, RSSIComb=%d\n",
	    MS(rxs.ds_rxstatus4, AR_RxRSSIAnt10),
	    MS(rxs.ds_rxstatus4, AR_RxRSSIAnt11),
	    MS(rxs.ds_rxstatus4, AR_RxRSSIAnt12),
	    MS(rxs.ds_rxstatus4, AR_RxRSSICombined));

	/* status2 */
	printf("  RxTimestamp=0x%08x,", rxs.ds_rxstatus2);

	/* status1 */
	printf(" DataLen=%d, RxMore=%d, NumDelim=%d\n",
	    rxs.ds_rxstatus1 & AR_DataLen,
	    MF(rxs.ds_rxstatus1, AR_RxMore),
	    MS(rxs.ds_rxstatus1, AR_NumDelim));

	/* status3 - RxRate however is for Owl 2.0 */
	printf("  GI=%d, 2040=%d, RxRate=0x%02x, DupFrame=%d, RxAnt=0x%08x\n",
	    MF(rxs.ds_rxstatus3, AR_GI),
	    MF(rxs.ds_rxstatus3, AR_2040),
	    MS(rxs.ds_rxstatus0, AR_RxRate),
	    MF(rxs.ds_rxstatus3, AR_DupFrame),
	    MS(rxs.ds_rxstatus3, AR_RxAntenna));

	/* status5 */
	/* status6 */
	/* status7 */
	printf("  RxEvm0=0x%08x, RxEvm1=0x%08x, RxEvm2=0x%08x\n",
	    rxs.ds_rxstatus5,
	    rxs.ds_rxstatus6,
	    rxs.ds_rxstatus7);
	
	/* status8 */
	printf("  RxDone=%d, RxFrameOk=%d, CrcErr=%d, DecryptCrcErr=%d\n",
	    MF(rxs.ds_rxstatus8, AR_RxDone),
	    MF(rxs.ds_rxstatus8, AR_RxFrameOK),
	    MF(rxs.ds_rxstatus8, AR_CRCErr),
	    MF(rxs.ds_rxstatus8, AR_DecryptCRCErr));
	printf("  PhyErr=%d, MichaelErr=%d, PreDelimCRCErr=%d, KeyIdxValid=%d\n",
	    MF(rxs.ds_rxstatus8, AR_PHYErr),
	    MF(rxs.ds_rxstatus8, AR_MichaelErr),
	    MF(rxs.ds_rxstatus8, AR_PreDelimCRCErr),
	    MF(rxs.ds_rxstatus8, AR_RxKeyIdxValid));

	printf("  RxMoreAggr=%d, RxAggr=%d, PostDelimCRCErr=%d, HiRxChain=%d\n",
	    MF(rxs.ds_rxstatus8, AR_RxMoreAggr),
	    MF(rxs.ds_rxstatus8, AR_RxAggr),
	    MF(rxs.ds_rxstatus8, AR_PostDelimCRCErr),
	    MF(rxs.ds_rxstatus8, AR_HiRxChain));

	/* If PHY error, print that out. Otherwise, the key index */
	if (MF(rxs.ds_rxstatus8, AR_PHYErr))
		printf("  PhyErrCode=0x%02x",
		    MS(rxs.ds_rxstatus8, AR_PHYErrCode));
	else
		printf("  KeyIdx=0x%02x",
		    MS(rxs.ds_rxstatus8, AR_KeyIdx));
	printf(", KeyMiss=%d\n",
	    MF(rxs.ds_rxstatus8, AR_KeyMiss));

	printf("\n ------\n");
}

void
ar5416_alq_payload(struct if_ath_alq_payload *a)
{

		switch (be16toh(a->hdr.op)) {
			case ATH_ALQ_EDMA_TXSTATUS:	/* TXSTATUS */
				ar5416_decode_txstatus(a);
				break;
			case ATH_ALQ_EDMA_RXSTATUS:	/* RXSTATUS */
				ar5416_decode_rxstatus(a);
				break;
			case ATH_ALQ_EDMA_TXDESC:	/* TXDESC */
				ar5416_decode_txdesc(a);
				break;
			default:
				printf("[%d.%06d] [%lld] op: %d; len %d\n",
				    be32toh(a->hdr.tstamp_sec),
				    be32toh(a->hdr.tstamp_usec),
				    be64toh(a->hdr.threadid),
				    be16toh(a->hdr.op), be16toh(a->hdr.len));
		}
}

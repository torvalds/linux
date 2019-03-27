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
#ifndef _ATH_AR5416_DESC_H_
#define _ATH_AR5416_DESC_H_

/*
 * Hardware-specific descriptor structures.
 */

/* XXX Need to replace this with a dynamic 
 * method of determining Owl2 if possible 
 */
#define _get_index(_ah) ( IS_5416V1(_ah)  ? -4 : 0 )
#define AR5416_DS_TXSTATUS(_ah, _ads) \
	((uint32_t*)(&(_ads)->u.tx.status[_get_index(_ah)]))
#define AR5416_DS_TXSTATUS_CONST(_ah, _ads) \
	((const uint32_t*)(&(_ads)->u.tx.status[_get_index(_ah)]))

#define AR5416_NUM_TX_STATUS	10 /* Number of TX status words */
/* Clear the whole descriptor */
#define AR5416_DESC_TX_CTL_SZ	sizeof(struct ar5416_tx_desc)

struct ar5416_tx_desc { /* tx desc has 12 control words + 10 status words */
	uint32_t	ctl2;
	uint32_t	ctl3;
	uint32_t	ctl4;
	uint32_t	ctl5;
	uint32_t	ctl6;
	uint32_t	ctl7;
	uint32_t	ctl8;
	uint32_t	ctl9;
	uint32_t	ctl10;
	uint32_t	ctl11;
	uint32_t	status[AR5416_NUM_TX_STATUS];
};

struct ar5416_rx_desc { /* rx desc has 2 control words + 9 status words */
	uint32_t	status0;
	uint32_t	status1;
	uint32_t	status2;
	uint32_t	status3;
	uint32_t	status4;
	uint32_t	status5;
	uint32_t	status6;
 	uint32_t	status7;
	uint32_t	status8;
};


struct ar5416_desc {
	uint32_t   ds_link;    /* link pointer */
	uint32_t   ds_data;    /* data buffer pointer */
	uint32_t   ds_ctl0;    /* DMA control 0 */
	uint32_t   ds_ctl1;    /* DMA control 1 */
	union {
		struct ar5416_tx_desc tx;
		struct ar5416_rx_desc rx;
	} u;
} __packed;
#define AR5416DESC(_ds) ((struct ar5416_desc *)(_ds))
#define AR5416DESC_CONST(_ds) ((const struct ar5416_desc *)(_ds))

#define ds_ctl2     u.tx.ctl2
#define ds_ctl3     u.tx.ctl3
#define ds_ctl4     u.tx.ctl4
#define ds_ctl5     u.tx.ctl5
#define ds_ctl6     u.tx.ctl6
#define ds_ctl7     u.tx.ctl7
#define ds_ctl8     u.tx.ctl8
#define ds_ctl9     u.tx.ctl9
#define ds_ctl10    u.tx.ctl10
#define ds_ctl11    u.tx.ctl11

#define ds_rxstatus0    u.rx.status0
#define ds_rxstatus1    u.rx.status1
#define ds_rxstatus2    u.rx.status2
#define ds_rxstatus3    u.rx.status3
#define ds_rxstatus4    u.rx.status4
#define ds_rxstatus5    u.rx.status5
#define ds_rxstatus6    u.rx.status6
#define ds_rxstatus7    u.rx.status7
#define ds_rxstatus8    u.rx.status8

/***********
 * TX Desc *
 ***********/

/* ds_ctl0 */
#define AR_FrameLen         0x00000fff
#define AR_VirtMoreFrag     0x00001000
#define AR_TxCtlRsvd00      0x0000e000
#define AR_XmitPower        0x003f0000
#define AR_XmitPower_S      16
#define AR_RTSEnable        0x00400000
#define AR_VEOL             0x00800000
#define AR_ClrDestMask      0x01000000
#define AR_TxCtlRsvd01      0x1e000000
#define AR_TxIntrReq        0x20000000
#define AR_DestIdxValid     0x40000000
#define AR_CTSEnable        0x80000000

/* ds_ctl1 */
#define AR_BufLen           0x00000fff
#define AR_TxMore           0x00001000
#define AR_DestIdx          0x000fe000
#define AR_DestIdx_S        13
#define AR_FrameType        0x00f00000
#define AR_FrameType_S      20
#define AR_NoAck            0x01000000
#define AR_InsertTS         0x02000000
#define AR_CorruptFCS       0x04000000
#define AR_ExtOnly          0x08000000
#define AR_ExtAndCtl        0x10000000
#define AR_MoreAggr         0x20000000
#define AR_IsAggr           0x40000000
#define AR_MoreRifs	    0x80000000

/* ds_ctl2 */
#define AR_BurstDur         0x00007fff
#define AR_BurstDur_S       0
#define AR_DurUpdateEn      0x00008000
#define AR_XmitDataTries0   0x000f0000
#define AR_XmitDataTries0_S 16
#define AR_XmitDataTries1   0x00f00000
#define AR_XmitDataTries1_S 20
#define AR_XmitDataTries2   0x0f000000
#define AR_XmitDataTries2_S 24
#define AR_XmitDataTries3   0xf0000000
#define AR_XmitDataTries3_S 28

/* ds_ctl3 */
#define AR_XmitRate0        0x000000ff
#define AR_XmitRate0_S      0
#define AR_XmitRate1        0x0000ff00
#define AR_XmitRate1_S      8
#define AR_XmitRate2        0x00ff0000
#define AR_XmitRate2_S      16
#define AR_XmitRate3        0xff000000
#define AR_XmitRate3_S      24

/* ds_ctl4 */
#define AR_PacketDur0       0x00007fff
#define AR_PacketDur0_S     0
#define AR_RTSCTSQual0      0x00008000
#define AR_PacketDur1       0x7fff0000
#define AR_PacketDur1_S     16
#define AR_RTSCTSQual1      0x80000000

/* ds_ctl5 */
#define AR_PacketDur2       0x00007fff
#define AR_PacketDur2_S     0
#define AR_RTSCTSQual2      0x00008000
#define AR_PacketDur3       0x7fff0000
#define AR_PacketDur3_S     16
#define AR_RTSCTSQual3      0x80000000

/* ds_ctl6 */
#define AR_AggrLen          0x0000ffff
#define AR_AggrLen_S        0
#define AR_TxCtlRsvd60      0x00030000
#define AR_PadDelim         0x03fc0000
#define AR_PadDelim_S       18
#define AR_EncrType         0x0c000000
#define AR_EncrType_S       26
#define AR_TxCtlRsvd61      0xf0000000

/* ds_ctl7 */
#define AR_2040_0           0x00000001
#define AR_GI0              0x00000002
#define AR_ChainSel0        0x0000001c
#define AR_ChainSel0_S      2
#define AR_2040_1           0x00000020
#define AR_GI1              0x00000040
#define AR_ChainSel1        0x00000380
#define AR_ChainSel1_S      7
#define AR_2040_2           0x00000400
#define AR_GI2              0x00000800
#define AR_ChainSel2        0x00007000
#define AR_ChainSel2_S      12
#define AR_2040_3           0x00008000
#define AR_GI3              0x00010000
#define AR_ChainSel3        0x000e0000
#define AR_ChainSel3_S      17
#define AR_RTSCTSRate       0x0ff00000
#define AR_RTSCTSRate_S     20
#define	AR_STBC0	    0x10000000
#define	AR_STBC1	    0x20000000
#define	AR_STBC2	    0x40000000
#define	AR_STBC3	    0x80000000

/* ds_ctl8 */
#define	AR_AntCtl0	    0x00ffffff
#define	AR_AntCtl0_S	    0
/* Xmit 0 TPC is AR_XmitPower in ctl0 */

/* ds_ctl9 */
#define	AR_AntCtl1	    0x00ffffff
#define	AR_AntCtl1_S	    0
#define	AR_XmitPower1	    0xff000000
#define	AR_XmitPower1_S	    24

/* ds_ctl10 */
#define	AR_AntCtl2	    0x00ffffff
#define	AR_AntCtl2_S	    0
#define	AR_XmitPower2	    0xff000000
#define	AR_XmitPower2_S	    24

/* ds_ctl11 */
#define	AR_AntCtl3	    0x00ffffff
#define	AR_AntCtl3_S	    0
#define	AR_XmitPower3	    0xff000000
#define	AR_XmitPower3_S	    24

/*************
 * TX Status *
 *************/

/* ds_status0 */
#define AR_TxRSSIAnt00      0x000000ff
#define AR_TxRSSIAnt00_S    0
#define AR_TxRSSIAnt01      0x0000ff00
#define AR_TxRSSIAnt01_S    8
#define AR_TxRSSIAnt02      0x00ff0000
#define AR_TxRSSIAnt02_S    16
#define AR_TxStatusRsvd00   0x3f000000
#define AR_TxBaStatus       0x40000000
#define AR_TxStatusRsvd01   0x80000000

/* ds_status1 */
#define AR_FrmXmitOK            0x00000001
#define AR_ExcessiveRetries     0x00000002
#define AR_FIFOUnderrun         0x00000004
#define AR_Filtered             0x00000008
#define AR_RTSFailCnt           0x000000f0
#define AR_RTSFailCnt_S         4
#define AR_DataFailCnt          0x00000f00
#define AR_DataFailCnt_S        8
#define AR_VirtRetryCnt         0x0000f000
#define AR_VirtRetryCnt_S       12
#define AR_TxDelimUnderrun      0x00010000
#define AR_TxDelimUnderrun_S    13
#define AR_TxDataUnderrun       0x00020000
#define AR_TxDataUnderrun_S     14
#define AR_DescCfgErr           0x00040000
#define AR_DescCfgErr_S         15
#define	AR_TxTimerExpired	0x00080000
#define AR_TxStatusRsvd10       0xfff00000

/* ds_status2 */
#define AR_SendTimestamp(_ptr)   (_ptr)[2]

/* ds_status3 */
#define AR_BaBitmapLow(_ptr)     (_ptr)[3]

/* ds_status4 */
#define AR_BaBitmapHigh(_ptr)    (_ptr)[4]

/* ds_status5 */
#define AR_TxRSSIAnt10      0x000000ff
#define AR_TxRSSIAnt10_S    0
#define AR_TxRSSIAnt11      0x0000ff00
#define AR_TxRSSIAnt11_S    8
#define AR_TxRSSIAnt12      0x00ff0000
#define AR_TxRSSIAnt12_S    16
#define AR_TxRSSICombined   0xff000000
#define AR_TxRSSICombined_S 24

/* ds_status6 */
#define AR_TxEVM0(_ptr)     (_ptr)[6]

/* ds_status7 */
#define AR_TxEVM1(_ptr)    (_ptr)[7]

/* ds_status8 */
#define AR_TxEVM2(_ptr)   (_ptr)[8]

/* ds_status9 */
#define AR_TxDone           0x00000001
#define AR_SeqNum           0x00001ffe
#define AR_SeqNum_S         1
#define AR_TxStatusRsvd80   0x0001e000
#define AR_TxOpExceeded     0x00020000
#define AR_TxStatusRsvd81   0x001c0000
#define AR_FinalTxIdx       0x00600000
#define AR_FinalTxIdx_S     21
#define AR_TxStatusRsvd82   0x01800000
#define AR_PowerMgmt        0x02000000
#define AR_TxTid            0xf0000000
#define AR_TxTid_S          28
#define AR_TxStatusRsvd83   0xfc000000

/***********
 * RX Desc *
 ***********/

/* ds_ctl0 */
#define AR_RxCTLRsvd00  0xffffffff

/* ds_ctl1 */
#define AR_BufLen       0x00000fff
#define AR_RxCtlRsvd00  0x00001000
#define AR_RxIntrReq    0x00002000
#define AR_RxCtlRsvd01  0xffffc000

/*************
 * Rx Status *
 *************/

/* ds_status0 */
#define AR_RxRSSIAnt00      0x000000ff
#define AR_RxRSSIAnt00_S    0
#define AR_RxRSSIAnt01      0x0000ff00
#define AR_RxRSSIAnt01_S    8
#define AR_RxRSSIAnt02      0x00ff0000
#define AR_RxRSSIAnt02_S    16
/* Rev specific */
/* Owl 1.x only */
#define AR_RxStatusRsvd00   0xff000000
/* Owl 2.x only */
#define AR_RxRate           0xff000000
#define AR_RxRate_S         24

/* ds_status1 */
#define AR_DataLen          0x00000fff
#define AR_RxMore           0x00001000
#define AR_NumDelim         0x003fc000
#define AR_NumDelim_S       14
#define AR_RxStatusRsvd10   0xff800000

/* ds_status2 */
#define AR_RcvTimestamp     ds_rxstatus2

/* ds_status3 */
#define AR_GI               0x00000001
#define AR_2040             0x00000002
/* Rev specific */
/* Owl 1.x only */
#define AR_RxRateV1         0x000003fc
#define AR_RxRateV1_S       2
#define AR_Parallel40       0x00000400
#define AR_RxStatusRsvd30   0xfffff800
/* Owl 2.x only */
#define AR_DupFrame	    0x00000004
#define AR_STBCFrame        0x00000008
#define AR_RxAntenna        0xffffff00
#define AR_RxAntenna_S      8

/* ds_status4 */
#define AR_RxRSSIAnt10            0x000000ff
#define AR_RxRSSIAnt10_S          0
#define AR_RxRSSIAnt11            0x0000ff00
#define AR_RxRSSIAnt11_S          8
#define AR_RxRSSIAnt12            0x00ff0000
#define AR_RxRSSIAnt12_S          16
#define AR_RxRSSICombined         0xff000000
#define AR_RxRSSICombined_S       24

/* ds_status5 */
#define AR_RxEVM0           ds_rxstatus5

/* ds_status6 */
#define AR_RxEVM1           ds_rxstatus6

/* ds_status7 */
#define AR_RxEVM2           ds_rxstatus7

/* ds_status8 */
#define AR_RxDone           0x00000001
#define AR_RxFrameOK        0x00000002
#define AR_CRCErr           0x00000004
#define AR_DecryptCRCErr    0x00000008
#define AR_PHYErr           0x00000010
#define AR_MichaelErr       0x00000020
#define AR_PreDelimCRCErr   0x00000040
#define AR_RxStatusRsvd70   0x00000080
#define AR_RxKeyIdxValid    0x00000100
#define AR_KeyIdx           0x0000fe00
#define AR_KeyIdx_S         9
#define AR_PHYErrCode       0x0000ff00
#define AR_PHYErrCode_S     8
#define AR_RxMoreAggr       0x00010000
#define AR_RxAggr           0x00020000
#define AR_PostDelimCRCErr  0x00040000
#define AR_RxStatusRsvd71   0x2ff80000
#define	AR_HiRxChain	    0x10000000
#define AR_DecryptBusyErr   0x40000000
#define AR_KeyMiss          0x80000000

#define TXCTL_OFFSET(ah)	2
#define TXCTL_NUMWORDS(ah)	(AR_SREV_5416_V20_OR_LATER(ah) ? 12 : 8)
#define TXSTATUS_OFFSET(ah)	(AR_SREV_5416_V20_OR_LATER(ah) ? 14 : 10)
#define TXSTATUS_NUMWORDS(ah)	10

#define RXCTL_OFFSET(ah)	3
#define RXCTL_NUMWORDS(ah)	1
#define RXSTATUS_OFFSET(ah)	4
#define RXSTATUS_NUMWORDS(ah)	9
#define RXSTATUS_RATE(ah, ads) \
	(AR_SREV_5416_V20_OR_LATER(ah) ? \
	 MS((ads)->ds_rxstatus0, AR_RxRate) : \
	 ((ads)->ds_rxstatus3 >> 2) & 0xFF)
#define RXSTATUS_DUPLICATE(ah, ads) \
	(AR_SREV_5416_V20_OR_LATER(ah) ?	\
	 MS((ads)->ds_rxstatus3, AR_Parallel40) : \
	 ((ads)->ds_rxstatus3 >> 10) & 0x1)
#endif /* _ATH_AR5416_DESC_H_ */

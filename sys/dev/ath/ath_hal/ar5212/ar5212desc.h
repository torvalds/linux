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
#ifndef _ATH_AR5212_DESC_H_
#define _ATH_AR5212_DESC_H_

/*
 * Hardware-specific descriptor structures.
 */

/*
 * AR5212-specific tx/rx descriptor definition.
 */
struct ar5212_desc {
	uint32_t	ds_link;	/* link pointer */
	uint32_t	ds_data;	/* data buffer pointer */
	uint32_t	ds_ctl0;	/* DMA control 0 */
	uint32_t	ds_ctl1;	/* DMA control 1 */
	union {
		struct {		/* xmit format */
			uint32_t	ctl2;	/* DMA control 2 */
			uint32_t	ctl3;	/* DMA control 3 */
			uint32_t	status0;/* DMA status 0 */
			uint32_t	status1;/* DMA status 1 */
		} tx;
		struct {		/* recv format */
			uint32_t	status0;/* DMA status 0 */
			uint32_t	status1;/* DMA status 1 */
		} rx;
	} u;
} __packed;
#define	AR5212DESC(_ds)	((struct ar5212_desc *)(_ds))
#define	AR5212DESC_CONST(_ds)	((const struct ar5212_desc *)(_ds))

#define	ds_ctl2		u.tx.ctl2
#define	ds_ctl3		u.tx.ctl3
#define	ds_txstatus0	u.tx.status0
#define	ds_txstatus1	u.tx.status1
#define	ds_rxstatus0	u.rx.status0
#define	ds_rxstatus1	u.rx.status1

/* TX ds_ctl0 */
#define	AR_FrameLen		0x00000fff	/* frame length */
/* bits 12-15 are reserved */
#define	AR_XmitPower		0x003f0000	/* transmit power control */
#define	AR_XmitPower_S		16
#define	AR_RTSCTSEnable		0x00400000	/* RTS/CTS protocol enable */
#define	AR_VEOL			0x00800000	/* virtual end-of-list */
#define	AR_ClearDestMask	0x01000000	/* Clear destination mask bit */
#define	AR_AntModeXmit		0x1e000000	/* TX antenna seslection */
#define	AR_AntModeXmit_S	25
#define	AR_TxInterReq		0x20000000	/* TX interrupt request */
#define	AR_DestIdxValid		0x40000000	/* destination index valid */
#define	AR_CTSEnable		0x80000000	/* precede frame with CTS */

/* TX ds_ctl1 */
#define	AR_BufLen		0x00000fff	/* data buffer length */
#define	AR_More			0x00001000	/* more desc in this frame */
#define	AR_DestIdx		0x000fe000	/* destination table index */
#define	AR_DestIdx_S		13
#define	AR_FrmType		0x00f00000	/* frame type indication */
#define	AR_FrmType_S		20
#define	AR_NoAck		0x01000000	/* No ACK flag */
#define	AR_CompProc		0x06000000	/* compression processing */
#define	AR_CompProc_S		25
#define	AR_CompIVLen		0x18000000	/* length of frame IV */
#define	AR_CompIVLen_S		27
#define	AR_CompICVLen		0x60000000	/* length of frame ICV */
#define	AR_CompICVLen_S		29
/* bit 31 is reserved */

/* TX ds_ctl2 */
#define	AR_RTSCTSDuration	0x00007fff	/* RTS/CTS duration */
#define	AR_RTSCTSDuration_S	0
#define	AR_DurUpdateEna		0x00008000	/* frame duration update ctl */
#define	AR_XmitDataTries0	0x000f0000	/* series 0 max attempts */
#define	AR_XmitDataTries0_S	16
#define	AR_XmitDataTries1	0x00f00000	/* series 1 max attempts */
#define	AR_XmitDataTries1_S	20
#define	AR_XmitDataTries2	0x0f000000	/* series 2 max attempts */
#define	AR_XmitDataTries2_S	24
#define	AR_XmitDataTries3	0xf0000000	/* series 3 max attempts */
#define	AR_XmitDataTries3_S	28

/* TX ds_ctl3 */
#define	AR_XmitRate0		0x0000001f	/* series 0 tx rate */
#define	AR_XmitRate0_S		0
#define	AR_XmitRate1		0x000003e0	/* series 1 tx rate */
#define	AR_XmitRate1_S		5
#define	AR_XmitRate2		0x00007c00	/* series 2 tx rate */
#define	AR_XmitRate2_S		10
#define	AR_XmitRate3		0x000f8000	/* series 3 tx rate */
#define	AR_XmitRate3_S		15
#define	AR_RTSCTSRate		0x01f00000	/* RTS or CTS rate */
#define	AR_RTSCTSRate_S		20
/* bits 25-31 are reserved */

/* RX ds_ctl1 */
/*	AR_BufLen		0x00000fff	   data buffer length */
/* bit 12 is reserved */
#define	AR_RxInterReq		0x00002000	/* RX interrupt request */
/* bits 14-31 are reserved */

/* TX ds_txstatus0 */
#define	AR_FrmXmitOK		0x00000001	/* TX success */
#define	AR_ExcessiveRetries	0x00000002	/* excessive retries */
#define	AR_FIFOUnderrun		0x00000004	/* TX FIFO underrun */
#define	AR_Filtered		0x00000008	/* TX filter indication */
#define	AR_RTSFailCnt		0x000000f0	/* RTS failure count */
#define	AR_RTSFailCnt_S		4
#define	AR_DataFailCnt		0x00000f00	/* Data failure count */
#define	AR_DataFailCnt_S	8
#define	AR_VirtCollCnt		0x0000f000	/* virtual collision count */
#define	AR_VirtCollCnt_S	12
#define	AR_SendTimestamp	0xffff0000	/* TX timestamp */
#define	AR_SendTimestamp_S	16

/* RX ds_rxstatus0 */
#define	AR_DataLen		0x00000fff	/* RX data length */
/*	AR_More			0x00001000	   more desc in this frame */
#define	AR_DecompCRCErr		0x00002000	/* decompression CRC error */
/* bit 14 is reserved */
#define	AR_RcvRate		0x000f8000	/* reception rate */
#define	AR_RcvRate_S		15
#define	AR_RcvSigStrength	0x0ff00000	/* receive signal strength */
#define	AR_RcvSigStrength_S	20
#define	AR_RcvAntenna		0xf0000000	/* receive antenaa */
#define	AR_RcvAntenna_S		28

/* TX ds_txstatus1 */
#define	AR_Done			0x00000001	/* descripter complete */
#define	AR_SeqNum		0x00001ffe	/* TX sequence number */
#define	AR_SeqNum_S		1
#define	AR_AckSigStrength	0x001fe000	/* strength of ACK */
#define	AR_AckSigStrength_S	13
#define	AR_FinalTSIndex		0x00600000	/* final TX attempt series ix */
#define	AR_FinalTSIndex_S	21
#define	AR_CompSuccess		0x00800000	/* compression status */
#define	AR_XmitAtenna		0x01000000	/* transmit antenna */
/* bits 25-31 are reserved */

/* RX ds_rxstatus1 */
/*	AR_Done			0x00000001	   descripter complete */
#define	AR_FrmRcvOK		0x00000002	/* frame reception success */
#define	AR_CRCErr		0x00000004	/* CRC error */
#define	AR_DecryptCRCErr	0x00000008	/* Decryption CRC fiailure */
#define	AR_PHYErr		0x00000010	/* PHY error */
#define	AR_MichaelErr		0x00000020	/* Michae MIC decrypt error */
/* bits 6-7 are reserved */
#define	AR_KeyIdxValid		0x00000100	/* decryption key index valid */
#define	AR_KeyIdx		0x0000fe00	/* Decryption key index */
#define	AR_KeyIdx_S		9
#define	AR_RcvTimestamp		0x7fff0000	/* timestamp */
#define	AR_RcvTimestamp_S	16
#define	AR_KeyCacheMiss		0x80000000	/* key cache miss indication */

/* NB: phy error code overlays key index and valid fields */
#define	AR_PHYErrCode		0x0000ff00	/* PHY error code */
#define	AR_PHYErrCode_S		8

#endif /* _ATH_AR5212_DESC_H_ */

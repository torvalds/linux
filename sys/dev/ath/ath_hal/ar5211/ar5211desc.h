/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2006 Atheros Communications, Inc.
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
#ifndef _DEV_ATH_AR5211DESC_H
#define _DEV_ATH_AR5211DESC_H

/*
 * Defintions for the DMA descriptors used by the Atheros
 * AR5211 and AR5110 Wireless Lan controller parts.
 */

/* DMA descriptors */
struct ar5211_desc {
	uint32_t	ds_link;	/* link pointer */
	uint32_t	ds_data;	/* data buffer pointer */
	uint32_t	ds_ctl0;	/* DMA control 0 */
	uint32_t	ds_ctl1;	/* DMA control 1 */
	uint32_t	ds_status0;	/* DMA status 0 */
	uint32_t	ds_status1;	/* DMA status 1 */
} __packed;
#define	AR5211DESC(_ds)	((struct ar5211_desc *)(_ds))
#define	AR5211DESC_CONST(_ds)	((const struct ar5211_desc *)(_ds))

/* TX ds_ctl0 */
#define	AR_FrameLen		0x00000fff	/* frame length */
/* bits 12-17 are reserved */
#define	AR_XmitRate		0x003c0000	/* txrate */
#define	AR_XmitRate_S		18
#define	AR_RTSCTSEnable		0x00400000	/* RTS/CTS enable */
#define	AR_VEOL			0x00800000	/* virtual end-of-list */
#define	AR_ClearDestMask	0x01000000	/* Clear destination mask bit */
#define	AR_AntModeXmit		0x1e000000	/* TX antenna seslection */
#define	AR_AntModeXmit_S	25
#define	AR_TxInterReq		0x20000000	/* TX interrupt request */
#define	AR_EncryptKeyValid	0x40000000	/* EncryptKeyIdx is valid */
/* bit 31 is reserved */

/* TX ds_ctl1 */
#define	AR_BufLen		0x00000fff	/* data buffer length */
#define	AR_More			0x00001000	/* more desc in this frame */
#define	AR_EncryptKeyIdx	0x000fe000	/* ecnrypt key table index */
#define	AR_EncryptKeyIdx_S	13
#define	AR_FrmType		0x00700000	/* frame type indication */
#define	AR_FrmType_S		20
#define	AR_Frm_Normal		0x00000000	/* normal frame */
#define	AR_Frm_ATIM		0x00100000	/* ATIM frame */
#define	AR_Frm_PSPOLL		0x00200000	/* PS poll frame */
#define	AR_Frm_Beacon		0x00300000	/* Beacon frame */
#define	AR_Frm_ProbeResp	0x00400000	/* no delay data */
#define	AR_NoAck		0x00800000	/* No ACK flag */
/* bits 24-31 are reserved */

/* RX ds_ctl1 */
/*	AR_BufLen		0x00000fff	   data buffer length */
/* bit 12 is reserved */
#define	AR_RxInterReq		0x00002000	/* RX interrupt request */
/* bits 14-31 are reserved */

/* TX ds_status0 */
#define	AR_FrmXmitOK		0x00000001	/* TX success */
#define	AR_ExcessiveRetries	0x00000002	/* excessive retries */
#define	AR_FIFOUnderrun		0x00000004	/* TX FIFO underrun */
#define	AR_Filtered		0x00000008	/* TX filter indication */
/* NB: the spec has the Short+Long retry counts reversed */
#define	AR_LongRetryCnt		0x000000f0	/* long retry count */
#define	AR_LongRetryCnt_S	4
#define	AR_ShortRetryCnt	0x00000f00	/* short retry count */
#define	AR_ShortRetryCnt_S	8
#define	AR_VirtCollCnt		0x0000f000	/* virtual collision count */
#define	AR_VirtCollCnt_S	12
#define	AR_SendTimestamp	0xffff0000	/* TX timestamp */
#define	AR_SendTimestamp_S	16

/* RX ds_status0 */
#define	AR_DataLen		0x00000fff	/* RX data length */
/*	AR_More			0x00001000	   more desc in this frame */
/* bits 13-14 are reserved */
#define	AR_RcvRate		0x00078000	/* reception rate */
#define	AR_RcvRate_S		15
#define	AR_RcvSigStrength	0x07f80000	/* receive signal strength */
#define	AR_RcvSigStrength_S	19
#define	AR_RcvAntenna		0x38000000	/* receive antenaa */
#define	AR_RcvAntenna_S		27
/* bits 30-31 are reserved */

/* TX ds_status1 */
#define	AR_Done			0x00000001	/* descripter complete */
#define	AR_SeqNum		0x00001ffe	/* TX sequence number */
#define	AR_SeqNum_S		1
#define	AR_AckSigStrength	0x001fe000	/* strength of ACK */
#define	AR_AckSigStrength_S	13
/* bits 21-31 are reserved */

/* RX ds_status1 */
/*	AR_Done			0x00000001	   descripter complete */
#define	AR_FrmRcvOK		0x00000002	/* frame reception success */
#define	AR_CRCErr		0x00000004	/* CRC error */
/* bit 3 reserved */
#define	AR_DecryptCRCErr	0x00000010	/* Decryption CRC fiailure */
#define	AR_PHYErr		0x000000e0	/* PHY error */
#define	AR_PHYErr_S		5
#define	AR_PHYErr_Underrun	0x00000000	/* Transmit underrun */
#define	AR_PHYErr_Tim		0x00000020	/* Timing error */
#define	AR_PHYErr_Par		0x00000040	/* Parity error */
#define	AR_PHYErr_Rate		0x00000060	/* Illegal rate */
#define	AR_PHYErr_Len		0x00000080	/* Illegal length */
#define	AR_PHYErr_Radar		0x000000a0	/* Radar detect */
#define	AR_PHYErr_Srv		0x000000c0	/* Illegal service */
#define	AR_PHYErr_TOR		0x000000e0	/* Transmit override receive */
#define	AR_KeyIdxValid		0x00000100	/* decryption key index valid */
#define	AR_KeyIdx		0x00007e00	/* Decryption key index */
#define	AR_KeyIdx_S		9
#define	AR_RcvTimestamp		0x0fff8000	/* timestamp */
#define	AR_RcvTimestamp_S	15
#define	AR_KeyCacheMiss		0x10000000	/* key cache miss indication */

#endif /* _DEV_ATH_AR5211DESC_H_ */

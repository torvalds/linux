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
#ifndef _DEV_ATH_AR5210DESC_H
#define _DEV_ATH_AR5210DESC_H

/*
 * Defintions for the DMA descriptors used by the Atheros
 * AR5210/AR5211 and AR5110 Wireless Lan controller parts.
 */

/* DMA descriptors */
struct ar5210_desc {
	uint32_t	ds_link;	/* link pointer */
	uint32_t	ds_data;	/* data buffer pointer */
	uint32_t	ds_ctl0;	/* DMA control 0 */
	uint32_t	ds_ctl1;	/* DMA control 1 */
	uint32_t	ds_status0;	/* DMA status 0 */
	uint32_t	ds_status1;	/* DMA status 1 */
} __packed;
#define	AR5210DESC(_ds)	((struct ar5210_desc *)(_ds))
#define	AR5210DESC_CONST(_ds)	((const struct ar5210_desc *)(_ds))

/* TX ds_ctl0 */
#define	AR_FrameLen		0x00000fff	/* frame length */
#define	AR_HdrLen		0x0003f000	/* header length */
#define	AR_HdrLen_S		12
#define	AR_XmitRate		0x003c0000	/* txrate */
#define	AR_XmitRate_S		18
#define	AR_Rate_6M		0xb
#define	AR_Rate_9M		0xf
#define	AR_Rate_12M		0xa
#define	AR_Rate_18M		0xe
#define	AR_Rate_24M		0x9
#define	AR_Rate_36M		0xd
#define	AR_Rate_48M		0x8
#define	AR_Rate_54M		0xc
#define	AR_RTSCTSEnable		0x00400000	/* RTS/CTS enable */
#define	AR_LongPkt		0x00800000	/* long packet indication */
#define	AR_ClearDestMask	0x01000000	/* Clear destination mask bit */
#define	AR_AntModeXmit		0x02000000	/* TX antenna seslection */
#define	AR_FrmType		0x1c000000	/* frame type indication */
#define	AR_FrmType_S		26
#define	AR_Frm_Normal		0x00000000	/* normal frame */
#define	AR_Frm_ATIM		0x04000000	/* ATIM frame */
#define	AR_Frm_PSPOLL		0x08000000	/* PS poll frame */
#define	AR_Frm_NoDelay		0x0c000000	/* no delay data */
#define	AR_Frm_PIFS		0x10000000	/* PIFS data */
#define	AR_TxInterReq		0x20000000	/* TX interrupt request */
#define	AR_EncryptKeyValid	0x40000000	/* EncryptKeyIdx is valid */

/* TX ds_ctl1 */
#define	AR_BufLen		0x00000fff	/* data buffer length */
#define	AR_More			0x00001000	/* more desc in this frame */
#define	AR_EncryptKeyIdx	0x0007e000	/* ecnrypt key table index */
#define	AR_EncryptKeyIdx_S	13
#define	AR_RTSDuration		0xfff80000	/* lower 13bit of duration */
#define	AR_RTSDuration_S	19

/* RX ds_ctl1 */
/*	AR_BufLen		0x00000fff	   data buffer length */
#define	AR_RxInterReq		0x00002000	/* RX interrupt request */

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
#define	AR_SendTimestamp	0xffff0000	/* TX timestamp */
#define	AR_SendTimestamp_S	16

/* RX ds_status0 */
#define	AR_DataLen		0x00000fff	/* RX data length */
/*	AR_More			0x00001000	   more desc in this frame */
#define	AR_RcvAntenna		0x00004000	/* received on ant 1 */
#define	AR_RcvRate		0x00078000	/* reception rate */
#define	AR_RcvRate_S		15
#define	AR_RcvSigStrength	0x07f80000	/* receive signal strength */
#define	AR_RcvSigStrength_S	19

/* TX ds_status1 */
#define	AR_Done			0x00000001	/* descripter complete */
#define	AR_SeqNum		0x00001ffe	/* TX sequence number */
#define	AR_AckSigStrength	0x001fe000	/* strength of ACK */
#define	AR_AckSigStrength_S	13

/* RX ds_status1 */
/*	AR_Done			0x00000001	   descripter complete */
#define	AR_FrmRcvOK		0x00000002	/* frame reception success */
#define	AR_CRCErr		0x00000004	/* CRC error */
#define	AR_FIFOOverrun		0x00000008	/* RX FIFO overrun */
#define	AR_DecryptCRCErr	0x00000010	/* Decryption CRC fiailure */
#define	AR_PHYErr		0x000000e0	/* PHY error */
#define	AR_PHYErr_S		5
#define	AR_PHYErr_NoErr		0x00000000	/* No error */
#define	AR_PHYErr_Tim		0x00000020	/* Timing error */
#define	AR_PHYErr_Par		0x00000040	/* Parity error */
#define	AR_PHYErr_Rate		0x00000060	/* Illegal rate */
#define	AR_PHYErr_Len		0x00000080	/* Illegal length */
#define	AR_PHYErr_QAM		0x000000a0	/* 64 QAM rate */
#define	AR_PHYErr_Srv		0x000000c0	/* Service bit error */
#define	AR_PHYErr_TOR		0x000000e0	/* Transmit override receive */
#define	AR_KeyIdxValid		0x00000100	/* decryption key index valid */
#define	AR_KeyIdx		0x00007e00	/* Decryption key index */
#define	AR_KeyIdx_S		9
#define	AR_RcvTimestamp		0x0fff8000	/* timestamp */
#define	AR_RcvTimestamp_S	15
#define	AR_KeyCacheMiss		0x10000000	/* key cache miss indication */

#endif /* _DEV_ATH_AR5210DESC_H_ */

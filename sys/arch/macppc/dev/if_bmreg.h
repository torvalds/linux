/*	$OpenBSD: if_bmreg.h,v 1.3 2005/10/09 04:24:50 brad Exp $	*/
/*	$NetBSD: if_bmreg.h,v 1.2 2000/01/25 14:38:50 tsubai Exp $	*/

/*
 * Copyright 1991-1998 by Open Software Foundation, Inc.
 *              All Rights Reserved
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appears in all copies and
 * that both the copyright notice and this permission notice appear in
 * supporting documentation.
 *
 * OSF DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 *
 * IN NO EVENT SHALL OSF BE LIABLE FOR ANY SPECIAL, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT,
 * NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* -------------------------------------------------------------------- */
/* Heathrow (F)eature (C)ontrol (R)egister Addresses			*/
/* -------------------------------------------------------------------- */
#define EnetEnable	0x60000000	/* enable Enet Xcvr/Controller */
#define ResetEnetCell	0x80000000	/* reset Enet cell */

/* -------------------------------------------------------------------- */
/*	BigMac Register Numbers & Bit Assignments			*/
/* -------------------------------------------------------------------- */
#define XIFC		0x0000
#define  TxOutputEnable		0x0001
#define  MIILoopbackBits	0x0006
#define  MIIBufferEnable	0x0008
#define  SQETestEnable		0x0010
#define LinkStatus	0x0100
#define TXFIFOCSR	0x0100
#define  TxFIFOEnable		0x0001
#define  TxFIFO128		0x0000
#define TXTH		0x0110
#define RXFIFOCSR	0x0120
#define  RxFIFOEnable		TxFIFOEnable
#define  RxFIFO128		TxFIFO128
#define MEMADD		0x0130
#define MEMDATAHI	0x0140
#define MEMDATALO	0x0150
#define XCVRIF		0x0160
#define  COLActiveLow		0x0002
#define  SerialMode		0x0004
#define  ClkBit			0x0008
#define CHIPID		0x0170
#define MIFCSR		0x0180
#define  MIFDC			0x0001	/* MII clock */
#define  MIFDO			0x0002	/* MII data out */
#define  MIFDIR			0x0004	/* MII direction (1: write) */
#define  MIFDI			0x0008	/* MII data in */
#define SROMCSR		0x0190
#define TXPNTR		0x01A0
#define RXPNTR		0x01B0
#define STATUS		0x0200
#define INTDISABLE	0x0210
#define  IntFrameReceived	0x0001
#define  IntRxFrameCntExp	0x0002
#define  IntRxAlignCntExp	0x0004
#define  IntRxCRCCntExp		0x0008
#define  IntRxLenCntExp		0x0010
#define  IntRxOverFlow		0x0020
#define  IntRxCodeViolation	0x0040
#define  IntSQETestError	0x0080
#define  IntFrameSent		0x0100
#define  IntTxUnderrun		0x0200
#define  IntTxMaxSizeError	0x0400
#define  IntTxNormalCollExp	0x0800
#define  IntTxExcessCollExp	0x1000
#define  IntTxLateCollExp	0x2000
#define  IntTxNetworkCollExp	0x4000
#define  IntTxDeferTimerExp	0x8000
#define  NormalIntEvents	~(IntFrameSent)
#define  NoEventsMask		0xFFFF

#define TxNeverGiveUp	0x0400
#define TXRST		0x0420
#define  TxResetBit		0x0001
#define TXCFG		0x0430
#define  TxMACEnable		0x0001
#define  TxThreshold		0x0004
#define  TxFullDuplex		0x0200
#define IPG1		0x0440
#define IPG2		0x0450
#define ALIMIT		0x0460
#define SLOT		0x0470
#define PALEN		0x0480
#define PAPAT		0x0490
#define TXSFD		0x04A0
#define JAM		0x04B0
#define TXMAX		0x04C0
#define TXMIN		0x04D0
#define PAREG		0x04E0
#define DCNT		0x04F0
#define NCCNT		0x0500
#define NTCNT		0x0510
#define EXCNT		0x0520
#define LTCNT		0x0530
#define RSEED		0x0540
#define TXSM		0x0550
#define RXRST		0x0620
#define  RxResetValue		0x0000
#define RXCFG		0x0630
#define  RxMACEnable		0x0001
#define  ReservedValue		0x0004
#define  RxPromiscEnable	0x0040
#define  RxCRCEnable		0x0100
#define  RxRejectOwnPackets	0x0200
#define  RxHashFilterEnable	0x0800
#define  RxAddrFilterEnable	0x1000
#define RXMAX		0x0640
#define RXMIN		0x0650
#define MADD2		0x0660
#define MADD1		0x0670
#define MADD0		0x0680
#define FRCNT		0x0690
#define LECNT		0x06A0
#define AECNT		0x06B0
#define FECNT		0x06C0
#define RXSM		0x06D0
#define RXCV		0x06E0
#define HASH3		0x0700
#define HASH2		0x0710
#define HASH1		0x0720
#define HASH0		0x0730
#define AFR2		0x0740
#define AFR1		0x0750
#define AFR0		0x0760
#define AFCR		0x0770
#define  EnableAllCompares	0x0fff

/* -------------------------------------------------------------------- */
/*	Misc. Bit definitions for BMac Status word			*/
/* -------------------------------------------------------------------- */
#define RxAbortBit	0x8000	/* status bit in BMac status for rx packets */
#define RxLengthMask	0x3FFF	/* bits that determine length of rx packets */

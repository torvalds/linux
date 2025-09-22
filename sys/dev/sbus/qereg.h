/*	$OpenBSD: qereg.h,v 1.4 2008/06/26 05:42:18 ray Exp $	*/
/*	$NetBSD: qereg.h,v 1.3 2000/07/24 04:28:51 mycroft Exp $	*/

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1998 Jason L. Wright.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * QE Channel registers
 */
#if 0
struct qe_cregs {
	u_int32_t ctrl;		/* control */
	u_int32_t stat;		/* status */
	u_int32_t rxds;		/* rx descriptor ring ptr */
	u_int32_t txds;		/* tx descriptor ring ptr */
	u_int32_t rimask;	/* rx interrupt mask */
	u_int32_t timask;	/* tx interrupt mask */
	u_int32_t qmask;	/* qec error interrupt mask */
	u_int32_t mmask;	/* mace error interrupt mask */
	u_int32_t rxwbufptr;	/* local memory rx write ptr */
	u_int32_t rxrbufptr;	/* local memory rx read ptr */
	u_int32_t txwbufptr;	/* local memory tx write ptr */
	u_int32_t txrbufptr;	/* local memory tx read ptr */
	u_int32_t ccnt;		/* collision counter */
	u_int32_t pipg;		/* inter-frame gap */
};
#endif
/* register indices: */
#define QE_CRI_CTRL	(0*4)
#define QE_CRI_STAT	(1*4)
#define QE_CRI_RXDS	(2*4)
#define QE_CRI_TXDS	(3*4)
#define QE_CRI_RIMASK	(4*4)
#define QE_CRI_TIMASK	(5*4)
#define QE_CRI_QMASK	(6*4)
#define QE_CRI_MMASK	(7*4)
#define QE_CRI_RXWBUF	(8*4)
#define QE_CRI_RXRBUF	(9*4)
#define QE_CRI_TXWBUF	(10*4)
#define QE_CRI_TXRBUF	(11*4)
#define QE_CRI_CCNT	(12*4)
#define QE_CRI_PIPG	(13*4)

/* qe_cregs.ctrl: control. */
#define	QE_CR_CTRL_RXOFF	0x00000004	/* disable receiver */
#define	QE_CR_CTRL_RESET	0x00000002	/* reset this channel */
#define	QE_CR_CTRL_TWAKEUP	0x00000001	/* tx dma wakeup */

/* qe_cregs.stat: status. */
#define	QE_CR_STAT_EDEFER	0x10000000	/* excessive defers */
#define	QE_CR_STAT_CLOSS	0x08000000	/* loss of carrier */
#define	QE_CR_STAT_ERETRIES	0x04000000	/* >16 retries */
#define	QE_CR_STAT_LCOLL	0x02000000	/* late tx collision */
#define	QE_CR_STAT_FUFLOW	0x01000000	/* fifo underflow */
#define	QE_CR_STAT_JERROR	0x00800000	/* jabber error */
#define	QE_CR_STAT_BERROR	0x00400000	/* babble error */
#define	QE_CR_STAT_TXIRQ	0x00200000	/* tx interrupt */
#define	QE_CR_STAT_TCCOFLOW	0x00100000	/* tx collision cntr expired */
#define	QE_CR_STAT_TXDERROR	0x00080000	/* tx descriptor is bad */
#define	QE_CR_STAT_TXLERR	0x00040000	/* tx late error */
#define	QE_CR_STAT_TXPERR	0x00020000	/* tx parity error */
#define	QE_CR_STAT_TXSERR	0x00010000	/* tx sbus error ack */
#define	QE_CR_STAT_RCCOFLOW	0x00001000	/* rx collision cntr expired */
#define	QE_CR_STAT_RUOFLOW	0x00000800	/* rx runt counter expired */
#define	QE_CR_STAT_MCOFLOW	0x00000400	/* rx missed counter expired */
#define	QE_CR_STAT_RXFOFLOW	0x00000200	/* rx fifo over flow */
#define	QE_CR_STAT_RLCOLL	0x00000100	/* rx late collision */
#define	QE_CR_STAT_FCOFLOW	0x00000080	/* rx frame counter expired */
#define	QE_CR_STAT_CECOFLOW	0x00000040	/* rx crc error cntr expired */
#define	QE_CR_STAT_RXIRQ	0x00000020	/* rx interrupt */
#define	QE_CR_STAT_RXDROP	0x00000010	/* rx dropped packet */
#define	QE_CR_STAT_RXSMALL	0x00000008	/* rx buffer too small */
#define	QE_CR_STAT_RXLERR	0x00000004	/* rx late error */
#define	QE_CR_STAT_RXPERR	0x00000002	/* rx parity error */
#define	QE_CR_STAT_RXSERR	0x00000001	/* rx sbus error ack */
#define QE_CR_STAT_BITS		"\020"				\
			"\01RXSERR\02RXPERR\03RXLERR"		\
			"\04RXSMALL\05RXDROP\06RXIRQ"		\
			"\07CECOFLOW\010FCOFLOW\011RLCOLL"	\
			"\012RXFOFLOW\013MCOFLOW\014RUOFLOW"	\
			"\015RCCOFLOW\021TXSERR\022TXPERR"	\
			"\023TXLERR\024TXDERROR\025TCCOFLOW"	\
			"\026TXIRQ\027BERROR\030JERROR"		\
			"\031FUFLOW\032LCOLL\033ERETRIES"	\
			"\034CLOSS\035EDEFER"

/*
 * Errors: all status bits except for TX/RX IRQ
 */
#define	QE_CR_STAT_ALLERRORS	\
	( QE_CR_STAT_EDEFER   | QE_CR_STAT_CLOSS    | QE_CR_STAT_ERETRIES \
	| QE_CR_STAT_LCOLL    | QE_CR_STAT_FUFLOW   | QE_CR_STAT_JERROR \
	| QE_CR_STAT_BERROR   | QE_CR_STAT_TCCOFLOW | QE_CR_STAT_TXDERROR \
	| QE_CR_STAT_TXLERR   | QE_CR_STAT_TXPERR   | QE_CR_STAT_TXSERR \
	| QE_CR_STAT_RCCOFLOW | QE_CR_STAT_RUOFLOW  | QE_CR_STAT_MCOFLOW \
	| QE_CR_STAT_RXFOFLOW | QE_CR_STAT_RLCOLL   | QE_CR_STAT_FCOFLOW \
	| QE_CR_STAT_CECOFLOW | QE_CR_STAT_RXDROP   | QE_CR_STAT_RXSMALL \
	| QE_CR_STAT_RXLERR   | QE_CR_STAT_RXPERR   | QE_CR_STAT_RXSERR)

/* qe_cregs.qmask: qec error interrupt mask. */
#define	QE_CR_QMASK_COFLOW	0x00100000	/* collision cntr overflow */
#define	QE_CR_QMASK_TXDERROR	0x00080000	/* tx descriptor error */
#define	QE_CR_QMASK_TXLERR	0x00040000	/* tx late error */
#define	QE_CR_QMASK_TXPERR	0x00020000	/* tx parity error */
#define	QE_CR_QMASK_TXSERR	0x00010000	/* tx sbus error ack */
#define	QE_CR_QMASK_RXDROP	0x00000010	/* rx packet dropped */
#define	QE_CR_QMASK_RXSMALL	0x00000008	/* rx buffer too small */
#define	QE_CR_QMASK_RXLERR	0x00000004	/* rx late error */
#define	QE_CR_QMASK_RXPERR	0x00000002	/* rx parity error */
#define	QE_CR_QMASK_RXSERR	0x00000001	/* rx sbus error ack */

/* qe_cregs.mmask: MACE error interrupt mask. */
#define	QE_CR_MMASK_EDEFER	0x10000000	/* excess defer */
#define	QE_CR_MMASK_CLOSS	0x08000000	/* carrier loss */
#define	QE_CR_MMASK_ERETRY	0x04000000	/* excess retry */
#define	QE_CR_MMASK_LCOLL	0x02000000	/* late collision error */
#define	QE_CR_MMASK_UFLOW	0x01000000	/* underflow */
#define	QE_CR_MMASK_JABBER	0x00800000	/* jabber error */
#define	QE_CR_MMASK_BABBLE	0x00400000	/* babble error */
#define	QE_CR_MMASK_OFLOW	0x00000800	/* overflow */
#define	QE_CR_MMASK_RXCOLL	0x00000400	/* rx coll-cntr overflow */
#define	QE_CR_MMASK_RPKT	0x00000200	/* runt pkt overflow */
#define	QE_CR_MMASK_MPKT	0x00000100	/* missed pkt overflow */

/* qe_cregs.pipg: inter-frame gap. */
#define	QE_CR_PIPG_TENAB	0x00000020	/* enable throttle */
#define	QE_CR_PIPG_MMODE	0x00000010	/* manual mode */
#define	QE_CR_PIPG_WMASK	0x0000000f	/* sbus wait mask */

/*
 * MACE registers
 */
#if 0
struct qe_mregs {
	u_int8_t rcvfifo;	[0]	/* receive fifo */
	u_int8_t xmtfifo;	[1]	/* transmit fifo */
	u_int8_t xmtfc;		[2]	/* transmit frame control */
	u_int8_t xmtfs;		[3]	/* transmit frame status */
	u_int8_t xmtrc;		[4]	/* tx retry count */
	u_int8_t rcvfc;		[5]	/* receive frame control */
	u_int8_t rcvfs;		[6]	/* receive frame status */
	u_int8_t fifofc;	[7]	/* fifo frame count */
	u_int8_t ir;		[8]	/* interrupt register */
	u_int8_t imr;		[9]	/* interrupt mask register */
	u_int8_t pr;		[10]	/* poll register */
	u_int8_t biucc;		[11]	/* biu config control */
	u_int8_t fifocc;	[12]	/* fifo config control */
	u_int8_t maccc;		[13]	/* mac config control */
	u_int8_t plscc;		[14]	/* pls config control */
	u_int8_t phycc;		[15]	/* phy config control */
	u_int8_t chipid1;	[16]	/* chipid, low byte */
	u_int8_t chipid2;	[17]	/* chipid, high byte */
	u_int8_t iac;		[18]	/* internal address config */
	u_int8_t _reserved0;	[19]	/* reserved */
	u_int8_t ladrf;		[20]	/* logical address filter */
	u_int8_t padr;		[21]	/* physical address */
	u_int8_t _reserved1;	[22]	/* reserved */
	u_int8_t _reserved2;	[23]	/* reserved */
	u_int8_t mpc;		[24]	/* missed packet count */
	u_int8_t _reserved3;	[25]	/* reserved */
	u_int8_t rntpc;		[26]	/* runt packet count */
	u_int8_t rcvcc;		[27]	/* receive collision count */
	u_int8_t _reserved4;	[28]	/* reserved */
	u_int8_t utr;		[29]	/* user test register */
	u_int8_t rtr1;		[30]	/* reserved test register 1 */
	u_int8_t rtr2;		[31]	/* reserved test register 2 */
};
#endif
/* register indices: */
#define QE_MRI_RCVFIFO	0	/* receive fifo */
#define QE_MRI_XMTFIFO	1	/* transmit fifo */
#define QE_MRI_XMTFC	2	/* transmit frame control */
#define QE_MRI_XMTFS	3	/* transmit frame status */
#define QE_MRI_XMTRC	4	/* tx retry count */
#define QE_MRI_RCVFC	5	/* receive frame control */
#define QE_MRI_RCVFS	6	/* receive frame status */
#define QE_MRI_FIFOFC	7	/* fifo frame count */
#define QE_MRI_IR	8	/* interrupt register */
#define QE_MRI_IMR	9	/* interrupt mask register */
#define QE_MRI_PR	10	/* poll register */
#define QE_MRI_BIUCC	11	/* biu config control */
#define QE_MRI_FIFOCC	12	/* fifo config control */
#define QE_MRI_MACCC	13	/* mac config control */
#define QE_MRI_PLSCC	14	/* pls config control */
#define QE_MRI_PHYCC	15	/* phy config control */
#define QE_MRI_CHIPID1	16	/* chipid, low byte */
#define QE_MRI_CHIPID2	17	/* chipid, high byte */
#define QE_MRI_IAC	18	/* internal address config */
#define QE_MRI_LADRF	20	/* logical address filter */
#define QE_MRI_PADR	21	/* physical address */
#define QE_MRI_MPC	24	/* missed packet count */
#define QE_MRI_RNTPC	26	/* runt packet count */
#define QE_MRI_RCVCC	27	/* receive collision count */
#define QE_MRI_UTR	29	/* user test register */
#define QE_MRI_RTR1	30	/* reserved test register 1 */
#define QE_MRI_RTR2	31	/* reserved test register 2 */

/* qe_mregs.xmtfc: transmit frame control. */
#define	QE_MR_XMTFC_DRETRY	0x80		/* disable retries */
#define	QE_MR_XMTFC_DXMTFCS	0x08		/* disable tx fcs */
#define	QE_MR_XMTFC_APADXMT	0x01		/* enable auto padding */

/* qe_mregs.xmtfs: transmit frame status. */
#define	QE_MR_XMTFS_XMTSV	0x80		/* tx valid */
#define	QE_MR_XMTFS_UFLO	0x40		/* tx underflow */
#define	QE_MR_XMTFS_LCOL	0x20		/* tx late collision */
#define	QE_MR_XMTFS_MORE	0x10		/* tx > 1 retries */
#define	QE_MR_XMTFS_ONE		0x08		/* tx 1 retry */
#define	QE_MR_XMTFS_DEFER	0x04		/* tx pkt deferred */
#define	QE_MR_XMTFS_LCAR	0x02		/* tx carrier lost */
#define	QE_MR_XMTFS_RTRY	0x01		/* tx retry error */

/* qe_mregs.xmtrc: transmit retry count. */
#define	QE_MR_XMTRC_EXDEF	0x80		/* tx excess defers */
#define	QE_MR_XMTRC_XMTRC	0x0f		/* tx retry count mask */

/* qe_mregs.rcvfc: receive frame control. */
#define	QE_MR_RCVFC_LLRCV	0x08		/* rx low latency */
#define	QE_MR_RCVFC_MR		0x04		/* rx addr match/reject */
#define	QE_MR_RCVFC_ASTRPRCV	0x01		/* rx auto strip */

/* qe_mregs.rcvfs: receive frame status. */
#define	QE_MR_RCVFS_OFLO	0x80		/* rx overflow */
#define	QE_MR_RCVFS_CLSN	0x40		/* rx late collision */
#define	QE_MR_RCVFS_FRAM	0x20		/* rx framing error */
#define	QE_MR_RCVFS_FCS		0x10		/* rx fcs error */
#define	QE_MR_RCVFS_RCVCNT	0x0f		/* rx msg byte count mask */

/* qe_mregs.fifofc: fifo frame count. */
#define	QE_MR_FIFOFC_RCVFC	0xf0		/* rx fifo frame count */
#define	QE_MR_FIFOFC_XMTFC	0x0f		/* tx fifo frame count */

/* qe_mregs.ir: interrupt register. */
#define	QE_MR_IR_JAB		0x80		/* jabber error */
#define	QE_MR_IR_BABL		0x40		/* babble error */
#define	QE_MR_IR_CERR		0x20		/* collision error */
#define	QE_MR_IR_RCVCCO		0x10		/* collision cnt overflow */
#define	QE_MR_IR_RNTPCO		0x08		/* runt pkt cnt overflow */
#define	QE_MR_IR_MPCO		0x04		/* miss pkt cnt overflow */
#define	QE_MR_IR_RCVINT		0x02		/* packet received */
#define	QE_MR_IR_XMTINT		0x01		/* packet transmitted */

/* qe_mregs.imr: interrupt mask register. */
#define	QE_MR_IMR_JABM		0x80		/* jabber errors */
#define	QE_MR_IMR_BABLM		0x40		/* babble errors */
#define	QE_MR_IMR_CERRM		0x20		/* collision errors */
#define	QE_MR_IMR_RCVCCOM	0x10		/* rx collision count oflow */
#define	QE_MR_IMR_RNTPCOM	0x08		/* runt pkt cnt ovrflw */
#define	QE_MR_IMR_MPCOM		0x04		/* miss pkt cnt ovrflw */
#define	QE_MR_IMR_RCVINTM	0x02		/* rx interrupts */
#define	QE_MR_IMR_XMTINTM	0x01		/* tx interrupts */

/* qe_mregs.pr: poll register. */
#define	QE_MR_PR_XMTSV		0x80		/* tx status is valid */
#define	QE_MR_PR_TDTREQ		0x40		/* tx data xfer request */
#define	QE_MR_PR_RDTREQ		0x20		/* rx data xfer request */

/* qe_mregs.biucc: biu config control. */
#define	QE_MR_BIUCC_BSWAP	0x40		/* byte swap */
#define	QE_MR_BIUCC_4TS		0x00		/* 4byte xmit start point */
#define	QE_MR_BIUCC_16TS	0x10		/* 16byte xmit start point */
#define	QE_MR_BIUCC_64TS	0x20		/* 64byte xmit start point */
#define	QE_MR_BIUCC_112TS	0x30		/* 112byte xmit start point */
#define	QE_MR_BIUCC_SWRST	0x01		/* sw-reset mace */

/* qe_mregs.fifocc: fifo config control. */
#define	QE_MR_FIFOCC_TXF8	0x00		/* tx fifo 8 write cycles */
#define	QE_MR_FIFOCC_TXF32	0x80		/* tx fifo 32 write cycles */
#define	QE_MR_FIFOCC_TXF16	0x40		/* tx fifo 16 write cycles */
#define	QE_MR_FIFOCC_RXF64	0x20		/* rx fifo 64 write cycles */
#define	QE_MR_FIFOCC_RXF32	0x10		/* rx fifo 32 write cycles */
#define	QE_MR_FIFOCC_RXF16	0x00		/* rx fifo 16 write cycles */
#define	QE_MR_FIFOCC_TFWU	0x08		/* tx fifo watermark update */
#define	QE_MR_FIFOCC_RFWU	0x04		/* rx fifo watermark update */
#define	QE_MR_FIFOCC_XMTBRST	0x02		/* tx burst enable */
#define	QE_MR_FIFOCC_RCVBRST	0x01		/* rx burst enable */

/* qe_mregs.maccc: mac config control. */
#define	QE_MR_MACCC_PROM	0x80		/* promiscuous mode enable */
#define	QE_MR_MACCC_DXMT2PD	0x40		/* tx 2part deferral enable */
#define	QE_MR_MACCC_EMBA	0x20		/* modified backoff enable */
#define	QE_MR_MACCC_DRCVPA	0x08		/* rx physical addr disable */
#define	QE_MR_MACCC_DRCVBC	0x04		/* rx broadcast disable */
#define	QE_MR_MACCC_ENXMT	0x02		/* enable transmitter */
#define	QE_MR_MACCC_ENRCV	0x01		/* enable receiver */

/* qe_mregs.plscc: pls config control. */
#define	QE_MR_PLSCC_XMTSEL	0x08		/* tx mode select */
#define QE_MR_PLSCC_PORTMASK	0x06		/* port selection bits */
#define	 QE_MR_PLSCC_GPSI	0x06		/* use gpsi connector */
#define	 QE_MR_PLSCC_DAI	0x04		/* use dai connector */
#define	 QE_MR_PLSCC_TP		0x02		/* use twistedpair connector */
#define	 QE_MR_PLSCC_AUI	0x00		/* use aui connector */
#define	QE_MR_PLSCC_ENPLSIO	0x01		/* pls i/o enable */

/* qe_mregs.phycc: phy config control. */
#define	QE_MR_PHYCC_LNKFL	0x80		/* link fail */
#define	QE_MR_PHYCC_DLNKTST	0x40		/* disable link test logic */
#define	QE_MR_PHYCC_REVPOL	0x20		/* rx polarity */
#define	QE_MR_PHYCC_DAPC	0x10		/* autopolaritycorrect disab */
#define	QE_MR_PHYCC_LRT		0x08		/* select low threshold */
#define	QE_MR_PHYCC_ASEL	0x04		/* connector port auto-sel */
#define	QE_MR_PHYCC_RWAKE	0x02		/* remote wakeup */
#define	QE_MR_PHYCC_AWAKE	0x01		/* auto wakeup */

/* qe_mregs.iac: internal address config. */
#define	QE_MR_IAC_ADDRCHG	0x80		/* start address change */
#define	QE_MR_IAC_PHYADDR	0x04		/* physical address reset */
#define	QE_MR_IAC_LOGADDR	0x02		/* logical address reset */

/* qe_mregs.utr: user test register. */
#define	QE_MR_UTR_RTRE		0x80		/* enable resv test register */
#define	QE_MR_UTR_RTRD		0x40		/* disab resv test register */
#define	QE_MR_UTR_RPA		0x20		/* accept runt packets */
#define	QE_MR_UTR_FCOLL		0x10		/* force collision status */
#define	QE_MR_UTR_RCVSFCSE	0x08		/* enable fcs on rx */
#define	QE_MR_UTR_INTLOOPM	0x06		/* Internal loopback w/mandec */
#define	QE_MR_UTR_INTLOOP	0x04		/* Internal loopback */
#define	QE_MR_UTR_EXTLOOP	0x02		/* external loopback */
#define	QE_MR_UTR_NOLOOP	0x00		/* no loopback */

/* Buffer and Ring sizes: fixed ring size */
#define	QE_TX_RING_MAXSIZE	256		/* maximum tx ring size */
#define	QE_RX_RING_MAXSIZE	256		/* maximum rx ring size */
#define	QE_TX_RING_SIZE		16
#define	QE_RX_RING_SIZE		16
#define	QE_PKT_BUF_SZ		2048

#define	MC_POLY_LE		0xedb88320	/* mcast crc, little endian */

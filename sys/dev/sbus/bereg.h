/*	$OpenBSD: bereg.h,v 1.5 2022/01/09 05:42:58 jsg Exp $	*/
/*	$NetBSD: bereg.h,v 1.4 2000/07/24 04:28:51 mycroft Exp $	*/

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
 * Copyright (c) 1998 Theo de Raadt and Jason L. Wright.
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
 * BE Global registers
 */
#if 0
struct be_bregs {
	u_int32_t xif_cfg;		/* XIF config */
	u_int32_t _unused[63];		/* reserved */
	u_int32_t stat;			/* status, clear on read */
	u_int32_t imask;		/* interrupt mask */
	u_int32_t _unused2[64];		/* reserved */
	u_int32_t tx_swreset;		/* tx software reset */
	u_int32_t tx_cfg;		/* tx config */
	u_int32_t ipkt_gap1;		/* inter-packet gap 1 */
	u_int32_t ipkt_gap2;		/* inter-packet gap 2 */
	u_int32_t attempt_limit;	/* tx attempt limit */
	u_int32_t stime;		/* tx slot time */
	u_int32_t preamble_len;		/* size of tx preamble */
	u_int32_t preamble_pattern;	/* pattern for tx preamble */
	u_int32_t tx_sframe_delim;	/* tx delimiter */
	u_int32_t jsize;		/* jam length */
	u_int32_t tx_pkt_max;		/* tx max pkt size */
	u_int32_t tx_pkt_min;		/* tx min pkt size */
	u_int32_t peak_attempt;		/* count of tx peak attempts */
	u_int32_t dt_ctr;		/* tx defer timer */
	u_int32_t nc_ctr;		/* tx normal collision cntr */
	u_int32_t fc_ctr;		/* tx first-collision cntr */
	u_int32_t ex_ctr;		/* tx excess-collision cntr */
	u_int32_t lt_ctr;		/* tx late-collision cntr */
	u_int32_t rand_seed;		/* tx random number seed */
	u_int32_t tx_smachine;		/* tx state machine */
	u_int32_t _unused3[44];		/* reserved */
	u_int32_t rx_swreset;		/* rx software reset */
	u_int32_t rx_cfg;		/* rx config register */
	u_int32_t rx_pkt_max;		/* rx max pkt size */
	u_int32_t rx_pkt_min;		/* rx min pkt size */
	u_int32_t mac_addr2;		/* ethernet address 2 (MSB) */
	u_int32_t mac_addr1;		/* ethernet address 1 */
	u_int32_t mac_addr0;		/* ethernet address 0 (LSB) */
	u_int32_t fr_ctr;		/* rx frame receive cntr */
	u_int32_t gle_ctr;		/* rx giant-len error cntr */
	u_int32_t unale_ctr;		/* rx unaligned error cntr */
	u_int32_t rcrce_ctr;		/* rx CRC error cntr */
	u_int32_t rx_smachine;		/* rx state machine */
	u_int32_t rx_cvalid;		/* rx code violation */
	u_int32_t _unused4;		/* reserved */
	u_int32_t htable3;		/* hash table 3 */
	u_int32_t htable2;		/* hash table 2 */
	u_int32_t htable1;		/* hash table 1 */
	u_int32_t htable0;		/* hash table 0 */
	u_int32_t afilter2;		/* address filter 2 */
	u_int32_t afilter1;		/* address filter 1 */
	u_int32_t afilter0;		/* address filter 0 */
	u_int32_t afilter_mask;		/* address filter mask */
};
#endif
/* register indices: */
#define BE_BRI_XIFCFG	(0*4)
#define BE_BRI_STAT	(64*4)
#define BE_BRI_IMASK	(65*4)
#define BE_BRI_TXCFG	(131*4)
#define BE_BRI_JSIZE	(139*4)
#define BE_BRI_NCCNT	(144*4)
#define BE_BRI_FCCNT	(145*4)
#define BE_BRI_EXCNT	(146*4)
#define BE_BRI_LTCNT	(147*4)
#define BE_BRI_RANDSEED	(148*4)
#define BE_BRI_RXCFG	(195*4)
#define BE_BRI_MACADDR2	(198*4)
#define BE_BRI_MACADDR1	(199*4)
#define BE_BRI_MACADDR0	(200*4)
#define BE_BRI_HASHTAB3	(208*4)
#define BE_BRI_HASHTAB2	(209*4)
#define BE_BRI_HASHTAB1	(210*4)
#define BE_BRI_HASHTAB0	(211*4)

/* be_bregs.xif_cfg: XIF config. */
#define BE_BR_XCFG_ODENABLE	0x00000001	/* output driver enable */
#define BE_BR_XCFG_RESV		0x00000002	/* reserved, write as 1 */
#define BE_BR_XCFG_MLBACK	0x00000004	/* loopback-mode mii enable */
#define BE_BR_XCFG_SMODE	0x00000008	/* enable serial mode */

/* be_bregs.stat: status, clear on read. */
#define BE_BR_STAT_GOTFRAME	0x00000001	/* received a frame */
#define BE_BR_STAT_RCNTEXP	0x00000002	/* rx frame cntr expired */
#define BE_BR_STAT_ACNTEXP	0x00000004	/* align-error cntr expired */
#define BE_BR_STAT_CCNTEXP	0x00000008	/* crc-error cntr expired */
#define BE_BR_STAT_LCNTEXP	0x00000010	/* length-error cntr expired */
#define BE_BR_STAT_RFIFOVF	0x00000020	/* rx fifo overflow */
#define BE_BR_STAT_CVCNTEXP	0x00000040	/* code-violation cntr exprd */
#define BE_BR_STAT_SENTFRAME	0x00000100	/* transmitted a frame */
#define BE_BR_STAT_TFIFO_UND	0x00000200	/* tx fifo underrun */
#define BE_BR_STAT_MAXPKTERR	0x00000400	/* max-packet size error */
#define BE_BR_STAT_NCNTEXP	0x00000800	/* normal-collision cntr exp */
#define BE_BR_STAT_ECNTEXP	0x00001000	/* excess-collision cntr exp */
#define BE_BR_STAT_LCCNTEXP	0x00002000	/* late-collision cntr exp */
#define BE_BR_STAT_FCNTEXP	0x00004000	/* first-collision cntr exp */
#define BE_BR_STAT_DTIMEXP	0x00008000	/* defer-timer expired */
#define BE_BR_STAT_BITS		"\020"				\
			"\01GOTFRAME\02RCNTEXP\03ACNTEXP"		\
			"\04CCNTEXP\05LCNTEXP\06RFIFOVF"		\
			"\07CVCNTEXP\011SENTFRAME\012TFIFO_UND"	\
			"\013MAXPKTERR\014NCNTEXP\015ECNTEXP"	\
			"\016LCCNTEXP\017FCNTEXP\020DTIMEXP"

/* be_bregs.imask: interrupt mask. */
#define BE_BR_IMASK_GOTFRAME	0x00000001	/* received a frame */
#define BE_BR_IMASK_RCNTEXP	0x00000002	/* rx frame cntr expired */
#define BE_BR_IMASK_ACNTEXP	0x00000004	/* align-error cntr expired */
#define BE_BR_IMASK_CCNTEXP	0x00000008	/* crc-error cntr expired */
#define BE_BR_IMASK_LCNTEXP	0x00000010	/* length-error cntr expired */
#define BE_BR_IMASK_RFIFOVF	0x00000020	/* rx fifo overflow */
#define BE_BR_IMASK_CVCNTEXP	0x00000040	/* code-violation cntr exprd */
#define BE_BR_IMASK_SENTFRAME	0x00000100	/* transmitted a frame */
#define BE_BR_IMASK_TFIFO_UND	0x00000200	/* tx fifo underrun */
#define BE_BR_IMASK_MAXPKTERR	0x00000400	/* max-packet size error */
#define BE_BR_IMASK_NCNTEXP	0x00000800	/* normal-collision cntr exp */
#define BE_BR_IMASK_ECNTEXP	0x00001000	/* excess-collision cntr exp */
#define BE_BR_IMASK_LCCNTEXP	0x00002000	/* late-collision cntr exp */
#define BE_BR_IMASK_FCNTEXP	0x00004000	/* first-collision cntr exp */
#define BE_BR_IMASK_DTIMEXP	0x00008000	/* defer-timer expired */

/* be_bregs.tx_cfg: tx config. */
#define BE_BR_TXCFG_ENABLE	0x00000001	/* enable the transmitter */
#define BE_BR_TXCFG_FIFO	0x00000010	/* default tx fthresh */
#define BE_BR_TXCFG_SMODE	0x00000020	/* enable slow transmit mode */
#define BE_BR_TXCFG_CIGN	0x00000040	/* ignore tx collisions */
#define BE_BR_TXCFG_FCSOFF	0x00000080	/* do not emit fcs */
#define BE_BR_TXCFG_DBACKOFF	0x00000100	/* disable backoff */
#define BE_BR_TXCFG_FULLDPLX	0x00000200	/* enable full-duplex */

/* be_bregs.rx_cfg: rx config. */
#define BE_BR_RXCFG_ENABLE	0x00000001	/* enable the receiver */
#define BE_BR_RXCFG_FIFO	0x0000000e	/* default rx fthresh */
#define BE_BR_RXCFG_PSTRIP	0x00000020	/* pad byte strip enable */
#define BE_BR_RXCFG_PMISC	0x00000040	/* enable promiscuous mode */
#define BE_BR_RXCFG_DERR	0x00000080	/* disable error checking */
#define BE_BR_RXCFG_DCRCS	0x00000100	/* disable crc stripping */
#define BE_BR_RXCFG_ME		0x00000200	/* receive packets for me */
#define BE_BR_RXCFG_PGRP	0x00000400	/* enable promisc group mode */
#define BE_BR_RXCFG_HENABLE	0x00000800	/* enable hash filter */
#define BE_BR_RXCFG_AENABLE	0x00001000	/* enable address filter */

/*
 * BE Channel registers
 */
#if 0
struct be_cregs {
	u_int32_t ctrl;		/* control */
	u_int32_t stat;		/* status */
	u_int32_t rxds;		/* rx descriptor ring ptr */
	u_int32_t txds;		/* tx descriptor ring ptr */
	u_int32_t rimask;	/* rx interrupt mask */
	u_int32_t timask;	/* tx interrupt mask */
	u_int32_t qmask;	/* qec error interrupt mask */
	u_int32_t bmask;	/* be error interrupt mask */
	u_int32_t rxwbufptr;	/* local memory rx write ptr */
	u_int32_t rxrbufptr;	/* local memory rx read ptr */
	u_int32_t txwbufptr;	/* local memory tx write ptr */
	u_int32_t txrbufptr;	/* local memory tx read ptr */
	u_int32_t ccnt;		/* collision counter */
};
#endif
/* register indices: */
#define BE_CRI_CTRL	(0*4)
#define BE_CRI_STAT	(1*4)
#define BE_CRI_RXDS	(2*4)
#define BE_CRI_TXDS	(3*4)
#define BE_CRI_RIMASK	(4*4)
#define BE_CRI_TIMASK	(5*4)
#define BE_CRI_QMASK	(6*4)
#define BE_CRI_BMASK	(7*4)
#define BE_CRI_RXWBUF	(8*4)
#define BE_CRI_RXRBUF	(9*4)
#define BE_CRI_TXWBUF	(10*4)
#define BE_CRI_TXRBUF	(11*4)
#define BE_CRI_CCNT	(12*4)

/* be_cregs.ctrl: control. */
#define	BE_CR_CTRL_TWAKEUP	0x00000001	/* tx dma wakeup */

/* be_cregs.stat: status. */
#define BE_CR_STAT_BERROR	0x80000000	/* be error */
#define BE_CR_STAT_TXIRQ	0x00200000	/* tx interrupt */
#define BE_CR_STAT_TXDERR	0x00080000	/* tx descriptor is bad */
#define BE_CR_STAT_TXLERR	0x00040000	/* tx late error */
#define BE_CR_STAT_TXPERR	0x00020000	/* tx parity error */
#define BE_CR_STAT_TXSERR	0x00010000	/* tx sbus error ack */
#define BE_CR_STAT_RXIRQ	0x00000020	/* rx interrupt */
#define BE_CR_STAT_RXDROP	0x00000010	/* rx packet dropped */
#define BE_CR_STAT_RXSMALL	0x00000008	/* rx buffer too small */
#define BE_CR_STAT_RXLERR	0x00000004	/* rx late error */
#define BE_CR_STAT_RXPERR	0x00000002	/* rx parity error */
#define BE_CR_STAT_RXSERR	0x00000001	/* rx sbus error ack */

/* be_cregs.qmask: qec error interrupt mask. */
#define BE_CR_QMASK_TXDERR	0x00080000	/* tx descriptor is bad */
#define BE_CR_QMASK_TXLERR	0x00040000	/* tx late error */
#define BE_CR_QMASK_TXPERR	0x00020000	/* tx parity error */
#define BE_CR_QMASK_TXSERR	0x00010000	/* tx sbus error ack */
#define BE_CR_QMASK_RXDROP	0x00000010	/* rx packet dropped */
#define BE_CR_QMASK_RXSMALL	0x00000008	/* rx buffer too small */
#define BE_CR_QMASK_RXLERR	0x00000004	/* rx late error */
#define BE_CR_QMASK_RXPERR	0x00000002	/* rx parity error */
#define BE_CR_QMASK_RXSERR	0x00000001	/* rx sbus error ack */

/*
 * BE Transceiver registers
 */
#if 0
struct be_tregs {
	u_int32_t	tcvr_pal;	/* transceiver pal */
	u_int32_t	mgmt_pal;	/* management pal */
};
#endif
/* register indices: */
#define BE_TRI_TCVRPAL	0
#define BE_TRI_MGMTPAL	4

/* be_tregs.tcvr_pal: transceiver pal */
#define	TCVR_PAL_SERIAL		0x00000001	/* serial mode enable */
#define TCVR_PAL_EXTLBACK	0x00000002	/* external loopback */
#define TCVR_PAL_MSENSE		0x00000004	/* media sense */
#define TCVR_PAL_LTENABLE	0x00000008	/* link test enable */
#define TCVR_PAL_LTSTATUS	0x00000010	/* link test status: p1 only */
#define TCVR_PAL_BITS		"\020"				\
				"\01SERIAL\02EXTLBACK\03MSENSE"	\
				"\04LTENABLE\05LTSTATUS"

/* be_tregs.mgmt_pal: management pal */
#define MGMT_PAL_DCLOCK		0x00000001	/* data clock strobe */
#define MGMT_PAL_OENAB		0x00000002	/* output enable */
#define MGMT_PAL_MDIO		0x00000004	/* MDIO data/attached */
#define MGMT_PAL_EXT_MDIO	MGMT_PAL_MDIO	/* external mdio */
#define MGMT_PAL_EXT_MDIO_SHIFT	2		/* position of ext mdio bit */
#define MGMT_PAL_TIMEO		0x00000008	/* tx enable timeout error */
#define MGMT_PAL_INT_MDIO	MGMT_PAL_TIMEO	/* internal mdio */
#define MGMT_PAL_INT_MDIO_SHIFT	3		/* position of int mdio bit */
#define MGMT_PAL_BITS		"\020"				\
				"\01DLCLOCK\02ENAB\03EXT_MDIO"	\
				"\04INT_MDIO"

/* Packet buffer size */
#define BE_PKT_BUF_SZ		2048

#define	MC_POLY_BE		0x04c11db7UL	/* mcast crc, big endian */
#define	MC_POLY_LE		0xedb88320UL	/* mcast crc, little endian */

/* PHY addresses */
#define BE_PHY_EXTERNAL		0
#define BE_PHY_INTERNAL		1

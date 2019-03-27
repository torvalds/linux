/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
 *
 *	from: NetBSD: hmereg.h,v 1.16 2003/11/02 11:07:45 wiz Exp
 *
 * $FreeBSD$
 */

/*
 * HME Shared Ethernet Block register offsets
 */
#define HME_SEBI_RESET	(0*4)
#define HME_SEBI_CFG	(1*4)
#define HME_SEBI_STAT	(64*4)
#define HME_SEBI_IMASK	(65*4)

/* HME SEB bits. */
#define HME_SEB_RESET_ETX	0x00000001	/* reset external transmitter */
#define HME_SEB_RESET_ERX	0x00000002	/* reset external receiver */

#define HME_SEB_CFG_BURSTMASK	0x00000003	/* covers all burst bits */
#define HME_SEB_CFG_BURST16	0x00000000	/* 16 byte bursts */
#define HME_SEB_CFG_BURST32	0x00000001	/* 32 byte bursts */
#define HME_SEB_CFG_BURST64	0x00000002	/* 64 byte bursts */
#define HME_SEB_CFG_64BIT	0x00000004	/* extended transfer mode */
#define HME_SEB_CFG_PARITY	0x00000008	/* parity check for DVMA/PIO */

#define HME_SEB_STAT_GOTFRAME	0x00000001	/* frame received */
#define HME_SEB_STAT_RCNTEXP	0x00000002	/* rx frame count expired */
#define HME_SEB_STAT_ACNTEXP	0x00000004	/* align error count expired */
#define HME_SEB_STAT_CCNTEXP	0x00000008	/* crc error count expired */
#define HME_SEB_STAT_LCNTEXP	0x00000010	/* length error count expired */
#define HME_SEB_STAT_RFIFOVF	0x00000020	/* rx fifo overflow */
#define HME_SEB_STAT_CVCNTEXP	0x00000040	/* code violation counter exp */
#define HME_SEB_STAT_STSTERR	0x00000080	/* xif sqe test failed */
#define HME_SEB_STAT_SENTFRAME	0x00000100	/* frame sent */
#define HME_SEB_STAT_TFIFO_UND	0x00000200	/* tx fifo underrun */
#define HME_SEB_STAT_MAXPKTERR	0x00000400	/* max-packet size error */
#define HME_SEB_STAT_NCNTEXP	0x00000800	/* normal collision count exp */
#define HME_SEB_STAT_ECNTEXP	0x00001000	/* excess collision count exp */
#define HME_SEB_STAT_LCCNTEXP	0x00002000	/* late collision count exp */
#define HME_SEB_STAT_FCNTEXP	0x00004000	/* first collision count exp */
#define HME_SEB_STAT_DTIMEXP	0x00008000	/* defer timer expired */
#define HME_SEB_STAT_RXTOHOST	0x00010000	/* pkt moved from rx fifo->memory */
#define HME_SEB_STAT_NORXD	0x00020000	/* out of receive descriptors */
#define HME_SEB_STAT_RXERR	0x00040000	/* rx DMA error */
#define HME_SEB_STAT_RXLATERR	0x00080000	/* late error during rx DMA */
#define HME_SEB_STAT_RXPERR	0x00100000	/* parity error during rx DMA */
#define HME_SEB_STAT_RXTERR	0x00200000	/* tag error during rx DMA */
#define HME_SEB_STAT_EOPERR	0x00400000	/* tx descriptor did not set EOP */
#define HME_SEB_STAT_MIFIRQ	0x00800000	/* mif needs attention */
#define HME_SEB_STAT_HOSTTOTX	0x01000000	/* pkt moved from memory->tx fifo */
#define HME_SEB_STAT_TXALL	0x02000000	/* all pkts in fifo transmitted */
#define HME_SEB_STAT_TXEACK	0x04000000	/* error during tx DMA */
#define HME_SEB_STAT_TXLERR	0x08000000	/* late error during tx DMA */
#define HME_SEB_STAT_TXPERR	0x10000000	/* parity error during tx DMA */
#define HME_SEB_STAT_TXTERR	0x20000000	/* tag error during tx DMA */
#define HME_SEB_STAT_SLVERR	0x40000000	/* pio access error */
#define HME_SEB_STAT_SLVPERR	0x80000000	/* pio access parity error */
#define HME_SEB_STAT_BITS	"\177\020"				\
			"b\0GOTFRAME\0b\1RCNTEXP\0b\2ACNTEXP\0"		\
			"b\3CCNTEXP\0b\4LCNTEXP\0b\5RFIFOVF\0"		\
			"b\6CVCNTEXP\0b\7STSTERR\0b\10SENTFRAME\0"	\
			"b\11TFIFO_UND\0b\12MAXPKTERR\0b\13NCNTEXP\0"	\
			"b\14ECNTEXP\0b\15LCCNTEXP\0b\16FCNTEXP\0"	\
			"b\17DTIMEXP\0b\20RXTOHOST\0b\21NORXD\0"	\
			"b\22RXERR\0b\23RXLATERR\0b\24RXPERR\0"		\
			"b\25RXTERR\0b\26EOPERR\0b\27MIFIRQ\0"		\
			"b\30HOSTTOTX\0b\31TXALL\0b\32XTEACK\0"		\
			"b\33TXLERR\0b\34TXPERR\0b\35TXTERR\0"		\
			"b\36SLVERR\0b\37SLVPERR\0\0"

#define HME_SEB_STAT_ALL_ERRORS	\
	(HME_SEB_STAT_SLVPERR  | HME_SEB_STAT_SLVERR  | HME_SEB_STAT_TXTERR   |\
	 HME_SEB_STAT_TXPERR   | HME_SEB_STAT_TXLERR  | HME_SEB_STAT_TXEACK   |\
	 HME_SEB_STAT_EOPERR   | HME_SEB_STAT_RXTERR  | HME_SEB_STAT_RXPERR   |\
	 HME_SEB_STAT_RXLATERR | HME_SEB_STAT_RXERR   | HME_SEB_STAT_NORXD    |\
	 HME_SEB_STAT_MAXPKTERR| HME_SEB_STAT_TFIFO_UND| HME_SEB_STAT_STSTERR |\
	 HME_SEB_STAT_RFIFOVF)

#define HME_SEB_STAT_VLAN_ERRORS	\
	(HME_SEB_STAT_SLVPERR  | HME_SEB_STAT_SLVERR  | HME_SEB_STAT_TXTERR   |\
	 HME_SEB_STAT_TXPERR   | HME_SEB_STAT_TXLERR  | HME_SEB_STAT_TXEACK   |\
	 HME_SEB_STAT_EOPERR   | HME_SEB_STAT_RXTERR  | HME_SEB_STAT_RXPERR   |\
	 HME_SEB_STAT_RXLATERR | HME_SEB_STAT_RXERR   | HME_SEB_STAT_NORXD    |\
	 HME_SEB_STAT_TFIFO_UND| HME_SEB_STAT_STSTERR | HME_SEB_STAT_RFIFOVF)

#define HME_SEB_STAT_FATAL_ERRORS	\
	(HME_SEB_STAT_SLVPERR  | HME_SEB_STAT_SLVERR  | HME_SEB_STAT_TXTERR   |\
	 HME_SEB_STAT_TXPERR   | HME_SEB_STAT_TXLERR  | HME_SEB_STAT_TXEACK   |\
	 HME_SEB_STAT_RXTERR   | HME_SEB_STAT_RXPERR  | HME_SEB_STAT_RXLATERR |\
	 HME_SEB_STAT_RXERR)

/*
 * HME Transmitter register offsets
 */
#define HME_ETXI_PENDING	(0*4)		/* Pending/wakeup */
#define HME_ETXI_CFG		(1*4)
#define HME_ETXI_RING		(2*4)		/* Descriptor Ring pointer */
#define HME_ETXI_BBASE		(3*4)		/* Buffer base address (ro) */
#define HME_ETXI_BDISP		(4*4)		/* Buffer displacement (ro) */
#define HME_ETXI_FIFO_WPTR	(5*4)		/* FIFO write pointer */
#define HME_ETXI_FIFO_SWPTR	(6*4)		/* FIFO shadow write pointer */
#define HME_ETXI_FIFO_RPTR	(7*4)		/* FIFO read pointer */
#define HME_ETXI_FIFO_SRPTR	(8*4)		/* FIFO shadow read pointer */
#define HME_ETXI_FIFO_PKTCNT	(9*4)		/* FIFO packet counter */
#define HME_ETXI_STATEMACHINE	(10*4)		/* State machine */
#define HME_ETXI_RSIZE		(11*4)		/* Ring size */
#define HME_ETXI_BPTR		(12*4)		/* Buffer pointer */


/* TXI_PENDING bits */
#define HME_ETX_TP_DMAWAKEUP	0x00000001	/* Start tx (rw, auto-clear) */

/* TXI_CFG bits */
#define HME_ETX_CFG_DMAENABLE	0x00000001	/* Enable TX DMA */
#define HME_ETX_CFG_FIFOTHRESH	0x000003fe	/* TX fifo threshold */
#define HME_ETX_CFG_IRQDAFTER	0x00000400	/* Intr after tx-fifo empty */
#define HME_ETX_CFG_IRQDBEFORE	0x00000000	/* Intr before tx-fifo empty */


/*
 * HME Receiver register offsets
 */
#define HME_ERXI_CFG		(0*4)
#define HME_ERXI_RING		(1*4)		/* Descriptor Ring pointer */
#define HME_ERXI_BPTR		(2*4)		/* Data Buffer pointer (ro) */
#define HME_ERXI_FIFO_WPTR	(3*4)		/* FIFO write pointer */
#define HME_ERXI_FIFO_SWPTR	(4*4)		/* FIFO shadow write pointer */
#define HME_ERXI_FIFO_RPTR	(5*4)		/* FIFO read pointer */
#define HME_ERXI_FIFO_PKTCNT	(6*4)		/* FIFO packet counter */
#define HME_ERXI_STATEMACHINE	(7*4)		/* State machine */

/* RXI_CFG bits */
#define HME_ERX_CFG_DMAENABLE	0x00000001	/* Enable RX DMA */
#define HME_ERX_CFG_FBO_MASK	0x00000038	/* RX first byte offset */
#define HME_ERX_CFG_FBO_SHIFT	0x00000003
#define HME_ERX_CFG_RINGSIZE32	0x00000000	/* Descriptor ring size: 32 */
#define HME_ERX_CFG_RINGSIZE64	0x00000200	/* Descriptor ring size: 64 */
#define HME_ERX_CFG_RINGSIZE128	0x00000400	/* Descriptor ring size: 128 */
#define HME_ERX_CFG_RINGSIZE256	0x00000600	/* Descriptor ring size: 256 */
#define HME_ERX_CFG_RINGSIZEMSK	0x00000600	/* Descriptor ring size: 256 */
#define HME_ERX_CFG_CSUMSTART_MASK 0x007f0000	/* cksum offset mask */
#define HME_ERX_CFG_CSUMSTART_SHIFT	16

/*
 * HME MAC-core register offsets
 */
#define HME_MACI_XIF		(0*4)
#define HME_MACI_TXSWRST	(130*4)		/* TX reset */
#define HME_MACI_TXCFG		(131*4)		/* TX config */
#define HME_MACI_JSIZE		(139*4)		/* TX jam size */
#define HME_MACI_TXSIZE		(140*4)		/* TX max size */
#define HME_MACI_NCCNT		(144*4)		/* TX normal collision cnt */
#define HME_MACI_FCCNT		(145*4)		/* TX first collision cnt */
#define HME_MACI_EXCNT		(146*4)		/* TX excess collision cnt */
#define HME_MACI_LTCNT		(147*4)		/* TX late collision cnt */
#define HME_MACI_RANDSEED	(148*4)		/*  */
#define HME_MACI_RXSWRST	(194*4)		/* RX reset */
#define HME_MACI_RXCFG		(195*4)		/* RX config */
#define HME_MACI_RXSIZE		(196*4)		/* RX max size */
#define HME_MACI_MACADDR2	(198*4)		/* MAC address */
#define HME_MACI_MACADDR1	(199*4)
#define HME_MACI_MACADDR0	(200*4)
#define HME_MACI_HASHTAB3	(208*4)		/* Address hash table */
#define HME_MACI_HASHTAB2	(209*4)
#define HME_MACI_HASHTAB1	(210*4)
#define HME_MACI_HASHTAB0	(211*4)
#define HME_MACI_AFILTER2	(212*4)		/* Address filter */
#define HME_MACI_AFILTER1	(213*4)
#define HME_MACI_AFILTER0	(214*4)
#define HME_MACI_AFILTER_MASK	(215*4)

/* XIF config register. */
#define HME_MAC_XIF_OE		0x00000001	/* Output driver enable */
#define HME_MAC_XIF_XLBACK	0x00000002	/* Loopback-mode XIF enable */
#define HME_MAC_XIF_MLBACK	0x00000004	/* Loopback-mode MII enable */
#define HME_MAC_XIF_MIIENABLE	0x00000008	/* MII receive buffer enable */
#define HME_MAC_XIF_SQENABLE	0x00000010	/* SQE test enable */
#define HME_MAC_XIF_SQETWIN	0x000003e0	/* SQE time window */
#define HME_MAC_XIF_LANCE	0x00000010	/* Lance mode enable */
#define HME_MAC_XIF_LIPG0	0x000003e0	/* Lance mode IPG0 */

/* Transmit config register. */
#define HME_MAC_TXCFG_ENABLE	0x00000001	/* Enable the transmitter */
#define HME_MAC_TXCFG_SMODE	0x00000020	/* Enable slow transmit mode */
#define HME_MAC_TXCFG_CIGN	0x00000040	/* Ignore transmit collisions */
#define HME_MAC_TXCFG_FCSOFF	0x00000080	/* Do not emit FCS */
#define HME_MAC_TXCFG_DBACKOFF	0x00000100	/* Disable backoff */
#define HME_MAC_TXCFG_FULLDPLX	0x00000200	/* Enable full-duplex */
#define HME_MAC_TXCFG_DGIVEUP	0x00000400	/* Don't give up on transmits */

/* Receive config register. */
#define HME_MAC_RXCFG_ENABLE	0x00000001 /* Enable the receiver */
#define HME_MAC_RXCFG_PSTRIP	0x00000020 /* Pad byte strip enable */
#define HME_MAC_RXCFG_PMISC	0x00000040 /* Enable promiscuous mode */
#define HME_MAC_RXCFG_DERR	0x00000080 /* Disable error checking */
#define HME_MAC_RXCFG_DCRCS	0x00000100 /* Disable CRC stripping */
#define HME_MAC_RXCFG_ME	0x00000200 /* Receive packets addressed to me */
#define HME_MAC_RXCFG_PGRP	0x00000400 /* Enable promisc group mode */
#define HME_MAC_RXCFG_HENABLE	0x00000800 /* Enable the hash filter */
#define HME_MAC_RXCFG_AENABLE	0x00001000 /* Enable the address filter */

/*
 * HME MIF register offsets
 */
#define HME_MIFI_BB_CLK		(0*4)	/* bit-bang clock */
#define HME_MIFI_BB_DATA	(1*4)	/* bit-bang data */
#define HME_MIFI_BB_OE		(2*4)	/* bit-bang output enable */
#define HME_MIFI_FO		(3*4)	/* frame output */
#define HME_MIFI_CFG		(4*4)	/*  */
#define HME_MIFI_IMASK		(5*4)	/* Interrupt mask for status change */
#define HME_MIFI_STAT		(6*4)	/* Status (ro, auto-clear) */
#define HME_MIFI_SM		(7*4)	/* State machine (ro) */

/* MIF Configuration register */
#define HME_MIF_CFG_PHY		0x00000001	/* PHY select */
#define HME_MIF_CFG_PE		0x00000002	/* Poll enable */
#define HME_MIF_CFG_BBMODE	0x00000004	/* Bit-bang mode */
#define HME_MIF_CFG_PRADDR	0x000000f8	/* Poll register address */
#define HME_MIF_CFG_MDI0	0x00000100	/* MDI_0 (ro) */
#define HME_MIF_CFG_MDI1	0x00000200	/* MDI_1 (ro) */
#define HME_MIF_CFG_PPADDR	0x00007c00	/* Poll phy address */

/* MIF Frame/Output register */
#define HME_MIF_FO_ST		0xc0000000	/* Start of frame */
#define HME_MIF_FO_ST_SHIFT	30		/* */
#define HME_MIF_FO_OPC		0x30000000	/* Opcode */
#define HME_MIF_FO_OPC_SHIFT	28		/* */
#define HME_MIF_FO_PHYAD	0x0f800000	/* PHY Address */
#define HME_MIF_FO_PHYAD_SHIFT	23		/* */
#define HME_MIF_FO_REGAD	0x007c0000	/* Register Address */
#define HME_MIF_FO_REGAD_SHIFT	18		/* */
#define HME_MIF_FO_TAMSB	0x00020000	/* Turn-around MSB */
#define HME_MIF_FO_TALSB	0x00010000	/* Turn-around LSB */
#define HME_MIF_FO_DATA		0x0000ffff	/* data to read or write */

/* Wired HME PHY addresses */
#define	HME_PHYAD_INTERNAL	1
#define	HME_PHYAD_EXTERNAL	0

/*
 * Buffer Descriptors.
 */
#define HME_XD_SIZE			8
#define HME_XD_FLAGS(base, index)	((base) + ((index) * HME_XD_SIZE) + 0)
#define HME_XD_ADDR(base, index)	((base) + ((index) * HME_XD_SIZE) + 4)
#define HME_XD_GETFLAGS(p, b, i)					\
	((p) ? le32toh(*((u_int32_t *)HME_XD_FLAGS(b,i))) :		\
		(*((u_int32_t *)HME_XD_FLAGS(b,i))))
#define HME_XD_SETFLAGS(p, b, i, f)	do {				\
	*((u_int32_t *)HME_XD_FLAGS(b,i)) = ((p) ? htole32((f)) : (f));	\
} while(/* CONSTCOND */ 0)
#define HME_XD_SETADDR(p, b, i, a)	do {				\
	*((u_int32_t *)HME_XD_ADDR(b,i)) = ((p) ? htole32((a)) : (a));	\
} while(/* CONSTCOND */ 0)

/* Descriptor flag values */
#define HME_XD_OWN	0x80000000	/* ownership: 1=hw, 0=sw */
#define HME_XD_SOP	0x40000000	/* start of packet marker (tx) */
#define HME_XD_OFL	0x40000000	/* buffer overflow (rx) */
#define HME_XD_EOP	0x20000000	/* end of packet marker (tx) */
#define HME_XD_TXCKSUM	0x10000000	/* checksum enable (tx) */
#define HME_XD_RXLENMSK	0x3fff0000	/* packet length mask (rx) */
#define HME_XD_RXLENSHIFT	16
#define HME_XD_TXLENMSK	0x00003fff	/* packet length mask (tx) */
#define HME_XD_TXCKSUM_SSHIFT	14
#define HME_XD_TXCKSUM_OSHIFT	20
#define HME_XD_RXCKSUM	0x0000ffff	/* packet checksum (rx) */

/* Macros to encode/decode the receive buffer size from the flags field */
#define HME_XD_ENCODE_RSIZE(sz)		\
	(((sz) << HME_XD_RXLENSHIFT) & HME_XD_RXLENMSK)
#define HME_XD_DECODE_RSIZE(flags)	\
	(((flags) & HME_XD_RXLENMSK) >> HME_XD_RXLENSHIFT)

/* Provide encode/decode macros for the transmit buffers for symmetry */
#define HME_XD_ENCODE_TSIZE(sz)		\
	(((sz) << 0) & HME_XD_TXLENMSK)
#define HME_XD_DECODE_TSIZE(flags)	\
	(((flags) & HME_XD_TXLENMSK) >> 0)

#define	HME_MINRXALIGN		0x10
#define	HME_RXOFFS		2

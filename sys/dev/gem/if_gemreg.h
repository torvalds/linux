/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (C) 2001 Eduardo Horvath.
 * All rights reserved.
 *
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR  ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR  BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: NetBSD: gemreg.h,v 1.9 2006/11/24 13:01:07 martin Exp
 *
 * $FreeBSD$
 */

#ifndef	_IF_GEMREG_H
#define	_IF_GEMREG_H

/* register definitions for Apple GMAC, Sun ERI and Sun GEM */

/*
 * First bank: these registers live at the start of the PCI
 * mapping, and at the start of the second bank of the SBus
 * version.
 */
#define	GEM_SEB_STATE		0x0000	/* SEB state reg, R/O */
#define	GEM_CONFIG		0x0004	/* config reg */
#define	GEM_STATUS		0x000c	/* status reg */
/* Note: Reading the status reg clears bits 0-6. */
#define	GEM_INTMASK		0x0010
#define	GEM_INTACK		0x0014	/* Interrupt acknowledge, W/O */
#define	GEM_STATUS_ALIAS	0x001c

/* Bits in GEM_SEB register */
#define	GEM_SEB_ARB		0x00000002	/* Arbitration status */
#define	GEM_SEB_RXWON		0x00000004

/* Bits in GEM_CONFIG register */
#define	GEM_CONFIG_BURST_64	0x00000000	/* maximum burst size 64KB */
#define	GEM_CONFIG_BURST_INF	0x00000001	/* infinite for entire packet */
#define	GEM_CONFIG_TXDMA_LIMIT	0x0000003e
#define	GEM_CONFIG_RXDMA_LIMIT	0x000007c0
/* GEM_CONFIG_RONPAULBIT and GEM_CONFIG_BUG2FIX are Apple only. */
#define	GEM_CONFIG_RONPAULBIT	0x00000800	/* after infinite burst use */
						/* memory read multiple for */
						/* PCI commands */
#define	GEM_CONFIG_BUG2FIX	0x00001000	/* fix RX hang after overflow */

#define	GEM_CONFIG_TXDMA_LIMIT_SHIFT	1
#define	GEM_CONFIG_RXDMA_LIMIT_SHIFT	6

/* Top part of GEM_STATUS has TX completion information */
#define	GEM_STATUS_TX_COMPLETION_MASK	0xfff80000	/* TX completion reg. */
#define	GEM_STATUS_TX_COMPLETION_SHFT	19

/*
 * Interrupt bits, for both the GEM_STATUS and GEM_INTMASK regs
 * Bits 0-6 auto-clear when read.
 */
#define	GEM_INTR_TX_INTME	0x00000001	/* Frame w/INTME bit set sent */
#define	GEM_INTR_TX_EMPTY	0x00000002	/* TX ring empty */
#define	GEM_INTR_TX_DONE	0x00000004	/* TX complete */
#define	GEM_INTR_RX_DONE	0x00000010	/* Got a packet */
#define	GEM_INTR_RX_NOBUF	0x00000020
#define	GEM_INTR_RX_TAG_ERR	0x00000040
#define	GEM_INTR_PERR		0x00000080	/* Parity error */
#define	GEM_INTR_PCS		0x00002000	/* Physical Code Sub-layer */
#define	GEM_INTR_TX_MAC		0x00004000
#define	GEM_INTR_RX_MAC		0x00008000
#define	GEM_INTR_MAC_CONTROL	0x00010000	/* MAC control interrupt */
#define	GEM_INTR_MIF		0x00020000
#define	GEM_INTR_BERR		0x00040000	/* Bus error interrupt */
#define	GEM_INTR_BITS	"\177\020"					\
			"b\0INTME\0b\1TXEMPTY\0b\2TXDONE\0"		\
			"b\4RXDONE\0b\5RXNOBUF\0b\6RX_TAG_ERR\0"	\
			"b\xdPCS\0b\xeTXMAC\0b\xfRXMAC\0"		\
			"b\x10MAC_CONTROL\0b\x11MIF\0b\x12IBERR\0\0"

/*
 * Second bank: these registers live at offset 0x1000 of the PCI
 * mapping, and at the start of the first bank of the SBus
 * version.
 */
#define	GEM_PCI_BANK2_OFFSET	0x1000
#define	GEM_PCI_BANK2_SIZE	0x14
/* This is the same as the GEM_STATUS reg but reading it does not clear bits. */
#define	GEM_PCI_ERROR_STATUS	0x0000	/* PCI error status */
#define	GEM_PCI_ERROR_MASK	0x0004	/* PCI error mask */
#define	GEM_PCI_BIF_CONFIG	0x0008	/* PCI BIF configuration */
#define	GEM_PCI_BIF_DIAG	0x000c	/* PCI BIF diagnostic */

#define	GEM_SBUS_BIF_RESET	0x0000	/* SBus BIF only software reset */
#define	GEM_SBUS_CONFIG		0x0004	/* SBus IO configuration */
#define	GEM_SBUS_STATUS		0x0008	/* SBus IO status */
#define	GEM_SBUS_REVISION	0x000c	/* SBus revision ID */

#define	GEM_RESET		0x0010	/* software reset */

/* GEM_PCI_ERROR_STATUS and GEM_PCI_ERROR_MASK error bits */
#define	GEM_PCI_ERR_STAT_BADACK	0x00000001	/* No ACK64# */
#define	GEM_PCI_ERR_STAT_DTRTO	0x00000002	/* Delayed xaction timeout */
#define	GEM_PCI_ERR_STAT_OTHERS	0x00000004
#define	GEM_PCI_ERR_BITS	"\177\020b\0ACKBAD\0b\1DTRTO\0b\2OTHER\0\0"

/* GEM_PCI_BIF_CONFIG register bits */
#define	GEM_PCI_BIF_CNF_SLOWCLK	0x00000001	/* Parity error timing */
#define	GEM_PCI_BIF_CNF_HOST_64	0x00000002	/* 64-bit host */
#define	GEM_PCI_BIF_CNF_B64D_DS	0x00000004	/* no 64-bit data cycle */
#define	GEM_PCI_BIF_CNF_M66EN	0x00000008
#define	GEM_PCI_BIF_CNF_BITS	"\177\020b\0SLOWCLK\0b\1HOST64\0"	\
				"b\2B64DIS\0b\3M66EN\0\0"

/* GEM_PCI_BIF_DIAG register bits */
#define	GEN_PCI_BIF_DIAG_BC_SM	0x007f0000	/* burst ctrl. state machine */
#define	GEN_PCI_BIF_DIAG_SM	0xff000000	/* BIF state machine */

/* Bits in GEM_SBUS_CONFIG register */
#define	GEM_SBUS_CFG_BURST_32	0x00000001	/* 32 byte bursts */
#define	GEM_SBUS_CFG_BURST_64	0x00000002	/* 64 byte bursts */
#define	GEM_SBUS_CFG_BURST_128	0x00000004	/* 128 byte bursts */
#define	GEM_SBUS_CFG_64BIT	0x00000008	/* extended transfer mode */
#define	GEM_SBUS_CFG_PARITY	0x00000200	/* enable parity checking */

/* GEM_SBUS_STATUS register bits */
#define	GEM_SBUS_STATUS_LERR	0x00000001	/* LERR from SBus slave */
#define	GEM_SBUS_STATUS_SACK	0x00000002	/* size ack. error */
#define	GEM_SBUS_STATUS_EACK	0x00000004	/* SBus ctrl. or slave error */
#define	GEM_SBUS_STATUS_MPARITY	0x00000008	/* SBus master parity error */

/* GEM_RESET register bits -- TX and RX self clear when complete. */
#define	GEM_RESET_TX		0x00000001	/* Reset TX half. */
#define	GEM_RESET_RX		0x00000002	/* Reset RX half. */
#define	GEM_RESET_PCI_RSTOUT	0x00000004	/* Force PCI RSTOUT#. */
#define	GEM_RESET_CLSZ_MASK	0x00ff0000	/* ERI cache line size */
#define	GEM_RESET_CLSZ_SHFT	16

/* The rest of the registers live in the first bank again. */

/* TX DMA registers */
#define	GEM_TX_KICK		0x2000		/* Write last valid desc + 1 */
#define	GEM_TX_CONFIG		0x2004
#define	GEM_TX_RING_PTR_LO	0x2008
#define	GEM_TX_RING_PTR_HI	0x200c

#define	GEM_TX_FIFO_WR_PTR	0x2014		/* FIFO write pointer */
#define	GEM_TX_FIFO_SDWR_PTR	0x2018		/* FIFO shadow write pointer */
#define	GEM_TX_FIFO_RD_PTR	0x201c		/* FIFO read pointer */
#define	GEM_TX_FIFO_SDRD_PTR	0x2020		/* FIFO shadow read pointer */
#define	GEM_TX_FIFO_PKT_CNT	0x2024		/* FIFO packet counter */

#define	GEM_TX_STATE_MACHINE	0x2028		/* ETX state machine reg */
#define	GEM_TX_DATA_PTR_LO	0x2030
#define	GEM_TX_DATA_PTR_HI	0x2034

#define	GEM_TX_COMPLETION	0x2100
#define	GEM_TX_FIFO_ADDRESS	0x2104
#define	GEM_TX_FIFO_TAG		0x2108
#define	GEM_TX_FIFO_DATA_LO	0x210c
#define	GEM_TX_FIFO_DATA_HI_T1	0x2110
#define	GEM_TX_FIFO_DATA_HI_T0	0x2114
#define	GEM_TX_FIFO_SIZE	0x2118
#define	GEM_TX_DEBUG		0x3028

/* GEM_TX_CONFIG register bits */
#define	GEM_TX_CONFIG_TXDMA_EN	0x00000001	/* TX DMA enable */
#define	GEM_TX_CONFIG_TXRING_SZ	0x0000001e	/* TX ring size */
#define	GEM_TX_CONFIG_TXFIFO_TH	0x001ffc00	/* TX fifo threshold */
#define	GEM_TX_CONFIG_PACED	0x00200000	/* TX_all_int modifier */

#define	GEM_RING_SZ_32		(0<<1)	/* 32 descriptors */
#define	GEM_RING_SZ_64		(1<<1)
#define	GEM_RING_SZ_128		(2<<1)
#define	GEM_RING_SZ_256		(3<<1)
#define	GEM_RING_SZ_512		(4<<1)
#define	GEM_RING_SZ_1024	(5<<1)
#define	GEM_RING_SZ_2048	(6<<1)
#define	GEM_RING_SZ_4096	(7<<1)
#define	GEM_RING_SZ_8192	(8<<1)

/* GEM_TX_COMPLETION register bits */
#define	GEM_TX_COMPLETION_MASK	0x00001fff	/* # of last descriptor */

/* RX DMA registers */
#define	GEM_RX_CONFIG		0x4000
#define	GEM_RX_RING_PTR_LO	0x4004		/* 64-bits unaligned GAK! */
#define	GEM_RX_RING_PTR_HI	0x4008		/* 64-bits unaligned GAK! */

#define	GEM_RX_FIFO_WR_PTR	0x400c		/* FIFO write pointer */
#define	GEM_RX_FIFO_SDWR_PTR	0x4010		/* FIFO shadow write pointer */
#define	GEM_RX_FIFO_RD_PTR	0x4014		/* FIFO read pointer */
#define	GEM_RX_FIFO_PKT_CNT	0x4018		/* FIFO packet counter */

#define	GEM_RX_STATE_MACHINE	0x401c		/* ERX state machine reg */
#define	GEM_RX_PAUSE_THRESH	0x4020

#define	GEM_RX_DATA_PTR_LO	0x4024		/* ERX state machine reg */
#define	GEM_RX_DATA_PTR_HI	0x4028		/* Damn thing is unaligned */

#define	GEM_RX_KICK		0x4100		/* Write last valid desc + 1 */
#define	GEM_RX_COMPLETION	0x4104		/* First pending desc */
#define	GEM_RX_BLANKING		0x4108		/* Interrupt blanking reg */

#define	GEM_RX_FIFO_ADDRESS	0x410c
#define	GEM_RX_FIFO_TAG		0x4110
#define	GEM_RX_FIFO_DATA_LO	0x4114
#define	GEM_RX_FIFO_DATA_HI_T1	0x4118
#define	GEM_RX_FIFO_DATA_HI_T0	0x411c
#define	GEM_RX_FIFO_SIZE	0x4120

/* GEM_RX_CONFIG register bits */
#define	GEM_RX_CONFIG_RXDMA_EN	0x00000001	/* RX DMA enable */
#define	GEM_RX_CONFIG_RXRING_SZ	0x0000001e	/* RX ring size */
#define	GEM_RX_CONFIG_BATCH_DIS	0x00000020	/* desc batching disable */
#define	GEM_RX_CONFIG_FBOFF	0x00001c00	/* first byte offset */
#define	GEM_RX_CONFIG_CXM_START	0x000fe000	/* cksum start offset bytes */
#define	GEM_RX_CONFIG_FIFO_THRS	0x07000000	/* fifo threshold size */

#define	GEM_THRSH_64	0
#define	GEM_THRSH_128	1
#define	GEM_THRSH_256	2
#define	GEM_THRSH_512	3
#define	GEM_THRSH_1024	4
#define	GEM_THRSH_2048	5

#define	GEM_RX_CONFIG_FIFO_THRS_SHIFT	24
#define	GEM_RX_CONFIG_FBOFF_SHFT	10
#define	GEM_RX_CONFIG_CXM_START_SHFT	13

/* GEM_RX_PAUSE_THRESH register bits -- sizes in multiples of 64 bytes */
#define	GEM_RX_PTH_XOFF_THRESH	0x000001ff
#define	GEM_RX_PTH_XON_THRESH	0x001ff000

/* GEM_RX_BLANKING register bits */
#define	GEM_RX_BLANKING_PACKETS	0x000001ff	/* Delay intr for x packets */
#define	GEM_RX_BLANKING_TIME	0x000ff000	/* Delay intr for x ticks */
#define	GEM_RX_BLANKING_TIME_SHIFT 12
/* One tick is 2048 PCI clocks, or 16us at 66MHz */

/* GEM_MAC registers */
#define	GEM_MAC_TXRESET		0x6000		/* Store 1, cleared when done */
#define	GEM_MAC_RXRESET		0x6004		/* ditto */
#define	GEM_MAC_SEND_PAUSE_CMD	0x6008
#define	GEM_MAC_TX_STATUS	0x6010
#define	GEM_MAC_RX_STATUS	0x6014
#define	GEM_MAC_CONTROL_STATUS	0x6018		/* MAC control status reg */
#define	GEM_MAC_TX_MASK		0x6020		/* TX MAC mask register */
#define	GEM_MAC_RX_MASK		0x6024
#define	GEM_MAC_CONTROL_MASK	0x6028
#define	GEM_MAC_TX_CONFIG	0x6030
#define	GEM_MAC_RX_CONFIG	0x6034
#define	GEM_MAC_CONTROL_CONFIG	0x6038
#define	GEM_MAC_XIF_CONFIG	0x603c
#define	GEM_MAC_IPG0		0x6040		/* inter packet gap 0 */
#define	GEM_MAC_IPG1		0x6044		/* inter packet gap 1 */
#define	GEM_MAC_IPG2		0x6048		/* inter packet gap 2 */
#define	GEM_MAC_SLOT_TIME	0x604c		/* slot time, bits 0-7 */
#define	GEM_MAC_MAC_MIN_FRAME	0x6050
#define	GEM_MAC_MAC_MAX_FRAME	0x6054
#define	GEM_MAC_PREAMBLE_LEN	0x6058
#define	GEM_MAC_JAM_SIZE	0x605c
#define	GEM_MAC_ATTEMPT_LIMIT	0x6060
#define	GEM_MAC_CONTROL_TYPE	0x6064

#define	GEM_MAC_ADDR0		0x6080		/* Normal MAC address 0 */
#define	GEM_MAC_ADDR1		0x6084
#define	GEM_MAC_ADDR2		0x6088
#define	GEM_MAC_ADDR3		0x608c		/* Alternate MAC address 0 */
#define	GEM_MAC_ADDR4		0x6090
#define	GEM_MAC_ADDR5		0x6094
#define	GEM_MAC_ADDR6		0x6098		/* Control MAC address 0 */
#define	GEM_MAC_ADDR7		0x609c
#define	GEM_MAC_ADDR8		0x60a0

#define	GEM_MAC_ADDR_FILTER0	0x60a4
#define	GEM_MAC_ADDR_FILTER1	0x60a8
#define	GEM_MAC_ADDR_FILTER2	0x60ac
#define	GEM_MAC_ADR_FLT_MASK1_2	0x60b0		/* Address filter mask 1,2 */
#define	GEM_MAC_ADR_FLT_MASK0	0x60b4		/* Address filter mask 0 reg */

#define	GEM_MAC_HASH0		0x60c0		/* Hash table 0 */
#define	GEM_MAC_HASH1		0x60c4
#define	GEM_MAC_HASH2		0x60c8
#define	GEM_MAC_HASH3		0x60cc
#define	GEM_MAC_HASH4		0x60d0
#define	GEM_MAC_HASH5		0x60d4
#define	GEM_MAC_HASH6		0x60d8
#define	GEM_MAC_HASH7		0x60dc
#define	GEM_MAC_HASH8		0x60e0
#define	GEM_MAC_HASH9		0x60e4
#define	GEM_MAC_HASH10		0x60e8
#define	GEM_MAC_HASH11		0x60ec
#define	GEM_MAC_HASH12		0x60f0
#define	GEM_MAC_HASH13		0x60f4
#define	GEM_MAC_HASH14		0x60f8
#define	GEM_MAC_HASH15		0x60fc

#define	GEM_MAC_NORM_COLL_CNT	0x6100		/* Normal collision counter */
#define	GEM_MAC_FIRST_COLL_CNT	0x6104		/* 1st successful collision cntr */
#define	GEM_MAC_EXCESS_COLL_CNT	0x6108		/* Excess collision counter */
#define	GEM_MAC_LATE_COLL_CNT	0x610c		/* Late collision counter */
#define	GEM_MAC_DEFER_TMR_CNT	0x6110		/* defer timer counter */
#define	GEM_MAC_PEAK_ATTEMPTS	0x6114
#define	GEM_MAC_RX_FRAME_COUNT	0x6118
#define	GEM_MAC_RX_LEN_ERR_CNT	0x611c
#define	GEM_MAC_RX_ALIGN_ERR	0x6120
#define	GEM_MAC_RX_CRC_ERR_CNT	0x6124
#define	GEM_MAC_RX_CODE_VIOL	0x6128
#define	GEM_MAC_RANDOM_SEED	0x6130
#define	GEM_MAC_MAC_STATE	0x6134		/* MAC state machine reg */

/* GEM_MAC_SEND_PAUSE_CMD register bits */
#define	GEM_MAC_PAUSE_CMD_TIME	0x0000ffff
#define	GEM_MAC_PAUSE_CMD_SEND	0x00010000

/* GEM_MAC_TX_STATUS and _MASK register bits */
#define	GEM_MAC_TX_XMIT_DONE	0x00000001
#define	GEM_MAC_TX_UNDERRUN	0x00000002
#define	GEM_MAC_TX_PKT_TOO_LONG	0x00000004
#define	GEM_MAC_TX_NCC_EXP	0x00000008	/* Normal collision cnt exp */
#define	GEM_MAC_TX_ECC_EXP	0x00000010
#define	GEM_MAC_TX_LCC_EXP	0x00000020
#define	GEM_MAC_TX_FCC_EXP	0x00000040
#define	GEM_MAC_TX_DEFER_EXP	0x00000080
#define	GEM_MAC_TX_PEAK_EXP	0x00000100

/* GEM_MAC_RX_STATUS and _MASK register bits */
#define	GEM_MAC_RX_DONE		0x00000001
#define	GEM_MAC_RX_OVERFLOW	0x00000002
#define	GEM_MAC_RX_FRAME_CNT	0x00000004
#define	GEM_MAC_RX_ALIGN_EXP	0x00000008
#define	GEM_MAC_RX_CRC_EXP	0x00000010
#define	GEM_MAC_RX_LEN_EXP	0x00000020
#define	GEM_MAC_RX_CVI_EXP	0x00000040	/* Code violation */

/* GEM_MAC_CONTROL_STATUS and GEM_MAC_CONTROL_MASK register bits */
#define	GEM_MAC_PAUSED		0x00000001	/* Pause received */
#define	GEM_MAC_PAUSE		0x00000002	/* enter pause state */
#define	GEM_MAC_RESUME		0x00000004	/* exit pause state */
#define	GEM_MAC_PAUSE_TIME_SLTS	0xffff0000	/* pause time in slots */
#define	GEM_MAC_STATUS_BITS	"\177\020b\0PAUSED\0b\1PAUSE\0b\2RESUME\0\0"

#define	GEM_MAC_PAUSE_TIME_SHFT	16
#define	GEM_MAC_PAUSE_TIME(x)						\
	(((x) & GEM_MAC_PAUSE_TIME_SLTS) >> GEM_MAC_PAUSE_TIME_SHFT)

/* GEM_MAC_XIF_CONFIG register bits */
#define	GEM_MAC_XIF_TX_MII_ENA	0x00000001	/* Enable XIF output drivers */
#define	GEM_MAC_XIF_MII_LOOPBK	0x00000002	/* Enable MII loopback mode */
#define	GEM_MAC_XIF_ECHO_DISABL	0x00000004	/* Disable echo */
#define	GEM_MAC_XIF_GMII_MODE	0x00000008	/* Select GMII/MII mode */
#define	GEM_MAC_XIF_MII_BUF_ENA	0x00000010	/* Enable MII recv buffers */
#define	GEM_MAC_XIF_LINK_LED	0x00000020	/* force link LED active */
#define	GEM_MAC_XIF_FDPLX_LED	0x00000040	/* force FDPLX LED active */
#define	GEM_MAC_XIF_BITS	"\177\020b\0TXMIIENA\0b\1MIILOOP\0b\2NOECHO" \
				"\0b\3GMII\0b\4MIIBUFENA\0b\5LINKLED\0" \
				"b\6FDLED\0\0"

/*
 * GEM_MAC_SLOT_TIME register
 * The slot time is used as PAUSE time unit, value depends on whether carrier
 * extension is enabled.
 */
#define	GEM_MAC_SLOT_TIME_CARR_EXTEND	0x200
#define	GEM_MAC_SLOT_TIME_NORMAL	0x40

/* GEM_MAC_TX_CONFIG register bits */
#define	GEM_MAC_TX_ENABLE	0x00000001	/* TX enable */
#define	GEM_MAC_TX_IGN_CARRIER	0x00000002	/* Ignore carrier sense */
#define	GEM_MAC_TX_IGN_COLLIS	0x00000004	/* ignore collisions */
#define	GEM_MAC_TX_ENA_IPG0	0x00000008	/* extend RX-to-TX IPG */
#define	GEM_MAC_TX_NGU		0x00000010	/* Never give up */
#define	GEM_MAC_TX_NGU_LIMIT	0x00000020	/* Never give up limit */
#define	GEM_MAC_TX_NO_BACKOFF	0x00000040
#define	GEM_MAC_TX_SLOWDOWN	0x00000080
#define	GEM_MAC_TX_NO_FCS	0x00000100	/* no FCS will be generated */
#define	GEM_MAC_TX_CARR_EXTEND	0x00000200	/* Ena TX Carrier Extension */
/* Carrier Extension is required for half duplex Gbps operation. */
#define	GEM_MAC_TX_CONFIG_BITS	"\177\020" \
				"b\0TXENA\0b\1IGNCAR\0b\2IGNCOLLIS\0" \
				"b\3IPG0ENA\0b\4TXNGU\0b\5TXNGULIM\0" \
				"b\6NOBKOFF\0b\7SLOWDN\0b\x8NOFCS\0" \
				"b\x9TXCARREXT\0\0"

/* GEM_MAC_RX_CONFIG register bits */
#define	GEM_MAC_RX_ENABLE	0x00000001	/* RX enable */
#define	GEM_MAC_RX_STRIP_PAD	0x00000002	/* strip pad bytes */
#define	GEM_MAC_RX_STRIP_CRC	0x00000004
#define	GEM_MAC_RX_PROMISCUOUS	0x00000008	/* promiscuous mode */
#define	GEM_MAC_RX_PROMISC_GRP	0x00000010	/* promiscuous group mode */
#define	GEM_MAC_RX_HASH_FILTER	0x00000020	/* enable hash filter */
#define	GEM_MAC_RX_ADDR_FILTER	0x00000040	/* enable address filter */
#define	GEM_MAC_RX_ERRCHK_DIS	0x00000080	/* disable error checking */
#define	GEM_MAC_RX_CARR_EXTEND	0x00000100	/* Ena RX Carrier Extension */
/*
 * Carrier Extension enables reception of packet bursts generated by
 * senders with carrier extension enabled.
 */
#define	GEM_MAC_RX_CONFIG_BITS	"\177\020" \
				"b\0RXENA\0b\1STRPAD\0b\2STRCRC\0" \
				"b\3PROMIS\0b\4PROMISCGRP\0b\5HASHFLTR\0" \
				"b\6ADDRFLTR\0b\7ERRCHKDIS\0b\x9TXCARREXT\0\0"

/* GEM_MAC_CONTROL_CONFIG bits */
#define	GEM_MAC_CC_TX_PAUSE	0x00000001	/* send pause enabled */
#define	GEM_MAC_CC_RX_PAUSE	0x00000002	/* receive pause enabled */
#define	GEM_MAC_CC_PASS_PAUSE	0x00000004	/* pass pause up */
#define	GEM_MAC_CC_BITS		"\177\020b\0TXPAUSE\0b\1RXPAUSE\0b\2NOPAUSE\0\0"

/*
 * MIF registers
 * Bit bang registers use low bit only.
 */
#define	GEM_MIF_BB_CLOCK	0x6200		/* bit bang clock */
#define	GEM_MIF_BB_DATA		0x6204		/* bit bang data */
#define	GEM_MIF_BB_OUTPUT_ENAB	0x6208
#define	GEM_MIF_FRAME		0x620c		/* MIF frame - ctl and data */
#define	GEM_MIF_CONFIG		0x6210
#define	GEM_MIF_MASK		0x6214
#define	GEM_MIF_STATUS		0x6218
#define	GEM_MIF_STATE_MACHINE	0x621c

/* GEM_MIF_FRAME bits */
#define	GEM_MIF_FRAME_DATA	0x0000ffff
#define	GEM_MIF_FRAME_TA0	0x00010000	/* TA LSB, 1 for completion */
#define	GEM_MIF_FRAME_TA1	0x00020000	/* TA MSB, 1 for instruction */
#define	GEM_MIF_FRAME_REG_ADDR	0x007c0000
#define	GEM_MIF_FRAME_PHY_ADDR	0x0f800000	/* PHY address */
#define	GEM_MIF_FRAME_OP	0x30000000	/* operation - write/read */
#define	GEM_MIF_FRAME_START	0xc0000000	/* START bits */

#define	GEM_MIF_FRAME_READ	0x60020000
#define	GEM_MIF_FRAME_WRITE	0x50020000

#define	GEM_MIF_REG_SHIFT	18
#define	GEM_MIF_PHY_SHIFT	23

/* GEM_MIF_CONFIG register bits */
#define	GEM_MIF_CONFIG_PHY_SEL	0x00000001	/* PHY select, 0: MDIO_0 */
#define	GEM_MIF_CONFIG_POLL_ENA	0x00000002	/* poll enable */
#define	GEM_MIF_CONFIG_BB_ENA	0x00000004	/* bit bang enable */
#define	GEM_MIF_CONFIG_REG_ADR	0x000000f8	/* poll register address */
#define	GEM_MIF_CONFIG_MDI0	0x00000100	/* MDIO_0 attached/data */
#define	GEM_MIF_CONFIG_MDI1	0x00000200	/* MDIO_1 attached/data */
#define	GEM_MIF_CONFIG_PHY_ADR	0x00007c00	/* poll PHY address */
/* MDI0 is the onboard transceiver, MDI1 is external, PHYAD for both is 0. */
#define	GEM_MIF_CONFIG_BITS	"\177\020b\0PHYSEL\0b\1POLL\0b\2BBENA\0" \
				"b\x8MDIO0\0b\x9MDIO1\0\0"

/* GEM_MIF_STATUS and GEM_MIF_MASK bits */
#define	GEM_MIF_POLL_STATUS_MASK	0x0000ffff	/* polling status */
#define	GEM_MIF_POLL_STATUS_SHFT	0
#define	GEM_MIF_POLL_DATA_MASK		0xffff0000	/* polling data */
#define	GEM_MIF_POLL_DATA_SHFT		8
/*
 * The Basic part is the last value read in the POLL field of the config
 * register.
 * The status part indicates the bits that have changed.
 */

/* GEM PCS/Serial link registers */
/* DO NOT TOUCH THESE REGISTERS ON ERI -- IT HARD HANGS. */
#define	GEM_MII_CONTROL		0x9000
#define	GEM_MII_STATUS		0x9004
#define	GEM_MII_ANAR		0x9008		/* MII advertisement reg */
#define	GEM_MII_ANLPAR		0x900c		/* Link Partner Ability Reg */
#define	GEM_MII_CONFIG		0x9010
#define	GEM_MII_STATE_MACHINE	0x9014
#define	GEM_MII_INTERRUP_STATUS	0x9018		/* PCS interrupt state */
#define	GEM_MII_DATAPATH_MODE	0x9050
#define	GEM_MII_SLINK_CONTROL	0x9054		/* Serial link control */
#define	GEM_MII_OUTPUT_SELECT	0x9058
#define	GEM_MII_SLINK_STATUS	0x905c		/* Serialink status */

/* GEM_MII_CONTROL bits - PCS "BMCR" (Basic Mode Control Reg) */
#define	GEM_MII_CONTROL_1000M	0x00000040	/* 1000Mbps speed select */
#define	GEM_MII_CONTROL_COL_TST	0x00000080	/* collision test */
#define	GEM_MII_CONTROL_FDUPLEX	0x00000100	/* full-duplex, always 0 */
#define	GEM_MII_CONTROL_RAN	0x00000200	/* restart auto-negotiation */
#define	GEM_MII_CONTROL_ISOLATE	0x00000400	/* isolate PHY from MII */
#define	GEM_MII_CONTROL_POWERDN	0x00000800	/* power down */
#define	GEM_MII_CONTROL_AUTONEG	0x00001000	/* auto-negotiation enable */
#define	GEM_MII_CONTROL_10_100M	0x00002000	/* 10/100Mbps speed select */
#define	GEM_MII_CONTROL_LOOPBK	0x00004000	/* 10-bit i/f loopback */
#define	GEM_MII_CONTROL_RESET	0x00008000	/* Reset PCS. */
#define	GEM_MII_CONTROL_BITS	"\177\020b\7COLTST\0b\x8_FD\0b\x9RAN\0" \
				"b\xaISOLATE\0b\xbPWRDWN\0b\xc_ANEG\0" \
				"b\xdGIGE\0b\xeLOOP\0b\xfRESET\0\0"

/* GEM_MII_STATUS reg - PCS "BMSR" (Basic Mode Status Reg) */
#define	GEM_MII_STATUS_EXTCAP	0x00000001	/* extended capability */
#define	GEM_MII_STATUS_JABBER	0x00000002	/* jabber condition detected */
#define	GEM_MII_STATUS_LINK_STS	0x00000004	/* link status */
#define	GEM_MII_STATUS_ACFG	0x00000008	/* can auto-negotiate */
#define	GEM_MII_STATUS_REM_FLT	0x00000010	/* remote fault detected */
#define	GEM_MII_STATUS_ANEG_CPT	0x00000020	/* auto-negotiate complete */
#define	GEM_MII_STATUS_EXTENDED	0x00000100	/* extended status */
#define	GEM_MII_STATUS_BITS	"\177\020b\0EXTCAP\0b\1JABBER\0b\2LINKSTS\0" \
				"b\3ACFG\0b\4REMFLT\0b\5ANEGCPT\0\0"

/* GEM_MII_ANAR and GEM_MII_ANLPAR reg bits */
#define	GEM_MII_ANEG_FDUPLX	0x00000020	/* full-duplex */
#define	GEM_MII_ANEG_HDUPLX	0x00000040	/* half-duplex */
#define	GEM_MII_ANEG_PAUSE	0x00000080	/* symmetric PAUSE */
#define	GEM_MII_ANEG_ASM_DIR	0x00000100	/* asymmetric PAUSE */
#define	GEM_MII_ANEG_RFLT_FAIL	0x00001000	/* remote fault - fail */
#define	GEM_MII_ANEG_RFLT_OFF	0x00002000	/* remote fault - off-line */
#define	GEM_MII_ANEG_RFLT_MASK						\
(CAS_PCS_ANEG_RFLT_FAIL | CAS_PCS_ANEG_RFLT_OFF)
#define	GEM_MII_ANEG_ACK	0x00004000	/* acknowledge */
#define	GEM_MII_ANEG_NP		0x00008000	/* next page */
#define	GEM_MII_ANEG_BITS	"\177\020b\5FDX\0b\6HDX\0b\7SYMPAUSE\0" \
				"\b\x8_ASYMPAUSE\0\b\xdREMFLT\0\b\xeLPACK\0" \
				"\b\xfNPBIT\0\0"

/* GEM_MII_CONFIG reg */
#define	GEM_MII_CONFIG_ENABLE	0x00000001	/* Enable PCS. */
#define	GEM_MII_CONFIG_SDO	0x00000002	/* signal detect override */
#define	GEM_MII_CONFIG_SDL	0x00000004	/* signal detect active-low */
#define	GEM_MII_CONFIG_JS_NORM	0x00000000	/* jitter study - normal op. */
#define	GEM_MII_CONFIG_JS_HF	0x00000008	/* jitter study - HF test */
#define	GEM_MII_CONFIG_JS_LF	0x00000010	/* jitter study - LF test */
#define	GEM_MII_CONFIG_JS_MASK						\
	(GEM_MII_CONFIG_JS_HF | GEM_MII_CONFIG_JS_LF)
#define	GEM_MII_CONFIG_ANTO	0x00000020	/* auto-neg. timer override */
#define	GEM_MII_CONFIG_BITS	"\177\020b\0PCSENA\0\0"

/*
 * GEM_MII_INTERRUP_STATUS reg
 * No mask register; mask with the global interrupt mask register.
 */
#define	GEM_MII_INTERRUP_LINK	0x00000004	/* PCS link status change */

/* GEM_MII_DATAPATH_MODE reg */
#define	GEM_MII_DATAPATH_SERIAL	0x00000001	/* Serialink */
#define	GEM_MII_DATAPATH_SERDES	0x00000002	/* SERDES via 10-bit */
#define	GEM_MII_DATAPATH_MII	0x00000004	/* GMII/MII */
#define	GEM_MII_DATAPATH_GMIIOE	0x00000008	/* serial output on GMII en. */
#define	GEM_MII_DATAPATH_BITS	"\177\020"	\
				"b\0SERIAL\0b\1SERDES\0b\2MII\0b\3GMIIOE\0\0"

/* GEM_MII_SLINK_CONTROL reg */
#define	GEM_MII_SLINK_LOOPBACK	0x00000001	/* enable loopback at SL, logic
						 * reversed for SERDES */
#define	GEM_MII_SLINK_EN_SYNC_D	0x00000002	/* enable sync detection */
#define	GEM_MII_SLINK_LOCK_REF	0x00000004	/* lock to reference clock */
#define	GEM_MII_SLINK_EMPHASIS	0x00000018	/* enable emphasis */
#define	GEM_MII_SLINK_SELFTEST	0x000001c0	/* self-test */
#define	GEM_MII_SLINK_POWER_OFF	0x00000200	/* Power down Serialink. */
#define	GEM_MII_SLINK_RX_ZERO	0x00000c00	/* PLL input to Serialink. */
#define	GEM_MII_SLINK_RX_POLE	0x00003000	/* PLL input to Serialink. */
#define	GEM_MII_SLINK_TX_ZERO	0x0000c000	/* PLL input to Serialink. */
#define	GEM_MII_SLINK_TX_POLE	0x00030000	/* PLL input to Serialink. */
#define	GEM_MII_SLINK_CONTROL_BITS		\
				"\177\020b\0LOOP\0b\1ENASYNC\0b\2LOCKREF" \
				"\0b\3EMPHASIS\0b\x9PWRDWN\0\0"

/* GEM_MII_SLINK_STATUS reg */
#define	GEM_MII_SLINK_TEST	0x00000000	/* undergoing test */
#define	GEM_MII_SLINK_LOCKED	0x00000001	/* waiting 500us w/ lockrefn */
#define	GEM_MII_SLINK_COMMA	0x00000002	/* waiting for comma detect */
#define	GEM_MII_SLINK_SYNC	0x00000003	/* recv data synchronized */

/*
 * PCI Expansion ROM runtime access
 * Sun GEMs map a 1MB space for the PCI Expansion ROM as the second half
 * of the first register bank, although they only support up to 64KB ROMs.
 */
#define	GEM_PCI_ROM_OFFSET	0x100000
#define	GEM_PCI_ROM_SIZE	0x10000

/* Wired PHY addresses */
#define	GEM_PHYAD_INTERNAL	1
#define	GEM_PHYAD_EXTERNAL	0

/* Miscellaneous */
#define	GEM_ERI_CACHE_LINE_SIZE	16
#define	GEM_ERI_LATENCY_TIMER	64

/*
 * descriptor table structures
 */
struct gem_desc {
	uint64_t	gd_flags;
	uint64_t	gd_addr;
};

/*
 * Transmit flags
 * GEM_TD_CXSUM_ENABLE, GEM_TD_CXSUM_START, GEM_TD_CXSUM_STUFF and
 * GEM_TD_INTERRUPT_ME only need to be set in the first descriptor of a group.
 */
#define	GEM_TD_BUFSIZE		0x0000000000007fffULL
#define	GEM_TD_CXSUM_START	0x00000000001f8000ULL	/* Cxsum start offset */
#define	GEM_TD_CXSUM_STARTSHFT	15
#define	GEM_TD_CXSUM_STUFF	0x000000001fe00000ULL	/* Cxsum stuff offset */
#define	GEM_TD_CXSUM_STUFFSHFT	21
#define	GEM_TD_CXSUM_ENABLE	0x0000000020000000ULL	/* Cxsum generation enable */
#define	GEM_TD_END_OF_PACKET	0x0000000040000000ULL
#define	GEM_TD_START_OF_PACKET	0x0000000080000000ULL
#define	GEM_TD_INTERRUPT_ME	0x0000000100000000ULL	/* Interrupt me now */
#define	GEM_TD_NO_CRC		0x0000000200000000ULL	/* do not insert crc */

/* Receive flags */
#define	GEM_RD_CHECKSUM		0x000000000000ffffULL	/* is the complement */
#define	GEM_RD_BUFSIZE		0x000000007fff0000ULL
#define	GEM_RD_OWN		0x0000000080000000ULL	/* 1 - owned by h/w */
#define	GEM_RD_HASHVAL		0x0ffff00000000000ULL
#define	GEM_RD_HASH_PASS	0x1000000000000000ULL	/* passed hash filter */
#define	GEM_RD_ALTERNATE_MAC	0x2000000000000000ULL	/* Alternate MAC adrs */
#define	GEM_RD_BAD_CRC		0x4000000000000000ULL
#define	GEM_RD_BUFSHIFT		16
#define	GEM_RD_BUFLEN(x)	(((x) & GEM_RD_BUFSIZE) >> GEM_RD_BUFSHIFT)

#endif

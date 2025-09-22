/*	$OpenBSD: gemreg.h,v 1.17 2009/07/12 15:54:32 kettenis Exp $	*/
/*	$NetBSD: gemreg.h,v 1.1 2001/09/16 00:11:43 eeh Exp $ */

/*
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
 */

#ifndef	_IF_GEMREG_H
#define	_IF_GEMREG_H

/* Register definitions for Sun GEM gigabit ethernet */

/*
 * First bank: this registers live at the start of the PCI
 * mapping, and at the start of the second bank of the SBUS
 * version.
 */
#define	GEM_SEB_STATE		0x0000	/* SEB state reg, R/O */
#define	GEM_CONFIG		0x0004	/* config reg */
#define	GEM_STATUS		0x000c	/* status reg */
/* Note: Reading the status reg clears bits 0-6 */
#define	GEM_INTMASK		0x0010
#define	GEM_INTACK		0x0014	/* Interrupt acknowledge, W/O */
#define	GEM_STATUS_ALIAS	0x001c

/*
 * Second bank: this registers live at offset 0x1000 of the PCI
 * mapping, and at the start of the first bank of the SBUS
 * version.
 */
#define	GEM_PCI_BANK2_OFFSET	0x1000
#define	GEM_PCI_BANK2_SIZE	0x14
/* This is the same as the GEM_STATUS reg but reading it does not clear bits. */
#define	GEM_ERROR_STATUS	0x0000  /* PCI error status R/C */
#define	GEM_SBUS_RESET		0x0000	/* Sbus Reset */
#define	GEM_ERROR_MASK		0x0004
#define	GEM_SBUS_CONFIG		0x0004
#define	GEM_BIF_CONFIG		0x0008  /* BIF config reg */
#define	GEM_BIF_DIAG		0x000c
#define	GEM_RESET		0x0010  /* Software reset register */

/* Bits in GEM_SEB register */
#define	GEM_SEB_ARB		0x000000002	/* Arbitration status */
#define	GEM_SEB_RXWON		0x000000004

/* Bits in GEM_SBUS_CONFIG register */
#define GEM_SBUS_CFG_BSIZE32	0x00000001
#define GEM_SBUS_CFG_BSIZE64	0x00000002
#define GEM_SBUS_CFG_BSIZE128	0x00000004
#define GEM_SBUS_CFG_BMODE64	0x00000008
#define GEM_SBUS_CFG_PARITY	0x00000200

/* Bits in GEM_CONFIG register */
#define	GEM_CONFIG_BURST_64	0x000000000	/* 0->infinity, 1->64KB */
#define	GEM_CONFIG_BURST_INF	0x000000001	/* 0->infinity, 1->64KB */
#define	GEM_CONFIG_TXDMA_LIMIT	0x00000003e
#define	GEM_CONFIG_RXDMA_LIMIT	0x0000007c0
/* GEM_CONFIG_RONPAULBIT and GEM_CONFIG_BUG2FIX are Apple only. */
#define	GEM_CONFIG_RONPAULBIT	0x000000800	/* after infinite burst use
						 * memory read multiple for
						 * PCI commands */
#define	GEM_CONFIG_BUG2FIX	0x000001000	/* fix RX hang after overflow */


#define	GEM_CONFIG_TXDMA_LIMIT_SHIFT	1
#define	GEM_CONFIG_RXDMA_LIMIT_SHIFT	6

/* Top part of GEM_STATUS has TX completion information */
#define	GEM_STATUS_TX_COMPL	0xfff800000	/* TX completion reg. */

/*
 * Interrupt bits, for both the GEM_STATUS and GEM_INTMASK regs.
 * Bits 0-6 auto-clear when read.
 */
#define	GEM_INTR_TX_INTME	0x000000001	/* Frame w/INTME bit set sent */
#define	GEM_INTR_TX_EMPTY	0x000000002	/* TX ring empty */
#define	GEM_INTR_TX_DONE	0x000000004	/* TX complete */
#define	GEM_INTR_RX_DONE	0x000000010	/* Got a packet */
#define	GEM_INTR_RX_NOBUF	0x000000020
#define	GEM_INTR_RX_TAG_ERR	0x000000040
#define	GEM_INTR_PCS		0x000002000	/* Physical Code Sub-layer */
#define	GEM_INTR_TX_MAC		0x000004000
#define	GEM_INTR_RX_MAC		0x000008000
#define	GEM_INTR_MAC_CONTROL	0x000010000	/* MAC control interrupt */
#define	GEM_INTR_MIF		0x000020000
#define	GEM_INTR_BERR		0x000040000	/* Bus error interrupt */
#define GEM_INTR_BITS	"\020"					\
			"\1INTME\2TXEMPTY\3TXDONE"		\
			"\5RXDONE\6RXNOBUF\7RX_TAG_ERR"		\
			"\16PCS\17TXMAC\20RXMAC"		\
			"\21MACCONTROL\22MIF\23BERR"

/* GEM_ERROR_STATUS and GEM_ERROR_MASK PCI error bits */
#define	GEM_ERROR_STAT_BADACK	0x000000001	/* No ACK64# */
#define	GEM_ERROR_STAT_DTRTO	0x000000002	/* Delayed xaction timeout */
#define	GEM_ERROR_STAT_OTHERS	0x000000004

/* GEM_BIF_CONFIG register bits */
#define	GEM_BIF_CONFIG_SLOWCLK	0x000000001	/* Parity error timing */
#define	GEM_BIF_CONFIG_HOST_64	0x000000002	/* 64-bit host */
#define	GEM_BIF_CONFIG_B64D_DIS	0x000000004	/* no 64-bit data cycle */
#define	GEM_BIF_CONFIG_M66EN	0x000000008

/* GEM_RESET register bits -- TX and RX self clear when complete. */
#define	GEM_RESET_TX		0x000000001	/* Reset TX half */
#define	GEM_RESET_RX		0x000000002	/* Reset RX half */
#define	GEM_RESET_RSTOUT	0x000000004	/* Force PCI RSTOUT# */

/* GEM TX DMA registers */
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
#define	GEM_TX_DATA_PTR		0x2030		/* ETX state machine reg (64-bit)*/

#define	GEM_TX_COMPLETION	0x2100
#define	GEM_TX_FIFO_ADDRESS	0x2104
#define	GEM_TX_FIFO_TAG		0x2108
#define	GEM_TX_FIFO_DATA_LO	0x210c
#define	GEM_TX_FIFO_DATA_HI_T1	0x2110
#define	GEM_TX_FIFO_DATA_HI_T0	0x2114
#define	GEM_TX_FIFO_SIZE	0x2118
#define	GEM_TX_DEBUG		0x3028

/* GEM_TX_CONFIG register bits. */
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

/* GEM RX DMA registers */
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

/* GEM_RX_CONFIG register bits. */
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
#define	GEM_RX_PTH_XON_THRESH	0x07fc0000

/* GEM_RX_BLANKING register bits */
#define	GEM_RX_BLANKING_PACKETS	0x000001ff	/* Delay intr for x packets */
#define	GEM_RX_BLANKING_TIME	0x03fc0000	/* Delay intr for x ticks */
/* One tick is 1048 PCI clocs, or 16us at 66MHz */

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
#define	GEM_MAC_MAC_STATE	0x6134		/* MAC sstate machine reg */

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
#define	GEM_MAC_PAUSE_TIME	0xffff0000

/* GEM_MAC_XIF_CONFIG register bits */
#define	GEM_MAC_XIF_TX_MII_ENA	0x00000001	/* Enable XIF output drivers */
#define	GEM_MAC_XIF_MII_LOOPBK	0x00000002	/* Enable MII loopback mode */
#define	GEM_MAC_XIF_ECHO_DISABL	0x00000004	/* Disable echo */
#define	GEM_MAC_XIF_GMII_MODE	0x00000008	/* Select GMII/MII mode */
#define	GEM_MAC_XIF_MII_BUF_ENA	0x00000010	/* Enable MII recv buffers */
#define	GEM_MAC_XIF_LINK_LED	0x00000020	/* force link LED active */
#define	GEM_MAC_XIF_FDPLX_LED	0x00000040	/* force FDPLX LED active */

/* GEM_MAC_SLOT_TIME register bits */
#define	GEM_MAC_SLOT_INT	0x40
#define	GEM_MAC_SLOT_EXT	0x200		/* external phy */

/* GEM_MAC_TX_CONFIG register bits */
#define	GEM_MAC_TX_ENABLE	0x00000001	/* TX enable */
#define	GEM_MAC_TX_IGN_CARRIER	0x00000002	/* Ignore carrier sense */
#define	GEM_MAC_TX_IGN_COLLIS	0x00000004	/* ignore collisions */
#define	GEM_MAC_TX_ENA_IPG0	0x00000008	/* extend Rx-to-TX IPG */
#define	GEM_MAC_TX_NGU		0x00000010	/* Never give up */
#define	GEM_MAC_TX_NGU_LIMIT	0x00000020	/* Never give up limit */
#define	GEM_MAC_TX_NO_BACKOFF	0x00000040
#define	GEM_MAC_TX_SLOWDOWN	0x00000080
#define	GEM_MAC_TX_NO_FCS	0x00000100	/* no FCS will be generated */
#define	GEM_MAC_TX_CARR_EXTEND	0x00000200	/* Ena TX Carrier Extension */
/* Carrier Extension is required for half duplex Gbps operation */

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

/* GEM_MAC_CONTROL_CONFIG bits */
#define	GEM_MAC_CC_TX_PAUSE	0x00000001	/* send pause enabled */
#define	GEM_MAC_CC_RX_PAUSE	0x00000002	/* receive pause enabled */
#define	GEM_MAC_CC_PASS_PAUSE	0x00000004	/* pass pause up */

/* GEM_MAC_MAC_STATE register bits */
#define GEM_MAC_STATE_OVERFLOW	0x03800000

/* GEM MIF registers */
/* Bit bang registers use low bit only */
#define	GEM_MIF_BB_CLOCK	0x6200		/* bit bang clock */
#define	GEM_MIF_BB_DATA		0x6204		/* bit bang data */
#define	GEM_MIF_BB_OUTPUT_ENAB	0x6208
#define	GEM_MIF_FRAME		0x620c		/* MIF frame - ctl and data */
#define	GEM_MIF_CONFIG		0x6210
#define	GEM_MIF_INTERRUPT_MASK	0x6214
#define	GEM_MIF_BASIC_STATUS	0x6218
#define	GEM_MIF_STATE_MACHINE	0x621c

/* GEM_MIF_FRAME bits */
#define	GEM_MIF_FRAME_DATA	0x0000ffff
#define	GEM_MIF_FRAME_TA0	0x00010000	/* TA bit, 1 for completion */
#define	GEM_MIF_FRAME_TA1	0x00020000	/* TA bits */
#define	GEM_MIF_FRAME_REG_ADDR	0x007c0000
#define	GEM_MIF_FRAME_PHY_ADDR	0x0f800000	/* phy address, should be 0 */
#define	GEM_MIF_FRAME_OP	0x30000000	/* operation - write/read */
#define	GEM_MIF_FRAME_START	0xc0000000	/* START bits */

#define	GEM_MIF_FRAME_READ	0x60020000
#define	GEM_MIF_FRAME_WRITE	0x50020000

#define	GEM_MIF_REG_SHIFT	18
#define	GEM_MIF_PHY_SHIFT	23

/* GEM_MIF_CONFIG register bits */
#define	GEM_MIF_CONFIG_PHY_SEL	0x00000001	/* PHY select, 0=MDIO0 */
#define	GEM_MIF_CONFIG_POLL_ENA	0x00000002	/* poll enable */
#define	GEM_MIF_CONFIG_BB_ENA	0x00000004	/* bit bang enable */
#define	GEM_MIF_CONFIG_REG_ADR	0x000000f8	/* poll register address */
#define	GEM_MIF_CONFIG_MDI0	0x00000100	/* MDIO_0 Data/MDIO_0 atached */
#define	GEM_MIF_CONFIG_MDI1	0x00000200	/* MDIO_1 Data/MDIO_1 atached */
#define	GEM_MIF_CONFIG_PHY_ADR	0x00007c00	/* poll PHY address */
/* MDI0 is onboard transceiver MDI1 is external, PHYAD for both is 0 */

/* GEM_MIF_BASIC_STATUS and GEM_MIF_INTERRUPT_MASK bits */
#define	GEM_MIF_STATUS		0x0000ffff
#define	GEM_MIF_BASIC		0xffff0000
/*
 * The Basic part is the last value read in the POLL field of the config
 * register.
 *
 * The status part indicates the bits that have changed.
 */

/* The GEM PCS/Serial link registers. */
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
#define	GEM_MII_SLINK_STATUS	0x905c		/* serial link status */

/* GEM_MII_CONTROL bits */
/* 
 * DO NOT TOUCH THIS REGISTER ON ERI -- IT HARD HANGS.
 */
#define	GEM_MII_CONTROL_RESET	0x00008000
#define	GEM_MII_CONTROL_LOOPBK	0x00004000	/* 10-bit i/f loopback */
#define	GEM_MII_CONTROL_1000M	0x00002000	/* speed select, always 0 */
#define	GEM_MII_CONTROL_AUTONEG	0x00001000	/* auto negotiation enabled */
#define	GEM_MII_CONTROL_POWERDN	0x00000800
#define	GEM_MII_CONTROL_ISOLATE	0x00000400	/* isolate phy from mii */
#define	GEM_MII_CONTROL_RAN	0x00000200	/* restart auto negotiation */
#define	GEM_MII_CONTROL_FDUPLEX	0x00000100	/* full duplex, always 0 */
#define	GEM_MII_CONTROL_COL_TST	0x00000080	/* collision test */

/* GEM_MII_STATUS reg - PCS "BMSR" (Basic Mode Status Reg) */
#define	GEM_MII_STATUS_GB_FDX	0x00000400	/* can perform GBit FDX */
#define	GEM_MII_STATUS_GB_HDX	0x00000200	/* can perform GBit HDX */
#define	GEM_MII_STATUS_UNK	0x00000100
#define	GEM_MII_STATUS_ANEG_CPT	0x00000020	/* auto negotiate compete */
#define	GEM_MII_STATUS_REM_FLT	0x00000010	/* remote fault detected */
#define	GEM_MII_STATUS_ACFG	0x00000008	/* can auto negotiate */
#define	GEM_MII_STATUS_LINK_STS	0x00000004	/* link status */
#define	GEM_MII_STATUS_JABBER	0x00000002	/* jabber condition detected */
#define	GEM_MII_STATUS_EXTCAP	0x00000001	/* extended register capability */

/* GEM_MII_ANAR and GEM_MII_ANLPAR reg bits */
#define	GEM_MII_ANEG_NP		0x00008000	/* next page bit */
#define	GEM_MII_ANEG_ACK	0x00004000	/* ack reception of */
						/* Link Partner Capability */
#define	GEM_MII_ANEG_RF		0x00003000	/* advertise remote fault cap */
#define	GEM_MII_ANEG_ASYM_PAUSE	0x00000100	/* asymmetric pause */
#define	GEM_MII_ANEG_SYM_PAUSE	0x00000080	/* symmetric pause */
#define	GEM_MII_ANEG_HLF_DUPLX	0x00000040
#define	GEM_MII_ANEG_FUL_DUPLX	0x00000020

/* GEM_MII_CONFIG reg */
#define	GEM_MII_CONFIG_TIMER	0x0000000e	/* link monitor timer values */
#define	GEM_MII_CONFIG_ANTO	0x00000020	/* 10ms ANEG timer override */
#define	GEM_MII_CONFIG_JS	0x00000018	/* Jitter Study, 0 normal
						 * 1 high freq, 2 low freq */
#define	GEM_MII_CONFIG_SDL	0x00000004	/* Signal Detect active low */
#define	GEM_MII_CONFIG_SDO	0x00000002	/* Signal Detect Override */
#define	GEM_MII_CONFIG_ENABLE	0x00000001	/* Enable PCS */

/*
 * GEM_MII_STATE_MACHINE
 * XXX These are best guesses from observed behavior.
 */
#define	GEM_MII_FSM_STOP	0x00000000	/* stopped */
#define	GEM_MII_FSM_RUN		0x00000001	/* running */
#define	GEM_MII_FSM_UNKWN	0x00000100	/* unknown */
#define	GEM_MII_FSM_DONE	0x00000101	/* complete */

/*
 * GEM_MII_INTERRUP_STATUS reg
 * No mask register; mask with the global interrupt mask register.
 */
#define	GEM_MII_INTERRUP_LINK	0x00000002	/* PCS link status change */

/* GEM_MII_DATAPATH_MODE reg */
#define	GEM_MII_DATAPATH_SERIAL	0x00000001	/* Serial link */
#define	GEM_MII_DATAPATH_SERDES	0x00000002	/* Use PCS via 10bit interfac */
#define	GEM_MII_DATAPATH_MII	0x00000004	/* Use {G}MII, not PCS */
#define	GEM_MII_DATAPATH_MIIOUT	0x00000008	/* enable serial output on GMII */

/* GEM_MII_SLINK_CONTROL reg */
#define	GEM_MII_SLINK_LOOPBACK	0x00000001	/* enable loopback at sl, logic
						 * reversed for SERDES */
#define	GEM_MII_SLINK_EN_SYNC_D	0x00000002	/* enable sync detection */
#define	GEM_MII_SLINK_LOCK_REF	0x00000004	/* lock reference clock */
#define	GEM_MII_SLINK_EMPHASIS	0x00000008	/* enable emphasis */
#define	GEM_MII_SLINK_SELFTEST	0x000001c0
#define	GEM_MII_SLINK_POWER_OFF	0x00000200	/* Power down serial link */

/* GEM_MII_SLINK_STATUS reg */
#define	GEM_MII_SLINK_TEST	0x00000000	/* undergoing test */
#define	GEM_MII_SLINK_LOCKED	0x00000001	/* waiting 500us lockrefn */
#define	GEM_MII_SLINK_COMMA	0x00000002	/* waiting for comma detect */
#define	GEM_MII_SLINK_SYNC	0x00000003	/* recv data synchronized */

/* Wired GEM PHY addresses */
#define	GEM_PHYAD_INTERNAL	1
#define	GEM_PHYAD_EXTERNAL	0

/*
 * GEM descriptor table structures.
 */
struct gem_desc {
	uint64_t	gd_flags;
	uint64_t	gd_addr;
};

/* Transmit flags */
#define	GEM_TD_BUFSIZE		0x0000000000007fffLL
#define	GEM_TD_CXSUM_START	0x00000000001f8000LL	/* Cxsum start offset */
#define	GEM_TD_CXSUM_STARTSHFT  15
#define	GEM_TD_CXSUM_STUFF	0x000000001fe00000LL	/* Cxsum stuff offset */
#define	GEM_TD_CXSUM_STUFFSHFT  21
#define	GEM_TD_CXSUM_ENABLE	0x0000000020000000LL	/* Cxsum generation enable */
#define	GEM_TD_END_OF_PACKET	0x0000000040000000LL
#define	GEM_TD_START_OF_PACKET	0x0000000080000000LL
#define	GEM_TD_INTERRUPT_ME	0x0000000100000000LL	/* Interrupt me now */
#define	GEM_TD_NO_CRC		0x0000000200000000LL	/* do not insert crc */
/*
 * Only need to set GEM_TD_CXSUM_ENABLE, GEM_TD_CXSUM_STUFF,
 * GEM_TD_CXSUM_START, and GEM_TD_INTERRUPT_ME in 1st descriptor of a group.
 */

/* Receive flags */
#define	GEM_RD_CHECKSUM		0x000000000000ffffLL	/* is the complement */
#define	GEM_RD_BUFSIZE		0x000000007fff0000LL
#define	GEM_RD_OWN		0x0000000080000000LL	/* 1 - owned by h/w */
#define	GEM_RD_HASHVAL		0x0ffff00000000000LL
#define	GEM_RD_HASH_PASS	0x1000000000000000LL	/* passed hash filter */
#define	GEM_RD_ALTERNATE_MAC	0x2000000000000000LL	/* Alternate MAC adrs */
#define	GEM_RD_BAD_CRC		0x4000000000000000LL

#define	GEM_RD_BUFSHIFT		16
#define	GEM_RD_BUFLEN(x)	(((x)&GEM_RD_BUFSIZE)>>GEM_RD_BUFSHIFT)

#endif /* _IF_GEMREG_H */

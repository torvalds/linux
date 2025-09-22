/*	$OpenBSD: if_casreg.h,v 1.11 2022/01/09 05:42:47 jsg Exp $	*/

/*
 *
 * Copyright (C) 2007 Mark Kettenis.
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

#ifndef	_IF_CASREG_H
#define	_IF_CASREG_H

/*
 * Register definitions for Sun Cassini ethernet controllers.
 */

/*
 * First bank: this registers live at the start of the PCI
 * mapping, and at the start of the second bank of the SBUS
 * version.
 */
#define	CAS_SEB_STATE		0x0000	/* SEB state reg, R/O */
#define	CAS_CONFIG		0x0004	/* config reg */
#define	CAS_STATUS		0x000c	/* status reg */
/* Note: Reading the status reg clears bits 0-6 */
#define	CAS_INTMASK		0x0010
#define	CAS_INTACK		0x0014	/* Interrupt acknowledge, W/O */
#define	CAS_STATUS_ALIAS	0x001c
/* Note: Same as CAS_STATUS but reading it does not clear bits. */

#define	CAS_ERROR_STATUS	0x1000  /* PCI error status R/C */
#define	CAS_ERROR_MASK		0x0004
#define	CAS_BIF_CONFIG		0x0008  /* BIF config reg */
#define	CAS_BIF_DIAG		0x000c
#define	CAS_RESET		0x1010  /* Software reset register */

/* Bits in CAS_SEB register */
#define	CAS_SEB_ARB		0x000000002	/* Arbitration status */
#define	CAS_SEB_RXWON		0x000000004

/* Bits in CAS_CONFIG register */
#define	CAS_CONFIG_BURST_64	0x000000000	/* 0->infinity, 1->64KB */
#define	CAS_CONFIG_BURST_INF	0x000000001	/* 0->infinity, 1->64KB */
#define	CAS_CONFIG_TXDMA_LIMIT	0x00000003e
#define	CAS_CONFIG_RXDMA_LIMIT	0x0000007c0

#define	CAS_CONFIG_TXDMA_LIMIT_SHIFT	1
#define	CAS_CONFIG_RXDMA_LIMIT_SHIFT	6

/* Top part of CAS_STATUS has TX completion information */
#define	CAS_STATUS_TX_COMPL	0xfff800000	/* TX completion reg. */

/*
 * Interrupt bits, for both the CAS_STATUS and CAS_INTMASK regs.
 * Bits 0-6 auto-clear when read.
 */
#define	CAS_INTR_TX_INTME	0x000000001	/* Frame w/INTME bit set sent */
#define	CAS_INTR_TX_EMPTY	0x000000002	/* TX ring empty */
#define	CAS_INTR_TX_DONE	0x000000004	/* TX complete */
#define	CAS_INTR_TX_TAG_ERR	0x000000008
#define	CAS_INTR_RX_DONE	0x000000010	/* Got a packet */
#define	CAS_INTR_RX_NOBUF	0x000000020
#define	CAS_INTR_RX_TAG_ERR	0x000000040
#define	CAS_INTR_RX_COMP_FULL	0x000000080
#define	CAS_INTR_PCS		0x000002000	/* Physical Code Sub-layer */
#define	CAS_INTR_TX_MAC		0x000004000
#define	CAS_INTR_RX_MAC		0x000008000
#define	CAS_INTR_MAC_CONTROL	0x000010000	/* MAC control interrupt */
#define	CAS_INTR_MIF		0x000020000
#define	CAS_INTR_BERR		0x000040000	/* Bus error interrupt */
#define CAS_INTR_BITS	"\020"					\
			"\1INTME\2TXEMPTY\3TXDONE\4TX_TAG_ERR"	\
			"\5RXDONE\6RXNOBUF\7RX_TAG_ERR"		\
			"\10RX_COMP_FULL"			\
			"\16PCS\17TXMAC\20RXMAC"		\
			"\21MACCONTROL\22MIF\23BERR"

/* CAS_ERROR_STATUS and CAS_ERROR_MASK PCI error bits */
#define	CAS_ERROR_STAT_BADACK	0x000000001	/* No ACK64# */
#define	CAS_ERROR_STAT_DTRTO	0x000000002	/* Delayed xaction timeout */
#define	CAS_ERROR_STAT_OTHERS	0x000000004

/* CAS_BIF_CONFIG register bits */
#define	CAS_BIF_CONFIG_SLOWCLK	0x000000001	/* Parity error timing */
#define	CAS_BIF_CONFIG_HOST_64	0x000000002	/* 64-bit host */
#define	CAS_BIF_CONFIG_B64D_DIS	0x000000004	/* no 64-bit data cycle */
#define	CAS_BIF_CONFIG_M66EN	0x000000008

/* CAS_RESET register bits -- TX and RX self clear when complete. */
#define	CAS_RESET_TX		0x000000001	/* Reset TX half */
#define	CAS_RESET_RX		0x000000002	/* Reset RX half */
#define	CAS_RESET_RSTOUT	0x000000004	/* Force PCI RSTOUT# */
#define	CAS_RESET_BLOCK_PCS	0x00000008	/* Block PCS reset */

/* TX DMA registers */
#define	CAS_TX_CONFIG		0x2004

#define	CAS_TX_FIFO_WR_PTR	0x2014		/* FIFO write pointer */
#define	CAS_TX_FIFO_SDWR_PTR	0x2018		/* FIFO shadow write pointer */
#define	CAS_TX_FIFO_RD_PTR	0x201c		/* FIFO read pointer */
#define	CAS_TX_FIFO_SDRD_PTR	0x2020		/* FIFO shadow read pointer */
#define	CAS_TX_FIFO_PKT_CNT	0x2024		/* FIFO packet counter */

#define	CAS_TX_STATE_MACHINE	0x2028		/* ETX state machine reg */
#define	CAS_TX_DATA_PTR		0x2030		/* ETX state machine reg (64-bit)*/

#define	CAS_TX_KICK1		0x2038		/* Write last valid desc + 1 */
#define	CAS_TX_KICK2		0x203c
#define	CAS_TX_KICK3		0x2040
#define	CAS_TX_KICK4		0x2044
#define	CAS_TX_COMPLETION1	0x2048
#define	CAS_TX_COMPLETION2	0x204c
#define	CAS_TX_COMPLETION3	0x2050
#define	CAS_TX_COMPLETION4	0x2054
#define	CAS_TX_RING_PTR_LO1	0x2060
#define	CAS_TX_RING_PTR_HI1	0x2064
#define	CAS_TX_RING_PTR_LO2	0x2068
#define	CAS_TX_RING_PTR_HI2	0x206c
#define	CAS_TX_RING_PTR_LO3	0x2070
#define	CAS_TX_RING_PTR_HI3	0x2074
#define	CAS_TX_RING_PTR_LO4	0x2078
#define	CAS_TX_RING_PTR_HI4	0x207c
#define	CAS_TX_MAXBURST1	0x2080
#define	CAS_TX_MAXBURST2	0x2084
#define	CAS_TX_MAXBURST3	0x2088
#define	CAS_TX_MAXBURST4	0x208c

#define CAS_TX_KICK		CAS_TX_KICK3
#define CAS_TX_COMPLETION	CAS_TX_COMPLETION3
#define CAS_TX_RING_PTR_LO	CAS_TX_RING_PTR_LO3
#define CAS_TX_RING_PTR_HI	CAS_TX_RING_PTR_HI3

#define	CAS_TX_FIFO_ADDRESS	0x2104
#define	CAS_TX_FIFO_TAG		0x2108
#define	CAS_TX_FIFO_DATA_LO	0x210c
#define	CAS_TX_FIFO_DATA_HI_T1	0x2110
#define	CAS_TX_FIFO_DATA_HI_T0	0x2114
#define	CAS_TX_FIFO_SIZE	0x2118
#define	CAS_TX_DEBUG		0x3028

/* CAS_TX_CONFIG register bits. */
#define	CAS_TX_CONFIG_TXDMA_EN	0x00000001	/* TX DMA enable */
#define	CAS_TX_CONFIG_TXRING_SZ	0x0000003c	/* TX ring size */
#define	CAS_TX_CONFIG_PACED	0x00100000	/* TX_all_int modifier */

#define	CAS_RING_SZ_32		0	/* 32 descriptors */
#define	CAS_RING_SZ_64		1
#define	CAS_RING_SZ_128		2
#define	CAS_RING_SZ_256		3
#define	CAS_RING_SZ_512		4
#define	CAS_RING_SZ_1024	5
#define	CAS_RING_SZ_2048	6
#define	CAS_RING_SZ_4096	7
#define	CAS_RING_SZ_8192	8

/* CAS_TX_COMPLETION register bits */
#define	CAS_TX_COMPLETION_MASK	0x00001fff	/* # of last descriptor */

/* RX DMA registers */
#define	CAS_RX_CONFIG		0x4000
#define	CAS_RX_PAGE_SIZE	0x4004
#define	CAS_RX_FIFO_WR_PTR	0x4008		/* FIFO write pointer */
#define	CAS_RX_FIFO_RD_PTR	0x400c		/* FIFO read pointer */
#define	CAS_RX_IPPFIFO_WR_PTR	0x4010		/* IPP FIFO write pointer */
#define	CAS_RX_IPPFIFO_RD_PTR	0x4014		/* IPP FIFO read pointer */
#define	CAS_RX_IPPFIFO_SDWR_PTR	0x4018		/* FIFO shadow write pointer */
#define	CAS_RX_DEBUG		0x401c		/* Debug reg */
#define	CAS_RX_PAUSE_THRESH	0x4020
#define	CAS_RX_KICK		0x4024		/* Write last valid desc + 1 */
#define	CAS_RX_DRING_PTR_LO	0x4028
#define	CAS_RX_DRING_PTR_HI	0x402c
#define	CAS_RX_CRING_PTR_LO	0x4030
#define	CAS_RX_CRING_PTR_HI	0x4034
#define	CAS_RX_COMPLETION	0x4038		/* First pending desc */
#define	CAS_RX_COMP_HEAD	0x403c
#define	CAS_RX_COMP_TAIL	0x4040
#define	CAS_RX_BLANKING		0x4044		/* Interrupt blanking reg */
#define	CAS_RX_RED		0x404c		/* Random Early Detection */

#define	CAS_RX_IPP_PKT_CNT	0x4054		/* IPP packet counter */

#define	CAS_RX_FIFO_ADDRESS	0x4080
#define	CAS_RX_FIFO_TAG		0x4084
#define	CAS_RX_FIFO_DATA_LO	0x4088
#define	CAS_RX_FIFO_DATA_HI_T0	0x408c
#define	CAS_RX_FIFO_DATA_HI_T1	0x4090

/* The following registers only exist on Cassini+. */
#define	CAS_RX_DRING_PTR_LO2	0x4200
#define	CAS_RX_DRING_PTR_HI2	0x4204
#define	CAS_RX_CRING_PTR_LO2	0x4208
#define	CAS_RX_CRING_PTR_HI2	0x420c
#define	CAS_RX_CRING_PTR_LO3	0x4210
#define	CAS_RX_CRING_PTR_HI3	0x4214
#define	CAS_RX_CRING_PTR_LO4	0x4218
#define	CAS_RX_CRING_PTR_HI4	0x421c
#define	CAS_RX_KICK2		0x4220
#define	CAS_RX_COMPLETION2	0x4224
#define	CAS_RX_COMP_HEAD2	0x4228
#define	CAS_RX_COMP_TAIL2	0x422c
#define	CAS_RX_COMP_HEAD3	0x4230
#define	CAS_RX_COMP_TAIL3	0x4234
#define	CAS_RX_COMP_HEAD4	0x4238
#define	CAS_RX_COMP_TAIL4	0x423c

/* CAS_RX_CONFIG register bits. */
#define	CAS_RX_CONFIG_RXDMA_EN	0x00000001	/* RX DMA enable */
#define	CAS_RX_CONFIG_RXDRNG_SZ	0x0000001e	/* RX descriptor ring size */
#define	CAS_RX_CONFIG_RXCRNG_SZ	0x000001e0	/* RX completion ring size */
#define	CAS_RX_CONFIG_BATCH_DIS	0x00000200	/* desc batching disable */
#define	CAS_RX_CONFIG_FBOFF	0x00001c00	/* first byte offset */

#define	CAS_RX_CONFIG_RXDRNG_SZ_SHIFT	1
#define	CAS_RX_CONFIG_RXCRNG_SZ_SHIFT	5
#define	CAS_RX_CONFIG_FBOFF_SHFT	10
#define	CAS_RX_CONFIG_RXDRNG2_SZ_SHIFT	16	/* Cassini+ */

/* CAS_RX_PAGE_SIZE register bits. */
#define	CAS_RX_PAGE_SIZE_SZ	0x00000003	/* Page size */
#define	CAS_RX_PAGE_SIZE_COUNT	0x00007800	/* MTU buffers per page */
#define	CAS_RX_PAGE_SIZE_STRIDE	0x18000000	/* MTU buffer separation */
#define	CAS_RX_PAGE_SIZE_FBOFF	0xc0000000	/* First byte offset */

#define	CAS_RX_PAGE_SIZE_COUNT_SHIFT	11
#define	CAS_RX_PAGE_SIZE_STRIDE_SHIFT	27
#define	CAS_RX_PAGE_SIZE_FBOFF_SHIFT	30

/* CAS_RX_PAUSE_THRESH register bits -- sizes in multiples of 64 bytes */
#define	CAS_RX_PTH_XOFF_THRESH	0x000001ff
#define	CAS_RX_PTH_XON_THRESH	0x07fc0000

/* CAS_RX_BLANKING register bits */
#define	CAS_RX_BLANKING_PACKETS	0x000001ff	/* Delay intr for x packets */
#define	CAS_RX_BLANKING_TIME	0x03fc0000	/* Delay intr for x ticks */
/* One tick is 1048 PCI clocks, or 16us at 66MHz */

/* CAS_MAC registers */
#define	CAS_MAC_TXRESET		0x6000		/* Store 1, cleared when done */
#define	CAS_MAC_RXRESET		0x6004		/* ditto */
#define	CAS_MAC_SEND_PAUSE_CMD	0x6008
#define	CAS_MAC_TX_STATUS	0x6010
#define	CAS_MAC_RX_STATUS	0x6014
#define	CAS_MAC_CONTROL_STATUS	0x6018		/* MAC control status reg */
#define	CAS_MAC_TX_MASK		0x6020		/* TX MAC mask register */
#define	CAS_MAC_RX_MASK		0x6024
#define	CAS_MAC_CONTROL_MASK	0x6028
#define	CAS_MAC_TX_CONFIG	0x6030
#define	CAS_MAC_RX_CONFIG	0x6034
#define	CAS_MAC_CONTROL_CONFIG	0x6038
#define	CAS_MAC_XIF_CONFIG	0x603c
#define	CAS_MAC_IPG0		0x6040		/* inter packet gap 0 */
#define	CAS_MAC_IPG1		0x6044		/* inter packet gap 1 */
#define	CAS_MAC_IPG2		0x6048		/* inter packet gap 2 */
#define	CAS_MAC_SLOT_TIME	0x604c		/* slot time, bits 0-7 */
#define	CAS_MAC_MAC_MIN_FRAME	0x6050
#define	CAS_MAC_MAC_MAX_FRAME	0x6054
#define	CAS_MAC_PREAMBLE_LEN	0x6058
#define	CAS_MAC_JAM_SIZE	0x605c
#define	CAS_MAC_ATTEMPT_LIMIT	0x6060
#define	CAS_MAC_CONTROL_TYPE	0x6064

#define	CAS_MAC_ADDR0		0x6080		/* Normal MAC address 0 */
#define	CAS_MAC_ADDR1		0x6084
#define	CAS_MAC_ADDR2		0x6088
#define	CAS_MAC_ADDR3		0x608c		/* Alternate MAC address 0 */
#define	CAS_MAC_ADDR4		0x6090
#define	CAS_MAC_ADDR5		0x6094
#define	CAS_MAC_ADDR42		0x6128		/* Control MAC address 0 */
#define	CAS_MAC_ADDR43		0x612c
#define	CAS_MAC_ADDR44		0x6130

#define	CAS_MAC_ADDR_FILTER0	0x614c
#define	CAS_MAC_ADDR_FILTER1	0x6150
#define	CAS_MAC_ADDR_FILTER2	0x6154
#define	CAS_MAC_ADR_FLT_MASK1_2	0x6158		/* Address filter mask 1,2 */
#define	CAS_MAC_ADR_FLT_MASK0	0x615c		/* Address filter mask 0 reg */

#define	CAS_MAC_HASH0		0x6160		/* Hash table 0 */
#define	CAS_MAC_HASH1		0x6164
#define	CAS_MAC_HASH2		0x6168
#define	CAS_MAC_HASH3		0x616c
#define	CAS_MAC_HASH4		0x6170
#define	CAS_MAC_HASH5		0x6174
#define	CAS_MAC_HASH6		0x6178
#define	CAS_MAC_HASH7		0x617c
#define	CAS_MAC_HASH8		0x6180
#define	CAS_MAC_HASH9		0x6184
#define	CAS_MAC_HASH10		0x6188
#define	CAS_MAC_HASH11		0x618c
#define	CAS_MAC_HASH12		0x6190
#define	CAS_MAC_HASH13		0x6194
#define	CAS_MAC_HASH14		0x6198
#define	CAS_MAC_HASH15		0x619c

#define	CAS_MAC_NORM_COLL_CNT	0x61a0		/* Normal collision counter */
#define	CAS_MAC_FIRST_COLL_CNT	0x61a4		/* 1st successful collision cntr */
#define	CAS_MAC_EXCESS_COLL_CNT	0x61a8		/* Excess collision counter */
#define	CAS_MAC_LATE_COLL_CNT	0x61ac		/* Late collision counter */
#define	CAS_MAC_DEFER_TMR_CNT	0x61b0		/* defer timer counter */
#define	CAS_MAC_PEAK_ATTEMPTS	0x61b4
#define	CAS_MAC_RX_FRAME_COUNT	0x61b8
#define	CAS_MAC_RX_LEN_ERR_CNT	0x61bc
#define	CAS_MAC_RX_ALIGN_ERR	0x61c0
#define	CAS_MAC_RX_CRC_ERR_CNT	0x61c4
#define	CAS_MAC_RX_CODE_VIOL	0x61c8
#define	CAS_MAC_RANDOM_SEED	0x61cc
#define	CAS_MAC_MAC_STATE	0x61d0		/* MAC sstate machine reg */

/* CAS_MAC_SEND_PAUSE_CMD register bits */
#define	CAS_MAC_PAUSE_CMD_TIME	0x0000ffff
#define	CAS_MAC_PAUSE_CMD_SEND	0x00010000

/* CAS_MAC_TX_STATUS and _MASK register bits */
#define	CAS_MAC_TX_XMIT_DONE	0x00000001
#define	CAS_MAC_TX_UNDERRUN	0x00000002
#define	CAS_MAC_TX_PKT_TOO_LONG	0x00000004
#define	CAS_MAC_TX_NCC_EXP	0x00000008	/* Normal collision cnt exp */
#define	CAS_MAC_TX_ECC_EXP	0x00000010
#define	CAS_MAC_TX_LCC_EXP	0x00000020
#define	CAS_MAC_TX_FCC_EXP	0x00000040
#define	CAS_MAC_TX_DEFER_EXP	0x00000080
#define	CAS_MAC_TX_PEAK_EXP	0x00000100

/* CAS_MAC_RX_STATUS and _MASK register bits */
#define	CAS_MAC_RX_DONE		0x00000001
#define	CAS_MAC_RX_OVERFLOW	0x00000002
#define	CAS_MAC_RX_FRAME_CNT	0x00000004
#define	CAS_MAC_RX_ALIGN_EXP	0x00000008
#define	CAS_MAC_RX_CRC_EXP	0x00000010
#define	CAS_MAC_RX_LEN_EXP	0x00000020
#define	CAS_MAC_RX_CVI_EXP	0x00000040	/* Code violation */

/* CAS_MAC_CONTROL_STATUS and CAS_MAC_CONTROL_MASK register bits */
#define	CAS_MAC_PAUSED		0x00000001	/* Pause received */
#define	CAS_MAC_PAUSE		0x00000002	/* enter pause state */
#define	CAS_MAC_RESUME		0x00000004	/* exit pause state */
#define	CAS_MAC_PAUSE_TIME	0xffff0000

/* CAS_MAC_XIF_CONFIG register bits */
#define	CAS_MAC_XIF_TX_MII_ENA	0x00000001	/* Enable XIF output drivers */
#define	CAS_MAC_XIF_MII_LOOPBK	0x00000002	/* Enable MII loopback mode */
#define	CAS_MAC_XIF_ECHO_DISABL	0x00000004	/* Disable echo */
#define	CAS_MAC_XIF_GMII_MODE	0x00000008	/* Select GMII/MII mode */
#define	CAS_MAC_XIF_MII_BUF_ENA	0x00000010	/* Enable MII recv buffers */
#define	CAS_MAC_XIF_LINK_LED	0x00000020	/* force link LED active */
#define	CAS_MAC_XIF_FDPLX_LED	0x00000040	/* force FDPLX LED active */

/* CAS_MAC_SLOT_TIME register bits */
#define	CAS_MAC_SLOT_INT	0x40
#define	CAS_MAC_SLOT_EXT	0x200		/* external phy */

/* CAS_MAC_TX_CONFIG register bits */
#define	CAS_MAC_TX_ENABLE	0x00000001	/* TX enable */
#define	CAS_MAC_TX_IGN_CARRIER	0x00000002	/* Ignore carrier sense */
#define	CAS_MAC_TX_IGN_COLLIS	0x00000004	/* ignore collisions */
#define	CAS_MAC_TX_ENA_IPG0	0x00000008	/* extend Rx-to-TX IPG */
#define	CAS_MAC_TX_NGU		0x00000010	/* Never give up */
#define	CAS_MAC_TX_NGU_LIMIT	0x00000020	/* Never give up limit */
#define	CAS_MAC_TX_NO_BACKOFF	0x00000040
#define	CAS_MAC_TX_SLOWDOWN	0x00000080
#define	CAS_MAC_TX_NO_FCS	0x00000100	/* no FCS will be generated */
#define	CAS_MAC_TX_CARR_EXTEND	0x00000200	/* Ena TX Carrier Extension */
/* Carrier Extension is required for half duplex Gbps operation */

/* CAS_MAC_RX_CONFIG register bits */
#define	CAS_MAC_RX_ENABLE	0x00000001	/* RX enable */
#define	CAS_MAC_RX_STRIP_PAD	0x00000002	/* strip pad bytes */
#define	CAS_MAC_RX_STRIP_CRC	0x00000004
#define	CAS_MAC_RX_PROMISCUOUS	0x00000008	/* promiscuous mode */
#define	CAS_MAC_RX_PROMISC_GRP	0x00000010	/* promiscuous group mode */
#define	CAS_MAC_RX_HASH_FILTER	0x00000020	/* enable hash filter */
#define	CAS_MAC_RX_ADDR_FILTER	0x00000040	/* enable address filter */
#define	CAS_MAC_RX_ERRCHK_DIS	0x00000080	/* disable error checking */
#define	CAS_MAC_RX_CARR_EXTEND	0x00000100	/* Ena RX Carrier Extension */
/*
 * Carrier Extension enables reception of packet bursts generated by
 * senders with carrier extension enabled.
 */

/* CAS_MAC_CONTROL_CONFIG bits */
#define	CAS_MAC_CC_TX_PAUSE	0x00000001	/* send pause enabled */
#define	CAS_MAC_CC_RX_PAUSE	0x00000002	/* receive pause enabled */
#define	CAS_MAC_CC_PASS_PAUSE	0x00000004	/* pass pause up */

/* Cassini MIF registers */
/* Bit bang registers use low bit only */
#define	CAS_MIF_BB_CLOCK	0x6200		/* bit bang clock */
#define	CAS_MIF_BB_DATA		0x6204		/* bit bang data */
#define	CAS_MIF_BB_OUTPUT_ENAB	0x6208
#define	CAS_MIF_FRAME		0x620c		/* MIF frame - ctl and data */
#define	CAS_MIF_CONFIG		0x6210
#define	CAS_MIF_INTERRUPT_MASK	0x6214
#define	CAS_MIF_BASIC_STATUS	0x6218
#define	CAS_MIF_STATE_MACHINE	0x621c

/* CAS_MIF_FRAME bits */
#define	CAS_MIF_FRAME_DATA	0x0000ffff
#define	CAS_MIF_FRAME_TA0	0x00010000	/* TA bit, 1 for completion */
#define	CAS_MIF_FRAME_TA1	0x00020000	/* TA bits */
#define	CAS_MIF_FRAME_REG_ADDR	0x007c0000
#define	CAS_MIF_FRAME_PHY_ADDR	0x0f800000	/* phy address, should be 0 */
#define	CAS_MIF_FRAME_OP	0x30000000	/* operation - write/read */
#define	CAS_MIF_FRAME_START	0xc0000000	/* START bits */

#define	CAS_MIF_FRAME_READ	0x60020000
#define	CAS_MIF_FRAME_WRITE	0x50020000

#define	CAS_MIF_REG_SHIFT	18
#define	CAS_MIF_PHY_SHIFT	23

/* CAS_MIF_CONFIG register bits */
#define	CAS_MIF_CONFIG_PHY_SEL	0x00000001	/* PHY select, 0=MDIO0 */
#define	CAS_MIF_CONFIG_POLL_ENA	0x00000002	/* poll enable */
#define	CAS_MIF_CONFIG_BB_ENA	0x00000004	/* bit bang enable */
#define	CAS_MIF_CONFIG_REG_ADR	0x000000f8	/* poll register address */
#define	CAS_MIF_CONFIG_MDI0	0x00000100	/* MDIO_0 Data/MDIO_0 atached */
#define	CAS_MIF_CONFIG_MDI1	0x00000200	/* MDIO_1 Data/MDIO_1 atached */
#define	CAS_MIF_CONFIG_PHY_ADR	0x00007c00	/* poll PHY address */
/* MDI0 is onboard transceiver MID1 is external, PHYAD for both is 0 */

/* CAS_MIF_BASIC_STATUS and CAS_MIF_INTERRUPT_MASK bits */
#define	CAS_MIF_STATUS		0x0000ffff
#define	CAS_MIF_BASIC		0xffff0000
/*
 * The Basic part is the last value read in the POLL field of the config
 * register.
 *
 * The status part indicates the bits that have changed.
 */

/* Cassini PCS/Serial link registers */
#define	CAS_MII_CONTROL		0x9000
#define	CAS_MII_STATUS		0x9004
#define	CAS_MII_ANAR		0x9008		/* MII advertisement reg */
#define	CAS_MII_ANLPAR		0x900c		/* Link Partner Ability Reg */
#define	CAS_MII_CONFIG		0x9010
#define	CAS_MII_STATE_MACHINE	0x9014
#define	CAS_MII_INTERRUP_STATUS	0x9018		/* PCS interrupt state */
#define	CAS_MII_DATAPATH_MODE	0x9050
#define	CAS_MII_SLINK_CONTROL	0x9054		/* Serial link control */
#define	CAS_MII_OUTPUT_SELECT	0x9058
#define	CAS_MII_SLINK_STATUS	0x905c		/* serial link status */
#define	CAS_MII_PACKET_COUNT	0x9060

/* CAS_MII_CONTROL bits */
#define	CAS_MII_CONTROL_RESET	0x00008000
#define	CAS_MII_CONTROL_LOOPBK	0x00004000	/* 10-bit i/f loopback */
#define	CAS_MII_CONTROL_1000M	0x00002000	/* speed select, always 0 */
#define	CAS_MII_CONTROL_AUTONEG	0x00001000	/* auto negotiation enabled */
#define	CAS_MII_CONTROL_POWERDN	0x00000800
#define	CAS_MII_CONTROL_ISOLATE	0x00000400	/* isolate phy from mii */
#define	CAS_MII_CONTROL_RAN	0x00000200	/* restart auto negotiation */
#define	CAS_MII_CONTROL_FDUPLEX	0x00000100	/* full duplex, always 0 */
#define	CAS_MII_CONTROL_COL_TST	0x00000080	/* collision test */

/* CAS_MII_STATUS reg - PCS "BMSR" (Basic Mode Status Reg) */
#define	CAS_MII_STATUS_GB_FDX	0x00000400	/* can perform GBit FDX */
#define	CAS_MII_STATUS_GB_HDX	0x00000200	/* can perform GBit HDX */
#define	CAS_MII_STATUS_UNK	0x00000100
#define	CAS_MII_STATUS_ANEG_CPT	0x00000020	/* auto negotiate compete */
#define	CAS_MII_STATUS_REM_FLT	0x00000010	/* remote fault detected */
#define	CAS_MII_STATUS_ACFG	0x00000008	/* can auto negotiate */
#define	CAS_MII_STATUS_LINK_STS	0x00000004	/* link status */
#define	CAS_MII_STATUS_JABBER	0x00000002	/* jabber condition detected */
#define	CAS_MII_STATUS_EXTCAP	0x00000001	/* extended register capability */

/* CAS_MII_ANAR and CAS_MII_ANLPAR reg bits */
#define	CAS_MII_ANEG_NP		0x00008000	/* next page bit */
#define	CAS_MII_ANEG_ACK	0x00004000	/* ack reception of */
						/* Link Partner Capability */
#define	CAS_MII_ANEG_RF		0x00003000	/* advertise remote fault cap */
#define	CAS_MII_ANEG_ASYM_PAUSE	0x00000100	/* asymmetric pause */
#define	CAS_MII_ANEG_SYM_PAUSE	0x00000080	/* symmetric pause */
#define	CAS_MII_ANEG_HLF_DUPLX	0x00000040
#define	CAS_MII_ANEG_FUL_DUPLX	0x00000020

/* CAS_MII_CONFIG reg */
#define	CAS_MII_CONFIG_TIMER	0x0000000e	/* link monitor timer values */
#define	CAS_MII_CONFIG_ANTO	0x00000020	/* 10ms ANEG timer override */
#define	CAS_MII_CONFIG_JS	0x00000018	/* Jitter Study, 0 normal
						 * 1 high freq, 2 low freq */
#define	CAS_MII_CONFIG_SDL	0x00000004	/* Signal Detect active low */
#define	CAS_MII_CONFIG_SDO	0x00000002	/* Signal Detect Override */
#define	CAS_MII_CONFIG_ENABLE	0x00000001	/* Enable PCS */

/*
 * CAS_MII_STATE_MACHINE
 * XXX These are best guesses from observed behavior.
 */
#define	CAS_MII_FSM_STOP	0x00000000	/* stopped */
#define	CAS_MII_FSM_RUN		0x00000001	/* running */
#define	CAS_MII_FSM_UNKWN	0x00000100	/* unknown */
#define	CAS_MII_FSM_DONE	0x00000101	/* complete */

/*
 * CAS_MII_INTERRUP_STATUS reg
 * No mask register; mask with the global interrupt mask register.
 */
#define	CAS_MII_INTERRUP_LINK	0x00000002	/* PCS link status change */

/* CAS_MII_DATAPATH_MODE reg */
#define	CAS_MII_DATAPATH_SERIAL	0x00000001	/* Serial link */
#define	CAS_MII_DATAPATH_SERDES	0x00000002	/* Use PCS via 10bit interfac */
#define	CAS_MII_DATAPATH_MII	0x00000004	/* Use {G}MII, not PCS */
#define	CAS_MII_DATAPATH_MIIOUT	0x00000008	/* enable serial output on GMII */

/* CAS_MII_SLINK_CONTROL reg */
#define	CAS_MII_SLINK_LOOPBACK	0x00000001	/* enable loopback at sl, logic
						 * reversed for SERDES */
#define	CAS_MII_SLINK_EN_SYNC_D	0x00000002	/* enable sync detection */
#define	CAS_MII_SLINK_LOCK_REF	0x00000004	/* lock reference clock */
#define	CAS_MII_SLINK_EMPHASIS	0x00000008	/* enable emphasis */
#define	CAS_MII_SLINK_SELFTEST	0x000001c0
#define	CAS_MII_SLINK_POWER_OFF	0x00000200	/* Power down serial link */

/* CAS_MII_SLINK_STATUS reg */
#define	CAS_MII_SLINK_TEST	0x00000000	/* undergoing test */
#define	CAS_MII_SLINK_LOCKED	0x00000001	/* waiting 500us lockrefn */
#define	CAS_MII_SLINK_COMMA	0x00000002	/* waiting for comma detect */
#define	CAS_MII_SLINK_SYNC	0x00000003	/* recv data synchronized */

/* Wired PHY addresses */
#define	CAS_PHYAD_INTERNAL	1
#define	CAS_PHYAD_EXTERNAL	0

/*
 * Cassini ring structures.
 */

/* Descriptor rings */
struct cas_desc {
	uint64_t	cd_flags;
	uint64_t	cd_addr;
};

/* Transmit flags */
#define	CAS_TD_BUFSIZE		0x0000000000007fffLL
#define	CAS_TD_CXSUM_START	0x00000000001f8000LL	/* Cxsum start offset */
#define	CAS_TD_CXSUM_STARTSHFT  15
#define	CAS_TD_CXSUM_STUFF	0x000000001fe00000LL	/* Cxsum stuff offset */
#define	CAS_TD_CXSUM_STUFFSHFT  21
#define	CAS_TD_CXSUM_ENABLE	0x0000000020000000LL	/* Cxsum generation enable */
#define	CAS_TD_END_OF_PACKET	0x0000000040000000LL
#define	CAS_TD_START_OF_PACKET	0x0000000080000000LL
#define	CAS_TD_INTERRUPT_ME	0x0000000100000000LL	/* Interrupt me now */
#define	CAS_TD_NO_CRC		0x0000000200000000LL	/* do not insert crc */
/*
 * Only need to set CAS_TD_CXSUM_ENABLE, CAS_TD_CXSUM_STUFF,
 * CAS_TD_CXSUM_START, and CAS_TD_INTERRUPT_ME in 1st descriptor of a group.
 */

/* Completion ring */
struct cas_comp {
	u_int64_t	cc_word[4];
};

#define	CAS_RC0_TYPE		0xc000000000000000ULL
#define	CAS_RC0_RELEASE_HDR	0x2000000000000000ULL
#define	CAS_RC0_RELEASE_DATA	0x1000000000000000ULL
#define	CAS_RC0_SPLIT		0x0400000000000000ULL
#define	CAS_RC0_SKIP_MASK	0x0180000000000000ULL
#define	CAS_RC0_SKIP_SHIFT	55
#define CAS_RC0_DATA_IDX_MASK	0x007ffe0000000000ULL
#define CAS_RC0_DATA_IDX_SHIFT	41
#define CAS_RC0_DATA_OFF_MASK	0x000001fff8000000ULL
#define CAS_RC0_DATA_OFF_SHIFT	27
#define CAS_RC0_DATA_LEN_MASK	0x0000000007ffe000ULL
#define CAS_RC0_DATA_LEN_SHIFT	13

#define CAS_RC0_SKIP(w) \
	(((w) & CAS_RC0_SKIP_MASK) >> CAS_RC0_SKIP_SHIFT)
#define CAS_RC0_DATA_IDX(w) \
	(((w) & CAS_RC0_DATA_IDX_MASK) >> CAS_RC0_DATA_IDX_SHIFT)
#define CAS_RC0_DATA_OFF(w) \
	(((w) & CAS_RC0_DATA_OFF_MASK) >> CAS_RC0_DATA_OFF_SHIFT)
#define CAS_RC0_DATA_LEN(w) \
	(((w) & CAS_RC0_DATA_LEN_MASK) >> CAS_RC0_DATA_LEN_SHIFT)

#define CAS_RC1_HDR_IDX_MASK	0xfffc000000000000ULL
#define CAS_RC1_HDR_IDX_SHIFT	50
#define CAS_RC1_HDR_OFF_MASK	0x0003f00000000000ULL
#define CAS_RC1_HDR_OFF_SHIFT	44
#define CAS_RC1_HDR_LEN_MASK	0x00000ff800000000ULL
#define CAS_RC1_HDR_LEN_SHIFT	35

#define CAS_RC1_HDR_IDX(w) \
	(((w) & CAS_RC1_HDR_IDX_MASK) >> CAS_RC1_HDR_IDX_SHIFT)
#define CAS_RC1_HDR_OFF(w) \
	(((w) & CAS_RC1_HDR_OFF_MASK) >> CAS_RC1_HDR_OFF_SHIFT)
#define CAS_RC1_HDR_LEN(w) \
	(((w) & CAS_RC1_HDR_LEN_MASK) >> CAS_RC1_HDR_LEN_SHIFT)

#define	CAS_RC3_OWN		0x0000080000000000ULL /* Owned by hardware */

#endif /* _IF_CASREG_H */

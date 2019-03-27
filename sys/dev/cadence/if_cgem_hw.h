/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012-2013 Thomas Skibo
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Hardware and register defines for Cadence GEM Gigabit Ethernet
 * controller such as the one used in Zynq-7000 SoC.
 *
 * Reference: Zynq-7000 All Programmable SoC Technical Reference Manual.
 * (v1.4) November 16, 2012.  Xilinx doc UG585.  GEM is covered in Ch. 16
 * and register definitions are in appendix B.18.
 */

#ifndef _IF_CGEM_HW_H_
#define _IF_CGEM_HW_H_

/* Cadence GEM hardware register definitions. */
#define CGEM_NET_CTRL			0x000	/* Network Control */
#define   CGEM_NET_CTRL_FLUSH_DPRAM_PKT		(1<<18)
#define   CGEM_NET_CTRL_TX_PFC_PRI_PAUSE_FRAME	(1<<17)
#define   CGEM_NET_CTRL_EN_PFC_PRI_PAUSE_RX	(1<<16)
#define   CGEM_NET_CTRL_STORE_RX_TSTAMP		(1<<15)
#define   CGEM_NET_CTRL_TX_ZEROQ_PAUSE_FRAME	(1<<12)
#define   CGEM_NET_CTRL_TX_PAUSE_FRAME		(1<<11)
#define   CGEM_NET_CTRL_TX_HALT			(1<<10)
#define   CGEM_NET_CTRL_START_TX		(1<<9)
#define   CGEM_NET_CTRL_BACK_PRESSURE		(1<<8)
#define   CGEM_NET_CTRL_WREN_STAT_REGS		(1<<7)
#define   CGEM_NET_CTRL_INCR_STAT_REGS		(1<<6)
#define   CGEM_NET_CTRL_CLR_STAT_REGS		(1<<5)
#define   CGEM_NET_CTRL_MGMT_PORT_EN		(1<<4)
#define   CGEM_NET_CTRL_TX_EN			(1<<3)
#define   CGEM_NET_CTRL_RX_EN			(1<<2)
#define   CGEM_NET_CTRL_LOOP_LOCAL		(1<<1)

#define CGEM_NET_CFG			0x004	/* Netowrk Configuration */
#define   CGEM_NET_CFG_UNIDIR_EN		(1<<31)
#define   CGEM_NET_CFG_IGNORE_IPG_RX_ER		(1<<30)
#define   CGEM_NET_CFG_RX_BAD_PREAMBLE		(1<<29)
#define   CGEM_NET_CFG_IPG_STRETCH_EN		(1<<28)
#define   CGEM_NET_CFG_SGMII_EN			(1<<27)
#define   CGEM_NET_CFG_IGNORE_RX_FCS		(1<<26)
#define   CGEM_NET_CFG_RX_HD_WHILE_TX		(1<<25)
#define   CGEM_NET_CFG_RX_CHKSUM_OFFLD_EN	(1<<24)
#define   CGEM_NET_CFG_DIS_CP_PAUSE_FRAME	(1<<23)
#define   CGEM_NET_CFG_DBUS_WIDTH_32		(0<<21)
#define   CGEM_NET_CFG_DBUS_WIDTH_64		(1<<21)
#define   CGEM_NET_CFG_DBUS_WIDTH_128		(2<<21)
#define   CGEM_NET_CFG_DBUS_WIDTH_MASK		(3<<21)
#define   CGEM_NET_CFG_MDC_CLK_DIV_8		(0<<18)
#define   CGEM_NET_CFG_MDC_CLK_DIV_16		(1<<18)
#define   CGEM_NET_CFG_MDC_CLK_DIV_32		(2<<18)
#define   CGEM_NET_CFG_MDC_CLK_DIV_48		(3<<18)
#define   CGEM_NET_CFG_MDC_CLK_DIV_64		(4<<18)
#define   CGEM_NET_CFG_MDC_CLK_DIV_96		(5<<18)
#define   CGEM_NET_CFG_MDC_CLK_DIV_128		(6<<18)
#define   CGEM_NET_CFG_MDC_CLK_DIV_224		(7<<18)
#define   CGEM_NET_CFG_MDC_CLK_DIV_MASK		(7<<18)
#define   CGEM_NET_CFG_FCS_REMOVE		(1<<17)
#define   CGEM_NET_CFG_LEN_ERR_FRAME_DISC	(1<<16)
#define   CGEM_NET_CFG_RX_BUF_OFFSET_SHFT	14
#define   CGEM_NET_CFG_RX_BUF_OFFSET_MASK	(3<<14)
#define   CGEM_NET_CFG_RX_BUF_OFFSET(n)		((n)<<14)
#define   CGEM_NET_CFG_PAUSE_EN			(1<<13)
#define   CGEM_NET_CFG_RETRY_TEST		(1<<12)
#define   CGEM_NET_CFG_PCS_SEL			(1<<11)
#define   CGEM_NET_CFG_GIGE_EN			(1<<10)
#define   CGEM_NET_CFG_EXT_ADDR_MATCH_EN	(1<<9)
#define   CGEM_NET_CFG_1536RXEN			(1<<8)
#define   CGEM_NET_CFG_UNI_HASH_EN		(1<<7)
#define   CGEM_NET_CFG_MULTI_HASH_EN		(1<<6)
#define   CGEM_NET_CFG_NO_BCAST			(1<<5)
#define   CGEM_NET_CFG_COPY_ALL			(1<<4)
#define   CGEM_NET_CFG_DISC_NON_VLAN		(1<<2)
#define   CGEM_NET_CFG_FULL_DUPLEX		(1<<1)
#define   CGEM_NET_CFG_SPEED100			(1<<0)

#define CGEM_NET_STAT			0x008	/* Network Status */
#define   CGEM_NET_STAT_PFC_PRI_PAUSE_NEG	(1<<6)
#define   CGEM_NET_STAT_PCS_AUTONEG_PAUSE_TX_RES (1<<5)
#define   CGEM_NET_STAT_PCS_AUTONEG_PAUSE_RX_RES (1<<4)
#define   CGEM_NET_STAT_PCS_AUTONEG_DUP_RES	(1<<3)
#define   CGEM_NET_STAT_PHY_MGMT_IDLE		(1<<2)
#define   CGEM_NET_STAT_MDIO_IN_PIN_STATUS	(1<<1)
#define   CGEM_NET_STAT_PCS_LINK_STATE		(1<<0)

#define CGEM_USER_IO			0x00C	/* User I/O */

#define CGEM_DMA_CFG			0x010	/* DMA Config */
#define   CGEM_DMA_CFG_DISC_WHEN_NO_AHB		(1<<24)
#define   CGEM_DMA_CFG_RX_BUF_SIZE_SHIFT	16
#define   CGEM_DMA_CFG_RX_BUF_SIZE_MASK		(0xff<<16)
#define   CGEM_DMA_CFG_RX_BUF_SIZE(sz)		((((sz) + 63) / 64) << 16)
#define   CGEM_DMA_CFG_CHKSUM_GEN_OFFLOAD_EN	(1<<11)
#define   CGEM_DMA_CFG_TX_PKTBUF_MEMSZ_SEL	(1<<10)
#define   CGEM_DMA_CFG_RX_PKTBUF_MEMSZ_SEL_1K	(0<<8)
#define   CGEM_DMA_CFG_RX_PKTBUF_MEMSZ_SEL_2K	(1<<8)
#define   CGEM_DMA_CFG_RX_PKTBUF_MEMSZ_SEL_4K	(2<<8)
#define   CGEM_DMA_CFG_RX_PKTBUF_MEMSZ_SEL_8K	(3<<8)
#define   CGEM_DMA_CFG_RX_PKTBUF_MEMSZ_SEL_MASK	(3<<8)
#define   CGEM_DMA_CFG_AHB_ENDIAN_SWAP_PKT_EN	(1<<7)
#define   CGEM_DMA_CFG_AHB_ENDIAN_SWAP_MGMT_EN	(1<<6)
#define   CGEM_DMA_CFG_AHB_FIXED_BURST_LEN_1	(1<<0)
#define   CGEM_DMA_CFG_AHB_FIXED_BURST_LEN_4	(4<<0)
#define   CGEM_DMA_CFG_AHB_FIXED_BURST_LEN_8	(8<<0)
#define   CGEM_DMA_CFG_AHB_FIXED_BURST_LEN_16	(16<<0)
#define   CGEM_DMA_CFG_AHB_FIXED_BURST_LEN_MASK	(0x1f<<0)

#define CGEM_TX_STAT			0x014	/* Transmit Status */
#define   CGEM_TX_STAT_HRESP_NOT_OK		(1<<8)
#define   CGEM_TX_STAT_LATE_COLL		(1<<7)
#define   CGEM_TX_STAT_UNDERRUN			(1<<6)
#define   CGEM_TX_STAT_COMPLETE			(1<<5)
#define   CGEM_TX_STAT_CORRUPT_AHB_ERR		(1<<4)
#define   CGEM_TX_STAT_GO			(1<<3)
#define   CGEM_TX_STAT_RETRY_LIMIT_EXC		(1<<2)
#define   CGEM_TX_STAT_COLLISION		(1<<1)
#define   CGEM_TX_STAT_USED_BIT_READ		(1<<0)
#define   CGEM_TX_STAT_ALL			0x1ff

#define CGEM_RX_QBAR			0x018	/* Receive Buf Q Base Addr */
#define CGEM_TX_QBAR			0x01C	/* Transmit Buf Q Base Addr */

#define CGEM_RX_STAT			0x020	/* Receive Status */
#define   CGEM_RX_STAT_HRESP_NOT_OK		(1<<3)
#define   CGEM_RX_STAT_OVERRUN			(1<<2)
#define   CGEM_RX_STAT_FRAME_RECD		(1<<1)
#define   CGEM_RX_STAT_BUF_NOT_AVAIL		(1<<0)
#define   CGEM_RX_STAT_ALL			0xf

#define CGEM_INTR_STAT			0x024	/* Interrupt Status */
#define CGEM_INTR_EN			0x028	/* Interrupt Enable */
#define CGEM_INTR_DIS			0x02C	/* Interrupt Disable */
#define CGEM_INTR_MASK			0x030	/* Interrupt Mask */
#define   CGEM_INTR_TSU_SEC_INCR		(1<<26)
#define   CGEM_INTR_PDELAY_RESP_TX		(1<<25)
#define   CGEM_INTR_PDELAY_REQ_TX		(1<<24)
#define   CGEM_INTR_PDELAY_RESP_RX		(1<<23)
#define   CGEM_INTR_PDELAY_REQ_RX		(1<<22)
#define   CGEM_INTR_SYNX_TX			(1<<21)
#define   CGEM_INTR_DELAY_REQ_TX		(1<<20)
#define   CGEM_INTR_SYNC_RX			(1<<19)
#define   CGEM_INTR_DELAY_REQ_RX		(1<<18)
#define   CGEM_INTR_PARTNER_PG_RX		(1<<17)
#define   CGEM_INTR_AUTONEG_COMPL		(1<<16)
#define   CGEM_INTR_EXT_INTR			(1<<15)
#define   CGEM_INTR_PAUSE_TX			(1<<14)
#define   CGEM_INTR_PAUSE_ZERO			(1<<13)
#define   CGEM_INTR_PAUSE_NONZEROQ_RX		(1<<12)
#define   CGEM_INTR_HRESP_NOT_OK		(1<<11)
#define   CGEM_INTR_RX_OVERRUN			(1<<10)
#define   CGEM_INTR_LINK_CHNG			(1<<9)
#define   CGEM_INTR_TX_COMPLETE			(1<<7)
#define   CGEM_INTR_TX_CORRUPT_AHB_ERR		(1<<6)
#define   CGEM_INTR_RETRY_EX_LATE_COLLISION	(1<<5)
#define   CGEM_INTR_TX_USED_READ		(1<<3)
#define   CGEM_INTR_RX_USED_READ		(1<<2)
#define   CGEM_INTR_RX_COMPLETE			(1<<1)
#define   CGEM_INTR_MGMT_SENT			(1<<0)
#define   CGEM_INTR_ALL				0x7FFFEFF

#define CGEM_PHY_MAINT			0x034	/* PHY Maintenenace */
#define   CGEM_PHY_MAINT_CLAUSE_22		(1<<30)
#define   CGEM_PHY_MAINT_OP_SHIFT		28
#define   CGEM_PHY_MAINT_OP_MASK		(3<<28)
#define   CGEM_PHY_MAINT_OP_READ		(2<<28)
#define   CGEM_PHY_MAINT_OP_WRITE		(1<<28)
#define   CGEM_PHY_MAINT_PHY_ADDR_SHIFT		23
#define   CGEM_PHY_MAINT_PHY_ADDR_MASK		(0x1f<<23)
#define   CGEM_PHY_MAINT_REG_ADDR_SHIFT		18
#define   CGEM_PHY_MAINT_REG_ADDR_MASK		(0x1f<<18)
#define   CGEM_PHY_MAINT_MUST_10		(2<<16)
#define   CGEM_PHY_MAINT_DATA_MASK		0xffff

#define CGEM_RX_PAUSEQ			0x038	/* Received Pause Quantum */
#define CGEM_TX_PAUSEQ			0x03C	/* Transmit Puase Quantum */

#define CGEM_HASH_BOT			0x080	/* Hash Reg Bottom [31:0] */
#define CGEM_HASH_TOP			0x084	/* Hash Reg Top [63:32] */
#define CGEM_SPEC_ADDR_LOW(n)		(0x088+(n)*8)	/* Specific Addr low */
#define CGEM_SPEC_ADDR_HI(n)		(0x08C+(n)*8)	/* Specific Addr hi */

#define CGEM_TYPE_ID_MATCH1		0x0A8	/* Type ID Match 1 */
#define   CGEM_TYPE_ID_MATCH_COPY_EN		(1<<31)
#define CGEM_TYPE_ID_MATCH2		0x0AC	/* Type ID Match 2 */
#define CGEM_TYPE_ID_MATCH3		0x0B0	/* Type ID Match 3 */
#define CGEM_TYPE_ID_MATCH4		0x0B4	/* Type ID Match 4 */

#define CGEM_WAKE_ON_LAN		0x0B8	/* Wake on LAN Register */
#define   CGEM_WOL_MULTI_HASH_EN		(1<<19)
#define   CGEM_WOL_SPEC_ADDR1_EN		(1<<18)
#define   CGEM_WOL_ARP_REQ_EN			(1<<17)
#define   CGEM_WOL_MAGIC_PKT_EN			(1<<16)
#define   CGEM_WOL_ARP_REQ_IP_ADDR_MASK		0xffff

#define CGEM_IPG_STRETCH		/* IPG Stretch Register */

#define CGEM_STACKED_VLAN		0x0C0	/* Stacked VLAN Register */
#define   CGEM_STACKED_VLAN_EN			(1<<31)

#define CGEM_TX_PFC_PAUSE		0x0C4	/* Transmit PFC Pause Reg */
#define   CGEM_TX_PFC_PAUSEQ_SEL_SHIFT		8
#define   CGEM_TX_PFC_PAUSEQ_SEL_MASK		(0xff<<8)
#define   CGEM_TX_PFC_PAUSE_PRI_EN_VEC_VAL_MASK 0xff

#define CGEM_SPEC_ADDR1_MASK_BOT	0x0C8	/* Specific Addr Mask1 [31:0]*/
#define CGEM_SPEC_ADDR1_MASK_TOP	0x0CC	/* Specific Addr Mask1[47:32]*/
#define CGEM_MODULE_ID			0x0FC	/* Module ID */
#define CGEM_OCTETS_TX_BOT		0x100	/* Octets xmitted [31:0] */
#define CGEM_OCTETS_TX_TOP		0x104	/* Octets xmitted [47:32] */
#define CGEM_FRAMES_TX			0x108	/* Frames xmitted */
#define CGEM_BCAST_FRAMES_TX		0x10C	/* Broadcast Frames xmitted */
#define CGEM_MULTI_FRAMES_TX		0x110	/* Multicast Frames xmitted */
#define CGEM_PAUSE_FRAMES_TX		0x114	/* Pause Frames xmitted */
#define CGEM_FRAMES_64B_TX		0x118	/* 64-Byte Frames xmitted */
#define CGEM_FRAMES_65_127B_TX		0x11C	/* 65-127 Byte Frames xmitted*/
#define CGEM_FRAMES_128_255B_TX		0x120	/* 128-255 Byte Frames xmit */
#define CGEM_FRAMES_256_511B_TX		0x124	/* 256-511 Byte Frames xmit */
#define CGEM_FRAMES_512_1023B_TX	0x128	/* 512-1023 Byte frames xmit */
#define CGEM_FRAMES_1024_1518B_TX	0x12C	/* 1024-1518 Byte frames xmit*/
#define CGEM_TX_UNDERRUNS		0x134	/* Transmit Under-runs */
#define CGEM_SINGLE_COLL_FRAMES		0x138	/* Single-Collision Frames */
#define CGEM_MULTI_COLL_FRAMES		0x13C	/* Multi-Collision Frames */
#define CGEM_EXCESSIVE_COLL_FRAMES	0x140	/* Excessive Collision Frames*/
#define CGEM_LATE_COLL			0x144	/* Late Collisions */
#define CGEM_DEFERRED_TX_FRAMES		0x148	/* Deferred Transmit Frames */
#define CGEM_CARRIER_SENSE_ERRS		0x14C	/* Carrier Sense Errors */
#define CGEM_OCTETS_RX_BOT		0x150	/* Octets Received [31:0] */
#define CGEM_OCTETS_RX_TOP		0x154	/* Octets Received [47:32] */
#define CGEM_FRAMES_RX			0x158	/* Frames Received */
#define CGEM_BCAST_FRAMES_RX		0x15C	/* Broadcast Frames Received */
#define CGEM_MULTI_FRAMES_RX		0x160	/* Multicast Frames Received */
#define CGEM_PAUSE_FRAMES_RX		0x164	/* Pause Frames Reeived */
#define CGEM_FRAMES_64B_RX		0x168	/* 64-Byte Frames Received */
#define CGEM_FRAMES_65_127B_RX		0x16C	/* 65-127 Byte Frames Rx'd */
#define CGEM_FRAMES_128_255B_RX		0x170	/* 128-255 Byte Frames Rx'd */
#define CGEM_FRAMES_256_511B_RX		0x174	/* 256-511 Byte Frames Rx'd */
#define CGEM_FRAMES_512_1023B_RX	0x178	/* 512-1023 Byte Frames Rx'd */
#define CGEM_FRAMES_1024_1518B_RX	0x17C	/* 1024-1518 Byte Frames Rx'd*/
#define CGEM_UNDERSZ_RX			0x184	/* Undersize Frames Rx'd */
#define CGEM_OVERSZ_RX			0x188	/* Oversize Frames Rx'd */
#define CGEM_JABBERS_RX			0x18C	/* Jabbers received */
#define CGEM_FCS_ERRS			0x190	/* Frame Check Sequence Errs */
#define CGEM_LENGTH_FIELD_ERRS		0x194	/* Length Firled Frame Errs */
#define CGEM_RX_SYMBOL_ERRS		0x198	/* Receive Symbol Errs */
#define CGEM_ALIGN_ERRS 		0x19C	/* Alignment Errors */
#define CGEM_RX_RESOURCE_ERRS		0x1A0	/* Receive Resoure Errors */
#define CGEM_RX_OVERRUN_ERRS		0x1A4	/* Receive Overrun Errors */
#define CGEM_IP_HDR_CKSUM_ERRS		0x1A8	/* IP Hdr Checksum Errors */
#define CGEM_TCP_CKSUM_ERRS		0x1AC	/* TCP Checksum Errors */
#define CGEM_UDP_CKSUM_ERRS		0x1B0	/* UDP Checksum Errors */
#define CGEM_TIMER_STROBE_S		0x1C8	/* 1588 timer sync strobe s */
#define CGEM_TIMER_STROBE_NS		0x1CC	/* timer sync strobe ns */
#define CGEM_TIMER_S			0x1D0	/* 1588 timer seconds */
#define CGEM_TIMER_NS			0x1D4	/* 1588 timer ns */
#define CGEM_ADJUST			0x1D8	/* 1588 timer adjust */
#define CGEM_INCR			0x1DC	/* 1588 timer increment */
#define CGEM_PTP_TX_S			0x1E0	/* PTP Event Frame xmit secs */
#define CGEM_PTP_TX_NS			0x1E4	/* PTP Event Frame xmit ns */
#define CGEM_PTP_RX_S			0x1E8	/* PTP Event Frame rcv'd s */
#define CGEM_PTP_RX_NS			0x1EC	/* PTP Event Frame rcv'd ns */
#define CGEM_PTP_PEER_TX_S		0x1F0	/* PTP Peer Event xmit s */
#define CGEM_PTP_PEER_TX_NS		0x1F4	/* PTP Peer Event xmit ns */
#define CGEM_PTP_PEER_RX_S		0x1F8	/* PTP Peer Event rcv'd s */
#define CGEM_PTP_PEER_RX_NS		0x1FC	/* PTP Peer Event rcv'd ns */

#define CGEM_DESIGN_CFG2		0x284	/* Design Configuration 2 */
#define   CGEM_DESIGN_CFG2_TX_PBUF_ADDR_SHIFT	26
#define   CGEM_DESIGN_CFG2_TX_PBUF_ADDR_MASK	(0xf<<26)
#define   CGEM_DESIGN_CFG2_RX_PBUF_ADDR_SHIFT	22
#define   CGEM_DESIGN_CFG2_RX_PBUF_ADDR_MASK	(0xf<<22)
#define   CGEM_DESIGN_CFG2_TX_PKT_BUF		(1<<21)
#define   CGEM_DESIGN_CFG2_RX_PKT_BUF		(1<<20)
#define   CGEM_DESIGN_CFG2_HPROT_VAL_SHIFT	16
#define   CGEM_DESIGN_CFG2_HPROT_VAL_MASK	(0xf<<16)
#define   CGEM_DESIGN_CFG2_JUMBO_MAX_LEN_MASK	0xffff

#define CGEM_DESIGN_CFG3		0x288	/* Design Configuration 3 */
#define   CGEM_DESIGN_CFG3_RX_BASE2_FIFO_SZ_MASK (0xffff<<16)
#define   CGEM_DESIGN_CFG3_RX_BASE2_FIFO_SZ_SHIFT 16
#define   CGEM_DESIGN_CFG3_RX_FIFO_SIZE_MASK	0xffff

#define CGEM_DESIGN_CFG4		0x28C	/* Design Configuration 4 */
#define   CGEM_DESIGN_CFG4_TX_BASE2_FIFO_SZ_SHIFT 16
#define   CGEM_DESIGN_CFG4_TX_BASE2_FIFO_SZ_MASK	(0xffff<<16)
#define   CGEM_DESIGN_CFG4_TX_FIFO_SIZE_MASK	0xffff

#define CGEM_DESIGN_CFG5		0x290	/* Design Configuration 5 */
#define   CGEM_DESIGN_CFG5_TSU_CLK		(1<<28)
#define   CGEM_DESIGN_CFG5_RX_BUF_LEN_DEF_SHIFT 20
#define   CGEM_DESIGN_CFG5_RX_BUF_LEN_DEF_MASK	(0xff<<20)
#define   CGEM_DESIGN_CFG5_TX_PBUF_SIZE_DEF	(1<<19)
#define   CGEM_DESIGN_CFG5_RX_PBUF_SIZE_DEF_SHIFT 17
#define   CGEM_DESIGN_CFG5_RX_PBUF_SIZE_DEF_MASK (3<<17)
#define   CGEM_DESIGN_CFG5_ENDIAN_SWAP_DEF_SHIFT 15
#define   CGEM_DESIGN_CFG5_ENDIAN_SWAP_DEF_MASK (3<<15)
#define   CGEM_DESIGN_CFG5_MDC_CLOCK_DIV_SHIFT	12
#define   CGEM_DESIGN_CFG5_MDC_CLOCK_DIV_MASK	(7<<12)
#define   CGEM_DESIGN_CFG5_DMA_BUS_WIDTH_SHIFT	10
#define   CGEM_DESIGN_CFG5_DMA_BUS_WIDTH_MASK	(3<<10)
#define   CGEM_DESIGN_CFG5_PHY_IDENT		(1<<9)
#define   CGEM_DESIGN_CFG5_TSU			(1<<8)
#define   CGEM_DESIGN_CFG5_TX_FIFO_CNT_WIDTH_SHIFT 4
#define   CGEM_DESIGN_CFG5_TX_FIFO_CNT_WIDTH_MASK (0xf<<4)
#define   CGEM_DESIGN_CFG5_RX_FIFO_CNT_WIDTH_MASK 0xf

/* Transmit Descriptors */
struct cgem_tx_desc {
	uint32_t	addr;
	uint32_t	ctl;
#define CGEM_TXDESC_USED			(1<<31) /* done transmitting */
#define CGEM_TXDESC_WRAP			(1<<30)	/* end of descr ring */
#define CGEM_TXDESC_RETRY_ERR			(1<<29)
#define CGEM_TXDESC_AHB_ERR			(1<<27)
#define CGEM_TXDESC_LATE_COLL			(1<<26)
#define CGEM_TXDESC_CKSUM_GEN_STAT_MASK		(7<<20)
#define CGEM_TXDESC_CKSUM_GEN_STAT_VLAN_HDR_ERR (1<<20)
#define CGEM_TXDESC_CKSUM_GEN_STAT_SNAP_HDR_ERR (2<<20)
#define CGEM_TXDESC_CKSUM_GEN_STAT_IP_HDR_ERR	(3<<20)
#define CGEM_TXDESC_CKSUM_GEN_STAT_UNKNOWN_TYPE (4<<20)
#define CGEM_TXDESC_CKSUM_GEN_STAT_UNSUPP_FRAG	(5<<20)
#define CGEM_TXDESC_CKSUM_GEN_STAT_NOT_TCPUDP	(6<<20)
#define CGEM_TXDESC_CKSUM_GEN_STAT_SHORT_PKT	(7<<20)
#define CGEM_TXDESC_NO_CRC_APPENDED		(1<<16)
#define CGEM_TXDESC_LAST_BUF			(1<<15)	/* last buf in frame */
#define CGEM_TXDESC_LENGTH_MASK		0x3fff
};

struct cgem_rx_desc {
	uint32_t	addr;
#define CGEM_RXDESC_WRAP			(1<<1)	/* goes in addr! */
#define CGEM_RXDESC_OWN				(1<<0)	/* buf filled */
	uint32_t	ctl;
#define CGEM_RXDESC_BCAST			(1<<31)	/* all 1's broadcast */
#define CGEM_RXDESC_MULTI_MATCH			(1<<30)	/* mutlicast match */
#define CGEM_RXDESC_UNICAST_MATCH		(1<<29)
#define CGEM_RXDESC_EXTERNAL_MATCH		(1<<28) /* ext addr match */
#define CGEM_RXDESC_SPEC_MATCH_SHIFT		25
#define CGEM_RXDESC_SPEC_MATCH_MASK		(3<<25)
#define CGEM_RXDESC_TYPE_ID_MATCH_SHIFT		22
#define CGEM_RXDESC_TYPE_ID_MATCH_MASK		(3<<22)
#define CGEM_RXDESC_CKSUM_STAT_MASK		(3<<22)	/* same field above */
#define CGEM_RXDESC_CKSUM_STAT_NONE		(0<<22)
#define CGEM_RXDESC_CKSUM_STAT_IP_GOOD		(1<<22)
#define CGEM_RXDESC_CKSUM_STAT_TCP_GOOD		(2<<22) /* and ip good */
#define CGEM_RXDESC_CKSUM_STAT_UDP_GOOD		(3<<22) /* and ip good */
#define CGEM_RXDESC_VLAN_DETECTED		(1<<21)
#define CGEM_RXDESC_PRIO_DETECTED		(1<<20)
#define CGEM_RXDESC_VLAN_PRIO_SHIFT		17
#define CGEM_RXDESC_VLAN_PRIO_MASK		(7<<17)
#define CGEM_RXDESC_CFI				(1<<16)
#define CGEM_RXDESC_EOF				(1<<15)	/* end of frame */
#define CGEM_RXDESC_SOF				(1<<14) /* start of frame */
#define CGEM_RXDESC_BAD_FCS			(1<<13)
#define CGEM_RXDESC_LENGTH_MASK			0x1fff
};

#endif /* _IF_CGEM_HW_H_ */

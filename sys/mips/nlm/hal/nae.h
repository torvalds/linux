/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef __NLM_NAE_H__
#define	__NLM_NAE_H__

/**
* @file_name nae.h
* @author Netlogic Microsystems
* @brief Basic definitions of XLP Networt Accelerator Engine
*/

/* NAE specific registers */
#define	NAE_REG(blk, intf, reg)	(((blk) << 11) | ((intf) << 7) | (reg))

/* ingress path registers */
#define	NAE_RX_CONFIG			NAE_REG(7, 0, 0x10)
#define	NAE_RX_IF_BASE_CONFIG0		NAE_REG(7, 0, 0x12)
#define	NAE_RX_IF_BASE_CONFIG1		NAE_REG(7, 0, 0x13)
#define	NAE_RX_IF_BASE_CONFIG2		NAE_REG(7, 0, 0x14)
#define	NAE_RX_IF_BASE_CONFIG3		NAE_REG(7, 0, 0x15)
#define	NAE_RX_IF_BASE_CONFIG4		NAE_REG(7, 0, 0x16)
#define	NAE_RX_IF_BASE_CONFIG5		NAE_REG(7, 0, 0x17)
#define	NAE_RX_IF_BASE_CONFIG6		NAE_REG(7, 0, 0x18)
#define	NAE_RX_IF_BASE_CONFIG7		NAE_REG(7, 0, 0x19)
#define	NAE_RX_IF_BASE_CONFIG8		NAE_REG(7, 0, 0x1a)
#define	NAE_RX_IF_BASE_CONFIG9		NAE_REG(7, 0, 0x1b)
#define	NAE_RX_IF_VEC_VALID		NAE_REG(7, 0, 0x1c)
#define	NAE_RX_IF_SLOT_CAL		NAE_REG(7, 0, 0x1d)
#define	NAE_PARSER_CONFIG		NAE_REG(7, 0, 0x1e)
#define	NAE_PARSER_SEQ_FIFO_CFG		NAE_REG(7, 0, 0x1f)
#define	NAE_FREE_IN_FIFO_CFG		NAE_REG(7, 0, 0x20)
#define	NAE_RXBUF_BASE_DPTH_ADDR	NAE_REG(7, 0, 0x21)
#define	NAE_RXBUF_BASE_DPTH		NAE_REG(7, 0, 0x22)
#define	NAE_RX_UCORE_CFG		NAE_REG(7, 0, 0x23)
#define	NAE_RX_UCORE_CAM_MASK0		NAE_REG(7, 0, 0x24)
#define	NAE_RX_UCORE_CAM_MASK1		NAE_REG(7, 0, 0x25)
#define	NAE_RX_UCORE_CAM_MASK2		NAE_REG(7, 0, 0x26)
#define	NAE_RX_UCORE_CAM_MASK3		NAE_REG(7, 0, 0x27)
#define	NAE_FREEIN_FIFO_UNIQ_SZ_CFG	NAE_REG(7, 0, 0x28)
#define	NAE_RX_CRC_POLY0_CFG		NAE_REG(7, 0, 0x2a)
#define	NAE_RX_CRC_POLY1_CFG		NAE_REG(7, 0, 0x2b)
#define	NAE_FREE_SPILL0_MEM_CFG		NAE_REG(7, 0, 0x2c)
#define	NAE_FREE_SPILL1_MEM_CFG		NAE_REG(7, 0, 0x2d)
#define	NAE_FREEFIFO_THRESH_CFG		NAE_REG(7, 0, 0x2e)
#define	NAE_FLOW_CRC16_POLY_CFG		NAE_REG(7, 0, 0x2f)
#define	NAE_EGR_NIOR_CAL_LEN_REG	NAE_REG(7, 0, 0x4e)
#define	NAE_EGR_NIOR_CRDT_CAL_PROG	NAE_REG(7, 0, 0x52)
#define	NAE_TEST			NAE_REG(7, 0, 0x5f)
#define	NAE_BIU_TIMEOUT_CFG		NAE_REG(7, 0, 0x60)
#define	NAE_BIU_CFG			NAE_REG(7, 0, 0x61)
#define	NAE_RX_FREE_FIFO_POP		NAE_REG(7, 0, 0x62)
#define	NAE_RX_DSBL_ECC			NAE_REG(7, 0, 0x63)
#define	NAE_FLOW_BASEMASK_CFG		NAE_REG(7, 0, 0x80)
#define	NAE_POE_CLASS_SETUP_CFG		NAE_REG(7, 0, 0x81)
#define	NAE_UCORE_IFACEMASK_CFG		NAE_REG(7, 0, 0x82)
#define	NAE_RXBUF_XOFFON_THRESH		NAE_REG(7, 0, 0x83)
#define	NAE_FLOW_TABLE1_CFG		NAE_REG(7, 0, 0x84)
#define	NAE_FLOW_TABLE2_CFG		NAE_REG(7, 0, 0x85)
#define	NAE_FLOW_TABLE3_CFG		NAE_REG(7, 0, 0x86)
#define	NAE_RX_FREE_FIFO_THRESH		NAE_REG(7, 0, 0x87)
#define	NAE_RX_PARSER_UNCLA		NAE_REG(7, 0, 0x88)
#define	NAE_RX_BUF_INTR_THRESH		NAE_REG(7, 0, 0x89)
#define	NAE_IFACE_FIFO_CFG		NAE_REG(7, 0, 0x8a)
#define	NAE_PARSER_SEQ_FIFO_THRESH_CFG	NAE_REG(7, 0, 0x8b)
#define	NAE_RX_ERRINJ_CTRL0		NAE_REG(7, 0, 0x8c)
#define	NAE_RX_ERRINJ_CTRL1		NAE_REG(7, 0, 0x8d)
#define	NAE_RX_ERR_LATCH0		NAE_REG(7, 0, 0x8e)
#define	NAE_RX_ERR_LATCH1		NAE_REG(7, 0, 0x8f)
#define	NAE_RX_PERF_CTR_CFG		NAE_REG(7, 0, 0xa0)
#define	NAE_RX_PERF_CTR_VAL		NAE_REG(7, 0, 0xa1)

/* NAE hardware parser registers */
#define	NAE_L2_TYPE_PORT0		NAE_REG(7, 0, 0x210)
#define	NAE_L2_TYPE_PORT1		NAE_REG(7, 0, 0x211)
#define	NAE_L2_TYPE_PORT2		NAE_REG(7, 0, 0x212)
#define	NAE_L2_TYPE_PORT3		NAE_REG(7, 0, 0x213)
#define	NAE_L2_TYPE_PORT4		NAE_REG(7, 0, 0x214)
#define	NAE_L2_TYPE_PORT5		NAE_REG(7, 0, 0x215)
#define	NAE_L2_TYPE_PORT6		NAE_REG(7, 0, 0x216)
#define	NAE_L2_TYPE_PORT7		NAE_REG(7, 0, 0x217)
#define	NAE_L2_TYPE_PORT8		NAE_REG(7, 0, 0x218)
#define	NAE_L2_TYPE_PORT9		NAE_REG(7, 0, 0x219)
#define	NAE_L2_TYPE_PORT10		NAE_REG(7, 0, 0x21a)
#define	NAE_L2_TYPE_PORT11		NAE_REG(7, 0, 0x21b)
#define	NAE_L2_TYPE_PORT12		NAE_REG(7, 0, 0x21c)
#define	NAE_L2_TYPE_PORT13		NAE_REG(7, 0, 0x21d)
#define	NAE_L2_TYPE_PORT14		NAE_REG(7, 0, 0x21e)
#define	NAE_L2_TYPE_PORT15		NAE_REG(7, 0, 0x21f)
#define	NAE_L2_TYPE_PORT16		NAE_REG(7, 0, 0x220)
#define	NAE_L2_TYPE_PORT17		NAE_REG(7, 0, 0x221)
#define	NAE_L2_TYPE_PORT18		NAE_REG(7, 0, 0x222)
#define	NAE_L2_TYPE_PORT19		NAE_REG(7, 0, 0x223)
#define	NAE_L3_CTABLE_MASK0		NAE_REG(7, 0, 0x22c)
#define	NAE_L3_CTABLE_MASK1		NAE_REG(7, 0, 0x22d)
#define	NAE_L3_CTABLE_MASK2		NAE_REG(7, 0, 0x22e)
#define	NAE_L3_CTABLE_MASK3		NAE_REG(7, 0, 0x22f)
#define	NAE_L3CTABLE0			NAE_REG(7, 0, 0x230)
#define	NAE_L3CTABLE1			NAE_REG(7, 0, 0x231)
#define	NAE_L3CTABLE2			NAE_REG(7, 0, 0x232)
#define	NAE_L3CTABLE3			NAE_REG(7, 0, 0x233)
#define	NAE_L3CTABLE4			NAE_REG(7, 0, 0x234)
#define	NAE_L3CTABLE5			NAE_REG(7, 0, 0x235)
#define	NAE_L3CTABLE6			NAE_REG(7, 0, 0x236)
#define	NAE_L3CTABLE7			NAE_REG(7, 0, 0x237)
#define	NAE_L3CTABLE8			NAE_REG(7, 0, 0x238)
#define	NAE_L3CTABLE9			NAE_REG(7, 0, 0x239)
#define	NAE_L3CTABLE10			NAE_REG(7, 0, 0x23a)
#define	NAE_L3CTABLE11			NAE_REG(7, 0, 0x23b)
#define	NAE_L3CTABLE12			NAE_REG(7, 0, 0x23c)
#define	NAE_L3CTABLE13			NAE_REG(7, 0, 0x23d)
#define	NAE_L3CTABLE14			NAE_REG(7, 0, 0x23e)
#define	NAE_L3CTABLE15			NAE_REG(7, 0, 0x23f)
#define	NAE_L4CTABLE0			NAE_REG(7, 0, 0x250)
#define	NAE_L4CTABLE1			NAE_REG(7, 0, 0x251)
#define	NAE_L4CTABLE2			NAE_REG(7, 0, 0x252)
#define	NAE_L4CTABLE3			NAE_REG(7, 0, 0x253)
#define	NAE_L4CTABLE4			NAE_REG(7, 0, 0x254)
#define	NAE_L4CTABLE5			NAE_REG(7, 0, 0x255)
#define	NAE_L4CTABLE6			NAE_REG(7, 0, 0x256)
#define	NAE_L4CTABLE7			NAE_REG(7, 0, 0x257)
#define	NAE_IPV6_EXT_HEADER0		NAE_REG(7, 0, 0x260)
#define	NAE_IPV6_EXT_HEADER1		NAE_REG(7, 0, 0x261)
#define	NAE_VLAN_TYPES01		NAE_REG(7, 0, 0x262)
#define	NAE_VLAN_TYPES23		NAE_REG(7, 0, 0x263)

/* NAE Egress path registers */
#define	NAE_TX_CONFIG			NAE_REG(7, 0, 0x11)
#define	NAE_DMA_TX_CREDIT_TH		NAE_REG(7, 0, 0x29)
#define	NAE_STG1_STG2CRDT_CMD		NAE_REG(7, 0, 0x30)
#define	NAE_STG2_EHCRDT_CMD		NAE_REG(7, 0, 0x32)
#define	NAE_EH_FREECRDT_CMD		NAE_REG(7, 0, 0x34)
#define	NAE_STG2_STRCRDT_CMD		NAE_REG(7, 0, 0x36)
#define	NAE_TXFIFO_IFACEMAP_CMD		NAE_REG(7, 0, 0x38)
#define	NAE_VFBID_DESTMAP_CMD		NAE_REG(7, 0, 0x3a)
#define	NAE_STG1_PMEM_PROG		NAE_REG(7, 0, 0x3c)
#define	NAE_STG2_PMEM_PROG		NAE_REG(7, 0, 0x3e)
#define	NAE_EH_PMEM_PROG		NAE_REG(7, 0, 0x40)
#define	NAE_FREE_PMEM_PROG		NAE_REG(7, 0, 0x42)
#define	NAE_TX_DDR_ACTVLIST_CMD		NAE_REG(7, 0, 0x44)
#define	NAE_TX_IF_BURSTMAX_CMD		NAE_REG(7, 0, 0x46)
#define	NAE_TX_IF_ENABLE_CMD		NAE_REG(7, 0, 0x48)
#define	NAE_TX_PKTLEN_PMEM_CMD		NAE_REG(7, 0, 0x4a)
#define	NAE_TX_SCHED_MAP_CMD0		NAE_REG(7, 0, 0x4c)
#define	NAE_TX_SCHED_MAP_CMD1		NAE_REG(7, 0, 0x4d)
#define	NAE_TX_PKT_PMEM_CMD0		NAE_REG(7, 0, 0x50)
#define	NAE_TX_PKT_PMEM_CMD1		NAE_REG(7, 0, 0x51)
#define	NAE_TX_SCHED_CTRL		NAE_REG(7, 0, 0x53)
#define	NAE_TX_CRC_POLY0		NAE_REG(7, 0, 0x54)
#define	NAE_TX_CRC_POLY1		NAE_REG(7, 0, 0x55)
#define	NAE_TX_CRC_POLY2		NAE_REG(7, 0, 0x56)
#define	NAE_TX_CRC_POLY3		NAE_REG(7, 0, 0x57)
#define	NAE_STR_PMEM_CMD		NAE_REG(7, 0, 0x58)
#define	NAE_TX_IORCRDT_INIT		NAE_REG(7, 0, 0x59)
#define	NAE_TX_DSBL_ECC			NAE_REG(7, 0, 0x5a)
#define	NAE_TX_IORCRDT_IGNORE		NAE_REG(7, 0, 0x5b)
#define	NAE_IF0_1588_TMSTMP_HI		NAE_REG(7, 0, 0x300)
#define	NAE_IF1_1588_TMSTMP_HI		NAE_REG(7, 0, 0x302)
#define	NAE_IF2_1588_TMSTMP_HI		NAE_REG(7, 0, 0x304)
#define	NAE_IF3_1588_TMSTMP_HI		NAE_REG(7, 0, 0x306)
#define	NAE_IF4_1588_TMSTMP_HI		NAE_REG(7, 0, 0x308)
#define	NAE_IF5_1588_TMSTMP_HI		NAE_REG(7, 0, 0x30a)
#define	NAE_IF6_1588_TMSTMP_HI		NAE_REG(7, 0, 0x30c)
#define	NAE_IF7_1588_TMSTMP_HI		NAE_REG(7, 0, 0x30e)
#define	NAE_IF8_1588_TMSTMP_HI		NAE_REG(7, 0, 0x310)
#define	NAE_IF9_1588_TMSTMP_HI		NAE_REG(7, 0, 0x312)
#define	NAE_IF10_1588_TMSTMP_HI		NAE_REG(7, 0, 0x314)
#define	NAE_IF11_1588_TMSTMP_HI		NAE_REG(7, 0, 0x316)
#define	NAE_IF12_1588_TMSTMP_HI		NAE_REG(7, 0, 0x318)
#define	NAE_IF13_1588_TMSTMP_HI		NAE_REG(7, 0, 0x31a)
#define	NAE_IF14_1588_TMSTMP_HI		NAE_REG(7, 0, 0x31c)
#define	NAE_IF15_1588_TMSTMP_HI		NAE_REG(7, 0, 0x31e)
#define	NAE_IF16_1588_TMSTMP_HI		NAE_REG(7, 0, 0x320)
#define	NAE_IF17_1588_TMSTMP_HI		NAE_REG(7, 0, 0x322)
#define	NAE_IF18_1588_TMSTMP_HI		NAE_REG(7, 0, 0x324)
#define	NAE_IF19_1588_TMSTMP_HI		NAE_REG(7, 0, 0x326)
#define	NAE_IF0_1588_TMSTMP_LO		NAE_REG(7, 0, 0x301)
#define	NAE_IF1_1588_TMSTMP_LO		NAE_REG(7, 0, 0x303)
#define	NAE_IF2_1588_TMSTMP_LO		NAE_REG(7, 0, 0x305)
#define	NAE_IF3_1588_TMSTMP_LO		NAE_REG(7, 0, 0x307)
#define	NAE_IF4_1588_TMSTMP_LO		NAE_REG(7, 0, 0x309)
#define	NAE_IF5_1588_TMSTMP_LO		NAE_REG(7, 0, 0x30b)
#define	NAE_IF6_1588_TMSTMP_LO		NAE_REG(7, 0, 0x30d)
#define	NAE_IF7_1588_TMSTMP_LO		NAE_REG(7, 0, 0x30f)
#define	NAE_IF8_1588_TMSTMP_LO		NAE_REG(7, 0, 0x311)
#define	NAE_IF9_1588_TMSTMP_LO		NAE_REG(7, 0, 0x313)
#define	NAE_IF10_1588_TMSTMP_LO		NAE_REG(7, 0, 0x315)
#define	NAE_IF11_1588_TMSTMP_LO		NAE_REG(7, 0, 0x317)
#define	NAE_IF12_1588_TMSTMP_LO		NAE_REG(7, 0, 0x319)
#define	NAE_IF13_1588_TMSTMP_LO		NAE_REG(7, 0, 0x31b)
#define	NAE_IF14_1588_TMSTMP_LO		NAE_REG(7, 0, 0x31d)
#define	NAE_IF15_1588_TMSTMP_LO		NAE_REG(7, 0, 0x31f)
#define	NAE_IF16_1588_TMSTMP_LO		NAE_REG(7, 0, 0x321)
#define	NAE_IF17_1588_TMSTMP_LO		NAE_REG(7, 0, 0x323)
#define	NAE_IF18_1588_TMSTMP_LO		NAE_REG(7, 0, 0x325)
#define	NAE_IF19_1588_TMSTMP_LO		NAE_REG(7, 0, 0x327)
#define	NAE_TX_EL0			NAE_REG(7, 0, 0x328)
#define	NAE_TX_EL1			NAE_REG(7, 0, 0x329)
#define	NAE_EIC0			NAE_REG(7, 0, 0x32a)
#define	NAE_EIC1			NAE_REG(7, 0, 0x32b)
#define	NAE_STG1_STG2CRDT_STATUS	NAE_REG(7, 0, 0x32c)
#define	NAE_STG2_EHCRDT_STATUS		NAE_REG(7, 0, 0x32d)
#define	NAE_STG2_FREECRDT_STATUS	NAE_REG(7, 0, 0x32e)
#define	NAE_STG2_STRCRDT_STATUS		NAE_REG(7, 0, 0x32f)
#define	NAE_TX_PERF_CNTR_INTR_STATUS	NAE_REG(7, 0, 0x330)
#define	NAE_TX_PERF_CNTR_ROLL_STATUS	NAE_REG(7, 0, 0x331)
#define	NAE_TX_PERF_CNTR0		NAE_REG(7, 0, 0x332)
#define	NAE_TX_PERF_CNTR1		NAE_REG(7, 0, 0x334)
#define	NAE_TX_PERF_CNTR2		NAE_REG(7, 0, 0x336)
#define	NAE_TX_PERF_CNTR3		NAE_REG(7, 0, 0x338)
#define	NAE_TX_PERF_CNTR4		NAE_REG(7, 0, 0x33a)
#define	NAE_TX_PERF_CNTR0_CTL		NAE_REG(7, 0, 0x333)
#define	NAE_TX_PERF_CNTR1_CTL		NAE_REG(7, 0, 0x335)
#define	NAE_TX_PERF_CNTR2_CTL		NAE_REG(7, 0, 0x337)
#define	NAE_TX_PERF_CNTR3_CTL		NAE_REG(7, 0, 0x339)
#define	NAE_TX_PERF_CNTR4_CTL		NAE_REG(7, 0, 0x33b)
#define	NAE_VFBID_DESTMAP_STATUS	NAE_REG(7, 0, 0x380)
#define	NAE_STG2_PMEM_STATUS		NAE_REG(7, 0, 0x381)
#define	NAE_EH_PMEM_STATUS		NAE_REG(7, 0, 0x382)
#define	NAE_FREE_PMEM_STATUS		NAE_REG(7, 0, 0x383)
#define	NAE_TX_DDR_ACTVLIST_STATUS	NAE_REG(7, 0, 0x384)
#define	NAE_TX_IF_BURSTMAX_STATUS	NAE_REG(7, 0, 0x385)
#define	NAE_TX_PKTLEN_PMEM_STATUS	NAE_REG(7, 0, 0x386)
#define	NAE_TX_SCHED_MAP_STATUS0	NAE_REG(7, 0, 0x387)
#define	NAE_TX_SCHED_MAP_STATUS1	NAE_REG(7, 0, 0x388)
#define	NAE_TX_PKT_PMEM_STATUS		NAE_REG(7, 0, 0x389)
#define	NAE_STR_PMEM_STATUS		NAE_REG(7, 0, 0x38a)

/* Network interface interrupt registers */
#define	NAE_NET_IF0_INTR_STAT		NAE_REG(7, 0, 0x280)
#define	NAE_NET_IF1_INTR_STAT		NAE_REG(7, 0, 0x282)
#define	NAE_NET_IF2_INTR_STAT		NAE_REG(7, 0, 0x284)
#define	NAE_NET_IF3_INTR_STAT		NAE_REG(7, 0, 0x286)
#define	NAE_NET_IF4_INTR_STAT		NAE_REG(7, 0, 0x288)
#define	NAE_NET_IF5_INTR_STAT		NAE_REG(7, 0, 0x28a)
#define	NAE_NET_IF6_INTR_STAT		NAE_REG(7, 0, 0x28c)
#define	NAE_NET_IF7_INTR_STAT		NAE_REG(7, 0, 0x28e)
#define	NAE_NET_IF8_INTR_STAT		NAE_REG(7, 0, 0x290)
#define	NAE_NET_IF9_INTR_STAT		NAE_REG(7, 0, 0x292)
#define	NAE_NET_IF10_INTR_STAT		NAE_REG(7, 0, 0x294)
#define	NAE_NET_IF11_INTR_STAT		NAE_REG(7, 0, 0x296)
#define	NAE_NET_IF12_INTR_STAT		NAE_REG(7, 0, 0x298)
#define	NAE_NET_IF13_INTR_STAT		NAE_REG(7, 0, 0x29a)
#define	NAE_NET_IF14_INTR_STAT		NAE_REG(7, 0, 0x29c)
#define	NAE_NET_IF15_INTR_STAT		NAE_REG(7, 0, 0x29e)
#define	NAE_NET_IF16_INTR_STAT		NAE_REG(7, 0, 0x2a0)
#define	NAE_NET_IF17_INTR_STAT		NAE_REG(7, 0, 0x2a2)
#define	NAE_NET_IF18_INTR_STAT		NAE_REG(7, 0, 0x2a4)
#define	NAE_NET_IF19_INTR_STAT		NAE_REG(7, 0, 0x2a6)
#define	NAE_NET_IF0_INTR_MASK		NAE_REG(7, 0, 0x281)
#define	NAE_NET_IF1_INTR_MASK		NAE_REG(7, 0, 0x283)
#define	NAE_NET_IF2_INTR_MASK		NAE_REG(7, 0, 0x285)
#define	NAE_NET_IF3_INTR_MASK		NAE_REG(7, 0, 0x287)
#define	NAE_NET_IF4_INTR_MASK		NAE_REG(7, 0, 0x289)
#define	NAE_NET_IF5_INTR_MASK		NAE_REG(7, 0, 0x28b)
#define	NAE_NET_IF6_INTR_MASK		NAE_REG(7, 0, 0x28d)
#define	NAE_NET_IF7_INTR_MASK		NAE_REG(7, 0, 0x28f)
#define	NAE_NET_IF8_INTR_MASK		NAE_REG(7, 0, 0x291)
#define	NAE_NET_IF9_INTR_MASK		NAE_REG(7, 0, 0x293)
#define	NAE_NET_IF10_INTR_MASK		NAE_REG(7, 0, 0x295)
#define	NAE_NET_IF11_INTR_MASK		NAE_REG(7, 0, 0x297)
#define	NAE_NET_IF12_INTR_MASK		NAE_REG(7, 0, 0x299)
#define	NAE_NET_IF13_INTR_MASK		NAE_REG(7, 0, 0x29b)
#define	NAE_NET_IF14_INTR_MASK		NAE_REG(7, 0, 0x29d)
#define	NAE_NET_IF15_INTR_MASK		NAE_REG(7, 0, 0x29f)
#define	NAE_NET_IF16_INTR_MASK		NAE_REG(7, 0, 0x2a1)
#define	NAE_NET_IF17_INTR_MASK		NAE_REG(7, 0, 0x2a3)
#define	NAE_NET_IF18_INTR_MASK		NAE_REG(7, 0, 0x2a5)
#define	NAE_NET_IF19_INTR_MASK		NAE_REG(7, 0, 0x2a7)
#define	NAE_COMMON0_INTR_STAT		NAE_REG(7, 0, 0x2a8)
#define	NAE_COMMON0_INTR_MASK		NAE_REG(7, 0, 0x2a9)
#define	NAE_COMMON1_INTR_STAT		NAE_REG(7, 0, 0x2aa)
#define	NAE_COMMON1_INTR_MASK		NAE_REG(7, 0, 0x2ab)

/* Network Interface Low-block Registers */
#define	NAE_PHY_LANE0_STATUS(block)	NAE_REG(block, 0xe, 0)
#define	NAE_PHY_LANE1_STATUS(block)	NAE_REG(block, 0xe, 1)
#define	NAE_PHY_LANE2_STATUS(block)	NAE_REG(block, 0xe, 2)
#define	NAE_PHY_LANE3_STATUS(block)	NAE_REG(block, 0xe, 3)
#define	NAE_PHY_LANE0_CTRL(block)	NAE_REG(block, 0xe, 4)
#define	NAE_PHY_LANE1_CTRL(block)	NAE_REG(block, 0xe, 5)
#define	NAE_PHY_LANE2_CTRL(block)	NAE_REG(block, 0xe, 6)
#define	NAE_PHY_LANE3_CTRL(block)	NAE_REG(block, 0xe, 7)

/* Network interface Top-block registers */
#define	NAE_LANE_CFG_CPLX_0_1		NAE_REG(7, 0, 0x780)
#define	NAE_LANE_CFG_CPLX_2_3		NAE_REG(7, 0, 0x781)
#define	NAE_LANE_CFG_CPLX_4		NAE_REG(7, 0, 0x782)
#define	NAE_LANE_CFG_SOFTRESET		NAE_REG(7, 0, 0x783)
#define	NAE_1588_PTP_OFFSET_HI		NAE_REG(7, 0, 0x784)
#define	NAE_1588_PTP_OFFSET_LO		NAE_REG(7, 0, 0x785)
#define	NAE_1588_PTP_INC_DEN		NAE_REG(7, 0, 0x786)
#define	NAE_1588_PTP_INC_NUM		NAE_REG(7, 0, 0x787)
#define	NAE_1588_PTP_INC_INTG		NAE_REG(7, 0, 0x788)
#define	NAE_1588_PTP_CONTROL		NAE_REG(7, 0, 0x789)
#define	NAE_1588_PTP_STATUS		NAE_REG(7, 0, 0x78a)
#define	NAE_1588_PTP_USER_VALUE_HI	NAE_REG(7, 0, 0x78b)
#define	NAE_1588_PTP_USER_VALUE_LO	NAE_REG(7, 0, 0x78c)
#define	NAE_1588_PTP_TMR1_HI		NAE_REG(7, 0, 0x78d)
#define	NAE_1588_PTP_TMR1_LO		NAE_REG(7, 0, 0x78e)
#define	NAE_1588_PTP_TMR2_HI		NAE_REG(7, 0, 0x78f)
#define	NAE_1588_PTP_TMR2_LO		NAE_REG(7, 0, 0x790)
#define	NAE_1588_PTP_TMR3_HI		NAE_REG(7, 0, 0x791)
#define	NAE_1588_PTP_TMR3_LO		NAE_REG(7, 0, 0x792)
#define	NAE_TX_FC_CAL_IDX_TBL_CTRL	NAE_REG(7, 0, 0x793)
#define	NAE_TX_FC_CAL_TBL_CTRL		NAE_REG(7, 0, 0x794)
#define	NAE_TX_FC_CAL_TBL_DATA0		NAE_REG(7, 0, 0x795)
#define	NAE_TX_FC_CAL_TBL_DATA1		NAE_REG(7, 0, 0x796)
#define	NAE_TX_FC_CAL_TBL_DATA2		NAE_REG(7, 0, 0x797)
#define	NAE_TX_FC_CAL_TBL_DATA3		NAE_REG(7, 0, 0x798)
#define	NAE_INT_MDIO_CTRL		NAE_REG(7, 0, 0x799)
#define	NAE_INT_MDIO_CTRL_DATA		NAE_REG(7, 0, 0x79a)
#define	NAE_INT_MDIO_RD_STAT		NAE_REG(7, 0, 0x79b)
#define	NAE_INT_MDIO_LINK_STAT		NAE_REG(7, 0, 0x79c)
#define	NAE_EXT_G0_MDIO_CTRL		NAE_REG(7, 0, 0x79d)
#define	NAE_EXT_G1_MDIO_CTRL		NAE_REG(7, 0, 0x7a1)
#define	NAE_EXT_G0_MDIO_CTRL_DATA	NAE_REG(7, 0, 0x79e)
#define	NAE_EXT_G1_MDIO_CTRL_DATA	NAE_REG(7, 0, 0x7a2)
#define	NAE_EXT_G0_MDIO_RD_STAT		NAE_REG(7, 0, 0x79f)
#define	NAE_EXT_G1_MDIO_RD_STAT		NAE_REG(7, 0, 0x7a3)
#define	NAE_EXT_G0_MDIO_LINK_STAT	NAE_REG(7, 0, 0x7a0)
#define	NAE_EXT_G1_MDIO_LINK_STAT	NAE_REG(7, 0, 0x7a4)
#define	NAE_EXT_XG0_MDIO_CTRL		NAE_REG(7, 0, 0x7a5)
#define	NAE_EXT_XG1_MDIO_CTRL		NAE_REG(7, 0, 0x7a9)
#define	NAE_EXT_XG0_MDIO_CTRL_DATA	NAE_REG(7, 0, 0x7a6)
#define	NAE_EXT_XG1_MDIO_CTRL_DATA	NAE_REG(7, 0, 0x7aa)
#define	NAE_EXT_XG0_MDIO_RD_STAT	NAE_REG(7, 0, 0x7a7)
#define	NAE_EXT_XG1_MDIO_RD_STAT	NAE_REG(7, 0, 0x7ab)
#define	NAE_EXT_XG0_MDIO_LINK_STAT	NAE_REG(7, 0, 0x7a8)
#define	NAE_EXT_XG1_MDIO_LINK_STAT	NAE_REG(7, 0, 0x7ac)
#define	NAE_GMAC_FC_SLOT0		NAE_REG(7, 0, 0x7ad)
#define	NAE_GMAC_FC_SLOT1		NAE_REG(7, 0, 0x7ae)
#define	NAE_GMAC_FC_SLOT2		NAE_REG(7, 0, 0x7af)
#define	NAE_GMAC_FC_SLOT3		NAE_REG(7, 0, 0x7b0)
#define	NAE_NETIOR_NTB_SLOT		NAE_REG(7, 0, 0x7b1)
#define	NAE_NETIOR_MISC_CTRL0		NAE_REG(7, 0, 0x7b2)
#define	NAE_NETIOR_INT0			NAE_REG(7, 0, 0x7b3)
#define	NAE_NETIOR_INT0_MASK		NAE_REG(7, 0, 0x7b4)
#define	NAE_NETIOR_INT1			NAE_REG(7, 0, 0x7b5)
#define	NAE_NETIOR_INT1_MASK		NAE_REG(7, 0, 0x7b6)
#define	NAE_GMAC_PFC_REPEAT		NAE_REG(7, 0, 0x7b7)
#define	NAE_XGMAC_PFC_REPEAT		NAE_REG(7, 0, 0x7b8)
#define	NAE_NETIOR_MISC_CTRL1		NAE_REG(7, 0, 0x7b9)
#define	NAE_NETIOR_MISC_CTRL2		NAE_REG(7, 0, 0x7ba)
#define	NAE_NETIOR_INT2			NAE_REG(7, 0, 0x7bb)
#define	NAE_NETIOR_INT2_MASK		NAE_REG(7, 0, 0x7bc)
#define	NAE_NETIOR_MISC_CTRL3		NAE_REG(7, 0, 0x7bd)

/* Network interface lane configuration registers */
#define	NAE_LANE_CFG_MISCREG1		NAE_REG(7, 0xf, 0x39)
#define	NAE_LANE_CFG_MISCREG2		NAE_REG(7, 0xf, 0x3A)

/* Network interface soft reset register */
#define	NAE_SOFT_RESET			NAE_REG(7, 0xf, 3)

/* ucore instruction/shared CAM RAM access */
#define	NAE_UCORE_SHARED_RAM_OFFSET	0x10000

#define	PORTS_PER_CMPLX			4
#define	NAE_CACHELINE_SIZE		64

#define	PHY_LANE_0_CTRL			4
#define	PHY_LANE_1_CTRL			5
#define	PHY_LANE_2_CTRL			6
#define	PHY_LANE_3_CTRL			7

#define	PHY_LANE_STAT_SRCS		0x00000001
#define	PHY_LANE_STAT_STD		0x00000010
#define	PHY_LANE_STAT_SFEA		0x00000020
#define	PHY_LANE_STAT_STCS		0x00000040
#define	PHY_LANE_STAT_SPC		0x00000200
#define	PHY_LANE_STAT_XLF		0x00000400
#define	PHY_LANE_STAT_PCR		0x00000800

#define	PHY_LANE_CTRL_DATA_POS		0
#define	PHY_LANE_CTRL_ADDR_POS		8
#define	PHY_LANE_CTRL_CMD_READ		0x00010000
#define	PHY_LANE_CTRL_CMD_WRITE		0x00000000
#define	PHY_LANE_CTRL_CMD_START		0x00020000
#define	PHY_LANE_CTRL_CMD_PENDING	0x00040000
#define	PHY_LANE_CTRL_ALL		0x00200000
#define	PHY_LANE_CTRL_FAST_INIT		0x00400000
#define	PHY_LANE_CTRL_REXSEL_POS	23
#define	PHY_LANE_CTRL_PHYMODE_POS	25
#define	PHY_LANE_CTRL_PWRDOWN		0x20000000
#define	PHY_LANE_CTRL_RST		0x40000000
#define	PHY_LANE_CTRL_RST_XAUI		0xc0000000
#define	PHY_LANE_CTRL_BPC_XAUI		0x80000000

#define	LANE_CFG_CPLX_0_1		0x0
#define	LANE_CFG_CPLX_2_3		0x1
#define	LANE_CFG_CPLX_4			0x2

#define	MAC_CONF1			0x0
#define	MAC_CONF2			0x1
#define	MAX_FRM				0x4

#define	NETIOR_GMAC_CTRL1		0x7F
#define	NETIOR_GMAC_CTRL2		0x7E
#define	NETIOR_GMAC_CTRL3		0x7C

#define	SGMII_CAL_SLOTS			3
#define	XAUI_CAL_SLOTS			13
#define	IL8_CAL_SLOTS			26
#define	IL4_CAL_SLOTS			10

#define	NAE_DRR_QUANTA			2048

#define	XLP3XX_STG2_FIFO_SZ		512
#define	XLP3XX_EH_FIFO_SZ		512
#define	XLP3XX_FROUT_FIFO_SZ		512
#define	XLP3XX_MS_FIFO_SZ		512
#define	XLP3XX_PKT_FIFO_SZ		8192
#define	XLP3XX_PKTLEN_FIFO_SZ		512

#define	XLP3XX_MAX_STG2_OFFSET		0x7F
#define	XLP3XX_MAX_EH_OFFSET		0x1f
#define	XLP3XX_MAX_FREE_OUT_OFFSET	0x1f
#define	XLP3XX_MAX_MS_OFFSET		0xF
#define	XLP3XX_MAX_PMEM_OFFSET		0x7FE

#define	XLP3XX_STG1_2_CREDIT		XLP3XX_STG2_FIFO_SZ
#define	XLP3XX_STG2_EH_CREDIT		XLP3XX_EH_FIFO_SZ
#define	XLP3XX_STG2_FROUT_CREDIT	XLP3XX_FROUT_FIFO_SZ
#define	XLP3XX_STG2_MS_CREDIT		XLP3XX_MS_FIFO_SZ

#define	XLP8XX_STG2_FIFO_SZ		2048
#define	XLP8XX_EH_FIFO_SZ		4096
#define	XLP8XX_FROUT_FIFO_SZ		4096
#define	XLP8XX_MS_FIFO_SZ		2048
#define	XLP8XX_PKT_FIFO_SZ		16384
#define	XLP8XX_PKTLEN_FIFO_SZ		2048

#define	XLP8XX_MAX_STG2_OFFSET		0x7F
#define	XLP8XX_MAX_EH_OFFSET		0x7F
#define	XLP8XX_MAX_FREE_OUT_OFFSET	0x7F
#define	XLP8XX_MAX_MS_OFFSET		0x1F
#define	XLP8XX_MAX_PMEM_OFFSET		0x7FE

#define	XLP8XX_STG1_2_CREDIT		XLP8XX_STG2_FIFO_SZ
#define	XLP8XX_STG2_EH_CREDIT		XLP8XX_EH_FIFO_SZ
#define	XLP8XX_STG2_FROUT_CREDIT	XLP8XX_FROUT_FIFO_SZ
#define	XLP8XX_STG2_MS_CREDIT		XLP8XX_MS_FIFO_SZ

#define	MAX_CAL_SLOTS			64
#define	XLP_MAX_PORTS			18
#define	XLP_STORM_MAX_PORTS		8

#define	MAX_FREE_FIFO_POOL_8XX		20
#define	MAX_FREE_FIFO_POOL_3XX		9

#if !defined(LOCORE) && !defined(__ASSEMBLY__)

#define	nlm_read_nae_reg(b, r)		nlm_read_reg_xkphys(b, r)
#define	nlm_write_nae_reg(b, r, v)	nlm_write_reg_xkphys(b, r, v)
#define	nlm_get_nae_pcibase(node)	\
			nlm_pcicfg_base(XLP_IO_NAE_OFFSET(node))
#define	nlm_get_nae_regbase(node)	\
			nlm_xkphys_map_pcibar0(nlm_get_nae_pcibase(node))

#define	MAX_POE_CLASSES			8
#define	MAX_POE_CLASS_CTXT_TBL_SZ	((NUM_CONTEXTS / MAX_POE_CLASSES) + 1)
#define	TXINITIORCR(x)			(((x) & 0x7ffff) << 8)

enum XLPNAE_TX_TYPE {
        P2D_NEOP = 0,
        P2P,
        P2D_EOP,
        MSC
};

enum nblock_type {
	UNKNOWN	= 0, /* DONT MAKE IT NON-ZERO */
	SGMIIC	= 1,
	XAUIC	= 2,
	ILC	= 3
};

enum nae_interface_type {
        GMAC_0 = 0,
        GMAC_1,
        GMAC_2,
        GMAC_3,
        XGMAC,
        INTERLAKEN,
        PHY = 0xE,
        LANE_CFG = 0xF,
};

enum {
	LM_UNCONNECTED = 0,
	LM_SGMII = 1,
	LM_XAUI = 2,
	LM_IL = 3,
};

enum nae_block {
        BLOCK_0 = 0,
        BLOCK_1,
        BLOCK_2,
        BLOCK_3,
        BLOCK_4,
        BLOCK_5,
        BLOCK_6,
        BLOCK_7,
};

enum {
        PHYMODE_NONE = 0,
        PHYMODE_HS_SGMII = 1,
        PHYMODE_XAUI = 1,
        PHYMODE_SGMII = 2,
        PHYMODE_IL = 3,
};

static __inline int
nae_num_complex(uint64_t nae_pcibase)
{
	return (nlm_read_reg(nae_pcibase, XLP_PCI_DEVINFO_REG0) & 0xff);
}

static __inline int
nae_num_context(uint64_t nae_pcibase)
{
	return (nlm_read_reg(nae_pcibase, XLP_PCI_DEVINFO_REG5));
}

/* per port config structure */
struct nae_port_config {
	int		node;	/* node id (quickread) */
	int		block;	/* network block id (quickread) */
	int		port;	/* port id - among the 18 in XLP */
	int		type;	/* port type - see xlp_gmac_port_types */
	int		mdio_bus;
	int		phy_addr;
	int		num_channels;
	int		num_free_descs;
	int		free_desc_sizes;
	int		ucore_mask;
	int		loopback_mode;	/* is complex is in loopback? */
	uint32_t	freein_spill_size; /* Freein spill size for each port */
	uint32_t	free_fifo_size;	/* (512entries x 2desc/entry)1024desc */
	uint32_t	iface_fifo_size;/* 256 entries x 64B/entry    = 16KB */
	uint32_t	pseq_fifo_size;	/* 1024 entries - 1 pktlen/entry */
	uint32_t	rxbuf_size;	/* 4096 entries x 64B = 256KB */
	uint32_t	rx_if_base_config;
	uint32_t	rx_slots_reqd;
	uint32_t	tx_slots_reqd;
	uint32_t	stg2_fifo_size;
	uint32_t	eh_fifo_size;
	uint32_t	frout_fifo_size;
	uint32_t	ms_fifo_size;
	uint32_t	pkt_fifo_size;
	uint32_t	pktlen_fifo_size;
	uint32_t	max_stg2_offset;
	uint32_t	max_eh_offset;
	uint32_t	max_frout_offset;
	uint32_t	max_ms_offset;
	uint32_t	max_pmem_offset;
	uint32_t	stg1_2_credit;
	uint32_t	stg2_eh_credit;
	uint32_t	stg2_frout_credit;
	uint32_t	stg2_ms_credit;
	uint32_t	vlan_pri_en;
	uint32_t	txq;
	uint32_t	rxfreeq;
	uint32_t	ieee1588_inc_intg;
	uint32_t	ieee1588_inc_den;
	uint32_t	ieee1588_inc_num;
	uint64_t	ieee1588_userval;
	uint64_t	ieee1588_ptpoff;
	uint64_t	ieee1588_tmr1;
	uint64_t	ieee1588_tmr2;
	uint64_t	ieee1588_tmr3;
};

void nlm_nae_flush_free_fifo(uint64_t nae_base, int nblocks);
void nlm_program_nae_parser_seq_fifo(uint64_t, int, struct nae_port_config *);
void nlm_setup_rx_cal_cfg(uint64_t, int, struct nae_port_config *);
void nlm_setup_tx_cal_cfg(uint64_t, int, struct nae_port_config *cfg);
void nlm_deflate_frin_fifo_carving(uint64_t, int);
void nlm_reset_nae(int);
int nlm_set_nae_frequency(int, int);
void nlm_setup_poe_class_config(uint64_t nae_base, int max_poe_classes,
    int num_contexts, int *poe_cl_tbl);
void nlm_setup_vfbid_mapping(uint64_t);
void nlm_setup_flow_crc_poly(uint64_t, uint32_t);
void nlm_setup_iface_fifo_cfg(uint64_t, int, struct nae_port_config *);
void nlm_setup_rx_base_config(uint64_t, int, struct nae_port_config *);
void nlm_setup_rx_buf_config(uint64_t, int, struct nae_port_config *);
void nlm_setup_freein_fifo_cfg(uint64_t, struct nae_port_config *);
int nlm_get_flow_mask(int);
void nlm_program_flow_cfg(uint64_t, int, uint32_t, uint32_t);
void xlp_ax_nae_lane_reset_txpll(uint64_t, int, int, int);
void xlp_nae_lane_reset_txpll(uint64_t, int, int, int);
void xlp_nae_config_lane_gmac(uint64_t, int);
void config_egress_fifo_carvings(uint64_t, int, int, int, int,
    struct nae_port_config *);
void config_egress_fifo_credits(uint64_t, int, int, int, int,
    struct nae_port_config *);
void nlm_config_freein_fifo_uniq_cfg(uint64_t, int, int);
void nlm_config_ucore_iface_mask_cfg(uint64_t, int, int);
int nlm_nae_init_netior(uint64_t nae_base, int nblocks);
void nlm_nae_init_ingress(uint64_t, uint32_t);
void nlm_nae_init_egress(uint64_t);
uint32_t ucore_spray_config(uint32_t, uint32_t, int);
void nlm_nae_init_ucore(uint64_t nae_base, int if_num, uint32_t ucore_mask);
int nlm_nae_open_if(uint64_t, int, int, int, uint32_t);
void nlm_mac_enable(uint64_t, int, int, int);
void nlm_mac_disable(uint64_t, int, int, int);
uint64_t nae_tx_desc(u_int, u_int, u_int, u_int, uint64_t);
void nlm_setup_l2type(uint64_t, int, uint32_t, uint32_t, uint32_t,
    uint32_t, uint32_t, uint32_t);
void nlm_setup_l3ctable_mask(uint64_t, int, uint32_t, uint32_t);
void nlm_setup_l3ctable_even(uint64_t, int, uint32_t, uint32_t, uint32_t,
    uint32_t, uint32_t);
void nlm_setup_l3ctable_odd(uint64_t, int, uint32_t, uint32_t, uint32_t,
    uint32_t, uint32_t, uint32_t);
void nlm_setup_l4ctable_even(uint64_t, int, uint32_t, uint32_t, uint32_t,
    uint32_t, uint32_t, uint32_t);
void nlm_setup_l4ctable_odd(uint64_t, int, uint32_t, uint32_t, uint32_t, uint32_t);
void nlm_enable_hardware_parser(uint64_t);
void nlm_enable_hardware_parser_per_port(uint64_t, int, int);
void nlm_prepad_enable(uint64_t, int);
void nlm_setup_1588_timer(uint64_t, struct nae_port_config *);

#endif /* !(LOCORE) && !(__ASSEMBLY__) */

#endif

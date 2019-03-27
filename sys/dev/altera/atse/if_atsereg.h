/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Bjoern A. Zeeb
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-11-C-0249)
 * ("MRC2"), as part of the DARPA MRC research programme.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef _DEV_IF_ATSEREG_H
#define _DEV_IF_ATSEREG_H

#include <dev/xdma/xdma.h>

#define	ATSE_VENDOR			0x6af7
#define	ATSE_DEVICE			0x00bd

/* See hints file/fdt for ctrl port and Avalon FIFO addresses. */

/* Section 3. Parameter Settings. */
/*
 * This is a lot of options that affect the way things are synthesized.
 * We cannot really make them all hints and most of them might be stale.
 */

/* 3-1 Core Configuration */
#if 0
static const char *atse_core_core_variation[] = {
	[0] = "10/100/1000 Mbps Ethernet MAC only",
	[1] = "10/100/1000 Mbps Ethernet MAC with 1000BASE-X/SGMII PCS",
	[2] = "1000BASE-X/SGMII PCS only",
	[3] = "1000 Mbps Small MAC",
	[4] = "10/100 Mbps Small MAC",
	NULL
};
static const char *atse_core_interface[] = {
	[0] = "MII",				/* Core variation 4. */
	[1] = "GMII",				/* Core variation 3. */
	[2] = "RGMII",				/* Core variation 0,1,3. */
	[3] = "MII/GMII",			/* Core variation 0,1. */
	NULL
};
#endif
#define	CORE_CORE_VARIATION		1	/* atse_core_core_variation[] */
#define	CORE_INTERFACE			3	/* atse_core_interface[] */
#define	CORE_USE_INTERNAL_FIFO		1
#define	CORE_NUMBER_OF_PORTS		1	/* Internal FIFO count. */
#define	CORE_USE_TRANSCEIVER_BLOCK	1	/* SGMII PCS transceiver:
						 * LVDS I/O. */

/* 3-2 MAC Options. */
/* Ethernet MAC Options. */
#define	MAC_ENABLE_10_100_HDX_SUPPORT	0
#define	MAC_ENABLE_RG_G_MII_LOOPBACK	0
#define	MAC_ENABLE_SUPL_MAC_UCAST_ADDR	0	/* Supplementary MAC unicast. */
#define	MAC_INCLUDE_STATISTICS_COUNTERS	0
#define	MAC_STATISTICS_COUNTERS_64BIT	0
#define	MAC_INCLUDE_MC_HASHTABLE	0	/* Multicast. */
#define	MAC_ALIGN_PKTHDR_32BIT		1
#define	MAC_ENABLE_FDX_FLOW_CTRL	0
#define	MAC_ENABLE_VLAN_DETECTION	0	/* VLAN and stacked VLANs. */
#define	MAC_ENABLE_MAGIC_PKT_DETECTION	0
/* MDIO Module. */
#define	MAC_MDIO_INCLUDE_MDIO_MODULE	1
#define	MAC_MDIO_HOST_CLOCK_DIVISOR	40	/* Not just On/Off. */

/* 3-4 FIFO Options. */
/* Width and Memory Type. */
#if 0
static char *fifo_memory_block[] = {
	[0] = "M4K",
	[1] = "M9K",
	[2] = "M144K",
	[3] = "MRAM",
	[4] = "AUTO",
	NULL
};
#endif
#define	FIFO_MEMORY_BLOCK		4
#define	FIFO_WITDH			32	/* Other: 8 bits. */
/* Depth. */
#define	FIFO_DEPTH_TX			2048	/* 64 .. 64k, 2048x32bits. */
#define	FIFO_DEPTH_RX			2048	/* 64 .. 64k, 2048x32bits. */

#define	ATSE_TX_LIST_CNT		5	/* Certainly not bufferbloat. */

/* 3-4 PCS/Transceiver Options */
/* PCS Options. */
#define	PCS_TXRX_PHY_ID			0x00000000	/* 32 bits */
#define	PCS_TXRX_ENABLE_SGMII_BRIDGE	0
/* Transceiver Options. */
#define	PCS_TXRX_EXP_POWER_DOWN_SIGNAL	0	/* Export power down signal. */
#define	PCS_TXRX_ENABLE_DYNAMIC_RECONF	0	/* Dynamic trans. reconfig. */
#define	PCS_TXRX_STARTING_CHANNEL	0	/* 0..284. */


/* -------------------------------------------------------------------------- */

/* XXX more values based on the bitmaps provided. Cleanup. */
/* See regs above. */
#define	AVALON_FIFO_TX_BLOCK_DIAGRAM		0
#define	AVALON_FIFO_TX_BLOCK_DIAGRAM_SHOW_SIGANLS	0
#define	AVALON_FIFO_TX_PARAM_SINGLE_RESET_MODE	0
#define	AVALON_FIFO_TX_BASIC_OPTS_DEPTH		16
#define	AVALON_FIFO_TX_BASIC_OPTS_ALLOW_BACKPRESSURE	1
#define	AVALON_FIFO_TX_BASIC_OPTS_CLOCK_SETTING	"Single Clock Mode"
#define	AVALON_FIFO_TX_BASIC_OPTS_FIFO_IMPL	"Construct FIFO from embedded memory blocks"
#define	AVALON_FIFO_TX_STATUS_PORT_CREATE_STATUS_INT_FOR_INPUT	1
#define	AVALON_FIFO_TX_STATUS_PORT_CREATE_STATUS_INT_FOR_OUTPUT	0
#define	AVALON_FIFO_TX_STATUS_PORT_ENABLE_IRQ_FOR_STATUS_PORT	1
#define	AVALON_FIFO_TX_INPUT_TYPE			"AVALONMM_WRITE"
#define	AVALON_FIFO_TX_OUTPUT_TYPE			"AVALONST_SOURCE"
#define	AVALON_FIFO_TX_AVALON_MM_PORT_SETTINGS_DATA_WIDTH	""
#define	AVALON_FIFO_TX_AVALON_ST_PORT_SETTINGS_BITS_PER_SYMBOL	8
#define	AVALON_FIFO_TX_AVALON_ST_PORT_SETTINGS_SYM_PER_BEAT	4
#define	AVALON_FIFO_TX_AVALON_ST_PORT_SETTINGS_ERROR_WIDTH		1
#define	AVALON_FIFO_TX_AVALON_ST_PORT_SETTINGS_CHANNEL_WIDTH	0
#define	AVALON_FIFO_TX_AVALON_ST_PORT_SETTINGS_ENABLE_PACKET_DATA	1

#define	AVALON_FIFO_RX_BLOCK_DIAGRAM		0
#define	AVALON_FIFO_RX_BLOCK_DIAGRAM_SHOW_SIGNALS		0
#define	AVALON_FIFO_RX_PARAM_SINGLE_RESET_MODE	0
#define	AVALON_FIFO_RX_BASIC_OPTS_DEPTH		16
#define	AVALON_FIFO_RX_BASIC_OPTS_ALLOW_BACKPRESSURE	1
#define	AVALON_FIFO_RX_BASIC_OPTS_CLOCK_SETTING	"Single Clock Mode"
#define	AVALON_FIFO_RX_BASIC_OPTS_FIFO_IMPL	"Construct FIFO from embedded memory blocks"
#define	AVALON_FIFO_RX_STATUS_PORT_CREATE_STATUS_INT_FOR_INPUT	1
#define	AVALON_FIFO_RX_STATUS_PORT_CREATE_STATUS_INT_FOR_OUTPUT	0
#define	AVALON_FIFO_RX_STATUS_PORT_ENABLE_IRQ_FOR_STATUS_PORT	1
#define	AVALON_FIFO_RX_INPUT_TYPE			"AVALONST_SINK"
#define	AVALON_FIFO_RX_OUTPUT_TYPE			"AVALONMM_READ"
#define	AVALON_FIFO_RX_AVALON_MM_PORT_SETTINGS_DATA_WIDTH	""
#define	AVALON_FIFO_RX_AVALON_ST_PORT_SETTINGS_BITS_PER_SYMBOL	8
#define	AVALON_FIFO_RX_AVALON_ST_PORT_SETTINGS_SYM_PER_BEAT	4
#define	AVALON_FIFO_RX_AVALON_ST_PORT_SETTINGS_ERROR_WIDTH		6
#define	AVALON_FIFO_RX_AVALON_ST_PORT_SETTINGS_CHANNEL_WIDTH	0
#define	AVALON_FIFO_RX_AVALON_ST_PORT_SETTINGS_ENABLE_PACKET_DATA	1

/* -------------------------------------------------------------------------- */

/* 5. Configuration Register Space. */

/* 5-1, MAC Configuration Register Space; Dword offsets. */
/* 0x00 - 0x17, Base Configuration. */
#define	BASE_CONFIG_REV			0x00		/* ro, IP Core ver. */
#define	BASE_CFG_REV_VER_MASK			0x0000FFFF
#define	BASE_CFG_REV_CUST_VERSION__MASK		0xFFFF0000

#define	BASE_CFG_SCRATCH		0x01		/* rw, 0 */

#define	BASE_CFG_COMMAND_CONFIG		0x02		/* rw, 0 */
#define	BASE_CFG_COMMAND_CONFIG_TX_ENA		(1<<0)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_RX_ENA		(1<<1)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_XON_GEN		(1<<2)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_ETH_SPEED	(1<<3)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_PROMIS_EN	(1<<4)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_PAD_EN		(1<<5)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_CRC_FWD		(1<<6)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_PAUSE_FWD	(1<<7)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_PAUSE_IGNORE	(1<<8)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_TX_ADDR_INS	(1<<9)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_HD_ENA		(1<<10)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_EXCESS_COL	(1<<11)	/* ro */
#define	BASE_CFG_COMMAND_CONFIG_LATE_COL	(1<<12)	/* ro */
#define	BASE_CFG_COMMAND_CONFIG_SW_RESET	(1<<13)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_MHASH_SEL	(1<<14)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_LOOP_ENA	(1<<15)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_TX_ADDR_SEL	(1<<16|1<<17|1<<18) /* rw */
#define	BASE_CFG_COMMAND_CONFIG_MAGIC_ENA	(1<<19)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_SLEEP		(1<<20)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_WAKEUP		(1<<21)	/* ro */
#define	BASE_CFG_COMMAND_CONFIG_XOFF_GEN	(1<<22)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_CNTL_FRM_ENA	(1<<23)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_NO_LGTH_CHECK	(1<<24)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_ENA_10		(1<<25)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_RX_ERR_DISC	(1<<26)	/* rw */
#define	BASE_CFG_COMMAND_CONFIG_DISABLE_READ_TIMEOUT	(1<<27)	/* rw */
	/* 28-30 Reserved. */				/* - */
#define	BASE_CFG_COMMAND_CONFIG_CNT_RESET	(1<<31)	/* rw */

#define	BASE_CFG_MAC_0			0x03		/* rw, 0 */
#define	BASE_CFG_MAC_1			0x04		/* rw, 0 */
#define	BASE_CFG_FRM_LENGTH		0x05		/* rw/ro, 1518 */
#define	BASE_CFG_PAUSE_QUANT		0x06		/* rw, 0 */
#define	BASE_CFG_RX_SECTION_EMPTY	0x07		/* rw/ro, 0 */
#define	BASE_CFG_RX_SECTION_FULL	0x08		/* rw/ro, 0 */
#define	BASE_CFG_TX_SECTION_EMPTY	0x09		/* rw/ro, 0 */
#define	BASE_CFG_TX_SECTION_FULL	0x0A		/* rw/ro, 0 */
#define	BASE_CFG_RX_ALMOST_EMPTY	0x0B		/* rw/ro, 0 */
#define	BASE_CFG_RX_ALMOST_FULL		0x0C		/* rw/ro, 0 */
#define	BASE_CFG_TX_ALMOST_EMPTY	0x0D		/* rw/ro, 0 */
#define	BASE_CFG_TX_ALMOST_FULL		0x0E		/* rw/ro, 0 */
#define	BASE_CFG_MDIO_ADDR0		0x0F		/* rw, 0 */
#define	BASE_CFG_MDIO_ADDR1		0x10		/* rw, 1 */
#define	BASE_CFG_HOLDOFF_QUANT		0x11		/* rw, 0xFFFF */
/* 0x12-0x16 Reserved. */				/* -, 0 */
#define	BASE_CFG_TX_IPG_LENGTH		0x17		/* rw, 0 */

/* 0x18 - 0x38, Statistics Counters. */
#define	STATS_A_MAC_ID_0		0x18		/* ro */
#define	STATS_A_MAC_ID_1		0x19		/* ro */
#define	STATS_A_FRAMES_TX_OK		0x1A		/* ro */
#define	STATS_A_FRAMES_RX_OK		0x1B		/* ro */
#define	STATS_A_FCS_ERRORS		0x1C		/* ro */
#define	STATS_A_ALIGNMENT_ERRORS	0x1D		/* ro */
#define	STATS_A_OCTETS_TX_OK		0x1E		/* ro */
#define	STATS_A_OCTETS_RX_OK		0x1F		/* ro */
#define	STATS_A_TX_PAUSE_MAX_CTRL_FRAME	0x20		/* ro */
#define	STATS_A_RX_PAUSE_MAX_CTRL_FRAME	0x21		/* ro */
#define	STATS_IF_IN_ERRORS		0x22		/* ro */
#define	STATS_IF_OUT_ERRORS		0x23		/* ro */
#define	STATS_IF_IN_UCAST_PKTS		0x24		/* ro */
#define	STATS_IF_IN_MULTICAST_PKTS	0x25		/* ro */
#define	STATS_IF_IN_BROADCAST_PKTS	0x26		/* ro */
#define	STATS_IF_OUT_DISCARDS		0x27		/* ro */
#define	STATS_IF_OUT_UCAST_PKTS		0x28		/* ro */
#define	STATS_IF_OUT_MULTICAST_PKTS	0x29		/* ro */
#define	STATS_IF_OUT_BROADCAST_PKTS	0x2A		/* ro */
#define	STATS_ETHER_STATS_DROP_EVENT	0x2B		/* ro */
#define	STATS_ETHER_STATS_OCTETS	0x2C		/* ro */
#define	STATS_ETHER_STATS_PKTS		0x2D		/* ro */
#define	STATS_ETHER_STATS_USIZE_PKTS	0x2E		/* ro */
#define	STATS_ETHER_STATS_OSIZE_PKTS	0x2F		/* ro */
#define	STATS_ETHER_STATS_PKTS_64_OCTETS 0x30		/* ro */
#define	STATS_ETHER_STATS_PKTS_65_TO_127_OCTETS	 0x31	/* ro */
#define	STATS_ETHER_STATS_PKTS_128_TO_255_OCTETS 0x32	/* ro */
#define	STATS_ETHER_STATS_PKTS_256_TO_511_OCTETS 0x33	/* ro */
#define	STATS_ETHER_STATS_PKTS_512_TO_1023_OCTETS 0x34	/* ro */
#define	STATS_ETHER_STATS_PKTS_1024_TO_1518_OCTETS 0x35	/* ro */
#define	STATS_ETHER_STATS_PKTS_1519_TO_X_OCTETS	0x36	/* ro */
#define	STATS_ETHER_STATS_JABBERS	0x37		/* ro */
#define	STATS_ETHER_STATS_FRAGMENTS	0x38		/* ro */
	/* 0x39, Reserved. */				/* - */

/* 0x3A, Transmit Command. */
#define	TX_CMD_STAT			0x3A		/* rw */
#define	TX_CMD_STAT_OMIT_CRC			(1<<17)
#define	TX_CMD_STAT_TX_SHIFT16			(1<<18)

/* 0x3B, Receive Command. */
#define	RX_CMD_STAT			0x3B		/* rw */
#define	RX_CMD_STAT_RX_SHIFT16			(1<<25)

/* 0x3C - 0x3E, Extended Statistics Counters. */
#define	ESTATS_MSB_A_OCTETS_TX_OK	0x3C		/* ro */
#define	ESTATS_MSB_A_OCTETS_RX_OK	0x3D		/* ro */
#define	ESTATS_MSB_ETHER_STATS_OCTETS	0x3E		/* ro */

/* 0x3F, Reserved. */

/* 0x40 - 0x7F, Multicast Hash Table. */
#define	MHASH_START			0x40
#define	MHASH_LEN			0x3F

/* 0x80 - 0x9F, MDIO Space 0 or PCS Function Configuration. */
#define	MDIO_0_START			0x80

/* The following are offsets to the first PCS register at 0x80. */
/* See sys/dev/mii/mii.h. */
#define	PCS_CONTROL			0x00		/* rw */
	/* Bits 0:4, Reserved. */			/* - */
#define	PCS_CONTROL_UNIDIRECTIONAL_ENABLE	(1<<5)	/* rw */
#define	PCS_CONTROL_SPEED_SELECTION		(1<<6|1<<13) /* ro */
#define	PCS_CONTROL_COLLISION_TEST		(1<<7)	/* ro */
#define	PCS_CONTROL_DUPLEX_MODE			(1<<8)	/* ro */
#define	PCS_CONTROL_RESTART_AUTO_NEGOTIATION	(1<<9)	/* rw */
#define	PCS_CONTROL_ISOLATE			(1<<10)	/* rw */
#define	PCS_CONTROL_POWERDOWN			(1<<11)	/* rw */
#define	PCS_CONTROL_AUTO_NEGOTIATION_ENABLE	(1<<12)	/* rw */
	/* See bit 6 above. */				/* ro */
#define	PCS_CONTROL_LOOPBACK			(1<<14)	/* rw */
#define	PCS_CONTROL_RESET			(1<<15)	/* rw */

#define	PCS_STATUS			0x01		/* ro */
#define	PCS_STATUS_EXTENDED_CAPABILITY		(1<<0)	/* ro */
#define	PCS_STATUS_JABBER_DETECT		(1<<1)	/* -, 0 */
#define	PCS_STATUS_LINK_STATUS			(1<<2)	/* ro */
#define	PCS_STATUS_AUTO_NEGOTIATION_ABILITY	(1<<3)	/* ro */
#define	PCS_STATUS_REMOTE_FAULT			(1<<4)	/* -, 0 */
#define	PCS_STATUS_AUTO_NEGOTIATION_COMPLETE	(1<<5)	/* ro */
#define	PCS_STATUS_MF_PREAMBLE_SUPPRESSION	(1<<6)	/* -, 0 */
#define	PCS_STATUS_UNIDIRECTIONAL_ABILITY	(1<<7)	/* ro */
#define	PCS_STATUS_EXTENDED_STATUS		(1<<8)	/* -, 0 */
#define	PCS_STATUS_100BASET2_HALF_DUPLEX	(1<<9)	/* ro */
#define	PCS_STATUS_100BASET2_FULL_DUPLEX	(1<<10)	/* ro */
#define	PCS_STATUS_10MBPS_HALF_DUPLEX		(1<<11)	/* ro */
#define	PCS_STATUS_10MBPS_FULL_DUPLEX		(1<<12)	/* ro */
#define	PCS_STATUS_100BASE_X_HALF_DUPLEX	(1<<13)	/* ro */
#define	PCS_STATUS_100BASE_X_FULL_DUPLEX	(1<<14)	/* ro */
#define	PCS_STATUS_100BASE_T4			(1<<15)	/* ro */

#define	PCS_PHY_IDENTIFIER_0		0x02		/* ro */
#define	PCS_PHY_IDENTIFIER_1		0x03		/* ro */

#define	PCS_DEV_ABILITY			0x04		/* rw */
	/* 1000BASE-X */
	/* Bits 0:4, Reserved. */			/* - */
#define	PCS_DEV_ABILITY_1000BASE_X_FD		(1<<5)	/* rw */
#define	PCS_DEV_ABILITY_1000BASE_X_HD		(1<<6)	/* rw */
#define	PCS_DEV_ABILITY_1000BASE_X_PS1		(1<<7)	/* rw */
#define	PCS_DEV_ABILITY_1000BASE_X_PS2		(1<<8)	/* rw */
	/* Bits 9:11, Reserved. */			/* - */
#define	PCS_DEV_ABILITY_1000BASE_X_RF1		(1<<12)	/* rw */
#define	PCS_DEV_ABILITY_1000BASE_X_RF2		(1<<13)	/* rw */
#define	PCS_DEV_ABILITY_1000BASE_X_ACK		(1<<14)	/* rw */
#define	PCS_DEV_ABILITY_1000BASE_X_NP		(1<<15)	/* rw */

#define	PCS_PARTNER_ABILITY		0x05		/* ro */
	/* 1000BASE-X */
	/* Bits 0:4, Reserved. */			/* - */
#define	PCS_PARTNER_ABILITY_1000BASE_X_FD	(1<<5)	/* ro */
#define	PCS_PARTNER_ABILITY_1000BASE_X_HD	(1<<6)	/* ro */
#define	PCS_PARTNER_ABILITY_1000BASE_X_PS1	(1<<7)	/* ro */
#define	PCS_PARTNER_ABILITY_1000BASE_X_PS2	(1<<8)	/* ro */
	/* Bits 9:11, Reserved. */			/* - */
#define	PCS_PARTNER_ABILITY_1000BASE_X_RF1	(1<<12)	/* ro */
#define	PCS_PARTNER_ABILITY_1000BASE_X_RF2	(1<<13)	/* ro */
#define	PCS_PARTNER_ABILITY_1000BASE_X_ACK	(1<<14)	/* ro */
#define	PCS_PARTNER_ABILITY_1000BASE_X_NP	(1<<15)	/* ro */
	/* SGMII */
	/* Bits 0:9, Reserved. */			/* - */
#define	PCS_PARTNER_ABILITY_SGMII_COPPER_SPEED0	(1<<10)	/* ro */
#define	PCS_PARTNER_ABILITY_SGMII_COPPER_SPEED1	(1<<11)	/* ro */
#define	PCS_PARTNER_ABILITY_SGMII_COPPER_DUPLEX_STATUS	(1<<12)	/* ro */
	/* Bit 13, Reserved. */				/* - */
#define	PCS_PARTNER_ABILITY_SGMII_ACK		(1<<14)	/* ro */
#define	PCS_PARTNER_ABILITY_SGMII_COPPER_LINK_STATUS	(1<<15)	/* ro */

#define	PCS_AN_EXPANSION		0x06		/* ro */
#define	PCS_AN_EXPANSION_LINK_PARTNER_AUTO_NEGOTIATION_ABLE	(1<<0)	/* ro */
#define	PCS_AN_EXPANSION_PAGE_RECEIVE		(1<<1)	/* ro */
#define	PCS_AN_EXPANSION_NEXT_PAGE_ABLE		(1<<2)	/* -, 0 */
	/* Bits 3:15, Reserved. */			/* - */

#define	PCS_DEVICE_NEXT_PAGE		0x07		/* ro */
#define	PCS_PARTNER_NEXT_PAGE		0x08		/* ro */
#define	PCS_MASTER_SLAVE_CNTL		0x09		/* ro */
#define	PCS_MASTER_SLAVE_STAT		0x0A		/* ro */
	/* 0x0B - 0x0E, Reserved */			/* - */
#define	PCS_EXTENDED_STATUS		0x0F		/* ro */
/* Specific Extended Registers. */
#define	PCS_EXT_SCRATCH			0x10		/* rw */
#define	PCS_EXT_REV			0x11		/* ro */
#define	PCS_EXT_LINK_TIMER_0		0x12		/* rw */
#define	PCS_EXT_LINK_TIMER_1		0x13		/* rw */
#define	PCS_EXT_IF_MODE			0x14		/* rw */
#define	PCS_EXT_IF_MODE_SGMII_ENA		(1<<0)	/* rw */
#define	PCS_EXT_IF_MODE_USE_SGMII_AN		(1<<1)	/* rw */
#define	PCS_EXT_IF_MODE_SGMII_SPEED1		(1<<2)	/* rw */
#define	PCS_EXT_IF_MODE_SGMII_SPEED0		(1<<3)	/* rw */
#define	PCS_EXT_IF_MODE_SGMII_DUPLEX		(1<<4)	/* rw */
	/* Bits 5:15, Reserved. */			/* - */

#define	PCS_EXT_DISABLE_READ_TIMEOUT	0x15		/* rw */
#define	PCS_EXT_READ_TIMEOUT		0x16		/* r0 */
	/* 0x17-0x1F, Reserved. */

/* 0xA0 - 0xBF, MDIO Space 1. */
#define	MDIO_1_START			0xA0
#define	ATSE_BMCR			MDIO_1_START

/* 0xC0 - 0xC7, Supplementary Address. */
#define	SUPPL_ADDR_SMAC_0_0		0xC0		/* rw */
#define	SUPPL_ADDR_SMAC_0_1		0xC1		/* rw */
#define	SUPPL_ADDR_SMAC_1_0		0xC2		/* rw */
#define	SUPPL_ADDR_SMAC_1_1		0xC3		/* rw */
#define	SUPPL_ADDR_SMAC_2_0		0xC4		/* rw */
#define	SUPPL_ADDR_SMAC_2_1		0xC5		/* rw */
#define	SUPPL_ADDR_SMAC_3_0		0xC6		/* rw */
#define	SUPPL_ADDR_SMAC_3_1		0xC7		/* rw */

/* 0xC8 - 0xCF, Reserved; set to zero, ignore on read. */
/* 0xD7 - 0xFF, Reserved; set to zero, ignore on read. */


/* -------------------------------------------------------------------------- */

/* DE4 Intel Strata Flash Ethernet Option Bits area. */
/* XXX-BZ this is something a loader will have to handle for us. */
#define	ALTERA_ETHERNET_OPTION_BITS_OFF	0x00008000
#define	ALTERA_ETHERNET_OPTION_BITS_LEN	0x00007fff

/* -------------------------------------------------------------------------- */

struct atse_softc {
	struct ifnet		*atse_ifp;
	struct resource		*atse_mem_res;
	device_t		atse_miibus;
	device_t		atse_dev;
	int			atse_unit;
	int			atse_mem_rid;
	int			atse_phy_addr;
	int			atse_if_flags;
	bus_addr_t		atse_bmcr0;
	bus_addr_t		atse_bmcr1;
	uint32_t		atse_flags;
#define	ATSE_FLAGS_LINK			0x00000001
#define	ATSE_FLAGS_ERROR		0x00000002
#define	ATSE_FLAGS_SOP_SEEN		0x00000004
	uint8_t			atse_eth_addr[ETHER_ADDR_LEN];
#define	ATSE_ETH_ADDR_DEF	0x01
#define	ATSE_ETH_ADDR_SUPP1	0x02
#define	ATSE_ETH_ADDR_SUPP2	0x04
#define	ATSE_ETH_ADDR_SUPP3	0x08
#define	ATSE_ETH_ADDR_SUPP4	0x10
#define	ATSE_ETH_ADDR_ALL	0x1f
	int16_t			atse_rx_cycles;		/* POLLING */
#define	RX_CYCLES_IN_INTR	5
	uint32_t		atse_rx_err[6];
#define	ATSE_RX_ERR_FIFO_THRES_EOP	0 /* FIFO threshold reached, on EOP. */
#define	ATSE_RX_ERR_ELEN		1 /* Frame/payload length not valid. */
#define	ATSE_RX_ERR_CRC32		2 /* CRC-32 error. */
#define	ATSE_RX_ERR_FIFO_THRES_TRUNC	3 /* FIFO thresh., truncated frame. */
#define	ATSE_RX_ERR_4			4 /* ? */
#define	ATSE_RX_ERR_5			5 /* / */
#define	ATSE_RX_ERR_MAX			6
	struct callout		atse_tick;
	struct mtx		atse_mtx;
	device_t		dev;

	/* xDMA */
	xdma_controller_t	*xdma_tx;
	xdma_channel_t		*xchan_tx;
	void			*ih_tx;
	int			txcount;

	xdma_controller_t	*xdma_rx;
	xdma_channel_t		*xchan_rx;
	void			*ih_rx;

	struct buf_ring		*br;
	struct mtx		br_mtx;
};


int	atse_attach(device_t);
int	atse_detach_dev(device_t);
void	atse_detach_resources(device_t);

int	atse_miibus_readreg(device_t, int, int);
int	atse_miibus_writereg(device_t, int, int, int);
void	atse_miibus_statchg(device_t);

extern devclass_t atse_devclass;

#endif /* _DEV_IF_ATSEREG_H */

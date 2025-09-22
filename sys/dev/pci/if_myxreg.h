/*	$OpenBSD: if_myxreg.h,v 1.13 2019/04/15 02:59:41 dlg Exp $	*/

/*
 * Copyright (c) 2007 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Register definitions for the Myricom Myri-10G Lanai-Z8E Ethernet chipsets.
 */

#ifndef _MYX_REG_H
#define _MYX_REG_H

/*
 * Common definitions
 */

#define MYXBAR0			PCI_MAPREG_START

#define MYX_NRXDESC		256
#define MYX_NTXDESC_MIN		2
#define MYX_IRQCOALDELAY	60
#define MYX_IRQDEASSERTWAIT	1

#define MYXALIGN_CMD		64
#define MYXALIGN_DATA		PAGE_SIZE
#define MYX_BOUNDARY		4096
#define MYX_MTU			9400

#define MYX_ADDRHIGH(_v)	(((u_int64_t)_v >> 32) & 0xffffffff)
#define MYX_ADDRLOW(_v)		((u_int64_t)_v & 0xffffffff)

/*
 * PCI memory/register layout
 */

#define MYX_SRAM		0x00000000	/* SRAM offset */
#define MYX_SRAM_SIZE		0x001dff00	/* SRAM size */
#define  MYX_HEADER_POS		0x0000003c	/* Header position offset */
#define  MYX_HEADER_POS_SIZE	0x00000004	/* Header position size */
#define  MYX_FW			0x00100000	/* Firmware offset */
#define   MYX_FW_BOOT		0x00100008	/* Firmware boot offset */
#define  MYX_STRING_SPECS	0x001dfe00	/* STRING_SPECS offset */
#define  MYX_STRING_SPECS_SIZE	0x00000100	/* STRING_SPECS size */
#define MYX_BOOT		0x00fc0000	/* Boot handoff */
#define MYX_RDMA		0x00fc01c0	/* Dummy RDMA */
#define MYX_CMD			0x00f80000	/* Command offset */

/*
 * Firmware definitions
 */

#define MYXFW_ALIGNED		"myx-eth_z8e"
#define MYXFW_UNALIGNED		"myx-ethp_z8e"
#define MYXFW_TYPE_ETH		0x45544820
#define MYXFW_VER		"1.4."		/* stored as a string... */

#define MYXFW_MIN_LEN		(MYX_HEADER_POS + MYX_HEADER_POS_SIZE)

struct myx_gen_hdr {
	u_int32_t	fw_hdrlength;
	u_int32_t	fw_type;
	u_int8_t	fw_version[128];
	u_int32_t	fw_mcp_globals;
	u_int32_t	fw_sram_size;
	u_int32_t	fw_specs;
	u_int32_t	fw_specs_len;
} __packed;

/*
 * Commands, descriptors, and DMA structures
 */

struct myx_cmd {
	u_int32_t	mc_cmd;
	u_int32_t	mc_data0;
	u_int32_t	mc_data1;
	u_int32_t	mc_data2;
	u_int32_t	mc_addr_high;
	u_int32_t	mc_addr_low;
	u_int8_t	mc_pad[40];		/* pad up to 64 bytes */
} __packed __aligned(4);

struct myx_response {
	u_int32_t	mr_data;
	u_int32_t	mr_result;
} __packed;

struct myx_bootcmd {
	u_int32_t	bc_addr_high;
	u_int32_t	bc_addr_low;
	u_int32_t	bc_result;
	u_int32_t	bc_offset;
	u_int32_t	bc_length;
	u_int32_t	bc_copyto;
	u_int32_t	bc_jumpto;
	u_int8_t	bc_pad[36];		/* pad up to 64 bytes */
} __packed;

struct myx_rdmacmd {
	u_int32_t	rc_addr_high;
	u_int32_t	rc_addr_low;
	u_int32_t	rc_result;
	u_int32_t	rc_rdma_high;
	u_int32_t	rc_rdma_low;
	u_int32_t	rc_enable;
#define  MYXRDMA_ON	1
#define  MYXRDMA_OFF	0
	u_int8_t	rc_pad[40];		/* pad up to 64 bytes */
} __packed;

struct myx_status {
	u_int32_t	ms_reserved;
	u_int32_t	ms_dropped_pause;
	u_int32_t	ms_dropped_unicast;
	u_int32_t	ms_dropped_crc32err;
	u_int32_t	ms_dropped_phyerr;
	u_int32_t	ms_dropped_mcast;
	u_int32_t	ms_txdonecnt;
	u_int32_t	ms_linkstate;
#define  MYXSTS_LINKDOWN	0
#define  MYXSTS_LINKUP		1
#define  MYXSTS_LINKUNKNOWN	2
	u_int32_t	ms_dropped_linkoverflow;
	u_int32_t	ms_dropped_linkerror;
	u_int32_t	ms_dropped_runt;
	u_int32_t	ms_dropped_overrun;
	u_int32_t	ms_dropped_smallbufunderrun;
	u_int32_t	ms_dropped_bigbufunderrun;
	u_int32_t	ms_rdmatags_available;
#define  MYXSTS_RDMAON	1
#define  MYXSTS_RDMAOFF	0
	u_int8_t	ms_txstopped;
	u_int8_t	ms_linkdown;
	u_int8_t	ms_statusupdated;
	u_int8_t	ms_isvalid;
} __packed __aligned(4);

struct myx_intrq_desc {
	u_int16_t	iq_csum;
	u_int16_t	iq_length;
} __packed __aligned(4);

struct myx_rx_desc {
	u_int64_t	rx_addr;
} __packed __aligned(8);

struct myx_tx_desc {
	u_int64_t	tx_addr;
	u_int16_t	tx_hdr_offset;
	u_int16_t	tx_length;
	u_int8_t	tx_pad;
	u_int8_t	tx_nsegs;
	u_int8_t	tx_cksum_offset;
	u_int8_t	tx_flags;
#define  MYXTXD_FLAGS_SMALL	(1<<0)
#define  MYXTXD_FLAGS_FIRST	(1<<1)
#define  MYXTXD_FLAGS_ALIGN_ODD	(1<<2)
#define  MYXTXD_FLAGS_CKSUM	(1<<3)
#define  MYXTXD_FLAGS_NO_TSO	(1<<4)

#define  MYXTXD_FLAGS_TSO_HDR	(1<<0)
#define  MYXTXD_FLAGS_TSO_LAST	(1<<3)
#define  MYXTXD_FLAGS_TSO_CHOP	(1<<4)
#define  MYXTXD_FLAGS_TSO_PLD	(1<<5)
} __packed __aligned(8);

enum {
	MYXCMD_NONE			= 0,
	MYXCMD_RESET			= 1,
	MYXCMD_GET_VERSION		= 2,
	MYXCMD_SET_INTRQDMA		= 3,
	MYXCMD_SET_BIGBUFSZ		= 4,
	MYXCMD_SET_SMALLBUFSZ		= 5,
	MYXCMD_GET_TXRINGOFF		= 6,
	MYXCMD_GET_RXSMALLRINGOFF	= 7,
	MYXCMD_GET_RXBIGRINGOFF		= 8,
	MYXCMD_GET_INTRACKOFF		= 9,
	MYXCMD_GET_INTRDEASSERTOFF	= 10,
	MYXCMD_GET_TXRINGSZ		= 11,
	MYXCMD_GET_RXRINGSZ		= 12,
	MYXCMD_SET_INTRQSZ		= 13,
	MYXCMD_SET_IFUP			= 14,
	MYXCMD_SET_IFDOWN		= 15,
	MYXCMD_SET_MTU			= 16,
	MYXCMD_GET_INTRCOALDELAYOFF	= 17,
	MYXCMD_SET_STATSINTVL		= 18,
	MYXCMD_SET_STATSDMA_OLD		= 19,
	MYXCMD_SET_PROMISC		= 20,
	MYXCMD_UNSET_PROMISC		= 21,
	MYXCMD_SET_LLADDR		= 22,
	MYXCMD_SET_FC			= 23,
	MYXCMD_UNSET_FC			= 24,
#define  MYXCMD_FC_DEFAULT		MYXCMD_SET_FC	/* set flow control */
	MYXCMD_DMA_TEST			= 25,
	MYXCMD_SET_ALLMULTI		= 26,
	MYXCMD_UNSET_ALLMULTI		= 27,
	MYXCMD_SET_MCASTGROUP		= 28,
	MYXCMD_UNSET_MCASTGROUP		= 29,
	MYXCMD_UNSET_MCAST		= 30,
	MYXCMD_SET_STATSDMA		= 31,
	MYXCMD_UNALIGNED_DMA_TEST	= 32,
	MYXCMD_GET_UNALIGNED_STATUS	= 33,
	MYXCMD_ALWAYS_USE_N_BIG_BUFFERS	= 34,
	MYXCMD_GET_MAX_RSS_QUEUES	= 35,
	MYXCMD_ENABLE_RSS_QUEUES	= 36,
	MYXCMD_GET_RSS_SHARED_INTERRUPT_MASK_OFFSET
					= 37,
	MYXCMD_SET_RSS_SHARED_INTERRUPT_DMA
					= 38,
	MYXCMD_GET_RSS_TABLE_OFFSET	= 39,
	MYXCMD_SET_RSS_TABLE_SIZE	= 40,
	MYXCMD_GET_RSS_KEY_OFFSET	= 41,
	MYXCMD_RSS_KEY_UPDATED		= 42,
	MYXCMD_SET_RSS_ENABLE		= 43,
	MYXCMD_GET_MAX_TSO6_HDR_SIZE	= 44,
	MYXCMD_SET_TSO_MODE		= 45,
	MYXCMD_MDIO_READ		= 46,
	MYXCMD_MDIO_WRITE		= 47,
	MYXCMD_I2C_READ			= 48,
	MYXCMD_I2C_BYTE			= 49,
	MYXCMD_GET_VPUMP_OFFSET		= 50,
	MYXCMD_RESET_VPUMP		= 51,
	MYXCMD_SET_RSS_MCP_SLOT_TYPE	= 52,
	MYXCMD_SET_THROTTLE_FACTOR	= 53,
	MYXCMD_VPUMP_UP			= 54,
	MYXCMD_GET_VPUMP_CLK		= 55,
	MYXCMD_GET_DCA_OFFSET		= 56,
	MYXCMD_NETQ_GET_FILTERS_PER_QUEUE
					= 57,
	MYXCMD_NETQ_ADD_FILTER		= 58,
	MYXCMD_NETQ_DEL_FILTER		= 59,
	MYXCMD_NETQ_QUERY1		= 60,
	MYXCMD_NETQ_QUERY2		= 61,
	MYXCMD_NETQ_QUERY3		= 62,
	MYXCMD_NETQ_QUERY4		= 63,
	MYXCMD_RELAX_RXBUFFER_ALIGNMENT	= 64,
};

enum {
	MYXCMD_OK			= 0,
	MYXCMD_UNKNOWN			= 1,
	MYXCMD_ERR_RANGE		= 2,
	MYXCMD_ERR_BUSY			= 3,
	MYXCMD_ERR_EMPTY		= 4,
	MYXCMD_ERR_CLOSED		= 5,
	MYXCMD_ERR_HASH			= 6,
	MYXCMD_ERR_BADPORT		= 7,
	MYXCMD_ERR_RES			= 8,
	MYXCMD_ERR_MULTICAST		= 9,
	MYXCMD_ERR_UNALIGNED		= 10,
	MYXCMD_ERR_NO_MDIO		= 11,
	MYXCMD_ERR_I2C_FAILURE		= 12,
	MYXCMD_ERR_I2C_ABSENT		= 13,
	MYXCMD_ERR_BAD_PCIE_LINK	= 14,
};

#endif /* _MYX_REG_H */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2013 Qlogic Corporation
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
/*
 * File: qla_hw.h
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */
#ifndef _QLA_HW_H_
#define _QLA_HW_H_

#define Q8_MAX_NUM_MULTICAST_ADDRS	128
#define Q8_MAC_ADDR_LEN			6

/*
 * Firmware Interface
 */

/*
 * Command Response Interface - Commands
 */
typedef struct qla_cdrp {
	uint32_t cmd;
	uint32_t cmd_arg1;
	uint32_t cmd_arg2;
	uint32_t cmd_arg3;
	uint32_t rsp;
	uint32_t rsp_arg1;
	uint32_t rsp_arg2;
	uint32_t rsp_arg3;
} qla_cdrp_t;
 
#define Q8_CMD_RD_MAX_RDS_PER_CNTXT	0x80000002
#define Q8_CMD_RD_MAX_SDS_PER_CNTXT	0x80000003
#define Q8_CMD_RD_MAX_RULES_PER_CNTXT	0x80000004
#define Q8_CMD_RD_MAX_RX_CNTXT		0x80000005
#define Q8_CMD_RD_MAX_TX_CNTXT		0x80000006
#define Q8_CMD_CREATE_RX_CNTXT		0x80000007
#define Q8_CMD_DESTROY_RX_CNTXT		0x80000008
#define Q8_CMD_CREATE_TX_CNTXT		0x80000009
#define Q8_CMD_DESTROY_TX_CNTXT		0x8000000A
#define Q8_CMD_SETUP_STATS		0x8000000E
#define Q8_CMD_GET_STATS		0x8000000F
#define Q8_CMD_DELETE_STATS		0x80000010
#define Q8_CMD_GEN_INT			0x80000011
#define Q8_CMD_SET_MTU			0x80000012
#define Q8_CMD_GET_FLOW_CNTRL		0x80000016
#define Q8_CMD_SET_FLOW_CNTRL		0x80000017
#define Q8_CMD_RD_MAX_MTU		0x80000018
#define Q8_CMD_RD_MAX_LRO		0x80000019

/*
 * Command Response Interface - Response
 */
#define Q8_RSP_SUCCESS			0x00000000
#define Q8_RSP_NO_HOST_MEM		0x00000001
#define Q8_RSP_NO_HOST_RSRC		0x00000002
#define Q8_RSP_NO_CARD_CRB		0x00000003
#define Q8_RSP_NO_CARD_MEM		0x00000004
#define Q8_RSP_NO_CARD_RSRC		0x00000005
#define Q8_RSP_INVALID_ARGS		0x00000006
#define Q8_RSP_INVALID_ACTION		0x00000007
#define Q8_RSP_INVALID_STATE		0x00000008
#define Q8_RSP_NOT_SUPPORTED		0x00000009
#define Q8_RSP_NOT_PERMITTED		0x0000000A
#define Q8_RSP_NOT_READY		0x0000000B
#define Q8_RSP_DOES_NOT_EXIST		0x0000000C
#define Q8_RSP_ALREADY_EXISTS		0x0000000D
#define Q8_RSP_BAD_SIGNATURE		0x0000000E
#define Q8_RSP_CMD_NOT_IMPLEMENTED	0x0000000F
#define Q8_RSP_CMD_INVALID		0x00000010
#define Q8_RSP_TIMEOUT			0x00000011


/*
 * Transmit Related Definitions
 */

/*
 * Transmit Context - Q8_CMD_CREATE_TX_CNTXT Command Configuration Data
 */

typedef struct _q80_tx_cntxt_req {
	uint64_t rsp_dma_addr;		/* rsp from firmware is DMA'ed here */
	uint64_t cmd_cons_dma_addr;
	uint64_t rsrvd0;

	uint32_t caps[4];		/* capabilities  - bit vector*/
#define CNTXT_CAP0_BASEFW		0x0001
#define CNTXT_CAP0_LEGACY_MN		0x0004
#define CNTXT_CAP0_LSO			0x0040

	uint32_t intr_mode;		/* Interrupt Mode */
#define CNTXT_INTR_MODE_UNIQUE	0x0000
#define CNTXT_INTR_MODE_SHARED	0x0001

	uint64_t rsrvd1;
	uint16_t msi_index;
	uint16_t rsrvd2;
	uint64_t phys_addr;		/* physical address of transmit ring
					 * in system memory */
	uint32_t num_entries;		/* number of entries in transmit ring */
	uint8_t rsrvd3[128];
} __packed q80_tx_cntxt_req_t; /* 188 bytes total */
	

/*
 * Transmit Context - Response from Firmware to Q8_CMD_CREATE_TX_CNTXT
 */

typedef struct _q80_tx_cntxt_rsp {
	uint32_t cntxt_state;	/* starting state */
#define CNTXT_STATE_ALLOCATED_NOT_ACTIVE	0x0001
#define CNTXT_STATE_ACTIVE			0x0002
#define CNTXT_STATE_QUIESCED			0x0004

	uint16_t cntxt_id;	/* handle for context */
	uint8_t phys_port_id;	/* physical id of port */
	uint8_t virt_port_id;	/* virtual or logical id of port */
	uint32_t producer_reg;	/* producer register for transmit ring */
	uint32_t intr_mask_reg;	/* interrupt mask register */
	uint8_t rsrvd[128];
} __packed q80_tx_cntxt_rsp_t; /* 144 bytes */

/*
 * Transmit Command Descriptor
 * These commands are issued on the Transmit Ring associated with a Transmit
 * context
 */
typedef struct _q80_tx_cmd {
	uint8_t		tcp_hdr_off;	/* TCP Header Offset */
	uint8_t		ip_hdr_off;	/* IP Header Offset */
	uint16_t	flags_opcode;	/* Bits 0-6: flags; 7-12: opcode */

	/* flags field */
#define Q8_TX_CMD_FLAGS_MULTICAST	0x01
#define Q8_TX_CMD_FLAGS_LSO_TSO		0x02
#define Q8_TX_CMD_FLAGS_VLAN_TAGGED	0x10
#define Q8_TX_CMD_FLAGS_HW_VLAN_ID	0x40

	/* opcode field */
#define Q8_TX_CMD_OP_XMT_UDP_CHKSUM_IPV6	(0xC << 7)
#define Q8_TX_CMD_OP_XMT_TCP_CHKSUM_IPV6	(0xB << 7)
#define Q8_TX_CMD_OP_XMT_TCP_LSO_IPV6		(0x6 << 7)
#define Q8_TX_CMD_OP_XMT_TCP_LSO		(0x5 << 7)
#define Q8_TX_CMD_OP_XMT_UDP_CHKSUM		(0x3 << 7)
#define Q8_TX_CMD_OP_XMT_TCP_CHKSUM		(0x2 << 7)
#define Q8_TX_CMD_OP_XMT_ETHER			(0x1 << 7)

	uint8_t		n_bufs;		/* # of data segs in data buffer */
	uint8_t		data_len_lo;	/* data length lower 8 bits */
	uint16_t	data_len_hi;	/* data length upper 16 bits */

	uint64_t	buf2_addr;	/* buffer 2 address */

	uint16_t	rsrvd0;
	uint16_t	mss;		/* MSS for this packet */
	uint8_t		port_cntxtid;	/* Bits 7-4: ContextId; 3-0: reserved */

#define Q8_TX_CMD_PORT_CNXTID(c_id) ((c_id & 0xF) << 4)

	uint8_t		total_hdr_len;	/* MAC+IP+TCP Header Length for LSO */
	uint16_t	rsrvd1;

	uint64_t	buf3_addr;	/* buffer 3 address */
	uint64_t	buf1_addr;	/* buffer 1 address */

	uint16_t	buf1_len;	/* length of buffer 1 */
	uint16_t	buf2_len;	/* length of buffer 2 */
	uint16_t	buf3_len;	/* length of buffer 3 */
	uint16_t	buf4_len;	/* length of buffer 4 */

	uint64_t	buf4_addr;	/* buffer 4 address */

	uint32_t	rsrvd2;
	uint16_t	rsrvd3;
	uint16_t	vlan_tci;	/* VLAN TCI when hw tagging is enabled*/

} __packed q80_tx_cmd_t; /* 64 bytes */

#define Q8_TX_CMD_MAX_SEGMENTS	4
#define Q8_TX_CMD_TSO_ALIGN	2
#define Q8_TX_MAX_SEGMENTS	14


/*
 * Receive Related Definitions
 */
/*
 * Receive Context - Q8_CMD_CREATE_RX_CNTXT Command Configuration Data
 */

typedef struct _q80_rq_sds_ring {
	uint64_t phys_addr; /* physical addr of status ring in system memory */
	uint32_t size; /* number of entries in status ring */
	uint16_t msi_index;
	uint16_t rsrvd;
} __packed q80_rq_sds_ring_t; /* 16 bytes */

typedef struct _q80_rq_rds_ring {
	uint64_t phys_addr;	/* physical addr of rcv ring in system memory */
	uint64_t buf_size;	/* packet buffer size */
	uint32_t size;		/* number of entries in ring */
	uint32_t rsrvd;
} __packed q80_rq_rds_ring_t; /* 24 bytes */

typedef struct _q80_rq_rcv_cntxt {
	uint64_t rsp_dma_addr;	/* rsp from firmware is DMA'ed here */
	uint32_t caps[4];	/* bit vector */
#define CNTXT_CAP0_JUMBO		0x0080 /* Contiguous Jumbo buffers*/
#define CNTXT_CAP0_LRO			0x0100
#define CNTXT_CAP0_HW_LRO		0x0800 /* HW LRO */

	uint32_t intr_mode;	/* same as q80_tx_cntxt_req_t */
	uint32_t rds_intr_mode; /* same as q80_tx_cntxt_req_t */

	uint32_t rds_ring_offset; /* rds configuration relative to data[0] */
	uint32_t sds_ring_offset; /* sds configuration relative to data[0] */

	uint16_t num_rds_rings;
	uint16_t num_sds_rings;

	uint8_t rsrvd1[132];
} __packed q80_rq_rcv_cntxt_t; /* 176 bytes header + rds + sds ring rqsts */

/*
 * Receive Context - Response from Firmware to Q8_CMD_CREATE_RX_CNTXT
 */

typedef struct _q80_rsp_rds_ring {
	uint32_t producer_reg;
	uint32_t rsrvd;
} __packed q80_rsp_rds_ring_t; /* 8 bytes */

typedef struct _q80_rsp_sds_ring {
	uint32_t consumer_reg;
	uint32_t intr_mask_reg;
} __packed q80_rsp_sds_ring_t; /* 8 bytes */

typedef struct _q80_rsp_rcv_cntxt {
	uint32_t rds_ring_offset; /* rds configuration relative to data[0] */
	uint32_t sds_ring_offset; /* sds configuration relative to data[0] */

	uint32_t cntxt_state; /* starting state */
	uint32_t funcs_per_port; /* number of PCI functions sharing each port */

	uint16_t num_rds_rings;
	uint16_t num_sds_rings;

	uint16_t cntxt_id; /* handle for context */

	uint8_t phys_port; /* physical id of port */
	uint8_t virt_port; /* virtual or logical id of port */

	uint8_t rsrvd[128];
	uint8_t data[0];
} __packed q80_rsp_rcv_cntxt_t; /* 152 bytes header + rds + sds ring rspncs */


/*
 * Note:
 *	Transmit Context
 *	188 (rq) + 144 (rsp) = 332 bytes are required
 *	
 *	Receive Context
 *	1 RDS and 1 SDS rings: (16+24+176)+(8+8+152) = 384 bytes
 *
 *	3 RDS and 4 SDS rings: (((16+24)*3)+176) + (((8+8)*4)+152) =
 *				= 296 + 216 = 512 bytes
 *	Clearly this within the minimum PAGE size of most O.S platforms
 *	(typically 4Kbytes). Hence it is simpler to simply allocate one PAGE
 *	and then carve out space for each context. It is also a good idea to
 * 	to throw in the shadown register for the consumer index of the transmit
 *	ring in this PAGE.
 */

/*
 * Receive Descriptor corresponding to each entry in the receive ring
 */
typedef struct _q80_rcv_desc {
	uint16_t handle;
	uint16_t rsrvd;
	uint32_t buf_size; /* buffer size in bytes */
	uint64_t buf_addr; /* physical address of buffer */
} __packed q80_recv_desc_t;

/*
 * Status Descriptor corresponding to each entry in the Status ring
 */
typedef struct _q80_stat_desc {
	uint64_t data[2];
} __packed q80_stat_desc_t;

/*
 * definitions for data[0] field of Status Descriptor
 */
#define Q8_STAT_DESC_OWNER(data)		((data >> 56) & 0x3)
#define		Q8_STAT_DESC_OWNER_HOST		0x1
#define		Q8_STAT_DESC_OWNER_FW		0x2

#define Q8_STAT_DESC_OWNER_MASK			(((uint64_t)0x3) << 56)
#define Q8_STAT_DESC_SET_OWNER(owner)	(uint64_t)(((uint64_t)owner) << 56)

#define Q8_STAT_DESC_OPCODE(data)		((data >> 58) & 0x003F)
#define		Q8_STAT_DESC_OPCODE_SYN_OFFLOAD		0x03
#define		Q8_STAT_DESC_OPCODE_RCV_PKT		0x04
#define		Q8_STAT_DESC_OPCODE_CTRL_MSG		0x05
#define		Q8_STAT_DESC_OPCODE_LRO_PKT		0x12

/*
 * definitions for data[0] field of Status Descriptor for standard frames
 * status descriptor opcode equals 0x04
 */
#define Q8_STAT_DESC_PORT(data)			((data) & 0x000F)
#define Q8_STAT_DESC_STATUS(data)		((data >> 4) & 0x000F)
#define		Q8_STAT_DESC_STATUS_NO_CHKSUM		0x01
#define		Q8_STAT_DESC_STATUS_CHKSUM_OK		0x02
#define		Q8_STAT_DESC_STATUS_CHKSUM_ERR		0x03

#define Q8_STAT_DESC_TYPE(data)			((data >> 8) & 0x000F)
#define Q8_STAT_DESC_TOTAL_LENGTH(data)		((data >> 12) & 0xFFFF)
#define Q8_STAT_DESC_HANDLE(data)		((data >> 28) & 0xFFFF)
#define Q8_STAT_DESC_PROTOCOL(data)		((data >> 44) & 0x000F)
#define Q8_STAT_DESC_L2_OFFSET(data)		((data >> 48) & 0x001F)
#define Q8_STAT_DESC_COUNT(data)		((data >> 53) & 0x0007)

/*
 * definitions for data[0-1] fields of Status Descriptor for LRO
 * status descriptor opcode equals 0x05
 */
/* definitions for data[0] field */
#define Q8_LRO_STAT_DESC_HANDLE(data)		((data) & 0xFFFF)
#define Q8_LRO_STAT_DESC_PAYLOAD_LENGTH(data)	((data >> 16) & 0xFFFF)
#define Q8_LRO_STAT_DESC_L2_OFFSET(data)	((data >> 32) & 0xFF)
#define Q8_LRO_STAT_DESC_L4_OFFSET(data)	((data >> 40) & 0xFF)
#define Q8_LRO_STAT_DESC_TS_PRESENT(data)	((data >> 48) & 0x1)
#define Q8_LRO_STAT_DESC_TYPE(data)		((data >> 49) & 0x7)
#define Q8_LRO_STAT_DESC_PUSH_BIT(data)		((data >> 52) & 0x1)

/* definitions for data[1] field */
#define Q8_LRO_STAT_DESC_SEQ_NUM(data)		(uint32_t)(data)

/** Driver Related Definitions Begin **/

#define MAX_RDS_RINGS           2 /* Max# of Receive Descriptor Rings */
#define MAX_SDS_RINGS           4 /* Max# of Status Descriptor Rings */
#define TX_SMALL_PKT_SIZE	128 /* size in bytes of small packets */

/* The number of descriptors should be a power of 2 */
#define NUM_TX_DESCRIPTORS		2048
#define NUM_RX_DESCRIPTORS		8192
//#define NUM_RX_JUMBO_DESCRIPTORS	1024
#define NUM_RX_JUMBO_DESCRIPTORS	2048
//#define NUM_STATUS_DESCRIPTORS		8192
#define NUM_STATUS_DESCRIPTORS		2048

typedef struct _q80_rcv_cntxt_req {
	q80_rq_rcv_cntxt_t	rx_req;
	q80_rq_rds_ring_t	rds_req[MAX_RDS_RINGS];
	q80_rq_sds_ring_t	sds_req[MAX_SDS_RINGS];
} __packed q80_rcv_cntxt_req_t;

typedef struct _q80_rcv_cntxt_rsp {
	q80_rsp_rcv_cntxt_t	rx_rsp;
	q80_rsp_rds_ring_t	rds_rsp[MAX_RDS_RINGS];
	q80_rsp_sds_ring_t	sds_rsp[MAX_SDS_RINGS];
} __packed q80_rcv_cntxt_rsp_t;

/*
 * structure describing various dma buffers
 */
#define RDS_RING_INDEX_NORMAL	0
#define RDS_RING_INDEX_JUMBO	1

typedef struct qla_dmabuf {
        volatile struct {
                uint32_t        tx_ring		:1,
                                rds_ring	:1,
                                sds_ring	:1,
                                context		:1;
        } flags;

        qla_dma_t               tx_ring;
        qla_dma_t               rds_ring[MAX_RDS_RINGS];
        qla_dma_t               sds_ring[MAX_SDS_RINGS];
        qla_dma_t               context;
} qla_dmabuf_t;

/** Driver Related Definitions End **/

/*
 * Firmware Control Descriptor
 */
typedef struct _qla_fw_cds_hdr {
	uint64_t cmd; 
#define Q8_FWCD_CNTRL_REQ	(0x13 << 23)
	uint8_t	opcode;
	uint8_t cookie;
	uint16_t cntxt_id;
	uint8_t response;
#define Q8_FW_CDS_HDR_COMPLETION	0x1
	uint16_t rsrvd;
	uint8_t sub_opcode;
} __packed qla_fw_cds_hdr_t;

/*
 * definitions for opcode in qla_fw_cds_hdr_t
 */
#define Q8_FWCD_OPCODE_CONFIG_RSS		0x01
#define Q8_FWCD_OPCODE_CONFIG_RSS_TABLE		0x02
#define Q8_FWCD_OPCODE_CONFIG_INTR_COALESCING	0x03
#define Q8_FWCD_OPCODE_CONFIG_LED		0x04
#define Q8_FWCD_OPCODE_CONFIG_MAC_ADDR		0x06
#define Q8_FWCD_OPCODE_LRO_FLOW			0x07
#define Q8_FWCD_OPCODE_GET_SNMP_STATS		0x08
#define Q8_FWCD_OPCODE_CONFIG_MAC_RCV_MODE	0x0C
#define Q8_FWCD_OPCODE_STATISTICS		0x10
#define Q8_FWCD_OPCODE_CONFIG_IPADDR		0x12
#define Q8_FWCD_OPCODE_CONFIG_LOOPBACK		0x13
#define Q8_FWCD_OPCODE_LINK_EVENT_REQ		0x15
#define Q8_FWCD_OPCODE_CONFIG_BRIDGING		0x17
#define Q8_FWCD_OPCODE_CONFIG_LRO		0x18

/*
 * Configure RSS
 */
typedef struct _qla_fw_cds_config_rss {
	qla_fw_cds_hdr_t	hdr;
	uint8_t			hash_type;
#define Q8_FWCD_RSS_HASH_TYPE_IPV4_TCP		(0x2 << 4)
#define Q8_FWCD_RSS_HASH_TYPE_IPV4_IP		(0x1 << 4)
#define Q8_FWCD_RSS_HASH_TYPE_IPV4_TCP_IP	(0x3 << 4)
#define Q8_FWCD_RSS_HASH_TYPE_IPV6_TCP		(0x2 << 6)
#define Q8_FWCD_RSS_HASH_TYPE_IPV6_IP		(0x1 << 6)
#define Q8_FWCD_RSS_HASH_TYPE_IPV6_TCP_IP	(0x3 << 6)

	uint8_t			flags;
#define Q8_FWCD_RSS_FLAGS_ENABLE_RSS		0x1
#define Q8_FWCD_RSS_FLAGS_USE_IND_TABLE		0x2
	uint8_t			rsrvd[4];
	uint16_t		ind_tbl_mask;
	uint64_t		rss_key[5];
} __packed qla_fw_cds_config_rss_t;

/*
 * Configure RSS Table
 */
typedef struct _qla_fw_cds_config_rss_table {
	qla_fw_cds_hdr_t	hdr;
	uint64_t		index;
	uint8_t			table[40];
} __packed qla_fw_cds_config_rss_table_t;

/*
 * Configure Interrupt Coalescing
 */
typedef struct _qla_fw_cds_config_intr_coalesc {
	qla_fw_cds_hdr_t	hdr;
	uint16_t		rsrvd0;
	uint16_t		rsrvd1;
	uint16_t		flags;
	uint16_t		rsrvd2;
	uint64_t		rsrvd3;
	uint16_t		max_rcv_pkts;
	uint16_t		max_rcv_usecs;
	uint16_t		max_snd_pkts;
	uint16_t		max_snd_usecs;
	uint64_t		rsrvd4;
	uint64_t		rsrvd5;
	uint32_t		usecs_to;
	uint8_t			timer_type;
#define Q8_FWCMD_INTR_COALESC_TIMER_NONE	0x00
#define Q8_FWCMD_INTR_COALESC_TIMER_ONCE	0x01
#define Q8_FWCMD_INTR_COALESC_TIMER_PERIODIC	0x02

	uint8_t			sds_ring_bitmask;
#define Q8_FWCMD_INTR_COALESC_SDS_RING_0	0x01
#define Q8_FWCMD_INTR_COALESC_SDS_RING_1	0x02
#define Q8_FWCMD_INTR_COALESC_SDS_RING_2	0x04
#define Q8_FWCMD_INTR_COALESC_SDS_RING_3	0x08

	uint16_t		rsrvd6;
} __packed qla_fw_cds_config_intr_coalesc_t;

/*
 * Configure LED Parameters
 */
typedef struct _qla_fw_cds_config_led {
	qla_fw_cds_hdr_t	hdr;
	uint32_t		cntxt_id;
	uint32_t		blink_rate;
	uint32_t		blink_state;
	uint32_t		rsrvd;
} __packed qla_fw_cds_config_led_t;

/*
 * Configure MAC Address
 */
typedef struct _qla_fw_cds_config_mac_addr {
	qla_fw_cds_hdr_t	hdr;
	uint8_t			cmd;
#define Q8_FWCD_ADD_MAC_ADDR	0x1
#define Q8_FWCD_DEL_MAC_ADDR	0x2
	uint8_t			rsrvd;
	uint8_t			mac_addr[6];
} __packed qla_fw_cds_config_mac_addr_t;

/*
 * Configure Add/Delete LRO
 */
typedef struct _qla_fw_cds_config_lro {
	qla_fw_cds_hdr_t	hdr;
	uint32_t		dst_ip_addr;
	uint32_t		src_ip_addr;
	uint16_t		dst_tcp_port;
	uint16_t		src_tcp_port;
	uint8_t			ipv6;
	uint8_t			time_stamp;
	uint16_t		rsrvd;
	uint32_t		rss_hash;
	uint32_t		host_handle;
} __packed qla_fw_cds_config_lro_t;

/*
 * Get SNMP Statistics
 */
typedef struct _qla_fw_cds_get_snmp {
	qla_fw_cds_hdr_t	hdr;
	uint64_t		phys_addr;
	uint16_t		size;
	uint16_t		cntxt_id;
	uint32_t		rsrvd;
} __packed qla_fw_cds_get_snmp_t;

typedef struct _qla_snmp_stats {
	uint64_t		jabber_state;
	uint64_t		false_carrier;
	uint64_t		rsrvd;
	uint64_t		mac_cntrl;
	uint64_t		align_errors;
	uint64_t		chksum_errors;
	uint64_t		oversize_frames;
	uint64_t		tx_errors;
	uint64_t		mac_rcv_errors;
	uint64_t		phy_rcv_errors;
	uint64_t		rcv_pause;
	uint64_t		tx_pause;
} __packed qla_snmp_stats_t;

/*
 * Enable Link Event Requests
 */
typedef struct _qla_link_event_req {
	qla_fw_cds_hdr_t	hdr;
	uint8_t			enable;
	uint8_t			get_clnk_params;
	uint8_t			pad[6];
} __packed qla_link_event_req_t;


/*
 * Set MAC Receive Mode
 */
typedef struct _qla_set_mac_rcv_mode {
	qla_fw_cds_hdr_t	hdr;

	uint32_t		mode;
#define Q8_MAC_RCV_RESET_PROMISC_ALLMULTI	0x00
#define Q8_MAC_RCV_ENABLE_PROMISCUOUS		0x01
#define Q8_MAC_RCV_ENABLE_ALLMULTI		0x02

	uint8_t			pad[4];
} __packed qla_set_mac_rcv_mode_t;

/*
 * Configure IP Address
 */
typedef struct _qla_config_ipv4 {
	qla_fw_cds_hdr_t	hdr;

	uint64_t		cmd;
#define Q8_CONFIG_CMD_IP_ENABLE		0x02
#define Q8_CONFIG_CMD_IP_DISABLE	0x03

	uint64_t		ipv4_addr;
} __packed qla_config_ipv4_t;

/*
 * Configure LRO
 */
typedef struct _qla_config_lro {
	qla_fw_cds_hdr_t	hdr;

	uint64_t		cmd;
#define Q8_CONFIG_LRO_ENABLE		0x08
} __packed qla_config_lro_t;


/*
 * Control Messages Received on SDS Ring
 */
/* Header */
typedef struct _qla_cntrl_msg_hdr {
	uint16_t rsrvd0;
	uint16_t err_code;
	uint8_t  rsp_type;
	uint8_t  comp_id;
	uint16_t tag;
#define Q8_CTRL_MSG_TAG_DESC_COUNT_MASK		(0x7 << 5)
#define Q8_CTRL_MSG_TAG_OWNER_MASK		(0x3 << 8)
#define Q8_CTRL_MSG_TAG_OPCODE_MASK		(0x3F << 10)
} __packed qla_cntrl_msg_hdr_t;

/*
 * definitions for rsp_type in qla_cntrl_msg_hdr_t
 */
#define Q8_CTRL_CONFIG_MAC_RSP			0x85
#define Q8_CTRL_LRO_FLOW_DELETE_RSP		0x86
#define Q8_CTRL_LRO_FLOW_ADD_FAILURE_RSP	0x87
#define Q8_CTRL_GET_SNMP_STATS_RSP		0x88
#define Q8_CTRL_GET_NETWORK_STATS_RSP		0x8C
#define Q8_CTRL_LINK_EVENT_NOTIFICATION		0x8D

/*
 * Configure MAC Response
 */
typedef struct _qla_config_mac_rsp {
	uint32_t		rval;
	uint32_t		rsrvd;
} __packed qla_config_mac_rsp_t;

/*
 * LRO Flow Response (can be LRO Flow Delete and LRO Flow Add Failure)
 */
typedef struct _qla_lro_flow_rsp {
	uint32_t		handle;
	uint32_t		rss_hash;
	uint32_t		dst_ip;
	uint32_t		src_ip;
	uint16_t		dst_tcp_port;
	uint16_t		src_tcp_port;
	uint8_t			ipv6;
	uint8_t			rsrvd0;
	uint16_t		rsrvd1;
} __packed qla_lro_flow_rsp_t;

/*
 * Get SNMP Statistics Response
 */
typedef struct _qla_get_snmp_stats_rsp {
	uint64_t		rsrvd;
} __packed qla_get_snmp_stats_rsp_t;

/*
 * Get Network Statistics Response
 */
typedef struct _qla_get_net_stats_rsp {
	uint64_t		rsrvd;
} __packed qla_get_net_stats_rsp_t;

/*
 * Link Event Notification
 */
typedef struct _qla_link_event {
	uint32_t		cable_oui;
	uint16_t		cable_length;

	uint16_t		link_speed;
#define Q8_LE_SPEED_MASK	0xFFF
#define Q8_LE_SPEED_10GBPS	0x710
#define Q8_LE_SPEED_1GBPS	0x3E8
#define Q8_LE_SPEED_100MBPS	0x064
#define Q8_LE_SPEED_10MBPS	0x00A

	uint8_t			link_up;/* 0 = down; else up */

	uint8_t			mod_info;
#define Q8_LE_MI_MODULE_NOT_PRESENT		0x01
#define Q8_LE_MI_UNKNOWN_OPTICAL_MODULE		0x02
#define Q8_LE_MI_SR_LR_OPTICAL_MODULE		0x03
#define Q8_LE_MI_LRM_OPTICAL_MODULE		0x04
#define Q8_LE_MI_SFP_1G_MODULE			0x05
#define Q8_LE_MI_UNSUPPORTED_TWINAX		0x06
#define Q8_LE_MI_UNSUPPORTED_TWINAX_LENGTH	0x07
#define Q8_LE_MI_SUPPORTED_TWINAX		0x08

	uint8_t			fduplex; /* 1 = full duplex; 0 = half duplex */
	uint8_t			autoneg; /* 1 = autoneg enable; 0 = disabled */ 
	uint32_t		rsrvd;
} __packed qla_link_event_t;

typedef struct _qla_sds {
	q80_stat_desc_t *sds_ring_base; /* start of sds ring */
	uint32_t	sdsr_next; /* next entry in SDS ring to process */
	struct lro_ctrl	lro;
	void		*rxb_free;
	uint32_t	rx_free;
	void		*rxjb_free;
	uint32_t	rxj_free;
	volatile uint32_t rcv_active;
} qla_sds_t;

#define QL_FRAME_HDR_SIZE (ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN +\
		sizeof (struct ip) + sizeof (struct tcphdr) + 16)
/*
 * struct for storing hardware specific information for a given interface
 */
typedef struct _qla_hw {
	struct {
		uint32_t
			lro		:1,
			init_tx_cnxt	:1,
			init_rx_cnxt	:1,
			fduplex		:1,
			autoneg		:1,
			link_up		:1;
	} flags;

	uint16_t	link_speed;
	uint16_t	cable_length;
	uint16_t	cable_oui;
	uint8_t		mod_info;
	uint8_t		rsrvd;

	uint32_t	max_rds_per_cntxt;
	uint32_t	max_sds_per_cntxt;
	uint32_t	max_rules_per_cntxt;
	uint32_t	max_rcv_cntxts;
	uint32_t	max_xmt_cntxts;
	uint32_t	max_mtu;
	uint32_t	max_lro;

	uint8_t		mac_addr[ETHER_ADDR_LEN];

	uint16_t	num_rds_rings;
	uint16_t	num_sds_rings;

        qla_dmabuf_t	dma_buf;
	
	/* Transmit Side */

	q80_tx_cmd_t	*tx_ring_base;

	q80_tx_cntxt_req_t *tx_cntxt_req; /* TX Context Request */
	bus_addr_t	tx_cntxt_req_paddr;

	q80_tx_cntxt_rsp_t *tx_cntxt_rsp; /* TX Context Response */
	bus_addr_t	tx_cntxt_rsp_paddr;

	uint32_t	*tx_cons; /* tx consumer shadow reg */
	bus_addr_t	tx_cons_paddr;

	volatile uint32_t txr_free; /* # of free entries in tx ring */
	volatile uint32_t txr_next; /* # next available tx ring entry */
	volatile uint32_t txr_comp; /* index of last tx entry completed */

	uint32_t	tx_prod_reg;

	/* Receive Side */
	volatile uint32_t rx_next; /* next standard rcv ring to arm fw */
	volatile int32_t  rxj_next; /* next jumbo rcv ring to arm fw */

	volatile int32_t  rx_in; /* next standard rcv ring to add mbufs */
	volatile int32_t  rxj_in; /* next jumbo rcv ring to add mbufs */

	q80_rcv_cntxt_req_t *rx_cntxt_req; /* Rcv Context Request */
	bus_addr_t	rx_cntxt_req_paddr;
	q80_rcv_cntxt_rsp_t *rx_cntxt_rsp; /* Rcv Context Response */
	bus_addr_t	rx_cntxt_rsp_paddr;
	
	qla_sds_t	sds[MAX_SDS_RINGS]; 

	uint8_t		frame_hdr[QL_FRAME_HDR_SIZE];
} qla_hw_t;

#define QL_UPDATE_RDS_PRODUCER_INDEX(ha, i, val) \
	WRITE_REG32(ha, ((ha->hw.rx_cntxt_rsp)->rds_rsp[i].producer_reg +\
		0x1b2000), val)

#define QL_UPDATE_TX_PRODUCER_INDEX(ha, val) \
	WRITE_REG32(ha, (ha->hw.tx_prod_reg + 0x1b2000), val)

#define QL_UPDATE_SDS_CONSUMER_INDEX(ha, i, val) \
	WRITE_REG32(ha, ((ha->hw.rx_cntxt_rsp)->sds_rsp[i].consumer_reg +\
		0x1b2000), val)

#define QL_CLEAR_INTERRUPTS(ha) \
	if (ha->pci_func == 0) {\
		WRITE_REG32(ha, Q8_INT_TARGET_STATUS_F0, 0xFFFFFFFF);\
	} else {\
		WRITE_REG32(ha, Q8_INT_TARGET_STATUS_F1, 0xFFFFFFFF);\
	}\

#define QL_ENABLE_INTERRUPTS(ha, sds_index) \
	{\
		q80_rsp_sds_ring_t *rsp_sds;\
		rsp_sds = &((ha->hw.rx_cntxt_rsp)->sds_rsp[sds_index]);\
		WRITE_REG32(ha, (rsp_sds->intr_mask_reg + 0x1b2000), 0x1);\
	}

#define QL_DISABLE_INTERRUPTS(ha, sds_index) \
	{\
		q80_rsp_sds_ring_t *rsp_sds;\
		rsp_sds = &((ha->hw.rx_cntxt_rsp)->sds_rsp[sds_index]);\
		WRITE_REG32(ha, (rsp_sds->intr_mask_reg + 0x1b2000), 0x0);\
	}


#define QL_BUFFER_ALIGN                16

#endif /* #ifndef _QLA_HW_H_ */

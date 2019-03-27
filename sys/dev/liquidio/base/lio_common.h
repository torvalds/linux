/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2017 Cavium, Inc.. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Cavium, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER(S) OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*$FreeBSD$*/

/*   \file  lio_common.h
 *   \brief Common: Structures and macros used in PCI-NIC package by core and
 *   host driver.
 */

#ifndef __LIO_COMMON_H__
#define __LIO_COMMON_H__

#include "lio_config.h"

#define LIO_STR_HELPER(x)	#x
#define LIO_STR(x)	LIO_STR_HELPER(x)
#define LIO_BASE_MAJOR_VERSION	1
#define LIO_BASE_MINOR_VERSION	6
#define LIO_BASE_MICRO_VERSION	1
#define LIO_BASE_VERSION	LIO_STR(LIO_BASE_MAJOR_VERSION) "."	\
				LIO_STR(LIO_BASE_MINOR_VERSION)
#define LIO_VERSION		LIO_STR(LIO_BASE_MAJOR_VERSION) "."	\
				LIO_STR(LIO_BASE_MINOR_VERSION)		\
				"." LIO_STR(LIO_BASE_MICRO_VERSION)

struct lio_version {
	uint16_t	major;
	uint16_t	minor;
	uint16_t	micro;
	uint16_t	reserved;
};

/* Tag types used by Octeon cores in its work. */
enum lio_tag_type {
	LIO_ORDERED_TAG		= 0,
	LIO_ATOMIC_TAG		= 1,
	LIO_NULL_TAG		= 2,
	LIO_NULL_NULL_TAG	= 3
};

/* pre-defined host->NIC tag values */
#define LIO_CONTROL	(0x11111110)
#define LIO_DATA(i)	(0x11111111 + (i))

/*
 * Opcodes used by host driver/apps to perform operations on the core.
 * These are used to identify the major subsystem that the operation
 * is for.
 */
#define LIO_OPCODE_NIC	1	/* used for NIC operations */

/*
 * Subcodes are used by host driver/apps to identify the sub-operation
 * for the core. They only need to by unique for a given subsystem.
 */
#define LIO_OPCODE_SUBCODE(op, sub)	((((op) & 0x0f) << 8) | ((sub) & 0x7f))

/* OPCODE_CORE subcodes. For future use. */

/* OPCODE_NIC subcodes */

/* This subcode is sent by core PCI driver to indicate cores are ready. */
#define LIO_OPCODE_NIC_CORE_DRV_ACTIVE	0x01
#define LIO_OPCODE_NIC_NW_DATA		0x02	/* network packet data */
#define LIO_OPCODE_NIC_CMD		0x03
#define LIO_OPCODE_NIC_INFO		0x04
#define LIO_OPCODE_NIC_PORT_STATS	0x05
#define LIO_OPCODE_NIC_INTRMOD_CFG	0x08
#define LIO_OPCODE_NIC_IF_CFG		0x09
#define LIO_OPCODE_NIC_INTRMOD_PARAMS	0x0B

/* Application codes advertised by the core driver initialization packet. */
#define LIO_DRV_APP_START	0x0
#define LIO_DRV_APP_COUNT	0x2
#define LIO_DRV_NIC_APP		(LIO_DRV_APP_START + 0x1)
#define LIO_DRV_INVALID_APP	(LIO_DRV_APP_START + 0x2)
#define LIO_DRV_APP_END		(LIO_DRV_INVALID_APP - 1)

#define BYTES_PER_DHLEN_UNIT	8

#define SCR2_BIT_FW_LOADED	63
#define SCR2_BIT_FW_RELOADED	62

static inline uint32_t
lio_incr_index(uint32_t index, uint32_t count, uint32_t max)
{
	if ((index + count) >= max)
		index = index + count - max;
	else
		index += count;

	return (index);
}

#define LIO_BOARD_NAME		32
#define LIO_SERIAL_NUM_LEN	64

/*
 * Structure used by core driver to send indication that the Octeon
 * application is ready.
 */
struct lio_core_setup {
	uint64_t	corefreq;

	char		boardname[LIO_BOARD_NAME];

	char		board_serial_number[LIO_SERIAL_NUM_LEN];

	uint64_t	board_rev_major;

	uint64_t	board_rev_minor;

};

/*---------------------------  SCATTER GATHER ENTRY  -----------------------*/

/*
 * The Scatter-Gather List Entry. The scatter or gather component used with
 * a Octeon input instruction has this format.
 */
struct lio_sg_entry {
	/* The first 64 bit gives the size of data in each dptr. */
	union {
		uint16_t	size[4];
		uint64_t	size64;
	}	u;

	/* The 4 dptr pointers for this entry. */
	uint64_t	ptr[4];

};

#define LIO_SG_ENTRY_SIZE    (sizeof(struct lio_sg_entry))

/*
 * \brief Add size to gather list
 * @param sg_entry scatter/gather entry
 * @param size size to add
 * @param pos position to add it.
 */
static inline void
lio_add_sg_size(struct lio_sg_entry *sg_entry, uint16_t size, uint32_t pos)
{

#if BYTE_ORDER == BIG_ENDIAN
	sg_entry->u.size[pos] = size;
#else	/* BYTE_ORDER != BIG_ENDIAN  */
	sg_entry->u.size[3 - pos] = size;
#endif	/* BYTE_ORDER == BIG_ENDIAN  */
}

/*------------------------- End Scatter/Gather ---------------------------*/

#define LIO_FRM_HEADER_SIZE	 22	/* VLAN + Ethernet */

#define LIO_MAX_FRM_SIZE	(16000 + LIO_FRM_HEADER_SIZE)

#define LIO_DEFAULT_FRM_SIZE	(1500 + LIO_FRM_HEADER_SIZE)

/* NIC Command types */
#define LIO_CMD_CHANGE_MTU	0x1
#define LIO_CMD_CHANGE_MACADDR	0x2
#define LIO_CMD_CHANGE_DEVFLAGS	0x3
#define LIO_CMD_RX_CTL		0x4
#define LIO_CMD_SET_MULTI_LIST	0x5

/* command for setting the speed, duplex & autoneg */
#define LIO_CMD_SET_SETTINGS	0x7
#define LIO_CMD_SET_FLOW_CTL	0x8

#define LIO_CMD_GPIO_ACCESS	0xA
#define LIO_CMD_LRO_ENABLE	0xB
#define LIO_CMD_LRO_DISABLE	0xC
#define LIO_CMD_SET_RSS		0xD

#define LIO_CMD_TNL_RX_CSUM_CTL	0x10
#define LIO_CMD_TNL_TX_CSUM_CTL	0x11
#define LIO_CMD_VERBOSE_ENABLE	0x14
#define LIO_CMD_VERBOSE_DISABLE	0x15

#define LIO_CMD_VLAN_FILTER_CTL	0x16
#define LIO_CMD_ADD_VLAN_FILTER	0x17
#define LIO_CMD_DEL_VLAN_FILTER	0x18
#define LIO_CMD_VXLAN_PORT_CONFIG	0x19

#define LIO_CMD_ID_ACTIVE	0x1a

#define LIO_CMD_SET_FNV	0x1d

#define LIO_CMD_PKT_STEERING_CTL	0x1e

#define LIO_CMD_QUEUE_COUNT_CTL	0x1f

#define LIO_CMD_VXLAN_PORT_ADD	0x0
#define LIO_CMD_VXLAN_PORT_DEL	0x1
#define LIO_CMD_RXCSUM_ENABLE	0x0
#define LIO_CMD_RXCSUM_DISABLE	0x1
#define LIO_CMD_TXCSUM_ENABLE	0x0
#define LIO_CMD_TXCSUM_DISABLE	0x1
#define LIO_CMD_FNV_ENABLE	0x1
#define LIO_CMD_FNV_DISABLE	0x0
#define LIO_CMD_PKT_STEERING_ENABLE	0x0
#define LIO_CMD_PKT_STEERING_DISABLE	0x1

/* RX(packets coming from wire) Checksum verification flags */
/* TCP/UDP csum */
#define LIO_L4SUM_VERIFIED	0x1
#define LIO_IPSUM_VERIFIED	0x2

/*LROIPV4 and LROIPV6 Flags*/
#define LIO_LROIPV4	0x1
#define LIO_LROIPV6	0x2

/* Interface flags communicated between host driver and core app. */
enum lio_ifflags {
	LIO_IFFLAG_PROMISC	= 0x01,
	LIO_IFFLAG_ALLMULTI	= 0x02,
	LIO_IFFLAG_MULTICAST	= 0x04,
	LIO_IFFLAG_BROADCAST	= 0x08,
	LIO_IFFLAG_UNICAST	= 0x10
};

/*
 *   wqe
 *  ---------------  0
 * |  wqe  word0-3 |
 *  ---------------  32
 * |    PCI IH     |
 *  ---------------  40
 * |     RPTR      |
 *  ---------------  48
 * |    PCI IRH    |
 *  ---------------  56
 * |  OCT_NET_CMD  |
 *  ---------------  64
 * | Addtl 8-BData |
 * |               |
 *  ---------------
 */
union octeon_cmd {
	uint64_t	cmd64;

	struct {
#if BYTE_ORDER == BIG_ENDIAN
		uint64_t	cmd:5;

		uint64_t	more:6;	/* How many udd words follow the command */

		uint64_t	reserved:29;

		uint64_t	param1:16;

		uint64_t	param2:8;

#else	/* BYTE_ORDER != BIG_ENDIAN */

		uint64_t	param2:8;

		uint64_t	param1:16;

		uint64_t	reserved:29;

		uint64_t	more:6;

		uint64_t	cmd:5;

#endif	/* BYTE_ORDER == BIG_ENDIAN */
	}	s;

};

#define OCTEON_CMD_SIZE	(sizeof(union octeon_cmd))

/* pkiih3 + irh + ossp[0] + ossp[1] + rdp + rptr = 40 bytes */
#define LIO_SOFTCMDRESP_IH3	(40 + 8)

#define LIO_PCICMD_O3		(24 + 8)

/* Instruction Header(DPI) - for OCTEON-III models */
struct octeon_instr_ih3 {
#if BYTE_ORDER == BIG_ENDIAN

	/* Reserved3 */
	uint64_t	reserved3:1;

	/* Gather indicator 1=gather */
	uint64_t	gather:1;

	/* Data length OR no. of entries in gather list */
	uint64_t	dlengsz:14;

	/* Front Data size */
	uint64_t	fsz:6;

	/* Reserved2 */
	uint64_t	reserved2:4;

	/* PKI port kind - PKIND */
	uint64_t	pkind:6;

	/* Reserved1 */
	uint64_t	reserved1:32;

#else	/* BYTE_ORDER != BIG_ENDIAN */

	/* Reserved1 */
	uint64_t	reserved1:32;

	/* PKI port kind - PKIND */
	uint64_t	pkind:6;

	/* Reserved2 */
	uint64_t	reserved2:4;

	/* Front Data size */
	uint64_t	fsz:6;

	/* Data length OR no. of entries in gather list */
	uint64_t	dlengsz:14;

	/* Gather indicator 1=gather */
	uint64_t	gather:1;

	/* Reserved3 */
	uint64_t	reserved3:1;

#endif	/* BYTE_ORDER == BIG_ENDIAN */
};

/* Optional PKI Instruction Header(PKI IH) - for OCTEON-III models */
/* BIG ENDIAN format.   */
struct octeon_instr_pki_ih3 {
#if BYTE_ORDER == BIG_ENDIAN

	/* Wider bit */
	uint64_t	w:1;

	/* Raw mode indicator 1 = RAW */
	uint64_t	raw:1;

	/* Use Tag */
	uint64_t	utag:1;

	/* Use QPG */
	uint64_t	uqpg:1;

	/* Reserved2 */
	uint64_t	reserved2:1;

	/* Parse Mode */
	uint64_t	pm:3;

	/* Skip Length */
	uint64_t	sl:8;

	/* Use Tag Type */
	uint64_t	utt:1;

	/* Tag type */
	uint64_t	tagtype:2;

	/* Reserved1 */
	uint64_t	reserved1:2;

	/* QPG Value */
	uint64_t	qpg:11;

	/* Tag Value */
	uint64_t	tag:32;

#else	/* BYTE_ORDER != BIG_ENDIAN */

	/* Tag Value */
	uint64_t	tag:32;

	/* QPG Value */
	uint64_t	qpg:11;

	/* Reserved1 */
	uint64_t	reserved1:2;

	/* Tag type */
	uint64_t	tagtype:2;

	/* Use Tag Type */
	uint64_t	utt:1;

	/* Skip Length */
	uint64_t	sl:8;

	/* Parse Mode */
	uint64_t	pm:3;

	/* Reserved2 */
	uint64_t	reserved2:1;

	/* Use QPG */
	uint64_t	uqpg:1;

	/* Use Tag */
	uint64_t	utag:1;

	/* Raw mode indicator 1 = RAW */
	uint64_t	raw:1;

	/* Wider bit */
	uint64_t	w:1;
#endif	/* BYTE_ORDER == BIG_ENDIAN */

};

/* Input Request Header */
struct octeon_instr_irh {
#if BYTE_ORDER == BIG_ENDIAN
	uint64_t	opcode:4;
	uint64_t	rflag:1;
	uint64_t	subcode:7;
	uint64_t	vlan:12;
	uint64_t	priority:3;
	uint64_t	reserved:5;
	uint64_t	ossp:32;	/* opcode/subcode specific parameters */

#else	/* BYTE_ORDER != BIG_ENDIAN */

	uint64_t	ossp:32;	/* opcode/subcode specific parameters */
	uint64_t	reserved:5;
	uint64_t	priority:3;
	uint64_t	vlan:12;
	uint64_t	subcode:7;
	uint64_t	rflag:1;
	uint64_t	opcode:4;
#endif	/* BYTE_ORDER == BIG_ENDIAN */
};

/* Return Data Parameters */
struct octeon_instr_rdp {
#if BYTE_ORDER == BIG_ENDIAN
	uint64_t	reserved:49;
	uint64_t	pcie_port:3;
	uint64_t	rlen:12;

#else	/* BYTE_ORDER != BIG_ENDIAN */

	uint64_t	rlen:12;
	uint64_t	pcie_port:3;
	uint64_t	reserved:49;
#endif	/* BYTE_ORDER == BIG_ENDIAN */
};

/* Receive Header */
union octeon_rh {
#if BYTE_ORDER == BIG_ENDIAN
	uint64_t	rh64;
	struct {
		uint64_t	opcode:4;
		uint64_t	subcode:8;
		uint64_t	len:3;		/* additional 64-bit words */
		uint64_t	reserved:17;
		uint64_t	ossp:32;	/* opcode/subcode specific parameters */
	}	r;
	struct {
		uint64_t	opcode:4;
		uint64_t	subcode:8;
		uint64_t	len:3;		/* additional 64-bit words */
		uint64_t	extra:28;
		uint64_t	vlan:12;
		uint64_t	priority:3;
		uint64_t	csum_verified:3;/* checksum verified. */
		uint64_t	has_hwtstamp:1;	/* Has hardware timestamp. 1 = yes. */
		uint64_t	encap_on:1;
		uint64_t	has_hash:1;	/* Has hash (rth or rss). 1 = yes. */
	}	r_dh;
	struct {
		uint64_t	opcode:4;
		uint64_t	subcode:8;
		uint64_t	len:3;		/* additional 64-bit words */
		uint64_t	reserved:11;
		uint64_t	num_gmx_ports:8;
		uint64_t	max_nic_ports:10;
		uint64_t	app_cap_flags:4;
		uint64_t	app_mode:8;
		uint64_t	pkind:8;
	}	r_core_drv_init;
	struct {
		uint64_t	opcode:4;
		uint64_t	subcode:8;
		uint64_t	len:3;		/* additional 64-bit words */
		uint64_t	reserved:8;
		uint64_t	extra:25;
		uint64_t	gmxport:16;
	}	r_nic_info;
#else	/* BYTE_ORDER != BIG_ENDIAN */
	uint64_t	rh64;
	struct {
		uint64_t	ossp:32;	/* opcode/subcode specific parameters */
		uint64_t	reserved:17;
		uint64_t	len:3;		/* additional 64-bit words */
		uint64_t	subcode:8;
		uint64_t	opcode:4;
	}	r;
	struct {
		uint64_t	has_hash:1;	/* Has hash (rth or rss). 1 = yes. */
		uint64_t	encap_on:1;
		uint64_t	has_hwtstamp:1;	/* 1 = has hwtstamp */
		uint64_t	csum_verified:3;	/* checksum verified. */
		uint64_t	priority:3;
		uint64_t	vlan:12;
		uint64_t	extra:28;
		uint64_t	len:3;		/* additional 64-bit words */
		uint64_t	subcode:8;
		uint64_t	opcode:4;
	}	r_dh;
	struct {
		uint64_t	pkind:8;
		uint64_t	app_mode:8;
		uint64_t	app_cap_flags:4;
		uint64_t	max_nic_ports:10;
		uint64_t	num_gmx_ports:8;
		uint64_t	reserved:11;
		uint64_t	len:3;		/* additional 64-bit words */
		uint64_t	subcode:8;
		uint64_t	opcode:4;
	}	r_core_drv_init;
	struct {
		uint64_t	gmxport:16;
		uint64_t	extra:25;
		uint64_t	reserved:8;
		uint64_t	len:3;		/* additional 64-bit words */
		uint64_t	subcode:8;
		uint64_t	opcode:4;
	}	r_nic_info;
#endif	/* BYTE_ORDER == BIG_ENDIAN */
};

#define OCTEON_RH_SIZE (sizeof(union  octeon_rh))

union octeon_packet_params {
	uint32_t	pkt_params32;
	struct {
#if BYTE_ORDER == BIG_ENDIAN
		uint32_t	reserved:24;
		uint32_t	ip_csum:1;	/* Perform IP header checksum(s) */
		/* Perform Outer transport header checksum */
		uint32_t	transport_csum:1;
		/* Find tunnel, and perform transport csum. */
		uint32_t	tnl_csum:1;
		uint32_t	tsflag:1;	/* Timestamp this packet */
		uint32_t	ipsec_ops:4;	/* IPsec operation */

#else	/* BYTE_ORDER != BIG_ENDIAN */

		uint32_t	ipsec_ops:4;
		uint32_t	tsflag:1;
		uint32_t	tnl_csum:1;
		uint32_t	transport_csum:1;
		uint32_t	ip_csum:1;
		uint32_t	reserved:24;
#endif	/* BYTE_ORDER == BIG_ENDIAN */
	}	s;
};

/* Status of a RGMII Link on Octeon as seen by core driver. */
union octeon_link_status {
	uint64_t	link_status64;

	struct {
#if BYTE_ORDER == BIG_ENDIAN
		uint64_t	duplex:8;
		uint64_t	mtu:16;
		uint64_t	speed:16;
		uint64_t	link_up:1;
		uint64_t	autoneg:1;
		uint64_t	if_mode:5;
		uint64_t	pause:1;
		uint64_t	flashing:1;
		uint64_t	reserved:15;

#else	/* BYTE_ORDER != BIG_ENDIAN */

		uint64_t	reserved:15;
		uint64_t	flashing:1;
		uint64_t	pause:1;
		uint64_t	if_mode:5;
		uint64_t	autoneg:1;
		uint64_t	link_up:1;
		uint64_t	speed:16;
		uint64_t	mtu:16;
		uint64_t	duplex:8;
#endif	/* BYTE_ORDER == BIG_ENDIAN */
	}	s;
};

/* The txpciq info passed to host from the firmware */

union octeon_txpciq {
	uint64_t	txpciq64;

	struct {
#if BYTE_ORDER == BIG_ENDIAN
		uint64_t	q_no:8;
		uint64_t	port:8;
		uint64_t	pkind:6;
		uint64_t	use_qpg:1;
		uint64_t	qpg:11;
		uint64_t	aura_num:10;
		uint64_t	reserved:20;

#else	/* BYTE_ORDER != BIG_ENDIAN */

		uint64_t	reserved:20;
		uint64_t	aura_num:10;
		uint64_t	qpg:11;
		uint64_t	use_qpg:1;
		uint64_t	pkind:6;
		uint64_t	port:8;
		uint64_t	q_no:8;
#endif	/* BYTE_ORDER == BIG_ENDIAN */
	}	s;
};

/* The rxpciq info passed to host from the firmware */

union octeon_rxpciq {
	uint64_t	rxpciq64;

	struct {
#if BYTE_ORDER == BIG_ENDIAN
		uint64_t	q_no:8;
		uint64_t	reserved:56;

#else	/* BYTE_ORDER != BIG_ENDIAN */

		uint64_t	reserved:56;
		uint64_t	q_no:8;
#endif	/* BYTE_ORDER == BIG_ENDIAN */
	}	s;
};

/* Information for a OCTEON ethernet interface shared between core & host. */
struct octeon_link_info {
	union octeon_link_status link;
	uint64_t	hw_addr;

#if BYTE_ORDER == BIG_ENDIAN
	uint64_t	gmxport:16;
	uint64_t	macaddr_is_admin_asgnd:1;
	uint64_t	vlan_is_admin_assigned:1;
	uint64_t	rsvd:30;
	uint64_t	num_txpciq:8;
	uint64_t	num_rxpciq:8;

#else	/* BYTE_ORDER != BIG_ENDIAN */

	uint64_t	num_rxpciq:8;
	uint64_t	num_txpciq:8;
	uint64_t	rsvd:30;
	uint64_t	vlan_is_admin_assigned:1;
	uint64_t	macaddr_is_admin_asgnd:1;
	uint64_t	gmxport:16;
#endif	/* BYTE_ORDER == BIG_ENDIAN */

	union octeon_txpciq txpciq[LIO_MAX_IOQS_PER_NICIF];
	union octeon_rxpciq rxpciq[LIO_MAX_IOQS_PER_NICIF];
};

struct octeon_if_cfg_info {
	uint64_t		iqmask;		/* mask for IQs enabled for  the port */
	uint64_t		oqmask;		/* mask for OQs enabled for the port */
	struct octeon_link_info linfo;	/* initial link information */
	char			lio_firmware_version[32];
};

/* Stats for each NIC port in RX direction. */
struct octeon_rx_stats {
	/* link-level stats */
	uint64_t	total_rcvd;
	uint64_t	bytes_rcvd;
	uint64_t	total_bcst;
	uint64_t	total_mcst;
	uint64_t	runts;
	uint64_t	ctl_rcvd;
	uint64_t	fifo_err;		/* Accounts for over/under-run of buffers */
	uint64_t	dmac_drop;
	uint64_t	fcs_err;
	uint64_t	jabber_err;
	uint64_t	l2_err;
	uint64_t	frame_err;

	/* firmware stats */
	uint64_t	fw_total_rcvd;
	uint64_t	fw_total_fwd;
	uint64_t	fw_total_fwd_bytes;
	uint64_t	fw_err_pko;
	uint64_t	fw_err_link;
	uint64_t	fw_err_drop;
	uint64_t	fw_rx_vxlan;
	uint64_t	fw_rx_vxlan_err;

	/* LRO */
	uint64_t	fw_lro_pkts;		/* Number of packets that are LROed */
	uint64_t	fw_lro_octs;		/* Number of octets that are LROed */
	uint64_t	fw_total_lro;		/* Number of LRO packets formed */
	uint64_t	fw_lro_aborts;		/* Number of times lRO of packet aborted */
	uint64_t	fw_lro_aborts_port;
	uint64_t	fw_lro_aborts_seq;
	uint64_t	fw_lro_aborts_tsval;
	uint64_t	fw_lro_aborts_timer;
	/* intrmod: packet forward rate */
	uint64_t	fwd_rate;
};

/* Stats for each NIC port in RX direction. */
struct octeon_tx_stats {
	/* link-level stats */
	uint64_t	total_pkts_sent;
	uint64_t	total_bytes_sent;
	uint64_t	mcast_pkts_sent;
	uint64_t	bcast_pkts_sent;
	uint64_t	ctl_sent;
	uint64_t	one_collision_sent;	/* Packets sent after one collision */
	uint64_t	multi_collision_sent;	/* Packets sent after multiple collision */
	uint64_t	max_collision_fail;	/* Packets not sent due to max collisions */
	uint64_t	max_deferral_fail;	/* Packets not sent due to max deferrals */
	uint64_t	fifo_err;		/* Accounts for over/under-run of buffers */
	uint64_t	runts;
	uint64_t	total_collisions;	/* Total number of collisions detected */

	/* firmware stats */
	uint64_t	fw_total_sent;
	uint64_t	fw_total_fwd;
	uint64_t	fw_total_fwd_bytes;
	uint64_t	fw_err_pko;
	uint64_t	fw_err_link;
	uint64_t	fw_err_drop;
	uint64_t	fw_err_tso;
	uint64_t	fw_tso;			/* number of tso requests */
	uint64_t	fw_tso_fwd;		/* number of packets segmented in tso */
	uint64_t	fw_tx_vxlan;
	uint64_t	fw_err_pki;
};

struct octeon_link_stats {
	struct octeon_rx_stats	fromwire;
	struct octeon_tx_stats	fromhost;

};

static inline int
lio_opcode_slow_path(union octeon_rh *rh)
{
	uint16_t	subcode1, subcode2;

	subcode1 = LIO_OPCODE_SUBCODE((rh)->r.opcode, (rh)->r.subcode);
	subcode2 = LIO_OPCODE_SUBCODE(LIO_OPCODE_NIC, LIO_OPCODE_NIC_NW_DATA);

	return (subcode2 != subcode1);
}

struct octeon_mdio_cmd {
	uint64_t	op;
	uint64_t	mdio_addr;
	uint64_t	value1;
	uint64_t	value2;
	uint64_t	value3;
};

struct octeon_intrmod_cfg {
	uint64_t	rx_enable;
	uint64_t	tx_enable;
	uint64_t	check_intrvl;
	uint64_t	maxpkt_ratethr;
	uint64_t	minpkt_ratethr;
	uint64_t	rx_maxcnt_trigger;
	uint64_t	rx_mincnt_trigger;
	uint64_t	rx_maxtmr_trigger;
	uint64_t	rx_mintmr_trigger;
	uint64_t	tx_mincnt_trigger;
	uint64_t	tx_maxcnt_trigger;
	uint64_t	rx_frames;
	uint64_t	tx_frames;
	uint64_t	rx_usecs;
};

#define LIO_BASE_QUEUE_NOT_REQUESTED	65535

union octeon_if_cfg {
	uint64_t	if_cfg64;
	struct {
#if BYTE_ORDER == BIG_ENDIAN
		uint64_t	base_queue:16;
		uint64_t	num_iqueues:16;
		uint64_t	num_oqueues:16;
		uint64_t	gmx_port_id:8;
		uint64_t	vf_id:8;

#else	/* BYTE_ORDER != BIG_ENDIAN */

		uint64_t	vf_id:8;
		uint64_t	gmx_port_id:8;
		uint64_t	num_oqueues:16;
		uint64_t	num_iqueues:16;
		uint64_t	base_queue:16;
#endif	/* BYTE_ORDER == BIG_ENDIAN */
	}	s;
};

#endif	/* __LIO_COMMON_H__ */

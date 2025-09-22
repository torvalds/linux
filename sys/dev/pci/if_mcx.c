/*	$OpenBSD: if_mcx.c,v 1.119 2025/03/05 06:44:02 dlg Exp $ */

/*
 * Copyright (c) 2017 David Gwynne <dlg@openbsd.org>
 * Copyright (c) 2019 Jonathan Matthew <jmatthew@openbsd.org>
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

#include "bpfilter.h"
#include "vlan.h"
#include "kstat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/device.h>
#include <sys/timeout.h>
#include <sys/task.h>
#include <sys/atomic.h>
#include <sys/timetc.h>
#include <sys/intrmap.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/toeplitz.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#if NKSTAT > 0
#include <sys/kstat.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#define BUS_DMASYNC_PRERW	(BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE)
#define BUS_DMASYNC_POSTRW	(BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE)

#define MCX_HCA_BAR	PCI_MAPREG_START /* BAR 0 */

#define MCX_FW_VER			0x0000
#define  MCX_FW_VER_MAJOR(_v)			((_v) & 0xffff)
#define  MCX_FW_VER_MINOR(_v)			((_v) >> 16)
#define MCX_CMDIF_FW_SUBVER		0x0004
#define  MCX_FW_VER_SUBMINOR(_v)		((_v) & 0xffff)
#define  MCX_CMDIF(_v)				((_v) >> 16)

#define MCX_ISSI			1 /* as per the PRM */
#define MCX_CMD_IF_SUPPORTED		5

#define MCX_HARDMTU			9500

enum mcx_cmdq_slot {
	MCX_CMDQ_SLOT_POLL = 0,
	MCX_CMDQ_SLOT_IOCTL,
	MCX_CMDQ_SLOT_KSTAT,
	MCX_CMDQ_SLOT_LINK,

	MCX_CMDQ_NUM_SLOTS
};

#define MCX_PAGE_SHIFT			12
#define MCX_PAGE_SIZE			(1 << MCX_PAGE_SHIFT)

/* queue sizes */
#define MCX_LOG_EQ_SIZE			7
#define MCX_LOG_CQ_SIZE			12
#define MCX_LOG_RQ_SIZE			10
#define MCX_LOG_SQ_SIZE			11

#define MCX_MAX_QUEUES			16

/* completion event moderation - about 10khz, or 90% of the cq */
#define MCX_CQ_MOD_PERIOD		50
#define MCX_CQ_MOD_COUNTER		\
	(((1 << (MCX_LOG_CQ_SIZE - 1)) * 9) / 10)

#define MCX_LOG_SQ_ENTRY_SIZE		6
#define MCX_SQ_ENTRY_MAX_SLOTS		4
#define MCX_SQ_SEGS_PER_SLOT		\
	(sizeof(struct mcx_sq_entry) / sizeof(struct mcx_sq_entry_seg))
#define MCX_SQ_MAX_SEGMENTS		\
	1 + ((MCX_SQ_ENTRY_MAX_SLOTS-1) * MCX_SQ_SEGS_PER_SLOT)

#define MCX_LOG_FLOW_TABLE_SIZE		5
#define MCX_NUM_STATIC_FLOWS		4 /* promisc, allmulti, ucast, bcast */
#define MCX_NUM_MCAST_FLOWS 		\
	((1 << MCX_LOG_FLOW_TABLE_SIZE) - MCX_NUM_STATIC_FLOWS)

#define MCX_SQ_INLINE_SIZE		18
CTASSERT(ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN == MCX_SQ_INLINE_SIZE);

/* doorbell offsets */
#define MCX_DOORBELL_AREA_SIZE		MCX_PAGE_SIZE

#define MCX_CQ_DOORBELL_BASE		0
#define MCX_CQ_DOORBELL_STRIDE		64

#define MCX_WQ_DOORBELL_BASE		MCX_PAGE_SIZE/2
#define MCX_WQ_DOORBELL_STRIDE		64
/* make sure the doorbells fit */
CTASSERT(MCX_MAX_QUEUES * MCX_CQ_DOORBELL_STRIDE < MCX_WQ_DOORBELL_BASE);
CTASSERT(MCX_MAX_QUEUES * MCX_WQ_DOORBELL_STRIDE <
    MCX_DOORBELL_AREA_SIZE - MCX_WQ_DOORBELL_BASE);

#define MCX_WQ_DOORBELL_MASK		0xffff

/* uar registers */
#define MCX_UAR_CQ_DOORBELL		0x20
#define MCX_UAR_EQ_DOORBELL_ARM		0x40
#define MCX_UAR_EQ_DOORBELL		0x48
#define MCX_UAR_BF			0x800

#define MCX_CMDQ_ADDR_HI		0x0010
#define MCX_CMDQ_ADDR_LO		0x0014
#define MCX_CMDQ_ADDR_NMASK		0xfff
#define MCX_CMDQ_LOG_SIZE(_v)		((_v) >> 4 & 0xf)
#define MCX_CMDQ_LOG_STRIDE(_v)		((_v) >> 0 & 0xf)
#define MCX_CMDQ_INTERFACE_MASK		(0x3 << 8)
#define MCX_CMDQ_INTERFACE_FULL_DRIVER	(0x0 << 8)
#define MCX_CMDQ_INTERFACE_DISABLED	(0x1 << 8)

#define MCX_CMDQ_DOORBELL		0x0018

#define MCX_STATE			0x01fc
#define MCX_STATE_MASK				(1U << 31)
#define MCX_STATE_INITIALIZING			(1U << 31)
#define MCX_STATE_READY				(0 << 31)
#define MCX_STATE_INTERFACE_MASK		(0x3 << 24)
#define MCX_STATE_INTERFACE_FULL_DRIVER		(0x0 << 24)
#define MCX_STATE_INTERFACE_DISABLED		(0x1 << 24)

#define MCX_INTERNAL_TIMER		0x1000
#define MCX_INTERNAL_TIMER_H		0x1000
#define MCX_INTERNAL_TIMER_L		0x1004

#define MCX_CLEAR_INT			0x100c

#define MCX_REG_OP_WRITE		0
#define MCX_REG_OP_READ			1

#define MCX_REG_PMLP			0x5002
#define MCX_REG_PMTU			0x5003
#define MCX_REG_PTYS			0x5004
#define MCX_REG_PAOS			0x5006
#define MCX_REG_PFCC			0x5007
#define MCX_REG_PPCNT			0x5008
#define MCX_REG_MTCAP			0x9009 /* mgmt temp capabilities */
#define MCX_REG_MTMP			0x900a /* mgmt temp */
#define MCX_REG_MCIA			0x9014
#define MCX_REG_MCAM			0x907f

#define MCX_ETHER_CAP_SGMII		0
#define MCX_ETHER_CAP_1000_KX		1
#define MCX_ETHER_CAP_10G_CX4		2
#define MCX_ETHER_CAP_10G_KX4		3
#define MCX_ETHER_CAP_10G_KR		4
#define MCX_ETHER_CAP_40G_CR4		6
#define MCX_ETHER_CAP_40G_KR4		7
#define MCX_ETHER_CAP_10G_CR		12
#define MCX_ETHER_CAP_10G_SR		13
#define MCX_ETHER_CAP_10G_LR		14
#define MCX_ETHER_CAP_40G_SR4		15
#define MCX_ETHER_CAP_40G_LR4		16
#define MCX_ETHER_CAP_50G_SR2		18
#define MCX_ETHER_CAP_100G_CR4		20
#define MCX_ETHER_CAP_100G_SR4		21
#define MCX_ETHER_CAP_100G_KR4		22
#define MCX_ETHER_CAP_100G_LR4		23
#define MCX_ETHER_CAP_25G_CR		27
#define MCX_ETHER_CAP_25G_KR		28
#define MCX_ETHER_CAP_25G_SR		29
#define MCX_ETHER_CAP_50G_CR2		30
#define MCX_ETHER_CAP_50G_KR2		31

#define MCX_ETHER_EXT_CAP_SGMII_100	0
#define MCX_ETHER_EXT_CAP_1000_X	1
#define MCX_ETHER_EXT_CAP_5G_R		3
#define MCX_ETHER_EXT_CAP_XAUI		4
#define MCX_ETHER_EXT_CAP_XLAUI		5
#define MCX_ETHER_EXT_CAP_25G_AUI1	6
#define MCX_ETHER_EXT_CAP_50G_AUI2	7
#define MCX_ETHER_EXT_CAP_50G_AUI1	8
#define MCX_ETHER_EXT_CAP_CAUI4		9
#define MCX_ETHER_EXT_CAP_100G_AUI2	10
#define MCX_ETHER_EXT_CAP_200G_AUI4	12
#define MCX_ETHER_EXT_CAP_400G_AUI8	15

#define MCX_MAX_CQE			32

#define MCX_CMD_QUERY_HCA_CAP		0x100
#define MCX_CMD_QUERY_ADAPTER		0x101
#define MCX_CMD_INIT_HCA		0x102
#define MCX_CMD_TEARDOWN_HCA		0x103
#define MCX_CMD_ENABLE_HCA		0x104
#define MCX_CMD_DISABLE_HCA		0x105
#define MCX_CMD_QUERY_PAGES		0x107
#define MCX_CMD_MANAGE_PAGES		0x108
#define MCX_CMD_SET_HCA_CAP		0x109
#define MCX_CMD_QUERY_ISSI		0x10a
#define MCX_CMD_SET_ISSI		0x10b
#define MCX_CMD_SET_DRIVER_VERSION	0x10d
#define MCX_CMD_QUERY_SPECIAL_CONTEXTS	0x203
#define MCX_CMD_CREATE_EQ		0x301
#define MCX_CMD_DESTROY_EQ		0x302
#define MCX_CMD_QUERY_EQ		0x303
#define MCX_CMD_CREATE_CQ		0x400
#define MCX_CMD_DESTROY_CQ		0x401
#define MCX_CMD_QUERY_CQ		0x402
#define MCX_CMD_QUERY_NIC_VPORT_CONTEXT	0x754
#define MCX_CMD_MODIFY_NIC_VPORT_CONTEXT \
					0x755
#define MCX_CMD_QUERY_VPORT_COUNTERS	0x770
#define MCX_CMD_ALLOC_PD		0x800
#define MCX_CMD_ALLOC_UAR		0x802
#define MCX_CMD_ACCESS_REG		0x805
#define MCX_CMD_ALLOC_TRANSPORT_DOMAIN	0x816
#define MCX_CMD_CREATE_TIR		0x900
#define MCX_CMD_DESTROY_TIR		0x902
#define MCX_CMD_CREATE_SQ		0x904
#define MCX_CMD_MODIFY_SQ		0x905
#define MCX_CMD_DESTROY_SQ		0x906
#define MCX_CMD_QUERY_SQ		0x907
#define MCX_CMD_CREATE_RQ		0x908
#define MCX_CMD_MODIFY_RQ		0x909
#define MCX_CMD_DESTROY_RQ		0x90a
#define MCX_CMD_QUERY_RQ		0x90b
#define MCX_CMD_CREATE_TIS		0x912
#define MCX_CMD_DESTROY_TIS		0x914
#define MCX_CMD_CREATE_RQT		0x916
#define MCX_CMD_DESTROY_RQT		0x918
#define MCX_CMD_SET_FLOW_TABLE_ROOT	0x92f
#define MCX_CMD_CREATE_FLOW_TABLE	0x930
#define MCX_CMD_DESTROY_FLOW_TABLE	0x931
#define MCX_CMD_QUERY_FLOW_TABLE	0x932
#define MCX_CMD_CREATE_FLOW_GROUP	0x933
#define MCX_CMD_DESTROY_FLOW_GROUP	0x934
#define MCX_CMD_QUERY_FLOW_GROUP	0x935
#define MCX_CMD_SET_FLOW_TABLE_ENTRY	0x936
#define MCX_CMD_QUERY_FLOW_TABLE_ENTRY	0x937
#define MCX_CMD_DELETE_FLOW_TABLE_ENTRY	0x938
#define MCX_CMD_ALLOC_FLOW_COUNTER	0x939
#define MCX_CMD_QUERY_FLOW_COUNTER	0x93b

#define MCX_QUEUE_STATE_RST		0
#define MCX_QUEUE_STATE_RDY		1
#define MCX_QUEUE_STATE_ERR		3

#define MCX_FLOW_TABLE_TYPE_RX		0
#define MCX_FLOW_TABLE_TYPE_TX		1

#define MCX_CMDQ_INLINE_DATASIZE 16

struct mcx_cmdq_entry {
	uint8_t			cq_type;
#define MCX_CMDQ_TYPE_PCIE		0x7
	uint8_t			cq_reserved0[3];

	uint32_t		cq_input_length;
	uint64_t		cq_input_ptr;
	uint8_t			cq_input_data[MCX_CMDQ_INLINE_DATASIZE];

	uint8_t			cq_output_data[MCX_CMDQ_INLINE_DATASIZE];
	uint64_t		cq_output_ptr;
	uint32_t		cq_output_length;

	uint8_t			cq_token;
	uint8_t			cq_signature;
	uint8_t			cq_reserved1[1];
	uint8_t			cq_status;
#define MCX_CQ_STATUS_SHIFT		1
#define MCX_CQ_STATUS_MASK		(0x7f << MCX_CQ_STATUS_SHIFT)
#define MCX_CQ_STATUS_OK		(0x00 << MCX_CQ_STATUS_SHIFT)
#define MCX_CQ_STATUS_INT_ERR		(0x01 << MCX_CQ_STATUS_SHIFT)
#define MCX_CQ_STATUS_BAD_OPCODE	(0x02 << MCX_CQ_STATUS_SHIFT)
#define MCX_CQ_STATUS_BAD_PARAM		(0x03 << MCX_CQ_STATUS_SHIFT)
#define MCX_CQ_STATUS_BAD_SYS_STATE	(0x04 << MCX_CQ_STATUS_SHIFT)
#define MCX_CQ_STATUS_BAD_RESOURCE	(0x05 << MCX_CQ_STATUS_SHIFT)
#define MCX_CQ_STATUS_RESOURCE_BUSY	(0x06 << MCX_CQ_STATUS_SHIFT)
#define MCX_CQ_STATUS_EXCEED_LIM	(0x08 << MCX_CQ_STATUS_SHIFT)
#define MCX_CQ_STATUS_BAD_RES_STATE	(0x09 << MCX_CQ_STATUS_SHIFT)
#define MCX_CQ_STATUS_BAD_INDEX		(0x0a << MCX_CQ_STATUS_SHIFT)
#define MCX_CQ_STATUS_NO_RESOURCES	(0x0f << MCX_CQ_STATUS_SHIFT)
#define MCX_CQ_STATUS_BAD_INPUT_LEN	(0x50 << MCX_CQ_STATUS_SHIFT)
#define MCX_CQ_STATUS_BAD_OUTPUT_LEN	(0x51 << MCX_CQ_STATUS_SHIFT)
#define MCX_CQ_STATUS_BAD_RESOURCE_STATE \
					(0x10 << MCX_CQ_STATUS_SHIFT)
#define MCX_CQ_STATUS_BAD_SIZE		(0x40 << MCX_CQ_STATUS_SHIFT)
#define MCX_CQ_STATUS_OWN_MASK		0x1
#define MCX_CQ_STATUS_OWN_SW		0x0
#define MCX_CQ_STATUS_OWN_HW		0x1
} __packed __aligned(8);

#define MCX_CMDQ_MAILBOX_DATASIZE	512

struct mcx_cmdq_mailbox {
	uint8_t			mb_data[MCX_CMDQ_MAILBOX_DATASIZE];
	uint8_t			mb_reserved0[48];
	uint64_t		mb_next_ptr;
	uint32_t		mb_block_number;
	uint8_t			mb_reserved1[1];
	uint8_t			mb_token;
	uint8_t			mb_ctrl_signature;
	uint8_t			mb_signature;
} __packed __aligned(8);

#define MCX_CMDQ_MAILBOX_ALIGN	(1 << 10)
#define MCX_CMDQ_MAILBOX_SIZE	roundup(sizeof(struct mcx_cmdq_mailbox), \
				    MCX_CMDQ_MAILBOX_ALIGN)
/*
 * command mailbox structures
 */

struct mcx_cmd_enable_hca_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[2];
	uint16_t		cmd_function_id;
	uint8_t			cmd_reserved2[4];
} __packed __aligned(4);

struct mcx_cmd_enable_hca_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_init_hca_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_init_hca_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_teardown_hca_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[2];
#define MCX_CMD_TEARDOWN_HCA_GRACEFUL	0x0
#define MCX_CMD_TEARDOWN_HCA_PANIC	0x1
	uint16_t		cmd_profile;
	uint8_t			cmd_reserved2[4];
} __packed __aligned(4);

struct mcx_cmd_teardown_hca_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_access_reg_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[2];
	uint16_t		cmd_register_id;
	uint32_t		cmd_argument;
} __packed __aligned(4);

struct mcx_cmd_access_reg_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_reg_pmtu {
	uint8_t			rp_reserved1;
	uint8_t			rp_local_port;
	uint8_t			rp_reserved2[2];
	uint16_t		rp_max_mtu;
	uint8_t			rp_reserved3[2];
	uint16_t		rp_admin_mtu;
	uint8_t			rp_reserved4[2];
	uint16_t		rp_oper_mtu;
	uint8_t			rp_reserved5[2];
} __packed __aligned(4);

struct mcx_reg_ptys {
	uint8_t			rp_reserved1;
	uint8_t			rp_local_port;
	uint8_t			rp_reserved2;
	uint8_t			rp_proto_mask;
#define MCX_REG_PTYS_PROTO_MASK_ETH		(1 << 2)
	uint8_t			rp_reserved3[4];
	uint32_t		rp_ext_eth_proto_cap;
	uint32_t		rp_eth_proto_cap;
	uint8_t			rp_reserved4[4];
	uint32_t		rp_ext_eth_proto_admin;
	uint32_t		rp_eth_proto_admin;
	uint8_t			rp_reserved5[4];
	uint32_t		rp_ext_eth_proto_oper;
	uint32_t		rp_eth_proto_oper;
	uint8_t			rp_reserved6[24];
} __packed __aligned(4);

struct mcx_reg_paos {
	uint8_t			rp_reserved1;
	uint8_t			rp_local_port;
	uint8_t			rp_admin_status;
#define MCX_REG_PAOS_ADMIN_STATUS_UP		1
#define MCX_REG_PAOS_ADMIN_STATUS_DOWN		2
#define MCX_REG_PAOS_ADMIN_STATUS_UP_ONCE	3
#define MCX_REG_PAOS_ADMIN_STATUS_DISABLED	4
	uint8_t			rp_oper_status;
#define MCX_REG_PAOS_OPER_STATUS_UP		1
#define MCX_REG_PAOS_OPER_STATUS_DOWN		2
#define MCX_REG_PAOS_OPER_STATUS_FAILED		4
	uint8_t			rp_admin_state_update;
#define MCX_REG_PAOS_ADMIN_STATE_UPDATE_EN	(1 << 7)
	uint8_t			rp_reserved2[11];
} __packed __aligned(4);

struct mcx_reg_pfcc {
	uint8_t			rp_reserved1;
	uint8_t			rp_local_port;
	uint8_t			rp_reserved2[3];
	uint8_t			rp_prio_mask_tx;
	uint8_t			rp_reserved3;
	uint8_t			rp_prio_mask_rx;
	uint8_t			rp_pptx_aptx;
	uint8_t			rp_pfctx;
	uint8_t			rp_fctx_dis;
	uint8_t			rp_reserved4;
	uint8_t			rp_pprx_aprx;
	uint8_t			rp_pfcrx;
	uint8_t			rp_reserved5[2];
	uint16_t		rp_dev_stall_min;
	uint16_t		rp_dev_stall_crit;
	uint8_t			rp_reserved6[12];
} __packed __aligned(4);

#define MCX_PMLP_MODULE_NUM_MASK	0xff
struct mcx_reg_pmlp {
	uint8_t			rp_rxtx;
	uint8_t			rp_local_port;
	uint8_t			rp_reserved0;
	uint8_t			rp_width;
	uint32_t		rp_lane0_mapping;
	uint32_t		rp_lane1_mapping;
	uint32_t		rp_lane2_mapping;
	uint32_t		rp_lane3_mapping;
	uint8_t			rp_reserved1[44];
} __packed __aligned(4);

struct mcx_reg_ppcnt {
	uint8_t			ppcnt_swid;
	uint8_t			ppcnt_local_port;
	uint8_t			ppcnt_pnat;
	uint8_t			ppcnt_grp;
#define MCX_REG_PPCNT_GRP_IEEE8023		0x00
#define MCX_REG_PPCNT_GRP_RFC2863		0x01
#define MCX_REG_PPCNT_GRP_RFC2819		0x02
#define MCX_REG_PPCNT_GRP_RFC3635		0x03
#define MCX_REG_PPCNT_GRP_PER_PRIO		0x10
#define MCX_REG_PPCNT_GRP_PER_TC		0x11
#define MCX_REG_PPCNT_GRP_PER_RX_BUFFER		0x11

	uint8_t			ppcnt_clr;
	uint8_t			ppcnt_reserved1[2];
	uint8_t			ppcnt_prio_tc;
#define MCX_REG_PPCNT_CLR			(1 << 7)

	uint8_t			ppcnt_counter_set[248];
} __packed __aligned(8);
CTASSERT(sizeof(struct mcx_reg_ppcnt) == 256);
CTASSERT((offsetof(struct mcx_reg_ppcnt, ppcnt_counter_set) %
    sizeof(uint64_t)) == 0);

enum mcx_ppcnt_ieee8023 {
	frames_transmitted_ok,
	frames_received_ok,
	frame_check_sequence_errors,
	alignment_errors,
	octets_transmitted_ok,
	octets_received_ok,
	multicast_frames_xmitted_ok,
	broadcast_frames_xmitted_ok,
	multicast_frames_received_ok,
	broadcast_frames_received_ok,
	in_range_length_errors,
	out_of_range_length_field,
	frame_too_long_errors,
	symbol_error_during_carrier,
	mac_control_frames_transmitted,
	mac_control_frames_received,
	unsupported_opcodes_received,
	pause_mac_ctrl_frames_received,
	pause_mac_ctrl_frames_transmitted,

	mcx_ppcnt_ieee8023_count
};
CTASSERT(mcx_ppcnt_ieee8023_count * sizeof(uint64_t) == 0x98);

enum mcx_ppcnt_rfc2863 {
	in_octets,
	in_ucast_pkts,
	in_discards,
	in_errors,
	in_unknown_protos,
	out_octets,
	out_ucast_pkts,
	out_discards,
	out_errors,
	in_multicast_pkts,
	in_broadcast_pkts,
	out_multicast_pkts,
	out_broadcast_pkts,

	mcx_ppcnt_rfc2863_count
};
CTASSERT(mcx_ppcnt_rfc2863_count * sizeof(uint64_t) == 0x68);

enum mcx_ppcnt_rfc2819 {
	drop_events,
	octets,
	pkts,
	broadcast_pkts,
	multicast_pkts,
	crc_align_errors,
	undersize_pkts,
	oversize_pkts,
	fragments,
	jabbers,
	collisions,
	pkts64octets,
	pkts65to127octets,
	pkts128to255octets,
	pkts256to511octets,
	pkts512to1023octets,
	pkts1024to1518octets,
	pkts1519to2047octets,
	pkts2048to4095octets,
	pkts4096to8191octets,
	pkts8192to10239octets,

	mcx_ppcnt_rfc2819_count
};
CTASSERT((mcx_ppcnt_rfc2819_count * sizeof(uint64_t)) == 0xa8);

enum mcx_ppcnt_rfc3635 {
	dot3stats_alignment_errors,
	dot3stats_fcs_errors,
	dot3stats_single_collision_frames,
	dot3stats_multiple_collision_frames,
	dot3stats_sqe_test_errors,
	dot3stats_deferred_transmissions,
	dot3stats_late_collisions,
	dot3stats_excessive_collisions,
	dot3stats_internal_mac_transmit_errors,
	dot3stats_carrier_sense_errors,
	dot3stats_frame_too_longs,
	dot3stats_internal_mac_receive_errors,
	dot3stats_symbol_errors,
	dot3control_in_unknown_opcodes,
	dot3in_pause_frames,
	dot3out_pause_frames,

	mcx_ppcnt_rfc3635_count
};
CTASSERT((mcx_ppcnt_rfc3635_count * sizeof(uint64_t)) == 0x80);

struct mcx_reg_mcam {
	uint8_t			_reserved1[1];
	uint8_t			mcam_feature_group;
	uint8_t			_reserved2[1];
	uint8_t			mcam_access_reg_group;
	uint8_t			_reserved3[4];
	uint8_t			mcam_access_reg_cap_mask[16];
	uint8_t			_reserved4[16];
	uint8_t			mcam_feature_cap_mask[16];
	uint8_t			_reserved5[16];
} __packed __aligned(4);

#define MCX_BITFIELD_BIT(bf, b)	(bf[(sizeof bf - 1) - (b / 8)] & (b % 8))

#define MCX_MCAM_FEATURE_CAP_SENSOR_MAP	6

struct mcx_reg_mtcap {
	uint8_t			_reserved1[3];
	uint8_t			mtcap_sensor_count;
	uint8_t			_reserved2[4];

	uint64_t		mtcap_sensor_map;
};

struct mcx_reg_mtmp {
	uint8_t			_reserved1[2];
	uint16_t		mtmp_sensor_index;

	uint8_t			_reserved2[2];
	uint16_t		mtmp_temperature;

	uint16_t		mtmp_mte_mtr;
#define MCX_REG_MTMP_MTE		(1 << 15)
#define MCX_REG_MTMP_MTR		(1 << 14)
	uint16_t		mtmp_max_temperature;

	uint16_t		mtmp_tee;
#define MCX_REG_MTMP_TEE_NOPE		(0 << 14)
#define MCX_REG_MTMP_TEE_GENERATE	(1 << 14)
#define MCX_REG_MTMP_TEE_GENERATE_ONE	(2 << 14)
	uint16_t		mtmp_temperature_threshold_hi;

	uint8_t			_reserved3[2];
	uint16_t		mtmp_temperature_threshold_lo;

	uint8_t			_reserved4[4];

	uint8_t			mtmp_sensor_name[8];
};
CTASSERT(sizeof(struct mcx_reg_mtmp) == 0x20);
CTASSERT(offsetof(struct mcx_reg_mtmp, mtmp_sensor_name) == 0x18);

#define MCX_MCIA_EEPROM_BYTES	32
struct mcx_reg_mcia {
	uint8_t			rm_l;
	uint8_t			rm_module;
	uint8_t			rm_reserved0;
	uint8_t			rm_status;
	uint8_t			rm_i2c_addr;
	uint8_t			rm_page_num;
	uint16_t		rm_dev_addr;
	uint16_t		rm_reserved1;
	uint16_t		rm_size;
	uint32_t		rm_reserved2;
	uint8_t			rm_data[48];
} __packed __aligned(4);

struct mcx_cmd_query_issi_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_query_issi_il_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[2];
	uint16_t		cmd_current_issi;
	uint8_t			cmd_reserved2[4];
} __packed __aligned(4);

CTASSERT(sizeof(struct mcx_cmd_query_issi_il_out) == MCX_CMDQ_INLINE_DATASIZE);

struct mcx_cmd_query_issi_mb_out {
	uint8_t			cmd_reserved2[16];
	uint8_t			cmd_supported_issi[80]; /* very big endian */
} __packed __aligned(4);

CTASSERT(sizeof(struct mcx_cmd_query_issi_mb_out) <= MCX_CMDQ_MAILBOX_DATASIZE);

struct mcx_cmd_set_issi_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[2];
	uint16_t		cmd_current_issi;
	uint8_t			cmd_reserved2[4];
} __packed __aligned(4);

CTASSERT(sizeof(struct mcx_cmd_set_issi_in) <= MCX_CMDQ_INLINE_DATASIZE);

struct mcx_cmd_set_issi_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

CTASSERT(sizeof(struct mcx_cmd_set_issi_out) <= MCX_CMDQ_INLINE_DATASIZE);

struct mcx_cmd_query_pages_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
#define MCX_CMD_QUERY_PAGES_BOOT	0x01
#define MCX_CMD_QUERY_PAGES_INIT	0x02
#define MCX_CMD_QUERY_PAGES_REGULAR	0x03
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_query_pages_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[2];
	uint16_t		cmd_func_id;
	int32_t			cmd_num_pages;
} __packed __aligned(4);

struct mcx_cmd_manage_pages_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
#define MCX_CMD_MANAGE_PAGES_ALLOC_FAIL \
					0x00
#define MCX_CMD_MANAGE_PAGES_ALLOC_SUCCESS \
					0x01
#define MCX_CMD_MANAGE_PAGES_HCA_RETURN_PAGES \
					0x02
	uint8_t			cmd_reserved1[2];
	uint16_t		cmd_func_id;
	uint32_t		cmd_input_num_entries;
} __packed __aligned(4);

CTASSERT(sizeof(struct mcx_cmd_manage_pages_in) == MCX_CMDQ_INLINE_DATASIZE);

struct mcx_cmd_manage_pages_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint32_t		cmd_output_num_entries;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

CTASSERT(sizeof(struct mcx_cmd_manage_pages_out) == MCX_CMDQ_INLINE_DATASIZE);

struct mcx_cmd_query_hca_cap_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
#define MCX_CMD_QUERY_HCA_CAP_MAX	(0x0 << 0)
#define MCX_CMD_QUERY_HCA_CAP_CURRENT	(0x1 << 0)
#define MCX_CMD_QUERY_HCA_CAP_DEVICE	(0x0 << 1)
#define MCX_CMD_QUERY_HCA_CAP_OFFLOAD	(0x1 << 1)
#define MCX_CMD_QUERY_HCA_CAP_FLOW	(0x7 << 1)
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_query_hca_cap_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

#define MCX_HCA_CAP_LEN			0x1000
#define MCX_HCA_CAP_NMAILBOXES		\
	(MCX_HCA_CAP_LEN / MCX_CMDQ_MAILBOX_DATASIZE)

#if __GNUC_PREREQ__(4, 3)
#define __counter__		__COUNTER__
#else
#define __counter__		__LINE__
#endif

#define __token(_tok, _num)	_tok##_num
#define _token(_tok, _num)	__token(_tok, _num)
#define __reserved__		_token(__reserved, __counter__)

struct mcx_cap_device {
	uint8_t			reserved0[16];

	uint8_t			log_max_srq_sz;
	uint8_t			log_max_qp_sz;
	uint8_t			__reserved__[1];
	uint8_t			log_max_qp; /* 5 bits */
#define MCX_CAP_DEVICE_LOG_MAX_QP	0x1f

	uint8_t			__reserved__[1];
	uint8_t			log_max_srq; /* 5 bits */
#define MCX_CAP_DEVICE_LOG_MAX_SRQ	0x1f
	uint8_t			__reserved__[2];

	uint8_t			__reserved__[1];
	uint8_t			log_max_cq_sz;
	uint8_t			__reserved__[1];
	uint8_t			log_max_cq; /* 5 bits */
#define MCX_CAP_DEVICE_LOG_MAX_CQ	0x1f

	uint8_t			log_max_eq_sz;
	uint8_t			log_max_mkey; /* 6 bits */
#define MCX_CAP_DEVICE_LOG_MAX_MKEY	0x3f
	uint8_t			__reserved__[1];
	uint8_t			log_max_eq; /* 4 bits */
#define MCX_CAP_DEVICE_LOG_MAX_EQ	0x0f

	uint8_t			max_indirection;
	uint8_t			log_max_mrw_sz; /* 7 bits */
#define MCX_CAP_DEVICE_LOG_MAX_MRW_SZ	0x7f
	uint8_t			teardown_log_max_msf_list_size;
#define MCX_CAP_DEVICE_FORCE_TEARDOWN	0x80
#define MCX_CAP_DEVICE_LOG_MAX_MSF_LIST_SIZE \
					0x3f
	uint8_t			log_max_klm_list_size; /* 6 bits */
#define MCX_CAP_DEVICE_LOG_MAX_KLM_LIST_SIZE \
					0x3f

	uint8_t			__reserved__[1];
	uint8_t			log_max_ra_req_dc; /* 6 bits */
#define MCX_CAP_DEVICE_LOG_MAX_REQ_DC	0x3f
	uint8_t			__reserved__[1];
	uint8_t			log_max_ra_res_dc; /* 6 bits */
#define MCX_CAP_DEVICE_LOG_MAX_RA_RES_DC \
					0x3f

	uint8_t			__reserved__[1];
	uint8_t			log_max_ra_req_qp; /* 6 bits */
#define MCX_CAP_DEVICE_LOG_MAX_RA_REQ_QP \
					0x3f
	uint8_t			__reserved__[1];
	uint8_t			log_max_ra_res_qp; /* 6 bits */
#define MCX_CAP_DEVICE_LOG_MAX_RA_RES_QP \
					0x3f

	uint8_t			flags1;
#define MCX_CAP_DEVICE_END_PAD		0x80
#define MCX_CAP_DEVICE_CC_QUERY_ALLOWED	0x40
#define MCX_CAP_DEVICE_CC_MODIFY_ALLOWED \
					0x20
#define MCX_CAP_DEVICE_START_PAD	0x10
#define MCX_CAP_DEVICE_128BYTE_CACHELINE \
					0x08
	uint8_t			__reserved__[1];
	uint16_t		gid_table_size;

	uint16_t		flags2;
#define MCX_CAP_DEVICE_OUT_OF_SEQ_CNT	0x8000
#define MCX_CAP_DEVICE_VPORT_COUNTERS	0x4000
#define MCX_CAP_DEVICE_RETRANSMISSION_Q_COUNTERS \
					0x2000
#define MCX_CAP_DEVICE_DEBUG		0x1000
#define MCX_CAP_DEVICE_MODIFY_RQ_COUNTERS_SET_ID \
					0x8000
#define MCX_CAP_DEVICE_RQ_DELAY_DROP	0x4000
#define MCX_CAP_DEVICe_MAX_QP_CNT_MASK	0x03ff
	uint16_t		pkey_table_size;

	uint8_t			flags3;
#define MCX_CAP_DEVICE_VPORT_GROUP_MANAGER \
					0x80
#define MCX_CAP_DEVICE_VHCA_GROUP_MANAGER \
					0x40
#define MCX_CAP_DEVICE_IB_VIRTUAL	0x20
#define MCX_CAP_DEVICE_ETH_VIRTUAL	0x10
#define MCX_CAP_DEVICE_ETS		0x04
#define MCX_CAP_DEVICE_NIC_FLOW_TABLE	0x02
#define MCX_CAP_DEVICE_ESWITCH_FLOW_TABLE \
					0x01
	uint8_t			local_ca_ack_delay; /* 5 bits */
#define MCX_CAP_DEVICE_LOCAL_CA_ACK_DELAY \
					0x1f
#define MCX_CAP_DEVICE_MCAM_REG		0x40
	uint8_t			port_type;
#define MCX_CAP_DEVICE_PORT_MODULE_EVENT \
					0x80
#define MCX_CAP_DEVICE_PORT_TYPE	0x03
#define MCX_CAP_DEVICE_PORT_TYPE_ETH	0x01
	uint8_t			num_ports;

	uint8_t			snapshot_log_max_msg;
#define MCX_CAP_DEVICE_SNAPSHOT		0x80
#define MCX_CAP_DEVICE_LOG_MAX_MSG	0x1f
	uint8_t			max_tc; /* 4 bits */
#define MCX_CAP_DEVICE_MAX_TC		0x0f
	uint8_t			flags4;
#define MCX_CAP_DEVICE_TEMP_WARN_EVENT	0x80
#define MCX_CAP_DEVICE_DCBX		0x40
#define MCX_CAP_DEVICE_ROL_S		0x02
#define MCX_CAP_DEVICE_ROL_G		0x01
	uint8_t			wol;
#define MCX_CAP_DEVICE_WOL_S		0x40
#define MCX_CAP_DEVICE_WOL_G		0x20
#define MCX_CAP_DEVICE_WOL_A		0x10
#define MCX_CAP_DEVICE_WOL_B		0x08
#define MCX_CAP_DEVICE_WOL_M		0x04
#define MCX_CAP_DEVICE_WOL_U		0x02
#define MCX_CAP_DEVICE_WOL_P		0x01

	uint16_t		stat_rate_support;
	uint8_t			__reserved__[1];
	uint8_t			cqe_version; /* 4 bits */
#define MCX_CAP_DEVICE_CQE_VERSION	0x0f

	uint32_t		flags5;
#define MCX_CAP_DEVICE_COMPACT_ADDRESS_VECTOR \
					0x80000000
#define MCX_CAP_DEVICE_STRIDING_RQ	0x40000000
#define MCX_CAP_DEVICE_IPOIP_ENHANCED_OFFLOADS \
					0x10000000
#define MCX_CAP_DEVICE_IPOIP_IPOIP_OFFLOADS \
					0x08000000
#define MCX_CAP_DEVICE_DC_CONNECT_CP	0x00040000
#define MCX_CAP_DEVICE_DC_CNAK_DRACE	0x00020000
#define MCX_CAP_DEVICE_DRAIN_SIGERR	0x00010000
#define MCX_CAP_DEVICE_CMDIF_CHECKSUM	0x0000c000
#define MCX_CAP_DEVICE_SIGERR_QCE	0x00002000
#define MCX_CAP_DEVICE_WQ_SIGNATURE	0x00000800
#define MCX_CAP_DEVICE_SCTR_DATA_CQE	0x00000400
#define MCX_CAP_DEVICE_SHO		0x00000100
#define MCX_CAP_DEVICE_TPH		0x00000080
#define MCX_CAP_DEVICE_RF		0x00000040
#define MCX_CAP_DEVICE_DCT		0x00000020
#define MCX_CAP_DEVICE_QOS		0x00000010
#define MCX_CAP_DEVICe_ETH_NET_OFFLOADS	0x00000008
#define MCX_CAP_DEVICE_ROCE		0x00000004
#define MCX_CAP_DEVICE_ATOMIC		0x00000002

	uint32_t		flags6;
#define MCX_CAP_DEVICE_CQ_OI		0x80000000
#define MCX_CAP_DEVICE_CQ_RESIZE	0x40000000
#define MCX_CAP_DEVICE_CQ_MODERATION	0x20000000
#define MCX_CAP_DEVICE_CQ_PERIOD_MODE_MODIFY \
					0x10000000
#define MCX_CAP_DEVICE_CQ_INVALIDATE	0x08000000
#define MCX_CAP_DEVICE_RESERVED_AT_255	0x04000000
#define MCX_CAP_DEVICE_CQ_EQ_REMAP	0x02000000
#define MCX_CAP_DEVICE_PG		0x01000000
#define MCX_CAP_DEVICE_BLOCK_LB_MC	0x00800000
#define MCX_CAP_DEVICE_EXPONENTIAL_BACKOFF \
					0x00400000
#define MCX_CAP_DEVICE_SCQE_BREAK_MODERATION \
					0x00200000
#define MCX_CAP_DEVICE_CQ_PERIOD_START_FROM_CQE \
					0x00100000
#define MCX_CAP_DEVICE_CD		0x00080000
#define MCX_CAP_DEVICE_ATM		0x00040000
#define MCX_CAP_DEVICE_APM		0x00020000
#define MCX_CAP_DEVICE_IMAICL		0x00010000
#define MCX_CAP_DEVICE_QKV		0x00000200
#define MCX_CAP_DEVICE_PKV		0x00000100
#define MCX_CAP_DEVICE_SET_DETH_SQPN	0x00000080
#define MCX_CAP_DEVICE_XRC		0x00000008
#define MCX_CAP_DEVICE_UD		0x00000004
#define MCX_CAP_DEVICE_UC		0x00000002
#define MCX_CAP_DEVICE_RC		0x00000001

	uint8_t			uar_flags;
#define MCX_CAP_DEVICE_UAR_4K		0x80
	uint8_t			uar_sz;	/* 6 bits */
#define MCX_CAP_DEVICE_UAR_SZ		0x3f
	uint8_t			__reserved__[1];
	uint8_t			log_pg_sz;

	uint8_t			flags7;
#define MCX_CAP_DEVICE_BF		0x80
#define MCX_CAP_DEVICE_DRIVER_VERSION	0x40
#define MCX_CAP_DEVICE_PAD_TX_ETH_PACKET \
					0x20
	uint8_t			log_bf_reg_size; /* 5 bits */
#define MCX_CAP_DEVICE_LOG_BF_REG_SIZE	0x1f
	uint8_t			__reserved__[2];

	uint16_t		num_of_diagnostic_counters;
	uint16_t		max_wqe_sz_sq;

	uint8_t			__reserved__[2];
	uint16_t		max_wqe_sz_rq;

	uint8_t			__reserved__[2];
	uint16_t		max_wqe_sz_sq_dc;

	uint32_t		max_qp_mcg; /* 25 bits */
#define MCX_CAP_DEVICE_MAX_QP_MCG	0x1ffffff

	uint8_t			__reserved__[3];
	uint8_t			log_max_mcq;

	uint8_t			log_max_transport_domain; /* 5 bits */
#define MCX_CAP_DEVICE_LOG_MAX_TRANSORT_DOMAIN \
					0x1f
	uint8_t			log_max_pd; /* 5 bits */
#define MCX_CAP_DEVICE_LOG_MAX_PD	0x1f
	uint8_t			__reserved__[1];
	uint8_t			log_max_xrcd; /* 5 bits */
#define MCX_CAP_DEVICE_LOG_MAX_XRCD	0x1f

	uint8_t			__reserved__[2];
	uint16_t		max_flow_counter;

	uint8_t			log_max_rq; /* 5 bits */
#define MCX_CAP_DEVICE_LOG_MAX_RQ	0x1f
	uint8_t			log_max_sq; /* 5 bits */
#define MCX_CAP_DEVICE_LOG_MAX_SQ	0x1f
	uint8_t			log_max_tir; /* 5 bits */
#define MCX_CAP_DEVICE_LOG_MAX_TIR	0x1f
	uint8_t			log_max_tis; /* 5 bits */
#define MCX_CAP_DEVICE_LOG_MAX_TIS	0x1f

	uint8_t 		flags8;
#define MCX_CAP_DEVICE_BASIC_CYCLIC_RCV_WQE \
					0x80
#define MCX_CAP_DEVICE_LOG_MAX_RMP	0x1f
	uint8_t			log_max_rqt; /* 5 bits */
#define MCX_CAP_DEVICE_LOG_MAX_RQT	0x1f
	uint8_t			log_max_rqt_size; /* 5 bits */
#define MCX_CAP_DEVICE_LOG_MAX_RQT_SIZE	0x1f
	uint8_t			log_max_tis_per_sq; /* 5 bits */
#define MCX_CAP_DEVICE_LOG_MAX_TIS_PER_SQ \
					0x1f

	uint8_t			flags9;
#define MXC_CAP_DEVICE_EXT_STRIDE_NUM_RANGES \
					0x80
#define MXC_CAP_DEVICE_LOG_MAX_STRIDE_SZ_RQ \
					0x1f
	uint8_t			log_min_stride_sz_rq; /* 5 bits */
#define MXC_CAP_DEVICE_LOG_MIN_STRIDE_SZ_RQ \
					0x1f
	uint8_t			log_max_stride_sz_sq; /* 5 bits */
#define MXC_CAP_DEVICE_LOG_MAX_STRIDE_SZ_SQ \
					0x1f
	uint8_t			log_min_stride_sz_sq; /* 5 bits */
#define MXC_CAP_DEVICE_LOG_MIN_STRIDE_SZ_SQ \
					0x1f

	uint8_t			log_max_hairpin_queues;
#define MXC_CAP_DEVICE_HAIRPIN		0x80
#define MXC_CAP_DEVICE_LOG_MAX_HAIRPIN_QUEUES \
					0x1f
	uint8_t			log_min_hairpin_queues;
#define MXC_CAP_DEVICE_LOG_MIN_HAIRPIN_QUEUES \
					0x1f
	uint8_t			log_max_hairpin_num_packets;
#define MXC_CAP_DEVICE_LOG_MAX_HAIRPIN_NUM_PACKETS \
					0x1f
	uint8_t			log_max_mq_sz;
#define MXC_CAP_DEVICE_LOG_MAX_WQ_SZ \
					0x1f

	uint8_t			log_min_hairpin_wq_data_sz;
#define MXC_CAP_DEVICE_NIC_VPORT_CHANGE_EVENT \
					0x80
#define MXC_CAP_DEVICE_DISABLE_LOCAL_LB_UC \
					0x40
#define MXC_CAP_DEVICE_DISABLE_LOCAL_LB_MC \
					0x20
#define MCX_CAP_DEVICE_LOG_MIN_HAIRPIN_WQ_DATA_SZ \
					0x1f
	uint8_t			log_max_vlan_list;
#define MXC_CAP_DEVICE_SYSTEM_IMAGE_GUID_MODIFIABLE \
					0x80
#define MXC_CAP_DEVICE_LOG_MAX_VLAN_LIST \
					0x1f
	uint8_t			log_max_current_mc_list;
#define MXC_CAP_DEVICE_LOG_MAX_CURRENT_MC_LIST \
					0x1f
	uint8_t			log_max_current_uc_list;
#define MXC_CAP_DEVICE_LOG_MAX_CURRENT_UC_LIST \
					0x1f

	uint8_t			__reserved__[4];

	uint32_t		create_qp_start_hint; /* 24 bits */

	uint8_t			log_max_uctx; /* 5 bits */
#define MXC_CAP_DEVICE_LOG_MAX_UCTX	0x1f
	uint8_t			log_max_umem; /* 5 bits */
#define MXC_CAP_DEVICE_LOG_MAX_UMEM	0x1f
	uint16_t		max_num_eqs;

	uint8_t			log_max_l2_table; /* 5 bits */
#define MXC_CAP_DEVICE_LOG_MAX_L2_TABLE	0x1f
	uint8_t			__reserved__[1];
	uint16_t		log_uar_page_sz;

	uint8_t			__reserved__[8];

	uint32_t		device_frequency_mhz;
	uint32_t		device_frequency_khz;
} __packed __aligned(8);

CTASSERT(offsetof(struct mcx_cap_device, max_indirection) == 0x20);
CTASSERT(offsetof(struct mcx_cap_device, flags1) == 0x2c);
CTASSERT(offsetof(struct mcx_cap_device, flags2) == 0x30);
CTASSERT(offsetof(struct mcx_cap_device, snapshot_log_max_msg) == 0x38);
CTASSERT(offsetof(struct mcx_cap_device, flags5) == 0x40);
CTASSERT(offsetof(struct mcx_cap_device, flags7) == 0x4c);
CTASSERT(offsetof(struct mcx_cap_device, device_frequency_mhz) == 0x98);
CTASSERT(offsetof(struct mcx_cap_device, device_frequency_khz) == 0x9c);
CTASSERT(sizeof(struct mcx_cap_device) <= MCX_CMDQ_MAILBOX_DATASIZE);

struct mcx_cmd_set_driver_version_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_set_driver_version_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_set_driver_version {
	uint8_t			cmd_driver_version[64];
} __packed __aligned(8);

struct mcx_cmd_modify_nic_vport_context_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[4];
	uint32_t		cmd_field_select;
#define MCX_CMD_MODIFY_NIC_VPORT_CONTEXT_FIELD_ADDR	0x04
#define MCX_CMD_MODIFY_NIC_VPORT_CONTEXT_FIELD_PROMISC	0x10
#define MCX_CMD_MODIFY_NIC_VPORT_CONTEXT_FIELD_MTU	0x40
} __packed __aligned(4);

struct mcx_cmd_modify_nic_vport_context_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_query_nic_vport_context_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[4];
	uint8_t			cmd_allowed_list_type;
	uint8_t			cmd_reserved2[3];
} __packed __aligned(4);

struct mcx_cmd_query_nic_vport_context_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_nic_vport_ctx {
	uint32_t		vp_min_wqe_inline_mode;
	uint8_t			vp_reserved0[32];
	uint32_t		vp_mtu;
	uint8_t			vp_reserved1[200];
	uint16_t		vp_flags;
#define MCX_NIC_VPORT_CTX_LIST_UC_MAC			(0)
#define MCX_NIC_VPORT_CTX_LIST_MC_MAC			(1 << 24)
#define MCX_NIC_VPORT_CTX_LIST_VLAN			(2 << 24)
#define MCX_NIC_VPORT_CTX_PROMISC_ALL			(1 << 13)
#define MCX_NIC_VPORT_CTX_PROMISC_MCAST			(1 << 14)
#define MCX_NIC_VPORT_CTX_PROMISC_UCAST			(1 << 15)
	uint16_t		vp_allowed_list_size;
	uint64_t		vp_perm_addr;
	uint8_t			vp_reserved2[4];
	/* allowed list follows */
} __packed __aligned(4);

struct mcx_counter {
	uint64_t		packets;
	uint64_t		octets;
} __packed __aligned(4);

struct mcx_nic_vport_counters {
	struct mcx_counter	rx_err;
	struct mcx_counter	tx_err;
	uint8_t			reserved0[64]; /* 0x30 */
	struct mcx_counter	rx_bcast;
	struct mcx_counter	tx_bcast;
	struct mcx_counter	rx_ucast;
	struct mcx_counter	tx_ucast;
	struct mcx_counter	rx_mcast;
	struct mcx_counter	tx_mcast;
	uint8_t			reserved1[0x210 - 0xd0];
} __packed __aligned(4);

struct mcx_cmd_query_vport_counters_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_query_vport_counters_mb_in {
	uint8_t			cmd_reserved0[8];
	uint8_t			cmd_clear;
	uint8_t			cmd_reserved1[7];
} __packed __aligned(4);

struct mcx_cmd_query_vport_counters_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_query_flow_counter_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_query_flow_counter_mb_in {
	uint8_t			cmd_reserved0[8];
	uint8_t			cmd_clear;
	uint8_t			cmd_reserved1[5];
	uint16_t		cmd_flow_counter_id;
} __packed __aligned(4);

struct mcx_cmd_query_flow_counter_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_alloc_uar_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_alloc_uar_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint32_t		cmd_uar;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_query_special_ctx_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_query_special_ctx_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[4];
	uint32_t		cmd_resd_lkey;
} __packed __aligned(4);

struct mcx_eq_ctx {
	uint32_t		eq_status;
#define MCX_EQ_CTX_STATE_SHIFT		8
#define MCX_EQ_CTX_STATE_MASK		(0xf << MCX_EQ_CTX_STATE_SHIFT)
#define MCX_EQ_CTX_STATE_ARMED		0x9
#define MCX_EQ_CTX_STATE_FIRED		0xa
#define MCX_EQ_CTX_OI_SHIFT		17
#define MCX_EQ_CTX_OI			(1 << MCX_EQ_CTX_OI_SHIFT)
#define MCX_EQ_CTX_EC_SHIFT		18
#define MCX_EQ_CTX_EC			(1 << MCX_EQ_CTX_EC_SHIFT)
#define MCX_EQ_CTX_STATUS_SHIFT		28
#define MCX_EQ_CTX_STATUS_MASK		(0xf << MCX_EQ_CTX_STATUS_SHIFT)
#define MCX_EQ_CTX_STATUS_OK		0x0
#define MCX_EQ_CTX_STATUS_EQ_WRITE_FAILURE 0xa
	uint32_t		eq_reserved1;
	uint32_t		eq_page_offset;
#define MCX_EQ_CTX_PAGE_OFFSET_SHIFT	5
	uint32_t		eq_uar_size;
#define MCX_EQ_CTX_UAR_PAGE_MASK	0xffffff
#define MCX_EQ_CTX_LOG_EQ_SIZE_SHIFT	24
	uint32_t		eq_reserved2;
	uint8_t			eq_reserved3[3];
	uint8_t			eq_intr;
	uint32_t		eq_log_page_size;
#define MCX_EQ_CTX_LOG_PAGE_SIZE_SHIFT	24
	uint32_t		eq_reserved4[3];
	uint32_t		eq_consumer_counter;
	uint32_t		eq_producer_counter;
#define MCX_EQ_CTX_COUNTER_MASK		0xffffff
	uint32_t		eq_reserved5[4];
} __packed __aligned(4);

CTASSERT(sizeof(struct mcx_eq_ctx) == 64);

struct mcx_cmd_create_eq_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_create_eq_mb_in {
	struct mcx_eq_ctx	cmd_eq_ctx;
	uint8_t			cmd_reserved0[8];
	uint64_t		cmd_event_bitmask;
#define MCX_EVENT_TYPE_COMPLETION	0x00
#define MCX_EVENT_TYPE_CQ_ERROR		0x04
#define MCX_EVENT_TYPE_INTERNAL_ERROR	0x08
#define MCX_EVENT_TYPE_PORT_CHANGE	0x09
#define MCX_EVENT_TYPE_CMD_COMPLETION	0x0a
#define MCX_EVENT_TYPE_PAGE_REQUEST	0x0b
#define MCX_EVENT_TYPE_LAST_WQE		0x13
	uint8_t			cmd_reserved1[176];
} __packed __aligned(4);

struct mcx_cmd_create_eq_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint32_t		cmd_eqn;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_query_eq_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint32_t		cmd_eqn;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_query_eq_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_eq_entry {
	uint8_t			eq_reserved1;
	uint8_t			eq_event_type;
	uint8_t			eq_reserved2;
	uint8_t			eq_event_sub_type;

	uint8_t			eq_reserved3[28];
	uint32_t		eq_event_data[7];
	uint8_t			eq_reserved4[2];
	uint8_t			eq_signature;
	uint8_t			eq_owner;
#define MCX_EQ_ENTRY_OWNER_INIT			1
} __packed __aligned(4);

CTASSERT(sizeof(struct mcx_eq_entry) == 64);

struct mcx_cmd_alloc_pd_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_alloc_pd_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint32_t		cmd_pd;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_alloc_td_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_alloc_td_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint32_t		cmd_tdomain;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_create_tir_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_create_tir_mb_in {
	uint8_t			cmd_reserved0[20];
	uint32_t		cmd_disp_type;
#define MCX_TIR_CTX_DISP_TYPE_DIRECT	0
#define MCX_TIR_CTX_DISP_TYPE_INDIRECT	1
#define MCX_TIR_CTX_DISP_TYPE_SHIFT	28
	uint8_t			cmd_reserved1[8];
	uint32_t		cmd_lro;
	uint8_t			cmd_reserved2[8];
	uint32_t		cmd_inline_rqn;
	uint32_t		cmd_indir_table;
	uint32_t		cmd_tdomain;
#define MCX_TIR_CTX_HASH_TOEPLITZ	2
#define MCX_TIR_CTX_HASH_SHIFT		28
	uint8_t			cmd_rx_hash_key[40];
	uint32_t		cmd_rx_hash_sel_outer;
#define MCX_TIR_CTX_HASH_SEL_SRC_IP	(1 << 0)
#define MCX_TIR_CTX_HASH_SEL_DST_IP	(1 << 1)
#define MCX_TIR_CTX_HASH_SEL_SPORT	(1 << 2)
#define MCX_TIR_CTX_HASH_SEL_DPORT	(1 << 3)
#define MCX_TIR_CTX_HASH_SEL_IPV4	(0 << 31)
#define MCX_TIR_CTX_HASH_SEL_IPV6	(1U << 31)
#define MCX_TIR_CTX_HASH_SEL_TCP	(0 << 30)
#define MCX_TIR_CTX_HASH_SEL_UDP	(1 << 30)
	uint32_t		cmd_rx_hash_sel_inner;
	uint8_t			cmd_reserved3[152];
} __packed __aligned(4);

struct mcx_cmd_create_tir_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint32_t		cmd_tirn;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_destroy_tir_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint32_t		cmd_tirn;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_destroy_tir_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_create_tis_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_create_tis_mb_in {
	uint8_t			cmd_reserved[16];
	uint32_t		cmd_prio;
	uint8_t			cmd_reserved1[32];
	uint32_t		cmd_tdomain;
	uint8_t			cmd_reserved2[120];
} __packed __aligned(4);

struct mcx_cmd_create_tis_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint32_t		cmd_tisn;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_destroy_tis_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint32_t		cmd_tisn;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_destroy_tis_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_create_rqt_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_rqt_ctx {
	uint8_t			cmd_reserved0[20];
	uint16_t		cmd_reserved1;
	uint16_t		cmd_rqt_max_size;
	uint16_t		cmd_reserved2;
	uint16_t		cmd_rqt_actual_size;
	uint8_t			cmd_reserved3[212];
} __packed __aligned(4);

struct mcx_cmd_create_rqt_mb_in {
	uint8_t			cmd_reserved0[16];
	struct mcx_rqt_ctx	cmd_rqt;
} __packed __aligned(4);

struct mcx_cmd_create_rqt_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint32_t		cmd_rqtn;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_destroy_rqt_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint32_t		cmd_rqtn;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_destroy_rqt_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cq_ctx {
	uint32_t		cq_status;
#define MCX_CQ_CTX_STATUS_SHIFT		28
#define MCX_CQ_CTX_STATUS_MASK		(0xf << MCX_CQ_CTX_STATUS_SHIFT)
#define MCX_CQ_CTX_STATUS_OK		0x0
#define MCX_CQ_CTX_STATUS_OVERFLOW	0x9
#define MCX_CQ_CTX_STATUS_WRITE_FAIL	0xa
#define MCX_CQ_CTX_STATE_SHIFT		8
#define MCX_CQ_CTX_STATE_MASK		(0xf << MCX_CQ_CTX_STATE_SHIFT)
#define MCX_CQ_CTX_STATE_SOLICITED	0x6
#define MCX_CQ_CTX_STATE_ARMED		0x9
#define MCX_CQ_CTX_STATE_FIRED		0xa
	uint32_t		cq_reserved1;
	uint32_t		cq_page_offset;
	uint32_t		cq_uar_size;
#define MCX_CQ_CTX_UAR_PAGE_MASK	0xffffff
#define MCX_CQ_CTX_LOG_CQ_SIZE_SHIFT	24
	uint32_t		cq_period_max_count;
#define MCX_CQ_CTX_PERIOD_SHIFT		16
	uint32_t		cq_eqn;
	uint32_t		cq_log_page_size;
#define MCX_CQ_CTX_LOG_PAGE_SIZE_SHIFT	24
	uint32_t		cq_reserved2;
	uint32_t		cq_last_notified;
	uint32_t		cq_last_solicit;
	uint32_t		cq_consumer_counter;
	uint32_t		cq_producer_counter;
	uint8_t			cq_reserved3[8];
	uint64_t		cq_doorbell;
} __packed __aligned(4);

CTASSERT(sizeof(struct mcx_cq_ctx) == 64);

struct mcx_cmd_create_cq_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_create_cq_mb_in {
	struct mcx_cq_ctx	cmd_cq_ctx;
	uint8_t			cmd_reserved1[192];
} __packed __aligned(4);

struct mcx_cmd_create_cq_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint32_t		cmd_cqn;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_destroy_cq_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint32_t		cmd_cqn;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_destroy_cq_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_query_cq_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint32_t		cmd_cqn;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_query_cq_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cq_entry {
	uint32_t		__reserved__;
	uint32_t		cq_lro;
	uint32_t		cq_lro_ack_seq_num;
	uint32_t		cq_rx_hash;
	uint8_t			cq_rx_hash_type;
	uint8_t			cq_ml_path;
	uint16_t		__reserved__;
	uint32_t		cq_checksum;
	uint32_t		__reserved__;
	uint32_t		cq_flags;
#define MCX_CQ_ENTRY_FLAGS_L4_OK		(1 << 26)
#define MCX_CQ_ENTRY_FLAGS_L3_OK		(1 << 25)
#define MCX_CQ_ENTRY_FLAGS_L2_OK		(1 << 24)
#define MCX_CQ_ENTRY_FLAGS_CV			(1 << 16)
#define MCX_CQ_ENTRY_FLAGS_VLAN_MASK		(0xffff)

	uint32_t		cq_lro_srqn;
	uint32_t		__reserved__[2];
	uint32_t		cq_byte_cnt;
	uint64_t		cq_timestamp;
	uint8_t			cq_rx_drops;
	uint8_t			cq_flow_tag[3];
	uint16_t		cq_wqe_count;
	uint8_t			cq_signature;
	uint8_t			cq_opcode_owner;
#define MCX_CQ_ENTRY_FLAG_OWNER			(1 << 0)
#define MCX_CQ_ENTRY_FLAG_SE			(1 << 1)
#define MCX_CQ_ENTRY_FORMAT_SHIFT		2
#define MCX_CQ_ENTRY_OPCODE_SHIFT		4

#define MCX_CQ_ENTRY_FORMAT_NO_INLINE		0
#define MCX_CQ_ENTRY_FORMAT_INLINE_32		1
#define MCX_CQ_ENTRY_FORMAT_INLINE_64		2
#define MCX_CQ_ENTRY_FORMAT_COMPRESSED		3

#define MCX_CQ_ENTRY_OPCODE_REQ			0
#define MCX_CQ_ENTRY_OPCODE_SEND		2
#define MCX_CQ_ENTRY_OPCODE_REQ_ERR		13
#define MCX_CQ_ENTRY_OPCODE_SEND_ERR		14
#define MCX_CQ_ENTRY_OPCODE_INVALID		15

} __packed __aligned(4);

CTASSERT(sizeof(struct mcx_cq_entry) == 64);

struct mcx_cq_doorbell {
	uint32_t		 db_update_ci;
	uint32_t		 db_arm_ci;
#define MCX_CQ_DOORBELL_ARM_CMD_SN_SHIFT	28
#define MCX_CQ_DOORBELL_ARM_CMD			(1 << 24)
#define MCX_CQ_DOORBELL_ARM_CI_MASK		(0xffffff)
} __packed __aligned(8);

struct mcx_wq_ctx {
	uint8_t			 wq_type;
#define MCX_WQ_CTX_TYPE_CYCLIC			(1 << 4)
#define MCX_WQ_CTX_TYPE_SIGNATURE		(1 << 3)
	uint8_t			 wq_reserved0[5];
	uint16_t		 wq_lwm;
	uint32_t		 wq_pd;
	uint32_t		 wq_uar_page;
	uint64_t		 wq_doorbell;
	uint32_t		 wq_hw_counter;
	uint32_t		 wq_sw_counter;
	uint16_t		 wq_log_stride;
	uint8_t			 wq_log_page_sz;
	uint8_t			 wq_log_size;
	uint8_t			 wq_reserved1[156];
} __packed __aligned(4);

CTASSERT(sizeof(struct mcx_wq_ctx) == 0xC0);

struct mcx_sq_ctx {
	uint32_t		sq_flags;
#define MCX_SQ_CTX_RLKEY			(1U << 31)
#define MCX_SQ_CTX_FRE_SHIFT			(1 << 29)
#define MCX_SQ_CTX_FLUSH_IN_ERROR		(1 << 28)
#define MCX_SQ_CTX_MIN_WQE_INLINE_SHIFT		24
#define MCX_SQ_CTX_STATE_SHIFT			20
#define MCX_SQ_CTX_STATE_MASK			(0xf << 20)
#define MCX_SQ_CTX_STATE_RST			0
#define MCX_SQ_CTX_STATE_RDY			1
#define MCX_SQ_CTX_STATE_ERR			3
	uint32_t		sq_user_index;
	uint32_t		sq_cqn;
	uint32_t		sq_reserved1[5];
	uint32_t		sq_tis_lst_sz;
#define MCX_SQ_CTX_TIS_LST_SZ_SHIFT		16
	uint32_t		sq_reserved2[2];
	uint32_t		sq_tis_num;
	struct mcx_wq_ctx	sq_wq;
} __packed __aligned(4);

struct mcx_sq_entry_seg {
	uint32_t		sqs_byte_count;
	uint32_t		sqs_lkey;
	uint64_t		sqs_addr;
} __packed __aligned(4);

struct mcx_sq_entry {
	/* control segment */
	uint32_t		sqe_opcode_index;
#define MCX_SQE_WQE_INDEX_SHIFT			8
#define MCX_SQE_WQE_OPCODE_NOP			0x00
#define MCX_SQE_WQE_OPCODE_SEND			0x0a
	uint32_t		sqe_ds_sq_num;
#define MCX_SQE_SQ_NUM_SHIFT			8
	uint32_t		sqe_signature;
#define MCX_SQE_SIGNATURE_SHIFT			24
#define MCX_SQE_SOLICITED_EVENT			0x02
#define MCX_SQE_CE_CQE_ON_ERR			0x00
#define MCX_SQE_CE_CQE_FIRST_ERR		0x04
#define MCX_SQE_CE_CQE_ALWAYS			0x08
#define MCX_SQE_CE_CQE_SOLICIT			0x0C
#define MCX_SQE_FM_NO_FENCE			0x00
#define MCX_SQE_FM_SMALL_FENCE			0x40
	uint32_t		sqe_mkey;

	/* ethernet segment */
	uint32_t		sqe_reserved1;
	uint32_t		sqe_mss_csum;
#define MCX_SQE_L4_CSUM				(1U << 31)
#define MCX_SQE_L3_CSUM				(1 << 30)
	uint32_t		sqe_reserved2;
	uint16_t		sqe_inline_header_size;
	uint16_t		sqe_inline_headers[9];

	/* data segment */
	struct mcx_sq_entry_seg sqe_segs[1];
} __packed __aligned(64);

CTASSERT(sizeof(struct mcx_sq_entry) == 64);

struct mcx_cmd_create_sq_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_create_sq_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint32_t		cmd_sqn;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_modify_sq_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint32_t		cmd_sq_state;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_modify_sq_mb_in {
	uint32_t		cmd_modify_hi;
	uint32_t		cmd_modify_lo;
	uint8_t			cmd_reserved0[8];
	struct mcx_sq_ctx	cmd_sq_ctx;
} __packed __aligned(4);

struct mcx_cmd_modify_sq_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_destroy_sq_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint32_t		cmd_sqn;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_destroy_sq_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);


struct mcx_rq_ctx {
	uint32_t		rq_flags;
#define MCX_RQ_CTX_RLKEY			(1U << 31)
#define MCX_RQ_CTX_VLAN_STRIP_DIS		(1 << 28)
#define MCX_RQ_CTX_MEM_RQ_TYPE_SHIFT		24
#define MCX_RQ_CTX_STATE_SHIFT			20
#define MCX_RQ_CTX_STATE_MASK			(0xf << 20)
#define MCX_RQ_CTX_STATE_RST			0
#define MCX_RQ_CTX_STATE_RDY			1
#define MCX_RQ_CTX_STATE_ERR			3
#define MCX_RQ_CTX_FLUSH_IN_ERROR		(1 << 18)
	uint32_t		rq_user_index;
	uint32_t		rq_cqn;
	uint32_t		rq_reserved1;
	uint32_t		rq_rmpn;
	uint32_t		rq_reserved2[7];
	struct mcx_wq_ctx	rq_wq;
} __packed __aligned(4);

struct mcx_rq_entry {
	uint32_t		rqe_byte_count;
	uint32_t		rqe_lkey;
	uint64_t		rqe_addr;
} __packed __aligned(16);

struct mcx_cmd_create_rq_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_create_rq_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint32_t		cmd_rqn;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_modify_rq_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint32_t		cmd_rq_state;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_modify_rq_mb_in {
	uint32_t		cmd_modify_hi;
	uint32_t		cmd_modify_lo;
	uint8_t			cmd_reserved0[8];
	struct mcx_rq_ctx	cmd_rq_ctx;
} __packed __aligned(4);

struct mcx_cmd_modify_rq_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_destroy_rq_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint32_t		cmd_rqn;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_destroy_rq_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_create_flow_table_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_flow_table_ctx {
	uint8_t			ft_miss_action;
	uint8_t			ft_level;
	uint8_t			ft_reserved0;
	uint8_t			ft_log_size;
	uint32_t		ft_table_miss_id;
	uint8_t			ft_reserved1[28];
} __packed __aligned(4);

struct mcx_cmd_create_flow_table_mb_in {
	uint8_t			cmd_table_type;
	uint8_t			cmd_reserved0[7];
	struct mcx_flow_table_ctx cmd_ctx;
} __packed __aligned(4);

struct mcx_cmd_create_flow_table_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint32_t		cmd_table_id;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_destroy_flow_table_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_destroy_flow_table_mb_in {
	uint8_t			cmd_table_type;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_table_id;
	uint8_t			cmd_reserved1[40];
} __packed __aligned(4);

struct mcx_cmd_destroy_flow_table_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_set_flow_table_root_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_set_flow_table_root_mb_in {
	uint8_t			cmd_table_type;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_table_id;
	uint8_t			cmd_reserved1[56];
} __packed __aligned(4);

struct mcx_cmd_set_flow_table_root_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_flow_match {
	/* outer headers */
	uint8_t			mc_src_mac[6];
	uint16_t		mc_ethertype;
	uint8_t			mc_dest_mac[6];
	uint16_t		mc_first_vlan;
	uint8_t			mc_ip_proto;
	uint8_t			mc_ip_dscp_ecn;
	uint8_t			mc_vlan_flags;
#define MCX_FLOW_MATCH_IP_FRAG	(1 << 5)
	uint8_t			mc_tcp_flags;
	uint16_t		mc_tcp_sport;
	uint16_t		mc_tcp_dport;
	uint32_t		mc_reserved0;
	uint16_t		mc_udp_sport;
	uint16_t		mc_udp_dport;
	uint8_t			mc_src_ip[16];
	uint8_t			mc_dest_ip[16];

	/* misc parameters */
	uint8_t			mc_reserved1[8];
	uint16_t		mc_second_vlan;
	uint8_t			mc_reserved2[2];
	uint8_t			mc_second_vlan_flags;
	uint8_t			mc_reserved3[15];
	uint32_t		mc_outer_ipv6_flow_label;
	uint8_t			mc_reserved4[32];

	uint8_t			mc_reserved[384];
} __packed __aligned(4);

CTASSERT(sizeof(struct mcx_flow_match) == 512);

struct mcx_cmd_create_flow_group_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_create_flow_group_mb_in {
	uint8_t			cmd_table_type;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_table_id;
	uint8_t			cmd_reserved1[4];
	uint32_t		cmd_start_flow_index;
	uint8_t			cmd_reserved2[4];
	uint32_t		cmd_end_flow_index;
	uint8_t			cmd_reserved3[23];
	uint8_t			cmd_match_criteria_enable;
#define MCX_CREATE_FLOW_GROUP_CRIT_OUTER	(1 << 0)
#define MCX_CREATE_FLOW_GROUP_CRIT_MISC		(1 << 1)
#define MCX_CREATE_FLOW_GROUP_CRIT_INNER	(1 << 2)
	struct mcx_flow_match	cmd_match_criteria;
	uint8_t			cmd_reserved4[448];
} __packed __aligned(4);

struct mcx_cmd_create_flow_group_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint32_t		cmd_group_id;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_flow_ctx {
	uint8_t			fc_reserved0[4];
	uint32_t		fc_group_id;
	uint32_t		fc_flow_tag;
	uint32_t		fc_action;
#define MCX_FLOW_CONTEXT_ACTION_ALLOW		(1 << 0)
#define MCX_FLOW_CONTEXT_ACTION_DROP		(1 << 1)
#define MCX_FLOW_CONTEXT_ACTION_FORWARD		(1 << 2)
#define MCX_FLOW_CONTEXT_ACTION_COUNT		(1 << 3)
	uint32_t		fc_dest_list_size;
	uint32_t		fc_counter_list_size;
	uint8_t			fc_reserved1[40];
	struct mcx_flow_match	fc_match_value;
	uint8_t			fc_reserved2[192];
} __packed __aligned(4);

#define MCX_FLOW_CONTEXT_DEST_TYPE_TABLE	(1 << 24)
#define MCX_FLOW_CONTEXT_DEST_TYPE_TIR		(2 << 24)

struct mcx_cmd_destroy_flow_group_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_destroy_flow_group_mb_in {
	uint8_t			cmd_table_type;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_table_id;
	uint32_t		cmd_group_id;
	uint8_t			cmd_reserved1[36];
} __packed __aligned(4);

struct mcx_cmd_destroy_flow_group_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_set_flow_table_entry_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_set_flow_table_entry_mb_in {
	uint8_t			cmd_table_type;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_table_id;
	uint32_t		cmd_modify_enable_mask;
	uint8_t			cmd_reserved1[4];
	uint32_t		cmd_flow_index;
	uint8_t			cmd_reserved2[28];
	struct mcx_flow_ctx	cmd_flow_ctx;
} __packed __aligned(4);

struct mcx_cmd_set_flow_table_entry_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_query_flow_table_entry_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_query_flow_table_entry_mb_in {
	uint8_t			cmd_table_type;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_table_id;
	uint8_t			cmd_reserved1[8];
	uint32_t		cmd_flow_index;
	uint8_t			cmd_reserved2[28];
} __packed __aligned(4);

struct mcx_cmd_query_flow_table_entry_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_query_flow_table_entry_mb_out {
	uint8_t			cmd_reserved0[48];
	struct mcx_flow_ctx	cmd_flow_ctx;
} __packed __aligned(4);

struct mcx_cmd_delete_flow_table_entry_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_delete_flow_table_entry_mb_in {
	uint8_t			cmd_table_type;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_table_id;
	uint8_t			cmd_reserved1[8];
	uint32_t		cmd_flow_index;
	uint8_t			cmd_reserved2[28];
} __packed __aligned(4);

struct mcx_cmd_delete_flow_table_entry_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_query_flow_group_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_query_flow_group_mb_in {
	uint8_t			cmd_table_type;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_table_id;
	uint32_t		cmd_group_id;
	uint8_t			cmd_reserved1[36];
} __packed __aligned(4);

struct mcx_cmd_query_flow_group_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_query_flow_group_mb_out {
	uint8_t			cmd_reserved0[12];
	uint32_t		cmd_start_flow_index;
	uint8_t			cmd_reserved1[4];
	uint32_t		cmd_end_flow_index;
	uint8_t			cmd_reserved2[20];
	uint32_t		cmd_match_criteria_enable;
	uint8_t			cmd_match_criteria[512];
	uint8_t			cmd_reserved4[448];
} __packed __aligned(4);

struct mcx_cmd_query_flow_table_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_query_flow_table_mb_in {
	uint8_t			cmd_table_type;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_table_id;
	uint8_t			cmd_reserved1[40];
} __packed __aligned(4);

struct mcx_cmd_query_flow_table_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_query_flow_table_mb_out {
	uint8_t			cmd_reserved0[4];
	struct mcx_flow_table_ctx cmd_ctx;
} __packed __aligned(4);

struct mcx_cmd_alloc_flow_counter_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_query_rq_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint32_t		cmd_rqn;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_query_rq_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_query_rq_mb_out {
	uint8_t			cmd_reserved0[16];
	struct mcx_rq_ctx	cmd_ctx;
};

struct mcx_cmd_query_sq_in {
	uint16_t		cmd_opcode;
	uint8_t			cmd_reserved0[4];
	uint16_t		cmd_op_mod;
	uint32_t		cmd_sqn;
	uint8_t			cmd_reserved1[4];
} __packed __aligned(4);

struct mcx_cmd_query_sq_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[8];
} __packed __aligned(4);

struct mcx_cmd_query_sq_mb_out {
	uint8_t			cmd_reserved0[16];
	struct mcx_sq_ctx	cmd_ctx;
};

struct mcx_cmd_alloc_flow_counter_out {
	uint8_t			cmd_status;
	uint8_t			cmd_reserved0[3];
	uint32_t		cmd_syndrome;
	uint8_t			cmd_reserved1[2];
	uint16_t		cmd_flow_counter_id;
	uint8_t			cmd_reserved2[4];
} __packed __aligned(4);

struct mcx_wq_doorbell {
	uint32_t		 db_recv_counter;
	uint32_t		 db_send_counter;
} __packed __aligned(8);

struct mcx_dmamem {
	bus_dmamap_t		 mxm_map;
	bus_dma_segment_t	 mxm_seg;
	int			 mxm_nsegs;
	size_t			 mxm_size;
	caddr_t			 mxm_kva;
};
#define MCX_DMA_MAP(_mxm)	((_mxm)->mxm_map)
#define MCX_DMA_DVA(_mxm)	((_mxm)->mxm_map->dm_segs[0].ds_addr)
#define MCX_DMA_KVA(_mxm)	((void *)(_mxm)->mxm_kva)
#define MCX_DMA_OFF(_mxm, _off)	((void *)((_mxm)->mxm_kva + (_off)))
#define MCX_DMA_LEN(_mxm)	((_mxm)->mxm_size)

struct mcx_hwmem {
	bus_dmamap_t		 mhm_map;
	bus_dma_segment_t	*mhm_segs;
	unsigned int		 mhm_seg_count;
	unsigned int		 mhm_npages;
};

struct mcx_slot {
	bus_dmamap_t		 ms_map;
	struct mbuf		*ms_m;
};

struct mcx_eq {
	int			 eq_n;
	uint32_t		 eq_cons;
	struct mcx_dmamem	 eq_mem;
};

struct mcx_cq {
	int			 cq_n;
	struct mcx_dmamem	 cq_mem;
	bus_addr_t		 cq_doorbell;
	uint32_t		 cq_cons;
	uint32_t		 cq_count;
};

struct mcx_calibration {
	uint64_t		 c_timestamp;	/* previous mcx chip time */
	uint64_t		 c_uptime;	/* previous kernel nanouptime */
	uint64_t		 c_tbase;	/* mcx chip time */
	uint64_t		 c_ubase;	/* kernel nanouptime */
	uint64_t		 c_ratio;
};

#define MCX_CALIBRATE_FIRST    2
#define MCX_CALIBRATE_NORMAL   32

struct mcx_rx {
	struct mcx_softc	*rx_softc;
	struct ifiqueue		*rx_ifiq;

	int			 rx_rqn;
	struct mcx_dmamem	 rx_rq_mem;
	struct mcx_slot		*rx_slots;
	bus_addr_t		 rx_doorbell;

	uint32_t		 rx_prod;
	struct timeout		 rx_refill;
	struct if_rxring	 rx_rxr;
} __aligned(64);

struct mcx_tx {
	struct mcx_softc	*tx_softc;
	struct ifqueue		*tx_ifq;
#if NBPFILTER > 0
	caddr_t			*tx_bpfp;
#endif

	int			 tx_uar;
	int			 tx_sqn;
	struct mcx_dmamem	 tx_sq_mem;
	struct mcx_slot		*tx_slots;
	bus_addr_t		 tx_doorbell;
	int			 tx_bf_offset;

	uint32_t		 tx_cons;
	uint32_t		 tx_prod;
} __aligned(64);

struct mcx_queues {
	char			 q_name[16];
	void			*q_ihc;
	struct mcx_softc	*q_sc;
	int			 q_uar;
	int			 q_index;
	struct mcx_rx		 q_rx;
	struct mcx_tx		 q_tx;
	struct mcx_cq		 q_cq;
	struct mcx_eq		 q_eq;
#if NBPFILTER > 0
	caddr_t			 q_bpf;
#endif
#if NKSTAT > 0
	struct kstat		*q_kstat;
#endif
};

struct mcx_flow_group {
	int			 g_id;
	int			 g_table;
	int			 g_start;
	int			 g_size;
};

#define MCX_FLOW_GROUP_PROMISC	 0
#define MCX_FLOW_GROUP_ALLMULTI	 1
#define MCX_FLOW_GROUP_MAC	 2
#define MCX_FLOW_GROUP_RSS_L4	 3
#define MCX_FLOW_GROUP_RSS_L3	 4
#define MCX_FLOW_GROUP_RSS_NONE	 5
#define MCX_NUM_FLOW_GROUPS	 6

#define MCX_HASH_SEL_L3		MCX_TIR_CTX_HASH_SEL_SRC_IP | \
				MCX_TIR_CTX_HASH_SEL_DST_IP
#define MCX_HASH_SEL_L4		MCX_HASH_SEL_L3 | MCX_TIR_CTX_HASH_SEL_SPORT | \
				MCX_TIR_CTX_HASH_SEL_DPORT

#define MCX_RSS_HASH_SEL_V4_TCP MCX_HASH_SEL_L4 | MCX_TIR_CTX_HASH_SEL_TCP  |\
				MCX_TIR_CTX_HASH_SEL_IPV4
#define MCX_RSS_HASH_SEL_V6_TCP	MCX_HASH_SEL_L4 | MCX_TIR_CTX_HASH_SEL_TCP | \
				MCX_TIR_CTX_HASH_SEL_IPV6
#define MCX_RSS_HASH_SEL_V4_UDP	MCX_HASH_SEL_L4 | MCX_TIR_CTX_HASH_SEL_UDP | \
				MCX_TIR_CTX_HASH_SEL_IPV4
#define MCX_RSS_HASH_SEL_V6_UDP	MCX_HASH_SEL_L4 | MCX_TIR_CTX_HASH_SEL_UDP | \
				MCX_TIR_CTX_HASH_SEL_IPV6
#define MCX_RSS_HASH_SEL_V4	MCX_HASH_SEL_L3 | MCX_TIR_CTX_HASH_SEL_IPV4
#define MCX_RSS_HASH_SEL_V6	MCX_HASH_SEL_L3 | MCX_TIR_CTX_HASH_SEL_IPV6

/*
 * There are a few different pieces involved in configuring RSS.
 * A Receive Queue Table (RQT) is the indirection table that maps packets to
 * different rx queues based on a hash value.  We only create one, because
 * we want to scatter any traffic we can apply RSS to across all our rx
 * queues.  Anything else will only be delivered to the first rx queue,
 * which doesn't require an RQT.
 *
 * A Transport Interface Receive (TIR) delivers packets to either a single rx
 * queue or an RQT, and in the latter case, specifies the set of fields
 * hashed, the hash function, and the hash key.  We need one of these for each
 * type of RSS traffic - v4 TCP, v6 TCP, v4 UDP, v6 UDP, other v4, other v6,
 * and one for non-RSS traffic.
 *
 * Flow tables hold flow table entries in sequence.  The first entry that
 * matches a packet is applied, sending the packet to either another flow
 * table or a TIR.  We use one flow table to select packets based on
 * destination MAC address, and a second to apply RSS.  The entries in the
 * first table send matching packets to the second, and the entries in the
 * RSS table send packets to RSS TIRs if possible, or the non-RSS TIR.
 *
 * The flow table entry that delivers packets to an RSS TIR must include match
 * criteria that ensure packets delivered to the TIR include all the fields
 * that the TIR hashes on - so for a v4 TCP TIR, the flow table entry must
 * only accept v4 TCP packets.  Accordingly, we need flow table entries for
 * each TIR.
 *
 * All of this is a lot more flexible than we need, and we can describe most
 * of the stuff we need with a simple array.
 *
 * An RSS config creates a TIR with hashing enabled on a set of fields,
 * pointing to either the first rx queue or the RQT containing all the rx
 * queues, and a flow table entry that matches on an ether type and
 * optionally an ip proto, that delivers packets to the TIR.
 */
static struct mcx_rss_rule {
	int			hash_sel;
	int			flow_group;
	int			ethertype;
	int			ip_proto;
} mcx_rss_config[] = {
	/* udp and tcp for v4/v6 */
	{ MCX_RSS_HASH_SEL_V4_TCP, MCX_FLOW_GROUP_RSS_L4,
	  ETHERTYPE_IP, IPPROTO_TCP },
	{ MCX_RSS_HASH_SEL_V6_TCP, MCX_FLOW_GROUP_RSS_L4,
	  ETHERTYPE_IPV6, IPPROTO_TCP },
	{ MCX_RSS_HASH_SEL_V4_UDP, MCX_FLOW_GROUP_RSS_L4,
	  ETHERTYPE_IP, IPPROTO_UDP },
	{ MCX_RSS_HASH_SEL_V6_UDP, MCX_FLOW_GROUP_RSS_L4,
	  ETHERTYPE_IPV6, IPPROTO_UDP },

	/* other v4/v6 */
	{ MCX_RSS_HASH_SEL_V4, MCX_FLOW_GROUP_RSS_L3,
	  ETHERTYPE_IP, 0 },
	{ MCX_RSS_HASH_SEL_V6, MCX_FLOW_GROUP_RSS_L3,
	  ETHERTYPE_IPV6, 0 },

	/* non v4/v6 */
	{ 0, MCX_FLOW_GROUP_RSS_NONE, 0, 0 }
};

struct mcx_softc {
	struct device		 sc_dev;
	struct arpcom		 sc_ac;
	struct ifmedia		 sc_media;
	uint64_t		 sc_media_status;
	uint64_t		 sc_media_active;

	pci_chipset_tag_t	 sc_pc;
	pci_intr_handle_t	 sc_ih;
	void			*sc_ihc;
	pcitag_t		 sc_tag;

	bus_dma_tag_t		 sc_dmat;
	bus_space_tag_t		 sc_memt;
	bus_space_handle_t	 sc_memh;
	bus_size_t		 sc_mems;

	struct mcx_dmamem	 sc_cmdq_mem;
	unsigned int		 sc_cmdq_mask;
	unsigned int		 sc_cmdq_size;

	unsigned int		 sc_cmdq_token;
	struct mutex		 sc_cmdq_mtx;
	struct rwlock		 sc_cmdq_kstat_lk;
	struct rwlock		 sc_cmdq_ioctl_lk;

	struct mcx_hwmem	 sc_boot_pages;
	struct mcx_hwmem	 sc_init_pages;
	struct mcx_hwmem	 sc_regular_pages;

	int			 sc_uar;
	int			 sc_pd;
	int			 sc_tdomain;
	uint32_t		 sc_lkey;
	int			 sc_tis;
	int			 sc_tir[nitems(mcx_rss_config)];
	int			 sc_rqt;

	struct mcx_dmamem	 sc_doorbell_mem;

	struct mcx_eq		 sc_admin_eq;
	struct mcx_eq		 sc_queue_eq;

	int			 sc_hardmtu;
	int			 sc_rxbufsz;

	int			 sc_bf_size;
	int			 sc_max_rqt_size;

	struct task		 sc_port_change;

	int			 sc_mac_flow_table_id;
	int			 sc_rss_flow_table_id;
	struct mcx_flow_group	 sc_flow_group[MCX_NUM_FLOW_GROUPS];
	int			 sc_promisc_flow_enabled;
	int			 sc_allmulti_flow_enabled;
	int			 sc_mcast_flow_base;
	int			 sc_extra_mcast;
	uint8_t			 sc_mcast_flows[MCX_NUM_MCAST_FLOWS][ETHER_ADDR_LEN];

	struct mcx_calibration	 sc_calibration[2];
	unsigned int		 sc_calibration_gen;
	struct timeout		 sc_calibrate;
	uint32_t		 sc_mhz;
	uint32_t		 sc_khz;

	struct intrmap		*sc_intrmap;
	struct mcx_queues	*sc_queues;

	int			 sc_mcam_reg;

#if NKSTAT > 0
	struct kstat		*sc_kstat_ieee8023;
	struct kstat		*sc_kstat_rfc2863;
	struct kstat		*sc_kstat_rfc2819;
	struct kstat		*sc_kstat_rfc3635;
	unsigned int		 sc_kstat_mtmp_count;
	struct kstat		**sc_kstat_mtmp;
#endif

	struct timecounter	 sc_timecounter;
};
#define DEVNAME(_sc) ((_sc)->sc_dev.dv_xname)

static int	mcx_match(struct device *, void *, void *);
static void	mcx_attach(struct device *, struct device *, void *);

#if NKSTAT > 0
static void	mcx_kstat_attach(struct mcx_softc *);
#endif

static void	mcx_timecounter_attach(struct mcx_softc *);

static int	mcx_version(struct mcx_softc *);
static int	mcx_init_wait(struct mcx_softc *);
static int	mcx_enable_hca(struct mcx_softc *);
static int	mcx_teardown_hca(struct mcx_softc *, uint16_t);
static int	mcx_access_hca_reg(struct mcx_softc *, uint16_t, int, void *,
		    int, enum mcx_cmdq_slot);
static int	mcx_issi(struct mcx_softc *);
static int	mcx_pages(struct mcx_softc *, struct mcx_hwmem *, uint16_t);
static int	mcx_hca_max_caps(struct mcx_softc *);
static int	mcx_hca_set_caps(struct mcx_softc *);
static int	mcx_init_hca(struct mcx_softc *);
static int	mcx_set_driver_version(struct mcx_softc *);
static int	mcx_iff(struct mcx_softc *);
static int	mcx_alloc_uar(struct mcx_softc *, int *);
static int	mcx_alloc_pd(struct mcx_softc *);
static int	mcx_alloc_tdomain(struct mcx_softc *);
static int	mcx_create_eq(struct mcx_softc *, struct mcx_eq *, int,
		    uint64_t, int);
static int	mcx_query_nic_vport_context(struct mcx_softc *);
static int	mcx_query_special_contexts(struct mcx_softc *);
static int	mcx_set_port_mtu(struct mcx_softc *, int);
static int	mcx_create_cq(struct mcx_softc *, struct mcx_cq *, int, int,
		    int);
static int	mcx_destroy_cq(struct mcx_softc *, struct mcx_cq *);
static int	mcx_create_sq(struct mcx_softc *, struct mcx_tx *, int, int,
		    int);
static int	mcx_destroy_sq(struct mcx_softc *, struct mcx_tx *);
static int	mcx_ready_sq(struct mcx_softc *, struct mcx_tx *);
static int	mcx_create_rq(struct mcx_softc *, struct mcx_rx *, int, int);
static int	mcx_destroy_rq(struct mcx_softc *, struct mcx_rx *);
static int	mcx_ready_rq(struct mcx_softc *, struct mcx_rx *);
static int	mcx_create_tir_direct(struct mcx_softc *, struct mcx_rx *,
		    int *);
static int	mcx_create_tir_indirect(struct mcx_softc *, int, uint32_t,
		    int *);
static int	mcx_destroy_tir(struct mcx_softc *, int);
static int	mcx_create_tis(struct mcx_softc *, int *);
static int	mcx_destroy_tis(struct mcx_softc *, int);
static int	mcx_create_rqt(struct mcx_softc *, int, int *, int *);
static int	mcx_destroy_rqt(struct mcx_softc *, int);
static int	mcx_create_flow_table(struct mcx_softc *, int, int, int *);
static int	mcx_set_flow_table_root(struct mcx_softc *, int);
static int	mcx_destroy_flow_table(struct mcx_softc *, int);
static int	mcx_create_flow_group(struct mcx_softc *, int, int, int,
		    int, int, struct mcx_flow_match *);
static int	mcx_destroy_flow_group(struct mcx_softc *, int);
static int	mcx_set_flow_table_entry_mac(struct mcx_softc *, int, int,
		    uint8_t *, uint32_t);
static int	mcx_set_flow_table_entry_proto(struct mcx_softc *, int, int,
		    int, int, uint32_t);
static int	mcx_delete_flow_table_entry(struct mcx_softc *, int, int);

#if NKSTAT > 0
static int	mcx_query_rq(struct mcx_softc *, struct mcx_rx *, struct mcx_rq_ctx *);
static int	mcx_query_sq(struct mcx_softc *, struct mcx_tx *, struct mcx_sq_ctx *);
static int	mcx_query_cq(struct mcx_softc *, struct mcx_cq *, struct mcx_cq_ctx *);
static int	mcx_query_eq(struct mcx_softc *, struct mcx_eq *, struct mcx_eq_ctx *);
#endif

#if 0
static int	mcx_dump_flow_table(struct mcx_softc *, int);
static int	mcx_dump_flow_table_entry(struct mcx_softc *, int, int);
static int	mcx_dump_flow_group(struct mcx_softc *, int);
#endif


/*
static void	mcx_cmdq_dump(const struct mcx_cmdq_entry *);
static void	mcx_cmdq_mbox_dump(struct mcx_dmamem *, int);
*/
static void	mcx_refill(void *);
static int	mcx_process_rx(struct mcx_softc *, struct mcx_rx *,
		    struct mcx_cq_entry *, struct mbuf_list *,
		    const struct mcx_calibration *);
static int	mcx_process_txeof(struct mcx_softc *, struct mcx_tx *,
		    struct mcx_cq_entry *);
static void	mcx_process_cq(struct mcx_softc *, struct mcx_queues *,
		    struct mcx_cq *);

static void	mcx_arm_cq(struct mcx_softc *, struct mcx_cq *, int);
static void	mcx_arm_eq(struct mcx_softc *, struct mcx_eq *, int);
static int	mcx_admin_intr(void *);
static int	mcx_cq_intr(void *);

static int	mcx_up(struct mcx_softc *);
static void	mcx_down(struct mcx_softc *);
static int	mcx_ioctl(struct ifnet *, u_long, caddr_t);
static int	mcx_rxrinfo(struct mcx_softc *, struct if_rxrinfo *);
static void	mcx_start(struct ifqueue *);
static void	mcx_watchdog(struct ifnet *);
static void	mcx_media_add_types(struct mcx_softc *);
static void	mcx_media_status(struct ifnet *, struct ifmediareq *);
static int	mcx_media_change(struct ifnet *);
static int	mcx_get_sffpage(struct ifnet *, struct if_sffpage *);
static void	mcx_port_change(void *);

static void	mcx_calibrate_first(struct mcx_softc *);
static void	mcx_calibrate(void *);

static inline uint32_t
		mcx_rd(struct mcx_softc *, bus_size_t);
static inline void
		mcx_wr(struct mcx_softc *, bus_size_t, uint32_t);
static inline void
		mcx_bar(struct mcx_softc *, bus_size_t, bus_size_t, int);

static uint64_t	mcx_timer(struct mcx_softc *);

static int	mcx_dmamem_alloc(struct mcx_softc *, struct mcx_dmamem *,
		    bus_size_t, u_int align);
static void	mcx_dmamem_zero(struct mcx_dmamem *);
static void	mcx_dmamem_free(struct mcx_softc *, struct mcx_dmamem *);

static int	mcx_hwmem_alloc(struct mcx_softc *, struct mcx_hwmem *,
		    unsigned int);
static void	mcx_hwmem_free(struct mcx_softc *, struct mcx_hwmem *);

struct cfdriver mcx_cd = {
	NULL,
	"mcx",
	DV_IFNET,
};

const struct cfattach mcx_ca = {
	sizeof(struct mcx_softc),
	mcx_match,
	mcx_attach,
};

static const struct pci_matchid mcx_devices[] = {
	{ PCI_VENDOR_MELLANOX,	PCI_PRODUCT_MELLANOX_MT27700 },
	{ PCI_VENDOR_MELLANOX,	PCI_PRODUCT_MELLANOX_MT27700VF },
	{ PCI_VENDOR_MELLANOX,	PCI_PRODUCT_MELLANOX_MT27710 },
	{ PCI_VENDOR_MELLANOX,	PCI_PRODUCT_MELLANOX_MT27710VF },
	{ PCI_VENDOR_MELLANOX,	PCI_PRODUCT_MELLANOX_MT27800 },
	{ PCI_VENDOR_MELLANOX,	PCI_PRODUCT_MELLANOX_MT27800VF },
	{ PCI_VENDOR_MELLANOX,	PCI_PRODUCT_MELLANOX_MT28800 },
	{ PCI_VENDOR_MELLANOX,	PCI_PRODUCT_MELLANOX_MT28800VF },
	{ PCI_VENDOR_MELLANOX,	PCI_PRODUCT_MELLANOX_MT28908 },
	{ PCI_VENDOR_MELLANOX,	PCI_PRODUCT_MELLANOX_MT28908VF },
	{ PCI_VENDOR_MELLANOX,	PCI_PRODUCT_MELLANOX_MT2892 },
	{ PCI_VENDOR_MELLANOX,	PCI_PRODUCT_MELLANOX_MT2894 },
};

struct mcx_eth_proto_capability {
	uint64_t	cap_media;
	uint64_t	cap_baudrate;
};

static const struct mcx_eth_proto_capability mcx_eth_cap_map[] = {
	[MCX_ETHER_CAP_SGMII]		= { IFM_1000_SGMII,	IF_Gbps(1) },
	[MCX_ETHER_CAP_1000_KX]		= { IFM_1000_KX,	IF_Gbps(1) },
	[MCX_ETHER_CAP_10G_CX4]		= { IFM_10G_CX4,	IF_Gbps(10) },
	[MCX_ETHER_CAP_10G_KX4]		= { IFM_10G_KX4,	IF_Gbps(10) },
	[MCX_ETHER_CAP_10G_KR]		= { IFM_10G_KR,		IF_Gbps(10) },
	[MCX_ETHER_CAP_40G_CR4]		= { IFM_40G_CR4,	IF_Gbps(40) },
	[MCX_ETHER_CAP_40G_KR4]		= { IFM_40G_KR4,	IF_Gbps(40) },
	[MCX_ETHER_CAP_10G_CR]		= { IFM_10G_SFP_CU,	IF_Gbps(10) },
	[MCX_ETHER_CAP_10G_SR]		= { IFM_10G_SR,		IF_Gbps(10) },
	[MCX_ETHER_CAP_10G_LR]		= { IFM_10G_LR,		IF_Gbps(10) },
	[MCX_ETHER_CAP_40G_SR4]		= { IFM_40G_SR4,	IF_Gbps(40) },
	[MCX_ETHER_CAP_40G_LR4]		= { IFM_40G_LR4,	IF_Gbps(40) },
	[MCX_ETHER_CAP_50G_SR2]		= { 0 /*IFM_50G_SR2*/,	IF_Gbps(50) },
	[MCX_ETHER_CAP_100G_CR4]	= { IFM_100G_CR4,	IF_Gbps(100) },
	[MCX_ETHER_CAP_100G_SR4]	= { IFM_100G_SR4,	IF_Gbps(100) },
	[MCX_ETHER_CAP_100G_KR4]	= { IFM_100G_KR4,	IF_Gbps(100) },
	[MCX_ETHER_CAP_100G_LR4]	= { IFM_100G_LR4,	IF_Gbps(100) },
	[MCX_ETHER_CAP_25G_CR]		= { IFM_25G_CR,		IF_Gbps(25) },
	[MCX_ETHER_CAP_25G_KR]		= { IFM_25G_KR,		IF_Gbps(25) },
	[MCX_ETHER_CAP_25G_SR]		= { IFM_25G_SR,		IF_Gbps(25) },
	[MCX_ETHER_CAP_50G_CR2]		= { IFM_50G_CR2,	IF_Gbps(50) },
	[MCX_ETHER_CAP_50G_KR2]		= { IFM_50G_KR2,	IF_Gbps(50) },
};

static const struct mcx_eth_proto_capability mcx_ext_eth_cap_map[] = {
	[MCX_ETHER_EXT_CAP_SGMII_100]	= { IFM_100_FX,		IF_Mbps(100) },
	[MCX_ETHER_EXT_CAP_1000_X]	= { IFM_1000_SX,	IF_Gbps(1) },
	[MCX_ETHER_EXT_CAP_5G_R]	= { IFM_5000_T,		IF_Gbps(5) },
	[MCX_ETHER_EXT_CAP_XAUI]	= { IFM_10G_SFI,	IF_Gbps(10) },
	[MCX_ETHER_EXT_CAP_XLAUI]	= { IFM_40G_XLPPI,	IF_Gbps(40) },
	[MCX_ETHER_EXT_CAP_25G_AUI1]	= { 0 /*IFM_25G_AUI*/,	IF_Gbps(25) },
	[MCX_ETHER_EXT_CAP_50G_AUI2]	= { 0 /*IFM_50G_AUI*/,	IF_Gbps(50) },
	[MCX_ETHER_EXT_CAP_50G_AUI1]	= { 0 /*IFM_50G_AUI*/,	IF_Gbps(50) },
	[MCX_ETHER_EXT_CAP_CAUI4]	= { 0 /*IFM_100G_AUI*/,	IF_Gbps(100) },
	[MCX_ETHER_EXT_CAP_100G_AUI2]	= { 0 /*IFM_100G_AUI*/,	IF_Gbps(100) },
	[MCX_ETHER_EXT_CAP_200G_AUI4]	= { 0 /*IFM_200G_AUI*/,	IF_Gbps(200) },
	[MCX_ETHER_EXT_CAP_400G_AUI8]	= { 0 /*IFM_400G_AUI*/,	IF_Gbps(400) },
};

static int
mcx_get_id(uint32_t val)
{
	return betoh32(val) & 0x00ffffff;
}

static int
mcx_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, mcx_devices, nitems(mcx_devices)));
}

void
mcx_attach(struct device *parent, struct device *self, void *aux)
{
	struct mcx_softc *sc = (struct mcx_softc *)self;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct pci_attach_args *pa = aux;
	pcireg_t memtype;
	uint32_t r;
	unsigned int cq_stride;
	unsigned int cq_size;
	const char *intrstr;
	int i, msix;

	sc->sc_pc = pa->pa_pc;
	sc->sc_tag = pa->pa_tag;
	sc->sc_dmat = pa->pa_dmat;

	/* Map the PCI memory space */
	memtype = pci_mapreg_type(sc->sc_pc, sc->sc_tag, MCX_HCA_BAR);
	if (pci_mapreg_map(pa, MCX_HCA_BAR, memtype,
	    BUS_SPACE_MAP_PREFETCHABLE, &sc->sc_memt, &sc->sc_memh,
	    NULL, &sc->sc_mems, 0)) {
		printf(": unable to map register memory\n");
		return;
	}

	if (mcx_version(sc) != 0) {
		/* error printed by mcx_version */
		goto unmap;
	}

	r = mcx_rd(sc, MCX_CMDQ_ADDR_LO);
	cq_stride = 1 << MCX_CMDQ_LOG_STRIDE(r); /* size of the entries */
	cq_size = 1 << MCX_CMDQ_LOG_SIZE(r); /* number of entries */
	if (cq_size > MCX_MAX_CQE) {
		printf(", command queue size overflow %u\n", cq_size);
		goto unmap;
	}
	if (cq_stride < sizeof(struct mcx_cmdq_entry)) {
		printf(", command queue entry size underflow %u\n", cq_stride);
		goto unmap;
	}
	if (cq_stride * cq_size > MCX_PAGE_SIZE) {
		printf(", command queue page overflow\n");
		goto unmap;
	}

	if (mcx_dmamem_alloc(sc, &sc->sc_doorbell_mem, MCX_DOORBELL_AREA_SIZE,
	    MCX_PAGE_SIZE) != 0) {
		printf(", unable to allocate doorbell memory\n");
		goto unmap;
	}

	if (mcx_dmamem_alloc(sc, &sc->sc_cmdq_mem, MCX_PAGE_SIZE,
	    MCX_PAGE_SIZE) != 0) {
		printf(", unable to allocate command queue\n");
		goto dbfree;
	}

	mcx_wr(sc, MCX_CMDQ_ADDR_HI, MCX_DMA_DVA(&sc->sc_cmdq_mem) >> 32);
	mcx_bar(sc, MCX_CMDQ_ADDR_HI, sizeof(uint32_t),
	    BUS_SPACE_BARRIER_WRITE);
	mcx_wr(sc, MCX_CMDQ_ADDR_LO, MCX_DMA_DVA(&sc->sc_cmdq_mem));
	mcx_bar(sc, MCX_CMDQ_ADDR_LO, sizeof(uint32_t),
	    BUS_SPACE_BARRIER_WRITE);

	if (mcx_init_wait(sc) != 0) {
		printf(", timeout waiting for init\n");
		goto cqfree;
	}

	sc->sc_cmdq_mask = cq_size - 1;
	sc->sc_cmdq_size = cq_stride;
	rw_init(&sc->sc_cmdq_kstat_lk, "mcxkstat");
	rw_init(&sc->sc_cmdq_ioctl_lk, "mcxioctl");
	mtx_init(&sc->sc_cmdq_mtx, IPL_NET);

	if (mcx_enable_hca(sc) != 0) {
		/* error printed by mcx_enable_hca */
		goto cqfree;
	}

	if (mcx_issi(sc) != 0) {
		/* error printed by mcx_issi */
		goto teardown;
	}

	if (mcx_pages(sc, &sc->sc_boot_pages,
	    htobe16(MCX_CMD_QUERY_PAGES_BOOT)) != 0) {
		/* error printed by mcx_pages */
		goto teardown;
	}

	if (mcx_hca_max_caps(sc) != 0) {
		/* error printed by mcx_hca_max_caps */
		goto teardown;
	}

	if (mcx_hca_set_caps(sc) != 0) {
		/* error printed by mcx_hca_set_caps */
		goto teardown;
	}

	if (mcx_pages(sc, &sc->sc_init_pages,
	    htobe16(MCX_CMD_QUERY_PAGES_INIT)) != 0) {
		/* error printed by mcx_pages */
		goto teardown;
	}

	if (mcx_init_hca(sc) != 0) {
		/* error printed by mcx_init_hca */
		goto teardown;
	}

	if (mcx_pages(sc, &sc->sc_regular_pages,
	    htobe16(MCX_CMD_QUERY_PAGES_REGULAR)) != 0) {
		/* error printed by mcx_pages */
		goto teardown;
	}

	/* apparently not necessary? */
	if (mcx_set_driver_version(sc) != 0) {
		/* error printed by mcx_set_driver_version */
		goto teardown;
	}

	if (mcx_iff(sc) != 0) {	/* modify nic vport context */
		/* error printed by mcx_iff? */
		goto teardown;
	}

	if (mcx_alloc_uar(sc, &sc->sc_uar) != 0) {
		/* error printed by mcx_alloc_uar */
		goto teardown;
	}

	if (mcx_alloc_pd(sc) != 0) {
		/* error printed by mcx_alloc_pd */
		goto teardown;
	}

	if (mcx_alloc_tdomain(sc) != 0) {
		/* error printed by mcx_alloc_tdomain */
		goto teardown;
	}

	msix = pci_intr_msix_count(pa);
	if (msix < 2) {
		printf(": not enough msi-x vectors\n");
		goto teardown;
	}

	/*
	 * PRM makes no mention of msi interrupts, just legacy and msi-x.
	 * mellanox support tells me legacy interrupts are not supported,
	 * so we're stuck with just msi-x.
	 */
	if (pci_intr_map_msix(pa, 0, &sc->sc_ih) != 0) {
		printf(": unable to map interrupt\n");
		goto teardown;
	}
	intrstr = pci_intr_string(sc->sc_pc, sc->sc_ih);
	sc->sc_ihc = pci_intr_establish(sc->sc_pc, sc->sc_ih,
	    IPL_NET | IPL_MPSAFE, mcx_admin_intr, sc, DEVNAME(sc));
	if (sc->sc_ihc == NULL) {
		printf(": unable to establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto teardown;
	}

	if (mcx_create_eq(sc, &sc->sc_admin_eq, sc->sc_uar,
	    (1ull << MCX_EVENT_TYPE_INTERNAL_ERROR) |
	    (1ull << MCX_EVENT_TYPE_PORT_CHANGE) |
	    (1ull << MCX_EVENT_TYPE_CMD_COMPLETION) |
	    (1ull << MCX_EVENT_TYPE_PAGE_REQUEST), 0) != 0) {
		/* error printed by mcx_create_eq */
		goto teardown;
	}

	if (mcx_query_nic_vport_context(sc) != 0) {
		/* error printed by mcx_query_nic_vport_context */
		goto teardown;
	}

	if (mcx_query_special_contexts(sc) != 0) {
		/* error printed by mcx_query_special_contexts */
		goto teardown;
	}

	if (mcx_set_port_mtu(sc, MCX_HARDMTU) != 0) {
		/* error printed by mcx_set_port_mtu */
		goto teardown;
	}

	msix--; /* admin ops took one */
	sc->sc_intrmap = intrmap_create(&sc->sc_dev, msix, MCX_MAX_QUEUES,
	    INTRMAP_POWEROF2);
	if (sc->sc_intrmap == NULL) {
		printf(": unable to create interrupt map\n");
		goto teardown;
	}
	sc->sc_queues = mallocarray(intrmap_count(sc->sc_intrmap),
	    sizeof(*sc->sc_queues), M_DEVBUF, M_WAITOK|M_ZERO);
	if (sc->sc_queues == NULL) {
		printf(": unable to create queues\n");
		goto intrunmap;
	}

	printf(", %s, %d queue%s, address %s\n", intrstr,
	    intrmap_count(sc->sc_intrmap),
	    intrmap_count(sc->sc_intrmap) > 1 ? "s" : "",
	    ether_sprintf(sc->sc_ac.ac_enaddr));

	strlcpy(ifp->if_xname, DEVNAME(sc), IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_MULTICAST | IFF_SIMPLEX;
	ifp->if_xflags = IFXF_MPSAFE;
	ifp->if_ioctl = mcx_ioctl;
	ifp->if_qstart = mcx_start;
	ifp->if_watchdog = mcx_watchdog;
	ifp->if_hardmtu = sc->sc_hardmtu;
	ifp->if_capabilities = IFCAP_VLAN_MTU | IFCAP_CSUM_IPv4 |
	    IFCAP_CSUM_UDPv4 | IFCAP_CSUM_UDPv6 | IFCAP_CSUM_TCPv4 |
	    IFCAP_CSUM_TCPv6;
#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif
	ifq_init_maxlen(&ifp->if_snd, 1024);

	ifmedia_init(&sc->sc_media, IFM_IMASK, mcx_media_change,
	    mcx_media_status);
	mcx_media_add_types(sc);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);

	if_attach_iqueues(ifp, intrmap_count(sc->sc_intrmap));
	if_attach_queues(ifp, intrmap_count(sc->sc_intrmap));
	for (i = 0; i < intrmap_count(sc->sc_intrmap); i++) {
		struct ifiqueue *ifiq = ifp->if_iqs[i];
		struct ifqueue *ifq = ifp->if_ifqs[i];
		struct mcx_queues *q = &sc->sc_queues[i];
		struct mcx_rx *rx = &q->q_rx;
		struct mcx_tx *tx = &q->q_tx;
		pci_intr_handle_t ih;
		int vec;

		vec = i + 1;
		q->q_sc = sc;
		q->q_index = i;

		if (mcx_alloc_uar(sc, &q->q_uar) != 0) {
			printf("%s: unable to alloc uar %d\n",
			    DEVNAME(sc), i);
			goto intrdisestablish;
		}

		if (mcx_create_eq(sc, &q->q_eq, q->q_uar, 0, vec) != 0) {
			printf("%s: unable to create event queue %d\n",
			    DEVNAME(sc), i);
			goto intrdisestablish;
		}

		rx->rx_softc = sc;
		rx->rx_ifiq = ifiq;
		timeout_set(&rx->rx_refill, mcx_refill, rx);
		ifiq->ifiq_softc = rx;

		tx->tx_softc = sc;
		tx->tx_ifq = ifq;
		ifq->ifq_softc = tx;

		if (pci_intr_map_msix(pa, vec, &ih) != 0) {
			printf("%s: unable to map queue interrupt %d\n",
			    DEVNAME(sc), i);
			goto intrdisestablish;
		}
		snprintf(q->q_name, sizeof(q->q_name), "%s:%d",
		    DEVNAME(sc), i);
		q->q_ihc = pci_intr_establish_cpu(sc->sc_pc, ih,
		    IPL_NET | IPL_MPSAFE, intrmap_cpu(sc->sc_intrmap, i),
		    mcx_cq_intr, q, q->q_name);
		if (q->q_ihc == NULL) {
			printf("%s: unable to establish interrupt %d\n",
			    DEVNAME(sc), i);
			goto intrdisestablish;
		}

#if NBPFILTER > 0
		bpfxattach(&q->q_bpf, q->q_name,
		    ifp, DLT_EN10MB, ETHER_HDR_LEN);

		ifiq->ifiq_bpfp = &q->q_bpf;
		tx->tx_bpfp = &q->q_bpf;
#endif
	}

	timeout_set(&sc->sc_calibrate, mcx_calibrate, sc);

	task_set(&sc->sc_port_change, mcx_port_change, sc);
	mcx_port_change(sc);

	sc->sc_mac_flow_table_id = -1;
	sc->sc_rss_flow_table_id = -1;
	sc->sc_rqt = -1;
	for (i = 0; i < MCX_NUM_FLOW_GROUPS; i++) {
		struct mcx_flow_group *mfg = &sc->sc_flow_group[i];
		mfg->g_id = -1;
		mfg->g_table = -1;
		mfg->g_size = 0;
		mfg->g_start = 0;
	}
	sc->sc_extra_mcast = 0;
	memset(sc->sc_mcast_flows, 0, sizeof(sc->sc_mcast_flows));

#if NKSTAT > 0
	mcx_kstat_attach(sc);
#endif
	mcx_timecounter_attach(sc);
	return;

intrdisestablish:
	for (i = 0; i < intrmap_count(sc->sc_intrmap); i++) {
		struct mcx_queues *q = &sc->sc_queues[i];
		if (q->q_ihc == NULL)
			continue;
		pci_intr_disestablish(sc->sc_pc, q->q_ihc);
		q->q_ihc = NULL;
	}
	free(sc->sc_queues, M_DEVBUF,
	    intrmap_count(sc->sc_intrmap) * sizeof(*sc->sc_queues));
intrunmap:
	intrmap_destroy(sc->sc_intrmap);
	sc->sc_intrmap = NULL;
teardown:
	mcx_teardown_hca(sc, htobe16(MCX_CMD_TEARDOWN_HCA_GRACEFUL));
	/* error printed by mcx_teardown_hca, and we're already unwinding */
cqfree:
	mcx_wr(sc, MCX_CMDQ_ADDR_HI, MCX_DMA_DVA(&sc->sc_cmdq_mem) >> 32);
	mcx_bar(sc, MCX_CMDQ_ADDR_HI, sizeof(uint64_t),
	    BUS_SPACE_BARRIER_WRITE);
	mcx_wr(sc, MCX_CMDQ_ADDR_LO, MCX_DMA_DVA(&sc->sc_cmdq_mem) |
	    MCX_CMDQ_INTERFACE_DISABLED);
	mcx_bar(sc, MCX_CMDQ_ADDR_LO, sizeof(uint64_t),
	    BUS_SPACE_BARRIER_WRITE);

	mcx_wr(sc, MCX_CMDQ_ADDR_HI, 0);
	mcx_bar(sc, MCX_CMDQ_ADDR_HI, sizeof(uint64_t),
	    BUS_SPACE_BARRIER_WRITE);
	mcx_wr(sc, MCX_CMDQ_ADDR_LO, MCX_CMDQ_INTERFACE_DISABLED);

	mcx_dmamem_free(sc, &sc->sc_cmdq_mem);
dbfree:
	mcx_dmamem_free(sc, &sc->sc_doorbell_mem);
unmap:
	bus_space_unmap(sc->sc_memt, sc->sc_memh, sc->sc_mems);
	sc->sc_mems = 0;
}

static int
mcx_version(struct mcx_softc *sc)
{
	uint32_t fw0, fw1;
	uint16_t cmdif;

	fw0 = mcx_rd(sc, MCX_FW_VER);
	fw1 = mcx_rd(sc, MCX_CMDIF_FW_SUBVER);

	printf(": FW %u.%u.%04u", MCX_FW_VER_MAJOR(fw0),
	    MCX_FW_VER_MINOR(fw0), MCX_FW_VER_SUBMINOR(fw1));

	cmdif = MCX_CMDIF(fw1);
	if (cmdif != MCX_CMD_IF_SUPPORTED) {
		printf(", unsupported command interface %u\n", cmdif);
		return (-1);
	}

	return (0);
}

static int
mcx_init_wait(struct mcx_softc *sc)
{
	unsigned int i;
	uint32_t r;

	for (i = 0; i < 2000; i++) {
		r = mcx_rd(sc, MCX_STATE);
		if ((r & MCX_STATE_MASK) == MCX_STATE_READY)
			return (0);

		delay(1000);
		mcx_bar(sc, MCX_STATE, sizeof(uint32_t),
		    BUS_SPACE_BARRIER_READ);
	}

	return (-1);
}

static uint8_t
mcx_cmdq_poll(struct mcx_softc *sc, struct mcx_cmdq_entry *cqe,
    unsigned int msec)
{
	unsigned int i;

	for (i = 0; i < msec; i++) {
		bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&sc->sc_cmdq_mem),
		    0, MCX_DMA_LEN(&sc->sc_cmdq_mem), BUS_DMASYNC_POSTRW);

		if ((cqe->cq_status & MCX_CQ_STATUS_OWN_MASK) ==
		    MCX_CQ_STATUS_OWN_SW)
			return (0);

		delay(1000);
	}

	return (ETIMEDOUT);
}

static uint32_t
mcx_mix_u64(uint32_t xor, uint64_t u64)
{
	xor ^= u64 >> 32;
	xor ^= u64;

	return (xor);
}

static uint32_t
mcx_mix_u32(uint32_t xor, uint32_t u32)
{
	xor ^= u32;

	return (xor);
}

static uint32_t
mcx_mix_u8(uint32_t xor, uint8_t u8)
{
	xor ^= u8;

	return (xor);
}

static uint8_t
mcx_mix_done(uint32_t xor)
{
	xor ^= xor >> 16;
	xor ^= xor >> 8;

	return (xor);
}

static uint8_t
mcx_xor(const void *buf, size_t len)
{
	const uint32_t *dwords = buf;
	uint32_t xor = 0xff;
	size_t i;

	len /= sizeof(*dwords);

	for (i = 0; i < len; i++)
		xor ^= dwords[i];

	return (mcx_mix_done(xor));
}

static uint8_t
mcx_cmdq_token(struct mcx_softc *sc)
{
	uint8_t token;

	mtx_enter(&sc->sc_cmdq_mtx);
	do {
		token = ++sc->sc_cmdq_token;
	} while (token == 0);
	mtx_leave(&sc->sc_cmdq_mtx);

	return (token);
}

static struct mcx_cmdq_entry *
mcx_get_cmdq_entry(struct mcx_softc *sc, enum mcx_cmdq_slot slot)
{
	struct mcx_cmdq_entry *cqe;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	cqe += slot;

	/* make sure the slot isn't running a command already */
	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&sc->sc_cmdq_mem),
	    0, MCX_DMA_LEN(&sc->sc_cmdq_mem), BUS_DMASYNC_POSTRW);
	if ((cqe->cq_status & MCX_CQ_STATUS_OWN_MASK) !=
	    MCX_CQ_STATUS_OWN_SW)
		cqe = NULL;

	return (cqe);
}

static void
mcx_cmdq_init(struct mcx_softc *sc, struct mcx_cmdq_entry *cqe,
    uint32_t ilen, uint32_t olen, uint8_t token)
{
	memset(cqe, 0, sc->sc_cmdq_size);

	cqe->cq_type = MCX_CMDQ_TYPE_PCIE;
	htobem32(&cqe->cq_input_length, ilen);
	htobem32(&cqe->cq_output_length, olen);
	cqe->cq_token = token;
	cqe->cq_status = MCX_CQ_STATUS_OWN_HW;
}

static void
mcx_cmdq_sign(struct mcx_cmdq_entry *cqe)
{
	cqe->cq_signature = ~mcx_xor(cqe, sizeof(*cqe));
}

static int
mcx_cmdq_verify(const struct mcx_cmdq_entry *cqe)
{
	/* return (mcx_xor(cqe, sizeof(*cqe)) ? -1 :  0); */
	return (0);
}

static void *
mcx_cmdq_in(struct mcx_cmdq_entry *cqe)
{
	return (&cqe->cq_input_data);
}

static void *
mcx_cmdq_out(struct mcx_cmdq_entry *cqe)
{
	return (&cqe->cq_output_data);
}

static void
mcx_cmdq_post(struct mcx_softc *sc, struct mcx_cmdq_entry *cqe,
    unsigned int slot)
{
	mcx_cmdq_sign(cqe);

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&sc->sc_cmdq_mem),
	    0, MCX_DMA_LEN(&sc->sc_cmdq_mem), BUS_DMASYNC_PRERW);

	mcx_wr(sc, MCX_CMDQ_DOORBELL, 1U << slot);
	mcx_bar(sc, MCX_CMDQ_DOORBELL, sizeof(uint32_t),
	    BUS_SPACE_BARRIER_WRITE);
}

static int
mcx_cmdq_exec(struct mcx_softc *sc, struct mcx_cmdq_entry *cqe,
    unsigned int slot, unsigned int msec)
{
	int err;

	if (slot == MCX_CMDQ_SLOT_POLL) {
		mcx_cmdq_post(sc, cqe, slot);
		return (mcx_cmdq_poll(sc, cqe, msec));
	}

	mtx_enter(&sc->sc_cmdq_mtx);
	mcx_cmdq_post(sc, cqe, slot);

	err = 0;
	while (err == 0) {
		err = msleep_nsec(&sc->sc_cmdq_token, &sc->sc_cmdq_mtx, 0,
		    "mcxcmd", msec * 1000);
		bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&sc->sc_cmdq_mem), 0,
		    MCX_DMA_LEN(&sc->sc_cmdq_mem), BUS_DMASYNC_POSTRW);
		if ((cqe->cq_status & MCX_CQ_STATUS_OWN_MASK) ==
		    MCX_CQ_STATUS_OWN_SW) {
			err = 0;
			break;
		}
	}

	mtx_leave(&sc->sc_cmdq_mtx);
	return (err);
}

static int
mcx_enable_hca(struct mcx_softc *sc)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_enable_hca_in *in;
	struct mcx_cmd_enable_hca_out *out;
	int error;
	uint8_t status;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out), mcx_cmdq_token(sc));

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_ENABLE_HCA);
	in->cmd_op_mod = htobe16(0);
	in->cmd_function_id = htobe16(0);

	mcx_cmdq_post(sc, cqe, 0);

	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf(", hca enable timeout\n");
		return (-1);
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf(", hca enable command corrupt\n");
		return (-1);
	}

	status = cqe->cq_output_data[0];
	if (status != MCX_CQ_STATUS_OK) {
		printf(", hca enable failed (%x)\n", status);
		return (-1);
	}

	return (0);
}

static int
mcx_teardown_hca(struct mcx_softc *sc, uint16_t profile)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_teardown_hca_in *in;
	struct mcx_cmd_teardown_hca_out *out;
	int error;
	uint8_t status;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out), mcx_cmdq_token(sc));

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_TEARDOWN_HCA);
	in->cmd_op_mod = htobe16(0);
	in->cmd_profile = profile;

	mcx_cmdq_post(sc, cqe, 0);

	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf(", hca teardown timeout\n");
		return (-1);
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf(", hca teardown command corrupt\n");
		return (-1);
	}

	status = cqe->cq_output_data[0];
	if (status != MCX_CQ_STATUS_OK) {
		printf(", hca teardown failed (%x)\n", status);
		return (-1);
	}

	return (0);
}

static int
mcx_cmdq_mboxes_alloc(struct mcx_softc *sc, struct mcx_dmamem *mxm,
    unsigned int nmb, uint64_t *ptr, uint8_t token)
{
	caddr_t kva;
	uint64_t dva;
	int i;
	int error;

	error = mcx_dmamem_alloc(sc, mxm,
	    nmb * MCX_CMDQ_MAILBOX_SIZE, MCX_CMDQ_MAILBOX_ALIGN);
	if (error != 0)
		return (error);

	mcx_dmamem_zero(mxm);

	dva = MCX_DMA_DVA(mxm);
	kva = MCX_DMA_KVA(mxm);
	for (i = 0; i < nmb; i++) {
		struct mcx_cmdq_mailbox *mbox = (struct mcx_cmdq_mailbox *)kva;

		/* patch the cqe or mbox pointing at this one */
		htobem64(ptr, dva);

		/* fill in this mbox */
		htobem32(&mbox->mb_block_number, i);
		mbox->mb_token = token;

		/* move to the next one */
		ptr = &mbox->mb_next_ptr;

		dva += MCX_CMDQ_MAILBOX_SIZE;
		kva += MCX_CMDQ_MAILBOX_SIZE;
	}

	return (0);
}

static uint32_t
mcx_cmdq_mbox_ctrl_sig(const struct mcx_cmdq_mailbox *mb)
{
	uint32_t xor = 0xff;

	/* only 3 fields get set, so mix them directly */
	xor = mcx_mix_u64(xor, mb->mb_next_ptr);
	xor = mcx_mix_u32(xor, mb->mb_block_number);
	xor = mcx_mix_u8(xor, mb->mb_token);

	return (mcx_mix_done(xor));
}

static void
mcx_cmdq_mboxes_sign(struct mcx_dmamem *mxm, unsigned int nmb)
{
	caddr_t kva;
	int i;

	kva = MCX_DMA_KVA(mxm);

	for (i = 0; i < nmb; i++) {
		struct mcx_cmdq_mailbox *mb = (struct mcx_cmdq_mailbox *)kva;
		uint8_t sig = mcx_cmdq_mbox_ctrl_sig(mb);
		mb->mb_ctrl_signature = sig;
		mb->mb_signature = sig ^
		    mcx_xor(mb->mb_data, sizeof(mb->mb_data));

		kva += MCX_CMDQ_MAILBOX_SIZE;
	}
}

static void
mcx_cmdq_mboxes_sync(struct mcx_softc *sc, struct mcx_dmamem *mxm, int ops)
{
	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(mxm),
	    0, MCX_DMA_LEN(mxm), ops);
}

static struct mcx_cmdq_mailbox *
mcx_cq_mbox(struct mcx_dmamem *mxm, unsigned int i)
{
	caddr_t kva;

	kva = MCX_DMA_KVA(mxm);
	kva += i * MCX_CMDQ_MAILBOX_SIZE;

	return ((struct mcx_cmdq_mailbox *)kva);
}

static inline void *
mcx_cq_mbox_data(struct mcx_cmdq_mailbox *mb)
{
	return (&mb->mb_data);
}

static void
mcx_cmdq_mboxes_copyin(struct mcx_dmamem *mxm, unsigned int nmb,
    void *b, size_t len)
{
	caddr_t buf = b;
	struct mcx_cmdq_mailbox *mb;
	int i;

	mb = (struct mcx_cmdq_mailbox *)MCX_DMA_KVA(mxm);
	for (i = 0; i < nmb; i++) {

		memcpy(mb->mb_data, buf, min(sizeof(mb->mb_data), len));

		if (sizeof(mb->mb_data) >= len)
			break;

		buf += sizeof(mb->mb_data);
		len -= sizeof(mb->mb_data);
		mb++;
	}
}

static void
mcx_cmdq_mboxes_pas(struct mcx_dmamem *mxm, int offset, int npages,
    struct mcx_dmamem *buf)
{
	uint64_t *pas;
	int mbox, mbox_pages, i;

	mbox = offset / MCX_CMDQ_MAILBOX_DATASIZE;
	offset %= MCX_CMDQ_MAILBOX_DATASIZE;

	pas = mcx_cq_mbox_data(mcx_cq_mbox(mxm, mbox));
	pas += (offset / sizeof(*pas));
	mbox_pages = (MCX_CMDQ_MAILBOX_DATASIZE - offset) / sizeof(*pas);
	for (i = 0; i < npages; i++) {
		if (i == mbox_pages) {
			mbox++;
			pas = mcx_cq_mbox_data(mcx_cq_mbox(mxm, mbox));
			mbox_pages += MCX_CMDQ_MAILBOX_DATASIZE / sizeof(*pas);
		}
		*pas = htobe64(MCX_DMA_DVA(buf) + (i * MCX_PAGE_SIZE));
		pas++;
	}
}

static void
mcx_cmdq_mboxes_copyout(struct mcx_dmamem *mxm, int nmb, void *b, size_t len)
{
	caddr_t buf = b;
	struct mcx_cmdq_mailbox *mb;
	int i;

	mb = (struct mcx_cmdq_mailbox *)MCX_DMA_KVA(mxm);
	for (i = 0; i < nmb; i++) {
		memcpy(buf, mb->mb_data, min(sizeof(mb->mb_data), len));

		if (sizeof(mb->mb_data) >= len)
			break;

		buf += sizeof(mb->mb_data);
		len -= sizeof(mb->mb_data);
		mb++;
	}
}

static void
mcx_cq_mboxes_free(struct mcx_softc *sc, struct mcx_dmamem *mxm)
{
	mcx_dmamem_free(sc, mxm);
}

#if 0
static void
mcx_cmdq_dump(const struct mcx_cmdq_entry *cqe)
{
	unsigned int i;

	printf(" type %02x, ilen %u, iptr %016llx", cqe->cq_type,
	    bemtoh32(&cqe->cq_input_length), bemtoh64(&cqe->cq_input_ptr));

	printf(", idata ");
	for (i = 0; i < sizeof(cqe->cq_input_data); i++)
		printf("%02x", cqe->cq_input_data[i]);

	printf(", odata ");
	for (i = 0; i < sizeof(cqe->cq_output_data); i++)
		printf("%02x", cqe->cq_output_data[i]);

	printf(", optr %016llx, olen %u, token %02x, sig %02x, status %02x",
	    bemtoh64(&cqe->cq_output_ptr), bemtoh32(&cqe->cq_output_length),
	    cqe->cq_token, cqe->cq_signature, cqe->cq_status);
}

static void
mcx_cmdq_mbox_dump(struct mcx_dmamem *mboxes, int num)
{
	int i, j;
	uint8_t *d;

	for (i = 0; i < num; i++) {
		struct mcx_cmdq_mailbox *mbox;
		mbox = mcx_cq_mbox(mboxes, i);

		d = mcx_cq_mbox_data(mbox);
		for (j = 0; j < MCX_CMDQ_MAILBOX_DATASIZE; j++) {
			if (j != 0 && (j % 16 == 0))
				printf("\n");
			printf("%.2x ", d[j]);
		}
	}
}
#endif

static int
mcx_access_hca_reg(struct mcx_softc *sc, uint16_t reg, int op, void *data,
    int len, enum mcx_cmdq_slot slot)
{
	struct mcx_dmamem mxm;
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_access_reg_in *in;
	struct mcx_cmd_access_reg_out *out;
	uint8_t token = mcx_cmdq_token(sc);
	int error, nmb;

	cqe = mcx_get_cmdq_entry(sc, slot);
	if (cqe == NULL)
		return (-1);

	mcx_cmdq_init(sc, cqe, sizeof(*in) + len, sizeof(*out) + len,
	    token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_ACCESS_REG);
	in->cmd_op_mod = htobe16(op);
	in->cmd_register_id = htobe16(reg);

	nmb = howmany(len, MCX_CMDQ_MAILBOX_DATASIZE);
	if (mcx_cmdq_mboxes_alloc(sc, &mxm, nmb,
	    &cqe->cq_output_ptr, token) != 0) {
		printf(", unable to allocate access reg mailboxen\n");
		return (-1);
	}
	cqe->cq_input_ptr = cqe->cq_output_ptr;
	mcx_cmdq_mboxes_copyin(&mxm, nmb, data, len);
	mcx_cmdq_mboxes_sign(&mxm, nmb);
	mcx_cmdq_mboxes_sync(sc, &mxm, BUS_DMASYNC_PRERW);

	error = mcx_cmdq_exec(sc, cqe, slot, 1000);
	mcx_cmdq_mboxes_sync(sc, &mxm, BUS_DMASYNC_POSTRW);

	if (error != 0) {
		printf("%s: access reg (%s %x) timeout\n", DEVNAME(sc),
		    (op == MCX_REG_OP_WRITE ? "write" : "read"), reg);
		goto free;
	}
	error = mcx_cmdq_verify(cqe);
	if (error != 0) {
		printf("%s: access reg (%s %x) reply corrupt\n",
		    (op == MCX_REG_OP_WRITE ? "write" : "read"), DEVNAME(sc),
		    reg);
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: access reg (%s %x) failed (%x, %.6x)\n",
		    DEVNAME(sc), (op == MCX_REG_OP_WRITE ? "write" : "read"),
		    reg, out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

	mcx_cmdq_mboxes_copyout(&mxm, nmb, data, len);
free:
	mcx_dmamem_free(sc, &mxm);

	return (error);
}

static int
mcx_set_issi(struct mcx_softc *sc, struct mcx_cmdq_entry *cqe,
    unsigned int slot)
{
	struct mcx_cmd_set_issi_in *in;
	struct mcx_cmd_set_issi_out *out;
	uint8_t status;

	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out), mcx_cmdq_token(sc));

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_SET_ISSI);
	in->cmd_op_mod = htobe16(0);
	in->cmd_current_issi = htobe16(MCX_ISSI);

	mcx_cmdq_post(sc, cqe, slot);
	if (mcx_cmdq_poll(sc, cqe, 1000) != 0)
		return (-1);
	if (mcx_cmdq_verify(cqe) != 0)
		return (-1);

	status = cqe->cq_output_data[0];
	if (status != MCX_CQ_STATUS_OK)
		return (-1);

	return (0);
}

static int
mcx_issi(struct mcx_softc *sc)
{
	struct mcx_dmamem mxm;
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_query_issi_in *in;
	struct mcx_cmd_query_issi_il_out *out;
	struct mcx_cmd_query_issi_mb_out *mb;
	uint8_t token = mcx_cmdq_token(sc);
	uint8_t status;
	int error;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out) + sizeof(*mb), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_QUERY_ISSI);
	in->cmd_op_mod = htobe16(0);

	CTASSERT(sizeof(*mb) <= MCX_CMDQ_MAILBOX_DATASIZE);
	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 1,
	    &cqe->cq_output_ptr, token) != 0) {
		printf(", unable to allocate query issi mailbox\n");
		return (-1);
	}
	mcx_cmdq_mboxes_sign(&mxm, 1);

	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf(", query issi timeout\n");
		goto free;
	}
	error = mcx_cmdq_verify(cqe);
	if (error != 0) {
		printf(", query issi reply corrupt\n");
		goto free;
	}

	status = cqe->cq_output_data[0];
	switch (status) {
	case MCX_CQ_STATUS_OK:
		break;
	case MCX_CQ_STATUS_BAD_OPCODE:
		/* use ISSI 0 */
		goto free;
	default:
		printf(", query issi failed (%x)\n", status);
		error = -1;
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_current_issi == htobe16(MCX_ISSI)) {
		/* use ISSI 1 */
		goto free;
	}

	/* don't need to read cqe anymore, can be used for SET ISSI */

	mb = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	CTASSERT(MCX_ISSI < NBBY);
	 /* XXX math is hard */
	if (!ISSET(mb->cmd_supported_issi[79], 1 << MCX_ISSI)) {
		/* use ISSI 0 */
		goto free;
	}

	if (mcx_set_issi(sc, cqe, 0) != 0) {
		/* ignore the error, just use ISSI 0 */
	} else {
		/* use ISSI 1 */
	}

free:
	mcx_cq_mboxes_free(sc, &mxm);
	return (error);
}

static int
mcx_query_pages(struct mcx_softc *sc, uint16_t type,
    int32_t *npages, uint16_t *func_id)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_query_pages_in *in;
	struct mcx_cmd_query_pages_out *out;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out), mcx_cmdq_token(sc));

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_QUERY_PAGES);
	in->cmd_op_mod = type;

	mcx_cmdq_post(sc, cqe, 0);
	if (mcx_cmdq_poll(sc, cqe, 1000) != 0) {
		printf(", query pages timeout\n");
		return (-1);
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf(", query pages reply corrupt\n");
		return (-1);
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf(", query pages failed (%x)\n", out->cmd_status);
		return (-1);
	}

	*func_id = out->cmd_func_id;
	*npages = bemtoh32(&out->cmd_num_pages);

	return (0);
}

struct bus_dma_iter {
	bus_dmamap_t		i_map;
	bus_size_t		i_offset;
	unsigned int		i_index;
};

static void
bus_dma_iter_init(struct bus_dma_iter *i, bus_dmamap_t map)
{
	i->i_map = map;
	i->i_offset = 0;
	i->i_index = 0;
}

static bus_addr_t
bus_dma_iter_addr(struct bus_dma_iter *i)
{
	return (i->i_map->dm_segs[i->i_index].ds_addr + i->i_offset);
}

static void
bus_dma_iter_add(struct bus_dma_iter *i, bus_size_t size)
{
	bus_dma_segment_t *seg = i->i_map->dm_segs + i->i_index;
	bus_size_t diff;

	do {
		diff = seg->ds_len - i->i_offset;
		if (size < diff)
			break;

		size -= diff;

		seg++;

		i->i_offset = 0;
		i->i_index++;
	} while (size > 0);

	i->i_offset += size;
}

static int
mcx_add_pages(struct mcx_softc *sc, struct mcx_hwmem *mhm, uint16_t func_id)
{
	struct mcx_dmamem mxm;
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_manage_pages_in *in;
	struct mcx_cmd_manage_pages_out *out;
	unsigned int paslen, nmb, i, j, npages;
	struct bus_dma_iter iter;
	uint64_t *pas;
	uint8_t status;
	uint8_t token = mcx_cmdq_token(sc);
	int error;

	npages = mhm->mhm_npages;

	paslen = sizeof(*pas) * npages;
	nmb = howmany(paslen, MCX_CMDQ_MAILBOX_DATASIZE);

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + paslen, sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_MANAGE_PAGES);
	in->cmd_op_mod = htobe16(MCX_CMD_MANAGE_PAGES_ALLOC_SUCCESS);
	in->cmd_func_id = func_id;
	htobem32(&in->cmd_input_num_entries, npages);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, nmb,
	    &cqe->cq_input_ptr, token) != 0) {
		printf(", unable to allocate manage pages mailboxen\n");
		return (-1);
	}

	bus_dma_iter_init(&iter, mhm->mhm_map);
	for (i = 0; i < nmb; i++) {
		unsigned int lim;

		pas = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, i));
		lim = min(MCX_CMDQ_MAILBOX_DATASIZE / sizeof(*pas), npages);

		for (j = 0; j < lim; j++) {
			htobem64(&pas[j], bus_dma_iter_addr(&iter));
			bus_dma_iter_add(&iter, MCX_PAGE_SIZE);
		}

		npages -= lim;
	}

	mcx_cmdq_mboxes_sign(&mxm, nmb);

	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf(", manage pages timeout\n");
		goto free;
	}
	error = mcx_cmdq_verify(cqe);
	if (error != 0) {
		printf(", manage pages reply corrupt\n");
		goto free;
	}

	status = cqe->cq_output_data[0];
	if (status != MCX_CQ_STATUS_OK) {
		printf(", manage pages failed (%x)\n", status);
		error = -1;
		goto free;
	}

free:
	mcx_dmamem_free(sc, &mxm);

	return (error);
}

static int
mcx_pages(struct mcx_softc *sc, struct mcx_hwmem *mhm, uint16_t type)
{
	int32_t npages;
	uint16_t func_id;

	if (mcx_query_pages(sc, type, &npages, &func_id) != 0) {
		/* error printed by mcx_query_pages */
		return (-1);
	}

	if (npages < 1)
		return (0);

	if (mcx_hwmem_alloc(sc, mhm, npages) != 0) {
		printf(", unable to allocate hwmem\n");
		return (-1);
	}

	if (mcx_add_pages(sc, mhm, func_id) != 0) {
		printf(", unable to add hwmem\n");
		goto free;
	}

	return (0);

free:
	mcx_hwmem_free(sc, mhm);

	return (-1);
}

static int
mcx_hca_max_caps(struct mcx_softc *sc)
{
	struct mcx_dmamem mxm;
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_query_hca_cap_in *in;
	struct mcx_cmd_query_hca_cap_out *out;
	struct mcx_cmdq_mailbox *mb;
	struct mcx_cap_device *hca;
	uint8_t status;
	uint8_t token = mcx_cmdq_token(sc);
	int error;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out) + MCX_HCA_CAP_LEN,
	    token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_QUERY_HCA_CAP);
	in->cmd_op_mod = htobe16(MCX_CMD_QUERY_HCA_CAP_MAX |
	    MCX_CMD_QUERY_HCA_CAP_DEVICE);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, MCX_HCA_CAP_NMAILBOXES,
	    &cqe->cq_output_ptr, token) != 0) {
		printf(", unable to allocate query hca caps mailboxen\n");
		return (-1);
	}
	mcx_cmdq_mboxes_sign(&mxm, MCX_HCA_CAP_NMAILBOXES);
	mcx_cmdq_mboxes_sync(sc, &mxm, BUS_DMASYNC_PRERW);

	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	mcx_cmdq_mboxes_sync(sc, &mxm, BUS_DMASYNC_POSTRW);

	if (error != 0) {
		printf(", query hca caps timeout\n");
		goto free;
	}
	error = mcx_cmdq_verify(cqe);
	if (error != 0) {
		printf(", query hca caps reply corrupt\n");
		goto free;
	}

	status = cqe->cq_output_data[0];
	if (status != MCX_CQ_STATUS_OK) {
		printf(", query hca caps failed (%x)\n", status);
		error = -1;
		goto free;
	}

	mb = mcx_cq_mbox(&mxm, 0);
	hca = mcx_cq_mbox_data(mb);

	if ((hca->port_type & MCX_CAP_DEVICE_PORT_TYPE)
	    != MCX_CAP_DEVICE_PORT_TYPE_ETH) {
		printf(", not in ethernet mode\n");
		error = -1;
		goto free;
	}
	if (hca->log_pg_sz > PAGE_SHIFT) {
		printf(", minimum system page shift %u is too large\n",
		    hca->log_pg_sz);
		error = -1;
		goto free;
	}
	/*
	 * blueflame register is split into two buffers, and we must alternate
	 * between the two of them.
	 */
	sc->sc_bf_size = (1 << hca->log_bf_reg_size) / 2;
	sc->sc_max_rqt_size = (1 << hca->log_max_rqt_size);
	
	if (hca->local_ca_ack_delay & MCX_CAP_DEVICE_MCAM_REG)
		sc->sc_mcam_reg = 1;

	sc->sc_mhz = bemtoh32(&hca->device_frequency_mhz);
	sc->sc_khz = bemtoh32(&hca->device_frequency_khz);

free:
	mcx_dmamem_free(sc, &mxm);

	return (error);
}

static int
mcx_hca_set_caps(struct mcx_softc *sc)
{
	struct mcx_dmamem mxm;
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_query_hca_cap_in *in;
	struct mcx_cmd_query_hca_cap_out *out;
	struct mcx_cmdq_mailbox *mb;
	struct mcx_cap_device *hca;
	uint8_t status;
	uint8_t token = mcx_cmdq_token(sc);
	int error;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out) + MCX_HCA_CAP_LEN,
	    token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_QUERY_HCA_CAP);
	in->cmd_op_mod = htobe16(MCX_CMD_QUERY_HCA_CAP_CURRENT |
	    MCX_CMD_QUERY_HCA_CAP_DEVICE);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, MCX_HCA_CAP_NMAILBOXES,
	    &cqe->cq_output_ptr, token) != 0) {
		printf(", unable to allocate manage pages mailboxen\n");
		return (-1);
	}
	mcx_cmdq_mboxes_sign(&mxm, MCX_HCA_CAP_NMAILBOXES);
	mcx_cmdq_mboxes_sync(sc, &mxm, BUS_DMASYNC_PRERW);

	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	mcx_cmdq_mboxes_sync(sc, &mxm, BUS_DMASYNC_POSTRW);

	if (error != 0) {
		printf(", query hca caps timeout\n");
		goto free;
	}
	error = mcx_cmdq_verify(cqe);
	if (error != 0) {
		printf(", query hca caps reply corrupt\n");
		goto free;
	}

	status = cqe->cq_output_data[0];
	if (status != MCX_CQ_STATUS_OK) {
		printf(", query hca caps failed (%x)\n", status);
		error = -1;
		goto free;
	}

	mb = mcx_cq_mbox(&mxm, 0);
	hca = mcx_cq_mbox_data(mb);

	hca->log_pg_sz = PAGE_SHIFT;

free:
	mcx_dmamem_free(sc, &mxm);

	return (error);
}


static int
mcx_init_hca(struct mcx_softc *sc)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_init_hca_in *in;
	struct mcx_cmd_init_hca_out *out;
	int error;
	uint8_t status;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out), mcx_cmdq_token(sc));

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_INIT_HCA);
	in->cmd_op_mod = htobe16(0);

	mcx_cmdq_post(sc, cqe, 0);

	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf(", hca init timeout\n");
		return (-1);
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf(", hca init command corrupt\n");
		return (-1);
	}

	status = cqe->cq_output_data[0];
	if (status != MCX_CQ_STATUS_OK) {
		printf(", hca init failed (%x)\n", status);
		return (-1);
	}

	return (0);
}

static int
mcx_set_driver_version(struct mcx_softc *sc)
{
	struct mcx_dmamem mxm;
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_set_driver_version_in *in;
	struct mcx_cmd_set_driver_version_out *out;
	int error;
	int token;
	uint8_t status;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) +
	    sizeof(struct mcx_cmd_set_driver_version), sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_SET_DRIVER_VERSION);
	in->cmd_op_mod = htobe16(0);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 1,
	    &cqe->cq_input_ptr, token) != 0) {
		printf(", unable to allocate set driver version mailboxen\n");
		return (-1);
	}
	strlcpy(mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0)),
	    "OpenBSD,mcx,1.000.000000", MCX_CMDQ_MAILBOX_DATASIZE);

	mcx_cmdq_mboxes_sign(&mxm, 1);
	mcx_cmdq_post(sc, cqe, 0);

	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf(", set driver version timeout\n");
		goto free;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf(", set driver version command corrupt\n");
		goto free;
	}

	status = cqe->cq_output_data[0];
	if (status != MCX_CQ_STATUS_OK) {
		printf(", set driver version failed (%x)\n", status);
		error = -1;
		goto free;
	}

free:
	mcx_dmamem_free(sc, &mxm);

	return (error);
}

static int
mcx_iff(struct mcx_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct mcx_dmamem mxm;
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_modify_nic_vport_context_in *in;
	struct mcx_cmd_modify_nic_vport_context_out *out;
	struct mcx_nic_vport_ctx *ctx;
	int error;
	int token;
	int insize;
	uint32_t dest;

	dest = MCX_FLOW_CONTEXT_DEST_TYPE_TABLE |
	    sc->sc_rss_flow_table_id;

	/* enable or disable the promisc flow */
	if (ISSET(ifp->if_flags, IFF_PROMISC)) {
		if (sc->sc_promisc_flow_enabled == 0) {
			mcx_set_flow_table_entry_mac(sc,
			    MCX_FLOW_GROUP_PROMISC, 0, NULL, dest);
			sc->sc_promisc_flow_enabled = 1;
		}
	} else if (sc->sc_promisc_flow_enabled != 0) {
		mcx_delete_flow_table_entry(sc, MCX_FLOW_GROUP_PROMISC, 0);
		sc->sc_promisc_flow_enabled = 0;
	}

	/* enable or disable the all-multicast flow */
	if (ISSET(ifp->if_flags, IFF_ALLMULTI)) {
		if (sc->sc_allmulti_flow_enabled == 0) {
			uint8_t mcast[ETHER_ADDR_LEN];

			memset(mcast, 0, sizeof(mcast));
			mcast[0] = 0x01;
			mcx_set_flow_table_entry_mac(sc,
			    MCX_FLOW_GROUP_ALLMULTI, 0, mcast, dest);
			sc->sc_allmulti_flow_enabled = 1;
		}
	} else if (sc->sc_allmulti_flow_enabled != 0) {
		mcx_delete_flow_table_entry(sc, MCX_FLOW_GROUP_ALLMULTI, 0);
		sc->sc_allmulti_flow_enabled = 0;
	}

	insize = sizeof(struct mcx_nic_vport_ctx) + 240;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + insize, sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_MODIFY_NIC_VPORT_CONTEXT);
	in->cmd_op_mod = htobe16(0);
	in->cmd_field_select = htobe32(
	    MCX_CMD_MODIFY_NIC_VPORT_CONTEXT_FIELD_PROMISC |
	    MCX_CMD_MODIFY_NIC_VPORT_CONTEXT_FIELD_MTU);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 1, &cqe->cq_input_ptr, token) != 0) {
		printf(", unable to allocate modify "
		    "nic vport context mailboxen\n");
		return (-1);
	}
	ctx = (struct mcx_nic_vport_ctx *)
	    (((char *)mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0))) + 240);
	ctx->vp_mtu = htobe32(sc->sc_hardmtu);
	/*
         * always leave promisc-all enabled on the vport since we
         * can't give it a vlan list, and we're already doing multicast
         * filtering in the flow table.
	 */
	ctx->vp_flags = htobe16(MCX_NIC_VPORT_CTX_PROMISC_ALL);

	mcx_cmdq_mboxes_sign(&mxm, 1);
	mcx_cmdq_post(sc, cqe, 0);

	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf(", modify nic vport context timeout\n");
		goto free;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf(", modify nic vport context command corrupt\n");
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf(", modify nic vport context failed (%x, %x)\n",
		    out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

free:
	mcx_dmamem_free(sc, &mxm);

	return (error);
}

static int
mcx_alloc_uar(struct mcx_softc *sc, int *uar)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_alloc_uar_in *in;
	struct mcx_cmd_alloc_uar_out *out;
	int error;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out), mcx_cmdq_token(sc));

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_ALLOC_UAR);
	in->cmd_op_mod = htobe16(0);

	mcx_cmdq_post(sc, cqe, 0);

	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf(", alloc uar timeout\n");
		return (-1);
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf(", alloc uar command corrupt\n");
		return (-1);
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf(", alloc uar failed (%x)\n", out->cmd_status);
		return (-1);
	}

	*uar = mcx_get_id(out->cmd_uar);
	return (0);
}

static int
mcx_create_eq(struct mcx_softc *sc, struct mcx_eq *eq, int uar,
    uint64_t events, int vector)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_dmamem mxm;
	struct mcx_cmd_create_eq_in *in;
	struct mcx_cmd_create_eq_mb_in *mbin;
	struct mcx_cmd_create_eq_out *out;
	struct mcx_eq_entry *eqe;
	int error;
	uint64_t *pas;
	int insize, npages, paslen, i, token;

	eq->eq_cons = 0;

	npages = howmany((1 << MCX_LOG_EQ_SIZE) * sizeof(struct mcx_eq_entry),
	    MCX_PAGE_SIZE);
	paslen = npages * sizeof(*pas);
	insize = sizeof(struct mcx_cmd_create_eq_mb_in) + paslen;

	if (mcx_dmamem_alloc(sc, &eq->eq_mem, npages * MCX_PAGE_SIZE,
	    MCX_PAGE_SIZE) != 0) {
		printf(", unable to allocate event queue memory\n");
		return (-1);
	}

	eqe = (struct mcx_eq_entry *)MCX_DMA_KVA(&eq->eq_mem);
	for (i = 0; i < (1 << MCX_LOG_EQ_SIZE); i++) {
		eqe[i].eq_owner = MCX_EQ_ENTRY_OWNER_INIT;
	}

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + insize, sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_CREATE_EQ);
	in->cmd_op_mod = htobe16(0);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm,
	    howmany(insize, MCX_CMDQ_MAILBOX_DATASIZE),
	    &cqe->cq_input_ptr, token) != 0) {
		printf(", unable to allocate create eq mailboxen\n");
		goto free_eq;
	}
	mbin = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	mbin->cmd_eq_ctx.eq_uar_size = htobe32(
	    (MCX_LOG_EQ_SIZE << MCX_EQ_CTX_LOG_EQ_SIZE_SHIFT) | uar);
	mbin->cmd_eq_ctx.eq_intr = vector;
	mbin->cmd_event_bitmask = htobe64(events);

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&eq->eq_mem),
	    0, MCX_DMA_LEN(&eq->eq_mem), BUS_DMASYNC_PREREAD);

	/* physical addresses follow the mailbox in data */
	mcx_cmdq_mboxes_pas(&mxm, sizeof(*mbin), npages, &eq->eq_mem);
	mcx_cmdq_mboxes_sign(&mxm, howmany(insize, MCX_CMDQ_MAILBOX_DATASIZE));
	mcx_cmdq_post(sc, cqe, 0);

	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf(", create eq timeout\n");
		goto free_mxm;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf(", create eq command corrupt\n");
		goto free_mxm;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf(", create eq failed (%x, %x)\n", out->cmd_status,
		    betoh32(out->cmd_syndrome));
		goto free_mxm;
	}

	eq->eq_n = mcx_get_id(out->cmd_eqn);

	mcx_dmamem_free(sc, &mxm);

	mcx_arm_eq(sc, eq, uar);

	return (0);

free_mxm:
	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&eq->eq_mem),
	    0, MCX_DMA_LEN(&eq->eq_mem), BUS_DMASYNC_POSTREAD);
	mcx_dmamem_free(sc, &mxm);
free_eq:
	mcx_dmamem_free(sc, &eq->eq_mem);
	return (-1);
}

static int
mcx_alloc_pd(struct mcx_softc *sc)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_alloc_pd_in *in;
	struct mcx_cmd_alloc_pd_out *out;
	int error;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out), mcx_cmdq_token(sc));

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_ALLOC_PD);
	in->cmd_op_mod = htobe16(0);

	mcx_cmdq_post(sc, cqe, 0);

	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf(", alloc pd timeout\n");
		return (-1);
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf(", alloc pd command corrupt\n");
		return (-1);
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf(", alloc pd failed (%x)\n", out->cmd_status);
		return (-1);
	}

	sc->sc_pd = mcx_get_id(out->cmd_pd);
	return (0);
}

static int
mcx_alloc_tdomain(struct mcx_softc *sc)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_alloc_td_in *in;
	struct mcx_cmd_alloc_td_out *out;
	int error;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out), mcx_cmdq_token(sc));

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_ALLOC_TRANSPORT_DOMAIN);
	in->cmd_op_mod = htobe16(0);

	mcx_cmdq_post(sc, cqe, 0);

	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf(", alloc transport domain timeout\n");
		return (-1);
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf(", alloc transport domain command corrupt\n");
		return (-1);
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf(", alloc transport domain failed (%x)\n",
		    out->cmd_status);
		return (-1);
	}

	sc->sc_tdomain = mcx_get_id(out->cmd_tdomain);
	return (0);
}

static int
mcx_query_nic_vport_context(struct mcx_softc *sc)
{
	struct mcx_dmamem mxm;
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_query_nic_vport_context_in *in;
	struct mcx_cmd_query_nic_vport_context_out *out;
	struct mcx_nic_vport_ctx *ctx;
	uint8_t *addr;
	int error, token, i;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out) + sizeof(*ctx), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_QUERY_NIC_VPORT_CONTEXT);
	in->cmd_op_mod = htobe16(0);
	in->cmd_allowed_list_type = 0;

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 1,
	    &cqe->cq_output_ptr, token) != 0) {
		printf(", unable to allocate "
		    "query nic vport context mailboxen\n");
		return (-1);
	}
	mcx_cmdq_mboxes_sign(&mxm, 1);
	mcx_cmdq_post(sc, cqe, 0);

	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf(", query nic vport context timeout\n");
		goto free;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf(", query nic vport context command corrupt\n");
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf(", query nic vport context failed (%x, %x)\n",
		    out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

	ctx = (struct mcx_nic_vport_ctx *)
	    mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	addr = (uint8_t *)&ctx->vp_perm_addr;
	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		sc->sc_ac.ac_enaddr[i] = addr[i + 2];
	}
free:
	mcx_dmamem_free(sc, &mxm);

	return (error);
}

static int
mcx_query_special_contexts(struct mcx_softc *sc)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_query_special_ctx_in *in;
	struct mcx_cmd_query_special_ctx_out *out;
	int error;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out), mcx_cmdq_token(sc));

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_QUERY_SPECIAL_CONTEXTS);
	in->cmd_op_mod = htobe16(0);

	mcx_cmdq_post(sc, cqe, 0);

	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf(", query special contexts timeout\n");
		return (-1);
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf(", query special contexts command corrupt\n");
		return (-1);
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf(", query special contexts failed (%x)\n",
		    out->cmd_status);
		return (-1);
	}

	sc->sc_lkey = betoh32(out->cmd_resd_lkey);
	return (0);
}

static int
mcx_set_port_mtu(struct mcx_softc *sc, int mtu)
{
	struct mcx_reg_pmtu pmtu;
	int error;

	/* read max mtu */
	memset(&pmtu, 0, sizeof(pmtu));
	pmtu.rp_local_port = 1;
	error = mcx_access_hca_reg(sc, MCX_REG_PMTU, MCX_REG_OP_READ, &pmtu,
	    sizeof(pmtu), MCX_CMDQ_SLOT_POLL);
	if (error != 0) {
		printf(", unable to get port MTU\n");
		return error;
	}

	mtu = min(mtu, betoh16(pmtu.rp_max_mtu));
	pmtu.rp_admin_mtu = htobe16(mtu);
	error = mcx_access_hca_reg(sc, MCX_REG_PMTU, MCX_REG_OP_WRITE, &pmtu,
	    sizeof(pmtu), MCX_CMDQ_SLOT_POLL);
	if (error != 0) {
		printf(", unable to set port MTU\n");
		return error;
	}

	sc->sc_hardmtu = mtu;
	sc->sc_rxbufsz = roundup(mtu + ETHER_ALIGN, sizeof(long));
	return 0;
}

static int
mcx_create_cq(struct mcx_softc *sc, struct mcx_cq *cq, int uar, int db, int eqn)
{
	struct mcx_cmdq_entry *cmde;
	struct mcx_cq_entry *cqe;
	struct mcx_dmamem mxm;
	struct mcx_cmd_create_cq_in *in;
	struct mcx_cmd_create_cq_mb_in *mbin;
	struct mcx_cmd_create_cq_out *out;
	int error;
	uint64_t *pas;
	int insize, npages, paslen, i, token;

	cq->cq_doorbell = MCX_CQ_DOORBELL_BASE + (MCX_CQ_DOORBELL_STRIDE * db);

	npages = howmany((1 << MCX_LOG_CQ_SIZE) * sizeof(struct mcx_cq_entry),
	    MCX_PAGE_SIZE);
	paslen = npages * sizeof(*pas);
	insize = sizeof(struct mcx_cmd_create_cq_mb_in) + paslen;

	if (mcx_dmamem_alloc(sc, &cq->cq_mem, npages * MCX_PAGE_SIZE,
	    MCX_PAGE_SIZE) != 0) {
		printf("%s: unable to allocate completion queue memory\n",
		    DEVNAME(sc));
		return (-1);
	}
	cqe = MCX_DMA_KVA(&cq->cq_mem);
	for (i = 0; i < (1 << MCX_LOG_CQ_SIZE); i++) {
		cqe[i].cq_opcode_owner = MCX_CQ_ENTRY_FLAG_OWNER;
	}

	cmde = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cmde, sizeof(*in) + insize, sizeof(*out), token);

	in = mcx_cmdq_in(cmde);
	in->cmd_opcode = htobe16(MCX_CMD_CREATE_CQ);
	in->cmd_op_mod = htobe16(0);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm,
	    howmany(insize, MCX_CMDQ_MAILBOX_DATASIZE),
	    &cmde->cq_input_ptr, token) != 0) {
		printf("%s: unable to allocate create cq mailboxen\n",
		    DEVNAME(sc));
		goto free_cq;
	}
	mbin = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	mbin->cmd_cq_ctx.cq_uar_size = htobe32(
	    (MCX_LOG_CQ_SIZE << MCX_CQ_CTX_LOG_CQ_SIZE_SHIFT) | uar);
	mbin->cmd_cq_ctx.cq_eqn = htobe32(eqn);
	mbin->cmd_cq_ctx.cq_period_max_count = htobe32(
	    (MCX_CQ_MOD_PERIOD << MCX_CQ_CTX_PERIOD_SHIFT) |
	    MCX_CQ_MOD_COUNTER);
	mbin->cmd_cq_ctx.cq_doorbell = htobe64(
	    MCX_DMA_DVA(&sc->sc_doorbell_mem) + cq->cq_doorbell);

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&cq->cq_mem),
	    0, MCX_DMA_LEN(&cq->cq_mem), BUS_DMASYNC_PREREAD);

	/* physical addresses follow the mailbox in data */
	mcx_cmdq_mboxes_pas(&mxm, sizeof(*mbin), npages, &cq->cq_mem);
	mcx_cmdq_post(sc, cmde, 0);

	error = mcx_cmdq_poll(sc, cmde, 1000);
	if (error != 0) {
		printf("%s: create cq timeout\n", DEVNAME(sc));
		goto free_mxm;
	}
	if (mcx_cmdq_verify(cmde) != 0) {
		printf("%s: create cq command corrupt\n", DEVNAME(sc));
		goto free_mxm;
	}

	out = mcx_cmdq_out(cmde);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: create cq failed (%x, %x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		goto free_mxm;
	}

	cq->cq_n = mcx_get_id(out->cmd_cqn);
	cq->cq_cons = 0;
	cq->cq_count = 0;

	mcx_dmamem_free(sc, &mxm);

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&sc->sc_doorbell_mem),
	    cq->cq_doorbell, sizeof(struct mcx_cq_doorbell),
	    BUS_DMASYNC_PREWRITE);

	mcx_arm_cq(sc, cq, uar);

	return (0);

free_mxm:
	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&cq->cq_mem),
	    0, MCX_DMA_LEN(&cq->cq_mem), BUS_DMASYNC_POSTREAD);
	mcx_dmamem_free(sc, &mxm);
free_cq:
	mcx_dmamem_free(sc, &cq->cq_mem);
	return (-1);
}

static int
mcx_destroy_cq(struct mcx_softc *sc, struct mcx_cq *cq)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_destroy_cq_in *in;
	struct mcx_cmd_destroy_cq_out *out;
	int error;
	int token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_DESTROY_CQ);
	in->cmd_op_mod = htobe16(0);
	in->cmd_cqn = htobe32(cq->cq_n);

	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: destroy cq timeout\n", DEVNAME(sc));
		return error;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: destroy cq command corrupt\n", DEVNAME(sc));
		return error;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: destroy cq failed (%x, %x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		return -1;
	}

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&sc->sc_doorbell_mem),
	    cq->cq_doorbell, sizeof(struct mcx_cq_doorbell),
	    BUS_DMASYNC_POSTWRITE);

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&cq->cq_mem),
	    0, MCX_DMA_LEN(&cq->cq_mem), BUS_DMASYNC_POSTREAD);
	mcx_dmamem_free(sc, &cq->cq_mem);

	cq->cq_n = 0;
	cq->cq_cons = 0;
	cq->cq_count = 0;
	return 0;
}

static int
mcx_create_rq(struct mcx_softc *sc, struct mcx_rx *rx, int db, int cqn)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_dmamem mxm;
	struct mcx_cmd_create_rq_in *in;
	struct mcx_cmd_create_rq_out *out;
	struct mcx_rq_ctx *mbin;
	int error;
	uint64_t *pas;
	uint32_t rq_flags;
	int insize, npages, paslen, token;

	rx->rx_doorbell = MCX_WQ_DOORBELL_BASE +
	    (db * MCX_WQ_DOORBELL_STRIDE);

	npages = howmany((1 << MCX_LOG_RQ_SIZE) * sizeof(struct mcx_rq_entry),
	    MCX_PAGE_SIZE);
	paslen = npages * sizeof(*pas);
	insize = 0x10 + sizeof(struct mcx_rq_ctx) + paslen;

	if (mcx_dmamem_alloc(sc, &rx->rx_rq_mem, npages * MCX_PAGE_SIZE,
	    MCX_PAGE_SIZE) != 0) {
		printf("%s: unable to allocate receive queue memory\n",
		    DEVNAME(sc));
		return (-1);
	}

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + insize, sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_CREATE_RQ);
	in->cmd_op_mod = htobe16(0);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm,
	    howmany(insize, MCX_CMDQ_MAILBOX_DATASIZE),
	    &cqe->cq_input_ptr, token) != 0) {
		printf("%s: unable to allocate create rq mailboxen\n",
		    DEVNAME(sc));
		goto free_rq;
	}
	mbin = (struct mcx_rq_ctx *)
	    (((char *)mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0))) + 0x10);
	rq_flags = MCX_RQ_CTX_RLKEY;
#if NVLAN == 0
	rq_flags |= MCX_RQ_CTX_VLAN_STRIP_DIS;
#endif
	mbin->rq_flags = htobe32(rq_flags);
	mbin->rq_cqn = htobe32(cqn);
	mbin->rq_wq.wq_type = MCX_WQ_CTX_TYPE_CYCLIC;
	mbin->rq_wq.wq_pd = htobe32(sc->sc_pd);
	mbin->rq_wq.wq_doorbell = htobe64(MCX_DMA_DVA(&sc->sc_doorbell_mem) +
	    rx->rx_doorbell);
	mbin->rq_wq.wq_log_stride = htobe16(4);
	mbin->rq_wq.wq_log_size = MCX_LOG_RQ_SIZE;

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&rx->rx_rq_mem),
	    0, MCX_DMA_LEN(&rx->rx_rq_mem), BUS_DMASYNC_PREWRITE);

	/* physical addresses follow the mailbox in data */
	mcx_cmdq_mboxes_pas(&mxm, sizeof(*mbin) + 0x10, npages, &rx->rx_rq_mem);
	mcx_cmdq_post(sc, cqe, 0);

	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: create rq timeout\n", DEVNAME(sc));
		goto free_mxm;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: create rq command corrupt\n", DEVNAME(sc));
		goto free_mxm;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: create rq failed (%x, %x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		goto free_mxm;
	}

	rx->rx_rqn = mcx_get_id(out->cmd_rqn);

	mcx_dmamem_free(sc, &mxm);

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&sc->sc_doorbell_mem),
	    rx->rx_doorbell, sizeof(uint32_t), BUS_DMASYNC_PREWRITE);

	return (0);

free_mxm:
	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&rx->rx_rq_mem),
	    0, MCX_DMA_LEN(&rx->rx_rq_mem), BUS_DMASYNC_POSTWRITE);
	mcx_dmamem_free(sc, &mxm);
free_rq:
	mcx_dmamem_free(sc, &rx->rx_rq_mem);
	return (-1);
}

static int
mcx_ready_rq(struct mcx_softc *sc, struct mcx_rx *rx)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_dmamem mxm;
	struct mcx_cmd_modify_rq_in *in;
	struct mcx_cmd_modify_rq_mb_in *mbin;
	struct mcx_cmd_modify_rq_out *out;
	int error;
	int token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + sizeof(*mbin),
	    sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_MODIFY_RQ);
	in->cmd_op_mod = htobe16(0);
	in->cmd_rq_state = htobe32((MCX_QUEUE_STATE_RST << 28) | rx->rx_rqn);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 1,
	    &cqe->cq_input_ptr, token) != 0) {
		printf("%s: unable to allocate modify rq mailbox\n",
		    DEVNAME(sc));
		return (-1);
	}
	mbin = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	mbin->cmd_rq_ctx.rq_flags = htobe32(
	    MCX_QUEUE_STATE_RDY << MCX_RQ_CTX_STATE_SHIFT);

	mcx_cmdq_mboxes_sign(&mxm, 1);
	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: modify rq timeout\n", DEVNAME(sc));
		goto free;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: modify rq command corrupt\n", DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: modify rq failed (%x, %x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

free:
	mcx_dmamem_free(sc, &mxm);
	return (error);
}

static int
mcx_destroy_rq(struct mcx_softc *sc, struct mcx_rx *rx)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_destroy_rq_in *in;
	struct mcx_cmd_destroy_rq_out *out;
	int error;
	int token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_DESTROY_RQ);
	in->cmd_op_mod = htobe16(0);
	in->cmd_rqn = htobe32(rx->rx_rqn);

	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: destroy rq timeout\n", DEVNAME(sc));
		return error;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: destroy rq command corrupt\n", DEVNAME(sc));
		return error;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: destroy rq failed (%x, %x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		return -1;
	}

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&sc->sc_doorbell_mem),
	    rx->rx_doorbell, sizeof(uint32_t), BUS_DMASYNC_POSTWRITE);

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&rx->rx_rq_mem),
	    0, MCX_DMA_LEN(&rx->rx_rq_mem), BUS_DMASYNC_POSTWRITE);
	mcx_dmamem_free(sc, &rx->rx_rq_mem);

	rx->rx_rqn = 0;
	return 0;
}

static int
mcx_create_tir_direct(struct mcx_softc *sc, struct mcx_rx *rx, int *tirn)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_dmamem mxm;
	struct mcx_cmd_create_tir_in *in;
	struct mcx_cmd_create_tir_mb_in *mbin;
	struct mcx_cmd_create_tir_out *out;
	int error;
	int token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + sizeof(*mbin),
	    sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_CREATE_TIR);
	in->cmd_op_mod = htobe16(0);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 1,
	    &cqe->cq_input_ptr, token) != 0) {
		printf("%s: unable to allocate create tir mailbox\n",
		    DEVNAME(sc));
		return (-1);
	}
	mbin = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	/* leave disp_type = 0, so packets get sent to the inline rqn */
	mbin->cmd_inline_rqn = htobe32(rx->rx_rqn);
	mbin->cmd_tdomain = htobe32(sc->sc_tdomain);

	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: create tir timeout\n", DEVNAME(sc));
		goto free;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: create tir command corrupt\n", DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: create tir failed (%x, %x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

	*tirn = mcx_get_id(out->cmd_tirn);
free:
	mcx_dmamem_free(sc, &mxm);
	return (error);
}

static int
mcx_create_tir_indirect(struct mcx_softc *sc, int rqtn, uint32_t hash_sel,
    int *tirn)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_dmamem mxm;
	struct mcx_cmd_create_tir_in *in;
	struct mcx_cmd_create_tir_mb_in *mbin;
	struct mcx_cmd_create_tir_out *out;
	int error;
	int token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + sizeof(*mbin),
	    sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_CREATE_TIR);
	in->cmd_op_mod = htobe16(0);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 1,
	    &cqe->cq_input_ptr, token) != 0) {
		printf("%s: unable to allocate create tir mailbox\n",
		    DEVNAME(sc));
		return (-1);
	}
	mbin = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	mbin->cmd_disp_type = htobe32(MCX_TIR_CTX_DISP_TYPE_INDIRECT
	    << MCX_TIR_CTX_DISP_TYPE_SHIFT);
	mbin->cmd_indir_table = htobe32(rqtn);
	mbin->cmd_tdomain = htobe32(sc->sc_tdomain |
	    MCX_TIR_CTX_HASH_TOEPLITZ << MCX_TIR_CTX_HASH_SHIFT);
	mbin->cmd_rx_hash_sel_outer = htobe32(hash_sel);
	stoeplitz_to_key(&mbin->cmd_rx_hash_key,
	    sizeof(mbin->cmd_rx_hash_key));

	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: create tir timeout\n", DEVNAME(sc));
		goto free;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: create tir command corrupt\n", DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: create tir failed (%x, %x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

	*tirn = mcx_get_id(out->cmd_tirn);
free:
	mcx_dmamem_free(sc, &mxm);
	return (error);
}

static int
mcx_destroy_tir(struct mcx_softc *sc, int tirn)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_destroy_tir_in *in;
	struct mcx_cmd_destroy_tir_out *out;
	int error;
	int token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_DESTROY_TIR);
	in->cmd_op_mod = htobe16(0);
	in->cmd_tirn = htobe32(tirn);

	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: destroy tir timeout\n", DEVNAME(sc));
		return error;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: destroy tir command corrupt\n", DEVNAME(sc));
		return error;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: destroy tir failed (%x, %x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		return -1;
	}

	return (0);
}

static int
mcx_create_sq(struct mcx_softc *sc, struct mcx_tx *tx, int uar, int db,
    int cqn)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_dmamem mxm;
	struct mcx_cmd_create_sq_in *in;
	struct mcx_sq_ctx *mbin;
	struct mcx_cmd_create_sq_out *out;
	int error;
	uint64_t *pas;
	int insize, npages, paslen, token;

	tx->tx_doorbell = MCX_WQ_DOORBELL_BASE +
	    (db * MCX_WQ_DOORBELL_STRIDE) + 4;

	npages = howmany((1 << MCX_LOG_SQ_SIZE) * sizeof(struct mcx_sq_entry),
	    MCX_PAGE_SIZE);
	paslen = npages * sizeof(*pas);
	insize = sizeof(struct mcx_sq_ctx) + paslen;

	if (mcx_dmamem_alloc(sc, &tx->tx_sq_mem, npages * MCX_PAGE_SIZE,
	    MCX_PAGE_SIZE) != 0) {
		printf("%s: unable to allocate send queue memory\n",
		    DEVNAME(sc));
		return (-1);
	}

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + insize + paslen, sizeof(*out),
	    token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_CREATE_SQ);
	in->cmd_op_mod = htobe16(0);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm,
	    howmany(insize, MCX_CMDQ_MAILBOX_DATASIZE),
	    &cqe->cq_input_ptr, token) != 0) {
		printf("%s: unable to allocate create sq mailboxen\n",
		    DEVNAME(sc));
		goto free_sq;
	}
	mbin = (struct mcx_sq_ctx *)
	    (((char *)mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0))) + 0x10);
	mbin->sq_flags = htobe32(MCX_SQ_CTX_RLKEY |
	    (1 << MCX_SQ_CTX_MIN_WQE_INLINE_SHIFT));
	mbin->sq_cqn = htobe32(cqn);
	mbin->sq_tis_lst_sz = htobe32(1 << MCX_SQ_CTX_TIS_LST_SZ_SHIFT);
	mbin->sq_tis_num = htobe32(sc->sc_tis);
	mbin->sq_wq.wq_type = MCX_WQ_CTX_TYPE_CYCLIC;
	mbin->sq_wq.wq_pd = htobe32(sc->sc_pd);
	mbin->sq_wq.wq_uar_page = htobe32(uar);
	mbin->sq_wq.wq_doorbell = htobe64(MCX_DMA_DVA(&sc->sc_doorbell_mem) +
	    tx->tx_doorbell);
	mbin->sq_wq.wq_log_stride = htobe16(MCX_LOG_SQ_ENTRY_SIZE);
	mbin->sq_wq.wq_log_size = MCX_LOG_SQ_SIZE;

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&tx->tx_sq_mem),
	    0, MCX_DMA_LEN(&tx->tx_sq_mem), BUS_DMASYNC_PREWRITE);

	/* physical addresses follow the mailbox in data */
	mcx_cmdq_mboxes_pas(&mxm, sizeof(*mbin) + 0x10,
	    npages, &tx->tx_sq_mem);
	mcx_cmdq_post(sc, cqe, 0);

	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: create sq timeout\n", DEVNAME(sc));
		goto free_mxm;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: create sq command corrupt\n", DEVNAME(sc));
		goto free_mxm;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: create sq failed (%x, %x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		goto free_mxm;
	}

	tx->tx_uar = uar;
	tx->tx_sqn = mcx_get_id(out->cmd_sqn);

	mcx_dmamem_free(sc, &mxm);

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&sc->sc_doorbell_mem),
	    tx->tx_doorbell, sizeof(uint32_t), BUS_DMASYNC_PREWRITE);

	return (0);

free_mxm:
	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&tx->tx_sq_mem),
	    0, MCX_DMA_LEN(&tx->tx_sq_mem), BUS_DMASYNC_POSTWRITE);
	mcx_dmamem_free(sc, &mxm);
free_sq:
	mcx_dmamem_free(sc, &tx->tx_sq_mem);
	return (-1);
}

static int
mcx_destroy_sq(struct mcx_softc *sc, struct mcx_tx *tx)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_destroy_sq_in *in;
	struct mcx_cmd_destroy_sq_out *out;
	int error;
	int token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_DESTROY_SQ);
	in->cmd_op_mod = htobe16(0);
	in->cmd_sqn = htobe32(tx->tx_sqn);

	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: destroy sq timeout\n", DEVNAME(sc));
		return error;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: destroy sq command corrupt\n", DEVNAME(sc));
		return error;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: destroy sq failed (%x, %x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		return -1;
	}

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&sc->sc_doorbell_mem),
	    tx->tx_doorbell, sizeof(uint32_t), BUS_DMASYNC_POSTWRITE);

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&tx->tx_sq_mem),
	    0, MCX_DMA_LEN(&tx->tx_sq_mem), BUS_DMASYNC_POSTWRITE);
	mcx_dmamem_free(sc, &tx->tx_sq_mem);

	tx->tx_sqn = 0;
	return 0;
}

static int
mcx_ready_sq(struct mcx_softc *sc, struct mcx_tx *tx)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_dmamem mxm;
	struct mcx_cmd_modify_sq_in *in;
	struct mcx_cmd_modify_sq_mb_in *mbin;
	struct mcx_cmd_modify_sq_out *out;
	int error;
	int token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + sizeof(*mbin),
	    sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_MODIFY_SQ);
	in->cmd_op_mod = htobe16(0);
	in->cmd_sq_state = htobe32((MCX_QUEUE_STATE_RST << 28) | tx->tx_sqn);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 1,
	    &cqe->cq_input_ptr, token) != 0) {
		printf("%s: unable to allocate modify sq mailbox\n",
		    DEVNAME(sc));
		return (-1);
	}
	mbin = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	mbin->cmd_sq_ctx.sq_flags = htobe32(
	    MCX_QUEUE_STATE_RDY << MCX_SQ_CTX_STATE_SHIFT);

	mcx_cmdq_mboxes_sign(&mxm, 1);
	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: modify sq timeout\n", DEVNAME(sc));
		goto free;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: modify sq command corrupt\n", DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: modify sq failed (%x, %x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

free:
	mcx_dmamem_free(sc, &mxm);
	return (error);
}

static int
mcx_create_tis(struct mcx_softc *sc, int *tis)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_dmamem mxm;
	struct mcx_cmd_create_tis_in *in;
	struct mcx_cmd_create_tis_mb_in *mbin;
	struct mcx_cmd_create_tis_out *out;
	int error;
	int token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + sizeof(*mbin),
	    sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_CREATE_TIS);
	in->cmd_op_mod = htobe16(0);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 1,
	    &cqe->cq_input_ptr, token) != 0) {
		printf("%s: unable to allocate create tis mailbox\n",
		    DEVNAME(sc));
		return (-1);
	}
	mbin = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	mbin->cmd_tdomain = htobe32(sc->sc_tdomain);

	mcx_cmdq_mboxes_sign(&mxm, 1);
	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: create tis timeout\n", DEVNAME(sc));
		goto free;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: create tis command corrupt\n", DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: create tis failed (%x, %x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

	*tis = mcx_get_id(out->cmd_tisn);
free:
	mcx_dmamem_free(sc, &mxm);
	return (error);
}

static int
mcx_destroy_tis(struct mcx_softc *sc, int tis)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_destroy_tis_in *in;
	struct mcx_cmd_destroy_tis_out *out;
	int error;
	int token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_DESTROY_TIS);
	in->cmd_op_mod = htobe16(0);
	in->cmd_tisn = htobe32(tis);

	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: destroy tis timeout\n", DEVNAME(sc));
		return error;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: destroy tis command corrupt\n", DEVNAME(sc));
		return error;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: destroy tis failed (%x, %x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		return -1;
	}

	return 0;
}

static int
mcx_create_rqt(struct mcx_softc *sc, int size, int *rqns, int *rqt)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_dmamem mxm;
	struct mcx_cmd_create_rqt_in *in;
	struct mcx_cmd_create_rqt_mb_in *mbin;
	struct mcx_cmd_create_rqt_out *out;
	struct mcx_rqt_ctx *rqt_ctx;
	int *rqtn;
	int error;
	int token;
	int i;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + sizeof(*mbin) +
	    (size * sizeof(int)), sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_CREATE_RQT);
	in->cmd_op_mod = htobe16(0);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 1,
	    &cqe->cq_input_ptr, token) != 0) {
		printf("%s: unable to allocate create rqt mailbox\n",
		    DEVNAME(sc));
		return (-1);
	}
	mbin = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	rqt_ctx = &mbin->cmd_rqt;
	rqt_ctx->cmd_rqt_max_size = htobe16(sc->sc_max_rqt_size);
	rqt_ctx->cmd_rqt_actual_size = htobe16(size);

	/* rqt list follows the rqt context */
	rqtn = (int *)(rqt_ctx + 1);
	for (i = 0; i < size; i++) {
		rqtn[i] = htobe32(rqns[i]);
	}

	mcx_cmdq_mboxes_sign(&mxm, 1);
	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: create rqt timeout\n", DEVNAME(sc));
		goto free;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: create rqt command corrupt\n", DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: create rqt failed (%x, %x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

	*rqt = mcx_get_id(out->cmd_rqtn);
	return (0);
free:
	mcx_dmamem_free(sc, &mxm);
	return (error);
}

static int
mcx_destroy_rqt(struct mcx_softc *sc, int rqt)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_destroy_rqt_in *in;
	struct mcx_cmd_destroy_rqt_out *out;
	int error;
	int token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_DESTROY_RQT);
	in->cmd_op_mod = htobe16(0);
	in->cmd_rqtn = htobe32(rqt);

	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: destroy rqt timeout\n", DEVNAME(sc));
		return error;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: destroy rqt command corrupt\n", DEVNAME(sc));
		return error;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: destroy rqt failed (%x, %x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		return -1;
	}

	return 0;
}

#if 0
static int
mcx_alloc_flow_counter(struct mcx_softc *sc, int i)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_alloc_flow_counter_in *in;
	struct mcx_cmd_alloc_flow_counter_out *out;
	int error;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out), mcx_cmdq_token(sc));

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_ALLOC_FLOW_COUNTER);
	in->cmd_op_mod = htobe16(0);

	mcx_cmdq_post(sc, cqe, 0);

	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: alloc flow counter timeout\n", DEVNAME(sc));
		return (-1);
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: alloc flow counter command corrupt\n", DEVNAME(sc));
		return (-1);
	}

	out = (struct mcx_cmd_alloc_flow_counter_out *)cqe->cq_output_data;
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: alloc flow counter failed (%x)\n", DEVNAME(sc),
		    out->cmd_status);
		return (-1);
	}

	sc->sc_flow_counter_id[i]  = betoh16(out->cmd_flow_counter_id);
	printf("flow counter id %d = %d\n", i, sc->sc_flow_counter_id[i]);

	return (0);
}
#endif

static int
mcx_create_flow_table(struct mcx_softc *sc, int log_size, int level,
    int *flow_table_id)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_dmamem mxm;
	struct mcx_cmd_create_flow_table_in *in;
	struct mcx_cmd_create_flow_table_mb_in *mbin;
	struct mcx_cmd_create_flow_table_out *out;
	int error;
	int token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + sizeof(*mbin),
	    sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_CREATE_FLOW_TABLE);
	in->cmd_op_mod = htobe16(0);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 1,
	    &cqe->cq_input_ptr, token) != 0) {
		printf("%s: unable to allocate create flow table mailbox\n",
		    DEVNAME(sc));
		return (-1);
	}
	mbin = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	mbin->cmd_table_type = MCX_FLOW_TABLE_TYPE_RX;
	mbin->cmd_ctx.ft_log_size = log_size;
	mbin->cmd_ctx.ft_level = level;

	mcx_cmdq_mboxes_sign(&mxm, 1);
	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: create flow table timeout\n", DEVNAME(sc));
		goto free;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: create flow table command corrupt\n", DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: create flow table failed (%x, %x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

	*flow_table_id = mcx_get_id(out->cmd_table_id);
free:
	mcx_dmamem_free(sc, &mxm);
	return (error);
}

static int
mcx_set_flow_table_root(struct mcx_softc *sc, int flow_table_id)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_dmamem mxm;
	struct mcx_cmd_set_flow_table_root_in *in;
	struct mcx_cmd_set_flow_table_root_mb_in *mbin;
	struct mcx_cmd_set_flow_table_root_out *out;
	int error;
	int token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + sizeof(*mbin),
	    sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_SET_FLOW_TABLE_ROOT);
	in->cmd_op_mod = htobe16(0);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 1,
	    &cqe->cq_input_ptr, token) != 0) {
		printf("%s: unable to allocate set flow table root mailbox\n",
		    DEVNAME(sc));
		return (-1);
	}
	mbin = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	mbin->cmd_table_type = MCX_FLOW_TABLE_TYPE_RX;
	mbin->cmd_table_id = htobe32(flow_table_id);

	mcx_cmdq_mboxes_sign(&mxm, 1);
	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: set flow table root timeout\n", DEVNAME(sc));
		goto free;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: set flow table root command corrupt\n",
		    DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: set flow table root failed (%x, %x)\n",
		    DEVNAME(sc), out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

free:
	mcx_dmamem_free(sc, &mxm);
	return (error);
}

static int
mcx_destroy_flow_table(struct mcx_softc *sc, int flow_table_id)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_dmamem mxm;
	struct mcx_cmd_destroy_flow_table_in *in;
	struct mcx_cmd_destroy_flow_table_mb_in *mb;
	struct mcx_cmd_destroy_flow_table_out *out;
	int error;
	int token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + sizeof(*mb), sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_DESTROY_FLOW_TABLE);
	in->cmd_op_mod = htobe16(0);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 1,
	    &cqe->cq_input_ptr, token) != 0) {
		printf("%s: unable to allocate destroy flow table mailbox\n",
		    DEVNAME(sc));
		return (-1);
	}
	mb = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	mb->cmd_table_type = MCX_FLOW_TABLE_TYPE_RX;
	mb->cmd_table_id = htobe32(flow_table_id);

	mcx_cmdq_mboxes_sign(&mxm, 1);
	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: destroy flow table timeout\n", DEVNAME(sc));
		goto free;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: destroy flow table command corrupt\n",
		    DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: destroy flow table failed (%x, %x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

free:
	mcx_dmamem_free(sc, &mxm);
	return (error);
}


static int
mcx_create_flow_group(struct mcx_softc *sc, int flow_table_id, int group,
    int start, int size, int match_enable, struct mcx_flow_match *match)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_dmamem mxm;
	struct mcx_cmd_create_flow_group_in *in;
	struct mcx_cmd_create_flow_group_mb_in *mbin;
	struct mcx_cmd_create_flow_group_out *out;
	struct mcx_flow_group *mfg;
	int error;
	int token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + sizeof(*mbin), sizeof(*out),
	    token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_CREATE_FLOW_GROUP);
	in->cmd_op_mod = htobe16(0);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 2, &cqe->cq_input_ptr, token)
	    != 0) {
		printf("%s: unable to allocate create flow group mailbox\n",
		    DEVNAME(sc));
		return (-1);
	}
	mbin = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	mbin->cmd_table_type = MCX_FLOW_TABLE_TYPE_RX;
	mbin->cmd_table_id = htobe32(flow_table_id);
	mbin->cmd_start_flow_index = htobe32(start);
	mbin->cmd_end_flow_index = htobe32(start + (size - 1));

	mbin->cmd_match_criteria_enable = match_enable;
	memcpy(&mbin->cmd_match_criteria, match, sizeof(*match));

	mcx_cmdq_mboxes_sign(&mxm, 2);
	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: create flow group timeout\n", DEVNAME(sc));
		goto free;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: create flow group command corrupt\n", DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: create flow group failed (%x, %x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

	mfg = &sc->sc_flow_group[group];
	mfg->g_id = mcx_get_id(out->cmd_group_id);
	mfg->g_table = flow_table_id;
	mfg->g_start = start;
	mfg->g_size = size;

free:
	mcx_dmamem_free(sc, &mxm);
	return (error);
}

static int
mcx_destroy_flow_group(struct mcx_softc *sc, int group)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_dmamem mxm;
	struct mcx_cmd_destroy_flow_group_in *in;
	struct mcx_cmd_destroy_flow_group_mb_in *mb;
	struct mcx_cmd_destroy_flow_group_out *out;
	struct mcx_flow_group *mfg;
	int error;
	int token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + sizeof(*mb), sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_DESTROY_FLOW_GROUP);
	in->cmd_op_mod = htobe16(0);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 2,
	    &cqe->cq_input_ptr, token) != 0) {
		printf("%s: unable to allocate destroy flow group mailbox\n",
		    DEVNAME(sc));
		return (-1);
	}
	mb = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	mb->cmd_table_type = MCX_FLOW_TABLE_TYPE_RX;
	mfg = &sc->sc_flow_group[group];
	mb->cmd_table_id = htobe32(mfg->g_table);
	mb->cmd_group_id = htobe32(mfg->g_id);

	mcx_cmdq_mboxes_sign(&mxm, 2);
	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: destroy flow group timeout\n", DEVNAME(sc));
		goto free;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: destroy flow group command corrupt\n", DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: destroy flow group failed (%x, %x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

	mfg->g_id = -1;
	mfg->g_table = -1;
	mfg->g_size = 0;
	mfg->g_start = 0;
free:
	mcx_dmamem_free(sc, &mxm);
	return (error);
}

static int
mcx_set_flow_table_entry_mac(struct mcx_softc *sc, int group, int index,
    uint8_t *macaddr, uint32_t dest)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_dmamem mxm;
	struct mcx_cmd_set_flow_table_entry_in *in;
	struct mcx_cmd_set_flow_table_entry_mb_in *mbin;
	struct mcx_cmd_set_flow_table_entry_out *out;
	struct mcx_flow_group *mfg;
	uint32_t *pdest;
	int error;
	int token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + sizeof(*mbin) + sizeof(*pdest),
	    sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_SET_FLOW_TABLE_ENTRY);
	in->cmd_op_mod = htobe16(0);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 2, &cqe->cq_input_ptr, token)
	    != 0) {
		printf("%s: unable to allocate set flow table entry mailbox\n",
		    DEVNAME(sc));
		return (-1);
	}

	mbin = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	mbin->cmd_table_type = MCX_FLOW_TABLE_TYPE_RX;

	mfg = &sc->sc_flow_group[group];
	mbin->cmd_table_id = htobe32(mfg->g_table);
	mbin->cmd_flow_index = htobe32(mfg->g_start + index);
	mbin->cmd_flow_ctx.fc_group_id = htobe32(mfg->g_id);

	/* flow context ends at offset 0x330, 0x130 into the second mbox */
	pdest = (uint32_t *)
	    (((char *)mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 1))) + 0x130);
	mbin->cmd_flow_ctx.fc_action = htobe32(MCX_FLOW_CONTEXT_ACTION_FORWARD);
	mbin->cmd_flow_ctx.fc_dest_list_size = htobe32(1);
	*pdest = htobe32(dest);

	/* the only thing we match on at the moment is the dest mac address */
	if (macaddr != NULL) {
		memcpy(mbin->cmd_flow_ctx.fc_match_value.mc_dest_mac, macaddr,
		    ETHER_ADDR_LEN);
	}

	mcx_cmdq_mboxes_sign(&mxm, 2);
	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: set flow table entry timeout\n", DEVNAME(sc));
		goto free;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: set flow table entry command corrupt\n",
		    DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: set flow table entry failed (%x, %x)\n",
		    DEVNAME(sc), out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

free:
	mcx_dmamem_free(sc, &mxm);
	return (error);
}

static int
mcx_set_flow_table_entry_proto(struct mcx_softc *sc, int group, int index,
    int ethertype, int ip_proto, uint32_t dest)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_dmamem mxm;
	struct mcx_cmd_set_flow_table_entry_in *in;
	struct mcx_cmd_set_flow_table_entry_mb_in *mbin;
	struct mcx_cmd_set_flow_table_entry_out *out;
	struct mcx_flow_group *mfg;
	uint32_t *pdest;
	int error;
	int token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + sizeof(*mbin) + sizeof(*pdest),
	    sizeof(*out), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_SET_FLOW_TABLE_ENTRY);
	in->cmd_op_mod = htobe16(0);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 2, &cqe->cq_input_ptr, token)
	    != 0) {
		printf("%s: unable to allocate set flow table entry mailbox\n",
		    DEVNAME(sc));
		return (-1);
	}

	mbin = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	mbin->cmd_table_type = MCX_FLOW_TABLE_TYPE_RX;

	mfg = &sc->sc_flow_group[group];
	mbin->cmd_table_id = htobe32(mfg->g_table);
	mbin->cmd_flow_index = htobe32(mfg->g_start + index);
	mbin->cmd_flow_ctx.fc_group_id = htobe32(mfg->g_id);

	/* flow context ends at offset 0x330, 0x130 into the second mbox */
	pdest = (uint32_t *)
	    (((char *)mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 1))) + 0x130);
	mbin->cmd_flow_ctx.fc_action = htobe32(MCX_FLOW_CONTEXT_ACTION_FORWARD);
	mbin->cmd_flow_ctx.fc_dest_list_size = htobe32(1);
	*pdest = htobe32(dest);

	mbin->cmd_flow_ctx.fc_match_value.mc_ethertype = htobe16(ethertype);
	mbin->cmd_flow_ctx.fc_match_value.mc_ip_proto = ip_proto;

	mcx_cmdq_mboxes_sign(&mxm, 2);
	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: set flow table entry timeout\n", DEVNAME(sc));
		goto free;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: set flow table entry command corrupt\n",
		    DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: set flow table entry failed (%x, %x)\n",
		    DEVNAME(sc), out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

free:
	mcx_dmamem_free(sc, &mxm);
	return (error);
}

static int
mcx_delete_flow_table_entry(struct mcx_softc *sc, int group, int index)
{
	struct mcx_cmdq_entry *cqe;
	struct mcx_dmamem mxm;
	struct mcx_cmd_delete_flow_table_entry_in *in;
	struct mcx_cmd_delete_flow_table_entry_mb_in *mbin;
	struct mcx_cmd_delete_flow_table_entry_out *out;
	struct mcx_flow_group *mfg;
	int error;
	int token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + sizeof(*mbin), sizeof(*out),
	    token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_DELETE_FLOW_TABLE_ENTRY);
	in->cmd_op_mod = htobe16(0);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 2,
	    &cqe->cq_input_ptr, token) != 0) {
		printf("%s: unable to allocate "
		    "delete flow table entry mailbox\n", DEVNAME(sc));
		return (-1);
	}
	mbin = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	mbin->cmd_table_type = MCX_FLOW_TABLE_TYPE_RX;

	mfg = &sc->sc_flow_group[group];
	mbin->cmd_table_id = htobe32(mfg->g_table);
	mbin->cmd_flow_index = htobe32(mfg->g_start + index);

	mcx_cmdq_mboxes_sign(&mxm, 2);
	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: delete flow table entry timeout\n", DEVNAME(sc));
		goto free;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: delete flow table entry command corrupt\n",
		    DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: delete flow table entry %d:%d failed (%x, %x)\n",
		    DEVNAME(sc), group, index, out->cmd_status,
		    betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

free:
	mcx_dmamem_free(sc, &mxm);
	return (error);
}

#if 0
int
mcx_dump_flow_table(struct mcx_softc *sc, int flow_table_id)
{
	struct mcx_dmamem mxm;
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_query_flow_table_in *in;
	struct mcx_cmd_query_flow_table_mb_in *mbin;
	struct mcx_cmd_query_flow_table_out *out;
	struct mcx_cmd_query_flow_table_mb_out *mbout;
	uint8_t token = mcx_cmdq_token(sc);
	int error;
	int i;
	uint8_t *dump;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + sizeof(*mbin),
	    sizeof(*out) + sizeof(*mbout) + 16, token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_QUERY_FLOW_TABLE);
	in->cmd_op_mod = htobe16(0);

	CTASSERT(sizeof(*mbin) <= MCX_CMDQ_MAILBOX_DATASIZE);
	CTASSERT(sizeof(*mbout) <= MCX_CMDQ_MAILBOX_DATASIZE);
	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 2,
	    &cqe->cq_output_ptr, token) != 0) {
		printf(", unable to allocate query flow table mailboxes\n");
		return (-1);
	}
	cqe->cq_input_ptr = cqe->cq_output_ptr;

	mbin = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	mbin->cmd_table_type = 0;
	mbin->cmd_table_id = htobe32(flow_table_id);

	mcx_cmdq_mboxes_sign(&mxm, 1);

	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: query flow table timeout\n", DEVNAME(sc));
		goto free;
	}
	error = mcx_cmdq_verify(cqe);
	if (error != 0) {
		printf("%s: query flow table reply corrupt\n", DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	switch (out->cmd_status) {
	case MCX_CQ_STATUS_OK:
		break;
	default:
		printf("%s: query flow table failed (%x/%x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

        mbout = (struct mcx_cmd_query_flow_table_mb_out *)
	    (mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0)));
	dump = (uint8_t *)mbout + 8;
	for (i = 0; i < sizeof(struct mcx_flow_table_ctx); i++) {
		printf("%.2x ", dump[i]);
		if (i % 16 == 15)
			printf("\n");
	}
free:
	mcx_cq_mboxes_free(sc, &mxm);
	return (error);
}
int
mcx_dump_flow_table_entry(struct mcx_softc *sc, int flow_table_id, int index)
{
	struct mcx_dmamem mxm;
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_query_flow_table_entry_in *in;
	struct mcx_cmd_query_flow_table_entry_mb_in *mbin;
	struct mcx_cmd_query_flow_table_entry_out *out;
	struct mcx_cmd_query_flow_table_entry_mb_out *mbout;
	uint8_t token = mcx_cmdq_token(sc);
	int error;
	int i;
	uint8_t *dump;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + sizeof(*mbin),
	    sizeof(*out) + sizeof(*mbout) + 16, token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_QUERY_FLOW_TABLE_ENTRY);
	in->cmd_op_mod = htobe16(0);

	CTASSERT(sizeof(*mbin) <= MCX_CMDQ_MAILBOX_DATASIZE);
	CTASSERT(sizeof(*mbout) <= MCX_CMDQ_MAILBOX_DATASIZE*2);
	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 2,
	    &cqe->cq_output_ptr, token) != 0) {
		printf(", unable to allocate "
		    "query flow table entry mailboxes\n");
		return (-1);
	}
	cqe->cq_input_ptr = cqe->cq_output_ptr;

	mbin = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	mbin->cmd_table_type = 0;
	mbin->cmd_table_id = htobe32(flow_table_id);
	mbin->cmd_flow_index = htobe32(index);

	mcx_cmdq_mboxes_sign(&mxm, 1);

	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: query flow table entry timeout\n", DEVNAME(sc));
		goto free;
	}
	error = mcx_cmdq_verify(cqe);
	if (error != 0) {
		printf("%s: query flow table entry reply corrupt\n",
		    DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	switch (out->cmd_status) {
	case MCX_CQ_STATUS_OK:
		break;
	default:
		printf("%s: query flow table entry failed (%x/%x)\n",
		    DEVNAME(sc), out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

        mbout = (struct mcx_cmd_query_flow_table_entry_mb_out *)
	    (mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0)));
	dump = (uint8_t *)mbout;
	for (i = 0; i < MCX_CMDQ_MAILBOX_DATASIZE; i++) {
		printf("%.2x ", dump[i]);
		if (i % 16 == 15)
			printf("\n");
	}

free:
	mcx_cq_mboxes_free(sc, &mxm);
	return (error);
}

int
mcx_dump_flow_group(struct mcx_softc *sc, int flow_table_id)
{
	struct mcx_dmamem mxm;
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_query_flow_group_in *in;
	struct mcx_cmd_query_flow_group_mb_in *mbin;
	struct mcx_cmd_query_flow_group_out *out;
	struct mcx_cmd_query_flow_group_mb_out *mbout;
	uint8_t token = mcx_cmdq_token(sc);
	int error;
	int i;
	uint8_t *dump;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + sizeof(*mbin),
	    sizeof(*out) + sizeof(*mbout) + 16, token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_QUERY_FLOW_GROUP);
	in->cmd_op_mod = htobe16(0);

	CTASSERT(sizeof(*mbin) <= MCX_CMDQ_MAILBOX_DATASIZE);
	CTASSERT(sizeof(*mbout) <= MCX_CMDQ_MAILBOX_DATASIZE*2);
	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 2,
	    &cqe->cq_output_ptr, token) != 0) {
		printf(", unable to allocate query flow group mailboxes\n");
		return (-1);
	}
	cqe->cq_input_ptr = cqe->cq_output_ptr;

	mbin = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	mbin->cmd_table_type = 0;
	mbin->cmd_table_id = htobe32(flow_table_id);
	mbin->cmd_group_id = htobe32(sc->sc_flow_group_id);

	mcx_cmdq_mboxes_sign(&mxm, 1);

	mcx_cmdq_post(sc, cqe, 0);
	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: query flow group timeout\n", DEVNAME(sc));
		goto free;
	}
	error = mcx_cmdq_verify(cqe);
	if (error != 0) {
		printf("%s: query flow group reply corrupt\n", DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	switch (out->cmd_status) {
	case MCX_CQ_STATUS_OK:
		break;
	default:
		printf("%s: query flow group failed (%x/%x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

        mbout = (struct mcx_cmd_query_flow_group_mb_out *)
	    (mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0)));
	dump = (uint8_t *)mbout;
	for (i = 0; i < MCX_CMDQ_MAILBOX_DATASIZE; i++) {
		printf("%.2x ", dump[i]);
		if (i % 16 == 15)
			printf("\n");
	}
	dump = (uint8_t *)(mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 1)));
	for (i = 0; i < MCX_CMDQ_MAILBOX_DATASIZE; i++) {
		printf("%.2x ", dump[i]);
		if (i % 16 == 15)
			printf("\n");
	}

free:
	mcx_cq_mboxes_free(sc, &mxm);
	return (error);
}

static int
mcx_dump_counters(struct mcx_softc *sc)
{
	struct mcx_dmamem mxm;
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_query_vport_counters_in *in;
	struct mcx_cmd_query_vport_counters_mb_in *mbin;
	struct mcx_cmd_query_vport_counters_out *out;
	struct mcx_nic_vport_counters *counters;
	int error, token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + sizeof(*mbin),
	    sizeof(*out) + sizeof(*counters), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_QUERY_VPORT_COUNTERS);
	in->cmd_op_mod = htobe16(0);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 1,
	    &cqe->cq_output_ptr, token) != 0) {
		printf(", unable to allocate "
		    "query nic vport counters mailboxen\n");
		return (-1);
	}
	cqe->cq_input_ptr = cqe->cq_output_ptr;

	mbin = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	mbin->cmd_clear = 0x80;

	mcx_cmdq_mboxes_sign(&mxm, 1);
	mcx_cmdq_post(sc, cqe, 0);

	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: query nic vport counters timeout\n", DEVNAME(sc));
		goto free;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: query nic vport counters command corrupt\n",
		    DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: query nic vport counters failed (%x, %x)\n",
		    DEVNAME(sc), out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

	counters = (struct mcx_nic_vport_counters *)
	    (mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0)));
	if (counters->rx_bcast.packets + counters->tx_bcast.packets +
	    counters->rx_ucast.packets + counters->tx_ucast.packets +
	    counters->rx_err.packets + counters->tx_err.packets)
		printf("%s: err %llx/%llx uc %llx/%llx bc %llx/%llx\n",
		    DEVNAME(sc),
		    betoh64(counters->tx_err.packets),
		    betoh64(counters->rx_err.packets),
		    betoh64(counters->tx_ucast.packets),
		    betoh64(counters->rx_ucast.packets),
		    betoh64(counters->tx_bcast.packets),
		    betoh64(counters->rx_bcast.packets));
free:
	mcx_dmamem_free(sc, &mxm);

	return (error);
}

static int
mcx_dump_flow_counter(struct mcx_softc *sc, int index, const char *what)
{
	struct mcx_dmamem mxm;
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_query_flow_counter_in *in;
	struct mcx_cmd_query_flow_counter_mb_in *mbin;
	struct mcx_cmd_query_flow_counter_out *out;
	struct mcx_counter *counters;
	int error, token;

	cqe = MCX_DMA_KVA(&sc->sc_cmdq_mem);
	token = mcx_cmdq_token(sc);
	mcx_cmdq_init(sc, cqe, sizeof(*in) + sizeof(*mbin), sizeof(*out) +
	    sizeof(*counters), token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_QUERY_FLOW_COUNTER);
	in->cmd_op_mod = htobe16(0);

	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 1,
	    &cqe->cq_output_ptr, token) != 0) {
		printf(", unable to allocate query flow counter mailboxen\n");
		return (-1);
	}
	cqe->cq_input_ptr = cqe->cq_output_ptr;
	mbin = mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0));
	mbin->cmd_flow_counter_id = htobe16(sc->sc_flow_counter_id[index]);
	mbin->cmd_clear = 0x80;

	mcx_cmdq_mboxes_sign(&mxm, 1);
	mcx_cmdq_post(sc, cqe, 0);

	error = mcx_cmdq_poll(sc, cqe, 1000);
	if (error != 0) {
		printf("%s: query flow counter timeout\n", DEVNAME(sc));
		goto free;
	}
	if (mcx_cmdq_verify(cqe) != 0) {
		printf("%s: query flow counter command corrupt\n", DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	if (out->cmd_status != MCX_CQ_STATUS_OK) {
		printf("%s: query flow counter failed (%x, %x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

	counters = (struct mcx_counter *)
	    (mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0)));
	if (counters->packets)
		printf("%s: %s inflow %llx\n", DEVNAME(sc), what,
		    betoh64(counters->packets));
free:
	mcx_dmamem_free(sc, &mxm);

	return (error);
}

#endif

#if NKSTAT > 0

int
mcx_query_rq(struct mcx_softc *sc, struct mcx_rx *rx, struct mcx_rq_ctx *rq_ctx)
{
	struct mcx_dmamem mxm;
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_query_rq_in *in;
	struct mcx_cmd_query_rq_out *out;
	struct mcx_cmd_query_rq_mb_out *mbout;
	uint8_t token = mcx_cmdq_token(sc);
	int error;

	cqe = mcx_get_cmdq_entry(sc, MCX_CMDQ_SLOT_KSTAT);
	if (cqe == NULL)
		return (-1);

	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out) + sizeof(*mbout) + 16,
	    token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_QUERY_RQ);
	in->cmd_op_mod = htobe16(0);
	in->cmd_rqn = htobe32(rx->rx_rqn);

	CTASSERT(sizeof(*mbout) <= MCX_CMDQ_MAILBOX_DATASIZE*2);
	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 2,
	    &cqe->cq_output_ptr, token) != 0) {
		printf("%s: unable to allocate query rq mailboxes\n", DEVNAME(sc));
		return (-1);
	}

	mcx_cmdq_mboxes_sign(&mxm, 1);

	error = mcx_cmdq_exec(sc, cqe, MCX_CMDQ_SLOT_KSTAT, 1000);
	if (error != 0) {
		printf("%s: query rq timeout\n", DEVNAME(sc));
		goto free;
	}
	error = mcx_cmdq_verify(cqe);
	if (error != 0) {
		printf("%s: query rq reply corrupt\n", DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	switch (out->cmd_status) {
	case MCX_CQ_STATUS_OK:
		break;
	default:
		printf("%s: query rq failed (%x/%x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

        mbout = (struct mcx_cmd_query_rq_mb_out *)
	    (mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0)));
	memcpy(rq_ctx, &mbout->cmd_ctx, sizeof(*rq_ctx));

free:
	mcx_cq_mboxes_free(sc, &mxm);
	return (error);
}

int
mcx_query_sq(struct mcx_softc *sc, struct mcx_tx *tx, struct mcx_sq_ctx *sq_ctx)
{
	struct mcx_dmamem mxm;
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_query_sq_in *in;
	struct mcx_cmd_query_sq_out *out;
	struct mcx_cmd_query_sq_mb_out *mbout;
	uint8_t token = mcx_cmdq_token(sc);
	int error;

	cqe = mcx_get_cmdq_entry(sc, MCX_CMDQ_SLOT_KSTAT);
	if (cqe == NULL)
		return (-1);

	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out) + sizeof(*mbout) + 16,
	    token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_QUERY_SQ);
	in->cmd_op_mod = htobe16(0);
	in->cmd_sqn = htobe32(tx->tx_sqn);

	CTASSERT(sizeof(*mbout) <= MCX_CMDQ_MAILBOX_DATASIZE*2);
	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 2,
	    &cqe->cq_output_ptr, token) != 0) {
		printf("%s: unable to allocate query sq mailboxes\n", DEVNAME(sc));
		return (-1);
	}

	mcx_cmdq_mboxes_sign(&mxm, 1);

	error = mcx_cmdq_exec(sc, cqe, MCX_CMDQ_SLOT_KSTAT, 1000);
	if (error != 0) {
		printf("%s: query sq timeout\n", DEVNAME(sc));
		goto free;
	}
	error = mcx_cmdq_verify(cqe);
	if (error != 0) {
		printf("%s: query sq reply corrupt\n", DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	switch (out->cmd_status) {
	case MCX_CQ_STATUS_OK:
		break;
	default:
		printf("%s: query sq failed (%x/%x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

        mbout = (struct mcx_cmd_query_sq_mb_out *)
	    (mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0)));
	memcpy(sq_ctx, &mbout->cmd_ctx, sizeof(*sq_ctx));

free:
	mcx_cq_mboxes_free(sc, &mxm);
	return (error);
}

int
mcx_query_cq(struct mcx_softc *sc, struct mcx_cq *cq, struct mcx_cq_ctx *cq_ctx)
{
	struct mcx_dmamem mxm;
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_query_cq_in *in;
	struct mcx_cmd_query_cq_out *out;
	struct mcx_cq_ctx *ctx;
	uint8_t token = mcx_cmdq_token(sc);
	int error;

	cqe = mcx_get_cmdq_entry(sc, MCX_CMDQ_SLOT_KSTAT);
	if (cqe == NULL)
		return (-1);

	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out) + sizeof(*ctx) + 16,
	    token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_QUERY_CQ);
	in->cmd_op_mod = htobe16(0);
	in->cmd_cqn = htobe32(cq->cq_n);

	CTASSERT(sizeof(*ctx) <= MCX_CMDQ_MAILBOX_DATASIZE*2);
	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 2,
	    &cqe->cq_output_ptr, token) != 0) {
		printf("%s: unable to allocate query cq mailboxes\n", DEVNAME(sc));
		return (-1);
	}

	mcx_cmdq_mboxes_sign(&mxm, 1);

	error = mcx_cmdq_exec(sc, cqe, MCX_CMDQ_SLOT_KSTAT, 1000);
	if (error != 0) {
		printf("%s: query cq timeout\n", DEVNAME(sc));
		goto free;
	}
	error = mcx_cmdq_verify(cqe);
	if (error != 0) {
		printf("%s: query cq reply corrupt\n", DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	switch (out->cmd_status) {
	case MCX_CQ_STATUS_OK:
		break;
	default:
		printf("%s: query cq failed (%x/%x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

        ctx = (struct mcx_cq_ctx *)(mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0)));
	memcpy(cq_ctx, ctx, sizeof(*cq_ctx));
free:
	mcx_cq_mboxes_free(sc, &mxm);
	return (error);
}

int
mcx_query_eq(struct mcx_softc *sc, struct mcx_eq *eq, struct mcx_eq_ctx *eq_ctx)
{
	struct mcx_dmamem mxm;
	struct mcx_cmdq_entry *cqe;
	struct mcx_cmd_query_eq_in *in;
	struct mcx_cmd_query_eq_out *out;
	struct mcx_eq_ctx *ctx;
	uint8_t token = mcx_cmdq_token(sc);
	int error;

	cqe = mcx_get_cmdq_entry(sc, MCX_CMDQ_SLOT_KSTAT);
	if (cqe == NULL)
		return (-1);

	mcx_cmdq_init(sc, cqe, sizeof(*in), sizeof(*out) + sizeof(*ctx) + 16,
	    token);

	in = mcx_cmdq_in(cqe);
	in->cmd_opcode = htobe16(MCX_CMD_QUERY_EQ);
	in->cmd_op_mod = htobe16(0);
	in->cmd_eqn = htobe32(eq->eq_n);

	CTASSERT(sizeof(*ctx) <= MCX_CMDQ_MAILBOX_DATASIZE*2);
	if (mcx_cmdq_mboxes_alloc(sc, &mxm, 2,
	    &cqe->cq_output_ptr, token) != 0) {
		printf("%s: unable to allocate query eq mailboxes\n", DEVNAME(sc));
		return (-1);
	}

	mcx_cmdq_mboxes_sign(&mxm, 1);

	error = mcx_cmdq_exec(sc, cqe, MCX_CMDQ_SLOT_KSTAT, 1000);
	if (error != 0) {
		printf("%s: query eq timeout\n", DEVNAME(sc));
		goto free;
	}
	error = mcx_cmdq_verify(cqe);
	if (error != 0) {
		printf("%s: query eq reply corrupt\n", DEVNAME(sc));
		goto free;
	}

	out = mcx_cmdq_out(cqe);
	switch (out->cmd_status) {
	case MCX_CQ_STATUS_OK:
		break;
	default:
		printf("%s: query eq failed (%x/%x)\n", DEVNAME(sc),
		    out->cmd_status, betoh32(out->cmd_syndrome));
		error = -1;
		goto free;
	}

        ctx = (struct mcx_eq_ctx *)(mcx_cq_mbox_data(mcx_cq_mbox(&mxm, 0)));
	memcpy(eq_ctx, ctx, sizeof(*eq_ctx));
free:
	mcx_cq_mboxes_free(sc, &mxm);
	return (error);
}

#endif /* NKSTAT > 0 */

static inline unsigned int
mcx_rx_fill_slots(struct mcx_softc *sc, struct mcx_rx *rx, uint nslots)
{
	struct mcx_rq_entry *ring, *rqe;
	struct mcx_slot *ms;
	struct mbuf *m;
	uint slot, p, fills;

	ring = MCX_DMA_KVA(&rx->rx_rq_mem);
	p = rx->rx_prod;

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&rx->rx_rq_mem),
	    0, MCX_DMA_LEN(&rx->rx_rq_mem), BUS_DMASYNC_POSTWRITE);

	for (fills = 0; fills < nslots; fills++) {
		slot = p % (1 << MCX_LOG_RQ_SIZE);

		ms = &rx->rx_slots[slot];
		rqe = &ring[slot];

		m = MCLGETL(NULL, M_DONTWAIT, sc->sc_rxbufsz);
		if (m == NULL)
			break;

		m->m_data += (m->m_ext.ext_size - sc->sc_rxbufsz);
		m->m_data += ETHER_ALIGN;
		m->m_len = m->m_pkthdr.len = sc->sc_hardmtu;

		if (bus_dmamap_load_mbuf(sc->sc_dmat, ms->ms_map, m,
		    BUS_DMA_NOWAIT) != 0) {
			m_freem(m);
			break;
		}
		ms->ms_m = m;

		htobem32(&rqe->rqe_byte_count, ms->ms_map->dm_segs[0].ds_len);
		htobem64(&rqe->rqe_addr, ms->ms_map->dm_segs[0].ds_addr);
		htobem32(&rqe->rqe_lkey, sc->sc_lkey);

		p++;
	}

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&rx->rx_rq_mem),
	    0, MCX_DMA_LEN(&rx->rx_rq_mem), BUS_DMASYNC_PREWRITE);

	rx->rx_prod = p;

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&sc->sc_doorbell_mem),
	    rx->rx_doorbell, sizeof(uint32_t), BUS_DMASYNC_POSTWRITE);
	htobem32(MCX_DMA_OFF(&sc->sc_doorbell_mem, rx->rx_doorbell),
	    p & MCX_WQ_DOORBELL_MASK);
	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&sc->sc_doorbell_mem),
	    rx->rx_doorbell, sizeof(uint32_t), BUS_DMASYNC_PREWRITE);

	return (nslots - fills);
}

int
mcx_rx_fill(struct mcx_softc *sc, struct mcx_rx *rx)
{
	u_int slots;

	slots = if_rxr_get(&rx->rx_rxr, (1 << MCX_LOG_RQ_SIZE));
	if (slots == 0)
		return (1);

	slots = mcx_rx_fill_slots(sc, rx, slots);
	if_rxr_put(&rx->rx_rxr, slots);
	return (0);
}

void
mcx_refill(void *xrx)
{
	struct mcx_rx *rx = xrx;
	struct mcx_softc *sc = rx->rx_softc;

	mcx_rx_fill(sc, rx);

	if (if_rxr_inuse(&rx->rx_rxr) == 0)
		timeout_add(&rx->rx_refill, 1);
}

static int
mcx_process_txeof(struct mcx_softc *sc, struct mcx_tx *tx,
    struct mcx_cq_entry *cqe)
{
	struct mcx_slot *ms;
	bus_dmamap_t map;
	int slot, slots;

	slot = betoh16(cqe->cq_wqe_count) % (1 << MCX_LOG_SQ_SIZE);

	ms = &tx->tx_slots[slot];
	map = ms->ms_map;
	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);

	slots = 1;
	if (map->dm_nsegs > 1)
		slots += (map->dm_nsegs+2) / MCX_SQ_SEGS_PER_SLOT;

	bus_dmamap_unload(sc->sc_dmat, map);
	m_freem(ms->ms_m);
	ms->ms_m = NULL;

	return (slots);
}

static void
mcx_calibrate_first(struct mcx_softc *sc)
{
	struct mcx_calibration *c = &sc->sc_calibration[0];
	int s;

	sc->sc_calibration_gen = 0;

	s = splhigh(); /* crit_enter? */
	c->c_ubase = nsecuptime();
	c->c_tbase = mcx_timer(sc);
	splx(s);
	c->c_ratio = 0;

#ifdef notyet
	timeout_add_sec(&sc->sc_calibrate, MCX_CALIBRATE_FIRST);
#endif
}

#define MCX_TIMESTAMP_SHIFT 24

static void
mcx_calibrate(void *arg)
{
	struct mcx_softc *sc = arg;
	struct mcx_calibration *nc, *pc;
	uint64_t udiff, tdiff;
	unsigned int gen;
	int s;

	if (!ISSET(sc->sc_ac.ac_if.if_flags, IFF_RUNNING))
		return;

	timeout_add_sec(&sc->sc_calibrate, MCX_CALIBRATE_NORMAL);

	gen = sc->sc_calibration_gen;
	pc = &sc->sc_calibration[gen % nitems(sc->sc_calibration)];
	gen++;
	nc = &sc->sc_calibration[gen % nitems(sc->sc_calibration)];

	nc->c_uptime = pc->c_ubase;
	nc->c_timestamp = pc->c_tbase;

	s = splhigh(); /* crit_enter? */
	nc->c_ubase = nsecuptime();
	nc->c_tbase = mcx_timer(sc);
	splx(s);

	udiff = nc->c_ubase - nc->c_uptime;
	tdiff = nc->c_tbase - nc->c_timestamp;

	/*
	 * udiff is the wall clock time between calibration ticks,
	 * which should be 32 seconds or 32 billion nanoseconds. if
	 * we squint, 1 billion nanoseconds is kind of like a 32 bit
	 * number, so 32 billion should still have a lot of high bits
	 * spare. we use this space by shifting the nanoseconds up
	 * 24 bits so we have a nice big number to divide by the
	 * number of mcx timer ticks.
	 */
	nc->c_ratio = (udiff << MCX_TIMESTAMP_SHIFT) / tdiff;

	membar_producer();
	sc->sc_calibration_gen = gen;
}

static int
mcx_process_rx(struct mcx_softc *sc, struct mcx_rx *rx,
    struct mcx_cq_entry *cqe, struct mbuf_list *ml,
    const struct mcx_calibration *c)
{
	struct mcx_slot *ms;
	struct mbuf *m;
	uint32_t flags, len;
	int slot;

	len = bemtoh32(&cqe->cq_byte_cnt);
	slot = betoh16(cqe->cq_wqe_count) % (1 << MCX_LOG_RQ_SIZE);

	ms = &rx->rx_slots[slot];
	bus_dmamap_sync(sc->sc_dmat, ms->ms_map, 0, len, BUS_DMASYNC_POSTREAD);
	bus_dmamap_unload(sc->sc_dmat, ms->ms_map);

	m = ms->ms_m;
	ms->ms_m = NULL;

	m->m_pkthdr.len = m->m_len = len;

	if (cqe->cq_rx_hash_type) {
		m->m_pkthdr.ph_flowid = betoh32(cqe->cq_rx_hash);
		m->m_pkthdr.csum_flags |= M_FLOWID;
	}

	flags = bemtoh32(&cqe->cq_flags);
	if (flags & MCX_CQ_ENTRY_FLAGS_L3_OK)
		m->m_pkthdr.csum_flags |= M_IPV4_CSUM_IN_OK;
	if (flags & MCX_CQ_ENTRY_FLAGS_L4_OK)
		m->m_pkthdr.csum_flags |= M_TCP_CSUM_IN_OK |
		    M_UDP_CSUM_IN_OK;
#if NVLAN > 0
	if (flags & MCX_CQ_ENTRY_FLAGS_CV) {
		m->m_pkthdr.ether_vtag = (flags &
		    MCX_CQ_ENTRY_FLAGS_VLAN_MASK);
		m->m_flags |= M_VLANTAG;
	}
#endif

#ifdef notyet
	if (ISSET(sc->sc_ac.ac_if.if_flags, IFF_LINK0) && c->c_ratio) {
		uint64_t t = bemtoh64(&cqe->cq_timestamp);
		t -= c->c_timestamp;
		t *= c->c_ratio;
		t >>= MCX_TIMESTAMP_SHIFT;
		t += c->c_uptime;

		m->m_pkthdr.ph_timestamp = t;
		SET(m->m_pkthdr.csum_flags, M_TIMESTAMP);
	}
#endif

	ml_enqueue(ml, m);

	return (1);
}

static struct mcx_cq_entry *
mcx_next_cq_entry(struct mcx_softc *sc, struct mcx_cq *cq)
{
	struct mcx_cq_entry *cqe;
	int next;

	cqe = (struct mcx_cq_entry *)MCX_DMA_KVA(&cq->cq_mem);
	next = cq->cq_cons % (1 << MCX_LOG_CQ_SIZE);

	if ((cqe[next].cq_opcode_owner & MCX_CQ_ENTRY_FLAG_OWNER) ==
	    ((cq->cq_cons >> MCX_LOG_CQ_SIZE) & 1)) {
		return (&cqe[next]);
	}

	return (NULL);
}

static void
mcx_arm_cq(struct mcx_softc *sc, struct mcx_cq *cq, int uar)
{
	struct mcx_cq_doorbell *db;
	bus_size_t offset;
	uint32_t val;
	uint64_t uval;

	val = ((cq->cq_count) & 3) << MCX_CQ_DOORBELL_ARM_CMD_SN_SHIFT;
	val |= (cq->cq_cons & MCX_CQ_DOORBELL_ARM_CI_MASK);

	db = MCX_DMA_OFF(&sc->sc_doorbell_mem, cq->cq_doorbell);

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&sc->sc_doorbell_mem),
	    cq->cq_doorbell, sizeof(*db), BUS_DMASYNC_POSTWRITE);

	htobem32(&db->db_update_ci, cq->cq_cons & MCX_CQ_DOORBELL_ARM_CI_MASK);
	htobem32(&db->db_arm_ci, val);

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&sc->sc_doorbell_mem),
	    cq->cq_doorbell, sizeof(*db), BUS_DMASYNC_PREWRITE);

	offset = (MCX_PAGE_SIZE * uar) + MCX_UAR_CQ_DOORBELL;

	uval = (uint64_t)val << 32;
	uval |= cq->cq_n;

	bus_space_write_raw_8(sc->sc_memt, sc->sc_memh, offset, htobe64(uval));
	mcx_bar(sc, offset, sizeof(uval), BUS_SPACE_BARRIER_WRITE);
}

void
mcx_process_cq(struct mcx_softc *sc, struct mcx_queues *q, struct mcx_cq *cq)
{
	struct mcx_rx *rx = &q->q_rx;
	struct mcx_tx *tx = &q->q_tx;
	const struct mcx_calibration *c;
	unsigned int gen;
	struct mcx_cq_entry *cqe;
	uint8_t *cqp;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	int rxfree, txfree;

	gen = sc->sc_calibration_gen;
	membar_consumer();
	c = &sc->sc_calibration[gen % nitems(sc->sc_calibration)];

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&cq->cq_mem),
	    0, MCX_DMA_LEN(&cq->cq_mem), BUS_DMASYNC_POSTREAD);

	rxfree = 0;
	txfree = 0;
	while ((cqe = mcx_next_cq_entry(sc, cq))) {
		uint8_t opcode;
		opcode = (cqe->cq_opcode_owner >> MCX_CQ_ENTRY_OPCODE_SHIFT);
		switch (opcode) {
		case MCX_CQ_ENTRY_OPCODE_REQ:
			txfree += mcx_process_txeof(sc, tx, cqe);
			break;
		case MCX_CQ_ENTRY_OPCODE_SEND:
			rxfree += mcx_process_rx(sc, rx, cqe, &ml, c);
			break;
		case MCX_CQ_ENTRY_OPCODE_REQ_ERR:
		case MCX_CQ_ENTRY_OPCODE_SEND_ERR:
			cqp = (uint8_t *)cqe;
			/* printf("%s: cq completion error: %x\n",
			    DEVNAME(sc), cqp[0x37]); */
			break;

		default:
			/* printf("%s: cq completion opcode %x??\n",
			    DEVNAME(sc), opcode); */
			break;
		}

		cq->cq_cons++;
	}

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&cq->cq_mem),
	    0, MCX_DMA_LEN(&cq->cq_mem), BUS_DMASYNC_PREREAD);

	if (rxfree > 0) {
		if_rxr_put(&rx->rx_rxr, rxfree);
		if (ifiq_input(rx->rx_ifiq, &ml))
			if_rxr_livelocked(&rx->rx_rxr);

		mcx_rx_fill(sc, rx);
		if (if_rxr_inuse(&rx->rx_rxr) == 0)
			timeout_add(&rx->rx_refill, 1);
	}

	cq->cq_count++;
	mcx_arm_cq(sc, cq, q->q_uar);

	if (txfree > 0) {
		tx->tx_cons += txfree;
		if (ifq_is_oactive(tx->tx_ifq))
			ifq_restart(tx->tx_ifq);
	}
}


static void
mcx_arm_eq(struct mcx_softc *sc, struct mcx_eq *eq, int uar)
{
	bus_size_t offset;
	uint32_t val;

	offset = (MCX_PAGE_SIZE * uar) + MCX_UAR_EQ_DOORBELL_ARM;
	val = (eq->eq_n << 24) | (eq->eq_cons & 0xffffff);

	mcx_wr(sc, offset, val);
	mcx_bar(sc, offset, sizeof(val), BUS_SPACE_BARRIER_WRITE);
}

static struct mcx_eq_entry *
mcx_next_eq_entry(struct mcx_softc *sc, struct mcx_eq *eq)
{
	struct mcx_eq_entry *eqe;
	int next;

	eqe = (struct mcx_eq_entry *)MCX_DMA_KVA(&eq->eq_mem);
	next = eq->eq_cons % (1 << MCX_LOG_EQ_SIZE);
	if ((eqe[next].eq_owner & 1) ==
	    ((eq->eq_cons >> MCX_LOG_EQ_SIZE) & 1)) {
		eq->eq_cons++;
		return (&eqe[next]);
	}
	return (NULL);
}

int
mcx_admin_intr(void *xsc)
{
	struct mcx_softc *sc = (struct mcx_softc *)xsc;
	struct mcx_eq *eq = &sc->sc_admin_eq;
	struct mcx_eq_entry *eqe;

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&eq->eq_mem),
	    0, MCX_DMA_LEN(&eq->eq_mem), BUS_DMASYNC_POSTREAD);

	while ((eqe = mcx_next_eq_entry(sc, eq)) != NULL) {
		switch (eqe->eq_event_type) {
		case MCX_EVENT_TYPE_LAST_WQE:
			/* printf("%s: last wqe reached?\n", DEVNAME(sc)); */
			break;

		case MCX_EVENT_TYPE_CQ_ERROR:
			/* printf("%s: cq error\n", DEVNAME(sc)); */
			break;

		case MCX_EVENT_TYPE_CMD_COMPLETION:
			mtx_enter(&sc->sc_cmdq_mtx);
			wakeup(&sc->sc_cmdq_token);
			mtx_leave(&sc->sc_cmdq_mtx);
			break;

		case MCX_EVENT_TYPE_PORT_CHANGE:
			task_add(systq, &sc->sc_port_change);
			break;

		default:
			/* printf("%s: something happened\n", DEVNAME(sc)); */
			break;
		}
	}

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&eq->eq_mem),
	    0, MCX_DMA_LEN(&eq->eq_mem), BUS_DMASYNC_PREREAD);

	mcx_arm_eq(sc, eq, sc->sc_uar);

	return (1);
}

int
mcx_cq_intr(void *xq)
{
	struct mcx_queues *q = (struct mcx_queues *)xq;
	struct mcx_softc *sc = q->q_sc;
	struct mcx_eq *eq = &q->q_eq;
	struct mcx_eq_entry *eqe;
	int cqn;

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&eq->eq_mem),
	    0, MCX_DMA_LEN(&eq->eq_mem), BUS_DMASYNC_POSTREAD);

	while ((eqe = mcx_next_eq_entry(sc, eq)) != NULL) {
		switch (eqe->eq_event_type) {
		case MCX_EVENT_TYPE_COMPLETION:
			cqn = betoh32(eqe->eq_event_data[6]);
			if (cqn == q->q_cq.cq_n)
				mcx_process_cq(sc, q, &q->q_cq);
			break;
		}
	}

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&eq->eq_mem),
	    0, MCX_DMA_LEN(&eq->eq_mem), BUS_DMASYNC_PREREAD);

	mcx_arm_eq(sc, eq, q->q_uar);

	return (1);
}

static void
mcx_free_slots(struct mcx_softc *sc, struct mcx_slot *slots, int allocated,
    int total)
{
	struct mcx_slot *ms;

	int i = allocated;
	while (i-- > 0) {
		ms = &slots[i];
		bus_dmamap_destroy(sc->sc_dmat, ms->ms_map);
		if (ms->ms_m != NULL)
			m_freem(ms->ms_m);
	}
	free(slots, M_DEVBUF, total * sizeof(*ms));
}

static int
mcx_queue_up(struct mcx_softc *sc, struct mcx_queues *q)
{
	struct mcx_rx *rx;
	struct mcx_tx *tx;
	struct mcx_slot *ms;
	int i;

	rx = &q->q_rx;
	rx->rx_slots = mallocarray(sizeof(*ms), (1 << MCX_LOG_RQ_SIZE),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	if (rx->rx_slots == NULL) {
		printf("%s: failed to allocate rx slots\n", DEVNAME(sc));
		return ENOMEM;
	}

	for (i = 0; i < (1 << MCX_LOG_RQ_SIZE); i++) {
		ms = &rx->rx_slots[i];
		if (bus_dmamap_create(sc->sc_dmat, sc->sc_hardmtu, 1,
		    sc->sc_hardmtu, 0,
		    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
		    &ms->ms_map) != 0) {
			printf("%s: failed to allocate rx dma maps\n",
			    DEVNAME(sc));
			goto destroy_rx_slots;
		}
	}

	tx = &q->q_tx;
	tx->tx_slots = mallocarray(sizeof(*ms), (1 << MCX_LOG_SQ_SIZE),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	if (tx->tx_slots == NULL) {
		printf("%s: failed to allocate tx slots\n", DEVNAME(sc));
		goto destroy_rx_slots;
	}

	for (i = 0; i < (1 << MCX_LOG_SQ_SIZE); i++) {
		ms = &tx->tx_slots[i];
		if (bus_dmamap_create(sc->sc_dmat, sc->sc_hardmtu,
		    MCX_SQ_MAX_SEGMENTS, sc->sc_hardmtu, 0,
		    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
		    &ms->ms_map) != 0) {
			printf("%s: failed to allocate tx dma maps\n",
			    DEVNAME(sc));
			goto destroy_tx_slots;
		}
	}

	if (mcx_create_cq(sc, &q->q_cq, q->q_uar, q->q_index,
	    q->q_eq.eq_n) != 0)
		goto destroy_tx_slots;

	if (mcx_create_sq(sc, tx, q->q_uar, q->q_index, q->q_cq.cq_n)
	    != 0)
		goto destroy_cq;

	if (mcx_create_rq(sc, rx, q->q_index, q->q_cq.cq_n) != 0)
		goto destroy_sq;

	return 0;

destroy_sq:
	mcx_destroy_sq(sc, tx);
destroy_cq:
	mcx_destroy_cq(sc, &q->q_cq);
destroy_tx_slots:
	mcx_free_slots(sc, tx->tx_slots, i, (1 << MCX_LOG_SQ_SIZE));
	tx->tx_slots = NULL;

	i = (1 << MCX_LOG_RQ_SIZE);
destroy_rx_slots:
	mcx_free_slots(sc, rx->rx_slots, i, (1 << MCX_LOG_RQ_SIZE));
	rx->rx_slots = NULL;
	return ENOMEM;
}

static int
mcx_rss_group_entry_count(struct mcx_softc *sc, int group)
{
	int i;
	int count;

	count = 0;
	for (i = 0; i < nitems(mcx_rss_config); i++) {
		if (mcx_rss_config[i].flow_group == group)
			count++;
	}

	return count;
}

static int
mcx_up(struct mcx_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct mcx_rx *rx;
	struct mcx_tx *tx;
	int i, start, count, flow_group, flow_index;
	struct mcx_flow_match match_crit;
	struct mcx_rss_rule *rss;
	uint32_t dest;
	int rqns[MCX_MAX_QUEUES];

	if (mcx_create_tis(sc, &sc->sc_tis) != 0)
		goto down;

	for (i = 0; i < intrmap_count(sc->sc_intrmap); i++) {
		if (mcx_queue_up(sc, &sc->sc_queues[i]) != 0) {
			goto down;
		}
	}

	/* RSS flow table and flow groups */
	if (mcx_create_flow_table(sc, MCX_LOG_FLOW_TABLE_SIZE, 1,
	    &sc->sc_rss_flow_table_id) != 0)
		goto down;

	dest = MCX_FLOW_CONTEXT_DEST_TYPE_TABLE |
	    sc->sc_rss_flow_table_id;

	/* L4 RSS flow group (v4/v6 tcp/udp, no fragments) */
	memset(&match_crit, 0, sizeof(match_crit));
	match_crit.mc_ethertype = 0xffff;
	match_crit.mc_ip_proto = 0xff;
	match_crit.mc_vlan_flags = MCX_FLOW_MATCH_IP_FRAG;
	start = 0;
	count = mcx_rss_group_entry_count(sc, MCX_FLOW_GROUP_RSS_L4);
	if (count != 0) {
		if (mcx_create_flow_group(sc, sc->sc_rss_flow_table_id,
		    MCX_FLOW_GROUP_RSS_L4, start, count,
		    MCX_CREATE_FLOW_GROUP_CRIT_OUTER, &match_crit) != 0)
			goto down;
		start += count;
	}

	/* L3 RSS flow group (v4/v6, including fragments) */
	memset(&match_crit, 0, sizeof(match_crit));
	match_crit.mc_ethertype = 0xffff;
	count = mcx_rss_group_entry_count(sc, MCX_FLOW_GROUP_RSS_L3);
	if (mcx_create_flow_group(sc, sc->sc_rss_flow_table_id,
	    MCX_FLOW_GROUP_RSS_L3, start, count,
	    MCX_CREATE_FLOW_GROUP_CRIT_OUTER, &match_crit) != 0)
		goto down;
	start += count;

	/* non-RSS flow group */
	count = mcx_rss_group_entry_count(sc, MCX_FLOW_GROUP_RSS_NONE);
	memset(&match_crit, 0, sizeof(match_crit));
	if (mcx_create_flow_group(sc, sc->sc_rss_flow_table_id,
	    MCX_FLOW_GROUP_RSS_NONE, start, count, 0, &match_crit) != 0)
		goto down;

	/* Root flow table, matching packets based on mac address */
	if (mcx_create_flow_table(sc, MCX_LOG_FLOW_TABLE_SIZE, 0,
	    &sc->sc_mac_flow_table_id) != 0)
		goto down;

	/* promisc flow group */
	start = 0;
	memset(&match_crit, 0, sizeof(match_crit));
	if (mcx_create_flow_group(sc, sc->sc_mac_flow_table_id,
	    MCX_FLOW_GROUP_PROMISC, start, 1, 0, &match_crit) != 0)
		goto down;
	sc->sc_promisc_flow_enabled = 0;
	start++;

	/* all multicast flow group */
	match_crit.mc_dest_mac[0] = 0x01;
	if (mcx_create_flow_group(sc, sc->sc_mac_flow_table_id,
	    MCX_FLOW_GROUP_ALLMULTI, start, 1,
	    MCX_CREATE_FLOW_GROUP_CRIT_OUTER, &match_crit) != 0)
		goto down;
	sc->sc_allmulti_flow_enabled = 0;
	start++;

	/* mac address matching flow group */
	memset(&match_crit.mc_dest_mac, 0xff, sizeof(match_crit.mc_dest_mac));
	if (mcx_create_flow_group(sc, sc->sc_mac_flow_table_id,
	    MCX_FLOW_GROUP_MAC, start, (1 << MCX_LOG_FLOW_TABLE_SIZE) - start,
	    MCX_CREATE_FLOW_GROUP_CRIT_OUTER, &match_crit) != 0)
		goto down;

	/* flow table entries for unicast and broadcast */
	start = 0;
	if (mcx_set_flow_table_entry_mac(sc, MCX_FLOW_GROUP_MAC, start,
	    sc->sc_ac.ac_enaddr, dest) != 0)
		goto down;
	start++;

	if (mcx_set_flow_table_entry_mac(sc, MCX_FLOW_GROUP_MAC, start,
	    etherbroadcastaddr, dest) != 0)
		goto down;
	start++;

	/* multicast entries go after that */
	sc->sc_mcast_flow_base = start;

	/* re-add any existing multicast flows */
	for (i = 0; i < MCX_NUM_MCAST_FLOWS; i++) {
		if (sc->sc_mcast_flows[i][0] != 0) {
			mcx_set_flow_table_entry_mac(sc, MCX_FLOW_GROUP_MAC,
			    sc->sc_mcast_flow_base + i,
			    sc->sc_mcast_flows[i], dest);
		}
	}

	if (mcx_set_flow_table_root(sc, sc->sc_mac_flow_table_id) != 0)
		goto down;

	/*
	 * the RQT can be any size as long as it's a power of two.
	 * since we also restrict the number of queues to a power of two,
	 * we can just put each rx queue in once.
	 */
	for (i = 0; i < intrmap_count(sc->sc_intrmap); i++)
		rqns[i] = sc->sc_queues[i].q_rx.rx_rqn;

	if (mcx_create_rqt(sc, intrmap_count(sc->sc_intrmap), rqns,
	    &sc->sc_rqt) != 0)
		goto down;

	start = 0;
	flow_index = 0;
	flow_group = -1;
	for (i = 0; i < nitems(mcx_rss_config); i++) {
		rss = &mcx_rss_config[i];
		if (rss->flow_group != flow_group) {
			flow_group = rss->flow_group;
			flow_index = 0;
		}

		if (rss->hash_sel == 0) {
			if (mcx_create_tir_direct(sc, &sc->sc_queues[0].q_rx,
			    &sc->sc_tir[i]) != 0)
				goto down;
		} else {
			if (mcx_create_tir_indirect(sc, sc->sc_rqt,
			    rss->hash_sel, &sc->sc_tir[i]) != 0)
				goto down;
		}

		if (mcx_set_flow_table_entry_proto(sc, flow_group,
		    flow_index, rss->ethertype, rss->ip_proto,
		    MCX_FLOW_CONTEXT_DEST_TYPE_TIR | sc->sc_tir[i]) != 0)
			goto down;
		flow_index++;
	}

	for (i = 0; i < intrmap_count(sc->sc_intrmap); i++) {
		struct mcx_queues *q = &sc->sc_queues[i];
		rx = &q->q_rx;
		tx = &q->q_tx;

		/* start the queues */
		if (mcx_ready_sq(sc, tx) != 0)
			goto down;

		if (mcx_ready_rq(sc, rx) != 0)
			goto down;

		if_rxr_init(&rx->rx_rxr, 1, (1 << MCX_LOG_RQ_SIZE));
		rx->rx_prod = 0;
		mcx_rx_fill(sc, rx);

		tx->tx_cons = 0;
		tx->tx_prod = 0;
		ifq_clr_oactive(tx->tx_ifq);
	}

	mcx_calibrate_first(sc);

	SET(ifp->if_flags, IFF_RUNNING);

	return ENETRESET;
down:
	mcx_down(sc);
	return ENOMEM;
}

static void
mcx_down(struct mcx_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct mcx_rss_rule *rss;
	int group, i, flow_group, flow_index;

	CLR(ifp->if_flags, IFF_RUNNING);

	/*
	 * delete flow table entries first, so no packets can arrive
	 * after the barriers
	 */
	if (sc->sc_promisc_flow_enabled)
		mcx_delete_flow_table_entry(sc, MCX_FLOW_GROUP_PROMISC, 0);
	if (sc->sc_allmulti_flow_enabled)
		mcx_delete_flow_table_entry(sc, MCX_FLOW_GROUP_ALLMULTI, 0);
	mcx_delete_flow_table_entry(sc, MCX_FLOW_GROUP_MAC, 0);
	mcx_delete_flow_table_entry(sc, MCX_FLOW_GROUP_MAC, 1);
	for (i = 0; i < MCX_NUM_MCAST_FLOWS; i++) {
		if (sc->sc_mcast_flows[i][0] != 0) {
			mcx_delete_flow_table_entry(sc, MCX_FLOW_GROUP_MAC,
			    sc->sc_mcast_flow_base + i);
		}
	}

	flow_group = -1;
	flow_index = 0;
	for (i = 0; i < nitems(mcx_rss_config); i++) {
		rss = &mcx_rss_config[i];
		if (rss->flow_group != flow_group) {
			flow_group = rss->flow_group;
			flow_index = 0;
		}

		mcx_delete_flow_table_entry(sc, flow_group, flow_index);

		mcx_destroy_tir(sc, sc->sc_tir[i]);
		sc->sc_tir[i] = 0;

		flow_index++;
	}
	intr_barrier(sc->sc_ihc);
	for (i = 0; i < intrmap_count(sc->sc_intrmap); i++) {
		struct ifqueue *ifq = sc->sc_queues[i].q_tx.tx_ifq;
		ifq_barrier(ifq);

		timeout_del_barrier(&sc->sc_queues[i].q_rx.rx_refill);

		intr_barrier(sc->sc_queues[i].q_ihc);
	}

	timeout_del_barrier(&sc->sc_calibrate);

	for (group = 0; group < MCX_NUM_FLOW_GROUPS; group++) {
		if (sc->sc_flow_group[group].g_id != -1)
			mcx_destroy_flow_group(sc, group);
	}

	if (sc->sc_mac_flow_table_id != -1) {
		mcx_destroy_flow_table(sc, sc->sc_mac_flow_table_id);
		sc->sc_mac_flow_table_id = -1;
	}
	if (sc->sc_rss_flow_table_id != -1) {
		mcx_destroy_flow_table(sc, sc->sc_rss_flow_table_id);
		sc->sc_rss_flow_table_id = -1;
	}
	if (sc->sc_rqt != -1) {
		mcx_destroy_rqt(sc, sc->sc_rqt);
		sc->sc_rqt = -1;
	}

	for (i = 0; i < intrmap_count(sc->sc_intrmap); i++) {
		struct mcx_queues *q = &sc->sc_queues[i];
		struct mcx_rx *rx = &q->q_rx;
		struct mcx_tx *tx = &q->q_tx;
		struct mcx_cq *cq = &q->q_cq;

		if (rx->rx_rqn != 0)
			mcx_destroy_rq(sc, rx);

		if (tx->tx_sqn != 0)
			mcx_destroy_sq(sc, tx);

		if (tx->tx_slots != NULL) {
			mcx_free_slots(sc, tx->tx_slots,
			    (1 << MCX_LOG_SQ_SIZE), (1 << MCX_LOG_SQ_SIZE));
			tx->tx_slots = NULL;
		}
		if (rx->rx_slots != NULL) {
			mcx_free_slots(sc, rx->rx_slots,
			    (1 << MCX_LOG_RQ_SIZE), (1 << MCX_LOG_RQ_SIZE));
			rx->rx_slots = NULL;
		}

		if (cq->cq_n != 0)
			mcx_destroy_cq(sc, cq);
	}
	if (sc->sc_tis != 0) {
		mcx_destroy_tis(sc, sc->sc_tis);
		sc->sc_tis = 0;
	}
}

static int
mcx_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct mcx_softc *sc = (struct mcx_softc *)ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	uint8_t addrhi[ETHER_ADDR_LEN], addrlo[ETHER_ADDR_LEN];
	int s, i, error = 0;
	uint32_t dest;

	s = splnet();
	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */

	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = ENETRESET;
			else
				error = mcx_up(sc);
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				mcx_down(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;

	case SIOCGIFSFFPAGE:
		error = mcx_get_sffpage(ifp, (struct if_sffpage *)data);
		break;

	case SIOCGIFRXR:
		error = mcx_rxrinfo(sc, (struct if_rxrinfo *)ifr->ifr_data);
		break;

	case SIOCADDMULTI:
		if (ether_addmulti(ifr, &sc->sc_ac) == ENETRESET) {
			error = ether_multiaddr(&ifr->ifr_addr, addrlo, addrhi);
			if (error != 0)
				break;

			dest = MCX_FLOW_CONTEXT_DEST_TYPE_TABLE |
			    sc->sc_rss_flow_table_id;

			for (i = 0; i < MCX_NUM_MCAST_FLOWS; i++) {
				if (sc->sc_mcast_flows[i][0] == 0) {
					memcpy(sc->sc_mcast_flows[i], addrlo,
					    ETHER_ADDR_LEN);
					if (ISSET(ifp->if_flags, IFF_RUNNING)) {
						mcx_set_flow_table_entry_mac(sc,
						    MCX_FLOW_GROUP_MAC,
						    sc->sc_mcast_flow_base + i,
						    sc->sc_mcast_flows[i], dest);
					}
					break;
				}
			}

			if (!ISSET(ifp->if_flags, IFF_ALLMULTI)) {
				if (i == MCX_NUM_MCAST_FLOWS) {
					SET(ifp->if_flags, IFF_ALLMULTI);
					sc->sc_extra_mcast++;
					error = ENETRESET;
				}

				if (sc->sc_ac.ac_multirangecnt > 0) {
					SET(ifp->if_flags, IFF_ALLMULTI);
					error = ENETRESET;
				}
			}
		}
		break;

	case SIOCDELMULTI:
		if (ether_delmulti(ifr, &sc->sc_ac) == ENETRESET) {
			error = ether_multiaddr(&ifr->ifr_addr, addrlo, addrhi);
			if (error != 0)
				break;

			for (i = 0; i < MCX_NUM_MCAST_FLOWS; i++) {
				if (memcmp(sc->sc_mcast_flows[i], addrlo,
				    ETHER_ADDR_LEN) == 0) {
					if (ISSET(ifp->if_flags, IFF_RUNNING)) {
						mcx_delete_flow_table_entry(sc,
						    MCX_FLOW_GROUP_MAC,
						    sc->sc_mcast_flow_base + i);
					}
					sc->sc_mcast_flows[i][0] = 0;
					break;
				}
			}

			if (i == MCX_NUM_MCAST_FLOWS)
				sc->sc_extra_mcast--;

			if (ISSET(ifp->if_flags, IFF_ALLMULTI) &&
			    (sc->sc_extra_mcast == 0) &&
			    (sc->sc_ac.ac_multirangecnt == 0)) {
				CLR(ifp->if_flags, IFF_ALLMULTI);
				error = ENETRESET;
			}
		}
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			mcx_iff(sc);
		error = 0;
	}
	splx(s);

	return (error);
}

static int
mcx_get_sffpage(struct ifnet *ifp, struct if_sffpage *sff)
{
	struct mcx_softc *sc = (struct mcx_softc *)ifp->if_softc;
	struct mcx_reg_mcia mcia;
	struct mcx_reg_pmlp pmlp;
	int offset, error;

	rw_enter_write(&sc->sc_cmdq_ioctl_lk);

	/* get module number */
	memset(&pmlp, 0, sizeof(pmlp));
	pmlp.rp_local_port = 1;
	error = mcx_access_hca_reg(sc, MCX_REG_PMLP, MCX_REG_OP_READ, &pmlp,
	    sizeof(pmlp), MCX_CMDQ_SLOT_IOCTL);
	if (error != 0) {
		printf("%s: unable to get eeprom module number\n",
		    DEVNAME(sc));
		goto out;
	}

	for (offset = 0; offset < 256; offset += MCX_MCIA_EEPROM_BYTES) {
		memset(&mcia, 0, sizeof(mcia));
		mcia.rm_l = 0;
		mcia.rm_module = betoh32(pmlp.rp_lane0_mapping) &
		    MCX_PMLP_MODULE_NUM_MASK;
		mcia.rm_i2c_addr = sff->sff_addr / 2;	/* apparently */
		mcia.rm_page_num = sff->sff_page;
		mcia.rm_dev_addr = htobe16(offset);
		mcia.rm_size = htobe16(MCX_MCIA_EEPROM_BYTES);

		error = mcx_access_hca_reg(sc, MCX_REG_MCIA, MCX_REG_OP_READ,
		    &mcia, sizeof(mcia), MCX_CMDQ_SLOT_IOCTL);
		if (error != 0) {
			printf("%s: unable to read eeprom at %x\n",
			    DEVNAME(sc), offset);
			goto out;
		}

		memcpy(sff->sff_data + offset, mcia.rm_data,
		    MCX_MCIA_EEPROM_BYTES);
	}

 out:
	rw_exit_write(&sc->sc_cmdq_ioctl_lk);
	return (error);
}

static int
mcx_rxrinfo(struct mcx_softc *sc, struct if_rxrinfo *ifri)
{
	struct if_rxring_info *ifrs;
	unsigned int i;
	int error;

	ifrs = mallocarray(intrmap_count(sc->sc_intrmap), sizeof(*ifrs),
	    M_TEMP, M_WAITOK|M_ZERO|M_CANFAIL);
	if (ifrs == NULL)
		return (ENOMEM);

	for (i = 0; i < intrmap_count(sc->sc_intrmap); i++) {
		struct mcx_rx *rx = &sc->sc_queues[i].q_rx;
		struct if_rxring_info *ifr = &ifrs[i];

		snprintf(ifr->ifr_name, sizeof(ifr->ifr_name), "%u", i);
		ifr->ifr_size = sc->sc_hardmtu;
		ifr->ifr_info = rx->rx_rxr;
	}

	error = if_rxr_info_ioctl(ifri, i, ifrs);
	free(ifrs, M_TEMP, i * sizeof(*ifrs));

	return (error);
}

int
mcx_load_mbuf(struct mcx_softc *sc, struct mcx_slot *ms, struct mbuf *m)
{
	switch (bus_dmamap_load_mbuf(sc->sc_dmat, ms->ms_map, m,
	    BUS_DMA_STREAMING | BUS_DMA_NOWAIT)) {
	case 0:
		break;

	case EFBIG:
		if (m_defrag(m, M_DONTWAIT) == 0 &&
		    bus_dmamap_load_mbuf(sc->sc_dmat, ms->ms_map, m,
		    BUS_DMA_STREAMING | BUS_DMA_NOWAIT) == 0)
			break;

	default:
		return (1);
	}

	ms->ms_m = m;
	return (0);
}

static void
mcx_start(struct ifqueue *ifq)
{
	struct mcx_tx *tx = ifq->ifq_softc;
	struct ifnet *ifp = ifq->ifq_if;
	struct mcx_softc *sc = ifp->if_softc;
	struct mcx_sq_entry *sq, *sqe;
	struct mcx_sq_entry_seg *sqs;
	struct mcx_slot *ms;
	bus_dmamap_t map;
	struct mbuf *m;
	u_int idx, free, used;
	uint64_t *bf;
	uint32_t csum;
	size_t bf_base;
	int i, seg, nseg;
#if NBPFILTER > 0
	caddr_t if_bpf;
#endif

	bf_base = (tx->tx_uar * MCX_PAGE_SIZE) + MCX_UAR_BF;

	idx = tx->tx_prod % (1 << MCX_LOG_SQ_SIZE);
	free = (tx->tx_cons + (1 << MCX_LOG_SQ_SIZE)) - tx->tx_prod;

	used = 0;
	bf = NULL;

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&tx->tx_sq_mem),
	    0, MCX_DMA_LEN(&tx->tx_sq_mem), BUS_DMASYNC_POSTWRITE);

	sq = (struct mcx_sq_entry *)MCX_DMA_KVA(&tx->tx_sq_mem);

	for (;;) {
		if (used + MCX_SQ_ENTRY_MAX_SLOTS >= free) {
			ifq_set_oactive(ifq);
			break;
		}

		m = ifq_dequeue(ifq);
		if (m == NULL) {
			break;
		}

		sqe = sq + idx;
		ms = &tx->tx_slots[idx];
		memset(sqe, 0, sizeof(*sqe));

		/* ctrl segment */
		sqe->sqe_opcode_index = htobe32(MCX_SQE_WQE_OPCODE_SEND |
		    ((tx->tx_prod & 0xffff) << MCX_SQE_WQE_INDEX_SHIFT));
		/* always generate a completion event */
		sqe->sqe_signature = htobe32(MCX_SQE_CE_CQE_ALWAYS);

		/* eth segment */
		csum = 0;
		if (m->m_pkthdr.csum_flags & M_IPV4_CSUM_OUT)
			csum |= MCX_SQE_L3_CSUM;
		if (m->m_pkthdr.csum_flags & (M_TCP_CSUM_OUT | M_UDP_CSUM_OUT))
			csum |= MCX_SQE_L4_CSUM;
		sqe->sqe_mss_csum = htobe32(csum);
		sqe->sqe_inline_header_size = htobe16(MCX_SQ_INLINE_SIZE);
#if NVLAN > 0
		if (m->m_flags & M_VLANTAG) {
			struct ether_vlan_header *evh;
			evh = (struct ether_vlan_header *)
			    &sqe->sqe_inline_headers;

			/* slightly cheaper vlan_inject() */
			m_copydata(m, 0, ETHER_HDR_LEN, evh);
			evh->evl_proto = evh->evl_encap_proto;
			evh->evl_encap_proto = htons(ETHERTYPE_VLAN);
			evh->evl_tag = htons(m->m_pkthdr.ether_vtag);

			m_adj(m, ETHER_HDR_LEN);
		} else
#endif
		{
			m_copydata(m, 0, MCX_SQ_INLINE_SIZE,
			    sqe->sqe_inline_headers);
			m_adj(m, MCX_SQ_INLINE_SIZE);
		}

		if (mcx_load_mbuf(sc, ms, m) != 0) {
			m_freem(m);
			ifp->if_oerrors++;
			continue;
		}
		bf = (uint64_t *)sqe;

#if NBPFILTER > 0
		if_bpf = *tx->tx_bpfp;
		if (if_bpf) {
			bpf_mtap_hdr(if_bpf,
			    (caddr_t)sqe->sqe_inline_headers,
			    MCX_SQ_INLINE_SIZE, m, BPF_DIRECTION_OUT);
		}

		if_bpf = ifp->if_bpf;
		if (if_bpf) {
			bpf_mtap_hdr(if_bpf,
			    (caddr_t)sqe->sqe_inline_headers,
			    MCX_SQ_INLINE_SIZE, m, BPF_DIRECTION_OUT);
		}
#endif
		map = ms->ms_map;
		bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
		    BUS_DMASYNC_PREWRITE);

		sqe->sqe_ds_sq_num =
		    htobe32((tx->tx_sqn << MCX_SQE_SQ_NUM_SHIFT) |
		    (map->dm_nsegs + 3));

		/* data segment - first wqe has one segment */
		sqs = sqe->sqe_segs;
		seg = 0;
		nseg = 1;
		for (i = 0; i < map->dm_nsegs; i++) {
			if (seg == nseg) {
				/* next slot */
				idx++;
				if (idx == (1 << MCX_LOG_SQ_SIZE))
					idx = 0;
				tx->tx_prod++;
				used++;

				sqs = (struct mcx_sq_entry_seg *)(sq + idx);
				seg = 0;
				nseg = MCX_SQ_SEGS_PER_SLOT;
			}
			sqs[seg].sqs_byte_count =
			    htobe32(map->dm_segs[i].ds_len);
			sqs[seg].sqs_lkey = htobe32(sc->sc_lkey);
			sqs[seg].sqs_addr = htobe64(map->dm_segs[i].ds_addr);
			seg++;
		}

		idx++;
		if (idx == (1 << MCX_LOG_SQ_SIZE))
			idx = 0;
		tx->tx_prod++;
		used++;
	}

	bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&tx->tx_sq_mem),
	    0, MCX_DMA_LEN(&tx->tx_sq_mem), BUS_DMASYNC_PREWRITE);

	if (used) {
		bus_size_t blueflame;

		bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&sc->sc_doorbell_mem),
		    tx->tx_doorbell, sizeof(uint32_t), BUS_DMASYNC_POSTWRITE);
		htobem32(MCX_DMA_OFF(&sc->sc_doorbell_mem, tx->tx_doorbell),
		    tx->tx_prod & MCX_WQ_DOORBELL_MASK);
		bus_dmamap_sync(sc->sc_dmat, MCX_DMA_MAP(&sc->sc_doorbell_mem),
		    tx->tx_doorbell, sizeof(uint32_t), BUS_DMASYNC_PREWRITE);

		/*
		 * write the first 64 bits of the last sqe we produced
		 * to the blue flame buffer
		 */

		blueflame = bf_base + tx->tx_bf_offset;
		bus_space_write_raw_8(sc->sc_memt, sc->sc_memh,
		    blueflame, *bf);
		mcx_bar(sc, blueflame, sizeof(*bf), BUS_SPACE_BARRIER_WRITE);

		/* next write goes to the other buffer */
		tx->tx_bf_offset ^= sc->sc_bf_size;
	}
}

static void
mcx_watchdog(struct ifnet *ifp)
{
}

static void
mcx_media_add_types(struct mcx_softc *sc)
{
	struct mcx_reg_ptys ptys;
	int i;
	uint32_t proto_cap;

	memset(&ptys, 0, sizeof(ptys));
	ptys.rp_local_port = 1;
	ptys.rp_proto_mask = MCX_REG_PTYS_PROTO_MASK_ETH;
	if (mcx_access_hca_reg(sc, MCX_REG_PTYS, MCX_REG_OP_READ, &ptys,
	    sizeof(ptys), MCX_CMDQ_SLOT_POLL) != 0) {
		printf("%s: unable to read port type/speed\n", DEVNAME(sc));
		return;
	}

	proto_cap = betoh32(ptys.rp_eth_proto_cap);
	for (i = 0; i < nitems(mcx_eth_cap_map); i++) {
		const struct mcx_eth_proto_capability *cap;
		if (!ISSET(proto_cap, 1 << i))
			continue;

		cap = &mcx_eth_cap_map[i];
		if (cap->cap_media == 0)
			continue;

		ifmedia_add(&sc->sc_media, IFM_ETHER | cap->cap_media, 0, NULL);
	}

	proto_cap = betoh32(ptys.rp_ext_eth_proto_cap);
	for (i = 0; i < nitems(mcx_ext_eth_cap_map); i++) {
		const struct mcx_eth_proto_capability *cap;
		if (!ISSET(proto_cap, 1 << i))
			continue;

		cap = &mcx_ext_eth_cap_map[i];
		if (cap->cap_media == 0)
			continue;

		ifmedia_add(&sc->sc_media, IFM_ETHER | cap->cap_media, 0, NULL);
	}
}

static void
mcx_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct mcx_softc *sc = (struct mcx_softc *)ifp->if_softc;
	struct mcx_reg_ptys ptys;
	int i;
	uint32_t proto_oper;
	uint32_t ext_proto_oper;
	uint64_t media_oper;

	memset(&ptys, 0, sizeof(ptys));
	ptys.rp_local_port = 1;
	ptys.rp_proto_mask = MCX_REG_PTYS_PROTO_MASK_ETH;

	rw_enter_write(&sc->sc_cmdq_ioctl_lk);
	if (mcx_access_hca_reg(sc, MCX_REG_PTYS, MCX_REG_OP_READ, &ptys,
	    sizeof(ptys), MCX_CMDQ_SLOT_IOCTL) != 0) {
		printf("%s: unable to read port type/speed\n", DEVNAME(sc));
		goto out;
	}

	proto_oper = betoh32(ptys.rp_eth_proto_oper);
	ext_proto_oper = betoh32(ptys.rp_ext_eth_proto_oper);

	media_oper = 0;

	for (i = 0; i < nitems(mcx_eth_cap_map); i++) {
		const struct mcx_eth_proto_capability *cap;
		if (!ISSET(proto_oper, 1 << i))
			continue;

		cap = &mcx_eth_cap_map[i];

		if (cap->cap_media != 0)
			media_oper = cap->cap_media;
	}

	if (media_oper == 0) {
		for (i = 0; i < nitems(mcx_ext_eth_cap_map); i++) {
			const struct mcx_eth_proto_capability *cap;
			if (!ISSET(ext_proto_oper, 1 << i))
				continue;

			cap = &mcx_ext_eth_cap_map[i];

			if (cap->cap_media != 0)
				media_oper = cap->cap_media;
		}
	}

	ifmr->ifm_status = IFM_AVALID;
	if ((proto_oper | ext_proto_oper) != 0) {
		ifmr->ifm_status |= IFM_ACTIVE;
		ifmr->ifm_active = IFM_ETHER | IFM_AUTO | media_oper;
		/* txpause, rxpause, duplex? */
	}
 out:
	rw_exit_write(&sc->sc_cmdq_ioctl_lk);
}

static int
mcx_media_change(struct ifnet *ifp)
{
	struct mcx_softc *sc = (struct mcx_softc *)ifp->if_softc;
	struct mcx_reg_ptys ptys;
	struct mcx_reg_paos paos;
	uint32_t media;
	uint32_t ext_media;
	int i, error;

	if (IFM_TYPE(sc->sc_media.ifm_media) != IFM_ETHER)
		return EINVAL;

	error = 0;
	rw_enter_write(&sc->sc_cmdq_ioctl_lk);

	if (IFM_SUBTYPE(sc->sc_media.ifm_media) == IFM_AUTO) {
		/* read ptys to get supported media */
		memset(&ptys, 0, sizeof(ptys));
		ptys.rp_local_port = 1;
		ptys.rp_proto_mask = MCX_REG_PTYS_PROTO_MASK_ETH;
		if (mcx_access_hca_reg(sc, MCX_REG_PTYS, MCX_REG_OP_READ,
		    &ptys, sizeof(ptys), MCX_CMDQ_SLOT_IOCTL) != 0) {
			printf("%s: unable to read port type/speed\n",
			    DEVNAME(sc));
			error = EIO;
			goto out;
		}

		media = betoh32(ptys.rp_eth_proto_cap);
		ext_media = betoh32(ptys.rp_ext_eth_proto_cap);
	} else {
		/* map media type */
		media = 0;
		for (i = 0; i < nitems(mcx_eth_cap_map); i++) {
			const struct  mcx_eth_proto_capability *cap;

			cap = &mcx_eth_cap_map[i];
			if (cap->cap_media ==
			    IFM_SUBTYPE(sc->sc_media.ifm_media)) {
				media = (1 << i);
				break;
			}
		}

		ext_media = 0;
		for (i = 0; i < nitems(mcx_ext_eth_cap_map); i++) {
			const struct  mcx_eth_proto_capability *cap;

			cap = &mcx_ext_eth_cap_map[i];
			if (cap->cap_media ==
			    IFM_SUBTYPE(sc->sc_media.ifm_media)) {
				ext_media = (1 << i);
				break;
			}
		}
	}

	/* disable the port */
	memset(&paos, 0, sizeof(paos));
	paos.rp_local_port = 1;
	paos.rp_admin_status = MCX_REG_PAOS_ADMIN_STATUS_DOWN;
	paos.rp_admin_state_update = MCX_REG_PAOS_ADMIN_STATE_UPDATE_EN;
	if (mcx_access_hca_reg(sc, MCX_REG_PAOS, MCX_REG_OP_WRITE, &paos,
	    sizeof(paos), MCX_CMDQ_SLOT_IOCTL) != 0) {
		printf("%s: unable to set port state to down\n", DEVNAME(sc));
		error = EIO;
		goto out;
	}

	memset(&ptys, 0, sizeof(ptys));
	ptys.rp_local_port = 1;
	ptys.rp_proto_mask = MCX_REG_PTYS_PROTO_MASK_ETH;
	ptys.rp_eth_proto_admin = htobe32(media);
	ptys.rp_ext_eth_proto_admin = htobe32(ext_media);
	if (mcx_access_hca_reg(sc, MCX_REG_PTYS, MCX_REG_OP_WRITE, &ptys,
	    sizeof(ptys), MCX_CMDQ_SLOT_IOCTL) != 0) {
		printf("%s: unable to set port media type/speed\n",
		    DEVNAME(sc));
		error = EIO;
		/* continue on */
	}

	/* re-enable the port to start negotiation */
	memset(&paos, 0, sizeof(paos));
	paos.rp_local_port = 1;
	paos.rp_admin_status = MCX_REG_PAOS_ADMIN_STATUS_UP;
	paos.rp_admin_state_update = MCX_REG_PAOS_ADMIN_STATE_UPDATE_EN;
	if (mcx_access_hca_reg(sc, MCX_REG_PAOS, MCX_REG_OP_WRITE, &paos,
	    sizeof(paos), MCX_CMDQ_SLOT_IOCTL) != 0) {
		printf("%s: unable to set port state to up\n", DEVNAME(sc));
		error = EIO;
	}

 out:
	rw_exit_write(&sc->sc_cmdq_ioctl_lk);
	return error;
}

static void
mcx_port_change(void *xsc)
{
	struct mcx_softc *sc = xsc;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct mcx_reg_ptys ptys = {
		.rp_local_port = 1,
		.rp_proto_mask = MCX_REG_PTYS_PROTO_MASK_ETH,
	};
	int link_state = LINK_STATE_DOWN;
	int slot;

	if (cold) {
		slot = MCX_CMDQ_SLOT_POLL;
	} else
		slot = MCX_CMDQ_SLOT_LINK;

	if (mcx_access_hca_reg(sc, MCX_REG_PTYS, MCX_REG_OP_READ, &ptys,
	    sizeof(ptys), slot) == 0) {
		uint32_t proto_oper = betoh32(ptys.rp_eth_proto_oper);
		uint32_t ext_proto_oper = betoh32(ptys.rp_ext_eth_proto_oper);
		uint64_t baudrate = 0;
		unsigned int i;

		if ((proto_oper | ext_proto_oper) != 0)
			link_state = LINK_STATE_FULL_DUPLEX;

		for (i = 0; i < nitems(mcx_eth_cap_map); i++) {
			const struct mcx_eth_proto_capability *cap;
			if (!ISSET(proto_oper, 1 << i))
				continue;

			cap = &mcx_eth_cap_map[i];
			if (cap->cap_baudrate == 0)
				continue;

			baudrate = cap->cap_baudrate;
			break;
		}

		if (baudrate == 0) {
			for (i = 0; i < nitems(mcx_ext_eth_cap_map); i++) {
				const struct mcx_eth_proto_capability *cap;
				if (!ISSET(ext_proto_oper, 1 << i))
					continue;

				cap = &mcx_ext_eth_cap_map[i];
				if (cap->cap_baudrate == 0)
					continue;

				baudrate = cap->cap_baudrate;
				break;
			}
		}

		ifp->if_baudrate = baudrate;
	}

	if (link_state != ifp->if_link_state) {
		ifp->if_link_state = link_state;
		if_link_state_change(ifp);
	}
}

static inline uint32_t
mcx_rd(struct mcx_softc *sc, bus_size_t r)
{
	uint32_t word;

	word = bus_space_read_raw_4(sc->sc_memt, sc->sc_memh, r);

	return (betoh32(word));
}

static inline void
mcx_wr(struct mcx_softc *sc, bus_size_t r, uint32_t v)
{
	bus_space_write_raw_4(sc->sc_memt, sc->sc_memh, r, htobe32(v));
}

static inline void
mcx_bar(struct mcx_softc *sc, bus_size_t r, bus_size_t l, int f)
{
	bus_space_barrier(sc->sc_memt, sc->sc_memh, r, l, f);
}

static uint64_t
mcx_timer(struct mcx_softc *sc)
{
	uint32_t hi, lo, ni;

	hi = mcx_rd(sc, MCX_INTERNAL_TIMER_H);
	for (;;) {
		lo = mcx_rd(sc, MCX_INTERNAL_TIMER_L);
		mcx_bar(sc, MCX_INTERNAL_TIMER_L, 8, BUS_SPACE_BARRIER_READ);
		ni = mcx_rd(sc, MCX_INTERNAL_TIMER_H);

		if (ni == hi)
			break;

		hi = ni;
	}

	return (((uint64_t)hi << 32) | (uint64_t)lo);
}

static int
mcx_dmamem_alloc(struct mcx_softc *sc, struct mcx_dmamem *mxm,
    bus_size_t size, u_int align)
{
	mxm->mxm_size = size;

	if (bus_dmamap_create(sc->sc_dmat, mxm->mxm_size, 1,
	    mxm->mxm_size, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
	    &mxm->mxm_map) != 0)
		return (1);
	if (bus_dmamem_alloc(sc->sc_dmat, mxm->mxm_size,
	    align, 0, &mxm->mxm_seg, 1, &mxm->mxm_nsegs,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_64BIT) != 0)
		goto destroy;
	if (bus_dmamem_map(sc->sc_dmat, &mxm->mxm_seg, mxm->mxm_nsegs,
	    mxm->mxm_size, &mxm->mxm_kva, BUS_DMA_WAITOK) != 0)
		goto free;
	if (bus_dmamap_load(sc->sc_dmat, mxm->mxm_map, mxm->mxm_kva,
	    mxm->mxm_size, NULL, BUS_DMA_WAITOK) != 0)
		goto unmap;

	return (0);
unmap:
	bus_dmamem_unmap(sc->sc_dmat, mxm->mxm_kva, mxm->mxm_size);
free:
	bus_dmamem_free(sc->sc_dmat, &mxm->mxm_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, mxm->mxm_map);
	return (1);
}

static void
mcx_dmamem_zero(struct mcx_dmamem *mxm)
{
	memset(MCX_DMA_KVA(mxm), 0, MCX_DMA_LEN(mxm));
}

static void
mcx_dmamem_free(struct mcx_softc *sc, struct mcx_dmamem *mxm)
{
	bus_dmamap_unload(sc->sc_dmat, mxm->mxm_map);
	bus_dmamem_unmap(sc->sc_dmat, mxm->mxm_kva, mxm->mxm_size);
	bus_dmamem_free(sc->sc_dmat, &mxm->mxm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, mxm->mxm_map);
}

static int
mcx_hwmem_alloc(struct mcx_softc *sc, struct mcx_hwmem *mhm, unsigned int pages)
{
	bus_dma_segment_t *segs;
	bus_size_t len = pages * MCX_PAGE_SIZE;
	size_t seglen;

	segs = mallocarray(sizeof(*segs), pages, M_DEVBUF, M_WAITOK|M_CANFAIL);
	if (segs == NULL)
		return (-1);

	seglen = sizeof(*segs) * pages;

	if (bus_dmamem_alloc(sc->sc_dmat, len, MCX_PAGE_SIZE, 0,
	    segs, pages, &mhm->mhm_seg_count,
            BUS_DMA_NOWAIT|BUS_DMA_64BIT) != 0)
		goto free_segs;

	if (mhm->mhm_seg_count < pages) {
		size_t nseglen;

		mhm->mhm_segs = mallocarray(sizeof(*mhm->mhm_segs),
		    mhm->mhm_seg_count, M_DEVBUF, M_WAITOK|M_CANFAIL);
		if (mhm->mhm_segs == NULL)
			goto free_dmamem;

		nseglen = sizeof(*mhm->mhm_segs) * mhm->mhm_seg_count;

		memcpy(mhm->mhm_segs, segs, nseglen);

		free(segs, M_DEVBUF, seglen);

		segs = mhm->mhm_segs;
		seglen = nseglen;
	} else
		mhm->mhm_segs = segs;

	if (bus_dmamap_create(sc->sc_dmat, len, pages, MCX_PAGE_SIZE,
	    MCX_PAGE_SIZE, BUS_DMA_NOWAIT|BUS_DMA_ALLOCNOW|BUS_DMA_64BIT,
	    &mhm->mhm_map) != 0)
		goto free_dmamem;

	if (bus_dmamap_load_raw(sc->sc_dmat, mhm->mhm_map,
	    mhm->mhm_segs, mhm->mhm_seg_count, len, BUS_DMA_NOWAIT) != 0)
		goto destroy;

	bus_dmamap_sync(sc->sc_dmat, mhm->mhm_map,
	    0, mhm->mhm_map->dm_mapsize, BUS_DMASYNC_PRERW);

	mhm->mhm_npages = pages;

	return (0);

destroy:
	bus_dmamap_destroy(sc->sc_dmat, mhm->mhm_map);
free_dmamem:
	bus_dmamem_free(sc->sc_dmat, mhm->mhm_segs, mhm->mhm_seg_count);
free_segs:
	free(segs, M_DEVBUF, seglen);
	mhm->mhm_segs = NULL;

	return (-1);
}

static void
mcx_hwmem_free(struct mcx_softc *sc, struct mcx_hwmem *mhm)
{
	if (mhm->mhm_npages == 0)
		return;

	bus_dmamap_sync(sc->sc_dmat, mhm->mhm_map,
	    0, mhm->mhm_map->dm_mapsize, BUS_DMASYNC_POSTRW);

	bus_dmamap_unload(sc->sc_dmat, mhm->mhm_map);
	bus_dmamap_destroy(sc->sc_dmat, mhm->mhm_map);
	bus_dmamem_free(sc->sc_dmat, mhm->mhm_segs, mhm->mhm_seg_count);
	free(mhm->mhm_segs, M_DEVBUF,
	    sizeof(*mhm->mhm_segs) * mhm->mhm_seg_count);

	mhm->mhm_npages = 0;
}

#if NKSTAT > 0
struct mcx_ppcnt {
	char			 name[KSTAT_KV_NAMELEN];
	enum kstat_kv_unit	 unit;
};

static const struct mcx_ppcnt mcx_ppcnt_ieee8023_tpl[] = {
	{ "Good Tx",		KSTAT_KV_U_PACKETS, },
	{ "Good Rx",		KSTAT_KV_U_PACKETS, },
	{ "FCS errs",		KSTAT_KV_U_PACKETS, },
	{ "Alignment Errs",	KSTAT_KV_U_PACKETS, },
	{ "Good Tx",		KSTAT_KV_U_BYTES, },
	{ "Good Rx",		KSTAT_KV_U_BYTES, },
	{ "Multicast Tx",	KSTAT_KV_U_PACKETS, },
	{ "Broadcast Tx",	KSTAT_KV_U_PACKETS, },
	{ "Multicast Rx",	KSTAT_KV_U_PACKETS, },
	{ "Broadcast Rx",	KSTAT_KV_U_PACKETS, },
	{ "In Range Len",	KSTAT_KV_U_PACKETS, },
	{ "Out Of Range Len",	KSTAT_KV_U_PACKETS, },
	{ "Frame Too Long",	KSTAT_KV_U_PACKETS, },
	{ "Symbol Errs",	KSTAT_KV_U_PACKETS, },
	{ "MAC Ctrl Tx",	KSTAT_KV_U_PACKETS, },
	{ "MAC Ctrl Rx",	KSTAT_KV_U_PACKETS, },
	{ "MAC Ctrl Unsup",	KSTAT_KV_U_PACKETS, },
	{ "Pause Rx",		KSTAT_KV_U_PACKETS, },
	{ "Pause Tx",		KSTAT_KV_U_PACKETS, },
};
CTASSERT(nitems(mcx_ppcnt_ieee8023_tpl) == mcx_ppcnt_ieee8023_count);

static const struct mcx_ppcnt mcx_ppcnt_rfc2863_tpl[] = {
	{ "Rx Bytes",		KSTAT_KV_U_BYTES, },
	{ "Rx Unicast",		KSTAT_KV_U_PACKETS, },
	{ "Rx Discards",	KSTAT_KV_U_PACKETS, },
	{ "Rx Errors",		KSTAT_KV_U_PACKETS, },
	{ "Rx Unknown Proto",	KSTAT_KV_U_PACKETS, },
	{ "Tx Bytes",		KSTAT_KV_U_BYTES, },
	{ "Tx Unicast",		KSTAT_KV_U_PACKETS, },
	{ "Tx Discards",	KSTAT_KV_U_PACKETS, },
	{ "Tx Errors",		KSTAT_KV_U_PACKETS, },
	{ "Rx Multicast",	KSTAT_KV_U_PACKETS, },
	{ "Rx Broadcast",	KSTAT_KV_U_PACKETS, },
	{ "Tx Multicast",	KSTAT_KV_U_PACKETS, },
	{ "Tx Broadcast",	KSTAT_KV_U_PACKETS, },
};
CTASSERT(nitems(mcx_ppcnt_rfc2863_tpl) == mcx_ppcnt_rfc2863_count);

static const struct mcx_ppcnt mcx_ppcnt_rfc2819_tpl[] = {
	{ "Drop Events",	KSTAT_KV_U_PACKETS, },
	{ "Octets",		KSTAT_KV_U_BYTES, },
	{ "Packets",		KSTAT_KV_U_PACKETS, },
	{ "Broadcasts",		KSTAT_KV_U_PACKETS, },
	{ "Multicasts",		KSTAT_KV_U_PACKETS, },
	{ "CRC Align Errs",	KSTAT_KV_U_PACKETS, },
	{ "Undersize",		KSTAT_KV_U_PACKETS, },
	{ "Oversize",		KSTAT_KV_U_PACKETS, },
	{ "Fragments",		KSTAT_KV_U_PACKETS, },
	{ "Jabbers",		KSTAT_KV_U_PACKETS, },
	{ "Collisions",		KSTAT_KV_U_NONE, },
	{ "64B",		KSTAT_KV_U_PACKETS, },
	{ "65-127B",		KSTAT_KV_U_PACKETS, },
	{ "128-255B",		KSTAT_KV_U_PACKETS, },
	{ "256-511B",		KSTAT_KV_U_PACKETS, },
	{ "512-1023B",		KSTAT_KV_U_PACKETS, },
	{ "1024-1518B",		KSTAT_KV_U_PACKETS, },
	{ "1519-2047B",		KSTAT_KV_U_PACKETS, },
	{ "2048-4095B",		KSTAT_KV_U_PACKETS, },
	{ "4096-8191B",		KSTAT_KV_U_PACKETS, },
	{ "8192-10239B",	KSTAT_KV_U_PACKETS, },
};
CTASSERT(nitems(mcx_ppcnt_rfc2819_tpl) == mcx_ppcnt_rfc2819_count);

static const struct mcx_ppcnt mcx_ppcnt_rfc3635_tpl[] = {
	{ "Alignment Errs",	KSTAT_KV_U_PACKETS, },
	{ "FCS Errs",		KSTAT_KV_U_PACKETS, },
	{ "Single Colls",	KSTAT_KV_U_PACKETS, },
	{ "Multiple Colls",	KSTAT_KV_U_PACKETS, },
	{ "SQE Test Errs",	KSTAT_KV_U_NONE, },
	{ "Deferred Tx",	KSTAT_KV_U_PACKETS, },
	{ "Late Colls",		KSTAT_KV_U_NONE, },
	{ "Exess Colls",	KSTAT_KV_U_NONE, },
	{ "Int MAC Tx Errs",	KSTAT_KV_U_PACKETS, },
	{ "CSM Sense Errs",	KSTAT_KV_U_NONE, },
	{ "Too Long",		KSTAT_KV_U_PACKETS, },
	{ "Int MAC Rx Errs",	KSTAT_KV_U_PACKETS, },
	{ "Symbol Errs",	KSTAT_KV_U_NONE, },
	{ "Unknown Control",	KSTAT_KV_U_PACKETS, },
	{ "Pause Rx",		KSTAT_KV_U_PACKETS, },
	{ "Pause Tx",		KSTAT_KV_U_PACKETS, },
};
CTASSERT(nitems(mcx_ppcnt_rfc3635_tpl) == mcx_ppcnt_rfc3635_count);

struct mcx_kstat_ppcnt {
	const char		*ksp_name;
	const struct mcx_ppcnt	*ksp_tpl;
	unsigned int		 ksp_n;
	uint8_t			 ksp_grp;
};

static const struct mcx_kstat_ppcnt mcx_kstat_ppcnt_ieee8023 = {
	.ksp_name =		"ieee802.3",
	.ksp_tpl =		mcx_ppcnt_ieee8023_tpl,
	.ksp_n =		nitems(mcx_ppcnt_ieee8023_tpl),
	.ksp_grp =		MCX_REG_PPCNT_GRP_IEEE8023,
};

static const struct mcx_kstat_ppcnt mcx_kstat_ppcnt_rfc2863 = {
	.ksp_name =		"rfc2863",
	.ksp_tpl =		mcx_ppcnt_rfc2863_tpl,
	.ksp_n =		nitems(mcx_ppcnt_rfc2863_tpl),
	.ksp_grp =		MCX_REG_PPCNT_GRP_RFC2863,
};

static const struct mcx_kstat_ppcnt mcx_kstat_ppcnt_rfc2819 = {
	.ksp_name =		"rfc2819",
	.ksp_tpl =		mcx_ppcnt_rfc2819_tpl,
	.ksp_n =		nitems(mcx_ppcnt_rfc2819_tpl),
	.ksp_grp =		MCX_REG_PPCNT_GRP_RFC2819,
};

static const struct mcx_kstat_ppcnt mcx_kstat_ppcnt_rfc3635 = {
	.ksp_name =		"rfc3635",
	.ksp_tpl =		mcx_ppcnt_rfc3635_tpl,
	.ksp_n =		nitems(mcx_ppcnt_rfc3635_tpl),
	.ksp_grp =		MCX_REG_PPCNT_GRP_RFC3635,
};

static int	mcx_kstat_ppcnt_read(struct kstat *);

static void	mcx_kstat_attach_tmps(struct mcx_softc *sc);
static void	mcx_kstat_attach_queues(struct mcx_softc *sc);

static struct kstat *
mcx_kstat_attach_ppcnt(struct mcx_softc *sc,
    const struct mcx_kstat_ppcnt *ksp)
{
	struct kstat *ks;
	struct kstat_kv *kvs;
	unsigned int i;

	ks = kstat_create(DEVNAME(sc), 0, ksp->ksp_name, 0, KSTAT_T_KV, 0);
	if (ks == NULL)
		return (NULL);

	kvs = mallocarray(ksp->ksp_n, sizeof(*kvs),
	    M_DEVBUF, M_WAITOK);

	for (i = 0; i < ksp->ksp_n; i++) {
		const struct mcx_ppcnt *tpl = &ksp->ksp_tpl[i];

		kstat_kv_unit_init(&kvs[i], tpl->name,
		    KSTAT_KV_T_COUNTER64, tpl->unit);
	}

	ks->ks_softc = sc;
	ks->ks_ptr = (void *)ksp;
	ks->ks_data = kvs;
	ks->ks_datalen = ksp->ksp_n * sizeof(*kvs);
	ks->ks_read = mcx_kstat_ppcnt_read;
	kstat_set_wlock(ks, &sc->sc_cmdq_kstat_lk);

	kstat_install(ks);

	return (ks);
}

static void
mcx_kstat_attach(struct mcx_softc *sc)
{
	sc->sc_kstat_ieee8023 = mcx_kstat_attach_ppcnt(sc,
	    &mcx_kstat_ppcnt_ieee8023);
	sc->sc_kstat_rfc2863 = mcx_kstat_attach_ppcnt(sc,
	    &mcx_kstat_ppcnt_rfc2863);
	sc->sc_kstat_rfc2819 = mcx_kstat_attach_ppcnt(sc,
	    &mcx_kstat_ppcnt_rfc2819);
	sc->sc_kstat_rfc3635 = mcx_kstat_attach_ppcnt(sc,
	    &mcx_kstat_ppcnt_rfc3635);

	mcx_kstat_attach_tmps(sc);
	mcx_kstat_attach_queues(sc);
}

static int
mcx_kstat_ppcnt_read(struct kstat *ks)
{
	struct mcx_softc *sc = ks->ks_softc;
	struct mcx_kstat_ppcnt *ksp = ks->ks_ptr;
	struct mcx_reg_ppcnt ppcnt = {
		.ppcnt_grp = ksp->ksp_grp,
		.ppcnt_local_port = 1,
	};
	struct kstat_kv *kvs = ks->ks_data;
	uint64_t *vs = (uint64_t *)&ppcnt.ppcnt_counter_set;
	unsigned int i;
	int rv;

	rv = mcx_access_hca_reg(sc, MCX_REG_PPCNT, MCX_REG_OP_READ,
	    &ppcnt, sizeof(ppcnt), MCX_CMDQ_SLOT_KSTAT);
	if (rv != 0)
		return (EIO);

	nanouptime(&ks->ks_updated);

	for (i = 0; i < ksp->ksp_n; i++)
		kstat_kv_u64(&kvs[i]) = bemtoh64(&vs[i]);

	return (0);
}

struct mcx_kstat_mtmp {
	struct kstat_kv		ktmp_name;
	struct kstat_kv		ktmp_temperature;
	struct kstat_kv		ktmp_threshold_lo;
	struct kstat_kv		ktmp_threshold_hi;
};

static const struct mcx_kstat_mtmp mcx_kstat_mtmp_tpl = {
	KSTAT_KV_INITIALIZER("name",		KSTAT_KV_T_ISTR),
	KSTAT_KV_INITIALIZER("temperature",	KSTAT_KV_T_TEMP),
	KSTAT_KV_INITIALIZER("lo threshold",	KSTAT_KV_T_TEMP),
	KSTAT_KV_INITIALIZER("hi threshold",	KSTAT_KV_T_TEMP),
};

static const struct timeval mcx_kstat_mtmp_rate = { 1, 0 };

static int mcx_kstat_mtmp_read(struct kstat *);

static void
mcx_kstat_attach_tmps(struct mcx_softc *sc)
{
	struct kstat *ks;
	struct mcx_reg_mcam mcam;
	struct mcx_reg_mtcap mtcap;
	struct mcx_kstat_mtmp *ktmp;
	uint64_t map;
	unsigned int i, n;

	memset(&mtcap, 0, sizeof(mtcap));
	memset(&mcam, 0, sizeof(mcam));

	if (sc->sc_mcam_reg == 0) {
		/* no management capabilities */
		return;
	}

	if (mcx_access_hca_reg(sc, MCX_REG_MCAM, MCX_REG_OP_READ,
	    &mcam, sizeof(mcam), MCX_CMDQ_SLOT_POLL) != 0) {
		/* unable to check management capabilities? */
		return;
	}

	if (MCX_BITFIELD_BIT(mcam.mcam_feature_cap_mask,
	    MCX_MCAM_FEATURE_CAP_SENSOR_MAP) == 0) {
		/* no sensor map */
		return;
	}

	if (mcx_access_hca_reg(sc, MCX_REG_MTCAP, MCX_REG_OP_READ,
	    &mtcap, sizeof(mtcap), MCX_CMDQ_SLOT_POLL) != 0) {
		/* unable to find temperature sensors */
		return;
	}

	sc->sc_kstat_mtmp_count = mtcap.mtcap_sensor_count;
	sc->sc_kstat_mtmp = mallocarray(sc->sc_kstat_mtmp_count,
	    sizeof(*sc->sc_kstat_mtmp), M_DEVBUF, M_WAITOK);

	n = 0;
	map = bemtoh64(&mtcap.mtcap_sensor_map);
	for (i = 0; i < sizeof(map) * NBBY; i++) {
		if (!ISSET(map, (1ULL << i)))
			continue;

		ks = kstat_create(DEVNAME(sc), 0, "temperature", i,
		    KSTAT_T_KV, 0);
		if (ks == NULL) {
			/* unable to attach temperature sensor %u, i */
			continue;
		}

		ktmp = malloc(sizeof(*ktmp), M_DEVBUF, M_WAITOK|M_ZERO);
		*ktmp = mcx_kstat_mtmp_tpl;

		ks->ks_data = ktmp;
		ks->ks_datalen = sizeof(*ktmp);
		TIMEVAL_TO_TIMESPEC(&mcx_kstat_mtmp_rate, &ks->ks_interval);
		ks->ks_read = mcx_kstat_mtmp_read;
		kstat_set_wlock(ks, &sc->sc_cmdq_kstat_lk);

		ks->ks_softc = sc;
		kstat_install(ks);

		sc->sc_kstat_mtmp[n++] = ks;
		if (n >= sc->sc_kstat_mtmp_count)
			break;
	}
}

static uint64_t
mcx_tmp_to_uK(uint16_t *t)
{
	int64_t mt = (int16_t)bemtoh16(t); /* 0.125 C units */
	mt *= 1000000 / 8; /* convert to uC */
	mt += 273150000; /* convert to uK */

	return (mt);
}

static int
mcx_kstat_mtmp_read(struct kstat *ks)
{
	struct mcx_softc *sc = ks->ks_softc;
	struct mcx_kstat_mtmp *ktmp = ks->ks_data;
	struct mcx_reg_mtmp mtmp;
	int rv;
	struct timeval updated;

	TIMESPEC_TO_TIMEVAL(&updated, &ks->ks_updated);

	if (!ratecheck(&updated, &mcx_kstat_mtmp_rate))
		return (0);

	memset(&mtmp, 0, sizeof(mtmp));
	htobem16(&mtmp.mtmp_sensor_index, ks->ks_unit);

	rv = mcx_access_hca_reg(sc, MCX_REG_MTMP, MCX_REG_OP_READ,
	    &mtmp, sizeof(mtmp), MCX_CMDQ_SLOT_KSTAT);
	if (rv != 0)
		return (EIO);

	memset(kstat_kv_istr(&ktmp->ktmp_name), 0,
	    sizeof(kstat_kv_istr(&ktmp->ktmp_name)));
	memcpy(kstat_kv_istr(&ktmp->ktmp_name),
	    mtmp.mtmp_sensor_name, sizeof(mtmp.mtmp_sensor_name));
	kstat_kv_temp(&ktmp->ktmp_temperature) =
	    mcx_tmp_to_uK(&mtmp.mtmp_temperature);
	kstat_kv_temp(&ktmp->ktmp_threshold_lo) =
	    mcx_tmp_to_uK(&mtmp.mtmp_temperature_threshold_lo);
	kstat_kv_temp(&ktmp->ktmp_threshold_hi) =
	    mcx_tmp_to_uK(&mtmp.mtmp_temperature_threshold_hi);

	TIMEVAL_TO_TIMESPEC(&updated, &ks->ks_updated);

	return (0);
}

struct mcx_queuestat {
	char			 name[KSTAT_KV_NAMELEN];
	enum kstat_kv_type	 type;
};

static const struct mcx_queuestat mcx_queue_kstat_tpl[] = {
	{ "RQ SW prod",		KSTAT_KV_T_COUNTER64 },
	{ "RQ HW prod",		KSTAT_KV_T_COUNTER64 },
	{ "RQ HW cons",		KSTAT_KV_T_COUNTER64 },
	{ "RQ HW state",	KSTAT_KV_T_ISTR },

	{ "SQ SW prod",		KSTAT_KV_T_COUNTER64 },
	{ "SQ SW cons",		KSTAT_KV_T_COUNTER64 },
	{ "SQ HW prod",		KSTAT_KV_T_COUNTER64 },
	{ "SQ HW cons",		KSTAT_KV_T_COUNTER64 },
	{ "SQ HW state",	KSTAT_KV_T_ISTR },

	{ "CQ SW cons",		KSTAT_KV_T_COUNTER64 },
	{ "CQ HW prod",		KSTAT_KV_T_COUNTER64 },
	{ "CQ HW cons",		KSTAT_KV_T_COUNTER64 },
	{ "CQ HW notify",	KSTAT_KV_T_COUNTER64 },
	{ "CQ HW solicit",	KSTAT_KV_T_COUNTER64 },
	{ "CQ HW status",	KSTAT_KV_T_ISTR },
	{ "CQ HW state",	KSTAT_KV_T_ISTR },

	{ "EQ SW cons",		KSTAT_KV_T_COUNTER64 },
	{ "EQ HW prod",		KSTAT_KV_T_COUNTER64 },
	{ "EQ HW cons",		KSTAT_KV_T_COUNTER64 },
	{ "EQ HW status",	KSTAT_KV_T_ISTR },
	{ "EQ HW state",	KSTAT_KV_T_ISTR },
};

static int	mcx_kstat_queue_read(struct kstat *);

static void
mcx_kstat_attach_queues(struct mcx_softc *sc)
{
	struct kstat *ks;
	struct kstat_kv *kvs;
	int q, i;

	for (q = 0; q < intrmap_count(sc->sc_intrmap); q++) {
		ks = kstat_create(DEVNAME(sc), 0, "mcx-queues", q,
		    KSTAT_T_KV, 0);
		if (ks == NULL) {
			/* unable to attach queue stats %u, q */
			continue;
		}

		kvs = mallocarray(nitems(mcx_queue_kstat_tpl),
		    sizeof(*kvs), M_DEVBUF, M_WAITOK);

		for (i = 0; i < nitems(mcx_queue_kstat_tpl); i++) {
			const struct mcx_queuestat *tpl =
			    &mcx_queue_kstat_tpl[i];

			kstat_kv_init(&kvs[i], tpl->name, tpl->type);
		}

		ks->ks_softc = &sc->sc_queues[q];
		ks->ks_data = kvs;
		ks->ks_datalen = nitems(mcx_queue_kstat_tpl) * sizeof(*kvs);
		ks->ks_read = mcx_kstat_queue_read;

		sc->sc_queues[q].q_kstat = ks;
		kstat_install(ks);
	}
}

static int
mcx_kstat_queue_read(struct kstat *ks)
{
	struct mcx_queues *q = ks->ks_softc;
	struct mcx_softc *sc = q->q_sc;
	struct kstat_kv *kvs = ks->ks_data;
	union {
		struct mcx_rq_ctx rq;
		struct mcx_sq_ctx sq;
		struct mcx_cq_ctx cq;
		struct mcx_eq_ctx eq;
	} u;
	const char *text;
	int error = 0;

	if (mcx_query_rq(sc, &q->q_rx, &u.rq) != 0) {
		error = EIO;
		goto out;
	}

	kstat_kv_u64(kvs++) = q->q_rx.rx_prod;
	kstat_kv_u64(kvs++) = bemtoh32(&u.rq.rq_wq.wq_sw_counter);
	kstat_kv_u64(kvs++) = bemtoh32(&u.rq.rq_wq.wq_hw_counter);
	switch ((bemtoh32(&u.rq.rq_flags) & MCX_RQ_CTX_STATE_MASK) >>
	    MCX_RQ_CTX_STATE_SHIFT) {
	case MCX_RQ_CTX_STATE_RST:
		text = "RST";
		break;
	case MCX_RQ_CTX_STATE_RDY:
		text = "RDY";
		break;
	case MCX_RQ_CTX_STATE_ERR:
		text = "ERR";
		break;
	default:
		text = "unknown";
		break;
	}
	strlcpy(kstat_kv_istr(kvs), text, sizeof(kstat_kv_istr(kvs)));
	kvs++;

	if (mcx_query_sq(sc, &q->q_tx, &u.sq) != 0) {
		error = EIO;
		goto out;
	}

	kstat_kv_u64(kvs++) = q->q_tx.tx_prod;
	kstat_kv_u64(kvs++) = q->q_tx.tx_cons;
	kstat_kv_u64(kvs++) = bemtoh32(&u.sq.sq_wq.wq_sw_counter);
	kstat_kv_u64(kvs++) = bemtoh32(&u.sq.sq_wq.wq_hw_counter);
	switch ((bemtoh32(&u.sq.sq_flags) & MCX_SQ_CTX_STATE_MASK) >>
	    MCX_SQ_CTX_STATE_SHIFT) {
	case MCX_SQ_CTX_STATE_RST:
		text = "RST";
		break;
	case MCX_SQ_CTX_STATE_RDY:
		text = "RDY";
		break;
	case MCX_SQ_CTX_STATE_ERR:
		text = "ERR";
		break;
	default:
		text = "unknown";
		break;
	}
	strlcpy(kstat_kv_istr(kvs), text, sizeof(kstat_kv_istr(kvs)));
	kvs++;

	if (mcx_query_cq(sc, &q->q_cq, &u.cq) != 0) {
		error = EIO;
		goto out;
	}

	kstat_kv_u64(kvs++) = q->q_cq.cq_cons;
	kstat_kv_u64(kvs++) = bemtoh32(&u.cq.cq_producer_counter);
	kstat_kv_u64(kvs++) = bemtoh32(&u.cq.cq_consumer_counter);
	kstat_kv_u64(kvs++) = bemtoh32(&u.cq.cq_last_notified);
	kstat_kv_u64(kvs++) = bemtoh32(&u.cq.cq_last_solicit);

	switch ((bemtoh32(&u.cq.cq_status) & MCX_CQ_CTX_STATUS_MASK) >>
	    MCX_CQ_CTX_STATUS_SHIFT) {
	case MCX_CQ_CTX_STATUS_OK:
		text = "OK";
		break;
	case MCX_CQ_CTX_STATUS_OVERFLOW:
		text = "overflow";
		break;
	case MCX_CQ_CTX_STATUS_WRITE_FAIL:
		text = "write fail";
		break;
	default:
		text = "unknown";
		break;
	}
	strlcpy(kstat_kv_istr(kvs), text, sizeof(kstat_kv_istr(kvs)));
	kvs++;

	switch ((bemtoh32(&u.cq.cq_status) & MCX_CQ_CTX_STATE_MASK) >>
	    MCX_CQ_CTX_STATE_SHIFT) {
	case MCX_CQ_CTX_STATE_SOLICITED:
		text = "solicited";
		break;
	case MCX_CQ_CTX_STATE_ARMED:
		text = "armed";
		break;
	case MCX_CQ_CTX_STATE_FIRED:
		text = "fired";
		break;
	default:
		text = "unknown";
		break;
	}
	strlcpy(kstat_kv_istr(kvs), text, sizeof(kstat_kv_istr(kvs)));
	kvs++;

	if (mcx_query_eq(sc, &q->q_eq, &u.eq) != 0) {
		error = EIO;
		goto out;
	}

	kstat_kv_u64(kvs++) = q->q_eq.eq_cons;
	kstat_kv_u64(kvs++) = bemtoh32(&u.eq.eq_producer_counter);
	kstat_kv_u64(kvs++) = bemtoh32(&u.eq.eq_consumer_counter);

	switch ((bemtoh32(&u.eq.eq_status) & MCX_EQ_CTX_STATUS_MASK) >>
	    MCX_EQ_CTX_STATUS_SHIFT) {
	case MCX_EQ_CTX_STATUS_EQ_WRITE_FAILURE:
		text = "write fail";
		break;
	case MCX_EQ_CTX_STATUS_OK:
		text = "OK";
		break;
	default:
		text = "unknown";
		break;
	}
	strlcpy(kstat_kv_istr(kvs), text, sizeof(kstat_kv_istr(kvs)));
	kvs++;

	switch ((bemtoh32(&u.eq.eq_status) & MCX_EQ_CTX_STATE_MASK) >>
	    MCX_EQ_CTX_STATE_SHIFT) {
	case MCX_EQ_CTX_STATE_ARMED:
		text = "armed";
		break;
	case MCX_EQ_CTX_STATE_FIRED:
		text = "fired";
		break;
	default:
		text = "unknown";
		break;
	}
	strlcpy(kstat_kv_istr(kvs), text, sizeof(kstat_kv_istr(kvs)));
	kvs++;

	nanouptime(&ks->ks_updated);
out:
	return (error);
}

#endif /* NKSTAT > 0 */

static unsigned int
mcx_timecounter_read(struct timecounter *tc)
{
	struct mcx_softc *sc = tc->tc_priv;

	return (mcx_rd(sc, MCX_INTERNAL_TIMER_L));
}

static void
mcx_timecounter_attach(struct mcx_softc *sc)
{
	struct timecounter *tc = &sc->sc_timecounter;

	tc->tc_get_timecount = mcx_timecounter_read;
	tc->tc_counter_mask = ~0U;
	tc->tc_frequency = sc->sc_khz * 1000;
	tc->tc_name = sc->sc_dev.dv_xname;
	tc->tc_quality = -100;
	tc->tc_priv = sc;

	tc_init(tc);
}

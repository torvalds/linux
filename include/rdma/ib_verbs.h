/*
 * Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2004 Infinicon Corporation.  All rights reserved.
 * Copyright (c) 2004 Intel Corporation.  All rights reserved.
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2004 Voltaire Corporation.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#if !defined(IB_VERBS_H)
#define IB_VERBS_H

#include <linux/types.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/rwsem.h>
#include <linux/scatterlist.h>
#include <linux/workqueue.h>
#include <uapi/linux/if_ether.h>

#include <linux/atomic.h>
#include <asm/uaccess.h>

extern struct workqueue_struct *ib_wq;

union ib_gid {
	u8	raw[16];
	struct {
		__be64	subnet_prefix;
		__be64	interface_id;
	} global;
};

enum rdma_node_type {
	/* IB values map to NodeInfo:NodeType. */
	RDMA_NODE_IB_CA 	= 1,
	RDMA_NODE_IB_SWITCH,
	RDMA_NODE_IB_ROUTER,
	RDMA_NODE_RNIC,
	RDMA_NODE_USNIC,
	RDMA_NODE_USNIC_UDP,
};

enum rdma_transport_type {
	RDMA_TRANSPORT_IB,
	RDMA_TRANSPORT_IWARP,
	RDMA_TRANSPORT_USNIC,
	RDMA_TRANSPORT_USNIC_UDP
};

__attribute_const__ enum rdma_transport_type
rdma_node_get_transport(enum rdma_node_type node_type);

enum rdma_link_layer {
	IB_LINK_LAYER_UNSPECIFIED,
	IB_LINK_LAYER_INFINIBAND,
	IB_LINK_LAYER_ETHERNET,
};

enum ib_device_cap_flags {
	IB_DEVICE_RESIZE_MAX_WR		= 1,
	IB_DEVICE_BAD_PKEY_CNTR		= (1<<1),
	IB_DEVICE_BAD_QKEY_CNTR		= (1<<2),
	IB_DEVICE_RAW_MULTI		= (1<<3),
	IB_DEVICE_AUTO_PATH_MIG		= (1<<4),
	IB_DEVICE_CHANGE_PHY_PORT	= (1<<5),
	IB_DEVICE_UD_AV_PORT_ENFORCE	= (1<<6),
	IB_DEVICE_CURR_QP_STATE_MOD	= (1<<7),
	IB_DEVICE_SHUTDOWN_PORT		= (1<<8),
	IB_DEVICE_INIT_TYPE		= (1<<9),
	IB_DEVICE_PORT_ACTIVE_EVENT	= (1<<10),
	IB_DEVICE_SYS_IMAGE_GUID	= (1<<11),
	IB_DEVICE_RC_RNR_NAK_GEN	= (1<<12),
	IB_DEVICE_SRQ_RESIZE		= (1<<13),
	IB_DEVICE_N_NOTIFY_CQ		= (1<<14),
	IB_DEVICE_LOCAL_DMA_LKEY	= (1<<15),
	IB_DEVICE_RESERVED		= (1<<16), /* old SEND_W_INV */
	IB_DEVICE_MEM_WINDOW		= (1<<17),
	/*
	 * Devices should set IB_DEVICE_UD_IP_SUM if they support
	 * insertion of UDP and TCP checksum on outgoing UD IPoIB
	 * messages and can verify the validity of checksum for
	 * incoming messages.  Setting this flag implies that the
	 * IPoIB driver may set NETIF_F_IP_CSUM for datagram mode.
	 */
	IB_DEVICE_UD_IP_CSUM		= (1<<18),
	IB_DEVICE_UD_TSO		= (1<<19),
	IB_DEVICE_XRC			= (1<<20),
	IB_DEVICE_MEM_MGT_EXTENSIONS	= (1<<21),
	IB_DEVICE_BLOCK_MULTICAST_LOOPBACK = (1<<22),
	IB_DEVICE_MEM_WINDOW_TYPE_2A	= (1<<23),
	IB_DEVICE_MEM_WINDOW_TYPE_2B	= (1<<24),
	IB_DEVICE_MANAGED_FLOW_STEERING = (1<<29),
	IB_DEVICE_SIGNATURE_HANDOVER	= (1<<30)
};

enum ib_signature_prot_cap {
	IB_PROT_T10DIF_TYPE_1 = 1,
	IB_PROT_T10DIF_TYPE_2 = 1 << 1,
	IB_PROT_T10DIF_TYPE_3 = 1 << 2,
};

enum ib_signature_guard_cap {
	IB_GUARD_T10DIF_CRC	= 1,
	IB_GUARD_T10DIF_CSUM	= 1 << 1,
};

enum ib_atomic_cap {
	IB_ATOMIC_NONE,
	IB_ATOMIC_HCA,
	IB_ATOMIC_GLOB
};

struct ib_device_attr {
	u64			fw_ver;
	__be64			sys_image_guid;
	u64			max_mr_size;
	u64			page_size_cap;
	u32			vendor_id;
	u32			vendor_part_id;
	u32			hw_ver;
	int			max_qp;
	int			max_qp_wr;
	int			device_cap_flags;
	int			max_sge;
	int			max_sge_rd;
	int			max_cq;
	int			max_cqe;
	int			max_mr;
	int			max_pd;
	int			max_qp_rd_atom;
	int			max_ee_rd_atom;
	int			max_res_rd_atom;
	int			max_qp_init_rd_atom;
	int			max_ee_init_rd_atom;
	enum ib_atomic_cap	atomic_cap;
	enum ib_atomic_cap	masked_atomic_cap;
	int			max_ee;
	int			max_rdd;
	int			max_mw;
	int			max_raw_ipv6_qp;
	int			max_raw_ethy_qp;
	int			max_mcast_grp;
	int			max_mcast_qp_attach;
	int			max_total_mcast_qp_attach;
	int			max_ah;
	int			max_fmr;
	int			max_map_per_fmr;
	int			max_srq;
	int			max_srq_wr;
	int			max_srq_sge;
	unsigned int		max_fast_reg_page_list_len;
	u16			max_pkeys;
	u8			local_ca_ack_delay;
	int			sig_prot_cap;
	int			sig_guard_cap;
};

enum ib_mtu {
	IB_MTU_256  = 1,
	IB_MTU_512  = 2,
	IB_MTU_1024 = 3,
	IB_MTU_2048 = 4,
	IB_MTU_4096 = 5
};

static inline int ib_mtu_enum_to_int(enum ib_mtu mtu)
{
	switch (mtu) {
	case IB_MTU_256:  return  256;
	case IB_MTU_512:  return  512;
	case IB_MTU_1024: return 1024;
	case IB_MTU_2048: return 2048;
	case IB_MTU_4096: return 4096;
	default: 	  return -1;
	}
}

enum ib_port_state {
	IB_PORT_NOP		= 0,
	IB_PORT_DOWN		= 1,
	IB_PORT_INIT		= 2,
	IB_PORT_ARMED		= 3,
	IB_PORT_ACTIVE		= 4,
	IB_PORT_ACTIVE_DEFER	= 5
};

enum ib_port_cap_flags {
	IB_PORT_SM				= 1 <<  1,
	IB_PORT_NOTICE_SUP			= 1 <<  2,
	IB_PORT_TRAP_SUP			= 1 <<  3,
	IB_PORT_OPT_IPD_SUP                     = 1 <<  4,
	IB_PORT_AUTO_MIGR_SUP			= 1 <<  5,
	IB_PORT_SL_MAP_SUP			= 1 <<  6,
	IB_PORT_MKEY_NVRAM			= 1 <<  7,
	IB_PORT_PKEY_NVRAM			= 1 <<  8,
	IB_PORT_LED_INFO_SUP			= 1 <<  9,
	IB_PORT_SM_DISABLED			= 1 << 10,
	IB_PORT_SYS_IMAGE_GUID_SUP		= 1 << 11,
	IB_PORT_PKEY_SW_EXT_PORT_TRAP_SUP	= 1 << 12,
	IB_PORT_EXTENDED_SPEEDS_SUP             = 1 << 14,
	IB_PORT_CM_SUP				= 1 << 16,
	IB_PORT_SNMP_TUNNEL_SUP			= 1 << 17,
	IB_PORT_REINIT_SUP			= 1 << 18,
	IB_PORT_DEVICE_MGMT_SUP			= 1 << 19,
	IB_PORT_VENDOR_CLASS_SUP		= 1 << 20,
	IB_PORT_DR_NOTICE_SUP			= 1 << 21,
	IB_PORT_CAP_MASK_NOTICE_SUP		= 1 << 22,
	IB_PORT_BOOT_MGMT_SUP			= 1 << 23,
	IB_PORT_LINK_LATENCY_SUP		= 1 << 24,
	IB_PORT_CLIENT_REG_SUP			= 1 << 25,
	IB_PORT_IP_BASED_GIDS			= 1 << 26
};

enum ib_port_width {
	IB_WIDTH_1X	= 1,
	IB_WIDTH_4X	= 2,
	IB_WIDTH_8X	= 4,
	IB_WIDTH_12X	= 8
};

static inline int ib_width_enum_to_int(enum ib_port_width width)
{
	switch (width) {
	case IB_WIDTH_1X:  return  1;
	case IB_WIDTH_4X:  return  4;
	case IB_WIDTH_8X:  return  8;
	case IB_WIDTH_12X: return 12;
	default: 	  return -1;
	}
}

enum ib_port_speed {
	IB_SPEED_SDR	= 1,
	IB_SPEED_DDR	= 2,
	IB_SPEED_QDR	= 4,
	IB_SPEED_FDR10	= 8,
	IB_SPEED_FDR	= 16,
	IB_SPEED_EDR	= 32
};

struct ib_protocol_stats {
	/* TBD... */
};

struct iw_protocol_stats {
	u64	ipInReceives;
	u64	ipInHdrErrors;
	u64	ipInTooBigErrors;
	u64	ipInNoRoutes;
	u64	ipInAddrErrors;
	u64	ipInUnknownProtos;
	u64	ipInTruncatedPkts;
	u64	ipInDiscards;
	u64	ipInDelivers;
	u64	ipOutForwDatagrams;
	u64	ipOutRequests;
	u64	ipOutDiscards;
	u64	ipOutNoRoutes;
	u64	ipReasmTimeout;
	u64	ipReasmReqds;
	u64	ipReasmOKs;
	u64	ipReasmFails;
	u64	ipFragOKs;
	u64	ipFragFails;
	u64	ipFragCreates;
	u64	ipInMcastPkts;
	u64	ipOutMcastPkts;
	u64	ipInBcastPkts;
	u64	ipOutBcastPkts;

	u64	tcpRtoAlgorithm;
	u64	tcpRtoMin;
	u64	tcpRtoMax;
	u64	tcpMaxConn;
	u64	tcpActiveOpens;
	u64	tcpPassiveOpens;
	u64	tcpAttemptFails;
	u64	tcpEstabResets;
	u64	tcpCurrEstab;
	u64	tcpInSegs;
	u64	tcpOutSegs;
	u64	tcpRetransSegs;
	u64	tcpInErrs;
	u64	tcpOutRsts;
};

union rdma_protocol_stats {
	struct ib_protocol_stats	ib;
	struct iw_protocol_stats	iw;
};

struct ib_port_attr {
	enum ib_port_state	state;
	enum ib_mtu		max_mtu;
	enum ib_mtu		active_mtu;
	int			gid_tbl_len;
	u32			port_cap_flags;
	u32			max_msg_sz;
	u32			bad_pkey_cntr;
	u32			qkey_viol_cntr;
	u16			pkey_tbl_len;
	u16			lid;
	u16			sm_lid;
	u8			lmc;
	u8			max_vl_num;
	u8			sm_sl;
	u8			subnet_timeout;
	u8			init_type_reply;
	u8			active_width;
	u8			active_speed;
	u8                      phys_state;
};

enum ib_device_modify_flags {
	IB_DEVICE_MODIFY_SYS_IMAGE_GUID	= 1 << 0,
	IB_DEVICE_MODIFY_NODE_DESC	= 1 << 1
};

struct ib_device_modify {
	u64	sys_image_guid;
	char	node_desc[64];
};

enum ib_port_modify_flags {
	IB_PORT_SHUTDOWN		= 1,
	IB_PORT_INIT_TYPE		= (1<<2),
	IB_PORT_RESET_QKEY_CNTR		= (1<<3)
};

struct ib_port_modify {
	u32	set_port_cap_mask;
	u32	clr_port_cap_mask;
	u8	init_type;
};

enum ib_event_type {
	IB_EVENT_CQ_ERR,
	IB_EVENT_QP_FATAL,
	IB_EVENT_QP_REQ_ERR,
	IB_EVENT_QP_ACCESS_ERR,
	IB_EVENT_COMM_EST,
	IB_EVENT_SQ_DRAINED,
	IB_EVENT_PATH_MIG,
	IB_EVENT_PATH_MIG_ERR,
	IB_EVENT_DEVICE_FATAL,
	IB_EVENT_PORT_ACTIVE,
	IB_EVENT_PORT_ERR,
	IB_EVENT_LID_CHANGE,
	IB_EVENT_PKEY_CHANGE,
	IB_EVENT_SM_CHANGE,
	IB_EVENT_SRQ_ERR,
	IB_EVENT_SRQ_LIMIT_REACHED,
	IB_EVENT_QP_LAST_WQE_REACHED,
	IB_EVENT_CLIENT_REREGISTER,
	IB_EVENT_GID_CHANGE,
};

struct ib_event {
	struct ib_device	*device;
	union {
		struct ib_cq	*cq;
		struct ib_qp	*qp;
		struct ib_srq	*srq;
		u8		port_num;
	} element;
	enum ib_event_type	event;
};

struct ib_event_handler {
	struct ib_device *device;
	void            (*handler)(struct ib_event_handler *, struct ib_event *);
	struct list_head  list;
};

#define INIT_IB_EVENT_HANDLER(_ptr, _device, _handler)		\
	do {							\
		(_ptr)->device  = _device;			\
		(_ptr)->handler = _handler;			\
		INIT_LIST_HEAD(&(_ptr)->list);			\
	} while (0)

struct ib_global_route {
	union ib_gid	dgid;
	u32		flow_label;
	u8		sgid_index;
	u8		hop_limit;
	u8		traffic_class;
};

struct ib_grh {
	__be32		version_tclass_flow;
	__be16		paylen;
	u8		next_hdr;
	u8		hop_limit;
	union ib_gid	sgid;
	union ib_gid	dgid;
};

enum {
	IB_MULTICAST_QPN = 0xffffff
};

#define IB_LID_PERMISSIVE	cpu_to_be16(0xFFFF)

enum ib_ah_flags {
	IB_AH_GRH	= 1
};

enum ib_rate {
	IB_RATE_PORT_CURRENT = 0,
	IB_RATE_2_5_GBPS = 2,
	IB_RATE_5_GBPS   = 5,
	IB_RATE_10_GBPS  = 3,
	IB_RATE_20_GBPS  = 6,
	IB_RATE_30_GBPS  = 4,
	IB_RATE_40_GBPS  = 7,
	IB_RATE_60_GBPS  = 8,
	IB_RATE_80_GBPS  = 9,
	IB_RATE_120_GBPS = 10,
	IB_RATE_14_GBPS  = 11,
	IB_RATE_56_GBPS  = 12,
	IB_RATE_112_GBPS = 13,
	IB_RATE_168_GBPS = 14,
	IB_RATE_25_GBPS  = 15,
	IB_RATE_100_GBPS = 16,
	IB_RATE_200_GBPS = 17,
	IB_RATE_300_GBPS = 18
};

/**
 * ib_rate_to_mult - Convert the IB rate enum to a multiple of the
 * base rate of 2.5 Gbit/sec.  For example, IB_RATE_5_GBPS will be
 * converted to 2, since 5 Gbit/sec is 2 * 2.5 Gbit/sec.
 * @rate: rate to convert.
 */
__attribute_const__ int ib_rate_to_mult(enum ib_rate rate);

/**
 * ib_rate_to_mbps - Convert the IB rate enum to Mbps.
 * For example, IB_RATE_2_5_GBPS will be converted to 2500.
 * @rate: rate to convert.
 */
__attribute_const__ int ib_rate_to_mbps(enum ib_rate rate);

enum ib_mr_create_flags {
	IB_MR_SIGNATURE_EN = 1,
};

/**
 * ib_mr_init_attr - Memory region init attributes passed to routine
 *     ib_create_mr.
 * @max_reg_descriptors: max number of registration descriptors that
 *     may be used with registration work requests.
 * @flags: MR creation flags bit mask.
 */
struct ib_mr_init_attr {
	int	    max_reg_descriptors;
	u32	    flags;
};

enum ib_signature_type {
	IB_SIG_TYPE_T10_DIF,
};

/**
 * T10-DIF Signature types
 * T10-DIF types are defined by SCSI
 * specifications.
 */
enum ib_t10_dif_type {
	IB_T10DIF_NONE,
	IB_T10DIF_TYPE1,
	IB_T10DIF_TYPE2,
	IB_T10DIF_TYPE3
};

/**
 * Signature T10-DIF block-guard types
 * IB_T10DIF_CRC: Corresponds to T10-PI mandated CRC checksum rules.
 * IB_T10DIF_CSUM: Corresponds to IP checksum rules.
 */
enum ib_t10_dif_bg_type {
	IB_T10DIF_CRC,
	IB_T10DIF_CSUM
};

/**
 * struct ib_t10_dif_domain - Parameters specific for T10-DIF
 *     domain.
 * @type: T10-DIF type (0|1|2|3)
 * @bg_type: T10-DIF block guard type (CRC|CSUM)
 * @pi_interval: protection information interval.
 * @bg: seed of guard computation.
 * @app_tag: application tag of guard block
 * @ref_tag: initial guard block reference tag.
 * @type3_inc_reftag: T10-DIF type 3 does not state
 *     about the reference tag, it is the user
 *     choice to increment it or not.
 */
struct ib_t10_dif_domain {
	enum ib_t10_dif_type	type;
	enum ib_t10_dif_bg_type bg_type;
	u16			pi_interval;
	u16			bg;
	u16			app_tag;
	u32			ref_tag;
	bool			type3_inc_reftag;
};

/**
 * struct ib_sig_domain - Parameters for signature domain
 * @sig_type: specific signauture type
 * @sig: union of all signature domain attributes that may
 *     be used to set domain layout.
 */
struct ib_sig_domain {
	enum ib_signature_type sig_type;
	union {
		struct ib_t10_dif_domain dif;
	} sig;
};

/**
 * struct ib_sig_attrs - Parameters for signature handover operation
 * @check_mask: bitmask for signature byte check (8 bytes)
 * @mem: memory domain layout desciptor.
 * @wire: wire domain layout desciptor.
 */
struct ib_sig_attrs {
	u8			check_mask;
	struct ib_sig_domain	mem;
	struct ib_sig_domain	wire;
};

enum ib_sig_err_type {
	IB_SIG_BAD_GUARD,
	IB_SIG_BAD_REFTAG,
	IB_SIG_BAD_APPTAG,
};

/**
 * struct ib_sig_err - signature error descriptor
 */
struct ib_sig_err {
	enum ib_sig_err_type	err_type;
	u32			expected;
	u32			actual;
	u64			sig_err_offset;
	u32			key;
};

enum ib_mr_status_check {
	IB_MR_CHECK_SIG_STATUS = 1,
};

/**
 * struct ib_mr_status - Memory region status container
 *
 * @fail_status: Bitmask of MR checks status. For each
 *     failed check a corresponding status bit is set.
 * @sig_err: Additional info for IB_MR_CEHCK_SIG_STATUS
 *     failure.
 */
struct ib_mr_status {
	u32		    fail_status;
	struct ib_sig_err   sig_err;
};

/**
 * mult_to_ib_rate - Convert a multiple of 2.5 Gbit/sec to an IB rate
 * enum.
 * @mult: multiple to convert.
 */
__attribute_const__ enum ib_rate mult_to_ib_rate(int mult);

struct ib_ah_attr {
	struct ib_global_route	grh;
	u16			dlid;
	u8			sl;
	u8			src_path_bits;
	u8			static_rate;
	u8			ah_flags;
	u8			port_num;
	u8			dmac[ETH_ALEN];
	u16			vlan_id;
};

enum ib_wc_status {
	IB_WC_SUCCESS,
	IB_WC_LOC_LEN_ERR,
	IB_WC_LOC_QP_OP_ERR,
	IB_WC_LOC_EEC_OP_ERR,
	IB_WC_LOC_PROT_ERR,
	IB_WC_WR_FLUSH_ERR,
	IB_WC_MW_BIND_ERR,
	IB_WC_BAD_RESP_ERR,
	IB_WC_LOC_ACCESS_ERR,
	IB_WC_REM_INV_REQ_ERR,
	IB_WC_REM_ACCESS_ERR,
	IB_WC_REM_OP_ERR,
	IB_WC_RETRY_EXC_ERR,
	IB_WC_RNR_RETRY_EXC_ERR,
	IB_WC_LOC_RDD_VIOL_ERR,
	IB_WC_REM_INV_RD_REQ_ERR,
	IB_WC_REM_ABORT_ERR,
	IB_WC_INV_EECN_ERR,
	IB_WC_INV_EEC_STATE_ERR,
	IB_WC_FATAL_ERR,
	IB_WC_RESP_TIMEOUT_ERR,
	IB_WC_GENERAL_ERR
};

enum ib_wc_opcode {
	IB_WC_SEND,
	IB_WC_RDMA_WRITE,
	IB_WC_RDMA_READ,
	IB_WC_COMP_SWAP,
	IB_WC_FETCH_ADD,
	IB_WC_BIND_MW,
	IB_WC_LSO,
	IB_WC_LOCAL_INV,
	IB_WC_FAST_REG_MR,
	IB_WC_MASKED_COMP_SWAP,
	IB_WC_MASKED_FETCH_ADD,
/*
 * Set value of IB_WC_RECV so consumers can test if a completion is a
 * receive by testing (opcode & IB_WC_RECV).
 */
	IB_WC_RECV			= 1 << 7,
	IB_WC_RECV_RDMA_WITH_IMM
};

enum ib_wc_flags {
	IB_WC_GRH		= 1,
	IB_WC_WITH_IMM		= (1<<1),
	IB_WC_WITH_INVALIDATE	= (1<<2),
	IB_WC_IP_CSUM_OK	= (1<<3),
	IB_WC_WITH_SMAC		= (1<<4),
	IB_WC_WITH_VLAN		= (1<<5),
};

struct ib_wc {
	u64			wr_id;
	enum ib_wc_status	status;
	enum ib_wc_opcode	opcode;
	u32			vendor_err;
	u32			byte_len;
	struct ib_qp	       *qp;
	union {
		__be32		imm_data;
		u32		invalidate_rkey;
	} ex;
	u32			src_qp;
	int			wc_flags;
	u16			pkey_index;
	u16			slid;
	u8			sl;
	u8			dlid_path_bits;
	u8			port_num;	/* valid only for DR SMPs on switches */
	u8			smac[ETH_ALEN];
	u16			vlan_id;
};

enum ib_cq_notify_flags {
	IB_CQ_SOLICITED			= 1 << 0,
	IB_CQ_NEXT_COMP			= 1 << 1,
	IB_CQ_SOLICITED_MASK		= IB_CQ_SOLICITED | IB_CQ_NEXT_COMP,
	IB_CQ_REPORT_MISSED_EVENTS	= 1 << 2,
};

enum ib_srq_type {
	IB_SRQT_BASIC,
	IB_SRQT_XRC
};

enum ib_srq_attr_mask {
	IB_SRQ_MAX_WR	= 1 << 0,
	IB_SRQ_LIMIT	= 1 << 1,
};

struct ib_srq_attr {
	u32	max_wr;
	u32	max_sge;
	u32	srq_limit;
};

struct ib_srq_init_attr {
	void		      (*event_handler)(struct ib_event *, void *);
	void		       *srq_context;
	struct ib_srq_attr	attr;
	enum ib_srq_type	srq_type;

	union {
		struct {
			struct ib_xrcd *xrcd;
			struct ib_cq   *cq;
		} xrc;
	} ext;
};

struct ib_qp_cap {
	u32	max_send_wr;
	u32	max_recv_wr;
	u32	max_send_sge;
	u32	max_recv_sge;
	u32	max_inline_data;
};

enum ib_sig_type {
	IB_SIGNAL_ALL_WR,
	IB_SIGNAL_REQ_WR
};

enum ib_qp_type {
	/*
	 * IB_QPT_SMI and IB_QPT_GSI have to be the first two entries
	 * here (and in that order) since the MAD layer uses them as
	 * indices into a 2-entry table.
	 */
	IB_QPT_SMI,
	IB_QPT_GSI,

	IB_QPT_RC,
	IB_QPT_UC,
	IB_QPT_UD,
	IB_QPT_RAW_IPV6,
	IB_QPT_RAW_ETHERTYPE,
	IB_QPT_RAW_PACKET = 8,
	IB_QPT_XRC_INI = 9,
	IB_QPT_XRC_TGT,
	IB_QPT_MAX,
	/* Reserve a range for qp types internal to the low level driver.
	 * These qp types will not be visible at the IB core layer, so the
	 * IB_QPT_MAX usages should not be affected in the core layer
	 */
	IB_QPT_RESERVED1 = 0x1000,
	IB_QPT_RESERVED2,
	IB_QPT_RESERVED3,
	IB_QPT_RESERVED4,
	IB_QPT_RESERVED5,
	IB_QPT_RESERVED6,
	IB_QPT_RESERVED7,
	IB_QPT_RESERVED8,
	IB_QPT_RESERVED9,
	IB_QPT_RESERVED10,
};

enum ib_qp_create_flags {
	IB_QP_CREATE_IPOIB_UD_LSO		= 1 << 0,
	IB_QP_CREATE_BLOCK_MULTICAST_LOOPBACK	= 1 << 1,
	IB_QP_CREATE_NETIF_QP			= 1 << 5,
	IB_QP_CREATE_SIGNATURE_EN		= 1 << 6,
	IB_QP_CREATE_USE_GFP_NOIO		= 1 << 7,
	/* reserve bits 26-31 for low level drivers' internal use */
	IB_QP_CREATE_RESERVED_START		= 1 << 26,
	IB_QP_CREATE_RESERVED_END		= 1 << 31,
};


/*
 * Note: users may not call ib_close_qp or ib_destroy_qp from the event_handler
 * callback to destroy the passed in QP.
 */

struct ib_qp_init_attr {
	void                  (*event_handler)(struct ib_event *, void *);
	void		       *qp_context;
	struct ib_cq	       *send_cq;
	struct ib_cq	       *recv_cq;
	struct ib_srq	       *srq;
	struct ib_xrcd	       *xrcd;     /* XRC TGT QPs only */
	struct ib_qp_cap	cap;
	enum ib_sig_type	sq_sig_type;
	enum ib_qp_type		qp_type;
	enum ib_qp_create_flags	create_flags;
	u8			port_num; /* special QP types only */
};

struct ib_qp_open_attr {
	void                  (*event_handler)(struct ib_event *, void *);
	void		       *qp_context;
	u32			qp_num;
	enum ib_qp_type		qp_type;
};

enum ib_rnr_timeout {
	IB_RNR_TIMER_655_36 =  0,
	IB_RNR_TIMER_000_01 =  1,
	IB_RNR_TIMER_000_02 =  2,
	IB_RNR_TIMER_000_03 =  3,
	IB_RNR_TIMER_000_04 =  4,
	IB_RNR_TIMER_000_06 =  5,
	IB_RNR_TIMER_000_08 =  6,
	IB_RNR_TIMER_000_12 =  7,
	IB_RNR_TIMER_000_16 =  8,
	IB_RNR_TIMER_000_24 =  9,
	IB_RNR_TIMER_000_32 = 10,
	IB_RNR_TIMER_000_48 = 11,
	IB_RNR_TIMER_000_64 = 12,
	IB_RNR_TIMER_000_96 = 13,
	IB_RNR_TIMER_001_28 = 14,
	IB_RNR_TIMER_001_92 = 15,
	IB_RNR_TIMER_002_56 = 16,
	IB_RNR_TIMER_003_84 = 17,
	IB_RNR_TIMER_005_12 = 18,
	IB_RNR_TIMER_007_68 = 19,
	IB_RNR_TIMER_010_24 = 20,
	IB_RNR_TIMER_015_36 = 21,
	IB_RNR_TIMER_020_48 = 22,
	IB_RNR_TIMER_030_72 = 23,
	IB_RNR_TIMER_040_96 = 24,
	IB_RNR_TIMER_061_44 = 25,
	IB_RNR_TIMER_081_92 = 26,
	IB_RNR_TIMER_122_88 = 27,
	IB_RNR_TIMER_163_84 = 28,
	IB_RNR_TIMER_245_76 = 29,
	IB_RNR_TIMER_327_68 = 30,
	IB_RNR_TIMER_491_52 = 31
};

enum ib_qp_attr_mask {
	IB_QP_STATE			= 1,
	IB_QP_CUR_STATE			= (1<<1),
	IB_QP_EN_SQD_ASYNC_NOTIFY	= (1<<2),
	IB_QP_ACCESS_FLAGS		= (1<<3),
	IB_QP_PKEY_INDEX		= (1<<4),
	IB_QP_PORT			= (1<<5),
	IB_QP_QKEY			= (1<<6),
	IB_QP_AV			= (1<<7),
	IB_QP_PATH_MTU			= (1<<8),
	IB_QP_TIMEOUT			= (1<<9),
	IB_QP_RETRY_CNT			= (1<<10),
	IB_QP_RNR_RETRY			= (1<<11),
	IB_QP_RQ_PSN			= (1<<12),
	IB_QP_MAX_QP_RD_ATOMIC		= (1<<13),
	IB_QP_ALT_PATH			= (1<<14),
	IB_QP_MIN_RNR_TIMER		= (1<<15),
	IB_QP_SQ_PSN			= (1<<16),
	IB_QP_MAX_DEST_RD_ATOMIC	= (1<<17),
	IB_QP_PATH_MIG_STATE		= (1<<18),
	IB_QP_CAP			= (1<<19),
	IB_QP_DEST_QPN			= (1<<20),
	IB_QP_SMAC			= (1<<21),
	IB_QP_ALT_SMAC			= (1<<22),
	IB_QP_VID			= (1<<23),
	IB_QP_ALT_VID			= (1<<24),
};

enum ib_qp_state {
	IB_QPS_RESET,
	IB_QPS_INIT,
	IB_QPS_RTR,
	IB_QPS_RTS,
	IB_QPS_SQD,
	IB_QPS_SQE,
	IB_QPS_ERR
};

enum ib_mig_state {
	IB_MIG_MIGRATED,
	IB_MIG_REARM,
	IB_MIG_ARMED
};

enum ib_mw_type {
	IB_MW_TYPE_1 = 1,
	IB_MW_TYPE_2 = 2
};

struct ib_qp_attr {
	enum ib_qp_state	qp_state;
	enum ib_qp_state	cur_qp_state;
	enum ib_mtu		path_mtu;
	enum ib_mig_state	path_mig_state;
	u32			qkey;
	u32			rq_psn;
	u32			sq_psn;
	u32			dest_qp_num;
	int			qp_access_flags;
	struct ib_qp_cap	cap;
	struct ib_ah_attr	ah_attr;
	struct ib_ah_attr	alt_ah_attr;
	u16			pkey_index;
	u16			alt_pkey_index;
	u8			en_sqd_async_notify;
	u8			sq_draining;
	u8			max_rd_atomic;
	u8			max_dest_rd_atomic;
	u8			min_rnr_timer;
	u8			port_num;
	u8			timeout;
	u8			retry_cnt;
	u8			rnr_retry;
	u8			alt_port_num;
	u8			alt_timeout;
	u8			smac[ETH_ALEN];
	u8			alt_smac[ETH_ALEN];
	u16			vlan_id;
	u16			alt_vlan_id;
};

enum ib_wr_opcode {
	IB_WR_RDMA_WRITE,
	IB_WR_RDMA_WRITE_WITH_IMM,
	IB_WR_SEND,
	IB_WR_SEND_WITH_IMM,
	IB_WR_RDMA_READ,
	IB_WR_ATOMIC_CMP_AND_SWP,
	IB_WR_ATOMIC_FETCH_AND_ADD,
	IB_WR_LSO,
	IB_WR_SEND_WITH_INV,
	IB_WR_RDMA_READ_WITH_INV,
	IB_WR_LOCAL_INV,
	IB_WR_FAST_REG_MR,
	IB_WR_MASKED_ATOMIC_CMP_AND_SWP,
	IB_WR_MASKED_ATOMIC_FETCH_AND_ADD,
	IB_WR_BIND_MW,
	IB_WR_REG_SIG_MR,
	/* reserve values for low level drivers' internal use.
	 * These values will not be used at all in the ib core layer.
	 */
	IB_WR_RESERVED1 = 0xf0,
	IB_WR_RESERVED2,
	IB_WR_RESERVED3,
	IB_WR_RESERVED4,
	IB_WR_RESERVED5,
	IB_WR_RESERVED6,
	IB_WR_RESERVED7,
	IB_WR_RESERVED8,
	IB_WR_RESERVED9,
	IB_WR_RESERVED10,
};

enum ib_send_flags {
	IB_SEND_FENCE		= 1,
	IB_SEND_SIGNALED	= (1<<1),
	IB_SEND_SOLICITED	= (1<<2),
	IB_SEND_INLINE		= (1<<3),
	IB_SEND_IP_CSUM		= (1<<4),

	/* reserve bits 26-31 for low level drivers' internal use */
	IB_SEND_RESERVED_START	= (1 << 26),
	IB_SEND_RESERVED_END	= (1 << 31),
};

struct ib_sge {
	u64	addr;
	u32	length;
	u32	lkey;
};

struct ib_fast_reg_page_list {
	struct ib_device       *device;
	u64		       *page_list;
	unsigned int		max_page_list_len;
};

/**
 * struct ib_mw_bind_info - Parameters for a memory window bind operation.
 * @mr: A memory region to bind the memory window to.
 * @addr: The address where the memory window should begin.
 * @length: The length of the memory window, in bytes.
 * @mw_access_flags: Access flags from enum ib_access_flags for the window.
 *
 * This struct contains the shared parameters for type 1 and type 2
 * memory window bind operations.
 */
struct ib_mw_bind_info {
	struct ib_mr   *mr;
	u64		addr;
	u64		length;
	int		mw_access_flags;
};

struct ib_send_wr {
	struct ib_send_wr      *next;
	u64			wr_id;
	struct ib_sge	       *sg_list;
	int			num_sge;
	enum ib_wr_opcode	opcode;
	int			send_flags;
	union {
		__be32		imm_data;
		u32		invalidate_rkey;
	} ex;
	union {
		struct {
			u64	remote_addr;
			u32	rkey;
		} rdma;
		struct {
			u64	remote_addr;
			u64	compare_add;
			u64	swap;
			u64	compare_add_mask;
			u64	swap_mask;
			u32	rkey;
		} atomic;
		struct {
			struct ib_ah *ah;
			void   *header;
			int     hlen;
			int     mss;
			u32	remote_qpn;
			u32	remote_qkey;
			u16	pkey_index; /* valid for GSI only */
			u8	port_num;   /* valid for DR SMPs on switch only */
		} ud;
		struct {
			u64				iova_start;
			struct ib_fast_reg_page_list   *page_list;
			unsigned int			page_shift;
			unsigned int			page_list_len;
			u32				length;
			int				access_flags;
			u32				rkey;
		} fast_reg;
		struct {
			struct ib_mw            *mw;
			/* The new rkey for the memory window. */
			u32                      rkey;
			struct ib_mw_bind_info   bind_info;
		} bind_mw;
		struct {
			struct ib_sig_attrs    *sig_attrs;
			struct ib_mr	       *sig_mr;
			int			access_flags;
			struct ib_sge	       *prot;
		} sig_handover;
	} wr;
	u32			xrc_remote_srq_num;	/* XRC TGT QPs only */
};

struct ib_recv_wr {
	struct ib_recv_wr      *next;
	u64			wr_id;
	struct ib_sge	       *sg_list;
	int			num_sge;
};

enum ib_access_flags {
	IB_ACCESS_LOCAL_WRITE	= 1,
	IB_ACCESS_REMOTE_WRITE	= (1<<1),
	IB_ACCESS_REMOTE_READ	= (1<<2),
	IB_ACCESS_REMOTE_ATOMIC	= (1<<3),
	IB_ACCESS_MW_BIND	= (1<<4),
	IB_ZERO_BASED		= (1<<5)
};

struct ib_phys_buf {
	u64      addr;
	u64      size;
};

struct ib_mr_attr {
	struct ib_pd	*pd;
	u64		device_virt_addr;
	u64		size;
	int		mr_access_flags;
	u32		lkey;
	u32		rkey;
};

enum ib_mr_rereg_flags {
	IB_MR_REREG_TRANS	= 1,
	IB_MR_REREG_PD		= (1<<1),
	IB_MR_REREG_ACCESS	= (1<<2),
	IB_MR_REREG_SUPPORTED	= ((IB_MR_REREG_ACCESS << 1) - 1)
};

/**
 * struct ib_mw_bind - Parameters for a type 1 memory window bind operation.
 * @wr_id:      Work request id.
 * @send_flags: Flags from ib_send_flags enum.
 * @bind_info:  More parameters of the bind operation.
 */
struct ib_mw_bind {
	u64                    wr_id;
	int                    send_flags;
	struct ib_mw_bind_info bind_info;
};

struct ib_fmr_attr {
	int	max_pages;
	int	max_maps;
	u8	page_shift;
};

struct ib_ucontext {
	struct ib_device       *device;
	struct list_head	pd_list;
	struct list_head	mr_list;
	struct list_head	mw_list;
	struct list_head	cq_list;
	struct list_head	qp_list;
	struct list_head	srq_list;
	struct list_head	ah_list;
	struct list_head	xrcd_list;
	struct list_head	rule_list;
	int			closing;
};

struct ib_uobject {
	u64			user_handle;	/* handle given to us by userspace */
	struct ib_ucontext     *context;	/* associated user context */
	void		       *object;		/* containing object */
	struct list_head	list;		/* link to context's list */
	int			id;		/* index into kernel idr */
	struct kref		ref;
	struct rw_semaphore	mutex;		/* protects .live */
	int			live;
};

struct ib_udata {
	const void __user *inbuf;
	void __user *outbuf;
	size_t       inlen;
	size_t       outlen;
};

struct ib_pd {
	struct ib_device       *device;
	struct ib_uobject      *uobject;
	atomic_t          	usecnt; /* count all resources */
};

struct ib_xrcd {
	struct ib_device       *device;
	atomic_t		usecnt; /* count all exposed resources */
	struct inode	       *inode;

	struct mutex		tgt_qp_mutex;
	struct list_head	tgt_qp_list;
};

struct ib_ah {
	struct ib_device	*device;
	struct ib_pd		*pd;
	struct ib_uobject	*uobject;
};

typedef void (*ib_comp_handler)(struct ib_cq *cq, void *cq_context);

struct ib_cq {
	struct ib_device       *device;
	struct ib_uobject      *uobject;
	ib_comp_handler   	comp_handler;
	void                  (*event_handler)(struct ib_event *, void *);
	void                   *cq_context;
	int               	cqe;
	atomic_t          	usecnt; /* count number of work queues */
};

struct ib_srq {
	struct ib_device       *device;
	struct ib_pd	       *pd;
	struct ib_uobject      *uobject;
	void		      (*event_handler)(struct ib_event *, void *);
	void		       *srq_context;
	enum ib_srq_type	srq_type;
	atomic_t		usecnt;

	union {
		struct {
			struct ib_xrcd *xrcd;
			struct ib_cq   *cq;
			u32		srq_num;
		} xrc;
	} ext;
};

struct ib_qp {
	struct ib_device       *device;
	struct ib_pd	       *pd;
	struct ib_cq	       *send_cq;
	struct ib_cq	       *recv_cq;
	struct ib_srq	       *srq;
	struct ib_xrcd	       *xrcd; /* XRC TGT QPs only */
	struct list_head	xrcd_list;
	/* count times opened, mcast attaches, flow attaches */
	atomic_t		usecnt;
	struct list_head	open_list;
	struct ib_qp           *real_qp;
	struct ib_uobject      *uobject;
	void                  (*event_handler)(struct ib_event *, void *);
	void		       *qp_context;
	u32			qp_num;
	enum ib_qp_type		qp_type;
};

struct ib_mr {
	struct ib_device  *device;
	struct ib_pd	  *pd;
	struct ib_uobject *uobject;
	u32		   lkey;
	u32		   rkey;
	atomic_t	   usecnt; /* count number of MWs */
};

struct ib_mw {
	struct ib_device	*device;
	struct ib_pd		*pd;
	struct ib_uobject	*uobject;
	u32			rkey;
	enum ib_mw_type         type;
};

struct ib_fmr {
	struct ib_device	*device;
	struct ib_pd		*pd;
	struct list_head	list;
	u32			lkey;
	u32			rkey;
};

/* Supported steering options */
enum ib_flow_attr_type {
	/* steering according to rule specifications */
	IB_FLOW_ATTR_NORMAL		= 0x0,
	/* default unicast and multicast rule -
	 * receive all Eth traffic which isn't steered to any QP
	 */
	IB_FLOW_ATTR_ALL_DEFAULT	= 0x1,
	/* default multicast rule -
	 * receive all Eth multicast traffic which isn't steered to any QP
	 */
	IB_FLOW_ATTR_MC_DEFAULT		= 0x2,
	/* sniffer rule - receive all port traffic */
	IB_FLOW_ATTR_SNIFFER		= 0x3
};

/* Supported steering header types */
enum ib_flow_spec_type {
	/* L2 headers*/
	IB_FLOW_SPEC_ETH	= 0x20,
	IB_FLOW_SPEC_IB		= 0x22,
	/* L3 header*/
	IB_FLOW_SPEC_IPV4	= 0x30,
	/* L4 headers*/
	IB_FLOW_SPEC_TCP	= 0x40,
	IB_FLOW_SPEC_UDP	= 0x41
};
#define IB_FLOW_SPEC_LAYER_MASK	0xF0
#define IB_FLOW_SPEC_SUPPORT_LAYERS 4

/* Flow steering rule priority is set according to it's domain.
 * Lower domain value means higher priority.
 */
enum ib_flow_domain {
	IB_FLOW_DOMAIN_USER,
	IB_FLOW_DOMAIN_ETHTOOL,
	IB_FLOW_DOMAIN_RFS,
	IB_FLOW_DOMAIN_NIC,
	IB_FLOW_DOMAIN_NUM /* Must be last */
};

struct ib_flow_eth_filter {
	u8	dst_mac[6];
	u8	src_mac[6];
	__be16	ether_type;
	__be16	vlan_tag;
};

struct ib_flow_spec_eth {
	enum ib_flow_spec_type	  type;
	u16			  size;
	struct ib_flow_eth_filter val;
	struct ib_flow_eth_filter mask;
};

struct ib_flow_ib_filter {
	__be16 dlid;
	__u8   sl;
};

struct ib_flow_spec_ib {
	enum ib_flow_spec_type	 type;
	u16			 size;
	struct ib_flow_ib_filter val;
	struct ib_flow_ib_filter mask;
};

struct ib_flow_ipv4_filter {
	__be32	src_ip;
	__be32	dst_ip;
};

struct ib_flow_spec_ipv4 {
	enum ib_flow_spec_type	   type;
	u16			   size;
	struct ib_flow_ipv4_filter val;
	struct ib_flow_ipv4_filter mask;
};

struct ib_flow_tcp_udp_filter {
	__be16	dst_port;
	__be16	src_port;
};

struct ib_flow_spec_tcp_udp {
	enum ib_flow_spec_type	      type;
	u16			      size;
	struct ib_flow_tcp_udp_filter val;
	struct ib_flow_tcp_udp_filter mask;
};

union ib_flow_spec {
	struct {
		enum ib_flow_spec_type	type;
		u16			size;
	};
	struct ib_flow_spec_eth		eth;
	struct ib_flow_spec_ib		ib;
	struct ib_flow_spec_ipv4        ipv4;
	struct ib_flow_spec_tcp_udp	tcp_udp;
};

struct ib_flow_attr {
	enum ib_flow_attr_type type;
	u16	     size;
	u16	     priority;
	u32	     flags;
	u8	     num_of_specs;
	u8	     port;
	/* Following are the optional layers according to user request
	 * struct ib_flow_spec_xxx
	 * struct ib_flow_spec_yyy
	 */
};

struct ib_flow {
	struct ib_qp		*qp;
	struct ib_uobject	*uobject;
};

struct ib_mad;
struct ib_grh;

enum ib_process_mad_flags {
	IB_MAD_IGNORE_MKEY	= 1,
	IB_MAD_IGNORE_BKEY	= 2,
	IB_MAD_IGNORE_ALL	= IB_MAD_IGNORE_MKEY | IB_MAD_IGNORE_BKEY
};

enum ib_mad_result {
	IB_MAD_RESULT_FAILURE  = 0,      /* (!SUCCESS is the important flag) */
	IB_MAD_RESULT_SUCCESS  = 1 << 0, /* MAD was successfully processed   */
	IB_MAD_RESULT_REPLY    = 1 << 1, /* Reply packet needs to be sent    */
	IB_MAD_RESULT_CONSUMED = 1 << 2  /* Packet consumed: stop processing */
};

#define IB_DEVICE_NAME_MAX 64

struct ib_cache {
	rwlock_t                lock;
	struct ib_event_handler event_handler;
	struct ib_pkey_cache  **pkey_cache;
	struct ib_gid_cache   **gid_cache;
	u8                     *lmc_cache;
};

struct ib_dma_mapping_ops {
	int		(*mapping_error)(struct ib_device *dev,
					 u64 dma_addr);
	u64		(*map_single)(struct ib_device *dev,
				      void *ptr, size_t size,
				      enum dma_data_direction direction);
	void		(*unmap_single)(struct ib_device *dev,
					u64 addr, size_t size,
					enum dma_data_direction direction);
	u64		(*map_page)(struct ib_device *dev,
				    struct page *page, unsigned long offset,
				    size_t size,
				    enum dma_data_direction direction);
	void		(*unmap_page)(struct ib_device *dev,
				      u64 addr, size_t size,
				      enum dma_data_direction direction);
	int		(*map_sg)(struct ib_device *dev,
				  struct scatterlist *sg, int nents,
				  enum dma_data_direction direction);
	void		(*unmap_sg)(struct ib_device *dev,
				    struct scatterlist *sg, int nents,
				    enum dma_data_direction direction);
	void		(*sync_single_for_cpu)(struct ib_device *dev,
					       u64 dma_handle,
					       size_t size,
					       enum dma_data_direction dir);
	void		(*sync_single_for_device)(struct ib_device *dev,
						  u64 dma_handle,
						  size_t size,
						  enum dma_data_direction dir);
	void		*(*alloc_coherent)(struct ib_device *dev,
					   size_t size,
					   u64 *dma_handle,
					   gfp_t flag);
	void		(*free_coherent)(struct ib_device *dev,
					 size_t size, void *cpu_addr,
					 u64 dma_handle);
};

struct iw_cm_verbs;

struct ib_device {
	struct device                *dma_device;

	char                          name[IB_DEVICE_NAME_MAX];

	struct list_head              event_handler_list;
	spinlock_t                    event_handler_lock;

	spinlock_t                    client_data_lock;
	struct list_head              core_list;
	struct list_head              client_data_list;

	struct ib_cache               cache;
	int                          *pkey_tbl_len;
	int                          *gid_tbl_len;

	int			      num_comp_vectors;

	struct iw_cm_verbs	     *iwcm;

	int		           (*get_protocol_stats)(struct ib_device *device,
							 union rdma_protocol_stats *stats);
	int		           (*query_device)(struct ib_device *device,
						   struct ib_device_attr *device_attr);
	int		           (*query_port)(struct ib_device *device,
						 u8 port_num,
						 struct ib_port_attr *port_attr);
	enum rdma_link_layer	   (*get_link_layer)(struct ib_device *device,
						     u8 port_num);
	int		           (*query_gid)(struct ib_device *device,
						u8 port_num, int index,
						union ib_gid *gid);
	int		           (*query_pkey)(struct ib_device *device,
						 u8 port_num, u16 index, u16 *pkey);
	int		           (*modify_device)(struct ib_device *device,
						    int device_modify_mask,
						    struct ib_device_modify *device_modify);
	int		           (*modify_port)(struct ib_device *device,
						  u8 port_num, int port_modify_mask,
						  struct ib_port_modify *port_modify);
	struct ib_ucontext *       (*alloc_ucontext)(struct ib_device *device,
						     struct ib_udata *udata);
	int                        (*dealloc_ucontext)(struct ib_ucontext *context);
	int                        (*mmap)(struct ib_ucontext *context,
					   struct vm_area_struct *vma);
	struct ib_pd *             (*alloc_pd)(struct ib_device *device,
					       struct ib_ucontext *context,
					       struct ib_udata *udata);
	int                        (*dealloc_pd)(struct ib_pd *pd);
	struct ib_ah *             (*create_ah)(struct ib_pd *pd,
						struct ib_ah_attr *ah_attr);
	int                        (*modify_ah)(struct ib_ah *ah,
						struct ib_ah_attr *ah_attr);
	int                        (*query_ah)(struct ib_ah *ah,
					       struct ib_ah_attr *ah_attr);
	int                        (*destroy_ah)(struct ib_ah *ah);
	struct ib_srq *            (*create_srq)(struct ib_pd *pd,
						 struct ib_srq_init_attr *srq_init_attr,
						 struct ib_udata *udata);
	int                        (*modify_srq)(struct ib_srq *srq,
						 struct ib_srq_attr *srq_attr,
						 enum ib_srq_attr_mask srq_attr_mask,
						 struct ib_udata *udata);
	int                        (*query_srq)(struct ib_srq *srq,
						struct ib_srq_attr *srq_attr);
	int                        (*destroy_srq)(struct ib_srq *srq);
	int                        (*post_srq_recv)(struct ib_srq *srq,
						    struct ib_recv_wr *recv_wr,
						    struct ib_recv_wr **bad_recv_wr);
	struct ib_qp *             (*create_qp)(struct ib_pd *pd,
						struct ib_qp_init_attr *qp_init_attr,
						struct ib_udata *udata);
	int                        (*modify_qp)(struct ib_qp *qp,
						struct ib_qp_attr *qp_attr,
						int qp_attr_mask,
						struct ib_udata *udata);
	int                        (*query_qp)(struct ib_qp *qp,
					       struct ib_qp_attr *qp_attr,
					       int qp_attr_mask,
					       struct ib_qp_init_attr *qp_init_attr);
	int                        (*destroy_qp)(struct ib_qp *qp);
	int                        (*post_send)(struct ib_qp *qp,
						struct ib_send_wr *send_wr,
						struct ib_send_wr **bad_send_wr);
	int                        (*post_recv)(struct ib_qp *qp,
						struct ib_recv_wr *recv_wr,
						struct ib_recv_wr **bad_recv_wr);
	struct ib_cq *             (*create_cq)(struct ib_device *device, int cqe,
						int comp_vector,
						struct ib_ucontext *context,
						struct ib_udata *udata);
	int                        (*modify_cq)(struct ib_cq *cq, u16 cq_count,
						u16 cq_period);
	int                        (*destroy_cq)(struct ib_cq *cq);
	int                        (*resize_cq)(struct ib_cq *cq, int cqe,
						struct ib_udata *udata);
	int                        (*poll_cq)(struct ib_cq *cq, int num_entries,
					      struct ib_wc *wc);
	int                        (*peek_cq)(struct ib_cq *cq, int wc_cnt);
	int                        (*req_notify_cq)(struct ib_cq *cq,
						    enum ib_cq_notify_flags flags);
	int                        (*req_ncomp_notif)(struct ib_cq *cq,
						      int wc_cnt);
	struct ib_mr *             (*get_dma_mr)(struct ib_pd *pd,
						 int mr_access_flags);
	struct ib_mr *             (*reg_phys_mr)(struct ib_pd *pd,
						  struct ib_phys_buf *phys_buf_array,
						  int num_phys_buf,
						  int mr_access_flags,
						  u64 *iova_start);
	struct ib_mr *             (*reg_user_mr)(struct ib_pd *pd,
						  u64 start, u64 length,
						  u64 virt_addr,
						  int mr_access_flags,
						  struct ib_udata *udata);
	int			   (*rereg_user_mr)(struct ib_mr *mr,
						    int flags,
						    u64 start, u64 length,
						    u64 virt_addr,
						    int mr_access_flags,
						    struct ib_pd *pd,
						    struct ib_udata *udata);
	int                        (*query_mr)(struct ib_mr *mr,
					       struct ib_mr_attr *mr_attr);
	int                        (*dereg_mr)(struct ib_mr *mr);
	int                        (*destroy_mr)(struct ib_mr *mr);
	struct ib_mr *		   (*create_mr)(struct ib_pd *pd,
						struct ib_mr_init_attr *mr_init_attr);
	struct ib_mr *		   (*alloc_fast_reg_mr)(struct ib_pd *pd,
					       int max_page_list_len);
	struct ib_fast_reg_page_list * (*alloc_fast_reg_page_list)(struct ib_device *device,
								   int page_list_len);
	void			   (*free_fast_reg_page_list)(struct ib_fast_reg_page_list *page_list);
	int                        (*rereg_phys_mr)(struct ib_mr *mr,
						    int mr_rereg_mask,
						    struct ib_pd *pd,
						    struct ib_phys_buf *phys_buf_array,
						    int num_phys_buf,
						    int mr_access_flags,
						    u64 *iova_start);
	struct ib_mw *             (*alloc_mw)(struct ib_pd *pd,
					       enum ib_mw_type type);
	int                        (*bind_mw)(struct ib_qp *qp,
					      struct ib_mw *mw,
					      struct ib_mw_bind *mw_bind);
	int                        (*dealloc_mw)(struct ib_mw *mw);
	struct ib_fmr *	           (*alloc_fmr)(struct ib_pd *pd,
						int mr_access_flags,
						struct ib_fmr_attr *fmr_attr);
	int		           (*map_phys_fmr)(struct ib_fmr *fmr,
						   u64 *page_list, int list_len,
						   u64 iova);
	int		           (*unmap_fmr)(struct list_head *fmr_list);
	int		           (*dealloc_fmr)(struct ib_fmr *fmr);
	int                        (*attach_mcast)(struct ib_qp *qp,
						   union ib_gid *gid,
						   u16 lid);
	int                        (*detach_mcast)(struct ib_qp *qp,
						   union ib_gid *gid,
						   u16 lid);
	int                        (*process_mad)(struct ib_device *device,
						  int process_mad_flags,
						  u8 port_num,
						  struct ib_wc *in_wc,
						  struct ib_grh *in_grh,
						  struct ib_mad *in_mad,
						  struct ib_mad *out_mad);
	struct ib_xrcd *	   (*alloc_xrcd)(struct ib_device *device,
						 struct ib_ucontext *ucontext,
						 struct ib_udata *udata);
	int			   (*dealloc_xrcd)(struct ib_xrcd *xrcd);
	struct ib_flow *	   (*create_flow)(struct ib_qp *qp,
						  struct ib_flow_attr
						  *flow_attr,
						  int domain);
	int			   (*destroy_flow)(struct ib_flow *flow_id);
	int			   (*check_mr_status)(struct ib_mr *mr, u32 check_mask,
						      struct ib_mr_status *mr_status);

	struct ib_dma_mapping_ops   *dma_ops;

	struct module               *owner;
	struct device                dev;
	struct kobject               *ports_parent;
	struct list_head             port_list;

	enum {
		IB_DEV_UNINITIALIZED,
		IB_DEV_REGISTERED,
		IB_DEV_UNREGISTERED
	}                            reg_state;

	int			     uverbs_abi_ver;
	u64			     uverbs_cmd_mask;
	u64			     uverbs_ex_cmd_mask;

	char			     node_desc[64];
	__be64			     node_guid;
	u32			     local_dma_lkey;
	u8                           node_type;
	u8                           phys_port_cnt;
};

struct ib_client {
	char  *name;
	void (*add)   (struct ib_device *);
	void (*remove)(struct ib_device *);

	struct list_head list;
};

struct ib_device *ib_alloc_device(size_t size);
void ib_dealloc_device(struct ib_device *device);

int ib_register_device(struct ib_device *device,
		       int (*port_callback)(struct ib_device *,
					    u8, struct kobject *));
void ib_unregister_device(struct ib_device *device);

int ib_register_client   (struct ib_client *client);
void ib_unregister_client(struct ib_client *client);

void *ib_get_client_data(struct ib_device *device, struct ib_client *client);
void  ib_set_client_data(struct ib_device *device, struct ib_client *client,
			 void *data);

static inline int ib_copy_from_udata(void *dest, struct ib_udata *udata, size_t len)
{
	return copy_from_user(dest, udata->inbuf, len) ? -EFAULT : 0;
}

static inline int ib_copy_to_udata(struct ib_udata *udata, void *src, size_t len)
{
	return copy_to_user(udata->outbuf, src, len) ? -EFAULT : 0;
}

/**
 * ib_modify_qp_is_ok - Check that the supplied attribute mask
 * contains all required attributes and no attributes not allowed for
 * the given QP state transition.
 * @cur_state: Current QP state
 * @next_state: Next QP state
 * @type: QP type
 * @mask: Mask of supplied QP attributes
 * @ll : link layer of port
 *
 * This function is a helper function that a low-level driver's
 * modify_qp method can use to validate the consumer's input.  It
 * checks that cur_state and next_state are valid QP states, that a
 * transition from cur_state to next_state is allowed by the IB spec,
 * and that the attribute mask supplied is allowed for the transition.
 */
int ib_modify_qp_is_ok(enum ib_qp_state cur_state, enum ib_qp_state next_state,
		       enum ib_qp_type type, enum ib_qp_attr_mask mask,
		       enum rdma_link_layer ll);

int ib_register_event_handler  (struct ib_event_handler *event_handler);
int ib_unregister_event_handler(struct ib_event_handler *event_handler);
void ib_dispatch_event(struct ib_event *event);

int ib_query_device(struct ib_device *device,
		    struct ib_device_attr *device_attr);

int ib_query_port(struct ib_device *device,
		  u8 port_num, struct ib_port_attr *port_attr);

enum rdma_link_layer rdma_port_get_link_layer(struct ib_device *device,
					       u8 port_num);

int ib_query_gid(struct ib_device *device,
		 u8 port_num, int index, union ib_gid *gid);

int ib_query_pkey(struct ib_device *device,
		  u8 port_num, u16 index, u16 *pkey);

int ib_modify_device(struct ib_device *device,
		     int device_modify_mask,
		     struct ib_device_modify *device_modify);

int ib_modify_port(struct ib_device *device,
		   u8 port_num, int port_modify_mask,
		   struct ib_port_modify *port_modify);

int ib_find_gid(struct ib_device *device, union ib_gid *gid,
		u8 *port_num, u16 *index);

int ib_find_pkey(struct ib_device *device,
		 u8 port_num, u16 pkey, u16 *index);

/**
 * ib_alloc_pd - Allocates an unused protection domain.
 * @device: The device on which to allocate the protection domain.
 *
 * A protection domain object provides an association between QPs, shared
 * receive queues, address handles, memory regions, and memory windows.
 */
struct ib_pd *ib_alloc_pd(struct ib_device *device);

/**
 * ib_dealloc_pd - Deallocates a protection domain.
 * @pd: The protection domain to deallocate.
 */
int ib_dealloc_pd(struct ib_pd *pd);

/**
 * ib_create_ah - Creates an address handle for the given address vector.
 * @pd: The protection domain associated with the address handle.
 * @ah_attr: The attributes of the address vector.
 *
 * The address handle is used to reference a local or global destination
 * in all UD QP post sends.
 */
struct ib_ah *ib_create_ah(struct ib_pd *pd, struct ib_ah_attr *ah_attr);

/**
 * ib_init_ah_from_wc - Initializes address handle attributes from a
 *   work completion.
 * @device: Device on which the received message arrived.
 * @port_num: Port on which the received message arrived.
 * @wc: Work completion associated with the received message.
 * @grh: References the received global route header.  This parameter is
 *   ignored unless the work completion indicates that the GRH is valid.
 * @ah_attr: Returned attributes that can be used when creating an address
 *   handle for replying to the message.
 */
int ib_init_ah_from_wc(struct ib_device *device, u8 port_num, struct ib_wc *wc,
		       struct ib_grh *grh, struct ib_ah_attr *ah_attr);

/**
 * ib_create_ah_from_wc - Creates an address handle associated with the
 *   sender of the specified work completion.
 * @pd: The protection domain associated with the address handle.
 * @wc: Work completion information associated with a received message.
 * @grh: References the received global route header.  This parameter is
 *   ignored unless the work completion indicates that the GRH is valid.
 * @port_num: The outbound port number to associate with the address.
 *
 * The address handle is used to reference a local or global destination
 * in all UD QP post sends.
 */
struct ib_ah *ib_create_ah_from_wc(struct ib_pd *pd, struct ib_wc *wc,
				   struct ib_grh *grh, u8 port_num);

/**
 * ib_modify_ah - Modifies the address vector associated with an address
 *   handle.
 * @ah: The address handle to modify.
 * @ah_attr: The new address vector attributes to associate with the
 *   address handle.
 */
int ib_modify_ah(struct ib_ah *ah, struct ib_ah_attr *ah_attr);

/**
 * ib_query_ah - Queries the address vector associated with an address
 *   handle.
 * @ah: The address handle to query.
 * @ah_attr: The address vector attributes associated with the address
 *   handle.
 */
int ib_query_ah(struct ib_ah *ah, struct ib_ah_attr *ah_attr);

/**
 * ib_destroy_ah - Destroys an address handle.
 * @ah: The address handle to destroy.
 */
int ib_destroy_ah(struct ib_ah *ah);

/**
 * ib_create_srq - Creates a SRQ associated with the specified protection
 *   domain.
 * @pd: The protection domain associated with the SRQ.
 * @srq_init_attr: A list of initial attributes required to create the
 *   SRQ.  If SRQ creation succeeds, then the attributes are updated to
 *   the actual capabilities of the created SRQ.
 *
 * srq_attr->max_wr and srq_attr->max_sge are read the determine the
 * requested size of the SRQ, and set to the actual values allocated
 * on return.  If ib_create_srq() succeeds, then max_wr and max_sge
 * will always be at least as large as the requested values.
 */
struct ib_srq *ib_create_srq(struct ib_pd *pd,
			     struct ib_srq_init_attr *srq_init_attr);

/**
 * ib_modify_srq - Modifies the attributes for the specified SRQ.
 * @srq: The SRQ to modify.
 * @srq_attr: On input, specifies the SRQ attributes to modify.  On output,
 *   the current values of selected SRQ attributes are returned.
 * @srq_attr_mask: A bit-mask used to specify which attributes of the SRQ
 *   are being modified.
 *
 * The mask may contain IB_SRQ_MAX_WR to resize the SRQ and/or
 * IB_SRQ_LIMIT to set the SRQ's limit and request notification when
 * the number of receives queued drops below the limit.
 */
int ib_modify_srq(struct ib_srq *srq,
		  struct ib_srq_attr *srq_attr,
		  enum ib_srq_attr_mask srq_attr_mask);

/**
 * ib_query_srq - Returns the attribute list and current values for the
 *   specified SRQ.
 * @srq: The SRQ to query.
 * @srq_attr: The attributes of the specified SRQ.
 */
int ib_query_srq(struct ib_srq *srq,
		 struct ib_srq_attr *srq_attr);

/**
 * ib_destroy_srq - Destroys the specified SRQ.
 * @srq: The SRQ to destroy.
 */
int ib_destroy_srq(struct ib_srq *srq);

/**
 * ib_post_srq_recv - Posts a list of work requests to the specified SRQ.
 * @srq: The SRQ to post the work request on.
 * @recv_wr: A list of work requests to post on the receive queue.
 * @bad_recv_wr: On an immediate failure, this parameter will reference
 *   the work request that failed to be posted on the QP.
 */
static inline int ib_post_srq_recv(struct ib_srq *srq,
				   struct ib_recv_wr *recv_wr,
				   struct ib_recv_wr **bad_recv_wr)
{
	return srq->device->post_srq_recv(srq, recv_wr, bad_recv_wr);
}

/**
 * ib_create_qp - Creates a QP associated with the specified protection
 *   domain.
 * @pd: The protection domain associated with the QP.
 * @qp_init_attr: A list of initial attributes required to create the
 *   QP.  If QP creation succeeds, then the attributes are updated to
 *   the actual capabilities of the created QP.
 */
struct ib_qp *ib_create_qp(struct ib_pd *pd,
			   struct ib_qp_init_attr *qp_init_attr);

/**
 * ib_modify_qp - Modifies the attributes for the specified QP and then
 *   transitions the QP to the given state.
 * @qp: The QP to modify.
 * @qp_attr: On input, specifies the QP attributes to modify.  On output,
 *   the current values of selected QP attributes are returned.
 * @qp_attr_mask: A bit-mask used to specify which attributes of the QP
 *   are being modified.
 */
int ib_modify_qp(struct ib_qp *qp,
		 struct ib_qp_attr *qp_attr,
		 int qp_attr_mask);

/**
 * ib_query_qp - Returns the attribute list and current values for the
 *   specified QP.
 * @qp: The QP to query.
 * @qp_attr: The attributes of the specified QP.
 * @qp_attr_mask: A bit-mask used to select specific attributes to query.
 * @qp_init_attr: Additional attributes of the selected QP.
 *
 * The qp_attr_mask may be used to limit the query to gathering only the
 * selected attributes.
 */
int ib_query_qp(struct ib_qp *qp,
		struct ib_qp_attr *qp_attr,
		int qp_attr_mask,
		struct ib_qp_init_attr *qp_init_attr);

/**
 * ib_destroy_qp - Destroys the specified QP.
 * @qp: The QP to destroy.
 */
int ib_destroy_qp(struct ib_qp *qp);

/**
 * ib_open_qp - Obtain a reference to an existing sharable QP.
 * @xrcd - XRC domain
 * @qp_open_attr: Attributes identifying the QP to open.
 *
 * Returns a reference to a sharable QP.
 */
struct ib_qp *ib_open_qp(struct ib_xrcd *xrcd,
			 struct ib_qp_open_attr *qp_open_attr);

/**
 * ib_close_qp - Release an external reference to a QP.
 * @qp: The QP handle to release
 *
 * The opened QP handle is released by the caller.  The underlying
 * shared QP is not destroyed until all internal references are released.
 */
int ib_close_qp(struct ib_qp *qp);

/**
 * ib_post_send - Posts a list of work requests to the send queue of
 *   the specified QP.
 * @qp: The QP to post the work request on.
 * @send_wr: A list of work requests to post on the send queue.
 * @bad_send_wr: On an immediate failure, this parameter will reference
 *   the work request that failed to be posted on the QP.
 *
 * While IBA Vol. 1 section 11.4.1.1 specifies that if an immediate
 * error is returned, the QP state shall not be affected,
 * ib_post_send() will return an immediate error after queueing any
 * earlier work requests in the list.
 */
static inline int ib_post_send(struct ib_qp *qp,
			       struct ib_send_wr *send_wr,
			       struct ib_send_wr **bad_send_wr)
{
	return qp->device->post_send(qp, send_wr, bad_send_wr);
}

/**
 * ib_post_recv - Posts a list of work requests to the receive queue of
 *   the specified QP.
 * @qp: The QP to post the work request on.
 * @recv_wr: A list of work requests to post on the receive queue.
 * @bad_recv_wr: On an immediate failure, this parameter will reference
 *   the work request that failed to be posted on the QP.
 */
static inline int ib_post_recv(struct ib_qp *qp,
			       struct ib_recv_wr *recv_wr,
			       struct ib_recv_wr **bad_recv_wr)
{
	return qp->device->post_recv(qp, recv_wr, bad_recv_wr);
}

/**
 * ib_create_cq - Creates a CQ on the specified device.
 * @device: The device on which to create the CQ.
 * @comp_handler: A user-specified callback that is invoked when a
 *   completion event occurs on the CQ.
 * @event_handler: A user-specified callback that is invoked when an
 *   asynchronous event not associated with a completion occurs on the CQ.
 * @cq_context: Context associated with the CQ returned to the user via
 *   the associated completion and event handlers.
 * @cqe: The minimum size of the CQ.
 * @comp_vector - Completion vector used to signal completion events.
 *     Must be >= 0 and < context->num_comp_vectors.
 *
 * Users can examine the cq structure to determine the actual CQ size.
 */
struct ib_cq *ib_create_cq(struct ib_device *device,
			   ib_comp_handler comp_handler,
			   void (*event_handler)(struct ib_event *, void *),
			   void *cq_context, int cqe, int comp_vector);

/**
 * ib_resize_cq - Modifies the capacity of the CQ.
 * @cq: The CQ to resize.
 * @cqe: The minimum size of the CQ.
 *
 * Users can examine the cq structure to determine the actual CQ size.
 */
int ib_resize_cq(struct ib_cq *cq, int cqe);

/**
 * ib_modify_cq - Modifies moderation params of the CQ
 * @cq: The CQ to modify.
 * @cq_count: number of CQEs that will trigger an event
 * @cq_period: max period of time in usec before triggering an event
 *
 */
int ib_modify_cq(struct ib_cq *cq, u16 cq_count, u16 cq_period);

/**
 * ib_destroy_cq - Destroys the specified CQ.
 * @cq: The CQ to destroy.
 */
int ib_destroy_cq(struct ib_cq *cq);

/**
 * ib_poll_cq - poll a CQ for completion(s)
 * @cq:the CQ being polled
 * @num_entries:maximum number of completions to return
 * @wc:array of at least @num_entries &struct ib_wc where completions
 *   will be returned
 *
 * Poll a CQ for (possibly multiple) completions.  If the return value
 * is < 0, an error occurred.  If the return value is >= 0, it is the
 * number of completions returned.  If the return value is
 * non-negative and < num_entries, then the CQ was emptied.
 */
static inline int ib_poll_cq(struct ib_cq *cq, int num_entries,
			     struct ib_wc *wc)
{
	return cq->device->poll_cq(cq, num_entries, wc);
}

/**
 * ib_peek_cq - Returns the number of unreaped completions currently
 *   on the specified CQ.
 * @cq: The CQ to peek.
 * @wc_cnt: A minimum number of unreaped completions to check for.
 *
 * If the number of unreaped completions is greater than or equal to wc_cnt,
 * this function returns wc_cnt, otherwise, it returns the actual number of
 * unreaped completions.
 */
int ib_peek_cq(struct ib_cq *cq, int wc_cnt);

/**
 * ib_req_notify_cq - Request completion notification on a CQ.
 * @cq: The CQ to generate an event for.
 * @flags:
 *   Must contain exactly one of %IB_CQ_SOLICITED or %IB_CQ_NEXT_COMP
 *   to request an event on the next solicited event or next work
 *   completion at any type, respectively. %IB_CQ_REPORT_MISSED_EVENTS
 *   may also be |ed in to request a hint about missed events, as
 *   described below.
 *
 * Return Value:
 *    < 0 means an error occurred while requesting notification
 *   == 0 means notification was requested successfully, and if
 *        IB_CQ_REPORT_MISSED_EVENTS was passed in, then no events
 *        were missed and it is safe to wait for another event.  In
 *        this case is it guaranteed that any work completions added
 *        to the CQ since the last CQ poll will trigger a completion
 *        notification event.
 *    > 0 is only returned if IB_CQ_REPORT_MISSED_EVENTS was passed
 *        in.  It means that the consumer must poll the CQ again to
 *        make sure it is empty to avoid missing an event because of a
 *        race between requesting notification and an entry being
 *        added to the CQ.  This return value means it is possible
 *        (but not guaranteed) that a work completion has been added
 *        to the CQ since the last poll without triggering a
 *        completion notification event.
 */
static inline int ib_req_notify_cq(struct ib_cq *cq,
				   enum ib_cq_notify_flags flags)
{
	return cq->device->req_notify_cq(cq, flags);
}

/**
 * ib_req_ncomp_notif - Request completion notification when there are
 *   at least the specified number of unreaped completions on the CQ.
 * @cq: The CQ to generate an event for.
 * @wc_cnt: The number of unreaped completions that should be on the
 *   CQ before an event is generated.
 */
static inline int ib_req_ncomp_notif(struct ib_cq *cq, int wc_cnt)
{
	return cq->device->req_ncomp_notif ?
		cq->device->req_ncomp_notif(cq, wc_cnt) :
		-ENOSYS;
}

/**
 * ib_get_dma_mr - Returns a memory region for system memory that is
 *   usable for DMA.
 * @pd: The protection domain associated with the memory region.
 * @mr_access_flags: Specifies the memory access rights.
 *
 * Note that the ib_dma_*() functions defined below must be used
 * to create/destroy addresses used with the Lkey or Rkey returned
 * by ib_get_dma_mr().
 */
struct ib_mr *ib_get_dma_mr(struct ib_pd *pd, int mr_access_flags);

/**
 * ib_dma_mapping_error - check a DMA addr for error
 * @dev: The device for which the dma_addr was created
 * @dma_addr: The DMA address to check
 */
static inline int ib_dma_mapping_error(struct ib_device *dev, u64 dma_addr)
{
	if (dev->dma_ops)
		return dev->dma_ops->mapping_error(dev, dma_addr);
	return dma_mapping_error(dev->dma_device, dma_addr);
}

/**
 * ib_dma_map_single - Map a kernel virtual address to DMA address
 * @dev: The device for which the dma_addr is to be created
 * @cpu_addr: The kernel virtual address
 * @size: The size of the region in bytes
 * @direction: The direction of the DMA
 */
static inline u64 ib_dma_map_single(struct ib_device *dev,
				    void *cpu_addr, size_t size,
				    enum dma_data_direction direction)
{
	if (dev->dma_ops)
		return dev->dma_ops->map_single(dev, cpu_addr, size, direction);
	return dma_map_single(dev->dma_device, cpu_addr, size, direction);
}

/**
 * ib_dma_unmap_single - Destroy a mapping created by ib_dma_map_single()
 * @dev: The device for which the DMA address was created
 * @addr: The DMA address
 * @size: The size of the region in bytes
 * @direction: The direction of the DMA
 */
static inline void ib_dma_unmap_single(struct ib_device *dev,
				       u64 addr, size_t size,
				       enum dma_data_direction direction)
{
	if (dev->dma_ops)
		dev->dma_ops->unmap_single(dev, addr, size, direction);
	else
		dma_unmap_single(dev->dma_device, addr, size, direction);
}

static inline u64 ib_dma_map_single_attrs(struct ib_device *dev,
					  void *cpu_addr, size_t size,
					  enum dma_data_direction direction,
					  struct dma_attrs *attrs)
{
	return dma_map_single_attrs(dev->dma_device, cpu_addr, size,
				    direction, attrs);
}

static inline void ib_dma_unmap_single_attrs(struct ib_device *dev,
					     u64 addr, size_t size,
					     enum dma_data_direction direction,
					     struct dma_attrs *attrs)
{
	return dma_unmap_single_attrs(dev->dma_device, addr, size,
				      direction, attrs);
}

/**
 * ib_dma_map_page - Map a physical page to DMA address
 * @dev: The device for which the dma_addr is to be created
 * @page: The page to be mapped
 * @offset: The offset within the page
 * @size: The size of the region in bytes
 * @direction: The direction of the DMA
 */
static inline u64 ib_dma_map_page(struct ib_device *dev,
				  struct page *page,
				  unsigned long offset,
				  size_t size,
					 enum dma_data_direction direction)
{
	if (dev->dma_ops)
		return dev->dma_ops->map_page(dev, page, offset, size, direction);
	return dma_map_page(dev->dma_device, page, offset, size, direction);
}

/**
 * ib_dma_unmap_page - Destroy a mapping created by ib_dma_map_page()
 * @dev: The device for which the DMA address was created
 * @addr: The DMA address
 * @size: The size of the region in bytes
 * @direction: The direction of the DMA
 */
static inline void ib_dma_unmap_page(struct ib_device *dev,
				     u64 addr, size_t size,
				     enum dma_data_direction direction)
{
	if (dev->dma_ops)
		dev->dma_ops->unmap_page(dev, addr, size, direction);
	else
		dma_unmap_page(dev->dma_device, addr, size, direction);
}

/**
 * ib_dma_map_sg - Map a scatter/gather list to DMA addresses
 * @dev: The device for which the DMA addresses are to be created
 * @sg: The array of scatter/gather entries
 * @nents: The number of scatter/gather entries
 * @direction: The direction of the DMA
 */
static inline int ib_dma_map_sg(struct ib_device *dev,
				struct scatterlist *sg, int nents,
				enum dma_data_direction direction)
{
	if (dev->dma_ops)
		return dev->dma_ops->map_sg(dev, sg, nents, direction);
	return dma_map_sg(dev->dma_device, sg, nents, direction);
}

/**
 * ib_dma_unmap_sg - Unmap a scatter/gather list of DMA addresses
 * @dev: The device for which the DMA addresses were created
 * @sg: The array of scatter/gather entries
 * @nents: The number of scatter/gather entries
 * @direction: The direction of the DMA
 */
static inline void ib_dma_unmap_sg(struct ib_device *dev,
				   struct scatterlist *sg, int nents,
				   enum dma_data_direction direction)
{
	if (dev->dma_ops)
		dev->dma_ops->unmap_sg(dev, sg, nents, direction);
	else
		dma_unmap_sg(dev->dma_device, sg, nents, direction);
}

static inline int ib_dma_map_sg_attrs(struct ib_device *dev,
				      struct scatterlist *sg, int nents,
				      enum dma_data_direction direction,
				      struct dma_attrs *attrs)
{
	return dma_map_sg_attrs(dev->dma_device, sg, nents, direction, attrs);
}

static inline void ib_dma_unmap_sg_attrs(struct ib_device *dev,
					 struct scatterlist *sg, int nents,
					 enum dma_data_direction direction,
					 struct dma_attrs *attrs)
{
	dma_unmap_sg_attrs(dev->dma_device, sg, nents, direction, attrs);
}
/**
 * ib_sg_dma_address - Return the DMA address from a scatter/gather entry
 * @dev: The device for which the DMA addresses were created
 * @sg: The scatter/gather entry
 *
 * Note: this function is obsolete. To do: change all occurrences of
 * ib_sg_dma_address() into sg_dma_address().
 */
static inline u64 ib_sg_dma_address(struct ib_device *dev,
				    struct scatterlist *sg)
{
	return sg_dma_address(sg);
}

/**
 * ib_sg_dma_len - Return the DMA length from a scatter/gather entry
 * @dev: The device for which the DMA addresses were created
 * @sg: The scatter/gather entry
 *
 * Note: this function is obsolete. To do: change all occurrences of
 * ib_sg_dma_len() into sg_dma_len().
 */
static inline unsigned int ib_sg_dma_len(struct ib_device *dev,
					 struct scatterlist *sg)
{
	return sg_dma_len(sg);
}

/**
 * ib_dma_sync_single_for_cpu - Prepare DMA region to be accessed by CPU
 * @dev: The device for which the DMA address was created
 * @addr: The DMA address
 * @size: The size of the region in bytes
 * @dir: The direction of the DMA
 */
static inline void ib_dma_sync_single_for_cpu(struct ib_device *dev,
					      u64 addr,
					      size_t size,
					      enum dma_data_direction dir)
{
	if (dev->dma_ops)
		dev->dma_ops->sync_single_for_cpu(dev, addr, size, dir);
	else
		dma_sync_single_for_cpu(dev->dma_device, addr, size, dir);
}

/**
 * ib_dma_sync_single_for_device - Prepare DMA region to be accessed by device
 * @dev: The device for which the DMA address was created
 * @addr: The DMA address
 * @size: The size of the region in bytes
 * @dir: The direction of the DMA
 */
static inline void ib_dma_sync_single_for_device(struct ib_device *dev,
						 u64 addr,
						 size_t size,
						 enum dma_data_direction dir)
{
	if (dev->dma_ops)
		dev->dma_ops->sync_single_for_device(dev, addr, size, dir);
	else
		dma_sync_single_for_device(dev->dma_device, addr, size, dir);
}

/**
 * ib_dma_alloc_coherent - Allocate memory and map it for DMA
 * @dev: The device for which the DMA address is requested
 * @size: The size of the region to allocate in bytes
 * @dma_handle: A pointer for returning the DMA address of the region
 * @flag: memory allocator flags
 */
static inline void *ib_dma_alloc_coherent(struct ib_device *dev,
					   size_t size,
					   u64 *dma_handle,
					   gfp_t flag)
{
	if (dev->dma_ops)
		return dev->dma_ops->alloc_coherent(dev, size, dma_handle, flag);
	else {
		dma_addr_t handle;
		void *ret;

		ret = dma_alloc_coherent(dev->dma_device, size, &handle, flag);
		*dma_handle = handle;
		return ret;
	}
}

/**
 * ib_dma_free_coherent - Free memory allocated by ib_dma_alloc_coherent()
 * @dev: The device for which the DMA addresses were allocated
 * @size: The size of the region
 * @cpu_addr: the address returned by ib_dma_alloc_coherent()
 * @dma_handle: the DMA address returned by ib_dma_alloc_coherent()
 */
static inline void ib_dma_free_coherent(struct ib_device *dev,
					size_t size, void *cpu_addr,
					u64 dma_handle)
{
	if (dev->dma_ops)
		dev->dma_ops->free_coherent(dev, size, cpu_addr, dma_handle);
	else
		dma_free_coherent(dev->dma_device, size, cpu_addr, dma_handle);
}

/**
 * ib_reg_phys_mr - Prepares a virtually addressed memory region for use
 *   by an HCA.
 * @pd: The protection domain associated assigned to the registered region.
 * @phys_buf_array: Specifies a list of physical buffers to use in the
 *   memory region.
 * @num_phys_buf: Specifies the size of the phys_buf_array.
 * @mr_access_flags: Specifies the memory access rights.
 * @iova_start: The offset of the region's starting I/O virtual address.
 */
struct ib_mr *ib_reg_phys_mr(struct ib_pd *pd,
			     struct ib_phys_buf *phys_buf_array,
			     int num_phys_buf,
			     int mr_access_flags,
			     u64 *iova_start);

/**
 * ib_rereg_phys_mr - Modifies the attributes of an existing memory region.
 *   Conceptually, this call performs the functions deregister memory region
 *   followed by register physical memory region.  Where possible,
 *   resources are reused instead of deallocated and reallocated.
 * @mr: The memory region to modify.
 * @mr_rereg_mask: A bit-mask used to indicate which of the following
 *   properties of the memory region are being modified.
 * @pd: If %IB_MR_REREG_PD is set in mr_rereg_mask, this field specifies
 *   the new protection domain to associated with the memory region,
 *   otherwise, this parameter is ignored.
 * @phys_buf_array: If %IB_MR_REREG_TRANS is set in mr_rereg_mask, this
 *   field specifies a list of physical buffers to use in the new
 *   translation, otherwise, this parameter is ignored.
 * @num_phys_buf: If %IB_MR_REREG_TRANS is set in mr_rereg_mask, this
 *   field specifies the size of the phys_buf_array, otherwise, this
 *   parameter is ignored.
 * @mr_access_flags: If %IB_MR_REREG_ACCESS is set in mr_rereg_mask, this
 *   field specifies the new memory access rights, otherwise, this
 *   parameter is ignored.
 * @iova_start: The offset of the region's starting I/O virtual address.
 */
int ib_rereg_phys_mr(struct ib_mr *mr,
		     int mr_rereg_mask,
		     struct ib_pd *pd,
		     struct ib_phys_buf *phys_buf_array,
		     int num_phys_buf,
		     int mr_access_flags,
		     u64 *iova_start);

/**
 * ib_query_mr - Retrieves information about a specific memory region.
 * @mr: The memory region to retrieve information about.
 * @mr_attr: The attributes of the specified memory region.
 */
int ib_query_mr(struct ib_mr *mr, struct ib_mr_attr *mr_attr);

/**
 * ib_dereg_mr - Deregisters a memory region and removes it from the
 *   HCA translation table.
 * @mr: The memory region to deregister.
 *
 * This function can fail, if the memory region has memory windows bound to it.
 */
int ib_dereg_mr(struct ib_mr *mr);


/**
 * ib_create_mr - Allocates a memory region that may be used for
 *     signature handover operations.
 * @pd: The protection domain associated with the region.
 * @mr_init_attr: memory region init attributes.
 */
struct ib_mr *ib_create_mr(struct ib_pd *pd,
			   struct ib_mr_init_attr *mr_init_attr);

/**
 * ib_destroy_mr - Destroys a memory region that was created using
 *     ib_create_mr and removes it from HW translation tables.
 * @mr: The memory region to destroy.
 *
 * This function can fail, if the memory region has memory windows bound to it.
 */
int ib_destroy_mr(struct ib_mr *mr);

/**
 * ib_alloc_fast_reg_mr - Allocates memory region usable with the
 *   IB_WR_FAST_REG_MR send work request.
 * @pd: The protection domain associated with the region.
 * @max_page_list_len: requested max physical buffer list length to be
 *   used with fast register work requests for this MR.
 */
struct ib_mr *ib_alloc_fast_reg_mr(struct ib_pd *pd, int max_page_list_len);

/**
 * ib_alloc_fast_reg_page_list - Allocates a page list array
 * @device - ib device pointer.
 * @page_list_len - size of the page list array to be allocated.
 *
 * This allocates and returns a struct ib_fast_reg_page_list * and a
 * page_list array that is at least page_list_len in size.  The actual
 * size is returned in max_page_list_len.  The caller is responsible
 * for initializing the contents of the page_list array before posting
 * a send work request with the IB_WC_FAST_REG_MR opcode.
 *
 * The page_list array entries must be translated using one of the
 * ib_dma_*() functions just like the addresses passed to
 * ib_map_phys_fmr().  Once the ib_post_send() is issued, the struct
 * ib_fast_reg_page_list must not be modified by the caller until the
 * IB_WC_FAST_REG_MR work request completes.
 */
struct ib_fast_reg_page_list *ib_alloc_fast_reg_page_list(
				struct ib_device *device, int page_list_len);

/**
 * ib_free_fast_reg_page_list - Deallocates a previously allocated
 *   page list array.
 * @page_list - struct ib_fast_reg_page_list pointer to be deallocated.
 */
void ib_free_fast_reg_page_list(struct ib_fast_reg_page_list *page_list);

/**
 * ib_update_fast_reg_key - updates the key portion of the fast_reg MR
 *   R_Key and L_Key.
 * @mr - struct ib_mr pointer to be updated.
 * @newkey - new key to be used.
 */
static inline void ib_update_fast_reg_key(struct ib_mr *mr, u8 newkey)
{
	mr->lkey = (mr->lkey & 0xffffff00) | newkey;
	mr->rkey = (mr->rkey & 0xffffff00) | newkey;
}

/**
 * ib_inc_rkey - increments the key portion of the given rkey. Can be used
 * for calculating a new rkey for type 2 memory windows.
 * @rkey - the rkey to increment.
 */
static inline u32 ib_inc_rkey(u32 rkey)
{
	const u32 mask = 0x000000ff;
	return ((rkey + 1) & mask) | (rkey & ~mask);
}

/**
 * ib_alloc_mw - Allocates a memory window.
 * @pd: The protection domain associated with the memory window.
 * @type: The type of the memory window (1 or 2).
 */
struct ib_mw *ib_alloc_mw(struct ib_pd *pd, enum ib_mw_type type);

/**
 * ib_bind_mw - Posts a work request to the send queue of the specified
 *   QP, which binds the memory window to the given address range and
 *   remote access attributes.
 * @qp: QP to post the bind work request on.
 * @mw: The memory window to bind.
 * @mw_bind: Specifies information about the memory window, including
 *   its address range, remote access rights, and associated memory region.
 *
 * If there is no immediate error, the function will update the rkey member
 * of the mw parameter to its new value. The bind operation can still fail
 * asynchronously.
 */
static inline int ib_bind_mw(struct ib_qp *qp,
			     struct ib_mw *mw,
			     struct ib_mw_bind *mw_bind)
{
	/* XXX reference counting in corresponding MR? */
	return mw->device->bind_mw ?
		mw->device->bind_mw(qp, mw, mw_bind) :
		-ENOSYS;
}

/**
 * ib_dealloc_mw - Deallocates a memory window.
 * @mw: The memory window to deallocate.
 */
int ib_dealloc_mw(struct ib_mw *mw);

/**
 * ib_alloc_fmr - Allocates a unmapped fast memory region.
 * @pd: The protection domain associated with the unmapped region.
 * @mr_access_flags: Specifies the memory access rights.
 * @fmr_attr: Attributes of the unmapped region.
 *
 * A fast memory region must be mapped before it can be used as part of
 * a work request.
 */
struct ib_fmr *ib_alloc_fmr(struct ib_pd *pd,
			    int mr_access_flags,
			    struct ib_fmr_attr *fmr_attr);

/**
 * ib_map_phys_fmr - Maps a list of physical pages to a fast memory region.
 * @fmr: The fast memory region to associate with the pages.
 * @page_list: An array of physical pages to map to the fast memory region.
 * @list_len: The number of pages in page_list.
 * @iova: The I/O virtual address to use with the mapped region.
 */
static inline int ib_map_phys_fmr(struct ib_fmr *fmr,
				  u64 *page_list, int list_len,
				  u64 iova)
{
	return fmr->device->map_phys_fmr(fmr, page_list, list_len, iova);
}

/**
 * ib_unmap_fmr - Removes the mapping from a list of fast memory regions.
 * @fmr_list: A linked list of fast memory regions to unmap.
 */
int ib_unmap_fmr(struct list_head *fmr_list);

/**
 * ib_dealloc_fmr - Deallocates a fast memory region.
 * @fmr: The fast memory region to deallocate.
 */
int ib_dealloc_fmr(struct ib_fmr *fmr);

/**
 * ib_attach_mcast - Attaches the specified QP to a multicast group.
 * @qp: QP to attach to the multicast group.  The QP must be type
 *   IB_QPT_UD.
 * @gid: Multicast group GID.
 * @lid: Multicast group LID in host byte order.
 *
 * In order to send and receive multicast packets, subnet
 * administration must have created the multicast group and configured
 * the fabric appropriately.  The port associated with the specified
 * QP must also be a member of the multicast group.
 */
int ib_attach_mcast(struct ib_qp *qp, union ib_gid *gid, u16 lid);

/**
 * ib_detach_mcast - Detaches the specified QP from a multicast group.
 * @qp: QP to detach from the multicast group.
 * @gid: Multicast group GID.
 * @lid: Multicast group LID in host byte order.
 */
int ib_detach_mcast(struct ib_qp *qp, union ib_gid *gid, u16 lid);

/**
 * ib_alloc_xrcd - Allocates an XRC domain.
 * @device: The device on which to allocate the XRC domain.
 */
struct ib_xrcd *ib_alloc_xrcd(struct ib_device *device);

/**
 * ib_dealloc_xrcd - Deallocates an XRC domain.
 * @xrcd: The XRC domain to deallocate.
 */
int ib_dealloc_xrcd(struct ib_xrcd *xrcd);

struct ib_flow *ib_create_flow(struct ib_qp *qp,
			       struct ib_flow_attr *flow_attr, int domain);
int ib_destroy_flow(struct ib_flow *flow_id);

static inline int ib_check_mr_access(int flags)
{
	/*
	 * Local write permission is required if remote write or
	 * remote atomic permission is also requested.
	 */
	if (flags & (IB_ACCESS_REMOTE_ATOMIC | IB_ACCESS_REMOTE_WRITE) &&
	    !(flags & IB_ACCESS_LOCAL_WRITE))
		return -EINVAL;

	return 0;
}

/**
 * ib_check_mr_status: lightweight check of MR status.
 *     This routine may provide status checks on a selected
 *     ib_mr. first use is for signature status check.
 *
 * @mr: A memory region.
 * @check_mask: Bitmask of which checks to perform from
 *     ib_mr_status_check enumeration.
 * @mr_status: The container of relevant status checks.
 *     failed checks will be indicated in the status bitmask
 *     and the relevant info shall be in the error item.
 */
int ib_check_mr_status(struct ib_mr *mr, u32 check_mask,
		       struct ib_mr_status *mr_status);

#endif /* IB_VERBS_H */

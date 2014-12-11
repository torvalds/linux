/*
 * Copyright (c) 2006, 2007 Cisco Systems, Inc.  All rights reserved.
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
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
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

#ifndef MLX4_DEVICE_H
#define MLX4_DEVICE_H

#include <linux/if_ether.h>
#include <linux/pci.h>
#include <linux/completion.h>
#include <linux/radix-tree.h>
#include <linux/cpu_rmap.h>
#include <linux/crash_dump.h>

#include <linux/atomic.h>

#include <linux/clocksource.h>

#define MAX_MSIX_P_PORT		17
#define MAX_MSIX		64
#define MSIX_LEGACY_SZ		4
#define MIN_MSIX_P_PORT		5

#define MLX4_NUM_UP			8
#define MLX4_NUM_TC			8
#define MLX4_MAX_100M_UNITS_VAL		255	/*
						 * work around: can't set values
						 * greater then this value when
						 * using 100 Mbps units.
						 */
#define MLX4_RATELIMIT_100M_UNITS	3	/* 100 Mbps */
#define MLX4_RATELIMIT_1G_UNITS		4	/* 1 Gbps */
#define MLX4_RATELIMIT_DEFAULT		0x00ff

#define MLX4_ROCE_MAX_GIDS	128
#define MLX4_ROCE_PF_GIDS	16

enum {
	MLX4_FLAG_MSI_X		= 1 << 0,
	MLX4_FLAG_OLD_PORT_CMDS	= 1 << 1,
	MLX4_FLAG_MASTER	= 1 << 2,
	MLX4_FLAG_SLAVE		= 1 << 3,
	MLX4_FLAG_SRIOV		= 1 << 4,
	MLX4_FLAG_OLD_REG_MAC	= 1 << 6,
};

enum {
	MLX4_PORT_CAP_IS_SM	= 1 << 1,
	MLX4_PORT_CAP_DEV_MGMT_SUP = 1 << 19,
};

enum {
	MLX4_MAX_PORTS		= 2,
	MLX4_MAX_PORT_PKEYS	= 128
};

/* base qkey for use in sriov tunnel-qp/proxy-qp communication.
 * These qkeys must not be allowed for general use. This is a 64k range,
 * and to test for violation, we use the mask (protect against future chg).
 */
#define MLX4_RESERVED_QKEY_BASE  (0xFFFF0000)
#define MLX4_RESERVED_QKEY_MASK  (0xFFFF0000)

enum {
	MLX4_BOARD_ID_LEN = 64
};

enum {
	MLX4_MAX_NUM_PF		= 16,
	MLX4_MAX_NUM_VF		= 126,
	MLX4_MAX_NUM_VF_P_PORT  = 64,
	MLX4_MFUNC_MAX		= 80,
	MLX4_MAX_EQ_NUM		= 1024,
	MLX4_MFUNC_EQ_NUM	= 4,
	MLX4_MFUNC_MAX_EQES     = 8,
	MLX4_MFUNC_EQE_MASK     = (MLX4_MFUNC_MAX_EQES - 1)
};

/* Driver supports 3 diffrent device methods to manage traffic steering:
 *	-device managed - High level API for ib and eth flow steering. FW is
 *			  managing flow steering tables.
 *	- B0 steering mode - Common low level API for ib and (if supported) eth.
 *	- A0 steering mode - Limited low level API for eth. In case of IB,
 *			     B0 mode is in use.
 */
enum {
	MLX4_STEERING_MODE_A0,
	MLX4_STEERING_MODE_B0,
	MLX4_STEERING_MODE_DEVICE_MANAGED
};

enum {
	MLX4_STEERING_DMFS_A0_DEFAULT,
	MLX4_STEERING_DMFS_A0_DYNAMIC,
	MLX4_STEERING_DMFS_A0_STATIC,
	MLX4_STEERING_DMFS_A0_DISABLE,
	MLX4_STEERING_DMFS_A0_NOT_SUPPORTED
};

static inline const char *mlx4_steering_mode_str(int steering_mode)
{
	switch (steering_mode) {
	case MLX4_STEERING_MODE_A0:
		return "A0 steering";

	case MLX4_STEERING_MODE_B0:
		return "B0 steering";

	case MLX4_STEERING_MODE_DEVICE_MANAGED:
		return "Device managed flow steering";

	default:
		return "Unrecognize steering mode";
	}
}

enum {
	MLX4_TUNNEL_OFFLOAD_MODE_NONE,
	MLX4_TUNNEL_OFFLOAD_MODE_VXLAN
};

enum {
	MLX4_DEV_CAP_FLAG_RC		= 1LL <<  0,
	MLX4_DEV_CAP_FLAG_UC		= 1LL <<  1,
	MLX4_DEV_CAP_FLAG_UD		= 1LL <<  2,
	MLX4_DEV_CAP_FLAG_XRC		= 1LL <<  3,
	MLX4_DEV_CAP_FLAG_SRQ		= 1LL <<  6,
	MLX4_DEV_CAP_FLAG_IPOIB_CSUM	= 1LL <<  7,
	MLX4_DEV_CAP_FLAG_BAD_PKEY_CNTR	= 1LL <<  8,
	MLX4_DEV_CAP_FLAG_BAD_QKEY_CNTR	= 1LL <<  9,
	MLX4_DEV_CAP_FLAG_DPDP		= 1LL << 12,
	MLX4_DEV_CAP_FLAG_BLH		= 1LL << 15,
	MLX4_DEV_CAP_FLAG_MEM_WINDOW	= 1LL << 16,
	MLX4_DEV_CAP_FLAG_APM		= 1LL << 17,
	MLX4_DEV_CAP_FLAG_ATOMIC	= 1LL << 18,
	MLX4_DEV_CAP_FLAG_RAW_MCAST	= 1LL << 19,
	MLX4_DEV_CAP_FLAG_UD_AV_PORT	= 1LL << 20,
	MLX4_DEV_CAP_FLAG_UD_MCAST	= 1LL << 21,
	MLX4_DEV_CAP_FLAG_IBOE		= 1LL << 30,
	MLX4_DEV_CAP_FLAG_UC_LOOPBACK	= 1LL << 32,
	MLX4_DEV_CAP_FLAG_FCS_KEEP	= 1LL << 34,
	MLX4_DEV_CAP_FLAG_WOL_PORT1	= 1LL << 37,
	MLX4_DEV_CAP_FLAG_WOL_PORT2	= 1LL << 38,
	MLX4_DEV_CAP_FLAG_UDP_RSS	= 1LL << 40,
	MLX4_DEV_CAP_FLAG_VEP_UC_STEER	= 1LL << 41,
	MLX4_DEV_CAP_FLAG_VEP_MC_STEER	= 1LL << 42,
	MLX4_DEV_CAP_FLAG_COUNTERS	= 1LL << 48,
	MLX4_DEV_CAP_FLAG_SET_ETH_SCHED = 1LL << 53,
	MLX4_DEV_CAP_FLAG_SENSE_SUPPORT	= 1LL << 55,
	MLX4_DEV_CAP_FLAG_PORT_MNG_CHG_EV = 1LL << 59,
	MLX4_DEV_CAP_FLAG_64B_EQE	= 1LL << 61,
	MLX4_DEV_CAP_FLAG_64B_CQE	= 1LL << 62
};

enum {
	MLX4_DEV_CAP_FLAG2_RSS			= 1LL <<  0,
	MLX4_DEV_CAP_FLAG2_RSS_TOP		= 1LL <<  1,
	MLX4_DEV_CAP_FLAG2_RSS_XOR		= 1LL <<  2,
	MLX4_DEV_CAP_FLAG2_FS_EN		= 1LL <<  3,
	MLX4_DEV_CAP_FLAG2_REASSIGN_MAC_EN	= 1LL <<  4,
	MLX4_DEV_CAP_FLAG2_TS			= 1LL <<  5,
	MLX4_DEV_CAP_FLAG2_VLAN_CONTROL		= 1LL <<  6,
	MLX4_DEV_CAP_FLAG2_FSM			= 1LL <<  7,
	MLX4_DEV_CAP_FLAG2_UPDATE_QP		= 1LL <<  8,
	MLX4_DEV_CAP_FLAG2_DMFS_IPOIB		= 1LL <<  9,
	MLX4_DEV_CAP_FLAG2_VXLAN_OFFLOADS	= 1LL <<  10,
	MLX4_DEV_CAP_FLAG2_MAD_DEMUX		= 1LL <<  11,
	MLX4_DEV_CAP_FLAG2_CQE_STRIDE		= 1LL <<  12,
	MLX4_DEV_CAP_FLAG2_EQE_STRIDE		= 1LL <<  13,
	MLX4_DEV_CAP_FLAG2_ETH_PROT_CTRL        = 1LL <<  14,
	MLX4_DEV_CAP_FLAG2_ETH_BACKPL_AN_REP	= 1LL <<  15,
	MLX4_DEV_CAP_FLAG2_CONFIG_DEV		= 1LL <<  16,
	MLX4_DEV_CAP_FLAG2_SYS_EQS		= 1LL <<  17,
	MLX4_DEV_CAP_FLAG2_80_VFS		= 1LL <<  18,
	MLX4_DEV_CAP_FLAG2_FS_A0		= 1LL <<  19
};

enum {
	MLX4_QUERY_FUNC_FLAGS_BF_RES_QP		= 1LL << 0,
	MLX4_QUERY_FUNC_FLAGS_A0_RES_QP		= 1LL << 1
};

/* bit enums for an 8-bit flags field indicating special use
 * QPs which require special handling in qp_reserve_range.
 * Currently, this only includes QPs used by the ETH interface,
 * where we expect to use blueflame.  These QPs must not have
 * bits 6 and 7 set in their qp number.
 *
 * This enum may use only bits 0..7.
 */
enum {
	MLX4_RESERVE_A0_QP	= 1 << 6,
	MLX4_RESERVE_ETH_BF_QP	= 1 << 7,
};

enum {
	MLX4_DEV_CAP_64B_EQE_ENABLED	= 1LL << 0,
	MLX4_DEV_CAP_64B_CQE_ENABLED	= 1LL << 1,
	MLX4_DEV_CAP_CQE_STRIDE_ENABLED	= 1LL << 2,
	MLX4_DEV_CAP_EQE_STRIDE_ENABLED	= 1LL << 3
};

enum {
	MLX4_USER_DEV_CAP_LARGE_CQE	= 1L << 0
};

enum {
	MLX4_FUNC_CAP_64B_EQE_CQE	= 1L << 0,
	MLX4_FUNC_CAP_EQE_CQE_STRIDE	= 1L << 1,
	MLX4_FUNC_CAP_DMFS_A0_STATIC	= 1L << 2
};


#define MLX4_ATTR_EXTENDED_PORT_INFO	cpu_to_be16(0xff90)

enum {
	MLX4_BMME_FLAG_WIN_TYPE_2B	= 1 <<  1,
	MLX4_BMME_FLAG_LOCAL_INV	= 1 <<  6,
	MLX4_BMME_FLAG_REMOTE_INV	= 1 <<  7,
	MLX4_BMME_FLAG_TYPE_2_WIN	= 1 <<  9,
	MLX4_BMME_FLAG_RESERVED_LKEY	= 1 << 10,
	MLX4_BMME_FLAG_FAST_REG_WR	= 1 << 11,
	MLX4_BMME_FLAG_VSD_INIT2RTR	= 1 << 28,
};

enum mlx4_event {
	MLX4_EVENT_TYPE_COMP		   = 0x00,
	MLX4_EVENT_TYPE_PATH_MIG	   = 0x01,
	MLX4_EVENT_TYPE_COMM_EST	   = 0x02,
	MLX4_EVENT_TYPE_SQ_DRAINED	   = 0x03,
	MLX4_EVENT_TYPE_SRQ_QP_LAST_WQE	   = 0x13,
	MLX4_EVENT_TYPE_SRQ_LIMIT	   = 0x14,
	MLX4_EVENT_TYPE_CQ_ERROR	   = 0x04,
	MLX4_EVENT_TYPE_WQ_CATAS_ERROR	   = 0x05,
	MLX4_EVENT_TYPE_EEC_CATAS_ERROR	   = 0x06,
	MLX4_EVENT_TYPE_PATH_MIG_FAILED	   = 0x07,
	MLX4_EVENT_TYPE_WQ_INVAL_REQ_ERROR = 0x10,
	MLX4_EVENT_TYPE_WQ_ACCESS_ERROR	   = 0x11,
	MLX4_EVENT_TYPE_SRQ_CATAS_ERROR	   = 0x12,
	MLX4_EVENT_TYPE_LOCAL_CATAS_ERROR  = 0x08,
	MLX4_EVENT_TYPE_PORT_CHANGE	   = 0x09,
	MLX4_EVENT_TYPE_EQ_OVERFLOW	   = 0x0f,
	MLX4_EVENT_TYPE_ECC_DETECT	   = 0x0e,
	MLX4_EVENT_TYPE_CMD		   = 0x0a,
	MLX4_EVENT_TYPE_VEP_UPDATE	   = 0x19,
	MLX4_EVENT_TYPE_COMM_CHANNEL	   = 0x18,
	MLX4_EVENT_TYPE_OP_REQUIRED	   = 0x1a,
	MLX4_EVENT_TYPE_FATAL_WARNING	   = 0x1b,
	MLX4_EVENT_TYPE_FLR_EVENT	   = 0x1c,
	MLX4_EVENT_TYPE_PORT_MNG_CHG_EVENT = 0x1d,
	MLX4_EVENT_TYPE_NONE		   = 0xff,
};

enum {
	MLX4_PORT_CHANGE_SUBTYPE_DOWN	= 1,
	MLX4_PORT_CHANGE_SUBTYPE_ACTIVE	= 4
};

enum {
	MLX4_FATAL_WARNING_SUBTYPE_WARMING = 0,
};

enum slave_port_state {
	SLAVE_PORT_DOWN = 0,
	SLAVE_PENDING_UP,
	SLAVE_PORT_UP,
};

enum slave_port_gen_event {
	SLAVE_PORT_GEN_EVENT_DOWN = 0,
	SLAVE_PORT_GEN_EVENT_UP,
	SLAVE_PORT_GEN_EVENT_NONE,
};

enum slave_port_state_event {
	MLX4_PORT_STATE_DEV_EVENT_PORT_DOWN,
	MLX4_PORT_STATE_DEV_EVENT_PORT_UP,
	MLX4_PORT_STATE_IB_PORT_STATE_EVENT_GID_VALID,
	MLX4_PORT_STATE_IB_EVENT_GID_INVALID,
};

enum {
	MLX4_PERM_LOCAL_READ	= 1 << 10,
	MLX4_PERM_LOCAL_WRITE	= 1 << 11,
	MLX4_PERM_REMOTE_READ	= 1 << 12,
	MLX4_PERM_REMOTE_WRITE	= 1 << 13,
	MLX4_PERM_ATOMIC	= 1 << 14,
	MLX4_PERM_BIND_MW	= 1 << 15,
	MLX4_PERM_MASK		= 0xFC00
};

enum {
	MLX4_OPCODE_NOP			= 0x00,
	MLX4_OPCODE_SEND_INVAL		= 0x01,
	MLX4_OPCODE_RDMA_WRITE		= 0x08,
	MLX4_OPCODE_RDMA_WRITE_IMM	= 0x09,
	MLX4_OPCODE_SEND		= 0x0a,
	MLX4_OPCODE_SEND_IMM		= 0x0b,
	MLX4_OPCODE_LSO			= 0x0e,
	MLX4_OPCODE_RDMA_READ		= 0x10,
	MLX4_OPCODE_ATOMIC_CS		= 0x11,
	MLX4_OPCODE_ATOMIC_FA		= 0x12,
	MLX4_OPCODE_MASKED_ATOMIC_CS	= 0x14,
	MLX4_OPCODE_MASKED_ATOMIC_FA	= 0x15,
	MLX4_OPCODE_BIND_MW		= 0x18,
	MLX4_OPCODE_FMR			= 0x19,
	MLX4_OPCODE_LOCAL_INVAL		= 0x1b,
	MLX4_OPCODE_CONFIG_CMD		= 0x1f,

	MLX4_RECV_OPCODE_RDMA_WRITE_IMM	= 0x00,
	MLX4_RECV_OPCODE_SEND		= 0x01,
	MLX4_RECV_OPCODE_SEND_IMM	= 0x02,
	MLX4_RECV_OPCODE_SEND_INVAL	= 0x03,

	MLX4_CQE_OPCODE_ERROR		= 0x1e,
	MLX4_CQE_OPCODE_RESIZE		= 0x16,
};

enum {
	MLX4_STAT_RATE_OFFSET	= 5
};

enum mlx4_protocol {
	MLX4_PROT_IB_IPV6 = 0,
	MLX4_PROT_ETH,
	MLX4_PROT_IB_IPV4,
	MLX4_PROT_FCOE
};

enum {
	MLX4_MTT_FLAG_PRESENT		= 1
};

enum mlx4_qp_region {
	MLX4_QP_REGION_FW = 0,
	MLX4_QP_REGION_RSS_RAW_ETH,
	MLX4_QP_REGION_BOTTOM = MLX4_QP_REGION_RSS_RAW_ETH,
	MLX4_QP_REGION_ETH_ADDR,
	MLX4_QP_REGION_FC_ADDR,
	MLX4_QP_REGION_FC_EXCH,
	MLX4_NUM_QP_REGION
};

enum mlx4_port_type {
	MLX4_PORT_TYPE_NONE	= 0,
	MLX4_PORT_TYPE_IB	= 1,
	MLX4_PORT_TYPE_ETH	= 2,
	MLX4_PORT_TYPE_AUTO	= 3
};

enum mlx4_special_vlan_idx {
	MLX4_NO_VLAN_IDX        = 0,
	MLX4_VLAN_MISS_IDX,
	MLX4_VLAN_REGULAR
};

enum mlx4_steer_type {
	MLX4_MC_STEER = 0,
	MLX4_UC_STEER,
	MLX4_NUM_STEERS
};

enum {
	MLX4_NUM_FEXCH          = 64 * 1024,
};

enum {
	MLX4_MAX_FAST_REG_PAGES = 511,
};

enum {
	MLX4_DEV_PMC_SUBTYPE_GUID_INFO	 = 0x14,
	MLX4_DEV_PMC_SUBTYPE_PORT_INFO	 = 0x15,
	MLX4_DEV_PMC_SUBTYPE_PKEY_TABLE	 = 0x16,
};

/* Port mgmt change event handling */
enum {
	MLX4_EQ_PORT_INFO_MSTR_SM_LID_CHANGE_MASK	= 1 << 0,
	MLX4_EQ_PORT_INFO_GID_PFX_CHANGE_MASK		= 1 << 1,
	MLX4_EQ_PORT_INFO_LID_CHANGE_MASK		= 1 << 2,
	MLX4_EQ_PORT_INFO_CLIENT_REREG_MASK		= 1 << 3,
	MLX4_EQ_PORT_INFO_MSTR_SM_SL_CHANGE_MASK	= 1 << 4,
};

#define MSTR_SM_CHANGE_MASK (MLX4_EQ_PORT_INFO_MSTR_SM_SL_CHANGE_MASK | \
			     MLX4_EQ_PORT_INFO_MSTR_SM_LID_CHANGE_MASK)

enum mlx4_module_id {
	MLX4_MODULE_ID_SFP              = 0x3,
	MLX4_MODULE_ID_QSFP             = 0xC,
	MLX4_MODULE_ID_QSFP_PLUS        = 0xD,
	MLX4_MODULE_ID_QSFP28           = 0x11,
};

static inline u64 mlx4_fw_ver(u64 major, u64 minor, u64 subminor)
{
	return (major << 32) | (minor << 16) | subminor;
}

struct mlx4_phys_caps {
	u32			gid_phys_table_len[MLX4_MAX_PORTS + 1];
	u32			pkey_phys_table_len[MLX4_MAX_PORTS + 1];
	u32			num_phys_eqs;
	u32			base_sqpn;
	u32			base_proxy_sqpn;
	u32			base_tunnel_sqpn;
};

struct mlx4_caps {
	u64			fw_ver;
	u32			function;
	int			num_ports;
	int			vl_cap[MLX4_MAX_PORTS + 1];
	int			ib_mtu_cap[MLX4_MAX_PORTS + 1];
	__be32			ib_port_def_cap[MLX4_MAX_PORTS + 1];
	u64			def_mac[MLX4_MAX_PORTS + 1];
	int			eth_mtu_cap[MLX4_MAX_PORTS + 1];
	int			gid_table_len[MLX4_MAX_PORTS + 1];
	int			pkey_table_len[MLX4_MAX_PORTS + 1];
	int			trans_type[MLX4_MAX_PORTS + 1];
	int			vendor_oui[MLX4_MAX_PORTS + 1];
	int			wavelength[MLX4_MAX_PORTS + 1];
	u64			trans_code[MLX4_MAX_PORTS + 1];
	int			local_ca_ack_delay;
	int			num_uars;
	u32			uar_page_size;
	int			bf_reg_size;
	int			bf_regs_per_page;
	int			max_sq_sg;
	int			max_rq_sg;
	int			num_qps;
	int			max_wqes;
	int			max_sq_desc_sz;
	int			max_rq_desc_sz;
	int			max_qp_init_rdma;
	int			max_qp_dest_rdma;
	u32			*qp0_qkey;
	u32			*qp0_proxy;
	u32			*qp1_proxy;
	u32			*qp0_tunnel;
	u32			*qp1_tunnel;
	int			num_srqs;
	int			max_srq_wqes;
	int			max_srq_sge;
	int			reserved_srqs;
	int			num_cqs;
	int			max_cqes;
	int			reserved_cqs;
	int			num_sys_eqs;
	int			num_eqs;
	int			reserved_eqs;
	int			num_comp_vectors;
	int			comp_pool;
	int			num_mpts;
	int			max_fmr_maps;
	int			num_mtts;
	int			fmr_reserved_mtts;
	int			reserved_mtts;
	int			reserved_mrws;
	int			reserved_uars;
	int			num_mgms;
	int			num_amgms;
	int			reserved_mcgs;
	int			num_qp_per_mgm;
	int			steering_mode;
	int			dmfs_high_steer_mode;
	int			fs_log_max_ucast_qp_range_size;
	int			num_pds;
	int			reserved_pds;
	int			max_xrcds;
	int			reserved_xrcds;
	int			mtt_entry_sz;
	u32			max_msg_sz;
	u32			page_size_cap;
	u64			flags;
	u64			flags2;
	u32			bmme_flags;
	u32			reserved_lkey;
	u16			stat_rate_support;
	u8			port_width_cap[MLX4_MAX_PORTS + 1];
	int			max_gso_sz;
	int			max_rss_tbl_sz;
	int                     reserved_qps_cnt[MLX4_NUM_QP_REGION];
	int			reserved_qps;
	int                     reserved_qps_base[MLX4_NUM_QP_REGION];
	int                     log_num_macs;
	int                     log_num_vlans;
	enum mlx4_port_type	port_type[MLX4_MAX_PORTS + 1];
	u8			supported_type[MLX4_MAX_PORTS + 1];
	u8                      suggested_type[MLX4_MAX_PORTS + 1];
	u8                      default_sense[MLX4_MAX_PORTS + 1];
	u32			port_mask[MLX4_MAX_PORTS + 1];
	enum mlx4_port_type	possible_type[MLX4_MAX_PORTS + 1];
	u32			max_counters;
	u8			port_ib_mtu[MLX4_MAX_PORTS + 1];
	u16			sqp_demux;
	u32			eqe_size;
	u32			cqe_size;
	u8			eqe_factor;
	u32			userspace_caps; /* userspace must be aware of these */
	u32			function_caps;  /* VFs must be aware of these */
	u16			hca_core_clock;
	u64			phys_port_id[MLX4_MAX_PORTS + 1];
	int			tunnel_offload_mode;
	u8			rx_checksum_flags_port[MLX4_MAX_PORTS + 1];
	u8			alloc_res_qp_mask;
	u32			dmfs_high_rate_qpn_base;
	u32			dmfs_high_rate_qpn_range;
};

struct mlx4_buf_list {
	void		       *buf;
	dma_addr_t		map;
};

struct mlx4_buf {
	struct mlx4_buf_list	direct;
	struct mlx4_buf_list   *page_list;
	int			nbufs;
	int			npages;
	int			page_shift;
};

struct mlx4_mtt {
	u32			offset;
	int			order;
	int			page_shift;
};

enum {
	MLX4_DB_PER_PAGE = PAGE_SIZE / 4
};

struct mlx4_db_pgdir {
	struct list_head	list;
	DECLARE_BITMAP(order0, MLX4_DB_PER_PAGE);
	DECLARE_BITMAP(order1, MLX4_DB_PER_PAGE / 2);
	unsigned long	       *bits[2];
	__be32		       *db_page;
	dma_addr_t		db_dma;
};

struct mlx4_ib_user_db_page;

struct mlx4_db {
	__be32			*db;
	union {
		struct mlx4_db_pgdir		*pgdir;
		struct mlx4_ib_user_db_page	*user_page;
	}			u;
	dma_addr_t		dma;
	int			index;
	int			order;
};

struct mlx4_hwq_resources {
	struct mlx4_db		db;
	struct mlx4_mtt		mtt;
	struct mlx4_buf		buf;
};

struct mlx4_mr {
	struct mlx4_mtt		mtt;
	u64			iova;
	u64			size;
	u32			key;
	u32			pd;
	u32			access;
	int			enabled;
};

enum mlx4_mw_type {
	MLX4_MW_TYPE_1 = 1,
	MLX4_MW_TYPE_2 = 2,
};

struct mlx4_mw {
	u32			key;
	u32			pd;
	enum mlx4_mw_type	type;
	int			enabled;
};

struct mlx4_fmr {
	struct mlx4_mr		mr;
	struct mlx4_mpt_entry  *mpt;
	__be64		       *mtts;
	dma_addr_t		dma_handle;
	int			max_pages;
	int			max_maps;
	int			maps;
	u8			page_shift;
};

struct mlx4_uar {
	unsigned long		pfn;
	int			index;
	struct list_head	bf_list;
	unsigned		free_bf_bmap;
	void __iomem	       *map;
	void __iomem	       *bf_map;
};

struct mlx4_bf {
	unsigned int		offset;
	int			buf_size;
	struct mlx4_uar	       *uar;
	void __iomem	       *reg;
};

struct mlx4_cq {
	void (*comp)		(struct mlx4_cq *);
	void (*event)		(struct mlx4_cq *, enum mlx4_event);

	struct mlx4_uar	       *uar;

	u32			cons_index;

	u16                     irq;
	__be32		       *set_ci_db;
	__be32		       *arm_db;
	int			arm_sn;

	int			cqn;
	unsigned		vector;

	atomic_t		refcount;
	struct completion	free;
	struct {
		struct list_head list;
		void (*comp)(struct mlx4_cq *);
		void		*priv;
	} tasklet_ctx;
};

struct mlx4_qp {
	void (*event)		(struct mlx4_qp *, enum mlx4_event);

	int			qpn;

	atomic_t		refcount;
	struct completion	free;
};

struct mlx4_srq {
	void (*event)		(struct mlx4_srq *, enum mlx4_event);

	int			srqn;
	int			max;
	int			max_gs;
	int			wqe_shift;

	atomic_t		refcount;
	struct completion	free;
};

struct mlx4_av {
	__be32			port_pd;
	u8			reserved1;
	u8			g_slid;
	__be16			dlid;
	u8			reserved2;
	u8			gid_index;
	u8			stat_rate;
	u8			hop_limit;
	__be32			sl_tclass_flowlabel;
	u8			dgid[16];
};

struct mlx4_eth_av {
	__be32		port_pd;
	u8		reserved1;
	u8		smac_idx;
	u16		reserved2;
	u8		reserved3;
	u8		gid_index;
	u8		stat_rate;
	u8		hop_limit;
	__be32		sl_tclass_flowlabel;
	u8		dgid[16];
	u8		s_mac[6];
	u8		reserved4[2];
	__be16		vlan;
	u8		mac[ETH_ALEN];
};

union mlx4_ext_av {
	struct mlx4_av		ib;
	struct mlx4_eth_av	eth;
};

struct mlx4_counter {
	u8	reserved1[3];
	u8	counter_mode;
	__be32	num_ifc;
	u32	reserved2[2];
	__be64	rx_frames;
	__be64	rx_bytes;
	__be64	tx_frames;
	__be64	tx_bytes;
};

struct mlx4_quotas {
	int qp;
	int cq;
	int srq;
	int mpt;
	int mtt;
	int counter;
	int xrcd;
};

struct mlx4_vf_dev {
	u8			min_port;
	u8			n_ports;
};

struct mlx4_dev {
	struct pci_dev	       *pdev;
	unsigned long		flags;
	unsigned long		num_slaves;
	struct mlx4_caps	caps;
	struct mlx4_phys_caps	phys_caps;
	struct mlx4_quotas	quotas;
	struct radix_tree_root	qp_table_tree;
	u8			rev_id;
	char			board_id[MLX4_BOARD_ID_LEN];
	int			num_vfs;
	int			numa_node;
	int			oper_log_mgm_entry_size;
	u64			regid_promisc_array[MLX4_MAX_PORTS + 1];
	u64			regid_allmulti_array[MLX4_MAX_PORTS + 1];
	struct mlx4_vf_dev     *dev_vfs;
	int                     nvfs[MLX4_MAX_PORTS + 1];
};

struct mlx4_eqe {
	u8			reserved1;
	u8			type;
	u8			reserved2;
	u8			subtype;
	union {
		u32		raw[6];
		struct {
			__be32	cqn;
		} __packed comp;
		struct {
			u16	reserved1;
			__be16	token;
			u32	reserved2;
			u8	reserved3[3];
			u8	status;
			__be64	out_param;
		} __packed cmd;
		struct {
			__be32	qpn;
		} __packed qp;
		struct {
			__be32	srqn;
		} __packed srq;
		struct {
			__be32	cqn;
			u32	reserved1;
			u8	reserved2[3];
			u8	syndrome;
		} __packed cq_err;
		struct {
			u32	reserved1[2];
			__be32	port;
		} __packed port_change;
		struct {
			#define COMM_CHANNEL_BIT_ARRAY_SIZE	4
			u32 reserved;
			u32 bit_vec[COMM_CHANNEL_BIT_ARRAY_SIZE];
		} __packed comm_channel_arm;
		struct {
			u8	port;
			u8	reserved[3];
			__be64	mac;
		} __packed mac_update;
		struct {
			__be32	slave_id;
		} __packed flr_event;
		struct {
			__be16  current_temperature;
			__be16  warning_threshold;
		} __packed warming;
		struct {
			u8 reserved[3];
			u8 port;
			union {
				struct {
					__be16 mstr_sm_lid;
					__be16 port_lid;
					__be32 changed_attr;
					u8 reserved[3];
					u8 mstr_sm_sl;
					__be64 gid_prefix;
				} __packed port_info;
				struct {
					__be32 block_ptr;
					__be32 tbl_entries_mask;
				} __packed tbl_change_info;
			} params;
		} __packed port_mgmt_change;
	}			event;
	u8			slave_id;
	u8			reserved3[2];
	u8			owner;
} __packed;

struct mlx4_init_port_param {
	int			set_guid0;
	int			set_node_guid;
	int			set_si_guid;
	u16			mtu;
	int			port_width_cap;
	u16			vl_cap;
	u16			max_gid;
	u16			max_pkey;
	u64			guid0;
	u64			node_guid;
	u64			si_guid;
};

#define MAD_IFC_DATA_SZ 192
/* MAD IFC Mailbox */
struct mlx4_mad_ifc {
	u8	base_version;
	u8	mgmt_class;
	u8	class_version;
	u8	method;
	__be16	status;
	__be16	class_specific;
	__be64	tid;
	__be16	attr_id;
	__be16	resv;
	__be32	attr_mod;
	__be64	mkey;
	__be16	dr_slid;
	__be16	dr_dlid;
	u8	reserved[28];
	u8	data[MAD_IFC_DATA_SZ];
} __packed;

#define mlx4_foreach_port(port, dev, type)				\
	for ((port) = 1; (port) <= (dev)->caps.num_ports; (port)++)	\
		if ((type) == (dev)->caps.port_mask[(port)])

#define mlx4_foreach_non_ib_transport_port(port, dev)                     \
	for ((port) = 1; (port) <= (dev)->caps.num_ports; (port)++)	  \
		if (((dev)->caps.port_mask[port] != MLX4_PORT_TYPE_IB))

#define mlx4_foreach_ib_transport_port(port, dev)                         \
	for ((port) = 1; (port) <= (dev)->caps.num_ports; (port)++)	  \
		if (((dev)->caps.port_mask[port] == MLX4_PORT_TYPE_IB) || \
			((dev)->caps.flags & MLX4_DEV_CAP_FLAG_IBOE))

#define MLX4_INVALID_SLAVE_ID	0xFF

void handle_port_mgmt_change_event(struct work_struct *work);

static inline int mlx4_master_func_num(struct mlx4_dev *dev)
{
	return dev->caps.function;
}

static inline int mlx4_is_master(struct mlx4_dev *dev)
{
	return dev->flags & MLX4_FLAG_MASTER;
}

static inline int mlx4_num_reserved_sqps(struct mlx4_dev *dev)
{
	return dev->phys_caps.base_sqpn + 8 +
		16 * MLX4_MFUNC_MAX * !!mlx4_is_master(dev);
}

static inline int mlx4_is_qp_reserved(struct mlx4_dev *dev, u32 qpn)
{
	return (qpn < dev->phys_caps.base_sqpn + 8 +
		16 * MLX4_MFUNC_MAX * !!mlx4_is_master(dev) &&
		qpn >= dev->phys_caps.base_sqpn) ||
	       (qpn < dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW]);
}

static inline int mlx4_is_guest_proxy(struct mlx4_dev *dev, int slave, u32 qpn)
{
	int guest_proxy_base = dev->phys_caps.base_proxy_sqpn + slave * 8;

	if (qpn >= guest_proxy_base && qpn < guest_proxy_base + 8)
		return 1;

	return 0;
}

static inline int mlx4_is_mfunc(struct mlx4_dev *dev)
{
	return dev->flags & (MLX4_FLAG_SLAVE | MLX4_FLAG_MASTER);
}

static inline int mlx4_is_slave(struct mlx4_dev *dev)
{
	return dev->flags & MLX4_FLAG_SLAVE;
}

int mlx4_buf_alloc(struct mlx4_dev *dev, int size, int max_direct,
		   struct mlx4_buf *buf, gfp_t gfp);
void mlx4_buf_free(struct mlx4_dev *dev, int size, struct mlx4_buf *buf);
static inline void *mlx4_buf_offset(struct mlx4_buf *buf, int offset)
{
	if (BITS_PER_LONG == 64 || buf->nbufs == 1)
		return buf->direct.buf + offset;
	else
		return buf->page_list[offset >> PAGE_SHIFT].buf +
			(offset & (PAGE_SIZE - 1));
}

int mlx4_pd_alloc(struct mlx4_dev *dev, u32 *pdn);
void mlx4_pd_free(struct mlx4_dev *dev, u32 pdn);
int mlx4_xrcd_alloc(struct mlx4_dev *dev, u32 *xrcdn);
void mlx4_xrcd_free(struct mlx4_dev *dev, u32 xrcdn);

int mlx4_uar_alloc(struct mlx4_dev *dev, struct mlx4_uar *uar);
void mlx4_uar_free(struct mlx4_dev *dev, struct mlx4_uar *uar);
int mlx4_bf_alloc(struct mlx4_dev *dev, struct mlx4_bf *bf, int node);
void mlx4_bf_free(struct mlx4_dev *dev, struct mlx4_bf *bf);

int mlx4_mtt_init(struct mlx4_dev *dev, int npages, int page_shift,
		  struct mlx4_mtt *mtt);
void mlx4_mtt_cleanup(struct mlx4_dev *dev, struct mlx4_mtt *mtt);
u64 mlx4_mtt_addr(struct mlx4_dev *dev, struct mlx4_mtt *mtt);

int mlx4_mr_alloc(struct mlx4_dev *dev, u32 pd, u64 iova, u64 size, u32 access,
		  int npages, int page_shift, struct mlx4_mr *mr);
int mlx4_mr_free(struct mlx4_dev *dev, struct mlx4_mr *mr);
int mlx4_mr_enable(struct mlx4_dev *dev, struct mlx4_mr *mr);
int mlx4_mw_alloc(struct mlx4_dev *dev, u32 pd, enum mlx4_mw_type type,
		  struct mlx4_mw *mw);
void mlx4_mw_free(struct mlx4_dev *dev, struct mlx4_mw *mw);
int mlx4_mw_enable(struct mlx4_dev *dev, struct mlx4_mw *mw);
int mlx4_write_mtt(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
		   int start_index, int npages, u64 *page_list);
int mlx4_buf_write_mtt(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
		       struct mlx4_buf *buf, gfp_t gfp);

int mlx4_db_alloc(struct mlx4_dev *dev, struct mlx4_db *db, int order,
		  gfp_t gfp);
void mlx4_db_free(struct mlx4_dev *dev, struct mlx4_db *db);

int mlx4_alloc_hwq_res(struct mlx4_dev *dev, struct mlx4_hwq_resources *wqres,
		       int size, int max_direct);
void mlx4_free_hwq_res(struct mlx4_dev *mdev, struct mlx4_hwq_resources *wqres,
		       int size);

int mlx4_cq_alloc(struct mlx4_dev *dev, int nent, struct mlx4_mtt *mtt,
		  struct mlx4_uar *uar, u64 db_rec, struct mlx4_cq *cq,
		  unsigned vector, int collapsed, int timestamp_en);
void mlx4_cq_free(struct mlx4_dev *dev, struct mlx4_cq *cq);
int mlx4_qp_reserve_range(struct mlx4_dev *dev, int cnt, int align,
			  int *base, u8 flags);
void mlx4_qp_release_range(struct mlx4_dev *dev, int base_qpn, int cnt);

int mlx4_qp_alloc(struct mlx4_dev *dev, int qpn, struct mlx4_qp *qp,
		  gfp_t gfp);
void mlx4_qp_free(struct mlx4_dev *dev, struct mlx4_qp *qp);

int mlx4_srq_alloc(struct mlx4_dev *dev, u32 pdn, u32 cqn, u16 xrcdn,
		   struct mlx4_mtt *mtt, u64 db_rec, struct mlx4_srq *srq);
void mlx4_srq_free(struct mlx4_dev *dev, struct mlx4_srq *srq);
int mlx4_srq_arm(struct mlx4_dev *dev, struct mlx4_srq *srq, int limit_watermark);
int mlx4_srq_query(struct mlx4_dev *dev, struct mlx4_srq *srq, int *limit_watermark);

int mlx4_INIT_PORT(struct mlx4_dev *dev, int port);
int mlx4_CLOSE_PORT(struct mlx4_dev *dev, int port);

int mlx4_unicast_attach(struct mlx4_dev *dev, struct mlx4_qp *qp, u8 gid[16],
			int block_mcast_loopback, enum mlx4_protocol prot);
int mlx4_unicast_detach(struct mlx4_dev *dev, struct mlx4_qp *qp, u8 gid[16],
			enum mlx4_protocol prot);
int mlx4_multicast_attach(struct mlx4_dev *dev, struct mlx4_qp *qp, u8 gid[16],
			  u8 port, int block_mcast_loopback,
			  enum mlx4_protocol protocol, u64 *reg_id);
int mlx4_multicast_detach(struct mlx4_dev *dev, struct mlx4_qp *qp, u8 gid[16],
			  enum mlx4_protocol protocol, u64 reg_id);

enum {
	MLX4_DOMAIN_UVERBS	= 0x1000,
	MLX4_DOMAIN_ETHTOOL     = 0x2000,
	MLX4_DOMAIN_RFS         = 0x3000,
	MLX4_DOMAIN_NIC    = 0x5000,
};

enum mlx4_net_trans_rule_id {
	MLX4_NET_TRANS_RULE_ID_ETH = 0,
	MLX4_NET_TRANS_RULE_ID_IB,
	MLX4_NET_TRANS_RULE_ID_IPV6,
	MLX4_NET_TRANS_RULE_ID_IPV4,
	MLX4_NET_TRANS_RULE_ID_TCP,
	MLX4_NET_TRANS_RULE_ID_UDP,
	MLX4_NET_TRANS_RULE_ID_VXLAN,
	MLX4_NET_TRANS_RULE_NUM, /* should be last */
};

extern const u16 __sw_id_hw[];

static inline int map_hw_to_sw_id(u16 header_id)
{

	int i;
	for (i = 0; i < MLX4_NET_TRANS_RULE_NUM; i++) {
		if (header_id == __sw_id_hw[i])
			return i;
	}
	return -EINVAL;
}

enum mlx4_net_trans_promisc_mode {
	MLX4_FS_REGULAR = 1,
	MLX4_FS_ALL_DEFAULT,
	MLX4_FS_MC_DEFAULT,
	MLX4_FS_UC_SNIFFER,
	MLX4_FS_MC_SNIFFER,
	MLX4_FS_MODE_NUM, /* should be last */
};

struct mlx4_spec_eth {
	u8	dst_mac[ETH_ALEN];
	u8	dst_mac_msk[ETH_ALEN];
	u8	src_mac[ETH_ALEN];
	u8	src_mac_msk[ETH_ALEN];
	u8	ether_type_enable;
	__be16	ether_type;
	__be16	vlan_id_msk;
	__be16	vlan_id;
};

struct mlx4_spec_tcp_udp {
	__be16 dst_port;
	__be16 dst_port_msk;
	__be16 src_port;
	__be16 src_port_msk;
};

struct mlx4_spec_ipv4 {
	__be32 dst_ip;
	__be32 dst_ip_msk;
	__be32 src_ip;
	__be32 src_ip_msk;
};

struct mlx4_spec_ib {
	__be32  l3_qpn;
	__be32	qpn_msk;
	u8	dst_gid[16];
	u8	dst_gid_msk[16];
};

struct mlx4_spec_vxlan {
	__be32 vni;
	__be32 vni_mask;

};

struct mlx4_spec_list {
	struct	list_head list;
	enum	mlx4_net_trans_rule_id id;
	union {
		struct mlx4_spec_eth eth;
		struct mlx4_spec_ib ib;
		struct mlx4_spec_ipv4 ipv4;
		struct mlx4_spec_tcp_udp tcp_udp;
		struct mlx4_spec_vxlan vxlan;
	};
};

enum mlx4_net_trans_hw_rule_queue {
	MLX4_NET_TRANS_Q_FIFO,
	MLX4_NET_TRANS_Q_LIFO,
};

struct mlx4_net_trans_rule {
	struct	list_head list;
	enum	mlx4_net_trans_hw_rule_queue queue_mode;
	bool	exclusive;
	bool	allow_loopback;
	enum	mlx4_net_trans_promisc_mode promisc_mode;
	u8	port;
	u16	priority;
	u32	qpn;
};

struct mlx4_net_trans_rule_hw_ctrl {
	__be16 prio;
	u8 type;
	u8 flags;
	u8 rsvd1;
	u8 funcid;
	u8 vep;
	u8 port;
	__be32 qpn;
	__be32 rsvd2;
};

struct mlx4_net_trans_rule_hw_ib {
	u8 size;
	u8 rsvd1;
	__be16 id;
	u32 rsvd2;
	__be32 l3_qpn;
	__be32 qpn_mask;
	u8 dst_gid[16];
	u8 dst_gid_msk[16];
} __packed;

struct mlx4_net_trans_rule_hw_eth {
	u8	size;
	u8	rsvd;
	__be16	id;
	u8	rsvd1[6];
	u8	dst_mac[6];
	u16	rsvd2;
	u8	dst_mac_msk[6];
	u16	rsvd3;
	u8	src_mac[6];
	u16	rsvd4;
	u8	src_mac_msk[6];
	u8      rsvd5;
	u8      ether_type_enable;
	__be16  ether_type;
	__be16  vlan_tag_msk;
	__be16  vlan_tag;
} __packed;

struct mlx4_net_trans_rule_hw_tcp_udp {
	u8	size;
	u8	rsvd;
	__be16	id;
	__be16	rsvd1[3];
	__be16	dst_port;
	__be16	rsvd2;
	__be16	dst_port_msk;
	__be16	rsvd3;
	__be16	src_port;
	__be16	rsvd4;
	__be16	src_port_msk;
} __packed;

struct mlx4_net_trans_rule_hw_ipv4 {
	u8	size;
	u8	rsvd;
	__be16	id;
	__be32	rsvd1;
	__be32	dst_ip;
	__be32	dst_ip_msk;
	__be32	src_ip;
	__be32	src_ip_msk;
} __packed;

struct mlx4_net_trans_rule_hw_vxlan {
	u8	size;
	u8	rsvd;
	__be16	id;
	__be32	rsvd1;
	__be32	vni;
	__be32	vni_mask;
} __packed;

struct _rule_hw {
	union {
		struct {
			u8 size;
			u8 rsvd;
			__be16 id;
		};
		struct mlx4_net_trans_rule_hw_eth eth;
		struct mlx4_net_trans_rule_hw_ib ib;
		struct mlx4_net_trans_rule_hw_ipv4 ipv4;
		struct mlx4_net_trans_rule_hw_tcp_udp tcp_udp;
		struct mlx4_net_trans_rule_hw_vxlan vxlan;
	};
};

enum {
	VXLAN_STEER_BY_OUTER_MAC	= 1 << 0,
	VXLAN_STEER_BY_OUTER_VLAN	= 1 << 1,
	VXLAN_STEER_BY_VSID_VNI		= 1 << 2,
	VXLAN_STEER_BY_INNER_MAC	= 1 << 3,
	VXLAN_STEER_BY_INNER_VLAN	= 1 << 4,
};


int mlx4_flow_steer_promisc_add(struct mlx4_dev *dev, u8 port, u32 qpn,
				enum mlx4_net_trans_promisc_mode mode);
int mlx4_flow_steer_promisc_remove(struct mlx4_dev *dev, u8 port,
				   enum mlx4_net_trans_promisc_mode mode);
int mlx4_multicast_promisc_add(struct mlx4_dev *dev, u32 qpn, u8 port);
int mlx4_multicast_promisc_remove(struct mlx4_dev *dev, u32 qpn, u8 port);
int mlx4_unicast_promisc_add(struct mlx4_dev *dev, u32 qpn, u8 port);
int mlx4_unicast_promisc_remove(struct mlx4_dev *dev, u32 qpn, u8 port);
int mlx4_SET_MCAST_FLTR(struct mlx4_dev *dev, u8 port, u64 mac, u64 clear, u8 mode);

int mlx4_register_mac(struct mlx4_dev *dev, u8 port, u64 mac);
void mlx4_unregister_mac(struct mlx4_dev *dev, u8 port, u64 mac);
int mlx4_get_base_qpn(struct mlx4_dev *dev, u8 port);
int __mlx4_replace_mac(struct mlx4_dev *dev, u8 port, int qpn, u64 new_mac);
void mlx4_set_stats_bitmap(struct mlx4_dev *dev, u64 *stats_bitmap);
int mlx4_SET_PORT_general(struct mlx4_dev *dev, u8 port, int mtu,
			  u8 pptx, u8 pfctx, u8 pprx, u8 pfcrx);
int mlx4_SET_PORT_qpn_calc(struct mlx4_dev *dev, u8 port, u32 base_qpn,
			   u8 promisc);
int mlx4_SET_PORT_PRIO2TC(struct mlx4_dev *dev, u8 port, u8 *prio2tc);
int mlx4_SET_PORT_SCHEDULER(struct mlx4_dev *dev, u8 port, u8 *tc_tx_bw,
		u8 *pg, u16 *ratelimit);
int mlx4_SET_PORT_VXLAN(struct mlx4_dev *dev, u8 port, u8 steering, int enable);
int mlx4_find_cached_mac(struct mlx4_dev *dev, u8 port, u64 mac, int *idx);
int mlx4_find_cached_vlan(struct mlx4_dev *dev, u8 port, u16 vid, int *idx);
int mlx4_register_vlan(struct mlx4_dev *dev, u8 port, u16 vlan, int *index);
void mlx4_unregister_vlan(struct mlx4_dev *dev, u8 port, u16 vlan);

int mlx4_map_phys_fmr(struct mlx4_dev *dev, struct mlx4_fmr *fmr, u64 *page_list,
		      int npages, u64 iova, u32 *lkey, u32 *rkey);
int mlx4_fmr_alloc(struct mlx4_dev *dev, u32 pd, u32 access, int max_pages,
		   int max_maps, u8 page_shift, struct mlx4_fmr *fmr);
int mlx4_fmr_enable(struct mlx4_dev *dev, struct mlx4_fmr *fmr);
void mlx4_fmr_unmap(struct mlx4_dev *dev, struct mlx4_fmr *fmr,
		    u32 *lkey, u32 *rkey);
int mlx4_fmr_free(struct mlx4_dev *dev, struct mlx4_fmr *fmr);
int mlx4_SYNC_TPT(struct mlx4_dev *dev);
int mlx4_test_interrupts(struct mlx4_dev *dev);
int mlx4_assign_eq(struct mlx4_dev *dev, char *name, struct cpu_rmap *rmap,
		   int *vector);
void mlx4_release_eq(struct mlx4_dev *dev, int vec);

int mlx4_eq_get_irq(struct mlx4_dev *dev, int vec);

int mlx4_get_phys_port_id(struct mlx4_dev *dev);
int mlx4_wol_read(struct mlx4_dev *dev, u64 *config, int port);
int mlx4_wol_write(struct mlx4_dev *dev, u64 config, int port);

int mlx4_counter_alloc(struct mlx4_dev *dev, u32 *idx);
void mlx4_counter_free(struct mlx4_dev *dev, u32 idx);

int mlx4_flow_attach(struct mlx4_dev *dev,
		     struct mlx4_net_trans_rule *rule, u64 *reg_id);
int mlx4_flow_detach(struct mlx4_dev *dev, u64 reg_id);
int mlx4_map_sw_to_hw_steering_mode(struct mlx4_dev *dev,
				    enum mlx4_net_trans_promisc_mode flow_type);
int mlx4_map_sw_to_hw_steering_id(struct mlx4_dev *dev,
				  enum mlx4_net_trans_rule_id id);
int mlx4_hw_rule_sz(struct mlx4_dev *dev, enum mlx4_net_trans_rule_id id);

int mlx4_tunnel_steer_add(struct mlx4_dev *dev, unsigned char *addr,
			  int port, int qpn, u16 prio, u64 *reg_id);

void mlx4_sync_pkey_table(struct mlx4_dev *dev, int slave, int port,
			  int i, int val);

int mlx4_get_parav_qkey(struct mlx4_dev *dev, u32 qpn, u32 *qkey);

int mlx4_is_slave_active(struct mlx4_dev *dev, int slave);
int mlx4_gen_pkey_eqe(struct mlx4_dev *dev, int slave, u8 port);
int mlx4_gen_guid_change_eqe(struct mlx4_dev *dev, int slave, u8 port);
int mlx4_gen_slaves_port_mgt_ev(struct mlx4_dev *dev, u8 port, int attr);
int mlx4_gen_port_state_change_eqe(struct mlx4_dev *dev, int slave, u8 port, u8 port_subtype_change);
enum slave_port_state mlx4_get_slave_port_state(struct mlx4_dev *dev, int slave, u8 port);
int set_and_calc_slave_port_state(struct mlx4_dev *dev, int slave, u8 port, int event, enum slave_port_gen_event *gen_event);

void mlx4_put_slave_node_guid(struct mlx4_dev *dev, int slave, __be64 guid);
__be64 mlx4_get_slave_node_guid(struct mlx4_dev *dev, int slave);

int mlx4_get_slave_from_roce_gid(struct mlx4_dev *dev, int port, u8 *gid,
				 int *slave_id);
int mlx4_get_roce_gid_from_slave(struct mlx4_dev *dev, int port, int slave_id,
				 u8 *gid);

int mlx4_FLOW_STEERING_IB_UC_QP_RANGE(struct mlx4_dev *dev, u32 min_range_qpn,
				      u32 max_range_qpn);

cycle_t mlx4_read_clock(struct mlx4_dev *dev);

struct mlx4_active_ports {
	DECLARE_BITMAP(ports, MLX4_MAX_PORTS);
};
/* Returns a bitmap of the physical ports which are assigned to slave */
struct mlx4_active_ports mlx4_get_active_ports(struct mlx4_dev *dev, int slave);

/* Returns the physical port that represents the virtual port of the slave, */
/* or a value < 0 in case of an error. If a slave has 2 ports, the identity */
/* mapping is returned.							    */
int mlx4_slave_convert_port(struct mlx4_dev *dev, int slave, int port);

struct mlx4_slaves_pport {
	DECLARE_BITMAP(slaves, MLX4_MFUNC_MAX);
};
/* Returns a bitmap of all slaves that are assigned to port. */
struct mlx4_slaves_pport mlx4_phys_to_slaves_pport(struct mlx4_dev *dev,
						   int port);

/* Returns a bitmap of all slaves that are assigned exactly to all the */
/* the ports that are set in crit_ports.			       */
struct mlx4_slaves_pport mlx4_phys_to_slaves_pport_actv(
		struct mlx4_dev *dev,
		const struct mlx4_active_ports *crit_ports);

/* Returns the slave's virtual port that represents the physical port. */
int mlx4_phys_to_slave_port(struct mlx4_dev *dev, int slave, int port);

int mlx4_get_base_gid_ix(struct mlx4_dev *dev, int slave, int port);

int mlx4_config_vxlan_port(struct mlx4_dev *dev, __be16 udp_port);
int mlx4_vf_smi_enabled(struct mlx4_dev *dev, int slave, int port);
int mlx4_vf_get_enable_smi_admin(struct mlx4_dev *dev, int slave, int port);
int mlx4_vf_set_enable_smi_admin(struct mlx4_dev *dev, int slave, int port,
				 int enable);
int mlx4_mr_hw_get_mpt(struct mlx4_dev *dev, struct mlx4_mr *mmr,
		       struct mlx4_mpt_entry ***mpt_entry);
int mlx4_mr_hw_write_mpt(struct mlx4_dev *dev, struct mlx4_mr *mmr,
			 struct mlx4_mpt_entry **mpt_entry);
int mlx4_mr_hw_change_pd(struct mlx4_dev *dev, struct mlx4_mpt_entry *mpt_entry,
			 u32 pdn);
int mlx4_mr_hw_change_access(struct mlx4_dev *dev,
			     struct mlx4_mpt_entry *mpt_entry,
			     u32 access);
void mlx4_mr_hw_put_mpt(struct mlx4_dev *dev,
			struct mlx4_mpt_entry **mpt_entry);
void mlx4_mr_rereg_mem_cleanup(struct mlx4_dev *dev, struct mlx4_mr *mr);
int mlx4_mr_rereg_mem_write(struct mlx4_dev *dev, struct mlx4_mr *mr,
			    u64 iova, u64 size, int npages,
			    int page_shift, struct mlx4_mpt_entry *mpt_entry);

int mlx4_get_module_info(struct mlx4_dev *dev, u8 port,
			 u16 offset, u16 size, u8 *data);

/* Returns true if running in low memory profile (kdump kernel) */
static inline bool mlx4_low_memory_profile(void)
{
	return is_kdump_kernel();
}

/* ACCESS REG commands */
enum mlx4_access_reg_method {
	MLX4_ACCESS_REG_QUERY = 0x1,
	MLX4_ACCESS_REG_WRITE = 0x2,
};

/* ACCESS PTYS Reg command */
enum mlx4_ptys_proto {
	MLX4_PTYS_IB = 1<<0,
	MLX4_PTYS_EN = 1<<2,
};

struct mlx4_ptys_reg {
	u8 resrvd1;
	u8 local_port;
	u8 resrvd2;
	u8 proto_mask;
	__be32 resrvd3[2];
	__be32 eth_proto_cap;
	__be16 ib_width_cap;
	__be16 ib_speed_cap;
	__be32 resrvd4;
	__be32 eth_proto_admin;
	__be16 ib_width_admin;
	__be16 ib_speed_admin;
	__be32 resrvd5;
	__be32 eth_proto_oper;
	__be16 ib_width_oper;
	__be16 ib_speed_oper;
	__be32 resrvd6;
	__be32 eth_proto_lp_adv;
} __packed;

int mlx4_ACCESS_PTYS_REG(struct mlx4_dev *dev,
			 enum mlx4_access_reg_method method,
			 struct mlx4_ptys_reg *ptys_reg);

#endif /* MLX4_DEVICE_H */

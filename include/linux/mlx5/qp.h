/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
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

#ifndef MLX5_QP_H
#define MLX5_QP_H

#include <linux/mlx5/device.h>
#include <linux/mlx5/driver.h>

#define MLX5_INVALID_LKEY	0x100
/* UMR (3 WQE_BB's) + SIG (3 WQE_BB's) + PSV (mem) + PSV (wire) */
#define MLX5_SIG_WQE_SIZE	(MLX5_SEND_WQE_BB * 8)
#define MLX5_DIF_SIZE		8
#define MLX5_STRIDE_BLOCK_OP	0x400
#define MLX5_CPY_GRD_MASK	0xc0
#define MLX5_CPY_APP_MASK	0x30
#define MLX5_CPY_REF_MASK	0x0f
#define MLX5_BSF_INC_REFTAG	(1 << 6)
#define MLX5_BSF_INL_VALID	(1 << 15)
#define MLX5_BSF_REFRESH_DIF	(1 << 14)
#define MLX5_BSF_REPEAT_BLOCK	(1 << 7)
#define MLX5_BSF_APPTAG_ESCAPE	0x1
#define MLX5_BSF_APPREF_ESCAPE	0x2

enum mlx5_qp_optpar {
	MLX5_QP_OPTPAR_ALT_ADDR_PATH		= 1 << 0,
	MLX5_QP_OPTPAR_RRE			= 1 << 1,
	MLX5_QP_OPTPAR_RAE			= 1 << 2,
	MLX5_QP_OPTPAR_RWE			= 1 << 3,
	MLX5_QP_OPTPAR_PKEY_INDEX		= 1 << 4,
	MLX5_QP_OPTPAR_Q_KEY			= 1 << 5,
	MLX5_QP_OPTPAR_RNR_TIMEOUT		= 1 << 6,
	MLX5_QP_OPTPAR_PRIMARY_ADDR_PATH	= 1 << 7,
	MLX5_QP_OPTPAR_SRA_MAX			= 1 << 8,
	MLX5_QP_OPTPAR_RRA_MAX			= 1 << 9,
	MLX5_QP_OPTPAR_PM_STATE			= 1 << 10,
	MLX5_QP_OPTPAR_RETRY_COUNT		= 1 << 12,
	MLX5_QP_OPTPAR_RNR_RETRY		= 1 << 13,
	MLX5_QP_OPTPAR_ACK_TIMEOUT		= 1 << 14,
	MLX5_QP_OPTPAR_PRI_PORT			= 1 << 16,
	MLX5_QP_OPTPAR_SRQN			= 1 << 18,
	MLX5_QP_OPTPAR_CQN_RCV			= 1 << 19,
	MLX5_QP_OPTPAR_DC_HS			= 1 << 20,
	MLX5_QP_OPTPAR_DC_KEY			= 1 << 21,
};

enum mlx5_qp_state {
	MLX5_QP_STATE_RST			= 0,
	MLX5_QP_STATE_INIT			= 1,
	MLX5_QP_STATE_RTR			= 2,
	MLX5_QP_STATE_RTS			= 3,
	MLX5_QP_STATE_SQER			= 4,
	MLX5_QP_STATE_SQD			= 5,
	MLX5_QP_STATE_ERR			= 6,
	MLX5_QP_STATE_SQ_DRAINING		= 7,
	MLX5_QP_STATE_SUSPENDED			= 9,
	MLX5_QP_NUM_STATE,
	MLX5_QP_STATE,
	MLX5_QP_STATE_BAD,
};

enum {
	MLX5_SQ_STATE_NA	= MLX5_SQC_STATE_ERR + 1,
	MLX5_SQ_NUM_STATE	= MLX5_SQ_STATE_NA + 1,
	MLX5_RQ_STATE_NA	= MLX5_RQC_STATE_ERR + 1,
	MLX5_RQ_NUM_STATE	= MLX5_RQ_STATE_NA + 1,
};

enum {
	MLX5_QP_ST_RC				= 0x0,
	MLX5_QP_ST_UC				= 0x1,
	MLX5_QP_ST_UD				= 0x2,
	MLX5_QP_ST_XRC				= 0x3,
	MLX5_QP_ST_MLX				= 0x4,
	MLX5_QP_ST_DCI				= 0x5,
	MLX5_QP_ST_DCT				= 0x6,
	MLX5_QP_ST_QP0				= 0x7,
	MLX5_QP_ST_QP1				= 0x8,
	MLX5_QP_ST_RAW_ETHERTYPE		= 0x9,
	MLX5_QP_ST_RAW_IPV6			= 0xa,
	MLX5_QP_ST_SNIFFER			= 0xb,
	MLX5_QP_ST_SYNC_UMR			= 0xe,
	MLX5_QP_ST_PTP_1588			= 0xd,
	MLX5_QP_ST_REG_UMR			= 0xc,
	MLX5_QP_ST_MAX
};

enum {
	MLX5_QP_PM_MIGRATED			= 0x3,
	MLX5_QP_PM_ARMED			= 0x0,
	MLX5_QP_PM_REARM			= 0x1
};

enum {
	MLX5_NON_ZERO_RQ	= 0x0,
	MLX5_SRQ_RQ		= 0x1,
	MLX5_CRQ_RQ		= 0x2,
	MLX5_ZERO_LEN_RQ	= 0x3
};

/* TODO REM */
enum {
	/* params1 */
	MLX5_QP_BIT_SRE				= 1 << 15,
	MLX5_QP_BIT_SWE				= 1 << 14,
	MLX5_QP_BIT_SAE				= 1 << 13,
	/* params2 */
	MLX5_QP_BIT_RRE				= 1 << 15,
	MLX5_QP_BIT_RWE				= 1 << 14,
	MLX5_QP_BIT_RAE				= 1 << 13,
	MLX5_QP_BIT_RIC				= 1 <<	4,
	MLX5_QP_BIT_CC_SLAVE_RECV		= 1 <<  2,
	MLX5_QP_BIT_CC_SLAVE_SEND		= 1 <<  1,
	MLX5_QP_BIT_CC_MASTER			= 1 <<  0
};

enum {
	MLX5_WQE_CTRL_CQ_UPDATE		= 2 << 2,
	MLX5_WQE_CTRL_CQ_UPDATE_AND_EQE	= 3 << 2,
	MLX5_WQE_CTRL_SOLICITED		= 1 << 1,
};

enum {
	MLX5_SEND_WQE_DS	= 16,
	MLX5_SEND_WQE_BB	= 64,
};

#define MLX5_SEND_WQEBB_NUM_DS	(MLX5_SEND_WQE_BB / MLX5_SEND_WQE_DS)

enum {
	MLX5_SEND_WQE_MAX_WQEBBS	= 16,
};

enum {
	MLX5_WQE_FMR_PERM_LOCAL_READ	= 1 << 27,
	MLX5_WQE_FMR_PERM_LOCAL_WRITE	= 1 << 28,
	MLX5_WQE_FMR_PERM_REMOTE_READ	= 1 << 29,
	MLX5_WQE_FMR_PERM_REMOTE_WRITE	= 1 << 30,
	MLX5_WQE_FMR_PERM_ATOMIC	= 1 << 31
};

enum {
	MLX5_FENCE_MODE_NONE			= 0 << 5,
	MLX5_FENCE_MODE_INITIATOR_SMALL		= 1 << 5,
	MLX5_FENCE_MODE_FENCE			= 2 << 5,
	MLX5_FENCE_MODE_STRONG_ORDERING		= 3 << 5,
	MLX5_FENCE_MODE_SMALL_AND_FENCE		= 4 << 5,
};

enum {
	MLX5_RCV_DBR	= 0,
	MLX5_SND_DBR	= 1,
};

enum {
	MLX5_FLAGS_INLINE	= 1<<7,
	MLX5_FLAGS_CHECK_FREE   = 1<<5,
};

struct mlx5_wqe_fmr_seg {
	__be32			flags;
	__be32			mem_key;
	__be64			buf_list;
	__be64			start_addr;
	__be64			reg_len;
	__be32			offset;
	__be32			page_size;
	u32			reserved[2];
};

struct mlx5_wqe_ctrl_seg {
	__be32			opmod_idx_opcode;
	__be32			qpn_ds;
	u8			signature;
	u8			rsvd[2];
	u8			fm_ce_se;
	__be32			imm;
};

#define MLX5_WQE_CTRL_DS_MASK 0x3f
#define MLX5_WQE_CTRL_QPN_MASK 0xffffff00
#define MLX5_WQE_CTRL_QPN_SHIFT 8
#define MLX5_WQE_DS_UNITS 16
#define MLX5_WQE_CTRL_OPCODE_MASK 0xff
#define MLX5_WQE_CTRL_WQE_INDEX_MASK 0x00ffff00
#define MLX5_WQE_CTRL_WQE_INDEX_SHIFT 8

enum {
	MLX5_ETH_WQE_L3_INNER_CSUM      = 1 << 4,
	MLX5_ETH_WQE_L4_INNER_CSUM      = 1 << 5,
	MLX5_ETH_WQE_L3_CSUM            = 1 << 6,
	MLX5_ETH_WQE_L4_CSUM            = 1 << 7,
};

enum {
	MLX5_ETH_WQE_SVLAN              = 1 << 0,
	MLX5_ETH_WQE_INSERT_VLAN        = 1 << 15,
};

enum {
	MLX5_ETH_WQE_SWP_INNER_L3_IPV6  = 1 << 0,
	MLX5_ETH_WQE_SWP_INNER_L4_UDP   = 1 << 1,
	MLX5_ETH_WQE_SWP_OUTER_L3_IPV6  = 1 << 4,
	MLX5_ETH_WQE_SWP_OUTER_L4_UDP   = 1 << 5,
};

struct mlx5_wqe_eth_seg {
	u8              swp_outer_l4_offset;
	u8              swp_outer_l3_offset;
	u8              swp_inner_l4_offset;
	u8              swp_inner_l3_offset;
	u8              cs_flags;
	u8              swp_flags;
	__be16          mss;
	__be32          rsvd2;
	union {
		struct {
			__be16 sz;
			u8     start[2];
		} inline_hdr;
		struct {
			__be16 type;
			__be16 vlan_tci;
		} insert;
	};
};

struct mlx5_wqe_xrc_seg {
	__be32			xrc_srqn;
	u8			rsvd[12];
};

struct mlx5_wqe_masked_atomic_seg {
	__be64			swap_add;
	__be64			compare;
	__be64			swap_add_mask;
	__be64			compare_mask;
};

struct mlx5_base_av {
	union {
		struct {
			__be32	qkey;
			__be32	reserved;
		} qkey;
		__be64	dc_key;
	} key;
	__be32	dqp_dct;
	u8	stat_rate_sl;
	u8	fl_mlid;
	union {
		__be16	rlid;
		__be16  udp_sport;
	};
};

struct mlx5_av {
	union {
		struct {
			__be32	qkey;
			__be32	reserved;
		} qkey;
		__be64	dc_key;
	} key;
	__be32	dqp_dct;
	u8	stat_rate_sl;
	u8	fl_mlid;
	union {
		__be16	rlid;
		__be16  udp_sport;
	};
	u8	reserved0[4];
	u8	rmac[6];
	u8	tclass;
	u8	hop_limit;
	__be32	grh_gid_fl;
	u8	rgid[16];
};

struct mlx5_ib_ah {
	struct ib_ah		ibah;
	struct mlx5_av		av;
};

static inline struct mlx5_ib_ah *to_mah(struct ib_ah *ibah)
{
	return container_of(ibah, struct mlx5_ib_ah, ibah);
}

struct mlx5_wqe_datagram_seg {
	struct mlx5_av	av;
};

struct mlx5_wqe_raddr_seg {
	__be64			raddr;
	__be32			rkey;
	u32			reserved;
};

struct mlx5_wqe_atomic_seg {
	__be64			swap_add;
	__be64			compare;
};

struct mlx5_wqe_data_seg {
	__be32			byte_count;
	__be32			lkey;
	__be64			addr;
};

struct mlx5_wqe_umr_ctrl_seg {
	u8		flags;
	u8		rsvd0[3];
	__be16		xlt_octowords;
	union {
		__be16	xlt_offset;
		__be16	bsf_octowords;
	};
	__be64		mkey_mask;
	__be32		xlt_offset_47_16;
	u8		rsvd1[28];
};

struct mlx5_seg_set_psv {
	__be32		psv_num;
	__be16		syndrome;
	__be16		status;
	__be32		transient_sig;
	__be32		ref_tag;
};

struct mlx5_seg_get_psv {
	u8		rsvd[19];
	u8		num_psv;
	__be32		l_key;
	__be64		va;
	__be32		psv_index[4];
};

struct mlx5_seg_check_psv {
	u8		rsvd0[2];
	__be16		err_coalescing_op;
	u8		rsvd1[2];
	__be16		xport_err_op;
	u8		rsvd2[2];
	__be16		xport_err_mask;
	u8		rsvd3[7];
	u8		num_psv;
	__be32		l_key;
	__be64		va;
	__be32		psv_index[4];
};

struct mlx5_rwqe_sig {
	u8	rsvd0[4];
	u8	signature;
	u8	rsvd1[11];
};

struct mlx5_wqe_signature_seg {
	u8	rsvd0[4];
	u8	signature;
	u8	rsvd1[11];
};

#define MLX5_WQE_INLINE_SEG_BYTE_COUNT_MASK 0x3ff

struct mlx5_wqe_inline_seg {
	__be32	byte_count;
	__be32	data[0];
};

enum mlx5_sig_type {
	MLX5_DIF_CRC = 0x1,
	MLX5_DIF_IPCS = 0x2,
};

struct mlx5_bsf_inl {
	__be16		vld_refresh;
	__be16		dif_apptag;
	__be32		dif_reftag;
	u8		sig_type;
	u8		rp_inv_seed;
	u8		rsvd[3];
	u8		dif_inc_ref_guard_check;
	__be16		dif_app_bitmask_check;
};

struct mlx5_bsf {
	struct mlx5_bsf_basic {
		u8		bsf_size_sbs;
		u8		check_byte_mask;
		union {
			u8	copy_byte_mask;
			u8	bs_selector;
			u8	rsvd_wflags;
		} wire;
		union {
			u8	bs_selector;
			u8	rsvd_mflags;
		} mem;
		__be32		raw_data_size;
		__be32		w_bfs_psv;
		__be32		m_bfs_psv;
	} basic;
	struct mlx5_bsf_ext {
		__be32		t_init_gen_pro_size;
		__be32		rsvd_epi_size;
		__be32		w_tfs_psv;
		__be32		m_tfs_psv;
	} ext;
	struct mlx5_bsf_inl	w_inl;
	struct mlx5_bsf_inl	m_inl;
};

struct mlx5_mtt {
	__be64		ptag;
};

struct mlx5_klm {
	__be32		bcount;
	__be32		key;
	__be64		va;
};

struct mlx5_stride_block_entry {
	__be16		stride;
	__be16		bcount;
	__be32		key;
	__be64		va;
};

struct mlx5_stride_block_ctrl_seg {
	__be32		bcount_per_cycle;
	__be32		op;
	__be32		repeat_count;
	u16		rsvd;
	__be16		num_entries;
};

struct mlx5_core_qp {
	struct mlx5_core_rsc_common	common; /* must be first */
	void (*event)		(struct mlx5_core_qp *, int);
	int			qpn;
	struct mlx5_rsc_debug	*dbg;
	int			pid;
	u16			uid;
};

struct mlx5_core_dct {
	struct mlx5_core_qp	mqp;
	struct completion	drained;
};

struct mlx5_qp_path {
	u8			fl_free_ar;
	u8			rsvd3;
	__be16			pkey_index;
	u8			rsvd0;
	u8			grh_mlid;
	__be16			rlid;
	u8			ackto_lt;
	u8			mgid_index;
	u8			static_rate;
	u8			hop_limit;
	__be32			tclass_flowlabel;
	union {
		u8		rgid[16];
		u8		rip[16];
	};
	u8			f_dscp_ecn_prio;
	u8			ecn_dscp;
	__be16			udp_sport;
	u8			dci_cfi_prio_sl;
	u8			port;
	u8			rmac[6];
};

/* FIXME: use mlx5_ifc.h qpc */
struct mlx5_qp_context {
	__be32			flags;
	__be32			flags_pd;
	u8			mtu_msgmax;
	u8			rq_size_stride;
	__be16			sq_crq_size;
	__be32			qp_counter_set_usr_page;
	__be32			wire_qpn;
	__be32			log_pg_sz_remote_qpn;
	struct			mlx5_qp_path pri_path;
	struct			mlx5_qp_path alt_path;
	__be32			params1;
	u8			reserved2[4];
	__be32			next_send_psn;
	__be32			cqn_send;
	__be32			deth_sqpn;
	u8			reserved3[4];
	__be32			last_acked_psn;
	__be32			ssn;
	__be32			params2;
	__be32			rnr_nextrecvpsn;
	__be32			xrcd;
	__be32			cqn_recv;
	__be64			db_rec_addr;
	__be32			qkey;
	__be32			rq_type_srqn;
	__be32			rmsn;
	__be16			hw_sq_wqe_counter;
	__be16			sw_sq_wqe_counter;
	__be16			hw_rcyclic_byte_counter;
	__be16			hw_rq_counter;
	__be16			sw_rcyclic_byte_counter;
	__be16			sw_rq_counter;
	u8			rsvd0[5];
	u8			cgs;
	u8			cs_req;
	u8			cs_res;
	__be64			dc_access_key;
	u8			rsvd1[24];
};

static inline struct mlx5_core_qp *__mlx5_qp_lookup(struct mlx5_core_dev *dev, u32 qpn)
{
	return radix_tree_lookup(&dev->priv.qp_table.tree, qpn);
}

static inline struct mlx5_core_mkey *__mlx5_mr_lookup(struct mlx5_core_dev *dev, u32 key)
{
	return radix_tree_lookup(&dev->priv.mkey_table.tree, key);
}

int mlx5_core_create_dct(struct mlx5_core_dev *dev,
			 struct mlx5_core_dct *qp,
			 u32 *in, int inlen,
			 u32 *out, int outlen);
int mlx5_core_create_qp(struct mlx5_core_dev *dev,
			struct mlx5_core_qp *qp,
			u32 *in,
			int inlen);
int mlx5_core_qp_modify(struct mlx5_core_dev *dev, u16 opcode,
			u32 opt_param_mask, void *qpc,
			struct mlx5_core_qp *qp);
int mlx5_core_destroy_qp(struct mlx5_core_dev *dev,
			 struct mlx5_core_qp *qp);
int mlx5_core_destroy_dct(struct mlx5_core_dev *dev,
			  struct mlx5_core_dct *dct);
int mlx5_core_qp_query(struct mlx5_core_dev *dev, struct mlx5_core_qp *qp,
		       u32 *out, int outlen);
int mlx5_core_dct_query(struct mlx5_core_dev *dev, struct mlx5_core_dct *dct,
			u32 *out, int outlen);

int mlx5_core_set_delay_drop(struct mlx5_core_dev *dev,
			     u32 timeout_usec);

int mlx5_core_xrcd_alloc(struct mlx5_core_dev *dev, u32 *xrcdn);
int mlx5_core_xrcd_dealloc(struct mlx5_core_dev *dev, u32 xrcdn);
void mlx5_init_qp_table(struct mlx5_core_dev *dev);
void mlx5_cleanup_qp_table(struct mlx5_core_dev *dev);
int mlx5_debug_qp_add(struct mlx5_core_dev *dev, struct mlx5_core_qp *qp);
void mlx5_debug_qp_remove(struct mlx5_core_dev *dev, struct mlx5_core_qp *qp);
int mlx5_core_create_rq_tracked(struct mlx5_core_dev *dev, u32 *in, int inlen,
				struct mlx5_core_qp *rq);
void mlx5_core_destroy_rq_tracked(struct mlx5_core_dev *dev,
				  struct mlx5_core_qp *rq);
int mlx5_core_create_sq_tracked(struct mlx5_core_dev *dev, u32 *in, int inlen,
				struct mlx5_core_qp *sq);
void mlx5_core_destroy_sq_tracked(struct mlx5_core_dev *dev,
				  struct mlx5_core_qp *sq);
int mlx5_core_alloc_q_counter(struct mlx5_core_dev *dev, u16 *counter_id);
int mlx5_core_dealloc_q_counter(struct mlx5_core_dev *dev, u16 counter_id);
int mlx5_core_query_q_counter(struct mlx5_core_dev *dev, u16 counter_id,
			      int reset, void *out, int out_size);

struct mlx5_core_rsc_common *mlx5_core_res_hold(struct mlx5_core_dev *dev,
						int res_num,
						enum mlx5_res_type res_type);
void mlx5_core_res_put(struct mlx5_core_rsc_common *res);

static inline const char *mlx5_qp_type_str(int type)
{
	switch (type) {
	case MLX5_QP_ST_RC: return "RC";
	case MLX5_QP_ST_UC: return "C";
	case MLX5_QP_ST_UD: return "UD";
	case MLX5_QP_ST_XRC: return "XRC";
	case MLX5_QP_ST_MLX: return "MLX";
	case MLX5_QP_ST_QP0: return "QP0";
	case MLX5_QP_ST_QP1: return "QP1";
	case MLX5_QP_ST_RAW_ETHERTYPE: return "RAW_ETHERTYPE";
	case MLX5_QP_ST_RAW_IPV6: return "RAW_IPV6";
	case MLX5_QP_ST_SNIFFER: return "SNIFFER";
	case MLX5_QP_ST_SYNC_UMR: return "SYNC_UMR";
	case MLX5_QP_ST_PTP_1588: return "PTP_1588";
	case MLX5_QP_ST_REG_UMR: return "REG_UMR";
	default: return "Invalid transport type";
	}
}

static inline const char *mlx5_qp_state_str(int state)
{
	switch (state) {
	case MLX5_QP_STATE_RST:
	return "RST";
	case MLX5_QP_STATE_INIT:
	return "INIT";
	case MLX5_QP_STATE_RTR:
	return "RTR";
	case MLX5_QP_STATE_RTS:
	return "RTS";
	case MLX5_QP_STATE_SQER:
	return "SQER";
	case MLX5_QP_STATE_SQD:
	return "SQD";
	case MLX5_QP_STATE_ERR:
	return "ERR";
	case MLX5_QP_STATE_SQ_DRAINING:
	return "SQ_DRAINING";
	case MLX5_QP_STATE_SUSPENDED:
	return "SUSPENDED";
	default: return "Invalid QP state";
	}
}

#endif /* MLX5_QP_H */

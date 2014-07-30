/*
 * Copyright (c) 2013, Mellanox Technologies inc.  All rights reserved.
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

#ifndef MLX5_DEVICE_H
#define MLX5_DEVICE_H

#include <linux/types.h>
#include <rdma/ib_verbs.h>

#if defined(__LITTLE_ENDIAN)
#define MLX5_SET_HOST_ENDIANNESS	0
#elif defined(__BIG_ENDIAN)
#define MLX5_SET_HOST_ENDIANNESS	0x80
#else
#error Host endianness not defined
#endif

enum {
	MLX5_MAX_COMMANDS		= 32,
	MLX5_CMD_DATA_BLOCK_SIZE	= 512,
	MLX5_PCI_CMD_XPORT		= 7,
	MLX5_MKEY_BSF_OCTO_SIZE		= 4,
	MLX5_MAX_PSVS			= 4,
};

enum {
	MLX5_EXTENDED_UD_AV		= 0x80000000,
};

enum {
	MLX5_CQ_STATE_ARMED		= 9,
	MLX5_CQ_STATE_ALWAYS_ARMED	= 0xb,
	MLX5_CQ_STATE_FIRED		= 0xa,
};

enum {
	MLX5_STAT_RATE_OFFSET	= 5,
};

enum {
	MLX5_INLINE_SEG = 0x80000000,
};

enum {
	MLX5_PERM_LOCAL_READ	= 1 << 2,
	MLX5_PERM_LOCAL_WRITE	= 1 << 3,
	MLX5_PERM_REMOTE_READ	= 1 << 4,
	MLX5_PERM_REMOTE_WRITE	= 1 << 5,
	MLX5_PERM_ATOMIC	= 1 << 6,
	MLX5_PERM_UMR_EN	= 1 << 7,
};

enum {
	MLX5_PCIE_CTRL_SMALL_FENCE	= 1 << 0,
	MLX5_PCIE_CTRL_RELAXED_ORDERING	= 1 << 2,
	MLX5_PCIE_CTRL_NO_SNOOP		= 1 << 3,
	MLX5_PCIE_CTRL_TLP_PROCE_EN	= 1 << 6,
	MLX5_PCIE_CTRL_TPH_MASK		= 3 << 4,
};

enum {
	MLX5_ACCESS_MODE_PA	= 0,
	MLX5_ACCESS_MODE_MTT	= 1,
	MLX5_ACCESS_MODE_KLM	= 2
};

enum {
	MLX5_MKEY_REMOTE_INVAL	= 1 << 24,
	MLX5_MKEY_FLAG_SYNC_UMR = 1 << 29,
	MLX5_MKEY_BSF_EN	= 1 << 30,
	MLX5_MKEY_LEN64		= 1 << 31,
};

enum {
	MLX5_EN_RD	= (u64)1,
	MLX5_EN_WR	= (u64)2
};

enum {
	MLX5_BF_REGS_PER_PAGE		= 4,
	MLX5_MAX_UAR_PAGES		= 1 << 8,
	MLX5_NON_FP_BF_REGS_PER_PAGE	= 2,
	MLX5_MAX_UUARS	= MLX5_MAX_UAR_PAGES * MLX5_NON_FP_BF_REGS_PER_PAGE,
};

enum {
	MLX5_MKEY_MASK_LEN		= 1ull << 0,
	MLX5_MKEY_MASK_PAGE_SIZE	= 1ull << 1,
	MLX5_MKEY_MASK_START_ADDR	= 1ull << 6,
	MLX5_MKEY_MASK_PD		= 1ull << 7,
	MLX5_MKEY_MASK_EN_RINVAL	= 1ull << 8,
	MLX5_MKEY_MASK_EN_SIGERR	= 1ull << 9,
	MLX5_MKEY_MASK_BSF_EN		= 1ull << 12,
	MLX5_MKEY_MASK_KEY		= 1ull << 13,
	MLX5_MKEY_MASK_QPN		= 1ull << 14,
	MLX5_MKEY_MASK_LR		= 1ull << 17,
	MLX5_MKEY_MASK_LW		= 1ull << 18,
	MLX5_MKEY_MASK_RR		= 1ull << 19,
	MLX5_MKEY_MASK_RW		= 1ull << 20,
	MLX5_MKEY_MASK_A		= 1ull << 21,
	MLX5_MKEY_MASK_SMALL_FENCE	= 1ull << 23,
	MLX5_MKEY_MASK_FREE		= 1ull << 29,
};

enum mlx5_event {
	MLX5_EVENT_TYPE_COMP		   = 0x0,

	MLX5_EVENT_TYPE_PATH_MIG	   = 0x01,
	MLX5_EVENT_TYPE_COMM_EST	   = 0x02,
	MLX5_EVENT_TYPE_SQ_DRAINED	   = 0x03,
	MLX5_EVENT_TYPE_SRQ_LAST_WQE	   = 0x13,
	MLX5_EVENT_TYPE_SRQ_RQ_LIMIT	   = 0x14,

	MLX5_EVENT_TYPE_CQ_ERROR	   = 0x04,
	MLX5_EVENT_TYPE_WQ_CATAS_ERROR	   = 0x05,
	MLX5_EVENT_TYPE_PATH_MIG_FAILED	   = 0x07,
	MLX5_EVENT_TYPE_WQ_INVAL_REQ_ERROR = 0x10,
	MLX5_EVENT_TYPE_WQ_ACCESS_ERROR	   = 0x11,
	MLX5_EVENT_TYPE_SRQ_CATAS_ERROR	   = 0x12,

	MLX5_EVENT_TYPE_INTERNAL_ERROR	   = 0x08,
	MLX5_EVENT_TYPE_PORT_CHANGE	   = 0x09,
	MLX5_EVENT_TYPE_GPIO_EVENT	   = 0x15,
	MLX5_EVENT_TYPE_REMOTE_CONFIG	   = 0x19,

	MLX5_EVENT_TYPE_DB_BF_CONGESTION   = 0x1a,
	MLX5_EVENT_TYPE_STALL_EVENT	   = 0x1b,

	MLX5_EVENT_TYPE_CMD		   = 0x0a,
	MLX5_EVENT_TYPE_PAGE_REQUEST	   = 0xb,
};

enum {
	MLX5_PORT_CHANGE_SUBTYPE_DOWN		= 1,
	MLX5_PORT_CHANGE_SUBTYPE_ACTIVE		= 4,
	MLX5_PORT_CHANGE_SUBTYPE_INITIALIZED	= 5,
	MLX5_PORT_CHANGE_SUBTYPE_LID		= 6,
	MLX5_PORT_CHANGE_SUBTYPE_PKEY		= 7,
	MLX5_PORT_CHANGE_SUBTYPE_GUID		= 8,
	MLX5_PORT_CHANGE_SUBTYPE_CLIENT_REREG	= 9,
};

enum {
	MLX5_DEV_CAP_FLAG_RC		= 1LL <<  0,
	MLX5_DEV_CAP_FLAG_UC		= 1LL <<  1,
	MLX5_DEV_CAP_FLAG_UD		= 1LL <<  2,
	MLX5_DEV_CAP_FLAG_XRC		= 1LL <<  3,
	MLX5_DEV_CAP_FLAG_SRQ		= 1LL <<  6,
	MLX5_DEV_CAP_FLAG_BAD_PKEY_CNTR	= 1LL <<  8,
	MLX5_DEV_CAP_FLAG_BAD_QKEY_CNTR	= 1LL <<  9,
	MLX5_DEV_CAP_FLAG_APM		= 1LL << 17,
	MLX5_DEV_CAP_FLAG_ATOMIC	= 1LL << 18,
	MLX5_DEV_CAP_FLAG_BLOCK_MCAST	= 1LL << 23,
	MLX5_DEV_CAP_FLAG_ON_DMND_PG	= 1LL << 24,
	MLX5_DEV_CAP_FLAG_CQ_MODER	= 1LL << 29,
	MLX5_DEV_CAP_FLAG_RESIZE_CQ	= 1LL << 30,
	MLX5_DEV_CAP_FLAG_RESIZE_SRQ	= 1LL << 32,
	MLX5_DEV_CAP_FLAG_REMOTE_FENCE	= 1LL << 38,
	MLX5_DEV_CAP_FLAG_TLP_HINTS	= 1LL << 39,
	MLX5_DEV_CAP_FLAG_SIG_HAND_OVER	= 1LL << 40,
	MLX5_DEV_CAP_FLAG_DCT		= 1LL << 41,
	MLX5_DEV_CAP_FLAG_CMDIF_CSUM	= 3LL << 46,
};

enum {
	MLX5_OPCODE_NOP			= 0x00,
	MLX5_OPCODE_SEND_INVAL		= 0x01,
	MLX5_OPCODE_RDMA_WRITE		= 0x08,
	MLX5_OPCODE_RDMA_WRITE_IMM	= 0x09,
	MLX5_OPCODE_SEND		= 0x0a,
	MLX5_OPCODE_SEND_IMM		= 0x0b,
	MLX5_OPCODE_RDMA_READ		= 0x10,
	MLX5_OPCODE_ATOMIC_CS		= 0x11,
	MLX5_OPCODE_ATOMIC_FA		= 0x12,
	MLX5_OPCODE_ATOMIC_MASKED_CS	= 0x14,
	MLX5_OPCODE_ATOMIC_MASKED_FA	= 0x15,
	MLX5_OPCODE_BIND_MW		= 0x18,
	MLX5_OPCODE_CONFIG_CMD		= 0x1f,

	MLX5_RECV_OPCODE_RDMA_WRITE_IMM	= 0x00,
	MLX5_RECV_OPCODE_SEND		= 0x01,
	MLX5_RECV_OPCODE_SEND_IMM	= 0x02,
	MLX5_RECV_OPCODE_SEND_INVAL	= 0x03,

	MLX5_CQE_OPCODE_ERROR		= 0x1e,
	MLX5_CQE_OPCODE_RESIZE		= 0x16,

	MLX5_OPCODE_SET_PSV		= 0x20,
	MLX5_OPCODE_GET_PSV		= 0x21,
	MLX5_OPCODE_CHECK_PSV		= 0x22,
	MLX5_OPCODE_RGET_PSV		= 0x26,
	MLX5_OPCODE_RCHECK_PSV		= 0x27,

	MLX5_OPCODE_UMR			= 0x25,

};

enum {
	MLX5_SET_PORT_RESET_QKEY	= 0,
	MLX5_SET_PORT_GUID0		= 16,
	MLX5_SET_PORT_NODE_GUID		= 17,
	MLX5_SET_PORT_SYS_GUID		= 18,
	MLX5_SET_PORT_GID_TABLE		= 19,
	MLX5_SET_PORT_PKEY_TABLE	= 20,
};

enum {
	MLX5_MAX_PAGE_SHIFT		= 31
};

enum {
	MLX5_ADAPTER_PAGE_SHIFT		= 12,
	MLX5_ADAPTER_PAGE_SIZE		= 1 << MLX5_ADAPTER_PAGE_SHIFT,
};

enum {
	MLX5_CAP_OFF_DCT		= 41,
	MLX5_CAP_OFF_CMDIF_CSUM		= 46,
};

struct mlx5_inbox_hdr {
	__be16		opcode;
	u8		rsvd[4];
	__be16		opmod;
};

struct mlx5_outbox_hdr {
	u8		status;
	u8		rsvd[3];
	__be32		syndrome;
};

struct mlx5_cmd_query_adapter_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	u8			rsvd[8];
};

struct mlx5_cmd_query_adapter_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd0[24];
	u8			intapin;
	u8			rsvd1[13];
	__be16			vsd_vendor_id;
	u8			vsd[208];
	u8			vsd_psid[16];
};

struct mlx5_hca_cap {
	u8	rsvd1[16];
	u8	log_max_srq_sz;
	u8	log_max_qp_sz;
	u8	rsvd2;
	u8	log_max_qp;
	u8	log_max_strq_sz;
	u8	log_max_srqs;
	u8	rsvd4[2];
	u8	rsvd5;
	u8	log_max_cq_sz;
	u8	rsvd6;
	u8	log_max_cq;
	u8	log_max_eq_sz;
	u8	log_max_mkey;
	u8	rsvd7;
	u8	log_max_eq;
	u8	max_indirection;
	u8	log_max_mrw_sz;
	u8	log_max_bsf_list_sz;
	u8	log_max_klm_list_sz;
	u8	rsvd_8_0;
	u8	log_max_ra_req_dc;
	u8	rsvd_8_1;
	u8	log_max_ra_res_dc;
	u8	rsvd9;
	u8	log_max_ra_req_qp;
	u8	rsvd10;
	u8	log_max_ra_res_qp;
	u8	rsvd11[4];
	__be16	max_qp_count;
	__be16	rsvd12;
	u8	rsvd13;
	u8	local_ca_ack_delay;
	u8	rsvd14;
	u8	num_ports;
	u8	log_max_msg;
	u8	rsvd15[3];
	__be16	stat_rate_support;
	u8	rsvd16[2];
	__be64	flags;
	u8	rsvd17;
	u8	uar_sz;
	u8	rsvd18;
	u8	log_pg_sz;
	__be16	bf_log_bf_reg_size;
	u8	rsvd19[4];
	__be16	max_desc_sz_sq;
	u8	rsvd20[2];
	__be16	max_desc_sz_rq;
	u8	rsvd21[2];
	__be16	max_desc_sz_sq_dc;
	__be32	max_qp_mcg;
	u8	rsvd22[3];
	u8	log_max_mcg;
	u8	rsvd23;
	u8	log_max_pd;
	u8	rsvd24;
	u8	log_max_xrcd;
	u8	rsvd25[42];
	__be16  log_uar_page_sz;
	u8	rsvd26[28];
	u8	log_max_atomic_size_qp;
	u8	rsvd27[2];
	u8	log_max_atomic_size_dc;
	u8	rsvd28[76];
};


struct mlx5_cmd_query_hca_cap_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	u8			rsvd[8];
};


struct mlx5_cmd_query_hca_cap_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd0[8];
	struct mlx5_hca_cap     hca_cap;
};


struct mlx5_cmd_set_hca_cap_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	u8			rsvd[8];
	struct mlx5_hca_cap     hca_cap;
};


struct mlx5_cmd_set_hca_cap_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd0[8];
};


struct mlx5_cmd_init_hca_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	u8			rsvd0[2];
	__be16			profile;
	u8			rsvd1[4];
};

struct mlx5_cmd_init_hca_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd[8];
};

struct mlx5_cmd_teardown_hca_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	u8			rsvd0[2];
	__be16			profile;
	u8			rsvd1[4];
};

struct mlx5_cmd_teardown_hca_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd[8];
};

struct mlx5_cmd_layout {
	u8		type;
	u8		rsvd0[3];
	__be32		inlen;
	__be64		in_ptr;
	__be32		in[4];
	__be32		out[4];
	__be64		out_ptr;
	__be32		outlen;
	u8		token;
	u8		sig;
	u8		rsvd1;
	u8		status_own;
};


struct health_buffer {
	__be32		assert_var[5];
	__be32		rsvd0[3];
	__be32		assert_exit_ptr;
	__be32		assert_callra;
	__be32		rsvd1[2];
	__be32		fw_ver;
	__be32		hw_id;
	__be32		rsvd2;
	u8		irisc_index;
	u8		synd;
	__be16		ext_sync;
};

struct mlx5_init_seg {
	__be32			fw_rev;
	__be32			cmdif_rev_fw_sub;
	__be32			rsvd0[2];
	__be32			cmdq_addr_h;
	__be32			cmdq_addr_l_sz;
	__be32			cmd_dbell;
	__be32			rsvd1[121];
	struct health_buffer	health;
	__be32			rsvd2[884];
	__be32			health_counter;
	__be32			rsvd3[1019];
	__be64			ieee1588_clk;
	__be32			ieee1588_clk_type;
	__be32			clr_intx;
};

struct mlx5_eqe_comp {
	__be32	reserved[6];
	__be32	cqn;
};

struct mlx5_eqe_qp_srq {
	__be32	reserved[6];
	__be32	qp_srq_n;
};

struct mlx5_eqe_cq_err {
	__be32	cqn;
	u8	reserved1[7];
	u8	syndrome;
};

struct mlx5_eqe_port_state {
	u8	reserved0[8];
	u8	port;
};

struct mlx5_eqe_gpio {
	__be32	reserved0[2];
	__be64	gpio_event;
};

struct mlx5_eqe_congestion {
	u8	type;
	u8	rsvd0;
	u8	congestion_level;
};

struct mlx5_eqe_stall_vl {
	u8	rsvd0[3];
	u8	port_vl;
};

struct mlx5_eqe_cmd {
	__be32	vector;
	__be32	rsvd[6];
};

struct mlx5_eqe_page_req {
	u8		rsvd0[2];
	__be16		func_id;
	__be32		num_pages;
	__be32		rsvd1[5];
};

union ev_data {
	__be32				raw[7];
	struct mlx5_eqe_cmd		cmd;
	struct mlx5_eqe_comp		comp;
	struct mlx5_eqe_qp_srq		qp_srq;
	struct mlx5_eqe_cq_err		cq_err;
	struct mlx5_eqe_port_state	port;
	struct mlx5_eqe_gpio		gpio;
	struct mlx5_eqe_congestion	cong;
	struct mlx5_eqe_stall_vl	stall_vl;
	struct mlx5_eqe_page_req	req_pages;
} __packed;

struct mlx5_eqe {
	u8		rsvd0;
	u8		type;
	u8		rsvd1;
	u8		sub_type;
	__be32		rsvd2[7];
	union ev_data	data;
	__be16		rsvd3;
	u8		signature;
	u8		owner;
} __packed;

struct mlx5_cmd_prot_block {
	u8		data[MLX5_CMD_DATA_BLOCK_SIZE];
	u8		rsvd0[48];
	__be64		next;
	__be32		block_num;
	u8		rsvd1;
	u8		token;
	u8		ctrl_sig;
	u8		sig;
};

struct mlx5_err_cqe {
	u8	rsvd0[32];
	__be32	srqn;
	u8	rsvd1[18];
	u8	vendor_err_synd;
	u8	syndrome;
	__be32	s_wqe_opcode_qpn;
	__be16	wqe_counter;
	u8	signature;
	u8	op_own;
};

struct mlx5_cqe64 {
	u8		rsvd0[17];
	u8		ml_path;
	u8		rsvd20[4];
	__be16		slid;
	__be32		flags_rqpn;
	u8		rsvd28[4];
	__be32		srqn;
	__be32		imm_inval_pkey;
	u8		rsvd40[4];
	__be32		byte_cnt;
	__be64		timestamp;
	__be32		sop_drop_qpn;
	__be16		wqe_counter;
	u8		signature;
	u8		op_own;
};

struct mlx5_sig_err_cqe {
	u8		rsvd0[16];
	__be32		expected_trans_sig;
	__be32		actual_trans_sig;
	__be32		expected_reftag;
	__be32		actual_reftag;
	__be16		syndrome;
	u8		rsvd22[2];
	__be32		mkey;
	__be64		err_offset;
	u8		rsvd30[8];
	__be32		qpn;
	u8		rsvd38[2];
	u8		signature;
	u8		op_own;
};

struct mlx5_wqe_srq_next_seg {
	u8			rsvd0[2];
	__be16			next_wqe_index;
	u8			signature;
	u8			rsvd1[11];
};

union mlx5_ext_cqe {
	struct ib_grh	grh;
	u8		inl[64];
};

struct mlx5_cqe128 {
	union mlx5_ext_cqe	inl_grh;
	struct mlx5_cqe64	cqe64;
};

struct mlx5_srq_ctx {
	u8			state_log_sz;
	u8			rsvd0[3];
	__be32			flags_xrcd;
	__be32			pgoff_cqn;
	u8			rsvd1[4];
	u8			log_pg_sz;
	u8			rsvd2[7];
	__be32			pd;
	__be16			lwm;
	__be16			wqe_cnt;
	u8			rsvd3[8];
	__be64			db_record;
};

struct mlx5_create_srq_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	__be32			input_srqn;
	u8			rsvd0[4];
	struct mlx5_srq_ctx	ctx;
	u8			rsvd1[208];
	__be64			pas[0];
};

struct mlx5_create_srq_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	__be32			srqn;
	u8			rsvd[4];
};

struct mlx5_destroy_srq_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	__be32			srqn;
	u8			rsvd[4];
};

struct mlx5_destroy_srq_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd[8];
};

struct mlx5_query_srq_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	__be32			srqn;
	u8			rsvd0[4];
};

struct mlx5_query_srq_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd0[8];
	struct mlx5_srq_ctx	ctx;
	u8			rsvd1[32];
	__be64			pas[0];
};

struct mlx5_arm_srq_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	__be32			srqn;
	__be16			rsvd;
	__be16			lwm;
};

struct mlx5_arm_srq_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd[8];
};

struct mlx5_cq_context {
	u8			status;
	u8			cqe_sz_flags;
	u8			st;
	u8			rsvd3;
	u8			rsvd4[6];
	__be16			page_offset;
	__be32			log_sz_usr_page;
	__be16			cq_period;
	__be16			cq_max_count;
	__be16			rsvd20;
	__be16			c_eqn;
	u8			log_pg_sz;
	u8			rsvd25[7];
	__be32			last_notified_index;
	__be32			solicit_producer_index;
	__be32			consumer_counter;
	__be32			producer_counter;
	u8			rsvd48[8];
	__be64			db_record_addr;
};

struct mlx5_create_cq_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	__be32			input_cqn;
	u8			rsvdx[4];
	struct mlx5_cq_context	ctx;
	u8			rsvd6[192];
	__be64			pas[0];
};

struct mlx5_create_cq_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	__be32			cqn;
	u8			rsvd0[4];
};

struct mlx5_destroy_cq_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	__be32			cqn;
	u8			rsvd0[4];
};

struct mlx5_destroy_cq_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd0[8];
};

struct mlx5_query_cq_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	__be32			cqn;
	u8			rsvd0[4];
};

struct mlx5_query_cq_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd0[8];
	struct mlx5_cq_context	ctx;
	u8			rsvd6[16];
	__be64			pas[0];
};

struct mlx5_modify_cq_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	__be32			cqn;
	__be32			field_select;
	struct mlx5_cq_context	ctx;
	u8			rsvd[192];
	__be64			pas[0];
};

struct mlx5_modify_cq_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd[8];
};

struct mlx5_enable_hca_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	u8			rsvd[8];
};

struct mlx5_enable_hca_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd[8];
};

struct mlx5_disable_hca_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	u8			rsvd[8];
};

struct mlx5_disable_hca_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd[8];
};

struct mlx5_eq_context {
	u8			status;
	u8			ec_oi;
	u8			st;
	u8			rsvd2[7];
	__be16			page_pffset;
	__be32			log_sz_usr_page;
	u8			rsvd3[7];
	u8			intr;
	u8			log_page_size;
	u8			rsvd4[15];
	__be32			consumer_counter;
	__be32			produser_counter;
	u8			rsvd5[16];
};

struct mlx5_create_eq_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	u8			rsvd0[3];
	u8			input_eqn;
	u8			rsvd1[4];
	struct mlx5_eq_context	ctx;
	u8			rsvd2[8];
	__be64			events_mask;
	u8			rsvd3[176];
	__be64			pas[0];
};

struct mlx5_create_eq_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd0[3];
	u8			eq_number;
	u8			rsvd1[4];
};

struct mlx5_destroy_eq_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	u8			rsvd0[3];
	u8			eqn;
	u8			rsvd1[4];
};

struct mlx5_destroy_eq_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd[8];
};

struct mlx5_map_eq_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	__be64			mask;
	u8			mu;
	u8			rsvd0[2];
	u8			eqn;
	u8			rsvd1[24];
};

struct mlx5_map_eq_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd[8];
};

struct mlx5_query_eq_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	u8			rsvd0[3];
	u8			eqn;
	u8			rsvd1[4];
};

struct mlx5_query_eq_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd[8];
	struct mlx5_eq_context	ctx;
};

struct mlx5_mkey_seg {
	/* This is a two bit field occupying bits 31-30.
	 * bit 31 is always 0,
	 * bit 30 is zero for regular MRs and 1 (e.g free) for UMRs that do not have tanslation
	 */
	u8		status;
	u8		pcie_control;
	u8		flags;
	u8		version;
	__be32		qpn_mkey7_0;
	u8		rsvd1[4];
	__be32		flags_pd;
	__be64		start_addr;
	__be64		len;
	__be32		bsfs_octo_size;
	u8		rsvd2[16];
	__be32		xlt_oct_size;
	u8		rsvd3[3];
	u8		log2_page_size;
	u8		rsvd4[4];
};

struct mlx5_query_special_ctxs_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	u8			rsvd[8];
};

struct mlx5_query_special_ctxs_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	__be32			dump_fill_mkey;
	__be32			reserved_lkey;
};

struct mlx5_create_mkey_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	__be32			input_mkey_index;
	u8			rsvd0[4];
	struct mlx5_mkey_seg	seg;
	u8			rsvd1[16];
	__be32			xlat_oct_act_size;
	__be32			rsvd2;
	u8			rsvd3[168];
	__be64			pas[0];
};

struct mlx5_create_mkey_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	__be32			mkey;
	u8			rsvd[4];
};

struct mlx5_destroy_mkey_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	__be32			mkey;
	u8			rsvd[4];
};

struct mlx5_destroy_mkey_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd[8];
};

struct mlx5_query_mkey_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	__be32			mkey;
};

struct mlx5_query_mkey_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	__be64			pas[0];
};

struct mlx5_modify_mkey_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	__be32			mkey;
	__be64			pas[0];
};

struct mlx5_modify_mkey_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd[8];
};

struct mlx5_dump_mkey_mbox_in {
	struct mlx5_inbox_hdr	hdr;
};

struct mlx5_dump_mkey_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	__be32			mkey;
};

struct mlx5_mad_ifc_mbox_in {
	struct mlx5_inbox_hdr	hdr;
	__be16			remote_lid;
	u8			rsvd0;
	u8			port;
	u8			rsvd1[4];
	u8			data[256];
};

struct mlx5_mad_ifc_mbox_out {
	struct mlx5_outbox_hdr	hdr;
	u8			rsvd[8];
	u8			data[256];
};

struct mlx5_access_reg_mbox_in {
	struct mlx5_inbox_hdr		hdr;
	u8				rsvd0[2];
	__be16				register_id;
	__be32				arg;
	__be32				data[0];
};

struct mlx5_access_reg_mbox_out {
	struct mlx5_outbox_hdr		hdr;
	u8				rsvd[8];
	__be32				data[0];
};

#define MLX5_ATTR_EXTENDED_PORT_INFO	cpu_to_be16(0xff90)

enum {
	MLX_EXT_PORT_CAP_FLAG_EXTENDED_PORT_INFO	= 1 <<  0
};

struct mlx5_allocate_psv_in {
	struct mlx5_inbox_hdr   hdr;
	__be32			npsv_pd;
	__be32			rsvd_psv0;
};

struct mlx5_allocate_psv_out {
	struct mlx5_outbox_hdr  hdr;
	u8			rsvd[8];
	__be32			psv_idx[4];
};

struct mlx5_destroy_psv_in {
	struct mlx5_inbox_hdr	hdr;
	__be32                  psv_number;
	u8                      rsvd[4];
};

struct mlx5_destroy_psv_out {
	struct mlx5_outbox_hdr  hdr;
	u8                      rsvd[8];
};

#endif /* MLX5_DEVICE_H */

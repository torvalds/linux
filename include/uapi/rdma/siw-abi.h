/* SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-note) or BSD-3-Clause */

/* Authors: Bernard Metzler <bmt@zurich.ibm.com> */
/* Copyright (c) 2008-2019, IBM Corporation */

#ifndef _SIW_USER_H
#define _SIW_USER_H

#include <linux/types.h>

#define SIW_NODE_DESC_COMMON "Software iWARP stack"
#define SIW_ABI_VERSION 1
#define SIW_MAX_SGE 6
#define SIW_UOBJ_MAX_KEY 0x08FFFF
#define SIW_INVAL_UOBJ_KEY (SIW_UOBJ_MAX_KEY + 1)

struct siw_uresp_create_cq {
	__u32 cq_id;
	__u32 num_cqe;
	__aligned_u64 cq_key;
};

struct siw_uresp_create_qp {
	__u32 qp_id;
	__u32 num_sqe;
	__u32 num_rqe;
	__u32 pad;
	__aligned_u64 sq_key;
	__aligned_u64 rq_key;
};

struct siw_ureq_reg_mr {
	__u8 stag_key;
	__u8 reserved[3];
	__u32 pad;
};

struct siw_uresp_reg_mr {
	__u32 stag;
	__u32 pad;
};

struct siw_uresp_create_srq {
	__u32 num_rqe;
	__u32 pad;
	__aligned_u64 srq_key;
};

struct siw_uresp_alloc_ctx {
	__u32 dev_id;
	__u32 pad;
};

enum siw_opcode {
	SIW_OP_WRITE,
	SIW_OP_READ,
	SIW_OP_READ_LOCAL_INV,
	SIW_OP_SEND,
	SIW_OP_SEND_WITH_IMM,
	SIW_OP_SEND_REMOTE_INV,

	/* Unsupported */
	SIW_OP_FETCH_AND_ADD,
	SIW_OP_COMP_AND_SWAP,

	SIW_OP_RECEIVE,
	/* provider internal SQE */
	SIW_OP_READ_RESPONSE,
	/*
	 * below opcodes valid for
	 * in-kernel clients only
	 */
	SIW_OP_INVAL_STAG,
	SIW_OP_REG_MR,
	SIW_NUM_OPCODES
};

/* Keep it same as ibv_sge to allow for memcpy */
struct siw_sge {
	__aligned_u64 laddr;
	__u32 length;
	__u32 lkey;
};

/*
 * Inline data are kept within the work request itself occupying
 * the space of sge[1] .. sge[n]. Therefore, inline data cannot be
 * supported if SIW_MAX_SGE is below 2 elements.
 */
#define SIW_MAX_INLINE (sizeof(struct siw_sge) * (SIW_MAX_SGE - 1))

#if SIW_MAX_SGE < 2
#error "SIW_MAX_SGE must be at least 2"
#endif

enum siw_wqe_flags {
	SIW_WQE_VALID = 1,
	SIW_WQE_INLINE = (1 << 1),
	SIW_WQE_SIGNALLED = (1 << 2),
	SIW_WQE_SOLICITED = (1 << 3),
	SIW_WQE_READ_FENCE = (1 << 4),
	SIW_WQE_REM_INVAL = (1 << 5),
	SIW_WQE_COMPLETED = (1 << 6)
};

/* Send Queue Element */
struct siw_sqe {
	__aligned_u64 id;
	__u16 flags;
	__u8 num_sge;
	/* Contains enum siw_opcode values */
	__u8 opcode;
	__u32 rkey;
	union {
		__aligned_u64 raddr;
		__aligned_u64 base_mr;
	};
	union {
		struct siw_sge sge[SIW_MAX_SGE];
		__aligned_u64 access;
	};
};

/* Receive Queue Element */
struct siw_rqe {
	__aligned_u64 id;
	__u16 flags;
	__u8 num_sge;
	/*
	 * only used by kernel driver,
	 * ignored if set by user
	 */
	__u8 opcode;
	__u32 unused;
	struct siw_sge sge[SIW_MAX_SGE];
};

enum siw_notify_flags {
	SIW_NOTIFY_NOT = (0),
	SIW_NOTIFY_SOLICITED = (1 << 0),
	SIW_NOTIFY_NEXT_COMPLETION = (1 << 1),
	SIW_NOTIFY_MISSED_EVENTS = (1 << 2),
	SIW_NOTIFY_ALL = SIW_NOTIFY_SOLICITED | SIW_NOTIFY_NEXT_COMPLETION |
			 SIW_NOTIFY_MISSED_EVENTS
};

enum siw_wc_status {
	SIW_WC_SUCCESS,
	SIW_WC_LOC_LEN_ERR,
	SIW_WC_LOC_PROT_ERR,
	SIW_WC_LOC_QP_OP_ERR,
	SIW_WC_WR_FLUSH_ERR,
	SIW_WC_BAD_RESP_ERR,
	SIW_WC_LOC_ACCESS_ERR,
	SIW_WC_REM_ACCESS_ERR,
	SIW_WC_REM_INV_REQ_ERR,
	SIW_WC_GENERAL_ERR,
	SIW_NUM_WC_STATUS
};

struct siw_cqe {
	__aligned_u64 id;
	__u8 flags;
	__u8 opcode;
	__u16 status;
	__u32 bytes;
	union {
		__aligned_u64 imm_data;
		__u32 inval_stag;
	};
	/* QP number or QP pointer */
	union {
		struct ib_qp *base_qp;
		__aligned_u64 qp_id;
	};
};

/*
 * Shared structure between user and kernel
 * to control CQ arming.
 */
struct siw_cq_ctrl {
	__u32 flags;
	__u32 pad;
};
#endif

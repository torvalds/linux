/*
 * Copyright (c) 2018-2019 Cavium, Inc.
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
 *
 */

#ifndef __QLNXR_ROCE_H__
#define __QLNXR_ROCE_H__ 


/*
 * roce completion notification queue element
 */
struct roce_cnqe {
	struct regpair cq_handle;
};


struct roce_cqe_responder {
	struct regpair srq_wr_id;
	struct regpair qp_handle;
	__le32 imm_data_or_inv_r_Key;
	__le32 length;
	__le32 reserved0;
	__le16 rq_cons;
	u8 flags;
#define ROCE_CQE_RESPONDER_TOGGLE_BIT_MASK  0x1
#define ROCE_CQE_RESPONDER_TOGGLE_BIT_SHIFT 0
#define ROCE_CQE_RESPONDER_TYPE_MASK        0x3
#define ROCE_CQE_RESPONDER_TYPE_SHIFT       1
#define ROCE_CQE_RESPONDER_INV_FLG_MASK     0x1
#define ROCE_CQE_RESPONDER_INV_FLG_SHIFT    3
#define ROCE_CQE_RESPONDER_IMM_FLG_MASK     0x1
#define ROCE_CQE_RESPONDER_IMM_FLG_SHIFT    4
#define ROCE_CQE_RESPONDER_RDMA_FLG_MASK    0x1
#define ROCE_CQE_RESPONDER_RDMA_FLG_SHIFT   5
#define ROCE_CQE_RESPONDER_RESERVED2_MASK   0x3
#define ROCE_CQE_RESPONDER_RESERVED2_SHIFT  6
	u8 status;
};

struct roce_cqe_requester {
	__le16 sq_cons;
	__le16 reserved0;
	__le32 reserved1;
	struct regpair qp_handle;
	struct regpair reserved2;
	__le32 reserved3;
	__le16 reserved4;
	u8 flags;
#define ROCE_CQE_REQUESTER_TOGGLE_BIT_MASK  0x1
#define ROCE_CQE_REQUESTER_TOGGLE_BIT_SHIFT 0
#define ROCE_CQE_REQUESTER_TYPE_MASK        0x3
#define ROCE_CQE_REQUESTER_TYPE_SHIFT       1
#define ROCE_CQE_REQUESTER_RESERVED5_MASK   0x1F
#define ROCE_CQE_REQUESTER_RESERVED5_SHIFT  3
	u8 status;
};

struct roce_cqe_common {
	struct regpair reserved0;
	struct regpair qp_handle;
	__le16 reserved1[7];
	u8 flags;
#define ROCE_CQE_COMMON_TOGGLE_BIT_MASK  0x1
#define ROCE_CQE_COMMON_TOGGLE_BIT_SHIFT 0
#define ROCE_CQE_COMMON_TYPE_MASK        0x3
#define ROCE_CQE_COMMON_TYPE_SHIFT       1
#define ROCE_CQE_COMMON_RESERVED2_MASK   0x1F
#define ROCE_CQE_COMMON_RESERVED2_SHIFT  3
	u8 status;
};

/*
 * roce completion queue element
 */
union roce_cqe {
	struct roce_cqe_responder resp;
	struct roce_cqe_requester req;
	struct roce_cqe_common cmn;
};




/*
 * CQE requester status enumeration
 */
enum roce_cqe_requester_status_enum {
	ROCE_CQE_REQ_STS_OK,
	ROCE_CQE_REQ_STS_BAD_RESPONSE_ERR,
	ROCE_CQE_REQ_STS_LOCAL_LENGTH_ERR,
	ROCE_CQE_REQ_STS_LOCAL_QP_OPERATION_ERR,
	ROCE_CQE_REQ_STS_LOCAL_PROTECTION_ERR,
	ROCE_CQE_REQ_STS_MEMORY_MGT_OPERATION_ERR,
	ROCE_CQE_REQ_STS_REMOTE_INVALID_REQUEST_ERR,
	ROCE_CQE_REQ_STS_REMOTE_ACCESS_ERR,
	ROCE_CQE_REQ_STS_REMOTE_OPERATION_ERR,
	ROCE_CQE_REQ_STS_RNR_NAK_RETRY_CNT_ERR,
	ROCE_CQE_REQ_STS_TRANSPORT_RETRY_CNT_ERR,
	ROCE_CQE_REQ_STS_WORK_REQUEST_FLUSHED_ERR,
	MAX_ROCE_CQE_REQUESTER_STATUS_ENUM
};



/*
 * CQE responder status enumeration
 */
enum roce_cqe_responder_status_enum {
	ROCE_CQE_RESP_STS_OK,
	ROCE_CQE_RESP_STS_LOCAL_ACCESS_ERR,
	ROCE_CQE_RESP_STS_LOCAL_LENGTH_ERR,
	ROCE_CQE_RESP_STS_LOCAL_QP_OPERATION_ERR,
	ROCE_CQE_RESP_STS_LOCAL_PROTECTION_ERR,
	ROCE_CQE_RESP_STS_MEMORY_MGT_OPERATION_ERR,
	ROCE_CQE_RESP_STS_REMOTE_INVALID_REQUEST_ERR,
	ROCE_CQE_RESP_STS_WORK_REQUEST_FLUSHED_ERR,
	MAX_ROCE_CQE_RESPONDER_STATUS_ENUM
};


/*
 * CQE type enumeration
 */
enum roce_cqe_type {
	ROCE_CQE_TYPE_REQUESTER,
	ROCE_CQE_TYPE_RESPONDER_RQ,
	ROCE_CQE_TYPE_RESPONDER_SRQ,
	ROCE_CQE_TYPE_INVALID,
	MAX_ROCE_CQE_TYPE
};


/*
 * memory window type enumeration
 */
enum roce_mw_type {
	ROCE_MW_TYPE_1,
	ROCE_MW_TYPE_2A,
	MAX_ROCE_MW_TYPE
};


struct roce_rq_sge {
	struct regpair addr;
	__le32 length;
	__le32 flags;
#define ROCE_RQ_SGE_L_KEY_MASK      0x3FFFFFF
#define ROCE_RQ_SGE_L_KEY_SHIFT     0
#define ROCE_RQ_SGE_NUM_SGES_MASK   0x7
#define ROCE_RQ_SGE_NUM_SGES_SHIFT  26
#define ROCE_RQ_SGE_RESERVED0_MASK  0x7
#define ROCE_RQ_SGE_RESERVED0_SHIFT 29
};


struct roce_sq_atomic_wqe {
	struct regpair remote_va;
	__le32 xrc_srq;
	u8 req_type;
	u8 flags;
#define ROCE_SQ_ATOMIC_WQE_COMP_FLG_MASK       0x1
#define ROCE_SQ_ATOMIC_WQE_COMP_FLG_SHIFT      0
#define ROCE_SQ_ATOMIC_WQE_RD_FENCE_FLG_MASK   0x1
#define ROCE_SQ_ATOMIC_WQE_RD_FENCE_FLG_SHIFT  1
#define ROCE_SQ_ATOMIC_WQE_INV_FENCE_FLG_MASK  0x1
#define ROCE_SQ_ATOMIC_WQE_INV_FENCE_FLG_SHIFT 2
#define ROCE_SQ_ATOMIC_WQE_SE_FLG_MASK         0x1
#define ROCE_SQ_ATOMIC_WQE_SE_FLG_SHIFT        3
#define ROCE_SQ_ATOMIC_WQE_INLINE_FLG_MASK     0x1
#define ROCE_SQ_ATOMIC_WQE_INLINE_FLG_SHIFT    4
#define ROCE_SQ_ATOMIC_WQE_RESERVED0_MASK      0x7
#define ROCE_SQ_ATOMIC_WQE_RESERVED0_SHIFT     5
	u8 reserved1;
	u8 prev_wqe_size;
	struct regpair swap_data;
	__le32 r_key;
	__le32 reserved2;
	struct regpair cmp_data;
	struct regpair reserved3;
};


/*
 * First element (16 bytes) of atomic wqe
 */
struct roce_sq_atomic_wqe_1st {
	struct regpair remote_va;
	__le32 xrc_srq;
	u8 req_type;
	u8 flags;
#define ROCE_SQ_ATOMIC_WQE_1ST_COMP_FLG_MASK       0x1
#define ROCE_SQ_ATOMIC_WQE_1ST_COMP_FLG_SHIFT      0
#define ROCE_SQ_ATOMIC_WQE_1ST_RD_FENCE_FLG_MASK   0x1
#define ROCE_SQ_ATOMIC_WQE_1ST_RD_FENCE_FLG_SHIFT  1
#define ROCE_SQ_ATOMIC_WQE_1ST_INV_FENCE_FLG_MASK  0x1
#define ROCE_SQ_ATOMIC_WQE_1ST_INV_FENCE_FLG_SHIFT 2
#define ROCE_SQ_ATOMIC_WQE_1ST_SE_FLG_MASK         0x1
#define ROCE_SQ_ATOMIC_WQE_1ST_SE_FLG_SHIFT        3
#define ROCE_SQ_ATOMIC_WQE_1ST_INLINE_FLG_MASK     0x1
#define ROCE_SQ_ATOMIC_WQE_1ST_INLINE_FLG_SHIFT    4
#define ROCE_SQ_ATOMIC_WQE_1ST_RESERVED0_MASK      0x7
#define ROCE_SQ_ATOMIC_WQE_1ST_RESERVED0_SHIFT     5
	u8 reserved1;
	u8 prev_wqe_size;
};


/*
 * Second element (16 bytes) of atomic wqe
 */
struct roce_sq_atomic_wqe_2nd {
	struct regpair swap_data;
	__le32 r_key;
	__le32 reserved2;
};


/*
 * Third element (16 bytes) of atomic wqe
 */
struct roce_sq_atomic_wqe_3rd {
	struct regpair cmp_data;
	struct regpair reserved3;
};


struct roce_sq_bind_wqe {
	struct regpair addr;
	__le32 l_key;
	u8 req_type;
	u8 flags;
#define ROCE_SQ_BIND_WQE_COMP_FLG_MASK       0x1
#define ROCE_SQ_BIND_WQE_COMP_FLG_SHIFT      0
#define ROCE_SQ_BIND_WQE_RD_FENCE_FLG_MASK   0x1
#define ROCE_SQ_BIND_WQE_RD_FENCE_FLG_SHIFT  1
#define ROCE_SQ_BIND_WQE_INV_FENCE_FLG_MASK  0x1
#define ROCE_SQ_BIND_WQE_INV_FENCE_FLG_SHIFT 2
#define ROCE_SQ_BIND_WQE_SE_FLG_MASK         0x1
#define ROCE_SQ_BIND_WQE_SE_FLG_SHIFT        3
#define ROCE_SQ_BIND_WQE_INLINE_FLG_MASK     0x1
#define ROCE_SQ_BIND_WQE_INLINE_FLG_SHIFT    4
#define ROCE_SQ_BIND_WQE_RESERVED0_MASK      0x7
#define ROCE_SQ_BIND_WQE_RESERVED0_SHIFT     5
	u8 access_ctrl;
#define ROCE_SQ_BIND_WQE_REMOTE_READ_MASK    0x1
#define ROCE_SQ_BIND_WQE_REMOTE_READ_SHIFT   0
#define ROCE_SQ_BIND_WQE_REMOTE_WRITE_MASK   0x1
#define ROCE_SQ_BIND_WQE_REMOTE_WRITE_SHIFT  1
#define ROCE_SQ_BIND_WQE_ENABLE_ATOMIC_MASK  0x1
#define ROCE_SQ_BIND_WQE_ENABLE_ATOMIC_SHIFT 2
#define ROCE_SQ_BIND_WQE_LOCAL_READ_MASK     0x1
#define ROCE_SQ_BIND_WQE_LOCAL_READ_SHIFT    3
#define ROCE_SQ_BIND_WQE_LOCAL_WRITE_MASK    0x1
#define ROCE_SQ_BIND_WQE_LOCAL_WRITE_SHIFT   4
#define ROCE_SQ_BIND_WQE_RESERVED1_MASK      0x7
#define ROCE_SQ_BIND_WQE_RESERVED1_SHIFT     5
	u8 prev_wqe_size;
	u8 bind_ctrl;
#define ROCE_SQ_BIND_WQE_ZERO_BASED_MASK     0x1
#define ROCE_SQ_BIND_WQE_ZERO_BASED_SHIFT    0
#define ROCE_SQ_BIND_WQE_MW_TYPE_MASK        0x1
#define ROCE_SQ_BIND_WQE_MW_TYPE_SHIFT       1
#define ROCE_SQ_BIND_WQE_RESERVED2_MASK      0x3F
#define ROCE_SQ_BIND_WQE_RESERVED2_SHIFT     2
	u8 reserved3[2];
	u8 length_hi;
	__le32 length_lo;
	__le32 parent_l_key;
	__le32 reserved6;
};


/*
 * First element (16 bytes) of bind wqe
 */
struct roce_sq_bind_wqe_1st {
	struct regpair addr;
	__le32 l_key;
	u8 req_type;
	u8 flags;
#define ROCE_SQ_BIND_WQE_1ST_COMP_FLG_MASK       0x1
#define ROCE_SQ_BIND_WQE_1ST_COMP_FLG_SHIFT      0
#define ROCE_SQ_BIND_WQE_1ST_RD_FENCE_FLG_MASK   0x1
#define ROCE_SQ_BIND_WQE_1ST_RD_FENCE_FLG_SHIFT  1
#define ROCE_SQ_BIND_WQE_1ST_INV_FENCE_FLG_MASK  0x1
#define ROCE_SQ_BIND_WQE_1ST_INV_FENCE_FLG_SHIFT 2
#define ROCE_SQ_BIND_WQE_1ST_SE_FLG_MASK         0x1
#define ROCE_SQ_BIND_WQE_1ST_SE_FLG_SHIFT        3
#define ROCE_SQ_BIND_WQE_1ST_INLINE_FLG_MASK     0x1
#define ROCE_SQ_BIND_WQE_1ST_INLINE_FLG_SHIFT    4
#define ROCE_SQ_BIND_WQE_1ST_RESERVED0_MASK      0x7
#define ROCE_SQ_BIND_WQE_1ST_RESERVED0_SHIFT     5
	u8 access_ctrl;
#define ROCE_SQ_BIND_WQE_1ST_REMOTE_READ_MASK    0x1
#define ROCE_SQ_BIND_WQE_1ST_REMOTE_READ_SHIFT   0
#define ROCE_SQ_BIND_WQE_1ST_REMOTE_WRITE_MASK   0x1
#define ROCE_SQ_BIND_WQE_1ST_REMOTE_WRITE_SHIFT  1
#define ROCE_SQ_BIND_WQE_1ST_ENABLE_ATOMIC_MASK  0x1
#define ROCE_SQ_BIND_WQE_1ST_ENABLE_ATOMIC_SHIFT 2
#define ROCE_SQ_BIND_WQE_1ST_LOCAL_READ_MASK     0x1
#define ROCE_SQ_BIND_WQE_1ST_LOCAL_READ_SHIFT    3
#define ROCE_SQ_BIND_WQE_1ST_LOCAL_WRITE_MASK    0x1
#define ROCE_SQ_BIND_WQE_1ST_LOCAL_WRITE_SHIFT   4
#define ROCE_SQ_BIND_WQE_1ST_RESERVED1_MASK      0x7
#define ROCE_SQ_BIND_WQE_1ST_RESERVED1_SHIFT     5
	u8 prev_wqe_size;
};


/*
 * Second element (16 bytes) of bind wqe
 */
struct roce_sq_bind_wqe_2nd {
	u8 bind_ctrl;
#define ROCE_SQ_BIND_WQE_2ND_ZERO_BASED_MASK  0x1
#define ROCE_SQ_BIND_WQE_2ND_ZERO_BASED_SHIFT 0
#define ROCE_SQ_BIND_WQE_2ND_MW_TYPE_MASK     0x1
#define ROCE_SQ_BIND_WQE_2ND_MW_TYPE_SHIFT    1
#define ROCE_SQ_BIND_WQE_2ND_RESERVED2_MASK   0x3F
#define ROCE_SQ_BIND_WQE_2ND_RESERVED2_SHIFT  2
	u8 reserved3[2];
	u8 length_hi;
	__le32 length_lo;
	__le32 parent_l_key;
	__le32 reserved6;
};


/*
 * Structure with only the SQ WQE common fields. Size is of one SQ element (16B)
 */
struct roce_sq_common_wqe {
	__le32 reserved1[3];
	u8 req_type;
	u8 flags;
#define ROCE_SQ_COMMON_WQE_COMP_FLG_MASK       0x1
#define ROCE_SQ_COMMON_WQE_COMP_FLG_SHIFT      0
#define ROCE_SQ_COMMON_WQE_RD_FENCE_FLG_MASK   0x1
#define ROCE_SQ_COMMON_WQE_RD_FENCE_FLG_SHIFT  1
#define ROCE_SQ_COMMON_WQE_INV_FENCE_FLG_MASK  0x1
#define ROCE_SQ_COMMON_WQE_INV_FENCE_FLG_SHIFT 2
#define ROCE_SQ_COMMON_WQE_SE_FLG_MASK         0x1
#define ROCE_SQ_COMMON_WQE_SE_FLG_SHIFT        3
#define ROCE_SQ_COMMON_WQE_INLINE_FLG_MASK     0x1
#define ROCE_SQ_COMMON_WQE_INLINE_FLG_SHIFT    4
#define ROCE_SQ_COMMON_WQE_RESERVED0_MASK      0x7
#define ROCE_SQ_COMMON_WQE_RESERVED0_SHIFT     5
	u8 reserved2;
	u8 prev_wqe_size;
};


struct roce_sq_fmr_wqe {
	struct regpair addr;
	__le32 l_key;
	u8 req_type;
	u8 flags;
#define ROCE_SQ_FMR_WQE_COMP_FLG_MASK       0x1
#define ROCE_SQ_FMR_WQE_COMP_FLG_SHIFT      0
#define ROCE_SQ_FMR_WQE_RD_FENCE_FLG_MASK   0x1
#define ROCE_SQ_FMR_WQE_RD_FENCE_FLG_SHIFT  1
#define ROCE_SQ_FMR_WQE_INV_FENCE_FLG_MASK  0x1
#define ROCE_SQ_FMR_WQE_INV_FENCE_FLG_SHIFT 2
#define ROCE_SQ_FMR_WQE_SE_FLG_MASK         0x1
#define ROCE_SQ_FMR_WQE_SE_FLG_SHIFT        3
#define ROCE_SQ_FMR_WQE_INLINE_FLG_MASK     0x1
#define ROCE_SQ_FMR_WQE_INLINE_FLG_SHIFT    4
#define ROCE_SQ_FMR_WQE_RESERVED0_MASK      0x7
#define ROCE_SQ_FMR_WQE_RESERVED0_SHIFT     5
	u8 access_ctrl;
#define ROCE_SQ_FMR_WQE_REMOTE_READ_MASK    0x1
#define ROCE_SQ_FMR_WQE_REMOTE_READ_SHIFT   0
#define ROCE_SQ_FMR_WQE_REMOTE_WRITE_MASK   0x1
#define ROCE_SQ_FMR_WQE_REMOTE_WRITE_SHIFT  1
#define ROCE_SQ_FMR_WQE_ENABLE_ATOMIC_MASK  0x1
#define ROCE_SQ_FMR_WQE_ENABLE_ATOMIC_SHIFT 2
#define ROCE_SQ_FMR_WQE_LOCAL_READ_MASK     0x1
#define ROCE_SQ_FMR_WQE_LOCAL_READ_SHIFT    3
#define ROCE_SQ_FMR_WQE_LOCAL_WRITE_MASK    0x1
#define ROCE_SQ_FMR_WQE_LOCAL_WRITE_SHIFT   4
#define ROCE_SQ_FMR_WQE_RESERVED1_MASK      0x7
#define ROCE_SQ_FMR_WQE_RESERVED1_SHIFT     5
	u8 prev_wqe_size;
	u8 fmr_ctrl;
#define ROCE_SQ_FMR_WQE_PAGE_SIZE_LOG_MASK  0x1F
#define ROCE_SQ_FMR_WQE_PAGE_SIZE_LOG_SHIFT 0
#define ROCE_SQ_FMR_WQE_ZERO_BASED_MASK     0x1
#define ROCE_SQ_FMR_WQE_ZERO_BASED_SHIFT    5
#define ROCE_SQ_FMR_WQE_BIND_EN_MASK        0x1
#define ROCE_SQ_FMR_WQE_BIND_EN_SHIFT       6
#define ROCE_SQ_FMR_WQE_RESERVED2_MASK      0x1
#define ROCE_SQ_FMR_WQE_RESERVED2_SHIFT     7
	u8 reserved3[2];
	u8 length_hi;
	__le32 length_lo;
	struct regpair pbl_addr;
};


/*
 * First element (16 bytes) of fmr wqe
 */
struct roce_sq_fmr_wqe_1st {
	struct regpair addr;
	__le32 l_key;
	u8 req_type;
	u8 flags;
#define ROCE_SQ_FMR_WQE_1ST_COMP_FLG_MASK       0x1
#define ROCE_SQ_FMR_WQE_1ST_COMP_FLG_SHIFT      0
#define ROCE_SQ_FMR_WQE_1ST_RD_FENCE_FLG_MASK   0x1
#define ROCE_SQ_FMR_WQE_1ST_RD_FENCE_FLG_SHIFT  1
#define ROCE_SQ_FMR_WQE_1ST_INV_FENCE_FLG_MASK  0x1
#define ROCE_SQ_FMR_WQE_1ST_INV_FENCE_FLG_SHIFT 2
#define ROCE_SQ_FMR_WQE_1ST_SE_FLG_MASK         0x1
#define ROCE_SQ_FMR_WQE_1ST_SE_FLG_SHIFT        3
#define ROCE_SQ_FMR_WQE_1ST_INLINE_FLG_MASK     0x1
#define ROCE_SQ_FMR_WQE_1ST_INLINE_FLG_SHIFT    4
#define ROCE_SQ_FMR_WQE_1ST_RESERVED0_MASK      0x7
#define ROCE_SQ_FMR_WQE_1ST_RESERVED0_SHIFT     5
	u8 access_ctrl;
#define ROCE_SQ_FMR_WQE_1ST_REMOTE_READ_MASK    0x1
#define ROCE_SQ_FMR_WQE_1ST_REMOTE_READ_SHIFT   0
#define ROCE_SQ_FMR_WQE_1ST_REMOTE_WRITE_MASK   0x1
#define ROCE_SQ_FMR_WQE_1ST_REMOTE_WRITE_SHIFT  1
#define ROCE_SQ_FMR_WQE_1ST_ENABLE_ATOMIC_MASK  0x1
#define ROCE_SQ_FMR_WQE_1ST_ENABLE_ATOMIC_SHIFT 2
#define ROCE_SQ_FMR_WQE_1ST_LOCAL_READ_MASK     0x1
#define ROCE_SQ_FMR_WQE_1ST_LOCAL_READ_SHIFT    3
#define ROCE_SQ_FMR_WQE_1ST_LOCAL_WRITE_MASK    0x1
#define ROCE_SQ_FMR_WQE_1ST_LOCAL_WRITE_SHIFT   4
#define ROCE_SQ_FMR_WQE_1ST_RESERVED1_MASK      0x7
#define ROCE_SQ_FMR_WQE_1ST_RESERVED1_SHIFT     5
	u8 prev_wqe_size;
};


/*
 * Second element (16 bytes) of fmr wqe
 */
struct roce_sq_fmr_wqe_2nd {
	u8 fmr_ctrl;
#define ROCE_SQ_FMR_WQE_2ND_PAGE_SIZE_LOG_MASK  0x1F
#define ROCE_SQ_FMR_WQE_2ND_PAGE_SIZE_LOG_SHIFT 0
#define ROCE_SQ_FMR_WQE_2ND_ZERO_BASED_MASK     0x1
#define ROCE_SQ_FMR_WQE_2ND_ZERO_BASED_SHIFT    5
#define ROCE_SQ_FMR_WQE_2ND_BIND_EN_MASK        0x1
#define ROCE_SQ_FMR_WQE_2ND_BIND_EN_SHIFT       6
#define ROCE_SQ_FMR_WQE_2ND_RESERVED2_MASK      0x1
#define ROCE_SQ_FMR_WQE_2ND_RESERVED2_SHIFT     7
	u8 reserved3[2];
	u8 length_hi;
	__le32 length_lo;
	struct regpair pbl_addr;
};


struct roce_sq_local_inv_wqe {
	struct regpair reserved;
	__le32 inv_l_key;
	u8 req_type;
	u8 flags;
#define ROCE_SQ_LOCAL_INV_WQE_COMP_FLG_MASK       0x1
#define ROCE_SQ_LOCAL_INV_WQE_COMP_FLG_SHIFT      0
#define ROCE_SQ_LOCAL_INV_WQE_RD_FENCE_FLG_MASK   0x1
#define ROCE_SQ_LOCAL_INV_WQE_RD_FENCE_FLG_SHIFT  1
#define ROCE_SQ_LOCAL_INV_WQE_INV_FENCE_FLG_MASK  0x1
#define ROCE_SQ_LOCAL_INV_WQE_INV_FENCE_FLG_SHIFT 2
#define ROCE_SQ_LOCAL_INV_WQE_SE_FLG_MASK         0x1
#define ROCE_SQ_LOCAL_INV_WQE_SE_FLG_SHIFT        3
#define ROCE_SQ_LOCAL_INV_WQE_INLINE_FLG_MASK     0x1
#define ROCE_SQ_LOCAL_INV_WQE_INLINE_FLG_SHIFT    4
#define ROCE_SQ_LOCAL_INV_WQE_RESERVED0_MASK      0x7
#define ROCE_SQ_LOCAL_INV_WQE_RESERVED0_SHIFT     5
	u8 reserved1;
	u8 prev_wqe_size;
};


struct roce_sq_rdma_wqe {
	__le32 imm_data;
	__le32 length;
	__le32 xrc_srq;
	u8 req_type;
	u8 flags;
#define ROCE_SQ_RDMA_WQE_COMP_FLG_MASK       0x1
#define ROCE_SQ_RDMA_WQE_COMP_FLG_SHIFT      0
#define ROCE_SQ_RDMA_WQE_RD_FENCE_FLG_MASK   0x1
#define ROCE_SQ_RDMA_WQE_RD_FENCE_FLG_SHIFT  1
#define ROCE_SQ_RDMA_WQE_INV_FENCE_FLG_MASK  0x1
#define ROCE_SQ_RDMA_WQE_INV_FENCE_FLG_SHIFT 2
#define ROCE_SQ_RDMA_WQE_SE_FLG_MASK         0x1
#define ROCE_SQ_RDMA_WQE_SE_FLG_SHIFT        3
#define ROCE_SQ_RDMA_WQE_INLINE_FLG_MASK     0x1
#define ROCE_SQ_RDMA_WQE_INLINE_FLG_SHIFT    4
#define ROCE_SQ_RDMA_WQE_RESERVED0_MASK      0x7
#define ROCE_SQ_RDMA_WQE_RESERVED0_SHIFT     5
	u8 wqe_size;
	u8 prev_wqe_size;
	struct regpair remote_va;
	__le32 r_key;
	__le32 reserved1;
};


/*
 * First element (16 bytes) of rdma wqe
 */
struct roce_sq_rdma_wqe_1st {
	__le32 imm_data;
	__le32 length;
	__le32 xrc_srq;
	u8 req_type;
	u8 flags;
#define ROCE_SQ_RDMA_WQE_1ST_COMP_FLG_MASK       0x1
#define ROCE_SQ_RDMA_WQE_1ST_COMP_FLG_SHIFT      0
#define ROCE_SQ_RDMA_WQE_1ST_RD_FENCE_FLG_MASK   0x1
#define ROCE_SQ_RDMA_WQE_1ST_RD_FENCE_FLG_SHIFT  1
#define ROCE_SQ_RDMA_WQE_1ST_INV_FENCE_FLG_MASK  0x1
#define ROCE_SQ_RDMA_WQE_1ST_INV_FENCE_FLG_SHIFT 2
#define ROCE_SQ_RDMA_WQE_1ST_SE_FLG_MASK         0x1
#define ROCE_SQ_RDMA_WQE_1ST_SE_FLG_SHIFT        3
#define ROCE_SQ_RDMA_WQE_1ST_INLINE_FLG_MASK     0x1
#define ROCE_SQ_RDMA_WQE_1ST_INLINE_FLG_SHIFT    4
#define ROCE_SQ_RDMA_WQE_1ST_RESERVED0_MASK      0x7
#define ROCE_SQ_RDMA_WQE_1ST_RESERVED0_SHIFT     5
	u8 wqe_size;
	u8 prev_wqe_size;
};


/*
 * Second element (16 bytes) of rdma wqe
 */
struct roce_sq_rdma_wqe_2nd {
	struct regpair remote_va;
	__le32 r_key;
	__le32 reserved1;
};


/*
 * SQ WQE req type enumeration
 */
enum roce_sq_req_type {
	ROCE_SQ_REQ_TYPE_SEND,
	ROCE_SQ_REQ_TYPE_SEND_WITH_IMM,
	ROCE_SQ_REQ_TYPE_SEND_WITH_INVALIDATE,
	ROCE_SQ_REQ_TYPE_RDMA_WR,
	ROCE_SQ_REQ_TYPE_RDMA_WR_WITH_IMM,
	ROCE_SQ_REQ_TYPE_RDMA_RD,
	ROCE_SQ_REQ_TYPE_ATOMIC_CMP_AND_SWAP,
	ROCE_SQ_REQ_TYPE_ATOMIC_ADD,
	ROCE_SQ_REQ_TYPE_LOCAL_INVALIDATE,
	ROCE_SQ_REQ_TYPE_FAST_MR,
	ROCE_SQ_REQ_TYPE_BIND,
	ROCE_SQ_REQ_TYPE_INVALID,
	MAX_ROCE_SQ_REQ_TYPE
};


struct roce_sq_send_wqe {
	__le32 inv_key_or_imm_data;
	__le32 length;
	__le32 xrc_srq;
	u8 req_type;
	u8 flags;
#define ROCE_SQ_SEND_WQE_COMP_FLG_MASK       0x1
#define ROCE_SQ_SEND_WQE_COMP_FLG_SHIFT      0
#define ROCE_SQ_SEND_WQE_RD_FENCE_FLG_MASK   0x1
#define ROCE_SQ_SEND_WQE_RD_FENCE_FLG_SHIFT  1
#define ROCE_SQ_SEND_WQE_INV_FENCE_FLG_MASK  0x1
#define ROCE_SQ_SEND_WQE_INV_FENCE_FLG_SHIFT 2
#define ROCE_SQ_SEND_WQE_SE_FLG_MASK         0x1
#define ROCE_SQ_SEND_WQE_SE_FLG_SHIFT        3
#define ROCE_SQ_SEND_WQE_INLINE_FLG_MASK     0x1
#define ROCE_SQ_SEND_WQE_INLINE_FLG_SHIFT    4
#define ROCE_SQ_SEND_WQE_RESERVED0_MASK      0x7
#define ROCE_SQ_SEND_WQE_RESERVED0_SHIFT     5
	u8 wqe_size;
	u8 prev_wqe_size;
};


struct roce_sq_sge {
	__le32 length;
	struct regpair addr;
	__le32 l_key;
};


struct roce_srq_prod {
	__le16 prod;
};


struct roce_srq_sge {
	struct regpair addr;
	__le32 length;
	__le32 l_key;
	struct regpair wr_id;
	u8 flags;
#define ROCE_SRQ_SGE_NUM_SGES_MASK   0x3
#define ROCE_SRQ_SGE_NUM_SGES_SHIFT  0
#define ROCE_SRQ_SGE_RESERVED0_MASK  0x3F
#define ROCE_SRQ_SGE_RESERVED0_SHIFT 2
	u8 reserved1;
	__le16 reserved2;
	__le32 reserved3;
};


/*
 * RoCE doorbell data for SQ and RQ
 */
struct roce_pwm_val16_data {
	__le16 icid;
	__le16 prod_val;
};


union roce_pwm_val16_data_union {
	struct roce_pwm_val16_data as_struct;
	__le32 as_dword;
};


/*
 * RoCE doorbell data for CQ
 */
struct roce_pwm_val32_data {
	__le16 icid;
	u8 agg_flags;
	u8 params;
#define ROCE_PWM_VAL32_DATA_AGG_CMD_MASK    0x3
#define ROCE_PWM_VAL32_DATA_AGG_CMD_SHIFT   0
#define ROCE_PWM_VAL32_DATA_BYPASS_EN_MASK  0x1
#define ROCE_PWM_VAL32_DATA_BYPASS_EN_SHIFT 2
#define ROCE_PWM_VAL32_DATA_RESERVED_MASK   0x1F
#define ROCE_PWM_VAL32_DATA_RESERVED_SHIFT  3
	__le32 cq_cons_val;
};


union roce_pwm_val32_data_union {
	struct roce_pwm_val32_data as_struct;
	struct regpair as_repair;
};

#endif /* __QLNXR_ROCE_H__ */

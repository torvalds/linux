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

#ifndef __QLNXR_USER_H__
#define __QLNXR_USER_H__

#define QLNXR_ABI_VERSION		(7)
#define QLNXR_BE_ROCE_ABI_VERSION	(1)

/* user kernel communication data structures. */

struct qlnxr_alloc_ucontext_resp {
	u64 db_pa;
	u32 db_size;

	uint32_t max_send_wr;
	uint32_t max_recv_wr;
	uint32_t max_srq_wr;
	uint32_t sges_per_send_wr;
	uint32_t sges_per_recv_wr;
	uint32_t sges_per_srq_wr;
	int max_cqes;
	uint8_t dpm_enabled;
	uint8_t wids_enabled;
	uint16_t wid_count;
};

struct qlnxr_alloc_pd_ureq {
	u64 rsvd1;
};

struct qlnxr_alloc_pd_uresp {
	u32 pd_id;
};

struct qlnxr_create_cq_ureq {
	uint64_t addr;		/* user space virtual address of CQ buffer */
	size_t len;		/* size of CQ buffer */
};

struct qlnxr_create_cq_uresp {
	u32 db_offset;
	u16 icid;
};

struct qlnxr_create_qp_ureq {
	u32 qp_handle_hi;
	u32 qp_handle_lo;

	/* SQ */
	uint64_t sq_addr;	/* user space virtual address of SQ buffer */
	size_t sq_len;		/* length of SQ buffer */

	/* RQ */
	uint64_t rq_addr;	/* user space virtual address of RQ buffer */
	size_t rq_len;		/* length of RQ buffer */
};

struct qlnxr_create_qp_uresp {
	u32 qp_id;
	int atomic_supported;

	/* SQ*/
	u32 sq_db_offset;
	u16 sq_icid;
	
	/* RQ */
	u32 rq_db_offset;
	u16 rq_icid;

	u32 rq_db2_offset;
};

struct qlnxr_create_srq_ureq {
	/* user space virtual address of producer pair */
	uint64_t prod_pair_addr;
	uint64_t srq_addr;	/* user space virtual address of SQ buffer */
	size_t srq_len;		/* length of SQ buffer */
};

struct qlnxr_create_srq_uresp {
	u16 srq_id;
};

#endif	/* #ifndef __QLNXR_USER_H__ */

/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */

/*
 * This file contains defines, structures, etc. that are used
 * to communicate between kernel and user code.
 */

#ifndef RVT_ABI_USER_H
#define RVT_ABI_USER_H

#include <linux/types.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_verbs.h>
#ifndef RDMA_ATOMIC_UAPI
#define RDMA_ATOMIC_UAPI(_type, _name) struct{ _type val; } _name
#endif

/*
 * This structure is used to contain the head pointer, tail pointer,
 * and completion queue entries as a single memory allocation so
 * it can be mmap'ed into user space.
 */
struct rvt_cq_wc {
	/* index of next entry to fill */
	RDMA_ATOMIC_UAPI(__u32, head);
	/* index of next ib_poll_cq() entry */
	RDMA_ATOMIC_UAPI(__u32, tail);

	/* these are actually size ibcq.cqe + 1 */
	struct ib_uverbs_wc uqueue[];
};

/*
 * Receive work request queue entry.
 * The size of the sg_list is determined when the QP (or SRQ) is created
 * and stored in qp->r_rq.max_sge (or srq->rq.max_sge).
 */
struct rvt_rwqe {
	__u64 wr_id;
	__u8 num_sge;
	__u8 padding[7];
	struct ib_sge sg_list[];
};

/*
 * This structure is used to contain the head pointer, tail pointer,
 * and receive work queue entries as a single memory allocation so
 * it can be mmap'ed into user space.
 * Note that the wq array elements are variable size so you can't
 * just index into the array to get the N'th element;
 * use get_rwqe_ptr() for user space and rvt_get_rwqe_ptr()
 * for kernel space.
 */
struct rvt_rwq {
	/* new work requests posted to the head */
	RDMA_ATOMIC_UAPI(__u32, head);
	/* receives pull requests from here. */
	RDMA_ATOMIC_UAPI(__u32, tail);
	struct rvt_rwqe wq[];
};
#endif /* RVT_ABI_USER_H */

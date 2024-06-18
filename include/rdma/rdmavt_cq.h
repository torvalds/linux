/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright(c) 2016 - 2018 Intel Corporation.
 */

#ifndef DEF_RDMAVT_INCCQ_H
#define DEF_RDMAVT_INCCQ_H

#include <linux/kthread.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_verbs.h>

/*
 * Define an ib_cq_notify value that is not valid so we know when CQ
 * notifications are armed.
 */
#define RVT_CQ_NONE      (IB_CQ_NEXT_COMP + 1)

/*
 * Define read macro that apply smp_load_acquire memory barrier
 * when reading indice of circular buffer that mmaped to user space.
 */
#define RDMA_READ_UAPI_ATOMIC(member) smp_load_acquire(&(member).val)

/*
 * Define write macro that uses smp_store_release memory barrier
 * when writing indice of circular buffer that mmaped to user space.
 */
#define RDMA_WRITE_UAPI_ATOMIC(member, x) smp_store_release(&(member).val, x)
#include <rdma/rvt-abi.h>

/*
 * This structure is used to contain the head pointer, tail pointer,
 * and completion queue entries as a single memory allocation so
 * it can be mmap'ed into user space.
 */
struct rvt_k_cq_wc {
	u32 head;               /* index of next entry to fill */
	u32 tail;               /* index of next ib_poll_cq() entry */
	struct ib_wc kqueue[];
};

/*
 * The completion queue structure.
 */
struct rvt_cq {
	struct ib_cq ibcq;
	struct work_struct comptask;
	spinlock_t lock; /* protect changes in this struct */
	u8 notify;
	u8 triggered;
	u8 cq_full;
	int comp_vector_cpu;
	struct rvt_dev_info *rdi;
	struct rvt_cq_wc *queue;
	struct rvt_mmap_info *ip;
	struct rvt_k_cq_wc *kqueue;
};

static inline struct rvt_cq *ibcq_to_rvtcq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct rvt_cq, ibcq);
}

bool rvt_cq_enter(struct rvt_cq *cq, struct ib_wc *entry, bool solicited);

#endif          /* DEF_RDMAVT_INCCQH */

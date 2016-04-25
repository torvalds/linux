#ifndef DEF_RDMAVT_INCCQ_H
#define DEF_RDMAVT_INCCQ_H

/*
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2016 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/kthread.h>
#include <rdma/ib_user_verbs.h>

/*
 * Define an ib_cq_notify value that is not valid so we know when CQ
 * notifications are armed.
 */
#define RVT_CQ_NONE      (IB_CQ_NEXT_COMP + 1)

/*
 * This structure is used to contain the head pointer, tail pointer,
 * and completion queue entries as a single memory allocation so
 * it can be mmap'ed into user space.
 */
struct rvt_cq_wc {
	u32 head;               /* index of next entry to fill */
	u32 tail;               /* index of next ib_poll_cq() entry */
	union {
		/* these are actually size ibcq.cqe + 1 */
		struct ib_uverbs_wc uqueue[0];
		struct ib_wc kqueue[0];
	};
};

/*
 * The completion queue structure.
 */
struct rvt_cq {
	struct ib_cq ibcq;
	struct kthread_work comptask;
	spinlock_t lock; /* protect changes in this struct */
	u8 notify;
	u8 triggered;
	struct rvt_dev_info *rdi;
	struct rvt_cq_wc *queue;
	struct rvt_mmap_info *ip;
};

static inline struct rvt_cq *ibcq_to_rvtcq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct rvt_cq, ibcq);
}

void rvt_cq_enter(struct rvt_cq *cq, struct ib_wc *entry, bool solicited);

#endif          /* DEF_RDMAVT_INCCQH */

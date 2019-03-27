/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
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

#ifndef MTHCA_PROVIDER_H
#define MTHCA_PROVIDER_H

#include <rdma/ib_verbs.h>
#include <rdma/ib_pack.h>

#include <linux/wait.h>

#define MTHCA_MPT_FLAG_ATOMIC        (1 << 14)
#define MTHCA_MPT_FLAG_REMOTE_WRITE  (1 << 13)
#define MTHCA_MPT_FLAG_REMOTE_READ   (1 << 12)
#define MTHCA_MPT_FLAG_LOCAL_WRITE   (1 << 11)
#define MTHCA_MPT_FLAG_LOCAL_READ    (1 << 10)

struct mthca_buf_list {
	void *buf;
	DEFINE_DMA_UNMAP_ADDR(mapping);
};

union mthca_buf {
	struct mthca_buf_list direct;
	struct mthca_buf_list *page_list;
};

struct mthca_uar {
	unsigned long pfn;
	int           index;
};

struct mthca_user_db_table;

struct mthca_ucontext {
	struct ib_ucontext          ibucontext;
	struct mthca_uar            uar;
	struct mthca_user_db_table *db_tab;
	int			    reg_mr_warned;
};

struct mthca_mtt;

struct mthca_mr {
	struct ib_mr      ibmr;
	struct ib_umem   *umem;
	struct mthca_mtt *mtt;
};

struct mthca_fmr {
	struct ib_fmr      ibmr;
	struct ib_fmr_attr attr;
	struct mthca_mtt  *mtt;
	int                maps;
	union {
		struct {
			struct mthca_mpt_entry __iomem *mpt;
			u64 __iomem *mtts;
		} tavor;
		struct {
			struct mthca_mpt_entry *mpt;
			__be64 *mtts;
			dma_addr_t dma_handle;
		} arbel;
	} mem;
};

struct mthca_pd {
	struct ib_pd    ibpd;
	u32             pd_num;
	atomic_t        sqp_count;
	struct mthca_mr ntmr;
	int             privileged;
};

struct mthca_eq {
	struct mthca_dev      *dev;
	int                    eqn;
	u32                    eqn_mask;
	u32                    cons_index;
	u16                    msi_x_vector;
	u16                    msi_x_entry;
	int                    have_irq;
	int                    nent;
	struct mthca_buf_list *page_list;
	struct mthca_mr        mr;
	char		       irq_name[IB_DEVICE_NAME_MAX];
};

struct mthca_av;

enum mthca_ah_type {
	MTHCA_AH_ON_HCA,
	MTHCA_AH_PCI_POOL,
	MTHCA_AH_KMALLOC
};

struct mthca_ah {
	struct ib_ah       ibah;
	enum mthca_ah_type type;
	u32                key;
	struct mthca_av   *av;
	dma_addr_t         avdma;
};

/*
 * Quick description of our CQ/QP locking scheme:
 *
 * We have one global lock that protects dev->cq/qp_table.  Each
 * struct mthca_cq/qp also has its own lock.  An individual qp lock
 * may be taken inside of an individual cq lock.  Both cqs attached to
 * a qp may be locked, with the cq with the lower cqn locked first.
 * No other nesting should be done.
 *
 * Each struct mthca_cq/qp also has an ref count, protected by the
 * corresponding table lock.  The pointer from the cq/qp_table to the
 * struct counts as one reference.  This reference also is good for
 * access through the consumer API, so modifying the CQ/QP etc doesn't
 * need to take another reference.  Access to a QP because of a
 * completion being polled does not need a reference either.
 *
 * Finally, each struct mthca_cq/qp has a wait_queue_head_t for the
 * destroy function to sleep on.
 *
 * This means that access from the consumer API requires nothing but
 * taking the struct's lock.
 *
 * Access because of a completion event should go as follows:
 * - lock cq/qp_table and look up struct
 * - increment ref count in struct
 * - drop cq/qp_table lock
 * - lock struct, do your thing, and unlock struct
 * - decrement ref count; if zero, wake up waiters
 *
 * To destroy a CQ/QP, we can do the following:
 * - lock cq/qp_table
 * - remove pointer and decrement ref count
 * - unlock cq/qp_table lock
 * - wait_event until ref count is zero
 *
 * It is the consumer's responsibilty to make sure that no QP
 * operations (WQE posting or state modification) are pending when a
 * QP is destroyed.  Also, the consumer must make sure that calls to
 * qp_modify are serialized.  Similarly, the consumer is responsible
 * for ensuring that no CQ resize operations are pending when a CQ
 * is destroyed.
 *
 * Possible optimizations (wait for profile data to see if/where we
 * have locks bouncing between CPUs):
 * - split cq/qp table lock into n separate (cache-aligned) locks,
 *   indexed (say) by the page in the table
 * - split QP struct lock into three (one for common info, one for the
 *   send queue and one for the receive queue)
 */

struct mthca_cq_buf {
	union mthca_buf		queue;
	struct mthca_mr		mr;
	int			is_direct;
};

struct mthca_cq_resize {
	struct mthca_cq_buf	buf;
	int			cqe;
	enum {
		CQ_RESIZE_ALLOC,
		CQ_RESIZE_READY,
		CQ_RESIZE_SWAPPED
	}			state;
};

struct mthca_cq {
	struct ib_cq		ibcq;
	spinlock_t		lock;
	int			refcount;
	int			cqn;
	u32			cons_index;
	struct mthca_cq_buf	buf;
	struct mthca_cq_resize *resize_buf;
	int			is_kernel;

	/* Next fields are Arbel only */
	int			set_ci_db_index;
	__be32		       *set_ci_db;
	int			arm_db_index;
	__be32		       *arm_db;
	int			arm_sn;

	wait_queue_head_t	wait;
	struct mutex		mutex;
};

struct mthca_srq {
	struct ib_srq		ibsrq;
	spinlock_t		lock;
	int			refcount;
	int			srqn;
	int			max;
	int			max_gs;
	int			wqe_shift;
	int			first_free;
	int			last_free;
	u16			counter;  /* Arbel only */
	int			db_index; /* Arbel only */
	__be32		       *db;       /* Arbel only */
	void		       *last;

	int			is_direct;
	u64		       *wrid;
	union mthca_buf		queue;
	struct mthca_mr		mr;

	wait_queue_head_t	wait;
	struct mutex		mutex;
};

struct mthca_wq {
	spinlock_t lock;
	int        max;
	unsigned   next_ind;
	unsigned   last_comp;
	unsigned   head;
	unsigned   tail;
	void      *last;
	int        max_gs;
	int        wqe_shift;

	int        db_index;	/* Arbel only */
	__be32    *db;
};

struct mthca_qp {
	struct ib_qp           ibqp;
	int                    refcount;
	u32                    qpn;
	int                    is_direct;
	u8                     port; /* for SQP and memfree use only */
	u8                     alt_port; /* for memfree use only */
	u8                     transport;
	u8                     state;
	u8                     atomic_rd_en;
	u8                     resp_depth;

	struct mthca_mr        mr;

	struct mthca_wq        rq;
	struct mthca_wq        sq;
	enum ib_sig_type       sq_policy;
	int                    send_wqe_offset;
	int                    max_inline_data;

	u64                   *wrid;
	union mthca_buf	       queue;

	wait_queue_head_t      wait;
	struct mutex	       mutex;
};

struct mthca_sqp {
	struct mthca_qp qp;
	int             pkey_index;
	u32             qkey;
	u32             send_psn;
	struct ib_ud_header ud_header;
	int             header_buf_size;
	void           *header_buf;
	dma_addr_t      header_dma;
};

static inline struct mthca_ucontext *to_mucontext(struct ib_ucontext *ibucontext)
{
	return container_of(ibucontext, struct mthca_ucontext, ibucontext);
}

static inline struct mthca_fmr *to_mfmr(struct ib_fmr *ibmr)
{
	return container_of(ibmr, struct mthca_fmr, ibmr);
}

static inline struct mthca_mr *to_mmr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct mthca_mr, ibmr);
}

static inline struct mthca_pd *to_mpd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct mthca_pd, ibpd);
}

static inline struct mthca_ah *to_mah(struct ib_ah *ibah)
{
	return container_of(ibah, struct mthca_ah, ibah);
}

static inline struct mthca_cq *to_mcq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct mthca_cq, ibcq);
}

static inline struct mthca_srq *to_msrq(struct ib_srq *ibsrq)
{
	return container_of(ibsrq, struct mthca_srq, ibsrq);
}

static inline struct mthca_qp *to_mqp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct mthca_qp, ibqp);
}

static inline struct mthca_sqp *to_msqp(struct mthca_qp *qp)
{
	return container_of(qp, struct mthca_sqp, qp);
}

#endif /* MTHCA_PROVIDER_H */

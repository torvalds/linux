#ifndef DEF_RDMAVT_INCQP_H
#define DEF_RDMAVT_INCQP_H

/*
 * Copyright(c) 2016 - 2018 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
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

#include <rdma/rdma_vt.h>
#include <rdma/ib_pack.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdmavt_cq.h>
/*
 * Atomic bit definitions for r_aflags.
 */
#define RVT_R_WRID_VALID        0
#define RVT_R_REWIND_SGE        1

/*
 * Bit definitions for r_flags.
 */
#define RVT_R_REUSE_SGE 0x01
#define RVT_R_RDMAR_SEQ 0x02
#define RVT_R_RSP_NAK   0x04
#define RVT_R_RSP_SEND  0x08
#define RVT_R_COMM_EST  0x10

/*
 * Bit definitions for s_flags.
 *
 * RVT_S_SIGNAL_REQ_WR - set if QP send WRs contain completion signaled
 * RVT_S_BUSY - send tasklet is processing the QP
 * RVT_S_TIMER - the RC retry timer is active
 * RVT_S_ACK_PENDING - an ACK is waiting to be sent after RDMA read/atomics
 * RVT_S_WAIT_FENCE - waiting for all prior RDMA read or atomic SWQEs
 *                         before processing the next SWQE
 * RVT_S_WAIT_RDMAR - waiting for a RDMA read or atomic SWQE to complete
 *                         before processing the next SWQE
 * RVT_S_WAIT_RNR - waiting for RNR timeout
 * RVT_S_WAIT_SSN_CREDIT - waiting for RC credits to process next SWQE
 * RVT_S_WAIT_DMA - waiting for send DMA queue to drain before generating
 *                  next send completion entry not via send DMA
 * RVT_S_WAIT_PIO - waiting for a send buffer to be available
 * RVT_S_WAIT_PIO_DRAIN - waiting for a qp to drain pio packets
 * RVT_S_WAIT_TX - waiting for a struct verbs_txreq to be available
 * RVT_S_WAIT_DMA_DESC - waiting for DMA descriptors to be available
 * RVT_S_WAIT_KMEM - waiting for kernel memory to be available
 * RVT_S_WAIT_PSN - waiting for a packet to exit the send DMA queue
 * RVT_S_WAIT_ACK - waiting for an ACK packet before sending more requests
 * RVT_S_SEND_ONE - send one packet, request ACK, then wait for ACK
 * RVT_S_ECN - a BECN was queued to the send engine
 * RVT_S_MAX_BIT_MASK - The max bit that can be used by rdmavt
 */
#define RVT_S_SIGNAL_REQ_WR	0x0001
#define RVT_S_BUSY		0x0002
#define RVT_S_TIMER		0x0004
#define RVT_S_RESP_PENDING	0x0008
#define RVT_S_ACK_PENDING	0x0010
#define RVT_S_WAIT_FENCE	0x0020
#define RVT_S_WAIT_RDMAR	0x0040
#define RVT_S_WAIT_RNR		0x0080
#define RVT_S_WAIT_SSN_CREDIT	0x0100
#define RVT_S_WAIT_DMA		0x0200
#define RVT_S_WAIT_PIO		0x0400
#define RVT_S_WAIT_TX		0x0800
#define RVT_S_WAIT_DMA_DESC	0x1000
#define RVT_S_WAIT_KMEM		0x2000
#define RVT_S_WAIT_PSN		0x4000
#define RVT_S_WAIT_ACK		0x8000
#define RVT_S_SEND_ONE		0x10000
#define RVT_S_UNLIMITED_CREDIT	0x20000
#define RVT_S_ECN		0x40000
#define RVT_S_MAX_BIT_MASK	0x800000

/*
 * Drivers should use s_flags starting with bit 31 down to the bit next to
 * RVT_S_MAX_BIT_MASK
 */

/*
 * Wait flags that would prevent any packet type from being sent.
 */
#define RVT_S_ANY_WAIT_IO \
	(RVT_S_WAIT_PIO | RVT_S_WAIT_TX | \
	 RVT_S_WAIT_DMA_DESC | RVT_S_WAIT_KMEM)

/*
 * Wait flags that would prevent send work requests from making progress.
 */
#define RVT_S_ANY_WAIT_SEND (RVT_S_WAIT_FENCE | RVT_S_WAIT_RDMAR | \
	RVT_S_WAIT_RNR | RVT_S_WAIT_SSN_CREDIT | RVT_S_WAIT_DMA | \
	RVT_S_WAIT_PSN | RVT_S_WAIT_ACK)

#define RVT_S_ANY_WAIT (RVT_S_ANY_WAIT_IO | RVT_S_ANY_WAIT_SEND)

/* Number of bits to pay attention to in the opcode for checking qp type */
#define RVT_OPCODE_QP_MASK 0xE0

/* Flags for checking QP state (see ib_rvt_state_ops[]) */
#define RVT_POST_SEND_OK                0x01
#define RVT_POST_RECV_OK                0x02
#define RVT_PROCESS_RECV_OK             0x04
#define RVT_PROCESS_SEND_OK             0x08
#define RVT_PROCESS_NEXT_SEND_OK        0x10
#define RVT_FLUSH_SEND			0x20
#define RVT_FLUSH_RECV			0x40
#define RVT_PROCESS_OR_FLUSH_SEND \
	(RVT_PROCESS_SEND_OK | RVT_FLUSH_SEND)
#define RVT_SEND_OR_FLUSH_OR_RECV_OK \
	(RVT_PROCESS_SEND_OK | RVT_FLUSH_SEND | RVT_PROCESS_RECV_OK)

/*
 * Internal send flags
 */
#define RVT_SEND_RESERVE_USED           IB_SEND_RESERVED_START
#define RVT_SEND_COMPLETION_ONLY	(IB_SEND_RESERVED_START << 1)

/*
 * Send work request queue entry.
 * The size of the sg_list is determined when the QP is created and stored
 * in qp->s_max_sge.
 */
struct rvt_swqe {
	union {
		struct ib_send_wr wr;   /* don't use wr.sg_list */
		struct ib_ud_wr ud_wr;
		struct ib_reg_wr reg_wr;
		struct ib_rdma_wr rdma_wr;
		struct ib_atomic_wr atomic_wr;
	};
	u32 psn;                /* first packet sequence number */
	u32 lpsn;               /* last packet sequence number */
	u32 ssn;                /* send sequence number */
	u32 length;             /* total length of data in sg_list */
	void *priv;             /* driver dependent field */
	struct rvt_sge sg_list[0];
};

/*
 * Receive work request queue entry.
 * The size of the sg_list is determined when the QP (or SRQ) is created
 * and stored in qp->r_rq.max_sge (or srq->rq.max_sge).
 */
struct rvt_rwqe {
	u64 wr_id;
	u8 num_sge;
	struct ib_sge sg_list[0];
};

/*
 * This structure is used to contain the head pointer, tail pointer,
 * and receive work queue entries as a single memory allocation so
 * it can be mmap'ed into user space.
 * Note that the wq array elements are variable size so you can't
 * just index into the array to get the N'th element;
 * use get_rwqe_ptr() instead.
 */
struct rvt_rwq {
	u32 head;               /* new work requests posted to the head */
	u32 tail;               /* receives pull requests from here. */
	struct rvt_rwqe wq[0];
};

struct rvt_rq {
	struct rvt_rwq *wq;
	u32 size;               /* size of RWQE array */
	u8 max_sge;
	/* protect changes in this struct */
	spinlock_t lock ____cacheline_aligned_in_smp;
};

/*
 * This structure is used by rvt_mmap() to validate an offset
 * when an mmap() request is made.  The vm_area_struct then uses
 * this as its vm_private_data.
 */
struct rvt_mmap_info {
	struct list_head pending_mmaps;
	struct ib_ucontext *context;
	void *obj;
	__u64 offset;
	struct kref ref;
	unsigned size;
};

/*
 * This structure holds the information that the send tasklet needs
 * to send a RDMA read response or atomic operation.
 */
struct rvt_ack_entry {
	struct rvt_sge rdma_sge;
	u64 atomic_data;
	u32 psn;
	u32 lpsn;
	u8 opcode;
	u8 sent;
	void *priv;
};

#define	RC_QP_SCALING_INTERVAL	5

#define RVT_OPERATION_PRIV        0x00000001
#define RVT_OPERATION_ATOMIC      0x00000002
#define RVT_OPERATION_ATOMIC_SGE  0x00000004
#define RVT_OPERATION_LOCAL       0x00000008
#define RVT_OPERATION_USE_RESERVE 0x00000010
#define RVT_OPERATION_IGN_RNR_CNT 0x00000020

#define RVT_OPERATION_MAX (IB_WR_RESERVED10 + 1)

/**
 * rvt_operation_params - op table entry
 * @length - the length to copy into the swqe entry
 * @qpt_support - a bit mask indicating QP type support
 * @flags - RVT_OPERATION flags (see above)
 *
 * This supports table driven post send so that
 * the driver can have differing an potentially
 * different sets of operations.
 *
 **/

struct rvt_operation_params {
	size_t length;
	u32 qpt_support;
	u32 flags;
};

/*
 * Common variables are protected by both r_rq.lock and s_lock in that order
 * which only happens in modify_qp() or changing the QP 'state'.
 */
struct rvt_qp {
	struct ib_qp ibqp;
	void *priv; /* Driver private data */
	/* read mostly fields above and below */
	struct rdma_ah_attr remote_ah_attr;
	struct rdma_ah_attr alt_ah_attr;
	struct rvt_qp __rcu *next;           /* link list for QPN hash table */
	struct rvt_swqe *s_wq;  /* send work queue */
	struct rvt_mmap_info *ip;

	unsigned long timeout_jiffies;  /* computed from timeout */

	int srate_mbps;		/* s_srate (below) converted to Mbit/s */
	pid_t pid;		/* pid for user mode QPs */
	u32 remote_qpn;
	u32 qkey;               /* QKEY for this QP (for UD or RD) */
	u32 s_size;             /* send work queue size */

	u16 pmtu;		/* decoded from path_mtu */
	u8 log_pmtu;		/* shift for pmtu */
	u8 state;               /* QP state */
	u8 allowed_ops;		/* high order bits of allowed opcodes */
	u8 qp_access_flags;
	u8 alt_timeout;         /* Alternate path timeout for this QP */
	u8 timeout;             /* Timeout for this QP */
	u8 s_srate;
	u8 s_mig_state;
	u8 port_num;
	u8 s_pkey_index;        /* PKEY index to use */
	u8 s_alt_pkey_index;    /* Alternate path PKEY index to use */
	u8 r_max_rd_atomic;     /* max number of RDMA read/atomic to receive */
	u8 s_max_rd_atomic;     /* max number of RDMA read/atomic to send */
	u8 s_retry_cnt;         /* number of times to retry */
	u8 s_rnr_retry_cnt;
	u8 r_min_rnr_timer;     /* retry timeout value for RNR NAKs */
	u8 s_max_sge;           /* size of s_wq->sg_list */
	u8 s_draining;

	/* start of read/write fields */
	atomic_t refcount ____cacheline_aligned_in_smp;
	wait_queue_head_t wait;

	struct rvt_ack_entry *s_ack_queue;
	struct rvt_sge_state s_rdma_read_sge;

	spinlock_t r_lock ____cacheline_aligned_in_smp;      /* used for APM */
	u32 r_psn;              /* expected rcv packet sequence number */
	unsigned long r_aflags;
	u64 r_wr_id;            /* ID for current receive WQE */
	u32 r_ack_psn;          /* PSN for next ACK or atomic ACK */
	u32 r_len;              /* total length of r_sge */
	u32 r_rcv_len;          /* receive data len processed */
	u32 r_msn;              /* message sequence number */

	u8 r_state;             /* opcode of last packet received */
	u8 r_flags;
	u8 r_head_ack_queue;    /* index into s_ack_queue[] */
	u8 r_adefered;          /* defered ack count */

	struct list_head rspwait;       /* link for waiting to respond */

	struct rvt_sge_state r_sge;     /* current receive data */
	struct rvt_rq r_rq;             /* receive work queue */

	/* post send line */
	spinlock_t s_hlock ____cacheline_aligned_in_smp;
	u32 s_head;             /* new entries added here */
	u32 s_next_psn;         /* PSN for next request */
	u32 s_avail;            /* number of entries avail */
	u32 s_ssn;              /* SSN of tail entry */
	atomic_t s_reserved_used; /* reserved entries in use */

	spinlock_t s_lock ____cacheline_aligned_in_smp;
	u32 s_flags;
	struct rvt_sge_state *s_cur_sge;
	struct rvt_swqe *s_wqe;
	struct rvt_sge_state s_sge;     /* current send request data */
	struct rvt_mregion *s_rdma_mr;
	u32 s_len;              /* total length of s_sge */
	u32 s_rdma_read_len;    /* total length of s_rdma_read_sge */
	u32 s_last_psn;         /* last response PSN processed */
	u32 s_sending_psn;      /* lowest PSN that is being sent */
	u32 s_sending_hpsn;     /* highest PSN that is being sent */
	u32 s_psn;              /* current packet sequence number */
	u32 s_ack_rdma_psn;     /* PSN for sending RDMA read responses */
	u32 s_ack_psn;          /* PSN for acking sends and RDMA writes */
	u32 s_tail;             /* next entry to process */
	u32 s_cur;              /* current work queue entry */
	u32 s_acked;            /* last un-ACK'ed entry */
	u32 s_last;             /* last completed entry */
	u32 s_lsn;              /* limit sequence number (credit) */
	u32 s_ahgpsn;           /* set to the psn in the copy of the header */
	u16 s_cur_size;         /* size of send packet in bytes */
	u16 s_rdma_ack_cnt;
	u8 s_hdrwords;         /* size of s_hdr in 32 bit words */
	s8 s_ahgidx;
	u8 s_state;             /* opcode of last packet sent */
	u8 s_ack_state;         /* opcode of packet to ACK */
	u8 s_nak_state;         /* non-zero if NAK is pending */
	u8 r_nak_state;         /* non-zero if NAK is pending */
	u8 s_retry;             /* requester retry counter */
	u8 s_rnr_retry;         /* requester RNR retry counter */
	u8 s_num_rd_atomic;     /* number of RDMA read/atomic pending */
	u8 s_tail_ack_queue;    /* index into s_ack_queue[] */
	u8 s_acked_ack_queue;   /* index into s_ack_queue[] */

	struct rvt_sge_state s_ack_rdma_sge;
	struct timer_list s_timer;
	struct hrtimer s_rnr_timer;

	atomic_t local_ops_pending; /* number of fast_reg/local_inv reqs */

	/*
	 * This sge list MUST be last. Do not add anything below here.
	 */
	struct rvt_sge r_sg_list[0] /* verified SGEs */
		____cacheline_aligned_in_smp;
};

struct rvt_srq {
	struct ib_srq ibsrq;
	struct rvt_rq rq;
	struct rvt_mmap_info *ip;
	/* send signal when number of RWQEs < limit */
	u32 limit;
};

#define RVT_QPN_MAX                 BIT(24)
#define RVT_QPNMAP_ENTRIES          (RVT_QPN_MAX / PAGE_SIZE / BITS_PER_BYTE)
#define RVT_BITS_PER_PAGE           (PAGE_SIZE * BITS_PER_BYTE)
#define RVT_BITS_PER_PAGE_MASK      (RVT_BITS_PER_PAGE - 1)
#define RVT_QPN_MASK		    IB_QPN_MASK

/*
 * QPN-map pages start out as NULL, they get allocated upon
 * first use and are never deallocated. This way,
 * large bitmaps are not allocated unless large numbers of QPs are used.
 */
struct rvt_qpn_map {
	void *page;
};

struct rvt_qpn_table {
	spinlock_t lock; /* protect changes to the qp table */
	unsigned flags;         /* flags for QP0/1 allocated for each port */
	u32 last;               /* last QP number allocated */
	u32 nmaps;              /* size of the map table */
	u16 limit;
	u8  incr;
	/* bit map of free QP numbers other than 0/1 */
	struct rvt_qpn_map map[RVT_QPNMAP_ENTRIES];
};

struct rvt_qp_ibdev {
	u32 qp_table_size;
	u32 qp_table_bits;
	struct rvt_qp __rcu **qp_table;
	spinlock_t qpt_lock; /* qptable lock */
	struct rvt_qpn_table qpn_table;
};

/*
 * There is one struct rvt_mcast for each multicast GID.
 * All attached QPs are then stored as a list of
 * struct rvt_mcast_qp.
 */
struct rvt_mcast_qp {
	struct list_head list;
	struct rvt_qp *qp;
};

struct rvt_mcast_addr {
	union ib_gid mgid;
	u16 lid;
};

struct rvt_mcast {
	struct rb_node rb_node;
	struct rvt_mcast_addr mcast_addr;
	struct list_head qp_list;
	wait_queue_head_t wait;
	atomic_t refcount;
	int n_attached;
};

/*
 * Since struct rvt_swqe is not a fixed size, we can't simply index into
 * struct rvt_qp.s_wq.  This function does the array index computation.
 */
static inline struct rvt_swqe *rvt_get_swqe_ptr(struct rvt_qp *qp,
						unsigned n)
{
	return (struct rvt_swqe *)((char *)qp->s_wq +
				     (sizeof(struct rvt_swqe) +
				      qp->s_max_sge *
				      sizeof(struct rvt_sge)) * n);
}

/*
 * Since struct rvt_rwqe is not a fixed size, we can't simply index into
 * struct rvt_rwq.wq.  This function does the array index computation.
 */
static inline struct rvt_rwqe *rvt_get_rwqe_ptr(struct rvt_rq *rq, unsigned n)
{
	return (struct rvt_rwqe *)
		((char *)rq->wq->wq +
		 (sizeof(struct rvt_rwqe) +
		  rq->max_sge * sizeof(struct ib_sge)) * n);
}

/**
 * rvt_is_user_qp - return if this is user mode QP
 * @qp - the target QP
 */
static inline bool rvt_is_user_qp(struct rvt_qp *qp)
{
	return !!qp->pid;
}

/**
 * rvt_get_qp - get a QP reference
 * @qp - the QP to hold
 */
static inline void rvt_get_qp(struct rvt_qp *qp)
{
	atomic_inc(&qp->refcount);
}

/**
 * rvt_put_qp - release a QP reference
 * @qp - the QP to release
 */
static inline void rvt_put_qp(struct rvt_qp *qp)
{
	if (qp && atomic_dec_and_test(&qp->refcount))
		wake_up(&qp->wait);
}

/**
 * rvt_put_swqe - drop mr refs held by swqe
 * @wqe - the send wqe
 *
 * This drops any mr references held by the swqe
 */
static inline void rvt_put_swqe(struct rvt_swqe *wqe)
{
	int i;

	for (i = 0; i < wqe->wr.num_sge; i++) {
		struct rvt_sge *sge = &wqe->sg_list[i];

		rvt_put_mr(sge->mr);
	}
}

/**
 * rvt_qp_wqe_reserve - reserve operation
 * @qp - the rvt qp
 * @wqe - the send wqe
 *
 * This routine used in post send to record
 * a wqe relative reserved operation use.
 */
static inline void rvt_qp_wqe_reserve(
	struct rvt_qp *qp,
	struct rvt_swqe *wqe)
{
	atomic_inc(&qp->s_reserved_used);
}

/**
 * rvt_qp_wqe_unreserve - clean reserved operation
 * @qp - the rvt qp
 * @wqe - the send wqe
 *
 * This decrements the reserve use count.
 *
 * This call MUST precede the change to
 * s_last to insure that post send sees a stable
 * s_avail.
 *
 * An smp_mp__after_atomic() is used to insure
 * the compiler does not juggle the order of the s_last
 * ring index and the decrementing of s_reserved_used.
 */
static inline void rvt_qp_wqe_unreserve(
	struct rvt_qp *qp,
	struct rvt_swqe *wqe)
{
	if (unlikely(wqe->wr.send_flags & RVT_SEND_RESERVE_USED)) {
		atomic_dec(&qp->s_reserved_used);
		/* insure no compiler re-order up to s_last change */
		smp_mb__after_atomic();
	}
}

extern const enum ib_wc_opcode ib_rvt_wc_opcode[];

/**
 * rvt_qp_swqe_complete() - insert send completion
 * @qp - the qp
 * @wqe - the send wqe
 * @status - completion status
 *
 * Insert a send completion into the completion
 * queue if the qp indicates it should be done.
 *
 * See IBTA 10.7.3.1 for info on completion
 * control.
 */
static inline void rvt_qp_swqe_complete(
	struct rvt_qp *qp,
	struct rvt_swqe *wqe,
	enum ib_wc_opcode opcode,
	enum ib_wc_status status)
{
	if (unlikely(wqe->wr.send_flags & RVT_SEND_RESERVE_USED))
		return;
	if (!(qp->s_flags & RVT_S_SIGNAL_REQ_WR) ||
	    (wqe->wr.send_flags & IB_SEND_SIGNALED) ||
	     status != IB_WC_SUCCESS) {
		struct ib_wc wc;

		memset(&wc, 0, sizeof(wc));
		wc.wr_id = wqe->wr.wr_id;
		wc.status = status;
		wc.opcode = opcode;
		wc.qp = &qp->ibqp;
		wc.byte_len = wqe->length;
		rvt_cq_enter(ibcq_to_rvtcq(qp->ibqp.send_cq), &wc,
			     status != IB_WC_SUCCESS);
	}
}

/*
 * Compare the lower 24 bits of the msn values.
 * Returns an integer <, ==, or > than zero.
 */
static inline int rvt_cmp_msn(u32 a, u32 b)
{
	return (((int)a) - ((int)b)) << 8;
}

/**
 * rvt_compute_aeth - compute the AETH (syndrome + MSN)
 * @qp: the queue pair to compute the AETH for
 *
 * Returns the AETH.
 */
__be32 rvt_compute_aeth(struct rvt_qp *qp);

/**
 * rvt_get_credit - flush the send work queue of a QP
 * @qp: the qp who's send work queue to flush
 * @aeth: the Acknowledge Extended Transport Header
 *
 * The QP s_lock should be held.
 */
void rvt_get_credit(struct rvt_qp *qp, u32 aeth);

/**
 * rvt_restart_sge - rewind the sge state for a wqe
 * @ss: the sge state pointer
 * @wqe: the wqe to rewind
 * @len: the data length from the start of the wqe in bytes
 *
 * Returns the remaining data length.
 */
u32 rvt_restart_sge(struct rvt_sge_state *ss, struct rvt_swqe *wqe, u32 len);

/**
 * @qp - the qp pair
 * @len - the length
 *
 * Perform a shift based mtu round up divide
 */
static inline u32 rvt_div_round_up_mtu(struct rvt_qp *qp, u32 len)
{
	return (len + qp->pmtu - 1) >> qp->log_pmtu;
}

/**
 * @qp - the qp pair
 * @len - the length
 *
 * Perform a shift based mtu divide
 */
static inline u32 rvt_div_mtu(struct rvt_qp *qp, u32 len)
{
	return len >> qp->log_pmtu;
}

/**
 * rvt_timeout_to_jiffies - Convert a ULP timeout input into jiffies
 * @timeout - timeout input(0 - 31).
 *
 * Return a timeout value in jiffies.
 */
static inline unsigned long rvt_timeout_to_jiffies(u8 timeout)
{
	if (timeout > 31)
		timeout = 31;

	return usecs_to_jiffies(1U << timeout) * 4096UL / 1000UL;
}

extern const int  ib_rvt_state_ops[];

struct rvt_dev_info;
int rvt_get_rwqe(struct rvt_qp *qp, bool wr_id_only);
void rvt_comm_est(struct rvt_qp *qp);
int rvt_error_qp(struct rvt_qp *qp, enum ib_wc_status err);
void rvt_rc_error(struct rvt_qp *qp, enum ib_wc_status err);
unsigned long rvt_rnr_tbl_to_usec(u32 index);
enum hrtimer_restart rvt_rc_rnr_retry(struct hrtimer *t);
void rvt_add_rnr_timer(struct rvt_qp *qp, u32 aeth);
void rvt_del_timers_sync(struct rvt_qp *qp);
void rvt_stop_rc_timers(struct rvt_qp *qp);
void rvt_add_retry_timer_ext(struct rvt_qp *qp, u8 shift);
static inline void rvt_add_retry_timer(struct rvt_qp *qp)
{
	rvt_add_retry_timer_ext(qp, 0);
}

void rvt_copy_sge(struct rvt_qp *qp, struct rvt_sge_state *ss,
		  void *data, u32 length,
		  bool release, bool copy_last);
void rvt_send_complete(struct rvt_qp *qp, struct rvt_swqe *wqe,
		       enum ib_wc_status status);
void rvt_ruc_loopback(struct rvt_qp *qp);

/**
 * struct rvt_qp_iter - the iterator for QPs
 * @qp - the current QP
 *
 * This structure defines the current iterator
 * state for sequenced access to all QPs relative
 * to an rvt_dev_info.
 */
struct rvt_qp_iter {
	struct rvt_qp *qp;
	/* private: backpointer */
	struct rvt_dev_info *rdi;
	/* private: callback routine */
	void (*cb)(struct rvt_qp *qp, u64 v);
	/* private: for arg to callback routine */
	u64 v;
	/* private: number of SMI,GSI QPs for device */
	int specials;
	/* private: current iterator index */
	int n;
};

struct rvt_qp_iter *rvt_qp_iter_init(struct rvt_dev_info *rdi,
				     u64 v,
				     void (*cb)(struct rvt_qp *qp, u64 v));
int rvt_qp_iter_next(struct rvt_qp_iter *iter);
void rvt_qp_iter(struct rvt_dev_info *rdi,
		 u64 v,
		 void (*cb)(struct rvt_qp *qp, u64 v));
void rvt_qp_mr_clean(struct rvt_qp *qp, u32 lkey);
#endif          /* DEF_RDMAVT_INCQP_H */

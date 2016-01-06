#ifndef DEF_RDMA_VT_H
#define DEF_RDMA_VT_H

/*
 * Copyright(c) 2015 Intel Corporation.
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

/*
 * Structure that low level drivers will populate in order to register with the
 * rdmavt layer.
 */

#include "ib_verbs.h"

/*
 * For Memory Regions. This stuff should probably be moved into rdmavt/mr.h once
 * drivers no longer need access to the MR directly.
 */

/*
 * A segment is a linear region of low physical memory.
 * Used by the verbs layer.
 */
struct rvt_seg {
	void *vaddr;
	size_t length;
};

/* The number of rvt_segs that fit in a page. */
#define RVT_SEGSZ     (PAGE_SIZE / sizeof(struct rvt_seg))

struct rvt_segarray {
	struct rvt_seg segs[RVT_SEGSZ];
};

struct rvt_mregion {
	struct ib_pd *pd;       /* shares refcnt of ibmr.pd */
	u64 user_base;          /* User's address for this region */
	u64 iova;               /* IB start address of this region */
	size_t length;
	u32 lkey;
	u32 offset;             /* offset (bytes) to start of region */
	int access_flags;
	u32 max_segs;           /* number of rvt_segs in all the arrays */
	u32 mapsz;              /* size of the map array */
	u8  page_shift;         /* 0 - non unform/non powerof2 sizes */
	u8  lkey_published;     /* in global table */
	struct completion comp; /* complete when refcount goes to zero */
	atomic_t refcount;
	struct rvt_segarray *map[0];    /* the segments */
};

#define RVT_MAX_LKEY_TABLE_BITS 23

struct rvt_lkey_table {
	spinlock_t lock; /* protect changes in this struct */
	u32 next;               /* next unused index (speeds search) */
	u32 gen;                /* generation count */
	u32 max;                /* size of the table */
	struct rvt_mregion __rcu **table;
};

/* End Memmory Region */

/*
 * Things needed for the Queue Pair definition. Like the MR stuff above the
 * following should probably get moved to qp.h once drivers stop trying to make
 * and manipulate thier own QPs. For the few instnaces where a driver may need
 * to look into a queue pair there should be a pointer to a driver priavte data
 * structure that they can look at.
 */

/*
 * These keep track of the copy progress within a memory region.
 * Used by the verbs layer.
 */
struct rvt_sge {
	struct rvt_mregion *mr;
	void *vaddr;            /* kernel virtual address of segment */
	u32 sge_length;         /* length of the SGE */
	u32 length;             /* remaining length of the segment */
	u16 m;                  /* current index: mr->map[m] */
	u16 n;                  /* current index: mr->map[m]->segs[n] */
};

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

#define RVT_MAX_RDMA_ATOMIC	16

/*
 * This structure holds the information that the send tasklet needs
 * to send a RDMA read response or atomic operation.
 */
struct rvt_ack_entry {
	u8 opcode;
	u8 sent;
	u32 psn;
	u32 lpsn;
	union {
		struct rvt_sge rdma_sge;
		u64 atomic_data;
	};
};

struct rvt_sge_state {
	struct rvt_sge *sg_list;      /* next SGE to be used if any */
	struct rvt_sge sge;   /* progress state for the current SGE */
	u32 total_len;
	u8 num_sge;
};

/*
 * Variables prefixed with s_ are for the requester (sender).
 * Variables prefixed with r_ are for the responder (receiver).
 * Variables prefixed with ack_ are for responder replies.
 *
 * Common variables are protected by both r_rq.lock and s_lock in that order
 * which only happens in modify_qp() or changing the QP 'state'.
 */
struct rvt_qp {
	struct ib_qp ibqp;
	void *priv; /* Driver private data */
	/* read mostly fields above and below */
	struct ib_ah_attr remote_ah_attr;
	struct ib_ah_attr alt_ah_attr;
	struct rvt_qp __rcu *next;           /* link list for QPN hash table */
	struct rvt_swqe *s_wq;  /* send work queue */
	struct rvt_mmap_info *ip;

	unsigned long timeout_jiffies;  /* computed from timeout */

	enum ib_mtu path_mtu;
	int srate_mbps;		/* s_srate (below) converted to Mbit/s */
	u32 remote_qpn;
	u32 pmtu;		/* decoded from path_mtu */
	u32 qkey;               /* QKEY for this QP (for UD or RD) */
	u32 s_size;             /* send work queue size */
	u32 s_rnr_timeout;      /* number of milliseconds for RNR timeout */
	u32 s_ahgpsn;           /* set to the psn in the copy of the header */

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

	struct rvt_ack_entry s_ack_queue[RVT_MAX_RDMA_ATOMIC + 1]
		____cacheline_aligned_in_smp;
	struct rvt_sge_state s_rdma_read_sge;

	spinlock_t r_lock ____cacheline_aligned_in_smp;      /* used for APM */
	unsigned long r_aflags;
	u64 r_wr_id;            /* ID for current receive WQE */
	u32 r_ack_psn;          /* PSN for next ACK or atomic ACK */
	u32 r_len;              /* total length of r_sge */
	u32 r_rcv_len;          /* receive data len processed */
	u32 r_psn;              /* expected rcv packet sequence number */
	u32 r_msn;              /* message sequence number */

	u8 r_state;             /* opcode of last packet received */
	u8 r_flags;
	u8 r_head_ack_queue;    /* index into s_ack_queue[] */

	struct list_head rspwait;       /* link for waiting to respond */

	struct rvt_sge_state r_sge;     /* current receive data */
	struct rvt_rq r_rq;             /* receive work queue */

	spinlock_t s_lock ____cacheline_aligned_in_smp;
	struct rvt_sge_state *s_cur_sge;
	u32 s_flags;
	struct rvt_swqe *s_wqe;
	struct rvt_sge_state s_sge;     /* current send request data */
	struct rvt_mregion *s_rdma_mr;
	struct sdma_engine *s_sde; /* current sde */
	u32 s_cur_size;         /* size of send packet in bytes */
	u32 s_len;              /* total length of s_sge */
	u32 s_rdma_read_len;    /* total length of s_rdma_read_sge */
	u32 s_next_psn;         /* PSN for next request */
	u32 s_last_psn;         /* last response PSN processed */
	u32 s_sending_psn;      /* lowest PSN that is being sent */
	u32 s_sending_hpsn;     /* highest PSN that is being sent */
	u32 s_psn;              /* current packet sequence number */
	u32 s_ack_rdma_psn;     /* PSN for sending RDMA read responses */
	u32 s_ack_psn;          /* PSN for acking sends and RDMA writes */
	u32 s_head;             /* new entries added here */
	u32 s_tail;             /* next entry to process */
	u32 s_cur;              /* current work queue entry */
	u32 s_acked;            /* last un-ACK'ed entry */
	u32 s_last;             /* last completed entry */
	u32 s_ssn;              /* SSN of tail entry */
	u32 s_lsn;              /* limit sequence number (credit) */
	u16 s_hdrwords;         /* size of s_hdr in 32 bit words */
	u16 s_rdma_ack_cnt;
	s8 s_ahgidx;
	u8 s_state;             /* opcode of last packet sent */
	u8 s_ack_state;         /* opcode of packet to ACK */
	u8 s_nak_state;         /* non-zero if NAK is pending */
	u8 r_nak_state;         /* non-zero if NAK is pending */
	u8 s_retry;             /* requester retry counter */
	u8 s_rnr_retry;         /* requester RNR retry counter */
	u8 s_num_rd_atomic;     /* number of RDMA read/atomic pending */
	u8 s_tail_ack_queue;    /* index into s_ack_queue[] */

	struct rvt_sge_state s_ack_rdma_sge;
	struct timer_list s_timer;

	/*
	 * This sge list MUST be last. Do not add anything below here.
	 */
	struct rvt_sge r_sg_list[0] /* verified SGEs */
		____cacheline_aligned_in_smp;
};

/* End QP section */

/*
 * Things that are driver specific, module parameters in hfi1 and qib
 */
struct rvt_driver_params {
	/*
	 * driver required fields:
	 *	node_guid
	 *	phys_port_cnt
	 *	dma_device
	 *	owner
	 * driver optional fields (rvt will provide generic value if blank):
	 *	name
	 *	node_desc
	 * rvt fields, driver value ignored:
	 *	uverbs_abi_ver
	 *	node_type
	 *	num_comp_vectors
	 *	uverbs_cmd_mask
	 */
	struct ib_device_attr props;

	/*
	 * Drivers will need to support a number of notifications to rvt in
	 * accordance with certain events. This structure should contain a mask
	 * of the supported events. Such events that the rvt may need to know
	 * about include:
	 * port errors
	 * port active
	 * lid change
	 * sm change
	 * client reregister
	 * pkey change
	 *
	 * There may also be other events that the rvt layers needs to know
	 * about this is not an exhaustive list. Some events though rvt does not
	 * need to rely on the driver for such as completion queue error.
	 */
	 int rvt_signal_supported;

	/*
	 * Anything driver specific that is not covered by props
	 * For instance special module parameters. Goes here.
	 */
};

/*
 * Functions that drivers are required to support
 */
struct rvt_driver_provided {
	/*
	 * The work to create port files in /sys/class Infiniband is different
	 * depending on the driver. This should not be extracted away and
	 * instead drivers are responsible for setting the correct callback for
	 * this.
	 */
	int (*port_callback)(struct ib_device *, u8, struct kobject *);
};

/* Protection domain */
struct rvt_pd {
	struct ib_pd ibpd;
	int user;               /* non-zero if created from user space */
};

struct rvt_dev_info {
	/*
	 * Prior to calling for registration the driver will be responsible for
	 * allocating space for this structure.
	 *
	 * The driver will also be responsible for filling in certain members of
	 * dparms.props
	 */
	struct ib_device ibdev;

	/* Driver specific properties */
	struct rvt_driver_params dparms;

	struct rvt_mregion __rcu *dma_mr;
	struct rvt_lkey_table lkey_table;

	/* PKey Table goes here */

	/* Driver specific helper functions */
	struct rvt_driver_provided driver_f;

	/* Internal use */
	int n_pds_allocated;
	spinlock_t n_pds_lock; /* Protect pd allocated count */
};

static inline struct rvt_pd *ibpd_to_rvtpd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct rvt_pd, ibpd);
}

static inline struct rvt_dev_info *ib_to_rvt(struct ib_device *ibdev)
{
	return  container_of(ibdev, struct rvt_dev_info, ibdev);
}

int rvt_register_device(struct rvt_dev_info *rvd);
void rvt_unregister_device(struct rvt_dev_info *rvd);

#endif          /* DEF_RDMA_VT_H */

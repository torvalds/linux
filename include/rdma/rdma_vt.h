#ifndef DEF_RDMA_VT_H
#define DEF_RDMA_VT_H

/*
 * Copyright(c) 2016 Intel Corporation.
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

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdmavt_mr.h>
#include <rdma/rdmavt_qp.h>

#define RVT_MAX_PKEY_VALUES 16

struct rvt_ibport {
	struct rvt_qp __rcu *qp[2];
	struct ib_mad_agent *send_agent;	/* agent for SMI (traps) */
	struct rb_root mcast_tree;
	spinlock_t lock;		/* protect changes in this struct */

	/* non-zero when timer is set */
	unsigned long mkey_lease_timeout;
	unsigned long trap_timeout;
	__be64 gid_prefix;      /* in network order */
	__be64 mkey;
	u64 tid;
	u32 port_cap_flags;
	u32 pma_sample_start;
	u32 pma_sample_interval;
	__be16 pma_counter_select[5];
	u16 pma_tag;
	u16 mkey_lease_period;
	u16 sm_lid;
	u8 sm_sl;
	u8 mkeyprot;
	u8 subnet_timeout;
	u8 vl_high_limit;

	/*
	 * Driver is expected to keep these up to date. These
	 * counters are informational only and not required to be
	 * completely accurate.
	 */
	u64 n_rc_resends;
	u64 n_seq_naks;
	u64 n_rdma_seq;
	u64 n_rnr_naks;
	u64 n_other_naks;
	u64 n_loop_pkts;
	u64 n_pkt_drops;
	u64 n_vl15_dropped;
	u64 n_rc_timeouts;
	u64 n_dmawait;
	u64 n_unaligned;
	u64 n_rc_dupreq;
	u64 n_rc_seqnak;
	u16 pkey_violations;
	u16 qkey_violations;
	u16 mkey_violations;

	/* Hot-path per CPU counters to avoid cacheline trading to update */
	u64 z_rc_acks;
	u64 z_rc_qacks;
	u64 z_rc_delayed_comp;
	u64 __percpu *rc_acks;
	u64 __percpu *rc_qacks;
	u64 __percpu *rc_delayed_comp;

	void *priv; /* driver private data */

	/*
	 * The pkey table is allocated and maintained by the driver. Drivers
	 * need to have access to this before registering with rdmav. However
	 * rdmavt will need access to it so drivers need to proviee this during
	 * the attach port API call.
	 */
	u16 *pkey_table;

	struct rvt_ah *sm_ah;
};

#define RVT_CQN_MAX 16 /* maximum length of cq name */

/*
 * Things that are driver specific, module parameters in hfi1 and qib
 */
struct rvt_driver_params {
	struct ib_device_attr props;

	/*
	 * Anything driver specific that is not covered by props
	 * For instance special module parameters. Goes here.
	 */
	unsigned int lkey_table_size;
	unsigned int qp_table_size;
	int qpn_start;
	int qpn_inc;
	int qpn_res_start;
	int qpn_res_end;
	int nports;
	int npkeys;
	char cq_name[RVT_CQN_MAX];
	int node;
	int psn_mask;
	int psn_shift;
	int psn_modify_mask;
	u32 core_cap_flags;
	u32 max_mad_size;
	u8 qos_shift;
	u8 max_rdma_atomic;
	u8 reserved_operations;
};

/* Protection domain */
struct rvt_pd {
	struct ib_pd ibpd;
	bool user;
};

/* Address handle */
struct rvt_ah {
	struct ib_ah ibah;
	struct rdma_ah_attr attr;
	atomic_t refcount;
	u8 vl;
	u8 log_pmtu;
};

struct rvt_dev_info;
struct rvt_swqe;
struct rvt_driver_provided {
	/*
	 * Which functions are required depends on which verbs rdmavt is
	 * providing and which verbs the driver is overriding. See
	 * check_support() for details.
	 */

	/* hot path calldowns in a single cacheline */

	/*
	 * Give the driver a notice that there is send work to do. It is up to
	 * the driver to generally push the packets out, this just queues the
	 * work with the driver. There are two variants here. The no_lock
	 * version requires the s_lock not to be held. The other assumes the
	 * s_lock is held.
	 */
	void (*schedule_send)(struct rvt_qp *qp);
	void (*schedule_send_no_lock)(struct rvt_qp *qp);

	/* Driver specific work request checking */
	int (*check_send_wqe)(struct rvt_qp *qp, struct rvt_swqe *wqe);

	/*
	 * Sometimes rdmavt needs to kick the driver's send progress. That is
	 * done by this call back.
	 */
	void (*do_send)(struct rvt_qp *qp);

	/* Passed to ib core registration. Callback to create syfs files */
	int (*port_callback)(struct ib_device *, u8, struct kobject *);

	/*
	 * Returns a string to represent the device for which is being
	 * registered. This is primarily used for error and debug messages on
	 * the console.
	 */
	const char * (*get_card_name)(struct rvt_dev_info *rdi);

	/*
	 * Returns a pointer to the undelying hardware's PCI device. This is
	 * used to display information as to what hardware is being referenced
	 * in an output message
	 */
	struct pci_dev * (*get_pci_dev)(struct rvt_dev_info *rdi);

	/*
	 * Allocate a private queue pair data structure for driver specific
	 * information which is opaque to rdmavt.  Errors are returned via
	 * ERR_PTR(err).  The driver is free to return NULL or a valid
	 * pointer.
	 */
	void * (*qp_priv_alloc)(struct rvt_dev_info *rdi, struct rvt_qp *qp,
				gfp_t gfp);

	/*
	 * Free the driver's private qp structure.
	 */
	void (*qp_priv_free)(struct rvt_dev_info *rdi, struct rvt_qp *qp);

	/*
	 * Inform the driver the particular qp in quesiton has been reset so
	 * that it can clean up anything it needs to.
	 */
	void (*notify_qp_reset)(struct rvt_qp *qp);

	/*
	 * Get a path mtu from the driver based on qp attributes.
	 */
	int (*get_pmtu_from_attr)(struct rvt_dev_info *rdi, struct rvt_qp *qp,
				  struct ib_qp_attr *attr);

	/*
	 * Notify driver that it needs to flush any outstanding IO requests that
	 * are waiting on a qp.
	 */
	void (*flush_qp_waiters)(struct rvt_qp *qp);

	/*
	 * Notify driver to stop its queue of sending packets. Nothing else
	 * should be posted to the queue pair after this has been called.
	 */
	void (*stop_send_queue)(struct rvt_qp *qp);

	/*
	 * Have the drivr drain any in progress operations
	 */
	void (*quiesce_qp)(struct rvt_qp *qp);

	/*
	 * Inform the driver a qp has went to error state.
	 */
	void (*notify_error_qp)(struct rvt_qp *qp);

	/*
	 * Get an MTU for a qp.
	 */
	u32 (*mtu_from_qp)(struct rvt_dev_info *rdi, struct rvt_qp *qp,
			   u32 pmtu);
	/*
	 * Convert an mtu to a path mtu
	 */
	int (*mtu_to_path_mtu)(u32 mtu);

	/*
	 * Get the guid of a port in big endian byte order
	 */
	int (*get_guid_be)(struct rvt_dev_info *rdi, struct rvt_ibport *rvp,
			   int guid_index, __be64 *guid);

	/*
	 * Query driver for the state of the port.
	 */
	int (*query_port_state)(struct rvt_dev_info *rdi, u8 port_num,
				struct ib_port_attr *props);

	/*
	 * Tell driver to shutdown a port
	 */
	int (*shut_down_port)(struct rvt_dev_info *rdi, u8 port_num);

	/* Tell driver to send a trap for changed  port capabilities */
	void (*cap_mask_chg)(struct rvt_dev_info *rdi, u8 port_num);

	/*
	 * The following functions can be safely ignored completely. Any use of
	 * these is checked for NULL before blindly calling. Rdmavt should also
	 * be functional if drivers omit these.
	 */

	/* Called to inform the driver that all qps should now be freed. */
	unsigned (*free_all_qps)(struct rvt_dev_info *rdi);

	/* Driver specific AH validation */
	int (*check_ah)(struct ib_device *, struct rdma_ah_attr *);

	/* Inform the driver a new AH has been created */
	void (*notify_new_ah)(struct ib_device *, struct rdma_ah_attr *,
			      struct rvt_ah *);

	/* Let the driver pick the next queue pair number*/
	int (*alloc_qpn)(struct rvt_dev_info *rdi, struct rvt_qpn_table *qpt,
			 enum ib_qp_type type, u8 port_num, gfp_t gfp);

	/* Determine if its safe or allowed to modify the qp */
	int (*check_modify_qp)(struct rvt_qp *qp, struct ib_qp_attr *attr,
			       int attr_mask, struct ib_udata *udata);

	/* Driver specific QP modification/notification-of */
	void (*modify_qp)(struct rvt_qp *qp, struct ib_qp_attr *attr,
			  int attr_mask, struct ib_udata *udata);

	/* Notify driver a mad agent has been created */
	void (*notify_create_mad_agent)(struct rvt_dev_info *rdi, int port_idx);

	/* Notify driver a mad agent has been removed */
	void (*notify_free_mad_agent)(struct rvt_dev_info *rdi, int port_idx);

	/* Notify driver to restart rc */
	void (*notify_restart_rc)(struct rvt_qp *qp, u32 psn, int wait);
};

struct rvt_dev_info {
	struct ib_device ibdev; /* Keep this first. Nothing above here */

	/*
	 * Prior to calling for registration the driver will be responsible for
	 * allocating space for this structure.
	 *
	 * The driver will also be responsible for filling in certain members of
	 * dparms.props. The driver needs to fill in dparms exactly as it would
	 * want values reported to a ULP. This will be returned to the caller
	 * in rdmavt's device. The driver should also therefore refrain from
	 * modifying this directly after registration with rdmavt.
	 */

	/* Driver specific properties */
	struct rvt_driver_params dparms;

	/* post send table */
	const struct rvt_operation_params *post_parms;

	/* Driver specific helper functions */
	struct rvt_driver_provided driver_f;

	struct rvt_mregion __rcu *dma_mr;
	struct rvt_lkey_table lkey_table;

	/* Internal use */
	int n_pds_allocated;
	spinlock_t n_pds_lock; /* Protect pd allocated count */

	int n_ahs_allocated;
	spinlock_t n_ahs_lock; /* Protect ah allocated count */

	u32 n_srqs_allocated;
	spinlock_t n_srqs_lock; /* Protect srqs allocated count */

	int flags;
	struct rvt_ibport **ports;

	/* QP */
	struct rvt_qp_ibdev *qp_dev;
	u32 n_qps_allocated;    /* number of QPs allocated for device */
	u32 n_rc_qps;		/* number of RC QPs allocated for device */
	u32 busy_jiffies;	/* timeout scaling based on RC QP count */
	spinlock_t n_qps_lock;	/* protect qps, rc qps and busy jiffy counts */

	/* memory maps */
	struct list_head pending_mmaps;
	spinlock_t mmap_offset_lock; /* protect mmap_offset */
	u32 mmap_offset;
	spinlock_t pending_lock; /* protect pending mmap list */

	/* CQ */
	struct kthread_worker *worker; /* per device cq worker */
	u32 n_cqs_allocated;    /* number of CQs allocated for device */
	spinlock_t n_cqs_lock; /* protect count of in use cqs */

	/* Multicast */
	u32 n_mcast_grps_allocated; /* number of mcast groups allocated */
	spinlock_t n_mcast_grps_lock;

};

static inline struct rvt_pd *ibpd_to_rvtpd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct rvt_pd, ibpd);
}

static inline struct rvt_ah *ibah_to_rvtah(struct ib_ah *ibah)
{
	return container_of(ibah, struct rvt_ah, ibah);
}

static inline struct rvt_dev_info *ib_to_rvt(struct ib_device *ibdev)
{
	return  container_of(ibdev, struct rvt_dev_info, ibdev);
}

static inline struct rvt_srq *ibsrq_to_rvtsrq(struct ib_srq *ibsrq)
{
	return container_of(ibsrq, struct rvt_srq, ibsrq);
}

static inline struct rvt_qp *ibqp_to_rvtqp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct rvt_qp, ibqp);
}

static inline unsigned rvt_get_npkeys(struct rvt_dev_info *rdi)
{
	/*
	 * All ports have same number of pkeys.
	 */
	return rdi->dparms.npkeys;
}

/*
 * Return the max atomic suitable for determining
 * the size of the ack ring buffer in a QP.
 */
static inline unsigned int rvt_max_atomic(struct rvt_dev_info *rdi)
{
	return rdi->dparms.max_rdma_atomic + 1;
}

/*
 * Return the indexed PKEY from the port PKEY table.
 */
static inline u16 rvt_get_pkey(struct rvt_dev_info *rdi,
			       int port_index,
			       unsigned index)
{
	if (index >= rvt_get_npkeys(rdi))
		return 0;
	else
		return rdi->ports[port_index]->pkey_table[index];
}

/**
 * rvt_lookup_qpn - return the QP with the given QPN
 * @ibp: the ibport
 * @qpn: the QP number to look up
 *
 * The caller must hold the rcu_read_lock(), and keep the lock until
 * the returned qp is no longer in use.
 */
/* TODO: Remove this and put in rdmavt/qp.h when no longer needed by drivers */
static inline struct rvt_qp *rvt_lookup_qpn(struct rvt_dev_info *rdi,
					    struct rvt_ibport *rvp,
					    u32 qpn) __must_hold(RCU)
{
	struct rvt_qp *qp = NULL;

	if (unlikely(qpn <= 1)) {
		qp = rcu_dereference(rvp->qp[qpn]);
	} else {
		u32 n = hash_32(qpn, rdi->qp_dev->qp_table_bits);

		for (qp = rcu_dereference(rdi->qp_dev->qp_table[n]); qp;
			qp = rcu_dereference(qp->next))
			if (qp->ibqp.qp_num == qpn)
				break;
	}
	return qp;
}

/**
 * rvt_mod_retry_timer - mod a retry timer
 * @qp - the QP
 * Modify a potentially already running retry timer
 */
static inline void rvt_mod_retry_timer(struct rvt_qp *qp)
{
	struct ib_qp *ibqp = &qp->ibqp;
	struct rvt_dev_info *rdi = ib_to_rvt(ibqp->device);

	lockdep_assert_held(&qp->s_lock);
	qp->s_flags |= RVT_S_TIMER;
	/* 4.096 usec. * (1 << qp->timeout) */
	mod_timer(&qp->s_timer, jiffies + qp->timeout_jiffies +
		  rdi->busy_jiffies);
}

struct rvt_dev_info *rvt_alloc_device(size_t size, int nports);
void rvt_dealloc_device(struct rvt_dev_info *rdi);
int rvt_register_device(struct rvt_dev_info *rvd);
void rvt_unregister_device(struct rvt_dev_info *rvd);
int rvt_check_ah(struct ib_device *ibdev, struct rdma_ah_attr *ah_attr);
int rvt_init_port(struct rvt_dev_info *rdi, struct rvt_ibport *port,
		  int port_index, u16 *pkey_table);
int rvt_fast_reg_mr(struct rvt_qp *qp, struct ib_mr *ibmr, u32 key,
		    int access);
int rvt_invalidate_rkey(struct rvt_qp *qp, u32 rkey);
int rvt_rkey_ok(struct rvt_qp *qp, struct rvt_sge *sge,
		u32 len, u64 vaddr, u32 rkey, int acc);
int rvt_lkey_ok(struct rvt_lkey_table *rkt, struct rvt_pd *pd,
		struct rvt_sge *isge, struct ib_sge *sge, int acc);
struct rvt_mcast *rvt_mcast_find(struct rvt_ibport *ibp, union ib_gid *mgid,
				 u16 lid);

#endif          /* DEF_RDMA_VT_H */

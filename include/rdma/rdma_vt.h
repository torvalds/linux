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

#include <linux/spinlock.h>
#include <linux/list.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdmavt_mr.h>
#include <rdma/rdmavt_qp.h>

/*
 * For some of the IBTA objects there will likely be some
 * initializations required. We need flags to determine whether it is OK
 * for rdmavt to do this or not. This does not imply any functions of a
 * partiuclar IBTA object are overridden.
 */
#define RVT_FLAG_MR_INIT_DRIVER BIT(1)
#define RVT_FLAG_QP_INIT_DRIVER BIT(2)
#define RVT_FLAG_CQ_INIT_DRIVER BIT(3)

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

	/* TODO: Move sm_ah and smi_ah into here as well*/
};

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
	unsigned int lkey_table_size;
	int nports;
};

/* Protection domain */
struct rvt_pd {
	struct ib_pd ibpd;
	int user;               /* non-zero if created from user space */
};

/* Address handle */
struct rvt_ah {
	struct ib_ah ibah;
	struct ib_ah_attr attr;
	atomic_t refcount;
	u8 vl;
	u8 log_pmtu;
};

struct rvt_dev_info;
struct rvt_driver_provided {
	/*
	 * The work to create port files in /sys/class Infiniband is different
	 * depending on the driver. This should not be extracted away and
	 * instead drivers are responsible for setting the correct callback for
	 * this.
	 */

	/* -------------------*/
	/* Required functions */
	/* -------------------*/
	int (*port_callback)(struct ib_device *, u8, struct kobject *);
	const char * (*get_card_name)(struct rvt_dev_info *rdi);
	struct pci_dev * (*get_pci_dev)(struct rvt_dev_info *rdi);

	/*--------------------*/
	/* Optional functions */
	/*--------------------*/
	int (*check_ah)(struct ib_device *, struct ib_ah_attr *);
	void (*notify_new_ah)(struct ib_device *, struct ib_ah_attr *,
			      struct rvt_ah *);
};

struct rvt_dev_info {
	struct ib_device ibdev; /* Keep this first. Nothing above here */

	/*
	 * Prior to calling for registration the driver will be responsible for
	 * allocating space for this structure.
	 *
	 * The driver will also be responsible for filling in certain members of
	 * dparms.props
	 */

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

	int n_ahs_allocated;
	spinlock_t n_ahs_lock; /* Protect ah allocated count */

	int flags;
	struct rvt_ibport **ports;
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

int rvt_register_device(struct rvt_dev_info *rvd);
void rvt_unregister_device(struct rvt_dev_info *rvd);
int rvt_check_ah(struct ib_device *ibdev, struct ib_ah_attr *ah_attr);
void rvt_attach_port(struct rvt_dev_info *rdi, struct rvt_ibport *port,
		     int portnum);
int rvt_rkey_ok(struct rvt_qp *qp, struct rvt_sge *sge,
		u32 len, u64 vaddr, u32 rkey, int acc);
int rvt_lkey_ok(struct rvt_lkey_table *rkt, struct rvt_pd *pd,
		struct rvt_sge *isge, struct ib_sge *sge, int acc);
#endif          /* DEF_RDMA_VT_H */

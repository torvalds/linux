/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NETNS_XFRM_H
#define __NETNS_XFRM_H

#include <linux/list.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/rhashtable-types.h>
#include <linux/xfrm.h>
#include <net/dst_ops.h>

struct ctl_table_header;

struct xfrm_policy_hash {
	struct hlist_head	__rcu *table;
	unsigned int		hmask;
	u8			dbits4;
	u8			sbits4;
	u8			dbits6;
	u8			sbits6;
};

struct xfrm_policy_hthresh {
	struct work_struct	work;
	seqlock_t		lock;
	u8			lbits4;
	u8			rbits4;
	u8			lbits6;
	u8			rbits6;
};

struct netns_xfrm {
	struct list_head	state_all;
	/*
	 * Hash table to find appropriate SA towards given target (endpoint of
	 * tunnel or destination of transport mode) allowed by selector.
	 *
	 * Main use is finding SA after policy selected tunnel or transport
	 * mode. Also, it can be used by ah/esp icmp error handler to find
	 * offending SA.
	 */
	struct hlist_head	__rcu *state_bydst;
	struct hlist_head	__rcu *state_bysrc;
	struct hlist_head	__rcu *state_byspi;
	struct hlist_head	__rcu *state_byseq;
	unsigned int		state_hmask;
	unsigned int		state_num;
	struct work_struct	state_hash_work;

	struct list_head	policy_all;
	struct hlist_head	*policy_byidx;
	unsigned int		policy_idx_hmask;
	struct hlist_head	policy_inexact[XFRM_POLICY_MAX];
	struct xfrm_policy_hash	policy_bydst[XFRM_POLICY_MAX];
	unsigned int		policy_count[XFRM_POLICY_MAX * 2];
	struct work_struct	policy_hash_work;
	struct xfrm_policy_hthresh policy_hthresh;
	struct list_head	inexact_bins;


	struct sock		*nlsk;
	struct sock		*nlsk_stash;

	u32			sysctl_aevent_etime;
	u32			sysctl_aevent_rseqth;
	int			sysctl_larval_drop;
	u32			sysctl_acq_expires;

	u8			policy_default[XFRM_POLICY_MAX];

#ifdef CONFIG_SYSCTL
	struct ctl_table_header	*sysctl_hdr;
#endif

	struct dst_ops		xfrm4_dst_ops;
#if IS_ENABLED(CONFIG_IPV6)
	struct dst_ops		xfrm6_dst_ops;
#endif
	spinlock_t		xfrm_state_lock;
	seqcount_spinlock_t	xfrm_state_hash_generation;
	seqcount_spinlock_t	xfrm_policy_hash_generation;

	spinlock_t xfrm_policy_lock;
	struct mutex xfrm_cfg_mutex;
};

#endif

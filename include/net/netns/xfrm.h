#ifndef __NETNS_XFRM_H
#define __NETNS_XFRM_H

#include <linux/list.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/xfrm.h>
#include <net/dst_ops.h>

struct ctl_table_header;

struct xfrm_policy_hash {
	struct hlist_head	*table;
	unsigned int		hmask;
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
	struct hlist_head	*state_bydst;
	struct hlist_head	*state_bysrc;
	struct hlist_head	*state_byspi;
	unsigned int		state_hmask;
	unsigned int		state_num;
	struct work_struct	state_hash_work;
	struct hlist_head	state_gc_list;
	struct work_struct	state_gc_work;

	wait_queue_head_t	km_waitq;

	struct list_head	policy_all;
	struct hlist_head	*policy_byidx;
	unsigned int		policy_idx_hmask;
	struct hlist_head	policy_inexact[XFRM_POLICY_MAX * 2];
	struct xfrm_policy_hash	policy_bydst[XFRM_POLICY_MAX * 2];
	unsigned int		policy_count[XFRM_POLICY_MAX * 2];
	struct work_struct	policy_hash_work;

	struct dst_ops		xfrm4_dst_ops;
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	struct dst_ops		xfrm6_dst_ops;
#endif

	struct sock		*nlsk;
	struct sock		*nlsk_stash;

	u32			sysctl_aevent_etime;
	u32			sysctl_aevent_rseqth;
	int			sysctl_larval_drop;
	u32			sysctl_acq_expires;
#ifdef CONFIG_SYSCTL
	struct ctl_table_header	*sysctl_hdr;
#endif
};

#endif

/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2019 Netronome Systems, Inc. */

#ifndef __NET_TC_MPLS_H
#define __NET_TC_MPLS_H

#include <linux/tc_act/tc_mpls.h>
#include <net/act_api.h>

struct tcf_mpls_params {
	int tcfm_action;
	u32 tcfm_label;
	int action; /* tcf_action */
	u8 tcfm_tc;
	u8 tcfm_ttl;
	u8 tcfm_bos;
	__be16 tcfm_proto;
	struct rcu_head	rcu;
};

#define ACT_MPLS_TC_NOT_SET	0xff
#define ACT_MPLS_BOS_NOT_SET	0xff
#define ACT_MPLS_LABEL_NOT_SET	0xffffffff

struct tcf_mpls {
	struct tc_action common;
	struct tcf_mpls_params __rcu *mpls_p;
};
#define to_mpls(a) ((struct tcf_mpls *)a)

static inline u32 tcf_mpls_action(const struct tc_action *a)
{
	u32 tcfm_action;

	rcu_read_lock();
	tcfm_action = rcu_dereference(to_mpls(a)->mpls_p)->tcfm_action;
	rcu_read_unlock();

	return tcfm_action;
}

static inline __be16 tcf_mpls_proto(const struct tc_action *a)
{
	__be16 tcfm_proto;

	rcu_read_lock();
	tcfm_proto = rcu_dereference(to_mpls(a)->mpls_p)->tcfm_proto;
	rcu_read_unlock();

	return tcfm_proto;
}

static inline u32 tcf_mpls_label(const struct tc_action *a)
{
	u32 tcfm_label;

	rcu_read_lock();
	tcfm_label = rcu_dereference(to_mpls(a)->mpls_p)->tcfm_label;
	rcu_read_unlock();

	return tcfm_label;
}

static inline u8 tcf_mpls_tc(const struct tc_action *a)
{
	u8 tcfm_tc;

	rcu_read_lock();
	tcfm_tc = rcu_dereference(to_mpls(a)->mpls_p)->tcfm_tc;
	rcu_read_unlock();

	return tcfm_tc;
}

static inline u8 tcf_mpls_bos(const struct tc_action *a)
{
	u8 tcfm_bos;

	rcu_read_lock();
	tcfm_bos = rcu_dereference(to_mpls(a)->mpls_p)->tcfm_bos;
	rcu_read_unlock();

	return tcfm_bos;
}

static inline u8 tcf_mpls_ttl(const struct tc_action *a)
{
	u8 tcfm_ttl;

	rcu_read_lock();
	tcfm_ttl = rcu_dereference(to_mpls(a)->mpls_p)->tcfm_ttl;
	rcu_read_unlock();

	return tcfm_ttl;
}

#endif /* __NET_TC_MPLS_H */

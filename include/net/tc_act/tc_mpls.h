/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2019 Netronome Systems, Inc. */

#ifndef __NET_TC_MPLS_H
#define __NET_TC_MPLS_H

#include <linux/tc_act/tc_mpls.h>
#include <net/act_api.h>

struct tcf_mpls_params {
	int tcfm_action;
	u32 tcfm_label;
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

#endif /* __NET_TC_MPLS_H */

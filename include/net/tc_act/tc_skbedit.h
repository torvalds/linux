/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2008, Intel Corporation.
 *
 * Author: Alexander Duyck <alexander.h.duyck@intel.com>
 */

#ifndef __NET_TC_SKBEDIT_H
#define __NET_TC_SKBEDIT_H

#include <net/act_api.h>
#include <linux/tc_act/tc_skbedit.h>

struct tcf_skbedit_params {
	u32 flags;
	u32 priority;
	u32 mark;
	u32 mask;
	u16 queue_mapping;
	u16 ptype;
	struct rcu_head rcu;
};

struct tcf_skbedit {
	struct tc_action common;
	struct tcf_skbedit_params __rcu *params;
};
#define to_skbedit(a) ((struct tcf_skbedit *)a)

/* Return true iff action is mark */
static inline bool is_tcf_skbedit_mark(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	u32 flags;

	if (a->ops && a->ops->id == TCA_ID_SKBEDIT) {
		rcu_read_lock();
		flags = rcu_dereference(to_skbedit(a)->params)->flags;
		rcu_read_unlock();
		return flags == SKBEDIT_F_MARK;
	}
#endif
	return false;
}

static inline u32 tcf_skbedit_mark(const struct tc_action *a)
{
	u32 mark;

	rcu_read_lock();
	mark = rcu_dereference(to_skbedit(a)->params)->mark;
	rcu_read_unlock();

	return mark;
}

#endif /* __NET_TC_SKBEDIT_H */

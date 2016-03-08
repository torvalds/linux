/*
 * Copyright (c) 2008, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Alexander Duyck <alexander.h.duyck@intel.com>
 */

#ifndef __NET_TC_SKBEDIT_H
#define __NET_TC_SKBEDIT_H

#include <net/act_api.h>
#include <linux/tc_act/tc_skbedit.h>

struct tcf_skbedit {
	struct tcf_common	common;
	u32			flags;
	u32     		priority;
	u32     		mark;
	u16			queue_mapping;
	/* XXX: 16-bit pad here? */
};
#define to_skbedit(a) \
	container_of(a->priv, struct tcf_skbedit, common)

/* Return true iff action is mark */
static inline bool is_tcf_skbedit_mark(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	if (a->ops && a->ops->type == TCA_ACT_SKBEDIT)
		return to_skbedit(a)->flags == SKBEDIT_F_MARK;
#endif
	return false;
}

static inline u32 tcf_skbedit_mark(const struct tc_action *a)
{
	return to_skbedit(a)->mark;
}

#endif /* __NET_TC_SKBEDIT_H */

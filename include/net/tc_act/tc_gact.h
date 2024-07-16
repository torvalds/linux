/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_TC_GACT_H
#define __NET_TC_GACT_H

#include <net/act_api.h>
#include <linux/tc_act/tc_gact.h>

struct tcf_gact {
	struct tc_action	common;
#ifdef CONFIG_GACT_PROB
	u16			tcfg_ptype;
	u16			tcfg_pval;
	int			tcfg_paction;
	atomic_t		packets;
#endif
};
#define to_gact(a) ((struct tcf_gact *)a)

static inline bool __is_tcf_gact_act(const struct tc_action *a, int act,
				     bool is_ext)
{
#ifdef CONFIG_NET_CLS_ACT
	struct tcf_gact *gact;

	if (a->ops && a->ops->id != TCA_ID_GACT)
		return false;

	gact = to_gact(a);
	if ((!is_ext && gact->tcf_action == act) ||
	    (is_ext && TC_ACT_EXT_CMP(gact->tcf_action, act)))
		return true;

#endif
	return false;
}

static inline bool is_tcf_gact_ok(const struct tc_action *a)
{
	return __is_tcf_gact_act(a, TC_ACT_OK, false);
}

static inline bool is_tcf_gact_shot(const struct tc_action *a)
{
	return __is_tcf_gact_act(a, TC_ACT_SHOT, false);
}

static inline bool is_tcf_gact_trap(const struct tc_action *a)
{
	return __is_tcf_gact_act(a, TC_ACT_TRAP, false);
}

static inline bool is_tcf_gact_goto_chain(const struct tc_action *a)
{
	return __is_tcf_gact_act(a, TC_ACT_GOTO_CHAIN, true);
}

static inline u32 tcf_gact_goto_chain_index(const struct tc_action *a)
{
	return READ_ONCE(a->tcfa_action) & TC_ACT_EXT_VAL_MASK;
}

static inline bool is_tcf_gact_continue(const struct tc_action *a)
{
	return __is_tcf_gact_act(a, TC_ACT_UNSPEC, false);
}

static inline bool is_tcf_gact_reclassify(const struct tc_action *a)
{
	return __is_tcf_gact_act(a, TC_ACT_RECLASSIFY, false);
}

static inline bool is_tcf_gact_pipe(const struct tc_action *a)
{
	return __is_tcf_gact_act(a, TC_ACT_PIPE, false);
}

#endif /* __NET_TC_GACT_H */

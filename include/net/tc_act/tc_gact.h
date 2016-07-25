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

static inline bool is_tcf_gact_shot(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	struct tcf_gact *gact;

	if (a->ops && a->ops->type != TCA_ACT_GACT)
		return false;

	gact = to_gact(a);
	if (gact->tcf_action == TC_ACT_SHOT)
		return true;

#endif
	return false;
}
#endif /* __NET_TC_GACT_H */

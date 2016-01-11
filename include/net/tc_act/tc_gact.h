#ifndef __NET_TC_GACT_H
#define __NET_TC_GACT_H

#include <net/act_api.h>

struct tcf_gact {
	struct tcf_common	common;
#ifdef CONFIG_GACT_PROB
	u16			tcfg_ptype;
	u16			tcfg_pval;
	int			tcfg_paction;
	atomic_t		packets;
#endif
};
#define to_gact(a) \
	container_of(a->priv, struct tcf_gact, common)

#endif /* __NET_TC_GACT_H */

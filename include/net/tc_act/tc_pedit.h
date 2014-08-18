#ifndef __NET_TC_PED_H
#define __NET_TC_PED_H

#include <net/act_api.h>

struct tcf_pedit {
	struct tcf_common	common;
	unsigned char		tcfp_nkeys;
	unsigned char		tcfp_flags;
	struct tc_pedit_key	*tcfp_keys;
};
#define to_pedit(a) \
	container_of(a->priv, struct tcf_pedit, common)

#endif /* __NET_TC_PED_H */

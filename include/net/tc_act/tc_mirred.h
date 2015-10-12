#ifndef __NET_TC_MIR_H
#define __NET_TC_MIR_H

#include <net/act_api.h>

struct tcf_mirred {
	struct tcf_common	common;
	int			tcfm_eaction;
	int			tcfm_ifindex;
	int			tcfm_ok_push;
	struct net_device __rcu	*tcfm_dev;
	struct list_head	tcfm_list;
};
#define to_mirred(a) \
	container_of(a->priv, struct tcf_mirred, common)

#endif /* __NET_TC_MIR_H */

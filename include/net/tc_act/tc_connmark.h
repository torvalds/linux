#ifndef __NET_TC_CONNMARK_H
#define __NET_TC_CONNMARK_H

#include <net/act_api.h>

struct tcf_connmark_info {
	struct tcf_common common;
	struct net *net;
	u16 zone;
};

#define to_connmark(a) \
	container_of(a->priv, struct tcf_connmark_info, common)

#endif /* __NET_TC_CONNMARK_H */

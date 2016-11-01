#ifndef __NET_TC_CONNMARK_H
#define __NET_TC_CONNMARK_H

#include <net/act_api.h>

struct tcf_connmark_info {
	struct tc_action common;
	struct net *net;
	u16 zone;
};

#define to_connmark(a) ((struct tcf_connmark_info *)a)

#endif /* __NET_TC_CONNMARK_H */

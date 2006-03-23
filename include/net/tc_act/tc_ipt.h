#ifndef __NET_TC_IPT_H
#define __NET_TC_IPT_H

#include <net/act_api.h>

struct xt_entry_target;

struct tcf_ipt
{
	tca_gen(ipt);
	u32 hook;
	char *tname;
	struct xt_entry_target *t;
};

#endif

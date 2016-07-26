#ifndef __NET_TC_CSUM_H
#define __NET_TC_CSUM_H

#include <linux/types.h>
#include <net/act_api.h>

struct tcf_csum {
	struct tc_action common;

	u32 update_flags;
};
#define to_tcf_csum(a) ((struct tcf_csum *)a)

#endif /* __NET_TC_CSUM_H */

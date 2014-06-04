#ifndef __NET_TC_CSUM_H
#define __NET_TC_CSUM_H

#include <linux/types.h>
#include <net/act_api.h>

struct tcf_csum {
	struct tcf_common common;

	u32 update_flags;
};
#define to_tcf_csum(a) \
	container_of(a->priv,struct tcf_csum,common)

#endif /* __NET_TC_CSUM_H */

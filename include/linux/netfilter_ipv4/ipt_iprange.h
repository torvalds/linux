#ifndef _IPT_IPRANGE_H
#define _IPT_IPRANGE_H

#include <linux/types.h>
#include <linux/netfilter/xt_iprange.h>

struct ipt_iprange {
	/* Inclusive: network order. */
	__be32 min_ip, max_ip;
};

struct ipt_iprange_info
{
	struct ipt_iprange src;
	struct ipt_iprange dst;

	/* Flags from above */
	u_int8_t flags;
};

#endif /* _IPT_IPRANGE_H */

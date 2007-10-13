#ifndef _IPT_IPRANGE_H
#define _IPT_IPRANGE_H

#include <linux/types.h>

#define IPRANGE_SRC		0x01	/* Match source IP address */
#define IPRANGE_DST		0x02	/* Match destination IP address */
#define IPRANGE_SRC_INV		0x10	/* Negate the condition */
#define IPRANGE_DST_INV		0x20	/* Negate the condition */

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

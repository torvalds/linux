#ifndef _IPT_SAME_H
#define _IPT_SAME_H

#include <linux/types.h>

#define IPT_SAME_MAX_RANGE	10

#define IPT_SAME_NODST		0x01

struct ipt_same_info {
	unsigned char info;
	__u32 rangesize;
	__u32 ipnum;
	__u32 *iparray;

	/* hangs off end. */
	struct nf_nat_range range[IPT_SAME_MAX_RANGE];
};

#endif /*_IPT_SAME_H*/

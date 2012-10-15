#ifndef __IP_SET_BITMAP_H
#define __IP_SET_BITMAP_H

#include <uapi/linux/netfilter/ipset/ip_set_bitmap.h>

#define IPSET_BITMAP_MAX_RANGE	0x0000FFFF

/* Common functions */

static inline u32
range_to_mask(u32 from, u32 to, u8 *bits)
{
	u32 mask = 0xFFFFFFFE;

	*bits = 32;
	while (--(*bits) > 0 && mask && (to & mask) != from)
		mask <<= 1;

	return mask;
}

#endif /* __IP_SET_BITMAP_H */

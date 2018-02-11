/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_NS_HASH_H__
#define __NET_NS_HASH_H__

#include <asm/cache.h>

struct net;

static inline u32 net_hash_mix(const struct net *net)
{
#ifdef CONFIG_NET_NS
	/*
	 * shift this right to eliminate bits, that are
	 * always zeroed
	 */

	return (u32)(((unsigned long)net) >> L1_CACHE_SHIFT);
#else
	return 0;
#endif
}
#endif

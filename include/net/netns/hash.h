/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_NS_HASH_H__
#define __NET_NS_HASH_H__

#include <asm/cache.h>

struct net;

static inline u32 net_hash_mix(const struct net *net)
{
#ifdef CONFIG_NET_NS
	return (u32)(((unsigned long)net) >> ilog2(sizeof(*net)));
#else
	return 0;
#endif
}
#endif

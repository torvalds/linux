#ifndef _NET_DST_CACHE_H
#define _NET_DST_CACHE_H

#include <linux/jiffies.h>
#include <net/dst.h>
#if IS_ENABLED(CONFIG_IPV6)
#include <net/ip6_fib.h>
#endif

struct dst_cache {
	struct dst_cache_pcpu __percpu *cache;
	unsigned long reset_ts;
};

/**
 *	dst_cache_get - perform cache lookup
 *	@dst_cache: the cache
 *
 *	The caller should use dst_cache_get_ip4() if it need to retrieve the
 *	source address to be used when xmitting to the cached dst.
 *	local BH must be disabled.
 */
struct dst_entry *dst_cache_get(struct dst_cache *dst_cache);

/**
 *	dst_cache_get_ip4 - perform cache lookup and fetch ipv4 source address
 *	@dst_cache: the cache
 *	@saddr: return value for the retrieved source address
 *
 *	local BH must be disabled.
 */
struct rtable *dst_cache_get_ip4(struct dst_cache *dst_cache, __be32 *saddr);

/**
 *	dst_cache_set_ip4 - store the ipv4 dst into the cache
 *	@dst_cache: the cache
 *	@dst: the entry to be cached
 *	@saddr: the source address to be stored inside the cache
 *
 *	local BH must be disabled.
 */
void dst_cache_set_ip4(struct dst_cache *dst_cache, struct dst_entry *dst,
		       __be32 saddr);

#if IS_ENABLED(CONFIG_IPV6)

/**
 *	dst_cache_set_ip6 - store the ipv6 dst into the cache
 *	@dst_cache: the cache
 *	@dst: the entry to be cached
 *	@saddr: the source address to be stored inside the cache
 *
 *	local BH must be disabled.
 */
void dst_cache_set_ip6(struct dst_cache *dst_cache, struct dst_entry *dst,
		       const struct in6_addr *addr);

/**
 *	dst_cache_get_ip6 - perform cache lookup and fetch ipv6 source address
 *	@dst_cache: the cache
 *	@saddr: return value for the retrieved source address
 *
 *	local BH must be disabled.
 */
struct dst_entry *dst_cache_get_ip6(struct dst_cache *dst_cache,
				    struct in6_addr *saddr);
#endif

/**
 *	dst_cache_reset - invalidate the cache contents
 *	@dst_cache: the cache
 *
 *	This do not free the cached dst to avoid races and contentions.
 *	the dst will be freed on later cache lookup.
 */
static inline void dst_cache_reset(struct dst_cache *dst_cache)
{
	dst_cache->reset_ts = jiffies;
}

/**
 *	dst_cache_init - initialize the cache, allocating the required storage
 *	@dst_cache: the cache
 *	@gfp: allocation flags
 */
int dst_cache_init(struct dst_cache *dst_cache, gfp_t gfp);

/**
 *	dst_cache_destroy - empty the cache and free the allocated storage
 *	@dst_cache: the cache
 *
 *	No synchronization is enforced: it must be called only when the cache
 *	is unsed.
 */
void dst_cache_destroy(struct dst_cache *dst_cache);

#endif

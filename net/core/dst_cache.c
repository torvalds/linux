// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/core/dst_cache.c - dst entry cache
 *
 * Copyright (c) 2016 Paolo Abeni <pabeni@redhat.com>
 */

#include <linux/kernel.h>
#include <linux/percpu.h>
#include <net/dst_cache.h>
#include <net/route.h>
#if IS_ENABLED(CONFIG_IPV6)
#include <net/ip6_fib.h>
#endif
#include <uapi/linux/in.h>

struct dst_cache_pcpu {
	unsigned long refresh_ts;
	struct dst_entry *dst;
	u32 cookie;
	union {
		struct in_addr in_saddr;
		struct in6_addr in6_saddr;
	};
};

static void dst_cache_per_cpu_dst_set(struct dst_cache_pcpu *dst_cache,
				      struct dst_entry *dst, u32 cookie)
{
	DEBUG_NET_WARN_ON_ONCE(!in_softirq());
	dst_release(dst_cache->dst);
	if (dst)
		dst_hold(dst);

	dst_cache->cookie = cookie;
	dst_cache->dst = dst;
}

static struct dst_entry *dst_cache_per_cpu_get(struct dst_cache *dst_cache,
					       struct dst_cache_pcpu *idst)
{
	struct dst_entry *dst;

	DEBUG_NET_WARN_ON_ONCE(!in_softirq());
	dst = idst->dst;
	if (!dst)
		goto fail;

	/* the cache already hold a dst reference; it can't go away */
	dst_hold(dst);

	if (unlikely(!time_after(idst->refresh_ts,
				 READ_ONCE(dst_cache->reset_ts)) ||
		     (dst->obsolete && !dst->ops->check(dst, idst->cookie)))) {
		dst_cache_per_cpu_dst_set(idst, NULL, 0);
		dst_release(dst);
		goto fail;
	}
	return dst;

fail:
	idst->refresh_ts = jiffies;
	return NULL;
}

struct dst_entry *dst_cache_get(struct dst_cache *dst_cache)
{
	if (!dst_cache->cache)
		return NULL;

	return dst_cache_per_cpu_get(dst_cache, this_cpu_ptr(dst_cache->cache));
}
EXPORT_SYMBOL_GPL(dst_cache_get);

struct rtable *dst_cache_get_ip4(struct dst_cache *dst_cache, __be32 *saddr)
{
	struct dst_cache_pcpu *idst;
	struct dst_entry *dst;

	if (!dst_cache->cache)
		return NULL;

	idst = this_cpu_ptr(dst_cache->cache);
	dst = dst_cache_per_cpu_get(dst_cache, idst);
	if (!dst)
		return NULL;

	*saddr = idst->in_saddr.s_addr;
	return dst_rtable(dst);
}
EXPORT_SYMBOL_GPL(dst_cache_get_ip4);

void dst_cache_set_ip4(struct dst_cache *dst_cache, struct dst_entry *dst,
		       __be32 saddr)
{
	struct dst_cache_pcpu *idst;

	if (!dst_cache->cache)
		return;

	idst = this_cpu_ptr(dst_cache->cache);
	dst_cache_per_cpu_dst_set(idst, dst, 0);
	idst->in_saddr.s_addr = saddr;
}
EXPORT_SYMBOL_GPL(dst_cache_set_ip4);

#if IS_ENABLED(CONFIG_IPV6)
void dst_cache_set_ip6(struct dst_cache *dst_cache, struct dst_entry *dst,
		       const struct in6_addr *saddr)
{
	struct dst_cache_pcpu *idst;

	if (!dst_cache->cache)
		return;

	idst = this_cpu_ptr(dst_cache->cache);
	dst_cache_per_cpu_dst_set(idst, dst,
				  rt6_get_cookie(dst_rt6_info(dst)));
	idst->in6_saddr = *saddr;
}
EXPORT_SYMBOL_GPL(dst_cache_set_ip6);

struct dst_entry *dst_cache_get_ip6(struct dst_cache *dst_cache,
				    struct in6_addr *saddr)
{
	struct dst_cache_pcpu *idst;
	struct dst_entry *dst;

	if (!dst_cache->cache)
		return NULL;

	idst = this_cpu_ptr(dst_cache->cache);
	dst = dst_cache_per_cpu_get(dst_cache, idst);
	if (!dst)
		return NULL;

	*saddr = idst->in6_saddr;
	return dst;
}
EXPORT_SYMBOL_GPL(dst_cache_get_ip6);
#endif

int dst_cache_init(struct dst_cache *dst_cache, gfp_t gfp)
{
	dst_cache->cache = alloc_percpu_gfp(struct dst_cache_pcpu,
					    gfp | __GFP_ZERO);
	if (!dst_cache->cache)
		return -ENOMEM;

	dst_cache_reset(dst_cache);
	return 0;
}
EXPORT_SYMBOL_GPL(dst_cache_init);

void dst_cache_destroy(struct dst_cache *dst_cache)
{
	int i;

	if (!dst_cache->cache)
		return;

	for_each_possible_cpu(i)
		dst_release(per_cpu_ptr(dst_cache->cache, i)->dst);

	free_percpu(dst_cache->cache);
}
EXPORT_SYMBOL_GPL(dst_cache_destroy);

void dst_cache_reset_now(struct dst_cache *dst_cache)
{
	int i;

	if (!dst_cache->cache)
		return;

	dst_cache_reset(dst_cache);
	for_each_possible_cpu(i) {
		struct dst_cache_pcpu *idst = per_cpu_ptr(dst_cache->cache, i);
		struct dst_entry *dst = idst->dst;

		idst->cookie = 0;
		idst->dst = NULL;
		dst_release(dst);
	}
}
EXPORT_SYMBOL_GPL(dst_cache_reset_now);

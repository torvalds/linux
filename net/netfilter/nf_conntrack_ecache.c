/* Event cache for netfilter. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2003,2004 USAGI/WIDE Project <http://www.linux-ipv6.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/netfilter.h>
#include <linux/skbuff.h>
#include <linux/vmalloc.h>
#include <linux/stddef.h>
#include <linux/err.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_protocol.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_core.h>

ATOMIC_NOTIFIER_HEAD(nf_conntrack_chain);
ATOMIC_NOTIFIER_HEAD(nf_conntrack_expect_chain);

DEFINE_PER_CPU(struct nf_conntrack_ecache, nf_conntrack_ecache);

/* deliver cached events and clear cache entry - must be called with locally
 * disabled softirqs */
static inline void
__nf_ct_deliver_cached_events(struct nf_conntrack_ecache *ecache)
{
	if (nf_ct_is_confirmed(ecache->ct) && !nf_ct_is_dying(ecache->ct)
	    && ecache->events)
		atomic_notifier_call_chain(&nf_conntrack_chain, ecache->events,
				    ecache->ct);

	ecache->events = 0;
	nf_ct_put(ecache->ct);
	ecache->ct = NULL;
}

/* Deliver all cached events for a particular conntrack. This is called
 * by code prior to async packet handling for freeing the skb */
void nf_ct_deliver_cached_events(const struct nf_conn *ct)
{
	struct nf_conntrack_ecache *ecache;

	local_bh_disable();
	ecache = &__get_cpu_var(nf_conntrack_ecache);
	if (ecache->ct == ct)
		__nf_ct_deliver_cached_events(ecache);
	local_bh_enable();
}

/* Deliver cached events for old pending events, if current conntrack != old */
void __nf_ct_event_cache_init(struct nf_conn *ct)
{
	struct nf_conntrack_ecache *ecache;

	/* take care of delivering potentially old events */
	ecache = &__get_cpu_var(nf_conntrack_ecache);
	BUG_ON(ecache->ct == ct);
	if (ecache->ct)
		__nf_ct_deliver_cached_events(ecache);
	/* initialize for this conntrack/packet */
	ecache->ct = ct;
	nf_conntrack_get(&ct->ct_general);
}

/* flush the event cache - touches other CPU's data and must not be called
 * while packets are still passing through the code */
void nf_ct_event_cache_flush(void)
{
	struct nf_conntrack_ecache *ecache;
	int cpu;

	for_each_possible_cpu(cpu) {
		ecache = &per_cpu(nf_conntrack_ecache, cpu);
		if (ecache->ct)
			nf_ct_put(ecache->ct);
	}
}


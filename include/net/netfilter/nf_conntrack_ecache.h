/*
 * connection tracking event cache.
 */

#ifndef _NF_CONNTRACK_ECACHE_H
#define _NF_CONNTRACK_ECACHE_H
#include <net/netfilter/nf_conntrack.h>

#include <linux/notifier.h>
#include <linux/interrupt.h>
#include <net/net_namespace.h>
#include <net/netfilter/nf_conntrack_expect.h>

#ifdef CONFIG_NF_CONNTRACK_EVENTS
struct nf_conntrack_ecache {
	struct nf_conn *ct;
	unsigned int events;
};

extern struct atomic_notifier_head nf_conntrack_chain;
extern int nf_conntrack_register_notifier(struct notifier_block *nb);
extern int nf_conntrack_unregister_notifier(struct notifier_block *nb);

extern void nf_ct_deliver_cached_events(const struct nf_conn *ct);
extern void __nf_ct_event_cache_init(struct nf_conn *ct);
extern void nf_ct_event_cache_flush(struct net *net);

static inline void
nf_conntrack_event_cache(enum ip_conntrack_events event, struct nf_conn *ct)
{
	struct net *net = nf_ct_net(ct);
	struct nf_conntrack_ecache *ecache;

	local_bh_disable();
	ecache = per_cpu_ptr(net->ct.ecache, raw_smp_processor_id());
	if (ct != ecache->ct)
		__nf_ct_event_cache_init(ct);
	ecache->events |= event;
	local_bh_enable();
}

static inline void nf_conntrack_event(enum ip_conntrack_events event,
				      struct nf_conn *ct)
{
	if (nf_ct_is_confirmed(ct) && !nf_ct_is_dying(ct))
		atomic_notifier_call_chain(&nf_conntrack_chain, event, ct);
}

extern struct atomic_notifier_head nf_ct_expect_chain;
extern int nf_ct_expect_register_notifier(struct notifier_block *nb);
extern int nf_ct_expect_unregister_notifier(struct notifier_block *nb);

static inline void
nf_ct_expect_event(enum ip_conntrack_expect_events event,
		   struct nf_conntrack_expect *exp)
{
	atomic_notifier_call_chain(&nf_ct_expect_chain, event, exp);
}

extern int nf_conntrack_ecache_init(struct net *net);
extern void nf_conntrack_ecache_fini(struct net *net);

#else /* CONFIG_NF_CONNTRACK_EVENTS */

static inline void nf_conntrack_event_cache(enum ip_conntrack_events event,
					    const struct sk_buff *skb) {}
static inline void nf_conntrack_event(enum ip_conntrack_events event,
				      struct nf_conn *ct) {}
static inline void nf_ct_deliver_cached_events(const struct nf_conn *ct) {}
static inline void nf_ct_expect_event(enum ip_conntrack_expect_events event,
				      struct nf_conntrack_expect *exp) {}
static inline void nf_ct_event_cache_flush(struct net *net) {}

static inline int nf_conntrack_ecache_init(struct net *net)
{
	return 0;

static inline void nf_conntrack_ecache_fini(struct net *net)
{
}
#endif /* CONFIG_NF_CONNTRACK_EVENTS */

#endif /*_NF_CONNTRACK_ECACHE_H*/


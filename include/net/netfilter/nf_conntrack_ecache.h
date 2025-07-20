/* SPDX-License-Identifier: GPL-2.0 */
/*
 * connection tracking event cache.
 */

#ifndef _NF_CONNTRACK_ECACHE_H
#define _NF_CONNTRACK_ECACHE_H
#include <net/netfilter/nf_conntrack.h>

#include <net/net_namespace.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <linux/netfilter/nf_conntrack_common.h>
#include <linux/netfilter/nf_conntrack_tuple_common.h>
#include <net/netfilter/nf_conntrack_extend.h>
#include <asm/local64.h>

enum nf_ct_ecache_state {
	NFCT_ECACHE_DESTROY_FAIL,	/* tried but failed to send destroy event */
	NFCT_ECACHE_DESTROY_SENT,	/* sent destroy event after failure */
};

struct nf_conntrack_ecache {
	unsigned long cache;		/* bitops want long */
#ifdef CONFIG_NF_CONNTRACK_TIMESTAMP
	local64_t timestamp;		/* event timestamp, in nanoseconds */
#endif
	u16 ctmask;			/* bitmask of ct events to be delivered */
	u16 expmask;			/* bitmask of expect events to be delivered */
	u32 missed;			/* missed events */
	u32 portid;			/* netlink portid of destroyer */
};

static inline struct nf_conntrack_ecache *
nf_ct_ecache_find(const struct nf_conn *ct)
{
#ifdef CONFIG_NF_CONNTRACK_EVENTS
	return nf_ct_ext_find(ct, NF_CT_EXT_ECACHE);
#else
	return NULL;
#endif
}

static inline bool nf_ct_ecache_exist(const struct nf_conn *ct)
{
#ifdef CONFIG_NF_CONNTRACK_EVENTS
	return nf_ct_ext_exist(ct, NF_CT_EXT_ECACHE);
#else
	return false;
#endif
}

#ifdef CONFIG_NF_CONNTRACK_EVENTS

/* This structure is passed to event handler */
struct nf_ct_event {
	struct nf_conn *ct;
	u32 portid;
	int report;
};

struct nf_exp_event {
	struct nf_conntrack_expect *exp;
	u32 portid;
	int report;
};

struct nf_ct_event_notifier {
	int (*ct_event)(unsigned int events, const struct nf_ct_event *item);
	int (*exp_event)(unsigned int events, const struct nf_exp_event *item);
};

void nf_conntrack_register_notifier(struct net *net,
				   const struct nf_ct_event_notifier *nb);
void nf_conntrack_unregister_notifier(struct net *net);

void nf_ct_deliver_cached_events(struct nf_conn *ct);
int nf_conntrack_eventmask_report(unsigned int eventmask, struct nf_conn *ct,
				  u32 portid, int report);

bool nf_ct_ecache_ext_add(struct nf_conn *ct, u16 ctmask, u16 expmask, gfp_t gfp);
#else

static inline void nf_ct_deliver_cached_events(const struct nf_conn *ct)
{
}

static inline int nf_conntrack_eventmask_report(unsigned int eventmask,
						struct nf_conn *ct,
						u32 portid,
						int report)
{
	return 0;
}

static inline bool nf_ct_ecache_ext_add(struct nf_conn *ct, u16 ctmask, u16 expmask, gfp_t gfp)
{
	return false;
}
#endif

static inline void
nf_conntrack_event_cache(enum ip_conntrack_events event, struct nf_conn *ct)
{
#ifdef CONFIG_NF_CONNTRACK_EVENTS
	struct net *net = nf_ct_net(ct);
	struct nf_conntrack_ecache *e;

	if (!rcu_access_pointer(net->ct.nf_conntrack_event_cb))
		return;

	e = nf_ct_ecache_find(ct);
	if (e == NULL)
		return;

#ifdef CONFIG_NF_CONNTRACK_TIMESTAMP
	/* renew only if this is the first cached event, so that the
	 * timestamp reflects the first, not the last, generated event.
	 */
	if (local64_read(&e->timestamp) && READ_ONCE(e->cache) == 0)
		local64_set(&e->timestamp, ktime_get_real_ns());
#endif

	set_bit(event, &e->cache);
#endif
}

static inline int
nf_conntrack_event_report(enum ip_conntrack_events event, struct nf_conn *ct,
			  u32 portid, int report)
{
#ifdef CONFIG_NF_CONNTRACK_EVENTS
	if (nf_ct_ecache_exist(ct))
		return nf_conntrack_eventmask_report(1 << event, ct, portid, report);
#endif
	return 0;
}

static inline int
nf_conntrack_event(enum ip_conntrack_events event, struct nf_conn *ct)
{
#ifdef CONFIG_NF_CONNTRACK_EVENTS
	if (nf_ct_ecache_exist(ct))
		return nf_conntrack_eventmask_report(1 << event, ct, 0, 0);
#endif
	return 0;
}

#ifdef CONFIG_NF_CONNTRACK_EVENTS
void nf_ct_expect_event_report(enum ip_conntrack_expect_events event,
			       struct nf_conntrack_expect *exp,
			       u32 portid, int report);

void nf_conntrack_ecache_work(struct net *net, enum nf_ct_ecache_state state);

void nf_conntrack_ecache_pernet_init(struct net *net);
void nf_conntrack_ecache_pernet_fini(struct net *net);

struct nf_conntrack_net_ecache *nf_conn_pernet_ecache(const struct net *net);

static inline bool nf_conntrack_ecache_dwork_pending(const struct net *net)
{
	return net->ct.ecache_dwork_pending;
}
#else /* CONFIG_NF_CONNTRACK_EVENTS */

static inline void nf_ct_expect_event_report(enum ip_conntrack_expect_events e,
					     struct nf_conntrack_expect *exp,
					     u32 portid,
					     int report)
{
}

static inline void nf_conntrack_ecache_work(struct net *net,
					    enum nf_ct_ecache_state s)
{
}

static inline void nf_conntrack_ecache_pernet_init(struct net *net)
{
}

static inline void nf_conntrack_ecache_pernet_fini(struct net *net)
{
}
static inline bool nf_conntrack_ecache_dwork_pending(const struct net *net) { return false; }
#endif /* CONFIG_NF_CONNTRACK_EVENTS */
#endif /*_NF_CONNTRACK_ECACHE_H*/

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

/* Connection tracking event bits */
enum ip_conntrack_events
{
	/* New conntrack */
	IPCT_NEW_BIT = 0,
	IPCT_NEW = (1 << IPCT_NEW_BIT),

	/* Expected connection */
	IPCT_RELATED_BIT = 1,
	IPCT_RELATED = (1 << IPCT_RELATED_BIT),

	/* Destroyed conntrack */
	IPCT_DESTROY_BIT = 2,
	IPCT_DESTROY = (1 << IPCT_DESTROY_BIT),

	/* Timer has been refreshed */
	IPCT_REFRESH_BIT = 3,
	IPCT_REFRESH = (1 << IPCT_REFRESH_BIT),

	/* Status has changed */
	IPCT_STATUS_BIT = 4,
	IPCT_STATUS = (1 << IPCT_STATUS_BIT),

	/* Update of protocol info */
	IPCT_PROTOINFO_BIT = 5,
	IPCT_PROTOINFO = (1 << IPCT_PROTOINFO_BIT),

	/* Volatile protocol info */
	IPCT_PROTOINFO_VOLATILE_BIT = 6,
	IPCT_PROTOINFO_VOLATILE = (1 << IPCT_PROTOINFO_VOLATILE_BIT),

	/* New helper for conntrack */
	IPCT_HELPER_BIT = 7,
	IPCT_HELPER = (1 << IPCT_HELPER_BIT),

	/* Update of helper info */
	IPCT_HELPINFO_BIT = 8,
	IPCT_HELPINFO = (1 << IPCT_HELPINFO_BIT),

	/* Volatile helper info */
	IPCT_HELPINFO_VOLATILE_BIT = 9,
	IPCT_HELPINFO_VOLATILE = (1 << IPCT_HELPINFO_VOLATILE_BIT),

	/* NAT info */
	IPCT_NATINFO_BIT = 10,
	IPCT_NATINFO = (1 << IPCT_NATINFO_BIT),

	/* Counter highest bit has been set, unused */
	IPCT_COUNTER_FILLING_BIT = 11,
	IPCT_COUNTER_FILLING = (1 << IPCT_COUNTER_FILLING_BIT),

	/* Mark is set */
	IPCT_MARK_BIT = 12,
	IPCT_MARK = (1 << IPCT_MARK_BIT),

	/* NAT sequence adjustment */
	IPCT_NATSEQADJ_BIT = 13,
	IPCT_NATSEQADJ = (1 << IPCT_NATSEQADJ_BIT),

	/* Secmark is set */
	IPCT_SECMARK_BIT = 14,
	IPCT_SECMARK = (1 << IPCT_SECMARK_BIT),
};

enum ip_conntrack_expect_events {
	IPEXP_NEW_BIT = 0,
	IPEXP_NEW = (1 << IPEXP_NEW_BIT),
};

#ifdef CONFIG_NF_CONNTRACK_EVENTS
struct nf_conntrack_ecache {
	struct nf_conn *ct;
	unsigned int events;
};

/* This structure is passed to event handler */
struct nf_ct_event {
	struct nf_conn *ct;
	u32 pid;
	int report;
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

static inline void
nf_conntrack_event_report(enum ip_conntrack_events event,
			  struct nf_conn *ct,
			  u32 pid,
			  int report)
{
	struct nf_ct_event item = {
		.ct 	= ct,
		.pid	= pid,
		.report = report
	};
	if (nf_ct_is_confirmed(ct) && !nf_ct_is_dying(ct))
		atomic_notifier_call_chain(&nf_conntrack_chain, event, &item);
}

static inline void
nf_conntrack_event(enum ip_conntrack_events event, struct nf_conn *ct)
{
	nf_conntrack_event_report(event, ct, 0, 0);
}

struct nf_exp_event {
	struct nf_conntrack_expect *exp;
	u32 pid;
	int report;
};

extern struct atomic_notifier_head nf_ct_expect_chain;
extern int nf_ct_expect_register_notifier(struct notifier_block *nb);
extern int nf_ct_expect_unregister_notifier(struct notifier_block *nb);

static inline void
nf_ct_expect_event_report(enum ip_conntrack_expect_events event,
			  struct nf_conntrack_expect *exp,
			  u32 pid,
			  int report)
{
	struct nf_exp_event item = {
		.exp	= exp,
		.pid	= pid,
		.report = report
	};
	atomic_notifier_call_chain(&nf_ct_expect_chain, event, &item);
}

static inline void
nf_ct_expect_event(enum ip_conntrack_expect_events event,
		   struct nf_conntrack_expect *exp)
{
	nf_ct_expect_event_report(event, exp, 0, 0);
}

extern int nf_conntrack_ecache_init(struct net *net);
extern void nf_conntrack_ecache_fini(struct net *net);

#else /* CONFIG_NF_CONNTRACK_EVENTS */

static inline void nf_conntrack_event_cache(enum ip_conntrack_events event,
					    struct nf_conn *ct) {}
static inline void nf_conntrack_event(enum ip_conntrack_events event,
				      struct nf_conn *ct) {}
static inline void nf_conntrack_event_report(enum ip_conntrack_events event,
					     struct nf_conn *ct,
					     u32 pid,
					     int report) {}
static inline void nf_ct_deliver_cached_events(const struct nf_conn *ct) {}
static inline void nf_ct_expect_event(enum ip_conntrack_expect_events event,
				      struct nf_conntrack_expect *exp) {}
static inline void nf_ct_expect_event_report(enum ip_conntrack_expect_events e,
					     struct nf_conntrack_expect *exp,
 					     u32 pid,
 					     int report) {}
static inline void nf_ct_event_cache_flush(struct net *net) {}

static inline int nf_conntrack_ecache_init(struct net *net)
{
	return 0;
}

static inline void nf_conntrack_ecache_fini(struct net *net)
{
}
#endif /* CONFIG_NF_CONNTRACK_EVENTS */

#endif /*_NF_CONNTRACK_ECACHE_H*/


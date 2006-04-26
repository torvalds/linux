/*
 * Connection state tracking for netfilter.  This is separated from,
 * but required by, the (future) NAT layer; it can also be used by an iptables
 * extension.
 *
 * 16 Dec 2003: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- generalize L3 protocol dependent part.
 *
 * Derived from include/linux/netfiter_ipv4/ip_conntrack.h
 */

#ifndef _NF_CONNTRACK_H
#define _NF_CONNTRACK_H

#include <linux/netfilter/nf_conntrack_common.h>

#ifdef __KERNEL__
#include <linux/bitops.h>
#include <linux/compiler.h>
#include <asm/atomic.h>

#include <linux/netfilter/nf_conntrack_tcp.h>
#include <linux/netfilter/nf_conntrack_sctp.h>
#include <net/netfilter/ipv4/nf_conntrack_icmp.h>
#include <net/netfilter/ipv6/nf_conntrack_icmpv6.h>

#include <net/netfilter/nf_conntrack_tuple.h>

/* per conntrack: protocol private data */
union nf_conntrack_proto {
	/* insert conntrack proto private data here */
	struct ip_ct_sctp sctp;
	struct ip_ct_tcp tcp;
	struct ip_ct_icmp icmp;
	struct nf_ct_icmpv6 icmpv6;
};

union nf_conntrack_expect_proto {
	/* insert expect proto private data here */
};

/* Add protocol helper include file here */
#include <linux/netfilter/nf_conntrack_ftp.h>

/* per conntrack: application helper private data */
union nf_conntrack_help {
	/* insert conntrack helper private data (master) here */
	struct ip_ct_ftp_master ct_ftp_info;
};

#include <linux/types.h>
#include <linux/skbuff.h>

#ifdef CONFIG_NETFILTER_DEBUG
#define NF_CT_ASSERT(x)							\
do {									\
	if (!(x))							\
		/* Wooah!  I'm tripping my conntrack in a frenzy of	\
		   netplay... */					\
		printk("NF_CT_ASSERT: %s:%i(%s)\n",			\
		       __FILE__, __LINE__, __FUNCTION__);		\
} while(0)
#else
#define NF_CT_ASSERT(x)
#endif

struct nf_conntrack_helper;

/* nf_conn feature for connections that have a helper */
struct nf_conn_help {
	/* Helper. if any */
	struct nf_conntrack_helper *helper;

	union nf_conntrack_help help;

	/* Current number of expected connections */
	unsigned int expecting;
};


#include <net/netfilter/ipv4/nf_conntrack_ipv4.h>
struct nf_conn
{
	/* Usage count in here is 1 for hash table/destruct timer, 1 per skb,
           plus 1 for any connection(s) we are `master' for */
	struct nf_conntrack ct_general;

	/* XXX should I move this to the tail ? - Y.K */
	/* These are my tuples; original and reply */
	struct nf_conntrack_tuple_hash tuplehash[IP_CT_DIR_MAX];

	/* Have we seen traffic both ways yet? (bitset) */
	unsigned long status;

	/* If we were expected by an expectation, this will be it */
	struct nf_conn *master;

	/* Timer function; drops refcnt when it goes off. */
	struct timer_list timeout;

#ifdef CONFIG_NF_CT_ACCT
	/* Accounting Information (same cache line as other written members) */
	struct ip_conntrack_counter counters[IP_CT_DIR_MAX];
#endif

	/* Unique ID that identifies this conntrack*/
	unsigned int id;

	/* features - nat, helper, ... used by allocating system */
	u_int32_t features;

#if defined(CONFIG_NF_CONNTRACK_MARK)
	u_int32_t mark;
#endif

	/* Storage reserved for other modules: */
	union nf_conntrack_proto proto;

	/* features dynamically at the end: helper, nat (both optional) */
	char data[0];
};

struct nf_conntrack_expect
{
	/* Internal linked list (global expectation list) */
	struct list_head list;

	/* We expect this tuple, with the following mask */
	struct nf_conntrack_tuple tuple, mask;
 
	/* Function to call after setup and insertion */
	void (*expectfn)(struct nf_conn *new,
			 struct nf_conntrack_expect *this);

	/* The conntrack of the master connection */
	struct nf_conn *master;

	/* Timer function; deletes the expectation. */
	struct timer_list timeout;

	/* Usage count. */
	atomic_t use;

	/* Unique ID */
	unsigned int id;

	/* Flags */
	unsigned int flags;

#ifdef CONFIG_NF_NAT_NEEDED
	/* This is the original per-proto part, used to map the
	 * expected connection the way the recipient expects. */
	union nf_conntrack_manip_proto saved_proto;
	/* Direction relative to the master connection. */
	enum ip_conntrack_dir dir;
#endif
};

#define NF_CT_EXPECT_PERMANENT 0x1

static inline struct nf_conn *
nf_ct_tuplehash_to_ctrack(const struct nf_conntrack_tuple_hash *hash)
{
	return container_of(hash, struct nf_conn,
			    tuplehash[hash->tuple.dst.dir]);
}

/* get master conntrack via master expectation */
#define master_ct(conntr) (conntr->master)

/* Alter reply tuple (maybe alter helper). */
extern void
nf_conntrack_alter_reply(struct nf_conn *conntrack,
			 const struct nf_conntrack_tuple *newreply);

/* Is this tuple taken? (ignoring any belonging to the given
   conntrack). */
extern int
nf_conntrack_tuple_taken(const struct nf_conntrack_tuple *tuple,
			 const struct nf_conn *ignored_conntrack);

/* Return conntrack_info and tuple hash for given skb. */
static inline struct nf_conn *
nf_ct_get(const struct sk_buff *skb, enum ip_conntrack_info *ctinfo)
{
	*ctinfo = skb->nfctinfo;
	return (struct nf_conn *)skb->nfct;
}

/* decrement reference count on a conntrack */
static inline void nf_ct_put(struct nf_conn *ct)
{
	NF_CT_ASSERT(ct);
	nf_conntrack_put(&ct->ct_general);
}

/* Protocol module loading */
extern int nf_ct_l3proto_try_module_get(unsigned short l3proto);
extern void nf_ct_l3proto_module_put(unsigned short l3proto);

extern struct nf_conntrack_tuple_hash *
__nf_conntrack_find(const struct nf_conntrack_tuple *tuple,
		    const struct nf_conn *ignored_conntrack);

extern void nf_conntrack_hash_insert(struct nf_conn *ct);

extern struct nf_conntrack_expect *
__nf_conntrack_expect_find(const struct nf_conntrack_tuple *tuple);

extern struct nf_conntrack_expect *
nf_conntrack_expect_find(const struct nf_conntrack_tuple *tuple);

extern void nf_ct_unlink_expect(struct nf_conntrack_expect *exp);

extern void nf_ct_remove_expectations(struct nf_conn *ct);

extern void nf_conntrack_flush(void);

extern struct nf_conntrack_helper *
nf_ct_helper_find_get( const struct nf_conntrack_tuple *tuple);
extern void nf_ct_helper_put(struct nf_conntrack_helper *helper);

extern struct nf_conntrack_helper *
__nf_conntrack_helper_find_byname(const char *name);

extern int nf_ct_invert_tuplepr(struct nf_conntrack_tuple *inverse,
				const struct nf_conntrack_tuple *orig);

extern void __nf_ct_refresh_acct(struct nf_conn *ct,
				 enum ip_conntrack_info ctinfo,
				 const struct sk_buff *skb,
				 unsigned long extra_jiffies,
				 int do_acct);

/* Refresh conntrack for this many jiffies and do accounting */
static inline void nf_ct_refresh_acct(struct nf_conn *ct,
				      enum ip_conntrack_info ctinfo,
				      const struct sk_buff *skb,
				      unsigned long extra_jiffies)
{
	__nf_ct_refresh_acct(ct, ctinfo, skb, extra_jiffies, 1);
}

/* Refresh conntrack for this many jiffies */
static inline void nf_ct_refresh(struct nf_conn *ct,
				 const struct sk_buff *skb,
				 unsigned long extra_jiffies)
{
	__nf_ct_refresh_acct(ct, 0, skb, extra_jiffies, 0);
}

/* These are for NAT.  Icky. */
/* Update TCP window tracking data when NAT mangles the packet */
extern void nf_conntrack_tcp_update(struct sk_buff *skb,
				    unsigned int dataoff,
				    struct nf_conn *conntrack,
				    int dir);

/* Call me when a conntrack is destroyed. */
extern void (*nf_conntrack_destroyed)(struct nf_conn *conntrack);

/* Fake conntrack entry for untracked connections */
extern struct nf_conn nf_conntrack_untracked;

extern int nf_ct_no_defrag;

/* Iterate over all conntracks: if iter returns true, it's deleted. */
extern void
nf_ct_iterate_cleanup(int (*iter)(struct nf_conn *i, void *data), void *data);
extern void nf_conntrack_free(struct nf_conn *ct);
extern struct nf_conn *
nf_conntrack_alloc(const struct nf_conntrack_tuple *orig,
		   const struct nf_conntrack_tuple *repl);

/* It's confirmed if it is, or has been in the hash table. */
static inline int nf_ct_is_confirmed(struct nf_conn *ct)
{
	return test_bit(IPS_CONFIRMED_BIT, &ct->status);
}

static inline int nf_ct_is_dying(struct nf_conn *ct)
{
	return test_bit(IPS_DYING_BIT, &ct->status);
}

extern unsigned int nf_conntrack_htable_size;

#define NF_CT_STAT_INC(count) (__get_cpu_var(nf_conntrack_stat).count++)

#ifdef CONFIG_NF_CONNTRACK_EVENTS
#include <linux/notifier.h>
#include <linux/interrupt.h>

struct nf_conntrack_ecache {
	struct nf_conn *ct;
	unsigned int events;
};
DECLARE_PER_CPU(struct nf_conntrack_ecache, nf_conntrack_ecache);

#define CONNTRACK_ECACHE(x)	(__get_cpu_var(nf_conntrack_ecache).x)

extern struct atomic_notifier_head nf_conntrack_chain;
extern struct atomic_notifier_head nf_conntrack_expect_chain;

static inline int nf_conntrack_register_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&nf_conntrack_chain, nb);
}

static inline int nf_conntrack_unregister_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&nf_conntrack_chain, nb);
}

static inline int
nf_conntrack_expect_register_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&nf_conntrack_expect_chain, nb);
}

static inline int
nf_conntrack_expect_unregister_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&nf_conntrack_expect_chain,
			nb);
}

extern void nf_ct_deliver_cached_events(const struct nf_conn *ct);
extern void __nf_ct_event_cache_init(struct nf_conn *ct);

static inline void
nf_conntrack_event_cache(enum ip_conntrack_events event,
			 const struct sk_buff *skb)
{
	struct nf_conn *ct = (struct nf_conn *)skb->nfct;
	struct nf_conntrack_ecache *ecache;

	local_bh_disable();
	ecache = &__get_cpu_var(nf_conntrack_ecache);
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

static inline void
nf_conntrack_expect_event(enum ip_conntrack_expect_events event,
			  struct nf_conntrack_expect *exp)
{
	atomic_notifier_call_chain(&nf_conntrack_expect_chain, event, exp);
}
#else /* CONFIG_NF_CONNTRACK_EVENTS */
static inline void nf_conntrack_event_cache(enum ip_conntrack_events event,
					    const struct sk_buff *skb) {}
static inline void nf_conntrack_event(enum ip_conntrack_events event,
				      struct nf_conn *ct) {}
static inline void nf_ct_deliver_cached_events(const struct nf_conn *ct) {}
static inline void
nf_conntrack_expect_event(enum ip_conntrack_expect_events event,
			  struct nf_conntrack_expect *exp) {}
#endif /* CONFIG_NF_CONNTRACK_EVENTS */

/* no helper, no nat */
#define	NF_CT_F_BASIC	0
/* for helper */
#define	NF_CT_F_HELP	1
/* for nat. */
#define	NF_CT_F_NAT	2
#define NF_CT_F_NUM	4

extern int
nf_conntrack_register_cache(u_int32_t features, const char *name, size_t size);
extern void
nf_conntrack_unregister_cache(u_int32_t features);

/* valid combinations:
 * basic: nf_conn, nf_conn .. nf_conn_help
 * nat: nf_conn .. nf_conn_nat, nf_conn .. nf_conn_nat, nf_conn help
 */
static inline struct nf_conn_help *nfct_help(const struct nf_conn *ct)
{
	unsigned int offset = sizeof(struct nf_conn);

	if (!(ct->features & NF_CT_F_HELP))
		return NULL;

	return (struct nf_conn_help *) ((void *)ct + offset);
}

#endif /* __KERNEL__ */
#endif /* _NF_CONNTRACK_H */

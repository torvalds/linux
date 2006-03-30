#ifndef _IP_CONNTRACK_H
#define _IP_CONNTRACK_H

#include <linux/netfilter/nf_conntrack_common.h>

#ifdef __KERNEL__
#include <linux/config.h>
#include <linux/netfilter_ipv4/ip_conntrack_tuple.h>
#include <linux/bitops.h>
#include <linux/compiler.h>
#include <asm/atomic.h>

#include <linux/netfilter_ipv4/ip_conntrack_tcp.h>
#include <linux/netfilter_ipv4/ip_conntrack_icmp.h>
#include <linux/netfilter_ipv4/ip_conntrack_proto_gre.h>
#include <linux/netfilter_ipv4/ip_conntrack_sctp.h>

/* per conntrack: protocol private data */
union ip_conntrack_proto {
	/* insert conntrack proto private data here */
	struct ip_ct_gre gre;
	struct ip_ct_sctp sctp;
	struct ip_ct_tcp tcp;
	struct ip_ct_icmp icmp;
};

union ip_conntrack_expect_proto {
	/* insert expect proto private data here */
};

/* Add protocol helper include file here */
#include <linux/netfilter_ipv4/ip_conntrack_h323.h>
#include <linux/netfilter_ipv4/ip_conntrack_pptp.h>
#include <linux/netfilter_ipv4/ip_conntrack_amanda.h>
#include <linux/netfilter_ipv4/ip_conntrack_ftp.h>
#include <linux/netfilter_ipv4/ip_conntrack_irc.h>

/* per conntrack: application helper private data */
union ip_conntrack_help {
	/* insert conntrack helper private data (master) here */
	struct ip_ct_h323_master ct_h323_info;
	struct ip_ct_pptp_master ct_pptp_info;
	struct ip_ct_ftp_master ct_ftp_info;
	struct ip_ct_irc_master ct_irc_info;
};

#ifdef CONFIG_IP_NF_NAT_NEEDED
#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_pptp.h>

/* per conntrack: nat application helper private data */
union ip_conntrack_nat_help {
	/* insert nat helper private data here */
	struct ip_nat_pptp nat_pptp_info;
};
#endif

#include <linux/types.h>
#include <linux/skbuff.h>

#ifdef CONFIG_NETFILTER_DEBUG
#define IP_NF_ASSERT(x)							\
do {									\
	if (!(x))							\
		/* Wooah!  I'm tripping my conntrack in a frenzy of	\
		   netplay... */					\
		printk("NF_IP_ASSERT: %s:%i(%s)\n",			\
		       __FILE__, __LINE__, __FUNCTION__);		\
} while(0)
#else
#define IP_NF_ASSERT(x)
#endif

struct ip_conntrack_helper;

struct ip_conntrack
{
	/* Usage count in here is 1 for hash table/destruct timer, 1 per skb,
           plus 1 for any connection(s) we are `master' for */
	struct nf_conntrack ct_general;

	/* Have we seen traffic both ways yet? (bitset) */
	unsigned long status;

	/* Timer function; drops refcnt when it goes off. */
	struct timer_list timeout;

#ifdef CONFIG_IP_NF_CT_ACCT
	/* Accounting Information (same cache line as other written members) */
	struct ip_conntrack_counter counters[IP_CT_DIR_MAX];
#endif
	/* If we were expected by an expectation, this will be it */
	struct ip_conntrack *master;

	/* Current number of expected connections */
	unsigned int expecting;

	/* Unique ID that identifies this conntrack*/
	unsigned int id;

	/* Helper, if any. */
	struct ip_conntrack_helper *helper;

	/* Storage reserved for other modules: */
	union ip_conntrack_proto proto;

	union ip_conntrack_help help;

#ifdef CONFIG_IP_NF_NAT_NEEDED
	struct {
		struct ip_nat_info info;
		union ip_conntrack_nat_help help;
#if defined(CONFIG_IP_NF_TARGET_MASQUERADE) || \
	defined(CONFIG_IP_NF_TARGET_MASQUERADE_MODULE)
		int masq_index;
#endif
	} nat;
#endif /* CONFIG_IP_NF_NAT_NEEDED */

#if defined(CONFIG_IP_NF_CONNTRACK_MARK)
	u_int32_t mark;
#endif

	/* Traversed often, so hopefully in different cacheline to top */
	/* These are my tuples; original and reply */
	struct ip_conntrack_tuple_hash tuplehash[IP_CT_DIR_MAX];
};

struct ip_conntrack_expect
{
	/* Internal linked list (global expectation list) */
	struct list_head list;

	/* We expect this tuple, with the following mask */
	struct ip_conntrack_tuple tuple, mask;
 
	/* Function to call after setup and insertion */
	void (*expectfn)(struct ip_conntrack *new,
			 struct ip_conntrack_expect *this);

	/* The conntrack of the master connection */
	struct ip_conntrack *master;

	/* Timer function; deletes the expectation. */
	struct timer_list timeout;

	/* Usage count. */
	atomic_t use;

	/* Unique ID */
	unsigned int id;

	/* Flags */
	unsigned int flags;

#ifdef CONFIG_IP_NF_NAT_NEEDED
	/* This is the original per-proto part, used to map the
	 * expected connection the way the recipient expects. */
	union ip_conntrack_manip_proto saved_proto;
	/* Direction relative to the master connection. */
	enum ip_conntrack_dir dir;
#endif
};

#define IP_CT_EXPECT_PERMANENT	0x1

static inline struct ip_conntrack *
tuplehash_to_ctrack(const struct ip_conntrack_tuple_hash *hash)
{
	return container_of(hash, struct ip_conntrack,
			    tuplehash[hash->tuple.dst.dir]);
}

/* get master conntrack via master expectation */
#define master_ct(conntr) (conntr->master)

/* Alter reply tuple (maybe alter helper). */
extern void
ip_conntrack_alter_reply(struct ip_conntrack *conntrack,
			 const struct ip_conntrack_tuple *newreply);

/* Is this tuple taken? (ignoring any belonging to the given
   conntrack). */
extern int
ip_conntrack_tuple_taken(const struct ip_conntrack_tuple *tuple,
			 const struct ip_conntrack *ignored_conntrack);

/* Return conntrack_info and tuple hash for given skb. */
static inline struct ip_conntrack *
ip_conntrack_get(const struct sk_buff *skb, enum ip_conntrack_info *ctinfo)
{
	*ctinfo = skb->nfctinfo;
	return (struct ip_conntrack *)skb->nfct;
}

/* decrement reference count on a conntrack */
static inline void
ip_conntrack_put(struct ip_conntrack *ct)
{
	IP_NF_ASSERT(ct);
	nf_conntrack_put(&ct->ct_general);
}

extern int invert_tuplepr(struct ip_conntrack_tuple *inverse,
			  const struct ip_conntrack_tuple *orig);

extern void __ip_ct_refresh_acct(struct ip_conntrack *ct,
			         enum ip_conntrack_info ctinfo,
			         const struct sk_buff *skb,
			         unsigned long extra_jiffies,
				 int do_acct);

/* Refresh conntrack for this many jiffies and do accounting */
static inline void ip_ct_refresh_acct(struct ip_conntrack *ct, 
				      enum ip_conntrack_info ctinfo,
				      const struct sk_buff *skb,
				      unsigned long extra_jiffies)
{
	__ip_ct_refresh_acct(ct, ctinfo, skb, extra_jiffies, 1);
}

/* Refresh conntrack for this many jiffies */
static inline void ip_ct_refresh(struct ip_conntrack *ct,
				 const struct sk_buff *skb,
				 unsigned long extra_jiffies)
{
	__ip_ct_refresh_acct(ct, 0, skb, extra_jiffies, 0);
}

/* These are for NAT.  Icky. */
/* Update TCP window tracking data when NAT mangles the packet */
extern void ip_conntrack_tcp_update(struct sk_buff *skb,
				    struct ip_conntrack *conntrack,
				    enum ip_conntrack_dir dir);

/* Call me when a conntrack is destroyed. */
extern void (*ip_conntrack_destroyed)(struct ip_conntrack *conntrack);

/* Fake conntrack entry for untracked connections */
extern struct ip_conntrack ip_conntrack_untracked;

/* Returns new sk_buff, or NULL */
struct sk_buff *
ip_ct_gather_frags(struct sk_buff *skb, u_int32_t user);

/* Iterate over all conntracks: if iter returns true, it's deleted. */
extern void
ip_ct_iterate_cleanup(int (*iter)(struct ip_conntrack *i, void *data),
		      void *data);

extern struct ip_conntrack_helper *
__ip_conntrack_helper_find_byname(const char *);
extern struct ip_conntrack_helper *
ip_conntrack_helper_find_get(const struct ip_conntrack_tuple *tuple);
extern void ip_conntrack_helper_put(struct ip_conntrack_helper *helper);

extern struct ip_conntrack_protocol *
__ip_conntrack_proto_find(u_int8_t protocol);
extern struct ip_conntrack_protocol *
ip_conntrack_proto_find_get(u_int8_t protocol);
extern void ip_conntrack_proto_put(struct ip_conntrack_protocol *proto);

extern void ip_ct_remove_expectations(struct ip_conntrack *ct);

extern struct ip_conntrack *ip_conntrack_alloc(struct ip_conntrack_tuple *,
					       struct ip_conntrack_tuple *);

extern void ip_conntrack_free(struct ip_conntrack *ct);

extern void ip_conntrack_hash_insert(struct ip_conntrack *ct);

extern struct ip_conntrack_expect *
__ip_conntrack_expect_find(const struct ip_conntrack_tuple *tuple);

extern struct ip_conntrack_expect *
ip_conntrack_expect_find(const struct ip_conntrack_tuple *tuple);

extern struct ip_conntrack_tuple_hash *
__ip_conntrack_find(const struct ip_conntrack_tuple *tuple,
                    const struct ip_conntrack *ignored_conntrack);

extern void ip_conntrack_flush(void);

/* It's confirmed if it is, or has been in the hash table. */
static inline int is_confirmed(struct ip_conntrack *ct)
{
	return test_bit(IPS_CONFIRMED_BIT, &ct->status);
}

static inline int is_dying(struct ip_conntrack *ct)
{
	return test_bit(IPS_DYING_BIT, &ct->status);
}

extern unsigned int ip_conntrack_htable_size;
 
#define CONNTRACK_STAT_INC(count) (__get_cpu_var(ip_conntrack_stat).count++)

#ifdef CONFIG_IP_NF_CONNTRACK_EVENTS
#include <linux/notifier.h>
#include <linux/interrupt.h>

struct ip_conntrack_ecache {
	struct ip_conntrack *ct;
	unsigned int events;
};
DECLARE_PER_CPU(struct ip_conntrack_ecache, ip_conntrack_ecache);

#define CONNTRACK_ECACHE(x)	(__get_cpu_var(ip_conntrack_ecache).x)
 
extern struct atomic_notifier_head ip_conntrack_chain;
extern struct atomic_notifier_head ip_conntrack_expect_chain;

static inline int ip_conntrack_register_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&ip_conntrack_chain, nb);
}

static inline int ip_conntrack_unregister_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&ip_conntrack_chain, nb);
}

static inline int 
ip_conntrack_expect_register_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&ip_conntrack_expect_chain, nb);
}

static inline int
ip_conntrack_expect_unregister_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&ip_conntrack_expect_chain,
			nb);
}

extern void ip_ct_deliver_cached_events(const struct ip_conntrack *ct);
extern void __ip_ct_event_cache_init(struct ip_conntrack *ct);

static inline void 
ip_conntrack_event_cache(enum ip_conntrack_events event,
			 const struct sk_buff *skb)
{
	struct ip_conntrack *ct = (struct ip_conntrack *)skb->nfct;
	struct ip_conntrack_ecache *ecache;
	
	local_bh_disable();
	ecache = &__get_cpu_var(ip_conntrack_ecache);
	if (ct != ecache->ct)
		__ip_ct_event_cache_init(ct);
	ecache->events |= event;
	local_bh_enable();
}

static inline void ip_conntrack_event(enum ip_conntrack_events event,
				      struct ip_conntrack *ct)
{
	if (is_confirmed(ct) && !is_dying(ct))
		atomic_notifier_call_chain(&ip_conntrack_chain, event, ct);
}

static inline void 
ip_conntrack_expect_event(enum ip_conntrack_expect_events event,
			  struct ip_conntrack_expect *exp)
{
	atomic_notifier_call_chain(&ip_conntrack_expect_chain, event, exp);
}
#else /* CONFIG_IP_NF_CONNTRACK_EVENTS */
static inline void ip_conntrack_event_cache(enum ip_conntrack_events event, 
					    const struct sk_buff *skb) {}
static inline void ip_conntrack_event(enum ip_conntrack_events event, 
				      struct ip_conntrack *ct) {}
static inline void ip_ct_deliver_cached_events(const struct ip_conntrack *ct) {}
static inline void 
ip_conntrack_expect_event(enum ip_conntrack_expect_events event, 
			  struct ip_conntrack_expect *exp) {}
#endif /* CONFIG_IP_NF_CONNTRACK_EVENTS */

#ifdef CONFIG_IP_NF_NAT_NEEDED
static inline int ip_nat_initialized(struct ip_conntrack *conntrack,
				     enum ip_nat_manip_type manip)
{
	if (manip == IP_NAT_MANIP_SRC)
		return test_bit(IPS_SRC_NAT_DONE_BIT, &conntrack->status);
	return test_bit(IPS_DST_NAT_DONE_BIT, &conntrack->status);
}
#endif /* CONFIG_IP_NF_NAT_NEEDED */

#endif /* __KERNEL__ */
#endif /* _IP_CONNTRACK_H */

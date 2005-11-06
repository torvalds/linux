#ifndef _IP_CONNTRACK_H
#define _IP_CONNTRACK_H
/* Connection state tracking for netfilter.  This is separated from,
   but required by, the NAT layer; it can also be used by an iptables
   extension. */
enum ip_conntrack_info
{
	/* Part of an established connection (either direction). */
	IP_CT_ESTABLISHED,

	/* Like NEW, but related to an existing connection, or ICMP error
	   (in either direction). */
	IP_CT_RELATED,

	/* Started a new connection to track (only
           IP_CT_DIR_ORIGINAL); may be a retransmission. */
	IP_CT_NEW,

	/* >= this indicates reply direction */
	IP_CT_IS_REPLY,

	/* Number of distinct IP_CT types (no NEW in reply dirn). */
	IP_CT_NUMBER = IP_CT_IS_REPLY * 2 - 1
};

/* Bitset representing status of connection. */
enum ip_conntrack_status {
	/* It's an expected connection: bit 0 set.  This bit never changed */
	IPS_EXPECTED_BIT = 0,
	IPS_EXPECTED = (1 << IPS_EXPECTED_BIT),

	/* We've seen packets both ways: bit 1 set.  Can be set, not unset. */
	IPS_SEEN_REPLY_BIT = 1,
	IPS_SEEN_REPLY = (1 << IPS_SEEN_REPLY_BIT),

	/* Conntrack should never be early-expired. */
	IPS_ASSURED_BIT = 2,
	IPS_ASSURED = (1 << IPS_ASSURED_BIT),

	/* Connection is confirmed: originating packet has left box */
	IPS_CONFIRMED_BIT = 3,
	IPS_CONFIRMED = (1 << IPS_CONFIRMED_BIT),

	/* Connection needs src nat in orig dir.  This bit never changed. */
	IPS_SRC_NAT_BIT = 4,
	IPS_SRC_NAT = (1 << IPS_SRC_NAT_BIT),

	/* Connection needs dst nat in orig dir.  This bit never changed. */
	IPS_DST_NAT_BIT = 5,
	IPS_DST_NAT = (1 << IPS_DST_NAT_BIT),

	/* Both together. */
	IPS_NAT_MASK = (IPS_DST_NAT | IPS_SRC_NAT),

	/* Connection needs TCP sequence adjusted. */
	IPS_SEQ_ADJUST_BIT = 6,
	IPS_SEQ_ADJUST = (1 << IPS_SEQ_ADJUST_BIT),

	/* NAT initialization bits. */
	IPS_SRC_NAT_DONE_BIT = 7,
	IPS_SRC_NAT_DONE = (1 << IPS_SRC_NAT_DONE_BIT),

	IPS_DST_NAT_DONE_BIT = 8,
	IPS_DST_NAT_DONE = (1 << IPS_DST_NAT_DONE_BIT),

	/* Both together */
	IPS_NAT_DONE_MASK = (IPS_DST_NAT_DONE | IPS_SRC_NAT_DONE),

	/* Connection is dying (removed from lists), can not be unset. */
	IPS_DYING_BIT = 9,
	IPS_DYING = (1 << IPS_DYING_BIT),
};

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

	/* Counter highest bit has been set */
	IPCT_COUNTER_FILLING_BIT = 11,
	IPCT_COUNTER_FILLING = (1 << IPCT_COUNTER_FILLING_BIT),
};

enum ip_conntrack_expect_events {
	IPEXP_NEW_BIT = 0,
	IPEXP_NEW = (1 << IPEXP_NEW_BIT),
};

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
#include <linux/netfilter_ipv4/ip_conntrack_pptp.h>
#include <linux/netfilter_ipv4/ip_conntrack_amanda.h>
#include <linux/netfilter_ipv4/ip_conntrack_ftp.h>
#include <linux/netfilter_ipv4/ip_conntrack_irc.h>

/* per conntrack: application helper private data */
union ip_conntrack_help {
	/* insert conntrack helper private data (master) here */
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

struct ip_conntrack_counter
{
	u_int32_t packets;
	u_int32_t bytes;
};

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

/* call to create an explicit dependency on ip_conntrack. */
extern void need_ip_conntrack(void);

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
 
struct ip_conntrack_stat
{
	unsigned int searched;
	unsigned int found;
	unsigned int new;
	unsigned int invalid;
	unsigned int ignore;
	unsigned int delete;
	unsigned int delete_list;
	unsigned int insert;
	unsigned int insert_failed;
	unsigned int drop;
	unsigned int early_drop;
	unsigned int error;
	unsigned int expect_new;
	unsigned int expect_create;
	unsigned int expect_delete;
};

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
 
extern struct notifier_block *ip_conntrack_chain;
extern struct notifier_block *ip_conntrack_expect_chain;

static inline int ip_conntrack_register_notifier(struct notifier_block *nb)
{
	return notifier_chain_register(&ip_conntrack_chain, nb);
}

static inline int ip_conntrack_unregister_notifier(struct notifier_block *nb)
{
	return notifier_chain_unregister(&ip_conntrack_chain, nb);
}

static inline int 
ip_conntrack_expect_register_notifier(struct notifier_block *nb)
{
	return notifier_chain_register(&ip_conntrack_expect_chain, nb);
}

static inline int
ip_conntrack_expect_unregister_notifier(struct notifier_block *nb)
{
	return notifier_chain_unregister(&ip_conntrack_expect_chain, nb);
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
		notifier_call_chain(&ip_conntrack_chain, event, ct);
}

static inline void 
ip_conntrack_expect_event(enum ip_conntrack_expect_events event,
			  struct ip_conntrack_expect *exp)
{
	notifier_call_chain(&ip_conntrack_expect_chain, event, exp);
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

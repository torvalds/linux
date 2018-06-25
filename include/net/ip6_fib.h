/*
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _IP6_FIB_H
#define _IP6_FIB_H

#include <linux/ipv6_route.h>
#include <linux/rtnetlink.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <net/dst.h>
#include <net/flow.h>
#include <net/netlink.h>
#include <net/inetpeer.h>
#include <net/fib_notifier.h>

#ifdef CONFIG_IPV6_MULTIPLE_TABLES
#define FIB6_TABLE_HASHSZ 256
#else
#define FIB6_TABLE_HASHSZ 1
#endif

#define RT6_DEBUG 2

#if RT6_DEBUG >= 3
#define RT6_TRACE(x...) pr_debug(x)
#else
#define RT6_TRACE(x...) do { ; } while (0)
#endif

struct rt6_info;
struct fib6_info;

struct fib6_config {
	u32		fc_table;
	u32		fc_metric;
	int		fc_dst_len;
	int		fc_src_len;
	int		fc_ifindex;
	u32		fc_flags;
	u32		fc_protocol;
	u16		fc_type;        /* only 8 bits are used */
	u16		fc_delete_all_nh : 1,
			__unused : 15;

	struct in6_addr	fc_dst;
	struct in6_addr	fc_src;
	struct in6_addr	fc_prefsrc;
	struct in6_addr	fc_gateway;

	unsigned long	fc_expires;
	struct nlattr	*fc_mx;
	int		fc_mx_len;
	int		fc_mp_len;
	struct nlattr	*fc_mp;

	struct nl_info	fc_nlinfo;
	struct nlattr	*fc_encap;
	u16		fc_encap_type;
};

struct fib6_node {
	struct fib6_node __rcu	*parent;
	struct fib6_node __rcu	*left;
	struct fib6_node __rcu	*right;
#ifdef CONFIG_IPV6_SUBTREES
	struct fib6_node __rcu	*subtree;
#endif
	struct fib6_info __rcu	*leaf;

	__u16			fn_bit;		/* bit key */
	__u16			fn_flags;
	int			fn_sernum;
	struct fib6_info __rcu	*rr_ptr;
	struct rcu_head		rcu;
};

struct fib6_gc_args {
	int			timeout;
	int			more;
};

#ifndef CONFIG_IPV6_SUBTREES
#define FIB6_SUBTREE(fn)	NULL
#else
#define FIB6_SUBTREE(fn)	(rcu_dereference_protected((fn)->subtree, 1))
#endif

/*
 *	routing information
 *
 */

struct rt6key {
	struct in6_addr	addr;
	int		plen;
};

struct fib6_table;

struct rt6_exception_bucket {
	struct hlist_head	chain;
	int			depth;
};

struct rt6_exception {
	struct hlist_node	hlist;
	struct rt6_info		*rt6i;
	unsigned long		stamp;
	struct rcu_head		rcu;
};

#define FIB6_EXCEPTION_BUCKET_SIZE_SHIFT 10
#define FIB6_EXCEPTION_BUCKET_SIZE (1 << FIB6_EXCEPTION_BUCKET_SIZE_SHIFT)
#define FIB6_MAX_DEPTH 5

struct fib6_nh {
	struct in6_addr		nh_gw;
	struct net_device	*nh_dev;
	struct lwtunnel_state	*nh_lwtstate;

	unsigned int		nh_flags;
	atomic_t		nh_upper_bound;
	int			nh_weight;
};

struct fib6_info {
	struct fib6_table		*fib6_table;
	struct fib6_info __rcu		*fib6_next;
	struct fib6_node __rcu		*fib6_node;

	/* Multipath routes:
	 * siblings is a list of fib6_info that have the the same metric/weight,
	 * destination, but not the same gateway. nsiblings is just a cache
	 * to speed up lookup.
	 */
	struct list_head		fib6_siblings;
	unsigned int			fib6_nsiblings;

	atomic_t			fib6_ref;
	unsigned long			expires;
	struct dst_metrics		*fib6_metrics;
#define fib6_pmtu		fib6_metrics->metrics[RTAX_MTU-1]

	struct rt6key			fib6_dst;
	u32				fib6_flags;
	struct rt6key			fib6_src;
	struct rt6key			fib6_prefsrc;

	struct rt6_info * __percpu	*rt6i_pcpu;
	struct rt6_exception_bucket __rcu *rt6i_exception_bucket;

	u32				fib6_metric;
	u8				fib6_protocol;
	u8				fib6_type;
	u8				exception_bucket_flushed:1,
					should_flush:1,
					dst_nocount:1,
					dst_nopolicy:1,
					dst_host:1,
					unused:3;

	struct fib6_nh			fib6_nh;
	struct rcu_head			rcu;
};

struct rt6_info {
	struct dst_entry		dst;
	struct fib6_info __rcu		*from;

	struct rt6key			rt6i_dst;
	struct rt6key			rt6i_src;
	struct in6_addr			rt6i_gateway;
	struct inet6_dev		*rt6i_idev;
	u32				rt6i_flags;
	struct rt6key			rt6i_prefsrc;

	struct list_head		rt6i_uncached;
	struct uncached_list		*rt6i_uncached_list;

	/* more non-fragment space at head required */
	unsigned short			rt6i_nfheader_len;
};

#define for_each_fib6_node_rt_rcu(fn)					\
	for (rt = rcu_dereference((fn)->leaf); rt;			\
	     rt = rcu_dereference(rt->fib6_next))

#define for_each_fib6_walker_rt(w)					\
	for (rt = (w)->leaf; rt;					\
	     rt = rcu_dereference_protected(rt->fib6_next, 1))

static inline struct inet6_dev *ip6_dst_idev(struct dst_entry *dst)
{
	return ((struct rt6_info *)dst)->rt6i_idev;
}

static inline void fib6_clean_expires(struct fib6_info *f6i)
{
	f6i->fib6_flags &= ~RTF_EXPIRES;
	f6i->expires = 0;
}

static inline void fib6_set_expires(struct fib6_info *f6i,
				    unsigned long expires)
{
	f6i->expires = expires;
	f6i->fib6_flags |= RTF_EXPIRES;
}

static inline bool fib6_check_expired(const struct fib6_info *f6i)
{
	if (f6i->fib6_flags & RTF_EXPIRES)
		return time_after(jiffies, f6i->expires);
	return false;
}

/* Function to safely get fn->sernum for passed in rt
 * and store result in passed in cookie.
 * Return true if we can get cookie safely
 * Return false if not
 */
static inline bool fib6_get_cookie_safe(const struct fib6_info *f6i,
					u32 *cookie)
{
	struct fib6_node *fn;
	bool status = false;

	fn = rcu_dereference(f6i->fib6_node);

	if (fn) {
		*cookie = fn->fn_sernum;
		/* pairs with smp_wmb() in fib6_update_sernum_upto_root() */
		smp_rmb();
		status = true;
	}

	return status;
}

static inline u32 rt6_get_cookie(const struct rt6_info *rt)
{
	struct fib6_info *from;
	u32 cookie = 0;

	rcu_read_lock();

	from = rcu_dereference(rt->from);
	if (from && (rt->rt6i_flags & RTF_PCPU ||
	    unlikely(!list_empty(&rt->rt6i_uncached))))
		fib6_get_cookie_safe(from, &cookie);

	rcu_read_unlock();

	return cookie;
}

static inline void ip6_rt_put(struct rt6_info *rt)
{
	/* dst_release() accepts a NULL parameter.
	 * We rely on dst being first structure in struct rt6_info
	 */
	BUILD_BUG_ON(offsetof(struct rt6_info, dst) != 0);
	dst_release(&rt->dst);
}

struct fib6_info *fib6_info_alloc(gfp_t gfp_flags);
void fib6_info_destroy_rcu(struct rcu_head *head);

static inline void fib6_info_hold(struct fib6_info *f6i)
{
	atomic_inc(&f6i->fib6_ref);
}

static inline void fib6_info_release(struct fib6_info *f6i)
{
	if (f6i && atomic_dec_and_test(&f6i->fib6_ref))
		call_rcu(&f6i->rcu, fib6_info_destroy_rcu);
}

enum fib6_walk_state {
#ifdef CONFIG_IPV6_SUBTREES
	FWS_S,
#endif
	FWS_L,
	FWS_R,
	FWS_C,
	FWS_U
};

struct fib6_walker {
	struct list_head lh;
	struct fib6_node *root, *node;
	struct fib6_info *leaf;
	enum fib6_walk_state state;
	unsigned int skip;
	unsigned int count;
	int (*func)(struct fib6_walker *);
	void *args;
};

struct rt6_statistics {
	__u32		fib_nodes;		/* all fib6 nodes */
	__u32		fib_route_nodes;	/* intermediate nodes */
	__u32		fib_rt_entries;		/* rt entries in fib table */
	__u32		fib_rt_cache;		/* cached rt entries in exception table */
	__u32		fib_discarded_routes;	/* total number of routes delete */

	/* The following stats are not protected by any lock */
	atomic_t	fib_rt_alloc;		/* total number of routes alloced */
	atomic_t	fib_rt_uncache;		/* rt entries in uncached list */
};

#define RTN_TL_ROOT	0x0001
#define RTN_ROOT	0x0002		/* tree root node		*/
#define RTN_RTINFO	0x0004		/* node with valid routing info	*/

/*
 *	priority levels (or metrics)
 *
 */


struct fib6_table {
	struct hlist_node	tb6_hlist;
	u32			tb6_id;
	spinlock_t		tb6_lock;
	struct fib6_node	tb6_root;
	struct inet_peer_base	tb6_peers;
	unsigned int		flags;
	unsigned int		fib_seq;
#define RT6_TABLE_HAS_DFLT_ROUTER	BIT(0)
};

#define RT6_TABLE_UNSPEC	RT_TABLE_UNSPEC
#define RT6_TABLE_MAIN		RT_TABLE_MAIN
#define RT6_TABLE_DFLT		RT6_TABLE_MAIN
#define RT6_TABLE_INFO		RT6_TABLE_MAIN
#define RT6_TABLE_PREFIX	RT6_TABLE_MAIN

#ifdef CONFIG_IPV6_MULTIPLE_TABLES
#define FIB6_TABLE_MIN		1
#define FIB6_TABLE_MAX		RT_TABLE_MAX
#define RT6_TABLE_LOCAL		RT_TABLE_LOCAL
#else
#define FIB6_TABLE_MIN		RT_TABLE_MAIN
#define FIB6_TABLE_MAX		FIB6_TABLE_MIN
#define RT6_TABLE_LOCAL		RT6_TABLE_MAIN
#endif

typedef struct rt6_info *(*pol_lookup_t)(struct net *,
					 struct fib6_table *,
					 struct flowi6 *,
					 const struct sk_buff *, int);

struct fib6_entry_notifier_info {
	struct fib_notifier_info info; /* must be first */
	struct fib6_info *rt;
};

/*
 *	exported functions
 */

struct fib6_table *fib6_get_table(struct net *net, u32 id);
struct fib6_table *fib6_new_table(struct net *net, u32 id);
struct dst_entry *fib6_rule_lookup(struct net *net, struct flowi6 *fl6,
				   const struct sk_buff *skb,
				   int flags, pol_lookup_t lookup);

/* called with rcu lock held; can return error pointer
 * caller needs to select path
 */
struct fib6_info *fib6_lookup(struct net *net, int oif, struct flowi6 *fl6,
			      int flags);

/* called with rcu lock held; caller needs to select path */
struct fib6_info *fib6_table_lookup(struct net *net, struct fib6_table *table,
				    int oif, struct flowi6 *fl6, int strict);

struct fib6_info *fib6_multipath_select(const struct net *net,
					struct fib6_info *match,
					struct flowi6 *fl6, int oif,
					const struct sk_buff *skb, int strict);

struct fib6_node *fib6_node_lookup(struct fib6_node *root,
				   const struct in6_addr *daddr,
				   const struct in6_addr *saddr);

struct fib6_node *fib6_locate(struct fib6_node *root,
			      const struct in6_addr *daddr, int dst_len,
			      const struct in6_addr *saddr, int src_len,
			      bool exact_match);

void fib6_clean_all(struct net *net, int (*func)(struct fib6_info *, void *arg),
		    void *arg);

int fib6_add(struct fib6_node *root, struct fib6_info *rt,
	     struct nl_info *info, struct netlink_ext_ack *extack);
int fib6_del(struct fib6_info *rt, struct nl_info *info);

static inline struct net_device *fib6_info_nh_dev(const struct fib6_info *f6i)
{
	return f6i->fib6_nh.nh_dev;
}

static inline
struct lwtunnel_state *fib6_info_nh_lwt(const struct fib6_info *f6i)
{
	return f6i->fib6_nh.nh_lwtstate;
}

void inet6_rt_notify(int event, struct fib6_info *rt, struct nl_info *info,
		     unsigned int flags);

void fib6_run_gc(unsigned long expires, struct net *net, bool force);

void fib6_gc_cleanup(void);

int fib6_init(void);

struct ipv6_route_iter {
	struct seq_net_private p;
	struct fib6_walker w;
	loff_t skip;
	struct fib6_table *tbl;
	int sernum;
};

extern const struct seq_operations ipv6_route_seq_ops;

int call_fib6_notifier(struct notifier_block *nb, struct net *net,
		       enum fib_event_type event_type,
		       struct fib_notifier_info *info);
int call_fib6_notifiers(struct net *net, enum fib_event_type event_type,
			struct fib_notifier_info *info);

int __net_init fib6_notifier_init(struct net *net);
void __net_exit fib6_notifier_exit(struct net *net);

unsigned int fib6_tables_seq_read(struct net *net);
int fib6_tables_dump(struct net *net, struct notifier_block *nb);

void fib6_update_sernum(struct net *net, struct fib6_info *rt);
void fib6_update_sernum_upto_root(struct net *net, struct fib6_info *rt);

void fib6_metric_set(struct fib6_info *f6i, int metric, u32 val);
static inline bool fib6_metric_locked(struct fib6_info *f6i, int metric)
{
	return !!(f6i->fib6_metrics->metrics[RTAX_LOCK - 1] & (1 << metric));
}

#ifdef CONFIG_IPV6_MULTIPLE_TABLES
int fib6_rules_init(void);
void fib6_rules_cleanup(void);
bool fib6_rule_default(const struct fib_rule *rule);
int fib6_rules_dump(struct net *net, struct notifier_block *nb);
unsigned int fib6_rules_seq_read(struct net *net);

static inline bool fib6_rules_early_flow_dissect(struct net *net,
						 struct sk_buff *skb,
						 struct flowi6 *fl6,
						 struct flow_keys *flkeys)
{
	unsigned int flag = FLOW_DISSECTOR_F_STOP_AT_ENCAP;

	if (!net->ipv6.fib6_rules_require_fldissect)
		return false;

	skb_flow_dissect_flow_keys(skb, flkeys, flag);
	fl6->fl6_sport = flkeys->ports.src;
	fl6->fl6_dport = flkeys->ports.dst;
	fl6->flowi6_proto = flkeys->basic.ip_proto;

	return true;
}
#else
static inline int               fib6_rules_init(void)
{
	return 0;
}
static inline void              fib6_rules_cleanup(void)
{
	return ;
}
static inline bool fib6_rule_default(const struct fib_rule *rule)
{
	return true;
}
static inline int fib6_rules_dump(struct net *net, struct notifier_block *nb)
{
	return 0;
}
static inline unsigned int fib6_rules_seq_read(struct net *net)
{
	return 0;
}
static inline bool fib6_rules_early_flow_dissect(struct net *net,
						 struct sk_buff *skb,
						 struct flowi6 *fl6,
						 struct flow_keys *flkeys)
{
	return false;
}
#endif
#endif

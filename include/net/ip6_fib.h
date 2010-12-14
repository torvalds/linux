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

#ifdef __KERNEL__

#include <linux/ipv6_route.h>
#include <linux/rtnetlink.h>
#include <linux/spinlock.h>
#include <net/dst.h>
#include <net/flow.h>
#include <net/netlink.h>

#ifdef CONFIG_IPV6_MULTIPLE_TABLES
#define FIB6_TABLE_HASHSZ 256
#else
#define FIB6_TABLE_HASHSZ 1
#endif

struct rt6_info;

struct fib6_config {
	u32		fc_table;
	u32		fc_metric;
	int		fc_dst_len;
	int		fc_src_len;
	int		fc_ifindex;
	u32		fc_flags;
	u32		fc_protocol;

	struct in6_addr	fc_dst;
	struct in6_addr	fc_src;
	struct in6_addr	fc_gateway;

	unsigned long	fc_expires;
	struct nlattr	*fc_mx;
	int		fc_mx_len;

	struct nl_info	fc_nlinfo;
};

struct fib6_node {
	struct fib6_node	*parent;
	struct fib6_node	*left;
	struct fib6_node	*right;
#ifdef CONFIG_IPV6_SUBTREES
	struct fib6_node	*subtree;
#endif
	struct rt6_info		*leaf;

	__u16			fn_bit;		/* bit key */
	__u16			fn_flags;
	__u32			fn_sernum;
	struct rt6_info		*rr_ptr;
};

#ifndef CONFIG_IPV6_SUBTREES
#define FIB6_SUBTREE(fn)	NULL
#else
#define FIB6_SUBTREE(fn)	((fn)->subtree)
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

struct rt6_info {
	struct dst_entry		dst;

#define rt6i_dev			dst.dev
#define rt6i_nexthop			dst.neighbour
#define rt6i_expires			dst.expires

	/*
	 * Tail elements of dst_entry (__refcnt etc.)
	 * and these elements (rarely used in hot path) are in
	 * the same cache line.
	 */
	struct fib6_table		*rt6i_table;
	struct fib6_node		*rt6i_node;

	struct in6_addr			rt6i_gateway;

	atomic_t			rt6i_ref;

	/* These are in a separate cache line. */
	struct rt6key			rt6i_dst ____cacheline_aligned_in_smp;
	u32				rt6i_flags;
	struct rt6key			rt6i_src;
	u32				rt6i_metric;

	struct inet6_dev		*rt6i_idev;

#ifdef CONFIG_XFRM
	u32				rt6i_flow_cache_genid;
#endif
	/* more non-fragment space at head required */
	unsigned short			rt6i_nfheader_len;

	u8				rt6i_protocol;
};

static inline struct inet6_dev *ip6_dst_idev(struct dst_entry *dst)
{
	return ((struct rt6_info *)dst)->rt6i_idev;
}

struct fib6_walker_t {
	struct list_head lh;
	struct fib6_node *root, *node;
	struct rt6_info *leaf;
	unsigned char state;
	unsigned char prune;
	unsigned int skip;
	unsigned int count;
	int (*func)(struct fib6_walker_t *);
	void *args;
};

struct rt6_statistics {
	__u32		fib_nodes;
	__u32		fib_route_nodes;
	__u32		fib_rt_alloc;		/* permanent routes	*/
	__u32		fib_rt_entries;		/* rt entries in table	*/
	__u32		fib_rt_cache;		/* cache routes		*/
	__u32		fib_discarded_routes;
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
	rwlock_t		tb6_lock;
	struct fib6_node	tb6_root;
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
					 struct flowi *, int);

/*
 *	exported functions
 */

extern struct fib6_table        *fib6_get_table(struct net *net, u32 id);
extern struct fib6_table        *fib6_new_table(struct net *net, u32 id);
extern struct dst_entry         *fib6_rule_lookup(struct net *net,
						  struct flowi *fl, int flags,
						  pol_lookup_t lookup);

extern struct fib6_node		*fib6_lookup(struct fib6_node *root,
					     struct in6_addr *daddr,
					     struct in6_addr *saddr);

struct fib6_node		*fib6_locate(struct fib6_node *root,
					     struct in6_addr *daddr, int dst_len,
					     struct in6_addr *saddr, int src_len);

extern void			fib6_clean_all(struct net *net,
					       int (*func)(struct rt6_info *, void *arg),
					       int prune, void *arg);

extern int			fib6_add(struct fib6_node *root,
					 struct rt6_info *rt,
					 struct nl_info *info);

extern int			fib6_del(struct rt6_info *rt,
					 struct nl_info *info);

extern void			inet6_rt_notify(int event, struct rt6_info *rt,
						struct nl_info *info);

extern void			fib6_run_gc(unsigned long expires,
					    struct net *net);

extern void			fib6_gc_cleanup(void);

extern int			fib6_init(void);

#ifdef CONFIG_IPV6_MULTIPLE_TABLES
extern int			fib6_rules_init(void);
extern void			fib6_rules_cleanup(void);
#else
static inline int               fib6_rules_init(void)
{
	return 0;
}
static inline void              fib6_rules_cleanup(void)
{
	return ;
}
#endif
#endif
#endif

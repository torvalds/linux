/*
 *	Linux INET6 implementation
 *	Forwarding Information Database
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	$Id: ip6_fib.c,v 1.25 2001/10/31 21:55:55 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

/*
 * 	Changes:
 * 	Yuji SEKIYA @USAGI:	Support default route on router node;
 * 				remove ip6_null_entry from the top of
 * 				routing table.
 * 	Ville Nuorvala:		Fixed routing subtrees.
 */
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/net.h>
#include <linux/route.h>
#include <linux/netdevice.h>
#include <linux/in6.h>
#include <linux/init.h>
#include <linux/list.h>

#ifdef 	CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif

#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/addrconf.h>

#include <net/ip6_fib.h>
#include <net/ip6_route.h>

#define RT6_DEBUG 2

#if RT6_DEBUG >= 3
#define RT6_TRACE(x...) printk(KERN_DEBUG x)
#else
#define RT6_TRACE(x...) do { ; } while (0)
#endif

struct rt6_statistics	rt6_stats;

static struct kmem_cache * fib6_node_kmem __read_mostly;

enum fib_walk_state_t
{
#ifdef CONFIG_IPV6_SUBTREES
	FWS_S,
#endif
	FWS_L,
	FWS_R,
	FWS_C,
	FWS_U
};

struct fib6_cleaner_t
{
	struct fib6_walker_t w;
	int (*func)(struct rt6_info *, void *arg);
	void *arg;
};

static DEFINE_RWLOCK(fib6_walker_lock);

#ifdef CONFIG_IPV6_SUBTREES
#define FWS_INIT FWS_S
#else
#define FWS_INIT FWS_L
#endif

static void fib6_prune_clones(struct fib6_node *fn, struct rt6_info *rt);
static struct rt6_info * fib6_find_prefix(struct fib6_node *fn);
static struct fib6_node * fib6_repair_tree(struct fib6_node *fn);
static int fib6_walk(struct fib6_walker_t *w);
static int fib6_walk_continue(struct fib6_walker_t *w);

/*
 *	A routing update causes an increase of the serial number on the
 *	affected subtree. This allows for cached routes to be asynchronously
 *	tested when modifications are made to the destination cache as a
 *	result of redirects, path MTU changes, etc.
 */

static __u32 rt_sernum;

static DEFINE_TIMER(ip6_fib_timer, fib6_run_gc, 0, 0);

static struct fib6_walker_t fib6_walker_list = {
	.prev	= &fib6_walker_list,
	.next	= &fib6_walker_list,
};

#define FOR_WALKERS(w) for ((w)=fib6_walker_list.next; (w) != &fib6_walker_list; (w)=(w)->next)

static inline void fib6_walker_link(struct fib6_walker_t *w)
{
	write_lock_bh(&fib6_walker_lock);
	w->next = fib6_walker_list.next;
	w->prev = &fib6_walker_list;
	w->next->prev = w;
	w->prev->next = w;
	write_unlock_bh(&fib6_walker_lock);
}

static inline void fib6_walker_unlink(struct fib6_walker_t *w)
{
	write_lock_bh(&fib6_walker_lock);
	w->next->prev = w->prev;
	w->prev->next = w->next;
	w->prev = w->next = w;
	write_unlock_bh(&fib6_walker_lock);
}
static __inline__ u32 fib6_new_sernum(void)
{
	u32 n = ++rt_sernum;
	if ((__s32)n <= 0)
		rt_sernum = n = 1;
	return n;
}

/*
 *	Auxiliary address test functions for the radix tree.
 *
 *	These assume a 32bit processor (although it will work on
 *	64bit processors)
 */

/*
 *	test bit
 */

static __inline__ __be32 addr_bit_set(void *token, int fn_bit)
{
	__be32 *addr = token;

	return htonl(1 << ((~fn_bit)&0x1F)) & addr[fn_bit>>5];
}

static __inline__ struct fib6_node * node_alloc(void)
{
	struct fib6_node *fn;

	fn = kmem_cache_zalloc(fib6_node_kmem, GFP_ATOMIC);

	return fn;
}

static __inline__ void node_free(struct fib6_node * fn)
{
	kmem_cache_free(fib6_node_kmem, fn);
}

static __inline__ void rt6_release(struct rt6_info *rt)
{
	if (atomic_dec_and_test(&rt->rt6i_ref))
		dst_free(&rt->u.dst);
}

static struct fib6_table fib6_main_tbl = {
	.tb6_id		= RT6_TABLE_MAIN,
	.tb6_root	= {
		.leaf		= &ip6_null_entry,
		.fn_flags	= RTN_ROOT | RTN_TL_ROOT | RTN_RTINFO,
	},
};

#ifdef CONFIG_IPV6_MULTIPLE_TABLES
#define FIB_TABLE_HASHSZ 256
#else
#define FIB_TABLE_HASHSZ 1
#endif
static struct hlist_head fib_table_hash[FIB_TABLE_HASHSZ];

static void fib6_link_table(struct fib6_table *tb)
{
	unsigned int h;

	/*
	 * Initialize table lock at a single place to give lockdep a key,
	 * tables aren't visible prior to being linked to the list.
	 */
	rwlock_init(&tb->tb6_lock);

	h = tb->tb6_id & (FIB_TABLE_HASHSZ - 1);

	/*
	 * No protection necessary, this is the only list mutatation
	 * operation, tables never disappear once they exist.
	 */
	hlist_add_head_rcu(&tb->tb6_hlist, &fib_table_hash[h]);
}

#ifdef CONFIG_IPV6_MULTIPLE_TABLES
static struct fib6_table fib6_local_tbl = {
	.tb6_id		= RT6_TABLE_LOCAL,
	.tb6_root 	= {
		.leaf		= &ip6_null_entry,
		.fn_flags	= RTN_ROOT | RTN_TL_ROOT | RTN_RTINFO,
	},
};

static struct fib6_table *fib6_alloc_table(u32 id)
{
	struct fib6_table *table;

	table = kzalloc(sizeof(*table), GFP_ATOMIC);
	if (table != NULL) {
		table->tb6_id = id;
		table->tb6_root.leaf = &ip6_null_entry;
		table->tb6_root.fn_flags = RTN_ROOT | RTN_TL_ROOT | RTN_RTINFO;
	}

	return table;
}

struct fib6_table *fib6_new_table(u32 id)
{
	struct fib6_table *tb;

	if (id == 0)
		id = RT6_TABLE_MAIN;
	tb = fib6_get_table(id);
	if (tb)
		return tb;

	tb = fib6_alloc_table(id);
	if (tb != NULL)
		fib6_link_table(tb);

	return tb;
}

struct fib6_table *fib6_get_table(u32 id)
{
	struct fib6_table *tb;
	struct hlist_node *node;
	unsigned int h;

	if (id == 0)
		id = RT6_TABLE_MAIN;
	h = id & (FIB_TABLE_HASHSZ - 1);
	rcu_read_lock();
	hlist_for_each_entry_rcu(tb, node, &fib_table_hash[h], tb6_hlist) {
		if (tb->tb6_id == id) {
			rcu_read_unlock();
			return tb;
		}
	}
	rcu_read_unlock();

	return NULL;
}

static void __init fib6_tables_init(void)
{
	fib6_link_table(&fib6_main_tbl);
	fib6_link_table(&fib6_local_tbl);
}

#else

struct fib6_table *fib6_new_table(u32 id)
{
	return fib6_get_table(id);
}

struct fib6_table *fib6_get_table(u32 id)
{
	return &fib6_main_tbl;
}

struct dst_entry *fib6_rule_lookup(struct flowi *fl, int flags,
				   pol_lookup_t lookup)
{
	return (struct dst_entry *) lookup(&fib6_main_tbl, fl, flags);
}

static void __init fib6_tables_init(void)
{
	fib6_link_table(&fib6_main_tbl);
}

#endif

static int fib6_dump_node(struct fib6_walker_t *w)
{
	int res;
	struct rt6_info *rt;

	for (rt = w->leaf; rt; rt = rt->u.dst.rt6_next) {
		res = rt6_dump_route(rt, w->args);
		if (res < 0) {
			/* Frame is full, suspend walking */
			w->leaf = rt;
			return 1;
		}
		BUG_TRAP(res!=0);
	}
	w->leaf = NULL;
	return 0;
}

static void fib6_dump_end(struct netlink_callback *cb)
{
	struct fib6_walker_t *w = (void*)cb->args[2];

	if (w) {
		cb->args[2] = 0;
		kfree(w);
	}
	cb->done = (void*)cb->args[3];
	cb->args[1] = 3;
}

static int fib6_dump_done(struct netlink_callback *cb)
{
	fib6_dump_end(cb);
	return cb->done ? cb->done(cb) : 0;
}

static int fib6_dump_table(struct fib6_table *table, struct sk_buff *skb,
			   struct netlink_callback *cb)
{
	struct fib6_walker_t *w;
	int res;

	w = (void *)cb->args[2];
	w->root = &table->tb6_root;

	if (cb->args[4] == 0) {
		read_lock_bh(&table->tb6_lock);
		res = fib6_walk(w);
		read_unlock_bh(&table->tb6_lock);
		if (res > 0)
			cb->args[4] = 1;
	} else {
		read_lock_bh(&table->tb6_lock);
		res = fib6_walk_continue(w);
		read_unlock_bh(&table->tb6_lock);
		if (res != 0) {
			if (res < 0)
				fib6_walker_unlink(w);
			goto end;
		}
		fib6_walker_unlink(w);
		cb->args[4] = 0;
	}
end:
	return res;
}

static int inet6_dump_fib(struct sk_buff *skb, struct netlink_callback *cb)
{
	unsigned int h, s_h;
	unsigned int e = 0, s_e;
	struct rt6_rtnl_dump_arg arg;
	struct fib6_walker_t *w;
	struct fib6_table *tb;
	struct hlist_node *node;
	int res = 0;

	s_h = cb->args[0];
	s_e = cb->args[1];

	w = (void *)cb->args[2];
	if (w == NULL) {
		/* New dump:
		 *
		 * 1. hook callback destructor.
		 */
		cb->args[3] = (long)cb->done;
		cb->done = fib6_dump_done;

		/*
		 * 2. allocate and initialize walker.
		 */
		w = kzalloc(sizeof(*w), GFP_ATOMIC);
		if (w == NULL)
			return -ENOMEM;
		w->func = fib6_dump_node;
		cb->args[2] = (long)w;
	}

	arg.skb = skb;
	arg.cb = cb;
	w->args = &arg;

	for (h = s_h; h < FIB_TABLE_HASHSZ; h++, s_e = 0) {
		e = 0;
		hlist_for_each_entry(tb, node, &fib_table_hash[h], tb6_hlist) {
			if (e < s_e)
				goto next;
			res = fib6_dump_table(tb, skb, cb);
			if (res != 0)
				goto out;
next:
			e++;
		}
	}
out:
	cb->args[1] = e;
	cb->args[0] = h;

	res = res < 0 ? res : skb->len;
	if (res <= 0)
		fib6_dump_end(cb);
	return res;
}

/*
 *	Routing Table
 *
 *	return the appropriate node for a routing tree "add" operation
 *	by either creating and inserting or by returning an existing
 *	node.
 */

static struct fib6_node * fib6_add_1(struct fib6_node *root, void *addr,
				     int addrlen, int plen,
				     int offset)
{
	struct fib6_node *fn, *in, *ln;
	struct fib6_node *pn = NULL;
	struct rt6key *key;
	int	bit;
	__be32	dir = 0;
	__u32	sernum = fib6_new_sernum();

	RT6_TRACE("fib6_add_1\n");

	/* insert node in tree */

	fn = root;

	do {
		key = (struct rt6key *)((u8 *)fn->leaf + offset);

		/*
		 *	Prefix match
		 */
		if (plen < fn->fn_bit ||
		    !ipv6_prefix_equal(&key->addr, addr, fn->fn_bit))
			goto insert_above;

		/*
		 *	Exact match ?
		 */

		if (plen == fn->fn_bit) {
			/* clean up an intermediate node */
			if ((fn->fn_flags & RTN_RTINFO) == 0) {
				rt6_release(fn->leaf);
				fn->leaf = NULL;
			}

			fn->fn_sernum = sernum;

			return fn;
		}

		/*
		 *	We have more bits to go
		 */

		/* Try to walk down on tree. */
		fn->fn_sernum = sernum;
		dir = addr_bit_set(addr, fn->fn_bit);
		pn = fn;
		fn = dir ? fn->right: fn->left;
	} while (fn);

	/*
	 *	We walked to the bottom of tree.
	 *	Create new leaf node without children.
	 */

	ln = node_alloc();

	if (ln == NULL)
		return NULL;
	ln->fn_bit = plen;

	ln->parent = pn;
	ln->fn_sernum = sernum;

	if (dir)
		pn->right = ln;
	else
		pn->left  = ln;

	return ln;


insert_above:
	/*
	 * split since we don't have a common prefix anymore or
	 * we have a less significant route.
	 * we've to insert an intermediate node on the list
	 * this new node will point to the one we need to create
	 * and the current
	 */

	pn = fn->parent;

	/* find 1st bit in difference between the 2 addrs.

	   See comment in __ipv6_addr_diff: bit may be an invalid value,
	   but if it is >= plen, the value is ignored in any case.
	 */

	bit = __ipv6_addr_diff(addr, &key->addr, addrlen);

	/*
	 *		(intermediate)[in]
	 *	          /	   \
	 *	(new leaf node)[ln] (old node)[fn]
	 */
	if (plen > bit) {
		in = node_alloc();
		ln = node_alloc();

		if (in == NULL || ln == NULL) {
			if (in)
				node_free(in);
			if (ln)
				node_free(ln);
			return NULL;
		}

		/*
		 * new intermediate node.
		 * RTN_RTINFO will
		 * be off since that an address that chooses one of
		 * the branches would not match less specific routes
		 * in the other branch
		 */

		in->fn_bit = bit;

		in->parent = pn;
		in->leaf = fn->leaf;
		atomic_inc(&in->leaf->rt6i_ref);

		in->fn_sernum = sernum;

		/* update parent pointer */
		if (dir)
			pn->right = in;
		else
			pn->left  = in;

		ln->fn_bit = plen;

		ln->parent = in;
		fn->parent = in;

		ln->fn_sernum = sernum;

		if (addr_bit_set(addr, bit)) {
			in->right = ln;
			in->left  = fn;
		} else {
			in->left  = ln;
			in->right = fn;
		}
	} else { /* plen <= bit */

		/*
		 *		(new leaf node)[ln]
		 *	          /	   \
		 *	     (old node)[fn] NULL
		 */

		ln = node_alloc();

		if (ln == NULL)
			return NULL;

		ln->fn_bit = plen;

		ln->parent = pn;

		ln->fn_sernum = sernum;

		if (dir)
			pn->right = ln;
		else
			pn->left  = ln;

		if (addr_bit_set(&key->addr, plen))
			ln->right = fn;
		else
			ln->left  = fn;

		fn->parent = ln;
	}
	return ln;
}

/*
 *	Insert routing information in a node.
 */

static int fib6_add_rt2node(struct fib6_node *fn, struct rt6_info *rt,
			    struct nl_info *info)
{
	struct rt6_info *iter = NULL;
	struct rt6_info **ins;

	ins = &fn->leaf;

	if (fn->fn_flags&RTN_TL_ROOT &&
	    fn->leaf == &ip6_null_entry &&
	    !(rt->rt6i_flags & (RTF_DEFAULT | RTF_ADDRCONF)) ){
		fn->leaf = rt;
		rt->u.dst.rt6_next = NULL;
		goto out;
	}

	for (iter = fn->leaf; iter; iter=iter->u.dst.rt6_next) {
		/*
		 *	Search for duplicates
		 */

		if (iter->rt6i_metric == rt->rt6i_metric) {
			/*
			 *	Same priority level
			 */

			if (iter->rt6i_dev == rt->rt6i_dev &&
			    iter->rt6i_idev == rt->rt6i_idev &&
			    ipv6_addr_equal(&iter->rt6i_gateway,
					    &rt->rt6i_gateway)) {
				if (!(iter->rt6i_flags&RTF_EXPIRES))
					return -EEXIST;
				iter->rt6i_expires = rt->rt6i_expires;
				if (!(rt->rt6i_flags&RTF_EXPIRES)) {
					iter->rt6i_flags &= ~RTF_EXPIRES;
					iter->rt6i_expires = 0;
				}
				return -EEXIST;
			}
		}

		if (iter->rt6i_metric > rt->rt6i_metric)
			break;

		ins = &iter->u.dst.rt6_next;
	}

	/* Reset round-robin state, if necessary */
	if (ins == &fn->leaf)
		fn->rr_ptr = NULL;

	/*
	 *	insert node
	 */

out:
	rt->u.dst.rt6_next = iter;
	*ins = rt;
	rt->rt6i_node = fn;
	atomic_inc(&rt->rt6i_ref);
	inet6_rt_notify(RTM_NEWROUTE, rt, info);
	rt6_stats.fib_rt_entries++;

	if ((fn->fn_flags & RTN_RTINFO) == 0) {
		rt6_stats.fib_route_nodes++;
		fn->fn_flags |= RTN_RTINFO;
	}

	return 0;
}

static __inline__ void fib6_start_gc(struct rt6_info *rt)
{
	if (ip6_fib_timer.expires == 0 &&
	    (rt->rt6i_flags & (RTF_EXPIRES|RTF_CACHE)))
		mod_timer(&ip6_fib_timer, jiffies + ip6_rt_gc_interval);
}

void fib6_force_start_gc(void)
{
	if (ip6_fib_timer.expires == 0)
		mod_timer(&ip6_fib_timer, jiffies + ip6_rt_gc_interval);
}

/*
 *	Add routing information to the routing tree.
 *	<destination addr>/<source addr>
 *	with source addr info in sub-trees
 */

int fib6_add(struct fib6_node *root, struct rt6_info *rt, struct nl_info *info)
{
	struct fib6_node *fn, *pn = NULL;
	int err = -ENOMEM;

	fn = fib6_add_1(root, &rt->rt6i_dst.addr, sizeof(struct in6_addr),
			rt->rt6i_dst.plen, offsetof(struct rt6_info, rt6i_dst));

	if (fn == NULL)
		goto out;

	pn = fn;

#ifdef CONFIG_IPV6_SUBTREES
	if (rt->rt6i_src.plen) {
		struct fib6_node *sn;

		if (fn->subtree == NULL) {
			struct fib6_node *sfn;

			/*
			 * Create subtree.
			 *
			 *		fn[main tree]
			 *		|
			 *		sfn[subtree root]
			 *		   \
			 *		    sn[new leaf node]
			 */

			/* Create subtree root node */
			sfn = node_alloc();
			if (sfn == NULL)
				goto st_failure;

			sfn->leaf = &ip6_null_entry;
			atomic_inc(&ip6_null_entry.rt6i_ref);
			sfn->fn_flags = RTN_ROOT;
			sfn->fn_sernum = fib6_new_sernum();

			/* Now add the first leaf node to new subtree */

			sn = fib6_add_1(sfn, &rt->rt6i_src.addr,
					sizeof(struct in6_addr), rt->rt6i_src.plen,
					offsetof(struct rt6_info, rt6i_src));

			if (sn == NULL) {
				/* If it is failed, discard just allocated
				   root, and then (in st_failure) stale node
				   in main tree.
				 */
				node_free(sfn);
				goto st_failure;
			}

			/* Now link new subtree to main tree */
			sfn->parent = fn;
			fn->subtree = sfn;
		} else {
			sn = fib6_add_1(fn->subtree, &rt->rt6i_src.addr,
					sizeof(struct in6_addr), rt->rt6i_src.plen,
					offsetof(struct rt6_info, rt6i_src));

			if (sn == NULL)
				goto st_failure;
		}

		if (fn->leaf == NULL) {
			fn->leaf = rt;
			atomic_inc(&rt->rt6i_ref);
		}
		fn = sn;
	}
#endif

	err = fib6_add_rt2node(fn, rt, info);

	if (err == 0) {
		fib6_start_gc(rt);
		if (!(rt->rt6i_flags&RTF_CACHE))
			fib6_prune_clones(pn, rt);
	}

out:
	if (err) {
#ifdef CONFIG_IPV6_SUBTREES
		/*
		 * If fib6_add_1 has cleared the old leaf pointer in the
		 * super-tree leaf node we have to find a new one for it.
		 */
		if (pn != fn && !pn->leaf && !(pn->fn_flags & RTN_RTINFO)) {
			pn->leaf = fib6_find_prefix(pn);
#if RT6_DEBUG >= 2
			if (!pn->leaf) {
				BUG_TRAP(pn->leaf != NULL);
				pn->leaf = &ip6_null_entry;
			}
#endif
			atomic_inc(&pn->leaf->rt6i_ref);
		}
#endif
		dst_free(&rt->u.dst);
	}
	return err;

#ifdef CONFIG_IPV6_SUBTREES
	/* Subtree creation failed, probably main tree node
	   is orphan. If it is, shoot it.
	 */
st_failure:
	if (fn && !(fn->fn_flags & (RTN_RTINFO|RTN_ROOT)))
		fib6_repair_tree(fn);
	dst_free(&rt->u.dst);
	return err;
#endif
}

/*
 *	Routing tree lookup
 *
 */

struct lookup_args {
	int		offset;		/* key offset on rt6_info	*/
	struct in6_addr	*addr;		/* search key			*/
};

static struct fib6_node * fib6_lookup_1(struct fib6_node *root,
					struct lookup_args *args)
{
	struct fib6_node *fn;
	__be32 dir;

	if (unlikely(args->offset == 0))
		return NULL;

	/*
	 *	Descend on a tree
	 */

	fn = root;

	for (;;) {
		struct fib6_node *next;

		dir = addr_bit_set(args->addr, fn->fn_bit);

		next = dir ? fn->right : fn->left;

		if (next) {
			fn = next;
			continue;
		}

		break;
	}

	while(fn) {
		if (FIB6_SUBTREE(fn) || fn->fn_flags & RTN_RTINFO) {
			struct rt6key *key;

			key = (struct rt6key *) ((u8 *) fn->leaf +
						 args->offset);

			if (ipv6_prefix_equal(&key->addr, args->addr, key->plen)) {
#ifdef CONFIG_IPV6_SUBTREES
				if (fn->subtree)
					fn = fib6_lookup_1(fn->subtree, args + 1);
#endif
				if (!fn || fn->fn_flags & RTN_RTINFO)
					return fn;
			}
		}

		if (fn->fn_flags & RTN_ROOT)
			break;

		fn = fn->parent;
	}

	return NULL;
}

struct fib6_node * fib6_lookup(struct fib6_node *root, struct in6_addr *daddr,
			       struct in6_addr *saddr)
{
	struct fib6_node *fn;
	struct lookup_args args[] = {
		{
			.offset = offsetof(struct rt6_info, rt6i_dst),
			.addr = daddr,
		},
#ifdef CONFIG_IPV6_SUBTREES
		{
			.offset = offsetof(struct rt6_info, rt6i_src),
			.addr = saddr,
		},
#endif
		{
			.offset = 0,	/* sentinel */
		}
	};

	fn = fib6_lookup_1(root, daddr ? args : args + 1);

	if (fn == NULL || fn->fn_flags & RTN_TL_ROOT)
		fn = root;

	return fn;
}

/*
 *	Get node with specified destination prefix (and source prefix,
 *	if subtrees are used)
 */


static struct fib6_node * fib6_locate_1(struct fib6_node *root,
					struct in6_addr *addr,
					int plen, int offset)
{
	struct fib6_node *fn;

	for (fn = root; fn ; ) {
		struct rt6key *key = (struct rt6key *)((u8 *)fn->leaf + offset);

		/*
		 *	Prefix match
		 */
		if (plen < fn->fn_bit ||
		    !ipv6_prefix_equal(&key->addr, addr, fn->fn_bit))
			return NULL;

		if (plen == fn->fn_bit)
			return fn;

		/*
		 *	We have more bits to go
		 */
		if (addr_bit_set(addr, fn->fn_bit))
			fn = fn->right;
		else
			fn = fn->left;
	}
	return NULL;
}

struct fib6_node * fib6_locate(struct fib6_node *root,
			       struct in6_addr *daddr, int dst_len,
			       struct in6_addr *saddr, int src_len)
{
	struct fib6_node *fn;

	fn = fib6_locate_1(root, daddr, dst_len,
			   offsetof(struct rt6_info, rt6i_dst));

#ifdef CONFIG_IPV6_SUBTREES
	if (src_len) {
		BUG_TRAP(saddr!=NULL);
		if (fn && fn->subtree)
			fn = fib6_locate_1(fn->subtree, saddr, src_len,
					   offsetof(struct rt6_info, rt6i_src));
	}
#endif

	if (fn && fn->fn_flags&RTN_RTINFO)
		return fn;

	return NULL;
}


/*
 *	Deletion
 *
 */

static struct rt6_info * fib6_find_prefix(struct fib6_node *fn)
{
	if (fn->fn_flags&RTN_ROOT)
		return &ip6_null_entry;

	while(fn) {
		if(fn->left)
			return fn->left->leaf;

		if(fn->right)
			return fn->right->leaf;

		fn = FIB6_SUBTREE(fn);
	}
	return NULL;
}

/*
 *	Called to trim the tree of intermediate nodes when possible. "fn"
 *	is the node we want to try and remove.
 */

static struct fib6_node * fib6_repair_tree(struct fib6_node *fn)
{
	int children;
	int nstate;
	struct fib6_node *child, *pn;
	struct fib6_walker_t *w;
	int iter = 0;

	for (;;) {
		RT6_TRACE("fixing tree: plen=%d iter=%d\n", fn->fn_bit, iter);
		iter++;

		BUG_TRAP(!(fn->fn_flags&RTN_RTINFO));
		BUG_TRAP(!(fn->fn_flags&RTN_TL_ROOT));
		BUG_TRAP(fn->leaf==NULL);

		children = 0;
		child = NULL;
		if (fn->right) child = fn->right, children |= 1;
		if (fn->left) child = fn->left, children |= 2;

		if (children == 3 || FIB6_SUBTREE(fn)
#ifdef CONFIG_IPV6_SUBTREES
		    /* Subtree root (i.e. fn) may have one child */
		    || (children && fn->fn_flags&RTN_ROOT)
#endif
		    ) {
			fn->leaf = fib6_find_prefix(fn);
#if RT6_DEBUG >= 2
			if (fn->leaf==NULL) {
				BUG_TRAP(fn->leaf);
				fn->leaf = &ip6_null_entry;
			}
#endif
			atomic_inc(&fn->leaf->rt6i_ref);
			return fn->parent;
		}

		pn = fn->parent;
#ifdef CONFIG_IPV6_SUBTREES
		if (FIB6_SUBTREE(pn) == fn) {
			BUG_TRAP(fn->fn_flags&RTN_ROOT);
			FIB6_SUBTREE(pn) = NULL;
			nstate = FWS_L;
		} else {
			BUG_TRAP(!(fn->fn_flags&RTN_ROOT));
#endif
			if (pn->right == fn) pn->right = child;
			else if (pn->left == fn) pn->left = child;
#if RT6_DEBUG >= 2
			else BUG_TRAP(0);
#endif
			if (child)
				child->parent = pn;
			nstate = FWS_R;
#ifdef CONFIG_IPV6_SUBTREES
		}
#endif

		read_lock(&fib6_walker_lock);
		FOR_WALKERS(w) {
			if (child == NULL) {
				if (w->root == fn) {
					w->root = w->node = NULL;
					RT6_TRACE("W %p adjusted by delroot 1\n", w);
				} else if (w->node == fn) {
					RT6_TRACE("W %p adjusted by delnode 1, s=%d/%d\n", w, w->state, nstate);
					w->node = pn;
					w->state = nstate;
				}
			} else {
				if (w->root == fn) {
					w->root = child;
					RT6_TRACE("W %p adjusted by delroot 2\n", w);
				}
				if (w->node == fn) {
					w->node = child;
					if (children&2) {
						RT6_TRACE("W %p adjusted by delnode 2, s=%d\n", w, w->state);
						w->state = w->state>=FWS_R ? FWS_U : FWS_INIT;
					} else {
						RT6_TRACE("W %p adjusted by delnode 2, s=%d\n", w, w->state);
						w->state = w->state>=FWS_C ? FWS_U : FWS_INIT;
					}
				}
			}
		}
		read_unlock(&fib6_walker_lock);

		node_free(fn);
		if (pn->fn_flags&RTN_RTINFO || FIB6_SUBTREE(pn))
			return pn;

		rt6_release(pn->leaf);
		pn->leaf = NULL;
		fn = pn;
	}
}

static void fib6_del_route(struct fib6_node *fn, struct rt6_info **rtp,
			   struct nl_info *info)
{
	struct fib6_walker_t *w;
	struct rt6_info *rt = *rtp;

	RT6_TRACE("fib6_del_route\n");

	/* Unlink it */
	*rtp = rt->u.dst.rt6_next;
	rt->rt6i_node = NULL;
	rt6_stats.fib_rt_entries--;
	rt6_stats.fib_discarded_routes++;

	/* Reset round-robin state, if necessary */
	if (fn->rr_ptr == rt)
		fn->rr_ptr = NULL;

	/* Adjust walkers */
	read_lock(&fib6_walker_lock);
	FOR_WALKERS(w) {
		if (w->state == FWS_C && w->leaf == rt) {
			RT6_TRACE("walker %p adjusted by delroute\n", w);
			w->leaf = rt->u.dst.rt6_next;
			if (w->leaf == NULL)
				w->state = FWS_U;
		}
	}
	read_unlock(&fib6_walker_lock);

	rt->u.dst.rt6_next = NULL;

	if (fn->leaf == NULL && fn->fn_flags&RTN_TL_ROOT)
		fn->leaf = &ip6_null_entry;

	/* If it was last route, expunge its radix tree node */
	if (fn->leaf == NULL) {
		fn->fn_flags &= ~RTN_RTINFO;
		rt6_stats.fib_route_nodes--;
		fn = fib6_repair_tree(fn);
	}

	if (atomic_read(&rt->rt6i_ref) != 1) {
		/* This route is used as dummy address holder in some split
		 * nodes. It is not leaked, but it still holds other resources,
		 * which must be released in time. So, scan ascendant nodes
		 * and replace dummy references to this route with references
		 * to still alive ones.
		 */
		while (fn) {
			if (!(fn->fn_flags&RTN_RTINFO) && fn->leaf == rt) {
				fn->leaf = fib6_find_prefix(fn);
				atomic_inc(&fn->leaf->rt6i_ref);
				rt6_release(rt);
			}
			fn = fn->parent;
		}
		/* No more references are possible at this point. */
		if (atomic_read(&rt->rt6i_ref) != 1) BUG();
	}

	inet6_rt_notify(RTM_DELROUTE, rt, info);
	rt6_release(rt);
}

int fib6_del(struct rt6_info *rt, struct nl_info *info)
{
	struct fib6_node *fn = rt->rt6i_node;
	struct rt6_info **rtp;

#if RT6_DEBUG >= 2
	if (rt->u.dst.obsolete>0) {
		BUG_TRAP(fn==NULL);
		return -ENOENT;
	}
#endif
	if (fn == NULL || rt == &ip6_null_entry)
		return -ENOENT;

	BUG_TRAP(fn->fn_flags&RTN_RTINFO);

	if (!(rt->rt6i_flags&RTF_CACHE)) {
		struct fib6_node *pn = fn;
#ifdef CONFIG_IPV6_SUBTREES
		/* clones of this route might be in another subtree */
		if (rt->rt6i_src.plen) {
			while (!(pn->fn_flags&RTN_ROOT))
				pn = pn->parent;
			pn = pn->parent;
		}
#endif
		fib6_prune_clones(pn, rt);
	}

	/*
	 *	Walk the leaf entries looking for ourself
	 */

	for (rtp = &fn->leaf; *rtp; rtp = &(*rtp)->u.dst.rt6_next) {
		if (*rtp == rt) {
			fib6_del_route(fn, rtp, info);
			return 0;
		}
	}
	return -ENOENT;
}

/*
 *	Tree traversal function.
 *
 *	Certainly, it is not interrupt safe.
 *	However, it is internally reenterable wrt itself and fib6_add/fib6_del.
 *	It means, that we can modify tree during walking
 *	and use this function for garbage collection, clone pruning,
 *	cleaning tree when a device goes down etc. etc.
 *
 *	It guarantees that every node will be traversed,
 *	and that it will be traversed only once.
 *
 *	Callback function w->func may return:
 *	0 -> continue walking.
 *	positive value -> walking is suspended (used by tree dumps,
 *	and probably by gc, if it will be split to several slices)
 *	negative value -> terminate walking.
 *
 *	The function itself returns:
 *	0   -> walk is complete.
 *	>0  -> walk is incomplete (i.e. suspended)
 *	<0  -> walk is terminated by an error.
 */

static int fib6_walk_continue(struct fib6_walker_t *w)
{
	struct fib6_node *fn, *pn;

	for (;;) {
		fn = w->node;
		if (fn == NULL)
			return 0;

		if (w->prune && fn != w->root &&
		    fn->fn_flags&RTN_RTINFO && w->state < FWS_C) {
			w->state = FWS_C;
			w->leaf = fn->leaf;
		}
		switch (w->state) {
#ifdef CONFIG_IPV6_SUBTREES
		case FWS_S:
			if (FIB6_SUBTREE(fn)) {
				w->node = FIB6_SUBTREE(fn);
				continue;
			}
			w->state = FWS_L;
#endif
		case FWS_L:
			if (fn->left) {
				w->node = fn->left;
				w->state = FWS_INIT;
				continue;
			}
			w->state = FWS_R;
		case FWS_R:
			if (fn->right) {
				w->node = fn->right;
				w->state = FWS_INIT;
				continue;
			}
			w->state = FWS_C;
			w->leaf = fn->leaf;
		case FWS_C:
			if (w->leaf && fn->fn_flags&RTN_RTINFO) {
				int err = w->func(w);
				if (err)
					return err;
				continue;
			}
			w->state = FWS_U;
		case FWS_U:
			if (fn == w->root)
				return 0;
			pn = fn->parent;
			w->node = pn;
#ifdef CONFIG_IPV6_SUBTREES
			if (FIB6_SUBTREE(pn) == fn) {
				BUG_TRAP(fn->fn_flags&RTN_ROOT);
				w->state = FWS_L;
				continue;
			}
#endif
			if (pn->left == fn) {
				w->state = FWS_R;
				continue;
			}
			if (pn->right == fn) {
				w->state = FWS_C;
				w->leaf = w->node->leaf;
				continue;
			}
#if RT6_DEBUG >= 2
			BUG_TRAP(0);
#endif
		}
	}
}

static int fib6_walk(struct fib6_walker_t *w)
{
	int res;

	w->state = FWS_INIT;
	w->node = w->root;

	fib6_walker_link(w);
	res = fib6_walk_continue(w);
	if (res <= 0)
		fib6_walker_unlink(w);
	return res;
}

static int fib6_clean_node(struct fib6_walker_t *w)
{
	int res;
	struct rt6_info *rt;
	struct fib6_cleaner_t *c = (struct fib6_cleaner_t*)w;

	for (rt = w->leaf; rt; rt = rt->u.dst.rt6_next) {
		res = c->func(rt, c->arg);
		if (res < 0) {
			w->leaf = rt;
			res = fib6_del(rt, NULL);
			if (res) {
#if RT6_DEBUG >= 2
				printk(KERN_DEBUG "fib6_clean_node: del failed: rt=%p@%p err=%d\n", rt, rt->rt6i_node, res);
#endif
				continue;
			}
			return 0;
		}
		BUG_TRAP(res==0);
	}
	w->leaf = rt;
	return 0;
}

/*
 *	Convenient frontend to tree walker.
 *
 *	func is called on each route.
 *		It may return -1 -> delete this route.
 *		              0  -> continue walking
 *
 *	prune==1 -> only immediate children of node (certainly,
 *	ignoring pure split nodes) will be scanned.
 */

static void fib6_clean_tree(struct fib6_node *root,
			    int (*func)(struct rt6_info *, void *arg),
			    int prune, void *arg)
{
	struct fib6_cleaner_t c;

	c.w.root = root;
	c.w.func = fib6_clean_node;
	c.w.prune = prune;
	c.func = func;
	c.arg = arg;

	fib6_walk(&c.w);
}

void fib6_clean_all(int (*func)(struct rt6_info *, void *arg),
		    int prune, void *arg)
{
	struct fib6_table *table;
	struct hlist_node *node;
	unsigned int h;

	rcu_read_lock();
	for (h = 0; h < FIB_TABLE_HASHSZ; h++) {
		hlist_for_each_entry_rcu(table, node, &fib_table_hash[h],
					 tb6_hlist) {
			write_lock_bh(&table->tb6_lock);
			fib6_clean_tree(&table->tb6_root, func, prune, arg);
			write_unlock_bh(&table->tb6_lock);
		}
	}
	rcu_read_unlock();
}

static int fib6_prune_clone(struct rt6_info *rt, void *arg)
{
	if (rt->rt6i_flags & RTF_CACHE) {
		RT6_TRACE("pruning clone %p\n", rt);
		return -1;
	}

	return 0;
}

static void fib6_prune_clones(struct fib6_node *fn, struct rt6_info *rt)
{
	fib6_clean_tree(fn, fib6_prune_clone, 1, rt);
}

/*
 *	Garbage collection
 */

static struct fib6_gc_args
{
	int			timeout;
	int			more;
} gc_args;

static int fib6_age(struct rt6_info *rt, void *arg)
{
	unsigned long now = jiffies;

	/*
	 *	check addrconf expiration here.
	 *	Routes are expired even if they are in use.
	 *
	 *	Also age clones. Note, that clones are aged out
	 *	only if they are not in use now.
	 */

	if (rt->rt6i_flags&RTF_EXPIRES && rt->rt6i_expires) {
		if (time_after(now, rt->rt6i_expires)) {
			RT6_TRACE("expiring %p\n", rt);
			return -1;
		}
		gc_args.more++;
	} else if (rt->rt6i_flags & RTF_CACHE) {
		if (atomic_read(&rt->u.dst.__refcnt) == 0 &&
		    time_after_eq(now, rt->u.dst.lastuse + gc_args.timeout)) {
			RT6_TRACE("aging clone %p\n", rt);
			return -1;
		} else if ((rt->rt6i_flags & RTF_GATEWAY) &&
			   (!(rt->rt6i_nexthop->flags & NTF_ROUTER))) {
			RT6_TRACE("purging route %p via non-router but gateway\n",
				  rt);
			return -1;
		}
		gc_args.more++;
	}

	return 0;
}

static DEFINE_SPINLOCK(fib6_gc_lock);

void fib6_run_gc(unsigned long dummy)
{
	if (dummy != ~0UL) {
		spin_lock_bh(&fib6_gc_lock);
		gc_args.timeout = dummy ? (int)dummy : ip6_rt_gc_interval;
	} else {
		local_bh_disable();
		if (!spin_trylock(&fib6_gc_lock)) {
			mod_timer(&ip6_fib_timer, jiffies + HZ);
			local_bh_enable();
			return;
		}
		gc_args.timeout = ip6_rt_gc_interval;
	}
	gc_args.more = 0;

	ndisc_dst_gc(&gc_args.more);
	fib6_clean_all(fib6_age, 0, NULL);

	if (gc_args.more)
		mod_timer(&ip6_fib_timer, jiffies + ip6_rt_gc_interval);
	else {
		del_timer(&ip6_fib_timer);
		ip6_fib_timer.expires = 0;
	}
	spin_unlock_bh(&fib6_gc_lock);
}

void __init fib6_init(void)
{
	fib6_node_kmem = kmem_cache_create("fib6_nodes",
					   sizeof(struct fib6_node),
					   0, SLAB_HWCACHE_ALIGN|SLAB_PANIC,
					   NULL, NULL);

	fib6_tables_init();

	__rtnl_register(PF_INET6, RTM_GETROUTE, NULL, inet6_dump_fib);
}

void fib6_gc_cleanup(void)
{
	del_timer(&ip6_fib_timer);
	kmem_cache_destroy(fib6_node_kmem);
}

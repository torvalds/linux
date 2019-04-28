/*
 *	Linux INET6 implementation
 *	Forwarding Information Database
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *	Changes:
 *	Yuji SEKIYA @USAGI:	Support default route on router node;
 *				remove ip6_null_entry from the top of
 *				routing table.
 *	Ville Nuorvala:		Fixed routing subtrees.
 */

#define pr_fmt(fmt) "IPv6: " fmt

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/net.h>
#include <linux/route.h>
#include <linux/netdevice.h>
#include <linux/in6.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/slab.h>

#include <net/ip.h>
#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/addrconf.h>
#include <net/lwtunnel.h>
#include <net/fib_notifier.h>

#include <net/ip6_fib.h>
#include <net/ip6_route.h>

static struct kmem_cache *fib6_node_kmem __read_mostly;

struct fib6_cleaner {
	struct fib6_walker w;
	struct net *net;
	int (*func)(struct fib6_info *, void *arg);
	int sernum;
	void *arg;
	bool skip_notify;
};

#ifdef CONFIG_IPV6_SUBTREES
#define FWS_INIT FWS_S
#else
#define FWS_INIT FWS_L
#endif

static struct fib6_info *fib6_find_prefix(struct net *net,
					 struct fib6_table *table,
					 struct fib6_node *fn);
static struct fib6_node *fib6_repair_tree(struct net *net,
					  struct fib6_table *table,
					  struct fib6_node *fn);
static int fib6_walk(struct net *net, struct fib6_walker *w);
static int fib6_walk_continue(struct fib6_walker *w);

/*
 *	A routing update causes an increase of the serial number on the
 *	affected subtree. This allows for cached routes to be asynchronously
 *	tested when modifications are made to the destination cache as a
 *	result of redirects, path MTU changes, etc.
 */

static void fib6_gc_timer_cb(struct timer_list *t);

#define FOR_WALKERS(net, w) \
	list_for_each_entry(w, &(net)->ipv6.fib6_walkers, lh)

static void fib6_walker_link(struct net *net, struct fib6_walker *w)
{
	write_lock_bh(&net->ipv6.fib6_walker_lock);
	list_add(&w->lh, &net->ipv6.fib6_walkers);
	write_unlock_bh(&net->ipv6.fib6_walker_lock);
}

static void fib6_walker_unlink(struct net *net, struct fib6_walker *w)
{
	write_lock_bh(&net->ipv6.fib6_walker_lock);
	list_del(&w->lh);
	write_unlock_bh(&net->ipv6.fib6_walker_lock);
}

static int fib6_new_sernum(struct net *net)
{
	int new, old;

	do {
		old = atomic_read(&net->ipv6.fib6_sernum);
		new = old < INT_MAX ? old + 1 : 1;
	} while (atomic_cmpxchg(&net->ipv6.fib6_sernum,
				old, new) != old);
	return new;
}

enum {
	FIB6_NO_SERNUM_CHANGE = 0,
};

void fib6_update_sernum(struct net *net, struct fib6_info *f6i)
{
	struct fib6_node *fn;

	fn = rcu_dereference_protected(f6i->fib6_node,
			lockdep_is_held(&f6i->fib6_table->tb6_lock));
	if (fn)
		fn->fn_sernum = fib6_new_sernum(net);
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
#if defined(__LITTLE_ENDIAN)
# define BITOP_BE32_SWIZZLE	(0x1F & ~7)
#else
# define BITOP_BE32_SWIZZLE	0
#endif

static __be32 addr_bit_set(const void *token, int fn_bit)
{
	const __be32 *addr = token;
	/*
	 * Here,
	 *	1 << ((~fn_bit ^ BITOP_BE32_SWIZZLE) & 0x1f)
	 * is optimized version of
	 *	htonl(1 << ((~fn_bit)&0x1F))
	 * See include/asm-generic/bitops/le.h.
	 */
	return (__force __be32)(1 << ((~fn_bit ^ BITOP_BE32_SWIZZLE) & 0x1f)) &
	       addr[fn_bit >> 5];
}

struct fib6_info *fib6_info_alloc(gfp_t gfp_flags)
{
	struct fib6_info *f6i;

	f6i = kzalloc(sizeof(*f6i), gfp_flags);
	if (!f6i)
		return NULL;

	f6i->rt6i_pcpu = alloc_percpu_gfp(struct rt6_info *, gfp_flags);
	if (!f6i->rt6i_pcpu) {
		kfree(f6i);
		return NULL;
	}

	INIT_LIST_HEAD(&f6i->fib6_siblings);
	atomic_inc(&f6i->fib6_ref);

	return f6i;
}

void fib6_info_destroy_rcu(struct rcu_head *head)
{
	struct fib6_info *f6i = container_of(head, struct fib6_info, rcu);
	struct rt6_exception_bucket *bucket;

	WARN_ON(f6i->fib6_node);

	bucket = rcu_dereference_protected(f6i->rt6i_exception_bucket, 1);
	if (bucket) {
		f6i->rt6i_exception_bucket = NULL;
		kfree(bucket);
	}

	if (f6i->rt6i_pcpu) {
		int cpu;

		for_each_possible_cpu(cpu) {
			struct rt6_info **ppcpu_rt;
			struct rt6_info *pcpu_rt;

			ppcpu_rt = per_cpu_ptr(f6i->rt6i_pcpu, cpu);
			pcpu_rt = *ppcpu_rt;
			if (pcpu_rt) {
				dst_dev_put(&pcpu_rt->dst);
				dst_release(&pcpu_rt->dst);
				*ppcpu_rt = NULL;
			}
		}

		free_percpu(f6i->rt6i_pcpu);
	}

	lwtstate_put(f6i->fib6_nh.nh_lwtstate);

	if (f6i->fib6_nh.nh_dev)
		dev_put(f6i->fib6_nh.nh_dev);

	ip_fib_metrics_put(f6i->fib6_metrics);

	kfree(f6i);
}
EXPORT_SYMBOL_GPL(fib6_info_destroy_rcu);

static struct fib6_node *node_alloc(struct net *net)
{
	struct fib6_node *fn;

	fn = kmem_cache_zalloc(fib6_node_kmem, GFP_ATOMIC);
	if (fn)
		net->ipv6.rt6_stats->fib_nodes++;

	return fn;
}

static void node_free_immediate(struct net *net, struct fib6_node *fn)
{
	kmem_cache_free(fib6_node_kmem, fn);
	net->ipv6.rt6_stats->fib_nodes--;
}

static void node_free_rcu(struct rcu_head *head)
{
	struct fib6_node *fn = container_of(head, struct fib6_node, rcu);

	kmem_cache_free(fib6_node_kmem, fn);
}

static void node_free(struct net *net, struct fib6_node *fn)
{
	call_rcu(&fn->rcu, node_free_rcu);
	net->ipv6.rt6_stats->fib_nodes--;
}

static void fib6_free_table(struct fib6_table *table)
{
	inetpeer_invalidate_tree(&table->tb6_peers);
	kfree(table);
}

static void fib6_link_table(struct net *net, struct fib6_table *tb)
{
	unsigned int h;

	/*
	 * Initialize table lock at a single place to give lockdep a key,
	 * tables aren't visible prior to being linked to the list.
	 */
	spin_lock_init(&tb->tb6_lock);
	h = tb->tb6_id & (FIB6_TABLE_HASHSZ - 1);

	/*
	 * No protection necessary, this is the only list mutatation
	 * operation, tables never disappear once they exist.
	 */
	hlist_add_head_rcu(&tb->tb6_hlist, &net->ipv6.fib_table_hash[h]);
}

#ifdef CONFIG_IPV6_MULTIPLE_TABLES

static struct fib6_table *fib6_alloc_table(struct net *net, u32 id)
{
	struct fib6_table *table;

	table = kzalloc(sizeof(*table), GFP_ATOMIC);
	if (table) {
		table->tb6_id = id;
		rcu_assign_pointer(table->tb6_root.leaf,
				   net->ipv6.fib6_null_entry);
		table->tb6_root.fn_flags = RTN_ROOT | RTN_TL_ROOT | RTN_RTINFO;
		inet_peer_base_init(&table->tb6_peers);
	}

	return table;
}

struct fib6_table *fib6_new_table(struct net *net, u32 id)
{
	struct fib6_table *tb;

	if (id == 0)
		id = RT6_TABLE_MAIN;
	tb = fib6_get_table(net, id);
	if (tb)
		return tb;

	tb = fib6_alloc_table(net, id);
	if (tb)
		fib6_link_table(net, tb);

	return tb;
}
EXPORT_SYMBOL_GPL(fib6_new_table);

struct fib6_table *fib6_get_table(struct net *net, u32 id)
{
	struct fib6_table *tb;
	struct hlist_head *head;
	unsigned int h;

	if (id == 0)
		id = RT6_TABLE_MAIN;
	h = id & (FIB6_TABLE_HASHSZ - 1);
	rcu_read_lock();
	head = &net->ipv6.fib_table_hash[h];
	hlist_for_each_entry_rcu(tb, head, tb6_hlist) {
		if (tb->tb6_id == id) {
			rcu_read_unlock();
			return tb;
		}
	}
	rcu_read_unlock();

	return NULL;
}
EXPORT_SYMBOL_GPL(fib6_get_table);

static void __net_init fib6_tables_init(struct net *net)
{
	fib6_link_table(net, net->ipv6.fib6_main_tbl);
	fib6_link_table(net, net->ipv6.fib6_local_tbl);
}
#else

struct fib6_table *fib6_new_table(struct net *net, u32 id)
{
	return fib6_get_table(net, id);
}

struct fib6_table *fib6_get_table(struct net *net, u32 id)
{
	  return net->ipv6.fib6_main_tbl;
}

struct dst_entry *fib6_rule_lookup(struct net *net, struct flowi6 *fl6,
				   const struct sk_buff *skb,
				   int flags, pol_lookup_t lookup)
{
	struct rt6_info *rt;

	rt = lookup(net, net->ipv6.fib6_main_tbl, fl6, skb, flags);
	if (rt->dst.error == -EAGAIN) {
		ip6_rt_put(rt);
		rt = net->ipv6.ip6_null_entry;
		dst_hold(&rt->dst);
	}

	return &rt->dst;
}

/* called with rcu lock held; no reference taken on fib6_info */
struct fib6_info *fib6_lookup(struct net *net, int oif, struct flowi6 *fl6,
			      int flags)
{
	return fib6_table_lookup(net, net->ipv6.fib6_main_tbl, oif, fl6, flags);
}

static void __net_init fib6_tables_init(struct net *net)
{
	fib6_link_table(net, net->ipv6.fib6_main_tbl);
}

#endif

unsigned int fib6_tables_seq_read(struct net *net)
{
	unsigned int h, fib_seq = 0;

	rcu_read_lock();
	for (h = 0; h < FIB6_TABLE_HASHSZ; h++) {
		struct hlist_head *head = &net->ipv6.fib_table_hash[h];
		struct fib6_table *tb;

		hlist_for_each_entry_rcu(tb, head, tb6_hlist)
			fib_seq += tb->fib_seq;
	}
	rcu_read_unlock();

	return fib_seq;
}

static int call_fib6_entry_notifier(struct notifier_block *nb, struct net *net,
				    enum fib_event_type event_type,
				    struct fib6_info *rt)
{
	struct fib6_entry_notifier_info info = {
		.rt = rt,
	};

	return call_fib6_notifier(nb, net, event_type, &info.info);
}

static int call_fib6_entry_notifiers(struct net *net,
				     enum fib_event_type event_type,
				     struct fib6_info *rt,
				     struct netlink_ext_ack *extack)
{
	struct fib6_entry_notifier_info info = {
		.info.extack = extack,
		.rt = rt,
	};

	rt->fib6_table->fib_seq++;
	return call_fib6_notifiers(net, event_type, &info.info);
}

struct fib6_dump_arg {
	struct net *net;
	struct notifier_block *nb;
};

static void fib6_rt_dump(struct fib6_info *rt, struct fib6_dump_arg *arg)
{
	if (rt == arg->net->ipv6.fib6_null_entry)
		return;
	call_fib6_entry_notifier(arg->nb, arg->net, FIB_EVENT_ENTRY_ADD, rt);
}

static int fib6_node_dump(struct fib6_walker *w)
{
	struct fib6_info *rt;

	for_each_fib6_walker_rt(w)
		fib6_rt_dump(rt, w->args);
	w->leaf = NULL;
	return 0;
}

static void fib6_table_dump(struct net *net, struct fib6_table *tb,
			    struct fib6_walker *w)
{
	w->root = &tb->tb6_root;
	spin_lock_bh(&tb->tb6_lock);
	fib6_walk(net, w);
	spin_unlock_bh(&tb->tb6_lock);
}

/* Called with rcu_read_lock() */
int fib6_tables_dump(struct net *net, struct notifier_block *nb)
{
	struct fib6_dump_arg arg;
	struct fib6_walker *w;
	unsigned int h;

	w = kzalloc(sizeof(*w), GFP_ATOMIC);
	if (!w)
		return -ENOMEM;

	w->func = fib6_node_dump;
	arg.net = net;
	arg.nb = nb;
	w->args = &arg;

	for (h = 0; h < FIB6_TABLE_HASHSZ; h++) {
		struct hlist_head *head = &net->ipv6.fib_table_hash[h];
		struct fib6_table *tb;

		hlist_for_each_entry_rcu(tb, head, tb6_hlist)
			fib6_table_dump(net, tb, w);
	}

	kfree(w);

	return 0;
}

static int fib6_dump_node(struct fib6_walker *w)
{
	int res;
	struct fib6_info *rt;

	for_each_fib6_walker_rt(w) {
		res = rt6_dump_route(rt, w->args);
		if (res < 0) {
			/* Frame is full, suspend walking */
			w->leaf = rt;
			return 1;
		}

		/* Multipath routes are dumped in one route with the
		 * RTA_MULTIPATH attribute. Jump 'rt' to point to the
		 * last sibling of this route (no need to dump the
		 * sibling routes again)
		 */
		if (rt->fib6_nsiblings)
			rt = list_last_entry(&rt->fib6_siblings,
					     struct fib6_info,
					     fib6_siblings);
	}
	w->leaf = NULL;
	return 0;
}

static void fib6_dump_end(struct netlink_callback *cb)
{
	struct net *net = sock_net(cb->skb->sk);
	struct fib6_walker *w = (void *)cb->args[2];

	if (w) {
		if (cb->args[4]) {
			cb->args[4] = 0;
			fib6_walker_unlink(net, w);
		}
		cb->args[2] = 0;
		kfree(w);
	}
	cb->done = (void *)cb->args[3];
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
	struct net *net = sock_net(skb->sk);
	struct fib6_walker *w;
	int res;

	w = (void *)cb->args[2];
	w->root = &table->tb6_root;

	if (cb->args[4] == 0) {
		w->count = 0;
		w->skip = 0;

		spin_lock_bh(&table->tb6_lock);
		res = fib6_walk(net, w);
		spin_unlock_bh(&table->tb6_lock);
		if (res > 0) {
			cb->args[4] = 1;
			cb->args[5] = w->root->fn_sernum;
		}
	} else {
		if (cb->args[5] != w->root->fn_sernum) {
			/* Begin at the root if the tree changed */
			cb->args[5] = w->root->fn_sernum;
			w->state = FWS_INIT;
			w->node = w->root;
			w->skip = w->count;
		} else
			w->skip = 0;

		spin_lock_bh(&table->tb6_lock);
		res = fib6_walk_continue(w);
		spin_unlock_bh(&table->tb6_lock);
		if (res <= 0) {
			fib6_walker_unlink(net, w);
			cb->args[4] = 0;
		}
	}

	return res;
}

static int inet6_dump_fib(struct sk_buff *skb, struct netlink_callback *cb)
{
	const struct nlmsghdr *nlh = cb->nlh;
	struct net *net = sock_net(skb->sk);
	struct rt6_rtnl_dump_arg arg = {};
	unsigned int h, s_h;
	unsigned int e = 0, s_e;
	struct fib6_walker *w;
	struct fib6_table *tb;
	struct hlist_head *head;
	int res = 0;

	if (cb->strict_check) {
		int err;

		err = ip_valid_fib_dump_req(net, nlh, &arg.filter, cb);
		if (err < 0)
			return err;
	} else if (nlmsg_len(nlh) >= sizeof(struct rtmsg)) {
		struct rtmsg *rtm = nlmsg_data(nlh);

		arg.filter.flags = rtm->rtm_flags & (RTM_F_PREFIX|RTM_F_CLONED);
	}

	/* fib entries are never clones */
	if (arg.filter.flags & RTM_F_CLONED)
		goto out;

	w = (void *)cb->args[2];
	if (!w) {
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
		if (!w)
			return -ENOMEM;
		w->func = fib6_dump_node;
		cb->args[2] = (long)w;
	}

	arg.skb = skb;
	arg.cb = cb;
	arg.net = net;
	w->args = &arg;

	if (arg.filter.table_id) {
		tb = fib6_get_table(net, arg.filter.table_id);
		if (!tb) {
			if (arg.filter.dump_all_families)
				goto out;

			NL_SET_ERR_MSG_MOD(cb->extack, "FIB table does not exist");
			return -ENOENT;
		}

		if (!cb->args[0]) {
			res = fib6_dump_table(tb, skb, cb);
			if (!res)
				cb->args[0] = 1;
		}
		goto out;
	}

	s_h = cb->args[0];
	s_e = cb->args[1];

	rcu_read_lock();
	for (h = s_h; h < FIB6_TABLE_HASHSZ; h++, s_e = 0) {
		e = 0;
		head = &net->ipv6.fib_table_hash[h];
		hlist_for_each_entry_rcu(tb, head, tb6_hlist) {
			if (e < s_e)
				goto next;
			res = fib6_dump_table(tb, skb, cb);
			if (res != 0)
				goto out_unlock;
next:
			e++;
		}
	}
out_unlock:
	rcu_read_unlock();
	cb->args[1] = e;
	cb->args[0] = h;
out:
	res = res < 0 ? res : skb->len;
	if (res <= 0)
		fib6_dump_end(cb);
	return res;
}

void fib6_metric_set(struct fib6_info *f6i, int metric, u32 val)
{
	if (!f6i)
		return;

	if (f6i->fib6_metrics == &dst_default_metrics) {
		struct dst_metrics *p = kzalloc(sizeof(*p), GFP_ATOMIC);

		if (!p)
			return;

		refcount_set(&p->refcnt, 1);
		f6i->fib6_metrics = p;
	}

	f6i->fib6_metrics->metrics[metric - 1] = val;
}

/*
 *	Routing Table
 *
 *	return the appropriate node for a routing tree "add" operation
 *	by either creating and inserting or by returning an existing
 *	node.
 */

static struct fib6_node *fib6_add_1(struct net *net,
				    struct fib6_table *table,
				    struct fib6_node *root,
				    struct in6_addr *addr, int plen,
				    int offset, int allow_create,
				    int replace_required,
				    struct netlink_ext_ack *extack)
{
	struct fib6_node *fn, *in, *ln;
	struct fib6_node *pn = NULL;
	struct rt6key *key;
	int	bit;
	__be32	dir = 0;

	RT6_TRACE("fib6_add_1\n");

	/* insert node in tree */

	fn = root;

	do {
		struct fib6_info *leaf = rcu_dereference_protected(fn->leaf,
					    lockdep_is_held(&table->tb6_lock));
		key = (struct rt6key *)((u8 *)leaf + offset);

		/*
		 *	Prefix match
		 */
		if (plen < fn->fn_bit ||
		    !ipv6_prefix_equal(&key->addr, addr, fn->fn_bit)) {
			if (!allow_create) {
				if (replace_required) {
					NL_SET_ERR_MSG(extack,
						       "Can not replace route - no match found");
					pr_warn("Can't replace route, no match found\n");
					return ERR_PTR(-ENOENT);
				}
				pr_warn("NLM_F_CREATE should be set when creating new route\n");
			}
			goto insert_above;
		}

		/*
		 *	Exact match ?
		 */

		if (plen == fn->fn_bit) {
			/* clean up an intermediate node */
			if (!(fn->fn_flags & RTN_RTINFO)) {
				RCU_INIT_POINTER(fn->leaf, NULL);
				fib6_info_release(leaf);
			/* remove null_entry in the root node */
			} else if (fn->fn_flags & RTN_TL_ROOT &&
				   rcu_access_pointer(fn->leaf) ==
				   net->ipv6.fib6_null_entry) {
				RCU_INIT_POINTER(fn->leaf, NULL);
			}

			return fn;
		}

		/*
		 *	We have more bits to go
		 */

		/* Try to walk down on tree. */
		dir = addr_bit_set(addr, fn->fn_bit);
		pn = fn;
		fn = dir ?
		     rcu_dereference_protected(fn->right,
					lockdep_is_held(&table->tb6_lock)) :
		     rcu_dereference_protected(fn->left,
					lockdep_is_held(&table->tb6_lock));
	} while (fn);

	if (!allow_create) {
		/* We should not create new node because
		 * NLM_F_REPLACE was specified without NLM_F_CREATE
		 * I assume it is safe to require NLM_F_CREATE when
		 * REPLACE flag is used! Later we may want to remove the
		 * check for replace_required, because according
		 * to netlink specification, NLM_F_CREATE
		 * MUST be specified if new route is created.
		 * That would keep IPv6 consistent with IPv4
		 */
		if (replace_required) {
			NL_SET_ERR_MSG(extack,
				       "Can not replace route - no match found");
			pr_warn("Can't replace route, no match found\n");
			return ERR_PTR(-ENOENT);
		}
		pr_warn("NLM_F_CREATE should be set when creating new route\n");
	}
	/*
	 *	We walked to the bottom of tree.
	 *	Create new leaf node without children.
	 */

	ln = node_alloc(net);

	if (!ln)
		return ERR_PTR(-ENOMEM);
	ln->fn_bit = plen;
	RCU_INIT_POINTER(ln->parent, pn);

	if (dir)
		rcu_assign_pointer(pn->right, ln);
	else
		rcu_assign_pointer(pn->left, ln);

	return ln;


insert_above:
	/*
	 * split since we don't have a common prefix anymore or
	 * we have a less significant route.
	 * we've to insert an intermediate node on the list
	 * this new node will point to the one we need to create
	 * and the current
	 */

	pn = rcu_dereference_protected(fn->parent,
				       lockdep_is_held(&table->tb6_lock));

	/* find 1st bit in difference between the 2 addrs.

	   See comment in __ipv6_addr_diff: bit may be an invalid value,
	   but if it is >= plen, the value is ignored in any case.
	 */

	bit = __ipv6_addr_diff(addr, &key->addr, sizeof(*addr));

	/*
	 *		(intermediate)[in]
	 *	          /	   \
	 *	(new leaf node)[ln] (old node)[fn]
	 */
	if (plen > bit) {
		in = node_alloc(net);
		ln = node_alloc(net);

		if (!in || !ln) {
			if (in)
				node_free_immediate(net, in);
			if (ln)
				node_free_immediate(net, ln);
			return ERR_PTR(-ENOMEM);
		}

		/*
		 * new intermediate node.
		 * RTN_RTINFO will
		 * be off since that an address that chooses one of
		 * the branches would not match less specific routes
		 * in the other branch
		 */

		in->fn_bit = bit;

		RCU_INIT_POINTER(in->parent, pn);
		in->leaf = fn->leaf;
		atomic_inc(&rcu_dereference_protected(in->leaf,
				lockdep_is_held(&table->tb6_lock))->fib6_ref);

		/* update parent pointer */
		if (dir)
			rcu_assign_pointer(pn->right, in);
		else
			rcu_assign_pointer(pn->left, in);

		ln->fn_bit = plen;

		RCU_INIT_POINTER(ln->parent, in);
		rcu_assign_pointer(fn->parent, in);

		if (addr_bit_set(addr, bit)) {
			rcu_assign_pointer(in->right, ln);
			rcu_assign_pointer(in->left, fn);
		} else {
			rcu_assign_pointer(in->left, ln);
			rcu_assign_pointer(in->right, fn);
		}
	} else { /* plen <= bit */

		/*
		 *		(new leaf node)[ln]
		 *	          /	   \
		 *	     (old node)[fn] NULL
		 */

		ln = node_alloc(net);

		if (!ln)
			return ERR_PTR(-ENOMEM);

		ln->fn_bit = plen;

		RCU_INIT_POINTER(ln->parent, pn);

		if (addr_bit_set(&key->addr, plen))
			RCU_INIT_POINTER(ln->right, fn);
		else
			RCU_INIT_POINTER(ln->left, fn);

		rcu_assign_pointer(fn->parent, ln);

		if (dir)
			rcu_assign_pointer(pn->right, ln);
		else
			rcu_assign_pointer(pn->left, ln);
	}
	return ln;
}

static void fib6_drop_pcpu_from(struct fib6_info *f6i,
				const struct fib6_table *table)
{
	int cpu;

	/* release the reference to this fib entry from
	 * all of its cached pcpu routes
	 */
	for_each_possible_cpu(cpu) {
		struct rt6_info **ppcpu_rt;
		struct rt6_info *pcpu_rt;

		ppcpu_rt = per_cpu_ptr(f6i->rt6i_pcpu, cpu);
		pcpu_rt = *ppcpu_rt;
		if (pcpu_rt) {
			struct fib6_info *from;

			from = xchg((__force struct fib6_info **)&pcpu_rt->from, NULL);
			fib6_info_release(from);
		}
	}
}

static void fib6_purge_rt(struct fib6_info *rt, struct fib6_node *fn,
			  struct net *net)
{
	struct fib6_table *table = rt->fib6_table;

	if (atomic_read(&rt->fib6_ref) != 1) {
		/* This route is used as dummy address holder in some split
		 * nodes. It is not leaked, but it still holds other resources,
		 * which must be released in time. So, scan ascendant nodes
		 * and replace dummy references to this route with references
		 * to still alive ones.
		 */
		while (fn) {
			struct fib6_info *leaf = rcu_dereference_protected(fn->leaf,
					    lockdep_is_held(&table->tb6_lock));
			struct fib6_info *new_leaf;
			if (!(fn->fn_flags & RTN_RTINFO) && leaf == rt) {
				new_leaf = fib6_find_prefix(net, table, fn);
				atomic_inc(&new_leaf->fib6_ref);

				rcu_assign_pointer(fn->leaf, new_leaf);
				fib6_info_release(rt);
			}
			fn = rcu_dereference_protected(fn->parent,
				    lockdep_is_held(&table->tb6_lock));
		}

		if (rt->rt6i_pcpu)
			fib6_drop_pcpu_from(rt, table);
	}
}

/*
 *	Insert routing information in a node.
 */

static int fib6_add_rt2node(struct fib6_node *fn, struct fib6_info *rt,
			    struct nl_info *info,
			    struct netlink_ext_ack *extack)
{
	struct fib6_info *leaf = rcu_dereference_protected(fn->leaf,
				    lockdep_is_held(&rt->fib6_table->tb6_lock));
	struct fib6_info *iter = NULL;
	struct fib6_info __rcu **ins;
	struct fib6_info __rcu **fallback_ins = NULL;
	int replace = (info->nlh &&
		       (info->nlh->nlmsg_flags & NLM_F_REPLACE));
	int add = (!info->nlh ||
		   (info->nlh->nlmsg_flags & NLM_F_CREATE));
	int found = 0;
	bool rt_can_ecmp = rt6_qualify_for_ecmp(rt);
	u16 nlflags = NLM_F_EXCL;
	int err;

	if (info->nlh && (info->nlh->nlmsg_flags & NLM_F_APPEND))
		nlflags |= NLM_F_APPEND;

	ins = &fn->leaf;

	for (iter = leaf; iter;
	     iter = rcu_dereference_protected(iter->fib6_next,
				lockdep_is_held(&rt->fib6_table->tb6_lock))) {
		/*
		 *	Search for duplicates
		 */

		if (iter->fib6_metric == rt->fib6_metric) {
			/*
			 *	Same priority level
			 */
			if (info->nlh &&
			    (info->nlh->nlmsg_flags & NLM_F_EXCL))
				return -EEXIST;

			nlflags &= ~NLM_F_EXCL;
			if (replace) {
				if (rt_can_ecmp == rt6_qualify_for_ecmp(iter)) {
					found++;
					break;
				}
				if (rt_can_ecmp)
					fallback_ins = fallback_ins ?: ins;
				goto next_iter;
			}

			if (rt6_duplicate_nexthop(iter, rt)) {
				if (rt->fib6_nsiblings)
					rt->fib6_nsiblings = 0;
				if (!(iter->fib6_flags & RTF_EXPIRES))
					return -EEXIST;
				if (!(rt->fib6_flags & RTF_EXPIRES))
					fib6_clean_expires(iter);
				else
					fib6_set_expires(iter, rt->expires);

				if (rt->fib6_pmtu)
					fib6_metric_set(iter, RTAX_MTU,
							rt->fib6_pmtu);
				return -EEXIST;
			}
			/* If we have the same destination and the same metric,
			 * but not the same gateway, then the route we try to
			 * add is sibling to this route, increment our counter
			 * of siblings, and later we will add our route to the
			 * list.
			 * Only static routes (which don't have flag
			 * RTF_EXPIRES) are used for ECMPv6.
			 *
			 * To avoid long list, we only had siblings if the
			 * route have a gateway.
			 */
			if (rt_can_ecmp &&
			    rt6_qualify_for_ecmp(iter))
				rt->fib6_nsiblings++;
		}

		if (iter->fib6_metric > rt->fib6_metric)
			break;

next_iter:
		ins = &iter->fib6_next;
	}

	if (fallback_ins && !found) {
		/* No ECMP-able route found, replace first non-ECMP one */
		ins = fallback_ins;
		iter = rcu_dereference_protected(*ins,
				    lockdep_is_held(&rt->fib6_table->tb6_lock));
		found++;
	}

	/* Reset round-robin state, if necessary */
	if (ins == &fn->leaf)
		fn->rr_ptr = NULL;

	/* Link this route to others same route. */
	if (rt->fib6_nsiblings) {
		unsigned int fib6_nsiblings;
		struct fib6_info *sibling, *temp_sibling;

		/* Find the first route that have the same metric */
		sibling = leaf;
		while (sibling) {
			if (sibling->fib6_metric == rt->fib6_metric &&
			    rt6_qualify_for_ecmp(sibling)) {
				list_add_tail(&rt->fib6_siblings,
					      &sibling->fib6_siblings);
				break;
			}
			sibling = rcu_dereference_protected(sibling->fib6_next,
				    lockdep_is_held(&rt->fib6_table->tb6_lock));
		}
		/* For each sibling in the list, increment the counter of
		 * siblings. BUG() if counters does not match, list of siblings
		 * is broken!
		 */
		fib6_nsiblings = 0;
		list_for_each_entry_safe(sibling, temp_sibling,
					 &rt->fib6_siblings, fib6_siblings) {
			sibling->fib6_nsiblings++;
			BUG_ON(sibling->fib6_nsiblings != rt->fib6_nsiblings);
			fib6_nsiblings++;
		}
		BUG_ON(fib6_nsiblings != rt->fib6_nsiblings);
		rt6_multipath_rebalance(temp_sibling);
	}

	/*
	 *	insert node
	 */
	if (!replace) {
		if (!add)
			pr_warn("NLM_F_CREATE should be set when creating new route\n");

add:
		nlflags |= NLM_F_CREATE;

		err = call_fib6_entry_notifiers(info->nl_net,
						FIB_EVENT_ENTRY_ADD,
						rt, extack);
		if (err)
			return err;

		rcu_assign_pointer(rt->fib6_next, iter);
		atomic_inc(&rt->fib6_ref);
		rcu_assign_pointer(rt->fib6_node, fn);
		rcu_assign_pointer(*ins, rt);
		if (!info->skip_notify)
			inet6_rt_notify(RTM_NEWROUTE, rt, info, nlflags);
		info->nl_net->ipv6.rt6_stats->fib_rt_entries++;

		if (!(fn->fn_flags & RTN_RTINFO)) {
			info->nl_net->ipv6.rt6_stats->fib_route_nodes++;
			fn->fn_flags |= RTN_RTINFO;
		}

	} else {
		int nsiblings;

		if (!found) {
			if (add)
				goto add;
			pr_warn("NLM_F_REPLACE set, but no existing node found!\n");
			return -ENOENT;
		}

		err = call_fib6_entry_notifiers(info->nl_net,
						FIB_EVENT_ENTRY_REPLACE,
						rt, extack);
		if (err)
			return err;

		atomic_inc(&rt->fib6_ref);
		rcu_assign_pointer(rt->fib6_node, fn);
		rt->fib6_next = iter->fib6_next;
		rcu_assign_pointer(*ins, rt);
		if (!info->skip_notify)
			inet6_rt_notify(RTM_NEWROUTE, rt, info, NLM_F_REPLACE);
		if (!(fn->fn_flags & RTN_RTINFO)) {
			info->nl_net->ipv6.rt6_stats->fib_route_nodes++;
			fn->fn_flags |= RTN_RTINFO;
		}
		nsiblings = iter->fib6_nsiblings;
		iter->fib6_node = NULL;
		fib6_purge_rt(iter, fn, info->nl_net);
		if (rcu_access_pointer(fn->rr_ptr) == iter)
			fn->rr_ptr = NULL;
		fib6_info_release(iter);

		if (nsiblings) {
			/* Replacing an ECMP route, remove all siblings */
			ins = &rt->fib6_next;
			iter = rcu_dereference_protected(*ins,
				    lockdep_is_held(&rt->fib6_table->tb6_lock));
			while (iter) {
				if (iter->fib6_metric > rt->fib6_metric)
					break;
				if (rt6_qualify_for_ecmp(iter)) {
					*ins = iter->fib6_next;
					iter->fib6_node = NULL;
					fib6_purge_rt(iter, fn, info->nl_net);
					if (rcu_access_pointer(fn->rr_ptr) == iter)
						fn->rr_ptr = NULL;
					fib6_info_release(iter);
					nsiblings--;
					info->nl_net->ipv6.rt6_stats->fib_rt_entries--;
				} else {
					ins = &iter->fib6_next;
				}
				iter = rcu_dereference_protected(*ins,
					lockdep_is_held(&rt->fib6_table->tb6_lock));
			}
			WARN_ON(nsiblings != 0);
		}
	}

	return 0;
}

static void fib6_start_gc(struct net *net, struct fib6_info *rt)
{
	if (!timer_pending(&net->ipv6.ip6_fib_timer) &&
	    (rt->fib6_flags & RTF_EXPIRES))
		mod_timer(&net->ipv6.ip6_fib_timer,
			  jiffies + net->ipv6.sysctl.ip6_rt_gc_interval);
}

void fib6_force_start_gc(struct net *net)
{
	if (!timer_pending(&net->ipv6.ip6_fib_timer))
		mod_timer(&net->ipv6.ip6_fib_timer,
			  jiffies + net->ipv6.sysctl.ip6_rt_gc_interval);
}

static void __fib6_update_sernum_upto_root(struct fib6_info *rt,
					   int sernum)
{
	struct fib6_node *fn = rcu_dereference_protected(rt->fib6_node,
				lockdep_is_held(&rt->fib6_table->tb6_lock));

	/* paired with smp_rmb() in rt6_get_cookie_safe() */
	smp_wmb();
	while (fn) {
		fn->fn_sernum = sernum;
		fn = rcu_dereference_protected(fn->parent,
				lockdep_is_held(&rt->fib6_table->tb6_lock));
	}
}

void fib6_update_sernum_upto_root(struct net *net, struct fib6_info *rt)
{
	__fib6_update_sernum_upto_root(rt, fib6_new_sernum(net));
}

/*
 *	Add routing information to the routing tree.
 *	<destination addr>/<source addr>
 *	with source addr info in sub-trees
 *	Need to own table->tb6_lock
 */

int fib6_add(struct fib6_node *root, struct fib6_info *rt,
	     struct nl_info *info, struct netlink_ext_ack *extack)
{
	struct fib6_table *table = rt->fib6_table;
	struct fib6_node *fn, *pn = NULL;
	int err = -ENOMEM;
	int allow_create = 1;
	int replace_required = 0;
	int sernum = fib6_new_sernum(info->nl_net);

	if (info->nlh) {
		if (!(info->nlh->nlmsg_flags & NLM_F_CREATE))
			allow_create = 0;
		if (info->nlh->nlmsg_flags & NLM_F_REPLACE)
			replace_required = 1;
	}
	if (!allow_create && !replace_required)
		pr_warn("RTM_NEWROUTE with no NLM_F_CREATE or NLM_F_REPLACE\n");

	fn = fib6_add_1(info->nl_net, table, root,
			&rt->fib6_dst.addr, rt->fib6_dst.plen,
			offsetof(struct fib6_info, fib6_dst), allow_create,
			replace_required, extack);
	if (IS_ERR(fn)) {
		err = PTR_ERR(fn);
		fn = NULL;
		goto out;
	}

	pn = fn;

#ifdef CONFIG_IPV6_SUBTREES
	if (rt->fib6_src.plen) {
		struct fib6_node *sn;

		if (!rcu_access_pointer(fn->subtree)) {
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
			sfn = node_alloc(info->nl_net);
			if (!sfn)
				goto failure;

			atomic_inc(&info->nl_net->ipv6.fib6_null_entry->fib6_ref);
			rcu_assign_pointer(sfn->leaf,
					   info->nl_net->ipv6.fib6_null_entry);
			sfn->fn_flags = RTN_ROOT;

			/* Now add the first leaf node to new subtree */

			sn = fib6_add_1(info->nl_net, table, sfn,
					&rt->fib6_src.addr, rt->fib6_src.plen,
					offsetof(struct fib6_info, fib6_src),
					allow_create, replace_required, extack);

			if (IS_ERR(sn)) {
				/* If it is failed, discard just allocated
				   root, and then (in failure) stale node
				   in main tree.
				 */
				node_free_immediate(info->nl_net, sfn);
				err = PTR_ERR(sn);
				goto failure;
			}

			/* Now link new subtree to main tree */
			rcu_assign_pointer(sfn->parent, fn);
			rcu_assign_pointer(fn->subtree, sfn);
		} else {
			sn = fib6_add_1(info->nl_net, table, FIB6_SUBTREE(fn),
					&rt->fib6_src.addr, rt->fib6_src.plen,
					offsetof(struct fib6_info, fib6_src),
					allow_create, replace_required, extack);

			if (IS_ERR(sn)) {
				err = PTR_ERR(sn);
				goto failure;
			}
		}

		if (!rcu_access_pointer(fn->leaf)) {
			if (fn->fn_flags & RTN_TL_ROOT) {
				/* put back null_entry for root node */
				rcu_assign_pointer(fn->leaf,
					    info->nl_net->ipv6.fib6_null_entry);
			} else {
				atomic_inc(&rt->fib6_ref);
				rcu_assign_pointer(fn->leaf, rt);
			}
		}
		fn = sn;
	}
#endif

	err = fib6_add_rt2node(fn, rt, info, extack);
	if (!err) {
		__fib6_update_sernum_upto_root(rt, sernum);
		fib6_start_gc(info->nl_net, rt);
	}

out:
	if (err) {
#ifdef CONFIG_IPV6_SUBTREES
		/*
		 * If fib6_add_1 has cleared the old leaf pointer in the
		 * super-tree leaf node we have to find a new one for it.
		 */
		if (pn != fn) {
			struct fib6_info *pn_leaf =
				rcu_dereference_protected(pn->leaf,
				    lockdep_is_held(&table->tb6_lock));
			if (pn_leaf == rt) {
				pn_leaf = NULL;
				RCU_INIT_POINTER(pn->leaf, NULL);
				fib6_info_release(rt);
			}
			if (!pn_leaf && !(pn->fn_flags & RTN_RTINFO)) {
				pn_leaf = fib6_find_prefix(info->nl_net, table,
							   pn);
#if RT6_DEBUG >= 2
				if (!pn_leaf) {
					WARN_ON(!pn_leaf);
					pn_leaf =
					    info->nl_net->ipv6.fib6_null_entry;
				}
#endif
				fib6_info_hold(pn_leaf);
				rcu_assign_pointer(pn->leaf, pn_leaf);
			}
		}
#endif
		goto failure;
	}
	return err;

failure:
	/* fn->leaf could be NULL and fib6_repair_tree() needs to be called if:
	 * 1. fn is an intermediate node and we failed to add the new
	 * route to it in both subtree creation failure and fib6_add_rt2node()
	 * failure case.
	 * 2. fn is the root node in the table and we fail to add the first
	 * default route to it.
	 */
	if (fn &&
	    (!(fn->fn_flags & (RTN_RTINFO|RTN_ROOT)) ||
	     (fn->fn_flags & RTN_TL_ROOT &&
	      !rcu_access_pointer(fn->leaf))))
		fib6_repair_tree(info->nl_net, table, fn);
	return err;
}

/*
 *	Routing tree lookup
 *
 */

struct lookup_args {
	int			offset;		/* key offset on fib6_info */
	const struct in6_addr	*addr;		/* search key			*/
};

static struct fib6_node *fib6_node_lookup_1(struct fib6_node *root,
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

		next = dir ? rcu_dereference(fn->right) :
			     rcu_dereference(fn->left);

		if (next) {
			fn = next;
			continue;
		}
		break;
	}

	while (fn) {
		struct fib6_node *subtree = FIB6_SUBTREE(fn);

		if (subtree || fn->fn_flags & RTN_RTINFO) {
			struct fib6_info *leaf = rcu_dereference(fn->leaf);
			struct rt6key *key;

			if (!leaf)
				goto backtrack;

			key = (struct rt6key *) ((u8 *)leaf + args->offset);

			if (ipv6_prefix_equal(&key->addr, args->addr, key->plen)) {
#ifdef CONFIG_IPV6_SUBTREES
				if (subtree) {
					struct fib6_node *sfn;
					sfn = fib6_node_lookup_1(subtree,
								 args + 1);
					if (!sfn)
						goto backtrack;
					fn = sfn;
				}
#endif
				if (fn->fn_flags & RTN_RTINFO)
					return fn;
			}
		}
backtrack:
		if (fn->fn_flags & RTN_ROOT)
			break;

		fn = rcu_dereference(fn->parent);
	}

	return NULL;
}

/* called with rcu_read_lock() held
 */
struct fib6_node *fib6_node_lookup(struct fib6_node *root,
				   const struct in6_addr *daddr,
				   const struct in6_addr *saddr)
{
	struct fib6_node *fn;
	struct lookup_args args[] = {
		{
			.offset = offsetof(struct fib6_info, fib6_dst),
			.addr = daddr,
		},
#ifdef CONFIG_IPV6_SUBTREES
		{
			.offset = offsetof(struct fib6_info, fib6_src),
			.addr = saddr,
		},
#endif
		{
			.offset = 0,	/* sentinel */
		}
	};

	fn = fib6_node_lookup_1(root, daddr ? args : args + 1);
	if (!fn || fn->fn_flags & RTN_TL_ROOT)
		fn = root;

	return fn;
}

/*
 *	Get node with specified destination prefix (and source prefix,
 *	if subtrees are used)
 *	exact_match == true means we try to find fn with exact match of
 *	the passed in prefix addr
 *	exact_match == false means we try to find fn with longest prefix
 *	match of the passed in prefix addr. This is useful for finding fn
 *	for cached route as it will be stored in the exception table under
 *	the node with longest prefix length.
 */


static struct fib6_node *fib6_locate_1(struct fib6_node *root,
				       const struct in6_addr *addr,
				       int plen, int offset,
				       bool exact_match)
{
	struct fib6_node *fn, *prev = NULL;

	for (fn = root; fn ; ) {
		struct fib6_info *leaf = rcu_dereference(fn->leaf);
		struct rt6key *key;

		/* This node is being deleted */
		if (!leaf) {
			if (plen <= fn->fn_bit)
				goto out;
			else
				goto next;
		}

		key = (struct rt6key *)((u8 *)leaf + offset);

		/*
		 *	Prefix match
		 */
		if (plen < fn->fn_bit ||
		    !ipv6_prefix_equal(&key->addr, addr, fn->fn_bit))
			goto out;

		if (plen == fn->fn_bit)
			return fn;

		prev = fn;

next:
		/*
		 *	We have more bits to go
		 */
		if (addr_bit_set(addr, fn->fn_bit))
			fn = rcu_dereference(fn->right);
		else
			fn = rcu_dereference(fn->left);
	}
out:
	if (exact_match)
		return NULL;
	else
		return prev;
}

struct fib6_node *fib6_locate(struct fib6_node *root,
			      const struct in6_addr *daddr, int dst_len,
			      const struct in6_addr *saddr, int src_len,
			      bool exact_match)
{
	struct fib6_node *fn;

	fn = fib6_locate_1(root, daddr, dst_len,
			   offsetof(struct fib6_info, fib6_dst),
			   exact_match);

#ifdef CONFIG_IPV6_SUBTREES
	if (src_len) {
		WARN_ON(saddr == NULL);
		if (fn) {
			struct fib6_node *subtree = FIB6_SUBTREE(fn);

			if (subtree) {
				fn = fib6_locate_1(subtree, saddr, src_len,
					   offsetof(struct fib6_info, fib6_src),
					   exact_match);
			}
		}
	}
#endif

	if (fn && fn->fn_flags & RTN_RTINFO)
		return fn;

	return NULL;
}


/*
 *	Deletion
 *
 */

static struct fib6_info *fib6_find_prefix(struct net *net,
					 struct fib6_table *table,
					 struct fib6_node *fn)
{
	struct fib6_node *child_left, *child_right;

	if (fn->fn_flags & RTN_ROOT)
		return net->ipv6.fib6_null_entry;

	while (fn) {
		child_left = rcu_dereference_protected(fn->left,
				    lockdep_is_held(&table->tb6_lock));
		child_right = rcu_dereference_protected(fn->right,
				    lockdep_is_held(&table->tb6_lock));
		if (child_left)
			return rcu_dereference_protected(child_left->leaf,
					lockdep_is_held(&table->tb6_lock));
		if (child_right)
			return rcu_dereference_protected(child_right->leaf,
					lockdep_is_held(&table->tb6_lock));

		fn = FIB6_SUBTREE(fn);
	}
	return NULL;
}

/*
 *	Called to trim the tree of intermediate nodes when possible. "fn"
 *	is the node we want to try and remove.
 *	Need to own table->tb6_lock
 */

static struct fib6_node *fib6_repair_tree(struct net *net,
					  struct fib6_table *table,
					  struct fib6_node *fn)
{
	int children;
	int nstate;
	struct fib6_node *child;
	struct fib6_walker *w;
	int iter = 0;

	/* Set fn->leaf to null_entry for root node. */
	if (fn->fn_flags & RTN_TL_ROOT) {
		rcu_assign_pointer(fn->leaf, net->ipv6.fib6_null_entry);
		return fn;
	}

	for (;;) {
		struct fib6_node *fn_r = rcu_dereference_protected(fn->right,
					    lockdep_is_held(&table->tb6_lock));
		struct fib6_node *fn_l = rcu_dereference_protected(fn->left,
					    lockdep_is_held(&table->tb6_lock));
		struct fib6_node *pn = rcu_dereference_protected(fn->parent,
					    lockdep_is_held(&table->tb6_lock));
		struct fib6_node *pn_r = rcu_dereference_protected(pn->right,
					    lockdep_is_held(&table->tb6_lock));
		struct fib6_node *pn_l = rcu_dereference_protected(pn->left,
					    lockdep_is_held(&table->tb6_lock));
		struct fib6_info *fn_leaf = rcu_dereference_protected(fn->leaf,
					    lockdep_is_held(&table->tb6_lock));
		struct fib6_info *pn_leaf = rcu_dereference_protected(pn->leaf,
					    lockdep_is_held(&table->tb6_lock));
		struct fib6_info *new_fn_leaf;

		RT6_TRACE("fixing tree: plen=%d iter=%d\n", fn->fn_bit, iter);
		iter++;

		WARN_ON(fn->fn_flags & RTN_RTINFO);
		WARN_ON(fn->fn_flags & RTN_TL_ROOT);
		WARN_ON(fn_leaf);

		children = 0;
		child = NULL;
		if (fn_r)
			child = fn_r, children |= 1;
		if (fn_l)
			child = fn_l, children |= 2;

		if (children == 3 || FIB6_SUBTREE(fn)
#ifdef CONFIG_IPV6_SUBTREES
		    /* Subtree root (i.e. fn) may have one child */
		    || (children && fn->fn_flags & RTN_ROOT)
#endif
		    ) {
			new_fn_leaf = fib6_find_prefix(net, table, fn);
#if RT6_DEBUG >= 2
			if (!new_fn_leaf) {
				WARN_ON(!new_fn_leaf);
				new_fn_leaf = net->ipv6.fib6_null_entry;
			}
#endif
			fib6_info_hold(new_fn_leaf);
			rcu_assign_pointer(fn->leaf, new_fn_leaf);
			return pn;
		}

#ifdef CONFIG_IPV6_SUBTREES
		if (FIB6_SUBTREE(pn) == fn) {
			WARN_ON(!(fn->fn_flags & RTN_ROOT));
			RCU_INIT_POINTER(pn->subtree, NULL);
			nstate = FWS_L;
		} else {
			WARN_ON(fn->fn_flags & RTN_ROOT);
#endif
			if (pn_r == fn)
				rcu_assign_pointer(pn->right, child);
			else if (pn_l == fn)
				rcu_assign_pointer(pn->left, child);
#if RT6_DEBUG >= 2
			else
				WARN_ON(1);
#endif
			if (child)
				rcu_assign_pointer(child->parent, pn);
			nstate = FWS_R;
#ifdef CONFIG_IPV6_SUBTREES
		}
#endif

		read_lock(&net->ipv6.fib6_walker_lock);
		FOR_WALKERS(net, w) {
			if (!child) {
				if (w->node == fn) {
					RT6_TRACE("W %p adjusted by delnode 1, s=%d/%d\n", w, w->state, nstate);
					w->node = pn;
					w->state = nstate;
				}
			} else {
				if (w->node == fn) {
					w->node = child;
					if (children&2) {
						RT6_TRACE("W %p adjusted by delnode 2, s=%d\n", w, w->state);
						w->state = w->state >= FWS_R ? FWS_U : FWS_INIT;
					} else {
						RT6_TRACE("W %p adjusted by delnode 2, s=%d\n", w, w->state);
						w->state = w->state >= FWS_C ? FWS_U : FWS_INIT;
					}
				}
			}
		}
		read_unlock(&net->ipv6.fib6_walker_lock);

		node_free(net, fn);
		if (pn->fn_flags & RTN_RTINFO || FIB6_SUBTREE(pn))
			return pn;

		RCU_INIT_POINTER(pn->leaf, NULL);
		fib6_info_release(pn_leaf);
		fn = pn;
	}
}

static void fib6_del_route(struct fib6_table *table, struct fib6_node *fn,
			   struct fib6_info __rcu **rtp, struct nl_info *info)
{
	struct fib6_walker *w;
	struct fib6_info *rt = rcu_dereference_protected(*rtp,
				    lockdep_is_held(&table->tb6_lock));
	struct net *net = info->nl_net;

	RT6_TRACE("fib6_del_route\n");

	/* Unlink it */
	*rtp = rt->fib6_next;
	rt->fib6_node = NULL;
	net->ipv6.rt6_stats->fib_rt_entries--;
	net->ipv6.rt6_stats->fib_discarded_routes++;

	/* Flush all cached dst in exception table */
	rt6_flush_exceptions(rt);

	/* Reset round-robin state, if necessary */
	if (rcu_access_pointer(fn->rr_ptr) == rt)
		fn->rr_ptr = NULL;

	/* Remove this entry from other siblings */
	if (rt->fib6_nsiblings) {
		struct fib6_info *sibling, *next_sibling;

		list_for_each_entry_safe(sibling, next_sibling,
					 &rt->fib6_siblings, fib6_siblings)
			sibling->fib6_nsiblings--;
		rt->fib6_nsiblings = 0;
		list_del_init(&rt->fib6_siblings);
		rt6_multipath_rebalance(next_sibling);
	}

	/* Adjust walkers */
	read_lock(&net->ipv6.fib6_walker_lock);
	FOR_WALKERS(net, w) {
		if (w->state == FWS_C && w->leaf == rt) {
			RT6_TRACE("walker %p adjusted by delroute\n", w);
			w->leaf = rcu_dereference_protected(rt->fib6_next,
					    lockdep_is_held(&table->tb6_lock));
			if (!w->leaf)
				w->state = FWS_U;
		}
	}
	read_unlock(&net->ipv6.fib6_walker_lock);

	/* If it was last route, call fib6_repair_tree() to:
	 * 1. For root node, put back null_entry as how the table was created.
	 * 2. For other nodes, expunge its radix tree node.
	 */
	if (!rcu_access_pointer(fn->leaf)) {
		if (!(fn->fn_flags & RTN_TL_ROOT)) {
			fn->fn_flags &= ~RTN_RTINFO;
			net->ipv6.rt6_stats->fib_route_nodes--;
		}
		fn = fib6_repair_tree(net, table, fn);
	}

	fib6_purge_rt(rt, fn, net);

	call_fib6_entry_notifiers(net, FIB_EVENT_ENTRY_DEL, rt, NULL);
	if (!info->skip_notify)
		inet6_rt_notify(RTM_DELROUTE, rt, info, 0);
	fib6_info_release(rt);
}

/* Need to own table->tb6_lock */
int fib6_del(struct fib6_info *rt, struct nl_info *info)
{
	struct fib6_node *fn = rcu_dereference_protected(rt->fib6_node,
				    lockdep_is_held(&rt->fib6_table->tb6_lock));
	struct fib6_table *table = rt->fib6_table;
	struct net *net = info->nl_net;
	struct fib6_info __rcu **rtp;
	struct fib6_info __rcu **rtp_next;

	if (!fn || rt == net->ipv6.fib6_null_entry)
		return -ENOENT;

	WARN_ON(!(fn->fn_flags & RTN_RTINFO));

	/*
	 *	Walk the leaf entries looking for ourself
	 */

	for (rtp = &fn->leaf; *rtp; rtp = rtp_next) {
		struct fib6_info *cur = rcu_dereference_protected(*rtp,
					lockdep_is_held(&table->tb6_lock));
		if (rt == cur) {
			fib6_del_route(table, fn, rtp, info);
			return 0;
		}
		rtp_next = &cur->fib6_next;
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
 *
 *	This function is called with tb6_lock held.
 */

static int fib6_walk_continue(struct fib6_walker *w)
{
	struct fib6_node *fn, *pn, *left, *right;

	/* w->root should always be table->tb6_root */
	WARN_ON_ONCE(!(w->root->fn_flags & RTN_TL_ROOT));

	for (;;) {
		fn = w->node;
		if (!fn)
			return 0;

		switch (w->state) {
#ifdef CONFIG_IPV6_SUBTREES
		case FWS_S:
			if (FIB6_SUBTREE(fn)) {
				w->node = FIB6_SUBTREE(fn);
				continue;
			}
			w->state = FWS_L;
#endif
			/* fall through */
		case FWS_L:
			left = rcu_dereference_protected(fn->left, 1);
			if (left) {
				w->node = left;
				w->state = FWS_INIT;
				continue;
			}
			w->state = FWS_R;
			/* fall through */
		case FWS_R:
			right = rcu_dereference_protected(fn->right, 1);
			if (right) {
				w->node = right;
				w->state = FWS_INIT;
				continue;
			}
			w->state = FWS_C;
			w->leaf = rcu_dereference_protected(fn->leaf, 1);
			/* fall through */
		case FWS_C:
			if (w->leaf && fn->fn_flags & RTN_RTINFO) {
				int err;

				if (w->skip) {
					w->skip--;
					goto skip;
				}

				err = w->func(w);
				if (err)
					return err;

				w->count++;
				continue;
			}
skip:
			w->state = FWS_U;
			/* fall through */
		case FWS_U:
			if (fn == w->root)
				return 0;
			pn = rcu_dereference_protected(fn->parent, 1);
			left = rcu_dereference_protected(pn->left, 1);
			right = rcu_dereference_protected(pn->right, 1);
			w->node = pn;
#ifdef CONFIG_IPV6_SUBTREES
			if (FIB6_SUBTREE(pn) == fn) {
				WARN_ON(!(fn->fn_flags & RTN_ROOT));
				w->state = FWS_L;
				continue;
			}
#endif
			if (left == fn) {
				w->state = FWS_R;
				continue;
			}
			if (right == fn) {
				w->state = FWS_C;
				w->leaf = rcu_dereference_protected(w->node->leaf, 1);
				continue;
			}
#if RT6_DEBUG >= 2
			WARN_ON(1);
#endif
		}
	}
}

static int fib6_walk(struct net *net, struct fib6_walker *w)
{
	int res;

	w->state = FWS_INIT;
	w->node = w->root;

	fib6_walker_link(net, w);
	res = fib6_walk_continue(w);
	if (res <= 0)
		fib6_walker_unlink(net, w);
	return res;
}

static int fib6_clean_node(struct fib6_walker *w)
{
	int res;
	struct fib6_info *rt;
	struct fib6_cleaner *c = container_of(w, struct fib6_cleaner, w);
	struct nl_info info = {
		.nl_net = c->net,
		.skip_notify = c->skip_notify,
	};

	if (c->sernum != FIB6_NO_SERNUM_CHANGE &&
	    w->node->fn_sernum != c->sernum)
		w->node->fn_sernum = c->sernum;

	if (!c->func) {
		WARN_ON_ONCE(c->sernum == FIB6_NO_SERNUM_CHANGE);
		w->leaf = NULL;
		return 0;
	}

	for_each_fib6_walker_rt(w) {
		res = c->func(rt, c->arg);
		if (res == -1) {
			w->leaf = rt;
			res = fib6_del(rt, &info);
			if (res) {
#if RT6_DEBUG >= 2
				pr_debug("%s: del failed: rt=%p@%p err=%d\n",
					 __func__, rt,
					 rcu_access_pointer(rt->fib6_node),
					 res);
#endif
				continue;
			}
			return 0;
		} else if (res == -2) {
			if (WARN_ON(!rt->fib6_nsiblings))
				continue;
			rt = list_last_entry(&rt->fib6_siblings,
					     struct fib6_info, fib6_siblings);
			continue;
		}
		WARN_ON(res != 0);
	}
	w->leaf = rt;
	return 0;
}

/*
 *	Convenient frontend to tree walker.
 *
 *	func is called on each route.
 *		It may return -2 -> skip multipath route.
 *			      -1 -> delete this route.
 *		              0  -> continue walking
 */

static void fib6_clean_tree(struct net *net, struct fib6_node *root,
			    int (*func)(struct fib6_info *, void *arg),
			    int sernum, void *arg, bool skip_notify)
{
	struct fib6_cleaner c;

	c.w.root = root;
	c.w.func = fib6_clean_node;
	c.w.count = 0;
	c.w.skip = 0;
	c.func = func;
	c.sernum = sernum;
	c.arg = arg;
	c.net = net;
	c.skip_notify = skip_notify;

	fib6_walk(net, &c.w);
}

static void __fib6_clean_all(struct net *net,
			     int (*func)(struct fib6_info *, void *),
			     int sernum, void *arg, bool skip_notify)
{
	struct fib6_table *table;
	struct hlist_head *head;
	unsigned int h;

	rcu_read_lock();
	for (h = 0; h < FIB6_TABLE_HASHSZ; h++) {
		head = &net->ipv6.fib_table_hash[h];
		hlist_for_each_entry_rcu(table, head, tb6_hlist) {
			spin_lock_bh(&table->tb6_lock);
			fib6_clean_tree(net, &table->tb6_root,
					func, sernum, arg, skip_notify);
			spin_unlock_bh(&table->tb6_lock);
		}
	}
	rcu_read_unlock();
}

void fib6_clean_all(struct net *net, int (*func)(struct fib6_info *, void *),
		    void *arg)
{
	__fib6_clean_all(net, func, FIB6_NO_SERNUM_CHANGE, arg, false);
}

void fib6_clean_all_skip_notify(struct net *net,
				int (*func)(struct fib6_info *, void *),
				void *arg)
{
	__fib6_clean_all(net, func, FIB6_NO_SERNUM_CHANGE, arg, true);
}

static void fib6_flush_trees(struct net *net)
{
	int new_sernum = fib6_new_sernum(net);

	__fib6_clean_all(net, NULL, new_sernum, NULL, false);
}

/*
 *	Garbage collection
 */

static int fib6_age(struct fib6_info *rt, void *arg)
{
	struct fib6_gc_args *gc_args = arg;
	unsigned long now = jiffies;

	/*
	 *	check addrconf expiration here.
	 *	Routes are expired even if they are in use.
	 */

	if (rt->fib6_flags & RTF_EXPIRES && rt->expires) {
		if (time_after(now, rt->expires)) {
			RT6_TRACE("expiring %p\n", rt);
			return -1;
		}
		gc_args->more++;
	}

	/*	Also age clones in the exception table.
	 *	Note, that clones are aged out
	 *	only if they are not in use now.
	 */
	rt6_age_exceptions(rt, gc_args, now);

	return 0;
}

void fib6_run_gc(unsigned long expires, struct net *net, bool force)
{
	struct fib6_gc_args gc_args;
	unsigned long now;

	if (force) {
		spin_lock_bh(&net->ipv6.fib6_gc_lock);
	} else if (!spin_trylock_bh(&net->ipv6.fib6_gc_lock)) {
		mod_timer(&net->ipv6.ip6_fib_timer, jiffies + HZ);
		return;
	}
	gc_args.timeout = expires ? (int)expires :
			  net->ipv6.sysctl.ip6_rt_gc_interval;
	gc_args.more = 0;

	fib6_clean_all(net, fib6_age, &gc_args);
	now = jiffies;
	net->ipv6.ip6_rt_last_gc = now;

	if (gc_args.more)
		mod_timer(&net->ipv6.ip6_fib_timer,
			  round_jiffies(now
					+ net->ipv6.sysctl.ip6_rt_gc_interval));
	else
		del_timer(&net->ipv6.ip6_fib_timer);
	spin_unlock_bh(&net->ipv6.fib6_gc_lock);
}

static void fib6_gc_timer_cb(struct timer_list *t)
{
	struct net *arg = from_timer(arg, t, ipv6.ip6_fib_timer);

	fib6_run_gc(0, arg, true);
}

static int __net_init fib6_net_init(struct net *net)
{
	size_t size = sizeof(struct hlist_head) * FIB6_TABLE_HASHSZ;
	int err;

	err = fib6_notifier_init(net);
	if (err)
		return err;

	spin_lock_init(&net->ipv6.fib6_gc_lock);
	rwlock_init(&net->ipv6.fib6_walker_lock);
	INIT_LIST_HEAD(&net->ipv6.fib6_walkers);
	timer_setup(&net->ipv6.ip6_fib_timer, fib6_gc_timer_cb, 0);

	net->ipv6.rt6_stats = kzalloc(sizeof(*net->ipv6.rt6_stats), GFP_KERNEL);
	if (!net->ipv6.rt6_stats)
		goto out_timer;

	/* Avoid false sharing : Use at least a full cache line */
	size = max_t(size_t, size, L1_CACHE_BYTES);

	net->ipv6.fib_table_hash = kzalloc(size, GFP_KERNEL);
	if (!net->ipv6.fib_table_hash)
		goto out_rt6_stats;

	net->ipv6.fib6_main_tbl = kzalloc(sizeof(*net->ipv6.fib6_main_tbl),
					  GFP_KERNEL);
	if (!net->ipv6.fib6_main_tbl)
		goto out_fib_table_hash;

	net->ipv6.fib6_main_tbl->tb6_id = RT6_TABLE_MAIN;
	rcu_assign_pointer(net->ipv6.fib6_main_tbl->tb6_root.leaf,
			   net->ipv6.fib6_null_entry);
	net->ipv6.fib6_main_tbl->tb6_root.fn_flags =
		RTN_ROOT | RTN_TL_ROOT | RTN_RTINFO;
	inet_peer_base_init(&net->ipv6.fib6_main_tbl->tb6_peers);

#ifdef CONFIG_IPV6_MULTIPLE_TABLES
	net->ipv6.fib6_local_tbl = kzalloc(sizeof(*net->ipv6.fib6_local_tbl),
					   GFP_KERNEL);
	if (!net->ipv6.fib6_local_tbl)
		goto out_fib6_main_tbl;
	net->ipv6.fib6_local_tbl->tb6_id = RT6_TABLE_LOCAL;
	rcu_assign_pointer(net->ipv6.fib6_local_tbl->tb6_root.leaf,
			   net->ipv6.fib6_null_entry);
	net->ipv6.fib6_local_tbl->tb6_root.fn_flags =
		RTN_ROOT | RTN_TL_ROOT | RTN_RTINFO;
	inet_peer_base_init(&net->ipv6.fib6_local_tbl->tb6_peers);
#endif
	fib6_tables_init(net);

	return 0;

#ifdef CONFIG_IPV6_MULTIPLE_TABLES
out_fib6_main_tbl:
	kfree(net->ipv6.fib6_main_tbl);
#endif
out_fib_table_hash:
	kfree(net->ipv6.fib_table_hash);
out_rt6_stats:
	kfree(net->ipv6.rt6_stats);
out_timer:
	fib6_notifier_exit(net);
	return -ENOMEM;
}

static void fib6_net_exit(struct net *net)
{
	unsigned int i;

	del_timer_sync(&net->ipv6.ip6_fib_timer);

	for (i = 0; i < FIB6_TABLE_HASHSZ; i++) {
		struct hlist_head *head = &net->ipv6.fib_table_hash[i];
		struct hlist_node *tmp;
		struct fib6_table *tb;

		hlist_for_each_entry_safe(tb, tmp, head, tb6_hlist) {
			hlist_del(&tb->tb6_hlist);
			fib6_free_table(tb);
		}
	}

	kfree(net->ipv6.fib_table_hash);
	kfree(net->ipv6.rt6_stats);
	fib6_notifier_exit(net);
}

static struct pernet_operations fib6_net_ops = {
	.init = fib6_net_init,
	.exit = fib6_net_exit,
};

int __init fib6_init(void)
{
	int ret = -ENOMEM;

	fib6_node_kmem = kmem_cache_create("fib6_nodes",
					   sizeof(struct fib6_node),
					   0, SLAB_HWCACHE_ALIGN,
					   NULL);
	if (!fib6_node_kmem)
		goto out;

	ret = register_pernet_subsys(&fib6_net_ops);
	if (ret)
		goto out_kmem_cache_create;

	ret = rtnl_register_module(THIS_MODULE, PF_INET6, RTM_GETROUTE, NULL,
				   inet6_dump_fib, 0);
	if (ret)
		goto out_unregister_subsys;

	__fib6_flush_trees = fib6_flush_trees;
out:
	return ret;

out_unregister_subsys:
	unregister_pernet_subsys(&fib6_net_ops);
out_kmem_cache_create:
	kmem_cache_destroy(fib6_node_kmem);
	goto out;
}

void fib6_gc_cleanup(void)
{
	unregister_pernet_subsys(&fib6_net_ops);
	kmem_cache_destroy(fib6_node_kmem);
}

#ifdef CONFIG_PROC_FS
static int ipv6_route_seq_show(struct seq_file *seq, void *v)
{
	struct fib6_info *rt = v;
	struct ipv6_route_iter *iter = seq->private;
	const struct net_device *dev;

	seq_printf(seq, "%pi6 %02x ", &rt->fib6_dst.addr, rt->fib6_dst.plen);

#ifdef CONFIG_IPV6_SUBTREES
	seq_printf(seq, "%pi6 %02x ", &rt->fib6_src.addr, rt->fib6_src.plen);
#else
	seq_puts(seq, "00000000000000000000000000000000 00 ");
#endif
	if (rt->fib6_flags & RTF_GATEWAY)
		seq_printf(seq, "%pi6", &rt->fib6_nh.nh_gw);
	else
		seq_puts(seq, "00000000000000000000000000000000");

	dev = rt->fib6_nh.nh_dev;
	seq_printf(seq, " %08x %08x %08x %08x %8s\n",
		   rt->fib6_metric, atomic_read(&rt->fib6_ref), 0,
		   rt->fib6_flags, dev ? dev->name : "");
	iter->w.leaf = NULL;
	return 0;
}

static int ipv6_route_yield(struct fib6_walker *w)
{
	struct ipv6_route_iter *iter = w->args;

	if (!iter->skip)
		return 1;

	do {
		iter->w.leaf = rcu_dereference_protected(
				iter->w.leaf->fib6_next,
				lockdep_is_held(&iter->tbl->tb6_lock));
		iter->skip--;
		if (!iter->skip && iter->w.leaf)
			return 1;
	} while (iter->w.leaf);

	return 0;
}

static void ipv6_route_seq_setup_walk(struct ipv6_route_iter *iter,
				      struct net *net)
{
	memset(&iter->w, 0, sizeof(iter->w));
	iter->w.func = ipv6_route_yield;
	iter->w.root = &iter->tbl->tb6_root;
	iter->w.state = FWS_INIT;
	iter->w.node = iter->w.root;
	iter->w.args = iter;
	iter->sernum = iter->w.root->fn_sernum;
	INIT_LIST_HEAD(&iter->w.lh);
	fib6_walker_link(net, &iter->w);
}

static struct fib6_table *ipv6_route_seq_next_table(struct fib6_table *tbl,
						    struct net *net)
{
	unsigned int h;
	struct hlist_node *node;

	if (tbl) {
		h = (tbl->tb6_id & (FIB6_TABLE_HASHSZ - 1)) + 1;
		node = rcu_dereference_bh(hlist_next_rcu(&tbl->tb6_hlist));
	} else {
		h = 0;
		node = NULL;
	}

	while (!node && h < FIB6_TABLE_HASHSZ) {
		node = rcu_dereference_bh(
			hlist_first_rcu(&net->ipv6.fib_table_hash[h++]));
	}
	return hlist_entry_safe(node, struct fib6_table, tb6_hlist);
}

static void ipv6_route_check_sernum(struct ipv6_route_iter *iter)
{
	if (iter->sernum != iter->w.root->fn_sernum) {
		iter->sernum = iter->w.root->fn_sernum;
		iter->w.state = FWS_INIT;
		iter->w.node = iter->w.root;
		WARN_ON(iter->w.skip);
		iter->w.skip = iter->w.count;
	}
}

static void *ipv6_route_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	int r;
	struct fib6_info *n;
	struct net *net = seq_file_net(seq);
	struct ipv6_route_iter *iter = seq->private;

	if (!v)
		goto iter_table;

	n = rcu_dereference_bh(((struct fib6_info *)v)->fib6_next);
	if (n) {
		++*pos;
		return n;
	}

iter_table:
	ipv6_route_check_sernum(iter);
	spin_lock_bh(&iter->tbl->tb6_lock);
	r = fib6_walk_continue(&iter->w);
	spin_unlock_bh(&iter->tbl->tb6_lock);
	if (r > 0) {
		if (v)
			++*pos;
		return iter->w.leaf;
	} else if (r < 0) {
		fib6_walker_unlink(net, &iter->w);
		return NULL;
	}
	fib6_walker_unlink(net, &iter->w);

	iter->tbl = ipv6_route_seq_next_table(iter->tbl, net);
	if (!iter->tbl)
		return NULL;

	ipv6_route_seq_setup_walk(iter, net);
	goto iter_table;
}

static void *ipv6_route_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU_BH)
{
	struct net *net = seq_file_net(seq);
	struct ipv6_route_iter *iter = seq->private;

	rcu_read_lock_bh();
	iter->tbl = ipv6_route_seq_next_table(NULL, net);
	iter->skip = *pos;

	if (iter->tbl) {
		ipv6_route_seq_setup_walk(iter, net);
		return ipv6_route_seq_next(seq, NULL, pos);
	} else {
		return NULL;
	}
}

static bool ipv6_route_iter_active(struct ipv6_route_iter *iter)
{
	struct fib6_walker *w = &iter->w;
	return w->node && !(w->state == FWS_U && w->node == w->root);
}

static void ipv6_route_seq_stop(struct seq_file *seq, void *v)
	__releases(RCU_BH)
{
	struct net *net = seq_file_net(seq);
	struct ipv6_route_iter *iter = seq->private;

	if (ipv6_route_iter_active(iter))
		fib6_walker_unlink(net, &iter->w);

	rcu_read_unlock_bh();
}

const struct seq_operations ipv6_route_seq_ops = {
	.start	= ipv6_route_seq_start,
	.next	= ipv6_route_seq_next,
	.stop	= ipv6_route_seq_stop,
	.show	= ipv6_route_seq_show
};
#endif /* CONFIG_PROC_FS */

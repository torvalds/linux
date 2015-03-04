/*
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either version
 *   2 of the License, or (at your option) any later version.
 *
 *   Robert Olsson <robert.olsson@its.uu.se> Uppsala Universitet
 *     & Swedish University of Agricultural Sciences.
 *
 *   Jens Laas <jens.laas@data.slu.se> Swedish University of
 *     Agricultural Sciences.
 *
 *   Hans Liss <hans.liss@its.uu.se>  Uppsala Universitet
 *
 * This work is based on the LPC-trie which is originally described in:
 *
 * An experimental study of compression methods for dynamic tries
 * Stefan Nilsson and Matti Tikkanen. Algorithmica, 33(1):19-33, 2002.
 * http://www.csc.kth.se/~snilsson/software/dyntrie2/
 *
 *
 * IP-address lookup using LC-tries. Stefan Nilsson and Gunnar Karlsson
 * IEEE Journal on Selected Areas in Communications, 17(6):1083-1092, June 1999
 *
 *
 * Code from fib_hash has been reused which includes the following header:
 *
 *
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IPv4 FIB: lookup engine and maintenance routines.
 *
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Substantial contributions to this work comes from:
 *
 *		David S. Miller, <davem@davemloft.net>
 *		Stephen Hemminger <shemminger@osdl.org>
 *		Paul E. McKenney <paulmck@us.ibm.com>
 *		Patrick McHardy <kaber@trash.net>
 */

#define VERSION "0.409"

#include <asm/uaccess.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/inetdevice.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/proc_fs.h>
#include <linux/rcupdate.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <net/net_namespace.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/sock.h>
#include <net/ip_fib.h>
#include "fib_lookup.h"

#define MAX_STAT_DEPTH 32

#define KEYLENGTH	(8*sizeof(t_key))
#define KEY_MAX		((t_key)~0)

typedef unsigned int t_key;

#define IS_TNODE(n) ((n)->bits)
#define IS_LEAF(n) (!(n)->bits)

#define get_index(_key, _kv) (((_key) ^ (_kv)->key) >> (_kv)->pos)

struct tnode {
	struct rcu_head rcu;

	t_key empty_children; /* KEYLENGTH bits needed */
	t_key full_children;  /* KEYLENGTH bits needed */
	struct tnode __rcu *parent;

	t_key key;
	unsigned char pos;		/* 2log(KEYLENGTH) bits needed */
	unsigned char bits;		/* 2log(KEYLENGTH) bits needed */
	unsigned char slen;
	union {
		/* This list pointer if valid if (pos | bits) == 0 (LEAF) */
		struct hlist_head leaf;
		/* This array is valid if (pos | bits) > 0 (TNODE) */
		struct tnode __rcu *tnode[0];
	};
};

#define TNODE_SIZE(n)	offsetof(struct tnode, tnode[n])
#define LEAF_SIZE	TNODE_SIZE(1)

#ifdef CONFIG_IP_FIB_TRIE_STATS
struct trie_use_stats {
	unsigned int gets;
	unsigned int backtrack;
	unsigned int semantic_match_passed;
	unsigned int semantic_match_miss;
	unsigned int null_node_hit;
	unsigned int resize_node_skipped;
};
#endif

struct trie_stat {
	unsigned int totdepth;
	unsigned int maxdepth;
	unsigned int tnodes;
	unsigned int leaves;
	unsigned int nullpointers;
	unsigned int prefixes;
	unsigned int nodesizes[MAX_STAT_DEPTH];
};

struct trie {
	struct tnode __rcu *trie;
#ifdef CONFIG_IP_FIB_TRIE_STATS
	struct trie_use_stats __percpu *stats;
#endif
};

static void resize(struct trie *t, struct tnode *tn);
static size_t tnode_free_size;

/*
 * synchronize_rcu after call_rcu for that many pages; it should be especially
 * useful before resizing the root node with PREEMPT_NONE configs; the value was
 * obtained experimentally, aiming to avoid visible slowdown.
 */
static const int sync_pages = 128;

static struct kmem_cache *fn_alias_kmem __read_mostly;
static struct kmem_cache *trie_leaf_kmem __read_mostly;

/* caller must hold RTNL */
#define node_parent(n) rtnl_dereference((n)->parent)

/* caller must hold RCU read lock or RTNL */
#define node_parent_rcu(n) rcu_dereference_rtnl((n)->parent)

/* wrapper for rcu_assign_pointer */
static inline void node_set_parent(struct tnode *n, struct tnode *tp)
{
	if (n)
		rcu_assign_pointer(n->parent, tp);
}

#define NODE_INIT_PARENT(n, p) RCU_INIT_POINTER((n)->parent, p)

/* This provides us with the number of children in this node, in the case of a
 * leaf this will return 0 meaning none of the children are accessible.
 */
static inline unsigned long tnode_child_length(const struct tnode *tn)
{
	return (1ul << tn->bits) & ~(1ul);
}

/* caller must hold RTNL */
static inline struct tnode *tnode_get_child(const struct tnode *tn,
					    unsigned long i)
{
	return rtnl_dereference(tn->tnode[i]);
}

/* caller must hold RCU read lock or RTNL */
static inline struct tnode *tnode_get_child_rcu(const struct tnode *tn,
						unsigned long i)
{
	return rcu_dereference_rtnl(tn->tnode[i]);
}

/* To understand this stuff, an understanding of keys and all their bits is
 * necessary. Every node in the trie has a key associated with it, but not
 * all of the bits in that key are significant.
 *
 * Consider a node 'n' and its parent 'tp'.
 *
 * If n is a leaf, every bit in its key is significant. Its presence is
 * necessitated by path compression, since during a tree traversal (when
 * searching for a leaf - unless we are doing an insertion) we will completely
 * ignore all skipped bits we encounter. Thus we need to verify, at the end of
 * a potentially successful search, that we have indeed been walking the
 * correct key path.
 *
 * Note that we can never "miss" the correct key in the tree if present by
 * following the wrong path. Path compression ensures that segments of the key
 * that are the same for all keys with a given prefix are skipped, but the
 * skipped part *is* identical for each node in the subtrie below the skipped
 * bit! trie_insert() in this implementation takes care of that.
 *
 * if n is an internal node - a 'tnode' here, the various parts of its key
 * have many different meanings.
 *
 * Example:
 * _________________________________________________________________
 * | i | i | i | i | i | i | i | N | N | N | S | S | S | S | S | C |
 * -----------------------------------------------------------------
 *  31  30  29  28  27  26  25  24  23  22  21  20  19  18  17  16
 *
 * _________________________________________________________________
 * | C | C | C | u | u | u | u | u | u | u | u | u | u | u | u | u |
 * -----------------------------------------------------------------
 *  15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
 *
 * tp->pos = 22
 * tp->bits = 3
 * n->pos = 13
 * n->bits = 4
 *
 * First, let's just ignore the bits that come before the parent tp, that is
 * the bits from (tp->pos + tp->bits) to 31. They are *known* but at this
 * point we do not use them for anything.
 *
 * The bits from (tp->pos) to (tp->pos + tp->bits - 1) - "N", above - are the
 * index into the parent's child array. That is, they will be used to find
 * 'n' among tp's children.
 *
 * The bits from (n->pos + n->bits) to (tn->pos - 1) - "S" - are skipped bits
 * for the node n.
 *
 * All the bits we have seen so far are significant to the node n. The rest
 * of the bits are really not needed or indeed known in n->key.
 *
 * The bits from (n->pos) to (n->pos + n->bits - 1) - "C" - are the index into
 * n's child array, and will of course be different for each child.
 *
 * The rest of the bits, from 0 to (n->pos + n->bits), are completely unknown
 * at this point.
 */

static const int halve_threshold = 25;
static const int inflate_threshold = 50;
static const int halve_threshold_root = 15;
static const int inflate_threshold_root = 30;

static void __alias_free_mem(struct rcu_head *head)
{
	struct fib_alias *fa = container_of(head, struct fib_alias, rcu);
	kmem_cache_free(fn_alias_kmem, fa);
}

static inline void alias_free_mem_rcu(struct fib_alias *fa)
{
	call_rcu(&fa->rcu, __alias_free_mem);
}

#define TNODE_KMALLOC_MAX \
	ilog2((PAGE_SIZE - TNODE_SIZE(0)) / sizeof(struct tnode *))

static void __node_free_rcu(struct rcu_head *head)
{
	struct tnode *n = container_of(head, struct tnode, rcu);

	if (IS_LEAF(n))
		kmem_cache_free(trie_leaf_kmem, n);
	else if (n->bits <= TNODE_KMALLOC_MAX)
		kfree(n);
	else
		vfree(n);
}

#define node_free(n) call_rcu(&n->rcu, __node_free_rcu)

static struct tnode *tnode_alloc(size_t size)
{
	if (size <= PAGE_SIZE)
		return kzalloc(size, GFP_KERNEL);
	else
		return vzalloc(size);
}

static inline void empty_child_inc(struct tnode *n)
{
	++n->empty_children ? : ++n->full_children;
}

static inline void empty_child_dec(struct tnode *n)
{
	n->empty_children-- ? : n->full_children--;
}

static struct tnode *leaf_new(t_key key, struct fib_alias *fa)
{
	struct tnode *l = kmem_cache_alloc(trie_leaf_kmem, GFP_KERNEL);
	if (l) {
		l->parent = NULL;
		/* set key and pos to reflect full key value
		 * any trailing zeros in the key should be ignored
		 * as the nodes are searched
		 */
		l->key = key;
		l->slen = fa->fa_slen;
		l->pos = 0;
		/* set bits to 0 indicating we are not a tnode */
		l->bits = 0;

		/* link leaf to fib alias */
		INIT_HLIST_HEAD(&l->leaf);
		hlist_add_head(&fa->fa_list, &l->leaf);
	}
	return l;
}

static struct tnode *tnode_new(t_key key, int pos, int bits)
{
	size_t sz = TNODE_SIZE(1ul << bits);
	struct tnode *tn = tnode_alloc(sz);
	unsigned int shift = pos + bits;

	/* verify bits and pos their msb bits clear and values are valid */
	BUG_ON(!bits || (shift > KEYLENGTH));

	if (tn) {
		tn->parent = NULL;
		tn->slen = pos;
		tn->pos = pos;
		tn->bits = bits;
		tn->key = (shift < KEYLENGTH) ? (key >> shift) << shift : 0;
		if (bits == KEYLENGTH)
			tn->full_children = 1;
		else
			tn->empty_children = 1ul << bits;
	}

	pr_debug("AT %p s=%zu %zu\n", tn, TNODE_SIZE(0),
		 sizeof(struct tnode *) << bits);
	return tn;
}

/* Check whether a tnode 'n' is "full", i.e. it is an internal node
 * and no bits are skipped. See discussion in dyntree paper p. 6
 */
static inline int tnode_full(const struct tnode *tn, const struct tnode *n)
{
	return n && ((n->pos + n->bits) == tn->pos) && IS_TNODE(n);
}

/* Add a child at position i overwriting the old value.
 * Update the value of full_children and empty_children.
 */
static void put_child(struct tnode *tn, unsigned long i, struct tnode *n)
{
	struct tnode *chi = tnode_get_child(tn, i);
	int isfull, wasfull;

	BUG_ON(i >= tnode_child_length(tn));

	/* update emptyChildren, overflow into fullChildren */
	if (n == NULL && chi != NULL)
		empty_child_inc(tn);
	if (n != NULL && chi == NULL)
		empty_child_dec(tn);

	/* update fullChildren */
	wasfull = tnode_full(tn, chi);
	isfull = tnode_full(tn, n);

	if (wasfull && !isfull)
		tn->full_children--;
	else if (!wasfull && isfull)
		tn->full_children++;

	if (n && (tn->slen < n->slen))
		tn->slen = n->slen;

	rcu_assign_pointer(tn->tnode[i], n);
}

static void update_children(struct tnode *tn)
{
	unsigned long i;

	/* update all of the child parent pointers */
	for (i = tnode_child_length(tn); i;) {
		struct tnode *inode = tnode_get_child(tn, --i);

		if (!inode)
			continue;

		/* Either update the children of a tnode that
		 * already belongs to us or update the child
		 * to point to ourselves.
		 */
		if (node_parent(inode) == tn)
			update_children(inode);
		else
			node_set_parent(inode, tn);
	}
}

static inline void put_child_root(struct tnode *tp, struct trie *t,
				  t_key key, struct tnode *n)
{
	if (tp)
		put_child(tp, get_index(key, tp), n);
	else
		rcu_assign_pointer(t->trie, n);
}

static inline void tnode_free_init(struct tnode *tn)
{
	tn->rcu.next = NULL;
}

static inline void tnode_free_append(struct tnode *tn, struct tnode *n)
{
	n->rcu.next = tn->rcu.next;
	tn->rcu.next = &n->rcu;
}

static void tnode_free(struct tnode *tn)
{
	struct callback_head *head = &tn->rcu;

	while (head) {
		head = head->next;
		tnode_free_size += TNODE_SIZE(1ul << tn->bits);
		node_free(tn);

		tn = container_of(head, struct tnode, rcu);
	}

	if (tnode_free_size >= PAGE_SIZE * sync_pages) {
		tnode_free_size = 0;
		synchronize_rcu();
	}
}

static void replace(struct trie *t, struct tnode *oldtnode, struct tnode *tn)
{
	struct tnode *tp = node_parent(oldtnode);
	unsigned long i;

	/* setup the parent pointer out of and back into this node */
	NODE_INIT_PARENT(tn, tp);
	put_child_root(tp, t, tn->key, tn);

	/* update all of the child parent pointers */
	update_children(tn);

	/* all pointers should be clean so we are done */
	tnode_free(oldtnode);

	/* resize children now that oldtnode is freed */
	for (i = tnode_child_length(tn); i;) {
		struct tnode *inode = tnode_get_child(tn, --i);

		/* resize child node */
		if (tnode_full(tn, inode))
			resize(t, inode);
	}
}

static int inflate(struct trie *t, struct tnode *oldtnode)
{
	struct tnode *tn;
	unsigned long i;
	t_key m;

	pr_debug("In inflate\n");

	tn = tnode_new(oldtnode->key, oldtnode->pos - 1, oldtnode->bits + 1);
	if (!tn)
		return -ENOMEM;

	/* prepare oldtnode to be freed */
	tnode_free_init(oldtnode);

	/* Assemble all of the pointers in our cluster, in this case that
	 * represents all of the pointers out of our allocated nodes that
	 * point to existing tnodes and the links between our allocated
	 * nodes.
	 */
	for (i = tnode_child_length(oldtnode), m = 1u << tn->pos; i;) {
		struct tnode *inode = tnode_get_child(oldtnode, --i);
		struct tnode *node0, *node1;
		unsigned long j, k;

		/* An empty child */
		if (inode == NULL)
			continue;

		/* A leaf or an internal node with skipped bits */
		if (!tnode_full(oldtnode, inode)) {
			put_child(tn, get_index(inode->key, tn), inode);
			continue;
		}

		/* drop the node in the old tnode free list */
		tnode_free_append(oldtnode, inode);

		/* An internal node with two children */
		if (inode->bits == 1) {
			put_child(tn, 2 * i + 1, tnode_get_child(inode, 1));
			put_child(tn, 2 * i, tnode_get_child(inode, 0));
			continue;
		}

		/* We will replace this node 'inode' with two new
		 * ones, 'node0' and 'node1', each with half of the
		 * original children. The two new nodes will have
		 * a position one bit further down the key and this
		 * means that the "significant" part of their keys
		 * (see the discussion near the top of this file)
		 * will differ by one bit, which will be "0" in
		 * node0's key and "1" in node1's key. Since we are
		 * moving the key position by one step, the bit that
		 * we are moving away from - the bit at position
		 * (tn->pos) - is the one that will differ between
		 * node0 and node1. So... we synthesize that bit in the
		 * two new keys.
		 */
		node1 = tnode_new(inode->key | m, inode->pos, inode->bits - 1);
		if (!node1)
			goto nomem;
		node0 = tnode_new(inode->key, inode->pos, inode->bits - 1);

		tnode_free_append(tn, node1);
		if (!node0)
			goto nomem;
		tnode_free_append(tn, node0);

		/* populate child pointers in new nodes */
		for (k = tnode_child_length(inode), j = k / 2; j;) {
			put_child(node1, --j, tnode_get_child(inode, --k));
			put_child(node0, j, tnode_get_child(inode, j));
			put_child(node1, --j, tnode_get_child(inode, --k));
			put_child(node0, j, tnode_get_child(inode, j));
		}

		/* link new nodes to parent */
		NODE_INIT_PARENT(node1, tn);
		NODE_INIT_PARENT(node0, tn);

		/* link parent to nodes */
		put_child(tn, 2 * i + 1, node1);
		put_child(tn, 2 * i, node0);
	}

	/* setup the parent pointers into and out of this node */
	replace(t, oldtnode, tn);

	return 0;
nomem:
	/* all pointers should be clean so we are done */
	tnode_free(tn);
	return -ENOMEM;
}

static int halve(struct trie *t, struct tnode *oldtnode)
{
	struct tnode *tn;
	unsigned long i;

	pr_debug("In halve\n");

	tn = tnode_new(oldtnode->key, oldtnode->pos + 1, oldtnode->bits - 1);
	if (!tn)
		return -ENOMEM;

	/* prepare oldtnode to be freed */
	tnode_free_init(oldtnode);

	/* Assemble all of the pointers in our cluster, in this case that
	 * represents all of the pointers out of our allocated nodes that
	 * point to existing tnodes and the links between our allocated
	 * nodes.
	 */
	for (i = tnode_child_length(oldtnode); i;) {
		struct tnode *node1 = tnode_get_child(oldtnode, --i);
		struct tnode *node0 = tnode_get_child(oldtnode, --i);
		struct tnode *inode;

		/* At least one of the children is empty */
		if (!node1 || !node0) {
			put_child(tn, i / 2, node1 ? : node0);
			continue;
		}

		/* Two nonempty children */
		inode = tnode_new(node0->key, oldtnode->pos, 1);
		if (!inode) {
			tnode_free(tn);
			return -ENOMEM;
		}
		tnode_free_append(tn, inode);

		/* initialize pointers out of node */
		put_child(inode, 1, node1);
		put_child(inode, 0, node0);
		NODE_INIT_PARENT(inode, tn);

		/* link parent to node */
		put_child(tn, i / 2, inode);
	}

	/* setup the parent pointers into and out of this node */
	replace(t, oldtnode, tn);

	return 0;
}

static void collapse(struct trie *t, struct tnode *oldtnode)
{
	struct tnode *n, *tp;
	unsigned long i;

	/* scan the tnode looking for that one child that might still exist */
	for (n = NULL, i = tnode_child_length(oldtnode); !n && i;)
		n = tnode_get_child(oldtnode, --i);

	/* compress one level */
	tp = node_parent(oldtnode);
	put_child_root(tp, t, oldtnode->key, n);
	node_set_parent(n, tp);

	/* drop dead node */
	node_free(oldtnode);
}

static unsigned char update_suffix(struct tnode *tn)
{
	unsigned char slen = tn->pos;
	unsigned long stride, i;

	/* search though the list of children looking for nodes that might
	 * have a suffix greater than the one we currently have.  This is
	 * why we start with a stride of 2 since a stride of 1 would
	 * represent the nodes with suffix length equal to tn->pos
	 */
	for (i = 0, stride = 0x2ul ; i < tnode_child_length(tn); i += stride) {
		struct tnode *n = tnode_get_child(tn, i);

		if (!n || (n->slen <= slen))
			continue;

		/* update stride and slen based on new value */
		stride <<= (n->slen - slen);
		slen = n->slen;
		i &= ~(stride - 1);

		/* if slen covers all but the last bit we can stop here
		 * there will be nothing longer than that since only node
		 * 0 and 1 << (bits - 1) could have that as their suffix
		 * length.
		 */
		if ((slen + 1) >= (tn->pos + tn->bits))
			break;
	}

	tn->slen = slen;

	return slen;
}

/* From "Implementing a dynamic compressed trie" by Stefan Nilsson of
 * the Helsinki University of Technology and Matti Tikkanen of Nokia
 * Telecommunications, page 6:
 * "A node is doubled if the ratio of non-empty children to all
 * children in the *doubled* node is at least 'high'."
 *
 * 'high' in this instance is the variable 'inflate_threshold'. It
 * is expressed as a percentage, so we multiply it with
 * tnode_child_length() and instead of multiplying by 2 (since the
 * child array will be doubled by inflate()) and multiplying
 * the left-hand side by 100 (to handle the percentage thing) we
 * multiply the left-hand side by 50.
 *
 * The left-hand side may look a bit weird: tnode_child_length(tn)
 * - tn->empty_children is of course the number of non-null children
 * in the current node. tn->full_children is the number of "full"
 * children, that is non-null tnodes with a skip value of 0.
 * All of those will be doubled in the resulting inflated tnode, so
 * we just count them one extra time here.
 *
 * A clearer way to write this would be:
 *
 * to_be_doubled = tn->full_children;
 * not_to_be_doubled = tnode_child_length(tn) - tn->empty_children -
 *     tn->full_children;
 *
 * new_child_length = tnode_child_length(tn) * 2;
 *
 * new_fill_factor = 100 * (not_to_be_doubled + 2*to_be_doubled) /
 *      new_child_length;
 * if (new_fill_factor >= inflate_threshold)
 *
 * ...and so on, tho it would mess up the while () loop.
 *
 * anyway,
 * 100 * (not_to_be_doubled + 2*to_be_doubled) / new_child_length >=
 *      inflate_threshold
 *
 * avoid a division:
 * 100 * (not_to_be_doubled + 2*to_be_doubled) >=
 *      inflate_threshold * new_child_length
 *
 * expand not_to_be_doubled and to_be_doubled, and shorten:
 * 100 * (tnode_child_length(tn) - tn->empty_children +
 *    tn->full_children) >= inflate_threshold * new_child_length
 *
 * expand new_child_length:
 * 100 * (tnode_child_length(tn) - tn->empty_children +
 *    tn->full_children) >=
 *      inflate_threshold * tnode_child_length(tn) * 2
 *
 * shorten again:
 * 50 * (tn->full_children + tnode_child_length(tn) -
 *    tn->empty_children) >= inflate_threshold *
 *    tnode_child_length(tn)
 *
 */
static bool should_inflate(const struct tnode *tp, const struct tnode *tn)
{
	unsigned long used = tnode_child_length(tn);
	unsigned long threshold = used;

	/* Keep root node larger */
	threshold *= tp ? inflate_threshold : inflate_threshold_root;
	used -= tn->empty_children;
	used += tn->full_children;

	/* if bits == KEYLENGTH then pos = 0, and will fail below */

	return (used > 1) && tn->pos && ((50 * used) >= threshold);
}

static bool should_halve(const struct tnode *tp, const struct tnode *tn)
{
	unsigned long used = tnode_child_length(tn);
	unsigned long threshold = used;

	/* Keep root node larger */
	threshold *= tp ? halve_threshold : halve_threshold_root;
	used -= tn->empty_children;

	/* if bits == KEYLENGTH then used = 100% on wrap, and will fail below */

	return (used > 1) && (tn->bits > 1) && ((100 * used) < threshold);
}

static bool should_collapse(const struct tnode *tn)
{
	unsigned long used = tnode_child_length(tn);

	used -= tn->empty_children;

	/* account for bits == KEYLENGTH case */
	if ((tn->bits == KEYLENGTH) && tn->full_children)
		used -= KEY_MAX;

	/* One child or none, time to drop us from the trie */
	return used < 2;
}

#define MAX_WORK 10
static void resize(struct trie *t, struct tnode *tn)
{
	struct tnode *tp = node_parent(tn);
	struct tnode __rcu **cptr;
	int max_work = MAX_WORK;

	pr_debug("In tnode_resize %p inflate_threshold=%d threshold=%d\n",
		 tn, inflate_threshold, halve_threshold);

	/* track the tnode via the pointer from the parent instead of
	 * doing it ourselves.  This way we can let RCU fully do its
	 * thing without us interfering
	 */
	cptr = tp ? &tp->tnode[get_index(tn->key, tp)] : &t->trie;
	BUG_ON(tn != rtnl_dereference(*cptr));

	/* Double as long as the resulting node has a number of
	 * nonempty nodes that are above the threshold.
	 */
	while (should_inflate(tp, tn) && max_work) {
		if (inflate(t, tn)) {
#ifdef CONFIG_IP_FIB_TRIE_STATS
			this_cpu_inc(t->stats->resize_node_skipped);
#endif
			break;
		}

		max_work--;
		tn = rtnl_dereference(*cptr);
	}

	/* Return if at least one inflate is run */
	if (max_work != MAX_WORK)
		return;

	/* Halve as long as the number of empty children in this
	 * node is above threshold.
	 */
	while (should_halve(tp, tn) && max_work) {
		if (halve(t, tn)) {
#ifdef CONFIG_IP_FIB_TRIE_STATS
			this_cpu_inc(t->stats->resize_node_skipped);
#endif
			break;
		}

		max_work--;
		tn = rtnl_dereference(*cptr);
	}

	/* Only one child remains */
	if (should_collapse(tn)) {
		collapse(t, tn);
		return;
	}

	/* Return if at least one deflate was run */
	if (max_work != MAX_WORK)
		return;

	/* push the suffix length to the parent node */
	if (tn->slen > tn->pos) {
		unsigned char slen = update_suffix(tn);

		if (tp && (slen > tp->slen))
			tp->slen = slen;
	}
}

static void leaf_pull_suffix(struct tnode *tp, struct tnode *l)
{
	while (tp && (tp->slen > tp->pos) && (tp->slen > l->slen)) {
		if (update_suffix(tp) > l->slen)
			break;
		tp = node_parent(tp);
	}
}

static void leaf_push_suffix(struct tnode *tn, struct tnode *l)
{
	/* if this is a new leaf then tn will be NULL and we can sort
	 * out parent suffix lengths as a part of trie_rebalance
	 */
	while (tn && (tn->slen < l->slen)) {
		tn->slen = l->slen;
		tn = node_parent(tn);
	}
}

/* rcu_read_lock needs to be hold by caller from readside */
static struct tnode *fib_find_node(struct trie *t, struct tnode **tn, u32 key)
{
	struct tnode *pn = NULL, *n = rcu_dereference_rtnl(t->trie);

	while (n) {
		unsigned long index = get_index(key, n);

		/* This bit of code is a bit tricky but it combines multiple
		 * checks into a single check.  The prefix consists of the
		 * prefix plus zeros for the bits in the cindex. The index
		 * is the difference between the key and this value.  From
		 * this we can actually derive several pieces of data.
		 *   if (index >= (1ul << bits))
		 *     we have a mismatch in skip bits and failed
		 *   else
		 *     we know the value is cindex
		 *
		 * This check is safe even if bits == KEYLENGTH due to the
		 * fact that we can only allocate a node with 32 bits if a
		 * long is greater than 32 bits.
		 */
		if (index >= (1ul << n->bits)) {
			n = NULL;
			break;
		}

		/* we have found a leaf. Prefixes have already been compared */
		if (IS_LEAF(n))
			break;

		pn = n;
		n = tnode_get_child_rcu(n, index);
	}

	*tn = pn;

	return n;
}

/* Return the first fib alias matching TOS with
 * priority less than or equal to PRIO.
 */
static struct fib_alias *fib_find_alias(struct hlist_head *fah, u8 slen,
					u8 tos, u32 prio)
{
	struct fib_alias *fa;

	if (!fah)
		return NULL;

	hlist_for_each_entry(fa, fah, fa_list) {
		if (fa->fa_slen < slen)
			continue;
		if (fa->fa_slen != slen)
			break;
		if (fa->fa_tos > tos)
			continue;
		if (fa->fa_info->fib_priority >= prio || fa->fa_tos < tos)
			return fa;
	}

	return NULL;
}

static void trie_rebalance(struct trie *t, struct tnode *tn)
{
	struct tnode *tp;

	while (tn) {
		tp = node_parent(tn);
		resize(t, tn);
		tn = tp;
	}
}

/* only used from updater-side */
static int fib_insert_node(struct trie *t, struct tnode *tp,
			   struct fib_alias *new, t_key key)
{
	struct tnode *n, *l;

	l = leaf_new(key, new);
	if (!l)
		return -ENOMEM;

	/* retrieve child from parent node */
	if (tp)
		n = tnode_get_child(tp, get_index(key, tp));
	else
		n = rcu_dereference_rtnl(t->trie);

	/* Case 2: n is a LEAF or a TNODE and the key doesn't match.
	 *
	 *  Add a new tnode here
	 *  first tnode need some special handling
	 *  leaves us in position for handling as case 3
	 */
	if (n) {
		struct tnode *tn;

		tn = tnode_new(key, __fls(key ^ n->key), 1);
		if (!tn) {
			node_free(l);
			return -ENOMEM;
		}

		/* initialize routes out of node */
		NODE_INIT_PARENT(tn, tp);
		put_child(tn, get_index(key, tn) ^ 1, n);

		/* start adding routes into the node */
		put_child_root(tp, t, key, tn);
		node_set_parent(n, tn);

		/* parent now has a NULL spot where the leaf can go */
		tp = tn;
	}

	/* Case 3: n is NULL, and will just insert a new leaf */
	NODE_INIT_PARENT(l, tp);
	put_child_root(tp, t, key, l);
	trie_rebalance(t, tp);

	return 0;
}

static int fib_insert_alias(struct trie *t, struct tnode *tp,
			    struct tnode *l, struct fib_alias *new,
			    struct fib_alias *fa, t_key key)
{
	if (!l)
		return fib_insert_node(t, tp, new, key);

	if (fa) {
		hlist_add_before_rcu(&new->fa_list, &fa->fa_list);
	} else {
		struct fib_alias *last;

		hlist_for_each_entry(last, &l->leaf, fa_list) {
			if (new->fa_slen < last->fa_slen)
				break;
			fa = last;
		}

		if (fa)
			hlist_add_behind_rcu(&new->fa_list, &fa->fa_list);
		else
			hlist_add_head_rcu(&new->fa_list, &l->leaf);
	}

	/* if we added to the tail node then we need to update slen */
	if (l->slen < new->fa_slen) {
		l->slen = new->fa_slen;
		leaf_push_suffix(tp, l);
	}

	return 0;
}

/* Caller must hold RTNL. */
int fib_table_insert(struct fib_table *tb, struct fib_config *cfg)
{
	struct trie *t = (struct trie *)tb->tb_data;
	struct fib_alias *fa, *new_fa;
	struct tnode *l, *tp;
	struct fib_info *fi;
	u8 plen = cfg->fc_dst_len;
	u8 slen = KEYLENGTH - plen;
	u8 tos = cfg->fc_tos;
	u32 key;
	int err;

	if (plen > KEYLENGTH)
		return -EINVAL;

	key = ntohl(cfg->fc_dst);

	pr_debug("Insert table=%u %08x/%d\n", tb->tb_id, key, plen);

	if ((plen < KEYLENGTH) && (key << plen))
		return -EINVAL;

	fi = fib_create_info(cfg);
	if (IS_ERR(fi)) {
		err = PTR_ERR(fi);
		goto err;
	}

	l = fib_find_node(t, &tp, key);
	fa = l ? fib_find_alias(&l->leaf, slen, tos, fi->fib_priority) : NULL;

	/* Now fa, if non-NULL, points to the first fib alias
	 * with the same keys [prefix,tos,priority], if such key already
	 * exists or to the node before which we will insert new one.
	 *
	 * If fa is NULL, we will need to allocate a new one and
	 * insert to the tail of the section matching the suffix length
	 * of the new alias.
	 */

	if (fa && fa->fa_tos == tos &&
	    fa->fa_info->fib_priority == fi->fib_priority) {
		struct fib_alias *fa_first, *fa_match;

		err = -EEXIST;
		if (cfg->fc_nlflags & NLM_F_EXCL)
			goto out;

		/* We have 2 goals:
		 * 1. Find exact match for type, scope, fib_info to avoid
		 * duplicate routes
		 * 2. Find next 'fa' (or head), NLM_F_APPEND inserts before it
		 */
		fa_match = NULL;
		fa_first = fa;
		hlist_for_each_entry_from(fa, fa_list) {
			if ((fa->fa_slen != slen) || (fa->fa_tos != tos))
				break;
			if (fa->fa_info->fib_priority != fi->fib_priority)
				break;
			if (fa->fa_type == cfg->fc_type &&
			    fa->fa_info == fi) {
				fa_match = fa;
				break;
			}
		}

		if (cfg->fc_nlflags & NLM_F_REPLACE) {
			struct fib_info *fi_drop;
			u8 state;

			fa = fa_first;
			if (fa_match) {
				if (fa == fa_match)
					err = 0;
				goto out;
			}
			err = -ENOBUFS;
			new_fa = kmem_cache_alloc(fn_alias_kmem, GFP_KERNEL);
			if (new_fa == NULL)
				goto out;

			fi_drop = fa->fa_info;
			new_fa->fa_tos = fa->fa_tos;
			new_fa->fa_info = fi;
			new_fa->fa_type = cfg->fc_type;
			state = fa->fa_state;
			new_fa->fa_state = state & ~FA_S_ACCESSED;
			new_fa->fa_slen = fa->fa_slen;

			hlist_replace_rcu(&fa->fa_list, &new_fa->fa_list);
			alias_free_mem_rcu(fa);

			fib_release_info(fi_drop);
			if (state & FA_S_ACCESSED)
				rt_cache_flush(cfg->fc_nlinfo.nl_net);
			rtmsg_fib(RTM_NEWROUTE, htonl(key), new_fa, plen,
				tb->tb_id, &cfg->fc_nlinfo, NLM_F_REPLACE);

			goto succeeded;
		}
		/* Error if we find a perfect match which
		 * uses the same scope, type, and nexthop
		 * information.
		 */
		if (fa_match)
			goto out;

		if (!(cfg->fc_nlflags & NLM_F_APPEND))
			fa = fa_first;
	}
	err = -ENOENT;
	if (!(cfg->fc_nlflags & NLM_F_CREATE))
		goto out;

	err = -ENOBUFS;
	new_fa = kmem_cache_alloc(fn_alias_kmem, GFP_KERNEL);
	if (new_fa == NULL)
		goto out;

	new_fa->fa_info = fi;
	new_fa->fa_tos = tos;
	new_fa->fa_type = cfg->fc_type;
	new_fa->fa_state = 0;
	new_fa->fa_slen = slen;

	/* Insert new entry to the list. */
	err = fib_insert_alias(t, tp, l, new_fa, fa, key);
	if (err)
		goto out_free_new_fa;

	if (!plen)
		tb->tb_num_default++;

	rt_cache_flush(cfg->fc_nlinfo.nl_net);
	rtmsg_fib(RTM_NEWROUTE, htonl(key), new_fa, plen, tb->tb_id,
		  &cfg->fc_nlinfo, 0);
succeeded:
	return 0;

out_free_new_fa:
	kmem_cache_free(fn_alias_kmem, new_fa);
out:
	fib_release_info(fi);
err:
	return err;
}

static inline t_key prefix_mismatch(t_key key, struct tnode *n)
{
	t_key prefix = n->key;

	return (key ^ prefix) & (prefix | -prefix);
}

/* should be called with rcu_read_lock */
int fib_table_lookup(struct fib_table *tb, const struct flowi4 *flp,
		     struct fib_result *res, int fib_flags)
{
	struct trie *t = (struct trie *)tb->tb_data;
#ifdef CONFIG_IP_FIB_TRIE_STATS
	struct trie_use_stats __percpu *stats = t->stats;
#endif
	const t_key key = ntohl(flp->daddr);
	struct tnode *n, *pn;
	struct fib_alias *fa;
	t_key cindex;

	n = rcu_dereference(t->trie);
	if (!n)
		return -EAGAIN;

#ifdef CONFIG_IP_FIB_TRIE_STATS
	this_cpu_inc(stats->gets);
#endif

	pn = n;
	cindex = 0;

	/* Step 1: Travel to the longest prefix match in the trie */
	for (;;) {
		unsigned long index = get_index(key, n);

		/* This bit of code is a bit tricky but it combines multiple
		 * checks into a single check.  The prefix consists of the
		 * prefix plus zeros for the "bits" in the prefix. The index
		 * is the difference between the key and this value.  From
		 * this we can actually derive several pieces of data.
		 *   if (index & (~0ul << bits))
		 *     we have a mismatch in skip bits and failed
		 *   else
		 *     we know the value is cindex
		 */
		if (index & (~0ul << n->bits))
			break;

		/* we have found a leaf. Prefixes have already been compared */
		if (IS_LEAF(n))
			goto found;

		/* only record pn and cindex if we are going to be chopping
		 * bits later.  Otherwise we are just wasting cycles.
		 */
		if (n->slen > n->pos) {
			pn = n;
			cindex = index;
		}

		n = tnode_get_child_rcu(n, index);
		if (unlikely(!n))
			goto backtrace;
	}

	/* Step 2: Sort out leaves and begin backtracing for longest prefix */
	for (;;) {
		/* record the pointer where our next node pointer is stored */
		struct tnode __rcu **cptr = n->tnode;

		/* This test verifies that none of the bits that differ
		 * between the key and the prefix exist in the region of
		 * the lsb and higher in the prefix.
		 */
		if (unlikely(prefix_mismatch(key, n)) || (n->slen == n->pos))
			goto backtrace;

		/* exit out and process leaf */
		if (unlikely(IS_LEAF(n)))
			break;

		/* Don't bother recording parent info.  Since we are in
		 * prefix match mode we will have to come back to wherever
		 * we started this traversal anyway
		 */

		while ((n = rcu_dereference(*cptr)) == NULL) {
backtrace:
#ifdef CONFIG_IP_FIB_TRIE_STATS
			if (!n)
				this_cpu_inc(stats->null_node_hit);
#endif
			/* If we are at cindex 0 there are no more bits for
			 * us to strip at this level so we must ascend back
			 * up one level to see if there are any more bits to
			 * be stripped there.
			 */
			while (!cindex) {
				t_key pkey = pn->key;

				pn = node_parent_rcu(pn);
				if (unlikely(!pn))
					return -EAGAIN;
#ifdef CONFIG_IP_FIB_TRIE_STATS
				this_cpu_inc(stats->backtrack);
#endif
				/* Get Child's index */
				cindex = get_index(pkey, pn);
			}

			/* strip the least significant bit from the cindex */
			cindex &= cindex - 1;

			/* grab pointer for next child node */
			cptr = &pn->tnode[cindex];
		}
	}

found:
	/* Step 3: Process the leaf, if that fails fall back to backtracing */
	hlist_for_each_entry_rcu(fa, &n->leaf, fa_list) {
		struct fib_info *fi = fa->fa_info;
		int nhsel, err;

		if (((key ^ n->key) >= (1ul << fa->fa_slen)) &&
		    ((BITS_PER_LONG > KEYLENGTH) || (fa->fa_slen != KEYLENGTH)))
				continue;
		if (fa->fa_tos && fa->fa_tos != flp->flowi4_tos)
			continue;
		if (fi->fib_dead)
			continue;
		if (fa->fa_info->fib_scope < flp->flowi4_scope)
			continue;
		fib_alias_accessed(fa);
		err = fib_props[fa->fa_type].error;
		if (unlikely(err < 0)) {
#ifdef CONFIG_IP_FIB_TRIE_STATS
			this_cpu_inc(stats->semantic_match_passed);
#endif
			return err;
		}
		if (fi->fib_flags & RTNH_F_DEAD)
			continue;
		for (nhsel = 0; nhsel < fi->fib_nhs; nhsel++) {
			const struct fib_nh *nh = &fi->fib_nh[nhsel];

			if (nh->nh_flags & RTNH_F_DEAD)
				continue;
			if (flp->flowi4_oif && flp->flowi4_oif != nh->nh_oif)
				continue;

			if (!(fib_flags & FIB_LOOKUP_NOREF))
				atomic_inc(&fi->fib_clntref);

			res->prefixlen = KEYLENGTH - fa->fa_slen;
			res->nh_sel = nhsel;
			res->type = fa->fa_type;
			res->scope = fi->fib_scope;
			res->fi = fi;
			res->table = tb;
			res->fa_head = &n->leaf;
#ifdef CONFIG_IP_FIB_TRIE_STATS
			this_cpu_inc(stats->semantic_match_passed);
#endif
			return err;
		}
	}
#ifdef CONFIG_IP_FIB_TRIE_STATS
	this_cpu_inc(stats->semantic_match_miss);
#endif
	goto backtrace;
}
EXPORT_SYMBOL_GPL(fib_table_lookup);

static void fib_remove_alias(struct trie *t, struct tnode *tp,
			     struct tnode *l, struct fib_alias *old)
{
	/* record the location of the previous list_info entry */
	struct hlist_node **pprev = old->fa_list.pprev;
	struct fib_alias *fa = hlist_entry(pprev, typeof(*fa), fa_list.next);

	/* remove the fib_alias from the list */
	hlist_del_rcu(&old->fa_list);

	/* if we emptied the list this leaf will be freed and we can sort
	 * out parent suffix lengths as a part of trie_rebalance
	 */
	if (hlist_empty(&l->leaf)) {
		put_child_root(tp, t, l->key, NULL);
		node_free(l);
		trie_rebalance(t, tp);
		return;
	}

	/* only access fa if it is pointing at the last valid hlist_node */
	if (*pprev)
		return;

	/* update the trie with the latest suffix length */
	l->slen = fa->fa_slen;
	leaf_pull_suffix(tp, l);
}

/* Caller must hold RTNL. */
int fib_table_delete(struct fib_table *tb, struct fib_config *cfg)
{
	struct trie *t = (struct trie *) tb->tb_data;
	struct fib_alias *fa, *fa_to_delete;
	struct tnode *l, *tp;
	u8 plen = cfg->fc_dst_len;
	u8 slen = KEYLENGTH - plen;
	u8 tos = cfg->fc_tos;
	u32 key;

	if (plen > KEYLENGTH)
		return -EINVAL;

	key = ntohl(cfg->fc_dst);

	if ((plen < KEYLENGTH) && (key << plen))
		return -EINVAL;

	l = fib_find_node(t, &tp, key);
	if (!l)
		return -ESRCH;

	fa = fib_find_alias(&l->leaf, slen, tos, 0);
	if (!fa)
		return -ESRCH;

	pr_debug("Deleting %08x/%d tos=%d t=%p\n", key, plen, tos, t);

	fa_to_delete = NULL;
	hlist_for_each_entry_from(fa, fa_list) {
		struct fib_info *fi = fa->fa_info;

		if ((fa->fa_slen != slen) || (fa->fa_tos != tos))
			break;

		if ((!cfg->fc_type || fa->fa_type == cfg->fc_type) &&
		    (cfg->fc_scope == RT_SCOPE_NOWHERE ||
		     fa->fa_info->fib_scope == cfg->fc_scope) &&
		    (!cfg->fc_prefsrc ||
		     fi->fib_prefsrc == cfg->fc_prefsrc) &&
		    (!cfg->fc_protocol ||
		     fi->fib_protocol == cfg->fc_protocol) &&
		    fib_nh_match(cfg, fi) == 0) {
			fa_to_delete = fa;
			break;
		}
	}

	if (!fa_to_delete)
		return -ESRCH;

	rtmsg_fib(RTM_DELROUTE, htonl(key), fa_to_delete, plen, tb->tb_id,
		  &cfg->fc_nlinfo, 0);

	if (!plen)
		tb->tb_num_default--;

	fib_remove_alias(t, tp, l, fa_to_delete);

	if (fa_to_delete->fa_state & FA_S_ACCESSED)
		rt_cache_flush(cfg->fc_nlinfo.nl_net);

	fib_release_info(fa_to_delete->fa_info);
	alias_free_mem_rcu(fa_to_delete);
	return 0;
}

/* Scan for the next leaf starting at the provided key value */
static struct tnode *leaf_walk_rcu(struct tnode **tn, t_key key)
{
	struct tnode *pn, *n = *tn;
	unsigned long cindex;

	/* record parent node for backtracing */
	pn = n;
	cindex = n ? get_index(key, n) : 0;

	/* this loop is meant to try and find the key in the trie */
	while (n) {
		unsigned long idx = get_index(key, n);

		/* guarantee forward progress on the keys */
		if (IS_LEAF(n) && (n->key >= key))
			goto found;
		if (idx >= (1ul << n->bits))
			break;

		/* record parent and next child index */
		pn = n;
		cindex = idx;

		/* descend into the next child */
		n = tnode_get_child_rcu(pn, cindex++);
	}

	/* this loop will search for the next leaf with a greater key */
	while (pn) {
		/* if we exhausted the parent node we will need to climb */
		if (cindex >= (1ul << pn->bits)) {
			t_key pkey = pn->key;

			pn = node_parent_rcu(pn);
			if (!pn)
				break;

			cindex = get_index(pkey, pn) + 1;
			continue;
		}

		/* grab the next available node */
		n = tnode_get_child_rcu(pn, cindex++);
		if (!n)
			continue;

		/* no need to compare keys since we bumped the index */
		if (IS_LEAF(n))
			goto found;

		/* Rescan start scanning in new node */
		pn = n;
		cindex = 0;
	}

	*tn = pn;
	return NULL; /* Root of trie */
found:
	/* if we are at the limit for keys just return NULL for the tnode */
	*tn = (n->key == KEY_MAX) ? NULL : pn;
	return n;
}

/* Caller must hold RTNL. */
int fib_table_flush(struct fib_table *tb)
{
	struct trie *t = (struct trie *)tb->tb_data;
	struct hlist_node *tmp;
	struct fib_alias *fa;
	struct tnode *n, *pn;
	unsigned long cindex;
	unsigned char slen;
	int found = 0;

	n = rcu_dereference(t->trie);
	if (!n)
		goto flush_complete;

	pn = NULL;
	cindex = 0;

	while (IS_TNODE(n)) {
		/* record pn and cindex for leaf walking */
		pn = n;
		cindex = 1ul << n->bits;
backtrace:
		/* walk trie in reverse order */
		do {
			while (!(cindex--)) {
				t_key pkey = pn->key;

				n = pn;
				pn = node_parent(n);

				/* resize completed node */
				resize(t, n);

				/* if we got the root we are done */
				if (!pn)
					goto flush_complete;

				cindex = get_index(pkey, pn);
			}

			/* grab the next available node */
			n = tnode_get_child(pn, cindex);
		} while (!n);
	}

	/* track slen in case any prefixes survive */
	slen = 0;

	hlist_for_each_entry_safe(fa, tmp, &n->leaf, fa_list) {
		struct fib_info *fi = fa->fa_info;

		if (fi && (fi->fib_flags & RTNH_F_DEAD)) {
			hlist_del_rcu(&fa->fa_list);
			fib_release_info(fa->fa_info);
			alias_free_mem_rcu(fa);
			found++;

			continue;
		}

		slen = fa->fa_slen;
	}

	/* update leaf slen */
	n->slen = slen;

	if (hlist_empty(&n->leaf)) {
		put_child_root(pn, t, n->key, NULL);
		node_free(n);
	} else {
		leaf_pull_suffix(pn, n);
	}

	/* if trie is leaf only loop is completed */
	if (pn)
		goto backtrace;
flush_complete:
	pr_debug("trie_flush found=%d\n", found);
	return found;
}

void fib_free_table(struct fib_table *tb)
{
#ifdef CONFIG_IP_FIB_TRIE_STATS
	struct trie *t = (struct trie *)tb->tb_data;

	free_percpu(t->stats);
#endif /* CONFIG_IP_FIB_TRIE_STATS */
	kfree(tb);
}

static int fn_trie_dump_leaf(struct tnode *l, struct fib_table *tb,
			     struct sk_buff *skb, struct netlink_callback *cb)
{
	__be32 xkey = htonl(l->key);
	struct fib_alias *fa;
	int i, s_i;

	s_i = cb->args[4];
	i = 0;

	/* rcu_read_lock is hold by caller */
	hlist_for_each_entry_rcu(fa, &l->leaf, fa_list) {
		if (i < s_i) {
			i++;
			continue;
		}

		if (fib_dump_info(skb, NETLINK_CB(cb->skb).portid,
				  cb->nlh->nlmsg_seq,
				  RTM_NEWROUTE,
				  tb->tb_id,
				  fa->fa_type,
				  xkey,
				  KEYLENGTH - fa->fa_slen,
				  fa->fa_tos,
				  fa->fa_info, NLM_F_MULTI) < 0) {
			cb->args[4] = i;
			return -1;
		}
		i++;
	}

	cb->args[4] = i;
	return skb->len;
}

int fib_table_dump(struct fib_table *tb, struct sk_buff *skb,
		   struct netlink_callback *cb)
{
	struct trie *t = (struct trie *)tb->tb_data;
	struct tnode *l, *tp;
	/* Dump starting at last key.
	 * Note: 0.0.0.0/0 (ie default) is first key.
	 */
	int count = cb->args[2];
	t_key key = cb->args[3];

	rcu_read_lock();

	tp = rcu_dereference_rtnl(t->trie);

	while ((l = leaf_walk_rcu(&tp, key)) != NULL) {
		if (fn_trie_dump_leaf(l, tb, skb, cb) < 0) {
			cb->args[3] = key;
			cb->args[2] = count;
			rcu_read_unlock();
			return -1;
		}

		++count;
		key = l->key + 1;

		memset(&cb->args[4], 0,
		       sizeof(cb->args) - 4*sizeof(cb->args[0]));

		/* stop loop if key wrapped back to 0 */
		if (key < l->key)
			break;
	}

	rcu_read_unlock();

	cb->args[3] = key;
	cb->args[2] = count;

	return skb->len;
}

void __init fib_trie_init(void)
{
	fn_alias_kmem = kmem_cache_create("ip_fib_alias",
					  sizeof(struct fib_alias),
					  0, SLAB_PANIC, NULL);

	trie_leaf_kmem = kmem_cache_create("ip_fib_trie",
					   LEAF_SIZE,
					   0, SLAB_PANIC, NULL);
}


struct fib_table *fib_trie_table(u32 id)
{
	struct fib_table *tb;
	struct trie *t;

	tb = kmalloc(sizeof(struct fib_table) + sizeof(struct trie),
		     GFP_KERNEL);
	if (tb == NULL)
		return NULL;

	tb->tb_id = id;
	tb->tb_default = -1;
	tb->tb_num_default = 0;

	t = (struct trie *) tb->tb_data;
	RCU_INIT_POINTER(t->trie, NULL);
#ifdef CONFIG_IP_FIB_TRIE_STATS
	t->stats = alloc_percpu(struct trie_use_stats);
	if (!t->stats) {
		kfree(tb);
		tb = NULL;
	}
#endif

	return tb;
}

#ifdef CONFIG_PROC_FS
/* Depth first Trie walk iterator */
struct fib_trie_iter {
	struct seq_net_private p;
	struct fib_table *tb;
	struct tnode *tnode;
	unsigned int index;
	unsigned int depth;
};

static struct tnode *fib_trie_get_next(struct fib_trie_iter *iter)
{
	unsigned long cindex = iter->index;
	struct tnode *tn = iter->tnode;
	struct tnode *p;

	/* A single entry routing table */
	if (!tn)
		return NULL;

	pr_debug("get_next iter={node=%p index=%d depth=%d}\n",
		 iter->tnode, iter->index, iter->depth);
rescan:
	while (cindex < tnode_child_length(tn)) {
		struct tnode *n = tnode_get_child_rcu(tn, cindex);

		if (n) {
			if (IS_LEAF(n)) {
				iter->tnode = tn;
				iter->index = cindex + 1;
			} else {
				/* push down one level */
				iter->tnode = n;
				iter->index = 0;
				++iter->depth;
			}
			return n;
		}

		++cindex;
	}

	/* Current node exhausted, pop back up */
	p = node_parent_rcu(tn);
	if (p) {
		cindex = get_index(tn->key, p) + 1;
		tn = p;
		--iter->depth;
		goto rescan;
	}

	/* got root? */
	return NULL;
}

static struct tnode *fib_trie_get_first(struct fib_trie_iter *iter,
				       struct trie *t)
{
	struct tnode *n;

	if (!t)
		return NULL;

	n = rcu_dereference(t->trie);
	if (!n)
		return NULL;

	if (IS_TNODE(n)) {
		iter->tnode = n;
		iter->index = 0;
		iter->depth = 1;
	} else {
		iter->tnode = NULL;
		iter->index = 0;
		iter->depth = 0;
	}

	return n;
}

static void trie_collect_stats(struct trie *t, struct trie_stat *s)
{
	struct tnode *n;
	struct fib_trie_iter iter;

	memset(s, 0, sizeof(*s));

	rcu_read_lock();
	for (n = fib_trie_get_first(&iter, t); n; n = fib_trie_get_next(&iter)) {
		if (IS_LEAF(n)) {
			struct fib_alias *fa;

			s->leaves++;
			s->totdepth += iter.depth;
			if (iter.depth > s->maxdepth)
				s->maxdepth = iter.depth;

			hlist_for_each_entry_rcu(fa, &n->leaf, fa_list)
				++s->prefixes;
		} else {
			s->tnodes++;
			if (n->bits < MAX_STAT_DEPTH)
				s->nodesizes[n->bits]++;
			s->nullpointers += n->empty_children;
		}
	}
	rcu_read_unlock();
}

/*
 *	This outputs /proc/net/fib_triestats
 */
static void trie_show_stats(struct seq_file *seq, struct trie_stat *stat)
{
	unsigned int i, max, pointers, bytes, avdepth;

	if (stat->leaves)
		avdepth = stat->totdepth*100 / stat->leaves;
	else
		avdepth = 0;

	seq_printf(seq, "\tAver depth:     %u.%02d\n",
		   avdepth / 100, avdepth % 100);
	seq_printf(seq, "\tMax depth:      %u\n", stat->maxdepth);

	seq_printf(seq, "\tLeaves:         %u\n", stat->leaves);
	bytes = LEAF_SIZE * stat->leaves;

	seq_printf(seq, "\tPrefixes:       %u\n", stat->prefixes);
	bytes += sizeof(struct fib_alias) * stat->prefixes;

	seq_printf(seq, "\tInternal nodes: %u\n\t", stat->tnodes);
	bytes += TNODE_SIZE(0) * stat->tnodes;

	max = MAX_STAT_DEPTH;
	while (max > 0 && stat->nodesizes[max-1] == 0)
		max--;

	pointers = 0;
	for (i = 1; i < max; i++)
		if (stat->nodesizes[i] != 0) {
			seq_printf(seq, "  %u: %u",  i, stat->nodesizes[i]);
			pointers += (1<<i) * stat->nodesizes[i];
		}
	seq_putc(seq, '\n');
	seq_printf(seq, "\tPointers: %u\n", pointers);

	bytes += sizeof(struct tnode *) * pointers;
	seq_printf(seq, "Null ptrs: %u\n", stat->nullpointers);
	seq_printf(seq, "Total size: %u  kB\n", (bytes + 1023) / 1024);
}

#ifdef CONFIG_IP_FIB_TRIE_STATS
static void trie_show_usage(struct seq_file *seq,
			    const struct trie_use_stats __percpu *stats)
{
	struct trie_use_stats s = { 0 };
	int cpu;

	/* loop through all of the CPUs and gather up the stats */
	for_each_possible_cpu(cpu) {
		const struct trie_use_stats *pcpu = per_cpu_ptr(stats, cpu);

		s.gets += pcpu->gets;
		s.backtrack += pcpu->backtrack;
		s.semantic_match_passed += pcpu->semantic_match_passed;
		s.semantic_match_miss += pcpu->semantic_match_miss;
		s.null_node_hit += pcpu->null_node_hit;
		s.resize_node_skipped += pcpu->resize_node_skipped;
	}

	seq_printf(seq, "\nCounters:\n---------\n");
	seq_printf(seq, "gets = %u\n", s.gets);
	seq_printf(seq, "backtracks = %u\n", s.backtrack);
	seq_printf(seq, "semantic match passed = %u\n",
		   s.semantic_match_passed);
	seq_printf(seq, "semantic match miss = %u\n", s.semantic_match_miss);
	seq_printf(seq, "null node hit= %u\n", s.null_node_hit);
	seq_printf(seq, "skipped node resize = %u\n\n", s.resize_node_skipped);
}
#endif /*  CONFIG_IP_FIB_TRIE_STATS */

static void fib_table_print(struct seq_file *seq, struct fib_table *tb)
{
	if (tb->tb_id == RT_TABLE_LOCAL)
		seq_puts(seq, "Local:\n");
	else if (tb->tb_id == RT_TABLE_MAIN)
		seq_puts(seq, "Main:\n");
	else
		seq_printf(seq, "Id %d:\n", tb->tb_id);
}


static int fib_triestat_seq_show(struct seq_file *seq, void *v)
{
	struct net *net = (struct net *)seq->private;
	unsigned int h;

	seq_printf(seq,
		   "Basic info: size of leaf:"
		   " %Zd bytes, size of tnode: %Zd bytes.\n",
		   LEAF_SIZE, TNODE_SIZE(0));

	for (h = 0; h < FIB_TABLE_HASHSZ; h++) {
		struct hlist_head *head = &net->ipv4.fib_table_hash[h];
		struct fib_table *tb;

		hlist_for_each_entry_rcu(tb, head, tb_hlist) {
			struct trie *t = (struct trie *) tb->tb_data;
			struct trie_stat stat;

			if (!t)
				continue;

			fib_table_print(seq, tb);

			trie_collect_stats(t, &stat);
			trie_show_stats(seq, &stat);
#ifdef CONFIG_IP_FIB_TRIE_STATS
			trie_show_usage(seq, t->stats);
#endif
		}
	}

	return 0;
}

static int fib_triestat_seq_open(struct inode *inode, struct file *file)
{
	return single_open_net(inode, file, fib_triestat_seq_show);
}

static const struct file_operations fib_triestat_fops = {
	.owner	= THIS_MODULE,
	.open	= fib_triestat_seq_open,
	.read	= seq_read,
	.llseek	= seq_lseek,
	.release = single_release_net,
};

static struct tnode *fib_trie_get_idx(struct seq_file *seq, loff_t pos)
{
	struct fib_trie_iter *iter = seq->private;
	struct net *net = seq_file_net(seq);
	loff_t idx = 0;
	unsigned int h;

	for (h = 0; h < FIB_TABLE_HASHSZ; h++) {
		struct hlist_head *head = &net->ipv4.fib_table_hash[h];
		struct fib_table *tb;

		hlist_for_each_entry_rcu(tb, head, tb_hlist) {
			struct tnode *n;

			for (n = fib_trie_get_first(iter,
						    (struct trie *) tb->tb_data);
			     n; n = fib_trie_get_next(iter))
				if (pos == idx++) {
					iter->tb = tb;
					return n;
				}
		}
	}

	return NULL;
}

static void *fib_trie_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	rcu_read_lock();
	return fib_trie_get_idx(seq, *pos);
}

static void *fib_trie_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct fib_trie_iter *iter = seq->private;
	struct net *net = seq_file_net(seq);
	struct fib_table *tb = iter->tb;
	struct hlist_node *tb_node;
	unsigned int h;
	struct tnode *n;

	++*pos;
	/* next node in same table */
	n = fib_trie_get_next(iter);
	if (n)
		return n;

	/* walk rest of this hash chain */
	h = tb->tb_id & (FIB_TABLE_HASHSZ - 1);
	while ((tb_node = rcu_dereference(hlist_next_rcu(&tb->tb_hlist)))) {
		tb = hlist_entry(tb_node, struct fib_table, tb_hlist);
		n = fib_trie_get_first(iter, (struct trie *) tb->tb_data);
		if (n)
			goto found;
	}

	/* new hash chain */
	while (++h < FIB_TABLE_HASHSZ) {
		struct hlist_head *head = &net->ipv4.fib_table_hash[h];
		hlist_for_each_entry_rcu(tb, head, tb_hlist) {
			n = fib_trie_get_first(iter, (struct trie *) tb->tb_data);
			if (n)
				goto found;
		}
	}
	return NULL;

found:
	iter->tb = tb;
	return n;
}

static void fib_trie_seq_stop(struct seq_file *seq, void *v)
	__releases(RCU)
{
	rcu_read_unlock();
}

static void seq_indent(struct seq_file *seq, int n)
{
	while (n-- > 0)
		seq_puts(seq, "   ");
}

static inline const char *rtn_scope(char *buf, size_t len, enum rt_scope_t s)
{
	switch (s) {
	case RT_SCOPE_UNIVERSE: return "universe";
	case RT_SCOPE_SITE:	return "site";
	case RT_SCOPE_LINK:	return "link";
	case RT_SCOPE_HOST:	return "host";
	case RT_SCOPE_NOWHERE:	return "nowhere";
	default:
		snprintf(buf, len, "scope=%d", s);
		return buf;
	}
}

static const char *const rtn_type_names[__RTN_MAX] = {
	[RTN_UNSPEC] = "UNSPEC",
	[RTN_UNICAST] = "UNICAST",
	[RTN_LOCAL] = "LOCAL",
	[RTN_BROADCAST] = "BROADCAST",
	[RTN_ANYCAST] = "ANYCAST",
	[RTN_MULTICAST] = "MULTICAST",
	[RTN_BLACKHOLE] = "BLACKHOLE",
	[RTN_UNREACHABLE] = "UNREACHABLE",
	[RTN_PROHIBIT] = "PROHIBIT",
	[RTN_THROW] = "THROW",
	[RTN_NAT] = "NAT",
	[RTN_XRESOLVE] = "XRESOLVE",
};

static inline const char *rtn_type(char *buf, size_t len, unsigned int t)
{
	if (t < __RTN_MAX && rtn_type_names[t])
		return rtn_type_names[t];
	snprintf(buf, len, "type %u", t);
	return buf;
}

/* Pretty print the trie */
static int fib_trie_seq_show(struct seq_file *seq, void *v)
{
	const struct fib_trie_iter *iter = seq->private;
	struct tnode *n = v;

	if (!node_parent_rcu(n))
		fib_table_print(seq, iter->tb);

	if (IS_TNODE(n)) {
		__be32 prf = htonl(n->key);

		seq_indent(seq, iter->depth-1);
		seq_printf(seq, "  +-- %pI4/%zu %u %u %u\n",
			   &prf, KEYLENGTH - n->pos - n->bits, n->bits,
			   n->full_children, n->empty_children);
	} else {
		__be32 val = htonl(n->key);
		struct fib_alias *fa;

		seq_indent(seq, iter->depth);
		seq_printf(seq, "  |-- %pI4\n", &val);

		hlist_for_each_entry_rcu(fa, &n->leaf, fa_list) {
			char buf1[32], buf2[32];

			seq_indent(seq, iter->depth + 1);
			seq_printf(seq, "  /%zu %s %s",
				   KEYLENGTH - fa->fa_slen,
				   rtn_scope(buf1, sizeof(buf1),
					     fa->fa_info->fib_scope),
				   rtn_type(buf2, sizeof(buf2),
					    fa->fa_type));
			if (fa->fa_tos)
				seq_printf(seq, " tos=%d", fa->fa_tos);
			seq_putc(seq, '\n');
		}
	}

	return 0;
}

static const struct seq_operations fib_trie_seq_ops = {
	.start  = fib_trie_seq_start,
	.next   = fib_trie_seq_next,
	.stop   = fib_trie_seq_stop,
	.show   = fib_trie_seq_show,
};

static int fib_trie_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &fib_trie_seq_ops,
			    sizeof(struct fib_trie_iter));
}

static const struct file_operations fib_trie_fops = {
	.owner  = THIS_MODULE,
	.open   = fib_trie_seq_open,
	.read   = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_net,
};

struct fib_route_iter {
	struct seq_net_private p;
	struct fib_table *main_tb;
	struct tnode *tnode;
	loff_t	pos;
	t_key	key;
};

static struct tnode *fib_route_get_idx(struct fib_route_iter *iter, loff_t pos)
{
	struct fib_table *tb = iter->main_tb;
	struct tnode *l, **tp = &iter->tnode;
	struct trie *t;
	t_key key;

	/* use cache location of next-to-find key */
	if (iter->pos > 0 && pos >= iter->pos) {
		pos -= iter->pos;
		key = iter->key;
	} else {
		t = (struct trie *)tb->tb_data;
		iter->tnode = rcu_dereference_rtnl(t->trie);
		iter->pos = 0;
		key = 0;
	}

	while ((l = leaf_walk_rcu(tp, key)) != NULL) {
		key = l->key + 1;
		iter->pos++;

		if (pos-- <= 0)
			break;

		l = NULL;

		/* handle unlikely case of a key wrap */
		if (!key)
			break;
	}

	if (l)
		iter->key = key;	/* remember it */
	else
		iter->pos = 0;		/* forget it */

	return l;
}

static void *fib_route_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	struct fib_route_iter *iter = seq->private;
	struct fib_table *tb;
	struct trie *t;

	rcu_read_lock();

	tb = fib_get_table(seq_file_net(seq), RT_TABLE_MAIN);
	if (!tb)
		return NULL;

	iter->main_tb = tb;

	if (*pos != 0)
		return fib_route_get_idx(iter, *pos);

	t = (struct trie *)tb->tb_data;
	iter->tnode = rcu_dereference_rtnl(t->trie);
	iter->pos = 0;
	iter->key = 0;

	return SEQ_START_TOKEN;
}

static void *fib_route_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct fib_route_iter *iter = seq->private;
	struct tnode *l = NULL;
	t_key key = iter->key;

	++*pos;

	/* only allow key of 0 for start of sequence */
	if ((v == SEQ_START_TOKEN) || key)
		l = leaf_walk_rcu(&iter->tnode, key);

	if (l) {
		iter->key = l->key + 1;
		iter->pos++;
	} else {
		iter->pos = 0;
	}

	return l;
}

static void fib_route_seq_stop(struct seq_file *seq, void *v)
	__releases(RCU)
{
	rcu_read_unlock();
}

static unsigned int fib_flag_trans(int type, __be32 mask, const struct fib_info *fi)
{
	unsigned int flags = 0;

	if (type == RTN_UNREACHABLE || type == RTN_PROHIBIT)
		flags = RTF_REJECT;
	if (fi && fi->fib_nh->nh_gw)
		flags |= RTF_GATEWAY;
	if (mask == htonl(0xFFFFFFFF))
		flags |= RTF_HOST;
	flags |= RTF_UP;
	return flags;
}

/*
 *	This outputs /proc/net/route.
 *	The format of the file is not supposed to be changed
 *	and needs to be same as fib_hash output to avoid breaking
 *	legacy utilities
 */
static int fib_route_seq_show(struct seq_file *seq, void *v)
{
	struct fib_alias *fa;
	struct tnode *l = v;
	__be32 prefix;

	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, "%-127s\n", "Iface\tDestination\tGateway "
			   "\tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU"
			   "\tWindow\tIRTT");
		return 0;
	}

	prefix = htonl(l->key);

	hlist_for_each_entry_rcu(fa, &l->leaf, fa_list) {
		const struct fib_info *fi = fa->fa_info;
		__be32 mask = inet_make_mask(KEYLENGTH - fa->fa_slen);
		unsigned int flags = fib_flag_trans(fa->fa_type, mask, fi);

		if ((fa->fa_type == RTN_BROADCAST) ||
		    (fa->fa_type == RTN_MULTICAST))
			continue;

		seq_setwidth(seq, 127);

		if (fi)
			seq_printf(seq,
				   "%s\t%08X\t%08X\t%04X\t%d\t%u\t"
				   "%d\t%08X\t%d\t%u\t%u",
				   fi->fib_dev ? fi->fib_dev->name : "*",
				   prefix,
				   fi->fib_nh->nh_gw, flags, 0, 0,
				   fi->fib_priority,
				   mask,
				   (fi->fib_advmss ?
				    fi->fib_advmss + 40 : 0),
				   fi->fib_window,
				   fi->fib_rtt >> 3);
		else
			seq_printf(seq,
				   "*\t%08X\t%08X\t%04X\t%d\t%u\t"
				   "%d\t%08X\t%d\t%u\t%u",
				   prefix, 0, flags, 0, 0, 0,
				   mask, 0, 0, 0);

		seq_pad(seq, '\n');
	}

	return 0;
}

static const struct seq_operations fib_route_seq_ops = {
	.start  = fib_route_seq_start,
	.next   = fib_route_seq_next,
	.stop   = fib_route_seq_stop,
	.show   = fib_route_seq_show,
};

static int fib_route_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &fib_route_seq_ops,
			    sizeof(struct fib_route_iter));
}

static const struct file_operations fib_route_fops = {
	.owner  = THIS_MODULE,
	.open   = fib_route_seq_open,
	.read   = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_net,
};

int __net_init fib_proc_init(struct net *net)
{
	if (!proc_create("fib_trie", S_IRUGO, net->proc_net, &fib_trie_fops))
		goto out1;

	if (!proc_create("fib_triestat", S_IRUGO, net->proc_net,
			 &fib_triestat_fops))
		goto out2;

	if (!proc_create("route", S_IRUGO, net->proc_net, &fib_route_fops))
		goto out3;

	return 0;

out3:
	remove_proc_entry("fib_triestat", net->proc_net);
out2:
	remove_proc_entry("fib_trie", net->proc_net);
out1:
	return -ENOMEM;
}

void __net_exit fib_proc_exit(struct net *net)
{
	remove_proc_entry("fib_trie", net->proc_net);
	remove_proc_entry("fib_triestat", net->proc_net);
	remove_proc_entry("route", net->proc_net);
}

#endif /* CONFIG_PROC_FS */

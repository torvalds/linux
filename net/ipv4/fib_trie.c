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

#define KEYLENGTH (8*sizeof(t_key))

typedef unsigned int t_key;

#define IS_TNODE(n) ((n)->bits)
#define IS_LEAF(n) (!(n)->bits)

#define get_shift(_kv) (KEYLENGTH - (_kv)->pos - (_kv)->bits)
#define get_index(_key, _kv) (((_key) ^ (_kv)->key) >> get_shift(_kv))

struct tnode {
	t_key key;
	unsigned char bits;		/* 2log(KEYLENGTH) bits needed */
	unsigned char pos;		/* 2log(KEYLENGTH) bits needed */
	struct tnode __rcu *parent;
	struct rcu_head rcu;
	union {
		/* The fields in this struct are valid if bits > 0 (TNODE) */
		struct {
			unsigned int full_children;  /* KEYLENGTH bits needed */
			unsigned int empty_children; /* KEYLENGTH bits needed */
			struct tnode __rcu *child[0];
		};
		/* This list pointer if valid if bits == 0 (LEAF) */
		struct hlist_head list;
	};
};

struct leaf_info {
	struct hlist_node hlist;
	int plen;
	u32 mask_plen; /* ntohl(inet_make_mask(plen)) */
	struct list_head falh;
	struct rcu_head rcu;
};

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

static void tnode_put_child_reorg(struct tnode *tn, int i, struct tnode *n,
				  int wasfull);
static struct tnode *resize(struct trie *t, struct tnode *tn);
static struct tnode *inflate(struct trie *t, struct tnode *tn);
static struct tnode *halve(struct trie *t, struct tnode *tn);
/* tnodes to free after resize(); protected by RTNL */
static struct callback_head *tnode_free_head;
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
static inline int tnode_child_length(const struct tnode *tn)
{
	return (1ul << tn->bits) & ~(1ul);
}

/*
 * caller must hold RTNL
 */
static inline struct tnode *tnode_get_child(const struct tnode *tn, unsigned int i)
{
	BUG_ON(i >= tnode_child_length(tn));

	return rtnl_dereference(tn->child[i]);
}

/*
 * caller must hold RCU read lock or RTNL
 */
static inline struct tnode *tnode_get_child_rcu(const struct tnode *tn, unsigned int i)
{
	BUG_ON(i >= tnode_child_length(tn));

	return rcu_dereference_rtnl(tn->child[i]);
}

static inline t_key mask_pfx(t_key k, unsigned int l)
{
	return (l == 0) ? 0 : k >> (KEYLENGTH-l) << (KEYLENGTH-l);
}

static inline t_key tkey_extract_bits(t_key a, unsigned int offset, unsigned int bits)
{
	if (offset < KEYLENGTH)
		return ((t_key)(a << offset)) >> (KEYLENGTH - bits);
	else
		return 0;
}

static inline int tkey_equals(t_key a, t_key b)
{
	return a == b;
}

static inline int tkey_sub_equals(t_key a, int offset, int bits, t_key b)
{
	if (bits == 0 || offset >= KEYLENGTH)
		return 1;
	bits = bits > KEYLENGTH ? KEYLENGTH : bits;
	return ((a ^ b) << offset) >> (KEYLENGTH - bits) == 0;
}

static inline int tkey_mismatch(t_key a, int offset, t_key b)
{
	t_key diff = a ^ b;
	int i = offset;

	if (!diff)
		return 0;
	while ((diff << i) >> (KEYLENGTH-1) == 0)
		i++;
	return i;
}

/*
  To understand this stuff, an understanding of keys and all their bits is
  necessary. Every node in the trie has a key associated with it, but not
  all of the bits in that key are significant.

  Consider a node 'n' and its parent 'tp'.

  If n is a leaf, every bit in its key is significant. Its presence is
  necessitated by path compression, since during a tree traversal (when
  searching for a leaf - unless we are doing an insertion) we will completely
  ignore all skipped bits we encounter. Thus we need to verify, at the end of
  a potentially successful search, that we have indeed been walking the
  correct key path.

  Note that we can never "miss" the correct key in the tree if present by
  following the wrong path. Path compression ensures that segments of the key
  that are the same for all keys with a given prefix are skipped, but the
  skipped part *is* identical for each node in the subtrie below the skipped
  bit! trie_insert() in this implementation takes care of that - note the
  call to tkey_sub_equals() in trie_insert().

  if n is an internal node - a 'tnode' here, the various parts of its key
  have many different meanings.

  Example:
  _________________________________________________________________
  | i | i | i | i | i | i | i | N | N | N | S | S | S | S | S | C |
  -----------------------------------------------------------------
    0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15

  _________________________________________________________________
  | C | C | C | u | u | u | u | u | u | u | u | u | u | u | u | u |
  -----------------------------------------------------------------
   16  17  18  19  20  21  22  23  24  25  26  27  28  29  30  31

  tp->pos = 7
  tp->bits = 3
  n->pos = 15
  n->bits = 4

  First, let's just ignore the bits that come before the parent tp, that is
  the bits from 0 to (tp->pos-1). They are *known* but at this point we do
  not use them for anything.

  The bits from (tp->pos) to (tp->pos + tp->bits - 1) - "N", above - are the
  index into the parent's child array. That is, they will be used to find
  'n' among tp's children.

  The bits from (tp->pos + tp->bits) to (n->pos - 1) - "S" - are skipped bits
  for the node n.

  All the bits we have seen so far are significant to the node n. The rest
  of the bits are really not needed or indeed known in n->key.

  The bits from (n->pos) to (n->pos + n->bits - 1) - "C" - are the index into
  n's child array, and will of course be different for each child.


  The rest of the bits, from (n->pos + n->bits) onward, are completely unknown
  at this point.

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
	ilog2((PAGE_SIZE - sizeof(struct tnode)) / sizeof(struct tnode *))

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

static inline void free_leaf_info(struct leaf_info *leaf)
{
	kfree_rcu(leaf, rcu);
}

static struct tnode *tnode_alloc(size_t size)
{
	if (size <= PAGE_SIZE)
		return kzalloc(size, GFP_KERNEL);
	else
		return vzalloc(size);
}

static void tnode_free_safe(struct tnode *tn)
{
	BUG_ON(IS_LEAF(tn));
	tn->rcu.next = tnode_free_head;
	tnode_free_head = &tn->rcu;
}

static void tnode_free_flush(void)
{
	struct callback_head *head;

	while ((head = tnode_free_head)) {
		struct tnode *tn = container_of(head, struct tnode, rcu);

		tnode_free_head = head->next;
		tnode_free_size += offsetof(struct tnode, child[1 << tn->bits]);

		node_free(tn);
	}

	if (tnode_free_size >= PAGE_SIZE * sync_pages) {
		tnode_free_size = 0;
		synchronize_rcu();
	}
}

static struct tnode *leaf_new(t_key key)
{
	struct tnode *l = kmem_cache_alloc(trie_leaf_kmem, GFP_KERNEL);
	if (l) {
		l->parent = NULL;
		/* set key and pos to reflect full key value
		 * any trailing zeros in the key should be ignored
		 * as the nodes are searched
		 */
		l->key = key;
		l->pos = KEYLENGTH;
		/* set bits to 0 indicating we are not a tnode */
		l->bits = 0;

		INIT_HLIST_HEAD(&l->list);
	}
	return l;
}

static struct leaf_info *leaf_info_new(int plen)
{
	struct leaf_info *li = kmalloc(sizeof(struct leaf_info),  GFP_KERNEL);
	if (li) {
		li->plen = plen;
		li->mask_plen = ntohl(inet_make_mask(plen));
		INIT_LIST_HEAD(&li->falh);
	}
	return li;
}

static struct tnode *tnode_new(t_key key, int pos, int bits)
{
	size_t sz = offsetof(struct tnode, child[1 << bits]);
	struct tnode *tn = tnode_alloc(sz);
	unsigned int shift = pos + bits;

	/* verify bits and pos their msb bits clear and values are valid */
	BUG_ON(!bits || (shift > KEYLENGTH));

	if (tn) {
		tn->parent = NULL;
		tn->pos = pos;
		tn->bits = bits;
		tn->key = mask_pfx(key, pos);
		tn->full_children = 0;
		tn->empty_children = 1<<bits;
	}

	pr_debug("AT %p s=%zu %zu\n", tn, sizeof(struct tnode),
		 sizeof(struct tnode *) << bits);
	return tn;
}

/*
 * Check whether a tnode 'n' is "full", i.e. it is an internal node
 * and no bits are skipped. See discussion in dyntree paper p. 6
 */

static inline int tnode_full(const struct tnode *tn, const struct tnode *n)
{
	return n && IS_TNODE(n) && (n->pos == (tn->pos + tn->bits));
}

static inline void put_child(struct tnode *tn, int i,
			     struct tnode *n)
{
	tnode_put_child_reorg(tn, i, n, -1);
}

 /*
  * Add a child at position i overwriting the old value.
  * Update the value of full_children and empty_children.
  */

static void tnode_put_child_reorg(struct tnode *tn, int i, struct tnode *n,
				  int wasfull)
{
	struct tnode *chi = rtnl_dereference(tn->child[i]);
	int isfull;

	BUG_ON(i >= 1<<tn->bits);

	/* update emptyChildren */
	if (n == NULL && chi != NULL)
		tn->empty_children++;
	else if (n != NULL && chi == NULL)
		tn->empty_children--;

	/* update fullChildren */
	if (wasfull == -1)
		wasfull = tnode_full(tn, chi);

	isfull = tnode_full(tn, n);
	if (wasfull && !isfull)
		tn->full_children--;
	else if (!wasfull && isfull)
		tn->full_children++;

	node_set_parent(n, tn);

	rcu_assign_pointer(tn->child[i], n);
}

#define MAX_WORK 10
static struct tnode *resize(struct trie *t, struct tnode *tn)
{
	struct tnode *old_tn, *n = NULL;
	int inflate_threshold_use;
	int halve_threshold_use;
	int max_work;

	if (!tn)
		return NULL;

	pr_debug("In tnode_resize %p inflate_threshold=%d threshold=%d\n",
		 tn, inflate_threshold, halve_threshold);

	/* No children */
	if (tn->empty_children > (tnode_child_length(tn) - 1))
		goto no_children;

	/* One child */
	if (tn->empty_children == (tnode_child_length(tn) - 1))
		goto one_child;
	/*
	 * Double as long as the resulting node has a number of
	 * nonempty nodes that are above the threshold.
	 */

	/*
	 * From "Implementing a dynamic compressed trie" by Stefan Nilsson of
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

	/* Keep root node larger  */

	if (!node_parent(tn)) {
		inflate_threshold_use = inflate_threshold_root;
		halve_threshold_use = halve_threshold_root;
	} else {
		inflate_threshold_use = inflate_threshold;
		halve_threshold_use = halve_threshold;
	}

	max_work = MAX_WORK;
	while ((tn->full_children > 0 &&  max_work-- &&
		50 * (tn->full_children + tnode_child_length(tn)
		      - tn->empty_children)
		>= inflate_threshold_use * tnode_child_length(tn))) {

		old_tn = tn;
		tn = inflate(t, tn);

		if (IS_ERR(tn)) {
			tn = old_tn;
#ifdef CONFIG_IP_FIB_TRIE_STATS
			this_cpu_inc(t->stats->resize_node_skipped);
#endif
			break;
		}
	}

	/* Return if at least one inflate is run */
	if (max_work != MAX_WORK)
		return tn;

	/*
	 * Halve as long as the number of empty children in this
	 * node is above threshold.
	 */

	max_work = MAX_WORK;
	while (tn->bits > 1 &&  max_work-- &&
	       100 * (tnode_child_length(tn) - tn->empty_children) <
	       halve_threshold_use * tnode_child_length(tn)) {

		old_tn = tn;
		tn = halve(t, tn);
		if (IS_ERR(tn)) {
			tn = old_tn;
#ifdef CONFIG_IP_FIB_TRIE_STATS
			this_cpu_inc(t->stats->resize_node_skipped);
#endif
			break;
		}
	}


	/* Only one child remains */
	if (tn->empty_children == (tnode_child_length(tn) - 1)) {
		unsigned long i;
one_child:
		for (i = tnode_child_length(tn); !n && i;)
			n = tnode_get_child(tn, --i);
no_children:
		/* compress one level */
		node_set_parent(n, NULL);
		tnode_free_safe(tn);
		return n;
	}
	return tn;
}


static void tnode_clean_free(struct tnode *tn)
{
	struct tnode *tofree;
	int i;

	for (i = 0; i < tnode_child_length(tn); i++) {
		tofree = rtnl_dereference(tn->child[i]);
		if (tofree)
			node_free(tofree);
	}
	node_free(tn);
}

static struct tnode *inflate(struct trie *t, struct tnode *oldtnode)
{
	int olen = tnode_child_length(oldtnode);
	struct tnode *tn;
	int i;

	pr_debug("In inflate\n");

	tn = tnode_new(oldtnode->key, oldtnode->pos, oldtnode->bits + 1);

	if (!tn)
		return ERR_PTR(-ENOMEM);

	/*
	 * Preallocate and store tnodes before the actual work so we
	 * don't get into an inconsistent state if memory allocation
	 * fails. In case of failure we return the oldnode and  inflate
	 * of tnode is ignored.
	 */

	for (i = 0; i < olen; i++) {
		struct tnode *inode;

		inode = tnode_get_child(oldtnode, i);
		if (tnode_full(oldtnode, inode) && inode->bits > 1) {
			struct tnode *left, *right;
			t_key m = ~0U << (KEYLENGTH - 1) >> inode->pos;

			left = tnode_new(inode->key&(~m), inode->pos + 1,
					 inode->bits - 1);
			if (!left)
				goto nomem;

			right = tnode_new(inode->key|m, inode->pos + 1,
					  inode->bits - 1);

			if (!right) {
				node_free(left);
				goto nomem;
			}

			put_child(tn, 2*i, left);
			put_child(tn, 2*i+1, right);
		}
	}

	for (i = 0; i < olen; i++) {
		struct tnode *inode = tnode_get_child(oldtnode, i);
		struct tnode *left, *right;
		int size, j;

		/* An empty child */
		if (inode == NULL)
			continue;

		/* A leaf or an internal node with skipped bits */
		if (!tnode_full(oldtnode, inode)) {
			put_child(tn,
				tkey_extract_bits(inode->key, tn->pos, tn->bits),
				inode);
			continue;
		}

		/* An internal node with two children */
		if (inode->bits == 1) {
			put_child(tn, 2*i, rtnl_dereference(inode->child[0]));
			put_child(tn, 2*i+1, rtnl_dereference(inode->child[1]));

			tnode_free_safe(inode);
			continue;
		}

		/* An internal node with more than two children */

		/* We will replace this node 'inode' with two new
		 * ones, 'left' and 'right', each with half of the
		 * original children. The two new nodes will have
		 * a position one bit further down the key and this
		 * means that the "significant" part of their keys
		 * (see the discussion near the top of this file)
		 * will differ by one bit, which will be "0" in
		 * left's key and "1" in right's key. Since we are
		 * moving the key position by one step, the bit that
		 * we are moving away from - the bit at position
		 * (inode->pos) - is the one that will differ between
		 * left and right. So... we synthesize that bit in the
		 * two  new keys.
		 * The mask 'm' below will be a single "one" bit at
		 * the position (inode->pos)
		 */

		/* Use the old key, but set the new significant
		 *   bit to zero.
		 */

		left = tnode_get_child(tn, 2*i);
		put_child(tn, 2*i, NULL);

		BUG_ON(!left);

		right = tnode_get_child(tn, 2*i+1);
		put_child(tn, 2*i+1, NULL);

		BUG_ON(!right);

		size = tnode_child_length(left);
		for (j = 0; j < size; j++) {
			put_child(left, j, rtnl_dereference(inode->child[j]));
			put_child(right, j, rtnl_dereference(inode->child[j + size]));
		}
		put_child(tn, 2*i, resize(t, left));
		put_child(tn, 2*i+1, resize(t, right));

		tnode_free_safe(inode);
	}
	tnode_free_safe(oldtnode);
	return tn;
nomem:
	tnode_clean_free(tn);
	return ERR_PTR(-ENOMEM);
}

static struct tnode *halve(struct trie *t, struct tnode *oldtnode)
{
	int olen = tnode_child_length(oldtnode);
	struct tnode *tn, *left, *right;
	int i;

	pr_debug("In halve\n");

	tn = tnode_new(oldtnode->key, oldtnode->pos, oldtnode->bits - 1);

	if (!tn)
		return ERR_PTR(-ENOMEM);

	/*
	 * Preallocate and store tnodes before the actual work so we
	 * don't get into an inconsistent state if memory allocation
	 * fails. In case of failure we return the oldnode and halve
	 * of tnode is ignored.
	 */

	for (i = 0; i < olen; i += 2) {
		left = tnode_get_child(oldtnode, i);
		right = tnode_get_child(oldtnode, i+1);

		/* Two nonempty children */
		if (left && right) {
			struct tnode *newn;

			newn = tnode_new(left->key, tn->pos + tn->bits, 1);

			if (!newn)
				goto nomem;

			put_child(tn, i/2, newn);
		}

	}

	for (i = 0; i < olen; i += 2) {
		struct tnode *newBinNode;

		left = tnode_get_child(oldtnode, i);
		right = tnode_get_child(oldtnode, i+1);

		/* At least one of the children is empty */
		if (left == NULL) {
			if (right == NULL)    /* Both are empty */
				continue;
			put_child(tn, i/2, right);
			continue;
		}

		if (right == NULL) {
			put_child(tn, i/2, left);
			continue;
		}

		/* Two nonempty children */
		newBinNode = tnode_get_child(tn, i/2);
		put_child(tn, i/2, NULL);
		put_child(newBinNode, 0, left);
		put_child(newBinNode, 1, right);
		put_child(tn, i/2, resize(t, newBinNode));
	}
	tnode_free_safe(oldtnode);
	return tn;
nomem:
	tnode_clean_free(tn);
	return ERR_PTR(-ENOMEM);
}

/* readside must use rcu_read_lock currently dump routines
 via get_fa_head and dump */

static struct leaf_info *find_leaf_info(struct tnode *l, int plen)
{
	struct hlist_head *head = &l->list;
	struct leaf_info *li;

	hlist_for_each_entry_rcu(li, head, hlist)
		if (li->plen == plen)
			return li;

	return NULL;
}

static inline struct list_head *get_fa_head(struct tnode *l, int plen)
{
	struct leaf_info *li = find_leaf_info(l, plen);

	if (!li)
		return NULL;

	return &li->falh;
}

static void insert_leaf_info(struct hlist_head *head, struct leaf_info *new)
{
	struct leaf_info *li = NULL, *last = NULL;

	if (hlist_empty(head)) {
		hlist_add_head_rcu(&new->hlist, head);
	} else {
		hlist_for_each_entry(li, head, hlist) {
			if (new->plen > li->plen)
				break;

			last = li;
		}
		if (last)
			hlist_add_behind_rcu(&new->hlist, &last->hlist);
		else
			hlist_add_before_rcu(&new->hlist, &li->hlist);
	}
}

/* rcu_read_lock needs to be hold by caller from readside */

static struct tnode *fib_find_node(struct trie *t, u32 key)
{
	struct tnode *n = rcu_dereference_rtnl(t->trie);
	int pos = 0;

	while (n && IS_TNODE(n)) {
		if (tkey_sub_equals(n->key, pos, n->pos-pos, key)) {
			pos = n->pos + n->bits;
			n = tnode_get_child_rcu(n,
						tkey_extract_bits(key,
								  n->pos,
								  n->bits));
		} else
			break;
	}
	/* Case we have found a leaf. Compare prefixes */

	if (n != NULL && IS_LEAF(n) && tkey_equals(key, n->key))
		return n;

	return NULL;
}

static void trie_rebalance(struct trie *t, struct tnode *tn)
{
	int wasfull;
	t_key cindex, key;
	struct tnode *tp;

	key = tn->key;

	while (tn != NULL && (tp = node_parent(tn)) != NULL) {
		cindex = tkey_extract_bits(key, tp->pos, tp->bits);
		wasfull = tnode_full(tp, tnode_get_child(tp, cindex));
		tn = resize(t, tn);

		tnode_put_child_reorg(tp, cindex, tn, wasfull);

		tp = node_parent(tn);
		if (!tp)
			rcu_assign_pointer(t->trie, tn);

		tnode_free_flush();
		if (!tp)
			break;
		tn = tp;
	}

	/* Handle last (top) tnode */
	if (IS_TNODE(tn))
		tn = resize(t, tn);

	rcu_assign_pointer(t->trie, tn);
	tnode_free_flush();
}

/* only used from updater-side */

static struct list_head *fib_insert_node(struct trie *t, u32 key, int plen)
{
	int pos, newpos;
	struct tnode *tp = NULL, *tn = NULL;
	struct tnode *n;
	struct tnode *l;
	int missbit;
	struct list_head *fa_head = NULL;
	struct leaf_info *li;
	t_key cindex;

	pos = 0;
	n = rtnl_dereference(t->trie);

	/* If we point to NULL, stop. Either the tree is empty and we should
	 * just put a new leaf in if, or we have reached an empty child slot,
	 * and we should just put our new leaf in that.
	 * If we point to a T_TNODE, check if it matches our key. Note that
	 * a T_TNODE might be skipping any number of bits - its 'pos' need
	 * not be the parent's 'pos'+'bits'!
	 *
	 * If it does match the current key, get pos/bits from it, extract
	 * the index from our key, push the T_TNODE and walk the tree.
	 *
	 * If it doesn't, we have to replace it with a new T_TNODE.
	 *
	 * If we point to a T_LEAF, it might or might not have the same key
	 * as we do. If it does, just change the value, update the T_LEAF's
	 * value, and return it.
	 * If it doesn't, we need to replace it with a T_TNODE.
	 */

	while (n && IS_TNODE(n)) {
		if (tkey_sub_equals(n->key, pos, n->pos-pos, key)) {
			tp = n;
			pos = n->pos + n->bits;
			n = tnode_get_child(n,
					    tkey_extract_bits(key,
							      n->pos,
							      n->bits));

			BUG_ON(n && node_parent(n) != tp);
		} else
			break;
	}

	/*
	 * n  ----> NULL, LEAF or TNODE
	 *
	 * tp is n's (parent) ----> NULL or TNODE
	 */

	BUG_ON(tp && IS_LEAF(tp));

	/* Case 1: n is a leaf. Compare prefixes */

	if (n != NULL && IS_LEAF(n) && tkey_equals(key, n->key)) {
		li = leaf_info_new(plen);

		if (!li)
			return NULL;

		fa_head = &li->falh;
		insert_leaf_info(&n->list, li);
		goto done;
	}
	l = leaf_new(key);

	if (!l)
		return NULL;

	li = leaf_info_new(plen);

	if (!li) {
		node_free(l);
		return NULL;
	}

	fa_head = &li->falh;
	insert_leaf_info(&l->list, li);

	if (t->trie && n == NULL) {
		/* Case 2: n is NULL, and will just insert a new leaf */

		node_set_parent(l, tp);

		cindex = tkey_extract_bits(key, tp->pos, tp->bits);
		put_child(tp, cindex, l);
	} else {
		/* Case 3: n is a LEAF or a TNODE and the key doesn't match. */
		/*
		 *  Add a new tnode here
		 *  first tnode need some special handling
		 */

		if (n) {
			pos = tp ? tp->pos+tp->bits : 0;
			newpos = tkey_mismatch(key, pos, n->key);
			tn = tnode_new(n->key, newpos, 1);
		} else {
			newpos = 0;
			tn = tnode_new(key, newpos, 1); /* First tnode */
		}

		if (!tn) {
			free_leaf_info(li);
			node_free(l);
			return NULL;
		}

		node_set_parent(tn, tp);

		missbit = tkey_extract_bits(key, newpos, 1);
		put_child(tn, missbit, l);
		put_child(tn, 1-missbit, n);

		if (tp) {
			cindex = tkey_extract_bits(key, tp->pos, tp->bits);
			put_child(tp, cindex, tn);
		} else {
			rcu_assign_pointer(t->trie, tn);
		}

		tp = tn;
	}

	if (tp && tp->pos + tp->bits > 32)
		pr_warn("fib_trie tp=%p pos=%d, bits=%d, key=%0x plen=%d\n",
			tp, tp->pos, tp->bits, key, plen);

	/* Rebalance the trie */

	trie_rebalance(t, tp);
done:
	return fa_head;
}

/*
 * Caller must hold RTNL.
 */
int fib_table_insert(struct fib_table *tb, struct fib_config *cfg)
{
	struct trie *t = (struct trie *) tb->tb_data;
	struct fib_alias *fa, *new_fa;
	struct list_head *fa_head = NULL;
	struct fib_info *fi;
	int plen = cfg->fc_dst_len;
	u8 tos = cfg->fc_tos;
	u32 key, mask;
	int err;
	struct tnode *l;

	if (plen > 32)
		return -EINVAL;

	key = ntohl(cfg->fc_dst);

	pr_debug("Insert table=%u %08x/%d\n", tb->tb_id, key, plen);

	mask = ntohl(inet_make_mask(plen));

	if (key & ~mask)
		return -EINVAL;

	key = key & mask;

	fi = fib_create_info(cfg);
	if (IS_ERR(fi)) {
		err = PTR_ERR(fi);
		goto err;
	}

	l = fib_find_node(t, key);
	fa = NULL;

	if (l) {
		fa_head = get_fa_head(l, plen);
		fa = fib_find_alias(fa_head, tos, fi->fib_priority);
	}

	/* Now fa, if non-NULL, points to the first fib alias
	 * with the same keys [prefix,tos,priority], if such key already
	 * exists or to the node before which we will insert new one.
	 *
	 * If fa is NULL, we will need to allocate a new one and
	 * insert to the head of f.
	 *
	 * If f is NULL, no fib node matched the destination key
	 * and we need to allocate a new one of those as well.
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
		fa = list_entry(fa->fa_list.prev, struct fib_alias, fa_list);
		list_for_each_entry_continue(fa, fa_head, fa_list) {
			if (fa->fa_tos != tos)
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

			list_replace_rcu(&fa->fa_list, &new_fa->fa_list);
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
	/*
	 * Insert new entry to the list.
	 */

	if (!fa_head) {
		fa_head = fib_insert_node(t, key, plen);
		if (unlikely(!fa_head)) {
			err = -ENOMEM;
			goto out_free_new_fa;
		}
	}

	if (!plen)
		tb->tb_num_default++;

	list_add_tail_rcu(&new_fa->fa_list,
			  (fa ? &fa->fa_list : fa_head));

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

/* should be called with rcu_read_lock */
static int check_leaf(struct fib_table *tb, struct trie *t, struct tnode *l,
		      t_key key,  const struct flowi4 *flp,
		      struct fib_result *res, int fib_flags)
{
	struct leaf_info *li;
	struct hlist_head *hhead = &l->list;

	hlist_for_each_entry_rcu(li, hhead, hlist) {
		struct fib_alias *fa;

		if (l->key != (key & li->mask_plen))
			continue;

		list_for_each_entry_rcu(fa, &li->falh, fa_list) {
			struct fib_info *fi = fa->fa_info;
			int nhsel, err;

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
				this_cpu_inc(t->stats->semantic_match_passed);
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

#ifdef CONFIG_IP_FIB_TRIE_STATS
				this_cpu_inc(t->stats->semantic_match_passed);
#endif
				res->prefixlen = li->plen;
				res->nh_sel = nhsel;
				res->type = fa->fa_type;
				res->scope = fi->fib_scope;
				res->fi = fi;
				res->table = tb;
				res->fa_head = &li->falh;
				if (!(fib_flags & FIB_LOOKUP_NOREF))
					atomic_inc(&fi->fib_clntref);
				return 0;
			}
		}

#ifdef CONFIG_IP_FIB_TRIE_STATS
		this_cpu_inc(t->stats->semantic_match_miss);
#endif
	}

	return 1;
}

static inline t_key prefix_mismatch(t_key key, struct tnode *n)
{
	t_key prefix = n->key;

	return (key ^ prefix) & (prefix | -prefix);
}

int fib_table_lookup(struct fib_table *tb, const struct flowi4 *flp,
		     struct fib_result *res, int fib_flags)
{
	struct trie *t = (struct trie *)tb->tb_data;
#ifdef CONFIG_IP_FIB_TRIE_STATS
	struct trie_use_stats __percpu *stats = t->stats;
#endif
	const t_key key = ntohl(flp->daddr);
	struct tnode *n, *pn;
	t_key cindex;
	int ret = 1;

	rcu_read_lock();

	n = rcu_dereference(t->trie);
	if (!n)
		goto failed;

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
		 *   if !(index >> bits)
		 *     we know the value is child index
		 *   else
		 *     we have a mismatch in skip bits and failed
		 */
		if (index >> n->bits)
			break;

		/* we have found a leaf. Prefixes have already been compared */
		if (IS_LEAF(n))
			goto found;

		/* only record pn and cindex if we are going to be chopping
		 * bits later.  Otherwise we are just wasting cycles.
		 */
		if (index) {
			pn = n;
			cindex = index;
		}

		n = rcu_dereference(n->child[index]);
		if (unlikely(!n))
			goto backtrace;
	}

	/* Step 2: Sort out leaves and begin backtracing for longest prefix */
	for (;;) {
		/* record the pointer where our next node pointer is stored */
		struct tnode __rcu **cptr = n->child;

		/* This test verifies that none of the bits that differ
		 * between the key and the prefix exist in the region of
		 * the lsb and higher in the prefix.
		 */
		if (unlikely(prefix_mismatch(key, n)))
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
					goto failed;
#ifdef CONFIG_IP_FIB_TRIE_STATS
				this_cpu_inc(stats->backtrack);
#endif
				/* Get Child's index */
				cindex = get_index(pkey, pn);
			}

			/* strip the least significant bit from the cindex */
			cindex &= cindex - 1;

			/* grab pointer for next child node */
			cptr = &pn->child[cindex];
		}
	}

found:
	/* Step 3: Process the leaf, if that fails fall back to backtracing */
	ret = check_leaf(tb, t, n, key, flp, res, fib_flags);
	if (unlikely(ret > 0))
		goto backtrace;
failed:
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(fib_table_lookup);

/*
 * Remove the leaf and return parent.
 */
static void trie_leaf_remove(struct trie *t, struct tnode *l)
{
	struct tnode *tp = node_parent(l);

	pr_debug("entering trie_leaf_remove(%p)\n", l);

	if (tp) {
		t_key cindex = tkey_extract_bits(l->key, tp->pos, tp->bits);
		put_child(tp, cindex, NULL);
		trie_rebalance(t, tp);
	} else
		RCU_INIT_POINTER(t->trie, NULL);

	node_free(l);
}

/*
 * Caller must hold RTNL.
 */
int fib_table_delete(struct fib_table *tb, struct fib_config *cfg)
{
	struct trie *t = (struct trie *) tb->tb_data;
	u32 key, mask;
	int plen = cfg->fc_dst_len;
	u8 tos = cfg->fc_tos;
	struct fib_alias *fa, *fa_to_delete;
	struct list_head *fa_head;
	struct tnode *l;
	struct leaf_info *li;

	if (plen > 32)
		return -EINVAL;

	key = ntohl(cfg->fc_dst);
	mask = ntohl(inet_make_mask(plen));

	if (key & ~mask)
		return -EINVAL;

	key = key & mask;
	l = fib_find_node(t, key);

	if (!l)
		return -ESRCH;

	li = find_leaf_info(l, plen);

	if (!li)
		return -ESRCH;

	fa_head = &li->falh;
	fa = fib_find_alias(fa_head, tos, 0);

	if (!fa)
		return -ESRCH;

	pr_debug("Deleting %08x/%d tos=%d t=%p\n", key, plen, tos, t);

	fa_to_delete = NULL;
	fa = list_entry(fa->fa_list.prev, struct fib_alias, fa_list);
	list_for_each_entry_continue(fa, fa_head, fa_list) {
		struct fib_info *fi = fa->fa_info;

		if (fa->fa_tos != tos)
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

	fa = fa_to_delete;
	rtmsg_fib(RTM_DELROUTE, htonl(key), fa, plen, tb->tb_id,
		  &cfg->fc_nlinfo, 0);

	list_del_rcu(&fa->fa_list);

	if (!plen)
		tb->tb_num_default--;

	if (list_empty(fa_head)) {
		hlist_del_rcu(&li->hlist);
		free_leaf_info(li);
	}

	if (hlist_empty(&l->list))
		trie_leaf_remove(t, l);

	if (fa->fa_state & FA_S_ACCESSED)
		rt_cache_flush(cfg->fc_nlinfo.nl_net);

	fib_release_info(fa->fa_info);
	alias_free_mem_rcu(fa);
	return 0;
}

static int trie_flush_list(struct list_head *head)
{
	struct fib_alias *fa, *fa_node;
	int found = 0;

	list_for_each_entry_safe(fa, fa_node, head, fa_list) {
		struct fib_info *fi = fa->fa_info;

		if (fi && (fi->fib_flags & RTNH_F_DEAD)) {
			list_del_rcu(&fa->fa_list);
			fib_release_info(fa->fa_info);
			alias_free_mem_rcu(fa);
			found++;
		}
	}
	return found;
}

static int trie_flush_leaf(struct tnode *l)
{
	int found = 0;
	struct hlist_head *lih = &l->list;
	struct hlist_node *tmp;
	struct leaf_info *li = NULL;

	hlist_for_each_entry_safe(li, tmp, lih, hlist) {
		found += trie_flush_list(&li->falh);

		if (list_empty(&li->falh)) {
			hlist_del_rcu(&li->hlist);
			free_leaf_info(li);
		}
	}
	return found;
}

/*
 * Scan for the next right leaf starting at node p->child[idx]
 * Since we have back pointer, no recursion necessary.
 */
static struct tnode *leaf_walk_rcu(struct tnode *p, struct tnode *c)
{
	do {
		t_key idx;

		if (c)
			idx = tkey_extract_bits(c->key, p->pos, p->bits) + 1;
		else
			idx = 0;

		while (idx < 1u << p->bits) {
			c = tnode_get_child_rcu(p, idx++);
			if (!c)
				continue;

			if (IS_LEAF(c))
				return c;

			/* Rescan start scanning in new node */
			p = c;
			idx = 0;
		}

		/* Node empty, walk back up to parent */
		c = p;
	} while ((p = node_parent_rcu(c)) != NULL);

	return NULL; /* Root of trie */
}

static struct tnode *trie_firstleaf(struct trie *t)
{
	struct tnode *n = rcu_dereference_rtnl(t->trie);

	if (!n)
		return NULL;

	if (IS_LEAF(n))          /* trie is just a leaf */
		return n;

	return leaf_walk_rcu(n, NULL);
}

static struct tnode *trie_nextleaf(struct tnode *l)
{
	struct tnode *p = node_parent_rcu(l);

	if (!p)
		return NULL;	/* trie with just one leaf */

	return leaf_walk_rcu(p, l);
}

static struct tnode *trie_leafindex(struct trie *t, int index)
{
	struct tnode *l = trie_firstleaf(t);

	while (l && index-- > 0)
		l = trie_nextleaf(l);

	return l;
}


/*
 * Caller must hold RTNL.
 */
int fib_table_flush(struct fib_table *tb)
{
	struct trie *t = (struct trie *) tb->tb_data;
	struct tnode *l, *ll = NULL;
	int found = 0;

	for (l = trie_firstleaf(t); l; l = trie_nextleaf(l)) {
		found += trie_flush_leaf(l);

		if (ll && hlist_empty(&ll->list))
			trie_leaf_remove(t, ll);
		ll = l;
	}

	if (ll && hlist_empty(&ll->list))
		trie_leaf_remove(t, ll);

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

static int fn_trie_dump_fa(t_key key, int plen, struct list_head *fah,
			   struct fib_table *tb,
			   struct sk_buff *skb, struct netlink_callback *cb)
{
	int i, s_i;
	struct fib_alias *fa;
	__be32 xkey = htonl(key);

	s_i = cb->args[5];
	i = 0;

	/* rcu_read_lock is hold by caller */

	list_for_each_entry_rcu(fa, fah, fa_list) {
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
				  plen,
				  fa->fa_tos,
				  fa->fa_info, NLM_F_MULTI) < 0) {
			cb->args[5] = i;
			return -1;
		}
		i++;
	}
	cb->args[5] = i;
	return skb->len;
}

static int fn_trie_dump_leaf(struct tnode *l, struct fib_table *tb,
			struct sk_buff *skb, struct netlink_callback *cb)
{
	struct leaf_info *li;
	int i, s_i;

	s_i = cb->args[4];
	i = 0;

	/* rcu_read_lock is hold by caller */
	hlist_for_each_entry_rcu(li, &l->list, hlist) {
		if (i < s_i) {
			i++;
			continue;
		}

		if (i > s_i)
			cb->args[5] = 0;

		if (list_empty(&li->falh))
			continue;

		if (fn_trie_dump_fa(l->key, li->plen, &li->falh, tb, skb, cb) < 0) {
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
	struct tnode *l;
	struct trie *t = (struct trie *) tb->tb_data;
	t_key key = cb->args[2];
	int count = cb->args[3];

	rcu_read_lock();
	/* Dump starting at last key.
	 * Note: 0.0.0.0/0 (ie default) is first key.
	 */
	if (count == 0)
		l = trie_firstleaf(t);
	else {
		/* Normally, continue from last key, but if that is missing
		 * fallback to using slow rescan
		 */
		l = fib_find_node(t, key);
		if (!l)
			l = trie_leafindex(t, count);
	}

	while (l) {
		cb->args[2] = l->key;
		if (fn_trie_dump_leaf(l, tb, skb, cb) < 0) {
			cb->args[3] = count;
			rcu_read_unlock();
			return -1;
		}

		++count;
		l = trie_nextleaf(l);
		memset(&cb->args[4], 0,
		       sizeof(cb->args) - 4*sizeof(cb->args[0]));
	}
	cb->args[3] = count;
	rcu_read_unlock();

	return skb->len;
}

void __init fib_trie_init(void)
{
	fn_alias_kmem = kmem_cache_create("ip_fib_alias",
					  sizeof(struct fib_alias),
					  0, SLAB_PANIC, NULL);

	trie_leaf_kmem = kmem_cache_create("ip_fib_trie",
					   max(sizeof(struct tnode),
					       sizeof(struct leaf_info)),
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
	struct tnode *tn = iter->tnode;
	unsigned int cindex = iter->index;
	struct tnode *p;

	/* A single entry routing table */
	if (!tn)
		return NULL;

	pr_debug("get_next iter={node=%p index=%d depth=%d}\n",
		 iter->tnode, iter->index, iter->depth);
rescan:
	while (cindex < (1<<tn->bits)) {
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
		cindex = tkey_extract_bits(tn->key, p->pos, p->bits)+1;
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
			struct leaf_info *li;

			s->leaves++;
			s->totdepth += iter.depth;
			if (iter.depth > s->maxdepth)
				s->maxdepth = iter.depth;

			hlist_for_each_entry_rcu(li, &n->list, hlist)
				++s->prefixes;
		} else {
			int i;

			s->tnodes++;
			if (n->bits < MAX_STAT_DEPTH)
				s->nodesizes[n->bits]++;

			for (i = 0; i < tnode_child_length(n); i++)
				if (!rcu_access_pointer(n->child[i]))
					s->nullpointers++;
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
	bytes = sizeof(struct tnode) * stat->leaves;

	seq_printf(seq, "\tPrefixes:       %u\n", stat->prefixes);
	bytes += sizeof(struct leaf_info) * stat->prefixes;

	seq_printf(seq, "\tInternal nodes: %u\n\t", stat->tnodes);
	bytes += sizeof(struct tnode) * stat->tnodes;

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
		   sizeof(struct tnode), sizeof(struct tnode));

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

		seq_indent(seq, iter->depth - 1);
		seq_printf(seq, "  +-- %pI4/%d %d %d %d\n",
			   &prf, n->pos, n->bits, n->full_children,
			   n->empty_children);
	} else {
		struct leaf_info *li;
		__be32 val = htonl(n->key);

		seq_indent(seq, iter->depth);
		seq_printf(seq, "  |-- %pI4\n", &val);

		hlist_for_each_entry_rcu(li, &n->list, hlist) {
			struct fib_alias *fa;

			list_for_each_entry_rcu(fa, &li->falh, fa_list) {
				char buf1[32], buf2[32];

				seq_indent(seq, iter->depth+1);
				seq_printf(seq, "  /%d %s %s", li->plen,
					   rtn_scope(buf1, sizeof(buf1),
						     fa->fa_info->fib_scope),
					   rtn_type(buf2, sizeof(buf2),
						    fa->fa_type));
				if (fa->fa_tos)
					seq_printf(seq, " tos=%d", fa->fa_tos);
				seq_putc(seq, '\n');
			}
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
	struct trie *main_trie;
	loff_t	pos;
	t_key	key;
};

static struct tnode *fib_route_get_idx(struct fib_route_iter *iter, loff_t pos)
{
	struct tnode *l = NULL;
	struct trie *t = iter->main_trie;

	/* use cache location of last found key */
	if (iter->pos > 0 && pos >= iter->pos && (l = fib_find_node(t, iter->key)))
		pos -= iter->pos;
	else {
		iter->pos = 0;
		l = trie_firstleaf(t);
	}

	while (l && pos-- > 0) {
		iter->pos++;
		l = trie_nextleaf(l);
	}

	if (l)
		iter->key = pos;	/* remember it */
	else
		iter->pos = 0;		/* forget it */

	return l;
}

static void *fib_route_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	struct fib_route_iter *iter = seq->private;
	struct fib_table *tb;

	rcu_read_lock();
	tb = fib_get_table(seq_file_net(seq), RT_TABLE_MAIN);
	if (!tb)
		return NULL;

	iter->main_trie = (struct trie *) tb->tb_data;
	if (*pos == 0)
		return SEQ_START_TOKEN;
	else
		return fib_route_get_idx(iter, *pos - 1);
}

static void *fib_route_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct fib_route_iter *iter = seq->private;
	struct tnode *l = v;

	++*pos;
	if (v == SEQ_START_TOKEN) {
		iter->pos = 0;
		l = trie_firstleaf(iter->main_trie);
	} else {
		iter->pos++;
		l = trie_nextleaf(l);
	}

	if (l)
		iter->key = l->key;
	else
		iter->pos = 0;
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
	struct tnode *l = v;
	struct leaf_info *li;

	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, "%-127s\n", "Iface\tDestination\tGateway "
			   "\tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU"
			   "\tWindow\tIRTT");
		return 0;
	}

	hlist_for_each_entry_rcu(li, &l->list, hlist) {
		struct fib_alias *fa;
		__be32 mask, prefix;

		mask = inet_make_mask(li->plen);
		prefix = htonl(l->key);

		list_for_each_entry_rcu(fa, &li->falh, fa_list) {
			const struct fib_info *fi = fa->fa_info;
			unsigned int flags = fib_flag_trans(fa->fa_type, mask, fi);

			if (fa->fa_type == RTN_BROADCAST
			    || fa->fa_type == RTN_MULTICAST)
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

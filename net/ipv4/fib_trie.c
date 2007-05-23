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
 * This work is based on the LPC-trie which is originally descibed in:
 *
 * An experimental study of compression methods for dynamic tries
 * Stefan Nilsson and Matti Tikkanen. Algorithmica, 33(1):19-33, 2002.
 * http://www.nada.kth.se/~snilsson/public/papers/dyntrie2/
 *
 *
 * IP-address lookup using LC-tries. Stefan Nilsson and Gunnar Karlsson
 * IEEE Journal on Selected Areas in Communications, 17(6):1083-1092, June 1999
 *
 * Version:	$Id: fib_trie.c,v 1.3 2005/06/08 14:20:01 robert Exp $
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

#define VERSION "0.408"

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
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
#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/sock.h>
#include <net/ip_fib.h>
#include "fib_lookup.h"

#undef CONFIG_IP_FIB_TRIE_STATS
#define MAX_STAT_DEPTH 32

#define KEYLENGTH (8*sizeof(t_key))
#define MASK_PFX(k, l) (((l)==0)?0:(k >> (KEYLENGTH-l)) << (KEYLENGTH-l))
#define TKEY_GET_MASK(offset, bits) (((bits)==0)?0:((t_key)(-1) << (KEYLENGTH - bits) >> offset))

typedef unsigned int t_key;

#define T_TNODE 0
#define T_LEAF  1
#define NODE_TYPE_MASK	0x1UL
#define NODE_PARENT(node) \
	((struct tnode *)rcu_dereference(((node)->parent & ~NODE_TYPE_MASK)))

#define NODE_TYPE(node) ((node)->parent & NODE_TYPE_MASK)

#define NODE_SET_PARENT(node, ptr)		\
	rcu_assign_pointer((node)->parent,	\
			   ((unsigned long)(ptr)) | NODE_TYPE(node))

#define IS_TNODE(n) (!(n->parent & T_LEAF))
#define IS_LEAF(n) (n->parent & T_LEAF)

struct node {
	t_key key;
	unsigned long parent;
};

struct leaf {
	t_key key;
	unsigned long parent;
	struct hlist_head list;
	struct rcu_head rcu;
};

struct leaf_info {
	struct hlist_node hlist;
	struct rcu_head rcu;
	int plen;
	struct list_head falh;
};

struct tnode {
	t_key key;
	unsigned long parent;
	unsigned short pos:5;		/* 2log(KEYLENGTH) bits needed */
	unsigned short bits:5;		/* 2log(KEYLENGTH) bits needed */
	unsigned short full_children;	/* KEYLENGTH bits needed */
	unsigned short empty_children;	/* KEYLENGTH bits needed */
	struct rcu_head rcu;
	struct node *child[0];
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
	unsigned int nodesizes[MAX_STAT_DEPTH];
};

struct trie {
	struct node *trie;
#ifdef CONFIG_IP_FIB_TRIE_STATS
	struct trie_use_stats stats;
#endif
	int size;
	unsigned int revision;
};

static void put_child(struct trie *t, struct tnode *tn, int i, struct node *n);
static void tnode_put_child_reorg(struct tnode *tn, int i, struct node *n, int wasfull);
static struct node *resize(struct trie *t, struct tnode *tn);
static struct tnode *inflate(struct trie *t, struct tnode *tn);
static struct tnode *halve(struct trie *t, struct tnode *tn);
static void tnode_free(struct tnode *tn);

static struct kmem_cache *fn_alias_kmem __read_mostly;
static struct trie *trie_local = NULL, *trie_main = NULL;


/* rcu_read_lock needs to be hold by caller from readside */

static inline struct node *tnode_get_child(struct tnode *tn, int i)
{
	BUG_ON(i >= 1 << tn->bits);

	return rcu_dereference(tn->child[i]);
}

static inline int tnode_child_length(const struct tnode *tn)
{
	return 1 << tn->bits;
}

static inline t_key tkey_extract_bits(t_key a, int offset, int bits)
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

static inline void check_tnode(const struct tnode *tn)
{
	WARN_ON(tn && tn->pos+tn->bits > 32);
}

static int halve_threshold = 25;
static int inflate_threshold = 50;
static int halve_threshold_root = 8;
static int inflate_threshold_root = 15;


static void __alias_free_mem(struct rcu_head *head)
{
	struct fib_alias *fa = container_of(head, struct fib_alias, rcu);
	kmem_cache_free(fn_alias_kmem, fa);
}

static inline void alias_free_mem_rcu(struct fib_alias *fa)
{
	call_rcu(&fa->rcu, __alias_free_mem);
}

static void __leaf_free_rcu(struct rcu_head *head)
{
	kfree(container_of(head, struct leaf, rcu));
}

static void __leaf_info_free_rcu(struct rcu_head *head)
{
	kfree(container_of(head, struct leaf_info, rcu));
}

static inline void free_leaf_info(struct leaf_info *leaf)
{
	call_rcu(&leaf->rcu, __leaf_info_free_rcu);
}

static struct tnode *tnode_alloc(unsigned int size)
{
	struct page *pages;

	if (size <= PAGE_SIZE)
		return kcalloc(size, 1, GFP_KERNEL);

	pages = alloc_pages(GFP_KERNEL|__GFP_ZERO, get_order(size));
	if (!pages)
		return NULL;

	return page_address(pages);
}

static void __tnode_free_rcu(struct rcu_head *head)
{
	struct tnode *tn = container_of(head, struct tnode, rcu);
	unsigned int size = sizeof(struct tnode) +
		(1 << tn->bits) * sizeof(struct node *);

	if (size <= PAGE_SIZE)
		kfree(tn);
	else
		free_pages((unsigned long)tn, get_order(size));
}

static inline void tnode_free(struct tnode *tn)
{
	if (IS_LEAF(tn)) {
		struct leaf *l = (struct leaf *) tn;
		call_rcu_bh(&l->rcu, __leaf_free_rcu);
	} else
		call_rcu(&tn->rcu, __tnode_free_rcu);
}

static struct leaf *leaf_new(void)
{
	struct leaf *l = kmalloc(sizeof(struct leaf),  GFP_KERNEL);
	if (l) {
		l->parent = T_LEAF;
		INIT_HLIST_HEAD(&l->list);
	}
	return l;
}

static struct leaf_info *leaf_info_new(int plen)
{
	struct leaf_info *li = kmalloc(sizeof(struct leaf_info),  GFP_KERNEL);
	if (li) {
		li->plen = plen;
		INIT_LIST_HEAD(&li->falh);
	}
	return li;
}

static struct tnode* tnode_new(t_key key, int pos, int bits)
{
	int nchildren = 1<<bits;
	int sz = sizeof(struct tnode) + nchildren * sizeof(struct node *);
	struct tnode *tn = tnode_alloc(sz);

	if (tn) {
		memset(tn, 0, sz);
		tn->parent = T_TNODE;
		tn->pos = pos;
		tn->bits = bits;
		tn->key = key;
		tn->full_children = 0;
		tn->empty_children = 1<<bits;
	}

	pr_debug("AT %p s=%u %u\n", tn, (unsigned int) sizeof(struct tnode),
		 (unsigned int) (sizeof(struct node) * 1<<bits));
	return tn;
}

/*
 * Check whether a tnode 'n' is "full", i.e. it is an internal node
 * and no bits are skipped. See discussion in dyntree paper p. 6
 */

static inline int tnode_full(const struct tnode *tn, const struct node *n)
{
	if (n == NULL || IS_LEAF(n))
		return 0;

	return ((struct tnode *) n)->pos == tn->pos + tn->bits;
}

static inline void put_child(struct trie *t, struct tnode *tn, int i, struct node *n)
{
	tnode_put_child_reorg(tn, i, n, -1);
}

 /*
  * Add a child at position i overwriting the old value.
  * Update the value of full_children and empty_children.
  */

static void tnode_put_child_reorg(struct tnode *tn, int i, struct node *n, int wasfull)
{
	struct node *chi = tn->child[i];
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

	if (n)
		NODE_SET_PARENT(n, tn);

	rcu_assign_pointer(tn->child[i], n);
}

static struct node *resize(struct trie *t, struct tnode *tn)
{
	int i;
	int err = 0;
	struct tnode *old_tn;
	int inflate_threshold_use;
	int halve_threshold_use;
	int max_resize;

	if (!tn)
		return NULL;

	pr_debug("In tnode_resize %p inflate_threshold=%d threshold=%d\n",
		 tn, inflate_threshold, halve_threshold);

	/* No children */
	if (tn->empty_children == tnode_child_length(tn)) {
		tnode_free(tn);
		return NULL;
	}
	/* One child */
	if (tn->empty_children == tnode_child_length(tn) - 1)
		for (i = 0; i < tnode_child_length(tn); i++) {
			struct node *n;

			n = tn->child[i];
			if (!n)
				continue;

			/* compress one level */
			NODE_SET_PARENT(n, NULL);
			tnode_free(tn);
			return n;
		}
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

	check_tnode(tn);

	/* Keep root node larger  */

	if (!tn->parent)
		inflate_threshold_use = inflate_threshold_root;
	else
		inflate_threshold_use = inflate_threshold;

	err = 0;
	max_resize = 10;
	while ((tn->full_children > 0 &&  max_resize-- &&
	       50 * (tn->full_children + tnode_child_length(tn) - tn->empty_children) >=
				inflate_threshold_use * tnode_child_length(tn))) {

		old_tn = tn;
		tn = inflate(t, tn);
		if (IS_ERR(tn)) {
			tn = old_tn;
#ifdef CONFIG_IP_FIB_TRIE_STATS
			t->stats.resize_node_skipped++;
#endif
			break;
		}
	}

	if (max_resize < 0) {
		if (!tn->parent)
			printk(KERN_WARNING "Fix inflate_threshold_root. Now=%d size=%d bits\n",
			       inflate_threshold_root, tn->bits);
		else
			printk(KERN_WARNING "Fix inflate_threshold. Now=%d size=%d bits\n",
			       inflate_threshold, tn->bits);
	}

	check_tnode(tn);

	/*
	 * Halve as long as the number of empty children in this
	 * node is above threshold.
	 */


	/* Keep root node larger  */

	if (!tn->parent)
		halve_threshold_use = halve_threshold_root;
	else
		halve_threshold_use = halve_threshold;

	err = 0;
	max_resize = 10;
	while (tn->bits > 1 &&  max_resize-- &&
	       100 * (tnode_child_length(tn) - tn->empty_children) <
	       halve_threshold_use * tnode_child_length(tn)) {

		old_tn = tn;
		tn = halve(t, tn);
		if (IS_ERR(tn)) {
			tn = old_tn;
#ifdef CONFIG_IP_FIB_TRIE_STATS
			t->stats.resize_node_skipped++;
#endif
			break;
		}
	}

	if (max_resize < 0) {
		if (!tn->parent)
			printk(KERN_WARNING "Fix halve_threshold_root. Now=%d size=%d bits\n",
			       halve_threshold_root, tn->bits);
		else
			printk(KERN_WARNING "Fix halve_threshold. Now=%d size=%d bits\n",
			       halve_threshold, tn->bits);
	}

	/* Only one child remains */
	if (tn->empty_children == tnode_child_length(tn) - 1)
		for (i = 0; i < tnode_child_length(tn); i++) {
			struct node *n;

			n = tn->child[i];
			if (!n)
				continue;

			/* compress one level */

			NODE_SET_PARENT(n, NULL);
			tnode_free(tn);
			return n;
		}

	return (struct node *) tn;
}

static struct tnode *inflate(struct trie *t, struct tnode *tn)
{
	struct tnode *inode;
	struct tnode *oldtnode = tn;
	int olen = tnode_child_length(tn);
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
		struct tnode *inode = (struct tnode *) tnode_get_child(oldtnode, i);

		if (inode &&
		    IS_TNODE(inode) &&
		    inode->pos == oldtnode->pos + oldtnode->bits &&
		    inode->bits > 1) {
			struct tnode *left, *right;
			t_key m = TKEY_GET_MASK(inode->pos, 1);

			left = tnode_new(inode->key&(~m), inode->pos + 1,
					 inode->bits - 1);
			if (!left)
				goto nomem;

			right = tnode_new(inode->key|m, inode->pos + 1,
					  inode->bits - 1);

			if (!right) {
				tnode_free(left);
				goto nomem;
			}

			put_child(t, tn, 2*i, (struct node *) left);
			put_child(t, tn, 2*i+1, (struct node *) right);
		}
	}

	for (i = 0; i < olen; i++) {
		struct node *node = tnode_get_child(oldtnode, i);
		struct tnode *left, *right;
		int size, j;

		/* An empty child */
		if (node == NULL)
			continue;

		/* A leaf or an internal node with skipped bits */

		if (IS_LEAF(node) || ((struct tnode *) node)->pos >
		   tn->pos + tn->bits - 1) {
			if (tkey_extract_bits(node->key, oldtnode->pos + oldtnode->bits,
					     1) == 0)
				put_child(t, tn, 2*i, node);
			else
				put_child(t, tn, 2*i+1, node);
			continue;
		}

		/* An internal node with two children */
		inode = (struct tnode *) node;

		if (inode->bits == 1) {
			put_child(t, tn, 2*i, inode->child[0]);
			put_child(t, tn, 2*i+1, inode->child[1]);

			tnode_free(inode);
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

		left = (struct tnode *) tnode_get_child(tn, 2*i);
		put_child(t, tn, 2*i, NULL);

		BUG_ON(!left);

		right = (struct tnode *) tnode_get_child(tn, 2*i+1);
		put_child(t, tn, 2*i+1, NULL);

		BUG_ON(!right);

		size = tnode_child_length(left);
		for (j = 0; j < size; j++) {
			put_child(t, left, j, inode->child[j]);
			put_child(t, right, j, inode->child[j + size]);
		}
		put_child(t, tn, 2*i, resize(t, left));
		put_child(t, tn, 2*i+1, resize(t, right));

		tnode_free(inode);
	}
	tnode_free(oldtnode);
	return tn;
nomem:
	{
		int size = tnode_child_length(tn);
		int j;

		for (j = 0; j < size; j++)
			if (tn->child[j])
				tnode_free((struct tnode *)tn->child[j]);

		tnode_free(tn);

		return ERR_PTR(-ENOMEM);
	}
}

static struct tnode *halve(struct trie *t, struct tnode *tn)
{
	struct tnode *oldtnode = tn;
	struct node *left, *right;
	int i;
	int olen = tnode_child_length(tn);

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

			put_child(t, tn, i/2, (struct node *)newn);
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
			put_child(t, tn, i/2, right);
			continue;
		}

		if (right == NULL) {
			put_child(t, tn, i/2, left);
			continue;
		}

		/* Two nonempty children */
		newBinNode = (struct tnode *) tnode_get_child(tn, i/2);
		put_child(t, tn, i/2, NULL);
		put_child(t, newBinNode, 0, left);
		put_child(t, newBinNode, 1, right);
		put_child(t, tn, i/2, resize(t, newBinNode));
	}
	tnode_free(oldtnode);
	return tn;
nomem:
	{
		int size = tnode_child_length(tn);
		int j;

		for (j = 0; j < size; j++)
			if (tn->child[j])
				tnode_free((struct tnode *)tn->child[j]);

		tnode_free(tn);

		return ERR_PTR(-ENOMEM);
	}
}

static void trie_init(struct trie *t)
{
	if (!t)
		return;

	t->size = 0;
	rcu_assign_pointer(t->trie, NULL);
	t->revision = 0;
#ifdef CONFIG_IP_FIB_TRIE_STATS
	memset(&t->stats, 0, sizeof(struct trie_use_stats));
#endif
}

/* readside must use rcu_read_lock currently dump routines
 via get_fa_head and dump */

static struct leaf_info *find_leaf_info(struct leaf *l, int plen)
{
	struct hlist_head *head = &l->list;
	struct hlist_node *node;
	struct leaf_info *li;

	hlist_for_each_entry_rcu(li, node, head, hlist)
		if (li->plen == plen)
			return li;

	return NULL;
}

static inline struct list_head * get_fa_head(struct leaf *l, int plen)
{
	struct leaf_info *li = find_leaf_info(l, plen);

	if (!li)
		return NULL;

	return &li->falh;
}

static void insert_leaf_info(struct hlist_head *head, struct leaf_info *new)
{
	struct leaf_info *li = NULL, *last = NULL;
	struct hlist_node *node;

	if (hlist_empty(head)) {
		hlist_add_head_rcu(&new->hlist, head);
	} else {
		hlist_for_each_entry(li, node, head, hlist) {
			if (new->plen > li->plen)
				break;

			last = li;
		}
		if (last)
			hlist_add_after_rcu(&last->hlist, &new->hlist);
		else
			hlist_add_before_rcu(&new->hlist, &li->hlist);
	}
}

/* rcu_read_lock needs to be hold by caller from readside */

static struct leaf *
fib_find_node(struct trie *t, u32 key)
{
	int pos;
	struct tnode *tn;
	struct node *n;

	pos = 0;
	n = rcu_dereference(t->trie);

	while (n != NULL &&  NODE_TYPE(n) == T_TNODE) {
		tn = (struct tnode *) n;

		check_tnode(tn);

		if (tkey_sub_equals(tn->key, pos, tn->pos-pos, key)) {
			pos = tn->pos + tn->bits;
			n = tnode_get_child(tn, tkey_extract_bits(key, tn->pos, tn->bits));
		} else
			break;
	}
	/* Case we have found a leaf. Compare prefixes */

	if (n != NULL && IS_LEAF(n) && tkey_equals(key, n->key))
		return (struct leaf *)n;

	return NULL;
}

static struct node *trie_rebalance(struct trie *t, struct tnode *tn)
{
	int wasfull;
	t_key cindex, key;
	struct tnode *tp = NULL;

	key = tn->key;

	while (tn != NULL && NODE_PARENT(tn) != NULL) {

		tp = NODE_PARENT(tn);
		cindex = tkey_extract_bits(key, tp->pos, tp->bits);
		wasfull = tnode_full(tp, tnode_get_child(tp, cindex));
		tn = (struct tnode *) resize (t, (struct tnode *)tn);
		tnode_put_child_reorg((struct tnode *)tp, cindex,(struct node*)tn, wasfull);

		if (!NODE_PARENT(tn))
			break;

		tn = NODE_PARENT(tn);
	}
	/* Handle last (top) tnode */
	if (IS_TNODE(tn))
		tn = (struct tnode*) resize(t, (struct tnode *)tn);

	return (struct node*) tn;
}

/* only used from updater-side */

static  struct list_head *
fib_insert_node(struct trie *t, int *err, u32 key, int plen)
{
	int pos, newpos;
	struct tnode *tp = NULL, *tn = NULL;
	struct node *n;
	struct leaf *l;
	int missbit;
	struct list_head *fa_head = NULL;
	struct leaf_info *li;
	t_key cindex;

	pos = 0;
	n = t->trie;

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

	while (n != NULL &&  NODE_TYPE(n) == T_TNODE) {
		tn = (struct tnode *) n;

		check_tnode(tn);

		if (tkey_sub_equals(tn->key, pos, tn->pos-pos, key)) {
			tp = tn;
			pos = tn->pos + tn->bits;
			n = tnode_get_child(tn, tkey_extract_bits(key, tn->pos, tn->bits));

			BUG_ON(n && NODE_PARENT(n) != tn);
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
		struct leaf *l = (struct leaf *) n;

		li = leaf_info_new(plen);

		if (!li) {
			*err = -ENOMEM;
			goto err;
		}

		fa_head = &li->falh;
		insert_leaf_info(&l->list, li);
		goto done;
	}
	t->size++;
	l = leaf_new();

	if (!l) {
		*err = -ENOMEM;
		goto err;
	}

	l->key = key;
	li = leaf_info_new(plen);

	if (!li) {
		tnode_free((struct tnode *) l);
		*err = -ENOMEM;
		goto err;
	}

	fa_head = &li->falh;
	insert_leaf_info(&l->list, li);

	if (t->trie && n == NULL) {
		/* Case 2: n is NULL, and will just insert a new leaf */

		NODE_SET_PARENT(l, tp);

		cindex = tkey_extract_bits(key, tp->pos, tp->bits);
		put_child(t, (struct tnode *)tp, cindex, (struct node *)l);
	} else {
		/* Case 3: n is a LEAF or a TNODE and the key doesn't match. */
		/*
		 *  Add a new tnode here
		 *  first tnode need some special handling
		 */

		if (tp)
			pos = tp->pos+tp->bits;
		else
			pos = 0;

		if (n) {
			newpos = tkey_mismatch(key, pos, n->key);
			tn = tnode_new(n->key, newpos, 1);
		} else {
			newpos = 0;
			tn = tnode_new(key, newpos, 1); /* First tnode */
		}

		if (!tn) {
			free_leaf_info(li);
			tnode_free((struct tnode *) l);
			*err = -ENOMEM;
			goto err;
		}

		NODE_SET_PARENT(tn, tp);

		missbit = tkey_extract_bits(key, newpos, 1);
		put_child(t, tn, missbit, (struct node *)l);
		put_child(t, tn, 1-missbit, n);

		if (tp) {
			cindex = tkey_extract_bits(key, tp->pos, tp->bits);
			put_child(t, (struct tnode *)tp, cindex, (struct node *)tn);
		} else {
			rcu_assign_pointer(t->trie, (struct node *)tn); /* First tnode */
			tp = tn;
		}
	}

	if (tp && tp->pos + tp->bits > 32)
		printk(KERN_WARNING "fib_trie tp=%p pos=%d, bits=%d, key=%0x plen=%d\n",
		       tp, tp->pos, tp->bits, key, plen);

	/* Rebalance the trie */

	rcu_assign_pointer(t->trie, trie_rebalance(t, tp));
done:
	t->revision++;
err:
	return fa_head;
}

/*
 * Caller must hold RTNL.
 */
static int fn_trie_insert(struct fib_table *tb, struct fib_config *cfg)
{
	struct trie *t = (struct trie *) tb->tb_data;
	struct fib_alias *fa, *new_fa;
	struct list_head *fa_head = NULL;
	struct fib_info *fi;
	int plen = cfg->fc_dst_len;
	u8 tos = cfg->fc_tos;
	u32 key, mask;
	int err;
	struct leaf *l;

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

	if (fa && fa->fa_info->fib_priority == fi->fib_priority) {
		struct fib_alias *fa_orig;

		err = -EEXIST;
		if (cfg->fc_nlflags & NLM_F_EXCL)
			goto out;

		if (cfg->fc_nlflags & NLM_F_REPLACE) {
			struct fib_info *fi_drop;
			u8 state;

			err = -ENOBUFS;
			new_fa = kmem_cache_alloc(fn_alias_kmem, GFP_KERNEL);
			if (new_fa == NULL)
				goto out;

			fi_drop = fa->fa_info;
			new_fa->fa_tos = fa->fa_tos;
			new_fa->fa_info = fi;
			new_fa->fa_type = cfg->fc_type;
			new_fa->fa_scope = cfg->fc_scope;
			state = fa->fa_state;
			new_fa->fa_state &= ~FA_S_ACCESSED;

			list_replace_rcu(&fa->fa_list, &new_fa->fa_list);
			alias_free_mem_rcu(fa);

			fib_release_info(fi_drop);
			if (state & FA_S_ACCESSED)
				rt_cache_flush(-1);
			rtmsg_fib(RTM_NEWROUTE, htonl(key), new_fa, plen,
				tb->tb_id, &cfg->fc_nlinfo, NLM_F_REPLACE);

			goto succeeded;
		}
		/* Error if we find a perfect match which
		 * uses the same scope, type, and nexthop
		 * information.
		 */
		fa_orig = fa;
		list_for_each_entry(fa, fa_orig->fa_list.prev, fa_list) {
			if (fa->fa_tos != tos)
				break;
			if (fa->fa_info->fib_priority != fi->fib_priority)
				break;
			if (fa->fa_type == cfg->fc_type &&
			    fa->fa_scope == cfg->fc_scope &&
			    fa->fa_info == fi) {
				goto out;
			}
		}
		if (!(cfg->fc_nlflags & NLM_F_APPEND))
			fa = fa_orig;
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
	new_fa->fa_scope = cfg->fc_scope;
	new_fa->fa_state = 0;
	/*
	 * Insert new entry to the list.
	 */

	if (!fa_head) {
		err = 0;
		fa_head = fib_insert_node(t, &err, key, plen);
		if (err)
			goto out_free_new_fa;
	}

	list_add_tail_rcu(&new_fa->fa_list,
			  (fa ? &fa->fa_list : fa_head));

	rt_cache_flush(-1);
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
static inline int check_leaf(struct trie *t, struct leaf *l,
			     t_key key, int *plen, const struct flowi *flp,
			     struct fib_result *res)
{
	int err, i;
	__be32 mask;
	struct leaf_info *li;
	struct hlist_head *hhead = &l->list;
	struct hlist_node *node;

	hlist_for_each_entry_rcu(li, node, hhead, hlist) {
		i = li->plen;
		mask = inet_make_mask(i);
		if (l->key != (key & ntohl(mask)))
			continue;

		if ((err = fib_semantic_match(&li->falh, flp, res, htonl(l->key), mask, i)) <= 0) {
			*plen = i;
#ifdef CONFIG_IP_FIB_TRIE_STATS
			t->stats.semantic_match_passed++;
#endif
			return err;
		}
#ifdef CONFIG_IP_FIB_TRIE_STATS
		t->stats.semantic_match_miss++;
#endif
	}
	return 1;
}

static int
fn_trie_lookup(struct fib_table *tb, const struct flowi *flp, struct fib_result *res)
{
	struct trie *t = (struct trie *) tb->tb_data;
	int plen, ret = 0;
	struct node *n;
	struct tnode *pn;
	int pos, bits;
	t_key key = ntohl(flp->fl4_dst);
	int chopped_off;
	t_key cindex = 0;
	int current_prefix_length = KEYLENGTH;
	struct tnode *cn;
	t_key node_prefix, key_prefix, pref_mismatch;
	int mp;

	rcu_read_lock();

	n = rcu_dereference(t->trie);
	if (!n)
		goto failed;

#ifdef CONFIG_IP_FIB_TRIE_STATS
	t->stats.gets++;
#endif

	/* Just a leaf? */
	if (IS_LEAF(n)) {
		if ((ret = check_leaf(t, (struct leaf *)n, key, &plen, flp, res)) <= 0)
			goto found;
		goto failed;
	}
	pn = (struct tnode *) n;
	chopped_off = 0;

	while (pn) {
		pos = pn->pos;
		bits = pn->bits;

		if (!chopped_off)
			cindex = tkey_extract_bits(MASK_PFX(key, current_prefix_length), pos, bits);

		n = tnode_get_child(pn, cindex);

		if (n == NULL) {
#ifdef CONFIG_IP_FIB_TRIE_STATS
			t->stats.null_node_hit++;
#endif
			goto backtrace;
		}

		if (IS_LEAF(n)) {
			if ((ret = check_leaf(t, (struct leaf *)n, key, &plen, flp, res)) <= 0)
				goto found;
			else
				goto backtrace;
		}

#define HL_OPTIMIZE
#ifdef HL_OPTIMIZE
		cn = (struct tnode *)n;

		/*
		 * It's a tnode, and we can do some extra checks here if we
		 * like, to avoid descending into a dead-end branch.
		 * This tnode is in the parent's child array at index
		 * key[p_pos..p_pos+p_bits] but potentially with some bits
		 * chopped off, so in reality the index may be just a
		 * subprefix, padded with zero at the end.
		 * We can also take a look at any skipped bits in this
		 * tnode - everything up to p_pos is supposed to be ok,
		 * and the non-chopped bits of the index (se previous
		 * paragraph) are also guaranteed ok, but the rest is
		 * considered unknown.
		 *
		 * The skipped bits are key[pos+bits..cn->pos].
		 */

		/* If current_prefix_length < pos+bits, we are already doing
		 * actual prefix  matching, which means everything from
		 * pos+(bits-chopped_off) onward must be zero along some
		 * branch of this subtree - otherwise there is *no* valid
		 * prefix present. Here we can only check the skipped
		 * bits. Remember, since we have already indexed into the
		 * parent's child array, we know that the bits we chopped of
		 * *are* zero.
		 */

		/* NOTA BENE: CHECKING ONLY SKIPPED BITS FOR THE NEW NODE HERE */

		if (current_prefix_length < pos+bits) {
			if (tkey_extract_bits(cn->key, current_prefix_length,
						cn->pos - current_prefix_length) != 0 ||
			    !(cn->child[0]))
				goto backtrace;
		}

		/*
		 * If chopped_off=0, the index is fully validated and we
		 * only need to look at the skipped bits for this, the new,
		 * tnode. What we actually want to do is to find out if
		 * these skipped bits match our key perfectly, or if we will
		 * have to count on finding a matching prefix further down,
		 * because if we do, we would like to have some way of
		 * verifying the existence of such a prefix at this point.
		 */

		/* The only thing we can do at this point is to verify that
		 * any such matching prefix can indeed be a prefix to our
		 * key, and if the bits in the node we are inspecting that
		 * do not match our key are not ZERO, this cannot be true.
		 * Thus, find out where there is a mismatch (before cn->pos)
		 * and verify that all the mismatching bits are zero in the
		 * new tnode's key.
		 */

		/* Note: We aren't very concerned about the piece of the key
		 * that precede pn->pos+pn->bits, since these have already been
		 * checked. The bits after cn->pos aren't checked since these are
		 * by definition "unknown" at this point. Thus, what we want to
		 * see is if we are about to enter the "prefix matching" state,
		 * and in that case verify that the skipped bits that will prevail
		 * throughout this subtree are zero, as they have to be if we are
		 * to find a matching prefix.
		 */

		node_prefix = MASK_PFX(cn->key, cn->pos);
		key_prefix = MASK_PFX(key, cn->pos);
		pref_mismatch = key_prefix^node_prefix;
		mp = 0;

		/* In short: If skipped bits in this node do not match the search
		 * key, enter the "prefix matching" state.directly.
		 */
		if (pref_mismatch) {
			while (!(pref_mismatch & (1<<(KEYLENGTH-1)))) {
				mp++;
				pref_mismatch = pref_mismatch <<1;
			}
			key_prefix = tkey_extract_bits(cn->key, mp, cn->pos-mp);

			if (key_prefix != 0)
				goto backtrace;

			if (current_prefix_length >= cn->pos)
				current_prefix_length = mp;
		}
#endif
		pn = (struct tnode *)n; /* Descend */
		chopped_off = 0;
		continue;

backtrace:
		chopped_off++;

		/* As zero don't change the child key (cindex) */
		while ((chopped_off <= pn->bits) && !(cindex & (1<<(chopped_off-1))))
			chopped_off++;

		/* Decrease current_... with bits chopped off */
		if (current_prefix_length > pn->pos + pn->bits - chopped_off)
			current_prefix_length = pn->pos + pn->bits - chopped_off;

		/*
		 * Either we do the actual chop off according or if we have
		 * chopped off all bits in this tnode walk up to our parent.
		 */

		if (chopped_off <= pn->bits) {
			cindex &= ~(1 << (chopped_off-1));
		} else {
			if (NODE_PARENT(pn) == NULL)
				goto failed;

			/* Get Child's index */
			cindex = tkey_extract_bits(pn->key, NODE_PARENT(pn)->pos, NODE_PARENT(pn)->bits);
			pn = NODE_PARENT(pn);
			chopped_off = 0;

#ifdef CONFIG_IP_FIB_TRIE_STATS
			t->stats.backtrack++;
#endif
			goto backtrace;
		}
	}
failed:
	ret = 1;
found:
	rcu_read_unlock();
	return ret;
}

/* only called from updater side */
static int trie_leaf_remove(struct trie *t, t_key key)
{
	t_key cindex;
	struct tnode *tp = NULL;
	struct node *n = t->trie;
	struct leaf *l;

	pr_debug("entering trie_leaf_remove(%p)\n", n);

	/* Note that in the case skipped bits, those bits are *not* checked!
	 * When we finish this, we will have NULL or a T_LEAF, and the
	 * T_LEAF may or may not match our key.
	 */

	while (n != NULL && IS_TNODE(n)) {
		struct tnode *tn = (struct tnode *) n;
		check_tnode(tn);
		n = tnode_get_child(tn ,tkey_extract_bits(key, tn->pos, tn->bits));

		BUG_ON(n && NODE_PARENT(n) != tn);
	}
	l = (struct leaf *) n;

	if (!n || !tkey_equals(l->key, key))
		return 0;

	/*
	 * Key found.
	 * Remove the leaf and rebalance the tree
	 */

	t->revision++;
	t->size--;

	tp = NODE_PARENT(n);
	tnode_free((struct tnode *) n);

	if (tp) {
		cindex = tkey_extract_bits(key, tp->pos, tp->bits);
		put_child(t, (struct tnode *)tp, cindex, NULL);
		rcu_assign_pointer(t->trie, trie_rebalance(t, tp));
	} else
		rcu_assign_pointer(t->trie, NULL);

	return 1;
}

/*
 * Caller must hold RTNL.
 */
static int fn_trie_delete(struct fib_table *tb, struct fib_config *cfg)
{
	struct trie *t = (struct trie *) tb->tb_data;
	u32 key, mask;
	int plen = cfg->fc_dst_len;
	u8 tos = cfg->fc_tos;
	struct fib_alias *fa, *fa_to_delete;
	struct list_head *fa_head;
	struct leaf *l;
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

	fa_head = get_fa_head(l, plen);
	fa = fib_find_alias(fa_head, tos, 0);

	if (!fa)
		return -ESRCH;

	pr_debug("Deleting %08x/%d tos=%d t=%p\n", key, plen, tos, t);

	fa_to_delete = NULL;
	fa_head = fa->fa_list.prev;

	list_for_each_entry(fa, fa_head, fa_list) {
		struct fib_info *fi = fa->fa_info;

		if (fa->fa_tos != tos)
			break;

		if ((!cfg->fc_type || fa->fa_type == cfg->fc_type) &&
		    (cfg->fc_scope == RT_SCOPE_NOWHERE ||
		     fa->fa_scope == cfg->fc_scope) &&
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

	l = fib_find_node(t, key);
	li = find_leaf_info(l, plen);

	list_del_rcu(&fa->fa_list);

	if (list_empty(fa_head)) {
		hlist_del_rcu(&li->hlist);
		free_leaf_info(li);
	}

	if (hlist_empty(&l->list))
		trie_leaf_remove(t, key);

	if (fa->fa_state & FA_S_ACCESSED)
		rt_cache_flush(-1);

	fib_release_info(fa->fa_info);
	alias_free_mem_rcu(fa);
	return 0;
}

static int trie_flush_list(struct trie *t, struct list_head *head)
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

static int trie_flush_leaf(struct trie *t, struct leaf *l)
{
	int found = 0;
	struct hlist_head *lih = &l->list;
	struct hlist_node *node, *tmp;
	struct leaf_info *li = NULL;

	hlist_for_each_entry_safe(li, node, tmp, lih, hlist) {
		found += trie_flush_list(t, &li->falh);

		if (list_empty(&li->falh)) {
			hlist_del_rcu(&li->hlist);
			free_leaf_info(li);
		}
	}
	return found;
}

/* rcu_read_lock needs to be hold by caller from readside */

static struct leaf *nextleaf(struct trie *t, struct leaf *thisleaf)
{
	struct node *c = (struct node *) thisleaf;
	struct tnode *p;
	int idx;
	struct node *trie = rcu_dereference(t->trie);

	if (c == NULL) {
		if (trie == NULL)
			return NULL;

		if (IS_LEAF(trie))          /* trie w. just a leaf */
			return (struct leaf *) trie;

		p = (struct tnode*) trie;  /* Start */
	} else
		p = (struct tnode *) NODE_PARENT(c);

	while (p) {
		int pos, last;

		/*  Find the next child of the parent */
		if (c)
			pos = 1 + tkey_extract_bits(c->key, p->pos, p->bits);
		else
			pos = 0;

		last = 1 << p->bits;
		for (idx = pos; idx < last ; idx++) {
			c = rcu_dereference(p->child[idx]);

			if (!c)
				continue;

			/* Decend if tnode */
			while (IS_TNODE(c)) {
				p = (struct tnode *) c;
				idx = 0;

				/* Rightmost non-NULL branch */
				if (p && IS_TNODE(p))
					while (!(c = rcu_dereference(p->child[idx]))
					       && idx < (1<<p->bits)) idx++;

				/* Done with this tnode? */
				if (idx >= (1 << p->bits) || !c)
					goto up;
			}
			return (struct leaf *) c;
		}
up:
		/* No more children go up one step  */
		c = (struct node *) p;
		p = (struct tnode *) NODE_PARENT(p);
	}
	return NULL; /* Ready. Root of trie */
}

/*
 * Caller must hold RTNL.
 */
static int fn_trie_flush(struct fib_table *tb)
{
	struct trie *t = (struct trie *) tb->tb_data;
	struct leaf *ll = NULL, *l = NULL;
	int found = 0, h;

	t->revision++;

	for (h = 0; (l = nextleaf(t, l)) != NULL; h++) {
		found += trie_flush_leaf(t, l);

		if (ll && hlist_empty(&ll->list))
			trie_leaf_remove(t, ll->key);
		ll = l;
	}

	if (ll && hlist_empty(&ll->list))
		trie_leaf_remove(t, ll->key);

	pr_debug("trie_flush found=%d\n", found);
	return found;
}

static int trie_last_dflt = -1;

static void
fn_trie_select_default(struct fib_table *tb, const struct flowi *flp, struct fib_result *res)
{
	struct trie *t = (struct trie *) tb->tb_data;
	int order, last_idx;
	struct fib_info *fi = NULL;
	struct fib_info *last_resort;
	struct fib_alias *fa = NULL;
	struct list_head *fa_head;
	struct leaf *l;

	last_idx = -1;
	last_resort = NULL;
	order = -1;

	rcu_read_lock();

	l = fib_find_node(t, 0);
	if (!l)
		goto out;

	fa_head = get_fa_head(l, 0);
	if (!fa_head)
		goto out;

	if (list_empty(fa_head))
		goto out;

	list_for_each_entry_rcu(fa, fa_head, fa_list) {
		struct fib_info *next_fi = fa->fa_info;

		if (fa->fa_scope != res->scope ||
		    fa->fa_type != RTN_UNICAST)
			continue;

		if (next_fi->fib_priority > res->fi->fib_priority)
			break;
		if (!next_fi->fib_nh[0].nh_gw ||
		    next_fi->fib_nh[0].nh_scope != RT_SCOPE_LINK)
			continue;
		fa->fa_state |= FA_S_ACCESSED;

		if (fi == NULL) {
			if (next_fi != res->fi)
				break;
		} else if (!fib_detect_death(fi, order, &last_resort,
					     &last_idx, &trie_last_dflt)) {
			if (res->fi)
				fib_info_put(res->fi);
			res->fi = fi;
			atomic_inc(&fi->fib_clntref);
			trie_last_dflt = order;
			goto out;
		}
		fi = next_fi;
		order++;
	}
	if (order <= 0 || fi == NULL) {
		trie_last_dflt = -1;
		goto out;
	}

	if (!fib_detect_death(fi, order, &last_resort, &last_idx, &trie_last_dflt)) {
		if (res->fi)
			fib_info_put(res->fi);
		res->fi = fi;
		atomic_inc(&fi->fib_clntref);
		trie_last_dflt = order;
		goto out;
	}
	if (last_idx >= 0) {
		if (res->fi)
			fib_info_put(res->fi);
		res->fi = last_resort;
		if (last_resort)
			atomic_inc(&last_resort->fib_clntref);
	}
	trie_last_dflt = last_idx;
 out:;
	rcu_read_unlock();
}

static int fn_trie_dump_fa(t_key key, int plen, struct list_head *fah, struct fib_table *tb,
			   struct sk_buff *skb, struct netlink_callback *cb)
{
	int i, s_i;
	struct fib_alias *fa;

	__be32 xkey = htonl(key);

	s_i = cb->args[4];
	i = 0;

	/* rcu_read_lock is hold by caller */

	list_for_each_entry_rcu(fa, fah, fa_list) {
		if (i < s_i) {
			i++;
			continue;
		}
		BUG_ON(!fa->fa_info);

		if (fib_dump_info(skb, NETLINK_CB(cb->skb).pid,
				  cb->nlh->nlmsg_seq,
				  RTM_NEWROUTE,
				  tb->tb_id,
				  fa->fa_type,
				  fa->fa_scope,
				  xkey,
				  plen,
				  fa->fa_tos,
				  fa->fa_info, 0) < 0) {
			cb->args[4] = i;
			return -1;
		}
		i++;
	}
	cb->args[4] = i;
	return skb->len;
}

static int fn_trie_dump_plen(struct trie *t, int plen, struct fib_table *tb, struct sk_buff *skb,
			     struct netlink_callback *cb)
{
	int h, s_h;
	struct list_head *fa_head;
	struct leaf *l = NULL;

	s_h = cb->args[3];

	for (h = 0; (l = nextleaf(t, l)) != NULL; h++) {
		if (h < s_h)
			continue;
		if (h > s_h)
			memset(&cb->args[4], 0,
			       sizeof(cb->args) - 4*sizeof(cb->args[0]));

		fa_head = get_fa_head(l, plen);

		if (!fa_head)
			continue;

		if (list_empty(fa_head))
			continue;

		if (fn_trie_dump_fa(l->key, plen, fa_head, tb, skb, cb)<0) {
			cb->args[3] = h;
			return -1;
		}
	}
	cb->args[3] = h;
	return skb->len;
}

static int fn_trie_dump(struct fib_table *tb, struct sk_buff *skb, struct netlink_callback *cb)
{
	int m, s_m;
	struct trie *t = (struct trie *) tb->tb_data;

	s_m = cb->args[2];

	rcu_read_lock();
	for (m = 0; m <= 32; m++) {
		if (m < s_m)
			continue;
		if (m > s_m)
			memset(&cb->args[3], 0,
				sizeof(cb->args) - 3*sizeof(cb->args[0]));

		if (fn_trie_dump_plen(t, 32-m, tb, skb, cb)<0) {
			cb->args[2] = m;
			goto out;
		}
	}
	rcu_read_unlock();
	cb->args[2] = m;
	return skb->len;
out:
	rcu_read_unlock();
	return -1;
}

/* Fix more generic FIB names for init later */

#ifdef CONFIG_IP_MULTIPLE_TABLES
struct fib_table * fib_hash_init(u32 id)
#else
struct fib_table * __init fib_hash_init(u32 id)
#endif
{
	struct fib_table *tb;
	struct trie *t;

	if (fn_alias_kmem == NULL)
		fn_alias_kmem = kmem_cache_create("ip_fib_alias",
						  sizeof(struct fib_alias),
						  0, SLAB_HWCACHE_ALIGN,
						  NULL, NULL);

	tb = kmalloc(sizeof(struct fib_table) + sizeof(struct trie),
		     GFP_KERNEL);
	if (tb == NULL)
		return NULL;

	tb->tb_id = id;
	tb->tb_lookup = fn_trie_lookup;
	tb->tb_insert = fn_trie_insert;
	tb->tb_delete = fn_trie_delete;
	tb->tb_flush = fn_trie_flush;
	tb->tb_select_default = fn_trie_select_default;
	tb->tb_dump = fn_trie_dump;
	memset(tb->tb_data, 0, sizeof(struct trie));

	t = (struct trie *) tb->tb_data;

	trie_init(t);

	if (id == RT_TABLE_LOCAL)
		trie_local = t;
	else if (id == RT_TABLE_MAIN)
		trie_main = t;

	if (id == RT_TABLE_LOCAL)
		printk(KERN_INFO "IPv4 FIB: Using LC-trie version %s\n", VERSION);

	return tb;
}

#ifdef CONFIG_PROC_FS
/* Depth first Trie walk iterator */
struct fib_trie_iter {
	struct tnode *tnode;
	struct trie *trie;
	unsigned index;
	unsigned depth;
};

static struct node *fib_trie_get_next(struct fib_trie_iter *iter)
{
	struct tnode *tn = iter->tnode;
	unsigned cindex = iter->index;
	struct tnode *p;

	/* A single entry routing table */
	if (!tn)
		return NULL;

	pr_debug("get_next iter={node=%p index=%d depth=%d}\n",
		 iter->tnode, iter->index, iter->depth);
rescan:
	while (cindex < (1<<tn->bits)) {
		struct node *n = tnode_get_child(tn, cindex);

		if (n) {
			if (IS_LEAF(n)) {
				iter->tnode = tn;
				iter->index = cindex + 1;
			} else {
				/* push down one level */
				iter->tnode = (struct tnode *) n;
				iter->index = 0;
				++iter->depth;
			}
			return n;
		}

		++cindex;
	}

	/* Current node exhausted, pop back up */
	p = NODE_PARENT(tn);
	if (p) {
		cindex = tkey_extract_bits(tn->key, p->pos, p->bits)+1;
		tn = p;
		--iter->depth;
		goto rescan;
	}

	/* got root? */
	return NULL;
}

static struct node *fib_trie_get_first(struct fib_trie_iter *iter,
				       struct trie *t)
{
	struct node *n ;

	if (!t)
		return NULL;

	n = rcu_dereference(t->trie);

	if (!iter)
		return NULL;

	if (n) {
		if (IS_TNODE(n)) {
			iter->tnode = (struct tnode *) n;
			iter->trie = t;
			iter->index = 0;
			iter->depth = 1;
		} else {
			iter->tnode = NULL;
			iter->trie  = t;
			iter->index = 0;
			iter->depth = 0;
		}
		return n;
	}
	return NULL;
}

static void trie_collect_stats(struct trie *t, struct trie_stat *s)
{
	struct node *n;
	struct fib_trie_iter iter;

	memset(s, 0, sizeof(*s));

	rcu_read_lock();
	for (n = fib_trie_get_first(&iter, t); n;
	     n = fib_trie_get_next(&iter)) {
		if (IS_LEAF(n)) {
			s->leaves++;
			s->totdepth += iter.depth;
			if (iter.depth > s->maxdepth)
				s->maxdepth = iter.depth;
		} else {
			const struct tnode *tn = (const struct tnode *) n;
			int i;

			s->tnodes++;
			if (tn->bits < MAX_STAT_DEPTH)
				s->nodesizes[tn->bits]++;

			for (i = 0; i < (1<<tn->bits); i++)
				if (!tn->child[i])
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
	unsigned i, max, pointers, bytes, avdepth;

	if (stat->leaves)
		avdepth = stat->totdepth*100 / stat->leaves;
	else
		avdepth = 0;

	seq_printf(seq, "\tAver depth:     %d.%02d\n", avdepth / 100, avdepth % 100 );
	seq_printf(seq, "\tMax depth:      %u\n", stat->maxdepth);

	seq_printf(seq, "\tLeaves:         %u\n", stat->leaves);

	bytes = sizeof(struct leaf) * stat->leaves;
	seq_printf(seq, "\tInternal nodes: %d\n\t", stat->tnodes);
	bytes += sizeof(struct tnode) * stat->tnodes;

	max = MAX_STAT_DEPTH;
	while (max > 0 && stat->nodesizes[max-1] == 0)
		max--;

	pointers = 0;
	for (i = 1; i <= max; i++)
		if (stat->nodesizes[i] != 0) {
			seq_printf(seq, "  %d: %d",  i, stat->nodesizes[i]);
			pointers += (1<<i) * stat->nodesizes[i];
		}
	seq_putc(seq, '\n');
	seq_printf(seq, "\tPointers: %d\n", pointers);

	bytes += sizeof(struct node *) * pointers;
	seq_printf(seq, "Null ptrs: %d\n", stat->nullpointers);
	seq_printf(seq, "Total size: %d  kB\n", (bytes + 1023) / 1024);

#ifdef CONFIG_IP_FIB_TRIE_STATS
	seq_printf(seq, "Counters:\n---------\n");
	seq_printf(seq,"gets = %d\n", t->stats.gets);
	seq_printf(seq,"backtracks = %d\n", t->stats.backtrack);
	seq_printf(seq,"semantic match passed = %d\n", t->stats.semantic_match_passed);
	seq_printf(seq,"semantic match miss = %d\n", t->stats.semantic_match_miss);
	seq_printf(seq,"null node hit= %d\n", t->stats.null_node_hit);
	seq_printf(seq,"skipped node resize = %d\n", t->stats.resize_node_skipped);
#ifdef CLEAR_STATS
	memset(&(t->stats), 0, sizeof(t->stats));
#endif
#endif /*  CONFIG_IP_FIB_TRIE_STATS */
}

static int fib_triestat_seq_show(struct seq_file *seq, void *v)
{
	struct trie_stat *stat;

	stat = kmalloc(sizeof(*stat), GFP_KERNEL);
	if (!stat)
		return -ENOMEM;

	seq_printf(seq, "Basic info: size of leaf: %Zd bytes, size of tnode: %Zd bytes.\n",
		   sizeof(struct leaf), sizeof(struct tnode));

	if (trie_local) {
		seq_printf(seq, "Local:\n");
		trie_collect_stats(trie_local, stat);
		trie_show_stats(seq, stat);
	}

	if (trie_main) {
		seq_printf(seq, "Main:\n");
		trie_collect_stats(trie_main, stat);
		trie_show_stats(seq, stat);
	}
	kfree(stat);

	return 0;
}

static int fib_triestat_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, fib_triestat_seq_show, NULL);
}

static const struct file_operations fib_triestat_fops = {
	.owner	= THIS_MODULE,
	.open	= fib_triestat_seq_open,
	.read	= seq_read,
	.llseek	= seq_lseek,
	.release = single_release,
};

static struct node *fib_trie_get_idx(struct fib_trie_iter *iter,
				      loff_t pos)
{
	loff_t idx = 0;
	struct node *n;

	for (n = fib_trie_get_first(iter, trie_local);
	     n; ++idx, n = fib_trie_get_next(iter)) {
		if (pos == idx)
			return n;
	}

	for (n = fib_trie_get_first(iter, trie_main);
	     n; ++idx, n = fib_trie_get_next(iter)) {
		if (pos == idx)
			return n;
	}
	return NULL;
}

static void *fib_trie_seq_start(struct seq_file *seq, loff_t *pos)
{
	rcu_read_lock();
	if (*pos == 0)
		return SEQ_START_TOKEN;
	return fib_trie_get_idx(seq->private, *pos - 1);
}

static void *fib_trie_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct fib_trie_iter *iter = seq->private;
	void *l = v;

	++*pos;
	if (v == SEQ_START_TOKEN)
		return fib_trie_get_idx(iter, 0);

	v = fib_trie_get_next(iter);
	BUG_ON(v == l);
	if (v)
		return v;

	/* continue scan in next trie */
	if (iter->trie == trie_local)
		return fib_trie_get_first(iter, trie_main);

	return NULL;
}

static void fib_trie_seq_stop(struct seq_file *seq, void *v)
{
	rcu_read_unlock();
}

static void seq_indent(struct seq_file *seq, int n)
{
	while (n-- > 0) seq_puts(seq, "   ");
}

static inline const char *rtn_scope(enum rt_scope_t s)
{
	static char buf[32];

	switch (s) {
	case RT_SCOPE_UNIVERSE: return "universe";
	case RT_SCOPE_SITE:	return "site";
	case RT_SCOPE_LINK:	return "link";
	case RT_SCOPE_HOST:	return "host";
	case RT_SCOPE_NOWHERE:	return "nowhere";
	default:
		snprintf(buf, sizeof(buf), "scope=%d", s);
		return buf;
	}
}

static const char *rtn_type_names[__RTN_MAX] = {
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

static inline const char *rtn_type(unsigned t)
{
	static char buf[32];

	if (t < __RTN_MAX && rtn_type_names[t])
		return rtn_type_names[t];
	snprintf(buf, sizeof(buf), "type %d", t);
	return buf;
}

/* Pretty print the trie */
static int fib_trie_seq_show(struct seq_file *seq, void *v)
{
	const struct fib_trie_iter *iter = seq->private;
	struct node *n = v;

	if (v == SEQ_START_TOKEN)
		return 0;

	if (!NODE_PARENT(n)) {
		if (iter->trie == trie_local)
			seq_puts(seq, "<local>:\n");
		else
			seq_puts(seq, "<main>:\n");
	}

	if (IS_TNODE(n)) {
		struct tnode *tn = (struct tnode *) n;
		__be32 prf = htonl(MASK_PFX(tn->key, tn->pos));

		seq_indent(seq, iter->depth-1);
		seq_printf(seq, "  +-- %d.%d.%d.%d/%d %d %d %d\n",
			   NIPQUAD(prf), tn->pos, tn->bits, tn->full_children,
			   tn->empty_children);

	} else {
		struct leaf *l = (struct leaf *) n;
		int i;
		__be32 val = htonl(l->key);

		seq_indent(seq, iter->depth);
		seq_printf(seq, "  |-- %d.%d.%d.%d\n", NIPQUAD(val));
		for (i = 32; i >= 0; i--) {
			struct leaf_info *li = find_leaf_info(l, i);
			if (li) {
				struct fib_alias *fa;
				list_for_each_entry_rcu(fa, &li->falh, fa_list) {
					seq_indent(seq, iter->depth+1);
					seq_printf(seq, "  /%d %s %s", i,
						   rtn_scope(fa->fa_scope),
						   rtn_type(fa->fa_type));
					if (fa->fa_tos)
						seq_printf(seq, "tos =%d\n",
							   fa->fa_tos);
					seq_putc(seq, '\n');
				}
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
	struct seq_file *seq;
	int rc = -ENOMEM;
	struct fib_trie_iter *s = kmalloc(sizeof(*s), GFP_KERNEL);

	if (!s)
		goto out;

	rc = seq_open(file, &fib_trie_seq_ops);
	if (rc)
		goto out_kfree;

	seq	     = file->private_data;
	seq->private = s;
	memset(s, 0, sizeof(*s));
out:
	return rc;
out_kfree:
	kfree(s);
	goto out;
}

static const struct file_operations fib_trie_fops = {
	.owner  = THIS_MODULE,
	.open   = fib_trie_seq_open,
	.read   = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_private,
};

static unsigned fib_flag_trans(int type, __be32 mask, const struct fib_info *fi)
{
	static unsigned type2flags[RTN_MAX + 1] = {
		[7] = RTF_REJECT, [8] = RTF_REJECT,
	};
	unsigned flags = type2flags[type];

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
 * 	and needs to be same as fib_hash output to avoid breaking
 *	legacy utilities
 */
static int fib_route_seq_show(struct seq_file *seq, void *v)
{
	const struct fib_trie_iter *iter = seq->private;
	struct leaf *l = v;
	int i;
	char bf[128];

	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, "%-127s\n", "Iface\tDestination\tGateway "
			   "\tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU"
			   "\tWindow\tIRTT");
		return 0;
	}

	if (iter->trie == trie_local)
		return 0;
	if (IS_TNODE(l))
		return 0;

	for (i=32; i>=0; i--) {
		struct leaf_info *li = find_leaf_info(l, i);
		struct fib_alias *fa;
		__be32 mask, prefix;

		if (!li)
			continue;

		mask = inet_make_mask(li->plen);
		prefix = htonl(l->key);

		list_for_each_entry_rcu(fa, &li->falh, fa_list) {
			const struct fib_info *fi = fa->fa_info;
			unsigned flags = fib_flag_trans(fa->fa_type, mask, fi);

			if (fa->fa_type == RTN_BROADCAST
			    || fa->fa_type == RTN_MULTICAST)
				continue;

			if (fi)
				snprintf(bf, sizeof(bf),
					 "%s\t%08X\t%08X\t%04X\t%d\t%u\t%d\t%08X\t%d\t%u\t%u",
					 fi->fib_dev ? fi->fib_dev->name : "*",
					 prefix,
					 fi->fib_nh->nh_gw, flags, 0, 0,
					 fi->fib_priority,
					 mask,
					 (fi->fib_advmss ? fi->fib_advmss + 40 : 0),
					 fi->fib_window,
					 fi->fib_rtt >> 3);
			else
				snprintf(bf, sizeof(bf),
					 "*\t%08X\t%08X\t%04X\t%d\t%u\t%d\t%08X\t%d\t%u\t%u",
					 prefix, 0, flags, 0, 0, 0,
					 mask, 0, 0, 0);

			seq_printf(seq, "%-127s\n", bf);
		}
	}

	return 0;
}

static const struct seq_operations fib_route_seq_ops = {
	.start  = fib_trie_seq_start,
	.next   = fib_trie_seq_next,
	.stop   = fib_trie_seq_stop,
	.show   = fib_route_seq_show,
};

static int fib_route_seq_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int rc = -ENOMEM;
	struct fib_trie_iter *s = kmalloc(sizeof(*s), GFP_KERNEL);

	if (!s)
		goto out;

	rc = seq_open(file, &fib_route_seq_ops);
	if (rc)
		goto out_kfree;

	seq	     = file->private_data;
	seq->private = s;
	memset(s, 0, sizeof(*s));
out:
	return rc;
out_kfree:
	kfree(s);
	goto out;
}

static const struct file_operations fib_route_fops = {
	.owner  = THIS_MODULE,
	.open   = fib_route_seq_open,
	.read   = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_private,
};

int __init fib_proc_init(void)
{
	if (!proc_net_fops_create("fib_trie", S_IRUGO, &fib_trie_fops))
		goto out1;

	if (!proc_net_fops_create("fib_triestat", S_IRUGO, &fib_triestat_fops))
		goto out2;

	if (!proc_net_fops_create("route", S_IRUGO, &fib_route_fops))
		goto out3;

	return 0;

out3:
	proc_net_remove("fib_triestat");
out2:
	proc_net_remove("fib_trie");
out1:
	return -ENOMEM;
}

void __init fib_proc_exit(void)
{
	proc_net_remove("fib_trie");
	proc_net_remove("fib_triestat");
	proc_net_remove("route");
}

#endif /* CONFIG_PROC_FS */

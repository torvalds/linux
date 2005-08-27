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
 */

#define VERSION "0.325"

#include <linux/config.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/proc_fs.h>
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
#define MAX_CHILDS 16384

#define EXTRACT(p, n, str) ((str)<<(p)>>(32-(n)))
#define KEYLENGTH (8*sizeof(t_key))
#define MASK_PFX(k, l) (((l)==0)?0:(k >> (KEYLENGTH-l)) << (KEYLENGTH-l))
#define TKEY_GET_MASK(offset, bits) (((bits)==0)?0:((t_key)(-1) << (KEYLENGTH - bits) >> offset))

static DEFINE_RWLOCK(fib_lock);

typedef unsigned int t_key;

#define T_TNODE 0
#define T_LEAF  1
#define NODE_TYPE_MASK	0x1UL
#define NODE_PARENT(_node) \
	((struct tnode *)((_node)->_parent & ~NODE_TYPE_MASK))
#define NODE_SET_PARENT(_node, _ptr) \
	((_node)->_parent = (((unsigned long)(_ptr)) | \
                     ((_node)->_parent & NODE_TYPE_MASK)))
#define NODE_INIT_PARENT(_node, _type) \
	((_node)->_parent = (_type))
#define NODE_TYPE(_node) \
	((_node)->_parent & NODE_TYPE_MASK)

#define IS_TNODE(n) (!(n->_parent & T_LEAF))
#define IS_LEAF(n) (n->_parent & T_LEAF)

struct node {
        t_key key;
	unsigned long _parent;
};

struct leaf {
        t_key key;
	unsigned long _parent;
	struct hlist_head list;
};

struct leaf_info {
	struct hlist_node hlist;
	int plen;
	struct list_head falh;
};

struct tnode {
        t_key key;
	unsigned long _parent;
        unsigned short pos:5;        /* 2log(KEYLENGTH) bits needed */
        unsigned short bits:5;       /* 2log(KEYLENGTH) bits needed */
        unsigned short full_children;  /* KEYLENGTH bits needed */
        unsigned short empty_children; /* KEYLENGTH bits needed */
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
	unsigned int nodesizes[MAX_CHILDS];
};

struct trie {
        struct node *trie;
#ifdef CONFIG_IP_FIB_TRIE_STATS
	struct trie_use_stats stats;
#endif
        int size;
	unsigned int revision;
};

static int trie_debug = 0;

static int tnode_full(struct tnode *tn, struct node *n);
static void put_child(struct trie *t, struct tnode *tn, int i, struct node *n);
static void tnode_put_child_reorg(struct tnode *tn, int i, struct node *n, int wasfull);
static int tnode_child_length(struct tnode *tn);
static struct node *resize(struct trie *t, struct tnode *tn);
static struct tnode *inflate(struct trie *t, struct tnode *tn, int *err);
static struct tnode *halve(struct trie *t, struct tnode *tn, int *err);
static void tnode_free(struct tnode *tn);
static void trie_dump_seq(struct seq_file *seq, struct trie *t);
extern struct fib_alias *fib_find_alias(struct list_head *fah, u8 tos, u32 prio);
extern int fib_detect_death(struct fib_info *fi, int order,
                            struct fib_info **last_resort, int *last_idx, int *dflt);

extern void rtmsg_fib(int event, u32 key, struct fib_alias *fa, int z, int tb_id,
               struct nlmsghdr *n, struct netlink_skb_parms *req);

static kmem_cache_t *fn_alias_kmem;
static struct trie *trie_local = NULL, *trie_main = NULL;

static void trie_bug(char *err)
{
	printk("Trie Bug: %s\n", err);
	BUG();
}

static inline struct node *tnode_get_child(struct tnode *tn, int i)
{
        if (i >= 1<<tn->bits)
                trie_bug("tnode_get_child");

        return tn->child[i];
}

static inline int tnode_child_length(struct tnode *tn)
{
        return 1<<tn->bits;
}

/*
  _________________________________________________________________
  | i | i | i | i | i | i | i | N | N | N | S | S | S | S | S | C |
  ----------------------------------------------------------------
    0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15

  _________________________________________________________________
  | C | C | C | u | u | u | u | u | u | u | u | u | u | u | u | u |
  -----------------------------------------------------------------
   16  17  18  19  20  21  22  23  24  25  26  27  28  29  30  31

  tp->pos = 7
  tp->bits = 3
  n->pos = 15
  n->bits=4
  KEYLENGTH=32
*/

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

/* Candiate for fib_semantics */

static void fn_free_alias(struct fib_alias *fa)
{
	fib_release_info(fa->fa_info);
	kmem_cache_free(fn_alias_kmem, fa);
}

/*
  To understand this stuff, an understanding of keys and all their bits is 
  necessary. Every node in the trie has a key associated with it, but not 
  all of the bits in that key are significant.

  Consider a node 'n' and its parent 'tp'.

  If n is a leaf, every bit in its key is significant. Its presence is 
  necessitaded by path compression, since during a tree traversal (when 
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
  n->bits=4

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

static void check_tnode(struct tnode *tn)
{
	if (tn && tn->pos+tn->bits > 32) {
		printk("TNODE ERROR tn=%p, pos=%d, bits=%d\n", tn, tn->pos, tn->bits);
	}
}

static int halve_threshold = 25;
static int inflate_threshold = 50;

static struct leaf *leaf_new(void)
{
	struct leaf *l = kmalloc(sizeof(struct leaf),  GFP_KERNEL);
	if (l) {
		NODE_INIT_PARENT(l, T_LEAF);
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

static inline void free_leaf(struct leaf *l)
{
	kfree(l);
}

static inline void free_leaf_info(struct leaf_info *li)
{
	kfree(li);
}

static struct tnode *tnode_alloc(unsigned int size)
{
	if (size <= PAGE_SIZE) {
		return kmalloc(size, GFP_KERNEL);
	} else {
		return (struct tnode *)
			__get_free_pages(GFP_KERNEL, get_order(size));
	}
}

static void __tnode_free(struct tnode *tn)
{
	unsigned int size = sizeof(struct tnode) +
	                    (1<<tn->bits) * sizeof(struct node *);

	if (size <= PAGE_SIZE)
		kfree(tn);
	else
		free_pages((unsigned long)tn, get_order(size));
}

static struct tnode* tnode_new(t_key key, int pos, int bits)
{
	int nchildren = 1<<bits;
	int sz = sizeof(struct tnode) + nchildren * sizeof(struct node *);
	struct tnode *tn = tnode_alloc(sz);

	if (tn)  {
		memset(tn, 0, sz);
		NODE_INIT_PARENT(tn, T_TNODE);
		tn->pos = pos;
		tn->bits = bits;
		tn->key = key;
		tn->full_children = 0;
		tn->empty_children = 1<<bits;
	}

	if (trie_debug > 0)
		printk("AT %p s=%u %u\n", tn, (unsigned int) sizeof(struct tnode),
		       (unsigned int) (sizeof(struct node) * 1<<bits));
	return tn;
}

static void tnode_free(struct tnode *tn)
{
	if (!tn) {
		trie_bug("tnode_free\n");
	}
	if (IS_LEAF(tn)) {
		free_leaf((struct leaf *)tn);
		if (trie_debug > 0 )
			printk("FL %p \n", tn);
	}
	else if (IS_TNODE(tn)) {
		__tnode_free(tn);
		if (trie_debug > 0 )
			printk("FT %p \n", tn);
	}
	else {
		trie_bug("tnode_free\n");
	}
}

/*
 * Check whether a tnode 'n' is "full", i.e. it is an internal node
 * and no bits are skipped. See discussion in dyntree paper p. 6
 */

static inline int tnode_full(struct tnode *tn, struct node *n)
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
	struct node *chi;
	int isfull;

	if (i >= 1<<tn->bits) {
		printk("bits=%d, i=%d\n", tn->bits, i);
		trie_bug("tnode_put_child_reorg bits");
	}
	write_lock_bh(&fib_lock);
	chi = tn->child[i];

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

	tn->child[i] = n;
	write_unlock_bh(&fib_lock);
}

static struct node *resize(struct trie *t, struct tnode *tn)
{
	int i;
	int err = 0;

 	if (!tn)
		return NULL;

	if (trie_debug)
		printk("In tnode_resize %p inflate_threshold=%d threshold=%d\n",
		      tn, inflate_threshold, halve_threshold);

	/* No children */
	if (tn->empty_children == tnode_child_length(tn)) {
		tnode_free(tn);
		return NULL;
	}
	/* One child */
	if (tn->empty_children == tnode_child_length(tn) - 1)
		for (i = 0; i < tnode_child_length(tn); i++) {

			write_lock_bh(&fib_lock);
			if (tn->child[i] != NULL) {

				/* compress one level */
				struct node *n = tn->child[i];
				if (n)
					NODE_INIT_PARENT(n, NODE_TYPE(n));

				write_unlock_bh(&fib_lock);
				tnode_free(tn);
				return n;
			}
			write_unlock_bh(&fib_lock);
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
	 *    tn->full_children ) >= inflate_threshold * new_child_length
	 *
	 * expand new_child_length:
	 * 100 * (tnode_child_length(tn) - tn->empty_children +
	 *    tn->full_children ) >=
	 *      inflate_threshold * tnode_child_length(tn) * 2
	 *
	 * shorten again:
	 * 50 * (tn->full_children + tnode_child_length(tn) -
	 *    tn->empty_children ) >= inflate_threshold *
	 *    tnode_child_length(tn)
	 *
	 */

	check_tnode(tn);

	err = 0;
	while ((tn->full_children > 0 &&
	       50 * (tn->full_children + tnode_child_length(tn) - tn->empty_children) >=
				inflate_threshold * tnode_child_length(tn))) {

		tn = inflate(t, tn, &err);

		if (err) {
#ifdef CONFIG_IP_FIB_TRIE_STATS
			t->stats.resize_node_skipped++;
#endif
			break;
		}
	}

	check_tnode(tn);

	/*
	 * Halve as long as the number of empty children in this
	 * node is above threshold.
	 */

	err = 0;
	while (tn->bits > 1 &&
	       100 * (tnode_child_length(tn) - tn->empty_children) <
	       halve_threshold * tnode_child_length(tn)) {

		tn = halve(t, tn, &err);

		if (err) {
#ifdef CONFIG_IP_FIB_TRIE_STATS
			t->stats.resize_node_skipped++;
#endif
			break;
		}
	}


	/* Only one child remains */

	if (tn->empty_children == tnode_child_length(tn) - 1)
		for (i = 0; i < tnode_child_length(tn); i++) {
		
			write_lock_bh(&fib_lock);
			if (tn->child[i] != NULL) {
				/* compress one level */
				struct node *n = tn->child[i];

				if (n)
					NODE_INIT_PARENT(n, NODE_TYPE(n));

				write_unlock_bh(&fib_lock);
				tnode_free(tn);
				return n;
			}
			write_unlock_bh(&fib_lock);
		}

	return (struct node *) tn;
}

static struct tnode *inflate(struct trie *t, struct tnode *tn, int *err)
{
	struct tnode *inode;
	struct tnode *oldtnode = tn;
	int olen = tnode_child_length(tn);
	int i;

  	if (trie_debug)
		printk("In inflate\n");

	tn = tnode_new(oldtnode->key, oldtnode->pos, oldtnode->bits + 1);

	if (!tn) {
		*err = -ENOMEM;
		return oldtnode;
	}

	/*
	 * Preallocate and store tnodes before the actual work so we
	 * don't get into an inconsistent state if memory allocation
	 * fails. In case of failure we return the oldnode and  inflate
	 * of tnode is ignored.
	 */
		
	for(i = 0; i < olen; i++) {
		struct tnode *inode = (struct tnode *) tnode_get_child(oldtnode, i);

		if (inode &&
		    IS_TNODE(inode) &&
		    inode->pos == oldtnode->pos + oldtnode->bits &&
		    inode->bits > 1) {
			struct tnode *left, *right;

			t_key m = TKEY_GET_MASK(inode->pos, 1);

			left = tnode_new(inode->key&(~m), inode->pos + 1,
					 inode->bits - 1);

			if (!left) {
				*err = -ENOMEM;
				break;
			}
		
			right = tnode_new(inode->key|m, inode->pos + 1,
					  inode->bits - 1);

			if (!right) {
				*err = -ENOMEM;
				break;
			}

			put_child(t, tn, 2*i, (struct node *) left);
			put_child(t, tn, 2*i+1, (struct node *) right);
		}
	}

	if (*err) {
		int size = tnode_child_length(tn);
		int j;

		for(j = 0; j < size; j++)
			if (tn->child[j])
				tnode_free((struct tnode *)tn->child[j]);

		tnode_free(tn);
	
		*err = -ENOMEM;
		return oldtnode;
	}

	for(i = 0; i < olen; i++) {
		struct node *node = tnode_get_child(oldtnode, i);

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
		}

			/* An internal node with more than two children */
		else {
			struct tnode *left, *right;
			int size, j;

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

			if (!left)
				BUG();

			right = (struct tnode *) tnode_get_child(tn, 2*i+1);
			put_child(t, tn, 2*i+1, NULL);

			if (!right)
				BUG();

			size = tnode_child_length(left);
			for(j = 0; j < size; j++) {
				put_child(t, left, j, inode->child[j]);
				put_child(t, right, j, inode->child[j + size]);
			}
			put_child(t, tn, 2*i, resize(t, left));
			put_child(t, tn, 2*i+1, resize(t, right));

			tnode_free(inode);
		}
	}
	tnode_free(oldtnode);
	return tn;
}

static struct tnode *halve(struct trie *t, struct tnode *tn, int *err)
{
	struct tnode *oldtnode = tn;
	struct node *left, *right;
	int i;
	int olen = tnode_child_length(tn);

	if (trie_debug) printk("In halve\n");

	tn = tnode_new(oldtnode->key, oldtnode->pos, oldtnode->bits - 1);

	if (!tn) {
		*err = -ENOMEM;
		return oldtnode;
	}

	/*
	 * Preallocate and store tnodes before the actual work so we
	 * don't get into an inconsistent state if memory allocation
	 * fails. In case of failure we return the oldnode and halve
	 * of tnode is ignored.
	 */

	for(i = 0; i < olen; i += 2) {
		left = tnode_get_child(oldtnode, i);
		right = tnode_get_child(oldtnode, i+1);

		/* Two nonempty children */
		if (left && right)  {
			struct tnode *newBinNode =
				tnode_new(left->key, tn->pos + tn->bits, 1);

			if (!newBinNode) {
				*err = -ENOMEM;
				break;
			}
			put_child(t, tn, i/2, (struct node *)newBinNode);
		}
	}

	if (*err) {
		int size = tnode_child_length(tn);
		int j;

		for(j = 0; j < size; j++)
			if (tn->child[j])
				tnode_free((struct tnode *)tn->child[j]);

		tnode_free(tn);
	
		*err = -ENOMEM;
		return oldtnode;
	}

	for(i = 0; i < olen; i += 2) {
		left = tnode_get_child(oldtnode, i);
		right = tnode_get_child(oldtnode, i+1);

		/* At least one of the children is empty */
		if (left == NULL) {
			if (right == NULL)    /* Both are empty */
				continue;
			put_child(t, tn, i/2, right);
		} else if (right == NULL)
			put_child(t, tn, i/2, left);

		/* Two nonempty children */
		else {
			struct tnode *newBinNode =
				(struct tnode *) tnode_get_child(tn, i/2);
			put_child(t, tn, i/2, NULL);

			if (!newBinNode)
				BUG();

			put_child(t, newBinNode, 0, left);
			put_child(t, newBinNode, 1, right);
			put_child(t, tn, i/2, resize(t, newBinNode));
		}
	}
	tnode_free(oldtnode);
	return tn;
}

static void *trie_init(struct trie *t)
{
	if (t) {
		t->size = 0;
		t->trie = NULL;
		t->revision = 0;
#ifdef CONFIG_IP_FIB_TRIE_STATS
       		memset(&t->stats, 0, sizeof(struct trie_use_stats));
#endif
	}
	return t;
}

static struct leaf_info *find_leaf_info(struct hlist_head *head, int plen)
{
	struct hlist_node *node;
	struct leaf_info *li;

	hlist_for_each_entry(li, node, head, hlist) {
		if (li->plen == plen)
			return li;
	}
	return NULL;
}

static inline struct list_head * get_fa_head(struct leaf *l, int plen)
{
	struct list_head *fa_head = NULL;
	struct leaf_info *li = find_leaf_info(&l->list, plen);

	if (li)
		fa_head = &li->falh;

	return fa_head;
}

static void insert_leaf_info(struct hlist_head *head, struct leaf_info *new)
{
	struct leaf_info *li = NULL, *last = NULL;
	struct hlist_node *node, *tmp;

	write_lock_bh(&fib_lock);

	if (hlist_empty(head))
		hlist_add_head(&new->hlist, head);
	else {
		hlist_for_each_entry_safe(li, node, tmp, head, hlist) {
		
			if (new->plen > li->plen)
				break;
		
			last = li;
		}
		if (last)
			hlist_add_after(&last->hlist, &new->hlist);
		else
			hlist_add_before(&new->hlist, &li->hlist);
	}
	write_unlock_bh(&fib_lock);
}

static struct leaf *
fib_find_node(struct trie *t, u32 key)
{
	int pos;
	struct tnode *tn;
	struct node *n;

	pos = 0;
	n = t->trie;

	while (n != NULL &&  NODE_TYPE(n) == T_TNODE) {
		tn = (struct tnode *) n;
		
		check_tnode(tn);
		
		if (tkey_sub_equals(tn->key, pos, tn->pos-pos, key)) {
			pos=tn->pos + tn->bits;
			n = tnode_get_child(tn, tkey_extract_bits(key, tn->pos, tn->bits));
		}
		else
			break;
	}
	/* Case we have found a leaf. Compare prefixes */

	if (n != NULL && IS_LEAF(n) && tkey_equals(key, n->key)) {
		struct leaf *l = (struct leaf *) n;
		return l;
	}
	return NULL;
}

static struct node *trie_rebalance(struct trie *t, struct tnode *tn)
{
	int i = 0;
	int wasfull;
	t_key cindex, key;
	struct tnode *tp = NULL;

	if (!tn)
		BUG();

	key = tn->key;
	i = 0;

	while (tn != NULL && NODE_PARENT(tn) != NULL) {

		if (i > 10) {
			printk("Rebalance tn=%p \n", tn);
			if (tn) 		printk("tn->parent=%p \n", NODE_PARENT(tn));
		
			printk("Rebalance tp=%p \n", tp);
			if (tp) 		printk("tp->parent=%p \n", NODE_PARENT(tp));
		}

		if (i > 12) BUG();
		i++;

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
			pos=tn->pos + tn->bits;
			n = tnode_get_child(tn, tkey_extract_bits(key, tn->pos, tn->bits));

			if (n && NODE_PARENT(n) != tn) {
				printk("BUG tn=%p, n->parent=%p\n", tn, NODE_PARENT(n));
				BUG();
			}
		}
		else
			break;
	}

	/*
	 * n  ----> NULL, LEAF or TNODE
	 *
	 * tp is n's (parent) ----> NULL or TNODE
	 */

	if (tp && IS_LEAF(tp))
		BUG();


	/* Case 1: n is a leaf. Compare prefixes */

	if (n != NULL && IS_LEAF(n) && tkey_equals(key, n->key)) {
		struct leaf *l = ( struct leaf *)  n;
	
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

	/* Case 2: n is NULL, and will just insert a new leaf */
	if (t->trie && n == NULL) {

		NODE_SET_PARENT(l, tp);
	
		if (!tp)
			BUG();

		else {
			cindex = tkey_extract_bits(key, tp->pos, tp->bits);
			put_child(t, (struct tnode *)tp, cindex, (struct node *)l);
		}
	}
	/* Case 3: n is a LEAF or a TNODE and the key doesn't match. */
	else {
		/*
		 *  Add a new tnode here
		 *  first tnode need some special handling
		 */

		if (tp)
			pos=tp->pos+tp->bits;
		else
			pos=0;
		if (n) {
			newpos = tkey_mismatch(key, pos, n->key);
			tn = tnode_new(n->key, newpos, 1);
		}
		else {
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

		missbit=tkey_extract_bits(key, newpos, 1);
		put_child(t, tn, missbit, (struct node *)l);
		put_child(t, tn, 1-missbit, n);

		if (tp) {
			cindex = tkey_extract_bits(key, tp->pos, tp->bits);
			put_child(t, (struct tnode *)tp, cindex, (struct node *)tn);
		}
		else {
			t->trie = (struct node*) tn; /* First tnode */
			tp = tn;
		}
	}
	if (tp && tp->pos+tp->bits > 32) {
		printk("ERROR tp=%p pos=%d, bits=%d, key=%0x plen=%d\n",
		       tp, tp->pos, tp->bits, key, plen);
	}
	/* Rebalance the trie */
	t->trie = trie_rebalance(t, tp);
done:
	t->revision++;
err:;
	return fa_head;
}

static int
fn_trie_insert(struct fib_table *tb, struct rtmsg *r, struct kern_rta *rta,
	       struct nlmsghdr *nlhdr, struct netlink_skb_parms *req)
{
	struct trie *t = (struct trie *) tb->tb_data;
	struct fib_alias *fa, *new_fa;
	struct list_head *fa_head = NULL;
	struct fib_info *fi;
	int plen = r->rtm_dst_len;
	int type = r->rtm_type;
	u8 tos = r->rtm_tos;
	u32 key, mask;
	int err;
	struct leaf *l;

	if (plen > 32)
		return -EINVAL;

	key = 0;
	if (rta->rta_dst)
		memcpy(&key, rta->rta_dst, 4);

	key = ntohl(key);

	if (trie_debug)
		printk("Insert table=%d %08x/%d\n", tb->tb_id, key, plen);

	mask = ntohl( inet_make_mask(plen) );

	if (key & ~mask)
		return -EINVAL;

	key = key & mask;

	if  ((fi = fib_create_info(r, rta, nlhdr, &err)) == NULL)
		goto err;

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

	if (fa &&
	    fa->fa_info->fib_priority == fi->fib_priority) {
		struct fib_alias *fa_orig;

		err = -EEXIST;
		if (nlhdr->nlmsg_flags & NLM_F_EXCL)
			goto out;

		if (nlhdr->nlmsg_flags & NLM_F_REPLACE) {
			struct fib_info *fi_drop;
			u8 state;

			write_lock_bh(&fib_lock);

			fi_drop = fa->fa_info;
			fa->fa_info = fi;
			fa->fa_type = type;
			fa->fa_scope = r->rtm_scope;
			state = fa->fa_state;
			fa->fa_state &= ~FA_S_ACCESSED;

			write_unlock_bh(&fib_lock);

			fib_release_info(fi_drop);
			if (state & FA_S_ACCESSED)
			  rt_cache_flush(-1);

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
			if (fa->fa_type == type &&
			    fa->fa_scope == r->rtm_scope &&
			    fa->fa_info == fi) {
				goto out;
			}
		}
		if (!(nlhdr->nlmsg_flags & NLM_F_APPEND))
			fa = fa_orig;
	}
	err = -ENOENT;
	if (!(nlhdr->nlmsg_flags&NLM_F_CREATE))
		goto out;

	err = -ENOBUFS;
	new_fa = kmem_cache_alloc(fn_alias_kmem, SLAB_KERNEL);
	if (new_fa == NULL)
		goto out;

	new_fa->fa_info = fi;
	new_fa->fa_tos = tos;
	new_fa->fa_type = type;
	new_fa->fa_scope = r->rtm_scope;
	new_fa->fa_state = 0;
#if 0
	new_fa->dst = NULL;
#endif
	/*
	 * Insert new entry to the list.
	 */

	if (!fa_head) {
		fa_head = fib_insert_node(t, &err, key, plen);
		err = 0;
		if (err)
			goto out_free_new_fa;
	}

	write_lock_bh(&fib_lock);

	list_add_tail(&new_fa->fa_list,
		 (fa ? &fa->fa_list : fa_head));

	write_unlock_bh(&fib_lock);

	rt_cache_flush(-1);
	rtmsg_fib(RTM_NEWROUTE, htonl(key), new_fa, plen, tb->tb_id, nlhdr, req);
succeeded:
	return 0;

out_free_new_fa:
	kmem_cache_free(fn_alias_kmem, new_fa);
out:
	fib_release_info(fi);
err:;
	return err;
}

static inline int check_leaf(struct trie *t, struct leaf *l,  t_key key, int *plen, const struct flowi *flp,
			     struct fib_result *res)
{
	int err, i;
	t_key mask;
	struct leaf_info *li;
	struct hlist_head *hhead = &l->list;
	struct hlist_node *node;

	hlist_for_each_entry(li, node, hhead, hlist) {

		i = li->plen;
		mask = ntohl(inet_make_mask(i));
		if (l->key != (key & mask))
			continue;

		if ((err = fib_semantic_match(&li->falh, flp, res, l->key, mask, i)) <= 0) {
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
	t_key key=ntohl(flp->fl4_dst);
	int chopped_off;
	t_key cindex = 0;
	int current_prefix_length = KEYLENGTH;
	n = t->trie;

	read_lock(&fib_lock);
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

		if (IS_TNODE(n)) {
#define HL_OPTIMIZE
#ifdef HL_OPTIMIZE
			struct tnode *cn = (struct tnode *)n;
			t_key node_prefix, key_prefix, pref_mismatch;
			int mp;

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
					current_prefix_length=mp;
		       }
#endif
		       pn = (struct tnode *)n; /* Descend */
		       chopped_off = 0;
		       continue;
		}
		if (IS_LEAF(n)) {
			if ((ret = check_leaf(t, (struct leaf *)n, key, &plen, flp, res)) <= 0)
				goto found;
	       }
backtrace:
		chopped_off++;

		/* As zero don't change the child key (cindex) */
		while ((chopped_off <= pn->bits) && !(cindex & (1<<(chopped_off-1)))) {
			chopped_off++;
		}

		/* Decrease current_... with bits chopped off */
		if (current_prefix_length > pn->pos + pn->bits - chopped_off)
			current_prefix_length = pn->pos + pn->bits - chopped_off;
	
		/*
		 * Either we do the actual chop off according or if we have
		 * chopped off all bits in this tnode walk up to our parent.
		 */

		if (chopped_off <= pn->bits)
			cindex &= ~(1 << (chopped_off-1));
		else {
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
	read_unlock(&fib_lock);
	return ret;
}

static int trie_leaf_remove(struct trie *t, t_key key)
{
	t_key cindex;
	struct tnode *tp = NULL;
	struct node *n = t->trie;
	struct leaf *l;

	if (trie_debug)
		printk("entering trie_leaf_remove(%p)\n", n);

	/* Note that in the case skipped bits, those bits are *not* checked!
	 * When we finish this, we will have NULL or a T_LEAF, and the
	 * T_LEAF may or may not match our key.
	 */

        while (n != NULL && IS_TNODE(n)) {
		struct tnode *tn = (struct tnode *) n;
		check_tnode(tn);
		n = tnode_get_child(tn ,tkey_extract_bits(key, tn->pos, tn->bits));

			if (n && NODE_PARENT(n) != tn) {
				printk("BUG tn=%p, n->parent=%p\n", tn, NODE_PARENT(n));
				BUG();
			}
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
		t->trie = trie_rebalance(t, tp);
	}
	else
		t->trie = NULL;

	return 1;
}

static int
fn_trie_delete(struct fib_table *tb, struct rtmsg *r, struct kern_rta *rta,
	       struct nlmsghdr *nlhdr, struct netlink_skb_parms *req)
{
	struct trie *t = (struct trie *) tb->tb_data;
	u32 key, mask;
	int plen = r->rtm_dst_len;
	u8 tos = r->rtm_tos;
	struct fib_alias *fa, *fa_to_delete;
	struct list_head *fa_head;
	struct leaf *l;

	if (plen > 32)
		return -EINVAL;

	key = 0;
	if (rta->rta_dst)
		memcpy(&key, rta->rta_dst, 4);

	key = ntohl(key);
	mask = ntohl( inet_make_mask(plen) );

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

	if (trie_debug)
		printk("Deleting %08x/%d tos=%d t=%p\n", key, plen, tos, t);

	fa_to_delete = NULL;
	fa_head = fa->fa_list.prev;
	list_for_each_entry(fa, fa_head, fa_list) {
		struct fib_info *fi = fa->fa_info;

		if (fa->fa_tos != tos)
			break;

		if ((!r->rtm_type ||
		     fa->fa_type == r->rtm_type) &&
		    (r->rtm_scope == RT_SCOPE_NOWHERE ||
		     fa->fa_scope == r->rtm_scope) &&
		    (!r->rtm_protocol ||
		     fi->fib_protocol == r->rtm_protocol) &&
		    fib_nh_match(r, nlhdr, rta, fi) == 0) {
			fa_to_delete = fa;
			break;
		}
	}

	if (fa_to_delete) {
		int kill_li = 0;
		struct leaf_info *li;

		fa = fa_to_delete;
		rtmsg_fib(RTM_DELROUTE, htonl(key), fa, plen, tb->tb_id, nlhdr, req);

		l = fib_find_node(t, key);
		li = find_leaf_info(&l->list, plen);

		write_lock_bh(&fib_lock);

		list_del(&fa->fa_list);

		if (list_empty(fa_head)) {
			hlist_del(&li->hlist);
			kill_li = 1;
		}
		write_unlock_bh(&fib_lock);
	
		if (kill_li)
			free_leaf_info(li);

		if (hlist_empty(&l->list))
			trie_leaf_remove(t, key);

		if (fa->fa_state & FA_S_ACCESSED)
			rt_cache_flush(-1);

		fn_free_alias(fa);
		return 0;
	}
	return -ESRCH;
}

static int trie_flush_list(struct trie *t, struct list_head *head)
{
	struct fib_alias *fa, *fa_node;
	int found = 0;

	list_for_each_entry_safe(fa, fa_node, head, fa_list) {
		struct fib_info *fi = fa->fa_info;
	
		if (fi && (fi->fib_flags&RTNH_F_DEAD)) {

 			write_lock_bh(&fib_lock);
			list_del(&fa->fa_list);
			write_unlock_bh(&fib_lock);

			fn_free_alias(fa);
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

 			write_lock_bh(&fib_lock);
			hlist_del(&li->hlist);
			write_unlock_bh(&fib_lock);

			free_leaf_info(li);
		}
	}
	return found;
}

static struct leaf *nextleaf(struct trie *t, struct leaf *thisleaf)
{
	struct node *c = (struct node *) thisleaf;
	struct tnode *p;
	int idx;

	if (c == NULL) {
		if (t->trie == NULL)
			return NULL;

		if (IS_LEAF(t->trie))          /* trie w. just a leaf */
			return (struct leaf *) t->trie;

		p = (struct tnode*) t->trie;  /* Start */
	}
	else
		p = (struct tnode *) NODE_PARENT(c);

	while (p) {
		int pos, last;

		/*  Find the next child of the parent */
		if (c)
			pos = 1 + tkey_extract_bits(c->key, p->pos, p->bits);
		else
			pos = 0;

		last = 1 << p->bits;
		for(idx = pos; idx < last ; idx++) {
			if (p->child[idx]) {

				/* Decend if tnode */

				while (IS_TNODE(p->child[idx])) {
					p = (struct tnode*) p->child[idx];
					idx = 0;
				
					/* Rightmost non-NULL branch */
					if (p && IS_TNODE(p))
						while (p->child[idx] == NULL && idx < (1 << p->bits)) idx++;

					/* Done with this tnode? */
					if (idx >= (1 << p->bits) || p->child[idx] == NULL )
						goto up;
				}
				return (struct leaf*) p->child[idx];
			}
		}
up:
		/* No more children go up one step  */
		c = (struct node*) p;
		p = (struct tnode *) NODE_PARENT(p);
	}
	return NULL; /* Ready. Root of trie */
}

static int fn_trie_flush(struct fib_table *tb)
{
	struct trie *t = (struct trie *) tb->tb_data;
	struct leaf *ll = NULL, *l = NULL;
	int found = 0, h;

	t->revision++;

	for (h=0; (l = nextleaf(t, l)) != NULL; h++) {
		found += trie_flush_leaf(t, l);

		if (ll && hlist_empty(&ll->list))
			trie_leaf_remove(t, ll->key);
		ll = l;
	}

	if (ll && hlist_empty(&ll->list))
		trie_leaf_remove(t, ll->key);

	if (trie_debug)
		printk("trie_flush found=%d\n", found);
	return found;
}

static int trie_last_dflt=-1;

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

	read_lock(&fib_lock);

	l = fib_find_node(t, 0);
	if (!l)
		goto out;

	fa_head = get_fa_head(l, 0);
	if (!fa_head)
		goto out;

	if (list_empty(fa_head))
		goto out;

	list_for_each_entry(fa, fa_head, fa_list) {
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
	read_unlock(&fib_lock);
}

static int fn_trie_dump_fa(t_key key, int plen, struct list_head *fah, struct fib_table *tb,
			   struct sk_buff *skb, struct netlink_callback *cb)
{
	int i, s_i;
	struct fib_alias *fa;

	u32 xkey=htonl(key);

	s_i=cb->args[3];
	i = 0;

	list_for_each_entry(fa, fah, fa_list) {
		if (i < s_i) {
			i++;
			continue;
		}
		if (fa->fa_info->fib_nh == NULL) {
			printk("Trie error _fib_nh=NULL in fa[%d] k=%08x plen=%d\n", i, key, plen);
			i++;
			continue;
		}
		if (fa->fa_info == NULL) {
			printk("Trie error fa_info=NULL in fa[%d] k=%08x plen=%d\n", i, key, plen);
			i++;
			continue;
		}

		if (fib_dump_info(skb, NETLINK_CB(cb->skb).pid,
				  cb->nlh->nlmsg_seq,
				  RTM_NEWROUTE,
				  tb->tb_id,
				  fa->fa_type,
				  fa->fa_scope,
				  &xkey,
				  plen,
				  fa->fa_tos,
				  fa->fa_info, 0) < 0) {
			cb->args[3] = i;
			return -1;
			}
		i++;
	}
	cb->args[3]=i;
	return skb->len;
}

static int fn_trie_dump_plen(struct trie *t, int plen, struct fib_table *tb, struct sk_buff *skb,
			     struct netlink_callback *cb)
{
	int h, s_h;
	struct list_head *fa_head;
	struct leaf *l = NULL;
	s_h=cb->args[2];

	for (h=0; (l = nextleaf(t, l)) != NULL; h++) {

		if (h < s_h)
			continue;
		if (h > s_h)
			memset(&cb->args[3], 0,
			       sizeof(cb->args) - 3*sizeof(cb->args[0]));

		fa_head = get_fa_head(l, plen);
	
		if (!fa_head)
			continue;

		if (list_empty(fa_head))
			continue;

		if (fn_trie_dump_fa(l->key, plen, fa_head, tb, skb, cb)<0) {
			cb->args[2]=h;
			return -1;
		}
	}
	cb->args[2]=h;
	return skb->len;
}

static int fn_trie_dump(struct fib_table *tb, struct sk_buff *skb, struct netlink_callback *cb)
{
	int m, s_m;
	struct trie *t = (struct trie *) tb->tb_data;

	s_m = cb->args[1];

	read_lock(&fib_lock);
	for (m=0; m<=32; m++) {

		if (m < s_m)
			continue;
		if (m > s_m)
			memset(&cb->args[2], 0,
			       sizeof(cb->args) - 2*sizeof(cb->args[0]));

		if (fn_trie_dump_plen(t, 32-m, tb, skb, cb)<0) {
			cb->args[1] = m;
			goto out;
		}
	}
	read_unlock(&fib_lock);
	cb->args[1] = m;
	return skb->len;
 out:
	read_unlock(&fib_lock);
	return -1;
}

/* Fix more generic FIB names for init later */

#ifdef CONFIG_IP_MULTIPLE_TABLES
struct fib_table * fib_hash_init(int id)
#else
struct fib_table * __init fib_hash_init(int id)
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
		printk("IPv4 FIB: Using LC-trie version %s\n", VERSION);

	return tb;
}

/* Trie dump functions */

static void putspace_seq(struct seq_file *seq, int n)
{
	while (n--) seq_printf(seq, " ");
}

static void printbin_seq(struct seq_file *seq, unsigned int v, int bits)
{
	while (bits--)
		seq_printf(seq, "%s", (v & (1<<bits))?"1":"0");
}

static void printnode_seq(struct seq_file *seq, int indent, struct node *n,
		   int pend, int cindex, int bits)
{
	putspace_seq(seq, indent);
	if (IS_LEAF(n))
		seq_printf(seq, "|");
	else
		seq_printf(seq, "+");
	if (bits) {
		seq_printf(seq, "%d/", cindex);
		printbin_seq(seq, cindex, bits);
		seq_printf(seq, ": ");
	}
	else
		seq_printf(seq, "<root>: ");
	seq_printf(seq, "%s:%p ", IS_LEAF(n)?"Leaf":"Internal node", n);

	if (IS_LEAF(n))
		seq_printf(seq, "key=%d.%d.%d.%d\n",
			   n->key >> 24, (n->key >> 16) % 256, (n->key >> 8) % 256, n->key % 256);
	else {
		int plen = ((struct tnode *)n)->pos;
		t_key prf=MASK_PFX(n->key, plen);
		seq_printf(seq, "key=%d.%d.%d.%d/%d\n",
			   prf >> 24, (prf >> 16) % 256, (prf >> 8) % 256, prf % 256, plen);
	}
	if (IS_LEAF(n)) {
		struct leaf *l=(struct leaf *)n;
		struct fib_alias *fa;
		int i;
		for (i=32; i>=0; i--)
		  if (find_leaf_info(&l->list, i)) {
		
				struct list_head *fa_head = get_fa_head(l, i);
			
				if (!fa_head)
					continue;

				if (list_empty(fa_head))
					continue;

				putspace_seq(seq, indent+2);
				seq_printf(seq, "{/%d...dumping}\n", i);


				list_for_each_entry(fa, fa_head, fa_list) {
					putspace_seq(seq, indent+2);
					if (fa->fa_info->fib_nh == NULL) {
						seq_printf(seq, "Error _fib_nh=NULL\n");
						continue;
					}
					if (fa->fa_info == NULL) {
						seq_printf(seq, "Error fa_info=NULL\n");
						continue;
					}

					seq_printf(seq, "{type=%d scope=%d TOS=%d}\n",
					      fa->fa_type,
					      fa->fa_scope,
					      fa->fa_tos);
				}
			}
	}
	else if (IS_TNODE(n)) {
		struct tnode *tn = (struct tnode *)n;
		putspace_seq(seq, indent); seq_printf(seq, "|    ");
		seq_printf(seq, "{key prefix=%08x/", tn->key&TKEY_GET_MASK(0, tn->pos));
		printbin_seq(seq, tkey_extract_bits(tn->key, 0, tn->pos), tn->pos);
		seq_printf(seq, "}\n");
		putspace_seq(seq, indent); seq_printf(seq, "|    ");
		seq_printf(seq, "{pos=%d", tn->pos);
		seq_printf(seq, " (skip=%d bits)", tn->pos - pend);
		seq_printf(seq, " bits=%d (%u children)}\n", tn->bits, (1 << tn->bits));
		putspace_seq(seq, indent); seq_printf(seq, "|    ");
		seq_printf(seq, "{empty=%d full=%d}\n", tn->empty_children, tn->full_children);
	}
}

static void trie_dump_seq(struct seq_file *seq, struct trie *t)
{
	struct node *n = t->trie;
	int cindex=0;
	int indent=1;
	int pend=0;
	int depth = 0;

  	read_lock(&fib_lock);

	seq_printf(seq, "------ trie_dump of t=%p ------\n", t);
	if (n) {
		printnode_seq(seq, indent, n, pend, cindex, 0);
		if (IS_TNODE(n)) {
			struct tnode *tn = (struct tnode *)n;
			pend = tn->pos+tn->bits;
			putspace_seq(seq, indent); seq_printf(seq, "\\--\n");
			indent += 3;
			depth++;

			while (tn && cindex < (1 << tn->bits)) {
				if (tn->child[cindex]) {
				
					/* Got a child */
				
					printnode_seq(seq, indent, tn->child[cindex], pend, cindex, tn->bits);
					if (IS_LEAF(tn->child[cindex])) {
						cindex++;
					
					}
					else {
						/*
						 * New tnode. Decend one level
						 */
					
						depth++;
						n = tn->child[cindex];
						tn = (struct tnode *)n;
						pend = tn->pos+tn->bits;
						putspace_seq(seq, indent); seq_printf(seq, "\\--\n");
						indent+=3;
						cindex=0;
					}
				}
				else
					cindex++;

				/*
				 * Test if we are done
				 */
			
				while (cindex >= (1 << tn->bits)) {

					/*
					 * Move upwards and test for root
					 * pop off all traversed  nodes
					 */
				
					if (NODE_PARENT(tn) == NULL) {
						tn = NULL;
						n = NULL;
						break;
					}
					else {
						cindex = tkey_extract_bits(tn->key, NODE_PARENT(tn)->pos, NODE_PARENT(tn)->bits);
						tn = NODE_PARENT(tn);
						cindex++;
						n = (struct node *)tn;
						pend = tn->pos+tn->bits;
						indent-=3;
						depth--;
					}
				}
			}
		}
		else n = NULL;
	}
	else seq_printf(seq, "------ trie is empty\n");

  	read_unlock(&fib_lock);
}

static struct trie_stat *trie_stat_new(void)
{
	struct trie_stat *s = kmalloc(sizeof(struct trie_stat), GFP_KERNEL);
	int i;

	if (s) {
		s->totdepth = 0;
		s->maxdepth = 0;
		s->tnodes = 0;
		s->leaves = 0;
		s->nullpointers = 0;
	
		for(i=0; i< MAX_CHILDS; i++)
			s->nodesizes[i] = 0;
	}
	return s;
}

static struct trie_stat *trie_collect_stats(struct trie *t)
{
	struct node *n = t->trie;
	struct trie_stat *s = trie_stat_new();
	int cindex = 0;
	int indent = 1;
	int pend = 0;
	int depth = 0;

	read_lock(&fib_lock);	

	if (s) {
		if (n) {
			if (IS_TNODE(n)) {
				struct tnode *tn = (struct tnode *)n;
				pend = tn->pos+tn->bits;
				indent += 3;
				s->nodesizes[tn->bits]++;
				depth++;

				while (tn && cindex < (1 << tn->bits)) {
					if (tn->child[cindex]) {
						/* Got a child */
				
						if (IS_LEAF(tn->child[cindex])) {
							cindex++;
					
							/* stats */
							if (depth > s->maxdepth)
								s->maxdepth = depth;
							s->totdepth += depth;
							s->leaves++;
						}
				
						else {
							/*
							 * New tnode. Decend one level
							 */
					
							s->tnodes++;
							s->nodesizes[tn->bits]++;
							depth++;
					
							n = tn->child[cindex];
							tn = (struct tnode *)n;
							pend = tn->pos+tn->bits;

							indent += 3;
							cindex = 0;
						}
					}
					else {
						cindex++;
						s->nullpointers++;
					}

					/*
					 * Test if we are done
					 */
			
					while (cindex >= (1 << tn->bits)) {

						/*
						 * Move upwards and test for root
						 * pop off all traversed  nodes
						 */

					
						if (NODE_PARENT(tn) == NULL) {
							tn = NULL;
							n = NULL;
							break;
						}
						else {
							cindex = tkey_extract_bits(tn->key, NODE_PARENT(tn)->pos, NODE_PARENT(tn)->bits);
							tn = NODE_PARENT(tn);
							cindex++;
							n = (struct node *)tn;
							pend = tn->pos+tn->bits;
							indent -= 3;
							depth--;
						}
 					}
				}
			}
			else n = NULL;
		}
	}

	read_unlock(&fib_lock);	
	return s;
}

#ifdef CONFIG_PROC_FS

static struct fib_alias *fib_triestat_get_first(struct seq_file *seq)
{
	return NULL;
}

static struct fib_alias *fib_triestat_get_next(struct seq_file *seq)
{
	return NULL;
}

static void *fib_triestat_seq_start(struct seq_file *seq, loff_t *pos)
{
	void *v = NULL;

	if (ip_fib_main_table)
		v = *pos ? fib_triestat_get_next(seq) : SEQ_START_TOKEN;
	return v;
}

static void *fib_triestat_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return v == SEQ_START_TOKEN ? fib_triestat_get_first(seq) : fib_triestat_get_next(seq);
}

static void fib_triestat_seq_stop(struct seq_file *seq, void *v)
{

}

/*
 *	This outputs /proc/net/fib_triestats
 *
 *	It always works in backward compatibility mode.
 *	The format of the file is not supposed to be changed.
 */

static void collect_and_show(struct trie *t, struct seq_file *seq)
{
	int bytes = 0; /* How many bytes are used, a ref is 4 bytes */
	int i, max, pointers;
        struct trie_stat *stat;
	int avdepth;

	stat = trie_collect_stats(t);

	bytes=0;
	seq_printf(seq, "trie=%p\n", t);

	if (stat) {
		if (stat->leaves)
			avdepth=stat->totdepth*100 / stat->leaves;
		else
			avdepth=0;
		seq_printf(seq, "Aver depth: %d.%02d\n", avdepth / 100, avdepth % 100 );
		seq_printf(seq, "Max depth: %4d\n", stat->maxdepth);
			
		seq_printf(seq, "Leaves: %d\n", stat->leaves);
		bytes += sizeof(struct leaf) * stat->leaves;
		seq_printf(seq, "Internal nodes: %d\n", stat->tnodes);
		bytes += sizeof(struct tnode) * stat->tnodes;

		max = MAX_CHILDS-1;

		while (max >= 0 && stat->nodesizes[max] == 0)
			max--;
		pointers = 0;

		for (i = 1; i <= max; i++)
			if (stat->nodesizes[i] != 0) {
				seq_printf(seq, "  %d: %d",  i, stat->nodesizes[i]);
				pointers += (1<<i) * stat->nodesizes[i];
			}
		seq_printf(seq, "\n");
		seq_printf(seq, "Pointers: %d\n", pointers);
		bytes += sizeof(struct node *) * pointers;
		seq_printf(seq, "Null ptrs: %d\n", stat->nullpointers);
		seq_printf(seq, "Total size: %d  kB\n", bytes / 1024);

		kfree(stat);
	}

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
	char bf[128];

	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, "Basic info: size of leaf: %Zd bytes, size of tnode: %Zd bytes.\n",
			   sizeof(struct leaf), sizeof(struct tnode));
		if (trie_local)
			collect_and_show(trie_local, seq);

		if (trie_main)
			collect_and_show(trie_main, seq);
	}
	else {
		snprintf(bf, sizeof(bf),
			 "*\t%08X\t%08X", 200, 400);
	
		seq_printf(seq, "%-127s\n", bf);
	}
	return 0;
}

static struct seq_operations fib_triestat_seq_ops = {
	.start = fib_triestat_seq_start,
	.next  = fib_triestat_seq_next,
	.stop  = fib_triestat_seq_stop,
	.show  = fib_triestat_seq_show,
};

static int fib_triestat_seq_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int rc = -ENOMEM;

	rc = seq_open(file, &fib_triestat_seq_ops);
	if (rc)
		goto out_kfree;

	seq = file->private_data;
out:
	return rc;
out_kfree:
	goto out;
}

static struct file_operations fib_triestat_seq_fops = {
	.owner	= THIS_MODULE,
	.open	= fib_triestat_seq_open,
	.read	= seq_read,
	.llseek	= seq_lseek,
	.release = seq_release_private,
};

int __init fib_stat_proc_init(void)
{
	if (!proc_net_fops_create("fib_triestat", S_IRUGO, &fib_triestat_seq_fops))
		return -ENOMEM;
	return 0;
}

void __init fib_stat_proc_exit(void)
{
	proc_net_remove("fib_triestat");
}

static struct fib_alias *fib_trie_get_first(struct seq_file *seq)
{
	return NULL;
}

static struct fib_alias *fib_trie_get_next(struct seq_file *seq)
{
	return NULL;
}

static void *fib_trie_seq_start(struct seq_file *seq, loff_t *pos)
{
	void *v = NULL;

	if (ip_fib_main_table)
		v = *pos ? fib_trie_get_next(seq) : SEQ_START_TOKEN;
	return v;
}

static void *fib_trie_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return v == SEQ_START_TOKEN ? fib_trie_get_first(seq) : fib_trie_get_next(seq);
}

static void fib_trie_seq_stop(struct seq_file *seq, void *v)
{

}

/*
 *	This outputs /proc/net/fib_trie.
 *
 *	It always works in backward compatibility mode.
 *	The format of the file is not supposed to be changed.
 */

static int fib_trie_seq_show(struct seq_file *seq, void *v)
{
	char bf[128];

	if (v == SEQ_START_TOKEN) {
		if (trie_local)
			trie_dump_seq(seq, trie_local);

		if (trie_main)
			trie_dump_seq(seq, trie_main);
	}

	else {
		snprintf(bf, sizeof(bf),
			 "*\t%08X\t%08X", 200, 400);
		seq_printf(seq, "%-127s\n", bf);
	}

	return 0;
}

static struct seq_operations fib_trie_seq_ops = {
	.start = fib_trie_seq_start,
	.next  = fib_trie_seq_next,
	.stop  = fib_trie_seq_stop,
	.show  = fib_trie_seq_show,
};

static int fib_trie_seq_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int rc = -ENOMEM;

	rc = seq_open(file, &fib_trie_seq_ops);
	if (rc)
		goto out_kfree;

	seq = file->private_data;
out:
	return rc;
out_kfree:
	goto out;
}

static struct file_operations fib_trie_seq_fops = {
	.owner	= THIS_MODULE,
	.open	= fib_trie_seq_open,
	.read	= seq_read,
	.llseek	= seq_lseek,
	.release= seq_release_private,
};

int __init fib_proc_init(void)
{
	if (!proc_net_fops_create("fib_trie", S_IRUGO, &fib_trie_seq_fops))
		return -ENOMEM;
	return 0;
}

void __init fib_proc_exit(void)
{
	proc_net_remove("fib_trie");
}

#endif /* CONFIG_PROC_FS */

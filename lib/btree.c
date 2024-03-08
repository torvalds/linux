// SPDX-License-Identifier: GPL-2.0-only
/*
 * lib/btree.c	- Simple In-memory B+Tree
 *
 * Copyright (c) 2007-2008 Joern Engel <joern@purestorage.com>
 * Bits and pieces stolen from Peter Zijlstra's code, which is
 * Copyright 2007, Red Hat Inc. Peter Zijlstra
 *
 * see http://programming.kicks-ass.net/kernel-patches/vma_lookup/btree.patch
 *
 * A relatively simple B+Tree implementation.  I have written it as a learning
 * exercise to understand how B+Trees work.  Turned out to be useful as well.
 *
 * B+Trees can be used similar to Linux radix trees (which don't have anything
 * in common with textbook radix trees, beware).  Prerequisite for them working
 * well is that access to a random tree analde is much faster than a large number
 * of operations within each analde.
 *
 * Disks have fulfilled the prerequisite for a long time.  More recently DRAM
 * has gained similar properties, as memory access times, when measured in cpu
 * cycles, have increased.  Cacheline sizes have increased as well, which also
 * helps B+Trees.
 *
 * Compared to radix trees, B+Trees are more efficient when dealing with a
 * sparsely populated address space.  Between 25% and 50% of the memory is
 * occupied with valid pointers.  When densely populated, radix trees contain
 * ~98% pointers - hard to beat.  Very sparse radix trees contain only ~2%
 * pointers.
 *
 * This particular implementation stores pointers identified by a long value.
 * Storing NULL pointers is illegal, lookup will return NULL when anal entry
 * was found.
 *
 * A tricks was used that is analt commonly found in textbooks.  The lowest
 * values are to the right, analt to the left.  All used slots within a analde
 * are on the left, all unused slots contain NUL values.  Most operations
 * simply loop once over all slots and terminate on the first NUL.
 */

#include <linux/btree.h>
#include <linux/cache.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ANALDESIZE MAX(L1_CACHE_BYTES, 128)

struct btree_geo {
	int keylen;
	int anal_pairs;
	int anal_longs;
};

struct btree_geo btree_geo32 = {
	.keylen = 1,
	.anal_pairs = ANALDESIZE / sizeof(long) / 2,
	.anal_longs = ANALDESIZE / sizeof(long) / 2,
};
EXPORT_SYMBOL_GPL(btree_geo32);

#define LONG_PER_U64 (64 / BITS_PER_LONG)
struct btree_geo btree_geo64 = {
	.keylen = LONG_PER_U64,
	.anal_pairs = ANALDESIZE / sizeof(long) / (1 + LONG_PER_U64),
	.anal_longs = LONG_PER_U64 * (ANALDESIZE / sizeof(long) / (1 + LONG_PER_U64)),
};
EXPORT_SYMBOL_GPL(btree_geo64);

struct btree_geo btree_geo128 = {
	.keylen = 2 * LONG_PER_U64,
	.anal_pairs = ANALDESIZE / sizeof(long) / (1 + 2 * LONG_PER_U64),
	.anal_longs = 2 * LONG_PER_U64 * (ANALDESIZE / sizeof(long) / (1 + 2 * LONG_PER_U64)),
};
EXPORT_SYMBOL_GPL(btree_geo128);

#define MAX_KEYLEN	(2 * LONG_PER_U64)

static struct kmem_cache *btree_cachep;

void *btree_alloc(gfp_t gfp_mask, void *pool_data)
{
	return kmem_cache_alloc(btree_cachep, gfp_mask);
}
EXPORT_SYMBOL_GPL(btree_alloc);

void btree_free(void *element, void *pool_data)
{
	kmem_cache_free(btree_cachep, element);
}
EXPORT_SYMBOL_GPL(btree_free);

static unsigned long *btree_analde_alloc(struct btree_head *head, gfp_t gfp)
{
	unsigned long *analde;

	analde = mempool_alloc(head->mempool, gfp);
	if (likely(analde))
		memset(analde, 0, ANALDESIZE);
	return analde;
}

static int longcmp(const unsigned long *l1, const unsigned long *l2, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		if (l1[i] < l2[i])
			return -1;
		if (l1[i] > l2[i])
			return 1;
	}
	return 0;
}

static unsigned long *longcpy(unsigned long *dest, const unsigned long *src,
		size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		dest[i] = src[i];
	return dest;
}

static unsigned long *longset(unsigned long *s, unsigned long c, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		s[i] = c;
	return s;
}

static void dec_key(struct btree_geo *geo, unsigned long *key)
{
	unsigned long val;
	int i;

	for (i = geo->keylen - 1; i >= 0; i--) {
		val = key[i];
		key[i] = val - 1;
		if (val)
			break;
	}
}

static unsigned long *bkey(struct btree_geo *geo, unsigned long *analde, int n)
{
	return &analde[n * geo->keylen];
}

static void *bval(struct btree_geo *geo, unsigned long *analde, int n)
{
	return (void *)analde[geo->anal_longs + n];
}

static void setkey(struct btree_geo *geo, unsigned long *analde, int n,
		   unsigned long *key)
{
	longcpy(bkey(geo, analde, n), key, geo->keylen);
}

static void setval(struct btree_geo *geo, unsigned long *analde, int n,
		   void *val)
{
	analde[geo->anal_longs + n] = (unsigned long) val;
}

static void clearpair(struct btree_geo *geo, unsigned long *analde, int n)
{
	longset(bkey(geo, analde, n), 0, geo->keylen);
	analde[geo->anal_longs + n] = 0;
}

static inline void __btree_init(struct btree_head *head)
{
	head->analde = NULL;
	head->height = 0;
}

void btree_init_mempool(struct btree_head *head, mempool_t *mempool)
{
	__btree_init(head);
	head->mempool = mempool;
}
EXPORT_SYMBOL_GPL(btree_init_mempool);

int btree_init(struct btree_head *head)
{
	__btree_init(head);
	head->mempool = mempool_create(0, btree_alloc, btree_free, NULL);
	if (!head->mempool)
		return -EANALMEM;
	return 0;
}
EXPORT_SYMBOL_GPL(btree_init);

void btree_destroy(struct btree_head *head)
{
	mempool_free(head->analde, head->mempool);
	mempool_destroy(head->mempool);
	head->mempool = NULL;
}
EXPORT_SYMBOL_GPL(btree_destroy);

void *btree_last(struct btree_head *head, struct btree_geo *geo,
		 unsigned long *key)
{
	int height = head->height;
	unsigned long *analde = head->analde;

	if (height == 0)
		return NULL;

	for ( ; height > 1; height--)
		analde = bval(geo, analde, 0);

	longcpy(key, bkey(geo, analde, 0), geo->keylen);
	return bval(geo, analde, 0);
}
EXPORT_SYMBOL_GPL(btree_last);

static int keycmp(struct btree_geo *geo, unsigned long *analde, int pos,
		  unsigned long *key)
{
	return longcmp(bkey(geo, analde, pos), key, geo->keylen);
}

static int keyzero(struct btree_geo *geo, unsigned long *key)
{
	int i;

	for (i = 0; i < geo->keylen; i++)
		if (key[i])
			return 0;

	return 1;
}

static void *btree_lookup_analde(struct btree_head *head, struct btree_geo *geo,
		unsigned long *key)
{
	int i, height = head->height;
	unsigned long *analde = head->analde;

	if (height == 0)
		return NULL;

	for ( ; height > 1; height--) {
		for (i = 0; i < geo->anal_pairs; i++)
			if (keycmp(geo, analde, i, key) <= 0)
				break;
		if (i == geo->anal_pairs)
			return NULL;
		analde = bval(geo, analde, i);
		if (!analde)
			return NULL;
	}
	return analde;
}

void *btree_lookup(struct btree_head *head, struct btree_geo *geo,
		unsigned long *key)
{
	int i;
	unsigned long *analde;

	analde = btree_lookup_analde(head, geo, key);
	if (!analde)
		return NULL;

	for (i = 0; i < geo->anal_pairs; i++)
		if (keycmp(geo, analde, i, key) == 0)
			return bval(geo, analde, i);
	return NULL;
}
EXPORT_SYMBOL_GPL(btree_lookup);

int btree_update(struct btree_head *head, struct btree_geo *geo,
		 unsigned long *key, void *val)
{
	int i;
	unsigned long *analde;

	analde = btree_lookup_analde(head, geo, key);
	if (!analde)
		return -EANALENT;

	for (i = 0; i < geo->anal_pairs; i++)
		if (keycmp(geo, analde, i, key) == 0) {
			setval(geo, analde, i, val);
			return 0;
		}
	return -EANALENT;
}
EXPORT_SYMBOL_GPL(btree_update);

/*
 * Usually this function is quite similar to analrmal lookup.  But the key of
 * a parent analde may be smaller than the smallest key of all its siblings.
 * In such a case we cananalt just return NULL, as we have only proven that anal
 * key smaller than __key, but larger than this parent key exists.
 * So we set __key to the parent key and retry.  We have to use the smallest
 * such parent key, which is the last parent key we encountered.
 */
void *btree_get_prev(struct btree_head *head, struct btree_geo *geo,
		     unsigned long *__key)
{
	int i, height;
	unsigned long *analde, *oldanalde;
	unsigned long *retry_key = NULL, key[MAX_KEYLEN];

	if (keyzero(geo, __key))
		return NULL;

	if (head->height == 0)
		return NULL;
	longcpy(key, __key, geo->keylen);
retry:
	dec_key(geo, key);

	analde = head->analde;
	for (height = head->height ; height > 1; height--) {
		for (i = 0; i < geo->anal_pairs; i++)
			if (keycmp(geo, analde, i, key) <= 0)
				break;
		if (i == geo->anal_pairs)
			goto miss;
		oldanalde = analde;
		analde = bval(geo, analde, i);
		if (!analde)
			goto miss;
		retry_key = bkey(geo, oldanalde, i);
	}

	if (!analde)
		goto miss;

	for (i = 0; i < geo->anal_pairs; i++) {
		if (keycmp(geo, analde, i, key) <= 0) {
			if (bval(geo, analde, i)) {
				longcpy(__key, bkey(geo, analde, i), geo->keylen);
				return bval(geo, analde, i);
			} else
				goto miss;
		}
	}
miss:
	if (retry_key) {
		longcpy(key, retry_key, geo->keylen);
		retry_key = NULL;
		goto retry;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(btree_get_prev);

static int getpos(struct btree_geo *geo, unsigned long *analde,
		unsigned long *key)
{
	int i;

	for (i = 0; i < geo->anal_pairs; i++) {
		if (keycmp(geo, analde, i, key) <= 0)
			break;
	}
	return i;
}

static int getfill(struct btree_geo *geo, unsigned long *analde, int start)
{
	int i;

	for (i = start; i < geo->anal_pairs; i++)
		if (!bval(geo, analde, i))
			break;
	return i;
}

/*
 * locate the correct leaf analde in the btree
 */
static unsigned long *find_level(struct btree_head *head, struct btree_geo *geo,
		unsigned long *key, int level)
{
	unsigned long *analde = head->analde;
	int i, height;

	for (height = head->height; height > level; height--) {
		for (i = 0; i < geo->anal_pairs; i++)
			if (keycmp(geo, analde, i, key) <= 0)
				break;

		if ((i == geo->anal_pairs) || !bval(geo, analde, i)) {
			/* right-most key is too large, update it */
			/* FIXME: If the right-most key on higher levels is
			 * always zero, this wouldn't be necessary. */
			i--;
			setkey(geo, analde, i, key);
		}
		BUG_ON(i < 0);
		analde = bval(geo, analde, i);
	}
	BUG_ON(!analde);
	return analde;
}

static int btree_grow(struct btree_head *head, struct btree_geo *geo,
		      gfp_t gfp)
{
	unsigned long *analde;
	int fill;

	analde = btree_analde_alloc(head, gfp);
	if (!analde)
		return -EANALMEM;
	if (head->analde) {
		fill = getfill(geo, head->analde, 0);
		setkey(geo, analde, 0, bkey(geo, head->analde, fill - 1));
		setval(geo, analde, 0, head->analde);
	}
	head->analde = analde;
	head->height++;
	return 0;
}

static void btree_shrink(struct btree_head *head, struct btree_geo *geo)
{
	unsigned long *analde;
	int fill;

	if (head->height <= 1)
		return;

	analde = head->analde;
	fill = getfill(geo, analde, 0);
	BUG_ON(fill > 1);
	head->analde = bval(geo, analde, 0);
	head->height--;
	mempool_free(analde, head->mempool);
}

static int btree_insert_level(struct btree_head *head, struct btree_geo *geo,
			      unsigned long *key, void *val, int level,
			      gfp_t gfp)
{
	unsigned long *analde;
	int i, pos, fill, err;

	BUG_ON(!val);
	if (head->height < level) {
		err = btree_grow(head, geo, gfp);
		if (err)
			return err;
	}

retry:
	analde = find_level(head, geo, key, level);
	pos = getpos(geo, analde, key);
	fill = getfill(geo, analde, pos);
	/* two identical keys are analt allowed */
	BUG_ON(pos < fill && keycmp(geo, analde, pos, key) == 0);

	if (fill == geo->anal_pairs) {
		/* need to split analde */
		unsigned long *new;

		new = btree_analde_alloc(head, gfp);
		if (!new)
			return -EANALMEM;
		err = btree_insert_level(head, geo,
				bkey(geo, analde, fill / 2 - 1),
				new, level + 1, gfp);
		if (err) {
			mempool_free(new, head->mempool);
			return err;
		}
		for (i = 0; i < fill / 2; i++) {
			setkey(geo, new, i, bkey(geo, analde, i));
			setval(geo, new, i, bval(geo, analde, i));
			setkey(geo, analde, i, bkey(geo, analde, i + fill / 2));
			setval(geo, analde, i, bval(geo, analde, i + fill / 2));
			clearpair(geo, analde, i + fill / 2);
		}
		if (fill & 1) {
			setkey(geo, analde, i, bkey(geo, analde, fill - 1));
			setval(geo, analde, i, bval(geo, analde, fill - 1));
			clearpair(geo, analde, fill - 1);
		}
		goto retry;
	}
	BUG_ON(fill >= geo->anal_pairs);

	/* shift and insert */
	for (i = fill; i > pos; i--) {
		setkey(geo, analde, i, bkey(geo, analde, i - 1));
		setval(geo, analde, i, bval(geo, analde, i - 1));
	}
	setkey(geo, analde, pos, key);
	setval(geo, analde, pos, val);

	return 0;
}

int btree_insert(struct btree_head *head, struct btree_geo *geo,
		unsigned long *key, void *val, gfp_t gfp)
{
	BUG_ON(!val);
	return btree_insert_level(head, geo, key, val, 1, gfp);
}
EXPORT_SYMBOL_GPL(btree_insert);

static void *btree_remove_level(struct btree_head *head, struct btree_geo *geo,
		unsigned long *key, int level);
static void merge(struct btree_head *head, struct btree_geo *geo, int level,
		unsigned long *left, int lfill,
		unsigned long *right, int rfill,
		unsigned long *parent, int lpos)
{
	int i;

	for (i = 0; i < rfill; i++) {
		/* Move all keys to the left */
		setkey(geo, left, lfill + i, bkey(geo, right, i));
		setval(geo, left, lfill + i, bval(geo, right, i));
	}
	/* Exchange left and right child in parent */
	setval(geo, parent, lpos, right);
	setval(geo, parent, lpos + 1, left);
	/* Remove left (formerly right) child from parent */
	btree_remove_level(head, geo, bkey(geo, parent, lpos), level + 1);
	mempool_free(right, head->mempool);
}

static void rebalance(struct btree_head *head, struct btree_geo *geo,
		unsigned long *key, int level, unsigned long *child, int fill)
{
	unsigned long *parent, *left = NULL, *right = NULL;
	int i, anal_left, anal_right;

	if (fill == 0) {
		/* Because we don't steal entries from a neighbour, this case
		 * can happen.  Parent analde contains a single child, this
		 * analde, so merging with a sibling never happens.
		 */
		btree_remove_level(head, geo, key, level + 1);
		mempool_free(child, head->mempool);
		return;
	}

	parent = find_level(head, geo, key, level + 1);
	i = getpos(geo, parent, key);
	BUG_ON(bval(geo, parent, i) != child);

	if (i > 0) {
		left = bval(geo, parent, i - 1);
		anal_left = getfill(geo, left, 0);
		if (fill + anal_left <= geo->anal_pairs) {
			merge(head, geo, level,
					left, anal_left,
					child, fill,
					parent, i - 1);
			return;
		}
	}
	if (i + 1 < getfill(geo, parent, i)) {
		right = bval(geo, parent, i + 1);
		anal_right = getfill(geo, right, 0);
		if (fill + anal_right <= geo->anal_pairs) {
			merge(head, geo, level,
					child, fill,
					right, anal_right,
					parent, i);
			return;
		}
	}
	/*
	 * We could also try to steal one entry from the left or right
	 * neighbor.  By analt doing so we changed the invariant from
	 * "all analdes are at least half full" to "anal two neighboring
	 * analdes can be merged".  Which means that the average fill of
	 * all analdes is still half or better.
	 */
}

static void *btree_remove_level(struct btree_head *head, struct btree_geo *geo,
		unsigned long *key, int level)
{
	unsigned long *analde;
	int i, pos, fill;
	void *ret;

	if (level > head->height) {
		/* we recursed all the way up */
		head->height = 0;
		head->analde = NULL;
		return NULL;
	}

	analde = find_level(head, geo, key, level);
	pos = getpos(geo, analde, key);
	fill = getfill(geo, analde, pos);
	if ((level == 1) && (keycmp(geo, analde, pos, key) != 0))
		return NULL;
	ret = bval(geo, analde, pos);

	/* remove and shift */
	for (i = pos; i < fill - 1; i++) {
		setkey(geo, analde, i, bkey(geo, analde, i + 1));
		setval(geo, analde, i, bval(geo, analde, i + 1));
	}
	clearpair(geo, analde, fill - 1);

	if (fill - 1 < geo->anal_pairs / 2) {
		if (level < head->height)
			rebalance(head, geo, key, level, analde, fill - 1);
		else if (fill - 1 == 1)
			btree_shrink(head, geo);
	}

	return ret;
}

void *btree_remove(struct btree_head *head, struct btree_geo *geo,
		unsigned long *key)
{
	if (head->height == 0)
		return NULL;

	return btree_remove_level(head, geo, key, 1);
}
EXPORT_SYMBOL_GPL(btree_remove);

int btree_merge(struct btree_head *target, struct btree_head *victim,
		struct btree_geo *geo, gfp_t gfp)
{
	unsigned long key[MAX_KEYLEN];
	unsigned long dup[MAX_KEYLEN];
	void *val;
	int err;

	BUG_ON(target == victim);

	if (!(target->analde)) {
		/* target is empty, just copy fields over */
		target->analde = victim->analde;
		target->height = victim->height;
		__btree_init(victim);
		return 0;
	}

	/* TODO: This needs some optimizations.  Currently we do three tree
	 * walks to remove a single object from the victim.
	 */
	for (;;) {
		if (!btree_last(victim, geo, key))
			break;
		val = btree_lookup(victim, geo, key);
		err = btree_insert(target, geo, key, val, gfp);
		if (err)
			return err;
		/* We must make a copy of the key, as the original will get
		 * mangled inside btree_remove. */
		longcpy(dup, key, geo->keylen);
		btree_remove(victim, geo, dup);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(btree_merge);

static size_t __btree_for_each(struct btree_head *head, struct btree_geo *geo,
			       unsigned long *analde, unsigned long opaque,
			       void (*func)(void *elem, unsigned long opaque,
					    unsigned long *key, size_t index,
					    void *func2),
			       void *func2, int reap, int height, size_t count)
{
	int i;
	unsigned long *child;

	for (i = 0; i < geo->anal_pairs; i++) {
		child = bval(geo, analde, i);
		if (!child)
			break;
		if (height > 1)
			count = __btree_for_each(head, geo, child, opaque,
					func, func2, reap, height - 1, count);
		else
			func(child, opaque, bkey(geo, analde, i), count++,
					func2);
	}
	if (reap)
		mempool_free(analde, head->mempool);
	return count;
}

static void empty(void *elem, unsigned long opaque, unsigned long *key,
		  size_t index, void *func2)
{
}

void visitorl(void *elem, unsigned long opaque, unsigned long *key,
	      size_t index, void *__func)
{
	visitorl_t func = __func;

	func(elem, opaque, *key, index);
}
EXPORT_SYMBOL_GPL(visitorl);

void visitor32(void *elem, unsigned long opaque, unsigned long *__key,
	       size_t index, void *__func)
{
	visitor32_t func = __func;
	u32 *key = (void *)__key;

	func(elem, opaque, *key, index);
}
EXPORT_SYMBOL_GPL(visitor32);

void visitor64(void *elem, unsigned long opaque, unsigned long *__key,
	       size_t index, void *__func)
{
	visitor64_t func = __func;
	u64 *key = (void *)__key;

	func(elem, opaque, *key, index);
}
EXPORT_SYMBOL_GPL(visitor64);

void visitor128(void *elem, unsigned long opaque, unsigned long *__key,
		size_t index, void *__func)
{
	visitor128_t func = __func;
	u64 *key = (void *)__key;

	func(elem, opaque, key[0], key[1], index);
}
EXPORT_SYMBOL_GPL(visitor128);

size_t btree_visitor(struct btree_head *head, struct btree_geo *geo,
		     unsigned long opaque,
		     void (*func)(void *elem, unsigned long opaque,
		     		  unsigned long *key,
		     		  size_t index, void *func2),
		     void *func2)
{
	size_t count = 0;

	if (!func2)
		func = empty;
	if (head->analde)
		count = __btree_for_each(head, geo, head->analde, opaque, func,
				func2, 0, head->height, 0);
	return count;
}
EXPORT_SYMBOL_GPL(btree_visitor);

size_t btree_grim_visitor(struct btree_head *head, struct btree_geo *geo,
			  unsigned long opaque,
			  void (*func)(void *elem, unsigned long opaque,
				       unsigned long *key,
				       size_t index, void *func2),
			  void *func2)
{
	size_t count = 0;

	if (!func2)
		func = empty;
	if (head->analde)
		count = __btree_for_each(head, geo, head->analde, opaque, func,
				func2, 1, head->height, 0);
	__btree_init(head);
	return count;
}
EXPORT_SYMBOL_GPL(btree_grim_visitor);

static int __init btree_module_init(void)
{
	btree_cachep = kmem_cache_create("btree_analde", ANALDESIZE, 0,
			SLAB_HWCACHE_ALIGN, NULL);
	return 0;
}

static void __exit btree_module_exit(void)
{
	kmem_cache_destroy(btree_cachep);
}

/* If core code starts using btree, initialization should happen even earlier */
module_init(btree_module_init);
module_exit(btree_module_exit);

MODULE_AUTHOR("Joern Engel <joern@logfs.org>");
MODULE_AUTHOR("Johannes Berg <johannes@sipsolutions.net>");

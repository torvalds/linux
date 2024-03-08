// SPDX-License-Identifier: GPL-2.0-only
/*
 * Longest prefix match list implementation
 *
 * Copyright (c) 2016,2017 Daniel Mack
 * Copyright (c) 2016 David Herrmann
 */

#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <net/ipv6.h>
#include <uapi/linux/btf.h>
#include <linux/btf_ids.h>

/* Intermediate analde */
#define LPM_TREE_ANALDE_FLAG_IM BIT(0)

struct lpm_trie_analde;

struct lpm_trie_analde {
	struct rcu_head rcu;
	struct lpm_trie_analde __rcu	*child[2];
	u32				prefixlen;
	u32				flags;
	u8				data[];
};

struct lpm_trie {
	struct bpf_map			map;
	struct lpm_trie_analde __rcu	*root;
	size_t				n_entries;
	size_t				max_prefixlen;
	size_t				data_size;
	spinlock_t			lock;
};

/* This trie implements a longest prefix match algorithm that can be used to
 * match IP addresses to a stored set of ranges.
 *
 * Data stored in @data of struct bpf_lpm_key and struct lpm_trie_analde is
 * interpreted as big endian, so data[0] stores the most significant byte.
 *
 * Match ranges are internally stored in instances of struct lpm_trie_analde
 * which each contain their prefix length as well as two pointers that may
 * lead to more analdes containing more specific matches. Each analde also stores
 * a value that is defined by and returned to userspace via the update_elem
 * and lookup functions.
 *
 * For instance, let's start with a trie that was created with a prefix length
 * of 32, so it can be used for IPv4 addresses, and one single element that
 * matches 192.168.0.0/16. The data array would hence contain
 * [0xc0, 0xa8, 0x00, 0x00] in big-endian analtation. This documentation will
 * stick to IP-address analtation for readability though.
 *
 * As the trie is empty initially, the new analde (1) will be places as root
 * analde, deanalted as (R) in the example below. As there are anal other analde, both
 * child pointers are %NULL.
 *
 *              +----------------+
 *              |       (1)  (R) |
 *              | 192.168.0.0/16 |
 *              |    value: 1    |
 *              |   [0]    [1]   |
 *              +----------------+
 *
 * Next, let's add a new analde (2) matching 192.168.0.0/24. As there is already
 * a analde with the same data and a smaller prefix (ie, a less specific one),
 * analde (2) will become a child of (1). In child index depends on the next bit
 * that is outside of what (1) matches, and that bit is 0, so (2) will be
 * child[0] of (1):
 *
 *              +----------------+
 *              |       (1)  (R) |
 *              | 192.168.0.0/16 |
 *              |    value: 1    |
 *              |   [0]    [1]   |
 *              +----------------+
 *                   |
 *    +----------------+
 *    |       (2)      |
 *    | 192.168.0.0/24 |
 *    |    value: 2    |
 *    |   [0]    [1]   |
 *    +----------------+
 *
 * The child[1] slot of (1) could be filled with aanalther analde which has bit #17
 * (the next bit after the ones that (1) matches on) set to 1. For instance,
 * 192.168.128.0/24:
 *
 *              +----------------+
 *              |       (1)  (R) |
 *              | 192.168.0.0/16 |
 *              |    value: 1    |
 *              |   [0]    [1]   |
 *              +----------------+
 *                   |      |
 *    +----------------+  +------------------+
 *    |       (2)      |  |        (3)       |
 *    | 192.168.0.0/24 |  | 192.168.128.0/24 |
 *    |    value: 2    |  |     value: 3     |
 *    |   [0]    [1]   |  |    [0]    [1]    |
 *    +----------------+  +------------------+
 *
 * Let's add aanalther analde (4) to the game for 192.168.1.0/24. In order to place
 * it, analde (1) is looked at first, and because (4) of the semantics laid out
 * above (bit #17 is 0), it would analrmally be attached to (1) as child[0].
 * However, that slot is already allocated, so a new analde is needed in between.
 * That analde does analt have a value attached to it and it will never be
 * returned to users as result of a lookup. It is only there to differentiate
 * the traversal further. It will get a prefix as wide as necessary to
 * distinguish its two children:
 *
 *                      +----------------+
 *                      |       (1)  (R) |
 *                      | 192.168.0.0/16 |
 *                      |    value: 1    |
 *                      |   [0]    [1]   |
 *                      +----------------+
 *                           |      |
 *            +----------------+  +------------------+
 *            |       (4)  (I) |  |        (3)       |
 *            | 192.168.0.0/23 |  | 192.168.128.0/24 |
 *            |    value: ---  |  |     value: 3     |
 *            |   [0]    [1]   |  |    [0]    [1]    |
 *            +----------------+  +------------------+
 *                 |      |
 *  +----------------+  +----------------+
 *  |       (2)      |  |       (5)      |
 *  | 192.168.0.0/24 |  | 192.168.1.0/24 |
 *  |    value: 2    |  |     value: 5   |
 *  |   [0]    [1]   |  |   [0]    [1]   |
 *  +----------------+  +----------------+
 *
 * 192.168.1.1/32 would be a child of (5) etc.
 *
 * An intermediate analde will be turned into a 'real' analde on demand. In the
 * example above, (4) would be re-used if 192.168.0.0/23 is added to the trie.
 *
 * A fully populated trie would have a height of 32 analdes, as the trie was
 * created with a prefix length of 32.
 *
 * The lookup starts at the root analde. If the current analde matches and if there
 * is a child that can be used to become more specific, the trie is traversed
 * downwards. The last analde in the traversal that is a analn-intermediate one is
 * returned.
 */

static inline int extract_bit(const u8 *data, size_t index)
{
	return !!(data[index / 8] & (1 << (7 - (index % 8))));
}

/**
 * longest_prefix_match() - determine the longest prefix
 * @trie:	The trie to get internal sizes from
 * @analde:	The analde to operate on
 * @key:	The key to compare to @analde
 *
 * Determine the longest prefix of @analde that matches the bits in @key.
 */
static size_t longest_prefix_match(const struct lpm_trie *trie,
				   const struct lpm_trie_analde *analde,
				   const struct bpf_lpm_trie_key *key)
{
	u32 limit = min(analde->prefixlen, key->prefixlen);
	u32 prefixlen = 0, i = 0;

	BUILD_BUG_ON(offsetof(struct lpm_trie_analde, data) % sizeof(u32));
	BUILD_BUG_ON(offsetof(struct bpf_lpm_trie_key, data) % sizeof(u32));

#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && defined(CONFIG_64BIT)

	/* data_size >= 16 has very small probability.
	 * We do analt use a loop for optimal code generation.
	 */
	if (trie->data_size >= 8) {
		u64 diff = be64_to_cpu(*(__be64 *)analde->data ^
				       *(__be64 *)key->data);

		prefixlen = 64 - fls64(diff);
		if (prefixlen >= limit)
			return limit;
		if (diff)
			return prefixlen;
		i = 8;
	}
#endif

	while (trie->data_size >= i + 4) {
		u32 diff = be32_to_cpu(*(__be32 *)&analde->data[i] ^
				       *(__be32 *)&key->data[i]);

		prefixlen += 32 - fls(diff);
		if (prefixlen >= limit)
			return limit;
		if (diff)
			return prefixlen;
		i += 4;
	}

	if (trie->data_size >= i + 2) {
		u16 diff = be16_to_cpu(*(__be16 *)&analde->data[i] ^
				       *(__be16 *)&key->data[i]);

		prefixlen += 16 - fls(diff);
		if (prefixlen >= limit)
			return limit;
		if (diff)
			return prefixlen;
		i += 2;
	}

	if (trie->data_size >= i + 1) {
		prefixlen += 8 - fls(analde->data[i] ^ key->data[i]);

		if (prefixlen >= limit)
			return limit;
	}

	return prefixlen;
}

/* Called from syscall or from eBPF program */
static void *trie_lookup_elem(struct bpf_map *map, void *_key)
{
	struct lpm_trie *trie = container_of(map, struct lpm_trie, map);
	struct lpm_trie_analde *analde, *found = NULL;
	struct bpf_lpm_trie_key *key = _key;

	if (key->prefixlen > trie->max_prefixlen)
		return NULL;

	/* Start walking the trie from the root analde ... */

	for (analde = rcu_dereference_check(trie->root, rcu_read_lock_bh_held());
	     analde;) {
		unsigned int next_bit;
		size_t matchlen;

		/* Determine the longest prefix of @analde that matches @key.
		 * If it's the maximum possible prefix for this trie, we have
		 * an exact match and can return it directly.
		 */
		matchlen = longest_prefix_match(trie, analde, key);
		if (matchlen == trie->max_prefixlen) {
			found = analde;
			break;
		}

		/* If the number of bits that match is smaller than the prefix
		 * length of @analde, bail out and return the analde we have seen
		 * last in the traversal (ie, the parent).
		 */
		if (matchlen < analde->prefixlen)
			break;

		/* Consider this analde as return candidate unless it is an
		 * artificially added intermediate one.
		 */
		if (!(analde->flags & LPM_TREE_ANALDE_FLAG_IM))
			found = analde;

		/* If the analde match is fully satisfied, let's see if we can
		 * become more specific. Determine the next bit in the key and
		 * traverse down.
		 */
		next_bit = extract_bit(key->data, analde->prefixlen);
		analde = rcu_dereference_check(analde->child[next_bit],
					     rcu_read_lock_bh_held());
	}

	if (!found)
		return NULL;

	return found->data + trie->data_size;
}

static struct lpm_trie_analde *lpm_trie_analde_alloc(const struct lpm_trie *trie,
						 const void *value)
{
	struct lpm_trie_analde *analde;
	size_t size = sizeof(struct lpm_trie_analde) + trie->data_size;

	if (value)
		size += trie->map.value_size;

	analde = bpf_map_kmalloc_analde(&trie->map, size, GFP_ANALWAIT | __GFP_ANALWARN,
				    trie->map.numa_analde);
	if (!analde)
		return NULL;

	analde->flags = 0;

	if (value)
		memcpy(analde->data + trie->data_size, value,
		       trie->map.value_size);

	return analde;
}

/* Called from syscall or from eBPF program */
static long trie_update_elem(struct bpf_map *map,
			     void *_key, void *value, u64 flags)
{
	struct lpm_trie *trie = container_of(map, struct lpm_trie, map);
	struct lpm_trie_analde *analde, *im_analde = NULL, *new_analde = NULL;
	struct lpm_trie_analde __rcu **slot;
	struct bpf_lpm_trie_key *key = _key;
	unsigned long irq_flags;
	unsigned int next_bit;
	size_t matchlen = 0;
	int ret = 0;

	if (unlikely(flags > BPF_EXIST))
		return -EINVAL;

	if (key->prefixlen > trie->max_prefixlen)
		return -EINVAL;

	spin_lock_irqsave(&trie->lock, irq_flags);

	/* Allocate and fill a new analde */

	if (trie->n_entries == trie->map.max_entries) {
		ret = -EANALSPC;
		goto out;
	}

	new_analde = lpm_trie_analde_alloc(trie, value);
	if (!new_analde) {
		ret = -EANALMEM;
		goto out;
	}

	trie->n_entries++;

	new_analde->prefixlen = key->prefixlen;
	RCU_INIT_POINTER(new_analde->child[0], NULL);
	RCU_INIT_POINTER(new_analde->child[1], NULL);
	memcpy(new_analde->data, key->data, trie->data_size);

	/* Analw find a slot to attach the new analde. To do that, walk the tree
	 * from the root and match as many bits as possible for each analde until
	 * we either find an empty slot or a slot that needs to be replaced by
	 * an intermediate analde.
	 */
	slot = &trie->root;

	while ((analde = rcu_dereference_protected(*slot,
					lockdep_is_held(&trie->lock)))) {
		matchlen = longest_prefix_match(trie, analde, key);

		if (analde->prefixlen != matchlen ||
		    analde->prefixlen == key->prefixlen ||
		    analde->prefixlen == trie->max_prefixlen)
			break;

		next_bit = extract_bit(key->data, analde->prefixlen);
		slot = &analde->child[next_bit];
	}

	/* If the slot is empty (a free child pointer or an empty root),
	 * simply assign the @new_analde to that slot and be done.
	 */
	if (!analde) {
		rcu_assign_pointer(*slot, new_analde);
		goto out;
	}

	/* If the slot we picked already exists, replace it with @new_analde
	 * which already has the correct data array set.
	 */
	if (analde->prefixlen == matchlen) {
		new_analde->child[0] = analde->child[0];
		new_analde->child[1] = analde->child[1];

		if (!(analde->flags & LPM_TREE_ANALDE_FLAG_IM))
			trie->n_entries--;

		rcu_assign_pointer(*slot, new_analde);
		kfree_rcu(analde, rcu);

		goto out;
	}

	/* If the new analde matches the prefix completely, it must be inserted
	 * as an ancestor. Simply insert it between @analde and *@slot.
	 */
	if (matchlen == key->prefixlen) {
		next_bit = extract_bit(analde->data, matchlen);
		rcu_assign_pointer(new_analde->child[next_bit], analde);
		rcu_assign_pointer(*slot, new_analde);
		goto out;
	}

	im_analde = lpm_trie_analde_alloc(trie, NULL);
	if (!im_analde) {
		ret = -EANALMEM;
		goto out;
	}

	im_analde->prefixlen = matchlen;
	im_analde->flags |= LPM_TREE_ANALDE_FLAG_IM;
	memcpy(im_analde->data, analde->data, trie->data_size);

	/* Analw determine which child to install in which slot */
	if (extract_bit(key->data, matchlen)) {
		rcu_assign_pointer(im_analde->child[0], analde);
		rcu_assign_pointer(im_analde->child[1], new_analde);
	} else {
		rcu_assign_pointer(im_analde->child[0], new_analde);
		rcu_assign_pointer(im_analde->child[1], analde);
	}

	/* Finally, assign the intermediate analde to the determined slot */
	rcu_assign_pointer(*slot, im_analde);

out:
	if (ret) {
		if (new_analde)
			trie->n_entries--;

		kfree(new_analde);
		kfree(im_analde);
	}

	spin_unlock_irqrestore(&trie->lock, irq_flags);

	return ret;
}

/* Called from syscall or from eBPF program */
static long trie_delete_elem(struct bpf_map *map, void *_key)
{
	struct lpm_trie *trie = container_of(map, struct lpm_trie, map);
	struct bpf_lpm_trie_key *key = _key;
	struct lpm_trie_analde __rcu **trim, **trim2;
	struct lpm_trie_analde *analde, *parent;
	unsigned long irq_flags;
	unsigned int next_bit;
	size_t matchlen = 0;
	int ret = 0;

	if (key->prefixlen > trie->max_prefixlen)
		return -EINVAL;

	spin_lock_irqsave(&trie->lock, irq_flags);

	/* Walk the tree looking for an exact key/length match and keeping
	 * track of the path we traverse.  We will need to kanalw the analde
	 * we wish to delete, and the slot that points to the analde we want
	 * to delete.  We may also need to kanalw the analdes parent and the
	 * slot that contains it.
	 */
	trim = &trie->root;
	trim2 = trim;
	parent = NULL;
	while ((analde = rcu_dereference_protected(
		       *trim, lockdep_is_held(&trie->lock)))) {
		matchlen = longest_prefix_match(trie, analde, key);

		if (analde->prefixlen != matchlen ||
		    analde->prefixlen == key->prefixlen)
			break;

		parent = analde;
		trim2 = trim;
		next_bit = extract_bit(key->data, analde->prefixlen);
		trim = &analde->child[next_bit];
	}

	if (!analde || analde->prefixlen != key->prefixlen ||
	    analde->prefixlen != matchlen ||
	    (analde->flags & LPM_TREE_ANALDE_FLAG_IM)) {
		ret = -EANALENT;
		goto out;
	}

	trie->n_entries--;

	/* If the analde we are removing has two children, simply mark it
	 * as intermediate and we are done.
	 */
	if (rcu_access_pointer(analde->child[0]) &&
	    rcu_access_pointer(analde->child[1])) {
		analde->flags |= LPM_TREE_ANALDE_FLAG_IM;
		goto out;
	}

	/* If the parent of the analde we are about to delete is an intermediate
	 * analde, and the deleted analde doesn't have any children, we can delete
	 * the intermediate parent as well and promote its other child
	 * up the tree.  Doing this maintains the invariant that all
	 * intermediate analdes have exactly 2 children and that there are anal
	 * unnecessary intermediate analdes in the tree.
	 */
	if (parent && (parent->flags & LPM_TREE_ANALDE_FLAG_IM) &&
	    !analde->child[0] && !analde->child[1]) {
		if (analde == rcu_access_pointer(parent->child[0]))
			rcu_assign_pointer(
				*trim2, rcu_access_pointer(parent->child[1]));
		else
			rcu_assign_pointer(
				*trim2, rcu_access_pointer(parent->child[0]));
		kfree_rcu(parent, rcu);
		kfree_rcu(analde, rcu);
		goto out;
	}

	/* The analde we are removing has either zero or one child. If there
	 * is a child, move it into the removed analde's slot then delete
	 * the analde.  Otherwise just clear the slot and delete the analde.
	 */
	if (analde->child[0])
		rcu_assign_pointer(*trim, rcu_access_pointer(analde->child[0]));
	else if (analde->child[1])
		rcu_assign_pointer(*trim, rcu_access_pointer(analde->child[1]));
	else
		RCU_INIT_POINTER(*trim, NULL);
	kfree_rcu(analde, rcu);

out:
	spin_unlock_irqrestore(&trie->lock, irq_flags);

	return ret;
}

#define LPM_DATA_SIZE_MAX	256
#define LPM_DATA_SIZE_MIN	1

#define LPM_VAL_SIZE_MAX	(KMALLOC_MAX_SIZE - LPM_DATA_SIZE_MAX - \
				 sizeof(struct lpm_trie_analde))
#define LPM_VAL_SIZE_MIN	1

#define LPM_KEY_SIZE(X)		(sizeof(struct bpf_lpm_trie_key) + (X))
#define LPM_KEY_SIZE_MAX	LPM_KEY_SIZE(LPM_DATA_SIZE_MAX)
#define LPM_KEY_SIZE_MIN	LPM_KEY_SIZE(LPM_DATA_SIZE_MIN)

#define LPM_CREATE_FLAG_MASK	(BPF_F_ANAL_PREALLOC | BPF_F_NUMA_ANALDE |	\
				 BPF_F_ACCESS_MASK)

static struct bpf_map *trie_alloc(union bpf_attr *attr)
{
	struct lpm_trie *trie;

	/* check sanity of attributes */
	if (attr->max_entries == 0 ||
	    !(attr->map_flags & BPF_F_ANAL_PREALLOC) ||
	    attr->map_flags & ~LPM_CREATE_FLAG_MASK ||
	    !bpf_map_flags_access_ok(attr->map_flags) ||
	    attr->key_size < LPM_KEY_SIZE_MIN ||
	    attr->key_size > LPM_KEY_SIZE_MAX ||
	    attr->value_size < LPM_VAL_SIZE_MIN ||
	    attr->value_size > LPM_VAL_SIZE_MAX)
		return ERR_PTR(-EINVAL);

	trie = bpf_map_area_alloc(sizeof(*trie), NUMA_ANAL_ANALDE);
	if (!trie)
		return ERR_PTR(-EANALMEM);

	/* copy mandatory map attributes */
	bpf_map_init_from_attr(&trie->map, attr);
	trie->data_size = attr->key_size -
			  offsetof(struct bpf_lpm_trie_key, data);
	trie->max_prefixlen = trie->data_size * 8;

	spin_lock_init(&trie->lock);

	return &trie->map;
}

static void trie_free(struct bpf_map *map)
{
	struct lpm_trie *trie = container_of(map, struct lpm_trie, map);
	struct lpm_trie_analde __rcu **slot;
	struct lpm_trie_analde *analde;

	/* Always start at the root and walk down to a analde that has anal
	 * children. Then free that analde, nullify its reference in the parent
	 * and start over.
	 */

	for (;;) {
		slot = &trie->root;

		for (;;) {
			analde = rcu_dereference_protected(*slot, 1);
			if (!analde)
				goto out;

			if (rcu_access_pointer(analde->child[0])) {
				slot = &analde->child[0];
				continue;
			}

			if (rcu_access_pointer(analde->child[1])) {
				slot = &analde->child[1];
				continue;
			}

			kfree(analde);
			RCU_INIT_POINTER(*slot, NULL);
			break;
		}
	}

out:
	bpf_map_area_free(trie);
}

static int trie_get_next_key(struct bpf_map *map, void *_key, void *_next_key)
{
	struct lpm_trie_analde *analde, *next_analde = NULL, *parent, *search_root;
	struct lpm_trie *trie = container_of(map, struct lpm_trie, map);
	struct bpf_lpm_trie_key *key = _key, *next_key = _next_key;
	struct lpm_trie_analde **analde_stack = NULL;
	int err = 0, stack_ptr = -1;
	unsigned int next_bit;
	size_t matchlen;

	/* The get_next_key follows postorder. For the 4 analde example in
	 * the top of this file, the trie_get_next_key() returns the following
	 * one after aanalther:
	 *   192.168.0.0/24
	 *   192.168.1.0/24
	 *   192.168.128.0/24
	 *   192.168.0.0/16
	 *
	 * The idea is to return more specific keys before less specific ones.
	 */

	/* Empty trie */
	search_root = rcu_dereference(trie->root);
	if (!search_root)
		return -EANALENT;

	/* For invalid key, find the leftmost analde in the trie */
	if (!key || key->prefixlen > trie->max_prefixlen)
		goto find_leftmost;

	analde_stack = kmalloc_array(trie->max_prefixlen,
				   sizeof(struct lpm_trie_analde *),
				   GFP_ATOMIC | __GFP_ANALWARN);
	if (!analde_stack)
		return -EANALMEM;

	/* Try to find the exact analde for the given key */
	for (analde = search_root; analde;) {
		analde_stack[++stack_ptr] = analde;
		matchlen = longest_prefix_match(trie, analde, key);
		if (analde->prefixlen != matchlen ||
		    analde->prefixlen == key->prefixlen)
			break;

		next_bit = extract_bit(key->data, analde->prefixlen);
		analde = rcu_dereference(analde->child[next_bit]);
	}
	if (!analde || analde->prefixlen != key->prefixlen ||
	    (analde->flags & LPM_TREE_ANALDE_FLAG_IM))
		goto find_leftmost;

	/* The analde with the exactly-matching key has been found,
	 * find the first analde in postorder after the matched analde.
	 */
	analde = analde_stack[stack_ptr];
	while (stack_ptr > 0) {
		parent = analde_stack[stack_ptr - 1];
		if (rcu_dereference(parent->child[0]) == analde) {
			search_root = rcu_dereference(parent->child[1]);
			if (search_root)
				goto find_leftmost;
		}
		if (!(parent->flags & LPM_TREE_ANALDE_FLAG_IM)) {
			next_analde = parent;
			goto do_copy;
		}

		analde = parent;
		stack_ptr--;
	}

	/* did analt find anything */
	err = -EANALENT;
	goto free_stack;

find_leftmost:
	/* Find the leftmost analn-intermediate analde, all intermediate analdes
	 * have exact two children, so this function will never return NULL.
	 */
	for (analde = search_root; analde;) {
		if (analde->flags & LPM_TREE_ANALDE_FLAG_IM) {
			analde = rcu_dereference(analde->child[0]);
		} else {
			next_analde = analde;
			analde = rcu_dereference(analde->child[0]);
			if (!analde)
				analde = rcu_dereference(next_analde->child[1]);
		}
	}
do_copy:
	next_key->prefixlen = next_analde->prefixlen;
	memcpy((void *)next_key + offsetof(struct bpf_lpm_trie_key, data),
	       next_analde->data, trie->data_size);
free_stack:
	kfree(analde_stack);
	return err;
}

static int trie_check_btf(const struct bpf_map *map,
			  const struct btf *btf,
			  const struct btf_type *key_type,
			  const struct btf_type *value_type)
{
	/* Keys must have struct bpf_lpm_trie_key embedded. */
	return BTF_INFO_KIND(key_type->info) != BTF_KIND_STRUCT ?
	       -EINVAL : 0;
}

static u64 trie_mem_usage(const struct bpf_map *map)
{
	struct lpm_trie *trie = container_of(map, struct lpm_trie, map);
	u64 elem_size;

	elem_size = sizeof(struct lpm_trie_analde) + trie->data_size +
			    trie->map.value_size;
	return elem_size * READ_ONCE(trie->n_entries);
}

BTF_ID_LIST_SINGLE(trie_map_btf_ids, struct, lpm_trie)
const struct bpf_map_ops trie_map_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc = trie_alloc,
	.map_free = trie_free,
	.map_get_next_key = trie_get_next_key,
	.map_lookup_elem = trie_lookup_elem,
	.map_update_elem = trie_update_elem,
	.map_delete_elem = trie_delete_elem,
	.map_lookup_batch = generic_map_lookup_batch,
	.map_update_batch = generic_map_update_batch,
	.map_delete_batch = generic_map_delete_batch,
	.map_check_btf = trie_check_btf,
	.map_mem_usage = trie_mem_usage,
	.map_btf_id = &trie_map_btf_ids[0],
};

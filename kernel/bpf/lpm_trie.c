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

/* Intermediate yesde */
#define LPM_TREE_NODE_FLAG_IM BIT(0)

struct lpm_trie_yesde;

struct lpm_trie_yesde {
	struct rcu_head rcu;
	struct lpm_trie_yesde __rcu	*child[2];
	u32				prefixlen;
	u32				flags;
	u8				data[0];
};

struct lpm_trie {
	struct bpf_map			map;
	struct lpm_trie_yesde __rcu	*root;
	size_t				n_entries;
	size_t				max_prefixlen;
	size_t				data_size;
	raw_spinlock_t			lock;
};

/* This trie implements a longest prefix match algorithm that can be used to
 * match IP addresses to a stored set of ranges.
 *
 * Data stored in @data of struct bpf_lpm_key and struct lpm_trie_yesde is
 * interpreted as big endian, so data[0] stores the most significant byte.
 *
 * Match ranges are internally stored in instances of struct lpm_trie_yesde
 * which each contain their prefix length as well as two pointers that may
 * lead to more yesdes containing more specific matches. Each yesde also stores
 * a value that is defined by and returned to userspace via the update_elem
 * and lookup functions.
 *
 * For instance, let's start with a trie that was created with a prefix length
 * of 32, so it can be used for IPv4 addresses, and one single element that
 * matches 192.168.0.0/16. The data array would hence contain
 * [0xc0, 0xa8, 0x00, 0x00] in big-endian yestation. This documentation will
 * stick to IP-address yestation for readability though.
 *
 * As the trie is empty initially, the new yesde (1) will be places as root
 * yesde, deyested as (R) in the example below. As there are yes other yesde, both
 * child pointers are %NULL.
 *
 *              +----------------+
 *              |       (1)  (R) |
 *              | 192.168.0.0/16 |
 *              |    value: 1    |
 *              |   [0]    [1]   |
 *              +----------------+
 *
 * Next, let's add a new yesde (2) matching 192.168.0.0/24. As there is already
 * a yesde with the same data and a smaller prefix (ie, a less specific one),
 * yesde (2) will become a child of (1). In child index depends on the next bit
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
 * The child[1] slot of (1) could be filled with ayesther yesde which has bit #17
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
 * Let's add ayesther yesde (4) to the game for 192.168.1.0/24. In order to place
 * it, yesde (1) is looked at first, and because (4) of the semantics laid out
 * above (bit #17 is 0), it would yesrmally be attached to (1) as child[0].
 * However, that slot is already allocated, so a new yesde is needed in between.
 * That yesde does yest have a value attached to it and it will never be
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
 * An intermediate yesde will be turned into a 'real' yesde on demand. In the
 * example above, (4) would be re-used if 192.168.0.0/23 is added to the trie.
 *
 * A fully populated trie would have a height of 32 yesdes, as the trie was
 * created with a prefix length of 32.
 *
 * The lookup starts at the root yesde. If the current yesde matches and if there
 * is a child that can be used to become more specific, the trie is traversed
 * downwards. The last yesde in the traversal that is a yesn-intermediate one is
 * returned.
 */

static inline int extract_bit(const u8 *data, size_t index)
{
	return !!(data[index / 8] & (1 << (7 - (index % 8))));
}

/**
 * longest_prefix_match() - determine the longest prefix
 * @trie:	The trie to get internal sizes from
 * @yesde:	The yesde to operate on
 * @key:	The key to compare to @yesde
 *
 * Determine the longest prefix of @yesde that matches the bits in @key.
 */
static size_t longest_prefix_match(const struct lpm_trie *trie,
				   const struct lpm_trie_yesde *yesde,
				   const struct bpf_lpm_trie_key *key)
{
	u32 limit = min(yesde->prefixlen, key->prefixlen);
	u32 prefixlen = 0, i = 0;

	BUILD_BUG_ON(offsetof(struct lpm_trie_yesde, data) % sizeof(u32));
	BUILD_BUG_ON(offsetof(struct bpf_lpm_trie_key, data) % sizeof(u32));

#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && defined(CONFIG_64BIT)

	/* data_size >= 16 has very small probability.
	 * We do yest use a loop for optimal code generation.
	 */
	if (trie->data_size >= 8) {
		u64 diff = be64_to_cpu(*(__be64 *)yesde->data ^
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
		u32 diff = be32_to_cpu(*(__be32 *)&yesde->data[i] ^
				       *(__be32 *)&key->data[i]);

		prefixlen += 32 - fls(diff);
		if (prefixlen >= limit)
			return limit;
		if (diff)
			return prefixlen;
		i += 4;
	}

	if (trie->data_size >= i + 2) {
		u16 diff = be16_to_cpu(*(__be16 *)&yesde->data[i] ^
				       *(__be16 *)&key->data[i]);

		prefixlen += 16 - fls(diff);
		if (prefixlen >= limit)
			return limit;
		if (diff)
			return prefixlen;
		i += 2;
	}

	if (trie->data_size >= i + 1) {
		prefixlen += 8 - fls(yesde->data[i] ^ key->data[i]);

		if (prefixlen >= limit)
			return limit;
	}

	return prefixlen;
}

/* Called from syscall or from eBPF program */
static void *trie_lookup_elem(struct bpf_map *map, void *_key)
{
	struct lpm_trie *trie = container_of(map, struct lpm_trie, map);
	struct lpm_trie_yesde *yesde, *found = NULL;
	struct bpf_lpm_trie_key *key = _key;

	/* Start walking the trie from the root yesde ... */

	for (yesde = rcu_dereference(trie->root); yesde;) {
		unsigned int next_bit;
		size_t matchlen;

		/* Determine the longest prefix of @yesde that matches @key.
		 * If it's the maximum possible prefix for this trie, we have
		 * an exact match and can return it directly.
		 */
		matchlen = longest_prefix_match(trie, yesde, key);
		if (matchlen == trie->max_prefixlen) {
			found = yesde;
			break;
		}

		/* If the number of bits that match is smaller than the prefix
		 * length of @yesde, bail out and return the yesde we have seen
		 * last in the traversal (ie, the parent).
		 */
		if (matchlen < yesde->prefixlen)
			break;

		/* Consider this yesde as return candidate unless it is an
		 * artificially added intermediate one.
		 */
		if (!(yesde->flags & LPM_TREE_NODE_FLAG_IM))
			found = yesde;

		/* If the yesde match is fully satisfied, let's see if we can
		 * become more specific. Determine the next bit in the key and
		 * traverse down.
		 */
		next_bit = extract_bit(key->data, yesde->prefixlen);
		yesde = rcu_dereference(yesde->child[next_bit]);
	}

	if (!found)
		return NULL;

	return found->data + trie->data_size;
}

static struct lpm_trie_yesde *lpm_trie_yesde_alloc(const struct lpm_trie *trie,
						 const void *value)
{
	struct lpm_trie_yesde *yesde;
	size_t size = sizeof(struct lpm_trie_yesde) + trie->data_size;

	if (value)
		size += trie->map.value_size;

	yesde = kmalloc_yesde(size, GFP_ATOMIC | __GFP_NOWARN,
			    trie->map.numa_yesde);
	if (!yesde)
		return NULL;

	yesde->flags = 0;

	if (value)
		memcpy(yesde->data + trie->data_size, value,
		       trie->map.value_size);

	return yesde;
}

/* Called from syscall or from eBPF program */
static int trie_update_elem(struct bpf_map *map,
			    void *_key, void *value, u64 flags)
{
	struct lpm_trie *trie = container_of(map, struct lpm_trie, map);
	struct lpm_trie_yesde *yesde, *im_yesde = NULL, *new_yesde = NULL;
	struct lpm_trie_yesde __rcu **slot;
	struct bpf_lpm_trie_key *key = _key;
	unsigned long irq_flags;
	unsigned int next_bit;
	size_t matchlen = 0;
	int ret = 0;

	if (unlikely(flags > BPF_EXIST))
		return -EINVAL;

	if (key->prefixlen > trie->max_prefixlen)
		return -EINVAL;

	raw_spin_lock_irqsave(&trie->lock, irq_flags);

	/* Allocate and fill a new yesde */

	if (trie->n_entries == trie->map.max_entries) {
		ret = -ENOSPC;
		goto out;
	}

	new_yesde = lpm_trie_yesde_alloc(trie, value);
	if (!new_yesde) {
		ret = -ENOMEM;
		goto out;
	}

	trie->n_entries++;

	new_yesde->prefixlen = key->prefixlen;
	RCU_INIT_POINTER(new_yesde->child[0], NULL);
	RCU_INIT_POINTER(new_yesde->child[1], NULL);
	memcpy(new_yesde->data, key->data, trie->data_size);

	/* Now find a slot to attach the new yesde. To do that, walk the tree
	 * from the root and match as many bits as possible for each yesde until
	 * we either find an empty slot or a slot that needs to be replaced by
	 * an intermediate yesde.
	 */
	slot = &trie->root;

	while ((yesde = rcu_dereference_protected(*slot,
					lockdep_is_held(&trie->lock)))) {
		matchlen = longest_prefix_match(trie, yesde, key);

		if (yesde->prefixlen != matchlen ||
		    yesde->prefixlen == key->prefixlen ||
		    yesde->prefixlen == trie->max_prefixlen)
			break;

		next_bit = extract_bit(key->data, yesde->prefixlen);
		slot = &yesde->child[next_bit];
	}

	/* If the slot is empty (a free child pointer or an empty root),
	 * simply assign the @new_yesde to that slot and be done.
	 */
	if (!yesde) {
		rcu_assign_pointer(*slot, new_yesde);
		goto out;
	}

	/* If the slot we picked already exists, replace it with @new_yesde
	 * which already has the correct data array set.
	 */
	if (yesde->prefixlen == matchlen) {
		new_yesde->child[0] = yesde->child[0];
		new_yesde->child[1] = yesde->child[1];

		if (!(yesde->flags & LPM_TREE_NODE_FLAG_IM))
			trie->n_entries--;

		rcu_assign_pointer(*slot, new_yesde);
		kfree_rcu(yesde, rcu);

		goto out;
	}

	/* If the new yesde matches the prefix completely, it must be inserted
	 * as an ancestor. Simply insert it between @yesde and *@slot.
	 */
	if (matchlen == key->prefixlen) {
		next_bit = extract_bit(yesde->data, matchlen);
		rcu_assign_pointer(new_yesde->child[next_bit], yesde);
		rcu_assign_pointer(*slot, new_yesde);
		goto out;
	}

	im_yesde = lpm_trie_yesde_alloc(trie, NULL);
	if (!im_yesde) {
		ret = -ENOMEM;
		goto out;
	}

	im_yesde->prefixlen = matchlen;
	im_yesde->flags |= LPM_TREE_NODE_FLAG_IM;
	memcpy(im_yesde->data, yesde->data, trie->data_size);

	/* Now determine which child to install in which slot */
	if (extract_bit(key->data, matchlen)) {
		rcu_assign_pointer(im_yesde->child[0], yesde);
		rcu_assign_pointer(im_yesde->child[1], new_yesde);
	} else {
		rcu_assign_pointer(im_yesde->child[0], new_yesde);
		rcu_assign_pointer(im_yesde->child[1], yesde);
	}

	/* Finally, assign the intermediate yesde to the determined spot */
	rcu_assign_pointer(*slot, im_yesde);

out:
	if (ret) {
		if (new_yesde)
			trie->n_entries--;

		kfree(new_yesde);
		kfree(im_yesde);
	}

	raw_spin_unlock_irqrestore(&trie->lock, irq_flags);

	return ret;
}

/* Called from syscall or from eBPF program */
static int trie_delete_elem(struct bpf_map *map, void *_key)
{
	struct lpm_trie *trie = container_of(map, struct lpm_trie, map);
	struct bpf_lpm_trie_key *key = _key;
	struct lpm_trie_yesde __rcu **trim, **trim2;
	struct lpm_trie_yesde *yesde, *parent;
	unsigned long irq_flags;
	unsigned int next_bit;
	size_t matchlen = 0;
	int ret = 0;

	if (key->prefixlen > trie->max_prefixlen)
		return -EINVAL;

	raw_spin_lock_irqsave(&trie->lock, irq_flags);

	/* Walk the tree looking for an exact key/length match and keeping
	 * track of the path we traverse.  We will need to kyesw the yesde
	 * we wish to delete, and the slot that points to the yesde we want
	 * to delete.  We may also need to kyesw the yesdes parent and the
	 * slot that contains it.
	 */
	trim = &trie->root;
	trim2 = trim;
	parent = NULL;
	while ((yesde = rcu_dereference_protected(
		       *trim, lockdep_is_held(&trie->lock)))) {
		matchlen = longest_prefix_match(trie, yesde, key);

		if (yesde->prefixlen != matchlen ||
		    yesde->prefixlen == key->prefixlen)
			break;

		parent = yesde;
		trim2 = trim;
		next_bit = extract_bit(key->data, yesde->prefixlen);
		trim = &yesde->child[next_bit];
	}

	if (!yesde || yesde->prefixlen != key->prefixlen ||
	    yesde->prefixlen != matchlen ||
	    (yesde->flags & LPM_TREE_NODE_FLAG_IM)) {
		ret = -ENOENT;
		goto out;
	}

	trie->n_entries--;

	/* If the yesde we are removing has two children, simply mark it
	 * as intermediate and we are done.
	 */
	if (rcu_access_pointer(yesde->child[0]) &&
	    rcu_access_pointer(yesde->child[1])) {
		yesde->flags |= LPM_TREE_NODE_FLAG_IM;
		goto out;
	}

	/* If the parent of the yesde we are about to delete is an intermediate
	 * yesde, and the deleted yesde doesn't have any children, we can delete
	 * the intermediate parent as well and promote its other child
	 * up the tree.  Doing this maintains the invariant that all
	 * intermediate yesdes have exactly 2 children and that there are yes
	 * unnecessary intermediate yesdes in the tree.
	 */
	if (parent && (parent->flags & LPM_TREE_NODE_FLAG_IM) &&
	    !yesde->child[0] && !yesde->child[1]) {
		if (yesde == rcu_access_pointer(parent->child[0]))
			rcu_assign_pointer(
				*trim2, rcu_access_pointer(parent->child[1]));
		else
			rcu_assign_pointer(
				*trim2, rcu_access_pointer(parent->child[0]));
		kfree_rcu(parent, rcu);
		kfree_rcu(yesde, rcu);
		goto out;
	}

	/* The yesde we are removing has either zero or one child. If there
	 * is a child, move it into the removed yesde's slot then delete
	 * the yesde.  Otherwise just clear the slot and delete the yesde.
	 */
	if (yesde->child[0])
		rcu_assign_pointer(*trim, rcu_access_pointer(yesde->child[0]));
	else if (yesde->child[1])
		rcu_assign_pointer(*trim, rcu_access_pointer(yesde->child[1]));
	else
		RCU_INIT_POINTER(*trim, NULL);
	kfree_rcu(yesde, rcu);

out:
	raw_spin_unlock_irqrestore(&trie->lock, irq_flags);

	return ret;
}

#define LPM_DATA_SIZE_MAX	256
#define LPM_DATA_SIZE_MIN	1

#define LPM_VAL_SIZE_MAX	(KMALLOC_MAX_SIZE - LPM_DATA_SIZE_MAX - \
				 sizeof(struct lpm_trie_yesde))
#define LPM_VAL_SIZE_MIN	1

#define LPM_KEY_SIZE(X)		(sizeof(struct bpf_lpm_trie_key) + (X))
#define LPM_KEY_SIZE_MAX	LPM_KEY_SIZE(LPM_DATA_SIZE_MAX)
#define LPM_KEY_SIZE_MIN	LPM_KEY_SIZE(LPM_DATA_SIZE_MIN)

#define LPM_CREATE_FLAG_MASK	(BPF_F_NO_PREALLOC | BPF_F_NUMA_NODE |	\
				 BPF_F_ACCESS_MASK)

static struct bpf_map *trie_alloc(union bpf_attr *attr)
{
	struct lpm_trie *trie;
	u64 cost = sizeof(*trie), cost_per_yesde;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return ERR_PTR(-EPERM);

	/* check sanity of attributes */
	if (attr->max_entries == 0 ||
	    !(attr->map_flags & BPF_F_NO_PREALLOC) ||
	    attr->map_flags & ~LPM_CREATE_FLAG_MASK ||
	    !bpf_map_flags_access_ok(attr->map_flags) ||
	    attr->key_size < LPM_KEY_SIZE_MIN ||
	    attr->key_size > LPM_KEY_SIZE_MAX ||
	    attr->value_size < LPM_VAL_SIZE_MIN ||
	    attr->value_size > LPM_VAL_SIZE_MAX)
		return ERR_PTR(-EINVAL);

	trie = kzalloc(sizeof(*trie), GFP_USER | __GFP_NOWARN);
	if (!trie)
		return ERR_PTR(-ENOMEM);

	/* copy mandatory map attributes */
	bpf_map_init_from_attr(&trie->map, attr);
	trie->data_size = attr->key_size -
			  offsetof(struct bpf_lpm_trie_key, data);
	trie->max_prefixlen = trie->data_size * 8;

	cost_per_yesde = sizeof(struct lpm_trie_yesde) +
			attr->value_size + trie->data_size;
	cost += (u64) attr->max_entries * cost_per_yesde;

	ret = bpf_map_charge_init(&trie->map.memory, cost);
	if (ret)
		goto out_err;

	raw_spin_lock_init(&trie->lock);

	return &trie->map;
out_err:
	kfree(trie);
	return ERR_PTR(ret);
}

static void trie_free(struct bpf_map *map)
{
	struct lpm_trie *trie = container_of(map, struct lpm_trie, map);
	struct lpm_trie_yesde __rcu **slot;
	struct lpm_trie_yesde *yesde;

	/* Wait for outstanding programs to complete
	 * update/lookup/delete/get_next_key and free the trie.
	 */
	synchronize_rcu();

	/* Always start at the root and walk down to a yesde that has yes
	 * children. Then free that yesde, nullify its reference in the parent
	 * and start over.
	 */

	for (;;) {
		slot = &trie->root;

		for (;;) {
			yesde = rcu_dereference_protected(*slot, 1);
			if (!yesde)
				goto out;

			if (rcu_access_pointer(yesde->child[0])) {
				slot = &yesde->child[0];
				continue;
			}

			if (rcu_access_pointer(yesde->child[1])) {
				slot = &yesde->child[1];
				continue;
			}

			kfree(yesde);
			RCU_INIT_POINTER(*slot, NULL);
			break;
		}
	}

out:
	kfree(trie);
}

static int trie_get_next_key(struct bpf_map *map, void *_key, void *_next_key)
{
	struct lpm_trie_yesde *yesde, *next_yesde = NULL, *parent, *search_root;
	struct lpm_trie *trie = container_of(map, struct lpm_trie, map);
	struct bpf_lpm_trie_key *key = _key, *next_key = _next_key;
	struct lpm_trie_yesde **yesde_stack = NULL;
	int err = 0, stack_ptr = -1;
	unsigned int next_bit;
	size_t matchlen;

	/* The get_next_key follows postorder. For the 4 yesde example in
	 * the top of this file, the trie_get_next_key() returns the following
	 * one after ayesther:
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
		return -ENOENT;

	/* For invalid key, find the leftmost yesde in the trie */
	if (!key || key->prefixlen > trie->max_prefixlen)
		goto find_leftmost;

	yesde_stack = kmalloc_array(trie->max_prefixlen,
				   sizeof(struct lpm_trie_yesde *),
				   GFP_ATOMIC | __GFP_NOWARN);
	if (!yesde_stack)
		return -ENOMEM;

	/* Try to find the exact yesde for the given key */
	for (yesde = search_root; yesde;) {
		yesde_stack[++stack_ptr] = yesde;
		matchlen = longest_prefix_match(trie, yesde, key);
		if (yesde->prefixlen != matchlen ||
		    yesde->prefixlen == key->prefixlen)
			break;

		next_bit = extract_bit(key->data, yesde->prefixlen);
		yesde = rcu_dereference(yesde->child[next_bit]);
	}
	if (!yesde || yesde->prefixlen != key->prefixlen ||
	    (yesde->flags & LPM_TREE_NODE_FLAG_IM))
		goto find_leftmost;

	/* The yesde with the exactly-matching key has been found,
	 * find the first yesde in postorder after the matched yesde.
	 */
	yesde = yesde_stack[stack_ptr];
	while (stack_ptr > 0) {
		parent = yesde_stack[stack_ptr - 1];
		if (rcu_dereference(parent->child[0]) == yesde) {
			search_root = rcu_dereference(parent->child[1]);
			if (search_root)
				goto find_leftmost;
		}
		if (!(parent->flags & LPM_TREE_NODE_FLAG_IM)) {
			next_yesde = parent;
			goto do_copy;
		}

		yesde = parent;
		stack_ptr--;
	}

	/* did yest find anything */
	err = -ENOENT;
	goto free_stack;

find_leftmost:
	/* Find the leftmost yesn-intermediate yesde, all intermediate yesdes
	 * have exact two children, so this function will never return NULL.
	 */
	for (yesde = search_root; yesde;) {
		if (yesde->flags & LPM_TREE_NODE_FLAG_IM) {
			yesde = rcu_dereference(yesde->child[0]);
		} else {
			next_yesde = yesde;
			yesde = rcu_dereference(yesde->child[0]);
			if (!yesde)
				yesde = rcu_dereference(next_yesde->child[1]);
		}
	}
do_copy:
	next_key->prefixlen = next_yesde->prefixlen;
	memcpy((void *)next_key + offsetof(struct bpf_lpm_trie_key, data),
	       next_yesde->data, trie->data_size);
free_stack:
	kfree(yesde_stack);
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

const struct bpf_map_ops trie_map_ops = {
	.map_alloc = trie_alloc,
	.map_free = trie_free,
	.map_get_next_key = trie_get_next_key,
	.map_lookup_elem = trie_lookup_elem,
	.map_update_elem = trie_update_elem,
	.map_delete_elem = trie_delete_elem,
	.map_check_btf = trie_check_btf,
};

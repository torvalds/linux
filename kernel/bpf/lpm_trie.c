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

/* Intermediate node */
#define LPM_TREE_NODE_FLAG_IM BIT(0)

struct lpm_trie_node;

struct lpm_trie_node {
	struct rcu_head rcu;
	struct lpm_trie_node __rcu	*child[2];
	u32				prefixlen;
	u32				flags;
	u8				data[];
};

struct lpm_trie {
	struct bpf_map			map;
	struct lpm_trie_node __rcu	*root;
	size_t				n_entries;
	size_t				max_prefixlen;
	size_t				data_size;
	spinlock_t			lock;
};

/* This trie implements a longest prefix match algorithm that can be used to
 * match IP addresses to a stored set of ranges.
 *
 * Data stored in @data of struct bpf_lpm_key and struct lpm_trie_node is
 * interpreted as big endian, so data[0] stores the most significant byte.
 *
 * Match ranges are internally stored in instances of struct lpm_trie_node
 * which each contain their prefix length as well as two pointers that may
 * lead to more nodes containing more specific matches. Each node also stores
 * a value that is defined by and returned to userspace via the update_elem
 * and lookup functions.
 *
 * For instance, let's start with a trie that was created with a prefix length
 * of 32, so it can be used for IPv4 addresses, and one single element that
 * matches 192.168.0.0/16. The data array would hence contain
 * [0xc0, 0xa8, 0x00, 0x00] in big-endian notation. This documentation will
 * stick to IP-address notation for readability though.
 *
 * As the trie is empty initially, the new node (1) will be places as root
 * node, denoted as (R) in the example below. As there are no other node, both
 * child pointers are %NULL.
 *
 *              +----------------+
 *              |       (1)  (R) |
 *              | 192.168.0.0/16 |
 *              |    value: 1    |
 *              |   [0]    [1]   |
 *              +----------------+
 *
 * Next, let's add a new node (2) matching 192.168.0.0/24. As there is already
 * a node with the same data and a smaller prefix (ie, a less specific one),
 * node (2) will become a child of (1). In child index depends on the next bit
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
 * The child[1] slot of (1) could be filled with another node which has bit #17
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
 * Let's add another node (4) to the game for 192.168.1.0/24. In order to place
 * it, node (1) is looked at first, and because (4) of the semantics laid out
 * above (bit #17 is 0), it would normally be attached to (1) as child[0].
 * However, that slot is already allocated, so a new node is needed in between.
 * That node does not have a value attached to it and it will never be
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
 * An intermediate node will be turned into a 'real' node on demand. In the
 * example above, (4) would be re-used if 192.168.0.0/23 is added to the trie.
 *
 * A fully populated trie would have a height of 32 nodes, as the trie was
 * created with a prefix length of 32.
 *
 * The lookup starts at the root node. If the current node matches and if there
 * is a child that can be used to become more specific, the trie is traversed
 * downwards. The last node in the traversal that is a non-intermediate one is
 * returned.
 */

static inline int extract_bit(const u8 *data, size_t index)
{
	return !!(data[index / 8] & (1 << (7 - (index % 8))));
}

/**
 * longest_prefix_match() - determine the longest prefix
 * @trie:	The trie to get internal sizes from
 * @node:	The node to operate on
 * @key:	The key to compare to @node
 *
 * Determine the longest prefix of @node that matches the bits in @key.
 */
static size_t longest_prefix_match(const struct lpm_trie *trie,
				   const struct lpm_trie_node *node,
				   const struct bpf_lpm_trie_key *key)
{
	u32 limit = min(node->prefixlen, key->prefixlen);
	u32 prefixlen = 0, i = 0;

	BUILD_BUG_ON(offsetof(struct lpm_trie_node, data) % sizeof(u32));
	BUILD_BUG_ON(offsetof(struct bpf_lpm_trie_key, data) % sizeof(u32));

#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && defined(CONFIG_64BIT)

	/* data_size >= 16 has very small probability.
	 * We do not use a loop for optimal code generation.
	 */
	if (trie->data_size >= 8) {
		u64 diff = be64_to_cpu(*(__be64 *)node->data ^
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
		u32 diff = be32_to_cpu(*(__be32 *)&node->data[i] ^
				       *(__be32 *)&key->data[i]);

		prefixlen += 32 - fls(diff);
		if (prefixlen >= limit)
			return limit;
		if (diff)
			return prefixlen;
		i += 4;
	}

	if (trie->data_size >= i + 2) {
		u16 diff = be16_to_cpu(*(__be16 *)&node->data[i] ^
				       *(__be16 *)&key->data[i]);

		prefixlen += 16 - fls(diff);
		if (prefixlen >= limit)
			return limit;
		if (diff)
			return prefixlen;
		i += 2;
	}

	if (trie->data_size >= i + 1) {
		prefixlen += 8 - fls(node->data[i] ^ key->data[i]);

		if (prefixlen >= limit)
			return limit;
	}

	return prefixlen;
}

/* Called from syscall or from eBPF program */
static void *trie_lookup_elem(struct bpf_map *map, void *_key)
{
	struct lpm_trie *trie = container_of(map, struct lpm_trie, map);
	struct lpm_trie_node *node, *found = NULL;
	struct bpf_lpm_trie_key *key = _key;

	if (key->prefixlen > trie->max_prefixlen)
		return NULL;

	/* Start walking the trie from the root node ... */

	for (node = rcu_dereference_check(trie->root, rcu_read_lock_bh_held());
	     node;) {
		unsigned int next_bit;
		size_t matchlen;

		/* Determine the longest prefix of @node that matches @key.
		 * If it's the maximum possible prefix for this trie, we have
		 * an exact match and can return it directly.
		 */
		matchlen = longest_prefix_match(trie, node, key);
		if (matchlen == trie->max_prefixlen) {
			found = node;
			break;
		}

		/* If the number of bits that match is smaller than the prefix
		 * length of @node, bail out and return the node we have seen
		 * last in the traversal (ie, the parent).
		 */
		if (matchlen < node->prefixlen)
			break;

		/* Consider this node as return candidate unless it is an
		 * artificially added intermediate one.
		 */
		if (!(node->flags & LPM_TREE_NODE_FLAG_IM))
			found = node;

		/* If the node match is fully satisfied, let's see if we can
		 * become more specific. Determine the next bit in the key and
		 * traverse down.
		 */
		next_bit = extract_bit(key->data, node->prefixlen);
		node = rcu_dereference_check(node->child[next_bit],
					     rcu_read_lock_bh_held());
	}

	if (!found)
		return NULL;

	return found->data + trie->data_size;
}

static struct lpm_trie_node *lpm_trie_node_alloc(const struct lpm_trie *trie,
						 const void *value)
{
	struct lpm_trie_node *node;
	size_t size = sizeof(struct lpm_trie_node) + trie->data_size;

	if (value)
		size += trie->map.value_size;

	node = bpf_map_kmalloc_node(&trie->map, size, GFP_NOWAIT | __GFP_NOWARN,
				    trie->map.numa_node);
	if (!node)
		return NULL;

	node->flags = 0;

	if (value)
		memcpy(node->data + trie->data_size, value,
		       trie->map.value_size);

	return node;
}

/* Called from syscall or from eBPF program */
static int trie_update_elem(struct bpf_map *map,
			    void *_key, void *value, u64 flags)
{
	struct lpm_trie *trie = container_of(map, struct lpm_trie, map);
	struct lpm_trie_node *node, *im_node = NULL, *new_node = NULL;
	struct lpm_trie_node __rcu **slot;
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

	/* Allocate and fill a new node */

	if (trie->n_entries == trie->map.max_entries) {
		ret = -ENOSPC;
		goto out;
	}

	new_node = lpm_trie_node_alloc(trie, value);
	if (!new_node) {
		ret = -ENOMEM;
		goto out;
	}

	trie->n_entries++;

	new_node->prefixlen = key->prefixlen;
	RCU_INIT_POINTER(new_node->child[0], NULL);
	RCU_INIT_POINTER(new_node->child[1], NULL);
	memcpy(new_node->data, key->data, trie->data_size);

	/* Now find a slot to attach the new node. To do that, walk the tree
	 * from the root and match as many bits as possible for each node until
	 * we either find an empty slot or a slot that needs to be replaced by
	 * an intermediate node.
	 */
	slot = &trie->root;

	while ((node = rcu_dereference_protected(*slot,
					lockdep_is_held(&trie->lock)))) {
		matchlen = longest_prefix_match(trie, node, key);

		if (node->prefixlen != matchlen ||
		    node->prefixlen == key->prefixlen ||
		    node->prefixlen == trie->max_prefixlen)
			break;

		next_bit = extract_bit(key->data, node->prefixlen);
		slot = &node->child[next_bit];
	}

	/* If the slot is empty (a free child pointer or an empty root),
	 * simply assign the @new_node to that slot and be done.
	 */
	if (!node) {
		rcu_assign_pointer(*slot, new_node);
		goto out;
	}

	/* If the slot we picked already exists, replace it with @new_node
	 * which already has the correct data array set.
	 */
	if (node->prefixlen == matchlen) {
		new_node->child[0] = node->child[0];
		new_node->child[1] = node->child[1];

		if (!(node->flags & LPM_TREE_NODE_FLAG_IM))
			trie->n_entries--;

		rcu_assign_pointer(*slot, new_node);
		kfree_rcu(node, rcu);

		goto out;
	}

	/* If the new node matches the prefix completely, it must be inserted
	 * as an ancestor. Simply insert it between @node and *@slot.
	 */
	if (matchlen == key->prefixlen) {
		next_bit = extract_bit(node->data, matchlen);
		rcu_assign_pointer(new_node->child[next_bit], node);
		rcu_assign_pointer(*slot, new_node);
		goto out;
	}

	im_node = lpm_trie_node_alloc(trie, NULL);
	if (!im_node) {
		ret = -ENOMEM;
		goto out;
	}

	im_node->prefixlen = matchlen;
	im_node->flags |= LPM_TREE_NODE_FLAG_IM;
	memcpy(im_node->data, node->data, trie->data_size);

	/* Now determine which child to install in which slot */
	if (extract_bit(key->data, matchlen)) {
		rcu_assign_pointer(im_node->child[0], node);
		rcu_assign_pointer(im_node->child[1], new_node);
	} else {
		rcu_assign_pointer(im_node->child[0], new_node);
		rcu_assign_pointer(im_node->child[1], node);
	}

	/* Finally, assign the intermediate node to the determined slot */
	rcu_assign_pointer(*slot, im_node);

out:
	if (ret) {
		if (new_node)
			trie->n_entries--;

		kfree(new_node);
		kfree(im_node);
	}

	spin_unlock_irqrestore(&trie->lock, irq_flags);

	return ret;
}

/* Called from syscall or from eBPF program */
static int trie_delete_elem(struct bpf_map *map, void *_key)
{
	struct lpm_trie *trie = container_of(map, struct lpm_trie, map);
	struct bpf_lpm_trie_key *key = _key;
	struct lpm_trie_node __rcu **trim, **trim2;
	struct lpm_trie_node *node, *parent;
	unsigned long irq_flags;
	unsigned int next_bit;
	size_t matchlen = 0;
	int ret = 0;

	if (key->prefixlen > trie->max_prefixlen)
		return -EINVAL;

	spin_lock_irqsave(&trie->lock, irq_flags);

	/* Walk the tree looking for an exact key/length match and keeping
	 * track of the path we traverse.  We will need to know the node
	 * we wish to delete, and the slot that points to the node we want
	 * to delete.  We may also need to know the nodes parent and the
	 * slot that contains it.
	 */
	trim = &trie->root;
	trim2 = trim;
	parent = NULL;
	while ((node = rcu_dereference_protected(
		       *trim, lockdep_is_held(&trie->lock)))) {
		matchlen = longest_prefix_match(trie, node, key);

		if (node->prefixlen != matchlen ||
		    node->prefixlen == key->prefixlen)
			break;

		parent = node;
		trim2 = trim;
		next_bit = extract_bit(key->data, node->prefixlen);
		trim = &node->child[next_bit];
	}

	if (!node || node->prefixlen != key->prefixlen ||
	    node->prefixlen != matchlen ||
	    (node->flags & LPM_TREE_NODE_FLAG_IM)) {
		ret = -ENOENT;
		goto out;
	}

	trie->n_entries--;

	/* If the node we are removing has two children, simply mark it
	 * as intermediate and we are done.
	 */
	if (rcu_access_pointer(node->child[0]) &&
	    rcu_access_pointer(node->child[1])) {
		node->flags |= LPM_TREE_NODE_FLAG_IM;
		goto out;
	}

	/* If the parent of the node we are about to delete is an intermediate
	 * node, and the deleted node doesn't have any children, we can delete
	 * the intermediate parent as well and promote its other child
	 * up the tree.  Doing this maintains the invariant that all
	 * intermediate nodes have exactly 2 children and that there are no
	 * unnecessary intermediate nodes in the tree.
	 */
	if (parent && (parent->flags & LPM_TREE_NODE_FLAG_IM) &&
	    !node->child[0] && !node->child[1]) {
		if (node == rcu_access_pointer(parent->child[0]))
			rcu_assign_pointer(
				*trim2, rcu_access_pointer(parent->child[1]));
		else
			rcu_assign_pointer(
				*trim2, rcu_access_pointer(parent->child[0]));
		kfree_rcu(parent, rcu);
		kfree_rcu(node, rcu);
		goto out;
	}

	/* The node we are removing has either zero or one child. If there
	 * is a child, move it into the removed node's slot then delete
	 * the node.  Otherwise just clear the slot and delete the node.
	 */
	if (node->child[0])
		rcu_assign_pointer(*trim, rcu_access_pointer(node->child[0]));
	else if (node->child[1])
		rcu_assign_pointer(*trim, rcu_access_pointer(node->child[1]));
	else
		RCU_INIT_POINTER(*trim, NULL);
	kfree_rcu(node, rcu);

out:
	spin_unlock_irqrestore(&trie->lock, irq_flags);

	return ret;
}

#define LPM_DATA_SIZE_MAX	256
#define LPM_DATA_SIZE_MIN	1

#define LPM_VAL_SIZE_MAX	(KMALLOC_MAX_SIZE - LPM_DATA_SIZE_MAX - \
				 sizeof(struct lpm_trie_node))
#define LPM_VAL_SIZE_MIN	1

#define LPM_KEY_SIZE(X)		(sizeof(struct bpf_lpm_trie_key) + (X))
#define LPM_KEY_SIZE_MAX	LPM_KEY_SIZE(LPM_DATA_SIZE_MAX)
#define LPM_KEY_SIZE_MIN	LPM_KEY_SIZE(LPM_DATA_SIZE_MIN)

#define LPM_CREATE_FLAG_MASK	(BPF_F_NO_PREALLOC | BPF_F_NUMA_NODE |	\
				 BPF_F_ACCESS_MASK)

static struct bpf_map *trie_alloc(union bpf_attr *attr)
{
	struct lpm_trie *trie;

	if (!bpf_capable())
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

	trie = bpf_map_area_alloc(sizeof(*trie), NUMA_NO_NODE);
	if (!trie)
		return ERR_PTR(-ENOMEM);

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
	struct lpm_trie_node __rcu **slot;
	struct lpm_trie_node *node;

	/* Always start at the root and walk down to a node that has no
	 * children. Then free that node, nullify its reference in the parent
	 * and start over.
	 */

	for (;;) {
		slot = &trie->root;

		for (;;) {
			node = rcu_dereference_protected(*slot, 1);
			if (!node)
				goto out;

			if (rcu_access_pointer(node->child[0])) {
				slot = &node->child[0];
				continue;
			}

			if (rcu_access_pointer(node->child[1])) {
				slot = &node->child[1];
				continue;
			}

			kfree(node);
			RCU_INIT_POINTER(*slot, NULL);
			break;
		}
	}

out:
	bpf_map_area_free(trie);
}

static int trie_get_next_key(struct bpf_map *map, void *_key, void *_next_key)
{
	struct lpm_trie_node *node, *next_node = NULL, *parent, *search_root;
	struct lpm_trie *trie = container_of(map, struct lpm_trie, map);
	struct bpf_lpm_trie_key *key = _key, *next_key = _next_key;
	struct lpm_trie_node **node_stack = NULL;
	int err = 0, stack_ptr = -1;
	unsigned int next_bit;
	size_t matchlen;

	/* The get_next_key follows postorder. For the 4 node example in
	 * the top of this file, the trie_get_next_key() returns the following
	 * one after another:
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

	/* For invalid key, find the leftmost node in the trie */
	if (!key || key->prefixlen > trie->max_prefixlen)
		goto find_leftmost;

	node_stack = kmalloc_array(trie->max_prefixlen,
				   sizeof(struct lpm_trie_node *),
				   GFP_ATOMIC | __GFP_NOWARN);
	if (!node_stack)
		return -ENOMEM;

	/* Try to find the exact node for the given key */
	for (node = search_root; node;) {
		node_stack[++stack_ptr] = node;
		matchlen = longest_prefix_match(trie, node, key);
		if (node->prefixlen != matchlen ||
		    node->prefixlen == key->prefixlen)
			break;

		next_bit = extract_bit(key->data, node->prefixlen);
		node = rcu_dereference(node->child[next_bit]);
	}
	if (!node || node->prefixlen != key->prefixlen ||
	    (node->flags & LPM_TREE_NODE_FLAG_IM))
		goto find_leftmost;

	/* The node with the exactly-matching key has been found,
	 * find the first node in postorder after the matched node.
	 */
	node = node_stack[stack_ptr];
	while (stack_ptr > 0) {
		parent = node_stack[stack_ptr - 1];
		if (rcu_dereference(parent->child[0]) == node) {
			search_root = rcu_dereference(parent->child[1]);
			if (search_root)
				goto find_leftmost;
		}
		if (!(parent->flags & LPM_TREE_NODE_FLAG_IM)) {
			next_node = parent;
			goto do_copy;
		}

		node = parent;
		stack_ptr--;
	}

	/* did not find anything */
	err = -ENOENT;
	goto free_stack;

find_leftmost:
	/* Find the leftmost non-intermediate node, all intermediate nodes
	 * have exact two children, so this function will never return NULL.
	 */
	for (node = search_root; node;) {
		if (node->flags & LPM_TREE_NODE_FLAG_IM) {
			node = rcu_dereference(node->child[0]);
		} else {
			next_node = node;
			node = rcu_dereference(node->child[0]);
			if (!node)
				node = rcu_dereference(next_node->child[1]);
		}
	}
do_copy:
	next_key->prefixlen = next_node->prefixlen;
	memcpy((void *)next_key + offsetof(struct bpf_lpm_trie_key, data),
	       next_node->data, trie->data_size);
free_stack:
	kfree(node_stack);
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
	.map_btf_id = &trie_map_btf_ids[0],
};

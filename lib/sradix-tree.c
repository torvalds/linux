#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/gcd.h>
#include <linux/sradix-tree.h>

static inline int sradix_node_full(struct sradix_tree_root *root, struct sradix_tree_node *node)
{
	return node->fulls == root->stores_size || 
		(node->height == 1 && node->count == root->stores_size);
}

/*
 *	Extend a sradix tree so it can store key @index.
 */
static int sradix_tree_extend(struct sradix_tree_root *root, unsigned long index)
{
	struct sradix_tree_node *node;
	unsigned int height;

	if (unlikely(root->rnode == NULL)) {
		if (!(node = root->alloc()))
			return -ENOMEM;

		node->height = 1;
		root->rnode = node;
		root->height = 1;
	}

	/* Figure out what the height should be.  */
	height = root->height;
	index >>= root->shift * height;

	while (index) {
		index >>= root->shift;
		height++;
	}

	while (height > root->height) {
		unsigned int newheight;
		if (!(node = root->alloc()))
			return -ENOMEM;

		/* Increase the height.  */
		node->stores[0] = root->rnode;
		root->rnode->parent = node;
		if (root->extend)
			root->extend(node, root->rnode);

		newheight = root->height + 1;
		node->height = newheight;
		node->count = 1;
		if (sradix_node_full(root, root->rnode))
			node->fulls = 1;

		root->rnode = node;
		root->height = newheight;
	}

	return 0;
}

/*
 * Search the next item from the current node, that is not NULL
 * and can satify root->iter().
 */
void *sradix_tree_next(struct sradix_tree_root *root,
		       struct sradix_tree_node *node, unsigned long index,
		       int (*iter)(void *item, unsigned long height))
{
	unsigned long offset;
	void *item;

	if (unlikely(node == NULL)) {
		node = root->rnode;
		for (offset = 0; offset < root->stores_size; offset++) {
			item = node->stores[offset];
			if (item && (!iter || iter(item, node->height)))
				break;
		}

		if (unlikely(offset >= root->stores_size))
			return NULL;

		if (node->height == 1)
			return item;
		else
			goto go_down;
	}

	while (node) {
		offset = (index & root->mask) + 1;					
		for (;offset < root->stores_size; offset++) {
			item = node->stores[offset];
			if (item && (!iter || iter(item, node->height)))
				break;
		}

		if (offset < root->stores_size)
			break;

		node = node->parent;
		index >>= root->shift;
	}

	if (!node)
		return NULL;

	while (node->height > 1) {
go_down:
		node = item;
		for (offset = 0; offset < root->stores_size; offset++) {
			item = node->stores[offset];
			if (item && (!iter || iter(item, node->height)))
				break;
		}

		if (unlikely(offset >= root->stores_size))
			return NULL;
	}

	BUG_ON(offset > root->stores_size);

	return item;
}

/*
 * Blindly insert the item to the tree. Typically, we reuse the
 * first empty store item.
 */
int sradix_tree_enter(struct sradix_tree_root *root, void **item, int num)
{
	unsigned long index;
	unsigned int height;
	struct sradix_tree_node *node, *tmp = NULL;
	int offset, offset_saved;
	void **store = NULL;
	int error, i, j, shift;

go_on:
	index = root->min;

	if (root->enter_node && !sradix_node_full(root, root->enter_node)) {
		node = root->enter_node;
		BUG_ON((index >> (root->shift * root->height)));
	} else {
		node = root->rnode;
		if (node == NULL || (index >> (root->shift * root->height))
		    || sradix_node_full(root, node)) {
			error = sradix_tree_extend(root, index);
			if (error)
				return error;

			node = root->rnode;
		}
	}


	height = node->height;
	shift = (height - 1) * root->shift;
	offset = (index >> shift) & root->mask;
	while (shift > 0) {
		offset_saved = offset;
		for (; offset < root->stores_size; offset++) {
			store = &node->stores[offset];
			tmp = *store;

			if (!tmp || !sradix_node_full(root, tmp))
				break;
		}
		BUG_ON(offset >= root->stores_size);

		if (offset != offset_saved) {
			index += (offset - offset_saved) << shift;
			index &= ~((1UL << shift) - 1);
		}

		if (!tmp) {
			if (!(tmp = root->alloc()))
				return -ENOMEM;

			tmp->height = shift / root->shift;
			*store = tmp;
			tmp->parent = node;
			node->count++;
//			if (root->extend)
//				root->extend(node, tmp);
		}

		node = tmp;
		shift -= root->shift;
		offset = (index >> shift) & root->mask;
	}

	BUG_ON(node->height != 1);


	store = &node->stores[offset];
	for (i = 0, j = 0;
	      j < root->stores_size - node->count && 
	      i < root->stores_size - offset && j < num; i++) {
		if (!store[i]) {
			store[i] = item[j];
			if (root->assign)
				root->assign(node, index + i, item[j]);
			j++;
		}
	}

	node->count += j;
	root->num += j;
	num -= j;

	while (sradix_node_full(root, node)) {
		node = node->parent;
		if (!node)
			break;

		node->fulls++;
	}

	if (unlikely(!node)) {
		/* All nodes are full */
		root->min = 1 << (root->height * root->shift);
		root->enter_node = NULL;
	} else {
		root->min = index + i - 1;
		root->min |= (1UL << (node->height - 1)) - 1;
		root->min++;
		root->enter_node = node;
	}

	if (num) {
		item += j;
		goto go_on;
	}

	return 0;
}


/**
 *	sradix_tree_shrink    -    shrink height of a sradix tree to minimal
 *      @root		sradix tree root
 *  
 */
static inline void sradix_tree_shrink(struct sradix_tree_root *root)
{
	/* try to shrink tree height */
	while (root->height > 1) {
		struct sradix_tree_node *to_free = root->rnode;

		/*
		 * The candidate node has more than one child, or its child
		 * is not at the leftmost store, we cannot shrink.
		 */
		if (to_free->count != 1 || !to_free->stores[0])
			break;

		root->rnode = to_free->stores[0];
		root->rnode->parent = NULL;
		root->height--;
		if (unlikely(root->enter_node == to_free)) {
			root->enter_node = NULL;
		}
		root->free(to_free);
	}
}

/*
 * Del the item on the known leaf node and index
 */
void sradix_tree_delete_from_leaf(struct sradix_tree_root *root, 
				  struct sradix_tree_node *node, unsigned long index)
{
	unsigned int offset;
	struct sradix_tree_node *start, *end;

	BUG_ON(node->height != 1);

	start = node;
	while (node && !(--node->count))
		node = node->parent;

	end = node;
	if (!node) {
		root->rnode = NULL;
		root->height = 0;
		root->min = 0;
		root->num = 0;
		root->enter_node = NULL;
	} else {
		offset = (index >> (root->shift * (node->height - 1))) & root->mask;
		if (root->rm)
			root->rm(node, offset);
		node->stores[offset] = NULL;
		root->num--;
		if (root->min > index) {
			root->min = index;
			root->enter_node = node;
		}
	}

	if (start != end) {
		do {
			node = start;
			start = start->parent;
			if (unlikely(root->enter_node == node))
				root->enter_node = end;
			root->free(node);
		} while (start != end);

		/*
		 * Note that shrink may free "end", so enter_node still need to
		 * be checked inside.
		 */
		sradix_tree_shrink(root);
	} else if (node->count == root->stores_size - 1) {
		/* It WAS a full leaf node. Update the ancestors */
		node = node->parent;
		while (node) {
			node->fulls--;
			if (node->fulls != root->stores_size - 1)
				break;

			node = node->parent;
		}
	}
}

void *sradix_tree_lookup(struct sradix_tree_root *root, unsigned long index)
{
	unsigned int height, offset;
	struct sradix_tree_node *node;
	int shift;

	node = root->rnode;
	if (node == NULL || (index >> (root->shift * root->height)))
		return NULL;

	height = root->height;
	shift = (height - 1) * root->shift;

	do {
		offset = (index >> shift) & root->mask;
		node = node->stores[offset];
		if (!node)
			return NULL;

		shift -= root->shift;
	} while (shift >= 0);

	return node;
}

/*
 * Return the item if it exists, otherwise create it in place
 * and return the created item.
 */
void *sradix_tree_lookup_create(struct sradix_tree_root *root, 
			unsigned long index, void *(*item_alloc)(void))
{
	unsigned int height, offset;
	struct sradix_tree_node *node, *tmp;
	void *item;
	int shift, error;

	if (root->rnode == NULL || (index >> (root->shift * root->height))) {
		if (item_alloc) {
			error = sradix_tree_extend(root, index);
			if (error)
				return NULL;
		} else {
			return NULL;
		}
	}

	node = root->rnode;
	height = root->height;
	shift = (height - 1) * root->shift;

	do {
		offset = (index >> shift) & root->mask;
		if (!node->stores[offset]) {
			if (!(tmp = root->alloc()))
				return NULL;

			tmp->height = shift / root->shift;
			node->stores[offset] = tmp;
			tmp->parent = node;
			node->count++;
			node = tmp;
		} else {
			node = node->stores[offset];
		}

		shift -= root->shift;
	} while (shift > 0);

	BUG_ON(node->height != 1);
	offset = index & root->mask;
	if (node->stores[offset]) {
		return node->stores[offset];
	} else if (item_alloc) {
		if (!(item = item_alloc()))
			return NULL;

		node->stores[offset] = item;

		/*
		 * NOTE: we do NOT call root->assign here, since this item is
		 * newly created by us having no meaning. Caller can call this
		 * if it's necessary to do so.
		 */

		node->count++;
		root->num++;

		while (sradix_node_full(root, node)) {
			node = node->parent;
			if (!node)
				break;

			node->fulls++;
		}

		if (unlikely(!node)) {
			/* All nodes are full */
			root->min = 1 << (root->height * root->shift);
		} else {
			if (root->min == index) {
				root->min |= (1UL << (node->height - 1)) - 1;
				root->min++;
				root->enter_node = node;
			}
		}

		return item;
	} else {
		return NULL;
	}

}

int sradix_tree_delete(struct sradix_tree_root *root, unsigned long index)
{
	unsigned int height, offset;
	struct sradix_tree_node *node;
	int shift;

	node = root->rnode;
	if (node == NULL || (index >> (root->shift * root->height)))
		return -ENOENT;

	height = root->height;
	shift = (height - 1) * root->shift;

	do {
		offset = (index >> shift) & root->mask;
		node = node->stores[offset];
		if (!node)
			return -ENOENT;

		shift -= root->shift;
	} while (shift > 0);

	offset = index & root->mask;
	if (!node->stores[offset])
		return -ENOENT;

	sradix_tree_delete_from_leaf(root, node, index);

	return 0;
}

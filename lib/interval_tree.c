#include <linux/init.h>
#include <linux/interval_tree.h>

/* Callbacks for augmented rbtree insert and remove */

static inline unsigned long
compute_subtree_last(struct interval_tree_node *node)
{
	unsigned long max = node->last, subtree_last;
	if (node->rb.rb_left) {
		subtree_last = rb_entry(node->rb.rb_left,
			struct interval_tree_node, rb)->__subtree_last;
		if (max < subtree_last)
			max = subtree_last;
	}
	if (node->rb.rb_right) {
		subtree_last = rb_entry(node->rb.rb_right,
			struct interval_tree_node, rb)->__subtree_last;
		if (max < subtree_last)
			max = subtree_last;
	}
	return max;
}

RB_DECLARE_CALLBACKS(static, augment_callbacks, struct interval_tree_node, rb,
		     unsigned long, __subtree_last, compute_subtree_last)

/* Insert / remove interval nodes from the tree */

void interval_tree_insert(struct interval_tree_node *node,
			  struct rb_root *root)
{
	struct rb_node **link = &root->rb_node, *rb_parent = NULL;
	unsigned long start = node->start, last = node->last;
	struct interval_tree_node *parent;

	while (*link) {
		rb_parent = *link;
		parent = rb_entry(rb_parent, struct interval_tree_node, rb);
		if (parent->__subtree_last < last)
			parent->__subtree_last = last;
		if (start < parent->start)
			link = &parent->rb.rb_left;
		else
			link = &parent->rb.rb_right;
	}

	node->__subtree_last = last;
	rb_link_node(&node->rb, rb_parent, link);
	rb_insert_augmented(&node->rb, root, &augment_callbacks);
}

void interval_tree_remove(struct interval_tree_node *node,
			  struct rb_root *root)
{
	rb_erase_augmented(&node->rb, root, &augment_callbacks);
}

/*
 * Iterate over intervals intersecting [start;last]
 *
 * Note that a node's interval intersects [start;last] iff:
 *   Cond1: node->start <= last
 * and
 *   Cond2: start <= node->last
 */

static struct interval_tree_node *
subtree_search(struct interval_tree_node *node,
	       unsigned long start, unsigned long last)
{
	while (true) {
		/*
		 * Loop invariant: start <= node->__subtree_last
		 * (Cond2 is satisfied by one of the subtree nodes)
		 */
		if (node->rb.rb_left) {
			struct interval_tree_node *left =
				rb_entry(node->rb.rb_left,
					 struct interval_tree_node, rb);
			if (start <= left->__subtree_last) {
				/*
				 * Some nodes in left subtree satisfy Cond2.
				 * Iterate to find the leftmost such node N.
				 * If it also satisfies Cond1, that's the match
				 * we are looking for. Otherwise, there is no
				 * matching interval as nodes to the right of N
				 * can't satisfy Cond1 either.
				 */
				node = left;
				continue;
			}
		}
		if (node->start <= last) {		/* Cond1 */
			if (start <= node->last)	/* Cond2 */
				return node;	/* node is leftmost match */
			if (node->rb.rb_right) {
				node = rb_entry(node->rb.rb_right,
					struct interval_tree_node, rb);
				if (start <= node->__subtree_last)
					continue;
			}
		}
		return NULL;	/* No match */
	}
}

struct interval_tree_node *
interval_tree_iter_first(struct rb_root *root,
			 unsigned long start, unsigned long last)
{
	struct interval_tree_node *node;

	if (!root->rb_node)
		return NULL;
	node = rb_entry(root->rb_node, struct interval_tree_node, rb);
	if (node->__subtree_last < start)
		return NULL;
	return subtree_search(node, start, last);
}

struct interval_tree_node *
interval_tree_iter_next(struct interval_tree_node *node,
			unsigned long start, unsigned long last)
{
	struct rb_node *rb = node->rb.rb_right, *prev;

	while (true) {
		/*
		 * Loop invariants:
		 *   Cond1: node->start <= last
		 *   rb == node->rb.rb_right
		 *
		 * First, search right subtree if suitable
		 */
		if (rb) {
			struct interval_tree_node *right =
				rb_entry(rb, struct interval_tree_node, rb);
			if (start <= right->__subtree_last)
				return subtree_search(right, start, last);
		}

		/* Move up the tree until we come from a node's left child */
		do {
			rb = rb_parent(&node->rb);
			if (!rb)
				return NULL;
			prev = &node->rb;
			node = rb_entry(rb, struct interval_tree_node, rb);
			rb = node->rb.rb_right;
		} while (prev == rb);

		/* Check if the node intersects [start;last] */
		if (last < node->start)		/* !Cond1 */
			return NULL;
		else if (start <= node->last)	/* Cond2 */
			return node;
	}
}

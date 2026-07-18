// SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause
/*
 * Copyright (c) 2025-2026 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2025-2026 Emil Tsalapatis <emil@etsalapatis.com>
 */

#include <libarena/common.h>

#include <libarena/asan.h>
#include <libarena/rbtree.h>

int rb_integrity_check(struct rbtree __arena *rbtree);
void rbnode_print(size_t depth, struct rbnode __arena *rbn);
static int rbnode_replace(struct rbtree __arena *rbtree,
			  struct rbnode __arena *existing,
			  struct rbnode __arena *replacement);

struct rbtree __arena *rb_create(enum rbtree_alloc alloc,
				 enum rbtree_insert_mode insert)
{
	struct rbtree __arena *rbtree;

	rbtree = arena_malloc(sizeof(*rbtree));
	if (unlikely(!rbtree))
		return NULL;

	/*
	 * RB_UPDATE overwrites existing values in the nodes, but RB_NOALLOC
	 * trees manage the tree nodes directly (including holding pointers
	 * to them). Disallow mixing the two modes to avoid dealing with
	 * unintuitive semantics.
	 */
	if (alloc == RB_NOALLOC && insert == RB_UPDATE) {
		arena_stderr("WARNING: Cannot combine RB_NOALLOC and RB_UPDATE");
		arena_free(rbtree);
		return NULL;
	}

	rbtree->alloc = alloc;
	rbtree->insert = insert;
	rbtree->root = NULL;

	return rbtree;
}

__weak
int rb_destroy(struct rbtree __arena *rbtree)
{
	int ret = 0;

	arena_subprog_init();

	if (unlikely(!rbtree))
		return -EINVAL;

	if (rbtree->alloc == RB_NOALLOC) {
		/*
		 * We cannot do anything about RB_NOALLOC nodes. The whole
		 * point of RB_NOALLOC is that the nodes are directly owned
		 * by the caller that allocates and inserts them. We could
		 * unilaterally grab all nodes and free them anyway, but that
		 * would almost certainly cause UAF as the callers keep accessing
		 * the now freed nodes. Throw an error instead.
		 */
		if (rbtree->root) {
			arena_stderr("WARNING: Destroying RB_NOALLOC tree with > 0 nodes");
			return -EBUSY;
		}

		goto out;
	}

	while (rbtree->root && can_loop) {
		ret = rb_remove(rbtree, rbtree->root->key);
		if (ret)
			break;
	}

out:
	arena_free(rbtree);
	return ret;
}

static inline int rbnode_dir(struct rbnode __arena *node)
{
	/* Arbitrarily choose a direction for the root. */
	if (unlikely(!node->parent))
		return 0;

	return (node->parent->left == node) ? 0 : 1;
}

/*
 * The __noinline is to prevent inlining from bloating the add
 * remove calls, in turn causing register splits and increasing
 * stack usage above what is permitted.
 */
__noinline
int rbnode_rotate(struct rbtree __arena *rbtree,
		  struct rbnode __arena *node, int dir)
{
	struct rbnode __arena *tmp, *parent;
	int parentdir;

	parent = node->parent;
	if (parent)
		parentdir = rbnode_dir(node);

	/* If we're doing a root change, are we the root? */
	if (unlikely(!parent && rbtree->root != node))
		return -EINVAL;

	/*
	 * Does the node we're turning into the root into exist?
	 * Note that the new root is on the opposite side of the
	 * rotation's direction.
	 */
	tmp = node->child[1 - dir];
	if (unlikely(!tmp))
		return -EINVAL;

	/* Steal the closest child of the new root. */
	node->child[1 - dir] = tmp->child[dir];
	if (node->child[1 - dir])
		node->child[1 - dir]->parent = node;

	/* Put the node below the new root.*/
	tmp->child[dir] = node;
	node->parent = tmp;

	tmp->parent = parent;
	if (parent)
		parent->child[parentdir] = tmp;
	else
		rbtree->root = tmp;

	return 0;
}

static
struct rbnode __arena *rbnode_find(struct rbnode __arena *subtree, u64 key)
{
	struct rbnode __arena *node = subtree;
	int dir;

	if (!subtree)
		return NULL;

	while (can_loop) {
		if (node->key == key)
			break;

		dir = (key < node->key) ? 0 : 1;

		if (!node->child[dir])
			break;

		node = node->child[dir];
	}

	return node;
}

static
struct rbnode __arena *rbnode_least_upper_bound(struct rbnode __arena *subtree, uint64_t key)
{
	struct rbnode __arena *node = subtree;
	int dir;

	if (!subtree)
		return NULL;

	while (can_loop) {
		dir = (key <= node->key) ? 0 : 1;

		if (!node->child[dir])
			break;

		node = node->child[dir];
	}

	return node;
}

__weak
int rb_find(struct rbtree __arena *rbtree, u64 key, u64 *value)
{
	struct rbnode __arena *node;

	if (unlikely(!rbtree))
		return -EINVAL;

	if (unlikely(!value))
		return -EINVAL;

	node = rbnode_find(rbtree->root, key);
	if (!node || node->key != key)
		return -ENOENT;

	*value = node->value;

	return 0;
}

__weak
struct rbnode __arena *rb_node_alloc(u64 key, u64 value)
{
	struct rbnode __arena *rbnode = NULL;

	rbnode = (struct rbnode __arena *)arena_malloc(sizeof(*rbnode));
	if (!rbnode)
		return NULL;

	/*
	 * WARNING: The order of assignments is weird on purpose.
	 * See comment in rb_insert_node() for more context.
	 * TL;DR: Prevent consecutive 0 assignments from being
	 * promoted into an unverifiable memset by the compiler.
	 */

	rbnode->key = key;
	rbnode->parent = NULL;
	rbnode->value = value;
	rbnode->left = NULL;
	rbnode->is_red = true;
	rbnode->right = NULL;

	return rbnode;
}

__weak
void rb_node_free(struct rbnode __arena *rbnode)
{
	arena_free(rbnode);
}

static
int rb_node_insert(struct rbtree __arena *rbtree,
		   struct rbnode __arena *node)
{
	struct rbnode __arena *grandparent, *parent = rbtree->root;
	u64 key = node->key;
	struct rbnode __arena *uncle;
	int dir;
	int ret;

	if (unlikely(!rbtree))
		return -EINVAL;

	if (!parent) {
		rbtree->root = node;
		return 0;
	}

	if (rbtree->insert != RB_DUPLICATE)
		parent = rbnode_find(parent, key);
	else
		parent = rbnode_least_upper_bound(parent, key);

	if (key == parent->key && rbtree->insert != RB_DUPLICATE) {
		if (rbtree->insert == RB_UPDATE) {
			/*
			 * Replace the old node with the new one.
			 * Free up the old node.
			 */
			ret = rbnode_replace(rbtree, parent, node);
			if (ret)
				return ret;

			if (rbtree->alloc == RB_ALLOC)
				rb_node_free(parent);

			return 0;
		}

		/* Otherwise it's RB_DEFAULT. */
		return -EALREADY;
	}

	node->parent = parent;
	/* Also works if key == parent->key. */
	if (key <= parent->key)
		parent->left = node;
	else
		parent->right = node;

	while (can_loop) {
		parent = node->parent;
		if (!parent)
			return 0;

		if (!parent->is_red)
			return 0;

		grandparent = parent->parent;
		if (!grandparent) {
			parent->is_red = false;
			return 0;
		}

		dir = rbnode_dir(parent);
		uncle = grandparent->child[1 - dir];

		if (!uncle || !uncle->is_red) {
			if (node == parent->child[1 - dir]) {
				rbnode_rotate(rbtree, parent, dir);
				node = parent;
				parent = grandparent->child[dir];
			}

			rbnode_rotate(rbtree, grandparent, 1 - dir);
			parent->is_red = false;
			grandparent->is_red = true;

			return 0;
		}

		/* Uncle is red. */

		parent->is_red = false;
		uncle->is_red = false;
		grandparent->is_red = true;

		node = grandparent;
	}

	return 0;
}

int rb_insert_node(struct rbtree __arena *rbtree,
		   struct rbnode __arena *node)
{
	if (unlikely(!rbtree))
		return -EINVAL;

	if (unlikely(rbtree->alloc == RB_ALLOC))
		return -EINVAL;

	node->left = NULL;

	/*
	 * Workaround to break an optimization that causes
	 * verification failures on some compilers. Assignments
	 * of the kind
	 *
	 * *(r0 + 0) = 0;
	 * *(r0 + 8) = 0;
	 * *(r0 + 16) = 0;
	 *
	 * get promoted into a memset, and that in turn is not
	 * handled properly for arena memory by LLVM 21 and GCC 15.
	 * Add a barrier for now to prevent the assignments from being fused.
	 */
	barrier();

	node->parent = NULL;
	node->right = NULL;
	
	node->is_red = true;

	return rb_node_insert(rbtree, node);
}

__weak
int rb_insert(struct rbtree __arena *rbtree, u64 key, u64 value)
{
	struct rbnode __arena *node;
	int ret;

	if (unlikely(!rbtree))
		return -EINVAL;

	if (unlikely(rbtree->alloc != RB_ALLOC))
		return -EINVAL;

	node = rb_node_alloc(key, value);
	if (!node)
		return -ENOMEM;

	ret = rb_node_insert(rbtree, node);
	if (ret) {
		rb_node_free(node);
		return ret;
	}

	return 0;
}

static inline struct rbnode __arena *rbnode_least(struct rbnode __arena *subtree)
{
	while (subtree->left && can_loop)
		subtree = subtree->left;

	return subtree;
}

__weak int rb_least(struct rbtree __arena *rbtree, u64 *key, u64 *value)
{
	struct rbnode __arena *least;

	if (unlikely(!rbtree))
		return -EINVAL;

	if (!rbtree->root)
		return -ENOENT;

	least = rbnode_least(rbtree->root);
	if (key)
		*key = least->key;
	if (value)
		*value = least->value;

	return 0;
}


/*
 * If we are referencing ourselves, a and b have a parent-child relation,
 * and we should be pointing at the other node instead.
 */
static inline void rbnode_fixup_pointers(struct rbnode __arena *a,
					 struct rbnode __arena *b)
{
#define fixup(n1, n2, member) do { if (n1->member == n1) n1->member = n2; } while (0)
	fixup(a, b, left);
	fixup(a, b, right);
	fixup(a, b, parent);
#undef fixup
}

static inline void rbnode_swap_values(struct rbnode __arena *a,
				      struct rbnode __arena *b)
{
#define swap(n1, n2, tmp) do { (tmp) = (n1); (n1) = (n2); (n2) = (tmp); } while (0)
	struct rbnode __arena *tmpnode;
	u64 tmp;

	/* Swap the pointers. */
	swap(a->is_red, b->is_red, tmp);

	swap(a->left, b->left, tmpnode);
	swap(a->right, b->right, tmpnode);
	swap(a->parent, b->parent, tmpnode);
#undef swap

	/* Account for the nodes being parent and child. */
	rbnode_fixup_pointers(b, a);
	rbnode_fixup_pointers(a, b);
}

static inline void rbnode_adjust_neighbors(struct rbtree __arena *rbtree,
					   struct rbnode __arena *node, int dir)
{
	if (node->left)
		node->left->parent = node;
	if (node->right)
		node->right->parent = node;

	if (node->parent) {
		node->parent->child[dir] = node;
		return;
	}

	rbtree->root = node;
}

/*
 * Directly replace an existing node with a replacement. The replacement node
 * should not already be in the tree.
 */
static int rbnode_replace(struct rbtree __arena *rbtree,
			  struct rbnode __arena *existing,
			  struct rbnode __arena *replacement)
{
	int dir = 0;

	if (unlikely(replacement->parent || replacement->left || replacement->right))
		return -EINVAL;

	if (existing->parent)
		dir = rbnode_dir(existing);

	replacement->is_red = existing->is_red;
	replacement->left = existing->left;
	replacement->right = existing->right;
	replacement->parent = existing->parent;

	/* Fix up the new node's neighbors. */
	rbnode_adjust_neighbors(rbtree, replacement, dir);

	return 0;
}

/*
 * Switch two nodes in the tree in place. This is useful during node deletion.
 * This is more involved than switching the values of the two nodes because we
 * must update all tree pointers.
 */
static void rbnode_switch(struct rbtree __arena *rbtree,
			  struct rbnode __arena *a,
			  struct rbnode __arena *b)
{
	int adir = 0, bdir = 0;

	/*
	 * Store the direction in the parent because we will not
	 * be able to recompute it once we start swapping values.
	 */
	if (a->parent)
		adir = rbnode_dir(a);

	if (b->parent)
		bdir = rbnode_dir(b);

	rbnode_swap_values(a, b);

	/*
	 * Fix up the pointers from the children/parent to the
	 * new nodes.
	 */
	rbnode_adjust_neighbors(rbtree, a, bdir);
	rbnode_adjust_neighbors(rbtree, b, adir);
}

static inline int rbnode_remove_node_single_child(struct rbtree __arena *rbtree,
						  struct rbnode __arena *node,
						  bool free)
{
	struct rbnode __arena *child;
	int dir;

	if (unlikely(node->is_red)) {
		arena_stderr("Node unexpectedly red\n");
		return -EINVAL;
	}

	child = node->left ? node->left : node->right;
	if (unlikely(!child->is_red)) {
		arena_stderr("Only child is black\n");
		return -EINVAL;
	}

	/*
	 * Since it's the immediate child, we can just
	 * remove the parent.
	 */
	child->parent = node->parent;

	if (node->parent) {
		dir = rbnode_dir(node);
		node->parent->child[dir] = child;
	} else {
		rbtree->root = child;
	}

	/* Color the child black. */
	child->is_red = false;

	/* Only free if called from rb_remove. */
	if (free)
		rb_node_free(node);

	return 0;
}

static inline bool rbnode_has_red_children(struct rbnode __arena *node)
{
	if (node->left && node->left->is_red)
		return true;

	return node->right && node->right->is_red;
}

static
int rb_node_remove(struct rbtree __arena *rbtree,
		   struct rbnode __arena *node)
{
	struct rbnode __arena *parent, *sibling, *close_nephew, *distant_nephew;
	bool free = (rbtree->alloc == RB_ALLOC);
	struct rbnode __arena *replace, *initial;
	bool is_red;
	int dir;

	/* Both children present, replace with next largest key. */
	if (node->left && node->right) {
		/*
		 * Swap the node itself instead of just the
		 * key/value pair to account for nodes embedded
		 * in other structs.
		 */

		replace = rbnode_least(node->right);
		rbnode_switch(rbtree, replace, node);

		/*
		 * FALLTHROUGH: We moved the node we are removing to
		 * the leftmost position of the subtree. We can now
		 * remove it as if it was always where we moved it to.
		 */
	}

	initial = node;

	/* Only one child present, replace with child and paint it black. */
	if (!node->left != !node->right)
		return rbnode_remove_node_single_child(rbtree, node, free);

	/* (!node->left && !node->right) */

	parent = node->parent;
	if (!parent) {
		/* Check that we're _actually_ the root. */
		if (rbtree->root == node)
			rbtree->root = NULL;
		else
			arena_stderr("WARNING: Attempting to remove detached node from rbtree\n");

		if (free)
			rb_node_free(node);
		return 0;
	}

	dir = rbnode_dir(node);
	parent->child[dir] = NULL;
	is_red = node->is_red;

	if (free)
		rb_node_free(node);

	/* If we removed a red node, we did not unbalance the tree.*/
	if (is_red)
		return 0;

	sibling = parent->child[1 - dir];
	if (unlikely(!sibling)) {
		arena_stderr("rbtree: removed black node has no sibling\n");
		return -EINVAL;
	}

	/*
	 * We removed a black node, causing a change in path
	 * weight. Start rebalancing. The invariant is that
	 * all paths going through the node are shortened
	 * by one, and the current node is black.
	 */
	while (can_loop) {

		/* Balancing reached the root, there can be no imbalance. */
		if (!parent)
			return 0;

		/*
		 * We already determined the dir, either above or
		 * at the end of the loop.
		 */

		/*
		 * If we have no sibling, the tree was
		 * already unbalanced.
		 */
		sibling = parent->child[1 - dir];
		if (unlikely(!sibling)) {
			arena_stderr("rbtree: removed black node has no sibling\n");
			return -EINVAL;
		}

		/* Sibling is red, turn it into the grandparent. */
		if (sibling->is_red) {
			/*
			 * Sibling is red. Transform the tree to turn
			 * the sibling into the parent's position, and
			 * repaint them. This does not balance the tree
			 * but makes it so we know the sibling is black
			 * and so can use the transformations to balance.
			 */
			rbnode_rotate(rbtree, parent, dir);
			parent->is_red = true;
			sibling->is_red = false;

			/* Our new sibling is now the close nephew. */
			sibling = parent->child[1 - dir];
			/* If sibling has any red siblings, break out. */
			if (rbnode_has_red_children(sibling))
				break;

			/* We can repaint the sibling and parent, we're done. */
			sibling->is_red = true;
			parent->is_red = false;

			return 0;
		}

		/* Sibling guaranteed to be black. If it has red children, break out. */
		if (rbnode_has_red_children(sibling))
			break;

		/*
		 * Both sibling and children are black. If parent is red, swap
		 * colors with the sibling. Otherwise
		 */
		if (parent->is_red) {
			parent->is_red = false;
			sibling->is_red = true;
			return 0;
		}

		/*
		 * Parent, sibling, and all its children are black. Repaint the sibling.
		 * This shortens the paths through it, so pop up a level in the
		 * tree and repeat the balancing.
		 */
		sibling->is_red = true;
		node = parent;
		parent = node->parent;
		dir = rbnode_dir(node);
	}

	if (node != initial) {
		dir = rbnode_dir(node);
		parent = node->parent;
		sibling = parent->child[1-dir];
	}
	/*
	 * Almost there. We know between the parent, sibling,
	 * and nephews only one or two of the nephews are red. If
	 * it is the close one, rotate it to the sibling position,
	 * paint it black, and paint the previous sibling red.
	 */

	close_nephew = sibling->child[dir];
	distant_nephew = sibling->child[1 - dir];

	/*
	 * If the distant red nephew is not red, rotate
	 * and repaint. We need the distant nephew
	 * to be red. We know the close nephew is red
	 * because at least one of them are, so the
	 * distant one is black if it exists.
	 */
	if (!distant_nephew || !distant_nephew->is_red) {
		rbnode_rotate(rbtree, sibling, 1 - dir);
		sibling->is_red = true;
		close_nephew->is_red = false;
		distant_nephew = sibling;
		sibling = close_nephew;
	}

	/*
	 * We now know it's the distant nephew that's red.
	 * Rotate the sibling into our parent's position
	 * and paint both black.
	 */

	rbnode_rotate(rbtree, parent, dir);
	sibling->is_red = parent->is_red;
	parent->is_red = false;
	distant_nephew->is_red = false;

	return 0;
}

__weak
int rb_remove_node(struct rbtree __arena *rbtree,
		   struct rbnode __arena *node)
{
	if (unlikely(!rbtree))
		return -EINVAL;

	if (unlikely(rbtree->alloc == RB_ALLOC))
		return -EINVAL;

	return rb_node_remove(rbtree, node);
}

__weak
int rb_remove(struct rbtree __arena *rbtree, u64 key)
{
	struct rbnode __arena *node;

	if (unlikely(!rbtree))
		return -EINVAL;

	if (unlikely(rbtree->alloc != RB_ALLOC))
		return -EINVAL;

	if (!rbtree->root)
		return -ENOENT;

	node = rbnode_find(rbtree->root, key);
	if (!node || node->key != key)
		return -ENOENT;

	return rb_node_remove(rbtree, node);
}

__weak
int rb_pop(struct rbtree __arena *rbtree, u64 *key, u64 *value)
{
	struct rbnode __arena *node;

	if (unlikely(!rbtree))
		return -EINVAL;

	if (!rbtree->root)
		return -ENOENT;

	if (rbtree->alloc != RB_ALLOC)
		return -EINVAL;

	node = rbnode_least(rbtree->root);
	if (unlikely(!node))
		return -ENOENT;

	if (key)
		*key = node->key;
	if (value)
		*value = node->value;

	return rb_node_remove(rbtree, node);
}

inline void rbnode_print(size_t depth, struct rbnode __arena *rbn)
{
	arena_stderr("[DEPTH %d] %p (%s)\n PARENT %p", depth, rbn, rbn->is_red ? "red" : "black", rbn->parent);
	arena_stderr("\tKV (%ld, %ld)\n LEFT %p RIGHT %p]\n", rbn->key, rbn->value, rbn->left, rbn->right);
}

enum rb_print_state {
	RB_NONE_VISITED,
	RB_LEFT_VISITED,
	RB_RIGHT_VISITED,
};

__weak
enum rb_print_state rb_print_next_state(struct rbnode __arena *rbnode,
					enum rb_print_state state, u64 *next)
{
	if (unlikely(!next))
		return RB_NONE_VISITED;

	switch (state) {
	case RB_NONE_VISITED:
		if (rbnode->left) {
			*next = (u64)rbnode->left;
			state = RB_LEFT_VISITED;
			break;
		}

		/* FALLTHROUGH */

	case RB_LEFT_VISITED:
		if (rbnode->right) {
			*next = (u64)rbnode->right;
			state = RB_RIGHT_VISITED;
			break;
		}

		/* FALLTHROUGH */

	default:
		*next = 0;
		state = RB_RIGHT_VISITED;
	}

	return state;
}

__weak
int rb_print_pop_up(struct rbnode __arena **rbnodep, u8 *depthp, enum rb_print_state (*stack)[RB_MAXLVL_PRINT], enum rb_print_state *state)
{
	struct rbnode __arena *rbnode;
	volatile u8 depth;
	int j;

	if (unlikely(!rbnodep || !depthp || !stack || !state))
		return -EINVAL;

	rbnode = *rbnodep;
	depth = *depthp;

	for (j = 0; j < RB_MAXLVL_PRINT && can_loop; j++) {
		if (*state != RB_RIGHT_VISITED)
			break;

		depth -= 1;
		if (depth < 0 || depth >= RB_MAXLVL_PRINT)
			break;

		*state = (*stack)[depth % RB_MAXLVL_PRINT];
		rbnode = rbnode->parent;
	}

	*rbnodep = rbnode;
	*depthp = depth;

	return 0;
}

__weak
int rb_print(struct rbtree __arena *rbtree)
{
	enum rb_print_state stack[RB_MAXLVL_PRINT];
	struct rbnode __arena *rbnode = rbtree->root;
	enum rb_print_state state;
	struct rbnode __arena *next;
	u64 next_addr;
	u8 depth;
	int ret;

	if (unlikely(!rbtree))
		return -EINVAL;

	depth = 0;
	state = RB_NONE_VISITED;

	arena_stderr("=== RB TREE START ===\n");

	if (!rbtree->root)
		goto out;

	/* Even with can_loop, the verifier doesn't like infinite loops. */
	while (can_loop) {
		if (state == RB_NONE_VISITED)
			rbnode_print(depth, rbnode);

		/* Find which child to traverse next. */
		state = rb_print_next_state(rbnode, state, &next_addr);
		next = (struct rbnode __arena *)next_addr;

		/* Child found. Store the node state and go on. */
		if (next) {
			if (depth < 0 || depth >= RB_MAXLVL_PRINT)
				return 0;

			stack[depth++] = state;

			rbnode = next;
			state = RB_NONE_VISITED;

			continue;
		}

		/* Otherwise, go as far up as possible. */
		ret = rb_print_pop_up(&rbnode, &depth, &stack, &state);
		if (ret)
			return -EINVAL;

		if (depth < 0 || depth >= RB_MAXLVL_PRINT) {
			arena_stderr("=== RB TREE END (depth %d\n)===", depth);
			return 0;
		}

	}

out:
	arena_stderr("=== RB TREE END ===\n");

	return 0;
}

__weak
int rb_integrity_check(struct rbtree __arena *rbtree)
{
	enum rb_print_state stack[RB_MAXLVL_PRINT];
	struct rbnode __arena *rbnode = rbtree->root;
	enum rb_print_state state;
	struct rbnode __arena *next;
	u64 next_addr;
	u8 depth;
	int ret;

	if (unlikely(!rbtree))
		return -EINVAL;

	if (!rbtree->root)
		return 0;

	depth = 0;
	state = RB_NONE_VISITED;

	/* Even with can_loop, the verifier doesn't like infinite loops. */
	while (can_loop) {
		if (rbnode->parent && rbnode->parent->left != rbnode
			&& rbnode->parent->right != rbnode) {
			arena_stderr("WARNING: Inconsistent tree. Parent %p has no child %p\n", rbnode->parent, rbnode);
			return -EINVAL;
		}

		if (rbnode->parent == rbnode) {
			arena_stderr("WARNING: Inconsistent tree, node %p is its own parent\n", rbnode);
			return -EINVAL;
		}

		if (rbnode->left == rbnode) {
			arena_stderr("WARNING: Inconsistent tree, node %p is its own left child\n", rbnode);
			return -EINVAL;
		}

		if (rbnode->right == rbnode) {
			arena_stderr("WARNING: Inconsistent tree, node %p is its own right child\n", rbnode);
			return -EINVAL;
		}

		if (rbnode->is_red) {
			if (rbnode->left && rbnode->left->is_red) {
				arena_stderr("WARNING: Inconsistent tree. Parent has %p has red child %p\n", rbnode, rbnode->left);
				return -EINVAL;
			}
			if (rbnode->right && rbnode->right->is_red) {
				arena_stderr("WARNING: Inconsistent tree. Parent has %p has red child %p\n", rbnode, rbnode->right);
				return -EINVAL;
			}
		} else if (rbnode->parent && rbnode->parent->child[1 - rbnode_dir(rbnode)] == NULL) {
			arena_stderr("WARNING: Inconsistent tree. Black node %p has no sibling\n", rbnode);
			return -EINVAL;
		}

		/* Find which child to traverse next. */
		state = rb_print_next_state(rbnode, state, &next_addr);
		next = (struct rbnode __arena *)next_addr;

		/* Child found. Store the node state and go on. */
		if (next) {
			if (depth < 0 || depth >= RB_MAXLVL_PRINT)
				return 0;

			stack[depth++] = state;

			rbnode = next;
			state = RB_NONE_VISITED;

			continue;
		}

		/* Otherwise, go as far up as possible. */
		ret = rb_print_pop_up(&rbnode, &depth, &stack, &state);
		if (ret)
			return -EINVAL;

		if (depth < 0 || depth >= RB_MAXLVL_PRINT) {
			return 0;
		}

	}

	return 0;
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sparse bit array
 *
 * Copyright (C) 2018, Google LLC.
 * Copyright (C) 2018, Red Hat, Inc. (code style cleanup and fuzzing driver)
 *
 * This library provides functions to support a memory efficient bit array,
 * with an index size of 2^64.  A sparsebit array is allocated through
 * the use sparsebit_alloc() and free'd via sparsebit_free(),
 * such as in the following:
 *
 *   struct sparsebit *s;
 *   s = sparsebit_alloc();
 *   sparsebit_free(&s);
 *
 * The struct sparsebit type resolves down to a struct sparsebit.
 * Note that, sparsebit_free() takes a pointer to the sparsebit
 * structure.  This is so that sparsebit_free() is able to poison
 * the pointer (e.g. set it to NULL) to the struct sparsebit before
 * returning to the caller.
 *
 * Between the return of sparsebit_alloc() and the call of
 * sparsebit_free(), there are multiple query and modifying operations
 * that can be performed on the allocated sparsebit array.  All of
 * these operations take as a parameter the value returned from
 * sparsebit_alloc() and most also take a bit index.  Frequently
 * used routines include:
 *
 *  ---- Query Operations
 *  sparsebit_is_set(s, idx)
 *  sparsebit_is_clear(s, idx)
 *  sparsebit_any_set(s)
 *  sparsebit_first_set(s)
 *  sparsebit_next_set(s, prev_idx)
 *
 *  ---- Modifying Operations
 *  sparsebit_set(s, idx)
 *  sparsebit_clear(s, idx)
 *  sparsebit_set_num(s, idx, num);
 *  sparsebit_clear_num(s, idx, num);
 *
 * A common operation, is to itterate over all the bits set in a test
 * sparsebit array.  This can be done via code with the following structure:
 *
 *   sparsebit_idx_t idx;
 *   if (sparsebit_any_set(s)) {
 *     idx = sparsebit_first_set(s);
 *     do {
 *       ...
 *       idx = sparsebit_next_set(s, idx);
 *     } while (idx != 0);
 *   }
 *
 * The index of the first bit set needs to be obtained via
 * sparsebit_first_set(), because sparsebit_next_set(), needs
 * the index of the previously set.  The sparsebit_idx_t type is
 * unsigned, so there is no previous index before 0 that is available.
 * Also, the call to sparsebit_first_set() is not made unless there
 * is at least 1 bit in the array set.  This is because sparsebit_first_set()
 * aborts if sparsebit_first_set() is called with no bits set.
 * It is the callers responsibility to assure that the
 * sparsebit array has at least a single bit set before calling
 * sparsebit_first_set().
 *
 * ==== Implementation Overview ====
 * For the most part the internal implementation of sparsebit is
 * opaque to the caller.  One important implementation detail that the
 * caller may need to be aware of is the spatial complexity of the
 * implementation.  This implementation of a sparsebit array is not
 * only sparse, in that it uses memory proportional to the number of bits
 * set.  It is also efficient in memory usage when most of the bits are
 * set.
 *
 * At a high-level the state of the bit settings are maintained through
 * the use of a binary-search tree, where each node contains at least
 * the following members:
 *
 *   typedef uint64_t sparsebit_idx_t;
 *   typedef uint64_t sparsebit_num_t;
 *
 *   sparsebit_idx_t idx;
 *   uint32_t mask;
 *   sparsebit_num_t num_after;
 *
 * The idx member contains the bit index of the first bit described by this
 * node, while the mask member stores the setting of the first 32-bits.
 * The setting of the bit at idx + n, where 0 <= n < 32, is located in the
 * mask member at 1 << n.
 *
 * Nodes are sorted by idx and the bits described by two nodes will never
 * overlap. The idx member is always aligned to the mask size, i.e. a
 * multiple of 32.
 *
 * Beyond a typical implementation, the nodes in this implementation also
 * contains a member named num_after.  The num_after member holds the
 * number of bits immediately after the mask bits that are contiguously set.
 * The use of the num_after member allows this implementation to efficiently
 * represent cases where most bits are set.  For example, the case of all
 * but the last two bits set, is represented by the following two nodes:
 *
 *   node 0 - idx: 0x0 mask: 0xffffffff num_after: 0xffffffffffffffc0
 *   node 1 - idx: 0xffffffffffffffe0 mask: 0x3fffffff num_after: 0
 *
 * ==== Invariants ====
 * This implementation usses the following invariants:
 *
 *   + Node are only used to represent bits that are set.
 *     Nodes with a mask of 0 and num_after of 0 are not allowed.
 *
 *   + Sum of bits set in all the nodes is equal to the value of
 *     the struct sparsebit_pvt num_set member.
 *
 *   + The setting of at least one bit is always described in a nodes
 *     mask (mask >= 1).
 *
 *   + A node with all mask bits set only occurs when the last bit
 *     described by the previous node is not equal to this nodes
 *     starting index - 1.  All such occurences of this condition are
 *     avoided by moving the setting of the nodes mask bits into
 *     the previous nodes num_after setting.
 *
 *   + Node starting index is evenly divisible by the number of bits
 *     within a nodes mask member.
 *
 *   + Nodes never represent a range of bits that wrap around the
 *     highest supported index.
 *
 *      (idx + MASK_BITS + num_after - 1) <= ((sparsebit_idx_t) 0) - 1)
 *
 *     As a consequence of the above, the num_after member of a node
 *     will always be <=:
 *
 *       maximum_index - nodes_starting_index - number_of_mask_bits
 *
 *   + Nodes within the binary search tree are sorted based on each
 *     nodes starting index.
 *
 *   + The range of bits described by any two nodes do not overlap.  The
 *     range of bits described by a single node is:
 *
 *       start: node->idx
 *       end (inclusive): node->idx + MASK_BITS + node->num_after - 1;
 *
 * Note, at times these invariants are temporarily violated for a
 * specific portion of the code.  For example, when setting a mask
 * bit, there is a small delay between when the mask bit is set and the
 * value in the struct sparsebit_pvt num_set member is updated.  Other
 * temporary violations occur when node_split() is called with a specified
 * index and assures that a node where its mask represents the bit
 * at the specified index exists.  At times to do this node_split()
 * must split an existing node into two nodes or create a node that
 * has no bits set.  Such temporary violations must be corrected before
 * returning to the caller.  These corrections are typically performed
 * by the local function node_reduce().
 */

#include "test_util.h"
#include "sparsebit.h"
#include <limits.h>
#include <assert.h>

#define DUMP_LINE_MAX 100 /* Does not include indent amount */

typedef uint32_t mask_t;
#define MASK_BITS (sizeof(mask_t) * CHAR_BIT)

struct node {
	struct node *parent;
	struct node *left;
	struct node *right;
	sparsebit_idx_t idx; /* index of least-significant bit in mask */
	sparsebit_num_t num_after; /* num contiguously set after mask */
	mask_t mask;
};

struct sparsebit {
	/*
	 * Points to root node of the binary search
	 * tree.  Equal to NULL when no bits are set in
	 * the entire sparsebit array.
	 */
	struct node *root;

	/*
	 * A redundant count of the total number of bits set.  Used for
	 * diagnostic purposes and to change the time complexity of
	 * sparsebit_num_set() from O(n) to O(1).
	 * Note: Due to overflow, a value of 0 means none or all set.
	 */
	sparsebit_num_t num_set;
};

/* Returns the number of set bits described by the settings
 * of the node pointed to by nodep.
 */
static sparsebit_num_t node_num_set(struct node *nodep)
{
	return nodep->num_after + __builtin_popcount(nodep->mask);
}

/* Returns a pointer to the node that describes the
 * lowest bit index.
 */
static struct node *node_first(struct sparsebit *s)
{
	struct node *nodep;

	for (nodep = s->root; nodep && nodep->left; nodep = nodep->left)
		;

	return nodep;
}

/* Returns a pointer to the node that describes the
 * lowest bit index > the index of the node pointed to by np.
 * Returns NULL if no node with a higher index exists.
 */
static struct node *node_next(struct sparsebit *s, struct node *np)
{
	struct node *nodep = np;

	/*
	 * If current node has a right child, next node is the left-most
	 * of the right child.
	 */
	if (nodep->right) {
		for (nodep = nodep->right; nodep->left; nodep = nodep->left)
			;
		return nodep;
	}

	/*
	 * No right child.  Go up until node is left child of a parent.
	 * That parent is then the next node.
	 */
	while (nodep->parent && nodep == nodep->parent->right)
		nodep = nodep->parent;

	return nodep->parent;
}

/* Searches for and returns a pointer to the node that describes the
 * highest index < the index of the node pointed to by np.
 * Returns NULL if no node with a lower index exists.
 */
static struct node *node_prev(struct sparsebit *s, struct node *np)
{
	struct node *nodep = np;

	/*
	 * If current node has a left child, next node is the right-most
	 * of the left child.
	 */
	if (nodep->left) {
		for (nodep = nodep->left; nodep->right; nodep = nodep->right)
			;
		return (struct node *) nodep;
	}

	/*
	 * No left child.  Go up until node is right child of a parent.
	 * That parent is then the next node.
	 */
	while (nodep->parent && nodep == nodep->parent->left)
		nodep = nodep->parent;

	return (struct node *) nodep->parent;
}


/* Allocates space to hold a copy of the node sub-tree pointed to by
 * subtree and duplicates the bit settings to the newly allocated nodes.
 * Returns the newly allocated copy of subtree.
 */
static struct node *node_copy_subtree(struct node *subtree)
{
	struct node *root;

	/* Duplicate the node at the root of the subtree */
	root = calloc(1, sizeof(*root));
	if (!root) {
		perror("calloc");
		abort();
	}

	root->idx = subtree->idx;
	root->mask = subtree->mask;
	root->num_after = subtree->num_after;

	/* As needed, recursively duplicate the left and right subtrees */
	if (subtree->left) {
		root->left = node_copy_subtree(subtree->left);
		root->left->parent = root;
	}

	if (subtree->right) {
		root->right = node_copy_subtree(subtree->right);
		root->right->parent = root;
	}

	return root;
}

/* Searches for and returns a pointer to the node that describes the setting
 * of the bit given by idx.  A node describes the setting of a bit if its
 * index is within the bits described by the mask bits or the number of
 * contiguous bits set after the mask.  Returns NULL if there is no such node.
 */
static struct node *node_find(struct sparsebit *s, sparsebit_idx_t idx)
{
	struct node *nodep;

	/* Find the node that describes the setting of the bit at idx */
	for (nodep = s->root; nodep;
	     nodep = nodep->idx > idx ? nodep->left : nodep->right) {
		if (idx >= nodep->idx &&
		    idx <= nodep->idx + MASK_BITS + nodep->num_after - 1)
			break;
	}

	return nodep;
}

/* Entry Requirements:
 *   + A node that describes the setting of idx is not already present.
 *
 * Adds a new node to describe the setting of the bit at the index given
 * by idx.  Returns a pointer to the newly added node.
 *
 * TODO(lhuemill): Degenerate cases causes the tree to get unbalanced.
 */
static struct node *node_add(struct sparsebit *s, sparsebit_idx_t idx)
{
	struct node *nodep, *parentp, *prev;

	/* Allocate and initialize the new node. */
	nodep = calloc(1, sizeof(*nodep));
	if (!nodep) {
		perror("calloc");
		abort();
	}

	nodep->idx = idx & -MASK_BITS;

	/* If no nodes, set it up as the root node. */
	if (!s->root) {
		s->root = nodep;
		return nodep;
	}

	/*
	 * Find the parent where the new node should be attached
	 * and add the node there.
	 */
	parentp = s->root;
	while (true) {
		if (idx < parentp->idx) {
			if (!parentp->left) {
				parentp->left = nodep;
				nodep->parent = parentp;
				break;
			}
			parentp = parentp->left;
		} else {
			assert(idx > parentp->idx + MASK_BITS + parentp->num_after - 1);
			if (!parentp->right) {
				parentp->right = nodep;
				nodep->parent = parentp;
				break;
			}
			parentp = parentp->right;
		}
	}

	/*
	 * Does num_after bits of previous node overlap with the mask
	 * of the new node?  If so set the bits in the new nodes mask
	 * and reduce the previous nodes num_after.
	 */
	prev = node_prev(s, nodep);
	while (prev && prev->idx + MASK_BITS + prev->num_after - 1 >= nodep->idx) {
		unsigned int n1 = (prev->idx + MASK_BITS + prev->num_after - 1)
			- nodep->idx;
		assert(prev->num_after > 0);
		assert(n1 < MASK_BITS);
		assert(!(nodep->mask & (1 << n1)));
		nodep->mask |= (1 << n1);
		prev->num_after--;
	}

	return nodep;
}

/* Returns whether all the bits in the sparsebit array are set.  */
bool sparsebit_all_set(struct sparsebit *s)
{
	/*
	 * If any nodes there must be at least one bit set.  Only case
	 * where a bit is set and total num set is 0, is when all bits
	 * are set.
	 */
	return s->root && s->num_set == 0;
}

/* Clears all bits described by the node pointed to by nodep, then
 * removes the node.
 */
static void node_rm(struct sparsebit *s, struct node *nodep)
{
	struct node *tmp;
	sparsebit_num_t num_set;

	num_set = node_num_set(nodep);
	assert(s->num_set >= num_set || sparsebit_all_set(s));
	s->num_set -= node_num_set(nodep);

	/* Have both left and right child */
	if (nodep->left && nodep->right) {
		/*
		 * Move left children to the leftmost leaf node
		 * of the right child.
		 */
		for (tmp = nodep->right; tmp->left; tmp = tmp->left)
			;
		tmp->left = nodep->left;
		nodep->left = NULL;
		tmp->left->parent = tmp;
	}

	/* Left only child */
	if (nodep->left) {
		if (!nodep->parent) {
			s->root = nodep->left;
			nodep->left->parent = NULL;
		} else {
			nodep->left->parent = nodep->parent;
			if (nodep == nodep->parent->left)
				nodep->parent->left = nodep->left;
			else {
				assert(nodep == nodep->parent->right);
				nodep->parent->right = nodep->left;
			}
		}

		nodep->parent = nodep->left = nodep->right = NULL;
		free(nodep);

		return;
	}


	/* Right only child */
	if (nodep->right) {
		if (!nodep->parent) {
			s->root = nodep->right;
			nodep->right->parent = NULL;
		} else {
			nodep->right->parent = nodep->parent;
			if (nodep == nodep->parent->left)
				nodep->parent->left = nodep->right;
			else {
				assert(nodep == nodep->parent->right);
				nodep->parent->right = nodep->right;
			}
		}

		nodep->parent = nodep->left = nodep->right = NULL;
		free(nodep);

		return;
	}

	/* Leaf Node */
	if (!nodep->parent) {
		s->root = NULL;
	} else {
		if (nodep->parent->left == nodep)
			nodep->parent->left = NULL;
		else {
			assert(nodep == nodep->parent->right);
			nodep->parent->right = NULL;
		}
	}

	nodep->parent = nodep->left = nodep->right = NULL;
	free(nodep);

	return;
}

/* Splits the node containing the bit at idx so that there is a node
 * that starts at the specified index.  If no such node exists, a new
 * node at the specified index is created.  Returns the new node.
 *
 * idx must start of a mask boundary.
 */
static struct node *node_split(struct sparsebit *s, sparsebit_idx_t idx)
{
	struct node *nodep1, *nodep2;
	sparsebit_idx_t offset;
	sparsebit_num_t orig_num_after;

	assert(!(idx % MASK_BITS));

	/*
	 * Is there a node that describes the setting of idx?
	 * If not, add it.
	 */
	nodep1 = node_find(s, idx);
	if (!nodep1)
		return node_add(s, idx);

	/*
	 * All done if the starting index of the node is where the
	 * split should occur.
	 */
	if (nodep1->idx == idx)
		return nodep1;

	/*
	 * Split point not at start of mask, so it must be part of
	 * bits described by num_after.
	 */

	/*
	 * Calculate offset within num_after for where the split is
	 * to occur.
	 */
	offset = idx - (nodep1->idx + MASK_BITS);
	orig_num_after = nodep1->num_after;

	/*
	 * Add a new node to describe the bits starting at
	 * the split point.
	 */
	nodep1->num_after = offset;
	nodep2 = node_add(s, idx);

	/* Move bits after the split point into the new node */
	nodep2->num_after = orig_num_after - offset;
	if (nodep2->num_after >= MASK_BITS) {
		nodep2->mask = ~(mask_t) 0;
		nodep2->num_after -= MASK_BITS;
	} else {
		nodep2->mask = (1 << nodep2->num_after) - 1;
		nodep2->num_after = 0;
	}

	return nodep2;
}

/* Iteratively reduces the node pointed to by nodep and its adjacent
 * nodes into a more compact form.  For example, a node with a mask with
 * all bits set adjacent to a previous node, will get combined into a
 * single node with an increased num_after setting.
 *
 * After each reduction, a further check is made to see if additional
 * reductions are possible with the new previous and next nodes.  Note,
 * a search for a reduction is only done across the nodes nearest nodep
 * and those that became part of a reduction.  Reductions beyond nodep
 * and the adjacent nodes that are reduced are not discovered.  It is the
 * responsibility of the caller to pass a nodep that is within one node
 * of each possible reduction.
 *
 * This function does not fix the temporary violation of all invariants.
 * For example it does not fix the case where the bit settings described
 * by two or more nodes overlap.  Such a violation introduces the potential
 * complication of a bit setting for a specific index having different settings
 * in different nodes.  This would then introduce the further complication
 * of which node has the correct setting of the bit and thus such conditions
 * are not allowed.
 *
 * This function is designed to fix invariant violations that are introduced
 * by node_split() and by changes to the nodes mask or num_after members.
 * For example, when setting a bit within a nodes mask, the function that
 * sets the bit doesn't have to worry about whether the setting of that
 * bit caused the mask to have leading only or trailing only bits set.
 * Instead, the function can call node_reduce(), with nodep equal to the
 * node address that it set a mask bit in, and node_reduce() will notice
 * the cases of leading or trailing only bits and that there is an
 * adjacent node that the bit settings could be merged into.
 *
 * This implementation specifically detects and corrects violation of the
 * following invariants:
 *
 *   + Node are only used to represent bits that are set.
 *     Nodes with a mask of 0 and num_after of 0 are not allowed.
 *
 *   + The setting of at least one bit is always described in a nodes
 *     mask (mask >= 1).
 *
 *   + A node with all mask bits set only occurs when the last bit
 *     described by the previous node is not equal to this nodes
 *     starting index - 1.  All such occurences of this condition are
 *     avoided by moving the setting of the nodes mask bits into
 *     the previous nodes num_after setting.
 */
static void node_reduce(struct sparsebit *s, struct node *nodep)
{
	bool reduction_performed;

	do {
		reduction_performed = false;
		struct node *prev, *next, *tmp;

		/* 1) Potential reductions within the current node. */

		/* Nodes with all bits cleared may be removed. */
		if (nodep->mask == 0 && nodep->num_after == 0) {
			/*
			 * About to remove the node pointed to by
			 * nodep, which normally would cause a problem
			 * for the next pass through the reduction loop,
			 * because the node at the starting point no longer
			 * exists.  This potential problem is handled
			 * by first remembering the location of the next
			 * or previous nodes.  Doesn't matter which, because
			 * once the node at nodep is removed, there will be
			 * no other nodes between prev and next.
			 *
			 * Note, the checks performed on nodep against both
			 * both prev and next both check for an adjacent
			 * node that can be reduced into a single node.  As
			 * such, after removing the node at nodep, doesn't
			 * matter whether the nodep for the next pass
			 * through the loop is equal to the previous pass
			 * prev or next node.  Either way, on the next pass
			 * the one not selected will become either the
			 * prev or next node.
			 */
			tmp = node_next(s, nodep);
			if (!tmp)
				tmp = node_prev(s, nodep);

			node_rm(s, nodep);
			nodep = NULL;

			nodep = tmp;
			reduction_performed = true;
			continue;
		}

		/*
		 * When the mask is 0, can reduce the amount of num_after
		 * bits by moving the initial num_after bits into the mask.
		 */
		if (nodep->mask == 0) {
			assert(nodep->num_after != 0);
			assert(nodep->idx + MASK_BITS > nodep->idx);

			nodep->idx += MASK_BITS;

			if (nodep->num_after >= MASK_BITS) {
				nodep->mask = ~0;
				nodep->num_after -= MASK_BITS;
			} else {
				nodep->mask = (1u << nodep->num_after) - 1;
				nodep->num_after = 0;
			}

			reduction_performed = true;
			continue;
		}

		/*
		 * 2) Potential reductions between the current and
		 * previous nodes.
		 */
		prev = node_prev(s, nodep);
		if (prev) {
			sparsebit_idx_t prev_highest_bit;

			/* Nodes with no bits set can be removed. */
			if (prev->mask == 0 && prev->num_after == 0) {
				node_rm(s, prev);

				reduction_performed = true;
				continue;
			}

			/*
			 * All mask bits set and previous node has
			 * adjacent index.
			 */
			if (nodep->mask + 1 == 0 &&
			    prev->idx + MASK_BITS == nodep->idx) {
				prev->num_after += MASK_BITS + nodep->num_after;
				nodep->mask = 0;
				nodep->num_after = 0;

				reduction_performed = true;
				continue;
			}

			/*
			 * Is node adjacent to previous node and the node
			 * contains a single contiguous range of bits
			 * starting from the beginning of the mask?
			 */
			prev_highest_bit = prev->idx + MASK_BITS - 1 + prev->num_after;
			if (prev_highest_bit + 1 == nodep->idx &&
			    (nodep->mask | (nodep->mask >> 1)) == nodep->mask) {
				/*
				 * How many contiguous bits are there?
				 * Is equal to the total number of set
				 * bits, due to an earlier check that
				 * there is a single contiguous range of
				 * set bits.
				 */
				unsigned int num_contiguous
					= __builtin_popcount(nodep->mask);
				assert((num_contiguous > 0) &&
				       ((1ULL << num_contiguous) - 1) == nodep->mask);

				prev->num_after += num_contiguous;
				nodep->mask = 0;

				/*
				 * For predictable performance, handle special
				 * case where all mask bits are set and there
				 * is a non-zero num_after setting.  This code
				 * is functionally correct without the following
				 * conditionalized statements, but without them
				 * the value of num_after is only reduced by
				 * the number of mask bits per pass.  There are
				 * cases where num_after can be close to 2^64.
				 * Without this code it could take nearly
				 * (2^64) / 32 passes to perform the full
				 * reduction.
				 */
				if (num_contiguous == MASK_BITS) {
					prev->num_after += nodep->num_after;
					nodep->num_after = 0;
				}

				reduction_performed = true;
				continue;
			}
		}

		/*
		 * 3) Potential reductions between the current and
		 * next nodes.
		 */
		next = node_next(s, nodep);
		if (next) {
			/* Nodes with no bits set can be removed. */
			if (next->mask == 0 && next->num_after == 0) {
				node_rm(s, next);
				reduction_performed = true;
				continue;
			}

			/*
			 * Is next node index adjacent to current node
			 * and has a mask with all bits set?
			 */
			if (next->idx == nodep->idx + MASK_BITS + nodep->num_after &&
			    next->mask == ~(mask_t) 0) {
				nodep->num_after += MASK_BITS;
				next->mask = 0;
				nodep->num_after += next->num_after;
				next->num_after = 0;

				node_rm(s, next);
				next = NULL;

				reduction_performed = true;
				continue;
			}
		}
	} while (nodep && reduction_performed);
}

/* Returns whether the bit at the index given by idx, within the
 * sparsebit array is set or not.
 */
bool sparsebit_is_set(struct sparsebit *s, sparsebit_idx_t idx)
{
	struct node *nodep;

	/* Find the node that describes the setting of the bit at idx */
	for (nodep = s->root; nodep;
	     nodep = nodep->idx > idx ? nodep->left : nodep->right)
		if (idx >= nodep->idx &&
		    idx <= nodep->idx + MASK_BITS + nodep->num_after - 1)
			goto have_node;

	return false;

have_node:
	/* Bit is set if it is any of the bits described by num_after */
	if (nodep->num_after && idx >= nodep->idx + MASK_BITS)
		return true;

	/* Is the corresponding mask bit set */
	assert(idx >= nodep->idx && idx - nodep->idx < MASK_BITS);
	return !!(nodep->mask & (1 << (idx - nodep->idx)));
}

/* Within the sparsebit array pointed to by s, sets the bit
 * at the index given by idx.
 */
static void bit_set(struct sparsebit *s, sparsebit_idx_t idx)
{
	struct node *nodep;

	/* Skip bits that are already set */
	if (sparsebit_is_set(s, idx))
		return;

	/*
	 * Get a node where the bit at idx is described by the mask.
	 * The node_split will also create a node, if there isn't
	 * already a node that describes the setting of bit.
	 */
	nodep = node_split(s, idx & -MASK_BITS);

	/* Set the bit within the nodes mask */
	assert(idx >= nodep->idx && idx <= nodep->idx + MASK_BITS - 1);
	assert(!(nodep->mask & (1 << (idx - nodep->idx))));
	nodep->mask |= 1 << (idx - nodep->idx);
	s->num_set++;

	node_reduce(s, nodep);
}

/* Within the sparsebit array pointed to by s, clears the bit
 * at the index given by idx.
 */
static void bit_clear(struct sparsebit *s, sparsebit_idx_t idx)
{
	struct node *nodep;

	/* Skip bits that are already cleared */
	if (!sparsebit_is_set(s, idx))
		return;

	/* Is there a node that describes the setting of this bit? */
	nodep = node_find(s, idx);
	if (!nodep)
		return;

	/*
	 * If a num_after bit, split the node, so that the bit is
	 * part of a node mask.
	 */
	if (idx >= nodep->idx + MASK_BITS)
		nodep = node_split(s, idx & -MASK_BITS);

	/*
	 * After node_split above, bit at idx should be within the mask.
	 * Clear that bit.
	 */
	assert(idx >= nodep->idx && idx <= nodep->idx + MASK_BITS - 1);
	assert(nodep->mask & (1 << (idx - nodep->idx)));
	nodep->mask &= ~(1 << (idx - nodep->idx));
	assert(s->num_set > 0 || sparsebit_all_set(s));
	s->num_set--;

	node_reduce(s, nodep);
}

/* Recursively dumps to the FILE stream given by stream the contents
 * of the sub-tree of nodes pointed to by nodep.  Each line of output
 * is prefixed by the number of spaces given by indent.  On each
 * recursion, the indent amount is increased by 2.  This causes nodes
 * at each level deeper into the binary search tree to be displayed
 * with a greater indent.
 */
static void dump_nodes(FILE *stream, struct node *nodep,
	unsigned int indent)
{
	char *node_type;

	/* Dump contents of node */
	if (!nodep->parent)
		node_type = "root";
	else if (nodep == nodep->parent->left)
		node_type = "left";
	else {
		assert(nodep == nodep->parent->right);
		node_type = "right";
	}
	fprintf(stream, "%*s---- %s nodep: %p\n", indent, "", node_type, nodep);
	fprintf(stream, "%*s  parent: %p left: %p right: %p\n", indent, "",
		nodep->parent, nodep->left, nodep->right);
	fprintf(stream, "%*s  idx: 0x%lx mask: 0x%x num_after: 0x%lx\n",
		indent, "", nodep->idx, nodep->mask, nodep->num_after);

	/* If present, dump contents of left child nodes */
	if (nodep->left)
		dump_nodes(stream, nodep->left, indent + 2);

	/* If present, dump contents of right child nodes */
	if (nodep->right)
		dump_nodes(stream, nodep->right, indent + 2);
}

static inline sparsebit_idx_t node_first_set(struct node *nodep, int start)
{
	mask_t leading = (mask_t)1 << start;
	int n1 = __builtin_ctz(nodep->mask & -leading);

	return nodep->idx + n1;
}

static inline sparsebit_idx_t node_first_clear(struct node *nodep, int start)
{
	mask_t leading = (mask_t)1 << start;
	int n1 = __builtin_ctz(~nodep->mask & -leading);

	return nodep->idx + n1;
}

/* Dumps to the FILE stream specified by stream, the implementation dependent
 * internal state of s.  Each line of output is prefixed with the number
 * of spaces given by indent.  The output is completely implementation
 * dependent and subject to change.  Output from this function should only
 * be used for diagnostic purposes.  For example, this function can be
 * used by test cases after they detect an unexpected condition, as a means
 * to capture diagnostic information.
 */
static void sparsebit_dump_internal(FILE *stream, struct sparsebit *s,
	unsigned int indent)
{
	/* Dump the contents of s */
	fprintf(stream, "%*sroot: %p\n", indent, "", s->root);
	fprintf(stream, "%*snum_set: 0x%lx\n", indent, "", s->num_set);

	if (s->root)
		dump_nodes(stream, s->root, indent);
}

/* Allocates and returns a new sparsebit array. The initial state
 * of the newly allocated sparsebit array has all bits cleared.
 */
struct sparsebit *sparsebit_alloc(void)
{
	struct sparsebit *s;

	/* Allocate top level structure. */
	s = calloc(1, sizeof(*s));
	if (!s) {
		perror("calloc");
		abort();
	}

	return s;
}

/* Frees the implementation dependent data for the sparsebit array
 * pointed to by s and poisons the pointer to that data.
 */
void sparsebit_free(struct sparsebit **sbitp)
{
	struct sparsebit *s = *sbitp;

	if (!s)
		return;

	sparsebit_clear_all(s);
	free(s);
	*sbitp = NULL;
}

/* Makes a copy of the sparsebit array given by s, to the sparsebit
 * array given by d.  Note, d must have already been allocated via
 * sparsebit_alloc().  It can though already have bits set, which
 * if different from src will be cleared.
 */
void sparsebit_copy(struct sparsebit *d, struct sparsebit *s)
{
	/* First clear any bits already set in the destination */
	sparsebit_clear_all(d);

	if (s->root) {
		d->root = node_copy_subtree(s->root);
		d->num_set = s->num_set;
	}
}

/* Returns whether num consecutive bits starting at idx are all set.  */
bool sparsebit_is_set_num(struct sparsebit *s,
	sparsebit_idx_t idx, sparsebit_num_t num)
{
	sparsebit_idx_t next_cleared;

	assert(num > 0);
	assert(idx + num - 1 >= idx);

	/* With num > 0, the first bit must be set. */
	if (!sparsebit_is_set(s, idx))
		return false;

	/* Find the next cleared bit */
	next_cleared = sparsebit_next_clear(s, idx);

	/*
	 * If no cleared bits beyond idx, then there are at least num
	 * set bits. idx + num doesn't wrap.  Otherwise check if
	 * there are enough set bits between idx and the next cleared bit.
	 */
	return next_cleared == 0 || next_cleared - idx >= num;
}

/* Returns whether the bit at the index given by idx.  */
bool sparsebit_is_clear(struct sparsebit *s,
	sparsebit_idx_t idx)
{
	return !sparsebit_is_set(s, idx);
}

/* Returns whether num consecutive bits starting at idx are all cleared.  */
bool sparsebit_is_clear_num(struct sparsebit *s,
	sparsebit_idx_t idx, sparsebit_num_t num)
{
	sparsebit_idx_t next_set;

	assert(num > 0);
	assert(idx + num - 1 >= idx);

	/* With num > 0, the first bit must be cleared. */
	if (!sparsebit_is_clear(s, idx))
		return false;

	/* Find the next set bit */
	next_set = sparsebit_next_set(s, idx);

	/*
	 * If no set bits beyond idx, then there are at least num
	 * cleared bits. idx + num doesn't wrap.  Otherwise check if
	 * there are enough cleared bits between idx and the next set bit.
	 */
	return next_set == 0 || next_set - idx >= num;
}

/* Returns the total number of bits set.  Note: 0 is also returned for
 * the case of all bits set.  This is because with all bits set, there
 * is 1 additional bit set beyond what can be represented in the return
 * value.  Use sparsebit_any_set(), instead of sparsebit_num_set() > 0,
 * to determine if the sparsebit array has any bits set.
 */
sparsebit_num_t sparsebit_num_set(struct sparsebit *s)
{
	return s->num_set;
}

/* Returns whether any bit is set in the sparsebit array.  */
bool sparsebit_any_set(struct sparsebit *s)
{
	/*
	 * Nodes only describe set bits.  If any nodes then there
	 * is at least 1 bit set.
	 */
	if (!s->root)
		return false;

	/*
	 * Every node should have a non-zero mask.  For now will
	 * just assure that the root node has a non-zero mask,
	 * which is a quick check that at least 1 bit is set.
	 */
	assert(s->root->mask != 0);
	assert(s->num_set > 0 ||
	       (s->root->num_after == ((sparsebit_num_t) 0) - MASK_BITS &&
		s->root->mask == ~(mask_t) 0));

	return true;
}

/* Returns whether all the bits in the sparsebit array are cleared.  */
bool sparsebit_all_clear(struct sparsebit *s)
{
	return !sparsebit_any_set(s);
}

/* Returns whether all the bits in the sparsebit array are set.  */
bool sparsebit_any_clear(struct sparsebit *s)
{
	return !sparsebit_all_set(s);
}

/* Returns the index of the first set bit.  Abort if no bits are set.
 */
sparsebit_idx_t sparsebit_first_set(struct sparsebit *s)
{
	struct node *nodep;

	/* Validate at least 1 bit is set */
	assert(sparsebit_any_set(s));

	nodep = node_first(s);
	return node_first_set(nodep, 0);
}

/* Returns the index of the first cleared bit.  Abort if
 * no bits are cleared.
 */
sparsebit_idx_t sparsebit_first_clear(struct sparsebit *s)
{
	struct node *nodep1, *nodep2;

	/* Validate at least 1 bit is cleared. */
	assert(sparsebit_any_clear(s));

	/* If no nodes or first node index > 0 then lowest cleared is 0 */
	nodep1 = node_first(s);
	if (!nodep1 || nodep1->idx > 0)
		return 0;

	/* Does the mask in the first node contain any cleared bits. */
	if (nodep1->mask != ~(mask_t) 0)
		return node_first_clear(nodep1, 0);

	/*
	 * All mask bits set in first node.  If there isn't a second node
	 * then the first cleared bit is the first bit after the bits
	 * described by the first node.
	 */
	nodep2 = node_next(s, nodep1);
	if (!nodep2) {
		/*
		 * No second node.  First cleared bit is first bit beyond
		 * bits described by first node.
		 */
		assert(nodep1->mask == ~(mask_t) 0);
		assert(nodep1->idx + MASK_BITS + nodep1->num_after != (sparsebit_idx_t) 0);
		return nodep1->idx + MASK_BITS + nodep1->num_after;
	}

	/*
	 * There is a second node.
	 * If it is not adjacent to the first node, then there is a gap
	 * of cleared bits between the nodes, and the first cleared bit
	 * is the first bit within the gap.
	 */
	if (nodep1->idx + MASK_BITS + nodep1->num_after != nodep2->idx)
		return nodep1->idx + MASK_BITS + nodep1->num_after;

	/*
	 * Second node is adjacent to the first node.
	 * Because it is adjacent, its mask should be non-zero.  If all
	 * its mask bits are set, then with it being adjacent, it should
	 * have had the mask bits moved into the num_after setting of the
	 * previous node.
	 */
	return node_first_clear(nodep2, 0);
}

/* Returns index of next bit set within s after the index given by prev.
 * Returns 0 if there are no bits after prev that are set.
 */
sparsebit_idx_t sparsebit_next_set(struct sparsebit *s,
	sparsebit_idx_t prev)
{
	sparsebit_idx_t lowest_possible = prev + 1;
	sparsebit_idx_t start;
	struct node *nodep;

	/* A bit after the highest index can't be set. */
	if (lowest_possible == 0)
		return 0;

	/*
	 * Find the leftmost 'candidate' overlapping or to the right
	 * of lowest_possible.
	 */
	struct node *candidate = NULL;

	/* True iff lowest_possible is within candidate */
	bool contains = false;

	/*
	 * Find node that describes setting of bit at lowest_possible.
	 * If such a node doesn't exist, find the node with the lowest
	 * starting index that is > lowest_possible.
	 */
	for (nodep = s->root; nodep;) {
		if ((nodep->idx + MASK_BITS + nodep->num_after - 1)
			>= lowest_possible) {
			candidate = nodep;
			if (candidate->idx <= lowest_possible) {
				contains = true;
				break;
			}
			nodep = nodep->left;
		} else {
			nodep = nodep->right;
		}
	}
	if (!candidate)
		return 0;

	assert(candidate->mask != 0);

	/* Does the candidate node describe the setting of lowest_possible? */
	if (!contains) {
		/*
		 * Candidate doesn't describe setting of bit at lowest_possible.
		 * Candidate points to the first node with a starting index
		 * > lowest_possible.
		 */
		assert(candidate->idx > lowest_possible);

		return node_first_set(candidate, 0);
	}

	/*
	 * Candidate describes setting of bit at lowest_possible.
	 * Note: although the node describes the setting of the bit
	 * at lowest_possible, its possible that its setting and the
	 * setting of all latter bits described by this node are 0.
	 * For now, just handle the cases where this node describes
	 * a bit at or after an index of lowest_possible that is set.
	 */
	start = lowest_possible - candidate->idx;

	if (start < MASK_BITS && candidate->mask >= (1 << start))
		return node_first_set(candidate, start);

	if (candidate->num_after) {
		sparsebit_idx_t first_num_after_idx = candidate->idx + MASK_BITS;

		return lowest_possible < first_num_after_idx
			? first_num_after_idx : lowest_possible;
	}

	/*
	 * Although candidate node describes setting of bit at
	 * the index of lowest_possible, all bits at that index and
	 * latter that are described by candidate are cleared.  With
	 * this, the next bit is the first bit in the next node, if
	 * such a node exists.  If a next node doesn't exist, then
	 * there is no next set bit.
	 */
	candidate = node_next(s, candidate);
	if (!candidate)
		return 0;

	return node_first_set(candidate, 0);
}

/* Returns index of next bit cleared within s after the index given by prev.
 * Returns 0 if there are no bits after prev that are cleared.
 */
sparsebit_idx_t sparsebit_next_clear(struct sparsebit *s,
	sparsebit_idx_t prev)
{
	sparsebit_idx_t lowest_possible = prev + 1;
	sparsebit_idx_t idx;
	struct node *nodep1, *nodep2;

	/* A bit after the highest index can't be set. */
	if (lowest_possible == 0)
		return 0;

	/*
	 * Does a node describing the setting of lowest_possible exist?
	 * If not, the bit at lowest_possible is cleared.
	 */
	nodep1 = node_find(s, lowest_possible);
	if (!nodep1)
		return lowest_possible;

	/* Does a mask bit in node 1 describe the next cleared bit. */
	for (idx = lowest_possible - nodep1->idx; idx < MASK_BITS; idx++)
		if (!(nodep1->mask & (1 << idx)))
			return nodep1->idx + idx;

	/*
	 * Next cleared bit is not described by node 1.  If there
	 * isn't a next node, then next cleared bit is described
	 * by bit after the bits described by the first node.
	 */
	nodep2 = node_next(s, nodep1);
	if (!nodep2)
		return nodep1->idx + MASK_BITS + nodep1->num_after;

	/*
	 * There is a second node.
	 * If it is not adjacent to the first node, then there is a gap
	 * of cleared bits between the nodes, and the next cleared bit
	 * is the first bit within the gap.
	 */
	if (nodep1->idx + MASK_BITS + nodep1->num_after != nodep2->idx)
		return nodep1->idx + MASK_BITS + nodep1->num_after;

	/*
	 * Second node is adjacent to the first node.
	 * Because it is adjacent, its mask should be non-zero.  If all
	 * its mask bits are set, then with it being adjacent, it should
	 * have had the mask bits moved into the num_after setting of the
	 * previous node.
	 */
	return node_first_clear(nodep2, 0);
}

/* Starting with the index 1 greater than the index given by start, finds
 * and returns the index of the first sequence of num consecutively set
 * bits.  Returns a value of 0 of no such sequence exists.
 */
sparsebit_idx_t sparsebit_next_set_num(struct sparsebit *s,
	sparsebit_idx_t start, sparsebit_num_t num)
{
	sparsebit_idx_t idx;

	assert(num >= 1);

	for (idx = sparsebit_next_set(s, start);
		idx != 0 && idx + num - 1 >= idx;
		idx = sparsebit_next_set(s, idx)) {
		assert(sparsebit_is_set(s, idx));

		/*
		 * Does the sequence of bits starting at idx consist of
		 * num set bits?
		 */
		if (sparsebit_is_set_num(s, idx, num))
			return idx;

		/*
		 * Sequence of set bits at idx isn't large enough.
		 * Skip this entire sequence of set bits.
		 */
		idx = sparsebit_next_clear(s, idx);
		if (idx == 0)
			return 0;
	}

	return 0;
}

/* Starting with the index 1 greater than the index given by start, finds
 * and returns the index of the first sequence of num consecutively cleared
 * bits.  Returns a value of 0 of no such sequence exists.
 */
sparsebit_idx_t sparsebit_next_clear_num(struct sparsebit *s,
	sparsebit_idx_t start, sparsebit_num_t num)
{
	sparsebit_idx_t idx;

	assert(num >= 1);

	for (idx = sparsebit_next_clear(s, start);
		idx != 0 && idx + num - 1 >= idx;
		idx = sparsebit_next_clear(s, idx)) {
		assert(sparsebit_is_clear(s, idx));

		/*
		 * Does the sequence of bits starting at idx consist of
		 * num cleared bits?
		 */
		if (sparsebit_is_clear_num(s, idx, num))
			return idx;

		/*
		 * Sequence of cleared bits at idx isn't large enough.
		 * Skip this entire sequence of cleared bits.
		 */
		idx = sparsebit_next_set(s, idx);
		if (idx == 0)
			return 0;
	}

	return 0;
}

/* Sets the bits * in the inclusive range idx through idx + num - 1.  */
void sparsebit_set_num(struct sparsebit *s,
	sparsebit_idx_t start, sparsebit_num_t num)
{
	struct node *nodep, *next;
	unsigned int n1;
	sparsebit_idx_t idx;
	sparsebit_num_t n;
	sparsebit_idx_t middle_start, middle_end;

	assert(num > 0);
	assert(start + num - 1 >= start);

	/*
	 * Leading - bits before first mask boundary.
	 *
	 * TODO(lhuemill): With some effort it may be possible to
	 *   replace the following loop with a sequential sequence
	 *   of statements.  High level sequence would be:
	 *
	 *     1. Use node_split() to force node that describes setting
	 *        of idx to be within the mask portion of a node.
	 *     2. Form mask of bits to be set.
	 *     3. Determine number of mask bits already set in the node
	 *        and store in a local variable named num_already_set.
	 *     4. Set the appropriate mask bits within the node.
	 *     5. Increment struct sparsebit_pvt num_set member
	 *        by the number of bits that were actually set.
	 *        Exclude from the counts bits that were already set.
	 *     6. Before returning to the caller, use node_reduce() to
	 *        handle the multiple corner cases that this method
	 *        introduces.
	 */
	for (idx = start, n = num; n > 0 && idx % MASK_BITS != 0; idx++, n--)
		bit_set(s, idx);

	/* Middle - bits spanning one or more entire mask */
	middle_start = idx;
	middle_end = middle_start + (n & -MASK_BITS) - 1;
	if (n >= MASK_BITS) {
		nodep = node_split(s, middle_start);

		/*
		 * As needed, split just after end of middle bits.
		 * No split needed if end of middle bits is at highest
		 * supported bit index.
		 */
		if (middle_end + 1 > middle_end)
			(void) node_split(s, middle_end + 1);

		/* Delete nodes that only describe bits within the middle. */
		for (next = node_next(s, nodep);
			next && (next->idx < middle_end);
			next = node_next(s, nodep)) {
			assert(next->idx + MASK_BITS + next->num_after - 1 <= middle_end);
			node_rm(s, next);
			next = NULL;
		}

		/* As needed set each of the mask bits */
		for (n1 = 0; n1 < MASK_BITS; n1++) {
			if (!(nodep->mask & (1 << n1))) {
				nodep->mask |= 1 << n1;
				s->num_set++;
			}
		}

		s->num_set -= nodep->num_after;
		nodep->num_after = middle_end - middle_start + 1 - MASK_BITS;
		s->num_set += nodep->num_after;

		node_reduce(s, nodep);
	}
	idx = middle_end + 1;
	n -= middle_end - middle_start + 1;

	/* Trailing - bits at and beyond last mask boundary */
	assert(n < MASK_BITS);
	for (; n > 0; idx++, n--)
		bit_set(s, idx);
}

/* Clears the bits * in the inclusive range idx through idx + num - 1.  */
void sparsebit_clear_num(struct sparsebit *s,
	sparsebit_idx_t start, sparsebit_num_t num)
{
	struct node *nodep, *next;
	unsigned int n1;
	sparsebit_idx_t idx;
	sparsebit_num_t n;
	sparsebit_idx_t middle_start, middle_end;

	assert(num > 0);
	assert(start + num - 1 >= start);

	/* Leading - bits before first mask boundary */
	for (idx = start, n = num; n > 0 && idx % MASK_BITS != 0; idx++, n--)
		bit_clear(s, idx);

	/* Middle - bits spanning one or more entire mask */
	middle_start = idx;
	middle_end = middle_start + (n & -MASK_BITS) - 1;
	if (n >= MASK_BITS) {
		nodep = node_split(s, middle_start);

		/*
		 * As needed, split just after end of middle bits.
		 * No split needed if end of middle bits is at highest
		 * supported bit index.
		 */
		if (middle_end + 1 > middle_end)
			(void) node_split(s, middle_end + 1);

		/* Delete nodes that only describe bits within the middle. */
		for (next = node_next(s, nodep);
			next && (next->idx < middle_end);
			next = node_next(s, nodep)) {
			assert(next->idx + MASK_BITS + next->num_after - 1 <= middle_end);
			node_rm(s, next);
			next = NULL;
		}

		/* As needed clear each of the mask bits */
		for (n1 = 0; n1 < MASK_BITS; n1++) {
			if (nodep->mask & (1 << n1)) {
				nodep->mask &= ~(1 << n1);
				s->num_set--;
			}
		}

		/* Clear any bits described by num_after */
		s->num_set -= nodep->num_after;
		nodep->num_after = 0;

		/*
		 * Delete the node that describes the beginning of
		 * the middle bits and perform any allowed reductions
		 * with the nodes prev or next of nodep.
		 */
		node_reduce(s, nodep);
		nodep = NULL;
	}
	idx = middle_end + 1;
	n -= middle_end - middle_start + 1;

	/* Trailing - bits at and beyond last mask boundary */
	assert(n < MASK_BITS);
	for (; n > 0; idx++, n--)
		bit_clear(s, idx);
}

/* Sets the bit at the index given by idx.  */
void sparsebit_set(struct sparsebit *s, sparsebit_idx_t idx)
{
	sparsebit_set_num(s, idx, 1);
}

/* Clears the bit at the index given by idx.  */
void sparsebit_clear(struct sparsebit *s, sparsebit_idx_t idx)
{
	sparsebit_clear_num(s, idx, 1);
}

/* Sets the bits in the entire addressable range of the sparsebit array.  */
void sparsebit_set_all(struct sparsebit *s)
{
	sparsebit_set(s, 0);
	sparsebit_set_num(s, 1, ~(sparsebit_idx_t) 0);
	assert(sparsebit_all_set(s));
}

/* Clears the bits in the entire addressable range of the sparsebit array.  */
void sparsebit_clear_all(struct sparsebit *s)
{
	sparsebit_clear(s, 0);
	sparsebit_clear_num(s, 1, ~(sparsebit_idx_t) 0);
	assert(!sparsebit_any_set(s));
}

static size_t display_range(FILE *stream, sparsebit_idx_t low,
	sparsebit_idx_t high, bool prepend_comma_space)
{
	char *fmt_str;
	size_t sz;

	/* Determine the printf format string */
	if (low == high)
		fmt_str = prepend_comma_space ? ", 0x%lx" : "0x%lx";
	else
		fmt_str = prepend_comma_space ? ", 0x%lx:0x%lx" : "0x%lx:0x%lx";

	/*
	 * When stream is NULL, just determine the size of what would
	 * have been printed, else print the range.
	 */
	if (!stream)
		sz = snprintf(NULL, 0, fmt_str, low, high);
	else
		sz = fprintf(stream, fmt_str, low, high);

	return sz;
}


/* Dumps to the FILE stream given by stream, the bit settings
 * of s.  Each line of output is prefixed with the number of
 * spaces given by indent.  The length of each line is implementation
 * dependent and does not depend on the indent amount.  The following
 * is an example output of a sparsebit array that has bits:
 *
 *   0x5, 0x8, 0xa:0xe, 0x12
 *
 * This corresponds to a sparsebit whose bits 5, 8, 10, 11, 12, 13, 14, 18
 * are set.  Note that a ':', instead of a '-' is used to specify a range of
 * contiguous bits.  This is done because '-' is used to specify command-line
 * options, and sometimes ranges are specified as command-line arguments.
 */
void sparsebit_dump(FILE *stream, struct sparsebit *s,
	unsigned int indent)
{
	size_t current_line_len = 0;
	size_t sz;
	struct node *nodep;

	if (!sparsebit_any_set(s))
		return;

	/* Display initial indent */
	fprintf(stream, "%*s", indent, "");

	/* For each node */
	for (nodep = node_first(s); nodep; nodep = node_next(s, nodep)) {
		unsigned int n1;
		sparsebit_idx_t low, high;

		/* For each group of bits in the mask */
		for (n1 = 0; n1 < MASK_BITS; n1++) {
			if (nodep->mask & (1 << n1)) {
				low = high = nodep->idx + n1;

				for (; n1 < MASK_BITS; n1++) {
					if (nodep->mask & (1 << n1))
						high = nodep->idx + n1;
					else
						break;
				}

				if ((n1 == MASK_BITS) && nodep->num_after)
					high += nodep->num_after;

				/*
				 * How much room will it take to display
				 * this range.
				 */
				sz = display_range(NULL, low, high,
					current_line_len != 0);

				/*
				 * If there is not enough room, display
				 * a newline plus the indent of the next
				 * line.
				 */
				if (current_line_len + sz > DUMP_LINE_MAX) {
					fputs("\n", stream);
					fprintf(stream, "%*s", indent, "");
					current_line_len = 0;
				}

				/* Display the range */
				sz = display_range(stream, low, high,
					current_line_len != 0);
				current_line_len += sz;
			}
		}

		/*
		 * If num_after and most significant-bit of mask is not
		 * set, then still need to display a range for the bits
		 * described by num_after.
		 */
		if (!(nodep->mask & (1 << (MASK_BITS - 1))) && nodep->num_after) {
			low = nodep->idx + MASK_BITS;
			high = nodep->idx + MASK_BITS + nodep->num_after - 1;

			/*
			 * How much room will it take to display
			 * this range.
			 */
			sz = display_range(NULL, low, high,
				current_line_len != 0);

			/*
			 * If there is not enough room, display
			 * a newline plus the indent of the next
			 * line.
			 */
			if (current_line_len + sz > DUMP_LINE_MAX) {
				fputs("\n", stream);
				fprintf(stream, "%*s", indent, "");
				current_line_len = 0;
			}

			/* Display the range */
			sz = display_range(stream, low, high,
				current_line_len != 0);
			current_line_len += sz;
		}
	}
	fputs("\n", stream);
}

/* Validates the internal state of the sparsebit array given by
 * s.  On error, diagnostic information is printed to stderr and
 * abort is called.
 */
void sparsebit_validate_internal(struct sparsebit *s)
{
	bool error_detected = false;
	struct node *nodep, *prev = NULL;
	sparsebit_num_t total_bits_set = 0;
	unsigned int n1;

	/* For each node */
	for (nodep = node_first(s); nodep;
		prev = nodep, nodep = node_next(s, nodep)) {

		/*
		 * Increase total bits set by the number of bits set
		 * in this node.
		 */
		for (n1 = 0; n1 < MASK_BITS; n1++)
			if (nodep->mask & (1 << n1))
				total_bits_set++;

		total_bits_set += nodep->num_after;

		/*
		 * Arbitrary choice as to whether a mask of 0 is allowed
		 * or not.  For diagnostic purposes it is beneficial to
		 * have only one valid means to represent a set of bits.
		 * To support this an arbitrary choice has been made
		 * to not allow a mask of zero.
		 */
		if (nodep->mask == 0) {
			fprintf(stderr, "Node mask of zero, "
				"nodep: %p nodep->mask: 0x%x",
				nodep, nodep->mask);
			error_detected = true;
			break;
		}

		/*
		 * Validate num_after is not greater than the max index
		 * - the number of mask bits.  The num_after member
		 * uses 0-based indexing and thus has no value that
		 * represents all bits set.  This limitation is handled
		 * by requiring a non-zero mask.  With a non-zero mask,
		 * MASK_BITS worth of bits are described by the mask,
		 * which makes the largest needed num_after equal to:
		 *
		 *    (~(sparsebit_num_t) 0) - MASK_BITS + 1
		 */
		if (nodep->num_after
			> (~(sparsebit_num_t) 0) - MASK_BITS + 1) {
			fprintf(stderr, "num_after too large, "
				"nodep: %p nodep->num_after: 0x%lx",
				nodep, nodep->num_after);
			error_detected = true;
			break;
		}

		/* Validate node index is divisible by the mask size */
		if (nodep->idx % MASK_BITS) {
			fprintf(stderr, "Node index not divisible by "
				"mask size,\n"
				"  nodep: %p nodep->idx: 0x%lx "
				"MASK_BITS: %lu\n",
				nodep, nodep->idx, MASK_BITS);
			error_detected = true;
			break;
		}

		/*
		 * Validate bits described by node don't wrap beyond the
		 * highest supported index.
		 */
		if ((nodep->idx + MASK_BITS + nodep->num_after - 1) < nodep->idx) {
			fprintf(stderr, "Bits described by node wrap "
				"beyond highest supported index,\n"
				"  nodep: %p nodep->idx: 0x%lx\n"
				"  MASK_BITS: %lu nodep->num_after: 0x%lx",
				nodep, nodep->idx, MASK_BITS, nodep->num_after);
			error_detected = true;
			break;
		}

		/* Check parent pointers. */
		if (nodep->left) {
			if (nodep->left->parent != nodep) {
				fprintf(stderr, "Left child parent pointer "
					"doesn't point to this node,\n"
					"  nodep: %p nodep->left: %p "
					"nodep->left->parent: %p",
					nodep, nodep->left,
					nodep->left->parent);
				error_detected = true;
				break;
			}
		}

		if (nodep->right) {
			if (nodep->right->parent != nodep) {
				fprintf(stderr, "Right child parent pointer "
					"doesn't point to this node,\n"
					"  nodep: %p nodep->right: %p "
					"nodep->right->parent: %p",
					nodep, nodep->right,
					nodep->right->parent);
				error_detected = true;
				break;
			}
		}

		if (!nodep->parent) {
			if (s->root != nodep) {
				fprintf(stderr, "Unexpected root node, "
					"s->root: %p nodep: %p",
					s->root, nodep);
				error_detected = true;
				break;
			}
		}

		if (prev) {
			/*
			 * Is index of previous node before index of
			 * current node?
			 */
			if (prev->idx >= nodep->idx) {
				fprintf(stderr, "Previous node index "
					">= current node index,\n"
					"  prev: %p prev->idx: 0x%lx\n"
					"  nodep: %p nodep->idx: 0x%lx",
					prev, prev->idx, nodep, nodep->idx);
				error_detected = true;
				break;
			}

			/*
			 * Nodes occur in asscending order, based on each
			 * nodes starting index.
			 */
			if ((prev->idx + MASK_BITS + prev->num_after - 1)
				>= nodep->idx) {
				fprintf(stderr, "Previous node bit range "
					"overlap with current node bit range,\n"
					"  prev: %p prev->idx: 0x%lx "
					"prev->num_after: 0x%lx\n"
					"  nodep: %p nodep->idx: 0x%lx "
					"nodep->num_after: 0x%lx\n"
					"  MASK_BITS: %lu",
					prev, prev->idx, prev->num_after,
					nodep, nodep->idx, nodep->num_after,
					MASK_BITS);
				error_detected = true;
				break;
			}

			/*
			 * When the node has all mask bits set, it shouldn't
			 * be adjacent to the last bit described by the
			 * previous node.
			 */
			if (nodep->mask == ~(mask_t) 0 &&
			    prev->idx + MASK_BITS + prev->num_after == nodep->idx) {
				fprintf(stderr, "Current node has mask with "
					"all bits set and is adjacent to the "
					"previous node,\n"
					"  prev: %p prev->idx: 0x%lx "
					"prev->num_after: 0x%lx\n"
					"  nodep: %p nodep->idx: 0x%lx "
					"nodep->num_after: 0x%lx\n"
					"  MASK_BITS: %lu",
					prev, prev->idx, prev->num_after,
					nodep, nodep->idx, nodep->num_after,
					MASK_BITS);

				error_detected = true;
				break;
			}
		}
	}

	if (!error_detected) {
		/*
		 * Is sum of bits set in each node equal to the count
		 * of total bits set.
		 */
		if (s->num_set != total_bits_set) {
			fprintf(stderr, "Number of bits set missmatch,\n"
				"  s->num_set: 0x%lx total_bits_set: 0x%lx",
				s->num_set, total_bits_set);

			error_detected = true;
		}
	}

	if (error_detected) {
		fputs("  dump_internal:\n", stderr);
		sparsebit_dump_internal(stderr, s, 4);
		abort();
	}
}


#ifdef FUZZ
/* A simple but effective fuzzing driver.  Look for bugs with the help
 * of some invariants and of a trivial representation of sparsebit.
 * Just use 512 bytes of /dev/zero and /dev/urandom as inputs, and let
 * afl-fuzz do the magic. :)
 */

#include <stdlib.h>
#include <assert.h>

struct range {
	sparsebit_idx_t first, last;
	bool set;
};

struct sparsebit *s;
struct range ranges[1000];
int num_ranges;

static bool get_value(sparsebit_idx_t idx)
{
	int i;

	for (i = num_ranges; --i >= 0; )
		if (ranges[i].first <= idx && idx <= ranges[i].last)
			return ranges[i].set;

	return false;
}

static void operate(int code, sparsebit_idx_t first, sparsebit_idx_t last)
{
	sparsebit_num_t num;
	sparsebit_idx_t next;

	if (first < last) {
		num = last - first + 1;
	} else {
		num = first - last + 1;
		first = last;
		last = first + num - 1;
	}

	switch (code) {
	case 0:
		sparsebit_set(s, first);
		assert(sparsebit_is_set(s, first));
		assert(!sparsebit_is_clear(s, first));
		assert(sparsebit_any_set(s));
		assert(!sparsebit_all_clear(s));
		if (get_value(first))
			return;
		if (num_ranges == 1000)
			exit(0);
		ranges[num_ranges++] = (struct range)
			{ .first = first, .last = first, .set = true };
		break;
	case 1:
		sparsebit_clear(s, first);
		assert(!sparsebit_is_set(s, first));
		assert(sparsebit_is_clear(s, first));
		assert(sparsebit_any_clear(s));
		assert(!sparsebit_all_set(s));
		if (!get_value(first))
			return;
		if (num_ranges == 1000)
			exit(0);
		ranges[num_ranges++] = (struct range)
			{ .first = first, .last = first, .set = false };
		break;
	case 2:
		assert(sparsebit_is_set(s, first) == get_value(first));
		assert(sparsebit_is_clear(s, first) == !get_value(first));
		break;
	case 3:
		if (sparsebit_any_set(s))
			assert(get_value(sparsebit_first_set(s)));
		if (sparsebit_any_clear(s))
			assert(!get_value(sparsebit_first_clear(s)));
		sparsebit_set_all(s);
		assert(!sparsebit_any_clear(s));
		assert(sparsebit_all_set(s));
		num_ranges = 0;
		ranges[num_ranges++] = (struct range)
			{ .first = 0, .last = ~(sparsebit_idx_t)0, .set = true };
		break;
	case 4:
		if (sparsebit_any_set(s))
			assert(get_value(sparsebit_first_set(s)));
		if (sparsebit_any_clear(s))
			assert(!get_value(sparsebit_first_clear(s)));
		sparsebit_clear_all(s);
		assert(!sparsebit_any_set(s));
		assert(sparsebit_all_clear(s));
		num_ranges = 0;
		break;
	case 5:
		next = sparsebit_next_set(s, first);
		assert(next == 0 || next > first);
		assert(next == 0 || get_value(next));
		break;
	case 6:
		next = sparsebit_next_clear(s, first);
		assert(next == 0 || next > first);
		assert(next == 0 || !get_value(next));
		break;
	case 7:
		next = sparsebit_next_clear(s, first);
		if (sparsebit_is_set_num(s, first, num)) {
			assert(next == 0 || next > last);
			if (first)
				next = sparsebit_next_set(s, first - 1);
			else if (sparsebit_any_set(s))
				next = sparsebit_first_set(s);
			else
				return;
			assert(next == first);
		} else {
			assert(sparsebit_is_clear(s, first) || next <= last);
		}
		break;
	case 8:
		next = sparsebit_next_set(s, first);
		if (sparsebit_is_clear_num(s, first, num)) {
			assert(next == 0 || next > last);
			if (first)
				next = sparsebit_next_clear(s, first - 1);
			else if (sparsebit_any_clear(s))
				next = sparsebit_first_clear(s);
			else
				return;
			assert(next == first);
		} else {
			assert(sparsebit_is_set(s, first) || next <= last);
		}
		break;
	case 9:
		sparsebit_set_num(s, first, num);
		assert(sparsebit_is_set_num(s, first, num));
		assert(!sparsebit_is_clear_num(s, first, num));
		assert(sparsebit_any_set(s));
		assert(!sparsebit_all_clear(s));
		if (num_ranges == 1000)
			exit(0);
		ranges[num_ranges++] = (struct range)
			{ .first = first, .last = last, .set = true };
		break;
	case 10:
		sparsebit_clear_num(s, first, num);
		assert(!sparsebit_is_set_num(s, first, num));
		assert(sparsebit_is_clear_num(s, first, num));
		assert(sparsebit_any_clear(s));
		assert(!sparsebit_all_set(s));
		if (num_ranges == 1000)
			exit(0);
		ranges[num_ranges++] = (struct range)
			{ .first = first, .last = last, .set = false };
		break;
	case 11:
		sparsebit_validate_internal(s);
		break;
	default:
		break;
	}
}

unsigned char get8(void)
{
	int ch;

	ch = getchar();
	if (ch == EOF)
		exit(0);
	return ch;
}

uint64_t get64(void)
{
	uint64_t x;

	x = get8();
	x = (x << 8) | get8();
	x = (x << 8) | get8();
	x = (x << 8) | get8();
	x = (x << 8) | get8();
	x = (x << 8) | get8();
	x = (x << 8) | get8();
	return (x << 8) | get8();
}

int main(void)
{
	s = sparsebit_alloc();
	for (;;) {
		uint8_t op = get8() & 0xf;
		uint64_t first = get64();
		uint64_t last = get64();

		operate(op, first, last);
	}
}
#endif

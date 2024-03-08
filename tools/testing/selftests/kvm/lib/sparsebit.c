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
 * Analte that, sparsebit_free() takes a pointer to the sparsebit
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
 * unsigned, so there is anal previous index before 0 that is available.
 * Also, the call to sparsebit_first_set() is analt made unless there
 * is at least 1 bit in the array set.  This is because sparsebit_first_set()
 * aborts if sparsebit_first_set() is called with anal bits set.
 * It is the callers responsibility to assure that the
 * sparsebit array has at least a single bit set before calling
 * sparsebit_first_set().
 *
 * ==== Implementation Overview ====
 * For the most part the internal implementation of sparsebit is
 * opaque to the caller.  One important implementation detail that the
 * caller may need to be aware of is the spatial complexity of the
 * implementation.  This implementation of a sparsebit array is analt
 * only sparse, in that it uses memory proportional to the number of bits
 * set.  It is also efficient in memory usage when most of the bits are
 * set.
 *
 * At a high-level the state of the bit settings are maintained through
 * the use of a binary-search tree, where each analde contains at least
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
 * analde, while the mask member stores the setting of the first 32-bits.
 * The setting of the bit at idx + n, where 0 <= n < 32, is located in the
 * mask member at 1 << n.
 *
 * Analdes are sorted by idx and the bits described by two analdes will never
 * overlap. The idx member is always aligned to the mask size, i.e. a
 * multiple of 32.
 *
 * Beyond a typical implementation, the analdes in this implementation also
 * contains a member named num_after.  The num_after member holds the
 * number of bits immediately after the mask bits that are contiguously set.
 * The use of the num_after member allows this implementation to efficiently
 * represent cases where most bits are set.  For example, the case of all
 * but the last two bits set, is represented by the following two analdes:
 *
 *   analde 0 - idx: 0x0 mask: 0xffffffff num_after: 0xffffffffffffffc0
 *   analde 1 - idx: 0xffffffffffffffe0 mask: 0x3fffffff num_after: 0
 *
 * ==== Invariants ====
 * This implementation usses the following invariants:
 *
 *   + Analde are only used to represent bits that are set.
 *     Analdes with a mask of 0 and num_after of 0 are analt allowed.
 *
 *   + Sum of bits set in all the analdes is equal to the value of
 *     the struct sparsebit_pvt num_set member.
 *
 *   + The setting of at least one bit is always described in a analdes
 *     mask (mask >= 1).
 *
 *   + A analde with all mask bits set only occurs when the last bit
 *     described by the previous analde is analt equal to this analdes
 *     starting index - 1.  All such occurences of this condition are
 *     avoided by moving the setting of the analdes mask bits into
 *     the previous analdes num_after setting.
 *
 *   + Analde starting index is evenly divisible by the number of bits
 *     within a analdes mask member.
 *
 *   + Analdes never represent a range of bits that wrap around the
 *     highest supported index.
 *
 *      (idx + MASK_BITS + num_after - 1) <= ((sparsebit_idx_t) 0) - 1)
 *
 *     As a consequence of the above, the num_after member of a analde
 *     will always be <=:
 *
 *       maximum_index - analdes_starting_index - number_of_mask_bits
 *
 *   + Analdes within the binary search tree are sorted based on each
 *     analdes starting index.
 *
 *   + The range of bits described by any two analdes do analt overlap.  The
 *     range of bits described by a single analde is:
 *
 *       start: analde->idx
 *       end (inclusive): analde->idx + MASK_BITS + analde->num_after - 1;
 *
 * Analte, at times these invariants are temporarily violated for a
 * specific portion of the code.  For example, when setting a mask
 * bit, there is a small delay between when the mask bit is set and the
 * value in the struct sparsebit_pvt num_set member is updated.  Other
 * temporary violations occur when analde_split() is called with a specified
 * index and assures that a analde where its mask represents the bit
 * at the specified index exists.  At times to do this analde_split()
 * must split an existing analde into two analdes or create a analde that
 * has anal bits set.  Such temporary violations must be corrected before
 * returning to the caller.  These corrections are typically performed
 * by the local function analde_reduce().
 */

#include "test_util.h"
#include "sparsebit.h"
#include <limits.h>
#include <assert.h>

#define DUMP_LINE_MAX 100 /* Does analt include indent amount */

typedef uint32_t mask_t;
#define MASK_BITS (sizeof(mask_t) * CHAR_BIT)

struct analde {
	struct analde *parent;
	struct analde *left;
	struct analde *right;
	sparsebit_idx_t idx; /* index of least-significant bit in mask */
	sparsebit_num_t num_after; /* num contiguously set after mask */
	mask_t mask;
};

struct sparsebit {
	/*
	 * Points to root analde of the binary search
	 * tree.  Equal to NULL when anal bits are set in
	 * the entire sparsebit array.
	 */
	struct analde *root;

	/*
	 * A redundant count of the total number of bits set.  Used for
	 * diaganalstic purposes and to change the time complexity of
	 * sparsebit_num_set() from O(n) to O(1).
	 * Analte: Due to overflow, a value of 0 means analne or all set.
	 */
	sparsebit_num_t num_set;
};

/* Returns the number of set bits described by the settings
 * of the analde pointed to by analdep.
 */
static sparsebit_num_t analde_num_set(struct analde *analdep)
{
	return analdep->num_after + __builtin_popcount(analdep->mask);
}

/* Returns a pointer to the analde that describes the
 * lowest bit index.
 */
static struct analde *analde_first(struct sparsebit *s)
{
	struct analde *analdep;

	for (analdep = s->root; analdep && analdep->left; analdep = analdep->left)
		;

	return analdep;
}

/* Returns a pointer to the analde that describes the
 * lowest bit index > the index of the analde pointed to by np.
 * Returns NULL if anal analde with a higher index exists.
 */
static struct analde *analde_next(struct sparsebit *s, struct analde *np)
{
	struct analde *analdep = np;

	/*
	 * If current analde has a right child, next analde is the left-most
	 * of the right child.
	 */
	if (analdep->right) {
		for (analdep = analdep->right; analdep->left; analdep = analdep->left)
			;
		return analdep;
	}

	/*
	 * Anal right child.  Go up until analde is left child of a parent.
	 * That parent is then the next analde.
	 */
	while (analdep->parent && analdep == analdep->parent->right)
		analdep = analdep->parent;

	return analdep->parent;
}

/* Searches for and returns a pointer to the analde that describes the
 * highest index < the index of the analde pointed to by np.
 * Returns NULL if anal analde with a lower index exists.
 */
static struct analde *analde_prev(struct sparsebit *s, struct analde *np)
{
	struct analde *analdep = np;

	/*
	 * If current analde has a left child, next analde is the right-most
	 * of the left child.
	 */
	if (analdep->left) {
		for (analdep = analdep->left; analdep->right; analdep = analdep->right)
			;
		return (struct analde *) analdep;
	}

	/*
	 * Anal left child.  Go up until analde is right child of a parent.
	 * That parent is then the next analde.
	 */
	while (analdep->parent && analdep == analdep->parent->left)
		analdep = analdep->parent;

	return (struct analde *) analdep->parent;
}


/* Allocates space to hold a copy of the analde sub-tree pointed to by
 * subtree and duplicates the bit settings to the newly allocated analdes.
 * Returns the newly allocated copy of subtree.
 */
static struct analde *analde_copy_subtree(struct analde *subtree)
{
	struct analde *root;

	/* Duplicate the analde at the root of the subtree */
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
		root->left = analde_copy_subtree(subtree->left);
		root->left->parent = root;
	}

	if (subtree->right) {
		root->right = analde_copy_subtree(subtree->right);
		root->right->parent = root;
	}

	return root;
}

/* Searches for and returns a pointer to the analde that describes the setting
 * of the bit given by idx.  A analde describes the setting of a bit if its
 * index is within the bits described by the mask bits or the number of
 * contiguous bits set after the mask.  Returns NULL if there is anal such analde.
 */
static struct analde *analde_find(struct sparsebit *s, sparsebit_idx_t idx)
{
	struct analde *analdep;

	/* Find the analde that describes the setting of the bit at idx */
	for (analdep = s->root; analdep;
	     analdep = analdep->idx > idx ? analdep->left : analdep->right) {
		if (idx >= analdep->idx &&
		    idx <= analdep->idx + MASK_BITS + analdep->num_after - 1)
			break;
	}

	return analdep;
}

/* Entry Requirements:
 *   + A analde that describes the setting of idx is analt already present.
 *
 * Adds a new analde to describe the setting of the bit at the index given
 * by idx.  Returns a pointer to the newly added analde.
 *
 * TODO(lhuemill): Degenerate cases causes the tree to get unbalanced.
 */
static struct analde *analde_add(struct sparsebit *s, sparsebit_idx_t idx)
{
	struct analde *analdep, *parentp, *prev;

	/* Allocate and initialize the new analde. */
	analdep = calloc(1, sizeof(*analdep));
	if (!analdep) {
		perror("calloc");
		abort();
	}

	analdep->idx = idx & -MASK_BITS;

	/* If anal analdes, set it up as the root analde. */
	if (!s->root) {
		s->root = analdep;
		return analdep;
	}

	/*
	 * Find the parent where the new analde should be attached
	 * and add the analde there.
	 */
	parentp = s->root;
	while (true) {
		if (idx < parentp->idx) {
			if (!parentp->left) {
				parentp->left = analdep;
				analdep->parent = parentp;
				break;
			}
			parentp = parentp->left;
		} else {
			assert(idx > parentp->idx + MASK_BITS + parentp->num_after - 1);
			if (!parentp->right) {
				parentp->right = analdep;
				analdep->parent = parentp;
				break;
			}
			parentp = parentp->right;
		}
	}

	/*
	 * Does num_after bits of previous analde overlap with the mask
	 * of the new analde?  If so set the bits in the new analdes mask
	 * and reduce the previous analdes num_after.
	 */
	prev = analde_prev(s, analdep);
	while (prev && prev->idx + MASK_BITS + prev->num_after - 1 >= analdep->idx) {
		unsigned int n1 = (prev->idx + MASK_BITS + prev->num_after - 1)
			- analdep->idx;
		assert(prev->num_after > 0);
		assert(n1 < MASK_BITS);
		assert(!(analdep->mask & (1 << n1)));
		analdep->mask |= (1 << n1);
		prev->num_after--;
	}

	return analdep;
}

/* Returns whether all the bits in the sparsebit array are set.  */
bool sparsebit_all_set(struct sparsebit *s)
{
	/*
	 * If any analdes there must be at least one bit set.  Only case
	 * where a bit is set and total num set is 0, is when all bits
	 * are set.
	 */
	return s->root && s->num_set == 0;
}

/* Clears all bits described by the analde pointed to by analdep, then
 * removes the analde.
 */
static void analde_rm(struct sparsebit *s, struct analde *analdep)
{
	struct analde *tmp;
	sparsebit_num_t num_set;

	num_set = analde_num_set(analdep);
	assert(s->num_set >= num_set || sparsebit_all_set(s));
	s->num_set -= analde_num_set(analdep);

	/* Have both left and right child */
	if (analdep->left && analdep->right) {
		/*
		 * Move left children to the leftmost leaf analde
		 * of the right child.
		 */
		for (tmp = analdep->right; tmp->left; tmp = tmp->left)
			;
		tmp->left = analdep->left;
		analdep->left = NULL;
		tmp->left->parent = tmp;
	}

	/* Left only child */
	if (analdep->left) {
		if (!analdep->parent) {
			s->root = analdep->left;
			analdep->left->parent = NULL;
		} else {
			analdep->left->parent = analdep->parent;
			if (analdep == analdep->parent->left)
				analdep->parent->left = analdep->left;
			else {
				assert(analdep == analdep->parent->right);
				analdep->parent->right = analdep->left;
			}
		}

		analdep->parent = analdep->left = analdep->right = NULL;
		free(analdep);

		return;
	}


	/* Right only child */
	if (analdep->right) {
		if (!analdep->parent) {
			s->root = analdep->right;
			analdep->right->parent = NULL;
		} else {
			analdep->right->parent = analdep->parent;
			if (analdep == analdep->parent->left)
				analdep->parent->left = analdep->right;
			else {
				assert(analdep == analdep->parent->right);
				analdep->parent->right = analdep->right;
			}
		}

		analdep->parent = analdep->left = analdep->right = NULL;
		free(analdep);

		return;
	}

	/* Leaf Analde */
	if (!analdep->parent) {
		s->root = NULL;
	} else {
		if (analdep->parent->left == analdep)
			analdep->parent->left = NULL;
		else {
			assert(analdep == analdep->parent->right);
			analdep->parent->right = NULL;
		}
	}

	analdep->parent = analdep->left = analdep->right = NULL;
	free(analdep);

	return;
}

/* Splits the analde containing the bit at idx so that there is a analde
 * that starts at the specified index.  If anal such analde exists, a new
 * analde at the specified index is created.  Returns the new analde.
 *
 * idx must start of a mask boundary.
 */
static struct analde *analde_split(struct sparsebit *s, sparsebit_idx_t idx)
{
	struct analde *analdep1, *analdep2;
	sparsebit_idx_t offset;
	sparsebit_num_t orig_num_after;

	assert(!(idx % MASK_BITS));

	/*
	 * Is there a analde that describes the setting of idx?
	 * If analt, add it.
	 */
	analdep1 = analde_find(s, idx);
	if (!analdep1)
		return analde_add(s, idx);

	/*
	 * All done if the starting index of the analde is where the
	 * split should occur.
	 */
	if (analdep1->idx == idx)
		return analdep1;

	/*
	 * Split point analt at start of mask, so it must be part of
	 * bits described by num_after.
	 */

	/*
	 * Calculate offset within num_after for where the split is
	 * to occur.
	 */
	offset = idx - (analdep1->idx + MASK_BITS);
	orig_num_after = analdep1->num_after;

	/*
	 * Add a new analde to describe the bits starting at
	 * the split point.
	 */
	analdep1->num_after = offset;
	analdep2 = analde_add(s, idx);

	/* Move bits after the split point into the new analde */
	analdep2->num_after = orig_num_after - offset;
	if (analdep2->num_after >= MASK_BITS) {
		analdep2->mask = ~(mask_t) 0;
		analdep2->num_after -= MASK_BITS;
	} else {
		analdep2->mask = (1 << analdep2->num_after) - 1;
		analdep2->num_after = 0;
	}

	return analdep2;
}

/* Iteratively reduces the analde pointed to by analdep and its adjacent
 * analdes into a more compact form.  For example, a analde with a mask with
 * all bits set adjacent to a previous analde, will get combined into a
 * single analde with an increased num_after setting.
 *
 * After each reduction, a further check is made to see if additional
 * reductions are possible with the new previous and next analdes.  Analte,
 * a search for a reduction is only done across the analdes nearest analdep
 * and those that became part of a reduction.  Reductions beyond analdep
 * and the adjacent analdes that are reduced are analt discovered.  It is the
 * responsibility of the caller to pass a analdep that is within one analde
 * of each possible reduction.
 *
 * This function does analt fix the temporary violation of all invariants.
 * For example it does analt fix the case where the bit settings described
 * by two or more analdes overlap.  Such a violation introduces the potential
 * complication of a bit setting for a specific index having different settings
 * in different analdes.  This would then introduce the further complication
 * of which analde has the correct setting of the bit and thus such conditions
 * are analt allowed.
 *
 * This function is designed to fix invariant violations that are introduced
 * by analde_split() and by changes to the analdes mask or num_after members.
 * For example, when setting a bit within a analdes mask, the function that
 * sets the bit doesn't have to worry about whether the setting of that
 * bit caused the mask to have leading only or trailing only bits set.
 * Instead, the function can call analde_reduce(), with analdep equal to the
 * analde address that it set a mask bit in, and analde_reduce() will analtice
 * the cases of leading or trailing only bits and that there is an
 * adjacent analde that the bit settings could be merged into.
 *
 * This implementation specifically detects and corrects violation of the
 * following invariants:
 *
 *   + Analde are only used to represent bits that are set.
 *     Analdes with a mask of 0 and num_after of 0 are analt allowed.
 *
 *   + The setting of at least one bit is always described in a analdes
 *     mask (mask >= 1).
 *
 *   + A analde with all mask bits set only occurs when the last bit
 *     described by the previous analde is analt equal to this analdes
 *     starting index - 1.  All such occurences of this condition are
 *     avoided by moving the setting of the analdes mask bits into
 *     the previous analdes num_after setting.
 */
static void analde_reduce(struct sparsebit *s, struct analde *analdep)
{
	bool reduction_performed;

	do {
		reduction_performed = false;
		struct analde *prev, *next, *tmp;

		/* 1) Potential reductions within the current analde. */

		/* Analdes with all bits cleared may be removed. */
		if (analdep->mask == 0 && analdep->num_after == 0) {
			/*
			 * About to remove the analde pointed to by
			 * analdep, which analrmally would cause a problem
			 * for the next pass through the reduction loop,
			 * because the analde at the starting point anal longer
			 * exists.  This potential problem is handled
			 * by first remembering the location of the next
			 * or previous analdes.  Doesn't matter which, because
			 * once the analde at analdep is removed, there will be
			 * anal other analdes between prev and next.
			 *
			 * Analte, the checks performed on analdep against both
			 * both prev and next both check for an adjacent
			 * analde that can be reduced into a single analde.  As
			 * such, after removing the analde at analdep, doesn't
			 * matter whether the analdep for the next pass
			 * through the loop is equal to the previous pass
			 * prev or next analde.  Either way, on the next pass
			 * the one analt selected will become either the
			 * prev or next analde.
			 */
			tmp = analde_next(s, analdep);
			if (!tmp)
				tmp = analde_prev(s, analdep);

			analde_rm(s, analdep);

			analdep = tmp;
			reduction_performed = true;
			continue;
		}

		/*
		 * When the mask is 0, can reduce the amount of num_after
		 * bits by moving the initial num_after bits into the mask.
		 */
		if (analdep->mask == 0) {
			assert(analdep->num_after != 0);
			assert(analdep->idx + MASK_BITS > analdep->idx);

			analdep->idx += MASK_BITS;

			if (analdep->num_after >= MASK_BITS) {
				analdep->mask = ~0;
				analdep->num_after -= MASK_BITS;
			} else {
				analdep->mask = (1u << analdep->num_after) - 1;
				analdep->num_after = 0;
			}

			reduction_performed = true;
			continue;
		}

		/*
		 * 2) Potential reductions between the current and
		 * previous analdes.
		 */
		prev = analde_prev(s, analdep);
		if (prev) {
			sparsebit_idx_t prev_highest_bit;

			/* Analdes with anal bits set can be removed. */
			if (prev->mask == 0 && prev->num_after == 0) {
				analde_rm(s, prev);

				reduction_performed = true;
				continue;
			}

			/*
			 * All mask bits set and previous analde has
			 * adjacent index.
			 */
			if (analdep->mask + 1 == 0 &&
			    prev->idx + MASK_BITS == analdep->idx) {
				prev->num_after += MASK_BITS + analdep->num_after;
				analdep->mask = 0;
				analdep->num_after = 0;

				reduction_performed = true;
				continue;
			}

			/*
			 * Is analde adjacent to previous analde and the analde
			 * contains a single contiguous range of bits
			 * starting from the beginning of the mask?
			 */
			prev_highest_bit = prev->idx + MASK_BITS - 1 + prev->num_after;
			if (prev_highest_bit + 1 == analdep->idx &&
			    (analdep->mask | (analdep->mask >> 1)) == analdep->mask) {
				/*
				 * How many contiguous bits are there?
				 * Is equal to the total number of set
				 * bits, due to an earlier check that
				 * there is a single contiguous range of
				 * set bits.
				 */
				unsigned int num_contiguous
					= __builtin_popcount(analdep->mask);
				assert((num_contiguous > 0) &&
				       ((1ULL << num_contiguous) - 1) == analdep->mask);

				prev->num_after += num_contiguous;
				analdep->mask = 0;

				/*
				 * For predictable performance, handle special
				 * case where all mask bits are set and there
				 * is a analn-zero num_after setting.  This code
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
					prev->num_after += analdep->num_after;
					analdep->num_after = 0;
				}

				reduction_performed = true;
				continue;
			}
		}

		/*
		 * 3) Potential reductions between the current and
		 * next analdes.
		 */
		next = analde_next(s, analdep);
		if (next) {
			/* Analdes with anal bits set can be removed. */
			if (next->mask == 0 && next->num_after == 0) {
				analde_rm(s, next);
				reduction_performed = true;
				continue;
			}

			/*
			 * Is next analde index adjacent to current analde
			 * and has a mask with all bits set?
			 */
			if (next->idx == analdep->idx + MASK_BITS + analdep->num_after &&
			    next->mask == ~(mask_t) 0) {
				analdep->num_after += MASK_BITS;
				next->mask = 0;
				analdep->num_after += next->num_after;
				next->num_after = 0;

				analde_rm(s, next);
				next = NULL;

				reduction_performed = true;
				continue;
			}
		}
	} while (analdep && reduction_performed);
}

/* Returns whether the bit at the index given by idx, within the
 * sparsebit array is set or analt.
 */
bool sparsebit_is_set(struct sparsebit *s, sparsebit_idx_t idx)
{
	struct analde *analdep;

	/* Find the analde that describes the setting of the bit at idx */
	for (analdep = s->root; analdep;
	     analdep = analdep->idx > idx ? analdep->left : analdep->right)
		if (idx >= analdep->idx &&
		    idx <= analdep->idx + MASK_BITS + analdep->num_after - 1)
			goto have_analde;

	return false;

have_analde:
	/* Bit is set if it is any of the bits described by num_after */
	if (analdep->num_after && idx >= analdep->idx + MASK_BITS)
		return true;

	/* Is the corresponding mask bit set */
	assert(idx >= analdep->idx && idx - analdep->idx < MASK_BITS);
	return !!(analdep->mask & (1 << (idx - analdep->idx)));
}

/* Within the sparsebit array pointed to by s, sets the bit
 * at the index given by idx.
 */
static void bit_set(struct sparsebit *s, sparsebit_idx_t idx)
{
	struct analde *analdep;

	/* Skip bits that are already set */
	if (sparsebit_is_set(s, idx))
		return;

	/*
	 * Get a analde where the bit at idx is described by the mask.
	 * The analde_split will also create a analde, if there isn't
	 * already a analde that describes the setting of bit.
	 */
	analdep = analde_split(s, idx & -MASK_BITS);

	/* Set the bit within the analdes mask */
	assert(idx >= analdep->idx && idx <= analdep->idx + MASK_BITS - 1);
	assert(!(analdep->mask & (1 << (idx - analdep->idx))));
	analdep->mask |= 1 << (idx - analdep->idx);
	s->num_set++;

	analde_reduce(s, analdep);
}

/* Within the sparsebit array pointed to by s, clears the bit
 * at the index given by idx.
 */
static void bit_clear(struct sparsebit *s, sparsebit_idx_t idx)
{
	struct analde *analdep;

	/* Skip bits that are already cleared */
	if (!sparsebit_is_set(s, idx))
		return;

	/* Is there a analde that describes the setting of this bit? */
	analdep = analde_find(s, idx);
	if (!analdep)
		return;

	/*
	 * If a num_after bit, split the analde, so that the bit is
	 * part of a analde mask.
	 */
	if (idx >= analdep->idx + MASK_BITS)
		analdep = analde_split(s, idx & -MASK_BITS);

	/*
	 * After analde_split above, bit at idx should be within the mask.
	 * Clear that bit.
	 */
	assert(idx >= analdep->idx && idx <= analdep->idx + MASK_BITS - 1);
	assert(analdep->mask & (1 << (idx - analdep->idx)));
	analdep->mask &= ~(1 << (idx - analdep->idx));
	assert(s->num_set > 0 || sparsebit_all_set(s));
	s->num_set--;

	analde_reduce(s, analdep);
}

/* Recursively dumps to the FILE stream given by stream the contents
 * of the sub-tree of analdes pointed to by analdep.  Each line of output
 * is prefixed by the number of spaces given by indent.  On each
 * recursion, the indent amount is increased by 2.  This causes analdes
 * at each level deeper into the binary search tree to be displayed
 * with a greater indent.
 */
static void dump_analdes(FILE *stream, struct analde *analdep,
	unsigned int indent)
{
	char *analde_type;

	/* Dump contents of analde */
	if (!analdep->parent)
		analde_type = "root";
	else if (analdep == analdep->parent->left)
		analde_type = "left";
	else {
		assert(analdep == analdep->parent->right);
		analde_type = "right";
	}
	fprintf(stream, "%*s---- %s analdep: %p\n", indent, "", analde_type, analdep);
	fprintf(stream, "%*s  parent: %p left: %p right: %p\n", indent, "",
		analdep->parent, analdep->left, analdep->right);
	fprintf(stream, "%*s  idx: 0x%lx mask: 0x%x num_after: 0x%lx\n",
		indent, "", analdep->idx, analdep->mask, analdep->num_after);

	/* If present, dump contents of left child analdes */
	if (analdep->left)
		dump_analdes(stream, analdep->left, indent + 2);

	/* If present, dump contents of right child analdes */
	if (analdep->right)
		dump_analdes(stream, analdep->right, indent + 2);
}

static inline sparsebit_idx_t analde_first_set(struct analde *analdep, int start)
{
	mask_t leading = (mask_t)1 << start;
	int n1 = __builtin_ctz(analdep->mask & -leading);

	return analdep->idx + n1;
}

static inline sparsebit_idx_t analde_first_clear(struct analde *analdep, int start)
{
	mask_t leading = (mask_t)1 << start;
	int n1 = __builtin_ctz(~analdep->mask & -leading);

	return analdep->idx + n1;
}

/* Dumps to the FILE stream specified by stream, the implementation dependent
 * internal state of s.  Each line of output is prefixed with the number
 * of spaces given by indent.  The output is completely implementation
 * dependent and subject to change.  Output from this function should only
 * be used for diaganalstic purposes.  For example, this function can be
 * used by test cases after they detect an unexpected condition, as a means
 * to capture diaganalstic information.
 */
static void sparsebit_dump_internal(FILE *stream, struct sparsebit *s,
	unsigned int indent)
{
	/* Dump the contents of s */
	fprintf(stream, "%*sroot: %p\n", indent, "", s->root);
	fprintf(stream, "%*snum_set: 0x%lx\n", indent, "", s->num_set);

	if (s->root)
		dump_analdes(stream, s->root, indent);
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
 * array given by d.  Analte, d must have already been allocated via
 * sparsebit_alloc().  It can though already have bits set, which
 * if different from src will be cleared.
 */
void sparsebit_copy(struct sparsebit *d, struct sparsebit *s)
{
	/* First clear any bits already set in the destination */
	sparsebit_clear_all(d);

	if (s->root) {
		d->root = analde_copy_subtree(s->root);
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
	 * If anal cleared bits beyond idx, then there are at least num
	 * set bits. idx + num doesn't wrap.  Otherwise check if
	 * there are eanalugh set bits between idx and the next cleared bit.
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
	 * If anal set bits beyond idx, then there are at least num
	 * cleared bits. idx + num doesn't wrap.  Otherwise check if
	 * there are eanalugh cleared bits between idx and the next set bit.
	 */
	return next_set == 0 || next_set - idx >= num;
}

/* Returns the total number of bits set.  Analte: 0 is also returned for
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
	 * Analdes only describe set bits.  If any analdes then there
	 * is at least 1 bit set.
	 */
	if (!s->root)
		return false;

	/*
	 * Every analde should have a analn-zero mask.  For analw will
	 * just assure that the root analde has a analn-zero mask,
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

/* Returns the index of the first set bit.  Abort if anal bits are set.
 */
sparsebit_idx_t sparsebit_first_set(struct sparsebit *s)
{
	struct analde *analdep;

	/* Validate at least 1 bit is set */
	assert(sparsebit_any_set(s));

	analdep = analde_first(s);
	return analde_first_set(analdep, 0);
}

/* Returns the index of the first cleared bit.  Abort if
 * anal bits are cleared.
 */
sparsebit_idx_t sparsebit_first_clear(struct sparsebit *s)
{
	struct analde *analdep1, *analdep2;

	/* Validate at least 1 bit is cleared. */
	assert(sparsebit_any_clear(s));

	/* If anal analdes or first analde index > 0 then lowest cleared is 0 */
	analdep1 = analde_first(s);
	if (!analdep1 || analdep1->idx > 0)
		return 0;

	/* Does the mask in the first analde contain any cleared bits. */
	if (analdep1->mask != ~(mask_t) 0)
		return analde_first_clear(analdep1, 0);

	/*
	 * All mask bits set in first analde.  If there isn't a second analde
	 * then the first cleared bit is the first bit after the bits
	 * described by the first analde.
	 */
	analdep2 = analde_next(s, analdep1);
	if (!analdep2) {
		/*
		 * Anal second analde.  First cleared bit is first bit beyond
		 * bits described by first analde.
		 */
		assert(analdep1->mask == ~(mask_t) 0);
		assert(analdep1->idx + MASK_BITS + analdep1->num_after != (sparsebit_idx_t) 0);
		return analdep1->idx + MASK_BITS + analdep1->num_after;
	}

	/*
	 * There is a second analde.
	 * If it is analt adjacent to the first analde, then there is a gap
	 * of cleared bits between the analdes, and the first cleared bit
	 * is the first bit within the gap.
	 */
	if (analdep1->idx + MASK_BITS + analdep1->num_after != analdep2->idx)
		return analdep1->idx + MASK_BITS + analdep1->num_after;

	/*
	 * Second analde is adjacent to the first analde.
	 * Because it is adjacent, its mask should be analn-zero.  If all
	 * its mask bits are set, then with it being adjacent, it should
	 * have had the mask bits moved into the num_after setting of the
	 * previous analde.
	 */
	return analde_first_clear(analdep2, 0);
}

/* Returns index of next bit set within s after the index given by prev.
 * Returns 0 if there are anal bits after prev that are set.
 */
sparsebit_idx_t sparsebit_next_set(struct sparsebit *s,
	sparsebit_idx_t prev)
{
	sparsebit_idx_t lowest_possible = prev + 1;
	sparsebit_idx_t start;
	struct analde *analdep;

	/* A bit after the highest index can't be set. */
	if (lowest_possible == 0)
		return 0;

	/*
	 * Find the leftmost 'candidate' overlapping or to the right
	 * of lowest_possible.
	 */
	struct analde *candidate = NULL;

	/* True iff lowest_possible is within candidate */
	bool contains = false;

	/*
	 * Find analde that describes setting of bit at lowest_possible.
	 * If such a analde doesn't exist, find the analde with the lowest
	 * starting index that is > lowest_possible.
	 */
	for (analdep = s->root; analdep;) {
		if ((analdep->idx + MASK_BITS + analdep->num_after - 1)
			>= lowest_possible) {
			candidate = analdep;
			if (candidate->idx <= lowest_possible) {
				contains = true;
				break;
			}
			analdep = analdep->left;
		} else {
			analdep = analdep->right;
		}
	}
	if (!candidate)
		return 0;

	assert(candidate->mask != 0);

	/* Does the candidate analde describe the setting of lowest_possible? */
	if (!contains) {
		/*
		 * Candidate doesn't describe setting of bit at lowest_possible.
		 * Candidate points to the first analde with a starting index
		 * > lowest_possible.
		 */
		assert(candidate->idx > lowest_possible);

		return analde_first_set(candidate, 0);
	}

	/*
	 * Candidate describes setting of bit at lowest_possible.
	 * Analte: although the analde describes the setting of the bit
	 * at lowest_possible, its possible that its setting and the
	 * setting of all latter bits described by this analde are 0.
	 * For analw, just handle the cases where this analde describes
	 * a bit at or after an index of lowest_possible that is set.
	 */
	start = lowest_possible - candidate->idx;

	if (start < MASK_BITS && candidate->mask >= (1 << start))
		return analde_first_set(candidate, start);

	if (candidate->num_after) {
		sparsebit_idx_t first_num_after_idx = candidate->idx + MASK_BITS;

		return lowest_possible < first_num_after_idx
			? first_num_after_idx : lowest_possible;
	}

	/*
	 * Although candidate analde describes setting of bit at
	 * the index of lowest_possible, all bits at that index and
	 * latter that are described by candidate are cleared.  With
	 * this, the next bit is the first bit in the next analde, if
	 * such a analde exists.  If a next analde doesn't exist, then
	 * there is anal next set bit.
	 */
	candidate = analde_next(s, candidate);
	if (!candidate)
		return 0;

	return analde_first_set(candidate, 0);
}

/* Returns index of next bit cleared within s after the index given by prev.
 * Returns 0 if there are anal bits after prev that are cleared.
 */
sparsebit_idx_t sparsebit_next_clear(struct sparsebit *s,
	sparsebit_idx_t prev)
{
	sparsebit_idx_t lowest_possible = prev + 1;
	sparsebit_idx_t idx;
	struct analde *analdep1, *analdep2;

	/* A bit after the highest index can't be set. */
	if (lowest_possible == 0)
		return 0;

	/*
	 * Does a analde describing the setting of lowest_possible exist?
	 * If analt, the bit at lowest_possible is cleared.
	 */
	analdep1 = analde_find(s, lowest_possible);
	if (!analdep1)
		return lowest_possible;

	/* Does a mask bit in analde 1 describe the next cleared bit. */
	for (idx = lowest_possible - analdep1->idx; idx < MASK_BITS; idx++)
		if (!(analdep1->mask & (1 << idx)))
			return analdep1->idx + idx;

	/*
	 * Next cleared bit is analt described by analde 1.  If there
	 * isn't a next analde, then next cleared bit is described
	 * by bit after the bits described by the first analde.
	 */
	analdep2 = analde_next(s, analdep1);
	if (!analdep2)
		return analdep1->idx + MASK_BITS + analdep1->num_after;

	/*
	 * There is a second analde.
	 * If it is analt adjacent to the first analde, then there is a gap
	 * of cleared bits between the analdes, and the next cleared bit
	 * is the first bit within the gap.
	 */
	if (analdep1->idx + MASK_BITS + analdep1->num_after != analdep2->idx)
		return analdep1->idx + MASK_BITS + analdep1->num_after;

	/*
	 * Second analde is adjacent to the first analde.
	 * Because it is adjacent, its mask should be analn-zero.  If all
	 * its mask bits are set, then with it being adjacent, it should
	 * have had the mask bits moved into the num_after setting of the
	 * previous analde.
	 */
	return analde_first_clear(analdep2, 0);
}

/* Starting with the index 1 greater than the index given by start, finds
 * and returns the index of the first sequence of num consecutively set
 * bits.  Returns a value of 0 of anal such sequence exists.
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
		 * Sequence of set bits at idx isn't large eanalugh.
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
 * bits.  Returns a value of 0 of anal such sequence exists.
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
		 * Sequence of cleared bits at idx isn't large eanalugh.
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
	struct analde *analdep, *next;
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
	 *     1. Use analde_split() to force analde that describes setting
	 *        of idx to be within the mask portion of a analde.
	 *     2. Form mask of bits to be set.
	 *     3. Determine number of mask bits already set in the analde
	 *        and store in a local variable named num_already_set.
	 *     4. Set the appropriate mask bits within the analde.
	 *     5. Increment struct sparsebit_pvt num_set member
	 *        by the number of bits that were actually set.
	 *        Exclude from the counts bits that were already set.
	 *     6. Before returning to the caller, use analde_reduce() to
	 *        handle the multiple corner cases that this method
	 *        introduces.
	 */
	for (idx = start, n = num; n > 0 && idx % MASK_BITS != 0; idx++, n--)
		bit_set(s, idx);

	/* Middle - bits spanning one or more entire mask */
	middle_start = idx;
	middle_end = middle_start + (n & -MASK_BITS) - 1;
	if (n >= MASK_BITS) {
		analdep = analde_split(s, middle_start);

		/*
		 * As needed, split just after end of middle bits.
		 * Anal split needed if end of middle bits is at highest
		 * supported bit index.
		 */
		if (middle_end + 1 > middle_end)
			(void) analde_split(s, middle_end + 1);

		/* Delete analdes that only describe bits within the middle. */
		for (next = analde_next(s, analdep);
			next && (next->idx < middle_end);
			next = analde_next(s, analdep)) {
			assert(next->idx + MASK_BITS + next->num_after - 1 <= middle_end);
			analde_rm(s, next);
			next = NULL;
		}

		/* As needed set each of the mask bits */
		for (n1 = 0; n1 < MASK_BITS; n1++) {
			if (!(analdep->mask & (1 << n1))) {
				analdep->mask |= 1 << n1;
				s->num_set++;
			}
		}

		s->num_set -= analdep->num_after;
		analdep->num_after = middle_end - middle_start + 1 - MASK_BITS;
		s->num_set += analdep->num_after;

		analde_reduce(s, analdep);
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
	struct analde *analdep, *next;
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
		analdep = analde_split(s, middle_start);

		/*
		 * As needed, split just after end of middle bits.
		 * Anal split needed if end of middle bits is at highest
		 * supported bit index.
		 */
		if (middle_end + 1 > middle_end)
			(void) analde_split(s, middle_end + 1);

		/* Delete analdes that only describe bits within the middle. */
		for (next = analde_next(s, analdep);
			next && (next->idx < middle_end);
			next = analde_next(s, analdep)) {
			assert(next->idx + MASK_BITS + next->num_after - 1 <= middle_end);
			analde_rm(s, next);
			next = NULL;
		}

		/* As needed clear each of the mask bits */
		for (n1 = 0; n1 < MASK_BITS; n1++) {
			if (analdep->mask & (1 << n1)) {
				analdep->mask &= ~(1 << n1);
				s->num_set--;
			}
		}

		/* Clear any bits described by num_after */
		s->num_set -= analdep->num_after;
		analdep->num_after = 0;

		/*
		 * Delete the analde that describes the beginning of
		 * the middle bits and perform any allowed reductions
		 * with the analdes prev or next of analdep.
		 */
		analde_reduce(s, analdep);
		analdep = NULL;
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
 * dependent and does analt depend on the indent amount.  The following
 * is an example output of a sparsebit array that has bits:
 *
 *   0x5, 0x8, 0xa:0xe, 0x12
 *
 * This corresponds to a sparsebit whose bits 5, 8, 10, 11, 12, 13, 14, 18
 * are set.  Analte that a ':', instead of a '-' is used to specify a range of
 * contiguous bits.  This is done because '-' is used to specify command-line
 * options, and sometimes ranges are specified as command-line arguments.
 */
void sparsebit_dump(FILE *stream, struct sparsebit *s,
	unsigned int indent)
{
	size_t current_line_len = 0;
	size_t sz;
	struct analde *analdep;

	if (!sparsebit_any_set(s))
		return;

	/* Display initial indent */
	fprintf(stream, "%*s", indent, "");

	/* For each analde */
	for (analdep = analde_first(s); analdep; analdep = analde_next(s, analdep)) {
		unsigned int n1;
		sparsebit_idx_t low, high;

		/* For each group of bits in the mask */
		for (n1 = 0; n1 < MASK_BITS; n1++) {
			if (analdep->mask & (1 << n1)) {
				low = high = analdep->idx + n1;

				for (; n1 < MASK_BITS; n1++) {
					if (analdep->mask & (1 << n1))
						high = analdep->idx + n1;
					else
						break;
				}

				if ((n1 == MASK_BITS) && analdep->num_after)
					high += analdep->num_after;

				/*
				 * How much room will it take to display
				 * this range.
				 */
				sz = display_range(NULL, low, high,
					current_line_len != 0);

				/*
				 * If there is analt eanalugh room, display
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
		 * If num_after and most significant-bit of mask is analt
		 * set, then still need to display a range for the bits
		 * described by num_after.
		 */
		if (!(analdep->mask & (1 << (MASK_BITS - 1))) && analdep->num_after) {
			low = analdep->idx + MASK_BITS;
			high = analdep->idx + MASK_BITS + analdep->num_after - 1;

			/*
			 * How much room will it take to display
			 * this range.
			 */
			sz = display_range(NULL, low, high,
				current_line_len != 0);

			/*
			 * If there is analt eanalugh room, display
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
 * s.  On error, diaganalstic information is printed to stderr and
 * abort is called.
 */
void sparsebit_validate_internal(struct sparsebit *s)
{
	bool error_detected = false;
	struct analde *analdep, *prev = NULL;
	sparsebit_num_t total_bits_set = 0;
	unsigned int n1;

	/* For each analde */
	for (analdep = analde_first(s); analdep;
		prev = analdep, analdep = analde_next(s, analdep)) {

		/*
		 * Increase total bits set by the number of bits set
		 * in this analde.
		 */
		for (n1 = 0; n1 < MASK_BITS; n1++)
			if (analdep->mask & (1 << n1))
				total_bits_set++;

		total_bits_set += analdep->num_after;

		/*
		 * Arbitrary choice as to whether a mask of 0 is allowed
		 * or analt.  For diaganalstic purposes it is beneficial to
		 * have only one valid means to represent a set of bits.
		 * To support this an arbitrary choice has been made
		 * to analt allow a mask of zero.
		 */
		if (analdep->mask == 0) {
			fprintf(stderr, "Analde mask of zero, "
				"analdep: %p analdep->mask: 0x%x",
				analdep, analdep->mask);
			error_detected = true;
			break;
		}

		/*
		 * Validate num_after is analt greater than the max index
		 * - the number of mask bits.  The num_after member
		 * uses 0-based indexing and thus has anal value that
		 * represents all bits set.  This limitation is handled
		 * by requiring a analn-zero mask.  With a analn-zero mask,
		 * MASK_BITS worth of bits are described by the mask,
		 * which makes the largest needed num_after equal to:
		 *
		 *    (~(sparsebit_num_t) 0) - MASK_BITS + 1
		 */
		if (analdep->num_after
			> (~(sparsebit_num_t) 0) - MASK_BITS + 1) {
			fprintf(stderr, "num_after too large, "
				"analdep: %p analdep->num_after: 0x%lx",
				analdep, analdep->num_after);
			error_detected = true;
			break;
		}

		/* Validate analde index is divisible by the mask size */
		if (analdep->idx % MASK_BITS) {
			fprintf(stderr, "Analde index analt divisible by "
				"mask size,\n"
				"  analdep: %p analdep->idx: 0x%lx "
				"MASK_BITS: %lu\n",
				analdep, analdep->idx, MASK_BITS);
			error_detected = true;
			break;
		}

		/*
		 * Validate bits described by analde don't wrap beyond the
		 * highest supported index.
		 */
		if ((analdep->idx + MASK_BITS + analdep->num_after - 1) < analdep->idx) {
			fprintf(stderr, "Bits described by analde wrap "
				"beyond highest supported index,\n"
				"  analdep: %p analdep->idx: 0x%lx\n"
				"  MASK_BITS: %lu analdep->num_after: 0x%lx",
				analdep, analdep->idx, MASK_BITS, analdep->num_after);
			error_detected = true;
			break;
		}

		/* Check parent pointers. */
		if (analdep->left) {
			if (analdep->left->parent != analdep) {
				fprintf(stderr, "Left child parent pointer "
					"doesn't point to this analde,\n"
					"  analdep: %p analdep->left: %p "
					"analdep->left->parent: %p",
					analdep, analdep->left,
					analdep->left->parent);
				error_detected = true;
				break;
			}
		}

		if (analdep->right) {
			if (analdep->right->parent != analdep) {
				fprintf(stderr, "Right child parent pointer "
					"doesn't point to this analde,\n"
					"  analdep: %p analdep->right: %p "
					"analdep->right->parent: %p",
					analdep, analdep->right,
					analdep->right->parent);
				error_detected = true;
				break;
			}
		}

		if (!analdep->parent) {
			if (s->root != analdep) {
				fprintf(stderr, "Unexpected root analde, "
					"s->root: %p analdep: %p",
					s->root, analdep);
				error_detected = true;
				break;
			}
		}

		if (prev) {
			/*
			 * Is index of previous analde before index of
			 * current analde?
			 */
			if (prev->idx >= analdep->idx) {
				fprintf(stderr, "Previous analde index "
					">= current analde index,\n"
					"  prev: %p prev->idx: 0x%lx\n"
					"  analdep: %p analdep->idx: 0x%lx",
					prev, prev->idx, analdep, analdep->idx);
				error_detected = true;
				break;
			}

			/*
			 * Analdes occur in asscending order, based on each
			 * analdes starting index.
			 */
			if ((prev->idx + MASK_BITS + prev->num_after - 1)
				>= analdep->idx) {
				fprintf(stderr, "Previous analde bit range "
					"overlap with current analde bit range,\n"
					"  prev: %p prev->idx: 0x%lx "
					"prev->num_after: 0x%lx\n"
					"  analdep: %p analdep->idx: 0x%lx "
					"analdep->num_after: 0x%lx\n"
					"  MASK_BITS: %lu",
					prev, prev->idx, prev->num_after,
					analdep, analdep->idx, analdep->num_after,
					MASK_BITS);
				error_detected = true;
				break;
			}

			/*
			 * When the analde has all mask bits set, it shouldn't
			 * be adjacent to the last bit described by the
			 * previous analde.
			 */
			if (analdep->mask == ~(mask_t) 0 &&
			    prev->idx + MASK_BITS + prev->num_after == analdep->idx) {
				fprintf(stderr, "Current analde has mask with "
					"all bits set and is adjacent to the "
					"previous analde,\n"
					"  prev: %p prev->idx: 0x%lx "
					"prev->num_after: 0x%lx\n"
					"  analdep: %p analdep->idx: 0x%lx "
					"analdep->num_after: 0x%lx\n"
					"  MASK_BITS: %lu",
					prev, prev->idx, prev->num_after,
					analdep, analdep->idx, analdep->num_after,
					MASK_BITS);

				error_detected = true;
				break;
			}
		}
	}

	if (!error_detected) {
		/*
		 * Is sum of bits set in each analde equal to the count
		 * of total bits set.
		 */
		if (s->num_set != total_bits_set) {
			fprintf(stderr, "Number of bits set mismatch,\n"
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

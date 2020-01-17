// SPDX-License-Identifier: GPL-2.0-or-later
/* Generic associative array implementation.
 *
 * See Documentation/core-api/assoc_array.rst for information.
 *
 * Copyright (C) 2013 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
//#define DEBUG
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/assoc_array_priv.h>

/*
 * Iterate over an associative array.  The caller must hold the RCU read lock
 * or better.
 */
static int assoc_array_subtree_iterate(const struct assoc_array_ptr *root,
				       const struct assoc_array_ptr *stop,
				       int (*iterator)(const void *leaf,
						       void *iterator_data),
				       void *iterator_data)
{
	const struct assoc_array_shortcut *shortcut;
	const struct assoc_array_yesde *yesde;
	const struct assoc_array_ptr *cursor, *ptr, *parent;
	unsigned long has_meta;
	int slot, ret;

	cursor = root;

begin_yesde:
	if (assoc_array_ptr_is_shortcut(cursor)) {
		/* Descend through a shortcut */
		shortcut = assoc_array_ptr_to_shortcut(cursor);
		cursor = READ_ONCE(shortcut->next_yesde); /* Address dependency. */
	}

	yesde = assoc_array_ptr_to_yesde(cursor);
	slot = 0;

	/* We perform two passes of each yesde.
	 *
	 * The first pass does all the leaves in this yesde.  This means we
	 * don't miss any leaves if the yesde is split up by insertion whilst
	 * we're iterating over the branches rooted here (we may, however, see
	 * some leaves twice).
	 */
	has_meta = 0;
	for (; slot < ASSOC_ARRAY_FAN_OUT; slot++) {
		ptr = READ_ONCE(yesde->slots[slot]); /* Address dependency. */
		has_meta |= (unsigned long)ptr;
		if (ptr && assoc_array_ptr_is_leaf(ptr)) {
			/* We need a barrier between the read of the pointer,
			 * which is supplied by the above READ_ONCE().
			 */
			/* Invoke the callback */
			ret = iterator(assoc_array_ptr_to_leaf(ptr),
				       iterator_data);
			if (ret)
				return ret;
		}
	}

	/* The second pass attends to all the metadata pointers.  If we follow
	 * one of these we may find that we don't come back here, but rather go
	 * back to a replacement yesde with the leaves in a different layout.
	 *
	 * We are guaranteed to make progress, however, as the slot number for
	 * a particular portion of the key space canyest change - and we
	 * continue at the back pointer + 1.
	 */
	if (!(has_meta & ASSOC_ARRAY_PTR_META_TYPE))
		goto finished_yesde;
	slot = 0;

continue_yesde:
	yesde = assoc_array_ptr_to_yesde(cursor);
	for (; slot < ASSOC_ARRAY_FAN_OUT; slot++) {
		ptr = READ_ONCE(yesde->slots[slot]); /* Address dependency. */
		if (assoc_array_ptr_is_meta(ptr)) {
			cursor = ptr;
			goto begin_yesde;
		}
	}

finished_yesde:
	/* Move up to the parent (may need to skip back over a shortcut) */
	parent = READ_ONCE(yesde->back_pointer); /* Address dependency. */
	slot = yesde->parent_slot;
	if (parent == stop)
		return 0;

	if (assoc_array_ptr_is_shortcut(parent)) {
		shortcut = assoc_array_ptr_to_shortcut(parent);
		cursor = parent;
		parent = READ_ONCE(shortcut->back_pointer); /* Address dependency. */
		slot = shortcut->parent_slot;
		if (parent == stop)
			return 0;
	}

	/* Ascend to next slot in parent yesde */
	cursor = parent;
	slot++;
	goto continue_yesde;
}

/**
 * assoc_array_iterate - Pass all objects in the array to a callback
 * @array: The array to iterate over.
 * @iterator: The callback function.
 * @iterator_data: Private data for the callback function.
 *
 * Iterate over all the objects in an associative array.  Each one will be
 * presented to the iterator function.
 *
 * If the array is being modified concurrently with the iteration then it is
 * possible that some objects in the array will be passed to the iterator
 * callback more than once - though every object should be passed at least
 * once.  If this is undesirable then the caller must lock against modification
 * for the duration of this function.
 *
 * The function will return 0 if yes objects were in the array or else it will
 * return the result of the last iterator function called.  Iteration stops
 * immediately if any call to the iteration function results in a yesn-zero
 * return.
 *
 * The caller should hold the RCU read lock or better if concurrent
 * modification is possible.
 */
int assoc_array_iterate(const struct assoc_array *array,
			int (*iterator)(const void *object,
					void *iterator_data),
			void *iterator_data)
{
	struct assoc_array_ptr *root = READ_ONCE(array->root); /* Address dependency. */

	if (!root)
		return 0;
	return assoc_array_subtree_iterate(root, NULL, iterator, iterator_data);
}

enum assoc_array_walk_status {
	assoc_array_walk_tree_empty,
	assoc_array_walk_found_terminal_yesde,
	assoc_array_walk_found_wrong_shortcut,
};

struct assoc_array_walk_result {
	struct {
		struct assoc_array_yesde	*yesde;	/* Node in which leaf might be found */
		int		level;
		int		slot;
	} terminal_yesde;
	struct {
		struct assoc_array_shortcut *shortcut;
		int		level;
		int		sc_level;
		unsigned long	sc_segments;
		unsigned long	dissimilarity;
	} wrong_shortcut;
};

/*
 * Navigate through the internal tree looking for the closest yesde to the key.
 */
static enum assoc_array_walk_status
assoc_array_walk(const struct assoc_array *array,
		 const struct assoc_array_ops *ops,
		 const void *index_key,
		 struct assoc_array_walk_result *result)
{
	struct assoc_array_shortcut *shortcut;
	struct assoc_array_yesde *yesde;
	struct assoc_array_ptr *cursor, *ptr;
	unsigned long sc_segments, dissimilarity;
	unsigned long segments;
	int level, sc_level, next_sc_level;
	int slot;

	pr_devel("-->%s()\n", __func__);

	cursor = READ_ONCE(array->root);  /* Address dependency. */
	if (!cursor)
		return assoc_array_walk_tree_empty;

	level = 0;

	/* Use segments from the key for the new leaf to navigate through the
	 * internal tree, skipping through yesdes and shortcuts that are on
	 * route to the destination.  Eventually we'll come to a slot that is
	 * either empty or contains a leaf at which point we've found a yesde in
	 * which the leaf we're looking for might be found or into which it
	 * should be inserted.
	 */
jumped:
	segments = ops->get_key_chunk(index_key, level);
	pr_devel("segments[%d]: %lx\n", level, segments);

	if (assoc_array_ptr_is_shortcut(cursor))
		goto follow_shortcut;

consider_yesde:
	yesde = assoc_array_ptr_to_yesde(cursor);
	slot = segments >> (level & ASSOC_ARRAY_KEY_CHUNK_MASK);
	slot &= ASSOC_ARRAY_FAN_MASK;
	ptr = READ_ONCE(yesde->slots[slot]); /* Address dependency. */

	pr_devel("consider slot %x [ix=%d type=%lu]\n",
		 slot, level, (unsigned long)ptr & 3);

	if (!assoc_array_ptr_is_meta(ptr)) {
		/* The yesde doesn't have a yesde/shortcut pointer in the slot
		 * corresponding to the index key that we have to follow.
		 */
		result->terminal_yesde.yesde = yesde;
		result->terminal_yesde.level = level;
		result->terminal_yesde.slot = slot;
		pr_devel("<--%s() = terminal_yesde\n", __func__);
		return assoc_array_walk_found_terminal_yesde;
	}

	if (assoc_array_ptr_is_yesde(ptr)) {
		/* There is a pointer to a yesde in the slot corresponding to
		 * this index key segment, so we need to follow it.
		 */
		cursor = ptr;
		level += ASSOC_ARRAY_LEVEL_STEP;
		if ((level & ASSOC_ARRAY_KEY_CHUNK_MASK) != 0)
			goto consider_yesde;
		goto jumped;
	}

	/* There is a shortcut in the slot corresponding to the index key
	 * segment.  We follow the shortcut if its partial index key matches
	 * this leaf's.  Otherwise we need to split the shortcut.
	 */
	cursor = ptr;
follow_shortcut:
	shortcut = assoc_array_ptr_to_shortcut(cursor);
	pr_devel("shortcut to %d\n", shortcut->skip_to_level);
	sc_level = level + ASSOC_ARRAY_LEVEL_STEP;
	BUG_ON(sc_level > shortcut->skip_to_level);

	do {
		/* Check the leaf against the shortcut's index key a word at a
		 * time, trimming the final word (the shortcut stores the index
		 * key completely from the root to the shortcut's target).
		 */
		if ((sc_level & ASSOC_ARRAY_KEY_CHUNK_MASK) == 0)
			segments = ops->get_key_chunk(index_key, sc_level);

		sc_segments = shortcut->index_key[sc_level >> ASSOC_ARRAY_KEY_CHUNK_SHIFT];
		dissimilarity = segments ^ sc_segments;

		if (round_up(sc_level, ASSOC_ARRAY_KEY_CHUNK_SIZE) > shortcut->skip_to_level) {
			/* Trim segments that are beyond the shortcut */
			int shift = shortcut->skip_to_level & ASSOC_ARRAY_KEY_CHUNK_MASK;
			dissimilarity &= ~(ULONG_MAX << shift);
			next_sc_level = shortcut->skip_to_level;
		} else {
			next_sc_level = sc_level + ASSOC_ARRAY_KEY_CHUNK_SIZE;
			next_sc_level = round_down(next_sc_level, ASSOC_ARRAY_KEY_CHUNK_SIZE);
		}

		if (dissimilarity != 0) {
			/* This shortcut points elsewhere */
			result->wrong_shortcut.shortcut = shortcut;
			result->wrong_shortcut.level = level;
			result->wrong_shortcut.sc_level = sc_level;
			result->wrong_shortcut.sc_segments = sc_segments;
			result->wrong_shortcut.dissimilarity = dissimilarity;
			return assoc_array_walk_found_wrong_shortcut;
		}

		sc_level = next_sc_level;
	} while (sc_level < shortcut->skip_to_level);

	/* The shortcut matches the leaf's index to this point. */
	cursor = READ_ONCE(shortcut->next_yesde); /* Address dependency. */
	if (((level ^ sc_level) & ~ASSOC_ARRAY_KEY_CHUNK_MASK) != 0) {
		level = sc_level;
		goto jumped;
	} else {
		level = sc_level;
		goto consider_yesde;
	}
}

/**
 * assoc_array_find - Find an object by index key
 * @array: The associative array to search.
 * @ops: The operations to use.
 * @index_key: The key to the object.
 *
 * Find an object in an associative array by walking through the internal tree
 * to the yesde that should contain the object and then searching the leaves
 * there.  NULL is returned if the requested object was yest found in the array.
 *
 * The caller must hold the RCU read lock or better.
 */
void *assoc_array_find(const struct assoc_array *array,
		       const struct assoc_array_ops *ops,
		       const void *index_key)
{
	struct assoc_array_walk_result result;
	const struct assoc_array_yesde *yesde;
	const struct assoc_array_ptr *ptr;
	const void *leaf;
	int slot;

	if (assoc_array_walk(array, ops, index_key, &result) !=
	    assoc_array_walk_found_terminal_yesde)
		return NULL;

	yesde = result.terminal_yesde.yesde;

	/* If the target key is available to us, it's has to be pointed to by
	 * the terminal yesde.
	 */
	for (slot = 0; slot < ASSOC_ARRAY_FAN_OUT; slot++) {
		ptr = READ_ONCE(yesde->slots[slot]); /* Address dependency. */
		if (ptr && assoc_array_ptr_is_leaf(ptr)) {
			/* We need a barrier between the read of the pointer
			 * and dereferencing the pointer - but only if we are
			 * actually going to dereference it.
			 */
			leaf = assoc_array_ptr_to_leaf(ptr);
			if (ops->compare_object(leaf, index_key))
				return (void *)leaf;
		}
	}

	return NULL;
}

/*
 * Destructively iterate over an associative array.  The caller must prevent
 * other simultaneous accesses.
 */
static void assoc_array_destroy_subtree(struct assoc_array_ptr *root,
					const struct assoc_array_ops *ops)
{
	struct assoc_array_shortcut *shortcut;
	struct assoc_array_yesde *yesde;
	struct assoc_array_ptr *cursor, *parent = NULL;
	int slot = -1;

	pr_devel("-->%s()\n", __func__);

	cursor = root;
	if (!cursor) {
		pr_devel("empty\n");
		return;
	}

move_to_meta:
	if (assoc_array_ptr_is_shortcut(cursor)) {
		/* Descend through a shortcut */
		pr_devel("[%d] shortcut\n", slot);
		BUG_ON(!assoc_array_ptr_is_shortcut(cursor));
		shortcut = assoc_array_ptr_to_shortcut(cursor);
		BUG_ON(shortcut->back_pointer != parent);
		BUG_ON(slot != -1 && shortcut->parent_slot != slot);
		parent = cursor;
		cursor = shortcut->next_yesde;
		slot = -1;
		BUG_ON(!assoc_array_ptr_is_yesde(cursor));
	}

	pr_devel("[%d] yesde\n", slot);
	yesde = assoc_array_ptr_to_yesde(cursor);
	BUG_ON(yesde->back_pointer != parent);
	BUG_ON(slot != -1 && yesde->parent_slot != slot);
	slot = 0;

continue_yesde:
	pr_devel("Node %p [back=%p]\n", yesde, yesde->back_pointer);
	for (; slot < ASSOC_ARRAY_FAN_OUT; slot++) {
		struct assoc_array_ptr *ptr = yesde->slots[slot];
		if (!ptr)
			continue;
		if (assoc_array_ptr_is_meta(ptr)) {
			parent = cursor;
			cursor = ptr;
			goto move_to_meta;
		}

		if (ops) {
			pr_devel("[%d] free leaf\n", slot);
			ops->free_object(assoc_array_ptr_to_leaf(ptr));
		}
	}

	parent = yesde->back_pointer;
	slot = yesde->parent_slot;
	pr_devel("free yesde\n");
	kfree(yesde);
	if (!parent)
		return; /* Done */

	/* Move back up to the parent (may need to free a shortcut on
	 * the way up) */
	if (assoc_array_ptr_is_shortcut(parent)) {
		shortcut = assoc_array_ptr_to_shortcut(parent);
		BUG_ON(shortcut->next_yesde != cursor);
		cursor = parent;
		parent = shortcut->back_pointer;
		slot = shortcut->parent_slot;
		pr_devel("free shortcut\n");
		kfree(shortcut);
		if (!parent)
			return;

		BUG_ON(!assoc_array_ptr_is_yesde(parent));
	}

	/* Ascend to next slot in parent yesde */
	pr_devel("ascend to %p[%d]\n", parent, slot);
	cursor = parent;
	yesde = assoc_array_ptr_to_yesde(cursor);
	slot++;
	goto continue_yesde;
}

/**
 * assoc_array_destroy - Destroy an associative array
 * @array: The array to destroy.
 * @ops: The operations to use.
 *
 * Discard all metadata and free all objects in an associative array.  The
 * array will be empty and ready to use again upon completion.  This function
 * canyest fail.
 *
 * The caller must prevent all other accesses whilst this takes place as yes
 * attempt is made to adjust pointers gracefully to permit RCU readlock-holding
 * accesses to continue.  On the other hand, yes memory allocation is required.
 */
void assoc_array_destroy(struct assoc_array *array,
			 const struct assoc_array_ops *ops)
{
	assoc_array_destroy_subtree(array->root, ops);
	array->root = NULL;
}

/*
 * Handle insertion into an empty tree.
 */
static bool assoc_array_insert_in_empty_tree(struct assoc_array_edit *edit)
{
	struct assoc_array_yesde *new_n0;

	pr_devel("-->%s()\n", __func__);

	new_n0 = kzalloc(sizeof(struct assoc_array_yesde), GFP_KERNEL);
	if (!new_n0)
		return false;

	edit->new_meta[0] = assoc_array_yesde_to_ptr(new_n0);
	edit->leaf_p = &new_n0->slots[0];
	edit->adjust_count_on = new_n0;
	edit->set[0].ptr = &edit->array->root;
	edit->set[0].to = assoc_array_yesde_to_ptr(new_n0);

	pr_devel("<--%s() = ok [yes root]\n", __func__);
	return true;
}

/*
 * Handle insertion into a terminal yesde.
 */
static bool assoc_array_insert_into_terminal_yesde(struct assoc_array_edit *edit,
						  const struct assoc_array_ops *ops,
						  const void *index_key,
						  struct assoc_array_walk_result *result)
{
	struct assoc_array_shortcut *shortcut, *new_s0;
	struct assoc_array_yesde *yesde, *new_n0, *new_n1, *side;
	struct assoc_array_ptr *ptr;
	unsigned long dissimilarity, base_seg, blank;
	size_t keylen;
	bool have_meta;
	int level, diff;
	int slot, next_slot, free_slot, i, j;

	yesde	= result->terminal_yesde.yesde;
	level	= result->terminal_yesde.level;
	edit->segment_cache[ASSOC_ARRAY_FAN_OUT] = result->terminal_yesde.slot;

	pr_devel("-->%s()\n", __func__);

	/* We arrived at a yesde which doesn't have an onward yesde or shortcut
	 * pointer that we have to follow.  This means that (a) the leaf we
	 * want must go here (either by insertion or replacement) or (b) we
	 * need to split this yesde and insert in one of the fragments.
	 */
	free_slot = -1;

	/* Firstly, we have to check the leaves in this yesde to see if there's
	 * a matching one we should replace in place.
	 */
	for (i = 0; i < ASSOC_ARRAY_FAN_OUT; i++) {
		ptr = yesde->slots[i];
		if (!ptr) {
			free_slot = i;
			continue;
		}
		if (assoc_array_ptr_is_leaf(ptr) &&
		    ops->compare_object(assoc_array_ptr_to_leaf(ptr),
					index_key)) {
			pr_devel("replace in slot %d\n", i);
			edit->leaf_p = &yesde->slots[i];
			edit->dead_leaf = yesde->slots[i];
			pr_devel("<--%s() = ok [replace]\n", __func__);
			return true;
		}
	}

	/* If there is a free slot in this yesde then we can just insert the
	 * leaf here.
	 */
	if (free_slot >= 0) {
		pr_devel("insert in free slot %d\n", free_slot);
		edit->leaf_p = &yesde->slots[free_slot];
		edit->adjust_count_on = yesde;
		pr_devel("<--%s() = ok [insert]\n", __func__);
		return true;
	}

	/* The yesde has yes spare slots - so we're either going to have to split
	 * it or insert ayesther yesde before it.
	 *
	 * Whatever, we're going to need at least two new yesdes - so allocate
	 * those yesw.  We may also need a new shortcut, but we deal with that
	 * when we need it.
	 */
	new_n0 = kzalloc(sizeof(struct assoc_array_yesde), GFP_KERNEL);
	if (!new_n0)
		return false;
	edit->new_meta[0] = assoc_array_yesde_to_ptr(new_n0);
	new_n1 = kzalloc(sizeof(struct assoc_array_yesde), GFP_KERNEL);
	if (!new_n1)
		return false;
	edit->new_meta[1] = assoc_array_yesde_to_ptr(new_n1);

	/* We need to find out how similar the leaves are. */
	pr_devel("yes spare slots\n");
	have_meta = false;
	for (i = 0; i < ASSOC_ARRAY_FAN_OUT; i++) {
		ptr = yesde->slots[i];
		if (assoc_array_ptr_is_meta(ptr)) {
			edit->segment_cache[i] = 0xff;
			have_meta = true;
			continue;
		}
		base_seg = ops->get_object_key_chunk(
			assoc_array_ptr_to_leaf(ptr), level);
		base_seg >>= level & ASSOC_ARRAY_KEY_CHUNK_MASK;
		edit->segment_cache[i] = base_seg & ASSOC_ARRAY_FAN_MASK;
	}

	if (have_meta) {
		pr_devel("have meta\n");
		goto split_yesde;
	}

	/* The yesde contains only leaves */
	dissimilarity = 0;
	base_seg = edit->segment_cache[0];
	for (i = 1; i < ASSOC_ARRAY_FAN_OUT; i++)
		dissimilarity |= edit->segment_cache[i] ^ base_seg;

	pr_devel("only leaves; dissimilarity=%lx\n", dissimilarity);

	if ((dissimilarity & ASSOC_ARRAY_FAN_MASK) == 0) {
		/* The old leaves all cluster in the same slot.  We will need
		 * to insert a shortcut if the new yesde wants to cluster with them.
		 */
		if ((edit->segment_cache[ASSOC_ARRAY_FAN_OUT] ^ base_seg) == 0)
			goto all_leaves_cluster_together;

		/* Otherwise all the old leaves cluster in the same slot, but
		 * the new leaf wants to go into a different slot - so we
		 * create a new yesde (n0) to hold the new leaf and a pointer to
		 * a new yesde (n1) holding all the old leaves.
		 *
		 * This can be done by falling through to the yesde splitting
		 * path.
		 */
		pr_devel("present leaves cluster but yest new leaf\n");
	}

split_yesde:
	pr_devel("split yesde\n");

	/* We need to split the current yesde.  The yesde must contain anything
	 * from a single leaf (in the one leaf case, this leaf will cluster
	 * with the new leaf) and the rest meta-pointers, to all leaves, some
	 * of which may cluster.
	 *
	 * It won't contain the case in which all the current leaves plus the
	 * new leaves want to cluster in the same slot.
	 *
	 * We need to expel at least two leaves out of a set consisting of the
	 * leaves in the yesde and the new leaf.  The current meta pointers can
	 * just be copied as they shouldn't cluster with any of the leaves.
	 *
	 * We need a new yesde (n0) to replace the current one and a new yesde to
	 * take the expelled yesdes (n1).
	 */
	edit->set[0].to = assoc_array_yesde_to_ptr(new_n0);
	new_n0->back_pointer = yesde->back_pointer;
	new_n0->parent_slot = yesde->parent_slot;
	new_n1->back_pointer = assoc_array_yesde_to_ptr(new_n0);
	new_n1->parent_slot = -1; /* Need to calculate this */

do_split_yesde:
	pr_devel("do_split_yesde\n");

	new_n0->nr_leaves_on_branch = yesde->nr_leaves_on_branch;
	new_n1->nr_leaves_on_branch = 0;

	/* Begin by finding two matching leaves.  There have to be at least two
	 * that match - even if there are meta pointers - because any leaf that
	 * would match a slot with a meta pointer in it must be somewhere
	 * behind that meta pointer and canyest be here.  Further, given N
	 * remaining leaf slots, we yesw have N+1 leaves to go in them.
	 */
	for (i = 0; i < ASSOC_ARRAY_FAN_OUT; i++) {
		slot = edit->segment_cache[i];
		if (slot != 0xff)
			for (j = i + 1; j < ASSOC_ARRAY_FAN_OUT + 1; j++)
				if (edit->segment_cache[j] == slot)
					goto found_slot_for_multiple_occupancy;
	}
found_slot_for_multiple_occupancy:
	pr_devel("same slot: %x %x [%02x]\n", i, j, slot);
	BUG_ON(i >= ASSOC_ARRAY_FAN_OUT);
	BUG_ON(j >= ASSOC_ARRAY_FAN_OUT + 1);
	BUG_ON(slot >= ASSOC_ARRAY_FAN_OUT);

	new_n1->parent_slot = slot;

	/* Metadata pointers canyest change slot */
	for (i = 0; i < ASSOC_ARRAY_FAN_OUT; i++)
		if (assoc_array_ptr_is_meta(yesde->slots[i]))
			new_n0->slots[i] = yesde->slots[i];
		else
			new_n0->slots[i] = NULL;
	BUG_ON(new_n0->slots[slot] != NULL);
	new_n0->slots[slot] = assoc_array_yesde_to_ptr(new_n1);

	/* Filter the leaf pointers between the new yesdes */
	free_slot = -1;
	next_slot = 0;
	for (i = 0; i < ASSOC_ARRAY_FAN_OUT; i++) {
		if (assoc_array_ptr_is_meta(yesde->slots[i]))
			continue;
		if (edit->segment_cache[i] == slot) {
			new_n1->slots[next_slot++] = yesde->slots[i];
			new_n1->nr_leaves_on_branch++;
		} else {
			do {
				free_slot++;
			} while (new_n0->slots[free_slot] != NULL);
			new_n0->slots[free_slot] = yesde->slots[i];
		}
	}

	pr_devel("filtered: f=%x n=%x\n", free_slot, next_slot);

	if (edit->segment_cache[ASSOC_ARRAY_FAN_OUT] != slot) {
		do {
			free_slot++;
		} while (new_n0->slots[free_slot] != NULL);
		edit->leaf_p = &new_n0->slots[free_slot];
		edit->adjust_count_on = new_n0;
	} else {
		edit->leaf_p = &new_n1->slots[next_slot++];
		edit->adjust_count_on = new_n1;
	}

	BUG_ON(next_slot <= 1);

	edit->set_backpointers_to = assoc_array_yesde_to_ptr(new_n0);
	for (i = 0; i < ASSOC_ARRAY_FAN_OUT; i++) {
		if (edit->segment_cache[i] == 0xff) {
			ptr = yesde->slots[i];
			BUG_ON(assoc_array_ptr_is_leaf(ptr));
			if (assoc_array_ptr_is_yesde(ptr)) {
				side = assoc_array_ptr_to_yesde(ptr);
				edit->set_backpointers[i] = &side->back_pointer;
			} else {
				shortcut = assoc_array_ptr_to_shortcut(ptr);
				edit->set_backpointers[i] = &shortcut->back_pointer;
			}
		}
	}

	ptr = yesde->back_pointer;
	if (!ptr)
		edit->set[0].ptr = &edit->array->root;
	else if (assoc_array_ptr_is_yesde(ptr))
		edit->set[0].ptr = &assoc_array_ptr_to_yesde(ptr)->slots[yesde->parent_slot];
	else
		edit->set[0].ptr = &assoc_array_ptr_to_shortcut(ptr)->next_yesde;
	edit->excised_meta[0] = assoc_array_yesde_to_ptr(yesde);
	pr_devel("<--%s() = ok [split yesde]\n", __func__);
	return true;

all_leaves_cluster_together:
	/* All the leaves, new and old, want to cluster together in this yesde
	 * in the same slot, so we have to replace this yesde with a shortcut to
	 * skip over the identical parts of the key and then place a pair of
	 * yesdes, one inside the other, at the end of the shortcut and
	 * distribute the keys between them.
	 *
	 * Firstly we need to work out where the leaves start diverging as a
	 * bit position into their keys so that we kyesw how big the shortcut
	 * needs to be.
	 *
	 * We only need to make a single pass of N of the N+1 leaves because if
	 * any keys differ between themselves at bit X then at least one of
	 * them must also differ with the base key at bit X or before.
	 */
	pr_devel("all leaves cluster together\n");
	diff = INT_MAX;
	for (i = 0; i < ASSOC_ARRAY_FAN_OUT; i++) {
		int x = ops->diff_objects(assoc_array_ptr_to_leaf(yesde->slots[i]),
					  index_key);
		if (x < diff) {
			BUG_ON(x < 0);
			diff = x;
		}
	}
	BUG_ON(diff == INT_MAX);
	BUG_ON(diff < level + ASSOC_ARRAY_LEVEL_STEP);

	keylen = round_up(diff, ASSOC_ARRAY_KEY_CHUNK_SIZE);
	keylen >>= ASSOC_ARRAY_KEY_CHUNK_SHIFT;

	new_s0 = kzalloc(sizeof(struct assoc_array_shortcut) +
			 keylen * sizeof(unsigned long), GFP_KERNEL);
	if (!new_s0)
		return false;
	edit->new_meta[2] = assoc_array_shortcut_to_ptr(new_s0);

	edit->set[0].to = assoc_array_shortcut_to_ptr(new_s0);
	new_s0->back_pointer = yesde->back_pointer;
	new_s0->parent_slot = yesde->parent_slot;
	new_s0->next_yesde = assoc_array_yesde_to_ptr(new_n0);
	new_n0->back_pointer = assoc_array_shortcut_to_ptr(new_s0);
	new_n0->parent_slot = 0;
	new_n1->back_pointer = assoc_array_yesde_to_ptr(new_n0);
	new_n1->parent_slot = -1; /* Need to calculate this */

	new_s0->skip_to_level = level = diff & ~ASSOC_ARRAY_LEVEL_STEP_MASK;
	pr_devel("skip_to_level = %d [diff %d]\n", level, diff);
	BUG_ON(level <= 0);

	for (i = 0; i < keylen; i++)
		new_s0->index_key[i] =
			ops->get_key_chunk(index_key, i * ASSOC_ARRAY_KEY_CHUNK_SIZE);

	if (level & ASSOC_ARRAY_KEY_CHUNK_MASK) {
		blank = ULONG_MAX << (level & ASSOC_ARRAY_KEY_CHUNK_MASK);
		pr_devel("blank off [%zu] %d: %lx\n", keylen - 1, level, blank);
		new_s0->index_key[keylen - 1] &= ~blank;
	}

	/* This yesw reduces to a yesde splitting exercise for which we'll need
	 * to regenerate the disparity table.
	 */
	for (i = 0; i < ASSOC_ARRAY_FAN_OUT; i++) {
		ptr = yesde->slots[i];
		base_seg = ops->get_object_key_chunk(assoc_array_ptr_to_leaf(ptr),
						     level);
		base_seg >>= level & ASSOC_ARRAY_KEY_CHUNK_MASK;
		edit->segment_cache[i] = base_seg & ASSOC_ARRAY_FAN_MASK;
	}

	base_seg = ops->get_key_chunk(index_key, level);
	base_seg >>= level & ASSOC_ARRAY_KEY_CHUNK_MASK;
	edit->segment_cache[ASSOC_ARRAY_FAN_OUT] = base_seg & ASSOC_ARRAY_FAN_MASK;
	goto do_split_yesde;
}

/*
 * Handle insertion into the middle of a shortcut.
 */
static bool assoc_array_insert_mid_shortcut(struct assoc_array_edit *edit,
					    const struct assoc_array_ops *ops,
					    struct assoc_array_walk_result *result)
{
	struct assoc_array_shortcut *shortcut, *new_s0, *new_s1;
	struct assoc_array_yesde *yesde, *new_n0, *side;
	unsigned long sc_segments, dissimilarity, blank;
	size_t keylen;
	int level, sc_level, diff;
	int sc_slot;

	shortcut	= result->wrong_shortcut.shortcut;
	level		= result->wrong_shortcut.level;
	sc_level	= result->wrong_shortcut.sc_level;
	sc_segments	= result->wrong_shortcut.sc_segments;
	dissimilarity	= result->wrong_shortcut.dissimilarity;

	pr_devel("-->%s(ix=%d dis=%lx scix=%d)\n",
		 __func__, level, dissimilarity, sc_level);

	/* We need to split a shortcut and insert a yesde between the two
	 * pieces.  Zero-length pieces will be dispensed with entirely.
	 *
	 * First of all, we need to find out in which level the first
	 * difference was.
	 */
	diff = __ffs(dissimilarity);
	diff &= ~ASSOC_ARRAY_LEVEL_STEP_MASK;
	diff += sc_level & ~ASSOC_ARRAY_KEY_CHUNK_MASK;
	pr_devel("diff=%d\n", diff);

	if (!shortcut->back_pointer) {
		edit->set[0].ptr = &edit->array->root;
	} else if (assoc_array_ptr_is_yesde(shortcut->back_pointer)) {
		yesde = assoc_array_ptr_to_yesde(shortcut->back_pointer);
		edit->set[0].ptr = &yesde->slots[shortcut->parent_slot];
	} else {
		BUG();
	}

	edit->excised_meta[0] = assoc_array_shortcut_to_ptr(shortcut);

	/* Create a new yesde yesw since we're going to need it anyway */
	new_n0 = kzalloc(sizeof(struct assoc_array_yesde), GFP_KERNEL);
	if (!new_n0)
		return false;
	edit->new_meta[0] = assoc_array_yesde_to_ptr(new_n0);
	edit->adjust_count_on = new_n0;

	/* Insert a new shortcut before the new yesde if this segment isn't of
	 * zero length - otherwise we just connect the new yesde directly to the
	 * parent.
	 */
	level += ASSOC_ARRAY_LEVEL_STEP;
	if (diff > level) {
		pr_devel("pre-shortcut %d...%d\n", level, diff);
		keylen = round_up(diff, ASSOC_ARRAY_KEY_CHUNK_SIZE);
		keylen >>= ASSOC_ARRAY_KEY_CHUNK_SHIFT;

		new_s0 = kzalloc(sizeof(struct assoc_array_shortcut) +
				 keylen * sizeof(unsigned long), GFP_KERNEL);
		if (!new_s0)
			return false;
		edit->new_meta[1] = assoc_array_shortcut_to_ptr(new_s0);
		edit->set[0].to = assoc_array_shortcut_to_ptr(new_s0);
		new_s0->back_pointer = shortcut->back_pointer;
		new_s0->parent_slot = shortcut->parent_slot;
		new_s0->next_yesde = assoc_array_yesde_to_ptr(new_n0);
		new_s0->skip_to_level = diff;

		new_n0->back_pointer = assoc_array_shortcut_to_ptr(new_s0);
		new_n0->parent_slot = 0;

		memcpy(new_s0->index_key, shortcut->index_key,
		       keylen * sizeof(unsigned long));

		blank = ULONG_MAX << (diff & ASSOC_ARRAY_KEY_CHUNK_MASK);
		pr_devel("blank off [%zu] %d: %lx\n", keylen - 1, diff, blank);
		new_s0->index_key[keylen - 1] &= ~blank;
	} else {
		pr_devel("yes pre-shortcut\n");
		edit->set[0].to = assoc_array_yesde_to_ptr(new_n0);
		new_n0->back_pointer = shortcut->back_pointer;
		new_n0->parent_slot = shortcut->parent_slot;
	}

	side = assoc_array_ptr_to_yesde(shortcut->next_yesde);
	new_n0->nr_leaves_on_branch = side->nr_leaves_on_branch;

	/* We need to kyesw which slot in the new yesde is going to take a
	 * metadata pointer.
	 */
	sc_slot = sc_segments >> (diff & ASSOC_ARRAY_KEY_CHUNK_MASK);
	sc_slot &= ASSOC_ARRAY_FAN_MASK;

	pr_devel("new slot %lx >> %d -> %d\n",
		 sc_segments, diff & ASSOC_ARRAY_KEY_CHUNK_MASK, sc_slot);

	/* Determine whether we need to follow the new yesde with a replacement
	 * for the current shortcut.  We could in theory reuse the current
	 * shortcut if its parent slot number doesn't change - but that's a
	 * 1-in-16 chance so yest worth expending the code upon.
	 */
	level = diff + ASSOC_ARRAY_LEVEL_STEP;
	if (level < shortcut->skip_to_level) {
		pr_devel("post-shortcut %d...%d\n", level, shortcut->skip_to_level);
		keylen = round_up(shortcut->skip_to_level, ASSOC_ARRAY_KEY_CHUNK_SIZE);
		keylen >>= ASSOC_ARRAY_KEY_CHUNK_SHIFT;

		new_s1 = kzalloc(sizeof(struct assoc_array_shortcut) +
				 keylen * sizeof(unsigned long), GFP_KERNEL);
		if (!new_s1)
			return false;
		edit->new_meta[2] = assoc_array_shortcut_to_ptr(new_s1);

		new_s1->back_pointer = assoc_array_yesde_to_ptr(new_n0);
		new_s1->parent_slot = sc_slot;
		new_s1->next_yesde = shortcut->next_yesde;
		new_s1->skip_to_level = shortcut->skip_to_level;

		new_n0->slots[sc_slot] = assoc_array_shortcut_to_ptr(new_s1);

		memcpy(new_s1->index_key, shortcut->index_key,
		       keylen * sizeof(unsigned long));

		edit->set[1].ptr = &side->back_pointer;
		edit->set[1].to = assoc_array_shortcut_to_ptr(new_s1);
	} else {
		pr_devel("yes post-shortcut\n");

		/* We don't have to replace the pointed-to yesde as long as we
		 * use memory barriers to make sure the parent slot number is
		 * changed before the back pointer (the parent slot number is
		 * irrelevant to the old parent shortcut).
		 */
		new_n0->slots[sc_slot] = shortcut->next_yesde;
		edit->set_parent_slot[0].p = &side->parent_slot;
		edit->set_parent_slot[0].to = sc_slot;
		edit->set[1].ptr = &side->back_pointer;
		edit->set[1].to = assoc_array_yesde_to_ptr(new_n0);
	}

	/* Install the new leaf in a spare slot in the new yesde. */
	if (sc_slot == 0)
		edit->leaf_p = &new_n0->slots[1];
	else
		edit->leaf_p = &new_n0->slots[0];

	pr_devel("<--%s() = ok [split shortcut]\n", __func__);
	return edit;
}

/**
 * assoc_array_insert - Script insertion of an object into an associative array
 * @array: The array to insert into.
 * @ops: The operations to use.
 * @index_key: The key to insert at.
 * @object: The object to insert.
 *
 * Precalculate and preallocate a script for the insertion or replacement of an
 * object in an associative array.  This results in an edit script that can
 * either be applied or cancelled.
 *
 * The function returns a pointer to an edit script or -ENOMEM.
 *
 * The caller should lock against other modifications and must continue to hold
 * the lock until assoc_array_apply_edit() has been called.
 *
 * Accesses to the tree may take place concurrently with this function,
 * provided they hold the RCU read lock.
 */
struct assoc_array_edit *assoc_array_insert(struct assoc_array *array,
					    const struct assoc_array_ops *ops,
					    const void *index_key,
					    void *object)
{
	struct assoc_array_walk_result result;
	struct assoc_array_edit *edit;

	pr_devel("-->%s()\n", __func__);

	/* The leaf pointer we're given must yest have the bottom bit set as we
	 * use those for type-marking the pointer.  NULL pointers are also yest
	 * allowed as they indicate an empty slot but we have to allow them
	 * here as they can be updated later.
	 */
	BUG_ON(assoc_array_ptr_is_meta(object));

	edit = kzalloc(sizeof(struct assoc_array_edit), GFP_KERNEL);
	if (!edit)
		return ERR_PTR(-ENOMEM);
	edit->array = array;
	edit->ops = ops;
	edit->leaf = assoc_array_leaf_to_ptr(object);
	edit->adjust_count_by = 1;

	switch (assoc_array_walk(array, ops, index_key, &result)) {
	case assoc_array_walk_tree_empty:
		/* Allocate a root yesde if there isn't one yet */
		if (!assoc_array_insert_in_empty_tree(edit))
			goto eyesmem;
		return edit;

	case assoc_array_walk_found_terminal_yesde:
		/* We found a yesde that doesn't have a yesde/shortcut pointer in
		 * the slot corresponding to the index key that we have to
		 * follow.
		 */
		if (!assoc_array_insert_into_terminal_yesde(edit, ops, index_key,
							   &result))
			goto eyesmem;
		return edit;

	case assoc_array_walk_found_wrong_shortcut:
		/* We found a shortcut that didn't match our key in a slot we
		 * needed to follow.
		 */
		if (!assoc_array_insert_mid_shortcut(edit, ops, &result))
			goto eyesmem;
		return edit;
	}

eyesmem:
	/* Clean up after an out of memory error */
	pr_devel("eyesmem\n");
	assoc_array_cancel_edit(edit);
	return ERR_PTR(-ENOMEM);
}

/**
 * assoc_array_insert_set_object - Set the new object pointer in an edit script
 * @edit: The edit script to modify.
 * @object: The object pointer to set.
 *
 * Change the object to be inserted in an edit script.  The object pointed to
 * by the old object is yest freed.  This must be done prior to applying the
 * script.
 */
void assoc_array_insert_set_object(struct assoc_array_edit *edit, void *object)
{
	BUG_ON(!object);
	edit->leaf = assoc_array_leaf_to_ptr(object);
}

struct assoc_array_delete_collapse_context {
	struct assoc_array_yesde	*yesde;
	const void		*skip_leaf;
	int			slot;
};

/*
 * Subtree collapse to yesde iterator.
 */
static int assoc_array_delete_collapse_iterator(const void *leaf,
						void *iterator_data)
{
	struct assoc_array_delete_collapse_context *collapse = iterator_data;

	if (leaf == collapse->skip_leaf)
		return 0;

	BUG_ON(collapse->slot >= ASSOC_ARRAY_FAN_OUT);

	collapse->yesde->slots[collapse->slot++] = assoc_array_leaf_to_ptr(leaf);
	return 0;
}

/**
 * assoc_array_delete - Script deletion of an object from an associative array
 * @array: The array to search.
 * @ops: The operations to use.
 * @index_key: The key to the object.
 *
 * Precalculate and preallocate a script for the deletion of an object from an
 * associative array.  This results in an edit script that can either be
 * applied or cancelled.
 *
 * The function returns a pointer to an edit script if the object was found,
 * NULL if the object was yest found or -ENOMEM.
 *
 * The caller should lock against other modifications and must continue to hold
 * the lock until assoc_array_apply_edit() has been called.
 *
 * Accesses to the tree may take place concurrently with this function,
 * provided they hold the RCU read lock.
 */
struct assoc_array_edit *assoc_array_delete(struct assoc_array *array,
					    const struct assoc_array_ops *ops,
					    const void *index_key)
{
	struct assoc_array_delete_collapse_context collapse;
	struct assoc_array_walk_result result;
	struct assoc_array_yesde *yesde, *new_n0;
	struct assoc_array_edit *edit;
	struct assoc_array_ptr *ptr;
	bool has_meta;
	int slot, i;

	pr_devel("-->%s()\n", __func__);

	edit = kzalloc(sizeof(struct assoc_array_edit), GFP_KERNEL);
	if (!edit)
		return ERR_PTR(-ENOMEM);
	edit->array = array;
	edit->ops = ops;
	edit->adjust_count_by = -1;

	switch (assoc_array_walk(array, ops, index_key, &result)) {
	case assoc_array_walk_found_terminal_yesde:
		/* We found a yesde that should contain the leaf we've been
		 * asked to remove - *if* it's in the tree.
		 */
		pr_devel("terminal_yesde\n");
		yesde = result.terminal_yesde.yesde;

		for (slot = 0; slot < ASSOC_ARRAY_FAN_OUT; slot++) {
			ptr = yesde->slots[slot];
			if (ptr &&
			    assoc_array_ptr_is_leaf(ptr) &&
			    ops->compare_object(assoc_array_ptr_to_leaf(ptr),
						index_key))
				goto found_leaf;
		}
		/* fall through */
	case assoc_array_walk_tree_empty:
	case assoc_array_walk_found_wrong_shortcut:
	default:
		assoc_array_cancel_edit(edit);
		pr_devel("yest found\n");
		return NULL;
	}

found_leaf:
	BUG_ON(array->nr_leaves_on_tree <= 0);

	/* In the simplest form of deletion we just clear the slot and release
	 * the leaf after a suitable interval.
	 */
	edit->dead_leaf = yesde->slots[slot];
	edit->set[0].ptr = &yesde->slots[slot];
	edit->set[0].to = NULL;
	edit->adjust_count_on = yesde;

	/* If that concludes erasure of the last leaf, then delete the entire
	 * internal array.
	 */
	if (array->nr_leaves_on_tree == 1) {
		edit->set[1].ptr = &array->root;
		edit->set[1].to = NULL;
		edit->adjust_count_on = NULL;
		edit->excised_subtree = array->root;
		pr_devel("all gone\n");
		return edit;
	}

	/* However, we'd also like to clear up some metadata blocks if we
	 * possibly can.
	 *
	 * We go for a simple algorithm of: if this yesde has FAN_OUT or fewer
	 * leaves in it, then attempt to collapse it - and attempt to
	 * recursively collapse up the tree.
	 *
	 * We could also try and collapse in partially filled subtrees to take
	 * up space in this yesde.
	 */
	if (yesde->nr_leaves_on_branch <= ASSOC_ARRAY_FAN_OUT + 1) {
		struct assoc_array_yesde *parent, *grandparent;
		struct assoc_array_ptr *ptr;

		/* First of all, we need to kyesw if this yesde has metadata so
		 * that we don't try collapsing if all the leaves are already
		 * here.
		 */
		has_meta = false;
		for (i = 0; i < ASSOC_ARRAY_FAN_OUT; i++) {
			ptr = yesde->slots[i];
			if (assoc_array_ptr_is_meta(ptr)) {
				has_meta = true;
				break;
			}
		}

		pr_devel("leaves: %ld [m=%d]\n",
			 yesde->nr_leaves_on_branch - 1, has_meta);

		/* Look further up the tree to see if we can collapse this yesde
		 * into a more proximal yesde too.
		 */
		parent = yesde;
	collapse_up:
		pr_devel("collapse subtree: %ld\n", parent->nr_leaves_on_branch);

		ptr = parent->back_pointer;
		if (!ptr)
			goto do_collapse;
		if (assoc_array_ptr_is_shortcut(ptr)) {
			struct assoc_array_shortcut *s = assoc_array_ptr_to_shortcut(ptr);
			ptr = s->back_pointer;
			if (!ptr)
				goto do_collapse;
		}

		grandparent = assoc_array_ptr_to_yesde(ptr);
		if (grandparent->nr_leaves_on_branch <= ASSOC_ARRAY_FAN_OUT + 1) {
			parent = grandparent;
			goto collapse_up;
		}

	do_collapse:
		/* There's yes point collapsing if the original yesde has yes meta
		 * pointers to discard and if we didn't merge into one of that
		 * yesde's ancestry.
		 */
		if (has_meta || parent != yesde) {
			yesde = parent;

			/* Create a new yesde to collapse into */
			new_n0 = kzalloc(sizeof(struct assoc_array_yesde), GFP_KERNEL);
			if (!new_n0)
				goto eyesmem;
			edit->new_meta[0] = assoc_array_yesde_to_ptr(new_n0);

			new_n0->back_pointer = yesde->back_pointer;
			new_n0->parent_slot = yesde->parent_slot;
			new_n0->nr_leaves_on_branch = yesde->nr_leaves_on_branch;
			edit->adjust_count_on = new_n0;

			collapse.yesde = new_n0;
			collapse.skip_leaf = assoc_array_ptr_to_leaf(edit->dead_leaf);
			collapse.slot = 0;
			assoc_array_subtree_iterate(assoc_array_yesde_to_ptr(yesde),
						    yesde->back_pointer,
						    assoc_array_delete_collapse_iterator,
						    &collapse);
			pr_devel("collapsed %d,%lu\n", collapse.slot, new_n0->nr_leaves_on_branch);
			BUG_ON(collapse.slot != new_n0->nr_leaves_on_branch - 1);

			if (!yesde->back_pointer) {
				edit->set[1].ptr = &array->root;
			} else if (assoc_array_ptr_is_leaf(yesde->back_pointer)) {
				BUG();
			} else if (assoc_array_ptr_is_yesde(yesde->back_pointer)) {
				struct assoc_array_yesde *p =
					assoc_array_ptr_to_yesde(yesde->back_pointer);
				edit->set[1].ptr = &p->slots[yesde->parent_slot];
			} else if (assoc_array_ptr_is_shortcut(yesde->back_pointer)) {
				struct assoc_array_shortcut *s =
					assoc_array_ptr_to_shortcut(yesde->back_pointer);
				edit->set[1].ptr = &s->next_yesde;
			}
			edit->set[1].to = assoc_array_yesde_to_ptr(new_n0);
			edit->excised_subtree = assoc_array_yesde_to_ptr(yesde);
		}
	}

	return edit;

eyesmem:
	/* Clean up after an out of memory error */
	pr_devel("eyesmem\n");
	assoc_array_cancel_edit(edit);
	return ERR_PTR(-ENOMEM);
}

/**
 * assoc_array_clear - Script deletion of all objects from an associative array
 * @array: The array to clear.
 * @ops: The operations to use.
 *
 * Precalculate and preallocate a script for the deletion of all the objects
 * from an associative array.  This results in an edit script that can either
 * be applied or cancelled.
 *
 * The function returns a pointer to an edit script if there are objects to be
 * deleted, NULL if there are yes objects in the array or -ENOMEM.
 *
 * The caller should lock against other modifications and must continue to hold
 * the lock until assoc_array_apply_edit() has been called.
 *
 * Accesses to the tree may take place concurrently with this function,
 * provided they hold the RCU read lock.
 */
struct assoc_array_edit *assoc_array_clear(struct assoc_array *array,
					   const struct assoc_array_ops *ops)
{
	struct assoc_array_edit *edit;

	pr_devel("-->%s()\n", __func__);

	if (!array->root)
		return NULL;

	edit = kzalloc(sizeof(struct assoc_array_edit), GFP_KERNEL);
	if (!edit)
		return ERR_PTR(-ENOMEM);
	edit->array = array;
	edit->ops = ops;
	edit->set[1].ptr = &array->root;
	edit->set[1].to = NULL;
	edit->excised_subtree = array->root;
	edit->ops_for_excised_subtree = ops;
	pr_devel("all gone\n");
	return edit;
}

/*
 * Handle the deferred destruction after an applied edit.
 */
static void assoc_array_rcu_cleanup(struct rcu_head *head)
{
	struct assoc_array_edit *edit =
		container_of(head, struct assoc_array_edit, rcu);
	int i;

	pr_devel("-->%s()\n", __func__);

	if (edit->dead_leaf)
		edit->ops->free_object(assoc_array_ptr_to_leaf(edit->dead_leaf));
	for (i = 0; i < ARRAY_SIZE(edit->excised_meta); i++)
		if (edit->excised_meta[i])
			kfree(assoc_array_ptr_to_yesde(edit->excised_meta[i]));

	if (edit->excised_subtree) {
		BUG_ON(assoc_array_ptr_is_leaf(edit->excised_subtree));
		if (assoc_array_ptr_is_yesde(edit->excised_subtree)) {
			struct assoc_array_yesde *n =
				assoc_array_ptr_to_yesde(edit->excised_subtree);
			n->back_pointer = NULL;
		} else {
			struct assoc_array_shortcut *s =
				assoc_array_ptr_to_shortcut(edit->excised_subtree);
			s->back_pointer = NULL;
		}
		assoc_array_destroy_subtree(edit->excised_subtree,
					    edit->ops_for_excised_subtree);
	}

	kfree(edit);
}

/**
 * assoc_array_apply_edit - Apply an edit script to an associative array
 * @edit: The script to apply.
 *
 * Apply an edit script to an associative array to effect an insertion,
 * deletion or clearance.  As the edit script includes preallocated memory,
 * this is guaranteed yest to fail.
 *
 * The edit script, dead objects and dead metadata will be scheduled for
 * destruction after an RCU grace period to permit those doing read-only
 * accesses on the array to continue to do so under the RCU read lock whilst
 * the edit is taking place.
 */
void assoc_array_apply_edit(struct assoc_array_edit *edit)
{
	struct assoc_array_shortcut *shortcut;
	struct assoc_array_yesde *yesde;
	struct assoc_array_ptr *ptr;
	int i;

	pr_devel("-->%s()\n", __func__);

	smp_wmb();
	if (edit->leaf_p)
		*edit->leaf_p = edit->leaf;

	smp_wmb();
	for (i = 0; i < ARRAY_SIZE(edit->set_parent_slot); i++)
		if (edit->set_parent_slot[i].p)
			*edit->set_parent_slot[i].p = edit->set_parent_slot[i].to;

	smp_wmb();
	for (i = 0; i < ARRAY_SIZE(edit->set_backpointers); i++)
		if (edit->set_backpointers[i])
			*edit->set_backpointers[i] = edit->set_backpointers_to;

	smp_wmb();
	for (i = 0; i < ARRAY_SIZE(edit->set); i++)
		if (edit->set[i].ptr)
			*edit->set[i].ptr = edit->set[i].to;

	if (edit->array->root == NULL) {
		edit->array->nr_leaves_on_tree = 0;
	} else if (edit->adjust_count_on) {
		yesde = edit->adjust_count_on;
		for (;;) {
			yesde->nr_leaves_on_branch += edit->adjust_count_by;

			ptr = yesde->back_pointer;
			if (!ptr)
				break;
			if (assoc_array_ptr_is_shortcut(ptr)) {
				shortcut = assoc_array_ptr_to_shortcut(ptr);
				ptr = shortcut->back_pointer;
				if (!ptr)
					break;
			}
			BUG_ON(!assoc_array_ptr_is_yesde(ptr));
			yesde = assoc_array_ptr_to_yesde(ptr);
		}

		edit->array->nr_leaves_on_tree += edit->adjust_count_by;
	}

	call_rcu(&edit->rcu, assoc_array_rcu_cleanup);
}

/**
 * assoc_array_cancel_edit - Discard an edit script.
 * @edit: The script to discard.
 *
 * Free an edit script and all the preallocated data it holds without making
 * any changes to the associative array it was intended for.
 *
 * NOTE!  In the case of an insertion script, this does _yest_ release the leaf
 * that was to be inserted.  That is left to the caller.
 */
void assoc_array_cancel_edit(struct assoc_array_edit *edit)
{
	struct assoc_array_ptr *ptr;
	int i;

	pr_devel("-->%s()\n", __func__);

	/* Clean up after an out of memory error */
	for (i = 0; i < ARRAY_SIZE(edit->new_meta); i++) {
		ptr = edit->new_meta[i];
		if (ptr) {
			if (assoc_array_ptr_is_yesde(ptr))
				kfree(assoc_array_ptr_to_yesde(ptr));
			else
				kfree(assoc_array_ptr_to_shortcut(ptr));
		}
	}
	kfree(edit);
}

/**
 * assoc_array_gc - Garbage collect an associative array.
 * @array: The array to clean.
 * @ops: The operations to use.
 * @iterator: A callback function to pass judgement on each object.
 * @iterator_data: Private data for the callback function.
 *
 * Collect garbage from an associative array and pack down the internal tree to
 * save memory.
 *
 * The iterator function is asked to pass judgement upon each object in the
 * array.  If it returns false, the object is discard and if it returns true,
 * the object is kept.  If it returns true, it must increment the object's
 * usage count (or whatever it needs to do to retain it) before returning.
 *
 * This function returns 0 if successful or -ENOMEM if out of memory.  In the
 * latter case, the array is yest changed.
 *
 * The caller should lock against other modifications and must continue to hold
 * the lock until assoc_array_apply_edit() has been called.
 *
 * Accesses to the tree may take place concurrently with this function,
 * provided they hold the RCU read lock.
 */
int assoc_array_gc(struct assoc_array *array,
		   const struct assoc_array_ops *ops,
		   bool (*iterator)(void *object, void *iterator_data),
		   void *iterator_data)
{
	struct assoc_array_shortcut *shortcut, *new_s;
	struct assoc_array_yesde *yesde, *new_n;
	struct assoc_array_edit *edit;
	struct assoc_array_ptr *cursor, *ptr;
	struct assoc_array_ptr *new_root, *new_parent, **new_ptr_pp;
	unsigned long nr_leaves_on_tree;
	int keylen, slot, nr_free, next_slot, i;

	pr_devel("-->%s()\n", __func__);

	if (!array->root)
		return 0;

	edit = kzalloc(sizeof(struct assoc_array_edit), GFP_KERNEL);
	if (!edit)
		return -ENOMEM;
	edit->array = array;
	edit->ops = ops;
	edit->ops_for_excised_subtree = ops;
	edit->set[0].ptr = &array->root;
	edit->excised_subtree = array->root;

	new_root = new_parent = NULL;
	new_ptr_pp = &new_root;
	cursor = array->root;

descend:
	/* If this point is a shortcut, then we need to duplicate it and
	 * advance the target cursor.
	 */
	if (assoc_array_ptr_is_shortcut(cursor)) {
		shortcut = assoc_array_ptr_to_shortcut(cursor);
		keylen = round_up(shortcut->skip_to_level, ASSOC_ARRAY_KEY_CHUNK_SIZE);
		keylen >>= ASSOC_ARRAY_KEY_CHUNK_SHIFT;
		new_s = kmalloc(sizeof(struct assoc_array_shortcut) +
				keylen * sizeof(unsigned long), GFP_KERNEL);
		if (!new_s)
			goto eyesmem;
		pr_devel("dup shortcut %p -> %p\n", shortcut, new_s);
		memcpy(new_s, shortcut, (sizeof(struct assoc_array_shortcut) +
					 keylen * sizeof(unsigned long)));
		new_s->back_pointer = new_parent;
		new_s->parent_slot = shortcut->parent_slot;
		*new_ptr_pp = new_parent = assoc_array_shortcut_to_ptr(new_s);
		new_ptr_pp = &new_s->next_yesde;
		cursor = shortcut->next_yesde;
	}

	/* Duplicate the yesde at this position */
	yesde = assoc_array_ptr_to_yesde(cursor);
	new_n = kzalloc(sizeof(struct assoc_array_yesde), GFP_KERNEL);
	if (!new_n)
		goto eyesmem;
	pr_devel("dup yesde %p -> %p\n", yesde, new_n);
	new_n->back_pointer = new_parent;
	new_n->parent_slot = yesde->parent_slot;
	*new_ptr_pp = new_parent = assoc_array_yesde_to_ptr(new_n);
	new_ptr_pp = NULL;
	slot = 0;

continue_yesde:
	/* Filter across any leaves and gc any subtrees */
	for (; slot < ASSOC_ARRAY_FAN_OUT; slot++) {
		ptr = yesde->slots[slot];
		if (!ptr)
			continue;

		if (assoc_array_ptr_is_leaf(ptr)) {
			if (iterator(assoc_array_ptr_to_leaf(ptr),
				     iterator_data))
				/* The iterator will have done any reference
				 * counting on the object for us.
				 */
				new_n->slots[slot] = ptr;
			continue;
		}

		new_ptr_pp = &new_n->slots[slot];
		cursor = ptr;
		goto descend;
	}

	pr_devel("-- compress yesde %p --\n", new_n);

	/* Count up the number of empty slots in this yesde and work out the
	 * subtree leaf count.
	 */
	new_n->nr_leaves_on_branch = 0;
	nr_free = 0;
	for (slot = 0; slot < ASSOC_ARRAY_FAN_OUT; slot++) {
		ptr = new_n->slots[slot];
		if (!ptr)
			nr_free++;
		else if (assoc_array_ptr_is_leaf(ptr))
			new_n->nr_leaves_on_branch++;
	}
	pr_devel("free=%d, leaves=%lu\n", nr_free, new_n->nr_leaves_on_branch);

	/* See what we can fold in */
	next_slot = 0;
	for (slot = 0; slot < ASSOC_ARRAY_FAN_OUT; slot++) {
		struct assoc_array_shortcut *s;
		struct assoc_array_yesde *child;

		ptr = new_n->slots[slot];
		if (!ptr || assoc_array_ptr_is_leaf(ptr))
			continue;

		s = NULL;
		if (assoc_array_ptr_is_shortcut(ptr)) {
			s = assoc_array_ptr_to_shortcut(ptr);
			ptr = s->next_yesde;
		}

		child = assoc_array_ptr_to_yesde(ptr);
		new_n->nr_leaves_on_branch += child->nr_leaves_on_branch;

		if (child->nr_leaves_on_branch <= nr_free + 1) {
			/* Fold the child yesde into this one */
			pr_devel("[%d] fold yesde %lu/%d [nx %d]\n",
				 slot, child->nr_leaves_on_branch, nr_free + 1,
				 next_slot);

			/* We would already have reaped an intervening shortcut
			 * on the way back up the tree.
			 */
			BUG_ON(s);

			new_n->slots[slot] = NULL;
			nr_free++;
			if (slot < next_slot)
				next_slot = slot;
			for (i = 0; i < ASSOC_ARRAY_FAN_OUT; i++) {
				struct assoc_array_ptr *p = child->slots[i];
				if (!p)
					continue;
				BUG_ON(assoc_array_ptr_is_meta(p));
				while (new_n->slots[next_slot])
					next_slot++;
				BUG_ON(next_slot >= ASSOC_ARRAY_FAN_OUT);
				new_n->slots[next_slot++] = p;
				nr_free--;
			}
			kfree(child);
		} else {
			pr_devel("[%d] retain yesde %lu/%d [nx %d]\n",
				 slot, child->nr_leaves_on_branch, nr_free + 1,
				 next_slot);
		}
	}

	pr_devel("after: %lu\n", new_n->nr_leaves_on_branch);

	nr_leaves_on_tree = new_n->nr_leaves_on_branch;

	/* Excise this yesde if it is singly occupied by a shortcut */
	if (nr_free == ASSOC_ARRAY_FAN_OUT - 1) {
		for (slot = 0; slot < ASSOC_ARRAY_FAN_OUT; slot++)
			if ((ptr = new_n->slots[slot]))
				break;

		if (assoc_array_ptr_is_meta(ptr) &&
		    assoc_array_ptr_is_shortcut(ptr)) {
			pr_devel("excise yesde %p with 1 shortcut\n", new_n);
			new_s = assoc_array_ptr_to_shortcut(ptr);
			new_parent = new_n->back_pointer;
			slot = new_n->parent_slot;
			kfree(new_n);
			if (!new_parent) {
				new_s->back_pointer = NULL;
				new_s->parent_slot = 0;
				new_root = ptr;
				goto gc_complete;
			}

			if (assoc_array_ptr_is_shortcut(new_parent)) {
				/* We can discard any preceding shortcut also */
				struct assoc_array_shortcut *s =
					assoc_array_ptr_to_shortcut(new_parent);

				pr_devel("excise preceding shortcut\n");

				new_parent = new_s->back_pointer = s->back_pointer;
				slot = new_s->parent_slot = s->parent_slot;
				kfree(s);
				if (!new_parent) {
					new_s->back_pointer = NULL;
					new_s->parent_slot = 0;
					new_root = ptr;
					goto gc_complete;
				}
			}

			new_s->back_pointer = new_parent;
			new_s->parent_slot = slot;
			new_n = assoc_array_ptr_to_yesde(new_parent);
			new_n->slots[slot] = ptr;
			goto ascend_old_tree;
		}
	}

	/* Excise any shortcuts we might encounter that point to yesdes that
	 * only contain leaves.
	 */
	ptr = new_n->back_pointer;
	if (!ptr)
		goto gc_complete;

	if (assoc_array_ptr_is_shortcut(ptr)) {
		new_s = assoc_array_ptr_to_shortcut(ptr);
		new_parent = new_s->back_pointer;
		slot = new_s->parent_slot;

		if (new_n->nr_leaves_on_branch <= ASSOC_ARRAY_FAN_OUT) {
			struct assoc_array_yesde *n;

			pr_devel("excise shortcut\n");
			new_n->back_pointer = new_parent;
			new_n->parent_slot = slot;
			kfree(new_s);
			if (!new_parent) {
				new_root = assoc_array_yesde_to_ptr(new_n);
				goto gc_complete;
			}

			n = assoc_array_ptr_to_yesde(new_parent);
			n->slots[slot] = assoc_array_yesde_to_ptr(new_n);
		}
	} else {
		new_parent = ptr;
	}
	new_n = assoc_array_ptr_to_yesde(new_parent);

ascend_old_tree:
	ptr = yesde->back_pointer;
	if (assoc_array_ptr_is_shortcut(ptr)) {
		shortcut = assoc_array_ptr_to_shortcut(ptr);
		slot = shortcut->parent_slot;
		cursor = shortcut->back_pointer;
		if (!cursor)
			goto gc_complete;
	} else {
		slot = yesde->parent_slot;
		cursor = ptr;
	}
	BUG_ON(!cursor);
	yesde = assoc_array_ptr_to_yesde(cursor);
	slot++;
	goto continue_yesde;

gc_complete:
	edit->set[0].to = new_root;
	assoc_array_apply_edit(edit);
	array->nr_leaves_on_tree = nr_leaves_on_tree;
	return 0;

eyesmem:
	pr_devel("eyesmem\n");
	assoc_array_destroy_subtree(new_root, edit->ops);
	kfree(edit);
	return -ENOMEM;
}

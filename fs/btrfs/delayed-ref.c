/*
 * Copyright (C) 2009 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include "ctree.h"
#include "delayed-ref.h"
#include "transaction.h"

/*
 * delayed back reference update tracking.  For subvolume trees
 * we queue up extent allocations and backref maintenance for
 * delayed processing.   This avoids deep call chains where we
 * add extents in the middle of btrfs_search_slot, and it allows
 * us to buffer up frequently modified backrefs in an rb tree instead
 * of hammering updates on the extent allocation tree.
 */

/*
 * compare two delayed tree backrefs with same bytenr and type
 */
static int comp_tree_refs(struct btrfs_delayed_tree_ref *ref2,
			  struct btrfs_delayed_tree_ref *ref1)
{
	if (ref1->node.type == BTRFS_TREE_BLOCK_REF_KEY) {
		if (ref1->root < ref2->root)
			return -1;
		if (ref1->root > ref2->root)
			return 1;
	} else {
		if (ref1->parent < ref2->parent)
			return -1;
		if (ref1->parent > ref2->parent)
			return 1;
	}
	return 0;
}

/*
 * compare two delayed data backrefs with same bytenr and type
 */
static int comp_data_refs(struct btrfs_delayed_data_ref *ref2,
			  struct btrfs_delayed_data_ref *ref1)
{
	if (ref1->node.type == BTRFS_EXTENT_DATA_REF_KEY) {
		if (ref1->root < ref2->root)
			return -1;
		if (ref1->root > ref2->root)
			return 1;
		if (ref1->objectid < ref2->objectid)
			return -1;
		if (ref1->objectid > ref2->objectid)
			return 1;
		if (ref1->offset < ref2->offset)
			return -1;
		if (ref1->offset > ref2->offset)
			return 1;
	} else {
		if (ref1->parent < ref2->parent)
			return -1;
		if (ref1->parent > ref2->parent)
			return 1;
	}
	return 0;
}

/*
 * entries in the rb tree are ordered by the byte number of the extent,
 * type of the delayed backrefs and content of delayed backrefs.
 */
static int comp_entry(struct btrfs_delayed_ref_node *ref2,
		      struct btrfs_delayed_ref_node *ref1)
{
	if (ref1->bytenr < ref2->bytenr)
		return -1;
	if (ref1->bytenr > ref2->bytenr)
		return 1;
	if (ref1->is_head && ref2->is_head)
		return 0;
	if (ref2->is_head)
		return -1;
	if (ref1->is_head)
		return 1;
	if (ref1->type < ref2->type)
		return -1;
	if (ref1->type > ref2->type)
		return 1;
	if (ref1->type == BTRFS_TREE_BLOCK_REF_KEY ||
	    ref1->type == BTRFS_SHARED_BLOCK_REF_KEY) {
		return comp_tree_refs(btrfs_delayed_node_to_tree_ref(ref2),
				      btrfs_delayed_node_to_tree_ref(ref1));
	} else if (ref1->type == BTRFS_EXTENT_DATA_REF_KEY ||
		   ref1->type == BTRFS_SHARED_DATA_REF_KEY) {
		return comp_data_refs(btrfs_delayed_node_to_data_ref(ref2),
				      btrfs_delayed_node_to_data_ref(ref1));
	}
	BUG();
	return 0;
}

/*
 * insert a new ref into the rbtree.  This returns any existing refs
 * for the same (bytenr,parent) tuple, or NULL if the new node was properly
 * inserted.
 */
static struct btrfs_delayed_ref_node *tree_insert(struct rb_root *root,
						  struct rb_node *node)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent_node = NULL;
	struct btrfs_delayed_ref_node *entry;
	struct btrfs_delayed_ref_node *ins;
	int cmp;

	ins = rb_entry(node, struct btrfs_delayed_ref_node, rb_node);
	while (*p) {
		parent_node = *p;
		entry = rb_entry(parent_node, struct btrfs_delayed_ref_node,
				 rb_node);

		cmp = comp_entry(entry, ins);
		if (cmp < 0)
			p = &(*p)->rb_left;
		else if (cmp > 0)
			p = &(*p)->rb_right;
		else
			return entry;
	}

	rb_link_node(node, parent_node, p);
	rb_insert_color(node, root);
	return NULL;
}

/*
 * find an head entry based on bytenr. This returns the delayed ref
 * head if it was able to find one, or NULL if nothing was in that spot
 */
static struct btrfs_delayed_ref_node *find_ref_head(struct rb_root *root,
				  u64 bytenr,
				  struct btrfs_delayed_ref_node **last)
{
	struct rb_node *n = root->rb_node;
	struct btrfs_delayed_ref_node *entry;
	int cmp;

	while (n) {
		entry = rb_entry(n, struct btrfs_delayed_ref_node, rb_node);
		WARN_ON(!entry->in_tree);
		if (last)
			*last = entry;

		if (bytenr < entry->bytenr)
			cmp = -1;
		else if (bytenr > entry->bytenr)
			cmp = 1;
		else if (!btrfs_delayed_ref_is_head(entry))
			cmp = 1;
		else
			cmp = 0;

		if (cmp < 0)
			n = n->rb_left;
		else if (cmp > 0)
			n = n->rb_right;
		else
			return entry;
	}
	return NULL;
}

int btrfs_delayed_ref_lock(struct btrfs_trans_handle *trans,
			   struct btrfs_delayed_ref_head *head)
{
	struct btrfs_delayed_ref_root *delayed_refs;

	delayed_refs = &trans->transaction->delayed_refs;
	assert_spin_locked(&delayed_refs->lock);
	if (mutex_trylock(&head->mutex))
		return 0;

	atomic_inc(&head->node.refs);
	spin_unlock(&delayed_refs->lock);

	mutex_lock(&head->mutex);
	spin_lock(&delayed_refs->lock);
	if (!head->node.in_tree) {
		mutex_unlock(&head->mutex);
		btrfs_put_delayed_ref(&head->node);
		return -EAGAIN;
	}
	btrfs_put_delayed_ref(&head->node);
	return 0;
}

int btrfs_find_ref_cluster(struct btrfs_trans_handle *trans,
			   struct list_head *cluster, u64 start)
{
	int count = 0;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct rb_node *node;
	struct btrfs_delayed_ref_node *ref;
	struct btrfs_delayed_ref_head *head;

	delayed_refs = &trans->transaction->delayed_refs;
	if (start == 0) {
		node = rb_first(&delayed_refs->root);
	} else {
		ref = NULL;
		find_ref_head(&delayed_refs->root, start, &ref);
		if (ref) {
			struct btrfs_delayed_ref_node *tmp;

			node = rb_prev(&ref->rb_node);
			while (node) {
				tmp = rb_entry(node,
					       struct btrfs_delayed_ref_node,
					       rb_node);
				if (tmp->bytenr < start)
					break;
				ref = tmp;
				node = rb_prev(&ref->rb_node);
			}
			node = &ref->rb_node;
		} else
			node = rb_first(&delayed_refs->root);
	}
again:
	while (node && count < 32) {
		ref = rb_entry(node, struct btrfs_delayed_ref_node, rb_node);
		if (btrfs_delayed_ref_is_head(ref)) {
			head = btrfs_delayed_node_to_head(ref);
			if (list_empty(&head->cluster)) {
				list_add_tail(&head->cluster, cluster);
				delayed_refs->run_delayed_start =
					head->node.bytenr;
				count++;

				WARN_ON(delayed_refs->num_heads_ready == 0);
				delayed_refs->num_heads_ready--;
			} else if (count) {
				/* the goal of the clustering is to find extents
				 * that are likely to end up in the same extent
				 * leaf on disk.  So, we don't want them spread
				 * all over the tree.  Stop now if we've hit
				 * a head that was already in use
				 */
				break;
			}
		}
		node = rb_next(node);
	}
	if (count) {
		return 0;
	} else if (start) {
		/*
		 * we've gone to the end of the rbtree without finding any
		 * clusters.  start from the beginning and try again
		 */
		start = 0;
		node = rb_first(&delayed_refs->root);
		goto again;
	}
	return 1;
}

/*
 * This checks to see if there are any delayed refs in the
 * btree for a given bytenr.  It returns one if it finds any
 * and zero otherwise.
 *
 * If it only finds a head node, it returns 0.
 *
 * The idea is to use this when deciding if you can safely delete an
 * extent from the extent allocation tree.  There may be a pending
 * ref in the rbtree that adds or removes references, so as long as this
 * returns one you need to leave the BTRFS_EXTENT_ITEM in the extent
 * allocation tree.
 */
int btrfs_delayed_ref_pending(struct btrfs_trans_handle *trans, u64 bytenr)
{
	struct btrfs_delayed_ref_node *ref;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct rb_node *prev_node;
	int ret = 0;

	delayed_refs = &trans->transaction->delayed_refs;
	spin_lock(&delayed_refs->lock);

	ref = find_ref_head(&delayed_refs->root, bytenr, NULL);
	if (ref) {
		prev_node = rb_prev(&ref->rb_node);
		if (!prev_node)
			goto out;
		ref = rb_entry(prev_node, struct btrfs_delayed_ref_node,
			       rb_node);
		if (ref->bytenr == bytenr)
			ret = 1;
	}
out:
	spin_unlock(&delayed_refs->lock);
	return ret;
}

/*
 * helper function to update an extent delayed ref in the
 * rbtree.  existing and update must both have the same
 * bytenr and parent
 *
 * This may free existing if the update cancels out whatever
 * operation it was doing.
 */
static noinline void
update_existing_ref(struct btrfs_trans_handle *trans,
		    struct btrfs_delayed_ref_root *delayed_refs,
		    struct btrfs_delayed_ref_node *existing,
		    struct btrfs_delayed_ref_node *update)
{
	if (update->action != existing->action) {
		/*
		 * this is effectively undoing either an add or a
		 * drop.  We decrement the ref_mod, and if it goes
		 * down to zero we just delete the entry without
		 * every changing the extent allocation tree.
		 */
		existing->ref_mod--;
		if (existing->ref_mod == 0) {
			rb_erase(&existing->rb_node,
				 &delayed_refs->root);
			existing->in_tree = 0;
			btrfs_put_delayed_ref(existing);
			delayed_refs->num_entries--;
			if (trans->delayed_ref_updates)
				trans->delayed_ref_updates--;
		} else {
			WARN_ON(existing->type == BTRFS_TREE_BLOCK_REF_KEY ||
				existing->type == BTRFS_SHARED_BLOCK_REF_KEY);
		}
	} else {
		WARN_ON(existing->type == BTRFS_TREE_BLOCK_REF_KEY ||
			existing->type == BTRFS_SHARED_BLOCK_REF_KEY);
		/*
		 * the action on the existing ref matches
		 * the action on the ref we're trying to add.
		 * Bump the ref_mod by one so the backref that
		 * is eventually added/removed has the correct
		 * reference count
		 */
		existing->ref_mod += update->ref_mod;
	}
}

/*
 * helper function to update the accounting in the head ref
 * existing and update must have the same bytenr
 */
static noinline void
update_existing_head_ref(struct btrfs_delayed_ref_node *existing,
			 struct btrfs_delayed_ref_node *update)
{
	struct btrfs_delayed_ref_head *existing_ref;
	struct btrfs_delayed_ref_head *ref;

	existing_ref = btrfs_delayed_node_to_head(existing);
	ref = btrfs_delayed_node_to_head(update);
	BUG_ON(existing_ref->is_data != ref->is_data);

	if (ref->must_insert_reserved) {
		/* if the extent was freed and then
		 * reallocated before the delayed ref
		 * entries were processed, we can end up
		 * with an existing head ref without
		 * the must_insert_reserved flag set.
		 * Set it again here
		 */
		existing_ref->must_insert_reserved = ref->must_insert_reserved;

		/*
		 * update the num_bytes so we make sure the accounting
		 * is done correctly
		 */
		existing->num_bytes = update->num_bytes;

	}

	if (ref->extent_op) {
		if (!existing_ref->extent_op) {
			existing_ref->extent_op = ref->extent_op;
		} else {
			if (ref->extent_op->update_key) {
				memcpy(&existing_ref->extent_op->key,
				       &ref->extent_op->key,
				       sizeof(ref->extent_op->key));
				existing_ref->extent_op->update_key = 1;
			}
			if (ref->extent_op->update_flags) {
				existing_ref->extent_op->flags_to_set |=
					ref->extent_op->flags_to_set;
				existing_ref->extent_op->update_flags = 1;
			}
			kfree(ref->extent_op);
		}
	}
	/*
	 * update the reference mod on the head to reflect this new operation
	 */
	existing->ref_mod += update->ref_mod;
}

/*
 * helper function to actually insert a head node into the rbtree.
 * this does all the dirty work in terms of maintaining the correct
 * overall modification count.
 */
static noinline int add_delayed_ref_head(struct btrfs_trans_handle *trans,
					struct btrfs_delayed_ref_node *ref,
					u64 bytenr, u64 num_bytes,
					int action, int is_data)
{
	struct btrfs_delayed_ref_node *existing;
	struct btrfs_delayed_ref_head *head_ref = NULL;
	struct btrfs_delayed_ref_root *delayed_refs;
	int count_mod = 1;
	int must_insert_reserved = 0;

	/*
	 * the head node stores the sum of all the mods, so dropping a ref
	 * should drop the sum in the head node by one.
	 */
	if (action == BTRFS_UPDATE_DELAYED_HEAD)
		count_mod = 0;
	else if (action == BTRFS_DROP_DELAYED_REF)
		count_mod = -1;

	/*
	 * BTRFS_ADD_DELAYED_EXTENT means that we need to update
	 * the reserved accounting when the extent is finally added, or
	 * if a later modification deletes the delayed ref without ever
	 * inserting the extent into the extent allocation tree.
	 * ref->must_insert_reserved is the flag used to record
	 * that accounting mods are required.
	 *
	 * Once we record must_insert_reserved, switch the action to
	 * BTRFS_ADD_DELAYED_REF because other special casing is not required.
	 */
	if (action == BTRFS_ADD_DELAYED_EXTENT)
		must_insert_reserved = 1;
	else
		must_insert_reserved = 0;

	delayed_refs = &trans->transaction->delayed_refs;

	/* first set the basic ref node struct up */
	atomic_set(&ref->refs, 1);
	ref->bytenr = bytenr;
	ref->num_bytes = num_bytes;
	ref->ref_mod = count_mod;
	ref->type  = 0;
	ref->action  = 0;
	ref->is_head = 1;
	ref->in_tree = 1;

	head_ref = btrfs_delayed_node_to_head(ref);
	head_ref->must_insert_reserved = must_insert_reserved;
	head_ref->is_data = is_data;

	INIT_LIST_HEAD(&head_ref->cluster);
	mutex_init(&head_ref->mutex);

	trace_btrfs_delayed_ref_head(ref, head_ref, action);

	existing = tree_insert(&delayed_refs->root, &ref->rb_node);

	if (existing) {
		update_existing_head_ref(existing, ref);
		/*
		 * we've updated the existing ref, free the newly
		 * allocated ref
		 */
		kfree(ref);
	} else {
		delayed_refs->num_heads++;
		delayed_refs->num_heads_ready++;
		delayed_refs->num_entries++;
		trans->delayed_ref_updates++;
	}
	return 0;
}

/*
 * helper to insert a delayed tree ref into the rbtree.
 */
static noinline int add_delayed_tree_ref(struct btrfs_trans_handle *trans,
					 struct btrfs_delayed_ref_node *ref,
					 u64 bytenr, u64 num_bytes, u64 parent,
					 u64 ref_root, int level, int action)
{
	struct btrfs_delayed_ref_node *existing;
	struct btrfs_delayed_tree_ref *full_ref;
	struct btrfs_delayed_ref_root *delayed_refs;

	if (action == BTRFS_ADD_DELAYED_EXTENT)
		action = BTRFS_ADD_DELAYED_REF;

	delayed_refs = &trans->transaction->delayed_refs;

	/* first set the basic ref node struct up */
	atomic_set(&ref->refs, 1);
	ref->bytenr = bytenr;
	ref->num_bytes = num_bytes;
	ref->ref_mod = 1;
	ref->action = action;
	ref->is_head = 0;
	ref->in_tree = 1;

	full_ref = btrfs_delayed_node_to_tree_ref(ref);
	if (parent) {
		full_ref->parent = parent;
		ref->type = BTRFS_SHARED_BLOCK_REF_KEY;
	} else {
		full_ref->root = ref_root;
		ref->type = BTRFS_TREE_BLOCK_REF_KEY;
	}
	full_ref->level = level;

	trace_btrfs_delayed_tree_ref(ref, full_ref, action);

	existing = tree_insert(&delayed_refs->root, &ref->rb_node);

	if (existing) {
		update_existing_ref(trans, delayed_refs, existing, ref);
		/*
		 * we've updated the existing ref, free the newly
		 * allocated ref
		 */
		kfree(ref);
	} else {
		delayed_refs->num_entries++;
		trans->delayed_ref_updates++;
	}
	return 0;
}

/*
 * helper to insert a delayed data ref into the rbtree.
 */
static noinline int add_delayed_data_ref(struct btrfs_trans_handle *trans,
					 struct btrfs_delayed_ref_node *ref,
					 u64 bytenr, u64 num_bytes, u64 parent,
					 u64 ref_root, u64 owner, u64 offset,
					 int action)
{
	struct btrfs_delayed_ref_node *existing;
	struct btrfs_delayed_data_ref *full_ref;
	struct btrfs_delayed_ref_root *delayed_refs;

	if (action == BTRFS_ADD_DELAYED_EXTENT)
		action = BTRFS_ADD_DELAYED_REF;

	delayed_refs = &trans->transaction->delayed_refs;

	/* first set the basic ref node struct up */
	atomic_set(&ref->refs, 1);
	ref->bytenr = bytenr;
	ref->num_bytes = num_bytes;
	ref->ref_mod = 1;
	ref->action = action;
	ref->is_head = 0;
	ref->in_tree = 1;

	full_ref = btrfs_delayed_node_to_data_ref(ref);
	if (parent) {
		full_ref->parent = parent;
		ref->type = BTRFS_SHARED_DATA_REF_KEY;
	} else {
		full_ref->root = ref_root;
		ref->type = BTRFS_EXTENT_DATA_REF_KEY;
	}
	full_ref->objectid = owner;
	full_ref->offset = offset;

	trace_btrfs_delayed_data_ref(ref, full_ref, action);

	existing = tree_insert(&delayed_refs->root, &ref->rb_node);

	if (existing) {
		update_existing_ref(trans, delayed_refs, existing, ref);
		/*
		 * we've updated the existing ref, free the newly
		 * allocated ref
		 */
		kfree(ref);
	} else {
		delayed_refs->num_entries++;
		trans->delayed_ref_updates++;
	}
	return 0;
}

/*
 * add a delayed tree ref.  This does all of the accounting required
 * to make sure the delayed ref is eventually processed before this
 * transaction commits.
 */
int btrfs_add_delayed_tree_ref(struct btrfs_trans_handle *trans,
			       u64 bytenr, u64 num_bytes, u64 parent,
			       u64 ref_root,  int level, int action,
			       struct btrfs_delayed_extent_op *extent_op)
{
	struct btrfs_delayed_tree_ref *ref;
	struct btrfs_delayed_ref_head *head_ref;
	struct btrfs_delayed_ref_root *delayed_refs;
	int ret;

	BUG_ON(extent_op && extent_op->is_data);
	ref = kmalloc(sizeof(*ref), GFP_NOFS);
	if (!ref)
		return -ENOMEM;

	head_ref = kmalloc(sizeof(*head_ref), GFP_NOFS);
	if (!head_ref) {
		kfree(ref);
		return -ENOMEM;
	}

	head_ref->extent_op = extent_op;

	delayed_refs = &trans->transaction->delayed_refs;
	spin_lock(&delayed_refs->lock);

	/*
	 * insert both the head node and the new ref without dropping
	 * the spin lock
	 */
	ret = add_delayed_ref_head(trans, &head_ref->node, bytenr, num_bytes,
				   action, 0);
	BUG_ON(ret);

	ret = add_delayed_tree_ref(trans, &ref->node, bytenr, num_bytes,
				   parent, ref_root, level, action);
	BUG_ON(ret);
	spin_unlock(&delayed_refs->lock);
	return 0;
}

/*
 * add a delayed data ref. it's similar to btrfs_add_delayed_tree_ref.
 */
int btrfs_add_delayed_data_ref(struct btrfs_trans_handle *trans,
			       u64 bytenr, u64 num_bytes,
			       u64 parent, u64 ref_root,
			       u64 owner, u64 offset, int action,
			       struct btrfs_delayed_extent_op *extent_op)
{
	struct btrfs_delayed_data_ref *ref;
	struct btrfs_delayed_ref_head *head_ref;
	struct btrfs_delayed_ref_root *delayed_refs;
	int ret;

	BUG_ON(extent_op && !extent_op->is_data);
	ref = kmalloc(sizeof(*ref), GFP_NOFS);
	if (!ref)
		return -ENOMEM;

	head_ref = kmalloc(sizeof(*head_ref), GFP_NOFS);
	if (!head_ref) {
		kfree(ref);
		return -ENOMEM;
	}

	head_ref->extent_op = extent_op;

	delayed_refs = &trans->transaction->delayed_refs;
	spin_lock(&delayed_refs->lock);

	/*
	 * insert both the head node and the new ref without dropping
	 * the spin lock
	 */
	ret = add_delayed_ref_head(trans, &head_ref->node, bytenr, num_bytes,
				   action, 1);
	BUG_ON(ret);

	ret = add_delayed_data_ref(trans, &ref->node, bytenr, num_bytes,
				   parent, ref_root, owner, offset, action);
	BUG_ON(ret);
	spin_unlock(&delayed_refs->lock);
	return 0;
}

int btrfs_add_delayed_extent_op(struct btrfs_trans_handle *trans,
				u64 bytenr, u64 num_bytes,
				struct btrfs_delayed_extent_op *extent_op)
{
	struct btrfs_delayed_ref_head *head_ref;
	struct btrfs_delayed_ref_root *delayed_refs;
	int ret;

	head_ref = kmalloc(sizeof(*head_ref), GFP_NOFS);
	if (!head_ref)
		return -ENOMEM;

	head_ref->extent_op = extent_op;

	delayed_refs = &trans->transaction->delayed_refs;
	spin_lock(&delayed_refs->lock);

	ret = add_delayed_ref_head(trans, &head_ref->node, bytenr,
				   num_bytes, BTRFS_UPDATE_DELAYED_HEAD,
				   extent_op->is_data);
	BUG_ON(ret);

	spin_unlock(&delayed_refs->lock);
	return 0;
}

/*
 * this does a simple search for the head node for a given extent.
 * It must be called with the delayed ref spinlock held, and it returns
 * the head node if any where found, or NULL if not.
 */
struct btrfs_delayed_ref_head *
btrfs_find_delayed_ref_head(struct btrfs_trans_handle *trans, u64 bytenr)
{
	struct btrfs_delayed_ref_node *ref;
	struct btrfs_delayed_ref_root *delayed_refs;

	delayed_refs = &trans->transaction->delayed_refs;
	ref = find_ref_head(&delayed_refs->root, bytenr, NULL);
	if (ref)
		return btrfs_delayed_node_to_head(ref);
	return NULL;
}

/*
 * add a delayed ref to the tree.  This does all of the accounting required
 * to make sure the delayed ref is eventually processed before this
 * transaction commits.
 *
 * The main point of this call is to add and remove a backreference in a single
 * shot, taking the lock only once, and only searching for the head node once.
 *
 * It is the same as doing a ref add and delete in two separate calls.
 */
#if 0
int btrfs_update_delayed_ref(struct btrfs_trans_handle *trans,
			  u64 bytenr, u64 num_bytes, u64 orig_parent,
			  u64 parent, u64 orig_ref_root, u64 ref_root,
			  u64 orig_ref_generation, u64 ref_generation,
			  u64 owner_objectid, int pin)
{
	struct btrfs_delayed_ref *ref;
	struct btrfs_delayed_ref *old_ref;
	struct btrfs_delayed_ref_head *head_ref;
	struct btrfs_delayed_ref_root *delayed_refs;
	int ret;

	ref = kmalloc(sizeof(*ref), GFP_NOFS);
	if (!ref)
		return -ENOMEM;

	old_ref = kmalloc(sizeof(*old_ref), GFP_NOFS);
	if (!old_ref) {
		kfree(ref);
		return -ENOMEM;
	}

	/*
	 * the parent = 0 case comes from cases where we don't actually
	 * know the parent yet.  It will get updated later via a add/drop
	 * pair.
	 */
	if (parent == 0)
		parent = bytenr;
	if (orig_parent == 0)
		orig_parent = bytenr;

	head_ref = kmalloc(sizeof(*head_ref), GFP_NOFS);
	if (!head_ref) {
		kfree(ref);
		kfree(old_ref);
		return -ENOMEM;
	}
	delayed_refs = &trans->transaction->delayed_refs;
	spin_lock(&delayed_refs->lock);

	/*
	 * insert both the head node and the new ref without dropping
	 * the spin lock
	 */
	ret = __btrfs_add_delayed_ref(trans, &head_ref->node, bytenr, num_bytes,
				      (u64)-1, 0, 0, 0,
				      BTRFS_UPDATE_DELAYED_HEAD, 0);
	BUG_ON(ret);

	ret = __btrfs_add_delayed_ref(trans, &ref->node, bytenr, num_bytes,
				      parent, ref_root, ref_generation,
				      owner_objectid, BTRFS_ADD_DELAYED_REF, 0);
	BUG_ON(ret);

	ret = __btrfs_add_delayed_ref(trans, &old_ref->node, bytenr, num_bytes,
				      orig_parent, orig_ref_root,
				      orig_ref_generation, owner_objectid,
				      BTRFS_DROP_DELAYED_REF, pin);
	BUG_ON(ret);
	spin_unlock(&delayed_refs->lock);
	return 0;
}
#endif

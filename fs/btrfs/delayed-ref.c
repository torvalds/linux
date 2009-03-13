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
#include <linux/sort.h>
#include <linux/ftrace.h>
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
 *
 * Right now this code is only used for reference counted trees, but
 * the long term goal is to get rid of the similar code for delayed
 * extent tree modifications.
 */

/*
 * entries in the rb tree are ordered by the byte number of the extent
 * and by the byte number of the parent block.
 */
static int comp_entry(struct btrfs_delayed_ref_node *ref,
		      u64 bytenr, u64 parent)
{
	if (bytenr < ref->bytenr)
		return -1;
	if (bytenr > ref->bytenr)
		return 1;
	if (parent < ref->parent)
		return -1;
	if (parent > ref->parent)
		return 1;
	return 0;
}

/*
 * insert a new ref into the rbtree.  This returns any existing refs
 * for the same (bytenr,parent) tuple, or NULL if the new node was properly
 * inserted.
 */
static struct btrfs_delayed_ref_node *tree_insert(struct rb_root *root,
						  u64 bytenr, u64 parent,
						  struct rb_node *node)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent_node = NULL;
	struct btrfs_delayed_ref_node *entry;
	int cmp;

	while (*p) {
		parent_node = *p;
		entry = rb_entry(parent_node, struct btrfs_delayed_ref_node,
				 rb_node);

		cmp = comp_entry(entry, bytenr, parent);
		if (cmp < 0)
			p = &(*p)->rb_left;
		else if (cmp > 0)
			p = &(*p)->rb_right;
		else
			return entry;
	}

	entry = rb_entry(node, struct btrfs_delayed_ref_node, rb_node);
	rb_link_node(node, parent_node, p);
	rb_insert_color(node, root);
	return NULL;
}

/*
 * find an entry based on (bytenr,parent).  This returns the delayed
 * ref if it was able to find one, or NULL if nothing was in that spot
 */
static struct btrfs_delayed_ref_node *tree_search(struct rb_root *root,
						  u64 bytenr, u64 parent)
{
	struct rb_node *n = root->rb_node;
	struct btrfs_delayed_ref_node *entry;
	int cmp;

	while (n) {
		entry = rb_entry(n, struct btrfs_delayed_ref_node, rb_node);
		WARN_ON(!entry->in_tree);

		cmp = comp_entry(entry, bytenr, parent);
		if (cmp < 0)
			n = n->rb_left;
		else if (cmp > 0)
			n = n->rb_right;
		else
			return entry;
	}
	return NULL;
}

/*
 * Locking on delayed refs is done by taking a lock on the head node,
 * which has the (impossible) parent id of (u64)-1.  Once a lock is held
 * on the head node, you're allowed (and required) to process all the
 * delayed refs for a given byte number in the tree.
 *
 * This will walk forward in the rbtree until it finds a head node it
 * is able to lock.  It might not lock the delayed ref you asked for,
 * and so it will return the one it did lock in next_ret and return 0.
 *
 * If no locks are taken, next_ret is set to null and 1 is returned.  This
 * means there are no more unlocked head nodes in the rbtree.
 */
int btrfs_lock_delayed_ref(struct btrfs_trans_handle *trans,
			   struct btrfs_delayed_ref_node *ref,
			   struct btrfs_delayed_ref_head **next_ret)
{
	struct rb_node *node;
	struct btrfs_delayed_ref_head *head;
	int ret = 0;

	while (1) {
		if (btrfs_delayed_ref_is_head(ref)) {
			head = btrfs_delayed_node_to_head(ref);
			if (mutex_trylock(&head->mutex)) {
				*next_ret = head;
				ret = 0;
				break;
			}
		}
		node = rb_next(&ref->rb_node);
		if (!node) {
			ret = 1;
			*next_ret = NULL;
			break;
		}
		ref = rb_entry(node, struct btrfs_delayed_ref_node, rb_node);
	}
	return ret;
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

	ref = tree_search(&delayed_refs->root, bytenr, (u64)-1);
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
 * helper function to lookup reference count
 *
 * the head node for delayed ref is used to store the sum of all the
 * reference count modifications queued up in the rbtree.  This way you
 * can check to see what the reference count would be if all of the
 * delayed refs are processed.
 */
int btrfs_lookup_extent_ref(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root, u64 bytenr,
			    u64 num_bytes, u32 *refs)
{
	struct btrfs_delayed_ref_node *ref;
	struct btrfs_delayed_ref_head *head;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_extent_item *ei;
	struct btrfs_key key;
	u32 num_refs;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = num_bytes;
	delayed_refs = &trans->transaction->delayed_refs;
again:
	ret = btrfs_search_slot(trans, root->fs_info->extent_root,
				&key, path, 0, 0);
	if (ret < 0)
		goto out;

	if (ret == 0) {
		leaf = path->nodes[0];
		ei = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_extent_item);
		num_refs = btrfs_extent_refs(leaf, ei);
	} else {
		num_refs = 0;
		ret = 0;
	}

	spin_lock(&delayed_refs->lock);
	ref = tree_search(&delayed_refs->root, bytenr, (u64)-1);
	if (ref) {
		head = btrfs_delayed_node_to_head(ref);
		if (mutex_trylock(&head->mutex)) {
			num_refs += ref->ref_mod;
			mutex_unlock(&head->mutex);
			*refs = num_refs;
			goto out;
		}

		atomic_inc(&ref->refs);
		spin_unlock(&delayed_refs->lock);

		btrfs_release_path(root->fs_info->extent_root, path);

		mutex_lock(&head->mutex);
		mutex_unlock(&head->mutex);
		btrfs_put_delayed_ref(ref);
		goto again;
	} else {
		*refs = num_refs;
	}
out:
	spin_unlock(&delayed_refs->lock);
	btrfs_free_path(path);
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
	struct btrfs_delayed_ref *existing_ref;
	struct btrfs_delayed_ref *ref;

	existing_ref = btrfs_delayed_node_to_ref(existing);
	ref = btrfs_delayed_node_to_ref(update);

	if (ref->pin)
		existing_ref->pin = 1;

	if (ref->action != existing_ref->action) {
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
		}
	} else {
		if (existing_ref->action == BTRFS_ADD_DELAYED_REF) {
			/* if we're adding refs, make sure all the
			 * details match up.  The extent could
			 * have been totally freed and reallocated
			 * by a different owner before the delayed
			 * ref entries were removed.
			 */
			existing_ref->owner_objectid = ref->owner_objectid;
			existing_ref->generation = ref->generation;
			existing_ref->root = ref->root;
			existing->num_bytes = update->num_bytes;
		}
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

	/*
	 * update the reference mod on the head to reflect this new operation
	 */
	existing->ref_mod += update->ref_mod;
}

/*
 * helper function to actually insert a delayed ref into the rbtree.
 * this does all the dirty work in terms of maintaining the correct
 * overall modification count in the head node and properly dealing
 * with updating existing nodes as new modifications are queued.
 */
static noinline int __btrfs_add_delayed_ref(struct btrfs_trans_handle *trans,
			  struct btrfs_delayed_ref_node *ref,
			  u64 bytenr, u64 num_bytes, u64 parent, u64 ref_root,
			  u64 ref_generation, u64 owner_objectid, int action,
			  int pin)
{
	struct btrfs_delayed_ref_node *existing;
	struct btrfs_delayed_ref *full_ref;
	struct btrfs_delayed_ref_head *head_ref;
	struct btrfs_delayed_ref_root *delayed_refs;
	int count_mod = 1;
	int must_insert_reserved = 0;

	/*
	 * the head node stores the sum of all the mods, so dropping a ref
	 * should drop the sum in the head node by one.
	 */
	if (parent == (u64)-1 && action == BTRFS_DROP_DELAYED_REF)
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
	if (action == BTRFS_ADD_DELAYED_EXTENT) {
		must_insert_reserved = 1;
		action = BTRFS_ADD_DELAYED_REF;
	} else {
		must_insert_reserved = 0;
	}


	delayed_refs = &trans->transaction->delayed_refs;

	/* first set the basic ref node struct up */
	atomic_set(&ref->refs, 1);
	ref->bytenr = bytenr;
	ref->parent = parent;
	ref->ref_mod = count_mod;
	ref->in_tree = 1;
	ref->num_bytes = num_bytes;

	if (btrfs_delayed_ref_is_head(ref)) {
		head_ref = btrfs_delayed_node_to_head(ref);
		head_ref->must_insert_reserved = must_insert_reserved;
		mutex_init(&head_ref->mutex);
	} else {
		full_ref = btrfs_delayed_node_to_ref(ref);
		full_ref->root = ref_root;
		full_ref->generation = ref_generation;
		full_ref->owner_objectid = owner_objectid;
		full_ref->pin = pin;
		full_ref->action = action;
	}

	existing = tree_insert(&delayed_refs->root, bytenr,
			       parent, &ref->rb_node);

	if (existing) {
		if (btrfs_delayed_ref_is_head(ref))
			update_existing_head_ref(existing, ref);
		else
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
 * add a delayed ref to the tree.  This does all of the accounting required
 * to make sure the delayed ref is eventually processed before this
 * transaction commits.
 */
int btrfs_add_delayed_ref(struct btrfs_trans_handle *trans,
			  u64 bytenr, u64 num_bytes, u64 parent, u64 ref_root,
			  u64 ref_generation, u64 owner_objectid, int action,
			  int pin)
{
	struct btrfs_delayed_ref *ref;
	struct btrfs_delayed_ref_head *head_ref;
	struct btrfs_delayed_ref_root *delayed_refs;
	int ret;

	ref = kmalloc(sizeof(*ref), GFP_NOFS);
	if (!ref)
		return -ENOMEM;

	/*
	 * the parent = 0 case comes from cases where we don't actually
	 * know the parent yet.  It will get updated later via a add/drop
	 * pair.
	 */
	if (parent == 0)
		parent = bytenr;

	head_ref = kmalloc(sizeof(*head_ref), GFP_NOFS);
	if (!head_ref) {
		kfree(ref);
		return -ENOMEM;
	}
	delayed_refs = &trans->transaction->delayed_refs;
	spin_lock(&delayed_refs->lock);

	/*
	 * insert both the head node and the new ref without dropping
	 * the spin lock
	 */
	ret = __btrfs_add_delayed_ref(trans, &head_ref->node, bytenr, num_bytes,
				      (u64)-1, 0, 0, 0, action, pin);
	BUG_ON(ret);

	ret = __btrfs_add_delayed_ref(trans, &ref->node, bytenr, num_bytes,
				      parent, ref_root, ref_generation,
				      owner_objectid, action, pin);
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
	ref = tree_search(&delayed_refs->root, bytenr, (u64)-1);
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
				      BTRFS_ADD_DELAYED_REF, 0);
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

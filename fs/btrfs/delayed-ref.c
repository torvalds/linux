// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2009 Oracle.  All rights reserved.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include "messages.h"
#include "ctree.h"
#include "delayed-ref.h"
#include "extent-tree.h"
#include "transaction.h"
#include "qgroup.h"
#include "space-info.h"
#include "tree-mod-log.h"
#include "fs.h"

struct kmem_cache *btrfs_delayed_ref_head_cachep;
struct kmem_cache *btrfs_delayed_ref_node_cachep;
struct kmem_cache *btrfs_delayed_extent_op_cachep;
/*
 * delayed back reference update tracking.  For subvolume trees
 * we queue up extent allocations and backref maintenance for
 * delayed processing.   This avoids deep call chains where we
 * add extents in the middle of btrfs_search_slot, and it allows
 * us to buffer up frequently modified backrefs in an rb tree instead
 * of hammering updates on the extent allocation tree.
 */

bool btrfs_check_space_for_delayed_refs(struct btrfs_fs_info *fs_info)
{
	struct btrfs_block_rsv *delayed_refs_rsv = &fs_info->delayed_refs_rsv;
	struct btrfs_block_rsv *global_rsv = &fs_info->global_block_rsv;
	bool ret = false;
	u64 reserved;

	spin_lock(&global_rsv->lock);
	reserved = global_rsv->reserved;
	spin_unlock(&global_rsv->lock);

	/*
	 * Since the global reserve is just kind of magic we don't really want
	 * to rely on it to save our bacon, so if our size is more than the
	 * delayed_refs_rsv and the global rsv then it's time to think about
	 * bailing.
	 */
	spin_lock(&delayed_refs_rsv->lock);
	reserved += delayed_refs_rsv->reserved;
	if (delayed_refs_rsv->size >= reserved)
		ret = true;
	spin_unlock(&delayed_refs_rsv->lock);
	return ret;
}

/*
 * Release a ref head's reservation.
 *
 * @fs_info:  the filesystem
 * @nr_refs:  number of delayed refs to drop
 * @nr_csums: number of csum items to drop
 *
 * Drops the delayed ref head's count from the delayed refs rsv and free any
 * excess reservation we had.
 */
void btrfs_delayed_refs_rsv_release(struct btrfs_fs_info *fs_info, int nr_refs, int nr_csums)
{
	struct btrfs_block_rsv *block_rsv = &fs_info->delayed_refs_rsv;
	u64 num_bytes;
	u64 released;

	num_bytes = btrfs_calc_delayed_ref_bytes(fs_info, nr_refs);
	num_bytes += btrfs_calc_delayed_ref_csum_bytes(fs_info, nr_csums);

	released = btrfs_block_rsv_release(fs_info, block_rsv, num_bytes, NULL);
	if (released)
		trace_btrfs_space_reservation(fs_info, "delayed_refs_rsv",
					      0, released, 0);
}

/*
 * Adjust the size of the delayed refs rsv.
 *
 * This is to be called anytime we may have adjusted trans->delayed_ref_updates
 * or trans->delayed_ref_csum_deletions, it'll calculate the additional size and
 * add it to the delayed_refs_rsv.
 */
void btrfs_update_delayed_refs_rsv(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_block_rsv *delayed_rsv = &fs_info->delayed_refs_rsv;
	struct btrfs_block_rsv *local_rsv = &trans->delayed_rsv;
	u64 num_bytes;
	u64 reserved_bytes;

	if (btrfs_is_testing(fs_info))
		return;

	num_bytes = btrfs_calc_delayed_ref_bytes(fs_info, trans->delayed_ref_updates);
	num_bytes += btrfs_calc_delayed_ref_csum_bytes(fs_info,
						       trans->delayed_ref_csum_deletions);

	if (num_bytes == 0)
		return;

	/*
	 * Try to take num_bytes from the transaction's local delayed reserve.
	 * If not possible, try to take as much as it's available. If the local
	 * reserve doesn't have enough reserved space, the delayed refs reserve
	 * will be refilled next time btrfs_delayed_refs_rsv_refill() is called
	 * by someone or if a transaction commit is triggered before that, the
	 * global block reserve will be used. We want to minimize using the
	 * global block reserve for cases we can account for in advance, to
	 * avoid exhausting it and reach -ENOSPC during a transaction commit.
	 */
	spin_lock(&local_rsv->lock);
	reserved_bytes = min(num_bytes, local_rsv->reserved);
	local_rsv->reserved -= reserved_bytes;
	local_rsv->full = (local_rsv->reserved >= local_rsv->size);
	spin_unlock(&local_rsv->lock);

	spin_lock(&delayed_rsv->lock);
	delayed_rsv->size += num_bytes;
	delayed_rsv->reserved += reserved_bytes;
	delayed_rsv->full = (delayed_rsv->reserved >= delayed_rsv->size);
	spin_unlock(&delayed_rsv->lock);
	trans->delayed_ref_updates = 0;
	trans->delayed_ref_csum_deletions = 0;
}

/*
 * Adjust the size of the delayed refs block reserve for 1 block group item
 * insertion, used after allocating a block group.
 */
void btrfs_inc_delayed_refs_rsv_bg_inserts(struct btrfs_fs_info *fs_info)
{
	struct btrfs_block_rsv *delayed_rsv = &fs_info->delayed_refs_rsv;

	spin_lock(&delayed_rsv->lock);
	/*
	 * Inserting a block group item does not require changing the free space
	 * tree, only the extent tree or the block group tree, so this is all we
	 * need.
	 */
	delayed_rsv->size += btrfs_calc_insert_metadata_size(fs_info, 1);
	delayed_rsv->full = false;
	spin_unlock(&delayed_rsv->lock);
}

/*
 * Adjust the size of the delayed refs block reserve to release space for 1
 * block group item insertion.
 */
void btrfs_dec_delayed_refs_rsv_bg_inserts(struct btrfs_fs_info *fs_info)
{
	struct btrfs_block_rsv *delayed_rsv = &fs_info->delayed_refs_rsv;
	const u64 num_bytes = btrfs_calc_insert_metadata_size(fs_info, 1);
	u64 released;

	released = btrfs_block_rsv_release(fs_info, delayed_rsv, num_bytes, NULL);
	if (released > 0)
		trace_btrfs_space_reservation(fs_info, "delayed_refs_rsv",
					      0, released, 0);
}

/*
 * Adjust the size of the delayed refs block reserve for 1 block group item
 * update.
 */
void btrfs_inc_delayed_refs_rsv_bg_updates(struct btrfs_fs_info *fs_info)
{
	struct btrfs_block_rsv *delayed_rsv = &fs_info->delayed_refs_rsv;

	spin_lock(&delayed_rsv->lock);
	/*
	 * Updating a block group item does not result in new nodes/leaves and
	 * does not require changing the free space tree, only the extent tree
	 * or the block group tree, so this is all we need.
	 */
	delayed_rsv->size += btrfs_calc_metadata_size(fs_info, 1);
	delayed_rsv->full = false;
	spin_unlock(&delayed_rsv->lock);
}

/*
 * Adjust the size of the delayed refs block reserve to release space for 1
 * block group item update.
 */
void btrfs_dec_delayed_refs_rsv_bg_updates(struct btrfs_fs_info *fs_info)
{
	struct btrfs_block_rsv *delayed_rsv = &fs_info->delayed_refs_rsv;
	const u64 num_bytes = btrfs_calc_metadata_size(fs_info, 1);
	u64 released;

	released = btrfs_block_rsv_release(fs_info, delayed_rsv, num_bytes, NULL);
	if (released > 0)
		trace_btrfs_space_reservation(fs_info, "delayed_refs_rsv",
					      0, released, 0);
}

/*
 * Refill based on our delayed refs usage.
 *
 * @fs_info: the filesystem
 * @flush:   control how we can flush for this reservation.
 *
 * This will refill the delayed block_rsv up to 1 items size worth of space and
 * will return -ENOSPC if we can't make the reservation.
 */
int btrfs_delayed_refs_rsv_refill(struct btrfs_fs_info *fs_info,
				  enum btrfs_reserve_flush_enum flush)
{
	struct btrfs_block_rsv *block_rsv = &fs_info->delayed_refs_rsv;
	struct btrfs_space_info *space_info = block_rsv->space_info;
	u64 limit = btrfs_calc_delayed_ref_bytes(fs_info, 1);
	u64 num_bytes = 0;
	u64 refilled_bytes;
	u64 to_free;
	int ret = -ENOSPC;

	spin_lock(&block_rsv->lock);
	if (block_rsv->reserved < block_rsv->size) {
		num_bytes = block_rsv->size - block_rsv->reserved;
		num_bytes = min(num_bytes, limit);
	}
	spin_unlock(&block_rsv->lock);

	if (!num_bytes)
		return 0;

	ret = btrfs_reserve_metadata_bytes(fs_info, space_info, num_bytes, flush);
	if (ret)
		return ret;

	/*
	 * We may have raced with someone else, so check again if we the block
	 * reserve is still not full and release any excess space.
	 */
	spin_lock(&block_rsv->lock);
	if (block_rsv->reserved < block_rsv->size) {
		u64 needed = block_rsv->size - block_rsv->reserved;

		if (num_bytes >= needed) {
			block_rsv->reserved += needed;
			block_rsv->full = true;
			to_free = num_bytes - needed;
			refilled_bytes = needed;
		} else {
			block_rsv->reserved += num_bytes;
			to_free = 0;
			refilled_bytes = num_bytes;
		}
	} else {
		to_free = num_bytes;
		refilled_bytes = 0;
	}
	spin_unlock(&block_rsv->lock);

	if (to_free > 0)
		btrfs_space_info_free_bytes_may_use(space_info, to_free);

	if (refilled_bytes > 0)
		trace_btrfs_space_reservation(fs_info, "delayed_refs_rsv", 0,
					      refilled_bytes, 1);
	return 0;
}

/*
 * compare two delayed data backrefs with same bytenr and type
 */
static int comp_data_refs(const struct btrfs_delayed_ref_node *ref1,
			  const struct btrfs_delayed_ref_node *ref2)
{
	if (ref1->data_ref.objectid < ref2->data_ref.objectid)
		return -1;
	if (ref1->data_ref.objectid > ref2->data_ref.objectid)
		return 1;
	if (ref1->data_ref.offset < ref2->data_ref.offset)
		return -1;
	if (ref1->data_ref.offset > ref2->data_ref.offset)
		return 1;
	return 0;
}

static int comp_refs(const struct btrfs_delayed_ref_node *ref1,
		     const struct btrfs_delayed_ref_node *ref2,
		     bool check_seq)
{
	int ret = 0;

	if (ref1->type < ref2->type)
		return -1;
	if (ref1->type > ref2->type)
		return 1;
	if (ref1->type == BTRFS_SHARED_BLOCK_REF_KEY ||
	    ref1->type == BTRFS_SHARED_DATA_REF_KEY) {
		if (ref1->parent < ref2->parent)
			return -1;
		if (ref1->parent > ref2->parent)
			return 1;
	} else {
		if (ref1->ref_root < ref2->ref_root)
			return -1;
		if (ref1->ref_root > ref2->ref_root)
			return 1;
		if (ref1->type == BTRFS_EXTENT_DATA_REF_KEY)
			ret = comp_data_refs(ref1, ref2);
	}
	if (ret)
		return ret;
	if (check_seq) {
		if (ref1->seq < ref2->seq)
			return -1;
		if (ref1->seq > ref2->seq)
			return 1;
	}
	return 0;
}

static int cmp_refs_node(const struct rb_node *new, const struct rb_node *exist)
{
	const struct btrfs_delayed_ref_node *new_node =
		rb_entry(new, struct btrfs_delayed_ref_node, ref_node);
	const struct btrfs_delayed_ref_node *exist_node =
		rb_entry(exist, struct btrfs_delayed_ref_node, ref_node);

	return comp_refs(new_node, exist_node, true);
}

static struct btrfs_delayed_ref_node* tree_insert(struct rb_root_cached *root,
		struct btrfs_delayed_ref_node *ins)
{
	struct rb_node *node = &ins->ref_node;
	struct rb_node *exist;

	exist = rb_find_add_cached(node, root, cmp_refs_node);
	if (exist)
		return rb_entry(exist, struct btrfs_delayed_ref_node, ref_node);
	return NULL;
}

static struct btrfs_delayed_ref_head *find_first_ref_head(
		struct btrfs_delayed_ref_root *dr)
{
	unsigned long from = 0;

	lockdep_assert_held(&dr->lock);

	return xa_find(&dr->head_refs, &from, ULONG_MAX, XA_PRESENT);
}

static bool btrfs_delayed_ref_lock(struct btrfs_delayed_ref_root *delayed_refs,
				   struct btrfs_delayed_ref_head *head)
{
	lockdep_assert_held(&delayed_refs->lock);
	if (mutex_trylock(&head->mutex))
		return true;

	refcount_inc(&head->refs);
	spin_unlock(&delayed_refs->lock);

	mutex_lock(&head->mutex);
	spin_lock(&delayed_refs->lock);
	if (!head->tracked) {
		mutex_unlock(&head->mutex);
		btrfs_put_delayed_ref_head(head);
		return false;
	}
	btrfs_put_delayed_ref_head(head);
	return true;
}

static inline void drop_delayed_ref(struct btrfs_fs_info *fs_info,
				    struct btrfs_delayed_ref_root *delayed_refs,
				    struct btrfs_delayed_ref_head *head,
				    struct btrfs_delayed_ref_node *ref)
{
	lockdep_assert_held(&head->lock);
	rb_erase_cached(&ref->ref_node, &head->ref_tree);
	RB_CLEAR_NODE(&ref->ref_node);
	if (!list_empty(&ref->add_list))
		list_del(&ref->add_list);
	btrfs_put_delayed_ref(ref);
	btrfs_delayed_refs_rsv_release(fs_info, 1, 0);
}

static bool merge_ref(struct btrfs_fs_info *fs_info,
		      struct btrfs_delayed_ref_root *delayed_refs,
		      struct btrfs_delayed_ref_head *head,
		      struct btrfs_delayed_ref_node *ref,
		      u64 seq)
{
	struct btrfs_delayed_ref_node *next;
	struct rb_node *node = rb_next(&ref->ref_node);
	bool done = false;

	while (!done && node) {
		int mod;

		next = rb_entry(node, struct btrfs_delayed_ref_node, ref_node);
		node = rb_next(node);
		if (seq && next->seq >= seq)
			break;
		if (comp_refs(ref, next, false))
			break;

		if (ref->action == next->action) {
			mod = next->ref_mod;
		} else {
			if (ref->ref_mod < next->ref_mod) {
				swap(ref, next);
				done = true;
			}
			mod = -next->ref_mod;
		}

		drop_delayed_ref(fs_info, delayed_refs, head, next);
		ref->ref_mod += mod;
		if (ref->ref_mod == 0) {
			drop_delayed_ref(fs_info, delayed_refs, head, ref);
			done = true;
		} else {
			/*
			 * Can't have multiples of the same ref on a tree block.
			 */
			WARN_ON(ref->type == BTRFS_TREE_BLOCK_REF_KEY ||
				ref->type == BTRFS_SHARED_BLOCK_REF_KEY);
		}
	}

	return done;
}

void btrfs_merge_delayed_refs(struct btrfs_fs_info *fs_info,
			      struct btrfs_delayed_ref_root *delayed_refs,
			      struct btrfs_delayed_ref_head *head)
{
	struct btrfs_delayed_ref_node *ref;
	struct rb_node *node;
	u64 seq = 0;

	lockdep_assert_held(&head->lock);

	if (RB_EMPTY_ROOT(&head->ref_tree.rb_root))
		return;

	/* We don't have too many refs to merge for data. */
	if (head->is_data)
		return;

	seq = btrfs_tree_mod_log_lowest_seq(fs_info);
again:
	for (node = rb_first_cached(&head->ref_tree); node;
	     node = rb_next(node)) {
		ref = rb_entry(node, struct btrfs_delayed_ref_node, ref_node);
		if (seq && ref->seq >= seq)
			continue;
		if (merge_ref(fs_info, delayed_refs, head, ref, seq))
			goto again;
	}
}

int btrfs_check_delayed_seq(struct btrfs_fs_info *fs_info, u64 seq)
{
	int ret = 0;
	u64 min_seq = btrfs_tree_mod_log_lowest_seq(fs_info);

	if (min_seq != 0 && seq >= min_seq) {
		btrfs_debug(fs_info,
			    "holding back delayed_ref %llu, lowest is %llu",
			    seq, min_seq);
		ret = 1;
	}

	return ret;
}

struct btrfs_delayed_ref_head *btrfs_select_ref_head(
		const struct btrfs_fs_info *fs_info,
		struct btrfs_delayed_ref_root *delayed_refs)
{
	struct btrfs_delayed_ref_head *head;
	unsigned long start_index;
	unsigned long found_index;
	bool found_head = false;
	bool locked;

	spin_lock(&delayed_refs->lock);
again:
	start_index = (delayed_refs->run_delayed_start >> fs_info->sectorsize_bits);
	xa_for_each_start(&delayed_refs->head_refs, found_index, head, start_index) {
		if (!head->processing) {
			found_head = true;
			break;
		}
	}
	if (!found_head) {
		if (delayed_refs->run_delayed_start == 0) {
			spin_unlock(&delayed_refs->lock);
			return NULL;
		}
		delayed_refs->run_delayed_start = 0;
		goto again;
	}

	head->processing = true;
	WARN_ON(delayed_refs->num_heads_ready == 0);
	delayed_refs->num_heads_ready--;
	delayed_refs->run_delayed_start = head->bytenr +
		head->num_bytes;

	locked = btrfs_delayed_ref_lock(delayed_refs, head);
	spin_unlock(&delayed_refs->lock);

	/*
	 * We may have dropped the spin lock to get the head mutex lock, and
	 * that might have given someone else time to free the head.  If that's
	 * true, it has been removed from our list and we can move on.
	 */
	if (!locked)
		return ERR_PTR(-EAGAIN);

	return head;
}

void btrfs_unselect_ref_head(struct btrfs_delayed_ref_root *delayed_refs,
			     struct btrfs_delayed_ref_head *head)
{
	spin_lock(&delayed_refs->lock);
	head->processing = false;
	delayed_refs->num_heads_ready++;
	spin_unlock(&delayed_refs->lock);
	btrfs_delayed_ref_unlock(head);
}

void btrfs_delete_ref_head(const struct btrfs_fs_info *fs_info,
			   struct btrfs_delayed_ref_root *delayed_refs,
			   struct btrfs_delayed_ref_head *head)
{
	const unsigned long index = (head->bytenr >> fs_info->sectorsize_bits);

	lockdep_assert_held(&delayed_refs->lock);
	lockdep_assert_held(&head->lock);

	xa_erase(&delayed_refs->head_refs, index);
	head->tracked = false;
	delayed_refs->num_heads--;
	if (!head->processing)
		delayed_refs->num_heads_ready--;
}

struct btrfs_delayed_ref_node *btrfs_select_delayed_ref(struct btrfs_delayed_ref_head *head)
{
	struct btrfs_delayed_ref_node *ref;

	lockdep_assert_held(&head->mutex);
	lockdep_assert_held(&head->lock);

	if (RB_EMPTY_ROOT(&head->ref_tree.rb_root))
		return NULL;

	/*
	 * Select a delayed ref of type BTRFS_ADD_DELAYED_REF first.
	 * This is to prevent a ref count from going down to zero, which deletes
	 * the extent item from the extent tree, when there still are references
	 * to add, which would fail because they would not find the extent item.
	 */
	if (!list_empty(&head->ref_add_list))
		return list_first_entry(&head->ref_add_list,
					struct btrfs_delayed_ref_node, add_list);

	ref = rb_entry(rb_first_cached(&head->ref_tree),
		       struct btrfs_delayed_ref_node, ref_node);
	ASSERT(list_empty(&ref->add_list));
	return ref;
}

/*
 * Helper to insert the ref_node to the tail or merge with tail.
 *
 * Return false if the ref was inserted.
 * Return true if the ref was merged into an existing one (and therefore can be
 * freed by the caller).
 */
static bool insert_delayed_ref(struct btrfs_trans_handle *trans,
			       struct btrfs_delayed_ref_head *href,
			       struct btrfs_delayed_ref_node *ref)
{
	struct btrfs_delayed_ref_root *root = &trans->transaction->delayed_refs;
	struct btrfs_delayed_ref_node *exist;
	int mod;

	spin_lock(&href->lock);
	exist = tree_insert(&href->ref_tree, ref);
	if (!exist) {
		if (ref->action == BTRFS_ADD_DELAYED_REF)
			list_add_tail(&ref->add_list, &href->ref_add_list);
		spin_unlock(&href->lock);
		trans->delayed_ref_updates++;
		return false;
	}

	/* Now we are sure we can merge */
	if (exist->action == ref->action) {
		mod = ref->ref_mod;
	} else {
		/* Need to change action */
		if (exist->ref_mod < ref->ref_mod) {
			exist->action = ref->action;
			mod = -exist->ref_mod;
			exist->ref_mod = ref->ref_mod;
			if (ref->action == BTRFS_ADD_DELAYED_REF)
				list_add_tail(&exist->add_list,
					      &href->ref_add_list);
			else if (ref->action == BTRFS_DROP_DELAYED_REF) {
				ASSERT(!list_empty(&exist->add_list));
				list_del_init(&exist->add_list);
			} else {
				ASSERT(0);
			}
		} else
			mod = -ref->ref_mod;
	}
	exist->ref_mod += mod;

	/* remove existing tail if its ref_mod is zero */
	if (exist->ref_mod == 0)
		drop_delayed_ref(trans->fs_info, root, href, exist);
	spin_unlock(&href->lock);
	return true;
}

/*
 * helper function to update the accounting in the head ref
 * existing and update must have the same bytenr
 */
static noinline void update_existing_head_ref(struct btrfs_trans_handle *trans,
			 struct btrfs_delayed_ref_head *existing,
			 struct btrfs_delayed_ref_head *update)
{
	struct btrfs_delayed_ref_root *delayed_refs =
		&trans->transaction->delayed_refs;
	struct btrfs_fs_info *fs_info = trans->fs_info;
	int old_ref_mod;

	BUG_ON(existing->is_data != update->is_data);

	spin_lock(&existing->lock);

	/*
	 * When freeing an extent, we may not know the owning root when we
	 * first create the head_ref. However, some deref before the last deref
	 * will know it, so we just need to update the head_ref accordingly.
	 */
	if (!existing->owning_root)
		existing->owning_root = update->owning_root;

	if (update->must_insert_reserved) {
		/* if the extent was freed and then
		 * reallocated before the delayed ref
		 * entries were processed, we can end up
		 * with an existing head ref without
		 * the must_insert_reserved flag set.
		 * Set it again here
		 */
		existing->must_insert_reserved = update->must_insert_reserved;
		existing->owning_root = update->owning_root;

		/*
		 * update the num_bytes so we make sure the accounting
		 * is done correctly
		 */
		existing->num_bytes = update->num_bytes;

	}

	if (update->extent_op) {
		if (!existing->extent_op) {
			existing->extent_op = update->extent_op;
		} else {
			if (update->extent_op->update_key) {
				memcpy(&existing->extent_op->key,
				       &update->extent_op->key,
				       sizeof(update->extent_op->key));
				existing->extent_op->update_key = true;
			}
			if (update->extent_op->update_flags) {
				existing->extent_op->flags_to_set |=
					update->extent_op->flags_to_set;
				existing->extent_op->update_flags = true;
			}
			btrfs_free_delayed_extent_op(update->extent_op);
		}
	}
	/*
	 * update the reference mod on the head to reflect this new operation,
	 * only need the lock for this case cause we could be processing it
	 * currently, for refs we just added we know we're a-ok.
	 */
	old_ref_mod = existing->total_ref_mod;
	existing->ref_mod += update->ref_mod;
	existing->total_ref_mod += update->ref_mod;

	/*
	 * If we are going to from a positive ref mod to a negative or vice
	 * versa we need to make sure to adjust pending_csums accordingly.
	 * We reserve bytes for csum deletion when adding or updating a ref head
	 * see add_delayed_ref_head() for more details.
	 */
	if (existing->is_data) {
		u64 csum_leaves =
			btrfs_csum_bytes_to_leaves(fs_info,
						   existing->num_bytes);

		if (existing->total_ref_mod >= 0 && old_ref_mod < 0) {
			delayed_refs->pending_csums -= existing->num_bytes;
			btrfs_delayed_refs_rsv_release(fs_info, 0, csum_leaves);
		}
		if (existing->total_ref_mod < 0 && old_ref_mod >= 0) {
			delayed_refs->pending_csums += existing->num_bytes;
			trans->delayed_ref_csum_deletions += csum_leaves;
		}
	}

	spin_unlock(&existing->lock);
}

static void init_delayed_ref_head(struct btrfs_delayed_ref_head *head_ref,
				  struct btrfs_ref *generic_ref,
				  struct btrfs_qgroup_extent_record *qrecord,
				  u64 reserved)
{
	int count_mod = 1;
	bool must_insert_reserved = false;

	/* If reserved is provided, it must be a data extent. */
	BUG_ON(generic_ref->type != BTRFS_REF_DATA && reserved);

	switch (generic_ref->action) {
	case BTRFS_ADD_DELAYED_REF:
		/* count_mod is already set to 1. */
		break;
	case BTRFS_UPDATE_DELAYED_HEAD:
		count_mod = 0;
		break;
	case BTRFS_DROP_DELAYED_REF:
		/*
		 * The head node stores the sum of all the mods, so dropping a ref
		 * should drop the sum in the head node by one.
		 */
		count_mod = -1;
		break;
	case BTRFS_ADD_DELAYED_EXTENT:
		/*
		 * BTRFS_ADD_DELAYED_EXTENT means that we need to update the
		 * reserved accounting when the extent is finally added, or if a
		 * later modification deletes the delayed ref without ever
		 * inserting the extent into the extent allocation tree.
		 * ref->must_insert_reserved is the flag used to record that
		 * accounting mods are required.
		 *
		 * Once we record must_insert_reserved, switch the action to
		 * BTRFS_ADD_DELAYED_REF because other special casing is not
		 * required.
		 */
		must_insert_reserved = true;
		break;
	}

	refcount_set(&head_ref->refs, 1);
	head_ref->bytenr = generic_ref->bytenr;
	head_ref->num_bytes = generic_ref->num_bytes;
	head_ref->ref_mod = count_mod;
	head_ref->reserved_bytes = reserved;
	head_ref->must_insert_reserved = must_insert_reserved;
	head_ref->owning_root = generic_ref->owning_root;
	head_ref->is_data = (generic_ref->type == BTRFS_REF_DATA);
	head_ref->is_system = (generic_ref->ref_root == BTRFS_CHUNK_TREE_OBJECTID);
	head_ref->ref_tree = RB_ROOT_CACHED;
	INIT_LIST_HEAD(&head_ref->ref_add_list);
	head_ref->tracked = false;
	head_ref->processing = false;
	head_ref->total_ref_mod = count_mod;
	spin_lock_init(&head_ref->lock);
	mutex_init(&head_ref->mutex);

	/* If not metadata set an impossible level to help debugging. */
	if (generic_ref->type == BTRFS_REF_METADATA)
		head_ref->level = generic_ref->tree_ref.level;
	else
		head_ref->level = U8_MAX;

	if (qrecord) {
		if (generic_ref->ref_root && reserved) {
			qrecord->data_rsv = reserved;
			qrecord->data_rsv_refroot = generic_ref->ref_root;
		}
		qrecord->num_bytes = generic_ref->num_bytes;
		qrecord->old_roots = NULL;
	}
}

/*
 * helper function to actually insert a head node into the rbtree.
 * this does all the dirty work in terms of maintaining the correct
 * overall modification count.
 *
 * Returns an error pointer in case of an error.
 */
static noinline struct btrfs_delayed_ref_head *
add_delayed_ref_head(struct btrfs_trans_handle *trans,
		     struct btrfs_delayed_ref_head *head_ref,
		     struct btrfs_qgroup_extent_record *qrecord,
		     int action, bool *qrecord_inserted_ret)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_delayed_ref_head *existing;
	struct btrfs_delayed_ref_root *delayed_refs;
	const unsigned long index = (head_ref->bytenr >> fs_info->sectorsize_bits);
	bool qrecord_inserted = false;

	delayed_refs = &trans->transaction->delayed_refs;
	lockdep_assert_held(&delayed_refs->lock);

#if BITS_PER_LONG == 32
	if (head_ref->bytenr >= MAX_LFS_FILESIZE) {
		if (qrecord)
			xa_release(&delayed_refs->dirty_extents, index);
		btrfs_err_rl(fs_info,
"delayed ref head %llu is beyond 32bit page cache and xarray index limit",
			     head_ref->bytenr);
		btrfs_err_32bit_limit(fs_info);
		return ERR_PTR(-EOVERFLOW);
	}
#endif

	/* Record qgroup extent info if provided */
	if (qrecord) {
		int ret;

		ret = btrfs_qgroup_trace_extent_nolock(fs_info, delayed_refs, qrecord,
						       head_ref->bytenr);
		if (ret) {
			/* Clean up if insertion fails or item exists. */
			xa_release(&delayed_refs->dirty_extents, index);
			/* Caller responsible for freeing qrecord on error. */
			if (ret < 0)
				return ERR_PTR(ret);
			kfree(qrecord);
		} else {
			qrecord_inserted = true;
		}
	}

	trace_add_delayed_ref_head(fs_info, head_ref, action);

	existing = xa_load(&delayed_refs->head_refs, index);
	if (existing) {
		update_existing_head_ref(trans, existing, head_ref);
		/*
		 * we've updated the existing ref, free the newly
		 * allocated ref
		 */
		kmem_cache_free(btrfs_delayed_ref_head_cachep, head_ref);
		head_ref = existing;
	} else {
		existing = xa_store(&delayed_refs->head_refs, index, head_ref, GFP_ATOMIC);
		if (xa_is_err(existing)) {
			/* Memory was preallocated by the caller. */
			ASSERT(xa_err(existing) != -ENOMEM);
			return ERR_PTR(xa_err(existing));
		} else if (WARN_ON(existing)) {
			/*
			 * Shouldn't happen we just did a lookup before under
			 * delayed_refs->lock.
			 */
			return ERR_PTR(-EEXIST);
		}
		head_ref->tracked = true;
		/*
		 * We reserve the amount of bytes needed to delete csums when
		 * adding the ref head and not when adding individual drop refs
		 * since the csum items are deleted only after running the last
		 * delayed drop ref (the data extent's ref count drops to 0).
		 */
		if (head_ref->is_data && head_ref->ref_mod < 0) {
			delayed_refs->pending_csums += head_ref->num_bytes;
			trans->delayed_ref_csum_deletions +=
				btrfs_csum_bytes_to_leaves(fs_info, head_ref->num_bytes);
		}
		delayed_refs->num_heads++;
		delayed_refs->num_heads_ready++;
	}
	if (qrecord_inserted_ret)
		*qrecord_inserted_ret = qrecord_inserted;

	return head_ref;
}

/*
 * Initialize the structure which represents a modification to a an extent.
 *
 * @fs_info:    Internal to the mounted filesystem mount structure.
 *
 * @ref:	The structure which is going to be initialized.
 *
 * @bytenr:	The logical address of the extent for which a modification is
 *		going to be recorded.
 *
 * @num_bytes:  Size of the extent whose modification is being recorded.
 *
 * @ref_root:	The id of the root where this modification has originated, this
 *		can be either one of the well-known metadata trees or the
 *		subvolume id which references this extent.
 *
 * @action:	Can be one of BTRFS_ADD_DELAYED_REF/BTRFS_DROP_DELAYED_REF or
 *		BTRFS_ADD_DELAYED_EXTENT
 *
 * @ref_type:	Holds the type of the extent which is being recorded, can be
 *		one of BTRFS_SHARED_BLOCK_REF_KEY/BTRFS_TREE_BLOCK_REF_KEY
 *		when recording a metadata extent or BTRFS_SHARED_DATA_REF_KEY/
 *		BTRFS_EXTENT_DATA_REF_KEY when recording data extent
 */
static void init_delayed_ref_common(struct btrfs_fs_info *fs_info,
				    struct btrfs_delayed_ref_node *ref,
				    struct btrfs_ref *generic_ref)
{
	int action = generic_ref->action;
	u64 seq = 0;

	if (action == BTRFS_ADD_DELAYED_EXTENT)
		action = BTRFS_ADD_DELAYED_REF;

	if (is_fstree(generic_ref->ref_root))
		seq = atomic64_read(&fs_info->tree_mod_seq);

	refcount_set(&ref->refs, 1);
	ref->bytenr = generic_ref->bytenr;
	ref->num_bytes = generic_ref->num_bytes;
	ref->ref_mod = 1;
	ref->action = action;
	ref->seq = seq;
	ref->type = btrfs_ref_type(generic_ref);
	ref->ref_root = generic_ref->ref_root;
	ref->parent = generic_ref->parent;
	RB_CLEAR_NODE(&ref->ref_node);
	INIT_LIST_HEAD(&ref->add_list);

	if (generic_ref->type == BTRFS_REF_DATA)
		ref->data_ref = generic_ref->data_ref;
	else
		ref->tree_ref = generic_ref->tree_ref;
}

void btrfs_init_tree_ref(struct btrfs_ref *generic_ref, int level, u64 mod_root,
			 bool skip_qgroup)
{
#ifdef CONFIG_BTRFS_FS_REF_VERIFY
	/* If @real_root not set, use @root as fallback */
	generic_ref->real_root = mod_root ?: generic_ref->ref_root;
#endif
	generic_ref->tree_ref.level = level;
	generic_ref->type = BTRFS_REF_METADATA;
	if (skip_qgroup || !(is_fstree(generic_ref->ref_root) &&
			     (!mod_root || is_fstree(mod_root))))
		generic_ref->skip_qgroup = true;
	else
		generic_ref->skip_qgroup = false;

}

void btrfs_init_data_ref(struct btrfs_ref *generic_ref, u64 ino, u64 offset,
			 u64 mod_root, bool skip_qgroup)
{
#ifdef CONFIG_BTRFS_FS_REF_VERIFY
	/* If @real_root not set, use @root as fallback */
	generic_ref->real_root = mod_root ?: generic_ref->ref_root;
#endif
	generic_ref->data_ref.objectid = ino;
	generic_ref->data_ref.offset = offset;
	generic_ref->type = BTRFS_REF_DATA;
	if (skip_qgroup || !(is_fstree(generic_ref->ref_root) &&
			     (!mod_root || is_fstree(mod_root))))
		generic_ref->skip_qgroup = true;
	else
		generic_ref->skip_qgroup = false;
}

static int add_delayed_ref(struct btrfs_trans_handle *trans,
			   struct btrfs_ref *generic_ref,
			   struct btrfs_delayed_extent_op *extent_op,
			   u64 reserved)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_delayed_ref_node *node;
	struct btrfs_delayed_ref_head *head_ref;
	struct btrfs_delayed_ref_head *new_head_ref;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_qgroup_extent_record *record = NULL;
	const unsigned long index = (generic_ref->bytenr >> fs_info->sectorsize_bits);
	bool qrecord_reserved = false;
	bool qrecord_inserted;
	int action = generic_ref->action;
	bool merged;
	int ret;

	node = kmem_cache_alloc(btrfs_delayed_ref_node_cachep, GFP_NOFS);
	if (!node)
		return -ENOMEM;

	head_ref = kmem_cache_alloc(btrfs_delayed_ref_head_cachep, GFP_NOFS);
	if (!head_ref) {
		ret = -ENOMEM;
		goto free_node;
	}

	delayed_refs = &trans->transaction->delayed_refs;

	if (btrfs_qgroup_full_accounting(fs_info) && !generic_ref->skip_qgroup) {
		record = kzalloc(sizeof(*record), GFP_NOFS);
		if (!record) {
			ret = -ENOMEM;
			goto free_head_ref;
		}
		if (xa_reserve(&delayed_refs->dirty_extents, index, GFP_NOFS)) {
			ret = -ENOMEM;
			goto free_record;
		}
		qrecord_reserved = true;
	}

	ret = xa_reserve(&delayed_refs->head_refs, index, GFP_NOFS);
	if (ret) {
		if (qrecord_reserved)
			xa_release(&delayed_refs->dirty_extents, index);
		goto free_record;
	}

	init_delayed_ref_common(fs_info, node, generic_ref);
	init_delayed_ref_head(head_ref, generic_ref, record, reserved);
	head_ref->extent_op = extent_op;

	spin_lock(&delayed_refs->lock);

	/*
	 * insert both the head node and the new ref without dropping
	 * the spin lock
	 */
	new_head_ref = add_delayed_ref_head(trans, head_ref, record,
					    action, &qrecord_inserted);
	if (IS_ERR(new_head_ref)) {
		xa_release(&delayed_refs->head_refs, index);
		spin_unlock(&delayed_refs->lock);
		ret = PTR_ERR(new_head_ref);
		goto free_record;
	}
	head_ref = new_head_ref;

	merged = insert_delayed_ref(trans, head_ref, node);
	spin_unlock(&delayed_refs->lock);

	/*
	 * Need to update the delayed_refs_rsv with any changes we may have
	 * made.
	 */
	btrfs_update_delayed_refs_rsv(trans);

	if (generic_ref->type == BTRFS_REF_DATA)
		trace_add_delayed_data_ref(trans->fs_info, node);
	else
		trace_add_delayed_tree_ref(trans->fs_info, node);
	if (merged)
		kmem_cache_free(btrfs_delayed_ref_node_cachep, node);

	if (qrecord_inserted)
		return btrfs_qgroup_trace_extent_post(trans, record, generic_ref->bytenr);
	return 0;

free_record:
	kfree(record);
free_head_ref:
	kmem_cache_free(btrfs_delayed_ref_head_cachep, head_ref);
free_node:
	kmem_cache_free(btrfs_delayed_ref_node_cachep, node);
	return ret;
}

/*
 * Add a delayed tree ref. This does all of the accounting required to make sure
 * the delayed ref is eventually processed before this transaction commits.
 */
int btrfs_add_delayed_tree_ref(struct btrfs_trans_handle *trans,
			       struct btrfs_ref *generic_ref,
			       struct btrfs_delayed_extent_op *extent_op)
{
	ASSERT(generic_ref->type == BTRFS_REF_METADATA && generic_ref->action);
	return add_delayed_ref(trans, generic_ref, extent_op, 0);
}

/*
 * add a delayed data ref. it's similar to btrfs_add_delayed_tree_ref.
 */
int btrfs_add_delayed_data_ref(struct btrfs_trans_handle *trans,
			       struct btrfs_ref *generic_ref,
			       u64 reserved)
{
	ASSERT(generic_ref->type == BTRFS_REF_DATA && generic_ref->action);
	return add_delayed_ref(trans, generic_ref, NULL, reserved);
}

int btrfs_add_delayed_extent_op(struct btrfs_trans_handle *trans,
				u64 bytenr, u64 num_bytes, u8 level,
				struct btrfs_delayed_extent_op *extent_op)
{
	const unsigned long index = (bytenr >> trans->fs_info->sectorsize_bits);
	struct btrfs_delayed_ref_head *head_ref;
	struct btrfs_delayed_ref_head *head_ref_ret;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_ref generic_ref = {
		.type = BTRFS_REF_METADATA,
		.action = BTRFS_UPDATE_DELAYED_HEAD,
		.bytenr = bytenr,
		.num_bytes = num_bytes,
		.tree_ref.level = level,
	};
	int ret;

	head_ref = kmem_cache_alloc(btrfs_delayed_ref_head_cachep, GFP_NOFS);
	if (!head_ref)
		return -ENOMEM;

	init_delayed_ref_head(head_ref, &generic_ref, NULL, 0);
	head_ref->extent_op = extent_op;

	delayed_refs = &trans->transaction->delayed_refs;

	ret = xa_reserve(&delayed_refs->head_refs, index, GFP_NOFS);
	if (ret) {
		kmem_cache_free(btrfs_delayed_ref_head_cachep, head_ref);
		return ret;
	}

	spin_lock(&delayed_refs->lock);
	head_ref_ret = add_delayed_ref_head(trans, head_ref, NULL,
					    BTRFS_UPDATE_DELAYED_HEAD, NULL);
	if (IS_ERR(head_ref_ret)) {
		xa_release(&delayed_refs->head_refs, index);
		spin_unlock(&delayed_refs->lock);
		kmem_cache_free(btrfs_delayed_ref_head_cachep, head_ref);
		return PTR_ERR(head_ref_ret);
	}
	spin_unlock(&delayed_refs->lock);

	/*
	 * Need to update the delayed_refs_rsv with any changes we may have
	 * made.
	 */
	btrfs_update_delayed_refs_rsv(trans);
	return 0;
}

void btrfs_put_delayed_ref(struct btrfs_delayed_ref_node *ref)
{
	if (refcount_dec_and_test(&ref->refs)) {
		WARN_ON(!RB_EMPTY_NODE(&ref->ref_node));
		kmem_cache_free(btrfs_delayed_ref_node_cachep, ref);
	}
}

/*
 * This does a simple search for the head node for a given extent.  Returns the
 * head node if found, or NULL if not.
 */
struct btrfs_delayed_ref_head *
btrfs_find_delayed_ref_head(const struct btrfs_fs_info *fs_info,
			    struct btrfs_delayed_ref_root *delayed_refs,
			    u64 bytenr)
{
	const unsigned long index = (bytenr >> fs_info->sectorsize_bits);

	lockdep_assert_held(&delayed_refs->lock);

	return xa_load(&delayed_refs->head_refs, index);
}

static int find_comp(struct btrfs_delayed_ref_node *entry, u64 root, u64 parent)
{
	int type = parent ? BTRFS_SHARED_BLOCK_REF_KEY : BTRFS_TREE_BLOCK_REF_KEY;

	if (type < entry->type)
		return -1;
	if (type > entry->type)
		return 1;

	if (type == BTRFS_TREE_BLOCK_REF_KEY) {
		if (root < entry->ref_root)
			return -1;
		if (root > entry->ref_root)
			return 1;
	} else {
		if (parent < entry->parent)
			return -1;
		if (parent > entry->parent)
			return 1;
	}
	return 0;
}

/*
 * Check to see if a given root/parent reference is attached to the head.  This
 * only checks for BTRFS_ADD_DELAYED_REF references that match, as that
 * indicates the reference exists for the given root or parent.  This is for
 * tree blocks only.
 *
 * @head: the head of the bytenr we're searching.
 * @root: the root objectid of the reference if it is a normal reference.
 * @parent: the parent if this is a shared backref.
 */
bool btrfs_find_delayed_tree_ref(struct btrfs_delayed_ref_head *head,
				 u64 root, u64 parent)
{
	struct rb_node *node;
	bool found = false;

	lockdep_assert_held(&head->mutex);

	spin_lock(&head->lock);
	node = head->ref_tree.rb_root.rb_node;
	while (node) {
		struct btrfs_delayed_ref_node *entry;
		int ret;

		entry = rb_entry(node, struct btrfs_delayed_ref_node, ref_node);
		ret = find_comp(entry, root, parent);
		if (ret < 0) {
			node = node->rb_left;
		} else if (ret > 0) {
			node = node->rb_right;
		} else {
			/*
			 * We only want to count ADD actions, as drops mean the
			 * ref doesn't exist.
			 */
			if (entry->action == BTRFS_ADD_DELAYED_REF)
				found = true;
			break;
		}
	}
	spin_unlock(&head->lock);
	return found;
}

void btrfs_destroy_delayed_refs(struct btrfs_transaction *trans)
{
	struct btrfs_delayed_ref_root *delayed_refs = &trans->delayed_refs;
	struct btrfs_fs_info *fs_info = trans->fs_info;
	bool testing = btrfs_is_testing(fs_info);

	spin_lock(&delayed_refs->lock);
	while (true) {
		struct btrfs_delayed_ref_head *head;
		struct rb_node *n;
		bool pin_bytes = false;

		head = find_first_ref_head(delayed_refs);
		if (!head)
			break;

		if (!btrfs_delayed_ref_lock(delayed_refs, head))
			continue;

		spin_lock(&head->lock);
		while ((n = rb_first_cached(&head->ref_tree)) != NULL) {
			struct btrfs_delayed_ref_node *ref;

			ref = rb_entry(n, struct btrfs_delayed_ref_node, ref_node);
			drop_delayed_ref(fs_info, delayed_refs, head, ref);
		}
		if (head->must_insert_reserved)
			pin_bytes = true;
		btrfs_free_delayed_extent_op(head->extent_op);
		btrfs_delete_ref_head(fs_info, delayed_refs, head);
		spin_unlock(&head->lock);
		spin_unlock(&delayed_refs->lock);
		mutex_unlock(&head->mutex);

		if (!testing && pin_bytes) {
			struct btrfs_block_group *bg;

			bg = btrfs_lookup_block_group(fs_info, head->bytenr);
			if (WARN_ON_ONCE(bg == NULL)) {
				/*
				 * Unexpected and there's nothing we can do here
				 * because we are in a transaction abort path,
				 * so any errors can only be ignored or reported
				 * while attempting to cleanup all resources.
				 */
				btrfs_err(fs_info,
"block group for delayed ref at %llu was not found while destroying ref head",
					  head->bytenr);
			} else {
				spin_lock(&bg->space_info->lock);
				spin_lock(&bg->lock);
				bg->pinned += head->num_bytes;
				btrfs_space_info_update_bytes_pinned(bg->space_info,
								     head->num_bytes);
				bg->reserved -= head->num_bytes;
				bg->space_info->bytes_reserved -= head->num_bytes;
				spin_unlock(&bg->lock);
				spin_unlock(&bg->space_info->lock);

				btrfs_put_block_group(bg);
			}

			btrfs_error_unpin_extent_range(fs_info, head->bytenr,
				head->bytenr + head->num_bytes - 1);
		}
		if (!testing)
			btrfs_cleanup_ref_head_accounting(fs_info, delayed_refs, head);
		btrfs_put_delayed_ref_head(head);
		cond_resched();
		spin_lock(&delayed_refs->lock);
	}

	if (!testing)
		btrfs_qgroup_destroy_extent_records(trans);

	spin_unlock(&delayed_refs->lock);
}

void __cold btrfs_delayed_ref_exit(void)
{
	kmem_cache_destroy(btrfs_delayed_ref_head_cachep);
	kmem_cache_destroy(btrfs_delayed_ref_node_cachep);
	kmem_cache_destroy(btrfs_delayed_extent_op_cachep);
}

int __init btrfs_delayed_ref_init(void)
{
	btrfs_delayed_ref_head_cachep = KMEM_CACHE(btrfs_delayed_ref_head, 0);
	if (!btrfs_delayed_ref_head_cachep)
		goto fail;

	btrfs_delayed_ref_node_cachep = KMEM_CACHE(btrfs_delayed_ref_node, 0);
	if (!btrfs_delayed_ref_node_cachep)
		goto fail;

	btrfs_delayed_extent_op_cachep = KMEM_CACHE(btrfs_delayed_extent_op, 0);
	if (!btrfs_delayed_extent_op_cachep)
		goto fail;

	return 0;
fail:
	btrfs_delayed_ref_exit();
	return -ENOMEM;
}

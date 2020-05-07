// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2009 Oracle.  All rights reserved.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include "ctree.h"
#include "delayed-ref.h"
#include "transaction.h"
#include "qgroup.h"
#include "space-info.h"

struct kmem_cache *btrfs_delayed_ref_head_cachep;
struct kmem_cache *btrfs_delayed_tree_ref_cachep;
struct kmem_cache *btrfs_delayed_data_ref_cachep;
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

int btrfs_should_throttle_delayed_refs(struct btrfs_trans_handle *trans)
{
	u64 num_entries =
		atomic_read(&trans->transaction->delayed_refs.num_entries);
	u64 avg_runtime;
	u64 val;

	smp_mb();
	avg_runtime = trans->fs_info->avg_delayed_ref_runtime;
	val = num_entries * avg_runtime;
	if (val >= NSEC_PER_SEC)
		return 1;
	if (val >= NSEC_PER_SEC / 2)
		return 2;

	return btrfs_check_space_for_delayed_refs(trans->fs_info);
}

/**
 * btrfs_delayed_refs_rsv_release - release a ref head's reservation.
 * @fs_info - the fs_info for our fs.
 * @nr - the number of items to drop.
 *
 * This drops the delayed ref head's count from the delayed refs rsv and frees
 * any excess reservation we had.
 */
void btrfs_delayed_refs_rsv_release(struct btrfs_fs_info *fs_info, int nr)
{
	struct btrfs_block_rsv *block_rsv = &fs_info->delayed_refs_rsv;
	u64 num_bytes = btrfs_calc_insert_metadata_size(fs_info, nr);
	u64 released = 0;

	released = btrfs_block_rsv_release(fs_info, block_rsv, num_bytes, NULL);
	if (released)
		trace_btrfs_space_reservation(fs_info, "delayed_refs_rsv",
					      0, released, 0);
}

/*
 * btrfs_update_delayed_refs_rsv - adjust the size of the delayed refs rsv
 * @trans - the trans that may have generated delayed refs
 *
 * This is to be called anytime we may have adjusted trans->delayed_ref_updates,
 * it'll calculate the additional size and add it to the delayed_refs_rsv.
 */
void btrfs_update_delayed_refs_rsv(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_block_rsv *delayed_rsv = &fs_info->delayed_refs_rsv;
	u64 num_bytes;

	if (!trans->delayed_ref_updates)
		return;

	num_bytes = btrfs_calc_insert_metadata_size(fs_info,
						    trans->delayed_ref_updates);
	spin_lock(&delayed_rsv->lock);
	delayed_rsv->size += num_bytes;
	delayed_rsv->full = 0;
	spin_unlock(&delayed_rsv->lock);
	trans->delayed_ref_updates = 0;
}

/**
 * btrfs_migrate_to_delayed_refs_rsv - transfer bytes to our delayed refs rsv.
 * @fs_info - the fs info for our fs.
 * @src - the source block rsv to transfer from.
 * @num_bytes - the number of bytes to transfer.
 *
 * This transfers up to the num_bytes amount from the src rsv to the
 * delayed_refs_rsv.  Any extra bytes are returned to the space info.
 */
void btrfs_migrate_to_delayed_refs_rsv(struct btrfs_fs_info *fs_info,
				       struct btrfs_block_rsv *src,
				       u64 num_bytes)
{
	struct btrfs_block_rsv *delayed_refs_rsv = &fs_info->delayed_refs_rsv;
	u64 to_free = 0;

	spin_lock(&src->lock);
	src->reserved -= num_bytes;
	src->size -= num_bytes;
	spin_unlock(&src->lock);

	spin_lock(&delayed_refs_rsv->lock);
	if (delayed_refs_rsv->size > delayed_refs_rsv->reserved) {
		u64 delta = delayed_refs_rsv->size -
			delayed_refs_rsv->reserved;
		if (num_bytes > delta) {
			to_free = num_bytes - delta;
			num_bytes = delta;
		}
	} else {
		to_free = num_bytes;
		num_bytes = 0;
	}

	if (num_bytes)
		delayed_refs_rsv->reserved += num_bytes;
	if (delayed_refs_rsv->reserved >= delayed_refs_rsv->size)
		delayed_refs_rsv->full = 1;
	spin_unlock(&delayed_refs_rsv->lock);

	if (num_bytes)
		trace_btrfs_space_reservation(fs_info, "delayed_refs_rsv",
					      0, num_bytes, 1);
	if (to_free)
		btrfs_space_info_free_bytes_may_use(fs_info,
				delayed_refs_rsv->space_info, to_free);
}

/**
 * btrfs_delayed_refs_rsv_refill - refill based on our delayed refs usage.
 * @fs_info - the fs_info for our fs.
 * @flush - control how we can flush for this reservation.
 *
 * This will refill the delayed block_rsv up to 1 items size worth of space and
 * will return -ENOSPC if we can't make the reservation.
 */
int btrfs_delayed_refs_rsv_refill(struct btrfs_fs_info *fs_info,
				  enum btrfs_reserve_flush_enum flush)
{
	struct btrfs_block_rsv *block_rsv = &fs_info->delayed_refs_rsv;
	u64 limit = btrfs_calc_insert_metadata_size(fs_info, 1);
	u64 num_bytes = 0;
	int ret = -ENOSPC;

	spin_lock(&block_rsv->lock);
	if (block_rsv->reserved < block_rsv->size) {
		num_bytes = block_rsv->size - block_rsv->reserved;
		num_bytes = min(num_bytes, limit);
	}
	spin_unlock(&block_rsv->lock);

	if (!num_bytes)
		return 0;

	ret = btrfs_reserve_metadata_bytes(fs_info->extent_root, block_rsv,
					   num_bytes, flush);
	if (ret)
		return ret;
	btrfs_block_rsv_add_bytes(block_rsv, num_bytes, 0);
	trace_btrfs_space_reservation(fs_info, "delayed_refs_rsv",
				      0, num_bytes, 1);
	return 0;
}

/*
 * compare two delayed tree backrefs with same bytenr and type
 */
static int comp_tree_refs(struct btrfs_delayed_tree_ref *ref1,
			  struct btrfs_delayed_tree_ref *ref2)
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
static int comp_data_refs(struct btrfs_delayed_data_ref *ref1,
			  struct btrfs_delayed_data_ref *ref2)
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

static int comp_refs(struct btrfs_delayed_ref_node *ref1,
		     struct btrfs_delayed_ref_node *ref2,
		     bool check_seq)
{
	int ret = 0;

	if (ref1->type < ref2->type)
		return -1;
	if (ref1->type > ref2->type)
		return 1;
	if (ref1->type == BTRFS_TREE_BLOCK_REF_KEY ||
	    ref1->type == BTRFS_SHARED_BLOCK_REF_KEY)
		ret = comp_tree_refs(btrfs_delayed_node_to_tree_ref(ref1),
				     btrfs_delayed_node_to_tree_ref(ref2));
	else
		ret = comp_data_refs(btrfs_delayed_node_to_data_ref(ref1),
				     btrfs_delayed_node_to_data_ref(ref2));
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

/* insert a new ref to head ref rbtree */
static struct btrfs_delayed_ref_head *htree_insert(struct rb_root_cached *root,
						   struct rb_node *node)
{
	struct rb_node **p = &root->rb_root.rb_node;
	struct rb_node *parent_node = NULL;
	struct btrfs_delayed_ref_head *entry;
	struct btrfs_delayed_ref_head *ins;
	u64 bytenr;
	bool leftmost = true;

	ins = rb_entry(node, struct btrfs_delayed_ref_head, href_node);
	bytenr = ins->bytenr;
	while (*p) {
		parent_node = *p;
		entry = rb_entry(parent_node, struct btrfs_delayed_ref_head,
				 href_node);

		if (bytenr < entry->bytenr) {
			p = &(*p)->rb_left;
		} else if (bytenr > entry->bytenr) {
			p = &(*p)->rb_right;
			leftmost = false;
		} else {
			return entry;
		}
	}

	rb_link_node(node, parent_node, p);
	rb_insert_color_cached(node, root, leftmost);
	return NULL;
}

static struct btrfs_delayed_ref_node* tree_insert(struct rb_root_cached *root,
		struct btrfs_delayed_ref_node *ins)
{
	struct rb_node **p = &root->rb_root.rb_node;
	struct rb_node *node = &ins->ref_node;
	struct rb_node *parent_node = NULL;
	struct btrfs_delayed_ref_node *entry;
	bool leftmost = true;

	while (*p) {
		int comp;

		parent_node = *p;
		entry = rb_entry(parent_node, struct btrfs_delayed_ref_node,
				 ref_node);
		comp = comp_refs(ins, entry, true);
		if (comp < 0) {
			p = &(*p)->rb_left;
		} else if (comp > 0) {
			p = &(*p)->rb_right;
			leftmost = false;
		} else {
			return entry;
		}
	}

	rb_link_node(node, parent_node, p);
	rb_insert_color_cached(node, root, leftmost);
	return NULL;
}

static struct btrfs_delayed_ref_head *find_first_ref_head(
		struct btrfs_delayed_ref_root *dr)
{
	struct rb_node *n;
	struct btrfs_delayed_ref_head *entry;

	n = rb_first_cached(&dr->href_root);
	if (!n)
		return NULL;

	entry = rb_entry(n, struct btrfs_delayed_ref_head, href_node);

	return entry;
}

/*
 * Find a head entry based on bytenr. This returns the delayed ref head if it
 * was able to find one, or NULL if nothing was in that spot.  If return_bigger
 * is given, the next bigger entry is returned if no exact match is found.
 */
static struct btrfs_delayed_ref_head *find_ref_head(
		struct btrfs_delayed_ref_root *dr, u64 bytenr,
		bool return_bigger)
{
	struct rb_root *root = &dr->href_root.rb_root;
	struct rb_node *n;
	struct btrfs_delayed_ref_head *entry;

	n = root->rb_node;
	entry = NULL;
	while (n) {
		entry = rb_entry(n, struct btrfs_delayed_ref_head, href_node);

		if (bytenr < entry->bytenr)
			n = n->rb_left;
		else if (bytenr > entry->bytenr)
			n = n->rb_right;
		else
			return entry;
	}
	if (entry && return_bigger) {
		if (bytenr > entry->bytenr) {
			n = rb_next(&entry->href_node);
			if (!n)
				return NULL;
			entry = rb_entry(n, struct btrfs_delayed_ref_head,
					 href_node);
		}
		return entry;
	}
	return NULL;
}

int btrfs_delayed_ref_lock(struct btrfs_delayed_ref_root *delayed_refs,
			   struct btrfs_delayed_ref_head *head)
{
	lockdep_assert_held(&delayed_refs->lock);
	if (mutex_trylock(&head->mutex))
		return 0;

	refcount_inc(&head->refs);
	spin_unlock(&delayed_refs->lock);

	mutex_lock(&head->mutex);
	spin_lock(&delayed_refs->lock);
	if (RB_EMPTY_NODE(&head->href_node)) {
		mutex_unlock(&head->mutex);
		btrfs_put_delayed_ref_head(head);
		return -EAGAIN;
	}
	btrfs_put_delayed_ref_head(head);
	return 0;
}

static inline void drop_delayed_ref(struct btrfs_trans_handle *trans,
				    struct btrfs_delayed_ref_root *delayed_refs,
				    struct btrfs_delayed_ref_head *head,
				    struct btrfs_delayed_ref_node *ref)
{
	lockdep_assert_held(&head->lock);
	rb_erase_cached(&ref->ref_node, &head->ref_tree);
	RB_CLEAR_NODE(&ref->ref_node);
	if (!list_empty(&ref->add_list))
		list_del(&ref->add_list);
	ref->in_tree = 0;
	btrfs_put_delayed_ref(ref);
	atomic_dec(&delayed_refs->num_entries);
}

static bool merge_ref(struct btrfs_trans_handle *trans,
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

		drop_delayed_ref(trans, delayed_refs, head, next);
		ref->ref_mod += mod;
		if (ref->ref_mod == 0) {
			drop_delayed_ref(trans, delayed_refs, head, ref);
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

void btrfs_merge_delayed_refs(struct btrfs_trans_handle *trans,
			      struct btrfs_delayed_ref_root *delayed_refs,
			      struct btrfs_delayed_ref_head *head)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_delayed_ref_node *ref;
	struct rb_node *node;
	u64 seq = 0;

	lockdep_assert_held(&head->lock);

	if (RB_EMPTY_ROOT(&head->ref_tree.rb_root))
		return;

	/* We don't have too many refs to merge for data. */
	if (head->is_data)
		return;

	read_lock(&fs_info->tree_mod_log_lock);
	if (!list_empty(&fs_info->tree_mod_seq_list)) {
		struct seq_list *elem;

		elem = list_first_entry(&fs_info->tree_mod_seq_list,
					struct seq_list, list);
		seq = elem->seq;
	}
	read_unlock(&fs_info->tree_mod_log_lock);

again:
	for (node = rb_first_cached(&head->ref_tree); node;
	     node = rb_next(node)) {
		ref = rb_entry(node, struct btrfs_delayed_ref_node, ref_node);
		if (seq && ref->seq >= seq)
			continue;
		if (merge_ref(trans, delayed_refs, head, ref, seq))
			goto again;
	}
}

int btrfs_check_delayed_seq(struct btrfs_fs_info *fs_info, u64 seq)
{
	struct seq_list *elem;
	int ret = 0;

	read_lock(&fs_info->tree_mod_log_lock);
	if (!list_empty(&fs_info->tree_mod_seq_list)) {
		elem = list_first_entry(&fs_info->tree_mod_seq_list,
					struct seq_list, list);
		if (seq >= elem->seq) {
			btrfs_debug(fs_info,
				"holding back delayed_ref %#x.%x, lowest is %#x.%x",
				(u32)(seq >> 32), (u32)seq,
				(u32)(elem->seq >> 32), (u32)elem->seq);
			ret = 1;
		}
	}

	read_unlock(&fs_info->tree_mod_log_lock);
	return ret;
}

struct btrfs_delayed_ref_head *btrfs_select_ref_head(
		struct btrfs_delayed_ref_root *delayed_refs)
{
	struct btrfs_delayed_ref_head *head;

again:
	head = find_ref_head(delayed_refs, delayed_refs->run_delayed_start,
			     true);
	if (!head && delayed_refs->run_delayed_start != 0) {
		delayed_refs->run_delayed_start = 0;
		head = find_first_ref_head(delayed_refs);
	}
	if (!head)
		return NULL;

	while (head->processing) {
		struct rb_node *node;

		node = rb_next(&head->href_node);
		if (!node) {
			if (delayed_refs->run_delayed_start == 0)
				return NULL;
			delayed_refs->run_delayed_start = 0;
			goto again;
		}
		head = rb_entry(node, struct btrfs_delayed_ref_head,
				href_node);
	}

	head->processing = 1;
	WARN_ON(delayed_refs->num_heads_ready == 0);
	delayed_refs->num_heads_ready--;
	delayed_refs->run_delayed_start = head->bytenr +
		head->num_bytes;
	return head;
}

void btrfs_delete_ref_head(struct btrfs_delayed_ref_root *delayed_refs,
			   struct btrfs_delayed_ref_head *head)
{
	lockdep_assert_held(&delayed_refs->lock);
	lockdep_assert_held(&head->lock);

	rb_erase_cached(&head->href_node, &delayed_refs->href_root);
	RB_CLEAR_NODE(&head->href_node);
	atomic_dec(&delayed_refs->num_entries);
	delayed_refs->num_heads--;
	if (head->processing == 0)
		delayed_refs->num_heads_ready--;
}

/*
 * Helper to insert the ref_node to the tail or merge with tail.
 *
 * Return 0 for insert.
 * Return >0 for merge.
 */
static int insert_delayed_ref(struct btrfs_trans_handle *trans,
			      struct btrfs_delayed_ref_root *root,
			      struct btrfs_delayed_ref_head *href,
			      struct btrfs_delayed_ref_node *ref)
{
	struct btrfs_delayed_ref_node *exist;
	int mod;
	int ret = 0;

	spin_lock(&href->lock);
	exist = tree_insert(&href->ref_tree, ref);
	if (!exist)
		goto inserted;

	/* Now we are sure we can merge */
	ret = 1;
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
				list_del(&exist->add_list);
			} else {
				ASSERT(0);
			}
		} else
			mod = -ref->ref_mod;
	}
	exist->ref_mod += mod;

	/* remove existing tail if its ref_mod is zero */
	if (exist->ref_mod == 0)
		drop_delayed_ref(trans, root, href, exist);
	spin_unlock(&href->lock);
	return ret;
inserted:
	if (ref->action == BTRFS_ADD_DELAYED_REF)
		list_add_tail(&ref->add_list, &href->ref_add_list);
	atomic_inc(&root->num_entries);
	spin_unlock(&href->lock);
	return ret;
}

/*
 * helper function to update the accounting in the head ref
 * existing and update must have the same bytenr
 */
static noinline void update_existing_head_ref(struct btrfs_trans_handle *trans,
			 struct btrfs_delayed_ref_head *existing,
			 struct btrfs_delayed_ref_head *update,
			 int *old_ref_mod_ret)
{
	struct btrfs_delayed_ref_root *delayed_refs =
		&trans->transaction->delayed_refs;
	struct btrfs_fs_info *fs_info = trans->fs_info;
	int old_ref_mod;

	BUG_ON(existing->is_data != update->is_data);

	spin_lock(&existing->lock);
	if (update->must_insert_reserved) {
		/* if the extent was freed and then
		 * reallocated before the delayed ref
		 * entries were processed, we can end up
		 * with an existing head ref without
		 * the must_insert_reserved flag set.
		 * Set it again here
		 */
		existing->must_insert_reserved = update->must_insert_reserved;

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
	if (old_ref_mod_ret)
		*old_ref_mod_ret = old_ref_mod;
	existing->ref_mod += update->ref_mod;
	existing->total_ref_mod += update->ref_mod;

	/*
	 * If we are going to from a positive ref mod to a negative or vice
	 * versa we need to make sure to adjust pending_csums accordingly.
	 */
	if (existing->is_data) {
		u64 csum_leaves =
			btrfs_csum_bytes_to_leaves(fs_info,
						   existing->num_bytes);

		if (existing->total_ref_mod >= 0 && old_ref_mod < 0) {
			delayed_refs->pending_csums -= existing->num_bytes;
			btrfs_delayed_refs_rsv_release(fs_info, csum_leaves);
		}
		if (existing->total_ref_mod < 0 && old_ref_mod >= 0) {
			delayed_refs->pending_csums += existing->num_bytes;
			trans->delayed_ref_updates += csum_leaves;
		}
	}
	spin_unlock(&existing->lock);
}

static void init_delayed_ref_head(struct btrfs_delayed_ref_head *head_ref,
				  struct btrfs_qgroup_extent_record *qrecord,
				  u64 bytenr, u64 num_bytes, u64 ref_root,
				  u64 reserved, int action, bool is_data,
				  bool is_system)
{
	int count_mod = 1;
	int must_insert_reserved = 0;

	/* If reserved is provided, it must be a data extent. */
	BUG_ON(!is_data && reserved);

	/*
	 * The head node stores the sum of all the mods, so dropping a ref
	 * should drop the sum in the head node by one.
	 */
	if (action == BTRFS_UPDATE_DELAYED_HEAD)
		count_mod = 0;
	else if (action == BTRFS_DROP_DELAYED_REF)
		count_mod = -1;

	/*
	 * BTRFS_ADD_DELAYED_EXTENT means that we need to update the reserved
	 * accounting when the extent is finally added, or if a later
	 * modification deletes the delayed ref without ever inserting the
	 * extent into the extent allocation tree.  ref->must_insert_reserved
	 * is the flag used to record that accounting mods are required.
	 *
	 * Once we record must_insert_reserved, switch the action to
	 * BTRFS_ADD_DELAYED_REF because other special casing is not required.
	 */
	if (action == BTRFS_ADD_DELAYED_EXTENT)
		must_insert_reserved = 1;
	else
		must_insert_reserved = 0;

	refcount_set(&head_ref->refs, 1);
	head_ref->bytenr = bytenr;
	head_ref->num_bytes = num_bytes;
	head_ref->ref_mod = count_mod;
	head_ref->must_insert_reserved = must_insert_reserved;
	head_ref->is_data = is_data;
	head_ref->is_system = is_system;
	head_ref->ref_tree = RB_ROOT_CACHED;
	INIT_LIST_HEAD(&head_ref->ref_add_list);
	RB_CLEAR_NODE(&head_ref->href_node);
	head_ref->processing = 0;
	head_ref->total_ref_mod = count_mod;
	spin_lock_init(&head_ref->lock);
	mutex_init(&head_ref->mutex);

	if (qrecord) {
		if (ref_root && reserved) {
			qrecord->data_rsv = reserved;
			qrecord->data_rsv_refroot = ref_root;
		}
		qrecord->bytenr = bytenr;
		qrecord->num_bytes = num_bytes;
		qrecord->old_roots = NULL;
	}
}

/*
 * helper function to actually insert a head node into the rbtree.
 * this does all the dirty work in terms of maintaining the correct
 * overall modification count.
 */
static noinline struct btrfs_delayed_ref_head *
add_delayed_ref_head(struct btrfs_trans_handle *trans,
		     struct btrfs_delayed_ref_head *head_ref,
		     struct btrfs_qgroup_extent_record *qrecord,
		     int action, int *qrecord_inserted_ret,
		     int *old_ref_mod, int *new_ref_mod)
{
	struct btrfs_delayed_ref_head *existing;
	struct btrfs_delayed_ref_root *delayed_refs;
	int qrecord_inserted = 0;

	delayed_refs = &trans->transaction->delayed_refs;

	/* Record qgroup extent info if provided */
	if (qrecord) {
		if (btrfs_qgroup_trace_extent_nolock(trans->fs_info,
					delayed_refs, qrecord))
			kfree(qrecord);
		else
			qrecord_inserted = 1;
	}

	trace_add_delayed_ref_head(trans->fs_info, head_ref, action);

	existing = htree_insert(&delayed_refs->href_root,
				&head_ref->href_node);
	if (existing) {
		update_existing_head_ref(trans, existing, head_ref,
					 old_ref_mod);
		/*
		 * we've updated the existing ref, free the newly
		 * allocated ref
		 */
		kmem_cache_free(btrfs_delayed_ref_head_cachep, head_ref);
		head_ref = existing;
	} else {
		if (old_ref_mod)
			*old_ref_mod = 0;
		if (head_ref->is_data && head_ref->ref_mod < 0) {
			delayed_refs->pending_csums += head_ref->num_bytes;
			trans->delayed_ref_updates +=
				btrfs_csum_bytes_to_leaves(trans->fs_info,
							   head_ref->num_bytes);
		}
		delayed_refs->num_heads++;
		delayed_refs->num_heads_ready++;
		atomic_inc(&delayed_refs->num_entries);
		trans->delayed_ref_updates++;
	}
	if (qrecord_inserted_ret)
		*qrecord_inserted_ret = qrecord_inserted;
	if (new_ref_mod)
		*new_ref_mod = head_ref->total_ref_mod;

	return head_ref;
}

/*
 * init_delayed_ref_common - Initialize the structure which represents a
 *			     modification to a an extent.
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
				    u64 bytenr, u64 num_bytes, u64 ref_root,
				    int action, u8 ref_type)
{
	u64 seq = 0;

	if (action == BTRFS_ADD_DELAYED_EXTENT)
		action = BTRFS_ADD_DELAYED_REF;

	if (is_fstree(ref_root))
		seq = atomic64_read(&fs_info->tree_mod_seq);

	refcount_set(&ref->refs, 1);
	ref->bytenr = bytenr;
	ref->num_bytes = num_bytes;
	ref->ref_mod = 1;
	ref->action = action;
	ref->is_head = 0;
	ref->in_tree = 1;
	ref->seq = seq;
	ref->type = ref_type;
	RB_CLEAR_NODE(&ref->ref_node);
	INIT_LIST_HEAD(&ref->add_list);
}

/*
 * add a delayed tree ref.  This does all of the accounting required
 * to make sure the delayed ref is eventually processed before this
 * transaction commits.
 */
int btrfs_add_delayed_tree_ref(struct btrfs_trans_handle *trans,
			       struct btrfs_ref *generic_ref,
			       struct btrfs_delayed_extent_op *extent_op,
			       int *old_ref_mod, int *new_ref_mod)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_delayed_tree_ref *ref;
	struct btrfs_delayed_ref_head *head_ref;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_qgroup_extent_record *record = NULL;
	int qrecord_inserted;
	bool is_system;
	int action = generic_ref->action;
	int level = generic_ref->tree_ref.level;
	int ret;
	u64 bytenr = generic_ref->bytenr;
	u64 num_bytes = generic_ref->len;
	u64 parent = generic_ref->parent;
	u8 ref_type;

	is_system = (generic_ref->real_root == BTRFS_CHUNK_TREE_OBJECTID);

	ASSERT(generic_ref->type == BTRFS_REF_METADATA && generic_ref->action);
	BUG_ON(extent_op && extent_op->is_data);
	ref = kmem_cache_alloc(btrfs_delayed_tree_ref_cachep, GFP_NOFS);
	if (!ref)
		return -ENOMEM;

	head_ref = kmem_cache_alloc(btrfs_delayed_ref_head_cachep, GFP_NOFS);
	if (!head_ref) {
		kmem_cache_free(btrfs_delayed_tree_ref_cachep, ref);
		return -ENOMEM;
	}

	if (test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags) &&
	    is_fstree(generic_ref->real_root) &&
	    is_fstree(generic_ref->tree_ref.root) &&
	    !generic_ref->skip_qgroup) {
		record = kzalloc(sizeof(*record), GFP_NOFS);
		if (!record) {
			kmem_cache_free(btrfs_delayed_tree_ref_cachep, ref);
			kmem_cache_free(btrfs_delayed_ref_head_cachep, head_ref);
			return -ENOMEM;
		}
	}

	if (parent)
		ref_type = BTRFS_SHARED_BLOCK_REF_KEY;
	else
		ref_type = BTRFS_TREE_BLOCK_REF_KEY;

	init_delayed_ref_common(fs_info, &ref->node, bytenr, num_bytes,
				generic_ref->tree_ref.root, action, ref_type);
	ref->root = generic_ref->tree_ref.root;
	ref->parent = parent;
	ref->level = level;

	init_delayed_ref_head(head_ref, record, bytenr, num_bytes,
			      generic_ref->tree_ref.root, 0, action, false,
			      is_system);
	head_ref->extent_op = extent_op;

	delayed_refs = &trans->transaction->delayed_refs;
	spin_lock(&delayed_refs->lock);

	/*
	 * insert both the head node and the new ref without dropping
	 * the spin lock
	 */
	head_ref = add_delayed_ref_head(trans, head_ref, record,
					action, &qrecord_inserted,
					old_ref_mod, new_ref_mod);

	ret = insert_delayed_ref(trans, delayed_refs, head_ref, &ref->node);
	spin_unlock(&delayed_refs->lock);

	/*
	 * Need to update the delayed_refs_rsv with any changes we may have
	 * made.
	 */
	btrfs_update_delayed_refs_rsv(trans);

	trace_add_delayed_tree_ref(fs_info, &ref->node, ref,
				   action == BTRFS_ADD_DELAYED_EXTENT ?
				   BTRFS_ADD_DELAYED_REF : action);
	if (ret > 0)
		kmem_cache_free(btrfs_delayed_tree_ref_cachep, ref);

	if (qrecord_inserted)
		btrfs_qgroup_trace_extent_post(fs_info, record);

	return 0;
}

/*
 * add a delayed data ref. it's similar to btrfs_add_delayed_tree_ref.
 */
int btrfs_add_delayed_data_ref(struct btrfs_trans_handle *trans,
			       struct btrfs_ref *generic_ref,
			       u64 reserved, int *old_ref_mod,
			       int *new_ref_mod)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_delayed_data_ref *ref;
	struct btrfs_delayed_ref_head *head_ref;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_qgroup_extent_record *record = NULL;
	int qrecord_inserted;
	int action = generic_ref->action;
	int ret;
	u64 bytenr = generic_ref->bytenr;
	u64 num_bytes = generic_ref->len;
	u64 parent = generic_ref->parent;
	u64 ref_root = generic_ref->data_ref.ref_root;
	u64 owner = generic_ref->data_ref.ino;
	u64 offset = generic_ref->data_ref.offset;
	u8 ref_type;

	ASSERT(generic_ref->type == BTRFS_REF_DATA && action);
	ref = kmem_cache_alloc(btrfs_delayed_data_ref_cachep, GFP_NOFS);
	if (!ref)
		return -ENOMEM;

	if (parent)
	        ref_type = BTRFS_SHARED_DATA_REF_KEY;
	else
	        ref_type = BTRFS_EXTENT_DATA_REF_KEY;
	init_delayed_ref_common(fs_info, &ref->node, bytenr, num_bytes,
				ref_root, action, ref_type);
	ref->root = ref_root;
	ref->parent = parent;
	ref->objectid = owner;
	ref->offset = offset;


	head_ref = kmem_cache_alloc(btrfs_delayed_ref_head_cachep, GFP_NOFS);
	if (!head_ref) {
		kmem_cache_free(btrfs_delayed_data_ref_cachep, ref);
		return -ENOMEM;
	}

	if (test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags) &&
	    is_fstree(ref_root) &&
	    is_fstree(generic_ref->real_root) &&
	    !generic_ref->skip_qgroup) {
		record = kzalloc(sizeof(*record), GFP_NOFS);
		if (!record) {
			kmem_cache_free(btrfs_delayed_data_ref_cachep, ref);
			kmem_cache_free(btrfs_delayed_ref_head_cachep,
					head_ref);
			return -ENOMEM;
		}
	}

	init_delayed_ref_head(head_ref, record, bytenr, num_bytes, ref_root,
			      reserved, action, true, false);
	head_ref->extent_op = NULL;

	delayed_refs = &trans->transaction->delayed_refs;
	spin_lock(&delayed_refs->lock);

	/*
	 * insert both the head node and the new ref without dropping
	 * the spin lock
	 */
	head_ref = add_delayed_ref_head(trans, head_ref, record,
					action, &qrecord_inserted,
					old_ref_mod, new_ref_mod);

	ret = insert_delayed_ref(trans, delayed_refs, head_ref, &ref->node);
	spin_unlock(&delayed_refs->lock);

	/*
	 * Need to update the delayed_refs_rsv with any changes we may have
	 * made.
	 */
	btrfs_update_delayed_refs_rsv(trans);

	trace_add_delayed_data_ref(trans->fs_info, &ref->node, ref,
				   action == BTRFS_ADD_DELAYED_EXTENT ?
				   BTRFS_ADD_DELAYED_REF : action);
	if (ret > 0)
		kmem_cache_free(btrfs_delayed_data_ref_cachep, ref);


	if (qrecord_inserted)
		return btrfs_qgroup_trace_extent_post(fs_info, record);
	return 0;
}

int btrfs_add_delayed_extent_op(struct btrfs_trans_handle *trans,
				u64 bytenr, u64 num_bytes,
				struct btrfs_delayed_extent_op *extent_op)
{
	struct btrfs_delayed_ref_head *head_ref;
	struct btrfs_delayed_ref_root *delayed_refs;

	head_ref = kmem_cache_alloc(btrfs_delayed_ref_head_cachep, GFP_NOFS);
	if (!head_ref)
		return -ENOMEM;

	init_delayed_ref_head(head_ref, NULL, bytenr, num_bytes, 0, 0,
			      BTRFS_UPDATE_DELAYED_HEAD, extent_op->is_data,
			      false);
	head_ref->extent_op = extent_op;

	delayed_refs = &trans->transaction->delayed_refs;
	spin_lock(&delayed_refs->lock);

	add_delayed_ref_head(trans, head_ref, NULL, BTRFS_UPDATE_DELAYED_HEAD,
			     NULL, NULL, NULL);

	spin_unlock(&delayed_refs->lock);

	/*
	 * Need to update the delayed_refs_rsv with any changes we may have
	 * made.
	 */
	btrfs_update_delayed_refs_rsv(trans);
	return 0;
}

/*
 * This does a simple search for the head node for a given extent.  Returns the
 * head node if found, or NULL if not.
 */
struct btrfs_delayed_ref_head *
btrfs_find_delayed_ref_head(struct btrfs_delayed_ref_root *delayed_refs, u64 bytenr)
{
	lockdep_assert_held(&delayed_refs->lock);

	return find_ref_head(delayed_refs, bytenr, false);
}

void __cold btrfs_delayed_ref_exit(void)
{
	kmem_cache_destroy(btrfs_delayed_ref_head_cachep);
	kmem_cache_destroy(btrfs_delayed_tree_ref_cachep);
	kmem_cache_destroy(btrfs_delayed_data_ref_cachep);
	kmem_cache_destroy(btrfs_delayed_extent_op_cachep);
}

int __init btrfs_delayed_ref_init(void)
{
	btrfs_delayed_ref_head_cachep = kmem_cache_create(
				"btrfs_delayed_ref_head",
				sizeof(struct btrfs_delayed_ref_head), 0,
				SLAB_MEM_SPREAD, NULL);
	if (!btrfs_delayed_ref_head_cachep)
		goto fail;

	btrfs_delayed_tree_ref_cachep = kmem_cache_create(
				"btrfs_delayed_tree_ref",
				sizeof(struct btrfs_delayed_tree_ref), 0,
				SLAB_MEM_SPREAD, NULL);
	if (!btrfs_delayed_tree_ref_cachep)
		goto fail;

	btrfs_delayed_data_ref_cachep = kmem_cache_create(
				"btrfs_delayed_data_ref",
				sizeof(struct btrfs_delayed_data_ref), 0,
				SLAB_MEM_SPREAD, NULL);
	if (!btrfs_delayed_data_ref_cachep)
		goto fail;

	btrfs_delayed_extent_op_cachep = kmem_cache_create(
				"btrfs_delayed_extent_op",
				sizeof(struct btrfs_delayed_extent_op), 0,
				SLAB_MEM_SPREAD, NULL);
	if (!btrfs_delayed_extent_op_cachep)
		goto fail;

	return 0;
fail:
	btrfs_delayed_ref_exit();
	return -ENOMEM;
}

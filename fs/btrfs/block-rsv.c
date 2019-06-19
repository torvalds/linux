// SPDX-License-Identifier: GPL-2.0

#include "ctree.h"
#include "block-rsv.h"
#include "space-info.h"
#include "math.h"
#include "transaction.h"

static u64 block_rsv_release_bytes(struct btrfs_fs_info *fs_info,
				    struct btrfs_block_rsv *block_rsv,
				    struct btrfs_block_rsv *dest, u64 num_bytes,
				    u64 *qgroup_to_release_ret)
{
	struct btrfs_space_info *space_info = block_rsv->space_info;
	u64 qgroup_to_release = 0;
	u64 ret;

	spin_lock(&block_rsv->lock);
	if (num_bytes == (u64)-1) {
		num_bytes = block_rsv->size;
		qgroup_to_release = block_rsv->qgroup_rsv_size;
	}
	block_rsv->size -= num_bytes;
	if (block_rsv->reserved >= block_rsv->size) {
		num_bytes = block_rsv->reserved - block_rsv->size;
		block_rsv->reserved = block_rsv->size;
		block_rsv->full = 1;
	} else {
		num_bytes = 0;
	}
	if (block_rsv->qgroup_rsv_reserved >= block_rsv->qgroup_rsv_size) {
		qgroup_to_release = block_rsv->qgroup_rsv_reserved -
				    block_rsv->qgroup_rsv_size;
		block_rsv->qgroup_rsv_reserved = block_rsv->qgroup_rsv_size;
	} else {
		qgroup_to_release = 0;
	}
	spin_unlock(&block_rsv->lock);

	ret = num_bytes;
	if (num_bytes > 0) {
		if (dest) {
			spin_lock(&dest->lock);
			if (!dest->full) {
				u64 bytes_to_add;

				bytes_to_add = dest->size - dest->reserved;
				bytes_to_add = min(num_bytes, bytes_to_add);
				dest->reserved += bytes_to_add;
				if (dest->reserved >= dest->size)
					dest->full = 1;
				num_bytes -= bytes_to_add;
			}
			spin_unlock(&dest->lock);
		}
		if (num_bytes)
			btrfs_space_info_add_old_bytes(fs_info, space_info,
						       num_bytes);
	}
	if (qgroup_to_release_ret)
		*qgroup_to_release_ret = qgroup_to_release;
	return ret;
}

int btrfs_block_rsv_migrate(struct btrfs_block_rsv *src,
			    struct btrfs_block_rsv *dst, u64 num_bytes,
			    bool update_size)
{
	int ret;

	ret = btrfs_block_rsv_use_bytes(src, num_bytes);
	if (ret)
		return ret;

	btrfs_block_rsv_add_bytes(dst, num_bytes, update_size);
	return 0;
}

void btrfs_init_block_rsv(struct btrfs_block_rsv *rsv, unsigned short type)
{
	memset(rsv, 0, sizeof(*rsv));
	spin_lock_init(&rsv->lock);
	rsv->type = type;
}

void btrfs_init_metadata_block_rsv(struct btrfs_fs_info *fs_info,
				   struct btrfs_block_rsv *rsv,
				   unsigned short type)
{
	btrfs_init_block_rsv(rsv, type);
	rsv->space_info = btrfs_find_space_info(fs_info,
					    BTRFS_BLOCK_GROUP_METADATA);
}

struct btrfs_block_rsv *btrfs_alloc_block_rsv(struct btrfs_fs_info *fs_info,
					      unsigned short type)
{
	struct btrfs_block_rsv *block_rsv;

	block_rsv = kmalloc(sizeof(*block_rsv), GFP_NOFS);
	if (!block_rsv)
		return NULL;

	btrfs_init_metadata_block_rsv(fs_info, block_rsv, type);
	return block_rsv;
}

void btrfs_free_block_rsv(struct btrfs_fs_info *fs_info,
			  struct btrfs_block_rsv *rsv)
{
	if (!rsv)
		return;
	btrfs_block_rsv_release(fs_info, rsv, (u64)-1);
	kfree(rsv);
}

int btrfs_block_rsv_add(struct btrfs_root *root,
			struct btrfs_block_rsv *block_rsv, u64 num_bytes,
			enum btrfs_reserve_flush_enum flush)
{
	int ret;

	if (num_bytes == 0)
		return 0;

	ret = btrfs_reserve_metadata_bytes(root, block_rsv, num_bytes, flush);
	if (!ret)
		btrfs_block_rsv_add_bytes(block_rsv, num_bytes, true);

	return ret;
}

int btrfs_block_rsv_check(struct btrfs_block_rsv *block_rsv, int min_factor)
{
	u64 num_bytes = 0;
	int ret = -ENOSPC;

	if (!block_rsv)
		return 0;

	spin_lock(&block_rsv->lock);
	num_bytes = div_factor(block_rsv->size, min_factor);
	if (block_rsv->reserved >= num_bytes)
		ret = 0;
	spin_unlock(&block_rsv->lock);

	return ret;
}

int btrfs_block_rsv_refill(struct btrfs_root *root,
			   struct btrfs_block_rsv *block_rsv, u64 min_reserved,
			   enum btrfs_reserve_flush_enum flush)
{
	u64 num_bytes = 0;
	int ret = -ENOSPC;

	if (!block_rsv)
		return 0;

	spin_lock(&block_rsv->lock);
	num_bytes = min_reserved;
	if (block_rsv->reserved >= num_bytes)
		ret = 0;
	else
		num_bytes -= block_rsv->reserved;
	spin_unlock(&block_rsv->lock);

	if (!ret)
		return 0;

	ret = btrfs_reserve_metadata_bytes(root, block_rsv, num_bytes, flush);
	if (!ret) {
		btrfs_block_rsv_add_bytes(block_rsv, num_bytes, false);
		return 0;
	}

	return ret;
}

u64 __btrfs_block_rsv_release(struct btrfs_fs_info *fs_info,
			      struct btrfs_block_rsv *block_rsv,
			      u64 num_bytes, u64 *qgroup_to_release)
{
	struct btrfs_block_rsv *global_rsv = &fs_info->global_block_rsv;
	struct btrfs_block_rsv *delayed_rsv = &fs_info->delayed_refs_rsv;
	struct btrfs_block_rsv *target = NULL;

	/*
	 * If we are the delayed_rsv then push to the global rsv, otherwise dump
	 * into the delayed rsv if it is not full.
	 */
	if (block_rsv == delayed_rsv)
		target = global_rsv;
	else if (block_rsv != global_rsv && !delayed_rsv->full)
		target = delayed_rsv;

	if (target && block_rsv->space_info != target->space_info)
		target = NULL;

	return block_rsv_release_bytes(fs_info, block_rsv, target, num_bytes,
				       qgroup_to_release);
}

int btrfs_block_rsv_use_bytes(struct btrfs_block_rsv *block_rsv, u64 num_bytes)
{
	int ret = -ENOSPC;

	spin_lock(&block_rsv->lock);
	if (block_rsv->reserved >= num_bytes) {
		block_rsv->reserved -= num_bytes;
		if (block_rsv->reserved < block_rsv->size)
			block_rsv->full = 0;
		ret = 0;
	}
	spin_unlock(&block_rsv->lock);
	return ret;
}

void btrfs_block_rsv_add_bytes(struct btrfs_block_rsv *block_rsv,
			       u64 num_bytes, bool update_size)
{
	spin_lock(&block_rsv->lock);
	block_rsv->reserved += num_bytes;
	if (update_size)
		block_rsv->size += num_bytes;
	else if (block_rsv->reserved >= block_rsv->size)
		block_rsv->full = 1;
	spin_unlock(&block_rsv->lock);
}

int btrfs_cond_migrate_bytes(struct btrfs_fs_info *fs_info,
			     struct btrfs_block_rsv *dest, u64 num_bytes,
			     int min_factor)
{
	struct btrfs_block_rsv *global_rsv = &fs_info->global_block_rsv;
	u64 min_bytes;

	if (global_rsv->space_info != dest->space_info)
		return -ENOSPC;

	spin_lock(&global_rsv->lock);
	min_bytes = div_factor(global_rsv->size, min_factor);
	if (global_rsv->reserved < min_bytes + num_bytes) {
		spin_unlock(&global_rsv->lock);
		return -ENOSPC;
	}
	global_rsv->reserved -= num_bytes;
	if (global_rsv->reserved < global_rsv->size)
		global_rsv->full = 0;
	spin_unlock(&global_rsv->lock);

	btrfs_block_rsv_add_bytes(dest, num_bytes, true);
	return 0;
}

void btrfs_update_global_block_rsv(struct btrfs_fs_info *fs_info)
{
	struct btrfs_block_rsv *block_rsv = &fs_info->global_block_rsv;
	struct btrfs_space_info *sinfo = block_rsv->space_info;
	u64 num_bytes;

	/*
	 * The global block rsv is based on the size of the extent tree, the
	 * checksum tree and the root tree.  If the fs is empty we want to set
	 * it to a minimal amount for safety.
	 */
	num_bytes = btrfs_root_used(&fs_info->extent_root->root_item) +
		btrfs_root_used(&fs_info->csum_root->root_item) +
		btrfs_root_used(&fs_info->tree_root->root_item);
	num_bytes = max_t(u64, num_bytes, SZ_16M);

	spin_lock(&sinfo->lock);
	spin_lock(&block_rsv->lock);

	block_rsv->size = min_t(u64, num_bytes, SZ_512M);

	if (block_rsv->reserved < block_rsv->size) {
		num_bytes = btrfs_space_info_used(sinfo, true);
		if (sinfo->total_bytes > num_bytes) {
			num_bytes = sinfo->total_bytes - num_bytes;
			num_bytes = min(num_bytes,
					block_rsv->size - block_rsv->reserved);
			block_rsv->reserved += num_bytes;
			btrfs_space_info_update_bytes_may_use(fs_info, sinfo,
							      num_bytes);
			trace_btrfs_space_reservation(fs_info, "space_info",
						      sinfo->flags, num_bytes,
						      1);
		}
	} else if (block_rsv->reserved > block_rsv->size) {
		num_bytes = block_rsv->reserved - block_rsv->size;
		btrfs_space_info_update_bytes_may_use(fs_info, sinfo,
						      -num_bytes);
		trace_btrfs_space_reservation(fs_info, "space_info",
				      sinfo->flags, num_bytes, 0);
		block_rsv->reserved = block_rsv->size;
	}

	if (block_rsv->reserved == block_rsv->size)
		block_rsv->full = 1;
	else
		block_rsv->full = 0;

	spin_unlock(&block_rsv->lock);
	spin_unlock(&sinfo->lock);
}

void btrfs_init_global_block_rsv(struct btrfs_fs_info *fs_info)
{
	struct btrfs_space_info *space_info;

	space_info = btrfs_find_space_info(fs_info, BTRFS_BLOCK_GROUP_SYSTEM);
	fs_info->chunk_block_rsv.space_info = space_info;

	space_info = btrfs_find_space_info(fs_info, BTRFS_BLOCK_GROUP_METADATA);
	fs_info->global_block_rsv.space_info = space_info;
	fs_info->trans_block_rsv.space_info = space_info;
	fs_info->empty_block_rsv.space_info = space_info;
	fs_info->delayed_block_rsv.space_info = space_info;
	fs_info->delayed_refs_rsv.space_info = space_info;

	fs_info->extent_root->block_rsv = &fs_info->delayed_refs_rsv;
	fs_info->csum_root->block_rsv = &fs_info->delayed_refs_rsv;
	fs_info->dev_root->block_rsv = &fs_info->global_block_rsv;
	fs_info->tree_root->block_rsv = &fs_info->global_block_rsv;
	if (fs_info->quota_root)
		fs_info->quota_root->block_rsv = &fs_info->global_block_rsv;
	fs_info->chunk_root->block_rsv = &fs_info->chunk_block_rsv;

	btrfs_update_global_block_rsv(fs_info);
}

void btrfs_release_global_block_rsv(struct btrfs_fs_info *fs_info)
{
	btrfs_block_rsv_release(fs_info, &fs_info->global_block_rsv, (u64)-1);
	WARN_ON(fs_info->trans_block_rsv.size > 0);
	WARN_ON(fs_info->trans_block_rsv.reserved > 0);
	WARN_ON(fs_info->chunk_block_rsv.size > 0);
	WARN_ON(fs_info->chunk_block_rsv.reserved > 0);
	WARN_ON(fs_info->delayed_block_rsv.size > 0);
	WARN_ON(fs_info->delayed_block_rsv.reserved > 0);
	WARN_ON(fs_info->delayed_refs_rsv.reserved > 0);
	WARN_ON(fs_info->delayed_refs_rsv.size > 0);
}

static struct btrfs_block_rsv *get_block_rsv(
					const struct btrfs_trans_handle *trans,
					const struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_block_rsv *block_rsv = NULL;

	if (test_bit(BTRFS_ROOT_REF_COWS, &root->state) ||
	    (root == fs_info->csum_root && trans->adding_csums) ||
	    (root == fs_info->uuid_root))
		block_rsv = trans->block_rsv;

	if (!block_rsv)
		block_rsv = root->block_rsv;

	if (!block_rsv)
		block_rsv = &fs_info->empty_block_rsv;

	return block_rsv;
}

struct btrfs_block_rsv *btrfs_use_block_rsv(struct btrfs_trans_handle *trans,
					    struct btrfs_root *root,
					    u32 blocksize)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_block_rsv *block_rsv;
	struct btrfs_block_rsv *global_rsv = &fs_info->global_block_rsv;
	int ret;
	bool global_updated = false;

	block_rsv = get_block_rsv(trans, root);

	if (unlikely(block_rsv->size == 0))
		goto try_reserve;
again:
	ret = btrfs_block_rsv_use_bytes(block_rsv, blocksize);
	if (!ret)
		return block_rsv;

	if (block_rsv->failfast)
		return ERR_PTR(ret);

	if (block_rsv->type == BTRFS_BLOCK_RSV_GLOBAL && !global_updated) {
		global_updated = true;
		btrfs_update_global_block_rsv(fs_info);
		goto again;
	}

	/*
	 * The global reserve still exists to save us from ourselves, so don't
	 * warn_on if we are short on our delayed refs reserve.
	 */
	if (block_rsv->type != BTRFS_BLOCK_RSV_DELREFS &&
	    btrfs_test_opt(fs_info, ENOSPC_DEBUG)) {
		static DEFINE_RATELIMIT_STATE(_rs,
				DEFAULT_RATELIMIT_INTERVAL * 10,
				/*DEFAULT_RATELIMIT_BURST*/ 1);
		if (__ratelimit(&_rs))
			WARN(1, KERN_DEBUG
				"BTRFS: block rsv returned %d\n", ret);
	}
try_reserve:
	ret = btrfs_reserve_metadata_bytes(root, block_rsv, blocksize,
					   BTRFS_RESERVE_NO_FLUSH);
	if (!ret)
		return block_rsv;
	/*
	 * If we couldn't reserve metadata bytes try and use some from
	 * the global reserve if its space type is the same as the global
	 * reservation.
	 */
	if (block_rsv->type != BTRFS_BLOCK_RSV_GLOBAL &&
	    block_rsv->space_info == global_rsv->space_info) {
		ret = btrfs_block_rsv_use_bytes(global_rsv, blocksize);
		if (!ret)
			return global_rsv;
	}
	return ERR_PTR(ret);
}

// SPDX-License-Identifier: GPL-2.0

#include "ctree.h"
#include "space-info.h"
#include "sysfs.h"
#include "volumes.h"

u64 btrfs_space_info_used(struct btrfs_space_info *s_info,
			  bool may_use_included)
{
	ASSERT(s_info);
	return s_info->bytes_used + s_info->bytes_reserved +
		s_info->bytes_pinned + s_info->bytes_readonly +
		(may_use_included ? s_info->bytes_may_use : 0);
}

/*
 * after adding space to the filesystem, we need to clear the full flags
 * on all the space infos.
 */
void btrfs_clear_space_info_full(struct btrfs_fs_info *info)
{
	struct list_head *head = &info->space_info;
	struct btrfs_space_info *found;

	rcu_read_lock();
	list_for_each_entry_rcu(found, head, list)
		found->full = 0;
	rcu_read_unlock();
}

static const char *alloc_name(u64 flags)
{
	switch (flags) {
	case BTRFS_BLOCK_GROUP_METADATA|BTRFS_BLOCK_GROUP_DATA:
		return "mixed";
	case BTRFS_BLOCK_GROUP_METADATA:
		return "metadata";
	case BTRFS_BLOCK_GROUP_DATA:
		return "data";
	case BTRFS_BLOCK_GROUP_SYSTEM:
		return "system";
	default:
		WARN_ON(1);
		return "invalid-combination";
	};
}

static int create_space_info(struct btrfs_fs_info *info, u64 flags)
{

	struct btrfs_space_info *space_info;
	int i;
	int ret;

	space_info = kzalloc(sizeof(*space_info), GFP_NOFS);
	if (!space_info)
		return -ENOMEM;

	ret = percpu_counter_init(&space_info->total_bytes_pinned, 0,
				 GFP_KERNEL);
	if (ret) {
		kfree(space_info);
		return ret;
	}

	for (i = 0; i < BTRFS_NR_RAID_TYPES; i++)
		INIT_LIST_HEAD(&space_info->block_groups[i]);
	init_rwsem(&space_info->groups_sem);
	spin_lock_init(&space_info->lock);
	space_info->flags = flags & BTRFS_BLOCK_GROUP_TYPE_MASK;
	space_info->force_alloc = CHUNK_ALLOC_NO_FORCE;
	init_waitqueue_head(&space_info->wait);
	INIT_LIST_HEAD(&space_info->ro_bgs);
	INIT_LIST_HEAD(&space_info->tickets);
	INIT_LIST_HEAD(&space_info->priority_tickets);

	ret = kobject_init_and_add(&space_info->kobj, &space_info_ktype,
				    info->space_info_kobj, "%s",
				    alloc_name(space_info->flags));
	if (ret) {
		kobject_put(&space_info->kobj);
		return ret;
	}

	list_add_rcu(&space_info->list, &info->space_info);
	if (flags & BTRFS_BLOCK_GROUP_DATA)
		info->data_sinfo = space_info;

	return ret;
}

int btrfs_init_space_info(struct btrfs_fs_info *fs_info)
{
	struct btrfs_super_block *disk_super;
	u64 features;
	u64 flags;
	int mixed = 0;
	int ret;

	disk_super = fs_info->super_copy;
	if (!btrfs_super_root(disk_super))
		return -EINVAL;

	features = btrfs_super_incompat_flags(disk_super);
	if (features & BTRFS_FEATURE_INCOMPAT_MIXED_GROUPS)
		mixed = 1;

	flags = BTRFS_BLOCK_GROUP_SYSTEM;
	ret = create_space_info(fs_info, flags);
	if (ret)
		goto out;

	if (mixed) {
		flags = BTRFS_BLOCK_GROUP_METADATA | BTRFS_BLOCK_GROUP_DATA;
		ret = create_space_info(fs_info, flags);
	} else {
		flags = BTRFS_BLOCK_GROUP_METADATA;
		ret = create_space_info(fs_info, flags);
		if (ret)
			goto out;

		flags = BTRFS_BLOCK_GROUP_DATA;
		ret = create_space_info(fs_info, flags);
	}
out:
	return ret;
}

void btrfs_update_space_info(struct btrfs_fs_info *info, u64 flags,
			     u64 total_bytes, u64 bytes_used,
			     u64 bytes_readonly,
			     struct btrfs_space_info **space_info)
{
	struct btrfs_space_info *found;
	int factor;

	factor = btrfs_bg_type_to_factor(flags);

	found = btrfs_find_space_info(info, flags);
	ASSERT(found);
	spin_lock(&found->lock);
	found->total_bytes += total_bytes;
	found->disk_total += total_bytes * factor;
	found->bytes_used += bytes_used;
	found->disk_used += bytes_used * factor;
	found->bytes_readonly += bytes_readonly;
	if (total_bytes > 0)
		found->full = 0;
	btrfs_space_info_add_new_bytes(info, found,
				       total_bytes - bytes_used -
				       bytes_readonly);
	spin_unlock(&found->lock);
	*space_info = found;
}

struct btrfs_space_info *btrfs_find_space_info(struct btrfs_fs_info *info,
					       u64 flags)
{
	struct list_head *head = &info->space_info;
	struct btrfs_space_info *found;

	flags &= BTRFS_BLOCK_GROUP_TYPE_MASK;

	rcu_read_lock();
	list_for_each_entry_rcu(found, head, list) {
		if (found->flags & flags) {
			rcu_read_unlock();
			return found;
		}
	}
	rcu_read_unlock();
	return NULL;
}

static inline u64 calc_global_rsv_need_space(struct btrfs_block_rsv *global)
{
	return (global->size << 1);
}

int btrfs_can_overcommit(struct btrfs_fs_info *fs_info,
			 struct btrfs_space_info *space_info, u64 bytes,
			 enum btrfs_reserve_flush_enum flush,
			 bool system_chunk)
{
	struct btrfs_block_rsv *global_rsv = &fs_info->global_block_rsv;
	u64 profile;
	u64 space_size;
	u64 avail;
	u64 used;
	int factor;

	/* Don't overcommit when in mixed mode. */
	if (space_info->flags & BTRFS_BLOCK_GROUP_DATA)
		return 0;

	if (system_chunk)
		profile = btrfs_system_alloc_profile(fs_info);
	else
		profile = btrfs_metadata_alloc_profile(fs_info);

	used = btrfs_space_info_used(space_info, false);

	/*
	 * We only want to allow over committing if we have lots of actual space
	 * free, but if we don't have enough space to handle the global reserve
	 * space then we could end up having a real enospc problem when trying
	 * to allocate a chunk or some other such important allocation.
	 */
	spin_lock(&global_rsv->lock);
	space_size = calc_global_rsv_need_space(global_rsv);
	spin_unlock(&global_rsv->lock);
	if (used + space_size >= space_info->total_bytes)
		return 0;

	used += space_info->bytes_may_use;

	avail = atomic64_read(&fs_info->free_chunk_space);

	/*
	 * If we have dup, raid1 or raid10 then only half of the free
	 * space is actually usable.  For raid56, the space info used
	 * doesn't include the parity drive, so we don't have to
	 * change the math
	 */
	factor = btrfs_bg_type_to_factor(profile);
	avail = div_u64(avail, factor);

	/*
	 * If we aren't flushing all things, let us overcommit up to
	 * 1/2th of the space. If we can flush, don't let us overcommit
	 * too much, let it overcommit up to 1/8 of the space.
	 */
	if (flush == BTRFS_RESERVE_FLUSH_ALL)
		avail >>= 3;
	else
		avail >>= 1;

	if (used + bytes < space_info->total_bytes + avail)
		return 1;
	return 0;
}

/*
 * This is for space we already have accounted in space_info->bytes_may_use, so
 * basically when we're returning space from block_rsv's.
 */
void btrfs_space_info_add_old_bytes(struct btrfs_fs_info *fs_info,
				    struct btrfs_space_info *space_info,
				    u64 num_bytes)
{
	struct reserve_ticket *ticket;
	struct list_head *head;
	u64 used;
	enum btrfs_reserve_flush_enum flush = BTRFS_RESERVE_NO_FLUSH;
	bool check_overcommit = false;

	spin_lock(&space_info->lock);
	head = &space_info->priority_tickets;

	/*
	 * If we are over our limit then we need to check and see if we can
	 * overcommit, and if we can't then we just need to free up our space
	 * and not satisfy any requests.
	 */
	used = btrfs_space_info_used(space_info, true);
	if (used - num_bytes >= space_info->total_bytes)
		check_overcommit = true;
again:
	while (!list_empty(head) && num_bytes) {
		ticket = list_first_entry(head, struct reserve_ticket,
					  list);
		/*
		 * We use 0 bytes because this space is already reserved, so
		 * adding the ticket space would be a double count.
		 */
		if (check_overcommit &&
		    !btrfs_can_overcommit(fs_info, space_info, 0, flush,
					  false))
			break;
		if (num_bytes >= ticket->bytes) {
			list_del_init(&ticket->list);
			num_bytes -= ticket->bytes;
			ticket->bytes = 0;
			space_info->tickets_id++;
			wake_up(&ticket->wait);
		} else {
			ticket->bytes -= num_bytes;
			num_bytes = 0;
		}
	}

	if (num_bytes && head == &space_info->priority_tickets) {
		head = &space_info->tickets;
		flush = BTRFS_RESERVE_FLUSH_ALL;
		goto again;
	}
	btrfs_space_info_update_bytes_may_use(fs_info, space_info, -num_bytes);
	trace_btrfs_space_reservation(fs_info, "space_info",
				      space_info->flags, num_bytes, 0);
	spin_unlock(&space_info->lock);
}

/*
 * This is for newly allocated space that isn't accounted in
 * space_info->bytes_may_use yet.  So if we allocate a chunk or unpin an extent
 * we use this helper.
 */
void btrfs_space_info_add_new_bytes(struct btrfs_fs_info *fs_info,
				    struct btrfs_space_info *space_info,
				    u64 num_bytes)
{
	struct reserve_ticket *ticket;
	struct list_head *head = &space_info->priority_tickets;

again:
	while (!list_empty(head) && num_bytes) {
		ticket = list_first_entry(head, struct reserve_ticket,
					  list);
		if (num_bytes >= ticket->bytes) {
			trace_btrfs_space_reservation(fs_info, "space_info",
						      space_info->flags,
						      ticket->bytes, 1);
			list_del_init(&ticket->list);
			num_bytes -= ticket->bytes;
			btrfs_space_info_update_bytes_may_use(fs_info,
							      space_info,
							      ticket->bytes);
			ticket->bytes = 0;
			space_info->tickets_id++;
			wake_up(&ticket->wait);
		} else {
			trace_btrfs_space_reservation(fs_info, "space_info",
						      space_info->flags,
						      num_bytes, 1);
			btrfs_space_info_update_bytes_may_use(fs_info,
							      space_info,
							      num_bytes);
			ticket->bytes -= num_bytes;
			num_bytes = 0;
		}
	}

	if (num_bytes && head == &space_info->priority_tickets) {
		head = &space_info->tickets;
		goto again;
	}
}

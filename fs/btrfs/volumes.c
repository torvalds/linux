// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <linux/sched.h>
#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/ratelimit.h>
#include <linux/kthread.h>
#include <linux/raid/pq.h>
#include <linux/semaphore.h>
#include <linux/uuid.h>
#include <linux/list_sort.h>
#include "misc.h"
#include "ctree.h"
#include "extent_map.h"
#include "disk-io.h"
#include "transaction.h"
#include "print-tree.h"
#include "volumes.h"
#include "raid56.h"
#include "async-thread.h"
#include "check-integrity.h"
#include "rcu-string.h"
#include "dev-replace.h"
#include "sysfs.h"
#include "tree-checker.h"
#include "space-info.h"
#include "block-group.h"

const struct btrfs_raid_attr btrfs_raid_array[BTRFS_NR_RAID_TYPES] = {
	[BTRFS_RAID_RAID10] = {
		.sub_stripes	= 2,
		.dev_stripes	= 1,
		.devs_max	= 0,	/* 0 == as many as possible */
		.devs_min	= 4,
		.tolerated_failures = 1,
		.devs_increment	= 2,
		.ncopies	= 2,
		.nparity        = 0,
		.raid_name	= "raid10",
		.bg_flag	= BTRFS_BLOCK_GROUP_RAID10,
		.mindev_error	= BTRFS_ERROR_DEV_RAID10_MIN_NOT_MET,
	},
	[BTRFS_RAID_RAID1] = {
		.sub_stripes	= 1,
		.dev_stripes	= 1,
		.devs_max	= 2,
		.devs_min	= 2,
		.tolerated_failures = 1,
		.devs_increment	= 2,
		.ncopies	= 2,
		.nparity        = 0,
		.raid_name	= "raid1",
		.bg_flag	= BTRFS_BLOCK_GROUP_RAID1,
		.mindev_error	= BTRFS_ERROR_DEV_RAID1_MIN_NOT_MET,
	},
	[BTRFS_RAID_DUP] = {
		.sub_stripes	= 1,
		.dev_stripes	= 2,
		.devs_max	= 1,
		.devs_min	= 1,
		.tolerated_failures = 0,
		.devs_increment	= 1,
		.ncopies	= 2,
		.nparity        = 0,
		.raid_name	= "dup",
		.bg_flag	= BTRFS_BLOCK_GROUP_DUP,
		.mindev_error	= 0,
	},
	[BTRFS_RAID_RAID0] = {
		.sub_stripes	= 1,
		.dev_stripes	= 1,
		.devs_max	= 0,
		.devs_min	= 2,
		.tolerated_failures = 0,
		.devs_increment	= 1,
		.ncopies	= 1,
		.nparity        = 0,
		.raid_name	= "raid0",
		.bg_flag	= BTRFS_BLOCK_GROUP_RAID0,
		.mindev_error	= 0,
	},
	[BTRFS_RAID_SINGLE] = {
		.sub_stripes	= 1,
		.dev_stripes	= 1,
		.devs_max	= 1,
		.devs_min	= 1,
		.tolerated_failures = 0,
		.devs_increment	= 1,
		.ncopies	= 1,
		.nparity        = 0,
		.raid_name	= "single",
		.bg_flag	= 0,
		.mindev_error	= 0,
	},
	[BTRFS_RAID_RAID5] = {
		.sub_stripes	= 1,
		.dev_stripes	= 1,
		.devs_max	= 0,
		.devs_min	= 2,
		.tolerated_failures = 1,
		.devs_increment	= 1,
		.ncopies	= 1,
		.nparity        = 1,
		.raid_name	= "raid5",
		.bg_flag	= BTRFS_BLOCK_GROUP_RAID5,
		.mindev_error	= BTRFS_ERROR_DEV_RAID5_MIN_NOT_MET,
	},
	[BTRFS_RAID_RAID6] = {
		.sub_stripes	= 1,
		.dev_stripes	= 1,
		.devs_max	= 0,
		.devs_min	= 3,
		.tolerated_failures = 2,
		.devs_increment	= 1,
		.ncopies	= 1,
		.nparity        = 2,
		.raid_name	= "raid6",
		.bg_flag	= BTRFS_BLOCK_GROUP_RAID6,
		.mindev_error	= BTRFS_ERROR_DEV_RAID6_MIN_NOT_MET,
	},
};

const char *btrfs_bg_type_to_raid_name(u64 flags)
{
	const int index = btrfs_bg_flags_to_raid_index(flags);

	if (index >= BTRFS_NR_RAID_TYPES)
		return NULL;

	return btrfs_raid_array[index].raid_name;
}

/*
 * Fill @buf with textual description of @bg_flags, no more than @size_buf
 * bytes including terminating null byte.
 */
void btrfs_describe_block_groups(u64 bg_flags, char *buf, u32 size_buf)
{
	int i;
	int ret;
	char *bp = buf;
	u64 flags = bg_flags;
	u32 size_bp = size_buf;

	if (!flags) {
		strcpy(bp, "NONE");
		return;
	}

#define DESCRIBE_FLAG(flag, desc)						\
	do {								\
		if (flags & (flag)) {					\
			ret = snprintf(bp, size_bp, "%s|", (desc));	\
			if (ret < 0 || ret >= size_bp)			\
				goto out_overflow;			\
			size_bp -= ret;					\
			bp += ret;					\
			flags &= ~(flag);				\
		}							\
	} while (0)

	DESCRIBE_FLAG(BTRFS_BLOCK_GROUP_DATA, "data");
	DESCRIBE_FLAG(BTRFS_BLOCK_GROUP_SYSTEM, "system");
	DESCRIBE_FLAG(BTRFS_BLOCK_GROUP_METADATA, "metadata");

	DESCRIBE_FLAG(BTRFS_AVAIL_ALLOC_BIT_SINGLE, "single");
	for (i = 0; i < BTRFS_NR_RAID_TYPES; i++)
		DESCRIBE_FLAG(btrfs_raid_array[i].bg_flag,
			      btrfs_raid_array[i].raid_name);
#undef DESCRIBE_FLAG

	if (flags) {
		ret = snprintf(bp, size_bp, "0x%llx|", flags);
		size_bp -= ret;
	}

	if (size_bp < size_buf)
		buf[size_buf - size_bp - 1] = '\0'; /* remove last | */

	/*
	 * The text is trimmed, it's up to the caller to provide sufficiently
	 * large buffer
	 */
out_overflow:;
}

static int init_first_rw_device(struct btrfs_trans_handle *trans);
static int btrfs_relocate_sys_chunks(struct btrfs_fs_info *fs_info);
static void btrfs_dev_stat_print_on_error(struct btrfs_device *dev);
static void btrfs_dev_stat_print_on_load(struct btrfs_device *device);
static int __btrfs_map_block(struct btrfs_fs_info *fs_info,
			     enum btrfs_map_op op,
			     u64 logical, u64 *length,
			     struct btrfs_bio **bbio_ret,
			     int mirror_num, int need_raid_map);

/*
 * Device locking
 * ==============
 *
 * There are several mutexes that protect manipulation of devices and low-level
 * structures like chunks but not block groups, extents or files
 *
 * uuid_mutex (global lock)
 * ------------------------
 * protects the fs_uuids list that tracks all per-fs fs_devices, resulting from
 * the SCAN_DEV ioctl registration or from mount either implicitly (the first
 * device) or requested by the device= mount option
 *
 * the mutex can be very coarse and can cover long-running operations
 *
 * protects: updates to fs_devices counters like missing devices, rw devices,
 * seeding, structure cloning, opening/closing devices at mount/umount time
 *
 * global::fs_devs - add, remove, updates to the global list
 *
 * does not protect: manipulation of the fs_devices::devices list!
 *
 * btrfs_device::name - renames (write side), read is RCU
 *
 * fs_devices::device_list_mutex (per-fs, with RCU)
 * ------------------------------------------------
 * protects updates to fs_devices::devices, ie. adding and deleting
 *
 * simple list traversal with read-only actions can be done with RCU protection
 *
 * may be used to exclude some operations from running concurrently without any
 * modifications to the list (see write_all_supers)
 *
 * balance_mutex
 * -------------
 * protects balance structures (status, state) and context accessed from
 * several places (internally, ioctl)
 *
 * chunk_mutex
 * -----------
 * protects chunks, adding or removing during allocation, trim or when a new
 * device is added/removed. Additionally it also protects post_commit_list of
 * individual devices, since they can be added to the transaction's
 * post_commit_list only with chunk_mutex held.
 *
 * cleaner_mutex
 * -------------
 * a big lock that is held by the cleaner thread and prevents running subvolume
 * cleaning together with relocation or delayed iputs
 *
 *
 * Lock nesting
 * ============
 *
 * uuid_mutex
 *   volume_mutex
 *     device_list_mutex
 *       chunk_mutex
 *     balance_mutex
 *
 *
 * Exclusive operations, BTRFS_FS_EXCL_OP
 * ======================================
 *
 * Maintains the exclusivity of the following operations that apply to the
 * whole filesystem and cannot run in parallel.
 *
 * - Balance (*)
 * - Device add
 * - Device remove
 * - Device replace (*)
 * - Resize
 *
 * The device operations (as above) can be in one of the following states:
 *
 * - Running state
 * - Paused state
 * - Completed state
 *
 * Only device operations marked with (*) can go into the Paused state for the
 * following reasons:
 *
 * - ioctl (only Balance can be Paused through ioctl)
 * - filesystem remounted as read-only
 * - filesystem unmounted and mounted as read-only
 * - system power-cycle and filesystem mounted as read-only
 * - filesystem or device errors leading to forced read-only
 *
 * BTRFS_FS_EXCL_OP flag is set and cleared using atomic operations.
 * During the course of Paused state, the BTRFS_FS_EXCL_OP remains set.
 * A device operation in Paused or Running state can be canceled or resumed
 * either by ioctl (Balance only) or when remounted as read-write.
 * BTRFS_FS_EXCL_OP flag is cleared when the device operation is canceled or
 * completed.
 */

DEFINE_MUTEX(uuid_mutex);
static LIST_HEAD(fs_uuids);
struct list_head *btrfs_get_fs_uuids(void)
{
	return &fs_uuids;
}

/*
 * alloc_fs_devices - allocate struct btrfs_fs_devices
 * @fsid:		if not NULL, copy the UUID to fs_devices::fsid
 * @metadata_fsid:	if not NULL, copy the UUID to fs_devices::metadata_fsid
 *
 * Return a pointer to a new struct btrfs_fs_devices on success, or ERR_PTR().
 * The returned struct is not linked onto any lists and can be destroyed with
 * kfree() right away.
 */
static struct btrfs_fs_devices *alloc_fs_devices(const u8 *fsid,
						 const u8 *metadata_fsid)
{
	struct btrfs_fs_devices *fs_devs;

	fs_devs = kzalloc(sizeof(*fs_devs), GFP_KERNEL);
	if (!fs_devs)
		return ERR_PTR(-ENOMEM);

	mutex_init(&fs_devs->device_list_mutex);

	INIT_LIST_HEAD(&fs_devs->devices);
	INIT_LIST_HEAD(&fs_devs->alloc_list);
	INIT_LIST_HEAD(&fs_devs->fs_list);
	if (fsid)
		memcpy(fs_devs->fsid, fsid, BTRFS_FSID_SIZE);

	if (metadata_fsid)
		memcpy(fs_devs->metadata_uuid, metadata_fsid, BTRFS_FSID_SIZE);
	else if (fsid)
		memcpy(fs_devs->metadata_uuid, fsid, BTRFS_FSID_SIZE);

	return fs_devs;
}

void btrfs_free_device(struct btrfs_device *device)
{
	WARN_ON(!list_empty(&device->post_commit_list));
	rcu_string_free(device->name);
	extent_io_tree_release(&device->alloc_state);
	bio_put(device->flush_bio);
	kfree(device);
}

static void free_fs_devices(struct btrfs_fs_devices *fs_devices)
{
	struct btrfs_device *device;
	WARN_ON(fs_devices->opened);
	while (!list_empty(&fs_devices->devices)) {
		device = list_entry(fs_devices->devices.next,
				    struct btrfs_device, dev_list);
		list_del(&device->dev_list);
		btrfs_free_device(device);
	}
	kfree(fs_devices);
}

void __exit btrfs_cleanup_fs_uuids(void)
{
	struct btrfs_fs_devices *fs_devices;

	while (!list_empty(&fs_uuids)) {
		fs_devices = list_entry(fs_uuids.next,
					struct btrfs_fs_devices, fs_list);
		list_del(&fs_devices->fs_list);
		free_fs_devices(fs_devices);
	}
}

/*
 * Returns a pointer to a new btrfs_device on success; ERR_PTR() on error.
 * Returned struct is not linked onto any lists and must be destroyed using
 * btrfs_free_device.
 */
static struct btrfs_device *__alloc_device(void)
{
	struct btrfs_device *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	/*
	 * Preallocate a bio that's always going to be used for flushing device
	 * barriers and matches the device lifespan
	 */
	dev->flush_bio = bio_alloc_bioset(GFP_KERNEL, 0, NULL);
	if (!dev->flush_bio) {
		kfree(dev);
		return ERR_PTR(-ENOMEM);
	}

	INIT_LIST_HEAD(&dev->dev_list);
	INIT_LIST_HEAD(&dev->dev_alloc_list);
	INIT_LIST_HEAD(&dev->post_commit_list);

	spin_lock_init(&dev->io_lock);

	atomic_set(&dev->reada_in_flight, 0);
	atomic_set(&dev->dev_stats_ccnt, 0);
	btrfs_device_data_ordered_init(dev);
	INIT_RADIX_TREE(&dev->reada_zones, GFP_NOFS & ~__GFP_DIRECT_RECLAIM);
	INIT_RADIX_TREE(&dev->reada_extents, GFP_NOFS & ~__GFP_DIRECT_RECLAIM);
	extent_io_tree_init(NULL, &dev->alloc_state, 0, NULL);

	return dev;
}

static noinline struct btrfs_fs_devices *find_fsid(
		const u8 *fsid, const u8 *metadata_fsid)
{
	struct btrfs_fs_devices *fs_devices;

	ASSERT(fsid);

	if (metadata_fsid) {
		/*
		 * Handle scanned device having completed its fsid change but
		 * belonging to a fs_devices that was created by first scanning
		 * a device which didn't have its fsid/metadata_uuid changed
		 * at all and the CHANGING_FSID_V2 flag set.
		 */
		list_for_each_entry(fs_devices, &fs_uuids, fs_list) {
			if (fs_devices->fsid_change &&
			    memcmp(metadata_fsid, fs_devices->fsid,
				   BTRFS_FSID_SIZE) == 0 &&
			    memcmp(fs_devices->fsid, fs_devices->metadata_uuid,
				   BTRFS_FSID_SIZE) == 0) {
				return fs_devices;
			}
		}
		/*
		 * Handle scanned device having completed its fsid change but
		 * belonging to a fs_devices that was created by a device that
		 * has an outdated pair of fsid/metadata_uuid and
		 * CHANGING_FSID_V2 flag set.
		 */
		list_for_each_entry(fs_devices, &fs_uuids, fs_list) {
			if (fs_devices->fsid_change &&
			    memcmp(fs_devices->metadata_uuid,
				   fs_devices->fsid, BTRFS_FSID_SIZE) != 0 &&
			    memcmp(metadata_fsid, fs_devices->metadata_uuid,
				   BTRFS_FSID_SIZE) == 0) {
				return fs_devices;
			}
		}
	}

	/* Handle non-split brain cases */
	list_for_each_entry(fs_devices, &fs_uuids, fs_list) {
		if (metadata_fsid) {
			if (memcmp(fsid, fs_devices->fsid, BTRFS_FSID_SIZE) == 0
			    && memcmp(metadata_fsid, fs_devices->metadata_uuid,
				      BTRFS_FSID_SIZE) == 0)
				return fs_devices;
		} else {
			if (memcmp(fsid, fs_devices->fsid, BTRFS_FSID_SIZE) == 0)
				return fs_devices;
		}
	}
	return NULL;
}

static int
btrfs_get_bdev_and_sb(const char *device_path, fmode_t flags, void *holder,
		      int flush, struct block_device **bdev,
		      struct buffer_head **bh)
{
	int ret;

	*bdev = blkdev_get_by_path(device_path, flags, holder);

	if (IS_ERR(*bdev)) {
		ret = PTR_ERR(*bdev);
		goto error;
	}

	if (flush)
		filemap_write_and_wait((*bdev)->bd_inode->i_mapping);
	ret = set_blocksize(*bdev, BTRFS_BDEV_BLOCKSIZE);
	if (ret) {
		blkdev_put(*bdev, flags);
		goto error;
	}
	invalidate_bdev(*bdev);
	*bh = btrfs_read_dev_super(*bdev);
	if (IS_ERR(*bh)) {
		ret = PTR_ERR(*bh);
		blkdev_put(*bdev, flags);
		goto error;
	}

	return 0;

error:
	*bdev = NULL;
	*bh = NULL;
	return ret;
}

static void requeue_list(struct btrfs_pending_bios *pending_bios,
			struct bio *head, struct bio *tail)
{

	struct bio *old_head;

	old_head = pending_bios->head;
	pending_bios->head = head;
	if (pending_bios->tail)
		tail->bi_next = old_head;
	else
		pending_bios->tail = tail;
}

/*
 * we try to collect pending bios for a device so we don't get a large
 * number of procs sending bios down to the same device.  This greatly
 * improves the schedulers ability to collect and merge the bios.
 *
 * But, it also turns into a long list of bios to process and that is sure
 * to eventually make the worker thread block.  The solution here is to
 * make some progress and then put this work struct back at the end of
 * the list if the block device is congested.  This way, multiple devices
 * can make progress from a single worker thread.
 */
static noinline void run_scheduled_bios(struct btrfs_device *device)
{
	struct btrfs_fs_info *fs_info = device->fs_info;
	struct bio *pending;
	struct backing_dev_info *bdi;
	struct btrfs_pending_bios *pending_bios;
	struct bio *tail;
	struct bio *cur;
	int again = 0;
	unsigned long num_run;
	unsigned long batch_run = 0;
	unsigned long last_waited = 0;
	int force_reg = 0;
	int sync_pending = 0;
	struct blk_plug plug;

	/*
	 * this function runs all the bios we've collected for
	 * a particular device.  We don't want to wander off to
	 * another device without first sending all of these down.
	 * So, setup a plug here and finish it off before we return
	 */
	blk_start_plug(&plug);

	bdi = device->bdev->bd_bdi;

loop:
	spin_lock(&device->io_lock);

loop_lock:
	num_run = 0;

	/* take all the bios off the list at once and process them
	 * later on (without the lock held).  But, remember the
	 * tail and other pointers so the bios can be properly reinserted
	 * into the list if we hit congestion
	 */
	if (!force_reg && device->pending_sync_bios.head) {
		pending_bios = &device->pending_sync_bios;
		force_reg = 1;
	} else {
		pending_bios = &device->pending_bios;
		force_reg = 0;
	}

	pending = pending_bios->head;
	tail = pending_bios->tail;
	WARN_ON(pending && !tail);

	/*
	 * if pending was null this time around, no bios need processing
	 * at all and we can stop.  Otherwise it'll loop back up again
	 * and do an additional check so no bios are missed.
	 *
	 * device->running_pending is used to synchronize with the
	 * schedule_bio code.
	 */
	if (device->pending_sync_bios.head == NULL &&
	    device->pending_bios.head == NULL) {
		again = 0;
		device->running_pending = 0;
	} else {
		again = 1;
		device->running_pending = 1;
	}

	pending_bios->head = NULL;
	pending_bios->tail = NULL;

	spin_unlock(&device->io_lock);

	while (pending) {

		rmb();
		/* we want to work on both lists, but do more bios on the
		 * sync list than the regular list
		 */
		if ((num_run > 32 &&
		    pending_bios != &device->pending_sync_bios &&
		    device->pending_sync_bios.head) ||
		   (num_run > 64 && pending_bios == &device->pending_sync_bios &&
		    device->pending_bios.head)) {
			spin_lock(&device->io_lock);
			requeue_list(pending_bios, pending, tail);
			goto loop_lock;
		}

		cur = pending;
		pending = pending->bi_next;
		cur->bi_next = NULL;

		BUG_ON(atomic_read(&cur->__bi_cnt) == 0);

		/*
		 * if we're doing the sync list, record that our
		 * plug has some sync requests on it
		 *
		 * If we're doing the regular list and there are
		 * sync requests sitting around, unplug before
		 * we add more
		 */
		if (pending_bios == &device->pending_sync_bios) {
			sync_pending = 1;
		} else if (sync_pending) {
			blk_finish_plug(&plug);
			blk_start_plug(&plug);
			sync_pending = 0;
		}

		btrfsic_submit_bio(cur);
		num_run++;
		batch_run++;

		cond_resched();

		/*
		 * we made progress, there is more work to do and the bdi
		 * is now congested.  Back off and let other work structs
		 * run instead
		 */
		if (pending && bdi_write_congested(bdi) && batch_run > 8 &&
		    fs_info->fs_devices->open_devices > 1) {
			struct io_context *ioc;

			ioc = current->io_context;

			/*
			 * the main goal here is that we don't want to
			 * block if we're going to be able to submit
			 * more requests without blocking.
			 *
			 * This code does two great things, it pokes into
			 * the elevator code from a filesystem _and_
			 * it makes assumptions about how batching works.
			 */
			if (ioc && ioc->nr_batch_requests > 0 &&
			    time_before(jiffies, ioc->last_waited + HZ/50UL) &&
			    (last_waited == 0 ||
			     ioc->last_waited == last_waited)) {
				/*
				 * we want to go through our batch of
				 * requests and stop.  So, we copy out
				 * the ioc->last_waited time and test
				 * against it before looping
				 */
				last_waited = ioc->last_waited;
				cond_resched();
				continue;
			}
			spin_lock(&device->io_lock);
			requeue_list(pending_bios, pending, tail);
			device->running_pending = 1;

			spin_unlock(&device->io_lock);
			btrfs_queue_work(fs_info->submit_workers,
					 &device->work);
			goto done;
		}
	}

	cond_resched();
	if (again)
		goto loop;

	spin_lock(&device->io_lock);
	if (device->pending_bios.head || device->pending_sync_bios.head)
		goto loop_lock;
	spin_unlock(&device->io_lock);

done:
	blk_finish_plug(&plug);
}

static void pending_bios_fn(struct btrfs_work *work)
{
	struct btrfs_device *device;

	device = container_of(work, struct btrfs_device, work);
	run_scheduled_bios(device);
}

static bool device_path_matched(const char *path, struct btrfs_device *device)
{
	int found;

	rcu_read_lock();
	found = strcmp(rcu_str_deref(device->name), path);
	rcu_read_unlock();

	return found == 0;
}

/*
 *  Search and remove all stale (devices which are not mounted) devices.
 *  When both inputs are NULL, it will search and release all stale devices.
 *  path:	Optional. When provided will it release all unmounted devices
 *		matching this path only.
 *  skip_dev:	Optional. Will skip this device when searching for the stale
 *		devices.
 *  Return:	0 for success or if @path is NULL.
 * 		-EBUSY if @path is a mounted device.
 * 		-ENOENT if @path does not match any device in the list.
 */
static int btrfs_free_stale_devices(const char *path,
				     struct btrfs_device *skip_device)
{
	struct btrfs_fs_devices *fs_devices, *tmp_fs_devices;
	struct btrfs_device *device, *tmp_device;
	int ret = 0;

	if (path)
		ret = -ENOENT;

	list_for_each_entry_safe(fs_devices, tmp_fs_devices, &fs_uuids, fs_list) {

		mutex_lock(&fs_devices->device_list_mutex);
		list_for_each_entry_safe(device, tmp_device,
					 &fs_devices->devices, dev_list) {
			if (skip_device && skip_device == device)
				continue;
			if (path && !device->name)
				continue;
			if (path && !device_path_matched(path, device))
				continue;
			if (fs_devices->opened) {
				/* for an already deleted device return 0 */
				if (path && ret != 0)
					ret = -EBUSY;
				break;
			}

			/* delete the stale device */
			fs_devices->num_devices--;
			list_del(&device->dev_list);
			btrfs_free_device(device);

			ret = 0;
			if (fs_devices->num_devices == 0)
				break;
		}
		mutex_unlock(&fs_devices->device_list_mutex);

		if (fs_devices->num_devices == 0) {
			btrfs_sysfs_remove_fsid(fs_devices);
			list_del(&fs_devices->fs_list);
			free_fs_devices(fs_devices);
		}
	}

	return ret;
}

static int btrfs_open_one_device(struct btrfs_fs_devices *fs_devices,
			struct btrfs_device *device, fmode_t flags,
			void *holder)
{
	struct request_queue *q;
	struct block_device *bdev;
	struct buffer_head *bh;
	struct btrfs_super_block *disk_super;
	u64 devid;
	int ret;

	if (device->bdev)
		return -EINVAL;
	if (!device->name)
		return -EINVAL;

	ret = btrfs_get_bdev_and_sb(device->name->str, flags, holder, 1,
				    &bdev, &bh);
	if (ret)
		return ret;

	disk_super = (struct btrfs_super_block *)bh->b_data;
	devid = btrfs_stack_device_id(&disk_super->dev_item);
	if (devid != device->devid)
		goto error_brelse;

	if (memcmp(device->uuid, disk_super->dev_item.uuid, BTRFS_UUID_SIZE))
		goto error_brelse;

	device->generation = btrfs_super_generation(disk_super);

	if (btrfs_super_flags(disk_super) & BTRFS_SUPER_FLAG_SEEDING) {
		if (btrfs_super_incompat_flags(disk_super) &
		    BTRFS_FEATURE_INCOMPAT_METADATA_UUID) {
			pr_err(
		"BTRFS: Invalid seeding and uuid-changed device detected\n");
			goto error_brelse;
		}

		clear_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state);
		fs_devices->seeding = 1;
	} else {
		if (bdev_read_only(bdev))
			clear_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state);
		else
			set_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state);
	}

	q = bdev_get_queue(bdev);
	if (!blk_queue_nonrot(q))
		fs_devices->rotating = 1;

	device->bdev = bdev;
	clear_bit(BTRFS_DEV_STATE_IN_FS_METADATA, &device->dev_state);
	device->mode = flags;

	fs_devices->open_devices++;
	if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state) &&
	    device->devid != BTRFS_DEV_REPLACE_DEVID) {
		fs_devices->rw_devices++;
		list_add_tail(&device->dev_alloc_list, &fs_devices->alloc_list);
	}
	brelse(bh);

	return 0;

error_brelse:
	brelse(bh);
	blkdev_put(bdev, flags);

	return -EINVAL;
}

/*
 * Handle scanned device having its CHANGING_FSID_V2 flag set and the fs_devices
 * being created with a disk that has already completed its fsid change.
 */
static struct btrfs_fs_devices *find_fsid_inprogress(
					struct btrfs_super_block *disk_super)
{
	struct btrfs_fs_devices *fs_devices;

	list_for_each_entry(fs_devices, &fs_uuids, fs_list) {
		if (memcmp(fs_devices->metadata_uuid, fs_devices->fsid,
			   BTRFS_FSID_SIZE) != 0 &&
		    memcmp(fs_devices->metadata_uuid, disk_super->fsid,
			   BTRFS_FSID_SIZE) == 0 && !fs_devices->fsid_change) {
			return fs_devices;
		}
	}

	return NULL;
}


static struct btrfs_fs_devices *find_fsid_changed(
					struct btrfs_super_block *disk_super)
{
	struct btrfs_fs_devices *fs_devices;

	/*
	 * Handles the case where scanned device is part of an fs that had
	 * multiple successful changes of FSID but curently device didn't
	 * observe it. Meaning our fsid will be different than theirs.
	 */
	list_for_each_entry(fs_devices, &fs_uuids, fs_list) {
		if (memcmp(fs_devices->metadata_uuid, fs_devices->fsid,
			   BTRFS_FSID_SIZE) != 0 &&
		    memcmp(fs_devices->metadata_uuid, disk_super->metadata_uuid,
			   BTRFS_FSID_SIZE) == 0 &&
		    memcmp(fs_devices->fsid, disk_super->fsid,
			   BTRFS_FSID_SIZE) != 0) {
			return fs_devices;
		}
	}

	return NULL;
}
/*
 * Add new device to list of registered devices
 *
 * Returns:
 * device pointer which was just added or updated when successful
 * error pointer when failed
 */
static noinline struct btrfs_device *device_list_add(const char *path,
			   struct btrfs_super_block *disk_super,
			   bool *new_device_added)
{
	struct btrfs_device *device;
	struct btrfs_fs_devices *fs_devices = NULL;
	struct rcu_string *name;
	u64 found_transid = btrfs_super_generation(disk_super);
	u64 devid = btrfs_stack_device_id(&disk_super->dev_item);
	bool has_metadata_uuid = (btrfs_super_incompat_flags(disk_super) &
		BTRFS_FEATURE_INCOMPAT_METADATA_UUID);
	bool fsid_change_in_progress = (btrfs_super_flags(disk_super) &
					BTRFS_SUPER_FLAG_CHANGING_FSID_V2);

	if (fsid_change_in_progress) {
		if (!has_metadata_uuid) {
			/*
			 * When we have an image which has CHANGING_FSID_V2 set
			 * it might belong to either a filesystem which has
			 * disks with completed fsid change or it might belong
			 * to fs with no UUID changes in effect, handle both.
			 */
			fs_devices = find_fsid_inprogress(disk_super);
			if (!fs_devices)
				fs_devices = find_fsid(disk_super->fsid, NULL);
		} else {
			fs_devices = find_fsid_changed(disk_super);
		}
	} else if (has_metadata_uuid) {
		fs_devices = find_fsid(disk_super->fsid,
				       disk_super->metadata_uuid);
	} else {
		fs_devices = find_fsid(disk_super->fsid, NULL);
	}


	if (!fs_devices) {
		if (has_metadata_uuid)
			fs_devices = alloc_fs_devices(disk_super->fsid,
						      disk_super->metadata_uuid);
		else
			fs_devices = alloc_fs_devices(disk_super->fsid, NULL);

		if (IS_ERR(fs_devices))
			return ERR_CAST(fs_devices);

		fs_devices->fsid_change = fsid_change_in_progress;

		mutex_lock(&fs_devices->device_list_mutex);
		list_add(&fs_devices->fs_list, &fs_uuids);

		device = NULL;
	} else {
		mutex_lock(&fs_devices->device_list_mutex);
		device = btrfs_find_device(fs_devices, devid,
				disk_super->dev_item.uuid, NULL, false);

		/*
		 * If this disk has been pulled into an fs devices created by
		 * a device which had the CHANGING_FSID_V2 flag then replace the
		 * metadata_uuid/fsid values of the fs_devices.
		 */
		if (has_metadata_uuid && fs_devices->fsid_change &&
		    found_transid > fs_devices->latest_generation) {
			memcpy(fs_devices->fsid, disk_super->fsid,
					BTRFS_FSID_SIZE);
			memcpy(fs_devices->metadata_uuid,
					disk_super->metadata_uuid, BTRFS_FSID_SIZE);

			fs_devices->fsid_change = false;
		}
	}

	if (!device) {
		if (fs_devices->opened) {
			mutex_unlock(&fs_devices->device_list_mutex);
			return ERR_PTR(-EBUSY);
		}

		device = btrfs_alloc_device(NULL, &devid,
					    disk_super->dev_item.uuid);
		if (IS_ERR(device)) {
			mutex_unlock(&fs_devices->device_list_mutex);
			/* we can safely leave the fs_devices entry around */
			return device;
		}

		name = rcu_string_strdup(path, GFP_NOFS);
		if (!name) {
			btrfs_free_device(device);
			mutex_unlock(&fs_devices->device_list_mutex);
			return ERR_PTR(-ENOMEM);
		}
		rcu_assign_pointer(device->name, name);

		list_add_rcu(&device->dev_list, &fs_devices->devices);
		fs_devices->num_devices++;

		device->fs_devices = fs_devices;
		*new_device_added = true;

		if (disk_super->label[0])
			pr_info("BTRFS: device label %s devid %llu transid %llu %s\n",
				disk_super->label, devid, found_transid, path);
		else
			pr_info("BTRFS: device fsid %pU devid %llu transid %llu %s\n",
				disk_super->fsid, devid, found_transid, path);

	} else if (!device->name || strcmp(device->name->str, path)) {
		/*
		 * When FS is already mounted.
		 * 1. If you are here and if the device->name is NULL that
		 *    means this device was missing at time of FS mount.
		 * 2. If you are here and if the device->name is different
		 *    from 'path' that means either
		 *      a. The same device disappeared and reappeared with
		 *         different name. or
		 *      b. The missing-disk-which-was-replaced, has
		 *         reappeared now.
		 *
		 * We must allow 1 and 2a above. But 2b would be a spurious
		 * and unintentional.
		 *
		 * Further in case of 1 and 2a above, the disk at 'path'
		 * would have missed some transaction when it was away and
		 * in case of 2a the stale bdev has to be updated as well.
		 * 2b must not be allowed at all time.
		 */

		/*
		 * For now, we do allow update to btrfs_fs_device through the
		 * btrfs dev scan cli after FS has been mounted.  We're still
		 * tracking a problem where systems fail mount by subvolume id
		 * when we reject replacement on a mounted FS.
		 */
		if (!fs_devices->opened && found_transid < device->generation) {
			/*
			 * That is if the FS is _not_ mounted and if you
			 * are here, that means there is more than one
			 * disk with same uuid and devid.We keep the one
			 * with larger generation number or the last-in if
			 * generation are equal.
			 */
			mutex_unlock(&fs_devices->device_list_mutex);
			return ERR_PTR(-EEXIST);
		}

		/*
		 * We are going to replace the device path for a given devid,
		 * make sure it's the same device if the device is mounted
		 */
		if (device->bdev) {
			struct block_device *path_bdev;

			path_bdev = lookup_bdev(path);
			if (IS_ERR(path_bdev)) {
				mutex_unlock(&fs_devices->device_list_mutex);
				return ERR_CAST(path_bdev);
			}

			if (device->bdev != path_bdev) {
				bdput(path_bdev);
				mutex_unlock(&fs_devices->device_list_mutex);
				btrfs_warn_in_rcu(device->fs_info,
			"duplicate device fsid:devid for %pU:%llu old:%s new:%s",
					disk_super->fsid, devid,
					rcu_str_deref(device->name), path);
				return ERR_PTR(-EEXIST);
			}
			bdput(path_bdev);
			btrfs_info_in_rcu(device->fs_info,
				"device fsid %pU devid %llu moved old:%s new:%s",
				disk_super->fsid, devid,
				rcu_str_deref(device->name), path);
		}

		name = rcu_string_strdup(path, GFP_NOFS);
		if (!name) {
			mutex_unlock(&fs_devices->device_list_mutex);
			return ERR_PTR(-ENOMEM);
		}
		rcu_string_free(device->name);
		rcu_assign_pointer(device->name, name);
		if (test_bit(BTRFS_DEV_STATE_MISSING, &device->dev_state)) {
			fs_devices->missing_devices--;
			clear_bit(BTRFS_DEV_STATE_MISSING, &device->dev_state);
		}
	}

	/*
	 * Unmount does not free the btrfs_device struct but would zero
	 * generation along with most of the other members. So just update
	 * it back. We need it to pick the disk with largest generation
	 * (as above).
	 */
	if (!fs_devices->opened) {
		device->generation = found_transid;
		fs_devices->latest_generation = max_t(u64, found_transid,
						fs_devices->latest_generation);
	}

	fs_devices->total_devices = btrfs_super_num_devices(disk_super);

	mutex_unlock(&fs_devices->device_list_mutex);
	return device;
}

static struct btrfs_fs_devices *clone_fs_devices(struct btrfs_fs_devices *orig)
{
	struct btrfs_fs_devices *fs_devices;
	struct btrfs_device *device;
	struct btrfs_device *orig_dev;
	int ret = 0;

	fs_devices = alloc_fs_devices(orig->fsid, NULL);
	if (IS_ERR(fs_devices))
		return fs_devices;

	mutex_lock(&orig->device_list_mutex);
	fs_devices->total_devices = orig->total_devices;

	list_for_each_entry(orig_dev, &orig->devices, dev_list) {
		struct rcu_string *name;

		device = btrfs_alloc_device(NULL, &orig_dev->devid,
					    orig_dev->uuid);
		if (IS_ERR(device)) {
			ret = PTR_ERR(device);
			goto error;
		}

		/*
		 * This is ok to do without rcu read locked because we hold the
		 * uuid mutex so nothing we touch in here is going to disappear.
		 */
		if (orig_dev->name) {
			name = rcu_string_strdup(orig_dev->name->str,
					GFP_KERNEL);
			if (!name) {
				btrfs_free_device(device);
				ret = -ENOMEM;
				goto error;
			}
			rcu_assign_pointer(device->name, name);
		}

		list_add(&device->dev_list, &fs_devices->devices);
		device->fs_devices = fs_devices;
		fs_devices->num_devices++;
	}
	mutex_unlock(&orig->device_list_mutex);
	return fs_devices;
error:
	mutex_unlock(&orig->device_list_mutex);
	free_fs_devices(fs_devices);
	return ERR_PTR(ret);
}

/*
 * After we have read the system tree and know devids belonging to
 * this filesystem, remove the device which does not belong there.
 */
void btrfs_free_extra_devids(struct btrfs_fs_devices *fs_devices, int step)
{
	struct btrfs_device *device, *next;
	struct btrfs_device *latest_dev = NULL;

	mutex_lock(&uuid_mutex);
again:
	/* This is the initialized path, it is safe to release the devices. */
	list_for_each_entry_safe(device, next, &fs_devices->devices, dev_list) {
		if (test_bit(BTRFS_DEV_STATE_IN_FS_METADATA,
							&device->dev_state)) {
			if (!test_bit(BTRFS_DEV_STATE_REPLACE_TGT,
			     &device->dev_state) &&
			     (!latest_dev ||
			      device->generation > latest_dev->generation)) {
				latest_dev = device;
			}
			continue;
		}

		if (device->devid == BTRFS_DEV_REPLACE_DEVID) {
			/*
			 * In the first step, keep the device which has
			 * the correct fsid and the devid that is used
			 * for the dev_replace procedure.
			 * In the second step, the dev_replace state is
			 * read from the device tree and it is known
			 * whether the procedure is really active or
			 * not, which means whether this device is
			 * used or whether it should be removed.
			 */
			if (step == 0 || test_bit(BTRFS_DEV_STATE_REPLACE_TGT,
						  &device->dev_state)) {
				continue;
			}
		}
		if (device->bdev) {
			blkdev_put(device->bdev, device->mode);
			device->bdev = NULL;
			fs_devices->open_devices--;
		}
		if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state)) {
			list_del_init(&device->dev_alloc_list);
			clear_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state);
			if (!test_bit(BTRFS_DEV_STATE_REPLACE_TGT,
				      &device->dev_state))
				fs_devices->rw_devices--;
		}
		list_del_init(&device->dev_list);
		fs_devices->num_devices--;
		btrfs_free_device(device);
	}

	if (fs_devices->seed) {
		fs_devices = fs_devices->seed;
		goto again;
	}

	fs_devices->latest_bdev = latest_dev->bdev;

	mutex_unlock(&uuid_mutex);
}

static void btrfs_close_bdev(struct btrfs_device *device)
{
	if (!device->bdev)
		return;

	if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state)) {
		sync_blockdev(device->bdev);
		invalidate_bdev(device->bdev);
	}

	blkdev_put(device->bdev, device->mode);
}

static void btrfs_close_one_device(struct btrfs_device *device)
{
	struct btrfs_fs_devices *fs_devices = device->fs_devices;
	struct btrfs_device *new_device;
	struct rcu_string *name;

	if (device->bdev)
		fs_devices->open_devices--;

	if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state) &&
	    device->devid != BTRFS_DEV_REPLACE_DEVID) {
		list_del_init(&device->dev_alloc_list);
		fs_devices->rw_devices--;
	}

	if (test_bit(BTRFS_DEV_STATE_MISSING, &device->dev_state))
		fs_devices->missing_devices--;

	btrfs_close_bdev(device);

	new_device = btrfs_alloc_device(NULL, &device->devid,
					device->uuid);
	BUG_ON(IS_ERR(new_device)); /* -ENOMEM */

	/* Safe because we are under uuid_mutex */
	if (device->name) {
		name = rcu_string_strdup(device->name->str, GFP_NOFS);
		BUG_ON(!name); /* -ENOMEM */
		rcu_assign_pointer(new_device->name, name);
	}

	list_replace_rcu(&device->dev_list, &new_device->dev_list);
	new_device->fs_devices = device->fs_devices;

	synchronize_rcu();
	btrfs_free_device(device);
}

static int close_fs_devices(struct btrfs_fs_devices *fs_devices)
{
	struct btrfs_device *device, *tmp;

	if (--fs_devices->opened > 0)
		return 0;

	mutex_lock(&fs_devices->device_list_mutex);
	list_for_each_entry_safe(device, tmp, &fs_devices->devices, dev_list) {
		btrfs_close_one_device(device);
	}
	mutex_unlock(&fs_devices->device_list_mutex);

	WARN_ON(fs_devices->open_devices);
	WARN_ON(fs_devices->rw_devices);
	fs_devices->opened = 0;
	fs_devices->seeding = 0;

	return 0;
}

int btrfs_close_devices(struct btrfs_fs_devices *fs_devices)
{
	struct btrfs_fs_devices *seed_devices = NULL;
	int ret;

	mutex_lock(&uuid_mutex);
	ret = close_fs_devices(fs_devices);
	if (!fs_devices->opened) {
		seed_devices = fs_devices->seed;
		fs_devices->seed = NULL;
	}
	mutex_unlock(&uuid_mutex);

	while (seed_devices) {
		fs_devices = seed_devices;
		seed_devices = fs_devices->seed;
		close_fs_devices(fs_devices);
		free_fs_devices(fs_devices);
	}
	return ret;
}

static int open_fs_devices(struct btrfs_fs_devices *fs_devices,
				fmode_t flags, void *holder)
{
	struct btrfs_device *device;
	struct btrfs_device *latest_dev = NULL;
	int ret = 0;

	flags |= FMODE_EXCL;

	list_for_each_entry(device, &fs_devices->devices, dev_list) {
		/* Just open everything we can; ignore failures here */
		if (btrfs_open_one_device(fs_devices, device, flags, holder))
			continue;

		if (!latest_dev ||
		    device->generation > latest_dev->generation)
			latest_dev = device;
	}
	if (fs_devices->open_devices == 0) {
		ret = -EINVAL;
		goto out;
	}
	fs_devices->opened = 1;
	fs_devices->latest_bdev = latest_dev->bdev;
	fs_devices->total_rw_bytes = 0;
out:
	return ret;
}

static int devid_cmp(void *priv, struct list_head *a, struct list_head *b)
{
	struct btrfs_device *dev1, *dev2;

	dev1 = list_entry(a, struct btrfs_device, dev_list);
	dev2 = list_entry(b, struct btrfs_device, dev_list);

	if (dev1->devid < dev2->devid)
		return -1;
	else if (dev1->devid > dev2->devid)
		return 1;
	return 0;
}

int btrfs_open_devices(struct btrfs_fs_devices *fs_devices,
		       fmode_t flags, void *holder)
{
	int ret;

	lockdep_assert_held(&uuid_mutex);

	mutex_lock(&fs_devices->device_list_mutex);
	if (fs_devices->opened) {
		fs_devices->opened++;
		ret = 0;
	} else {
		list_sort(NULL, &fs_devices->devices, devid_cmp);
		ret = open_fs_devices(fs_devices, flags, holder);
	}
	mutex_unlock(&fs_devices->device_list_mutex);

	return ret;
}

static void btrfs_release_disk_super(struct page *page)
{
	kunmap(page);
	put_page(page);
}

static int btrfs_read_disk_super(struct block_device *bdev, u64 bytenr,
				 struct page **page,
				 struct btrfs_super_block **disk_super)
{
	void *p;
	pgoff_t index;

	/* make sure our super fits in the device */
	if (bytenr + PAGE_SIZE >= i_size_read(bdev->bd_inode))
		return 1;

	/* make sure our super fits in the page */
	if (sizeof(**disk_super) > PAGE_SIZE)
		return 1;

	/* make sure our super doesn't straddle pages on disk */
	index = bytenr >> PAGE_SHIFT;
	if ((bytenr + sizeof(**disk_super) - 1) >> PAGE_SHIFT != index)
		return 1;

	/* pull in the page with our super */
	*page = read_cache_page_gfp(bdev->bd_inode->i_mapping,
				   index, GFP_KERNEL);

	if (IS_ERR_OR_NULL(*page))
		return 1;

	p = kmap(*page);

	/* align our pointer to the offset of the super block */
	*disk_super = p + offset_in_page(bytenr);

	if (btrfs_super_bytenr(*disk_super) != bytenr ||
	    btrfs_super_magic(*disk_super) != BTRFS_MAGIC) {
		btrfs_release_disk_super(*page);
		return 1;
	}

	if ((*disk_super)->label[0] &&
		(*disk_super)->label[BTRFS_LABEL_SIZE - 1])
		(*disk_super)->label[BTRFS_LABEL_SIZE - 1] = '\0';

	return 0;
}

int btrfs_forget_devices(const char *path)
{
	int ret;

	mutex_lock(&uuid_mutex);
	ret = btrfs_free_stale_devices(strlen(path) ? path : NULL, NULL);
	mutex_unlock(&uuid_mutex);

	return ret;
}

/*
 * Look for a btrfs signature on a device. This may be called out of the mount path
 * and we are not allowed to call set_blocksize during the scan. The superblock
 * is read via pagecache
 */
struct btrfs_device *btrfs_scan_one_device(const char *path, fmode_t flags,
					   void *holder)
{
	struct btrfs_super_block *disk_super;
	bool new_device_added = false;
	struct btrfs_device *device = NULL;
	struct block_device *bdev;
	struct page *page;
	u64 bytenr;

	lockdep_assert_held(&uuid_mutex);

	/*
	 * we would like to check all the supers, but that would make
	 * a btrfs mount succeed after a mkfs from a different FS.
	 * So, we need to add a special mount option to scan for
	 * later supers, using BTRFS_SUPER_MIRROR_MAX instead
	 */
	bytenr = btrfs_sb_offset(0);
	flags |= FMODE_EXCL;

	bdev = blkdev_get_by_path(path, flags, holder);
	if (IS_ERR(bdev))
		return ERR_CAST(bdev);

	if (btrfs_read_disk_super(bdev, bytenr, &page, &disk_super)) {
		device = ERR_PTR(-EINVAL);
		goto error_bdev_put;
	}

	device = device_list_add(path, disk_super, &new_device_added);
	if (!IS_ERR(device)) {
		if (new_device_added)
			btrfs_free_stale_devices(path, device);
	}

	btrfs_release_disk_super(page);

error_bdev_put:
	blkdev_put(bdev, flags);

	return device;
}

/*
 * Try to find a chunk that intersects [start, start + len] range and when one
 * such is found, record the end of it in *start
 */
static bool contains_pending_extent(struct btrfs_device *device, u64 *start,
				    u64 len)
{
	u64 physical_start, physical_end;

	lockdep_assert_held(&device->fs_info->chunk_mutex);

	if (!find_first_extent_bit(&device->alloc_state, *start,
				   &physical_start, &physical_end,
				   CHUNK_ALLOCATED, NULL)) {

		if (in_range(physical_start, *start, len) ||
		    in_range(*start, physical_start,
			     physical_end - physical_start)) {
			*start = physical_end + 1;
			return true;
		}
	}
	return false;
}


/*
 * find_free_dev_extent_start - find free space in the specified device
 * @device:	  the device which we search the free space in
 * @num_bytes:	  the size of the free space that we need
 * @search_start: the position from which to begin the search
 * @start:	  store the start of the free space.
 * @len:	  the size of the free space. that we find, or the size
 *		  of the max free space if we don't find suitable free space
 *
 * this uses a pretty simple search, the expectation is that it is
 * called very infrequently and that a given device has a small number
 * of extents
 *
 * @start is used to store the start of the free space if we find. But if we
 * don't find suitable free space, it will be used to store the start position
 * of the max free space.
 *
 * @len is used to store the size of the free space that we find.
 * But if we don't find suitable free space, it is used to store the size of
 * the max free space.
 *
 * NOTE: This function will search *commit* root of device tree, and does extra
 * check to ensure dev extents are not double allocated.
 * This makes the function safe to allocate dev extents but may not report
 * correct usable device space, as device extent freed in current transaction
 * is not reported as avaiable.
 */
static int find_free_dev_extent_start(struct btrfs_device *device,
				u64 num_bytes, u64 search_start, u64 *start,
				u64 *len)
{
	struct btrfs_fs_info *fs_info = device->fs_info;
	struct btrfs_root *root = fs_info->dev_root;
	struct btrfs_key key;
	struct btrfs_dev_extent *dev_extent;
	struct btrfs_path *path;
	u64 hole_size;
	u64 max_hole_start;
	u64 max_hole_size;
	u64 extent_end;
	u64 search_end = device->total_bytes;
	int ret;
	int slot;
	struct extent_buffer *l;

	/*
	 * We don't want to overwrite the superblock on the drive nor any area
	 * used by the boot loader (grub for example), so we make sure to start
	 * at an offset of at least 1MB.
	 */
	search_start = max_t(u64, search_start, SZ_1M);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	max_hole_start = search_start;
	max_hole_size = 0;

again:
	if (search_start >= search_end ||
		test_bit(BTRFS_DEV_STATE_REPLACE_TGT, &device->dev_state)) {
		ret = -ENOSPC;
		goto out;
	}

	path->reada = READA_FORWARD;
	path->search_commit_root = 1;
	path->skip_locking = 1;

	key.objectid = device->devid;
	key.offset = search_start;
	key.type = BTRFS_DEV_EXTENT_KEY;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	if (ret > 0) {
		ret = btrfs_previous_item(root, path, key.objectid, key.type);
		if (ret < 0)
			goto out;
	}

	while (1) {
		l = path->nodes[0];
		slot = path->slots[0];
		if (slot >= btrfs_header_nritems(l)) {
			ret = btrfs_next_leaf(root, path);
			if (ret == 0)
				continue;
			if (ret < 0)
				goto out;

			break;
		}
		btrfs_item_key_to_cpu(l, &key, slot);

		if (key.objectid < device->devid)
			goto next;

		if (key.objectid > device->devid)
			break;

		if (key.type != BTRFS_DEV_EXTENT_KEY)
			goto next;

		if (key.offset > search_start) {
			hole_size = key.offset - search_start;

			/*
			 * Have to check before we set max_hole_start, otherwise
			 * we could end up sending back this offset anyway.
			 */
			if (contains_pending_extent(device, &search_start,
						    hole_size)) {
				if (key.offset >= search_start)
					hole_size = key.offset - search_start;
				else
					hole_size = 0;
			}

			if (hole_size > max_hole_size) {
				max_hole_start = search_start;
				max_hole_size = hole_size;
			}

			/*
			 * If this free space is greater than which we need,
			 * it must be the max free space that we have found
			 * until now, so max_hole_start must point to the start
			 * of this free space and the length of this free space
			 * is stored in max_hole_size. Thus, we return
			 * max_hole_start and max_hole_size and go back to the
			 * caller.
			 */
			if (hole_size >= num_bytes) {
				ret = 0;
				goto out;
			}
		}

		dev_extent = btrfs_item_ptr(l, slot, struct btrfs_dev_extent);
		extent_end = key.offset + btrfs_dev_extent_length(l,
								  dev_extent);
		if (extent_end > search_start)
			search_start = extent_end;
next:
		path->slots[0]++;
		cond_resched();
	}

	/*
	 * At this point, search_start should be the end of
	 * allocated dev extents, and when shrinking the device,
	 * search_end may be smaller than search_start.
	 */
	if (search_end > search_start) {
		hole_size = search_end - search_start;

		if (contains_pending_extent(device, &search_start, hole_size)) {
			btrfs_release_path(path);
			goto again;
		}

		if (hole_size > max_hole_size) {
			max_hole_start = search_start;
			max_hole_size = hole_size;
		}
	}

	/* See above. */
	if (max_hole_size < num_bytes)
		ret = -ENOSPC;
	else
		ret = 0;

out:
	btrfs_free_path(path);
	*start = max_hole_start;
	if (len)
		*len = max_hole_size;
	return ret;
}

int find_free_dev_extent(struct btrfs_device *device, u64 num_bytes,
			 u64 *start, u64 *len)
{
	/* FIXME use last free of some kind */
	return find_free_dev_extent_start(device, num_bytes, 0, start, len);
}

static int btrfs_free_dev_extent(struct btrfs_trans_handle *trans,
			  struct btrfs_device *device,
			  u64 start, u64 *dev_extent_len)
{
	struct btrfs_fs_info *fs_info = device->fs_info;
	struct btrfs_root *root = fs_info->dev_root;
	int ret;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *leaf = NULL;
	struct btrfs_dev_extent *extent = NULL;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = device->devid;
	key.offset = start;
	key.type = BTRFS_DEV_EXTENT_KEY;
again:
	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret > 0) {
		ret = btrfs_previous_item(root, path, key.objectid,
					  BTRFS_DEV_EXTENT_KEY);
		if (ret)
			goto out;
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		extent = btrfs_item_ptr(leaf, path->slots[0],
					struct btrfs_dev_extent);
		BUG_ON(found_key.offset > start || found_key.offset +
		       btrfs_dev_extent_length(leaf, extent) < start);
		key = found_key;
		btrfs_release_path(path);
		goto again;
	} else if (ret == 0) {
		leaf = path->nodes[0];
		extent = btrfs_item_ptr(leaf, path->slots[0],
					struct btrfs_dev_extent);
	} else {
		btrfs_handle_fs_error(fs_info, ret, "Slot search failed");
		goto out;
	}

	*dev_extent_len = btrfs_dev_extent_length(leaf, extent);

	ret = btrfs_del_item(trans, root, path);
	if (ret) {
		btrfs_handle_fs_error(fs_info, ret,
				      "Failed to remove dev extent item");
	} else {
		set_bit(BTRFS_TRANS_HAVE_FREE_BGS, &trans->transaction->flags);
	}
out:
	btrfs_free_path(path);
	return ret;
}

static int btrfs_alloc_dev_extent(struct btrfs_trans_handle *trans,
				  struct btrfs_device *device,
				  u64 chunk_offset, u64 start, u64 num_bytes)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_fs_info *fs_info = device->fs_info;
	struct btrfs_root *root = fs_info->dev_root;
	struct btrfs_dev_extent *extent;
	struct extent_buffer *leaf;
	struct btrfs_key key;

	WARN_ON(!test_bit(BTRFS_DEV_STATE_IN_FS_METADATA, &device->dev_state));
	WARN_ON(test_bit(BTRFS_DEV_STATE_REPLACE_TGT, &device->dev_state));
	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = device->devid;
	key.offset = start;
	key.type = BTRFS_DEV_EXTENT_KEY;
	ret = btrfs_insert_empty_item(trans, root, path, &key,
				      sizeof(*extent));
	if (ret)
		goto out;

	leaf = path->nodes[0];
	extent = btrfs_item_ptr(leaf, path->slots[0],
				struct btrfs_dev_extent);
	btrfs_set_dev_extent_chunk_tree(leaf, extent,
					BTRFS_CHUNK_TREE_OBJECTID);
	btrfs_set_dev_extent_chunk_objectid(leaf, extent,
					    BTRFS_FIRST_CHUNK_TREE_OBJECTID);
	btrfs_set_dev_extent_chunk_offset(leaf, extent, chunk_offset);

	btrfs_set_dev_extent_length(leaf, extent, num_bytes);
	btrfs_mark_buffer_dirty(leaf);
out:
	btrfs_free_path(path);
	return ret;
}

static u64 find_next_chunk(struct btrfs_fs_info *fs_info)
{
	struct extent_map_tree *em_tree;
	struct extent_map *em;
	struct rb_node *n;
	u64 ret = 0;

	em_tree = &fs_info->mapping_tree;
	read_lock(&em_tree->lock);
	n = rb_last(&em_tree->map.rb_root);
	if (n) {
		em = rb_entry(n, struct extent_map, rb_node);
		ret = em->start + em->len;
	}
	read_unlock(&em_tree->lock);

	return ret;
}

static noinline int find_next_devid(struct btrfs_fs_info *fs_info,
				    u64 *devid_ret)
{
	int ret;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_path *path;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = BTRFS_DEV_ITEMS_OBJECTID;
	key.type = BTRFS_DEV_ITEM_KEY;
	key.offset = (u64)-1;

	ret = btrfs_search_slot(NULL, fs_info->chunk_root, &key, path, 0, 0);
	if (ret < 0)
		goto error;

	if (ret == 0) {
		/* Corruption */
		btrfs_err(fs_info, "corrupted chunk tree devid -1 matched");
		ret = -EUCLEAN;
		goto error;
	}

	ret = btrfs_previous_item(fs_info->chunk_root, path,
				  BTRFS_DEV_ITEMS_OBJECTID,
				  BTRFS_DEV_ITEM_KEY);
	if (ret) {
		*devid_ret = 1;
	} else {
		btrfs_item_key_to_cpu(path->nodes[0], &found_key,
				      path->slots[0]);
		*devid_ret = found_key.offset + 1;
	}
	ret = 0;
error:
	btrfs_free_path(path);
	return ret;
}

/*
 * the device information is stored in the chunk root
 * the btrfs_device struct should be fully filled in
 */
static int btrfs_add_dev_item(struct btrfs_trans_handle *trans,
			    struct btrfs_device *device)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_dev_item *dev_item;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	unsigned long ptr;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = BTRFS_DEV_ITEMS_OBJECTID;
	key.type = BTRFS_DEV_ITEM_KEY;
	key.offset = device->devid;

	ret = btrfs_insert_empty_item(trans, trans->fs_info->chunk_root, path,
				      &key, sizeof(*dev_item));
	if (ret)
		goto out;

	leaf = path->nodes[0];
	dev_item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_dev_item);

	btrfs_set_device_id(leaf, dev_item, device->devid);
	btrfs_set_device_generation(leaf, dev_item, 0);
	btrfs_set_device_type(leaf, dev_item, device->type);
	btrfs_set_device_io_align(leaf, dev_item, device->io_align);
	btrfs_set_device_io_width(leaf, dev_item, device->io_width);
	btrfs_set_device_sector_size(leaf, dev_item, device->sector_size);
	btrfs_set_device_total_bytes(leaf, dev_item,
				     btrfs_device_get_disk_total_bytes(device));
	btrfs_set_device_bytes_used(leaf, dev_item,
				    btrfs_device_get_bytes_used(device));
	btrfs_set_device_group(leaf, dev_item, 0);
	btrfs_set_device_seek_speed(leaf, dev_item, 0);
	btrfs_set_device_bandwidth(leaf, dev_item, 0);
	btrfs_set_device_start_offset(leaf, dev_item, 0);

	ptr = btrfs_device_uuid(dev_item);
	write_extent_buffer(leaf, device->uuid, ptr, BTRFS_UUID_SIZE);
	ptr = btrfs_device_fsid(dev_item);
	write_extent_buffer(leaf, trans->fs_info->fs_devices->metadata_uuid,
			    ptr, BTRFS_FSID_SIZE);
	btrfs_mark_buffer_dirty(leaf);

	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

/*
 * Function to update ctime/mtime for a given device path.
 * Mainly used for ctime/mtime based probe like libblkid.
 */
static void update_dev_time(const char *path_name)
{
	struct file *filp;

	filp = filp_open(path_name, O_RDWR, 0);
	if (IS_ERR(filp))
		return;
	file_update_time(filp);
	filp_close(filp, NULL);
}

static int btrfs_rm_dev_item(struct btrfs_device *device)
{
	struct btrfs_root *root = device->fs_info->chunk_root;
	int ret;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_trans_handle *trans;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		btrfs_free_path(path);
		return PTR_ERR(trans);
	}
	key.objectid = BTRFS_DEV_ITEMS_OBJECTID;
	key.type = BTRFS_DEV_ITEM_KEY;
	key.offset = device->devid;

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret) {
		if (ret > 0)
			ret = -ENOENT;
		btrfs_abort_transaction(trans, ret);
		btrfs_end_transaction(trans);
		goto out;
	}

	ret = btrfs_del_item(trans, root, path);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		btrfs_end_transaction(trans);
	}

out:
	btrfs_free_path(path);
	if (!ret)
		ret = btrfs_commit_transaction(trans);
	return ret;
}

/*
 * Verify that @num_devices satisfies the RAID profile constraints in the whole
 * filesystem. It's up to the caller to adjust that number regarding eg. device
 * replace.
 */
static int btrfs_check_raid_min_devices(struct btrfs_fs_info *fs_info,
		u64 num_devices)
{
	u64 all_avail;
	unsigned seq;
	int i;

	do {
		seq = read_seqbegin(&fs_info->profiles_lock);

		all_avail = fs_info->avail_data_alloc_bits |
			    fs_info->avail_system_alloc_bits |
			    fs_info->avail_metadata_alloc_bits;
	} while (read_seqretry(&fs_info->profiles_lock, seq));

	for (i = 0; i < BTRFS_NR_RAID_TYPES; i++) {
		if (!(all_avail & btrfs_raid_array[i].bg_flag))
			continue;

		if (num_devices < btrfs_raid_array[i].devs_min) {
			int ret = btrfs_raid_array[i].mindev_error;

			if (ret)
				return ret;
		}
	}

	return 0;
}

static struct btrfs_device * btrfs_find_next_active_device(
		struct btrfs_fs_devices *fs_devs, struct btrfs_device *device)
{
	struct btrfs_device *next_device;

	list_for_each_entry(next_device, &fs_devs->devices, dev_list) {
		if (next_device != device &&
		    !test_bit(BTRFS_DEV_STATE_MISSING, &next_device->dev_state)
		    && next_device->bdev)
			return next_device;
	}

	return NULL;
}

/*
 * Helper function to check if the given device is part of s_bdev / latest_bdev
 * and replace it with the provided or the next active device, in the context
 * where this function called, there should be always be another device (or
 * this_dev) which is active.
 */
void btrfs_assign_next_active_device(struct btrfs_device *device,
				     struct btrfs_device *this_dev)
{
	struct btrfs_fs_info *fs_info = device->fs_info;
	struct btrfs_device *next_device;

	if (this_dev)
		next_device = this_dev;
	else
		next_device = btrfs_find_next_active_device(fs_info->fs_devices,
								device);
	ASSERT(next_device);

	if (fs_info->sb->s_bdev &&
			(fs_info->sb->s_bdev == device->bdev))
		fs_info->sb->s_bdev = next_device->bdev;

	if (fs_info->fs_devices->latest_bdev == device->bdev)
		fs_info->fs_devices->latest_bdev = next_device->bdev;
}

/*
 * Return btrfs_fs_devices::num_devices excluding the device that's being
 * currently replaced.
 */
static u64 btrfs_num_devices(struct btrfs_fs_info *fs_info)
{
	u64 num_devices = fs_info->fs_devices->num_devices;

	down_read(&fs_info->dev_replace.rwsem);
	if (btrfs_dev_replace_is_ongoing(&fs_info->dev_replace)) {
		ASSERT(num_devices > 1);
		num_devices--;
	}
	up_read(&fs_info->dev_replace.rwsem);

	return num_devices;
}

int btrfs_rm_device(struct btrfs_fs_info *fs_info, const char *device_path,
		u64 devid)
{
	struct btrfs_device *device;
	struct btrfs_fs_devices *cur_devices;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	u64 num_devices;
	int ret = 0;

	mutex_lock(&uuid_mutex);

	num_devices = btrfs_num_devices(fs_info);

	ret = btrfs_check_raid_min_devices(fs_info, num_devices - 1);
	if (ret)
		goto out;

	device = btrfs_find_device_by_devspec(fs_info, devid, device_path);

	if (IS_ERR(device)) {
		if (PTR_ERR(device) == -ENOENT &&
		    strcmp(device_path, "missing") == 0)
			ret = BTRFS_ERROR_DEV_MISSING_NOT_FOUND;
		else
			ret = PTR_ERR(device);
		goto out;
	}

	if (btrfs_pinned_by_swapfile(fs_info, device)) {
		btrfs_warn_in_rcu(fs_info,
		  "cannot remove device %s (devid %llu) due to active swapfile",
				  rcu_str_deref(device->name), device->devid);
		ret = -ETXTBSY;
		goto out;
	}

	if (test_bit(BTRFS_DEV_STATE_REPLACE_TGT, &device->dev_state)) {
		ret = BTRFS_ERROR_DEV_TGT_REPLACE;
		goto out;
	}

	if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state) &&
	    fs_info->fs_devices->rw_devices == 1) {
		ret = BTRFS_ERROR_DEV_ONLY_WRITABLE;
		goto out;
	}

	if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state)) {
		mutex_lock(&fs_info->chunk_mutex);
		list_del_init(&device->dev_alloc_list);
		device->fs_devices->rw_devices--;
		mutex_unlock(&fs_info->chunk_mutex);
	}

	mutex_unlock(&uuid_mutex);
	ret = btrfs_shrink_device(device, 0);
	mutex_lock(&uuid_mutex);
	if (ret)
		goto error_undo;

	/*
	 * TODO: the superblock still includes this device in its num_devices
	 * counter although write_all_supers() is not locked out. This
	 * could give a filesystem state which requires a degraded mount.
	 */
	ret = btrfs_rm_dev_item(device);
	if (ret)
		goto error_undo;

	clear_bit(BTRFS_DEV_STATE_IN_FS_METADATA, &device->dev_state);
	btrfs_scrub_cancel_dev(device);

	/*
	 * the device list mutex makes sure that we don't change
	 * the device list while someone else is writing out all
	 * the device supers. Whoever is writing all supers, should
	 * lock the device list mutex before getting the number of
	 * devices in the super block (super_copy). Conversely,
	 * whoever updates the number of devices in the super block
	 * (super_copy) should hold the device list mutex.
	 */

	/*
	 * In normal cases the cur_devices == fs_devices. But in case
	 * of deleting a seed device, the cur_devices should point to
	 * its own fs_devices listed under the fs_devices->seed.
	 */
	cur_devices = device->fs_devices;
	mutex_lock(&fs_devices->device_list_mutex);
	list_del_rcu(&device->dev_list);

	cur_devices->num_devices--;
	cur_devices->total_devices--;
	/* Update total_devices of the parent fs_devices if it's seed */
	if (cur_devices != fs_devices)
		fs_devices->total_devices--;

	if (test_bit(BTRFS_DEV_STATE_MISSING, &device->dev_state))
		cur_devices->missing_devices--;

	btrfs_assign_next_active_device(device, NULL);

	if (device->bdev) {
		cur_devices->open_devices--;
		/* remove sysfs entry */
		btrfs_sysfs_rm_device_link(fs_devices, device);
	}

	num_devices = btrfs_super_num_devices(fs_info->super_copy) - 1;
	btrfs_set_super_num_devices(fs_info->super_copy, num_devices);
	mutex_unlock(&fs_devices->device_list_mutex);

	/*
	 * at this point, the device is zero sized and detached from
	 * the devices list.  All that's left is to zero out the old
	 * supers and free the device.
	 */
	if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state))
		btrfs_scratch_superblocks(device->bdev, device->name->str);

	btrfs_close_bdev(device);
	synchronize_rcu();
	btrfs_free_device(device);

	if (cur_devices->open_devices == 0) {
		while (fs_devices) {
			if (fs_devices->seed == cur_devices) {
				fs_devices->seed = cur_devices->seed;
				break;
			}
			fs_devices = fs_devices->seed;
		}
		cur_devices->seed = NULL;
		close_fs_devices(cur_devices);
		free_fs_devices(cur_devices);
	}

out:
	mutex_unlock(&uuid_mutex);
	return ret;

error_undo:
	if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state)) {
		mutex_lock(&fs_info->chunk_mutex);
		list_add(&device->dev_alloc_list,
			 &fs_devices->alloc_list);
		device->fs_devices->rw_devices++;
		mutex_unlock(&fs_info->chunk_mutex);
	}
	goto out;
}

void btrfs_rm_dev_replace_remove_srcdev(struct btrfs_device *srcdev)
{
	struct btrfs_fs_devices *fs_devices;

	lockdep_assert_held(&srcdev->fs_info->fs_devices->device_list_mutex);

	/*
	 * in case of fs with no seed, srcdev->fs_devices will point
	 * to fs_devices of fs_info. However when the dev being replaced is
	 * a seed dev it will point to the seed's local fs_devices. In short
	 * srcdev will have its correct fs_devices in both the cases.
	 */
	fs_devices = srcdev->fs_devices;

	list_del_rcu(&srcdev->dev_list);
	list_del(&srcdev->dev_alloc_list);
	fs_devices->num_devices--;
	if (test_bit(BTRFS_DEV_STATE_MISSING, &srcdev->dev_state))
		fs_devices->missing_devices--;

	if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &srcdev->dev_state))
		fs_devices->rw_devices--;

	if (srcdev->bdev)
		fs_devices->open_devices--;
}

void btrfs_rm_dev_replace_free_srcdev(struct btrfs_device *srcdev)
{
	struct btrfs_fs_info *fs_info = srcdev->fs_info;
	struct btrfs_fs_devices *fs_devices = srcdev->fs_devices;

	if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &srcdev->dev_state)) {
		/* zero out the old super if it is writable */
		btrfs_scratch_superblocks(srcdev->bdev, srcdev->name->str);
	}

	btrfs_close_bdev(srcdev);
	synchronize_rcu();
	btrfs_free_device(srcdev);

	/* if this is no devs we rather delete the fs_devices */
	if (!fs_devices->num_devices) {
		struct btrfs_fs_devices *tmp_fs_devices;

		/*
		 * On a mounted FS, num_devices can't be zero unless it's a
		 * seed. In case of a seed device being replaced, the replace
		 * target added to the sprout FS, so there will be no more
		 * device left under the seed FS.
		 */
		ASSERT(fs_devices->seeding);

		tmp_fs_devices = fs_info->fs_devices;
		while (tmp_fs_devices) {
			if (tmp_fs_devices->seed == fs_devices) {
				tmp_fs_devices->seed = fs_devices->seed;
				break;
			}
			tmp_fs_devices = tmp_fs_devices->seed;
		}
		fs_devices->seed = NULL;
		close_fs_devices(fs_devices);
		free_fs_devices(fs_devices);
	}
}

void btrfs_destroy_dev_replace_tgtdev(struct btrfs_device *tgtdev)
{
	struct btrfs_fs_devices *fs_devices = tgtdev->fs_info->fs_devices;

	WARN_ON(!tgtdev);
	mutex_lock(&fs_devices->device_list_mutex);

	btrfs_sysfs_rm_device_link(fs_devices, tgtdev);

	if (tgtdev->bdev)
		fs_devices->open_devices--;

	fs_devices->num_devices--;

	btrfs_assign_next_active_device(tgtdev, NULL);

	list_del_rcu(&tgtdev->dev_list);

	mutex_unlock(&fs_devices->device_list_mutex);

	/*
	 * The update_dev_time() with in btrfs_scratch_superblocks()
	 * may lead to a call to btrfs_show_devname() which will try
	 * to hold device_list_mutex. And here this device
	 * is already out of device list, so we don't have to hold
	 * the device_list_mutex lock.
	 */
	btrfs_scratch_superblocks(tgtdev->bdev, tgtdev->name->str);

	btrfs_close_bdev(tgtdev);
	synchronize_rcu();
	btrfs_free_device(tgtdev);
}

static struct btrfs_device *btrfs_find_device_by_path(
		struct btrfs_fs_info *fs_info, const char *device_path)
{
	int ret = 0;
	struct btrfs_super_block *disk_super;
	u64 devid;
	u8 *dev_uuid;
	struct block_device *bdev;
	struct buffer_head *bh;
	struct btrfs_device *device;

	ret = btrfs_get_bdev_and_sb(device_path, FMODE_READ,
				    fs_info->bdev_holder, 0, &bdev, &bh);
	if (ret)
		return ERR_PTR(ret);
	disk_super = (struct btrfs_super_block *)bh->b_data;
	devid = btrfs_stack_device_id(&disk_super->dev_item);
	dev_uuid = disk_super->dev_item.uuid;
	if (btrfs_fs_incompat(fs_info, METADATA_UUID))
		device = btrfs_find_device(fs_info->fs_devices, devid, dev_uuid,
					   disk_super->metadata_uuid, true);
	else
		device = btrfs_find_device(fs_info->fs_devices, devid, dev_uuid,
					   disk_super->fsid, true);

	brelse(bh);
	if (!device)
		device = ERR_PTR(-ENOENT);
	blkdev_put(bdev, FMODE_READ);
	return device;
}

/*
 * Lookup a device given by device id, or the path if the id is 0.
 */
struct btrfs_device *btrfs_find_device_by_devspec(
		struct btrfs_fs_info *fs_info, u64 devid,
		const char *device_path)
{
	struct btrfs_device *device;

	if (devid) {
		device = btrfs_find_device(fs_info->fs_devices, devid, NULL,
					   NULL, true);
		if (!device)
			return ERR_PTR(-ENOENT);
		return device;
	}

	if (!device_path || !device_path[0])
		return ERR_PTR(-EINVAL);

	if (strcmp(device_path, "missing") == 0) {
		/* Find first missing device */
		list_for_each_entry(device, &fs_info->fs_devices->devices,
				    dev_list) {
			if (test_bit(BTRFS_DEV_STATE_IN_FS_METADATA,
				     &device->dev_state) && !device->bdev)
				return device;
		}
		return ERR_PTR(-ENOENT);
	}

	return btrfs_find_device_by_path(fs_info, device_path);
}

/*
 * does all the dirty work required for changing file system's UUID.
 */
static int btrfs_prepare_sprout(struct btrfs_fs_info *fs_info)
{
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	struct btrfs_fs_devices *old_devices;
	struct btrfs_fs_devices *seed_devices;
	struct btrfs_super_block *disk_super = fs_info->super_copy;
	struct btrfs_device *device;
	u64 super_flags;

	lockdep_assert_held(&uuid_mutex);
	if (!fs_devices->seeding)
		return -EINVAL;

	seed_devices = alloc_fs_devices(NULL, NULL);
	if (IS_ERR(seed_devices))
		return PTR_ERR(seed_devices);

	old_devices = clone_fs_devices(fs_devices);
	if (IS_ERR(old_devices)) {
		kfree(seed_devices);
		return PTR_ERR(old_devices);
	}

	list_add(&old_devices->fs_list, &fs_uuids);

	memcpy(seed_devices, fs_devices, sizeof(*seed_devices));
	seed_devices->opened = 1;
	INIT_LIST_HEAD(&seed_devices->devices);
	INIT_LIST_HEAD(&seed_devices->alloc_list);
	mutex_init(&seed_devices->device_list_mutex);

	mutex_lock(&fs_devices->device_list_mutex);
	list_splice_init_rcu(&fs_devices->devices, &seed_devices->devices,
			      synchronize_rcu);
	list_for_each_entry(device, &seed_devices->devices, dev_list)
		device->fs_devices = seed_devices;

	mutex_lock(&fs_info->chunk_mutex);
	list_splice_init(&fs_devices->alloc_list, &seed_devices->alloc_list);
	mutex_unlock(&fs_info->chunk_mutex);

	fs_devices->seeding = 0;
	fs_devices->num_devices = 0;
	fs_devices->open_devices = 0;
	fs_devices->missing_devices = 0;
	fs_devices->rotating = 0;
	fs_devices->seed = seed_devices;

	generate_random_uuid(fs_devices->fsid);
	memcpy(fs_devices->metadata_uuid, fs_devices->fsid, BTRFS_FSID_SIZE);
	memcpy(disk_super->fsid, fs_devices->fsid, BTRFS_FSID_SIZE);
	mutex_unlock(&fs_devices->device_list_mutex);

	super_flags = btrfs_super_flags(disk_super) &
		      ~BTRFS_SUPER_FLAG_SEEDING;
	btrfs_set_super_flags(disk_super, super_flags);

	return 0;
}

/*
 * Store the expected generation for seed devices in device items.
 */
static int btrfs_finish_sprout(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *root = fs_info->chunk_root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_dev_item *dev_item;
	struct btrfs_device *device;
	struct btrfs_key key;
	u8 fs_uuid[BTRFS_FSID_SIZE];
	u8 dev_uuid[BTRFS_UUID_SIZE];
	u64 devid;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = BTRFS_DEV_ITEMS_OBJECTID;
	key.offset = 0;
	key.type = BTRFS_DEV_ITEM_KEY;

	while (1) {
		ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
		if (ret < 0)
			goto error;

		leaf = path->nodes[0];
next_slot:
		if (path->slots[0] >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret > 0)
				break;
			if (ret < 0)
				goto error;
			leaf = path->nodes[0];
			btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
			btrfs_release_path(path);
			continue;
		}

		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
		if (key.objectid != BTRFS_DEV_ITEMS_OBJECTID ||
		    key.type != BTRFS_DEV_ITEM_KEY)
			break;

		dev_item = btrfs_item_ptr(leaf, path->slots[0],
					  struct btrfs_dev_item);
		devid = btrfs_device_id(leaf, dev_item);
		read_extent_buffer(leaf, dev_uuid, btrfs_device_uuid(dev_item),
				   BTRFS_UUID_SIZE);
		read_extent_buffer(leaf, fs_uuid, btrfs_device_fsid(dev_item),
				   BTRFS_FSID_SIZE);
		device = btrfs_find_device(fs_info->fs_devices, devid, dev_uuid,
					   fs_uuid, true);
		BUG_ON(!device); /* Logic error */

		if (device->fs_devices->seeding) {
			btrfs_set_device_generation(leaf, dev_item,
						    device->generation);
			btrfs_mark_buffer_dirty(leaf);
		}

		path->slots[0]++;
		goto next_slot;
	}
	ret = 0;
error:
	btrfs_free_path(path);
	return ret;
}

int btrfs_init_new_device(struct btrfs_fs_info *fs_info, const char *device_path)
{
	struct btrfs_root *root = fs_info->dev_root;
	struct request_queue *q;
	struct btrfs_trans_handle *trans;
	struct btrfs_device *device;
	struct block_device *bdev;
	struct super_block *sb = fs_info->sb;
	struct rcu_string *name;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	u64 orig_super_total_bytes;
	u64 orig_super_num_devices;
	int seeding_dev = 0;
	int ret = 0;
	bool unlocked = false;

	if (sb_rdonly(sb) && !fs_devices->seeding)
		return -EROFS;

	bdev = blkdev_get_by_path(device_path, FMODE_WRITE | FMODE_EXCL,
				  fs_info->bdev_holder);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);

	if (fs_devices->seeding) {
		seeding_dev = 1;
		down_write(&sb->s_umount);
		mutex_lock(&uuid_mutex);
	}

	filemap_write_and_wait(bdev->bd_inode->i_mapping);

	mutex_lock(&fs_devices->device_list_mutex);
	list_for_each_entry(device, &fs_devices->devices, dev_list) {
		if (device->bdev == bdev) {
			ret = -EEXIST;
			mutex_unlock(
				&fs_devices->device_list_mutex);
			goto error;
		}
	}
	mutex_unlock(&fs_devices->device_list_mutex);

	device = btrfs_alloc_device(fs_info, NULL, NULL);
	if (IS_ERR(device)) {
		/* we can safely leave the fs_devices entry around */
		ret = PTR_ERR(device);
		goto error;
	}

	name = rcu_string_strdup(device_path, GFP_KERNEL);
	if (!name) {
		ret = -ENOMEM;
		goto error_free_device;
	}
	rcu_assign_pointer(device->name, name);

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto error_free_device;
	}

	q = bdev_get_queue(bdev);
	set_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state);
	device->generation = trans->transid;
	device->io_width = fs_info->sectorsize;
	device->io_align = fs_info->sectorsize;
	device->sector_size = fs_info->sectorsize;
	device->total_bytes = round_down(i_size_read(bdev->bd_inode),
					 fs_info->sectorsize);
	device->disk_total_bytes = device->total_bytes;
	device->commit_total_bytes = device->total_bytes;
	device->fs_info = fs_info;
	device->bdev = bdev;
	set_bit(BTRFS_DEV_STATE_IN_FS_METADATA, &device->dev_state);
	clear_bit(BTRFS_DEV_STATE_REPLACE_TGT, &device->dev_state);
	device->mode = FMODE_EXCL;
	device->dev_stats_valid = 1;
	set_blocksize(device->bdev, BTRFS_BDEV_BLOCKSIZE);

	if (seeding_dev) {
		sb->s_flags &= ~SB_RDONLY;
		ret = btrfs_prepare_sprout(fs_info);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto error_trans;
		}
	}

	device->fs_devices = fs_devices;

	mutex_lock(&fs_devices->device_list_mutex);
	mutex_lock(&fs_info->chunk_mutex);
	list_add_rcu(&device->dev_list, &fs_devices->devices);
	list_add(&device->dev_alloc_list, &fs_devices->alloc_list);
	fs_devices->num_devices++;
	fs_devices->open_devices++;
	fs_devices->rw_devices++;
	fs_devices->total_devices++;
	fs_devices->total_rw_bytes += device->total_bytes;

	atomic64_add(device->total_bytes, &fs_info->free_chunk_space);

	if (!blk_queue_nonrot(q))
		fs_devices->rotating = 1;

	orig_super_total_bytes = btrfs_super_total_bytes(fs_info->super_copy);
	btrfs_set_super_total_bytes(fs_info->super_copy,
		round_down(orig_super_total_bytes + device->total_bytes,
			   fs_info->sectorsize));

	orig_super_num_devices = btrfs_super_num_devices(fs_info->super_copy);
	btrfs_set_super_num_devices(fs_info->super_copy,
				    orig_super_num_devices + 1);

	/* add sysfs device entry */
	btrfs_sysfs_add_device_link(fs_devices, device);

	/*
	 * we've got more storage, clear any full flags on the space
	 * infos
	 */
	btrfs_clear_space_info_full(fs_info);

	mutex_unlock(&fs_info->chunk_mutex);
	mutex_unlock(&fs_devices->device_list_mutex);

	if (seeding_dev) {
		mutex_lock(&fs_info->chunk_mutex);
		ret = init_first_rw_device(trans);
		mutex_unlock(&fs_info->chunk_mutex);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto error_sysfs;
		}
	}

	ret = btrfs_add_dev_item(trans, device);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto error_sysfs;
	}

	if (seeding_dev) {
		ret = btrfs_finish_sprout(trans);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto error_sysfs;
		}

		btrfs_sysfs_update_sprout_fsid(fs_devices,
				fs_info->fs_devices->fsid);
	}

	ret = btrfs_commit_transaction(trans);

	if (seeding_dev) {
		mutex_unlock(&uuid_mutex);
		up_write(&sb->s_umount);
		unlocked = true;

		if (ret) /* transaction commit */
			return ret;

		ret = btrfs_relocate_sys_chunks(fs_info);
		if (ret < 0)
			btrfs_handle_fs_error(fs_info, ret,
				    "Failed to relocate sys chunks after device initialization. This can be fixed using the \"btrfs balance\" command.");
		trans = btrfs_attach_transaction(root);
		if (IS_ERR(trans)) {
			if (PTR_ERR(trans) == -ENOENT)
				return 0;
			ret = PTR_ERR(trans);
			trans = NULL;
			goto error_sysfs;
		}
		ret = btrfs_commit_transaction(trans);
	}

	/* Update ctime/mtime for libblkid */
	update_dev_time(device_path);
	return ret;

error_sysfs:
	btrfs_sysfs_rm_device_link(fs_devices, device);
	mutex_lock(&fs_info->fs_devices->device_list_mutex);
	mutex_lock(&fs_info->chunk_mutex);
	list_del_rcu(&device->dev_list);
	list_del(&device->dev_alloc_list);
	fs_info->fs_devices->num_devices--;
	fs_info->fs_devices->open_devices--;
	fs_info->fs_devices->rw_devices--;
	fs_info->fs_devices->total_devices--;
	fs_info->fs_devices->total_rw_bytes -= device->total_bytes;
	atomic64_sub(device->total_bytes, &fs_info->free_chunk_space);
	btrfs_set_super_total_bytes(fs_info->super_copy,
				    orig_super_total_bytes);
	btrfs_set_super_num_devices(fs_info->super_copy,
				    orig_super_num_devices);
	mutex_unlock(&fs_info->chunk_mutex);
	mutex_unlock(&fs_info->fs_devices->device_list_mutex);
error_trans:
	if (seeding_dev)
		sb->s_flags |= SB_RDONLY;
	if (trans)
		btrfs_end_transaction(trans);
error_free_device:
	btrfs_free_device(device);
error:
	blkdev_put(bdev, FMODE_EXCL);
	if (seeding_dev && !unlocked) {
		mutex_unlock(&uuid_mutex);
		up_write(&sb->s_umount);
	}
	return ret;
}

static noinline int btrfs_update_device(struct btrfs_trans_handle *trans,
					struct btrfs_device *device)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_root *root = device->fs_info->chunk_root;
	struct btrfs_dev_item *dev_item;
	struct extent_buffer *leaf;
	struct btrfs_key key;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = BTRFS_DEV_ITEMS_OBJECTID;
	key.type = BTRFS_DEV_ITEM_KEY;
	key.offset = device->devid;

	ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
	if (ret < 0)
		goto out;

	if (ret > 0) {
		ret = -ENOENT;
		goto out;
	}

	leaf = path->nodes[0];
	dev_item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_dev_item);

	btrfs_set_device_id(leaf, dev_item, device->devid);
	btrfs_set_device_type(leaf, dev_item, device->type);
	btrfs_set_device_io_align(leaf, dev_item, device->io_align);
	btrfs_set_device_io_width(leaf, dev_item, device->io_width);
	btrfs_set_device_sector_size(leaf, dev_item, device->sector_size);
	btrfs_set_device_total_bytes(leaf, dev_item,
				     btrfs_device_get_disk_total_bytes(device));
	btrfs_set_device_bytes_used(leaf, dev_item,
				    btrfs_device_get_bytes_used(device));
	btrfs_mark_buffer_dirty(leaf);

out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_grow_device(struct btrfs_trans_handle *trans,
		      struct btrfs_device *device, u64 new_size)
{
	struct btrfs_fs_info *fs_info = device->fs_info;
	struct btrfs_super_block *super_copy = fs_info->super_copy;
	u64 old_total;
	u64 diff;

	if (!test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state))
		return -EACCES;

	new_size = round_down(new_size, fs_info->sectorsize);

	mutex_lock(&fs_info->chunk_mutex);
	old_total = btrfs_super_total_bytes(super_copy);
	diff = round_down(new_size - device->total_bytes, fs_info->sectorsize);

	if (new_size <= device->total_bytes ||
	    test_bit(BTRFS_DEV_STATE_REPLACE_TGT, &device->dev_state)) {
		mutex_unlock(&fs_info->chunk_mutex);
		return -EINVAL;
	}

	btrfs_set_super_total_bytes(super_copy,
			round_down(old_total + diff, fs_info->sectorsize));
	device->fs_devices->total_rw_bytes += diff;

	btrfs_device_set_total_bytes(device, new_size);
	btrfs_device_set_disk_total_bytes(device, new_size);
	btrfs_clear_space_info_full(device->fs_info);
	if (list_empty(&device->post_commit_list))
		list_add_tail(&device->post_commit_list,
			      &trans->transaction->dev_update_list);
	mutex_unlock(&fs_info->chunk_mutex);

	return btrfs_update_device(trans, device);
}

static int btrfs_free_chunk(struct btrfs_trans_handle *trans, u64 chunk_offset)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *root = fs_info->chunk_root;
	int ret;
	struct btrfs_path *path;
	struct btrfs_key key;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = BTRFS_FIRST_CHUNK_TREE_OBJECTID;
	key.offset = chunk_offset;
	key.type = BTRFS_CHUNK_ITEM_KEY;

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0)
		goto out;
	else if (ret > 0) { /* Logic error or corruption */
		btrfs_handle_fs_error(fs_info, -ENOENT,
				      "Failed lookup while freeing chunk.");
		ret = -ENOENT;
		goto out;
	}

	ret = btrfs_del_item(trans, root, path);
	if (ret < 0)
		btrfs_handle_fs_error(fs_info, ret,
				      "Failed to delete chunk item.");
out:
	btrfs_free_path(path);
	return ret;
}

static int btrfs_del_sys_chunk(struct btrfs_fs_info *fs_info, u64 chunk_offset)
{
	struct btrfs_super_block *super_copy = fs_info->super_copy;
	struct btrfs_disk_key *disk_key;
	struct btrfs_chunk *chunk;
	u8 *ptr;
	int ret = 0;
	u32 num_stripes;
	u32 array_size;
	u32 len = 0;
	u32 cur;
	struct btrfs_key key;

	mutex_lock(&fs_info->chunk_mutex);
	array_size = btrfs_super_sys_array_size(super_copy);

	ptr = super_copy->sys_chunk_array;
	cur = 0;

	while (cur < array_size) {
		disk_key = (struct btrfs_disk_key *)ptr;
		btrfs_disk_key_to_cpu(&key, disk_key);

		len = sizeof(*disk_key);

		if (key.type == BTRFS_CHUNK_ITEM_KEY) {
			chunk = (struct btrfs_chunk *)(ptr + len);
			num_stripes = btrfs_stack_chunk_num_stripes(chunk);
			len += btrfs_chunk_item_size(num_stripes);
		} else {
			ret = -EIO;
			break;
		}
		if (key.objectid == BTRFS_FIRST_CHUNK_TREE_OBJECTID &&
		    key.offset == chunk_offset) {
			memmove(ptr, ptr + len, array_size - (cur + len));
			array_size -= len;
			btrfs_set_super_sys_array_size(super_copy, array_size);
		} else {
			ptr += len;
			cur += len;
		}
	}
	mutex_unlock(&fs_info->chunk_mutex);
	return ret;
}

/*
 * btrfs_get_chunk_map() - Find the mapping containing the given logical extent.
 * @logical: Logical block offset in bytes.
 * @length: Length of extent in bytes.
 *
 * Return: Chunk mapping or ERR_PTR.
 */
struct extent_map *btrfs_get_chunk_map(struct btrfs_fs_info *fs_info,
				       u64 logical, u64 length)
{
	struct extent_map_tree *em_tree;
	struct extent_map *em;

	em_tree = &fs_info->mapping_tree;
	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, logical, length);
	read_unlock(&em_tree->lock);

	if (!em) {
		btrfs_crit(fs_info, "unable to find logical %llu length %llu",
			   logical, length);
		return ERR_PTR(-EINVAL);
	}

	if (em->start > logical || em->start + em->len < logical) {
		btrfs_crit(fs_info,
			   "found a bad mapping, wanted %llu-%llu, found %llu-%llu",
			   logical, length, em->start, em->start + em->len);
		free_extent_map(em);
		return ERR_PTR(-EINVAL);
	}

	/* callers are responsible for dropping em's ref. */
	return em;
}

int btrfs_remove_chunk(struct btrfs_trans_handle *trans, u64 chunk_offset)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct extent_map *em;
	struct map_lookup *map;
	u64 dev_extent_len = 0;
	int i, ret = 0;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;

	em = btrfs_get_chunk_map(fs_info, chunk_offset, 1);
	if (IS_ERR(em)) {
		/*
		 * This is a logic error, but we don't want to just rely on the
		 * user having built with ASSERT enabled, so if ASSERT doesn't
		 * do anything we still error out.
		 */
		ASSERT(0);
		return PTR_ERR(em);
	}
	map = em->map_lookup;
	mutex_lock(&fs_info->chunk_mutex);
	check_system_chunk(trans, map->type);
	mutex_unlock(&fs_info->chunk_mutex);

	/*
	 * Take the device list mutex to prevent races with the final phase of
	 * a device replace operation that replaces the device object associated
	 * with map stripes (dev-replace.c:btrfs_dev_replace_finishing()).
	 */
	mutex_lock(&fs_devices->device_list_mutex);
	for (i = 0; i < map->num_stripes; i++) {
		struct btrfs_device *device = map->stripes[i].dev;
		ret = btrfs_free_dev_extent(trans, device,
					    map->stripes[i].physical,
					    &dev_extent_len);
		if (ret) {
			mutex_unlock(&fs_devices->device_list_mutex);
			btrfs_abort_transaction(trans, ret);
			goto out;
		}

		if (device->bytes_used > 0) {
			mutex_lock(&fs_info->chunk_mutex);
			btrfs_device_set_bytes_used(device,
					device->bytes_used - dev_extent_len);
			atomic64_add(dev_extent_len, &fs_info->free_chunk_space);
			btrfs_clear_space_info_full(fs_info);
			mutex_unlock(&fs_info->chunk_mutex);
		}

		ret = btrfs_update_device(trans, device);
		if (ret) {
			mutex_unlock(&fs_devices->device_list_mutex);
			btrfs_abort_transaction(trans, ret);
			goto out;
		}
	}
	mutex_unlock(&fs_devices->device_list_mutex);

	ret = btrfs_free_chunk(trans, chunk_offset);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	trace_btrfs_chunk_free(fs_info, map, chunk_offset, em->len);

	if (map->type & BTRFS_BLOCK_GROUP_SYSTEM) {
		ret = btrfs_del_sys_chunk(fs_info, chunk_offset);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto out;
		}
	}

	ret = btrfs_remove_block_group(trans, chunk_offset, em);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

out:
	/* once for us */
	free_extent_map(em);
	return ret;
}

static int btrfs_relocate_chunk(struct btrfs_fs_info *fs_info, u64 chunk_offset)
{
	struct btrfs_root *root = fs_info->chunk_root;
	struct btrfs_trans_handle *trans;
	int ret;

	/*
	 * Prevent races with automatic removal of unused block groups.
	 * After we relocate and before we remove the chunk with offset
	 * chunk_offset, automatic removal of the block group can kick in,
	 * resulting in a failure when calling btrfs_remove_chunk() below.
	 *
	 * Make sure to acquire this mutex before doing a tree search (dev
	 * or chunk trees) to find chunks. Otherwise the cleaner kthread might
	 * call btrfs_remove_chunk() (through btrfs_delete_unused_bgs()) after
	 * we release the path used to search the chunk/dev tree and before
	 * the current task acquires this mutex and calls us.
	 */
	lockdep_assert_held(&fs_info->delete_unused_bgs_mutex);

	/* step one, relocate all the extents inside this chunk */
	btrfs_scrub_pause(fs_info);
	ret = btrfs_relocate_block_group(fs_info, chunk_offset);
	btrfs_scrub_continue(fs_info);
	if (ret)
		return ret;

	trans = btrfs_start_trans_remove_block_group(root->fs_info,
						     chunk_offset);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		btrfs_handle_fs_error(root->fs_info, ret, NULL);
		return ret;
	}

	/*
	 * step two, delete the device extents and the
	 * chunk tree entries
	 */
	ret = btrfs_remove_chunk(trans, chunk_offset);
	btrfs_end_transaction(trans);
	return ret;
}

static int btrfs_relocate_sys_chunks(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *chunk_root = fs_info->chunk_root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_chunk *chunk;
	struct btrfs_key key;
	struct btrfs_key found_key;
	u64 chunk_type;
	bool retried = false;
	int failed = 0;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

again:
	key.objectid = BTRFS_FIRST_CHUNK_TREE_OBJECTID;
	key.offset = (u64)-1;
	key.type = BTRFS_CHUNK_ITEM_KEY;

	while (1) {
		mutex_lock(&fs_info->delete_unused_bgs_mutex);
		ret = btrfs_search_slot(NULL, chunk_root, &key, path, 0, 0);
		if (ret < 0) {
			mutex_unlock(&fs_info->delete_unused_bgs_mutex);
			goto error;
		}
		BUG_ON(ret == 0); /* Corruption */

		ret = btrfs_previous_item(chunk_root, path, key.objectid,
					  key.type);
		if (ret)
			mutex_unlock(&fs_info->delete_unused_bgs_mutex);
		if (ret < 0)
			goto error;
		if (ret > 0)
			break;

		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

		chunk = btrfs_item_ptr(leaf, path->slots[0],
				       struct btrfs_chunk);
		chunk_type = btrfs_chunk_type(leaf, chunk);
		btrfs_release_path(path);

		if (chunk_type & BTRFS_BLOCK_GROUP_SYSTEM) {
			ret = btrfs_relocate_chunk(fs_info, found_key.offset);
			if (ret == -ENOSPC)
				failed++;
			else
				BUG_ON(ret);
		}
		mutex_unlock(&fs_info->delete_unused_bgs_mutex);

		if (found_key.offset == 0)
			break;
		key.offset = found_key.offset - 1;
	}
	ret = 0;
	if (failed && !retried) {
		failed = 0;
		retried = true;
		goto again;
	} else if (WARN_ON(failed && retried)) {
		ret = -ENOSPC;
	}
error:
	btrfs_free_path(path);
	return ret;
}

/*
 * return 1 : allocate a data chunk successfully,
 * return <0: errors during allocating a data chunk,
 * return 0 : no need to allocate a data chunk.
 */
static int btrfs_may_alloc_data_chunk(struct btrfs_fs_info *fs_info,
				      u64 chunk_offset)
{
	struct btrfs_block_group_cache *cache;
	u64 bytes_used;
	u64 chunk_type;

	cache = btrfs_lookup_block_group(fs_info, chunk_offset);
	ASSERT(cache);
	chunk_type = cache->flags;
	btrfs_put_block_group(cache);

	if (chunk_type & BTRFS_BLOCK_GROUP_DATA) {
		spin_lock(&fs_info->data_sinfo->lock);
		bytes_used = fs_info->data_sinfo->bytes_used;
		spin_unlock(&fs_info->data_sinfo->lock);

		if (!bytes_used) {
			struct btrfs_trans_handle *trans;
			int ret;

			trans =	btrfs_join_transaction(fs_info->tree_root);
			if (IS_ERR(trans))
				return PTR_ERR(trans);

			ret = btrfs_force_chunk_alloc(trans,
						      BTRFS_BLOCK_GROUP_DATA);
			btrfs_end_transaction(trans);
			if (ret < 0)
				return ret;
			return 1;
		}
	}
	return 0;
}

static int insert_balance_item(struct btrfs_fs_info *fs_info,
			       struct btrfs_balance_control *bctl)
{
	struct btrfs_root *root = fs_info->tree_root;
	struct btrfs_trans_handle *trans;
	struct btrfs_balance_item *item;
	struct btrfs_disk_balance_args disk_bargs;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	int ret, err;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		btrfs_free_path(path);
		return PTR_ERR(trans);
	}

	key.objectid = BTRFS_BALANCE_OBJECTID;
	key.type = BTRFS_TEMPORARY_ITEM_KEY;
	key.offset = 0;

	ret = btrfs_insert_empty_item(trans, root, path, &key,
				      sizeof(*item));
	if (ret)
		goto out;

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_balance_item);

	memzero_extent_buffer(leaf, (unsigned long)item, sizeof(*item));

	btrfs_cpu_balance_args_to_disk(&disk_bargs, &bctl->data);
	btrfs_set_balance_data(leaf, item, &disk_bargs);
	btrfs_cpu_balance_args_to_disk(&disk_bargs, &bctl->meta);
	btrfs_set_balance_meta(leaf, item, &disk_bargs);
	btrfs_cpu_balance_args_to_disk(&disk_bargs, &bctl->sys);
	btrfs_set_balance_sys(leaf, item, &disk_bargs);

	btrfs_set_balance_flags(leaf, item, bctl->flags);

	btrfs_mark_buffer_dirty(leaf);
out:
	btrfs_free_path(path);
	err = btrfs_commit_transaction(trans);
	if (err && !ret)
		ret = err;
	return ret;
}

static int del_balance_item(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *root = fs_info->tree_root;
	struct btrfs_trans_handle *trans;
	struct btrfs_path *path;
	struct btrfs_key key;
	int ret, err;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		btrfs_free_path(path);
		return PTR_ERR(trans);
	}

	key.objectid = BTRFS_BALANCE_OBJECTID;
	key.type = BTRFS_TEMPORARY_ITEM_KEY;
	key.offset = 0;

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0)
		goto out;
	if (ret > 0) {
		ret = -ENOENT;
		goto out;
	}

	ret = btrfs_del_item(trans, root, path);
out:
	btrfs_free_path(path);
	err = btrfs_commit_transaction(trans);
	if (err && !ret)
		ret = err;
	return ret;
}

/*
 * This is a heuristic used to reduce the number of chunks balanced on
 * resume after balance was interrupted.
 */
static void update_balance_args(struct btrfs_balance_control *bctl)
{
	/*
	 * Turn on soft mode for chunk types that were being converted.
	 */
	if (bctl->data.flags & BTRFS_BALANCE_ARGS_CONVERT)
		bctl->data.flags |= BTRFS_BALANCE_ARGS_SOFT;
	if (bctl->sys.flags & BTRFS_BALANCE_ARGS_CONVERT)
		bctl->sys.flags |= BTRFS_BALANCE_ARGS_SOFT;
	if (bctl->meta.flags & BTRFS_BALANCE_ARGS_CONVERT)
		bctl->meta.flags |= BTRFS_BALANCE_ARGS_SOFT;

	/*
	 * Turn on usage filter if is not already used.  The idea is
	 * that chunks that we have already balanced should be
	 * reasonably full.  Don't do it for chunks that are being
	 * converted - that will keep us from relocating unconverted
	 * (albeit full) chunks.
	 */
	if (!(bctl->data.flags & BTRFS_BALANCE_ARGS_USAGE) &&
	    !(bctl->data.flags & BTRFS_BALANCE_ARGS_USAGE_RANGE) &&
	    !(bctl->data.flags & BTRFS_BALANCE_ARGS_CONVERT)) {
		bctl->data.flags |= BTRFS_BALANCE_ARGS_USAGE;
		bctl->data.usage = 90;
	}
	if (!(bctl->sys.flags & BTRFS_BALANCE_ARGS_USAGE) &&
	    !(bctl->sys.flags & BTRFS_BALANCE_ARGS_USAGE_RANGE) &&
	    !(bctl->sys.flags & BTRFS_BALANCE_ARGS_CONVERT)) {
		bctl->sys.flags |= BTRFS_BALANCE_ARGS_USAGE;
		bctl->sys.usage = 90;
	}
	if (!(bctl->meta.flags & BTRFS_BALANCE_ARGS_USAGE) &&
	    !(bctl->meta.flags & BTRFS_BALANCE_ARGS_USAGE_RANGE) &&
	    !(bctl->meta.flags & BTRFS_BALANCE_ARGS_CONVERT)) {
		bctl->meta.flags |= BTRFS_BALANCE_ARGS_USAGE;
		bctl->meta.usage = 90;
	}
}

/*
 * Clear the balance status in fs_info and delete the balance item from disk.
 */
static void reset_balance_state(struct btrfs_fs_info *fs_info)
{
	struct btrfs_balance_control *bctl = fs_info->balance_ctl;
	int ret;

	BUG_ON(!fs_info->balance_ctl);

	spin_lock(&fs_info->balance_lock);
	fs_info->balance_ctl = NULL;
	spin_unlock(&fs_info->balance_lock);

	kfree(bctl);
	ret = del_balance_item(fs_info);
	if (ret)
		btrfs_handle_fs_error(fs_info, ret, NULL);
}

/*
 * Balance filters.  Return 1 if chunk should be filtered out
 * (should not be balanced).
 */
static int chunk_profiles_filter(u64 chunk_type,
				 struct btrfs_balance_args *bargs)
{
	chunk_type = chunk_to_extended(chunk_type) &
				BTRFS_EXTENDED_PROFILE_MASK;

	if (bargs->profiles & chunk_type)
		return 0;

	return 1;
}

static int chunk_usage_range_filter(struct btrfs_fs_info *fs_info, u64 chunk_offset,
			      struct btrfs_balance_args *bargs)
{
	struct btrfs_block_group_cache *cache;
	u64 chunk_used;
	u64 user_thresh_min;
	u64 user_thresh_max;
	int ret = 1;

	cache = btrfs_lookup_block_group(fs_info, chunk_offset);
	chunk_used = btrfs_block_group_used(&cache->item);

	if (bargs->usage_min == 0)
		user_thresh_min = 0;
	else
		user_thresh_min = div_factor_fine(cache->key.offset,
					bargs->usage_min);

	if (bargs->usage_max == 0)
		user_thresh_max = 1;
	else if (bargs->usage_max > 100)
		user_thresh_max = cache->key.offset;
	else
		user_thresh_max = div_factor_fine(cache->key.offset,
					bargs->usage_max);

	if (user_thresh_min <= chunk_used && chunk_used < user_thresh_max)
		ret = 0;

	btrfs_put_block_group(cache);
	return ret;
}

static int chunk_usage_filter(struct btrfs_fs_info *fs_info,
		u64 chunk_offset, struct btrfs_balance_args *bargs)
{
	struct btrfs_block_group_cache *cache;
	u64 chunk_used, user_thresh;
	int ret = 1;

	cache = btrfs_lookup_block_group(fs_info, chunk_offset);
	chunk_used = btrfs_block_group_used(&cache->item);

	if (bargs->usage_min == 0)
		user_thresh = 1;
	else if (bargs->usage > 100)
		user_thresh = cache->key.offset;
	else
		user_thresh = div_factor_fine(cache->key.offset,
					      bargs->usage);

	if (chunk_used < user_thresh)
		ret = 0;

	btrfs_put_block_group(cache);
	return ret;
}

static int chunk_devid_filter(struct extent_buffer *leaf,
			      struct btrfs_chunk *chunk,
			      struct btrfs_balance_args *bargs)
{
	struct btrfs_stripe *stripe;
	int num_stripes = btrfs_chunk_num_stripes(leaf, chunk);
	int i;

	for (i = 0; i < num_stripes; i++) {
		stripe = btrfs_stripe_nr(chunk, i);
		if (btrfs_stripe_devid(leaf, stripe) == bargs->devid)
			return 0;
	}

	return 1;
}

static u64 calc_data_stripes(u64 type, int num_stripes)
{
	const int index = btrfs_bg_flags_to_raid_index(type);
	const int ncopies = btrfs_raid_array[index].ncopies;
	const int nparity = btrfs_raid_array[index].nparity;

	if (nparity)
		return num_stripes - nparity;
	else
		return num_stripes / ncopies;
}

/* [pstart, pend) */
static int chunk_drange_filter(struct extent_buffer *leaf,
			       struct btrfs_chunk *chunk,
			       struct btrfs_balance_args *bargs)
{
	struct btrfs_stripe *stripe;
	int num_stripes = btrfs_chunk_num_stripes(leaf, chunk);
	u64 stripe_offset;
	u64 stripe_length;
	u64 type;
	int factor;
	int i;

	if (!(bargs->flags & BTRFS_BALANCE_ARGS_DEVID))
		return 0;

	type = btrfs_chunk_type(leaf, chunk);
	factor = calc_data_stripes(type, num_stripes);

	for (i = 0; i < num_stripes; i++) {
		stripe = btrfs_stripe_nr(chunk, i);
		if (btrfs_stripe_devid(leaf, stripe) != bargs->devid)
			continue;

		stripe_offset = btrfs_stripe_offset(leaf, stripe);
		stripe_length = btrfs_chunk_length(leaf, chunk);
		stripe_length = div_u64(stripe_length, factor);

		if (stripe_offset < bargs->pend &&
		    stripe_offset + stripe_length > bargs->pstart)
			return 0;
	}

	return 1;
}

/* [vstart, vend) */
static int chunk_vrange_filter(struct extent_buffer *leaf,
			       struct btrfs_chunk *chunk,
			       u64 chunk_offset,
			       struct btrfs_balance_args *bargs)
{
	if (chunk_offset < bargs->vend &&
	    chunk_offset + btrfs_chunk_length(leaf, chunk) > bargs->vstart)
		/* at least part of the chunk is inside this vrange */
		return 0;

	return 1;
}

static int chunk_stripes_range_filter(struct extent_buffer *leaf,
			       struct btrfs_chunk *chunk,
			       struct btrfs_balance_args *bargs)
{
	int num_stripes = btrfs_chunk_num_stripes(leaf, chunk);

	if (bargs->stripes_min <= num_stripes
			&& num_stripes <= bargs->stripes_max)
		return 0;

	return 1;
}

static int chunk_soft_convert_filter(u64 chunk_type,
				     struct btrfs_balance_args *bargs)
{
	if (!(bargs->flags & BTRFS_BALANCE_ARGS_CONVERT))
		return 0;

	chunk_type = chunk_to_extended(chunk_type) &
				BTRFS_EXTENDED_PROFILE_MASK;

	if (bargs->target == chunk_type)
		return 1;

	return 0;
}

static int should_balance_chunk(struct extent_buffer *leaf,
				struct btrfs_chunk *chunk, u64 chunk_offset)
{
	struct btrfs_fs_info *fs_info = leaf->fs_info;
	struct btrfs_balance_control *bctl = fs_info->balance_ctl;
	struct btrfs_balance_args *bargs = NULL;
	u64 chunk_type = btrfs_chunk_type(leaf, chunk);

	/* type filter */
	if (!((chunk_type & BTRFS_BLOCK_GROUP_TYPE_MASK) &
	      (bctl->flags & BTRFS_BALANCE_TYPE_MASK))) {
		return 0;
	}

	if (chunk_type & BTRFS_BLOCK_GROUP_DATA)
		bargs = &bctl->data;
	else if (chunk_type & BTRFS_BLOCK_GROUP_SYSTEM)
		bargs = &bctl->sys;
	else if (chunk_type & BTRFS_BLOCK_GROUP_METADATA)
		bargs = &bctl->meta;

	/* profiles filter */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_PROFILES) &&
	    chunk_profiles_filter(chunk_type, bargs)) {
		return 0;
	}

	/* usage filter */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_USAGE) &&
	    chunk_usage_filter(fs_info, chunk_offset, bargs)) {
		return 0;
	} else if ((bargs->flags & BTRFS_BALANCE_ARGS_USAGE_RANGE) &&
	    chunk_usage_range_filter(fs_info, chunk_offset, bargs)) {
		return 0;
	}

	/* devid filter */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_DEVID) &&
	    chunk_devid_filter(leaf, chunk, bargs)) {
		return 0;
	}

	/* drange filter, makes sense only with devid filter */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_DRANGE) &&
	    chunk_drange_filter(leaf, chunk, bargs)) {
		return 0;
	}

	/* vrange filter */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_VRANGE) &&
	    chunk_vrange_filter(leaf, chunk, chunk_offset, bargs)) {
		return 0;
	}

	/* stripes filter */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_STRIPES_RANGE) &&
	    chunk_stripes_range_filter(leaf, chunk, bargs)) {
		return 0;
	}

	/* soft profile changing mode */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_SOFT) &&
	    chunk_soft_convert_filter(chunk_type, bargs)) {
		return 0;
	}

	/*
	 * limited by count, must be the last filter
	 */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_LIMIT)) {
		if (bargs->limit == 0)
			return 0;
		else
			bargs->limit--;
	} else if ((bargs->flags & BTRFS_BALANCE_ARGS_LIMIT_RANGE)) {
		/*
		 * Same logic as the 'limit' filter; the minimum cannot be
		 * determined here because we do not have the global information
		 * about the count of all chunks that satisfy the filters.
		 */
		if (bargs->limit_max == 0)
			return 0;
		else
			bargs->limit_max--;
	}

	return 1;
}

static int __btrfs_balance(struct btrfs_fs_info *fs_info)
{
	struct btrfs_balance_control *bctl = fs_info->balance_ctl;
	struct btrfs_root *chunk_root = fs_info->chunk_root;
	u64 chunk_type;
	struct btrfs_chunk *chunk;
	struct btrfs_path *path = NULL;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *leaf;
	int slot;
	int ret;
	int enospc_errors = 0;
	bool counting = true;
	/* The single value limit and min/max limits use the same bytes in the */
	u64 limit_data = bctl->data.limit;
	u64 limit_meta = bctl->meta.limit;
	u64 limit_sys = bctl->sys.limit;
	u32 count_data = 0;
	u32 count_meta = 0;
	u32 count_sys = 0;
	int chunk_reserved = 0;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto error;
	}

	/* zero out stat counters */
	spin_lock(&fs_info->balance_lock);
	memset(&bctl->stat, 0, sizeof(bctl->stat));
	spin_unlock(&fs_info->balance_lock);
again:
	if (!counting) {
		/*
		 * The single value limit and min/max limits use the same bytes
		 * in the
		 */
		bctl->data.limit = limit_data;
		bctl->meta.limit = limit_meta;
		bctl->sys.limit = limit_sys;
	}
	key.objectid = BTRFS_FIRST_CHUNK_TREE_OBJECTID;
	key.offset = (u64)-1;
	key.type = BTRFS_CHUNK_ITEM_KEY;

	while (1) {
		if ((!counting && atomic_read(&fs_info->balance_pause_req)) ||
		    atomic_read(&fs_info->balance_cancel_req)) {
			ret = -ECANCELED;
			goto error;
		}

		mutex_lock(&fs_info->delete_unused_bgs_mutex);
		ret = btrfs_search_slot(NULL, chunk_root, &key, path, 0, 0);
		if (ret < 0) {
			mutex_unlock(&fs_info->delete_unused_bgs_mutex);
			goto error;
		}

		/*
		 * this shouldn't happen, it means the last relocate
		 * failed
		 */
		if (ret == 0)
			BUG(); /* FIXME break ? */

		ret = btrfs_previous_item(chunk_root, path, 0,
					  BTRFS_CHUNK_ITEM_KEY);
		if (ret) {
			mutex_unlock(&fs_info->delete_unused_bgs_mutex);
			ret = 0;
			break;
		}

		leaf = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		if (found_key.objectid != key.objectid) {
			mutex_unlock(&fs_info->delete_unused_bgs_mutex);
			break;
		}

		chunk = btrfs_item_ptr(leaf, slot, struct btrfs_chunk);
		chunk_type = btrfs_chunk_type(leaf, chunk);

		if (!counting) {
			spin_lock(&fs_info->balance_lock);
			bctl->stat.considered++;
			spin_unlock(&fs_info->balance_lock);
		}

		ret = should_balance_chunk(leaf, chunk, found_key.offset);

		btrfs_release_path(path);
		if (!ret) {
			mutex_unlock(&fs_info->delete_unused_bgs_mutex);
			goto loop;
		}

		if (counting) {
			mutex_unlock(&fs_info->delete_unused_bgs_mutex);
			spin_lock(&fs_info->balance_lock);
			bctl->stat.expected++;
			spin_unlock(&fs_info->balance_lock);

			if (chunk_type & BTRFS_BLOCK_GROUP_DATA)
				count_data++;
			else if (chunk_type & BTRFS_BLOCK_GROUP_SYSTEM)
				count_sys++;
			else if (chunk_type & BTRFS_BLOCK_GROUP_METADATA)
				count_meta++;

			goto loop;
		}

		/*
		 * Apply limit_min filter, no need to check if the LIMITS
		 * filter is used, limit_min is 0 by default
		 */
		if (((chunk_type & BTRFS_BLOCK_GROUP_DATA) &&
					count_data < bctl->data.limit_min)
				|| ((chunk_type & BTRFS_BLOCK_GROUP_METADATA) &&
					count_meta < bctl->meta.limit_min)
				|| ((chunk_type & BTRFS_BLOCK_GROUP_SYSTEM) &&
					count_sys < bctl->sys.limit_min)) {
			mutex_unlock(&fs_info->delete_unused_bgs_mutex);
			goto loop;
		}

		if (!chunk_reserved) {
			/*
			 * We may be relocating the only data chunk we have,
			 * which could potentially end up with losing data's
			 * raid profile, so lets allocate an empty one in
			 * advance.
			 */
			ret = btrfs_may_alloc_data_chunk(fs_info,
							 found_key.offset);
			if (ret < 0) {
				mutex_unlock(&fs_info->delete_unused_bgs_mutex);
				goto error;
			} else if (ret == 1) {
				chunk_reserved = 1;
			}
		}

		ret = btrfs_relocate_chunk(fs_info, found_key.offset);
		mutex_unlock(&fs_info->delete_unused_bgs_mutex);
		if (ret == -ENOSPC) {
			enospc_errors++;
		} else if (ret == -ETXTBSY) {
			btrfs_info(fs_info,
	   "skipping relocation of block group %llu due to active swapfile",
				   found_key.offset);
			ret = 0;
		} else if (ret) {
			goto error;
		} else {
			spin_lock(&fs_info->balance_lock);
			bctl->stat.completed++;
			spin_unlock(&fs_info->balance_lock);
		}
loop:
		if (found_key.offset == 0)
			break;
		key.offset = found_key.offset - 1;
	}

	if (counting) {
		btrfs_release_path(path);
		counting = false;
		goto again;
	}
error:
	btrfs_free_path(path);
	if (enospc_errors) {
		btrfs_info(fs_info, "%d enospc errors during balance",
			   enospc_errors);
		if (!ret)
			ret = -ENOSPC;
	}

	return ret;
}

/**
 * alloc_profile_is_valid - see if a given profile is valid and reduced
 * @flags: profile to validate
 * @extended: if true @flags is treated as an extended profile
 */
static int alloc_profile_is_valid(u64 flags, int extended)
{
	u64 mask = (extended ? BTRFS_EXTENDED_PROFILE_MASK :
			       BTRFS_BLOCK_GROUP_PROFILE_MASK);

	flags &= ~BTRFS_BLOCK_GROUP_TYPE_MASK;

	/* 1) check that all other bits are zeroed */
	if (flags & ~mask)
		return 0;

	/* 2) see if profile is reduced */
	if (flags == 0)
		return !extended; /* "0" is valid for usual profiles */

	/* true if exactly one bit set */
	/*
	 * Don't use is_power_of_2(unsigned long) because it won't work
	 * for the single profile (1ULL << 48) on 32-bit CPUs.
	 */
	return flags != 0 && (flags & (flags - 1)) == 0;
}

static inline int balance_need_close(struct btrfs_fs_info *fs_info)
{
	/* cancel requested || normal exit path */
	return atomic_read(&fs_info->balance_cancel_req) ||
		(atomic_read(&fs_info->balance_pause_req) == 0 &&
		 atomic_read(&fs_info->balance_cancel_req) == 0);
}

/* Non-zero return value signifies invalidity */
static inline int validate_convert_profile(struct btrfs_balance_args *bctl_arg,
		u64 allowed)
{
	return ((bctl_arg->flags & BTRFS_BALANCE_ARGS_CONVERT) &&
		(!alloc_profile_is_valid(bctl_arg->target, 1) ||
		 (bctl_arg->target & ~allowed)));
}

/*
 * Fill @buf with textual description of balance filter flags @bargs, up to
 * @size_buf including the terminating null. The output may be trimmed if it
 * does not fit into the provided buffer.
 */
static void describe_balance_args(struct btrfs_balance_args *bargs, char *buf,
				 u32 size_buf)
{
	int ret;
	u32 size_bp = size_buf;
	char *bp = buf;
	u64 flags = bargs->flags;
	char tmp_buf[128] = {'\0'};

	if (!flags)
		return;

#define CHECK_APPEND_NOARG(a)						\
	do {								\
		ret = snprintf(bp, size_bp, (a));			\
		if (ret < 0 || ret >= size_bp)				\
			goto out_overflow;				\
		size_bp -= ret;						\
		bp += ret;						\
	} while (0)

#define CHECK_APPEND_1ARG(a, v1)					\
	do {								\
		ret = snprintf(bp, size_bp, (a), (v1));			\
		if (ret < 0 || ret >= size_bp)				\
			goto out_overflow;				\
		size_bp -= ret;						\
		bp += ret;						\
	} while (0)

#define CHECK_APPEND_2ARG(a, v1, v2)					\
	do {								\
		ret = snprintf(bp, size_bp, (a), (v1), (v2));		\
		if (ret < 0 || ret >= size_bp)				\
			goto out_overflow;				\
		size_bp -= ret;						\
		bp += ret;						\
	} while (0)

	if (flags & BTRFS_BALANCE_ARGS_CONVERT)
		CHECK_APPEND_1ARG("convert=%s,",
				  btrfs_bg_type_to_raid_name(bargs->target));

	if (flags & BTRFS_BALANCE_ARGS_SOFT)
		CHECK_APPEND_NOARG("soft,");

	if (flags & BTRFS_BALANCE_ARGS_PROFILES) {
		btrfs_describe_block_groups(bargs->profiles, tmp_buf,
					    sizeof(tmp_buf));
		CHECK_APPEND_1ARG("profiles=%s,", tmp_buf);
	}

	if (flags & BTRFS_BALANCE_ARGS_USAGE)
		CHECK_APPEND_1ARG("usage=%llu,", bargs->usage);

	if (flags & BTRFS_BALANCE_ARGS_USAGE_RANGE)
		CHECK_APPEND_2ARG("usage=%u..%u,",
				  bargs->usage_min, bargs->usage_max);

	if (flags & BTRFS_BALANCE_ARGS_DEVID)
		CHECK_APPEND_1ARG("devid=%llu,", bargs->devid);

	if (flags & BTRFS_BALANCE_ARGS_DRANGE)
		CHECK_APPEND_2ARG("drange=%llu..%llu,",
				  bargs->pstart, bargs->pend);

	if (flags & BTRFS_BALANCE_ARGS_VRANGE)
		CHECK_APPEND_2ARG("vrange=%llu..%llu,",
				  bargs->vstart, bargs->vend);

	if (flags & BTRFS_BALANCE_ARGS_LIMIT)
		CHECK_APPEND_1ARG("limit=%llu,", bargs->limit);

	if (flags & BTRFS_BALANCE_ARGS_LIMIT_RANGE)
		CHECK_APPEND_2ARG("limit=%u..%u,",
				bargs->limit_min, bargs->limit_max);

	if (flags & BTRFS_BALANCE_ARGS_STRIPES_RANGE)
		CHECK_APPEND_2ARG("stripes=%u..%u,",
				  bargs->stripes_min, bargs->stripes_max);

#undef CHECK_APPEND_2ARG
#undef CHECK_APPEND_1ARG
#undef CHECK_APPEND_NOARG

out_overflow:

	if (size_bp < size_buf)
		buf[size_buf - size_bp - 1] = '\0'; /* remove last , */
	else
		buf[0] = '\0';
}

static void describe_balance_start_or_resume(struct btrfs_fs_info *fs_info)
{
	u32 size_buf = 1024;
	char tmp_buf[192] = {'\0'};
	char *buf;
	char *bp;
	u32 size_bp = size_buf;
	int ret;
	struct btrfs_balance_control *bctl = fs_info->balance_ctl;

	buf = kzalloc(size_buf, GFP_KERNEL);
	if (!buf)
		return;

	bp = buf;

#define CHECK_APPEND_1ARG(a, v1)					\
	do {								\
		ret = snprintf(bp, size_bp, (a), (v1));			\
		if (ret < 0 || ret >= size_bp)				\
			goto out_overflow;				\
		size_bp -= ret;						\
		bp += ret;						\
	} while (0)

	if (bctl->flags & BTRFS_BALANCE_FORCE)
		CHECK_APPEND_1ARG("%s", "-f ");

	if (bctl->flags & BTRFS_BALANCE_DATA) {
		describe_balance_args(&bctl->data, tmp_buf, sizeof(tmp_buf));
		CHECK_APPEND_1ARG("-d%s ", tmp_buf);
	}

	if (bctl->flags & BTRFS_BALANCE_METADATA) {
		describe_balance_args(&bctl->meta, tmp_buf, sizeof(tmp_buf));
		CHECK_APPEND_1ARG("-m%s ", tmp_buf);
	}

	if (bctl->flags & BTRFS_BALANCE_SYSTEM) {
		describe_balance_args(&bctl->sys, tmp_buf, sizeof(tmp_buf));
		CHECK_APPEND_1ARG("-s%s ", tmp_buf);
	}

#undef CHECK_APPEND_1ARG

out_overflow:

	if (size_bp < size_buf)
		buf[size_buf - size_bp - 1] = '\0'; /* remove last " " */
	btrfs_info(fs_info, "balance: %s %s",
		   (bctl->flags & BTRFS_BALANCE_RESUME) ?
		   "resume" : "start", buf);

	kfree(buf);
}

/*
 * Should be called with balance mutexe held
 */
int btrfs_balance(struct btrfs_fs_info *fs_info,
		  struct btrfs_balance_control *bctl,
		  struct btrfs_ioctl_balance_args *bargs)
{
	u64 meta_target, data_target;
	u64 allowed;
	int mixed = 0;
	int ret;
	u64 num_devices;
	unsigned seq;
	bool reducing_integrity;
	int i;

	if (btrfs_fs_closing(fs_info) ||
	    atomic_read(&fs_info->balance_pause_req) ||
	    atomic_read(&fs_info->balance_cancel_req)) {
		ret = -EINVAL;
		goto out;
	}

	allowed = btrfs_super_incompat_flags(fs_info->super_copy);
	if (allowed & BTRFS_FEATURE_INCOMPAT_MIXED_GROUPS)
		mixed = 1;

	/*
	 * In case of mixed groups both data and meta should be picked,
	 * and identical options should be given for both of them.
	 */
	allowed = BTRFS_BALANCE_DATA | BTRFS_BALANCE_METADATA;
	if (mixed && (bctl->flags & allowed)) {
		if (!(bctl->flags & BTRFS_BALANCE_DATA) ||
		    !(bctl->flags & BTRFS_BALANCE_METADATA) ||
		    memcmp(&bctl->data, &bctl->meta, sizeof(bctl->data))) {
			btrfs_err(fs_info,
	  "balance: mixed groups data and metadata options must be the same");
			ret = -EINVAL;
			goto out;
		}
	}

	num_devices = btrfs_num_devices(fs_info);

	/*
	 * SINGLE profile on-disk has no profile bit, but in-memory we have a
	 * special bit for it, to make it easier to distinguish.  Thus we need
	 * to set it manually, or balance would refuse the profile.
	 */
	allowed = BTRFS_AVAIL_ALLOC_BIT_SINGLE;
	for (i = 0; i < ARRAY_SIZE(btrfs_raid_array); i++)
		if (num_devices >= btrfs_raid_array[i].devs_min)
			allowed |= btrfs_raid_array[i].bg_flag;

	if (validate_convert_profile(&bctl->data, allowed)) {
		btrfs_err(fs_info,
			  "balance: invalid convert data profile %s",
			  btrfs_bg_type_to_raid_name(bctl->data.target));
		ret = -EINVAL;
		goto out;
	}
	if (validate_convert_profile(&bctl->meta, allowed)) {
		btrfs_err(fs_info,
			  "balance: invalid convert metadata profile %s",
			  btrfs_bg_type_to_raid_name(bctl->meta.target));
		ret = -EINVAL;
		goto out;
	}
	if (validate_convert_profile(&bctl->sys, allowed)) {
		btrfs_err(fs_info,
			  "balance: invalid convert system profile %s",
			  btrfs_bg_type_to_raid_name(bctl->sys.target));
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Allow to reduce metadata or system integrity only if force set for
	 * profiles with redundancy (copies, parity)
	 */
	allowed = 0;
	for (i = 0; i < ARRAY_SIZE(btrfs_raid_array); i++) {
		if (btrfs_raid_array[i].ncopies >= 2 ||
		    btrfs_raid_array[i].tolerated_failures >= 1)
			allowed |= btrfs_raid_array[i].bg_flag;
	}
	do {
		seq = read_seqbegin(&fs_info->profiles_lock);

		if (((bctl->sys.flags & BTRFS_BALANCE_ARGS_CONVERT) &&
		     (fs_info->avail_system_alloc_bits & allowed) &&
		     !(bctl->sys.target & allowed)) ||
		    ((bctl->meta.flags & BTRFS_BALANCE_ARGS_CONVERT) &&
		     (fs_info->avail_metadata_alloc_bits & allowed) &&
		     !(bctl->meta.target & allowed)))
			reducing_integrity = true;
		else
			reducing_integrity = false;

		/* if we're not converting, the target field is uninitialized */
		meta_target = (bctl->meta.flags & BTRFS_BALANCE_ARGS_CONVERT) ?
			bctl->meta.target : fs_info->avail_metadata_alloc_bits;
		data_target = (bctl->data.flags & BTRFS_BALANCE_ARGS_CONVERT) ?
			bctl->data.target : fs_info->avail_data_alloc_bits;
	} while (read_seqretry(&fs_info->profiles_lock, seq));

	if (reducing_integrity) {
		if (bctl->flags & BTRFS_BALANCE_FORCE) {
			btrfs_info(fs_info,
				   "balance: force reducing metadata integrity");
		} else {
			btrfs_err(fs_info,
	  "balance: reduces metadata integrity, use --force if you want this");
			ret = -EINVAL;
			goto out;
		}
	}

	if (btrfs_get_num_tolerated_disk_barrier_failures(meta_target) <
		btrfs_get_num_tolerated_disk_barrier_failures(data_target)) {
		btrfs_warn(fs_info,
	"balance: metadata profile %s has lower redundancy than data profile %s",
				btrfs_bg_type_to_raid_name(meta_target),
				btrfs_bg_type_to_raid_name(data_target));
	}

	if (fs_info->send_in_progress) {
		btrfs_warn_rl(fs_info,
"cannot run balance while send operations are in progress (%d in progress)",
			      fs_info->send_in_progress);
		ret = -EAGAIN;
		goto out;
	}

	ret = insert_balance_item(fs_info, bctl);
	if (ret && ret != -EEXIST)
		goto out;

	if (!(bctl->flags & BTRFS_BALANCE_RESUME)) {
		BUG_ON(ret == -EEXIST);
		BUG_ON(fs_info->balance_ctl);
		spin_lock(&fs_info->balance_lock);
		fs_info->balance_ctl = bctl;
		spin_unlock(&fs_info->balance_lock);
	} else {
		BUG_ON(ret != -EEXIST);
		spin_lock(&fs_info->balance_lock);
		update_balance_args(bctl);
		spin_unlock(&fs_info->balance_lock);
	}

	ASSERT(!test_bit(BTRFS_FS_BALANCE_RUNNING, &fs_info->flags));
	set_bit(BTRFS_FS_BALANCE_RUNNING, &fs_info->flags);
	describe_balance_start_or_resume(fs_info);
	mutex_unlock(&fs_info->balance_mutex);

	ret = __btrfs_balance(fs_info);

	mutex_lock(&fs_info->balance_mutex);
	if (ret == -ECANCELED && atomic_read(&fs_info->balance_pause_req))
		btrfs_info(fs_info, "balance: paused");
	else if (ret == -ECANCELED && atomic_read(&fs_info->balance_cancel_req))
		btrfs_info(fs_info, "balance: canceled");
	else
		btrfs_info(fs_info, "balance: ended with status: %d", ret);

	clear_bit(BTRFS_FS_BALANCE_RUNNING, &fs_info->flags);

	if (bargs) {
		memset(bargs, 0, sizeof(*bargs));
		btrfs_update_ioctl_balance_args(fs_info, bargs);
	}

	if ((ret && ret != -ECANCELED && ret != -ENOSPC) ||
	    balance_need_close(fs_info)) {
		reset_balance_state(fs_info);
		clear_bit(BTRFS_FS_EXCL_OP, &fs_info->flags);
	}

	wake_up(&fs_info->balance_wait_q);

	return ret;
out:
	if (bctl->flags & BTRFS_BALANCE_RESUME)
		reset_balance_state(fs_info);
	else
		kfree(bctl);
	clear_bit(BTRFS_FS_EXCL_OP, &fs_info->flags);

	return ret;
}

static int balance_kthread(void *data)
{
	struct btrfs_fs_info *fs_info = data;
	int ret = 0;

	mutex_lock(&fs_info->balance_mutex);
	if (fs_info->balance_ctl)
		ret = btrfs_balance(fs_info, fs_info->balance_ctl, NULL);
	mutex_unlock(&fs_info->balance_mutex);

	return ret;
}

int btrfs_resume_balance_async(struct btrfs_fs_info *fs_info)
{
	struct task_struct *tsk;

	mutex_lock(&fs_info->balance_mutex);
	if (!fs_info->balance_ctl) {
		mutex_unlock(&fs_info->balance_mutex);
		return 0;
	}
	mutex_unlock(&fs_info->balance_mutex);

	if (btrfs_test_opt(fs_info, SKIP_BALANCE)) {
		btrfs_info(fs_info, "balance: resume skipped");
		return 0;
	}

	/*
	 * A ro->rw remount sequence should continue with the paused balance
	 * regardless of who pauses it, system or the user as of now, so set
	 * the resume flag.
	 */
	spin_lock(&fs_info->balance_lock);
	fs_info->balance_ctl->flags |= BTRFS_BALANCE_RESUME;
	spin_unlock(&fs_info->balance_lock);

	tsk = kthread_run(balance_kthread, fs_info, "btrfs-balance");
	return PTR_ERR_OR_ZERO(tsk);
}

int btrfs_recover_balance(struct btrfs_fs_info *fs_info)
{
	struct btrfs_balance_control *bctl;
	struct btrfs_balance_item *item;
	struct btrfs_disk_balance_args disk_bargs;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = BTRFS_BALANCE_OBJECTID;
	key.type = BTRFS_TEMPORARY_ITEM_KEY;
	key.offset = 0;

	ret = btrfs_search_slot(NULL, fs_info->tree_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	if (ret > 0) { /* ret = -ENOENT; */
		ret = 0;
		goto out;
	}

	bctl = kzalloc(sizeof(*bctl), GFP_NOFS);
	if (!bctl) {
		ret = -ENOMEM;
		goto out;
	}

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_balance_item);

	bctl->flags = btrfs_balance_flags(leaf, item);
	bctl->flags |= BTRFS_BALANCE_RESUME;

	btrfs_balance_data(leaf, item, &disk_bargs);
	btrfs_disk_balance_args_to_cpu(&bctl->data, &disk_bargs);
	btrfs_balance_meta(leaf, item, &disk_bargs);
	btrfs_disk_balance_args_to_cpu(&bctl->meta, &disk_bargs);
	btrfs_balance_sys(leaf, item, &disk_bargs);
	btrfs_disk_balance_args_to_cpu(&bctl->sys, &disk_bargs);

	/*
	 * This should never happen, as the paused balance state is recovered
	 * during mount without any chance of other exclusive ops to collide.
	 *
	 * This gives the exclusive op status to balance and keeps in paused
	 * state until user intervention (cancel or umount). If the ownership
	 * cannot be assigned, show a message but do not fail. The balance
	 * is in a paused state and must have fs_info::balance_ctl properly
	 * set up.
	 */
	if (test_and_set_bit(BTRFS_FS_EXCL_OP, &fs_info->flags))
		btrfs_warn(fs_info,
	"balance: cannot set exclusive op status, resume manually");

	mutex_lock(&fs_info->balance_mutex);
	BUG_ON(fs_info->balance_ctl);
	spin_lock(&fs_info->balance_lock);
	fs_info->balance_ctl = bctl;
	spin_unlock(&fs_info->balance_lock);
	mutex_unlock(&fs_info->balance_mutex);
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_pause_balance(struct btrfs_fs_info *fs_info)
{
	int ret = 0;

	mutex_lock(&fs_info->balance_mutex);
	if (!fs_info->balance_ctl) {
		mutex_unlock(&fs_info->balance_mutex);
		return -ENOTCONN;
	}

	if (test_bit(BTRFS_FS_BALANCE_RUNNING, &fs_info->flags)) {
		atomic_inc(&fs_info->balance_pause_req);
		mutex_unlock(&fs_info->balance_mutex);

		wait_event(fs_info->balance_wait_q,
			   !test_bit(BTRFS_FS_BALANCE_RUNNING, &fs_info->flags));

		mutex_lock(&fs_info->balance_mutex);
		/* we are good with balance_ctl ripped off from under us */
		BUG_ON(test_bit(BTRFS_FS_BALANCE_RUNNING, &fs_info->flags));
		atomic_dec(&fs_info->balance_pause_req);
	} else {
		ret = -ENOTCONN;
	}

	mutex_unlock(&fs_info->balance_mutex);
	return ret;
}

int btrfs_cancel_balance(struct btrfs_fs_info *fs_info)
{
	mutex_lock(&fs_info->balance_mutex);
	if (!fs_info->balance_ctl) {
		mutex_unlock(&fs_info->balance_mutex);
		return -ENOTCONN;
	}

	/*
	 * A paused balance with the item stored on disk can be resumed at
	 * mount time if the mount is read-write. Otherwise it's still paused
	 * and we must not allow cancelling as it deletes the item.
	 */
	if (sb_rdonly(fs_info->sb)) {
		mutex_unlock(&fs_info->balance_mutex);
		return -EROFS;
	}

	atomic_inc(&fs_info->balance_cancel_req);
	/*
	 * if we are running just wait and return, balance item is
	 * deleted in btrfs_balance in this case
	 */
	if (test_bit(BTRFS_FS_BALANCE_RUNNING, &fs_info->flags)) {
		mutex_unlock(&fs_info->balance_mutex);
		wait_event(fs_info->balance_wait_q,
			   !test_bit(BTRFS_FS_BALANCE_RUNNING, &fs_info->flags));
		mutex_lock(&fs_info->balance_mutex);
	} else {
		mutex_unlock(&fs_info->balance_mutex);
		/*
		 * Lock released to allow other waiters to continue, we'll
		 * reexamine the status again.
		 */
		mutex_lock(&fs_info->balance_mutex);

		if (fs_info->balance_ctl) {
			reset_balance_state(fs_info);
			clear_bit(BTRFS_FS_EXCL_OP, &fs_info->flags);
			btrfs_info(fs_info, "balance: canceled");
		}
	}

	BUG_ON(fs_info->balance_ctl ||
		test_bit(BTRFS_FS_BALANCE_RUNNING, &fs_info->flags));
	atomic_dec(&fs_info->balance_cancel_req);
	mutex_unlock(&fs_info->balance_mutex);
	return 0;
}

static int btrfs_uuid_scan_kthread(void *data)
{
	struct btrfs_fs_info *fs_info = data;
	struct btrfs_root *root = fs_info->tree_root;
	struct btrfs_key key;
	struct btrfs_path *path = NULL;
	int ret = 0;
	struct extent_buffer *eb;
	int slot;
	struct btrfs_root_item root_item;
	u32 item_size;
	struct btrfs_trans_handle *trans = NULL;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = 0;
	key.type = BTRFS_ROOT_ITEM_KEY;
	key.offset = 0;

	while (1) {
		ret = btrfs_search_forward(root, &key, path,
				BTRFS_OLDEST_GENERATION);
		if (ret) {
			if (ret > 0)
				ret = 0;
			break;
		}

		if (key.type != BTRFS_ROOT_ITEM_KEY ||
		    (key.objectid < BTRFS_FIRST_FREE_OBJECTID &&
		     key.objectid != BTRFS_FS_TREE_OBJECTID) ||
		    key.objectid > BTRFS_LAST_FREE_OBJECTID)
			goto skip;

		eb = path->nodes[0];
		slot = path->slots[0];
		item_size = btrfs_item_size_nr(eb, slot);
		if (item_size < sizeof(root_item))
			goto skip;

		read_extent_buffer(eb, &root_item,
				   btrfs_item_ptr_offset(eb, slot),
				   (int)sizeof(root_item));
		if (btrfs_root_refs(&root_item) == 0)
			goto skip;

		if (!btrfs_is_empty_uuid(root_item.uuid) ||
		    !btrfs_is_empty_uuid(root_item.received_uuid)) {
			if (trans)
				goto update_tree;

			btrfs_release_path(path);
			/*
			 * 1 - subvol uuid item
			 * 1 - received_subvol uuid item
			 */
			trans = btrfs_start_transaction(fs_info->uuid_root, 2);
			if (IS_ERR(trans)) {
				ret = PTR_ERR(trans);
				break;
			}
			continue;
		} else {
			goto skip;
		}
update_tree:
		if (!btrfs_is_empty_uuid(root_item.uuid)) {
			ret = btrfs_uuid_tree_add(trans, root_item.uuid,
						  BTRFS_UUID_KEY_SUBVOL,
						  key.objectid);
			if (ret < 0) {
				btrfs_warn(fs_info, "uuid_tree_add failed %d",
					ret);
				break;
			}
		}

		if (!btrfs_is_empty_uuid(root_item.received_uuid)) {
			ret = btrfs_uuid_tree_add(trans,
						  root_item.received_uuid,
						 BTRFS_UUID_KEY_RECEIVED_SUBVOL,
						  key.objectid);
			if (ret < 0) {
				btrfs_warn(fs_info, "uuid_tree_add failed %d",
					ret);
				break;
			}
		}

skip:
		if (trans) {
			ret = btrfs_end_transaction(trans);
			trans = NULL;
			if (ret)
				break;
		}

		btrfs_release_path(path);
		if (key.offset < (u64)-1) {
			key.offset++;
		} else if (key.type < BTRFS_ROOT_ITEM_KEY) {
			key.offset = 0;
			key.type = BTRFS_ROOT_ITEM_KEY;
		} else if (key.objectid < (u64)-1) {
			key.offset = 0;
			key.type = BTRFS_ROOT_ITEM_KEY;
			key.objectid++;
		} else {
			break;
		}
		cond_resched();
	}

out:
	btrfs_free_path(path);
	if (trans && !IS_ERR(trans))
		btrfs_end_transaction(trans);
	if (ret)
		btrfs_warn(fs_info, "btrfs_uuid_scan_kthread failed %d", ret);
	else
		set_bit(BTRFS_FS_UPDATE_UUID_TREE_GEN, &fs_info->flags);
	up(&fs_info->uuid_tree_rescan_sem);
	return 0;
}

/*
 * Callback for btrfs_uuid_tree_iterate().
 * returns:
 * 0	check succeeded, the entry is not outdated.
 * < 0	if an error occurred.
 * > 0	if the check failed, which means the caller shall remove the entry.
 */
static int btrfs_check_uuid_tree_entry(struct btrfs_fs_info *fs_info,
				       u8 *uuid, u8 type, u64 subid)
{
	struct btrfs_key key;
	int ret = 0;
	struct btrfs_root *subvol_root;

	if (type != BTRFS_UUID_KEY_SUBVOL &&
	    type != BTRFS_UUID_KEY_RECEIVED_SUBVOL)
		goto out;

	key.objectid = subid;
	key.type = BTRFS_ROOT_ITEM_KEY;
	key.offset = (u64)-1;
	subvol_root = btrfs_read_fs_root_no_name(fs_info, &key);
	if (IS_ERR(subvol_root)) {
		ret = PTR_ERR(subvol_root);
		if (ret == -ENOENT)
			ret = 1;
		goto out;
	}

	switch (type) {
	case BTRFS_UUID_KEY_SUBVOL:
		if (memcmp(uuid, subvol_root->root_item.uuid, BTRFS_UUID_SIZE))
			ret = 1;
		break;
	case BTRFS_UUID_KEY_RECEIVED_SUBVOL:
		if (memcmp(uuid, subvol_root->root_item.received_uuid,
			   BTRFS_UUID_SIZE))
			ret = 1;
		break;
	}

out:
	return ret;
}

static int btrfs_uuid_rescan_kthread(void *data)
{
	struct btrfs_fs_info *fs_info = (struct btrfs_fs_info *)data;
	int ret;

	/*
	 * 1st step is to iterate through the existing UUID tree and
	 * to delete all entries that contain outdated data.
	 * 2nd step is to add all missing entries to the UUID tree.
	 */
	ret = btrfs_uuid_tree_iterate(fs_info, btrfs_check_uuid_tree_entry);
	if (ret < 0) {
		btrfs_warn(fs_info, "iterating uuid_tree failed %d", ret);
		up(&fs_info->uuid_tree_rescan_sem);
		return ret;
	}
	return btrfs_uuid_scan_kthread(data);
}

int btrfs_create_uuid_tree(struct btrfs_fs_info *fs_info)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_root *uuid_root;
	struct task_struct *task;
	int ret;

	/*
	 * 1 - root node
	 * 1 - root item
	 */
	trans = btrfs_start_transaction(tree_root, 2);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	uuid_root = btrfs_create_tree(trans, BTRFS_UUID_TREE_OBJECTID);
	if (IS_ERR(uuid_root)) {
		ret = PTR_ERR(uuid_root);
		btrfs_abort_transaction(trans, ret);
		btrfs_end_transaction(trans);
		return ret;
	}

	fs_info->uuid_root = uuid_root;

	ret = btrfs_commit_transaction(trans);
	if (ret)
		return ret;

	down(&fs_info->uuid_tree_rescan_sem);
	task = kthread_run(btrfs_uuid_scan_kthread, fs_info, "btrfs-uuid");
	if (IS_ERR(task)) {
		/* fs_info->update_uuid_tree_gen remains 0 in all error case */
		btrfs_warn(fs_info, "failed to start uuid_scan task");
		up(&fs_info->uuid_tree_rescan_sem);
		return PTR_ERR(task);
	}

	return 0;
}

int btrfs_check_uuid_tree(struct btrfs_fs_info *fs_info)
{
	struct task_struct *task;

	down(&fs_info->uuid_tree_rescan_sem);
	task = kthread_run(btrfs_uuid_rescan_kthread, fs_info, "btrfs-uuid");
	if (IS_ERR(task)) {
		/* fs_info->update_uuid_tree_gen remains 0 in all error case */
		btrfs_warn(fs_info, "failed to start uuid_rescan task");
		up(&fs_info->uuid_tree_rescan_sem);
		return PTR_ERR(task);
	}

	return 0;
}

/*
 * shrinking a device means finding all of the device extents past
 * the new size, and then following the back refs to the chunks.
 * The chunk relocation code actually frees the device extent
 */
int btrfs_shrink_device(struct btrfs_device *device, u64 new_size)
{
	struct btrfs_fs_info *fs_info = device->fs_info;
	struct btrfs_root *root = fs_info->dev_root;
	struct btrfs_trans_handle *trans;
	struct btrfs_dev_extent *dev_extent = NULL;
	struct btrfs_path *path;
	u64 length;
	u64 chunk_offset;
	int ret;
	int slot;
	int failed = 0;
	bool retried = false;
	struct extent_buffer *l;
	struct btrfs_key key;
	struct btrfs_super_block *super_copy = fs_info->super_copy;
	u64 old_total = btrfs_super_total_bytes(super_copy);
	u64 old_size = btrfs_device_get_total_bytes(device);
	u64 diff;
	u64 start;

	new_size = round_down(new_size, fs_info->sectorsize);
	start = new_size;
	diff = round_down(old_size - new_size, fs_info->sectorsize);

	if (test_bit(BTRFS_DEV_STATE_REPLACE_TGT, &device->dev_state))
		return -EINVAL;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = READA_BACK;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		btrfs_free_path(path);
		return PTR_ERR(trans);
	}

	mutex_lock(&fs_info->chunk_mutex);

	btrfs_device_set_total_bytes(device, new_size);
	if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state)) {
		device->fs_devices->total_rw_bytes -= diff;
		atomic64_sub(diff, &fs_info->free_chunk_space);
	}

	/*
	 * Once the device's size has been set to the new size, ensure all
	 * in-memory chunks are synced to disk so that the loop below sees them
	 * and relocates them accordingly.
	 */
	if (contains_pending_extent(device, &start, diff)) {
		mutex_unlock(&fs_info->chunk_mutex);
		ret = btrfs_commit_transaction(trans);
		if (ret)
			goto done;
	} else {
		mutex_unlock(&fs_info->chunk_mutex);
		btrfs_end_transaction(trans);
	}

again:
	key.objectid = device->devid;
	key.offset = (u64)-1;
	key.type = BTRFS_DEV_EXTENT_KEY;

	do {
		mutex_lock(&fs_info->delete_unused_bgs_mutex);
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0) {
			mutex_unlock(&fs_info->delete_unused_bgs_mutex);
			goto done;
		}

		ret = btrfs_previous_item(root, path, 0, key.type);
		if (ret)
			mutex_unlock(&fs_info->delete_unused_bgs_mutex);
		if (ret < 0)
			goto done;
		if (ret) {
			ret = 0;
			btrfs_release_path(path);
			break;
		}

		l = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(l, &key, path->slots[0]);

		if (key.objectid != device->devid) {
			mutex_unlock(&fs_info->delete_unused_bgs_mutex);
			btrfs_release_path(path);
			break;
		}

		dev_extent = btrfs_item_ptr(l, slot, struct btrfs_dev_extent);
		length = btrfs_dev_extent_length(l, dev_extent);

		if (key.offset + length <= new_size) {
			mutex_unlock(&fs_info->delete_unused_bgs_mutex);
			btrfs_release_path(path);
			break;
		}

		chunk_offset = btrfs_dev_extent_chunk_offset(l, dev_extent);
		btrfs_release_path(path);

		/*
		 * We may be relocating the only data chunk we have,
		 * which could potentially end up with losing data's
		 * raid profile, so lets allocate an empty one in
		 * advance.
		 */
		ret = btrfs_may_alloc_data_chunk(fs_info, chunk_offset);
		if (ret < 0) {
			mutex_unlock(&fs_info->delete_unused_bgs_mutex);
			goto done;
		}

		ret = btrfs_relocate_chunk(fs_info, chunk_offset);
		mutex_unlock(&fs_info->delete_unused_bgs_mutex);
		if (ret == -ENOSPC) {
			failed++;
		} else if (ret) {
			if (ret == -ETXTBSY) {
				btrfs_warn(fs_info,
		   "could not shrink block group %llu due to active swapfile",
					   chunk_offset);
			}
			goto done;
		}
	} while (key.offset-- > 0);

	if (failed && !retried) {
		failed = 0;
		retried = true;
		goto again;
	} else if (failed && retried) {
		ret = -ENOSPC;
		goto done;
	}

	/* Shrinking succeeded, else we would be at "done". */
	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto done;
	}

	mutex_lock(&fs_info->chunk_mutex);
	btrfs_device_set_disk_total_bytes(device, new_size);
	if (list_empty(&device->post_commit_list))
		list_add_tail(&device->post_commit_list,
			      &trans->transaction->dev_update_list);

	WARN_ON(diff > old_total);
	btrfs_set_super_total_bytes(super_copy,
			round_down(old_total - diff, fs_info->sectorsize));
	mutex_unlock(&fs_info->chunk_mutex);

	/* Now btrfs_update_device() will change the on-disk size. */
	ret = btrfs_update_device(trans, device);
	if (ret < 0) {
		btrfs_abort_transaction(trans, ret);
		btrfs_end_transaction(trans);
	} else {
		ret = btrfs_commit_transaction(trans);
	}
done:
	btrfs_free_path(path);
	if (ret) {
		mutex_lock(&fs_info->chunk_mutex);
		btrfs_device_set_total_bytes(device, old_size);
		if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state))
			device->fs_devices->total_rw_bytes += diff;
		atomic64_add(diff, &fs_info->free_chunk_space);
		mutex_unlock(&fs_info->chunk_mutex);
	}
	return ret;
}

static int btrfs_add_system_chunk(struct btrfs_fs_info *fs_info,
			   struct btrfs_key *key,
			   struct btrfs_chunk *chunk, int item_size)
{
	struct btrfs_super_block *super_copy = fs_info->super_copy;
	struct btrfs_disk_key disk_key;
	u32 array_size;
	u8 *ptr;

	mutex_lock(&fs_info->chunk_mutex);
	array_size = btrfs_super_sys_array_size(super_copy);
	if (array_size + item_size + sizeof(disk_key)
			> BTRFS_SYSTEM_CHUNK_ARRAY_SIZE) {
		mutex_unlock(&fs_info->chunk_mutex);
		return -EFBIG;
	}

	ptr = super_copy->sys_chunk_array + array_size;
	btrfs_cpu_key_to_disk(&disk_key, key);
	memcpy(ptr, &disk_key, sizeof(disk_key));
	ptr += sizeof(disk_key);
	memcpy(ptr, chunk, item_size);
	item_size += sizeof(disk_key);
	btrfs_set_super_sys_array_size(super_copy, array_size + item_size);
	mutex_unlock(&fs_info->chunk_mutex);

	return 0;
}

/*
 * sort the devices in descending order by max_avail, total_avail
 */
static int btrfs_cmp_device_info(const void *a, const void *b)
{
	const struct btrfs_device_info *di_a = a;
	const struct btrfs_device_info *di_b = b;

	if (di_a->max_avail > di_b->max_avail)
		return -1;
	if (di_a->max_avail < di_b->max_avail)
		return 1;
	if (di_a->total_avail > di_b->total_avail)
		return -1;
	if (di_a->total_avail < di_b->total_avail)
		return 1;
	return 0;
}

static void check_raid56_incompat_flag(struct btrfs_fs_info *info, u64 type)
{
	if (!(type & BTRFS_BLOCK_GROUP_RAID56_MASK))
		return;

	btrfs_set_fs_incompat(info, RAID56);
}

static int __btrfs_alloc_chunk(struct btrfs_trans_handle *trans,
			       u64 start, u64 type)
{
	struct btrfs_fs_info *info = trans->fs_info;
	struct btrfs_fs_devices *fs_devices = info->fs_devices;
	struct btrfs_device *device;
	struct map_lookup *map = NULL;
	struct extent_map_tree *em_tree;
	struct extent_map *em;
	struct btrfs_device_info *devices_info = NULL;
	u64 total_avail;
	int num_stripes;	/* total number of stripes to allocate */
	int data_stripes;	/* number of stripes that count for
				   block group size */
	int sub_stripes;	/* sub_stripes info for map */
	int dev_stripes;	/* stripes per dev */
	int devs_max;		/* max devs to use */
	int devs_min;		/* min devs needed */
	int devs_increment;	/* ndevs has to be a multiple of this */
	int ncopies;		/* how many copies to data has */
	int nparity;		/* number of stripes worth of bytes to
				   store parity information */
	int ret;
	u64 max_stripe_size;
	u64 max_chunk_size;
	u64 stripe_size;
	u64 chunk_size;
	int ndevs;
	int i;
	int j;
	int index;

	BUG_ON(!alloc_profile_is_valid(type, 0));

	if (list_empty(&fs_devices->alloc_list)) {
		if (btrfs_test_opt(info, ENOSPC_DEBUG))
			btrfs_debug(info, "%s: no writable device", __func__);
		return -ENOSPC;
	}

	index = btrfs_bg_flags_to_raid_index(type);

	sub_stripes = btrfs_raid_array[index].sub_stripes;
	dev_stripes = btrfs_raid_array[index].dev_stripes;
	devs_max = btrfs_raid_array[index].devs_max;
	if (!devs_max)
		devs_max = BTRFS_MAX_DEVS(info);
	devs_min = btrfs_raid_array[index].devs_min;
	devs_increment = btrfs_raid_array[index].devs_increment;
	ncopies = btrfs_raid_array[index].ncopies;
	nparity = btrfs_raid_array[index].nparity;

	if (type & BTRFS_BLOCK_GROUP_DATA) {
		max_stripe_size = SZ_1G;
		max_chunk_size = BTRFS_MAX_DATA_CHUNK_SIZE;
	} else if (type & BTRFS_BLOCK_GROUP_METADATA) {
		/* for larger filesystems, use larger metadata chunks */
		if (fs_devices->total_rw_bytes > 50ULL * SZ_1G)
			max_stripe_size = SZ_1G;
		else
			max_stripe_size = SZ_256M;
		max_chunk_size = max_stripe_size;
	} else if (type & BTRFS_BLOCK_GROUP_SYSTEM) {
		max_stripe_size = SZ_32M;
		max_chunk_size = 2 * max_stripe_size;
	} else {
		btrfs_err(info, "invalid chunk type 0x%llx requested",
		       type);
		BUG();
	}

	/* We don't want a chunk larger than 10% of writable space */
	max_chunk_size = min(div_factor(fs_devices->total_rw_bytes, 1),
			     max_chunk_size);

	devices_info = kcalloc(fs_devices->rw_devices, sizeof(*devices_info),
			       GFP_NOFS);
	if (!devices_info)
		return -ENOMEM;

	/*
	 * in the first pass through the devices list, we gather information
	 * about the available holes on each device.
	 */
	ndevs = 0;
	list_for_each_entry(device, &fs_devices->alloc_list, dev_alloc_list) {
		u64 max_avail;
		u64 dev_offset;

		if (!test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state)) {
			WARN(1, KERN_ERR
			       "BTRFS: read-only device in alloc_list\n");
			continue;
		}

		if (!test_bit(BTRFS_DEV_STATE_IN_FS_METADATA,
					&device->dev_state) ||
		    test_bit(BTRFS_DEV_STATE_REPLACE_TGT, &device->dev_state))
			continue;

		if (device->total_bytes > device->bytes_used)
			total_avail = device->total_bytes - device->bytes_used;
		else
			total_avail = 0;

		/* If there is no space on this device, skip it. */
		if (total_avail == 0)
			continue;

		ret = find_free_dev_extent(device,
					   max_stripe_size * dev_stripes,
					   &dev_offset, &max_avail);
		if (ret && ret != -ENOSPC)
			goto error;

		if (ret == 0)
			max_avail = max_stripe_size * dev_stripes;

		if (max_avail < BTRFS_STRIPE_LEN * dev_stripes) {
			if (btrfs_test_opt(info, ENOSPC_DEBUG))
				btrfs_debug(info,
			"%s: devid %llu has no free space, have=%llu want=%u",
					    __func__, device->devid, max_avail,
					    BTRFS_STRIPE_LEN * dev_stripes);
			continue;
		}

		if (ndevs == fs_devices->rw_devices) {
			WARN(1, "%s: found more than %llu devices\n",
			     __func__, fs_devices->rw_devices);
			break;
		}
		devices_info[ndevs].dev_offset = dev_offset;
		devices_info[ndevs].max_avail = max_avail;
		devices_info[ndevs].total_avail = total_avail;
		devices_info[ndevs].dev = device;
		++ndevs;
	}

	/*
	 * now sort the devices by hole size / available space
	 */
	sort(devices_info, ndevs, sizeof(struct btrfs_device_info),
	     btrfs_cmp_device_info, NULL);

	/* round down to number of usable stripes */
	ndevs = round_down(ndevs, devs_increment);

	if (ndevs < devs_min) {
		ret = -ENOSPC;
		if (btrfs_test_opt(info, ENOSPC_DEBUG)) {
			btrfs_debug(info,
	"%s: not enough devices with free space: have=%d minimum required=%d",
				    __func__, ndevs, devs_min);
		}
		goto error;
	}

	ndevs = min(ndevs, devs_max);

	/*
	 * The primary goal is to maximize the number of stripes, so use as
	 * many devices as possible, even if the stripes are not maximum sized.
	 *
	 * The DUP profile stores more than one stripe per device, the
	 * max_avail is the total size so we have to adjust.
	 */
	stripe_size = div_u64(devices_info[ndevs - 1].max_avail, dev_stripes);
	num_stripes = ndevs * dev_stripes;

	/*
	 * this will have to be fixed for RAID1 and RAID10 over
	 * more drives
	 */
	data_stripes = (num_stripes - nparity) / ncopies;

	/*
	 * Use the number of data stripes to figure out how big this chunk
	 * is really going to be in terms of logical address space,
	 * and compare that answer with the max chunk size. If it's higher,
	 * we try to reduce stripe_size.
	 */
	if (stripe_size * data_stripes > max_chunk_size) {
		/*
		 * Reduce stripe_size, round it up to a 16MB boundary again and
		 * then use it, unless it ends up being even bigger than the
		 * previous value we had already.
		 */
		stripe_size = min(round_up(div_u64(max_chunk_size,
						   data_stripes), SZ_16M),
				  stripe_size);
	}

	/* align to BTRFS_STRIPE_LEN */
	stripe_size = round_down(stripe_size, BTRFS_STRIPE_LEN);

	map = kmalloc(map_lookup_size(num_stripes), GFP_NOFS);
	if (!map) {
		ret = -ENOMEM;
		goto error;
	}
	map->num_stripes = num_stripes;

	for (i = 0; i < ndevs; ++i) {
		for (j = 0; j < dev_stripes; ++j) {
			int s = i * dev_stripes + j;
			map->stripes[s].dev = devices_info[i].dev;
			map->stripes[s].physical = devices_info[i].dev_offset +
						   j * stripe_size;
		}
	}
	map->stripe_len = BTRFS_STRIPE_LEN;
	map->io_align = BTRFS_STRIPE_LEN;
	map->io_width = BTRFS_STRIPE_LEN;
	map->type = type;
	map->sub_stripes = sub_stripes;

	chunk_size = stripe_size * data_stripes;

	trace_btrfs_chunk_alloc(info, map, start, chunk_size);

	em = alloc_extent_map();
	if (!em) {
		kfree(map);
		ret = -ENOMEM;
		goto error;
	}
	set_bit(EXTENT_FLAG_FS_MAPPING, &em->flags);
	em->map_lookup = map;
	em->start = start;
	em->len = chunk_size;
	em->block_start = 0;
	em->block_len = em->len;
	em->orig_block_len = stripe_size;

	em_tree = &info->mapping_tree;
	write_lock(&em_tree->lock);
	ret = add_extent_mapping(em_tree, em, 0);
	if (ret) {
		write_unlock(&em_tree->lock);
		free_extent_map(em);
		goto error;
	}
	write_unlock(&em_tree->lock);

	ret = btrfs_make_block_group(trans, 0, type, start, chunk_size);
	if (ret)
		goto error_del_extent;

	for (i = 0; i < map->num_stripes; i++) {
		struct btrfs_device *dev = map->stripes[i].dev;

		btrfs_device_set_bytes_used(dev, dev->bytes_used + stripe_size);
		if (list_empty(&dev->post_commit_list))
			list_add_tail(&dev->post_commit_list,
				      &trans->transaction->dev_update_list);
	}

	atomic64_sub(stripe_size * map->num_stripes, &info->free_chunk_space);

	free_extent_map(em);
	check_raid56_incompat_flag(info, type);

	kfree(devices_info);
	return 0;

error_del_extent:
	write_lock(&em_tree->lock);
	remove_extent_mapping(em_tree, em);
	write_unlock(&em_tree->lock);

	/* One for our allocation */
	free_extent_map(em);
	/* One for the tree reference */
	free_extent_map(em);
error:
	kfree(devices_info);
	return ret;
}

int btrfs_finish_chunk_alloc(struct btrfs_trans_handle *trans,
			     u64 chunk_offset, u64 chunk_size)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *extent_root = fs_info->extent_root;
	struct btrfs_root *chunk_root = fs_info->chunk_root;
	struct btrfs_key key;
	struct btrfs_device *device;
	struct btrfs_chunk *chunk;
	struct btrfs_stripe *stripe;
	struct extent_map *em;
	struct map_lookup *map;
	size_t item_size;
	u64 dev_offset;
	u64 stripe_size;
	int i = 0;
	int ret = 0;

	em = btrfs_get_chunk_map(fs_info, chunk_offset, chunk_size);
	if (IS_ERR(em))
		return PTR_ERR(em);

	map = em->map_lookup;
	item_size = btrfs_chunk_item_size(map->num_stripes);
	stripe_size = em->orig_block_len;

	chunk = kzalloc(item_size, GFP_NOFS);
	if (!chunk) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * Take the device list mutex to prevent races with the final phase of
	 * a device replace operation that replaces the device object associated
	 * with the map's stripes, because the device object's id can change
	 * at any time during that final phase of the device replace operation
	 * (dev-replace.c:btrfs_dev_replace_finishing()).
	 */
	mutex_lock(&fs_info->fs_devices->device_list_mutex);
	for (i = 0; i < map->num_stripes; i++) {
		device = map->stripes[i].dev;
		dev_offset = map->stripes[i].physical;

		ret = btrfs_update_device(trans, device);
		if (ret)
			break;
		ret = btrfs_alloc_dev_extent(trans, device, chunk_offset,
					     dev_offset, stripe_size);
		if (ret)
			break;
	}
	if (ret) {
		mutex_unlock(&fs_info->fs_devices->device_list_mutex);
		goto out;
	}

	stripe = &chunk->stripe;
	for (i = 0; i < map->num_stripes; i++) {
		device = map->stripes[i].dev;
		dev_offset = map->stripes[i].physical;

		btrfs_set_stack_stripe_devid(stripe, device->devid);
		btrfs_set_stack_stripe_offset(stripe, dev_offset);
		memcpy(stripe->dev_uuid, device->uuid, BTRFS_UUID_SIZE);
		stripe++;
	}
	mutex_unlock(&fs_info->fs_devices->device_list_mutex);

	btrfs_set_stack_chunk_length(chunk, chunk_size);
	btrfs_set_stack_chunk_owner(chunk, extent_root->root_key.objectid);
	btrfs_set_stack_chunk_stripe_len(chunk, map->stripe_len);
	btrfs_set_stack_chunk_type(chunk, map->type);
	btrfs_set_stack_chunk_num_stripes(chunk, map->num_stripes);
	btrfs_set_stack_chunk_io_align(chunk, map->stripe_len);
	btrfs_set_stack_chunk_io_width(chunk, map->stripe_len);
	btrfs_set_stack_chunk_sector_size(chunk, fs_info->sectorsize);
	btrfs_set_stack_chunk_sub_stripes(chunk, map->sub_stripes);

	key.objectid = BTRFS_FIRST_CHUNK_TREE_OBJECTID;
	key.type = BTRFS_CHUNK_ITEM_KEY;
	key.offset = chunk_offset;

	ret = btrfs_insert_item(trans, chunk_root, &key, chunk, item_size);
	if (ret == 0 && map->type & BTRFS_BLOCK_GROUP_SYSTEM) {
		/*
		 * TODO: Cleanup of inserted chunk root in case of
		 * failure.
		 */
		ret = btrfs_add_system_chunk(fs_info, &key, chunk, item_size);
	}

out:
	kfree(chunk);
	free_extent_map(em);
	return ret;
}

/*
 * Chunk allocation falls into two parts. The first part does work
 * that makes the new allocated chunk usable, but does not do any operation
 * that modifies the chunk tree. The second part does the work that
 * requires modifying the chunk tree. This division is important for the
 * bootstrap process of adding storage to a seed btrfs.
 */
int btrfs_alloc_chunk(struct btrfs_trans_handle *trans, u64 type)
{
	u64 chunk_offset;

	lockdep_assert_held(&trans->fs_info->chunk_mutex);
	chunk_offset = find_next_chunk(trans->fs_info);
	return __btrfs_alloc_chunk(trans, chunk_offset, type);
}

static noinline int init_first_rw_device(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	u64 chunk_offset;
	u64 sys_chunk_offset;
	u64 alloc_profile;
	int ret;

	chunk_offset = find_next_chunk(fs_info);
	alloc_profile = btrfs_metadata_alloc_profile(fs_info);
	ret = __btrfs_alloc_chunk(trans, chunk_offset, alloc_profile);
	if (ret)
		return ret;

	sys_chunk_offset = find_next_chunk(fs_info);
	alloc_profile = btrfs_system_alloc_profile(fs_info);
	ret = __btrfs_alloc_chunk(trans, sys_chunk_offset, alloc_profile);
	return ret;
}

static inline int btrfs_chunk_max_errors(struct map_lookup *map)
{
	const int index = btrfs_bg_flags_to_raid_index(map->type);

	return btrfs_raid_array[index].tolerated_failures;
}

int btrfs_chunk_readonly(struct btrfs_fs_info *fs_info, u64 chunk_offset)
{
	struct extent_map *em;
	struct map_lookup *map;
	int readonly = 0;
	int miss_ndevs = 0;
	int i;

	em = btrfs_get_chunk_map(fs_info, chunk_offset, 1);
	if (IS_ERR(em))
		return 1;

	map = em->map_lookup;
	for (i = 0; i < map->num_stripes; i++) {
		if (test_bit(BTRFS_DEV_STATE_MISSING,
					&map->stripes[i].dev->dev_state)) {
			miss_ndevs++;
			continue;
		}
		if (!test_bit(BTRFS_DEV_STATE_WRITEABLE,
					&map->stripes[i].dev->dev_state)) {
			readonly = 1;
			goto end;
		}
	}

	/*
	 * If the number of missing devices is larger than max errors,
	 * we can not write the data into that chunk successfully, so
	 * set it readonly.
	 */
	if (miss_ndevs > btrfs_chunk_max_errors(map))
		readonly = 1;
end:
	free_extent_map(em);
	return readonly;
}

void btrfs_mapping_tree_free(struct extent_map_tree *tree)
{
	struct extent_map *em;

	while (1) {
		write_lock(&tree->lock);
		em = lookup_extent_mapping(tree, 0, (u64)-1);
		if (em)
			remove_extent_mapping(tree, em);
		write_unlock(&tree->lock);
		if (!em)
			break;
		/* once for us */
		free_extent_map(em);
		/* once for the tree */
		free_extent_map(em);
	}
}

int btrfs_num_copies(struct btrfs_fs_info *fs_info, u64 logical, u64 len)
{
	struct extent_map *em;
	struct map_lookup *map;
	int ret;

	em = btrfs_get_chunk_map(fs_info, logical, len);
	if (IS_ERR(em))
		/*
		 * We could return errors for these cases, but that could get
		 * ugly and we'd probably do the same thing which is just not do
		 * anything else and exit, so return 1 so the callers don't try
		 * to use other copies.
		 */
		return 1;

	map = em->map_lookup;
	if (map->type & (BTRFS_BLOCK_GROUP_DUP | BTRFS_BLOCK_GROUP_RAID1_MASK))
		ret = map->num_stripes;
	else if (map->type & BTRFS_BLOCK_GROUP_RAID10)
		ret = map->sub_stripes;
	else if (map->type & BTRFS_BLOCK_GROUP_RAID5)
		ret = 2;
	else if (map->type & BTRFS_BLOCK_GROUP_RAID6)
		/*
		 * There could be two corrupted data stripes, we need
		 * to loop retry in order to rebuild the correct data.
		 *
		 * Fail a stripe at a time on every retry except the
		 * stripe under reconstruction.
		 */
		ret = map->num_stripes;
	else
		ret = 1;
	free_extent_map(em);

	down_read(&fs_info->dev_replace.rwsem);
	if (btrfs_dev_replace_is_ongoing(&fs_info->dev_replace) &&
	    fs_info->dev_replace.tgtdev)
		ret++;
	up_read(&fs_info->dev_replace.rwsem);

	return ret;
}

unsigned long btrfs_full_stripe_len(struct btrfs_fs_info *fs_info,
				    u64 logical)
{
	struct extent_map *em;
	struct map_lookup *map;
	unsigned long len = fs_info->sectorsize;

	em = btrfs_get_chunk_map(fs_info, logical, len);

	if (!WARN_ON(IS_ERR(em))) {
		map = em->map_lookup;
		if (map->type & BTRFS_BLOCK_GROUP_RAID56_MASK)
			len = map->stripe_len * nr_data_stripes(map);
		free_extent_map(em);
	}
	return len;
}

int btrfs_is_parity_mirror(struct btrfs_fs_info *fs_info, u64 logical, u64 len)
{
	struct extent_map *em;
	struct map_lookup *map;
	int ret = 0;

	em = btrfs_get_chunk_map(fs_info, logical, len);

	if(!WARN_ON(IS_ERR(em))) {
		map = em->map_lookup;
		if (map->type & BTRFS_BLOCK_GROUP_RAID56_MASK)
			ret = 1;
		free_extent_map(em);
	}
	return ret;
}

static int find_live_mirror(struct btrfs_fs_info *fs_info,
			    struct map_lookup *map, int first,
			    int dev_replace_is_ongoing)
{
	int i;
	int num_stripes;
	int preferred_mirror;
	int tolerance;
	struct btrfs_device *srcdev;

	ASSERT((map->type &
		 (BTRFS_BLOCK_GROUP_RAID1_MASK | BTRFS_BLOCK_GROUP_RAID10)));

	if (map->type & BTRFS_BLOCK_GROUP_RAID10)
		num_stripes = map->sub_stripes;
	else
		num_stripes = map->num_stripes;

	preferred_mirror = first + current->pid % num_stripes;

	if (dev_replace_is_ongoing &&
	    fs_info->dev_replace.cont_reading_from_srcdev_mode ==
	     BTRFS_DEV_REPLACE_ITEM_CONT_READING_FROM_SRCDEV_MODE_AVOID)
		srcdev = fs_info->dev_replace.srcdev;
	else
		srcdev = NULL;

	/*
	 * try to avoid the drive that is the source drive for a
	 * dev-replace procedure, only choose it if no other non-missing
	 * mirror is available
	 */
	for (tolerance = 0; tolerance < 2; tolerance++) {
		if (map->stripes[preferred_mirror].dev->bdev &&
		    (tolerance || map->stripes[preferred_mirror].dev != srcdev))
			return preferred_mirror;
		for (i = first; i < first + num_stripes; i++) {
			if (map->stripes[i].dev->bdev &&
			    (tolerance || map->stripes[i].dev != srcdev))
				return i;
		}
	}

	/* we couldn't find one that doesn't fail.  Just return something
	 * and the io error handling code will clean up eventually
	 */
	return preferred_mirror;
}

static inline int parity_smaller(u64 a, u64 b)
{
	return a > b;
}

/* Bubble-sort the stripe set to put the parity/syndrome stripes last */
static void sort_parity_stripes(struct btrfs_bio *bbio, int num_stripes)
{
	struct btrfs_bio_stripe s;
	int i;
	u64 l;
	int again = 1;

	while (again) {
		again = 0;
		for (i = 0; i < num_stripes - 1; i++) {
			if (parity_smaller(bbio->raid_map[i],
					   bbio->raid_map[i+1])) {
				s = bbio->stripes[i];
				l = bbio->raid_map[i];
				bbio->stripes[i] = bbio->stripes[i+1];
				bbio->raid_map[i] = bbio->raid_map[i+1];
				bbio->stripes[i+1] = s;
				bbio->raid_map[i+1] = l;

				again = 1;
			}
		}
	}
}

static struct btrfs_bio *alloc_btrfs_bio(int total_stripes, int real_stripes)
{
	struct btrfs_bio *bbio = kzalloc(
		 /* the size of the btrfs_bio */
		sizeof(struct btrfs_bio) +
		/* plus the variable array for the stripes */
		sizeof(struct btrfs_bio_stripe) * (total_stripes) +
		/* plus the variable array for the tgt dev */
		sizeof(int) * (real_stripes) +
		/*
		 * plus the raid_map, which includes both the tgt dev
		 * and the stripes
		 */
		sizeof(u64) * (total_stripes),
		GFP_NOFS|__GFP_NOFAIL);

	atomic_set(&bbio->error, 0);
	refcount_set(&bbio->refs, 1);

	return bbio;
}

void btrfs_get_bbio(struct btrfs_bio *bbio)
{
	WARN_ON(!refcount_read(&bbio->refs));
	refcount_inc(&bbio->refs);
}

void btrfs_put_bbio(struct btrfs_bio *bbio)
{
	if (!bbio)
		return;
	if (refcount_dec_and_test(&bbio->refs))
		kfree(bbio);
}

/* can REQ_OP_DISCARD be sent with other REQ like REQ_OP_WRITE? */
/*
 * Please note that, discard won't be sent to target device of device
 * replace.
 */
static int __btrfs_map_block_for_discard(struct btrfs_fs_info *fs_info,
					 u64 logical, u64 length,
					 struct btrfs_bio **bbio_ret)
{
	struct extent_map *em;
	struct map_lookup *map;
	struct btrfs_bio *bbio;
	u64 offset;
	u64 stripe_nr;
	u64 stripe_nr_end;
	u64 stripe_end_offset;
	u64 stripe_cnt;
	u64 stripe_len;
	u64 stripe_offset;
	u64 num_stripes;
	u32 stripe_index;
	u32 factor = 0;
	u32 sub_stripes = 0;
	u64 stripes_per_dev = 0;
	u32 remaining_stripes = 0;
	u32 last_stripe = 0;
	int ret = 0;
	int i;

	/* discard always return a bbio */
	ASSERT(bbio_ret);

	em = btrfs_get_chunk_map(fs_info, logical, length);
	if (IS_ERR(em))
		return PTR_ERR(em);

	map = em->map_lookup;
	/* we don't discard raid56 yet */
	if (map->type & BTRFS_BLOCK_GROUP_RAID56_MASK) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	offset = logical - em->start;
	length = min_t(u64, em->len - offset, length);

	stripe_len = map->stripe_len;
	/*
	 * stripe_nr counts the total number of stripes we have to stride
	 * to get to this block
	 */
	stripe_nr = div64_u64(offset, stripe_len);

	/* stripe_offset is the offset of this block in its stripe */
	stripe_offset = offset - stripe_nr * stripe_len;

	stripe_nr_end = round_up(offset + length, map->stripe_len);
	stripe_nr_end = div64_u64(stripe_nr_end, map->stripe_len);
	stripe_cnt = stripe_nr_end - stripe_nr;
	stripe_end_offset = stripe_nr_end * map->stripe_len -
			    (offset + length);
	/*
	 * after this, stripe_nr is the number of stripes on this
	 * device we have to walk to find the data, and stripe_index is
	 * the number of our device in the stripe array
	 */
	num_stripes = 1;
	stripe_index = 0;
	if (map->type & (BTRFS_BLOCK_GROUP_RAID0 |
			 BTRFS_BLOCK_GROUP_RAID10)) {
		if (map->type & BTRFS_BLOCK_GROUP_RAID0)
			sub_stripes = 1;
		else
			sub_stripes = map->sub_stripes;

		factor = map->num_stripes / sub_stripes;
		num_stripes = min_t(u64, map->num_stripes,
				    sub_stripes * stripe_cnt);
		stripe_nr = div_u64_rem(stripe_nr, factor, &stripe_index);
		stripe_index *= sub_stripes;
		stripes_per_dev = div_u64_rem(stripe_cnt, factor,
					      &remaining_stripes);
		div_u64_rem(stripe_nr_end - 1, factor, &last_stripe);
		last_stripe *= sub_stripes;
	} else if (map->type & (BTRFS_BLOCK_GROUP_RAID1_MASK |
				BTRFS_BLOCK_GROUP_DUP)) {
		num_stripes = map->num_stripes;
	} else {
		stripe_nr = div_u64_rem(stripe_nr, map->num_stripes,
					&stripe_index);
	}

	bbio = alloc_btrfs_bio(num_stripes, 0);
	if (!bbio) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < num_stripes; i++) {
		bbio->stripes[i].physical =
			map->stripes[stripe_index].physical +
			stripe_offset + stripe_nr * map->stripe_len;
		bbio->stripes[i].dev = map->stripes[stripe_index].dev;

		if (map->type & (BTRFS_BLOCK_GROUP_RAID0 |
				 BTRFS_BLOCK_GROUP_RAID10)) {
			bbio->stripes[i].length = stripes_per_dev *
				map->stripe_len;

			if (i / sub_stripes < remaining_stripes)
				bbio->stripes[i].length +=
					map->stripe_len;

			/*
			 * Special for the first stripe and
			 * the last stripe:
			 *
			 * |-------|...|-------|
			 *     |----------|
			 *    off     end_off
			 */
			if (i < sub_stripes)
				bbio->stripes[i].length -=
					stripe_offset;

			if (stripe_index >= last_stripe &&
			    stripe_index <= (last_stripe +
					     sub_stripes - 1))
				bbio->stripes[i].length -=
					stripe_end_offset;

			if (i == sub_stripes - 1)
				stripe_offset = 0;
		} else {
			bbio->stripes[i].length = length;
		}

		stripe_index++;
		if (stripe_index == map->num_stripes) {
			stripe_index = 0;
			stripe_nr++;
		}
	}

	*bbio_ret = bbio;
	bbio->map_type = map->type;
	bbio->num_stripes = num_stripes;
out:
	free_extent_map(em);
	return ret;
}

/*
 * In dev-replace case, for repair case (that's the only case where the mirror
 * is selected explicitly when calling btrfs_map_block), blocks left of the
 * left cursor can also be read from the target drive.
 *
 * For REQ_GET_READ_MIRRORS, the target drive is added as the last one to the
 * array of stripes.
 * For READ, it also needs to be supported using the same mirror number.
 *
 * If the requested block is not left of the left cursor, EIO is returned. This
 * can happen because btrfs_num_copies() returns one more in the dev-replace
 * case.
 */
static int get_extra_mirror_from_replace(struct btrfs_fs_info *fs_info,
					 u64 logical, u64 length,
					 u64 srcdev_devid, int *mirror_num,
					 u64 *physical)
{
	struct btrfs_bio *bbio = NULL;
	int num_stripes;
	int index_srcdev = 0;
	int found = 0;
	u64 physical_of_found = 0;
	int i;
	int ret = 0;

	ret = __btrfs_map_block(fs_info, BTRFS_MAP_GET_READ_MIRRORS,
				logical, &length, &bbio, 0, 0);
	if (ret) {
		ASSERT(bbio == NULL);
		return ret;
	}

	num_stripes = bbio->num_stripes;
	if (*mirror_num > num_stripes) {
		/*
		 * BTRFS_MAP_GET_READ_MIRRORS does not contain this mirror,
		 * that means that the requested area is not left of the left
		 * cursor
		 */
		btrfs_put_bbio(bbio);
		return -EIO;
	}

	/*
	 * process the rest of the function using the mirror_num of the source
	 * drive. Therefore look it up first.  At the end, patch the device
	 * pointer to the one of the target drive.
	 */
	for (i = 0; i < num_stripes; i++) {
		if (bbio->stripes[i].dev->devid != srcdev_devid)
			continue;

		/*
		 * In case of DUP, in order to keep it simple, only add the
		 * mirror with the lowest physical address
		 */
		if (found &&
		    physical_of_found <= bbio->stripes[i].physical)
			continue;

		index_srcdev = i;
		found = 1;
		physical_of_found = bbio->stripes[i].physical;
	}

	btrfs_put_bbio(bbio);

	ASSERT(found);
	if (!found)
		return -EIO;

	*mirror_num = index_srcdev + 1;
	*physical = physical_of_found;
	return ret;
}

static void handle_ops_on_dev_replace(enum btrfs_map_op op,
				      struct btrfs_bio **bbio_ret,
				      struct btrfs_dev_replace *dev_replace,
				      int *num_stripes_ret, int *max_errors_ret)
{
	struct btrfs_bio *bbio = *bbio_ret;
	u64 srcdev_devid = dev_replace->srcdev->devid;
	int tgtdev_indexes = 0;
	int num_stripes = *num_stripes_ret;
	int max_errors = *max_errors_ret;
	int i;

	if (op == BTRFS_MAP_WRITE) {
		int index_where_to_add;

		/*
		 * duplicate the write operations while the dev replace
		 * procedure is running. Since the copying of the old disk to
		 * the new disk takes place at run time while the filesystem is
		 * mounted writable, the regular write operations to the old
		 * disk have to be duplicated to go to the new disk as well.
		 *
		 * Note that device->missing is handled by the caller, and that
		 * the write to the old disk is already set up in the stripes
		 * array.
		 */
		index_where_to_add = num_stripes;
		for (i = 0; i < num_stripes; i++) {
			if (bbio->stripes[i].dev->devid == srcdev_devid) {
				/* write to new disk, too */
				struct btrfs_bio_stripe *new =
					bbio->stripes + index_where_to_add;
				struct btrfs_bio_stripe *old =
					bbio->stripes + i;

				new->physical = old->physical;
				new->length = old->length;
				new->dev = dev_replace->tgtdev;
				bbio->tgtdev_map[i] = index_where_to_add;
				index_where_to_add++;
				max_errors++;
				tgtdev_indexes++;
			}
		}
		num_stripes = index_where_to_add;
	} else if (op == BTRFS_MAP_GET_READ_MIRRORS) {
		int index_srcdev = 0;
		int found = 0;
		u64 physical_of_found = 0;

		/*
		 * During the dev-replace procedure, the target drive can also
		 * be used to read data in case it is needed to repair a corrupt
		 * block elsewhere. This is possible if the requested area is
		 * left of the left cursor. In this area, the target drive is a
		 * full copy of the source drive.
		 */
		for (i = 0; i < num_stripes; i++) {
			if (bbio->stripes[i].dev->devid == srcdev_devid) {
				/*
				 * In case of DUP, in order to keep it simple,
				 * only add the mirror with the lowest physical
				 * address
				 */
				if (found &&
				    physical_of_found <=
				     bbio->stripes[i].physical)
					continue;
				index_srcdev = i;
				found = 1;
				physical_of_found = bbio->stripes[i].physical;
			}
		}
		if (found) {
			struct btrfs_bio_stripe *tgtdev_stripe =
				bbio->stripes + num_stripes;

			tgtdev_stripe->physical = physical_of_found;
			tgtdev_stripe->length =
				bbio->stripes[index_srcdev].length;
			tgtdev_stripe->dev = dev_replace->tgtdev;
			bbio->tgtdev_map[index_srcdev] = num_stripes;

			tgtdev_indexes++;
			num_stripes++;
		}
	}

	*num_stripes_ret = num_stripes;
	*max_errors_ret = max_errors;
	bbio->num_tgtdevs = tgtdev_indexes;
	*bbio_ret = bbio;
}

static bool need_full_stripe(enum btrfs_map_op op)
{
	return (op == BTRFS_MAP_WRITE || op == BTRFS_MAP_GET_READ_MIRRORS);
}

/*
 * btrfs_get_io_geometry - calculates the geomery of a particular (address, len)
 *		       tuple. This information is used to calculate how big a
 *		       particular bio can get before it straddles a stripe.
 *
 * @fs_info - the filesystem
 * @logical - address that we want to figure out the geometry of
 * @len	    - the length of IO we are going to perform, starting at @logical
 * @op      - type of operation - write or read
 * @io_geom - pointer used to return values
 *
 * Returns < 0 in case a chunk for the given logical address cannot be found,
 * usually shouldn't happen unless @logical is corrupted, 0 otherwise.
 */
int btrfs_get_io_geometry(struct btrfs_fs_info *fs_info, enum btrfs_map_op op,
			u64 logical, u64 len, struct btrfs_io_geometry *io_geom)
{
	struct extent_map *em;
	struct map_lookup *map;
	u64 offset;
	u64 stripe_offset;
	u64 stripe_nr;
	u64 stripe_len;
	u64 raid56_full_stripe_start = (u64)-1;
	int data_stripes;
	int ret = 0;

	ASSERT(op != BTRFS_MAP_DISCARD);

	em = btrfs_get_chunk_map(fs_info, logical, len);
	if (IS_ERR(em))
		return PTR_ERR(em);

	map = em->map_lookup;
	/* Offset of this logical address in the chunk */
	offset = logical - em->start;
	/* Len of a stripe in a chunk */
	stripe_len = map->stripe_len;
	/* Stripe wher this block falls in */
	stripe_nr = div64_u64(offset, stripe_len);
	/* Offset of stripe in the chunk */
	stripe_offset = stripe_nr * stripe_len;
	if (offset < stripe_offset) {
		btrfs_crit(fs_info,
"stripe math has gone wrong, stripe_offset=%llu offset=%llu start=%llu logical=%llu stripe_len=%llu",
			stripe_offset, offset, em->start, logical, stripe_len);
		ret = -EINVAL;
		goto out;
	}

	/* stripe_offset is the offset of this block in its stripe */
	stripe_offset = offset - stripe_offset;
	data_stripes = nr_data_stripes(map);

	if (map->type & BTRFS_BLOCK_GROUP_PROFILE_MASK) {
		u64 max_len = stripe_len - stripe_offset;

		/*
		 * In case of raid56, we need to know the stripe aligned start
		 */
		if (map->type & BTRFS_BLOCK_GROUP_RAID56_MASK) {
			unsigned long full_stripe_len = stripe_len * data_stripes;
			raid56_full_stripe_start = offset;

			/*
			 * Allow a write of a full stripe, but make sure we
			 * don't allow straddling of stripes
			 */
			raid56_full_stripe_start = div64_u64(raid56_full_stripe_start,
					full_stripe_len);
			raid56_full_stripe_start *= full_stripe_len;

			/*
			 * For writes to RAID[56], allow a full stripeset across
			 * all disks. For other RAID types and for RAID[56]
			 * reads, just allow a single stripe (on a single disk).
			 */
			if (op == BTRFS_MAP_WRITE) {
				max_len = stripe_len * data_stripes -
					  (offset - raid56_full_stripe_start);
			}
		}
		len = min_t(u64, em->len - offset, max_len);
	} else {
		len = em->len - offset;
	}

	io_geom->len = len;
	io_geom->offset = offset;
	io_geom->stripe_len = stripe_len;
	io_geom->stripe_nr = stripe_nr;
	io_geom->stripe_offset = stripe_offset;
	io_geom->raid56_stripe_offset = raid56_full_stripe_start;

out:
	/* once for us */
	free_extent_map(em);
	return ret;
}

static int __btrfs_map_block(struct btrfs_fs_info *fs_info,
			     enum btrfs_map_op op,
			     u64 logical, u64 *length,
			     struct btrfs_bio **bbio_ret,
			     int mirror_num, int need_raid_map)
{
	struct extent_map *em;
	struct map_lookup *map;
	u64 stripe_offset;
	u64 stripe_nr;
	u64 stripe_len;
	u32 stripe_index;
	int data_stripes;
	int i;
	int ret = 0;
	int num_stripes;
	int max_errors = 0;
	int tgtdev_indexes = 0;
	struct btrfs_bio *bbio = NULL;
	struct btrfs_dev_replace *dev_replace = &fs_info->dev_replace;
	int dev_replace_is_ongoing = 0;
	int num_alloc_stripes;
	int patch_the_first_stripe_for_dev_replace = 0;
	u64 physical_to_patch_in_first_stripe = 0;
	u64 raid56_full_stripe_start = (u64)-1;
	struct btrfs_io_geometry geom;

	ASSERT(bbio_ret);

	if (op == BTRFS_MAP_DISCARD)
		return __btrfs_map_block_for_discard(fs_info, logical,
						     *length, bbio_ret);

	ret = btrfs_get_io_geometry(fs_info, op, logical, *length, &geom);
	if (ret < 0)
		return ret;

	em = btrfs_get_chunk_map(fs_info, logical, *length);
	ASSERT(!IS_ERR(em));
	map = em->map_lookup;

	*length = geom.len;
	stripe_len = geom.stripe_len;
	stripe_nr = geom.stripe_nr;
	stripe_offset = geom.stripe_offset;
	raid56_full_stripe_start = geom.raid56_stripe_offset;
	data_stripes = nr_data_stripes(map);

	down_read(&dev_replace->rwsem);
	dev_replace_is_ongoing = btrfs_dev_replace_is_ongoing(dev_replace);
	/*
	 * Hold the semaphore for read during the whole operation, write is
	 * requested at commit time but must wait.
	 */
	if (!dev_replace_is_ongoing)
		up_read(&dev_replace->rwsem);

	if (dev_replace_is_ongoing && mirror_num == map->num_stripes + 1 &&
	    !need_full_stripe(op) && dev_replace->tgtdev != NULL) {
		ret = get_extra_mirror_from_replace(fs_info, logical, *length,
						    dev_replace->srcdev->devid,
						    &mirror_num,
					    &physical_to_patch_in_first_stripe);
		if (ret)
			goto out;
		else
			patch_the_first_stripe_for_dev_replace = 1;
	} else if (mirror_num > map->num_stripes) {
		mirror_num = 0;
	}

	num_stripes = 1;
	stripe_index = 0;
	if (map->type & BTRFS_BLOCK_GROUP_RAID0) {
		stripe_nr = div_u64_rem(stripe_nr, map->num_stripes,
				&stripe_index);
		if (!need_full_stripe(op))
			mirror_num = 1;
	} else if (map->type & BTRFS_BLOCK_GROUP_RAID1_MASK) {
		if (need_full_stripe(op))
			num_stripes = map->num_stripes;
		else if (mirror_num)
			stripe_index = mirror_num - 1;
		else {
			stripe_index = find_live_mirror(fs_info, map, 0,
					    dev_replace_is_ongoing);
			mirror_num = stripe_index + 1;
		}

	} else if (map->type & BTRFS_BLOCK_GROUP_DUP) {
		if (need_full_stripe(op)) {
			num_stripes = map->num_stripes;
		} else if (mirror_num) {
			stripe_index = mirror_num - 1;
		} else {
			mirror_num = 1;
		}

	} else if (map->type & BTRFS_BLOCK_GROUP_RAID10) {
		u32 factor = map->num_stripes / map->sub_stripes;

		stripe_nr = div_u64_rem(stripe_nr, factor, &stripe_index);
		stripe_index *= map->sub_stripes;

		if (need_full_stripe(op))
			num_stripes = map->sub_stripes;
		else if (mirror_num)
			stripe_index += mirror_num - 1;
		else {
			int old_stripe_index = stripe_index;
			stripe_index = find_live_mirror(fs_info, map,
					      stripe_index,
					      dev_replace_is_ongoing);
			mirror_num = stripe_index - old_stripe_index + 1;
		}

	} else if (map->type & BTRFS_BLOCK_GROUP_RAID56_MASK) {
		if (need_raid_map && (need_full_stripe(op) || mirror_num > 1)) {
			/* push stripe_nr back to the start of the full stripe */
			stripe_nr = div64_u64(raid56_full_stripe_start,
					stripe_len * data_stripes);

			/* RAID[56] write or recovery. Return all stripes */
			num_stripes = map->num_stripes;
			max_errors = nr_parity_stripes(map);

			*length = map->stripe_len;
			stripe_index = 0;
			stripe_offset = 0;
		} else {
			/*
			 * Mirror #0 or #1 means the original data block.
			 * Mirror #2 is RAID5 parity block.
			 * Mirror #3 is RAID6 Q block.
			 */
			stripe_nr = div_u64_rem(stripe_nr,
					data_stripes, &stripe_index);
			if (mirror_num > 1)
				stripe_index = data_stripes + mirror_num - 2;

			/* We distribute the parity blocks across stripes */
			div_u64_rem(stripe_nr + stripe_index, map->num_stripes,
					&stripe_index);
			if (!need_full_stripe(op) && mirror_num <= 1)
				mirror_num = 1;
		}
	} else {
		/*
		 * after this, stripe_nr is the number of stripes on this
		 * device we have to walk to find the data, and stripe_index is
		 * the number of our device in the stripe array
		 */
		stripe_nr = div_u64_rem(stripe_nr, map->num_stripes,
				&stripe_index);
		mirror_num = stripe_index + 1;
	}
	if (stripe_index >= map->num_stripes) {
		btrfs_crit(fs_info,
			   "stripe index math went horribly wrong, got stripe_index=%u, num_stripes=%u",
			   stripe_index, map->num_stripes);
		ret = -EINVAL;
		goto out;
	}

	num_alloc_stripes = num_stripes;
	if (dev_replace_is_ongoing && dev_replace->tgtdev != NULL) {
		if (op == BTRFS_MAP_WRITE)
			num_alloc_stripes <<= 1;
		if (op == BTRFS_MAP_GET_READ_MIRRORS)
			num_alloc_stripes++;
		tgtdev_indexes = num_stripes;
	}

	bbio = alloc_btrfs_bio(num_alloc_stripes, tgtdev_indexes);
	if (!bbio) {
		ret = -ENOMEM;
		goto out;
	}
	if (dev_replace_is_ongoing && dev_replace->tgtdev != NULL)
		bbio->tgtdev_map = (int *)(bbio->stripes + num_alloc_stripes);

	/* build raid_map */
	if (map->type & BTRFS_BLOCK_GROUP_RAID56_MASK && need_raid_map &&
	    (need_full_stripe(op) || mirror_num > 1)) {
		u64 tmp;
		unsigned rot;

		bbio->raid_map = (u64 *)((void *)bbio->stripes +
				 sizeof(struct btrfs_bio_stripe) *
				 num_alloc_stripes +
				 sizeof(int) * tgtdev_indexes);

		/* Work out the disk rotation on this stripe-set */
		div_u64_rem(stripe_nr, num_stripes, &rot);

		/* Fill in the logical address of each stripe */
		tmp = stripe_nr * data_stripes;
		for (i = 0; i < data_stripes; i++)
			bbio->raid_map[(i+rot) % num_stripes] =
				em->start + (tmp + i) * map->stripe_len;

		bbio->raid_map[(i+rot) % map->num_stripes] = RAID5_P_STRIPE;
		if (map->type & BTRFS_BLOCK_GROUP_RAID6)
			bbio->raid_map[(i+rot+1) % num_stripes] =
				RAID6_Q_STRIPE;
	}


	for (i = 0; i < num_stripes; i++) {
		bbio->stripes[i].physical =
			map->stripes[stripe_index].physical +
			stripe_offset +
			stripe_nr * map->stripe_len;
		bbio->stripes[i].dev =
			map->stripes[stripe_index].dev;
		stripe_index++;
	}

	if (need_full_stripe(op))
		max_errors = btrfs_chunk_max_errors(map);

	if (bbio->raid_map)
		sort_parity_stripes(bbio, num_stripes);

	if (dev_replace_is_ongoing && dev_replace->tgtdev != NULL &&
	    need_full_stripe(op)) {
		handle_ops_on_dev_replace(op, &bbio, dev_replace, &num_stripes,
					  &max_errors);
	}

	*bbio_ret = bbio;
	bbio->map_type = map->type;
	bbio->num_stripes = num_stripes;
	bbio->max_errors = max_errors;
	bbio->mirror_num = mirror_num;

	/*
	 * this is the case that REQ_READ && dev_replace_is_ongoing &&
	 * mirror_num == num_stripes + 1 && dev_replace target drive is
	 * available as a mirror
	 */
	if (patch_the_first_stripe_for_dev_replace && num_stripes > 0) {
		WARN_ON(num_stripes > 1);
		bbio->stripes[0].dev = dev_replace->tgtdev;
		bbio->stripes[0].physical = physical_to_patch_in_first_stripe;
		bbio->mirror_num = map->num_stripes + 1;
	}
out:
	if (dev_replace_is_ongoing) {
		lockdep_assert_held(&dev_replace->rwsem);
		/* Unlock and let waiting writers proceed */
		up_read(&dev_replace->rwsem);
	}
	free_extent_map(em);
	return ret;
}

int btrfs_map_block(struct btrfs_fs_info *fs_info, enum btrfs_map_op op,
		      u64 logical, u64 *length,
		      struct btrfs_bio **bbio_ret, int mirror_num)
{
	return __btrfs_map_block(fs_info, op, logical, length, bbio_ret,
				 mirror_num, 0);
}

/* For Scrub/replace */
int btrfs_map_sblock(struct btrfs_fs_info *fs_info, enum btrfs_map_op op,
		     u64 logical, u64 *length,
		     struct btrfs_bio **bbio_ret)
{
	return __btrfs_map_block(fs_info, op, logical, length, bbio_ret, 0, 1);
}

int btrfs_rmap_block(struct btrfs_fs_info *fs_info, u64 chunk_start,
		     u64 physical, u64 **logical, int *naddrs, int *stripe_len)
{
	struct extent_map *em;
	struct map_lookup *map;
	u64 *buf;
	u64 bytenr;
	u64 length;
	u64 stripe_nr;
	u64 rmap_len;
	int i, j, nr = 0;

	em = btrfs_get_chunk_map(fs_info, chunk_start, 1);
	if (IS_ERR(em))
		return -EIO;

	map = em->map_lookup;
	length = em->len;
	rmap_len = map->stripe_len;

	if (map->type & BTRFS_BLOCK_GROUP_RAID10)
		length = div_u64(length, map->num_stripes / map->sub_stripes);
	else if (map->type & BTRFS_BLOCK_GROUP_RAID0)
		length = div_u64(length, map->num_stripes);
	else if (map->type & BTRFS_BLOCK_GROUP_RAID56_MASK) {
		length = div_u64(length, nr_data_stripes(map));
		rmap_len = map->stripe_len * nr_data_stripes(map);
	}

	buf = kcalloc(map->num_stripes, sizeof(u64), GFP_NOFS);
	BUG_ON(!buf); /* -ENOMEM */

	for (i = 0; i < map->num_stripes; i++) {
		if (map->stripes[i].physical > physical ||
		    map->stripes[i].physical + length <= physical)
			continue;

		stripe_nr = physical - map->stripes[i].physical;
		stripe_nr = div64_u64(stripe_nr, map->stripe_len);

		if (map->type & BTRFS_BLOCK_GROUP_RAID10) {
			stripe_nr = stripe_nr * map->num_stripes + i;
			stripe_nr = div_u64(stripe_nr, map->sub_stripes);
		} else if (map->type & BTRFS_BLOCK_GROUP_RAID0) {
			stripe_nr = stripe_nr * map->num_stripes + i;
		} /* else if RAID[56], multiply by nr_data_stripes().
		   * Alternatively, just use rmap_len below instead of
		   * map->stripe_len */

		bytenr = chunk_start + stripe_nr * rmap_len;
		WARN_ON(nr >= map->num_stripes);
		for (j = 0; j < nr; j++) {
			if (buf[j] == bytenr)
				break;
		}
		if (j == nr) {
			WARN_ON(nr >= map->num_stripes);
			buf[nr++] = bytenr;
		}
	}

	*logical = buf;
	*naddrs = nr;
	*stripe_len = rmap_len;

	free_extent_map(em);
	return 0;
}

static inline void btrfs_end_bbio(struct btrfs_bio *bbio, struct bio *bio)
{
	bio->bi_private = bbio->private;
	bio->bi_end_io = bbio->end_io;
	bio_endio(bio);

	btrfs_put_bbio(bbio);
}

static void btrfs_end_bio(struct bio *bio)
{
	struct btrfs_bio *bbio = bio->bi_private;
	int is_orig_bio = 0;

	if (bio->bi_status) {
		atomic_inc(&bbio->error);
		if (bio->bi_status == BLK_STS_IOERR ||
		    bio->bi_status == BLK_STS_TARGET) {
			unsigned int stripe_index =
				btrfs_io_bio(bio)->stripe_index;
			struct btrfs_device *dev;

			BUG_ON(stripe_index >= bbio->num_stripes);
			dev = bbio->stripes[stripe_index].dev;
			if (dev->bdev) {
				if (bio_op(bio) == REQ_OP_WRITE)
					btrfs_dev_stat_inc_and_print(dev,
						BTRFS_DEV_STAT_WRITE_ERRS);
				else if (!(bio->bi_opf & REQ_RAHEAD))
					btrfs_dev_stat_inc_and_print(dev,
						BTRFS_DEV_STAT_READ_ERRS);
				if (bio->bi_opf & REQ_PREFLUSH)
					btrfs_dev_stat_inc_and_print(dev,
						BTRFS_DEV_STAT_FLUSH_ERRS);
			}
		}
	}

	if (bio == bbio->orig_bio)
		is_orig_bio = 1;

	btrfs_bio_counter_dec(bbio->fs_info);

	if (atomic_dec_and_test(&bbio->stripes_pending)) {
		if (!is_orig_bio) {
			bio_put(bio);
			bio = bbio->orig_bio;
		}

		btrfs_io_bio(bio)->mirror_num = bbio->mirror_num;
		/* only send an error to the higher layers if it is
		 * beyond the tolerance of the btrfs bio
		 */
		if (atomic_read(&bbio->error) > bbio->max_errors) {
			bio->bi_status = BLK_STS_IOERR;
		} else {
			/*
			 * this bio is actually up to date, we didn't
			 * go over the max number of errors
			 */
			bio->bi_status = BLK_STS_OK;
		}

		btrfs_end_bbio(bbio, bio);
	} else if (!is_orig_bio) {
		bio_put(bio);
	}
}

/*
 * see run_scheduled_bios for a description of why bios are collected for
 * async submit.
 *
 * This will add one bio to the pending list for a device and make sure
 * the work struct is scheduled.
 */
static noinline void btrfs_schedule_bio(struct btrfs_device *device,
					struct bio *bio)
{
	struct btrfs_fs_info *fs_info = device->fs_info;
	int should_queue = 1;
	struct btrfs_pending_bios *pending_bios;

	/* don't bother with additional async steps for reads, right now */
	if (bio_op(bio) == REQ_OP_READ) {
		btrfsic_submit_bio(bio);
		return;
	}

	WARN_ON(bio->bi_next);
	bio->bi_next = NULL;

	spin_lock(&device->io_lock);
	if (op_is_sync(bio->bi_opf))
		pending_bios = &device->pending_sync_bios;
	else
		pending_bios = &device->pending_bios;

	if (pending_bios->tail)
		pending_bios->tail->bi_next = bio;

	pending_bios->tail = bio;
	if (!pending_bios->head)
		pending_bios->head = bio;
	if (device->running_pending)
		should_queue = 0;

	spin_unlock(&device->io_lock);

	if (should_queue)
		btrfs_queue_work(fs_info->submit_workers, &device->work);
}

static void submit_stripe_bio(struct btrfs_bio *bbio, struct bio *bio,
			      u64 physical, int dev_nr, int async)
{
	struct btrfs_device *dev = bbio->stripes[dev_nr].dev;
	struct btrfs_fs_info *fs_info = bbio->fs_info;

	bio->bi_private = bbio;
	btrfs_io_bio(bio)->stripe_index = dev_nr;
	bio->bi_end_io = btrfs_end_bio;
	bio->bi_iter.bi_sector = physical >> 9;
	btrfs_debug_in_rcu(fs_info,
	"btrfs_map_bio: rw %d 0x%x, sector=%llu, dev=%lu (%s id %llu), size=%u",
		bio_op(bio), bio->bi_opf, (u64)bio->bi_iter.bi_sector,
		(u_long)dev->bdev->bd_dev, rcu_str_deref(dev->name), dev->devid,
		bio->bi_iter.bi_size);
	bio_set_dev(bio, dev->bdev);

	btrfs_bio_counter_inc_noblocked(fs_info);

	if (async)
		btrfs_schedule_bio(dev, bio);
	else
		btrfsic_submit_bio(bio);
}

static void bbio_error(struct btrfs_bio *bbio, struct bio *bio, u64 logical)
{
	atomic_inc(&bbio->error);
	if (atomic_dec_and_test(&bbio->stripes_pending)) {
		/* Should be the original bio. */
		WARN_ON(bio != bbio->orig_bio);

		btrfs_io_bio(bio)->mirror_num = bbio->mirror_num;
		bio->bi_iter.bi_sector = logical >> 9;
		if (atomic_read(&bbio->error) > bbio->max_errors)
			bio->bi_status = BLK_STS_IOERR;
		else
			bio->bi_status = BLK_STS_OK;
		btrfs_end_bbio(bbio, bio);
	}
}

blk_status_t btrfs_map_bio(struct btrfs_fs_info *fs_info, struct bio *bio,
			   int mirror_num, int async_submit)
{
	struct btrfs_device *dev;
	struct bio *first_bio = bio;
	u64 logical = (u64)bio->bi_iter.bi_sector << 9;
	u64 length = 0;
	u64 map_length;
	int ret;
	int dev_nr;
	int total_devs;
	struct btrfs_bio *bbio = NULL;

	length = bio->bi_iter.bi_size;
	map_length = length;

	btrfs_bio_counter_inc_blocked(fs_info);
	ret = __btrfs_map_block(fs_info, btrfs_op(bio), logical,
				&map_length, &bbio, mirror_num, 1);
	if (ret) {
		btrfs_bio_counter_dec(fs_info);
		return errno_to_blk_status(ret);
	}

	total_devs = bbio->num_stripes;
	bbio->orig_bio = first_bio;
	bbio->private = first_bio->bi_private;
	bbio->end_io = first_bio->bi_end_io;
	bbio->fs_info = fs_info;
	atomic_set(&bbio->stripes_pending, bbio->num_stripes);

	if ((bbio->map_type & BTRFS_BLOCK_GROUP_RAID56_MASK) &&
	    ((bio_op(bio) == REQ_OP_WRITE) || (mirror_num > 1))) {
		/* In this case, map_length has been set to the length of
		   a single stripe; not the whole write */
		if (bio_op(bio) == REQ_OP_WRITE) {
			ret = raid56_parity_write(fs_info, bio, bbio,
						  map_length);
		} else {
			ret = raid56_parity_recover(fs_info, bio, bbio,
						    map_length, mirror_num, 1);
		}

		btrfs_bio_counter_dec(fs_info);
		return errno_to_blk_status(ret);
	}

	if (map_length < length) {
		btrfs_crit(fs_info,
			   "mapping failed logical %llu bio len %llu len %llu",
			   logical, length, map_length);
		BUG();
	}

	for (dev_nr = 0; dev_nr < total_devs; dev_nr++) {
		dev = bbio->stripes[dev_nr].dev;
		if (!dev || !dev->bdev || test_bit(BTRFS_DEV_STATE_MISSING,
						   &dev->dev_state) ||
		    (bio_op(first_bio) == REQ_OP_WRITE &&
		    !test_bit(BTRFS_DEV_STATE_WRITEABLE, &dev->dev_state))) {
			bbio_error(bbio, first_bio, logical);
			continue;
		}

		if (dev_nr < total_devs - 1)
			bio = btrfs_bio_clone(first_bio);
		else
			bio = first_bio;

		submit_stripe_bio(bbio, bio, bbio->stripes[dev_nr].physical,
				  dev_nr, async_submit);
	}
	btrfs_bio_counter_dec(fs_info);
	return BLK_STS_OK;
}

/*
 * Find a device specified by @devid or @uuid in the list of @fs_devices, or
 * return NULL.
 *
 * If devid and uuid are both specified, the match must be exact, otherwise
 * only devid is used.
 *
 * If @seed is true, traverse through the seed devices.
 */
struct btrfs_device *btrfs_find_device(struct btrfs_fs_devices *fs_devices,
				       u64 devid, u8 *uuid, u8 *fsid,
				       bool seed)
{
	struct btrfs_device *device;

	while (fs_devices) {
		if (!fsid ||
		    !memcmp(fs_devices->metadata_uuid, fsid, BTRFS_FSID_SIZE)) {
			list_for_each_entry(device, &fs_devices->devices,
					    dev_list) {
				if (device->devid == devid &&
				    (!uuid || memcmp(device->uuid, uuid,
						     BTRFS_UUID_SIZE) == 0))
					return device;
			}
		}
		if (seed)
			fs_devices = fs_devices->seed;
		else
			return NULL;
	}
	return NULL;
}

static struct btrfs_device *add_missing_dev(struct btrfs_fs_devices *fs_devices,
					    u64 devid, u8 *dev_uuid)
{
	struct btrfs_device *device;

	device = btrfs_alloc_device(NULL, &devid, dev_uuid);
	if (IS_ERR(device))
		return device;

	list_add(&device->dev_list, &fs_devices->devices);
	device->fs_devices = fs_devices;
	fs_devices->num_devices++;

	set_bit(BTRFS_DEV_STATE_MISSING, &device->dev_state);
	fs_devices->missing_devices++;

	return device;
}

/**
 * btrfs_alloc_device - allocate struct btrfs_device
 * @fs_info:	used only for generating a new devid, can be NULL if
 *		devid is provided (i.e. @devid != NULL).
 * @devid:	a pointer to devid for this device.  If NULL a new devid
 *		is generated.
 * @uuid:	a pointer to UUID for this device.  If NULL a new UUID
 *		is generated.
 *
 * Return: a pointer to a new &struct btrfs_device on success; ERR_PTR()
 * on error.  Returned struct is not linked onto any lists and must be
 * destroyed with btrfs_free_device.
 */
struct btrfs_device *btrfs_alloc_device(struct btrfs_fs_info *fs_info,
					const u64 *devid,
					const u8 *uuid)
{
	struct btrfs_device *dev;
	u64 tmp;

	if (WARN_ON(!devid && !fs_info))
		return ERR_PTR(-EINVAL);

	dev = __alloc_device();
	if (IS_ERR(dev))
		return dev;

	if (devid)
		tmp = *devid;
	else {
		int ret;

		ret = find_next_devid(fs_info, &tmp);
		if (ret) {
			btrfs_free_device(dev);
			return ERR_PTR(ret);
		}
	}
	dev->devid = tmp;

	if (uuid)
		memcpy(dev->uuid, uuid, BTRFS_UUID_SIZE);
	else
		generate_random_uuid(dev->uuid);

	btrfs_init_work(&dev->work, btrfs_submit_helper,
			pending_bios_fn, NULL, NULL);

	return dev;
}

static void btrfs_report_missing_device(struct btrfs_fs_info *fs_info,
					u64 devid, u8 *uuid, bool error)
{
	if (error)
		btrfs_err_rl(fs_info, "devid %llu uuid %pU is missing",
			      devid, uuid);
	else
		btrfs_warn_rl(fs_info, "devid %llu uuid %pU is missing",
			      devid, uuid);
}

static u64 calc_stripe_length(u64 type, u64 chunk_len, int num_stripes)
{
	int index = btrfs_bg_flags_to_raid_index(type);
	int ncopies = btrfs_raid_array[index].ncopies;
	int data_stripes;

	switch (type & BTRFS_BLOCK_GROUP_PROFILE_MASK) {
	case BTRFS_BLOCK_GROUP_RAID5:
		data_stripes = num_stripes - 1;
		break;
	case BTRFS_BLOCK_GROUP_RAID6:
		data_stripes = num_stripes - 2;
		break;
	default:
		data_stripes = num_stripes / ncopies;
		break;
	}
	return div_u64(chunk_len, data_stripes);
}

static int read_one_chunk(struct btrfs_key *key, struct extent_buffer *leaf,
			  struct btrfs_chunk *chunk)
{
	struct btrfs_fs_info *fs_info = leaf->fs_info;
	struct extent_map_tree *map_tree = &fs_info->mapping_tree;
	struct map_lookup *map;
	struct extent_map *em;
	u64 logical;
	u64 length;
	u64 devid;
	u8 uuid[BTRFS_UUID_SIZE];
	int num_stripes;
	int ret;
	int i;

	logical = key->offset;
	length = btrfs_chunk_length(leaf, chunk);
	num_stripes = btrfs_chunk_num_stripes(leaf, chunk);

	/*
	 * Only need to verify chunk item if we're reading from sys chunk array,
	 * as chunk item in tree block is already verified by tree-checker.
	 */
	if (leaf->start == BTRFS_SUPER_INFO_OFFSET) {
		ret = btrfs_check_chunk_valid(leaf, chunk, logical);
		if (ret)
			return ret;
	}

	read_lock(&map_tree->lock);
	em = lookup_extent_mapping(map_tree, logical, 1);
	read_unlock(&map_tree->lock);

	/* already mapped? */
	if (em && em->start <= logical && em->start + em->len > logical) {
		free_extent_map(em);
		return 0;
	} else if (em) {
		free_extent_map(em);
	}

	em = alloc_extent_map();
	if (!em)
		return -ENOMEM;
	map = kmalloc(map_lookup_size(num_stripes), GFP_NOFS);
	if (!map) {
		free_extent_map(em);
		return -ENOMEM;
	}

	set_bit(EXTENT_FLAG_FS_MAPPING, &em->flags);
	em->map_lookup = map;
	em->start = logical;
	em->len = length;
	em->orig_start = 0;
	em->block_start = 0;
	em->block_len = em->len;

	map->num_stripes = num_stripes;
	map->io_width = btrfs_chunk_io_width(leaf, chunk);
	map->io_align = btrfs_chunk_io_align(leaf, chunk);
	map->stripe_len = btrfs_chunk_stripe_len(leaf, chunk);
	map->type = btrfs_chunk_type(leaf, chunk);
	map->sub_stripes = btrfs_chunk_sub_stripes(leaf, chunk);
	map->verified_stripes = 0;
	em->orig_block_len = calc_stripe_length(map->type, em->len,
						map->num_stripes);
	for (i = 0; i < num_stripes; i++) {
		map->stripes[i].physical =
			btrfs_stripe_offset_nr(leaf, chunk, i);
		devid = btrfs_stripe_devid_nr(leaf, chunk, i);
		read_extent_buffer(leaf, uuid, (unsigned long)
				   btrfs_stripe_dev_uuid_nr(chunk, i),
				   BTRFS_UUID_SIZE);
		map->stripes[i].dev = btrfs_find_device(fs_info->fs_devices,
							devid, uuid, NULL, true);
		if (!map->stripes[i].dev &&
		    !btrfs_test_opt(fs_info, DEGRADED)) {
			free_extent_map(em);
			btrfs_report_missing_device(fs_info, devid, uuid, true);
			return -ENOENT;
		}
		if (!map->stripes[i].dev) {
			map->stripes[i].dev =
				add_missing_dev(fs_info->fs_devices, devid,
						uuid);
			if (IS_ERR(map->stripes[i].dev)) {
				free_extent_map(em);
				btrfs_err(fs_info,
					"failed to init missing dev %llu: %ld",
					devid, PTR_ERR(map->stripes[i].dev));
				return PTR_ERR(map->stripes[i].dev);
			}
			btrfs_report_missing_device(fs_info, devid, uuid, false);
		}
		set_bit(BTRFS_DEV_STATE_IN_FS_METADATA,
				&(map->stripes[i].dev->dev_state));

	}

	write_lock(&map_tree->lock);
	ret = add_extent_mapping(map_tree, em, 0);
	write_unlock(&map_tree->lock);
	if (ret < 0) {
		btrfs_err(fs_info,
			  "failed to add chunk map, start=%llu len=%llu: %d",
			  em->start, em->len, ret);
	}
	free_extent_map(em);

	return ret;
}

static void fill_device_from_item(struct extent_buffer *leaf,
				 struct btrfs_dev_item *dev_item,
				 struct btrfs_device *device)
{
	unsigned long ptr;

	device->devid = btrfs_device_id(leaf, dev_item);
	device->disk_total_bytes = btrfs_device_total_bytes(leaf, dev_item);
	device->total_bytes = device->disk_total_bytes;
	device->commit_total_bytes = device->disk_total_bytes;
	device->bytes_used = btrfs_device_bytes_used(leaf, dev_item);
	device->commit_bytes_used = device->bytes_used;
	device->type = btrfs_device_type(leaf, dev_item);
	device->io_align = btrfs_device_io_align(leaf, dev_item);
	device->io_width = btrfs_device_io_width(leaf, dev_item);
	device->sector_size = btrfs_device_sector_size(leaf, dev_item);
	WARN_ON(device->devid == BTRFS_DEV_REPLACE_DEVID);
	clear_bit(BTRFS_DEV_STATE_REPLACE_TGT, &device->dev_state);

	ptr = btrfs_device_uuid(dev_item);
	read_extent_buffer(leaf, device->uuid, ptr, BTRFS_UUID_SIZE);
}

static struct btrfs_fs_devices *open_seed_devices(struct btrfs_fs_info *fs_info,
						  u8 *fsid)
{
	struct btrfs_fs_devices *fs_devices;
	int ret;

	lockdep_assert_held(&uuid_mutex);
	ASSERT(fsid);

	fs_devices = fs_info->fs_devices->seed;
	while (fs_devices) {
		if (!memcmp(fs_devices->fsid, fsid, BTRFS_FSID_SIZE))
			return fs_devices;

		fs_devices = fs_devices->seed;
	}

	fs_devices = find_fsid(fsid, NULL);
	if (!fs_devices) {
		if (!btrfs_test_opt(fs_info, DEGRADED))
			return ERR_PTR(-ENOENT);

		fs_devices = alloc_fs_devices(fsid, NULL);
		if (IS_ERR(fs_devices))
			return fs_devices;

		fs_devices->seeding = 1;
		fs_devices->opened = 1;
		return fs_devices;
	}

	fs_devices = clone_fs_devices(fs_devices);
	if (IS_ERR(fs_devices))
		return fs_devices;

	ret = open_fs_devices(fs_devices, FMODE_READ, fs_info->bdev_holder);
	if (ret) {
		free_fs_devices(fs_devices);
		fs_devices = ERR_PTR(ret);
		goto out;
	}

	if (!fs_devices->seeding) {
		close_fs_devices(fs_devices);
		free_fs_devices(fs_devices);
		fs_devices = ERR_PTR(-EINVAL);
		goto out;
	}

	fs_devices->seed = fs_info->fs_devices->seed;
	fs_info->fs_devices->seed = fs_devices;
out:
	return fs_devices;
}

static int read_one_dev(struct extent_buffer *leaf,
			struct btrfs_dev_item *dev_item)
{
	struct btrfs_fs_info *fs_info = leaf->fs_info;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	struct btrfs_device *device;
	u64 devid;
	int ret;
	u8 fs_uuid[BTRFS_FSID_SIZE];
	u8 dev_uuid[BTRFS_UUID_SIZE];

	devid = btrfs_device_id(leaf, dev_item);
	read_extent_buffer(leaf, dev_uuid, btrfs_device_uuid(dev_item),
			   BTRFS_UUID_SIZE);
	read_extent_buffer(leaf, fs_uuid, btrfs_device_fsid(dev_item),
			   BTRFS_FSID_SIZE);

	if (memcmp(fs_uuid, fs_devices->metadata_uuid, BTRFS_FSID_SIZE)) {
		fs_devices = open_seed_devices(fs_info, fs_uuid);
		if (IS_ERR(fs_devices))
			return PTR_ERR(fs_devices);
	}

	device = btrfs_find_device(fs_info->fs_devices, devid, dev_uuid,
				   fs_uuid, true);
	if (!device) {
		if (!btrfs_test_opt(fs_info, DEGRADED)) {
			btrfs_report_missing_device(fs_info, devid,
							dev_uuid, true);
			return -ENOENT;
		}

		device = add_missing_dev(fs_devices, devid, dev_uuid);
		if (IS_ERR(device)) {
			btrfs_err(fs_info,
				"failed to add missing dev %llu: %ld",
				devid, PTR_ERR(device));
			return PTR_ERR(device);
		}
		btrfs_report_missing_device(fs_info, devid, dev_uuid, false);
	} else {
		if (!device->bdev) {
			if (!btrfs_test_opt(fs_info, DEGRADED)) {
				btrfs_report_missing_device(fs_info,
						devid, dev_uuid, true);
				return -ENOENT;
			}
			btrfs_report_missing_device(fs_info, devid,
							dev_uuid, false);
		}

		if (!device->bdev &&
		    !test_bit(BTRFS_DEV_STATE_MISSING, &device->dev_state)) {
			/*
			 * this happens when a device that was properly setup
			 * in the device info lists suddenly goes bad.
			 * device->bdev is NULL, and so we have to set
			 * device->missing to one here
			 */
			device->fs_devices->missing_devices++;
			set_bit(BTRFS_DEV_STATE_MISSING, &device->dev_state);
		}

		/* Move the device to its own fs_devices */
		if (device->fs_devices != fs_devices) {
			ASSERT(test_bit(BTRFS_DEV_STATE_MISSING,
							&device->dev_state));

			list_move(&device->dev_list, &fs_devices->devices);
			device->fs_devices->num_devices--;
			fs_devices->num_devices++;

			device->fs_devices->missing_devices--;
			fs_devices->missing_devices++;

			device->fs_devices = fs_devices;
		}
	}

	if (device->fs_devices != fs_info->fs_devices) {
		BUG_ON(test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state));
		if (device->generation !=
		    btrfs_device_generation(leaf, dev_item))
			return -EINVAL;
	}

	fill_device_from_item(leaf, dev_item, device);
	set_bit(BTRFS_DEV_STATE_IN_FS_METADATA, &device->dev_state);
	if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state) &&
	   !test_bit(BTRFS_DEV_STATE_REPLACE_TGT, &device->dev_state)) {
		device->fs_devices->total_rw_bytes += device->total_bytes;
		atomic64_add(device->total_bytes - device->bytes_used,
				&fs_info->free_chunk_space);
	}
	ret = 0;
	return ret;
}

int btrfs_read_sys_array(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *root = fs_info->tree_root;
	struct btrfs_super_block *super_copy = fs_info->super_copy;
	struct extent_buffer *sb;
	struct btrfs_disk_key *disk_key;
	struct btrfs_chunk *chunk;
	u8 *array_ptr;
	unsigned long sb_array_offset;
	int ret = 0;
	u32 num_stripes;
	u32 array_size;
	u32 len = 0;
	u32 cur_offset;
	u64 type;
	struct btrfs_key key;

	ASSERT(BTRFS_SUPER_INFO_SIZE <= fs_info->nodesize);
	/*
	 * This will create extent buffer of nodesize, superblock size is
	 * fixed to BTRFS_SUPER_INFO_SIZE. If nodesize > sb size, this will
	 * overallocate but we can keep it as-is, only the first page is used.
	 */
	sb = btrfs_find_create_tree_block(fs_info, BTRFS_SUPER_INFO_OFFSET);
	if (IS_ERR(sb))
		return PTR_ERR(sb);
	set_extent_buffer_uptodate(sb);
	btrfs_set_buffer_lockdep_class(root->root_key.objectid, sb, 0);
	/*
	 * The sb extent buffer is artificial and just used to read the system array.
	 * set_extent_buffer_uptodate() call does not properly mark all it's
	 * pages up-to-date when the page is larger: extent does not cover the
	 * whole page and consequently check_page_uptodate does not find all
	 * the page's extents up-to-date (the hole beyond sb),
	 * write_extent_buffer then triggers a WARN_ON.
	 *
	 * Regular short extents go through mark_extent_buffer_dirty/writeback cycle,
	 * but sb spans only this function. Add an explicit SetPageUptodate call
	 * to silence the warning eg. on PowerPC 64.
	 */
	if (PAGE_SIZE > BTRFS_SUPER_INFO_SIZE)
		SetPageUptodate(sb->pages[0]);

	write_extent_buffer(sb, super_copy, 0, BTRFS_SUPER_INFO_SIZE);
	array_size = btrfs_super_sys_array_size(super_copy);

	array_ptr = super_copy->sys_chunk_array;
	sb_array_offset = offsetof(struct btrfs_super_block, sys_chunk_array);
	cur_offset = 0;

	while (cur_offset < array_size) {
		disk_key = (struct btrfs_disk_key *)array_ptr;
		len = sizeof(*disk_key);
		if (cur_offset + len > array_size)
			goto out_short_read;

		btrfs_disk_key_to_cpu(&key, disk_key);

		array_ptr += len;
		sb_array_offset += len;
		cur_offset += len;

		if (key.type == BTRFS_CHUNK_ITEM_KEY) {
			chunk = (struct btrfs_chunk *)sb_array_offset;
			/*
			 * At least one btrfs_chunk with one stripe must be
			 * present, exact stripe count check comes afterwards
			 */
			len = btrfs_chunk_item_size(1);
			if (cur_offset + len > array_size)
				goto out_short_read;

			num_stripes = btrfs_chunk_num_stripes(sb, chunk);
			if (!num_stripes) {
				btrfs_err(fs_info,
					"invalid number of stripes %u in sys_array at offset %u",
					num_stripes, cur_offset);
				ret = -EIO;
				break;
			}

			type = btrfs_chunk_type(sb, chunk);
			if ((type & BTRFS_BLOCK_GROUP_SYSTEM) == 0) {
				btrfs_err(fs_info,
			    "invalid chunk type %llu in sys_array at offset %u",
					type, cur_offset);
				ret = -EIO;
				break;
			}

			len = btrfs_chunk_item_size(num_stripes);
			if (cur_offset + len > array_size)
				goto out_short_read;

			ret = read_one_chunk(&key, sb, chunk);
			if (ret)
				break;
		} else {
			btrfs_err(fs_info,
			    "unexpected item type %u in sys_array at offset %u",
				  (u32)key.type, cur_offset);
			ret = -EIO;
			break;
		}
		array_ptr += len;
		sb_array_offset += len;
		cur_offset += len;
	}
	clear_extent_buffer_uptodate(sb);
	free_extent_buffer_stale(sb);
	return ret;

out_short_read:
	btrfs_err(fs_info, "sys_array too short to read %u bytes at offset %u",
			len, cur_offset);
	clear_extent_buffer_uptodate(sb);
	free_extent_buffer_stale(sb);
	return -EIO;
}

/*
 * Check if all chunks in the fs are OK for read-write degraded mount
 *
 * If the @failing_dev is specified, it's accounted as missing.
 *
 * Return true if all chunks meet the minimal RW mount requirements.
 * Return false if any chunk doesn't meet the minimal RW mount requirements.
 */
bool btrfs_check_rw_degradable(struct btrfs_fs_info *fs_info,
					struct btrfs_device *failing_dev)
{
	struct extent_map_tree *map_tree = &fs_info->mapping_tree;
	struct extent_map *em;
	u64 next_start = 0;
	bool ret = true;

	read_lock(&map_tree->lock);
	em = lookup_extent_mapping(map_tree, 0, (u64)-1);
	read_unlock(&map_tree->lock);
	/* No chunk at all? Return false anyway */
	if (!em) {
		ret = false;
		goto out;
	}
	while (em) {
		struct map_lookup *map;
		int missing = 0;
		int max_tolerated;
		int i;

		map = em->map_lookup;
		max_tolerated =
			btrfs_get_num_tolerated_disk_barrier_failures(
					map->type);
		for (i = 0; i < map->num_stripes; i++) {
			struct btrfs_device *dev = map->stripes[i].dev;

			if (!dev || !dev->bdev ||
			    test_bit(BTRFS_DEV_STATE_MISSING, &dev->dev_state) ||
			    dev->last_flush_error)
				missing++;
			else if (failing_dev && failing_dev == dev)
				missing++;
		}
		if (missing > max_tolerated) {
			if (!failing_dev)
				btrfs_warn(fs_info,
	"chunk %llu missing %d devices, max tolerance is %d for writable mount",
				   em->start, missing, max_tolerated);
			free_extent_map(em);
			ret = false;
			goto out;
		}
		next_start = extent_map_end(em);
		free_extent_map(em);

		read_lock(&map_tree->lock);
		em = lookup_extent_mapping(map_tree, next_start,
					   (u64)(-1) - next_start);
		read_unlock(&map_tree->lock);
	}
out:
	return ret;
}

int btrfs_read_chunk_tree(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *root = fs_info->chunk_root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	struct btrfs_key found_key;
	int ret;
	int slot;
	u64 total_dev = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	/*
	 * uuid_mutex is needed only if we are mounting a sprout FS
	 * otherwise we don't need it.
	 */
	mutex_lock(&uuid_mutex);
	mutex_lock(&fs_info->chunk_mutex);

	/*
	 * Read all device items, and then all the chunk items. All
	 * device items are found before any chunk item (their object id
	 * is smaller than the lowest possible object id for a chunk
	 * item - BTRFS_FIRST_CHUNK_TREE_OBJECTID).
	 */
	key.objectid = BTRFS_DEV_ITEMS_OBJECTID;
	key.offset = 0;
	key.type = 0;
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto error;
	while (1) {
		leaf = path->nodes[0];
		slot = path->slots[0];
		if (slot >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret == 0)
				continue;
			if (ret < 0)
				goto error;
			break;
		}
		btrfs_item_key_to_cpu(leaf, &found_key, slot);
		if (found_key.type == BTRFS_DEV_ITEM_KEY) {
			struct btrfs_dev_item *dev_item;
			dev_item = btrfs_item_ptr(leaf, slot,
						  struct btrfs_dev_item);
			ret = read_one_dev(leaf, dev_item);
			if (ret)
				goto error;
			total_dev++;
		} else if (found_key.type == BTRFS_CHUNK_ITEM_KEY) {
			struct btrfs_chunk *chunk;
			chunk = btrfs_item_ptr(leaf, slot, struct btrfs_chunk);
			ret = read_one_chunk(&found_key, leaf, chunk);
			if (ret)
				goto error;
		}
		path->slots[0]++;
	}

	/*
	 * After loading chunk tree, we've got all device information,
	 * do another round of validation checks.
	 */
	if (total_dev != fs_info->fs_devices->total_devices) {
		btrfs_err(fs_info,
	   "super_num_devices %llu mismatch with num_devices %llu found here",
			  btrfs_super_num_devices(fs_info->super_copy),
			  total_dev);
		ret = -EINVAL;
		goto error;
	}
	if (btrfs_super_total_bytes(fs_info->super_copy) <
	    fs_info->fs_devices->total_rw_bytes) {
		btrfs_err(fs_info,
	"super_total_bytes %llu mismatch with fs_devices total_rw_bytes %llu",
			  btrfs_super_total_bytes(fs_info->super_copy),
			  fs_info->fs_devices->total_rw_bytes);
		ret = -EINVAL;
		goto error;
	}
	ret = 0;
error:
	mutex_unlock(&fs_info->chunk_mutex);
	mutex_unlock(&uuid_mutex);

	btrfs_free_path(path);
	return ret;
}

void btrfs_init_devices_late(struct btrfs_fs_info *fs_info)
{
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	struct btrfs_device *device;

	while (fs_devices) {
		mutex_lock(&fs_devices->device_list_mutex);
		list_for_each_entry(device, &fs_devices->devices, dev_list)
			device->fs_info = fs_info;
		mutex_unlock(&fs_devices->device_list_mutex);

		fs_devices = fs_devices->seed;
	}
}

static u64 btrfs_dev_stats_value(const struct extent_buffer *eb,
				 const struct btrfs_dev_stats_item *ptr,
				 int index)
{
	u64 val;

	read_extent_buffer(eb, &val,
			   offsetof(struct btrfs_dev_stats_item, values) +
			    ((unsigned long)ptr) + (index * sizeof(u64)),
			   sizeof(val));
	return val;
}

static void btrfs_set_dev_stats_value(struct extent_buffer *eb,
				      struct btrfs_dev_stats_item *ptr,
				      int index, u64 val)
{
	write_extent_buffer(eb, &val,
			    offsetof(struct btrfs_dev_stats_item, values) +
			     ((unsigned long)ptr) + (index * sizeof(u64)),
			    sizeof(val));
}

int btrfs_init_dev_stats(struct btrfs_fs_info *fs_info)
{
	struct btrfs_key key;
	struct btrfs_root *dev_root = fs_info->dev_root;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	struct extent_buffer *eb;
	int slot;
	int ret = 0;
	struct btrfs_device *device;
	struct btrfs_path *path = NULL;
	int i;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	mutex_lock(&fs_devices->device_list_mutex);
	list_for_each_entry(device, &fs_devices->devices, dev_list) {
		int item_size;
		struct btrfs_dev_stats_item *ptr;

		key.objectid = BTRFS_DEV_STATS_OBJECTID;
		key.type = BTRFS_PERSISTENT_ITEM_KEY;
		key.offset = device->devid;
		ret = btrfs_search_slot(NULL, dev_root, &key, path, 0, 0);
		if (ret) {
			for (i = 0; i < BTRFS_DEV_STAT_VALUES_MAX; i++)
				btrfs_dev_stat_set(device, i, 0);
			device->dev_stats_valid = 1;
			btrfs_release_path(path);
			continue;
		}
		slot = path->slots[0];
		eb = path->nodes[0];
		item_size = btrfs_item_size_nr(eb, slot);

		ptr = btrfs_item_ptr(eb, slot,
				     struct btrfs_dev_stats_item);

		for (i = 0; i < BTRFS_DEV_STAT_VALUES_MAX; i++) {
			if (item_size >= (1 + i) * sizeof(__le64))
				btrfs_dev_stat_set(device, i,
					btrfs_dev_stats_value(eb, ptr, i));
			else
				btrfs_dev_stat_set(device, i, 0);
		}

		device->dev_stats_valid = 1;
		btrfs_dev_stat_print_on_load(device);
		btrfs_release_path(path);
	}
	mutex_unlock(&fs_devices->device_list_mutex);

	btrfs_free_path(path);
	return ret < 0 ? ret : 0;
}

static int update_dev_stat_item(struct btrfs_trans_handle *trans,
				struct btrfs_device *device)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *dev_root = fs_info->dev_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *eb;
	struct btrfs_dev_stats_item *ptr;
	int ret;
	int i;

	key.objectid = BTRFS_DEV_STATS_OBJECTID;
	key.type = BTRFS_PERSISTENT_ITEM_KEY;
	key.offset = device->devid;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	ret = btrfs_search_slot(trans, dev_root, &key, path, -1, 1);
	if (ret < 0) {
		btrfs_warn_in_rcu(fs_info,
			"error %d while searching for dev_stats item for device %s",
			      ret, rcu_str_deref(device->name));
		goto out;
	}

	if (ret == 0 &&
	    btrfs_item_size_nr(path->nodes[0], path->slots[0]) < sizeof(*ptr)) {
		/* need to delete old one and insert a new one */
		ret = btrfs_del_item(trans, dev_root, path);
		if (ret != 0) {
			btrfs_warn_in_rcu(fs_info,
				"delete too small dev_stats item for device %s failed %d",
				      rcu_str_deref(device->name), ret);
			goto out;
		}
		ret = 1;
	}

	if (ret == 1) {
		/* need to insert a new item */
		btrfs_release_path(path);
		ret = btrfs_insert_empty_item(trans, dev_root, path,
					      &key, sizeof(*ptr));
		if (ret < 0) {
			btrfs_warn_in_rcu(fs_info,
				"insert dev_stats item for device %s failed %d",
				rcu_str_deref(device->name), ret);
			goto out;
		}
	}

	eb = path->nodes[0];
	ptr = btrfs_item_ptr(eb, path->slots[0], struct btrfs_dev_stats_item);
	for (i = 0; i < BTRFS_DEV_STAT_VALUES_MAX; i++)
		btrfs_set_dev_stats_value(eb, ptr, i,
					  btrfs_dev_stat_read(device, i));
	btrfs_mark_buffer_dirty(eb);

out:
	btrfs_free_path(path);
	return ret;
}

/*
 * called from commit_transaction. Writes all changed device stats to disk.
 */
int btrfs_run_dev_stats(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	struct btrfs_device *device;
	int stats_cnt;
	int ret = 0;

	mutex_lock(&fs_devices->device_list_mutex);
	list_for_each_entry(device, &fs_devices->devices, dev_list) {
		stats_cnt = atomic_read(&device->dev_stats_ccnt);
		if (!device->dev_stats_valid || stats_cnt == 0)
			continue;


		/*
		 * There is a LOAD-LOAD control dependency between the value of
		 * dev_stats_ccnt and updating the on-disk values which requires
		 * reading the in-memory counters. Such control dependencies
		 * require explicit read memory barriers.
		 *
		 * This memory barriers pairs with smp_mb__before_atomic in
		 * btrfs_dev_stat_inc/btrfs_dev_stat_set and with the full
		 * barrier implied by atomic_xchg in
		 * btrfs_dev_stats_read_and_reset
		 */
		smp_rmb();

		ret = update_dev_stat_item(trans, device);
		if (!ret)
			atomic_sub(stats_cnt, &device->dev_stats_ccnt);
	}
	mutex_unlock(&fs_devices->device_list_mutex);

	return ret;
}

void btrfs_dev_stat_inc_and_print(struct btrfs_device *dev, int index)
{
	btrfs_dev_stat_inc(dev, index);
	btrfs_dev_stat_print_on_error(dev);
}

static void btrfs_dev_stat_print_on_error(struct btrfs_device *dev)
{
	if (!dev->dev_stats_valid)
		return;
	btrfs_err_rl_in_rcu(dev->fs_info,
		"bdev %s errs: wr %u, rd %u, flush %u, corrupt %u, gen %u",
			   rcu_str_deref(dev->name),
			   btrfs_dev_stat_read(dev, BTRFS_DEV_STAT_WRITE_ERRS),
			   btrfs_dev_stat_read(dev, BTRFS_DEV_STAT_READ_ERRS),
			   btrfs_dev_stat_read(dev, BTRFS_DEV_STAT_FLUSH_ERRS),
			   btrfs_dev_stat_read(dev, BTRFS_DEV_STAT_CORRUPTION_ERRS),
			   btrfs_dev_stat_read(dev, BTRFS_DEV_STAT_GENERATION_ERRS));
}

static void btrfs_dev_stat_print_on_load(struct btrfs_device *dev)
{
	int i;

	for (i = 0; i < BTRFS_DEV_STAT_VALUES_MAX; i++)
		if (btrfs_dev_stat_read(dev, i) != 0)
			break;
	if (i == BTRFS_DEV_STAT_VALUES_MAX)
		return; /* all values == 0, suppress message */

	btrfs_info_in_rcu(dev->fs_info,
		"bdev %s errs: wr %u, rd %u, flush %u, corrupt %u, gen %u",
	       rcu_str_deref(dev->name),
	       btrfs_dev_stat_read(dev, BTRFS_DEV_STAT_WRITE_ERRS),
	       btrfs_dev_stat_read(dev, BTRFS_DEV_STAT_READ_ERRS),
	       btrfs_dev_stat_read(dev, BTRFS_DEV_STAT_FLUSH_ERRS),
	       btrfs_dev_stat_read(dev, BTRFS_DEV_STAT_CORRUPTION_ERRS),
	       btrfs_dev_stat_read(dev, BTRFS_DEV_STAT_GENERATION_ERRS));
}

int btrfs_get_dev_stats(struct btrfs_fs_info *fs_info,
			struct btrfs_ioctl_get_dev_stats *stats)
{
	struct btrfs_device *dev;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	int i;

	mutex_lock(&fs_devices->device_list_mutex);
	dev = btrfs_find_device(fs_info->fs_devices, stats->devid, NULL, NULL,
				true);
	mutex_unlock(&fs_devices->device_list_mutex);

	if (!dev) {
		btrfs_warn(fs_info, "get dev_stats failed, device not found");
		return -ENODEV;
	} else if (!dev->dev_stats_valid) {
		btrfs_warn(fs_info, "get dev_stats failed, not yet valid");
		return -ENODEV;
	} else if (stats->flags & BTRFS_DEV_STATS_RESET) {
		for (i = 0; i < BTRFS_DEV_STAT_VALUES_MAX; i++) {
			if (stats->nr_items > i)
				stats->values[i] =
					btrfs_dev_stat_read_and_reset(dev, i);
			else
				btrfs_dev_stat_set(dev, i, 0);
		}
	} else {
		for (i = 0; i < BTRFS_DEV_STAT_VALUES_MAX; i++)
			if (stats->nr_items > i)
				stats->values[i] = btrfs_dev_stat_read(dev, i);
	}
	if (stats->nr_items > BTRFS_DEV_STAT_VALUES_MAX)
		stats->nr_items = BTRFS_DEV_STAT_VALUES_MAX;
	return 0;
}

void btrfs_scratch_superblocks(struct block_device *bdev, const char *device_path)
{
	struct buffer_head *bh;
	struct btrfs_super_block *disk_super;
	int copy_num;

	if (!bdev)
		return;

	for (copy_num = 0; copy_num < BTRFS_SUPER_MIRROR_MAX;
		copy_num++) {

		if (btrfs_read_dev_one_super(bdev, copy_num, &bh))
			continue;

		disk_super = (struct btrfs_super_block *)bh->b_data;

		memset(&disk_super->magic, 0, sizeof(disk_super->magic));
		set_buffer_dirty(bh);
		sync_dirty_buffer(bh);
		brelse(bh);
	}

	/* Notify udev that device has changed */
	btrfs_kobject_uevent(bdev, KOBJ_CHANGE);

	/* Update ctime/mtime for device path for libblkid */
	update_dev_time(device_path);
}

/*
 * Update the size and bytes used for each device where it changed.  This is
 * delayed since we would otherwise get errors while writing out the
 * superblocks.
 *
 * Must be invoked during transaction commit.
 */
void btrfs_commit_device_sizes(struct btrfs_transaction *trans)
{
	struct btrfs_device *curr, *next;

	ASSERT(trans->state == TRANS_STATE_COMMIT_DOING);

	if (list_empty(&trans->dev_update_list))
		return;

	/*
	 * We don't need the device_list_mutex here.  This list is owned by the
	 * transaction and the transaction must complete before the device is
	 * released.
	 */
	mutex_lock(&trans->fs_info->chunk_mutex);
	list_for_each_entry_safe(curr, next, &trans->dev_update_list,
				 post_commit_list) {
		list_del_init(&curr->post_commit_list);
		curr->commit_total_bytes = curr->disk_total_bytes;
		curr->commit_bytes_used = curr->bytes_used;
	}
	mutex_unlock(&trans->fs_info->chunk_mutex);
}

void btrfs_set_fs_info_ptr(struct btrfs_fs_info *fs_info)
{
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	while (fs_devices) {
		fs_devices->fs_info = fs_info;
		fs_devices = fs_devices->seed;
	}
}

void btrfs_reset_fs_info_ptr(struct btrfs_fs_info *fs_info)
{
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	while (fs_devices) {
		fs_devices->fs_info = NULL;
		fs_devices = fs_devices->seed;
	}
}

/*
 * Multiplicity factor for simple profiles: DUP, RAID1-like and RAID10.
 */
int btrfs_bg_type_to_factor(u64 flags)
{
	const int index = btrfs_bg_flags_to_raid_index(flags);

	return btrfs_raid_array[index].ncopies;
}



static int verify_one_dev_extent(struct btrfs_fs_info *fs_info,
				 u64 chunk_offset, u64 devid,
				 u64 physical_offset, u64 physical_len)
{
	struct extent_map_tree *em_tree = &fs_info->mapping_tree;
	struct extent_map *em;
	struct map_lookup *map;
	struct btrfs_device *dev;
	u64 stripe_len;
	bool found = false;
	int ret = 0;
	int i;

	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, chunk_offset, 1);
	read_unlock(&em_tree->lock);

	if (!em) {
		btrfs_err(fs_info,
"dev extent physical offset %llu on devid %llu doesn't have corresponding chunk",
			  physical_offset, devid);
		ret = -EUCLEAN;
		goto out;
	}

	map = em->map_lookup;
	stripe_len = calc_stripe_length(map->type, em->len, map->num_stripes);
	if (physical_len != stripe_len) {
		btrfs_err(fs_info,
"dev extent physical offset %llu on devid %llu length doesn't match chunk %llu, have %llu expect %llu",
			  physical_offset, devid, em->start, physical_len,
			  stripe_len);
		ret = -EUCLEAN;
		goto out;
	}

	for (i = 0; i < map->num_stripes; i++) {
		if (map->stripes[i].dev->devid == devid &&
		    map->stripes[i].physical == physical_offset) {
			found = true;
			if (map->verified_stripes >= map->num_stripes) {
				btrfs_err(fs_info,
				"too many dev extents for chunk %llu found",
					  em->start);
				ret = -EUCLEAN;
				goto out;
			}
			map->verified_stripes++;
			break;
		}
	}
	if (!found) {
		btrfs_err(fs_info,
	"dev extent physical offset %llu devid %llu has no corresponding chunk",
			physical_offset, devid);
		ret = -EUCLEAN;
	}

	/* Make sure no dev extent is beyond device bondary */
	dev = btrfs_find_device(fs_info->fs_devices, devid, NULL, NULL, true);
	if (!dev) {
		btrfs_err(fs_info, "failed to find devid %llu", devid);
		ret = -EUCLEAN;
		goto out;
	}

	/* It's possible this device is a dummy for seed device */
	if (dev->disk_total_bytes == 0) {
		dev = btrfs_find_device(fs_info->fs_devices->seed, devid, NULL,
					NULL, false);
		if (!dev) {
			btrfs_err(fs_info, "failed to find seed devid %llu",
				  devid);
			ret = -EUCLEAN;
			goto out;
		}
	}

	if (physical_offset + physical_len > dev->disk_total_bytes) {
		btrfs_err(fs_info,
"dev extent devid %llu physical offset %llu len %llu is beyond device boundary %llu",
			  devid, physical_offset, physical_len,
			  dev->disk_total_bytes);
		ret = -EUCLEAN;
		goto out;
	}
out:
	free_extent_map(em);
	return ret;
}

static int verify_chunk_dev_extent_mapping(struct btrfs_fs_info *fs_info)
{
	struct extent_map_tree *em_tree = &fs_info->mapping_tree;
	struct extent_map *em;
	struct rb_node *node;
	int ret = 0;

	read_lock(&em_tree->lock);
	for (node = rb_first_cached(&em_tree->map); node; node = rb_next(node)) {
		em = rb_entry(node, struct extent_map, rb_node);
		if (em->map_lookup->num_stripes !=
		    em->map_lookup->verified_stripes) {
			btrfs_err(fs_info,
			"chunk %llu has missing dev extent, have %d expect %d",
				  em->start, em->map_lookup->verified_stripes,
				  em->map_lookup->num_stripes);
			ret = -EUCLEAN;
			goto out;
		}
	}
out:
	read_unlock(&em_tree->lock);
	return ret;
}

/*
 * Ensure that all dev extents are mapped to correct chunk, otherwise
 * later chunk allocation/free would cause unexpected behavior.
 *
 * NOTE: This will iterate through the whole device tree, which should be of
 * the same size level as the chunk tree.  This slightly increases mount time.
 */
int btrfs_verify_dev_extents(struct btrfs_fs_info *fs_info)
{
	struct btrfs_path *path;
	struct btrfs_root *root = fs_info->dev_root;
	struct btrfs_key key;
	u64 prev_devid = 0;
	u64 prev_dev_ext_end = 0;
	int ret = 0;

	key.objectid = 1;
	key.type = BTRFS_DEV_EXTENT_KEY;
	key.offset = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = READA_FORWARD;
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;

	if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
		ret = btrfs_next_item(root, path);
		if (ret < 0)
			goto out;
		/* No dev extents at all? Not good */
		if (ret > 0) {
			ret = -EUCLEAN;
			goto out;
		}
	}
	while (1) {
		struct extent_buffer *leaf = path->nodes[0];
		struct btrfs_dev_extent *dext;
		int slot = path->slots[0];
		u64 chunk_offset;
		u64 physical_offset;
		u64 physical_len;
		u64 devid;

		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.type != BTRFS_DEV_EXTENT_KEY)
			break;
		devid = key.objectid;
		physical_offset = key.offset;

		dext = btrfs_item_ptr(leaf, slot, struct btrfs_dev_extent);
		chunk_offset = btrfs_dev_extent_chunk_offset(leaf, dext);
		physical_len = btrfs_dev_extent_length(leaf, dext);

		/* Check if this dev extent overlaps with the previous one */
		if (devid == prev_devid && physical_offset < prev_dev_ext_end) {
			btrfs_err(fs_info,
"dev extent devid %llu physical offset %llu overlap with previous dev extent end %llu",
				  devid, physical_offset, prev_dev_ext_end);
			ret = -EUCLEAN;
			goto out;
		}

		ret = verify_one_dev_extent(fs_info, chunk_offset, devid,
					    physical_offset, physical_len);
		if (ret < 0)
			goto out;
		prev_devid = devid;
		prev_dev_ext_end = physical_offset + physical_len;

		ret = btrfs_next_item(root, path);
		if (ret < 0)
			goto out;
		if (ret > 0) {
			ret = 0;
			break;
		}
	}

	/* Ensure all chunks have corresponding dev extents */
	ret = verify_chunk_dev_extent_mapping(fs_info);
out:
	btrfs_free_path(path);
	return ret;
}

/*
 * Check whether the given block group or device is pinned by any inode being
 * used as a swapfile.
 */
bool btrfs_pinned_by_swapfile(struct btrfs_fs_info *fs_info, void *ptr)
{
	struct btrfs_swapfile_pin *sp;
	struct rb_node *node;

	spin_lock(&fs_info->swapfile_pins_lock);
	node = fs_info->swapfile_pins.rb_node;
	while (node) {
		sp = rb_entry(node, struct btrfs_swapfile_pin, node);
		if (ptr < sp->ptr)
			node = node->rb_left;
		else if (ptr > sp->ptr)
			node = node->rb_right;
		else
			break;
	}
	spin_unlock(&fs_info->swapfile_pins_lock);
	return node != NULL;
}

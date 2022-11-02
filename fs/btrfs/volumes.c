// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/ratelimit.h>
#include <linux/kthread.h>
#include <linux/raid/pq.h>
#include <linux/semaphore.h>
#include <linux/uuid.h>
#include <linux/list_sort.h>
#include <linux/namei.h>
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
#include "discard.h"
#include "zoned.h"

static struct bio_set btrfs_bioset;

#define BTRFS_BLOCK_GROUP_STRIPE_MASK	(BTRFS_BLOCK_GROUP_RAID0 | \
					 BTRFS_BLOCK_GROUP_RAID10 | \
					 BTRFS_BLOCK_GROUP_RAID56_MASK)

const struct btrfs_raid_attr btrfs_raid_array[BTRFS_NR_RAID_TYPES] = {
	[BTRFS_RAID_RAID10] = {
		.sub_stripes	= 2,
		.dev_stripes	= 1,
		.devs_max	= 0,	/* 0 == as many as possible */
		.devs_min	= 2,
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
	[BTRFS_RAID_RAID1C3] = {
		.sub_stripes	= 1,
		.dev_stripes	= 1,
		.devs_max	= 3,
		.devs_min	= 3,
		.tolerated_failures = 2,
		.devs_increment	= 3,
		.ncopies	= 3,
		.nparity        = 0,
		.raid_name	= "raid1c3",
		.bg_flag	= BTRFS_BLOCK_GROUP_RAID1C3,
		.mindev_error	= BTRFS_ERROR_DEV_RAID1C3_MIN_NOT_MET,
	},
	[BTRFS_RAID_RAID1C4] = {
		.sub_stripes	= 1,
		.dev_stripes	= 1,
		.devs_max	= 4,
		.devs_min	= 4,
		.tolerated_failures = 3,
		.devs_increment	= 4,
		.ncopies	= 4,
		.nparity        = 0,
		.raid_name	= "raid1c4",
		.bg_flag	= BTRFS_BLOCK_GROUP_RAID1C4,
		.mindev_error	= BTRFS_ERROR_DEV_RAID1C4_MIN_NOT_MET,
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
		.devs_min	= 1,
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

/*
 * Convert block group flags (BTRFS_BLOCK_GROUP_*) to btrfs_raid_types, which
 * can be used as index to access btrfs_raid_array[].
 */
enum btrfs_raid_types __attribute_const__ btrfs_bg_flags_to_raid_index(u64 flags)
{
	const u64 profile = (flags & BTRFS_BLOCK_GROUP_PROFILE_MASK);

	if (!profile)
		return BTRFS_RAID_SINGLE;

	return BTRFS_BG_FLAG_TO_INDEX(profile);
}

const char *btrfs_bg_type_to_raid_name(u64 flags)
{
	const int index = btrfs_bg_flags_to_raid_index(flags);

	if (index >= BTRFS_NR_RAID_TYPES)
		return NULL;

	return btrfs_raid_array[index].raid_name;
}

int btrfs_nr_parity_stripes(u64 type)
{
	enum btrfs_raid_types index = btrfs_bg_flags_to_raid_index(type);

	return btrfs_raid_array[index].nparity;
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
static void btrfs_dev_stat_print_on_load(struct btrfs_device *device);
static int __btrfs_map_block(struct btrfs_fs_info *fs_info,
			     enum btrfs_map_op op, u64 logical, u64 *length,
			     struct btrfs_io_context **bioc_ret,
			     struct btrfs_io_stripe *smap,
			     int *mirror_num_ret, int need_raid_map);

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
 * does not protect: manipulation of the fs_devices::devices list in general
 * but in mount context it could be used to exclude list modifications by eg.
 * scan ioctl
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
 * Is not required at mount and close times, because our device list is
 * protected by the uuid_mutex at that point.
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
 *   device_list_mutex
 *     chunk_mutex
 *   balance_mutex
 *
 *
 * Exclusive operations
 * ====================
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
 * The status of exclusive operation is set and cleared atomically.
 * During the course of Paused state, fs_info::exclusive_operation remains set.
 * A device operation in Paused or Running state can be canceled or resumed
 * either by ioctl (Balance only) or when remounted as read-write.
 * The exclusive status is cleared when the device operation is canceled or
 * completed.
 */

DEFINE_MUTEX(uuid_mutex);
static LIST_HEAD(fs_uuids);
struct list_head * __attribute_const__ btrfs_get_fs_uuids(void)
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
	INIT_LIST_HEAD(&fs_devs->seed_list);
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
	btrfs_destroy_dev_zone_info(device);
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

static noinline struct btrfs_fs_devices *find_fsid(
		const u8 *fsid, const u8 *metadata_fsid)
{
	struct btrfs_fs_devices *fs_devices;

	ASSERT(fsid);

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

static struct btrfs_fs_devices *find_fsid_with_metadata_uuid(
				struct btrfs_super_block *disk_super)
{

	struct btrfs_fs_devices *fs_devices;

	/*
	 * Handle scanned device having completed its fsid change but
	 * belonging to a fs_devices that was created by first scanning
	 * a device which didn't have its fsid/metadata_uuid changed
	 * at all and the CHANGING_FSID_V2 flag set.
	 */
	list_for_each_entry(fs_devices, &fs_uuids, fs_list) {
		if (fs_devices->fsid_change &&
		    memcmp(disk_super->metadata_uuid, fs_devices->fsid,
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
		    memcmp(disk_super->metadata_uuid, fs_devices->metadata_uuid,
			   BTRFS_FSID_SIZE) == 0) {
			return fs_devices;
		}
	}

	return find_fsid(disk_super->fsid, disk_super->metadata_uuid);
}


static int
btrfs_get_bdev_and_sb(const char *device_path, fmode_t flags, void *holder,
		      int flush, struct block_device **bdev,
		      struct btrfs_super_block **disk_super)
{
	int ret;

	*bdev = blkdev_get_by_path(device_path, flags, holder);

	if (IS_ERR(*bdev)) {
		ret = PTR_ERR(*bdev);
		goto error;
	}

	if (flush)
		sync_blockdev(*bdev);
	ret = set_blocksize(*bdev, BTRFS_BDEV_BLOCKSIZE);
	if (ret) {
		blkdev_put(*bdev, flags);
		goto error;
	}
	invalidate_bdev(*bdev);
	*disk_super = btrfs_read_dev_super(*bdev);
	if (IS_ERR(*disk_super)) {
		ret = PTR_ERR(*disk_super);
		blkdev_put(*bdev, flags);
		goto error;
	}

	return 0;

error:
	*bdev = NULL;
	return ret;
}

/**
 *  Search and remove all stale devices (which are not mounted).
 *  When both inputs are NULL, it will search and release all stale devices.
 *
 *  @devt:	Optional. When provided will it release all unmounted devices
 *		matching this devt only.
 *  @skip_device:  Optional. Will skip this device when searching for the stale
 *		devices.
 *
 *  Return:	0 for success or if @devt is 0.
 *		-EBUSY if @devt is a mounted device.
 *		-ENOENT if @devt does not match any device in the list.
 */
static int btrfs_free_stale_devices(dev_t devt, struct btrfs_device *skip_device)
{
	struct btrfs_fs_devices *fs_devices, *tmp_fs_devices;
	struct btrfs_device *device, *tmp_device;
	int ret = 0;

	lockdep_assert_held(&uuid_mutex);

	if (devt)
		ret = -ENOENT;

	list_for_each_entry_safe(fs_devices, tmp_fs_devices, &fs_uuids, fs_list) {

		mutex_lock(&fs_devices->device_list_mutex);
		list_for_each_entry_safe(device, tmp_device,
					 &fs_devices->devices, dev_list) {
			if (skip_device && skip_device == device)
				continue;
			if (devt && devt != device->devt)
				continue;
			if (fs_devices->opened) {
				/* for an already deleted device return 0 */
				if (devt && ret != 0)
					ret = -EBUSY;
				break;
			}

			/* delete the stale device */
			fs_devices->num_devices--;
			list_del(&device->dev_list);
			btrfs_free_device(device);

			ret = 0;
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

/*
 * This is only used on mount, and we are protected from competing things
 * messing with our fs_devices by the uuid_mutex, thus we do not need the
 * fs_devices->device_list_mutex here.
 */
static int btrfs_open_one_device(struct btrfs_fs_devices *fs_devices,
			struct btrfs_device *device, fmode_t flags,
			void *holder)
{
	struct block_device *bdev;
	struct btrfs_super_block *disk_super;
	u64 devid;
	int ret;

	if (device->bdev)
		return -EINVAL;
	if (!device->name)
		return -EINVAL;

	ret = btrfs_get_bdev_and_sb(device->name->str, flags, holder, 1,
				    &bdev, &disk_super);
	if (ret)
		return ret;

	devid = btrfs_stack_device_id(&disk_super->dev_item);
	if (devid != device->devid)
		goto error_free_page;

	if (memcmp(device->uuid, disk_super->dev_item.uuid, BTRFS_UUID_SIZE))
		goto error_free_page;

	device->generation = btrfs_super_generation(disk_super);

	if (btrfs_super_flags(disk_super) & BTRFS_SUPER_FLAG_SEEDING) {
		if (btrfs_super_incompat_flags(disk_super) &
		    BTRFS_FEATURE_INCOMPAT_METADATA_UUID) {
			pr_err(
		"BTRFS: Invalid seeding and uuid-changed device detected\n");
			goto error_free_page;
		}

		clear_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state);
		fs_devices->seeding = true;
	} else {
		if (bdev_read_only(bdev))
			clear_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state);
		else
			set_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state);
	}

	if (!bdev_nonrot(bdev))
		fs_devices->rotating = true;

	device->bdev = bdev;
	clear_bit(BTRFS_DEV_STATE_IN_FS_METADATA, &device->dev_state);
	device->mode = flags;

	fs_devices->open_devices++;
	if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state) &&
	    device->devid != BTRFS_DEV_REPLACE_DEVID) {
		fs_devices->rw_devices++;
		list_add_tail(&device->dev_alloc_list, &fs_devices->alloc_list);
	}
	btrfs_release_disk_super(disk_super);

	return 0;

error_free_page:
	btrfs_release_disk_super(disk_super);
	blkdev_put(bdev, flags);

	return -EINVAL;
}

/*
 * Handle scanned device having its CHANGING_FSID_V2 flag set and the fs_devices
 * being created with a disk that has already completed its fsid change. Such
 * disk can belong to an fs which has its FSID changed or to one which doesn't.
 * Handle both cases here.
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

	return find_fsid(disk_super->fsid, NULL);
}


static struct btrfs_fs_devices *find_fsid_changed(
					struct btrfs_super_block *disk_super)
{
	struct btrfs_fs_devices *fs_devices;

	/*
	 * Handles the case where scanned device is part of an fs that had
	 * multiple successful changes of FSID but currently device didn't
	 * observe it. Meaning our fsid will be different than theirs. We need
	 * to handle two subcases :
	 *  1 - The fs still continues to have different METADATA/FSID uuids.
	 *  2 - The fs is switched back to its original FSID (METADATA/FSID
	 *  are equal).
	 */
	list_for_each_entry(fs_devices, &fs_uuids, fs_list) {
		/* Changed UUIDs */
		if (memcmp(fs_devices->metadata_uuid, fs_devices->fsid,
			   BTRFS_FSID_SIZE) != 0 &&
		    memcmp(fs_devices->metadata_uuid, disk_super->metadata_uuid,
			   BTRFS_FSID_SIZE) == 0 &&
		    memcmp(fs_devices->fsid, disk_super->fsid,
			   BTRFS_FSID_SIZE) != 0)
			return fs_devices;

		/* Unchanged UUIDs */
		if (memcmp(fs_devices->metadata_uuid, fs_devices->fsid,
			   BTRFS_FSID_SIZE) == 0 &&
		    memcmp(fs_devices->fsid, disk_super->metadata_uuid,
			   BTRFS_FSID_SIZE) == 0)
			return fs_devices;
	}

	return NULL;
}

static struct btrfs_fs_devices *find_fsid_reverted_metadata(
				struct btrfs_super_block *disk_super)
{
	struct btrfs_fs_devices *fs_devices;

	/*
	 * Handle the case where the scanned device is part of an fs whose last
	 * metadata UUID change reverted it to the original FSID. At the same
	 * time * fs_devices was first created by another constitutent device
	 * which didn't fully observe the operation. This results in an
	 * btrfs_fs_devices created with metadata/fsid different AND
	 * btrfs_fs_devices::fsid_change set AND the metadata_uuid of the
	 * fs_devices equal to the FSID of the disk.
	 */
	list_for_each_entry(fs_devices, &fs_uuids, fs_list) {
		if (memcmp(fs_devices->fsid, fs_devices->metadata_uuid,
			   BTRFS_FSID_SIZE) != 0 &&
		    memcmp(fs_devices->metadata_uuid, disk_super->fsid,
			   BTRFS_FSID_SIZE) == 0 &&
		    fs_devices->fsid_change)
			return fs_devices;
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
	dev_t path_devt;
	int error;
	bool has_metadata_uuid = (btrfs_super_incompat_flags(disk_super) &
		BTRFS_FEATURE_INCOMPAT_METADATA_UUID);
	bool fsid_change_in_progress = (btrfs_super_flags(disk_super) &
					BTRFS_SUPER_FLAG_CHANGING_FSID_V2);

	error = lookup_bdev(path, &path_devt);
	if (error)
		return ERR_PTR(error);

	if (fsid_change_in_progress) {
		if (!has_metadata_uuid)
			fs_devices = find_fsid_inprogress(disk_super);
		else
			fs_devices = find_fsid_changed(disk_super);
	} else if (has_metadata_uuid) {
		fs_devices = find_fsid_with_metadata_uuid(disk_super);
	} else {
		fs_devices = find_fsid_reverted_metadata(disk_super);
		if (!fs_devices)
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
		struct btrfs_dev_lookup_args args = {
			.devid = devid,
			.uuid = disk_super->dev_item.uuid,
		};

		mutex_lock(&fs_devices->device_list_mutex);
		device = btrfs_find_device(fs_devices, &args);

		/*
		 * If this disk has been pulled into an fs devices created by
		 * a device which had the CHANGING_FSID_V2 flag then replace the
		 * metadata_uuid/fsid values of the fs_devices.
		 */
		if (fs_devices->fsid_change &&
		    found_transid > fs_devices->latest_generation) {
			memcpy(fs_devices->fsid, disk_super->fsid,
					BTRFS_FSID_SIZE);

			if (has_metadata_uuid)
				memcpy(fs_devices->metadata_uuid,
				       disk_super->metadata_uuid,
				       BTRFS_FSID_SIZE);
			else
				memcpy(fs_devices->metadata_uuid,
				       disk_super->fsid, BTRFS_FSID_SIZE);

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
		device->devt = path_devt;

		list_add_rcu(&device->dev_list, &fs_devices->devices);
		fs_devices->num_devices++;

		device->fs_devices = fs_devices;
		*new_device_added = true;

		if (disk_super->label[0])
			pr_info(
	"BTRFS: device label %s devid %llu transid %llu %s scanned by %s (%d)\n",
				disk_super->label, devid, found_transid, path,
				current->comm, task_pid_nr(current));
		else
			pr_info(
	"BTRFS: device fsid %pU devid %llu transid %llu %s scanned by %s (%d)\n",
				disk_super->fsid, devid, found_transid, path,
				current->comm, task_pid_nr(current));

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
		 *
		 * NOTE: the device->fs_info may not be reliable here so pass
		 * in a NULL to message helpers instead. This avoids a possible
		 * use-after-free when the fs_info and fs_info->sb are already
		 * torn down.
		 */
		if (device->bdev) {
			if (device->devt != path_devt) {
				mutex_unlock(&fs_devices->device_list_mutex);
				btrfs_warn_in_rcu(NULL,
	"duplicate device %s devid %llu generation %llu scanned by %s (%d)",
						  path, devid, found_transid,
						  current->comm,
						  task_pid_nr(current));
				return ERR_PTR(-EEXIST);
			}
			btrfs_info_in_rcu(NULL,
	"devid %llu device path %s changed to %s scanned by %s (%d)",
					  devid, rcu_str_deref(device->name),
					  path, current->comm,
					  task_pid_nr(current));
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
		device->devt = path_devt;
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

	lockdep_assert_held(&uuid_mutex);

	fs_devices = alloc_fs_devices(orig->fsid, NULL);
	if (IS_ERR(fs_devices))
		return fs_devices;

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
	return fs_devices;
error:
	free_fs_devices(fs_devices);
	return ERR_PTR(ret);
}

static void __btrfs_free_extra_devids(struct btrfs_fs_devices *fs_devices,
				      struct btrfs_device **latest_dev)
{
	struct btrfs_device *device, *next;

	/* This is the initialized path, it is safe to release the devices. */
	list_for_each_entry_safe(device, next, &fs_devices->devices, dev_list) {
		if (test_bit(BTRFS_DEV_STATE_IN_FS_METADATA, &device->dev_state)) {
			if (!test_bit(BTRFS_DEV_STATE_REPLACE_TGT,
				      &device->dev_state) &&
			    !test_bit(BTRFS_DEV_STATE_MISSING,
				      &device->dev_state) &&
			    (!*latest_dev ||
			     device->generation > (*latest_dev)->generation)) {
				*latest_dev = device;
			}
			continue;
		}

		/*
		 * We have already validated the presence of BTRFS_DEV_REPLACE_DEVID,
		 * in btrfs_init_dev_replace() so just continue.
		 */
		if (device->devid == BTRFS_DEV_REPLACE_DEVID)
			continue;

		if (device->bdev) {
			blkdev_put(device->bdev, device->mode);
			device->bdev = NULL;
			fs_devices->open_devices--;
		}
		if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state)) {
			list_del_init(&device->dev_alloc_list);
			clear_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state);
			fs_devices->rw_devices--;
		}
		list_del_init(&device->dev_list);
		fs_devices->num_devices--;
		btrfs_free_device(device);
	}

}

/*
 * After we have read the system tree and know devids belonging to this
 * filesystem, remove the device which does not belong there.
 */
void btrfs_free_extra_devids(struct btrfs_fs_devices *fs_devices)
{
	struct btrfs_device *latest_dev = NULL;
	struct btrfs_fs_devices *seed_dev;

	mutex_lock(&uuid_mutex);
	__btrfs_free_extra_devids(fs_devices, &latest_dev);

	list_for_each_entry(seed_dev, &fs_devices->seed_list, seed_list)
		__btrfs_free_extra_devids(seed_dev, &latest_dev);

	fs_devices->latest_dev = latest_dev;

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

	if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state) &&
	    device->devid != BTRFS_DEV_REPLACE_DEVID) {
		list_del_init(&device->dev_alloc_list);
		fs_devices->rw_devices--;
	}

	if (device->devid == BTRFS_DEV_REPLACE_DEVID)
		clear_bit(BTRFS_DEV_STATE_REPLACE_TGT, &device->dev_state);

	if (test_bit(BTRFS_DEV_STATE_MISSING, &device->dev_state)) {
		clear_bit(BTRFS_DEV_STATE_MISSING, &device->dev_state);
		fs_devices->missing_devices--;
	}

	btrfs_close_bdev(device);
	if (device->bdev) {
		fs_devices->open_devices--;
		device->bdev = NULL;
	}
	clear_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state);
	btrfs_destroy_dev_zone_info(device);

	device->fs_info = NULL;
	atomic_set(&device->dev_stats_ccnt, 0);
	extent_io_tree_release(&device->alloc_state);

	/*
	 * Reset the flush error record. We might have a transient flush error
	 * in this mount, and if so we aborted the current transaction and set
	 * the fs to an error state, guaranteeing no super blocks can be further
	 * committed. However that error might be transient and if we unmount the
	 * filesystem and mount it again, we should allow the mount to succeed
	 * (btrfs_check_rw_degradable() should not fail) - if after mounting the
	 * filesystem again we still get flush errors, then we will again abort
	 * any transaction and set the error state, guaranteeing no commits of
	 * unsafe super blocks.
	 */
	device->last_flush_error = 0;

	/* Verify the device is back in a pristine state  */
	ASSERT(!test_bit(BTRFS_DEV_STATE_FLUSH_SENT, &device->dev_state));
	ASSERT(!test_bit(BTRFS_DEV_STATE_REPLACE_TGT, &device->dev_state));
	ASSERT(list_empty(&device->dev_alloc_list));
	ASSERT(list_empty(&device->post_commit_list));
}

static void close_fs_devices(struct btrfs_fs_devices *fs_devices)
{
	struct btrfs_device *device, *tmp;

	lockdep_assert_held(&uuid_mutex);

	if (--fs_devices->opened > 0)
		return;

	list_for_each_entry_safe(device, tmp, &fs_devices->devices, dev_list)
		btrfs_close_one_device(device);

	WARN_ON(fs_devices->open_devices);
	WARN_ON(fs_devices->rw_devices);
	fs_devices->opened = 0;
	fs_devices->seeding = false;
	fs_devices->fs_info = NULL;
}

void btrfs_close_devices(struct btrfs_fs_devices *fs_devices)
{
	LIST_HEAD(list);
	struct btrfs_fs_devices *tmp;

	mutex_lock(&uuid_mutex);
	close_fs_devices(fs_devices);
	if (!fs_devices->opened)
		list_splice_init(&fs_devices->seed_list, &list);

	list_for_each_entry_safe(fs_devices, tmp, &list, seed_list) {
		close_fs_devices(fs_devices);
		list_del(&fs_devices->seed_list);
		free_fs_devices(fs_devices);
	}
	mutex_unlock(&uuid_mutex);
}

static int open_fs_devices(struct btrfs_fs_devices *fs_devices,
				fmode_t flags, void *holder)
{
	struct btrfs_device *device;
	struct btrfs_device *latest_dev = NULL;
	struct btrfs_device *tmp_device;

	flags |= FMODE_EXCL;

	list_for_each_entry_safe(device, tmp_device, &fs_devices->devices,
				 dev_list) {
		int ret;

		ret = btrfs_open_one_device(fs_devices, device, flags, holder);
		if (ret == 0 &&
		    (!latest_dev || device->generation > latest_dev->generation)) {
			latest_dev = device;
		} else if (ret == -ENODATA) {
			fs_devices->num_devices--;
			list_del(&device->dev_list);
			btrfs_free_device(device);
		}
	}
	if (fs_devices->open_devices == 0)
		return -EINVAL;

	fs_devices->opened = 1;
	fs_devices->latest_dev = latest_dev;
	fs_devices->total_rw_bytes = 0;
	fs_devices->chunk_alloc_policy = BTRFS_CHUNK_ALLOC_REGULAR;
	fs_devices->read_policy = BTRFS_READ_POLICY_PID;

	return 0;
}

static int devid_cmp(void *priv, const struct list_head *a,
		     const struct list_head *b)
{
	const struct btrfs_device *dev1, *dev2;

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
	/*
	 * The device_list_mutex cannot be taken here in case opening the
	 * underlying device takes further locks like open_mutex.
	 *
	 * We also don't need the lock here as this is called during mount and
	 * exclusion is provided by uuid_mutex
	 */

	if (fs_devices->opened) {
		fs_devices->opened++;
		ret = 0;
	} else {
		list_sort(NULL, &fs_devices->devices, devid_cmp);
		ret = open_fs_devices(fs_devices, flags, holder);
	}

	return ret;
}

void btrfs_release_disk_super(struct btrfs_super_block *super)
{
	struct page *page = virt_to_page(super);

	put_page(page);
}

static struct btrfs_super_block *btrfs_read_disk_super(struct block_device *bdev,
						       u64 bytenr, u64 bytenr_orig)
{
	struct btrfs_super_block *disk_super;
	struct page *page;
	void *p;
	pgoff_t index;

	/* make sure our super fits in the device */
	if (bytenr + PAGE_SIZE >= bdev_nr_bytes(bdev))
		return ERR_PTR(-EINVAL);

	/* make sure our super fits in the page */
	if (sizeof(*disk_super) > PAGE_SIZE)
		return ERR_PTR(-EINVAL);

	/* make sure our super doesn't straddle pages on disk */
	index = bytenr >> PAGE_SHIFT;
	if ((bytenr + sizeof(*disk_super) - 1) >> PAGE_SHIFT != index)
		return ERR_PTR(-EINVAL);

	/* pull in the page with our super */
	page = read_cache_page_gfp(bdev->bd_inode->i_mapping, index, GFP_KERNEL);

	if (IS_ERR(page))
		return ERR_CAST(page);

	p = page_address(page);

	/* align our pointer to the offset of the super block */
	disk_super = p + offset_in_page(bytenr);

	if (btrfs_super_bytenr(disk_super) != bytenr_orig ||
	    btrfs_super_magic(disk_super) != BTRFS_MAGIC) {
		btrfs_release_disk_super(p);
		return ERR_PTR(-EINVAL);
	}

	if (disk_super->label[0] && disk_super->label[BTRFS_LABEL_SIZE - 1])
		disk_super->label[BTRFS_LABEL_SIZE - 1] = 0;

	return disk_super;
}

int btrfs_forget_devices(dev_t devt)
{
	int ret;

	mutex_lock(&uuid_mutex);
	ret = btrfs_free_stale_devices(devt, NULL);
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
	u64 bytenr, bytenr_orig;
	int ret;

	lockdep_assert_held(&uuid_mutex);

	/*
	 * we would like to check all the supers, but that would make
	 * a btrfs mount succeed after a mkfs from a different FS.
	 * So, we need to add a special mount option to scan for
	 * later supers, using BTRFS_SUPER_MIRROR_MAX instead
	 */
	flags |= FMODE_EXCL;

	bdev = blkdev_get_by_path(path, flags, holder);
	if (IS_ERR(bdev))
		return ERR_CAST(bdev);

	bytenr_orig = btrfs_sb_offset(0);
	ret = btrfs_sb_log_location_bdev(bdev, 0, READ, &bytenr);
	if (ret) {
		device = ERR_PTR(ret);
		goto error_bdev_put;
	}

	disk_super = btrfs_read_disk_super(bdev, bytenr, bytenr_orig);
	if (IS_ERR(disk_super)) {
		device = ERR_CAST(disk_super);
		goto error_bdev_put;
	}

	device = device_list_add(path, disk_super, &new_device_added);
	if (!IS_ERR(device) && new_device_added)
		btrfs_free_stale_devices(device->devt, device);

	btrfs_release_disk_super(disk_super);

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

static u64 dev_extent_search_start(struct btrfs_device *device, u64 start)
{
	switch (device->fs_devices->chunk_alloc_policy) {
	case BTRFS_CHUNK_ALLOC_REGULAR:
		return max_t(u64, start, BTRFS_DEVICE_RANGE_RESERVED);
	case BTRFS_CHUNK_ALLOC_ZONED:
		/*
		 * We don't care about the starting region like regular
		 * allocator, because we anyway use/reserve the first two zones
		 * for superblock logging.
		 */
		return ALIGN(start, device->zone_info->zone_size);
	default:
		BUG();
	}
}

static bool dev_extent_hole_check_zoned(struct btrfs_device *device,
					u64 *hole_start, u64 *hole_size,
					u64 num_bytes)
{
	u64 zone_size = device->zone_info->zone_size;
	u64 pos;
	int ret;
	bool changed = false;

	ASSERT(IS_ALIGNED(*hole_start, zone_size));

	while (*hole_size > 0) {
		pos = btrfs_find_allocatable_zones(device, *hole_start,
						   *hole_start + *hole_size,
						   num_bytes);
		if (pos != *hole_start) {
			*hole_size = *hole_start + *hole_size - pos;
			*hole_start = pos;
			changed = true;
			if (*hole_size < num_bytes)
				break;
		}

		ret = btrfs_ensure_empty_zones(device, pos, num_bytes);

		/* Range is ensured to be empty */
		if (!ret)
			return changed;

		/* Given hole range was invalid (outside of device) */
		if (ret == -ERANGE) {
			*hole_start += *hole_size;
			*hole_size = 0;
			return true;
		}

		*hole_start += zone_size;
		*hole_size -= zone_size;
		changed = true;
	}

	return changed;
}

/**
 * dev_extent_hole_check - check if specified hole is suitable for allocation
 * @device:	the device which we have the hole
 * @hole_start: starting position of the hole
 * @hole_size:	the size of the hole
 * @num_bytes:	the size of the free space that we need
 *
 * This function may modify @hole_start and @hole_size to reflect the suitable
 * position for allocation. Returns 1 if hole position is updated, 0 otherwise.
 */
static bool dev_extent_hole_check(struct btrfs_device *device, u64 *hole_start,
				  u64 *hole_size, u64 num_bytes)
{
	bool changed = false;
	u64 hole_end = *hole_start + *hole_size;

	for (;;) {
		/*
		 * Check before we set max_hole_start, otherwise we could end up
		 * sending back this offset anyway.
		 */
		if (contains_pending_extent(device, hole_start, *hole_size)) {
			if (hole_end >= *hole_start)
				*hole_size = hole_end - *hole_start;
			else
				*hole_size = 0;
			changed = true;
		}

		switch (device->fs_devices->chunk_alloc_policy) {
		case BTRFS_CHUNK_ALLOC_REGULAR:
			/* No extra check */
			break;
		case BTRFS_CHUNK_ALLOC_ZONED:
			if (dev_extent_hole_check_zoned(device, hole_start,
							hole_size, num_bytes)) {
				changed = true;
				/*
				 * The changed hole can contain pending extent.
				 * Loop again to check that.
				 */
				continue;
			}
			break;
		default:
			BUG();
		}

		break;
	}

	return changed;
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
 * is not reported as available.
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

	search_start = dev_extent_search_start(device, search_start);

	WARN_ON(device->zone_info &&
		!IS_ALIGNED(num_bytes, device->zone_info->zone_size));

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

	ret = btrfs_search_backwards(root, &key, path);
	if (ret < 0)
		goto out;

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
			dev_extent_hole_check(device, &search_start, &hole_size,
					      num_bytes);

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
		if (dev_extent_hole_check(device, &search_start, &hole_size,
					  num_bytes)) {
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
		goto out;
	}

	*dev_extent_len = btrfs_dev_extent_length(leaf, extent);

	ret = btrfs_del_item(trans, root, path);
	if (ret == 0)
		set_bit(BTRFS_TRANS_HAVE_FREE_BGS, &trans->transaction->flags);
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

	btrfs_reserve_chunk_metadata(trans, true);
	ret = btrfs_insert_empty_item(trans, trans->fs_info->chunk_root, path,
				      &key, sizeof(*dev_item));
	btrfs_trans_release_chunk_metadata(trans);
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
 *
 * We don't care about errors here, this is just to be kind to userspace.
 */
static void update_dev_time(const char *device_path)
{
	struct path path;
	struct timespec64 now;
	int ret;

	ret = kern_path(device_path, LOOKUP_FOLLOW, &path);
	if (ret)
		return;

	now = current_time(d_inode(path.dentry));
	inode_update_time(d_inode(path.dentry), &now, S_MTIME | S_CTIME);
	path_put(&path);
}

static int btrfs_rm_dev_item(struct btrfs_trans_handle *trans,
			     struct btrfs_device *device)
{
	struct btrfs_root *root = device->fs_info->chunk_root;
	int ret;
	struct btrfs_path *path;
	struct btrfs_key key;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = BTRFS_DEV_ITEMS_OBJECTID;
	key.type = BTRFS_DEV_ITEM_KEY;
	key.offset = device->devid;

	btrfs_reserve_chunk_metadata(trans, false);
	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	btrfs_trans_release_chunk_metadata(trans);
	if (ret) {
		if (ret > 0)
			ret = -ENOENT;
		goto out;
	}

	ret = btrfs_del_item(trans, root, path);
out:
	btrfs_free_path(path);
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

		if (num_devices < btrfs_raid_array[i].devs_min)
			return btrfs_raid_array[i].mindev_error;
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
 * Helper function to check if the given device is part of s_bdev / latest_dev
 * and replace it with the provided or the next active device, in the context
 * where this function called, there should be always be another device (or
 * this_dev) which is active.
 */
void __cold btrfs_assign_next_active_device(struct btrfs_device *device,
					    struct btrfs_device *next_device)
{
	struct btrfs_fs_info *fs_info = device->fs_info;

	if (!next_device)
		next_device = btrfs_find_next_active_device(fs_info->fs_devices,
							    device);
	ASSERT(next_device);

	if (fs_info->sb->s_bdev &&
			(fs_info->sb->s_bdev == device->bdev))
		fs_info->sb->s_bdev = next_device->bdev;

	if (fs_info->fs_devices->latest_dev->bdev == device->bdev)
		fs_info->fs_devices->latest_dev = next_device;
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

void btrfs_scratch_superblocks(struct btrfs_fs_info *fs_info,
			       struct block_device *bdev,
			       const char *device_path)
{
	struct btrfs_super_block *disk_super;
	int copy_num;

	if (!bdev)
		return;

	for (copy_num = 0; copy_num < BTRFS_SUPER_MIRROR_MAX; copy_num++) {
		struct page *page;
		int ret;

		disk_super = btrfs_read_dev_one_super(bdev, copy_num, false);
		if (IS_ERR(disk_super))
			continue;

		if (bdev_is_zoned(bdev)) {
			btrfs_reset_sb_log_zones(bdev, copy_num);
			continue;
		}

		memset(&disk_super->magic, 0, sizeof(disk_super->magic));

		page = virt_to_page(disk_super);
		set_page_dirty(page);
		lock_page(page);
		/* write_on_page() unlocks the page */
		ret = write_one_page(page);
		if (ret)
			btrfs_warn(fs_info,
				"error clearing superblock number %d (%d)",
				copy_num, ret);
		btrfs_release_disk_super(disk_super);

	}

	/* Notify udev that device has changed */
	btrfs_kobject_uevent(bdev, KOBJ_CHANGE);

	/* Update ctime/mtime for device path for libblkid */
	update_dev_time(device_path);
}

int btrfs_rm_device(struct btrfs_fs_info *fs_info,
		    struct btrfs_dev_lookup_args *args,
		    struct block_device **bdev, fmode_t *mode)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_device *device;
	struct btrfs_fs_devices *cur_devices;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	u64 num_devices;
	int ret = 0;

	if (btrfs_fs_incompat(fs_info, EXTENT_TREE_V2)) {
		btrfs_err(fs_info, "device remove not supported on extent tree v2 yet");
		return -EINVAL;
	}

	/*
	 * The device list in fs_devices is accessed without locks (neither
	 * uuid_mutex nor device_list_mutex) as it won't change on a mounted
	 * filesystem and another device rm cannot run.
	 */
	num_devices = btrfs_num_devices(fs_info);

	ret = btrfs_check_raid_min_devices(fs_info, num_devices - 1);
	if (ret)
		return ret;

	device = btrfs_find_device(fs_info->fs_devices, args);
	if (!device) {
		if (args->missing)
			ret = BTRFS_ERROR_DEV_MISSING_NOT_FOUND;
		else
			ret = -ENOENT;
		return ret;
	}

	if (btrfs_pinned_by_swapfile(fs_info, device)) {
		btrfs_warn_in_rcu(fs_info,
		  "cannot remove device %s (devid %llu) due to active swapfile",
				  rcu_str_deref(device->name), device->devid);
		return -ETXTBSY;
	}

	if (test_bit(BTRFS_DEV_STATE_REPLACE_TGT, &device->dev_state))
		return BTRFS_ERROR_DEV_TGT_REPLACE;

	if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state) &&
	    fs_info->fs_devices->rw_devices == 1)
		return BTRFS_ERROR_DEV_ONLY_WRITABLE;

	if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state)) {
		mutex_lock(&fs_info->chunk_mutex);
		list_del_init(&device->dev_alloc_list);
		device->fs_devices->rw_devices--;
		mutex_unlock(&fs_info->chunk_mutex);
	}

	ret = btrfs_shrink_device(device, 0);
	if (ret)
		goto error_undo;

	trans = btrfs_start_transaction(fs_info->chunk_root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto error_undo;
	}

	ret = btrfs_rm_dev_item(trans, device);
	if (ret) {
		/* Any error in dev item removal is critical */
		btrfs_crit(fs_info,
			   "failed to remove device item for devid %llu: %d",
			   device->devid, ret);
		btrfs_abort_transaction(trans, ret);
		btrfs_end_transaction(trans);
		return ret;
	}

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
	 * its own fs_devices listed under the fs_devices->seed_list.
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
		btrfs_sysfs_remove_device(device);
	}

	num_devices = btrfs_super_num_devices(fs_info->super_copy) - 1;
	btrfs_set_super_num_devices(fs_info->super_copy, num_devices);
	mutex_unlock(&fs_devices->device_list_mutex);

	/*
	 * At this point, the device is zero sized and detached from the
	 * devices list.  All that's left is to zero out the old supers and
	 * free the device.
	 *
	 * We cannot call btrfs_close_bdev() here because we're holding the sb
	 * write lock, and blkdev_put() will pull in the ->open_mutex on the
	 * block device and it's dependencies.  Instead just flush the device
	 * and let the caller do the final blkdev_put.
	 */
	if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state)) {
		btrfs_scratch_superblocks(fs_info, device->bdev,
					  device->name->str);
		if (device->bdev) {
			sync_blockdev(device->bdev);
			invalidate_bdev(device->bdev);
		}
	}

	*bdev = device->bdev;
	*mode = device->mode;
	synchronize_rcu();
	btrfs_free_device(device);

	/*
	 * This can happen if cur_devices is the private seed devices list.  We
	 * cannot call close_fs_devices() here because it expects the uuid_mutex
	 * to be held, but in fact we don't need that for the private
	 * seed_devices, we can simply decrement cur_devices->opened and then
	 * remove it from our list and free the fs_devices.
	 */
	if (cur_devices->num_devices == 0) {
		list_del_init(&cur_devices->seed_list);
		ASSERT(cur_devices->opened == 1);
		cur_devices->opened--;
		free_fs_devices(cur_devices);
	}

	ret = btrfs_commit_transaction(trans);

	return ret;

error_undo:
	if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state)) {
		mutex_lock(&fs_info->chunk_mutex);
		list_add(&device->dev_alloc_list,
			 &fs_devices->alloc_list);
		device->fs_devices->rw_devices++;
		mutex_unlock(&fs_info->chunk_mutex);
	}
	return ret;
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
	struct btrfs_fs_devices *fs_devices = srcdev->fs_devices;

	mutex_lock(&uuid_mutex);

	btrfs_close_bdev(srcdev);
	synchronize_rcu();
	btrfs_free_device(srcdev);

	/* if this is no devs we rather delete the fs_devices */
	if (!fs_devices->num_devices) {
		/*
		 * On a mounted FS, num_devices can't be zero unless it's a
		 * seed. In case of a seed device being replaced, the replace
		 * target added to the sprout FS, so there will be no more
		 * device left under the seed FS.
		 */
		ASSERT(fs_devices->seeding);

		list_del_init(&fs_devices->seed_list);
		close_fs_devices(fs_devices);
		free_fs_devices(fs_devices);
	}
	mutex_unlock(&uuid_mutex);
}

void btrfs_destroy_dev_replace_tgtdev(struct btrfs_device *tgtdev)
{
	struct btrfs_fs_devices *fs_devices = tgtdev->fs_info->fs_devices;

	mutex_lock(&fs_devices->device_list_mutex);

	btrfs_sysfs_remove_device(tgtdev);

	if (tgtdev->bdev)
		fs_devices->open_devices--;

	fs_devices->num_devices--;

	btrfs_assign_next_active_device(tgtdev, NULL);

	list_del_rcu(&tgtdev->dev_list);

	mutex_unlock(&fs_devices->device_list_mutex);

	btrfs_scratch_superblocks(tgtdev->fs_info, tgtdev->bdev,
				  tgtdev->name->str);

	btrfs_close_bdev(tgtdev);
	synchronize_rcu();
	btrfs_free_device(tgtdev);
}

/**
 * Populate args from device at path
 *
 * @fs_info:	the filesystem
 * @args:	the args to populate
 * @path:	the path to the device
 *
 * This will read the super block of the device at @path and populate @args with
 * the devid, fsid, and uuid.  This is meant to be used for ioctls that need to
 * lookup a device to operate on, but need to do it before we take any locks.
 * This properly handles the special case of "missing" that a user may pass in,
 * and does some basic sanity checks.  The caller must make sure that @path is
 * properly NUL terminated before calling in, and must call
 * btrfs_put_dev_args_from_path() in order to free up the temporary fsid and
 * uuid buffers.
 *
 * Return: 0 for success, -errno for failure
 */
int btrfs_get_dev_args_from_path(struct btrfs_fs_info *fs_info,
				 struct btrfs_dev_lookup_args *args,
				 const char *path)
{
	struct btrfs_super_block *disk_super;
	struct block_device *bdev;
	int ret;

	if (!path || !path[0])
		return -EINVAL;
	if (!strcmp(path, "missing")) {
		args->missing = true;
		return 0;
	}

	args->uuid = kzalloc(BTRFS_UUID_SIZE, GFP_KERNEL);
	args->fsid = kzalloc(BTRFS_FSID_SIZE, GFP_KERNEL);
	if (!args->uuid || !args->fsid) {
		btrfs_put_dev_args_from_path(args);
		return -ENOMEM;
	}

	ret = btrfs_get_bdev_and_sb(path, FMODE_READ, fs_info->bdev_holder, 0,
				    &bdev, &disk_super);
	if (ret) {
		btrfs_put_dev_args_from_path(args);
		return ret;
	}

	args->devid = btrfs_stack_device_id(&disk_super->dev_item);
	memcpy(args->uuid, disk_super->dev_item.uuid, BTRFS_UUID_SIZE);
	if (btrfs_fs_incompat(fs_info, METADATA_UUID))
		memcpy(args->fsid, disk_super->metadata_uuid, BTRFS_FSID_SIZE);
	else
		memcpy(args->fsid, disk_super->fsid, BTRFS_FSID_SIZE);
	btrfs_release_disk_super(disk_super);
	blkdev_put(bdev, FMODE_READ);
	return 0;
}

/*
 * Only use this jointly with btrfs_get_dev_args_from_path() because we will
 * allocate our ->uuid and ->fsid pointers, everybody else uses local variables
 * that don't need to be freed.
 */
void btrfs_put_dev_args_from_path(struct btrfs_dev_lookup_args *args)
{
	kfree(args->uuid);
	kfree(args->fsid);
	args->uuid = NULL;
	args->fsid = NULL;
}

struct btrfs_device *btrfs_find_device_by_devspec(
		struct btrfs_fs_info *fs_info, u64 devid,
		const char *device_path)
{
	BTRFS_DEV_LOOKUP_ARGS(args);
	struct btrfs_device *device;
	int ret;

	if (devid) {
		args.devid = devid;
		device = btrfs_find_device(fs_info->fs_devices, &args);
		if (!device)
			return ERR_PTR(-ENOENT);
		return device;
	}

	ret = btrfs_get_dev_args_from_path(fs_info, &args, device_path);
	if (ret)
		return ERR_PTR(ret);
	device = btrfs_find_device(fs_info->fs_devices, &args);
	btrfs_put_dev_args_from_path(&args);
	if (!device)
		return ERR_PTR(-ENOENT);
	return device;
}

static struct btrfs_fs_devices *btrfs_init_sprout(struct btrfs_fs_info *fs_info)
{
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	struct btrfs_fs_devices *old_devices;
	struct btrfs_fs_devices *seed_devices;

	lockdep_assert_held(&uuid_mutex);
	if (!fs_devices->seeding)
		return ERR_PTR(-EINVAL);

	/*
	 * Private copy of the seed devices, anchored at
	 * fs_info->fs_devices->seed_list
	 */
	seed_devices = alloc_fs_devices(NULL, NULL);
	if (IS_ERR(seed_devices))
		return seed_devices;

	/*
	 * It's necessary to retain a copy of the original seed fs_devices in
	 * fs_uuids so that filesystems which have been seeded can successfully
	 * reference the seed device from open_seed_devices. This also supports
	 * multiple fs seed.
	 */
	old_devices = clone_fs_devices(fs_devices);
	if (IS_ERR(old_devices)) {
		kfree(seed_devices);
		return old_devices;
	}

	list_add(&old_devices->fs_list, &fs_uuids);

	memcpy(seed_devices, fs_devices, sizeof(*seed_devices));
	seed_devices->opened = 1;
	INIT_LIST_HEAD(&seed_devices->devices);
	INIT_LIST_HEAD(&seed_devices->alloc_list);
	mutex_init(&seed_devices->device_list_mutex);

	return seed_devices;
}

/*
 * Splice seed devices into the sprout fs_devices.
 * Generate a new fsid for the sprouted read-write filesystem.
 */
static void btrfs_setup_sprout(struct btrfs_fs_info *fs_info,
			       struct btrfs_fs_devices *seed_devices)
{
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	struct btrfs_super_block *disk_super = fs_info->super_copy;
	struct btrfs_device *device;
	u64 super_flags;

	/*
	 * We are updating the fsid, the thread leading to device_list_add()
	 * could race, so uuid_mutex is needed.
	 */
	lockdep_assert_held(&uuid_mutex);

	/*
	 * The threads listed below may traverse dev_list but can do that without
	 * device_list_mutex:
	 * - All device ops and balance - as we are in btrfs_exclop_start.
	 * - Various dev_list readers - are using RCU.
	 * - btrfs_ioctl_fitrim() - is using RCU.
	 *
	 * For-read threads as below are using device_list_mutex:
	 * - Readonly scrub btrfs_scrub_dev()
	 * - Readonly scrub btrfs_scrub_progress()
	 * - btrfs_get_dev_stats()
	 */
	lockdep_assert_held(&fs_devices->device_list_mutex);

	list_splice_init_rcu(&fs_devices->devices, &seed_devices->devices,
			      synchronize_rcu);
	list_for_each_entry(device, &seed_devices->devices, dev_list)
		device->fs_devices = seed_devices;

	fs_devices->seeding = false;
	fs_devices->num_devices = 0;
	fs_devices->open_devices = 0;
	fs_devices->missing_devices = 0;
	fs_devices->rotating = false;
	list_add(&seed_devices->seed_list, &fs_devices->seed_list);

	generate_random_uuid(fs_devices->fsid);
	memcpy(fs_devices->metadata_uuid, fs_devices->fsid, BTRFS_FSID_SIZE);
	memcpy(disk_super->fsid, fs_devices->fsid, BTRFS_FSID_SIZE);

	super_flags = btrfs_super_flags(disk_super) &
		      ~BTRFS_SUPER_FLAG_SEEDING;
	btrfs_set_super_flags(disk_super, super_flags);
}

/*
 * Store the expected generation for seed devices in device items.
 */
static int btrfs_finish_sprout(struct btrfs_trans_handle *trans)
{
	BTRFS_DEV_LOOKUP_ARGS(args);
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *root = fs_info->chunk_root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_dev_item *dev_item;
	struct btrfs_device *device;
	struct btrfs_key key;
	u8 fs_uuid[BTRFS_FSID_SIZE];
	u8 dev_uuid[BTRFS_UUID_SIZE];
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = BTRFS_DEV_ITEMS_OBJECTID;
	key.offset = 0;
	key.type = BTRFS_DEV_ITEM_KEY;

	while (1) {
		btrfs_reserve_chunk_metadata(trans, false);
		ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
		btrfs_trans_release_chunk_metadata(trans);
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
		args.devid = btrfs_device_id(leaf, dev_item);
		read_extent_buffer(leaf, dev_uuid, btrfs_device_uuid(dev_item),
				   BTRFS_UUID_SIZE);
		read_extent_buffer(leaf, fs_uuid, btrfs_device_fsid(dev_item),
				   BTRFS_FSID_SIZE);
		args.uuid = dev_uuid;
		args.fsid = fs_uuid;
		device = btrfs_find_device(fs_info->fs_devices, &args);
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
	struct btrfs_trans_handle *trans;
	struct btrfs_device *device;
	struct block_device *bdev;
	struct super_block *sb = fs_info->sb;
	struct rcu_string *name;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	struct btrfs_fs_devices *seed_devices;
	u64 orig_super_total_bytes;
	u64 orig_super_num_devices;
	int ret = 0;
	bool seeding_dev = false;
	bool locked = false;

	if (sb_rdonly(sb) && !fs_devices->seeding)
		return -EROFS;

	bdev = blkdev_get_by_path(device_path, FMODE_WRITE | FMODE_EXCL,
				  fs_info->bdev_holder);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);

	if (!btrfs_check_device_zone_type(fs_info, bdev)) {
		ret = -EINVAL;
		goto error;
	}

	if (fs_devices->seeding) {
		seeding_dev = true;
		down_write(&sb->s_umount);
		mutex_lock(&uuid_mutex);
		locked = true;
	}

	sync_blockdev(bdev);

	rcu_read_lock();
	list_for_each_entry_rcu(device, &fs_devices->devices, dev_list) {
		if (device->bdev == bdev) {
			ret = -EEXIST;
			rcu_read_unlock();
			goto error;
		}
	}
	rcu_read_unlock();

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

	device->fs_info = fs_info;
	device->bdev = bdev;
	ret = lookup_bdev(device_path, &device->devt);
	if (ret)
		goto error_free_device;

	ret = btrfs_get_dev_zone_info(device, false);
	if (ret)
		goto error_free_device;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto error_free_zone;
	}

	set_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state);
	device->generation = trans->transid;
	device->io_width = fs_info->sectorsize;
	device->io_align = fs_info->sectorsize;
	device->sector_size = fs_info->sectorsize;
	device->total_bytes =
		round_down(bdev_nr_bytes(bdev), fs_info->sectorsize);
	device->disk_total_bytes = device->total_bytes;
	device->commit_total_bytes = device->total_bytes;
	set_bit(BTRFS_DEV_STATE_IN_FS_METADATA, &device->dev_state);
	clear_bit(BTRFS_DEV_STATE_REPLACE_TGT, &device->dev_state);
	device->mode = FMODE_EXCL;
	device->dev_stats_valid = 1;
	set_blocksize(device->bdev, BTRFS_BDEV_BLOCKSIZE);

	if (seeding_dev) {
		btrfs_clear_sb_rdonly(sb);

		/* GFP_KERNEL allocation must not be under device_list_mutex */
		seed_devices = btrfs_init_sprout(fs_info);
		if (IS_ERR(seed_devices)) {
			ret = PTR_ERR(seed_devices);
			btrfs_abort_transaction(trans, ret);
			goto error_trans;
		}
	}

	mutex_lock(&fs_devices->device_list_mutex);
	if (seeding_dev) {
		btrfs_setup_sprout(fs_info, seed_devices);
		btrfs_assign_next_active_device(fs_info->fs_devices->latest_dev,
						device);
	}

	device->fs_devices = fs_devices;

	mutex_lock(&fs_info->chunk_mutex);
	list_add_rcu(&device->dev_list, &fs_devices->devices);
	list_add(&device->dev_alloc_list, &fs_devices->alloc_list);
	fs_devices->num_devices++;
	fs_devices->open_devices++;
	fs_devices->rw_devices++;
	fs_devices->total_devices++;
	fs_devices->total_rw_bytes += device->total_bytes;

	atomic64_add(device->total_bytes, &fs_info->free_chunk_space);

	if (!bdev_nonrot(bdev))
		fs_devices->rotating = true;

	orig_super_total_bytes = btrfs_super_total_bytes(fs_info->super_copy);
	btrfs_set_super_total_bytes(fs_info->super_copy,
		round_down(orig_super_total_bytes + device->total_bytes,
			   fs_info->sectorsize));

	orig_super_num_devices = btrfs_super_num_devices(fs_info->super_copy);
	btrfs_set_super_num_devices(fs_info->super_copy,
				    orig_super_num_devices + 1);

	/*
	 * we've got more storage, clear any full flags on the space
	 * infos
	 */
	btrfs_clear_space_info_full(fs_info);

	mutex_unlock(&fs_info->chunk_mutex);

	/* Add sysfs device entry */
	btrfs_sysfs_add_device(device);

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

		/*
		 * fs_devices now represents the newly sprouted filesystem and
		 * its fsid has been changed by btrfs_sprout_splice().
		 */
		btrfs_sysfs_update_sprout_fsid(fs_devices);
	}

	ret = btrfs_commit_transaction(trans);

	if (seeding_dev) {
		mutex_unlock(&uuid_mutex);
		up_write(&sb->s_umount);
		locked = false;

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

	/*
	 * Now that we have written a new super block to this device, check all
	 * other fs_devices list if device_path alienates any other scanned
	 * device.
	 * We can ignore the return value as it typically returns -EINVAL and
	 * only succeeds if the device was an alien.
	 */
	btrfs_forget_devices(device->devt);

	/* Update ctime/mtime for blkid or udev */
	update_dev_time(device_path);

	return ret;

error_sysfs:
	btrfs_sysfs_remove_device(device);
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
		btrfs_set_sb_rdonly(sb);
	if (trans)
		btrfs_end_transaction(trans);
error_free_zone:
	btrfs_destroy_dev_zone_info(device);
error_free_device:
	btrfs_free_device(device);
error:
	blkdev_put(bdev, FMODE_EXCL);
	if (locked) {
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
	int ret;

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

	btrfs_reserve_chunk_metadata(trans, false);
	ret = btrfs_update_device(trans, device);
	btrfs_trans_release_chunk_metadata(trans);

	return ret;
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

	lockdep_assert_held(&fs_info->chunk_mutex);
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

static int remove_chunk_item(struct btrfs_trans_handle *trans,
			     struct map_lookup *map, u64 chunk_offset)
{
	int i;

	/*
	 * Removing chunk items and updating the device items in the chunks btree
	 * requires holding the chunk_mutex.
	 * See the comment at btrfs_chunk_alloc() for the details.
	 */
	lockdep_assert_held(&trans->fs_info->chunk_mutex);

	for (i = 0; i < map->num_stripes; i++) {
		int ret;

		ret = btrfs_update_device(trans, map->stripes[i].dev);
		if (ret)
			return ret;
	}

	return btrfs_free_chunk(trans, chunk_offset);
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

	/*
	 * First delete the device extent items from the devices btree.
	 * We take the device_list_mutex to avoid racing with the finishing phase
	 * of a device replace operation. See the comment below before acquiring
	 * fs_info->chunk_mutex. Note that here we do not acquire the chunk_mutex
	 * because that can result in a deadlock when deleting the device extent
	 * items from the devices btree - COWing an extent buffer from the btree
	 * may result in allocating a new metadata chunk, which would attempt to
	 * lock again fs_info->chunk_mutex.
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
	}
	mutex_unlock(&fs_devices->device_list_mutex);

	/*
	 * We acquire fs_info->chunk_mutex for 2 reasons:
	 *
	 * 1) Just like with the first phase of the chunk allocation, we must
	 *    reserve system space, do all chunk btree updates and deletions, and
	 *    update the system chunk array in the superblock while holding this
	 *    mutex. This is for similar reasons as explained on the comment at
	 *    the top of btrfs_chunk_alloc();
	 *
	 * 2) Prevent races with the final phase of a device replace operation
	 *    that replaces the device object associated with the map's stripes,
	 *    because the device object's id can change at any time during that
	 *    final phase of the device replace operation
	 *    (dev-replace.c:btrfs_dev_replace_finishing()), so we could grab the
	 *    replaced device and then see it with an ID of
	 *    BTRFS_DEV_REPLACE_DEVID, which would cause a failure when updating
	 *    the device item, which does not exists on the chunk btree.
	 *    The finishing phase of device replace acquires both the
	 *    device_list_mutex and the chunk_mutex, in that order, so we are
	 *    safe by just acquiring the chunk_mutex.
	 */
	trans->removing_chunk = true;
	mutex_lock(&fs_info->chunk_mutex);

	check_system_chunk(trans, map->type);

	ret = remove_chunk_item(trans, map, chunk_offset);
	/*
	 * Normally we should not get -ENOSPC since we reserved space before
	 * through the call to check_system_chunk().
	 *
	 * Despite our system space_info having enough free space, we may not
	 * be able to allocate extents from its block groups, because all have
	 * an incompatible profile, which will force us to allocate a new system
	 * block group with the right profile, or right after we called
	 * check_system_space() above, a scrub turned the only system block group
	 * with enough free space into RO mode.
	 * This is explained with more detail at do_chunk_alloc().
	 *
	 * So if we get -ENOSPC, allocate a new system chunk and retry once.
	 */
	if (ret == -ENOSPC) {
		const u64 sys_flags = btrfs_system_alloc_profile(fs_info);
		struct btrfs_block_group *sys_bg;

		sys_bg = btrfs_create_chunk(trans, sys_flags);
		if (IS_ERR(sys_bg)) {
			ret = PTR_ERR(sys_bg);
			btrfs_abort_transaction(trans, ret);
			goto out;
		}

		ret = btrfs_chunk_alloc_add_chunk_item(trans, sys_bg);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto out;
		}

		ret = remove_chunk_item(trans, map, chunk_offset);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto out;
		}
	} else if (ret) {
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

	mutex_unlock(&fs_info->chunk_mutex);
	trans->removing_chunk = false;

	/*
	 * We are done with chunk btree updates and deletions, so release the
	 * system space we previously reserved (with check_system_chunk()).
	 */
	btrfs_trans_release_chunk_metadata(trans);

	ret = btrfs_remove_block_group(trans, chunk_offset, em);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

out:
	if (trans->removing_chunk) {
		mutex_unlock(&fs_info->chunk_mutex);
		trans->removing_chunk = false;
	}
	/* once for us */
	free_extent_map(em);
	return ret;
}

int btrfs_relocate_chunk(struct btrfs_fs_info *fs_info, u64 chunk_offset)
{
	struct btrfs_root *root = fs_info->chunk_root;
	struct btrfs_trans_handle *trans;
	struct btrfs_block_group *block_group;
	u64 length;
	int ret;

	if (btrfs_fs_incompat(fs_info, EXTENT_TREE_V2)) {
		btrfs_err(fs_info,
			  "relocate: not supported on extent tree v2 yet");
		return -EINVAL;
	}

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
	lockdep_assert_held(&fs_info->reclaim_bgs_lock);

	/* step one, relocate all the extents inside this chunk */
	btrfs_scrub_pause(fs_info);
	ret = btrfs_relocate_block_group(fs_info, chunk_offset);
	btrfs_scrub_continue(fs_info);
	if (ret)
		return ret;

	block_group = btrfs_lookup_block_group(fs_info, chunk_offset);
	if (!block_group)
		return -ENOENT;
	btrfs_discard_cancel_work(&fs_info->discard_ctl, block_group);
	length = block_group->length;
	btrfs_put_block_group(block_group);

	/*
	 * On a zoned file system, discard the whole block group, this will
	 * trigger a REQ_OP_ZONE_RESET operation on the device zone. If
	 * resetting the zone fails, don't treat it as a fatal problem from the
	 * filesystem's point of view.
	 */
	if (btrfs_is_zoned(fs_info)) {
		ret = btrfs_discard_extent(fs_info, chunk_offset, length, NULL);
		if (ret)
			btrfs_info(fs_info,
				"failed to reset zone %llu after relocation",
				chunk_offset);
	}

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
		mutex_lock(&fs_info->reclaim_bgs_lock);
		ret = btrfs_search_slot(NULL, chunk_root, &key, path, 0, 0);
		if (ret < 0) {
			mutex_unlock(&fs_info->reclaim_bgs_lock);
			goto error;
		}
		BUG_ON(ret == 0); /* Corruption */

		ret = btrfs_previous_item(chunk_root, path, key.objectid,
					  key.type);
		if (ret)
			mutex_unlock(&fs_info->reclaim_bgs_lock);
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
		mutex_unlock(&fs_info->reclaim_bgs_lock);

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
	struct btrfs_block_group *cache;
	u64 bytes_used;
	u64 chunk_type;

	cache = btrfs_lookup_block_group(fs_info, chunk_offset);
	ASSERT(cache);
	chunk_type = cache->flags;
	btrfs_put_block_group(cache);

	if (!(chunk_type & BTRFS_BLOCK_GROUP_DATA))
		return 0;

	spin_lock(&fs_info->data_sinfo->lock);
	bytes_used = fs_info->data_sinfo->bytes_used;
	spin_unlock(&fs_info->data_sinfo->lock);

	if (!bytes_used) {
		struct btrfs_trans_handle *trans;
		int ret;

		trans =	btrfs_join_transaction(fs_info->tree_root);
		if (IS_ERR(trans))
			return PTR_ERR(trans);

		ret = btrfs_force_chunk_alloc(trans, BTRFS_BLOCK_GROUP_DATA);
		btrfs_end_transaction(trans);
		if (ret < 0)
			return ret;
		return 1;
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

	trans = btrfs_start_transaction_fallback_global_rsv(root, 0);
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
	struct btrfs_block_group *cache;
	u64 chunk_used;
	u64 user_thresh_min;
	u64 user_thresh_max;
	int ret = 1;

	cache = btrfs_lookup_block_group(fs_info, chunk_offset);
	chunk_used = cache->used;

	if (bargs->usage_min == 0)
		user_thresh_min = 0;
	else
		user_thresh_min = div_factor_fine(cache->length,
						  bargs->usage_min);

	if (bargs->usage_max == 0)
		user_thresh_max = 1;
	else if (bargs->usage_max > 100)
		user_thresh_max = cache->length;
	else
		user_thresh_max = div_factor_fine(cache->length,
						  bargs->usage_max);

	if (user_thresh_min <= chunk_used && chunk_used < user_thresh_max)
		ret = 0;

	btrfs_put_block_group(cache);
	return ret;
}

static int chunk_usage_filter(struct btrfs_fs_info *fs_info,
		u64 chunk_offset, struct btrfs_balance_args *bargs)
{
	struct btrfs_block_group *cache;
	u64 chunk_used, user_thresh;
	int ret = 1;

	cache = btrfs_lookup_block_group(fs_info, chunk_offset);
	chunk_used = cache->used;

	if (bargs->usage_min == 0)
		user_thresh = 1;
	else if (bargs->usage > 100)
		user_thresh = cache->length;
	else
		user_thresh = div_factor_fine(cache->length, bargs->usage);

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

	return (num_stripes - nparity) / ncopies;
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

		mutex_lock(&fs_info->reclaim_bgs_lock);
		ret = btrfs_search_slot(NULL, chunk_root, &key, path, 0, 0);
		if (ret < 0) {
			mutex_unlock(&fs_info->reclaim_bgs_lock);
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
			mutex_unlock(&fs_info->reclaim_bgs_lock);
			ret = 0;
			break;
		}

		leaf = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		if (found_key.objectid != key.objectid) {
			mutex_unlock(&fs_info->reclaim_bgs_lock);
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
			mutex_unlock(&fs_info->reclaim_bgs_lock);
			goto loop;
		}

		if (counting) {
			mutex_unlock(&fs_info->reclaim_bgs_lock);
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
			mutex_unlock(&fs_info->reclaim_bgs_lock);
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
				mutex_unlock(&fs_info->reclaim_bgs_lock);
				goto error;
			} else if (ret == 1) {
				chunk_reserved = 1;
			}
		}

		ret = btrfs_relocate_chunk(fs_info, found_key.offset);
		mutex_unlock(&fs_info->reclaim_bgs_lock);
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

	return has_single_bit_set(flags);
}

static inline int balance_need_close(struct btrfs_fs_info *fs_info)
{
	/* cancel requested || normal exit path */
	return atomic_read(&fs_info->balance_cancel_req) ||
		(atomic_read(&fs_info->balance_pause_req) == 0 &&
		 atomic_read(&fs_info->balance_cancel_req) == 0);
}

/*
 * Validate target profile against allowed profiles and return true if it's OK.
 * Otherwise print the error message and return false.
 */
static inline int validate_convert_profile(struct btrfs_fs_info *fs_info,
		const struct btrfs_balance_args *bargs,
		u64 allowed, const char *type)
{
	if (!(bargs->flags & BTRFS_BALANCE_ARGS_CONVERT))
		return true;

	/* Profile is valid and does not have bits outside of the allowed set */
	if (alloc_profile_is_valid(bargs->target, 1) &&
	    (bargs->target & ~allowed) == 0)
		return true;

	btrfs_err(fs_info, "balance: invalid convert %s profile %s",
			type, btrfs_bg_type_to_raid_name(bargs->target));
	return false;
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
	bool reducing_redundancy;
	int i;

	if (btrfs_fs_closing(fs_info) ||
	    atomic_read(&fs_info->balance_pause_req) ||
	    btrfs_should_cancel_balance(fs_info)) {
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

	/*
	 * rw_devices will not change at the moment, device add/delete/replace
	 * are exclusive
	 */
	num_devices = fs_info->fs_devices->rw_devices;

	/*
	 * SINGLE profile on-disk has no profile bit, but in-memory we have a
	 * special bit for it, to make it easier to distinguish.  Thus we need
	 * to set it manually, or balance would refuse the profile.
	 */
	allowed = BTRFS_AVAIL_ALLOC_BIT_SINGLE;
	for (i = 0; i < ARRAY_SIZE(btrfs_raid_array); i++)
		if (num_devices >= btrfs_raid_array[i].devs_min)
			allowed |= btrfs_raid_array[i].bg_flag;

	if (!validate_convert_profile(fs_info, &bctl->data, allowed, "data") ||
	    !validate_convert_profile(fs_info, &bctl->meta, allowed, "metadata") ||
	    !validate_convert_profile(fs_info, &bctl->sys,  allowed, "system")) {
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
			reducing_redundancy = true;
		else
			reducing_redundancy = false;

		/* if we're not converting, the target field is uninitialized */
		meta_target = (bctl->meta.flags & BTRFS_BALANCE_ARGS_CONVERT) ?
			bctl->meta.target : fs_info->avail_metadata_alloc_bits;
		data_target = (bctl->data.flags & BTRFS_BALANCE_ARGS_CONVERT) ?
			bctl->data.target : fs_info->avail_data_alloc_bits;
	} while (read_seqretry(&fs_info->profiles_lock, seq));

	if (reducing_redundancy) {
		if (bctl->flags & BTRFS_BALANCE_FORCE) {
			btrfs_info(fs_info,
			   "balance: force reducing metadata redundancy");
		} else {
			btrfs_err(fs_info,
	"balance: reduces metadata redundancy, use --force if you want this");
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
	if (ret == -ECANCELED && atomic_read(&fs_info->balance_pause_req)) {
		btrfs_info(fs_info, "balance: paused");
		btrfs_exclop_balance(fs_info, BTRFS_EXCLOP_BALANCE_PAUSED);
	}
	/*
	 * Balance can be canceled by:
	 *
	 * - Regular cancel request
	 *   Then ret == -ECANCELED and balance_cancel_req > 0
	 *
	 * - Fatal signal to "btrfs" process
	 *   Either the signal caught by wait_reserve_ticket() and callers
	 *   got -EINTR, or caught by btrfs_should_cancel_balance() and
	 *   got -ECANCELED.
	 *   Either way, in this case balance_cancel_req = 0, and
	 *   ret == -EINTR or ret == -ECANCELED.
	 *
	 * So here we only check the return value to catch canceled balance.
	 */
	else if (ret == -ECANCELED || ret == -EINTR)
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
		btrfs_exclop_finish(fs_info);
	}

	wake_up(&fs_info->balance_wait_q);

	return ret;
out:
	if (bctl->flags & BTRFS_BALANCE_RESUME)
		reset_balance_state(fs_info);
	else
		kfree(bctl);
	btrfs_exclop_finish(fs_info);

	return ret;
}

static int balance_kthread(void *data)
{
	struct btrfs_fs_info *fs_info = data;
	int ret = 0;

	sb_start_write(fs_info->sb);
	mutex_lock(&fs_info->balance_mutex);
	if (fs_info->balance_ctl)
		ret = btrfs_balance(fs_info, fs_info->balance_ctl, NULL);
	mutex_unlock(&fs_info->balance_mutex);
	sb_end_write(fs_info->sb);

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

	spin_lock(&fs_info->super_lock);
	ASSERT(fs_info->exclusive_operation == BTRFS_EXCLOP_BALANCE_PAUSED);
	fs_info->exclusive_operation = BTRFS_EXCLOP_BALANCE;
	spin_unlock(&fs_info->super_lock);
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
	if (!btrfs_exclop_start(fs_info, BTRFS_EXCLOP_BALANCE_PAUSED))
		btrfs_warn(fs_info,
	"balance: cannot set exclusive op status, resume manually");

	btrfs_release_path(path);

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
			btrfs_exclop_finish(fs_info);
			btrfs_info(fs_info, "balance: canceled");
		}
	}

	BUG_ON(fs_info->balance_ctl ||
		test_bit(BTRFS_FS_BALANCE_RUNNING, &fs_info->flags));
	atomic_dec(&fs_info->balance_cancel_req);
	mutex_unlock(&fs_info->balance_mutex);
	return 0;
}

int btrfs_uuid_scan_kthread(void *data)
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
	bool closing = false;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = 0;
	key.type = BTRFS_ROOT_ITEM_KEY;
	key.offset = 0;

	while (1) {
		if (btrfs_fs_closing(fs_info)) {
			closing = true;
			break;
		}
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
		item_size = btrfs_item_size(eb, slot);
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
		btrfs_release_path(path);
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
		btrfs_release_path(path);
		if (trans) {
			ret = btrfs_end_transaction(trans);
			trans = NULL;
			if (ret)
				break;
		}

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
	else if (!closing)
		set_bit(BTRFS_FS_UPDATE_UUID_TREE_GEN, &fs_info->flags);
	up(&fs_info->uuid_tree_rescan_sem);
	return 0;
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
		mutex_lock(&fs_info->reclaim_bgs_lock);
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0) {
			mutex_unlock(&fs_info->reclaim_bgs_lock);
			goto done;
		}

		ret = btrfs_previous_item(root, path, 0, key.type);
		if (ret) {
			mutex_unlock(&fs_info->reclaim_bgs_lock);
			if (ret < 0)
				goto done;
			ret = 0;
			btrfs_release_path(path);
			break;
		}

		l = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(l, &key, path->slots[0]);

		if (key.objectid != device->devid) {
			mutex_unlock(&fs_info->reclaim_bgs_lock);
			btrfs_release_path(path);
			break;
		}

		dev_extent = btrfs_item_ptr(l, slot, struct btrfs_dev_extent);
		length = btrfs_dev_extent_length(l, dev_extent);

		if (key.offset + length <= new_size) {
			mutex_unlock(&fs_info->reclaim_bgs_lock);
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
			mutex_unlock(&fs_info->reclaim_bgs_lock);
			goto done;
		}

		ret = btrfs_relocate_chunk(fs_info, chunk_offset);
		mutex_unlock(&fs_info->reclaim_bgs_lock);
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
	/* Clear all state bits beyond the shrunk device size */
	clear_extent_bits(&device->alloc_state, new_size, (u64)-1,
			  CHUNK_STATE_MASK);

	btrfs_device_set_disk_total_bytes(device, new_size);
	if (list_empty(&device->post_commit_list))
		list_add_tail(&device->post_commit_list,
			      &trans->transaction->dev_update_list);

	WARN_ON(diff > old_total);
	btrfs_set_super_total_bytes(super_copy,
			round_down(old_total - diff, fs_info->sectorsize));
	mutex_unlock(&fs_info->chunk_mutex);

	btrfs_reserve_chunk_metadata(trans, false);
	/* Now btrfs_update_device() will change the on-disk size. */
	ret = btrfs_update_device(trans, device);
	btrfs_trans_release_chunk_metadata(trans);
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

	lockdep_assert_held(&fs_info->chunk_mutex);

	array_size = btrfs_super_sys_array_size(super_copy);
	if (array_size + item_size + sizeof(disk_key)
			> BTRFS_SYSTEM_CHUNK_ARRAY_SIZE)
		return -EFBIG;

	ptr = super_copy->sys_chunk_array + array_size;
	btrfs_cpu_key_to_disk(&disk_key, key);
	memcpy(ptr, &disk_key, sizeof(disk_key));
	ptr += sizeof(disk_key);
	memcpy(ptr, chunk, item_size);
	item_size += sizeof(disk_key);
	btrfs_set_super_sys_array_size(super_copy, array_size + item_size);

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

static void check_raid1c34_incompat_flag(struct btrfs_fs_info *info, u64 type)
{
	if (!(type & (BTRFS_BLOCK_GROUP_RAID1C3 | BTRFS_BLOCK_GROUP_RAID1C4)))
		return;

	btrfs_set_fs_incompat(info, RAID1C34);
}

/*
 * Structure used internally for btrfs_create_chunk() function.
 * Wraps needed parameters.
 */
struct alloc_chunk_ctl {
	u64 start;
	u64 type;
	/* Total number of stripes to allocate */
	int num_stripes;
	/* sub_stripes info for map */
	int sub_stripes;
	/* Stripes per device */
	int dev_stripes;
	/* Maximum number of devices to use */
	int devs_max;
	/* Minimum number of devices to use */
	int devs_min;
	/* ndevs has to be a multiple of this */
	int devs_increment;
	/* Number of copies */
	int ncopies;
	/* Number of stripes worth of bytes to store parity information */
	int nparity;
	u64 max_stripe_size;
	u64 max_chunk_size;
	u64 dev_extent_min;
	u64 stripe_size;
	u64 chunk_size;
	int ndevs;
};

static void init_alloc_chunk_ctl_policy_regular(
				struct btrfs_fs_devices *fs_devices,
				struct alloc_chunk_ctl *ctl)
{
	struct btrfs_space_info *space_info;

	space_info = btrfs_find_space_info(fs_devices->fs_info, ctl->type);
	ASSERT(space_info);

	ctl->max_chunk_size = READ_ONCE(space_info->chunk_size);
	ctl->max_stripe_size = ctl->max_chunk_size;

	if (ctl->type & BTRFS_BLOCK_GROUP_SYSTEM)
		ctl->devs_max = min_t(int, ctl->devs_max, BTRFS_MAX_DEVS_SYS_CHUNK);

	/* We don't want a chunk larger than 10% of writable space */
	ctl->max_chunk_size = min(div_factor(fs_devices->total_rw_bytes, 1),
				  ctl->max_chunk_size);
	ctl->dev_extent_min = BTRFS_STRIPE_LEN * ctl->dev_stripes;
}

static void init_alloc_chunk_ctl_policy_zoned(
				      struct btrfs_fs_devices *fs_devices,
				      struct alloc_chunk_ctl *ctl)
{
	u64 zone_size = fs_devices->fs_info->zone_size;
	u64 limit;
	int min_num_stripes = ctl->devs_min * ctl->dev_stripes;
	int min_data_stripes = (min_num_stripes - ctl->nparity) / ctl->ncopies;
	u64 min_chunk_size = min_data_stripes * zone_size;
	u64 type = ctl->type;

	ctl->max_stripe_size = zone_size;
	if (type & BTRFS_BLOCK_GROUP_DATA) {
		ctl->max_chunk_size = round_down(BTRFS_MAX_DATA_CHUNK_SIZE,
						 zone_size);
	} else if (type & BTRFS_BLOCK_GROUP_METADATA) {
		ctl->max_chunk_size = ctl->max_stripe_size;
	} else if (type & BTRFS_BLOCK_GROUP_SYSTEM) {
		ctl->max_chunk_size = 2 * ctl->max_stripe_size;
		ctl->devs_max = min_t(int, ctl->devs_max,
				      BTRFS_MAX_DEVS_SYS_CHUNK);
	} else {
		BUG();
	}

	/* We don't want a chunk larger than 10% of writable space */
	limit = max(round_down(div_factor(fs_devices->total_rw_bytes, 1),
			       zone_size),
		    min_chunk_size);
	ctl->max_chunk_size = min(limit, ctl->max_chunk_size);
	ctl->dev_extent_min = zone_size * ctl->dev_stripes;
}

static void init_alloc_chunk_ctl(struct btrfs_fs_devices *fs_devices,
				 struct alloc_chunk_ctl *ctl)
{
	int index = btrfs_bg_flags_to_raid_index(ctl->type);

	ctl->sub_stripes = btrfs_raid_array[index].sub_stripes;
	ctl->dev_stripes = btrfs_raid_array[index].dev_stripes;
	ctl->devs_max = btrfs_raid_array[index].devs_max;
	if (!ctl->devs_max)
		ctl->devs_max = BTRFS_MAX_DEVS(fs_devices->fs_info);
	ctl->devs_min = btrfs_raid_array[index].devs_min;
	ctl->devs_increment = btrfs_raid_array[index].devs_increment;
	ctl->ncopies = btrfs_raid_array[index].ncopies;
	ctl->nparity = btrfs_raid_array[index].nparity;
	ctl->ndevs = 0;

	switch (fs_devices->chunk_alloc_policy) {
	case BTRFS_CHUNK_ALLOC_REGULAR:
		init_alloc_chunk_ctl_policy_regular(fs_devices, ctl);
		break;
	case BTRFS_CHUNK_ALLOC_ZONED:
		init_alloc_chunk_ctl_policy_zoned(fs_devices, ctl);
		break;
	default:
		BUG();
	}
}

static int gather_device_info(struct btrfs_fs_devices *fs_devices,
			      struct alloc_chunk_ctl *ctl,
			      struct btrfs_device_info *devices_info)
{
	struct btrfs_fs_info *info = fs_devices->fs_info;
	struct btrfs_device *device;
	u64 total_avail;
	u64 dev_extent_want = ctl->max_stripe_size * ctl->dev_stripes;
	int ret;
	int ndevs = 0;
	u64 max_avail;
	u64 dev_offset;

	/*
	 * in the first pass through the devices list, we gather information
	 * about the available holes on each device.
	 */
	list_for_each_entry(device, &fs_devices->alloc_list, dev_alloc_list) {
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
		if (total_avail < ctl->dev_extent_min)
			continue;

		ret = find_free_dev_extent(device, dev_extent_want, &dev_offset,
					   &max_avail);
		if (ret && ret != -ENOSPC)
			return ret;

		if (ret == 0)
			max_avail = dev_extent_want;

		if (max_avail < ctl->dev_extent_min) {
			if (btrfs_test_opt(info, ENOSPC_DEBUG))
				btrfs_debug(info,
			"%s: devid %llu has no free space, have=%llu want=%llu",
					    __func__, device->devid, max_avail,
					    ctl->dev_extent_min);
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
	ctl->ndevs = ndevs;

	/*
	 * now sort the devices by hole size / available space
	 */
	sort(devices_info, ndevs, sizeof(struct btrfs_device_info),
	     btrfs_cmp_device_info, NULL);

	return 0;
}

static int decide_stripe_size_regular(struct alloc_chunk_ctl *ctl,
				      struct btrfs_device_info *devices_info)
{
	/* Number of stripes that count for block group size */
	int data_stripes;

	/*
	 * The primary goal is to maximize the number of stripes, so use as
	 * many devices as possible, even if the stripes are not maximum sized.
	 *
	 * The DUP profile stores more than one stripe per device, the
	 * max_avail is the total size so we have to adjust.
	 */
	ctl->stripe_size = div_u64(devices_info[ctl->ndevs - 1].max_avail,
				   ctl->dev_stripes);
	ctl->num_stripes = ctl->ndevs * ctl->dev_stripes;

	/* This will have to be fixed for RAID1 and RAID10 over more drives */
	data_stripes = (ctl->num_stripes - ctl->nparity) / ctl->ncopies;

	/*
	 * Use the number of data stripes to figure out how big this chunk is
	 * really going to be in terms of logical address space, and compare
	 * that answer with the max chunk size. If it's higher, we try to
	 * reduce stripe_size.
	 */
	if (ctl->stripe_size * data_stripes > ctl->max_chunk_size) {
		/*
		 * Reduce stripe_size, round it up to a 16MB boundary again and
		 * then use it, unless it ends up being even bigger than the
		 * previous value we had already.
		 */
		ctl->stripe_size = min(round_up(div_u64(ctl->max_chunk_size,
							data_stripes), SZ_16M),
				       ctl->stripe_size);
	}

	/* Stripe size should not go beyond 1G. */
	ctl->stripe_size = min_t(u64, ctl->stripe_size, SZ_1G);

	/* Align to BTRFS_STRIPE_LEN */
	ctl->stripe_size = round_down(ctl->stripe_size, BTRFS_STRIPE_LEN);
	ctl->chunk_size = ctl->stripe_size * data_stripes;

	return 0;
}

static int decide_stripe_size_zoned(struct alloc_chunk_ctl *ctl,
				    struct btrfs_device_info *devices_info)
{
	u64 zone_size = devices_info[0].dev->zone_info->zone_size;
	/* Number of stripes that count for block group size */
	int data_stripes;

	/*
	 * It should hold because:
	 *    dev_extent_min == dev_extent_want == zone_size * dev_stripes
	 */
	ASSERT(devices_info[ctl->ndevs - 1].max_avail == ctl->dev_extent_min);

	ctl->stripe_size = zone_size;
	ctl->num_stripes = ctl->ndevs * ctl->dev_stripes;
	data_stripes = (ctl->num_stripes - ctl->nparity) / ctl->ncopies;

	/* stripe_size is fixed in zoned filesysmte. Reduce ndevs instead. */
	if (ctl->stripe_size * data_stripes > ctl->max_chunk_size) {
		ctl->ndevs = div_u64(div_u64(ctl->max_chunk_size * ctl->ncopies,
					     ctl->stripe_size) + ctl->nparity,
				     ctl->dev_stripes);
		ctl->num_stripes = ctl->ndevs * ctl->dev_stripes;
		data_stripes = (ctl->num_stripes - ctl->nparity) / ctl->ncopies;
		ASSERT(ctl->stripe_size * data_stripes <= ctl->max_chunk_size);
	}

	ctl->chunk_size = ctl->stripe_size * data_stripes;

	return 0;
}

static int decide_stripe_size(struct btrfs_fs_devices *fs_devices,
			      struct alloc_chunk_ctl *ctl,
			      struct btrfs_device_info *devices_info)
{
	struct btrfs_fs_info *info = fs_devices->fs_info;

	/*
	 * Round down to number of usable stripes, devs_increment can be any
	 * number so we can't use round_down() that requires power of 2, while
	 * rounddown is safe.
	 */
	ctl->ndevs = rounddown(ctl->ndevs, ctl->devs_increment);

	if (ctl->ndevs < ctl->devs_min) {
		if (btrfs_test_opt(info, ENOSPC_DEBUG)) {
			btrfs_debug(info,
	"%s: not enough devices with free space: have=%d minimum required=%d",
				    __func__, ctl->ndevs, ctl->devs_min);
		}
		return -ENOSPC;
	}

	ctl->ndevs = min(ctl->ndevs, ctl->devs_max);

	switch (fs_devices->chunk_alloc_policy) {
	case BTRFS_CHUNK_ALLOC_REGULAR:
		return decide_stripe_size_regular(ctl, devices_info);
	case BTRFS_CHUNK_ALLOC_ZONED:
		return decide_stripe_size_zoned(ctl, devices_info);
	default:
		BUG();
	}
}

static struct btrfs_block_group *create_chunk(struct btrfs_trans_handle *trans,
			struct alloc_chunk_ctl *ctl,
			struct btrfs_device_info *devices_info)
{
	struct btrfs_fs_info *info = trans->fs_info;
	struct map_lookup *map = NULL;
	struct extent_map_tree *em_tree;
	struct btrfs_block_group *block_group;
	struct extent_map *em;
	u64 start = ctl->start;
	u64 type = ctl->type;
	int ret;
	int i;
	int j;

	map = kmalloc(map_lookup_size(ctl->num_stripes), GFP_NOFS);
	if (!map)
		return ERR_PTR(-ENOMEM);
	map->num_stripes = ctl->num_stripes;

	for (i = 0; i < ctl->ndevs; ++i) {
		for (j = 0; j < ctl->dev_stripes; ++j) {
			int s = i * ctl->dev_stripes + j;
			map->stripes[s].dev = devices_info[i].dev;
			map->stripes[s].physical = devices_info[i].dev_offset +
						   j * ctl->stripe_size;
		}
	}
	map->stripe_len = BTRFS_STRIPE_LEN;
	map->io_align = BTRFS_STRIPE_LEN;
	map->io_width = BTRFS_STRIPE_LEN;
	map->type = type;
	map->sub_stripes = ctl->sub_stripes;

	trace_btrfs_chunk_alloc(info, map, start, ctl->chunk_size);

	em = alloc_extent_map();
	if (!em) {
		kfree(map);
		return ERR_PTR(-ENOMEM);
	}
	set_bit(EXTENT_FLAG_FS_MAPPING, &em->flags);
	em->map_lookup = map;
	em->start = start;
	em->len = ctl->chunk_size;
	em->block_start = 0;
	em->block_len = em->len;
	em->orig_block_len = ctl->stripe_size;

	em_tree = &info->mapping_tree;
	write_lock(&em_tree->lock);
	ret = add_extent_mapping(em_tree, em, 0);
	if (ret) {
		write_unlock(&em_tree->lock);
		free_extent_map(em);
		return ERR_PTR(ret);
	}
	write_unlock(&em_tree->lock);

	block_group = btrfs_make_block_group(trans, 0, type, start, ctl->chunk_size);
	if (IS_ERR(block_group))
		goto error_del_extent;

	for (i = 0; i < map->num_stripes; i++) {
		struct btrfs_device *dev = map->stripes[i].dev;

		btrfs_device_set_bytes_used(dev,
					    dev->bytes_used + ctl->stripe_size);
		if (list_empty(&dev->post_commit_list))
			list_add_tail(&dev->post_commit_list,
				      &trans->transaction->dev_update_list);
	}

	atomic64_sub(ctl->stripe_size * map->num_stripes,
		     &info->free_chunk_space);

	free_extent_map(em);
	check_raid56_incompat_flag(info, type);
	check_raid1c34_incompat_flag(info, type);

	return block_group;

error_del_extent:
	write_lock(&em_tree->lock);
	remove_extent_mapping(em_tree, em);
	write_unlock(&em_tree->lock);

	/* One for our allocation */
	free_extent_map(em);
	/* One for the tree reference */
	free_extent_map(em);

	return block_group;
}

struct btrfs_block_group *btrfs_create_chunk(struct btrfs_trans_handle *trans,
					    u64 type)
{
	struct btrfs_fs_info *info = trans->fs_info;
	struct btrfs_fs_devices *fs_devices = info->fs_devices;
	struct btrfs_device_info *devices_info = NULL;
	struct alloc_chunk_ctl ctl;
	struct btrfs_block_group *block_group;
	int ret;

	lockdep_assert_held(&info->chunk_mutex);

	if (!alloc_profile_is_valid(type, 0)) {
		ASSERT(0);
		return ERR_PTR(-EINVAL);
	}

	if (list_empty(&fs_devices->alloc_list)) {
		if (btrfs_test_opt(info, ENOSPC_DEBUG))
			btrfs_debug(info, "%s: no writable device", __func__);
		return ERR_PTR(-ENOSPC);
	}

	if (!(type & BTRFS_BLOCK_GROUP_TYPE_MASK)) {
		btrfs_err(info, "invalid chunk type 0x%llx requested", type);
		ASSERT(0);
		return ERR_PTR(-EINVAL);
	}

	ctl.start = find_next_chunk(info);
	ctl.type = type;
	init_alloc_chunk_ctl(fs_devices, &ctl);

	devices_info = kcalloc(fs_devices->rw_devices, sizeof(*devices_info),
			       GFP_NOFS);
	if (!devices_info)
		return ERR_PTR(-ENOMEM);

	ret = gather_device_info(fs_devices, &ctl, devices_info);
	if (ret < 0) {
		block_group = ERR_PTR(ret);
		goto out;
	}

	ret = decide_stripe_size(fs_devices, &ctl, devices_info);
	if (ret < 0) {
		block_group = ERR_PTR(ret);
		goto out;
	}

	block_group = create_chunk(trans, &ctl, devices_info);

out:
	kfree(devices_info);
	return block_group;
}

/*
 * This function, btrfs_chunk_alloc_add_chunk_item(), typically belongs to the
 * phase 1 of chunk allocation. It belongs to phase 2 only when allocating system
 * chunks.
 *
 * See the comment at btrfs_chunk_alloc() for details about the chunk allocation
 * phases.
 */
int btrfs_chunk_alloc_add_chunk_item(struct btrfs_trans_handle *trans,
				     struct btrfs_block_group *bg)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *chunk_root = fs_info->chunk_root;
	struct btrfs_key key;
	struct btrfs_chunk *chunk;
	struct btrfs_stripe *stripe;
	struct extent_map *em;
	struct map_lookup *map;
	size_t item_size;
	int i;
	int ret;

	/*
	 * We take the chunk_mutex for 2 reasons:
	 *
	 * 1) Updates and insertions in the chunk btree must be done while holding
	 *    the chunk_mutex, as well as updating the system chunk array in the
	 *    superblock. See the comment on top of btrfs_chunk_alloc() for the
	 *    details;
	 *
	 * 2) To prevent races with the final phase of a device replace operation
	 *    that replaces the device object associated with the map's stripes,
	 *    because the device object's id can change at any time during that
	 *    final phase of the device replace operation
	 *    (dev-replace.c:btrfs_dev_replace_finishing()), so we could grab the
	 *    replaced device and then see it with an ID of BTRFS_DEV_REPLACE_DEVID,
	 *    which would cause a failure when updating the device item, which does
	 *    not exists, or persisting a stripe of the chunk item with such ID.
	 *    Here we can't use the device_list_mutex because our caller already
	 *    has locked the chunk_mutex, and the final phase of device replace
	 *    acquires both mutexes - first the device_list_mutex and then the
	 *    chunk_mutex. Using any of those two mutexes protects us from a
	 *    concurrent device replace.
	 */
	lockdep_assert_held(&fs_info->chunk_mutex);

	em = btrfs_get_chunk_map(fs_info, bg->start, bg->length);
	if (IS_ERR(em)) {
		ret = PTR_ERR(em);
		btrfs_abort_transaction(trans, ret);
		return ret;
	}

	map = em->map_lookup;
	item_size = btrfs_chunk_item_size(map->num_stripes);

	chunk = kzalloc(item_size, GFP_NOFS);
	if (!chunk) {
		ret = -ENOMEM;
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	for (i = 0; i < map->num_stripes; i++) {
		struct btrfs_device *device = map->stripes[i].dev;

		ret = btrfs_update_device(trans, device);
		if (ret)
			goto out;
	}

	stripe = &chunk->stripe;
	for (i = 0; i < map->num_stripes; i++) {
		struct btrfs_device *device = map->stripes[i].dev;
		const u64 dev_offset = map->stripes[i].physical;

		btrfs_set_stack_stripe_devid(stripe, device->devid);
		btrfs_set_stack_stripe_offset(stripe, dev_offset);
		memcpy(stripe->dev_uuid, device->uuid, BTRFS_UUID_SIZE);
		stripe++;
	}

	btrfs_set_stack_chunk_length(chunk, bg->length);
	btrfs_set_stack_chunk_owner(chunk, BTRFS_EXTENT_TREE_OBJECTID);
	btrfs_set_stack_chunk_stripe_len(chunk, map->stripe_len);
	btrfs_set_stack_chunk_type(chunk, map->type);
	btrfs_set_stack_chunk_num_stripes(chunk, map->num_stripes);
	btrfs_set_stack_chunk_io_align(chunk, map->stripe_len);
	btrfs_set_stack_chunk_io_width(chunk, map->stripe_len);
	btrfs_set_stack_chunk_sector_size(chunk, fs_info->sectorsize);
	btrfs_set_stack_chunk_sub_stripes(chunk, map->sub_stripes);

	key.objectid = BTRFS_FIRST_CHUNK_TREE_OBJECTID;
	key.type = BTRFS_CHUNK_ITEM_KEY;
	key.offset = bg->start;

	ret = btrfs_insert_item(trans, chunk_root, &key, chunk, item_size);
	if (ret)
		goto out;

	set_bit(BLOCK_GROUP_FLAG_CHUNK_ITEM_INSERTED, &bg->runtime_flags);

	if (map->type & BTRFS_BLOCK_GROUP_SYSTEM) {
		ret = btrfs_add_system_chunk(fs_info, &key, chunk, item_size);
		if (ret)
			goto out;
	}

out:
	kfree(chunk);
	free_extent_map(em);
	return ret;
}

static noinline int init_first_rw_device(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	u64 alloc_profile;
	struct btrfs_block_group *meta_bg;
	struct btrfs_block_group *sys_bg;

	/*
	 * When adding a new device for sprouting, the seed device is read-only
	 * so we must first allocate a metadata and a system chunk. But before
	 * adding the block group items to the extent, device and chunk btrees,
	 * we must first:
	 *
	 * 1) Create both chunks without doing any changes to the btrees, as
	 *    otherwise we would get -ENOSPC since the block groups from the
	 *    seed device are read-only;
	 *
	 * 2) Add the device item for the new sprout device - finishing the setup
	 *    of a new block group requires updating the device item in the chunk
	 *    btree, so it must exist when we attempt to do it. The previous step
	 *    ensures this does not fail with -ENOSPC.
	 *
	 * After that we can add the block group items to their btrees:
	 * update existing device item in the chunk btree, add a new block group
	 * item to the extent btree, add a new chunk item to the chunk btree and
	 * finally add the new device extent items to the devices btree.
	 */

	alloc_profile = btrfs_metadata_alloc_profile(fs_info);
	meta_bg = btrfs_create_chunk(trans, alloc_profile);
	if (IS_ERR(meta_bg))
		return PTR_ERR(meta_bg);

	alloc_profile = btrfs_system_alloc_profile(fs_info);
	sys_bg = btrfs_create_chunk(trans, alloc_profile);
	if (IS_ERR(sys_bg))
		return PTR_ERR(sys_bg);

	return 0;
}

static inline int btrfs_chunk_max_errors(struct map_lookup *map)
{
	const int index = btrfs_bg_flags_to_raid_index(map->type);

	return btrfs_raid_array[index].tolerated_failures;
}

bool btrfs_chunk_writeable(struct btrfs_fs_info *fs_info, u64 chunk_offset)
{
	struct extent_map *em;
	struct map_lookup *map;
	int miss_ndevs = 0;
	int i;
	bool ret = true;

	em = btrfs_get_chunk_map(fs_info, chunk_offset, 1);
	if (IS_ERR(em))
		return false;

	map = em->map_lookup;
	for (i = 0; i < map->num_stripes; i++) {
		if (test_bit(BTRFS_DEV_STATE_MISSING,
					&map->stripes[i].dev->dev_state)) {
			miss_ndevs++;
			continue;
		}
		if (!test_bit(BTRFS_DEV_STATE_WRITEABLE,
					&map->stripes[i].dev->dev_state)) {
			ret = false;
			goto end;
		}
	}

	/*
	 * If the number of missing devices is larger than max errors, we can
	 * not write the data into that chunk successfully.
	 */
	if (miss_ndevs > btrfs_chunk_max_errors(map))
		ret = false;
end:
	free_extent_map(em);
	return ret;
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
	enum btrfs_raid_types index;
	int ret = 1;

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
	index = btrfs_bg_flags_to_raid_index(map->type);

	/* Non-RAID56, use their ncopies from btrfs_raid_array. */
	if (!(map->type & BTRFS_BLOCK_GROUP_RAID56_MASK))
		ret = btrfs_raid_array[index].ncopies;
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

	if (!btrfs_fs_incompat(fs_info, RAID56))
		return len;

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

	if (!btrfs_fs_incompat(fs_info, RAID56))
		return 0;

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

	switch (fs_info->fs_devices->read_policy) {
	default:
		/* Shouldn't happen, just warn and use pid instead of failing */
		btrfs_warn_rl(fs_info,
			      "unknown read_policy type %u, reset to pid",
			      fs_info->fs_devices->read_policy);
		fs_info->fs_devices->read_policy = BTRFS_READ_POLICY_PID;
		fallthrough;
	case BTRFS_READ_POLICY_PID:
		preferred_mirror = first + (current->pid % num_stripes);
		break;
	}

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

/* Bubble-sort the stripe set to put the parity/syndrome stripes last */
static void sort_parity_stripes(struct btrfs_io_context *bioc, int num_stripes)
{
	int i;
	int again = 1;

	while (again) {
		again = 0;
		for (i = 0; i < num_stripes - 1; i++) {
			/* Swap if parity is on a smaller index */
			if (bioc->raid_map[i] > bioc->raid_map[i + 1]) {
				swap(bioc->stripes[i], bioc->stripes[i + 1]);
				swap(bioc->raid_map[i], bioc->raid_map[i + 1]);
				again = 1;
			}
		}
	}
}

static struct btrfs_io_context *alloc_btrfs_io_context(struct btrfs_fs_info *fs_info,
						       int total_stripes,
						       int real_stripes)
{
	struct btrfs_io_context *bioc = kzalloc(
		 /* The size of btrfs_io_context */
		sizeof(struct btrfs_io_context) +
		/* Plus the variable array for the stripes */
		sizeof(struct btrfs_io_stripe) * (total_stripes) +
		/* Plus the variable array for the tgt dev */
		sizeof(int) * (real_stripes) +
		/*
		 * Plus the raid_map, which includes both the tgt dev
		 * and the stripes.
		 */
		sizeof(u64) * (total_stripes),
		GFP_NOFS|__GFP_NOFAIL);

	refcount_set(&bioc->refs, 1);

	bioc->fs_info = fs_info;
	bioc->tgtdev_map = (int *)(bioc->stripes + total_stripes);
	bioc->raid_map = (u64 *)(bioc->tgtdev_map + real_stripes);

	return bioc;
}

void btrfs_get_bioc(struct btrfs_io_context *bioc)
{
	WARN_ON(!refcount_read(&bioc->refs));
	refcount_inc(&bioc->refs);
}

void btrfs_put_bioc(struct btrfs_io_context *bioc)
{
	if (!bioc)
		return;
	if (refcount_dec_and_test(&bioc->refs))
		kfree(bioc);
}

/*
 * Please note that, discard won't be sent to target device of device
 * replace.
 */
struct btrfs_discard_stripe *btrfs_map_discard(struct btrfs_fs_info *fs_info,
					       u64 logical, u64 *length_ret,
					       u32 *num_stripes)
{
	struct extent_map *em;
	struct map_lookup *map;
	struct btrfs_discard_stripe *stripes;
	u64 length = *length_ret;
	u64 offset;
	u64 stripe_nr;
	u64 stripe_nr_end;
	u64 stripe_end_offset;
	u64 stripe_cnt;
	u64 stripe_len;
	u64 stripe_offset;
	u32 stripe_index;
	u32 factor = 0;
	u32 sub_stripes = 0;
	u64 stripes_per_dev = 0;
	u32 remaining_stripes = 0;
	u32 last_stripe = 0;
	int ret;
	int i;

	em = btrfs_get_chunk_map(fs_info, logical, length);
	if (IS_ERR(em))
		return ERR_CAST(em);

	map = em->map_lookup;

	/* we don't discard raid56 yet */
	if (map->type & BTRFS_BLOCK_GROUP_RAID56_MASK) {
		ret = -EOPNOTSUPP;
		goto out_free_map;
}

	offset = logical - em->start;
	length = min_t(u64, em->start + em->len - logical, length);
	*length_ret = length;

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
	*num_stripes = 1;
	stripe_index = 0;
	if (map->type & (BTRFS_BLOCK_GROUP_RAID0 |
			 BTRFS_BLOCK_GROUP_RAID10)) {
		if (map->type & BTRFS_BLOCK_GROUP_RAID0)
			sub_stripes = 1;
		else
			sub_stripes = map->sub_stripes;

		factor = map->num_stripes / sub_stripes;
		*num_stripes = min_t(u64, map->num_stripes,
				    sub_stripes * stripe_cnt);
		stripe_nr = div_u64_rem(stripe_nr, factor, &stripe_index);
		stripe_index *= sub_stripes;
		stripes_per_dev = div_u64_rem(stripe_cnt, factor,
					      &remaining_stripes);
		div_u64_rem(stripe_nr_end - 1, factor, &last_stripe);
		last_stripe *= sub_stripes;
	} else if (map->type & (BTRFS_BLOCK_GROUP_RAID1_MASK |
				BTRFS_BLOCK_GROUP_DUP)) {
		*num_stripes = map->num_stripes;
	} else {
		stripe_nr = div_u64_rem(stripe_nr, map->num_stripes,
					&stripe_index);
	}

	stripes = kcalloc(*num_stripes, sizeof(*stripes), GFP_NOFS);
	if (!stripes) {
		ret = -ENOMEM;
		goto out_free_map;
	}

	for (i = 0; i < *num_stripes; i++) {
		stripes[i].physical =
			map->stripes[stripe_index].physical +
			stripe_offset + stripe_nr * map->stripe_len;
		stripes[i].dev = map->stripes[stripe_index].dev;

		if (map->type & (BTRFS_BLOCK_GROUP_RAID0 |
				 BTRFS_BLOCK_GROUP_RAID10)) {
			stripes[i].length = stripes_per_dev * map->stripe_len;

			if (i / sub_stripes < remaining_stripes)
				stripes[i].length += map->stripe_len;

			/*
			 * Special for the first stripe and
			 * the last stripe:
			 *
			 * |-------|...|-------|
			 *     |----------|
			 *    off     end_off
			 */
			if (i < sub_stripes)
				stripes[i].length -= stripe_offset;

			if (stripe_index >= last_stripe &&
			    stripe_index <= (last_stripe +
					     sub_stripes - 1))
				stripes[i].length -= stripe_end_offset;

			if (i == sub_stripes - 1)
				stripe_offset = 0;
		} else {
			stripes[i].length = length;
		}

		stripe_index++;
		if (stripe_index == map->num_stripes) {
			stripe_index = 0;
			stripe_nr++;
		}
	}

	free_extent_map(em);
	return stripes;
out_free_map:
	free_extent_map(em);
	return ERR_PTR(ret);
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
	struct btrfs_io_context *bioc = NULL;
	int num_stripes;
	int index_srcdev = 0;
	int found = 0;
	u64 physical_of_found = 0;
	int i;
	int ret = 0;

	ret = __btrfs_map_block(fs_info, BTRFS_MAP_GET_READ_MIRRORS,
				logical, &length, &bioc, NULL, NULL, 0);
	if (ret) {
		ASSERT(bioc == NULL);
		return ret;
	}

	num_stripes = bioc->num_stripes;
	if (*mirror_num > num_stripes) {
		/*
		 * BTRFS_MAP_GET_READ_MIRRORS does not contain this mirror,
		 * that means that the requested area is not left of the left
		 * cursor
		 */
		btrfs_put_bioc(bioc);
		return -EIO;
	}

	/*
	 * process the rest of the function using the mirror_num of the source
	 * drive. Therefore look it up first.  At the end, patch the device
	 * pointer to the one of the target drive.
	 */
	for (i = 0; i < num_stripes; i++) {
		if (bioc->stripes[i].dev->devid != srcdev_devid)
			continue;

		/*
		 * In case of DUP, in order to keep it simple, only add the
		 * mirror with the lowest physical address
		 */
		if (found &&
		    physical_of_found <= bioc->stripes[i].physical)
			continue;

		index_srcdev = i;
		found = 1;
		physical_of_found = bioc->stripes[i].physical;
	}

	btrfs_put_bioc(bioc);

	ASSERT(found);
	if (!found)
		return -EIO;

	*mirror_num = index_srcdev + 1;
	*physical = physical_of_found;
	return ret;
}

static bool is_block_group_to_copy(struct btrfs_fs_info *fs_info, u64 logical)
{
	struct btrfs_block_group *cache;
	bool ret;

	/* Non zoned filesystem does not use "to_copy" flag */
	if (!btrfs_is_zoned(fs_info))
		return false;

	cache = btrfs_lookup_block_group(fs_info, logical);

	ret = test_bit(BLOCK_GROUP_FLAG_TO_COPY, &cache->runtime_flags);

	btrfs_put_block_group(cache);
	return ret;
}

static void handle_ops_on_dev_replace(enum btrfs_map_op op,
				      struct btrfs_io_context **bioc_ret,
				      struct btrfs_dev_replace *dev_replace,
				      u64 logical,
				      int *num_stripes_ret, int *max_errors_ret)
{
	struct btrfs_io_context *bioc = *bioc_ret;
	u64 srcdev_devid = dev_replace->srcdev->devid;
	int tgtdev_indexes = 0;
	int num_stripes = *num_stripes_ret;
	int max_errors = *max_errors_ret;
	int i;

	if (op == BTRFS_MAP_WRITE) {
		int index_where_to_add;

		/*
		 * A block group which have "to_copy" set will eventually
		 * copied by dev-replace process. We can avoid cloning IO here.
		 */
		if (is_block_group_to_copy(dev_replace->srcdev->fs_info, logical))
			return;

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
			if (bioc->stripes[i].dev->devid == srcdev_devid) {
				/* write to new disk, too */
				struct btrfs_io_stripe *new =
					bioc->stripes + index_where_to_add;
				struct btrfs_io_stripe *old =
					bioc->stripes + i;

				new->physical = old->physical;
				new->dev = dev_replace->tgtdev;
				bioc->tgtdev_map[i] = index_where_to_add;
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
			if (bioc->stripes[i].dev->devid == srcdev_devid) {
				/*
				 * In case of DUP, in order to keep it simple,
				 * only add the mirror with the lowest physical
				 * address
				 */
				if (found &&
				    physical_of_found <= bioc->stripes[i].physical)
					continue;
				index_srcdev = i;
				found = 1;
				physical_of_found = bioc->stripes[i].physical;
			}
		}
		if (found) {
			struct btrfs_io_stripe *tgtdev_stripe =
				bioc->stripes + num_stripes;

			tgtdev_stripe->physical = physical_of_found;
			tgtdev_stripe->dev = dev_replace->tgtdev;
			bioc->tgtdev_map[index_srcdev] = num_stripes;

			tgtdev_indexes++;
			num_stripes++;
		}
	}

	*num_stripes_ret = num_stripes;
	*max_errors_ret = max_errors;
	bioc->num_tgtdevs = tgtdev_indexes;
	*bioc_ret = bioc;
}

static bool need_full_stripe(enum btrfs_map_op op)
{
	return (op == BTRFS_MAP_WRITE || op == BTRFS_MAP_GET_READ_MIRRORS);
}

/*
 * Calculate the geometry of a particular (address, len) tuple. This
 * information is used to calculate how big a particular bio can get before it
 * straddles a stripe.
 *
 * @fs_info: the filesystem
 * @em:      mapping containing the logical extent
 * @op:      type of operation - write or read
 * @logical: address that we want to figure out the geometry of
 * @io_geom: pointer used to return values
 *
 * Returns < 0 in case a chunk for the given logical address cannot be found,
 * usually shouldn't happen unless @logical is corrupted, 0 otherwise.
 */
int btrfs_get_io_geometry(struct btrfs_fs_info *fs_info, struct extent_map *em,
			  enum btrfs_map_op op, u64 logical,
			  struct btrfs_io_geometry *io_geom)
{
	struct map_lookup *map;
	u64 len;
	u64 offset;
	u64 stripe_offset;
	u64 stripe_nr;
	u32 stripe_len;
	u64 raid56_full_stripe_start = (u64)-1;
	int data_stripes;

	ASSERT(op != BTRFS_MAP_DISCARD);

	map = em->map_lookup;
	/* Offset of this logical address in the chunk */
	offset = logical - em->start;
	/* Len of a stripe in a chunk */
	stripe_len = map->stripe_len;
	/*
	 * Stripe_nr is where this block falls in
	 * stripe_offset is the offset of this block in its stripe.
	 */
	stripe_nr = div64_u64_rem(offset, stripe_len, &stripe_offset);
	ASSERT(stripe_offset < U32_MAX);

	data_stripes = nr_data_stripes(map);

	/* Only stripe based profiles needs to check against stripe length. */
	if (map->type & BTRFS_BLOCK_GROUP_STRIPE_MASK) {
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

	return 0;
}

static void set_io_stripe(struct btrfs_io_stripe *dst, const struct map_lookup *map,
		          u32 stripe_index, u64 stripe_offset, u64 stripe_nr)
{
	dst->dev = map->stripes[stripe_index].dev;
	dst->physical = map->stripes[stripe_index].physical +
			stripe_offset + stripe_nr * map->stripe_len;
}

static int __btrfs_map_block(struct btrfs_fs_info *fs_info,
			     enum btrfs_map_op op, u64 logical, u64 *length,
			     struct btrfs_io_context **bioc_ret,
			     struct btrfs_io_stripe *smap,
			     int *mirror_num_ret, int need_raid_map)
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
	int mirror_num = (mirror_num_ret ? *mirror_num_ret : 0);
	int num_stripes;
	int max_errors = 0;
	int tgtdev_indexes = 0;
	struct btrfs_io_context *bioc = NULL;
	struct btrfs_dev_replace *dev_replace = &fs_info->dev_replace;
	int dev_replace_is_ongoing = 0;
	int num_alloc_stripes;
	int patch_the_first_stripe_for_dev_replace = 0;
	u64 physical_to_patch_in_first_stripe = 0;
	u64 raid56_full_stripe_start = (u64)-1;
	struct btrfs_io_geometry geom;

	ASSERT(bioc_ret);
	ASSERT(op != BTRFS_MAP_DISCARD);

	em = btrfs_get_chunk_map(fs_info, logical, *length);
	ASSERT(!IS_ERR(em));

	ret = btrfs_get_io_geometry(fs_info, em, op, logical, &geom);
	if (ret < 0)
		return ret;

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
		ASSERT(map->stripe_len == BTRFS_STRIPE_LEN);
		if (need_raid_map && (need_full_stripe(op) || mirror_num > 1)) {
			/* push stripe_nr back to the start of the full stripe */
			stripe_nr = div64_u64(raid56_full_stripe_start,
					stripe_len * data_stripes);

			/* RAID[56] write or recovery. Return all stripes */
			num_stripes = map->num_stripes;
			max_errors = btrfs_chunk_max_errors(map);

			/* Return the length to the full stripe end */
			*length = min(logical + *length,
				      raid56_full_stripe_start + em->start +
				      data_stripes * stripe_len) - logical;
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

	/*
	 * If this I/O maps to a single device, try to return the device and
	 * physical block information on the stack instead of allocating an
	 * I/O context structure.
	 */
	if (smap && num_alloc_stripes == 1 &&
	    !((map->type & BTRFS_BLOCK_GROUP_RAID56_MASK) && mirror_num > 1) &&
	    (!need_full_stripe(op) || !dev_replace_is_ongoing ||
	     !dev_replace->tgtdev)) {
		if (patch_the_first_stripe_for_dev_replace) {
			smap->dev = dev_replace->tgtdev;
			smap->physical = physical_to_patch_in_first_stripe;
			*mirror_num_ret = map->num_stripes + 1;
		} else {
			set_io_stripe(smap, map, stripe_index, stripe_offset,
				      stripe_nr);
			*mirror_num_ret = mirror_num;
		}
		*bioc_ret = NULL;
		ret = 0;
		goto out;
	}

	bioc = alloc_btrfs_io_context(fs_info, num_alloc_stripes, tgtdev_indexes);
	if (!bioc) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < num_stripes; i++) {
		set_io_stripe(&bioc->stripes[i], map, stripe_index, stripe_offset,
			      stripe_nr);
		stripe_index++;
	}

	/* Build raid_map */
	if (map->type & BTRFS_BLOCK_GROUP_RAID56_MASK && need_raid_map &&
	    (need_full_stripe(op) || mirror_num > 1)) {
		u64 tmp;
		unsigned rot;

		/* Work out the disk rotation on this stripe-set */
		div_u64_rem(stripe_nr, num_stripes, &rot);

		/* Fill in the logical address of each stripe */
		tmp = stripe_nr * data_stripes;
		for (i = 0; i < data_stripes; i++)
			bioc->raid_map[(i + rot) % num_stripes] =
				em->start + (tmp + i) * map->stripe_len;

		bioc->raid_map[(i + rot) % map->num_stripes] = RAID5_P_STRIPE;
		if (map->type & BTRFS_BLOCK_GROUP_RAID6)
			bioc->raid_map[(i + rot + 1) % num_stripes] =
				RAID6_Q_STRIPE;

		sort_parity_stripes(bioc, num_stripes);
	}

	if (need_full_stripe(op))
		max_errors = btrfs_chunk_max_errors(map);

	if (dev_replace_is_ongoing && dev_replace->tgtdev != NULL &&
	    need_full_stripe(op)) {
		handle_ops_on_dev_replace(op, &bioc, dev_replace, logical,
					  &num_stripes, &max_errors);
	}

	*bioc_ret = bioc;
	bioc->map_type = map->type;
	bioc->num_stripes = num_stripes;
	bioc->max_errors = max_errors;
	bioc->mirror_num = mirror_num;

	/*
	 * this is the case that REQ_READ && dev_replace_is_ongoing &&
	 * mirror_num == num_stripes + 1 && dev_replace target drive is
	 * available as a mirror
	 */
	if (patch_the_first_stripe_for_dev_replace && num_stripes > 0) {
		WARN_ON(num_stripes > 1);
		bioc->stripes[0].dev = dev_replace->tgtdev;
		bioc->stripes[0].physical = physical_to_patch_in_first_stripe;
		bioc->mirror_num = map->num_stripes + 1;
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
		      struct btrfs_io_context **bioc_ret, int mirror_num)
{
	return __btrfs_map_block(fs_info, op, logical, length, bioc_ret,
				 NULL, &mirror_num, 0);
}

/* For Scrub/replace */
int btrfs_map_sblock(struct btrfs_fs_info *fs_info, enum btrfs_map_op op,
		     u64 logical, u64 *length,
		     struct btrfs_io_context **bioc_ret)
{
	return __btrfs_map_block(fs_info, op, logical, length, bioc_ret,
				 NULL, NULL, 1);
}

/*
 * Initialize a btrfs_bio structure.  This skips the embedded bio itself as it
 * is already initialized by the block layer.
 */
static inline void btrfs_bio_init(struct btrfs_bio *bbio,
				  btrfs_bio_end_io_t end_io, void *private)
{
	memset(bbio, 0, offsetof(struct btrfs_bio, bio));
	bbio->end_io = end_io;
	bbio->private = private;
}

/*
 * Allocate a btrfs_bio structure.  The btrfs_bio is the main I/O container for
 * btrfs, and is used for all I/O submitted through btrfs_submit_bio.
 *
 * Just like the underlying bio_alloc_bioset it will not fail as it is backed by
 * a mempool.
 */
struct bio *btrfs_bio_alloc(unsigned int nr_vecs, blk_opf_t opf,
			    btrfs_bio_end_io_t end_io, void *private)
{
	struct bio *bio;

	bio = bio_alloc_bioset(NULL, nr_vecs, opf, GFP_NOFS, &btrfs_bioset);
	btrfs_bio_init(btrfs_bio(bio), end_io, private);
	return bio;
}

struct bio *btrfs_bio_clone_partial(struct bio *orig, u64 offset, u64 size,
				    btrfs_bio_end_io_t end_io, void *private)
{
	struct bio *bio;
	struct btrfs_bio *bbio;

	ASSERT(offset <= UINT_MAX && size <= UINT_MAX);

	bio = bio_alloc_clone(orig->bi_bdev, orig, GFP_NOFS, &btrfs_bioset);
	bbio = btrfs_bio(bio);
	btrfs_bio_init(bbio, end_io, private);

	bio_trim(bio, offset >> 9, size >> 9);
	bbio->iter = bio->bi_iter;
	return bio;
}

static void btrfs_log_dev_io_error(struct bio *bio, struct btrfs_device *dev)
{
	if (!dev || !dev->bdev)
		return;
	if (bio->bi_status != BLK_STS_IOERR && bio->bi_status != BLK_STS_TARGET)
		return;

	if (btrfs_op(bio) == BTRFS_MAP_WRITE)
		btrfs_dev_stat_inc_and_print(dev, BTRFS_DEV_STAT_WRITE_ERRS);
	if (!(bio->bi_opf & REQ_RAHEAD))
		btrfs_dev_stat_inc_and_print(dev, BTRFS_DEV_STAT_READ_ERRS);
	if (bio->bi_opf & REQ_PREFLUSH)
		btrfs_dev_stat_inc_and_print(dev, BTRFS_DEV_STAT_FLUSH_ERRS);
}

static struct workqueue_struct *btrfs_end_io_wq(struct btrfs_fs_info *fs_info,
						struct bio *bio)
{
	if (bio->bi_opf & REQ_META)
		return fs_info->endio_meta_workers;
	return fs_info->endio_workers;
}

static void btrfs_end_bio_work(struct work_struct *work)
{
	struct btrfs_bio *bbio =
		container_of(work, struct btrfs_bio, end_io_work);

	bbio->end_io(bbio);
}

static void btrfs_simple_end_io(struct bio *bio)
{
	struct btrfs_fs_info *fs_info = bio->bi_private;
	struct btrfs_bio *bbio = btrfs_bio(bio);

	btrfs_bio_counter_dec(fs_info);

	if (bio->bi_status)
		btrfs_log_dev_io_error(bio, bbio->device);

	if (bio_op(bio) == REQ_OP_READ) {
		INIT_WORK(&bbio->end_io_work, btrfs_end_bio_work);
		queue_work(btrfs_end_io_wq(fs_info, bio), &bbio->end_io_work);
	} else {
		bbio->end_io(bbio);
	}
}

static void btrfs_raid56_end_io(struct bio *bio)
{
	struct btrfs_io_context *bioc = bio->bi_private;
	struct btrfs_bio *bbio = btrfs_bio(bio);

	btrfs_bio_counter_dec(bioc->fs_info);
	bbio->mirror_num = bioc->mirror_num;
	bbio->end_io(bbio);

	btrfs_put_bioc(bioc);
}

static void btrfs_orig_write_end_io(struct bio *bio)
{
	struct btrfs_io_stripe *stripe = bio->bi_private;
	struct btrfs_io_context *bioc = stripe->bioc;
	struct btrfs_bio *bbio = btrfs_bio(bio);

	btrfs_bio_counter_dec(bioc->fs_info);

	if (bio->bi_status) {
		atomic_inc(&bioc->error);
		btrfs_log_dev_io_error(bio, stripe->dev);
	}

	/*
	 * Only send an error to the higher layers if it is beyond the tolerance
	 * threshold.
	 */
	if (atomic_read(&bioc->error) > bioc->max_errors)
		bio->bi_status = BLK_STS_IOERR;
	else
		bio->bi_status = BLK_STS_OK;

	bbio->end_io(bbio);
	btrfs_put_bioc(bioc);
}

static void btrfs_clone_write_end_io(struct bio *bio)
{
	struct btrfs_io_stripe *stripe = bio->bi_private;

	if (bio->bi_status) {
		atomic_inc(&stripe->bioc->error);
		btrfs_log_dev_io_error(bio, stripe->dev);
	}

	/* Pass on control to the original bio this one was cloned from */
	bio_endio(stripe->bioc->orig_bio);
	bio_put(bio);
}

static void btrfs_submit_dev_bio(struct btrfs_device *dev, struct bio *bio)
{
	if (!dev || !dev->bdev ||
	    test_bit(BTRFS_DEV_STATE_MISSING, &dev->dev_state) ||
	    (btrfs_op(bio) == BTRFS_MAP_WRITE &&
	     !test_bit(BTRFS_DEV_STATE_WRITEABLE, &dev->dev_state))) {
		bio_io_error(bio);
		return;
	}

	bio_set_dev(bio, dev->bdev);

	/*
	 * For zone append writing, bi_sector must point the beginning of the
	 * zone
	 */
	if (bio_op(bio) == REQ_OP_ZONE_APPEND) {
		u64 physical = bio->bi_iter.bi_sector << SECTOR_SHIFT;

		if (btrfs_dev_is_sequential(dev, physical)) {
			u64 zone_start = round_down(physical,
						    dev->fs_info->zone_size);

			bio->bi_iter.bi_sector = zone_start >> SECTOR_SHIFT;
		} else {
			bio->bi_opf &= ~REQ_OP_ZONE_APPEND;
			bio->bi_opf |= REQ_OP_WRITE;
		}
	}
	btrfs_debug_in_rcu(dev->fs_info,
	"%s: rw %d 0x%x, sector=%llu, dev=%lu (%s id %llu), size=%u",
		__func__, bio_op(bio), bio->bi_opf, bio->bi_iter.bi_sector,
		(unsigned long)dev->bdev->bd_dev, rcu_str_deref(dev->name),
		dev->devid, bio->bi_iter.bi_size);

	btrfsic_check_bio(bio);
	submit_bio(bio);
}

static void btrfs_submit_mirrored_bio(struct btrfs_io_context *bioc, int dev_nr)
{
	struct bio *orig_bio = bioc->orig_bio, *bio;

	ASSERT(bio_op(orig_bio) != REQ_OP_READ);

	/* Reuse the bio embedded into the btrfs_bio for the last mirror */
	if (dev_nr == bioc->num_stripes - 1) {
		bio = orig_bio;
		bio->bi_end_io = btrfs_orig_write_end_io;
	} else {
		bio = bio_alloc_clone(NULL, orig_bio, GFP_NOFS, &fs_bio_set);
		bio_inc_remaining(orig_bio);
		bio->bi_end_io = btrfs_clone_write_end_io;
	}

	bio->bi_private = &bioc->stripes[dev_nr];
	bio->bi_iter.bi_sector = bioc->stripes[dev_nr].physical >> SECTOR_SHIFT;
	bioc->stripes[dev_nr].bioc = bioc;
	btrfs_submit_dev_bio(bioc->stripes[dev_nr].dev, bio);
}

void btrfs_submit_bio(struct btrfs_fs_info *fs_info, struct bio *bio, int mirror_num)
{
	u64 logical = bio->bi_iter.bi_sector << 9;
	u64 length = bio->bi_iter.bi_size;
	u64 map_length = length;
	struct btrfs_io_context *bioc = NULL;
	struct btrfs_io_stripe smap;
	int ret;

	btrfs_bio_counter_inc_blocked(fs_info);
	ret = __btrfs_map_block(fs_info, btrfs_op(bio), logical, &map_length,
				&bioc, &smap, &mirror_num, 1);
	if (ret) {
		btrfs_bio_counter_dec(fs_info);
		btrfs_bio_end_io(btrfs_bio(bio), errno_to_blk_status(ret));
		return;
	}

	if (map_length < length) {
		btrfs_crit(fs_info,
			   "mapping failed logical %llu bio len %llu len %llu",
			   logical, length, map_length);
		BUG();
	}

	if (!bioc) {
		/* Single mirror read/write fast path */
		btrfs_bio(bio)->mirror_num = mirror_num;
		btrfs_bio(bio)->device = smap.dev;
		bio->bi_iter.bi_sector = smap.physical >> SECTOR_SHIFT;
		bio->bi_private = fs_info;
		bio->bi_end_io = btrfs_simple_end_io;
		btrfs_submit_dev_bio(smap.dev, bio);
	} else if (bioc->map_type & BTRFS_BLOCK_GROUP_RAID56_MASK) {
		/* Parity RAID write or read recovery */
		bio->bi_private = bioc;
		bio->bi_end_io = btrfs_raid56_end_io;
		if (bio_op(bio) == REQ_OP_READ)
			raid56_parity_recover(bio, bioc, mirror_num);
		else
			raid56_parity_write(bio, bioc);
	} else {
		/* Write to multiple mirrors */
		int total_devs = bioc->num_stripes;
		int dev_nr;

		bioc->orig_bio = bio;
		for (dev_nr = 0; dev_nr < total_devs; dev_nr++)
			btrfs_submit_mirrored_bio(bioc, dev_nr);
	}
}

static bool dev_args_match_fs_devices(const struct btrfs_dev_lookup_args *args,
				      const struct btrfs_fs_devices *fs_devices)
{
	if (args->fsid == NULL)
		return true;
	if (memcmp(fs_devices->metadata_uuid, args->fsid, BTRFS_FSID_SIZE) == 0)
		return true;
	return false;
}

static bool dev_args_match_device(const struct btrfs_dev_lookup_args *args,
				  const struct btrfs_device *device)
{
	ASSERT((args->devid != (u64)-1) || args->missing);

	if ((args->devid != (u64)-1) && device->devid != args->devid)
		return false;
	if (args->uuid && memcmp(device->uuid, args->uuid, BTRFS_UUID_SIZE) != 0)
		return false;
	if (!args->missing)
		return true;
	if (test_bit(BTRFS_DEV_STATE_IN_FS_METADATA, &device->dev_state) &&
	    !device->bdev)
		return true;
	return false;
}

/*
 * Find a device specified by @devid or @uuid in the list of @fs_devices, or
 * return NULL.
 *
 * If devid and uuid are both specified, the match must be exact, otherwise
 * only devid is used.
 */
struct btrfs_device *btrfs_find_device(const struct btrfs_fs_devices *fs_devices,
				       const struct btrfs_dev_lookup_args *args)
{
	struct btrfs_device *device;
	struct btrfs_fs_devices *seed_devs;

	if (dev_args_match_fs_devices(args, fs_devices)) {
		list_for_each_entry(device, &fs_devices->devices, dev_list) {
			if (dev_args_match_device(args, device))
				return device;
		}
	}

	list_for_each_entry(seed_devs, &fs_devices->seed_list, seed_list) {
		if (!dev_args_match_fs_devices(args, seed_devs))
			continue;
		list_for_each_entry(device, &seed_devs->devices, dev_list) {
			if (dev_args_match_device(args, device))
				return device;
		}
	}

	return NULL;
}

static struct btrfs_device *add_missing_dev(struct btrfs_fs_devices *fs_devices,
					    u64 devid, u8 *dev_uuid)
{
	struct btrfs_device *device;
	unsigned int nofs_flag;

	/*
	 * We call this under the chunk_mutex, so we want to use NOFS for this
	 * allocation, however we don't want to change btrfs_alloc_device() to
	 * always do NOFS because we use it in a lot of other GFP_KERNEL safe
	 * places.
	 */
	nofs_flag = memalloc_nofs_save();
	device = btrfs_alloc_device(NULL, &devid, dev_uuid);
	memalloc_nofs_restore(nofs_flag);
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

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&dev->dev_list);
	INIT_LIST_HEAD(&dev->dev_alloc_list);
	INIT_LIST_HEAD(&dev->post_commit_list);

	atomic_set(&dev->dev_stats_ccnt, 0);
	btrfs_device_data_ordered_init(dev);
	extent_io_tree_init(fs_info, &dev->alloc_state,
			    IO_TREE_DEVICE_ALLOC_STATE, NULL);

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

u64 btrfs_calc_stripe_length(const struct extent_map *em)
{
	const struct map_lookup *map = em->map_lookup;
	const int data_stripes = calc_data_stripes(map->type, map->num_stripes);

	return div_u64(em->len, data_stripes);
}

#if BITS_PER_LONG == 32
/*
 * Due to page cache limit, metadata beyond BTRFS_32BIT_MAX_FILE_SIZE
 * can't be accessed on 32bit systems.
 *
 * This function do mount time check to reject the fs if it already has
 * metadata chunk beyond that limit.
 */
static int check_32bit_meta_chunk(struct btrfs_fs_info *fs_info,
				  u64 logical, u64 length, u64 type)
{
	if (!(type & BTRFS_BLOCK_GROUP_METADATA))
		return 0;

	if (logical + length < MAX_LFS_FILESIZE)
		return 0;

	btrfs_err_32bit_limit(fs_info);
	return -EOVERFLOW;
}

/*
 * This is to give early warning for any metadata chunk reaching
 * BTRFS_32BIT_EARLY_WARN_THRESHOLD.
 * Although we can still access the metadata, it's not going to be possible
 * once the limit is reached.
 */
static void warn_32bit_meta_chunk(struct btrfs_fs_info *fs_info,
				  u64 logical, u64 length, u64 type)
{
	if (!(type & BTRFS_BLOCK_GROUP_METADATA))
		return;

	if (logical + length < BTRFS_32BIT_EARLY_WARN_THRESHOLD)
		return;

	btrfs_warn_32bit_limit(fs_info);
}
#endif

static struct btrfs_device *handle_missing_device(struct btrfs_fs_info *fs_info,
						  u64 devid, u8 *uuid)
{
	struct btrfs_device *dev;

	if (!btrfs_test_opt(fs_info, DEGRADED)) {
		btrfs_report_missing_device(fs_info, devid, uuid, true);
		return ERR_PTR(-ENOENT);
	}

	dev = add_missing_dev(fs_info->fs_devices, devid, uuid);
	if (IS_ERR(dev)) {
		btrfs_err(fs_info, "failed to init missing device %llu: %ld",
			  devid, PTR_ERR(dev));
		return dev;
	}
	btrfs_report_missing_device(fs_info, devid, uuid, false);

	return dev;
}

static int read_one_chunk(struct btrfs_key *key, struct extent_buffer *leaf,
			  struct btrfs_chunk *chunk)
{
	BTRFS_DEV_LOOKUP_ARGS(args);
	struct btrfs_fs_info *fs_info = leaf->fs_info;
	struct extent_map_tree *map_tree = &fs_info->mapping_tree;
	struct map_lookup *map;
	struct extent_map *em;
	u64 logical;
	u64 length;
	u64 devid;
	u64 type;
	u8 uuid[BTRFS_UUID_SIZE];
	int index;
	int num_stripes;
	int ret;
	int i;

	logical = key->offset;
	length = btrfs_chunk_length(leaf, chunk);
	type = btrfs_chunk_type(leaf, chunk);
	index = btrfs_bg_flags_to_raid_index(type);
	num_stripes = btrfs_chunk_num_stripes(leaf, chunk);

#if BITS_PER_LONG == 32
	ret = check_32bit_meta_chunk(fs_info, logical, length, type);
	if (ret < 0)
		return ret;
	warn_32bit_meta_chunk(fs_info, logical, length, type);
#endif

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
	map->type = type;
	/*
	 * We can't use the sub_stripes value, as for profiles other than
	 * RAID10, they may have 0 as sub_stripes for filesystems created by
	 * older mkfs (<v5.4).
	 * In that case, it can cause divide-by-zero errors later.
	 * Since currently sub_stripes is fixed for each profile, let's
	 * use the trusted value instead.
	 */
	map->sub_stripes = btrfs_raid_array[index].sub_stripes;
	map->verified_stripes = 0;
	em->orig_block_len = btrfs_calc_stripe_length(em);
	for (i = 0; i < num_stripes; i++) {
		map->stripes[i].physical =
			btrfs_stripe_offset_nr(leaf, chunk, i);
		devid = btrfs_stripe_devid_nr(leaf, chunk, i);
		args.devid = devid;
		read_extent_buffer(leaf, uuid, (unsigned long)
				   btrfs_stripe_dev_uuid_nr(chunk, i),
				   BTRFS_UUID_SIZE);
		args.uuid = uuid;
		map->stripes[i].dev = btrfs_find_device(fs_info->fs_devices, &args);
		if (!map->stripes[i].dev) {
			map->stripes[i].dev = handle_missing_device(fs_info,
								    devid, uuid);
			if (IS_ERR(map->stripes[i].dev)) {
				free_extent_map(em);
				return PTR_ERR(map->stripes[i].dev);
			}
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

	/* This will match only for multi-device seed fs */
	list_for_each_entry(fs_devices, &fs_info->fs_devices->seed_list, seed_list)
		if (!memcmp(fs_devices->fsid, fsid, BTRFS_FSID_SIZE))
			return fs_devices;


	fs_devices = find_fsid(fsid, NULL);
	if (!fs_devices) {
		if (!btrfs_test_opt(fs_info, DEGRADED))
			return ERR_PTR(-ENOENT);

		fs_devices = alloc_fs_devices(fsid, NULL);
		if (IS_ERR(fs_devices))
			return fs_devices;

		fs_devices->seeding = true;
		fs_devices->opened = 1;
		return fs_devices;
	}

	/*
	 * Upon first call for a seed fs fsid, just create a private copy of the
	 * respective fs_devices and anchor it at fs_info->fs_devices->seed_list
	 */
	fs_devices = clone_fs_devices(fs_devices);
	if (IS_ERR(fs_devices))
		return fs_devices;

	ret = open_fs_devices(fs_devices, FMODE_READ, fs_info->bdev_holder);
	if (ret) {
		free_fs_devices(fs_devices);
		return ERR_PTR(ret);
	}

	if (!fs_devices->seeding) {
		close_fs_devices(fs_devices);
		free_fs_devices(fs_devices);
		return ERR_PTR(-EINVAL);
	}

	list_add(&fs_devices->seed_list, &fs_info->fs_devices->seed_list);

	return fs_devices;
}

static int read_one_dev(struct extent_buffer *leaf,
			struct btrfs_dev_item *dev_item)
{
	BTRFS_DEV_LOOKUP_ARGS(args);
	struct btrfs_fs_info *fs_info = leaf->fs_info;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	struct btrfs_device *device;
	u64 devid;
	int ret;
	u8 fs_uuid[BTRFS_FSID_SIZE];
	u8 dev_uuid[BTRFS_UUID_SIZE];

	devid = btrfs_device_id(leaf, dev_item);
	args.devid = devid;
	read_extent_buffer(leaf, dev_uuid, btrfs_device_uuid(dev_item),
			   BTRFS_UUID_SIZE);
	read_extent_buffer(leaf, fs_uuid, btrfs_device_fsid(dev_item),
			   BTRFS_FSID_SIZE);
	args.uuid = dev_uuid;
	args.fsid = fs_uuid;

	if (memcmp(fs_uuid, fs_devices->metadata_uuid, BTRFS_FSID_SIZE)) {
		fs_devices = open_seed_devices(fs_info, fs_uuid);
		if (IS_ERR(fs_devices))
			return PTR_ERR(fs_devices);
	}

	device = btrfs_find_device(fs_info->fs_devices, &args);
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
	if (device->bdev) {
		u64 max_total_bytes = bdev_nr_bytes(device->bdev);

		if (device->total_bytes > max_total_bytes) {
			btrfs_err(fs_info,
			"device total_bytes should be at most %llu but found %llu",
				  max_total_bytes, device->total_bytes);
			return -EINVAL;
		}
	}
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
	 * We allocated a dummy extent, just to use extent buffer accessors.
	 * There will be unused space after BTRFS_SUPER_INFO_SIZE, but
	 * that's fine, we will not go beyond system chunk array anyway.
	 */
	sb = alloc_dummy_extent_buffer(fs_info, BTRFS_SUPER_INFO_OFFSET);
	if (!sb)
		return -ENOMEM;
	set_extent_buffer_uptodate(sb);

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

		if (key.type != BTRFS_CHUNK_ITEM_KEY) {
			btrfs_err(fs_info,
			    "unexpected item type %u in sys_array at offset %u",
				  (u32)key.type, cur_offset);
			ret = -EIO;
			break;
		}

		chunk = (struct btrfs_chunk *)sb_array_offset;
		/*
		 * At least one btrfs_chunk with one stripe must be present,
		 * exact stripe count check comes afterwards
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

static void readahead_tree_node_children(struct extent_buffer *node)
{
	int i;
	const int nr_items = btrfs_header_nritems(node);

	for (i = 0; i < nr_items; i++)
		btrfs_readahead_node_child(node, i);
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
	int iter_ret = 0;
	u64 total_dev = 0;
	u64 last_ra_node = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	/*
	 * uuid_mutex is needed only if we are mounting a sprout FS
	 * otherwise we don't need it.
	 */
	mutex_lock(&uuid_mutex);

	/*
	 * It is possible for mount and umount to race in such a way that
	 * we execute this code path, but open_fs_devices failed to clear
	 * total_rw_bytes. We certainly want it cleared before reading the
	 * device items, so clear it here.
	 */
	fs_info->fs_devices->total_rw_bytes = 0;

	/*
	 * Lockdep complains about possible circular locking dependency between
	 * a disk's open_mutex (struct gendisk.open_mutex), the rw semaphores
	 * used for freeze procection of a fs (struct super_block.s_writers),
	 * which we take when starting a transaction, and extent buffers of the
	 * chunk tree if we call read_one_dev() while holding a lock on an
	 * extent buffer of the chunk tree. Since we are mounting the filesystem
	 * and at this point there can't be any concurrent task modifying the
	 * chunk tree, to keep it simple, just skip locking on the chunk tree.
	 */
	ASSERT(!test_bit(BTRFS_FS_OPEN, &fs_info->flags));
	path->skip_locking = 1;

	/*
	 * Read all device items, and then all the chunk items. All
	 * device items are found before any chunk item (their object id
	 * is smaller than the lowest possible object id for a chunk
	 * item - BTRFS_FIRST_CHUNK_TREE_OBJECTID).
	 */
	key.objectid = BTRFS_DEV_ITEMS_OBJECTID;
	key.offset = 0;
	key.type = 0;
	btrfs_for_each_slot(root, &key, &found_key, path, iter_ret) {
		struct extent_buffer *node = path->nodes[1];

		leaf = path->nodes[0];
		slot = path->slots[0];

		if (node) {
			if (last_ra_node != node->start) {
				readahead_tree_node_children(node);
				last_ra_node = node->start;
			}
		}
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

			/*
			 * We are only called at mount time, so no need to take
			 * fs_info->chunk_mutex. Plus, to avoid lockdep warnings,
			 * we always lock first fs_info->chunk_mutex before
			 * acquiring any locks on the chunk tree. This is a
			 * requirement for chunk allocation, see the comment on
			 * top of btrfs_chunk_alloc() for details.
			 */
			chunk = btrfs_item_ptr(leaf, slot, struct btrfs_chunk);
			ret = read_one_chunk(&found_key, leaf, chunk);
			if (ret)
				goto error;
		}
	}
	/* Catch error found during iteration */
	if (iter_ret < 0) {
		ret = iter_ret;
		goto error;
	}

	/*
	 * After loading chunk tree, we've got all device information,
	 * do another round of validation checks.
	 */
	if (total_dev != fs_info->fs_devices->total_devices) {
		btrfs_warn(fs_info,
"super block num_devices %llu mismatch with DEV_ITEM count %llu, will be repaired on next transaction commit",
			  btrfs_super_num_devices(fs_info->super_copy),
			  total_dev);
		fs_info->fs_devices->total_devices = total_dev;
		btrfs_set_super_num_devices(fs_info->super_copy, total_dev);
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
	mutex_unlock(&uuid_mutex);

	btrfs_free_path(path);
	return ret;
}

void btrfs_init_devices_late(struct btrfs_fs_info *fs_info)
{
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices, *seed_devs;
	struct btrfs_device *device;

	fs_devices->fs_info = fs_info;

	mutex_lock(&fs_devices->device_list_mutex);
	list_for_each_entry(device, &fs_devices->devices, dev_list)
		device->fs_info = fs_info;

	list_for_each_entry(seed_devs, &fs_devices->seed_list, seed_list) {
		list_for_each_entry(device, &seed_devs->devices, dev_list)
			device->fs_info = fs_info;

		seed_devs->fs_info = fs_info;
	}
	mutex_unlock(&fs_devices->device_list_mutex);
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

static int btrfs_device_init_dev_stats(struct btrfs_device *device,
				       struct btrfs_path *path)
{
	struct btrfs_dev_stats_item *ptr;
	struct extent_buffer *eb;
	struct btrfs_key key;
	int item_size;
	int i, ret, slot;

	if (!device->fs_info->dev_root)
		return 0;

	key.objectid = BTRFS_DEV_STATS_OBJECTID;
	key.type = BTRFS_PERSISTENT_ITEM_KEY;
	key.offset = device->devid;
	ret = btrfs_search_slot(NULL, device->fs_info->dev_root, &key, path, 0, 0);
	if (ret) {
		for (i = 0; i < BTRFS_DEV_STAT_VALUES_MAX; i++)
			btrfs_dev_stat_set(device, i, 0);
		device->dev_stats_valid = 1;
		btrfs_release_path(path);
		return ret < 0 ? ret : 0;
	}
	slot = path->slots[0];
	eb = path->nodes[0];
	item_size = btrfs_item_size(eb, slot);

	ptr = btrfs_item_ptr(eb, slot, struct btrfs_dev_stats_item);

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

	return 0;
}

int btrfs_init_dev_stats(struct btrfs_fs_info *fs_info)
{
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices, *seed_devs;
	struct btrfs_device *device;
	struct btrfs_path *path = NULL;
	int ret = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	mutex_lock(&fs_devices->device_list_mutex);
	list_for_each_entry(device, &fs_devices->devices, dev_list) {
		ret = btrfs_device_init_dev_stats(device, path);
		if (ret)
			goto out;
	}
	list_for_each_entry(seed_devs, &fs_devices->seed_list, seed_list) {
		list_for_each_entry(device, &seed_devs->devices, dev_list) {
			ret = btrfs_device_init_dev_stats(device, path);
			if (ret)
				goto out;
		}
	}
out:
	mutex_unlock(&fs_devices->device_list_mutex);

	btrfs_free_path(path);
	return ret;
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
	    btrfs_item_size(path->nodes[0], path->slots[0]) < sizeof(*ptr)) {
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
	BTRFS_DEV_LOOKUP_ARGS(args);
	struct btrfs_device *dev;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	int i;

	mutex_lock(&fs_devices->device_list_mutex);
	args.devid = stats->devid;
	dev = btrfs_find_device(fs_info->fs_devices, &args);
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
		btrfs_info(fs_info, "device stats zeroed by %s (%d)",
			   current->comm, task_pid_nr(current));
	} else {
		for (i = 0; i < BTRFS_DEV_STAT_VALUES_MAX; i++)
			if (stats->nr_items > i)
				stats->values[i] = btrfs_dev_stat_read(dev, i);
	}
	if (stats->nr_items > BTRFS_DEV_STAT_VALUES_MAX)
		stats->nr_items = BTRFS_DEV_STAT_VALUES_MAX;
	return 0;
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
	struct btrfs_dev_lookup_args args = { .devid = devid };
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
	stripe_len = btrfs_calc_stripe_length(em);
	if (physical_len != stripe_len) {
		btrfs_err(fs_info,
"dev extent physical offset %llu on devid %llu length doesn't match chunk %llu, have %llu expect %llu",
			  physical_offset, devid, em->start, physical_len,
			  stripe_len);
		ret = -EUCLEAN;
		goto out;
	}

	/*
	 * Very old mkfs.btrfs (before v4.1) will not respect the reserved
	 * space. Although kernel can handle it without problem, better to warn
	 * the users.
	 */
	if (physical_offset < BTRFS_DEVICE_RANGE_RESERVED)
		btrfs_warn(fs_info,
		"devid %llu physical %llu len %llu inside the reserved space",
			   devid, physical_offset, physical_len);

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

	/* Make sure no dev extent is beyond device boundary */
	dev = btrfs_find_device(fs_info->fs_devices, &args);
	if (!dev) {
		btrfs_err(fs_info, "failed to find devid %llu", devid);
		ret = -EUCLEAN;
		goto out;
	}

	if (physical_offset + physical_len > dev->disk_total_bytes) {
		btrfs_err(fs_info,
"dev extent devid %llu physical offset %llu len %llu is beyond device boundary %llu",
			  devid, physical_offset, physical_len,
			  dev->disk_total_bytes);
		ret = -EUCLEAN;
		goto out;
	}

	if (dev->zone_info) {
		u64 zone_size = dev->zone_info->zone_size;

		if (!IS_ALIGNED(physical_offset, zone_size) ||
		    !IS_ALIGNED(physical_len, zone_size)) {
			btrfs_err(fs_info,
"zoned: dev extent devid %llu physical offset %llu len %llu is not aligned to device zone",
				  devid, physical_offset, physical_len);
			ret = -EUCLEAN;
			goto out;
		}
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

	/*
	 * We don't have a dev_root because we mounted with ignorebadroots and
	 * failed to load the root, so we want to skip the verification in this
	 * case for sure.
	 *
	 * However if the dev root is fine, but the tree itself is corrupted
	 * we'd still fail to mount.  This verification is only to make sure
	 * writes can happen safely, so instead just bypass this check
	 * completely in the case of IGNOREBADROOTS.
	 */
	if (btrfs_test_opt(fs_info, IGNOREBADROOTS))
		return 0;

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
		ret = btrfs_next_leaf(root, path);
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

static int relocating_repair_kthread(void *data)
{
	struct btrfs_block_group *cache = data;
	struct btrfs_fs_info *fs_info = cache->fs_info;
	u64 target;
	int ret = 0;

	target = cache->start;
	btrfs_put_block_group(cache);

	sb_start_write(fs_info->sb);
	if (!btrfs_exclop_start(fs_info, BTRFS_EXCLOP_BALANCE)) {
		btrfs_info(fs_info,
			   "zoned: skip relocating block group %llu to repair: EBUSY",
			   target);
		sb_end_write(fs_info->sb);
		return -EBUSY;
	}

	mutex_lock(&fs_info->reclaim_bgs_lock);

	/* Ensure block group still exists */
	cache = btrfs_lookup_block_group(fs_info, target);
	if (!cache)
		goto out;

	if (!test_bit(BLOCK_GROUP_FLAG_RELOCATING_REPAIR, &cache->runtime_flags))
		goto out;

	ret = btrfs_may_alloc_data_chunk(fs_info, target);
	if (ret < 0)
		goto out;

	btrfs_info(fs_info,
		   "zoned: relocating block group %llu to repair IO failure",
		   target);
	ret = btrfs_relocate_chunk(fs_info, target);

out:
	if (cache)
		btrfs_put_block_group(cache);
	mutex_unlock(&fs_info->reclaim_bgs_lock);
	btrfs_exclop_finish(fs_info);
	sb_end_write(fs_info->sb);

	return ret;
}

bool btrfs_repair_one_zone(struct btrfs_fs_info *fs_info, u64 logical)
{
	struct btrfs_block_group *cache;

	if (!btrfs_is_zoned(fs_info))
		return false;

	/* Do not attempt to repair in degraded state */
	if (btrfs_test_opt(fs_info, DEGRADED))
		return true;

	cache = btrfs_lookup_block_group(fs_info, logical);
	if (!cache)
		return true;

	if (test_and_set_bit(BLOCK_GROUP_FLAG_RELOCATING_REPAIR, &cache->runtime_flags)) {
		btrfs_put_block_group(cache);
		return true;
	}

	kthread_run(relocating_repair_kthread, cache,
		    "btrfs-relocating-repair");

	return true;
}

int __init btrfs_bioset_init(void)
{
	if (bioset_init(&btrfs_bioset, BIO_POOL_SIZE,
			offsetof(struct btrfs_bio, bio),
			BIOSET_NEED_BVECS))
		return -ENOMEM;
	return 0;
}

void __cold btrfs_bioset_exit(void)
{
	bioset_exit(&btrfs_bioset);
}

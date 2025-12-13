// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <linux/uuid.h>
#include <linux/list_sort.h>
#include <linux/namei.h>
#include "misc.h"
#include "disk-io.h"
#include "extent-tree.h"
#include "transaction.h"
#include "volumes.h"
#include "raid56.h"
#include "dev-replace.h"
#include "sysfs.h"
#include "tree-checker.h"
#include "space-info.h"
#include "block-group.h"
#include "discard.h"
#include "zoned.h"
#include "fs.h"
#include "accessors.h"
#include "uuid-tree.h"
#include "ioctl.h"
#include "relocation.h"
#include "scrub.h"
#include "super.h"
#include "raid-stripe-tree.h"

#define BTRFS_BLOCK_GROUP_STRIPE_MASK	(BTRFS_BLOCK_GROUP_RAID0 | \
					 BTRFS_BLOCK_GROUP_RAID10 | \
					 BTRFS_BLOCK_GROUP_RAID56_MASK)

struct btrfs_io_geometry {
	u32 stripe_index;
	u32 stripe_nr;
	int mirror_num;
	int num_stripes;
	u64 stripe_offset;
	u64 raid56_full_stripe_start;
	int max_errors;
	enum btrfs_map_op op;
	bool use_rst;
};

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

	if (!flags)
		return;

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
 * Allocate new btrfs_fs_devices structure identified by a fsid.
 *
 * @fsid:    if not NULL, copy the UUID to fs_devices::fsid and to
 *           fs_devices::metadata_fsid
 *
 * Return a pointer to a new struct btrfs_fs_devices on success, or ERR_PTR().
 * The returned struct is not linked onto any lists and can be destroyed with
 * kfree() right away.
 */
static struct btrfs_fs_devices *alloc_fs_devices(const u8 *fsid)
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

	if (fsid) {
		memcpy(fs_devs->fsid, fsid, BTRFS_FSID_SIZE);
		memcpy(fs_devs->metadata_uuid, fsid, BTRFS_FSID_SIZE);
	}

	return fs_devs;
}

static void btrfs_free_device(struct btrfs_device *device)
{
	WARN_ON(!list_empty(&device->post_commit_list));
	/*
	 * No need to call kfree_rcu() nor do RCU lock/unlock, nothing is
	 * reading the device name.
	 */
	kfree(rcu_dereference_raw(device->name));
	btrfs_extent_io_tree_release(&device->alloc_state);
	btrfs_destroy_dev_zone_info(device);
	kfree(device);
}

static void free_fs_devices(struct btrfs_fs_devices *fs_devices)
{
	struct btrfs_device *device;

	WARN_ON(fs_devices->opened);
	WARN_ON(fs_devices->holding);
	while (!list_empty(&fs_devices->devices)) {
		device = list_first_entry(&fs_devices->devices,
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
		fs_devices = list_first_entry(&fs_uuids, struct btrfs_fs_devices,
					      fs_list);
		list_del(&fs_devices->fs_list);
		free_fs_devices(fs_devices);
	}
}

static bool match_fsid_fs_devices(const struct btrfs_fs_devices *fs_devices,
				  const u8 *fsid, const u8 *metadata_fsid)
{
	if (memcmp(fsid, fs_devices->fsid, BTRFS_FSID_SIZE) != 0)
		return false;

	if (!metadata_fsid)
		return true;

	if (memcmp(metadata_fsid, fs_devices->metadata_uuid, BTRFS_FSID_SIZE) != 0)
		return false;

	return true;
}

static noinline struct btrfs_fs_devices *find_fsid(
		const u8 *fsid, const u8 *metadata_fsid)
{
	struct btrfs_fs_devices *fs_devices;

	ASSERT(fsid);

	/* Handle non-split brain cases */
	list_for_each_entry(fs_devices, &fs_uuids, fs_list) {
		if (match_fsid_fs_devices(fs_devices, fsid, metadata_fsid))
			return fs_devices;
	}
	return NULL;
}

static int
btrfs_get_bdev_and_sb(const char *device_path, blk_mode_t flags, void *holder,
		      int flush, struct file **bdev_file,
		      struct btrfs_super_block **disk_super)
{
	struct block_device *bdev;
	int ret;

	*bdev_file = bdev_file_open_by_path(device_path, flags, holder, &fs_holder_ops);

	if (IS_ERR(*bdev_file)) {
		ret = PTR_ERR(*bdev_file);
		btrfs_err(NULL, "failed to open device for path %s with flags 0x%x: %d",
			  device_path, flags, ret);
		goto error;
	}
	bdev = file_bdev(*bdev_file);

	if (flush)
		sync_blockdev(bdev);
	if (holder) {
		ret = set_blocksize(*bdev_file, BTRFS_BDEV_BLOCKSIZE);
		if (ret) {
			bdev_fput(*bdev_file);
			goto error;
		}
	}
	invalidate_bdev(bdev);
	*disk_super = btrfs_read_disk_super(bdev, 0, false);
	if (IS_ERR(*disk_super)) {
		ret = PTR_ERR(*disk_super);
		bdev_fput(*bdev_file);
		goto error;
	}

	return 0;

error:
	*disk_super = NULL;
	*bdev_file = NULL;
	return ret;
}

/*
 *  Search and remove all stale devices (which are not mounted).  When both
 *  inputs are NULL, it will search and release all stale devices.
 *
 *  @devt:         Optional. When provided will it release all unmounted devices
 *                 matching this devt only.
 *  @skip_device:  Optional. Will skip this device when searching for the stale
 *                 devices.
 *
 *  Return:	0 for success or if @devt is 0.
 *		-EBUSY if @devt is a mounted device.
 *		-ENOENT if @devt does not match any device in the list.
 */
static int btrfs_free_stale_devices(dev_t devt, struct btrfs_device *skip_device)
{
	struct btrfs_fs_devices *fs_devices, *tmp_fs_devices;
	struct btrfs_device *device, *tmp_device;
	int ret;
	bool freed = false;

	lockdep_assert_held(&uuid_mutex);

	/* Return good status if there is no instance of devt. */
	ret = 0;
	list_for_each_entry_safe(fs_devices, tmp_fs_devices, &fs_uuids, fs_list) {

		mutex_lock(&fs_devices->device_list_mutex);
		list_for_each_entry_safe(device, tmp_device,
					 &fs_devices->devices, dev_list) {
			if (skip_device && skip_device == device)
				continue;
			if (devt && devt != device->devt)
				continue;
			if (fs_devices->opened || fs_devices->holding) {
				if (devt)
					ret = -EBUSY;
				break;
			}

			/* delete the stale device */
			fs_devices->num_devices--;
			list_del(&device->dev_list);
			btrfs_free_device(device);

			freed = true;
		}
		mutex_unlock(&fs_devices->device_list_mutex);

		if (fs_devices->num_devices == 0) {
			btrfs_sysfs_remove_fsid(fs_devices);
			list_del(&fs_devices->fs_list);
			free_fs_devices(fs_devices);
		}
	}

	/* If there is at least one freed device return 0. */
	if (freed)
		return 0;

	return ret;
}

static struct btrfs_fs_devices *find_fsid_by_device(
					struct btrfs_super_block *disk_super,
					dev_t devt, bool *same_fsid_diff_dev)
{
	struct btrfs_fs_devices *fsid_fs_devices;
	struct btrfs_fs_devices *devt_fs_devices;
	const bool has_metadata_uuid = (btrfs_super_incompat_flags(disk_super) &
					BTRFS_FEATURE_INCOMPAT_METADATA_UUID);
	bool found_by_devt = false;

	/* Find the fs_device by the usual method, if found use it. */
	fsid_fs_devices = find_fsid(disk_super->fsid,
		    has_metadata_uuid ? disk_super->metadata_uuid : NULL);

	/* The temp_fsid feature is supported only with single device filesystem. */
	if (btrfs_super_num_devices(disk_super) != 1)
		return fsid_fs_devices;

	/*
	 * A seed device is an integral component of the sprout device, which
	 * functions as a multi-device filesystem. So, temp-fsid feature is
	 * not supported.
	 */
	if (btrfs_super_flags(disk_super) & BTRFS_SUPER_FLAG_SEEDING)
		return fsid_fs_devices;

	/* Try to find a fs_devices by matching devt. */
	list_for_each_entry(devt_fs_devices, &fs_uuids, fs_list) {
		struct btrfs_device *device;

		list_for_each_entry(device, &devt_fs_devices->devices, dev_list) {
			if (device->devt == devt) {
				found_by_devt = true;
				break;
			}
		}
		if (found_by_devt)
			break;
	}

	if (found_by_devt) {
		/* Existing device. */
		if (fsid_fs_devices == NULL) {
			if (devt_fs_devices->opened == 0) {
				/* Stale device. */
				return NULL;
			} else {
				/* temp_fsid is mounting a subvol. */
				return devt_fs_devices;
			}
		} else {
			/* Regular or temp_fsid device mounting a subvol. */
			return devt_fs_devices;
		}
	} else {
		/* New device. */
		if (fsid_fs_devices == NULL) {
			return NULL;
		} else {
			/* sb::fsid is already used create a new temp_fsid. */
			*same_fsid_diff_dev = true;
			return NULL;
		}
	}

	/* Not reached. */
}

/*
 * This is only used on mount, and we are protected from competing things
 * messing with our fs_devices by the uuid_mutex, thus we do not need the
 * fs_devices->device_list_mutex here.
 */
static int btrfs_open_one_device(struct btrfs_fs_devices *fs_devices,
			struct btrfs_device *device, blk_mode_t flags,
			void *holder)
{
	struct file *bdev_file;
	struct btrfs_super_block *disk_super;
	u64 devid;
	int ret;

	if (device->bdev)
		return -EINVAL;
	if (!device->name)
		return -EINVAL;

	ret = btrfs_get_bdev_and_sb(rcu_dereference_raw(device->name), flags, holder, 1,
				    &bdev_file, &disk_super);
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
			btrfs_err(NULL,
				  "invalid seeding and uuid-changed device detected");
			goto error_free_page;
		}

		clear_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state);
		fs_devices->seeding = true;
	} else {
		if (bdev_read_only(file_bdev(bdev_file)))
			clear_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state);
		else
			set_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state);
	}

	if (!bdev_nonrot(file_bdev(bdev_file)))
		fs_devices->rotating = true;

	if (bdev_max_discard_sectors(file_bdev(bdev_file)))
		fs_devices->discardable = true;

	device->bdev_file = bdev_file;
	device->bdev = file_bdev(bdev_file);
	clear_bit(BTRFS_DEV_STATE_IN_FS_METADATA, &device->dev_state);

	if (device->devt != device->bdev->bd_dev) {
		btrfs_warn(NULL,
			   "device %s maj:min changed from %d:%d to %d:%d",
			   rcu_dereference_raw(device->name), MAJOR(device->devt),
			   MINOR(device->devt), MAJOR(device->bdev->bd_dev),
			   MINOR(device->bdev->bd_dev));

		device->devt = device->bdev->bd_dev;
	}

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
	bdev_fput(bdev_file);

	return -EINVAL;
}

const u8 *btrfs_sb_fsid_ptr(const struct btrfs_super_block *sb)
{
	bool has_metadata_uuid = (btrfs_super_incompat_flags(sb) &
				  BTRFS_FEATURE_INCOMPAT_METADATA_UUID);

	return has_metadata_uuid ? sb->metadata_uuid : sb->fsid;
}

static bool is_same_device(struct btrfs_device *device, const char *new_path)
{
	struct path old = { .mnt = NULL, .dentry = NULL };
	struct path new = { .mnt = NULL, .dentry = NULL };
	char *old_path = NULL;
	bool is_same = false;
	int ret;

	if (!device->name)
		goto out;

	old_path = kzalloc(PATH_MAX, GFP_NOFS);
	if (!old_path)
		goto out;

	rcu_read_lock();
	ret = strscpy(old_path, rcu_dereference(device->name), PATH_MAX);
	rcu_read_unlock();
	if (ret < 0)
		goto out;

	ret = kern_path(old_path, LOOKUP_FOLLOW, &old);
	if (ret)
		goto out;
	ret = kern_path(new_path, LOOKUP_FOLLOW, &new);
	if (ret)
		goto out;
	if (path_equal(&old, &new))
		is_same = true;
out:
	kfree(old_path);
	path_put(&old);
	path_put(&new);
	return is_same;
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
	const char *name;
	u64 found_transid = btrfs_super_generation(disk_super);
	u64 devid = btrfs_stack_device_id(&disk_super->dev_item);
	dev_t path_devt;
	int ret;
	bool same_fsid_diff_dev = false;
	bool has_metadata_uuid = (btrfs_super_incompat_flags(disk_super) &
		BTRFS_FEATURE_INCOMPAT_METADATA_UUID);

	if (btrfs_super_flags(disk_super) & BTRFS_SUPER_FLAG_CHANGING_FSID_V2) {
		btrfs_err(NULL,
"device %s has incomplete metadata_uuid change, please use btrfstune to complete",
			  path);
		return ERR_PTR(-EAGAIN);
	}

	ret = lookup_bdev(path, &path_devt);
	if (ret) {
		btrfs_err(NULL, "failed to lookup block device for path %s: %d",
			  path, ret);
		return ERR_PTR(ret);
	}

	fs_devices = find_fsid_by_device(disk_super, path_devt, &same_fsid_diff_dev);

	if (!fs_devices) {
		fs_devices = alloc_fs_devices(disk_super->fsid);
		if (IS_ERR(fs_devices))
			return ERR_CAST(fs_devices);

		if (has_metadata_uuid)
			memcpy(fs_devices->metadata_uuid,
			       disk_super->metadata_uuid, BTRFS_FSID_SIZE);

		if (same_fsid_diff_dev) {
			generate_random_uuid(fs_devices->fsid);
			fs_devices->temp_fsid = true;
			btrfs_info(NULL, "device %s (%d:%d) using temp-fsid %pU",
				path, MAJOR(path_devt), MINOR(path_devt),
				fs_devices->fsid);
		}

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

		if (found_transid > fs_devices->latest_generation) {
			memcpy(fs_devices->fsid, disk_super->fsid,
					BTRFS_FSID_SIZE);
			memcpy(fs_devices->metadata_uuid,
			       btrfs_sb_fsid_ptr(disk_super), BTRFS_FSID_SIZE);
		}
	}

	if (!device) {
		unsigned int nofs_flag;

		if (fs_devices->opened) {
			btrfs_err(NULL,
"device %s (%d:%d) belongs to fsid %pU, and the fs is already mounted, scanned by %s (%d)",
				  path, MAJOR(path_devt), MINOR(path_devt),
				  fs_devices->fsid, current->comm,
				  task_pid_nr(current));
			mutex_unlock(&fs_devices->device_list_mutex);
			return ERR_PTR(-EBUSY);
		}

		nofs_flag = memalloc_nofs_save();
		device = btrfs_alloc_device(NULL, &devid,
					    disk_super->dev_item.uuid, path);
		memalloc_nofs_restore(nofs_flag);
		if (IS_ERR(device)) {
			mutex_unlock(&fs_devices->device_list_mutex);
			/* we can safely leave the fs_devices entry around */
			return device;
		}

		device->devt = path_devt;

		list_add_rcu(&device->dev_list, &fs_devices->devices);
		fs_devices->num_devices++;

		device->fs_devices = fs_devices;
		*new_device_added = true;

		if (disk_super->label[0])
			pr_info(
"BTRFS: device label %s devid %llu transid %llu %s (%d:%d) scanned by %s (%d)\n",
				disk_super->label, devid, found_transid, path,
				MAJOR(path_devt), MINOR(path_devt),
				current->comm, task_pid_nr(current));
		else
			pr_info(
"BTRFS: device fsid %pU devid %llu transid %llu %s (%d:%d) scanned by %s (%d)\n",
				disk_super->fsid, devid, found_transid, path,
				MAJOR(path_devt), MINOR(path_devt),
				current->comm, task_pid_nr(current));

	} else if (!device->name || !is_same_device(device, path)) {
		const char *old_name;

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
			btrfs_err(NULL,
"device %s already registered with a higher generation, found %llu expect %llu",
				  path, found_transid, device->generation);
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
				btrfs_warn(NULL,
	"duplicate device %s devid %llu generation %llu scanned by %s (%d)",
						  path, devid, found_transid,
						  current->comm,
						  task_pid_nr(current));
				return ERR_PTR(-EEXIST);
			}
			btrfs_info(NULL,
	"devid %llu device path %s changed to %s scanned by %s (%d)",
					  devid, btrfs_dev_name(device),
					  path, current->comm,
					  task_pid_nr(current));
		}

		name = kstrdup(path, GFP_NOFS);
		if (!name) {
			mutex_unlock(&fs_devices->device_list_mutex);
			return ERR_PTR(-ENOMEM);
		}
		rcu_read_lock();
		old_name = rcu_dereference(device->name);
		rcu_read_unlock();
		rcu_assign_pointer(device->name, name);
		kfree_rcu_mightsleep(old_name);

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

	fs_devices = alloc_fs_devices(orig->fsid);
	if (IS_ERR(fs_devices))
		return fs_devices;

	fs_devices->total_devices = orig->total_devices;

	list_for_each_entry(orig_dev, &orig->devices, dev_list) {
		const char *dev_path = NULL;

		/*
		 * This is ok to do without RCU read locked because we hold the
		 * uuid mutex so nothing we touch in here is going to disappear.
		 */
		if (orig_dev->name)
			dev_path = rcu_dereference_raw(orig_dev->name);

		device = btrfs_alloc_device(NULL, &orig_dev->devid,
					    orig_dev->uuid, dev_path);
		if (IS_ERR(device)) {
			ret = PTR_ERR(device);
			goto error;
		}

		if (orig_dev->zone_info) {
			struct btrfs_zoned_device_info *zone_info;

			zone_info = btrfs_clone_dev_zone_info(orig_dev);
			if (!zone_info) {
				btrfs_free_device(device);
				ret = -ENOMEM;
				goto error;
			}
			device->zone_info = zone_info;
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

		if (device->bdev_file) {
			bdev_fput(device->bdev_file);
			device->bdev = NULL;
			device->bdev_file = NULL;
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

	bdev_fput(device->bdev_file);
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
		device->bdev_file = NULL;
	}
	clear_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state);
	btrfs_destroy_dev_zone_info(device);

	device->fs_info = NULL;
	atomic_set(&device->dev_stats_ccnt, 0);
	btrfs_extent_io_tree_release(&device->alloc_state);

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
	WARN_ON(test_bit(BTRFS_DEV_STATE_FLUSH_SENT, &device->dev_state));
	WARN_ON(test_bit(BTRFS_DEV_STATE_REPLACE_TGT, &device->dev_state));
	WARN_ON(!list_empty(&device->dev_alloc_list));
	WARN_ON(!list_empty(&device->post_commit_list));
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
	if (!fs_devices->opened && !fs_devices->holding) {
		list_splice_init(&fs_devices->seed_list, &list);

		/*
		 * If the struct btrfs_fs_devices is not assembled with any
		 * other device, it can be re-initialized during the next mount
		 * without the needing device-scan step. Therefore, it can be
		 * fully freed.
		 */
		if (fs_devices->num_devices == 1) {
			list_del(&fs_devices->fs_list);
			free_fs_devices(fs_devices);
		}
	}


	list_for_each_entry_safe(fs_devices, tmp, &list, seed_list) {
		close_fs_devices(fs_devices);
		list_del(&fs_devices->seed_list);
		free_fs_devices(fs_devices);
	}
	mutex_unlock(&uuid_mutex);
}

static int open_fs_devices(struct btrfs_fs_devices *fs_devices,
				blk_mode_t flags, void *holder)
{
	struct btrfs_device *device;
	struct btrfs_device *latest_dev = NULL;
	struct btrfs_device *tmp_device;
	s64 __maybe_unused value = 0;
	int ret = 0;

	list_for_each_entry_safe(device, tmp_device, &fs_devices->devices,
				 dev_list) {
		int ret2;

		ret2 = btrfs_open_one_device(fs_devices, device, flags, holder);
		if (ret2 == 0 &&
		    (!latest_dev || device->generation > latest_dev->generation)) {
			latest_dev = device;
		} else if (ret2 == -ENODATA) {
			fs_devices->num_devices--;
			list_del(&device->dev_list);
			btrfs_free_device(device);
		}
		if (ret == 0 && ret2 != 0)
			ret = ret2;
	}

	if (fs_devices->open_devices == 0) {
		if (ret)
			return ret;
		return -EINVAL;
	}

	fs_devices->opened = 1;
	fs_devices->latest_dev = latest_dev;
	fs_devices->total_rw_bytes = 0;
	fs_devices->chunk_alloc_policy = BTRFS_CHUNK_ALLOC_REGULAR;
#ifdef CONFIG_BTRFS_EXPERIMENTAL
	fs_devices->rr_min_contig_read = BTRFS_DEFAULT_RR_MIN_CONTIG_READ;
	fs_devices->read_devid = latest_dev->devid;
	fs_devices->read_policy = btrfs_read_policy_to_enum(btrfs_get_mod_read_policy(),
							    &value);
	if (fs_devices->read_policy == BTRFS_READ_POLICY_RR)
		fs_devices->collect_fs_stats = true;

	if (value) {
		if (fs_devices->read_policy == BTRFS_READ_POLICY_RR)
			fs_devices->rr_min_contig_read = value;
		if (fs_devices->read_policy == BTRFS_READ_POLICY_DEVID)
			fs_devices->read_devid = value;
	}
#else
	fs_devices->read_policy = BTRFS_READ_POLICY_PID;
#endif

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
		       blk_mode_t flags, void *holder)
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

struct btrfs_super_block *btrfs_read_disk_super(struct block_device *bdev,
						int copy_num, bool drop_cache)
{
	struct btrfs_super_block *super;
	struct page *page;
	u64 bytenr, bytenr_orig;
	struct address_space *mapping = bdev->bd_mapping;
	int ret;

	bytenr_orig = btrfs_sb_offset(copy_num);
	ret = btrfs_sb_log_location_bdev(bdev, copy_num, READ, &bytenr);
	if (ret < 0) {
		if (ret == -ENOENT)
			ret = -EINVAL;
		return ERR_PTR(ret);
	}

	if (bytenr + BTRFS_SUPER_INFO_SIZE >= bdev_nr_bytes(bdev))
		return ERR_PTR(-EINVAL);

	if (drop_cache) {
		/* This should only be called with the primary sb. */
		ASSERT(copy_num == 0);

		/*
		 * Drop the page of the primary superblock, so later read will
		 * always read from the device.
		 */
		invalidate_inode_pages2_range(mapping, bytenr >> PAGE_SHIFT,
				      (bytenr + BTRFS_SUPER_INFO_SIZE) >> PAGE_SHIFT);
	}

	page = read_cache_page_gfp(mapping, bytenr >> PAGE_SHIFT, GFP_NOFS);
	if (IS_ERR(page))
		return ERR_CAST(page);

	super = page_address(page);
	if (btrfs_super_magic(super) != BTRFS_MAGIC ||
	    btrfs_super_bytenr(super) != bytenr_orig) {
		btrfs_release_disk_super(super);
		return ERR_PTR(-EINVAL);
	}

	/*
	 * Make sure the last byte of label is properly NUL terminated.  We use
	 * '%s' to print the label, if not properly NUL terminated we can access
	 * beyond the label.
	 */
	if (super->label[0] && super->label[BTRFS_LABEL_SIZE - 1])
		super->label[BTRFS_LABEL_SIZE - 1] = 0;

	return super;
}

int btrfs_forget_devices(dev_t devt)
{
	int ret;

	mutex_lock(&uuid_mutex);
	ret = btrfs_free_stale_devices(devt, NULL);
	mutex_unlock(&uuid_mutex);

	return ret;
}

static bool btrfs_skip_registration(struct btrfs_super_block *disk_super,
				    const char *path, dev_t devt,
				    bool mount_arg_dev)
{
	struct btrfs_fs_devices *fs_devices;

	/*
	 * Do not skip device registration for mounted devices with matching
	 * maj:min but different paths. Booting without initrd relies on
	 * /dev/root initially, later replaced with the actual root device.
	 * A successful scan ensures grub2-probe selects the correct device.
	 */
	list_for_each_entry(fs_devices, &fs_uuids, fs_list) {
		struct btrfs_device *device;

		mutex_lock(&fs_devices->device_list_mutex);

		if (!fs_devices->opened) {
			mutex_unlock(&fs_devices->device_list_mutex);
			continue;
		}

		list_for_each_entry(device, &fs_devices->devices, dev_list) {
			if (device->bdev && (device->bdev->bd_dev == devt) &&
			    strcmp(rcu_dereference_raw(device->name), path) != 0) {
				mutex_unlock(&fs_devices->device_list_mutex);

				/* Do not skip registration. */
				return false;
			}
		}
		mutex_unlock(&fs_devices->device_list_mutex);
	}

	if (!mount_arg_dev && btrfs_super_num_devices(disk_super) == 1 &&
	    !(btrfs_super_flags(disk_super) & BTRFS_SUPER_FLAG_SEEDING))
		return true;

	return false;
}

/*
 * Look for a btrfs signature on a device. This may be called out of the mount path
 * and we are not allowed to call set_blocksize during the scan. The superblock
 * is read via pagecache.
 *
 * With @mount_arg_dev it's a scan during mount time that will always register
 * the device or return an error. Multi-device and seeding devices are registered
 * in both cases.
 */
struct btrfs_device *btrfs_scan_one_device(const char *path,
					   bool mount_arg_dev)
{
	struct btrfs_super_block *disk_super;
	bool new_device_added = false;
	struct btrfs_device *device = NULL;
	struct file *bdev_file;
	dev_t devt;

	lockdep_assert_held(&uuid_mutex);

	/*
	 * Avoid an exclusive open here, as the systemd-udev may initiate the
	 * device scan which may race with the user's mount or mkfs command,
	 * resulting in failure.
	 * Since the device scan is solely for reading purposes, there is no
	 * need for an exclusive open. Additionally, the devices are read again
	 * during the mount process. It is ok to get some inconsistent
	 * values temporarily, as the device paths of the fsid are the only
	 * required information for assembling the volume.
	 */
	bdev_file = bdev_file_open_by_path(path, BLK_OPEN_READ, NULL, NULL);
	if (IS_ERR(bdev_file))
		return ERR_CAST(bdev_file);

	disk_super = btrfs_read_disk_super(file_bdev(bdev_file), 0, false);
	if (IS_ERR(disk_super)) {
		device = ERR_CAST(disk_super);
		goto error_bdev_put;
	}

	devt = file_bdev(bdev_file)->bd_dev;
	if (btrfs_skip_registration(disk_super, path, devt, mount_arg_dev)) {
		btrfs_debug(NULL, "skip registering single non-seed device %s (%d:%d)",
			  path, MAJOR(devt), MINOR(devt));

		btrfs_free_stale_devices(devt, NULL);

		device = NULL;
		goto free_disk_super;
	}

	device = device_list_add(path, disk_super, &new_device_added);
	if (!IS_ERR(device) && new_device_added)
		btrfs_free_stale_devices(device->devt, device);

free_disk_super:
	btrfs_release_disk_super(disk_super);

error_bdev_put:
	bdev_fput(bdev_file);

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

	if (btrfs_find_first_extent_bit(&device->alloc_state, *start,
					&physical_start, &physical_end,
					CHUNK_ALLOCATED, NULL)) {

		if (in_range(physical_start, *start, len) ||
		    in_range(*start, physical_start,
			     physical_end + 1 - physical_start)) {
			*start = physical_end + 1;
			return true;
		}
	}
	return false;
}

static u64 dev_extent_search_start(struct btrfs_device *device)
{
	switch (device->fs_devices->chunk_alloc_policy) {
	default:
		btrfs_warn_unknown_chunk_allocation(device->fs_devices->chunk_alloc_policy);
		fallthrough;
	case BTRFS_CHUNK_ALLOC_REGULAR:
		return BTRFS_DEVICE_RANGE_RESERVED;
	case BTRFS_CHUNK_ALLOC_ZONED:
		/*
		 * We don't care about the starting region like regular
		 * allocator, because we anyway use/reserve the first two zones
		 * for superblock logging.
		 */
		return 0;
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

	ASSERT(IS_ALIGNED(*hole_start, zone_size),
	       "hole_start=%llu zone_size=%llu", *hole_start, zone_size);

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

/*
 * Check if specified hole is suitable for allocation.
 *
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
		default:
			btrfs_warn_unknown_chunk_allocation(device->fs_devices->chunk_alloc_policy);
			fallthrough;
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
		}

		break;
	}

	return changed;
}

/*
 * Find free space in the specified device.
 *
 * @device:	  the device which we search the free space in
 * @num_bytes:	  the size of the free space that we need
 * @search_start: the position from which to begin the search
 * @start:	  store the start of the free space.
 * @len:	  the size of the free space. that we find, or the size
 *		  of the max free space if we don't find suitable free space
 *
 * This does a pretty simple search, the expectation is that it is called very
 * infrequently and that a given device has a small number of extents.
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
static int find_free_dev_extent(struct btrfs_device *device, u64 num_bytes,
				u64 *start, u64 *len)
{
	struct btrfs_fs_info *fs_info = device->fs_info;
	struct btrfs_root *root = fs_info->dev_root;
	struct btrfs_key key;
	struct btrfs_dev_extent *dev_extent;
	struct btrfs_path *path;
	u64 search_start;
	u64 hole_size;
	u64 max_hole_start;
	u64 max_hole_size = 0;
	u64 extent_end;
	u64 search_end = device->total_bytes;
	int ret;
	int slot;
	struct extent_buffer *l;

	search_start = dev_extent_search_start(device);
	max_hole_start = search_start;

	WARN_ON(device->zone_info &&
		!IS_ALIGNED(num_bytes, device->zone_info->zone_size));

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}
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
	key.type = BTRFS_DEV_EXTENT_KEY;
	key.offset = search_start;

	ret = btrfs_search_backwards(root, &key, path);
	if (ret < 0)
		goto out;

	while (search_start < search_end) {
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

		if (key.offset > search_end)
			break;

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

	ASSERT(max_hole_start + max_hole_size <= search_end,
	       "max_hole_start=%llu max_hole_size=%llu search_end=%llu",
	       max_hole_start, max_hole_size, search_end);
out:
	btrfs_free_path(path);
	*start = max_hole_start;
	if (len)
		*len = max_hole_size;
	return ret;
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
	key.type = BTRFS_DEV_EXTENT_KEY;
	key.offset = start;
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
	struct rb_node *n;
	u64 ret = 0;

	read_lock(&fs_info->mapping_tree_lock);
	n = rb_last(&fs_info->mapping_tree.rb_root);
	if (n) {
		struct btrfs_chunk_map *map;

		map = rb_entry(n, struct btrfs_chunk_map, rb_node);
		ret = map->start + map->chunk_len;
	}
	read_unlock(&fs_info->mapping_tree_lock);

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

	if (unlikely(ret == 0)) {
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
	int ret;

	ret = kern_path(device_path, LOOKUP_FOLLOW, &path);
	if (ret)
		return;

	inode_update_time(d_inode(path.dentry), S_MTIME | S_CTIME | S_VERSION);
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
		ASSERT(num_devices > 1, "num_devices=%llu", num_devices);
		num_devices--;
	}
	up_read(&fs_info->dev_replace.rwsem);

	return num_devices;
}

static void btrfs_scratch_superblock(struct btrfs_fs_info *fs_info,
				     struct block_device *bdev, int copy_num)
{
	struct btrfs_super_block *disk_super;
	const size_t len = sizeof(disk_super->magic);
	const u64 bytenr = btrfs_sb_offset(copy_num);
	int ret;

	disk_super = btrfs_read_disk_super(bdev, copy_num, false);
	if (IS_ERR(disk_super))
		return;

	memset(&disk_super->magic, 0, len);
	folio_mark_dirty(virt_to_folio(disk_super));
	btrfs_release_disk_super(disk_super);

	ret = sync_blockdev_range(bdev, bytenr, bytenr + len - 1);
	if (ret)
		btrfs_warn(fs_info, "error clearing superblock number %d (%d)",
			copy_num, ret);
}

void btrfs_scratch_superblocks(struct btrfs_fs_info *fs_info, struct btrfs_device *device)
{
	int copy_num;
	struct block_device *bdev = device->bdev;

	if (!bdev)
		return;

	for (copy_num = 0; copy_num < BTRFS_SUPER_MIRROR_MAX; copy_num++) {
		if (bdev_is_zoned(bdev))
			btrfs_reset_sb_log_zones(bdev, copy_num);
		else
			btrfs_scratch_superblock(fs_info, bdev, copy_num);
	}

	/* Notify udev that device has changed */
	btrfs_kobject_uevent(bdev, KOBJ_CHANGE);

	/* Update ctime/mtime for device path for libblkid */
	update_dev_time(rcu_dereference_raw(device->name));
}

int btrfs_rm_device(struct btrfs_fs_info *fs_info,
		    struct btrfs_dev_lookup_args *args,
		    struct file **bdev_file)
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
		btrfs_warn(fs_info,
		  "cannot remove device %s (devid %llu) due to active swapfile",
				  btrfs_dev_name(device), device->devid);
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
	if (unlikely(ret)) {
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

	if (device->bdev_file) {
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
	 * write lock, and bdev_fput() on the block device will pull in the
	 * ->open_mutex on the block device and it's dependencies.  Instead
	 *  just flush the device and let the caller do the final bdev_release.
	 */
	if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state)) {
		btrfs_scratch_superblocks(fs_info, device);
		if (device->bdev) {
			sync_blockdev(device->bdev);
			invalidate_bdev(device->bdev);
		}
	}

	*bdev_file = device->bdev_file;
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
		ASSERT(cur_devices->opened == 1, "opened=%d", cur_devices->opened);
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

	btrfs_scratch_superblocks(tgtdev->fs_info, tgtdev);

	btrfs_close_bdev(tgtdev);
	synchronize_rcu();
	btrfs_free_device(tgtdev);
}

/*
 * Populate args from device at path.
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
	struct file *bdev_file;
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

	ret = btrfs_get_bdev_and_sb(path, BLK_OPEN_READ, NULL, 0,
				    &bdev_file, &disk_super);
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
	bdev_fput(bdev_file);
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
	seed_devices = alloc_fs_devices(NULL);
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
	key.type = BTRFS_DEV_ITEM_KEY;
	key.offset = 0;

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

		if (device->fs_devices->seeding)
			btrfs_set_device_generation(leaf, dev_item,
						    device->generation);

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
	struct file *bdev_file;
	struct super_block *sb = fs_info->sb;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	struct btrfs_fs_devices *seed_devices = NULL;
	u64 orig_super_total_bytes;
	u64 orig_super_num_devices;
	int ret = 0;
	bool seeding_dev = false;
	bool locked = false;

	if (sb_rdonly(sb) && !fs_devices->seeding)
		return -EROFS;

	bdev_file = bdev_file_open_by_path(device_path, BLK_OPEN_WRITE,
					   fs_info->sb, &fs_holder_ops);
	if (IS_ERR(bdev_file))
		return PTR_ERR(bdev_file);

	if (!btrfs_check_device_zone_type(fs_info, file_bdev(bdev_file))) {
		ret = -EINVAL;
		goto error;
	}

	if (bdev_nr_bytes(file_bdev(bdev_file)) <= BTRFS_DEVICE_RANGE_RESERVED) {
		ret = -EINVAL;
		goto error;
	}

	if (fs_devices->seeding) {
		seeding_dev = true;
		down_write(&sb->s_umount);
		mutex_lock(&uuid_mutex);
		locked = true;
	}

	sync_blockdev(file_bdev(bdev_file));

	rcu_read_lock();
	list_for_each_entry_rcu(device, &fs_devices->devices, dev_list) {
		if (device->bdev == file_bdev(bdev_file)) {
			ret = -EEXIST;
			rcu_read_unlock();
			goto error;
		}
	}
	rcu_read_unlock();

	device = btrfs_alloc_device(fs_info, NULL, NULL, device_path);
	if (IS_ERR(device)) {
		/* we can safely leave the fs_devices entry around */
		ret = PTR_ERR(device);
		goto error;
	}

	device->fs_info = fs_info;
	device->bdev_file = bdev_file;
	device->bdev = file_bdev(bdev_file);
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
		round_down(bdev_nr_bytes(device->bdev), fs_info->sectorsize);
	device->disk_total_bytes = device->total_bytes;
	device->commit_total_bytes = device->total_bytes;
	set_bit(BTRFS_DEV_STATE_IN_FS_METADATA, &device->dev_state);
	clear_bit(BTRFS_DEV_STATE_REPLACE_TGT, &device->dev_state);
	device->dev_stats_valid = 1;
	set_blocksize(device->bdev_file, BTRFS_BDEV_BLOCKSIZE);

	if (seeding_dev) {
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

	if (!bdev_nonrot(device->bdev))
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
		if (unlikely(ret)) {
			btrfs_abort_transaction(trans, ret);
			goto error_sysfs;
		}
	}

	ret = btrfs_add_dev_item(trans, device);
	if (unlikely(ret)) {
		btrfs_abort_transaction(trans, ret);
		goto error_sysfs;
	}

	if (seeding_dev) {
		ret = btrfs_finish_sprout(trans);
		if (unlikely(ret)) {
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
	if (trans)
		btrfs_end_transaction(trans);
error_free_zone:
	btrfs_destroy_dev_zone_info(device);
error_free_device:
	btrfs_free_device(device);
error:
	bdev_fput(bdev_file);
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
	atomic64_add(diff, &fs_info->free_chunk_space);

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
	key.type = BTRFS_CHUNK_ITEM_KEY;
	key.offset = chunk_offset;

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0)
		goto out;
	else if (unlikely(ret > 0)) { /* Logic error or corruption */
		btrfs_err(fs_info, "failed to lookup chunk %llu when freeing",
			  chunk_offset);
		btrfs_abort_transaction(trans, -ENOENT);
		ret = -EUCLEAN;
		goto out;
	}

	ret = btrfs_del_item(trans, root, path);
	if (unlikely(ret < 0)) {
		btrfs_err(fs_info, "failed to delete chunk %llu item", chunk_offset);
		btrfs_abort_transaction(trans, ret);
		goto out;
	}
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

struct btrfs_chunk_map *btrfs_find_chunk_map_nolock(struct btrfs_fs_info *fs_info,
						    u64 logical, u64 length)
{
	struct rb_node *node = fs_info->mapping_tree.rb_root.rb_node;
	struct rb_node *prev = NULL;
	struct rb_node *orig_prev;
	struct btrfs_chunk_map *map;
	struct btrfs_chunk_map *prev_map = NULL;

	while (node) {
		map = rb_entry(node, struct btrfs_chunk_map, rb_node);
		prev = node;
		prev_map = map;

		if (logical < map->start) {
			node = node->rb_left;
		} else if (logical >= map->start + map->chunk_len) {
			node = node->rb_right;
		} else {
			refcount_inc(&map->refs);
			return map;
		}
	}

	if (!prev)
		return NULL;

	orig_prev = prev;
	while (prev && logical >= prev_map->start + prev_map->chunk_len) {
		prev = rb_next(prev);
		prev_map = rb_entry(prev, struct btrfs_chunk_map, rb_node);
	}

	if (!prev) {
		prev = orig_prev;
		prev_map = rb_entry(prev, struct btrfs_chunk_map, rb_node);
		while (prev && logical < prev_map->start) {
			prev = rb_prev(prev);
			prev_map = rb_entry(prev, struct btrfs_chunk_map, rb_node);
		}
	}

	if (prev) {
		u64 end = logical + length;

		/*
		 * Caller can pass a U64_MAX length when it wants to get any
		 * chunk starting at an offset of 'logical' or higher, so deal
		 * with underflow by resetting the end offset to U64_MAX.
		 */
		if (end < logical)
			end = U64_MAX;

		if (end > prev_map->start &&
		    logical < prev_map->start + prev_map->chunk_len) {
			refcount_inc(&prev_map->refs);
			return prev_map;
		}
	}

	return NULL;
}

struct btrfs_chunk_map *btrfs_find_chunk_map(struct btrfs_fs_info *fs_info,
					     u64 logical, u64 length)
{
	struct btrfs_chunk_map *map;

	read_lock(&fs_info->mapping_tree_lock);
	map = btrfs_find_chunk_map_nolock(fs_info, logical, length);
	read_unlock(&fs_info->mapping_tree_lock);

	return map;
}

/*
 * Find the mapping containing the given logical extent.
 *
 * @logical: Logical block offset in bytes.
 * @length: Length of extent in bytes.
 *
 * Return: Chunk mapping or ERR_PTR.
 */
struct btrfs_chunk_map *btrfs_get_chunk_map(struct btrfs_fs_info *fs_info,
					    u64 logical, u64 length)
{
	struct btrfs_chunk_map *map;

	map = btrfs_find_chunk_map(fs_info, logical, length);

	if (unlikely(!map)) {
		btrfs_crit(fs_info,
			   "unable to find chunk map for logical %llu length %llu",
			   logical, length);
		return ERR_PTR(-EINVAL);
	}

	if (unlikely(map->start > logical || map->start + map->chunk_len <= logical)) {
		btrfs_crit(fs_info,
			   "found a bad chunk map, wanted %llu-%llu, found %llu-%llu",
			   logical, logical + length, map->start,
			   map->start + map->chunk_len);
		btrfs_free_chunk_map(map);
		return ERR_PTR(-EINVAL);
	}

	/* Callers are responsible for dropping the reference. */
	return map;
}

static int remove_chunk_item(struct btrfs_trans_handle *trans,
			     struct btrfs_chunk_map *map, u64 chunk_offset)
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
	struct btrfs_chunk_map *map;
	u64 dev_extent_len = 0;
	int i, ret = 0;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;

	map = btrfs_get_chunk_map(fs_info, chunk_offset, 1);
	if (IS_ERR(map)) {
		/*
		 * This is a logic error, but we don't want to just rely on the
		 * user having built with ASSERT enabled, so if ASSERT doesn't
		 * do anything we still error out.
		 */
		DEBUG_WARN("errr %ld reading chunk map at offset %llu",
			   PTR_ERR(map), chunk_offset);
		return PTR_ERR(map);
	}

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
		if (unlikely(ret)) {
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

			if (list_empty(&device->post_commit_list)) {
				list_add_tail(&device->post_commit_list,
					      &trans->transaction->dev_update_list);
			}

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
		struct btrfs_space_info *space_info;

		space_info = btrfs_find_space_info(fs_info, sys_flags);
		if (unlikely(!space_info)) {
			ret = -EINVAL;
			btrfs_abort_transaction(trans, ret);
			goto out;
		}

		sys_bg = btrfs_create_chunk(trans, space_info, sys_flags);
		if (IS_ERR(sys_bg)) {
			ret = PTR_ERR(sys_bg);
			btrfs_abort_transaction(trans, ret);
			goto out;
		}

		ret = btrfs_chunk_alloc_add_chunk_item(trans, sys_bg);
		if (unlikely(ret)) {
			btrfs_abort_transaction(trans, ret);
			goto out;
		}

		ret = remove_chunk_item(trans, map, chunk_offset);
		if (unlikely(ret)) {
			btrfs_abort_transaction(trans, ret);
			goto out;
		}
	} else if (unlikely(ret)) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	trace_btrfs_chunk_free(fs_info, map, chunk_offset, map->chunk_len);

	if (map->type & BTRFS_BLOCK_GROUP_SYSTEM) {
		ret = btrfs_del_sys_chunk(fs_info, chunk_offset);
		if (unlikely(ret)) {
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

	ret = btrfs_remove_block_group(trans, map);
	if (unlikely(ret)) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

out:
	if (trans->removing_chunk) {
		mutex_unlock(&fs_info->chunk_mutex);
		trans->removing_chunk = false;
	}
	/* once for us */
	btrfs_free_chunk_map(map);
	return ret;
}

int btrfs_relocate_chunk(struct btrfs_fs_info *fs_info, u64 chunk_offset,
			 bool verbose)
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
	ret = btrfs_relocate_block_group(fs_info, chunk_offset, true);
	btrfs_scrub_continue(fs_info);
	if (ret) {
		/*
		 * If we had a transaction abort, stop all running scrubs.
		 * See transaction.c:cleanup_transaction() why we do it here.
		 */
		if (BTRFS_FS_ERROR(fs_info))
			btrfs_scrub_cancel(fs_info);
		return ret;
	}

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
	key.type = BTRFS_CHUNK_ITEM_KEY;
	key.offset = (u64)-1;

	while (1) {
		mutex_lock(&fs_info->reclaim_bgs_lock);
		ret = btrfs_search_slot(NULL, chunk_root, &key, path, 0, 0);
		if (ret < 0) {
			mutex_unlock(&fs_info->reclaim_bgs_lock);
			goto error;
		}
		if (unlikely(ret == 0)) {
			/*
			 * On the first search we would find chunk tree with
			 * offset -1, which is not possible. On subsequent
			 * loops this would find an existing item on an invalid
			 * offset (one less than the previous one, wrong
			 * alignment and size).
			 */
			ret = -EUCLEAN;
			mutex_unlock(&fs_info->reclaim_bgs_lock);
			goto error;
		}

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
			ret = btrfs_relocate_chunk(fs_info, found_key.offset,
						   true);
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

static void btrfs_disk_balance_args_to_cpu(struct btrfs_balance_args *cpu,
					   const struct btrfs_disk_balance_args *disk)
{
	memset(cpu, 0, sizeof(*cpu));

	cpu->profiles = le64_to_cpu(disk->profiles);
	cpu->usage = le64_to_cpu(disk->usage);
	cpu->devid = le64_to_cpu(disk->devid);
	cpu->pstart = le64_to_cpu(disk->pstart);
	cpu->pend = le64_to_cpu(disk->pend);
	cpu->vstart = le64_to_cpu(disk->vstart);
	cpu->vend = le64_to_cpu(disk->vend);
	cpu->target = le64_to_cpu(disk->target);
	cpu->flags = le64_to_cpu(disk->flags);
	cpu->limit = le64_to_cpu(disk->limit);
	cpu->stripes_min = le32_to_cpu(disk->stripes_min);
	cpu->stripes_max = le32_to_cpu(disk->stripes_max);
}

static void btrfs_cpu_balance_args_to_disk(struct btrfs_disk_balance_args *disk,
					   const struct btrfs_balance_args *cpu)
{
	memset(disk, 0, sizeof(*disk));

	disk->profiles = cpu_to_le64(cpu->profiles);
	disk->usage = cpu_to_le64(cpu->usage);
	disk->devid = cpu_to_le64(cpu->devid);
	disk->pstart = cpu_to_le64(cpu->pstart);
	disk->pend = cpu_to_le64(cpu->pend);
	disk->vstart = cpu_to_le64(cpu->vstart);
	disk->vend = cpu_to_le64(cpu->vend);
	disk->target = cpu_to_le64(cpu->target);
	disk->flags = cpu_to_le64(cpu->flags);
	disk->limit = cpu_to_le64(cpu->limit);
	disk->stripes_min = cpu_to_le32(cpu->stripes_min);
	disk->stripes_max = cpu_to_le32(cpu->stripes_max);
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

	ASSERT(fs_info->balance_ctl);

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
static bool chunk_profiles_filter(u64 chunk_type, struct btrfs_balance_args *bargs)
{
	chunk_type = chunk_to_extended(chunk_type) &
				BTRFS_EXTENDED_PROFILE_MASK;

	if (bargs->profiles & chunk_type)
		return false;

	return true;
}

static bool chunk_usage_range_filter(struct btrfs_fs_info *fs_info, u64 chunk_offset,
				     struct btrfs_balance_args *bargs)
{
	struct btrfs_block_group *cache;
	u64 chunk_used;
	u64 user_thresh_min;
	u64 user_thresh_max;
	bool ret = true;

	cache = btrfs_lookup_block_group(fs_info, chunk_offset);
	chunk_used = cache->used;

	if (bargs->usage_min == 0)
		user_thresh_min = 0;
	else
		user_thresh_min = mult_perc(cache->length, bargs->usage_min);

	if (bargs->usage_max == 0)
		user_thresh_max = 1;
	else if (bargs->usage_max > 100)
		user_thresh_max = cache->length;
	else
		user_thresh_max = mult_perc(cache->length, bargs->usage_max);

	if (user_thresh_min <= chunk_used && chunk_used < user_thresh_max)
		ret = false;

	btrfs_put_block_group(cache);
	return ret;
}

static bool chunk_usage_filter(struct btrfs_fs_info *fs_info, u64 chunk_offset,
			       struct btrfs_balance_args *bargs)
{
	struct btrfs_block_group *cache;
	u64 chunk_used, user_thresh;
	bool ret = true;

	cache = btrfs_lookup_block_group(fs_info, chunk_offset);
	chunk_used = cache->used;

	if (bargs->usage_min == 0)
		user_thresh = 1;
	else if (bargs->usage > 100)
		user_thresh = cache->length;
	else
		user_thresh = mult_perc(cache->length, bargs->usage);

	if (chunk_used < user_thresh)
		ret = false;

	btrfs_put_block_group(cache);
	return ret;
}

static bool chunk_devid_filter(struct extent_buffer *leaf, struct btrfs_chunk *chunk,
			       struct btrfs_balance_args *bargs)
{
	struct btrfs_stripe *stripe;
	int num_stripes = btrfs_chunk_num_stripes(leaf, chunk);
	int i;

	for (i = 0; i < num_stripes; i++) {
		stripe = btrfs_stripe_nr(chunk, i);
		if (btrfs_stripe_devid(leaf, stripe) == bargs->devid)
			return false;
	}

	return true;
}

static u64 calc_data_stripes(u64 type, int num_stripes)
{
	const int index = btrfs_bg_flags_to_raid_index(type);
	const int ncopies = btrfs_raid_array[index].ncopies;
	const int nparity = btrfs_raid_array[index].nparity;

	return (num_stripes - nparity) / ncopies;
}

/* [pstart, pend) */
static bool chunk_drange_filter(struct extent_buffer *leaf, struct btrfs_chunk *chunk,
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
		return false;

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
			return false;
	}

	return true;
}

/* [vstart, vend) */
static bool chunk_vrange_filter(struct extent_buffer *leaf, struct btrfs_chunk *chunk,
				u64 chunk_offset, struct btrfs_balance_args *bargs)
{
	if (chunk_offset < bargs->vend &&
	    chunk_offset + btrfs_chunk_length(leaf, chunk) > bargs->vstart)
		/* at least part of the chunk is inside this vrange */
		return false;

	return true;
}

static bool chunk_stripes_range_filter(struct extent_buffer *leaf,
				       struct btrfs_chunk *chunk,
				       struct btrfs_balance_args *bargs)
{
	int num_stripes = btrfs_chunk_num_stripes(leaf, chunk);

	if (bargs->stripes_min <= num_stripes
			&& num_stripes <= bargs->stripes_max)
		return false;

	return true;
}

static bool chunk_soft_convert_filter(u64 chunk_type, struct btrfs_balance_args *bargs)
{
	if (!(bargs->flags & BTRFS_BALANCE_ARGS_CONVERT))
		return false;

	chunk_type = chunk_to_extended(chunk_type) &
				BTRFS_EXTENDED_PROFILE_MASK;

	if (bargs->target == chunk_type)
		return true;

	return false;
}

static bool should_balance_chunk(struct extent_buffer *leaf, struct btrfs_chunk *chunk,
				 u64 chunk_offset)
{
	struct btrfs_fs_info *fs_info = leaf->fs_info;
	struct btrfs_balance_control *bctl = fs_info->balance_ctl;
	struct btrfs_balance_args *bargs = NULL;
	u64 chunk_type = btrfs_chunk_type(leaf, chunk);

	/* type filter */
	if (!((chunk_type & BTRFS_BLOCK_GROUP_TYPE_MASK) &
	      (bctl->flags & BTRFS_BALANCE_TYPE_MASK))) {
		return false;
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
		return false;
	}

	/* usage filter */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_USAGE) &&
	    chunk_usage_filter(fs_info, chunk_offset, bargs)) {
		return false;
	} else if ((bargs->flags & BTRFS_BALANCE_ARGS_USAGE_RANGE) &&
	    chunk_usage_range_filter(fs_info, chunk_offset, bargs)) {
		return false;
	}

	/* devid filter */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_DEVID) &&
	    chunk_devid_filter(leaf, chunk, bargs)) {
		return false;
	}

	/* drange filter, makes sense only with devid filter */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_DRANGE) &&
	    chunk_drange_filter(leaf, chunk, bargs)) {
		return false;
	}

	/* vrange filter */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_VRANGE) &&
	    chunk_vrange_filter(leaf, chunk, chunk_offset, bargs)) {
		return false;
	}

	/* stripes filter */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_STRIPES_RANGE) &&
	    chunk_stripes_range_filter(leaf, chunk, bargs)) {
		return false;
	}

	/* soft profile changing mode */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_SOFT) &&
	    chunk_soft_convert_filter(chunk_type, bargs)) {
		return false;
	}

	/*
	 * limited by count, must be the last filter
	 */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_LIMIT)) {
		if (bargs->limit == 0)
			return false;
		else
			bargs->limit--;
	} else if ((bargs->flags & BTRFS_BALANCE_ARGS_LIMIT_RANGE)) {
		/*
		 * Same logic as the 'limit' filter; the minimum cannot be
		 * determined here because we do not have the global information
		 * about the count of all chunks that satisfy the filters.
		 */
		if (bargs->limit_max == 0)
			return false;
		else
			bargs->limit_max--;
	}

	return true;
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
	key.type = BTRFS_CHUNK_ITEM_KEY;
	key.offset = (u64)-1;

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

		ret = btrfs_relocate_chunk(fs_info, found_key.offset, true);
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

/*
 * See if a given profile is valid and reduced.
 *
 * @flags:     profile to validate
 * @extended:  if true @flags is treated as an extended profile
 */
static int alloc_profile_is_valid(u64 flags, bool extended)
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
 * Should be called with balance mutex held
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
	bool paused = false;
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
		paused = true;
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

	/* We didn't pause, we can clean everything up. */
	if (!paused) {
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
	ASSERT(fs_info->exclusive_operation == BTRFS_EXCLOP_BALANCE_PAUSED,
	       "exclusive_operation=%d", fs_info->exclusive_operation);
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

	ASSERT(!test_bit(BTRFS_FS_BALANCE_RUNNING, &fs_info->flags));
	atomic_dec(&fs_info->balance_cancel_req);
	mutex_unlock(&fs_info->balance_mutex);
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
	u64 free_diff = 0;

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

		/*
		 * The new free_chunk_space is new_size - used, so we have to
		 * subtract the delta of the old free_chunk_space which included
		 * old_size - used.  If used > new_size then just subtract this
		 * entire device's free space.
		 */
		if (device->bytes_used < new_size)
			free_diff = (old_size - device->bytes_used) -
				    (new_size - device->bytes_used);
		else
			free_diff = old_size - device->bytes_used;
		atomic64_sub(free_diff, &fs_info->free_chunk_space);
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
	key.type = BTRFS_DEV_EXTENT_KEY;
	key.offset = (u64)-1;

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

		ret = btrfs_relocate_chunk(fs_info, chunk_offset, true);
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
	btrfs_clear_extent_bit(&device->alloc_state, new_size, (u64)-1,
			       CHUNK_STATE_MASK, NULL);

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
	if (unlikely(ret < 0)) {
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
		if (test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state)) {
			device->fs_devices->total_rw_bytes += diff;
			atomic64_add(free_diff, &fs_info->free_chunk_space);
		}
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
	/* Space_info the block group is going to belong. */
	struct btrfs_space_info *space_info;
};

static void init_alloc_chunk_ctl_policy_regular(
				struct btrfs_fs_devices *fs_devices,
				struct alloc_chunk_ctl *ctl)
{
	struct btrfs_space_info *space_info;

	space_info = btrfs_find_space_info(fs_devices->fs_info, ctl->type);
	ASSERT(space_info);

	ctl->max_chunk_size = READ_ONCE(space_info->chunk_size);
	ctl->max_stripe_size = min_t(u64, ctl->max_chunk_size, SZ_1G);

	if (ctl->type & BTRFS_BLOCK_GROUP_SYSTEM)
		ctl->devs_max = min_t(int, ctl->devs_max, BTRFS_MAX_DEVS_SYS_CHUNK);

	/* We don't want a chunk larger than 10% of writable space */
	ctl->max_chunk_size = min(mult_perc(fs_devices->total_rw_bytes, 10),
				  ctl->max_chunk_size);
	ctl->dev_extent_min = btrfs_stripe_nr_to_offset(ctl->dev_stripes);
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
	limit = max(round_down(mult_perc(fs_devices->total_rw_bytes, 10),
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
	default:
		btrfs_warn_unknown_chunk_allocation(fs_devices->chunk_alloc_policy);
		fallthrough;
	case BTRFS_CHUNK_ALLOC_REGULAR:
		init_alloc_chunk_ctl_policy_regular(fs_devices, ctl);
		break;
	case BTRFS_CHUNK_ALLOC_ZONED:
		init_alloc_chunk_ctl_policy_zoned(fs_devices, ctl);
		break;
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
	ASSERT(devices_info[ctl->ndevs - 1].max_avail == ctl->dev_extent_min,
	       "ndevs=%d max_avail=%llu dev_extent_min=%llu", ctl->ndevs,
	       devices_info[ctl->ndevs - 1].max_avail, ctl->dev_extent_min);

	ctl->stripe_size = zone_size;
	ctl->num_stripes = ctl->ndevs * ctl->dev_stripes;
	data_stripes = (ctl->num_stripes - ctl->nparity) / ctl->ncopies;

	/* stripe_size is fixed in zoned filesystem. Reduce ndevs instead. */
	if (ctl->stripe_size * data_stripes > ctl->max_chunk_size) {
		ctl->ndevs = div_u64(div_u64(ctl->max_chunk_size * ctl->ncopies,
					     ctl->stripe_size) + ctl->nparity,
				     ctl->dev_stripes);
		ctl->num_stripes = ctl->ndevs * ctl->dev_stripes;
		data_stripes = (ctl->num_stripes - ctl->nparity) / ctl->ncopies;
		ASSERT(ctl->stripe_size * data_stripes <= ctl->max_chunk_size,
		       "stripe_size=%llu data_stripes=%d max_chunk_size=%llu",
		       ctl->stripe_size, data_stripes, ctl->max_chunk_size);
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
	default:
		btrfs_warn_unknown_chunk_allocation(fs_devices->chunk_alloc_policy);
		fallthrough;
	case BTRFS_CHUNK_ALLOC_REGULAR:
		return decide_stripe_size_regular(ctl, devices_info);
	case BTRFS_CHUNK_ALLOC_ZONED:
		return decide_stripe_size_zoned(ctl, devices_info);
	}
}

static void chunk_map_device_set_bits(struct btrfs_chunk_map *map, unsigned int bits)
{
	for (int i = 0; i < map->num_stripes; i++) {
		struct btrfs_io_stripe *stripe = &map->stripes[i];
		struct btrfs_device *device = stripe->dev;

		btrfs_set_extent_bit(&device->alloc_state, stripe->physical,
				     stripe->physical + map->stripe_size - 1,
				     bits | EXTENT_NOWAIT, NULL);
	}
}

static void chunk_map_device_clear_bits(struct btrfs_chunk_map *map, unsigned int bits)
{
	for (int i = 0; i < map->num_stripes; i++) {
		struct btrfs_io_stripe *stripe = &map->stripes[i];
		struct btrfs_device *device = stripe->dev;

		btrfs_clear_extent_bit(&device->alloc_state, stripe->physical,
				       stripe->physical + map->stripe_size - 1,
				       bits | EXTENT_NOWAIT, NULL);
	}
}

void btrfs_remove_chunk_map(struct btrfs_fs_info *fs_info, struct btrfs_chunk_map *map)
{
	write_lock(&fs_info->mapping_tree_lock);
	rb_erase_cached(&map->rb_node, &fs_info->mapping_tree);
	RB_CLEAR_NODE(&map->rb_node);
	chunk_map_device_clear_bits(map, CHUNK_ALLOCATED);
	write_unlock(&fs_info->mapping_tree_lock);

	/* Once for the tree reference. */
	btrfs_free_chunk_map(map);
}

static int btrfs_chunk_map_cmp(const struct rb_node *new,
			       const struct rb_node *exist)
{
	const struct btrfs_chunk_map *new_map =
		rb_entry(new, struct btrfs_chunk_map, rb_node);
	const struct btrfs_chunk_map *exist_map =
		rb_entry(exist, struct btrfs_chunk_map, rb_node);

	if (new_map->start == exist_map->start)
		return 0;
	if (new_map->start < exist_map->start)
		return -1;
	return 1;
}

EXPORT_FOR_TESTS
int btrfs_add_chunk_map(struct btrfs_fs_info *fs_info, struct btrfs_chunk_map *map)
{
	struct rb_node *exist;

	write_lock(&fs_info->mapping_tree_lock);
	exist = rb_find_add_cached(&map->rb_node, &fs_info->mapping_tree,
				   btrfs_chunk_map_cmp);

	if (exist) {
		write_unlock(&fs_info->mapping_tree_lock);
		return -EEXIST;
	}
	chunk_map_device_set_bits(map, CHUNK_ALLOCATED);
	chunk_map_device_clear_bits(map, CHUNK_TRIMMED);
	write_unlock(&fs_info->mapping_tree_lock);

	return 0;
}

EXPORT_FOR_TESTS
struct btrfs_chunk_map *btrfs_alloc_chunk_map(int num_stripes, gfp_t gfp)
{
	struct btrfs_chunk_map *map;

	map = kmalloc(btrfs_chunk_map_size(num_stripes), gfp);
	if (!map)
		return NULL;

	refcount_set(&map->refs, 1);
	RB_CLEAR_NODE(&map->rb_node);

	return map;
}

static struct btrfs_block_group *create_chunk(struct btrfs_trans_handle *trans,
			struct alloc_chunk_ctl *ctl,
			struct btrfs_device_info *devices_info)
{
	struct btrfs_fs_info *info = trans->fs_info;
	struct btrfs_chunk_map *map;
	struct btrfs_block_group *block_group;
	u64 start = ctl->start;
	u64 type = ctl->type;
	int ret;

	map = btrfs_alloc_chunk_map(ctl->num_stripes, GFP_NOFS);
	if (!map)
		return ERR_PTR(-ENOMEM);

	map->start = start;
	map->chunk_len = ctl->chunk_size;
	map->stripe_size = ctl->stripe_size;
	map->type = type;
	map->io_align = BTRFS_STRIPE_LEN;
	map->io_width = BTRFS_STRIPE_LEN;
	map->sub_stripes = ctl->sub_stripes;
	map->num_stripes = ctl->num_stripes;

	for (int i = 0; i < ctl->ndevs; i++) {
		for (int j = 0; j < ctl->dev_stripes; j++) {
			int s = i * ctl->dev_stripes + j;
			map->stripes[s].dev = devices_info[i].dev;
			map->stripes[s].physical = devices_info[i].dev_offset +
						   j * ctl->stripe_size;
		}
	}

	trace_btrfs_chunk_alloc(info, map, start, ctl->chunk_size);

	ret = btrfs_add_chunk_map(info, map);
	if (ret) {
		btrfs_free_chunk_map(map);
		return ERR_PTR(ret);
	}

	block_group = btrfs_make_block_group(trans, ctl->space_info, type, start,
					     ctl->chunk_size);
	if (IS_ERR(block_group)) {
		btrfs_remove_chunk_map(info, map);
		return block_group;
	}

	for (int i = 0; i < map->num_stripes; i++) {
		struct btrfs_device *dev = map->stripes[i].dev;

		btrfs_device_set_bytes_used(dev,
					    dev->bytes_used + ctl->stripe_size);
		if (list_empty(&dev->post_commit_list))
			list_add_tail(&dev->post_commit_list,
				      &trans->transaction->dev_update_list);
	}

	atomic64_sub(ctl->stripe_size * map->num_stripes,
		     &info->free_chunk_space);

	check_raid56_incompat_flag(info, type);
	check_raid1c34_incompat_flag(info, type);

	return block_group;
}

struct btrfs_block_group *btrfs_create_chunk(struct btrfs_trans_handle *trans,
					     struct btrfs_space_info *space_info,
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
		DEBUG_WARN("invalid alloc profile for type %llu", type);
		return ERR_PTR(-EINVAL);
	}

	if (list_empty(&fs_devices->alloc_list)) {
		if (btrfs_test_opt(info, ENOSPC_DEBUG))
			btrfs_debug(info, "%s: no writable device", __func__);
		return ERR_PTR(-ENOSPC);
	}

	if (!(type & BTRFS_BLOCK_GROUP_TYPE_MASK)) {
		btrfs_err(info, "invalid chunk type 0x%llx requested", type);
		DEBUG_WARN();
		return ERR_PTR(-EINVAL);
	}

	ctl.start = find_next_chunk(info);
	ctl.type = type;
	ctl.space_info = space_info;
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
	struct btrfs_chunk_map *map;
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

	map = btrfs_get_chunk_map(fs_info, bg->start, bg->length);
	if (IS_ERR(map)) {
		ret = PTR_ERR(map);
		btrfs_abort_transaction(trans, ret);
		return ret;
	}

	item_size = btrfs_chunk_item_size(map->num_stripes);

	chunk = kzalloc(item_size, GFP_NOFS);
	if (unlikely(!chunk)) {
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
	btrfs_set_stack_chunk_stripe_len(chunk, BTRFS_STRIPE_LEN);
	btrfs_set_stack_chunk_type(chunk, map->type);
	btrfs_set_stack_chunk_num_stripes(chunk, map->num_stripes);
	btrfs_set_stack_chunk_io_align(chunk, BTRFS_STRIPE_LEN);
	btrfs_set_stack_chunk_io_width(chunk, BTRFS_STRIPE_LEN);
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
	btrfs_free_chunk_map(map);
	return ret;
}

static noinline int init_first_rw_device(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	u64 alloc_profile;
	struct btrfs_block_group *meta_bg;
	struct btrfs_space_info *meta_space_info;
	struct btrfs_block_group *sys_bg;
	struct btrfs_space_info *sys_space_info;

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
	meta_space_info = btrfs_find_space_info(fs_info, alloc_profile);
	if (!meta_space_info) {
		DEBUG_WARN();
		return -EINVAL;
	}
	meta_bg = btrfs_create_chunk(trans, meta_space_info, alloc_profile);
	if (IS_ERR(meta_bg))
		return PTR_ERR(meta_bg);

	alloc_profile = btrfs_system_alloc_profile(fs_info);
	sys_space_info = btrfs_find_space_info(fs_info, alloc_profile);
	if (!sys_space_info) {
		DEBUG_WARN();
		return -EINVAL;
	}
	sys_bg = btrfs_create_chunk(trans, sys_space_info, alloc_profile);
	if (IS_ERR(sys_bg))
		return PTR_ERR(sys_bg);

	return 0;
}

static inline int btrfs_chunk_max_errors(struct btrfs_chunk_map *map)
{
	const int index = btrfs_bg_flags_to_raid_index(map->type);

	return btrfs_raid_array[index].tolerated_failures;
}

bool btrfs_chunk_writeable(struct btrfs_fs_info *fs_info, u64 chunk_offset)
{
	struct btrfs_chunk_map *map;
	int miss_ndevs = 0;
	int i;
	bool ret = true;

	map = btrfs_get_chunk_map(fs_info, chunk_offset, 1);
	if (IS_ERR(map))
		return false;

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
	btrfs_free_chunk_map(map);
	return ret;
}

void btrfs_mapping_tree_free(struct btrfs_fs_info *fs_info)
{
	write_lock(&fs_info->mapping_tree_lock);
	while (!RB_EMPTY_ROOT(&fs_info->mapping_tree.rb_root)) {
		struct btrfs_chunk_map *map;
		struct rb_node *node;

		node = rb_first_cached(&fs_info->mapping_tree);
		map = rb_entry(node, struct btrfs_chunk_map, rb_node);
		rb_erase_cached(&map->rb_node, &fs_info->mapping_tree);
		RB_CLEAR_NODE(&map->rb_node);
		chunk_map_device_clear_bits(map, CHUNK_ALLOCATED);
		/* Once for the tree ref. */
		btrfs_free_chunk_map(map);
		cond_resched_rwlock_write(&fs_info->mapping_tree_lock);
	}
	write_unlock(&fs_info->mapping_tree_lock);
}

static int btrfs_chunk_map_num_copies(const struct btrfs_chunk_map *map)
{
	enum btrfs_raid_types index = btrfs_bg_flags_to_raid_index(map->type);

	if (map->type & BTRFS_BLOCK_GROUP_RAID5)
		return 2;

	/*
	 * There could be two corrupted data stripes, we need to loop retry in
	 * order to rebuild the correct data.
	 *
	 * Fail a stripe at a time on every retry except the stripe under
	 * reconstruction.
	 */
	if (map->type & BTRFS_BLOCK_GROUP_RAID6)
		return map->num_stripes;

	/* Non-RAID56, use their ncopies from btrfs_raid_array. */
	return btrfs_raid_array[index].ncopies;
}

int btrfs_num_copies(struct btrfs_fs_info *fs_info, u64 logical, u64 len)
{
	struct btrfs_chunk_map *map;
	int ret;

	map = btrfs_get_chunk_map(fs_info, logical, len);
	if (IS_ERR(map))
		/*
		 * We could return errors for these cases, but that could get
		 * ugly and we'd probably do the same thing which is just not do
		 * anything else and exit, so return 1 so the callers don't try
		 * to use other copies.
		 */
		return 1;

	ret = btrfs_chunk_map_num_copies(map);
	btrfs_free_chunk_map(map);
	return ret;
}

unsigned long btrfs_full_stripe_len(struct btrfs_fs_info *fs_info,
				    u64 logical)
{
	struct btrfs_chunk_map *map;
	unsigned long len = fs_info->sectorsize;

	if (!btrfs_fs_incompat(fs_info, RAID56))
		return len;

	map = btrfs_get_chunk_map(fs_info, logical, len);

	if (!WARN_ON(IS_ERR(map))) {
		if (map->type & BTRFS_BLOCK_GROUP_RAID56_MASK)
			len = btrfs_stripe_nr_to_offset(nr_data_stripes(map));
		btrfs_free_chunk_map(map);
	}
	return len;
}

#ifdef CONFIG_BTRFS_EXPERIMENTAL
static int btrfs_read_preferred(struct btrfs_chunk_map *map, int first, int num_stripes)
{
	for (int index = first; index < first + num_stripes; index++) {
		const struct btrfs_device *device = map->stripes[index].dev;

		if (device->devid == READ_ONCE(device->fs_devices->read_devid))
			return index;
	}

	/* If no read-preferred device is set use the first stripe. */
	return first;
}

struct stripe_mirror {
	u64 devid;
	int num;
};

static int btrfs_cmp_devid(const void *a, const void *b)
{
	const struct stripe_mirror *s1 = (const struct stripe_mirror *)a;
	const struct stripe_mirror *s2 = (const struct stripe_mirror *)b;

	if (s1->devid < s2->devid)
		return -1;
	if (s1->devid > s2->devid)
		return 1;
	return 0;
}

/*
 * Select a stripe for reading using the round-robin algorithm.
 *
 *  1. Compute the read cycle as the total sectors read divided by the minimum
 *     sectors per device.
 *  2. Determine the stripe number for the current read by taking the modulus
 *     of the read cycle with the total number of stripes:
 *
 *      stripe index = (total sectors / min sectors per dev) % num stripes
 *
 * The calculated stripe index is then used to select the corresponding device
 * from the list of devices, which is ordered by devid.
 */
static int btrfs_read_rr(const struct btrfs_chunk_map *map, int first, int num_stripes)
{
	struct stripe_mirror stripes[BTRFS_RAID1_MAX_MIRRORS] = { 0 };
	struct btrfs_device *device  = map->stripes[first].dev;
	struct btrfs_fs_info *fs_info = device->fs_devices->fs_info;
	unsigned int read_cycle;
	unsigned int total_reads;
	unsigned int min_reads_per_dev;

	total_reads = percpu_counter_sum(&fs_info->stats_read_blocks);
	min_reads_per_dev = READ_ONCE(fs_info->fs_devices->rr_min_contig_read) >>
						       fs_info->sectorsize_bits;

	for (int index = 0, i = first; i < first + num_stripes; i++) {
		stripes[index].devid = map->stripes[i].dev->devid;
		stripes[index].num = i;
		index++;
	}
	sort(stripes, num_stripes, sizeof(struct stripe_mirror),
	     btrfs_cmp_devid, NULL);

	read_cycle = total_reads / min_reads_per_dev;
	return stripes[read_cycle % num_stripes].num;
}
#endif

static int find_live_mirror(struct btrfs_fs_info *fs_info,
			    struct btrfs_chunk_map *map, int first,
			    bool dev_replace_is_ongoing)
{
	const enum btrfs_read_policy policy = READ_ONCE(fs_info->fs_devices->read_policy);
	int i;
	int num_stripes;
	int preferred_mirror;
	int tolerance;
	struct btrfs_device *srcdev;

	ASSERT((map->type & (BTRFS_BLOCK_GROUP_RAID1_MASK | BTRFS_BLOCK_GROUP_RAID10)),
	       "type=%llu", map->type);

	if (map->type & BTRFS_BLOCK_GROUP_RAID10)
		num_stripes = map->sub_stripes;
	else
		num_stripes = map->num_stripes;

	switch (policy) {
	default:
		/* Shouldn't happen, just warn and use pid instead of failing */
		btrfs_warn_rl(fs_info, "unknown read_policy type %u, reset to pid",
			      policy);
		WRITE_ONCE(fs_info->fs_devices->read_policy, BTRFS_READ_POLICY_PID);
		fallthrough;
	case BTRFS_READ_POLICY_PID:
		preferred_mirror = first + (current->pid % num_stripes);
		break;
#ifdef CONFIG_BTRFS_EXPERIMENTAL
	case BTRFS_READ_POLICY_RR:
		preferred_mirror = btrfs_read_rr(map, first, num_stripes);
		break;
	case BTRFS_READ_POLICY_DEVID:
		preferred_mirror = btrfs_read_preferred(map, first, num_stripes);
		break;
#endif
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

EXPORT_FOR_TESTS
struct btrfs_io_context *alloc_btrfs_io_context(struct btrfs_fs_info *fs_info,
						u64 logical, u16 total_stripes)
{
	struct btrfs_io_context *bioc;

	bioc = kzalloc(
		 /* The size of btrfs_io_context */
		sizeof(struct btrfs_io_context) +
		/* Plus the variable array for the stripes */
		sizeof(struct btrfs_io_stripe) * (total_stripes),
		GFP_NOFS);

	if (!bioc)
		return NULL;

	refcount_set(&bioc->refs, 1);

	bioc->fs_info = fs_info;
	bioc->replace_stripe_src = -1;
	bioc->full_stripe_logical = (u64)-1;
	bioc->logical = logical;

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
	struct btrfs_chunk_map *map;
	struct btrfs_discard_stripe *stripes;
	u64 length = *length_ret;
	u64 offset;
	u32 stripe_nr;
	u32 stripe_nr_end;
	u32 stripe_cnt;
	u64 stripe_end_offset;
	u64 stripe_offset;
	u32 stripe_index;
	u32 factor = 0;
	u32 sub_stripes = 0;
	u32 stripes_per_dev = 0;
	u32 remaining_stripes = 0;
	u32 last_stripe = 0;
	int ret;
	int i;

	map = btrfs_get_chunk_map(fs_info, logical, length);
	if (IS_ERR(map))
		return ERR_CAST(map);

	/* we don't discard raid56 yet */
	if (map->type & BTRFS_BLOCK_GROUP_RAID56_MASK) {
		ret = -EOPNOTSUPP;
		goto out_free_map;
	}

	offset = logical - map->start;
	length = min_t(u64, map->start + map->chunk_len - logical, length);
	*length_ret = length;

	/*
	 * stripe_nr counts the total number of stripes we have to stride
	 * to get to this block
	 */
	stripe_nr = offset >> BTRFS_STRIPE_LEN_SHIFT;

	/* stripe_offset is the offset of this block in its stripe */
	stripe_offset = offset - btrfs_stripe_nr_to_offset(stripe_nr);

	stripe_nr_end = round_up(offset + length, BTRFS_STRIPE_LEN) >>
			BTRFS_STRIPE_LEN_SHIFT;
	stripe_cnt = stripe_nr_end - stripe_nr;
	stripe_end_offset = btrfs_stripe_nr_to_offset(stripe_nr_end) -
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
		stripe_index = stripe_nr % factor;
		stripe_nr /= factor;
		stripe_index *= sub_stripes;

		remaining_stripes = stripe_cnt % factor;
		stripes_per_dev = stripe_cnt / factor;
		last_stripe = ((stripe_nr_end - 1) % factor) * sub_stripes;
	} else if (map->type & (BTRFS_BLOCK_GROUP_RAID1_MASK |
				BTRFS_BLOCK_GROUP_DUP)) {
		*num_stripes = map->num_stripes;
	} else {
		stripe_index = stripe_nr % map->num_stripes;
		stripe_nr /= map->num_stripes;
	}

	stripes = kcalloc(*num_stripes, sizeof(*stripes), GFP_NOFS);
	if (!stripes) {
		ret = -ENOMEM;
		goto out_free_map;
	}

	for (i = 0; i < *num_stripes; i++) {
		stripes[i].physical =
			map->stripes[stripe_index].physical +
			stripe_offset + btrfs_stripe_nr_to_offset(stripe_nr);
		stripes[i].dev = map->stripes[stripe_index].dev;

		if (map->type & (BTRFS_BLOCK_GROUP_RAID0 |
				 BTRFS_BLOCK_GROUP_RAID10)) {
			stripes[i].length = btrfs_stripe_nr_to_offset(stripes_per_dev);

			if (i / sub_stripes < remaining_stripes)
				stripes[i].length += BTRFS_STRIPE_LEN;

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

	btrfs_free_chunk_map(map);
	return stripes;
out_free_map:
	btrfs_free_chunk_map(map);
	return ERR_PTR(ret);
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

static void handle_ops_on_dev_replace(struct btrfs_io_context *bioc,
				      struct btrfs_dev_replace *dev_replace,
				      u64 logical,
				      struct btrfs_io_geometry *io_geom)
{
	u64 srcdev_devid = dev_replace->srcdev->devid;
	/*
	 * At this stage, num_stripes is still the real number of stripes,
	 * excluding the duplicated stripes.
	 */
	int num_stripes = io_geom->num_stripes;
	int max_errors = io_geom->max_errors;
	int nr_extra_stripes = 0;
	int i;

	/*
	 * A block group which has "to_copy" set will eventually be copied by
	 * the dev-replace process. We can avoid cloning IO here.
	 */
	if (is_block_group_to_copy(dev_replace->srcdev->fs_info, logical))
		return;

	/*
	 * Duplicate the write operations while the dev-replace procedure is
	 * running. Since the copying of the old disk to the new disk takes
	 * place at run time while the filesystem is mounted writable, the
	 * regular write operations to the old disk have to be duplicated to go
	 * to the new disk as well.
	 *
	 * Note that device->missing is handled by the caller, and that the
	 * write to the old disk is already set up in the stripes array.
	 */
	for (i = 0; i < num_stripes; i++) {
		struct btrfs_io_stripe *old = &bioc->stripes[i];
		struct btrfs_io_stripe *new = &bioc->stripes[num_stripes + nr_extra_stripes];

		if (old->dev->devid != srcdev_devid)
			continue;

		new->physical = old->physical;
		new->dev = dev_replace->tgtdev;
		if (bioc->map_type & BTRFS_BLOCK_GROUP_RAID56_MASK)
			bioc->replace_stripe_src = i;
		nr_extra_stripes++;
	}

	/* We can only have at most 2 extra nr_stripes (for DUP). */
	ASSERT(nr_extra_stripes <= 2, "nr_extra_stripes=%d", nr_extra_stripes);
	/*
	 * For GET_READ_MIRRORS, we can only return at most 1 extra stripe for
	 * replace.
	 * If we have 2 extra stripes, only choose the one with smaller physical.
	 */
	if (io_geom->op == BTRFS_MAP_GET_READ_MIRRORS && nr_extra_stripes == 2) {
		struct btrfs_io_stripe *first = &bioc->stripes[num_stripes];
		struct btrfs_io_stripe *second = &bioc->stripes[num_stripes + 1];

		/* Only DUP can have two extra stripes. */
		ASSERT(bioc->map_type & BTRFS_BLOCK_GROUP_DUP,
		       "map_type=%llu", bioc->map_type);

		/*
		 * Swap the last stripe stripes and reduce @nr_extra_stripes.
		 * The extra stripe would still be there, but won't be accessed.
		 */
		if (first->physical > second->physical) {
			swap(second->physical, first->physical);
			swap(second->dev, first->dev);
			nr_extra_stripes--;
		}
	}

	io_geom->num_stripes = num_stripes + nr_extra_stripes;
	io_geom->max_errors = max_errors + nr_extra_stripes;
	bioc->replace_nr_stripes = nr_extra_stripes;
}

static u64 btrfs_max_io_len(struct btrfs_chunk_map *map, u64 offset,
			    struct btrfs_io_geometry *io_geom)
{
	/*
	 * Stripe_nr is the stripe where this block falls.  stripe_offset is
	 * the offset of this block in its stripe.
	 */
	io_geom->stripe_offset = offset & BTRFS_STRIPE_LEN_MASK;
	io_geom->stripe_nr = offset >> BTRFS_STRIPE_LEN_SHIFT;
	ASSERT(io_geom->stripe_offset < U32_MAX,
	       "stripe_offset=%llu", io_geom->stripe_offset);

	if (map->type & BTRFS_BLOCK_GROUP_RAID56_MASK) {
		unsigned long full_stripe_len =
			btrfs_stripe_nr_to_offset(nr_data_stripes(map));

		/*
		 * For full stripe start, we use previously calculated
		 * @stripe_nr. Align it to nr_data_stripes, then multiply with
		 * STRIPE_LEN.
		 *
		 * By this we can avoid u64 division completely.  And we have
		 * to go rounddown(), not round_down(), as nr_data_stripes is
		 * not ensured to be power of 2.
		 */
		io_geom->raid56_full_stripe_start = btrfs_stripe_nr_to_offset(
			rounddown(io_geom->stripe_nr, nr_data_stripes(map)));

		ASSERT(io_geom->raid56_full_stripe_start + full_stripe_len > offset,
		       "raid56_full_stripe_start=%llu full_stripe_len=%lu offset=%llu",
		       io_geom->raid56_full_stripe_start, full_stripe_len, offset);
		ASSERT(io_geom->raid56_full_stripe_start <= offset,
		       "raid56_full_stripe_start=%llu offset=%llu",
		       io_geom->raid56_full_stripe_start, offset);
		/*
		 * For writes to RAID56, allow to write a full stripe set, but
		 * no straddling of stripe sets.
		 */
		if (io_geom->op == BTRFS_MAP_WRITE)
			return full_stripe_len - (offset - io_geom->raid56_full_stripe_start);
	}

	/*
	 * For other RAID types and for RAID56 reads, allow a single stripe (on
	 * a single disk).
	 */
	if (map->type & BTRFS_BLOCK_GROUP_STRIPE_MASK)
		return BTRFS_STRIPE_LEN - io_geom->stripe_offset;
	return U64_MAX;
}

static int set_io_stripe(struct btrfs_fs_info *fs_info, u64 logical,
			 u64 *length, struct btrfs_io_stripe *dst,
			 struct btrfs_chunk_map *map,
			 struct btrfs_io_geometry *io_geom)
{
	dst->dev = map->stripes[io_geom->stripe_index].dev;

	if (io_geom->op == BTRFS_MAP_READ && io_geom->use_rst)
		return btrfs_get_raid_extent_offset(fs_info, logical, length,
						    map->type,
						    io_geom->stripe_index, dst);

	dst->physical = map->stripes[io_geom->stripe_index].physical +
			io_geom->stripe_offset +
			btrfs_stripe_nr_to_offset(io_geom->stripe_nr);
	return 0;
}

static bool is_single_device_io(struct btrfs_fs_info *fs_info,
				const struct btrfs_io_stripe *smap,
				const struct btrfs_chunk_map *map,
				int num_alloc_stripes,
				struct btrfs_io_geometry *io_geom)
{
	if (!smap)
		return false;

	if (num_alloc_stripes != 1)
		return false;

	if (io_geom->use_rst && io_geom->op != BTRFS_MAP_READ)
		return false;

	if ((map->type & BTRFS_BLOCK_GROUP_RAID56_MASK) && io_geom->mirror_num > 1)
		return false;

	return true;
}

static void map_blocks_raid0(const struct btrfs_chunk_map *map,
			     struct btrfs_io_geometry *io_geom)
{
	io_geom->stripe_index = io_geom->stripe_nr % map->num_stripes;
	io_geom->stripe_nr /= map->num_stripes;
	if (io_geom->op == BTRFS_MAP_READ)
		io_geom->mirror_num = 1;
}

static void map_blocks_raid1(struct btrfs_fs_info *fs_info,
			     struct btrfs_chunk_map *map,
			     struct btrfs_io_geometry *io_geom,
			     bool dev_replace_is_ongoing)
{
	if (io_geom->op != BTRFS_MAP_READ) {
		io_geom->num_stripes = map->num_stripes;
		return;
	}

	if (io_geom->mirror_num) {
		io_geom->stripe_index = io_geom->mirror_num - 1;
		return;
	}

	io_geom->stripe_index = find_live_mirror(fs_info, map, 0,
						 dev_replace_is_ongoing);
	io_geom->mirror_num = io_geom->stripe_index + 1;
}

static void map_blocks_dup(const struct btrfs_chunk_map *map,
			   struct btrfs_io_geometry *io_geom)
{
	if (io_geom->op != BTRFS_MAP_READ) {
		io_geom->num_stripes = map->num_stripes;
		return;
	}

	if (io_geom->mirror_num) {
		io_geom->stripe_index = io_geom->mirror_num - 1;
		return;
	}

	io_geom->mirror_num = 1;
}

static void map_blocks_raid10(struct btrfs_fs_info *fs_info,
			      struct btrfs_chunk_map *map,
			      struct btrfs_io_geometry *io_geom,
			      bool dev_replace_is_ongoing)
{
	u32 factor = map->num_stripes / map->sub_stripes;
	int old_stripe_index;

	io_geom->stripe_index = (io_geom->stripe_nr % factor) * map->sub_stripes;
	io_geom->stripe_nr /= factor;

	if (io_geom->op != BTRFS_MAP_READ) {
		io_geom->num_stripes = map->sub_stripes;
		return;
	}

	if (io_geom->mirror_num) {
		io_geom->stripe_index += io_geom->mirror_num - 1;
		return;
	}

	old_stripe_index = io_geom->stripe_index;
	io_geom->stripe_index = find_live_mirror(fs_info, map,
						 io_geom->stripe_index,
						 dev_replace_is_ongoing);
	io_geom->mirror_num = io_geom->stripe_index - old_stripe_index + 1;
}

static void map_blocks_raid56_write(struct btrfs_chunk_map *map,
				    struct btrfs_io_geometry *io_geom,
				    u64 logical, u64 *length)
{
	int data_stripes = nr_data_stripes(map);

	/*
	 * Needs full stripe mapping.
	 *
	 * Push stripe_nr back to the start of the full stripe For those cases
	 * needing a full stripe, @stripe_nr is the full stripe number.
	 *
	 * Originally we go raid56_full_stripe_start / full_stripe_len, but
	 * that can be expensive.  Here we just divide @stripe_nr with
	 * @data_stripes.
	 */
	io_geom->stripe_nr /= data_stripes;

	/* RAID[56] write or recovery. Return all stripes */
	io_geom->num_stripes = map->num_stripes;
	io_geom->max_errors = btrfs_chunk_max_errors(map);

	/* Return the length to the full stripe end. */
	*length = min(logical + *length,
		      io_geom->raid56_full_stripe_start + map->start +
		      btrfs_stripe_nr_to_offset(data_stripes)) -
		logical;
	io_geom->stripe_index = 0;
	io_geom->stripe_offset = 0;
}

static void map_blocks_raid56_read(struct btrfs_chunk_map *map,
				   struct btrfs_io_geometry *io_geom)
{
	int data_stripes = nr_data_stripes(map);

	ASSERT(io_geom->mirror_num <= 1, "mirror_num=%d", io_geom->mirror_num);
	/* Just grab the data stripe directly. */
	io_geom->stripe_index = io_geom->stripe_nr % data_stripes;
	io_geom->stripe_nr /= data_stripes;

	/* We distribute the parity blocks across stripes. */
	io_geom->stripe_index =
		(io_geom->stripe_nr + io_geom->stripe_index) % map->num_stripes;

	if (io_geom->op == BTRFS_MAP_READ && io_geom->mirror_num < 1)
		io_geom->mirror_num = 1;
}

static void map_blocks_single(const struct btrfs_chunk_map *map,
			      struct btrfs_io_geometry *io_geom)
{
	io_geom->stripe_index = io_geom->stripe_nr % map->num_stripes;
	io_geom->stripe_nr /= map->num_stripes;
	io_geom->mirror_num = io_geom->stripe_index + 1;
}

/*
 * Map one logical range to one or more physical ranges.
 *
 * @length:		(Mandatory) mapped length of this run.
 *			One logical range can be split into different segments
 *			due to factors like zones and RAID0/5/6/10 stripe
 *			boundaries.
 *
 * @bioc_ret:		(Mandatory) returned btrfs_io_context structure.
 *			which has one or more physical ranges (btrfs_io_stripe)
 *			recorded inside.
 *			Caller should call btrfs_put_bioc() to free it after use.
 *
 * @smap:		(Optional) single physical range optimization.
 *			If the map request can be fulfilled by one single
 *			physical range, and this is parameter is not NULL,
 *			then @bioc_ret would be NULL, and @smap would be
 *			updated.
 *
 * @mirror_num_ret:	(Mandatory) returned mirror number if the original
 *			value is 0.
 *
 *			Mirror number 0 means to choose any live mirrors.
 *
 *			For non-RAID56 profiles, non-zero mirror_num means
 *			the Nth mirror. (e.g. mirror_num 1 means the first
 *			copy).
 *
 *			For RAID56 profile, mirror 1 means rebuild from P and
 *			the remaining data stripes.
 *
 *			For RAID6 profile, mirror > 2 means mark another
 *			data/P stripe error and rebuild from the remaining
 *			stripes..
 */
int btrfs_map_block(struct btrfs_fs_info *fs_info, enum btrfs_map_op op,
		    u64 logical, u64 *length,
		    struct btrfs_io_context **bioc_ret,
		    struct btrfs_io_stripe *smap, int *mirror_num_ret)
{
	struct btrfs_chunk_map *map;
	struct btrfs_io_geometry io_geom = { 0 };
	u64 map_offset;
	int ret = 0;
	int num_copies;
	struct btrfs_io_context *bioc = NULL;
	struct btrfs_dev_replace *dev_replace = &fs_info->dev_replace;
	bool dev_replace_is_ongoing = false;
	u16 num_alloc_stripes;
	u64 max_len;

	ASSERT(bioc_ret);

	io_geom.mirror_num = (mirror_num_ret ? *mirror_num_ret : 0);
	io_geom.num_stripes = 1;
	io_geom.stripe_index = 0;
	io_geom.op = op;

	map = btrfs_get_chunk_map(fs_info, logical, *length);
	if (IS_ERR(map))
		return PTR_ERR(map);

	num_copies = btrfs_chunk_map_num_copies(map);
	if (io_geom.mirror_num > num_copies)
		return -EINVAL;

	map_offset = logical - map->start;
	io_geom.raid56_full_stripe_start = (u64)-1;
	max_len = btrfs_max_io_len(map, map_offset, &io_geom);
	*length = min_t(u64, map->chunk_len - map_offset, max_len);
	io_geom.use_rst = btrfs_need_stripe_tree_update(fs_info, map->type);

	if (dev_replace->replace_task != current)
		down_read(&dev_replace->rwsem);

	dev_replace_is_ongoing = btrfs_dev_replace_is_ongoing(dev_replace);
	/*
	 * Hold the semaphore for read during the whole operation, write is
	 * requested at commit time but must wait.
	 */
	if (!dev_replace_is_ongoing && dev_replace->replace_task != current)
		up_read(&dev_replace->rwsem);

	switch (map->type & BTRFS_BLOCK_GROUP_PROFILE_MASK) {
	case BTRFS_BLOCK_GROUP_RAID0:
		map_blocks_raid0(map, &io_geom);
		break;
	case BTRFS_BLOCK_GROUP_RAID1:
	case BTRFS_BLOCK_GROUP_RAID1C3:
	case BTRFS_BLOCK_GROUP_RAID1C4:
		map_blocks_raid1(fs_info, map, &io_geom, dev_replace_is_ongoing);
		break;
	case BTRFS_BLOCK_GROUP_DUP:
		map_blocks_dup(map, &io_geom);
		break;
	case BTRFS_BLOCK_GROUP_RAID10:
		map_blocks_raid10(fs_info, map, &io_geom, dev_replace_is_ongoing);
		break;
	case BTRFS_BLOCK_GROUP_RAID5:
	case BTRFS_BLOCK_GROUP_RAID6:
		if (op != BTRFS_MAP_READ || io_geom.mirror_num > 1)
			map_blocks_raid56_write(map, &io_geom, logical, length);
		else
			map_blocks_raid56_read(map, &io_geom);
		break;
	default:
		/*
		 * After this, stripe_nr is the number of stripes on this
		 * device we have to walk to find the data, and stripe_index is
		 * the number of our device in the stripe array
		 */
		map_blocks_single(map, &io_geom);
		break;
	}
	if (io_geom.stripe_index >= map->num_stripes) {
		btrfs_crit(fs_info,
			   "stripe index math went horribly wrong, got stripe_index=%u, num_stripes=%u",
			   io_geom.stripe_index, map->num_stripes);
		ret = -EINVAL;
		goto out;
	}

	num_alloc_stripes = io_geom.num_stripes;
	if (dev_replace_is_ongoing && dev_replace->tgtdev != NULL &&
	    op != BTRFS_MAP_READ)
		/*
		 * For replace case, we need to add extra stripes for extra
		 * duplicated stripes.
		 *
		 * For both WRITE and GET_READ_MIRRORS, we may have at most
		 * 2 more stripes (DUP types, otherwise 1).
		 */
		num_alloc_stripes += 2;

	/*
	 * If this I/O maps to a single device, try to return the device and
	 * physical block information on the stack instead of allocating an
	 * I/O context structure.
	 */
	if (is_single_device_io(fs_info, smap, map, num_alloc_stripes, &io_geom)) {
		ret = set_io_stripe(fs_info, logical, length, smap, map, &io_geom);
		if (mirror_num_ret)
			*mirror_num_ret = io_geom.mirror_num;
		*bioc_ret = NULL;
		goto out;
	}

	bioc = alloc_btrfs_io_context(fs_info, logical, num_alloc_stripes);
	if (!bioc) {
		ret = -ENOMEM;
		goto out;
	}
	bioc->map_type = map->type;
	bioc->use_rst = io_geom.use_rst;

	/*
	 * For RAID56 full map, we need to make sure the stripes[] follows the
	 * rule that data stripes are all ordered, then followed with P and Q
	 * (if we have).
	 *
	 * It's still mostly the same as other profiles, just with extra rotation.
	 */
	if (map->type & BTRFS_BLOCK_GROUP_RAID56_MASK &&
	    (op != BTRFS_MAP_READ || io_geom.mirror_num > 1)) {
		/*
		 * For RAID56 @stripe_nr is already the number of full stripes
		 * before us, which is also the rotation value (needs to modulo
		 * with num_stripes).
		 *
		 * In this case, we just add @stripe_nr with @i, then do the
		 * modulo, to reduce one modulo call.
		 */
		bioc->full_stripe_logical = map->start +
			btrfs_stripe_nr_to_offset(io_geom.stripe_nr *
						  nr_data_stripes(map));
		for (int i = 0; i < io_geom.num_stripes; i++) {
			struct btrfs_io_stripe *dst = &bioc->stripes[i];
			u32 stripe_index;

			stripe_index = (i + io_geom.stripe_nr) % io_geom.num_stripes;
			dst->dev = map->stripes[stripe_index].dev;
			dst->physical =
				map->stripes[stripe_index].physical +
				io_geom.stripe_offset +
				btrfs_stripe_nr_to_offset(io_geom.stripe_nr);
		}
	} else {
		/*
		 * For all other non-RAID56 profiles, just copy the target
		 * stripe into the bioc.
		 */
		for (int i = 0; i < io_geom.num_stripes; i++) {
			ret = set_io_stripe(fs_info, logical, length,
					    &bioc->stripes[i], map, &io_geom);
			if (ret < 0)
				break;
			io_geom.stripe_index++;
		}
	}

	if (ret) {
		*bioc_ret = NULL;
		btrfs_put_bioc(bioc);
		goto out;
	}

	if (op != BTRFS_MAP_READ)
		io_geom.max_errors = btrfs_chunk_max_errors(map);

	if (dev_replace_is_ongoing && dev_replace->tgtdev != NULL &&
	    op != BTRFS_MAP_READ) {
		handle_ops_on_dev_replace(bioc, dev_replace, logical, &io_geom);
	}

	*bioc_ret = bioc;
	bioc->num_stripes = io_geom.num_stripes;
	bioc->max_errors = io_geom.max_errors;
	bioc->mirror_num = io_geom.mirror_num;

out:
	if (dev_replace_is_ongoing && dev_replace->replace_task != current) {
		lockdep_assert_held(&dev_replace->rwsem);
		/* Unlock and let waiting writers proceed */
		up_read(&dev_replace->rwsem);
	}
	btrfs_free_chunk_map(map);
	return ret;
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
	if (args->missing) {
		if (test_bit(BTRFS_DEV_STATE_IN_FS_METADATA, &device->dev_state) &&
		    !device->bdev)
			return true;
		return false;
	}

	if (device->devid != args->devid)
		return false;
	if (args->uuid && memcmp(device->uuid, args->uuid, BTRFS_UUID_SIZE) != 0)
		return false;
	return true;
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
	device = btrfs_alloc_device(NULL, &devid, dev_uuid, NULL);
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

/*
 * Allocate new device struct, set up devid and UUID.
 *
 * @fs_info:	used only for generating a new devid, can be NULL if
 *		devid is provided (i.e. @devid != NULL).
 * @devid:	a pointer to devid for this device.  If NULL a new devid
 *		is generated.
 * @uuid:	a pointer to UUID for this device.  If NULL a new UUID
 *		is generated.
 * @path:	a pointer to device path if available, NULL otherwise.
 *
 * Return: a pointer to a new &struct btrfs_device on success; ERR_PTR()
 * on error.  Returned struct is not linked onto any lists and must be
 * destroyed with btrfs_free_device.
 */
struct btrfs_device *btrfs_alloc_device(struct btrfs_fs_info *fs_info,
					const u64 *devid, const u8 *uuid,
					const char *path)
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
	btrfs_extent_io_tree_init(fs_info, &dev->alloc_state, IO_TREE_DEVICE_ALLOC_STATE);

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

	if (path) {
		const char *name;

		name = kstrdup(path, GFP_KERNEL);
		if (!name) {
			btrfs_free_device(dev);
			return ERR_PTR(-ENOMEM);
		}
		rcu_assign_pointer(dev->name, name);
	}

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

u64 btrfs_calc_stripe_length(const struct btrfs_chunk_map *map)
{
	const int data_stripes = calc_data_stripes(map->type, map->num_stripes);

	return div_u64(map->chunk_len, data_stripes);
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
	struct btrfs_chunk_map *map;
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

	map = btrfs_find_chunk_map(fs_info, logical, 1);

	/* already mapped? */
	if (map && map->start <= logical && map->start + map->chunk_len > logical) {
		btrfs_free_chunk_map(map);
		return 0;
	} else if (map) {
		btrfs_free_chunk_map(map);
	}

	map = btrfs_alloc_chunk_map(num_stripes, GFP_NOFS);
	if (!map)
		return -ENOMEM;

	map->start = logical;
	map->chunk_len = length;
	map->num_stripes = num_stripes;
	map->io_width = btrfs_chunk_io_width(leaf, chunk);
	map->io_align = btrfs_chunk_io_align(leaf, chunk);
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
	map->stripe_size = btrfs_calc_stripe_length(map);
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
				ret = PTR_ERR(map->stripes[i].dev);
				btrfs_free_chunk_map(map);
				return ret;
			}
		}

		set_bit(BTRFS_DEV_STATE_IN_FS_METADATA,
				&(map->stripes[i].dev->dev_state));
	}

	ret = btrfs_add_chunk_map(fs_info, map);
	if (ret < 0) {
		btrfs_err(fs_info,
			  "failed to add chunk map, start=%llu len=%llu: %d",
			  map->start, map->chunk_len, ret);
		btrfs_free_chunk_map(map);
	}

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
		if (!btrfs_test_opt(fs_info, DEGRADED)) {
			btrfs_err(fs_info,
		"failed to find fsid %pU when attempting to open seed devices",
				  fsid);
			return ERR_PTR(-ENOENT);
		}

		fs_devices = alloc_fs_devices(fsid);
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

	ret = open_fs_devices(fs_devices, BLK_OPEN_READ, fs_info->sb);
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
	u8 *array_ptr;
	unsigned long sb_array_offset;
	int ret = 0;
	u32 array_size;
	u32 cur_offset;
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
		struct btrfs_chunk *chunk;
		struct btrfs_disk_key *disk_key = (struct btrfs_disk_key *)array_ptr;
		u32 len = sizeof(*disk_key);

		/*
		 * The sys_chunk_array has been already verified at super block
		 * read time.  Only do ASSERT()s for basic checks.
		 */
		ASSERT(cur_offset + len <= array_size);

		btrfs_disk_key_to_cpu(&key, disk_key);

		array_ptr += len;
		sb_array_offset += len;
		cur_offset += len;

		ASSERT(key.type == BTRFS_CHUNK_ITEM_KEY);

		chunk = (struct btrfs_chunk *)sb_array_offset;
		ASSERT(btrfs_chunk_type(sb, chunk) & BTRFS_BLOCK_GROUP_SYSTEM);

		len = btrfs_chunk_item_size(btrfs_chunk_num_stripes(sb, chunk));

		ASSERT(cur_offset + len <= array_size);

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
	struct btrfs_chunk_map *map;
	u64 next_start;
	bool ret = true;

	map = btrfs_find_chunk_map(fs_info, 0, U64_MAX);
	/* No chunk at all? Return false anyway */
	if (!map) {
		ret = false;
		goto out;
	}
	while (map) {
		int missing = 0;
		int max_tolerated;
		int i;

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
				   map->start, missing, max_tolerated);
			btrfs_free_chunk_map(map);
			ret = false;
			goto out;
		}
		next_start = map->start + map->chunk_len;
		btrfs_free_chunk_map(map);

		map = btrfs_find_chunk_map(fs_info, next_start, U64_MAX - next_start);
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
	 * used for freeze protection of a fs (struct super_block.s_writers),
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
	key.type = 0;
	key.offset = 0;
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

int btrfs_init_devices_late(struct btrfs_fs_info *fs_info)
{
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices, *seed_devs;
	struct btrfs_device *device;
	int ret = 0;

	mutex_lock(&fs_devices->device_list_mutex);
	list_for_each_entry(device, &fs_devices->devices, dev_list)
		device->fs_info = fs_info;

	list_for_each_entry(seed_devs, &fs_devices->seed_list, seed_list) {
		list_for_each_entry(device, &seed_devs->devices, dev_list) {
			device->fs_info = fs_info;
			ret = btrfs_get_dev_zone_info(device, false);
			if (ret)
				break;
		}

		seed_devs->fs_info = fs_info;
	}
	mutex_unlock(&fs_devices->device_list_mutex);

	return ret;
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
		btrfs_warn(fs_info,
			"error %d while searching for dev_stats item for device %s",
				  ret, btrfs_dev_name(device));
		goto out;
	}

	if (ret == 0 &&
	    btrfs_item_size(path->nodes[0], path->slots[0]) < sizeof(*ptr)) {
		/* need to delete old one and insert a new one */
		ret = btrfs_del_item(trans, dev_root, path);
		if (ret != 0) {
			btrfs_warn(fs_info,
				"delete too small dev_stats item for device %s failed %d",
					  btrfs_dev_name(device), ret);
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
			btrfs_warn(fs_info,
				"insert dev_stats item for device %s failed %d",
				btrfs_dev_name(device), ret);
			goto out;
		}
	}

	eb = path->nodes[0];
	ptr = btrfs_item_ptr(eb, path->slots[0], struct btrfs_dev_stats_item);
	for (i = 0; i < BTRFS_DEV_STAT_VALUES_MAX; i++)
		btrfs_set_dev_stats_value(eb, ptr, i,
					  btrfs_dev_stat_read(device, i));
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
	btrfs_err_rl(dev->fs_info,
		"bdev %s errs: wr %u, rd %u, flush %u, corrupt %u, gen %u",
			   btrfs_dev_name(dev),
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

	btrfs_info(dev->fs_info,
		"bdev %s errs: wr %u, rd %u, flush %u, corrupt %u, gen %u",
	       btrfs_dev_name(dev),
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

	ASSERT(trans->state == TRANS_STATE_COMMIT_DOING, "state=%d" , trans->state);

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
	struct btrfs_chunk_map *map;
	struct btrfs_device *dev;
	u64 stripe_len;
	bool found = false;
	int ret = 0;
	int i;

	map = btrfs_find_chunk_map(fs_info, chunk_offset, 1);
	if (unlikely(!map)) {
		btrfs_err(fs_info,
"dev extent physical offset %llu on devid %llu doesn't have corresponding chunk",
			  physical_offset, devid);
		ret = -EUCLEAN;
		goto out;
	}

	stripe_len = btrfs_calc_stripe_length(map);
	if (unlikely(physical_len != stripe_len)) {
		btrfs_err(fs_info,
"dev extent physical offset %llu on devid %llu length doesn't match chunk %llu, have %llu expect %llu",
			  physical_offset, devid, map->start, physical_len,
			  stripe_len);
		ret = -EUCLEAN;
		goto out;
	}

	/*
	 * Very old mkfs.btrfs (before v4.15) will not respect the reserved
	 * space. Although kernel can handle it without problem, better to warn
	 * the users.
	 */
	if (physical_offset < BTRFS_DEVICE_RANGE_RESERVED)
		btrfs_warn(fs_info,
		"devid %llu physical %llu len %llu inside the reserved space",
			   devid, physical_offset, physical_len);

	for (i = 0; i < map->num_stripes; i++) {
		if (unlikely(map->stripes[i].dev->devid == devid &&
			     map->stripes[i].physical == physical_offset)) {
			found = true;
			if (map->verified_stripes >= map->num_stripes) {
				btrfs_err(fs_info,
				"too many dev extents for chunk %llu found",
					  map->start);
				ret = -EUCLEAN;
				goto out;
			}
			map->verified_stripes++;
			break;
		}
	}
	if (unlikely(!found)) {
		btrfs_err(fs_info,
	"dev extent physical offset %llu devid %llu has no corresponding chunk",
			physical_offset, devid);
		ret = -EUCLEAN;
	}

	/* Make sure no dev extent is beyond device boundary */
	dev = btrfs_find_device(fs_info->fs_devices, &args);
	if (unlikely(!dev)) {
		btrfs_err(fs_info, "failed to find devid %llu", devid);
		ret = -EUCLEAN;
		goto out;
	}

	if (unlikely(physical_offset + physical_len > dev->disk_total_bytes)) {
		btrfs_err(fs_info,
"dev extent devid %llu physical offset %llu len %llu is beyond device boundary %llu",
			  devid, physical_offset, physical_len,
			  dev->disk_total_bytes);
		ret = -EUCLEAN;
		goto out;
	}

	if (dev->zone_info) {
		u64 zone_size = dev->zone_info->zone_size;

		if (unlikely(!IS_ALIGNED(physical_offset, zone_size) ||
			     !IS_ALIGNED(physical_len, zone_size))) {
			btrfs_err(fs_info,
"zoned: dev extent devid %llu physical offset %llu len %llu is not aligned to device zone",
				  devid, physical_offset, physical_len);
			ret = -EUCLEAN;
			goto out;
		}
	}

out:
	btrfs_free_chunk_map(map);
	return ret;
}

static int verify_chunk_dev_extent_mapping(struct btrfs_fs_info *fs_info)
{
	struct rb_node *node;
	int ret = 0;

	read_lock(&fs_info->mapping_tree_lock);
	for (node = rb_first_cached(&fs_info->mapping_tree); node; node = rb_next(node)) {
		struct btrfs_chunk_map *map;

		map = rb_entry(node, struct btrfs_chunk_map, rb_node);
		if (unlikely(map->num_stripes != map->verified_stripes)) {
			btrfs_err(fs_info,
			"chunk %llu has missing dev extent, have %d expect %d",
				  map->start, map->verified_stripes, map->num_stripes);
			ret = -EUCLEAN;
			goto out;
		}
	}
out:
	read_unlock(&fs_info->mapping_tree_lock);
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
		if (unlikely(ret > 0)) {
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
		if (unlikely(devid == prev_devid && physical_offset < prev_dev_ext_end)) {
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
	ret = btrfs_relocate_chunk(fs_info, target, true);

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

static void map_raid56_repair_block(struct btrfs_io_context *bioc,
				    struct btrfs_io_stripe *smap,
				    u64 logical)
{
	int data_stripes = nr_bioc_data_stripes(bioc);
	int i;

	for (i = 0; i < data_stripes; i++) {
		u64 stripe_start = bioc->full_stripe_logical +
				   btrfs_stripe_nr_to_offset(i);

		if (logical >= stripe_start &&
		    logical < stripe_start + BTRFS_STRIPE_LEN)
			break;
	}
	ASSERT(i < data_stripes, "i=%d data_stripes=%d", i, data_stripes);
	smap->dev = bioc->stripes[i].dev;
	smap->physical = bioc->stripes[i].physical +
			((logical - bioc->full_stripe_logical) &
			 BTRFS_STRIPE_LEN_MASK);
}

/*
 * Map a repair write into a single device.
 *
 * A repair write is triggered by read time repair or scrub, which would only
 * update the contents of a single device.
 * Not update any other mirrors nor go through RMW path.
 *
 * Callers should ensure:
 *
 * - Call btrfs_bio_counter_inc_blocked() first
 * - The range does not cross stripe boundary
 * - Has a valid @mirror_num passed in.
 */
int btrfs_map_repair_block(struct btrfs_fs_info *fs_info,
			   struct btrfs_io_stripe *smap, u64 logical,
			   u32 length, int mirror_num)
{
	struct btrfs_io_context *bioc = NULL;
	u64 map_length = length;
	int mirror_ret = mirror_num;
	int ret;

	ASSERT(mirror_num > 0, "mirror_num=%d", mirror_num);

	ret = btrfs_map_block(fs_info, BTRFS_MAP_WRITE, logical, &map_length,
			      &bioc, smap, &mirror_ret);
	if (ret < 0)
		return ret;

	/* The map range should not cross stripe boundary. */
	ASSERT(map_length >= length, "map_length=%llu length=%u", map_length, length);

	/* Already mapped to single stripe. */
	if (!bioc)
		goto out;

	/* Map the RAID56 multi-stripe writes to a single one. */
	if (bioc->map_type & BTRFS_BLOCK_GROUP_RAID56_MASK) {
		map_raid56_repair_block(bioc, smap, logical);
		goto out;
	}

	ASSERT(mirror_num <= bioc->num_stripes,
	       "mirror_num=%d num_stripes=%d", mirror_num,  bioc->num_stripes);
	smap->dev = bioc->stripes[mirror_num - 1].dev;
	smap->physical = bioc->stripes[mirror_num - 1].physical;
out:
	btrfs_put_bioc(bioc);
	ASSERT(smap->dev);
	return 0;
}

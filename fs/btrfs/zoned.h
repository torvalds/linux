/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_ZONED_H
#define BTRFS_ZONED_H

#include <linux/types.h>
#include <linux/blkdev.h>
#include "volumes.h"
#include "disk-io.h"
#include "block-group.h"

struct btrfs_zoned_device_info {
	/*
	 * Number of zones, zone size and types of zones if bdev is a
	 * zoned block device.
	 */
	u64 zone_size;
	u8  zone_size_shift;
	u64 max_zone_append_size;
	u32 nr_zones;
	unsigned long *seq_zones;
	unsigned long *empty_zones;
	struct blk_zone sb_zones[2 * BTRFS_SUPER_MIRROR_MAX];
};

#ifdef CONFIG_BLK_DEV_ZONED
int btrfs_get_dev_zone(struct btrfs_device *device, u64 pos,
		       struct blk_zone *zone);
int btrfs_get_dev_zone_info_all_devices(struct btrfs_fs_info *fs_info);
int btrfs_get_dev_zone_info(struct btrfs_device *device);
void btrfs_destroy_dev_zone_info(struct btrfs_device *device);
int btrfs_check_zoned_mode(struct btrfs_fs_info *fs_info);
int btrfs_check_mountopts_zoned(struct btrfs_fs_info *info);
int btrfs_sb_log_location_bdev(struct block_device *bdev, int mirror, int rw,
			       u64 *bytenr_ret);
int btrfs_sb_log_location(struct btrfs_device *device, int mirror, int rw,
			  u64 *bytenr_ret);
void btrfs_advance_sb_log(struct btrfs_device *device, int mirror);
int btrfs_reset_sb_log_zones(struct block_device *bdev, int mirror);
u64 btrfs_find_allocatable_zones(struct btrfs_device *device, u64 hole_start,
				 u64 hole_end, u64 num_bytes);
int btrfs_reset_device_zone(struct btrfs_device *device, u64 physical,
			    u64 length, u64 *bytes);
int btrfs_ensure_empty_zones(struct btrfs_device *device, u64 start, u64 size);
int btrfs_load_block_group_zone_info(struct btrfs_block_group *cache, bool new);
void btrfs_calc_zone_unusable(struct btrfs_block_group *cache);
void btrfs_redirty_list_add(struct btrfs_transaction *trans,
			    struct extent_buffer *eb);
void btrfs_free_redirty_list(struct btrfs_transaction *trans);
bool btrfs_use_zone_append(struct btrfs_inode *inode, struct extent_map *em);
void btrfs_record_physical_zoned(struct inode *inode, u64 file_offset,
				 struct bio *bio);
void btrfs_rewrite_logical_zoned(struct btrfs_ordered_extent *ordered);
bool btrfs_check_meta_write_pointer(struct btrfs_fs_info *fs_info,
				    struct extent_buffer *eb,
				    struct btrfs_block_group **cache_ret);
void btrfs_revert_meta_write_pointer(struct btrfs_block_group *cache,
				     struct extent_buffer *eb);
int btrfs_zoned_issue_zeroout(struct btrfs_device *device, u64 physical, u64 length);
int btrfs_sync_zone_write_pointer(struct btrfs_device *tgt_dev, u64 logical,
				  u64 physical_start, u64 physical_pos);
#else /* CONFIG_BLK_DEV_ZONED */
static inline int btrfs_get_dev_zone(struct btrfs_device *device, u64 pos,
				     struct blk_zone *zone)
{
	return 0;
}

static inline int btrfs_get_dev_zone_info_all_devices(struct btrfs_fs_info *fs_info)
{
	return 0;
}

static inline int btrfs_get_dev_zone_info(struct btrfs_device *device)
{
	return 0;
}

static inline void btrfs_destroy_dev_zone_info(struct btrfs_device *device) { }

static inline int btrfs_check_zoned_mode(const struct btrfs_fs_info *fs_info)
{
	if (!btrfs_is_zoned(fs_info))
		return 0;

	btrfs_err(fs_info, "zoned block devices support is not enabled");
	return -EOPNOTSUPP;
}

static inline int btrfs_check_mountopts_zoned(struct btrfs_fs_info *info)
{
	return 0;
}

static inline int btrfs_sb_log_location_bdev(struct block_device *bdev,
					     int mirror, int rw, u64 *bytenr_ret)
{
	*bytenr_ret = btrfs_sb_offset(mirror);
	return 0;
}

static inline int btrfs_sb_log_location(struct btrfs_device *device, int mirror,
					int rw, u64 *bytenr_ret)
{
	*bytenr_ret = btrfs_sb_offset(mirror);
	return 0;
}

static inline void btrfs_advance_sb_log(struct btrfs_device *device, int mirror)
{ }

static inline int btrfs_reset_sb_log_zones(struct block_device *bdev, int mirror)
{
	return 0;
}

static inline u64 btrfs_find_allocatable_zones(struct btrfs_device *device,
					       u64 hole_start, u64 hole_end,
					       u64 num_bytes)
{
	return hole_start;
}

static inline int btrfs_reset_device_zone(struct btrfs_device *device,
					  u64 physical, u64 length, u64 *bytes)
{
	*bytes = 0;
	return 0;
}

static inline int btrfs_ensure_empty_zones(struct btrfs_device *device,
					   u64 start, u64 size)
{
	return 0;
}

static inline int btrfs_load_block_group_zone_info(
		struct btrfs_block_group *cache, bool new)
{
	return 0;
}

static inline void btrfs_calc_zone_unusable(struct btrfs_block_group *cache) { }

static inline void btrfs_redirty_list_add(struct btrfs_transaction *trans,
					  struct extent_buffer *eb) { }
static inline void btrfs_free_redirty_list(struct btrfs_transaction *trans) { }

static inline bool btrfs_use_zone_append(struct btrfs_inode *inode,
					 struct extent_map *em)
{
	return false;
}

static inline void btrfs_record_physical_zoned(struct inode *inode,
					       u64 file_offset, struct bio *bio)
{
}

static inline void btrfs_rewrite_logical_zoned(
				struct btrfs_ordered_extent *ordered) { }

static inline bool btrfs_check_meta_write_pointer(struct btrfs_fs_info *fs_info,
			       struct extent_buffer *eb,
			       struct btrfs_block_group **cache_ret)
{
	return true;
}

static inline void btrfs_revert_meta_write_pointer(
						struct btrfs_block_group *cache,
						struct extent_buffer *eb)
{
}

static inline int btrfs_zoned_issue_zeroout(struct btrfs_device *device,
					    u64 physical, u64 length)
{
	return -EOPNOTSUPP;
}

static inline int btrfs_sync_zone_write_pointer(struct btrfs_device *tgt_dev,
						u64 logical, u64 physical_start,
						u64 physical_pos)
{
	return -EOPNOTSUPP;
}

#endif

static inline bool btrfs_dev_is_sequential(struct btrfs_device *device, u64 pos)
{
	struct btrfs_zoned_device_info *zone_info = device->zone_info;

	if (!zone_info)
		return false;

	return test_bit(pos >> zone_info->zone_size_shift, zone_info->seq_zones);
}

static inline bool btrfs_dev_is_empty_zone(struct btrfs_device *device, u64 pos)
{
	struct btrfs_zoned_device_info *zone_info = device->zone_info;

	if (!zone_info)
		return true;

	return test_bit(pos >> zone_info->zone_size_shift, zone_info->empty_zones);
}

static inline void btrfs_dev_set_empty_zone_bit(struct btrfs_device *device,
						u64 pos, bool set)
{
	struct btrfs_zoned_device_info *zone_info = device->zone_info;
	unsigned int zno;

	if (!zone_info)
		return;

	zno = pos >> zone_info->zone_size_shift;
	if (set)
		set_bit(zno, zone_info->empty_zones);
	else
		clear_bit(zno, zone_info->empty_zones);
}

static inline void btrfs_dev_set_zone_empty(struct btrfs_device *device, u64 pos)
{
	btrfs_dev_set_empty_zone_bit(device, pos, true);
}

static inline void btrfs_dev_clear_zone_empty(struct btrfs_device *device, u64 pos)
{
	btrfs_dev_set_empty_zone_bit(device, pos, false);
}

static inline bool btrfs_check_device_zone_type(const struct btrfs_fs_info *fs_info,
						struct block_device *bdev)
{
	if (btrfs_is_zoned(fs_info)) {
		/*
		 * We can allow a regular device on a zoned filesystem, because
		 * we will emulate the zoned capabilities.
		 */
		if (!bdev_is_zoned(bdev))
			return true;

		return fs_info->zone_size ==
			(bdev_zone_sectors(bdev) << SECTOR_SHIFT);
	}

	/* Do not allow Host Manged zoned device */
	return bdev_zoned_model(bdev) != BLK_ZONED_HM;
}

static inline bool btrfs_check_super_location(struct btrfs_device *device, u64 pos)
{
	/*
	 * On a non-zoned device, any address is OK. On a zoned device,
	 * non-SEQUENTIAL WRITE REQUIRED zones are capable.
	 */
	return device->zone_info == NULL || !btrfs_dev_is_sequential(device, pos);
}

static inline bool btrfs_can_zone_reset(struct btrfs_device *device,
					u64 physical, u64 length)
{
	u64 zone_size;

	if (!btrfs_dev_is_sequential(device, physical))
		return false;

	zone_size = device->zone_info->zone_size;
	if (!IS_ALIGNED(physical, zone_size) || !IS_ALIGNED(length, zone_size))
		return false;

	return true;
}

static inline void btrfs_zoned_meta_io_lock(struct btrfs_fs_info *fs_info)
{
	if (!btrfs_is_zoned(fs_info))
		return;
	mutex_lock(&fs_info->zoned_meta_io_lock);
}

static inline void btrfs_zoned_meta_io_unlock(struct btrfs_fs_info *fs_info)
{
	if (!btrfs_is_zoned(fs_info))
		return;
	mutex_unlock(&fs_info->zoned_meta_io_lock);
}

static inline void btrfs_clear_treelog_bg(struct btrfs_block_group *bg)
{
	struct btrfs_fs_info *fs_info = bg->fs_info;

	if (!btrfs_is_zoned(fs_info))
		return;

	spin_lock(&fs_info->treelog_bg_lock);
	if (fs_info->treelog_bg == bg->start)
		fs_info->treelog_bg = 0;
	spin_unlock(&fs_info->treelog_bg_lock);
}

#endif

/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_ZONED_H
#define BTRFS_ZONED_H

#include <linux/types.h>
#include <linux/blkdev.h>
#include "volumes.h"
#include "disk-io.h"

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
#else /* CONFIG_BLK_DEV_ZONED */
static inline int btrfs_get_dev_zone(struct btrfs_device *device, u64 pos,
				     struct blk_zone *zone)
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
	u64 zone_size;

	if (btrfs_is_zoned(fs_info)) {
		zone_size = bdev_zone_sectors(bdev) << SECTOR_SHIFT;
		/* Do not allow non-zoned device */
		return bdev_is_zoned(bdev) && fs_info->zone_size == zone_size;
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

#endif

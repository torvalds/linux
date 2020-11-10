/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_ZONED_H
#define BTRFS_ZONED_H

#include <linux/types.h>

struct btrfs_zoned_device_info {
	/*
	 * Number of zones, zone size and types of zones if bdev is a
	 * zoned block device.
	 */
	u64 zone_size;
	u8  zone_size_shift;
	u32 nr_zones;
	unsigned long *seq_zones;
	unsigned long *empty_zones;
};

#ifdef CONFIG_BLK_DEV_ZONED
int btrfs_get_dev_zone(struct btrfs_device *device, u64 pos,
		       struct blk_zone *zone);
int btrfs_get_dev_zone_info(struct btrfs_device *device);
void btrfs_destroy_dev_zone_info(struct btrfs_device *device);
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

#endif

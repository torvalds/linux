// SPDX-License-Identifier: GPL-2.0

#include <linux/slab.h>
#include <linux/blkdev.h>
#include "ctree.h"
#include "volumes.h"
#include "zoned.h"
#include "rcu-string.h"

/* Maximum number of zones to report per blkdev_report_zones() call */
#define BTRFS_REPORT_NR_ZONES   4096

/* Number of superblock log zones */
#define BTRFS_NR_SB_LOG_ZONES 2

static int copy_zone_info_cb(struct blk_zone *zone, unsigned int idx, void *data)
{
	struct blk_zone *zones = data;

	memcpy(&zones[idx], zone, sizeof(*zone));

	return 0;
}

static int sb_write_pointer(struct block_device *bdev, struct blk_zone *zones,
			    u64 *wp_ret)
{
	bool empty[BTRFS_NR_SB_LOG_ZONES];
	bool full[BTRFS_NR_SB_LOG_ZONES];
	sector_t sector;

	ASSERT(zones[0].type != BLK_ZONE_TYPE_CONVENTIONAL &&
	       zones[1].type != BLK_ZONE_TYPE_CONVENTIONAL);

	empty[0] = (zones[0].cond == BLK_ZONE_COND_EMPTY);
	empty[1] = (zones[1].cond == BLK_ZONE_COND_EMPTY);
	full[0] = (zones[0].cond == BLK_ZONE_COND_FULL);
	full[1] = (zones[1].cond == BLK_ZONE_COND_FULL);

	/*
	 * Possible states of log buffer zones
	 *
	 *           Empty[0]  In use[0]  Full[0]
	 * Empty[1]         *          x        0
	 * In use[1]        0          x        0
	 * Full[1]          1          1        C
	 *
	 * Log position:
	 *   *: Special case, no superblock is written
	 *   0: Use write pointer of zones[0]
	 *   1: Use write pointer of zones[1]
	 *   C: Compare super blcoks from zones[0] and zones[1], use the latest
	 *      one determined by generation
	 *   x: Invalid state
	 */

	if (empty[0] && empty[1]) {
		/* Special case to distinguish no superblock to read */
		*wp_ret = zones[0].start << SECTOR_SHIFT;
		return -ENOENT;
	} else if (full[0] && full[1]) {
		/* Compare two super blocks */
		struct address_space *mapping = bdev->bd_inode->i_mapping;
		struct page *page[BTRFS_NR_SB_LOG_ZONES];
		struct btrfs_super_block *super[BTRFS_NR_SB_LOG_ZONES];
		int i;

		for (i = 0; i < BTRFS_NR_SB_LOG_ZONES; i++) {
			u64 bytenr;

			bytenr = ((zones[i].start + zones[i].len)
				   << SECTOR_SHIFT) - BTRFS_SUPER_INFO_SIZE;

			page[i] = read_cache_page_gfp(mapping,
					bytenr >> PAGE_SHIFT, GFP_NOFS);
			if (IS_ERR(page[i])) {
				if (i == 1)
					btrfs_release_disk_super(super[0]);
				return PTR_ERR(page[i]);
			}
			super[i] = page_address(page[i]);
		}

		if (super[0]->generation > super[1]->generation)
			sector = zones[1].start;
		else
			sector = zones[0].start;

		for (i = 0; i < BTRFS_NR_SB_LOG_ZONES; i++)
			btrfs_release_disk_super(super[i]);
	} else if (!full[0] && (empty[1] || full[1])) {
		sector = zones[0].wp;
	} else if (full[0]) {
		sector = zones[1].wp;
	} else {
		return -EUCLEAN;
	}
	*wp_ret = sector << SECTOR_SHIFT;
	return 0;
}

/*
 * The following zones are reserved as the circular buffer on ZONED btrfs.
 *  - The primary superblock: zones 0 and 1
 *  - The first copy: zones 16 and 17
 *  - The second copy: zones 1024 or zone at 256GB which is minimum, and
 *                     the following one
 */
static inline u32 sb_zone_number(int shift, int mirror)
{
	ASSERT(mirror < BTRFS_SUPER_MIRROR_MAX);

	switch (mirror) {
	case 0: return 0;
	case 1: return 16;
	case 2: return min_t(u64, btrfs_sb_offset(mirror) >> shift, 1024);
	}

	return 0;
}

static int btrfs_get_dev_zones(struct btrfs_device *device, u64 pos,
			       struct blk_zone *zones, unsigned int *nr_zones)
{
	int ret;

	if (!*nr_zones)
		return 0;

	ret = blkdev_report_zones(device->bdev, pos >> SECTOR_SHIFT, *nr_zones,
				  copy_zone_info_cb, zones);
	if (ret < 0) {
		btrfs_err_in_rcu(device->fs_info,
				 "zoned: failed to read zone %llu on %s (devid %llu)",
				 pos, rcu_str_deref(device->name),
				 device->devid);
		return ret;
	}
	*nr_zones = ret;
	if (!ret)
		return -EIO;

	return 0;
}

int btrfs_get_dev_zone_info(struct btrfs_device *device)
{
	struct btrfs_zoned_device_info *zone_info = NULL;
	struct block_device *bdev = device->bdev;
	struct request_queue *queue = bdev_get_queue(bdev);
	sector_t nr_sectors;
	sector_t sector = 0;
	struct blk_zone *zones = NULL;
	unsigned int i, nreported = 0, nr_zones;
	unsigned int zone_sectors;
	int ret;

	if (!bdev_is_zoned(bdev))
		return 0;

	if (device->zone_info)
		return 0;

	zone_info = kzalloc(sizeof(*zone_info), GFP_KERNEL);
	if (!zone_info)
		return -ENOMEM;

	nr_sectors = bdev_nr_sectors(bdev);
	zone_sectors = bdev_zone_sectors(bdev);
	/* Check if it's power of 2 (see is_power_of_2) */
	ASSERT(zone_sectors != 0 && (zone_sectors & (zone_sectors - 1)) == 0);
	zone_info->zone_size = zone_sectors << SECTOR_SHIFT;
	zone_info->zone_size_shift = ilog2(zone_info->zone_size);
	zone_info->max_zone_append_size =
		(u64)queue_max_zone_append_sectors(queue) << SECTOR_SHIFT;
	zone_info->nr_zones = nr_sectors >> ilog2(zone_sectors);
	if (!IS_ALIGNED(nr_sectors, zone_sectors))
		zone_info->nr_zones++;

	zone_info->seq_zones = bitmap_zalloc(zone_info->nr_zones, GFP_KERNEL);
	if (!zone_info->seq_zones) {
		ret = -ENOMEM;
		goto out;
	}

	zone_info->empty_zones = bitmap_zalloc(zone_info->nr_zones, GFP_KERNEL);
	if (!zone_info->empty_zones) {
		ret = -ENOMEM;
		goto out;
	}

	zones = kcalloc(BTRFS_REPORT_NR_ZONES, sizeof(struct blk_zone), GFP_KERNEL);
	if (!zones) {
		ret = -ENOMEM;
		goto out;
	}

	/* Get zones type */
	while (sector < nr_sectors) {
		nr_zones = BTRFS_REPORT_NR_ZONES;
		ret = btrfs_get_dev_zones(device, sector << SECTOR_SHIFT, zones,
					  &nr_zones);
		if (ret)
			goto out;

		for (i = 0; i < nr_zones; i++) {
			if (zones[i].type == BLK_ZONE_TYPE_SEQWRITE_REQ)
				__set_bit(nreported, zone_info->seq_zones);
			if (zones[i].cond == BLK_ZONE_COND_EMPTY)
				__set_bit(nreported, zone_info->empty_zones);
			nreported++;
		}
		sector = zones[nr_zones - 1].start + zones[nr_zones - 1].len;
	}

	if (nreported != zone_info->nr_zones) {
		btrfs_err_in_rcu(device->fs_info,
				 "inconsistent number of zones on %s (%u/%u)",
				 rcu_str_deref(device->name), nreported,
				 zone_info->nr_zones);
		ret = -EIO;
		goto out;
	}

	/* Validate superblock log */
	nr_zones = BTRFS_NR_SB_LOG_ZONES;
	for (i = 0; i < BTRFS_SUPER_MIRROR_MAX; i++) {
		u32 sb_zone;
		u64 sb_wp;
		int sb_pos = BTRFS_NR_SB_LOG_ZONES * i;

		sb_zone = sb_zone_number(zone_info->zone_size_shift, i);
		if (sb_zone + 1 >= zone_info->nr_zones)
			continue;

		sector = sb_zone << (zone_info->zone_size_shift - SECTOR_SHIFT);
		ret = btrfs_get_dev_zones(device, sector << SECTOR_SHIFT,
					  &zone_info->sb_zones[sb_pos],
					  &nr_zones);
		if (ret)
			goto out;

		if (nr_zones != BTRFS_NR_SB_LOG_ZONES) {
			btrfs_err_in_rcu(device->fs_info,
	"zoned: failed to read super block log zone info at devid %llu zone %u",
					 device->devid, sb_zone);
			ret = -EUCLEAN;
			goto out;
		}

		/*
		 * If zones[0] is conventional, always use the beggining of the
		 * zone to record superblock. No need to validate in that case.
		 */
		if (zone_info->sb_zones[BTRFS_NR_SB_LOG_ZONES * i].type ==
		    BLK_ZONE_TYPE_CONVENTIONAL)
			continue;

		ret = sb_write_pointer(device->bdev,
				       &zone_info->sb_zones[sb_pos], &sb_wp);
		if (ret != -ENOENT && ret) {
			btrfs_err_in_rcu(device->fs_info,
			"zoned: super block log zone corrupted devid %llu zone %u",
					 device->devid, sb_zone);
			ret = -EUCLEAN;
			goto out;
		}
	}


	kfree(zones);

	device->zone_info = zone_info;

	/* device->fs_info is not safe to use for printing messages */
	btrfs_info_in_rcu(NULL,
			"host-%s zoned block device %s, %u zones of %llu bytes",
			bdev_zoned_model(bdev) == BLK_ZONED_HM ? "managed" : "aware",
			rcu_str_deref(device->name), zone_info->nr_zones,
			zone_info->zone_size);

	return 0;

out:
	kfree(zones);
	bitmap_free(zone_info->empty_zones);
	bitmap_free(zone_info->seq_zones);
	kfree(zone_info);

	return ret;
}

void btrfs_destroy_dev_zone_info(struct btrfs_device *device)
{
	struct btrfs_zoned_device_info *zone_info = device->zone_info;

	if (!zone_info)
		return;

	bitmap_free(zone_info->seq_zones);
	bitmap_free(zone_info->empty_zones);
	kfree(zone_info);
	device->zone_info = NULL;
}

int btrfs_get_dev_zone(struct btrfs_device *device, u64 pos,
		       struct blk_zone *zone)
{
	unsigned int nr_zones = 1;
	int ret;

	ret = btrfs_get_dev_zones(device, pos, zone, &nr_zones);
	if (ret != 0 || !nr_zones)
		return ret ? ret : -EIO;

	return 0;
}

int btrfs_check_zoned_mode(struct btrfs_fs_info *fs_info)
{
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	struct btrfs_device *device;
	u64 zoned_devices = 0;
	u64 nr_devices = 0;
	u64 zone_size = 0;
	u64 max_zone_append_size = 0;
	const bool incompat_zoned = btrfs_is_zoned(fs_info);
	int ret = 0;

	/* Count zoned devices */
	list_for_each_entry(device, &fs_devices->devices, dev_list) {
		enum blk_zoned_model model;

		if (!device->bdev)
			continue;

		model = bdev_zoned_model(device->bdev);
		if (model == BLK_ZONED_HM ||
		    (model == BLK_ZONED_HA && incompat_zoned)) {
			struct btrfs_zoned_device_info *zone_info;

			zone_info = device->zone_info;
			zoned_devices++;
			if (!zone_size) {
				zone_size = zone_info->zone_size;
			} else if (zone_info->zone_size != zone_size) {
				btrfs_err(fs_info,
		"zoned: unequal block device zone sizes: have %llu found %llu",
					  device->zone_info->zone_size,
					  zone_size);
				ret = -EINVAL;
				goto out;
			}
			if (!max_zone_append_size ||
			    (zone_info->max_zone_append_size &&
			     zone_info->max_zone_append_size < max_zone_append_size))
				max_zone_append_size =
					zone_info->max_zone_append_size;
		}
		nr_devices++;
	}

	if (!zoned_devices && !incompat_zoned)
		goto out;

	if (!zoned_devices && incompat_zoned) {
		/* No zoned block device found on ZONED filesystem */
		btrfs_err(fs_info,
			  "zoned: no zoned devices found on a zoned filesystem");
		ret = -EINVAL;
		goto out;
	}

	if (zoned_devices && !incompat_zoned) {
		btrfs_err(fs_info,
			  "zoned: mode not enabled but zoned device found");
		ret = -EINVAL;
		goto out;
	}

	if (zoned_devices != nr_devices) {
		btrfs_err(fs_info,
			  "zoned: cannot mix zoned and regular devices");
		ret = -EINVAL;
		goto out;
	}

	/*
	 * stripe_size is always aligned to BTRFS_STRIPE_LEN in
	 * __btrfs_alloc_chunk(). Since we want stripe_len == zone_size,
	 * check the alignment here.
	 */
	if (!IS_ALIGNED(zone_size, BTRFS_STRIPE_LEN)) {
		btrfs_err(fs_info,
			  "zoned: zone size %llu not aligned to stripe %u",
			  zone_size, BTRFS_STRIPE_LEN);
		ret = -EINVAL;
		goto out;
	}

	if (btrfs_fs_incompat(fs_info, MIXED_GROUPS)) {
		btrfs_err(fs_info, "zoned: mixed block groups not supported");
		ret = -EINVAL;
		goto out;
	}

	fs_info->zone_size = zone_size;
	fs_info->max_zone_append_size = max_zone_append_size;

	btrfs_info(fs_info, "zoned mode enabled with zone size %llu", zone_size);
out:
	return ret;
}

int btrfs_check_mountopts_zoned(struct btrfs_fs_info *info)
{
	if (!btrfs_is_zoned(info))
		return 0;

	/*
	 * Space cache writing is not COWed. Disable that to avoid write errors
	 * in sequential zones.
	 */
	if (btrfs_test_opt(info, SPACE_CACHE)) {
		btrfs_err(info, "zoned: space cache v1 is not supported");
		return -EINVAL;
	}

	if (btrfs_test_opt(info, NODATACOW)) {
		btrfs_err(info, "zoned: NODATACOW not supported");
		return -EINVAL;
	}

	return 0;
}

static int sb_log_location(struct block_device *bdev, struct blk_zone *zones,
			   int rw, u64 *bytenr_ret)
{
	u64 wp;
	int ret;

	if (zones[0].type == BLK_ZONE_TYPE_CONVENTIONAL) {
		*bytenr_ret = zones[0].start << SECTOR_SHIFT;
		return 0;
	}

	ret = sb_write_pointer(bdev, zones, &wp);
	if (ret != -ENOENT && ret < 0)
		return ret;

	if (rw == WRITE) {
		struct blk_zone *reset = NULL;

		if (wp == zones[0].start << SECTOR_SHIFT)
			reset = &zones[0];
		else if (wp == zones[1].start << SECTOR_SHIFT)
			reset = &zones[1];

		if (reset && reset->cond != BLK_ZONE_COND_EMPTY) {
			ASSERT(reset->cond == BLK_ZONE_COND_FULL);

			ret = blkdev_zone_mgmt(bdev, REQ_OP_ZONE_RESET,
					       reset->start, reset->len,
					       GFP_NOFS);
			if (ret)
				return ret;

			reset->cond = BLK_ZONE_COND_EMPTY;
			reset->wp = reset->start;
		}
	} else if (ret != -ENOENT) {
		/* For READ, we want the precious one */
		if (wp == zones[0].start << SECTOR_SHIFT)
			wp = (zones[1].start + zones[1].len) << SECTOR_SHIFT;
		wp -= BTRFS_SUPER_INFO_SIZE;
	}

	*bytenr_ret = wp;
	return 0;

}

int btrfs_sb_log_location_bdev(struct block_device *bdev, int mirror, int rw,
			       u64 *bytenr_ret)
{
	struct blk_zone zones[BTRFS_NR_SB_LOG_ZONES];
	unsigned int zone_sectors;
	u32 sb_zone;
	int ret;
	u64 zone_size;
	u8 zone_sectors_shift;
	sector_t nr_sectors;
	u32 nr_zones;

	if (!bdev_is_zoned(bdev)) {
		*bytenr_ret = btrfs_sb_offset(mirror);
		return 0;
	}

	ASSERT(rw == READ || rw == WRITE);

	zone_sectors = bdev_zone_sectors(bdev);
	if (!is_power_of_2(zone_sectors))
		return -EINVAL;
	zone_size = zone_sectors << SECTOR_SHIFT;
	zone_sectors_shift = ilog2(zone_sectors);
	nr_sectors = bdev_nr_sectors(bdev);
	nr_zones = nr_sectors >> zone_sectors_shift;

	sb_zone = sb_zone_number(zone_sectors_shift + SECTOR_SHIFT, mirror);
	if (sb_zone + 1 >= nr_zones)
		return -ENOENT;

	ret = blkdev_report_zones(bdev, sb_zone << zone_sectors_shift,
				  BTRFS_NR_SB_LOG_ZONES, copy_zone_info_cb,
				  zones);
	if (ret < 0)
		return ret;
	if (ret != BTRFS_NR_SB_LOG_ZONES)
		return -EIO;

	return sb_log_location(bdev, zones, rw, bytenr_ret);
}

int btrfs_sb_log_location(struct btrfs_device *device, int mirror, int rw,
			  u64 *bytenr_ret)
{
	struct btrfs_zoned_device_info *zinfo = device->zone_info;
	u32 zone_num;

	if (!zinfo) {
		*bytenr_ret = btrfs_sb_offset(mirror);
		return 0;
	}

	zone_num = sb_zone_number(zinfo->zone_size_shift, mirror);
	if (zone_num + 1 >= zinfo->nr_zones)
		return -ENOENT;

	return sb_log_location(device->bdev,
			       &zinfo->sb_zones[BTRFS_NR_SB_LOG_ZONES * mirror],
			       rw, bytenr_ret);
}

static inline bool is_sb_log_zone(struct btrfs_zoned_device_info *zinfo,
				  int mirror)
{
	u32 zone_num;

	if (!zinfo)
		return false;

	zone_num = sb_zone_number(zinfo->zone_size_shift, mirror);
	if (zone_num + 1 >= zinfo->nr_zones)
		return false;

	if (!test_bit(zone_num, zinfo->seq_zones))
		return false;

	return true;
}

void btrfs_advance_sb_log(struct btrfs_device *device, int mirror)
{
	struct btrfs_zoned_device_info *zinfo = device->zone_info;
	struct blk_zone *zone;

	if (!is_sb_log_zone(zinfo, mirror))
		return;

	zone = &zinfo->sb_zones[BTRFS_NR_SB_LOG_ZONES * mirror];
	if (zone->cond != BLK_ZONE_COND_FULL) {
		if (zone->cond == BLK_ZONE_COND_EMPTY)
			zone->cond = BLK_ZONE_COND_IMP_OPEN;

		zone->wp += (BTRFS_SUPER_INFO_SIZE >> SECTOR_SHIFT);

		if (zone->wp == zone->start + zone->len)
			zone->cond = BLK_ZONE_COND_FULL;

		return;
	}

	zone++;
	ASSERT(zone->cond != BLK_ZONE_COND_FULL);
	if (zone->cond == BLK_ZONE_COND_EMPTY)
		zone->cond = BLK_ZONE_COND_IMP_OPEN;

	zone->wp += (BTRFS_SUPER_INFO_SIZE >> SECTOR_SHIFT);

	if (zone->wp == zone->start + zone->len)
		zone->cond = BLK_ZONE_COND_FULL;
}

int btrfs_reset_sb_log_zones(struct block_device *bdev, int mirror)
{
	sector_t zone_sectors;
	sector_t nr_sectors;
	u8 zone_sectors_shift;
	u32 sb_zone;
	u32 nr_zones;

	zone_sectors = bdev_zone_sectors(bdev);
	zone_sectors_shift = ilog2(zone_sectors);
	nr_sectors = bdev_nr_sectors(bdev);
	nr_zones = nr_sectors >> zone_sectors_shift;

	sb_zone = sb_zone_number(zone_sectors_shift + SECTOR_SHIFT, mirror);
	if (sb_zone + 1 >= nr_zones)
		return -ENOENT;

	return blkdev_zone_mgmt(bdev, REQ_OP_ZONE_RESET,
				sb_zone << zone_sectors_shift,
				zone_sectors * BTRFS_NR_SB_LOG_ZONES, GFP_NOFS);
}

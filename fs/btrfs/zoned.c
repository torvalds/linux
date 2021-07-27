// SPDX-License-Identifier: GPL-2.0

#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/sched/mm.h>
#include "ctree.h"
#include "volumes.h"
#include "zoned.h"
#include "rcu-string.h"
#include "disk-io.h"
#include "block-group.h"
#include "transaction.h"
#include "dev-replace.h"
#include "space-info.h"

/* Maximum number of zones to report per blkdev_report_zones() call */
#define BTRFS_REPORT_NR_ZONES   4096
/* Invalid allocation pointer value for missing devices */
#define WP_MISSING_DEV ((u64)-1)
/* Pseudo write pointer value for conventional zone */
#define WP_CONVENTIONAL ((u64)-2)

/*
 * Location of the first zone of superblock logging zone pairs.
 *
 * - primary superblock:    0B (zone 0)
 * - first copy:          512G (zone starting at that offset)
 * - second copy:           4T (zone starting at that offset)
 */
#define BTRFS_SB_LOG_PRIMARY_OFFSET	(0ULL)
#define BTRFS_SB_LOG_FIRST_OFFSET	(512ULL * SZ_1G)
#define BTRFS_SB_LOG_SECOND_OFFSET	(4096ULL * SZ_1G)

#define BTRFS_SB_LOG_FIRST_SHIFT	const_ilog2(BTRFS_SB_LOG_FIRST_OFFSET)
#define BTRFS_SB_LOG_SECOND_SHIFT	const_ilog2(BTRFS_SB_LOG_SECOND_OFFSET)

/* Number of superblock log zones */
#define BTRFS_NR_SB_LOG_ZONES 2

/*
 * Maximum supported zone size. Currently, SMR disks have a zone size of
 * 256MiB, and we are expecting ZNS drives to be in the 1-4GiB range. We do not
 * expect the zone size to become larger than 8GiB in the near future.
 */
#define BTRFS_MAX_ZONE_SIZE		SZ_8G

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
	 *   C: Compare super blocks from zones[0] and zones[1], use the latest
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
 * Get the first zone number of the superblock mirror
 */
static inline u32 sb_zone_number(int shift, int mirror)
{
	u64 zone;

	ASSERT(mirror < BTRFS_SUPER_MIRROR_MAX);
	switch (mirror) {
	case 0: zone = 0; break;
	case 1: zone = 1ULL << (BTRFS_SB_LOG_FIRST_SHIFT - shift); break;
	case 2: zone = 1ULL << (BTRFS_SB_LOG_SECOND_SHIFT - shift); break;
	}

	ASSERT(zone <= U32_MAX);

	return (u32)zone;
}

static inline sector_t zone_start_sector(u32 zone_number,
					 struct block_device *bdev)
{
	return (sector_t)zone_number << ilog2(bdev_zone_sectors(bdev));
}

static inline u64 zone_start_physical(u32 zone_number,
				      struct btrfs_zoned_device_info *zone_info)
{
	return (u64)zone_number << zone_info->zone_size_shift;
}

/*
 * Emulate blkdev_report_zones() for a non-zoned device. It slices up the block
 * device into static sized chunks and fake a conventional zone on each of
 * them.
 */
static int emulate_report_zones(struct btrfs_device *device, u64 pos,
				struct blk_zone *zones, unsigned int nr_zones)
{
	const sector_t zone_sectors = device->fs_info->zone_size >> SECTOR_SHIFT;
	sector_t bdev_size = bdev_nr_sectors(device->bdev);
	unsigned int i;

	pos >>= SECTOR_SHIFT;
	for (i = 0; i < nr_zones; i++) {
		zones[i].start = i * zone_sectors + pos;
		zones[i].len = zone_sectors;
		zones[i].capacity = zone_sectors;
		zones[i].wp = zones[i].start + zone_sectors;
		zones[i].type = BLK_ZONE_TYPE_CONVENTIONAL;
		zones[i].cond = BLK_ZONE_COND_NOT_WP;

		if (zones[i].wp >= bdev_size) {
			i++;
			break;
		}
	}

	return i;
}

static int btrfs_get_dev_zones(struct btrfs_device *device, u64 pos,
			       struct blk_zone *zones, unsigned int *nr_zones)
{
	int ret;

	if (!*nr_zones)
		return 0;

	if (!bdev_is_zoned(device->bdev)) {
		ret = emulate_report_zones(device, pos, zones, *nr_zones);
		*nr_zones = ret;
		return 0;
	}

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

/* The emulated zone size is determined from the size of device extent */
static int calculate_emulated_zone_size(struct btrfs_fs_info *fs_info)
{
	struct btrfs_path *path;
	struct btrfs_root *root = fs_info->dev_root;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_dev_extent *dext;
	int ret = 0;

	key.objectid = 1;
	key.type = BTRFS_DEV_EXTENT_KEY;
	key.offset = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

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

	leaf = path->nodes[0];
	dext = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_dev_extent);
	fs_info->zone_size = btrfs_dev_extent_length(leaf, dext);
	ret = 0;

out:
	btrfs_free_path(path);

	return ret;
}

int btrfs_get_dev_zone_info_all_devices(struct btrfs_fs_info *fs_info)
{
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	struct btrfs_device *device;
	int ret = 0;

	/* fs_info->zone_size might not set yet. Use the incomapt flag here. */
	if (!btrfs_fs_incompat(fs_info, ZONED))
		return 0;

	mutex_lock(&fs_devices->device_list_mutex);
	list_for_each_entry(device, &fs_devices->devices, dev_list) {
		/* We can skip reading of zone info for missing devices */
		if (!device->bdev)
			continue;

		ret = btrfs_get_dev_zone_info(device);
		if (ret)
			break;
	}
	mutex_unlock(&fs_devices->device_list_mutex);

	return ret;
}

int btrfs_get_dev_zone_info(struct btrfs_device *device)
{
	struct btrfs_fs_info *fs_info = device->fs_info;
	struct btrfs_zoned_device_info *zone_info = NULL;
	struct block_device *bdev = device->bdev;
	struct request_queue *queue = bdev_get_queue(bdev);
	sector_t nr_sectors;
	sector_t sector = 0;
	struct blk_zone *zones = NULL;
	unsigned int i, nreported = 0, nr_zones;
	sector_t zone_sectors;
	char *model, *emulated;
	int ret;

	/*
	 * Cannot use btrfs_is_zoned here, since fs_info::zone_size might not
	 * yet be set.
	 */
	if (!btrfs_fs_incompat(fs_info, ZONED))
		return 0;

	if (device->zone_info)
		return 0;

	zone_info = kzalloc(sizeof(*zone_info), GFP_KERNEL);
	if (!zone_info)
		return -ENOMEM;

	if (!bdev_is_zoned(bdev)) {
		if (!fs_info->zone_size) {
			ret = calculate_emulated_zone_size(fs_info);
			if (ret)
				goto out;
		}

		ASSERT(fs_info->zone_size);
		zone_sectors = fs_info->zone_size >> SECTOR_SHIFT;
	} else {
		zone_sectors = bdev_zone_sectors(bdev);
	}

	/* Check if it's power of 2 (see is_power_of_2) */
	ASSERT(zone_sectors != 0 && (zone_sectors & (zone_sectors - 1)) == 0);
	zone_info->zone_size = zone_sectors << SECTOR_SHIFT;

	/* We reject devices with a zone size larger than 8GB */
	if (zone_info->zone_size > BTRFS_MAX_ZONE_SIZE) {
		btrfs_err_in_rcu(fs_info,
		"zoned: %s: zone size %llu larger than supported maximum %llu",
				 rcu_str_deref(device->name),
				 zone_info->zone_size, BTRFS_MAX_ZONE_SIZE);
		ret = -EINVAL;
		goto out;
	}

	nr_sectors = bdev_nr_sectors(bdev);
	zone_info->zone_size_shift = ilog2(zone_info->zone_size);
	zone_info->max_zone_append_size =
		(u64)queue_max_zone_append_sectors(queue) << SECTOR_SHIFT;
	zone_info->nr_zones = nr_sectors >> ilog2(zone_sectors);
	if (!IS_ALIGNED(nr_sectors, zone_sectors))
		zone_info->nr_zones++;

	if (bdev_is_zoned(bdev) && zone_info->max_zone_append_size == 0) {
		btrfs_err(fs_info, "zoned: device %pg does not support zone append",
			  bdev);
		ret = -EINVAL;
		goto out;
	}

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

		ret = btrfs_get_dev_zones(device,
					  zone_start_physical(sb_zone, zone_info),
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
		 * If zones[0] is conventional, always use the beginning of the
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

	switch (bdev_zoned_model(bdev)) {
	case BLK_ZONED_HM:
		model = "host-managed zoned";
		emulated = "";
		break;
	case BLK_ZONED_HA:
		model = "host-aware zoned";
		emulated = "";
		break;
	case BLK_ZONED_NONE:
		model = "regular";
		emulated = "emulated ";
		break;
	default:
		/* Just in case */
		btrfs_err_in_rcu(fs_info, "zoned: unsupported model %d on %s",
				 bdev_zoned_model(bdev),
				 rcu_str_deref(device->name));
		ret = -EOPNOTSUPP;
		goto out_free_zone_info;
	}

	btrfs_info_in_rcu(fs_info,
		"%s block device %s, %u %szones of %llu bytes",
		model, rcu_str_deref(device->name), zone_info->nr_zones,
		emulated, zone_info->zone_size);

	return 0;

out:
	kfree(zones);
out_free_zone_info:
	bitmap_free(zone_info->empty_zones);
	bitmap_free(zone_info->seq_zones);
	kfree(zone_info);
	device->zone_info = NULL;

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
	const bool incompat_zoned = btrfs_fs_incompat(fs_info, ZONED);
	int ret = 0;

	/* Count zoned devices */
	list_for_each_entry(device, &fs_devices->devices, dev_list) {
		enum blk_zoned_model model;

		if (!device->bdev)
			continue;

		model = bdev_zoned_model(device->bdev);
		/*
		 * A Host-Managed zoned device must be used as a zoned device.
		 * A Host-Aware zoned device and a non-zoned devices can be
		 * treated as a zoned device, if ZONED flag is enabled in the
		 * superblock.
		 */
		if (model == BLK_ZONED_HM ||
		    (model == BLK_ZONED_HA && incompat_zoned) ||
		    (model == BLK_ZONED_NONE && incompat_zoned)) {
			struct btrfs_zoned_device_info *zone_info =
				device->zone_info;

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
	fs_info->fs_devices->chunk_alloc_policy = BTRFS_CHUNK_ALLOC_ZONED;

	/*
	 * Check mount options here, because we might change fs_info->zoned
	 * from fs_info->zone_size.
	 */
	ret = btrfs_check_mountopts_zoned(fs_info);
	if (ret)
		goto out;

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
	sector_t zone_sectors;
	u32 sb_zone;
	int ret;
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
	zone_sectors_shift = ilog2(zone_sectors);
	nr_sectors = bdev_nr_sectors(bdev);
	nr_zones = nr_sectors >> zone_sectors_shift;

	sb_zone = sb_zone_number(zone_sectors_shift + SECTOR_SHIFT, mirror);
	if (sb_zone + 1 >= nr_zones)
		return -ENOENT;

	ret = blkdev_report_zones(bdev, zone_start_sector(sb_zone, bdev),
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

	/*
	 * For a zoned filesystem on a non-zoned block device, use the same
	 * super block locations as regular filesystem. Doing so, the super
	 * block can always be retrieved and the zoned flag of the volume
	 * detected from the super block information.
	 */
	if (!bdev_is_zoned(device->bdev)) {
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
				zone_start_sector(sb_zone, bdev),
				zone_sectors * BTRFS_NR_SB_LOG_ZONES, GFP_NOFS);
}

/**
 * btrfs_find_allocatable_zones - find allocatable zones within a given region
 *
 * @device:	the device to allocate a region on
 * @hole_start: the position of the hole to allocate the region
 * @num_bytes:	size of wanted region
 * @hole_end:	the end of the hole
 * @return:	position of allocatable zones
 *
 * Allocatable region should not contain any superblock locations.
 */
u64 btrfs_find_allocatable_zones(struct btrfs_device *device, u64 hole_start,
				 u64 hole_end, u64 num_bytes)
{
	struct btrfs_zoned_device_info *zinfo = device->zone_info;
	const u8 shift = zinfo->zone_size_shift;
	u64 nzones = num_bytes >> shift;
	u64 pos = hole_start;
	u64 begin, end;
	bool have_sb;
	int i;

	ASSERT(IS_ALIGNED(hole_start, zinfo->zone_size));
	ASSERT(IS_ALIGNED(num_bytes, zinfo->zone_size));

	while (pos < hole_end) {
		begin = pos >> shift;
		end = begin + nzones;

		if (end > zinfo->nr_zones)
			return hole_end;

		/* Check if zones in the region are all empty */
		if (btrfs_dev_is_sequential(device, pos) &&
		    find_next_zero_bit(zinfo->empty_zones, end, begin) != end) {
			pos += zinfo->zone_size;
			continue;
		}

		have_sb = false;
		for (i = 0; i < BTRFS_SUPER_MIRROR_MAX; i++) {
			u32 sb_zone;
			u64 sb_pos;

			sb_zone = sb_zone_number(shift, i);
			if (!(end <= sb_zone ||
			      sb_zone + BTRFS_NR_SB_LOG_ZONES <= begin)) {
				have_sb = true;
				pos = zone_start_physical(
					sb_zone + BTRFS_NR_SB_LOG_ZONES, zinfo);
				break;
			}

			/* We also need to exclude regular superblock positions */
			sb_pos = btrfs_sb_offset(i);
			if (!(pos + num_bytes <= sb_pos ||
			      sb_pos + BTRFS_SUPER_INFO_SIZE <= pos)) {
				have_sb = true;
				pos = ALIGN(sb_pos + BTRFS_SUPER_INFO_SIZE,
					    zinfo->zone_size);
				break;
			}
		}
		if (!have_sb)
			break;
	}

	return pos;
}

int btrfs_reset_device_zone(struct btrfs_device *device, u64 physical,
			    u64 length, u64 *bytes)
{
	int ret;

	*bytes = 0;
	ret = blkdev_zone_mgmt(device->bdev, REQ_OP_ZONE_RESET,
			       physical >> SECTOR_SHIFT, length >> SECTOR_SHIFT,
			       GFP_NOFS);
	if (ret)
		return ret;

	*bytes = length;
	while (length) {
		btrfs_dev_set_zone_empty(device, physical);
		physical += device->zone_info->zone_size;
		length -= device->zone_info->zone_size;
	}

	return 0;
}

int btrfs_ensure_empty_zones(struct btrfs_device *device, u64 start, u64 size)
{
	struct btrfs_zoned_device_info *zinfo = device->zone_info;
	const u8 shift = zinfo->zone_size_shift;
	unsigned long begin = start >> shift;
	unsigned long end = (start + size) >> shift;
	u64 pos;
	int ret;

	ASSERT(IS_ALIGNED(start, zinfo->zone_size));
	ASSERT(IS_ALIGNED(size, zinfo->zone_size));

	if (end > zinfo->nr_zones)
		return -ERANGE;

	/* All the zones are conventional */
	if (find_next_bit(zinfo->seq_zones, begin, end) == end)
		return 0;

	/* All the zones are sequential and empty */
	if (find_next_zero_bit(zinfo->seq_zones, begin, end) == end &&
	    find_next_zero_bit(zinfo->empty_zones, begin, end) == end)
		return 0;

	for (pos = start; pos < start + size; pos += zinfo->zone_size) {
		u64 reset_bytes;

		if (!btrfs_dev_is_sequential(device, pos) ||
		    btrfs_dev_is_empty_zone(device, pos))
			continue;

		/* Free regions should be empty */
		btrfs_warn_in_rcu(
			device->fs_info,
		"zoned: resetting device %s (devid %llu) zone %llu for allocation",
			rcu_str_deref(device->name), device->devid, pos >> shift);
		WARN_ON_ONCE(1);

		ret = btrfs_reset_device_zone(device, pos, zinfo->zone_size,
					      &reset_bytes);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * Calculate an allocation pointer from the extent allocation information
 * for a block group consist of conventional zones. It is pointed to the
 * end of the highest addressed extent in the block group as an allocation
 * offset.
 */
static int calculate_alloc_pointer(struct btrfs_block_group *cache,
				   u64 *offset_ret)
{
	struct btrfs_fs_info *fs_info = cache->fs_info;
	struct btrfs_root *root = fs_info->extent_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_key found_key;
	int ret;
	u64 length;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = cache->start + cache->length;
	key.type = 0;
	key.offset = 0;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	/* We should not find the exact match */
	if (!ret)
		ret = -EUCLEAN;
	if (ret < 0)
		goto out;

	ret = btrfs_previous_extent_item(root, path, cache->start);
	if (ret) {
		if (ret == 1) {
			ret = 0;
			*offset_ret = 0;
		}
		goto out;
	}

	btrfs_item_key_to_cpu(path->nodes[0], &found_key, path->slots[0]);

	if (found_key.type == BTRFS_EXTENT_ITEM_KEY)
		length = found_key.offset;
	else
		length = fs_info->nodesize;

	if (!(found_key.objectid >= cache->start &&
	       found_key.objectid + length <= cache->start + cache->length)) {
		ret = -EUCLEAN;
		goto out;
	}
	*offset_ret = found_key.objectid + length - cache->start;
	ret = 0;

out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_load_block_group_zone_info(struct btrfs_block_group *cache, bool new)
{
	struct btrfs_fs_info *fs_info = cache->fs_info;
	struct extent_map_tree *em_tree = &fs_info->mapping_tree;
	struct extent_map *em;
	struct map_lookup *map;
	struct btrfs_device *device;
	u64 logical = cache->start;
	u64 length = cache->length;
	u64 physical = 0;
	int ret;
	int i;
	unsigned int nofs_flag;
	u64 *alloc_offsets = NULL;
	u64 last_alloc = 0;
	u32 num_sequential = 0, num_conventional = 0;

	if (!btrfs_is_zoned(fs_info))
		return 0;

	/* Sanity check */
	if (!IS_ALIGNED(length, fs_info->zone_size)) {
		btrfs_err(fs_info,
		"zoned: block group %llu len %llu unaligned to zone size %llu",
			  logical, length, fs_info->zone_size);
		return -EIO;
	}

	/* Get the chunk mapping */
	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, logical, length);
	read_unlock(&em_tree->lock);

	if (!em)
		return -EINVAL;

	map = em->map_lookup;

	alloc_offsets = kcalloc(map->num_stripes, sizeof(*alloc_offsets), GFP_NOFS);
	if (!alloc_offsets) {
		free_extent_map(em);
		return -ENOMEM;
	}

	for (i = 0; i < map->num_stripes; i++) {
		bool is_sequential;
		struct blk_zone zone;
		struct btrfs_dev_replace *dev_replace = &fs_info->dev_replace;
		int dev_replace_is_ongoing = 0;

		device = map->stripes[i].dev;
		physical = map->stripes[i].physical;

		if (device->bdev == NULL) {
			alloc_offsets[i] = WP_MISSING_DEV;
			continue;
		}

		is_sequential = btrfs_dev_is_sequential(device, physical);
		if (is_sequential)
			num_sequential++;
		else
			num_conventional++;

		if (!is_sequential) {
			alloc_offsets[i] = WP_CONVENTIONAL;
			continue;
		}

		/*
		 * This zone will be used for allocation, so mark this zone
		 * non-empty.
		 */
		btrfs_dev_clear_zone_empty(device, physical);

		down_read(&dev_replace->rwsem);
		dev_replace_is_ongoing = btrfs_dev_replace_is_ongoing(dev_replace);
		if (dev_replace_is_ongoing && dev_replace->tgtdev != NULL)
			btrfs_dev_clear_zone_empty(dev_replace->tgtdev, physical);
		up_read(&dev_replace->rwsem);

		/*
		 * The group is mapped to a sequential zone. Get the zone write
		 * pointer to determine the allocation offset within the zone.
		 */
		WARN_ON(!IS_ALIGNED(physical, fs_info->zone_size));
		nofs_flag = memalloc_nofs_save();
		ret = btrfs_get_dev_zone(device, physical, &zone);
		memalloc_nofs_restore(nofs_flag);
		if (ret == -EIO || ret == -EOPNOTSUPP) {
			ret = 0;
			alloc_offsets[i] = WP_MISSING_DEV;
			continue;
		} else if (ret) {
			goto out;
		}

		if (zone.type == BLK_ZONE_TYPE_CONVENTIONAL) {
			btrfs_err_in_rcu(fs_info,
	"zoned: unexpected conventional zone %llu on device %s (devid %llu)",
				zone.start << SECTOR_SHIFT,
				rcu_str_deref(device->name), device->devid);
			ret = -EIO;
			goto out;
		}

		switch (zone.cond) {
		case BLK_ZONE_COND_OFFLINE:
		case BLK_ZONE_COND_READONLY:
			btrfs_err(fs_info,
		"zoned: offline/readonly zone %llu on device %s (devid %llu)",
				  physical >> device->zone_info->zone_size_shift,
				  rcu_str_deref(device->name), device->devid);
			alloc_offsets[i] = WP_MISSING_DEV;
			break;
		case BLK_ZONE_COND_EMPTY:
			alloc_offsets[i] = 0;
			break;
		case BLK_ZONE_COND_FULL:
			alloc_offsets[i] = fs_info->zone_size;
			break;
		default:
			/* Partially used zone */
			alloc_offsets[i] =
					((zone.wp - zone.start) << SECTOR_SHIFT);
			break;
		}
	}

	if (num_sequential > 0)
		cache->seq_zone = true;

	if (num_conventional > 0) {
		/*
		 * Avoid calling calculate_alloc_pointer() for new BG. It
		 * is no use for new BG. It must be always 0.
		 *
		 * Also, we have a lock chain of extent buffer lock ->
		 * chunk mutex.  For new BG, this function is called from
		 * btrfs_make_block_group() which is already taking the
		 * chunk mutex. Thus, we cannot call
		 * calculate_alloc_pointer() which takes extent buffer
		 * locks to avoid deadlock.
		 */
		if (new) {
			cache->alloc_offset = 0;
			goto out;
		}
		ret = calculate_alloc_pointer(cache, &last_alloc);
		if (ret || map->num_stripes == num_conventional) {
			if (!ret)
				cache->alloc_offset = last_alloc;
			else
				btrfs_err(fs_info,
			"zoned: failed to determine allocation offset of bg %llu",
					  cache->start);
			goto out;
		}
	}

	switch (map->type & BTRFS_BLOCK_GROUP_PROFILE_MASK) {
	case 0: /* single */
		if (alloc_offsets[0] == WP_MISSING_DEV) {
			btrfs_err(fs_info,
			"zoned: cannot recover write pointer for zone %llu",
				physical);
			ret = -EIO;
			goto out;
		}
		cache->alloc_offset = alloc_offsets[0];
		break;
	case BTRFS_BLOCK_GROUP_DUP:
	case BTRFS_BLOCK_GROUP_RAID1:
	case BTRFS_BLOCK_GROUP_RAID0:
	case BTRFS_BLOCK_GROUP_RAID10:
	case BTRFS_BLOCK_GROUP_RAID5:
	case BTRFS_BLOCK_GROUP_RAID6:
		/* non-single profiles are not supported yet */
	default:
		btrfs_err(fs_info, "zoned: profile %s not yet supported",
			  btrfs_bg_type_to_raid_name(map->type));
		ret = -EINVAL;
		goto out;
	}

out:
	if (cache->alloc_offset > fs_info->zone_size) {
		btrfs_err(fs_info,
			"zoned: invalid write pointer %llu in block group %llu",
			cache->alloc_offset, cache->start);
		ret = -EIO;
	}

	/* An extent is allocated after the write pointer */
	if (!ret && num_conventional && last_alloc > cache->alloc_offset) {
		btrfs_err(fs_info,
			  "zoned: got wrong write pointer in BG %llu: %llu > %llu",
			  logical, last_alloc, cache->alloc_offset);
		ret = -EIO;
	}

	if (!ret)
		cache->meta_write_pointer = cache->alloc_offset + cache->start;

	kfree(alloc_offsets);
	free_extent_map(em);

	return ret;
}

void btrfs_calc_zone_unusable(struct btrfs_block_group *cache)
{
	u64 unusable, free;

	if (!btrfs_is_zoned(cache->fs_info))
		return;

	WARN_ON(cache->bytes_super != 0);
	unusable = cache->alloc_offset - cache->used;
	free = cache->length - cache->alloc_offset;

	/* We only need ->free_space in ALLOC_SEQ block groups */
	cache->last_byte_to_unpin = (u64)-1;
	cache->cached = BTRFS_CACHE_FINISHED;
	cache->free_space_ctl->free_space = free;
	cache->zone_unusable = unusable;

	/* Should not have any excluded extents. Just in case, though */
	btrfs_free_excluded_extents(cache);
}

void btrfs_redirty_list_add(struct btrfs_transaction *trans,
			    struct extent_buffer *eb)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;

	if (!btrfs_is_zoned(fs_info) ||
	    btrfs_header_flag(eb, BTRFS_HEADER_FLAG_WRITTEN) ||
	    !list_empty(&eb->release_list))
		return;

	set_extent_buffer_dirty(eb);
	set_extent_bits_nowait(&trans->dirty_pages, eb->start,
			       eb->start + eb->len - 1, EXTENT_DIRTY);
	memzero_extent_buffer(eb, 0, eb->len);
	set_bit(EXTENT_BUFFER_NO_CHECK, &eb->bflags);

	spin_lock(&trans->releasing_ebs_lock);
	list_add_tail(&eb->release_list, &trans->releasing_ebs);
	spin_unlock(&trans->releasing_ebs_lock);
	atomic_inc(&eb->refs);
}

void btrfs_free_redirty_list(struct btrfs_transaction *trans)
{
	spin_lock(&trans->releasing_ebs_lock);
	while (!list_empty(&trans->releasing_ebs)) {
		struct extent_buffer *eb;

		eb = list_first_entry(&trans->releasing_ebs,
				      struct extent_buffer, release_list);
		list_del_init(&eb->release_list);
		free_extent_buffer(eb);
	}
	spin_unlock(&trans->releasing_ebs_lock);
}

bool btrfs_use_zone_append(struct btrfs_inode *inode, u64 start)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct btrfs_block_group *cache;
	bool ret = false;

	if (!btrfs_is_zoned(fs_info))
		return false;

	if (!fs_info->max_zone_append_size)
		return false;

	if (!is_data_inode(&inode->vfs_inode))
		return false;

	cache = btrfs_lookup_block_group(fs_info, start);
	ASSERT(cache);
	if (!cache)
		return false;

	ret = cache->seq_zone;
	btrfs_put_block_group(cache);

	return ret;
}

void btrfs_record_physical_zoned(struct inode *inode, u64 file_offset,
				 struct bio *bio)
{
	struct btrfs_ordered_extent *ordered;
	const u64 physical = bio->bi_iter.bi_sector << SECTOR_SHIFT;

	if (bio_op(bio) != REQ_OP_ZONE_APPEND)
		return;

	ordered = btrfs_lookup_ordered_extent(BTRFS_I(inode), file_offset);
	if (WARN_ON(!ordered))
		return;

	ordered->physical = physical;
	ordered->bdev = bio->bi_bdev;

	btrfs_put_ordered_extent(ordered);
}

void btrfs_rewrite_logical_zoned(struct btrfs_ordered_extent *ordered)
{
	struct btrfs_inode *inode = BTRFS_I(ordered->inode);
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct extent_map_tree *em_tree;
	struct extent_map *em;
	struct btrfs_ordered_sum *sum;
	u64 orig_logical = ordered->disk_bytenr;
	u64 *logical = NULL;
	int nr, stripe_len;

	/* Zoned devices should not have partitions. So, we can assume it is 0 */
	ASSERT(!bdev_is_partition(ordered->bdev));
	if (WARN_ON(!ordered->bdev))
		return;

	if (WARN_ON(btrfs_rmap_block(fs_info, orig_logical, ordered->bdev,
				     ordered->physical, &logical, &nr,
				     &stripe_len)))
		goto out;

	WARN_ON(nr != 1);

	if (orig_logical == *logical)
		goto out;

	ordered->disk_bytenr = *logical;

	em_tree = &inode->extent_tree;
	write_lock(&em_tree->lock);
	em = search_extent_mapping(em_tree, ordered->file_offset,
				   ordered->num_bytes);
	em->block_start = *logical;
	free_extent_map(em);
	write_unlock(&em_tree->lock);

	list_for_each_entry(sum, &ordered->list, list) {
		if (*logical < orig_logical)
			sum->bytenr -= orig_logical - *logical;
		else
			sum->bytenr += *logical - orig_logical;
	}

out:
	kfree(logical);
}

bool btrfs_check_meta_write_pointer(struct btrfs_fs_info *fs_info,
				    struct extent_buffer *eb,
				    struct btrfs_block_group **cache_ret)
{
	struct btrfs_block_group *cache;
	bool ret = true;

	if (!btrfs_is_zoned(fs_info))
		return true;

	cache = *cache_ret;

	if (cache && (eb->start < cache->start ||
		      cache->start + cache->length <= eb->start)) {
		btrfs_put_block_group(cache);
		cache = NULL;
		*cache_ret = NULL;
	}

	if (!cache)
		cache = btrfs_lookup_block_group(fs_info, eb->start);

	if (cache) {
		if (cache->meta_write_pointer != eb->start) {
			btrfs_put_block_group(cache);
			cache = NULL;
			ret = false;
		} else {
			cache->meta_write_pointer = eb->start + eb->len;
		}

		*cache_ret = cache;
	}

	return ret;
}

void btrfs_revert_meta_write_pointer(struct btrfs_block_group *cache,
				     struct extent_buffer *eb)
{
	if (!btrfs_is_zoned(eb->fs_info) || !cache)
		return;

	ASSERT(cache->meta_write_pointer == eb->start + eb->len);
	cache->meta_write_pointer = eb->start;
}

int btrfs_zoned_issue_zeroout(struct btrfs_device *device, u64 physical, u64 length)
{
	if (!btrfs_dev_is_sequential(device, physical))
		return -EOPNOTSUPP;

	return blkdev_issue_zeroout(device->bdev, physical >> SECTOR_SHIFT,
				    length >> SECTOR_SHIFT, GFP_NOFS, 0);
}

static int read_zone_info(struct btrfs_fs_info *fs_info, u64 logical,
			  struct blk_zone *zone)
{
	struct btrfs_bio *bbio = NULL;
	u64 mapped_length = PAGE_SIZE;
	unsigned int nofs_flag;
	int nmirrors;
	int i, ret;

	ret = btrfs_map_sblock(fs_info, BTRFS_MAP_GET_READ_MIRRORS, logical,
			       &mapped_length, &bbio);
	if (ret || !bbio || mapped_length < PAGE_SIZE) {
		btrfs_put_bbio(bbio);
		return -EIO;
	}

	if (bbio->map_type & BTRFS_BLOCK_GROUP_RAID56_MASK)
		return -EINVAL;

	nofs_flag = memalloc_nofs_save();
	nmirrors = (int)bbio->num_stripes;
	for (i = 0; i < nmirrors; i++) {
		u64 physical = bbio->stripes[i].physical;
		struct btrfs_device *dev = bbio->stripes[i].dev;

		/* Missing device */
		if (!dev->bdev)
			continue;

		ret = btrfs_get_dev_zone(dev, physical, zone);
		/* Failing device */
		if (ret == -EIO || ret == -EOPNOTSUPP)
			continue;
		break;
	}
	memalloc_nofs_restore(nofs_flag);

	return ret;
}

/*
 * Synchronize write pointer in a zone at @physical_start on @tgt_dev, by
 * filling zeros between @physical_pos to a write pointer of dev-replace
 * source device.
 */
int btrfs_sync_zone_write_pointer(struct btrfs_device *tgt_dev, u64 logical,
				    u64 physical_start, u64 physical_pos)
{
	struct btrfs_fs_info *fs_info = tgt_dev->fs_info;
	struct blk_zone zone;
	u64 length;
	u64 wp;
	int ret;

	if (!btrfs_dev_is_sequential(tgt_dev, physical_pos))
		return 0;

	ret = read_zone_info(fs_info, logical, &zone);
	if (ret)
		return ret;

	wp = physical_start + ((zone.wp - zone.start) << SECTOR_SHIFT);

	if (physical_pos == wp)
		return 0;

	if (physical_pos > wp)
		return -EUCLEAN;

	length = wp - physical_pos;
	return btrfs_zoned_issue_zeroout(tgt_dev, physical_pos, length);
}

struct btrfs_device *btrfs_zoned_get_device(struct btrfs_fs_info *fs_info,
					    u64 logical, u64 length)
{
	struct btrfs_device *device;
	struct extent_map *em;
	struct map_lookup *map;

	em = btrfs_get_chunk_map(fs_info, logical, length);
	if (IS_ERR(em))
		return ERR_CAST(em);

	map = em->map_lookup;
	/* We only support single profile for now */
	ASSERT(map->num_stripes == 1);
	device = map->stripes[0].dev;

	free_extent_map(em);

	return device;
}

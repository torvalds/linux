// SPDX-License-Identifier: GPL-2.0

#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/sched/mm.h>
#include <linux/atomic.h>
#include <linux/vmalloc.h>
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
 * Minimum of active zones we need:
 *
 * - BTRFS_SUPER_MIRROR_MAX zones for superblock mirrors
 * - 3 zones to ensure at least one zone per SYSTEM, META and DATA block group
 * - 1 zone for tree-log dedicated block group
 * - 1 zone for relocation
 */
#define BTRFS_MIN_ACTIVE_ZONES		(BTRFS_SUPER_MIRROR_MAX + 5)

/*
 * Minimum / maximum supported zone size. Currently, SMR disks have a zone
 * size of 256MiB, and we are expecting ZNS drives to be in the 1-4GiB range.
 * We do not expect the zone size to become larger than 8GiB or smaller than
 * 4MiB in the near future.
 */
#define BTRFS_MAX_ZONE_SIZE		SZ_8G
#define BTRFS_MIN_ZONE_SIZE		SZ_4M

#define SUPER_INFO_SECTORS	((u64)BTRFS_SUPER_INFO_SIZE >> SECTOR_SHIFT)

static inline bool sb_zone_is_full(const struct blk_zone *zone)
{
	return (zone->cond == BLK_ZONE_COND_FULL) ||
		(zone->wp + SUPER_INFO_SECTORS > zone->start + zone->capacity);
}

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
	int i;

	for (i = 0; i < BTRFS_NR_SB_LOG_ZONES; i++) {
		ASSERT(zones[i].type != BLK_ZONE_TYPE_CONVENTIONAL);
		empty[i] = (zones[i].cond == BLK_ZONE_COND_EMPTY);
		full[i] = sb_zone_is_full(&zones[i]);
	}

	/*
	 * Possible states of log buffer zones
	 *
	 *           Empty[0]  In use[0]  Full[0]
	 * Empty[1]         *          0        1
	 * In use[1]        x          x        1
	 * Full[1]          0          0        C
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
	struct btrfs_zoned_device_info *zinfo = device->zone_info;
	u32 zno;
	int ret;

	if (!*nr_zones)
		return 0;

	if (!bdev_is_zoned(device->bdev)) {
		ret = emulate_report_zones(device, pos, zones, *nr_zones);
		*nr_zones = ret;
		return 0;
	}

	/* Check cache */
	if (zinfo->zone_cache) {
		unsigned int i;

		ASSERT(IS_ALIGNED(pos, zinfo->zone_size));
		zno = pos >> zinfo->zone_size_shift;
		/*
		 * We cannot report zones beyond the zone end. So, it is OK to
		 * cap *nr_zones to at the end.
		 */
		*nr_zones = min_t(u32, *nr_zones, zinfo->nr_zones - zno);

		for (i = 0; i < *nr_zones; i++) {
			struct blk_zone *zone_info;

			zone_info = &zinfo->zone_cache[zno + i];
			if (!zone_info->len)
				break;
		}

		if (i == *nr_zones) {
			/* Cache hit on all the zones */
			memcpy(zones, zinfo->zone_cache + zno,
			       sizeof(*zinfo->zone_cache) * *nr_zones);
			return 0;
		}
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

	/* Populate cache */
	if (zinfo->zone_cache)
		memcpy(zinfo->zone_cache + zno, zones,
		       sizeof(*zinfo->zone_cache) * *nr_zones);

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
		ret = btrfs_next_leaf(root, path);
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

		ret = btrfs_get_dev_zone_info(device, true);
		if (ret)
			break;
	}
	mutex_unlock(&fs_devices->device_list_mutex);

	return ret;
}

int btrfs_get_dev_zone_info(struct btrfs_device *device, bool populate_cache)
{
	struct btrfs_fs_info *fs_info = device->fs_info;
	struct btrfs_zoned_device_info *zone_info = NULL;
	struct block_device *bdev = device->bdev;
	unsigned int max_active_zones;
	unsigned int nactive;
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

	device->zone_info = zone_info;

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
	} else if (zone_info->zone_size < BTRFS_MIN_ZONE_SIZE) {
		btrfs_err_in_rcu(fs_info,
		"zoned: %s: zone size %llu smaller than supported minimum %u",
				 rcu_str_deref(device->name),
				 zone_info->zone_size, BTRFS_MIN_ZONE_SIZE);
		ret = -EINVAL;
		goto out;
	}

	nr_sectors = bdev_nr_sectors(bdev);
	zone_info->zone_size_shift = ilog2(zone_info->zone_size);
	zone_info->nr_zones = nr_sectors >> ilog2(zone_sectors);
	/*
	 * We limit max_zone_append_size also by max_segments *
	 * PAGE_SIZE. Technically, we can have multiple pages per segment. But,
	 * since btrfs adds the pages one by one to a bio, and btrfs cannot
	 * increase the metadata reservation even if it increases the number of
	 * extents, it is safe to stick with the limit.
	 *
	 * With the zoned emulation, we can have non-zoned device on the zoned
	 * mode. In this case, we don't have a valid max zone append size. So,
	 * use max_segments * PAGE_SIZE as the pseudo max_zone_append_size.
	 */
	if (bdev_is_zoned(bdev)) {
		zone_info->max_zone_append_size = min_t(u64,
			(u64)bdev_max_zone_append_sectors(bdev) << SECTOR_SHIFT,
			(u64)bdev_max_segments(bdev) << PAGE_SHIFT);
	} else {
		zone_info->max_zone_append_size =
			(u64)bdev_max_segments(bdev) << PAGE_SHIFT;
	}
	if (!IS_ALIGNED(nr_sectors, zone_sectors))
		zone_info->nr_zones++;

	max_active_zones = bdev_max_active_zones(bdev);
	if (max_active_zones && max_active_zones < BTRFS_MIN_ACTIVE_ZONES) {
		btrfs_err_in_rcu(fs_info,
"zoned: %s: max active zones %u is too small, need at least %u active zones",
				 rcu_str_deref(device->name), max_active_zones,
				 BTRFS_MIN_ACTIVE_ZONES);
		ret = -EINVAL;
		goto out;
	}
	zone_info->max_active_zones = max_active_zones;

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

	zone_info->active_zones = bitmap_zalloc(zone_info->nr_zones, GFP_KERNEL);
	if (!zone_info->active_zones) {
		ret = -ENOMEM;
		goto out;
	}

	zones = kcalloc(BTRFS_REPORT_NR_ZONES, sizeof(struct blk_zone), GFP_KERNEL);
	if (!zones) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * Enable zone cache only for a zoned device. On a non-zoned device, we
	 * fill the zone info with emulated CONVENTIONAL zones, so no need to
	 * use the cache.
	 */
	if (populate_cache && bdev_is_zoned(device->bdev)) {
		zone_info->zone_cache = vzalloc(sizeof(struct blk_zone) *
						zone_info->nr_zones);
		if (!zone_info->zone_cache) {
			btrfs_err_in_rcu(device->fs_info,
				"zoned: failed to allocate zone cache for %s",
				rcu_str_deref(device->name));
			ret = -ENOMEM;
			goto out;
		}
	}

	/* Get zones type */
	nactive = 0;
	while (sector < nr_sectors) {
		nr_zones = BTRFS_REPORT_NR_ZONES;
		ret = btrfs_get_dev_zones(device, sector << SECTOR_SHIFT, zones,
					  &nr_zones);
		if (ret)
			goto out;

		for (i = 0; i < nr_zones; i++) {
			if (zones[i].type == BLK_ZONE_TYPE_SEQWRITE_REQ)
				__set_bit(nreported, zone_info->seq_zones);
			switch (zones[i].cond) {
			case BLK_ZONE_COND_EMPTY:
				__set_bit(nreported, zone_info->empty_zones);
				break;
			case BLK_ZONE_COND_IMP_OPEN:
			case BLK_ZONE_COND_EXP_OPEN:
			case BLK_ZONE_COND_CLOSED:
				__set_bit(nreported, zone_info->active_zones);
				nactive++;
				break;
			}
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

	if (max_active_zones) {
		if (nactive > max_active_zones) {
			btrfs_err_in_rcu(device->fs_info,
			"zoned: %u active zones on %s exceeds max_active_zones %u",
					 nactive, rcu_str_deref(device->name),
					 max_active_zones);
			ret = -EIO;
			goto out;
		}
		atomic_set(&zone_info->active_zones_left,
			   max_active_zones - nactive);
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
	btrfs_destroy_dev_zone_info(device);

	return ret;
}

void btrfs_destroy_dev_zone_info(struct btrfs_device *device)
{
	struct btrfs_zoned_device_info *zone_info = device->zone_info;

	if (!zone_info)
		return;

	bitmap_free(zone_info->active_zones);
	bitmap_free(zone_info->seq_zones);
	bitmap_free(zone_info->empty_zones);
	vfree(zone_info->zone_cache);
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

static int btrfs_check_for_zoned_device(struct btrfs_fs_info *fs_info)
{
	struct btrfs_device *device;

	list_for_each_entry(device, &fs_info->fs_devices->devices, dev_list) {
		if (device->bdev &&
		    bdev_zoned_model(device->bdev) == BLK_ZONED_HM) {
			btrfs_err(fs_info,
				"zoned: mode not enabled but zoned device found: %pg",
				device->bdev);
			return -EINVAL;
		}
	}

	return 0;
}

int btrfs_check_zoned_mode(struct btrfs_fs_info *fs_info)
{
	struct btrfs_device *device;
	u64 zone_size = 0;
	u64 max_zone_append_size = 0;
	int ret;

	/*
	 * Host-Managed devices can't be used without the ZONED flag.  With the
	 * ZONED all devices can be used, using zone emulation if required.
	 */
	if (!btrfs_fs_incompat(fs_info, ZONED))
		return btrfs_check_for_zoned_device(fs_info);

	list_for_each_entry(device, &fs_info->fs_devices->devices, dev_list) {
		struct btrfs_zoned_device_info *zone_info = device->zone_info;

		if (!device->bdev)
			continue;

		if (!zone_size) {
			zone_size = zone_info->zone_size;
		} else if (zone_info->zone_size != zone_size) {
			btrfs_err(fs_info,
		"zoned: unequal block device zone sizes: have %llu found %llu",
				  zone_info->zone_size, zone_size);
			return -EINVAL;
		}
		if (!max_zone_append_size ||
		    (zone_info->max_zone_append_size &&
		     zone_info->max_zone_append_size < max_zone_append_size))
			max_zone_append_size = zone_info->max_zone_append_size;
	}

	/*
	 * stripe_size is always aligned to BTRFS_STRIPE_LEN in
	 * btrfs_create_chunk(). Since we want stripe_len == zone_size,
	 * check the alignment here.
	 */
	if (!IS_ALIGNED(zone_size, BTRFS_STRIPE_LEN)) {
		btrfs_err(fs_info,
			  "zoned: zone size %llu not aligned to stripe %u",
			  zone_size, BTRFS_STRIPE_LEN);
		return -EINVAL;
	}

	if (btrfs_fs_incompat(fs_info, MIXED_GROUPS)) {
		btrfs_err(fs_info, "zoned: mixed block groups not supported");
		return -EINVAL;
	}

	fs_info->zone_size = zone_size;
	fs_info->max_zone_append_size = ALIGN_DOWN(max_zone_append_size,
						   fs_info->sectorsize);
	fs_info->fs_devices->chunk_alloc_policy = BTRFS_CHUNK_ALLOC_ZONED;
	if (fs_info->max_zone_append_size < fs_info->max_extent_size)
		fs_info->max_extent_size = fs_info->max_zone_append_size;

	/*
	 * Check mount options here, because we might change fs_info->zoned
	 * from fs_info->zone_size.
	 */
	ret = btrfs_check_mountopts_zoned(fs_info);
	if (ret)
		return ret;

	btrfs_info(fs_info, "zoned mode enabled with zone size %llu", zone_size);
	return 0;
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
			ASSERT(sb_zone_is_full(reset));

			ret = blkdev_zone_mgmt(bdev, REQ_OP_ZONE_RESET,
					       reset->start, reset->len,
					       GFP_NOFS);
			if (ret)
				return ret;

			reset->cond = BLK_ZONE_COND_EMPTY;
			reset->wp = reset->start;
		}
	} else if (ret != -ENOENT) {
		/*
		 * For READ, we want the previous one. Move write pointer to
		 * the end of a zone, if it is at the head of a zone.
		 */
		u64 zone_end = 0;

		if (wp == zones[0].start << SECTOR_SHIFT)
			zone_end = zones[1].start + zones[1].capacity;
		else if (wp == zones[1].start << SECTOR_SHIFT)
			zone_end = zones[0].start + zones[0].capacity;
		if (zone_end)
			wp = ALIGN_DOWN(zone_end << SECTOR_SHIFT,
					BTRFS_SUPER_INFO_SIZE);

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

int btrfs_advance_sb_log(struct btrfs_device *device, int mirror)
{
	struct btrfs_zoned_device_info *zinfo = device->zone_info;
	struct blk_zone *zone;
	int i;

	if (!is_sb_log_zone(zinfo, mirror))
		return 0;

	zone = &zinfo->sb_zones[BTRFS_NR_SB_LOG_ZONES * mirror];
	for (i = 0; i < BTRFS_NR_SB_LOG_ZONES; i++) {
		/* Advance the next zone */
		if (zone->cond == BLK_ZONE_COND_FULL) {
			zone++;
			continue;
		}

		if (zone->cond == BLK_ZONE_COND_EMPTY)
			zone->cond = BLK_ZONE_COND_IMP_OPEN;

		zone->wp += SUPER_INFO_SECTORS;

		if (sb_zone_is_full(zone)) {
			/*
			 * No room left to write new superblock. Since
			 * superblock is written with REQ_SYNC, it is safe to
			 * finish the zone now.
			 *
			 * If the write pointer is exactly at the capacity,
			 * explicit ZONE_FINISH is not necessary.
			 */
			if (zone->wp != zone->start + zone->capacity) {
				int ret;

				ret = blkdev_zone_mgmt(device->bdev,
						REQ_OP_ZONE_FINISH, zone->start,
						zone->len, GFP_NOFS);
				if (ret)
					return ret;
			}

			zone->wp = zone->start + zone->len;
			zone->cond = BLK_ZONE_COND_FULL;
		}
		return 0;
	}

	/* All the zones are FULL. Should not reach here. */
	ASSERT(0);
	return -EIO;
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

static bool btrfs_dev_set_active_zone(struct btrfs_device *device, u64 pos)
{
	struct btrfs_zoned_device_info *zone_info = device->zone_info;
	unsigned int zno = (pos >> zone_info->zone_size_shift);

	/* We can use any number of zones */
	if (zone_info->max_active_zones == 0)
		return true;

	if (!test_bit(zno, zone_info->active_zones)) {
		/* Active zone left? */
		if (atomic_dec_if_positive(&zone_info->active_zones_left) < 0)
			return false;
		if (test_and_set_bit(zno, zone_info->active_zones)) {
			/* Someone already set the bit */
			atomic_inc(&zone_info->active_zones_left);
		}
	}

	return true;
}

static void btrfs_dev_clear_active_zone(struct btrfs_device *device, u64 pos)
{
	struct btrfs_zoned_device_info *zone_info = device->zone_info;
	unsigned int zno = (pos >> zone_info->zone_size_shift);

	/* We can use any number of zones */
	if (zone_info->max_active_zones == 0)
		return;

	if (test_and_clear_bit(zno, zone_info->active_zones))
		atomic_inc(&zone_info->active_zones_left);
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
		btrfs_dev_clear_active_zone(device, physical);
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
				   u64 *offset_ret, bool new)
{
	struct btrfs_fs_info *fs_info = cache->fs_info;
	struct btrfs_root *root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_key found_key;
	int ret;
	u64 length;

	/*
	 * Avoid  tree lookups for a new block group, there's no use for it.
	 * It must always be 0.
	 *
	 * Also, we have a lock chain of extent buffer lock -> chunk mutex.
	 * For new a block group, this function is called from
	 * btrfs_make_block_group() which is already taking the chunk mutex.
	 * Thus, we cannot call calculate_alloc_pointer() which takes extent
	 * buffer locks to avoid deadlock.
	 */
	if (new) {
		*offset_ret = 0;
		return 0;
	}

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = cache->start + cache->length;
	key.type = 0;
	key.offset = 0;

	root = btrfs_extent_root(fs_info, key.objectid);
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
	int ret;
	int i;
	unsigned int nofs_flag;
	u64 *alloc_offsets = NULL;
	u64 *caps = NULL;
	u64 *physical = NULL;
	unsigned long *active = NULL;
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

	cache->physical_map = kmemdup(map, map_lookup_size(map->num_stripes), GFP_NOFS);
	if (!cache->physical_map) {
		ret = -ENOMEM;
		goto out;
	}

	alloc_offsets = kcalloc(map->num_stripes, sizeof(*alloc_offsets), GFP_NOFS);
	if (!alloc_offsets) {
		ret = -ENOMEM;
		goto out;
	}

	caps = kcalloc(map->num_stripes, sizeof(*caps), GFP_NOFS);
	if (!caps) {
		ret = -ENOMEM;
		goto out;
	}

	physical = kcalloc(map->num_stripes, sizeof(*physical), GFP_NOFS);
	if (!physical) {
		ret = -ENOMEM;
		goto out;
	}

	active = bitmap_zalloc(map->num_stripes, GFP_NOFS);
	if (!active) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < map->num_stripes; i++) {
		bool is_sequential;
		struct blk_zone zone;
		struct btrfs_dev_replace *dev_replace = &fs_info->dev_replace;
		int dev_replace_is_ongoing = 0;

		device = map->stripes[i].dev;
		physical[i] = map->stripes[i].physical;

		if (device->bdev == NULL) {
			alloc_offsets[i] = WP_MISSING_DEV;
			continue;
		}

		is_sequential = btrfs_dev_is_sequential(device, physical[i]);
		if (is_sequential)
			num_sequential++;
		else
			num_conventional++;

		/*
		 * Consider a zone as active if we can allow any number of
		 * active zones.
		 */
		if (!device->zone_info->max_active_zones)
			__set_bit(i, active);

		if (!is_sequential) {
			alloc_offsets[i] = WP_CONVENTIONAL;
			continue;
		}

		/*
		 * This zone will be used for allocation, so mark this zone
		 * non-empty.
		 */
		btrfs_dev_clear_zone_empty(device, physical[i]);

		down_read(&dev_replace->rwsem);
		dev_replace_is_ongoing = btrfs_dev_replace_is_ongoing(dev_replace);
		if (dev_replace_is_ongoing && dev_replace->tgtdev != NULL)
			btrfs_dev_clear_zone_empty(dev_replace->tgtdev, physical[i]);
		up_read(&dev_replace->rwsem);

		/*
		 * The group is mapped to a sequential zone. Get the zone write
		 * pointer to determine the allocation offset within the zone.
		 */
		WARN_ON(!IS_ALIGNED(physical[i], fs_info->zone_size));
		nofs_flag = memalloc_nofs_save();
		ret = btrfs_get_dev_zone(device, physical[i], &zone);
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

		caps[i] = (zone.capacity << SECTOR_SHIFT);

		switch (zone.cond) {
		case BLK_ZONE_COND_OFFLINE:
		case BLK_ZONE_COND_READONLY:
			btrfs_err(fs_info,
		"zoned: offline/readonly zone %llu on device %s (devid %llu)",
				  physical[i] >> device->zone_info->zone_size_shift,
				  rcu_str_deref(device->name), device->devid);
			alloc_offsets[i] = WP_MISSING_DEV;
			break;
		case BLK_ZONE_COND_EMPTY:
			alloc_offsets[i] = 0;
			break;
		case BLK_ZONE_COND_FULL:
			alloc_offsets[i] = caps[i];
			break;
		default:
			/* Partially used zone */
			alloc_offsets[i] =
					((zone.wp - zone.start) << SECTOR_SHIFT);
			__set_bit(i, active);
			break;
		}
	}

	if (num_sequential > 0)
		cache->seq_zone = true;

	if (num_conventional > 0) {
		/* Zone capacity is always zone size in emulation */
		cache->zone_capacity = cache->length;
		ret = calculate_alloc_pointer(cache, &last_alloc, new);
		if (ret) {
			btrfs_err(fs_info,
			"zoned: failed to determine allocation offset of bg %llu",
				  cache->start);
			goto out;
		} else if (map->num_stripes == num_conventional) {
			cache->alloc_offset = last_alloc;
			set_bit(BLOCK_GROUP_FLAG_ZONE_IS_ACTIVE, &cache->runtime_flags);
			goto out;
		}
	}

	switch (map->type & BTRFS_BLOCK_GROUP_PROFILE_MASK) {
	case 0: /* single */
		if (alloc_offsets[0] == WP_MISSING_DEV) {
			btrfs_err(fs_info,
			"zoned: cannot recover write pointer for zone %llu",
				physical[0]);
			ret = -EIO;
			goto out;
		}
		cache->alloc_offset = alloc_offsets[0];
		cache->zone_capacity = caps[0];
		if (test_bit(0, active))
			set_bit(BLOCK_GROUP_FLAG_ZONE_IS_ACTIVE, &cache->runtime_flags);
		break;
	case BTRFS_BLOCK_GROUP_DUP:
		if (map->type & BTRFS_BLOCK_GROUP_DATA) {
			btrfs_err(fs_info, "zoned: profile DUP not yet supported on data bg");
			ret = -EINVAL;
			goto out;
		}
		if (alloc_offsets[0] == WP_MISSING_DEV) {
			btrfs_err(fs_info,
			"zoned: cannot recover write pointer for zone %llu",
				physical[0]);
			ret = -EIO;
			goto out;
		}
		if (alloc_offsets[1] == WP_MISSING_DEV) {
			btrfs_err(fs_info,
			"zoned: cannot recover write pointer for zone %llu",
				physical[1]);
			ret = -EIO;
			goto out;
		}
		if (alloc_offsets[0] != alloc_offsets[1]) {
			btrfs_err(fs_info,
			"zoned: write pointer offset mismatch of zones in DUP profile");
			ret = -EIO;
			goto out;
		}
		if (test_bit(0, active) != test_bit(1, active)) {
			if (!btrfs_zone_activate(cache)) {
				ret = -EIO;
				goto out;
			}
		} else {
			if (test_bit(0, active))
				set_bit(BLOCK_GROUP_FLAG_ZONE_IS_ACTIVE,
					&cache->runtime_flags);
		}
		cache->alloc_offset = alloc_offsets[0];
		cache->zone_capacity = min(caps[0], caps[1]);
		break;
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

	if (cache->alloc_offset > cache->zone_capacity) {
		btrfs_err(fs_info,
"zoned: invalid write pointer %llu (larger than zone capacity %llu) in block group %llu",
			  cache->alloc_offset, cache->zone_capacity,
			  cache->start);
		ret = -EIO;
	}

	/* An extent is allocated after the write pointer */
	if (!ret && num_conventional && last_alloc > cache->alloc_offset) {
		btrfs_err(fs_info,
			  "zoned: got wrong write pointer in BG %llu: %llu > %llu",
			  logical, last_alloc, cache->alloc_offset);
		ret = -EIO;
	}

	if (!ret) {
		cache->meta_write_pointer = cache->alloc_offset + cache->start;
		if (test_bit(BLOCK_GROUP_FLAG_ZONE_IS_ACTIVE, &cache->runtime_flags)) {
			btrfs_get_block_group(cache);
			spin_lock(&fs_info->zone_active_bgs_lock);
			list_add_tail(&cache->active_bg_list,
				      &fs_info->zone_active_bgs);
			spin_unlock(&fs_info->zone_active_bgs_lock);
		}
	} else {
		kfree(cache->physical_map);
		cache->physical_map = NULL;
	}
	bitmap_free(active);
	kfree(physical);
	kfree(caps);
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
	unusable = (cache->alloc_offset - cache->used) +
		   (cache->length - cache->zone_capacity);
	free = cache->zone_capacity - cache->alloc_offset;

	/* We only need ->free_space in ALLOC_SEQ block groups */
	cache->cached = BTRFS_CACHE_FINISHED;
	cache->free_space_ctl->free_space = free;
	cache->zone_unusable = unusable;
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

	if (!is_data_inode(&inode->vfs_inode))
		return false;

	/*
	 * Using REQ_OP_ZONE_APPNED for relocation can break assumptions on the
	 * extent layout the relocation code has.
	 * Furthermore we have set aside own block-group from which only the
	 * relocation "process" can allocate and make sure only one process at a
	 * time can add pages to an extent that gets relocated, so it's safe to
	 * use regular REQ_OP_WRITE for this special case.
	 */
	if (btrfs_is_data_reloc_root(inode->root))
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

	cache = btrfs_lookup_block_group(fs_info, eb->start);
	if (!cache)
		return true;

	if (cache->meta_write_pointer != eb->start) {
		btrfs_put_block_group(cache);
		cache = NULL;
		ret = false;
	} else {
		cache->meta_write_pointer = eb->start + eb->len;
	}

	*cache_ret = cache;

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
	struct btrfs_io_context *bioc = NULL;
	u64 mapped_length = PAGE_SIZE;
	unsigned int nofs_flag;
	int nmirrors;
	int i, ret;

	ret = btrfs_map_sblock(fs_info, BTRFS_MAP_GET_READ_MIRRORS, logical,
			       &mapped_length, &bioc);
	if (ret || !bioc || mapped_length < PAGE_SIZE) {
		ret = -EIO;
		goto out_put_bioc;
	}

	if (bioc->map_type & BTRFS_BLOCK_GROUP_RAID56_MASK) {
		ret = -EINVAL;
		goto out_put_bioc;
	}

	nofs_flag = memalloc_nofs_save();
	nmirrors = (int)bioc->num_stripes;
	for (i = 0; i < nmirrors; i++) {
		u64 physical = bioc->stripes[i].physical;
		struct btrfs_device *dev = bioc->stripes[i].dev;

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
out_put_bioc:
	btrfs_put_bioc(bioc);
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
	device = map->stripes[0].dev;

	free_extent_map(em);

	return device;
}

/**
 * Activate block group and underlying device zones
 *
 * @block_group: the block group to activate
 *
 * Return: true on success, false otherwise
 */
bool btrfs_zone_activate(struct btrfs_block_group *block_group)
{
	struct btrfs_fs_info *fs_info = block_group->fs_info;
	struct btrfs_space_info *space_info = block_group->space_info;
	struct map_lookup *map;
	struct btrfs_device *device;
	u64 physical;
	bool ret;
	int i;

	if (!btrfs_is_zoned(block_group->fs_info))
		return true;

	map = block_group->physical_map;

	spin_lock(&space_info->lock);
	spin_lock(&block_group->lock);
	if (test_bit(BLOCK_GROUP_FLAG_ZONE_IS_ACTIVE, &block_group->runtime_flags)) {
		ret = true;
		goto out_unlock;
	}

	/* No space left */
	if (btrfs_zoned_bg_is_full(block_group)) {
		ret = false;
		goto out_unlock;
	}

	for (i = 0; i < map->num_stripes; i++) {
		device = map->stripes[i].dev;
		physical = map->stripes[i].physical;

		if (device->zone_info->max_active_zones == 0)
			continue;

		if (!btrfs_dev_set_active_zone(device, physical)) {
			/* Cannot activate the zone */
			ret = false;
			goto out_unlock;
		}
	}

	/* Successfully activated all the zones */
	set_bit(BLOCK_GROUP_FLAG_ZONE_IS_ACTIVE, &block_group->runtime_flags);
	space_info->active_total_bytes += block_group->length;
	spin_unlock(&block_group->lock);
	btrfs_try_granting_tickets(fs_info, space_info);
	spin_unlock(&space_info->lock);

	/* For the active block group list */
	btrfs_get_block_group(block_group);

	spin_lock(&fs_info->zone_active_bgs_lock);
	list_add_tail(&block_group->active_bg_list, &fs_info->zone_active_bgs);
	spin_unlock(&fs_info->zone_active_bgs_lock);

	return true;

out_unlock:
	spin_unlock(&block_group->lock);
	spin_unlock(&space_info->lock);
	return ret;
}

static void wait_eb_writebacks(struct btrfs_block_group *block_group)
{
	struct btrfs_fs_info *fs_info = block_group->fs_info;
	const u64 end = block_group->start + block_group->length;
	struct radix_tree_iter iter;
	struct extent_buffer *eb;
	void __rcu **slot;

	rcu_read_lock();
	radix_tree_for_each_slot(slot, &fs_info->buffer_radix, &iter,
				 block_group->start >> fs_info->sectorsize_bits) {
		eb = radix_tree_deref_slot(slot);
		if (!eb)
			continue;
		if (radix_tree_deref_retry(eb)) {
			slot = radix_tree_iter_retry(&iter);
			continue;
		}

		if (eb->start < block_group->start)
			continue;
		if (eb->start >= end)
			break;

		slot = radix_tree_iter_resume(slot, &iter);
		rcu_read_unlock();
		wait_on_extent_buffer_writeback(eb);
		rcu_read_lock();
	}
	rcu_read_unlock();
}

static int do_zone_finish(struct btrfs_block_group *block_group, bool fully_written)
{
	struct btrfs_fs_info *fs_info = block_group->fs_info;
	struct map_lookup *map;
	const bool is_metadata = (block_group->flags &
			(BTRFS_BLOCK_GROUP_METADATA | BTRFS_BLOCK_GROUP_SYSTEM));
	int ret = 0;
	int i;

	spin_lock(&block_group->lock);
	if (!test_bit(BLOCK_GROUP_FLAG_ZONE_IS_ACTIVE, &block_group->runtime_flags)) {
		spin_unlock(&block_group->lock);
		return 0;
	}

	/* Check if we have unwritten allocated space */
	if (is_metadata &&
	    block_group->start + block_group->alloc_offset > block_group->meta_write_pointer) {
		spin_unlock(&block_group->lock);
		return -EAGAIN;
	}

	/*
	 * If we are sure that the block group is full (= no more room left for
	 * new allocation) and the IO for the last usable block is completed, we
	 * don't need to wait for the other IOs. This holds because we ensure
	 * the sequential IO submissions using the ZONE_APPEND command for data
	 * and block_group->meta_write_pointer for metadata.
	 */
	if (!fully_written) {
		spin_unlock(&block_group->lock);

		ret = btrfs_inc_block_group_ro(block_group, false);
		if (ret)
			return ret;

		/* Ensure all writes in this block group finish */
		btrfs_wait_block_group_reservations(block_group);
		/* No need to wait for NOCOW writers. Zoned mode does not allow that */
		btrfs_wait_ordered_roots(fs_info, U64_MAX, block_group->start,
					 block_group->length);
		/* Wait for extent buffers to be written. */
		if (is_metadata)
			wait_eb_writebacks(block_group);

		spin_lock(&block_group->lock);

		/*
		 * Bail out if someone already deactivated the block group, or
		 * allocated space is left in the block group.
		 */
		if (!test_bit(BLOCK_GROUP_FLAG_ZONE_IS_ACTIVE,
			      &block_group->runtime_flags)) {
			spin_unlock(&block_group->lock);
			btrfs_dec_block_group_ro(block_group);
			return 0;
		}

		if (block_group->reserved) {
			spin_unlock(&block_group->lock);
			btrfs_dec_block_group_ro(block_group);
			return -EAGAIN;
		}
	}

	clear_bit(BLOCK_GROUP_FLAG_ZONE_IS_ACTIVE, &block_group->runtime_flags);
	block_group->alloc_offset = block_group->zone_capacity;
	block_group->free_space_ctl->free_space = 0;
	btrfs_clear_treelog_bg(block_group);
	btrfs_clear_data_reloc_bg(block_group);
	spin_unlock(&block_group->lock);

	map = block_group->physical_map;
	for (i = 0; i < map->num_stripes; i++) {
		struct btrfs_device *device = map->stripes[i].dev;
		const u64 physical = map->stripes[i].physical;

		if (device->zone_info->max_active_zones == 0)
			continue;

		ret = blkdev_zone_mgmt(device->bdev, REQ_OP_ZONE_FINISH,
				       physical >> SECTOR_SHIFT,
				       device->zone_info->zone_size >> SECTOR_SHIFT,
				       GFP_NOFS);

		if (ret)
			return ret;

		btrfs_dev_clear_active_zone(device, physical);
	}

	if (!fully_written)
		btrfs_dec_block_group_ro(block_group);

	spin_lock(&fs_info->zone_active_bgs_lock);
	ASSERT(!list_empty(&block_group->active_bg_list));
	list_del_init(&block_group->active_bg_list);
	spin_unlock(&fs_info->zone_active_bgs_lock);

	/* For active_bg_list */
	btrfs_put_block_group(block_group);

	clear_and_wake_up_bit(BTRFS_FS_NEED_ZONE_FINISH, &fs_info->flags);

	return 0;
}

int btrfs_zone_finish(struct btrfs_block_group *block_group)
{
	if (!btrfs_is_zoned(block_group->fs_info))
		return 0;

	return do_zone_finish(block_group, false);
}

bool btrfs_can_activate_zone(struct btrfs_fs_devices *fs_devices, u64 flags)
{
	struct btrfs_fs_info *fs_info = fs_devices->fs_info;
	struct btrfs_device *device;
	bool ret = false;

	if (!btrfs_is_zoned(fs_info))
		return true;

	/* Check if there is a device with active zones left */
	mutex_lock(&fs_info->chunk_mutex);
	list_for_each_entry(device, &fs_devices->alloc_list, dev_alloc_list) {
		struct btrfs_zoned_device_info *zinfo = device->zone_info;

		if (!device->bdev)
			continue;

		if (!zinfo->max_active_zones ||
		    atomic_read(&zinfo->active_zones_left)) {
			ret = true;
			break;
		}
	}
	mutex_unlock(&fs_info->chunk_mutex);

	if (!ret)
		set_bit(BTRFS_FS_NEED_ZONE_FINISH, &fs_info->flags);

	return ret;
}

void btrfs_zone_finish_endio(struct btrfs_fs_info *fs_info, u64 logical, u64 length)
{
	struct btrfs_block_group *block_group;
	u64 min_alloc_bytes;

	if (!btrfs_is_zoned(fs_info))
		return;

	block_group = btrfs_lookup_block_group(fs_info, logical);
	ASSERT(block_group);

	/* No MIXED_BG on zoned btrfs. */
	if (block_group->flags & BTRFS_BLOCK_GROUP_DATA)
		min_alloc_bytes = fs_info->sectorsize;
	else
		min_alloc_bytes = fs_info->nodesize;

	/* Bail out if we can allocate more data from this block group. */
	if (logical + length + min_alloc_bytes <=
	    block_group->start + block_group->zone_capacity)
		goto out;

	do_zone_finish(block_group, true);

out:
	btrfs_put_block_group(block_group);
}

static void btrfs_zone_finish_endio_workfn(struct work_struct *work)
{
	struct btrfs_block_group *bg =
		container_of(work, struct btrfs_block_group, zone_finish_work);

	wait_on_extent_buffer_writeback(bg->last_eb);
	free_extent_buffer(bg->last_eb);
	btrfs_zone_finish_endio(bg->fs_info, bg->start, bg->length);
	btrfs_put_block_group(bg);
}

void btrfs_schedule_zone_finish_bg(struct btrfs_block_group *bg,
				   struct extent_buffer *eb)
{
	if (!bg->seq_zone || eb->start + eb->len * 2 <= bg->start + bg->zone_capacity)
		return;

	if (WARN_ON(bg->zone_finish_work.func == btrfs_zone_finish_endio_workfn)) {
		btrfs_err(bg->fs_info, "double scheduling of bg %llu zone finishing",
			  bg->start);
		return;
	}

	/* For the work */
	btrfs_get_block_group(bg);
	atomic_inc(&eb->refs);
	bg->last_eb = eb;
	INIT_WORK(&bg->zone_finish_work, btrfs_zone_finish_endio_workfn);
	queue_work(system_unbound_wq, &bg->zone_finish_work);
}

void btrfs_clear_data_reloc_bg(struct btrfs_block_group *bg)
{
	struct btrfs_fs_info *fs_info = bg->fs_info;

	spin_lock(&fs_info->relocation_bg_lock);
	if (fs_info->data_reloc_bg == bg->start)
		fs_info->data_reloc_bg = 0;
	spin_unlock(&fs_info->relocation_bg_lock);
}

void btrfs_free_zone_cache(struct btrfs_fs_info *fs_info)
{
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	struct btrfs_device *device;

	if (!btrfs_is_zoned(fs_info))
		return;

	mutex_lock(&fs_devices->device_list_mutex);
	list_for_each_entry(device, &fs_devices->devices, dev_list) {
		if (device->zone_info) {
			vfree(device->zone_info->zone_cache);
			device->zone_info->zone_cache = NULL;
		}
	}
	mutex_unlock(&fs_devices->device_list_mutex);
}

bool btrfs_zoned_should_reclaim(struct btrfs_fs_info *fs_info)
{
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	struct btrfs_device *device;
	u64 used = 0;
	u64 total = 0;
	u64 factor;

	ASSERT(btrfs_is_zoned(fs_info));

	if (fs_info->bg_reclaim_threshold == 0)
		return false;

	mutex_lock(&fs_devices->device_list_mutex);
	list_for_each_entry(device, &fs_devices->devices, dev_list) {
		if (!device->bdev)
			continue;

		total += device->disk_total_bytes;
		used += device->bytes_used;
	}
	mutex_unlock(&fs_devices->device_list_mutex);

	factor = div64_u64(used * 100, total);
	return factor >= fs_info->bg_reclaim_threshold;
}

void btrfs_zoned_release_data_reloc_bg(struct btrfs_fs_info *fs_info, u64 logical,
				       u64 length)
{
	struct btrfs_block_group *block_group;

	if (!btrfs_is_zoned(fs_info))
		return;

	block_group = btrfs_lookup_block_group(fs_info, logical);
	/* It should be called on a previous data relocation block group. */
	ASSERT(block_group && (block_group->flags & BTRFS_BLOCK_GROUP_DATA));

	spin_lock(&block_group->lock);
	if (!test_bit(BLOCK_GROUP_FLAG_ZONED_DATA_RELOC, &block_group->runtime_flags))
		goto out;

	/* All relocation extents are written. */
	if (block_group->start + block_group->alloc_offset == logical + length) {
		/* Now, release this block group for further allocations. */
		clear_bit(BLOCK_GROUP_FLAG_ZONED_DATA_RELOC,
			  &block_group->runtime_flags);
	}

out:
	spin_unlock(&block_group->lock);
	btrfs_put_block_group(block_group);
}

int btrfs_zone_finish_one_bg(struct btrfs_fs_info *fs_info)
{
	struct btrfs_block_group *block_group;
	struct btrfs_block_group *min_bg = NULL;
	u64 min_avail = U64_MAX;
	int ret;

	spin_lock(&fs_info->zone_active_bgs_lock);
	list_for_each_entry(block_group, &fs_info->zone_active_bgs,
			    active_bg_list) {
		u64 avail;

		spin_lock(&block_group->lock);
		if (block_group->reserved ||
		    (block_group->flags & BTRFS_BLOCK_GROUP_SYSTEM)) {
			spin_unlock(&block_group->lock);
			continue;
		}

		avail = block_group->zone_capacity - block_group->alloc_offset;
		if (min_avail > avail) {
			if (min_bg)
				btrfs_put_block_group(min_bg);
			min_bg = block_group;
			min_avail = avail;
			btrfs_get_block_group(min_bg);
		}
		spin_unlock(&block_group->lock);
	}
	spin_unlock(&fs_info->zone_active_bgs_lock);

	if (!min_bg)
		return 0;

	ret = btrfs_zone_finish(min_bg);
	btrfs_put_block_group(min_bg);

	return ret < 0 ? ret : 1;
}

int btrfs_zoned_activate_one_bg(struct btrfs_fs_info *fs_info,
				struct btrfs_space_info *space_info,
				bool do_finish)
{
	struct btrfs_block_group *bg;
	int index;

	if (!btrfs_is_zoned(fs_info) || (space_info->flags & BTRFS_BLOCK_GROUP_DATA))
		return 0;

	/* No more block groups to activate */
	if (space_info->active_total_bytes == space_info->total_bytes)
		return 0;

	for (;;) {
		int ret;
		bool need_finish = false;

		down_read(&space_info->groups_sem);
		for (index = 0; index < BTRFS_NR_RAID_TYPES; index++) {
			list_for_each_entry(bg, &space_info->block_groups[index],
					    list) {
				if (!spin_trylock(&bg->lock))
					continue;
				if (btrfs_zoned_bg_is_full(bg) ||
				    test_bit(BLOCK_GROUP_FLAG_ZONE_IS_ACTIVE,
					     &bg->runtime_flags)) {
					spin_unlock(&bg->lock);
					continue;
				}
				spin_unlock(&bg->lock);

				if (btrfs_zone_activate(bg)) {
					up_read(&space_info->groups_sem);
					return 1;
				}

				need_finish = true;
			}
		}
		up_read(&space_info->groups_sem);

		if (!do_finish || !need_finish)
			break;

		ret = btrfs_zone_finish_one_bg(fs_info);
		if (ret == 0)
			break;
		if (ret < 0)
			return ret;
	}

	return 0;
}

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2014-2026 Christoph Hellwig.
 *
 * Support for exportfs-based layout grants for direct block device access.
 */
#ifndef LINUX_EXPORTFS_BLOCK_H
#define LINUX_EXPORTFS_BLOCK_H 1

#include <linux/blkdev.h>
#include <linux/exportfs.h>
#include <linux/fs.h>

struct inode;
struct iomap;
struct super_block;

/*
 * There are the two types of block-style layout support:
 *  - In-band implies a device identified by a unique cookie inside the actual
 *    device address space checked by the ->get_uuid method as used by the pNFS
 *    block layout.  This is a bit dangerous and deprecated.
 *  - Out of band implies identification by out of band unique identifiers
 *    specified by the storage protocol, which is much safer and used by the
 *    pNFS SCSI/NVMe layouts.
 */
typedef unsigned int __bitwise expfs_block_layouts_t;
#define EXPFS_BLOCK_FLAG(__bit) \
	((__force expfs_block_layouts_t)(1u << __bit))
#define EXPFS_BLOCK_IN_BAND_ID		EXPFS_BLOCK_FLAG(0)
#define EXPFS_BLOCK_OUT_OF_BAND_ID	EXPFS_BLOCK_FLAG(1)

struct exportfs_block_ops {
	/*
	 * Returns the EXPFS_BLOCK_* bitmap of supported layout types.
	 */
	expfs_block_layouts_t (*layouts_supported)(struct super_block *sb);

	/*
	 * Get the in-band device unique signature exposed to clients.
	 */
	int (*get_uuid)(struct super_block *sb, u8 *buf, u32 *len, u64 *offset);

	/*
	 * Map blocks for direct block access.
	 * If @write is %true, also allocate the blocks for the range if needed.
	 */
	int (*map_blocks)(struct inode *inode, loff_t offset, u64 len,
			struct iomap *iomap, bool write,
			u32 *device_generation);

	/*
	 * Commit blocks previously handed out by ->map_blocks and written to by
	 * the client.
	 */
	int (*commit_blocks)(struct inode *inode, struct iomap *iomaps,
			int nr_iomaps, loff_t new_size);
};

static inline bool
exportfs_bdev_supports_out_of_band_id(struct block_device *bdev)
{
	return bdev->bd_disk->fops->pr_ops &&
		bdev->bd_disk->fops->get_unique_id;
}

#ifdef CONFIG_EXPORTFS_BLOCK_OPS
static inline expfs_block_layouts_t
exportfs_layouts_supported(struct super_block *sb)
{
	const struct exportfs_block_ops *bops = sb->s_export_op->block_ops;

	if (!bops ||
	    !bops->layouts_supported ||
	    WARN_ON_ONCE(!bops->map_blocks) ||
	    WARN_ON_ONCE(!bops->commit_blocks))
		return 0;
	return bops->layouts_supported(sb);
}
#else
static inline expfs_block_layouts_t
exportfs_layouts_supported(struct super_block *sb)
{
	return 0;
}
#endif /* CONFIG_EXPORTFS_BLOCK_OPS */

#endif /* LINUX_EXPORTFS_BLOCK_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _UDF_I_H
#define _UDF_I_H

struct extent_position {
	struct buffer_head *bh;
	uint32_t offset;
	struct kernel_lb_addr block;
};

struct udf_ext_cache {
	/* Extent position */
	struct extent_position epos;
	/* Start logical offset in bytes */
	loff_t lstart;
};

/*
 * The i_data_sem and i_mutex serve for protection of allocation information
 * of a regular files and symlinks. This includes all extents belonging to
 * the file/symlink, a fact whether data are in-inode or in external data
 * blocks, preallocation, goal block information... When extents are read,
 * i_mutex or i_data_sem must be held (for reading is enough in case of
 * i_data_sem). When extents are changed, i_data_sem must be held for writing
 * and also i_mutex must be held.
 *
 * For directories i_mutex is used for all the necessary protection.
 */

struct udf_inode_info {
	struct timespec64	i_crtime;
	/* Physical address of inode */
	struct kernel_lb_addr		i_location;
	__u64			i_unique;
	__u32			i_lenEAttr;
	__u32			i_lenAlloc;
	__u64			i_lenExtents;
	__u32			i_next_alloc_block;
	__u32			i_next_alloc_goal;
	__u32			i_checkpoint;
	unsigned		i_alloc_type : 3;
	unsigned		i_efe : 1;	/* extendedFileEntry */
	unsigned		i_use : 1;	/* unallocSpaceEntry */
	unsigned		i_strat4096 : 1;
	unsigned		i_streamdir : 1;
	unsigned		reserved : 25;
	union {
		struct short_ad	*i_sad;
		struct long_ad		*i_lad;
		__u8		*i_data;
	} i_ext;
	struct kernel_lb_addr	i_locStreamdir;
	__u64			i_lenStreams;
	struct rw_semaphore	i_data_sem;
	struct udf_ext_cache cached_extent;
	/* Spinlock for protecting extent cache */
	spinlock_t i_extent_cache_lock;
	struct inode vfs_inode;
};

static inline struct udf_inode_info *UDF_I(struct inode *inode)
{
	return container_of(inode, struct udf_inode_info, vfs_inode);
}

#endif /* _UDF_I_H) */

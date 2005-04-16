#ifndef _VXFS_KCOMPAT_H
#define _VXFS_KCOMPAT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))

#include <linux/blkdev.h>

typedef long sector_t;

/* From include/linux/fs.h (Linux 2.5.2-pre3)  */
static inline struct buffer_head * sb_bread(struct super_block *sb, int block)
{
	return bread(sb->s_dev, block, sb->s_blocksize);
}

/* Dito.  */
static inline void map_bh(struct buffer_head *bh, struct super_block *sb, int block)
{
	bh->b_state |= 1 << BH_Mapped;
	bh->b_dev = sb->s_dev;
	bh->b_blocknr = block;
}

/* From fs/block_dev.c (Linux 2.5.2-pre2)  */
static inline int sb_set_blocksize(struct super_block *sb, int size)
{
	int bits;
	if (set_blocksize(sb->s_dev, size) < 0)
		return 0;
	sb->s_blocksize = size;
	for (bits = 9, size >>= 9; size >>= 1; bits++)
		;
	sb->s_blocksize_bits = bits;
	return sb->s_blocksize;
}

/* Dito.  */
static inline int sb_min_blocksize(struct super_block *sb, int size)
{
	int minsize = get_hardsect_size(sb->s_dev);
	if (size < minsize)
		size = minsize;
	return sb_set_blocksize(sb, size);
}

#endif /* Kernel 2.4 */
#endif /* _VXFS_KCOMPAT_H */

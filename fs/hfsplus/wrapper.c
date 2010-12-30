/*
 *  linux/fs/hfsplus/wrapper.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 *
 * Handling of HFS wrappers around HFS+ volumes
 */

#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/cdrom.h>
#include <linux/genhd.h>
#include <asm/unaligned.h>

#include "hfsplus_fs.h"
#include "hfsplus_raw.h"

struct hfsplus_wd {
	u32 ablk_size;
	u16 ablk_start;
	u16 embed_start;
	u16 embed_count;
};

static int hfsplus_read_mdb(void *bufptr, struct hfsplus_wd *wd)
{
	u32 extent;
	u16 attrib;
	__be16 sig;

	sig = *(__be16 *)(bufptr + HFSP_WRAPOFF_EMBEDSIG);
	if (sig != cpu_to_be16(HFSPLUS_VOLHEAD_SIG) &&
	    sig != cpu_to_be16(HFSPLUS_VOLHEAD_SIGX))
		return 0;

	attrib = be16_to_cpu(*(__be16 *)(bufptr + HFSP_WRAPOFF_ATTRIB));
	if (!(attrib & HFSP_WRAP_ATTRIB_SLOCK) ||
	   !(attrib & HFSP_WRAP_ATTRIB_SPARED))
		return 0;

	wd->ablk_size = be32_to_cpu(*(__be32 *)(bufptr + HFSP_WRAPOFF_ABLKSIZE));
	if (wd->ablk_size < HFSPLUS_SECTOR_SIZE)
		return 0;
	if (wd->ablk_size % HFSPLUS_SECTOR_SIZE)
		return 0;
	wd->ablk_start = be16_to_cpu(*(__be16 *)(bufptr + HFSP_WRAPOFF_ABLKSTART));

	extent = get_unaligned_be32(bufptr + HFSP_WRAPOFF_EMBEDEXT);
	wd->embed_start = (extent >> 16) & 0xFFFF;
	wd->embed_count = extent & 0xFFFF;

	return 1;
}

static int hfsplus_get_last_session(struct super_block *sb,
				    sector_t *start, sector_t *size)
{
	struct cdrom_multisession ms_info;
	struct cdrom_tocentry te;
	int res;

	/* default values */
	*start = 0;
	*size = sb->s_bdev->bd_inode->i_size >> 9;

	if (HFSPLUS_SB(sb)->session >= 0) {
		te.cdte_track = HFSPLUS_SB(sb)->session;
		te.cdte_format = CDROM_LBA;
		res = ioctl_by_bdev(sb->s_bdev, CDROMREADTOCENTRY, (unsigned long)&te);
		if (!res && (te.cdte_ctrl & CDROM_DATA_TRACK) == 4) {
			*start = (sector_t)te.cdte_addr.lba << 2;
			return 0;
		}
		printk(KERN_ERR "hfs: invalid session number or type of track\n");
		return -EINVAL;
	}
	ms_info.addr_format = CDROM_LBA;
	res = ioctl_by_bdev(sb->s_bdev, CDROMMULTISESSION, (unsigned long)&ms_info);
	if (!res && ms_info.xa_flag)
		*start = (sector_t)ms_info.addr.lba << 2;
	return 0;
}

/* Find the volume header and fill in some minimum bits in superblock */
/* Takes in super block, returns true if good data read */
int hfsplus_read_wrapper(struct super_block *sb)
{
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(sb);
	struct buffer_head *bh;
	struct hfsplus_vh *vhdr;
	struct hfsplus_wd wd;
	sector_t part_start, part_size;
	u32 blocksize;

	blocksize = sb_min_blocksize(sb, HFSPLUS_SECTOR_SIZE);
	if (!blocksize)
		return -EINVAL;

	if (hfsplus_get_last_session(sb, &part_start, &part_size))
		return -EINVAL;
	if ((u64)part_start + part_size > 0x100000000ULL) {
		pr_err("hfs: volumes larger than 2TB are not supported yet\n");
		return -EINVAL;
	}
	while (1) {
		bh = sb_bread512(sb, part_start + HFSPLUS_VOLHEAD_SECTOR, vhdr);
		if (!bh)
			return -EIO;

		if (vhdr->signature == cpu_to_be16(HFSP_WRAP_MAGIC)) {
			if (!hfsplus_read_mdb(vhdr, &wd))
				goto error;
			wd.ablk_size >>= HFSPLUS_SECTOR_SHIFT;
			part_start += wd.ablk_start + wd.embed_start * wd.ablk_size;
			part_size = wd.embed_count * wd.ablk_size;
			brelse(bh);
			bh = sb_bread512(sb, part_start + HFSPLUS_VOLHEAD_SECTOR, vhdr);
			if (!bh)
				return -EIO;
		}
		if (vhdr->signature == cpu_to_be16(HFSPLUS_VOLHEAD_SIG))
			break;
		if (vhdr->signature == cpu_to_be16(HFSPLUS_VOLHEAD_SIGX)) {
			set_bit(HFSPLUS_SB_HFSX, &sbi->flags);
			break;
		}
		brelse(bh);

		/* check for a partition block
		 * (should do this only for cdrom/loop though)
		 */
		if (hfs_part_find(sb, &part_start, &part_size))
			return -EINVAL;
	}

	blocksize = be32_to_cpu(vhdr->blocksize);
	brelse(bh);

	/* block size must be at least as large as a sector
	 * and a multiple of 2
	 */
	if (blocksize < HFSPLUS_SECTOR_SIZE ||
	    ((blocksize - 1) & blocksize))
		return -EINVAL;
	sbi->alloc_blksz = blocksize;
	sbi->alloc_blksz_shift = 0;
	while ((blocksize >>= 1) != 0)
		sbi->alloc_blksz_shift++;
	blocksize = min(sbi->alloc_blksz, (u32)PAGE_SIZE);

	/* align block size to block offset */
	while (part_start & ((blocksize >> HFSPLUS_SECTOR_SHIFT) - 1))
		blocksize >>= 1;

	if (sb_set_blocksize(sb, blocksize) != blocksize) {
		printk(KERN_ERR "hfs: unable to set blocksize to %u!\n", blocksize);
		return -EINVAL;
	}

	sbi->blockoffset =
		part_start >> (sb->s_blocksize_bits - HFSPLUS_SECTOR_SHIFT);
	sbi->sect_count = part_size;
	sbi->fs_shift = sbi->alloc_blksz_shift - sb->s_blocksize_bits;

	bh = sb_bread512(sb, part_start + HFSPLUS_VOLHEAD_SECTOR, vhdr);
	if (!bh)
		return -EIO;

	/* should still be the same... */
	if (test_bit(HFSPLUS_SB_HFSX, &sbi->flags)) {
		if (vhdr->signature != cpu_to_be16(HFSPLUS_VOLHEAD_SIGX))
			goto error;
	} else {
		if (vhdr->signature != cpu_to_be16(HFSPLUS_VOLHEAD_SIG))
			goto error;
	}

	sbi->s_vhbh = bh;
	sbi->s_vhdr = vhdr;

	return 0;
 error:
	brelse(bh);
	return -EINVAL;
}

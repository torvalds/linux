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

/**
 * hfsplus_submit_bio - Perform block I/O
 * @sb: super block of volume for I/O
 * @sector: block to read or write, for blocks of HFSPLUS_SECTOR_SIZE bytes
 * @buf: buffer for I/O
 * @data: output pointer for location of requested data
 * @op: direction of I/O
 * @op_flags: request op flags
 *
 * The unit of I/O is hfsplus_min_io_size(sb), which may be bigger than
 * HFSPLUS_SECTOR_SIZE, and @buf must be sized accordingly. On reads
 * @data will return a pointer to the start of the requested sector,
 * which may not be the same location as @buf.
 *
 * If @sector is not aligned to the bdev logical block size it will
 * be rounded down. For writes this means that @buf should contain data
 * that starts at the rounded-down address. As long as the data was
 * read using hfsplus_submit_bio() and the same buffer is used things
 * will work correctly.
 */
int hfsplus_submit_bio(struct super_block *sb, sector_t sector,
		       void *buf, void **data, int op, int op_flags)
{
	struct bio *bio;
	int ret = 0;
	u64 io_size;
	loff_t start;
	int offset;

	/*
	 * Align sector to hardware sector size and find offset. We
	 * assume that io_size is a power of two, which _should_
	 * be true.
	 */
	io_size = hfsplus_min_io_size(sb);
	start = (loff_t)sector << HFSPLUS_SECTOR_SHIFT;
	offset = start & (io_size - 1);
	sector &= ~((io_size >> HFSPLUS_SECTOR_SHIFT) - 1);

	bio = bio_alloc(GFP_NOIO, 1);
	bio->bi_iter.bi_sector = sector;
	bio->bi_bdev = sb->s_bdev;
	bio_set_op_attrs(bio, op, op_flags);

	if (op != WRITE && data)
		*data = (u8 *)buf + offset;

	while (io_size > 0) {
		unsigned int page_offset = offset_in_page(buf);
		unsigned int len = min_t(unsigned int, PAGE_SIZE - page_offset,
					 io_size);

		ret = bio_add_page(bio, virt_to_page(buf), len, page_offset);
		if (ret != len) {
			ret = -EIO;
			goto out;
		}
		io_size -= len;
		buf = (u8 *)buf + len;
	}

	ret = submit_bio_wait(bio);
out:
	bio_put(bio);
	return ret < 0 ? ret : 0;
}

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

	wd->ablk_size =
		be32_to_cpu(*(__be32 *)(bufptr + HFSP_WRAPOFF_ABLKSIZE));
	if (wd->ablk_size < HFSPLUS_SECTOR_SIZE)
		return 0;
	if (wd->ablk_size % HFSPLUS_SECTOR_SIZE)
		return 0;
	wd->ablk_start =
		be16_to_cpu(*(__be16 *)(bufptr + HFSP_WRAPOFF_ABLKSTART));

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
		res = ioctl_by_bdev(sb->s_bdev,
			CDROMREADTOCENTRY, (unsigned long)&te);
		if (!res && (te.cdte_ctrl & CDROM_DATA_TRACK) == 4) {
			*start = (sector_t)te.cdte_addr.lba << 2;
			return 0;
		}
		pr_err("invalid session number or type of track\n");
		return -EINVAL;
	}
	ms_info.addr_format = CDROM_LBA;
	res = ioctl_by_bdev(sb->s_bdev, CDROMMULTISESSION,
		(unsigned long)&ms_info);
	if (!res && ms_info.xa_flag)
		*start = (sector_t)ms_info.addr.lba << 2;
	return 0;
}

/* Find the volume header and fill in some minimum bits in superblock */
/* Takes in super block, returns true if good data read */
int hfsplus_read_wrapper(struct super_block *sb)
{
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(sb);
	struct hfsplus_wd wd;
	sector_t part_start, part_size;
	u32 blocksize;
	int error = 0;

	error = -EINVAL;
	blocksize = sb_min_blocksize(sb, HFSPLUS_SECTOR_SIZE);
	if (!blocksize)
		goto out;

	if (hfsplus_get_last_session(sb, &part_start, &part_size))
		goto out;

	error = -ENOMEM;
	sbi->s_vhdr_buf = kmalloc(hfsplus_min_io_size(sb), GFP_KERNEL);
	if (!sbi->s_vhdr_buf)
		goto out;
	sbi->s_backup_vhdr_buf = kmalloc(hfsplus_min_io_size(sb), GFP_KERNEL);
	if (!sbi->s_backup_vhdr_buf)
		goto out_free_vhdr;

reread:
	error = hfsplus_submit_bio(sb, part_start + HFSPLUS_VOLHEAD_SECTOR,
				   sbi->s_vhdr_buf, (void **)&sbi->s_vhdr,
				   REQ_OP_READ, 0);
	if (error)
		goto out_free_backup_vhdr;

	error = -EINVAL;
	switch (sbi->s_vhdr->signature) {
	case cpu_to_be16(HFSPLUS_VOLHEAD_SIGX):
		set_bit(HFSPLUS_SB_HFSX, &sbi->flags);
		/*FALLTHRU*/
	case cpu_to_be16(HFSPLUS_VOLHEAD_SIG):
		break;
	case cpu_to_be16(HFSP_WRAP_MAGIC):
		if (!hfsplus_read_mdb(sbi->s_vhdr, &wd))
			goto out_free_backup_vhdr;
		wd.ablk_size >>= HFSPLUS_SECTOR_SHIFT;
		part_start += (sector_t)wd.ablk_start +
			       (sector_t)wd.embed_start * wd.ablk_size;
		part_size = (sector_t)wd.embed_count * wd.ablk_size;
		goto reread;
	default:
		/*
		 * Check for a partition block.
		 *
		 * (should do this only for cdrom/loop though)
		 */
		if (hfs_part_find(sb, &part_start, &part_size))
			goto out_free_backup_vhdr;
		goto reread;
	}

	error = hfsplus_submit_bio(sb, part_start + part_size - 2,
				   sbi->s_backup_vhdr_buf,
				   (void **)&sbi->s_backup_vhdr, REQ_OP_READ,
				   0);
	if (error)
		goto out_free_backup_vhdr;

	error = -EINVAL;
	if (sbi->s_backup_vhdr->signature != sbi->s_vhdr->signature) {
		pr_warn("invalid secondary volume header\n");
		goto out_free_backup_vhdr;
	}

	blocksize = be32_to_cpu(sbi->s_vhdr->blocksize);

	/*
	 * Block size must be at least as large as a sector and a multiple of 2.
	 */
	if (blocksize < HFSPLUS_SECTOR_SIZE || ((blocksize - 1) & blocksize))
		goto out_free_backup_vhdr;
	sbi->alloc_blksz = blocksize;
	sbi->alloc_blksz_shift = ilog2(blocksize);
	blocksize = min_t(u32, sbi->alloc_blksz, PAGE_SIZE);

	/*
	 * Align block size to block offset.
	 */
	while (part_start & ((blocksize >> HFSPLUS_SECTOR_SHIFT) - 1))
		blocksize >>= 1;

	if (sb_set_blocksize(sb, blocksize) != blocksize) {
		pr_err("unable to set blocksize to %u!\n", blocksize);
		goto out_free_backup_vhdr;
	}

	sbi->blockoffset =
		part_start >> (sb->s_blocksize_bits - HFSPLUS_SECTOR_SHIFT);
	sbi->part_start = part_start;
	sbi->sect_count = part_size;
	sbi->fs_shift = sbi->alloc_blksz_shift - sb->s_blocksize_bits;
	return 0;

out_free_backup_vhdr:
	kfree(sbi->s_backup_vhdr_buf);
out_free_vhdr:
	kfree(sbi->s_vhdr_buf);
out:
	return error;
}

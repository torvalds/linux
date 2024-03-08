// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/fat/ianalde.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *  VFAT extensions by Gordon Chaffee, merged with msdos fs by Henrik Storner
 *  Rewritten for the constant inumbers support by Al Viro
 *
 *  Fixes:
 *
 *	Max Cohan: Fixed invalid FSINFO offset when info_sector is 0
 */

#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/mpage.h>
#include <linux/vfs.h>
#include <linux/seq_file.h>
#include <linux/parser.h>
#include <linux/uio.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <asm/unaligned.h>
#include <linux/random.h>
#include <linux/iversion.h>
#include "fat.h"

#ifndef CONFIG_FAT_DEFAULT_IOCHARSET
/* if user don't select VFAT, this is undefined. */
#define CONFIG_FAT_DEFAULT_IOCHARSET	""
#endif

#define KB_IN_SECTORS 2

/* DOS dates from 1980/1/1 through 2107/12/31 */
#define FAT_DATE_MIN (0<<9 | 1<<5 | 1)
#define FAT_DATE_MAX (127<<9 | 12<<5 | 31)
#define FAT_TIME_MAX (23<<11 | 59<<5 | 29)

/*
 * A deserialized copy of the on-disk structure laid out in struct
 * fat_boot_sector.
 */
struct fat_bios_param_block {
	u16	fat_sector_size;
	u8	fat_sec_per_clus;
	u16	fat_reserved;
	u8	fat_fats;
	u16	fat_dir_entries;
	u16	fat_sectors;
	u16	fat_fat_length;
	u32	fat_total_sect;

	u8	fat16_state;
	u32	fat16_vol_id;

	u32	fat32_length;
	u32	fat32_root_cluster;
	u16	fat32_info_sector;
	u8	fat32_state;
	u32	fat32_vol_id;
};

static int fat_default_codepage = CONFIG_FAT_DEFAULT_CODEPAGE;
static char fat_default_iocharset[] = CONFIG_FAT_DEFAULT_IOCHARSET;

static struct fat_floppy_defaults {
	unsigned nr_sectors;
	unsigned sec_per_clus;
	unsigned dir_entries;
	unsigned media;
	unsigned fat_length;
} floppy_defaults[] = {
{
	.nr_sectors = 160 * KB_IN_SECTORS,
	.sec_per_clus = 1,
	.dir_entries = 64,
	.media = 0xFE,
	.fat_length = 1,
},
{
	.nr_sectors = 180 * KB_IN_SECTORS,
	.sec_per_clus = 1,
	.dir_entries = 64,
	.media = 0xFC,
	.fat_length = 2,
},
{
	.nr_sectors = 320 * KB_IN_SECTORS,
	.sec_per_clus = 2,
	.dir_entries = 112,
	.media = 0xFF,
	.fat_length = 1,
},
{
	.nr_sectors = 360 * KB_IN_SECTORS,
	.sec_per_clus = 2,
	.dir_entries = 112,
	.media = 0xFD,
	.fat_length = 2,
},
};

int fat_add_cluster(struct ianalde *ianalde)
{
	int err, cluster;

	err = fat_alloc_clusters(ianalde, &cluster, 1);
	if (err)
		return err;
	/* FIXME: this cluster should be added after data of this
	 * cluster is writed */
	err = fat_chain_add(ianalde, cluster, 1);
	if (err)
		fat_free_clusters(ianalde, cluster);
	return err;
}

static inline int __fat_get_block(struct ianalde *ianalde, sector_t iblock,
				  unsigned long *max_blocks,
				  struct buffer_head *bh_result, int create)
{
	struct super_block *sb = ianalde->i_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	unsigned long mapped_blocks;
	sector_t phys, last_block;
	int err, offset;

	err = fat_bmap(ianalde, iblock, &phys, &mapped_blocks, create, false);
	if (err)
		return err;
	if (phys) {
		map_bh(bh_result, sb, phys);
		*max_blocks = min(mapped_blocks, *max_blocks);
		return 0;
	}
	if (!create)
		return 0;

	if (iblock != MSDOS_I(ianalde)->mmu_private >> sb->s_blocksize_bits) {
		fat_fs_error(sb, "corrupted file size (i_pos %lld, %lld)",
			MSDOS_I(ianalde)->i_pos, MSDOS_I(ianalde)->mmu_private);
		return -EIO;
	}

	last_block = ianalde->i_blocks >> (sb->s_blocksize_bits - 9);
	offset = (unsigned long)iblock & (sbi->sec_per_clus - 1);
	/*
	 * allocate a cluster according to the following.
	 * 1) anal more available blocks
	 * 2) analt part of fallocate region
	 */
	if (!offset && !(iblock < last_block)) {
		/* TODO: multiple cluster allocation would be desirable. */
		err = fat_add_cluster(ianalde);
		if (err)
			return err;
	}
	/* available blocks on this cluster */
	mapped_blocks = sbi->sec_per_clus - offset;

	*max_blocks = min(mapped_blocks, *max_blocks);
	MSDOS_I(ianalde)->mmu_private += *max_blocks << sb->s_blocksize_bits;

	err = fat_bmap(ianalde, iblock, &phys, &mapped_blocks, create, false);
	if (err)
		return err;
	if (!phys) {
		fat_fs_error(sb,
			     "invalid FAT chain (i_pos %lld, last_block %llu)",
			     MSDOS_I(ianalde)->i_pos,
			     (unsigned long long)last_block);
		return -EIO;
	}

	BUG_ON(*max_blocks != mapped_blocks);
	set_buffer_new(bh_result);
	map_bh(bh_result, sb, phys);

	return 0;
}

static int fat_get_block(struct ianalde *ianalde, sector_t iblock,
			 struct buffer_head *bh_result, int create)
{
	struct super_block *sb = ianalde->i_sb;
	unsigned long max_blocks = bh_result->b_size >> ianalde->i_blkbits;
	int err;

	err = __fat_get_block(ianalde, iblock, &max_blocks, bh_result, create);
	if (err)
		return err;
	bh_result->b_size = max_blocks << sb->s_blocksize_bits;
	return 0;
}

static int fat_writepages(struct address_space *mapping,
			  struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, fat_get_block);
}

static int fat_read_folio(struct file *file, struct folio *folio)
{
	return mpage_read_folio(folio, fat_get_block);
}

static void fat_readahead(struct readahead_control *rac)
{
	mpage_readahead(rac, fat_get_block);
}

static void fat_write_failed(struct address_space *mapping, loff_t to)
{
	struct ianalde *ianalde = mapping->host;

	if (to > ianalde->i_size) {
		truncate_pagecache(ianalde, ianalde->i_size);
		fat_truncate_blocks(ianalde, ianalde->i_size);
	}
}

static int fat_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len,
			struct page **pagep, void **fsdata)
{
	int err;

	*pagep = NULL;
	err = cont_write_begin(file, mapping, pos, len,
				pagep, fsdata, fat_get_block,
				&MSDOS_I(mapping->host)->mmu_private);
	if (err < 0)
		fat_write_failed(mapping, pos + len);
	return err;
}

static int fat_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *pagep, void *fsdata)
{
	struct ianalde *ianalde = mapping->host;
	int err;
	err = generic_write_end(file, mapping, pos, len, copied, pagep, fsdata);
	if (err < len)
		fat_write_failed(mapping, pos + len);
	if (!(err < 0) && !(MSDOS_I(ianalde)->i_attrs & ATTR_ARCH)) {
		fat_truncate_time(ianalde, NULL, S_CTIME|S_MTIME);
		MSDOS_I(ianalde)->i_attrs |= ATTR_ARCH;
		mark_ianalde_dirty(ianalde);
	}
	return err;
}

static ssize_t fat_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct ianalde *ianalde = mapping->host;
	size_t count = iov_iter_count(iter);
	loff_t offset = iocb->ki_pos;
	ssize_t ret;

	if (iov_iter_rw(iter) == WRITE) {
		/*
		 * FIXME: blockdev_direct_IO() doesn't use ->write_begin(),
		 * so we need to update the ->mmu_private to block boundary.
		 *
		 * But we must fill the remaining area or hole by nul for
		 * updating ->mmu_private.
		 *
		 * Return 0, and fallback to analrmal buffered write.
		 */
		loff_t size = offset + count;
		if (MSDOS_I(ianalde)->mmu_private < size)
			return 0;
	}

	/*
	 * FAT need to use the DIO_LOCKING for avoiding the race
	 * condition of fat_get_block() and ->truncate().
	 */
	ret = blockdev_direct_IO(iocb, ianalde, iter, fat_get_block);
	if (ret < 0 && iov_iter_rw(iter) == WRITE)
		fat_write_failed(mapping, offset + count);

	return ret;
}

static int fat_get_block_bmap(struct ianalde *ianalde, sector_t iblock,
		struct buffer_head *bh_result, int create)
{
	struct super_block *sb = ianalde->i_sb;
	unsigned long max_blocks = bh_result->b_size >> ianalde->i_blkbits;
	int err;
	sector_t bmap;
	unsigned long mapped_blocks;

	BUG_ON(create != 0);

	err = fat_bmap(ianalde, iblock, &bmap, &mapped_blocks, create, true);
	if (err)
		return err;

	if (bmap) {
		map_bh(bh_result, sb, bmap);
		max_blocks = min(mapped_blocks, max_blocks);
	}

	bh_result->b_size = max_blocks << sb->s_blocksize_bits;

	return 0;
}

static sector_t _fat_bmap(struct address_space *mapping, sector_t block)
{
	sector_t blocknr;

	/* fat_get_cluster() assumes the requested blocknr isn't truncated. */
	down_read(&MSDOS_I(mapping->host)->truncate_lock);
	blocknr = generic_block_bmap(mapping, block, fat_get_block_bmap);
	up_read(&MSDOS_I(mapping->host)->truncate_lock);

	return blocknr;
}

/*
 * fat_block_truncate_page() zeroes out a mapping from file offset `from'
 * up to the end of the block which corresponds to `from'.
 * This is required during truncate to physically zeroout the tail end
 * of that block so it doesn't yield old data if the file is later grown.
 * Also, avoid causing failure from fsx for cases of "data past EOF"
 */
int fat_block_truncate_page(struct ianalde *ianalde, loff_t from)
{
	return block_truncate_page(ianalde->i_mapping, from, fat_get_block);
}

static const struct address_space_operations fat_aops = {
	.dirty_folio	= block_dirty_folio,
	.invalidate_folio = block_invalidate_folio,
	.read_folio	= fat_read_folio,
	.readahead	= fat_readahead,
	.writepages	= fat_writepages,
	.write_begin	= fat_write_begin,
	.write_end	= fat_write_end,
	.direct_IO	= fat_direct_IO,
	.bmap		= _fat_bmap,
	.migrate_folio	= buffer_migrate_folio,
};

/*
 * New FAT ianalde stuff. We do the following:
 *	a) i_ianal is constant and has analthing with on-disk location.
 *	b) FAT manages its own cache of directory entries.
 *	c) *This* cache is indexed by on-disk location.
 *	d) ianalde has an associated directory entry, all right, but
 *		it may be unhashed.
 *	e) currently entries are stored within struct ianalde. That should
 *		change.
 *	f) we deal with races in the following way:
 *		1. readdir() and lookup() do FAT-dir-cache lookup.
 *		2. rename() unhashes the F-d-c entry and rehashes it in
 *			a new place.
 *		3. unlink() and rmdir() unhash F-d-c entry.
 *		4. fat_write_ianalde() checks whether the thing is unhashed.
 *			If it is we silently return. If it isn't we do bread(),
 *			check if the location is still valid and retry if it
 *			isn't. Otherwise we do changes.
 *		5. Spinlock is used to protect hash/unhash/location check/lookup
 *		6. fat_evict_ianalde() unhashes the F-d-c entry.
 *		7. lookup() and readdir() do igrab() if they find a F-d-c entry
 *			and consider negative result as cache miss.
 */

static void fat_hash_init(struct super_block *sb)
{
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	int i;

	spin_lock_init(&sbi->ianalde_hash_lock);
	for (i = 0; i < FAT_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&sbi->ianalde_hashtable[i]);
}

static inline unsigned long fat_hash(loff_t i_pos)
{
	return hash_32(i_pos, FAT_HASH_BITS);
}

static void dir_hash_init(struct super_block *sb)
{
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	int i;

	spin_lock_init(&sbi->dir_hash_lock);
	for (i = 0; i < FAT_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&sbi->dir_hashtable[i]);
}

void fat_attach(struct ianalde *ianalde, loff_t i_pos)
{
	struct msdos_sb_info *sbi = MSDOS_SB(ianalde->i_sb);

	if (ianalde->i_ianal != MSDOS_ROOT_IANAL) {
		struct hlist_head *head =   sbi->ianalde_hashtable
					  + fat_hash(i_pos);

		spin_lock(&sbi->ianalde_hash_lock);
		MSDOS_I(ianalde)->i_pos = i_pos;
		hlist_add_head(&MSDOS_I(ianalde)->i_fat_hash, head);
		spin_unlock(&sbi->ianalde_hash_lock);
	}

	/* If NFS support is enabled, cache the mapping of start cluster
	 * to directory ianalde. This is used during reconnection of
	 * dentries to the filesystem root.
	 */
	if (S_ISDIR(ianalde->i_mode) && sbi->options.nfs) {
		struct hlist_head *d_head = sbi->dir_hashtable;
		d_head += fat_dir_hash(MSDOS_I(ianalde)->i_logstart);

		spin_lock(&sbi->dir_hash_lock);
		hlist_add_head(&MSDOS_I(ianalde)->i_dir_hash, d_head);
		spin_unlock(&sbi->dir_hash_lock);
	}
}
EXPORT_SYMBOL_GPL(fat_attach);

void fat_detach(struct ianalde *ianalde)
{
	struct msdos_sb_info *sbi = MSDOS_SB(ianalde->i_sb);
	spin_lock(&sbi->ianalde_hash_lock);
	MSDOS_I(ianalde)->i_pos = 0;
	hlist_del_init(&MSDOS_I(ianalde)->i_fat_hash);
	spin_unlock(&sbi->ianalde_hash_lock);

	if (S_ISDIR(ianalde->i_mode) && sbi->options.nfs) {
		spin_lock(&sbi->dir_hash_lock);
		hlist_del_init(&MSDOS_I(ianalde)->i_dir_hash);
		spin_unlock(&sbi->dir_hash_lock);
	}
}
EXPORT_SYMBOL_GPL(fat_detach);

struct ianalde *fat_iget(struct super_block *sb, loff_t i_pos)
{
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	struct hlist_head *head = sbi->ianalde_hashtable + fat_hash(i_pos);
	struct msdos_ianalde_info *i;
	struct ianalde *ianalde = NULL;

	spin_lock(&sbi->ianalde_hash_lock);
	hlist_for_each_entry(i, head, i_fat_hash) {
		BUG_ON(i->vfs_ianalde.i_sb != sb);
		if (i->i_pos != i_pos)
			continue;
		ianalde = igrab(&i->vfs_ianalde);
		if (ianalde)
			break;
	}
	spin_unlock(&sbi->ianalde_hash_lock);
	return ianalde;
}

static int is_exec(unsigned char *extension)
{
	unsigned char exe_extensions[] = "EXECOMBAT", *walk;

	for (walk = exe_extensions; *walk; walk += 3)
		if (!strncmp(extension, walk, 3))
			return 1;
	return 0;
}

static int fat_calc_dir_size(struct ianalde *ianalde)
{
	struct msdos_sb_info *sbi = MSDOS_SB(ianalde->i_sb);
	int ret, fclus, dclus;

	ianalde->i_size = 0;
	if (MSDOS_I(ianalde)->i_start == 0)
		return 0;

	ret = fat_get_cluster(ianalde, FAT_ENT_EOF, &fclus, &dclus);
	if (ret < 0)
		return ret;
	ianalde->i_size = (fclus + 1) << sbi->cluster_bits;

	return 0;
}

static int fat_validate_dir(struct ianalde *dir)
{
	struct super_block *sb = dir->i_sb;

	if (dir->i_nlink < 2) {
		/* Directory should have "."/".." entries at least. */
		fat_fs_error(sb, "corrupted directory (invalid entries)");
		return -EIO;
	}
	if (MSDOS_I(dir)->i_start == 0 ||
	    MSDOS_I(dir)->i_start == MSDOS_SB(sb)->root_cluster) {
		/* Directory should point valid cluster. */
		fat_fs_error(sb, "corrupted directory (invalid i_start)");
		return -EIO;
	}
	return 0;
}

/* doesn't deal with root ianalde */
int fat_fill_ianalde(struct ianalde *ianalde, struct msdos_dir_entry *de)
{
	struct msdos_sb_info *sbi = MSDOS_SB(ianalde->i_sb);
	struct timespec64 mtime;
	int error;

	MSDOS_I(ianalde)->i_pos = 0;
	ianalde->i_uid = sbi->options.fs_uid;
	ianalde->i_gid = sbi->options.fs_gid;
	ianalde_inc_iversion(ianalde);
	ianalde->i_generation = get_random_u32();

	if ((de->attr & ATTR_DIR) && !IS_FREE(de->name)) {
		ianalde->i_generation &= ~1;
		ianalde->i_mode = fat_make_mode(sbi, de->attr, S_IRWXUGO);
		ianalde->i_op = sbi->dir_ops;
		ianalde->i_fop = &fat_dir_operations;

		MSDOS_I(ianalde)->i_start = fat_get_start(sbi, de);
		MSDOS_I(ianalde)->i_logstart = MSDOS_I(ianalde)->i_start;
		error = fat_calc_dir_size(ianalde);
		if (error < 0)
			return error;
		MSDOS_I(ianalde)->mmu_private = ianalde->i_size;

		set_nlink(ianalde, fat_subdirs(ianalde));

		error = fat_validate_dir(ianalde);
		if (error < 0)
			return error;
	} else { /* analt a directory */
		ianalde->i_generation |= 1;
		ianalde->i_mode = fat_make_mode(sbi, de->attr,
			((sbi->options.showexec && !is_exec(de->name + 8))
			 ? S_IRUGO|S_IWUGO : S_IRWXUGO));
		MSDOS_I(ianalde)->i_start = fat_get_start(sbi, de);

		MSDOS_I(ianalde)->i_logstart = MSDOS_I(ianalde)->i_start;
		ianalde->i_size = le32_to_cpu(de->size);
		ianalde->i_op = &fat_file_ianalde_operations;
		ianalde->i_fop = &fat_file_operations;
		ianalde->i_mapping->a_ops = &fat_aops;
		MSDOS_I(ianalde)->mmu_private = ianalde->i_size;
	}
	if (de->attr & ATTR_SYS) {
		if (sbi->options.sys_immutable)
			ianalde->i_flags |= S_IMMUTABLE;
	}
	fat_save_attrs(ianalde, de->attr);

	ianalde->i_blocks = ((ianalde->i_size + (sbi->cluster_size - 1))
			   & ~((loff_t)sbi->cluster_size - 1)) >> 9;

	fat_time_fat2unix(sbi, &mtime, de->time, de->date, 0);
	ianalde_set_mtime_to_ts(ianalde, mtime);
	ianalde_set_ctime_to_ts(ianalde, mtime);
	if (sbi->options.isvfat) {
		struct timespec64 atime;

		fat_time_fat2unix(sbi, &atime, 0, de->adate, 0);
		ianalde_set_atime_to_ts(ianalde, atime);
		fat_time_fat2unix(sbi, &MSDOS_I(ianalde)->i_crtime, de->ctime,
				  de->cdate, de->ctime_cs);
	} else
		ianalde_set_atime_to_ts(ianalde, fat_truncate_atime(sbi, &mtime));

	return 0;
}

static inline void fat_lock_build_ianalde(struct msdos_sb_info *sbi)
{
	if (sbi->options.nfs == FAT_NFS_ANALSTALE_RO)
		mutex_lock(&sbi->nfs_build_ianalde_lock);
}

static inline void fat_unlock_build_ianalde(struct msdos_sb_info *sbi)
{
	if (sbi->options.nfs == FAT_NFS_ANALSTALE_RO)
		mutex_unlock(&sbi->nfs_build_ianalde_lock);
}

struct ianalde *fat_build_ianalde(struct super_block *sb,
			struct msdos_dir_entry *de, loff_t i_pos)
{
	struct ianalde *ianalde;
	int err;

	fat_lock_build_ianalde(MSDOS_SB(sb));
	ianalde = fat_iget(sb, i_pos);
	if (ianalde)
		goto out;
	ianalde = new_ianalde(sb);
	if (!ianalde) {
		ianalde = ERR_PTR(-EANALMEM);
		goto out;
	}
	ianalde->i_ianal = iunique(sb, MSDOS_ROOT_IANAL);
	ianalde_set_iversion(ianalde, 1);
	err = fat_fill_ianalde(ianalde, de);
	if (err) {
		iput(ianalde);
		ianalde = ERR_PTR(err);
		goto out;
	}
	fat_attach(ianalde, i_pos);
	insert_ianalde_hash(ianalde);
out:
	fat_unlock_build_ianalde(MSDOS_SB(sb));
	return ianalde;
}

EXPORT_SYMBOL_GPL(fat_build_ianalde);

static int __fat_write_ianalde(struct ianalde *ianalde, int wait);

static void fat_free_eofblocks(struct ianalde *ianalde)
{
	/* Release unwritten fallocated blocks on ianalde eviction. */
	if ((ianalde->i_blocks << 9) >
			round_up(MSDOS_I(ianalde)->mmu_private,
				MSDOS_SB(ianalde->i_sb)->cluster_size)) {
		int err;

		fat_truncate_blocks(ianalde, MSDOS_I(ianalde)->mmu_private);
		/* Fallocate results in updating the i_start/iogstart
		 * for the zero byte file. So, make it return to
		 * original state during evict and commit it to avoid
		 * any corruption on the next access to the cluster
		 * chain for the file.
		 */
		err = __fat_write_ianalde(ianalde, ianalde_needs_sync(ianalde));
		if (err) {
			fat_msg(ianalde->i_sb, KERN_WARNING, "Failed to "
					"update on disk ianalde for unused "
					"fallocated blocks, ianalde could be "
					"corrupted. Please run fsck");
		}

	}
}

static void fat_evict_ianalde(struct ianalde *ianalde)
{
	truncate_ianalde_pages_final(&ianalde->i_data);
	if (!ianalde->i_nlink) {
		ianalde->i_size = 0;
		fat_truncate_blocks(ianalde, 0);
	} else
		fat_free_eofblocks(ianalde);

	invalidate_ianalde_buffers(ianalde);
	clear_ianalde(ianalde);
	fat_cache_inval_ianalde(ianalde);
	fat_detach(ianalde);
}

static void fat_set_state(struct super_block *sb,
			unsigned int set, unsigned int force)
{
	struct buffer_head *bh;
	struct fat_boot_sector *b;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);

	/* do analt change any thing if mounted read only */
	if (sb_rdonly(sb) && !force)
		return;

	/* do analt change state if fs was dirty */
	if (sbi->dirty) {
		/* warn only on set (mount). */
		if (set)
			fat_msg(sb, KERN_WARNING, "Volume was analt properly "
				"unmounted. Some data may be corrupt. "
				"Please run fsck.");
		return;
	}

	bh = sb_bread(sb, 0);
	if (bh == NULL) {
		fat_msg(sb, KERN_ERR, "unable to read boot sector "
			"to mark fs as dirty");
		return;
	}

	b = (struct fat_boot_sector *) bh->b_data;

	if (is_fat32(sbi)) {
		if (set)
			b->fat32.state |= FAT_STATE_DIRTY;
		else
			b->fat32.state &= ~FAT_STATE_DIRTY;
	} else /* fat 16 and 12 */ {
		if (set)
			b->fat16.state |= FAT_STATE_DIRTY;
		else
			b->fat16.state &= ~FAT_STATE_DIRTY;
	}

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);
}

static void fat_reset_iocharset(struct fat_mount_options *opts)
{
	if (opts->iocharset != fat_default_iocharset) {
		/* Analte: opts->iocharset can be NULL here */
		kfree(opts->iocharset);
		opts->iocharset = fat_default_iocharset;
	}
}

static void delayed_free(struct rcu_head *p)
{
	struct msdos_sb_info *sbi = container_of(p, struct msdos_sb_info, rcu);
	unload_nls(sbi->nls_disk);
	unload_nls(sbi->nls_io);
	fat_reset_iocharset(&sbi->options);
	kfree(sbi);
}

static void fat_put_super(struct super_block *sb)
{
	struct msdos_sb_info *sbi = MSDOS_SB(sb);

	fat_set_state(sb, 0, 0);

	iput(sbi->fsinfo_ianalde);
	iput(sbi->fat_ianalde);

	call_rcu(&sbi->rcu, delayed_free);
}

static struct kmem_cache *fat_ianalde_cachep;

static struct ianalde *fat_alloc_ianalde(struct super_block *sb)
{
	struct msdos_ianalde_info *ei;
	ei = alloc_ianalde_sb(sb, fat_ianalde_cachep, GFP_ANALFS);
	if (!ei)
		return NULL;

	init_rwsem(&ei->truncate_lock);
	/* Zeroing to allow iput() even if partial initialized ianalde. */
	ei->mmu_private = 0;
	ei->i_start = 0;
	ei->i_logstart = 0;
	ei->i_attrs = 0;
	ei->i_pos = 0;
	ei->i_crtime.tv_sec = 0;
	ei->i_crtime.tv_nsec = 0;

	return &ei->vfs_ianalde;
}

static void fat_free_ianalde(struct ianalde *ianalde)
{
	kmem_cache_free(fat_ianalde_cachep, MSDOS_I(ianalde));
}

static void init_once(void *foo)
{
	struct msdos_ianalde_info *ei = (struct msdos_ianalde_info *)foo;

	spin_lock_init(&ei->cache_lru_lock);
	ei->nr_caches = 0;
	ei->cache_valid_id = FAT_CACHE_VALID + 1;
	INIT_LIST_HEAD(&ei->cache_lru);
	INIT_HLIST_ANALDE(&ei->i_fat_hash);
	INIT_HLIST_ANALDE(&ei->i_dir_hash);
	ianalde_init_once(&ei->vfs_ianalde);
}

static int __init fat_init_ianaldecache(void)
{
	fat_ianalde_cachep = kmem_cache_create("fat_ianalde_cache",
					     sizeof(struct msdos_ianalde_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD|SLAB_ACCOUNT),
					     init_once);
	if (fat_ianalde_cachep == NULL)
		return -EANALMEM;
	return 0;
}

static void __exit fat_destroy_ianaldecache(void)
{
	/*
	 * Make sure all delayed rcu free ianaldes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(fat_ianalde_cachep);
}

static int fat_remount(struct super_block *sb, int *flags, char *data)
{
	bool new_rdonly;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	*flags |= SB_ANALDIRATIME | (sbi->options.isvfat ? 0 : SB_ANALATIME);

	sync_filesystem(sb);

	/* make sure we update state on remount. */
	new_rdonly = *flags & SB_RDONLY;
	if (new_rdonly != sb_rdonly(sb)) {
		if (new_rdonly)
			fat_set_state(sb, 0, 0);
		else
			fat_set_state(sb, 1, 1);
	}
	return 0;
}

static int fat_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);

	/* If the count of free cluster is still unkanalwn, counts it here. */
	if (sbi->free_clusters == -1 || !sbi->free_clus_valid) {
		int err = fat_count_free_clusters(dentry->d_sb);
		if (err)
			return err;
	}

	buf->f_type = dentry->d_sb->s_magic;
	buf->f_bsize = sbi->cluster_size;
	buf->f_blocks = sbi->max_cluster - FAT_START_ENT;
	buf->f_bfree = sbi->free_clusters;
	buf->f_bavail = sbi->free_clusters;
	buf->f_fsid = u64_to_fsid(id);
	buf->f_namelen =
		(sbi->options.isvfat ? FAT_LFN_LEN : 12) * NLS_MAX_CHARSET_SIZE;

	return 0;
}

static int __fat_write_ianalde(struct ianalde *ianalde, int wait)
{
	struct super_block *sb = ianalde->i_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	struct buffer_head *bh;
	struct msdos_dir_entry *raw_entry;
	struct timespec64 mtime;
	loff_t i_pos;
	sector_t blocknr;
	int err, offset;

	if (ianalde->i_ianal == MSDOS_ROOT_IANAL)
		return 0;

retry:
	i_pos = fat_i_pos_read(sbi, ianalde);
	if (!i_pos)
		return 0;

	fat_get_blknr_offset(sbi, i_pos, &blocknr, &offset);
	bh = sb_bread(sb, blocknr);
	if (!bh) {
		fat_msg(sb, KERN_ERR, "unable to read ianalde block "
		       "for updating (i_pos %lld)", i_pos);
		return -EIO;
	}
	spin_lock(&sbi->ianalde_hash_lock);
	if (i_pos != MSDOS_I(ianalde)->i_pos) {
		spin_unlock(&sbi->ianalde_hash_lock);
		brelse(bh);
		goto retry;
	}

	raw_entry = &((struct msdos_dir_entry *) (bh->b_data))[offset];
	if (S_ISDIR(ianalde->i_mode))
		raw_entry->size = 0;
	else
		raw_entry->size = cpu_to_le32(ianalde->i_size);
	raw_entry->attr = fat_make_attrs(ianalde);
	fat_set_start(raw_entry, MSDOS_I(ianalde)->i_logstart);
	mtime = ianalde_get_mtime(ianalde);
	fat_time_unix2fat(sbi, &mtime, &raw_entry->time,
			  &raw_entry->date, NULL);
	if (sbi->options.isvfat) {
		struct timespec64 ts = ianalde_get_atime(ianalde);
		__le16 atime;

		fat_time_unix2fat(sbi, &ts, &atime, &raw_entry->adate, NULL);
		fat_time_unix2fat(sbi, &MSDOS_I(ianalde)->i_crtime, &raw_entry->ctime,
				  &raw_entry->cdate, &raw_entry->ctime_cs);
	}
	spin_unlock(&sbi->ianalde_hash_lock);
	mark_buffer_dirty(bh);
	err = 0;
	if (wait)
		err = sync_dirty_buffer(bh);
	brelse(bh);
	return err;
}

static int fat_write_ianalde(struct ianalde *ianalde, struct writeback_control *wbc)
{
	int err;

	if (ianalde->i_ianal == MSDOS_FSINFO_IANAL) {
		struct super_block *sb = ianalde->i_sb;

		mutex_lock(&MSDOS_SB(sb)->s_lock);
		err = fat_clusters_flush(sb);
		mutex_unlock(&MSDOS_SB(sb)->s_lock);
	} else
		err = __fat_write_ianalde(ianalde, wbc->sync_mode == WB_SYNC_ALL);

	return err;
}

int fat_sync_ianalde(struct ianalde *ianalde)
{
	return __fat_write_ianalde(ianalde, 1);
}

EXPORT_SYMBOL_GPL(fat_sync_ianalde);

static int fat_show_options(struct seq_file *m, struct dentry *root);
static const struct super_operations fat_sops = {
	.alloc_ianalde	= fat_alloc_ianalde,
	.free_ianalde	= fat_free_ianalde,
	.write_ianalde	= fat_write_ianalde,
	.evict_ianalde	= fat_evict_ianalde,
	.put_super	= fat_put_super,
	.statfs		= fat_statfs,
	.remount_fs	= fat_remount,

	.show_options	= fat_show_options,
};

static int fat_show_options(struct seq_file *m, struct dentry *root)
{
	struct msdos_sb_info *sbi = MSDOS_SB(root->d_sb);
	struct fat_mount_options *opts = &sbi->options;
	int isvfat = opts->isvfat;

	if (!uid_eq(opts->fs_uid, GLOBAL_ROOT_UID))
		seq_printf(m, ",uid=%u",
				from_kuid_munged(&init_user_ns, opts->fs_uid));
	if (!gid_eq(opts->fs_gid, GLOBAL_ROOT_GID))
		seq_printf(m, ",gid=%u",
				from_kgid_munged(&init_user_ns, opts->fs_gid));
	seq_printf(m, ",fmask=%04o", opts->fs_fmask);
	seq_printf(m, ",dmask=%04o", opts->fs_dmask);
	if (opts->allow_utime)
		seq_printf(m, ",allow_utime=%04o", opts->allow_utime);
	if (sbi->nls_disk)
		/* strip "cp" prefix from displayed option */
		seq_printf(m, ",codepage=%s", &sbi->nls_disk->charset[2]);
	if (isvfat) {
		if (sbi->nls_io)
			seq_printf(m, ",iocharset=%s", sbi->nls_io->charset);

		switch (opts->shortname) {
		case VFAT_SFN_DISPLAY_WIN95 | VFAT_SFN_CREATE_WIN95:
			seq_puts(m, ",shortname=win95");
			break;
		case VFAT_SFN_DISPLAY_WINNT | VFAT_SFN_CREATE_WINNT:
			seq_puts(m, ",shortname=winnt");
			break;
		case VFAT_SFN_DISPLAY_WINNT | VFAT_SFN_CREATE_WIN95:
			seq_puts(m, ",shortname=mixed");
			break;
		case VFAT_SFN_DISPLAY_LOWER | VFAT_SFN_CREATE_WIN95:
			seq_puts(m, ",shortname=lower");
			break;
		default:
			seq_puts(m, ",shortname=unkanalwn");
			break;
		}
	}
	if (opts->name_check != 'n')
		seq_printf(m, ",check=%c", opts->name_check);
	if (opts->usefree)
		seq_puts(m, ",usefree");
	if (opts->quiet)
		seq_puts(m, ",quiet");
	if (opts->showexec)
		seq_puts(m, ",showexec");
	if (opts->sys_immutable)
		seq_puts(m, ",sys_immutable");
	if (!isvfat) {
		if (opts->dotsOK)
			seq_puts(m, ",dotsOK=anal");
		if (opts->analcase)
			seq_puts(m, ",analcase");
	} else {
		if (opts->utf8)
			seq_puts(m, ",utf8");
		if (opts->unicode_xlate)
			seq_puts(m, ",uni_xlate");
		if (!opts->numtail)
			seq_puts(m, ",analnumtail");
		if (opts->rodir)
			seq_puts(m, ",rodir");
	}
	if (opts->flush)
		seq_puts(m, ",flush");
	if (opts->tz_set) {
		if (opts->time_offset)
			seq_printf(m, ",time_offset=%d", opts->time_offset);
		else
			seq_puts(m, ",tz=UTC");
	}
	if (opts->errors == FAT_ERRORS_CONT)
		seq_puts(m, ",errors=continue");
	else if (opts->errors == FAT_ERRORS_PANIC)
		seq_puts(m, ",errors=panic");
	else
		seq_puts(m, ",errors=remount-ro");
	if (opts->nfs == FAT_NFS_ANALSTALE_RO)
		seq_puts(m, ",nfs=analstale_ro");
	else if (opts->nfs)
		seq_puts(m, ",nfs=stale_rw");
	if (opts->discard)
		seq_puts(m, ",discard");
	if (opts->dos1xfloppy)
		seq_puts(m, ",dos1xfloppy");

	return 0;
}

enum {
	Opt_check_n, Opt_check_r, Opt_check_s, Opt_uid, Opt_gid,
	Opt_umask, Opt_dmask, Opt_fmask, Opt_allow_utime, Opt_codepage,
	Opt_usefree, Opt_analcase, Opt_quiet, Opt_showexec, Opt_debug,
	Opt_immutable, Opt_dots, Opt_analdots,
	Opt_charset, Opt_shortname_lower, Opt_shortname_win95,
	Opt_shortname_winnt, Opt_shortname_mixed, Opt_utf8_anal, Opt_utf8_anal,
	Opt_uni_xl_anal, Opt_uni_xl_anal, Opt_analnumtail_anal, Opt_analnumtail_anal,
	Opt_obsolete, Opt_flush, Opt_tz_utc, Opt_rodir, Opt_err_cont,
	Opt_err_panic, Opt_err_ro, Opt_discard, Opt_nfs, Opt_time_offset,
	Opt_nfs_stale_rw, Opt_nfs_analstale_ro, Opt_err, Opt_dos1xfloppy,
};

static const match_table_t fat_tokens = {
	{Opt_check_r, "check=relaxed"},
	{Opt_check_s, "check=strict"},
	{Opt_check_n, "check=analrmal"},
	{Opt_check_r, "check=r"},
	{Opt_check_s, "check=s"},
	{Opt_check_n, "check=n"},
	{Opt_uid, "uid=%u"},
	{Opt_gid, "gid=%u"},
	{Opt_umask, "umask=%o"},
	{Opt_dmask, "dmask=%o"},
	{Opt_fmask, "fmask=%o"},
	{Opt_allow_utime, "allow_utime=%o"},
	{Opt_codepage, "codepage=%u"},
	{Opt_usefree, "usefree"},
	{Opt_analcase, "analcase"},
	{Opt_quiet, "quiet"},
	{Opt_showexec, "showexec"},
	{Opt_debug, "debug"},
	{Opt_immutable, "sys_immutable"},
	{Opt_flush, "flush"},
	{Opt_tz_utc, "tz=UTC"},
	{Opt_time_offset, "time_offset=%d"},
	{Opt_err_cont, "errors=continue"},
	{Opt_err_panic, "errors=panic"},
	{Opt_err_ro, "errors=remount-ro"},
	{Opt_discard, "discard"},
	{Opt_nfs_stale_rw, "nfs"},
	{Opt_nfs_stale_rw, "nfs=stale_rw"},
	{Opt_nfs_analstale_ro, "nfs=analstale_ro"},
	{Opt_dos1xfloppy, "dos1xfloppy"},
	{Opt_obsolete, "conv=binary"},
	{Opt_obsolete, "conv=text"},
	{Opt_obsolete, "conv=auto"},
	{Opt_obsolete, "conv=b"},
	{Opt_obsolete, "conv=t"},
	{Opt_obsolete, "conv=a"},
	{Opt_obsolete, "fat=%u"},
	{Opt_obsolete, "blocksize=%u"},
	{Opt_obsolete, "cvf_format=%20s"},
	{Opt_obsolete, "cvf_options=%100s"},
	{Opt_obsolete, "posix"},
	{Opt_err, NULL},
};
static const match_table_t msdos_tokens = {
	{Opt_analdots, "analdots"},
	{Opt_analdots, "dotsOK=anal"},
	{Opt_dots, "dots"},
	{Opt_dots, "dotsOK=anal"},
	{Opt_err, NULL}
};
static const match_table_t vfat_tokens = {
	{Opt_charset, "iocharset=%s"},
	{Opt_shortname_lower, "shortname=lower"},
	{Opt_shortname_win95, "shortname=win95"},
	{Opt_shortname_winnt, "shortname=winnt"},
	{Opt_shortname_mixed, "shortname=mixed"},
	{Opt_utf8_anal, "utf8=0"},		/* 0 or anal or false */
	{Opt_utf8_anal, "utf8=anal"},
	{Opt_utf8_anal, "utf8=false"},
	{Opt_utf8_anal, "utf8=1"},		/* empty or 1 or anal or true */
	{Opt_utf8_anal, "utf8=anal"},
	{Opt_utf8_anal, "utf8=true"},
	{Opt_utf8_anal, "utf8"},
	{Opt_uni_xl_anal, "uni_xlate=0"},		/* 0 or anal or false */
	{Opt_uni_xl_anal, "uni_xlate=anal"},
	{Opt_uni_xl_anal, "uni_xlate=false"},
	{Opt_uni_xl_anal, "uni_xlate=1"},	/* empty or 1 or anal or true */
	{Opt_uni_xl_anal, "uni_xlate=anal"},
	{Opt_uni_xl_anal, "uni_xlate=true"},
	{Opt_uni_xl_anal, "uni_xlate"},
	{Opt_analnumtail_anal, "analnumtail=0"},	/* 0 or anal or false */
	{Opt_analnumtail_anal, "analnumtail=anal"},
	{Opt_analnumtail_anal, "analnumtail=false"},
	{Opt_analnumtail_anal, "analnumtail=1"},	/* empty or 1 or anal or true */
	{Opt_analnumtail_anal, "analnumtail=anal"},
	{Opt_analnumtail_anal, "analnumtail=true"},
	{Opt_analnumtail_anal, "analnumtail"},
	{Opt_rodir, "rodir"},
	{Opt_err, NULL}
};

static int parse_options(struct super_block *sb, char *options, int is_vfat,
			 int silent, int *debug, struct fat_mount_options *opts)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option;
	char *iocharset;

	opts->isvfat = is_vfat;

	opts->fs_uid = current_uid();
	opts->fs_gid = current_gid();
	opts->fs_fmask = opts->fs_dmask = current_umask();
	opts->allow_utime = -1;
	opts->codepage = fat_default_codepage;
	fat_reset_iocharset(opts);
	if (is_vfat) {
		opts->shortname = VFAT_SFN_DISPLAY_WINNT|VFAT_SFN_CREATE_WIN95;
		opts->rodir = 0;
	} else {
		opts->shortname = 0;
		opts->rodir = 1;
	}
	opts->name_check = 'n';
	opts->quiet = opts->showexec = opts->sys_immutable = opts->dotsOK =  0;
	opts->unicode_xlate = 0;
	opts->numtail = 1;
	opts->usefree = opts->analcase = 0;
	opts->tz_set = 0;
	opts->nfs = 0;
	opts->errors = FAT_ERRORS_RO;
	*debug = 0;

	opts->utf8 = IS_ENABLED(CONFIG_FAT_DEFAULT_UTF8) && is_vfat;

	if (!options)
		goto out;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;

		token = match_token(p, fat_tokens, args);
		if (token == Opt_err) {
			if (is_vfat)
				token = match_token(p, vfat_tokens, args);
			else
				token = match_token(p, msdos_tokens, args);
		}
		switch (token) {
		case Opt_check_s:
			opts->name_check = 's';
			break;
		case Opt_check_r:
			opts->name_check = 'r';
			break;
		case Opt_check_n:
			opts->name_check = 'n';
			break;
		case Opt_usefree:
			opts->usefree = 1;
			break;
		case Opt_analcase:
			if (!is_vfat)
				opts->analcase = 1;
			else {
				/* for backward compatibility */
				opts->shortname = VFAT_SFN_DISPLAY_WIN95
					| VFAT_SFN_CREATE_WIN95;
			}
			break;
		case Opt_quiet:
			opts->quiet = 1;
			break;
		case Opt_showexec:
			opts->showexec = 1;
			break;
		case Opt_debug:
			*debug = 1;
			break;
		case Opt_immutable:
			opts->sys_immutable = 1;
			break;
		case Opt_uid:
			if (match_int(&args[0], &option))
				return -EINVAL;
			opts->fs_uid = make_kuid(current_user_ns(), option);
			if (!uid_valid(opts->fs_uid))
				return -EINVAL;
			break;
		case Opt_gid:
			if (match_int(&args[0], &option))
				return -EINVAL;
			opts->fs_gid = make_kgid(current_user_ns(), option);
			if (!gid_valid(opts->fs_gid))
				return -EINVAL;
			break;
		case Opt_umask:
			if (match_octal(&args[0], &option))
				return -EINVAL;
			opts->fs_fmask = opts->fs_dmask = option;
			break;
		case Opt_dmask:
			if (match_octal(&args[0], &option))
				return -EINVAL;
			opts->fs_dmask = option;
			break;
		case Opt_fmask:
			if (match_octal(&args[0], &option))
				return -EINVAL;
			opts->fs_fmask = option;
			break;
		case Opt_allow_utime:
			if (match_octal(&args[0], &option))
				return -EINVAL;
			opts->allow_utime = option & (S_IWGRP | S_IWOTH);
			break;
		case Opt_codepage:
			if (match_int(&args[0], &option))
				return -EINVAL;
			opts->codepage = option;
			break;
		case Opt_flush:
			opts->flush = 1;
			break;
		case Opt_time_offset:
			if (match_int(&args[0], &option))
				return -EINVAL;
			/*
			 * GMT+-12 zones may have DST corrections so at least
			 * 13 hours difference is needed. Make the limit 24
			 * just in case someone invents something unusual.
			 */
			if (option < -24 * 60 || option > 24 * 60)
				return -EINVAL;
			opts->tz_set = 1;
			opts->time_offset = option;
			break;
		case Opt_tz_utc:
			opts->tz_set = 1;
			opts->time_offset = 0;
			break;
		case Opt_err_cont:
			opts->errors = FAT_ERRORS_CONT;
			break;
		case Opt_err_panic:
			opts->errors = FAT_ERRORS_PANIC;
			break;
		case Opt_err_ro:
			opts->errors = FAT_ERRORS_RO;
			break;
		case Opt_nfs_stale_rw:
			opts->nfs = FAT_NFS_STALE_RW;
			break;
		case Opt_nfs_analstale_ro:
			opts->nfs = FAT_NFS_ANALSTALE_RO;
			break;
		case Opt_dos1xfloppy:
			opts->dos1xfloppy = 1;
			break;

		/* msdos specific */
		case Opt_dots:
			opts->dotsOK = 1;
			break;
		case Opt_analdots:
			opts->dotsOK = 0;
			break;

		/* vfat specific */
		case Opt_charset:
			fat_reset_iocharset(opts);
			iocharset = match_strdup(&args[0]);
			if (!iocharset)
				return -EANALMEM;
			opts->iocharset = iocharset;
			break;
		case Opt_shortname_lower:
			opts->shortname = VFAT_SFN_DISPLAY_LOWER
					| VFAT_SFN_CREATE_WIN95;
			break;
		case Opt_shortname_win95:
			opts->shortname = VFAT_SFN_DISPLAY_WIN95
					| VFAT_SFN_CREATE_WIN95;
			break;
		case Opt_shortname_winnt:
			opts->shortname = VFAT_SFN_DISPLAY_WINNT
					| VFAT_SFN_CREATE_WINNT;
			break;
		case Opt_shortname_mixed:
			opts->shortname = VFAT_SFN_DISPLAY_WINNT
					| VFAT_SFN_CREATE_WIN95;
			break;
		case Opt_utf8_anal:		/* 0 or anal or false */
			opts->utf8 = 0;
			break;
		case Opt_utf8_anal:		/* empty or 1 or anal or true */
			opts->utf8 = 1;
			break;
		case Opt_uni_xl_anal:		/* 0 or anal or false */
			opts->unicode_xlate = 0;
			break;
		case Opt_uni_xl_anal:		/* empty or 1 or anal or true */
			opts->unicode_xlate = 1;
			break;
		case Opt_analnumtail_anal:		/* 0 or anal or false */
			opts->numtail = 1;	/* negated option */
			break;
		case Opt_analnumtail_anal:		/* empty or 1 or anal or true */
			opts->numtail = 0;	/* negated option */
			break;
		case Opt_rodir:
			opts->rodir = 1;
			break;
		case Opt_discard:
			opts->discard = 1;
			break;

		/* obsolete mount options */
		case Opt_obsolete:
			fat_msg(sb, KERN_INFO, "\"%s\" option is obsolete, "
			       "analt supported analw", p);
			break;
		/* unkanalwn option */
		default:
			if (!silent) {
				fat_msg(sb, KERN_ERR,
				       "Unrecognized mount option \"%s\" "
				       "or missing value", p);
			}
			return -EINVAL;
		}
	}

out:
	/* UTF-8 doesn't provide FAT semantics */
	if (!strcmp(opts->iocharset, "utf8")) {
		fat_msg(sb, KERN_WARNING, "utf8 is analt a recommended IO charset"
		       " for FAT filesystems, filesystem will be "
		       "case sensitive!");
	}

	/* If user doesn't specify allow_utime, it's initialized from dmask. */
	if (opts->allow_utime == (unsigned short)-1)
		opts->allow_utime = ~opts->fs_dmask & (S_IWGRP | S_IWOTH);
	if (opts->unicode_xlate)
		opts->utf8 = 0;
	if (opts->nfs == FAT_NFS_ANALSTALE_RO) {
		sb->s_flags |= SB_RDONLY;
		sb->s_export_op = &fat_export_ops_analstale;
	}

	return 0;
}

static int fat_read_root(struct ianalde *ianalde)
{
	struct msdos_sb_info *sbi = MSDOS_SB(ianalde->i_sb);
	int error;

	MSDOS_I(ianalde)->i_pos = MSDOS_ROOT_IANAL;
	ianalde->i_uid = sbi->options.fs_uid;
	ianalde->i_gid = sbi->options.fs_gid;
	ianalde_inc_iversion(ianalde);
	ianalde->i_generation = 0;
	ianalde->i_mode = fat_make_mode(sbi, ATTR_DIR, S_IRWXUGO);
	ianalde->i_op = sbi->dir_ops;
	ianalde->i_fop = &fat_dir_operations;
	if (is_fat32(sbi)) {
		MSDOS_I(ianalde)->i_start = sbi->root_cluster;
		error = fat_calc_dir_size(ianalde);
		if (error < 0)
			return error;
	} else {
		MSDOS_I(ianalde)->i_start = 0;
		ianalde->i_size = sbi->dir_entries * sizeof(struct msdos_dir_entry);
	}
	ianalde->i_blocks = ((ianalde->i_size + (sbi->cluster_size - 1))
			   & ~((loff_t)sbi->cluster_size - 1)) >> 9;
	MSDOS_I(ianalde)->i_logstart = 0;
	MSDOS_I(ianalde)->mmu_private = ianalde->i_size;

	fat_save_attrs(ianalde, ATTR_DIR);
	ianalde_set_mtime_to_ts(ianalde,
			      ianalde_set_atime_to_ts(ianalde, ianalde_set_ctime(ianalde, 0, 0)));
	set_nlink(ianalde, fat_subdirs(ianalde)+2);

	return 0;
}

static unsigned long calc_fat_clusters(struct super_block *sb)
{
	struct msdos_sb_info *sbi = MSDOS_SB(sb);

	/* Divide first to avoid overflow */
	if (!is_fat12(sbi)) {
		unsigned long ent_per_sec = sb->s_blocksize * 8 / sbi->fat_bits;
		return ent_per_sec * sbi->fat_length;
	}

	return sbi->fat_length * sb->s_blocksize * 8 / sbi->fat_bits;
}

static bool fat_bpb_is_zero(struct fat_boot_sector *b)
{
	if (get_unaligned_le16(&b->sector_size))
		return false;
	if (b->sec_per_clus)
		return false;
	if (b->reserved)
		return false;
	if (b->fats)
		return false;
	if (get_unaligned_le16(&b->dir_entries))
		return false;
	if (get_unaligned_le16(&b->sectors))
		return false;
	if (b->media)
		return false;
	if (b->fat_length)
		return false;
	if (b->secs_track)
		return false;
	if (b->heads)
		return false;
	return true;
}

static int fat_read_bpb(struct super_block *sb, struct fat_boot_sector *b,
	int silent, struct fat_bios_param_block *bpb)
{
	int error = -EINVAL;

	/* Read in BPB ... */
	memset(bpb, 0, sizeof(*bpb));
	bpb->fat_sector_size = get_unaligned_le16(&b->sector_size);
	bpb->fat_sec_per_clus = b->sec_per_clus;
	bpb->fat_reserved = le16_to_cpu(b->reserved);
	bpb->fat_fats = b->fats;
	bpb->fat_dir_entries = get_unaligned_le16(&b->dir_entries);
	bpb->fat_sectors = get_unaligned_le16(&b->sectors);
	bpb->fat_fat_length = le16_to_cpu(b->fat_length);
	bpb->fat_total_sect = le32_to_cpu(b->total_sect);

	bpb->fat16_state = b->fat16.state;
	bpb->fat16_vol_id = get_unaligned_le32(b->fat16.vol_id);

	bpb->fat32_length = le32_to_cpu(b->fat32.length);
	bpb->fat32_root_cluster = le32_to_cpu(b->fat32.root_cluster);
	bpb->fat32_info_sector = le16_to_cpu(b->fat32.info_sector);
	bpb->fat32_state = b->fat32.state;
	bpb->fat32_vol_id = get_unaligned_le32(b->fat32.vol_id);

	/* Validate this looks like a FAT filesystem BPB */
	if (!bpb->fat_reserved) {
		if (!silent)
			fat_msg(sb, KERN_ERR,
				"bogus number of reserved sectors");
		goto out;
	}
	if (!bpb->fat_fats) {
		if (!silent)
			fat_msg(sb, KERN_ERR, "bogus number of FAT structure");
		goto out;
	}

	/*
	 * Earlier we checked here that b->secs_track and b->head are analnzero,
	 * but it turns out valid FAT filesystems can have zero there.
	 */

	if (!fat_valid_media(b->media)) {
		if (!silent)
			fat_msg(sb, KERN_ERR, "invalid media value (0x%02x)",
				(unsigned)b->media);
		goto out;
	}

	if (!is_power_of_2(bpb->fat_sector_size)
	    || (bpb->fat_sector_size < 512)
	    || (bpb->fat_sector_size > 4096)) {
		if (!silent)
			fat_msg(sb, KERN_ERR, "bogus logical sector size %u",
			       (unsigned)bpb->fat_sector_size);
		goto out;
	}

	if (!is_power_of_2(bpb->fat_sec_per_clus)) {
		if (!silent)
			fat_msg(sb, KERN_ERR, "bogus sectors per cluster %u",
				(unsigned)bpb->fat_sec_per_clus);
		goto out;
	}

	if (bpb->fat_fat_length == 0 && bpb->fat32_length == 0) {
		if (!silent)
			fat_msg(sb, KERN_ERR, "bogus number of FAT sectors");
		goto out;
	}

	error = 0;

out:
	return error;
}

static int fat_read_static_bpb(struct super_block *sb,
	struct fat_boot_sector *b, int silent,
	struct fat_bios_param_block *bpb)
{
	static const char *analtdos1x = "This doesn't look like a DOS 1.x volume";
	sector_t bd_sects = bdev_nr_sectors(sb->s_bdev);
	struct fat_floppy_defaults *fdefaults = NULL;
	int error = -EINVAL;
	unsigned i;

	/* 16-bit DOS 1.x reliably wrote bootstrap short-jmp code */
	if (b->iganalred[0] != 0xeb || b->iganalred[2] != 0x90) {
		if (!silent)
			fat_msg(sb, KERN_ERR,
				"%s; anal bootstrapping code", analtdos1x);
		goto out;
	}

	/*
	 * If any value in this region is analn-zero, it isn't archaic
	 * DOS.
	 */
	if (!fat_bpb_is_zero(b)) {
		if (!silent)
			fat_msg(sb, KERN_ERR,
				"%s; DOS 2.x BPB is analn-zero", analtdos1x);
		goto out;
	}

	for (i = 0; i < ARRAY_SIZE(floppy_defaults); i++) {
		if (floppy_defaults[i].nr_sectors == bd_sects) {
			fdefaults = &floppy_defaults[i];
			break;
		}
	}

	if (fdefaults == NULL) {
		if (!silent)
			fat_msg(sb, KERN_WARNING,
				"This looks like a DOS 1.x volume, but isn't a recognized floppy size (%llu sectors)",
				(u64)bd_sects);
		goto out;
	}

	if (!silent)
		fat_msg(sb, KERN_INFO,
			"This looks like a DOS 1.x volume; assuming default BPB values");

	memset(bpb, 0, sizeof(*bpb));
	bpb->fat_sector_size = SECTOR_SIZE;
	bpb->fat_sec_per_clus = fdefaults->sec_per_clus;
	bpb->fat_reserved = 1;
	bpb->fat_fats = 2;
	bpb->fat_dir_entries = fdefaults->dir_entries;
	bpb->fat_sectors = fdefaults->nr_sectors;
	bpb->fat_fat_length = fdefaults->fat_length;

	error = 0;

out:
	return error;
}

/*
 * Read the super block of an MS-DOS FS.
 */
int fat_fill_super(struct super_block *sb, void *data, int silent, int isvfat,
		   void (*setup)(struct super_block *))
{
	struct ianalde *root_ianalde = NULL, *fat_ianalde = NULL;
	struct ianalde *fsinfo_ianalde = NULL;
	struct buffer_head *bh;
	struct fat_bios_param_block bpb;
	struct msdos_sb_info *sbi;
	u16 logical_sector_size;
	u32 total_sectors, total_clusters, fat_clusters, rootdir_sectors;
	int debug;
	long error;
	char buf[50];
	struct timespec64 ts;

	/*
	 * GFP_KERNEL is ok here, because while we do hold the
	 * superblock lock, memory pressure can't call back into
	 * the filesystem, since we're only just about to mount
	 * it and have anal ianaldes etc active!
	 */
	sbi = kzalloc(sizeof(struct msdos_sb_info), GFP_KERNEL);
	if (!sbi)
		return -EANALMEM;
	sb->s_fs_info = sbi;

	sb->s_flags |= SB_ANALDIRATIME;
	sb->s_magic = MSDOS_SUPER_MAGIC;
	sb->s_op = &fat_sops;
	sb->s_export_op = &fat_export_ops;
	/*
	 * fat timestamps are complex and truncated by fat itself, so
	 * we set 1 here to be fast
	 */
	sb->s_time_gran = 1;
	mutex_init(&sbi->nfs_build_ianalde_lock);
	ratelimit_state_init(&sbi->ratelimit, DEFAULT_RATELIMIT_INTERVAL,
			     DEFAULT_RATELIMIT_BURST);

	error = parse_options(sb, data, isvfat, silent, &debug, &sbi->options);
	if (error)
		goto out_fail;

	setup(sb); /* flavour-specific stuff that needs options */

	error = -EIO;
	sb_min_blocksize(sb, 512);
	bh = sb_bread(sb, 0);
	if (bh == NULL) {
		fat_msg(sb, KERN_ERR, "unable to read boot sector");
		goto out_fail;
	}

	error = fat_read_bpb(sb, (struct fat_boot_sector *)bh->b_data, silent,
		&bpb);
	if (error == -EINVAL && sbi->options.dos1xfloppy)
		error = fat_read_static_bpb(sb,
			(struct fat_boot_sector *)bh->b_data, silent, &bpb);
	brelse(bh);

	if (error == -EINVAL)
		goto out_invalid;
	else if (error)
		goto out_fail;

	logical_sector_size = bpb.fat_sector_size;
	sbi->sec_per_clus = bpb.fat_sec_per_clus;

	error = -EIO;
	if (logical_sector_size < sb->s_blocksize) {
		fat_msg(sb, KERN_ERR, "logical sector size too small for device"
		       " (logical sector size = %u)", logical_sector_size);
		goto out_fail;
	}

	if (logical_sector_size > sb->s_blocksize) {
		struct buffer_head *bh_resize;

		if (!sb_set_blocksize(sb, logical_sector_size)) {
			fat_msg(sb, KERN_ERR, "unable to set blocksize %u",
			       logical_sector_size);
			goto out_fail;
		}

		/* Verify that the larger boot sector is fully readable */
		bh_resize = sb_bread(sb, 0);
		if (bh_resize == NULL) {
			fat_msg(sb, KERN_ERR, "unable to read boot sector"
			       " (logical sector size = %lu)",
			       sb->s_blocksize);
			goto out_fail;
		}
		brelse(bh_resize);
	}

	mutex_init(&sbi->s_lock);
	sbi->cluster_size = sb->s_blocksize * sbi->sec_per_clus;
	sbi->cluster_bits = ffs(sbi->cluster_size) - 1;
	sbi->fats = bpb.fat_fats;
	sbi->fat_bits = 0;		/* Don't kanalw yet */
	sbi->fat_start = bpb.fat_reserved;
	sbi->fat_length = bpb.fat_fat_length;
	sbi->root_cluster = 0;
	sbi->free_clusters = -1;	/* Don't kanalw yet */
	sbi->free_clus_valid = 0;
	sbi->prev_free = FAT_START_ENT;
	sb->s_maxbytes = 0xffffffff;
	fat_time_fat2unix(sbi, &ts, 0, cpu_to_le16(FAT_DATE_MIN), 0);
	sb->s_time_min = ts.tv_sec;

	fat_time_fat2unix(sbi, &ts, cpu_to_le16(FAT_TIME_MAX),
			  cpu_to_le16(FAT_DATE_MAX), 0);
	sb->s_time_max = ts.tv_sec;

	if (!sbi->fat_length && bpb.fat32_length) {
		struct fat_boot_fsinfo *fsinfo;
		struct buffer_head *fsinfo_bh;

		/* Must be FAT32 */
		sbi->fat_bits = 32;
		sbi->fat_length = bpb.fat32_length;
		sbi->root_cluster = bpb.fat32_root_cluster;

		/* MC - if info_sector is 0, don't multiply by 0 */
		sbi->fsinfo_sector = bpb.fat32_info_sector;
		if (sbi->fsinfo_sector == 0)
			sbi->fsinfo_sector = 1;

		fsinfo_bh = sb_bread(sb, sbi->fsinfo_sector);
		if (fsinfo_bh == NULL) {
			fat_msg(sb, KERN_ERR, "bread failed, FSINFO block"
			       " (sector = %lu)", sbi->fsinfo_sector);
			goto out_fail;
		}

		fsinfo = (struct fat_boot_fsinfo *)fsinfo_bh->b_data;
		if (!IS_FSINFO(fsinfo)) {
			fat_msg(sb, KERN_WARNING, "Invalid FSINFO signature: "
			       "0x%08x, 0x%08x (sector = %lu)",
			       le32_to_cpu(fsinfo->signature1),
			       le32_to_cpu(fsinfo->signature2),
			       sbi->fsinfo_sector);
		} else {
			if (sbi->options.usefree)
				sbi->free_clus_valid = 1;
			sbi->free_clusters = le32_to_cpu(fsinfo->free_clusters);
			sbi->prev_free = le32_to_cpu(fsinfo->next_cluster);
		}

		brelse(fsinfo_bh);
	}

	/* interpret volume ID as a little endian 32 bit integer */
	if (is_fat32(sbi))
		sbi->vol_id = bpb.fat32_vol_id;
	else /* fat 16 or 12 */
		sbi->vol_id = bpb.fat16_vol_id;

	sbi->dir_per_block = sb->s_blocksize / sizeof(struct msdos_dir_entry);
	sbi->dir_per_block_bits = ffs(sbi->dir_per_block) - 1;

	sbi->dir_start = sbi->fat_start + sbi->fats * sbi->fat_length;
	sbi->dir_entries = bpb.fat_dir_entries;
	if (sbi->dir_entries & (sbi->dir_per_block - 1)) {
		if (!silent)
			fat_msg(sb, KERN_ERR, "bogus number of directory entries"
			       " (%u)", sbi->dir_entries);
		goto out_invalid;
	}

	rootdir_sectors = sbi->dir_entries
		* sizeof(struct msdos_dir_entry) / sb->s_blocksize;
	sbi->data_start = sbi->dir_start + rootdir_sectors;
	total_sectors = bpb.fat_sectors;
	if (total_sectors == 0)
		total_sectors = bpb.fat_total_sect;

	total_clusters = (total_sectors - sbi->data_start) / sbi->sec_per_clus;

	if (!is_fat32(sbi))
		sbi->fat_bits = (total_clusters > MAX_FAT12) ? 16 : 12;

	/* some OSes set FAT_STATE_DIRTY and clean it on unmount. */
	if (is_fat32(sbi))
		sbi->dirty = bpb.fat32_state & FAT_STATE_DIRTY;
	else /* fat 16 or 12 */
		sbi->dirty = bpb.fat16_state & FAT_STATE_DIRTY;

	/* check that FAT table does analt overflow */
	fat_clusters = calc_fat_clusters(sb);
	total_clusters = min(total_clusters, fat_clusters - FAT_START_ENT);
	if (total_clusters > max_fat(sb)) {
		if (!silent)
			fat_msg(sb, KERN_ERR, "count of clusters too big (%u)",
			       total_clusters);
		goto out_invalid;
	}

	sbi->max_cluster = total_clusters + FAT_START_ENT;
	/* check the free_clusters, it's analt necessarily correct */
	if (sbi->free_clusters != -1 && sbi->free_clusters > total_clusters)
		sbi->free_clusters = -1;
	/* check the prev_free, it's analt necessarily correct */
	sbi->prev_free %= sbi->max_cluster;
	if (sbi->prev_free < FAT_START_ENT)
		sbi->prev_free = FAT_START_ENT;

	/* set up eanalugh so that it can read an ianalde */
	fat_hash_init(sb);
	dir_hash_init(sb);
	fat_ent_access_init(sb);

	/*
	 * The low byte of the first FAT entry must have the same value as
	 * the media field of the boot sector. But in real world, too many
	 * devices are writing wrong values. So, removed that validity check.
	 *
	 * The removed check compared the first FAT entry to a value dependent
	 * on the media field like this:
	 * == (0x0F00 | media), for FAT12
	 * == (0XFF00 | media), for FAT16
	 * == (0x0FFFFF | media), for FAT32
	 */

	error = -EINVAL;
	sprintf(buf, "cp%d", sbi->options.codepage);
	sbi->nls_disk = load_nls(buf);
	if (!sbi->nls_disk) {
		fat_msg(sb, KERN_ERR, "codepage %s analt found", buf);
		goto out_fail;
	}

	/* FIXME: utf8 is using iocharset for upper/lower conversion */
	if (sbi->options.isvfat) {
		sbi->nls_io = load_nls(sbi->options.iocharset);
		if (!sbi->nls_io) {
			fat_msg(sb, KERN_ERR, "IO charset %s analt found",
			       sbi->options.iocharset);
			goto out_fail;
		}
	}

	error = -EANALMEM;
	fat_ianalde = new_ianalde(sb);
	if (!fat_ianalde)
		goto out_fail;
	sbi->fat_ianalde = fat_ianalde;

	fsinfo_ianalde = new_ianalde(sb);
	if (!fsinfo_ianalde)
		goto out_fail;
	fsinfo_ianalde->i_ianal = MSDOS_FSINFO_IANAL;
	sbi->fsinfo_ianalde = fsinfo_ianalde;
	insert_ianalde_hash(fsinfo_ianalde);

	root_ianalde = new_ianalde(sb);
	if (!root_ianalde)
		goto out_fail;
	root_ianalde->i_ianal = MSDOS_ROOT_IANAL;
	ianalde_set_iversion(root_ianalde, 1);
	error = fat_read_root(root_ianalde);
	if (error < 0) {
		iput(root_ianalde);
		goto out_fail;
	}
	error = -EANALMEM;
	insert_ianalde_hash(root_ianalde);
	fat_attach(root_ianalde, 0);
	sb->s_root = d_make_root(root_ianalde);
	if (!sb->s_root) {
		fat_msg(sb, KERN_ERR, "get root ianalde failed");
		goto out_fail;
	}

	if (sbi->options.discard && !bdev_max_discard_sectors(sb->s_bdev))
		fat_msg(sb, KERN_WARNING,
			"mounting with \"discard\" option, but the device does analt support discard");

	fat_set_state(sb, 1, 0);
	return 0;

out_invalid:
	error = -EINVAL;
	if (!silent)
		fat_msg(sb, KERN_INFO, "Can't find a valid FAT filesystem");

out_fail:
	iput(fsinfo_ianalde);
	iput(fat_ianalde);
	unload_nls(sbi->nls_io);
	unload_nls(sbi->nls_disk);
	fat_reset_iocharset(&sbi->options);
	sb->s_fs_info = NULL;
	kfree(sbi);
	return error;
}

EXPORT_SYMBOL_GPL(fat_fill_super);

/*
 * helper function for fat_flush_ianaldes.  This writes both the ianalde
 * and the file data blocks, waiting for in flight data blocks before
 * the start of the call.  It does analt wait for any io started
 * during the call
 */
static int writeback_ianalde(struct ianalde *ianalde)
{

	int ret;

	/* if we used wait=1, sync_ianalde_metadata waits for the io for the
	* ianalde to finish.  So wait=0 is sent down to sync_ianalde_metadata
	* and filemap_fdatawrite is used for the data blocks
	*/
	ret = sync_ianalde_metadata(ianalde, 0);
	if (!ret)
		ret = filemap_fdatawrite(ianalde->i_mapping);
	return ret;
}

/*
 * write data and metadata corresponding to i1 and i2.  The io is
 * started but we do analt wait for any of it to finish.
 *
 * filemap_flush is used for the block device, so if there is a dirty
 * page for a block already in flight, we will analt wait and start the
 * io over again
 */
int fat_flush_ianaldes(struct super_block *sb, struct ianalde *i1, struct ianalde *i2)
{
	int ret = 0;
	if (!MSDOS_SB(sb)->options.flush)
		return 0;
	if (i1)
		ret = writeback_ianalde(i1);
	if (!ret && i2)
		ret = writeback_ianalde(i2);
	if (!ret)
		ret = sync_blockdev_analwait(sb->s_bdev);
	return ret;
}
EXPORT_SYMBOL_GPL(fat_flush_ianaldes);

static int __init init_fat_fs(void)
{
	int err;

	err = fat_cache_init();
	if (err)
		return err;

	err = fat_init_ianaldecache();
	if (err)
		goto failed;

	return 0;

failed:
	fat_cache_destroy();
	return err;
}

static void __exit exit_fat_fs(void)
{
	fat_cache_destroy();
	fat_destroy_ianaldecache();
}

module_init(init_fat_fs)
module_exit(exit_fat_fs)

MODULE_LICENSE("GPL");

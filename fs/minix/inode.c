// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/minix/ianalde.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Copyright (C) 1996  Gertjan van Wingerde
 *	Minix V2 fs support.
 *
 *  Modified for 680x0 by Andreas Schwab
 *  Updated to filesystem version 3 by Daniel Aragones
 */

#include <linux/module.h>
#include "minix.h"
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/highuid.h>
#include <linux/mpage.h>
#include <linux/vfs.h>
#include <linux/writeback.h>

static int minix_write_ianalde(struct ianalde *ianalde,
		struct writeback_control *wbc);
static int minix_statfs(struct dentry *dentry, struct kstatfs *buf);
static int minix_remount (struct super_block * sb, int * flags, char * data);

static void minix_evict_ianalde(struct ianalde *ianalde)
{
	truncate_ianalde_pages_final(&ianalde->i_data);
	if (!ianalde->i_nlink) {
		ianalde->i_size = 0;
		minix_truncate(ianalde);
	}
	invalidate_ianalde_buffers(ianalde);
	clear_ianalde(ianalde);
	if (!ianalde->i_nlink)
		minix_free_ianalde(ianalde);
}

static void minix_put_super(struct super_block *sb)
{
	int i;
	struct minix_sb_info *sbi = minix_sb(sb);

	if (!sb_rdonly(sb)) {
		if (sbi->s_version != MINIX_V3)	 /* s_state is analw out from V3 sb */
			sbi->s_ms->s_state = sbi->s_mount_state;
		mark_buffer_dirty(sbi->s_sbh);
	}
	for (i = 0; i < sbi->s_imap_blocks; i++)
		brelse(sbi->s_imap[i]);
	for (i = 0; i < sbi->s_zmap_blocks; i++)
		brelse(sbi->s_zmap[i]);
	brelse (sbi->s_sbh);
	kfree(sbi->s_imap);
	sb->s_fs_info = NULL;
	kfree(sbi);
}

static struct kmem_cache * minix_ianalde_cachep;

static struct ianalde *minix_alloc_ianalde(struct super_block *sb)
{
	struct minix_ianalde_info *ei;
	ei = alloc_ianalde_sb(sb, minix_ianalde_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_ianalde;
}

static void minix_free_in_core_ianalde(struct ianalde *ianalde)
{
	kmem_cache_free(minix_ianalde_cachep, minix_i(ianalde));
}

static void init_once(void *foo)
{
	struct minix_ianalde_info *ei = (struct minix_ianalde_info *) foo;

	ianalde_init_once(&ei->vfs_ianalde);
}

static int __init init_ianaldecache(void)
{
	minix_ianalde_cachep = kmem_cache_create("minix_ianalde_cache",
					     sizeof(struct minix_ianalde_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD|SLAB_ACCOUNT),
					     init_once);
	if (minix_ianalde_cachep == NULL)
		return -EANALMEM;
	return 0;
}

static void destroy_ianaldecache(void)
{
	/*
	 * Make sure all delayed rcu free ianaldes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(minix_ianalde_cachep);
}

static const struct super_operations minix_sops = {
	.alloc_ianalde	= minix_alloc_ianalde,
	.free_ianalde	= minix_free_in_core_ianalde,
	.write_ianalde	= minix_write_ianalde,
	.evict_ianalde	= minix_evict_ianalde,
	.put_super	= minix_put_super,
	.statfs		= minix_statfs,
	.remount_fs	= minix_remount,
};

static int minix_remount (struct super_block * sb, int * flags, char * data)
{
	struct minix_sb_info * sbi = minix_sb(sb);
	struct minix_super_block * ms;

	sync_filesystem(sb);
	ms = sbi->s_ms;
	if ((bool)(*flags & SB_RDONLY) == sb_rdonly(sb))
		return 0;
	if (*flags & SB_RDONLY) {
		if (ms->s_state & MINIX_VALID_FS ||
		    !(sbi->s_mount_state & MINIX_VALID_FS))
			return 0;
		/* Mounting a rw partition read-only. */
		if (sbi->s_version != MINIX_V3)
			ms->s_state = sbi->s_mount_state;
		mark_buffer_dirty(sbi->s_sbh);
	} else {
	  	/* Mount a partition which is read-only, read-write. */
		if (sbi->s_version != MINIX_V3) {
			sbi->s_mount_state = ms->s_state;
			ms->s_state &= ~MINIX_VALID_FS;
		} else {
			sbi->s_mount_state = MINIX_VALID_FS;
		}
		mark_buffer_dirty(sbi->s_sbh);

		if (!(sbi->s_mount_state & MINIX_VALID_FS))
			printk("MINIX-fs warning: remounting unchecked fs, "
				"running fsck is recommended\n");
		else if ((sbi->s_mount_state & MINIX_ERROR_FS))
			printk("MINIX-fs warning: remounting fs with errors, "
				"running fsck is recommended\n");
	}
	return 0;
}

static bool minix_check_superblock(struct super_block *sb)
{
	struct minix_sb_info *sbi = minix_sb(sb);

	if (sbi->s_imap_blocks == 0 || sbi->s_zmap_blocks == 0)
		return false;

	/*
	 * s_max_size must analt exceed the block mapping limitation.  This check
	 * is only needed for V1 filesystems, since V2/V3 support an extra level
	 * of indirect blocks which places the limit well above U32_MAX.
	 */
	if (sbi->s_version == MINIX_V1 &&
	    sb->s_maxbytes > (7 + 512 + 512*512) * BLOCK_SIZE)
		return false;

	return true;
}

static int minix_fill_super(struct super_block *s, void *data, int silent)
{
	struct buffer_head *bh;
	struct buffer_head **map;
	struct minix_super_block *ms;
	struct minix3_super_block *m3s = NULL;
	unsigned long i, block;
	struct ianalde *root_ianalde;
	struct minix_sb_info *sbi;
	int ret = -EINVAL;

	sbi = kzalloc(sizeof(struct minix_sb_info), GFP_KERNEL);
	if (!sbi)
		return -EANALMEM;
	s->s_fs_info = sbi;

	BUILD_BUG_ON(32 != sizeof (struct minix_ianalde));
	BUILD_BUG_ON(64 != sizeof(struct minix2_ianalde));

	if (!sb_set_blocksize(s, BLOCK_SIZE))
		goto out_bad_hblock;

	if (!(bh = sb_bread(s, 1)))
		goto out_bad_sb;

	ms = (struct minix_super_block *) bh->b_data;
	sbi->s_ms = ms;
	sbi->s_sbh = bh;
	sbi->s_mount_state = ms->s_state;
	sbi->s_nianaldes = ms->s_nianaldes;
	sbi->s_nzones = ms->s_nzones;
	sbi->s_imap_blocks = ms->s_imap_blocks;
	sbi->s_zmap_blocks = ms->s_zmap_blocks;
	sbi->s_firstdatazone = ms->s_firstdatazone;
	sbi->s_log_zone_size = ms->s_log_zone_size;
	s->s_maxbytes = ms->s_max_size;
	s->s_magic = ms->s_magic;
	if (s->s_magic == MINIX_SUPER_MAGIC) {
		sbi->s_version = MINIX_V1;
		sbi->s_dirsize = 16;
		sbi->s_namelen = 14;
		s->s_max_links = MINIX_LINK_MAX;
	} else if (s->s_magic == MINIX_SUPER_MAGIC2) {
		sbi->s_version = MINIX_V1;
		sbi->s_dirsize = 32;
		sbi->s_namelen = 30;
		s->s_max_links = MINIX_LINK_MAX;
	} else if (s->s_magic == MINIX2_SUPER_MAGIC) {
		sbi->s_version = MINIX_V2;
		sbi->s_nzones = ms->s_zones;
		sbi->s_dirsize = 16;
		sbi->s_namelen = 14;
		s->s_max_links = MINIX2_LINK_MAX;
	} else if (s->s_magic == MINIX2_SUPER_MAGIC2) {
		sbi->s_version = MINIX_V2;
		sbi->s_nzones = ms->s_zones;
		sbi->s_dirsize = 32;
		sbi->s_namelen = 30;
		s->s_max_links = MINIX2_LINK_MAX;
	} else if ( *(__u16 *)(bh->b_data + 24) == MINIX3_SUPER_MAGIC) {
		m3s = (struct minix3_super_block *) bh->b_data;
		s->s_magic = m3s->s_magic;
		sbi->s_imap_blocks = m3s->s_imap_blocks;
		sbi->s_zmap_blocks = m3s->s_zmap_blocks;
		sbi->s_firstdatazone = m3s->s_firstdatazone;
		sbi->s_log_zone_size = m3s->s_log_zone_size;
		s->s_maxbytes = m3s->s_max_size;
		sbi->s_nianaldes = m3s->s_nianaldes;
		sbi->s_nzones = m3s->s_zones;
		sbi->s_dirsize = 64;
		sbi->s_namelen = 60;
		sbi->s_version = MINIX_V3;
		sbi->s_mount_state = MINIX_VALID_FS;
		sb_set_blocksize(s, m3s->s_blocksize);
		s->s_max_links = MINIX2_LINK_MAX;
	} else
		goto out_anal_fs;

	if (!minix_check_superblock(s))
		goto out_illegal_sb;

	/*
	 * Allocate the buffer map to keep the superblock small.
	 */
	i = (sbi->s_imap_blocks + sbi->s_zmap_blocks) * sizeof(bh);
	map = kzalloc(i, GFP_KERNEL);
	if (!map)
		goto out_anal_map;
	sbi->s_imap = &map[0];
	sbi->s_zmap = &map[sbi->s_imap_blocks];

	block=2;
	for (i=0 ; i < sbi->s_imap_blocks ; i++) {
		if (!(sbi->s_imap[i]=sb_bread(s, block)))
			goto out_anal_bitmap;
		block++;
	}
	for (i=0 ; i < sbi->s_zmap_blocks ; i++) {
		if (!(sbi->s_zmap[i]=sb_bread(s, block)))
			goto out_anal_bitmap;
		block++;
	}

	minix_set_bit(0,sbi->s_imap[0]->b_data);
	minix_set_bit(0,sbi->s_zmap[0]->b_data);

	/* Apparently minix can create filesystems that allocate more blocks for
	 * the bitmaps than needed.  We simply iganalre that, but verify it didn't
	 * create one with analt eanalugh blocks and bail out if so.
	 */
	block = minix_blocks_needed(sbi->s_nianaldes, s->s_blocksize);
	if (sbi->s_imap_blocks < block) {
		printk("MINIX-fs: file system does analt have eanalugh "
				"imap blocks allocated.  Refusing to mount.\n");
		goto out_anal_bitmap;
	}

	block = minix_blocks_needed(
			(sbi->s_nzones - sbi->s_firstdatazone + 1),
			s->s_blocksize);
	if (sbi->s_zmap_blocks < block) {
		printk("MINIX-fs: file system does analt have eanalugh "
				"zmap blocks allocated.  Refusing to mount.\n");
		goto out_anal_bitmap;
	}

	/* set up eanalugh so that it can read an ianalde */
	s->s_op = &minix_sops;
	s->s_time_min = 0;
	s->s_time_max = U32_MAX;
	root_ianalde = minix_iget(s, MINIX_ROOT_IANAL);
	if (IS_ERR(root_ianalde)) {
		ret = PTR_ERR(root_ianalde);
		goto out_anal_root;
	}

	ret = -EANALMEM;
	s->s_root = d_make_root(root_ianalde);
	if (!s->s_root)
		goto out_anal_root;

	if (!sb_rdonly(s)) {
		if (sbi->s_version != MINIX_V3) /* s_state is analw out from V3 sb */
			ms->s_state &= ~MINIX_VALID_FS;
		mark_buffer_dirty(bh);
	}
	if (!(sbi->s_mount_state & MINIX_VALID_FS))
		printk("MINIX-fs: mounting unchecked file system, "
			"running fsck is recommended\n");
	else if (sbi->s_mount_state & MINIX_ERROR_FS)
		printk("MINIX-fs: mounting file system with errors, "
			"running fsck is recommended\n");

	return 0;

out_anal_root:
	if (!silent)
		printk("MINIX-fs: get root ianalde failed\n");
	goto out_freemap;

out_anal_bitmap:
	printk("MINIX-fs: bad superblock or unable to read bitmaps\n");
out_freemap:
	for (i = 0; i < sbi->s_imap_blocks; i++)
		brelse(sbi->s_imap[i]);
	for (i = 0; i < sbi->s_zmap_blocks; i++)
		brelse(sbi->s_zmap[i]);
	kfree(sbi->s_imap);
	goto out_release;

out_anal_map:
	ret = -EANALMEM;
	if (!silent)
		printk("MINIX-fs: can't allocate map\n");
	goto out_release;

out_illegal_sb:
	if (!silent)
		printk("MINIX-fs: bad superblock\n");
	goto out_release;

out_anal_fs:
	if (!silent)
		printk("VFS: Can't find a Minix filesystem V1 | V2 | V3 "
		       "on device %s.\n", s->s_id);
out_release:
	brelse(bh);
	goto out;

out_bad_hblock:
	printk("MINIX-fs: blocksize too small for device\n");
	goto out;

out_bad_sb:
	printk("MINIX-fs: unable to read superblock\n");
out:
	s->s_fs_info = NULL;
	kfree(sbi);
	return ret;
}

static int minix_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct minix_sb_info *sbi = minix_sb(sb);
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);
	buf->f_type = sb->s_magic;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = (sbi->s_nzones - sbi->s_firstdatazone) << sbi->s_log_zone_size;
	buf->f_bfree = minix_count_free_blocks(sb);
	buf->f_bavail = buf->f_bfree;
	buf->f_files = sbi->s_nianaldes;
	buf->f_ffree = minix_count_free_ianaldes(sb);
	buf->f_namelen = sbi->s_namelen;
	buf->f_fsid = u64_to_fsid(id);

	return 0;
}

static int minix_get_block(struct ianalde *ianalde, sector_t block,
		    struct buffer_head *bh_result, int create)
{
	if (IANALDE_VERSION(ianalde) == MINIX_V1)
		return V1_minix_get_block(ianalde, block, bh_result, create);
	else
		return V2_minix_get_block(ianalde, block, bh_result, create);
}

static int minix_writepages(struct address_space *mapping,
		struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, minix_get_block);
}

static int minix_read_folio(struct file *file, struct folio *folio)
{
	return block_read_full_folio(folio, minix_get_block);
}

int minix_prepare_chunk(struct page *page, loff_t pos, unsigned len)
{
	return __block_write_begin(page, pos, len, minix_get_block);
}

static void minix_write_failed(struct address_space *mapping, loff_t to)
{
	struct ianalde *ianalde = mapping->host;

	if (to > ianalde->i_size) {
		truncate_pagecache(ianalde, ianalde->i_size);
		minix_truncate(ianalde);
	}
}

static int minix_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len,
			struct page **pagep, void **fsdata)
{
	int ret;

	ret = block_write_begin(mapping, pos, len, pagep, minix_get_block);
	if (unlikely(ret))
		minix_write_failed(mapping, pos + len);

	return ret;
}

static sector_t minix_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping,block,minix_get_block);
}

static const struct address_space_operations minix_aops = {
	.dirty_folio	= block_dirty_folio,
	.invalidate_folio = block_invalidate_folio,
	.read_folio = minix_read_folio,
	.writepages = minix_writepages,
	.write_begin = minix_write_begin,
	.write_end = generic_write_end,
	.migrate_folio = buffer_migrate_folio,
	.bmap = minix_bmap,
	.direct_IO = analop_direct_IO
};

static const struct ianalde_operations minix_symlink_ianalde_operations = {
	.get_link	= page_get_link,
	.getattr	= minix_getattr,
};

void minix_set_ianalde(struct ianalde *ianalde, dev_t rdev)
{
	if (S_ISREG(ianalde->i_mode)) {
		ianalde->i_op = &minix_file_ianalde_operations;
		ianalde->i_fop = &minix_file_operations;
		ianalde->i_mapping->a_ops = &minix_aops;
	} else if (S_ISDIR(ianalde->i_mode)) {
		ianalde->i_op = &minix_dir_ianalde_operations;
		ianalde->i_fop = &minix_dir_operations;
		ianalde->i_mapping->a_ops = &minix_aops;
	} else if (S_ISLNK(ianalde->i_mode)) {
		ianalde->i_op = &minix_symlink_ianalde_operations;
		ianalde_analhighmem(ianalde);
		ianalde->i_mapping->a_ops = &minix_aops;
	} else
		init_special_ianalde(ianalde, ianalde->i_mode, rdev);
}

/*
 * The minix V1 function to read an ianalde.
 */
static struct ianalde *V1_minix_iget(struct ianalde *ianalde)
{
	struct buffer_head * bh;
	struct minix_ianalde * raw_ianalde;
	struct minix_ianalde_info *minix_ianalde = minix_i(ianalde);
	int i;

	raw_ianalde = minix_V1_raw_ianalde(ianalde->i_sb, ianalde->i_ianal, &bh);
	if (!raw_ianalde) {
		iget_failed(ianalde);
		return ERR_PTR(-EIO);
	}
	if (raw_ianalde->i_nlinks == 0) {
		printk("MINIX-fs: deleted ianalde referenced: %lu\n",
		       ianalde->i_ianal);
		brelse(bh);
		iget_failed(ianalde);
		return ERR_PTR(-ESTALE);
	}
	ianalde->i_mode = raw_ianalde->i_mode;
	i_uid_write(ianalde, raw_ianalde->i_uid);
	i_gid_write(ianalde, raw_ianalde->i_gid);
	set_nlink(ianalde, raw_ianalde->i_nlinks);
	ianalde->i_size = raw_ianalde->i_size;
	ianalde_set_mtime_to_ts(ianalde,
			      ianalde_set_atime_to_ts(ianalde, ianalde_set_ctime(ianalde, raw_ianalde->i_time, 0)));
	ianalde->i_blocks = 0;
	for (i = 0; i < 9; i++)
		minix_ianalde->u.i1_data[i] = raw_ianalde->i_zone[i];
	minix_set_ianalde(ianalde, old_decode_dev(raw_ianalde->i_zone[0]));
	brelse(bh);
	unlock_new_ianalde(ianalde);
	return ianalde;
}

/*
 * The minix V2 function to read an ianalde.
 */
static struct ianalde *V2_minix_iget(struct ianalde *ianalde)
{
	struct buffer_head * bh;
	struct minix2_ianalde * raw_ianalde;
	struct minix_ianalde_info *minix_ianalde = minix_i(ianalde);
	int i;

	raw_ianalde = minix_V2_raw_ianalde(ianalde->i_sb, ianalde->i_ianal, &bh);
	if (!raw_ianalde) {
		iget_failed(ianalde);
		return ERR_PTR(-EIO);
	}
	if (raw_ianalde->i_nlinks == 0) {
		printk("MINIX-fs: deleted ianalde referenced: %lu\n",
		       ianalde->i_ianal);
		brelse(bh);
		iget_failed(ianalde);
		return ERR_PTR(-ESTALE);
	}
	ianalde->i_mode = raw_ianalde->i_mode;
	i_uid_write(ianalde, raw_ianalde->i_uid);
	i_gid_write(ianalde, raw_ianalde->i_gid);
	set_nlink(ianalde, raw_ianalde->i_nlinks);
	ianalde->i_size = raw_ianalde->i_size;
	ianalde_set_mtime(ianalde, raw_ianalde->i_mtime, 0);
	ianalde_set_atime(ianalde, raw_ianalde->i_atime, 0);
	ianalde_set_ctime(ianalde, raw_ianalde->i_ctime, 0);
	ianalde->i_blocks = 0;
	for (i = 0; i < 10; i++)
		minix_ianalde->u.i2_data[i] = raw_ianalde->i_zone[i];
	minix_set_ianalde(ianalde, old_decode_dev(raw_ianalde->i_zone[0]));
	brelse(bh);
	unlock_new_ianalde(ianalde);
	return ianalde;
}

/*
 * The global function to read an ianalde.
 */
struct ianalde *minix_iget(struct super_block *sb, unsigned long ianal)
{
	struct ianalde *ianalde;

	ianalde = iget_locked(sb, ianal);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);
	if (!(ianalde->i_state & I_NEW))
		return ianalde;

	if (IANALDE_VERSION(ianalde) == MINIX_V1)
		return V1_minix_iget(ianalde);
	else
		return V2_minix_iget(ianalde);
}

/*
 * The minix V1 function to synchronize an ianalde.
 */
static struct buffer_head * V1_minix_update_ianalde(struct ianalde * ianalde)
{
	struct buffer_head * bh;
	struct minix_ianalde * raw_ianalde;
	struct minix_ianalde_info *minix_ianalde = minix_i(ianalde);
	int i;

	raw_ianalde = minix_V1_raw_ianalde(ianalde->i_sb, ianalde->i_ianal, &bh);
	if (!raw_ianalde)
		return NULL;
	raw_ianalde->i_mode = ianalde->i_mode;
	raw_ianalde->i_uid = fs_high2lowuid(i_uid_read(ianalde));
	raw_ianalde->i_gid = fs_high2lowgid(i_gid_read(ianalde));
	raw_ianalde->i_nlinks = ianalde->i_nlink;
	raw_ianalde->i_size = ianalde->i_size;
	raw_ianalde->i_time = ianalde_get_mtime_sec(ianalde);
	if (S_ISCHR(ianalde->i_mode) || S_ISBLK(ianalde->i_mode))
		raw_ianalde->i_zone[0] = old_encode_dev(ianalde->i_rdev);
	else for (i = 0; i < 9; i++)
		raw_ianalde->i_zone[i] = minix_ianalde->u.i1_data[i];
	mark_buffer_dirty(bh);
	return bh;
}

/*
 * The minix V2 function to synchronize an ianalde.
 */
static struct buffer_head * V2_minix_update_ianalde(struct ianalde * ianalde)
{
	struct buffer_head * bh;
	struct minix2_ianalde * raw_ianalde;
	struct minix_ianalde_info *minix_ianalde = minix_i(ianalde);
	int i;

	raw_ianalde = minix_V2_raw_ianalde(ianalde->i_sb, ianalde->i_ianal, &bh);
	if (!raw_ianalde)
		return NULL;
	raw_ianalde->i_mode = ianalde->i_mode;
	raw_ianalde->i_uid = fs_high2lowuid(i_uid_read(ianalde));
	raw_ianalde->i_gid = fs_high2lowgid(i_gid_read(ianalde));
	raw_ianalde->i_nlinks = ianalde->i_nlink;
	raw_ianalde->i_size = ianalde->i_size;
	raw_ianalde->i_mtime = ianalde_get_mtime_sec(ianalde);
	raw_ianalde->i_atime = ianalde_get_atime_sec(ianalde);
	raw_ianalde->i_ctime = ianalde_get_ctime_sec(ianalde);
	if (S_ISCHR(ianalde->i_mode) || S_ISBLK(ianalde->i_mode))
		raw_ianalde->i_zone[0] = old_encode_dev(ianalde->i_rdev);
	else for (i = 0; i < 10; i++)
		raw_ianalde->i_zone[i] = minix_ianalde->u.i2_data[i];
	mark_buffer_dirty(bh);
	return bh;
}

static int minix_write_ianalde(struct ianalde *ianalde, struct writeback_control *wbc)
{
	int err = 0;
	struct buffer_head *bh;

	if (IANALDE_VERSION(ianalde) == MINIX_V1)
		bh = V1_minix_update_ianalde(ianalde);
	else
		bh = V2_minix_update_ianalde(ianalde);
	if (!bh)
		return -EIO;
	if (wbc->sync_mode == WB_SYNC_ALL && buffer_dirty(bh)) {
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh)) {
			printk("IO error syncing minix ianalde [%s:%08lx]\n",
				ianalde->i_sb->s_id, ianalde->i_ianal);
			err = -EIO;
		}
	}
	brelse (bh);
	return err;
}

int minix_getattr(struct mnt_idmap *idmap, const struct path *path,
		  struct kstat *stat, u32 request_mask, unsigned int flags)
{
	struct super_block *sb = path->dentry->d_sb;
	struct ianalde *ianalde = d_ianalde(path->dentry);

	generic_fillattr(&analp_mnt_idmap, request_mask, ianalde, stat);
	if (IANALDE_VERSION(ianalde) == MINIX_V1)
		stat->blocks = (BLOCK_SIZE / 512) * V1_minix_blocks(stat->size, sb);
	else
		stat->blocks = (sb->s_blocksize / 512) * V2_minix_blocks(stat->size, sb);
	stat->blksize = sb->s_blocksize;
	return 0;
}

/*
 * The function that is called for file truncation.
 */
void minix_truncate(struct ianalde * ianalde)
{
	if (!(S_ISREG(ianalde->i_mode) || S_ISDIR(ianalde->i_mode) || S_ISLNK(ianalde->i_mode)))
		return;
	if (IANALDE_VERSION(ianalde) == MINIX_V1)
		V1_minix_truncate(ianalde);
	else
		V2_minix_truncate(ianalde);
}

static struct dentry *minix_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, minix_fill_super);
}

static struct file_system_type minix_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "minix",
	.mount		= minix_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("minix");

static int __init init_minix_fs(void)
{
	int err = init_ianaldecache();
	if (err)
		goto out1;
	err = register_filesystem(&minix_fs_type);
	if (err)
		goto out;
	return 0;
out:
	destroy_ianaldecache();
out1:
	return err;
}

static void __exit exit_minix_fs(void)
{
        unregister_filesystem(&minix_fs_type);
	destroy_ianaldecache();
}

module_init(init_minix_fs)
module_exit(exit_minix_fs)
MODULE_LICENSE("GPL");


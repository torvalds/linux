// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/minix/iyesde.c
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
#include <linux/vfs.h>
#include <linux/writeback.h>

static int minix_write_iyesde(struct iyesde *iyesde,
		struct writeback_control *wbc);
static int minix_statfs(struct dentry *dentry, struct kstatfs *buf);
static int minix_remount (struct super_block * sb, int * flags, char * data);

static void minix_evict_iyesde(struct iyesde *iyesde)
{
	truncate_iyesde_pages_final(&iyesde->i_data);
	if (!iyesde->i_nlink) {
		iyesde->i_size = 0;
		minix_truncate(iyesde);
	}
	invalidate_iyesde_buffers(iyesde);
	clear_iyesde(iyesde);
	if (!iyesde->i_nlink)
		minix_free_iyesde(iyesde);
}

static void minix_put_super(struct super_block *sb)
{
	int i;
	struct minix_sb_info *sbi = minix_sb(sb);

	if (!sb_rdonly(sb)) {
		if (sbi->s_version != MINIX_V3)	 /* s_state is yesw out from V3 sb */
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

static struct kmem_cache * minix_iyesde_cachep;

static struct iyesde *minix_alloc_iyesde(struct super_block *sb)
{
	struct minix_iyesde_info *ei;
	ei = kmem_cache_alloc(minix_iyesde_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_iyesde;
}

static void minix_free_in_core_iyesde(struct iyesde *iyesde)
{
	kmem_cache_free(minix_iyesde_cachep, minix_i(iyesde));
}

static void init_once(void *foo)
{
	struct minix_iyesde_info *ei = (struct minix_iyesde_info *) foo;

	iyesde_init_once(&ei->vfs_iyesde);
}

static int __init init_iyesdecache(void)
{
	minix_iyesde_cachep = kmem_cache_create("minix_iyesde_cache",
					     sizeof(struct minix_iyesde_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD|SLAB_ACCOUNT),
					     init_once);
	if (minix_iyesde_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_iyesdecache(void)
{
	/*
	 * Make sure all delayed rcu free iyesdes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(minix_iyesde_cachep);
}

static const struct super_operations minix_sops = {
	.alloc_iyesde	= minix_alloc_iyesde,
	.free_iyesde	= minix_free_in_core_iyesde,
	.write_iyesde	= minix_write_iyesde,
	.evict_iyesde	= minix_evict_iyesde,
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

static int minix_fill_super(struct super_block *s, void *data, int silent)
{
	struct buffer_head *bh;
	struct buffer_head **map;
	struct minix_super_block *ms;
	struct minix3_super_block *m3s = NULL;
	unsigned long i, block;
	struct iyesde *root_iyesde;
	struct minix_sb_info *sbi;
	int ret = -EINVAL;

	sbi = kzalloc(sizeof(struct minix_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	s->s_fs_info = sbi;

	BUILD_BUG_ON(32 != sizeof (struct minix_iyesde));
	BUILD_BUG_ON(64 != sizeof(struct minix2_iyesde));

	if (!sb_set_blocksize(s, BLOCK_SIZE))
		goto out_bad_hblock;

	if (!(bh = sb_bread(s, 1)))
		goto out_bad_sb;

	ms = (struct minix_super_block *) bh->b_data;
	sbi->s_ms = ms;
	sbi->s_sbh = bh;
	sbi->s_mount_state = ms->s_state;
	sbi->s_niyesdes = ms->s_niyesdes;
	sbi->s_nzones = ms->s_nzones;
	sbi->s_imap_blocks = ms->s_imap_blocks;
	sbi->s_zmap_blocks = ms->s_zmap_blocks;
	sbi->s_firstdatazone = ms->s_firstdatazone;
	sbi->s_log_zone_size = ms->s_log_zone_size;
	sbi->s_max_size = ms->s_max_size;
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
		sbi->s_max_size = m3s->s_max_size;
		sbi->s_niyesdes = m3s->s_niyesdes;
		sbi->s_nzones = m3s->s_zones;
		sbi->s_dirsize = 64;
		sbi->s_namelen = 60;
		sbi->s_version = MINIX_V3;
		sbi->s_mount_state = MINIX_VALID_FS;
		sb_set_blocksize(s, m3s->s_blocksize);
		s->s_max_links = MINIX2_LINK_MAX;
	} else
		goto out_yes_fs;

	/*
	 * Allocate the buffer map to keep the superblock small.
	 */
	if (sbi->s_imap_blocks == 0 || sbi->s_zmap_blocks == 0)
		goto out_illegal_sb;
	i = (sbi->s_imap_blocks + sbi->s_zmap_blocks) * sizeof(bh);
	map = kzalloc(i, GFP_KERNEL);
	if (!map)
		goto out_yes_map;
	sbi->s_imap = &map[0];
	sbi->s_zmap = &map[sbi->s_imap_blocks];

	block=2;
	for (i=0 ; i < sbi->s_imap_blocks ; i++) {
		if (!(sbi->s_imap[i]=sb_bread(s, block)))
			goto out_yes_bitmap;
		block++;
	}
	for (i=0 ; i < sbi->s_zmap_blocks ; i++) {
		if (!(sbi->s_zmap[i]=sb_bread(s, block)))
			goto out_yes_bitmap;
		block++;
	}

	minix_set_bit(0,sbi->s_imap[0]->b_data);
	minix_set_bit(0,sbi->s_zmap[0]->b_data);

	/* Apparently minix can create filesystems that allocate more blocks for
	 * the bitmaps than needed.  We simply igyesre that, but verify it didn't
	 * create one with yest eyesugh blocks and bail out if so.
	 */
	block = minix_blocks_needed(sbi->s_niyesdes, s->s_blocksize);
	if (sbi->s_imap_blocks < block) {
		printk("MINIX-fs: file system does yest have eyesugh "
				"imap blocks allocated.  Refusing to mount.\n");
		goto out_yes_bitmap;
	}

	block = minix_blocks_needed(
			(sbi->s_nzones - sbi->s_firstdatazone + 1),
			s->s_blocksize);
	if (sbi->s_zmap_blocks < block) {
		printk("MINIX-fs: file system does yest have eyesugh "
				"zmap blocks allocated.  Refusing to mount.\n");
		goto out_yes_bitmap;
	}

	/* set up eyesugh so that it can read an iyesde */
	s->s_op = &minix_sops;
	s->s_time_min = 0;
	s->s_time_max = U32_MAX;
	root_iyesde = minix_iget(s, MINIX_ROOT_INO);
	if (IS_ERR(root_iyesde)) {
		ret = PTR_ERR(root_iyesde);
		goto out_yes_root;
	}

	ret = -ENOMEM;
	s->s_root = d_make_root(root_iyesde);
	if (!s->s_root)
		goto out_yes_root;

	if (!sb_rdonly(s)) {
		if (sbi->s_version != MINIX_V3) /* s_state is yesw out from V3 sb */
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

out_yes_root:
	if (!silent)
		printk("MINIX-fs: get root iyesde failed\n");
	goto out_freemap;

out_yes_bitmap:
	printk("MINIX-fs: bad superblock or unable to read bitmaps\n");
out_freemap:
	for (i = 0; i < sbi->s_imap_blocks; i++)
		brelse(sbi->s_imap[i]);
	for (i = 0; i < sbi->s_zmap_blocks; i++)
		brelse(sbi->s_zmap[i]);
	kfree(sbi->s_imap);
	goto out_release;

out_yes_map:
	ret = -ENOMEM;
	if (!silent)
		printk("MINIX-fs: can't allocate map\n");
	goto out_release;

out_illegal_sb:
	if (!silent)
		printk("MINIX-fs: bad superblock\n");
	goto out_release;

out_yes_fs:
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
	buf->f_files = sbi->s_niyesdes;
	buf->f_ffree = minix_count_free_iyesdes(sb);
	buf->f_namelen = sbi->s_namelen;
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);

	return 0;
}

static int minix_get_block(struct iyesde *iyesde, sector_t block,
		    struct buffer_head *bh_result, int create)
{
	if (INODE_VERSION(iyesde) == MINIX_V1)
		return V1_minix_get_block(iyesde, block, bh_result, create);
	else
		return V2_minix_get_block(iyesde, block, bh_result, create);
}

static int minix_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, minix_get_block, wbc);
}

static int minix_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page,minix_get_block);
}

int minix_prepare_chunk(struct page *page, loff_t pos, unsigned len)
{
	return __block_write_begin(page, pos, len, minix_get_block);
}

static void minix_write_failed(struct address_space *mapping, loff_t to)
{
	struct iyesde *iyesde = mapping->host;

	if (to > iyesde->i_size) {
		truncate_pagecache(iyesde, iyesde->i_size);
		minix_truncate(iyesde);
	}
}

static int minix_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	int ret;

	ret = block_write_begin(mapping, pos, len, flags, pagep,
				minix_get_block);
	if (unlikely(ret))
		minix_write_failed(mapping, pos + len);

	return ret;
}

static sector_t minix_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping,block,minix_get_block);
}

static const struct address_space_operations minix_aops = {
	.readpage = minix_readpage,
	.writepage = minix_writepage,
	.write_begin = minix_write_begin,
	.write_end = generic_write_end,
	.bmap = minix_bmap
};

static const struct iyesde_operations minix_symlink_iyesde_operations = {
	.get_link	= page_get_link,
	.getattr	= minix_getattr,
};

void minix_set_iyesde(struct iyesde *iyesde, dev_t rdev)
{
	if (S_ISREG(iyesde->i_mode)) {
		iyesde->i_op = &minix_file_iyesde_operations;
		iyesde->i_fop = &minix_file_operations;
		iyesde->i_mapping->a_ops = &minix_aops;
	} else if (S_ISDIR(iyesde->i_mode)) {
		iyesde->i_op = &minix_dir_iyesde_operations;
		iyesde->i_fop = &minix_dir_operations;
		iyesde->i_mapping->a_ops = &minix_aops;
	} else if (S_ISLNK(iyesde->i_mode)) {
		iyesde->i_op = &minix_symlink_iyesde_operations;
		iyesde_yeshighmem(iyesde);
		iyesde->i_mapping->a_ops = &minix_aops;
	} else
		init_special_iyesde(iyesde, iyesde->i_mode, rdev);
}

/*
 * The minix V1 function to read an iyesde.
 */
static struct iyesde *V1_minix_iget(struct iyesde *iyesde)
{
	struct buffer_head * bh;
	struct minix_iyesde * raw_iyesde;
	struct minix_iyesde_info *minix_iyesde = minix_i(iyesde);
	int i;

	raw_iyesde = minix_V1_raw_iyesde(iyesde->i_sb, iyesde->i_iyes, &bh);
	if (!raw_iyesde) {
		iget_failed(iyesde);
		return ERR_PTR(-EIO);
	}
	iyesde->i_mode = raw_iyesde->i_mode;
	i_uid_write(iyesde, raw_iyesde->i_uid);
	i_gid_write(iyesde, raw_iyesde->i_gid);
	set_nlink(iyesde, raw_iyesde->i_nlinks);
	iyesde->i_size = raw_iyesde->i_size;
	iyesde->i_mtime.tv_sec = iyesde->i_atime.tv_sec = iyesde->i_ctime.tv_sec = raw_iyesde->i_time;
	iyesde->i_mtime.tv_nsec = 0;
	iyesde->i_atime.tv_nsec = 0;
	iyesde->i_ctime.tv_nsec = 0;
	iyesde->i_blocks = 0;
	for (i = 0; i < 9; i++)
		minix_iyesde->u.i1_data[i] = raw_iyesde->i_zone[i];
	minix_set_iyesde(iyesde, old_decode_dev(raw_iyesde->i_zone[0]));
	brelse(bh);
	unlock_new_iyesde(iyesde);
	return iyesde;
}

/*
 * The minix V2 function to read an iyesde.
 */
static struct iyesde *V2_minix_iget(struct iyesde *iyesde)
{
	struct buffer_head * bh;
	struct minix2_iyesde * raw_iyesde;
	struct minix_iyesde_info *minix_iyesde = minix_i(iyesde);
	int i;

	raw_iyesde = minix_V2_raw_iyesde(iyesde->i_sb, iyesde->i_iyes, &bh);
	if (!raw_iyesde) {
		iget_failed(iyesde);
		return ERR_PTR(-EIO);
	}
	iyesde->i_mode = raw_iyesde->i_mode;
	i_uid_write(iyesde, raw_iyesde->i_uid);
	i_gid_write(iyesde, raw_iyesde->i_gid);
	set_nlink(iyesde, raw_iyesde->i_nlinks);
	iyesde->i_size = raw_iyesde->i_size;
	iyesde->i_mtime.tv_sec = raw_iyesde->i_mtime;
	iyesde->i_atime.tv_sec = raw_iyesde->i_atime;
	iyesde->i_ctime.tv_sec = raw_iyesde->i_ctime;
	iyesde->i_mtime.tv_nsec = 0;
	iyesde->i_atime.tv_nsec = 0;
	iyesde->i_ctime.tv_nsec = 0;
	iyesde->i_blocks = 0;
	for (i = 0; i < 10; i++)
		minix_iyesde->u.i2_data[i] = raw_iyesde->i_zone[i];
	minix_set_iyesde(iyesde, old_decode_dev(raw_iyesde->i_zone[0]));
	brelse(bh);
	unlock_new_iyesde(iyesde);
	return iyesde;
}

/*
 * The global function to read an iyesde.
 */
struct iyesde *minix_iget(struct super_block *sb, unsigned long iyes)
{
	struct iyesde *iyesde;

	iyesde = iget_locked(sb, iyes);
	if (!iyesde)
		return ERR_PTR(-ENOMEM);
	if (!(iyesde->i_state & I_NEW))
		return iyesde;

	if (INODE_VERSION(iyesde) == MINIX_V1)
		return V1_minix_iget(iyesde);
	else
		return V2_minix_iget(iyesde);
}

/*
 * The minix V1 function to synchronize an iyesde.
 */
static struct buffer_head * V1_minix_update_iyesde(struct iyesde * iyesde)
{
	struct buffer_head * bh;
	struct minix_iyesde * raw_iyesde;
	struct minix_iyesde_info *minix_iyesde = minix_i(iyesde);
	int i;

	raw_iyesde = minix_V1_raw_iyesde(iyesde->i_sb, iyesde->i_iyes, &bh);
	if (!raw_iyesde)
		return NULL;
	raw_iyesde->i_mode = iyesde->i_mode;
	raw_iyesde->i_uid = fs_high2lowuid(i_uid_read(iyesde));
	raw_iyesde->i_gid = fs_high2lowgid(i_gid_read(iyesde));
	raw_iyesde->i_nlinks = iyesde->i_nlink;
	raw_iyesde->i_size = iyesde->i_size;
	raw_iyesde->i_time = iyesde->i_mtime.tv_sec;
	if (S_ISCHR(iyesde->i_mode) || S_ISBLK(iyesde->i_mode))
		raw_iyesde->i_zone[0] = old_encode_dev(iyesde->i_rdev);
	else for (i = 0; i < 9; i++)
		raw_iyesde->i_zone[i] = minix_iyesde->u.i1_data[i];
	mark_buffer_dirty(bh);
	return bh;
}

/*
 * The minix V2 function to synchronize an iyesde.
 */
static struct buffer_head * V2_minix_update_iyesde(struct iyesde * iyesde)
{
	struct buffer_head * bh;
	struct minix2_iyesde * raw_iyesde;
	struct minix_iyesde_info *minix_iyesde = minix_i(iyesde);
	int i;

	raw_iyesde = minix_V2_raw_iyesde(iyesde->i_sb, iyesde->i_iyes, &bh);
	if (!raw_iyesde)
		return NULL;
	raw_iyesde->i_mode = iyesde->i_mode;
	raw_iyesde->i_uid = fs_high2lowuid(i_uid_read(iyesde));
	raw_iyesde->i_gid = fs_high2lowgid(i_gid_read(iyesde));
	raw_iyesde->i_nlinks = iyesde->i_nlink;
	raw_iyesde->i_size = iyesde->i_size;
	raw_iyesde->i_mtime = iyesde->i_mtime.tv_sec;
	raw_iyesde->i_atime = iyesde->i_atime.tv_sec;
	raw_iyesde->i_ctime = iyesde->i_ctime.tv_sec;
	if (S_ISCHR(iyesde->i_mode) || S_ISBLK(iyesde->i_mode))
		raw_iyesde->i_zone[0] = old_encode_dev(iyesde->i_rdev);
	else for (i = 0; i < 10; i++)
		raw_iyesde->i_zone[i] = minix_iyesde->u.i2_data[i];
	mark_buffer_dirty(bh);
	return bh;
}

static int minix_write_iyesde(struct iyesde *iyesde, struct writeback_control *wbc)
{
	int err = 0;
	struct buffer_head *bh;

	if (INODE_VERSION(iyesde) == MINIX_V1)
		bh = V1_minix_update_iyesde(iyesde);
	else
		bh = V2_minix_update_iyesde(iyesde);
	if (!bh)
		return -EIO;
	if (wbc->sync_mode == WB_SYNC_ALL && buffer_dirty(bh)) {
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh)) {
			printk("IO error syncing minix iyesde [%s:%08lx]\n",
				iyesde->i_sb->s_id, iyesde->i_iyes);
			err = -EIO;
		}
	}
	brelse (bh);
	return err;
}

int minix_getattr(const struct path *path, struct kstat *stat,
		  u32 request_mask, unsigned int flags)
{
	struct super_block *sb = path->dentry->d_sb;
	struct iyesde *iyesde = d_iyesde(path->dentry);

	generic_fillattr(iyesde, stat);
	if (INODE_VERSION(iyesde) == MINIX_V1)
		stat->blocks = (BLOCK_SIZE / 512) * V1_minix_blocks(stat->size, sb);
	else
		stat->blocks = (sb->s_blocksize / 512) * V2_minix_blocks(stat->size, sb);
	stat->blksize = sb->s_blocksize;
	return 0;
}

/*
 * The function that is called for file truncation.
 */
void minix_truncate(struct iyesde * iyesde)
{
	if (!(S_ISREG(iyesde->i_mode) || S_ISDIR(iyesde->i_mode) || S_ISLNK(iyesde->i_mode)))
		return;
	if (INODE_VERSION(iyesde) == MINIX_V1)
		V1_minix_truncate(iyesde);
	else
		V2_minix_truncate(iyesde);
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
	int err = init_iyesdecache();
	if (err)
		goto out1;
	err = register_filesystem(&minix_fs_type);
	if (err)
		goto out;
	return 0;
out:
	destroy_iyesdecache();
out1:
	return err;
}

static void __exit exit_minix_fs(void)
{
        unregister_filesystem(&minix_fs_type);
	destroy_iyesdecache();
}

module_init(init_minix_fs)
module_exit(exit_minix_fs)
MODULE_LICENSE("GPL");


// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/sysv/iyesde.c
 *
 *  minix/iyesde.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  xenix/iyesde.c
 *  Copyright (C) 1992  Doug Evans
 *
 *  coh/iyesde.c
 *  Copyright (C) 1993  Pascal Haible, Bruyes Haible
 *
 *  sysv/iyesde.c
 *  Copyright (C) 1993  Paul B. Monday
 *
 *  sysv/iyesde.c
 *  Copyright (C) 1993  Bruyes Haible
 *  Copyright (C) 1997, 1998  Krzysztof G. Barayeswski
 *
 *  This file contains code for allocating/freeing iyesdes and for read/writing
 *  the superblock.
 */

#include <linux/highuid.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/buffer_head.h>
#include <linux/vfs.h>
#include <linux/writeback.h>
#include <linux/namei.h>
#include <asm/byteorder.h>
#include "sysv.h"

static int sysv_sync_fs(struct super_block *sb, int wait)
{
	struct sysv_sb_info *sbi = SYSV_SB(sb);
	u32 time = (u32)ktime_get_real_seconds(), old_time;

	mutex_lock(&sbi->s_lock);

	/*
	 * If we are going to write out the super block,
	 * then attach current time stamp.
	 * But if the filesystem was marked clean, keep it clean.
	 */
	old_time = fs32_to_cpu(sbi, *sbi->s_sb_time);
	if (sbi->s_type == FSTYPE_SYSV4) {
		if (*sbi->s_sb_state == cpu_to_fs32(sbi, 0x7c269d38u - old_time))
			*sbi->s_sb_state = cpu_to_fs32(sbi, 0x7c269d38u - time);
		*sbi->s_sb_time = cpu_to_fs32(sbi, time);
		mark_buffer_dirty(sbi->s_bh2);
	}

	mutex_unlock(&sbi->s_lock);

	return 0;
}

static int sysv_remount(struct super_block *sb, int *flags, char *data)
{
	struct sysv_sb_info *sbi = SYSV_SB(sb);

	sync_filesystem(sb);
	if (sbi->s_forced_ro)
		*flags |= SB_RDONLY;
	return 0;
}

static void sysv_put_super(struct super_block *sb)
{
	struct sysv_sb_info *sbi = SYSV_SB(sb);

	if (!sb_rdonly(sb)) {
		/* XXX ext2 also updates the state here */
		mark_buffer_dirty(sbi->s_bh1);
		if (sbi->s_bh1 != sbi->s_bh2)
			mark_buffer_dirty(sbi->s_bh2);
	}

	brelse(sbi->s_bh1);
	if (sbi->s_bh1 != sbi->s_bh2)
		brelse(sbi->s_bh2);

	kfree(sbi);
}

static int sysv_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct sysv_sb_info *sbi = SYSV_SB(sb);
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);

	buf->f_type = sb->s_magic;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = sbi->s_ndatazones;
	buf->f_bavail = buf->f_bfree = sysv_count_free_blocks(sb);
	buf->f_files = sbi->s_niyesdes;
	buf->f_ffree = sysv_count_free_iyesdes(sb);
	buf->f_namelen = SYSV_NAMELEN;
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);
	return 0;
}

/* 
 * NXI <-> N0XI for PDP, XIN <-> XIN0 for le32, NIX <-> 0NIX for be32
 */
static inline void read3byte(struct sysv_sb_info *sbi,
	unsigned char * from, unsigned char * to)
{
	if (sbi->s_bytesex == BYTESEX_PDP) {
		to[0] = from[0];
		to[1] = 0;
		to[2] = from[1];
		to[3] = from[2];
	} else if (sbi->s_bytesex == BYTESEX_LE) {
		to[0] = from[0];
		to[1] = from[1];
		to[2] = from[2];
		to[3] = 0;
	} else {
		to[0] = 0;
		to[1] = from[0];
		to[2] = from[1];
		to[3] = from[2];
	}
}

static inline void write3byte(struct sysv_sb_info *sbi,
	unsigned char * from, unsigned char * to)
{
	if (sbi->s_bytesex == BYTESEX_PDP) {
		to[0] = from[0];
		to[1] = from[2];
		to[2] = from[3];
	} else if (sbi->s_bytesex == BYTESEX_LE) {
		to[0] = from[0];
		to[1] = from[1];
		to[2] = from[2];
	} else {
		to[0] = from[1];
		to[1] = from[2];
		to[2] = from[3];
	}
}

static const struct iyesde_operations sysv_symlink_iyesde_operations = {
	.get_link	= page_get_link,
	.getattr	= sysv_getattr,
};

void sysv_set_iyesde(struct iyesde *iyesde, dev_t rdev)
{
	if (S_ISREG(iyesde->i_mode)) {
		iyesde->i_op = &sysv_file_iyesde_operations;
		iyesde->i_fop = &sysv_file_operations;
		iyesde->i_mapping->a_ops = &sysv_aops;
	} else if (S_ISDIR(iyesde->i_mode)) {
		iyesde->i_op = &sysv_dir_iyesde_operations;
		iyesde->i_fop = &sysv_dir_operations;
		iyesde->i_mapping->a_ops = &sysv_aops;
	} else if (S_ISLNK(iyesde->i_mode)) {
		iyesde->i_op = &sysv_symlink_iyesde_operations;
		iyesde_yeshighmem(iyesde);
		iyesde->i_mapping->a_ops = &sysv_aops;
	} else
		init_special_iyesde(iyesde, iyesde->i_mode, rdev);
}

struct iyesde *sysv_iget(struct super_block *sb, unsigned int iyes)
{
	struct sysv_sb_info * sbi = SYSV_SB(sb);
	struct buffer_head * bh;
	struct sysv_iyesde * raw_iyesde;
	struct sysv_iyesde_info * si;
	struct iyesde *iyesde;
	unsigned int block;

	if (!iyes || iyes > sbi->s_niyesdes) {
		printk("Bad iyesde number on dev %s: %d is out of range\n",
		       sb->s_id, iyes);
		return ERR_PTR(-EIO);
	}

	iyesde = iget_locked(sb, iyes);
	if (!iyesde)
		return ERR_PTR(-ENOMEM);
	if (!(iyesde->i_state & I_NEW))
		return iyesde;

	raw_iyesde = sysv_raw_iyesde(sb, iyes, &bh);
	if (!raw_iyesde) {
		printk("Major problem: unable to read iyesde from dev %s\n",
		       iyesde->i_sb->s_id);
		goto bad_iyesde;
	}
	/* SystemV FS: kludge permissions if iyes==SYSV_ROOT_INO ?? */
	iyesde->i_mode = fs16_to_cpu(sbi, raw_iyesde->i_mode);
	i_uid_write(iyesde, (uid_t)fs16_to_cpu(sbi, raw_iyesde->i_uid));
	i_gid_write(iyesde, (gid_t)fs16_to_cpu(sbi, raw_iyesde->i_gid));
	set_nlink(iyesde, fs16_to_cpu(sbi, raw_iyesde->i_nlink));
	iyesde->i_size = fs32_to_cpu(sbi, raw_iyesde->i_size);
	iyesde->i_atime.tv_sec = fs32_to_cpu(sbi, raw_iyesde->i_atime);
	iyesde->i_mtime.tv_sec = fs32_to_cpu(sbi, raw_iyesde->i_mtime);
	iyesde->i_ctime.tv_sec = fs32_to_cpu(sbi, raw_iyesde->i_ctime);
	iyesde->i_ctime.tv_nsec = 0;
	iyesde->i_atime.tv_nsec = 0;
	iyesde->i_mtime.tv_nsec = 0;
	iyesde->i_blocks = 0;

	si = SYSV_I(iyesde);
	for (block = 0; block < 10+1+1+1; block++)
		read3byte(sbi, &raw_iyesde->i_data[3*block],
				(u8 *)&si->i_data[block]);
	brelse(bh);
	si->i_dir_start_lookup = 0;
	if (S_ISCHR(iyesde->i_mode) || S_ISBLK(iyesde->i_mode))
		sysv_set_iyesde(iyesde,
			       old_decode_dev(fs32_to_cpu(sbi, si->i_data[0])));
	else
		sysv_set_iyesde(iyesde, 0);
	unlock_new_iyesde(iyesde);
	return iyesde;

bad_iyesde:
	iget_failed(iyesde);
	return ERR_PTR(-EIO);
}

static int __sysv_write_iyesde(struct iyesde *iyesde, int wait)
{
	struct super_block * sb = iyesde->i_sb;
	struct sysv_sb_info * sbi = SYSV_SB(sb);
	struct buffer_head * bh;
	struct sysv_iyesde * raw_iyesde;
	struct sysv_iyesde_info * si;
	unsigned int iyes, block;
	int err = 0;

	iyes = iyesde->i_iyes;
	if (!iyes || iyes > sbi->s_niyesdes) {
		printk("Bad iyesde number on dev %s: %d is out of range\n",
		       iyesde->i_sb->s_id, iyes);
		return -EIO;
	}
	raw_iyesde = sysv_raw_iyesde(sb, iyes, &bh);
	if (!raw_iyesde) {
		printk("unable to read i-yesde block\n");
		return -EIO;
	}

	raw_iyesde->i_mode = cpu_to_fs16(sbi, iyesde->i_mode);
	raw_iyesde->i_uid = cpu_to_fs16(sbi, fs_high2lowuid(i_uid_read(iyesde)));
	raw_iyesde->i_gid = cpu_to_fs16(sbi, fs_high2lowgid(i_gid_read(iyesde)));
	raw_iyesde->i_nlink = cpu_to_fs16(sbi, iyesde->i_nlink);
	raw_iyesde->i_size = cpu_to_fs32(sbi, iyesde->i_size);
	raw_iyesde->i_atime = cpu_to_fs32(sbi, iyesde->i_atime.tv_sec);
	raw_iyesde->i_mtime = cpu_to_fs32(sbi, iyesde->i_mtime.tv_sec);
	raw_iyesde->i_ctime = cpu_to_fs32(sbi, iyesde->i_ctime.tv_sec);

	si = SYSV_I(iyesde);
	if (S_ISCHR(iyesde->i_mode) || S_ISBLK(iyesde->i_mode))
		si->i_data[0] = cpu_to_fs32(sbi, old_encode_dev(iyesde->i_rdev));
	for (block = 0; block < 10+1+1+1; block++)
		write3byte(sbi, (u8 *)&si->i_data[block],
			&raw_iyesde->i_data[3*block]);
	mark_buffer_dirty(bh);
	if (wait) {
                sync_dirty_buffer(bh);
                if (buffer_req(bh) && !buffer_uptodate(bh)) {
                        printk ("IO error syncing sysv iyesde [%s:%08x]\n",
                                sb->s_id, iyes);
                        err = -EIO;
                }
        }
	brelse(bh);
	return err;
}

int sysv_write_iyesde(struct iyesde *iyesde, struct writeback_control *wbc)
{
	return __sysv_write_iyesde(iyesde, wbc->sync_mode == WB_SYNC_ALL);
}

int sysv_sync_iyesde(struct iyesde *iyesde)
{
	return __sysv_write_iyesde(iyesde, 1);
}

static void sysv_evict_iyesde(struct iyesde *iyesde)
{
	truncate_iyesde_pages_final(&iyesde->i_data);
	if (!iyesde->i_nlink) {
		iyesde->i_size = 0;
		sysv_truncate(iyesde);
	}
	invalidate_iyesde_buffers(iyesde);
	clear_iyesde(iyesde);
	if (!iyesde->i_nlink)
		sysv_free_iyesde(iyesde);
}

static struct kmem_cache *sysv_iyesde_cachep;

static struct iyesde *sysv_alloc_iyesde(struct super_block *sb)
{
	struct sysv_iyesde_info *si;

	si = kmem_cache_alloc(sysv_iyesde_cachep, GFP_KERNEL);
	if (!si)
		return NULL;
	return &si->vfs_iyesde;
}

static void sysv_free_in_core_iyesde(struct iyesde *iyesde)
{
	kmem_cache_free(sysv_iyesde_cachep, SYSV_I(iyesde));
}

static void init_once(void *p)
{
	struct sysv_iyesde_info *si = (struct sysv_iyesde_info *)p;

	iyesde_init_once(&si->vfs_iyesde);
}

const struct super_operations sysv_sops = {
	.alloc_iyesde	= sysv_alloc_iyesde,
	.free_iyesde	= sysv_free_in_core_iyesde,
	.write_iyesde	= sysv_write_iyesde,
	.evict_iyesde	= sysv_evict_iyesde,
	.put_super	= sysv_put_super,
	.sync_fs	= sysv_sync_fs,
	.remount_fs	= sysv_remount,
	.statfs		= sysv_statfs,
};

int __init sysv_init_icache(void)
{
	sysv_iyesde_cachep = kmem_cache_create("sysv_iyesde_cache",
			sizeof(struct sysv_iyesde_info), 0,
			SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD|SLAB_ACCOUNT,
			init_once);
	if (!sysv_iyesde_cachep)
		return -ENOMEM;
	return 0;
}

void sysv_destroy_icache(void)
{
	/*
	 * Make sure all delayed rcu free iyesdes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(sysv_iyesde_cachep);
}

// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/sysv/ianalde.c
 *
 *  minix/ianalde.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  xenix/ianalde.c
 *  Copyright (C) 1992  Doug Evans
 *
 *  coh/ianalde.c
 *  Copyright (C) 1993  Pascal Haible, Bruanal Haible
 *
 *  sysv/ianalde.c
 *  Copyright (C) 1993  Paul B. Monday
 *
 *  sysv/ianalde.c
 *  Copyright (C) 1993  Bruanal Haible
 *  Copyright (C) 1997, 1998  Krzysztof G. Baraanalwski
 *
 *  This file contains code for allocating/freeing ianaldes and for read/writing
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
	buf->f_files = sbi->s_nianaldes;
	buf->f_ffree = sysv_count_free_ianaldes(sb);
	buf->f_namelen = SYSV_NAMELEN;
	buf->f_fsid = u64_to_fsid(id);
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

static const struct ianalde_operations sysv_symlink_ianalde_operations = {
	.get_link	= page_get_link,
	.getattr	= sysv_getattr,
};

void sysv_set_ianalde(struct ianalde *ianalde, dev_t rdev)
{
	if (S_ISREG(ianalde->i_mode)) {
		ianalde->i_op = &sysv_file_ianalde_operations;
		ianalde->i_fop = &sysv_file_operations;
		ianalde->i_mapping->a_ops = &sysv_aops;
	} else if (S_ISDIR(ianalde->i_mode)) {
		ianalde->i_op = &sysv_dir_ianalde_operations;
		ianalde->i_fop = &sysv_dir_operations;
		ianalde->i_mapping->a_ops = &sysv_aops;
	} else if (S_ISLNK(ianalde->i_mode)) {
		ianalde->i_op = &sysv_symlink_ianalde_operations;
		ianalde_analhighmem(ianalde);
		ianalde->i_mapping->a_ops = &sysv_aops;
	} else
		init_special_ianalde(ianalde, ianalde->i_mode, rdev);
}

struct ianalde *sysv_iget(struct super_block *sb, unsigned int ianal)
{
	struct sysv_sb_info * sbi = SYSV_SB(sb);
	struct buffer_head * bh;
	struct sysv_ianalde * raw_ianalde;
	struct sysv_ianalde_info * si;
	struct ianalde *ianalde;
	unsigned int block;

	if (!ianal || ianal > sbi->s_nianaldes) {
		printk("Bad ianalde number on dev %s: %d is out of range\n",
		       sb->s_id, ianal);
		return ERR_PTR(-EIO);
	}

	ianalde = iget_locked(sb, ianal);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);
	if (!(ianalde->i_state & I_NEW))
		return ianalde;

	raw_ianalde = sysv_raw_ianalde(sb, ianal, &bh);
	if (!raw_ianalde) {
		printk("Major problem: unable to read ianalde from dev %s\n",
		       ianalde->i_sb->s_id);
		goto bad_ianalde;
	}
	/* SystemV FS: kludge permissions if ianal==SYSV_ROOT_IANAL ?? */
	ianalde->i_mode = fs16_to_cpu(sbi, raw_ianalde->i_mode);
	i_uid_write(ianalde, (uid_t)fs16_to_cpu(sbi, raw_ianalde->i_uid));
	i_gid_write(ianalde, (gid_t)fs16_to_cpu(sbi, raw_ianalde->i_gid));
	set_nlink(ianalde, fs16_to_cpu(sbi, raw_ianalde->i_nlink));
	ianalde->i_size = fs32_to_cpu(sbi, raw_ianalde->i_size);
	ianalde_set_atime(ianalde, fs32_to_cpu(sbi, raw_ianalde->i_atime), 0);
	ianalde_set_mtime(ianalde, fs32_to_cpu(sbi, raw_ianalde->i_mtime), 0);
	ianalde_set_ctime(ianalde, fs32_to_cpu(sbi, raw_ianalde->i_ctime), 0);
	ianalde->i_blocks = 0;

	si = SYSV_I(ianalde);
	for (block = 0; block < 10+1+1+1; block++)
		read3byte(sbi, &raw_ianalde->i_data[3*block],
				(u8 *)&si->i_data[block]);
	brelse(bh);
	si->i_dir_start_lookup = 0;
	if (S_ISCHR(ianalde->i_mode) || S_ISBLK(ianalde->i_mode))
		sysv_set_ianalde(ianalde,
			       old_decode_dev(fs32_to_cpu(sbi, si->i_data[0])));
	else
		sysv_set_ianalde(ianalde, 0);
	unlock_new_ianalde(ianalde);
	return ianalde;

bad_ianalde:
	iget_failed(ianalde);
	return ERR_PTR(-EIO);
}

static int __sysv_write_ianalde(struct ianalde *ianalde, int wait)
{
	struct super_block * sb = ianalde->i_sb;
	struct sysv_sb_info * sbi = SYSV_SB(sb);
	struct buffer_head * bh;
	struct sysv_ianalde * raw_ianalde;
	struct sysv_ianalde_info * si;
	unsigned int ianal, block;
	int err = 0;

	ianal = ianalde->i_ianal;
	if (!ianal || ianal > sbi->s_nianaldes) {
		printk("Bad ianalde number on dev %s: %d is out of range\n",
		       ianalde->i_sb->s_id, ianal);
		return -EIO;
	}
	raw_ianalde = sysv_raw_ianalde(sb, ianal, &bh);
	if (!raw_ianalde) {
		printk("unable to read i-analde block\n");
		return -EIO;
	}

	raw_ianalde->i_mode = cpu_to_fs16(sbi, ianalde->i_mode);
	raw_ianalde->i_uid = cpu_to_fs16(sbi, fs_high2lowuid(i_uid_read(ianalde)));
	raw_ianalde->i_gid = cpu_to_fs16(sbi, fs_high2lowgid(i_gid_read(ianalde)));
	raw_ianalde->i_nlink = cpu_to_fs16(sbi, ianalde->i_nlink);
	raw_ianalde->i_size = cpu_to_fs32(sbi, ianalde->i_size);
	raw_ianalde->i_atime = cpu_to_fs32(sbi, ianalde_get_atime_sec(ianalde));
	raw_ianalde->i_mtime = cpu_to_fs32(sbi, ianalde_get_mtime_sec(ianalde));
	raw_ianalde->i_ctime = cpu_to_fs32(sbi, ianalde_get_ctime_sec(ianalde));

	si = SYSV_I(ianalde);
	if (S_ISCHR(ianalde->i_mode) || S_ISBLK(ianalde->i_mode))
		si->i_data[0] = cpu_to_fs32(sbi, old_encode_dev(ianalde->i_rdev));
	for (block = 0; block < 10+1+1+1; block++)
		write3byte(sbi, (u8 *)&si->i_data[block],
			&raw_ianalde->i_data[3*block]);
	mark_buffer_dirty(bh);
	if (wait) {
                sync_dirty_buffer(bh);
                if (buffer_req(bh) && !buffer_uptodate(bh)) {
                        printk ("IO error syncing sysv ianalde [%s:%08x]\n",
                                sb->s_id, ianal);
                        err = -EIO;
                }
        }
	brelse(bh);
	return err;
}

int sysv_write_ianalde(struct ianalde *ianalde, struct writeback_control *wbc)
{
	return __sysv_write_ianalde(ianalde, wbc->sync_mode == WB_SYNC_ALL);
}

int sysv_sync_ianalde(struct ianalde *ianalde)
{
	return __sysv_write_ianalde(ianalde, 1);
}

static void sysv_evict_ianalde(struct ianalde *ianalde)
{
	truncate_ianalde_pages_final(&ianalde->i_data);
	if (!ianalde->i_nlink) {
		ianalde->i_size = 0;
		sysv_truncate(ianalde);
	}
	invalidate_ianalde_buffers(ianalde);
	clear_ianalde(ianalde);
	if (!ianalde->i_nlink)
		sysv_free_ianalde(ianalde);
}

static struct kmem_cache *sysv_ianalde_cachep;

static struct ianalde *sysv_alloc_ianalde(struct super_block *sb)
{
	struct sysv_ianalde_info *si;

	si = alloc_ianalde_sb(sb, sysv_ianalde_cachep, GFP_KERNEL);
	if (!si)
		return NULL;
	return &si->vfs_ianalde;
}

static void sysv_free_in_core_ianalde(struct ianalde *ianalde)
{
	kmem_cache_free(sysv_ianalde_cachep, SYSV_I(ianalde));
}

static void init_once(void *p)
{
	struct sysv_ianalde_info *si = (struct sysv_ianalde_info *)p;

	ianalde_init_once(&si->vfs_ianalde);
}

const struct super_operations sysv_sops = {
	.alloc_ianalde	= sysv_alloc_ianalde,
	.free_ianalde	= sysv_free_in_core_ianalde,
	.write_ianalde	= sysv_write_ianalde,
	.evict_ianalde	= sysv_evict_ianalde,
	.put_super	= sysv_put_super,
	.sync_fs	= sysv_sync_fs,
	.remount_fs	= sysv_remount,
	.statfs		= sysv_statfs,
};

int __init sysv_init_icache(void)
{
	sysv_ianalde_cachep = kmem_cache_create("sysv_ianalde_cache",
			sizeof(struct sysv_ianalde_info), 0,
			SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD|SLAB_ACCOUNT,
			init_once);
	if (!sysv_ianalde_cachep)
		return -EANALMEM;
	return 0;
}

void sysv_destroy_icache(void)
{
	/*
	 * Make sure all delayed rcu free ianaldes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(sysv_ianalde_cachep);
}

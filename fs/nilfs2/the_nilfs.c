/*
 * the_nilfs.c - the_nilfs shared structure.
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Ryusuke Konishi <ryusuke@osrg.net>
 *
 */

#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include "nilfs.h"
#include "segment.h"
#include "alloc.h"
#include "cpfile.h"
#include "sufile.h"
#include "dat.h"
#include "seglist.h"
#include "segbuf.h"

void nilfs_set_last_segment(struct the_nilfs *nilfs,
			    sector_t start_blocknr, u64 seq, __u64 cno)
{
	spin_lock(&nilfs->ns_last_segment_lock);
	nilfs->ns_last_pseg = start_blocknr;
	nilfs->ns_last_seq = seq;
	nilfs->ns_last_cno = cno;
	spin_unlock(&nilfs->ns_last_segment_lock);
}

/**
 * alloc_nilfs - allocate the_nilfs structure
 * @bdev: block device to which the_nilfs is related
 *
 * alloc_nilfs() allocates memory for the_nilfs and
 * initializes its reference count and locks.
 *
 * Return Value: On success, pointer to the_nilfs is returned.
 * On error, NULL is returned.
 */
struct the_nilfs *alloc_nilfs(struct block_device *bdev)
{
	struct the_nilfs *nilfs;

	nilfs = kzalloc(sizeof(*nilfs), GFP_KERNEL);
	if (!nilfs)
		return NULL;

	nilfs->ns_bdev = bdev;
	atomic_set(&nilfs->ns_count, 1);
	atomic_set(&nilfs->ns_writer_refcount, -1);
	atomic_set(&nilfs->ns_ndirtyblks, 0);
	init_rwsem(&nilfs->ns_sem);
	mutex_init(&nilfs->ns_writer_mutex);
	INIT_LIST_HEAD(&nilfs->ns_supers);
	spin_lock_init(&nilfs->ns_last_segment_lock);
	nilfs->ns_gc_inodes_h = NULL;
	init_rwsem(&nilfs->ns_segctor_sem);

	return nilfs;
}

/**
 * put_nilfs - release a reference to the_nilfs
 * @nilfs: the_nilfs structure to be released
 *
 * put_nilfs() decrements a reference counter of the_nilfs.
 * If the reference count reaches zero, the_nilfs is freed.
 */
void put_nilfs(struct the_nilfs *nilfs)
{
	if (!atomic_dec_and_test(&nilfs->ns_count))
		return;
	/*
	 * Increment of ns_count never occur below because the caller
	 * of get_nilfs() holds at least one reference to the_nilfs.
	 * Thus its exclusion control is not required here.
	 */
	might_sleep();
	if (nilfs_loaded(nilfs)) {
		nilfs_mdt_clear(nilfs->ns_sufile);
		nilfs_mdt_destroy(nilfs->ns_sufile);
		nilfs_mdt_clear(nilfs->ns_cpfile);
		nilfs_mdt_destroy(nilfs->ns_cpfile);
		nilfs_mdt_clear(nilfs->ns_dat);
		nilfs_mdt_destroy(nilfs->ns_dat);
		/* XXX: how and when to clear nilfs->ns_gc_dat? */
		nilfs_mdt_destroy(nilfs->ns_gc_dat);
	}
	if (nilfs_init(nilfs)) {
		nilfs_destroy_gccache(nilfs);
		brelse(nilfs->ns_sbh);
	}
	kfree(nilfs);
}

static int nilfs_load_super_root(struct the_nilfs *nilfs,
				 struct nilfs_sb_info *sbi, sector_t sr_block)
{
	struct buffer_head *bh_sr;
	struct nilfs_super_root *raw_sr;
	unsigned dat_entry_size, segment_usage_size, checkpoint_size;
	unsigned inode_size;
	int err;

	err = nilfs_read_super_root_block(sbi->s_super, sr_block, &bh_sr, 1);
	if (unlikely(err))
		return err;

	down_read(&nilfs->ns_sem);
	dat_entry_size = le16_to_cpu(nilfs->ns_sbp->s_dat_entry_size);
	checkpoint_size = le16_to_cpu(nilfs->ns_sbp->s_checkpoint_size);
	segment_usage_size = le16_to_cpu(nilfs->ns_sbp->s_segment_usage_size);
	up_read(&nilfs->ns_sem);

	inode_size = nilfs->ns_inode_size;

	err = -ENOMEM;
	nilfs->ns_dat = nilfs_mdt_new(
		nilfs, NULL, NILFS_DAT_INO, NILFS_DAT_GFP);
	if (unlikely(!nilfs->ns_dat))
		goto failed;

	nilfs->ns_gc_dat = nilfs_mdt_new(
		nilfs, NULL, NILFS_DAT_INO, NILFS_DAT_GFP);
	if (unlikely(!nilfs->ns_gc_dat))
		goto failed_dat;

	nilfs->ns_cpfile = nilfs_mdt_new(
		nilfs, NULL, NILFS_CPFILE_INO, NILFS_CPFILE_GFP);
	if (unlikely(!nilfs->ns_cpfile))
		goto failed_gc_dat;

	nilfs->ns_sufile = nilfs_mdt_new(
		nilfs, NULL, NILFS_SUFILE_INO, NILFS_SUFILE_GFP);
	if (unlikely(!nilfs->ns_sufile))
		goto failed_cpfile;

	err = nilfs_palloc_init_blockgroup(nilfs->ns_dat, dat_entry_size);
	if (unlikely(err))
		goto failed_sufile;

	err = nilfs_palloc_init_blockgroup(nilfs->ns_gc_dat, dat_entry_size);
	if (unlikely(err))
		goto failed_sufile;

	nilfs_mdt_set_shadow(nilfs->ns_dat, nilfs->ns_gc_dat);
	nilfs_mdt_set_entry_size(nilfs->ns_cpfile, checkpoint_size,
				 sizeof(struct nilfs_cpfile_header));
	nilfs_mdt_set_entry_size(nilfs->ns_sufile, segment_usage_size,
				 sizeof(struct nilfs_sufile_header));

	err = nilfs_mdt_read_inode_direct(
		nilfs->ns_dat, bh_sr, NILFS_SR_DAT_OFFSET(inode_size));
	if (unlikely(err))
		goto failed_sufile;

	err = nilfs_mdt_read_inode_direct(
		nilfs->ns_cpfile, bh_sr, NILFS_SR_CPFILE_OFFSET(inode_size));
	if (unlikely(err))
		goto failed_sufile;

	err = nilfs_mdt_read_inode_direct(
		nilfs->ns_sufile, bh_sr, NILFS_SR_SUFILE_OFFSET(inode_size));
	if (unlikely(err))
		goto failed_sufile;

	raw_sr = (struct nilfs_super_root *)bh_sr->b_data;
	nilfs->ns_nongc_ctime = le64_to_cpu(raw_sr->sr_nongc_ctime);

 failed:
	brelse(bh_sr);
	return err;

 failed_sufile:
	nilfs_mdt_destroy(nilfs->ns_sufile);

 failed_cpfile:
	nilfs_mdt_destroy(nilfs->ns_cpfile);

 failed_gc_dat:
	nilfs_mdt_destroy(nilfs->ns_gc_dat);

 failed_dat:
	nilfs_mdt_destroy(nilfs->ns_dat);
	goto failed;
}

static void nilfs_init_recovery_info(struct nilfs_recovery_info *ri)
{
	memset(ri, 0, sizeof(*ri));
	INIT_LIST_HEAD(&ri->ri_used_segments);
}

static void nilfs_clear_recovery_info(struct nilfs_recovery_info *ri)
{
	nilfs_dispose_segment_list(&ri->ri_used_segments);
}

/**
 * load_nilfs - load and recover the nilfs
 * @nilfs: the_nilfs structure to be released
 * @sbi: nilfs_sb_info used to recover past segment
 *
 * load_nilfs() searches and load the latest super root,
 * attaches the last segment, and does recovery if needed.
 * The caller must call this exclusively for simultaneous mounts.
 */
int load_nilfs(struct the_nilfs *nilfs, struct nilfs_sb_info *sbi)
{
	struct nilfs_recovery_info ri;
	unsigned int s_flags = sbi->s_super->s_flags;
	int really_read_only = bdev_read_only(nilfs->ns_bdev);
	unsigned valid_fs;
	int err = 0;

	nilfs_init_recovery_info(&ri);

	down_write(&nilfs->ns_sem);
	valid_fs = (nilfs->ns_mount_state & NILFS_VALID_FS);
	up_write(&nilfs->ns_sem);

	if (!valid_fs && (s_flags & MS_RDONLY)) {
		printk(KERN_INFO "NILFS: INFO: recovery "
		       "required for readonly filesystem.\n");
		if (really_read_only) {
			printk(KERN_ERR "NILFS: write access "
			       "unavailable, cannot proceed.\n");
			err = -EROFS;
			goto failed;
		}
		printk(KERN_INFO "NILFS: write access will "
		       "be enabled during recovery.\n");
		sbi->s_super->s_flags &= ~MS_RDONLY;
	}

	err = nilfs_search_super_root(nilfs, sbi, &ri);
	if (unlikely(err)) {
		printk(KERN_ERR "NILFS: error searching super root.\n");
		goto failed;
	}

	err = nilfs_load_super_root(nilfs, sbi, ri.ri_super_root);
	if (unlikely(err)) {
		printk(KERN_ERR "NILFS: error loading super root.\n");
		goto failed;
	}

	if (!valid_fs) {
		err = nilfs_recover_logical_segments(nilfs, sbi, &ri);
		if (unlikely(err)) {
			nilfs_mdt_destroy(nilfs->ns_cpfile);
			nilfs_mdt_destroy(nilfs->ns_sufile);
			nilfs_mdt_destroy(nilfs->ns_dat);
			goto failed;
		}
		if (ri.ri_need_recovery == NILFS_RECOVERY_SR_UPDATED) {
			down_write(&nilfs->ns_sem);
			nilfs_update_last_segment(sbi, 0);
			up_write(&nilfs->ns_sem);
		}
	}

	set_nilfs_loaded(nilfs);

 failed:
	nilfs_clear_recovery_info(&ri);
	sbi->s_super->s_flags = s_flags;
	return err;
}

static unsigned long long nilfs_max_size(unsigned int blkbits)
{
	unsigned int max_bits;
	unsigned long long res = MAX_LFS_FILESIZE; /* page cache limit */

	max_bits = blkbits + NILFS_BMAP_KEY_BIT; /* bmap size limit */
	if (max_bits < 64)
		res = min_t(unsigned long long, res, (1ULL << max_bits) - 1);
	return res;
}

static int
nilfs_store_disk_layout(struct the_nilfs *nilfs, struct super_block *sb,
			struct nilfs_super_block *sbp)
{
	if (le32_to_cpu(sbp->s_rev_level) != NILFS_CURRENT_REV) {
		printk(KERN_ERR "NILFS: revision mismatch "
		       "(superblock rev.=%d.%d, current rev.=%d.%d). "
		       "Please check the version of mkfs.nilfs.\n",
		       le32_to_cpu(sbp->s_rev_level),
		       le16_to_cpu(sbp->s_minor_rev_level),
		       NILFS_CURRENT_REV, NILFS_MINOR_REV);
		return -EINVAL;
	}
	nilfs->ns_inode_size = le16_to_cpu(sbp->s_inode_size);
	nilfs->ns_first_ino = le32_to_cpu(sbp->s_first_ino);

	nilfs->ns_blocks_per_segment = le32_to_cpu(sbp->s_blocks_per_segment);
	if (nilfs->ns_blocks_per_segment < NILFS_SEG_MIN_BLOCKS) {
		printk(KERN_ERR "NILFS: too short segment. \n");
		return -EINVAL;
	}

	nilfs->ns_first_data_block = le64_to_cpu(sbp->s_first_data_block);
	nilfs->ns_nsegments = le64_to_cpu(sbp->s_nsegments);
	nilfs->ns_r_segments_percentage =
		le32_to_cpu(sbp->s_r_segments_percentage);
	nilfs->ns_nrsvsegs =
		max_t(unsigned long, NILFS_MIN_NRSVSEGS,
		      DIV_ROUND_UP(nilfs->ns_nsegments *
				   nilfs->ns_r_segments_percentage, 100));
	nilfs->ns_crc_seed = le32_to_cpu(sbp->s_crc_seed);
	return 0;
}

/**
 * init_nilfs - initialize a NILFS instance.
 * @nilfs: the_nilfs structure
 * @sbi: nilfs_sb_info
 * @sb: super block
 * @data: mount options
 *
 * init_nilfs() performs common initialization per block device (e.g.
 * reading the super block, getting disk layout information, initializing
 * shared fields in the_nilfs). It takes on some portion of the jobs
 * typically done by a fill_super() routine. This division arises from
 * the nature that multiple NILFS instances may be simultaneously
 * mounted on a device.
 * For multiple mounts on the same device, only the first mount
 * invokes these tasks.
 *
 * Return Value: On success, 0 is returned. On error, a negative error
 * code is returned.
 */
int init_nilfs(struct the_nilfs *nilfs, struct nilfs_sb_info *sbi, char *data)
{
	struct super_block *sb = sbi->s_super;
	struct buffer_head *sbh;
	struct nilfs_super_block *sbp;
	struct backing_dev_info *bdi;
	int blocksize;
	int err = 0;

	down_write(&nilfs->ns_sem);
	if (nilfs_init(nilfs)) {
		/* Load values from existing the_nilfs */
		sbp = nilfs->ns_sbp;
		err = nilfs_store_magic_and_option(sb, sbp, data);
		if (err)
			goto out;

		blocksize = BLOCK_SIZE << le32_to_cpu(sbp->s_log_block_size);
		if (sb->s_blocksize != blocksize &&
		    !sb_set_blocksize(sb, blocksize)) {
			printk(KERN_ERR "NILFS: blocksize %d unfit to device\n",
			       blocksize);
			err = -EINVAL;
		}
		sb->s_maxbytes = nilfs_max_size(sb->s_blocksize_bits);
		goto out;
	}

	sbp = nilfs_load_super_block(sb, &sbh);
	if (!sbp) {
		err = -EINVAL;
		goto out;
	}
	err = nilfs_store_magic_and_option(sb, sbp, data);
	if (err)
		goto failed_sbh;

	blocksize = BLOCK_SIZE << le32_to_cpu(sbp->s_log_block_size);
	if (sb->s_blocksize != blocksize) {
		sbp = nilfs_reload_super_block(sb, &sbh, blocksize);
		if (!sbp) {
			err = -EINVAL;
			goto out;
			/* not failed_sbh; sbh is released automatically
			   when reloading fails. */
		}
	}
	nilfs->ns_blocksize_bits = sb->s_blocksize_bits;

	err = nilfs_store_disk_layout(nilfs, sb, sbp);
	if (err)
		goto failed_sbh;

	sb->s_maxbytes = nilfs_max_size(sb->s_blocksize_bits);

	nilfs->ns_mount_state = le16_to_cpu(sbp->s_state);
	nilfs->ns_sbh = sbh;
	nilfs->ns_sbp = sbp;

	bdi = nilfs->ns_bdev->bd_inode_backing_dev_info;
	if (!bdi)
		bdi = nilfs->ns_bdev->bd_inode->i_mapping->backing_dev_info;
	nilfs->ns_bdi = bdi ? : &default_backing_dev_info;

	/* Finding last segment */
	nilfs->ns_last_pseg = le64_to_cpu(sbp->s_last_pseg);
	nilfs->ns_last_cno = le64_to_cpu(sbp->s_last_cno);
	nilfs->ns_last_seq = le64_to_cpu(sbp->s_last_seq);

	nilfs->ns_seg_seq = nilfs->ns_last_seq;
	nilfs->ns_segnum =
		nilfs_get_segnum_of_block(nilfs, nilfs->ns_last_pseg);
	nilfs->ns_cno = nilfs->ns_last_cno + 1;
	if (nilfs->ns_segnum >= nilfs->ns_nsegments) {
		printk(KERN_ERR "NILFS invalid last segment number.\n");
		err = -EINVAL;
		goto failed_sbh;
	}
	/* Dummy values  */
	nilfs->ns_free_segments_count =
		nilfs->ns_nsegments - (nilfs->ns_segnum + 1);

	/* Initialize gcinode cache */
	err = nilfs_init_gccache(nilfs);
	if (err)
		goto failed_sbh;

	set_nilfs_init(nilfs);
	err = 0;
 out:
	up_write(&nilfs->ns_sem);
	return err;

 failed_sbh:
	brelse(sbh);
	goto out;
}

int nilfs_count_free_blocks(struct the_nilfs *nilfs, sector_t *nblocks)
{
	struct inode *dat = nilfs_dat_inode(nilfs);
	unsigned long ncleansegs;
	int err;

	down_read(&NILFS_MDT(dat)->mi_sem);	/* XXX */
	err = nilfs_sufile_get_ncleansegs(nilfs->ns_sufile, &ncleansegs);
	up_read(&NILFS_MDT(dat)->mi_sem);	/* XXX */
	if (likely(!err))
		*nblocks = (sector_t)ncleansegs * nilfs->ns_blocks_per_segment;
	return err;
}

int nilfs_near_disk_full(struct the_nilfs *nilfs)
{
	struct inode *sufile = nilfs->ns_sufile;
	unsigned long ncleansegs, nincsegs;
	int ret;

	ret = nilfs_sufile_get_ncleansegs(sufile, &ncleansegs);
	if (likely(!ret)) {
		nincsegs = atomic_read(&nilfs->ns_ndirtyblks) /
			nilfs->ns_blocks_per_segment + 1;
		if (ncleansegs <= nilfs->ns_nrsvsegs + nincsegs)
			ret++;
	}
	return ret;
}

int nilfs_checkpoint_is_mounted(struct the_nilfs *nilfs, __u64 cno,
				int snapshot_mount)
{
	struct nilfs_sb_info *sbi;
	int ret = 0;

	down_read(&nilfs->ns_sem);
	if (cno == 0 || cno > nilfs->ns_cno)
		goto out_unlock;

	list_for_each_entry(sbi, &nilfs->ns_supers, s_list) {
		if (sbi->s_snapshot_cno == cno &&
		    (!snapshot_mount || nilfs_test_opt(sbi, SNAPSHOT))) {
					/* exclude read-only mounts */
			ret++;
			break;
		}
	}
	/* for protecting recent checkpoints */
	if (cno >= nilfs_last_cno(nilfs))
		ret++;

 out_unlock:
	up_read(&nilfs->ns_sem);
	return ret;
}

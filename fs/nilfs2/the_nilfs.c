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
#include <linux/crc32.h>
#include "nilfs.h"
#include "segment.h"
#include "alloc.h"
#include "cpfile.h"
#include "sufile.h"
#include "dat.h"
#include "segbuf.h"


static LIST_HEAD(nilfs_objects);
static DEFINE_SPINLOCK(nilfs_lock);

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
static struct the_nilfs *alloc_nilfs(struct block_device *bdev)
{
	struct the_nilfs *nilfs;

	nilfs = kzalloc(sizeof(*nilfs), GFP_KERNEL);
	if (!nilfs)
		return NULL;

	nilfs->ns_bdev = bdev;
	atomic_set(&nilfs->ns_count, 1);
	atomic_set(&nilfs->ns_ndirtyblks, 0);
	init_rwsem(&nilfs->ns_sem);
	init_rwsem(&nilfs->ns_super_sem);
	mutex_init(&nilfs->ns_mount_mutex);
	init_rwsem(&nilfs->ns_writer_sem);
	INIT_LIST_HEAD(&nilfs->ns_list);
	INIT_LIST_HEAD(&nilfs->ns_supers);
	spin_lock_init(&nilfs->ns_last_segment_lock);
	nilfs->ns_gc_inodes_h = NULL;
	init_rwsem(&nilfs->ns_segctor_sem);

	return nilfs;
}

/**
 * find_or_create_nilfs - find or create nilfs object
 * @bdev: block device to which the_nilfs is related
 *
 * find_nilfs() looks up an existent nilfs object created on the
 * device and gets the reference count of the object.  If no nilfs object
 * is found on the device, a new nilfs object is allocated.
 *
 * Return Value: On success, pointer to the nilfs object is returned.
 * On error, NULL is returned.
 */
struct the_nilfs *find_or_create_nilfs(struct block_device *bdev)
{
	struct the_nilfs *nilfs, *new = NULL;

 retry:
	spin_lock(&nilfs_lock);
	list_for_each_entry(nilfs, &nilfs_objects, ns_list) {
		if (nilfs->ns_bdev == bdev) {
			get_nilfs(nilfs);
			spin_unlock(&nilfs_lock);
			if (new)
				put_nilfs(new);
			return nilfs; /* existing object */
		}
	}
	if (new) {
		list_add_tail(&new->ns_list, &nilfs_objects);
		spin_unlock(&nilfs_lock);
		return new; /* new object */
	}
	spin_unlock(&nilfs_lock);

	new = alloc_nilfs(bdev);
	if (new)
		goto retry;
	return NULL; /* insufficient memory */
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
	spin_lock(&nilfs_lock);
	if (!atomic_dec_and_test(&nilfs->ns_count)) {
		spin_unlock(&nilfs_lock);
		return;
	}
	list_del_init(&nilfs->ns_list);
	spin_unlock(&nilfs_lock);

	/*
	 * Increment of ns_count never occurs below because the caller
	 * of get_nilfs() holds at least one reference to the_nilfs.
	 * Thus its exclusion control is not required here.
	 */

	might_sleep();
	if (nilfs_loaded(nilfs)) {
		nilfs_mdt_destroy(nilfs->ns_sufile);
		nilfs_mdt_destroy(nilfs->ns_cpfile);
		nilfs_mdt_destroy(nilfs->ns_dat);
		nilfs_mdt_destroy(nilfs->ns_gc_dat);
	}
	if (nilfs_init(nilfs)) {
		nilfs_destroy_gccache(nilfs);
		brelse(nilfs->ns_sbh[0]);
		brelse(nilfs->ns_sbh[1]);
	}
	kfree(nilfs);
}

static int nilfs_load_super_root(struct the_nilfs *nilfs,
				 struct nilfs_sb_info *sbi, sector_t sr_block)
{
	struct buffer_head *bh_sr;
	struct nilfs_super_root *raw_sr;
	struct nilfs_super_block **sbp = nilfs->ns_sbp;
	unsigned dat_entry_size, segment_usage_size, checkpoint_size;
	unsigned inode_size;
	int err;

	err = nilfs_read_super_root_block(sbi->s_super, sr_block, &bh_sr, 1);
	if (unlikely(err))
		return err;

	down_read(&nilfs->ns_sem);
	dat_entry_size = le16_to_cpu(sbp[0]->s_dat_entry_size);
	checkpoint_size = le16_to_cpu(sbp[0]->s_checkpoint_size);
	segment_usage_size = le16_to_cpu(sbp[0]->s_segment_usage_size);
	up_read(&nilfs->ns_sem);

	inode_size = nilfs->ns_inode_size;

	err = -ENOMEM;
	nilfs->ns_dat = nilfs_dat_new(nilfs, dat_entry_size);
	if (unlikely(!nilfs->ns_dat))
		goto failed;

	nilfs->ns_gc_dat = nilfs_dat_new(nilfs, dat_entry_size);
	if (unlikely(!nilfs->ns_gc_dat))
		goto failed_dat;

	nilfs->ns_cpfile = nilfs_cpfile_new(nilfs, checkpoint_size);
	if (unlikely(!nilfs->ns_cpfile))
		goto failed_gc_dat;

	nilfs->ns_sufile = nilfs_sufile_new(nilfs, segment_usage_size);
	if (unlikely(!nilfs->ns_sufile))
		goto failed_cpfile;

	nilfs_mdt_set_shadow(nilfs->ns_dat, nilfs->ns_gc_dat);

	err = nilfs_dat_read(nilfs->ns_dat, (void *)bh_sr->b_data +
			     NILFS_SR_DAT_OFFSET(inode_size));
	if (unlikely(err))
		goto failed_sufile;

	err = nilfs_cpfile_read(nilfs->ns_cpfile, (void *)bh_sr->b_data +
				NILFS_SR_CPFILE_OFFSET(inode_size));
	if (unlikely(err))
		goto failed_sufile;

	err = nilfs_sufile_read(nilfs->ns_sufile, (void *)bh_sr->b_data +
				NILFS_SR_SUFILE_OFFSET(inode_size));
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
	int valid_fs = nilfs_valid_fs(nilfs);
	int err;

	if (nilfs_loaded(nilfs)) {
		if (valid_fs ||
		    ((s_flags & MS_RDONLY) && nilfs_test_opt(sbi, NORECOVERY)))
			return 0;
		printk(KERN_ERR "NILFS: the filesystem is in an incomplete "
		       "recovery state.\n");
		return -EINVAL;
	}

	if (!valid_fs) {
		printk(KERN_WARNING "NILFS warning: mounting unchecked fs\n");
		if (s_flags & MS_RDONLY) {
			printk(KERN_INFO "NILFS: INFO: recovery "
			       "required for readonly filesystem.\n");
			printk(KERN_INFO "NILFS: write access will "
			       "be enabled during recovery.\n");
		}
	}

	nilfs_init_recovery_info(&ri);

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

	if (valid_fs)
		goto skip_recovery;

	if (s_flags & MS_RDONLY) {
		if (nilfs_test_opt(sbi, NORECOVERY)) {
			printk(KERN_INFO "NILFS: norecovery option specified. "
			       "skipping roll-forward recovery\n");
			goto skip_recovery;
		}
		if (really_read_only) {
			printk(KERN_ERR "NILFS: write access "
			       "unavailable, cannot proceed.\n");
			err = -EROFS;
			goto failed_unload;
		}
		sbi->s_super->s_flags &= ~MS_RDONLY;
	} else if (nilfs_test_opt(sbi, NORECOVERY)) {
		printk(KERN_ERR "NILFS: recovery cancelled because norecovery "
		       "option was specified for a read/write mount\n");
		err = -EINVAL;
		goto failed_unload;
	}

	err = nilfs_recover_logical_segments(nilfs, sbi, &ri);
	if (err)
		goto failed_unload;

	down_write(&nilfs->ns_sem);
	nilfs->ns_mount_state |= NILFS_VALID_FS;
	nilfs->ns_sbp[0]->s_state = cpu_to_le16(nilfs->ns_mount_state);
	err = nilfs_commit_super(sbi, 1);
	up_write(&nilfs->ns_sem);

	if (err) {
		printk(KERN_ERR "NILFS: failed to update super block. "
		       "recovery unfinished.\n");
		goto failed_unload;
	}
	printk(KERN_INFO "NILFS: recovery complete.\n");

 skip_recovery:
	set_nilfs_loaded(nilfs);
	nilfs_clear_recovery_info(&ri);
	sbi->s_super->s_flags = s_flags;
	return 0;

 failed_unload:
	nilfs_mdt_destroy(nilfs->ns_cpfile);
	nilfs_mdt_destroy(nilfs->ns_sufile);
	nilfs_mdt_destroy(nilfs->ns_dat);

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

static int nilfs_store_disk_layout(struct the_nilfs *nilfs,
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
	nilfs->ns_sbsize = le16_to_cpu(sbp->s_bytes);
	if (nilfs->ns_sbsize > BLOCK_SIZE)
		return -EINVAL;

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

static int nilfs_valid_sb(struct nilfs_super_block *sbp)
{
	static unsigned char sum[4];
	const int sumoff = offsetof(struct nilfs_super_block, s_sum);
	size_t bytes;
	u32 crc;

	if (!sbp || le16_to_cpu(sbp->s_magic) != NILFS_SUPER_MAGIC)
		return 0;
	bytes = le16_to_cpu(sbp->s_bytes);
	if (bytes > BLOCK_SIZE)
		return 0;
	crc = crc32_le(le32_to_cpu(sbp->s_crc_seed), (unsigned char *)sbp,
		       sumoff);
	crc = crc32_le(crc, sum, 4);
	crc = crc32_le(crc, (unsigned char *)sbp + sumoff + 4,
		       bytes - sumoff - 4);
	return crc == le32_to_cpu(sbp->s_sum);
}

static int nilfs_sb2_bad_offset(struct nilfs_super_block *sbp, u64 offset)
{
	return offset < ((le64_to_cpu(sbp->s_nsegments) *
			  le32_to_cpu(sbp->s_blocks_per_segment)) <<
			 (le32_to_cpu(sbp->s_log_block_size) + 10));
}

static void nilfs_release_super_block(struct the_nilfs *nilfs)
{
	int i;

	for (i = 0; i < 2; i++) {
		if (nilfs->ns_sbp[i]) {
			brelse(nilfs->ns_sbh[i]);
			nilfs->ns_sbh[i] = NULL;
			nilfs->ns_sbp[i] = NULL;
		}
	}
}

void nilfs_fall_back_super_block(struct the_nilfs *nilfs)
{
	brelse(nilfs->ns_sbh[0]);
	nilfs->ns_sbh[0] = nilfs->ns_sbh[1];
	nilfs->ns_sbp[0] = nilfs->ns_sbp[1];
	nilfs->ns_sbh[1] = NULL;
	nilfs->ns_sbp[1] = NULL;
}

void nilfs_swap_super_block(struct the_nilfs *nilfs)
{
	struct buffer_head *tsbh = nilfs->ns_sbh[0];
	struct nilfs_super_block *tsbp = nilfs->ns_sbp[0];

	nilfs->ns_sbh[0] = nilfs->ns_sbh[1];
	nilfs->ns_sbp[0] = nilfs->ns_sbp[1];
	nilfs->ns_sbh[1] = tsbh;
	nilfs->ns_sbp[1] = tsbp;
}

static int nilfs_load_super_block(struct the_nilfs *nilfs,
				  struct super_block *sb, int blocksize,
				  struct nilfs_super_block **sbpp)
{
	struct nilfs_super_block **sbp = nilfs->ns_sbp;
	struct buffer_head **sbh = nilfs->ns_sbh;
	u64 sb2off = NILFS_SB2_OFFSET_BYTES(nilfs->ns_bdev->bd_inode->i_size);
	int valid[2], swp = 0;

	sbp[0] = nilfs_read_super_block(sb, NILFS_SB_OFFSET_BYTES, blocksize,
					&sbh[0]);
	sbp[1] = nilfs_read_super_block(sb, sb2off, blocksize, &sbh[1]);

	if (!sbp[0]) {
		if (!sbp[1]) {
			printk(KERN_ERR "NILFS: unable to read superblock\n");
			return -EIO;
		}
		printk(KERN_WARNING
		       "NILFS warning: unable to read primary superblock\n");
	} else if (!sbp[1])
		printk(KERN_WARNING
		       "NILFS warning: unable to read secondary superblock\n");

	valid[0] = nilfs_valid_sb(sbp[0]);
	valid[1] = nilfs_valid_sb(sbp[1]);
	swp = valid[1] &&
		(!valid[0] ||
		 le64_to_cpu(sbp[1]->s_wtime) > le64_to_cpu(sbp[0]->s_wtime));

	if (valid[swp] && nilfs_sb2_bad_offset(sbp[swp], sb2off)) {
		brelse(sbh[1]);
		sbh[1] = NULL;
		sbp[1] = NULL;
		swp = 0;
	}
	if (!valid[swp]) {
		nilfs_release_super_block(nilfs);
		printk(KERN_ERR "NILFS: Can't find nilfs on dev %s.\n",
		       sb->s_id);
		return -EINVAL;
	}

	if (swp) {
		printk(KERN_WARNING "NILFS warning: broken superblock. "
		       "using spare superblock.\n");
		nilfs_swap_super_block(nilfs);
	}

	nilfs->ns_sbwtime[0] = le64_to_cpu(sbp[0]->s_wtime);
	nilfs->ns_sbwtime[1] = valid[!swp] ? le64_to_cpu(sbp[1]->s_wtime) : 0;
	nilfs->ns_prot_seq = le64_to_cpu(sbp[valid[1] & !swp]->s_last_seq);
	*sbpp = sbp[0];
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
	struct nilfs_super_block *sbp;
	struct backing_dev_info *bdi;
	int blocksize;
	int err;

	down_write(&nilfs->ns_sem);
	if (nilfs_init(nilfs)) {
		/* Load values from existing the_nilfs */
		sbp = nilfs->ns_sbp[0];
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

	blocksize = sb_min_blocksize(sb, BLOCK_SIZE);
	if (!blocksize) {
		printk(KERN_ERR "NILFS: unable to set blocksize\n");
		err = -EINVAL;
		goto out;
	}
	err = nilfs_load_super_block(nilfs, sb, blocksize, &sbp);
	if (err)
		goto out;

	err = nilfs_store_magic_and_option(sb, sbp, data);
	if (err)
		goto failed_sbh;

	blocksize = BLOCK_SIZE << le32_to_cpu(sbp->s_log_block_size);
	if (sb->s_blocksize != blocksize) {
		int hw_blocksize = bdev_logical_block_size(sb->s_bdev);

		if (blocksize < hw_blocksize) {
			printk(KERN_ERR
			       "NILFS: blocksize %d too small for device "
			       "(sector-size = %d).\n",
			       blocksize, hw_blocksize);
			err = -EINVAL;
			goto failed_sbh;
		}
		nilfs_release_super_block(nilfs);
		sb_set_blocksize(sb, blocksize);

		err = nilfs_load_super_block(nilfs, sb, blocksize, &sbp);
		if (err)
			goto out;
			/* not failed_sbh; sbh is released automatically
			   when reloading fails. */
	}
	nilfs->ns_blocksize_bits = sb->s_blocksize_bits;

	err = nilfs_store_disk_layout(nilfs, sbp);
	if (err)
		goto failed_sbh;

	sb->s_maxbytes = nilfs_max_size(sb->s_blocksize_bits);

	nilfs->ns_mount_state = le16_to_cpu(sbp->s_state);

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
	nilfs_release_super_block(nilfs);
	goto out;
}

int nilfs_count_free_blocks(struct the_nilfs *nilfs, sector_t *nblocks)
{
	struct inode *dat = nilfs_dat_inode(nilfs);
	unsigned long ncleansegs;

	down_read(&NILFS_MDT(dat)->mi_sem);	/* XXX */
	ncleansegs = nilfs_sufile_get_ncleansegs(nilfs->ns_sufile);
	up_read(&NILFS_MDT(dat)->mi_sem);	/* XXX */
	*nblocks = (sector_t)ncleansegs * nilfs->ns_blocks_per_segment;
	return 0;
}

int nilfs_near_disk_full(struct the_nilfs *nilfs)
{
	unsigned long ncleansegs, nincsegs;

	ncleansegs = nilfs_sufile_get_ncleansegs(nilfs->ns_sufile);
	nincsegs = atomic_read(&nilfs->ns_ndirtyblks) /
		nilfs->ns_blocks_per_segment + 1;

	return ncleansegs <= nilfs->ns_nrsvsegs + nincsegs;
}

/**
 * nilfs_find_sbinfo - find existing nilfs_sb_info structure
 * @nilfs: nilfs object
 * @rw_mount: mount type (non-zero value for read/write mount)
 * @cno: checkpoint number (zero for read-only mount)
 *
 * nilfs_find_sbinfo() returns the nilfs_sb_info structure which
 * @rw_mount and @cno (in case of snapshots) matched.  If no instance
 * was found, NULL is returned.  Although the super block instance can
 * be unmounted after this function returns, the nilfs_sb_info struct
 * is kept on memory until nilfs_put_sbinfo() is called.
 */
struct nilfs_sb_info *nilfs_find_sbinfo(struct the_nilfs *nilfs,
					int rw_mount, __u64 cno)
{
	struct nilfs_sb_info *sbi;

	down_read(&nilfs->ns_super_sem);
	/*
	 * The SNAPSHOT flag and sb->s_flags are supposed to be
	 * protected with nilfs->ns_super_sem.
	 */
	sbi = nilfs->ns_current;
	if (rw_mount) {
		if (sbi && !(sbi->s_super->s_flags & MS_RDONLY))
			goto found; /* read/write mount */
		else
			goto out;
	} else if (cno == 0) {
		if (sbi && (sbi->s_super->s_flags & MS_RDONLY))
			goto found; /* read-only mount */
		else
			goto out;
	}

	list_for_each_entry(sbi, &nilfs->ns_supers, s_list) {
		if (nilfs_test_opt(sbi, SNAPSHOT) &&
		    sbi->s_snapshot_cno == cno)
			goto found; /* snapshot mount */
	}
 out:
	up_read(&nilfs->ns_super_sem);
	return NULL;

 found:
	atomic_inc(&sbi->s_count);
	up_read(&nilfs->ns_super_sem);
	return sbi;
}

int nilfs_checkpoint_is_mounted(struct the_nilfs *nilfs, __u64 cno,
				int snapshot_mount)
{
	struct nilfs_sb_info *sbi;
	int ret = 0;

	down_read(&nilfs->ns_super_sem);
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
	up_read(&nilfs->ns_super_sem);
	return ret;
}

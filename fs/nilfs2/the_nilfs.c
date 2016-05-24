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
 * Written by Ryusuke Konishi.
 *
 */

#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/random.h>
#include <linux/crc32.h>
#include "nilfs.h"
#include "segment.h"
#include "alloc.h"
#include "cpfile.h"
#include "sufile.h"
#include "dat.h"
#include "segbuf.h"


static int nilfs_valid_sb(struct nilfs_super_block *sbp);

void nilfs_set_last_segment(struct the_nilfs *nilfs,
			    sector_t start_blocknr, u64 seq, __u64 cno)
{
	spin_lock(&nilfs->ns_last_segment_lock);
	nilfs->ns_last_pseg = start_blocknr;
	nilfs->ns_last_seq = seq;
	nilfs->ns_last_cno = cno;

	if (!nilfs_sb_dirty(nilfs)) {
		if (nilfs->ns_prev_seq == nilfs->ns_last_seq)
			goto stay_cursor;

		set_nilfs_sb_dirty(nilfs);
	}
	nilfs->ns_prev_seq = nilfs->ns_last_seq;

 stay_cursor:
	spin_unlock(&nilfs->ns_last_segment_lock);
}

/**
 * alloc_nilfs - allocate a nilfs object
 * @bdev: block device to which the_nilfs is related
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
	atomic_set(&nilfs->ns_ndirtyblks, 0);
	init_rwsem(&nilfs->ns_sem);
	mutex_init(&nilfs->ns_snapshot_mount_mutex);
	INIT_LIST_HEAD(&nilfs->ns_dirty_files);
	INIT_LIST_HEAD(&nilfs->ns_gc_inodes);
	spin_lock_init(&nilfs->ns_inode_lock);
	spin_lock_init(&nilfs->ns_next_gen_lock);
	spin_lock_init(&nilfs->ns_last_segment_lock);
	nilfs->ns_cptree = RB_ROOT;
	spin_lock_init(&nilfs->ns_cptree_lock);
	init_rwsem(&nilfs->ns_segctor_sem);
	nilfs->ns_sb_update_freq = NILFS_SB_FREQ;

	return nilfs;
}

/**
 * destroy_nilfs - destroy nilfs object
 * @nilfs: nilfs object to be released
 */
void destroy_nilfs(struct the_nilfs *nilfs)
{
	might_sleep();
	if (nilfs_init(nilfs)) {
		nilfs_sysfs_delete_device_group(nilfs);
		brelse(nilfs->ns_sbh[0]);
		brelse(nilfs->ns_sbh[1]);
	}
	kfree(nilfs);
}

static int nilfs_load_super_root(struct the_nilfs *nilfs,
				 struct super_block *sb, sector_t sr_block)
{
	struct buffer_head *bh_sr;
	struct nilfs_super_root *raw_sr;
	struct nilfs_super_block **sbp = nilfs->ns_sbp;
	struct nilfs_inode *rawi;
	unsigned int dat_entry_size, segment_usage_size, checkpoint_size;
	unsigned int inode_size;
	int err;

	err = nilfs_read_super_root_block(nilfs, sr_block, &bh_sr, 1);
	if (unlikely(err))
		return err;

	down_read(&nilfs->ns_sem);
	dat_entry_size = le16_to_cpu(sbp[0]->s_dat_entry_size);
	checkpoint_size = le16_to_cpu(sbp[0]->s_checkpoint_size);
	segment_usage_size = le16_to_cpu(sbp[0]->s_segment_usage_size);
	up_read(&nilfs->ns_sem);

	inode_size = nilfs->ns_inode_size;

	rawi = (void *)bh_sr->b_data + NILFS_SR_DAT_OFFSET(inode_size);
	err = nilfs_dat_read(sb, dat_entry_size, rawi, &nilfs->ns_dat);
	if (err)
		goto failed;

	rawi = (void *)bh_sr->b_data + NILFS_SR_CPFILE_OFFSET(inode_size);
	err = nilfs_cpfile_read(sb, checkpoint_size, rawi, &nilfs->ns_cpfile);
	if (err)
		goto failed_dat;

	rawi = (void *)bh_sr->b_data + NILFS_SR_SUFILE_OFFSET(inode_size);
	err = nilfs_sufile_read(sb, segment_usage_size, rawi,
				&nilfs->ns_sufile);
	if (err)
		goto failed_cpfile;

	raw_sr = (struct nilfs_super_root *)bh_sr->b_data;
	nilfs->ns_nongc_ctime = le64_to_cpu(raw_sr->sr_nongc_ctime);

 failed:
	brelse(bh_sr);
	return err;

 failed_cpfile:
	iput(nilfs->ns_cpfile);

 failed_dat:
	iput(nilfs->ns_dat);
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
 * nilfs_store_log_cursor - load log cursor from a super block
 * @nilfs: nilfs object
 * @sbp: buffer storing super block to be read
 *
 * nilfs_store_log_cursor() reads the last position of the log
 * containing a super root from a given super block, and initializes
 * relevant information on the nilfs object preparatory for log
 * scanning and recovery.
 */
static int nilfs_store_log_cursor(struct the_nilfs *nilfs,
				  struct nilfs_super_block *sbp)
{
	int ret = 0;

	nilfs->ns_last_pseg = le64_to_cpu(sbp->s_last_pseg);
	nilfs->ns_last_cno = le64_to_cpu(sbp->s_last_cno);
	nilfs->ns_last_seq = le64_to_cpu(sbp->s_last_seq);

	nilfs->ns_prev_seq = nilfs->ns_last_seq;
	nilfs->ns_seg_seq = nilfs->ns_last_seq;
	nilfs->ns_segnum =
		nilfs_get_segnum_of_block(nilfs, nilfs->ns_last_pseg);
	nilfs->ns_cno = nilfs->ns_last_cno + 1;
	if (nilfs->ns_segnum >= nilfs->ns_nsegments) {
		printk(KERN_ERR "NILFS invalid last segment number.\n");
		ret = -EINVAL;
	}
	return ret;
}

/**
 * load_nilfs - load and recover the nilfs
 * @nilfs: the_nilfs structure to be released
 * @sb: super block isntance used to recover past segment
 *
 * load_nilfs() searches and load the latest super root,
 * attaches the last segment, and does recovery if needed.
 * The caller must call this exclusively for simultaneous mounts.
 */
int load_nilfs(struct the_nilfs *nilfs, struct super_block *sb)
{
	struct nilfs_recovery_info ri;
	unsigned int s_flags = sb->s_flags;
	int really_read_only = bdev_read_only(nilfs->ns_bdev);
	int valid_fs = nilfs_valid_fs(nilfs);
	int err;

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

	err = nilfs_search_super_root(nilfs, &ri);
	if (unlikely(err)) {
		struct nilfs_super_block **sbp = nilfs->ns_sbp;
		int blocksize;

		if (err != -EINVAL)
			goto scan_error;

		if (!nilfs_valid_sb(sbp[1])) {
			printk(KERN_WARNING
			       "NILFS warning: unable to fall back to spare"
			       "super block\n");
			goto scan_error;
		}
		printk(KERN_INFO
		       "NILFS: try rollback from an earlier position\n");

		/*
		 * restore super block with its spare and reconfigure
		 * relevant states of the nilfs object.
		 */
		memcpy(sbp[0], sbp[1], nilfs->ns_sbsize);
		nilfs->ns_crc_seed = le32_to_cpu(sbp[0]->s_crc_seed);
		nilfs->ns_sbwtime = le64_to_cpu(sbp[0]->s_wtime);

		/* verify consistency between two super blocks */
		blocksize = BLOCK_SIZE << le32_to_cpu(sbp[0]->s_log_block_size);
		if (blocksize != nilfs->ns_blocksize) {
			printk(KERN_WARNING
			       "NILFS warning: blocksize differs between "
			       "two super blocks (%d != %d)\n",
			       blocksize, nilfs->ns_blocksize);
			goto scan_error;
		}

		err = nilfs_store_log_cursor(nilfs, sbp[0]);
		if (err)
			goto scan_error;

		/* drop clean flag to allow roll-forward and recovery */
		nilfs->ns_mount_state &= ~NILFS_VALID_FS;
		valid_fs = 0;

		err = nilfs_search_super_root(nilfs, &ri);
		if (err)
			goto scan_error;
	}

	err = nilfs_load_super_root(nilfs, sb, ri.ri_super_root);
	if (unlikely(err)) {
		printk(KERN_ERR "NILFS: error loading super root.\n");
		goto failed;
	}

	if (valid_fs)
		goto skip_recovery;

	if (s_flags & MS_RDONLY) {
		__u64 features;

		if (nilfs_test_opt(nilfs, NORECOVERY)) {
			printk(KERN_INFO "NILFS: norecovery option specified. "
			       "skipping roll-forward recovery\n");
			goto skip_recovery;
		}
		features = le64_to_cpu(nilfs->ns_sbp[0]->s_feature_compat_ro) &
			~NILFS_FEATURE_COMPAT_RO_SUPP;
		if (features) {
			printk(KERN_ERR "NILFS: couldn't proceed with "
			       "recovery because of unsupported optional "
			       "features (%llx)\n",
			       (unsigned long long)features);
			err = -EROFS;
			goto failed_unload;
		}
		if (really_read_only) {
			printk(KERN_ERR "NILFS: write access "
			       "unavailable, cannot proceed.\n");
			err = -EROFS;
			goto failed_unload;
		}
		sb->s_flags &= ~MS_RDONLY;
	} else if (nilfs_test_opt(nilfs, NORECOVERY)) {
		printk(KERN_ERR "NILFS: recovery cancelled because norecovery "
		       "option was specified for a read/write mount\n");
		err = -EINVAL;
		goto failed_unload;
	}

	err = nilfs_salvage_orphan_logs(nilfs, sb, &ri);
	if (err)
		goto failed_unload;

	down_write(&nilfs->ns_sem);
	nilfs->ns_mount_state |= NILFS_VALID_FS; /* set "clean" flag */
	err = nilfs_cleanup_super(sb);
	up_write(&nilfs->ns_sem);

	if (err) {
		printk(KERN_ERR "NILFS: failed to update super block. "
		       "recovery unfinished.\n");
		goto failed_unload;
	}
	printk(KERN_INFO "NILFS: recovery complete.\n");

 skip_recovery:
	nilfs_clear_recovery_info(&ri);
	sb->s_flags = s_flags;
	return 0;

 scan_error:
	printk(KERN_ERR "NILFS: error searching super root.\n");
	goto failed;

 failed_unload:
	iput(nilfs->ns_cpfile);
	iput(nilfs->ns_sufile);
	iput(nilfs->ns_dat);

 failed:
	nilfs_clear_recovery_info(&ri);
	sb->s_flags = s_flags;
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

/**
 * nilfs_nrsvsegs - calculate the number of reserved segments
 * @nilfs: nilfs object
 * @nsegs: total number of segments
 */
unsigned long nilfs_nrsvsegs(struct the_nilfs *nilfs, unsigned long nsegs)
{
	return max_t(unsigned long, NILFS_MIN_NRSVSEGS,
		     DIV_ROUND_UP(nsegs * nilfs->ns_r_segments_percentage,
				  100));
}

void nilfs_set_nsegments(struct the_nilfs *nilfs, unsigned long nsegs)
{
	nilfs->ns_nsegments = nsegs;
	nilfs->ns_nrsvsegs = nilfs_nrsvsegs(nilfs, nsegs);
}

static int nilfs_store_disk_layout(struct the_nilfs *nilfs,
				   struct nilfs_super_block *sbp)
{
	if (le32_to_cpu(sbp->s_rev_level) < NILFS_MIN_SUPP_REV) {
		printk(KERN_ERR "NILFS: unsupported revision "
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
	if (nilfs->ns_inode_size > nilfs->ns_blocksize) {
		printk(KERN_ERR "NILFS: too large inode size: %d bytes.\n",
		       nilfs->ns_inode_size);
		return -EINVAL;
	} else if (nilfs->ns_inode_size < NILFS_MIN_INODE_SIZE) {
		printk(KERN_ERR "NILFS: too small inode size: %d bytes.\n",
		       nilfs->ns_inode_size);
		return -EINVAL;
	}

	nilfs->ns_first_ino = le32_to_cpu(sbp->s_first_ino);

	nilfs->ns_blocks_per_segment = le32_to_cpu(sbp->s_blocks_per_segment);
	if (nilfs->ns_blocks_per_segment < NILFS_SEG_MIN_BLOCKS) {
		printk(KERN_ERR "NILFS: too short segment.\n");
		return -EINVAL;
	}

	nilfs->ns_first_data_block = le64_to_cpu(sbp->s_first_data_block);
	nilfs->ns_r_segments_percentage =
		le32_to_cpu(sbp->s_r_segments_percentage);
	if (nilfs->ns_r_segments_percentage < 1 ||
	    nilfs->ns_r_segments_percentage > 99) {
		printk(KERN_ERR "NILFS: invalid reserved segments percentage.\n");
		return -EINVAL;
	}

	nilfs_set_nsegments(nilfs, le64_to_cpu(sbp->s_nsegments));
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
		       "NILFS warning: unable to read primary superblock "
		       "(blocksize = %d)\n", blocksize);
	} else if (!sbp[1]) {
		printk(KERN_WARNING
		       "NILFS warning: unable to read secondary superblock "
		       "(blocksize = %d)\n", blocksize);
	}

	/*
	 * Compare two super blocks and set 1 in swp if the secondary
	 * super block is valid and newer.  Otherwise, set 0 in swp.
	 */
	valid[0] = nilfs_valid_sb(sbp[0]);
	valid[1] = nilfs_valid_sb(sbp[1]);
	swp = valid[1] && (!valid[0] ||
			   le64_to_cpu(sbp[1]->s_last_cno) >
			   le64_to_cpu(sbp[0]->s_last_cno));

	if (valid[swp] && nilfs_sb2_bad_offset(sbp[swp], sb2off)) {
		brelse(sbh[1]);
		sbh[1] = NULL;
		sbp[1] = NULL;
		valid[1] = 0;
		swp = 0;
	}
	if (!valid[swp]) {
		nilfs_release_super_block(nilfs);
		printk(KERN_ERR "NILFS: Can't find nilfs on dev %s.\n",
		       sb->s_id);
		return -EINVAL;
	}

	if (!valid[!swp])
		printk(KERN_WARNING "NILFS warning: broken superblock. "
		       "using spare superblock (blocksize = %d).\n", blocksize);
	if (swp)
		nilfs_swap_super_block(nilfs);

	nilfs->ns_sbwcount = 0;
	nilfs->ns_sbwtime = le64_to_cpu(sbp[0]->s_wtime);
	nilfs->ns_prot_seq = le64_to_cpu(sbp[valid[1] & !swp]->s_last_seq);
	*sbpp = sbp[0];
	return 0;
}

/**
 * init_nilfs - initialize a NILFS instance.
 * @nilfs: the_nilfs structure
 * @sb: super block
 * @data: mount options
 *
 * init_nilfs() performs common initialization per block device (e.g.
 * reading the super block, getting disk layout information, initializing
 * shared fields in the_nilfs).
 *
 * Return Value: On success, 0 is returned. On error, a negative error
 * code is returned.
 */
int init_nilfs(struct the_nilfs *nilfs, struct super_block *sb, char *data)
{
	struct nilfs_super_block *sbp;
	int blocksize;
	int err;

	down_write(&nilfs->ns_sem);

	blocksize = sb_min_blocksize(sb, NILFS_MIN_BLOCK_SIZE);
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

	err = nilfs_check_feature_compatibility(sb, sbp);
	if (err)
		goto failed_sbh;

	blocksize = BLOCK_SIZE << le32_to_cpu(sbp->s_log_block_size);
	if (blocksize < NILFS_MIN_BLOCK_SIZE ||
	    blocksize > NILFS_MAX_BLOCK_SIZE) {
		printk(KERN_ERR "NILFS: couldn't mount because of unsupported "
		       "filesystem blocksize %d\n", blocksize);
		err = -EINVAL;
		goto failed_sbh;
	}
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
			/*
			 * Not to failed_sbh; sbh is released automatically
			 * when reloading fails.
			 */
	}
	nilfs->ns_blocksize_bits = sb->s_blocksize_bits;
	nilfs->ns_blocksize = blocksize;

	get_random_bytes(&nilfs->ns_next_generation,
			 sizeof(nilfs->ns_next_generation));

	err = nilfs_store_disk_layout(nilfs, sbp);
	if (err)
		goto failed_sbh;

	sb->s_maxbytes = nilfs_max_size(sb->s_blocksize_bits);

	nilfs->ns_mount_state = le16_to_cpu(sbp->s_state);

	err = nilfs_store_log_cursor(nilfs, sbp);
	if (err)
		goto failed_sbh;

	err = nilfs_sysfs_create_device_group(sb);
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

int nilfs_discard_segments(struct the_nilfs *nilfs, __u64 *segnump,
			    size_t nsegs)
{
	sector_t seg_start, seg_end;
	sector_t start = 0, nblocks = 0;
	unsigned int sects_per_block;
	__u64 *sn;
	int ret = 0;

	sects_per_block = (1 << nilfs->ns_blocksize_bits) /
		bdev_logical_block_size(nilfs->ns_bdev);
	for (sn = segnump; sn < segnump + nsegs; sn++) {
		nilfs_get_segment_range(nilfs, *sn, &seg_start, &seg_end);

		if (!nblocks) {
			start = seg_start;
			nblocks = seg_end - seg_start + 1;
		} else if (start + nblocks == seg_start) {
			nblocks += seg_end - seg_start + 1;
		} else {
			ret = blkdev_issue_discard(nilfs->ns_bdev,
						   start * sects_per_block,
						   nblocks * sects_per_block,
						   GFP_NOFS, 0);
			if (ret < 0)
				return ret;
			nblocks = 0;
		}
	}
	if (nblocks)
		ret = blkdev_issue_discard(nilfs->ns_bdev,
					   start * sects_per_block,
					   nblocks * sects_per_block,
					   GFP_NOFS, 0);
	return ret;
}

int nilfs_count_free_blocks(struct the_nilfs *nilfs, sector_t *nblocks)
{
	unsigned long ncleansegs;

	down_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
	ncleansegs = nilfs_sufile_get_ncleansegs(nilfs->ns_sufile);
	up_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
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

struct nilfs_root *nilfs_lookup_root(struct the_nilfs *nilfs, __u64 cno)
{
	struct rb_node *n;
	struct nilfs_root *root;

	spin_lock(&nilfs->ns_cptree_lock);
	n = nilfs->ns_cptree.rb_node;
	while (n) {
		root = rb_entry(n, struct nilfs_root, rb_node);

		if (cno < root->cno) {
			n = n->rb_left;
		} else if (cno > root->cno) {
			n = n->rb_right;
		} else {
			atomic_inc(&root->count);
			spin_unlock(&nilfs->ns_cptree_lock);
			return root;
		}
	}
	spin_unlock(&nilfs->ns_cptree_lock);

	return NULL;
}

struct nilfs_root *
nilfs_find_or_create_root(struct the_nilfs *nilfs, __u64 cno)
{
	struct rb_node **p, *parent;
	struct nilfs_root *root, *new;
	int err;

	root = nilfs_lookup_root(nilfs, cno);
	if (root)
		return root;

	new = kzalloc(sizeof(*root), GFP_KERNEL);
	if (!new)
		return NULL;

	spin_lock(&nilfs->ns_cptree_lock);

	p = &nilfs->ns_cptree.rb_node;
	parent = NULL;

	while (*p) {
		parent = *p;
		root = rb_entry(parent, struct nilfs_root, rb_node);

		if (cno < root->cno) {
			p = &(*p)->rb_left;
		} else if (cno > root->cno) {
			p = &(*p)->rb_right;
		} else {
			atomic_inc(&root->count);
			spin_unlock(&nilfs->ns_cptree_lock);
			kfree(new);
			return root;
		}
	}

	new->cno = cno;
	new->ifile = NULL;
	new->nilfs = nilfs;
	atomic_set(&new->count, 1);
	atomic64_set(&new->inodes_count, 0);
	atomic64_set(&new->blocks_count, 0);

	rb_link_node(&new->rb_node, parent, p);
	rb_insert_color(&new->rb_node, &nilfs->ns_cptree);

	spin_unlock(&nilfs->ns_cptree_lock);

	err = nilfs_sysfs_create_snapshot_group(new);
	if (err) {
		kfree(new);
		new = NULL;
	}

	return new;
}

void nilfs_put_root(struct nilfs_root *root)
{
	if (atomic_dec_and_test(&root->count)) {
		struct the_nilfs *nilfs = root->nilfs;

		nilfs_sysfs_delete_snapshot_group(root);

		spin_lock(&nilfs->ns_cptree_lock);
		rb_erase(&root->rb_node, &nilfs->ns_cptree);
		spin_unlock(&nilfs->ns_cptree_lock);
		iput(root->ifile);

		kfree(root);
	}
}

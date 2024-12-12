// SPDX-License-Identifier: GPL-2.0+
/*
 * the_nilfs shared structure.
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Ryusuke Konishi.
 *
 */

#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/log2.h>
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
 * @sb: super block instance
 *
 * Return Value: On success, pointer to the_nilfs is returned.
 * On error, NULL is returned.
 */
struct the_nilfs *alloc_nilfs(struct super_block *sb)
{
	struct the_nilfs *nilfs;

	nilfs = kzalloc(sizeof(*nilfs), GFP_KERNEL);
	if (!nilfs)
		return NULL;

	nilfs->ns_sb = sb;
	nilfs->ns_bdev = sb->s_bdev;
	atomic_set(&nilfs->ns_ndirtyblks, 0);
	init_rwsem(&nilfs->ns_sem);
	mutex_init(&nilfs->ns_snapshot_mount_mutex);
	INIT_LIST_HEAD(&nilfs->ns_dirty_files);
	INIT_LIST_HEAD(&nilfs->ns_gc_inodes);
	spin_lock_init(&nilfs->ns_inode_lock);
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
		nilfs_err(nilfs->ns_sb,
			  "pointed segment number is out of range: segnum=%llu, nsegments=%lu",
			  (unsigned long long)nilfs->ns_segnum,
			  nilfs->ns_nsegments);
		ret = -EINVAL;
	}
	return ret;
}

/**
 * nilfs_get_blocksize - get block size from raw superblock data
 * @sb: super block instance
 * @sbp: superblock raw data buffer
 * @blocksize: place to store block size
 *
 * nilfs_get_blocksize() calculates the block size from the block size
 * exponent information written in @sbp and stores it in @blocksize,
 * or aborts with an error message if it's too large.
 *
 * Return Value: On success, 0 is returned. If the block size is too
 * large, -EINVAL is returned.
 */
static int nilfs_get_blocksize(struct super_block *sb,
			       struct nilfs_super_block *sbp, int *blocksize)
{
	unsigned int shift_bits = le32_to_cpu(sbp->s_log_block_size);

	if (unlikely(shift_bits >
		     ilog2(NILFS_MAX_BLOCK_SIZE) - BLOCK_SIZE_BITS)) {
		nilfs_err(sb, "too large filesystem blocksize: 2 ^ %u KiB",
			  shift_bits);
		return -EINVAL;
	}
	*blocksize = BLOCK_SIZE << shift_bits;
	return 0;
}

/**
 * load_nilfs - load and recover the nilfs
 * @nilfs: the_nilfs structure to be released
 * @sb: super block instance used to recover past segment
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
		nilfs_warn(sb, "mounting unchecked fs");
		if (s_flags & SB_RDONLY) {
			nilfs_info(sb,
				   "recovery required for readonly filesystem");
			nilfs_info(sb,
				   "write access will be enabled during recovery");
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
			nilfs_warn(sb,
				   "unable to fall back to spare super block");
			goto scan_error;
		}
		nilfs_info(sb, "trying rollback from an earlier position");

		/*
		 * restore super block with its spare and reconfigure
		 * relevant states of the nilfs object.
		 */
		memcpy(sbp[0], sbp[1], nilfs->ns_sbsize);
		nilfs->ns_crc_seed = le32_to_cpu(sbp[0]->s_crc_seed);
		nilfs->ns_sbwtime = le64_to_cpu(sbp[0]->s_wtime);

		/* verify consistency between two super blocks */
		err = nilfs_get_blocksize(sb, sbp[0], &blocksize);
		if (err)
			goto scan_error;

		if (blocksize != nilfs->ns_blocksize) {
			nilfs_warn(sb,
				   "blocksize differs between two super blocks (%d != %d)",
				   blocksize, nilfs->ns_blocksize);
			err = -EINVAL;
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
		nilfs_err(sb, "error %d while loading super root", err);
		goto failed;
	}

	err = nilfs_sysfs_create_device_group(sb);
	if (unlikely(err))
		goto sysfs_error;

	if (valid_fs)
		goto skip_recovery;

	if (s_flags & SB_RDONLY) {
		__u64 features;

		if (nilfs_test_opt(nilfs, NORECOVERY)) {
			nilfs_info(sb,
				   "norecovery option specified, skipping roll-forward recovery");
			goto skip_recovery;
		}
		features = le64_to_cpu(nilfs->ns_sbp[0]->s_feature_compat_ro) &
			~NILFS_FEATURE_COMPAT_RO_SUPP;
		if (features) {
			nilfs_err(sb,
				  "couldn't proceed with recovery because of unsupported optional features (%llx)",
				  (unsigned long long)features);
			err = -EROFS;
			goto failed_unload;
		}
		if (really_read_only) {
			nilfs_err(sb,
				  "write access unavailable, cannot proceed");
			err = -EROFS;
			goto failed_unload;
		}
		sb->s_flags &= ~SB_RDONLY;
	} else if (nilfs_test_opt(nilfs, NORECOVERY)) {
		nilfs_err(sb,
			  "recovery cancelled because norecovery option was specified for a read/write mount");
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
		nilfs_err(sb,
			  "error %d updating super block. recovery unfinished.",
			  err);
		goto failed_unload;
	}
	nilfs_info(sb, "recovery complete");

 skip_recovery:
	nilfs_clear_recovery_info(&ri);
	sb->s_flags = s_flags;
	return 0;

 scan_error:
	nilfs_err(sb, "error %d while searching super root", err);
	goto failed;

 failed_unload:
	nilfs_sysfs_delete_device_group(nilfs);

 sysfs_error:
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

/**
 * nilfs_max_segment_count - calculate the maximum number of segments
 * @nilfs: nilfs object
 */
static u64 nilfs_max_segment_count(struct the_nilfs *nilfs)
{
	u64 max_count = U64_MAX;

	max_count = div64_ul(max_count, nilfs->ns_blocks_per_segment);
	return min_t(u64, max_count, ULONG_MAX);
}

void nilfs_set_nsegments(struct the_nilfs *nilfs, unsigned long nsegs)
{
	nilfs->ns_nsegments = nsegs;
	nilfs->ns_nrsvsegs = nilfs_nrsvsegs(nilfs, nsegs);
}

static int nilfs_store_disk_layout(struct the_nilfs *nilfs,
				   struct nilfs_super_block *sbp)
{
	u64 nsegments, nblocks;

	if (le32_to_cpu(sbp->s_rev_level) < NILFS_MIN_SUPP_REV) {
		nilfs_err(nilfs->ns_sb,
			  "unsupported revision (superblock rev.=%d.%d, current rev.=%d.%d). Please check the version of mkfs.nilfs(2).",
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
		nilfs_err(nilfs->ns_sb, "too large inode size: %d bytes",
			  nilfs->ns_inode_size);
		return -EINVAL;
	} else if (nilfs->ns_inode_size < NILFS_MIN_INODE_SIZE) {
		nilfs_err(nilfs->ns_sb, "too small inode size: %d bytes",
			  nilfs->ns_inode_size);
		return -EINVAL;
	}

	nilfs->ns_first_ino = le32_to_cpu(sbp->s_first_ino);
	if (nilfs->ns_first_ino < NILFS_USER_INO) {
		nilfs_err(nilfs->ns_sb,
			  "too small lower limit for non-reserved inode numbers: %u",
			  nilfs->ns_first_ino);
		return -EINVAL;
	}

	nilfs->ns_blocks_per_segment = le32_to_cpu(sbp->s_blocks_per_segment);
	if (nilfs->ns_blocks_per_segment < NILFS_SEG_MIN_BLOCKS) {
		nilfs_err(nilfs->ns_sb, "too short segment: %lu blocks",
			  nilfs->ns_blocks_per_segment);
		return -EINVAL;
	}

	nilfs->ns_first_data_block = le64_to_cpu(sbp->s_first_data_block);
	nilfs->ns_r_segments_percentage =
		le32_to_cpu(sbp->s_r_segments_percentage);
	if (nilfs->ns_r_segments_percentage < 1 ||
	    nilfs->ns_r_segments_percentage > 99) {
		nilfs_err(nilfs->ns_sb,
			  "invalid reserved segments percentage: %lu",
			  nilfs->ns_r_segments_percentage);
		return -EINVAL;
	}

	nsegments = le64_to_cpu(sbp->s_nsegments);
	if (nsegments > nilfs_max_segment_count(nilfs)) {
		nilfs_err(nilfs->ns_sb,
			  "segment count %llu exceeds upper limit (%llu segments)",
			  (unsigned long long)nsegments,
			  (unsigned long long)nilfs_max_segment_count(nilfs));
		return -EINVAL;
	}

	nblocks = sb_bdev_nr_blocks(nilfs->ns_sb);
	if (nblocks) {
		u64 min_block_count = nsegments * nilfs->ns_blocks_per_segment;
		/*
		 * To avoid failing to mount early device images without a
		 * second superblock, exclude that block count from the
		 * "min_block_count" calculation.
		 */

		if (nblocks < min_block_count) {
			nilfs_err(nilfs->ns_sb,
				  "total number of segment blocks %llu exceeds device size (%llu blocks)",
				  (unsigned long long)min_block_count,
				  (unsigned long long)nblocks);
			return -EINVAL;
		}
	}

	nilfs_set_nsegments(nilfs, nsegments);
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
	if (bytes < sumoff + 4 || bytes > BLOCK_SIZE)
		return 0;
	crc = crc32_le(le32_to_cpu(sbp->s_crc_seed), (unsigned char *)sbp,
		       sumoff);
	crc = crc32_le(crc, sum, 4);
	crc = crc32_le(crc, (unsigned char *)sbp + sumoff + 4,
		       bytes - sumoff - 4);
	return crc == le32_to_cpu(sbp->s_sum);
}

/**
 * nilfs_sb2_bad_offset - check the location of the second superblock
 * @sbp: superblock raw data buffer
 * @offset: byte offset of second superblock calculated from device size
 *
 * nilfs_sb2_bad_offset() checks if the position on the second
 * superblock is valid or not based on the filesystem parameters
 * stored in @sbp.  If @offset points to a location within the segment
 * area, or if the parameters themselves are not normal, it is
 * determined to be invalid.
 *
 * Return Value: true if invalid, false if valid.
 */
static bool nilfs_sb2_bad_offset(struct nilfs_super_block *sbp, u64 offset)
{
	unsigned int shift_bits = le32_to_cpu(sbp->s_log_block_size);
	u32 blocks_per_segment = le32_to_cpu(sbp->s_blocks_per_segment);
	u64 nsegments = le64_to_cpu(sbp->s_nsegments);
	u64 index;

	if (blocks_per_segment < NILFS_SEG_MIN_BLOCKS ||
	    shift_bits > ilog2(NILFS_MAX_BLOCK_SIZE) - BLOCK_SIZE_BITS)
		return true;

	index = offset >> (shift_bits + BLOCK_SIZE_BITS);
	do_div(index, blocks_per_segment);
	return index < nsegments;
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
	u64 sb2off, devsize = bdev_nr_bytes(nilfs->ns_bdev);
	int valid[2], swp = 0, older;

	if (devsize < NILFS_SEG_MIN_BLOCKS * NILFS_MIN_BLOCK_SIZE + 4096) {
		nilfs_err(sb, "device size too small");
		return -EINVAL;
	}
	sb2off = NILFS_SB2_OFFSET_BYTES(devsize);

	sbp[0] = nilfs_read_super_block(sb, NILFS_SB_OFFSET_BYTES, blocksize,
					&sbh[0]);
	sbp[1] = nilfs_read_super_block(sb, sb2off, blocksize, &sbh[1]);

	if (!sbp[0]) {
		if (!sbp[1]) {
			nilfs_err(sb, "unable to read superblock");
			return -EIO;
		}
		nilfs_warn(sb,
			   "unable to read primary superblock (blocksize = %d)",
			   blocksize);
	} else if (!sbp[1]) {
		nilfs_warn(sb,
			   "unable to read secondary superblock (blocksize = %d)",
			   blocksize);
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
		nilfs_err(sb, "couldn't find nilfs on the device");
		return -EINVAL;
	}

	if (!valid[!swp])
		nilfs_warn(sb,
			   "broken superblock, retrying with spare superblock (blocksize = %d)",
			   blocksize);
	if (swp)
		nilfs_swap_super_block(nilfs);

	/*
	 * Calculate the array index of the older superblock data.
	 * If one has been dropped, set index 0 pointing to the remaining one,
	 * otherwise set index 1 pointing to the old one (including if both
	 * are the same).
	 *
	 *  Divided case             valid[0]  valid[1]  swp  ->  older
	 *  -------------------------------------------------------------
	 *  Both SBs are invalid        0         0       N/A (Error)
	 *  SB1 is invalid              0         1       1         0
	 *  SB2 is invalid              1         0       0         0
	 *  SB2 is newer                1         1       1         0
	 *  SB2 is older or the same    1         1       0         1
	 */
	older = valid[1] ^ swp;

	nilfs->ns_sbwcount = 0;
	nilfs->ns_sbwtime = le64_to_cpu(sbp[0]->s_wtime);
	nilfs->ns_prot_seq = le64_to_cpu(sbp[older]->s_last_seq);
	*sbpp = sbp[0];
	return 0;
}

/**
 * init_nilfs - initialize a NILFS instance.
 * @nilfs: the_nilfs structure
 * @sb: super block
 *
 * init_nilfs() performs common initialization per block device (e.g.
 * reading the super block, getting disk layout information, initializing
 * shared fields in the_nilfs).
 *
 * Return Value: On success, 0 is returned. On error, a negative error
 * code is returned.
 */
int init_nilfs(struct the_nilfs *nilfs, struct super_block *sb)
{
	struct nilfs_super_block *sbp;
	int blocksize;
	int err;

	down_write(&nilfs->ns_sem);

	blocksize = sb_min_blocksize(sb, NILFS_MIN_BLOCK_SIZE);
	if (!blocksize) {
		nilfs_err(sb, "unable to set blocksize");
		err = -EINVAL;
		goto out;
	}
	err = nilfs_load_super_block(nilfs, sb, blocksize, &sbp);
	if (err)
		goto out;

	err = nilfs_store_magic(sb, sbp);
	if (err)
		goto failed_sbh;

	err = nilfs_check_feature_compatibility(sb, sbp);
	if (err)
		goto failed_sbh;

	err = nilfs_get_blocksize(sb, sbp, &blocksize);
	if (err)
		goto failed_sbh;

	if (blocksize < NILFS_MIN_BLOCK_SIZE) {
		nilfs_err(sb,
			  "couldn't mount because of unsupported filesystem blocksize %d",
			  blocksize);
		err = -EINVAL;
		goto failed_sbh;
	}
	if (sb->s_blocksize != blocksize) {
		int hw_blocksize = bdev_logical_block_size(sb->s_bdev);

		if (blocksize < hw_blocksize) {
			nilfs_err(sb,
				  "blocksize %d too small for device (sector-size = %d)",
				  blocksize, hw_blocksize);
			err = -EINVAL;
			goto failed_sbh;
		}
		nilfs_release_super_block(nilfs);
		if (!sb_set_blocksize(sb, blocksize)) {
			nilfs_err(sb, "bad blocksize %d", blocksize);
			err = -EINVAL;
			goto out;
		}

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

	err = nilfs_store_disk_layout(nilfs, sbp);
	if (err)
		goto failed_sbh;

	sb->s_maxbytes = nilfs_max_size(sb->s_blocksize_bits);

	nilfs->ns_mount_state = le16_to_cpu(sbp->s_state);

	err = nilfs_store_log_cursor(nilfs, sbp);
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
						   GFP_NOFS);
			if (ret < 0)
				return ret;
			nblocks = 0;
		}
	}
	if (nblocks)
		ret = blkdev_issue_discard(nilfs->ns_bdev,
					   start * sects_per_block,
					   nblocks * sects_per_block,
					   GFP_NOFS);
	return ret;
}

int nilfs_count_free_blocks(struct the_nilfs *nilfs, sector_t *nblocks)
{
	unsigned long ncleansegs;

	ncleansegs = nilfs_sufile_get_ncleansegs(nilfs->ns_sufile);
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
			refcount_inc(&root->count);
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
			refcount_inc(&root->count);
			spin_unlock(&nilfs->ns_cptree_lock);
			kfree(new);
			return root;
		}
	}

	new->cno = cno;
	new->ifile = NULL;
	new->nilfs = nilfs;
	refcount_set(&new->count, 1);
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
	struct the_nilfs *nilfs = root->nilfs;

	if (refcount_dec_and_lock(&root->count, &nilfs->ns_cptree_lock)) {
		rb_erase(&root->rb_node, &nilfs->ns_cptree);
		spin_unlock(&nilfs->ns_cptree_lock);

		nilfs_sysfs_delete_snapshot_group(root);
		iput(root->ifile);

		kfree(root);
	}
}

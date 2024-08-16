// SPDX-License-Identifier: GPL-2.0+
/*
 * NILFS checkpoint file.
 *
 * Copyright (C) 2006-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Koji Sato.
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/buffer_head.h>
#include <linux/errno.h>
#include "mdt.h"
#include "cpfile.h"


static inline unsigned long
nilfs_cpfile_checkpoints_per_block(const struct inode *cpfile)
{
	return NILFS_MDT(cpfile)->mi_entries_per_block;
}

/* block number from the beginning of the file */
static unsigned long
nilfs_cpfile_get_blkoff(const struct inode *cpfile, __u64 cno)
{
	__u64 tcno = cno + NILFS_MDT(cpfile)->mi_first_entry_offset - 1;

	tcno = div64_ul(tcno, nilfs_cpfile_checkpoints_per_block(cpfile));
	return (unsigned long)tcno;
}

/* offset in block */
static unsigned long
nilfs_cpfile_get_offset(const struct inode *cpfile, __u64 cno)
{
	__u64 tcno = cno + NILFS_MDT(cpfile)->mi_first_entry_offset - 1;

	return do_div(tcno, nilfs_cpfile_checkpoints_per_block(cpfile));
}

static __u64 nilfs_cpfile_first_checkpoint_in_block(const struct inode *cpfile,
						    unsigned long blkoff)
{
	return (__u64)nilfs_cpfile_checkpoints_per_block(cpfile) * blkoff
		+ 1 - NILFS_MDT(cpfile)->mi_first_entry_offset;
}

static unsigned long
nilfs_cpfile_checkpoints_in_block(const struct inode *cpfile,
				  __u64 curr,
				  __u64 max)
{
	return min_t(__u64,
		     nilfs_cpfile_checkpoints_per_block(cpfile) -
		     nilfs_cpfile_get_offset(cpfile, curr),
		     max - curr);
}

static inline int nilfs_cpfile_is_in_first(const struct inode *cpfile,
					   __u64 cno)
{
	return nilfs_cpfile_get_blkoff(cpfile, cno) == 0;
}

static unsigned int
nilfs_cpfile_block_add_valid_checkpoints(const struct inode *cpfile,
					 struct buffer_head *bh,
					 void *kaddr,
					 unsigned int n)
{
	struct nilfs_checkpoint *cp = kaddr + bh_offset(bh);
	unsigned int count;

	count = le32_to_cpu(cp->cp_checkpoints_count) + n;
	cp->cp_checkpoints_count = cpu_to_le32(count);
	return count;
}

static unsigned int
nilfs_cpfile_block_sub_valid_checkpoints(const struct inode *cpfile,
					 struct buffer_head *bh,
					 void *kaddr,
					 unsigned int n)
{
	struct nilfs_checkpoint *cp = kaddr + bh_offset(bh);
	unsigned int count;

	WARN_ON(le32_to_cpu(cp->cp_checkpoints_count) < n);
	count = le32_to_cpu(cp->cp_checkpoints_count) - n;
	cp->cp_checkpoints_count = cpu_to_le32(count);
	return count;
}

static inline struct nilfs_cpfile_header *
nilfs_cpfile_block_get_header(const struct inode *cpfile,
			      struct buffer_head *bh,
			      void *kaddr)
{
	return kaddr + bh_offset(bh);
}

static struct nilfs_checkpoint *
nilfs_cpfile_block_get_checkpoint(const struct inode *cpfile, __u64 cno,
				  struct buffer_head *bh,
				  void *kaddr)
{
	return kaddr + bh_offset(bh) + nilfs_cpfile_get_offset(cpfile, cno) *
		NILFS_MDT(cpfile)->mi_entry_size;
}

static void nilfs_cpfile_block_init(struct inode *cpfile,
				    struct buffer_head *bh,
				    void *kaddr)
{
	struct nilfs_checkpoint *cp = kaddr + bh_offset(bh);
	size_t cpsz = NILFS_MDT(cpfile)->mi_entry_size;
	int n = nilfs_cpfile_checkpoints_per_block(cpfile);

	while (n-- > 0) {
		nilfs_checkpoint_set_invalid(cp);
		cp = (void *)cp + cpsz;
	}
}

static inline int nilfs_cpfile_get_header_block(struct inode *cpfile,
						struct buffer_head **bhp)
{
	return nilfs_mdt_get_block(cpfile, 0, 0, NULL, bhp);
}

static inline int nilfs_cpfile_get_checkpoint_block(struct inode *cpfile,
						    __u64 cno,
						    int create,
						    struct buffer_head **bhp)
{
	return nilfs_mdt_get_block(cpfile,
				   nilfs_cpfile_get_blkoff(cpfile, cno),
				   create, nilfs_cpfile_block_init, bhp);
}

/**
 * nilfs_cpfile_find_checkpoint_block - find and get a buffer on cpfile
 * @cpfile: inode of cpfile
 * @start_cno: start checkpoint number (inclusive)
 * @end_cno: end checkpoint number (inclusive)
 * @cnop: place to store the next checkpoint number
 * @bhp: place to store a pointer to buffer_head struct
 *
 * Return Value: On success, it returns 0. On error, the following negative
 * error code is returned.
 *
 * %-ENOMEM - Insufficient memory available.
 *
 * %-EIO - I/O error
 *
 * %-ENOENT - no block exists in the range.
 */
static int nilfs_cpfile_find_checkpoint_block(struct inode *cpfile,
					      __u64 start_cno, __u64 end_cno,
					      __u64 *cnop,
					      struct buffer_head **bhp)
{
	unsigned long start, end, blkoff;
	int ret;

	if (unlikely(start_cno > end_cno))
		return -ENOENT;

	start = nilfs_cpfile_get_blkoff(cpfile, start_cno);
	end = nilfs_cpfile_get_blkoff(cpfile, end_cno);

	ret = nilfs_mdt_find_block(cpfile, start, end, &blkoff, bhp);
	if (!ret)
		*cnop = (blkoff == start) ? start_cno :
			nilfs_cpfile_first_checkpoint_in_block(cpfile, blkoff);
	return ret;
}

static inline int nilfs_cpfile_delete_checkpoint_block(struct inode *cpfile,
						       __u64 cno)
{
	return nilfs_mdt_delete_block(cpfile,
				      nilfs_cpfile_get_blkoff(cpfile, cno));
}

/**
 * nilfs_cpfile_read_checkpoint - read a checkpoint entry in cpfile
 * @cpfile: checkpoint file inode
 * @cno:    number of checkpoint entry to read
 * @root:   nilfs root object
 * @ifile:  ifile's inode to read and attach to @root
 *
 * This function imports checkpoint information from the checkpoint file and
 * stores it to the inode file given by @ifile and the nilfs root object
 * given by @root.
 *
 * Return: 0 on success, or the following negative error code on failure.
 * * %-EINVAL	- Invalid checkpoint.
 * * %-ENOMEM	- Insufficient memory available.
 * * %-EIO	- I/O error (including metadata corruption).
 */
int nilfs_cpfile_read_checkpoint(struct inode *cpfile, __u64 cno,
				 struct nilfs_root *root, struct inode *ifile)
{
	struct buffer_head *cp_bh;
	struct nilfs_checkpoint *cp;
	void *kaddr;
	int ret;

	if (cno < 1 || cno > nilfs_mdt_cno(cpfile))
		return -EINVAL;

	down_read(&NILFS_MDT(cpfile)->mi_sem);
	ret = nilfs_cpfile_get_checkpoint_block(cpfile, cno, 0, &cp_bh);
	if (unlikely(ret < 0)) {
		if (ret == -ENOENT)
			ret = -EINVAL;
		goto out_sem;
	}

	kaddr = kmap_local_page(cp_bh->b_page);
	cp = nilfs_cpfile_block_get_checkpoint(cpfile, cno, cp_bh, kaddr);
	if (nilfs_checkpoint_invalid(cp)) {
		ret = -EINVAL;
		goto put_cp;
	}

	ret = nilfs_read_inode_common(ifile, &cp->cp_ifile_inode);
	if (unlikely(ret)) {
		/*
		 * Since this inode is on a checkpoint entry, treat errors
		 * as metadata corruption.
		 */
		nilfs_err(cpfile->i_sb,
			  "ifile inode (checkpoint number=%llu) corrupted",
			  (unsigned long long)cno);
		ret = -EIO;
		goto put_cp;
	}

	/* Configure the nilfs root object */
	atomic64_set(&root->inodes_count, le64_to_cpu(cp->cp_inodes_count));
	atomic64_set(&root->blocks_count, le64_to_cpu(cp->cp_blocks_count));
	root->ifile = ifile;

put_cp:
	kunmap_local(kaddr);
	brelse(cp_bh);
out_sem:
	up_read(&NILFS_MDT(cpfile)->mi_sem);
	return ret;
}

/**
 * nilfs_cpfile_create_checkpoint - create a checkpoint entry on cpfile
 * @cpfile: checkpoint file inode
 * @cno:    number of checkpoint to set up
 *
 * This function creates a checkpoint with the number specified by @cno on
 * cpfile.  If the specified checkpoint entry already exists due to a past
 * failure, it will be reused without returning an error.
 * In either case, the buffer of the block containing the checkpoint entry
 * and the cpfile inode are made dirty for inclusion in the write log.
 *
 * Return: 0 on success, or the following negative error code on failure.
 * * %-ENOMEM	- Insufficient memory available.
 * * %-EIO	- I/O error (including metadata corruption).
 * * %-EROFS	- Read only filesystem
 */
int nilfs_cpfile_create_checkpoint(struct inode *cpfile, __u64 cno)
{
	struct buffer_head *header_bh, *cp_bh;
	struct nilfs_cpfile_header *header;
	struct nilfs_checkpoint *cp;
	void *kaddr;
	int ret;

	if (WARN_ON_ONCE(cno < 1))
		return -EIO;

	down_write(&NILFS_MDT(cpfile)->mi_sem);
	ret = nilfs_cpfile_get_header_block(cpfile, &header_bh);
	if (unlikely(ret < 0)) {
		if (ret == -ENOENT) {
			nilfs_error(cpfile->i_sb,
				    "checkpoint creation failed due to metadata corruption.");
			ret = -EIO;
		}
		goto out_sem;
	}
	ret = nilfs_cpfile_get_checkpoint_block(cpfile, cno, 1, &cp_bh);
	if (unlikely(ret < 0))
		goto out_header;

	kaddr = kmap_local_page(cp_bh->b_page);
	cp = nilfs_cpfile_block_get_checkpoint(cpfile, cno, cp_bh, kaddr);
	if (nilfs_checkpoint_invalid(cp)) {
		/* a newly-created checkpoint */
		nilfs_checkpoint_clear_invalid(cp);
		if (!nilfs_cpfile_is_in_first(cpfile, cno))
			nilfs_cpfile_block_add_valid_checkpoints(cpfile, cp_bh,
								 kaddr, 1);
		kunmap_local(kaddr);

		kaddr = kmap_local_page(header_bh->b_page);
		header = nilfs_cpfile_block_get_header(cpfile, header_bh,
						       kaddr);
		le64_add_cpu(&header->ch_ncheckpoints, 1);
		kunmap_local(kaddr);
		mark_buffer_dirty(header_bh);
	} else {
		kunmap_local(kaddr);
	}

	/* Force the buffer and the inode to become dirty */
	mark_buffer_dirty(cp_bh);
	brelse(cp_bh);
	nilfs_mdt_mark_dirty(cpfile);

out_header:
	brelse(header_bh);

out_sem:
	up_write(&NILFS_MDT(cpfile)->mi_sem);
	return ret;
}

/**
 * nilfs_cpfile_finalize_checkpoint - fill in a checkpoint entry in cpfile
 * @cpfile: checkpoint file inode
 * @cno:    checkpoint number
 * @root:   nilfs root object
 * @blkinc: number of blocks added by this checkpoint
 * @ctime:  checkpoint creation time
 * @minor:  minor checkpoint flag
 *
 * This function completes the checkpoint entry numbered by @cno in the
 * cpfile with the data given by the arguments @root, @blkinc, @ctime, and
 * @minor.
 *
 * Return: 0 on success, or the following negative error code on failure.
 * * %-ENOMEM	- Insufficient memory available.
 * * %-EIO	- I/O error (including metadata corruption).
 */
int nilfs_cpfile_finalize_checkpoint(struct inode *cpfile, __u64 cno,
				     struct nilfs_root *root, __u64 blkinc,
				     time64_t ctime, bool minor)
{
	struct buffer_head *cp_bh;
	struct nilfs_checkpoint *cp;
	void *kaddr;
	int ret;

	if (WARN_ON_ONCE(cno < 1))
		return -EIO;

	down_write(&NILFS_MDT(cpfile)->mi_sem);
	ret = nilfs_cpfile_get_checkpoint_block(cpfile, cno, 0, &cp_bh);
	if (unlikely(ret < 0)) {
		if (ret == -ENOENT)
			goto error;
		goto out_sem;
	}

	kaddr = kmap_local_page(cp_bh->b_page);
	cp = nilfs_cpfile_block_get_checkpoint(cpfile, cno, cp_bh, kaddr);
	if (unlikely(nilfs_checkpoint_invalid(cp))) {
		kunmap_local(kaddr);
		brelse(cp_bh);
		goto error;
	}

	cp->cp_snapshot_list.ssl_next = 0;
	cp->cp_snapshot_list.ssl_prev = 0;
	cp->cp_inodes_count = cpu_to_le64(atomic64_read(&root->inodes_count));
	cp->cp_blocks_count = cpu_to_le64(atomic64_read(&root->blocks_count));
	cp->cp_nblk_inc = cpu_to_le64(blkinc);
	cp->cp_create = cpu_to_le64(ctime);
	cp->cp_cno = cpu_to_le64(cno);

	if (minor)
		nilfs_checkpoint_set_minor(cp);
	else
		nilfs_checkpoint_clear_minor(cp);

	nilfs_write_inode_common(root->ifile, &cp->cp_ifile_inode);
	nilfs_bmap_write(NILFS_I(root->ifile)->i_bmap, &cp->cp_ifile_inode);

	kunmap_local(kaddr);
	brelse(cp_bh);
out_sem:
	up_write(&NILFS_MDT(cpfile)->mi_sem);
	return ret;

error:
	nilfs_error(cpfile->i_sb,
		    "checkpoint finalization failed due to metadata corruption.");
	ret = -EIO;
	goto out_sem;
}

/**
 * nilfs_cpfile_delete_checkpoints - delete checkpoints
 * @cpfile: inode of checkpoint file
 * @start: start checkpoint number
 * @end: end checkpoint number
 *
 * Description: nilfs_cpfile_delete_checkpoints() deletes the checkpoints in
 * the period from @start to @end, excluding @end itself. The checkpoints
 * which have been already deleted are ignored.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-EINVAL - invalid checkpoints.
 */
int nilfs_cpfile_delete_checkpoints(struct inode *cpfile,
				    __u64 start,
				    __u64 end)
{
	struct buffer_head *header_bh, *cp_bh;
	struct nilfs_cpfile_header *header;
	struct nilfs_checkpoint *cp;
	size_t cpsz = NILFS_MDT(cpfile)->mi_entry_size;
	__u64 cno;
	void *kaddr;
	unsigned long tnicps;
	int ret, ncps, nicps, nss, count, i;

	if (unlikely(start == 0 || start > end)) {
		nilfs_err(cpfile->i_sb,
			  "cannot delete checkpoints: invalid range [%llu, %llu)",
			  (unsigned long long)start, (unsigned long long)end);
		return -EINVAL;
	}

	down_write(&NILFS_MDT(cpfile)->mi_sem);

	ret = nilfs_cpfile_get_header_block(cpfile, &header_bh);
	if (ret < 0)
		goto out_sem;
	tnicps = 0;
	nss = 0;

	for (cno = start; cno < end; cno += ncps) {
		ncps = nilfs_cpfile_checkpoints_in_block(cpfile, cno, end);
		ret = nilfs_cpfile_get_checkpoint_block(cpfile, cno, 0, &cp_bh);
		if (ret < 0) {
			if (ret != -ENOENT)
				break;
			/* skip hole */
			ret = 0;
			continue;
		}

		kaddr = kmap_local_page(cp_bh->b_page);
		cp = nilfs_cpfile_block_get_checkpoint(
			cpfile, cno, cp_bh, kaddr);
		nicps = 0;
		for (i = 0; i < ncps; i++, cp = (void *)cp + cpsz) {
			if (nilfs_checkpoint_snapshot(cp)) {
				nss++;
			} else if (!nilfs_checkpoint_invalid(cp)) {
				nilfs_checkpoint_set_invalid(cp);
				nicps++;
			}
		}
		if (nicps > 0) {
			tnicps += nicps;
			mark_buffer_dirty(cp_bh);
			nilfs_mdt_mark_dirty(cpfile);
			if (!nilfs_cpfile_is_in_first(cpfile, cno)) {
				count =
				  nilfs_cpfile_block_sub_valid_checkpoints(
						cpfile, cp_bh, kaddr, nicps);
				if (count == 0) {
					/* make hole */
					kunmap_local(kaddr);
					brelse(cp_bh);
					ret =
					  nilfs_cpfile_delete_checkpoint_block(
								   cpfile, cno);
					if (ret == 0)
						continue;
					nilfs_err(cpfile->i_sb,
						  "error %d deleting checkpoint block",
						  ret);
					break;
				}
			}
		}

		kunmap_local(kaddr);
		brelse(cp_bh);
	}

	if (tnicps > 0) {
		kaddr = kmap_local_page(header_bh->b_page);
		header = nilfs_cpfile_block_get_header(cpfile, header_bh,
						       kaddr);
		le64_add_cpu(&header->ch_ncheckpoints, -(u64)tnicps);
		mark_buffer_dirty(header_bh);
		nilfs_mdt_mark_dirty(cpfile);
		kunmap_local(kaddr);
	}

	brelse(header_bh);
	if (nss > 0)
		ret = -EBUSY;

 out_sem:
	up_write(&NILFS_MDT(cpfile)->mi_sem);
	return ret;
}

static void nilfs_cpfile_checkpoint_to_cpinfo(struct inode *cpfile,
					      struct nilfs_checkpoint *cp,
					      struct nilfs_cpinfo *ci)
{
	ci->ci_flags = le32_to_cpu(cp->cp_flags);
	ci->ci_cno = le64_to_cpu(cp->cp_cno);
	ci->ci_create = le64_to_cpu(cp->cp_create);
	ci->ci_nblk_inc = le64_to_cpu(cp->cp_nblk_inc);
	ci->ci_inodes_count = le64_to_cpu(cp->cp_inodes_count);
	ci->ci_blocks_count = le64_to_cpu(cp->cp_blocks_count);
	ci->ci_next = le64_to_cpu(cp->cp_snapshot_list.ssl_next);
}

static ssize_t nilfs_cpfile_do_get_cpinfo(struct inode *cpfile, __u64 *cnop,
					  void *buf, unsigned int cisz,
					  size_t nci)
{
	struct nilfs_checkpoint *cp;
	struct nilfs_cpinfo *ci = buf;
	struct buffer_head *bh;
	size_t cpsz = NILFS_MDT(cpfile)->mi_entry_size;
	__u64 cur_cno = nilfs_mdt_cno(cpfile), cno = *cnop;
	void *kaddr;
	int n, ret;
	int ncps, i;

	if (cno == 0)
		return -ENOENT; /* checkpoint number 0 is invalid */
	down_read(&NILFS_MDT(cpfile)->mi_sem);

	for (n = 0; n < nci; cno += ncps) {
		ret = nilfs_cpfile_find_checkpoint_block(
			cpfile, cno, cur_cno - 1, &cno, &bh);
		if (ret < 0) {
			if (likely(ret == -ENOENT))
				break;
			goto out;
		}
		ncps = nilfs_cpfile_checkpoints_in_block(cpfile, cno, cur_cno);

		kaddr = kmap_local_page(bh->b_page);
		cp = nilfs_cpfile_block_get_checkpoint(cpfile, cno, bh, kaddr);
		for (i = 0; i < ncps && n < nci; i++, cp = (void *)cp + cpsz) {
			if (!nilfs_checkpoint_invalid(cp)) {
				nilfs_cpfile_checkpoint_to_cpinfo(cpfile, cp,
								  ci);
				ci = (void *)ci + cisz;
				n++;
			}
		}
		kunmap_local(kaddr);
		brelse(bh);
	}

	ret = n;
	if (n > 0) {
		ci = (void *)ci - cisz;
		*cnop = ci->ci_cno + 1;
	}

 out:
	up_read(&NILFS_MDT(cpfile)->mi_sem);
	return ret;
}

static ssize_t nilfs_cpfile_do_get_ssinfo(struct inode *cpfile, __u64 *cnop,
					  void *buf, unsigned int cisz,
					  size_t nci)
{
	struct buffer_head *bh;
	struct nilfs_cpfile_header *header;
	struct nilfs_checkpoint *cp;
	struct nilfs_cpinfo *ci = buf;
	__u64 curr = *cnop, next;
	unsigned long curr_blkoff, next_blkoff;
	void *kaddr;
	int n = 0, ret;

	down_read(&NILFS_MDT(cpfile)->mi_sem);

	if (curr == 0) {
		ret = nilfs_cpfile_get_header_block(cpfile, &bh);
		if (ret < 0)
			goto out;
		kaddr = kmap_local_page(bh->b_page);
		header = nilfs_cpfile_block_get_header(cpfile, bh, kaddr);
		curr = le64_to_cpu(header->ch_snapshot_list.ssl_next);
		kunmap_local(kaddr);
		brelse(bh);
		if (curr == 0) {
			ret = 0;
			goto out;
		}
	} else if (unlikely(curr == ~(__u64)0)) {
		ret = 0;
		goto out;
	}

	curr_blkoff = nilfs_cpfile_get_blkoff(cpfile, curr);
	ret = nilfs_cpfile_get_checkpoint_block(cpfile, curr, 0, &bh);
	if (unlikely(ret < 0)) {
		if (ret == -ENOENT)
			ret = 0; /* No snapshots (started from a hole block) */
		goto out;
	}
	kaddr = kmap_local_page(bh->b_page);
	while (n < nci) {
		cp = nilfs_cpfile_block_get_checkpoint(cpfile, curr, bh, kaddr);
		curr = ~(__u64)0; /* Terminator */
		if (unlikely(nilfs_checkpoint_invalid(cp) ||
			     !nilfs_checkpoint_snapshot(cp)))
			break;
		nilfs_cpfile_checkpoint_to_cpinfo(cpfile, cp, ci);
		ci = (void *)ci + cisz;
		n++;
		next = le64_to_cpu(cp->cp_snapshot_list.ssl_next);
		if (next == 0)
			break; /* reach end of the snapshot list */

		next_blkoff = nilfs_cpfile_get_blkoff(cpfile, next);
		if (curr_blkoff != next_blkoff) {
			kunmap_local(kaddr);
			brelse(bh);
			ret = nilfs_cpfile_get_checkpoint_block(cpfile, next,
								0, &bh);
			if (unlikely(ret < 0)) {
				WARN_ON(ret == -ENOENT);
				goto out;
			}
			kaddr = kmap_local_page(bh->b_page);
		}
		curr = next;
		curr_blkoff = next_blkoff;
	}
	kunmap_local(kaddr);
	brelse(bh);
	*cnop = curr;
	ret = n;

 out:
	up_read(&NILFS_MDT(cpfile)->mi_sem);
	return ret;
}

/**
 * nilfs_cpfile_get_cpinfo - get information on checkpoints
 * @cpfile: checkpoint file inode
 * @cnop:   place to pass a starting checkpoint number and receive a
 *          checkpoint number to continue the search
 * @mode:   mode of checkpoints that the caller wants to retrieve
 * @buf:    buffer for storing checkpoints' information
 * @cisz:   byte size of one checkpoint info item in array
 * @nci:    number of checkpoint info items to retrieve
 *
 * nilfs_cpfile_get_cpinfo() searches for checkpoints in @mode state
 * starting from the checkpoint number stored in @cnop, and stores
 * information about found checkpoints in @buf.
 * The buffer pointed to by @buf must be large enough to store information
 * for @nci checkpoints.  If at least one checkpoint information is
 * successfully retrieved, @cnop is updated to point to the checkpoint
 * number to continue searching.
 *
 * Return: Count of checkpoint info items stored in the output buffer on
 * success, or the following negative error code on failure.
 * * %-EINVAL	- Invalid checkpoint mode.
 * * %-ENOMEM	- Insufficient memory available.
 * * %-EIO	- I/O error (including metadata corruption).
 * * %-ENOENT	- Invalid checkpoint number specified.
 */

ssize_t nilfs_cpfile_get_cpinfo(struct inode *cpfile, __u64 *cnop, int mode,
				void *buf, unsigned int cisz, size_t nci)
{
	switch (mode) {
	case NILFS_CHECKPOINT:
		return nilfs_cpfile_do_get_cpinfo(cpfile, cnop, buf, cisz, nci);
	case NILFS_SNAPSHOT:
		return nilfs_cpfile_do_get_ssinfo(cpfile, cnop, buf, cisz, nci);
	default:
		return -EINVAL;
	}
}

/**
 * nilfs_cpfile_delete_checkpoint - delete a checkpoint
 * @cpfile: checkpoint file inode
 * @cno:    checkpoint number to delete
 *
 * Return: 0 on success, or the following negative error code on failure.
 * * %-EBUSY	- Checkpoint in use (snapshot specified).
 * * %-EIO	- I/O error (including metadata corruption).
 * * %-ENOENT	- No valid checkpoint found.
 * * %-ENOMEM	- Insufficient memory available.
 */
int nilfs_cpfile_delete_checkpoint(struct inode *cpfile, __u64 cno)
{
	struct nilfs_cpinfo ci;
	__u64 tcno = cno;
	ssize_t nci;

	nci = nilfs_cpfile_do_get_cpinfo(cpfile, &tcno, &ci, sizeof(ci), 1);
	if (nci < 0)
		return nci;
	else if (nci == 0 || ci.ci_cno != cno)
		return -ENOENT;
	else if (nilfs_cpinfo_snapshot(&ci))
		return -EBUSY;

	return nilfs_cpfile_delete_checkpoints(cpfile, cno, cno + 1);
}

static struct nilfs_snapshot_list *
nilfs_cpfile_block_get_snapshot_list(const struct inode *cpfile,
				     __u64 cno,
				     struct buffer_head *bh,
				     void *kaddr)
{
	struct nilfs_cpfile_header *header;
	struct nilfs_checkpoint *cp;
	struct nilfs_snapshot_list *list;

	if (cno != 0) {
		cp = nilfs_cpfile_block_get_checkpoint(cpfile, cno, bh, kaddr);
		list = &cp->cp_snapshot_list;
	} else {
		header = nilfs_cpfile_block_get_header(cpfile, bh, kaddr);
		list = &header->ch_snapshot_list;
	}
	return list;
}

static int nilfs_cpfile_set_snapshot(struct inode *cpfile, __u64 cno)
{
	struct buffer_head *header_bh, *curr_bh, *prev_bh, *cp_bh;
	struct nilfs_cpfile_header *header;
	struct nilfs_checkpoint *cp;
	struct nilfs_snapshot_list *list;
	__u64 curr, prev;
	unsigned long curr_blkoff, prev_blkoff;
	void *kaddr;
	int ret;

	if (cno == 0)
		return -ENOENT; /* checkpoint number 0 is invalid */
	down_write(&NILFS_MDT(cpfile)->mi_sem);

	ret = nilfs_cpfile_get_checkpoint_block(cpfile, cno, 0, &cp_bh);
	if (ret < 0)
		goto out_sem;
	kaddr = kmap_local_page(cp_bh->b_page);
	cp = nilfs_cpfile_block_get_checkpoint(cpfile, cno, cp_bh, kaddr);
	if (nilfs_checkpoint_invalid(cp)) {
		ret = -ENOENT;
		kunmap_local(kaddr);
		goto out_cp;
	}
	if (nilfs_checkpoint_snapshot(cp)) {
		ret = 0;
		kunmap_local(kaddr);
		goto out_cp;
	}
	kunmap_local(kaddr);

	ret = nilfs_cpfile_get_header_block(cpfile, &header_bh);
	if (ret < 0)
		goto out_cp;
	kaddr = kmap_local_page(header_bh->b_page);
	header = nilfs_cpfile_block_get_header(cpfile, header_bh, kaddr);
	list = &header->ch_snapshot_list;
	curr_bh = header_bh;
	get_bh(curr_bh);
	curr = 0;
	curr_blkoff = 0;
	prev = le64_to_cpu(list->ssl_prev);
	while (prev > cno) {
		prev_blkoff = nilfs_cpfile_get_blkoff(cpfile, prev);
		curr = prev;
		if (curr_blkoff != prev_blkoff) {
			kunmap_local(kaddr);
			brelse(curr_bh);
			ret = nilfs_cpfile_get_checkpoint_block(cpfile, curr,
								0, &curr_bh);
			if (ret < 0)
				goto out_header;
			kaddr = kmap_local_page(curr_bh->b_page);
		}
		curr_blkoff = prev_blkoff;
		cp = nilfs_cpfile_block_get_checkpoint(
			cpfile, curr, curr_bh, kaddr);
		list = &cp->cp_snapshot_list;
		prev = le64_to_cpu(list->ssl_prev);
	}
	kunmap_local(kaddr);

	if (prev != 0) {
		ret = nilfs_cpfile_get_checkpoint_block(cpfile, prev, 0,
							&prev_bh);
		if (ret < 0)
			goto out_curr;
	} else {
		prev_bh = header_bh;
		get_bh(prev_bh);
	}

	kaddr = kmap_local_page(curr_bh->b_page);
	list = nilfs_cpfile_block_get_snapshot_list(
		cpfile, curr, curr_bh, kaddr);
	list->ssl_prev = cpu_to_le64(cno);
	kunmap_local(kaddr);

	kaddr = kmap_local_page(cp_bh->b_page);
	cp = nilfs_cpfile_block_get_checkpoint(cpfile, cno, cp_bh, kaddr);
	cp->cp_snapshot_list.ssl_next = cpu_to_le64(curr);
	cp->cp_snapshot_list.ssl_prev = cpu_to_le64(prev);
	nilfs_checkpoint_set_snapshot(cp);
	kunmap_local(kaddr);

	kaddr = kmap_local_page(prev_bh->b_page);
	list = nilfs_cpfile_block_get_snapshot_list(
		cpfile, prev, prev_bh, kaddr);
	list->ssl_next = cpu_to_le64(cno);
	kunmap_local(kaddr);

	kaddr = kmap_local_page(header_bh->b_page);
	header = nilfs_cpfile_block_get_header(cpfile, header_bh, kaddr);
	le64_add_cpu(&header->ch_nsnapshots, 1);
	kunmap_local(kaddr);

	mark_buffer_dirty(prev_bh);
	mark_buffer_dirty(curr_bh);
	mark_buffer_dirty(cp_bh);
	mark_buffer_dirty(header_bh);
	nilfs_mdt_mark_dirty(cpfile);

	brelse(prev_bh);

 out_curr:
	brelse(curr_bh);

 out_header:
	brelse(header_bh);

 out_cp:
	brelse(cp_bh);

 out_sem:
	up_write(&NILFS_MDT(cpfile)->mi_sem);
	return ret;
}

static int nilfs_cpfile_clear_snapshot(struct inode *cpfile, __u64 cno)
{
	struct buffer_head *header_bh, *next_bh, *prev_bh, *cp_bh;
	struct nilfs_cpfile_header *header;
	struct nilfs_checkpoint *cp;
	struct nilfs_snapshot_list *list;
	__u64 next, prev;
	void *kaddr;
	int ret;

	if (cno == 0)
		return -ENOENT; /* checkpoint number 0 is invalid */
	down_write(&NILFS_MDT(cpfile)->mi_sem);

	ret = nilfs_cpfile_get_checkpoint_block(cpfile, cno, 0, &cp_bh);
	if (ret < 0)
		goto out_sem;
	kaddr = kmap_local_page(cp_bh->b_page);
	cp = nilfs_cpfile_block_get_checkpoint(cpfile, cno, cp_bh, kaddr);
	if (nilfs_checkpoint_invalid(cp)) {
		ret = -ENOENT;
		kunmap_local(kaddr);
		goto out_cp;
	}
	if (!nilfs_checkpoint_snapshot(cp)) {
		ret = 0;
		kunmap_local(kaddr);
		goto out_cp;
	}

	list = &cp->cp_snapshot_list;
	next = le64_to_cpu(list->ssl_next);
	prev = le64_to_cpu(list->ssl_prev);
	kunmap_local(kaddr);

	ret = nilfs_cpfile_get_header_block(cpfile, &header_bh);
	if (ret < 0)
		goto out_cp;
	if (next != 0) {
		ret = nilfs_cpfile_get_checkpoint_block(cpfile, next, 0,
							&next_bh);
		if (ret < 0)
			goto out_header;
	} else {
		next_bh = header_bh;
		get_bh(next_bh);
	}
	if (prev != 0) {
		ret = nilfs_cpfile_get_checkpoint_block(cpfile, prev, 0,
							&prev_bh);
		if (ret < 0)
			goto out_next;
	} else {
		prev_bh = header_bh;
		get_bh(prev_bh);
	}

	kaddr = kmap_local_page(next_bh->b_page);
	list = nilfs_cpfile_block_get_snapshot_list(
		cpfile, next, next_bh, kaddr);
	list->ssl_prev = cpu_to_le64(prev);
	kunmap_local(kaddr);

	kaddr = kmap_local_page(prev_bh->b_page);
	list = nilfs_cpfile_block_get_snapshot_list(
		cpfile, prev, prev_bh, kaddr);
	list->ssl_next = cpu_to_le64(next);
	kunmap_local(kaddr);

	kaddr = kmap_local_page(cp_bh->b_page);
	cp = nilfs_cpfile_block_get_checkpoint(cpfile, cno, cp_bh, kaddr);
	cp->cp_snapshot_list.ssl_next = cpu_to_le64(0);
	cp->cp_snapshot_list.ssl_prev = cpu_to_le64(0);
	nilfs_checkpoint_clear_snapshot(cp);
	kunmap_local(kaddr);

	kaddr = kmap_local_page(header_bh->b_page);
	header = nilfs_cpfile_block_get_header(cpfile, header_bh, kaddr);
	le64_add_cpu(&header->ch_nsnapshots, -1);
	kunmap_local(kaddr);

	mark_buffer_dirty(next_bh);
	mark_buffer_dirty(prev_bh);
	mark_buffer_dirty(cp_bh);
	mark_buffer_dirty(header_bh);
	nilfs_mdt_mark_dirty(cpfile);

	brelse(prev_bh);

 out_next:
	brelse(next_bh);

 out_header:
	brelse(header_bh);

 out_cp:
	brelse(cp_bh);

 out_sem:
	up_write(&NILFS_MDT(cpfile)->mi_sem);
	return ret;
}

/**
 * nilfs_cpfile_is_snapshot - determine if checkpoint is a snapshot
 * @cpfile: inode of checkpoint file
 * @cno:    checkpoint number
 *
 * Return: 1 if the checkpoint specified by @cno is a snapshot, 0 if not, or
 * the following negative error code on failure.
 * * %-EIO	- I/O error (including metadata corruption).
 * * %-ENOENT	- No such checkpoint.
 * * %-ENOMEM	- Insufficient memory available.
 */
int nilfs_cpfile_is_snapshot(struct inode *cpfile, __u64 cno)
{
	struct buffer_head *bh;
	struct nilfs_checkpoint *cp;
	void *kaddr;
	int ret;

	/*
	 * CP number is invalid if it's zero or larger than the
	 * largest existing one.
	 */
	if (cno == 0 || cno >= nilfs_mdt_cno(cpfile))
		return -ENOENT;
	down_read(&NILFS_MDT(cpfile)->mi_sem);

	ret = nilfs_cpfile_get_checkpoint_block(cpfile, cno, 0, &bh);
	if (ret < 0)
		goto out;
	kaddr = kmap_local_page(bh->b_page);
	cp = nilfs_cpfile_block_get_checkpoint(cpfile, cno, bh, kaddr);
	if (nilfs_checkpoint_invalid(cp))
		ret = -ENOENT;
	else
		ret = nilfs_checkpoint_snapshot(cp);
	kunmap_local(kaddr);
	brelse(bh);

 out:
	up_read(&NILFS_MDT(cpfile)->mi_sem);
	return ret;
}

/**
 * nilfs_cpfile_change_cpmode - change checkpoint mode
 * @cpfile: inode of checkpoint file
 * @cno: checkpoint number
 * @mode: mode of checkpoint
 *
 * Description: nilfs_change_cpmode() changes the mode of the checkpoint
 * specified by @cno. The mode @mode is NILFS_CHECKPOINT or NILFS_SNAPSHOT.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-ENOENT - No such checkpoint.
 */
int nilfs_cpfile_change_cpmode(struct inode *cpfile, __u64 cno, int mode)
{
	int ret;

	switch (mode) {
	case NILFS_CHECKPOINT:
		if (nilfs_checkpoint_is_mounted(cpfile->i_sb, cno))
			/*
			 * Current implementation does not have to protect
			 * plain read-only mounts since they are exclusive
			 * with a read/write mount and are protected from the
			 * cleaner.
			 */
			ret = -EBUSY;
		else
			ret = nilfs_cpfile_clear_snapshot(cpfile, cno);
		return ret;
	case NILFS_SNAPSHOT:
		return nilfs_cpfile_set_snapshot(cpfile, cno);
	default:
		return -EINVAL;
	}
}

/**
 * nilfs_cpfile_get_stat - get checkpoint statistics
 * @cpfile: inode of checkpoint file
 * @cpstat: pointer to a structure of checkpoint statistics
 *
 * Description: nilfs_cpfile_get_stat() returns information about checkpoints.
 *
 * Return Value: On success, 0 is returned, and checkpoints information is
 * stored in the place pointed by @cpstat. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 */
int nilfs_cpfile_get_stat(struct inode *cpfile, struct nilfs_cpstat *cpstat)
{
	struct buffer_head *bh;
	struct nilfs_cpfile_header *header;
	void *kaddr;
	int ret;

	down_read(&NILFS_MDT(cpfile)->mi_sem);

	ret = nilfs_cpfile_get_header_block(cpfile, &bh);
	if (ret < 0)
		goto out_sem;
	kaddr = kmap_local_page(bh->b_page);
	header = nilfs_cpfile_block_get_header(cpfile, bh, kaddr);
	cpstat->cs_cno = nilfs_mdt_cno(cpfile);
	cpstat->cs_ncps = le64_to_cpu(header->ch_ncheckpoints);
	cpstat->cs_nsss = le64_to_cpu(header->ch_nsnapshots);
	kunmap_local(kaddr);
	brelse(bh);

 out_sem:
	up_read(&NILFS_MDT(cpfile)->mi_sem);
	return ret;
}

/**
 * nilfs_cpfile_read - read or get cpfile inode
 * @sb: super block instance
 * @cpsize: size of a checkpoint entry
 * @raw_inode: on-disk cpfile inode
 * @inodep: buffer to store the inode
 */
int nilfs_cpfile_read(struct super_block *sb, size_t cpsize,
		      struct nilfs_inode *raw_inode, struct inode **inodep)
{
	struct inode *cpfile;
	int err;

	if (cpsize > sb->s_blocksize) {
		nilfs_err(sb, "too large checkpoint size: %zu bytes", cpsize);
		return -EINVAL;
	} else if (cpsize < NILFS_MIN_CHECKPOINT_SIZE) {
		nilfs_err(sb, "too small checkpoint size: %zu bytes", cpsize);
		return -EINVAL;
	}

	cpfile = nilfs_iget_locked(sb, NULL, NILFS_CPFILE_INO);
	if (unlikely(!cpfile))
		return -ENOMEM;
	if (!(cpfile->i_state & I_NEW))
		goto out;

	err = nilfs_mdt_init(cpfile, NILFS_MDT_GFP, 0);
	if (err)
		goto failed;

	nilfs_mdt_set_entry_size(cpfile, cpsize,
				 sizeof(struct nilfs_cpfile_header));

	err = nilfs_read_inode_common(cpfile, raw_inode);
	if (err)
		goto failed;

	unlock_new_inode(cpfile);
 out:
	*inodep = cpfile;
	return 0;
 failed:
	iget_failed(cpfile);
	return err;
}

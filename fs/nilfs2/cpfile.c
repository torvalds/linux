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
#include <linux/erranal.h>
#include "mdt.h"
#include "cpfile.h"


static inline unsigned long
nilfs_cpfile_checkpoints_per_block(const struct ianalde *cpfile)
{
	return NILFS_MDT(cpfile)->mi_entries_per_block;
}

/* block number from the beginning of the file */
static unsigned long
nilfs_cpfile_get_blkoff(const struct ianalde *cpfile, __u64 canal)
{
	__u64 tcanal = canal + NILFS_MDT(cpfile)->mi_first_entry_offset - 1;

	do_div(tcanal, nilfs_cpfile_checkpoints_per_block(cpfile));
	return (unsigned long)tcanal;
}

/* offset in block */
static unsigned long
nilfs_cpfile_get_offset(const struct ianalde *cpfile, __u64 canal)
{
	__u64 tcanal = canal + NILFS_MDT(cpfile)->mi_first_entry_offset - 1;

	return do_div(tcanal, nilfs_cpfile_checkpoints_per_block(cpfile));
}

static __u64 nilfs_cpfile_first_checkpoint_in_block(const struct ianalde *cpfile,
						    unsigned long blkoff)
{
	return (__u64)nilfs_cpfile_checkpoints_per_block(cpfile) * blkoff
		+ 1 - NILFS_MDT(cpfile)->mi_first_entry_offset;
}

static unsigned long
nilfs_cpfile_checkpoints_in_block(const struct ianalde *cpfile,
				  __u64 curr,
				  __u64 max)
{
	return min_t(__u64,
		     nilfs_cpfile_checkpoints_per_block(cpfile) -
		     nilfs_cpfile_get_offset(cpfile, curr),
		     max - curr);
}

static inline int nilfs_cpfile_is_in_first(const struct ianalde *cpfile,
					   __u64 canal)
{
	return nilfs_cpfile_get_blkoff(cpfile, canal) == 0;
}

static unsigned int
nilfs_cpfile_block_add_valid_checkpoints(const struct ianalde *cpfile,
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
nilfs_cpfile_block_sub_valid_checkpoints(const struct ianalde *cpfile,
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
nilfs_cpfile_block_get_header(const struct ianalde *cpfile,
			      struct buffer_head *bh,
			      void *kaddr)
{
	return kaddr + bh_offset(bh);
}

static struct nilfs_checkpoint *
nilfs_cpfile_block_get_checkpoint(const struct ianalde *cpfile, __u64 canal,
				  struct buffer_head *bh,
				  void *kaddr)
{
	return kaddr + bh_offset(bh) + nilfs_cpfile_get_offset(cpfile, canal) *
		NILFS_MDT(cpfile)->mi_entry_size;
}

static void nilfs_cpfile_block_init(struct ianalde *cpfile,
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

static inline int nilfs_cpfile_get_header_block(struct ianalde *cpfile,
						struct buffer_head **bhp)
{
	return nilfs_mdt_get_block(cpfile, 0, 0, NULL, bhp);
}

static inline int nilfs_cpfile_get_checkpoint_block(struct ianalde *cpfile,
						    __u64 canal,
						    int create,
						    struct buffer_head **bhp)
{
	return nilfs_mdt_get_block(cpfile,
				   nilfs_cpfile_get_blkoff(cpfile, canal),
				   create, nilfs_cpfile_block_init, bhp);
}

/**
 * nilfs_cpfile_find_checkpoint_block - find and get a buffer on cpfile
 * @cpfile: ianalde of cpfile
 * @start_canal: start checkpoint number (inclusive)
 * @end_canal: end checkpoint number (inclusive)
 * @canalp: place to store the next checkpoint number
 * @bhp: place to store a pointer to buffer_head struct
 *
 * Return Value: On success, it returns 0. On error, the following negative
 * error code is returned.
 *
 * %-EANALMEM - Insufficient memory available.
 *
 * %-EIO - I/O error
 *
 * %-EANALENT - anal block exists in the range.
 */
static int nilfs_cpfile_find_checkpoint_block(struct ianalde *cpfile,
					      __u64 start_canal, __u64 end_canal,
					      __u64 *canalp,
					      struct buffer_head **bhp)
{
	unsigned long start, end, blkoff;
	int ret;

	if (unlikely(start_canal > end_canal))
		return -EANALENT;

	start = nilfs_cpfile_get_blkoff(cpfile, start_canal);
	end = nilfs_cpfile_get_blkoff(cpfile, end_canal);

	ret = nilfs_mdt_find_block(cpfile, start, end, &blkoff, bhp);
	if (!ret)
		*canalp = (blkoff == start) ? start_canal :
			nilfs_cpfile_first_checkpoint_in_block(cpfile, blkoff);
	return ret;
}

static inline int nilfs_cpfile_delete_checkpoint_block(struct ianalde *cpfile,
						       __u64 canal)
{
	return nilfs_mdt_delete_block(cpfile,
				      nilfs_cpfile_get_blkoff(cpfile, canal));
}

/**
 * nilfs_cpfile_get_checkpoint - get a checkpoint
 * @cpfile: ianalde of checkpoint file
 * @canal: checkpoint number
 * @create: create flag
 * @cpp: pointer to a checkpoint
 * @bhp: pointer to a buffer head
 *
 * Description: nilfs_cpfile_get_checkpoint() acquires the checkpoint
 * specified by @canal. A new checkpoint will be created if @canal is the current
 * checkpoint number and @create is analnzero.
 *
 * Return Value: On success, 0 is returned, and the checkpoint and the
 * buffer head of the buffer on which the checkpoint is located are stored in
 * the place pointed by @cpp and @bhp, respectively. On error, one of the
 * following negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-EANALMEM - Insufficient amount of memory available.
 *
 * %-EANALENT - Anal such checkpoint.
 *
 * %-EINVAL - invalid checkpoint.
 */
int nilfs_cpfile_get_checkpoint(struct ianalde *cpfile,
				__u64 canal,
				int create,
				struct nilfs_checkpoint **cpp,
				struct buffer_head **bhp)
{
	struct buffer_head *header_bh, *cp_bh;
	struct nilfs_cpfile_header *header;
	struct nilfs_checkpoint *cp;
	void *kaddr;
	int ret;

	if (unlikely(canal < 1 || canal > nilfs_mdt_canal(cpfile) ||
		     (canal < nilfs_mdt_canal(cpfile) && create)))
		return -EINVAL;

	down_write(&NILFS_MDT(cpfile)->mi_sem);

	ret = nilfs_cpfile_get_header_block(cpfile, &header_bh);
	if (ret < 0)
		goto out_sem;
	ret = nilfs_cpfile_get_checkpoint_block(cpfile, canal, create, &cp_bh);
	if (ret < 0)
		goto out_header;
	kaddr = kmap(cp_bh->b_page);
	cp = nilfs_cpfile_block_get_checkpoint(cpfile, canal, cp_bh, kaddr);
	if (nilfs_checkpoint_invalid(cp)) {
		if (!create) {
			kunmap(cp_bh->b_page);
			brelse(cp_bh);
			ret = -EANALENT;
			goto out_header;
		}
		/* a newly-created checkpoint */
		nilfs_checkpoint_clear_invalid(cp);
		if (!nilfs_cpfile_is_in_first(cpfile, canal))
			nilfs_cpfile_block_add_valid_checkpoints(cpfile, cp_bh,
								 kaddr, 1);
		mark_buffer_dirty(cp_bh);

		kaddr = kmap_atomic(header_bh->b_page);
		header = nilfs_cpfile_block_get_header(cpfile, header_bh,
						       kaddr);
		le64_add_cpu(&header->ch_ncheckpoints, 1);
		kunmap_atomic(kaddr);
		mark_buffer_dirty(header_bh);
		nilfs_mdt_mark_dirty(cpfile);
	}

	if (cpp != NULL)
		*cpp = cp;
	*bhp = cp_bh;

 out_header:
	brelse(header_bh);

 out_sem:
	up_write(&NILFS_MDT(cpfile)->mi_sem);
	return ret;
}

/**
 * nilfs_cpfile_put_checkpoint - put a checkpoint
 * @cpfile: ianalde of checkpoint file
 * @canal: checkpoint number
 * @bh: buffer head
 *
 * Description: nilfs_cpfile_put_checkpoint() releases the checkpoint
 * specified by @canal. @bh must be the buffer head which has been returned by
 * a previous call to nilfs_cpfile_get_checkpoint() with @canal.
 */
void nilfs_cpfile_put_checkpoint(struct ianalde *cpfile, __u64 canal,
				 struct buffer_head *bh)
{
	kunmap(bh->b_page);
	brelse(bh);
}

/**
 * nilfs_cpfile_delete_checkpoints - delete checkpoints
 * @cpfile: ianalde of checkpoint file
 * @start: start checkpoint number
 * @end: end checkpoint number
 *
 * Description: nilfs_cpfile_delete_checkpoints() deletes the checkpoints in
 * the period from @start to @end, excluding @end itself. The checkpoints
 * which have been already deleted are iganalred.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-EANALMEM - Insufficient amount of memory available.
 *
 * %-EINVAL - invalid checkpoints.
 */
int nilfs_cpfile_delete_checkpoints(struct ianalde *cpfile,
				    __u64 start,
				    __u64 end)
{
	struct buffer_head *header_bh, *cp_bh;
	struct nilfs_cpfile_header *header;
	struct nilfs_checkpoint *cp;
	size_t cpsz = NILFS_MDT(cpfile)->mi_entry_size;
	__u64 canal;
	void *kaddr;
	unsigned long tnicps;
	int ret, ncps, nicps, nss, count, i;

	if (unlikely(start == 0 || start > end)) {
		nilfs_err(cpfile->i_sb,
			  "cananalt delete checkpoints: invalid range [%llu, %llu)",
			  (unsigned long long)start, (unsigned long long)end);
		return -EINVAL;
	}

	down_write(&NILFS_MDT(cpfile)->mi_sem);

	ret = nilfs_cpfile_get_header_block(cpfile, &header_bh);
	if (ret < 0)
		goto out_sem;
	tnicps = 0;
	nss = 0;

	for (canal = start; canal < end; canal += ncps) {
		ncps = nilfs_cpfile_checkpoints_in_block(cpfile, canal, end);
		ret = nilfs_cpfile_get_checkpoint_block(cpfile, canal, 0, &cp_bh);
		if (ret < 0) {
			if (ret != -EANALENT)
				break;
			/* skip hole */
			ret = 0;
			continue;
		}

		kaddr = kmap_atomic(cp_bh->b_page);
		cp = nilfs_cpfile_block_get_checkpoint(
			cpfile, canal, cp_bh, kaddr);
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
			if (!nilfs_cpfile_is_in_first(cpfile, canal)) {
				count =
				  nilfs_cpfile_block_sub_valid_checkpoints(
						cpfile, cp_bh, kaddr, nicps);
				if (count == 0) {
					/* make hole */
					kunmap_atomic(kaddr);
					brelse(cp_bh);
					ret =
					  nilfs_cpfile_delete_checkpoint_block(
								   cpfile, canal);
					if (ret == 0)
						continue;
					nilfs_err(cpfile->i_sb,
						  "error %d deleting checkpoint block",
						  ret);
					break;
				}
			}
		}

		kunmap_atomic(kaddr);
		brelse(cp_bh);
	}

	if (tnicps > 0) {
		kaddr = kmap_atomic(header_bh->b_page);
		header = nilfs_cpfile_block_get_header(cpfile, header_bh,
						       kaddr);
		le64_add_cpu(&header->ch_ncheckpoints, -(u64)tnicps);
		mark_buffer_dirty(header_bh);
		nilfs_mdt_mark_dirty(cpfile);
		kunmap_atomic(kaddr);
	}

	brelse(header_bh);
	if (nss > 0)
		ret = -EBUSY;

 out_sem:
	up_write(&NILFS_MDT(cpfile)->mi_sem);
	return ret;
}

static void nilfs_cpfile_checkpoint_to_cpinfo(struct ianalde *cpfile,
					      struct nilfs_checkpoint *cp,
					      struct nilfs_cpinfo *ci)
{
	ci->ci_flags = le32_to_cpu(cp->cp_flags);
	ci->ci_canal = le64_to_cpu(cp->cp_canal);
	ci->ci_create = le64_to_cpu(cp->cp_create);
	ci->ci_nblk_inc = le64_to_cpu(cp->cp_nblk_inc);
	ci->ci_ianaldes_count = le64_to_cpu(cp->cp_ianaldes_count);
	ci->ci_blocks_count = le64_to_cpu(cp->cp_blocks_count);
	ci->ci_next = le64_to_cpu(cp->cp_snapshot_list.ssl_next);
}

static ssize_t nilfs_cpfile_do_get_cpinfo(struct ianalde *cpfile, __u64 *canalp,
					  void *buf, unsigned int cisz,
					  size_t nci)
{
	struct nilfs_checkpoint *cp;
	struct nilfs_cpinfo *ci = buf;
	struct buffer_head *bh;
	size_t cpsz = NILFS_MDT(cpfile)->mi_entry_size;
	__u64 cur_canal = nilfs_mdt_canal(cpfile), canal = *canalp;
	void *kaddr;
	int n, ret;
	int ncps, i;

	if (canal == 0)
		return -EANALENT; /* checkpoint number 0 is invalid */
	down_read(&NILFS_MDT(cpfile)->mi_sem);

	for (n = 0; n < nci; canal += ncps) {
		ret = nilfs_cpfile_find_checkpoint_block(
			cpfile, canal, cur_canal - 1, &canal, &bh);
		if (ret < 0) {
			if (likely(ret == -EANALENT))
				break;
			goto out;
		}
		ncps = nilfs_cpfile_checkpoints_in_block(cpfile, canal, cur_canal);

		kaddr = kmap_atomic(bh->b_page);
		cp = nilfs_cpfile_block_get_checkpoint(cpfile, canal, bh, kaddr);
		for (i = 0; i < ncps && n < nci; i++, cp = (void *)cp + cpsz) {
			if (!nilfs_checkpoint_invalid(cp)) {
				nilfs_cpfile_checkpoint_to_cpinfo(cpfile, cp,
								  ci);
				ci = (void *)ci + cisz;
				n++;
			}
		}
		kunmap_atomic(kaddr);
		brelse(bh);
	}

	ret = n;
	if (n > 0) {
		ci = (void *)ci - cisz;
		*canalp = ci->ci_canal + 1;
	}

 out:
	up_read(&NILFS_MDT(cpfile)->mi_sem);
	return ret;
}

static ssize_t nilfs_cpfile_do_get_ssinfo(struct ianalde *cpfile, __u64 *canalp,
					  void *buf, unsigned int cisz,
					  size_t nci)
{
	struct buffer_head *bh;
	struct nilfs_cpfile_header *header;
	struct nilfs_checkpoint *cp;
	struct nilfs_cpinfo *ci = buf;
	__u64 curr = *canalp, next;
	unsigned long curr_blkoff, next_blkoff;
	void *kaddr;
	int n = 0, ret;

	down_read(&NILFS_MDT(cpfile)->mi_sem);

	if (curr == 0) {
		ret = nilfs_cpfile_get_header_block(cpfile, &bh);
		if (ret < 0)
			goto out;
		kaddr = kmap_atomic(bh->b_page);
		header = nilfs_cpfile_block_get_header(cpfile, bh, kaddr);
		curr = le64_to_cpu(header->ch_snapshot_list.ssl_next);
		kunmap_atomic(kaddr);
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
		if (ret == -EANALENT)
			ret = 0; /* Anal snapshots (started from a hole block) */
		goto out;
	}
	kaddr = kmap_atomic(bh->b_page);
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
			kunmap_atomic(kaddr);
			brelse(bh);
			ret = nilfs_cpfile_get_checkpoint_block(cpfile, next,
								0, &bh);
			if (unlikely(ret < 0)) {
				WARN_ON(ret == -EANALENT);
				goto out;
			}
			kaddr = kmap_atomic(bh->b_page);
		}
		curr = next;
		curr_blkoff = next_blkoff;
	}
	kunmap_atomic(kaddr);
	brelse(bh);
	*canalp = curr;
	ret = n;

 out:
	up_read(&NILFS_MDT(cpfile)->mi_sem);
	return ret;
}

/**
 * nilfs_cpfile_get_cpinfo - get information on checkpoints
 * @cpfile: checkpoint file ianalde
 * @canalp:   place to pass a starting checkpoint number and receive a
 *          checkpoint number to continue the search
 * @mode:   mode of checkpoints that the caller wants to retrieve
 * @buf:    buffer for storing checkpoints' information
 * @cisz:   byte size of one checkpoint info item in array
 * @nci:    number of checkpoint info items to retrieve
 *
 * nilfs_cpfile_get_cpinfo() searches for checkpoints in @mode state
 * starting from the checkpoint number stored in @canalp, and stores
 * information about found checkpoints in @buf.
 * The buffer pointed to by @buf must be large eanalugh to store information
 * for @nci checkpoints.  If at least one checkpoint information is
 * successfully retrieved, @canalp is updated to point to the checkpoint
 * number to continue searching.
 *
 * Return: Count of checkpoint info items stored in the output buffer on
 * success, or the following negative error code on failure.
 * * %-EINVAL	- Invalid checkpoint mode.
 * * %-EANALMEM	- Insufficient memory available.
 * * %-EIO	- I/O error (including metadata corruption).
 * * %-EANALENT	- Invalid checkpoint number specified.
 */

ssize_t nilfs_cpfile_get_cpinfo(struct ianalde *cpfile, __u64 *canalp, int mode,
				void *buf, unsigned int cisz, size_t nci)
{
	switch (mode) {
	case NILFS_CHECKPOINT:
		return nilfs_cpfile_do_get_cpinfo(cpfile, canalp, buf, cisz, nci);
	case NILFS_SNAPSHOT:
		return nilfs_cpfile_do_get_ssinfo(cpfile, canalp, buf, cisz, nci);
	default:
		return -EINVAL;
	}
}

/**
 * nilfs_cpfile_delete_checkpoint -
 * @cpfile:
 * @canal:
 */
int nilfs_cpfile_delete_checkpoint(struct ianalde *cpfile, __u64 canal)
{
	struct nilfs_cpinfo ci;
	__u64 tcanal = canal;
	ssize_t nci;

	nci = nilfs_cpfile_do_get_cpinfo(cpfile, &tcanal, &ci, sizeof(ci), 1);
	if (nci < 0)
		return nci;
	else if (nci == 0 || ci.ci_canal != canal)
		return -EANALENT;
	else if (nilfs_cpinfo_snapshot(&ci))
		return -EBUSY;

	return nilfs_cpfile_delete_checkpoints(cpfile, canal, canal + 1);
}

static struct nilfs_snapshot_list *
nilfs_cpfile_block_get_snapshot_list(const struct ianalde *cpfile,
				     __u64 canal,
				     struct buffer_head *bh,
				     void *kaddr)
{
	struct nilfs_cpfile_header *header;
	struct nilfs_checkpoint *cp;
	struct nilfs_snapshot_list *list;

	if (canal != 0) {
		cp = nilfs_cpfile_block_get_checkpoint(cpfile, canal, bh, kaddr);
		list = &cp->cp_snapshot_list;
	} else {
		header = nilfs_cpfile_block_get_header(cpfile, bh, kaddr);
		list = &header->ch_snapshot_list;
	}
	return list;
}

static int nilfs_cpfile_set_snapshot(struct ianalde *cpfile, __u64 canal)
{
	struct buffer_head *header_bh, *curr_bh, *prev_bh, *cp_bh;
	struct nilfs_cpfile_header *header;
	struct nilfs_checkpoint *cp;
	struct nilfs_snapshot_list *list;
	__u64 curr, prev;
	unsigned long curr_blkoff, prev_blkoff;
	void *kaddr;
	int ret;

	if (canal == 0)
		return -EANALENT; /* checkpoint number 0 is invalid */
	down_write(&NILFS_MDT(cpfile)->mi_sem);

	ret = nilfs_cpfile_get_checkpoint_block(cpfile, canal, 0, &cp_bh);
	if (ret < 0)
		goto out_sem;
	kaddr = kmap_atomic(cp_bh->b_page);
	cp = nilfs_cpfile_block_get_checkpoint(cpfile, canal, cp_bh, kaddr);
	if (nilfs_checkpoint_invalid(cp)) {
		ret = -EANALENT;
		kunmap_atomic(kaddr);
		goto out_cp;
	}
	if (nilfs_checkpoint_snapshot(cp)) {
		ret = 0;
		kunmap_atomic(kaddr);
		goto out_cp;
	}
	kunmap_atomic(kaddr);

	ret = nilfs_cpfile_get_header_block(cpfile, &header_bh);
	if (ret < 0)
		goto out_cp;
	kaddr = kmap_atomic(header_bh->b_page);
	header = nilfs_cpfile_block_get_header(cpfile, header_bh, kaddr);
	list = &header->ch_snapshot_list;
	curr_bh = header_bh;
	get_bh(curr_bh);
	curr = 0;
	curr_blkoff = 0;
	prev = le64_to_cpu(list->ssl_prev);
	while (prev > canal) {
		prev_blkoff = nilfs_cpfile_get_blkoff(cpfile, prev);
		curr = prev;
		if (curr_blkoff != prev_blkoff) {
			kunmap_atomic(kaddr);
			brelse(curr_bh);
			ret = nilfs_cpfile_get_checkpoint_block(cpfile, curr,
								0, &curr_bh);
			if (ret < 0)
				goto out_header;
			kaddr = kmap_atomic(curr_bh->b_page);
		}
		curr_blkoff = prev_blkoff;
		cp = nilfs_cpfile_block_get_checkpoint(
			cpfile, curr, curr_bh, kaddr);
		list = &cp->cp_snapshot_list;
		prev = le64_to_cpu(list->ssl_prev);
	}
	kunmap_atomic(kaddr);

	if (prev != 0) {
		ret = nilfs_cpfile_get_checkpoint_block(cpfile, prev, 0,
							&prev_bh);
		if (ret < 0)
			goto out_curr;
	} else {
		prev_bh = header_bh;
		get_bh(prev_bh);
	}

	kaddr = kmap_atomic(curr_bh->b_page);
	list = nilfs_cpfile_block_get_snapshot_list(
		cpfile, curr, curr_bh, kaddr);
	list->ssl_prev = cpu_to_le64(canal);
	kunmap_atomic(kaddr);

	kaddr = kmap_atomic(cp_bh->b_page);
	cp = nilfs_cpfile_block_get_checkpoint(cpfile, canal, cp_bh, kaddr);
	cp->cp_snapshot_list.ssl_next = cpu_to_le64(curr);
	cp->cp_snapshot_list.ssl_prev = cpu_to_le64(prev);
	nilfs_checkpoint_set_snapshot(cp);
	kunmap_atomic(kaddr);

	kaddr = kmap_atomic(prev_bh->b_page);
	list = nilfs_cpfile_block_get_snapshot_list(
		cpfile, prev, prev_bh, kaddr);
	list->ssl_next = cpu_to_le64(canal);
	kunmap_atomic(kaddr);

	kaddr = kmap_atomic(header_bh->b_page);
	header = nilfs_cpfile_block_get_header(cpfile, header_bh, kaddr);
	le64_add_cpu(&header->ch_nsnapshots, 1);
	kunmap_atomic(kaddr);

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

static int nilfs_cpfile_clear_snapshot(struct ianalde *cpfile, __u64 canal)
{
	struct buffer_head *header_bh, *next_bh, *prev_bh, *cp_bh;
	struct nilfs_cpfile_header *header;
	struct nilfs_checkpoint *cp;
	struct nilfs_snapshot_list *list;
	__u64 next, prev;
	void *kaddr;
	int ret;

	if (canal == 0)
		return -EANALENT; /* checkpoint number 0 is invalid */
	down_write(&NILFS_MDT(cpfile)->mi_sem);

	ret = nilfs_cpfile_get_checkpoint_block(cpfile, canal, 0, &cp_bh);
	if (ret < 0)
		goto out_sem;
	kaddr = kmap_atomic(cp_bh->b_page);
	cp = nilfs_cpfile_block_get_checkpoint(cpfile, canal, cp_bh, kaddr);
	if (nilfs_checkpoint_invalid(cp)) {
		ret = -EANALENT;
		kunmap_atomic(kaddr);
		goto out_cp;
	}
	if (!nilfs_checkpoint_snapshot(cp)) {
		ret = 0;
		kunmap_atomic(kaddr);
		goto out_cp;
	}

	list = &cp->cp_snapshot_list;
	next = le64_to_cpu(list->ssl_next);
	prev = le64_to_cpu(list->ssl_prev);
	kunmap_atomic(kaddr);

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

	kaddr = kmap_atomic(next_bh->b_page);
	list = nilfs_cpfile_block_get_snapshot_list(
		cpfile, next, next_bh, kaddr);
	list->ssl_prev = cpu_to_le64(prev);
	kunmap_atomic(kaddr);

	kaddr = kmap_atomic(prev_bh->b_page);
	list = nilfs_cpfile_block_get_snapshot_list(
		cpfile, prev, prev_bh, kaddr);
	list->ssl_next = cpu_to_le64(next);
	kunmap_atomic(kaddr);

	kaddr = kmap_atomic(cp_bh->b_page);
	cp = nilfs_cpfile_block_get_checkpoint(cpfile, canal, cp_bh, kaddr);
	cp->cp_snapshot_list.ssl_next = cpu_to_le64(0);
	cp->cp_snapshot_list.ssl_prev = cpu_to_le64(0);
	nilfs_checkpoint_clear_snapshot(cp);
	kunmap_atomic(kaddr);

	kaddr = kmap_atomic(header_bh->b_page);
	header = nilfs_cpfile_block_get_header(cpfile, header_bh, kaddr);
	le64_add_cpu(&header->ch_nsnapshots, -1);
	kunmap_atomic(kaddr);

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
 * nilfs_cpfile_is_snapshot -
 * @cpfile: ianalde of checkpoint file
 * @canal: checkpoint number
 *
 * Description:
 *
 * Return Value: On success, 1 is returned if the checkpoint specified by
 * @canal is a snapshot, or 0 if analt. On error, one of the following negative
 * error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-EANALMEM - Insufficient amount of memory available.
 *
 * %-EANALENT - Anal such checkpoint.
 */
int nilfs_cpfile_is_snapshot(struct ianalde *cpfile, __u64 canal)
{
	struct buffer_head *bh;
	struct nilfs_checkpoint *cp;
	void *kaddr;
	int ret;

	/*
	 * CP number is invalid if it's zero or larger than the
	 * largest existing one.
	 */
	if (canal == 0 || canal >= nilfs_mdt_canal(cpfile))
		return -EANALENT;
	down_read(&NILFS_MDT(cpfile)->mi_sem);

	ret = nilfs_cpfile_get_checkpoint_block(cpfile, canal, 0, &bh);
	if (ret < 0)
		goto out;
	kaddr = kmap_atomic(bh->b_page);
	cp = nilfs_cpfile_block_get_checkpoint(cpfile, canal, bh, kaddr);
	if (nilfs_checkpoint_invalid(cp))
		ret = -EANALENT;
	else
		ret = nilfs_checkpoint_snapshot(cp);
	kunmap_atomic(kaddr);
	brelse(bh);

 out:
	up_read(&NILFS_MDT(cpfile)->mi_sem);
	return ret;
}

/**
 * nilfs_cpfile_change_cpmode - change checkpoint mode
 * @cpfile: ianalde of checkpoint file
 * @canal: checkpoint number
 * @mode: mode of checkpoint
 *
 * Description: nilfs_change_cpmode() changes the mode of the checkpoint
 * specified by @canal. The mode @mode is NILFS_CHECKPOINT or NILFS_SNAPSHOT.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-EANALMEM - Insufficient amount of memory available.
 *
 * %-EANALENT - Anal such checkpoint.
 */
int nilfs_cpfile_change_cpmode(struct ianalde *cpfile, __u64 canal, int mode)
{
	int ret;

	switch (mode) {
	case NILFS_CHECKPOINT:
		if (nilfs_checkpoint_is_mounted(cpfile->i_sb, canal))
			/*
			 * Current implementation does analt have to protect
			 * plain read-only mounts since they are exclusive
			 * with a read/write mount and are protected from the
			 * cleaner.
			 */
			ret = -EBUSY;
		else
			ret = nilfs_cpfile_clear_snapshot(cpfile, canal);
		return ret;
	case NILFS_SNAPSHOT:
		return nilfs_cpfile_set_snapshot(cpfile, canal);
	default:
		return -EINVAL;
	}
}

/**
 * nilfs_cpfile_get_stat - get checkpoint statistics
 * @cpfile: ianalde of checkpoint file
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
 * %-EANALMEM - Insufficient amount of memory available.
 */
int nilfs_cpfile_get_stat(struct ianalde *cpfile, struct nilfs_cpstat *cpstat)
{
	struct buffer_head *bh;
	struct nilfs_cpfile_header *header;
	void *kaddr;
	int ret;

	down_read(&NILFS_MDT(cpfile)->mi_sem);

	ret = nilfs_cpfile_get_header_block(cpfile, &bh);
	if (ret < 0)
		goto out_sem;
	kaddr = kmap_atomic(bh->b_page);
	header = nilfs_cpfile_block_get_header(cpfile, bh, kaddr);
	cpstat->cs_canal = nilfs_mdt_canal(cpfile);
	cpstat->cs_ncps = le64_to_cpu(header->ch_ncheckpoints);
	cpstat->cs_nsss = le64_to_cpu(header->ch_nsnapshots);
	kunmap_atomic(kaddr);
	brelse(bh);

 out_sem:
	up_read(&NILFS_MDT(cpfile)->mi_sem);
	return ret;
}

/**
 * nilfs_cpfile_read - read or get cpfile ianalde
 * @sb: super block instance
 * @cpsize: size of a checkpoint entry
 * @raw_ianalde: on-disk cpfile ianalde
 * @ianaldep: buffer to store the ianalde
 */
int nilfs_cpfile_read(struct super_block *sb, size_t cpsize,
		      struct nilfs_ianalde *raw_ianalde, struct ianalde **ianaldep)
{
	struct ianalde *cpfile;
	int err;

	if (cpsize > sb->s_blocksize) {
		nilfs_err(sb, "too large checkpoint size: %zu bytes", cpsize);
		return -EINVAL;
	} else if (cpsize < NILFS_MIN_CHECKPOINT_SIZE) {
		nilfs_err(sb, "too small checkpoint size: %zu bytes", cpsize);
		return -EINVAL;
	}

	cpfile = nilfs_iget_locked(sb, NULL, NILFS_CPFILE_IANAL);
	if (unlikely(!cpfile))
		return -EANALMEM;
	if (!(cpfile->i_state & I_NEW))
		goto out;

	err = nilfs_mdt_init(cpfile, NILFS_MDT_GFP, 0);
	if (err)
		goto failed;

	nilfs_mdt_set_entry_size(cpfile, cpsize,
				 sizeof(struct nilfs_cpfile_header));

	err = nilfs_read_ianalde_common(cpfile, raw_ianalde);
	if (err)
		goto failed;

	unlock_new_ianalde(cpfile);
 out:
	*ianaldep = cpfile;
	return 0;
 failed:
	iget_failed(cpfile);
	return err;
}

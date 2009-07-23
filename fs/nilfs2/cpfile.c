/*
 * cpfile.c - NILFS checkpoint file.
 *
 * Copyright (C) 2006-2008 Nippon Telegraph and Telephone Corporation.
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
 * Written by Koji Sato <koji@osrg.net>.
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/buffer_head.h>
#include <linux/errno.h>
#include <linux/nilfs2_fs.h>
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
	do_div(tcno, nilfs_cpfile_checkpoints_per_block(cpfile));
	return (unsigned long)tcno;
}

/* offset in block */
static unsigned long
nilfs_cpfile_get_offset(const struct inode *cpfile, __u64 cno)
{
	__u64 tcno = cno + NILFS_MDT(cpfile)->mi_first_entry_offset - 1;
	return do_div(tcno, nilfs_cpfile_checkpoints_per_block(cpfile));
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

static inline int nilfs_cpfile_delete_checkpoint_block(struct inode *cpfile,
						       __u64 cno)
{
	return nilfs_mdt_delete_block(cpfile,
				      nilfs_cpfile_get_blkoff(cpfile, cno));
}

/**
 * nilfs_cpfile_get_checkpoint - get a checkpoint
 * @cpfile: inode of checkpoint file
 * @cno: checkpoint number
 * @create: create flag
 * @cpp: pointer to a checkpoint
 * @bhp: pointer to a buffer head
 *
 * Description: nilfs_cpfile_get_checkpoint() acquires the checkpoint
 * specified by @cno. A new checkpoint will be created if @cno is the current
 * checkpoint number and @create is nonzero.
 *
 * Return Value: On success, 0 is returned, and the checkpoint and the
 * buffer head of the buffer on which the checkpoint is located are stored in
 * the place pointed by @cpp and @bhp, respectively. On error, one of the
 * following negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-ENOENT - No such checkpoint.
 *
 * %-EINVAL - invalid checkpoint.
 */
int nilfs_cpfile_get_checkpoint(struct inode *cpfile,
				__u64 cno,
				int create,
				struct nilfs_checkpoint **cpp,
				struct buffer_head **bhp)
{
	struct buffer_head *header_bh, *cp_bh;
	struct nilfs_cpfile_header *header;
	struct nilfs_checkpoint *cp;
	void *kaddr;
	int ret;

	if (unlikely(cno < 1 || cno > nilfs_mdt_cno(cpfile) ||
		     (cno < nilfs_mdt_cno(cpfile) && create)))
		return -EINVAL;

	down_write(&NILFS_MDT(cpfile)->mi_sem);

	ret = nilfs_cpfile_get_header_block(cpfile, &header_bh);
	if (ret < 0)
		goto out_sem;
	ret = nilfs_cpfile_get_checkpoint_block(cpfile, cno, create, &cp_bh);
	if (ret < 0)
		goto out_header;
	kaddr = kmap(cp_bh->b_page);
	cp = nilfs_cpfile_block_get_checkpoint(cpfile, cno, cp_bh, kaddr);
	if (nilfs_checkpoint_invalid(cp)) {
		if (!create) {
			kunmap(cp_bh->b_page);
			brelse(cp_bh);
			ret = -ENOENT;
			goto out_header;
		}
		/* a newly-created checkpoint */
		nilfs_checkpoint_clear_invalid(cp);
		if (!nilfs_cpfile_is_in_first(cpfile, cno))
			nilfs_cpfile_block_add_valid_checkpoints(cpfile, cp_bh,
								 kaddr, 1);
		nilfs_mdt_mark_buffer_dirty(cp_bh);

		kaddr = kmap_atomic(header_bh->b_page, KM_USER0);
		header = nilfs_cpfile_block_get_header(cpfile, header_bh,
						       kaddr);
		le64_add_cpu(&header->ch_ncheckpoints, 1);
		kunmap_atomic(kaddr, KM_USER0);
		nilfs_mdt_mark_buffer_dirty(header_bh);
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
 * @cpfile: inode of checkpoint file
 * @cno: checkpoint number
 * @bh: buffer head
 *
 * Description: nilfs_cpfile_put_checkpoint() releases the checkpoint
 * specified by @cno. @bh must be the buffer head which has been returned by
 * a previous call to nilfs_cpfile_get_checkpoint() with @cno.
 */
void nilfs_cpfile_put_checkpoint(struct inode *cpfile, __u64 cno,
				 struct buffer_head *bh)
{
	kunmap(bh->b_page);
	brelse(bh);
}

/**
 * nilfs_cpfile_delete_checkpoints - delete checkpoints
 * @cpfile: inode of checkpoint file
 * @start: start checkpoint number
 * @end: end checkpoint numer
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
	int ret, ncps, nicps, count, i;

	if (unlikely(start == 0 || start > end)) {
		printk(KERN_ERR "%s: invalid range of checkpoint numbers: "
		       "[%llu, %llu)\n", __func__,
		       (unsigned long long)start, (unsigned long long)end);
		return -EINVAL;
	}

	down_write(&NILFS_MDT(cpfile)->mi_sem);

	ret = nilfs_cpfile_get_header_block(cpfile, &header_bh);
	if (ret < 0)
		goto out_sem;
	tnicps = 0;

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

		kaddr = kmap_atomic(cp_bh->b_page, KM_USER0);
		cp = nilfs_cpfile_block_get_checkpoint(
			cpfile, cno, cp_bh, kaddr);
		nicps = 0;
		for (i = 0; i < ncps; i++, cp = (void *)cp + cpsz) {
			WARN_ON(nilfs_checkpoint_snapshot(cp));
			if (!nilfs_checkpoint_invalid(cp)) {
				nilfs_checkpoint_set_invalid(cp);
				nicps++;
			}
		}
		if (nicps > 0) {
			tnicps += nicps;
			nilfs_mdt_mark_buffer_dirty(cp_bh);
			nilfs_mdt_mark_dirty(cpfile);
			if (!nilfs_cpfile_is_in_first(cpfile, cno) &&
			    (count = nilfs_cpfile_block_sub_valid_checkpoints(
				    cpfile, cp_bh, kaddr, nicps)) == 0) {
				/* make hole */
				kunmap_atomic(kaddr, KM_USER0);
				brelse(cp_bh);
				ret = nilfs_cpfile_delete_checkpoint_block(
					cpfile, cno);
				if (ret == 0)
					continue;
				printk(KERN_ERR "%s: cannot delete block\n",
				       __func__);
				break;
			}
		}

		kunmap_atomic(kaddr, KM_USER0);
		brelse(cp_bh);
	}

	if (tnicps > 0) {
		kaddr = kmap_atomic(header_bh->b_page, KM_USER0);
		header = nilfs_cpfile_block_get_header(cpfile, header_bh,
						       kaddr);
		le64_add_cpu(&header->ch_ncheckpoints, -(u64)tnicps);
		nilfs_mdt_mark_buffer_dirty(header_bh);
		nilfs_mdt_mark_dirty(cpfile);
		kunmap_atomic(kaddr, KM_USER0);
	}

	brelse(header_bh);

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
					  void *buf, unsigned cisz, size_t nci)
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

	for (n = 0; cno < cur_cno && n < nci; cno += ncps) {
		ncps = nilfs_cpfile_checkpoints_in_block(cpfile, cno, cur_cno);
		ret = nilfs_cpfile_get_checkpoint_block(cpfile, cno, 0, &bh);
		if (ret < 0) {
			if (ret != -ENOENT)
				goto out;
			continue; /* skip hole */
		}

		kaddr = kmap_atomic(bh->b_page, KM_USER0);
		cp = nilfs_cpfile_block_get_checkpoint(cpfile, cno, bh, kaddr);
		for (i = 0; i < ncps && n < nci; i++, cp = (void *)cp + cpsz) {
			if (!nilfs_checkpoint_invalid(cp)) {
				nilfs_cpfile_checkpoint_to_cpinfo(cpfile, cp,
								  ci);
				ci = (void *)ci + cisz;
				n++;
			}
		}
		kunmap_atomic(kaddr, KM_USER0);
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
					  void *buf, unsigned cisz, size_t nci)
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
		kaddr = kmap_atomic(bh->b_page, KM_USER0);
		header = nilfs_cpfile_block_get_header(cpfile, bh, kaddr);
		curr = le64_to_cpu(header->ch_snapshot_list.ssl_next);
		kunmap_atomic(kaddr, KM_USER0);
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
	kaddr = kmap_atomic(bh->b_page, KM_USER0);
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
			kunmap_atomic(kaddr, KM_USER0);
			brelse(bh);
			ret = nilfs_cpfile_get_checkpoint_block(cpfile, next,
								0, &bh);
			if (unlikely(ret < 0)) {
				WARN_ON(ret == -ENOENT);
				goto out;
			}
			kaddr = kmap_atomic(bh->b_page, KM_USER0);
		}
		curr = next;
		curr_blkoff = next_blkoff;
	}
	kunmap_atomic(kaddr, KM_USER0);
	brelse(bh);
	*cnop = curr;
	ret = n;

 out:
	up_read(&NILFS_MDT(cpfile)->mi_sem);
	return ret;
}

/**
 * nilfs_cpfile_get_cpinfo -
 * @cpfile:
 * @cno:
 * @ci:
 * @nci:
 */

ssize_t nilfs_cpfile_get_cpinfo(struct inode *cpfile, __u64 *cnop, int mode,
				void *buf, unsigned cisz, size_t nci)
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
 * nilfs_cpfile_delete_checkpoint -
 * @cpfile:
 * @cno:
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
	kaddr = kmap_atomic(cp_bh->b_page, KM_USER0);
	cp = nilfs_cpfile_block_get_checkpoint(cpfile, cno, cp_bh, kaddr);
	if (nilfs_checkpoint_invalid(cp)) {
		ret = -ENOENT;
		kunmap_atomic(kaddr, KM_USER0);
		goto out_cp;
	}
	if (nilfs_checkpoint_snapshot(cp)) {
		ret = 0;
		kunmap_atomic(kaddr, KM_USER0);
		goto out_cp;
	}
	kunmap_atomic(kaddr, KM_USER0);

	ret = nilfs_cpfile_get_header_block(cpfile, &header_bh);
	if (ret < 0)
		goto out_cp;
	kaddr = kmap_atomic(header_bh->b_page, KM_USER0);
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
			kunmap_atomic(kaddr, KM_USER0);
			brelse(curr_bh);
			ret = nilfs_cpfile_get_checkpoint_block(cpfile, curr,
								0, &curr_bh);
			if (ret < 0)
				goto out_header;
			kaddr = kmap_atomic(curr_bh->b_page, KM_USER0);
		}
		curr_blkoff = prev_blkoff;
		cp = nilfs_cpfile_block_get_checkpoint(
			cpfile, curr, curr_bh, kaddr);
		list = &cp->cp_snapshot_list;
		prev = le64_to_cpu(list->ssl_prev);
	}
	kunmap_atomic(kaddr, KM_USER0);

	if (prev != 0) {
		ret = nilfs_cpfile_get_checkpoint_block(cpfile, prev, 0,
							&prev_bh);
		if (ret < 0)
			goto out_curr;
	} else {
		prev_bh = header_bh;
		get_bh(prev_bh);
	}

	kaddr = kmap_atomic(curr_bh->b_page, KM_USER0);
	list = nilfs_cpfile_block_get_snapshot_list(
		cpfile, curr, curr_bh, kaddr);
	list->ssl_prev = cpu_to_le64(cno);
	kunmap_atomic(kaddr, KM_USER0);

	kaddr = kmap_atomic(cp_bh->b_page, KM_USER0);
	cp = nilfs_cpfile_block_get_checkpoint(cpfile, cno, cp_bh, kaddr);
	cp->cp_snapshot_list.ssl_next = cpu_to_le64(curr);
	cp->cp_snapshot_list.ssl_prev = cpu_to_le64(prev);
	nilfs_checkpoint_set_snapshot(cp);
	kunmap_atomic(kaddr, KM_USER0);

	kaddr = kmap_atomic(prev_bh->b_page, KM_USER0);
	list = nilfs_cpfile_block_get_snapshot_list(
		cpfile, prev, prev_bh, kaddr);
	list->ssl_next = cpu_to_le64(cno);
	kunmap_atomic(kaddr, KM_USER0);

	kaddr = kmap_atomic(header_bh->b_page, KM_USER0);
	header = nilfs_cpfile_block_get_header(cpfile, header_bh, kaddr);
	le64_add_cpu(&header->ch_nsnapshots, 1);
	kunmap_atomic(kaddr, KM_USER0);

	nilfs_mdt_mark_buffer_dirty(prev_bh);
	nilfs_mdt_mark_buffer_dirty(curr_bh);
	nilfs_mdt_mark_buffer_dirty(cp_bh);
	nilfs_mdt_mark_buffer_dirty(header_bh);
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
	kaddr = kmap_atomic(cp_bh->b_page, KM_USER0);
	cp = nilfs_cpfile_block_get_checkpoint(cpfile, cno, cp_bh, kaddr);
	if (nilfs_checkpoint_invalid(cp)) {
		ret = -ENOENT;
		kunmap_atomic(kaddr, KM_USER0);
		goto out_cp;
	}
	if (!nilfs_checkpoint_snapshot(cp)) {
		ret = 0;
		kunmap_atomic(kaddr, KM_USER0);
		goto out_cp;
	}

	list = &cp->cp_snapshot_list;
	next = le64_to_cpu(list->ssl_next);
	prev = le64_to_cpu(list->ssl_prev);
	kunmap_atomic(kaddr, KM_USER0);

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

	kaddr = kmap_atomic(next_bh->b_page, KM_USER0);
	list = nilfs_cpfile_block_get_snapshot_list(
		cpfile, next, next_bh, kaddr);
	list->ssl_prev = cpu_to_le64(prev);
	kunmap_atomic(kaddr, KM_USER0);

	kaddr = kmap_atomic(prev_bh->b_page, KM_USER0);
	list = nilfs_cpfile_block_get_snapshot_list(
		cpfile, prev, prev_bh, kaddr);
	list->ssl_next = cpu_to_le64(next);
	kunmap_atomic(kaddr, KM_USER0);

	kaddr = kmap_atomic(cp_bh->b_page, KM_USER0);
	cp = nilfs_cpfile_block_get_checkpoint(cpfile, cno, cp_bh, kaddr);
	cp->cp_snapshot_list.ssl_next = cpu_to_le64(0);
	cp->cp_snapshot_list.ssl_prev = cpu_to_le64(0);
	nilfs_checkpoint_clear_snapshot(cp);
	kunmap_atomic(kaddr, KM_USER0);

	kaddr = kmap_atomic(header_bh->b_page, KM_USER0);
	header = nilfs_cpfile_block_get_header(cpfile, header_bh, kaddr);
	le64_add_cpu(&header->ch_nsnapshots, -1);
	kunmap_atomic(kaddr, KM_USER0);

	nilfs_mdt_mark_buffer_dirty(next_bh);
	nilfs_mdt_mark_buffer_dirty(prev_bh);
	nilfs_mdt_mark_buffer_dirty(cp_bh);
	nilfs_mdt_mark_buffer_dirty(header_bh);
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
 * @cpfile: inode of checkpoint file
 * @cno: checkpoint number
 *
 * Description:
 *
 * Return Value: On success, 1 is returned if the checkpoint specified by
 * @cno is a snapshot, or 0 if not. On error, one of the following negative
 * error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-ENOENT - No such checkpoint.
 */
int nilfs_cpfile_is_snapshot(struct inode *cpfile, __u64 cno)
{
	struct buffer_head *bh;
	struct nilfs_checkpoint *cp;
	void *kaddr;
	int ret;

	if (cno == 0)
		return -ENOENT; /* checkpoint number 0 is invalid */
	down_read(&NILFS_MDT(cpfile)->mi_sem);

	ret = nilfs_cpfile_get_checkpoint_block(cpfile, cno, 0, &bh);
	if (ret < 0)
		goto out;
	kaddr = kmap_atomic(bh->b_page, KM_USER0);
	cp = nilfs_cpfile_block_get_checkpoint(cpfile, cno, bh, kaddr);
	ret = nilfs_checkpoint_snapshot(cp);
	kunmap_atomic(kaddr, KM_USER0);
	brelse(bh);

 out:
	up_read(&NILFS_MDT(cpfile)->mi_sem);
	return ret;
}

/**
 * nilfs_cpfile_change_cpmode - change checkpoint mode
 * @cpfile: inode of checkpoint file
 * @cno: checkpoint number
 * @status: mode of checkpoint
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
	struct the_nilfs *nilfs;
	int ret;

	nilfs = NILFS_MDT(cpfile)->mi_nilfs;

	switch (mode) {
	case NILFS_CHECKPOINT:
		/*
		 * Check for protecting existing snapshot mounts:
		 * ns_mount_mutex is used to make this operation atomic and
		 * exclusive with a new mount job.  Though it doesn't cover
		 * umount, it's enough for the purpose.
		 */
		mutex_lock(&nilfs->ns_mount_mutex);
		if (nilfs_checkpoint_is_mounted(nilfs, cno, 1)) {
			/* Current implementation does not have to protect
			   plain read-only mounts since they are exclusive
			   with a read/write mount and are protected from the
			   cleaner. */
			ret = -EBUSY;
		} else
			ret = nilfs_cpfile_clear_snapshot(cpfile, cno);
		mutex_unlock(&nilfs->ns_mount_mutex);
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
 * @stat: pointer to a structure of checkpoint statistics
 *
 * Description: nilfs_cpfile_get_stat() returns information about checkpoints.
 *
 * Return Value: On success, 0 is returned, and checkpoints information is
 * stored in the place pointed by @stat. On error, one of the following
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
	kaddr = kmap_atomic(bh->b_page, KM_USER0);
	header = nilfs_cpfile_block_get_header(cpfile, bh, kaddr);
	cpstat->cs_cno = nilfs_mdt_cno(cpfile);
	cpstat->cs_ncps = le64_to_cpu(header->ch_ncheckpoints);
	cpstat->cs_nsss = le64_to_cpu(header->ch_nsnapshots);
	kunmap_atomic(kaddr, KM_USER0);
	brelse(bh);

 out_sem:
	up_read(&NILFS_MDT(cpfile)->mi_sem);
	return ret;
}

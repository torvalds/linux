/*
 * sufile.c - NILFS segment usage file.
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
#include "sufile.h"


static inline unsigned long
nilfs_sufile_segment_usages_per_block(const struct inode *sufile)
{
	return NILFS_MDT(sufile)->mi_entries_per_block;
}

static unsigned long
nilfs_sufile_get_blkoff(const struct inode *sufile, __u64 segnum)
{
	__u64 t = segnum + NILFS_MDT(sufile)->mi_first_entry_offset;
	do_div(t, nilfs_sufile_segment_usages_per_block(sufile));
	return (unsigned long)t;
}

static unsigned long
nilfs_sufile_get_offset(const struct inode *sufile, __u64 segnum)
{
	__u64 t = segnum + NILFS_MDT(sufile)->mi_first_entry_offset;
	return do_div(t, nilfs_sufile_segment_usages_per_block(sufile));
}

static unsigned long
nilfs_sufile_segment_usages_in_block(const struct inode *sufile, __u64 curr,
				     __u64 max)
{
	return min_t(unsigned long,
		     nilfs_sufile_segment_usages_per_block(sufile) -
		     nilfs_sufile_get_offset(sufile, curr),
		     max - curr + 1);
}

static inline struct nilfs_sufile_header *
nilfs_sufile_block_get_header(const struct inode *sufile,
			      struct buffer_head *bh,
			      void *kaddr)
{
	return kaddr + bh_offset(bh);
}

static struct nilfs_segment_usage *
nilfs_sufile_block_get_segment_usage(const struct inode *sufile, __u64 segnum,
				     struct buffer_head *bh, void *kaddr)
{
	return kaddr + bh_offset(bh) +
		nilfs_sufile_get_offset(sufile, segnum) *
		NILFS_MDT(sufile)->mi_entry_size;
}

static inline int nilfs_sufile_get_header_block(struct inode *sufile,
						struct buffer_head **bhp)
{
	return nilfs_mdt_get_block(sufile, 0, 0, NULL, bhp);
}

static inline int
nilfs_sufile_get_segment_usage_block(struct inode *sufile, __u64 segnum,
				     int create, struct buffer_head **bhp)
{
	return nilfs_mdt_get_block(sufile,
				   nilfs_sufile_get_blkoff(sufile, segnum),
				   create, NULL, bhp);
}

static void nilfs_sufile_mod_counter(struct buffer_head *header_bh,
				     u64 ncleanadd, u64 ndirtyadd)
{
	struct nilfs_sufile_header *header;
	void *kaddr;

	kaddr = kmap_atomic(header_bh->b_page, KM_USER0);
	header = kaddr + bh_offset(header_bh);
	le64_add_cpu(&header->sh_ncleansegs, ncleanadd);
	le64_add_cpu(&header->sh_ndirtysegs, ndirtyadd);
	kunmap_atomic(kaddr, KM_USER0);

	nilfs_mdt_mark_buffer_dirty(header_bh);
}

int nilfs_sufile_update(struct inode *sufile, __u64 segnum, int create,
			void (*dofunc)(struct inode *, __u64,
				       struct buffer_head *,
				       struct buffer_head *))
{
	struct buffer_head *header_bh, *bh;
	int ret;

	if (unlikely(segnum >= nilfs_sufile_get_nsegments(sufile))) {
		printk(KERN_WARNING "%s: invalid segment number: %llu\n",
		       __func__, (unsigned long long)segnum);
		return -EINVAL;
	}
	down_write(&NILFS_MDT(sufile)->mi_sem);

	ret = nilfs_sufile_get_header_block(sufile, &header_bh);
	if (ret < 0)
		goto out_sem;

	ret = nilfs_sufile_get_segment_usage_block(sufile, segnum, create, &bh);
	if (!ret) {
		dofunc(sufile, segnum, header_bh, bh);
		brelse(bh);
	}
	brelse(header_bh);

 out_sem:
	up_write(&NILFS_MDT(sufile)->mi_sem);
	return ret;
}

/**
 * nilfs_sufile_alloc - allocate a segment
 * @sufile: inode of segment usage file
 * @segnump: pointer to segment number
 *
 * Description: nilfs_sufile_alloc() allocates a clean segment.
 *
 * Return Value: On success, 0 is returned and the segment number of the
 * allocated segment is stored in the place pointed by @segnump. On error, one
 * of the following negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-ENOSPC - No clean segment left.
 */
int nilfs_sufile_alloc(struct inode *sufile, __u64 *segnump)
{
	struct buffer_head *header_bh, *su_bh;
	struct nilfs_sufile_header *header;
	struct nilfs_segment_usage *su;
	size_t susz = NILFS_MDT(sufile)->mi_entry_size;
	__u64 segnum, maxsegnum, last_alloc;
	void *kaddr;
	unsigned long nsegments, ncleansegs, nsus;
	int ret, i, j;

	down_write(&NILFS_MDT(sufile)->mi_sem);

	ret = nilfs_sufile_get_header_block(sufile, &header_bh);
	if (ret < 0)
		goto out_sem;
	kaddr = kmap_atomic(header_bh->b_page, KM_USER0);
	header = nilfs_sufile_block_get_header(sufile, header_bh, kaddr);
	ncleansegs = le64_to_cpu(header->sh_ncleansegs);
	last_alloc = le64_to_cpu(header->sh_last_alloc);
	kunmap_atomic(kaddr, KM_USER0);

	nsegments = nilfs_sufile_get_nsegments(sufile);
	segnum = last_alloc + 1;
	maxsegnum = nsegments - 1;
	for (i = 0; i < nsegments; i += nsus) {
		if (segnum >= nsegments) {
			/* wrap around */
			segnum = 0;
			maxsegnum = last_alloc;
		}
		ret = nilfs_sufile_get_segment_usage_block(sufile, segnum, 1,
							   &su_bh);
		if (ret < 0)
			goto out_header;
		kaddr = kmap_atomic(su_bh->b_page, KM_USER0);
		su = nilfs_sufile_block_get_segment_usage(
			sufile, segnum, su_bh, kaddr);

		nsus = nilfs_sufile_segment_usages_in_block(
			sufile, segnum, maxsegnum);
		for (j = 0; j < nsus; j++, su = (void *)su + susz, segnum++) {
			if (!nilfs_segment_usage_clean(su))
				continue;
			/* found a clean segment */
			nilfs_segment_usage_set_dirty(su);
			kunmap_atomic(kaddr, KM_USER0);

			kaddr = kmap_atomic(header_bh->b_page, KM_USER0);
			header = nilfs_sufile_block_get_header(
				sufile, header_bh, kaddr);
			le64_add_cpu(&header->sh_ncleansegs, -1);
			le64_add_cpu(&header->sh_ndirtysegs, 1);
			header->sh_last_alloc = cpu_to_le64(segnum);
			kunmap_atomic(kaddr, KM_USER0);

			nilfs_mdt_mark_buffer_dirty(header_bh);
			nilfs_mdt_mark_buffer_dirty(su_bh);
			nilfs_mdt_mark_dirty(sufile);
			brelse(su_bh);
			*segnump = segnum;
			goto out_header;
		}

		kunmap_atomic(kaddr, KM_USER0);
		brelse(su_bh);
	}

	/* no segments left */
	ret = -ENOSPC;

 out_header:
	brelse(header_bh);

 out_sem:
	up_write(&NILFS_MDT(sufile)->mi_sem);
	return ret;
}

void nilfs_sufile_do_cancel_free(struct inode *sufile, __u64 segnum,
				 struct buffer_head *header_bh,
				 struct buffer_head *su_bh)
{
	struct nilfs_segment_usage *su;
	void *kaddr;

	kaddr = kmap_atomic(su_bh->b_page, KM_USER0);
	su = nilfs_sufile_block_get_segment_usage(sufile, segnum, su_bh, kaddr);
	if (unlikely(!nilfs_segment_usage_clean(su))) {
		printk(KERN_WARNING "%s: segment %llu must be clean\n",
		       __func__, (unsigned long long)segnum);
		kunmap_atomic(kaddr, KM_USER0);
		return;
	}
	nilfs_segment_usage_set_dirty(su);
	kunmap_atomic(kaddr, KM_USER0);

	nilfs_sufile_mod_counter(header_bh, -1, 1);
	nilfs_mdt_mark_buffer_dirty(su_bh);
	nilfs_mdt_mark_dirty(sufile);
}

void nilfs_sufile_do_free(struct inode *sufile, __u64 segnum,
			  struct buffer_head *header_bh,
			  struct buffer_head *su_bh)
{
	struct nilfs_segment_usage *su;
	void *kaddr;
	int sudirty;

	kaddr = kmap_atomic(su_bh->b_page, KM_USER0);
	su = nilfs_sufile_block_get_segment_usage(sufile, segnum, su_bh, kaddr);
	if (nilfs_segment_usage_clean(su)) {
		printk(KERN_WARNING "%s: segment %llu is already clean\n",
		       __func__, (unsigned long long)segnum);
		kunmap_atomic(kaddr, KM_USER0);
		return;
	}
	WARN_ON(nilfs_segment_usage_error(su));
	WARN_ON(!nilfs_segment_usage_dirty(su));

	sudirty = nilfs_segment_usage_dirty(su);
	nilfs_segment_usage_set_clean(su);
	kunmap_atomic(kaddr, KM_USER0);
	nilfs_mdt_mark_buffer_dirty(su_bh);

	nilfs_sufile_mod_counter(header_bh, 1, sudirty ? (u64)-1 : 0);
	nilfs_mdt_mark_dirty(sufile);
}

/**
 * nilfs_sufile_get_segment_usage - get a segment usage
 * @sufile: inode of segment usage file
 * @segnum: segment number
 * @sup: pointer to segment usage
 * @bhp: pointer to buffer head
 *
 * Description: nilfs_sufile_get_segment_usage() acquires the segment usage
 * specified by @segnum.
 *
 * Return Value: On success, 0 is returned, and the segment usage and the
 * buffer head of the buffer on which the segment usage is located are stored
 * in the place pointed by @sup and @bhp, respectively. On error, one of the
 * following negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-EINVAL - Invalid segment usage number.
 */
int nilfs_sufile_get_segment_usage(struct inode *sufile, __u64 segnum,
				   struct nilfs_segment_usage **sup,
				   struct buffer_head **bhp)
{
	struct buffer_head *bh;
	struct nilfs_segment_usage *su;
	void *kaddr;
	int ret;

	/* segnum is 0 origin */
	if (segnum >= nilfs_sufile_get_nsegments(sufile))
		return -EINVAL;
	down_write(&NILFS_MDT(sufile)->mi_sem);
	ret = nilfs_sufile_get_segment_usage_block(sufile, segnum, 1, &bh);
	if (ret < 0)
		goto out_sem;
	kaddr = kmap(bh->b_page);
	su = nilfs_sufile_block_get_segment_usage(sufile, segnum, bh, kaddr);
	if (nilfs_segment_usage_error(su)) {
		kunmap(bh->b_page);
		brelse(bh);
		ret = -EINVAL;
		goto out_sem;
	}

	if (sup != NULL)
		*sup = su;
	*bhp = bh;

 out_sem:
	up_write(&NILFS_MDT(sufile)->mi_sem);
	return ret;
}

/**
 * nilfs_sufile_put_segment_usage - put a segment usage
 * @sufile: inode of segment usage file
 * @segnum: segment number
 * @bh: buffer head
 *
 * Description: nilfs_sufile_put_segment_usage() releases the segment usage
 * specified by @segnum. @bh must be the buffer head which have been returned
 * by a previous call to nilfs_sufile_get_segment_usage() with @segnum.
 */
void nilfs_sufile_put_segment_usage(struct inode *sufile, __u64 segnum,
				    struct buffer_head *bh)
{
	kunmap(bh->b_page);
	brelse(bh);
}

/**
 * nilfs_sufile_get_stat - get segment usage statistics
 * @sufile: inode of segment usage file
 * @stat: pointer to a structure of segment usage statistics
 *
 * Description: nilfs_sufile_get_stat() returns information about segment
 * usage.
 *
 * Return Value: On success, 0 is returned, and segment usage information is
 * stored in the place pointed by @stat. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 */
int nilfs_sufile_get_stat(struct inode *sufile, struct nilfs_sustat *sustat)
{
	struct buffer_head *header_bh;
	struct nilfs_sufile_header *header;
	struct the_nilfs *nilfs = NILFS_MDT(sufile)->mi_nilfs;
	void *kaddr;
	int ret;

	down_read(&NILFS_MDT(sufile)->mi_sem);

	ret = nilfs_sufile_get_header_block(sufile, &header_bh);
	if (ret < 0)
		goto out_sem;

	kaddr = kmap_atomic(header_bh->b_page, KM_USER0);
	header = nilfs_sufile_block_get_header(sufile, header_bh, kaddr);
	sustat->ss_nsegs = nilfs_sufile_get_nsegments(sufile);
	sustat->ss_ncleansegs = le64_to_cpu(header->sh_ncleansegs);
	sustat->ss_ndirtysegs = le64_to_cpu(header->sh_ndirtysegs);
	sustat->ss_ctime = nilfs->ns_ctime;
	sustat->ss_nongc_ctime = nilfs->ns_nongc_ctime;
	spin_lock(&nilfs->ns_last_segment_lock);
	sustat->ss_prot_seq = nilfs->ns_prot_seq;
	spin_unlock(&nilfs->ns_last_segment_lock);
	kunmap_atomic(kaddr, KM_USER0);
	brelse(header_bh);

 out_sem:
	up_read(&NILFS_MDT(sufile)->mi_sem);
	return ret;
}

/**
 * nilfs_sufile_get_ncleansegs - get the number of clean segments
 * @sufile: inode of segment usage file
 * @nsegsp: pointer to the number of clean segments
 *
 * Description: nilfs_sufile_get_ncleansegs() acquires the number of clean
 * segments.
 *
 * Return Value: On success, 0 is returned and the number of clean segments is
 * stored in the place pointed by @nsegsp. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 */
int nilfs_sufile_get_ncleansegs(struct inode *sufile, unsigned long *nsegsp)
{
	struct nilfs_sustat sustat;
	int ret;

	ret = nilfs_sufile_get_stat(sufile, &sustat);
	if (ret == 0)
		*nsegsp = sustat.ss_ncleansegs;
	return ret;
}

void nilfs_sufile_do_set_error(struct inode *sufile, __u64 segnum,
			       struct buffer_head *header_bh,
			       struct buffer_head *su_bh)
{
	struct nilfs_segment_usage *su;
	void *kaddr;
	int suclean;

	kaddr = kmap_atomic(su_bh->b_page, KM_USER0);
	su = nilfs_sufile_block_get_segment_usage(sufile, segnum, su_bh, kaddr);
	if (nilfs_segment_usage_error(su)) {
		kunmap_atomic(kaddr, KM_USER0);
		return;
	}
	suclean = nilfs_segment_usage_clean(su);
	nilfs_segment_usage_set_error(su);
	kunmap_atomic(kaddr, KM_USER0);

	if (suclean)
		nilfs_sufile_mod_counter(header_bh, -1, 0);
	nilfs_mdt_mark_buffer_dirty(su_bh);
	nilfs_mdt_mark_dirty(sufile);
}

/**
 * nilfs_sufile_get_suinfo -
 * @sufile: inode of segment usage file
 * @segnum: segment number to start looking
 * @si: array of suinfo
 * @nsi: size of suinfo array
 *
 * Description:
 *
 * Return Value: On success, 0 is returned and .... On error, one of the
 * following negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 */
ssize_t nilfs_sufile_get_suinfo(struct inode *sufile, __u64 segnum,
				struct nilfs_suinfo *si, size_t nsi)
{
	struct buffer_head *su_bh;
	struct nilfs_segment_usage *su;
	size_t susz = NILFS_MDT(sufile)->mi_entry_size;
	struct the_nilfs *nilfs = NILFS_MDT(sufile)->mi_nilfs;
	void *kaddr;
	unsigned long nsegs, segusages_per_block;
	ssize_t n;
	int ret, i, j;

	down_read(&NILFS_MDT(sufile)->mi_sem);

	segusages_per_block = nilfs_sufile_segment_usages_per_block(sufile);
	nsegs = min_t(unsigned long,
		      nilfs_sufile_get_nsegments(sufile) - segnum,
		      nsi);
	for (i = 0; i < nsegs; i += n, segnum += n) {
		n = min_t(unsigned long,
			  segusages_per_block -
				  nilfs_sufile_get_offset(sufile, segnum),
			  nsegs - i);
		ret = nilfs_sufile_get_segment_usage_block(sufile, segnum, 0,
							   &su_bh);
		if (ret < 0) {
			if (ret != -ENOENT)
				goto out;
			/* hole */
			memset(&si[i], 0, sizeof(struct nilfs_suinfo) * n);
			continue;
		}

		kaddr = kmap_atomic(su_bh->b_page, KM_USER0);
		su = nilfs_sufile_block_get_segment_usage(
			sufile, segnum, su_bh, kaddr);
		for (j = 0; j < n; j++, su = (void *)su + susz) {
			si[i + j].sui_lastmod = le64_to_cpu(su->su_lastmod);
			si[i + j].sui_nblocks = le32_to_cpu(su->su_nblocks);
			si[i + j].sui_flags = le32_to_cpu(su->su_flags) &
				~(1UL << NILFS_SEGMENT_USAGE_ACTIVE);
			if (nilfs_segment_is_active(nilfs, segnum + j))
				si[i + j].sui_flags |=
					(1UL << NILFS_SEGMENT_USAGE_ACTIVE);
		}
		kunmap_atomic(kaddr, KM_USER0);
		brelse(su_bh);
	}
	ret = nsegs;

 out:
	up_read(&NILFS_MDT(sufile)->mi_sem);
	return ret;
}

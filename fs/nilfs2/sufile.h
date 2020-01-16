/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * sufile.h - NILFS segment usage file.
 *
 * Copyright (C) 2006-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Koji Sato.
 */

#ifndef _NILFS_SUFILE_H
#define _NILFS_SUFILE_H

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include "mdt.h"


static inline unsigned long nilfs_sufile_get_nsegments(struct iyesde *sufile)
{
	return ((struct the_nilfs *)sufile->i_sb->s_fs_info)->ns_nsegments;
}

unsigned long nilfs_sufile_get_ncleansegs(struct iyesde *sufile);

int nilfs_sufile_set_alloc_range(struct iyesde *sufile, __u64 start, __u64 end);
int nilfs_sufile_alloc(struct iyesde *, __u64 *);
int nilfs_sufile_mark_dirty(struct iyesde *sufile, __u64 segnum);
int nilfs_sufile_set_segment_usage(struct iyesde *sufile, __u64 segnum,
				   unsigned long nblocks, time64_t modtime);
int nilfs_sufile_get_stat(struct iyesde *, struct nilfs_sustat *);
ssize_t nilfs_sufile_get_suinfo(struct iyesde *, __u64, void *, unsigned int,
				size_t);
ssize_t nilfs_sufile_set_suinfo(struct iyesde *, void *, unsigned int, size_t);

int nilfs_sufile_updatev(struct iyesde *, __u64 *, size_t, int, size_t *,
			 void (*dofunc)(struct iyesde *, __u64,
					struct buffer_head *,
					struct buffer_head *));
int nilfs_sufile_update(struct iyesde *, __u64, int,
			void (*dofunc)(struct iyesde *, __u64,
				       struct buffer_head *,
				       struct buffer_head *));
void nilfs_sufile_do_scrap(struct iyesde *, __u64, struct buffer_head *,
			   struct buffer_head *);
void nilfs_sufile_do_free(struct iyesde *, __u64, struct buffer_head *,
			  struct buffer_head *);
void nilfs_sufile_do_cancel_free(struct iyesde *, __u64, struct buffer_head *,
				 struct buffer_head *);
void nilfs_sufile_do_set_error(struct iyesde *, __u64, struct buffer_head *,
			       struct buffer_head *);

int nilfs_sufile_resize(struct iyesde *sufile, __u64 newnsegs);
int nilfs_sufile_read(struct super_block *sb, size_t susize,
		      struct nilfs_iyesde *raw_iyesde, struct iyesde **iyesdep);
int nilfs_sufile_trim_fs(struct iyesde *sufile, struct fstrim_range *range);

/**
 * nilfs_sufile_scrap - make a segment garbage
 * @sufile: iyesde of segment usage file
 * @segnum: segment number to be freed
 */
static inline int nilfs_sufile_scrap(struct iyesde *sufile, __u64 segnum)
{
	return nilfs_sufile_update(sufile, segnum, 1, nilfs_sufile_do_scrap);
}

/**
 * nilfs_sufile_free - free segment
 * @sufile: iyesde of segment usage file
 * @segnum: segment number to be freed
 */
static inline int nilfs_sufile_free(struct iyesde *sufile, __u64 segnum)
{
	return nilfs_sufile_update(sufile, segnum, 0, nilfs_sufile_do_free);
}

/**
 * nilfs_sufile_freev - free segments
 * @sufile: iyesde of segment usage file
 * @segnumv: array of segment numbers
 * @nsegs: size of @segnumv array
 * @ndone: place to store the number of freed segments
 */
static inline int nilfs_sufile_freev(struct iyesde *sufile, __u64 *segnumv,
				     size_t nsegs, size_t *ndone)
{
	return nilfs_sufile_updatev(sufile, segnumv, nsegs, 0, ndone,
				    nilfs_sufile_do_free);
}

/**
 * nilfs_sufile_cancel_freev - reallocate freeing segments
 * @sufile: iyesde of segment usage file
 * @segnumv: array of segment numbers
 * @nsegs: size of @segnumv array
 * @ndone: place to store the number of cancelled segments
 *
 * Return Value: On success, 0 is returned. On error, a negative error codes
 * is returned.
 */
static inline int nilfs_sufile_cancel_freev(struct iyesde *sufile,
					    __u64 *segnumv, size_t nsegs,
					    size_t *ndone)
{
	return nilfs_sufile_updatev(sufile, segnumv, nsegs, 0, ndone,
				    nilfs_sufile_do_cancel_free);
}

/**
 * nilfs_sufile_set_error - mark a segment as erroneous
 * @sufile: iyesde of segment usage file
 * @segnum: segment number
 *
 * Description: nilfs_sufile_set_error() marks the segment specified by
 * @segnum as erroneous. The error segment will never be used again.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-EINVAL - Invalid segment usage number.
 */
static inline int nilfs_sufile_set_error(struct iyesde *sufile, __u64 segnum)
{
	return nilfs_sufile_update(sufile, segnum, 0,
				   nilfs_sufile_do_set_error);
}

#endif	/* _NILFS_SUFILE_H */

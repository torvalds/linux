/*
 * sufile.h - NILFS segment usage file.
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

#ifndef _NILFS_SUFILE_H
#define _NILFS_SUFILE_H

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/nilfs2_fs.h>
#include "mdt.h"

#define NILFS_SUFILE_GFP	NILFS_MDT_GFP

static inline unsigned long nilfs_sufile_get_nsegments(struct inode *sufile)
{
	return NILFS_MDT(sufile)->mi_nilfs->ns_nsegments;
}

int nilfs_sufile_alloc(struct inode *, __u64 *);
int nilfs_sufile_cancel_free(struct inode *, __u64);
int nilfs_sufile_freev(struct inode *, __u64 *, size_t);
int nilfs_sufile_free(struct inode *, __u64);
int nilfs_sufile_get_segment_usage(struct inode *, __u64,
				   struct nilfs_segment_usage **,
				   struct buffer_head **);
void nilfs_sufile_put_segment_usage(struct inode *, __u64,
				    struct buffer_head *);
int nilfs_sufile_get_stat(struct inode *, struct nilfs_sustat *);
int nilfs_sufile_get_ncleansegs(struct inode *, unsigned long *);
int nilfs_sufile_set_error(struct inode *, __u64);
ssize_t nilfs_sufile_get_suinfo(struct inode *, __u64, struct nilfs_suinfo *,
				size_t);


#endif	/* _NILFS_SUFILE_H */

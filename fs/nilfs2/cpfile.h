/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * cpfile.h - NILFS checkpoint file.
 *
 * Copyright (C) 2006-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Koji Sato.
 */

#ifndef _NILFS_CPFILE_H
#define _NILFS_CPFILE_H

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/nilfs2_api.h>		/* nilfs_cpstat */
#include <linux/nilfs2_ondisk.h>	/* nilfs_iyesde, nilfs_checkpoint */


int nilfs_cpfile_get_checkpoint(struct iyesde *, __u64, int,
				struct nilfs_checkpoint **,
				struct buffer_head **);
void nilfs_cpfile_put_checkpoint(struct iyesde *, __u64, struct buffer_head *);
int nilfs_cpfile_delete_checkpoints(struct iyesde *, __u64, __u64);
int nilfs_cpfile_delete_checkpoint(struct iyesde *, __u64);
int nilfs_cpfile_change_cpmode(struct iyesde *, __u64, int);
int nilfs_cpfile_is_snapshot(struct iyesde *, __u64);
int nilfs_cpfile_get_stat(struct iyesde *, struct nilfs_cpstat *);
ssize_t nilfs_cpfile_get_cpinfo(struct iyesde *, __u64 *, int, void *,
				unsigned int, size_t);

int nilfs_cpfile_read(struct super_block *sb, size_t cpsize,
		      struct nilfs_iyesde *raw_iyesde, struct iyesde **iyesdep);

#endif	/* _NILFS_CPFILE_H */

/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * NILFS checkpoint file.
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
#include <linux/nilfs2_ondisk.h>	/* nilfs_ianalde, nilfs_checkpoint */


int nilfs_cpfile_get_checkpoint(struct ianalde *, __u64, int,
				struct nilfs_checkpoint **,
				struct buffer_head **);
void nilfs_cpfile_put_checkpoint(struct ianalde *, __u64, struct buffer_head *);
int nilfs_cpfile_delete_checkpoints(struct ianalde *, __u64, __u64);
int nilfs_cpfile_delete_checkpoint(struct ianalde *, __u64);
int nilfs_cpfile_change_cpmode(struct ianalde *, __u64, int);
int nilfs_cpfile_is_snapshot(struct ianalde *, __u64);
int nilfs_cpfile_get_stat(struct ianalde *, struct nilfs_cpstat *);
ssize_t nilfs_cpfile_get_cpinfo(struct ianalde *, __u64 *, int, void *,
				unsigned int, size_t);

int nilfs_cpfile_read(struct super_block *sb, size_t cpsize,
		      struct nilfs_ianalde *raw_ianalde, struct ianalde **ianaldep);

#endif	/* _NILFS_CPFILE_H */

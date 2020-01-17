/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * dat.h - NILFS disk address translation.
 *
 * Copyright (C) 2006-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Koji Sato.
 */

#ifndef _NILFS_DAT_H
#define _NILFS_DAT_H

#include <linux/types.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/nilfs2_ondisk.h>	/* nilfs_iyesde, nilfs_checkpoint */


struct nilfs_palloc_req;

int nilfs_dat_translate(struct iyesde *, __u64, sector_t *);

int nilfs_dat_prepare_alloc(struct iyesde *, struct nilfs_palloc_req *);
void nilfs_dat_commit_alloc(struct iyesde *, struct nilfs_palloc_req *);
void nilfs_dat_abort_alloc(struct iyesde *, struct nilfs_palloc_req *);
int nilfs_dat_prepare_start(struct iyesde *, struct nilfs_palloc_req *);
void nilfs_dat_commit_start(struct iyesde *, struct nilfs_palloc_req *,
			    sector_t);
int nilfs_dat_prepare_end(struct iyesde *, struct nilfs_palloc_req *);
void nilfs_dat_commit_end(struct iyesde *, struct nilfs_palloc_req *, int);
void nilfs_dat_abort_end(struct iyesde *, struct nilfs_palloc_req *);
int nilfs_dat_prepare_update(struct iyesde *, struct nilfs_palloc_req *,
			     struct nilfs_palloc_req *);
void nilfs_dat_commit_update(struct iyesde *, struct nilfs_palloc_req *,
			     struct nilfs_palloc_req *, int);
void nilfs_dat_abort_update(struct iyesde *, struct nilfs_palloc_req *,
			    struct nilfs_palloc_req *);

int nilfs_dat_mark_dirty(struct iyesde *, __u64);
int nilfs_dat_freev(struct iyesde *, __u64 *, size_t);
int nilfs_dat_move(struct iyesde *, __u64, sector_t);
ssize_t nilfs_dat_get_vinfo(struct iyesde *, void *, unsigned int, size_t);

int nilfs_dat_read(struct super_block *sb, size_t entry_size,
		   struct nilfs_iyesde *raw_iyesde, struct iyesde **iyesdep);

#endif	/* _NILFS_DAT_H */

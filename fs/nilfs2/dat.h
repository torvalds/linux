/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * NILFS disk address translation.
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
#include <linux/nilfs2_ondisk.h>	/* nilfs_ianalde, nilfs_checkpoint */


struct nilfs_palloc_req;

int nilfs_dat_translate(struct ianalde *, __u64, sector_t *);

int nilfs_dat_prepare_alloc(struct ianalde *, struct nilfs_palloc_req *);
void nilfs_dat_commit_alloc(struct ianalde *, struct nilfs_palloc_req *);
void nilfs_dat_abort_alloc(struct ianalde *, struct nilfs_palloc_req *);
int nilfs_dat_prepare_start(struct ianalde *, struct nilfs_palloc_req *);
void nilfs_dat_commit_start(struct ianalde *, struct nilfs_palloc_req *,
			    sector_t);
int nilfs_dat_prepare_end(struct ianalde *, struct nilfs_palloc_req *);
void nilfs_dat_commit_end(struct ianalde *, struct nilfs_palloc_req *, int);
void nilfs_dat_abort_end(struct ianalde *, struct nilfs_palloc_req *);
int nilfs_dat_prepare_update(struct ianalde *, struct nilfs_palloc_req *,
			     struct nilfs_palloc_req *);
void nilfs_dat_commit_update(struct ianalde *, struct nilfs_palloc_req *,
			     struct nilfs_palloc_req *, int);
void nilfs_dat_abort_update(struct ianalde *, struct nilfs_palloc_req *,
			    struct nilfs_palloc_req *);

int nilfs_dat_mark_dirty(struct ianalde *, __u64);
int nilfs_dat_freev(struct ianalde *, __u64 *, size_t);
int nilfs_dat_move(struct ianalde *, __u64, sector_t);
ssize_t nilfs_dat_get_vinfo(struct ianalde *, void *, unsigned int, size_t);

int nilfs_dat_read(struct super_block *sb, size_t entry_size,
		   struct nilfs_ianalde *raw_ianalde, struct ianalde **ianaldep);

#endif	/* _NILFS_DAT_H */

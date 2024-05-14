/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 */

#ifndef __DIO_DOT_H__
#define __DIO_DOT_H__

#include <linux/buffer_head.h>
#include <linux/string.h>
#include "incore.h"

static inline void gfs2_buffer_clear(struct buffer_head *bh)
{
	memset(bh->b_data, 0, bh->b_size);
}

static inline void gfs2_buffer_clear_tail(struct buffer_head *bh, int head)
{
	BUG_ON(head > bh->b_size);
	memset(bh->b_data + head, 0, bh->b_size - head);
}

static inline void gfs2_buffer_copy_tail(struct buffer_head *to_bh,
					 int to_head,
					 struct buffer_head *from_bh,
					 int from_head)
{
	BUG_ON(from_head < to_head);
	memcpy(to_bh->b_data + to_head, from_bh->b_data + from_head,
	       from_bh->b_size - from_head);
	memset(to_bh->b_data + to_bh->b_size + to_head - from_head,
	       0, from_head - to_head);
}

extern const struct address_space_operations gfs2_meta_aops;
extern const struct address_space_operations gfs2_rgrp_aops;

static inline struct gfs2_sbd *gfs2_mapping2sbd(struct address_space *mapping)
{
	struct inode *inode = mapping->host;
	if (mapping->a_ops == &gfs2_meta_aops) {
		struct gfs2_glock_aspace *gla =
			container_of(mapping, struct gfs2_glock_aspace, mapping);
		return gla->glock.gl_name.ln_sbd;
	} else if (mapping->a_ops == &gfs2_rgrp_aops)
		return container_of(mapping, struct gfs2_sbd, sd_aspace);
	else
		return inode->i_sb->s_fs_info;
}

extern struct buffer_head *gfs2_meta_new(struct gfs2_glock *gl, u64 blkno);
extern int gfs2_meta_read(struct gfs2_glock *gl, u64 blkno, int flags,
			  int rahead, struct buffer_head **bhp);
extern int gfs2_meta_wait(struct gfs2_sbd *sdp, struct buffer_head *bh);
extern struct buffer_head *gfs2_getbuf(struct gfs2_glock *gl, u64 blkno,
				       int create);
enum {
	REMOVE_JDATA = 0,
	REMOVE_META = 1,
};

extern void gfs2_remove_from_journal(struct buffer_head *bh, int meta);
extern void gfs2_journal_wipe(struct gfs2_inode *ip, u64 bstart, u32 blen);
extern int gfs2_meta_buffer(struct gfs2_inode *ip, u32 mtype, u64 num,
			    struct buffer_head **bhp);

static inline int gfs2_meta_inode_buffer(struct gfs2_inode *ip,
					 struct buffer_head **bhp)
{
	return gfs2_meta_buffer(ip, GFS2_METATYPE_DI, ip->i_no_addr, bhp);
}

struct buffer_head *gfs2_meta_ra(struct gfs2_glock *gl, u64 dblock, u32 extlen);

#define buffer_busy(bh) \
((bh)->b_state & ((1ul << BH_Dirty) | (1ul << BH_Lock) | (1ul << BH_Pinned)))

#endif /* __DIO_DOT_H__ */


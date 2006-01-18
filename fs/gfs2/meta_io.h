/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#ifndef __DIO_DOT_H__
#define __DIO_DOT_H__

static inline void gfs2_buffer_clear(struct buffer_head *bh)
{
	memset(bh->b_data, 0, bh->b_size);
}

static inline void gfs2_buffer_clear_tail(struct buffer_head *bh, int head)
{
	memset(bh->b_data + head, 0, bh->b_size - head);
}

static inline void gfs2_buffer_clear_ends(struct buffer_head *bh, int offset,
					  int amount, int journaled)
{
	int z_off1 = (journaled) ? sizeof(struct gfs2_meta_header) : 0;
	int z_len1 = offset - z_off1;
	int z_off2 = offset + amount;
	int z_len2 = (bh)->b_size - z_off2;

	if (z_len1)
		memset(bh->b_data + z_off1, 0, z_len1);

	if (z_len2)
		memset(bh->b_data + z_off2, 0, z_len2);
}

static inline void gfs2_buffer_copy_tail(struct buffer_head *to_bh,
					 int to_head,
					 struct buffer_head *from_bh,
					 int from_head)
{
	memcpy(to_bh->b_data + to_head,
	       from_bh->b_data + from_head,
	       from_bh->b_size - from_head);
	memset(to_bh->b_data + to_bh->b_size + to_head - from_head,
	       0,
	       from_head - to_head);
}

struct inode *gfs2_aspace_get(struct gfs2_sbd *sdp);
void gfs2_aspace_put(struct inode *aspace);

void gfs2_ail1_start_one(struct gfs2_sbd *sdp, struct gfs2_ail *ai);
int gfs2_ail1_empty_one(struct gfs2_sbd *sdp, struct gfs2_ail *ai, int flags);
void gfs2_ail2_empty_one(struct gfs2_sbd *sdp, struct gfs2_ail *ai);
void gfs2_ail_empty_gl(struct gfs2_glock *gl);

void gfs2_meta_inval(struct gfs2_glock *gl);
void gfs2_meta_sync(struct gfs2_glock *gl, int flags);

struct buffer_head *gfs2_meta_new(struct gfs2_glock *gl, uint64_t blkno);
int gfs2_meta_read(struct gfs2_glock *gl, uint64_t blkno,
		   int flags, struct buffer_head **bhp);
int gfs2_meta_reread(struct gfs2_sbd *sdp, struct buffer_head *bh, int flags);

void gfs2_attach_bufdata(struct gfs2_glock *gl, struct buffer_head *bh, int meta);
void gfs2_meta_pin(struct gfs2_sbd *sdp, struct buffer_head *bh);
void gfs2_meta_unpin(struct gfs2_sbd *sdp, struct buffer_head *bh,
		 struct gfs2_ail *ai);

void gfs2_meta_wipe(struct gfs2_inode *ip, uint64_t bstart, uint32_t blen);

void gfs2_meta_cache_flush(struct gfs2_inode *ip);
int gfs2_meta_indirect_buffer(struct gfs2_inode *ip, int height, uint64_t num,
			      int new, struct buffer_head **bhp);

static inline int gfs2_meta_inode_buffer(struct gfs2_inode *ip,
					 struct buffer_head **bhp)
{
	return gfs2_meta_indirect_buffer(ip, 0, ip->i_num.no_addr, 0, bhp);
}

void gfs2_meta_ra(struct gfs2_glock *gl, uint64_t dblock, uint32_t extlen);
void gfs2_meta_syncfs(struct gfs2_sbd *sdp);

#endif /* __DIO_DOT_H__ */


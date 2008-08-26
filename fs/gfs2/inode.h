/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __INODE_DOT_H__
#define __INODE_DOT_H__

#include "util.h"

static inline int gfs2_is_stuffed(const struct gfs2_inode *ip)
{
	return !ip->i_height;
}

static inline int gfs2_is_jdata(const struct gfs2_inode *ip)
{
	return ip->i_di.di_flags & GFS2_DIF_JDATA;
}

static inline int gfs2_is_writeback(const struct gfs2_inode *ip)
{
	const struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	return (sdp->sd_args.ar_data == GFS2_DATA_WRITEBACK) && !gfs2_is_jdata(ip);
}

static inline int gfs2_is_ordered(const struct gfs2_inode *ip)
{
	const struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	return (sdp->sd_args.ar_data == GFS2_DATA_ORDERED) && !gfs2_is_jdata(ip);
}

static inline int gfs2_is_dir(const struct gfs2_inode *ip)
{
	return S_ISDIR(ip->i_inode.i_mode);
}

static inline void gfs2_set_inode_blocks(struct inode *inode, u64 blocks)
{
	inode->i_blocks = blocks <<
		(GFS2_SB(inode)->sd_sb.sb_bsize_shift - GFS2_BASIC_BLOCK_SHIFT);
}

static inline u64 gfs2_get_inode_blocks(const struct inode *inode)
{
	return inode->i_blocks >>
		(GFS2_SB(inode)->sd_sb.sb_bsize_shift - GFS2_BASIC_BLOCK_SHIFT);
}

static inline void gfs2_add_inode_blocks(struct inode *inode, s64 change)
{
	gfs2_assert(GFS2_SB(inode), (change >= 0 || inode->i_blocks > -change));
	change *= (GFS2_SB(inode)->sd_sb.sb_bsize/GFS2_BASIC_BLOCK);
	inode->i_blocks += change;
}

static inline int gfs2_check_inum(const struct gfs2_inode *ip, u64 no_addr,
				  u64 no_formal_ino)
{
	return ip->i_no_addr == no_addr && ip->i_no_formal_ino == no_formal_ino;
}

static inline void gfs2_inum_out(const struct gfs2_inode *ip,
				 struct gfs2_dirent *dent)
{
	dent->de_inum.no_formal_ino = cpu_to_be64(ip->i_no_formal_ino);
	dent->de_inum.no_addr = cpu_to_be64(ip->i_no_addr);
}


void gfs2_set_iop(struct inode *inode);
struct inode *gfs2_inode_lookup(struct super_block *sb, unsigned type, 
				u64 no_addr, u64 no_formal_ino,
				int skip_freeing);
struct inode *gfs2_ilookup(struct super_block *sb, u64 no_addr);

int gfs2_inode_refresh(struct gfs2_inode *ip);

int gfs2_dinode_dealloc(struct gfs2_inode *inode);
int gfs2_change_nlink(struct gfs2_inode *ip, int diff);
struct inode *gfs2_lookupi(struct inode *dir, const struct qstr *name,
			   int is_root);
struct inode *gfs2_createi(struct gfs2_holder *ghs, const struct qstr *name,
			   unsigned int mode, dev_t dev);
int gfs2_rmdiri(struct gfs2_inode *dip, const struct qstr *name,
		struct gfs2_inode *ip);
int gfs2_unlink_ok(struct gfs2_inode *dip, const struct qstr *name,
		   const struct gfs2_inode *ip);
int gfs2_permission(struct inode *inode, int mask);
int gfs2_readlinki(struct gfs2_inode *ip, char **buf, unsigned int *len);
int gfs2_glock_nq_atime(struct gfs2_holder *gh);
int gfs2_setattr_simple(struct gfs2_inode *ip, struct iattr *attr);
struct inode *gfs2_lookup_simple(struct inode *dip, const char *name);
void gfs2_dinode_out(const struct gfs2_inode *ip, void *buf);
void gfs2_dinode_print(const struct gfs2_inode *ip);

#endif /* __INODE_DOT_H__ */


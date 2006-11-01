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

static inline int gfs2_is_stuffed(struct gfs2_inode *ip)
{
	return !ip->i_di.di_height;
}

static inline int gfs2_is_jdata(struct gfs2_inode *ip)
{
	return ip->i_di.di_flags & GFS2_DIF_JDATA;
}

static inline int gfs2_is_dir(struct gfs2_inode *ip)
{
	return S_ISDIR(ip->i_di.di_mode);
}

void gfs2_inode_attr_in(struct gfs2_inode *ip);
void gfs2_inode_attr_out(struct gfs2_inode *ip);
struct inode *gfs2_inode_lookup(struct super_block *sb, struct gfs2_inum_host *inum, unsigned type);
struct inode *gfs2_ilookup(struct super_block *sb, struct gfs2_inum_host *inum);

int gfs2_inode_refresh(struct gfs2_inode *ip);

int gfs2_dinode_dealloc(struct gfs2_inode *inode);
int gfs2_change_nlink(struct gfs2_inode *ip, int diff);
struct inode *gfs2_lookupi(struct inode *dir, const struct qstr *name,
			   int is_root, struct nameidata *nd);
struct inode *gfs2_createi(struct gfs2_holder *ghs, const struct qstr *name,
			   unsigned int mode, dev_t dev);
int gfs2_rmdiri(struct gfs2_inode *dip, const struct qstr *name,
		struct gfs2_inode *ip);
int gfs2_unlink_ok(struct gfs2_inode *dip, const struct qstr *name,
		   struct gfs2_inode *ip);
int gfs2_ok_to_move(struct gfs2_inode *this, struct gfs2_inode *to);
int gfs2_readlinki(struct gfs2_inode *ip, char **buf, unsigned int *len);

int gfs2_glock_nq_atime(struct gfs2_holder *gh);
int gfs2_glock_nq_m_atime(unsigned int num_gh, struct gfs2_holder *ghs);

int gfs2_setattr_simple(struct gfs2_inode *ip, struct iattr *attr);

struct inode *gfs2_lookup_simple(struct inode *dip, const char *name);

#endif /* __INODE_DOT_H__ */


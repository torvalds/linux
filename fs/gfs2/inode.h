/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
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

void gfs2_inode_attr_in(struct gfs2_inode *ip);
void gfs2_inode_attr_out(struct gfs2_inode *ip);
struct inode *gfs2_ip2v_lookup(struct gfs2_inode *ip);
struct inode *gfs2_ip2v(struct gfs2_inode *ip);
struct inode *gfs2_iget(struct super_block *sb, struct gfs2_inum *inum);

void gfs2_inode_min_init(struct gfs2_inode *ip, unsigned int type);
int gfs2_inode_refresh(struct gfs2_inode *ip);

int gfs2_inode_get(struct gfs2_glock *i_gl,
		   struct gfs2_inum *inum, int create,
		   struct gfs2_inode **ipp);
void gfs2_inode_hold(struct gfs2_inode *ip);
void gfs2_inode_put(struct gfs2_inode *ip);
void gfs2_inode_destroy(struct gfs2_inode *ip);

int gfs2_inode_dealloc(struct gfs2_sbd *sdp, struct gfs2_unlinked *ul);

int gfs2_change_nlink(struct gfs2_inode *ip, int diff);
int gfs2_lookupi(struct gfs2_inode *dip, struct qstr *name, int is_root,
		 struct gfs2_inode **ipp);
int gfs2_createi(struct gfs2_holder *ghs, struct qstr *name, unsigned int mode);
int gfs2_unlinki(struct gfs2_inode *dip, struct qstr *name,
		 struct gfs2_inode *ip, struct gfs2_unlinked *ul);
int gfs2_rmdiri(struct gfs2_inode *dip, struct qstr *name,
		struct gfs2_inode *ip, struct gfs2_unlinked *ul);
int gfs2_unlink_ok(struct gfs2_inode *dip, struct qstr *name,
		   struct gfs2_inode *ip);
int gfs2_ok_to_move(struct gfs2_inode *this, struct gfs2_inode *to);
int gfs2_readlinki(struct gfs2_inode *ip, char **buf, unsigned int *len);

int gfs2_glock_nq_atime(struct gfs2_holder *gh);
int gfs2_glock_nq_m_atime(unsigned int num_gh, struct gfs2_holder *ghs);

void gfs2_try_toss_vnode(struct gfs2_inode *ip);

int gfs2_setattr_simple(struct gfs2_inode *ip, struct iattr *attr);

int gfs2_repermission(struct inode *inode, int mask, struct nameidata *nd);

static inline int gfs2_lookup_simple(struct gfs2_inode *dip, char *name,
				     struct gfs2_inode **ipp)
{
	struct qstr qstr;
	memset(&qstr, 0, sizeof(struct qstr));
	qstr.name = name;
	qstr.len = strlen(name);
	return gfs2_lookupi(dip, &qstr, 1, ipp);
}

#endif /* __INODE_DOT_H__ */


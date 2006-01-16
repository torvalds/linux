/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#ifndef __QUOTA_DOT_H__
#define __QUOTA_DOT_H__

#define NO_QUOTA_CHANGE ((uint32_t)-1)

int gfs2_quota_hold(struct gfs2_inode *ip, uint32_t uid, uint32_t gid);
void gfs2_quota_unhold(struct gfs2_inode *ip);

int gfs2_quota_lock(struct gfs2_inode *ip, uint32_t uid, uint32_t gid);
void gfs2_quota_unlock(struct gfs2_inode *ip);

int gfs2_quota_check(struct gfs2_inode *ip, uint32_t uid, uint32_t gid);
void gfs2_quota_change(struct gfs2_inode *ip, int64_t change,
		       uint32_t uid, uint32_t gid);

int gfs2_quota_sync(struct gfs2_sbd *sdp);
int gfs2_quota_refresh(struct gfs2_sbd *sdp, int user, uint32_t id);
int gfs2_quota_read(struct gfs2_sbd *sdp, int user, uint32_t id,
		    struct gfs2_quota *q);

int gfs2_quota_init(struct gfs2_sbd *sdp);
void gfs2_quota_scan(struct gfs2_sbd *sdp);
void gfs2_quota_cleanup(struct gfs2_sbd *sdp);

#endif /* __QUOTA_DOT_H__ */

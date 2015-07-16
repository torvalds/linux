/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __QUOTA_DOT_H__
#define __QUOTA_DOT_H__

#include <linux/list_lru.h>

struct gfs2_inode;
struct gfs2_sbd;

#define NO_UID_QUOTA_CHANGE INVALID_UID
#define NO_GID_QUOTA_CHANGE INVALID_GID

extern int gfs2_quota_hold(struct gfs2_inode *ip, kuid_t uid, kgid_t gid);
extern void gfs2_quota_unhold(struct gfs2_inode *ip);

extern int gfs2_quota_lock(struct gfs2_inode *ip, kuid_t uid, kgid_t gid);
extern void gfs2_quota_unlock(struct gfs2_inode *ip);

extern int gfs2_quota_check(struct gfs2_inode *ip, kuid_t uid, kgid_t gid,
			    struct gfs2_alloc_parms *ap);
extern void gfs2_quota_change(struct gfs2_inode *ip, s64 change,
			      kuid_t uid, kgid_t gid);

extern int gfs2_quota_sync(struct super_block *sb, int type);
extern int gfs2_quota_refresh(struct gfs2_sbd *sdp, struct kqid qid);

extern int gfs2_quota_init(struct gfs2_sbd *sdp);
extern void gfs2_quota_cleanup(struct gfs2_sbd *sdp);
extern int gfs2_quotad(void *data);

extern void gfs2_wake_up_statfs(struct gfs2_sbd *sdp);

static inline int gfs2_quota_lock_check(struct gfs2_inode *ip,
					struct gfs2_alloc_parms *ap)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	int ret;
	if (sdp->sd_args.ar_quota == GFS2_QUOTA_OFF)
		return 0;
	ret = gfs2_quota_lock(ip, NO_UID_QUOTA_CHANGE, NO_GID_QUOTA_CHANGE);
	if (ret)
		return ret;
	if (sdp->sd_args.ar_quota != GFS2_QUOTA_ON)
		return 0;
	ret = gfs2_quota_check(ip, ip->i_inode.i_uid, ip->i_inode.i_gid, ap);
	if (ret)
		gfs2_quota_unlock(ip);
	return ret;
}

extern const struct quotactl_ops gfs2_quotactl_ops;
extern struct shrinker gfs2_qd_shrinker;
extern struct list_lru gfs2_qd_lru;
extern void __init gfs2_quota_hash_init(void);

#endif /* __QUOTA_DOT_H__ */

/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) International Business Machines  Corp., 2002
 */
#ifndef _H_JFS_ACL
#define _H_JFS_ACL

#ifdef CONFIG_JFS_POSIX_ACL

struct posix_acl *jfs_get_acl(struct ianalde *ianalde, int type, bool rcu);
int jfs_set_acl(struct mnt_idmap *idmap, struct dentry *dentry,
		struct posix_acl *acl, int type);
int jfs_init_acl(tid_t, struct ianalde *, struct ianalde *);

#else

static inline int jfs_init_acl(tid_t tid, struct ianalde *ianalde,
			       struct ianalde *dir)
{
	return 0;
}

#endif
#endif		/* _H_JFS_ACL */

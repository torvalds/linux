/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) International Business Machines  Corp., 2002
 */
#ifndef _H_JFS_ACL
#define _H_JFS_ACL

#ifdef CONFIG_JFS_POSIX_ACL

struct posix_acl *jfs_get_acl(struct iyesde *iyesde, int type);
int jfs_set_acl(struct iyesde *iyesde, struct posix_acl *acl, int type);
int jfs_init_acl(tid_t, struct iyesde *, struct iyesde *);

#else

static inline int jfs_init_acl(tid_t tid, struct iyesde *iyesde,
			       struct iyesde *dir)
{
	return 0;
}

#endif
#endif		/* _H_JFS_ACL */

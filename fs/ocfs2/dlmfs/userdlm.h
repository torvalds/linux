/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * userdlm.h
 *
 * Userspace dlm defines
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */


#ifndef USERDLM_H
#define USERDLM_H

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/workqueue.h>

/* user_lock_res->l_flags flags. */
#define USER_LOCK_ATTACHED      (0x00000001) /* we have initialized
					       * the lvb */
#define USER_LOCK_BUSY          (0x00000002) /* we are currently in
					       * dlm_lock */
#define USER_LOCK_BLOCKED       (0x00000004) /* blocked waiting to
					      * downconvert*/
#define USER_LOCK_IN_TEARDOWN   (0x00000008) /* we're currently
					      * destroying this
					      * lock. */
#define USER_LOCK_QUEUED        (0x00000010) /* lock is on the
					      * workqueue */
#define USER_LOCK_IN_CANCEL     (0x00000020)

struct user_lock_res {
	spinlock_t               l_lock;

	int                      l_flags;

#define USER_DLM_LOCK_ID_MAX_LEN  32
	char                     l_name[USER_DLM_LOCK_ID_MAX_LEN];
	int                      l_namelen;
	int                      l_level;
	unsigned int             l_ro_holders;
	unsigned int             l_ex_holders;
	struct ocfs2_dlm_lksb    l_lksb;

	int                      l_requested;
	int                      l_blocking;

	wait_queue_head_t        l_event;

	struct work_struct       l_work;
};

extern struct workqueue_struct *user_dlm_worker;

void user_dlm_lock_res_init(struct user_lock_res *lockres,
			    struct dentry *dentry);
int user_dlm_destroy_lock(struct user_lock_res *lockres);
int user_dlm_cluster_lock(struct user_lock_res *lockres,
			  int level,
			  int lkm_flags);
void user_dlm_cluster_unlock(struct user_lock_res *lockres,
			     int level);
void user_dlm_write_lvb(struct inode *inode,
			const char *val,
			unsigned int len);
ssize_t user_dlm_read_lvb(struct inode *inode,
			  char *val,
			  unsigned int len);
struct ocfs2_cluster_connection *user_dlm_register(const struct qstr *name);
void user_dlm_unregister(struct ocfs2_cluster_connection *conn);
void user_dlm_set_locking_protocol(void);

struct dlmfs_inode_private {
	struct ocfs2_cluster_connection	*ip_conn;

	struct user_lock_res ip_lockres; /* unused for directories. */
	struct inode         *ip_parent;

	struct inode         ip_vfs_inode;
};

static inline struct dlmfs_inode_private *
DLMFS_I(struct inode *inode)
{
        return container_of(inode,
			    struct dlmfs_inode_private,
			    ip_vfs_inode);
}

struct dlmfs_filp_private {
	int                  fp_lock_level;
};

#define DLMFS_MAGIC	0x76a9f425

#endif /* USERDLM_H */

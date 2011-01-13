/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * userdlm.c
 *
 * Code which implements the kernel side of a minimal userspace
 * interface to our DLM.
 *
 * Many of the functions here are pared down versions of dlmglue.c
 * functions.
 *
 * Copyright (C) 2003, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/signal.h>

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/crc32.h>

#include "ocfs2_lockingver.h"
#include "stackglue.h"
#include "userdlm.h"

#define MLOG_MASK_PREFIX ML_DLMFS
#include "cluster/masklog.h"


static inline struct user_lock_res *user_lksb_to_lock_res(struct ocfs2_dlm_lksb *lksb)
{
	return container_of(lksb, struct user_lock_res, l_lksb);
}

static inline int user_check_wait_flag(struct user_lock_res *lockres,
				       int flag)
{
	int ret;

	spin_lock(&lockres->l_lock);
	ret = lockres->l_flags & flag;
	spin_unlock(&lockres->l_lock);

	return ret;
}

static inline void user_wait_on_busy_lock(struct user_lock_res *lockres)

{
	wait_event(lockres->l_event,
		   !user_check_wait_flag(lockres, USER_LOCK_BUSY));
}

static inline void user_wait_on_blocked_lock(struct user_lock_res *lockres)

{
	wait_event(lockres->l_event,
		   !user_check_wait_flag(lockres, USER_LOCK_BLOCKED));
}

/* I heart container_of... */
static inline struct ocfs2_cluster_connection *
cluster_connection_from_user_lockres(struct user_lock_res *lockres)
{
	struct dlmfs_inode_private *ip;

	ip = container_of(lockres,
			  struct dlmfs_inode_private,
			  ip_lockres);
	return ip->ip_conn;
}

static struct inode *
user_dlm_inode_from_user_lockres(struct user_lock_res *lockres)
{
	struct dlmfs_inode_private *ip;

	ip = container_of(lockres,
			  struct dlmfs_inode_private,
			  ip_lockres);
	return &ip->ip_vfs_inode;
}

static inline void user_recover_from_dlm_error(struct user_lock_res *lockres)
{
	spin_lock(&lockres->l_lock);
	lockres->l_flags &= ~USER_LOCK_BUSY;
	spin_unlock(&lockres->l_lock);
}

#define user_log_dlm_error(_func, _stat, _lockres) do {			\
	mlog(ML_ERROR, "Dlm error %d while calling %s on "		\
		"resource %.*s\n", _stat, _func,			\
		_lockres->l_namelen, _lockres->l_name); 		\
} while (0)

/* WARNING: This function lives in a world where the only three lock
 * levels are EX, PR, and NL. It *will* have to be adjusted when more
 * lock types are added. */
static inline int user_highest_compat_lock_level(int level)
{
	int new_level = DLM_LOCK_EX;

	if (level == DLM_LOCK_EX)
		new_level = DLM_LOCK_NL;
	else if (level == DLM_LOCK_PR)
		new_level = DLM_LOCK_PR;
	return new_level;
}

static void user_ast(struct ocfs2_dlm_lksb *lksb)
{
	struct user_lock_res *lockres = user_lksb_to_lock_res(lksb);
	int status;

	mlog(ML_BASTS, "AST fired for lockres %.*s, level %d => %d\n",
	     lockres->l_namelen, lockres->l_name, lockres->l_level,
	     lockres->l_requested);

	spin_lock(&lockres->l_lock);

	status = ocfs2_dlm_lock_status(&lockres->l_lksb);
	if (status) {
		mlog(ML_ERROR, "lksb status value of %u on lockres %.*s\n",
		     status, lockres->l_namelen, lockres->l_name);
		spin_unlock(&lockres->l_lock);
		return;
	}

	mlog_bug_on_msg(lockres->l_requested == DLM_LOCK_IV,
			"Lockres %.*s, requested ivmode. flags 0x%x\n",
			lockres->l_namelen, lockres->l_name, lockres->l_flags);

	/* we're downconverting. */
	if (lockres->l_requested < lockres->l_level) {
		if (lockres->l_requested <=
		    user_highest_compat_lock_level(lockres->l_blocking)) {
			lockres->l_blocking = DLM_LOCK_NL;
			lockres->l_flags &= ~USER_LOCK_BLOCKED;
		}
	}

	lockres->l_level = lockres->l_requested;
	lockres->l_requested = DLM_LOCK_IV;
	lockres->l_flags |= USER_LOCK_ATTACHED;
	lockres->l_flags &= ~USER_LOCK_BUSY;

	spin_unlock(&lockres->l_lock);

	wake_up(&lockres->l_event);
}

static inline void user_dlm_grab_inode_ref(struct user_lock_res *lockres)
{
	struct inode *inode;
	inode = user_dlm_inode_from_user_lockres(lockres);
	if (!igrab(inode))
		BUG();
}

static void user_dlm_unblock_lock(struct work_struct *work);

static void __user_dlm_queue_lockres(struct user_lock_res *lockres)
{
	if (!(lockres->l_flags & USER_LOCK_QUEUED)) {
		user_dlm_grab_inode_ref(lockres);

		INIT_WORK(&lockres->l_work, user_dlm_unblock_lock);

		queue_work(user_dlm_worker, &lockres->l_work);
		lockres->l_flags |= USER_LOCK_QUEUED;
	}
}

static void __user_dlm_cond_queue_lockres(struct user_lock_res *lockres)
{
	int queue = 0;

	if (!(lockres->l_flags & USER_LOCK_BLOCKED))
		return;

	switch (lockres->l_blocking) {
	case DLM_LOCK_EX:
		if (!lockres->l_ex_holders && !lockres->l_ro_holders)
			queue = 1;
		break;
	case DLM_LOCK_PR:
		if (!lockres->l_ex_holders)
			queue = 1;
		break;
	default:
		BUG();
	}

	if (queue)
		__user_dlm_queue_lockres(lockres);
}

static void user_bast(struct ocfs2_dlm_lksb *lksb, int level)
{
	struct user_lock_res *lockres = user_lksb_to_lock_res(lksb);

	mlog(ML_BASTS, "BAST fired for lockres %.*s, blocking %d, level %d\n",
	     lockres->l_namelen, lockres->l_name, level, lockres->l_level);

	spin_lock(&lockres->l_lock);
	lockres->l_flags |= USER_LOCK_BLOCKED;
	if (level > lockres->l_blocking)
		lockres->l_blocking = level;

	__user_dlm_queue_lockres(lockres);
	spin_unlock(&lockres->l_lock);

	wake_up(&lockres->l_event);
}

static void user_unlock_ast(struct ocfs2_dlm_lksb *lksb, int status)
{
	struct user_lock_res *lockres = user_lksb_to_lock_res(lksb);

	mlog(ML_BASTS, "UNLOCK AST fired for lockres %.*s, flags 0x%x\n",
	     lockres->l_namelen, lockres->l_name, lockres->l_flags);

	if (status)
		mlog(ML_ERROR, "dlm returns status %d\n", status);

	spin_lock(&lockres->l_lock);
	/* The teardown flag gets set early during the unlock process,
	 * so test the cancel flag to make sure that this ast isn't
	 * for a concurrent cancel. */
	if (lockres->l_flags & USER_LOCK_IN_TEARDOWN
	    && !(lockres->l_flags & USER_LOCK_IN_CANCEL)) {
		lockres->l_level = DLM_LOCK_IV;
	} else if (status == DLM_CANCELGRANT) {
		/* We tried to cancel a convert request, but it was
		 * already granted. Don't clear the busy flag - the
		 * ast should've done this already. */
		BUG_ON(!(lockres->l_flags & USER_LOCK_IN_CANCEL));
		lockres->l_flags &= ~USER_LOCK_IN_CANCEL;
		goto out_noclear;
	} else {
		BUG_ON(!(lockres->l_flags & USER_LOCK_IN_CANCEL));
		/* Cancel succeeded, we want to re-queue */
		lockres->l_requested = DLM_LOCK_IV; /* cancel an
						    * upconvert
						    * request. */
		lockres->l_flags &= ~USER_LOCK_IN_CANCEL;
		/* we want the unblock thread to look at it again
		 * now. */
		if (lockres->l_flags & USER_LOCK_BLOCKED)
			__user_dlm_queue_lockres(lockres);
	}

	lockres->l_flags &= ~USER_LOCK_BUSY;
out_noclear:
	spin_unlock(&lockres->l_lock);

	wake_up(&lockres->l_event);
}

/*
 * This is the userdlmfs locking protocol version.
 *
 * See fs/ocfs2/dlmglue.c for more details on locking versions.
 */
static struct ocfs2_locking_protocol user_dlm_lproto = {
	.lp_max_version = {
		.pv_major = OCFS2_LOCKING_PROTOCOL_MAJOR,
		.pv_minor = OCFS2_LOCKING_PROTOCOL_MINOR,
	},
	.lp_lock_ast		= user_ast,
	.lp_blocking_ast	= user_bast,
	.lp_unlock_ast		= user_unlock_ast,
};

static inline void user_dlm_drop_inode_ref(struct user_lock_res *lockres)
{
	struct inode *inode;
	inode = user_dlm_inode_from_user_lockres(lockres);
	iput(inode);
}

static void user_dlm_unblock_lock(struct work_struct *work)
{
	int new_level, status;
	struct user_lock_res *lockres =
		container_of(work, struct user_lock_res, l_work);
	struct ocfs2_cluster_connection *conn =
		cluster_connection_from_user_lockres(lockres);

	mlog(0, "lockres %.*s\n", lockres->l_namelen, lockres->l_name);

	spin_lock(&lockres->l_lock);

	mlog_bug_on_msg(!(lockres->l_flags & USER_LOCK_QUEUED),
			"Lockres %.*s, flags 0x%x\n",
			lockres->l_namelen, lockres->l_name, lockres->l_flags);

	/* notice that we don't clear USER_LOCK_BLOCKED here. If it's
	 * set, we want user_ast clear it. */
	lockres->l_flags &= ~USER_LOCK_QUEUED;

	/* It's valid to get here and no longer be blocked - if we get
	 * several basts in a row, we might be queued by the first
	 * one, the unblock thread might run and clear the queued
	 * flag, and finally we might get another bast which re-queues
	 * us before our ast for the downconvert is called. */
	if (!(lockres->l_flags & USER_LOCK_BLOCKED)) {
		mlog(ML_BASTS, "lockres %.*s USER_LOCK_BLOCKED\n",
		     lockres->l_namelen, lockres->l_name);
		spin_unlock(&lockres->l_lock);
		goto drop_ref;
	}

	if (lockres->l_flags & USER_LOCK_IN_TEARDOWN) {
		mlog(ML_BASTS, "lockres %.*s USER_LOCK_IN_TEARDOWN\n",
		     lockres->l_namelen, lockres->l_name);
		spin_unlock(&lockres->l_lock);
		goto drop_ref;
	}

	if (lockres->l_flags & USER_LOCK_BUSY) {
		if (lockres->l_flags & USER_LOCK_IN_CANCEL) {
			mlog(ML_BASTS, "lockres %.*s USER_LOCK_IN_CANCEL\n",
			     lockres->l_namelen, lockres->l_name);
			spin_unlock(&lockres->l_lock);
			goto drop_ref;
		}

		lockres->l_flags |= USER_LOCK_IN_CANCEL;
		spin_unlock(&lockres->l_lock);

		status = ocfs2_dlm_unlock(conn, &lockres->l_lksb,
					  DLM_LKF_CANCEL);
		if (status)
			user_log_dlm_error("ocfs2_dlm_unlock", status, lockres);
		goto drop_ref;
	}

	/* If there are still incompat holders, we can exit safely
	 * without worrying about re-queueing this lock as that will
	 * happen on the last call to user_cluster_unlock. */
	if ((lockres->l_blocking == DLM_LOCK_EX)
	    && (lockres->l_ex_holders || lockres->l_ro_holders)) {
		spin_unlock(&lockres->l_lock);
		mlog(ML_BASTS, "lockres %.*s, EX/PR Holders %u,%u\n",
		     lockres->l_namelen, lockres->l_name,
		     lockres->l_ex_holders, lockres->l_ro_holders);
		goto drop_ref;
	}

	if ((lockres->l_blocking == DLM_LOCK_PR)
	    && lockres->l_ex_holders) {
		spin_unlock(&lockres->l_lock);
		mlog(ML_BASTS, "lockres %.*s, EX Holders %u\n",
		     lockres->l_namelen, lockres->l_name,
		     lockres->l_ex_holders);
		goto drop_ref;
	}

	/* yay, we can downconvert now. */
	new_level = user_highest_compat_lock_level(lockres->l_blocking);
	lockres->l_requested = new_level;
	lockres->l_flags |= USER_LOCK_BUSY;
	mlog(ML_BASTS, "lockres %.*s, downconvert %d => %d\n",
	     lockres->l_namelen, lockres->l_name, lockres->l_level, new_level);
	spin_unlock(&lockres->l_lock);

	/* need lock downconvert request now... */
	status = ocfs2_dlm_lock(conn, new_level, &lockres->l_lksb,
				DLM_LKF_CONVERT|DLM_LKF_VALBLK,
				lockres->l_name,
				lockres->l_namelen);
	if (status) {
		user_log_dlm_error("ocfs2_dlm_lock", status, lockres);
		user_recover_from_dlm_error(lockres);
	}

drop_ref:
	user_dlm_drop_inode_ref(lockres);
}

static inline void user_dlm_inc_holders(struct user_lock_res *lockres,
					int level)
{
	switch(level) {
	case DLM_LOCK_EX:
		lockres->l_ex_holders++;
		break;
	case DLM_LOCK_PR:
		lockres->l_ro_holders++;
		break;
	default:
		BUG();
	}
}

/* predict what lock level we'll be dropping down to on behalf
 * of another node, and return true if the currently wanted
 * level will be compatible with it. */
static inline int
user_may_continue_on_blocked_lock(struct user_lock_res *lockres,
				  int wanted)
{
	BUG_ON(!(lockres->l_flags & USER_LOCK_BLOCKED));

	return wanted <= user_highest_compat_lock_level(lockres->l_blocking);
}

int user_dlm_cluster_lock(struct user_lock_res *lockres,
			  int level,
			  int lkm_flags)
{
	int status, local_flags;
	struct ocfs2_cluster_connection *conn =
		cluster_connection_from_user_lockres(lockres);

	if (level != DLM_LOCK_EX &&
	    level != DLM_LOCK_PR) {
		mlog(ML_ERROR, "lockres %.*s: invalid request!\n",
		     lockres->l_namelen, lockres->l_name);
		status = -EINVAL;
		goto bail;
	}

	mlog(ML_BASTS, "lockres %.*s, level %d, flags = 0x%x\n",
	     lockres->l_namelen, lockres->l_name, level, lkm_flags);

again:
	if (signal_pending(current)) {
		status = -ERESTARTSYS;
		goto bail;
	}

	spin_lock(&lockres->l_lock);

	/* We only compare against the currently granted level
	 * here. If the lock is blocked waiting on a downconvert,
	 * we'll get caught below. */
	if ((lockres->l_flags & USER_LOCK_BUSY) &&
	    (level > lockres->l_level)) {
		/* is someone sitting in dlm_lock? If so, wait on
		 * them. */
		spin_unlock(&lockres->l_lock);

		user_wait_on_busy_lock(lockres);
		goto again;
	}

	if ((lockres->l_flags & USER_LOCK_BLOCKED) &&
	    (!user_may_continue_on_blocked_lock(lockres, level))) {
		/* is the lock is currently blocked on behalf of
		 * another node */
		spin_unlock(&lockres->l_lock);

		user_wait_on_blocked_lock(lockres);
		goto again;
	}

	if (level > lockres->l_level) {
		local_flags = lkm_flags | DLM_LKF_VALBLK;
		if (lockres->l_level != DLM_LOCK_IV)
			local_flags |= DLM_LKF_CONVERT;

		lockres->l_requested = level;
		lockres->l_flags |= USER_LOCK_BUSY;
		spin_unlock(&lockres->l_lock);

		BUG_ON(level == DLM_LOCK_IV);
		BUG_ON(level == DLM_LOCK_NL);

		/* call dlm_lock to upgrade lock now */
		status = ocfs2_dlm_lock(conn, level, &lockres->l_lksb,
					local_flags, lockres->l_name,
					lockres->l_namelen);
		if (status) {
			if ((lkm_flags & DLM_LKF_NOQUEUE) &&
			    (status != -EAGAIN))
				user_log_dlm_error("ocfs2_dlm_lock",
						   status, lockres);
			user_recover_from_dlm_error(lockres);
			goto bail;
		}

		user_wait_on_busy_lock(lockres);
		goto again;
	}

	user_dlm_inc_holders(lockres, level);
	spin_unlock(&lockres->l_lock);

	status = 0;
bail:
	return status;
}

static inline void user_dlm_dec_holders(struct user_lock_res *lockres,
					int level)
{
	switch(level) {
	case DLM_LOCK_EX:
		BUG_ON(!lockres->l_ex_holders);
		lockres->l_ex_holders--;
		break;
	case DLM_LOCK_PR:
		BUG_ON(!lockres->l_ro_holders);
		lockres->l_ro_holders--;
		break;
	default:
		BUG();
	}
}

void user_dlm_cluster_unlock(struct user_lock_res *lockres,
			     int level)
{
	if (level != DLM_LOCK_EX &&
	    level != DLM_LOCK_PR) {
		mlog(ML_ERROR, "lockres %.*s: invalid request!\n",
		     lockres->l_namelen, lockres->l_name);
		return;
	}

	spin_lock(&lockres->l_lock);
	user_dlm_dec_holders(lockres, level);
	__user_dlm_cond_queue_lockres(lockres);
	spin_unlock(&lockres->l_lock);
}

void user_dlm_write_lvb(struct inode *inode,
			const char *val,
			unsigned int len)
{
	struct user_lock_res *lockres = &DLMFS_I(inode)->ip_lockres;
	char *lvb;

	BUG_ON(len > DLM_LVB_LEN);

	spin_lock(&lockres->l_lock);

	BUG_ON(lockres->l_level < DLM_LOCK_EX);
	lvb = ocfs2_dlm_lvb(&lockres->l_lksb);
	memcpy(lvb, val, len);

	spin_unlock(&lockres->l_lock);
}

ssize_t user_dlm_read_lvb(struct inode *inode,
			  char *val,
			  unsigned int len)
{
	struct user_lock_res *lockres = &DLMFS_I(inode)->ip_lockres;
	char *lvb;
	ssize_t ret = len;

	BUG_ON(len > DLM_LVB_LEN);

	spin_lock(&lockres->l_lock);

	BUG_ON(lockres->l_level < DLM_LOCK_PR);
	if (ocfs2_dlm_lvb_valid(&lockres->l_lksb)) {
		lvb = ocfs2_dlm_lvb(&lockres->l_lksb);
		memcpy(val, lvb, len);
	} else
		ret = 0;

	spin_unlock(&lockres->l_lock);
	return ret;
}

void user_dlm_lock_res_init(struct user_lock_res *lockres,
			    struct dentry *dentry)
{
	memset(lockres, 0, sizeof(*lockres));

	spin_lock_init(&lockres->l_lock);
	init_waitqueue_head(&lockres->l_event);
	lockres->l_level = DLM_LOCK_IV;
	lockres->l_requested = DLM_LOCK_IV;
	lockres->l_blocking = DLM_LOCK_IV;

	/* should have been checked before getting here. */
	BUG_ON(dentry->d_name.len >= USER_DLM_LOCK_ID_MAX_LEN);

	memcpy(lockres->l_name,
	       dentry->d_name.name,
	       dentry->d_name.len);
	lockres->l_namelen = dentry->d_name.len;
}

int user_dlm_destroy_lock(struct user_lock_res *lockres)
{
	int status = -EBUSY;
	struct ocfs2_cluster_connection *conn =
		cluster_connection_from_user_lockres(lockres);

	mlog(ML_BASTS, "lockres %.*s\n", lockres->l_namelen, lockres->l_name);

	spin_lock(&lockres->l_lock);
	if (lockres->l_flags & USER_LOCK_IN_TEARDOWN) {
		spin_unlock(&lockres->l_lock);
		return 0;
	}

	lockres->l_flags |= USER_LOCK_IN_TEARDOWN;

	while (lockres->l_flags & USER_LOCK_BUSY) {
		spin_unlock(&lockres->l_lock);

		user_wait_on_busy_lock(lockres);

		spin_lock(&lockres->l_lock);
	}

	if (lockres->l_ro_holders || lockres->l_ex_holders) {
		spin_unlock(&lockres->l_lock);
		goto bail;
	}

	status = 0;
	if (!(lockres->l_flags & USER_LOCK_ATTACHED)) {
		spin_unlock(&lockres->l_lock);
		goto bail;
	}

	lockres->l_flags &= ~USER_LOCK_ATTACHED;
	lockres->l_flags |= USER_LOCK_BUSY;
	spin_unlock(&lockres->l_lock);

	status = ocfs2_dlm_unlock(conn, &lockres->l_lksb, DLM_LKF_VALBLK);
	if (status) {
		user_log_dlm_error("ocfs2_dlm_unlock", status, lockres);
		goto bail;
	}

	user_wait_on_busy_lock(lockres);

	status = 0;
bail:
	return status;
}

static void user_dlm_recovery_handler_noop(int node_num,
					   void *recovery_data)
{
	/* We ignore recovery events */
	return;
}

void user_dlm_set_locking_protocol(void)
{
	ocfs2_stack_glue_set_max_proto_version(&user_dlm_lproto.lp_max_version);
}

struct ocfs2_cluster_connection *user_dlm_register(struct qstr *name)
{
	int rc;
	struct ocfs2_cluster_connection *conn;

	rc = ocfs2_cluster_connect_agnostic(name->name, name->len,
					    &user_dlm_lproto,
					    user_dlm_recovery_handler_noop,
					    NULL, &conn);
	if (rc)
		mlog_errno(rc);

	return rc ? ERR_PTR(rc) : conn;
}

void user_dlm_unregister(struct ocfs2_cluster_connection *conn)
{
	ocfs2_cluster_disconnect(conn, 0);
}

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


#include "cluster/nodemanager.h"
#include "cluster/heartbeat.h"
#include "cluster/tcp.h"

#include "dlmapi.h"

#include "userdlm.h"

#define MLOG_MASK_PREFIX ML_DLMFS
#include "cluster/masklog.h"

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
static inline struct dlm_ctxt *
dlm_ctxt_from_user_lockres(struct user_lock_res *lockres)
{
	struct dlmfs_inode_private *ip;

	ip = container_of(lockres,
			  struct dlmfs_inode_private,
			  ip_lockres);
	return ip->ip_dlm;
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

#define user_log_dlm_error(_func, _stat, _lockres) do {		\
	mlog(ML_ERROR, "Dlm error \"%s\" while calling %s on "	\
		"resource %s: %s\n", dlm_errname(_stat), _func,	\
		_lockres->l_name, dlm_errmsg(_stat));		\
} while (0)

/* WARNING: This function lives in a world where the only three lock
 * levels are EX, PR, and NL. It *will* have to be adjusted when more
 * lock types are added. */
static inline int user_highest_compat_lock_level(int level)
{
	int new_level = LKM_EXMODE;

	if (level == LKM_EXMODE)
		new_level = LKM_NLMODE;
	else if (level == LKM_PRMODE)
		new_level = LKM_PRMODE;
	return new_level;
}

static void user_ast(void *opaque)
{
	struct user_lock_res *lockres = opaque;
	struct dlm_lockstatus *lksb;

	mlog(0, "AST fired for lockres %s\n", lockres->l_name);

	spin_lock(&lockres->l_lock);

	lksb = &(lockres->l_lksb);
	if (lksb->status != DLM_NORMAL) {
		mlog(ML_ERROR, "lksb status value of %u on lockres %s\n",
		     lksb->status, lockres->l_name);
		spin_unlock(&lockres->l_lock);
		return;
	}

	/* we're downconverting. */
	if (lockres->l_requested < lockres->l_level) {
		if (lockres->l_requested <=
		    user_highest_compat_lock_level(lockres->l_blocking)) {
			lockres->l_blocking = LKM_NLMODE;
			lockres->l_flags &= ~USER_LOCK_BLOCKED;
		}
	}

	lockres->l_level = lockres->l_requested;
	lockres->l_requested = LKM_IVMODE;
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

static void user_dlm_unblock_lock(void *opaque);

static void __user_dlm_queue_lockres(struct user_lock_res *lockres)
{
	if (!(lockres->l_flags & USER_LOCK_QUEUED)) {
		user_dlm_grab_inode_ref(lockres);

		INIT_WORK(&lockres->l_work, user_dlm_unblock_lock,
			  lockres);

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
	case LKM_EXMODE:
		if (!lockres->l_ex_holders && !lockres->l_ro_holders)
			queue = 1;
		break;
	case LKM_PRMODE:
		if (!lockres->l_ex_holders)
			queue = 1;
		break;
	default:
		BUG();
	}

	if (queue)
		__user_dlm_queue_lockres(lockres);
}

static void user_bast(void *opaque, int level)
{
	struct user_lock_res *lockres = opaque;

	mlog(0, "Blocking AST fired for lockres %s. Blocking level %d\n",
		lockres->l_name, level);

	spin_lock(&lockres->l_lock);
	lockres->l_flags |= USER_LOCK_BLOCKED;
	if (level > lockres->l_blocking)
		lockres->l_blocking = level;

	__user_dlm_queue_lockres(lockres);
	spin_unlock(&lockres->l_lock);

	wake_up(&lockres->l_event);
}

static void user_unlock_ast(void *opaque, enum dlm_status status)
{
	struct user_lock_res *lockres = opaque;

	mlog(0, "UNLOCK AST called on lock %s\n", lockres->l_name);

	if (status != DLM_NORMAL)
		mlog(ML_ERROR, "Dlm returns status %d\n", status);

	spin_lock(&lockres->l_lock);
	if (lockres->l_flags & USER_LOCK_IN_TEARDOWN)
		lockres->l_level = LKM_IVMODE;
	else {
		lockres->l_requested = LKM_IVMODE; /* cancel an
						    * upconvert
						    * request. */
		lockres->l_flags &= ~USER_LOCK_IN_CANCEL;
		/* we want the unblock thread to look at it again
		 * now. */
		__user_dlm_queue_lockres(lockres);
	}

	lockres->l_flags &= ~USER_LOCK_BUSY;
	spin_unlock(&lockres->l_lock);

	wake_up(&lockres->l_event);
}

static inline void user_dlm_drop_inode_ref(struct user_lock_res *lockres)
{
	struct inode *inode;
	inode = user_dlm_inode_from_user_lockres(lockres);
	iput(inode);
}

static void user_dlm_unblock_lock(void *opaque)
{
	int new_level, status;
	struct user_lock_res *lockres = (struct user_lock_res *) opaque;
	struct dlm_ctxt *dlm = dlm_ctxt_from_user_lockres(lockres);

	mlog(0, "processing lockres %s\n", lockres->l_name);

	spin_lock(&lockres->l_lock);

	mlog_bug_on_msg(!(lockres->l_flags & USER_LOCK_QUEUED),
			"Lockres %s, flags 0x%x\n",
			lockres->l_name, lockres->l_flags);

	/* notice that we don't clear USER_LOCK_BLOCKED here. If it's
	 * set, we want user_ast clear it. */
	lockres->l_flags &= ~USER_LOCK_QUEUED;

	/* It's valid to get here and no longer be blocked - if we get
	 * several basts in a row, we might be queued by the first
	 * one, the unblock thread might run and clear the queued
	 * flag, and finally we might get another bast which re-queues
	 * us before our ast for the downconvert is called. */
	if (!(lockres->l_flags & USER_LOCK_BLOCKED)) {
		mlog(0, "Lockres %s, flags 0x%x: queued but not blocking\n",
			lockres->l_name, lockres->l_flags);
		spin_unlock(&lockres->l_lock);
		goto drop_ref;
	}

	if (lockres->l_flags & USER_LOCK_IN_TEARDOWN) {
		mlog(0, "lock is in teardown so we do nothing\n");
		spin_unlock(&lockres->l_lock);
		goto drop_ref;
	}

	if (lockres->l_flags & USER_LOCK_BUSY) {
		mlog(0, "BUSY flag detected...\n");
		if (lockres->l_flags & USER_LOCK_IN_CANCEL) {
			spin_unlock(&lockres->l_lock);
			goto drop_ref;
		}

		lockres->l_flags |= USER_LOCK_IN_CANCEL;
		spin_unlock(&lockres->l_lock);

		status = dlmunlock(dlm,
				   &lockres->l_lksb,
				   LKM_CANCEL,
				   user_unlock_ast,
				   lockres);
		if (status == DLM_CANCELGRANT) {
			/* If we got this, then the ast was fired
			 * before we could cancel. We cleanup our
			 * state, and restart the function. */
			spin_lock(&lockres->l_lock);
			lockres->l_flags &= ~USER_LOCK_IN_CANCEL;
			spin_unlock(&lockres->l_lock);
		} else if (status != DLM_NORMAL)
			user_log_dlm_error("dlmunlock", status, lockres);
		goto drop_ref;
	}

	/* If there are still incompat holders, we can exit safely
	 * without worrying about re-queueing this lock as that will
	 * happen on the last call to user_cluster_unlock. */
	if ((lockres->l_blocking == LKM_EXMODE)
	    && (lockres->l_ex_holders || lockres->l_ro_holders)) {
		spin_unlock(&lockres->l_lock);
		mlog(0, "can't downconvert for ex: ro = %u, ex = %u\n",
			lockres->l_ro_holders, lockres->l_ex_holders);
		goto drop_ref;
	}

	if ((lockres->l_blocking == LKM_PRMODE)
	    && lockres->l_ex_holders) {
		spin_unlock(&lockres->l_lock);
		mlog(0, "can't downconvert for pr: ex = %u\n",
			lockres->l_ex_holders);
		goto drop_ref;
	}

	/* yay, we can downconvert now. */
	new_level = user_highest_compat_lock_level(lockres->l_blocking);
	lockres->l_requested = new_level;
	lockres->l_flags |= USER_LOCK_BUSY;
	mlog(0, "Downconvert lock from %d to %d\n",
		lockres->l_level, new_level);
	spin_unlock(&lockres->l_lock);

	/* need lock downconvert request now... */
	status = dlmlock(dlm,
			 new_level,
			 &lockres->l_lksb,
			 LKM_CONVERT|LKM_VALBLK,
			 lockres->l_name,
			 user_ast,
			 lockres,
			 user_bast);
	if (status != DLM_NORMAL) {
		user_log_dlm_error("dlmlock", status, lockres);
		user_recover_from_dlm_error(lockres);
	}

drop_ref:
	user_dlm_drop_inode_ref(lockres);
}

static inline void user_dlm_inc_holders(struct user_lock_res *lockres,
					int level)
{
	switch(level) {
	case LKM_EXMODE:
		lockres->l_ex_holders++;
		break;
	case LKM_PRMODE:
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
	struct dlm_ctxt *dlm = dlm_ctxt_from_user_lockres(lockres);

	if (level != LKM_EXMODE &&
	    level != LKM_PRMODE) {
		mlog(ML_ERROR, "lockres %s: invalid request!\n",
		     lockres->l_name);
		status = -EINVAL;
		goto bail;
	}

	mlog(0, "lockres %s: asking for %s lock, passed flags = 0x%x\n",
		lockres->l_name,
		(level == LKM_EXMODE) ? "LKM_EXMODE" : "LKM_PRMODE",
		lkm_flags);

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
		local_flags = lkm_flags | LKM_VALBLK;
		if (lockres->l_level != LKM_IVMODE)
			local_flags |= LKM_CONVERT;

		lockres->l_requested = level;
		lockres->l_flags |= USER_LOCK_BUSY;
		spin_unlock(&lockres->l_lock);

		BUG_ON(level == LKM_IVMODE);
		BUG_ON(level == LKM_NLMODE);

		mlog(0, "lock %s, get lock from %d to level = %d\n",
			lockres->l_name, lockres->l_level, level);

		/* call dlm_lock to upgrade lock now */
		status = dlmlock(dlm,
				 level,
				 &lockres->l_lksb,
				 local_flags,
				 lockres->l_name,
				 user_ast,
				 lockres,
				 user_bast);
		if (status != DLM_NORMAL) {
			if ((lkm_flags & LKM_NOQUEUE) &&
			    (status == DLM_NOTQUEUED))
				status = -EAGAIN;
			else {
				user_log_dlm_error("dlmlock", status, lockres);
				status = -EINVAL;
			}
			user_recover_from_dlm_error(lockres);
			goto bail;
		}

		mlog(0, "lock %s, successfull return from dlmlock\n",
			lockres->l_name);

		user_wait_on_busy_lock(lockres);
		goto again;
	}

	user_dlm_inc_holders(lockres, level);
	spin_unlock(&lockres->l_lock);

	mlog(0, "lockres %s: Got %s lock!\n", lockres->l_name,
		(level == LKM_EXMODE) ? "LKM_EXMODE" : "LKM_PRMODE");

	status = 0;
bail:
	return status;
}

static inline void user_dlm_dec_holders(struct user_lock_res *lockres,
					int level)
{
	switch(level) {
	case LKM_EXMODE:
		BUG_ON(!lockres->l_ex_holders);
		lockres->l_ex_holders--;
		break;
	case LKM_PRMODE:
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
	if (level != LKM_EXMODE &&
	    level != LKM_PRMODE) {
		mlog(ML_ERROR, "lockres %s: invalid request!\n", lockres->l_name);
		return;
	}

	mlog(0, "lockres %s: dropping %s lock\n", lockres->l_name,
		(level == LKM_EXMODE) ? "LKM_EXMODE" : "LKM_PRMODE");

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
	char *lvb = lockres->l_lksb.lvb;

	BUG_ON(len > DLM_LVB_LEN);

	spin_lock(&lockres->l_lock);

	BUG_ON(lockres->l_level < LKM_EXMODE);
	memcpy(lvb, val, len);

	spin_unlock(&lockres->l_lock);
}

void user_dlm_read_lvb(struct inode *inode,
		       char *val,
		       unsigned int len)
{
	struct user_lock_res *lockres = &DLMFS_I(inode)->ip_lockres;
	char *lvb = lockres->l_lksb.lvb;

	BUG_ON(len > DLM_LVB_LEN);

	spin_lock(&lockres->l_lock);

	BUG_ON(lockres->l_level < LKM_PRMODE);
	memcpy(val, lvb, len);

	spin_unlock(&lockres->l_lock);
}

void user_dlm_lock_res_init(struct user_lock_res *lockres,
			    struct dentry *dentry)
{
	memset(lockres, 0, sizeof(*lockres));

	spin_lock_init(&lockres->l_lock);
	init_waitqueue_head(&lockres->l_event);
	lockres->l_level = LKM_IVMODE;
	lockres->l_requested = LKM_IVMODE;
	lockres->l_blocking = LKM_IVMODE;

	/* should have been checked before getting here. */
	BUG_ON(dentry->d_name.len >= USER_DLM_LOCK_ID_MAX_LEN);

	memcpy(lockres->l_name,
	       dentry->d_name.name,
	       dentry->d_name.len);
}

int user_dlm_destroy_lock(struct user_lock_res *lockres)
{
	int status = -EBUSY;
	struct dlm_ctxt *dlm = dlm_ctxt_from_user_lockres(lockres);

	mlog(0, "asked to destroy %s\n", lockres->l_name);

	spin_lock(&lockres->l_lock);
	while (lockres->l_flags & USER_LOCK_BUSY) {
		spin_unlock(&lockres->l_lock);

		mlog(0, "lock %s is busy\n", lockres->l_name);

		user_wait_on_busy_lock(lockres);

		spin_lock(&lockres->l_lock);
	}

	if (lockres->l_ro_holders || lockres->l_ex_holders) {
		spin_unlock(&lockres->l_lock);
		mlog(0, "lock %s has holders\n", lockres->l_name);
		goto bail;
	}

	status = 0;
	if (!(lockres->l_flags & USER_LOCK_ATTACHED)) {
		spin_unlock(&lockres->l_lock);
		mlog(0, "lock %s is not attached\n", lockres->l_name);
		goto bail;
	}

	lockres->l_flags &= ~USER_LOCK_ATTACHED;
	lockres->l_flags |= USER_LOCK_BUSY;
	lockres->l_flags |= USER_LOCK_IN_TEARDOWN;
	spin_unlock(&lockres->l_lock);

	mlog(0, "unlocking lockres %s\n", lockres->l_name);
	status = dlmunlock(dlm,
			   &lockres->l_lksb,
			   LKM_VALBLK,
			   user_unlock_ast,
			   lockres);
	if (status != DLM_NORMAL) {
		user_log_dlm_error("dlmunlock", status, lockres);
		status = -EINVAL;
		goto bail;
	}

	user_wait_on_busy_lock(lockres);

	status = 0;
bail:
	return status;
}

struct dlm_ctxt *user_dlm_register_context(struct qstr *name)
{
	struct dlm_ctxt *dlm;
	u32 dlm_key;
	char *domain;

	domain = kmalloc(name->len + 1, GFP_KERNEL);
	if (!domain) {
		mlog_errno(-ENOMEM);
		return ERR_PTR(-ENOMEM);
	}

	dlm_key = crc32_le(0, name->name, name->len);

	snprintf(domain, name->len + 1, "%.*s", name->len, name->name);

	dlm = dlm_register_domain(domain, dlm_key);
	if (IS_ERR(dlm))
		mlog_errno(PTR_ERR(dlm));

	kfree(domain);
	return dlm;
}

void user_dlm_unregister_context(struct dlm_ctxt *dlm)
{
	dlm_unregister_domain(dlm);
}

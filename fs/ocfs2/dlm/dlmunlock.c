/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmunlock.c
 *
 * underlying calls for unlocking locks
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
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
 *
 */


#include <linux/module.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/sysctl.h>
#include <linux/random.h>
#include <linux/blkdev.h>
#include <linux/socket.h>
#include <linux/inet.h>
#include <linux/spinlock.h>
#include <linux/delay.h>

#include "cluster/heartbeat.h"
#include "cluster/nodemanager.h"
#include "cluster/tcp.h"

#include "dlmapi.h"
#include "dlmcommon.h"

#define MLOG_MASK_PREFIX ML_DLM
#include "cluster/masklog.h"

#define DLM_UNLOCK_FREE_LOCK           0x00000001
#define DLM_UNLOCK_CALL_AST            0x00000002
#define DLM_UNLOCK_REMOVE_LOCK         0x00000004
#define DLM_UNLOCK_REGRANT_LOCK        0x00000008
#define DLM_UNLOCK_CLEAR_CONVERT_TYPE  0x00000010


static enum dlm_status dlm_get_cancel_actions(struct dlm_ctxt *dlm,
					      struct dlm_lock_resource *res,
					      struct dlm_lock *lock,
					      struct dlm_lockstatus *lksb,
					      int *actions);
static enum dlm_status dlm_get_unlock_actions(struct dlm_ctxt *dlm,
					      struct dlm_lock_resource *res,
					      struct dlm_lock *lock,
					      struct dlm_lockstatus *lksb,
					      int *actions);

static enum dlm_status dlm_send_remote_unlock_request(struct dlm_ctxt *dlm,
						 struct dlm_lock_resource *res,
						 struct dlm_lock *lock,
						 struct dlm_lockstatus *lksb,
						 int flags,
						 u8 owner);


/*
 * according to the spec:
 * http://opendlm.sourceforge.net/cvsmirror/opendlm/docs/dlmbook_final.pdf
 *
 *  flags & LKM_CANCEL != 0: must be converting or blocked
 *  flags & LKM_CANCEL == 0: must be granted
 *
 * So to unlock a converting lock, you must first cancel the
 * convert (passing LKM_CANCEL in flags), then call the unlock
 * again (with no LKM_CANCEL in flags).
 */


/*
 * locking:
 *   caller needs:  none
 *   taken:         res->spinlock and lock->spinlock taken and dropped
 *   held on exit:  none
 * returns: DLM_NORMAL, DLM_NOLOCKMGR, status from network
 * all callers should have taken an extra ref on lock coming in
 */
static enum dlm_status dlmunlock_common(struct dlm_ctxt *dlm,
					struct dlm_lock_resource *res,
					struct dlm_lock *lock,
					struct dlm_lockstatus *lksb,
					int flags, int *call_ast,
					int master_node)
{
	enum dlm_status status;
	int actions = 0;
	int in_use;
        u8 owner;

	mlog(0, "master_node = %d, valblk = %d\n", master_node,
	     flags & LKM_VALBLK);

	if (master_node)
		BUG_ON(res->owner != dlm->node_num);
	else
		BUG_ON(res->owner == dlm->node_num);

	spin_lock(&dlm->ast_lock);
	/* We want to be sure that we're not freeing a lock
	 * that still has AST's pending... */
	in_use = !list_empty(&lock->ast_list);
	spin_unlock(&dlm->ast_lock);
	if (in_use && !(flags & LKM_CANCEL)) {
	       mlog(ML_ERROR, "lockres %.*s: Someone is calling dlmunlock "
		    "while waiting for an ast!", res->lockname.len,
		    res->lockname.name);
		return DLM_BADPARAM;
	}

	spin_lock(&res->spinlock);
	if (res->state & DLM_LOCK_RES_IN_PROGRESS) {
		if (master_node && !(flags & LKM_CANCEL)) {
			mlog(ML_ERROR, "lockres in progress!\n");
			spin_unlock(&res->spinlock);
			return DLM_FORWARD;
		}
		/* ok for this to sleep if not in a network handler */
		__dlm_wait_on_lockres(res);
		res->state |= DLM_LOCK_RES_IN_PROGRESS;
	}
	spin_lock(&lock->spinlock);

	if (res->state & DLM_LOCK_RES_RECOVERING) {
		status = DLM_RECOVERING;
		goto leave;
	}

	if (res->state & DLM_LOCK_RES_MIGRATING) {
		status = DLM_MIGRATING;
		goto leave;
	}

	/* see above for what the spec says about
	 * LKM_CANCEL and the lock queue state */
	if (flags & LKM_CANCEL)
		status = dlm_get_cancel_actions(dlm, res, lock, lksb, &actions);
	else
		status = dlm_get_unlock_actions(dlm, res, lock, lksb, &actions);

	if (status != DLM_NORMAL && (status != DLM_CANCELGRANT || !master_node))
		goto leave;

	/* By now this has been masked out of cancel requests. */
	if (flags & LKM_VALBLK) {
		/* make the final update to the lvb */
		if (master_node)
			memcpy(res->lvb, lksb->lvb, DLM_LVB_LEN);
		else
			flags |= LKM_PUT_LVB; /* let the send function
					       * handle it. */
	}

	if (!master_node) {
		owner = res->owner;
		/* drop locks and send message */
		if (flags & LKM_CANCEL)
			lock->cancel_pending = 1;
		else
			lock->unlock_pending = 1;
		spin_unlock(&lock->spinlock);
		spin_unlock(&res->spinlock);
		status = dlm_send_remote_unlock_request(dlm, res, lock, lksb,
							flags, owner);
		spin_lock(&res->spinlock);
		spin_lock(&lock->spinlock);
		/* if the master told us the lock was already granted,
		 * let the ast handle all of these actions */
		if (status == DLM_CANCELGRANT) {
			actions &= ~(DLM_UNLOCK_REMOVE_LOCK|
				     DLM_UNLOCK_REGRANT_LOCK|
				     DLM_UNLOCK_CLEAR_CONVERT_TYPE);
		} else if (status == DLM_RECOVERING ||
			   status == DLM_MIGRATING ||
			   status == DLM_FORWARD) {
			/* must clear the actions because this unlock
			 * is about to be retried.  cannot free or do
			 * any list manipulation. */
			mlog(0, "%s:%.*s: clearing actions, %s\n",
			     dlm->name, res->lockname.len,
			     res->lockname.name,
			     status==DLM_RECOVERING?"recovering":
			     (status==DLM_MIGRATING?"migrating":
			      "forward"));
			actions = 0;
		}
		if (flags & LKM_CANCEL)
			lock->cancel_pending = 0;
		else
			lock->unlock_pending = 0;

	}

	/* get an extra ref on lock.  if we are just switching
	 * lists here, we dont want the lock to go away. */
	dlm_lock_get(lock);

	if (actions & DLM_UNLOCK_REMOVE_LOCK) {
		list_del_init(&lock->list);
		dlm_lock_put(lock);
	}
	if (actions & DLM_UNLOCK_REGRANT_LOCK) {
		dlm_lock_get(lock);
		list_add_tail(&lock->list, &res->granted);
	}
	if (actions & DLM_UNLOCK_CLEAR_CONVERT_TYPE) {
		mlog(0, "clearing convert_type at %smaster node\n",
		     master_node ? "" : "non-");
		lock->ml.convert_type = LKM_IVMODE;
	}

	/* remove the extra ref on lock */
	dlm_lock_put(lock);

leave:
	res->state &= ~DLM_LOCK_RES_IN_PROGRESS;
	if (!dlm_lock_on_list(&res->converting, lock))
		BUG_ON(lock->ml.convert_type != LKM_IVMODE);
	else
		BUG_ON(lock->ml.convert_type == LKM_IVMODE);
	spin_unlock(&lock->spinlock);
	spin_unlock(&res->spinlock);
	wake_up(&res->wq);

	/* let the caller's final dlm_lock_put handle the actual kfree */
	if (actions & DLM_UNLOCK_FREE_LOCK) {
		/* this should always be coupled with list removal */
		BUG_ON(!(actions & DLM_UNLOCK_REMOVE_LOCK));
		mlog(0, "lock %u:%llu should be gone now! refs=%d\n",
		     dlm_get_lock_cookie_node(be64_to_cpu(lock->ml.cookie)),
		     dlm_get_lock_cookie_seq(be64_to_cpu(lock->ml.cookie)),
		     atomic_read(&lock->lock_refs.refcount)-1);
		dlm_lock_put(lock);
	}
	if (actions & DLM_UNLOCK_CALL_AST)
		*call_ast = 1;

	/* if cancel or unlock succeeded, lvb work is done */
	if (status == DLM_NORMAL)
		lksb->flags &= ~(DLM_LKSB_PUT_LVB|DLM_LKSB_GET_LVB);

	return status;
}

void dlm_commit_pending_unlock(struct dlm_lock_resource *res,
			       struct dlm_lock *lock)
{
	/* leave DLM_LKSB_PUT_LVB on the lksb so any final
	 * update of the lvb will be sent to the new master */
	list_del_init(&lock->list);
}

void dlm_commit_pending_cancel(struct dlm_lock_resource *res,
			       struct dlm_lock *lock)
{
	list_move_tail(&lock->list, &res->granted);
	lock->ml.convert_type = LKM_IVMODE;
}


static inline enum dlm_status dlmunlock_master(struct dlm_ctxt *dlm,
					  struct dlm_lock_resource *res,
					  struct dlm_lock *lock,
					  struct dlm_lockstatus *lksb,
					  int flags,
					  int *call_ast)
{
	return dlmunlock_common(dlm, res, lock, lksb, flags, call_ast, 1);
}

static inline enum dlm_status dlmunlock_remote(struct dlm_ctxt *dlm,
					  struct dlm_lock_resource *res,
					  struct dlm_lock *lock,
					  struct dlm_lockstatus *lksb,
					  int flags, int *call_ast)
{
	return dlmunlock_common(dlm, res, lock, lksb, flags, call_ast, 0);
}

/*
 * locking:
 *   caller needs:  none
 *   taken:         none
 *   held on exit:  none
 * returns: DLM_NORMAL, DLM_NOLOCKMGR, status from network
 */
static enum dlm_status dlm_send_remote_unlock_request(struct dlm_ctxt *dlm,
						 struct dlm_lock_resource *res,
						 struct dlm_lock *lock,
						 struct dlm_lockstatus *lksb,
						 int flags,
						 u8 owner)
{
	struct dlm_unlock_lock unlock;
	int tmpret;
	enum dlm_status ret;
	int status = 0;
	struct kvec vec[2];
	size_t veclen = 1;

	mlog(0, "%.*s\n", res->lockname.len, res->lockname.name);

	if (owner == dlm->node_num) {
		/* ended up trying to contact ourself.  this means
		 * that the lockres had been remote but became local
		 * via a migration.  just retry it, now as local */
		mlog(0, "%s:%.*s: this node became the master due to a "
		     "migration, re-evaluate now\n", dlm->name,
		     res->lockname.len, res->lockname.name);
		return DLM_FORWARD;
	}

	memset(&unlock, 0, sizeof(unlock));
	unlock.node_idx = dlm->node_num;
	unlock.flags = cpu_to_be32(flags);
	unlock.cookie = lock->ml.cookie;
	unlock.namelen = res->lockname.len;
	memcpy(unlock.name, res->lockname.name, unlock.namelen);

	vec[0].iov_len = sizeof(struct dlm_unlock_lock);
	vec[0].iov_base = &unlock;

	if (flags & LKM_PUT_LVB) {
		/* extra data to send if we are updating lvb */
		vec[1].iov_len = DLM_LVB_LEN;
		vec[1].iov_base = lock->lksb->lvb;
		veclen++;
	}

	tmpret = o2net_send_message_vec(DLM_UNLOCK_LOCK_MSG, dlm->key,
					vec, veclen, owner, &status);
	if (tmpret >= 0) {
		// successfully sent and received
		if (status == DLM_FORWARD)
			mlog(0, "master was in-progress.  retry\n");
		ret = status;
	} else {
		mlog(ML_ERROR, "Error %d when sending message %u (key 0x%x) to "
		     "node %u\n", tmpret, DLM_UNLOCK_LOCK_MSG, dlm->key, owner);
		if (dlm_is_host_down(tmpret)) {
			/* NOTE: this seems strange, but it is what we want.
			 * when the master goes down during a cancel or
			 * unlock, the recovery code completes the operation
			 * as if the master had not died, then passes the
			 * updated state to the recovery master.  this thread
			 * just needs to finish out the operation and call
			 * the unlockast. */
			ret = DLM_NORMAL;
		} else {
			/* something bad.  this will BUG in ocfs2 */
			ret = dlm_err_to_dlm_status(tmpret);
		}
	}

	return ret;
}

/*
 * locking:
 *   caller needs:  none
 *   taken:         takes and drops res->spinlock
 *   held on exit:  none
 * returns: DLM_NORMAL, DLM_BADARGS, DLM_IVLOCKID,
 *          return value from dlmunlock_master
 */
int dlm_unlock_lock_handler(struct o2net_msg *msg, u32 len, void *data,
			    void **ret_data)
{
	struct dlm_ctxt *dlm = data;
	struct dlm_unlock_lock *unlock = (struct dlm_unlock_lock *)msg->buf;
	struct dlm_lock_resource *res = NULL;
	struct dlm_lock *lock = NULL;
	enum dlm_status status = DLM_NORMAL;
	int found = 0, i;
	struct dlm_lockstatus *lksb = NULL;
	int ignore;
	u32 flags;
	struct list_head *queue;

	flags = be32_to_cpu(unlock->flags);

	if (flags & LKM_GET_LVB) {
		mlog(ML_ERROR, "bad args!  GET_LVB specified on unlock!\n");
		return DLM_BADARGS;
	}

	if ((flags & (LKM_PUT_LVB|LKM_CANCEL)) == (LKM_PUT_LVB|LKM_CANCEL)) {
		mlog(ML_ERROR, "bad args!  cannot modify lvb on a CANCEL "
		     "request!\n");
		return DLM_BADARGS;
	}

	if (unlock->namelen > DLM_LOCKID_NAME_MAX) {
		mlog(ML_ERROR, "Invalid name length in unlock handler!\n");
		return DLM_IVBUFLEN;
	}

	if (!dlm_grab(dlm))
		return DLM_REJECTED;

	mlog_bug_on_msg(!dlm_domain_fully_joined(dlm),
			"Domain %s not fully joined!\n", dlm->name);

	mlog(0, "lvb: %s\n", flags & LKM_PUT_LVB ? "put lvb" : "none");

	res = dlm_lookup_lockres(dlm, unlock->name, unlock->namelen);
	if (!res) {
		/* We assume here that a no lock resource simply means
		 * it was migrated away and destroyed before the other
		 * node could detect it. */
		mlog(0, "returning DLM_FORWARD -- res no longer exists\n");
		status = DLM_FORWARD;
		goto not_found;
	}

	queue=&res->granted;
	found = 0;
	spin_lock(&res->spinlock);
	if (res->state & DLM_LOCK_RES_RECOVERING) {
		spin_unlock(&res->spinlock);
		mlog(0, "returning DLM_RECOVERING\n");
		status = DLM_RECOVERING;
		goto leave;
	}

	if (res->state & DLM_LOCK_RES_MIGRATING) {
		spin_unlock(&res->spinlock);
		mlog(0, "returning DLM_MIGRATING\n");
		status = DLM_MIGRATING;
		goto leave;
	}

	if (res->owner != dlm->node_num) {
		spin_unlock(&res->spinlock);
		mlog(0, "returning DLM_FORWARD -- not master\n");
		status = DLM_FORWARD;
		goto leave;
	}

	for (i=0; i<3; i++) {
		list_for_each_entry(lock, queue, list) {
			if (lock->ml.cookie == unlock->cookie &&
		    	    lock->ml.node == unlock->node_idx) {
				dlm_lock_get(lock);
				found = 1;
				break;
			}
		}
		if (found)
			break;
		/* scan granted -> converting -> blocked queues */
		queue++;
	}
	spin_unlock(&res->spinlock);
	if (!found) {
		status = DLM_IVLOCKID;
		goto not_found;
	}

	/* lock was found on queue */
	lksb = lock->lksb;
	if (flags & (LKM_VALBLK|LKM_PUT_LVB) &&
	    lock->ml.type != LKM_EXMODE)
		flags &= ~(LKM_VALBLK|LKM_PUT_LVB);

	/* unlockast only called on originating node */
	if (flags & LKM_PUT_LVB) {
		lksb->flags |= DLM_LKSB_PUT_LVB;
		memcpy(&lksb->lvb[0], &unlock->lvb[0], DLM_LVB_LEN);
	}

	/* if this is in-progress, propagate the DLM_FORWARD
	 * all the way back out */
	status = dlmunlock_master(dlm, res, lock, lksb, flags, &ignore);
	if (status == DLM_FORWARD)
		mlog(0, "lockres is in progress\n");

	if (flags & LKM_PUT_LVB)
		lksb->flags &= ~DLM_LKSB_PUT_LVB;

	dlm_lockres_calc_usage(dlm, res);
	dlm_kick_thread(dlm, res);

not_found:
	if (!found)
		mlog(ML_ERROR, "failed to find lock to unlock! "
			       "cookie=%u:%llu\n",
		     dlm_get_lock_cookie_node(be64_to_cpu(unlock->cookie)),
		     dlm_get_lock_cookie_seq(be64_to_cpu(unlock->cookie)));
	else
		dlm_lock_put(lock);

leave:
	if (res)
		dlm_lockres_put(res);

	dlm_put(dlm);

	return status;
}


static enum dlm_status dlm_get_cancel_actions(struct dlm_ctxt *dlm,
					      struct dlm_lock_resource *res,
					      struct dlm_lock *lock,
					      struct dlm_lockstatus *lksb,
					      int *actions)
{
	enum dlm_status status;

	if (dlm_lock_on_list(&res->blocked, lock)) {
		/* cancel this outright */
		status = DLM_NORMAL;
		*actions = (DLM_UNLOCK_CALL_AST |
			    DLM_UNLOCK_REMOVE_LOCK);
	} else if (dlm_lock_on_list(&res->converting, lock)) {
		/* cancel the request, put back on granted */
		status = DLM_NORMAL;
		*actions = (DLM_UNLOCK_CALL_AST |
			    DLM_UNLOCK_REMOVE_LOCK |
			    DLM_UNLOCK_REGRANT_LOCK |
			    DLM_UNLOCK_CLEAR_CONVERT_TYPE);
	} else if (dlm_lock_on_list(&res->granted, lock)) {
		/* too late, already granted. */
		status = DLM_CANCELGRANT;
		*actions = DLM_UNLOCK_CALL_AST;
	} else {
		mlog(ML_ERROR, "lock to cancel is not on any list!\n");
		status = DLM_IVLOCKID;
		*actions = 0;
	}
	return status;
}

static enum dlm_status dlm_get_unlock_actions(struct dlm_ctxt *dlm,
					      struct dlm_lock_resource *res,
					      struct dlm_lock *lock,
					      struct dlm_lockstatus *lksb,
					      int *actions)
{
	enum dlm_status status;

	/* unlock request */
	if (!dlm_lock_on_list(&res->granted, lock)) {
		status = DLM_DENIED;
		dlm_error(status);
		*actions = 0;
	} else {
		/* unlock granted lock */
		status = DLM_NORMAL;
		*actions = (DLM_UNLOCK_FREE_LOCK |
			    DLM_UNLOCK_CALL_AST |
			    DLM_UNLOCK_REMOVE_LOCK);
	}
	return status;
}

/* there seems to be no point in doing this async
 * since (even for the remote case) there is really
 * no work to queue up... so just do it and fire the
 * unlockast by hand when done... */
enum dlm_status dlmunlock(struct dlm_ctxt *dlm, struct dlm_lockstatus *lksb,
			  int flags, dlm_astunlockfunc_t *unlockast, void *data)
{
	enum dlm_status status;
	struct dlm_lock_resource *res;
	struct dlm_lock *lock = NULL;
	int call_ast, is_master;

	if (!lksb) {
		dlm_error(DLM_BADARGS);
		return DLM_BADARGS;
	}

	if (flags & ~(LKM_CANCEL | LKM_VALBLK | LKM_INVVALBLK)) {
		dlm_error(DLM_BADPARAM);
		return DLM_BADPARAM;
	}

	if ((flags & (LKM_VALBLK | LKM_CANCEL)) == (LKM_VALBLK | LKM_CANCEL)) {
		mlog(0, "VALBLK given with CANCEL: ignoring VALBLK\n");
		flags &= ~LKM_VALBLK;
	}

	if (!lksb->lockid || !lksb->lockid->lockres) {
		dlm_error(DLM_BADPARAM);
		return DLM_BADPARAM;
	}

	lock = lksb->lockid;
	BUG_ON(!lock);
	dlm_lock_get(lock);

	res = lock->lockres;
	BUG_ON(!res);
	dlm_lockres_get(res);
retry:
	call_ast = 0;
	/* need to retry up here because owner may have changed */
	mlog(0, "lock=%p res=%p\n", lock, res);

	spin_lock(&res->spinlock);
	is_master = (res->owner == dlm->node_num);
	if (flags & LKM_VALBLK && lock->ml.type != LKM_EXMODE)
		flags &= ~LKM_VALBLK;
	spin_unlock(&res->spinlock);

	if (is_master) {
		status = dlmunlock_master(dlm, res, lock, lksb, flags,
					  &call_ast);
		mlog(0, "done calling dlmunlock_master: returned %d, "
		     "call_ast is %d\n", status, call_ast);
	} else {
		status = dlmunlock_remote(dlm, res, lock, lksb, flags,
					  &call_ast);
		mlog(0, "done calling dlmunlock_remote: returned %d, "
		     "call_ast is %d\n", status, call_ast);
	}

	if (status == DLM_RECOVERING ||
	    status == DLM_MIGRATING ||
	    status == DLM_FORWARD) {
		/* We want to go away for a tiny bit to allow recovery
		 * / migration to complete on this resource. I don't
		 * know of any wait queue we could sleep on as this
		 * may be happening on another node. Perhaps the
		 * proper solution is to queue up requests on the
		 * other end? */

		/* do we want to yield(); ?? */
		msleep(50);

		mlog(0, "retrying unlock due to pending recovery/"
		     "migration/in-progress\n");
		goto retry;
	}

	if (call_ast) {
		mlog(0, "calling unlockast(%p, %d)\n", data, status);
		if (is_master) {
			/* it is possible that there is one last bast
			 * pending.  make sure it is flushed, then
			 * call the unlockast.
			 * not an issue if this is a mastered remotely,
			 * since this lock has been removed from the
			 * lockres queues and cannot be found. */
			dlm_kick_thread(dlm, NULL);
			wait_event(dlm->ast_wq,
				   dlm_lock_basts_flushed(dlm, lock));
		}
		(*unlockast)(data, status);
	}

	if (status == DLM_CANCELGRANT)
		status = DLM_NORMAL;

	if (status == DLM_NORMAL) {
		mlog(0, "kicking the thread\n");
		dlm_kick_thread(dlm, res);
	} else
		dlm_error(status);

	dlm_lockres_calc_usage(dlm, res);
	dlm_lockres_put(res);
	dlm_lock_put(lock);

	mlog(0, "returning status=%d!\n", status);
	return status;
}
EXPORT_SYMBOL_GPL(dlmunlock);


/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmlock.c
 *
 * underlying calls for lock creation
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
#include <linux/slab.h>
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

#include "dlmconvert.h"

#define MLOG_MASK_PREFIX ML_DLM
#include "cluster/masklog.h"

static struct kmem_cache *dlm_lock_cache = NULL;

static DEFINE_SPINLOCK(dlm_cookie_lock);
static u64 dlm_next_cookie = 1;

static enum dlm_status dlm_send_remote_lock_request(struct dlm_ctxt *dlm,
					       struct dlm_lock_resource *res,
					       struct dlm_lock *lock, int flags);
static void dlm_init_lock(struct dlm_lock *newlock, int type,
			  u8 node, u64 cookie);
static void dlm_lock_release(struct kref *kref);
static void dlm_lock_detach_lockres(struct dlm_lock *lock);

int dlm_init_lock_cache(void)
{
	dlm_lock_cache = kmem_cache_create("o2dlm_lock",
					   sizeof(struct dlm_lock),
					   0, SLAB_HWCACHE_ALIGN, NULL);
	if (dlm_lock_cache == NULL)
		return -ENOMEM;
	return 0;
}

void dlm_destroy_lock_cache(void)
{
	if (dlm_lock_cache)
		kmem_cache_destroy(dlm_lock_cache);
}

/* Tell us whether we can grant a new lock request.
 * locking:
 *   caller needs:  res->spinlock
 *   taken:         none
 *   held on exit:  none
 * returns: 1 if the lock can be granted, 0 otherwise.
 */
static int dlm_can_grant_new_lock(struct dlm_lock_resource *res,
				  struct dlm_lock *lock)
{
	struct list_head *iter;
	struct dlm_lock *tmplock;

	list_for_each(iter, &res->granted) {
		tmplock = list_entry(iter, struct dlm_lock, list);

		if (!dlm_lock_compatible(tmplock->ml.type, lock->ml.type))
			return 0;
	}

	list_for_each(iter, &res->converting) {
		tmplock = list_entry(iter, struct dlm_lock, list);

		if (!dlm_lock_compatible(tmplock->ml.type, lock->ml.type))
			return 0;
		if (!dlm_lock_compatible(tmplock->ml.convert_type,
					 lock->ml.type))
			return 0;
	}

	return 1;
}

/* performs lock creation at the lockres master site
 * locking:
 *   caller needs:  none
 *   taken:         takes and drops res->spinlock
 *   held on exit:  none
 * returns: DLM_NORMAL, DLM_NOTQUEUED
 */
static enum dlm_status dlmlock_master(struct dlm_ctxt *dlm,
				      struct dlm_lock_resource *res,
				      struct dlm_lock *lock, int flags)
{
	int call_ast = 0, kick_thread = 0;
	enum dlm_status status = DLM_NORMAL;

	mlog_entry("type=%d\n", lock->ml.type);

	spin_lock(&res->spinlock);
	/* if called from dlm_create_lock_handler, need to
	 * ensure it will not sleep in dlm_wait_on_lockres */
	status = __dlm_lockres_state_to_status(res);
	if (status != DLM_NORMAL &&
	    lock->ml.node != dlm->node_num) {
		/* erf.  state changed after lock was dropped. */
		spin_unlock(&res->spinlock);
		dlm_error(status);
		return status;
	}
	__dlm_wait_on_lockres(res);
	__dlm_lockres_reserve_ast(res);

	if (dlm_can_grant_new_lock(res, lock)) {
		mlog(0, "I can grant this lock right away\n");
		/* got it right away */
		lock->lksb->status = DLM_NORMAL;
		status = DLM_NORMAL;
		dlm_lock_get(lock);
		list_add_tail(&lock->list, &res->granted);

		/* for the recovery lock, we can't allow the ast
		 * to be queued since the dlmthread is already
		 * frozen.  but the recovery lock is always locked
		 * with LKM_NOQUEUE so we do not need the ast in
		 * this special case */
		if (!dlm_is_recovery_lock(res->lockname.name,
					  res->lockname.len)) {
			kick_thread = 1;
			call_ast = 1;
		} else {
			mlog(0, "%s: returning DLM_NORMAL to "
			     "node %u for reco lock\n", dlm->name,
			     lock->ml.node);
		}
	} else {
		/* for NOQUEUE request, unless we get the
		 * lock right away, return DLM_NOTQUEUED */
		if (flags & LKM_NOQUEUE) {
			status = DLM_NOTQUEUED;
			if (dlm_is_recovery_lock(res->lockname.name,
						 res->lockname.len)) {
				mlog(0, "%s: returning NOTQUEUED to "
				     "node %u for reco lock\n", dlm->name,
				     lock->ml.node);
			}
		} else {
			dlm_lock_get(lock);
			list_add_tail(&lock->list, &res->blocked);
			kick_thread = 1;
		}
	}
	/* reduce the inflight count, this may result in the lockres
	 * being purged below during calc_usage */
	if (lock->ml.node == dlm->node_num)
		dlm_lockres_drop_inflight_ref(dlm, res);

	spin_unlock(&res->spinlock);
	wake_up(&res->wq);

	/* either queue the ast or release it */
	if (call_ast)
		dlm_queue_ast(dlm, lock);
	else
		dlm_lockres_release_ast(dlm, res);

	dlm_lockres_calc_usage(dlm, res);
	if (kick_thread)
		dlm_kick_thread(dlm, res);

	return status;
}

void dlm_revert_pending_lock(struct dlm_lock_resource *res,
			     struct dlm_lock *lock)
{
	/* remove from local queue if it failed */
	list_del_init(&lock->list);
	lock->lksb->flags &= ~DLM_LKSB_GET_LVB;
}


/*
 * locking:
 *   caller needs:  none
 *   taken:         takes and drops res->spinlock
 *   held on exit:  none
 * returns: DLM_DENIED, DLM_RECOVERING, or net status
 */
static enum dlm_status dlmlock_remote(struct dlm_ctxt *dlm,
				      struct dlm_lock_resource *res,
				      struct dlm_lock *lock, int flags)
{
	enum dlm_status status = DLM_DENIED;
	int lockres_changed = 1;

	mlog_entry("type=%d\n", lock->ml.type);
	mlog(0, "lockres %.*s, flags = 0x%x\n", res->lockname.len,
	     res->lockname.name, flags);

	spin_lock(&res->spinlock);

	/* will exit this call with spinlock held */
	__dlm_wait_on_lockres(res);
	res->state |= DLM_LOCK_RES_IN_PROGRESS;

	/* add lock to local (secondary) queue */
	dlm_lock_get(lock);
	list_add_tail(&lock->list, &res->blocked);
	lock->lock_pending = 1;
	spin_unlock(&res->spinlock);

	/* spec seems to say that you will get DLM_NORMAL when the lock
	 * has been queued, meaning we need to wait for a reply here. */
	status = dlm_send_remote_lock_request(dlm, res, lock, flags);

	spin_lock(&res->spinlock);
	res->state &= ~DLM_LOCK_RES_IN_PROGRESS;
	lock->lock_pending = 0;
	if (status != DLM_NORMAL) {
		if (status == DLM_RECOVERING &&
		    dlm_is_recovery_lock(res->lockname.name,
					 res->lockname.len)) {
			/* recovery lock was mastered by dead node.
			 * we need to have calc_usage shoot down this
			 * lockres and completely remaster it. */
			mlog(0, "%s: recovery lock was owned by "
			     "dead node %u, remaster it now.\n",
			     dlm->name, res->owner);
		} else if (status != DLM_NOTQUEUED) {
			/*
			 * DO NOT call calc_usage, as this would unhash
			 * the remote lockres before we ever get to use
			 * it.  treat as if we never made any change to
			 * the lockres.
			 */
			lockres_changed = 0;
			dlm_error(status);
		}
		dlm_revert_pending_lock(res, lock);
		dlm_lock_put(lock);
	} else if (dlm_is_recovery_lock(res->lockname.name,
					res->lockname.len)) {
		/* special case for the $RECOVERY lock.
		 * there will never be an AST delivered to put
		 * this lock on the proper secondary queue
		 * (granted), so do it manually. */
		mlog(0, "%s: $RECOVERY lock for this node (%u) is "
		     "mastered by %u; got lock, manually granting (no ast)\n",
		     dlm->name, dlm->node_num, res->owner);
		list_move_tail(&lock->list, &res->granted);
	}
	spin_unlock(&res->spinlock);

	if (lockres_changed)
		dlm_lockres_calc_usage(dlm, res);

	wake_up(&res->wq);
	return status;
}


/* for remote lock creation.
 * locking:
 *   caller needs:  none, but need res->state & DLM_LOCK_RES_IN_PROGRESS
 *   taken:         none
 *   held on exit:  none
 * returns: DLM_NOLOCKMGR, or net status
 */
static enum dlm_status dlm_send_remote_lock_request(struct dlm_ctxt *dlm,
					       struct dlm_lock_resource *res,
					       struct dlm_lock *lock, int flags)
{
	struct dlm_create_lock create;
	int tmpret, status = 0;
	enum dlm_status ret;

	mlog_entry_void();

	memset(&create, 0, sizeof(create));
	create.node_idx = dlm->node_num;
	create.requested_type = lock->ml.type;
	create.cookie = lock->ml.cookie;
	create.namelen = res->lockname.len;
	create.flags = cpu_to_be32(flags);
	memcpy(create.name, res->lockname.name, create.namelen);

	tmpret = o2net_send_message(DLM_CREATE_LOCK_MSG, dlm->key, &create,
				    sizeof(create), res->owner, &status);
	if (tmpret >= 0) {
		// successfully sent and received
		ret = status;  // this is already a dlm_status
		if (ret == DLM_REJECTED) {
			mlog(ML_ERROR, "%s:%.*s: BUG.  this is a stale lockres "
			     "no longer owned by %u.  that node is coming back "
			     "up currently.\n", dlm->name, create.namelen,
			     create.name, res->owner);
			dlm_print_one_lock_resource(res);
			BUG();
		}
	} else {
		mlog(ML_ERROR, "Error %d when sending message %u (key 0x%x) to "
		     "node %u\n", tmpret, DLM_CREATE_LOCK_MSG, dlm->key,
		     res->owner);
		if (dlm_is_host_down(tmpret)) {
			ret = DLM_RECOVERING;
			mlog(0, "node %u died so returning DLM_RECOVERING "
			     "from lock message!\n", res->owner);
		} else {
			ret = dlm_err_to_dlm_status(tmpret);
		}
	}

	return ret;
}

void dlm_lock_get(struct dlm_lock *lock)
{
	kref_get(&lock->lock_refs);
}

void dlm_lock_put(struct dlm_lock *lock)
{
	kref_put(&lock->lock_refs, dlm_lock_release);
}

static void dlm_lock_release(struct kref *kref)
{
	struct dlm_lock *lock;

	lock = container_of(kref, struct dlm_lock, lock_refs);

	BUG_ON(!list_empty(&lock->list));
	BUG_ON(!list_empty(&lock->ast_list));
	BUG_ON(!list_empty(&lock->bast_list));
	BUG_ON(lock->ast_pending);
	BUG_ON(lock->bast_pending);

	dlm_lock_detach_lockres(lock);

	if (lock->lksb_kernel_allocated) {
		mlog(0, "freeing kernel-allocated lksb\n");
		kfree(lock->lksb);
	}
	kmem_cache_free(dlm_lock_cache, lock);
}

/* associate a lock with it's lockres, getting a ref on the lockres */
void dlm_lock_attach_lockres(struct dlm_lock *lock,
			     struct dlm_lock_resource *res)
{
	dlm_lockres_get(res);
	lock->lockres = res;
}

/* drop ref on lockres, if there is still one associated with lock */
static void dlm_lock_detach_lockres(struct dlm_lock *lock)
{
	struct dlm_lock_resource *res;

	res = lock->lockres;
	if (res) {
		lock->lockres = NULL;
		mlog(0, "removing lock's lockres reference\n");
		dlm_lockres_put(res);
	}
}

static void dlm_init_lock(struct dlm_lock *newlock, int type,
			  u8 node, u64 cookie)
{
	INIT_LIST_HEAD(&newlock->list);
	INIT_LIST_HEAD(&newlock->ast_list);
	INIT_LIST_HEAD(&newlock->bast_list);
	spin_lock_init(&newlock->spinlock);
	newlock->ml.type = type;
	newlock->ml.convert_type = LKM_IVMODE;
	newlock->ml.highest_blocked = LKM_IVMODE;
	newlock->ml.node = node;
	newlock->ml.pad1 = 0;
	newlock->ml.list = 0;
	newlock->ml.flags = 0;
	newlock->ast = NULL;
	newlock->bast = NULL;
	newlock->astdata = NULL;
	newlock->ml.cookie = cpu_to_be64(cookie);
	newlock->ast_pending = 0;
	newlock->bast_pending = 0;
	newlock->convert_pending = 0;
	newlock->lock_pending = 0;
	newlock->unlock_pending = 0;
	newlock->cancel_pending = 0;
	newlock->lksb_kernel_allocated = 0;

	kref_init(&newlock->lock_refs);
}

struct dlm_lock * dlm_new_lock(int type, u8 node, u64 cookie,
			       struct dlm_lockstatus *lksb)
{
	struct dlm_lock *lock;
	int kernel_allocated = 0;

	lock = kmem_cache_zalloc(dlm_lock_cache, GFP_NOFS);
	if (!lock)
		return NULL;

	if (!lksb) {
		/* zero memory only if kernel-allocated */
		lksb = kzalloc(sizeof(*lksb), GFP_NOFS);
		if (!lksb) {
			kfree(lock);
			return NULL;
		}
		kernel_allocated = 1;
	}

	dlm_init_lock(lock, type, node, cookie);
	if (kernel_allocated)
		lock->lksb_kernel_allocated = 1;
	lock->lksb = lksb;
	lksb->lockid = lock;
	return lock;
}

/* handler for lock creation net message
 * locking:
 *   caller needs:  none
 *   taken:         takes and drops res->spinlock
 *   held on exit:  none
 * returns: DLM_NORMAL, DLM_SYSERR, DLM_IVLOCKID, DLM_NOTQUEUED
 */
int dlm_create_lock_handler(struct o2net_msg *msg, u32 len, void *data,
			    void **ret_data)
{
	struct dlm_ctxt *dlm = data;
	struct dlm_create_lock *create = (struct dlm_create_lock *)msg->buf;
	struct dlm_lock_resource *res = NULL;
	struct dlm_lock *newlock = NULL;
	struct dlm_lockstatus *lksb = NULL;
	enum dlm_status status = DLM_NORMAL;
	char *name;
	unsigned int namelen;

	BUG_ON(!dlm);

	mlog_entry_void();

	if (!dlm_grab(dlm))
		return DLM_REJECTED;

	name = create->name;
	namelen = create->namelen;
	status = DLM_REJECTED;
	if (!dlm_domain_fully_joined(dlm)) {
		mlog(ML_ERROR, "Domain %s not fully joined, but node %u is "
		     "sending a create_lock message for lock %.*s!\n",
		     dlm->name, create->node_idx, namelen, name);
		dlm_error(status);
		goto leave;
	}

	status = DLM_IVBUFLEN;
	if (namelen > DLM_LOCKID_NAME_MAX) {
		dlm_error(status);
		goto leave;
	}

	status = DLM_SYSERR;
	newlock = dlm_new_lock(create->requested_type,
			       create->node_idx,
			       be64_to_cpu(create->cookie), NULL);
	if (!newlock) {
		dlm_error(status);
		goto leave;
	}

	lksb = newlock->lksb;

	if (be32_to_cpu(create->flags) & LKM_GET_LVB) {
		lksb->flags |= DLM_LKSB_GET_LVB;
		mlog(0, "set DLM_LKSB_GET_LVB flag\n");
	}

	status = DLM_IVLOCKID;
	res = dlm_lookup_lockres(dlm, name, namelen);
	if (!res) {
		dlm_error(status);
		goto leave;
	}

	spin_lock(&res->spinlock);
	status = __dlm_lockres_state_to_status(res);
	spin_unlock(&res->spinlock);

	if (status != DLM_NORMAL) {
		mlog(0, "lockres recovering/migrating/in-progress\n");
		goto leave;
	}

	dlm_lock_attach_lockres(newlock, res);

	status = dlmlock_master(dlm, res, newlock, be32_to_cpu(create->flags));
leave:
	if (status != DLM_NORMAL)
		if (newlock)
			dlm_lock_put(newlock);

	if (res)
		dlm_lockres_put(res);

	dlm_put(dlm);

	return status;
}


/* fetch next node-local (u8 nodenum + u56 cookie) into u64 */
static inline void dlm_get_next_cookie(u8 node_num, u64 *cookie)
{
	u64 tmpnode = node_num;

	/* shift single byte of node num into top 8 bits */
	tmpnode <<= 56;

	spin_lock(&dlm_cookie_lock);
	*cookie = (dlm_next_cookie | tmpnode);
	if (++dlm_next_cookie & 0xff00000000000000ull) {
		mlog(0, "This node's cookie will now wrap!\n");
		dlm_next_cookie = 1;
	}
	spin_unlock(&dlm_cookie_lock);
}

enum dlm_status dlmlock(struct dlm_ctxt *dlm, int mode,
			struct dlm_lockstatus *lksb, int flags,
			const char *name, int namelen, dlm_astlockfunc_t *ast,
			void *data, dlm_bastlockfunc_t *bast)
{
	enum dlm_status status;
	struct dlm_lock_resource *res = NULL;
	struct dlm_lock *lock = NULL;
	int convert = 0, recovery = 0;

	/* yes this function is a mess.
	 * TODO: clean this up.  lots of common code in the
	 *       lock and convert paths, especially in the retry blocks */
	if (!lksb) {
		dlm_error(DLM_BADARGS);
		return DLM_BADARGS;
	}

	status = DLM_BADPARAM;
	if (mode != LKM_EXMODE && mode != LKM_PRMODE && mode != LKM_NLMODE) {
		dlm_error(status);
		goto error;
	}

	if (flags & ~LKM_VALID_FLAGS) {
		dlm_error(status);
		goto error;
	}

	convert = (flags & LKM_CONVERT);
	recovery = (flags & LKM_RECOVERY);

	if (recovery &&
	    (!dlm_is_recovery_lock(name, namelen) || convert) ) {
		dlm_error(status);
		goto error;
	}
	if (convert && (flags & LKM_LOCAL)) {
		mlog(ML_ERROR, "strange LOCAL convert request!\n");
		goto error;
	}

	if (convert) {
		/* CONVERT request */

		/* if converting, must pass in a valid dlm_lock */
		lock = lksb->lockid;
		if (!lock) {
			mlog(ML_ERROR, "NULL lock pointer in convert "
			     "request\n");
			goto error;
		}

		res = lock->lockres;
		if (!res) {
			mlog(ML_ERROR, "NULL lockres pointer in convert "
			     "request\n");
			goto error;
		}
		dlm_lockres_get(res);

		/* XXX: for ocfs2 purposes, the ast/bast/astdata/lksb are
	 	 * static after the original lock call.  convert requests will
		 * ensure that everything is the same, or return DLM_BADARGS.
	 	 * this means that DLM_DENIED_NOASTS will never be returned.
	 	 */
		if (lock->lksb != lksb || lock->ast != ast ||
		    lock->bast != bast || lock->astdata != data) {
			status = DLM_BADARGS;
			mlog(ML_ERROR, "new args:  lksb=%p, ast=%p, bast=%p, "
			     "astdata=%p\n", lksb, ast, bast, data);
			mlog(ML_ERROR, "orig args: lksb=%p, ast=%p, bast=%p, "
			     "astdata=%p\n", lock->lksb, lock->ast,
			     lock->bast, lock->astdata);
			goto error;
		}
retry_convert:
		dlm_wait_for_recovery(dlm);

		if (res->owner == dlm->node_num)
			status = dlmconvert_master(dlm, res, lock, flags, mode);
		else
			status = dlmconvert_remote(dlm, res, lock, flags, mode);
		if (status == DLM_RECOVERING || status == DLM_MIGRATING ||
		    status == DLM_FORWARD) {
			/* for now, see how this works without sleeping
			 * and just retry right away.  I suspect the reco
			 * or migration will complete fast enough that
			 * no waiting will be necessary */
			mlog(0, "retrying convert with migration/recovery/"
			     "in-progress\n");
			msleep(100);
			goto retry_convert;
		}
	} else {
		u64 tmpcookie;

		/* LOCK request */
		status = DLM_BADARGS;
		if (!name) {
			dlm_error(status);
			goto error;
		}

		status = DLM_IVBUFLEN;
		if (namelen > DLM_LOCKID_NAME_MAX || namelen < 1) {
			dlm_error(status);
			goto error;
		}

		dlm_get_next_cookie(dlm->node_num, &tmpcookie);
		lock = dlm_new_lock(mode, dlm->node_num, tmpcookie, lksb);
		if (!lock) {
			dlm_error(status);
			goto error;
		}

		if (!recovery)
			dlm_wait_for_recovery(dlm);

		/* find or create the lock resource */
		res = dlm_get_lock_resource(dlm, name, namelen, flags);
		if (!res) {
			status = DLM_IVLOCKID;
			dlm_error(status);
			goto error;
		}

		mlog(0, "type=%d, flags = 0x%x\n", mode, flags);
		mlog(0, "creating lock: lock=%p res=%p\n", lock, res);

		dlm_lock_attach_lockres(lock, res);
		lock->ast = ast;
		lock->bast = bast;
		lock->astdata = data;

retry_lock:
		if (flags & LKM_VALBLK) {
			mlog(0, "LKM_VALBLK passed by caller\n");

			/* LVB requests for non PR, PW or EX locks are
			 * ignored. */
			if (mode < LKM_PRMODE)
				flags &= ~LKM_VALBLK;
			else {
				flags |= LKM_GET_LVB;
				lock->lksb->flags |= DLM_LKSB_GET_LVB;
			}
		}

		if (res->owner == dlm->node_num)
			status = dlmlock_master(dlm, res, lock, flags);
		else
			status = dlmlock_remote(dlm, res, lock, flags);

		if (status == DLM_RECOVERING || status == DLM_MIGRATING ||
		    status == DLM_FORWARD) {
			mlog(0, "retrying lock with migration/"
			     "recovery/in progress\n");
			msleep(100);
			/* no waiting for dlm_reco_thread */
			if (recovery) {
				if (status != DLM_RECOVERING)
					goto retry_lock;

				mlog(0, "%s: got RECOVERING "
				     "for $RECOVERY lock, master "
				     "was %u\n", dlm->name,
				     res->owner);
				/* wait to see the node go down, then
				 * drop down and allow the lockres to
				 * get cleaned up.  need to remaster. */
				dlm_wait_for_node_death(dlm, res->owner,
						DLM_NODE_DEATH_WAIT_MAX);
			} else {
				dlm_wait_for_recovery(dlm);
				goto retry_lock;
			}
		}

		if (status != DLM_NORMAL) {
			lock->lksb->flags &= ~DLM_LKSB_GET_LVB;
			if (status != DLM_NOTQUEUED)
				dlm_error(status);
			goto error;
		}
	}

error:
	if (status != DLM_NORMAL) {
		if (lock && !convert)
			dlm_lock_put(lock);
		// this is kind of unnecessary
		lksb->status = status;
	}

	/* put lockres ref from the convert path
	 * or from dlm_get_lock_resource */
	if (res)
		dlm_lockres_put(res);

	return status;
}
EXPORT_SYMBOL_GPL(dlmlock);

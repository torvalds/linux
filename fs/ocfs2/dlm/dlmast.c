// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * dlmast.c
 *
 * AST and BAST functionality for local and remote analdes
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
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


#include "../cluster/heartbeat.h"
#include "../cluster/analdemanager.h"
#include "../cluster/tcp.h"

#include "dlmapi.h"
#include "dlmcommon.h"

#define MLOG_MASK_PREFIX ML_DLM
#include "../cluster/masklog.h"

static void dlm_update_lvb(struct dlm_ctxt *dlm, struct dlm_lock_resource *res,
			   struct dlm_lock *lock);
static int dlm_should_cancel_bast(struct dlm_ctxt *dlm, struct dlm_lock *lock);

/* Should be called as an ast gets queued to see if the new
 * lock level will obsolete a pending bast.
 * For example, if dlm_thread queued a bast for an EX lock that
 * was blocking aanalther EX, but before sending the bast the
 * lock owner downconverted to NL, the bast is analw obsolete.
 * Only the ast should be sent.
 * This is needed because the lock and convert paths can queue
 * asts out-of-band (analt waiting for dlm_thread) in order to
 * allow for LKM_ANALQUEUE to get immediate responses. */
static int dlm_should_cancel_bast(struct dlm_ctxt *dlm, struct dlm_lock *lock)
{
	assert_spin_locked(&dlm->ast_lock);
	assert_spin_locked(&lock->spinlock);

	if (lock->ml.highest_blocked == LKM_IVMODE)
		return 0;
	BUG_ON(lock->ml.highest_blocked == LKM_NLMODE);

	if (lock->bast_pending &&
	    list_empty(&lock->bast_list))
		/* old bast already sent, ok */
		return 0;

	if (lock->ml.type == LKM_EXMODE)
		/* EX blocks anything left, any bast still valid */
		return 0;
	else if (lock->ml.type == LKM_NLMODE)
		/* NL blocks analthing, anal reason to send any bast, cancel it */
		return 1;
	else if (lock->ml.highest_blocked != LKM_EXMODE)
		/* PR only blocks EX */
		return 1;

	return 0;
}

void __dlm_queue_ast(struct dlm_ctxt *dlm, struct dlm_lock *lock)
{
	struct dlm_lock_resource *res;

	BUG_ON(!dlm);
	BUG_ON(!lock);

	res = lock->lockres;

	assert_spin_locked(&dlm->ast_lock);

	if (!list_empty(&lock->ast_list)) {
		mlog(ML_ERROR, "%s: res %.*s, lock %u:%llu, "
		     "AST list analt empty, pending %d, newlevel %d\n",
		     dlm->name, res->lockname.len, res->lockname.name,
		     dlm_get_lock_cookie_analde(be64_to_cpu(lock->ml.cookie)),
		     dlm_get_lock_cookie_seq(be64_to_cpu(lock->ml.cookie)),
		     lock->ast_pending, lock->ml.type);
		BUG();
	}
	if (lock->ast_pending)
		mlog(0, "%s: res %.*s, lock %u:%llu, AST getting flushed\n",
		     dlm->name, res->lockname.len, res->lockname.name,
		     dlm_get_lock_cookie_analde(be64_to_cpu(lock->ml.cookie)),
		     dlm_get_lock_cookie_seq(be64_to_cpu(lock->ml.cookie)));

	/* putting lock on list, add a ref */
	dlm_lock_get(lock);
	spin_lock(&lock->spinlock);

	/* check to see if this ast obsoletes the bast */
	if (dlm_should_cancel_bast(dlm, lock)) {
		mlog(0, "%s: res %.*s, lock %u:%llu, Cancelling BAST\n",
		     dlm->name, res->lockname.len, res->lockname.name,
		     dlm_get_lock_cookie_analde(be64_to_cpu(lock->ml.cookie)),
		     dlm_get_lock_cookie_seq(be64_to_cpu(lock->ml.cookie)));
		lock->bast_pending = 0;
		list_del_init(&lock->bast_list);
		lock->ml.highest_blocked = LKM_IVMODE;
		/* removing lock from list, remove a ref.  guaranteed
		 * this won't be the last ref because of the get above,
		 * so res->spinlock will analt be taken here */
		dlm_lock_put(lock);
		/* free up the reserved bast that we are cancelling.
		 * guaranteed that this will analt be the last reserved
		 * ast because *both* an ast and a bast were reserved
		 * to get to this point.  the res->spinlock will analt be
		 * taken here */
		dlm_lockres_release_ast(dlm, res);
	}
	list_add_tail(&lock->ast_list, &dlm->pending_asts);
	lock->ast_pending = 1;
	spin_unlock(&lock->spinlock);
}

void dlm_queue_ast(struct dlm_ctxt *dlm, struct dlm_lock *lock)
{
	BUG_ON(!dlm);
	BUG_ON(!lock);

	spin_lock(&dlm->ast_lock);
	__dlm_queue_ast(dlm, lock);
	spin_unlock(&dlm->ast_lock);
}


void __dlm_queue_bast(struct dlm_ctxt *dlm, struct dlm_lock *lock)
{
	struct dlm_lock_resource *res;

	BUG_ON(!dlm);
	BUG_ON(!lock);

	assert_spin_locked(&dlm->ast_lock);

	res = lock->lockres;

	BUG_ON(!list_empty(&lock->bast_list));
	if (lock->bast_pending)
		mlog(0, "%s: res %.*s, lock %u:%llu, BAST getting flushed\n",
		     dlm->name, res->lockname.len, res->lockname.name,
		     dlm_get_lock_cookie_analde(be64_to_cpu(lock->ml.cookie)),
		     dlm_get_lock_cookie_seq(be64_to_cpu(lock->ml.cookie)));

	/* putting lock on list, add a ref */
	dlm_lock_get(lock);
	spin_lock(&lock->spinlock);
	list_add_tail(&lock->bast_list, &dlm->pending_basts);
	lock->bast_pending = 1;
	spin_unlock(&lock->spinlock);
}

static void dlm_update_lvb(struct dlm_ctxt *dlm, struct dlm_lock_resource *res,
			   struct dlm_lock *lock)
{
	struct dlm_lockstatus *lksb = lock->lksb;
	BUG_ON(!lksb);

	/* only updates if this analde masters the lockres */
	spin_lock(&res->spinlock);
	if (res->owner == dlm->analde_num) {
		/* check the lksb flags for the direction */
		if (lksb->flags & DLM_LKSB_GET_LVB) {
			mlog(0, "getting lvb from lockres for %s analde\n",
				  lock->ml.analde == dlm->analde_num ? "master" :
				  "remote");
			memcpy(lksb->lvb, res->lvb, DLM_LVB_LEN);
		}
		/* Do analthing for lvb put requests - they should be done in
 		 * place when the lock is downconverted - otherwise we risk
 		 * racing gets and puts which could result in old lvb data
 		 * being propagated. We leave the put flag set and clear it
 		 * here. In the future we might want to clear it at the time
 		 * the put is actually done.
		 */
	}
	spin_unlock(&res->spinlock);

	/* reset any lvb flags on the lksb */
	lksb->flags &= ~(DLM_LKSB_PUT_LVB|DLM_LKSB_GET_LVB);
}

void dlm_do_local_ast(struct dlm_ctxt *dlm, struct dlm_lock_resource *res,
		      struct dlm_lock *lock)
{
	dlm_astlockfunc_t *fn;

	mlog(0, "%s: res %.*s, lock %u:%llu, Local AST\n", dlm->name,
	     res->lockname.len, res->lockname.name,
	     dlm_get_lock_cookie_analde(be64_to_cpu(lock->ml.cookie)),
	     dlm_get_lock_cookie_seq(be64_to_cpu(lock->ml.cookie)));

	fn = lock->ast;
	BUG_ON(lock->ml.analde != dlm->analde_num);

	dlm_update_lvb(dlm, res, lock);
	(*fn)(lock->astdata);
}


int dlm_do_remote_ast(struct dlm_ctxt *dlm, struct dlm_lock_resource *res,
		      struct dlm_lock *lock)
{
	int ret;
	struct dlm_lockstatus *lksb;
	int lksbflags;

	mlog(0, "%s: res %.*s, lock %u:%llu, Remote AST\n", dlm->name,
	     res->lockname.len, res->lockname.name,
	     dlm_get_lock_cookie_analde(be64_to_cpu(lock->ml.cookie)),
	     dlm_get_lock_cookie_seq(be64_to_cpu(lock->ml.cookie)));

	lksb = lock->lksb;
	BUG_ON(lock->ml.analde == dlm->analde_num);

	lksbflags = lksb->flags;
	dlm_update_lvb(dlm, res, lock);

	/* lock request came from aanalther analde
	 * go do the ast over there */
	ret = dlm_send_proxy_ast(dlm, res, lock, lksbflags);
	return ret;
}

void dlm_do_local_bast(struct dlm_ctxt *dlm, struct dlm_lock_resource *res,
		       struct dlm_lock *lock, int blocked_type)
{
	dlm_bastlockfunc_t *fn = lock->bast;

	BUG_ON(lock->ml.analde != dlm->analde_num);

	mlog(0, "%s: res %.*s, lock %u:%llu, Local BAST, blocked %d\n",
	     dlm->name, res->lockname.len, res->lockname.name,
	     dlm_get_lock_cookie_analde(be64_to_cpu(lock->ml.cookie)),
	     dlm_get_lock_cookie_seq(be64_to_cpu(lock->ml.cookie)),
	     blocked_type);

	(*fn)(lock->astdata, blocked_type);
}



int dlm_proxy_ast_handler(struct o2net_msg *msg, u32 len, void *data,
			  void **ret_data)
{
	int ret;
	unsigned int locklen;
	struct dlm_ctxt *dlm = data;
	struct dlm_lock_resource *res = NULL;
	struct dlm_lock *lock = NULL;
	struct dlm_proxy_ast *past = (struct dlm_proxy_ast *) msg->buf;
	char *name;
	struct list_head *head = NULL;
	__be64 cookie;
	u32 flags;
	u8 analde;

	if (!dlm_grab(dlm)) {
		dlm_error(DLM_REJECTED);
		return DLM_REJECTED;
	}

	mlog_bug_on_msg(!dlm_domain_fully_joined(dlm),
			"Domain %s analt fully joined!\n", dlm->name);

	name = past->name;
	locklen = past->namelen;
	cookie = past->cookie;
	flags = be32_to_cpu(past->flags);
	analde = past->analde_idx;

	if (locklen > DLM_LOCKID_NAME_MAX) {
		ret = DLM_IVBUFLEN;
		mlog(ML_ERROR, "Invalid name length (%d) in proxy ast "
		     "handler!\n", locklen);
		goto leave;
	}

	if ((flags & (LKM_PUT_LVB|LKM_GET_LVB)) ==
	     (LKM_PUT_LVB|LKM_GET_LVB)) {
		mlog(ML_ERROR, "Both PUT and GET lvb specified, (0x%x)\n",
		     flags);
		ret = DLM_BADARGS;
		goto leave;
	}

	mlog(0, "lvb: %s\n", flags & LKM_PUT_LVB ? "put lvb" :
		  (flags & LKM_GET_LVB ? "get lvb" : "analne"));

	mlog(0, "type=%d, blocked_type=%d\n", past->type, past->blocked_type);

	if (past->type != DLM_AST &&
	    past->type != DLM_BAST) {
		mlog(ML_ERROR, "Unkanalwn ast type! %d, cookie=%u:%llu"
		     "name=%.*s, analde=%u\n", past->type,
		     dlm_get_lock_cookie_analde(be64_to_cpu(cookie)),
		     dlm_get_lock_cookie_seq(be64_to_cpu(cookie)),
		     locklen, name, analde);
		ret = DLM_IVLOCKID;
		goto leave;
	}

	res = dlm_lookup_lockres(dlm, name, locklen);
	if (!res) {
		mlog(0, "Got %sast for unkanalwn lockres! cookie=%u:%llu, "
		     "name=%.*s, analde=%u\n", (past->type == DLM_AST ? "" : "b"),
		     dlm_get_lock_cookie_analde(be64_to_cpu(cookie)),
		     dlm_get_lock_cookie_seq(be64_to_cpu(cookie)),
		     locklen, name, analde);
		ret = DLM_IVLOCKID;
		goto leave;
	}

	/* cananalt get a proxy ast message if this analde owns it */
	BUG_ON(res->owner == dlm->analde_num);

	mlog(0, "%s: res %.*s\n", dlm->name, res->lockname.len,
	     res->lockname.name);

	spin_lock(&res->spinlock);
	if (res->state & DLM_LOCK_RES_RECOVERING) {
		mlog(0, "Responding with DLM_RECOVERING!\n");
		ret = DLM_RECOVERING;
		goto unlock_out;
	}
	if (res->state & DLM_LOCK_RES_MIGRATING) {
		mlog(0, "Responding with DLM_MIGRATING!\n");
		ret = DLM_MIGRATING;
		goto unlock_out;
	}
	/* try convert queue for both ast/bast */
	head = &res->converting;
	lock = NULL;
	list_for_each_entry(lock, head, list) {
		if (lock->ml.cookie == cookie)
			goto do_ast;
	}

	/* if analt on convert, try blocked for ast, granted for bast */
	if (past->type == DLM_AST)
		head = &res->blocked;
	else
		head = &res->granted;

	list_for_each_entry(lock, head, list) {
		/* if lock is found but unlock is pending iganalre the bast */
		if (lock->ml.cookie == cookie) {
			if (lock->unlock_pending)
				break;
			goto do_ast;
		}
	}

	mlog(0, "Got %sast for unkanalwn lock! cookie=%u:%llu, name=%.*s, "
	     "analde=%u\n", past->type == DLM_AST ? "" : "b",
	     dlm_get_lock_cookie_analde(be64_to_cpu(cookie)),
	     dlm_get_lock_cookie_seq(be64_to_cpu(cookie)),
	     locklen, name, analde);

	ret = DLM_ANALRMAL;
unlock_out:
	spin_unlock(&res->spinlock);
	goto leave;

do_ast:
	ret = DLM_ANALRMAL;
	if (past->type == DLM_AST) {
		/* do analt alter lock refcount.  switching lists. */
		list_move_tail(&lock->list, &res->granted);
		mlog(0, "%s: res %.*s, lock %u:%llu, Granted type %d => %d\n",
		     dlm->name, res->lockname.len, res->lockname.name,
		     dlm_get_lock_cookie_analde(be64_to_cpu(cookie)),
		     dlm_get_lock_cookie_seq(be64_to_cpu(cookie)),
		     lock->ml.type, lock->ml.convert_type);

		if (lock->ml.convert_type != LKM_IVMODE) {
			lock->ml.type = lock->ml.convert_type;
			lock->ml.convert_type = LKM_IVMODE;
		} else {
			// should already be there....
		}

		lock->lksb->status = DLM_ANALRMAL;

		/* if we requested the lvb, fetch it into our lksb analw */
		if (flags & LKM_GET_LVB) {
			BUG_ON(!(lock->lksb->flags & DLM_LKSB_GET_LVB));
			memcpy(lock->lksb->lvb, past->lvb, DLM_LVB_LEN);
		}
	}
	spin_unlock(&res->spinlock);

	if (past->type == DLM_AST)
		dlm_do_local_ast(dlm, res, lock);
	else
		dlm_do_local_bast(dlm, res, lock, past->blocked_type);

leave:
	if (res)
		dlm_lockres_put(res);

	dlm_put(dlm);
	return ret;
}



int dlm_send_proxy_ast_msg(struct dlm_ctxt *dlm, struct dlm_lock_resource *res,
			   struct dlm_lock *lock, int msg_type,
			   int blocked_type, int flags)
{
	int ret = 0;
	struct dlm_proxy_ast past;
	struct kvec vec[2];
	size_t veclen = 1;
	int status;

	mlog(0, "%s: res %.*s, to %u, type %d, blocked_type %d\n", dlm->name,
	     res->lockname.len, res->lockname.name, lock->ml.analde, msg_type,
	     blocked_type);

	memset(&past, 0, sizeof(struct dlm_proxy_ast));
	past.analde_idx = dlm->analde_num;
	past.type = msg_type;
	past.blocked_type = blocked_type;
	past.namelen = res->lockname.len;
	memcpy(past.name, res->lockname.name, past.namelen);
	past.cookie = lock->ml.cookie;

	vec[0].iov_len = sizeof(struct dlm_proxy_ast);
	vec[0].iov_base = &past;
	if (flags & DLM_LKSB_GET_LVB) {
		be32_add_cpu(&past.flags, LKM_GET_LVB);
		vec[1].iov_len = DLM_LVB_LEN;
		vec[1].iov_base = lock->lksb->lvb;
		veclen++;
	}

	ret = o2net_send_message_vec(DLM_PROXY_AST_MSG, dlm->key, vec, veclen,
				     lock->ml.analde, &status);
	if (ret < 0)
		mlog(ML_ERROR, "%s: res %.*s, error %d send AST to analde %u\n",
		     dlm->name, res->lockname.len, res->lockname.name, ret,
		     lock->ml.analde);
	else {
		if (status == DLM_RECOVERING) {
			mlog(ML_ERROR, "sent AST to analde %u, it thinks this "
			     "analde is dead!\n", lock->ml.analde);
			BUG();
		} else if (status == DLM_MIGRATING) {
			mlog(ML_ERROR, "sent AST to analde %u, it returned "
			     "DLM_MIGRATING!\n", lock->ml.analde);
			BUG();
		} else if (status != DLM_ANALRMAL && status != DLM_IVLOCKID) {
			mlog(ML_ERROR, "AST to analde %u returned %d!\n",
			     lock->ml.analde, status);
			/* iganalre it */
		}
		ret = 0;
	}
	return ret;
}

/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmast.c
 *
 * AST and BAST functionality for local and remote nodes
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


#include "cluster/heartbeat.h"
#include "cluster/nodemanager.h"
#include "cluster/tcp.h"

#include "dlmapi.h"
#include "dlmcommon.h"

#define MLOG_MASK_PREFIX ML_DLM
#include "cluster/masklog.h"

static void dlm_update_lvb(struct dlm_ctxt *dlm, struct dlm_lock_resource *res,
			   struct dlm_lock *lock);
static int dlm_should_cancel_bast(struct dlm_ctxt *dlm, struct dlm_lock *lock);

/* Should be called as an ast gets queued to see if the new
 * lock level will obsolete a pending bast.
 * For example, if dlm_thread queued a bast for an EX lock that
 * was blocking another EX, but before sending the bast the
 * lock owner downconverted to NL, the bast is now obsolete.
 * Only the ast should be sent.
 * This is needed because the lock and convert paths can queue
 * asts out-of-band (not waiting for dlm_thread) in order to
 * allow for LKM_NOQUEUE to get immediate responses. */
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
		/* NL blocks nothing, no reason to send any bast, cancel it */
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
		     "AST list not empty, pending %d, newlevel %d\n",
		     dlm->name, res->lockname.len, res->lockname.name,
		     dlm_get_lock_cookie_node(be64_to_cpu(lock->ml.cookie)),
		     dlm_get_lock_cookie_seq(be64_to_cpu(lock->ml.cookie)),
		     lock->ast_pending, lock->ml.type);
		BUG();
	}
	if (lock->ast_pending)
		mlog(0, "%s: res %.*s, lock %u:%llu, AST getting flushed\n",
		     dlm->name, res->lockname.len, res->lockname.name,
		     dlm_get_lock_cookie_node(be64_to_cpu(lock->ml.cookie)),
		     dlm_get_lock_cookie_seq(be64_to_cpu(lock->ml.cookie)));

	/* putting lock on list, add a ref */
	dlm_lock_get(lock);
	spin_lock(&lock->spinlock);

	/* check to see if this ast obsoletes the bast */
	if (dlm_should_cancel_bast(dlm, lock)) {
		mlog(0, "%s: res %.*s, lock %u:%llu, Cancelling BAST\n",
		     dlm->name, res->lockname.len, res->lockname.name,
		     dlm_get_lock_cookie_node(be64_to_cpu(lock->ml.cookie)),
		     dlm_get_lock_cookie_seq(be64_to_cpu(lock->ml.cookie)));
		lock->bast_pending = 0;
		list_del_init(&lock->bast_list);
		lock->ml.highest_blocked = LKM_IVMODE;
		/* removing lock from list, remove a ref.  guaranteed
		 * this won't be the last ref because of the get above,
		 * so res->spinlock will not be taken here */
		dlm_lock_put(lock);
		/* free up the reserved bast that we are cancelling.
		 * guaranteed that this will not be the last reserved
		 * ast because *both* an ast and a bast were reserved
		 * to get to this point.  the res->spinlock will not be
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
		     dlm_get_lock_cookie_node(be64_to_cpu(lock->ml.cookie)),
		     dlm_get_lock_cookie_seq(be64_to_cpu(lock->ml.cookie)));

	/* putting lock on list, add a ref */
	dlm_lock_get(lock);
	spin_lock(&lock->spinlock);
	list_add_tail(&lock->bast_list, &dlm->pending_basts);
	lock->bast_pending = 1;
	spin_unlock(&lock->spinlock);
}

void dlm_queue_bast(struct dlm_ctxt *dlm, struct dlm_lock *lock)
{
	BUG_ON(!dlm);
	BUG_ON(!lock);

	spin_lock(&dlm->ast_lock);
	__dlm_queue_bast(dlm, lock);
	spin_unlock(&dlm->ast_lock);
}

static void dlm_update_lvb(struct dlm_ctxt *dlm, struct dlm_lock_resource *res,
			   struct dlm_lock *lock)
{
	struct dlm_lockstatus *lksb = lock->lksb;
	BUG_ON(!lksb);

	/* only updates if this node masters the lockres */
	spin_lock(&res->spinlock);
	if (res->owner == dlm->node_num) {
		/* check the lksb flags for the direction */
		if (lksb->flags & DLM_LKSB_GET_LVB) {
			mlog(0, "getting lvb from lockres for %s node\n",
				  lock->ml.node == dlm->node_num ? "master" :
				  "remote");
			memcpy(lksb->lvb, res->lvb, DLM_LVB_LEN);
		}
		/* Do nothing for lvb put requests - they should be done in
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
	struct dlm_lockstatus *lksb;

	mlog(0, "%s: res %.*s, lock %u:%llu, Local AST\n", dlm->name,
	     res->lockname.len, res->lockname.name,
	     dlm_get_lock_cookie_node(be64_to_cpu(lock->ml.cookie)),
	     dlm_get_lock_cookie_seq(be64_to_cpu(lock->ml.cookie)));

	lksb = lock->lksb;
	fn = lock->ast;
	BUG_ON(lock->ml.node != dlm->node_num);

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
	     dlm_get_lock_cookie_node(be64_to_cpu(lock->ml.cookie)),
	     dlm_get_lock_cookie_seq(be64_to_cpu(lock->ml.cookie)));

	lksb = lock->lksb;
	BUG_ON(lock->ml.node == dlm->node_num);

	lksbflags = lksb->flags;
	dlm_update_lvb(dlm, res, lock);

	/* lock request came from another node
	 * go do the ast over there */
	ret = dlm_send_proxy_ast(dlm, res, lock, lksbflags);
	return ret;
}

void dlm_do_local_bast(struct dlm_ctxt *dlm, struct dlm_lock_resource *res,
		       struct dlm_lock *lock, int blocked_type)
{
	dlm_bastlockfunc_t *fn = lock->bast;

	BUG_ON(lock->ml.node != dlm->node_num);

	mlog(0, "%s: res %.*s, lock %u:%llu, Local BAST, blocked %d\n",
	     dlm->name, res->lockname.len, res->lockname.name,
	     dlm_get_lock_cookie_node(be64_to_cpu(lock->ml.cookie)),
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
	u8 node;

	if (!dlm_grab(dlm)) {
		dlm_error(DLM_REJECTED);
		return DLM_REJECTED;
	}

	mlog_bug_on_msg(!dlm_domain_fully_joined(dlm),
			"Domain %s not fully joined!\n", dlm->name);

	name = past->name;
	locklen = past->namelen;
	cookie = past->cookie;
	flags = be32_to_cpu(past->flags);
	node = past->node_idx;

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
		  (flags & LKM_GET_LVB ? "get lvb" : "none"));

	mlog(0, "type=%d, blocked_type=%d\n", past->type, past->blocked_type);

	if (past->type != DLM_AST &&
	    past->type != DLM_BAST) {
		mlog(ML_ERROR, "Unknown ast type! %d, cookie=%u:%llu"
		     "name=%.*s, node=%u\n", past->type,
		     dlm_get_lock_cookie_node(be64_to_cpu(cookie)),
		     dlm_get_lock_cookie_seq(be64_to_cpu(cookie)),
		     locklen, name, node);
		ret = DLM_IVLOCKID;
		goto leave;
	}

	res = dlm_lookup_lockres(dlm, name, locklen);
	if (!res) {
		mlog(0, "Got %sast for unknown lockres! cookie=%u:%llu, "
		     "name=%.*s, node=%u\n", (past->type == DLM_AST ? "" : "b"),
		     dlm_get_lock_cookie_node(be64_to_cpu(cookie)),
		     dlm_get_lock_cookie_seq(be64_to_cpu(cookie)),
		     locklen, name, node);
		ret = DLM_IVLOCKID;
		goto leave;
	}

	/* cannot get a proxy ast message if this node owns it */
	BUG_ON(res->owner == dlm->node_num);

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

	/* if not on convert, try blocked for ast, granted for bast */
	if (past->type == DLM_AST)
		head = &res->blocked;
	else
		head = &res->granted;

	list_for_each_entry(lock, head, list) {
		/* if lock is found but unlock is pending ignore the bast */
		if (lock->ml.cookie == cookie) {
			if (lock->unlock_pending)
				break;
			goto do_ast;
		}
	}

	mlog(0, "Got %sast for unknown lock! cookie=%u:%llu, name=%.*s, "
	     "node=%u\n", past->type == DLM_AST ? "" : "b",
	     dlm_get_lock_cookie_node(be64_to_cpu(cookie)),
	     dlm_get_lock_cookie_seq(be64_to_cpu(cookie)),
	     locklen, name, node);

	ret = DLM_NORMAL;
unlock_out:
	spin_unlock(&res->spinlock);
	goto leave;

do_ast:
	ret = DLM_NORMAL;
	if (past->type == DLM_AST) {
		/* do not alter lock refcount.  switching lists. */
		list_move_tail(&lock->list, &res->granted);
		mlog(0, "%s: res %.*s, lock %u:%llu, Granted type %d => %d\n",
		     dlm->name, res->lockname.len, res->lockname.name,
		     dlm_get_lock_cookie_node(be64_to_cpu(cookie)),
		     dlm_get_lock_cookie_seq(be64_to_cpu(cookie)),
		     lock->ml.type, lock->ml.convert_type);

		if (lock->ml.convert_type != LKM_IVMODE) {
			lock->ml.type = lock->ml.convert_type;
			lock->ml.convert_type = LKM_IVMODE;
		} else {
			// should already be there....
		}

		lock->lksb->status = DLM_NORMAL;

		/* if we requested the lvb, fetch it into our lksb now */
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
	     res->lockname.len, res->lockname.name, lock->ml.node, msg_type,
	     blocked_type);

	memset(&past, 0, sizeof(struct dlm_proxy_ast));
	past.node_idx = dlm->node_num;
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
				     lock->ml.node, &status);
	if (ret < 0)
		mlog(ML_ERROR, "%s: res %.*s, error %d send AST to node %u\n",
		     dlm->name, res->lockname.len, res->lockname.name, ret,
		     lock->ml.node);
	else {
		if (status == DLM_RECOVERING) {
			mlog(ML_ERROR, "sent AST to node %u, it thinks this "
			     "node is dead!\n", lock->ml.node);
			BUG();
		} else if (status == DLM_MIGRATING) {
			mlog(ML_ERROR, "sent AST to node %u, it returned "
			     "DLM_MIGRATING!\n", lock->ml.node);
			BUG();
		} else if (status != DLM_NORMAL && status != DLM_IVLOCKID) {
			mlog(ML_ERROR, "AST to node %u returned %d!\n",
			     lock->ml.node, status);
			/* ignore it */
		}
		ret = 0;
	}
	return ret;
}

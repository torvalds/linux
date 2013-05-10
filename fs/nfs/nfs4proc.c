/*
 *  fs/nfs/nfs4proc.c
 *
 *  Client-side procedure declarations for NFSv4.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Kendrick Smith <kmsmith@umich.edu>
 *  Andy Adamson   <andros@umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/gss_api.h>
#include <linux/nfs.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/nfs_mount.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/module.h>
#include <linux/sunrpc/bc_xprt.h>
#include <linux/xattr.h>
#include <linux/utsname.h>

#include "nfs4_fs.h"
#include "delegation.h"
#include "internal.h"
#include "iostat.h"
#include "callback.h"
#include "pnfs.h"

#define NFSDBG_FACILITY		NFSDBG_PROC

#define NFS4_POLL_RETRY_MIN	(HZ/10)
#define NFS4_POLL_RETRY_MAX	(15*HZ)

#define NFS4_MAX_LOOP_ON_RECOVER (10)

struct nfs4_opendata;
static int _nfs4_proc_open(struct nfs4_opendata *data);
static int _nfs4_recover_proc_open(struct nfs4_opendata *data);
static int nfs4_do_fsinfo(struct nfs_server *, struct nfs_fh *, struct nfs_fsinfo *);
static int nfs4_async_handle_error(struct rpc_task *, const struct nfs_server *, struct nfs4_state *);
static int _nfs4_proc_lookup(struct rpc_clnt *client, struct inode *dir,
			     const struct qstr *name, struct nfs_fh *fhandle,
			     struct nfs_fattr *fattr);
static int _nfs4_proc_getattr(struct nfs_server *server, struct nfs_fh *fhandle, struct nfs_fattr *fattr);
static int nfs4_do_setattr(struct inode *inode, struct rpc_cred *cred,
			    struct nfs_fattr *fattr, struct iattr *sattr,
			    struct nfs4_state *state);

/* Prevent leaks of NFSv4 errors into userland */
static int nfs4_map_errors(int err)
{
	if (err >= -1000)
		return err;
	switch (err) {
	case -NFS4ERR_RESOURCE:
		return -EREMOTEIO;
	case -NFS4ERR_WRONGSEC:
		return -EPERM;
	case -NFS4ERR_BADOWNER:
	case -NFS4ERR_BADNAME:
		return -EINVAL;
	case -NFS4ERR_SHARE_DENIED:
		return -EACCES;
	default:
		dprintk("%s could not handle NFSv4 error %d\n",
				__func__, -err);
		break;
	}
	return -EIO;
}

/*
 * This is our standard bitmap for GETATTR requests.
 */
const u32 nfs4_fattr_bitmap[2] = {
	FATTR4_WORD0_TYPE
	| FATTR4_WORD0_CHANGE
	| FATTR4_WORD0_SIZE
	| FATTR4_WORD0_FSID
	| FATTR4_WORD0_FILEID,
	FATTR4_WORD1_MODE
	| FATTR4_WORD1_NUMLINKS
	| FATTR4_WORD1_OWNER
	| FATTR4_WORD1_OWNER_GROUP
	| FATTR4_WORD1_RAWDEV
	| FATTR4_WORD1_SPACE_USED
	| FATTR4_WORD1_TIME_ACCESS
	| FATTR4_WORD1_TIME_METADATA
	| FATTR4_WORD1_TIME_MODIFY
};

const u32 nfs4_statfs_bitmap[2] = {
	FATTR4_WORD0_FILES_AVAIL
	| FATTR4_WORD0_FILES_FREE
	| FATTR4_WORD0_FILES_TOTAL,
	FATTR4_WORD1_SPACE_AVAIL
	| FATTR4_WORD1_SPACE_FREE
	| FATTR4_WORD1_SPACE_TOTAL
};

const u32 nfs4_pathconf_bitmap[2] = {
	FATTR4_WORD0_MAXLINK
	| FATTR4_WORD0_MAXNAME,
	0
};

const u32 nfs4_fsinfo_bitmap[2] = { FATTR4_WORD0_MAXFILESIZE
			| FATTR4_WORD0_MAXREAD
			| FATTR4_WORD0_MAXWRITE
			| FATTR4_WORD0_LEASE_TIME,
			FATTR4_WORD1_TIME_DELTA
			| FATTR4_WORD1_FS_LAYOUT_TYPES
};

const u32 nfs4_fs_locations_bitmap[2] = {
	FATTR4_WORD0_TYPE
	| FATTR4_WORD0_CHANGE
	| FATTR4_WORD0_SIZE
	| FATTR4_WORD0_FSID
	| FATTR4_WORD0_FILEID
	| FATTR4_WORD0_FS_LOCATIONS,
	FATTR4_WORD1_MODE
	| FATTR4_WORD1_NUMLINKS
	| FATTR4_WORD1_OWNER
	| FATTR4_WORD1_OWNER_GROUP
	| FATTR4_WORD1_RAWDEV
	| FATTR4_WORD1_SPACE_USED
	| FATTR4_WORD1_TIME_ACCESS
	| FATTR4_WORD1_TIME_METADATA
	| FATTR4_WORD1_TIME_MODIFY
	| FATTR4_WORD1_MOUNTED_ON_FILEID
};

static void nfs4_setup_readdir(u64 cookie, __be32 *verifier, struct dentry *dentry,
		struct nfs4_readdir_arg *readdir)
{
	__be32 *start, *p;

	BUG_ON(readdir->count < 80);
	if (cookie > 2) {
		readdir->cookie = cookie;
		memcpy(&readdir->verifier, verifier, sizeof(readdir->verifier));
		return;
	}

	readdir->cookie = 0;
	memset(&readdir->verifier, 0, sizeof(readdir->verifier));
	if (cookie == 2)
		return;
	
	/*
	 * NFSv4 servers do not return entries for '.' and '..'
	 * Therefore, we fake these entries here.  We let '.'
	 * have cookie 0 and '..' have cookie 1.  Note that
	 * when talking to the server, we always send cookie 0
	 * instead of 1 or 2.
	 */
	start = p = kmap_atomic(*readdir->pages, KM_USER0);
	
	if (cookie == 0) {
		*p++ = xdr_one;                                  /* next */
		*p++ = xdr_zero;                   /* cookie, first word */
		*p++ = xdr_one;                   /* cookie, second word */
		*p++ = xdr_one;                             /* entry len */
		memcpy(p, ".\0\0\0", 4);                        /* entry */
		p++;
		*p++ = xdr_one;                         /* bitmap length */
		*p++ = htonl(FATTR4_WORD0_FILEID);             /* bitmap */
		*p++ = htonl(8);              /* attribute buffer length */
		p = xdr_encode_hyper(p, NFS_FILEID(dentry->d_inode));
	}
	
	*p++ = xdr_one;                                  /* next */
	*p++ = xdr_zero;                   /* cookie, first word */
	*p++ = xdr_two;                   /* cookie, second word */
	*p++ = xdr_two;                             /* entry len */
	memcpy(p, "..\0\0", 4);                         /* entry */
	p++;
	*p++ = xdr_one;                         /* bitmap length */
	*p++ = htonl(FATTR4_WORD0_FILEID);             /* bitmap */
	*p++ = htonl(8);              /* attribute buffer length */
	p = xdr_encode_hyper(p, NFS_FILEID(dentry->d_parent->d_inode));

	readdir->pgbase = (char *)p - (char *)start;
	readdir->count -= readdir->pgbase;
	kunmap_atomic(start, KM_USER0);
}

static int nfs4_wait_clnt_recover(struct nfs_client *clp)
{
	int res;

	might_sleep();

	res = wait_on_bit(&clp->cl_state, NFS4CLNT_MANAGER_RUNNING,
			nfs_wait_bit_killable, TASK_KILLABLE);
	return res;
}

static int nfs4_delay(struct rpc_clnt *clnt, long *timeout)
{
	int res = 0;

	might_sleep();

	if (*timeout <= 0)
		*timeout = NFS4_POLL_RETRY_MIN;
	if (*timeout > NFS4_POLL_RETRY_MAX)
		*timeout = NFS4_POLL_RETRY_MAX;
	schedule_timeout_killable(*timeout);
	if (fatal_signal_pending(current))
		res = -ERESTARTSYS;
	*timeout <<= 1;
	return res;
}

/* This is the error handling routine for processes that are allowed
 * to sleep.
 */
static int nfs4_handle_exception(struct nfs_server *server, int errorcode, struct nfs4_exception *exception)
{
	struct nfs_client *clp = server->nfs_client;
	struct nfs4_state *state = exception->state;
	struct inode *inode = exception->inode;
	int ret = errorcode;

	exception->retry = 0;
	switch(errorcode) {
		case 0:
			return 0;
		case -NFS4ERR_OPENMODE:
			if (nfs_have_delegation(inode, FMODE_READ)) {
				nfs_inode_return_delegation(inode);
				exception->retry = 1;
				return 0;
			}
			if (state == NULL)
				break;
			nfs4_schedule_stateid_recovery(server, state);
			goto wait_on_recovery;
		case -NFS4ERR_DELEG_REVOKED:
		case -NFS4ERR_ADMIN_REVOKED:
		case -NFS4ERR_BAD_STATEID:
			if (state != NULL)
				nfs_remove_bad_delegation(state->inode);
			if (state == NULL)
				break;
			nfs4_schedule_stateid_recovery(server, state);
			goto wait_on_recovery;
		case -NFS4ERR_EXPIRED:
			if (state != NULL)
				nfs4_schedule_stateid_recovery(server, state);
		case -NFS4ERR_STALE_STATEID:
		case -NFS4ERR_STALE_CLIENTID:
			nfs4_schedule_lease_recovery(clp);
			goto wait_on_recovery;
#if defined(CONFIG_NFS_V4_1)
		case -NFS4ERR_BADSESSION:
		case -NFS4ERR_BADSLOT:
		case -NFS4ERR_BAD_HIGH_SLOT:
		case -NFS4ERR_CONN_NOT_BOUND_TO_SESSION:
		case -NFS4ERR_DEADSESSION:
		case -NFS4ERR_SEQ_FALSE_RETRY:
		case -NFS4ERR_SEQ_MISORDERED:
			dprintk("%s ERROR: %d Reset session\n", __func__,
				errorcode);
			nfs4_schedule_session_recovery(clp->cl_session);
			goto wait_on_recovery;
#endif /* defined(CONFIG_NFS_V4_1) */
		case -NFS4ERR_FILE_OPEN:
			if (exception->timeout > HZ) {
				/* We have retried a decent amount, time to
				 * fail
				 */
				ret = -EBUSY;
				break;
			}
		case -NFS4ERR_GRACE:
		case -NFS4ERR_DELAY:
		case -EKEYEXPIRED:
			ret = nfs4_delay(server->client, &exception->timeout);
			if (ret != 0)
				break;
		case -NFS4ERR_RETRY_UNCACHED_REP:
		case -NFS4ERR_OLD_STATEID:
			exception->retry = 1;
			break;
		case -NFS4ERR_BADOWNER:
			/* The following works around a Linux server bug! */
		case -NFS4ERR_BADNAME:
			if (server->caps & NFS_CAP_UIDGID_NOMAP) {
				server->caps &= ~NFS_CAP_UIDGID_NOMAP;
				exception->retry = 1;
				printk(KERN_WARNING "NFS: v4 server %s "
						"does not accept raw "
						"uid/gids. "
						"Reenabling the idmapper.\n",
						server->nfs_client->cl_hostname);
			}
	}
	/* We failed to handle the error */
	return nfs4_map_errors(ret);
wait_on_recovery:
	ret = nfs4_wait_clnt_recover(clp);
	if (ret == 0)
		exception->retry = 1;
	return ret;
}


static void do_renew_lease(struct nfs_client *clp, unsigned long timestamp)
{
	spin_lock(&clp->cl_lock);
	if (time_before(clp->cl_last_renewal,timestamp))
		clp->cl_last_renewal = timestamp;
	spin_unlock(&clp->cl_lock);
}

static void renew_lease(const struct nfs_server *server, unsigned long timestamp)
{
	do_renew_lease(server->nfs_client, timestamp);
}

#if defined(CONFIG_NFS_V4_1)

/*
 * nfs4_free_slot - free a slot and efficiently update slot table.
 *
 * freeing a slot is trivially done by clearing its respective bit
 * in the bitmap.
 * If the freed slotid equals highest_used_slotid we want to update it
 * so that the server would be able to size down the slot table if needed,
 * otherwise we know that the highest_used_slotid is still in use.
 * When updating highest_used_slotid there may be "holes" in the bitmap
 * so we need to scan down from highest_used_slotid to 0 looking for the now
 * highest slotid in use.
 * If none found, highest_used_slotid is set to -1.
 *
 * Must be called while holding tbl->slot_tbl_lock
 */
static void
nfs4_free_slot(struct nfs4_slot_table *tbl, struct nfs4_slot *free_slot)
{
	int free_slotid = free_slot - tbl->slots;
	int slotid = free_slotid;

	BUG_ON(slotid < 0 || slotid >= NFS4_MAX_SLOT_TABLE);
	/* clear used bit in bitmap */
	__clear_bit(slotid, tbl->used_slots);

	/* update highest_used_slotid when it is freed */
	if (slotid == tbl->highest_used_slotid) {
		slotid = find_last_bit(tbl->used_slots, tbl->max_slots);
		if (slotid < tbl->max_slots)
			tbl->highest_used_slotid = slotid;
		else
			tbl->highest_used_slotid = -1;
	}
	dprintk("%s: free_slotid %u highest_used_slotid %d\n", __func__,
		free_slotid, tbl->highest_used_slotid);
}

/*
 * Signal state manager thread if session fore channel is drained
 */
static void nfs4_check_drain_fc_complete(struct nfs4_session *ses)
{
	struct rpc_task *task;

	if (!test_bit(NFS4_SESSION_DRAINING, &ses->session_state)) {
		task = rpc_wake_up_next(&ses->fc_slot_table.slot_tbl_waitq);
		if (task)
			rpc_task_set_priority(task, RPC_PRIORITY_PRIVILEGED);
		return;
	}

	if (ses->fc_slot_table.highest_used_slotid != -1)
		return;

	dprintk("%s COMPLETE: Session Fore Channel Drained\n", __func__);
	complete(&ses->fc_slot_table.complete);
}

/*
 * Signal state manager thread if session back channel is drained
 */
void nfs4_check_drain_bc_complete(struct nfs4_session *ses)
{
	if (!test_bit(NFS4_SESSION_DRAINING, &ses->session_state) ||
	    ses->bc_slot_table.highest_used_slotid != -1)
		return;
	dprintk("%s COMPLETE: Session Back Channel Drained\n", __func__);
	complete(&ses->bc_slot_table.complete);
}

static void nfs41_sequence_free_slot(struct nfs4_sequence_res *res)
{
	struct nfs4_slot_table *tbl;

	tbl = &res->sr_session->fc_slot_table;
	if (!res->sr_slot) {
		/* just wake up the next guy waiting since
		 * we may have not consumed a slot after all */
		dprintk("%s: No slot\n", __func__);
		return;
	}

	spin_lock(&tbl->slot_tbl_lock);
	nfs4_free_slot(tbl, res->sr_slot);
	nfs4_check_drain_fc_complete(res->sr_session);
	spin_unlock(&tbl->slot_tbl_lock);
	res->sr_slot = NULL;
}

static int nfs41_sequence_done(struct rpc_task *task, struct nfs4_sequence_res *res)
{
	unsigned long timestamp;
	struct nfs_client *clp;

	/*
	 * sr_status remains 1 if an RPC level error occurred. The server
	 * may or may not have processed the sequence operation..
	 * Proceed as if the server received and processed the sequence
	 * operation.
	 */
	if (res->sr_status == 1)
		res->sr_status = NFS_OK;

	/* don't increment the sequence number if the task wasn't sent */
	if (!RPC_WAS_SENT(task))
		goto out;

	/* Check the SEQUENCE operation status */
	switch (res->sr_status) {
	case 0:
		/* Update the slot's sequence and clientid lease timer */
		++res->sr_slot->seq_nr;
		timestamp = res->sr_renewal_time;
		clp = res->sr_session->clp;
		do_renew_lease(clp, timestamp);
		/* Check sequence flags */
		if (res->sr_status_flags != 0)
			nfs4_schedule_lease_recovery(clp);
		break;
	case -NFS4ERR_DELAY:
		/* The server detected a resend of the RPC call and
		 * returned NFS4ERR_DELAY as per Section 2.10.6.2
		 * of RFC5661.
		 */
		dprintk("%s: slot=%td seq=%d: Operation in progress\n",
			__func__,
			res->sr_slot - res->sr_session->fc_slot_table.slots,
			res->sr_slot->seq_nr);
		goto out_retry;
	default:
		/* Just update the slot sequence no. */
		++res->sr_slot->seq_nr;
	}
out:
	/* The session may be reset by one of the error handlers. */
	dprintk("%s: Error %d free the slot \n", __func__, res->sr_status);
	nfs41_sequence_free_slot(res);
	return 1;
out_retry:
	if (!rpc_restart_call(task))
		goto out;
	rpc_delay(task, NFS4_POLL_RETRY_MAX);
	return 0;
}

static int nfs4_sequence_done(struct rpc_task *task,
			       struct nfs4_sequence_res *res)
{
	if (res->sr_session == NULL)
		return 1;
	return nfs41_sequence_done(task, res);
}

/*
 * nfs4_find_slot - efficiently look for a free slot
 *
 * nfs4_find_slot looks for an unset bit in the used_slots bitmap.
 * If found, we mark the slot as used, update the highest_used_slotid,
 * and respectively set up the sequence operation args.
 * The slot number is returned if found, or NFS4_MAX_SLOT_TABLE otherwise.
 *
 * Note: must be called with under the slot_tbl_lock.
 */
static u8
nfs4_find_slot(struct nfs4_slot_table *tbl)
{
	int slotid;
	u8 ret_id = NFS4_MAX_SLOT_TABLE;
	BUILD_BUG_ON((u8)NFS4_MAX_SLOT_TABLE != (int)NFS4_MAX_SLOT_TABLE);

	dprintk("--> %s used_slots=%04lx highest_used=%d max_slots=%d\n",
		__func__, tbl->used_slots[0], tbl->highest_used_slotid,
		tbl->max_slots);
	slotid = find_first_zero_bit(tbl->used_slots, tbl->max_slots);
	if (slotid >= tbl->max_slots)
		goto out;
	__set_bit(slotid, tbl->used_slots);
	if (slotid > tbl->highest_used_slotid)
		tbl->highest_used_slotid = slotid;
	ret_id = slotid;
out:
	dprintk("<-- %s used_slots=%04lx highest_used=%d slotid=%d \n",
		__func__, tbl->used_slots[0], tbl->highest_used_slotid, ret_id);
	return ret_id;
}

int nfs41_setup_sequence(struct nfs4_session *session,
				struct nfs4_sequence_args *args,
				struct nfs4_sequence_res *res,
				int cache_reply,
				struct rpc_task *task)
{
	struct nfs4_slot *slot;
	struct nfs4_slot_table *tbl;
	u8 slotid;

	dprintk("--> %s\n", __func__);
	/* slot already allocated? */
	if (res->sr_slot != NULL)
		return 0;

	tbl = &session->fc_slot_table;

	spin_lock(&tbl->slot_tbl_lock);
	if (test_bit(NFS4_SESSION_DRAINING, &session->session_state) &&
	    !rpc_task_has_priority(task, RPC_PRIORITY_PRIVILEGED)) {
		/*
		 * The state manager will wait until the slot table is empty.
		 * Schedule the reset thread
		 */
		rpc_sleep_on(&tbl->slot_tbl_waitq, task, NULL);
		spin_unlock(&tbl->slot_tbl_lock);
		dprintk("%s Schedule Session Reset\n", __func__);
		return -EAGAIN;
	}

	if (!rpc_queue_empty(&tbl->slot_tbl_waitq) &&
	    !rpc_task_has_priority(task, RPC_PRIORITY_PRIVILEGED)) {
		rpc_sleep_on(&tbl->slot_tbl_waitq, task, NULL);
		spin_unlock(&tbl->slot_tbl_lock);
		dprintk("%s enforce FIFO order\n", __func__);
		return -EAGAIN;
	}

	slotid = nfs4_find_slot(tbl);
	if (slotid == NFS4_MAX_SLOT_TABLE) {
		rpc_sleep_on(&tbl->slot_tbl_waitq, task, NULL);
		spin_unlock(&tbl->slot_tbl_lock);
		dprintk("<-- %s: no free slots\n", __func__);
		return -EAGAIN;
	}
	spin_unlock(&tbl->slot_tbl_lock);

	rpc_task_set_priority(task, RPC_PRIORITY_NORMAL);
	slot = tbl->slots + slotid;
	args->sa_session = session;
	args->sa_slotid = slotid;
	args->sa_cache_this = cache_reply;

	dprintk("<-- %s slotid=%d seqid=%d\n", __func__, slotid, slot->seq_nr);

	res->sr_session = session;
	res->sr_slot = slot;
	res->sr_renewal_time = jiffies;
	res->sr_status_flags = 0;
	/*
	 * sr_status is only set in decode_sequence, and so will remain
	 * set to 1 if an rpc level failure occurs.
	 */
	res->sr_status = 1;
	return 0;
}
EXPORT_SYMBOL_GPL(nfs41_setup_sequence);

int nfs4_setup_sequence(const struct nfs_server *server,
			struct nfs4_sequence_args *args,
			struct nfs4_sequence_res *res,
			int cache_reply,
			struct rpc_task *task)
{
	struct nfs4_session *session = nfs4_get_session(server);
	int ret = 0;

	if (session == NULL) {
		args->sa_session = NULL;
		res->sr_session = NULL;
		goto out;
	}

	dprintk("--> %s clp %p session %p sr_slot %td\n",
		__func__, session->clp, session, res->sr_slot ?
			res->sr_slot - session->fc_slot_table.slots : -1);

	ret = nfs41_setup_sequence(session, args, res, cache_reply,
				   task);
out:
	dprintk("<-- %s status=%d\n", __func__, ret);
	return ret;
}

struct nfs41_call_sync_data {
	const struct nfs_server *seq_server;
	struct nfs4_sequence_args *seq_args;
	struct nfs4_sequence_res *seq_res;
	int cache_reply;
};

static void nfs41_call_sync_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs41_call_sync_data *data = calldata;

	dprintk("--> %s data->seq_server %p\n", __func__, data->seq_server);

	if (nfs4_setup_sequence(data->seq_server, data->seq_args,
				data->seq_res, data->cache_reply, task))
		return;
	rpc_call_start(task);
}

static void nfs41_call_priv_sync_prepare(struct rpc_task *task, void *calldata)
{
	rpc_task_set_priority(task, RPC_PRIORITY_PRIVILEGED);
	nfs41_call_sync_prepare(task, calldata);
}

static void nfs41_call_sync_done(struct rpc_task *task, void *calldata)
{
	struct nfs41_call_sync_data *data = calldata;

	nfs41_sequence_done(task, data->seq_res);
}

struct rpc_call_ops nfs41_call_sync_ops = {
	.rpc_call_prepare = nfs41_call_sync_prepare,
	.rpc_call_done = nfs41_call_sync_done,
};

struct rpc_call_ops nfs41_call_priv_sync_ops = {
	.rpc_call_prepare = nfs41_call_priv_sync_prepare,
	.rpc_call_done = nfs41_call_sync_done,
};

static int nfs4_call_sync_sequence(struct rpc_clnt *clnt,
				   struct nfs_server *server,
				   struct rpc_message *msg,
				   struct nfs4_sequence_args *args,
				   struct nfs4_sequence_res *res,
				   int cache_reply,
				   int privileged)
{
	int ret;
	struct rpc_task *task;
	struct nfs41_call_sync_data data = {
		.seq_server = server,
		.seq_args = args,
		.seq_res = res,
		.cache_reply = cache_reply,
	};
	struct rpc_task_setup task_setup = {
		.rpc_client = clnt,
		.rpc_message = msg,
		.callback_ops = &nfs41_call_sync_ops,
		.callback_data = &data
	};

	res->sr_slot = NULL;
	if (privileged)
		task_setup.callback_ops = &nfs41_call_priv_sync_ops;
	task = rpc_run_task(&task_setup);
	if (IS_ERR(task))
		ret = PTR_ERR(task);
	else {
		ret = task->tk_status;
		rpc_put_task(task);
	}
	return ret;
}

int _nfs4_call_sync_session(struct rpc_clnt *clnt,
			    struct nfs_server *server,
			    struct rpc_message *msg,
			    struct nfs4_sequence_args *args,
			    struct nfs4_sequence_res *res,
			    int cache_reply)
{
	return nfs4_call_sync_sequence(clnt, server, msg, args, res, cache_reply, 0);
}

#else
static int nfs4_sequence_done(struct rpc_task *task,
			       struct nfs4_sequence_res *res)
{
	return 1;
}
#endif /* CONFIG_NFS_V4_1 */

int _nfs4_call_sync(struct rpc_clnt *clnt,
		    struct nfs_server *server,
		    struct rpc_message *msg,
		    struct nfs4_sequence_args *args,
		    struct nfs4_sequence_res *res,
		    int cache_reply)
{
	args->sa_session = res->sr_session = NULL;
	return rpc_call_sync(clnt, msg, 0);
}

static inline
int nfs4_call_sync(struct rpc_clnt *clnt,
		   struct nfs_server *server,
		   struct rpc_message *msg,
		   struct nfs4_sequence_args *args,
		   struct nfs4_sequence_res *res,
		   int cache_reply)
{
	return server->nfs_client->cl_mvops->call_sync(clnt, server, msg,
						args, res, cache_reply);
}

static void update_changeattr(struct inode *dir, struct nfs4_change_info *cinfo)
{
	struct nfs_inode *nfsi = NFS_I(dir);

	spin_lock(&dir->i_lock);
	nfsi->cache_validity |= NFS_INO_INVALID_ATTR|NFS_INO_REVAL_PAGECACHE|NFS_INO_INVALID_DATA;
	if (!cinfo->atomic || cinfo->before != nfsi->change_attr)
		nfs_force_lookup_revalidate(dir);
	nfsi->change_attr = cinfo->after;
	spin_unlock(&dir->i_lock);
}

struct nfs4_opendata {
	struct kref kref;
	struct nfs_openargs o_arg;
	struct nfs_openres o_res;
	struct nfs_open_confirmargs c_arg;
	struct nfs_open_confirmres c_res;
	struct nfs_fattr f_attr;
	struct nfs_fattr dir_attr;
	struct path path;
	struct dentry *dir;
	struct nfs4_state_owner *owner;
	struct nfs4_state *state;
	struct iattr attrs;
	unsigned long timestamp;
	unsigned int rpc_done : 1;
	int rpc_status;
	int cancelled;
};


static void nfs4_init_opendata_res(struct nfs4_opendata *p)
{
	p->o_res.f_attr = &p->f_attr;
	p->o_res.dir_attr = &p->dir_attr;
	p->o_res.seqid = p->o_arg.seqid;
	p->c_res.seqid = p->c_arg.seqid;
	p->o_res.server = p->o_arg.server;
	nfs_fattr_init(&p->f_attr);
	nfs_fattr_init(&p->dir_attr);
}

static struct nfs4_opendata *nfs4_opendata_alloc(struct path *path,
		struct nfs4_state_owner *sp, fmode_t fmode, int flags,
		const struct iattr *attrs,
		gfp_t gfp_mask)
{
	struct dentry *parent = dget_parent(path->dentry);
	struct inode *dir = parent->d_inode;
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs4_opendata *p;

	p = kzalloc(sizeof(*p), gfp_mask);
	if (p == NULL)
		goto err;
	p->o_arg.seqid = nfs_alloc_seqid(&sp->so_seqid, gfp_mask);
	if (p->o_arg.seqid == NULL)
		goto err_free;
	path_get(path);
	p->path = *path;
	p->dir = parent;
	p->owner = sp;
	atomic_inc(&sp->so_count);
	p->o_arg.fh = NFS_FH(dir);
	p->o_arg.open_flags = flags;
	p->o_arg.fmode = fmode & (FMODE_READ|FMODE_WRITE);
	p->o_arg.clientid = server->nfs_client->cl_clientid;
	p->o_arg.id = sp->so_owner_id.id;
	p->o_arg.name = &p->path.dentry->d_name;
	p->o_arg.server = server;
	p->o_arg.bitmask = server->attr_bitmask;
	p->o_arg.claim = NFS4_OPEN_CLAIM_NULL;
	if (flags & O_CREAT) {
		u32 *s;

		p->o_arg.u.attrs = &p->attrs;
		memcpy(&p->attrs, attrs, sizeof(p->attrs));
		s = (u32 *) p->o_arg.u.verifier.data;
		s[0] = jiffies;
		s[1] = current->pid;
	}
	p->c_arg.fh = &p->o_res.fh;
	p->c_arg.stateid = &p->o_res.stateid;
	p->c_arg.seqid = p->o_arg.seqid;
	nfs4_init_opendata_res(p);
	kref_init(&p->kref);
	return p;
err_free:
	kfree(p);
err:
	dput(parent);
	return NULL;
}

static void nfs4_opendata_free(struct kref *kref)
{
	struct nfs4_opendata *p = container_of(kref,
			struct nfs4_opendata, kref);

	nfs_free_seqid(p->o_arg.seqid);
	if (p->state != NULL)
		nfs4_put_open_state(p->state);
	nfs4_put_state_owner(p->owner);
	dput(p->dir);
	path_put(&p->path);
	kfree(p);
}

static void nfs4_opendata_put(struct nfs4_opendata *p)
{
	if (p != NULL)
		kref_put(&p->kref, nfs4_opendata_free);
}

static int nfs4_wait_for_completion_rpc_task(struct rpc_task *task)
{
	int ret;

	ret = rpc_wait_for_completion_task(task);
	return ret;
}

static int can_open_cached(struct nfs4_state *state, fmode_t mode, int open_mode)
{
	int ret = 0;

	if (open_mode & O_EXCL)
		goto out;
	switch (mode & (FMODE_READ|FMODE_WRITE)) {
		case FMODE_READ:
			ret |= test_bit(NFS_O_RDONLY_STATE, &state->flags) != 0
				&& state->n_rdonly != 0;
			break;
		case FMODE_WRITE:
			ret |= test_bit(NFS_O_WRONLY_STATE, &state->flags) != 0
				&& state->n_wronly != 0;
			break;
		case FMODE_READ|FMODE_WRITE:
			ret |= test_bit(NFS_O_RDWR_STATE, &state->flags) != 0
				&& state->n_rdwr != 0;
	}
out:
	return ret;
}

static int can_open_delegated(struct nfs_delegation *delegation, fmode_t fmode)
{
	if ((delegation->type & fmode) != fmode)
		return 0;
	if (test_bit(NFS_DELEGATION_NEED_RECLAIM, &delegation->flags))
		return 0;
	nfs_mark_delegation_referenced(delegation);
	return 1;
}

static void update_open_stateflags(struct nfs4_state *state, fmode_t fmode)
{
	switch (fmode) {
		case FMODE_WRITE:
			state->n_wronly++;
			break;
		case FMODE_READ:
			state->n_rdonly++;
			break;
		case FMODE_READ|FMODE_WRITE:
			state->n_rdwr++;
	}
	nfs4_state_set_mode_locked(state, state->state | fmode);
}

static void nfs_set_open_stateid_locked(struct nfs4_state *state, nfs4_stateid *stateid, fmode_t fmode)
{
	if (test_bit(NFS_DELEGATED_STATE, &state->flags) == 0)
		memcpy(state->stateid.data, stateid->data, sizeof(state->stateid.data));
	memcpy(state->open_stateid.data, stateid->data, sizeof(state->open_stateid.data));
	switch (fmode) {
		case FMODE_READ:
			set_bit(NFS_O_RDONLY_STATE, &state->flags);
			break;
		case FMODE_WRITE:
			set_bit(NFS_O_WRONLY_STATE, &state->flags);
			break;
		case FMODE_READ|FMODE_WRITE:
			set_bit(NFS_O_RDWR_STATE, &state->flags);
	}
}

static void nfs_set_open_stateid(struct nfs4_state *state, nfs4_stateid *stateid, fmode_t fmode)
{
	write_seqlock(&state->seqlock);
	nfs_set_open_stateid_locked(state, stateid, fmode);
	write_sequnlock(&state->seqlock);
}

static void __update_open_stateid(struct nfs4_state *state, nfs4_stateid *open_stateid, const nfs4_stateid *deleg_stateid, fmode_t fmode)
{
	/*
	 * Protect the call to nfs4_state_set_mode_locked and
	 * serialise the stateid update
	 */
	write_seqlock(&state->seqlock);
	if (deleg_stateid != NULL) {
		memcpy(state->stateid.data, deleg_stateid->data, sizeof(state->stateid.data));
		set_bit(NFS_DELEGATED_STATE, &state->flags);
	}
	if (open_stateid != NULL)
		nfs_set_open_stateid_locked(state, open_stateid, fmode);
	write_sequnlock(&state->seqlock);
	spin_lock(&state->owner->so_lock);
	update_open_stateflags(state, fmode);
	spin_unlock(&state->owner->so_lock);
}

static int update_open_stateid(struct nfs4_state *state, nfs4_stateid *open_stateid, nfs4_stateid *delegation, fmode_t fmode)
{
	struct nfs_inode *nfsi = NFS_I(state->inode);
	struct nfs_delegation *deleg_cur;
	int ret = 0;

	fmode &= (FMODE_READ|FMODE_WRITE);

	rcu_read_lock();
	deleg_cur = rcu_dereference(nfsi->delegation);
	if (deleg_cur == NULL)
		goto no_delegation;

	spin_lock(&deleg_cur->lock);
	if (nfsi->delegation != deleg_cur ||
	    (deleg_cur->type & fmode) != fmode)
		goto no_delegation_unlock;

	if (delegation == NULL)
		delegation = &deleg_cur->stateid;
	else if (memcmp(deleg_cur->stateid.data, delegation->data, NFS4_STATEID_SIZE) != 0)
		goto no_delegation_unlock;

	nfs_mark_delegation_referenced(deleg_cur);
	__update_open_stateid(state, open_stateid, &deleg_cur->stateid, fmode);
	ret = 1;
no_delegation_unlock:
	spin_unlock(&deleg_cur->lock);
no_delegation:
	rcu_read_unlock();

	if (!ret && open_stateid != NULL) {
		__update_open_stateid(state, open_stateid, NULL, fmode);
		ret = 1;
	}

	return ret;
}


static void nfs4_return_incompatible_delegation(struct inode *inode, fmode_t fmode)
{
	struct nfs_delegation *delegation;

	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(inode)->delegation);
	if (delegation == NULL || (delegation->type & fmode) == fmode) {
		rcu_read_unlock();
		return;
	}
	rcu_read_unlock();
	nfs_inode_return_delegation(inode);
}

static struct nfs4_state *nfs4_try_open_cached(struct nfs4_opendata *opendata)
{
	struct nfs4_state *state = opendata->state;
	struct nfs_inode *nfsi = NFS_I(state->inode);
	struct nfs_delegation *delegation;
	int open_mode = opendata->o_arg.open_flags & O_EXCL;
	fmode_t fmode = opendata->o_arg.fmode;
	nfs4_stateid stateid;
	int ret = -EAGAIN;

	for (;;) {
		if (can_open_cached(state, fmode, open_mode)) {
			spin_lock(&state->owner->so_lock);
			if (can_open_cached(state, fmode, open_mode)) {
				update_open_stateflags(state, fmode);
				spin_unlock(&state->owner->so_lock);
				goto out_return_state;
			}
			spin_unlock(&state->owner->so_lock);
		}
		rcu_read_lock();
		delegation = rcu_dereference(nfsi->delegation);
		if (delegation == NULL ||
		    !can_open_delegated(delegation, fmode)) {
			rcu_read_unlock();
			break;
		}
		/* Save the delegation */
		memcpy(stateid.data, delegation->stateid.data, sizeof(stateid.data));
		rcu_read_unlock();
		ret = nfs_may_open(state->inode, state->owner->so_cred, open_mode);
		if (ret != 0)
			goto out;
		ret = -EAGAIN;

		/* Try to update the stateid using the delegation */
		if (update_open_stateid(state, NULL, &stateid, fmode))
			goto out_return_state;
	}
out:
	return ERR_PTR(ret);
out_return_state:
	atomic_inc(&state->count);
	return state;
}

static struct nfs4_state *nfs4_opendata_to_nfs4_state(struct nfs4_opendata *data)
{
	struct inode *inode;
	struct nfs4_state *state = NULL;
	struct nfs_delegation *delegation;
	int ret;

	if (!data->rpc_done) {
		state = nfs4_try_open_cached(data);
		goto out;
	}

	ret = -EAGAIN;
	if (!(data->f_attr.valid & NFS_ATTR_FATTR))
		goto err;
	inode = nfs_fhget(data->dir->d_sb, &data->o_res.fh, &data->f_attr);
	ret = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto err;
	ret = -ENOMEM;
	state = nfs4_get_open_state(inode, data->owner);
	if (state == NULL)
		goto err_put_inode;
	if (data->o_res.delegation_type != 0) {
		int delegation_flags = 0;

		rcu_read_lock();
		delegation = rcu_dereference(NFS_I(inode)->delegation);
		if (delegation)
			delegation_flags = delegation->flags;
		rcu_read_unlock();
		if ((delegation_flags & 1UL<<NFS_DELEGATION_NEED_RECLAIM) == 0)
			nfs_inode_set_delegation(state->inode,
					data->owner->so_cred,
					&data->o_res);
		else
			nfs_inode_reclaim_delegation(state->inode,
					data->owner->so_cred,
					&data->o_res);
	}

	update_open_stateid(state, &data->o_res.stateid, NULL,
			data->o_arg.fmode);
	iput(inode);
out:
	return state;
err_put_inode:
	iput(inode);
err:
	return ERR_PTR(ret);
}

static struct nfs_open_context *nfs4_state_find_open_context(struct nfs4_state *state)
{
	struct nfs_inode *nfsi = NFS_I(state->inode);
	struct nfs_open_context *ctx;

	spin_lock(&state->inode->i_lock);
	list_for_each_entry(ctx, &nfsi->open_files, list) {
		if (ctx->state != state)
			continue;
		get_nfs_open_context(ctx);
		spin_unlock(&state->inode->i_lock);
		return ctx;
	}
	spin_unlock(&state->inode->i_lock);
	return ERR_PTR(-ENOENT);
}

static struct nfs4_opendata *nfs4_open_recoverdata_alloc(struct nfs_open_context *ctx, struct nfs4_state *state)
{
	struct nfs4_opendata *opendata;

	opendata = nfs4_opendata_alloc(&ctx->path, state->owner, 0, 0, NULL, GFP_NOFS);
	if (opendata == NULL)
		return ERR_PTR(-ENOMEM);
	opendata->state = state;
	atomic_inc(&state->count);
	return opendata;
}

static int nfs4_open_recover_helper(struct nfs4_opendata *opendata, fmode_t fmode, struct nfs4_state **res)
{
	struct nfs4_state *newstate;
	int ret;

	opendata->o_arg.open_flags = 0;
	opendata->o_arg.fmode = fmode;
	memset(&opendata->o_res, 0, sizeof(opendata->o_res));
	memset(&opendata->c_res, 0, sizeof(opendata->c_res));
	nfs4_init_opendata_res(opendata);
	ret = _nfs4_recover_proc_open(opendata);
	if (ret != 0)
		return ret; 
	newstate = nfs4_opendata_to_nfs4_state(opendata);
	if (IS_ERR(newstate))
		return PTR_ERR(newstate);
	nfs4_close_state(&opendata->path, newstate, fmode);
	*res = newstate;
	return 0;
}

static int nfs4_open_recover(struct nfs4_opendata *opendata, struct nfs4_state *state)
{
	struct nfs4_state *newstate;
	int ret;

	/* memory barrier prior to reading state->n_* */
	clear_bit(NFS_DELEGATED_STATE, &state->flags);
	smp_rmb();
	if (state->n_rdwr != 0) {
		clear_bit(NFS_O_RDWR_STATE, &state->flags);
		ret = nfs4_open_recover_helper(opendata, FMODE_READ|FMODE_WRITE, &newstate);
		if (ret != 0)
			return ret;
		if (newstate != state)
			return -ESTALE;
	}
	if (state->n_wronly != 0) {
		clear_bit(NFS_O_WRONLY_STATE, &state->flags);
		ret = nfs4_open_recover_helper(opendata, FMODE_WRITE, &newstate);
		if (ret != 0)
			return ret;
		if (newstate != state)
			return -ESTALE;
	}
	if (state->n_rdonly != 0) {
		clear_bit(NFS_O_RDONLY_STATE, &state->flags);
		ret = nfs4_open_recover_helper(opendata, FMODE_READ, &newstate);
		if (ret != 0)
			return ret;
		if (newstate != state)
			return -ESTALE;
	}
	/*
	 * We may have performed cached opens for all three recoveries.
	 * Check if we need to update the current stateid.
	 */
	if (test_bit(NFS_DELEGATED_STATE, &state->flags) == 0 &&
	    memcmp(state->stateid.data, state->open_stateid.data, sizeof(state->stateid.data)) != 0) {
		write_seqlock(&state->seqlock);
		if (test_bit(NFS_DELEGATED_STATE, &state->flags) == 0)
			memcpy(state->stateid.data, state->open_stateid.data, sizeof(state->stateid.data));
		write_sequnlock(&state->seqlock);
	}
	return 0;
}

/*
 * OPEN_RECLAIM:
 * 	reclaim state on the server after a reboot.
 */
static int _nfs4_do_open_reclaim(struct nfs_open_context *ctx, struct nfs4_state *state)
{
	struct nfs_delegation *delegation;
	struct nfs4_opendata *opendata;
	fmode_t delegation_type = 0;
	int status;

	opendata = nfs4_open_recoverdata_alloc(ctx, state);
	if (IS_ERR(opendata))
		return PTR_ERR(opendata);
	opendata->o_arg.claim = NFS4_OPEN_CLAIM_PREVIOUS;
	opendata->o_arg.fh = NFS_FH(state->inode);
	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(state->inode)->delegation);
	if (delegation != NULL && test_bit(NFS_DELEGATION_NEED_RECLAIM, &delegation->flags) != 0)
		delegation_type = delegation->type;
	rcu_read_unlock();
	opendata->o_arg.u.delegation_type = delegation_type;
	status = nfs4_open_recover(opendata, state);
	nfs4_opendata_put(opendata);
	return status;
}

static int nfs4_do_open_reclaim(struct nfs_open_context *ctx, struct nfs4_state *state)
{
	struct nfs_server *server = NFS_SERVER(state->inode);
	struct nfs4_exception exception = { };
	int err;
	do {
		err = _nfs4_do_open_reclaim(ctx, state);
		if (err != -NFS4ERR_DELAY)
			break;
		nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);
	return err;
}

static int nfs4_open_reclaim(struct nfs4_state_owner *sp, struct nfs4_state *state)
{
	struct nfs_open_context *ctx;
	int ret;

	ctx = nfs4_state_find_open_context(state);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);
	ret = nfs4_do_open_reclaim(ctx, state);
	put_nfs_open_context(ctx);
	return ret;
}

static int _nfs4_open_delegation_recall(struct nfs_open_context *ctx, struct nfs4_state *state, const nfs4_stateid *stateid)
{
	struct nfs4_opendata *opendata;
	int ret;

	opendata = nfs4_open_recoverdata_alloc(ctx, state);
	if (IS_ERR(opendata))
		return PTR_ERR(opendata);
	opendata->o_arg.claim = NFS4_OPEN_CLAIM_DELEGATE_CUR;
	memcpy(opendata->o_arg.u.delegation.data, stateid->data,
			sizeof(opendata->o_arg.u.delegation.data));
	ret = nfs4_open_recover(opendata, state);
	nfs4_opendata_put(opendata);
	return ret;
}

int nfs4_open_delegation_recall(struct nfs_open_context *ctx, struct nfs4_state *state, const nfs4_stateid *stateid)
{
	struct nfs4_exception exception = { };
	struct nfs_server *server = NFS_SERVER(state->inode);
	int err;
	do {
		err = _nfs4_open_delegation_recall(ctx, state, stateid);
		switch (err) {
			case 0:
			case -ENOENT:
			case -ESTALE:
				goto out;
			case -NFS4ERR_BADSESSION:
			case -NFS4ERR_BADSLOT:
			case -NFS4ERR_BAD_HIGH_SLOT:
			case -NFS4ERR_CONN_NOT_BOUND_TO_SESSION:
			case -NFS4ERR_DEADSESSION:
				nfs4_schedule_session_recovery(server->nfs_client->cl_session);
				goto out;
			case -NFS4ERR_STALE_CLIENTID:
			case -NFS4ERR_STALE_STATEID:
			case -NFS4ERR_EXPIRED:
				/* Don't recall a delegation if it was lost */
				nfs4_schedule_lease_recovery(server->nfs_client);
				goto out;
			case -ERESTARTSYS:
				/*
				 * The show must go on: exit, but mark the
				 * stateid as needing recovery.
				 */
			case -NFS4ERR_DELEG_REVOKED:
			case -NFS4ERR_ADMIN_REVOKED:
			case -NFS4ERR_BAD_STATEID:
				nfs_inode_find_state_and_recover(state->inode,
						stateid);
				nfs4_schedule_stateid_recovery(server, state);
			case -EKEYEXPIRED:
				/*
				 * User RPCSEC_GSS context has expired.
				 * We cannot recover this stateid now, so
				 * skip it and allow recovery thread to
				 * proceed.
				 */
			case -ENOMEM:
				err = 0;
				goto out;
		}
		err = nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);
out:
	return err;
}

static void nfs4_open_confirm_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_opendata *data = calldata;

	data->rpc_status = task->tk_status;
	if (data->rpc_status == 0) {
		memcpy(data->o_res.stateid.data, data->c_res.stateid.data,
				sizeof(data->o_res.stateid.data));
		nfs_confirm_seqid(&data->owner->so_seqid, 0);
		renew_lease(data->o_res.server, data->timestamp);
		data->rpc_done = 1;
	}
}

static void nfs4_open_confirm_release(void *calldata)
{
	struct nfs4_opendata *data = calldata;
	struct nfs4_state *state = NULL;

	/* If this request hasn't been cancelled, do nothing */
	if (data->cancelled == 0)
		goto out_free;
	/* In case of error, no cleanup! */
	if (!data->rpc_done)
		goto out_free;
	state = nfs4_opendata_to_nfs4_state(data);
	if (!IS_ERR(state))
		nfs4_close_state(&data->path, state, data->o_arg.fmode);
out_free:
	nfs4_opendata_put(data);
}

static const struct rpc_call_ops nfs4_open_confirm_ops = {
	.rpc_call_done = nfs4_open_confirm_done,
	.rpc_release = nfs4_open_confirm_release,
};

/*
 * Note: On error, nfs4_proc_open_confirm will free the struct nfs4_opendata
 */
static int _nfs4_proc_open_confirm(struct nfs4_opendata *data)
{
	struct nfs_server *server = NFS_SERVER(data->dir->d_inode);
	struct rpc_task *task;
	struct  rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_OPEN_CONFIRM],
		.rpc_argp = &data->c_arg,
		.rpc_resp = &data->c_res,
		.rpc_cred = data->owner->so_cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = server->client,
		.rpc_message = &msg,
		.callback_ops = &nfs4_open_confirm_ops,
		.callback_data = data,
		.workqueue = nfsiod_workqueue,
		.flags = RPC_TASK_ASYNC,
	};
	int status;

	kref_get(&data->kref);
	data->rpc_done = 0;
	data->rpc_status = 0;
	data->timestamp = jiffies;
	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	status = nfs4_wait_for_completion_rpc_task(task);
	if (status != 0) {
		data->cancelled = 1;
		smp_wmb();
	} else
		status = data->rpc_status;
	rpc_put_task(task);
	return status;
}

static void nfs4_open_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs4_opendata *data = calldata;
	struct nfs4_state_owner *sp = data->owner;

	if (nfs_wait_on_sequence(data->o_arg.seqid, task) != 0)
		return;
	/*
	 * Check if we still need to send an OPEN call, or if we can use
	 * a delegation instead.
	 */
	if (data->state != NULL) {
		struct nfs_delegation *delegation;

		if (can_open_cached(data->state, data->o_arg.fmode, data->o_arg.open_flags))
			goto out_no_action;
		rcu_read_lock();
		delegation = rcu_dereference(NFS_I(data->state->inode)->delegation);
		if (delegation != NULL &&
		    test_bit(NFS_DELEGATION_NEED_RECLAIM, &delegation->flags) == 0) {
			rcu_read_unlock();
			goto out_no_action;
		}
		rcu_read_unlock();
	}
	/* Update sequence id. */
	data->o_arg.id = sp->so_owner_id.id;
	data->o_arg.clientid = sp->so_server->nfs_client->cl_clientid;
	if (data->o_arg.claim == NFS4_OPEN_CLAIM_PREVIOUS) {
		task->tk_msg.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_OPEN_NOATTR];
		nfs_copy_fh(&data->o_res.fh, data->o_arg.fh);
	}
	data->timestamp = jiffies;
	if (nfs4_setup_sequence(data->o_arg.server,
				&data->o_arg.seq_args,
				&data->o_res.seq_res, 1, task))
		return;
	rpc_call_start(task);
	return;
out_no_action:
	task->tk_action = NULL;

}

static void nfs4_recover_open_prepare(struct rpc_task *task, void *calldata)
{
	rpc_task_set_priority(task, RPC_PRIORITY_PRIVILEGED);
	nfs4_open_prepare(task, calldata);
}

static void nfs4_open_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_opendata *data = calldata;

	data->rpc_status = task->tk_status;

	if (!nfs4_sequence_done(task, &data->o_res.seq_res))
		return;

	if (task->tk_status == 0) {
		switch (data->o_res.f_attr->mode & S_IFMT) {
			case S_IFREG:
				break;
			case S_IFLNK:
				data->rpc_status = -ELOOP;
				break;
			case S_IFDIR:
				data->rpc_status = -EISDIR;
				break;
			default:
				data->rpc_status = -ENOTDIR;
		}
		renew_lease(data->o_res.server, data->timestamp);
		if (!(data->o_res.rflags & NFS4_OPEN_RESULT_CONFIRM))
			nfs_confirm_seqid(&data->owner->so_seqid, 0);
	}
	data->rpc_done = 1;
}

static void nfs4_open_release(void *calldata)
{
	struct nfs4_opendata *data = calldata;
	struct nfs4_state *state = NULL;

	/* If this request hasn't been cancelled, do nothing */
	if (data->cancelled == 0)
		goto out_free;
	/* In case of error, no cleanup! */
	if (data->rpc_status != 0 || !data->rpc_done)
		goto out_free;
	/* In case we need an open_confirm, no cleanup! */
	if (data->o_res.rflags & NFS4_OPEN_RESULT_CONFIRM)
		goto out_free;
	state = nfs4_opendata_to_nfs4_state(data);
	if (!IS_ERR(state))
		nfs4_close_state(&data->path, state, data->o_arg.fmode);
out_free:
	nfs4_opendata_put(data);
}

static const struct rpc_call_ops nfs4_open_ops = {
	.rpc_call_prepare = nfs4_open_prepare,
	.rpc_call_done = nfs4_open_done,
	.rpc_release = nfs4_open_release,
};

static const struct rpc_call_ops nfs4_recover_open_ops = {
	.rpc_call_prepare = nfs4_recover_open_prepare,
	.rpc_call_done = nfs4_open_done,
	.rpc_release = nfs4_open_release,
};

static int nfs4_run_open_task(struct nfs4_opendata *data, int isrecover)
{
	struct inode *dir = data->dir->d_inode;
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs_openargs *o_arg = &data->o_arg;
	struct nfs_openres *o_res = &data->o_res;
	struct rpc_task *task;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_OPEN],
		.rpc_argp = o_arg,
		.rpc_resp = o_res,
		.rpc_cred = data->owner->so_cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = server->client,
		.rpc_message = &msg,
		.callback_ops = &nfs4_open_ops,
		.callback_data = data,
		.workqueue = nfsiod_workqueue,
		.flags = RPC_TASK_ASYNC,
	};
	int status;

	kref_get(&data->kref);
	data->rpc_done = 0;
	data->rpc_status = 0;
	data->cancelled = 0;
	if (isrecover)
		task_setup_data.callback_ops = &nfs4_recover_open_ops;
	task = rpc_run_task(&task_setup_data);
        if (IS_ERR(task))
                return PTR_ERR(task);
        status = nfs4_wait_for_completion_rpc_task(task);
        if (status != 0) {
                data->cancelled = 1;
                smp_wmb();
        } else
                status = data->rpc_status;
        rpc_put_task(task);

	return status;
}

static int _nfs4_recover_proc_open(struct nfs4_opendata *data)
{
	struct inode *dir = data->dir->d_inode;
	struct nfs_openres *o_res = &data->o_res;
        int status;

	status = nfs4_run_open_task(data, 1);
	if (status != 0 || !data->rpc_done)
		return status;

	nfs_refresh_inode(dir, o_res->dir_attr);

	if (o_res->rflags & NFS4_OPEN_RESULT_CONFIRM) {
		status = _nfs4_proc_open_confirm(data);
		if (status != 0)
			return status;
	}

	return status;
}

/*
 * Note: On error, nfs4_proc_open will free the struct nfs4_opendata
 */
static int _nfs4_proc_open(struct nfs4_opendata *data)
{
	struct inode *dir = data->dir->d_inode;
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs_openargs *o_arg = &data->o_arg;
	struct nfs_openres *o_res = &data->o_res;
	int status;

	status = nfs4_run_open_task(data, 0);
	if (status != 0 || !data->rpc_done)
		return status;

	if (o_arg->open_flags & O_CREAT) {
		update_changeattr(dir, &o_res->cinfo);
		nfs_post_op_update_inode(dir, o_res->dir_attr);
	} else
		nfs_refresh_inode(dir, o_res->dir_attr);
	if ((o_res->rflags & NFS4_OPEN_RESULT_LOCKTYPE_POSIX) == 0)
		server->caps &= ~NFS_CAP_POSIX_LOCK;
	if(o_res->rflags & NFS4_OPEN_RESULT_CONFIRM) {
		status = _nfs4_proc_open_confirm(data);
		if (status != 0)
			return status;
	}
	if (!(o_res->f_attr->valid & NFS_ATTR_FATTR))
		_nfs4_proc_getattr(server, &o_res->fh, o_res->f_attr);
	return 0;
}

static int nfs4_client_recover_expired_lease(struct nfs_client *clp)
{
	unsigned int loop;
	int ret;

	for (loop = NFS4_MAX_LOOP_ON_RECOVER; loop != 0; loop--) {
		ret = nfs4_wait_clnt_recover(clp);
		if (ret != 0)
			break;
		if (!test_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state) &&
		    !test_bit(NFS4CLNT_CHECK_LEASE,&clp->cl_state))
			break;
		nfs4_schedule_state_manager(clp);
		ret = -EIO;
	}
	return ret;
}

static int nfs4_recover_expired_lease(struct nfs_server *server)
{
	return nfs4_client_recover_expired_lease(server->nfs_client);
}

/*
 * OPEN_EXPIRED:
 * 	reclaim state on the server after a network partition.
 * 	Assumes caller holds the appropriate lock
 */
static int _nfs4_open_expired(struct nfs_open_context *ctx, struct nfs4_state *state)
{
	struct nfs4_opendata *opendata;
	int ret;

	opendata = nfs4_open_recoverdata_alloc(ctx, state);
	if (IS_ERR(opendata))
		return PTR_ERR(opendata);
	ret = nfs4_open_recover(opendata, state);
	if (ret == -ESTALE)
		d_drop(ctx->path.dentry);
	nfs4_opendata_put(opendata);
	return ret;
}

static int nfs4_do_open_expired(struct nfs_open_context *ctx, struct nfs4_state *state)
{
	struct nfs_server *server = NFS_SERVER(state->inode);
	struct nfs4_exception exception = { };
	int err;

	do {
		err = _nfs4_open_expired(ctx, state);
		switch (err) {
		default:
			goto out;
		case -NFS4ERR_GRACE:
		case -NFS4ERR_DELAY:
			nfs4_handle_exception(server, err, &exception);
			err = 0;
		}
	} while (exception.retry);
out:
	return err;
}

static int nfs4_open_expired(struct nfs4_state_owner *sp, struct nfs4_state *state)
{
	struct nfs_open_context *ctx;
	int ret;

	ctx = nfs4_state_find_open_context(state);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);
	ret = nfs4_do_open_expired(ctx, state);
	put_nfs_open_context(ctx);
	return ret;
}

/*
 * on an EXCLUSIVE create, the server should send back a bitmask with FATTR4-*
 * fields corresponding to attributes that were used to store the verifier.
 * Make sure we clobber those fields in the later setattr call
 */
static inline void nfs4_exclusive_attrset(struct nfs4_opendata *opendata, struct iattr *sattr)
{
	if ((opendata->o_res.attrset[1] & FATTR4_WORD1_TIME_ACCESS) &&
	    !(sattr->ia_valid & ATTR_ATIME_SET))
		sattr->ia_valid |= ATTR_ATIME;

	if ((opendata->o_res.attrset[1] & FATTR4_WORD1_TIME_MODIFY) &&
	    !(sattr->ia_valid & ATTR_MTIME_SET))
		sattr->ia_valid |= ATTR_MTIME;
}

/*
 * Returns a referenced nfs4_state
 */
static int _nfs4_do_open(struct inode *dir, struct path *path, fmode_t fmode, int flags, struct iattr *sattr, struct rpc_cred *cred, struct nfs4_state **res)
{
	struct nfs4_state_owner  *sp;
	struct nfs4_state     *state = NULL;
	struct nfs_server       *server = NFS_SERVER(dir);
	struct nfs4_opendata *opendata;
	int status;

	/* Protect against reboot recovery conflicts */
	status = -ENOMEM;
	if (!(sp = nfs4_get_state_owner(server, cred))) {
		dprintk("nfs4_do_open: nfs4_get_state_owner failed!\n");
		goto out_err;
	}
	status = nfs4_recover_expired_lease(server);
	if (status != 0)
		goto err_put_state_owner;
	if (path->dentry->d_inode != NULL)
		nfs4_return_incompatible_delegation(path->dentry->d_inode, fmode);
	status = -ENOMEM;
	opendata = nfs4_opendata_alloc(path, sp, fmode, flags, sattr, GFP_KERNEL);
	if (opendata == NULL)
		goto err_put_state_owner;

	if (path->dentry->d_inode != NULL)
		opendata->state = nfs4_get_open_state(path->dentry->d_inode, sp);

	status = _nfs4_proc_open(opendata);
	if (status != 0)
		goto err_opendata_put;

	state = nfs4_opendata_to_nfs4_state(opendata);
	status = PTR_ERR(state);
	if (IS_ERR(state))
		goto err_opendata_put;
	if (server->caps & NFS_CAP_POSIX_LOCK)
		set_bit(NFS_STATE_POSIX_LOCKS, &state->flags);

	if (opendata->o_arg.open_flags & O_EXCL) {
		nfs4_exclusive_attrset(opendata, sattr);

		nfs_fattr_init(opendata->o_res.f_attr);
		status = nfs4_do_setattr(state->inode, cred,
				opendata->o_res.f_attr, sattr,
				state);
		if (status == 0)
			nfs_setattr_update_inode(state->inode, sattr);
		nfs_post_op_update_inode(state->inode, opendata->o_res.f_attr);
	}
	nfs_revalidate_inode(server, state->inode);
	nfs4_opendata_put(opendata);
	nfs4_put_state_owner(sp);
	*res = state;
	return 0;
err_opendata_put:
	nfs4_opendata_put(opendata);
err_put_state_owner:
	nfs4_put_state_owner(sp);
out_err:
	*res = NULL;
	return status;
}


static struct nfs4_state *nfs4_do_open(struct inode *dir, struct path *path, fmode_t fmode, int flags, struct iattr *sattr, struct rpc_cred *cred)
{
	struct nfs4_exception exception = { };
	struct nfs4_state *res;
	int status;

	do {
		status = _nfs4_do_open(dir, path, fmode, flags, sattr, cred, &res);
		if (status == 0)
			break;
		/* NOTE: BAD_SEQID means the server and client disagree about the
		 * book-keeping w.r.t. state-changing operations
		 * (OPEN/CLOSE/LOCK/LOCKU...)
		 * It is actually a sign of a bug on the client or on the server.
		 *
		 * If we receive a BAD_SEQID error in the particular case of
		 * doing an OPEN, we assume that nfs_increment_open_seqid() will
		 * have unhashed the old state_owner for us, and that we can
		 * therefore safely retry using a new one. We should still warn
		 * the user though...
		 */
		if (status == -NFS4ERR_BAD_SEQID) {
			printk(KERN_WARNING "NFS: v4 server %s "
					" returned a bad sequence-id error!\n",
					NFS_SERVER(dir)->nfs_client->cl_hostname);
			exception.retry = 1;
			continue;
		}
		/*
		 * BAD_STATEID on OPEN means that the server cancelled our
		 * state before it received the OPEN_CONFIRM.
		 * Recover by retrying the request as per the discussion
		 * on Page 181 of RFC3530.
		 */
		if (status == -NFS4ERR_BAD_STATEID) {
			exception.retry = 1;
			continue;
		}
		if (status == -EAGAIN) {
			/* We must have found a delegation */
			exception.retry = 1;
			continue;
		}
		res = ERR_PTR(nfs4_handle_exception(NFS_SERVER(dir),
					status, &exception));
	} while (exception.retry);
	return res;
}

static int _nfs4_do_setattr(struct inode *inode, struct rpc_cred *cred,
			    struct nfs_fattr *fattr, struct iattr *sattr,
			    struct nfs4_state *state)
{
	struct nfs_server *server = NFS_SERVER(inode);
        struct nfs_setattrargs  arg = {
                .fh             = NFS_FH(inode),
                .iap            = sattr,
		.server		= server,
		.bitmask = server->attr_bitmask,
        };
        struct nfs_setattrres  res = {
		.fattr		= fattr,
		.server		= server,
        };
        struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_SETATTR],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
		.rpc_cred	= cred,
        };
	unsigned long timestamp = jiffies;
	int status;

	nfs_fattr_init(fattr);

	if (nfs4_copy_delegation_stateid(&arg.stateid, inode)) {
		/* Use that stateid */
	} else if (state != NULL) {
		nfs4_copy_stateid(&arg.stateid, state, current->files, current->tgid);
	} else
		memcpy(&arg.stateid, &zero_stateid, sizeof(arg.stateid));

	status = nfs4_call_sync(server->client, server, &msg, &arg.seq_args, &res.seq_res, 1);
	if (status == 0 && state != NULL)
		renew_lease(server, timestamp);
	return status;
}

static int nfs4_do_setattr(struct inode *inode, struct rpc_cred *cred,
			   struct nfs_fattr *fattr, struct iattr *sattr,
			   struct nfs4_state *state)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs4_exception exception = {
		.state = state,
		.inode = inode,
	};
	int err;
	do {
		err = nfs4_handle_exception(server,
				_nfs4_do_setattr(inode, cred, fattr, sattr, state),
				&exception);
	} while (exception.retry);
	return err;
}

struct nfs4_closedata {
	struct path path;
	struct inode *inode;
	struct nfs4_state *state;
	struct nfs_closeargs arg;
	struct nfs_closeres res;
	struct nfs_fattr fattr;
	unsigned long timestamp;
	bool roc;
	u32 roc_barrier;
};

static void nfs4_free_closedata(void *data)
{
	struct nfs4_closedata *calldata = data;
	struct nfs4_state_owner *sp = calldata->state->owner;

	if (calldata->roc)
		pnfs_roc_release(calldata->state->inode);
	nfs4_put_open_state(calldata->state);
	nfs_free_seqid(calldata->arg.seqid);
	nfs4_put_state_owner(sp);
	path_put(&calldata->path);
	kfree(calldata);
}

static void nfs4_close_clear_stateid_flags(struct nfs4_state *state,
		fmode_t fmode)
{
	spin_lock(&state->owner->so_lock);
	if (!(fmode & FMODE_READ))
		clear_bit(NFS_O_RDONLY_STATE, &state->flags);
	if (!(fmode & FMODE_WRITE))
		clear_bit(NFS_O_WRONLY_STATE, &state->flags);
	clear_bit(NFS_O_RDWR_STATE, &state->flags);
	spin_unlock(&state->owner->so_lock);
}

static void nfs4_close_done(struct rpc_task *task, void *data)
{
	struct nfs4_closedata *calldata = data;
	struct nfs4_state *state = calldata->state;
	struct nfs_server *server = NFS_SERVER(calldata->inode);

	if (!nfs4_sequence_done(task, &calldata->res.seq_res))
		return;
        /* hmm. we are done with the inode, and in the process of freeing
	 * the state_owner. we keep this around to process errors
	 */
	switch (task->tk_status) {
		case 0:
			if (calldata->roc)
				pnfs_roc_set_barrier(state->inode,
						     calldata->roc_barrier);
			nfs_set_open_stateid(state, &calldata->res.stateid, 0);
			renew_lease(server, calldata->timestamp);
			nfs4_close_clear_stateid_flags(state,
					calldata->arg.fmode);
			break;
		case -NFS4ERR_STALE_STATEID:
		case -NFS4ERR_OLD_STATEID:
		case -NFS4ERR_BAD_STATEID:
		case -NFS4ERR_EXPIRED:
			if (calldata->arg.fmode == 0)
				break;
		default:
			if (nfs4_async_handle_error(task, server, state) == -EAGAIN)
				rpc_restart_call_prepare(task);
	}
	nfs_release_seqid(calldata->arg.seqid);
	nfs_refresh_inode(calldata->inode, calldata->res.fattr);
}

static void nfs4_close_prepare(struct rpc_task *task, void *data)
{
	struct nfs4_closedata *calldata = data;
	struct nfs4_state *state = calldata->state;
	int call_close = 0;

	if (nfs_wait_on_sequence(calldata->arg.seqid, task) != 0)
		return;

	task->tk_msg.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_OPEN_DOWNGRADE];
	calldata->arg.fmode = FMODE_READ|FMODE_WRITE;
	spin_lock(&state->owner->so_lock);
	/* Calculate the change in open mode */
	if (state->n_rdwr == 0) {
		if (state->n_rdonly == 0) {
			call_close |= test_bit(NFS_O_RDONLY_STATE, &state->flags);
			call_close |= test_bit(NFS_O_RDWR_STATE, &state->flags);
			calldata->arg.fmode &= ~FMODE_READ;
		}
		if (state->n_wronly == 0) {
			call_close |= test_bit(NFS_O_WRONLY_STATE, &state->flags);
			call_close |= test_bit(NFS_O_RDWR_STATE, &state->flags);
			calldata->arg.fmode &= ~FMODE_WRITE;
		}
	}
	spin_unlock(&state->owner->so_lock);

	if (!call_close) {
		/* Note: exit _without_ calling nfs4_close_done */
		task->tk_action = NULL;
		return;
	}

	if (calldata->arg.fmode == 0) {
		task->tk_msg.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_CLOSE];
		if (calldata->roc &&
		    pnfs_roc_drain(calldata->inode, &calldata->roc_barrier)) {
			rpc_sleep_on(&NFS_SERVER(calldata->inode)->roc_rpcwaitq,
				     task, NULL);
			return;
		}
	}

	nfs_fattr_init(calldata->res.fattr);
	calldata->timestamp = jiffies;
	if (nfs4_setup_sequence(NFS_SERVER(calldata->inode),
				&calldata->arg.seq_args, &calldata->res.seq_res,
				1, task))
		return;
	rpc_call_start(task);
}

static const struct rpc_call_ops nfs4_close_ops = {
	.rpc_call_prepare = nfs4_close_prepare,
	.rpc_call_done = nfs4_close_done,
	.rpc_release = nfs4_free_closedata,
};

/* 
 * It is possible for data to be read/written from a mem-mapped file 
 * after the sys_close call (which hits the vfs layer as a flush).
 * This means that we can't safely call nfsv4 close on a file until 
 * the inode is cleared. This in turn means that we are not good
 * NFSv4 citizens - we do not indicate to the server to update the file's 
 * share state even when we are done with one of the three share 
 * stateid's in the inode.
 *
 * NOTE: Caller must be holding the sp->so_owner semaphore!
 */
int nfs4_do_close(struct path *path, struct nfs4_state *state, gfp_t gfp_mask, int wait, bool roc)
{
	struct nfs_server *server = NFS_SERVER(state->inode);
	struct nfs4_closedata *calldata;
	struct nfs4_state_owner *sp = state->owner;
	struct rpc_task *task;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_CLOSE],
		.rpc_cred = state->owner->so_cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = server->client,
		.rpc_message = &msg,
		.callback_ops = &nfs4_close_ops,
		.workqueue = nfsiod_workqueue,
		.flags = RPC_TASK_ASYNC,
	};
	int status = -ENOMEM;

	calldata = kzalloc(sizeof(*calldata), gfp_mask);
	if (calldata == NULL)
		goto out;
	calldata->inode = state->inode;
	calldata->state = state;
	calldata->arg.fh = NFS_FH(state->inode);
	calldata->arg.stateid = &state->open_stateid;
	/* Serialization for the sequence id */
	calldata->arg.seqid = nfs_alloc_seqid(&state->owner->so_seqid, gfp_mask);
	if (calldata->arg.seqid == NULL)
		goto out_free_calldata;
	calldata->arg.fmode = 0;
	calldata->arg.bitmask = server->cache_consistency_bitmask;
	calldata->res.fattr = &calldata->fattr;
	calldata->res.seqid = calldata->arg.seqid;
	calldata->res.server = server;
	calldata->roc = roc;
	path_get(path);
	calldata->path = *path;

	msg.rpc_argp = &calldata->arg;
	msg.rpc_resp = &calldata->res;
	task_setup_data.callback_data = calldata;
	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	status = 0;
	if (wait)
		status = rpc_wait_for_completion_task(task);
	rpc_put_task(task);
	return status;
out_free_calldata:
	kfree(calldata);
out:
	if (roc)
		pnfs_roc_release(state->inode);
	nfs4_put_open_state(state);
	nfs4_put_state_owner(sp);
	return status;
}

static struct inode *
nfs4_atomic_open(struct inode *dir, struct nfs_open_context *ctx, int open_flags, struct iattr *attr)
{
	struct nfs4_state *state;

	/* Protect against concurrent sillydeletes */
	state = nfs4_do_open(dir, &ctx->path, ctx->mode, open_flags, attr, ctx->cred);
	if (IS_ERR(state))
		return ERR_CAST(state);
	ctx->state = state;
	return igrab(state->inode);
}

static void nfs4_close_context(struct nfs_open_context *ctx, int is_sync)
{
	if (ctx->state == NULL)
		return;
	if (is_sync)
		nfs4_close_sync(&ctx->path, ctx->state, ctx->mode);
	else
		nfs4_close_state(&ctx->path, ctx->state, ctx->mode);
}

static int _nfs4_server_capabilities(struct nfs_server *server, struct nfs_fh *fhandle)
{
	struct nfs4_server_caps_arg args = {
		.fhandle = fhandle,
	};
	struct nfs4_server_caps_res res = {};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SERVER_CAPS],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	int status;

	status = nfs4_call_sync(server->client, server, &msg, &args.seq_args, &res.seq_res, 0);
	if (status == 0) {
		memcpy(server->attr_bitmask, res.attr_bitmask, sizeof(server->attr_bitmask));
		server->caps &= ~(NFS_CAP_ACLS|NFS_CAP_HARDLINKS|
				NFS_CAP_SYMLINKS|NFS_CAP_FILEID|
				NFS_CAP_MODE|NFS_CAP_NLINK|NFS_CAP_OWNER|
				NFS_CAP_OWNER_GROUP|NFS_CAP_ATIME|
				NFS_CAP_CTIME|NFS_CAP_MTIME);
		if (res.attr_bitmask[0] & FATTR4_WORD0_ACL)
			server->caps |= NFS_CAP_ACLS;
		if (res.has_links != 0)
			server->caps |= NFS_CAP_HARDLINKS;
		if (res.has_symlinks != 0)
			server->caps |= NFS_CAP_SYMLINKS;
		if (res.attr_bitmask[0] & FATTR4_WORD0_FILEID)
			server->caps |= NFS_CAP_FILEID;
		if (res.attr_bitmask[1] & FATTR4_WORD1_MODE)
			server->caps |= NFS_CAP_MODE;
		if (res.attr_bitmask[1] & FATTR4_WORD1_NUMLINKS)
			server->caps |= NFS_CAP_NLINK;
		if (res.attr_bitmask[1] & FATTR4_WORD1_OWNER)
			server->caps |= NFS_CAP_OWNER;
		if (res.attr_bitmask[1] & FATTR4_WORD1_OWNER_GROUP)
			server->caps |= NFS_CAP_OWNER_GROUP;
		if (res.attr_bitmask[1] & FATTR4_WORD1_TIME_ACCESS)
			server->caps |= NFS_CAP_ATIME;
		if (res.attr_bitmask[1] & FATTR4_WORD1_TIME_METADATA)
			server->caps |= NFS_CAP_CTIME;
		if (res.attr_bitmask[1] & FATTR4_WORD1_TIME_MODIFY)
			server->caps |= NFS_CAP_MTIME;

		memcpy(server->cache_consistency_bitmask, res.attr_bitmask, sizeof(server->cache_consistency_bitmask));
		server->cache_consistency_bitmask[0] &= FATTR4_WORD0_CHANGE|FATTR4_WORD0_SIZE;
		server->cache_consistency_bitmask[1] &= FATTR4_WORD1_TIME_METADATA|FATTR4_WORD1_TIME_MODIFY;
		server->acl_bitmask = res.acl_bitmask;
	}

	return status;
}

int nfs4_server_capabilities(struct nfs_server *server, struct nfs_fh *fhandle)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(server,
				_nfs4_server_capabilities(server, fhandle),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_lookup_root(struct nfs_server *server, struct nfs_fh *fhandle,
		struct nfs_fsinfo *info)
{
	struct nfs4_lookup_root_arg args = {
		.bitmask = nfs4_fattr_bitmap,
	};
	struct nfs4_lookup_res res = {
		.server = server,
		.fattr = info->fattr,
		.fh = fhandle,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LOOKUP_ROOT],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};

	nfs_fattr_init(info->fattr);
	return nfs4_call_sync(server->client, server, &msg, &args.seq_args, &res.seq_res, 0);
}

static int nfs4_lookup_root(struct nfs_server *server, struct nfs_fh *fhandle,
		struct nfs_fsinfo *info)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = _nfs4_lookup_root(server, fhandle, info);
		switch (err) {
		case 0:
		case -NFS4ERR_WRONGSEC:
			break;
		default:
			err = nfs4_handle_exception(server, err, &exception);
		}
	} while (exception.retry);
	return err;
}

static int nfs4_lookup_root_sec(struct nfs_server *server, struct nfs_fh *fhandle,
				struct nfs_fsinfo *info, rpc_authflavor_t flavor)
{
	struct rpc_auth *auth;
	int ret;

	auth = rpcauth_create(flavor, server->client);
	if (!auth) {
		ret = -EIO;
		goto out;
	}
	ret = nfs4_lookup_root(server, fhandle, info);
out:
	return ret;
}

static int nfs4_find_root_sec(struct nfs_server *server, struct nfs_fh *fhandle,
			      struct nfs_fsinfo *info)
{
	int i, len, status = 0;
	rpc_authflavor_t flav_array[NFS_MAX_SECFLAVORS];

	len = gss_mech_list_pseudoflavors(&flav_array[0]);
	flav_array[len] = RPC_AUTH_NULL;
	len += 1;

	for (i = 0; i < len; i++) {
		status = nfs4_lookup_root_sec(server, fhandle, info, flav_array[i]);
		if (status == -NFS4ERR_WRONGSEC || status == -EACCES)
			continue;
		break;
	}
	/*
	 * -EACCESS could mean that the user doesn't have correct permissions
	 * to access the mount.  It could also mean that we tried to mount
	 * with a gss auth flavor, but rpc.gssd isn't running.  Either way,
	 * existing mount programs don't handle -EACCES very well so it should
	 * be mapped to -EPERM instead.
	 */
	if (status == -EACCES)
		status = -EPERM;
	return status;
}

/*
 * get the file handle for the "/" directory on the server
 */
static int nfs4_proc_get_root(struct nfs_server *server, struct nfs_fh *fhandle,
			      struct nfs_fsinfo *info)
{
	int status = nfs4_lookup_root(server, fhandle, info);
	if ((status == -NFS4ERR_WRONGSEC) && !(server->flags & NFS_MOUNT_SECFLAVOUR))
		/*
		 * A status of -NFS4ERR_WRONGSEC will be mapped to -EPERM
		 * by nfs4_map_errors() as this function exits.
		 */
		status = nfs4_find_root_sec(server, fhandle, info);
	if (status == 0)
		status = nfs4_server_capabilities(server, fhandle);
	if (status == 0)
		status = nfs4_do_fsinfo(server, fhandle, info);
	return nfs4_map_errors(status);
}

static void nfs_fixup_referral_attributes(struct nfs_fattr *fattr);
/*
 * Get locations and (maybe) other attributes of a referral.
 * Note that we'll actually follow the referral later when
 * we detect fsid mismatch in inode revalidation
 */
static int nfs4_get_referral(struct inode *dir, const struct qstr *name,
			     struct nfs_fattr *fattr, struct nfs_fh *fhandle)
{
	int status = -ENOMEM;
	struct page *page = NULL;
	struct nfs4_fs_locations *locations = NULL;

	page = alloc_page(GFP_KERNEL);
	if (page == NULL)
		goto out;
	locations = kmalloc(sizeof(struct nfs4_fs_locations), GFP_KERNEL);
	if (locations == NULL)
		goto out;

	status = nfs4_proc_fs_locations(dir, name, locations, page);
	if (status != 0)
		goto out;
	/* Make sure server returned a different fsid for the referral */
	if (nfs_fsid_equal(&NFS_SERVER(dir)->fsid, &locations->fattr.fsid)) {
		dprintk("%s: server did not return a different fsid for"
			" a referral at %s\n", __func__, name->name);
		status = -EIO;
		goto out;
	}
	/* Fixup attributes for the nfs_lookup() call to nfs_fhget() */
	nfs_fixup_referral_attributes(&locations->fattr);

	/* replace the lookup nfs_fattr with the locations nfs_fattr */
	memcpy(fattr, &locations->fattr, sizeof(struct nfs_fattr));
	memset(fhandle, 0, sizeof(struct nfs_fh));
out:
	if (page)
		__free_page(page);
	kfree(locations);
	return status;
}

static int _nfs4_proc_getattr(struct nfs_server *server, struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs4_getattr_arg args = {
		.fh = fhandle,
		.bitmask = server->attr_bitmask,
	};
	struct nfs4_getattr_res res = {
		.fattr = fattr,
		.server = server,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_GETATTR],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	
	nfs_fattr_init(fattr);
	return nfs4_call_sync(server->client, server, &msg, &args.seq_args, &res.seq_res, 0);
}

static int nfs4_proc_getattr(struct nfs_server *server, struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(server,
				_nfs4_proc_getattr(server, fhandle, fattr),
				&exception);
	} while (exception.retry);
	return err;
}

/* 
 * The file is not closed if it is opened due to the a request to change
 * the size of the file. The open call will not be needed once the
 * VFS layer lookup-intents are implemented.
 *
 * Close is called when the inode is destroyed.
 * If we haven't opened the file for O_WRONLY, we
 * need to in the size_change case to obtain a stateid.
 *
 * Got race?
 * Because OPEN is always done by name in nfsv4, it is
 * possible that we opened a different file by the same
 * name.  We can recognize this race condition, but we
 * can't do anything about it besides returning an error.
 *
 * This will be fixed with VFS changes (lookup-intent).
 */
static int
nfs4_proc_setattr(struct dentry *dentry, struct nfs_fattr *fattr,
		  struct iattr *sattr)
{
	struct inode *inode = dentry->d_inode;
	struct rpc_cred *cred = NULL;
	struct nfs4_state *state = NULL;
	int status;

	if (pnfs_ld_layoutret_on_setattr(inode))
		pnfs_return_layout(inode);

	nfs_fattr_init(fattr);
	
	/* Search for an existing open(O_WRITE) file */
	if (sattr->ia_valid & ATTR_FILE) {
		struct nfs_open_context *ctx;

		ctx = nfs_file_open_context(sattr->ia_file);
		if (ctx) {
			cred = ctx->cred;
			state = ctx->state;
		}
	}

	status = nfs4_do_setattr(inode, cred, fattr, sattr, state);
	if (status == 0)
		nfs_setattr_update_inode(inode, sattr);
	return status;
}

static int _nfs4_proc_lookupfh(struct rpc_clnt *clnt, struct nfs_server *server,
		const struct nfs_fh *dirfh, const struct qstr *name,
		struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	int		       status;
	struct nfs4_lookup_arg args = {
		.bitmask = server->attr_bitmask,
		.dir_fh = dirfh,
		.name = name,
	};
	struct nfs4_lookup_res res = {
		.server = server,
		.fattr = fattr,
		.fh = fhandle,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LOOKUP],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};

	nfs_fattr_init(fattr);

	dprintk("NFS call  lookupfh %s\n", name->name);
	status = nfs4_call_sync(clnt, server, &msg, &args.seq_args, &res.seq_res, 0);
	dprintk("NFS reply lookupfh: %d\n", status);
	return status;
}

static int nfs4_proc_lookupfh(struct nfs_server *server, struct nfs_fh *dirfh,
			      struct qstr *name, struct nfs_fh *fhandle,
			      struct nfs_fattr *fattr)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = _nfs4_proc_lookupfh(server->client, server, dirfh, name, fhandle, fattr);
		/* FIXME: !!!! */
		if (err == -NFS4ERR_MOVED) {
			err = -EREMOTE;
			break;
		}
		err = nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_lookup(struct rpc_clnt *clnt, struct inode *dir,
		const struct qstr *name, struct nfs_fh *fhandle,
		struct nfs_fattr *fattr)
{
	int status;
	
	dprintk("NFS call  lookup %s\n", name->name);
	status = _nfs4_proc_lookupfh(clnt, NFS_SERVER(dir), NFS_FH(dir), name, fhandle, fattr);
	if (status == -NFS4ERR_MOVED)
		status = nfs4_get_referral(dir, name, fattr, fhandle);
	dprintk("NFS reply lookup: %d\n", status);
	return status;
}

void nfs_fixup_secinfo_attributes(struct nfs_fattr *fattr, struct nfs_fh *fh)
{
	memset(fh, 0, sizeof(struct nfs_fh));
	fattr->fsid.major = 1;
	fattr->valid |= NFS_ATTR_FATTR_TYPE | NFS_ATTR_FATTR_MODE |
		NFS_ATTR_FATTR_NLINK | NFS_ATTR_FATTR_FSID | NFS_ATTR_FATTR_MOUNTPOINT;
	fattr->mode = S_IFDIR | S_IRUGO | S_IXUGO;
	fattr->nlink = 2;
}

static int nfs4_proc_lookup(struct rpc_clnt *clnt, struct inode *dir, struct qstr *name,
			    struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(dir),
				_nfs4_proc_lookup(clnt, dir, name, fhandle, fattr),
				&exception);
		if (err == -EPERM)
			nfs_fixup_secinfo_attributes(fattr, fhandle);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_access(struct inode *inode, struct nfs_access_entry *entry)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs4_accessargs args = {
		.fh = NFS_FH(inode),
		.bitmask = server->attr_bitmask,
	};
	struct nfs4_accessres res = {
		.server = server,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_ACCESS],
		.rpc_argp = &args,
		.rpc_resp = &res,
		.rpc_cred = entry->cred,
	};
	int mode = entry->mask;
	int status;

	/*
	 * Determine which access bits we want to ask for...
	 */
	if (mode & MAY_READ)
		args.access |= NFS4_ACCESS_READ;
	if (S_ISDIR(inode->i_mode)) {
		if (mode & MAY_WRITE)
			args.access |= NFS4_ACCESS_MODIFY | NFS4_ACCESS_EXTEND | NFS4_ACCESS_DELETE;
		if (mode & MAY_EXEC)
			args.access |= NFS4_ACCESS_LOOKUP;
	} else {
		if (mode & MAY_WRITE)
			args.access |= NFS4_ACCESS_MODIFY | NFS4_ACCESS_EXTEND;
		if (mode & MAY_EXEC)
			args.access |= NFS4_ACCESS_EXECUTE;
	}

	res.fattr = nfs_alloc_fattr();
	if (res.fattr == NULL)
		return -ENOMEM;

	status = nfs4_call_sync(server->client, server, &msg, &args.seq_args, &res.seq_res, 0);
	if (!status) {
		entry->mask = 0;
		if (res.access & NFS4_ACCESS_READ)
			entry->mask |= MAY_READ;
		if (res.access & (NFS4_ACCESS_MODIFY | NFS4_ACCESS_EXTEND | NFS4_ACCESS_DELETE))
			entry->mask |= MAY_WRITE;
		if (res.access & (NFS4_ACCESS_LOOKUP|NFS4_ACCESS_EXECUTE))
			entry->mask |= MAY_EXEC;
		nfs_refresh_inode(inode, res.fattr);
	}
	nfs_free_fattr(res.fattr);
	return status;
}

static int nfs4_proc_access(struct inode *inode, struct nfs_access_entry *entry)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(inode),
				_nfs4_proc_access(inode, entry),
				&exception);
	} while (exception.retry);
	return err;
}

/*
 * TODO: For the time being, we don't try to get any attributes
 * along with any of the zero-copy operations READ, READDIR,
 * READLINK, WRITE.
 *
 * In the case of the first three, we want to put the GETATTR
 * after the read-type operation -- this is because it is hard
 * to predict the length of a GETATTR response in v4, and thus
 * align the READ data correctly.  This means that the GETATTR
 * may end up partially falling into the page cache, and we should
 * shift it into the 'tail' of the xdr_buf before processing.
 * To do this efficiently, we need to know the total length
 * of data received, which doesn't seem to be available outside
 * of the RPC layer.
 *
 * In the case of WRITE, we also want to put the GETATTR after
 * the operation -- in this case because we want to make sure
 * we get the post-operation mtime and size.  This means that
 * we can't use xdr_encode_pages() as written: we need a variant
 * of it which would leave room in the 'tail' iovec.
 *
 * Both of these changes to the XDR layer would in fact be quite
 * minor, but I decided to leave them for a subsequent patch.
 */
static int _nfs4_proc_readlink(struct inode *inode, struct page *page,
		unsigned int pgbase, unsigned int pglen)
{
	struct nfs4_readlink args = {
		.fh       = NFS_FH(inode),
		.pgbase	  = pgbase,
		.pglen    = pglen,
		.pages    = &page,
	};
	struct nfs4_readlink_res res;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_READLINK],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};

	return nfs4_call_sync(NFS_SERVER(inode)->client, NFS_SERVER(inode), &msg, &args.seq_args, &res.seq_res, 0);
}

static int nfs4_proc_readlink(struct inode *inode, struct page *page,
		unsigned int pgbase, unsigned int pglen)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(inode),
				_nfs4_proc_readlink(inode, page, pgbase, pglen),
				&exception);
	} while (exception.retry);
	return err;
}

/*
 * Got race?
 * We will need to arrange for the VFS layer to provide an atomic open.
 * Until then, this create/open method is prone to inefficiency and race
 * conditions due to the lookup, create, and open VFS calls from sys_open()
 * placed on the wire.
 *
 * Given the above sorry state of affairs, I'm simply sending an OPEN.
 * The file will be opened again in the subsequent VFS open call
 * (nfs4_proc_file_open).
 *
 * The open for read will just hang around to be used by any process that
 * opens the file O_RDONLY. This will all be resolved with the VFS changes.
 */

static int
nfs4_proc_create(struct inode *dir, struct dentry *dentry, struct iattr *sattr,
                 int flags, struct nfs_open_context *ctx)
{
	struct path my_path = {
		.dentry = dentry,
	};
	struct path *path = &my_path;
	struct nfs4_state *state;
	struct rpc_cred *cred = NULL;
	fmode_t fmode = 0;
	int status = 0;

	if (ctx != NULL) {
		cred = ctx->cred;
		path = &ctx->path;
		fmode = ctx->mode;
	}
	sattr->ia_mode &= ~current_umask();
	state = nfs4_do_open(dir, path, fmode, flags, sattr, cred);
	d_drop(dentry);
	if (IS_ERR(state)) {
		status = PTR_ERR(state);
		goto out;
	}
	d_add(dentry, igrab(state->inode));
	nfs_set_verifier(dentry, nfs_save_change_attribute(dir));
	if (ctx != NULL)
		ctx->state = state;
	else
		nfs4_close_sync(path, state, fmode);
out:
	return status;
}

static int _nfs4_proc_remove(struct inode *dir, struct qstr *name)
{
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs_removeargs args = {
		.fh = NFS_FH(dir),
		.name.len = name->len,
		.name.name = name->name,
		.bitmask = server->attr_bitmask,
	};
	struct nfs_removeres res = {
		.server = server,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_REMOVE],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	int status = -ENOMEM;

	res.dir_attr = nfs_alloc_fattr();
	if (res.dir_attr == NULL)
		goto out;

	status = nfs4_call_sync(server->client, server, &msg, &args.seq_args, &res.seq_res, 1);
	if (status == 0) {
		update_changeattr(dir, &res.cinfo);
		nfs_post_op_update_inode(dir, res.dir_attr);
	}
	nfs_free_fattr(res.dir_attr);
out:
	return status;
}

static int nfs4_proc_remove(struct inode *dir, struct qstr *name)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(dir),
				_nfs4_proc_remove(dir, name),
				&exception);
	} while (exception.retry);
	return err;
}

static void nfs4_proc_unlink_setup(struct rpc_message *msg, struct inode *dir)
{
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs_removeargs *args = msg->rpc_argp;
	struct nfs_removeres *res = msg->rpc_resp;

	args->bitmask = server->cache_consistency_bitmask;
	res->server = server;
	res->seq_res.sr_slot = NULL;
	msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_REMOVE];
}

static int nfs4_proc_unlink_done(struct rpc_task *task, struct inode *dir)
{
	struct nfs_removeres *res = task->tk_msg.rpc_resp;

	if (!nfs4_sequence_done(task, &res->seq_res))
		return 0;
	if (nfs4_async_handle_error(task, res->server, NULL) == -EAGAIN)
		return 0;
	update_changeattr(dir, &res->cinfo);
	nfs_post_op_update_inode(dir, res->dir_attr);
	return 1;
}

static void nfs4_proc_rename_setup(struct rpc_message *msg, struct inode *dir)
{
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs_renameargs *arg = msg->rpc_argp;
	struct nfs_renameres *res = msg->rpc_resp;

	msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_RENAME];
	arg->bitmask = server->attr_bitmask;
	res->server = server;
}

static int nfs4_proc_rename_done(struct rpc_task *task, struct inode *old_dir,
				 struct inode *new_dir)
{
	struct nfs_renameres *res = task->tk_msg.rpc_resp;

	if (!nfs4_sequence_done(task, &res->seq_res))
		return 0;
	if (nfs4_async_handle_error(task, res->server, NULL) == -EAGAIN)
		return 0;

	update_changeattr(old_dir, &res->old_cinfo);
	nfs_post_op_update_inode(old_dir, res->old_fattr);
	update_changeattr(new_dir, &res->new_cinfo);
	nfs_post_op_update_inode(new_dir, res->new_fattr);
	return 1;
}

static int _nfs4_proc_rename(struct inode *old_dir, struct qstr *old_name,
		struct inode *new_dir, struct qstr *new_name)
{
	struct nfs_server *server = NFS_SERVER(old_dir);
	struct nfs_renameargs arg = {
		.old_dir = NFS_FH(old_dir),
		.new_dir = NFS_FH(new_dir),
		.old_name = old_name,
		.new_name = new_name,
		.bitmask = server->attr_bitmask,
	};
	struct nfs_renameres res = {
		.server = server,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_RENAME],
		.rpc_argp = &arg,
		.rpc_resp = &res,
	};
	int status = -ENOMEM;
	
	res.old_fattr = nfs_alloc_fattr();
	res.new_fattr = nfs_alloc_fattr();
	if (res.old_fattr == NULL || res.new_fattr == NULL)
		goto out;

	status = nfs4_call_sync(server->client, server, &msg, &arg.seq_args, &res.seq_res, 1);
	if (!status) {
		update_changeattr(old_dir, &res.old_cinfo);
		nfs_post_op_update_inode(old_dir, res.old_fattr);
		update_changeattr(new_dir, &res.new_cinfo);
		nfs_post_op_update_inode(new_dir, res.new_fattr);
	}
out:
	nfs_free_fattr(res.new_fattr);
	nfs_free_fattr(res.old_fattr);
	return status;
}

static int nfs4_proc_rename(struct inode *old_dir, struct qstr *old_name,
		struct inode *new_dir, struct qstr *new_name)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(old_dir),
				_nfs4_proc_rename(old_dir, old_name,
					new_dir, new_name),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_link(struct inode *inode, struct inode *dir, struct qstr *name)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs4_link_arg arg = {
		.fh     = NFS_FH(inode),
		.dir_fh = NFS_FH(dir),
		.name   = name,
		.bitmask = server->attr_bitmask,
	};
	struct nfs4_link_res res = {
		.server = server,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LINK],
		.rpc_argp = &arg,
		.rpc_resp = &res,
	};
	int status = -ENOMEM;

	res.fattr = nfs_alloc_fattr();
	res.dir_attr = nfs_alloc_fattr();
	if (res.fattr == NULL || res.dir_attr == NULL)
		goto out;

	status = nfs4_call_sync(server->client, server, &msg, &arg.seq_args, &res.seq_res, 1);
	if (!status) {
		update_changeattr(dir, &res.cinfo);
		nfs_post_op_update_inode(dir, res.dir_attr);
		nfs_post_op_update_inode(inode, res.fattr);
	}
out:
	nfs_free_fattr(res.dir_attr);
	nfs_free_fattr(res.fattr);
	return status;
}

static int nfs4_proc_link(struct inode *inode, struct inode *dir, struct qstr *name)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(inode),
				_nfs4_proc_link(inode, dir, name),
				&exception);
	} while (exception.retry);
	return err;
}

struct nfs4_createdata {
	struct rpc_message msg;
	struct nfs4_create_arg arg;
	struct nfs4_create_res res;
	struct nfs_fh fh;
	struct nfs_fattr fattr;
	struct nfs_fattr dir_fattr;
};

static struct nfs4_createdata *nfs4_alloc_createdata(struct inode *dir,
		struct qstr *name, struct iattr *sattr, u32 ftype)
{
	struct nfs4_createdata *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data != NULL) {
		struct nfs_server *server = NFS_SERVER(dir);

		data->msg.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_CREATE];
		data->msg.rpc_argp = &data->arg;
		data->msg.rpc_resp = &data->res;
		data->arg.dir_fh = NFS_FH(dir);
		data->arg.server = server;
		data->arg.name = name;
		data->arg.attrs = sattr;
		data->arg.ftype = ftype;
		data->arg.bitmask = server->attr_bitmask;
		data->res.server = server;
		data->res.fh = &data->fh;
		data->res.fattr = &data->fattr;
		data->res.dir_fattr = &data->dir_fattr;
		nfs_fattr_init(data->res.fattr);
		nfs_fattr_init(data->res.dir_fattr);
	}
	return data;
}

static int nfs4_do_create(struct inode *dir, struct dentry *dentry, struct nfs4_createdata *data)
{
	int status = nfs4_call_sync(NFS_SERVER(dir)->client, NFS_SERVER(dir), &data->msg,
				    &data->arg.seq_args, &data->res.seq_res, 1);
	if (status == 0) {
		update_changeattr(dir, &data->res.dir_cinfo);
		nfs_post_op_update_inode(dir, data->res.dir_fattr);
		status = nfs_instantiate(dentry, data->res.fh, data->res.fattr);
	}
	return status;
}

static void nfs4_free_createdata(struct nfs4_createdata *data)
{
	kfree(data);
}

static int _nfs4_proc_symlink(struct inode *dir, struct dentry *dentry,
		struct page *page, unsigned int len, struct iattr *sattr)
{
	struct nfs4_createdata *data;
	int status = -ENAMETOOLONG;

	if (len > NFS4_MAXPATHLEN)
		goto out;

	status = -ENOMEM;
	data = nfs4_alloc_createdata(dir, &dentry->d_name, sattr, NF4LNK);
	if (data == NULL)
		goto out;

	data->msg.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SYMLINK];
	data->arg.u.symlink.pages = &page;
	data->arg.u.symlink.len = len;
	
	status = nfs4_do_create(dir, dentry, data);

	nfs4_free_createdata(data);
out:
	return status;
}

static int nfs4_proc_symlink(struct inode *dir, struct dentry *dentry,
		struct page *page, unsigned int len, struct iattr *sattr)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(dir),
				_nfs4_proc_symlink(dir, dentry, page,
							len, sattr),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_mkdir(struct inode *dir, struct dentry *dentry,
		struct iattr *sattr)
{
	struct nfs4_createdata *data;
	int status = -ENOMEM;

	data = nfs4_alloc_createdata(dir, &dentry->d_name, sattr, NF4DIR);
	if (data == NULL)
		goto out;

	status = nfs4_do_create(dir, dentry, data);

	nfs4_free_createdata(data);
out:
	return status;
}

static int nfs4_proc_mkdir(struct inode *dir, struct dentry *dentry,
		struct iattr *sattr)
{
	struct nfs4_exception exception = { };
	int err;

	sattr->ia_mode &= ~current_umask();
	do {
		err = nfs4_handle_exception(NFS_SERVER(dir),
				_nfs4_proc_mkdir(dir, dentry, sattr),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_readdir(struct dentry *dentry, struct rpc_cred *cred,
		u64 cookie, struct page **pages, unsigned int count, int plus)
{
	struct inode		*dir = dentry->d_inode;
	struct nfs4_readdir_arg args = {
		.fh = NFS_FH(dir),
		.pages = pages,
		.pgbase = 0,
		.count = count,
		.bitmask = NFS_SERVER(dentry->d_inode)->attr_bitmask,
		.plus = plus,
	};
	struct nfs4_readdir_res res;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_READDIR],
		.rpc_argp = &args,
		.rpc_resp = &res,
		.rpc_cred = cred,
	};
	int			status;

	dprintk("%s: dentry = %s/%s, cookie = %Lu\n", __func__,
			dentry->d_parent->d_name.name,
			dentry->d_name.name,
			(unsigned long long)cookie);
	nfs4_setup_readdir(cookie, NFS_I(dir)->cookieverf, dentry, &args);
	res.pgbase = args.pgbase;
	status = nfs4_call_sync(NFS_SERVER(dir)->client, NFS_SERVER(dir), &msg, &args.seq_args, &res.seq_res, 0);
	if (status >= 0) {
		memcpy(NFS_I(dir)->cookieverf, res.verifier.data, NFS4_VERIFIER_SIZE);
		status += args.pgbase;
	}

	nfs_invalidate_atime(dir);

	dprintk("%s: returns %d\n", __func__, status);
	return status;
}

static int nfs4_proc_readdir(struct dentry *dentry, struct rpc_cred *cred,
		u64 cookie, struct page **pages, unsigned int count, int plus)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(dentry->d_inode),
				_nfs4_proc_readdir(dentry, cred, cookie,
					pages, count, plus),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_mknod(struct inode *dir, struct dentry *dentry,
		struct iattr *sattr, dev_t rdev)
{
	struct nfs4_createdata *data;
	int mode = sattr->ia_mode;
	int status = -ENOMEM;

	BUG_ON(!(sattr->ia_valid & ATTR_MODE));
	BUG_ON(!S_ISFIFO(mode) && !S_ISBLK(mode) && !S_ISCHR(mode) && !S_ISSOCK(mode));

	data = nfs4_alloc_createdata(dir, &dentry->d_name, sattr, NF4SOCK);
	if (data == NULL)
		goto out;

	if (S_ISFIFO(mode))
		data->arg.ftype = NF4FIFO;
	else if (S_ISBLK(mode)) {
		data->arg.ftype = NF4BLK;
		data->arg.u.device.specdata1 = MAJOR(rdev);
		data->arg.u.device.specdata2 = MINOR(rdev);
	}
	else if (S_ISCHR(mode)) {
		data->arg.ftype = NF4CHR;
		data->arg.u.device.specdata1 = MAJOR(rdev);
		data->arg.u.device.specdata2 = MINOR(rdev);
	}
	
	status = nfs4_do_create(dir, dentry, data);

	nfs4_free_createdata(data);
out:
	return status;
}

static int nfs4_proc_mknod(struct inode *dir, struct dentry *dentry,
		struct iattr *sattr, dev_t rdev)
{
	struct nfs4_exception exception = { };
	int err;

	sattr->ia_mode &= ~current_umask();
	do {
		err = nfs4_handle_exception(NFS_SERVER(dir),
				_nfs4_proc_mknod(dir, dentry, sattr, rdev),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_statfs(struct nfs_server *server, struct nfs_fh *fhandle,
		 struct nfs_fsstat *fsstat)
{
	struct nfs4_statfs_arg args = {
		.fh = fhandle,
		.bitmask = server->attr_bitmask,
	};
	struct nfs4_statfs_res res = {
		.fsstat = fsstat,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_STATFS],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};

	nfs_fattr_init(fsstat->fattr);
	return  nfs4_call_sync(server->client, server, &msg, &args.seq_args, &res.seq_res, 0);
}

static int nfs4_proc_statfs(struct nfs_server *server, struct nfs_fh *fhandle, struct nfs_fsstat *fsstat)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(server,
				_nfs4_proc_statfs(server, fhandle, fsstat),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_do_fsinfo(struct nfs_server *server, struct nfs_fh *fhandle,
		struct nfs_fsinfo *fsinfo)
{
	struct nfs4_fsinfo_arg args = {
		.fh = fhandle,
		.bitmask = server->attr_bitmask,
	};
	struct nfs4_fsinfo_res res = {
		.fsinfo = fsinfo,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_FSINFO],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};

	return nfs4_call_sync(server->client, server, &msg, &args.seq_args, &res.seq_res, 0);
}

static int nfs4_do_fsinfo(struct nfs_server *server, struct nfs_fh *fhandle, struct nfs_fsinfo *fsinfo)
{
	struct nfs4_exception exception = { };
	int err;

	do {
		err = nfs4_handle_exception(server,
				_nfs4_do_fsinfo(server, fhandle, fsinfo),
				&exception);
	} while (exception.retry);
	return err;
}

static int nfs4_proc_fsinfo(struct nfs_server *server, struct nfs_fh *fhandle, struct nfs_fsinfo *fsinfo)
{
	nfs_fattr_init(fsinfo->fattr);
	return nfs4_do_fsinfo(server, fhandle, fsinfo);
}

static int _nfs4_proc_pathconf(struct nfs_server *server, struct nfs_fh *fhandle,
		struct nfs_pathconf *pathconf)
{
	struct nfs4_pathconf_arg args = {
		.fh = fhandle,
		.bitmask = server->attr_bitmask,
	};
	struct nfs4_pathconf_res res = {
		.pathconf = pathconf,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_PATHCONF],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};

	/* None of the pathconf attributes are mandatory to implement */
	if ((args.bitmask[0] & nfs4_pathconf_bitmap[0]) == 0) {
		memset(pathconf, 0, sizeof(*pathconf));
		return 0;
	}

	nfs_fattr_init(pathconf->fattr);
	return nfs4_call_sync(server->client, server, &msg, &args.seq_args, &res.seq_res, 0);
}

static int nfs4_proc_pathconf(struct nfs_server *server, struct nfs_fh *fhandle,
		struct nfs_pathconf *pathconf)
{
	struct nfs4_exception exception = { };
	int err;

	do {
		err = nfs4_handle_exception(server,
				_nfs4_proc_pathconf(server, fhandle, pathconf),
				&exception);
	} while (exception.retry);
	return err;
}

void __nfs4_read_done_cb(struct nfs_read_data *data)
{
	nfs_invalidate_atime(data->inode);
}

static int nfs4_read_done_cb(struct rpc_task *task, struct nfs_read_data *data)
{
	struct nfs_server *server = NFS_SERVER(data->inode);

	if (nfs4_async_handle_error(task, server, data->args.context->state) == -EAGAIN) {
		nfs_restart_rpc(task, server->nfs_client);
		return -EAGAIN;
	}

	__nfs4_read_done_cb(data);
	if (task->tk_status > 0)
		renew_lease(server, data->timestamp);
	return 0;
}

static int nfs4_read_done(struct rpc_task *task, struct nfs_read_data *data)
{

	dprintk("--> %s\n", __func__);

	if (!nfs4_sequence_done(task, &data->res.seq_res))
		return -EAGAIN;

	return data->read_done_cb ? data->read_done_cb(task, data) :
				    nfs4_read_done_cb(task, data);
}

static void nfs4_proc_read_setup(struct nfs_read_data *data, struct rpc_message *msg)
{
	data->timestamp   = jiffies;
	data->read_done_cb = nfs4_read_done_cb;
	msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_READ];
}

/* Reset the the nfs_read_data to send the read to the MDS. */
void nfs4_reset_read(struct rpc_task *task, struct nfs_read_data *data)
{
	dprintk("%s Reset task for i/o through\n", __func__);
	put_lseg(data->lseg);
	data->lseg = NULL;
	/* offsets will differ in the dense stripe case */
	data->args.offset = data->mds_offset;
	data->ds_clp = NULL;
	data->args.fh     = NFS_FH(data->inode);
	data->read_done_cb = nfs4_read_done_cb;
	task->tk_ops = data->mds_ops;
	rpc_task_reset_client(task, NFS_CLIENT(data->inode));
}
EXPORT_SYMBOL_GPL(nfs4_reset_read);

static int nfs4_write_done_cb(struct rpc_task *task, struct nfs_write_data *data)
{
	struct inode *inode = data->inode;
	
	if (nfs4_async_handle_error(task, NFS_SERVER(inode), data->args.context->state) == -EAGAIN) {
		nfs_restart_rpc(task, NFS_SERVER(inode)->nfs_client);
		return -EAGAIN;
	}
	if (task->tk_status >= 0) {
		renew_lease(NFS_SERVER(inode), data->timestamp);
		nfs_post_op_update_inode_force_wcc(inode, data->res.fattr);
	}
	return 0;
}

static int nfs4_write_done(struct rpc_task *task, struct nfs_write_data *data)
{
	if (!nfs4_sequence_done(task, &data->res.seq_res))
		return -EAGAIN;
	return data->write_done_cb ? data->write_done_cb(task, data) :
		nfs4_write_done_cb(task, data);
}

/* Reset the the nfs_write_data to send the write to the MDS. */
void nfs4_reset_write(struct rpc_task *task, struct nfs_write_data *data)
{
	dprintk("%s Reset task for i/o through\n", __func__);
	put_lseg(data->lseg);
	data->lseg          = NULL;
	data->ds_clp        = NULL;
	data->write_done_cb = nfs4_write_done_cb;
	data->args.fh       = NFS_FH(data->inode);
	data->args.bitmask  = data->res.server->cache_consistency_bitmask;
	data->args.offset   = data->mds_offset;
	data->res.fattr     = &data->fattr;
	task->tk_ops        = data->mds_ops;
	rpc_task_reset_client(task, NFS_CLIENT(data->inode));
}
EXPORT_SYMBOL_GPL(nfs4_reset_write);

static void nfs4_proc_write_setup(struct nfs_write_data *data, struct rpc_message *msg)
{
	struct nfs_server *server = NFS_SERVER(data->inode);

	if (data->lseg) {
		data->args.bitmask = NULL;
		data->res.fattr = NULL;
	} else
		data->args.bitmask = server->cache_consistency_bitmask;
	if (!data->write_done_cb)
		data->write_done_cb = nfs4_write_done_cb;
	data->res.server = server;
	data->timestamp   = jiffies;

	msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_WRITE];
}

static int nfs4_commit_done_cb(struct rpc_task *task, struct nfs_write_data *data)
{
	struct inode *inode = data->inode;

	if (nfs4_async_handle_error(task, NFS_SERVER(inode), NULL) == -EAGAIN) {
		nfs_restart_rpc(task, NFS_SERVER(inode)->nfs_client);
		return -EAGAIN;
	}
	nfs_refresh_inode(inode, data->res.fattr);
	return 0;
}

static int nfs4_commit_done(struct rpc_task *task, struct nfs_write_data *data)
{
	if (!nfs4_sequence_done(task, &data->res.seq_res))
		return -EAGAIN;
	return data->write_done_cb(task, data);
}

static void nfs4_proc_commit_setup(struct nfs_write_data *data, struct rpc_message *msg)
{
	struct nfs_server *server = NFS_SERVER(data->inode);

	if (data->lseg) {
		data->args.bitmask = NULL;
		data->res.fattr = NULL;
	} else
		data->args.bitmask = server->cache_consistency_bitmask;
	if (!data->write_done_cb)
		data->write_done_cb = nfs4_commit_done_cb;
	data->res.server = server;
	msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_COMMIT];
}

struct nfs4_renewdata {
	struct nfs_client	*client;
	unsigned long		timestamp;
};

/*
 * nfs4_proc_async_renew(): This is not one of the nfs_rpc_ops; it is a special
 * standalone procedure for queueing an asynchronous RENEW.
 */
static void nfs4_renew_release(void *calldata)
{
	struct nfs4_renewdata *data = calldata;
	struct nfs_client *clp = data->client;

	if (atomic_read(&clp->cl_count) > 1)
		nfs4_schedule_state_renewal(clp);
	nfs_put_client(clp);
	kfree(data);
}

static void nfs4_renew_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_renewdata *data = calldata;
	struct nfs_client *clp = data->client;
	unsigned long timestamp = data->timestamp;

	if (task->tk_status < 0) {
		/* Unless we're shutting down, schedule state recovery! */
		if (test_bit(NFS_CS_RENEWD, &clp->cl_res_state) != 0)
			nfs4_schedule_lease_recovery(clp);
		return;
	}
	do_renew_lease(clp, timestamp);
}

static const struct rpc_call_ops nfs4_renew_ops = {
	.rpc_call_done = nfs4_renew_done,
	.rpc_release = nfs4_renew_release,
};

int nfs4_proc_async_renew(struct nfs_client *clp, struct rpc_cred *cred)
{
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_RENEW],
		.rpc_argp	= clp,
		.rpc_cred	= cred,
	};
	struct nfs4_renewdata *data;

	if (!atomic_inc_not_zero(&clp->cl_count))
		return -EIO;
	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;
	data->client = clp;
	data->timestamp = jiffies;
	return rpc_call_async(clp->cl_rpcclient, &msg, RPC_TASK_SOFT,
			&nfs4_renew_ops, data);
}

int nfs4_proc_renew(struct nfs_client *clp, struct rpc_cred *cred)
{
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_RENEW],
		.rpc_argp	= clp,
		.rpc_cred	= cred,
	};
	unsigned long now = jiffies;
	int status;

	status = rpc_call_sync(clp->cl_rpcclient, &msg, 0);
	if (status < 0)
		return status;
	do_renew_lease(clp, now);
	return 0;
}

static inline int nfs4_server_supports_acls(struct nfs_server *server)
{
	return (server->caps & NFS_CAP_ACLS)
		&& (server->acl_bitmask & ACL4_SUPPORT_ALLOW_ACL)
		&& (server->acl_bitmask & ACL4_SUPPORT_DENY_ACL);
}

/* Assuming that XATTR_SIZE_MAX is a multiple of PAGE_CACHE_SIZE, and that
 * it's OK to put sizeof(void) * (XATTR_SIZE_MAX/PAGE_CACHE_SIZE) bytes on
 * the stack.
 */
#define NFS4ACL_MAXPAGES (XATTR_SIZE_MAX >> PAGE_CACHE_SHIFT)

static int buf_to_pages_noslab(const void *buf, size_t buflen,
		struct page **pages, unsigned int *pgbase)
{
	struct page *newpage, **spages;
	int rc = 0;
	size_t len;
	spages = pages;

	do {
		len = min_t(size_t, PAGE_CACHE_SIZE, buflen);
		newpage = alloc_page(GFP_KERNEL);

		if (newpage == NULL)
			goto unwind;
		memcpy(page_address(newpage), buf, len);
                buf += len;
                buflen -= len;
		*pages++ = newpage;
		rc++;
	} while (buflen != 0);

	return rc;

unwind:
	for(; rc > 0; rc--)
		__free_page(spages[rc-1]);
	return -ENOMEM;
}

struct nfs4_cached_acl {
	int cached;
	size_t len;
	char data[0];
};

static void nfs4_set_cached_acl(struct inode *inode, struct nfs4_cached_acl *acl)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	spin_lock(&inode->i_lock);
	kfree(nfsi->nfs4_acl);
	nfsi->nfs4_acl = acl;
	spin_unlock(&inode->i_lock);
}

static void nfs4_zap_acl_attr(struct inode *inode)
{
	nfs4_set_cached_acl(inode, NULL);
}

static inline ssize_t nfs4_read_cached_acl(struct inode *inode, char *buf, size_t buflen)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs4_cached_acl *acl;
	int ret = -ENOENT;

	spin_lock(&inode->i_lock);
	acl = nfsi->nfs4_acl;
	if (acl == NULL)
		goto out;
	if (buf == NULL) /* user is just asking for length */
		goto out_len;
	if (acl->cached == 0)
		goto out;
	ret = -ERANGE; /* see getxattr(2) man page */
	if (acl->len > buflen)
		goto out;
	memcpy(buf, acl->data, acl->len);
out_len:
	ret = acl->len;
out:
	spin_unlock(&inode->i_lock);
	return ret;
}

static void nfs4_write_cached_acl(struct inode *inode, const char *buf, size_t acl_len)
{
	struct nfs4_cached_acl *acl;

	if (buf && acl_len <= PAGE_SIZE) {
		acl = kmalloc(sizeof(*acl) + acl_len, GFP_KERNEL);
		if (acl == NULL)
			goto out;
		acl->cached = 1;
		memcpy(acl->data, buf, acl_len);
	} else {
		acl = kmalloc(sizeof(*acl), GFP_KERNEL);
		if (acl == NULL)
			goto out;
		acl->cached = 0;
	}
	acl->len = acl_len;
out:
	nfs4_set_cached_acl(inode, acl);
}

/*
 * The getxattr API returns the required buffer length when called with a
 * NULL buf. The NFSv4 acl tool then calls getxattr again after allocating
 * the required buf.  On a NULL buf, we send a page of data to the server
 * guessing that the ACL request can be serviced by a page. If so, we cache
 * up to the page of ACL data, and the 2nd call to getxattr is serviced by
 * the cache. If not so, we throw away the page, and cache the required
 * length. The next getxattr call will then produce another round trip to
 * the server, this time with the input buf of the required size.
 */
static ssize_t __nfs4_get_acl_uncached(struct inode *inode, void *buf, size_t buflen)
{
	struct page *pages[NFS4ACL_MAXPAGES] = {NULL, };
	struct nfs_getaclargs args = {
		.fh = NFS_FH(inode),
		.acl_pages = pages,
		.acl_len = buflen,
	};
	struct nfs_getaclres res = {
		.acl_len = buflen,
	};
	void *resp_buf;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_GETACL],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	int ret = -ENOMEM, npages, i;
	size_t acl_len = 0;

	npages = (buflen + PAGE_SIZE - 1) >> PAGE_SHIFT;
	/* As long as we're doing a round trip to the server anyway,
	 * let's be prepared for a page of acl data. */
	if (npages == 0)
		npages = 1;

	for (i = 0; i < npages; i++) {
		pages[i] = alloc_page(GFP_KERNEL);
		if (!pages[i])
			goto out_free;
	}
	if (npages > 1) {
		/* for decoding across pages */
		res.acl_scratch = alloc_page(GFP_KERNEL);
		if (!res.acl_scratch)
			goto out_free;
	}
	args.acl_len = npages * PAGE_SIZE;
	args.acl_pgbase = 0;
	/* Let decode_getfacl know not to fail if the ACL data is larger than
	 * the page we send as a guess */
	if (buf == NULL)
		res.acl_flags |= NFS4_ACL_LEN_REQUEST;
	resp_buf = page_address(pages[0]);

	dprintk("%s  buf %p buflen %ld npages %d args.acl_len %ld\n",
		__func__, buf, buflen, npages, args.acl_len);
	ret = nfs4_call_sync(NFS_SERVER(inode)->client, NFS_SERVER(inode),
			     &msg, &args.seq_args, &res.seq_res, 0);
	if (ret)
		goto out_free;

	acl_len = res.acl_len - res.acl_data_offset;
	if (acl_len > args.acl_len)
		nfs4_write_cached_acl(inode, NULL, acl_len);
	else
		nfs4_write_cached_acl(inode, resp_buf + res.acl_data_offset,
				      acl_len);
	if (buf) {
		ret = -ERANGE;
		if (acl_len > buflen)
			goto out_free;
		_copy_from_pages(buf, pages, res.acl_data_offset,
				res.acl_len);
	}
	ret = acl_len;
out_free:
	for (i = 0; i < npages; i++)
		if (pages[i])
			__free_page(pages[i]);
	if (res.acl_scratch)
		__free_page(res.acl_scratch);
	return ret;
}

static ssize_t nfs4_get_acl_uncached(struct inode *inode, void *buf, size_t buflen)
{
	struct nfs4_exception exception = { };
	ssize_t ret;
	do {
		ret = __nfs4_get_acl_uncached(inode, buf, buflen);
		if (ret >= 0)
			break;
		ret = nfs4_handle_exception(NFS_SERVER(inode), ret, &exception);
	} while (exception.retry);
	return ret;
}

static ssize_t nfs4_proc_get_acl(struct inode *inode, void *buf, size_t buflen)
{
	struct nfs_server *server = NFS_SERVER(inode);
	int ret;

	if (!nfs4_server_supports_acls(server))
		return -EOPNOTSUPP;
	ret = nfs_revalidate_inode(server, inode);
	if (ret < 0)
		return ret;
	if (NFS_I(inode)->cache_validity & NFS_INO_INVALID_ACL)
		nfs_zap_acl_cache(inode);
	ret = nfs4_read_cached_acl(inode, buf, buflen);
	if (ret != -ENOENT)
		/* -ENOENT is returned if there is no ACL or if there is an ACL
		 * but no cached acl data, just the acl length */
		return ret;
	return nfs4_get_acl_uncached(inode, buf, buflen);
}

static int __nfs4_proc_set_acl(struct inode *inode, const void *buf, size_t buflen)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct page *pages[NFS4ACL_MAXPAGES];
	struct nfs_setaclargs arg = {
		.fh		= NFS_FH(inode),
		.acl_pages	= pages,
		.acl_len	= buflen,
	};
	struct nfs_setaclres res;
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_SETACL],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
	};
	int ret, i;

	if (!nfs4_server_supports_acls(server))
		return -EOPNOTSUPP;
	i = buf_to_pages_noslab(buf, buflen, arg.acl_pages, &arg.acl_pgbase);
	if (i < 0)
		return i;
	nfs_inode_return_delegation(inode);
	ret = nfs4_call_sync(server->client, server, &msg, &arg.seq_args, &res.seq_res, 1);

	/*
	 * Free each page after tx, so the only ref left is
	 * held by the network stack
	 */
	for (; i > 0; i--)
		put_page(pages[i-1]);

	/*
	 * Acl update can result in inode attribute update.
	 * so mark the attribute cache invalid.
	 */
	spin_lock(&inode->i_lock);
	NFS_I(inode)->cache_validity |= NFS_INO_INVALID_ATTR;
	spin_unlock(&inode->i_lock);
	nfs_access_zap_cache(inode);
	nfs_zap_acl_cache(inode);
	return ret;
}

static int nfs4_proc_set_acl(struct inode *inode, const void *buf, size_t buflen)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(inode),
				__nfs4_proc_set_acl(inode, buf, buflen),
				&exception);
	} while (exception.retry);
	return err;
}

static int
nfs4_async_handle_error(struct rpc_task *task, const struct nfs_server *server, struct nfs4_state *state)
{
	struct nfs_client *clp = server->nfs_client;

	if (task->tk_status >= 0)
		return 0;
	switch(task->tk_status) {
		case -NFS4ERR_DELEG_REVOKED:
		case -NFS4ERR_ADMIN_REVOKED:
		case -NFS4ERR_BAD_STATEID:
			if (state != NULL)
				nfs_remove_bad_delegation(state->inode);
		case -NFS4ERR_OPENMODE:
			if (state == NULL)
				break;
			nfs4_schedule_stateid_recovery(server, state);
			goto wait_on_recovery;
		case -NFS4ERR_EXPIRED:
			if (state != NULL)
				nfs4_schedule_stateid_recovery(server, state);
		case -NFS4ERR_STALE_STATEID:
		case -NFS4ERR_STALE_CLIENTID:
			nfs4_schedule_lease_recovery(clp);
			goto wait_on_recovery;
#if defined(CONFIG_NFS_V4_1)
		case -NFS4ERR_BADSESSION:
		case -NFS4ERR_BADSLOT:
		case -NFS4ERR_BAD_HIGH_SLOT:
		case -NFS4ERR_DEADSESSION:
		case -NFS4ERR_CONN_NOT_BOUND_TO_SESSION:
		case -NFS4ERR_SEQ_FALSE_RETRY:
		case -NFS4ERR_SEQ_MISORDERED:
			dprintk("%s ERROR %d, Reset session\n", __func__,
				task->tk_status);
			nfs4_schedule_session_recovery(clp->cl_session);
			task->tk_status = 0;
			return -EAGAIN;
#endif /* CONFIG_NFS_V4_1 */
		case -NFS4ERR_DELAY:
			nfs_inc_server_stats(server, NFSIOS_DELAY);
		case -NFS4ERR_GRACE:
		case -EKEYEXPIRED:
			rpc_delay(task, NFS4_POLL_RETRY_MAX);
			task->tk_status = 0;
			return -EAGAIN;
		case -NFS4ERR_RETRY_UNCACHED_REP:
		case -NFS4ERR_OLD_STATEID:
			task->tk_status = 0;
			return -EAGAIN;
	}
	task->tk_status = nfs4_map_errors(task->tk_status);
	return 0;
wait_on_recovery:
	rpc_sleep_on(&clp->cl_rpcwaitq, task, NULL);
	if (test_bit(NFS4CLNT_MANAGER_RUNNING, &clp->cl_state) == 0)
		rpc_wake_up_queued_task(&clp->cl_rpcwaitq, task);
	task->tk_status = 0;
	return -EAGAIN;
}

int nfs4_proc_setclientid(struct nfs_client *clp, u32 program,
		unsigned short port, struct rpc_cred *cred,
		struct nfs4_setclientid_res *res)
{
	nfs4_verifier sc_verifier;
	struct nfs4_setclientid setclientid = {
		.sc_verifier = &sc_verifier,
		.sc_prog = program,
		.sc_cb_ident = clp->cl_cb_ident,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SETCLIENTID],
		.rpc_argp = &setclientid,
		.rpc_resp = res,
		.rpc_cred = cred,
	};
	__be32 *p;
	int loop = 0;
	int status;

	p = (__be32*)sc_verifier.data;
	*p++ = htonl((u32)clp->cl_boot_time.tv_sec);
	*p = htonl((u32)clp->cl_boot_time.tv_nsec);

	for(;;) {
		setclientid.sc_name_len = scnprintf(setclientid.sc_name,
				sizeof(setclientid.sc_name), "%s/%s %s %s %u",
				clp->cl_ipaddr,
				rpc_peeraddr2str(clp->cl_rpcclient,
							RPC_DISPLAY_ADDR),
				rpc_peeraddr2str(clp->cl_rpcclient,
							RPC_DISPLAY_PROTO),
				clp->cl_rpcclient->cl_auth->au_ops->au_name,
				clp->cl_id_uniquifier);
		setclientid.sc_netid_len = scnprintf(setclientid.sc_netid,
				sizeof(setclientid.sc_netid),
				rpc_peeraddr2str(clp->cl_rpcclient,
							RPC_DISPLAY_NETID));
		setclientid.sc_uaddr_len = scnprintf(setclientid.sc_uaddr,
				sizeof(setclientid.sc_uaddr), "%s.%u.%u",
				clp->cl_ipaddr, port >> 8, port & 255);

		status = rpc_call_sync(clp->cl_rpcclient, &msg, RPC_TASK_TIMEOUT);
		if (status != -NFS4ERR_CLID_INUSE)
			break;
		if (loop != 0) {
			++clp->cl_id_uniquifier;
			break;
		}
		++loop;
		ssleep(clp->cl_lease_time / HZ + 1);
	}
	return status;
}

int nfs4_proc_setclientid_confirm(struct nfs_client *clp,
		struct nfs4_setclientid_res *arg,
		struct rpc_cred *cred)
{
	struct nfs_fsinfo fsinfo;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SETCLIENTID_CONFIRM],
		.rpc_argp = arg,
		.rpc_resp = &fsinfo,
		.rpc_cred = cred,
	};
	unsigned long now;
	int status;

	now = jiffies;
	status = rpc_call_sync(clp->cl_rpcclient, &msg, RPC_TASK_TIMEOUT);
	if (status == 0) {
		spin_lock(&clp->cl_lock);
		clp->cl_lease_time = fsinfo.lease_time * HZ;
		clp->cl_last_renewal = now;
		spin_unlock(&clp->cl_lock);
	}
	return status;
}

struct nfs4_delegreturndata {
	struct nfs4_delegreturnargs args;
	struct nfs4_delegreturnres res;
	struct nfs_fh fh;
	nfs4_stateid stateid;
	unsigned long timestamp;
	struct nfs_fattr fattr;
	int rpc_status;
};

static void nfs4_delegreturn_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_delegreturndata *data = calldata;

	if (!nfs4_sequence_done(task, &data->res.seq_res))
		return;

	switch (task->tk_status) {
	case -NFS4ERR_STALE_STATEID:
	case -NFS4ERR_EXPIRED:
	case 0:
		renew_lease(data->res.server, data->timestamp);
		break;
	default:
		if (nfs4_async_handle_error(task, data->res.server, NULL) ==
				-EAGAIN) {
			nfs_restart_rpc(task, data->res.server->nfs_client);
			return;
		}
	}
	data->rpc_status = task->tk_status;
}

static void nfs4_delegreturn_release(void *calldata)
{
	kfree(calldata);
}

#if defined(CONFIG_NFS_V4_1)
static void nfs4_delegreturn_prepare(struct rpc_task *task, void *data)
{
	struct nfs4_delegreturndata *d_data;

	d_data = (struct nfs4_delegreturndata *)data;

	if (nfs4_setup_sequence(d_data->res.server,
				&d_data->args.seq_args,
				&d_data->res.seq_res, 1, task))
		return;
	rpc_call_start(task);
}
#endif /* CONFIG_NFS_V4_1 */

static const struct rpc_call_ops nfs4_delegreturn_ops = {
#if defined(CONFIG_NFS_V4_1)
	.rpc_call_prepare = nfs4_delegreturn_prepare,
#endif /* CONFIG_NFS_V4_1 */
	.rpc_call_done = nfs4_delegreturn_done,
	.rpc_release = nfs4_delegreturn_release,
};

static int _nfs4_proc_delegreturn(struct inode *inode, struct rpc_cred *cred, const nfs4_stateid *stateid, int issync)
{
	struct nfs4_delegreturndata *data;
	struct nfs_server *server = NFS_SERVER(inode);
	struct rpc_task *task;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_DELEGRETURN],
		.rpc_cred = cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = server->client,
		.rpc_message = &msg,
		.callback_ops = &nfs4_delegreturn_ops,
		.flags = RPC_TASK_ASYNC,
	};
	int status = 0;

	data = kzalloc(sizeof(*data), GFP_NOFS);
	if (data == NULL)
		return -ENOMEM;
	data->args.fhandle = &data->fh;
	data->args.stateid = &data->stateid;
	data->args.bitmask = server->attr_bitmask;
	nfs_copy_fh(&data->fh, NFS_FH(inode));
	memcpy(&data->stateid, stateid, sizeof(data->stateid));
	data->res.fattr = &data->fattr;
	data->res.server = server;
	nfs_fattr_init(data->res.fattr);
	data->timestamp = jiffies;
	data->rpc_status = 0;

	task_setup_data.callback_data = data;
	msg.rpc_argp = &data->args;
	msg.rpc_resp = &data->res;
	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	if (!issync)
		goto out;
	status = nfs4_wait_for_completion_rpc_task(task);
	if (status != 0)
		goto out;
	status = data->rpc_status;
	if (status != 0)
		goto out;
	nfs_refresh_inode(inode, &data->fattr);
out:
	rpc_put_task(task);
	return status;
}

int nfs4_proc_delegreturn(struct inode *inode, struct rpc_cred *cred, const nfs4_stateid *stateid, int issync)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs4_exception exception = { };
	int err;
	do {
		err = _nfs4_proc_delegreturn(inode, cred, stateid, issync);
		switch (err) {
			case -NFS4ERR_STALE_STATEID:
			case -NFS4ERR_EXPIRED:
			case 0:
				return 0;
		}
		err = nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);
	return err;
}

#define NFS4_LOCK_MINTIMEOUT (1 * HZ)
#define NFS4_LOCK_MAXTIMEOUT (30 * HZ)

/* 
 * sleep, with exponential backoff, and retry the LOCK operation. 
 */
static unsigned long
nfs4_set_lock_task_retry(unsigned long timeout)
{
	schedule_timeout_killable(timeout);
	timeout <<= 1;
	if (timeout > NFS4_LOCK_MAXTIMEOUT)
		return NFS4_LOCK_MAXTIMEOUT;
	return timeout;
}

static int _nfs4_proc_getlk(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct inode *inode = state->inode;
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_client *clp = server->nfs_client;
	struct nfs_lockt_args arg = {
		.fh = NFS_FH(inode),
		.fl = request,
	};
	struct nfs_lockt_res res = {
		.denied = request,
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_LOCKT],
		.rpc_argp       = &arg,
		.rpc_resp       = &res,
		.rpc_cred	= state->owner->so_cred,
	};
	struct nfs4_lock_state *lsp;
	int status;

	arg.lock_owner.clientid = clp->cl_clientid;
	status = nfs4_set_lock_state(state, request);
	if (status != 0)
		goto out;
	lsp = request->fl_u.nfs4_fl.owner;
	arg.lock_owner.id = lsp->ls_id.id;
	arg.lock_owner.s_dev = server->s_dev;
	status = nfs4_call_sync(server->client, server, &msg, &arg.seq_args, &res.seq_res, 1);
	switch (status) {
		case 0:
			request->fl_type = F_UNLCK;
			break;
		case -NFS4ERR_DENIED:
			status = 0;
	}
	request->fl_ops->fl_release_private(request);
out:
	return status;
}

static int nfs4_proc_getlk(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct nfs4_exception exception = { };
	int err;

	do {
		err = nfs4_handle_exception(NFS_SERVER(state->inode),
				_nfs4_proc_getlk(state, cmd, request),
				&exception);
	} while (exception.retry);
	return err;
}

static int do_vfs_lock(struct file *file, struct file_lock *fl)
{
	int res = 0;
	switch (fl->fl_flags & (FL_POSIX|FL_FLOCK)) {
		case FL_POSIX:
			res = posix_lock_file_wait(file, fl);
			break;
		case FL_FLOCK:
			res = flock_lock_file_wait(file, fl);
			break;
		default:
			BUG();
	}
	return res;
}

struct nfs4_unlockdata {
	struct nfs_locku_args arg;
	struct nfs_locku_res res;
	struct nfs4_lock_state *lsp;
	struct nfs_open_context *ctx;
	struct file_lock fl;
	const struct nfs_server *server;
	unsigned long timestamp;
};

static struct nfs4_unlockdata *nfs4_alloc_unlockdata(struct file_lock *fl,
		struct nfs_open_context *ctx,
		struct nfs4_lock_state *lsp,
		struct nfs_seqid *seqid)
{
	struct nfs4_unlockdata *p;
	struct inode *inode = lsp->ls_state->inode;

	p = kzalloc(sizeof(*p), GFP_NOFS);
	if (p == NULL)
		return NULL;
	p->arg.fh = NFS_FH(inode);
	p->arg.fl = &p->fl;
	p->arg.seqid = seqid;
	p->res.seqid = seqid;
	p->arg.stateid = &lsp->ls_stateid;
	p->lsp = lsp;
	atomic_inc(&lsp->ls_count);
	/* Ensure we don't close file until we're done freeing locks! */
	p->ctx = get_nfs_open_context(ctx);
	memcpy(&p->fl, fl, sizeof(p->fl));
	p->server = NFS_SERVER(inode);
	return p;
}

static void nfs4_locku_release_calldata(void *data)
{
	struct nfs4_unlockdata *calldata = data;
	nfs_free_seqid(calldata->arg.seqid);
	nfs4_put_lock_state(calldata->lsp);
	put_nfs_open_context(calldata->ctx);
	kfree(calldata);
}

static void nfs4_locku_done(struct rpc_task *task, void *data)
{
	struct nfs4_unlockdata *calldata = data;

	if (!nfs4_sequence_done(task, &calldata->res.seq_res))
		return;
	switch (task->tk_status) {
		case 0:
			memcpy(calldata->lsp->ls_stateid.data,
					calldata->res.stateid.data,
					sizeof(calldata->lsp->ls_stateid.data));
			renew_lease(calldata->server, calldata->timestamp);
			break;
		case -NFS4ERR_BAD_STATEID:
		case -NFS4ERR_OLD_STATEID:
		case -NFS4ERR_STALE_STATEID:
		case -NFS4ERR_EXPIRED:
			break;
		default:
			if (nfs4_async_handle_error(task, calldata->server, NULL) == -EAGAIN)
				nfs_restart_rpc(task,
						 calldata->server->nfs_client);
	}
	nfs_release_seqid(calldata->arg.seqid);
}

static void nfs4_locku_prepare(struct rpc_task *task, void *data)
{
	struct nfs4_unlockdata *calldata = data;

	if (nfs_wait_on_sequence(calldata->arg.seqid, task) != 0)
		return;
	if ((calldata->lsp->ls_flags & NFS_LOCK_INITIALIZED) == 0) {
		/* Note: exit _without_ running nfs4_locku_done */
		task->tk_action = NULL;
		return;
	}
	calldata->timestamp = jiffies;
	if (nfs4_setup_sequence(calldata->server,
				&calldata->arg.seq_args,
				&calldata->res.seq_res, 1, task))
		return;
	rpc_call_start(task);
}

static const struct rpc_call_ops nfs4_locku_ops = {
	.rpc_call_prepare = nfs4_locku_prepare,
	.rpc_call_done = nfs4_locku_done,
	.rpc_release = nfs4_locku_release_calldata,
};

static struct rpc_task *nfs4_do_unlck(struct file_lock *fl,
		struct nfs_open_context *ctx,
		struct nfs4_lock_state *lsp,
		struct nfs_seqid *seqid)
{
	struct nfs4_unlockdata *data;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LOCKU],
		.rpc_cred = ctx->cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = NFS_CLIENT(lsp->ls_state->inode),
		.rpc_message = &msg,
		.callback_ops = &nfs4_locku_ops,
		.workqueue = nfsiod_workqueue,
		.flags = RPC_TASK_ASYNC,
	};

	/* Ensure this is an unlock - when canceling a lock, the
	 * canceled lock is passed in, and it won't be an unlock.
	 */
	fl->fl_type = F_UNLCK;

	data = nfs4_alloc_unlockdata(fl, ctx, lsp, seqid);
	if (data == NULL) {
		nfs_free_seqid(seqid);
		return ERR_PTR(-ENOMEM);
	}

	msg.rpc_argp = &data->arg;
	msg.rpc_resp = &data->res;
	task_setup_data.callback_data = data;
	return rpc_run_task(&task_setup_data);
}

static int nfs4_proc_unlck(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct nfs_inode *nfsi = NFS_I(state->inode);
	struct nfs_seqid *seqid;
	struct nfs4_lock_state *lsp;
	struct rpc_task *task;
	int status = 0;
	unsigned char fl_flags = request->fl_flags;

	status = nfs4_set_lock_state(state, request);
	/* Unlock _before_ we do the RPC call */
	request->fl_flags |= FL_EXISTS;
	down_read(&nfsi->rwsem);
	if (do_vfs_lock(request->fl_file, request) == -ENOENT) {
		up_read(&nfsi->rwsem);
		goto out;
	}
	up_read(&nfsi->rwsem);
	if (status != 0)
		goto out;
	/* Is this a delegated lock? */
	if (test_bit(NFS_DELEGATED_STATE, &state->flags))
		goto out;
	lsp = request->fl_u.nfs4_fl.owner;
	seqid = nfs_alloc_seqid(&lsp->ls_seqid, GFP_KERNEL);
	status = -ENOMEM;
	if (seqid == NULL)
		goto out;
	task = nfs4_do_unlck(request, nfs_file_open_context(request->fl_file), lsp, seqid);
	status = PTR_ERR(task);
	if (IS_ERR(task))
		goto out;
	status = nfs4_wait_for_completion_rpc_task(task);
	rpc_put_task(task);
out:
	request->fl_flags = fl_flags;
	return status;
}

struct nfs4_lockdata {
	struct nfs_lock_args arg;
	struct nfs_lock_res res;
	struct nfs4_lock_state *lsp;
	struct nfs_open_context *ctx;
	struct file_lock fl;
	unsigned long timestamp;
	int rpc_status;
	int cancelled;
	struct nfs_server *server;
};

static struct nfs4_lockdata *nfs4_alloc_lockdata(struct file_lock *fl,
		struct nfs_open_context *ctx, struct nfs4_lock_state *lsp,
		gfp_t gfp_mask)
{
	struct nfs4_lockdata *p;
	struct inode *inode = lsp->ls_state->inode;
	struct nfs_server *server = NFS_SERVER(inode);

	p = kzalloc(sizeof(*p), gfp_mask);
	if (p == NULL)
		return NULL;

	p->arg.fh = NFS_FH(inode);
	p->arg.fl = &p->fl;
	p->arg.open_seqid = nfs_alloc_seqid(&lsp->ls_state->owner->so_seqid, gfp_mask);
	if (p->arg.open_seqid == NULL)
		goto out_free;
	p->arg.lock_seqid = nfs_alloc_seqid(&lsp->ls_seqid, gfp_mask);
	if (p->arg.lock_seqid == NULL)
		goto out_free_seqid;
	p->arg.lock_stateid = &lsp->ls_stateid;
	p->arg.lock_owner.clientid = server->nfs_client->cl_clientid;
	p->arg.lock_owner.id = lsp->ls_id.id;
	p->arg.lock_owner.s_dev = server->s_dev;
	p->res.lock_seqid = p->arg.lock_seqid;
	p->lsp = lsp;
	p->server = server;
	atomic_inc(&lsp->ls_count);
	p->ctx = get_nfs_open_context(ctx);
	memcpy(&p->fl, fl, sizeof(p->fl));
	return p;
out_free_seqid:
	nfs_free_seqid(p->arg.open_seqid);
out_free:
	kfree(p);
	return NULL;
}

static void nfs4_lock_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs4_lockdata *data = calldata;
	struct nfs4_state *state = data->lsp->ls_state;

	dprintk("%s: begin!\n", __func__);
	if (nfs_wait_on_sequence(data->arg.lock_seqid, task) != 0)
		return;
	/* Do we need to do an open_to_lock_owner? */
	if (!(data->arg.lock_seqid->sequence->flags & NFS_SEQID_CONFIRMED)) {
		if (nfs_wait_on_sequence(data->arg.open_seqid, task) != 0)
			return;
		data->arg.open_stateid = &state->stateid;
		data->arg.new_lock_owner = 1;
		data->res.open_seqid = data->arg.open_seqid;
	} else
		data->arg.new_lock_owner = 0;
	data->timestamp = jiffies;
	if (nfs4_setup_sequence(data->server,
				&data->arg.seq_args,
				&data->res.seq_res, 1, task))
		return;
	rpc_call_start(task);
	dprintk("%s: done!, ret = %d\n", __func__, data->rpc_status);
}

static void nfs4_recover_lock_prepare(struct rpc_task *task, void *calldata)
{
	rpc_task_set_priority(task, RPC_PRIORITY_PRIVILEGED);
	nfs4_lock_prepare(task, calldata);
}

static void nfs4_lock_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_lockdata *data = calldata;

	dprintk("%s: begin!\n", __func__);

	if (!nfs4_sequence_done(task, &data->res.seq_res))
		return;

	data->rpc_status = task->tk_status;
	if (data->arg.new_lock_owner != 0) {
		if (data->rpc_status == 0)
			nfs_confirm_seqid(&data->lsp->ls_seqid, 0);
		else
			goto out;
	}
	if (data->rpc_status == 0) {
		memcpy(data->lsp->ls_stateid.data, data->res.stateid.data,
					sizeof(data->lsp->ls_stateid.data));
		data->lsp->ls_flags |= NFS_LOCK_INITIALIZED;
		renew_lease(NFS_SERVER(data->ctx->path.dentry->d_inode), data->timestamp);
	}
out:
	dprintk("%s: done, ret = %d!\n", __func__, data->rpc_status);
}

static void nfs4_lock_release(void *calldata)
{
	struct nfs4_lockdata *data = calldata;

	dprintk("%s: begin!\n", __func__);
	nfs_free_seqid(data->arg.open_seqid);
	if (data->cancelled != 0) {
		struct rpc_task *task;
		task = nfs4_do_unlck(&data->fl, data->ctx, data->lsp,
				data->arg.lock_seqid);
		if (!IS_ERR(task))
			rpc_put_task_async(task);
		dprintk("%s: cancelling lock!\n", __func__);
	} else
		nfs_free_seqid(data->arg.lock_seqid);
	nfs4_put_lock_state(data->lsp);
	put_nfs_open_context(data->ctx);
	kfree(data);
	dprintk("%s: done!\n", __func__);
}

static const struct rpc_call_ops nfs4_lock_ops = {
	.rpc_call_prepare = nfs4_lock_prepare,
	.rpc_call_done = nfs4_lock_done,
	.rpc_release = nfs4_lock_release,
};

static const struct rpc_call_ops nfs4_recover_lock_ops = {
	.rpc_call_prepare = nfs4_recover_lock_prepare,
	.rpc_call_done = nfs4_lock_done,
	.rpc_release = nfs4_lock_release,
};

static void nfs4_handle_setlk_error(struct nfs_server *server, struct nfs4_lock_state *lsp, int new_lock_owner, int error)
{
	switch (error) {
	case -NFS4ERR_ADMIN_REVOKED:
	case -NFS4ERR_BAD_STATEID:
		lsp->ls_seqid.flags &= ~NFS_SEQID_CONFIRMED;
		if (new_lock_owner != 0 ||
		   (lsp->ls_flags & NFS_LOCK_INITIALIZED) != 0)
			nfs4_schedule_stateid_recovery(server, lsp->ls_state);
		break;
	case -NFS4ERR_STALE_STATEID:
		lsp->ls_seqid.flags &= ~NFS_SEQID_CONFIRMED;
	case -NFS4ERR_EXPIRED:
		nfs4_schedule_lease_recovery(server->nfs_client);
	};
}

static int _nfs4_do_setlk(struct nfs4_state *state, int cmd, struct file_lock *fl, int recovery_type)
{
	struct nfs4_lockdata *data;
	struct rpc_task *task;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LOCK],
		.rpc_cred = state->owner->so_cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = NFS_CLIENT(state->inode),
		.rpc_message = &msg,
		.callback_ops = &nfs4_lock_ops,
		.workqueue = nfsiod_workqueue,
		.flags = RPC_TASK_ASYNC,
	};
	int ret;

	dprintk("%s: begin!\n", __func__);
	data = nfs4_alloc_lockdata(fl, nfs_file_open_context(fl->fl_file),
			fl->fl_u.nfs4_fl.owner,
			recovery_type == NFS_LOCK_NEW ? GFP_KERNEL : GFP_NOFS);
	if (data == NULL)
		return -ENOMEM;
	if (IS_SETLKW(cmd))
		data->arg.block = 1;
	if (recovery_type > NFS_LOCK_NEW) {
		if (recovery_type == NFS_LOCK_RECLAIM)
			data->arg.reclaim = NFS_LOCK_RECLAIM;
		task_setup_data.callback_ops = &nfs4_recover_lock_ops;
	}
	msg.rpc_argp = &data->arg;
	msg.rpc_resp = &data->res;
	task_setup_data.callback_data = data;
	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	ret = nfs4_wait_for_completion_rpc_task(task);
	if (ret == 0) {
		ret = data->rpc_status;
		if (ret)
			nfs4_handle_setlk_error(data->server, data->lsp,
					data->arg.new_lock_owner, ret);
	} else
		data->cancelled = 1;
	rpc_put_task(task);
	dprintk("%s: done, ret = %d!\n", __func__, ret);
	return ret;
}

static int nfs4_lock_reclaim(struct nfs4_state *state, struct file_lock *request)
{
	struct nfs_server *server = NFS_SERVER(state->inode);
	struct nfs4_exception exception = {
		.inode = state->inode,
	};
	int err;

	do {
		/* Cache the lock if possible... */
		if (test_bit(NFS_DELEGATED_STATE, &state->flags) != 0)
			return 0;
		err = _nfs4_do_setlk(state, F_SETLK, request, NFS_LOCK_RECLAIM);
		if (err != -NFS4ERR_DELAY)
			break;
		nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);
	return err;
}

static int nfs4_lock_expired(struct nfs4_state *state, struct file_lock *request)
{
	struct nfs_server *server = NFS_SERVER(state->inode);
	struct nfs4_exception exception = {
		.inode = state->inode,
	};
	int err;

	err = nfs4_set_lock_state(state, request);
	if (err != 0)
		return err;
	do {
		if (test_bit(NFS_DELEGATED_STATE, &state->flags) != 0)
			return 0;
		err = _nfs4_do_setlk(state, F_SETLK, request, NFS_LOCK_EXPIRED);
		switch (err) {
		default:
			goto out;
		case -NFS4ERR_GRACE:
		case -NFS4ERR_DELAY:
			nfs4_handle_exception(server, err, &exception);
			err = 0;
		}
	} while (exception.retry);
out:
	return err;
}

static int _nfs4_proc_setlk(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct nfs_inode *nfsi = NFS_I(state->inode);
	unsigned char fl_flags = request->fl_flags;
	int status = -ENOLCK;

	if ((fl_flags & FL_POSIX) &&
			!test_bit(NFS_STATE_POSIX_LOCKS, &state->flags))
		goto out;
	/* Is this a delegated open? */
	status = nfs4_set_lock_state(state, request);
	if (status != 0)
		goto out;
	request->fl_flags |= FL_ACCESS;
	status = do_vfs_lock(request->fl_file, request);
	if (status < 0)
		goto out;
	down_read(&nfsi->rwsem);
	if (test_bit(NFS_DELEGATED_STATE, &state->flags)) {
		/* Yes: cache locks! */
		/* ...but avoid races with delegation recall... */
		request->fl_flags = fl_flags & ~FL_SLEEP;
		status = do_vfs_lock(request->fl_file, request);
		goto out_unlock;
	}
	status = _nfs4_do_setlk(state, cmd, request, NFS_LOCK_NEW);
	if (status != 0)
		goto out_unlock;
	/* Note: we always want to sleep here! */
	request->fl_flags = fl_flags | FL_SLEEP;
	if (do_vfs_lock(request->fl_file, request) < 0)
		printk(KERN_WARNING "%s: VFS is out of sync with lock manager!\n", __func__);
out_unlock:
	up_read(&nfsi->rwsem);
out:
	request->fl_flags = fl_flags;
	return status;
}

static int nfs4_proc_setlk(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct nfs4_exception exception = {
		.state = state,
		.inode = state->inode,
	};
	int err;

	do {
		err = _nfs4_proc_setlk(state, cmd, request);
		if (err == -NFS4ERR_DENIED)
			err = -EAGAIN;
		err = nfs4_handle_exception(NFS_SERVER(state->inode),
				err, &exception);
	} while (exception.retry);
	return err;
}

static int
nfs4_proc_lock(struct file *filp, int cmd, struct file_lock *request)
{
	struct nfs_open_context *ctx;
	struct nfs4_state *state;
	unsigned long timeout = NFS4_LOCK_MINTIMEOUT;
	int status;

	/* verify open state */
	ctx = nfs_file_open_context(filp);
	state = ctx->state;

	if (request->fl_start < 0 || request->fl_end < 0)
		return -EINVAL;

	if (IS_GETLK(cmd)) {
		if (state != NULL)
			return nfs4_proc_getlk(state, F_GETLK, request);
		return 0;
	}

	if (!(IS_SETLK(cmd) || IS_SETLKW(cmd)))
		return -EINVAL;

	if (request->fl_type == F_UNLCK) {
		if (state != NULL)
			return nfs4_proc_unlck(state, cmd, request);
		return 0;
	}

	if (state == NULL)
		return -ENOLCK;
	/*
	 * Don't rely on the VFS having checked the file open mode,
	 * since it won't do this for flock() locks.
	 */
	switch (request->fl_type & (F_RDLCK|F_WRLCK|F_UNLCK)) {
	case F_RDLCK:
		if (!(filp->f_mode & FMODE_READ))
			return -EBADF;
		break;
	case F_WRLCK:
		if (!(filp->f_mode & FMODE_WRITE))
			return -EBADF;
	}

	do {
		status = nfs4_proc_setlk(state, cmd, request);
		if ((status != -EAGAIN) || IS_SETLK(cmd))
			break;
		timeout = nfs4_set_lock_task_retry(timeout);
		status = -ERESTARTSYS;
		if (signalled())
			break;
	} while(status < 0);
	return status;
}

int nfs4_lock_delegation_recall(struct nfs4_state *state, struct file_lock *fl)
{
	struct nfs_server *server = NFS_SERVER(state->inode);
	struct nfs4_exception exception = { };
	int err;

	err = nfs4_set_lock_state(state, fl);
	if (err != 0)
		goto out;
	do {
		err = _nfs4_do_setlk(state, F_SETLK, fl, NFS_LOCK_NEW);
		switch (err) {
			default:
				printk(KERN_ERR "%s: unhandled error %d.\n",
						__func__, err);
			case 0:
			case -ESTALE:
				goto out;
			case -NFS4ERR_EXPIRED:
				nfs4_schedule_stateid_recovery(server, state);
			case -NFS4ERR_STALE_CLIENTID:
			case -NFS4ERR_STALE_STATEID:
				nfs4_schedule_lease_recovery(server->nfs_client);
				goto out;
			case -NFS4ERR_BADSESSION:
			case -NFS4ERR_BADSLOT:
			case -NFS4ERR_BAD_HIGH_SLOT:
			case -NFS4ERR_CONN_NOT_BOUND_TO_SESSION:
			case -NFS4ERR_DEADSESSION:
				nfs4_schedule_session_recovery(server->nfs_client->cl_session);
				goto out;
			case -ERESTARTSYS:
				/*
				 * The show must go on: exit, but mark the
				 * stateid as needing recovery.
				 */
			case -NFS4ERR_DELEG_REVOKED:
			case -NFS4ERR_ADMIN_REVOKED:
			case -NFS4ERR_BAD_STATEID:
			case -NFS4ERR_OPENMODE:
				nfs4_schedule_stateid_recovery(server, state);
				err = 0;
				goto out;
			case -EKEYEXPIRED:
				/*
				 * User RPCSEC_GSS context has expired.
				 * We cannot recover this stateid now, so
				 * skip it and allow recovery thread to
				 * proceed.
				 */
				err = 0;
				goto out;
			case -ENOMEM:
			case -NFS4ERR_DENIED:
				/* kill_proc(fl->fl_pid, SIGLOST, 1); */
				err = 0;
				goto out;
			case -NFS4ERR_DELAY:
				break;
		}
		err = nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);
out:
	return err;
}

static void nfs4_release_lockowner_release(void *calldata)
{
	kfree(calldata);
}

const struct rpc_call_ops nfs4_release_lockowner_ops = {
	.rpc_release = nfs4_release_lockowner_release,
};

void nfs4_release_lockowner(const struct nfs4_lock_state *lsp)
{
	struct nfs_server *server = lsp->ls_state->owner->so_server;
	struct nfs_release_lockowner_args *args;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_RELEASE_LOCKOWNER],
	};

	if (server->nfs_client->cl_mvops->minor_version != 0)
		return;
	args = kmalloc(sizeof(*args), GFP_NOFS);
	if (!args)
		return;
	args->lock_owner.clientid = server->nfs_client->cl_clientid;
	args->lock_owner.id = lsp->ls_id.id;
	args->lock_owner.s_dev = server->s_dev;
	msg.rpc_argp = args;
	rpc_call_async(server->client, &msg, 0, &nfs4_release_lockowner_ops, args);
}

#define XATTR_NAME_NFSV4_ACL "system.nfs4_acl"

static int nfs4_xattr_set_nfs4_acl(struct dentry *dentry, const char *key,
				   const void *buf, size_t buflen,
				   int flags, int type)
{
	if (strcmp(key, "") != 0)
		return -EINVAL;

	return nfs4_proc_set_acl(dentry->d_inode, buf, buflen);
}

static int nfs4_xattr_get_nfs4_acl(struct dentry *dentry, const char *key,
				   void *buf, size_t buflen, int type)
{
	if (strcmp(key, "") != 0)
		return -EINVAL;

	return nfs4_proc_get_acl(dentry->d_inode, buf, buflen);
}

static size_t nfs4_xattr_list_nfs4_acl(struct dentry *dentry, char *list,
				       size_t list_len, const char *name,
				       size_t name_len, int type)
{
	size_t len = sizeof(XATTR_NAME_NFSV4_ACL);

	if (!nfs4_server_supports_acls(NFS_SERVER(dentry->d_inode)))
		return 0;

	if (list && len <= list_len)
		memcpy(list, XATTR_NAME_NFSV4_ACL, len);
	return len;
}

/*
 * nfs_fhget will use either the mounted_on_fileid or the fileid
 */
static void nfs_fixup_referral_attributes(struct nfs_fattr *fattr)
{
	if (!(((fattr->valid & NFS_ATTR_FATTR_MOUNTED_ON_FILEID) ||
	       (fattr->valid & NFS_ATTR_FATTR_FILEID)) &&
	      (fattr->valid & NFS_ATTR_FATTR_FSID) &&
	      (fattr->valid & NFS_ATTR_FATTR_V4_REFERRAL)))
		return;

	fattr->valid |= NFS_ATTR_FATTR_TYPE | NFS_ATTR_FATTR_MODE |
		NFS_ATTR_FATTR_NLINK;
	fattr->mode = S_IFDIR | S_IRUGO | S_IXUGO;
	fattr->nlink = 2;
}

int nfs4_proc_fs_locations(struct inode *dir, const struct qstr *name,
		struct nfs4_fs_locations *fs_locations, struct page *page)
{
	struct nfs_server *server = NFS_SERVER(dir);
	u32 bitmask[2] = {
		[0] = FATTR4_WORD0_FSID | FATTR4_WORD0_FS_LOCATIONS,
	};
	struct nfs4_fs_locations_arg args = {
		.dir_fh = NFS_FH(dir),
		.name = name,
		.page = page,
		.bitmask = bitmask,
	};
	struct nfs4_fs_locations_res res = {
		.fs_locations = fs_locations,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_FS_LOCATIONS],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	int status;

	dprintk("%s: start\n", __func__);

	/* Ask for the fileid of the absent filesystem if mounted_on_fileid
	 * is not supported */
	if (NFS_SERVER(dir)->attr_bitmask[1] & FATTR4_WORD1_MOUNTED_ON_FILEID)
		bitmask[1] |= FATTR4_WORD1_MOUNTED_ON_FILEID;
	else
		bitmask[0] |= FATTR4_WORD0_FILEID;

	nfs_fattr_init(&fs_locations->fattr);
	fs_locations->server = server;
	fs_locations->nlocations = 0;
	status = nfs4_call_sync(server->client, server, &msg, &args.seq_args, &res.seq_res, 0);
	dprintk("%s: returned status = %d\n", __func__, status);
	return status;
}

static int _nfs4_proc_secinfo(struct inode *dir, const struct qstr *name, struct nfs4_secinfo_flavors *flavors)
{
	int status;
	struct nfs4_secinfo_arg args = {
		.dir_fh = NFS_FH(dir),
		.name   = name,
	};
	struct nfs4_secinfo_res res = {
		.flavors     = flavors,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SECINFO],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};

	dprintk("NFS call  secinfo %s\n", name->name);
	status = nfs4_call_sync(NFS_SERVER(dir)->client, NFS_SERVER(dir), &msg, &args.seq_args, &res.seq_res, 0);
	dprintk("NFS reply  secinfo: %d\n", status);
	return status;
}

int nfs4_proc_secinfo(struct inode *dir, const struct qstr *name, struct nfs4_secinfo_flavors *flavors)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(dir),
				_nfs4_proc_secinfo(dir, name, flavors),
				&exception);
	} while (exception.retry);
	return err;
}

#ifdef CONFIG_NFS_V4_1
/*
 * Check the exchange flags returned by the server for invalid flags, having
 * both PNFS and NON_PNFS flags set, and not having one of NON_PNFS, PNFS, or
 * DS flags set.
 */
static int nfs4_check_cl_exchange_flags(u32 flags)
{
	if (flags & ~EXCHGID4_FLAG_MASK_R)
		goto out_inval;
	if ((flags & EXCHGID4_FLAG_USE_PNFS_MDS) &&
	    (flags & EXCHGID4_FLAG_USE_NON_PNFS))
		goto out_inval;
	if (!(flags & (EXCHGID4_FLAG_MASK_PNFS)))
		goto out_inval;
	return NFS_OK;
out_inval:
	return -NFS4ERR_INVAL;
}

/*
 * nfs4_proc_exchange_id()
 *
 * Since the clientid has expired, all compounds using sessions
 * associated with the stale clientid will be returning
 * NFS4ERR_BADSESSION in the sequence operation, and will therefore
 * be in some phase of session reset.
 */
int nfs4_proc_exchange_id(struct nfs_client *clp, struct rpc_cred *cred)
{
	nfs4_verifier verifier;
	struct nfs41_exchange_id_args args = {
		.client = clp,
		.flags = EXCHGID4_FLAG_SUPP_MOVED_REFER,
	};
	struct nfs41_exchange_id_res res = {
		.client = clp,
	};
	int status;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_EXCHANGE_ID],
		.rpc_argp = &args,
		.rpc_resp = &res,
		.rpc_cred = cred,
	};
	__be32 *p;

	dprintk("--> %s\n", __func__);
	BUG_ON(clp == NULL);

	p = (u32 *)verifier.data;
	*p++ = htonl((u32)clp->cl_boot_time.tv_sec);
	*p = htonl((u32)clp->cl_boot_time.tv_nsec);
	args.verifier = &verifier;

	args.id_len = scnprintf(args.id, sizeof(args.id),
				"%s/%s.%s/%u",
				clp->cl_ipaddr,
				init_utsname()->nodename,
				init_utsname()->domainname,
				clp->cl_rpcclient->cl_auth->au_flavor);

	status = rpc_call_sync(clp->cl_rpcclient, &msg, RPC_TASK_TIMEOUT);
	if (!status)
		status = nfs4_check_cl_exchange_flags(clp->cl_exchange_flags);
	dprintk("<-- %s status= %d\n", __func__, status);
	return status;
}

struct nfs4_get_lease_time_data {
	struct nfs4_get_lease_time_args *args;
	struct nfs4_get_lease_time_res *res;
	struct nfs_client *clp;
};

static void nfs4_get_lease_time_prepare(struct rpc_task *task,
					void *calldata)
{
	int ret;
	struct nfs4_get_lease_time_data *data =
			(struct nfs4_get_lease_time_data *)calldata;

	dprintk("--> %s\n", __func__);
	rpc_task_set_priority(task, RPC_PRIORITY_PRIVILEGED);
	/* just setup sequence, do not trigger session recovery
	   since we're invoked within one */
	ret = nfs41_setup_sequence(data->clp->cl_session,
				   &data->args->la_seq_args,
				   &data->res->lr_seq_res, 0, task);

	BUG_ON(ret == -EAGAIN);
	rpc_call_start(task);
	dprintk("<-- %s\n", __func__);
}

/*
 * Called from nfs4_state_manager thread for session setup, so don't recover
 * from sequence operation or clientid errors.
 */
static void nfs4_get_lease_time_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_get_lease_time_data *data =
			(struct nfs4_get_lease_time_data *)calldata;

	dprintk("--> %s\n", __func__);
	if (!nfs41_sequence_done(task, &data->res->lr_seq_res))
		return;
	switch (task->tk_status) {
	case -NFS4ERR_DELAY:
	case -NFS4ERR_GRACE:
		dprintk("%s Retry: tk_status %d\n", __func__, task->tk_status);
		rpc_delay(task, NFS4_POLL_RETRY_MIN);
		task->tk_status = 0;
		/* fall through */
	case -NFS4ERR_RETRY_UNCACHED_REP:
		nfs_restart_rpc(task, data->clp);
		return;
	}
	dprintk("<-- %s\n", __func__);
}

struct rpc_call_ops nfs4_get_lease_time_ops = {
	.rpc_call_prepare = nfs4_get_lease_time_prepare,
	.rpc_call_done = nfs4_get_lease_time_done,
};

int nfs4_proc_get_lease_time(struct nfs_client *clp, struct nfs_fsinfo *fsinfo)
{
	struct rpc_task *task;
	struct nfs4_get_lease_time_args args;
	struct nfs4_get_lease_time_res res = {
		.lr_fsinfo = fsinfo,
	};
	struct nfs4_get_lease_time_data data = {
		.args = &args,
		.res = &res,
		.clp = clp,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_GET_LEASE_TIME],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	struct rpc_task_setup task_setup = {
		.rpc_client = clp->cl_rpcclient,
		.rpc_message = &msg,
		.callback_ops = &nfs4_get_lease_time_ops,
		.callback_data = &data,
		.flags = RPC_TASK_TIMEOUT,
	};
	int status;

	dprintk("--> %s\n", __func__);
	task = rpc_run_task(&task_setup);

	if (IS_ERR(task))
		status = PTR_ERR(task);
	else {
		status = task->tk_status;
		rpc_put_task(task);
	}
	dprintk("<-- %s return %d\n", __func__, status);

	return status;
}

/*
 * Reset a slot table
 */
static int nfs4_reset_slot_table(struct nfs4_slot_table *tbl, u32 max_reqs,
				 int ivalue)
{
	struct nfs4_slot *new = NULL;
	int i;
	int ret = 0;

	dprintk("--> %s: max_reqs=%u, tbl->max_slots %d\n", __func__,
		max_reqs, tbl->max_slots);

	/* Does the newly negotiated max_reqs match the existing slot table? */
	if (max_reqs != tbl->max_slots) {
		ret = -ENOMEM;
		new = kmalloc(max_reqs * sizeof(struct nfs4_slot),
			      GFP_NOFS);
		if (!new)
			goto out;
		ret = 0;
		kfree(tbl->slots);
	}
	spin_lock(&tbl->slot_tbl_lock);
	if (new) {
		tbl->slots = new;
		tbl->max_slots = max_reqs;
	}
	for (i = 0; i < tbl->max_slots; ++i)
		tbl->slots[i].seq_nr = ivalue;
	spin_unlock(&tbl->slot_tbl_lock);
	dprintk("%s: tbl=%p slots=%p max_slots=%d\n", __func__,
		tbl, tbl->slots, tbl->max_slots);
out:
	dprintk("<-- %s: return %d\n", __func__, ret);
	return ret;
}

/*
 * Reset the forechannel and backchannel slot tables
 */
static int nfs4_reset_slot_tables(struct nfs4_session *session)
{
	int status;

	status = nfs4_reset_slot_table(&session->fc_slot_table,
			session->fc_attrs.max_reqs, 1);
	if (status)
		return status;

	status = nfs4_reset_slot_table(&session->bc_slot_table,
			session->bc_attrs.max_reqs, 0);
	return status;
}

/* Destroy the slot table */
static void nfs4_destroy_slot_tables(struct nfs4_session *session)
{
	if (session->fc_slot_table.slots != NULL) {
		kfree(session->fc_slot_table.slots);
		session->fc_slot_table.slots = NULL;
	}
	if (session->bc_slot_table.slots != NULL) {
		kfree(session->bc_slot_table.slots);
		session->bc_slot_table.slots = NULL;
	}
	return;
}

/*
 * Initialize slot table
 */
static int nfs4_init_slot_table(struct nfs4_slot_table *tbl,
		int max_slots, int ivalue)
{
	struct nfs4_slot *slot;
	int ret = -ENOMEM;

	BUG_ON(max_slots > NFS4_MAX_SLOT_TABLE);

	dprintk("--> %s: max_reqs=%u\n", __func__, max_slots);

	slot = kcalloc(max_slots, sizeof(struct nfs4_slot), GFP_NOFS);
	if (!slot)
		goto out;
	ret = 0;

	spin_lock(&tbl->slot_tbl_lock);
	tbl->max_slots = max_slots;
	tbl->slots = slot;
	tbl->highest_used_slotid = -1;  /* no slot is currently used */
	spin_unlock(&tbl->slot_tbl_lock);
	dprintk("%s: tbl=%p slots=%p max_slots=%d\n", __func__,
		tbl, tbl->slots, tbl->max_slots);
out:
	dprintk("<-- %s: return %d\n", __func__, ret);
	return ret;
}

/*
 * Initialize the forechannel and backchannel tables
 */
static int nfs4_init_slot_tables(struct nfs4_session *session)
{
	struct nfs4_slot_table *tbl;
	int status = 0;

	tbl = &session->fc_slot_table;
	if (tbl->slots == NULL) {
		status = nfs4_init_slot_table(tbl,
				session->fc_attrs.max_reqs, 1);
		if (status)
			return status;
	}

	tbl = &session->bc_slot_table;
	if (tbl->slots == NULL) {
		status = nfs4_init_slot_table(tbl,
				session->bc_attrs.max_reqs, 0);
		if (status)
			nfs4_destroy_slot_tables(session);
	}

	return status;
}

struct nfs4_session *nfs4_alloc_session(struct nfs_client *clp)
{
	struct nfs4_session *session;
	struct nfs4_slot_table *tbl;

	session = kzalloc(sizeof(struct nfs4_session), GFP_NOFS);
	if (!session)
		return NULL;

	tbl = &session->fc_slot_table;
	tbl->highest_used_slotid = -1;
	spin_lock_init(&tbl->slot_tbl_lock);
	rpc_init_priority_wait_queue(&tbl->slot_tbl_waitq, "ForeChannel Slot table");
	init_completion(&tbl->complete);

	tbl = &session->bc_slot_table;
	tbl->highest_used_slotid = -1;
	spin_lock_init(&tbl->slot_tbl_lock);
	rpc_init_wait_queue(&tbl->slot_tbl_waitq, "BackChannel Slot table");
	init_completion(&tbl->complete);

	session->session_state = 1<<NFS4_SESSION_INITING;

	session->clp = clp;
	return session;
}

void nfs4_destroy_session(struct nfs4_session *session)
{
	nfs4_proc_destroy_session(session);
	dprintk("%s Destroy backchannel for xprt %p\n",
		__func__, session->clp->cl_rpcclient->cl_xprt);
	xprt_destroy_backchannel(session->clp->cl_rpcclient->cl_xprt,
				NFS41_BC_MIN_CALLBACKS);
	nfs4_destroy_slot_tables(session);
	kfree(session);
}

/*
 * Initialize the values to be used by the client in CREATE_SESSION
 * If nfs4_init_session set the fore channel request and response sizes,
 * use them.
 *
 * Set the back channel max_resp_sz_cached to zero to force the client to
 * always set csa_cachethis to FALSE because the current implementation
 * of the back channel DRC only supports caching the CB_SEQUENCE operation.
 */
static void nfs4_init_channel_attrs(struct nfs41_create_session_args *args)
{
	struct nfs4_session *session = args->client->cl_session;
	unsigned int mxrqst_sz = session->fc_attrs.max_rqst_sz,
		     mxresp_sz = session->fc_attrs.max_resp_sz;

	if (mxrqst_sz == 0)
		mxrqst_sz = NFS_MAX_FILE_IO_SIZE;
	if (mxresp_sz == 0)
		mxresp_sz = NFS_MAX_FILE_IO_SIZE;
	/* Fore channel attributes */
	args->fc_attrs.max_rqst_sz = mxrqst_sz;
	args->fc_attrs.max_resp_sz = mxresp_sz;
	args->fc_attrs.max_ops = NFS4_MAX_OPS;
	args->fc_attrs.max_reqs = session->clp->cl_rpcclient->cl_xprt->max_reqs;

	dprintk("%s: Fore Channel : max_rqst_sz=%u max_resp_sz=%u "
		"max_ops=%u max_reqs=%u\n",
		__func__,
		args->fc_attrs.max_rqst_sz, args->fc_attrs.max_resp_sz,
		args->fc_attrs.max_ops, args->fc_attrs.max_reqs);

	/* Back channel attributes */
	args->bc_attrs.max_rqst_sz = PAGE_SIZE;
	args->bc_attrs.max_resp_sz = PAGE_SIZE;
	args->bc_attrs.max_resp_sz_cached = 0;
	args->bc_attrs.max_ops = NFS4_MAX_BACK_CHANNEL_OPS;
	args->bc_attrs.max_reqs = 1;

	dprintk("%s: Back Channel : max_rqst_sz=%u max_resp_sz=%u "
		"max_resp_sz_cached=%u max_ops=%u max_reqs=%u\n",
		__func__,
		args->bc_attrs.max_rqst_sz, args->bc_attrs.max_resp_sz,
		args->bc_attrs.max_resp_sz_cached, args->bc_attrs.max_ops,
		args->bc_attrs.max_reqs);
}

static int nfs4_verify_fore_channel_attrs(struct nfs41_create_session_args *args, struct nfs4_session *session)
{
	struct nfs4_channel_attrs *sent = &args->fc_attrs;
	struct nfs4_channel_attrs *rcvd = &session->fc_attrs;

	if (rcvd->max_resp_sz > sent->max_resp_sz)
		return -EINVAL;
	/*
	 * Our requested max_ops is the minimum we need; we're not
	 * prepared to break up compounds into smaller pieces than that.
	 * So, no point even trying to continue if the server won't
	 * cooperate:
	 */
	if (rcvd->max_ops < sent->max_ops)
		return -EINVAL;
	if (rcvd->max_reqs == 0)
		return -EINVAL;
	return 0;
}

static int nfs4_verify_back_channel_attrs(struct nfs41_create_session_args *args, struct nfs4_session *session)
{
	struct nfs4_channel_attrs *sent = &args->bc_attrs;
	struct nfs4_channel_attrs *rcvd = &session->bc_attrs;

	if (rcvd->max_rqst_sz > sent->max_rqst_sz)
		return -EINVAL;
	if (rcvd->max_resp_sz < sent->max_resp_sz)
		return -EINVAL;
	if (rcvd->max_resp_sz_cached > sent->max_resp_sz_cached)
		return -EINVAL;
	/* These would render the backchannel useless: */
	if (rcvd->max_ops  == 0)
		return -EINVAL;
	if (rcvd->max_reqs == 0)
		return -EINVAL;
	return 0;
}

static int nfs4_verify_channel_attrs(struct nfs41_create_session_args *args,
				     struct nfs4_session *session)
{
	int ret;

	ret = nfs4_verify_fore_channel_attrs(args, session);
	if (ret)
		return ret;
	return nfs4_verify_back_channel_attrs(args, session);
}

static int _nfs4_proc_create_session(struct nfs_client *clp)
{
	struct nfs4_session *session = clp->cl_session;
	struct nfs41_create_session_args args = {
		.client = clp,
		.cb_program = NFS4_CALLBACK,
	};
	struct nfs41_create_session_res res = {
		.client = clp,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_CREATE_SESSION],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	int status;

	nfs4_init_channel_attrs(&args);
	args.flags = (SESSION4_PERSIST | SESSION4_BACK_CHAN);

	status = rpc_call_sync(session->clp->cl_rpcclient, &msg, RPC_TASK_TIMEOUT);

	if (!status)
		/* Verify the session's negotiated channel_attrs values */
		status = nfs4_verify_channel_attrs(&args, session);
	if (!status) {
		/* Increment the clientid slot sequence id */
		clp->cl_seqid++;
	}

	return status;
}

/*
 * Issues a CREATE_SESSION operation to the server.
 * It is the responsibility of the caller to verify the session is
 * expired before calling this routine.
 */
int nfs4_proc_create_session(struct nfs_client *clp)
{
	int status;
	unsigned *ptr;
	struct nfs4_session *session = clp->cl_session;

	dprintk("--> %s clp=%p session=%p\n", __func__, clp, session);

	status = _nfs4_proc_create_session(clp);
	if (status)
		goto out;

	/* Init and reset the fore channel */
	status = nfs4_init_slot_tables(session);
	dprintk("slot table initialization returned %d\n", status);
	if (status)
		goto out;
	status = nfs4_reset_slot_tables(session);
	dprintk("slot table reset returned %d\n", status);
	if (status)
		goto out;

	ptr = (unsigned *)&session->sess_id.data[0];
	dprintk("%s client>seqid %d sessionid %u:%u:%u:%u\n", __func__,
		clp->cl_seqid, ptr[0], ptr[1], ptr[2], ptr[3]);
out:
	dprintk("<-- %s\n", __func__);
	return status;
}

/*
 * Issue the over-the-wire RPC DESTROY_SESSION.
 * The caller must serialize access to this routine.
 */
int nfs4_proc_destroy_session(struct nfs4_session *session)
{
	int status = 0;
	struct rpc_message msg;

	dprintk("--> nfs4_proc_destroy_session\n");

	/* session is still being setup */
	if (session->clp->cl_cons_state != NFS_CS_READY)
		return status;

	msg.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_DESTROY_SESSION];
	msg.rpc_argp = session;
	msg.rpc_resp = NULL;
	msg.rpc_cred = NULL;
	status = rpc_call_sync(session->clp->cl_rpcclient, &msg, RPC_TASK_TIMEOUT);

	if (status)
		printk(KERN_WARNING
			"Got error %d from the server on DESTROY_SESSION. "
			"Session has been destroyed regardless...\n", status);

	dprintk("<-- nfs4_proc_destroy_session\n");
	return status;
}

int nfs4_init_session(struct nfs_server *server)
{
	struct nfs_client *clp = server->nfs_client;
	struct nfs4_session *session;
	unsigned int rsize, wsize;
	int ret;

	if (!nfs4_has_session(clp))
		return 0;

	session = clp->cl_session;
	if (!test_and_clear_bit(NFS4_SESSION_INITING, &session->session_state))
		return 0;

	rsize = server->rsize;
	if (rsize == 0)
		rsize = NFS_MAX_FILE_IO_SIZE;
	wsize = server->wsize;
	if (wsize == 0)
		wsize = NFS_MAX_FILE_IO_SIZE;

	session->fc_attrs.max_rqst_sz = wsize + nfs41_maxwrite_overhead;
	session->fc_attrs.max_resp_sz = rsize + nfs41_maxread_overhead;

	ret = nfs4_recover_expired_lease(server);
	if (!ret)
		ret = nfs4_check_client_ready(clp);
	return ret;
}

int nfs4_init_ds_session(struct nfs_client *clp)
{
	struct nfs4_session *session = clp->cl_session;
	int ret;

	if (!test_and_clear_bit(NFS4_SESSION_INITING, &session->session_state))
		return 0;

	ret = nfs4_client_recover_expired_lease(clp);
	if (!ret)
		/* Test for the DS role */
		if (!is_ds_client(clp))
			ret = -ENODEV;
	if (!ret)
		ret = nfs4_check_client_ready(clp);
	return ret;

}
EXPORT_SYMBOL_GPL(nfs4_init_ds_session);


/*
 * Renew the cl_session lease.
 */
struct nfs4_sequence_data {
	struct nfs_client *clp;
	struct nfs4_sequence_args args;
	struct nfs4_sequence_res res;
};

static void nfs41_sequence_release(void *data)
{
	struct nfs4_sequence_data *calldata = data;
	struct nfs_client *clp = calldata->clp;

	if (atomic_read(&clp->cl_count) > 1)
		nfs4_schedule_state_renewal(clp);
	nfs_put_client(clp);
	kfree(calldata);
}

static int nfs41_sequence_handle_errors(struct rpc_task *task, struct nfs_client *clp)
{
	switch(task->tk_status) {
	case -NFS4ERR_DELAY:
		rpc_delay(task, NFS4_POLL_RETRY_MAX);
		return -EAGAIN;
	default:
		nfs4_schedule_lease_recovery(clp);
	}
	return 0;
}

static void nfs41_sequence_call_done(struct rpc_task *task, void *data)
{
	struct nfs4_sequence_data *calldata = data;
	struct nfs_client *clp = calldata->clp;

	if (!nfs41_sequence_done(task, task->tk_msg.rpc_resp))
		return;

	if (task->tk_status < 0) {
		dprintk("%s ERROR %d\n", __func__, task->tk_status);
		if (atomic_read(&clp->cl_count) == 1)
			goto out;

		if (nfs41_sequence_handle_errors(task, clp) == -EAGAIN) {
			rpc_restart_call_prepare(task);
			return;
		}
	}
	dprintk("%s rpc_cred %p\n", __func__, task->tk_msg.rpc_cred);
out:
	dprintk("<-- %s\n", __func__);
}

static void nfs41_sequence_prepare(struct rpc_task *task, void *data)
{
	struct nfs4_sequence_data *calldata = data;
	struct nfs_client *clp = calldata->clp;
	struct nfs4_sequence_args *args;
	struct nfs4_sequence_res *res;

	args = task->tk_msg.rpc_argp;
	res = task->tk_msg.rpc_resp;

	if (nfs41_setup_sequence(clp->cl_session, args, res, 0, task))
		return;
	rpc_call_start(task);
}

static const struct rpc_call_ops nfs41_sequence_ops = {
	.rpc_call_done = nfs41_sequence_call_done,
	.rpc_call_prepare = nfs41_sequence_prepare,
	.rpc_release = nfs41_sequence_release,
};

static struct rpc_task *_nfs41_proc_sequence(struct nfs_client *clp, struct rpc_cred *cred)
{
	struct nfs4_sequence_data *calldata;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SEQUENCE],
		.rpc_cred = cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = clp->cl_rpcclient,
		.rpc_message = &msg,
		.callback_ops = &nfs41_sequence_ops,
		.flags = RPC_TASK_ASYNC | RPC_TASK_SOFT,
	};

	if (!atomic_inc_not_zero(&clp->cl_count))
		return ERR_PTR(-EIO);
	calldata = kzalloc(sizeof(*calldata), GFP_NOFS);
	if (calldata == NULL) {
		nfs_put_client(clp);
		return ERR_PTR(-ENOMEM);
	}
	msg.rpc_argp = &calldata->args;
	msg.rpc_resp = &calldata->res;
	calldata->clp = clp;
	task_setup_data.callback_data = calldata;

	return rpc_run_task(&task_setup_data);
}

static int nfs41_proc_async_sequence(struct nfs_client *clp, struct rpc_cred *cred)
{
	struct rpc_task *task;
	int ret = 0;

	task = _nfs41_proc_sequence(clp, cred);
	if (IS_ERR(task))
		ret = PTR_ERR(task);
	else
		rpc_put_task_async(task);
	dprintk("<-- %s status=%d\n", __func__, ret);
	return ret;
}

static int nfs4_proc_sequence(struct nfs_client *clp, struct rpc_cred *cred)
{
	struct rpc_task *task;
	int ret;

	task = _nfs41_proc_sequence(clp, cred);
	if (IS_ERR(task)) {
		ret = PTR_ERR(task);
		goto out;
	}
	ret = rpc_wait_for_completion_task(task);
	if (!ret) {
		struct nfs4_sequence_res *res = task->tk_msg.rpc_resp;

		if (task->tk_status == 0)
			nfs41_handle_sequence_flag_errors(clp, res->sr_status_flags);
		ret = task->tk_status;
	}
	rpc_put_task(task);
out:
	dprintk("<-- %s status=%d\n", __func__, ret);
	return ret;
}

struct nfs4_reclaim_complete_data {
	struct nfs_client *clp;
	struct nfs41_reclaim_complete_args arg;
	struct nfs41_reclaim_complete_res res;
};

static void nfs4_reclaim_complete_prepare(struct rpc_task *task, void *data)
{
	struct nfs4_reclaim_complete_data *calldata = data;

	rpc_task_set_priority(task, RPC_PRIORITY_PRIVILEGED);
	if (nfs41_setup_sequence(calldata->clp->cl_session,
				&calldata->arg.seq_args,
				&calldata->res.seq_res, 0, task))
		return;

	rpc_call_start(task);
}

static int nfs41_reclaim_complete_handle_errors(struct rpc_task *task, struct nfs_client *clp)
{
	switch(task->tk_status) {
	case 0:
	case -NFS4ERR_COMPLETE_ALREADY:
	case -NFS4ERR_WRONG_CRED: /* What to do here? */
		break;
	case -NFS4ERR_DELAY:
		rpc_delay(task, NFS4_POLL_RETRY_MAX);
		/* fall through */
	case -NFS4ERR_RETRY_UNCACHED_REP:
		return -EAGAIN;
	default:
		nfs4_schedule_lease_recovery(clp);
	}
	return 0;
}

static void nfs4_reclaim_complete_done(struct rpc_task *task, void *data)
{
	struct nfs4_reclaim_complete_data *calldata = data;
	struct nfs_client *clp = calldata->clp;
	struct nfs4_sequence_res *res = &calldata->res.seq_res;

	dprintk("--> %s\n", __func__);
	if (!nfs41_sequence_done(task, res))
		return;

	if (nfs41_reclaim_complete_handle_errors(task, clp) == -EAGAIN) {
		rpc_restart_call_prepare(task);
		return;
	}
	dprintk("<-- %s\n", __func__);
}

static void nfs4_free_reclaim_complete_data(void *data)
{
	struct nfs4_reclaim_complete_data *calldata = data;

	kfree(calldata);
}

static const struct rpc_call_ops nfs4_reclaim_complete_call_ops = {
	.rpc_call_prepare = nfs4_reclaim_complete_prepare,
	.rpc_call_done = nfs4_reclaim_complete_done,
	.rpc_release = nfs4_free_reclaim_complete_data,
};

/*
 * Issue a global reclaim complete.
 */
static int nfs41_proc_reclaim_complete(struct nfs_client *clp)
{
	struct nfs4_reclaim_complete_data *calldata;
	struct rpc_task *task;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_RECLAIM_COMPLETE],
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = clp->cl_rpcclient,
		.rpc_message = &msg,
		.callback_ops = &nfs4_reclaim_complete_call_ops,
		.flags = RPC_TASK_ASYNC,
	};
	int status = -ENOMEM;

	dprintk("--> %s\n", __func__);
	calldata = kzalloc(sizeof(*calldata), GFP_NOFS);
	if (calldata == NULL)
		goto out;
	calldata->clp = clp;
	calldata->arg.one_fs = 0;

	msg.rpc_argp = &calldata->arg;
	msg.rpc_resp = &calldata->res;
	task_setup_data.callback_data = calldata;
	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task)) {
		status = PTR_ERR(task);
		goto out;
	}
	status = nfs4_wait_for_completion_rpc_task(task);
	if (status == 0)
		status = task->tk_status;
	rpc_put_task(task);
	return 0;
out:
	dprintk("<-- %s status=%d\n", __func__, status);
	return status;
}

static void
nfs4_layoutget_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs4_layoutget *lgp = calldata;
	struct nfs_server *server = NFS_SERVER(lgp->args.inode);

	dprintk("--> %s\n", __func__);
	/* Note the is a race here, where a CB_LAYOUTRECALL can come in
	 * right now covering the LAYOUTGET we are about to send.
	 * However, that is not so catastrophic, and there seems
	 * to be no way to prevent it completely.
	 */
	if (nfs4_setup_sequence(server, &lgp->args.seq_args,
				&lgp->res.seq_res, 0, task))
		return;
	if (pnfs_choose_layoutget_stateid(&lgp->args.stateid,
					  NFS_I(lgp->args.inode)->layout,
					  lgp->args.ctx->state)) {
		rpc_exit(task, NFS4_OK);
		return;
	}
	rpc_call_start(task);
}

static void nfs4_layoutget_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_layoutget *lgp = calldata;
	struct nfs_server *server = NFS_SERVER(lgp->args.inode);

	dprintk("--> %s\n", __func__);

	if (!nfs4_sequence_done(task, &lgp->res.seq_res))
		return;

	switch (task->tk_status) {
	case 0:
		break;
	case -NFS4ERR_LAYOUTTRYLATER:
	case -NFS4ERR_RECALLCONFLICT:
		task->tk_status = -NFS4ERR_DELAY;
		/* Fall through */
	default:
		if (nfs4_async_handle_error(task, server, NULL) == -EAGAIN) {
			rpc_restart_call_prepare(task);
			return;
		}
	}
	dprintk("<-- %s\n", __func__);
}

static void nfs4_layoutget_release(void *calldata)
{
	struct nfs4_layoutget *lgp = calldata;

	dprintk("--> %s\n", __func__);
	put_nfs_open_context(lgp->args.ctx);
	kfree(calldata);
	dprintk("<-- %s\n", __func__);
}

static const struct rpc_call_ops nfs4_layoutget_call_ops = {
	.rpc_call_prepare = nfs4_layoutget_prepare,
	.rpc_call_done = nfs4_layoutget_done,
	.rpc_release = nfs4_layoutget_release,
};

int nfs4_proc_layoutget(struct nfs4_layoutget *lgp)
{
	struct nfs_server *server = NFS_SERVER(lgp->args.inode);
	struct rpc_task *task;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LAYOUTGET],
		.rpc_argp = &lgp->args,
		.rpc_resp = &lgp->res,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = server->client,
		.rpc_message = &msg,
		.callback_ops = &nfs4_layoutget_call_ops,
		.callback_data = lgp,
		.flags = RPC_TASK_ASYNC,
	};
	int status = 0;

	dprintk("--> %s\n", __func__);

	lgp->res.layoutp = &lgp->args.layout;
	lgp->res.seq_res.sr_slot = NULL;
	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	status = nfs4_wait_for_completion_rpc_task(task);
	if (status == 0)
		status = task->tk_status;
	if (status == 0)
		status = pnfs_layout_process(lgp);
	rpc_put_task(task);
	dprintk("<-- %s status=%d\n", __func__, status);
	return status;
}

static void
nfs4_layoutreturn_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs4_layoutreturn *lrp = calldata;

	dprintk("--> %s\n", __func__);
	if (nfs41_setup_sequence(lrp->clp->cl_session, &lrp->args.seq_args,
				&lrp->res.seq_res, 0, task))
		return;
	rpc_call_start(task);
}

static void nfs4_layoutreturn_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_layoutreturn *lrp = calldata;
	struct nfs_server *server;
	struct pnfs_layout_hdr *lo = NFS_I(lrp->args.inode)->layout;

	dprintk("--> %s\n", __func__);

	if (!nfs4_sequence_done(task, &lrp->res.seq_res))
		return;

	server = NFS_SERVER(lrp->args.inode);
	if (nfs4_async_handle_error(task, server, NULL) == -EAGAIN) {
		nfs_restart_rpc(task, lrp->clp);
		return;
	}
	spin_lock(&lo->plh_inode->i_lock);
	if (task->tk_status == 0 && lrp->res.lrs_present)
		pnfs_set_layout_stateid(lo, &lrp->res.stateid, true);
	lo->plh_block_lgets--;
	spin_unlock(&lo->plh_inode->i_lock);
	dprintk("<-- %s\n", __func__);
}

static void nfs4_layoutreturn_release(void *calldata)
{
	struct nfs4_layoutreturn *lrp = calldata;

	dprintk("--> %s\n", __func__);
	put_layout_hdr(NFS_I(lrp->args.inode)->layout);
	kfree(calldata);
	dprintk("<-- %s\n", __func__);
}

static const struct rpc_call_ops nfs4_layoutreturn_call_ops = {
	.rpc_call_prepare = nfs4_layoutreturn_prepare,
	.rpc_call_done = nfs4_layoutreturn_done,
	.rpc_release = nfs4_layoutreturn_release,
};

int nfs4_proc_layoutreturn(struct nfs4_layoutreturn *lrp)
{
	struct rpc_task *task;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LAYOUTRETURN],
		.rpc_argp = &lrp->args,
		.rpc_resp = &lrp->res,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = lrp->clp->cl_rpcclient,
		.rpc_message = &msg,
		.callback_ops = &nfs4_layoutreturn_call_ops,
		.callback_data = lrp,
	};
	int status;

	dprintk("--> %s\n", __func__);
	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	status = task->tk_status;
	dprintk("<-- %s status=%d\n", __func__, status);
	rpc_put_task(task);
	return status;
}

static int
_nfs4_proc_getdeviceinfo(struct nfs_server *server, struct pnfs_device *pdev)
{
	struct nfs4_getdeviceinfo_args args = {
		.pdev = pdev,
	};
	struct nfs4_getdeviceinfo_res res = {
		.pdev = pdev,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_GETDEVICEINFO],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	int status;

	dprintk("--> %s\n", __func__);
	status = nfs4_call_sync(server->client, server, &msg, &args.seq_args, &res.seq_res, 0);
	dprintk("<-- %s status=%d\n", __func__, status);

	return status;
}

int nfs4_proc_getdeviceinfo(struct nfs_server *server, struct pnfs_device *pdev)
{
	struct nfs4_exception exception = { };
	int err;

	do {
		err = nfs4_handle_exception(server,
					_nfs4_proc_getdeviceinfo(server, pdev),
					&exception);
	} while (exception.retry);
	return err;
}
EXPORT_SYMBOL_GPL(nfs4_proc_getdeviceinfo);

static void nfs4_layoutcommit_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs4_layoutcommit_data *data = calldata;
	struct nfs_server *server = NFS_SERVER(data->args.inode);

	if (nfs4_setup_sequence(server, &data->args.seq_args,
				&data->res.seq_res, 1, task))
		return;
	rpc_call_start(task);
}

static void
nfs4_layoutcommit_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_layoutcommit_data *data = calldata;
	struct nfs_server *server = NFS_SERVER(data->args.inode);

	if (!nfs4_sequence_done(task, &data->res.seq_res))
		return;

	switch (task->tk_status) { /* Just ignore these failures */
	case NFS4ERR_DELEG_REVOKED: /* layout was recalled */
	case NFS4ERR_BADIOMODE:     /* no IOMODE_RW layout for range */
	case NFS4ERR_BADLAYOUT:     /* no layout */
	case NFS4ERR_GRACE:	    /* loca_recalim always false */
		task->tk_status = 0;
	}

	if (nfs4_async_handle_error(task, server, NULL) == -EAGAIN) {
		nfs_restart_rpc(task, server->nfs_client);
		return;
	}

	if (task->tk_status == 0)
		nfs_post_op_update_inode_force_wcc(data->args.inode,
						   data->res.fattr);
}

static void nfs4_layoutcommit_release(void *calldata)
{
	struct nfs4_layoutcommit_data *data = calldata;
	struct pnfs_layout_segment *lseg, *tmp;

	/* Matched by references in pnfs_set_layoutcommit */
	list_for_each_entry_safe(lseg, tmp, &data->lseg_list, pls_lc_list) {
		list_del_init(&lseg->pls_lc_list);
		if (test_and_clear_bit(NFS_LSEG_LAYOUTCOMMIT,
				       &lseg->pls_flags))
			put_lseg(lseg);
	}
	put_rpccred(data->cred);
	kfree(data);
}

static const struct rpc_call_ops nfs4_layoutcommit_ops = {
	.rpc_call_prepare = nfs4_layoutcommit_prepare,
	.rpc_call_done = nfs4_layoutcommit_done,
	.rpc_release = nfs4_layoutcommit_release,
};

int
nfs4_proc_layoutcommit(struct nfs4_layoutcommit_data *data, bool sync)
{
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LAYOUTCOMMIT],
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = data->cred,
	};
	struct rpc_task_setup task_setup_data = {
		.task = &data->task,
		.rpc_client = NFS_CLIENT(data->args.inode),
		.rpc_message = &msg,
		.callback_ops = &nfs4_layoutcommit_ops,
		.callback_data = data,
		.flags = RPC_TASK_ASYNC,
	};
	struct rpc_task *task;
	int status = 0;

	dprintk("NFS: %4d initiating layoutcommit call. sync %d "
		"lbw: %llu inode %lu\n",
		data->task.tk_pid, sync,
		data->args.lastbytewritten,
		data->args.inode->i_ino);

	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	if (sync == false)
		goto out;
	status = nfs4_wait_for_completion_rpc_task(task);
	if (status != 0)
		goto out;
	status = task->tk_status;
out:
	dprintk("%s: status %d\n", __func__, status);
	rpc_put_task(task);
	return status;
}
#endif /* CONFIG_NFS_V4_1 */

struct nfs4_state_recovery_ops nfs40_reboot_recovery_ops = {
	.owner_flag_bit = NFS_OWNER_RECLAIM_REBOOT,
	.state_flag_bit	= NFS_STATE_RECLAIM_REBOOT,
	.recover_open	= nfs4_open_reclaim,
	.recover_lock	= nfs4_lock_reclaim,
	.establish_clid = nfs4_init_clientid,
	.get_clid_cred	= nfs4_get_setclientid_cred,
};

#if defined(CONFIG_NFS_V4_1)
struct nfs4_state_recovery_ops nfs41_reboot_recovery_ops = {
	.owner_flag_bit = NFS_OWNER_RECLAIM_REBOOT,
	.state_flag_bit	= NFS_STATE_RECLAIM_REBOOT,
	.recover_open	= nfs4_open_reclaim,
	.recover_lock	= nfs4_lock_reclaim,
	.establish_clid = nfs41_init_clientid,
	.get_clid_cred	= nfs4_get_exchange_id_cred,
	.reclaim_complete = nfs41_proc_reclaim_complete,
};
#endif /* CONFIG_NFS_V4_1 */

struct nfs4_state_recovery_ops nfs40_nograce_recovery_ops = {
	.owner_flag_bit = NFS_OWNER_RECLAIM_NOGRACE,
	.state_flag_bit	= NFS_STATE_RECLAIM_NOGRACE,
	.recover_open	= nfs4_open_expired,
	.recover_lock	= nfs4_lock_expired,
	.establish_clid = nfs4_init_clientid,
	.get_clid_cred	= nfs4_get_setclientid_cred,
};

#if defined(CONFIG_NFS_V4_1)
struct nfs4_state_recovery_ops nfs41_nograce_recovery_ops = {
	.owner_flag_bit = NFS_OWNER_RECLAIM_NOGRACE,
	.state_flag_bit	= NFS_STATE_RECLAIM_NOGRACE,
	.recover_open	= nfs4_open_expired,
	.recover_lock	= nfs4_lock_expired,
	.establish_clid = nfs41_init_clientid,
	.get_clid_cred	= nfs4_get_exchange_id_cred,
};
#endif /* CONFIG_NFS_V4_1 */

struct nfs4_state_maintenance_ops nfs40_state_renewal_ops = {
	.sched_state_renewal = nfs4_proc_async_renew,
	.get_state_renewal_cred_locked = nfs4_get_renew_cred_locked,
	.renew_lease = nfs4_proc_renew,
};

#if defined(CONFIG_NFS_V4_1)
struct nfs4_state_maintenance_ops nfs41_state_renewal_ops = {
	.sched_state_renewal = nfs41_proc_async_sequence,
	.get_state_renewal_cred_locked = nfs4_get_machine_cred_locked,
	.renew_lease = nfs4_proc_sequence,
};
#endif

static const struct nfs4_minor_version_ops nfs_v4_0_minor_ops = {
	.minor_version = 0,
	.call_sync = _nfs4_call_sync,
	.validate_stateid = nfs4_validate_delegation_stateid,
	.reboot_recovery_ops = &nfs40_reboot_recovery_ops,
	.nograce_recovery_ops = &nfs40_nograce_recovery_ops,
	.state_renewal_ops = &nfs40_state_renewal_ops,
};

#if defined(CONFIG_NFS_V4_1)
static const struct nfs4_minor_version_ops nfs_v4_1_minor_ops = {
	.minor_version = 1,
	.call_sync = _nfs4_call_sync_session,
	.validate_stateid = nfs41_validate_delegation_stateid,
	.reboot_recovery_ops = &nfs41_reboot_recovery_ops,
	.nograce_recovery_ops = &nfs41_nograce_recovery_ops,
	.state_renewal_ops = &nfs41_state_renewal_ops,
};
#endif

const struct nfs4_minor_version_ops *nfs_v4_minor_ops[] = {
	[0] = &nfs_v4_0_minor_ops,
#if defined(CONFIG_NFS_V4_1)
	[1] = &nfs_v4_1_minor_ops,
#endif
};

static const struct inode_operations nfs4_file_inode_operations = {
	.permission	= nfs_permission,
	.getattr	= nfs_getattr,
	.setattr	= nfs_setattr,
	.getxattr	= generic_getxattr,
	.setxattr	= generic_setxattr,
	.listxattr	= generic_listxattr,
	.removexattr	= generic_removexattr,
};

const struct nfs_rpc_ops nfs_v4_clientops = {
	.version	= 4,			/* protocol version */
	.dentry_ops	= &nfs4_dentry_operations,
	.dir_inode_ops	= &nfs4_dir_inode_operations,
	.file_inode_ops	= &nfs4_file_inode_operations,
	.file_ops	= &nfs4_file_operations,
	.getroot	= nfs4_proc_get_root,
	.getattr	= nfs4_proc_getattr,
	.setattr	= nfs4_proc_setattr,
	.lookupfh	= nfs4_proc_lookupfh,
	.lookup		= nfs4_proc_lookup,
	.access		= nfs4_proc_access,
	.readlink	= nfs4_proc_readlink,
	.create		= nfs4_proc_create,
	.remove		= nfs4_proc_remove,
	.unlink_setup	= nfs4_proc_unlink_setup,
	.unlink_done	= nfs4_proc_unlink_done,
	.rename		= nfs4_proc_rename,
	.rename_setup	= nfs4_proc_rename_setup,
	.rename_done	= nfs4_proc_rename_done,
	.link		= nfs4_proc_link,
	.symlink	= nfs4_proc_symlink,
	.mkdir		= nfs4_proc_mkdir,
	.rmdir		= nfs4_proc_remove,
	.readdir	= nfs4_proc_readdir,
	.mknod		= nfs4_proc_mknod,
	.statfs		= nfs4_proc_statfs,
	.fsinfo		= nfs4_proc_fsinfo,
	.pathconf	= nfs4_proc_pathconf,
	.set_capabilities = nfs4_server_capabilities,
	.decode_dirent	= nfs4_decode_dirent,
	.read_setup	= nfs4_proc_read_setup,
	.read_done	= nfs4_read_done,
	.write_setup	= nfs4_proc_write_setup,
	.write_done	= nfs4_write_done,
	.commit_setup	= nfs4_proc_commit_setup,
	.commit_done	= nfs4_commit_done,
	.lock		= nfs4_proc_lock,
	.clear_acl_cache = nfs4_zap_acl_attr,
	.close_context  = nfs4_close_context,
	.open_context	= nfs4_atomic_open,
	.init_client	= nfs4_init_client,
	.secinfo	= nfs4_proc_secinfo,
};

static const struct xattr_handler nfs4_xattr_nfs4_acl_handler = {
	.prefix	= XATTR_NAME_NFSV4_ACL,
	.list	= nfs4_xattr_list_nfs4_acl,
	.get	= nfs4_xattr_get_nfs4_acl,
	.set	= nfs4_xattr_set_nfs4_acl,
};

const struct xattr_handler *nfs4_xattr_handlers[] = {
	&nfs4_xattr_nfs4_acl_handler,
	NULL
};

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */

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
#include <linux/ratelimit.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/nfs_mount.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/module.h>
#include <linux/nfs_idmap.h>
#include <linux/xattr.h>
#include <linux/utsname.h>
#include <linux/freezer.h>

#include "nfs4_fs.h"
#include "delegation.h"
#include "internal.h"
#include "iostat.h"
#include "callback.h"
#include "pnfs.h"
#include "netns.h"
#include "nfs4session.h"
#include "fscache.h"

#include "nfs4trace.h"

#define NFSDBG_FACILITY		NFSDBG_PROC

#define NFS4_POLL_RETRY_MIN	(HZ/10)
#define NFS4_POLL_RETRY_MAX	(15*HZ)

struct nfs4_opendata;
static int _nfs4_proc_open(struct nfs4_opendata *data);
static int _nfs4_recover_proc_open(struct nfs4_opendata *data);
static int nfs4_do_fsinfo(struct nfs_server *, struct nfs_fh *, struct nfs_fsinfo *);
static int nfs4_async_handle_error(struct rpc_task *, const struct nfs_server *, struct nfs4_state *, long *);
static void nfs_fixup_referral_attributes(struct nfs_fattr *fattr);
static int nfs4_proc_getattr(struct nfs_server *, struct nfs_fh *, struct nfs_fattr *, struct nfs4_label *label);
static int _nfs4_proc_getattr(struct nfs_server *server, struct nfs_fh *fhandle, struct nfs_fattr *fattr, struct nfs4_label *label);
static int nfs4_do_setattr(struct inode *inode, struct rpc_cred *cred,
			    struct nfs_fattr *fattr, struct iattr *sattr,
			    struct nfs4_state *state, struct nfs4_label *ilabel,
			    struct nfs4_label *olabel);
#ifdef CONFIG_NFS_V4_1
static int nfs41_test_stateid(struct nfs_server *, nfs4_stateid *,
		struct rpc_cred *);
static int nfs41_free_stateid(struct nfs_server *, nfs4_stateid *,
		struct rpc_cred *);
#endif

#ifdef CONFIG_NFS_V4_SECURITY_LABEL
static inline struct nfs4_label *
nfs4_label_init_security(struct inode *dir, struct dentry *dentry,
	struct iattr *sattr, struct nfs4_label *label)
{
	int err;

	if (label == NULL)
		return NULL;

	if (nfs_server_capable(dir, NFS_CAP_SECURITY_LABEL) == 0)
		return NULL;

	err = security_dentry_init_security(dentry, sattr->ia_mode,
				&dentry->d_name, (void **)&label->label, &label->len);
	if (err == 0)
		return label;

	return NULL;
}
static inline void
nfs4_label_release_security(struct nfs4_label *label)
{
	if (label)
		security_release_secctx(label->label, label->len);
}
static inline u32 *nfs4_bitmask(struct nfs_server *server, struct nfs4_label *label)
{
	if (label)
		return server->attr_bitmask;

	return server->attr_bitmask_nl;
}
#else
static inline struct nfs4_label *
nfs4_label_init_security(struct inode *dir, struct dentry *dentry,
	struct iattr *sattr, struct nfs4_label *l)
{ return NULL; }
static inline void
nfs4_label_release_security(struct nfs4_label *label)
{ return; }
static inline u32 *
nfs4_bitmask(struct nfs_server *server, struct nfs4_label *label)
{ return server->attr_bitmask; }
#endif

/* Prevent leaks of NFSv4 errors into userland */
static int nfs4_map_errors(int err)
{
	if (err >= -1000)
		return err;
	switch (err) {
	case -NFS4ERR_RESOURCE:
	case -NFS4ERR_LAYOUTTRYLATER:
	case -NFS4ERR_RECALLCONFLICT:
		return -EREMOTEIO;
	case -NFS4ERR_WRONGSEC:
	case -NFS4ERR_WRONG_CRED:
		return -EPERM;
	case -NFS4ERR_BADOWNER:
	case -NFS4ERR_BADNAME:
		return -EINVAL;
	case -NFS4ERR_SHARE_DENIED:
		return -EACCES;
	case -NFS4ERR_MINOR_VERS_MISMATCH:
		return -EPROTONOSUPPORT;
	case -NFS4ERR_FILE_OPEN:
		return -EBUSY;
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
const u32 nfs4_fattr_bitmap[3] = {
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
	| FATTR4_WORD1_TIME_MODIFY,
#ifdef CONFIG_NFS_V4_SECURITY_LABEL
	FATTR4_WORD2_SECURITY_LABEL
#endif
};

static const u32 nfs4_pnfs_open_bitmap[3] = {
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
	| FATTR4_WORD1_TIME_MODIFY,
	FATTR4_WORD2_MDSTHRESHOLD
};

static const u32 nfs4_open_noattr_bitmap[3] = {
	FATTR4_WORD0_TYPE
	| FATTR4_WORD0_CHANGE
	| FATTR4_WORD0_FILEID,
};

const u32 nfs4_statfs_bitmap[3] = {
	FATTR4_WORD0_FILES_AVAIL
	| FATTR4_WORD0_FILES_FREE
	| FATTR4_WORD0_FILES_TOTAL,
	FATTR4_WORD1_SPACE_AVAIL
	| FATTR4_WORD1_SPACE_FREE
	| FATTR4_WORD1_SPACE_TOTAL
};

const u32 nfs4_pathconf_bitmap[3] = {
	FATTR4_WORD0_MAXLINK
	| FATTR4_WORD0_MAXNAME,
	0
};

const u32 nfs4_fsinfo_bitmap[3] = { FATTR4_WORD0_MAXFILESIZE
			| FATTR4_WORD0_MAXREAD
			| FATTR4_WORD0_MAXWRITE
			| FATTR4_WORD0_LEASE_TIME,
			FATTR4_WORD1_TIME_DELTA
			| FATTR4_WORD1_FS_LAYOUT_TYPES,
			FATTR4_WORD2_LAYOUT_BLKSIZE
};

const u32 nfs4_fs_locations_bitmap[3] = {
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
	| FATTR4_WORD1_MOUNTED_ON_FILEID,
};

static void nfs4_setup_readdir(u64 cookie, __be32 *verifier, struct dentry *dentry,
		struct nfs4_readdir_arg *readdir)
{
	__be32 *start, *p;

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
	start = p = kmap_atomic(*readdir->pages);
	
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
	kunmap_atomic(start);
}

static long nfs4_update_delay(long *timeout)
{
	long ret;
	if (!timeout)
		return NFS4_POLL_RETRY_MAX;
	if (*timeout <= 0)
		*timeout = NFS4_POLL_RETRY_MIN;
	if (*timeout > NFS4_POLL_RETRY_MAX)
		*timeout = NFS4_POLL_RETRY_MAX;
	ret = *timeout;
	*timeout <<= 1;
	return ret;
}

static int nfs4_delay(struct rpc_clnt *clnt, long *timeout)
{
	int res = 0;

	might_sleep();

	freezable_schedule_timeout_killable_unsafe(
		nfs4_update_delay(timeout));
	if (fatal_signal_pending(current))
		res = -ERESTARTSYS;
	return res;
}

/* This is the error handling routine for processes that are allowed
 * to sleep.
 */
int nfs4_handle_exception(struct nfs_server *server, int errorcode, struct nfs4_exception *exception)
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
			if (inode && nfs4_have_delegation(inode, FMODE_READ)) {
				nfs4_inode_return_delegation(inode);
				exception->retry = 1;
				return 0;
			}
			if (state == NULL)
				break;
			ret = nfs4_schedule_stateid_recovery(server, state);
			if (ret < 0)
				break;
			goto wait_on_recovery;
		case -NFS4ERR_DELEG_REVOKED:
		case -NFS4ERR_ADMIN_REVOKED:
		case -NFS4ERR_BAD_STATEID:
			if (state == NULL)
				break;
			ret = nfs4_schedule_stateid_recovery(server, state);
			if (ret < 0)
				break;
			goto wait_on_recovery;
		case -NFS4ERR_EXPIRED:
			if (state != NULL) {
				ret = nfs4_schedule_stateid_recovery(server, state);
				if (ret < 0)
					break;
			}
		case -NFS4ERR_STALE_STATEID:
		case -NFS4ERR_STALE_CLIENTID:
			nfs4_schedule_lease_recovery(clp);
			goto wait_on_recovery;
		case -NFS4ERR_MOVED:
			ret = nfs4_schedule_migration_recovery(server);
			if (ret < 0)
				break;
			goto wait_on_recovery;
		case -NFS4ERR_LEASE_MOVED:
			nfs4_schedule_lease_moved_recovery(clp);
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
			nfs4_schedule_session_recovery(clp->cl_session, errorcode);
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
	if (test_bit(NFS_MIG_FAILED, &server->mig_status))
		return -EIO;
	if (ret == 0)
		exception->retry = 1;
	return ret;
}

/*
 * Return 'true' if 'clp' is using an rpc_client that is integrity protected
 * or 'false' otherwise.
 */
static bool _nfs4_is_integrity_protected(struct nfs_client *clp)
{
	rpc_authflavor_t flavor = clp->cl_rpcclient->cl_auth->au_flavor;

	if (flavor == RPC_AUTH_GSS_KRB5I ||
	    flavor == RPC_AUTH_GSS_KRB5P)
		return true;

	return false;
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

struct nfs4_call_sync_data {
	const struct nfs_server *seq_server;
	struct nfs4_sequence_args *seq_args;
	struct nfs4_sequence_res *seq_res;
};

static void nfs4_init_sequence(struct nfs4_sequence_args *args,
			       struct nfs4_sequence_res *res, int cache_reply)
{
	args->sa_slot = NULL;
	args->sa_cache_this = cache_reply;
	args->sa_privileged = 0;

	res->sr_slot = NULL;
}

static void nfs4_set_sequence_privileged(struct nfs4_sequence_args *args)
{
	args->sa_privileged = 1;
}

static int nfs40_setup_sequence(const struct nfs_server *server,
				struct nfs4_sequence_args *args,
				struct nfs4_sequence_res *res,
				struct rpc_task *task)
{
	struct nfs4_slot_table *tbl = server->nfs_client->cl_slot_tbl;
	struct nfs4_slot *slot;

	/* slot already allocated? */
	if (res->sr_slot != NULL)
		goto out_start;

	spin_lock(&tbl->slot_tbl_lock);
	if (nfs4_slot_tbl_draining(tbl) && !args->sa_privileged)
		goto out_sleep;

	slot = nfs4_alloc_slot(tbl);
	if (IS_ERR(slot)) {
		if (slot == ERR_PTR(-ENOMEM))
			task->tk_timeout = HZ >> 2;
		goto out_sleep;
	}
	spin_unlock(&tbl->slot_tbl_lock);

	args->sa_slot = slot;
	res->sr_slot = slot;

out_start:
	rpc_call_start(task);
	return 0;

out_sleep:
	if (args->sa_privileged)
		rpc_sleep_on_priority(&tbl->slot_tbl_waitq, task,
				NULL, RPC_PRIORITY_PRIVILEGED);
	else
		rpc_sleep_on(&tbl->slot_tbl_waitq, task, NULL);
	spin_unlock(&tbl->slot_tbl_lock);
	return -EAGAIN;
}

static int nfs40_sequence_done(struct rpc_task *task,
			       struct nfs4_sequence_res *res)
{
	struct nfs4_slot *slot = res->sr_slot;
	struct nfs4_slot_table *tbl;

	if (slot == NULL)
		goto out;

	tbl = slot->table;
	spin_lock(&tbl->slot_tbl_lock);
	if (!nfs41_wake_and_assign_slot(tbl, slot))
		nfs4_free_slot(tbl, slot);
	spin_unlock(&tbl->slot_tbl_lock);

	res->sr_slot = NULL;
out:
	return 1;
}

#if defined(CONFIG_NFS_V4_1)

static void nfs41_sequence_free_slot(struct nfs4_sequence_res *res)
{
	struct nfs4_session *session;
	struct nfs4_slot_table *tbl;
	struct nfs4_slot *slot = res->sr_slot;
	bool send_new_highest_used_slotid = false;

	tbl = slot->table;
	session = tbl->session;

	spin_lock(&tbl->slot_tbl_lock);
	/* Be nice to the server: try to ensure that the last transmitted
	 * value for highest_user_slotid <= target_highest_slotid
	 */
	if (tbl->highest_used_slotid > tbl->target_highest_slotid)
		send_new_highest_used_slotid = true;

	if (nfs41_wake_and_assign_slot(tbl, slot)) {
		send_new_highest_used_slotid = false;
		goto out_unlock;
	}
	nfs4_free_slot(tbl, slot);

	if (tbl->highest_used_slotid != NFS4_NO_SLOT)
		send_new_highest_used_slotid = false;
out_unlock:
	spin_unlock(&tbl->slot_tbl_lock);
	res->sr_slot = NULL;
	if (send_new_highest_used_slotid)
		nfs41_server_notify_highest_slotid_update(session->clp);
}

int nfs41_sequence_done(struct rpc_task *task, struct nfs4_sequence_res *res)
{
	struct nfs4_session *session;
	struct nfs4_slot *slot = res->sr_slot;
	struct nfs_client *clp;
	bool interrupted = false;
	int ret = 1;

	if (slot == NULL)
		goto out_noaction;
	/* don't increment the sequence number if the task wasn't sent */
	if (!RPC_WAS_SENT(task))
		goto out;

	session = slot->table->session;

	if (slot->interrupted) {
		slot->interrupted = 0;
		interrupted = true;
	}

	trace_nfs4_sequence_done(session, res);
	/* Check the SEQUENCE operation status */
	switch (res->sr_status) {
	case 0:
		/* Update the slot's sequence and clientid lease timer */
		++slot->seq_nr;
		clp = session->clp;
		do_renew_lease(clp, res->sr_timestamp);
		/* Check sequence flags */
		if (res->sr_status_flags != 0)
			nfs4_schedule_lease_recovery(clp);
		nfs41_update_target_slotid(slot->table, slot, res);
		break;
	case 1:
		/*
		 * sr_status remains 1 if an RPC level error occurred.
		 * The server may or may not have processed the sequence
		 * operation..
		 * Mark the slot as having hosted an interrupted RPC call.
		 */
		slot->interrupted = 1;
		goto out;
	case -NFS4ERR_DELAY:
		/* The server detected a resend of the RPC call and
		 * returned NFS4ERR_DELAY as per Section 2.10.6.2
		 * of RFC5661.
		 */
		dprintk("%s: slot=%u seq=%u: Operation in progress\n",
			__func__,
			slot->slot_nr,
			slot->seq_nr);
		goto out_retry;
	case -NFS4ERR_BADSLOT:
		/*
		 * The slot id we used was probably retired. Try again
		 * using a different slot id.
		 */
		goto retry_nowait;
	case -NFS4ERR_SEQ_MISORDERED:
		/*
		 * Was the last operation on this sequence interrupted?
		 * If so, retry after bumping the sequence number.
		 */
		if (interrupted) {
			++slot->seq_nr;
			goto retry_nowait;
		}
		/*
		 * Could this slot have been previously retired?
		 * If so, then the server may be expecting seq_nr = 1!
		 */
		if (slot->seq_nr != 1) {
			slot->seq_nr = 1;
			goto retry_nowait;
		}
		break;
	case -NFS4ERR_SEQ_FALSE_RETRY:
		++slot->seq_nr;
		goto retry_nowait;
	default:
		/* Just update the slot sequence no. */
		++slot->seq_nr;
	}
out:
	/* The session may be reset by one of the error handlers. */
	dprintk("%s: Error %d free the slot \n", __func__, res->sr_status);
	nfs41_sequence_free_slot(res);
out_noaction:
	return ret;
retry_nowait:
	if (rpc_restart_call_prepare(task)) {
		task->tk_status = 0;
		ret = 0;
	}
	goto out;
out_retry:
	if (!rpc_restart_call(task))
		goto out;
	rpc_delay(task, NFS4_POLL_RETRY_MAX);
	return 0;
}
EXPORT_SYMBOL_GPL(nfs41_sequence_done);

static int nfs4_sequence_done(struct rpc_task *task,
			       struct nfs4_sequence_res *res)
{
	if (res->sr_slot == NULL)
		return 1;
	if (!res->sr_slot->table->session)
		return nfs40_sequence_done(task, res);
	return nfs41_sequence_done(task, res);
}

int nfs41_setup_sequence(struct nfs4_session *session,
				struct nfs4_sequence_args *args,
				struct nfs4_sequence_res *res,
				struct rpc_task *task)
{
	struct nfs4_slot *slot;
	struct nfs4_slot_table *tbl;

	dprintk("--> %s\n", __func__);
	/* slot already allocated? */
	if (res->sr_slot != NULL)
		goto out_success;

	tbl = &session->fc_slot_table;

	task->tk_timeout = 0;

	spin_lock(&tbl->slot_tbl_lock);
	if (test_bit(NFS4_SLOT_TBL_DRAINING, &tbl->slot_tbl_state) &&
	    !args->sa_privileged) {
		/* The state manager will wait until the slot table is empty */
		dprintk("%s session is draining\n", __func__);
		goto out_sleep;
	}

	slot = nfs4_alloc_slot(tbl);
	if (IS_ERR(slot)) {
		/* If out of memory, try again in 1/4 second */
		if (slot == ERR_PTR(-ENOMEM))
			task->tk_timeout = HZ >> 2;
		dprintk("<-- %s: no free slots\n", __func__);
		goto out_sleep;
	}
	spin_unlock(&tbl->slot_tbl_lock);

	args->sa_slot = slot;

	dprintk("<-- %s slotid=%u seqid=%u\n", __func__,
			slot->slot_nr, slot->seq_nr);

	res->sr_slot = slot;
	res->sr_timestamp = jiffies;
	res->sr_status_flags = 0;
	/*
	 * sr_status is only set in decode_sequence, and so will remain
	 * set to 1 if an rpc level failure occurs.
	 */
	res->sr_status = 1;
	trace_nfs4_setup_sequence(session, args);
out_success:
	rpc_call_start(task);
	return 0;
out_sleep:
	/* Privileged tasks are queued with top priority */
	if (args->sa_privileged)
		rpc_sleep_on_priority(&tbl->slot_tbl_waitq, task,
				NULL, RPC_PRIORITY_PRIVILEGED);
	else
		rpc_sleep_on(&tbl->slot_tbl_waitq, task, NULL);
	spin_unlock(&tbl->slot_tbl_lock);
	return -EAGAIN;
}
EXPORT_SYMBOL_GPL(nfs41_setup_sequence);

static int nfs4_setup_sequence(const struct nfs_server *server,
			       struct nfs4_sequence_args *args,
			       struct nfs4_sequence_res *res,
			       struct rpc_task *task)
{
	struct nfs4_session *session = nfs4_get_session(server);
	int ret = 0;

	if (!session)
		return nfs40_setup_sequence(server, args, res, task);

	dprintk("--> %s clp %p session %p sr_slot %u\n",
		__func__, session->clp, session, res->sr_slot ?
			res->sr_slot->slot_nr : NFS4_NO_SLOT);

	ret = nfs41_setup_sequence(session, args, res, task);

	dprintk("<-- %s status=%d\n", __func__, ret);
	return ret;
}

static void nfs41_call_sync_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs4_call_sync_data *data = calldata;
	struct nfs4_session *session = nfs4_get_session(data->seq_server);

	dprintk("--> %s data->seq_server %p\n", __func__, data->seq_server);

	nfs41_setup_sequence(session, data->seq_args, data->seq_res, task);
}

static void nfs41_call_sync_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_call_sync_data *data = calldata;

	nfs41_sequence_done(task, data->seq_res);
}

static const struct rpc_call_ops nfs41_call_sync_ops = {
	.rpc_call_prepare = nfs41_call_sync_prepare,
	.rpc_call_done = nfs41_call_sync_done,
};

#else	/* !CONFIG_NFS_V4_1 */

static int nfs4_setup_sequence(const struct nfs_server *server,
			       struct nfs4_sequence_args *args,
			       struct nfs4_sequence_res *res,
			       struct rpc_task *task)
{
	return nfs40_setup_sequence(server, args, res, task);
}

static int nfs4_sequence_done(struct rpc_task *task,
			       struct nfs4_sequence_res *res)
{
	return nfs40_sequence_done(task, res);
}

#endif	/* !CONFIG_NFS_V4_1 */

static void nfs40_call_sync_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs4_call_sync_data *data = calldata;
	nfs4_setup_sequence(data->seq_server,
				data->seq_args, data->seq_res, task);
}

static void nfs40_call_sync_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_call_sync_data *data = calldata;
	nfs4_sequence_done(task, data->seq_res);
}

static const struct rpc_call_ops nfs40_call_sync_ops = {
	.rpc_call_prepare = nfs40_call_sync_prepare,
	.rpc_call_done = nfs40_call_sync_done,
};

static int nfs4_call_sync_sequence(struct rpc_clnt *clnt,
				   struct nfs_server *server,
				   struct rpc_message *msg,
				   struct nfs4_sequence_args *args,
				   struct nfs4_sequence_res *res)
{
	int ret;
	struct rpc_task *task;
	struct nfs_client *clp = server->nfs_client;
	struct nfs4_call_sync_data data = {
		.seq_server = server,
		.seq_args = args,
		.seq_res = res,
	};
	struct rpc_task_setup task_setup = {
		.rpc_client = clnt,
		.rpc_message = msg,
		.callback_ops = clp->cl_mvops->call_sync_ops,
		.callback_data = &data
	};

	task = rpc_run_task(&task_setup);
	if (IS_ERR(task))
		ret = PTR_ERR(task);
	else {
		ret = task->tk_status;
		rpc_put_task(task);
	}
	return ret;
}

int nfs4_call_sync(struct rpc_clnt *clnt,
		   struct nfs_server *server,
		   struct rpc_message *msg,
		   struct nfs4_sequence_args *args,
		   struct nfs4_sequence_res *res,
		   int cache_reply)
{
	nfs4_init_sequence(args, res, cache_reply);
	return nfs4_call_sync_sequence(clnt, server, msg, args, res);
}

static void update_changeattr(struct inode *dir, struct nfs4_change_info *cinfo)
{
	struct nfs_inode *nfsi = NFS_I(dir);

	spin_lock(&dir->i_lock);
	nfsi->cache_validity |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_DATA;
	if (!cinfo->atomic || cinfo->before != dir->i_version)
		nfs_force_lookup_revalidate(dir);
	dir->i_version = cinfo->after;
	nfs_fscache_invalidate(dir);
	spin_unlock(&dir->i_lock);
}

struct nfs4_opendata {
	struct kref kref;
	struct nfs_openargs o_arg;
	struct nfs_openres o_res;
	struct nfs_open_confirmargs c_arg;
	struct nfs_open_confirmres c_res;
	struct nfs4_string owner_name;
	struct nfs4_string group_name;
	struct nfs_fattr f_attr;
	struct nfs4_label *f_label;
	struct dentry *dir;
	struct dentry *dentry;
	struct nfs4_state_owner *owner;
	struct nfs4_state *state;
	struct iattr attrs;
	unsigned long timestamp;
	unsigned int rpc_done : 1;
	unsigned int file_created : 1;
	unsigned int is_recover : 1;
	int rpc_status;
	int cancelled;
};

static bool nfs4_clear_cap_atomic_open_v1(struct nfs_server *server,
		int err, struct nfs4_exception *exception)
{
	if (err != -EINVAL)
		return false;
	if (!(server->caps & NFS_CAP_ATOMIC_OPEN_V1))
		return false;
	server->caps &= ~NFS_CAP_ATOMIC_OPEN_V1;
	exception->retry = 1;
	return true;
}

static enum open_claim_type4
nfs4_map_atomic_open_claim(struct nfs_server *server,
		enum open_claim_type4 claim)
{
	if (server->caps & NFS_CAP_ATOMIC_OPEN_V1)
		return claim;
	switch (claim) {
	default:
		return claim;
	case NFS4_OPEN_CLAIM_FH:
		return NFS4_OPEN_CLAIM_NULL;
	case NFS4_OPEN_CLAIM_DELEG_CUR_FH:
		return NFS4_OPEN_CLAIM_DELEGATE_CUR;
	case NFS4_OPEN_CLAIM_DELEG_PREV_FH:
		return NFS4_OPEN_CLAIM_DELEGATE_PREV;
	}
}

static void nfs4_init_opendata_res(struct nfs4_opendata *p)
{
	p->o_res.f_attr = &p->f_attr;
	p->o_res.f_label = p->f_label;
	p->o_res.seqid = p->o_arg.seqid;
	p->c_res.seqid = p->c_arg.seqid;
	p->o_res.server = p->o_arg.server;
	p->o_res.access_request = p->o_arg.access;
	nfs_fattr_init(&p->f_attr);
	nfs_fattr_init_names(&p->f_attr, &p->owner_name, &p->group_name);
}

static struct nfs4_opendata *nfs4_opendata_alloc(struct dentry *dentry,
		struct nfs4_state_owner *sp, fmode_t fmode, int flags,
		const struct iattr *attrs,
		struct nfs4_label *label,
		enum open_claim_type4 claim,
		gfp_t gfp_mask)
{
	struct dentry *parent = dget_parent(dentry);
	struct inode *dir = parent->d_inode;
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs4_opendata *p;

	p = kzalloc(sizeof(*p), gfp_mask);
	if (p == NULL)
		goto err;

	p->f_label = nfs4_label_alloc(server, gfp_mask);
	if (IS_ERR(p->f_label))
		goto err_free_p;

	p->o_arg.seqid = nfs_alloc_seqid(&sp->so_seqid, gfp_mask);
	if (p->o_arg.seqid == NULL)
		goto err_free_label;
	nfs_sb_active(dentry->d_sb);
	p->dentry = dget(dentry);
	p->dir = parent;
	p->owner = sp;
	atomic_inc(&sp->so_count);
	p->o_arg.open_flags = flags;
	p->o_arg.fmode = fmode & (FMODE_READ|FMODE_WRITE);
	/* don't put an ACCESS op in OPEN compound if O_EXCL, because ACCESS
	 * will return permission denied for all bits until close */
	if (!(flags & O_EXCL)) {
		/* ask server to check for all possible rights as results
		 * are cached */
		p->o_arg.access = NFS4_ACCESS_READ | NFS4_ACCESS_MODIFY |
				  NFS4_ACCESS_EXTEND | NFS4_ACCESS_EXECUTE;
	}
	p->o_arg.clientid = server->nfs_client->cl_clientid;
	p->o_arg.id.create_time = ktime_to_ns(sp->so_seqid.create_time);
	p->o_arg.id.uniquifier = sp->so_seqid.owner_id;
	p->o_arg.name = &dentry->d_name;
	p->o_arg.server = server;
	p->o_arg.bitmask = nfs4_bitmask(server, label);
	p->o_arg.open_bitmap = &nfs4_fattr_bitmap[0];
	p->o_arg.label = label;
	p->o_arg.claim = nfs4_map_atomic_open_claim(server, claim);
	switch (p->o_arg.claim) {
	case NFS4_OPEN_CLAIM_NULL:
	case NFS4_OPEN_CLAIM_DELEGATE_CUR:
	case NFS4_OPEN_CLAIM_DELEGATE_PREV:
		p->o_arg.fh = NFS_FH(dir);
		break;
	case NFS4_OPEN_CLAIM_PREVIOUS:
	case NFS4_OPEN_CLAIM_FH:
	case NFS4_OPEN_CLAIM_DELEG_CUR_FH:
	case NFS4_OPEN_CLAIM_DELEG_PREV_FH:
		p->o_arg.fh = NFS_FH(dentry->d_inode);
	}
	if (attrs != NULL && attrs->ia_valid != 0) {
		__u32 verf[2];

		p->o_arg.u.attrs = &p->attrs;
		memcpy(&p->attrs, attrs, sizeof(p->attrs));

		verf[0] = jiffies;
		verf[1] = current->pid;
		memcpy(p->o_arg.u.verifier.data, verf,
				sizeof(p->o_arg.u.verifier.data));
	}
	p->c_arg.fh = &p->o_res.fh;
	p->c_arg.stateid = &p->o_res.stateid;
	p->c_arg.seqid = p->o_arg.seqid;
	nfs4_init_opendata_res(p);
	kref_init(&p->kref);
	return p;

err_free_label:
	nfs4_label_free(p->f_label);
err_free_p:
	kfree(p);
err:
	dput(parent);
	return NULL;
}

static void nfs4_opendata_free(struct kref *kref)
{
	struct nfs4_opendata *p = container_of(kref,
			struct nfs4_opendata, kref);
	struct super_block *sb = p->dentry->d_sb;

	nfs_free_seqid(p->o_arg.seqid);
	if (p->state != NULL)
		nfs4_put_open_state(p->state);
	nfs4_put_state_owner(p->owner);

	nfs4_label_free(p->f_label);

	dput(p->dir);
	dput(p->dentry);
	nfs_sb_deactive(sb);
	nfs_fattr_free_names(&p->f_attr);
	kfree(p->f_attr.mdsthreshold);
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

	if (open_mode & (O_EXCL|O_TRUNC))
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
	if (delegation == NULL)
		return 0;
	if ((delegation->type & fmode) != fmode)
		return 0;
	if (test_bit(NFS_DELEGATION_NEED_RECLAIM, &delegation->flags))
		return 0;
	if (test_bit(NFS_DELEGATION_RETURNING, &delegation->flags))
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

static void nfs_test_and_clear_all_open_stateid(struct nfs4_state *state)
{
	struct nfs_client *clp = state->owner->so_server->nfs_client;
	bool need_recover = false;

	if (test_and_clear_bit(NFS_O_RDONLY_STATE, &state->flags) && state->n_rdonly)
		need_recover = true;
	if (test_and_clear_bit(NFS_O_WRONLY_STATE, &state->flags) && state->n_wronly)
		need_recover = true;
	if (test_and_clear_bit(NFS_O_RDWR_STATE, &state->flags) && state->n_rdwr)
		need_recover = true;
	if (need_recover)
		nfs4_state_mark_reclaim_nograce(clp, state);
}

static bool nfs_need_update_open_stateid(struct nfs4_state *state,
		nfs4_stateid *stateid)
{
	if (test_and_set_bit(NFS_OPEN_STATE, &state->flags) == 0)
		return true;
	if (!nfs4_stateid_match_other(stateid, &state->open_stateid)) {
		nfs_test_and_clear_all_open_stateid(state);
		return true;
	}
	if (nfs4_stateid_is_newer(stateid, &state->open_stateid))
		return true;
	return false;
}

static void nfs_clear_open_stateid_locked(struct nfs4_state *state,
		nfs4_stateid *stateid, fmode_t fmode)
{
	clear_bit(NFS_O_RDWR_STATE, &state->flags);
	switch (fmode & (FMODE_READ|FMODE_WRITE)) {
	case FMODE_WRITE:
		clear_bit(NFS_O_RDONLY_STATE, &state->flags);
		break;
	case FMODE_READ:
		clear_bit(NFS_O_WRONLY_STATE, &state->flags);
		break;
	case 0:
		clear_bit(NFS_O_RDONLY_STATE, &state->flags);
		clear_bit(NFS_O_WRONLY_STATE, &state->flags);
		clear_bit(NFS_OPEN_STATE, &state->flags);
	}
	if (stateid == NULL)
		return;
	if (!nfs_need_update_open_stateid(state, stateid))
		return;
	if (test_bit(NFS_DELEGATED_STATE, &state->flags) == 0)
		nfs4_stateid_copy(&state->stateid, stateid);
	nfs4_stateid_copy(&state->open_stateid, stateid);
}

static void nfs_clear_open_stateid(struct nfs4_state *state, nfs4_stateid *stateid, fmode_t fmode)
{
	write_seqlock(&state->seqlock);
	nfs_clear_open_stateid_locked(state, stateid, fmode);
	write_sequnlock(&state->seqlock);
	if (test_bit(NFS_STATE_RECLAIM_NOGRACE, &state->flags))
		nfs4_schedule_state_manager(state->owner->so_server->nfs_client);
}

static void nfs_set_open_stateid_locked(struct nfs4_state *state, nfs4_stateid *stateid, fmode_t fmode)
{
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
	if (!nfs_need_update_open_stateid(state, stateid))
		return;
	if (test_bit(NFS_DELEGATED_STATE, &state->flags) == 0)
		nfs4_stateid_copy(&state->stateid, stateid);
	nfs4_stateid_copy(&state->open_stateid, stateid);
}

static void __update_open_stateid(struct nfs4_state *state, nfs4_stateid *open_stateid, const nfs4_stateid *deleg_stateid, fmode_t fmode)
{
	/*
	 * Protect the call to nfs4_state_set_mode_locked and
	 * serialise the stateid update
	 */
	write_seqlock(&state->seqlock);
	if (deleg_stateid != NULL) {
		nfs4_stateid_copy(&state->stateid, deleg_stateid);
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
	if (rcu_dereference(nfsi->delegation) != deleg_cur ||
	   test_bit(NFS_DELEGATION_RETURNING, &deleg_cur->flags) ||
	    (deleg_cur->type & fmode) != fmode)
		goto no_delegation_unlock;

	if (delegation == NULL)
		delegation = &deleg_cur->stateid;
	else if (!nfs4_stateid_match(&deleg_cur->stateid, delegation))
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
	if (test_bit(NFS_STATE_RECLAIM_NOGRACE, &state->flags))
		nfs4_schedule_state_manager(state->owner->so_server->nfs_client);

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
	nfs4_inode_return_delegation(inode);
}

static struct nfs4_state *nfs4_try_open_cached(struct nfs4_opendata *opendata)
{
	struct nfs4_state *state = opendata->state;
	struct nfs_inode *nfsi = NFS_I(state->inode);
	struct nfs_delegation *delegation;
	int open_mode = opendata->o_arg.open_flags;
	fmode_t fmode = opendata->o_arg.fmode;
	nfs4_stateid stateid;
	int ret = -EAGAIN;

	for (;;) {
		spin_lock(&state->owner->so_lock);
		if (can_open_cached(state, fmode, open_mode)) {
			update_open_stateflags(state, fmode);
			spin_unlock(&state->owner->so_lock);
			goto out_return_state;
		}
		spin_unlock(&state->owner->so_lock);
		rcu_read_lock();
		delegation = rcu_dereference(nfsi->delegation);
		if (!can_open_delegated(delegation, fmode)) {
			rcu_read_unlock();
			break;
		}
		/* Save the delegation */
		nfs4_stateid_copy(&stateid, &delegation->stateid);
		rcu_read_unlock();
		nfs_release_seqid(opendata->o_arg.seqid);
		if (!opendata->is_recover) {
			ret = nfs_may_open(state->inode, state->owner->so_cred, open_mode);
			if (ret != 0)
				goto out;
		}
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

static void
nfs4_opendata_check_deleg(struct nfs4_opendata *data, struct nfs4_state *state)
{
	struct nfs_client *clp = NFS_SERVER(state->inode)->nfs_client;
	struct nfs_delegation *delegation;
	int delegation_flags = 0;

	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(state->inode)->delegation);
	if (delegation)
		delegation_flags = delegation->flags;
	rcu_read_unlock();
	if (data->o_arg.claim == NFS4_OPEN_CLAIM_DELEGATE_CUR) {
		pr_err_ratelimited("NFS: Broken NFSv4 server %s is "
				   "returning a delegation for "
				   "OPEN(CLAIM_DELEGATE_CUR)\n",
				   clp->cl_hostname);
	} else if ((delegation_flags & 1UL<<NFS_DELEGATION_NEED_RECLAIM) == 0)
		nfs_inode_set_delegation(state->inode,
					 data->owner->so_cred,
					 &data->o_res);
	else
		nfs_inode_reclaim_delegation(state->inode,
					     data->owner->so_cred,
					     &data->o_res);
}

/*
 * Check the inode attributes against the CLAIM_PREVIOUS returned attributes
 * and update the nfs4_state.
 */
static struct nfs4_state *
_nfs4_opendata_reclaim_to_nfs4_state(struct nfs4_opendata *data)
{
	struct inode *inode = data->state->inode;
	struct nfs4_state *state = data->state;
	int ret;

	if (!data->rpc_done) {
		if (data->rpc_status) {
			ret = data->rpc_status;
			goto err;
		}
		/* cached opens have already been processed */
		goto update;
	}

	ret = nfs_refresh_inode(inode, &data->f_attr);
	if (ret)
		goto err;

	if (data->o_res.delegation_type != 0)
		nfs4_opendata_check_deleg(data, state);
update:
	update_open_stateid(state, &data->o_res.stateid, NULL,
			    data->o_arg.fmode);
	atomic_inc(&state->count);

	return state;
err:
	return ERR_PTR(ret);

}

static struct nfs4_state *
_nfs4_opendata_to_nfs4_state(struct nfs4_opendata *data)
{
	struct inode *inode;
	struct nfs4_state *state = NULL;
	int ret;

	if (!data->rpc_done) {
		state = nfs4_try_open_cached(data);
		goto out;
	}

	ret = -EAGAIN;
	if (!(data->f_attr.valid & NFS_ATTR_FATTR))
		goto err;
	inode = nfs_fhget(data->dir->d_sb, &data->o_res.fh, &data->f_attr, data->f_label);
	ret = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto err;
	ret = -ENOMEM;
	state = nfs4_get_open_state(inode, data->owner);
	if (state == NULL)
		goto err_put_inode;
	if (data->o_res.delegation_type != 0)
		nfs4_opendata_check_deleg(data, state);
	update_open_stateid(state, &data->o_res.stateid, NULL,
			data->o_arg.fmode);
	iput(inode);
out:
	nfs_release_seqid(data->o_arg.seqid);
	return state;
err_put_inode:
	iput(inode);
err:
	return ERR_PTR(ret);
}

static struct nfs4_state *
nfs4_opendata_to_nfs4_state(struct nfs4_opendata *data)
{
	if (data->o_arg.claim == NFS4_OPEN_CLAIM_PREVIOUS)
		return _nfs4_opendata_reclaim_to_nfs4_state(data);
	return _nfs4_opendata_to_nfs4_state(data);
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

static struct nfs4_opendata *nfs4_open_recoverdata_alloc(struct nfs_open_context *ctx,
		struct nfs4_state *state, enum open_claim_type4 claim)
{
	struct nfs4_opendata *opendata;

	opendata = nfs4_opendata_alloc(ctx->dentry, state->owner, 0, 0,
			NULL, NULL, claim, GFP_NOFS);
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
	nfs4_close_state(newstate, fmode);
	*res = newstate;
	return 0;
}

static int nfs4_open_recover(struct nfs4_opendata *opendata, struct nfs4_state *state)
{
	struct nfs4_state *newstate;
	int ret;

	/* Don't trigger recovery in nfs_test_and_clear_all_open_stateid */
	clear_bit(NFS_O_RDWR_STATE, &state->flags);
	clear_bit(NFS_O_WRONLY_STATE, &state->flags);
	clear_bit(NFS_O_RDONLY_STATE, &state->flags);
	/* memory barrier prior to reading state->n_* */
	clear_bit(NFS_DELEGATED_STATE, &state->flags);
	clear_bit(NFS_OPEN_STATE, &state->flags);
	smp_rmb();
	if (state->n_rdwr != 0) {
		ret = nfs4_open_recover_helper(opendata, FMODE_READ|FMODE_WRITE, &newstate);
		if (ret != 0)
			return ret;
		if (newstate != state)
			return -ESTALE;
	}
	if (state->n_wronly != 0) {
		ret = nfs4_open_recover_helper(opendata, FMODE_WRITE, &newstate);
		if (ret != 0)
			return ret;
		if (newstate != state)
			return -ESTALE;
	}
	if (state->n_rdonly != 0) {
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
	    !nfs4_stateid_match(&state->stateid, &state->open_stateid)) {
		write_seqlock(&state->seqlock);
		if (test_bit(NFS_DELEGATED_STATE, &state->flags) == 0)
			nfs4_stateid_copy(&state->stateid, &state->open_stateid);
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

	opendata = nfs4_open_recoverdata_alloc(ctx, state,
			NFS4_OPEN_CLAIM_PREVIOUS);
	if (IS_ERR(opendata))
		return PTR_ERR(opendata);
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
		trace_nfs4_open_reclaim(ctx, 0, err);
		if (nfs4_clear_cap_atomic_open_v1(server, err, &exception))
			continue;
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
		return -EAGAIN;
	ret = nfs4_do_open_reclaim(ctx, state);
	put_nfs_open_context(ctx);
	return ret;
}

static int nfs4_handle_delegation_recall_error(struct nfs_server *server, struct nfs4_state *state, const nfs4_stateid *stateid, int err)
{
	switch (err) {
		default:
			printk(KERN_ERR "NFS: %s: unhandled error "
					"%d.\n", __func__, err);
		case 0:
		case -ENOENT:
		case -ESTALE:
			break;
		case -NFS4ERR_BADSESSION:
		case -NFS4ERR_BADSLOT:
		case -NFS4ERR_BAD_HIGH_SLOT:
		case -NFS4ERR_CONN_NOT_BOUND_TO_SESSION:
		case -NFS4ERR_DEADSESSION:
			set_bit(NFS_DELEGATED_STATE, &state->flags);
			nfs4_schedule_session_recovery(server->nfs_client->cl_session, err);
			return -EAGAIN;
		case -NFS4ERR_STALE_CLIENTID:
		case -NFS4ERR_STALE_STATEID:
			set_bit(NFS_DELEGATED_STATE, &state->flags);
		case -NFS4ERR_EXPIRED:
			/* Don't recall a delegation if it was lost */
			nfs4_schedule_lease_recovery(server->nfs_client);
			return -EAGAIN;
		case -NFS4ERR_MOVED:
			nfs4_schedule_migration_recovery(server);
			return -EAGAIN;
		case -NFS4ERR_LEASE_MOVED:
			nfs4_schedule_lease_moved_recovery(server->nfs_client);
			return -EAGAIN;
		case -NFS4ERR_DELEG_REVOKED:
		case -NFS4ERR_ADMIN_REVOKED:
		case -NFS4ERR_BAD_STATEID:
		case -NFS4ERR_OPENMODE:
			nfs_inode_find_state_and_recover(state->inode,
					stateid);
			nfs4_schedule_stateid_recovery(server, state);
			return -EAGAIN;
		case -NFS4ERR_DELAY:
		case -NFS4ERR_GRACE:
			set_bit(NFS_DELEGATED_STATE, &state->flags);
			ssleep(1);
			return -EAGAIN;
		case -ENOMEM:
		case -NFS4ERR_DENIED:
			/* kill_proc(fl->fl_pid, SIGLOST, 1); */
			return 0;
	}
	return err;
}

int nfs4_open_delegation_recall(struct nfs_open_context *ctx, struct nfs4_state *state, const nfs4_stateid *stateid)
{
	struct nfs_server *server = NFS_SERVER(state->inode);
	struct nfs4_opendata *opendata;
	int err;

	opendata = nfs4_open_recoverdata_alloc(ctx, state,
			NFS4_OPEN_CLAIM_DELEG_CUR_FH);
	if (IS_ERR(opendata))
		return PTR_ERR(opendata);
	nfs4_stateid_copy(&opendata->o_arg.u.delegation, stateid);
	err = nfs4_open_recover(opendata, state);
	nfs4_opendata_put(opendata);
	return nfs4_handle_delegation_recall_error(server, state, stateid, err);
}

static void nfs4_open_confirm_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs4_opendata *data = calldata;

	nfs40_setup_sequence(data->o_arg.server, &data->c_arg.seq_args,
				&data->c_res.seq_res, task);
}

static void nfs4_open_confirm_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_opendata *data = calldata;

	nfs40_sequence_done(task, &data->c_res.seq_res);

	data->rpc_status = task->tk_status;
	if (data->rpc_status == 0) {
		nfs4_stateid_copy(&data->o_res.stateid, &data->c_res.stateid);
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
		nfs4_close_state(state, data->o_arg.fmode);
out_free:
	nfs4_opendata_put(data);
}

static const struct rpc_call_ops nfs4_open_confirm_ops = {
	.rpc_call_prepare = nfs4_open_confirm_prepare,
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

	nfs4_init_sequence(&data->c_arg.seq_args, &data->c_res.seq_res, 1);
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
	struct nfs_client *clp = sp->so_server->nfs_client;

	if (nfs_wait_on_sequence(data->o_arg.seqid, task) != 0)
		goto out_wait;
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
		if (data->o_arg.claim != NFS4_OPEN_CLAIM_DELEGATE_CUR &&
		    data->o_arg.claim != NFS4_OPEN_CLAIM_DELEG_CUR_FH &&
		    can_open_delegated(delegation, data->o_arg.fmode))
			goto unlock_no_action;
		rcu_read_unlock();
	}
	/* Update client id. */
	data->o_arg.clientid = clp->cl_clientid;
	switch (data->o_arg.claim) {
	case NFS4_OPEN_CLAIM_PREVIOUS:
	case NFS4_OPEN_CLAIM_DELEG_CUR_FH:
	case NFS4_OPEN_CLAIM_DELEG_PREV_FH:
		data->o_arg.open_bitmap = &nfs4_open_noattr_bitmap[0];
	case NFS4_OPEN_CLAIM_FH:
		task->tk_msg.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_OPEN_NOATTR];
		nfs_copy_fh(&data->o_res.fh, data->o_arg.fh);
	}
	data->timestamp = jiffies;
	if (nfs4_setup_sequence(data->o_arg.server,
				&data->o_arg.seq_args,
				&data->o_res.seq_res,
				task) != 0)
		nfs_release_seqid(data->o_arg.seqid);

	/* Set the create mode (note dependency on the session type) */
	data->o_arg.createmode = NFS4_CREATE_UNCHECKED;
	if (data->o_arg.open_flags & O_EXCL) {
		data->o_arg.createmode = NFS4_CREATE_EXCLUSIVE;
		if (nfs4_has_persistent_session(clp))
			data->o_arg.createmode = NFS4_CREATE_GUARDED;
		else if (clp->cl_mvops->minor_version > 0)
			data->o_arg.createmode = NFS4_CREATE_EXCLUSIVE4_1;
	}
	return;
unlock_no_action:
	rcu_read_unlock();
out_no_action:
	task->tk_action = NULL;
out_wait:
	nfs4_sequence_done(task, &data->o_res.seq_res);
}

static void nfs4_open_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_opendata *data = calldata;

	data->rpc_status = task->tk_status;

	if (!nfs4_sequence_done(task, &data->o_res.seq_res))
		return;

	if (task->tk_status == 0) {
		if (data->o_res.f_attr->valid & NFS_ATTR_FATTR_TYPE) {
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
		nfs4_close_state(state, data->o_arg.fmode);
out_free:
	nfs4_opendata_put(data);
}

static const struct rpc_call_ops nfs4_open_ops = {
	.rpc_call_prepare = nfs4_open_prepare,
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

	nfs4_init_sequence(&o_arg->seq_args, &o_res->seq_res, 1);
	kref_get(&data->kref);
	data->rpc_done = 0;
	data->rpc_status = 0;
	data->cancelled = 0;
	data->is_recover = 0;
	if (isrecover) {
		nfs4_set_sequence_privileged(&o_arg->seq_args);
		data->is_recover = 1;
	}
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

	nfs_fattr_map_and_free_names(NFS_SERVER(dir), &data->f_attr);

	if (o_res->rflags & NFS4_OPEN_RESULT_CONFIRM) {
		status = _nfs4_proc_open_confirm(data);
		if (status != 0)
			return status;
	}

	return status;
}

/*
 * Additional permission checks in order to distinguish between an
 * open for read, and an open for execute. This works around the
 * fact that NFSv4 OPEN treats read and execute permissions as being
 * the same.
 * Note that in the non-execute case, we want to turn off permission
 * checking if we just created a new file (POSIX open() semantics).
 */
static int nfs4_opendata_access(struct rpc_cred *cred,
				struct nfs4_opendata *opendata,
				struct nfs4_state *state, fmode_t fmode,
				int openflags)
{
	struct nfs_access_entry cache;
	u32 mask;

	/* access call failed or for some reason the server doesn't
	 * support any access modes -- defer access call until later */
	if (opendata->o_res.access_supported == 0)
		return 0;

	mask = 0;
	/*
	 * Use openflags to check for exec, because fmode won't
	 * always have FMODE_EXEC set when file open for exec.
	 */
	if (openflags & __FMODE_EXEC) {
		/* ONLY check for exec rights */
		mask = MAY_EXEC;
	} else if ((fmode & FMODE_READ) && !opendata->file_created)
		mask = MAY_READ;

	cache.cred = cred;
	cache.jiffies = jiffies;
	nfs_access_set_mask(&cache, opendata->o_res.access_result);
	nfs_access_add_cache(state->inode, &cache);

	if ((mask & ~cache.mask & (MAY_READ | MAY_EXEC)) == 0)
		return 0;

	/* even though OPEN succeeded, access is denied. Close the file */
	nfs4_close_state(state, fmode);
	return -EACCES;
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
	if (!data->rpc_done)
		return status;
	if (status != 0) {
		if (status == -NFS4ERR_BADNAME &&
				!(o_arg->open_flags & O_CREAT))
			return -ENOENT;
		return status;
	}

	nfs_fattr_map_and_free_names(server, &data->f_attr);

	if (o_arg->open_flags & O_CREAT) {
		update_changeattr(dir, &o_res->cinfo);
		if (o_arg->open_flags & O_EXCL)
			data->file_created = 1;
		else if (o_res->cinfo.before != o_res->cinfo.after)
			data->file_created = 1;
	}
	if ((o_res->rflags & NFS4_OPEN_RESULT_LOCKTYPE_POSIX) == 0)
		server->caps &= ~NFS_CAP_POSIX_LOCK;
	if(o_res->rflags & NFS4_OPEN_RESULT_CONFIRM) {
		status = _nfs4_proc_open_confirm(data);
		if (status != 0)
			return status;
	}
	if (!(o_res->f_attr->valid & NFS_ATTR_FATTR))
		nfs4_proc_getattr(server, &o_res->fh, o_res->f_attr, o_res->f_label);
	return 0;
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

	opendata = nfs4_open_recoverdata_alloc(ctx, state,
			NFS4_OPEN_CLAIM_FH);
	if (IS_ERR(opendata))
		return PTR_ERR(opendata);
	ret = nfs4_open_recover(opendata, state);
	if (ret == -ESTALE)
		d_drop(ctx->dentry);
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
		trace_nfs4_open_expired(ctx, 0, err);
		if (nfs4_clear_cap_atomic_open_v1(server, err, &exception))
			continue;
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
		return -EAGAIN;
	ret = nfs4_do_open_expired(ctx, state);
	put_nfs_open_context(ctx);
	return ret;
}

static void nfs_finish_clear_delegation_stateid(struct nfs4_state *state)
{
	nfs_remove_bad_delegation(state->inode);
	write_seqlock(&state->seqlock);
	nfs4_stateid_copy(&state->stateid, &state->open_stateid);
	write_sequnlock(&state->seqlock);
	clear_bit(NFS_DELEGATED_STATE, &state->flags);
}

static void nfs40_clear_delegation_stateid(struct nfs4_state *state)
{
	if (rcu_access_pointer(NFS_I(state->inode)->delegation) != NULL)
		nfs_finish_clear_delegation_stateid(state);
}

static int nfs40_open_expired(struct nfs4_state_owner *sp, struct nfs4_state *state)
{
	/* NFSv4.0 doesn't allow for delegation recovery on open expire */
	nfs40_clear_delegation_stateid(state);
	return nfs4_open_expired(sp, state);
}

#if defined(CONFIG_NFS_V4_1)
static void nfs41_check_delegation_stateid(struct nfs4_state *state)
{
	struct nfs_server *server = NFS_SERVER(state->inode);
	nfs4_stateid stateid;
	struct nfs_delegation *delegation;
	struct rpc_cred *cred;
	int status;

	/* Get the delegation credential for use by test/free_stateid */
	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(state->inode)->delegation);
	if (delegation == NULL) {
		rcu_read_unlock();
		return;
	}

	nfs4_stateid_copy(&stateid, &delegation->stateid);
	cred = get_rpccred(delegation->cred);
	rcu_read_unlock();
	status = nfs41_test_stateid(server, &stateid, cred);
	trace_nfs4_test_delegation_stateid(state, NULL, status);

	if (status != NFS_OK) {
		/* Free the stateid unless the server explicitly
		 * informs us the stateid is unrecognized. */
		if (status != -NFS4ERR_BAD_STATEID)
			nfs41_free_stateid(server, &stateid, cred);
		nfs_finish_clear_delegation_stateid(state);
	}

	put_rpccred(cred);
}

/**
 * nfs41_check_open_stateid - possibly free an open stateid
 *
 * @state: NFSv4 state for an inode
 *
 * Returns NFS_OK if recovery for this stateid is now finished.
 * Otherwise a negative NFS4ERR value is returned.
 */
static int nfs41_check_open_stateid(struct nfs4_state *state)
{
	struct nfs_server *server = NFS_SERVER(state->inode);
	nfs4_stateid *stateid = &state->open_stateid;
	struct rpc_cred *cred = state->owner->so_cred;
	int status;

	/* If a state reset has been done, test_stateid is unneeded */
	if ((test_bit(NFS_O_RDONLY_STATE, &state->flags) == 0) &&
	    (test_bit(NFS_O_WRONLY_STATE, &state->flags) == 0) &&
	    (test_bit(NFS_O_RDWR_STATE, &state->flags) == 0))
		return -NFS4ERR_BAD_STATEID;

	status = nfs41_test_stateid(server, stateid, cred);
	trace_nfs4_test_open_stateid(state, NULL, status);
	if (status != NFS_OK) {
		/* Free the stateid unless the server explicitly
		 * informs us the stateid is unrecognized. */
		if (status != -NFS4ERR_BAD_STATEID)
			nfs41_free_stateid(server, stateid, cred);

		clear_bit(NFS_O_RDONLY_STATE, &state->flags);
		clear_bit(NFS_O_WRONLY_STATE, &state->flags);
		clear_bit(NFS_O_RDWR_STATE, &state->flags);
		clear_bit(NFS_OPEN_STATE, &state->flags);
	}
	return status;
}

static int nfs41_open_expired(struct nfs4_state_owner *sp, struct nfs4_state *state)
{
	int status;

	nfs41_check_delegation_stateid(state);
	status = nfs41_check_open_stateid(state);
	if (status != NFS_OK)
		status = nfs4_open_expired(sp, state);
	return status;
}
#endif

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

static int _nfs4_open_and_get_state(struct nfs4_opendata *opendata,
		fmode_t fmode,
		int flags,
		struct nfs_open_context *ctx)
{
	struct nfs4_state_owner *sp = opendata->owner;
	struct nfs_server *server = sp->so_server;
	struct dentry *dentry;
	struct nfs4_state *state;
	unsigned int seq;
	int ret;

	seq = raw_seqcount_begin(&sp->so_reclaim_seqcount);

	ret = _nfs4_proc_open(opendata);
	if (ret != 0)
		goto out;

	state = nfs4_opendata_to_nfs4_state(opendata);
	ret = PTR_ERR(state);
	if (IS_ERR(state))
		goto out;
	if (server->caps & NFS_CAP_POSIX_LOCK)
		set_bit(NFS_STATE_POSIX_LOCKS, &state->flags);

	dentry = opendata->dentry;
	if (dentry->d_inode == NULL) {
		/* FIXME: Is this d_drop() ever needed? */
		d_drop(dentry);
		dentry = d_add_unique(dentry, igrab(state->inode));
		if (dentry == NULL) {
			dentry = opendata->dentry;
		} else if (dentry != ctx->dentry) {
			dput(ctx->dentry);
			ctx->dentry = dget(dentry);
		}
		nfs_set_verifier(dentry,
				nfs_save_change_attribute(opendata->dir->d_inode));
	}

	ret = nfs4_opendata_access(sp->so_cred, opendata, state, fmode, flags);
	if (ret != 0)
		goto out;

	ctx->state = state;
	if (dentry->d_inode == state->inode) {
		nfs_inode_attach_open_context(ctx);
		if (read_seqcount_retry(&sp->so_reclaim_seqcount, seq))
			nfs4_schedule_stateid_recovery(server, state);
	}
out:
	return ret;
}

/*
 * Returns a referenced nfs4_state
 */
static int _nfs4_do_open(struct inode *dir,
			struct nfs_open_context *ctx,
			int flags,
			struct iattr *sattr,
			struct nfs4_label *label,
			int *opened)
{
	struct nfs4_state_owner  *sp;
	struct nfs4_state     *state = NULL;
	struct nfs_server       *server = NFS_SERVER(dir);
	struct nfs4_opendata *opendata;
	struct dentry *dentry = ctx->dentry;
	struct rpc_cred *cred = ctx->cred;
	struct nfs4_threshold **ctx_th = &ctx->mdsthreshold;
	fmode_t fmode = ctx->mode & (FMODE_READ|FMODE_WRITE|FMODE_EXEC);
	enum open_claim_type4 claim = NFS4_OPEN_CLAIM_NULL;
	struct nfs4_label *olabel = NULL;
	int status;

	/* Protect against reboot recovery conflicts */
	status = -ENOMEM;
	sp = nfs4_get_state_owner(server, cred, GFP_KERNEL);
	if (sp == NULL) {
		dprintk("nfs4_do_open: nfs4_get_state_owner failed!\n");
		goto out_err;
	}
	status = nfs4_recover_expired_lease(server);
	if (status != 0)
		goto err_put_state_owner;
	if (dentry->d_inode != NULL)
		nfs4_return_incompatible_delegation(dentry->d_inode, fmode);
	status = -ENOMEM;
	if (dentry->d_inode)
		claim = NFS4_OPEN_CLAIM_FH;
	opendata = nfs4_opendata_alloc(dentry, sp, fmode, flags, sattr,
			label, claim, GFP_KERNEL);
	if (opendata == NULL)
		goto err_put_state_owner;

	if (label) {
		olabel = nfs4_label_alloc(server, GFP_KERNEL);
		if (IS_ERR(olabel)) {
			status = PTR_ERR(olabel);
			goto err_opendata_put;
		}
	}

	if (server->attr_bitmask[2] & FATTR4_WORD2_MDSTHRESHOLD) {
		if (!opendata->f_attr.mdsthreshold) {
			opendata->f_attr.mdsthreshold = pnfs_mdsthreshold_alloc();
			if (!opendata->f_attr.mdsthreshold)
				goto err_free_label;
		}
		opendata->o_arg.open_bitmap = &nfs4_pnfs_open_bitmap[0];
	}
	if (dentry->d_inode != NULL)
		opendata->state = nfs4_get_open_state(dentry->d_inode, sp);

	status = _nfs4_open_and_get_state(opendata, fmode, flags, ctx);
	if (status != 0)
		goto err_free_label;
	state = ctx->state;

	if ((opendata->o_arg.open_flags & O_EXCL) &&
	    (opendata->o_arg.createmode != NFS4_CREATE_GUARDED)) {
		nfs4_exclusive_attrset(opendata, sattr);

		nfs_fattr_init(opendata->o_res.f_attr);
		status = nfs4_do_setattr(state->inode, cred,
				opendata->o_res.f_attr, sattr,
				state, label, olabel);
		if (status == 0) {
			nfs_setattr_update_inode(state->inode, sattr);
			nfs_post_op_update_inode(state->inode, opendata->o_res.f_attr);
			nfs_setsecurity(state->inode, opendata->o_res.f_attr, olabel);
		}
	}
	if (opendata->file_created)
		*opened |= FILE_CREATED;

	if (pnfs_use_threshold(ctx_th, opendata->f_attr.mdsthreshold, server)) {
		*ctx_th = opendata->f_attr.mdsthreshold;
		opendata->f_attr.mdsthreshold = NULL;
	}

	nfs4_label_free(olabel);

	nfs4_opendata_put(opendata);
	nfs4_put_state_owner(sp);
	return 0;
err_free_label:
	nfs4_label_free(olabel);
err_opendata_put:
	nfs4_opendata_put(opendata);
err_put_state_owner:
	nfs4_put_state_owner(sp);
out_err:
	return status;
}


static struct nfs4_state *nfs4_do_open(struct inode *dir,
					struct nfs_open_context *ctx,
					int flags,
					struct iattr *sattr,
					struct nfs4_label *label,
					int *opened)
{
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs4_exception exception = { };
	struct nfs4_state *res;
	int status;

	do {
		status = _nfs4_do_open(dir, ctx, flags, sattr, label, opened);
		res = ctx->state;
		trace_nfs4_open_file(ctx, flags, status);
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
			pr_warn_ratelimited("NFS: v4 server %s "
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
		if (nfs4_clear_cap_atomic_open_v1(server, status, &exception))
			continue;
		res = ERR_PTR(nfs4_handle_exception(server,
					status, &exception));
	} while (exception.retry);
	return res;
}

static int _nfs4_do_setattr(struct inode *inode, struct rpc_cred *cred,
			    struct nfs_fattr *fattr, struct iattr *sattr,
			    struct nfs4_state *state, struct nfs4_label *ilabel,
			    struct nfs4_label *olabel)
{
	struct nfs_server *server = NFS_SERVER(inode);
        struct nfs_setattrargs  arg = {
                .fh             = NFS_FH(inode),
                .iap            = sattr,
		.server		= server,
		.bitmask = server->attr_bitmask,
		.label		= ilabel,
        };
        struct nfs_setattrres  res = {
		.fattr		= fattr,
		.label		= olabel,
		.server		= server,
        };
        struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_SETATTR],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
		.rpc_cred	= cred,
        };
	unsigned long timestamp = jiffies;
	fmode_t fmode;
	bool truncate;
	int status;

	arg.bitmask = nfs4_bitmask(server, ilabel);
	if (ilabel)
		arg.bitmask = nfs4_bitmask(server, olabel);

	nfs_fattr_init(fattr);

	/* Servers should only apply open mode checks for file size changes */
	truncate = (sattr->ia_valid & ATTR_SIZE) ? true : false;
	fmode = truncate ? FMODE_WRITE : FMODE_READ;

	if (nfs4_copy_delegation_stateid(&arg.stateid, inode, fmode)) {
		/* Use that stateid */
	} else if (truncate && state != NULL) {
		struct nfs_lockowner lockowner = {
			.l_owner = current->files,
			.l_pid = current->tgid,
		};
		if (!nfs4_valid_open_stateid(state))
			return -EBADF;
		if (nfs4_select_rw_stateid(&arg.stateid, state, FMODE_WRITE,
				&lockowner) == -EIO)
			return -EBADF;
	} else
		nfs4_stateid_copy(&arg.stateid, &zero_stateid);

	status = nfs4_call_sync(server->client, server, &msg, &arg.seq_args, &res.seq_res, 1);
	if (status == 0 && state != NULL)
		renew_lease(server, timestamp);
	return status;
}

static int nfs4_do_setattr(struct inode *inode, struct rpc_cred *cred,
			   struct nfs_fattr *fattr, struct iattr *sattr,
			   struct nfs4_state *state, struct nfs4_label *ilabel,
			   struct nfs4_label *olabel)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs4_exception exception = {
		.state = state,
		.inode = inode,
	};
	int err;
	do {
		err = _nfs4_do_setattr(inode, cred, fattr, sattr, state, ilabel, olabel);
		trace_nfs4_setattr(inode, err);
		switch (err) {
		case -NFS4ERR_OPENMODE:
			if (!(sattr->ia_valid & ATTR_SIZE)) {
				pr_warn_once("NFSv4: server %s is incorrectly "
						"applying open mode checks to "
						"a SETATTR that is not "
						"changing file size.\n",
						server->nfs_client->cl_hostname);
			}
			if (state && !(state->state & FMODE_WRITE)) {
				err = -EBADF;
				if (sattr->ia_valid & ATTR_OPEN)
					err = -EACCES;
				goto out;
			}
		}
		err = nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);
out:
	return err;
}

struct nfs4_closedata {
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
	struct super_block *sb = calldata->state->inode->i_sb;

	if (calldata->roc)
		pnfs_roc_release(calldata->state->inode);
	nfs4_put_open_state(calldata->state);
	nfs_free_seqid(calldata->arg.seqid);
	nfs4_put_state_owner(sp);
	nfs_sb_deactive(sb);
	kfree(calldata);
}

static void nfs4_close_done(struct rpc_task *task, void *data)
{
	struct nfs4_closedata *calldata = data;
	struct nfs4_state *state = calldata->state;
	struct nfs_server *server = NFS_SERVER(calldata->inode);
	nfs4_stateid *res_stateid = NULL;

	dprintk("%s: begin!\n", __func__);
	if (!nfs4_sequence_done(task, &calldata->res.seq_res))
		return;
	trace_nfs4_close(state, &calldata->arg, &calldata->res, task->tk_status);
        /* hmm. we are done with the inode, and in the process of freeing
	 * the state_owner. we keep this around to process errors
	 */
	switch (task->tk_status) {
		case 0:
			res_stateid = &calldata->res.stateid;
			if (calldata->arg.fmode == 0 && calldata->roc)
				pnfs_roc_set_barrier(state->inode,
						     calldata->roc_barrier);
			renew_lease(server, calldata->timestamp);
			break;
		case -NFS4ERR_ADMIN_REVOKED:
		case -NFS4ERR_STALE_STATEID:
		case -NFS4ERR_OLD_STATEID:
		case -NFS4ERR_BAD_STATEID:
		case -NFS4ERR_EXPIRED:
			if (calldata->arg.fmode == 0)
				break;
		default:
			if (nfs4_async_handle_error(task, server, state, NULL) == -EAGAIN) {
				rpc_restart_call_prepare(task);
				goto out_release;
			}
	}
	nfs_clear_open_stateid(state, res_stateid, calldata->arg.fmode);
out_release:
	nfs_release_seqid(calldata->arg.seqid);
	nfs_refresh_inode(calldata->inode, calldata->res.fattr);
	dprintk("%s: done, ret = %d!\n", __func__, task->tk_status);
}

static void nfs4_close_prepare(struct rpc_task *task, void *data)
{
	struct nfs4_closedata *calldata = data;
	struct nfs4_state *state = calldata->state;
	struct inode *inode = calldata->inode;
	bool is_rdonly, is_wronly, is_rdwr;
	int call_close = 0;

	dprintk("%s: begin!\n", __func__);
	if (nfs_wait_on_sequence(calldata->arg.seqid, task) != 0)
		goto out_wait;

	task->tk_msg.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_OPEN_DOWNGRADE];
	spin_lock(&state->owner->so_lock);
	is_rdwr = test_bit(NFS_O_RDWR_STATE, &state->flags);
	is_rdonly = test_bit(NFS_O_RDONLY_STATE, &state->flags);
	is_wronly = test_bit(NFS_O_WRONLY_STATE, &state->flags);
	/* Calculate the change in open mode */
	calldata->arg.fmode = 0;
	if (state->n_rdwr == 0) {
		if (state->n_rdonly == 0)
			call_close |= is_rdonly;
		else if (is_rdonly)
			calldata->arg.fmode |= FMODE_READ;
		if (state->n_wronly == 0)
			call_close |= is_wronly;
		else if (is_wronly)
			calldata->arg.fmode |= FMODE_WRITE;
	} else if (is_rdwr)
		calldata->arg.fmode |= FMODE_READ|FMODE_WRITE;

	if (calldata->arg.fmode == 0)
		call_close |= is_rdwr;

	if (!nfs4_valid_open_stateid(state))
		call_close = 0;
	spin_unlock(&state->owner->so_lock);

	if (!call_close) {
		/* Note: exit _without_ calling nfs4_close_done */
		goto out_no_action;
	}

	if (calldata->arg.fmode == 0) {
		task->tk_msg.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_CLOSE];
		if (calldata->roc &&
		    pnfs_roc_drain(inode, &calldata->roc_barrier, task)) {
			nfs_release_seqid(calldata->arg.seqid);
			goto out_wait;
		    }
	}

	nfs_fattr_init(calldata->res.fattr);
	calldata->timestamp = jiffies;
	if (nfs4_setup_sequence(NFS_SERVER(inode),
				&calldata->arg.seq_args,
				&calldata->res.seq_res,
				task) != 0)
		nfs_release_seqid(calldata->arg.seqid);
	dprintk("%s: done!\n", __func__);
	return;
out_no_action:
	task->tk_action = NULL;
out_wait:
	nfs4_sequence_done(task, &calldata->res.seq_res);
}

static const struct rpc_call_ops nfs4_close_ops = {
	.rpc_call_prepare = nfs4_close_prepare,
	.rpc_call_done = nfs4_close_done,
	.rpc_release = nfs4_free_closedata,
};

static bool nfs4_state_has_opener(struct nfs4_state *state)
{
	/* first check existing openers */
	if (test_bit(NFS_O_RDONLY_STATE, &state->flags) != 0 &&
	    state->n_rdonly != 0)
		return true;

	if (test_bit(NFS_O_WRONLY_STATE, &state->flags) != 0 &&
	    state->n_wronly != 0)
		return true;

	if (test_bit(NFS_O_RDWR_STATE, &state->flags) != 0 &&
	    state->n_rdwr != 0)
		return true;

	return false;
}

static bool nfs4_roc(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_open_context *ctx;
	struct nfs4_state *state;

	spin_lock(&inode->i_lock);
	list_for_each_entry(ctx, &nfsi->open_files, list) {
		state = ctx->state;
		if (state == NULL)
			continue;
		if (nfs4_state_has_opener(state)) {
			spin_unlock(&inode->i_lock);
			return false;
		}
	}
	spin_unlock(&inode->i_lock);

	if (nfs4_check_delegation(inode, FMODE_READ))
		return false;

	return pnfs_roc(inode);
}

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
int nfs4_do_close(struct nfs4_state *state, gfp_t gfp_mask, int wait)
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

	nfs4_state_protect(server->nfs_client, NFS_SP4_MACH_CRED_CLEANUP,
		&task_setup_data.rpc_client, &msg);

	calldata = kzalloc(sizeof(*calldata), gfp_mask);
	if (calldata == NULL)
		goto out;
	nfs4_init_sequence(&calldata->arg.seq_args, &calldata->res.seq_res, 1);
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
	calldata->roc = nfs4_roc(state->inode);
	nfs_sb_active(calldata->inode->i_sb);

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
	nfs4_put_open_state(state);
	nfs4_put_state_owner(sp);
	return status;
}

static struct inode *
nfs4_atomic_open(struct inode *dir, struct nfs_open_context *ctx,
		int open_flags, struct iattr *attr, int *opened)
{
	struct nfs4_state *state;
	struct nfs4_label l = {0, 0, 0, NULL}, *label = NULL;

	label = nfs4_label_init_security(dir, ctx->dentry, attr, &l);

	/* Protect against concurrent sillydeletes */
	state = nfs4_do_open(dir, ctx, open_flags, attr, label, opened);

	nfs4_label_release_security(label);

	if (IS_ERR(state))
		return ERR_CAST(state);
	return state->inode;
}

static void nfs4_close_context(struct nfs_open_context *ctx, int is_sync)
{
	if (ctx->state == NULL)
		return;
	if (is_sync)
		nfs4_close_sync(ctx->state, ctx->mode);
	else
		nfs4_close_state(ctx->state, ctx->mode);
}

#define FATTR4_WORD1_NFS40_MASK (2*FATTR4_WORD1_MOUNTED_ON_FILEID - 1UL)
#define FATTR4_WORD2_NFS41_MASK (2*FATTR4_WORD2_SUPPATTR_EXCLCREAT - 1UL)
#define FATTR4_WORD2_NFS42_MASK (2*FATTR4_WORD2_SECURITY_LABEL - 1UL)

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
		/* Sanity check the server answers */
		switch (server->nfs_client->cl_minorversion) {
		case 0:
			res.attr_bitmask[1] &= FATTR4_WORD1_NFS40_MASK;
			res.attr_bitmask[2] = 0;
			break;
		case 1:
			res.attr_bitmask[2] &= FATTR4_WORD2_NFS41_MASK;
			break;
		case 2:
			res.attr_bitmask[2] &= FATTR4_WORD2_NFS42_MASK;
		}
		memcpy(server->attr_bitmask, res.attr_bitmask, sizeof(server->attr_bitmask));
		server->caps &= ~(NFS_CAP_ACLS|NFS_CAP_HARDLINKS|
				NFS_CAP_SYMLINKS|NFS_CAP_FILEID|
				NFS_CAP_MODE|NFS_CAP_NLINK|NFS_CAP_OWNER|
				NFS_CAP_OWNER_GROUP|NFS_CAP_ATIME|
				NFS_CAP_CTIME|NFS_CAP_MTIME|
				NFS_CAP_SECURITY_LABEL);
		if (res.attr_bitmask[0] & FATTR4_WORD0_ACL &&
				res.acl_bitmask & ACL4_SUPPORT_ALLOW_ACL)
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
#ifdef CONFIG_NFS_V4_SECURITY_LABEL
		if (res.attr_bitmask[2] & FATTR4_WORD2_SECURITY_LABEL)
			server->caps |= NFS_CAP_SECURITY_LABEL;
#endif
		memcpy(server->attr_bitmask_nl, res.attr_bitmask,
				sizeof(server->attr_bitmask));
		server->attr_bitmask_nl[2] &= ~FATTR4_WORD2_SECURITY_LABEL;

		memcpy(server->cache_consistency_bitmask, res.attr_bitmask, sizeof(server->cache_consistency_bitmask));
		server->cache_consistency_bitmask[0] &= FATTR4_WORD0_CHANGE|FATTR4_WORD0_SIZE;
		server->cache_consistency_bitmask[1] &= FATTR4_WORD1_TIME_METADATA|FATTR4_WORD1_TIME_MODIFY;
		server->cache_consistency_bitmask[2] = 0;
		server->acl_bitmask = res.acl_bitmask;
		server->fh_expire_type = res.fh_expire_type;
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
	u32 bitmask[3];
	struct nfs4_lookup_root_arg args = {
		.bitmask = bitmask,
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

	bitmask[0] = nfs4_fattr_bitmap[0];
	bitmask[1] = nfs4_fattr_bitmap[1];
	/*
	 * Process the label in the upcoming getfattr
	 */
	bitmask[2] = nfs4_fattr_bitmap[2] & ~FATTR4_WORD2_SECURITY_LABEL;

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
		trace_nfs4_lookup_root(server, fhandle, info->fattr, err);
		switch (err) {
		case 0:
		case -NFS4ERR_WRONGSEC:
			goto out;
		default:
			err = nfs4_handle_exception(server, err, &exception);
		}
	} while (exception.retry);
out:
	return err;
}

static int nfs4_lookup_root_sec(struct nfs_server *server, struct nfs_fh *fhandle,
				struct nfs_fsinfo *info, rpc_authflavor_t flavor)
{
	struct rpc_auth_create_args auth_args = {
		.pseudoflavor = flavor,
	};
	struct rpc_auth *auth;
	int ret;

	auth = rpcauth_create(&auth_args, server->client);
	if (IS_ERR(auth)) {
		ret = -EACCES;
		goto out;
	}
	ret = nfs4_lookup_root(server, fhandle, info);
out:
	return ret;
}

/*
 * Retry pseudoroot lookup with various security flavors.  We do this when:
 *
 *   NFSv4.0: the PUTROOTFH operation returns NFS4ERR_WRONGSEC
 *   NFSv4.1: the server does not support the SECINFO_NO_NAME operation
 *
 * Returns zero on success, or a negative NFS4ERR value, or a
 * negative errno value.
 */
static int nfs4_find_root_sec(struct nfs_server *server, struct nfs_fh *fhandle,
			      struct nfs_fsinfo *info)
{
	/* Per 3530bis 15.33.5 */
	static const rpc_authflavor_t flav_array[] = {
		RPC_AUTH_GSS_KRB5P,
		RPC_AUTH_GSS_KRB5I,
		RPC_AUTH_GSS_KRB5,
		RPC_AUTH_UNIX,			/* courtesy */
		RPC_AUTH_NULL,
	};
	int status = -EPERM;
	size_t i;

	if (server->auth_info.flavor_len > 0) {
		/* try each flavor specified by user */
		for (i = 0; i < server->auth_info.flavor_len; i++) {
			status = nfs4_lookup_root_sec(server, fhandle, info,
						server->auth_info.flavors[i]);
			if (status == -NFS4ERR_WRONGSEC || status == -EACCES)
				continue;
			break;
		}
	} else {
		/* no flavors specified by user, try default list */
		for (i = 0; i < ARRAY_SIZE(flav_array); i++) {
			status = nfs4_lookup_root_sec(server, fhandle, info,
						      flav_array[i]);
			if (status == -NFS4ERR_WRONGSEC || status == -EACCES)
				continue;
			break;
		}
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

static int nfs4_do_find_root_sec(struct nfs_server *server,
		struct nfs_fh *fhandle, struct nfs_fsinfo *info)
{
	int mv = server->nfs_client->cl_minorversion;
	return nfs_v4_minor_ops[mv]->find_root_sec(server, fhandle, info);
}

/**
 * nfs4_proc_get_rootfh - get file handle for server's pseudoroot
 * @server: initialized nfs_server handle
 * @fhandle: we fill in the pseudo-fs root file handle
 * @info: we fill in an FSINFO struct
 * @auth_probe: probe the auth flavours
 *
 * Returns zero on success, or a negative errno.
 */
int nfs4_proc_get_rootfh(struct nfs_server *server, struct nfs_fh *fhandle,
			 struct nfs_fsinfo *info,
			 bool auth_probe)
{
	int status;

	switch (auth_probe) {
	case false:
		status = nfs4_lookup_root(server, fhandle, info);
		if (status != -NFS4ERR_WRONGSEC)
			break;
	default:
		status = nfs4_do_find_root_sec(server, fhandle, info);
	}

	if (status == 0)
		status = nfs4_server_capabilities(server, fhandle);
	if (status == 0)
		status = nfs4_do_fsinfo(server, fhandle, info);

	return nfs4_map_errors(status);
}

static int nfs4_proc_get_root(struct nfs_server *server, struct nfs_fh *mntfh,
			      struct nfs_fsinfo *info)
{
	int error;
	struct nfs_fattr *fattr = info->fattr;
	struct nfs4_label *label = NULL;

	error = nfs4_server_capabilities(server, mntfh);
	if (error < 0) {
		dprintk("nfs4_get_root: getcaps error = %d\n", -error);
		return error;
	}

	label = nfs4_label_alloc(server, GFP_KERNEL);
	if (IS_ERR(label))
		return PTR_ERR(label);

	error = nfs4_proc_getattr(server, mntfh, fattr, label);
	if (error < 0) {
		dprintk("nfs4_get_root: getattr error = %d\n", -error);
		goto err_free_label;
	}

	if (fattr->valid & NFS_ATTR_FATTR_FSID &&
	    !nfs_fsid_equal(&server->fsid, &fattr->fsid))
		memcpy(&server->fsid, &fattr->fsid, sizeof(server->fsid));

err_free_label:
	nfs4_label_free(label);

	return error;
}

/*
 * Get locations and (maybe) other attributes of a referral.
 * Note that we'll actually follow the referral later when
 * we detect fsid mismatch in inode revalidation
 */
static int nfs4_get_referral(struct rpc_clnt *client, struct inode *dir,
			     const struct qstr *name, struct nfs_fattr *fattr,
			     struct nfs_fh *fhandle)
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

	status = nfs4_proc_fs_locations(client, dir, name, locations, page);
	if (status != 0)
		goto out;

	/*
	 * If the fsid didn't change, this is a migration event, not a
	 * referral.  Cause us to drop into the exception handler, which
	 * will kick off migration recovery.
	 */
	if (nfs_fsid_equal(&NFS_SERVER(dir)->fsid, &locations->fattr.fsid)) {
		dprintk("%s: server did not return a different fsid for"
			" a referral at %s\n", __func__, name->name);
		status = -NFS4ERR_MOVED;
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

static int _nfs4_proc_getattr(struct nfs_server *server, struct nfs_fh *fhandle,
				struct nfs_fattr *fattr, struct nfs4_label *label)
{
	struct nfs4_getattr_arg args = {
		.fh = fhandle,
		.bitmask = server->attr_bitmask,
	};
	struct nfs4_getattr_res res = {
		.fattr = fattr,
		.label = label,
		.server = server,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_GETATTR],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};

	args.bitmask = nfs4_bitmask(server, label);

	nfs_fattr_init(fattr);
	return nfs4_call_sync(server->client, server, &msg, &args.seq_args, &res.seq_res, 0);
}

static int nfs4_proc_getattr(struct nfs_server *server, struct nfs_fh *fhandle,
				struct nfs_fattr *fattr, struct nfs4_label *label)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = _nfs4_proc_getattr(server, fhandle, fattr, label);
		trace_nfs4_getattr(server, fhandle, fattr, err);
		err = nfs4_handle_exception(server, err,
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
	struct nfs4_label *label = NULL;
	int status;

	if (pnfs_ld_layoutret_on_setattr(inode) &&
	    sattr->ia_valid & ATTR_SIZE &&
	    sattr->ia_size < i_size_read(inode))
		pnfs_commit_and_return_layout(inode);

	nfs_fattr_init(fattr);
	
	/* Deal with open(O_TRUNC) */
	if (sattr->ia_valid & ATTR_OPEN)
		sattr->ia_valid &= ~(ATTR_MTIME|ATTR_CTIME);

	/* Optimization: if the end result is no change, don't RPC */
	if ((sattr->ia_valid & ~(ATTR_FILE|ATTR_OPEN)) == 0)
		return 0;

	/* Search for an existing open(O_WRITE) file */
	if (sattr->ia_valid & ATTR_FILE) {
		struct nfs_open_context *ctx;

		ctx = nfs_file_open_context(sattr->ia_file);
		if (ctx) {
			cred = ctx->cred;
			state = ctx->state;
		}
	}

	label = nfs4_label_alloc(NFS_SERVER(inode), GFP_KERNEL);
	if (IS_ERR(label))
		return PTR_ERR(label);

	status = nfs4_do_setattr(inode, cred, fattr, sattr, state, NULL, label);
	if (status == 0) {
		nfs_setattr_update_inode(inode, sattr);
		nfs_setsecurity(inode, fattr, label);
	}
	nfs4_label_free(label);
	return status;
}

static int _nfs4_proc_lookup(struct rpc_clnt *clnt, struct inode *dir,
		const struct qstr *name, struct nfs_fh *fhandle,
		struct nfs_fattr *fattr, struct nfs4_label *label)
{
	struct nfs_server *server = NFS_SERVER(dir);
	int		       status;
	struct nfs4_lookup_arg args = {
		.bitmask = server->attr_bitmask,
		.dir_fh = NFS_FH(dir),
		.name = name,
	};
	struct nfs4_lookup_res res = {
		.server = server,
		.fattr = fattr,
		.label = label,
		.fh = fhandle,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LOOKUP],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};

	args.bitmask = nfs4_bitmask(server, label);

	nfs_fattr_init(fattr);

	dprintk("NFS call  lookup %s\n", name->name);
	status = nfs4_call_sync(clnt, server, &msg, &args.seq_args, &res.seq_res, 0);
	dprintk("NFS reply lookup: %d\n", status);
	return status;
}

static void nfs_fixup_secinfo_attributes(struct nfs_fattr *fattr)
{
	fattr->valid |= NFS_ATTR_FATTR_TYPE | NFS_ATTR_FATTR_MODE |
		NFS_ATTR_FATTR_NLINK | NFS_ATTR_FATTR_MOUNTPOINT;
	fattr->mode = S_IFDIR | S_IRUGO | S_IXUGO;
	fattr->nlink = 2;
}

static int nfs4_proc_lookup_common(struct rpc_clnt **clnt, struct inode *dir,
				   struct qstr *name, struct nfs_fh *fhandle,
				   struct nfs_fattr *fattr, struct nfs4_label *label)
{
	struct nfs4_exception exception = { };
	struct rpc_clnt *client = *clnt;
	int err;
	do {
		err = _nfs4_proc_lookup(client, dir, name, fhandle, fattr, label);
		trace_nfs4_lookup(dir, name, err);
		switch (err) {
		case -NFS4ERR_BADNAME:
			err = -ENOENT;
			goto out;
		case -NFS4ERR_MOVED:
			err = nfs4_get_referral(client, dir, name, fattr, fhandle);
			goto out;
		case -NFS4ERR_WRONGSEC:
			err = -EPERM;
			if (client != *clnt)
				goto out;
			client = nfs4_negotiate_security(client, dir, name);
			if (IS_ERR(client))
				return PTR_ERR(client);

			exception.retry = 1;
			break;
		default:
			err = nfs4_handle_exception(NFS_SERVER(dir), err, &exception);
		}
	} while (exception.retry);

out:
	if (err == 0)
		*clnt = client;
	else if (client != *clnt)
		rpc_shutdown_client(client);

	return err;
}

static int nfs4_proc_lookup(struct inode *dir, struct qstr *name,
			    struct nfs_fh *fhandle, struct nfs_fattr *fattr,
			    struct nfs4_label *label)
{
	int status;
	struct rpc_clnt *client = NFS_CLIENT(dir);

	status = nfs4_proc_lookup_common(&client, dir, name, fhandle, fattr, label);
	if (client != NFS_CLIENT(dir)) {
		rpc_shutdown_client(client);
		nfs_fixup_secinfo_attributes(fattr);
	}
	return status;
}

struct rpc_clnt *
nfs4_proc_lookup_mountpoint(struct inode *dir, struct qstr *name,
			    struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct rpc_clnt *client = NFS_CLIENT(dir);
	int status;

	status = nfs4_proc_lookup_common(&client, dir, name, fhandle, fattr, NULL);
	if (status < 0)
		return ERR_PTR(status);
	return (client == NFS_CLIENT(dir)) ? rpc_clone_client(client) : client;
}

static int _nfs4_proc_access(struct inode *inode, struct nfs_access_entry *entry)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs4_accessargs args = {
		.fh = NFS_FH(inode),
		.bitmask = server->cache_consistency_bitmask,
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
	int status = 0;

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
		nfs_access_set_mask(entry, res.access);
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
		err = _nfs4_proc_access(inode, entry);
		trace_nfs4_access(inode, err);
		err = nfs4_handle_exception(NFS_SERVER(inode), err,
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
 * we get the post-operation mtime and size.
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
		err = _nfs4_proc_readlink(inode, page, pgbase, pglen);
		trace_nfs4_readlink(inode, err);
		err = nfs4_handle_exception(NFS_SERVER(inode), err,
				&exception);
	} while (exception.retry);
	return err;
}

/*
 * This is just for mknod.  open(O_CREAT) will always do ->open_context().
 */
static int
nfs4_proc_create(struct inode *dir, struct dentry *dentry, struct iattr *sattr,
		 int flags)
{
	struct nfs4_label l, *ilabel = NULL;
	struct nfs_open_context *ctx;
	struct nfs4_state *state;
	int opened = 0;
	int status = 0;

	ctx = alloc_nfs_open_context(dentry, FMODE_READ);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ilabel = nfs4_label_init_security(dir, dentry, sattr, &l);

	sattr->ia_mode &= ~current_umask();
	state = nfs4_do_open(dir, ctx, flags, sattr, ilabel, &opened);
	if (IS_ERR(state)) {
		status = PTR_ERR(state);
		goto out;
	}
out:
	nfs4_label_release_security(ilabel);
	put_nfs_open_context(ctx);
	return status;
}

static int _nfs4_proc_remove(struct inode *dir, struct qstr *name)
{
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs_removeargs args = {
		.fh = NFS_FH(dir),
		.name = *name,
	};
	struct nfs_removeres res = {
		.server = server,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_REMOVE],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	int status;

	status = nfs4_call_sync(server->client, server, &msg, &args.seq_args, &res.seq_res, 1);
	if (status == 0)
		update_changeattr(dir, &res.cinfo);
	return status;
}

static int nfs4_proc_remove(struct inode *dir, struct qstr *name)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = _nfs4_proc_remove(dir, name);
		trace_nfs4_remove(dir, name, err);
		err = nfs4_handle_exception(NFS_SERVER(dir), err,
				&exception);
	} while (exception.retry);
	return err;
}

static void nfs4_proc_unlink_setup(struct rpc_message *msg, struct inode *dir)
{
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs_removeargs *args = msg->rpc_argp;
	struct nfs_removeres *res = msg->rpc_resp;

	res->server = server;
	msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_REMOVE];
	nfs4_init_sequence(&args->seq_args, &res->seq_res, 1);

	nfs_fattr_init(res->dir_attr);
}

static void nfs4_proc_unlink_rpc_prepare(struct rpc_task *task, struct nfs_unlinkdata *data)
{
	nfs4_setup_sequence(NFS_SERVER(data->dir),
			&data->args.seq_args,
			&data->res.seq_res,
			task);
}

static int nfs4_proc_unlink_done(struct rpc_task *task, struct inode *dir)
{
	struct nfs_unlinkdata *data = task->tk_calldata;
	struct nfs_removeres *res = &data->res;

	if (!nfs4_sequence_done(task, &res->seq_res))
		return 0;
	if (nfs4_async_handle_error(task, res->server, NULL,
				    &data->timeout) == -EAGAIN)
		return 0;
	update_changeattr(dir, &res->cinfo);
	return 1;
}

static void nfs4_proc_rename_setup(struct rpc_message *msg, struct inode *dir)
{
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs_renameargs *arg = msg->rpc_argp;
	struct nfs_renameres *res = msg->rpc_resp;

	msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_RENAME];
	res->server = server;
	nfs4_init_sequence(&arg->seq_args, &res->seq_res, 1);
}

static void nfs4_proc_rename_rpc_prepare(struct rpc_task *task, struct nfs_renamedata *data)
{
	nfs4_setup_sequence(NFS_SERVER(data->old_dir),
			&data->args.seq_args,
			&data->res.seq_res,
			task);
}

static int nfs4_proc_rename_done(struct rpc_task *task, struct inode *old_dir,
				 struct inode *new_dir)
{
	struct nfs_renamedata *data = task->tk_calldata;
	struct nfs_renameres *res = &data->res;

	if (!nfs4_sequence_done(task, &res->seq_res))
		return 0;
	if (nfs4_async_handle_error(task, res->server, NULL, &data->timeout) == -EAGAIN)
		return 0;

	update_changeattr(old_dir, &res->old_cinfo);
	update_changeattr(new_dir, &res->new_cinfo);
	return 1;
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
		.label = NULL,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LINK],
		.rpc_argp = &arg,
		.rpc_resp = &res,
	};
	int status = -ENOMEM;

	res.fattr = nfs_alloc_fattr();
	if (res.fattr == NULL)
		goto out;

	res.label = nfs4_label_alloc(server, GFP_KERNEL);
	if (IS_ERR(res.label)) {
		status = PTR_ERR(res.label);
		goto out;
	}
	arg.bitmask = nfs4_bitmask(server, res.label);

	status = nfs4_call_sync(server->client, server, &msg, &arg.seq_args, &res.seq_res, 1);
	if (!status) {
		update_changeattr(dir, &res.cinfo);
		status = nfs_post_op_update_inode(inode, res.fattr);
		if (!status)
			nfs_setsecurity(inode, res.fattr, res.label);
	}


	nfs4_label_free(res.label);

out:
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
	struct nfs4_label *label;
};

static struct nfs4_createdata *nfs4_alloc_createdata(struct inode *dir,
		struct qstr *name, struct iattr *sattr, u32 ftype)
{
	struct nfs4_createdata *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data != NULL) {
		struct nfs_server *server = NFS_SERVER(dir);

		data->label = nfs4_label_alloc(server, GFP_KERNEL);
		if (IS_ERR(data->label))
			goto out_free;

		data->msg.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_CREATE];
		data->msg.rpc_argp = &data->arg;
		data->msg.rpc_resp = &data->res;
		data->arg.dir_fh = NFS_FH(dir);
		data->arg.server = server;
		data->arg.name = name;
		data->arg.attrs = sattr;
		data->arg.ftype = ftype;
		data->arg.bitmask = nfs4_bitmask(server, data->label);
		data->res.server = server;
		data->res.fh = &data->fh;
		data->res.fattr = &data->fattr;
		data->res.label = data->label;
		nfs_fattr_init(data->res.fattr);
	}
	return data;
out_free:
	kfree(data);
	return NULL;
}

static int nfs4_do_create(struct inode *dir, struct dentry *dentry, struct nfs4_createdata *data)
{
	int status = nfs4_call_sync(NFS_SERVER(dir)->client, NFS_SERVER(dir), &data->msg,
				    &data->arg.seq_args, &data->res.seq_res, 1);
	if (status == 0) {
		update_changeattr(dir, &data->res.dir_cinfo);
		status = nfs_instantiate(dentry, data->res.fh, data->res.fattr, data->res.label);
	}
	return status;
}

static void nfs4_free_createdata(struct nfs4_createdata *data)
{
	nfs4_label_free(data->label);
	kfree(data);
}

static int _nfs4_proc_symlink(struct inode *dir, struct dentry *dentry,
		struct page *page, unsigned int len, struct iattr *sattr,
		struct nfs4_label *label)
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
	data->arg.label = label;
	
	status = nfs4_do_create(dir, dentry, data);

	nfs4_free_createdata(data);
out:
	return status;
}

static int nfs4_proc_symlink(struct inode *dir, struct dentry *dentry,
		struct page *page, unsigned int len, struct iattr *sattr)
{
	struct nfs4_exception exception = { };
	struct nfs4_label l, *label = NULL;
	int err;

	label = nfs4_label_init_security(dir, dentry, sattr, &l);

	do {
		err = _nfs4_proc_symlink(dir, dentry, page, len, sattr, label);
		trace_nfs4_symlink(dir, &dentry->d_name, err);
		err = nfs4_handle_exception(NFS_SERVER(dir), err,
				&exception);
	} while (exception.retry);

	nfs4_label_release_security(label);
	return err;
}

static int _nfs4_proc_mkdir(struct inode *dir, struct dentry *dentry,
		struct iattr *sattr, struct nfs4_label *label)
{
	struct nfs4_createdata *data;
	int status = -ENOMEM;

	data = nfs4_alloc_createdata(dir, &dentry->d_name, sattr, NF4DIR);
	if (data == NULL)
		goto out;

	data->arg.label = label;
	status = nfs4_do_create(dir, dentry, data);

	nfs4_free_createdata(data);
out:
	return status;
}

static int nfs4_proc_mkdir(struct inode *dir, struct dentry *dentry,
		struct iattr *sattr)
{
	struct nfs4_exception exception = { };
	struct nfs4_label l, *label = NULL;
	int err;

	label = nfs4_label_init_security(dir, dentry, sattr, &l);

	sattr->ia_mode &= ~current_umask();
	do {
		err = _nfs4_proc_mkdir(dir, dentry, sattr, label);
		trace_nfs4_mkdir(dir, &dentry->d_name, err);
		err = nfs4_handle_exception(NFS_SERVER(dir), err,
				&exception);
	} while (exception.retry);
	nfs4_label_release_security(label);

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

	dprintk("%s: dentry = %pd2, cookie = %Lu\n", __func__,
			dentry,
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
		err = _nfs4_proc_readdir(dentry, cred, cookie,
				pages, count, plus);
		trace_nfs4_readdir(dentry->d_inode, err);
		err = nfs4_handle_exception(NFS_SERVER(dentry->d_inode), err,
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_mknod(struct inode *dir, struct dentry *dentry,
		struct iattr *sattr, struct nfs4_label *label, dev_t rdev)
{
	struct nfs4_createdata *data;
	int mode = sattr->ia_mode;
	int status = -ENOMEM;

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
	} else if (!S_ISSOCK(mode)) {
		status = -EINVAL;
		goto out_free;
	}

	data->arg.label = label;
	status = nfs4_do_create(dir, dentry, data);
out_free:
	nfs4_free_createdata(data);
out:
	return status;
}

static int nfs4_proc_mknod(struct inode *dir, struct dentry *dentry,
		struct iattr *sattr, dev_t rdev)
{
	struct nfs4_exception exception = { };
	struct nfs4_label l, *label = NULL;
	int err;

	label = nfs4_label_init_security(dir, dentry, sattr, &l);

	sattr->ia_mode &= ~current_umask();
	do {
		err = _nfs4_proc_mknod(dir, dentry, sattr, label, rdev);
		trace_nfs4_mknod(dir, &dentry->d_name, err);
		err = nfs4_handle_exception(NFS_SERVER(dir), err,
				&exception);
	} while (exception.retry);

	nfs4_label_release_security(label);

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
	unsigned long now = jiffies;
	int err;

	do {
		err = _nfs4_do_fsinfo(server, fhandle, fsinfo);
		trace_nfs4_fsinfo(server, fhandle, fsinfo->fattr, err);
		if (err == 0) {
			struct nfs_client *clp = server->nfs_client;

			spin_lock(&clp->cl_lock);
			clp->cl_lease_time = fsinfo->lease_time * HZ;
			clp->cl_last_renewal = now;
			spin_unlock(&clp->cl_lock);
			break;
		}
		err = nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);
	return err;
}

static int nfs4_proc_fsinfo(struct nfs_server *server, struct nfs_fh *fhandle, struct nfs_fsinfo *fsinfo)
{
	int error;

	nfs_fattr_init(fsinfo->fattr);
	error = nfs4_do_fsinfo(server, fhandle, fsinfo);
	if (error == 0) {
		/* block layout checks this! */
		server->pnfs_blksize = fsinfo->blksize;
		set_pnfs_layoutdriver(server, fhandle, fsinfo->layouttype);
	}

	return error;
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

int nfs4_set_rw_stateid(nfs4_stateid *stateid,
		const struct nfs_open_context *ctx,
		const struct nfs_lock_context *l_ctx,
		fmode_t fmode)
{
	const struct nfs_lockowner *lockowner = NULL;

	if (l_ctx != NULL)
		lockowner = &l_ctx->lockowner;
	return nfs4_select_rw_stateid(stateid, ctx->state, fmode, lockowner);
}
EXPORT_SYMBOL_GPL(nfs4_set_rw_stateid);

static bool nfs4_stateid_is_current(nfs4_stateid *stateid,
		const struct nfs_open_context *ctx,
		const struct nfs_lock_context *l_ctx,
		fmode_t fmode)
{
	nfs4_stateid current_stateid;

	/* If the current stateid represents a lost lock, then exit */
	if (nfs4_set_rw_stateid(&current_stateid, ctx, l_ctx, fmode) == -EIO)
		return true;
	return nfs4_stateid_match(stateid, &current_stateid);
}

static bool nfs4_error_stateid_expired(int err)
{
	switch (err) {
	case -NFS4ERR_DELEG_REVOKED:
	case -NFS4ERR_ADMIN_REVOKED:
	case -NFS4ERR_BAD_STATEID:
	case -NFS4ERR_STALE_STATEID:
	case -NFS4ERR_OLD_STATEID:
	case -NFS4ERR_OPENMODE:
	case -NFS4ERR_EXPIRED:
		return true;
	}
	return false;
}

void __nfs4_read_done_cb(struct nfs_pgio_header *hdr)
{
	nfs_invalidate_atime(hdr->inode);
}

static int nfs4_read_done_cb(struct rpc_task *task, struct nfs_pgio_header *hdr)
{
	struct nfs_server *server = NFS_SERVER(hdr->inode);

	trace_nfs4_read(hdr, task->tk_status);
	if (nfs4_async_handle_error(task, server,
				    hdr->args.context->state,
				    NULL) == -EAGAIN) {
		rpc_restart_call_prepare(task);
		return -EAGAIN;
	}

	__nfs4_read_done_cb(hdr);
	if (task->tk_status > 0)
		renew_lease(server, hdr->timestamp);
	return 0;
}

static bool nfs4_read_stateid_changed(struct rpc_task *task,
		struct nfs_pgio_args *args)
{

	if (!nfs4_error_stateid_expired(task->tk_status) ||
		nfs4_stateid_is_current(&args->stateid,
				args->context,
				args->lock_context,
				FMODE_READ))
		return false;
	rpc_restart_call_prepare(task);
	return true;
}

static int nfs4_read_done(struct rpc_task *task, struct nfs_pgio_header *hdr)
{

	dprintk("--> %s\n", __func__);

	if (!nfs4_sequence_done(task, &hdr->res.seq_res))
		return -EAGAIN;
	if (nfs4_read_stateid_changed(task, &hdr->args))
		return -EAGAIN;
	return hdr->pgio_done_cb ? hdr->pgio_done_cb(task, hdr) :
				    nfs4_read_done_cb(task, hdr);
}

static void nfs4_proc_read_setup(struct nfs_pgio_header *hdr,
				 struct rpc_message *msg)
{
	hdr->timestamp   = jiffies;
	hdr->pgio_done_cb = nfs4_read_done_cb;
	msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_READ];
	nfs4_init_sequence(&hdr->args.seq_args, &hdr->res.seq_res, 0);
}

static int nfs4_proc_pgio_rpc_prepare(struct rpc_task *task,
				      struct nfs_pgio_header *hdr)
{
	if (nfs4_setup_sequence(NFS_SERVER(hdr->inode),
			&hdr->args.seq_args,
			&hdr->res.seq_res,
			task))
		return 0;
	if (nfs4_set_rw_stateid(&hdr->args.stateid, hdr->args.context,
				hdr->args.lock_context,
				hdr->rw_ops->rw_mode) == -EIO)
		return -EIO;
	if (unlikely(test_bit(NFS_CONTEXT_BAD, &hdr->args.context->flags)))
		return -EIO;
	return 0;
}

static int nfs4_write_done_cb(struct rpc_task *task,
			      struct nfs_pgio_header *hdr)
{
	struct inode *inode = hdr->inode;

	trace_nfs4_write(hdr, task->tk_status);
	if (nfs4_async_handle_error(task, NFS_SERVER(inode),
				    hdr->args.context->state,
				    NULL) == -EAGAIN) {
		rpc_restart_call_prepare(task);
		return -EAGAIN;
	}
	if (task->tk_status >= 0) {
		renew_lease(NFS_SERVER(inode), hdr->timestamp);
		nfs_post_op_update_inode_force_wcc(inode, &hdr->fattr);
	}
	return 0;
}

static bool nfs4_write_stateid_changed(struct rpc_task *task,
		struct nfs_pgio_args *args)
{

	if (!nfs4_error_stateid_expired(task->tk_status) ||
		nfs4_stateid_is_current(&args->stateid,
				args->context,
				args->lock_context,
				FMODE_WRITE))
		return false;
	rpc_restart_call_prepare(task);
	return true;
}

static int nfs4_write_done(struct rpc_task *task, struct nfs_pgio_header *hdr)
{
	if (!nfs4_sequence_done(task, &hdr->res.seq_res))
		return -EAGAIN;
	if (nfs4_write_stateid_changed(task, &hdr->args))
		return -EAGAIN;
	return hdr->pgio_done_cb ? hdr->pgio_done_cb(task, hdr) :
		nfs4_write_done_cb(task, hdr);
}

static
bool nfs4_write_need_cache_consistency_data(struct nfs_pgio_header *hdr)
{
	/* Don't request attributes for pNFS or O_DIRECT writes */
	if (hdr->ds_clp != NULL || hdr->dreq != NULL)
		return false;
	/* Otherwise, request attributes if and only if we don't hold
	 * a delegation
	 */
	return nfs4_have_delegation(hdr->inode, FMODE_READ) == 0;
}

static void nfs4_proc_write_setup(struct nfs_pgio_header *hdr,
				  struct rpc_message *msg)
{
	struct nfs_server *server = NFS_SERVER(hdr->inode);

	if (!nfs4_write_need_cache_consistency_data(hdr)) {
		hdr->args.bitmask = NULL;
		hdr->res.fattr = NULL;
	} else
		hdr->args.bitmask = server->cache_consistency_bitmask;

	if (!hdr->pgio_done_cb)
		hdr->pgio_done_cb = nfs4_write_done_cb;
	hdr->res.server = server;
	hdr->timestamp   = jiffies;

	msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_WRITE];
	nfs4_init_sequence(&hdr->args.seq_args, &hdr->res.seq_res, 1);
}

static void nfs4_proc_commit_rpc_prepare(struct rpc_task *task, struct nfs_commit_data *data)
{
	nfs4_setup_sequence(NFS_SERVER(data->inode),
			&data->args.seq_args,
			&data->res.seq_res,
			task);
}

static int nfs4_commit_done_cb(struct rpc_task *task, struct nfs_commit_data *data)
{
	struct inode *inode = data->inode;

	trace_nfs4_commit(data, task->tk_status);
	if (nfs4_async_handle_error(task, NFS_SERVER(inode),
				    NULL, NULL) == -EAGAIN) {
		rpc_restart_call_prepare(task);
		return -EAGAIN;
	}
	return 0;
}

static int nfs4_commit_done(struct rpc_task *task, struct nfs_commit_data *data)
{
	if (!nfs4_sequence_done(task, &data->res.seq_res))
		return -EAGAIN;
	return data->commit_done_cb(task, data);
}

static void nfs4_proc_commit_setup(struct nfs_commit_data *data, struct rpc_message *msg)
{
	struct nfs_server *server = NFS_SERVER(data->inode);

	if (data->commit_done_cb == NULL)
		data->commit_done_cb = nfs4_commit_done_cb;
	data->res.server = server;
	msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_COMMIT];
	nfs4_init_sequence(&data->args.seq_args, &data->res.seq_res, 1);
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

	trace_nfs4_renew_async(clp, task->tk_status);
	switch (task->tk_status) {
	case 0:
		break;
	case -NFS4ERR_LEASE_MOVED:
		nfs4_schedule_lease_moved_recovery(clp);
		break;
	default:
		/* Unless we're shutting down, schedule state recovery! */
		if (test_bit(NFS_CS_RENEWD, &clp->cl_res_state) == 0)
			return;
		if (task->tk_status != NFS4ERR_CB_PATH_DOWN) {
			nfs4_schedule_lease_recovery(clp);
			return;
		}
		nfs4_schedule_path_down_recovery(clp);
	}
	do_renew_lease(clp, timestamp);
}

static const struct rpc_call_ops nfs4_renew_ops = {
	.rpc_call_done = nfs4_renew_done,
	.rpc_release = nfs4_renew_release,
};

static int nfs4_proc_async_renew(struct nfs_client *clp, struct rpc_cred *cred, unsigned renew_flags)
{
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_RENEW],
		.rpc_argp	= clp,
		.rpc_cred	= cred,
	};
	struct nfs4_renewdata *data;

	if (renew_flags == 0)
		return 0;
	if (!atomic_inc_not_zero(&clp->cl_count))
		return -EIO;
	data = kmalloc(sizeof(*data), GFP_NOFS);
	if (data == NULL)
		return -ENOMEM;
	data->client = clp;
	data->timestamp = jiffies;
	return rpc_call_async(clp->cl_rpcclient, &msg, RPC_TASK_TIMEOUT,
			&nfs4_renew_ops, data);
}

static int nfs4_proc_renew(struct nfs_client *clp, struct rpc_cred *cred)
{
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_RENEW],
		.rpc_argp	= clp,
		.rpc_cred	= cred,
	};
	unsigned long now = jiffies;
	int status;

	status = rpc_call_sync(clp->cl_rpcclient, &msg, RPC_TASK_TIMEOUT);
	if (status < 0)
		return status;
	do_renew_lease(clp, now);
	return 0;
}

static inline int nfs4_server_supports_acls(struct nfs_server *server)
{
	return server->caps & NFS_CAP_ACLS;
}

/* Assuming that XATTR_SIZE_MAX is a multiple of PAGE_SIZE, and that
 * it's OK to put sizeof(void) * (XATTR_SIZE_MAX/PAGE_SIZE) bytes on
 * the stack.
 */
#define NFS4ACL_MAXPAGES DIV_ROUND_UP(XATTR_SIZE_MAX, PAGE_SIZE)

static int buf_to_pages_noslab(const void *buf, size_t buflen,
		struct page **pages, unsigned int *pgbase)
{
	struct page *newpage, **spages;
	int rc = 0;
	size_t len;
	spages = pages;

	do {
		len = min_t(size_t, PAGE_SIZE, buflen);
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

static void nfs4_write_cached_acl(struct inode *inode, struct page **pages, size_t pgbase, size_t acl_len)
{
	struct nfs4_cached_acl *acl;
	size_t buflen = sizeof(*acl) + acl_len;

	if (buflen <= PAGE_SIZE) {
		acl = kmalloc(buflen, GFP_KERNEL);
		if (acl == NULL)
			goto out;
		acl->cached = 1;
		_copy_from_pages(acl->data, pages, pgbase, acl_len);
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
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_GETACL],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	unsigned int npages = DIV_ROUND_UP(buflen, PAGE_SIZE);
	int ret = -ENOMEM, i;

	/* As long as we're doing a round trip to the server anyway,
	 * let's be prepared for a page of acl data. */
	if (npages == 0)
		npages = 1;
	if (npages > ARRAY_SIZE(pages))
		return -ERANGE;

	for (i = 0; i < npages; i++) {
		pages[i] = alloc_page(GFP_KERNEL);
		if (!pages[i])
			goto out_free;
	}

	/* for decoding across pages */
	res.acl_scratch = alloc_page(GFP_KERNEL);
	if (!res.acl_scratch)
		goto out_free;

	args.acl_len = npages * PAGE_SIZE;
	args.acl_pgbase = 0;

	dprintk("%s  buf %p buflen %zu npages %d args.acl_len %zu\n",
		__func__, buf, buflen, npages, args.acl_len);
	ret = nfs4_call_sync(NFS_SERVER(inode)->client, NFS_SERVER(inode),
			     &msg, &args.seq_args, &res.seq_res, 0);
	if (ret)
		goto out_free;

	/* Handle the case where the passed-in buffer is too short */
	if (res.acl_flags & NFS4_ACL_TRUNC) {
		/* Did the user only issue a request for the acl length? */
		if (buf == NULL)
			goto out_ok;
		ret = -ERANGE;
		goto out_free;
	}
	nfs4_write_cached_acl(inode, pages, res.acl_data_offset, res.acl_len);
	if (buf) {
		if (res.acl_len > buflen) {
			ret = -ERANGE;
			goto out_free;
		}
		_copy_from_pages(buf, pages, res.acl_data_offset, res.acl_len);
	}
out_ok:
	ret = res.acl_len;
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
		trace_nfs4_get_acl(inode, ret);
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
	unsigned int npages = DIV_ROUND_UP(buflen, PAGE_SIZE);
	int ret, i;

	if (!nfs4_server_supports_acls(server))
		return -EOPNOTSUPP;
	if (npages > ARRAY_SIZE(pages))
		return -ERANGE;
	i = buf_to_pages_noslab(buf, buflen, arg.acl_pages, &arg.acl_pgbase);
	if (i < 0)
		return i;
	nfs4_inode_return_delegation(inode);
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
		err = __nfs4_proc_set_acl(inode, buf, buflen);
		trace_nfs4_set_acl(inode, err);
		err = nfs4_handle_exception(NFS_SERVER(inode), err,
				&exception);
	} while (exception.retry);
	return err;
}

#ifdef CONFIG_NFS_V4_SECURITY_LABEL
static int _nfs4_get_security_label(struct inode *inode, void *buf,
					size_t buflen)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_fattr fattr;
	struct nfs4_label label = {0, 0, buflen, buf};

	u32 bitmask[3] = { 0, 0, FATTR4_WORD2_SECURITY_LABEL };
	struct nfs4_getattr_arg arg = {
		.fh		= NFS_FH(inode),
		.bitmask	= bitmask,
	};
	struct nfs4_getattr_res res = {
		.fattr		= &fattr,
		.label		= &label,
		.server		= server,
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_GETATTR],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
	};
	int ret;

	nfs_fattr_init(&fattr);

	ret = nfs4_call_sync(server->client, server, &msg, &arg.seq_args, &res.seq_res, 0);
	if (ret)
		return ret;
	if (!(fattr.valid & NFS_ATTR_FATTR_V4_SECURITY_LABEL))
		return -ENOENT;
	if (buflen < label.len)
		return -ERANGE;
	return 0;
}

static int nfs4_get_security_label(struct inode *inode, void *buf,
					size_t buflen)
{
	struct nfs4_exception exception = { };
	int err;

	if (!nfs_server_capable(inode, NFS_CAP_SECURITY_LABEL))
		return -EOPNOTSUPP;

	do {
		err = _nfs4_get_security_label(inode, buf, buflen);
		trace_nfs4_get_security_label(inode, err);
		err = nfs4_handle_exception(NFS_SERVER(inode), err,
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_do_set_security_label(struct inode *inode,
		struct nfs4_label *ilabel,
		struct nfs_fattr *fattr,
		struct nfs4_label *olabel)
{

	struct iattr sattr = {0};
	struct nfs_server *server = NFS_SERVER(inode);
	const u32 bitmask[3] = { 0, 0, FATTR4_WORD2_SECURITY_LABEL };
	struct nfs_setattrargs arg = {
		.fh             = NFS_FH(inode),
		.iap            = &sattr,
		.server		= server,
		.bitmask	= bitmask,
		.label		= ilabel,
	};
	struct nfs_setattrres res = {
		.fattr		= fattr,
		.label		= olabel,
		.server		= server,
	};
	struct rpc_message msg = {
		.rpc_proc       = &nfs4_procedures[NFSPROC4_CLNT_SETATTR],
		.rpc_argp       = &arg,
		.rpc_resp       = &res,
	};
	int status;

	nfs4_stateid_copy(&arg.stateid, &zero_stateid);

	status = nfs4_call_sync(server->client, server, &msg, &arg.seq_args, &res.seq_res, 1);
	if (status)
		dprintk("%s failed: %d\n", __func__, status);

	return status;
}

static int nfs4_do_set_security_label(struct inode *inode,
		struct nfs4_label *ilabel,
		struct nfs_fattr *fattr,
		struct nfs4_label *olabel)
{
	struct nfs4_exception exception = { };
	int err;

	do {
		err = _nfs4_do_set_security_label(inode, ilabel,
				fattr, olabel);
		trace_nfs4_set_security_label(inode, err);
		err = nfs4_handle_exception(NFS_SERVER(inode), err,
				&exception);
	} while (exception.retry);
	return err;
}

static int
nfs4_set_security_label(struct dentry *dentry, const void *buf, size_t buflen)
{
	struct nfs4_label ilabel, *olabel = NULL;
	struct nfs_fattr fattr;
	struct rpc_cred *cred;
	struct inode *inode = dentry->d_inode;
	int status;

	if (!nfs_server_capable(inode, NFS_CAP_SECURITY_LABEL))
		return -EOPNOTSUPP;

	nfs_fattr_init(&fattr);

	ilabel.pi = 0;
	ilabel.lfs = 0;
	ilabel.label = (char *)buf;
	ilabel.len = buflen;

	cred = rpc_lookup_cred();
	if (IS_ERR(cred))
		return PTR_ERR(cred);

	olabel = nfs4_label_alloc(NFS_SERVER(inode), GFP_KERNEL);
	if (IS_ERR(olabel)) {
		status = -PTR_ERR(olabel);
		goto out;
	}

	status = nfs4_do_set_security_label(inode, &ilabel, &fattr, olabel);
	if (status == 0)
		nfs_setsecurity(inode, &fattr, olabel);

	nfs4_label_free(olabel);
out:
	put_rpccred(cred);
	return status;
}
#endif	/* CONFIG_NFS_V4_SECURITY_LABEL */


static int
nfs4_async_handle_error(struct rpc_task *task, const struct nfs_server *server,
			struct nfs4_state *state, long *timeout)
{
	struct nfs_client *clp = server->nfs_client;

	if (task->tk_status >= 0)
		return 0;
	switch(task->tk_status) {
		case -NFS4ERR_DELEG_REVOKED:
		case -NFS4ERR_ADMIN_REVOKED:
		case -NFS4ERR_BAD_STATEID:
		case -NFS4ERR_OPENMODE:
			if (state == NULL)
				break;
			if (nfs4_schedule_stateid_recovery(server, state) < 0)
				goto recovery_failed;
			goto wait_on_recovery;
		case -NFS4ERR_EXPIRED:
			if (state != NULL) {
				if (nfs4_schedule_stateid_recovery(server, state) < 0)
					goto recovery_failed;
			}
		case -NFS4ERR_STALE_STATEID:
		case -NFS4ERR_STALE_CLIENTID:
			nfs4_schedule_lease_recovery(clp);
			goto wait_on_recovery;
		case -NFS4ERR_MOVED:
			if (nfs4_schedule_migration_recovery(server) < 0)
				goto recovery_failed;
			goto wait_on_recovery;
		case -NFS4ERR_LEASE_MOVED:
			nfs4_schedule_lease_moved_recovery(clp);
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
			nfs4_schedule_session_recovery(clp->cl_session, task->tk_status);
			goto wait_on_recovery;
#endif /* CONFIG_NFS_V4_1 */
		case -NFS4ERR_DELAY:
			nfs_inc_server_stats(server, NFSIOS_DELAY);
			rpc_delay(task, nfs4_update_delay(timeout));
			goto restart_call;
		case -NFS4ERR_GRACE:
			rpc_delay(task, NFS4_POLL_RETRY_MAX);
		case -NFS4ERR_RETRY_UNCACHED_REP:
		case -NFS4ERR_OLD_STATEID:
			goto restart_call;
	}
	task->tk_status = nfs4_map_errors(task->tk_status);
	return 0;
recovery_failed:
	task->tk_status = -EIO;
	return 0;
wait_on_recovery:
	rpc_sleep_on(&clp->cl_rpcwaitq, task, NULL);
	if (test_bit(NFS4CLNT_MANAGER_RUNNING, &clp->cl_state) == 0)
		rpc_wake_up_queued_task(&clp->cl_rpcwaitq, task);
	if (test_bit(NFS_MIG_FAILED, &server->mig_status))
		goto recovery_failed;
restart_call:
	task->tk_status = 0;
	return -EAGAIN;
}

static void nfs4_init_boot_verifier(const struct nfs_client *clp,
				    nfs4_verifier *bootverf)
{
	__be32 verf[2];

	if (test_bit(NFS4CLNT_PURGE_STATE, &clp->cl_state)) {
		/* An impossible timestamp guarantees this value
		 * will never match a generated boot time. */
		verf[0] = 0;
		verf[1] = cpu_to_be32(NSEC_PER_SEC + 1);
	} else {
		struct nfs_net *nn = net_generic(clp->cl_net, nfs_net_id);
		verf[0] = cpu_to_be32(nn->boot_time.tv_sec);
		verf[1] = cpu_to_be32(nn->boot_time.tv_nsec);
	}
	memcpy(bootverf->data, verf, sizeof(bootverf->data));
}

static unsigned int
nfs4_init_nonuniform_client_string(const struct nfs_client *clp,
				   char *buf, size_t len)
{
	unsigned int result;

	rcu_read_lock();
	result = scnprintf(buf, len, "Linux NFSv4.0 %s/%s %s",
				clp->cl_ipaddr,
				rpc_peeraddr2str(clp->cl_rpcclient,
							RPC_DISPLAY_ADDR),
				rpc_peeraddr2str(clp->cl_rpcclient,
							RPC_DISPLAY_PROTO));
	rcu_read_unlock();
	return result;
}

static unsigned int
nfs4_init_uniform_client_string(const struct nfs_client *clp,
				char *buf, size_t len)
{
	const char *nodename = clp->cl_rpcclient->cl_nodename;

	if (nfs4_client_id_uniquifier[0] != '\0')
		return scnprintf(buf, len, "Linux NFSv%u.%u %s/%s",
				clp->rpc_ops->version,
				clp->cl_minorversion,
				nfs4_client_id_uniquifier,
				nodename);
	return scnprintf(buf, len, "Linux NFSv%u.%u %s",
				clp->rpc_ops->version, clp->cl_minorversion,
				nodename);
}

/*
 * nfs4_callback_up_net() starts only "tcp" and "tcp6" callback
 * services.  Advertise one based on the address family of the
 * clientaddr.
 */
static unsigned int
nfs4_init_callback_netid(const struct nfs_client *clp, char *buf, size_t len)
{
	if (strchr(clp->cl_ipaddr, ':') != NULL)
		return scnprintf(buf, len, "tcp6");
	else
		return scnprintf(buf, len, "tcp");
}

static void nfs4_setclientid_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_setclientid *sc = calldata;

	if (task->tk_status == 0)
		sc->sc_cred = get_rpccred(task->tk_rqstp->rq_cred);
}

static const struct rpc_call_ops nfs4_setclientid_ops = {
	.rpc_call_done = nfs4_setclientid_done,
};

/**
 * nfs4_proc_setclientid - Negotiate client ID
 * @clp: state data structure
 * @program: RPC program for NFSv4 callback service
 * @port: IP port number for NFS4 callback service
 * @cred: RPC credential to use for this call
 * @res: where to place the result
 *
 * Returns zero, a negative errno, or a negative NFS4ERR status code.
 */
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
	struct rpc_task *task;
	struct rpc_task_setup task_setup_data = {
		.rpc_client = clp->cl_rpcclient,
		.rpc_message = &msg,
		.callback_ops = &nfs4_setclientid_ops,
		.callback_data = &setclientid,
		.flags = RPC_TASK_TIMEOUT,
	};
	int status;

	/* nfs_client_id4 */
	nfs4_init_boot_verifier(clp, &sc_verifier);
	if (test_bit(NFS_CS_MIGRATION, &clp->cl_flags))
		setclientid.sc_name_len =
				nfs4_init_uniform_client_string(clp,
						setclientid.sc_name,
						sizeof(setclientid.sc_name));
	else
		setclientid.sc_name_len =
				nfs4_init_nonuniform_client_string(clp,
						setclientid.sc_name,
						sizeof(setclientid.sc_name));
	/* cb_client4 */
	setclientid.sc_netid_len =
				nfs4_init_callback_netid(clp,
						setclientid.sc_netid,
						sizeof(setclientid.sc_netid));
	setclientid.sc_uaddr_len = scnprintf(setclientid.sc_uaddr,
				sizeof(setclientid.sc_uaddr), "%s.%u.%u",
				clp->cl_ipaddr, port >> 8, port & 255);

	dprintk("NFS call  setclientid auth=%s, '%.*s'\n",
		clp->cl_rpcclient->cl_auth->au_ops->au_name,
		setclientid.sc_name_len, setclientid.sc_name);
	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task)) {
		status = PTR_ERR(task);
		goto out;
	}
	status = task->tk_status;
	if (setclientid.sc_cred) {
		clp->cl_acceptor = rpcauth_stringify_acceptor(setclientid.sc_cred);
		put_rpccred(setclientid.sc_cred);
	}
	rpc_put_task(task);
out:
	trace_nfs4_setclientid(clp, status);
	dprintk("NFS reply setclientid: %d\n", status);
	return status;
}

/**
 * nfs4_proc_setclientid_confirm - Confirm client ID
 * @clp: state data structure
 * @res: result of a previous SETCLIENTID
 * @cred: RPC credential to use for this call
 *
 * Returns zero, a negative errno, or a negative NFS4ERR status code.
 */
int nfs4_proc_setclientid_confirm(struct nfs_client *clp,
		struct nfs4_setclientid_res *arg,
		struct rpc_cred *cred)
{
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SETCLIENTID_CONFIRM],
		.rpc_argp = arg,
		.rpc_cred = cred,
	};
	int status;

	dprintk("NFS call  setclientid_confirm auth=%s, (client ID %llx)\n",
		clp->cl_rpcclient->cl_auth->au_ops->au_name,
		clp->cl_clientid);
	status = rpc_call_sync(clp->cl_rpcclient, &msg, RPC_TASK_TIMEOUT);
	trace_nfs4_setclientid_confirm(clp, status);
	dprintk("NFS reply setclientid_confirm: %d\n", status);
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
	struct inode *inode;
	bool roc;
	u32 roc_barrier;
};

static void nfs4_delegreturn_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_delegreturndata *data = calldata;

	if (!nfs4_sequence_done(task, &data->res.seq_res))
		return;

	trace_nfs4_delegreturn_exit(&data->args, &data->res, task->tk_status);
	switch (task->tk_status) {
	case 0:
		renew_lease(data->res.server, data->timestamp);
	case -NFS4ERR_ADMIN_REVOKED:
	case -NFS4ERR_DELEG_REVOKED:
	case -NFS4ERR_BAD_STATEID:
	case -NFS4ERR_OLD_STATEID:
	case -NFS4ERR_STALE_STATEID:
	case -NFS4ERR_EXPIRED:
		task->tk_status = 0;
		if (data->roc)
			pnfs_roc_set_barrier(data->inode, data->roc_barrier);
		break;
	default:
		if (nfs4_async_handle_error(task, data->res.server,
					    NULL, NULL) == -EAGAIN) {
			rpc_restart_call_prepare(task);
			return;
		}
	}
	data->rpc_status = task->tk_status;
}

static void nfs4_delegreturn_release(void *calldata)
{
	struct nfs4_delegreturndata *data = calldata;

	if (data->roc)
		pnfs_roc_release(data->inode);
	kfree(calldata);
}

static void nfs4_delegreturn_prepare(struct rpc_task *task, void *data)
{
	struct nfs4_delegreturndata *d_data;

	d_data = (struct nfs4_delegreturndata *)data;

	if (d_data->roc &&
	    pnfs_roc_drain(d_data->inode, &d_data->roc_barrier, task))
		return;

	nfs4_setup_sequence(d_data->res.server,
			&d_data->args.seq_args,
			&d_data->res.seq_res,
			task);
}

static const struct rpc_call_ops nfs4_delegreturn_ops = {
	.rpc_call_prepare = nfs4_delegreturn_prepare,
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
	nfs4_init_sequence(&data->args.seq_args, &data->res.seq_res, 1);
	data->args.fhandle = &data->fh;
	data->args.stateid = &data->stateid;
	data->args.bitmask = server->cache_consistency_bitmask;
	nfs_copy_fh(&data->fh, NFS_FH(inode));
	nfs4_stateid_copy(&data->stateid, stateid);
	data->res.fattr = &data->fattr;
	data->res.server = server;
	nfs_fattr_init(data->res.fattr);
	data->timestamp = jiffies;
	data->rpc_status = 0;
	data->inode = inode;
	data->roc = list_empty(&NFS_I(inode)->open_files) ?
		    pnfs_roc(inode) : false;

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
	if (status == 0)
		nfs_post_op_update_inode_force_wcc(inode, &data->fattr);
	else
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
		trace_nfs4_delegreturn(inode, err);
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
	freezable_schedule_timeout_killable_unsafe(timeout);
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
	arg.lock_owner.id = lsp->ls_seqid.owner_id;
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
	request->fl_ops = NULL;
out:
	return status;
}

static int nfs4_proc_getlk(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct nfs4_exception exception = { };
	int err;

	do {
		err = _nfs4_proc_getlk(state, cmd, request);
		trace_nfs4_get_lock(request, state, cmd, err);
		err = nfs4_handle_exception(NFS_SERVER(state->inode), err,
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
			nfs4_stateid_copy(&calldata->lsp->ls_stateid,
					&calldata->res.stateid);
			renew_lease(calldata->server, calldata->timestamp);
			break;
		case -NFS4ERR_BAD_STATEID:
		case -NFS4ERR_OLD_STATEID:
		case -NFS4ERR_STALE_STATEID:
		case -NFS4ERR_EXPIRED:
			break;
		default:
			if (nfs4_async_handle_error(task, calldata->server,
						    NULL, NULL) == -EAGAIN)
				rpc_restart_call_prepare(task);
	}
	nfs_release_seqid(calldata->arg.seqid);
}

static void nfs4_locku_prepare(struct rpc_task *task, void *data)
{
	struct nfs4_unlockdata *calldata = data;

	if (nfs_wait_on_sequence(calldata->arg.seqid, task) != 0)
		goto out_wait;
	if (test_bit(NFS_LOCK_INITIALIZED, &calldata->lsp->ls_flags) == 0) {
		/* Note: exit _without_ running nfs4_locku_done */
		goto out_no_action;
	}
	calldata->timestamp = jiffies;
	if (nfs4_setup_sequence(calldata->server,
				&calldata->arg.seq_args,
				&calldata->res.seq_res,
				task) != 0)
		nfs_release_seqid(calldata->arg.seqid);
	return;
out_no_action:
	task->tk_action = NULL;
out_wait:
	nfs4_sequence_done(task, &calldata->res.seq_res);
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

	nfs4_state_protect(NFS_SERVER(lsp->ls_state->inode)->nfs_client,
		NFS_SP4_MACH_CRED_CLEANUP, &task_setup_data.rpc_client, &msg);

	/* Ensure this is an unlock - when canceling a lock, the
	 * canceled lock is passed in, and it won't be an unlock.
	 */
	fl->fl_type = F_UNLCK;

	data = nfs4_alloc_unlockdata(fl, ctx, lsp, seqid);
	if (data == NULL) {
		nfs_free_seqid(seqid);
		return ERR_PTR(-ENOMEM);
	}

	nfs4_init_sequence(&data->arg.seq_args, &data->res.seq_res, 1);
	msg.rpc_argp = &data->arg;
	msg.rpc_resp = &data->res;
	task_setup_data.callback_data = data;
	return rpc_run_task(&task_setup_data);
}

static int nfs4_proc_unlck(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct inode *inode = state->inode;
	struct nfs4_state_owner *sp = state->owner;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_seqid *seqid;
	struct nfs4_lock_state *lsp;
	struct rpc_task *task;
	int status = 0;
	unsigned char fl_flags = request->fl_flags;

	status = nfs4_set_lock_state(state, request);
	/* Unlock _before_ we do the RPC call */
	request->fl_flags |= FL_EXISTS;
	/* Exclude nfs_delegation_claim_locks() */
	mutex_lock(&sp->so_delegreturn_mutex);
	/* Exclude nfs4_reclaim_open_stateid() - note nesting! */
	down_read(&nfsi->rwsem);
	if (do_vfs_lock(request->fl_file, request) == -ENOENT) {
		up_read(&nfsi->rwsem);
		mutex_unlock(&sp->so_delegreturn_mutex);
		goto out;
	}
	up_read(&nfsi->rwsem);
	mutex_unlock(&sp->so_delegreturn_mutex);
	if (status != 0)
		goto out;
	/* Is this a delegated lock? */
	lsp = request->fl_u.nfs4_fl.owner;
	if (test_bit(NFS_LOCK_INITIALIZED, &lsp->ls_flags) == 0)
		goto out;
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
	trace_nfs4_unlock(request, state, F_SETLK, status);
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
	p->arg.lock_owner.id = lsp->ls_seqid.owner_id;
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
		goto out_wait;
	/* Do we need to do an open_to_lock_owner? */
	if (!(data->arg.lock_seqid->sequence->flags & NFS_SEQID_CONFIRMED)) {
		if (nfs_wait_on_sequence(data->arg.open_seqid, task) != 0) {
			goto out_release_lock_seqid;
		}
		data->arg.open_stateid = &state->open_stateid;
		data->arg.new_lock_owner = 1;
		data->res.open_seqid = data->arg.open_seqid;
	} else
		data->arg.new_lock_owner = 0;
	if (!nfs4_valid_open_stateid(state)) {
		data->rpc_status = -EBADF;
		task->tk_action = NULL;
		goto out_release_open_seqid;
	}
	data->timestamp = jiffies;
	if (nfs4_setup_sequence(data->server,
				&data->arg.seq_args,
				&data->res.seq_res,
				task) == 0)
		return;
out_release_open_seqid:
	nfs_release_seqid(data->arg.open_seqid);
out_release_lock_seqid:
	nfs_release_seqid(data->arg.lock_seqid);
out_wait:
	nfs4_sequence_done(task, &data->res.seq_res);
	dprintk("%s: done!, ret = %d\n", __func__, data->rpc_status);
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
		nfs4_stateid_copy(&data->lsp->ls_stateid, &data->res.stateid);
		set_bit(NFS_LOCK_INITIALIZED, &data->lsp->ls_flags);
		renew_lease(NFS_SERVER(data->ctx->dentry->d_inode), data->timestamp);
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

static void nfs4_handle_setlk_error(struct nfs_server *server, struct nfs4_lock_state *lsp, int new_lock_owner, int error)
{
	switch (error) {
	case -NFS4ERR_ADMIN_REVOKED:
	case -NFS4ERR_BAD_STATEID:
		lsp->ls_seqid.flags &= ~NFS_SEQID_CONFIRMED;
		if (new_lock_owner != 0 ||
		   test_bit(NFS_LOCK_INITIALIZED, &lsp->ls_flags) != 0)
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
	nfs4_init_sequence(&data->arg.seq_args, &data->res.seq_res, 1);
	msg.rpc_argp = &data->arg;
	msg.rpc_resp = &data->res;
	task_setup_data.callback_data = data;
	if (recovery_type > NFS_LOCK_NEW) {
		if (recovery_type == NFS_LOCK_RECLAIM)
			data->arg.reclaim = NFS_LOCK_RECLAIM;
		nfs4_set_sequence_privileged(&data->arg.seq_args);
	}
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
		trace_nfs4_lock_reclaim(request, state, F_SETLK, err);
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
	if (!recover_lost_locks) {
		set_bit(NFS_LOCK_LOST, &request->fl_u.nfs4_fl.owner->ls_flags);
		return 0;
	}
	do {
		if (test_bit(NFS_DELEGATED_STATE, &state->flags) != 0)
			return 0;
		err = _nfs4_do_setlk(state, F_SETLK, request, NFS_LOCK_EXPIRED);
		trace_nfs4_lock_expired(request, state, F_SETLK, err);
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

#if defined(CONFIG_NFS_V4_1)
/**
 * nfs41_check_expired_locks - possibly free a lock stateid
 *
 * @state: NFSv4 state for an inode
 *
 * Returns NFS_OK if recovery for this stateid is now finished.
 * Otherwise a negative NFS4ERR value is returned.
 */
static int nfs41_check_expired_locks(struct nfs4_state *state)
{
	int status, ret = -NFS4ERR_BAD_STATEID;
	struct nfs4_lock_state *lsp;
	struct nfs_server *server = NFS_SERVER(state->inode);

	list_for_each_entry(lsp, &state->lock_states, ls_locks) {
		if (test_bit(NFS_LOCK_INITIALIZED, &lsp->ls_flags)) {
			struct rpc_cred *cred = lsp->ls_state->owner->so_cred;

			status = nfs41_test_stateid(server,
					&lsp->ls_stateid,
					cred);
			trace_nfs4_test_lock_stateid(state, lsp, status);
			if (status != NFS_OK) {
				/* Free the stateid unless the server
				 * informs us the stateid is unrecognized. */
				if (status != -NFS4ERR_BAD_STATEID)
					nfs41_free_stateid(server,
							&lsp->ls_stateid,
							cred);
				clear_bit(NFS_LOCK_INITIALIZED, &lsp->ls_flags);
				ret = status;
			}
		}
	};

	return ret;
}

static int nfs41_lock_expired(struct nfs4_state *state, struct file_lock *request)
{
	int status = NFS_OK;

	if (test_bit(LK_STATE_IN_USE, &state->flags))
		status = nfs41_check_expired_locks(state);
	if (status != NFS_OK)
		status = nfs4_lock_expired(state, request);
	return status;
}
#endif

static int _nfs4_proc_setlk(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct nfs4_state_owner *sp = state->owner;
	struct nfs_inode *nfsi = NFS_I(state->inode);
	unsigned char fl_flags = request->fl_flags;
	unsigned int seq;
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
	seq = raw_seqcount_begin(&sp->so_reclaim_seqcount);
	up_read(&nfsi->rwsem);
	status = _nfs4_do_setlk(state, cmd, request, NFS_LOCK_NEW);
	if (status != 0)
		goto out;
	down_read(&nfsi->rwsem);
	if (read_seqcount_retry(&sp->so_reclaim_seqcount, seq)) {
		status = -NFS4ERR_DELAY;
		goto out_unlock;
	}
	/* Note: we always want to sleep here! */
	request->fl_flags = fl_flags | FL_SLEEP;
	if (do_vfs_lock(request->fl_file, request) < 0)
		printk(KERN_WARNING "NFS: %s: VFS is out of sync with lock "
			"manager!\n", __func__);
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
		trace_nfs4_set_lock(request, state, cmd, err);
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
	switch (request->fl_type) {
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

int nfs4_lock_delegation_recall(struct file_lock *fl, struct nfs4_state *state, const nfs4_stateid *stateid)
{
	struct nfs_server *server = NFS_SERVER(state->inode);
	int err;

	err = nfs4_set_lock_state(state, fl);
	if (err != 0)
		return err;
	err = _nfs4_do_setlk(state, F_SETLK, fl, NFS_LOCK_NEW);
	return nfs4_handle_delegation_recall_error(server, state, stateid, err);
}

struct nfs_release_lockowner_data {
	struct nfs4_lock_state *lsp;
	struct nfs_server *server;
	struct nfs_release_lockowner_args args;
	struct nfs_release_lockowner_res res;
	unsigned long timestamp;
};

static void nfs4_release_lockowner_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs_release_lockowner_data *data = calldata;
	struct nfs_server *server = data->server;
	nfs40_setup_sequence(server, &data->args.seq_args,
				&data->res.seq_res, task);
	data->args.lock_owner.clientid = server->nfs_client->cl_clientid;
	data->timestamp = jiffies;
}

static void nfs4_release_lockowner_done(struct rpc_task *task, void *calldata)
{
	struct nfs_release_lockowner_data *data = calldata;
	struct nfs_server *server = data->server;

	nfs40_sequence_done(task, &data->res.seq_res);

	switch (task->tk_status) {
	case 0:
		renew_lease(server, data->timestamp);
		break;
	case -NFS4ERR_STALE_CLIENTID:
	case -NFS4ERR_EXPIRED:
		nfs4_schedule_lease_recovery(server->nfs_client);
		break;
	case -NFS4ERR_LEASE_MOVED:
	case -NFS4ERR_DELAY:
		if (nfs4_async_handle_error(task, server,
					    NULL, NULL) == -EAGAIN)
			rpc_restart_call_prepare(task);
	}
}

static void nfs4_release_lockowner_release(void *calldata)
{
	struct nfs_release_lockowner_data *data = calldata;
	nfs4_free_lock_state(data->server, data->lsp);
	kfree(calldata);
}

static const struct rpc_call_ops nfs4_release_lockowner_ops = {
	.rpc_call_prepare = nfs4_release_lockowner_prepare,
	.rpc_call_done = nfs4_release_lockowner_done,
	.rpc_release = nfs4_release_lockowner_release,
};

static void
nfs4_release_lockowner(struct nfs_server *server, struct nfs4_lock_state *lsp)
{
	struct nfs_release_lockowner_data *data;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_RELEASE_LOCKOWNER],
	};

	if (server->nfs_client->cl_mvops->minor_version != 0)
		return;

	data = kmalloc(sizeof(*data), GFP_NOFS);
	if (!data)
		return;
	data->lsp = lsp;
	data->server = server;
	data->args.lock_owner.clientid = server->nfs_client->cl_clientid;
	data->args.lock_owner.id = lsp->ls_seqid.owner_id;
	data->args.lock_owner.s_dev = server->s_dev;

	msg.rpc_argp = &data->args;
	msg.rpc_resp = &data->res;
	nfs4_init_sequence(&data->args.seq_args, &data->res.seq_res, 0);
	rpc_call_async(server->client, &msg, 0, &nfs4_release_lockowner_ops, data);
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

#ifdef CONFIG_NFS_V4_SECURITY_LABEL
static inline int nfs4_server_supports_labels(struct nfs_server *server)
{
	return server->caps & NFS_CAP_SECURITY_LABEL;
}

static int nfs4_xattr_set_nfs4_label(struct dentry *dentry, const char *key,
				   const void *buf, size_t buflen,
				   int flags, int type)
{
	if (security_ismaclabel(key))
		return nfs4_set_security_label(dentry, buf, buflen);

	return -EOPNOTSUPP;
}

static int nfs4_xattr_get_nfs4_label(struct dentry *dentry, const char *key,
				   void *buf, size_t buflen, int type)
{
	if (security_ismaclabel(key))
		return nfs4_get_security_label(dentry->d_inode, buf, buflen);
	return -EOPNOTSUPP;
}

static size_t nfs4_xattr_list_nfs4_label(struct dentry *dentry, char *list,
				       size_t list_len, const char *name,
				       size_t name_len, int type)
{
	size_t len = 0;

	if (nfs_server_capable(dentry->d_inode, NFS_CAP_SECURITY_LABEL)) {
		len = security_inode_listsecurity(dentry->d_inode, NULL, 0);
		if (list && len <= list_len)
			security_inode_listsecurity(dentry->d_inode, list, len);
	}
	return len;
}

static const struct xattr_handler nfs4_xattr_nfs4_label_handler = {
	.prefix = XATTR_SECURITY_PREFIX,
	.list	= nfs4_xattr_list_nfs4_label,
	.get	= nfs4_xattr_get_nfs4_label,
	.set	= nfs4_xattr_set_nfs4_label,
};
#endif


/*
 * nfs_fhget will use either the mounted_on_fileid or the fileid
 */
static void nfs_fixup_referral_attributes(struct nfs_fattr *fattr)
{
	if (!(((fattr->valid & NFS_ATTR_FATTR_MOUNTED_ON_FILEID) ||
	       (fattr->valid & NFS_ATTR_FATTR_FILEID)) &&
	      (fattr->valid & NFS_ATTR_FATTR_FSID) &&
	      (fattr->valid & NFS_ATTR_FATTR_V4_LOCATIONS)))
		return;

	fattr->valid |= NFS_ATTR_FATTR_TYPE | NFS_ATTR_FATTR_MODE |
		NFS_ATTR_FATTR_NLINK | NFS_ATTR_FATTR_V4_REFERRAL;
	fattr->mode = S_IFDIR | S_IRUGO | S_IXUGO;
	fattr->nlink = 2;
}

static int _nfs4_proc_fs_locations(struct rpc_clnt *client, struct inode *dir,
				   const struct qstr *name,
				   struct nfs4_fs_locations *fs_locations,
				   struct page *page)
{
	struct nfs_server *server = NFS_SERVER(dir);
	u32 bitmask[3] = {
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
	status = nfs4_call_sync(client, server, &msg, &args.seq_args, &res.seq_res, 0);
	dprintk("%s: returned status = %d\n", __func__, status);
	return status;
}

int nfs4_proc_fs_locations(struct rpc_clnt *client, struct inode *dir,
			   const struct qstr *name,
			   struct nfs4_fs_locations *fs_locations,
			   struct page *page)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = _nfs4_proc_fs_locations(client, dir, name,
				fs_locations, page);
		trace_nfs4_get_fs_locations(dir, name, err);
		err = nfs4_handle_exception(NFS_SERVER(dir), err,
				&exception);
	} while (exception.retry);
	return err;
}

/*
 * This operation also signals the server that this client is
 * performing migration recovery.  The server can stop returning
 * NFS4ERR_LEASE_MOVED to this client.  A RENEW operation is
 * appended to this compound to identify the client ID which is
 * performing recovery.
 */
static int _nfs40_proc_get_locations(struct inode *inode,
				     struct nfs4_fs_locations *locations,
				     struct page *page, struct rpc_cred *cred)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct rpc_clnt *clnt = server->client;
	u32 bitmask[2] = {
		[0] = FATTR4_WORD0_FSID | FATTR4_WORD0_FS_LOCATIONS,
	};
	struct nfs4_fs_locations_arg args = {
		.clientid	= server->nfs_client->cl_clientid,
		.fh		= NFS_FH(inode),
		.page		= page,
		.bitmask	= bitmask,
		.migration	= 1,		/* skip LOOKUP */
		.renew		= 1,		/* append RENEW */
	};
	struct nfs4_fs_locations_res res = {
		.fs_locations	= locations,
		.migration	= 1,
		.renew		= 1,
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_FS_LOCATIONS],
		.rpc_argp	= &args,
		.rpc_resp	= &res,
		.rpc_cred	= cred,
	};
	unsigned long now = jiffies;
	int status;

	nfs_fattr_init(&locations->fattr);
	locations->server = server;
	locations->nlocations = 0;

	nfs4_init_sequence(&args.seq_args, &res.seq_res, 0);
	nfs4_set_sequence_privileged(&args.seq_args);
	status = nfs4_call_sync_sequence(clnt, server, &msg,
					&args.seq_args, &res.seq_res);
	if (status)
		return status;

	renew_lease(server, now);
	return 0;
}

#ifdef CONFIG_NFS_V4_1

/*
 * This operation also signals the server that this client is
 * performing migration recovery.  The server can stop asserting
 * SEQ4_STATUS_LEASE_MOVED for this client.  The client ID
 * performing this operation is identified in the SEQUENCE
 * operation in this compound.
 *
 * When the client supports GETATTR(fs_locations_info), it can
 * be plumbed in here.
 */
static int _nfs41_proc_get_locations(struct inode *inode,
				     struct nfs4_fs_locations *locations,
				     struct page *page, struct rpc_cred *cred)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct rpc_clnt *clnt = server->client;
	u32 bitmask[2] = {
		[0] = FATTR4_WORD0_FSID | FATTR4_WORD0_FS_LOCATIONS,
	};
	struct nfs4_fs_locations_arg args = {
		.fh		= NFS_FH(inode),
		.page		= page,
		.bitmask	= bitmask,
		.migration	= 1,		/* skip LOOKUP */
	};
	struct nfs4_fs_locations_res res = {
		.fs_locations	= locations,
		.migration	= 1,
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_FS_LOCATIONS],
		.rpc_argp	= &args,
		.rpc_resp	= &res,
		.rpc_cred	= cred,
	};
	int status;

	nfs_fattr_init(&locations->fattr);
	locations->server = server;
	locations->nlocations = 0;

	nfs4_init_sequence(&args.seq_args, &res.seq_res, 0);
	nfs4_set_sequence_privileged(&args.seq_args);
	status = nfs4_call_sync_sequence(clnt, server, &msg,
					&args.seq_args, &res.seq_res);
	if (status == NFS4_OK &&
	    res.seq_res.sr_status_flags & SEQ4_STATUS_LEASE_MOVED)
		status = -NFS4ERR_LEASE_MOVED;
	return status;
}

#endif	/* CONFIG_NFS_V4_1 */

/**
 * nfs4_proc_get_locations - discover locations for a migrated FSID
 * @inode: inode on FSID that is migrating
 * @locations: result of query
 * @page: buffer
 * @cred: credential to use for this operation
 *
 * Returns NFS4_OK on success, a negative NFS4ERR status code if the
 * operation failed, or a negative errno if a local error occurred.
 *
 * On success, "locations" is filled in, but if the server has
 * no locations information, NFS_ATTR_FATTR_V4_LOCATIONS is not
 * asserted.
 *
 * -NFS4ERR_LEASE_MOVED is returned if the server still has leases
 * from this client that require migration recovery.
 */
int nfs4_proc_get_locations(struct inode *inode,
			    struct nfs4_fs_locations *locations,
			    struct page *page, struct rpc_cred *cred)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_client *clp = server->nfs_client;
	const struct nfs4_mig_recovery_ops *ops =
					clp->cl_mvops->mig_recovery_ops;
	struct nfs4_exception exception = { };
	int status;

	dprintk("%s: FSID %llx:%llx on \"%s\"\n", __func__,
		(unsigned long long)server->fsid.major,
		(unsigned long long)server->fsid.minor,
		clp->cl_hostname);
	nfs_display_fhandle(NFS_FH(inode), __func__);

	do {
		status = ops->get_locations(inode, locations, page, cred);
		if (status != -NFS4ERR_DELAY)
			break;
		nfs4_handle_exception(server, status, &exception);
	} while (exception.retry);
	return status;
}

/*
 * This operation also signals the server that this client is
 * performing "lease moved" recovery.  The server can stop
 * returning NFS4ERR_LEASE_MOVED to this client.  A RENEW operation
 * is appended to this compound to identify the client ID which is
 * performing recovery.
 */
static int _nfs40_proc_fsid_present(struct inode *inode, struct rpc_cred *cred)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_client *clp = NFS_SERVER(inode)->nfs_client;
	struct rpc_clnt *clnt = server->client;
	struct nfs4_fsid_present_arg args = {
		.fh		= NFS_FH(inode),
		.clientid	= clp->cl_clientid,
		.renew		= 1,		/* append RENEW */
	};
	struct nfs4_fsid_present_res res = {
		.renew		= 1,
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_FSID_PRESENT],
		.rpc_argp	= &args,
		.rpc_resp	= &res,
		.rpc_cred	= cred,
	};
	unsigned long now = jiffies;
	int status;

	res.fh = nfs_alloc_fhandle();
	if (res.fh == NULL)
		return -ENOMEM;

	nfs4_init_sequence(&args.seq_args, &res.seq_res, 0);
	nfs4_set_sequence_privileged(&args.seq_args);
	status = nfs4_call_sync_sequence(clnt, server, &msg,
						&args.seq_args, &res.seq_res);
	nfs_free_fhandle(res.fh);
	if (status)
		return status;

	do_renew_lease(clp, now);
	return 0;
}

#ifdef CONFIG_NFS_V4_1

/*
 * This operation also signals the server that this client is
 * performing "lease moved" recovery.  The server can stop asserting
 * SEQ4_STATUS_LEASE_MOVED for this client.  The client ID performing
 * this operation is identified in the SEQUENCE operation in this
 * compound.
 */
static int _nfs41_proc_fsid_present(struct inode *inode, struct rpc_cred *cred)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct rpc_clnt *clnt = server->client;
	struct nfs4_fsid_present_arg args = {
		.fh		= NFS_FH(inode),
	};
	struct nfs4_fsid_present_res res = {
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_FSID_PRESENT],
		.rpc_argp	= &args,
		.rpc_resp	= &res,
		.rpc_cred	= cred,
	};
	int status;

	res.fh = nfs_alloc_fhandle();
	if (res.fh == NULL)
		return -ENOMEM;

	nfs4_init_sequence(&args.seq_args, &res.seq_res, 0);
	nfs4_set_sequence_privileged(&args.seq_args);
	status = nfs4_call_sync_sequence(clnt, server, &msg,
						&args.seq_args, &res.seq_res);
	nfs_free_fhandle(res.fh);
	if (status == NFS4_OK &&
	    res.seq_res.sr_status_flags & SEQ4_STATUS_LEASE_MOVED)
		status = -NFS4ERR_LEASE_MOVED;
	return status;
}

#endif	/* CONFIG_NFS_V4_1 */

/**
 * nfs4_proc_fsid_present - Is this FSID present or absent on server?
 * @inode: inode on FSID to check
 * @cred: credential to use for this operation
 *
 * Server indicates whether the FSID is present, moved, or not
 * recognized.  This operation is necessary to clear a LEASE_MOVED
 * condition for this client ID.
 *
 * Returns NFS4_OK if the FSID is present on this server,
 * -NFS4ERR_MOVED if the FSID is no longer present, a negative
 *  NFS4ERR code if some error occurred on the server, or a
 *  negative errno if a local failure occurred.
 */
int nfs4_proc_fsid_present(struct inode *inode, struct rpc_cred *cred)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_client *clp = server->nfs_client;
	const struct nfs4_mig_recovery_ops *ops =
					clp->cl_mvops->mig_recovery_ops;
	struct nfs4_exception exception = { };
	int status;

	dprintk("%s: FSID %llx:%llx on \"%s\"\n", __func__,
		(unsigned long long)server->fsid.major,
		(unsigned long long)server->fsid.minor,
		clp->cl_hostname);
	nfs_display_fhandle(NFS_FH(inode), __func__);

	do {
		status = ops->fsid_present(inode, cred);
		if (status != -NFS4ERR_DELAY)
			break;
		nfs4_handle_exception(server, status, &exception);
	} while (exception.retry);
	return status;
}

/**
 * If 'use_integrity' is true and the state managment nfs_client
 * cl_rpcclient is using krb5i/p, use the integrity protected cl_rpcclient
 * and the machine credential as per RFC3530bis and RFC5661 Security
 * Considerations sections. Otherwise, just use the user cred with the
 * filesystem's rpc_client.
 */
static int _nfs4_proc_secinfo(struct inode *dir, const struct qstr *name, struct nfs4_secinfo_flavors *flavors, bool use_integrity)
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
	struct rpc_clnt *clnt = NFS_SERVER(dir)->client;
	struct rpc_cred *cred = NULL;

	if (use_integrity) {
		clnt = NFS_SERVER(dir)->nfs_client->cl_rpcclient;
		cred = nfs4_get_clid_cred(NFS_SERVER(dir)->nfs_client);
		msg.rpc_cred = cred;
	}

	dprintk("NFS call  secinfo %s\n", name->name);

	nfs4_state_protect(NFS_SERVER(dir)->nfs_client,
		NFS_SP4_MACH_CRED_SECINFO, &clnt, &msg);

	status = nfs4_call_sync(clnt, NFS_SERVER(dir), &msg, &args.seq_args,
				&res.seq_res, 0);
	dprintk("NFS reply  secinfo: %d\n", status);

	if (cred)
		put_rpccred(cred);

	return status;
}

int nfs4_proc_secinfo(struct inode *dir, const struct qstr *name,
		      struct nfs4_secinfo_flavors *flavors)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = -NFS4ERR_WRONGSEC;

		/* try to use integrity protection with machine cred */
		if (_nfs4_is_integrity_protected(NFS_SERVER(dir)->nfs_client))
			err = _nfs4_proc_secinfo(dir, name, flavors, true);

		/*
		 * if unable to use integrity protection, or SECINFO with
		 * integrity protection returns NFS4ERR_WRONGSEC (which is
		 * disallowed by spec, but exists in deployed servers) use
		 * the current filesystem's rpc_client and the user cred.
		 */
		if (err == -NFS4ERR_WRONGSEC)
			err = _nfs4_proc_secinfo(dir, name, flavors, false);

		trace_nfs4_secinfo(dir, name, err);
		err = nfs4_handle_exception(NFS_SERVER(dir), err,
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

static bool
nfs41_same_server_scope(struct nfs41_server_scope *a,
			struct nfs41_server_scope *b)
{
	if (a->server_scope_sz == b->server_scope_sz &&
	    memcmp(a->server_scope, b->server_scope, a->server_scope_sz) == 0)
		return true;

	return false;
}

/*
 * nfs4_proc_bind_conn_to_session()
 *
 * The 4.1 client currently uses the same TCP connection for the
 * fore and backchannel.
 */
int nfs4_proc_bind_conn_to_session(struct nfs_client *clp, struct rpc_cred *cred)
{
	int status;
	struct nfs41_bind_conn_to_session_res res;
	struct rpc_message msg = {
		.rpc_proc =
			&nfs4_procedures[NFSPROC4_CLNT_BIND_CONN_TO_SESSION],
		.rpc_argp = clp,
		.rpc_resp = &res,
		.rpc_cred = cred,
	};

	dprintk("--> %s\n", __func__);

	res.session = kzalloc(sizeof(struct nfs4_session), GFP_NOFS);
	if (unlikely(res.session == NULL)) {
		status = -ENOMEM;
		goto out;
	}

	status = rpc_call_sync(clp->cl_rpcclient, &msg, RPC_TASK_TIMEOUT);
	trace_nfs4_bind_conn_to_session(clp, status);
	if (status == 0) {
		if (memcmp(res.session->sess_id.data,
		    clp->cl_session->sess_id.data, NFS4_MAX_SESSIONID_LEN)) {
			dprintk("NFS: %s: Session ID mismatch\n", __func__);
			status = -EIO;
			goto out_session;
		}
		if (res.dir != NFS4_CDFS4_BOTH) {
			dprintk("NFS: %s: Unexpected direction from server\n",
				__func__);
			status = -EIO;
			goto out_session;
		}
		if (res.use_conn_in_rdma_mode) {
			dprintk("NFS: %s: Server returned RDMA mode = true\n",
				__func__);
			status = -EIO;
			goto out_session;
		}
	}
out_session:
	kfree(res.session);
out:
	dprintk("<-- %s status= %d\n", __func__, status);
	return status;
}

/*
 * Minimum set of SP4_MACH_CRED operations from RFC 5661 in the enforce map
 * and operations we'd like to see to enable certain features in the allow map
 */
static const struct nfs41_state_protection nfs4_sp4_mach_cred_request = {
	.how = SP4_MACH_CRED,
	.enforce.u.words = {
		[1] = 1 << (OP_BIND_CONN_TO_SESSION - 32) |
		      1 << (OP_EXCHANGE_ID - 32) |
		      1 << (OP_CREATE_SESSION - 32) |
		      1 << (OP_DESTROY_SESSION - 32) |
		      1 << (OP_DESTROY_CLIENTID - 32)
	},
	.allow.u.words = {
		[0] = 1 << (OP_CLOSE) |
		      1 << (OP_LOCKU) |
		      1 << (OP_COMMIT),
		[1] = 1 << (OP_SECINFO - 32) |
		      1 << (OP_SECINFO_NO_NAME - 32) |
		      1 << (OP_TEST_STATEID - 32) |
		      1 << (OP_FREE_STATEID - 32) |
		      1 << (OP_WRITE - 32)
	}
};

/*
 * Select the state protection mode for client `clp' given the server results
 * from exchange_id in `sp'.
 *
 * Returns 0 on success, negative errno otherwise.
 */
static int nfs4_sp4_select_mode(struct nfs_client *clp,
				 struct nfs41_state_protection *sp)
{
	static const u32 supported_enforce[NFS4_OP_MAP_NUM_WORDS] = {
		[1] = 1 << (OP_BIND_CONN_TO_SESSION - 32) |
		      1 << (OP_EXCHANGE_ID - 32) |
		      1 << (OP_CREATE_SESSION - 32) |
		      1 << (OP_DESTROY_SESSION - 32) |
		      1 << (OP_DESTROY_CLIENTID - 32)
	};
	unsigned int i;

	if (sp->how == SP4_MACH_CRED) {
		/* Print state protect result */
		dfprintk(MOUNT, "Server SP4_MACH_CRED support:\n");
		for (i = 0; i <= LAST_NFS4_OP; i++) {
			if (test_bit(i, sp->enforce.u.longs))
				dfprintk(MOUNT, "  enforce op %d\n", i);
			if (test_bit(i, sp->allow.u.longs))
				dfprintk(MOUNT, "  allow op %d\n", i);
		}

		/* make sure nothing is on enforce list that isn't supported */
		for (i = 0; i < NFS4_OP_MAP_NUM_WORDS; i++) {
			if (sp->enforce.u.words[i] & ~supported_enforce[i]) {
				dfprintk(MOUNT, "sp4_mach_cred: disabled\n");
				return -EINVAL;
			}
		}

		/*
		 * Minimal mode - state operations are allowed to use machine
		 * credential.  Note this already happens by default, so the
		 * client doesn't have to do anything more than the negotiation.
		 *
		 * NOTE: we don't care if EXCHANGE_ID is in the list -
		 *       we're already using the machine cred for exchange_id
		 *       and will never use a different cred.
		 */
		if (test_bit(OP_BIND_CONN_TO_SESSION, sp->enforce.u.longs) &&
		    test_bit(OP_CREATE_SESSION, sp->enforce.u.longs) &&
		    test_bit(OP_DESTROY_SESSION, sp->enforce.u.longs) &&
		    test_bit(OP_DESTROY_CLIENTID, sp->enforce.u.longs)) {
			dfprintk(MOUNT, "sp4_mach_cred:\n");
			dfprintk(MOUNT, "  minimal mode enabled\n");
			set_bit(NFS_SP4_MACH_CRED_MINIMAL, &clp->cl_sp4_flags);
		} else {
			dfprintk(MOUNT, "sp4_mach_cred: disabled\n");
			return -EINVAL;
		}

		if (test_bit(OP_CLOSE, sp->allow.u.longs) &&
		    test_bit(OP_LOCKU, sp->allow.u.longs)) {
			dfprintk(MOUNT, "  cleanup mode enabled\n");
			set_bit(NFS_SP4_MACH_CRED_CLEANUP, &clp->cl_sp4_flags);
		}

		if (test_bit(OP_SECINFO, sp->allow.u.longs) &&
		    test_bit(OP_SECINFO_NO_NAME, sp->allow.u.longs)) {
			dfprintk(MOUNT, "  secinfo mode enabled\n");
			set_bit(NFS_SP4_MACH_CRED_SECINFO, &clp->cl_sp4_flags);
		}

		if (test_bit(OP_TEST_STATEID, sp->allow.u.longs) &&
		    test_bit(OP_FREE_STATEID, sp->allow.u.longs)) {
			dfprintk(MOUNT, "  stateid mode enabled\n");
			set_bit(NFS_SP4_MACH_CRED_STATEID, &clp->cl_sp4_flags);
		}

		if (test_bit(OP_WRITE, sp->allow.u.longs)) {
			dfprintk(MOUNT, "  write mode enabled\n");
			set_bit(NFS_SP4_MACH_CRED_WRITE, &clp->cl_sp4_flags);
		}

		if (test_bit(OP_COMMIT, sp->allow.u.longs)) {
			dfprintk(MOUNT, "  commit mode enabled\n");
			set_bit(NFS_SP4_MACH_CRED_COMMIT, &clp->cl_sp4_flags);
		}
	}

	return 0;
}

/*
 * _nfs4_proc_exchange_id()
 *
 * Wrapper for EXCHANGE_ID operation.
 */
static int _nfs4_proc_exchange_id(struct nfs_client *clp, struct rpc_cred *cred,
	u32 sp4_how)
{
	nfs4_verifier verifier;
	struct nfs41_exchange_id_args args = {
		.verifier = &verifier,
		.client = clp,
#ifdef CONFIG_NFS_V4_1_MIGRATION
		.flags = EXCHGID4_FLAG_SUPP_MOVED_REFER |
			 EXCHGID4_FLAG_BIND_PRINC_STATEID |
			 EXCHGID4_FLAG_SUPP_MOVED_MIGR,
#else
		.flags = EXCHGID4_FLAG_SUPP_MOVED_REFER |
			 EXCHGID4_FLAG_BIND_PRINC_STATEID,
#endif
	};
	struct nfs41_exchange_id_res res = {
		0
	};
	int status;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_EXCHANGE_ID],
		.rpc_argp = &args,
		.rpc_resp = &res,
		.rpc_cred = cred,
	};

	nfs4_init_boot_verifier(clp, &verifier);
	args.id_len = nfs4_init_uniform_client_string(clp, args.id,
							sizeof(args.id));
	dprintk("NFS call  exchange_id auth=%s, '%.*s'\n",
		clp->cl_rpcclient->cl_auth->au_ops->au_name,
		args.id_len, args.id);

	res.server_owner = kzalloc(sizeof(struct nfs41_server_owner),
					GFP_NOFS);
	if (unlikely(res.server_owner == NULL)) {
		status = -ENOMEM;
		goto out;
	}

	res.server_scope = kzalloc(sizeof(struct nfs41_server_scope),
					GFP_NOFS);
	if (unlikely(res.server_scope == NULL)) {
		status = -ENOMEM;
		goto out_server_owner;
	}

	res.impl_id = kzalloc(sizeof(struct nfs41_impl_id), GFP_NOFS);
	if (unlikely(res.impl_id == NULL)) {
		status = -ENOMEM;
		goto out_server_scope;
	}

	switch (sp4_how) {
	case SP4_NONE:
		args.state_protect.how = SP4_NONE;
		break;

	case SP4_MACH_CRED:
		args.state_protect = nfs4_sp4_mach_cred_request;
		break;

	default:
		/* unsupported! */
		WARN_ON_ONCE(1);
		status = -EINVAL;
		goto out_server_scope;
	}

	status = rpc_call_sync(clp->cl_rpcclient, &msg, RPC_TASK_TIMEOUT);
	trace_nfs4_exchange_id(clp, status);
	if (status == 0)
		status = nfs4_check_cl_exchange_flags(res.flags);

	if (status == 0)
		status = nfs4_sp4_select_mode(clp, &res.state_protect);

	if (status == 0) {
		clp->cl_clientid = res.clientid;
		clp->cl_exchange_flags = (res.flags & ~EXCHGID4_FLAG_CONFIRMED_R);
		if (!(res.flags & EXCHGID4_FLAG_CONFIRMED_R))
			clp->cl_seqid = res.seqid;

		kfree(clp->cl_serverowner);
		clp->cl_serverowner = res.server_owner;
		res.server_owner = NULL;

		/* use the most recent implementation id */
		kfree(clp->cl_implid);
		clp->cl_implid = res.impl_id;

		if (clp->cl_serverscope != NULL &&
		    !nfs41_same_server_scope(clp->cl_serverscope,
					     res.server_scope)) {
			dprintk("%s: server_scope mismatch detected\n",
				__func__);
			set_bit(NFS4CLNT_SERVER_SCOPE_MISMATCH, &clp->cl_state);
			kfree(clp->cl_serverscope);
			clp->cl_serverscope = NULL;
		}

		if (clp->cl_serverscope == NULL) {
			clp->cl_serverscope = res.server_scope;
			goto out;
		}
	} else
		kfree(res.impl_id);

out_server_owner:
	kfree(res.server_owner);
out_server_scope:
	kfree(res.server_scope);
out:
	if (clp->cl_implid != NULL)
		dprintk("NFS reply exchange_id: Server Implementation ID: "
			"domain: %s, name: %s, date: %llu,%u\n",
			clp->cl_implid->domain, clp->cl_implid->name,
			clp->cl_implid->date.seconds,
			clp->cl_implid->date.nseconds);
	dprintk("NFS reply exchange_id: %d\n", status);
	return status;
}

/*
 * nfs4_proc_exchange_id()
 *
 * Returns zero, a negative errno, or a negative NFS4ERR status code.
 *
 * Since the clientid has expired, all compounds using sessions
 * associated with the stale clientid will be returning
 * NFS4ERR_BADSESSION in the sequence operation, and will therefore
 * be in some phase of session reset.
 *
 * Will attempt to negotiate SP4_MACH_CRED if krb5i / krb5p auth is used.
 */
int nfs4_proc_exchange_id(struct nfs_client *clp, struct rpc_cred *cred)
{
	rpc_authflavor_t authflavor = clp->cl_rpcclient->cl_auth->au_flavor;
	int status;

	/* try SP4_MACH_CRED if krb5i/p	*/
	if (authflavor == RPC_AUTH_GSS_KRB5I ||
	    authflavor == RPC_AUTH_GSS_KRB5P) {
		status = _nfs4_proc_exchange_id(clp, cred, SP4_MACH_CRED);
		if (!status)
			return 0;
	}

	/* try SP4_NONE */
	return _nfs4_proc_exchange_id(clp, cred, SP4_NONE);
}

static int _nfs4_proc_destroy_clientid(struct nfs_client *clp,
		struct rpc_cred *cred)
{
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_DESTROY_CLIENTID],
		.rpc_argp = clp,
		.rpc_cred = cred,
	};
	int status;

	status = rpc_call_sync(clp->cl_rpcclient, &msg, RPC_TASK_TIMEOUT);
	trace_nfs4_destroy_clientid(clp, status);
	if (status)
		dprintk("NFS: Got error %d from the server %s on "
			"DESTROY_CLIENTID.", status, clp->cl_hostname);
	return status;
}

static int nfs4_proc_destroy_clientid(struct nfs_client *clp,
		struct rpc_cred *cred)
{
	unsigned int loop;
	int ret;

	for (loop = NFS4_MAX_LOOP_ON_RECOVER; loop != 0; loop--) {
		ret = _nfs4_proc_destroy_clientid(clp, cred);
		switch (ret) {
		case -NFS4ERR_DELAY:
		case -NFS4ERR_CLIENTID_BUSY:
			ssleep(1);
			break;
		default:
			return ret;
		}
	}
	return 0;
}

int nfs4_destroy_clientid(struct nfs_client *clp)
{
	struct rpc_cred *cred;
	int ret = 0;

	if (clp->cl_mvops->minor_version < 1)
		goto out;
	if (clp->cl_exchange_flags == 0)
		goto out;
	if (clp->cl_preserve_clid)
		goto out;
	cred = nfs4_get_clid_cred(clp);
	ret = nfs4_proc_destroy_clientid(clp, cred);
	if (cred)
		put_rpccred(cred);
	switch (ret) {
	case 0:
	case -NFS4ERR_STALE_CLIENTID:
		clp->cl_exchange_flags = 0;
	}
out:
	return ret;
}

struct nfs4_get_lease_time_data {
	struct nfs4_get_lease_time_args *args;
	struct nfs4_get_lease_time_res *res;
	struct nfs_client *clp;
};

static void nfs4_get_lease_time_prepare(struct rpc_task *task,
					void *calldata)
{
	struct nfs4_get_lease_time_data *data =
			(struct nfs4_get_lease_time_data *)calldata;

	dprintk("--> %s\n", __func__);
	/* just setup sequence, do not trigger session recovery
	   since we're invoked within one */
	nfs41_setup_sequence(data->clp->cl_session,
			&data->args->la_seq_args,
			&data->res->lr_seq_res,
			task);
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
		rpc_restart_call_prepare(task);
		return;
	}
	dprintk("<-- %s\n", __func__);
}

static const struct rpc_call_ops nfs4_get_lease_time_ops = {
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

	nfs4_init_sequence(&args.la_seq_args, &res.lr_seq_res, 0);
	nfs4_set_sequence_privileged(&args.la_seq_args);
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
	unsigned int max_rqst_sz, max_resp_sz;

	max_rqst_sz = NFS_MAX_FILE_IO_SIZE + nfs41_maxwrite_overhead;
	max_resp_sz = NFS_MAX_FILE_IO_SIZE + nfs41_maxread_overhead;

	/* Fore channel attributes */
	args->fc_attrs.max_rqst_sz = max_rqst_sz;
	args->fc_attrs.max_resp_sz = max_resp_sz;
	args->fc_attrs.max_ops = NFS4_MAX_OPS;
	args->fc_attrs.max_reqs = max_session_slots;

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
	if (rcvd->max_reqs > NFS4_MAX_SLOT_TABLE)
		rcvd->max_reqs = NFS4_MAX_SLOT_TABLE;
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
	if (rcvd->max_ops != sent->max_ops)
		return -EINVAL;
	if (rcvd->max_reqs != sent->max_reqs)
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

static int _nfs4_proc_create_session(struct nfs_client *clp,
		struct rpc_cred *cred)
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
		.rpc_cred = cred,
	};
	int status;

	nfs4_init_channel_attrs(&args);
	args.flags = (SESSION4_PERSIST | SESSION4_BACK_CHAN);

	status = rpc_call_sync(session->clp->cl_rpcclient, &msg, RPC_TASK_TIMEOUT);
	trace_nfs4_create_session(clp, status);

	if (!status) {
		/* Verify the session's negotiated channel_attrs values */
		status = nfs4_verify_channel_attrs(&args, session);
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
int nfs4_proc_create_session(struct nfs_client *clp, struct rpc_cred *cred)
{
	int status;
	unsigned *ptr;
	struct nfs4_session *session = clp->cl_session;

	dprintk("--> %s clp=%p session=%p\n", __func__, clp, session);

	status = _nfs4_proc_create_session(clp, cred);
	if (status)
		goto out;

	/* Init or reset the session slot tables */
	status = nfs4_setup_session_slot_tables(session);
	dprintk("slot table setup returned %d\n", status);
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
int nfs4_proc_destroy_session(struct nfs4_session *session,
		struct rpc_cred *cred)
{
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_DESTROY_SESSION],
		.rpc_argp = session,
		.rpc_cred = cred,
	};
	int status = 0;

	dprintk("--> nfs4_proc_destroy_session\n");

	/* session is still being setup */
	if (session->clp->cl_cons_state != NFS_CS_READY)
		return status;

	status = rpc_call_sync(session->clp->cl_rpcclient, &msg, RPC_TASK_TIMEOUT);
	trace_nfs4_destroy_session(session->clp, status);

	if (status)
		dprintk("NFS: Got error %d from the server on DESTROY_SESSION. "
			"Session has been destroyed regardless...\n", status);

	dprintk("<-- nfs4_proc_destroy_session\n");
	return status;
}

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

	trace_nfs4_sequence(clp, task->tk_status);
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

	nfs41_setup_sequence(clp->cl_session, args, res, task);
}

static const struct rpc_call_ops nfs41_sequence_ops = {
	.rpc_call_done = nfs41_sequence_call_done,
	.rpc_call_prepare = nfs41_sequence_prepare,
	.rpc_release = nfs41_sequence_release,
};

static struct rpc_task *_nfs41_proc_sequence(struct nfs_client *clp,
		struct rpc_cred *cred,
		bool is_privileged)
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
		.flags = RPC_TASK_ASYNC | RPC_TASK_TIMEOUT,
	};

	if (!atomic_inc_not_zero(&clp->cl_count))
		return ERR_PTR(-EIO);
	calldata = kzalloc(sizeof(*calldata), GFP_NOFS);
	if (calldata == NULL) {
		nfs_put_client(clp);
		return ERR_PTR(-ENOMEM);
	}
	nfs4_init_sequence(&calldata->args, &calldata->res, 0);
	if (is_privileged)
		nfs4_set_sequence_privileged(&calldata->args);
	msg.rpc_argp = &calldata->args;
	msg.rpc_resp = &calldata->res;
	calldata->clp = clp;
	task_setup_data.callback_data = calldata;

	return rpc_run_task(&task_setup_data);
}

static int nfs41_proc_async_sequence(struct nfs_client *clp, struct rpc_cred *cred, unsigned renew_flags)
{
	struct rpc_task *task;
	int ret = 0;

	if ((renew_flags & NFS4_RENEW_TIMEOUT) == 0)
		return -EAGAIN;
	task = _nfs41_proc_sequence(clp, cred, false);
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

	task = _nfs41_proc_sequence(clp, cred, true);
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

	nfs41_setup_sequence(calldata->clp->cl_session,
			&calldata->arg.seq_args,
			&calldata->res.seq_res,
			task);
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

	trace_nfs4_reclaim_complete(clp, task->tk_status);
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
static int nfs41_proc_reclaim_complete(struct nfs_client *clp,
		struct rpc_cred *cred)
{
	struct nfs4_reclaim_complete_data *calldata;
	struct rpc_task *task;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_RECLAIM_COMPLETE],
		.rpc_cred = cred,
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

	nfs4_init_sequence(&calldata->arg.seq_args, &calldata->res.seq_res, 0);
	nfs4_set_sequence_privileged(&calldata->arg.seq_args);
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
	struct nfs4_session *session = nfs4_get_session(server);

	dprintk("--> %s\n", __func__);
	/* Note the is a race here, where a CB_LAYOUTRECALL can come in
	 * right now covering the LAYOUTGET we are about to send.
	 * However, that is not so catastrophic, and there seems
	 * to be no way to prevent it completely.
	 */
	if (nfs41_setup_sequence(session, &lgp->args.seq_args,
				&lgp->res.seq_res, task))
		return;
	if (pnfs_choose_layoutget_stateid(&lgp->args.stateid,
					  NFS_I(lgp->args.inode)->layout,
					  lgp->args.ctx->state)) {
		rpc_exit(task, NFS4_OK);
	}
}

static void nfs4_layoutget_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_layoutget *lgp = calldata;
	struct inode *inode = lgp->args.inode;
	struct nfs_server *server = NFS_SERVER(inode);
	struct pnfs_layout_hdr *lo;
	struct nfs4_state *state = NULL;
	unsigned long timeo, now, giveup;

	dprintk("--> %s tk_status => %d\n", __func__, -task->tk_status);

	if (!nfs41_sequence_done(task, &lgp->res.seq_res))
		goto out;

	switch (task->tk_status) {
	case 0:
		goto out;
	/*
	 * NFS4ERR_LAYOUTTRYLATER is a conflict with another client
	 * (or clients) writing to the same RAID stripe
	 */
	case -NFS4ERR_LAYOUTTRYLATER:
	/*
	 * NFS4ERR_RECALLCONFLICT is when conflict with self (must recall
	 * existing layout before getting a new one).
	 */
	case -NFS4ERR_RECALLCONFLICT:
		timeo = rpc_get_timeout(task->tk_client);
		giveup = lgp->args.timestamp + timeo;
		now = jiffies;
		if (time_after(giveup, now)) {
			unsigned long delay;

			/* Delay for:
			 * - Not less then NFS4_POLL_RETRY_MIN.
			 * - One last time a jiffie before we give up
			 * - exponential backoff (time_now minus start_attempt)
			 */
			delay = max_t(unsigned long, NFS4_POLL_RETRY_MIN,
				    min((giveup - now - 1),
					now - lgp->args.timestamp));

			dprintk("%s: NFS4ERR_RECALLCONFLICT waiting %lu\n",
				__func__, delay);
			rpc_delay(task, delay);
			task->tk_status = 0;
			rpc_restart_call_prepare(task);
			goto out; /* Do not call nfs4_async_handle_error() */
		}
		break;
	case -NFS4ERR_EXPIRED:
	case -NFS4ERR_BAD_STATEID:
		spin_lock(&inode->i_lock);
		lo = NFS_I(inode)->layout;
		if (!lo || list_empty(&lo->plh_segs)) {
			spin_unlock(&inode->i_lock);
			/* If the open stateid was bad, then recover it. */
			state = lgp->args.ctx->state;
		} else {
			LIST_HEAD(head);

			/*
			 * Mark the bad layout state as invalid, then retry
			 * with the current stateid.
			 */
			pnfs_mark_matching_lsegs_invalid(lo, &head, NULL);
			spin_unlock(&inode->i_lock);
			pnfs_free_lseg_list(&head);
	
			task->tk_status = 0;
			rpc_restart_call_prepare(task);
		}
	}
	if (nfs4_async_handle_error(task, server, state, NULL) == -EAGAIN)
		rpc_restart_call_prepare(task);
out:
	dprintk("<-- %s\n", __func__);
}

static size_t max_response_pages(struct nfs_server *server)
{
	u32 max_resp_sz = server->nfs_client->cl_session->fc_attrs.max_resp_sz;
	return nfs_page_array_len(0, max_resp_sz);
}

static void nfs4_free_pages(struct page **pages, size_t size)
{
	int i;

	if (!pages)
		return;

	for (i = 0; i < size; i++) {
		if (!pages[i])
			break;
		__free_page(pages[i]);
	}
	kfree(pages);
}

static struct page **nfs4_alloc_pages(size_t size, gfp_t gfp_flags)
{
	struct page **pages;
	int i;

	pages = kcalloc(size, sizeof(struct page *), gfp_flags);
	if (!pages) {
		dprintk("%s: can't alloc array of %zu pages\n", __func__, size);
		return NULL;
	}

	for (i = 0; i < size; i++) {
		pages[i] = alloc_page(gfp_flags);
		if (!pages[i]) {
			dprintk("%s: failed to allocate page\n", __func__);
			nfs4_free_pages(pages, size);
			return NULL;
		}
	}

	return pages;
}

static void nfs4_layoutget_release(void *calldata)
{
	struct nfs4_layoutget *lgp = calldata;
	struct inode *inode = lgp->args.inode;
	struct nfs_server *server = NFS_SERVER(inode);
	size_t max_pages = max_response_pages(server);

	dprintk("--> %s\n", __func__);
	nfs4_free_pages(lgp->args.layout.pages, max_pages);
	pnfs_put_layout_hdr(NFS_I(inode)->layout);
	put_nfs_open_context(lgp->args.ctx);
	kfree(calldata);
	dprintk("<-- %s\n", __func__);
}

static const struct rpc_call_ops nfs4_layoutget_call_ops = {
	.rpc_call_prepare = nfs4_layoutget_prepare,
	.rpc_call_done = nfs4_layoutget_done,
	.rpc_release = nfs4_layoutget_release,
};

struct pnfs_layout_segment *
nfs4_proc_layoutget(struct nfs4_layoutget *lgp, gfp_t gfp_flags)
{
	struct inode *inode = lgp->args.inode;
	struct nfs_server *server = NFS_SERVER(inode);
	size_t max_pages = max_response_pages(server);
	struct rpc_task *task;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LAYOUTGET],
		.rpc_argp = &lgp->args,
		.rpc_resp = &lgp->res,
		.rpc_cred = lgp->cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = server->client,
		.rpc_message = &msg,
		.callback_ops = &nfs4_layoutget_call_ops,
		.callback_data = lgp,
		.flags = RPC_TASK_ASYNC,
	};
	struct pnfs_layout_segment *lseg = NULL;
	int status = 0;

	dprintk("--> %s\n", __func__);

	/* nfs4_layoutget_release calls pnfs_put_layout_hdr */
	pnfs_get_layout_hdr(NFS_I(inode)->layout);

	lgp->args.layout.pages = nfs4_alloc_pages(max_pages, gfp_flags);
	if (!lgp->args.layout.pages) {
		nfs4_layoutget_release(lgp);
		return ERR_PTR(-ENOMEM);
	}
	lgp->args.layout.pglen = max_pages * PAGE_SIZE;
	lgp->args.timestamp = jiffies;

	lgp->res.layoutp = &lgp->args.layout;
	lgp->res.seq_res.sr_slot = NULL;
	nfs4_init_sequence(&lgp->args.seq_args, &lgp->res.seq_res, 0);

	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return ERR_CAST(task);
	status = nfs4_wait_for_completion_rpc_task(task);
	if (status == 0)
		status = task->tk_status;
	trace_nfs4_layoutget(lgp->args.ctx,
			&lgp->args.range,
			&lgp->res.range,
			status);
	/* if layoutp->len is 0, nfs4_layoutget_prepare called rpc_exit */
	if (status == 0 && lgp->res.layoutp->len)
		lseg = pnfs_layout_process(lgp);
	rpc_put_task(task);
	dprintk("<-- %s status=%d\n", __func__, status);
	if (status)
		return ERR_PTR(status);
	return lseg;
}

static void
nfs4_layoutreturn_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs4_layoutreturn *lrp = calldata;

	dprintk("--> %s\n", __func__);
	nfs41_setup_sequence(lrp->clp->cl_session,
			&lrp->args.seq_args,
			&lrp->res.seq_res,
			task);
}

static void nfs4_layoutreturn_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_layoutreturn *lrp = calldata;
	struct nfs_server *server;

	dprintk("--> %s\n", __func__);

	if (!nfs41_sequence_done(task, &lrp->res.seq_res))
		return;

	server = NFS_SERVER(lrp->args.inode);
	switch (task->tk_status) {
	default:
		task->tk_status = 0;
	case 0:
		break;
	case -NFS4ERR_DELAY:
		if (nfs4_async_handle_error(task, server, NULL, NULL) != -EAGAIN)
			break;
		rpc_restart_call_prepare(task);
		return;
	}
	dprintk("<-- %s\n", __func__);
}

static void nfs4_layoutreturn_release(void *calldata)
{
	struct nfs4_layoutreturn *lrp = calldata;
	struct pnfs_layout_hdr *lo = lrp->args.layout;

	dprintk("--> %s\n", __func__);
	spin_lock(&lo->plh_inode->i_lock);
	if (lrp->res.lrs_present)
		pnfs_set_layout_stateid(lo, &lrp->res.stateid, true);
	lo->plh_block_lgets--;
	spin_unlock(&lo->plh_inode->i_lock);
	pnfs_put_layout_hdr(lrp->args.layout);
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
		.rpc_cred = lrp->cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = NFS_SERVER(lrp->args.inode)->client,
		.rpc_message = &msg,
		.callback_ops = &nfs4_layoutreturn_call_ops,
		.callback_data = lrp,
	};
	int status;

	dprintk("--> %s\n", __func__);
	nfs4_init_sequence(&lrp->args.seq_args, &lrp->res.seq_res, 1);
	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	status = task->tk_status;
	trace_nfs4_layoutreturn(lrp->args.inode, status);
	dprintk("<-- %s status=%d\n", __func__, status);
	rpc_put_task(task);
	return status;
}

static int
_nfs4_proc_getdeviceinfo(struct nfs_server *server,
		struct pnfs_device *pdev,
		struct rpc_cred *cred)
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
		.rpc_cred = cred,
	};
	int status;

	dprintk("--> %s\n", __func__);
	status = nfs4_call_sync(server->client, server, &msg, &args.seq_args, &res.seq_res, 0);
	dprintk("<-- %s status=%d\n", __func__, status);

	return status;
}

int nfs4_proc_getdeviceinfo(struct nfs_server *server,
		struct pnfs_device *pdev,
		struct rpc_cred *cred)
{
	struct nfs4_exception exception = { };
	int err;

	do {
		err = nfs4_handle_exception(server,
					_nfs4_proc_getdeviceinfo(server, pdev, cred),
					&exception);
	} while (exception.retry);
	return err;
}
EXPORT_SYMBOL_GPL(nfs4_proc_getdeviceinfo);

static void nfs4_layoutcommit_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs4_layoutcommit_data *data = calldata;
	struct nfs_server *server = NFS_SERVER(data->args.inode);
	struct nfs4_session *session = nfs4_get_session(server);

	nfs41_setup_sequence(session,
			&data->args.seq_args,
			&data->res.seq_res,
			task);
}

static void
nfs4_layoutcommit_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_layoutcommit_data *data = calldata;
	struct nfs_server *server = NFS_SERVER(data->args.inode);

	if (!nfs41_sequence_done(task, &data->res.seq_res))
		return;

	switch (task->tk_status) { /* Just ignore these failures */
	case -NFS4ERR_DELEG_REVOKED: /* layout was recalled */
	case -NFS4ERR_BADIOMODE:     /* no IOMODE_RW layout for range */
	case -NFS4ERR_BADLAYOUT:     /* no layout */
	case -NFS4ERR_GRACE:	    /* loca_recalim always false */
		task->tk_status = 0;
	case 0:
		break;
	default:
		if (nfs4_async_handle_error(task, server, NULL, NULL) == -EAGAIN) {
			rpc_restart_call_prepare(task);
			return;
		}
	}
}

static void nfs4_layoutcommit_release(void *calldata)
{
	struct nfs4_layoutcommit_data *data = calldata;

	pnfs_cleanup_layoutcommit(data);
	nfs_post_op_update_inode_force_wcc(data->args.inode,
					   data->res.fattr);
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

	nfs4_init_sequence(&data->args.seq_args, &data->res.seq_res, 1);
	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	if (sync == false)
		goto out;
	status = nfs4_wait_for_completion_rpc_task(task);
	if (status != 0)
		goto out;
	status = task->tk_status;
	trace_nfs4_layoutcommit(data->args.inode, status);
out:
	dprintk("%s: status %d\n", __func__, status);
	rpc_put_task(task);
	return status;
}

/**
 * Use the state managment nfs_client cl_rpcclient, which uses krb5i (if
 * possible) as per RFC3530bis and RFC5661 Security Considerations sections
 */
static int
_nfs41_proc_secinfo_no_name(struct nfs_server *server, struct nfs_fh *fhandle,
		    struct nfs_fsinfo *info,
		    struct nfs4_secinfo_flavors *flavors, bool use_integrity)
{
	struct nfs41_secinfo_no_name_args args = {
		.style = SECINFO_STYLE_CURRENT_FH,
	};
	struct nfs4_secinfo_res res = {
		.flavors = flavors,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SECINFO_NO_NAME],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	struct rpc_clnt *clnt = server->client;
	struct rpc_cred *cred = NULL;
	int status;

	if (use_integrity) {
		clnt = server->nfs_client->cl_rpcclient;
		cred = nfs4_get_clid_cred(server->nfs_client);
		msg.rpc_cred = cred;
	}

	dprintk("--> %s\n", __func__);
	status = nfs4_call_sync(clnt, server, &msg, &args.seq_args,
				&res.seq_res, 0);
	dprintk("<-- %s status=%d\n", __func__, status);

	if (cred)
		put_rpccred(cred);

	return status;
}

static int
nfs41_proc_secinfo_no_name(struct nfs_server *server, struct nfs_fh *fhandle,
			   struct nfs_fsinfo *info, struct nfs4_secinfo_flavors *flavors)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		/* first try using integrity protection */
		err = -NFS4ERR_WRONGSEC;

		/* try to use integrity protection with machine cred */
		if (_nfs4_is_integrity_protected(server->nfs_client))
			err = _nfs41_proc_secinfo_no_name(server, fhandle, info,
							  flavors, true);

		/*
		 * if unable to use integrity protection, or SECINFO with
		 * integrity protection returns NFS4ERR_WRONGSEC (which is
		 * disallowed by spec, but exists in deployed servers) use
		 * the current filesystem's rpc_client and the user cred.
		 */
		if (err == -NFS4ERR_WRONGSEC)
			err = _nfs41_proc_secinfo_no_name(server, fhandle, info,
							  flavors, false);

		switch (err) {
		case 0:
		case -NFS4ERR_WRONGSEC:
		case -ENOTSUPP:
			goto out;
		default:
			err = nfs4_handle_exception(server, err, &exception);
		}
	} while (exception.retry);
out:
	return err;
}

static int
nfs41_find_root_sec(struct nfs_server *server, struct nfs_fh *fhandle,
		    struct nfs_fsinfo *info)
{
	int err;
	struct page *page;
	rpc_authflavor_t flavor = RPC_AUTH_MAXFLAVOR;
	struct nfs4_secinfo_flavors *flavors;
	struct nfs4_secinfo4 *secinfo;
	int i;

	page = alloc_page(GFP_KERNEL);
	if (!page) {
		err = -ENOMEM;
		goto out;
	}

	flavors = page_address(page);
	err = nfs41_proc_secinfo_no_name(server, fhandle, info, flavors);

	/*
	 * Fall back on "guess and check" method if
	 * the server doesn't support SECINFO_NO_NAME
	 */
	if (err == -NFS4ERR_WRONGSEC || err == -ENOTSUPP) {
		err = nfs4_find_root_sec(server, fhandle, info);
		goto out_freepage;
	}
	if (err)
		goto out_freepage;

	for (i = 0; i < flavors->num_flavors; i++) {
		secinfo = &flavors->flavors[i];

		switch (secinfo->flavor) {
		case RPC_AUTH_NULL:
		case RPC_AUTH_UNIX:
		case RPC_AUTH_GSS:
			flavor = rpcauth_get_pseudoflavor(secinfo->flavor,
					&secinfo->flavor_info);
			break;
		default:
			flavor = RPC_AUTH_MAXFLAVOR;
			break;
		}

		if (!nfs_auth_info_match(&server->auth_info, flavor))
			flavor = RPC_AUTH_MAXFLAVOR;

		if (flavor != RPC_AUTH_MAXFLAVOR) {
			err = nfs4_lookup_root_sec(server, fhandle,
						   info, flavor);
			if (!err)
				break;
		}
	}

	if (flavor == RPC_AUTH_MAXFLAVOR)
		err = -EPERM;

out_freepage:
	put_page(page);
	if (err == -EACCES)
		return -EPERM;
out:
	return err;
}

static int _nfs41_test_stateid(struct nfs_server *server,
		nfs4_stateid *stateid,
		struct rpc_cred *cred)
{
	int status;
	struct nfs41_test_stateid_args args = {
		.stateid = stateid,
	};
	struct nfs41_test_stateid_res res;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_TEST_STATEID],
		.rpc_argp = &args,
		.rpc_resp = &res,
		.rpc_cred = cred,
	};
	struct rpc_clnt *rpc_client = server->client;

	nfs4_state_protect(server->nfs_client, NFS_SP4_MACH_CRED_STATEID,
		&rpc_client, &msg);

	dprintk("NFS call  test_stateid %p\n", stateid);
	nfs4_init_sequence(&args.seq_args, &res.seq_res, 0);
	nfs4_set_sequence_privileged(&args.seq_args);
	status = nfs4_call_sync_sequence(rpc_client, server, &msg,
			&args.seq_args, &res.seq_res);
	if (status != NFS_OK) {
		dprintk("NFS reply test_stateid: failed, %d\n", status);
		return status;
	}
	dprintk("NFS reply test_stateid: succeeded, %d\n", -res.status);
	return -res.status;
}

/**
 * nfs41_test_stateid - perform a TEST_STATEID operation
 *
 * @server: server / transport on which to perform the operation
 * @stateid: state ID to test
 * @cred: credential
 *
 * Returns NFS_OK if the server recognizes that "stateid" is valid.
 * Otherwise a negative NFS4ERR value is returned if the operation
 * failed or the state ID is not currently valid.
 */
static int nfs41_test_stateid(struct nfs_server *server,
		nfs4_stateid *stateid,
		struct rpc_cred *cred)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = _nfs41_test_stateid(server, stateid, cred);
		if (err != -NFS4ERR_DELAY)
			break;
		nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);
	return err;
}

struct nfs_free_stateid_data {
	struct nfs_server *server;
	struct nfs41_free_stateid_args args;
	struct nfs41_free_stateid_res res;
};

static void nfs41_free_stateid_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs_free_stateid_data *data = calldata;
	nfs41_setup_sequence(nfs4_get_session(data->server),
			&data->args.seq_args,
			&data->res.seq_res,
			task);
}

static void nfs41_free_stateid_done(struct rpc_task *task, void *calldata)
{
	struct nfs_free_stateid_data *data = calldata;

	nfs41_sequence_done(task, &data->res.seq_res);

	switch (task->tk_status) {
	case -NFS4ERR_DELAY:
		if (nfs4_async_handle_error(task, data->server, NULL, NULL) == -EAGAIN)
			rpc_restart_call_prepare(task);
	}
}

static void nfs41_free_stateid_release(void *calldata)
{
	kfree(calldata);
}

static const struct rpc_call_ops nfs41_free_stateid_ops = {
	.rpc_call_prepare = nfs41_free_stateid_prepare,
	.rpc_call_done = nfs41_free_stateid_done,
	.rpc_release = nfs41_free_stateid_release,
};

static struct rpc_task *_nfs41_free_stateid(struct nfs_server *server,
		nfs4_stateid *stateid,
		struct rpc_cred *cred,
		bool privileged)
{
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_FREE_STATEID],
		.rpc_cred = cred,
	};
	struct rpc_task_setup task_setup = {
		.rpc_client = server->client,
		.rpc_message = &msg,
		.callback_ops = &nfs41_free_stateid_ops,
		.flags = RPC_TASK_ASYNC,
	};
	struct nfs_free_stateid_data *data;

	nfs4_state_protect(server->nfs_client, NFS_SP4_MACH_CRED_STATEID,
		&task_setup.rpc_client, &msg);

	dprintk("NFS call  free_stateid %p\n", stateid);
	data = kmalloc(sizeof(*data), GFP_NOFS);
	if (!data)
		return ERR_PTR(-ENOMEM);
	data->server = server;
	nfs4_stateid_copy(&data->args.stateid, stateid);

	task_setup.callback_data = data;

	msg.rpc_argp = &data->args;
	msg.rpc_resp = &data->res;
	nfs4_init_sequence(&data->args.seq_args, &data->res.seq_res, 0);
	if (privileged)
		nfs4_set_sequence_privileged(&data->args.seq_args);

	return rpc_run_task(&task_setup);
}

/**
 * nfs41_free_stateid - perform a FREE_STATEID operation
 *
 * @server: server / transport on which to perform the operation
 * @stateid: state ID to release
 * @cred: credential
 *
 * Returns NFS_OK if the server freed "stateid".  Otherwise a
 * negative NFS4ERR value is returned.
 */
static int nfs41_free_stateid(struct nfs_server *server,
		nfs4_stateid *stateid,
		struct rpc_cred *cred)
{
	struct rpc_task *task;
	int ret;

	task = _nfs41_free_stateid(server, stateid, cred, true);
	if (IS_ERR(task))
		return PTR_ERR(task);
	ret = rpc_wait_for_completion_task(task);
	if (!ret)
		ret = task->tk_status;
	rpc_put_task(task);
	return ret;
}

static void
nfs41_free_lock_state(struct nfs_server *server, struct nfs4_lock_state *lsp)
{
	struct rpc_task *task;
	struct rpc_cred *cred = lsp->ls_state->owner->so_cred;

	task = _nfs41_free_stateid(server, &lsp->ls_stateid, cred, false);
	nfs4_free_lock_state(server, lsp);
	if (IS_ERR(task))
		return;
	rpc_put_task(task);
}

static bool nfs41_match_stateid(const nfs4_stateid *s1,
		const nfs4_stateid *s2)
{
	if (memcmp(s1->other, s2->other, sizeof(s1->other)) != 0)
		return false;

	if (s1->seqid == s2->seqid)
		return true;
	if (s1->seqid == 0 || s2->seqid == 0)
		return true;

	return false;
}

#endif /* CONFIG_NFS_V4_1 */

static bool nfs4_match_stateid(const nfs4_stateid *s1,
		const nfs4_stateid *s2)
{
	return nfs4_stateid_match(s1, s2);
}


static const struct nfs4_state_recovery_ops nfs40_reboot_recovery_ops = {
	.owner_flag_bit = NFS_OWNER_RECLAIM_REBOOT,
	.state_flag_bit	= NFS_STATE_RECLAIM_REBOOT,
	.recover_open	= nfs4_open_reclaim,
	.recover_lock	= nfs4_lock_reclaim,
	.establish_clid = nfs4_init_clientid,
	.detect_trunking = nfs40_discover_server_trunking,
};

#if defined(CONFIG_NFS_V4_1)
static const struct nfs4_state_recovery_ops nfs41_reboot_recovery_ops = {
	.owner_flag_bit = NFS_OWNER_RECLAIM_REBOOT,
	.state_flag_bit	= NFS_STATE_RECLAIM_REBOOT,
	.recover_open	= nfs4_open_reclaim,
	.recover_lock	= nfs4_lock_reclaim,
	.establish_clid = nfs41_init_clientid,
	.reclaim_complete = nfs41_proc_reclaim_complete,
	.detect_trunking = nfs41_discover_server_trunking,
};
#endif /* CONFIG_NFS_V4_1 */

static const struct nfs4_state_recovery_ops nfs40_nograce_recovery_ops = {
	.owner_flag_bit = NFS_OWNER_RECLAIM_NOGRACE,
	.state_flag_bit	= NFS_STATE_RECLAIM_NOGRACE,
	.recover_open	= nfs40_open_expired,
	.recover_lock	= nfs4_lock_expired,
	.establish_clid = nfs4_init_clientid,
};

#if defined(CONFIG_NFS_V4_1)
static const struct nfs4_state_recovery_ops nfs41_nograce_recovery_ops = {
	.owner_flag_bit = NFS_OWNER_RECLAIM_NOGRACE,
	.state_flag_bit	= NFS_STATE_RECLAIM_NOGRACE,
	.recover_open	= nfs41_open_expired,
	.recover_lock	= nfs41_lock_expired,
	.establish_clid = nfs41_init_clientid,
};
#endif /* CONFIG_NFS_V4_1 */

static const struct nfs4_state_maintenance_ops nfs40_state_renewal_ops = {
	.sched_state_renewal = nfs4_proc_async_renew,
	.get_state_renewal_cred_locked = nfs4_get_renew_cred_locked,
	.renew_lease = nfs4_proc_renew,
};

#if defined(CONFIG_NFS_V4_1)
static const struct nfs4_state_maintenance_ops nfs41_state_renewal_ops = {
	.sched_state_renewal = nfs41_proc_async_sequence,
	.get_state_renewal_cred_locked = nfs4_get_machine_cred_locked,
	.renew_lease = nfs4_proc_sequence,
};
#endif

static const struct nfs4_mig_recovery_ops nfs40_mig_recovery_ops = {
	.get_locations = _nfs40_proc_get_locations,
	.fsid_present = _nfs40_proc_fsid_present,
};

#if defined(CONFIG_NFS_V4_1)
static const struct nfs4_mig_recovery_ops nfs41_mig_recovery_ops = {
	.get_locations = _nfs41_proc_get_locations,
	.fsid_present = _nfs41_proc_fsid_present,
};
#endif	/* CONFIG_NFS_V4_1 */

static const struct nfs4_minor_version_ops nfs_v4_0_minor_ops = {
	.minor_version = 0,
	.init_caps = NFS_CAP_READDIRPLUS
		| NFS_CAP_ATOMIC_OPEN
		| NFS_CAP_CHANGE_ATTR
		| NFS_CAP_POSIX_LOCK,
	.init_client = nfs40_init_client,
	.shutdown_client = nfs40_shutdown_client,
	.match_stateid = nfs4_match_stateid,
	.find_root_sec = nfs4_find_root_sec,
	.free_lock_state = nfs4_release_lockowner,
	.call_sync_ops = &nfs40_call_sync_ops,
	.reboot_recovery_ops = &nfs40_reboot_recovery_ops,
	.nograce_recovery_ops = &nfs40_nograce_recovery_ops,
	.state_renewal_ops = &nfs40_state_renewal_ops,
	.mig_recovery_ops = &nfs40_mig_recovery_ops,
};

#if defined(CONFIG_NFS_V4_1)
static const struct nfs4_minor_version_ops nfs_v4_1_minor_ops = {
	.minor_version = 1,
	.init_caps = NFS_CAP_READDIRPLUS
		| NFS_CAP_ATOMIC_OPEN
		| NFS_CAP_CHANGE_ATTR
		| NFS_CAP_POSIX_LOCK
		| NFS_CAP_STATEID_NFSV41
		| NFS_CAP_ATOMIC_OPEN_V1,
	.init_client = nfs41_init_client,
	.shutdown_client = nfs41_shutdown_client,
	.match_stateid = nfs41_match_stateid,
	.find_root_sec = nfs41_find_root_sec,
	.free_lock_state = nfs41_free_lock_state,
	.call_sync_ops = &nfs41_call_sync_ops,
	.reboot_recovery_ops = &nfs41_reboot_recovery_ops,
	.nograce_recovery_ops = &nfs41_nograce_recovery_ops,
	.state_renewal_ops = &nfs41_state_renewal_ops,
	.mig_recovery_ops = &nfs41_mig_recovery_ops,
};
#endif

#if defined(CONFIG_NFS_V4_2)
static const struct nfs4_minor_version_ops nfs_v4_2_minor_ops = {
	.minor_version = 2,
	.init_caps = NFS_CAP_READDIRPLUS
		| NFS_CAP_ATOMIC_OPEN
		| NFS_CAP_CHANGE_ATTR
		| NFS_CAP_POSIX_LOCK
		| NFS_CAP_STATEID_NFSV41
		| NFS_CAP_ATOMIC_OPEN_V1
		| NFS_CAP_ALLOCATE
		| NFS_CAP_DEALLOCATE
		| NFS_CAP_SEEK,
	.init_client = nfs41_init_client,
	.shutdown_client = nfs41_shutdown_client,
	.match_stateid = nfs41_match_stateid,
	.find_root_sec = nfs41_find_root_sec,
	.free_lock_state = nfs41_free_lock_state,
	.call_sync_ops = &nfs41_call_sync_ops,
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
#if defined(CONFIG_NFS_V4_2)
	[2] = &nfs_v4_2_minor_ops,
#endif
};

static const struct inode_operations nfs4_dir_inode_operations = {
	.create		= nfs_create,
	.lookup		= nfs_lookup,
	.atomic_open	= nfs_atomic_open,
	.link		= nfs_link,
	.unlink		= nfs_unlink,
	.symlink	= nfs_symlink,
	.mkdir		= nfs_mkdir,
	.rmdir		= nfs_rmdir,
	.mknod		= nfs_mknod,
	.rename		= nfs_rename,
	.permission	= nfs_permission,
	.getattr	= nfs_getattr,
	.setattr	= nfs_setattr,
	.getxattr	= generic_getxattr,
	.setxattr	= generic_setxattr,
	.listxattr	= generic_listxattr,
	.removexattr	= generic_removexattr,
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
	.submount	= nfs4_submount,
	.try_mount	= nfs4_try_mount,
	.getattr	= nfs4_proc_getattr,
	.setattr	= nfs4_proc_setattr,
	.lookup		= nfs4_proc_lookup,
	.access		= nfs4_proc_access,
	.readlink	= nfs4_proc_readlink,
	.create		= nfs4_proc_create,
	.remove		= nfs4_proc_remove,
	.unlink_setup	= nfs4_proc_unlink_setup,
	.unlink_rpc_prepare = nfs4_proc_unlink_rpc_prepare,
	.unlink_done	= nfs4_proc_unlink_done,
	.rename_setup	= nfs4_proc_rename_setup,
	.rename_rpc_prepare = nfs4_proc_rename_rpc_prepare,
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
	.pgio_rpc_prepare = nfs4_proc_pgio_rpc_prepare,
	.read_setup	= nfs4_proc_read_setup,
	.read_done	= nfs4_read_done,
	.write_setup	= nfs4_proc_write_setup,
	.write_done	= nfs4_write_done,
	.commit_setup	= nfs4_proc_commit_setup,
	.commit_rpc_prepare = nfs4_proc_commit_rpc_prepare,
	.commit_done	= nfs4_commit_done,
	.lock		= nfs4_proc_lock,
	.clear_acl_cache = nfs4_zap_acl_attr,
	.close_context  = nfs4_close_context,
	.open_context	= nfs4_atomic_open,
	.have_delegation = nfs4_have_delegation,
	.return_delegation = nfs4_inode_return_delegation,
	.alloc_client	= nfs4_alloc_client,
	.init_client	= nfs4_init_client,
	.free_client	= nfs4_free_client,
	.create_server	= nfs4_create_server,
	.clone_server	= nfs_clone_server,
};

static const struct xattr_handler nfs4_xattr_nfs4_acl_handler = {
	.prefix	= XATTR_NAME_NFSV4_ACL,
	.list	= nfs4_xattr_list_nfs4_acl,
	.get	= nfs4_xattr_get_nfs4_acl,
	.set	= nfs4_xattr_set_nfs4_acl,
};

const struct xattr_handler *nfs4_xattr_handlers[] = {
	&nfs4_xattr_nfs4_acl_handler,
#ifdef CONFIG_NFS_V4_SECURITY_LABEL
	&nfs4_xattr_nfs4_label_handler,
#endif
	NULL
};

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */

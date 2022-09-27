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
#include <linux/xattr.h>
#include <linux/utsname.h>
#include <linux/freezer.h>
#include <linux/iversion.h>

#include "nfs4_fs.h"
#include "delegation.h"
#include "internal.h"
#include "iostat.h"
#include "callback.h"
#include "pnfs.h"
#include "netns.h"
#include "sysfs.h"
#include "nfs4idmap.h"
#include "nfs4session.h"
#include "fscache.h"
#include "nfs42.h"

#include "nfs4trace.h"

#ifdef CONFIG_NFS_V4_2
#include "nfs42.h"
#endif /* CONFIG_NFS_V4_2 */

#define NFSDBG_FACILITY		NFSDBG_PROC

#define NFS4_BITMASK_SZ		3

#define NFS4_POLL_RETRY_MIN	(HZ/10)
#define NFS4_POLL_RETRY_MAX	(15*HZ)

/* file attributes which can be mapped to nfs attributes */
#define NFS4_VALID_ATTRS (ATTR_MODE \
	| ATTR_UID \
	| ATTR_GID \
	| ATTR_SIZE \
	| ATTR_ATIME \
	| ATTR_MTIME \
	| ATTR_CTIME \
	| ATTR_ATIME_SET \
	| ATTR_MTIME_SET)

struct nfs4_opendata;
static int _nfs4_recover_proc_open(struct nfs4_opendata *data);
static int nfs4_do_fsinfo(struct nfs_server *, struct nfs_fh *, struct nfs_fsinfo *);
static void nfs_fixup_referral_attributes(struct nfs_fattr *fattr);
static int _nfs4_proc_getattr(struct nfs_server *server, struct nfs_fh *fhandle, struct nfs_fattr *fattr, struct nfs4_label *label, struct inode *inode);
static int nfs4_do_setattr(struct inode *inode, const struct cred *cred,
			    struct nfs_fattr *fattr, struct iattr *sattr,
			    struct nfs_open_context *ctx, struct nfs4_label *ilabel,
			    struct nfs4_label *olabel);
#ifdef CONFIG_NFS_V4_1
static struct rpc_task *_nfs41_proc_sequence(struct nfs_client *clp,
		const struct cred *cred,
		struct nfs4_slot *slot,
		bool is_privileged);
static int nfs41_test_stateid(struct nfs_server *, nfs4_stateid *,
		const struct cred *);
static int nfs41_free_stateid(struct nfs_server *, const nfs4_stateid *,
		const struct cred *, bool);
#endif
static void nfs4_bitmask_set(__u32 bitmask[NFS4_BITMASK_SZ],
			     const __u32 *src, struct inode *inode,
			     struct nfs_server *server,
			     struct nfs4_label *label);

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
	| FATTR4_WORD1_TIME_MODIFY
	| FATTR4_WORD1_MOUNTED_ON_FILEID,
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
#ifdef CONFIG_NFS_V4_SECURITY_LABEL
	| FATTR4_WORD2_SECURITY_LABEL
#endif
};

static const u32 nfs4_open_noattr_bitmap[3] = {
	FATTR4_WORD0_TYPE
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
			| FATTR4_WORD2_CLONE_BLKSIZE
			| FATTR4_WORD2_XATTR_SUPPORT
};

const u32 nfs4_fs_locations_bitmap[3] = {
	FATTR4_WORD0_CHANGE
	| FATTR4_WORD0_SIZE
	| FATTR4_WORD0_FSID
	| FATTR4_WORD0_FILEID
	| FATTR4_WORD0_FS_LOCATIONS,
	FATTR4_WORD1_OWNER
	| FATTR4_WORD1_OWNER_GROUP
	| FATTR4_WORD1_RAWDEV
	| FATTR4_WORD1_SPACE_USED
	| FATTR4_WORD1_TIME_ACCESS
	| FATTR4_WORD1_TIME_METADATA
	| FATTR4_WORD1_TIME_MODIFY
	| FATTR4_WORD1_MOUNTED_ON_FILEID,
};

static void nfs4_bitmap_copy_adjust(__u32 *dst, const __u32 *src,
		struct inode *inode)
{
	unsigned long cache_validity;

	memcpy(dst, src, NFS4_BITMASK_SZ*sizeof(*dst));
	if (!inode || !nfs4_have_delegation(inode, FMODE_READ))
		return;

	cache_validity = READ_ONCE(NFS_I(inode)->cache_validity);
	if (!(cache_validity & NFS_INO_REVAL_FORCED))
		cache_validity &= ~(NFS_INO_INVALID_CHANGE
				| NFS_INO_INVALID_SIZE);

	if (!(cache_validity & NFS_INO_INVALID_SIZE))
		dst[0] &= ~FATTR4_WORD0_SIZE;

	if (!(cache_validity & NFS_INO_INVALID_CHANGE))
		dst[0] &= ~FATTR4_WORD0_CHANGE;
}

static void nfs4_bitmap_copy_adjust_setattr(__u32 *dst,
		const __u32 *src, struct inode *inode)
{
	nfs4_bitmap_copy_adjust(dst, src, inode);
}

static void nfs4_setup_readdir(u64 cookie, __be32 *verifier, struct dentry *dentry,
		struct nfs4_readdir_arg *readdir)
{
	unsigned int attrs = FATTR4_WORD0_FILEID | FATTR4_WORD0_TYPE;
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
		*p++ = htonl(attrs);                           /* bitmap */
		*p++ = htonl(12);             /* attribute buffer length */
		*p++ = htonl(NF4DIR);
		p = xdr_encode_hyper(p, NFS_FILEID(d_inode(dentry)));
	}
	
	*p++ = xdr_one;                                  /* next */
	*p++ = xdr_zero;                   /* cookie, first word */
	*p++ = xdr_two;                   /* cookie, second word */
	*p++ = xdr_two;                             /* entry len */
	memcpy(p, "..\0\0", 4);                         /* entry */
	p++;
	*p++ = xdr_one;                         /* bitmap length */
	*p++ = htonl(attrs);                           /* bitmap */
	*p++ = htonl(12);             /* attribute buffer length */
	*p++ = htonl(NF4DIR);
	p = xdr_encode_hyper(p, NFS_FILEID(d_inode(dentry->d_parent)));

	readdir->pgbase = (char *)p - (char *)start;
	readdir->count -= readdir->pgbase;
	kunmap_atomic(start);
}

static void nfs4_fattr_set_prechange(struct nfs_fattr *fattr, u64 version)
{
	if (!(fattr->valid & NFS_ATTR_FATTR_PRECHANGE)) {
		fattr->pre_change_attr = version;
		fattr->valid |= NFS_ATTR_FATTR_PRECHANGE;
	}
}

static void nfs4_test_and_free_stateid(struct nfs_server *server,
		nfs4_stateid *stateid,
		const struct cred *cred)
{
	const struct nfs4_minor_version_ops *ops = server->nfs_client->cl_mvops;

	ops->test_and_free_expired(server, stateid, cred);
}

static void __nfs4_free_revoked_stateid(struct nfs_server *server,
		nfs4_stateid *stateid,
		const struct cred *cred)
{
	stateid->type = NFS4_REVOKED_STATEID_TYPE;
	nfs4_test_and_free_stateid(server, stateid, cred);
}

static void nfs4_free_revoked_stateid(struct nfs_server *server,
		const nfs4_stateid *stateid,
		const struct cred *cred)
{
	nfs4_stateid tmp;

	nfs4_stateid_copy(&tmp, stateid);
	__nfs4_free_revoked_stateid(server, &tmp, cred);
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

static int nfs4_delay_killable(long *timeout)
{
	might_sleep();

	freezable_schedule_timeout_killable_unsafe(
		nfs4_update_delay(timeout));
	if (!__fatal_signal_pending(current))
		return 0;
	return -EINTR;
}

static int nfs4_delay_interruptible(long *timeout)
{
	might_sleep();

	freezable_schedule_timeout_interruptible_unsafe(nfs4_update_delay(timeout));
	if (!signal_pending(current))
		return 0;
	return __fatal_signal_pending(current) ? -EINTR :-ERESTARTSYS;
}

static int nfs4_delay(long *timeout, bool interruptible)
{
	if (interruptible)
		return nfs4_delay_interruptible(timeout);
	return nfs4_delay_killable(timeout);
}

static const nfs4_stateid *
nfs4_recoverable_stateid(const nfs4_stateid *stateid)
{
	if (!stateid)
		return NULL;
	switch (stateid->type) {
	case NFS4_OPEN_STATEID_TYPE:
	case NFS4_LOCK_STATEID_TYPE:
	case NFS4_DELEGATION_STATEID_TYPE:
		return stateid;
	default:
		break;
	}
	return NULL;
}

/* This is the error handling routine for processes that are allowed
 * to sleep.
 */
static int nfs4_do_handle_exception(struct nfs_server *server,
		int errorcode, struct nfs4_exception *exception)
{
	struct nfs_client *clp = server->nfs_client;
	struct nfs4_state *state = exception->state;
	const nfs4_stateid *stateid;
	struct inode *inode = exception->inode;
	int ret = errorcode;

	exception->delay = 0;
	exception->recovering = 0;
	exception->retry = 0;

	stateid = nfs4_recoverable_stateid(exception->stateid);
	if (stateid == NULL && state != NULL)
		stateid = nfs4_recoverable_stateid(&state->stateid);

	switch(errorcode) {
		case 0:
			return 0;
		case -NFS4ERR_BADHANDLE:
		case -ESTALE:
			if (inode != NULL && S_ISREG(inode->i_mode))
				pnfs_destroy_layout(NFS_I(inode));
			break;
		case -NFS4ERR_DELEG_REVOKED:
		case -NFS4ERR_ADMIN_REVOKED:
		case -NFS4ERR_EXPIRED:
		case -NFS4ERR_BAD_STATEID:
		case -NFS4ERR_PARTNER_NO_AUTH:
			if (inode != NULL && stateid != NULL) {
				nfs_inode_find_state_and_recover(inode,
						stateid);
				goto wait_on_recovery;
			}
			fallthrough;
		case -NFS4ERR_OPENMODE:
			if (inode) {
				int err;

				err = nfs_async_inode_return_delegation(inode,
						stateid);
				if (err == 0)
					goto wait_on_recovery;
				if (stateid != NULL && stateid->type == NFS4_DELEGATION_STATEID_TYPE) {
					exception->retry = 1;
					break;
				}
			}
			if (state == NULL)
				break;
			ret = nfs4_schedule_stateid_recovery(server, state);
			if (ret < 0)
				break;
			goto wait_on_recovery;
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
			/* Handled in nfs41_sequence_process() */
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
			fallthrough;
		case -NFS4ERR_DELAY:
			nfs_inc_server_stats(server, NFSIOS_DELAY);
			fallthrough;
		case -NFS4ERR_GRACE:
		case -NFS4ERR_LAYOUTTRYLATER:
		case -NFS4ERR_RECALLCONFLICT:
			exception->delay = 1;
			return 0;

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
	exception->recovering = 1;
	return 0;
}

/* This is the error handling routine for processes that are allowed
 * to sleep.
 */
int nfs4_handle_exception(struct nfs_server *server, int errorcode, struct nfs4_exception *exception)
{
	struct nfs_client *clp = server->nfs_client;
	int ret;

	ret = nfs4_do_handle_exception(server, errorcode, exception);
	if (exception->delay) {
		ret = nfs4_delay(&exception->timeout,
				exception->interruptible);
		goto out_retry;
	}
	if (exception->recovering) {
		if (exception->task_is_privileged)
			return -EDEADLOCK;
		ret = nfs4_wait_clnt_recover(clp);
		if (test_bit(NFS_MIG_FAILED, &server->mig_status))
			return -EIO;
		goto out_retry;
	}
	return ret;
out_retry:
	if (ret == 0)
		exception->retry = 1;
	return ret;
}

static int
nfs4_async_handle_exception(struct rpc_task *task, struct nfs_server *server,
		int errorcode, struct nfs4_exception *exception)
{
	struct nfs_client *clp = server->nfs_client;
	int ret;

	ret = nfs4_do_handle_exception(server, errorcode, exception);
	if (exception->delay) {
		rpc_delay(task, nfs4_update_delay(&exception->timeout));
		goto out_retry;
	}
	if (exception->recovering) {
		if (exception->task_is_privileged)
			return -EDEADLOCK;
		rpc_sleep_on(&clp->cl_rpcwaitq, task, NULL);
		if (test_bit(NFS4CLNT_MANAGER_RUNNING, &clp->cl_state) == 0)
			rpc_wake_up_queued_task(&clp->cl_rpcwaitq, task);
		goto out_retry;
	}
	if (test_bit(NFS_MIG_FAILED, &server->mig_status))
		ret = -EIO;
	return ret;
out_retry:
	if (ret == 0) {
		exception->retry = 1;
		/*
		 * For NFS4ERR_MOVED, the client transport will need to
		 * be recomputed after migration recovery has completed.
		 */
		if (errorcode == -NFS4ERR_MOVED)
			rpc_task_release_transport(task);
	}
	return ret;
}

int
nfs4_async_handle_error(struct rpc_task *task, struct nfs_server *server,
			struct nfs4_state *state, long *timeout)
{
	struct nfs4_exception exception = {
		.state = state,
	};

	if (task->tk_status >= 0)
		return 0;
	if (timeout)
		exception.timeout = *timeout;
	task->tk_status = nfs4_async_handle_exception(task, server,
			task->tk_status,
			&exception);
	if (exception.delay && timeout)
		*timeout = exception.timeout;
	if (exception.retry)
		return -EAGAIN;
	return 0;
}

/*
 * Return 'true' if 'clp' is using an rpc_client that is integrity protected
 * or 'false' otherwise.
 */
static bool _nfs4_is_integrity_protected(struct nfs_client *clp)
{
	rpc_authflavor_t flavor = clp->cl_rpcclient->cl_auth->au_flavor;
	return (flavor == RPC_AUTH_GSS_KRB5I) || (flavor == RPC_AUTH_GSS_KRB5P);
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
	struct nfs_client *clp = server->nfs_client;

	if (!nfs4_has_session(clp))
		do_renew_lease(clp, timestamp);
}

struct nfs4_call_sync_data {
	const struct nfs_server *seq_server;
	struct nfs4_sequence_args *seq_args;
	struct nfs4_sequence_res *seq_res;
};

void nfs4_init_sequence(struct nfs4_sequence_args *args,
			struct nfs4_sequence_res *res, int cache_reply,
			int privileged)
{
	args->sa_slot = NULL;
	args->sa_cache_this = cache_reply;
	args->sa_privileged = privileged;

	res->sr_slot = NULL;
}

static void nfs40_sequence_free_slot(struct nfs4_sequence_res *res)
{
	struct nfs4_slot *slot = res->sr_slot;
	struct nfs4_slot_table *tbl;

	tbl = slot->table;
	spin_lock(&tbl->slot_tbl_lock);
	if (!nfs41_wake_and_assign_slot(tbl, slot))
		nfs4_free_slot(tbl, slot);
	spin_unlock(&tbl->slot_tbl_lock);

	res->sr_slot = NULL;
}

static int nfs40_sequence_done(struct rpc_task *task,
			       struct nfs4_sequence_res *res)
{
	if (res->sr_slot != NULL)
		nfs40_sequence_free_slot(res);
	return 1;
}

#if defined(CONFIG_NFS_V4_1)

static void nfs41_release_slot(struct nfs4_slot *slot)
{
	struct nfs4_session *session;
	struct nfs4_slot_table *tbl;
	bool send_new_highest_used_slotid = false;

	if (!slot)
		return;
	tbl = slot->table;
	session = tbl->session;

	/* Bump the slot sequence number */
	if (slot->seq_done)
		slot->seq_nr++;
	slot->seq_done = 0;

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
	if (send_new_highest_used_slotid)
		nfs41_notify_server(session->clp);
	if (waitqueue_active(&tbl->slot_waitq))
		wake_up_all(&tbl->slot_waitq);
}

static void nfs41_sequence_free_slot(struct nfs4_sequence_res *res)
{
	nfs41_release_slot(res->sr_slot);
	res->sr_slot = NULL;
}

static void nfs4_slot_sequence_record_sent(struct nfs4_slot *slot,
		u32 seqnr)
{
	if ((s32)(seqnr - slot->seq_nr_highest_sent) > 0)
		slot->seq_nr_highest_sent = seqnr;
}
static void nfs4_slot_sequence_acked(struct nfs4_slot *slot,
		u32 seqnr)
{
	slot->seq_nr_highest_sent = seqnr;
	slot->seq_nr_last_acked = seqnr;
}

static void nfs4_probe_sequence(struct nfs_client *client, const struct cred *cred,
				struct nfs4_slot *slot)
{
	struct rpc_task *task = _nfs41_proc_sequence(client, cred, slot, true);
	if (!IS_ERR(task))
		rpc_put_task_async(task);
}

static int nfs41_sequence_process(struct rpc_task *task,
		struct nfs4_sequence_res *res)
{
	struct nfs4_session *session;
	struct nfs4_slot *slot = res->sr_slot;
	struct nfs_client *clp;
	int status;
	int ret = 1;

	if (slot == NULL)
		goto out_noaction;
	/* don't increment the sequence number if the task wasn't sent */
	if (!RPC_WAS_SENT(task) || slot->seq_done)
		goto out;

	session = slot->table->session;
	clp = session->clp;

	trace_nfs4_sequence_done(session, res);

	status = res->sr_status;
	if (task->tk_status == -NFS4ERR_DEADSESSION)
		status = -NFS4ERR_DEADSESSION;

	/* Check the SEQUENCE operation status */
	switch (status) {
	case 0:
		/* Mark this sequence number as having been acked */
		nfs4_slot_sequence_acked(slot, slot->seq_nr);
		/* Update the slot's sequence and clientid lease timer */
		slot->seq_done = 1;
		do_renew_lease(clp, res->sr_timestamp);
		/* Check sequence flags */
		nfs41_handle_sequence_flag_errors(clp, res->sr_status_flags,
				!!slot->privileged);
		nfs41_update_target_slotid(slot->table, slot, res);
		break;
	case 1:
		/*
		 * sr_status remains 1 if an RPC level error occurred.
		 * The server may or may not have processed the sequence
		 * operation..
		 */
		nfs4_slot_sequence_record_sent(slot, slot->seq_nr);
		slot->seq_done = 1;
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
		nfs4_slot_sequence_acked(slot, slot->seq_nr);
		goto out_retry;
	case -NFS4ERR_RETRY_UNCACHED_REP:
	case -NFS4ERR_SEQ_FALSE_RETRY:
		/*
		 * The server thinks we tried to replay a request.
		 * Retry the call after bumping the sequence ID.
		 */
		nfs4_slot_sequence_acked(slot, slot->seq_nr);
		goto retry_new_seq;
	case -NFS4ERR_BADSLOT:
		/*
		 * The slot id we used was probably retired. Try again
		 * using a different slot id.
		 */
		if (slot->slot_nr < slot->table->target_highest_slotid)
			goto session_recover;
		goto retry_nowait;
	case -NFS4ERR_SEQ_MISORDERED:
		nfs4_slot_sequence_record_sent(slot, slot->seq_nr);
		/*
		 * Were one or more calls using this slot interrupted?
		 * If the server never received the request, then our
		 * transmitted slot sequence number may be too high. However,
		 * if the server did receive the request then it might
		 * accidentally give us a reply with a mismatched operation.
		 * We can sort this out by sending a lone sequence operation
		 * to the server on the same slot.
		 */
		if ((s32)(slot->seq_nr - slot->seq_nr_last_acked) > 1) {
			slot->seq_nr--;
			if (task->tk_msg.rpc_proc != &nfs4_procedures[NFSPROC4_CLNT_SEQUENCE]) {
				nfs4_probe_sequence(clp, task->tk_msg.rpc_cred, slot);
				res->sr_slot = NULL;
			}
			goto retry_nowait;
		}
		/*
		 * RFC5661:
		 * A retry might be sent while the original request is
		 * still in progress on the replier. The replier SHOULD
		 * deal with the issue by returning NFS4ERR_DELAY as the
		 * reply to SEQUENCE or CB_SEQUENCE operation, but
		 * implementations MAY return NFS4ERR_SEQ_MISORDERED.
		 *
		 * Restart the search after a delay.
		 */
		slot->seq_nr = slot->seq_nr_highest_sent;
		goto out_retry;
	case -NFS4ERR_BADSESSION:
	case -NFS4ERR_DEADSESSION:
	case -NFS4ERR_CONN_NOT_BOUND_TO_SESSION:
		goto session_recover;
	default:
		/* Just update the slot sequence no. */
		slot->seq_done = 1;
	}
out:
	/* The session may be reset by one of the error handlers. */
	dprintk("%s: Error %d free the slot \n", __func__, res->sr_status);
out_noaction:
	return ret;
session_recover:
	nfs4_schedule_session_recovery(session, status);
	dprintk("%s ERROR: %d Reset session\n", __func__, status);
	nfs41_sequence_free_slot(res);
	goto out;
retry_new_seq:
	++slot->seq_nr;
retry_nowait:
	if (rpc_restart_call_prepare(task)) {
		nfs41_sequence_free_slot(res);
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

int nfs41_sequence_done(struct rpc_task *task, struct nfs4_sequence_res *res)
{
	if (!nfs41_sequence_process(task, res))
		return 0;
	if (res->sr_slot != NULL)
		nfs41_sequence_free_slot(res);
	return 1;

}
EXPORT_SYMBOL_GPL(nfs41_sequence_done);

static int nfs4_sequence_process(struct rpc_task *task, struct nfs4_sequence_res *res)
{
	if (res->sr_slot == NULL)
		return 1;
	if (res->sr_slot->table->session != NULL)
		return nfs41_sequence_process(task, res);
	return nfs40_sequence_done(task, res);
}

static void nfs4_sequence_free_slot(struct nfs4_sequence_res *res)
{
	if (res->sr_slot != NULL) {
		if (res->sr_slot->table->session != NULL)
			nfs41_sequence_free_slot(res);
		else
			nfs40_sequence_free_slot(res);
	}
}

int nfs4_sequence_done(struct rpc_task *task, struct nfs4_sequence_res *res)
{
	if (res->sr_slot == NULL)
		return 1;
	if (!res->sr_slot->table->session)
		return nfs40_sequence_done(task, res);
	return nfs41_sequence_done(task, res);
}
EXPORT_SYMBOL_GPL(nfs4_sequence_done);

static void nfs41_call_sync_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs4_call_sync_data *data = calldata;

	dprintk("--> %s data->seq_server %p\n", __func__, data->seq_server);

	nfs4_setup_sequence(data->seq_server->nfs_client,
			    data->seq_args, data->seq_res, task);
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

static int nfs4_sequence_process(struct rpc_task *task, struct nfs4_sequence_res *res)
{
	return nfs40_sequence_done(task, res);
}

static void nfs4_sequence_free_slot(struct nfs4_sequence_res *res)
{
	if (res->sr_slot != NULL)
		nfs40_sequence_free_slot(res);
}

int nfs4_sequence_done(struct rpc_task *task,
		       struct nfs4_sequence_res *res)
{
	return nfs40_sequence_done(task, res);
}
EXPORT_SYMBOL_GPL(nfs4_sequence_done);

#endif	/* !CONFIG_NFS_V4_1 */

static void nfs41_sequence_res_init(struct nfs4_sequence_res *res)
{
	res->sr_timestamp = jiffies;
	res->sr_status_flags = 0;
	res->sr_status = 1;
}

static
void nfs4_sequence_attach_slot(struct nfs4_sequence_args *args,
		struct nfs4_sequence_res *res,
		struct nfs4_slot *slot)
{
	if (!slot)
		return;
	slot->privileged = args->sa_privileged ? 1 : 0;
	args->sa_slot = slot;

	res->sr_slot = slot;
}

int nfs4_setup_sequence(struct nfs_client *client,
			struct nfs4_sequence_args *args,
			struct nfs4_sequence_res *res,
			struct rpc_task *task)
{
	struct nfs4_session *session = nfs4_get_session(client);
	struct nfs4_slot_table *tbl  = client->cl_slot_tbl;
	struct nfs4_slot *slot;

	/* slot already allocated? */
	if (res->sr_slot != NULL)
		goto out_start;

	if (session)
		tbl = &session->fc_slot_table;

	spin_lock(&tbl->slot_tbl_lock);
	/* The state manager will wait until the slot table is empty */
	if (nfs4_slot_tbl_draining(tbl) && !args->sa_privileged)
		goto out_sleep;

	slot = nfs4_alloc_slot(tbl);
	if (IS_ERR(slot)) {
		if (slot == ERR_PTR(-ENOMEM))
			goto out_sleep_timeout;
		goto out_sleep;
	}
	spin_unlock(&tbl->slot_tbl_lock);

	nfs4_sequence_attach_slot(args, res, slot);

	trace_nfs4_setup_sequence(session, args);
out_start:
	nfs41_sequence_res_init(res);
	rpc_call_start(task);
	return 0;
out_sleep_timeout:
	/* Try again in 1/4 second */
	if (args->sa_privileged)
		rpc_sleep_on_priority_timeout(&tbl->slot_tbl_waitq, task,
				jiffies + (HZ >> 2), RPC_PRIORITY_PRIVILEGED);
	else
		rpc_sleep_on_timeout(&tbl->slot_tbl_waitq, task,
				NULL, jiffies + (HZ >> 2));
	spin_unlock(&tbl->slot_tbl_lock);
	return -EAGAIN;
out_sleep:
	if (args->sa_privileged)
		rpc_sleep_on_priority(&tbl->slot_tbl_waitq, task,
				RPC_PRIORITY_PRIVILEGED);
	else
		rpc_sleep_on(&tbl->slot_tbl_waitq, task, NULL);
	spin_unlock(&tbl->slot_tbl_lock);
	return -EAGAIN;
}
EXPORT_SYMBOL_GPL(nfs4_setup_sequence);

static void nfs40_call_sync_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs4_call_sync_data *data = calldata;
	nfs4_setup_sequence(data->seq_server->nfs_client,
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

static int nfs4_call_sync_custom(struct rpc_task_setup *task_setup)
{
	int ret;
	struct rpc_task *task;

	task = rpc_run_task(task_setup);
	if (IS_ERR(task))
		return PTR_ERR(task);

	ret = task->tk_status;
	rpc_put_task(task);
	return ret;
}

static int nfs4_do_call_sync(struct rpc_clnt *clnt,
			     struct nfs_server *server,
			     struct rpc_message *msg,
			     struct nfs4_sequence_args *args,
			     struct nfs4_sequence_res *res,
			     unsigned short task_flags)
{
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
		.callback_data = &data,
		.flags = task_flags,
	};

	return nfs4_call_sync_custom(&task_setup);
}

static int nfs4_call_sync_sequence(struct rpc_clnt *clnt,
				   struct nfs_server *server,
				   struct rpc_message *msg,
				   struct nfs4_sequence_args *args,
				   struct nfs4_sequence_res *res)
{
	return nfs4_do_call_sync(clnt, server, msg, args, res, 0);
}


int nfs4_call_sync(struct rpc_clnt *clnt,
		   struct nfs_server *server,
		   struct rpc_message *msg,
		   struct nfs4_sequence_args *args,
		   struct nfs4_sequence_res *res,
		   int cache_reply)
{
	nfs4_init_sequence(args, res, cache_reply, 0);
	return nfs4_call_sync_sequence(clnt, server, msg, args, res);
}

static void
nfs4_inc_nlink_locked(struct inode *inode)
{
	NFS_I(inode)->cache_validity |= NFS_INO_INVALID_OTHER;
	inc_nlink(inode);
}

static void
nfs4_dec_nlink_locked(struct inode *inode)
{
	NFS_I(inode)->cache_validity |= NFS_INO_INVALID_OTHER;
	drop_nlink(inode);
}

static void
nfs4_update_changeattr_locked(struct inode *inode,
		struct nfs4_change_info *cinfo,
		unsigned long timestamp, unsigned long cache_validity)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	nfsi->cache_validity |= NFS_INO_INVALID_CTIME
		| NFS_INO_INVALID_MTIME
		| cache_validity;

	if (cinfo->atomic && cinfo->before == inode_peek_iversion_raw(inode)) {
		nfsi->cache_validity &= ~NFS_INO_REVAL_PAGECACHE;
		nfsi->attrtimeo_timestamp = jiffies;
	} else {
		if (S_ISDIR(inode->i_mode)) {
			nfsi->cache_validity |= NFS_INO_INVALID_DATA;
			nfs_force_lookup_revalidate(inode);
		} else {
			if (!NFS_PROTO(inode)->have_delegation(inode,
							       FMODE_READ))
				nfsi->cache_validity |= NFS_INO_REVAL_PAGECACHE;
		}

		if (cinfo->before != inode_peek_iversion_raw(inode))
			nfsi->cache_validity |= NFS_INO_INVALID_ACCESS |
						NFS_INO_INVALID_ACL |
						NFS_INO_INVALID_XATTR;
	}
	inode_set_iversion_raw(inode, cinfo->after);
	nfsi->read_cache_jiffies = timestamp;
	nfsi->attr_gencount = nfs_inc_attr_generation_counter();
	nfsi->cache_validity &= ~NFS_INO_INVALID_CHANGE;

	if (nfsi->cache_validity & NFS_INO_INVALID_DATA)
		nfs_fscache_invalidate(inode);
}

void
nfs4_update_changeattr(struct inode *dir, struct nfs4_change_info *cinfo,
		unsigned long timestamp, unsigned long cache_validity)
{
	spin_lock(&dir->i_lock);
	nfs4_update_changeattr_locked(dir, cinfo, timestamp, cache_validity);
	spin_unlock(&dir->i_lock);
}

struct nfs4_open_createattrs {
	struct nfs4_label *label;
	struct iattr *sattr;
	const __u32 verf[2];
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

static fmode_t _nfs4_ctx_to_accessmode(const struct nfs_open_context *ctx)
{
	 return ctx->mode & (FMODE_READ|FMODE_WRITE|FMODE_EXEC);
}

static fmode_t _nfs4_ctx_to_openmode(const struct nfs_open_context *ctx)
{
	fmode_t ret = ctx->mode & (FMODE_READ|FMODE_WRITE);

	return (ctx->mode & FMODE_EXEC) ? FMODE_READ | ret : ret;
}

static u32
nfs4_map_atomic_open_share(struct nfs_server *server,
		fmode_t fmode, int openflags)
{
	u32 res = 0;

	switch (fmode & (FMODE_READ | FMODE_WRITE)) {
	case FMODE_READ:
		res = NFS4_SHARE_ACCESS_READ;
		break;
	case FMODE_WRITE:
		res = NFS4_SHARE_ACCESS_WRITE;
		break;
	case FMODE_READ|FMODE_WRITE:
		res = NFS4_SHARE_ACCESS_BOTH;
	}
	if (!(server->caps & NFS_CAP_ATOMIC_OPEN_V1))
		goto out;
	/* Want no delegation if we're using O_DIRECT */
	if (openflags & O_DIRECT)
		res |= NFS4_SHARE_WANT_NO_DELEG;
out:
	return res;
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
		const struct nfs4_open_createattrs *c,
		enum open_claim_type4 claim,
		gfp_t gfp_mask)
{
	struct dentry *parent = dget_parent(dentry);
	struct inode *dir = d_inode(parent);
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs_seqid *(*alloc_seqid)(struct nfs_seqid_counter *, gfp_t);
	struct nfs4_label *label = (c != NULL) ? c->label : NULL;
	struct nfs4_opendata *p;

	p = kzalloc(sizeof(*p), gfp_mask);
	if (p == NULL)
		goto err;

	p->f_label = nfs4_label_alloc(server, gfp_mask);
	if (IS_ERR(p->f_label))
		goto err_free_p;

	p->a_label = nfs4_label_alloc(server, gfp_mask);
	if (IS_ERR(p->a_label))
		goto err_free_f;

	alloc_seqid = server->nfs_client->cl_mvops->alloc_seqid;
	p->o_arg.seqid = alloc_seqid(&sp->so_seqid, gfp_mask);
	if (IS_ERR(p->o_arg.seqid))
		goto err_free_label;
	nfs_sb_active(dentry->d_sb);
	p->dentry = dget(dentry);
	p->dir = parent;
	p->owner = sp;
	atomic_inc(&sp->so_count);
	p->o_arg.open_flags = flags;
	p->o_arg.fmode = fmode & (FMODE_READ|FMODE_WRITE);
	p->o_arg.claim = nfs4_map_atomic_open_claim(server, claim);
	p->o_arg.share_access = nfs4_map_atomic_open_share(server,
			fmode, flags);
	if (flags & O_CREAT) {
		p->o_arg.umask = current_umask();
		p->o_arg.label = nfs4_label_copy(p->a_label, label);
		if (c->sattr != NULL && c->sattr->ia_valid != 0) {
			p->o_arg.u.attrs = &p->attrs;
			memcpy(&p->attrs, c->sattr, sizeof(p->attrs));

			memcpy(p->o_arg.u.verifier.data, c->verf,
					sizeof(p->o_arg.u.verifier.data));
		}
	}
	/* don't put an ACCESS op in OPEN compound if O_EXCL, because ACCESS
	 * will return permission denied for all bits until close */
	if (!(flags & O_EXCL)) {
		/* ask server to check for all possible rights as results
		 * are cached */
		switch (p->o_arg.claim) {
		default:
			break;
		case NFS4_OPEN_CLAIM_NULL:
		case NFS4_OPEN_CLAIM_FH:
			p->o_arg.access = NFS4_ACCESS_READ |
				NFS4_ACCESS_MODIFY |
				NFS4_ACCESS_EXTEND |
				NFS4_ACCESS_EXECUTE;
#ifdef CONFIG_NFS_V4_2
			if (server->caps & NFS_CAP_XATTR)
				p->o_arg.access |= NFS4_ACCESS_XAREAD |
				    NFS4_ACCESS_XAWRITE |
				    NFS4_ACCESS_XALIST;
#endif
		}
	}
	p->o_arg.clientid = server->nfs_client->cl_clientid;
	p->o_arg.id.create_time = ktime_to_ns(sp->so_seqid.create_time);
	p->o_arg.id.uniquifier = sp->so_seqid.owner_id;
	p->o_arg.name = &dentry->d_name;
	p->o_arg.server = server;
	p->o_arg.bitmask = nfs4_bitmask(server, label);
	p->o_arg.open_bitmap = &nfs4_fattr_bitmap[0];
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
		p->o_arg.fh = NFS_FH(d_inode(dentry));
	}
	p->c_arg.fh = &p->o_res.fh;
	p->c_arg.stateid = &p->o_res.stateid;
	p->c_arg.seqid = p->o_arg.seqid;
	nfs4_init_opendata_res(p);
	kref_init(&p->kref);
	return p;

err_free_label:
	nfs4_label_free(p->a_label);
err_free_f:
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

	nfs4_lgopen_release(p->lgp);
	nfs_free_seqid(p->o_arg.seqid);
	nfs4_sequence_free_slot(&p->o_res.seq_res);
	if (p->state != NULL)
		nfs4_put_open_state(p->state);
	nfs4_put_state_owner(p->owner);

	nfs4_label_free(p->a_label);
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

static bool nfs4_mode_match_open_stateid(struct nfs4_state *state,
		fmode_t fmode)
{
	switch(fmode & (FMODE_READ|FMODE_WRITE)) {
	case FMODE_READ|FMODE_WRITE:
		return state->n_rdwr != 0;
	case FMODE_WRITE:
		return state->n_wronly != 0;
	case FMODE_READ:
		return state->n_rdonly != 0;
	}
	WARN_ON_ONCE(1);
	return false;
}

static int can_open_cached(struct nfs4_state *state, fmode_t mode,
		int open_mode, enum open_claim_type4 claim)
{
	int ret = 0;

	if (open_mode & (O_EXCL|O_TRUNC))
		goto out;
	switch (claim) {
	case NFS4_OPEN_CLAIM_NULL:
	case NFS4_OPEN_CLAIM_FH:
		goto out;
	default:
		break;
	}
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

static int can_open_delegated(struct nfs_delegation *delegation, fmode_t fmode,
		enum open_claim_type4 claim)
{
	if (delegation == NULL)
		return 0;
	if ((delegation->type & fmode) != fmode)
		return 0;
	switch (claim) {
	case NFS4_OPEN_CLAIM_NULL:
	case NFS4_OPEN_CLAIM_FH:
		break;
	case NFS4_OPEN_CLAIM_PREVIOUS:
		if (!test_bit(NFS_DELEGATION_NEED_RECLAIM, &delegation->flags))
			break;
		fallthrough;
	default:
		return 0;
	}
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

#ifdef CONFIG_NFS_V4_1
static bool nfs_open_stateid_recover_openmode(struct nfs4_state *state)
{
	if (state->n_rdonly && !test_bit(NFS_O_RDONLY_STATE, &state->flags))
		return true;
	if (state->n_wronly && !test_bit(NFS_O_WRONLY_STATE, &state->flags))
		return true;
	if (state->n_rdwr && !test_bit(NFS_O_RDWR_STATE, &state->flags))
		return true;
	return false;
}
#endif /* CONFIG_NFS_V4_1 */

static void nfs_state_log_update_open_stateid(struct nfs4_state *state)
{
	if (test_and_clear_bit(NFS_STATE_CHANGE_WAIT, &state->flags))
		wake_up_all(&state->waitq);
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

/*
 * Check for whether or not the caller may update the open stateid
 * to the value passed in by stateid.
 *
 * Note: This function relies heavily on the server implementing
 * RFC7530 Section 9.1.4.2, and RFC5661 Section 8.2.2
 * correctly.
 * i.e. The stateid seqids have to be initialised to 1, and
 * are then incremented on every state transition.
 */
static bool nfs_stateid_is_sequential(struct nfs4_state *state,
		const nfs4_stateid *stateid)
{
	if (test_bit(NFS_OPEN_STATE, &state->flags)) {
		/* The common case - we're updating to a new sequence number */
		if (nfs4_stateid_match_other(stateid, &state->open_stateid)) {
			if (nfs4_stateid_is_next(&state->open_stateid, stateid))
				return true;
			return false;
		}
		/* The server returned a new stateid */
	}
	/* This is the first OPEN in this generation */
	if (stateid->seqid == cpu_to_be32(1))
		return true;
	return false;
}

static void nfs_resync_open_stateid_locked(struct nfs4_state *state)
{
	if (!(state->n_wronly || state->n_rdonly || state->n_rdwr))
		return;
	if (state->n_wronly)
		set_bit(NFS_O_WRONLY_STATE, &state->flags);
	if (state->n_rdonly)
		set_bit(NFS_O_RDONLY_STATE, &state->flags);
	if (state->n_rdwr)
		set_bit(NFS_O_RDWR_STATE, &state->flags);
	set_bit(NFS_OPEN_STATE, &state->flags);
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
	/* Handle OPEN+OPEN_DOWNGRADE races */
	if (nfs4_stateid_match_other(stateid, &state->open_stateid) &&
	    !nfs4_stateid_is_newer(stateid, &state->open_stateid)) {
		nfs_resync_open_stateid_locked(state);
		goto out;
	}
	if (test_bit(NFS_DELEGATED_STATE, &state->flags) == 0)
		nfs4_stateid_copy(&state->stateid, stateid);
	nfs4_stateid_copy(&state->open_stateid, stateid);
	trace_nfs4_open_stateid_update(state->inode, stateid, 0);
out:
	nfs_state_log_update_open_stateid(state);
}

static void nfs_clear_open_stateid(struct nfs4_state *state,
	nfs4_stateid *arg_stateid,
	nfs4_stateid *stateid, fmode_t fmode)
{
	write_seqlock(&state->seqlock);
	/* Ignore, if the CLOSE argment doesn't match the current stateid */
	if (nfs4_state_match_open_stateid_other(state, arg_stateid))
		nfs_clear_open_stateid_locked(state, stateid, fmode);
	write_sequnlock(&state->seqlock);
	if (test_bit(NFS_STATE_RECLAIM_NOGRACE, &state->flags))
		nfs4_schedule_state_manager(state->owner->so_server->nfs_client);
}

static void nfs_set_open_stateid_locked(struct nfs4_state *state,
		const nfs4_stateid *stateid, nfs4_stateid *freeme)
	__must_hold(&state->owner->so_lock)
	__must_hold(&state->seqlock)
	__must_hold(RCU)

{
	DEFINE_WAIT(wait);
	int status = 0;
	for (;;) {

		if (nfs_stateid_is_sequential(state, stateid))
			break;

		if (status)
			break;
		/* Rely on seqids for serialisation with NFSv4.0 */
		if (!nfs4_has_session(NFS_SERVER(state->inode)->nfs_client))
			break;

		set_bit(NFS_STATE_CHANGE_WAIT, &state->flags);
		prepare_to_wait(&state->waitq, &wait, TASK_KILLABLE);
		/*
		 * Ensure we process the state changes in the same order
		 * in which the server processed them by delaying the
		 * update of the stateid until we are in sequence.
		 */
		write_sequnlock(&state->seqlock);
		spin_unlock(&state->owner->so_lock);
		rcu_read_unlock();
		trace_nfs4_open_stateid_update_wait(state->inode, stateid, 0);

		if (!fatal_signal_pending(current)) {
			if (schedule_timeout(5*HZ) == 0)
				status = -EAGAIN;
			else
				status = 0;
		} else
			status = -EINTR;
		finish_wait(&state->waitq, &wait);
		rcu_read_lock();
		spin_lock(&state->owner->so_lock);
		write_seqlock(&state->seqlock);
	}

	if (test_bit(NFS_OPEN_STATE, &state->flags) &&
	    !nfs4_stateid_match_other(stateid, &state->open_stateid)) {
		nfs4_stateid_copy(freeme, &state->open_stateid);
		nfs_test_and_clear_all_open_stateid(state);
	}

	if (test_bit(NFS_DELEGATED_STATE, &state->flags) == 0)
		nfs4_stateid_copy(&state->stateid, stateid);
	nfs4_stateid_copy(&state->open_stateid, stateid);
	trace_nfs4_open_stateid_update(state->inode, stateid, status);
	nfs_state_log_update_open_stateid(state);
}

static void nfs_state_set_open_stateid(struct nfs4_state *state,
		const nfs4_stateid *open_stateid,
		fmode_t fmode,
		nfs4_stateid *freeme)
{
	/*
	 * Protect the call to nfs4_state_set_mode_locked and
	 * serialise the stateid update
	 */
	write_seqlock(&state->seqlock);
	nfs_set_open_stateid_locked(state, open_stateid, freeme);
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
	set_bit(NFS_OPEN_STATE, &state->flags);
	write_sequnlock(&state->seqlock);
}

static void nfs_state_clear_open_state_flags(struct nfs4_state *state)
{
	clear_bit(NFS_O_RDWR_STATE, &state->flags);
	clear_bit(NFS_O_WRONLY_STATE, &state->flags);
	clear_bit(NFS_O_RDONLY_STATE, &state->flags);
	clear_bit(NFS_OPEN_STATE, &state->flags);
}

static void nfs_state_set_delegation(struct nfs4_state *state,
		const nfs4_stateid *deleg_stateid,
		fmode_t fmode)
{
	/*
	 * Protect the call to nfs4_state_set_mode_locked and
	 * serialise the stateid update
	 */
	write_seqlock(&state->seqlock);
	nfs4_stateid_copy(&state->stateid, deleg_stateid);
	set_bit(NFS_DELEGATED_STATE, &state->flags);
	write_sequnlock(&state->seqlock);
}

static void nfs_state_clear_delegation(struct nfs4_state *state)
{
	write_seqlock(&state->seqlock);
	nfs4_stateid_copy(&state->stateid, &state->open_stateid);
	clear_bit(NFS_DELEGATED_STATE, &state->flags);
	write_sequnlock(&state->seqlock);
}

int update_open_stateid(struct nfs4_state *state,
		const nfs4_stateid *open_stateid,
		const nfs4_stateid *delegation,
		fmode_t fmode)
{
	struct nfs_server *server = NFS_SERVER(state->inode);
	struct nfs_client *clp = server->nfs_client;
	struct nfs_inode *nfsi = NFS_I(state->inode);
	struct nfs_delegation *deleg_cur;
	nfs4_stateid freeme = { };
	int ret = 0;

	fmode &= (FMODE_READ|FMODE_WRITE);

	rcu_read_lock();
	spin_lock(&state->owner->so_lock);
	if (open_stateid != NULL) {
		nfs_state_set_open_stateid(state, open_stateid, fmode, &freeme);
		ret = 1;
	}

	deleg_cur = nfs4_get_valid_delegation(state->inode);
	if (deleg_cur == NULL)
		goto no_delegation;

	spin_lock(&deleg_cur->lock);
	if (rcu_dereference(nfsi->delegation) != deleg_cur ||
	   test_bit(NFS_DELEGATION_RETURNING, &deleg_cur->flags) ||
	    (deleg_cur->type & fmode) != fmode)
		goto no_delegation_unlock;

	if (delegation == NULL)
		delegation = &deleg_cur->stateid;
	else if (!nfs4_stateid_match_other(&deleg_cur->stateid, delegation))
		goto no_delegation_unlock;

	nfs_mark_delegation_referenced(deleg_cur);
	nfs_state_set_delegation(state, &deleg_cur->stateid, fmode);
	ret = 1;
no_delegation_unlock:
	spin_unlock(&deleg_cur->lock);
no_delegation:
	if (ret)
		update_open_stateflags(state, fmode);
	spin_unlock(&state->owner->so_lock);
	rcu_read_unlock();

	if (test_bit(NFS_STATE_RECLAIM_NOGRACE, &state->flags))
		nfs4_schedule_state_manager(clp);
	if (freeme.type != 0)
		nfs4_test_and_free_stateid(server, &freeme,
				state->owner->so_cred);

	return ret;
}

static bool nfs4_update_lock_stateid(struct nfs4_lock_state *lsp,
		const nfs4_stateid *stateid)
{
	struct nfs4_state *state = lsp->ls_state;
	bool ret = false;

	spin_lock(&state->state_lock);
	if (!nfs4_stateid_match_other(stateid, &lsp->ls_stateid))
		goto out_noupdate;
	if (!nfs4_stateid_is_newer(stateid, &lsp->ls_stateid))
		goto out_noupdate;
	nfs4_stateid_copy(&lsp->ls_stateid, stateid);
	ret = true;
out_noupdate:
	spin_unlock(&state->state_lock);
	return ret;
}

static void nfs4_return_incompatible_delegation(struct inode *inode, fmode_t fmode)
{
	struct nfs_delegation *delegation;

	fmode &= FMODE_READ|FMODE_WRITE;
	rcu_read_lock();
	delegation = nfs4_get_valid_delegation(inode);
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
	struct nfs_delegation *delegation;
	int open_mode = opendata->o_arg.open_flags;
	fmode_t fmode = opendata->o_arg.fmode;
	enum open_claim_type4 claim = opendata->o_arg.claim;
	nfs4_stateid stateid;
	int ret = -EAGAIN;

	for (;;) {
		spin_lock(&state->owner->so_lock);
		if (can_open_cached(state, fmode, open_mode, claim)) {
			update_open_stateflags(state, fmode);
			spin_unlock(&state->owner->so_lock);
			goto out_return_state;
		}
		spin_unlock(&state->owner->so_lock);
		rcu_read_lock();
		delegation = nfs4_get_valid_delegation(state->inode);
		if (!can_open_delegated(delegation, fmode, claim)) {
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
	refcount_inc(&state->count);
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
	switch (data->o_arg.claim) {
	default:
		break;
	case NFS4_OPEN_CLAIM_DELEGATE_CUR:
	case NFS4_OPEN_CLAIM_DELEG_CUR_FH:
		pr_err_ratelimited("NFS: Broken NFSv4 server %s is "
				   "returning a delegation for "
				   "OPEN(CLAIM_DELEGATE_CUR)\n",
				   clp->cl_hostname);
		return;
	}
	if ((delegation_flags & 1UL<<NFS_DELEGATION_NEED_RECLAIM) == 0)
		nfs_inode_set_delegation(state->inode,
				data->owner->so_cred,
				data->o_res.delegation_type,
				&data->o_res.delegation,
				data->o_res.pagemod_limit);
	else
		nfs_inode_reclaim_delegation(state->inode,
				data->owner->so_cred,
				data->o_res.delegation_type,
				&data->o_res.delegation,
				data->o_res.pagemod_limit);

	if (data->o_res.do_recall)
		nfs_async_inode_return_delegation(state->inode,
						  &data->o_res.delegation);
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
		if (data->rpc_status)
			return ERR_PTR(data->rpc_status);
		/* cached opens have already been processed */
		goto update;
	}

	ret = nfs_refresh_inode(inode, &data->f_attr);
	if (ret)
		return ERR_PTR(ret);

	if (data->o_res.delegation_type != 0)
		nfs4_opendata_check_deleg(data, state);
update:
	if (!update_open_stateid(state, &data->o_res.stateid,
				NULL, data->o_arg.fmode))
		return ERR_PTR(-EAGAIN);
	refcount_inc(&state->count);

	return state;
}

static struct inode *
nfs4_opendata_get_inode(struct nfs4_opendata *data)
{
	struct inode *inode;

	switch (data->o_arg.claim) {
	case NFS4_OPEN_CLAIM_NULL:
	case NFS4_OPEN_CLAIM_DELEGATE_CUR:
	case NFS4_OPEN_CLAIM_DELEGATE_PREV:
		if (!(data->f_attr.valid & NFS_ATTR_FATTR))
			return ERR_PTR(-EAGAIN);
		inode = nfs_fhget(data->dir->d_sb, &data->o_res.fh,
				&data->f_attr, data->f_label);
		break;
	default:
		inode = d_inode(data->dentry);
		ihold(inode);
		nfs_refresh_inode(inode, &data->f_attr);
	}
	return inode;
}

static struct nfs4_state *
nfs4_opendata_find_nfs4_state(struct nfs4_opendata *data)
{
	struct nfs4_state *state;
	struct inode *inode;

	inode = nfs4_opendata_get_inode(data);
	if (IS_ERR(inode))
		return ERR_CAST(inode);
	if (data->state != NULL && data->state->inode == inode) {
		state = data->state;
		refcount_inc(&state->count);
	} else
		state = nfs4_get_open_state(inode, data->owner);
	iput(inode);
	if (state == NULL)
		state = ERR_PTR(-ENOMEM);
	return state;
}

static struct nfs4_state *
_nfs4_opendata_to_nfs4_state(struct nfs4_opendata *data)
{
	struct nfs4_state *state;

	if (!data->rpc_done) {
		state = nfs4_try_open_cached(data);
		trace_nfs4_cached_open(data->state);
		goto out;
	}

	state = nfs4_opendata_find_nfs4_state(data);
	if (IS_ERR(state))
		goto out;

	if (data->o_res.delegation_type != 0)
		nfs4_opendata_check_deleg(data, state);
	if (!update_open_stateid(state, &data->o_res.stateid,
				NULL, data->o_arg.fmode)) {
		nfs4_put_open_state(state);
		state = ERR_PTR(-EAGAIN);
	}
out:
	nfs_release_seqid(data->o_arg.seqid);
	return state;
}

static struct nfs4_state *
nfs4_opendata_to_nfs4_state(struct nfs4_opendata *data)
{
	struct nfs4_state *ret;

	if (data->o_arg.claim == NFS4_OPEN_CLAIM_PREVIOUS)
		ret =_nfs4_opendata_reclaim_to_nfs4_state(data);
	else
		ret = _nfs4_opendata_to_nfs4_state(data);
	nfs4_sequence_free_slot(&data->o_res.seq_res);
	return ret;
}

static struct nfs_open_context *
nfs4_state_find_open_context_mode(struct nfs4_state *state, fmode_t mode)
{
	struct nfs_inode *nfsi = NFS_I(state->inode);
	struct nfs_open_context *ctx;

	rcu_read_lock();
	list_for_each_entry_rcu(ctx, &nfsi->open_files, list) {
		if (ctx->state != state)
			continue;
		if ((ctx->mode & mode) != mode)
			continue;
		if (!get_nfs_open_context(ctx))
			continue;
		rcu_read_unlock();
		return ctx;
	}
	rcu_read_unlock();
	return ERR_PTR(-ENOENT);
}

static struct nfs_open_context *
nfs4_state_find_open_context(struct nfs4_state *state)
{
	struct nfs_open_context *ctx;

	ctx = nfs4_state_find_open_context_mode(state, FMODE_READ|FMODE_WRITE);
	if (!IS_ERR(ctx))
		return ctx;
	ctx = nfs4_state_find_open_context_mode(state, FMODE_WRITE);
	if (!IS_ERR(ctx))
		return ctx;
	return nfs4_state_find_open_context_mode(state, FMODE_READ);
}

static struct nfs4_opendata *nfs4_open_recoverdata_alloc(struct nfs_open_context *ctx,
		struct nfs4_state *state, enum open_claim_type4 claim)
{
	struct nfs4_opendata *opendata;

	opendata = nfs4_opendata_alloc(ctx->dentry, state->owner, 0, 0,
			NULL, claim, GFP_NOFS);
	if (opendata == NULL)
		return ERR_PTR(-ENOMEM);
	opendata->state = state;
	refcount_inc(&state->count);
	return opendata;
}

static int nfs4_open_recover_helper(struct nfs4_opendata *opendata,
		fmode_t fmode)
{
	struct nfs4_state *newstate;
	int ret;

	if (!nfs4_mode_match_open_stateid(opendata->state, fmode))
		return 0;
	opendata->o_arg.open_flags = 0;
	opendata->o_arg.fmode = fmode;
	opendata->o_arg.share_access = nfs4_map_atomic_open_share(
			NFS_SB(opendata->dentry->d_sb),
			fmode, 0);
	memset(&opendata->o_res, 0, sizeof(opendata->o_res));
	memset(&opendata->c_res, 0, sizeof(opendata->c_res));
	nfs4_init_opendata_res(opendata);
	ret = _nfs4_recover_proc_open(opendata);
	if (ret != 0)
		return ret; 
	newstate = nfs4_opendata_to_nfs4_state(opendata);
	if (IS_ERR(newstate))
		return PTR_ERR(newstate);
	if (newstate != opendata->state)
		ret = -ESTALE;
	nfs4_close_state(newstate, fmode);
	return ret;
}

static int nfs4_open_recover(struct nfs4_opendata *opendata, struct nfs4_state *state)
{
	int ret;

	/* memory barrier prior to reading state->n_* */
	smp_rmb();
	ret = nfs4_open_recover_helper(opendata, FMODE_READ|FMODE_WRITE);
	if (ret != 0)
		return ret;
	ret = nfs4_open_recover_helper(opendata, FMODE_WRITE);
	if (ret != 0)
		return ret;
	ret = nfs4_open_recover_helper(opendata, FMODE_READ);
	if (ret != 0)
		return ret;
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
	clear_bit(NFS_DELEGATED_STATE, &state->flags);
	nfs_state_clear_open_state_flags(state);
	ret = nfs4_do_open_reclaim(ctx, state);
	put_nfs_open_context(ctx);
	return ret;
}

static int nfs4_handle_delegation_recall_error(struct nfs_server *server, struct nfs4_state *state, const nfs4_stateid *stateid, struct file_lock *fl, int err)
{
	switch (err) {
		default:
			printk(KERN_ERR "NFS: %s: unhandled error "
					"%d.\n", __func__, err);
		case 0:
		case -ENOENT:
		case -EAGAIN:
		case -ESTALE:
		case -ETIMEDOUT:
			break;
		case -NFS4ERR_BADSESSION:
		case -NFS4ERR_BADSLOT:
		case -NFS4ERR_BAD_HIGH_SLOT:
		case -NFS4ERR_CONN_NOT_BOUND_TO_SESSION:
		case -NFS4ERR_DEADSESSION:
			return -EAGAIN;
		case -NFS4ERR_STALE_CLIENTID:
		case -NFS4ERR_STALE_STATEID:
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
		case -NFS4ERR_EXPIRED:
		case -NFS4ERR_BAD_STATEID:
		case -NFS4ERR_OPENMODE:
			nfs_inode_find_state_and_recover(state->inode,
					stateid);
			nfs4_schedule_stateid_recovery(server, state);
			return -EAGAIN;
		case -NFS4ERR_DELAY:
		case -NFS4ERR_GRACE:
			ssleep(1);
			return -EAGAIN;
		case -ENOMEM:
		case -NFS4ERR_DENIED:
			if (fl) {
				struct nfs4_lock_state *lsp = fl->fl_u.nfs4_fl.owner;
				if (lsp)
					set_bit(NFS_LOCK_LOST, &lsp->ls_flags);
			}
			return 0;
	}
	return err;
}

int nfs4_open_delegation_recall(struct nfs_open_context *ctx,
		struct nfs4_state *state, const nfs4_stateid *stateid)
{
	struct nfs_server *server = NFS_SERVER(state->inode);
	struct nfs4_opendata *opendata;
	int err = 0;

	opendata = nfs4_open_recoverdata_alloc(ctx, state,
			NFS4_OPEN_CLAIM_DELEG_CUR_FH);
	if (IS_ERR(opendata))
		return PTR_ERR(opendata);
	nfs4_stateid_copy(&opendata->o_arg.u.delegation, stateid);
	if (!test_bit(NFS_O_RDWR_STATE, &state->flags)) {
		err = nfs4_open_recover_helper(opendata, FMODE_READ|FMODE_WRITE);
		if (err)
			goto out;
	}
	if (!test_bit(NFS_O_WRONLY_STATE, &state->flags)) {
		err = nfs4_open_recover_helper(opendata, FMODE_WRITE);
		if (err)
			goto out;
	}
	if (!test_bit(NFS_O_RDONLY_STATE, &state->flags)) {
		err = nfs4_open_recover_helper(opendata, FMODE_READ);
		if (err)
			goto out;
	}
	nfs_state_clear_delegation(state);
out:
	nfs4_opendata_put(opendata);
	return nfs4_handle_delegation_recall_error(server, state, stateid, NULL, err);
}

static void nfs4_open_confirm_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs4_opendata *data = calldata;

	nfs4_setup_sequence(data->o_arg.server->nfs_client,
			   &data->c_arg.seq_args, &data->c_res.seq_res, task);
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
		data->rpc_done = true;
	}
}

static void nfs4_open_confirm_release(void *calldata)
{
	struct nfs4_opendata *data = calldata;
	struct nfs4_state *state = NULL;

	/* If this request hasn't been cancelled, do nothing */
	if (!data->cancelled)
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
	struct nfs_server *server = NFS_SERVER(d_inode(data->dir));
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
		.flags = RPC_TASK_ASYNC | RPC_TASK_CRED_NOREF,
	};
	int status;

	nfs4_init_sequence(&data->c_arg.seq_args, &data->c_res.seq_res, 1,
				data->is_recover);
	kref_get(&data->kref);
	data->rpc_done = false;
	data->rpc_status = 0;
	data->timestamp = jiffies;
	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	status = rpc_wait_for_completion_task(task);
	if (status != 0) {
		data->cancelled = true;
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
	enum open_claim_type4 claim = data->o_arg.claim;

	if (nfs_wait_on_sequence(data->o_arg.seqid, task) != 0)
		goto out_wait;
	/*
	 * Check if we still need to send an OPEN call, or if we can use
	 * a delegation instead.
	 */
	if (data->state != NULL) {
		struct nfs_delegation *delegation;

		if (can_open_cached(data->state, data->o_arg.fmode,
					data->o_arg.open_flags, claim))
			goto out_no_action;
		rcu_read_lock();
		delegation = nfs4_get_valid_delegation(data->state->inode);
		if (can_open_delegated(delegation, data->o_arg.fmode, claim))
			goto unlock_no_action;
		rcu_read_unlock();
	}
	/* Update client id. */
	data->o_arg.clientid = clp->cl_clientid;
	switch (claim) {
	default:
		break;
	case NFS4_OPEN_CLAIM_PREVIOUS:
	case NFS4_OPEN_CLAIM_DELEG_CUR_FH:
	case NFS4_OPEN_CLAIM_DELEG_PREV_FH:
		data->o_arg.open_bitmap = &nfs4_open_noattr_bitmap[0];
		fallthrough;
	case NFS4_OPEN_CLAIM_FH:
		task->tk_msg.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_OPEN_NOATTR];
	}
	data->timestamp = jiffies;
	if (nfs4_setup_sequence(data->o_arg.server->nfs_client,
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
	trace_nfs4_cached_open(data->state);
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

	if (!nfs4_sequence_process(task, &data->o_res.seq_res))
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
	data->rpc_done = true;
}

static void nfs4_open_release(void *calldata)
{
	struct nfs4_opendata *data = calldata;
	struct nfs4_state *state = NULL;

	/* If this request hasn't been cancelled, do nothing */
	if (!data->cancelled)
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

static int nfs4_run_open_task(struct nfs4_opendata *data,
			      struct nfs_open_context *ctx)
{
	struct inode *dir = d_inode(data->dir);
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
		.flags = RPC_TASK_ASYNC | RPC_TASK_CRED_NOREF,
	};
	int status;

	kref_get(&data->kref);
	data->rpc_done = false;
	data->rpc_status = 0;
	data->cancelled = false;
	data->is_recover = false;
	if (!ctx) {
		nfs4_init_sequence(&o_arg->seq_args, &o_res->seq_res, 1, 1);
		data->is_recover = true;
		task_setup_data.flags |= RPC_TASK_TIMEOUT;
	} else {
		nfs4_init_sequence(&o_arg->seq_args, &o_res->seq_res, 1, 0);
		pnfs_lgopen_prepare(data, ctx);
	}
	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	status = rpc_wait_for_completion_task(task);
	if (status != 0) {
		data->cancelled = true;
		smp_wmb();
	} else
		status = data->rpc_status;
	rpc_put_task(task);

	return status;
}

static int _nfs4_recover_proc_open(struct nfs4_opendata *data)
{
	struct inode *dir = d_inode(data->dir);
	struct nfs_openres *o_res = &data->o_res;
	int status;

	status = nfs4_run_open_task(data, NULL);
	if (status != 0 || !data->rpc_done)
		return status;

	nfs_fattr_map_and_free_names(NFS_SERVER(dir), &data->f_attr);

	if (o_res->rflags & NFS4_OPEN_RESULT_CONFIRM)
		status = _nfs4_proc_open_confirm(data);

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
static int nfs4_opendata_access(const struct cred *cred,
				struct nfs4_opendata *opendata,
				struct nfs4_state *state, fmode_t fmode,
				int openflags)
{
	struct nfs_access_entry cache;
	u32 mask, flags;

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
		if (S_ISDIR(state->inode->i_mode))
			mask = NFS4_ACCESS_LOOKUP;
		else
			mask = NFS4_ACCESS_EXECUTE;
	} else if ((fmode & FMODE_READ) && !opendata->file_created)
		mask = NFS4_ACCESS_READ;

	cache.cred = cred;
	nfs_access_set_mask(&cache, opendata->o_res.access_result);
	nfs_access_add_cache(state->inode, &cache);

	flags = NFS4_ACCESS_READ | NFS4_ACCESS_EXECUTE | NFS4_ACCESS_LOOKUP;
	if ((mask & ~cache.mask & flags) == 0)
		return 0;

	return -EACCES;
}

/*
 * Note: On error, nfs4_proc_open will free the struct nfs4_opendata
 */
static int _nfs4_proc_open(struct nfs4_opendata *data,
			   struct nfs_open_context *ctx)
{
	struct inode *dir = d_inode(data->dir);
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs_openargs *o_arg = &data->o_arg;
	struct nfs_openres *o_res = &data->o_res;
	int status;

	status = nfs4_run_open_task(data, ctx);
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
		if (o_arg->open_flags & O_EXCL)
			data->file_created = true;
		else if (o_res->cinfo.before != o_res->cinfo.after)
			data->file_created = true;
		if (data->file_created ||
		    inode_peek_iversion_raw(dir) != o_res->cinfo.after)
			nfs4_update_changeattr(dir, &o_res->cinfo,
					o_res->f_attr->time_start,
					NFS_INO_INVALID_DATA);
	}
	if ((o_res->rflags & NFS4_OPEN_RESULT_LOCKTYPE_POSIX) == 0)
		server->caps &= ~NFS_CAP_POSIX_LOCK;
	if(o_res->rflags & NFS4_OPEN_RESULT_CONFIRM) {
		status = _nfs4_proc_open_confirm(data);
		if (status != 0)
			return status;
	}
	if (!(o_res->f_attr->valid & NFS_ATTR_FATTR)) {
		nfs4_sequence_free_slot(&o_res->seq_res);
		nfs4_proc_getattr(server, &o_res->fh, o_res->f_attr,
				o_res->f_label, NULL);
	}
	return 0;
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

static void nfs_finish_clear_delegation_stateid(struct nfs4_state *state,
		const nfs4_stateid *stateid)
{
	nfs_remove_bad_delegation(state->inode, stateid);
	nfs_state_clear_delegation(state);
}

static void nfs40_clear_delegation_stateid(struct nfs4_state *state)
{
	if (rcu_access_pointer(NFS_I(state->inode)->delegation) != NULL)
		nfs_finish_clear_delegation_stateid(state, NULL);
}

static int nfs40_open_expired(struct nfs4_state_owner *sp, struct nfs4_state *state)
{
	/* NFSv4.0 doesn't allow for delegation recovery on open expire */
	nfs40_clear_delegation_stateid(state);
	nfs_state_clear_open_state_flags(state);
	return nfs4_open_expired(sp, state);
}

static int nfs40_test_and_free_expired_stateid(struct nfs_server *server,
		nfs4_stateid *stateid,
		const struct cred *cred)
{
	return -NFS4ERR_BAD_STATEID;
}

#if defined(CONFIG_NFS_V4_1)
static int nfs41_test_and_free_expired_stateid(struct nfs_server *server,
		nfs4_stateid *stateid,
		const struct cred *cred)
{
	int status;

	switch (stateid->type) {
	default:
		break;
	case NFS4_INVALID_STATEID_TYPE:
	case NFS4_SPECIAL_STATEID_TYPE:
		return -NFS4ERR_BAD_STATEID;
	case NFS4_REVOKED_STATEID_TYPE:
		goto out_free;
	}

	status = nfs41_test_stateid(server, stateid, cred);
	switch (status) {
	case -NFS4ERR_EXPIRED:
	case -NFS4ERR_ADMIN_REVOKED:
	case -NFS4ERR_DELEG_REVOKED:
		break;
	default:
		return status;
	}
out_free:
	/* Ack the revoked state to the server */
	nfs41_free_stateid(server, stateid, cred, true);
	return -NFS4ERR_EXPIRED;
}

static int nfs41_check_delegation_stateid(struct nfs4_state *state)
{
	struct nfs_server *server = NFS_SERVER(state->inode);
	nfs4_stateid stateid;
	struct nfs_delegation *delegation;
	const struct cred *cred = NULL;
	int status, ret = NFS_OK;

	/* Get the delegation credential for use by test/free_stateid */
	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(state->inode)->delegation);
	if (delegation == NULL) {
		rcu_read_unlock();
		nfs_state_clear_delegation(state);
		return NFS_OK;
	}

	spin_lock(&delegation->lock);
	nfs4_stateid_copy(&stateid, &delegation->stateid);

	if (!test_and_clear_bit(NFS_DELEGATION_TEST_EXPIRED,
				&delegation->flags)) {
		spin_unlock(&delegation->lock);
		rcu_read_unlock();
		return NFS_OK;
	}

	if (delegation->cred)
		cred = get_cred(delegation->cred);
	spin_unlock(&delegation->lock);
	rcu_read_unlock();
	status = nfs41_test_and_free_expired_stateid(server, &stateid, cred);
	trace_nfs4_test_delegation_stateid(state, NULL, status);
	if (status == -NFS4ERR_EXPIRED || status == -NFS4ERR_BAD_STATEID)
		nfs_finish_clear_delegation_stateid(state, &stateid);
	else
		ret = status;

	put_cred(cred);
	return ret;
}

static void nfs41_delegation_recover_stateid(struct nfs4_state *state)
{
	nfs4_stateid tmp;

	if (test_bit(NFS_DELEGATED_STATE, &state->flags) &&
	    nfs4_copy_delegation_stateid(state->inode, state->state,
				&tmp, NULL) &&
	    nfs4_stateid_match_other(&state->stateid, &tmp))
		nfs_state_set_delegation(state, &tmp, state->state);
	else
		nfs_state_clear_delegation(state);
}

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
	int status, ret = NFS_OK;
	struct nfs4_lock_state *lsp, *prev = NULL;
	struct nfs_server *server = NFS_SERVER(state->inode);

	if (!test_bit(LK_STATE_IN_USE, &state->flags))
		goto out;

	spin_lock(&state->state_lock);
	list_for_each_entry(lsp, &state->lock_states, ls_locks) {
		if (test_bit(NFS_LOCK_INITIALIZED, &lsp->ls_flags)) {
			const struct cred *cred = lsp->ls_state->owner->so_cred;

			refcount_inc(&lsp->ls_count);
			spin_unlock(&state->state_lock);

			nfs4_put_lock_state(prev);
			prev = lsp;

			status = nfs41_test_and_free_expired_stateid(server,
					&lsp->ls_stateid,
					cred);
			trace_nfs4_test_lock_stateid(state, lsp, status);
			if (status == -NFS4ERR_EXPIRED ||
			    status == -NFS4ERR_BAD_STATEID) {
				clear_bit(NFS_LOCK_INITIALIZED, &lsp->ls_flags);
				lsp->ls_stateid.type = NFS4_INVALID_STATEID_TYPE;
				if (!recover_lost_locks)
					set_bit(NFS_LOCK_LOST, &lsp->ls_flags);
			} else if (status != NFS_OK) {
				ret = status;
				nfs4_put_lock_state(prev);
				goto out;
			}
			spin_lock(&state->state_lock);
		}
	}
	spin_unlock(&state->state_lock);
	nfs4_put_lock_state(prev);
out:
	return ret;
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
	const struct cred *cred = state->owner->so_cred;
	int status;

	if (test_bit(NFS_OPEN_STATE, &state->flags) == 0)
		return -NFS4ERR_BAD_STATEID;
	status = nfs41_test_and_free_expired_stateid(server, stateid, cred);
	trace_nfs4_test_open_stateid(state, NULL, status);
	if (status == -NFS4ERR_EXPIRED || status == -NFS4ERR_BAD_STATEID) {
		nfs_state_clear_open_state_flags(state);
		stateid->type = NFS4_INVALID_STATEID_TYPE;
		return status;
	}
	if (nfs_open_stateid_recover_openmode(state))
		return -NFS4ERR_OPENMODE;
	return NFS_OK;
}

static int nfs41_open_expired(struct nfs4_state_owner *sp, struct nfs4_state *state)
{
	int status;

	status = nfs41_check_delegation_stateid(state);
	if (status != NFS_OK)
		return status;
	nfs41_delegation_recover_stateid(state);

	status = nfs41_check_expired_locks(state);
	if (status != NFS_OK)
		return status;
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
static unsigned nfs4_exclusive_attrset(struct nfs4_opendata *opendata,
				struct iattr *sattr, struct nfs4_label **label)
{
	const __u32 *bitmask = opendata->o_arg.server->exclcreat_bitmask;
	__u32 attrset[3];
	unsigned ret;
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(attrset); i++) {
		attrset[i] = opendata->o_res.attrset[i];
		if (opendata->o_arg.createmode == NFS4_CREATE_EXCLUSIVE4_1)
			attrset[i] &= ~bitmask[i];
	}

	ret = (opendata->o_arg.createmode == NFS4_CREATE_EXCLUSIVE) ?
		sattr->ia_valid : 0;

	if ((attrset[1] & (FATTR4_WORD1_TIME_ACCESS|FATTR4_WORD1_TIME_ACCESS_SET))) {
		if (sattr->ia_valid & ATTR_ATIME_SET)
			ret |= ATTR_ATIME_SET;
		else
			ret |= ATTR_ATIME;
	}

	if ((attrset[1] & (FATTR4_WORD1_TIME_MODIFY|FATTR4_WORD1_TIME_MODIFY_SET))) {
		if (sattr->ia_valid & ATTR_MTIME_SET)
			ret |= ATTR_MTIME_SET;
		else
			ret |= ATTR_MTIME;
	}

	if (!(attrset[2] & FATTR4_WORD2_SECURITY_LABEL))
		*label = NULL;
	return ret;
}

static int _nfs4_open_and_get_state(struct nfs4_opendata *opendata,
		int flags, struct nfs_open_context *ctx)
{
	struct nfs4_state_owner *sp = opendata->owner;
	struct nfs_server *server = sp->so_server;
	struct dentry *dentry;
	struct nfs4_state *state;
	fmode_t acc_mode = _nfs4_ctx_to_accessmode(ctx);
	struct inode *dir = d_inode(opendata->dir);
	unsigned long dir_verifier;
	unsigned int seq;
	int ret;

	seq = raw_seqcount_begin(&sp->so_reclaim_seqcount);
	dir_verifier = nfs_save_change_attribute(dir);

	ret = _nfs4_proc_open(opendata, ctx);
	if (ret != 0)
		goto out;

	state = _nfs4_opendata_to_nfs4_state(opendata);
	ret = PTR_ERR(state);
	if (IS_ERR(state))
		goto out;
	ctx->state = state;
	if (server->caps & NFS_CAP_POSIX_LOCK)
		set_bit(NFS_STATE_POSIX_LOCKS, &state->flags);
	if (opendata->o_res.rflags & NFS4_OPEN_RESULT_MAY_NOTIFY_LOCK)
		set_bit(NFS_STATE_MAY_NOTIFY_LOCK, &state->flags);

	dentry = opendata->dentry;
	if (d_really_is_negative(dentry)) {
		struct dentry *alias;
		d_drop(dentry);
		alias = d_exact_alias(dentry, state->inode);
		if (!alias)
			alias = d_splice_alias(igrab(state->inode), dentry);
		/* d_splice_alias() can't fail here - it's a non-directory */
		if (alias) {
			dput(ctx->dentry);
			ctx->dentry = dentry = alias;
		}
	}

	switch(opendata->o_arg.claim) {
	default:
		break;
	case NFS4_OPEN_CLAIM_NULL:
	case NFS4_OPEN_CLAIM_DELEGATE_CUR:
	case NFS4_OPEN_CLAIM_DELEGATE_PREV:
		if (!opendata->rpc_done)
			break;
		if (opendata->o_res.delegation_type != 0)
			dir_verifier = nfs_save_change_attribute(dir);
		nfs_set_verifier(dentry, dir_verifier);
	}

	/* Parse layoutget results before we check for access */
	pnfs_parse_lgopen(state->inode, opendata->lgp, ctx);

	ret = nfs4_opendata_access(sp->so_cred, opendata, state,
			acc_mode, flags);
	if (ret != 0)
		goto out;

	if (d_inode(dentry) == state->inode) {
		nfs_inode_attach_open_context(ctx);
		if (read_seqcount_retry(&sp->so_reclaim_seqcount, seq))
			nfs4_schedule_stateid_recovery(server, state);
	}

out:
	if (opendata->lgp) {
		nfs4_lgopen_release(opendata->lgp);
		opendata->lgp = NULL;
	}
	if (!opendata->cancelled)
		nfs4_sequence_free_slot(&opendata->o_res.seq_res);
	return ret;
}

/*
 * Returns a referenced nfs4_state
 */
static int _nfs4_do_open(struct inode *dir,
			struct nfs_open_context *ctx,
			int flags,
			const struct nfs4_open_createattrs *c,
			int *opened)
{
	struct nfs4_state_owner  *sp;
	struct nfs4_state     *state = NULL;
	struct nfs_server       *server = NFS_SERVER(dir);
	struct nfs4_opendata *opendata;
	struct dentry *dentry = ctx->dentry;
	const struct cred *cred = ctx->cred;
	struct nfs4_threshold **ctx_th = &ctx->mdsthreshold;
	fmode_t fmode = _nfs4_ctx_to_openmode(ctx);
	enum open_claim_type4 claim = NFS4_OPEN_CLAIM_NULL;
	struct iattr *sattr = c->sattr;
	struct nfs4_label *label = c->label;
	struct nfs4_label *olabel = NULL;
	int status;

	/* Protect against reboot recovery conflicts */
	status = -ENOMEM;
	sp = nfs4_get_state_owner(server, cred, GFP_KERNEL);
	if (sp == NULL) {
		dprintk("nfs4_do_open: nfs4_get_state_owner failed!\n");
		goto out_err;
	}
	status = nfs4_client_recover_expired_lease(server->nfs_client);
	if (status != 0)
		goto err_put_state_owner;
	if (d_really_is_positive(dentry))
		nfs4_return_incompatible_delegation(d_inode(dentry), fmode);
	status = -ENOMEM;
	if (d_really_is_positive(dentry))
		claim = NFS4_OPEN_CLAIM_FH;
	opendata = nfs4_opendata_alloc(dentry, sp, fmode, flags,
			c, claim, GFP_KERNEL);
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
	if (d_really_is_positive(dentry))
		opendata->state = nfs4_get_open_state(d_inode(dentry), sp);

	status = _nfs4_open_and_get_state(opendata, flags, ctx);
	if (status != 0)
		goto err_free_label;
	state = ctx->state;

	if ((opendata->o_arg.open_flags & (O_CREAT|O_EXCL)) == (O_CREAT|O_EXCL) &&
	    (opendata->o_arg.createmode != NFS4_CREATE_GUARDED)) {
		unsigned attrs = nfs4_exclusive_attrset(opendata, sattr, &label);
		/*
		 * send create attributes which was not set by open
		 * with an extra setattr.
		 */
		if (attrs || label) {
			unsigned ia_old = sattr->ia_valid;

			sattr->ia_valid = attrs;
			nfs_fattr_init(opendata->o_res.f_attr);
			status = nfs4_do_setattr(state->inode, cred,
					opendata->o_res.f_attr, sattr,
					ctx, label, olabel);
			if (status == 0) {
				nfs_setattr_update_inode(state->inode, sattr,
						opendata->o_res.f_attr);
				nfs_setsecurity(state->inode, opendata->o_res.f_attr, olabel);
			}
			sattr->ia_valid = ia_old;
		}
	}
	if (opened && opendata->file_created)
		*opened = 1;

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
	struct nfs4_exception exception = {
		.interruptible = true,
	};
	struct nfs4_state *res;
	struct nfs4_open_createattrs c = {
		.label = label,
		.sattr = sattr,
		.verf = {
			[0] = (__u32)jiffies,
			[1] = (__u32)current->pid,
		},
	};
	int status;

	do {
		status = _nfs4_do_open(dir, ctx, flags, &c, opened);
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
		if (status == -NFS4ERR_EXPIRED) {
			nfs4_schedule_lease_recovery(server->nfs_client);
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

static int _nfs4_do_setattr(struct inode *inode,
			    struct nfs_setattrargs *arg,
			    struct nfs_setattrres *res,
			    const struct cred *cred,
			    struct nfs_open_context *ctx)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_SETATTR],
		.rpc_argp	= arg,
		.rpc_resp	= res,
		.rpc_cred	= cred,
	};
	const struct cred *delegation_cred = NULL;
	unsigned long timestamp = jiffies;
	bool truncate;
	int status;

	nfs_fattr_init(res->fattr);

	/* Servers should only apply open mode checks for file size changes */
	truncate = (arg->iap->ia_valid & ATTR_SIZE) ? true : false;
	if (!truncate) {
		nfs4_inode_make_writeable(inode);
		goto zero_stateid;
	}

	if (nfs4_copy_delegation_stateid(inode, FMODE_WRITE, &arg->stateid, &delegation_cred)) {
		/* Use that stateid */
	} else if (ctx != NULL && ctx->state) {
		struct nfs_lock_context *l_ctx;
		if (!nfs4_valid_open_stateid(ctx->state))
			return -EBADF;
		l_ctx = nfs_get_lock_context(ctx);
		if (IS_ERR(l_ctx))
			return PTR_ERR(l_ctx);
		status = nfs4_select_rw_stateid(ctx->state, FMODE_WRITE, l_ctx,
						&arg->stateid, &delegation_cred);
		nfs_put_lock_context(l_ctx);
		if (status == -EIO)
			return -EBADF;
		else if (status == -EAGAIN)
			goto zero_stateid;
	} else {
zero_stateid:
		nfs4_stateid_copy(&arg->stateid, &zero_stateid);
	}
	if (delegation_cred)
		msg.rpc_cred = delegation_cred;

	status = nfs4_call_sync(server->client, server, &msg, &arg->seq_args, &res->seq_res, 1);

	put_cred(delegation_cred);
	if (status == 0 && ctx != NULL)
		renew_lease(server, timestamp);
	trace_nfs4_setattr(inode, &arg->stateid, status);
	return status;
}

static int nfs4_do_setattr(struct inode *inode, const struct cred *cred,
			   struct nfs_fattr *fattr, struct iattr *sattr,
			   struct nfs_open_context *ctx, struct nfs4_label *ilabel,
			   struct nfs4_label *olabel)
{
	struct nfs_server *server = NFS_SERVER(inode);
	__u32 bitmask[NFS4_BITMASK_SZ];
	struct nfs4_state *state = ctx ? ctx->state : NULL;
	struct nfs_setattrargs	arg = {
		.fh		= NFS_FH(inode),
		.iap		= sattr,
		.server		= server,
		.bitmask = bitmask,
		.label		= ilabel,
	};
	struct nfs_setattrres  res = {
		.fattr		= fattr,
		.label		= olabel,
		.server		= server,
	};
	struct nfs4_exception exception = {
		.state = state,
		.inode = inode,
		.stateid = &arg.stateid,
	};
	int err;

	do {
		nfs4_bitmap_copy_adjust_setattr(bitmask,
				nfs4_bitmask(server, olabel),
				inode);

		err = _nfs4_do_setattr(inode, &arg, &res, cred, ctx);
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

static bool
nfs4_wait_on_layoutreturn(struct inode *inode, struct rpc_task *task)
{
	if (inode == NULL || !nfs_have_layout(inode))
		return false;

	return pnfs_wait_on_layoutreturn(inode, task);
}

/*
 * Update the seqid of an open stateid
 */
static void nfs4_sync_open_stateid(nfs4_stateid *dst,
		struct nfs4_state *state)
{
	__be32 seqid_open;
	u32 dst_seqid;
	int seq;

	for (;;) {
		if (!nfs4_valid_open_stateid(state))
			break;
		seq = read_seqbegin(&state->seqlock);
		if (!nfs4_state_match_open_stateid_other(state, dst)) {
			nfs4_stateid_copy(dst, &state->open_stateid);
			if (read_seqretry(&state->seqlock, seq))
				continue;
			break;
		}
		seqid_open = state->open_stateid.seqid;
		if (read_seqretry(&state->seqlock, seq))
			continue;

		dst_seqid = be32_to_cpu(dst->seqid);
		if ((s32)(dst_seqid - be32_to_cpu(seqid_open)) < 0)
			dst->seqid = seqid_open;
		break;
	}
}

/*
 * Update the seqid of an open stateid after receiving
 * NFS4ERR_OLD_STATEID
 */
static bool nfs4_refresh_open_old_stateid(nfs4_stateid *dst,
		struct nfs4_state *state)
{
	__be32 seqid_open;
	u32 dst_seqid;
	bool ret;
	int seq, status = -EAGAIN;
	DEFINE_WAIT(wait);

	for (;;) {
		ret = false;
		if (!nfs4_valid_open_stateid(state))
			break;
		seq = read_seqbegin(&state->seqlock);
		if (!nfs4_state_match_open_stateid_other(state, dst)) {
			if (read_seqretry(&state->seqlock, seq))
				continue;
			break;
		}

		write_seqlock(&state->seqlock);
		seqid_open = state->open_stateid.seqid;

		dst_seqid = be32_to_cpu(dst->seqid);

		/* Did another OPEN bump the state's seqid?  try again: */
		if ((s32)(be32_to_cpu(seqid_open) - dst_seqid) > 0) {
			dst->seqid = seqid_open;
			write_sequnlock(&state->seqlock);
			ret = true;
			break;
		}

		/* server says we're behind but we haven't seen the update yet */
		set_bit(NFS_STATE_CHANGE_WAIT, &state->flags);
		prepare_to_wait(&state->waitq, &wait, TASK_KILLABLE);
		write_sequnlock(&state->seqlock);
		trace_nfs4_close_stateid_update_wait(state->inode, dst, 0);

		if (fatal_signal_pending(current))
			status = -EINTR;
		else
			if (schedule_timeout(5*HZ) != 0)
				status = 0;

		finish_wait(&state->waitq, &wait);

		if (!status)
			continue;
		if (status == -EINTR)
			break;

		/* we slept the whole 5 seconds, we must have lost a seqid */
		dst->seqid = cpu_to_be32(dst_seqid + 1);
		ret = true;
		break;
	}

	return ret;
}

struct nfs4_closedata {
	struct inode *inode;
	struct nfs4_state *state;
	struct nfs_closeargs arg;
	struct nfs_closeres res;
	struct {
		struct nfs4_layoutreturn_args arg;
		struct nfs4_layoutreturn_res res;
		struct nfs4_xdr_opaque_data ld_private;
		u32 roc_barrier;
		bool roc;
	} lr;
	struct nfs_fattr fattr;
	unsigned long timestamp;
};

static void nfs4_free_closedata(void *data)
{
	struct nfs4_closedata *calldata = data;
	struct nfs4_state_owner *sp = calldata->state->owner;
	struct super_block *sb = calldata->state->inode->i_sb;

	if (calldata->lr.roc)
		pnfs_roc_release(&calldata->lr.arg, &calldata->lr.res,
				calldata->res.lr_ret);
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
	struct nfs4_exception exception = {
		.state = state,
		.inode = calldata->inode,
		.stateid = &calldata->arg.stateid,
	};

	dprintk("%s: begin!\n", __func__);
	if (!nfs4_sequence_done(task, &calldata->res.seq_res))
		return;
	trace_nfs4_close(state, &calldata->arg, &calldata->res, task->tk_status);

	/* Handle Layoutreturn errors */
	if (pnfs_roc_done(task, &calldata->arg.lr_args, &calldata->res.lr_res,
			  &calldata->res.lr_ret) == -EAGAIN)
		goto out_restart;

	/* hmm. we are done with the inode, and in the process of freeing
	 * the state_owner. we keep this around to process errors
	 */
	switch (task->tk_status) {
		case 0:
			res_stateid = &calldata->res.stateid;
			renew_lease(server, calldata->timestamp);
			break;
		case -NFS4ERR_ACCESS:
			if (calldata->arg.bitmask != NULL) {
				calldata->arg.bitmask = NULL;
				calldata->res.fattr = NULL;
				goto out_restart;

			}
			break;
		case -NFS4ERR_OLD_STATEID:
			/* Did we race with OPEN? */
			if (nfs4_refresh_open_old_stateid(&calldata->arg.stateid,
						state))
				goto out_restart;
			goto out_release;
		case -NFS4ERR_ADMIN_REVOKED:
		case -NFS4ERR_STALE_STATEID:
		case -NFS4ERR_EXPIRED:
			nfs4_free_revoked_stateid(server,
					&calldata->arg.stateid,
					task->tk_msg.rpc_cred);
			fallthrough;
		case -NFS4ERR_BAD_STATEID:
			if (calldata->arg.fmode == 0)
				break;
			fallthrough;
		default:
			task->tk_status = nfs4_async_handle_exception(task,
					server, task->tk_status, &exception);
			if (exception.retry)
				goto out_restart;
	}
	nfs_clear_open_stateid(state, &calldata->arg.stateid,
			res_stateid, calldata->arg.fmode);
out_release:
	task->tk_status = 0;
	nfs_release_seqid(calldata->arg.seqid);
	nfs_refresh_inode(calldata->inode, &calldata->fattr);
	dprintk("%s: done, ret = %d!\n", __func__, task->tk_status);
	return;
out_restart:
	task->tk_status = 0;
	rpc_restart_call_prepare(task);
	goto out_release;
}

static void nfs4_close_prepare(struct rpc_task *task, void *data)
{
	struct nfs4_closedata *calldata = data;
	struct nfs4_state *state = calldata->state;
	struct inode *inode = calldata->inode;
	struct nfs_server *server = NFS_SERVER(inode);
	struct pnfs_layout_hdr *lo;
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
		if (calldata->arg.fmode != (FMODE_READ|FMODE_WRITE))
			call_close |= is_rdwr;
	} else if (is_rdwr)
		calldata->arg.fmode |= FMODE_READ|FMODE_WRITE;

	nfs4_sync_open_stateid(&calldata->arg.stateid, state);
	if (!nfs4_valid_open_stateid(state))
		call_close = 0;
	spin_unlock(&state->owner->so_lock);

	if (!call_close) {
		/* Note: exit _without_ calling nfs4_close_done */
		goto out_no_action;
	}

	if (!calldata->lr.roc && nfs4_wait_on_layoutreturn(inode, task)) {
		nfs_release_seqid(calldata->arg.seqid);
		goto out_wait;
	}

	lo = calldata->arg.lr_args ? calldata->arg.lr_args->layout : NULL;
	if (lo && !pnfs_layout_is_valid(lo)) {
		calldata->arg.lr_args = NULL;
		calldata->res.lr_res = NULL;
	}

	if (calldata->arg.fmode == 0)
		task->tk_msg.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_CLOSE];

	if (calldata->arg.fmode == 0 || calldata->arg.fmode == FMODE_READ) {
		/* Close-to-open cache consistency revalidation */
		if (!nfs4_have_delegation(inode, FMODE_READ)) {
			nfs4_bitmask_set(calldata->arg.bitmask_store,
					 server->cache_consistency_bitmask,
					 inode, server, NULL);
			calldata->arg.bitmask = calldata->arg.bitmask_store;
		} else
			calldata->arg.bitmask = NULL;
	}

	calldata->arg.share_access =
		nfs4_map_atomic_open_share(NFS_SERVER(inode),
				calldata->arg.fmode, 0);

	if (calldata->res.fattr == NULL)
		calldata->arg.bitmask = NULL;
	else if (calldata->arg.bitmask == NULL)
		calldata->res.fattr = NULL;
	calldata->timestamp = jiffies;
	if (nfs4_setup_sequence(NFS_SERVER(inode)->nfs_client,
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
	struct nfs_seqid *(*alloc_seqid)(struct nfs_seqid_counter *, gfp_t);
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
		.flags = RPC_TASK_ASYNC | RPC_TASK_CRED_NOREF,
	};
	int status = -ENOMEM;

	nfs4_state_protect(server->nfs_client, NFS_SP4_MACH_CRED_CLEANUP,
		&task_setup_data.rpc_client, &msg);

	calldata = kzalloc(sizeof(*calldata), gfp_mask);
	if (calldata == NULL)
		goto out;
	nfs4_init_sequence(&calldata->arg.seq_args, &calldata->res.seq_res, 1, 0);
	calldata->inode = state->inode;
	calldata->state = state;
	calldata->arg.fh = NFS_FH(state->inode);
	if (!nfs4_copy_open_stateid(&calldata->arg.stateid, state))
		goto out_free_calldata;
	/* Serialization for the sequence id */
	alloc_seqid = server->nfs_client->cl_mvops->alloc_seqid;
	calldata->arg.seqid = alloc_seqid(&state->owner->so_seqid, gfp_mask);
	if (IS_ERR(calldata->arg.seqid))
		goto out_free_calldata;
	nfs_fattr_init(&calldata->fattr);
	calldata->arg.fmode = 0;
	calldata->lr.arg.ld_private = &calldata->lr.ld_private;
	calldata->res.fattr = &calldata->fattr;
	calldata->res.seqid = calldata->arg.seqid;
	calldata->res.server = server;
	calldata->res.lr_ret = -NFS4ERR_NOMATCHING_LAYOUT;
	calldata->lr.roc = pnfs_roc(state->inode,
			&calldata->lr.arg, &calldata->lr.res, msg.rpc_cred);
	if (calldata->lr.roc) {
		calldata->arg.lr_args = &calldata->lr.arg;
		calldata->res.lr_res = &calldata->lr.res;
	}
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
		nfs4_close_sync(ctx->state, _nfs4_ctx_to_openmode(ctx));
	else
		nfs4_close_state(ctx->state, _nfs4_ctx_to_openmode(ctx));
}

#define FATTR4_WORD1_NFS40_MASK (2*FATTR4_WORD1_MOUNTED_ON_FILEID - 1UL)
#define FATTR4_WORD2_NFS41_MASK (2*FATTR4_WORD2_SUPPATTR_EXCLCREAT - 1UL)
#define FATTR4_WORD2_NFS42_MASK (2*FATTR4_WORD2_XATTR_SUPPORT - 1UL)

static int _nfs4_server_capabilities(struct nfs_server *server, struct nfs_fh *fhandle)
{
	u32 bitmask[3] = {}, minorversion = server->nfs_client->cl_minorversion;
	struct nfs4_server_caps_arg args = {
		.fhandle = fhandle,
		.bitmask = bitmask,
	};
	struct nfs4_server_caps_res res = {};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SERVER_CAPS],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	int status;
	int i;

	bitmask[0] = FATTR4_WORD0_SUPPORTED_ATTRS |
		     FATTR4_WORD0_FH_EXPIRE_TYPE |
		     FATTR4_WORD0_LINK_SUPPORT |
		     FATTR4_WORD0_SYMLINK_SUPPORT |
		     FATTR4_WORD0_ACLSUPPORT;
	if (minorversion)
		bitmask[2] = FATTR4_WORD2_SUPPATTR_EXCLCREAT;

	status = nfs4_call_sync(server->client, server, &msg, &args.seq_args, &res.seq_res, 0);
	if (status == 0) {
		/* Sanity check the server answers */
		switch (minorversion) {
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

		/* Avoid a regression due to buggy server */
		for (i = 0; i < ARRAY_SIZE(res.exclcreat_bitmask); i++)
			res.exclcreat_bitmask[i] &= res.attr_bitmask[i];
		memcpy(server->exclcreat_bitmask, res.exclcreat_bitmask,
			sizeof(server->exclcreat_bitmask));

		server->acl_bitmask = res.acl_bitmask;
		server->fh_expire_type = res.fh_expire_type;
	}

	return status;
}

int nfs4_server_capabilities(struct nfs_server *server, struct nfs_fh *fhandle)
{
	struct nfs4_exception exception = {
		.interruptible = true,
	};
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
	struct nfs4_exception exception = {
		.interruptible = true,
	};
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

	auth = rpcauth_create(&auth_args, server->client);
	if (IS_ERR(auth))
		return -EACCES;
	return nfs4_lookup_root(server, fhandle, info);
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
	 * -EACCES could mean that the user doesn't have correct permissions
	 * to access the mount.  It could also mean that we tried to mount
	 * with a gss auth flavor, but rpc.gssd isn't running.  Either way,
	 * existing mount programs don't handle -EACCES very well so it should
	 * be mapped to -EPERM instead.
	 */
	if (status == -EACCES)
		status = -EPERM;
	return status;
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
	int status = 0;

	if (!auth_probe)
		status = nfs4_lookup_root(server, fhandle, info);

	if (auth_probe || status == NFS4ERR_WRONGSEC)
		status = server->nfs_client->cl_mvops->find_root_sec(server,
				fhandle, info);

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
	struct nfs4_label *label = fattr->label;

	error = nfs4_server_capabilities(server, mntfh);
	if (error < 0) {
		dprintk("nfs4_get_root: getcaps error = %d\n", -error);
		return error;
	}

	error = nfs4_proc_getattr(server, mntfh, fattr, label, NULL);
	if (error < 0) {
		dprintk("nfs4_get_root: getattr error = %d\n", -error);
		goto out;
	}

	if (fattr->valid & NFS_ATTR_FATTR_FSID &&
	    !nfs_fsid_equal(&server->fsid, &fattr->fsid))
		memcpy(&server->fsid, &fattr->fsid, sizeof(server->fsid));

out:
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
				struct nfs_fattr *fattr, struct nfs4_label *label,
				struct inode *inode)
{
	__u32 bitmask[NFS4_BITMASK_SZ];
	struct nfs4_getattr_arg args = {
		.fh = fhandle,
		.bitmask = bitmask,
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
	unsigned short task_flags = 0;

	/* Is this is an attribute revalidation, subject to softreval? */
	if (inode && (server->flags & NFS_MOUNT_SOFTREVAL))
		task_flags |= RPC_TASK_TIMEOUT;

	nfs4_bitmap_copy_adjust(bitmask, nfs4_bitmask(server, label), inode);

	nfs_fattr_init(fattr);
	nfs4_init_sequence(&args.seq_args, &res.seq_res, 0, 0);
	return nfs4_do_call_sync(server->client, server, &msg,
			&args.seq_args, &res.seq_res, task_flags);
}

int nfs4_proc_getattr(struct nfs_server *server, struct nfs_fh *fhandle,
				struct nfs_fattr *fattr, struct nfs4_label *label,
				struct inode *inode)
{
	struct nfs4_exception exception = {
		.interruptible = true,
	};
	int err;
	do {
		err = _nfs4_proc_getattr(server, fhandle, fattr, label, inode);
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
	struct inode *inode = d_inode(dentry);
	const struct cred *cred = NULL;
	struct nfs_open_context *ctx = NULL;
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

		ctx = nfs_file_open_context(sattr->ia_file);
		if (ctx)
			cred = ctx->cred;
	}

	label = nfs4_label_alloc(NFS_SERVER(inode), GFP_KERNEL);
	if (IS_ERR(label))
		return PTR_ERR(label);

	/* Return any delegations if we're going to change ACLs */
	if ((sattr->ia_valid & (ATTR_MODE|ATTR_UID|ATTR_GID)) != 0)
		nfs4_inode_make_writeable(inode);

	status = nfs4_do_setattr(inode, cred, fattr, sattr, ctx, NULL, label);
	if (status == 0) {
		nfs_setattr_update_inode(inode, sattr, fattr);
		nfs_setsecurity(inode, fattr, label);
	}
	nfs4_label_free(label);
	return status;
}

static int _nfs4_proc_lookup(struct rpc_clnt *clnt, struct inode *dir,
		struct dentry *dentry, struct nfs_fh *fhandle,
		struct nfs_fattr *fattr, struct nfs4_label *label)
{
	struct nfs_server *server = NFS_SERVER(dir);
	int		       status;
	struct nfs4_lookup_arg args = {
		.bitmask = server->attr_bitmask,
		.dir_fh = NFS_FH(dir),
		.name = &dentry->d_name,
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
	unsigned short task_flags = 0;

	/* Is this is an attribute revalidation, subject to softreval? */
	if (nfs_lookup_is_soft_revalidate(dentry))
		task_flags |= RPC_TASK_TIMEOUT;

	args.bitmask = nfs4_bitmask(server, label);

	nfs_fattr_init(fattr);

	dprintk("NFS call  lookup %pd2\n", dentry);
	nfs4_init_sequence(&args.seq_args, &res.seq_res, 0, 0);
	status = nfs4_do_call_sync(clnt, server, &msg,
			&args.seq_args, &res.seq_res, task_flags);
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
				   struct dentry *dentry, struct nfs_fh *fhandle,
				   struct nfs_fattr *fattr, struct nfs4_label *label)
{
	struct nfs4_exception exception = {
		.interruptible = true,
	};
	struct rpc_clnt *client = *clnt;
	const struct qstr *name = &dentry->d_name;
	int err;
	do {
		err = _nfs4_proc_lookup(client, dir, dentry, fhandle, fattr, label);
		trace_nfs4_lookup(dir, name, err);
		switch (err) {
		case -NFS4ERR_BADNAME:
			err = -ENOENT;
			goto out;
		case -NFS4ERR_MOVED:
			err = nfs4_get_referral(client, dir, name, fattr, fhandle);
			if (err == -NFS4ERR_MOVED)
				err = nfs4_handle_exception(NFS_SERVER(dir), err, &exception);
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

static int nfs4_proc_lookup(struct inode *dir, struct dentry *dentry,
			    struct nfs_fh *fhandle, struct nfs_fattr *fattr,
			    struct nfs4_label *label)
{
	int status;
	struct rpc_clnt *client = NFS_CLIENT(dir);

	status = nfs4_proc_lookup_common(&client, dir, dentry, fhandle, fattr, label);
	if (client != NFS_CLIENT(dir)) {
		rpc_shutdown_client(client);
		nfs_fixup_secinfo_attributes(fattr);
	}
	return status;
}

struct rpc_clnt *
nfs4_proc_lookup_mountpoint(struct inode *dir, struct dentry *dentry,
			    struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct rpc_clnt *client = NFS_CLIENT(dir);
	int status;

	status = nfs4_proc_lookup_common(&client, dir, dentry, fhandle, fattr, NULL);
	if (status < 0)
		return ERR_PTR(status);
	return (client == NFS_CLIENT(dir)) ? rpc_clone_client(client) : client;
}

static int _nfs4_proc_lookupp(struct inode *inode,
		struct nfs_fh *fhandle, struct nfs_fattr *fattr,
		struct nfs4_label *label)
{
	struct rpc_clnt *clnt = NFS_CLIENT(inode);
	struct nfs_server *server = NFS_SERVER(inode);
	int		       status;
	struct nfs4_lookupp_arg args = {
		.bitmask = server->attr_bitmask,
		.fh = NFS_FH(inode),
	};
	struct nfs4_lookupp_res res = {
		.server = server,
		.fattr = fattr,
		.label = label,
		.fh = fhandle,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LOOKUPP],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};

	args.bitmask = nfs4_bitmask(server, label);

	nfs_fattr_init(fattr);

	dprintk("NFS call  lookupp ino=0x%lx\n", inode->i_ino);
	status = nfs4_call_sync(clnt, server, &msg, &args.seq_args,
				&res.seq_res, 0);
	dprintk("NFS reply lookupp: %d\n", status);
	return status;
}

static int nfs4_proc_lookupp(struct inode *inode, struct nfs_fh *fhandle,
			     struct nfs_fattr *fattr, struct nfs4_label *label)
{
	struct nfs4_exception exception = {
		.interruptible = true,
	};
	int err;
	do {
		err = _nfs4_proc_lookupp(inode, fhandle, fattr, label);
		trace_nfs4_lookupp(inode, err);
		err = nfs4_handle_exception(NFS_SERVER(inode), err,
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_access(struct inode *inode, struct nfs_access_entry *entry)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs4_accessargs args = {
		.fh = NFS_FH(inode),
		.access = entry->mask,
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
	int status = 0;

	if (!nfs4_have_delegation(inode, FMODE_READ)) {
		res.fattr = nfs_alloc_fattr();
		if (res.fattr == NULL)
			return -ENOMEM;
		args.bitmask = server->cache_consistency_bitmask;
	}
	status = nfs4_call_sync(server->client, server, &msg, &args.seq_args, &res.seq_res, 0);
	if (!status) {
		nfs_access_set_mask(entry, res.access);
		if (res.fattr)
			nfs_refresh_inode(inode, res.fattr);
	}
	nfs_free_fattr(res.fattr);
	return status;
}

static int nfs4_proc_access(struct inode *inode, struct nfs_access_entry *entry)
{
	struct nfs4_exception exception = {
		.interruptible = true,
	};
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
	struct nfs4_exception exception = {
		.interruptible = true,
	};
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
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs4_label l, *ilabel = NULL;
	struct nfs_open_context *ctx;
	struct nfs4_state *state;
	int status = 0;

	ctx = alloc_nfs_open_context(dentry, FMODE_READ, NULL);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ilabel = nfs4_label_init_security(dir, dentry, sattr, &l);

	if (!(server->attr_bitmask[2] & FATTR4_WORD2_MODE_UMASK))
		sattr->ia_mode &= ~current_umask();
	state = nfs4_do_open(dir, ctx, flags, sattr, ilabel, NULL);
	if (IS_ERR(state)) {
		status = PTR_ERR(state);
		goto out;
	}
out:
	nfs4_label_release_security(ilabel);
	put_nfs_open_context(ctx);
	return status;
}

static int
_nfs4_proc_remove(struct inode *dir, const struct qstr *name, u32 ftype)
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
	unsigned long timestamp = jiffies;
	int status;

	status = nfs4_call_sync(server->client, server, &msg, &args.seq_args, &res.seq_res, 1);
	if (status == 0) {
		spin_lock(&dir->i_lock);
		nfs4_update_changeattr_locked(dir, &res.cinfo, timestamp,
					      NFS_INO_INVALID_DATA);
		/* Removing a directory decrements nlink in the parent */
		if (ftype == NF4DIR && dir->i_nlink > 2)
			nfs4_dec_nlink_locked(dir);
		spin_unlock(&dir->i_lock);
	}
	return status;
}

static int nfs4_proc_remove(struct inode *dir, struct dentry *dentry)
{
	struct nfs4_exception exception = {
		.interruptible = true,
	};
	struct inode *inode = d_inode(dentry);
	int err;

	if (inode) {
		if (inode->i_nlink == 1)
			nfs4_inode_return_delegation(inode);
		else
			nfs4_inode_make_writeable(inode);
	}
	do {
		err = _nfs4_proc_remove(dir, &dentry->d_name, NF4REG);
		trace_nfs4_remove(dir, &dentry->d_name, err);
		err = nfs4_handle_exception(NFS_SERVER(dir), err,
				&exception);
	} while (exception.retry);
	return err;
}

static int nfs4_proc_rmdir(struct inode *dir, const struct qstr *name)
{
	struct nfs4_exception exception = {
		.interruptible = true,
	};
	int err;

	do {
		err = _nfs4_proc_remove(dir, name, NF4DIR);
		trace_nfs4_remove(dir, name, err);
		err = nfs4_handle_exception(NFS_SERVER(dir), err,
				&exception);
	} while (exception.retry);
	return err;
}

static void nfs4_proc_unlink_setup(struct rpc_message *msg,
		struct dentry *dentry,
		struct inode *inode)
{
	struct nfs_removeargs *args = msg->rpc_argp;
	struct nfs_removeres *res = msg->rpc_resp;

	res->server = NFS_SB(dentry->d_sb);
	msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_REMOVE];
	nfs4_init_sequence(&args->seq_args, &res->seq_res, 1, 0);

	nfs_fattr_init(res->dir_attr);

	if (inode)
		nfs4_inode_return_delegation(inode);
}

static void nfs4_proc_unlink_rpc_prepare(struct rpc_task *task, struct nfs_unlinkdata *data)
{
	nfs4_setup_sequence(NFS_SB(data->dentry->d_sb)->nfs_client,
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
	if (task->tk_status == 0)
		nfs4_update_changeattr(dir, &res->cinfo,
				res->dir_attr->time_start,
				NFS_INO_INVALID_DATA);
	return 1;
}

static void nfs4_proc_rename_setup(struct rpc_message *msg,
		struct dentry *old_dentry,
		struct dentry *new_dentry)
{
	struct nfs_renameargs *arg = msg->rpc_argp;
	struct nfs_renameres *res = msg->rpc_resp;
	struct inode *old_inode = d_inode(old_dentry);
	struct inode *new_inode = d_inode(new_dentry);

	if (old_inode)
		nfs4_inode_make_writeable(old_inode);
	if (new_inode)
		nfs4_inode_return_delegation(new_inode);
	msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_RENAME];
	res->server = NFS_SB(old_dentry->d_sb);
	nfs4_init_sequence(&arg->seq_args, &res->seq_res, 1, 0);
}

static void nfs4_proc_rename_rpc_prepare(struct rpc_task *task, struct nfs_renamedata *data)
{
	nfs4_setup_sequence(NFS_SERVER(data->old_dir)->nfs_client,
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

	if (task->tk_status == 0) {
		if (new_dir != old_dir) {
			/* Note: If we moved a directory, nlink will change */
			nfs4_update_changeattr(old_dir, &res->old_cinfo,
					res->old_fattr->time_start,
					NFS_INO_INVALID_OTHER |
					    NFS_INO_INVALID_DATA);
			nfs4_update_changeattr(new_dir, &res->new_cinfo,
					res->new_fattr->time_start,
					NFS_INO_INVALID_OTHER |
					    NFS_INO_INVALID_DATA);
		} else
			nfs4_update_changeattr(old_dir, &res->old_cinfo,
					res->old_fattr->time_start,
					NFS_INO_INVALID_DATA);
	}
	return 1;
}

static int _nfs4_proc_link(struct inode *inode, struct inode *dir, const struct qstr *name)
{
	struct nfs_server *server = NFS_SERVER(inode);
	__u32 bitmask[NFS4_BITMASK_SZ];
	struct nfs4_link_arg arg = {
		.fh     = NFS_FH(inode),
		.dir_fh = NFS_FH(dir),
		.name   = name,
		.bitmask = bitmask,
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

	nfs4_inode_make_writeable(inode);
	nfs4_bitmap_copy_adjust_setattr(bitmask, nfs4_bitmask(server, res.label), inode);

	status = nfs4_call_sync(server->client, server, &msg, &arg.seq_args, &res.seq_res, 1);
	if (!status) {
		nfs4_update_changeattr(dir, &res.cinfo, res.fattr->time_start,
				       NFS_INO_INVALID_DATA);
		status = nfs_post_op_update_inode(inode, res.fattr);
		if (!status)
			nfs_setsecurity(inode, res.fattr, res.label);
	}


	nfs4_label_free(res.label);

out:
	nfs_free_fattr(res.fattr);
	return status;
}

static int nfs4_proc_link(struct inode *inode, struct inode *dir, const struct qstr *name)
{
	struct nfs4_exception exception = {
		.interruptible = true,
	};
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
		const struct qstr *name, struct iattr *sattr, u32 ftype)
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
		data->arg.umask = current_umask();
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
		spin_lock(&dir->i_lock);
		nfs4_update_changeattr_locked(dir, &data->res.dir_cinfo,
				data->res.fattr->time_start,
				NFS_INO_INVALID_DATA);
		/* Creating a directory bumps nlink in the parent */
		if (data->arg.ftype == NF4DIR)
			nfs4_inc_nlink_locked(dir);
		spin_unlock(&dir->i_lock);
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
	struct nfs4_exception exception = {
		.interruptible = true,
	};
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
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs4_exception exception = {
		.interruptible = true,
	};
	struct nfs4_label l, *label = NULL;
	int err;

	label = nfs4_label_init_security(dir, dentry, sattr, &l);

	if (!(server->attr_bitmask[2] & FATTR4_WORD2_MODE_UMASK))
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

static int _nfs4_proc_readdir(struct dentry *dentry, const struct cred *cred,
		u64 cookie, struct page **pages, unsigned int count, bool plus)
{
	struct inode		*dir = d_inode(dentry);
	struct nfs_server	*server = NFS_SERVER(dir);
	struct nfs4_readdir_arg args = {
		.fh = NFS_FH(dir),
		.pages = pages,
		.pgbase = 0,
		.count = count,
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
	if (!(server->caps & NFS_CAP_SECURITY_LABEL))
		args.bitmask = server->attr_bitmask_nl;
	else
		args.bitmask = server->attr_bitmask;

	nfs4_setup_readdir(cookie, NFS_I(dir)->cookieverf, dentry, &args);
	res.pgbase = args.pgbase;
	status = nfs4_call_sync(server->client, server, &msg, &args.seq_args,
			&res.seq_res, 0);
	if (status >= 0) {
		memcpy(NFS_I(dir)->cookieverf, res.verifier.data, NFS4_VERIFIER_SIZE);
		status += args.pgbase;
	}

	nfs_invalidate_atime(dir);

	dprintk("%s: returns %d\n", __func__, status);
	return status;
}

static int nfs4_proc_readdir(struct dentry *dentry, const struct cred *cred,
		u64 cookie, struct page **pages, unsigned int count, bool plus)
{
	struct nfs4_exception exception = {
		.interruptible = true,
	};
	int err;
	do {
		err = _nfs4_proc_readdir(dentry, cred, cookie,
				pages, count, plus);
		trace_nfs4_readdir(d_inode(dentry), err);
		err = nfs4_handle_exception(NFS_SERVER(d_inode(dentry)), err,
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
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs4_exception exception = {
		.interruptible = true,
	};
	struct nfs4_label l, *label = NULL;
	int err;

	label = nfs4_label_init_security(dir, dentry, sattr, &l);

	if (!(server->attr_bitmask[2] & FATTR4_WORD2_MODE_UMASK))
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
	struct nfs4_exception exception = {
		.interruptible = true,
	};
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
	struct nfs4_exception exception = {
		.interruptible = true,
	};
	int err;

	do {
		err = _nfs4_do_fsinfo(server, fhandle, fsinfo);
		trace_nfs4_fsinfo(server, fhandle, fsinfo->fattr, err);
		if (err == 0) {
			nfs4_set_lease_period(server->nfs_client, fsinfo->lease_time * HZ);
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
		set_pnfs_layoutdriver(server, fhandle, fsinfo);
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
	struct nfs4_exception exception = {
		.interruptible = true,
	};
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
	return nfs4_select_rw_stateid(ctx->state, fmode, l_ctx, stateid, NULL);
}
EXPORT_SYMBOL_GPL(nfs4_set_rw_stateid);

static bool nfs4_stateid_is_current(nfs4_stateid *stateid,
		const struct nfs_open_context *ctx,
		const struct nfs_lock_context *l_ctx,
		fmode_t fmode)
{
	nfs4_stateid _current_stateid;

	/* If the current stateid represents a lost lock, then exit */
	if (nfs4_set_rw_stateid(&_current_stateid, ctx, l_ctx, fmode) == -EIO)
		return true;
	return nfs4_stateid_match(stateid, &_current_stateid);
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

static int nfs4_read_done_cb(struct rpc_task *task, struct nfs_pgio_header *hdr)
{
	struct nfs_server *server = NFS_SERVER(hdr->inode);

	trace_nfs4_read(hdr, task->tk_status);
	if (task->tk_status < 0) {
		struct nfs4_exception exception = {
			.inode = hdr->inode,
			.state = hdr->args.context->state,
			.stateid = &hdr->args.stateid,
		};
		task->tk_status = nfs4_async_handle_exception(task,
				server, task->tk_status, &exception);
		if (exception.retry) {
			rpc_restart_call_prepare(task);
			return -EAGAIN;
		}
	}

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

static bool nfs4_read_plus_not_supported(struct rpc_task *task,
					 struct nfs_pgio_header *hdr)
{
	struct nfs_server *server = NFS_SERVER(hdr->inode);
	struct rpc_message *msg = &task->tk_msg;

	if (msg->rpc_proc == &nfs4_procedures[NFSPROC4_CLNT_READ_PLUS] &&
	    server->caps & NFS_CAP_READ_PLUS && task->tk_status == -ENOTSUPP) {
		server->caps &= ~NFS_CAP_READ_PLUS;
		msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_READ];
		rpc_restart_call_prepare(task);
		return true;
	}
	return false;
}

static int nfs4_read_done(struct rpc_task *task, struct nfs_pgio_header *hdr)
{
	dprintk("--> %s\n", __func__);

	if (!nfs4_sequence_done(task, &hdr->res.seq_res))
		return -EAGAIN;
	if (nfs4_read_stateid_changed(task, &hdr->args))
		return -EAGAIN;
	if (nfs4_read_plus_not_supported(task, hdr))
		return -EAGAIN;
	if (task->tk_status > 0)
		nfs_invalidate_atime(hdr->inode);
	return hdr->pgio_done_cb ? hdr->pgio_done_cb(task, hdr) :
				    nfs4_read_done_cb(task, hdr);
}

#if defined CONFIG_NFS_V4_2 && defined CONFIG_NFS_V4_2_READ_PLUS
static void nfs42_read_plus_support(struct nfs_server *server, struct rpc_message *msg)
{
	if (server->caps & NFS_CAP_READ_PLUS)
		msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_READ_PLUS];
	else
		msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_READ];
}
#else
static void nfs42_read_plus_support(struct nfs_server *server, struct rpc_message *msg)
{
	msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_READ];
}
#endif /* CONFIG_NFS_V4_2 */

static void nfs4_proc_read_setup(struct nfs_pgio_header *hdr,
				 struct rpc_message *msg)
{
	hdr->timestamp   = jiffies;
	if (!hdr->pgio_done_cb)
		hdr->pgio_done_cb = nfs4_read_done_cb;
	nfs42_read_plus_support(NFS_SERVER(hdr->inode), msg);
	nfs4_init_sequence(&hdr->args.seq_args, &hdr->res.seq_res, 0, 0);
}

static int nfs4_proc_pgio_rpc_prepare(struct rpc_task *task,
				      struct nfs_pgio_header *hdr)
{
	if (nfs4_setup_sequence(NFS_SERVER(hdr->inode)->nfs_client,
			&hdr->args.seq_args,
			&hdr->res.seq_res,
			task))
		return 0;
	if (nfs4_set_rw_stateid(&hdr->args.stateid, hdr->args.context,
				hdr->args.lock_context,
				hdr->rw_mode) == -EIO)
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
	if (task->tk_status < 0) {
		struct nfs4_exception exception = {
			.inode = hdr->inode,
			.state = hdr->args.context->state,
			.stateid = &hdr->args.stateid,
		};
		task->tk_status = nfs4_async_handle_exception(task,
				NFS_SERVER(inode), task->tk_status,
				&exception);
		if (exception.retry) {
			rpc_restart_call_prepare(task);
			return -EAGAIN;
		}
	}
	if (task->tk_status >= 0) {
		renew_lease(NFS_SERVER(inode), hdr->timestamp);
		nfs_writeback_update_inode(hdr);
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

static void nfs4_bitmask_set(__u32 bitmask[NFS4_BITMASK_SZ], const __u32 *src,
			     struct inode *inode, struct nfs_server *server,
			     struct nfs4_label *label)
{
	unsigned long cache_validity = READ_ONCE(NFS_I(inode)->cache_validity);
	unsigned int i;

	memcpy(bitmask, src, sizeof(*bitmask) * NFS4_BITMASK_SZ);

	if (cache_validity & (NFS_INO_INVALID_CHANGE | NFS_INO_REVAL_PAGECACHE))
		bitmask[0] |= FATTR4_WORD0_CHANGE;
	if (cache_validity & NFS_INO_INVALID_ATIME)
		bitmask[1] |= FATTR4_WORD1_TIME_ACCESS;
	if (cache_validity & NFS_INO_INVALID_OTHER)
		bitmask[1] |= FATTR4_WORD1_MODE | FATTR4_WORD1_OWNER |
				FATTR4_WORD1_OWNER_GROUP |
				FATTR4_WORD1_NUMLINKS;
	if (label && label->len && cache_validity & NFS_INO_INVALID_LABEL)
		bitmask[2] |= FATTR4_WORD2_SECURITY_LABEL;
	if (cache_validity & NFS_INO_INVALID_CTIME)
		bitmask[1] |= FATTR4_WORD1_TIME_METADATA;
	if (cache_validity & NFS_INO_INVALID_MTIME)
		bitmask[1] |= FATTR4_WORD1_TIME_MODIFY;
	if (cache_validity & NFS_INO_INVALID_BLOCKS)
		bitmask[1] |= FATTR4_WORD1_SPACE_USED;

	if (nfs4_have_delegation(inode, FMODE_READ) &&
	    !(cache_validity & NFS_INO_REVAL_FORCED))
		bitmask[0] &= ~FATTR4_WORD0_SIZE;
	else if (cache_validity &
		 (NFS_INO_INVALID_SIZE | NFS_INO_REVAL_PAGECACHE))
		bitmask[0] |= FATTR4_WORD0_SIZE;

	for (i = 0; i < NFS4_BITMASK_SZ; i++)
		bitmask[i] &= server->attr_bitmask[i];
}

static void nfs4_proc_write_setup(struct nfs_pgio_header *hdr,
				  struct rpc_message *msg,
				  struct rpc_clnt **clnt)
{
	struct nfs_server *server = NFS_SERVER(hdr->inode);

	if (!nfs4_write_need_cache_consistency_data(hdr)) {
		hdr->args.bitmask = NULL;
		hdr->res.fattr = NULL;
	} else {
		nfs4_bitmask_set(hdr->args.bitmask_store,
				 server->cache_consistency_bitmask,
				 hdr->inode, server, NULL);
		hdr->args.bitmask = hdr->args.bitmask_store;
	}

	if (!hdr->pgio_done_cb)
		hdr->pgio_done_cb = nfs4_write_done_cb;
	hdr->res.server = server;
	hdr->timestamp   = jiffies;

	msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_WRITE];
	nfs4_init_sequence(&hdr->args.seq_args, &hdr->res.seq_res, 0, 0);
	nfs4_state_protect_write(server->nfs_client, clnt, msg, hdr);
}

static void nfs4_proc_commit_rpc_prepare(struct rpc_task *task, struct nfs_commit_data *data)
{
	nfs4_setup_sequence(NFS_SERVER(data->inode)->nfs_client,
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

static void nfs4_proc_commit_setup(struct nfs_commit_data *data, struct rpc_message *msg,
				   struct rpc_clnt **clnt)
{
	struct nfs_server *server = NFS_SERVER(data->inode);

	if (data->commit_done_cb == NULL)
		data->commit_done_cb = nfs4_commit_done_cb;
	data->res.server = server;
	msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_COMMIT];
	nfs4_init_sequence(&data->args.seq_args, &data->res.seq_res, 1, 0);
	nfs4_state_protect(server->nfs_client, NFS_SP4_MACH_CRED_COMMIT, clnt, msg);
}

static int _nfs4_proc_commit(struct file *dst, struct nfs_commitargs *args,
				struct nfs_commitres *res)
{
	struct inode *dst_inode = file_inode(dst);
	struct nfs_server *server = NFS_SERVER(dst_inode);
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_COMMIT],
		.rpc_argp = args,
		.rpc_resp = res,
	};

	args->fh = NFS_FH(dst_inode);
	return nfs4_call_sync(server->client, server, &msg,
			&args->seq_args, &res->seq_res, 1);
}

int nfs4_proc_commit(struct file *dst, __u64 offset, __u32 count, struct nfs_commitres *res)
{
	struct nfs_commitargs args = {
		.offset = offset,
		.count = count,
	};
	struct nfs_server *dst_server = NFS_SERVER(file_inode(dst));
	struct nfs4_exception exception = { };
	int status;

	do {
		status = _nfs4_proc_commit(dst, &args, res);
		status = nfs4_handle_exception(dst_server, status, &exception);
	} while (exception.retry);

	return status;
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

	if (refcount_read(&clp->cl_count) > 1)
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

static int nfs4_proc_async_renew(struct nfs_client *clp, const struct cred *cred, unsigned renew_flags)
{
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_RENEW],
		.rpc_argp	= clp,
		.rpc_cred	= cred,
	};
	struct nfs4_renewdata *data;

	if (renew_flags == 0)
		return 0;
	if (!refcount_inc_not_zero(&clp->cl_count))
		return -EIO;
	data = kmalloc(sizeof(*data), GFP_NOFS);
	if (data == NULL) {
		nfs_put_client(clp);
		return -ENOMEM;
	}
	data->client = clp;
	data->timestamp = jiffies;
	return rpc_call_async(clp->cl_rpcclient, &msg, RPC_TASK_TIMEOUT,
			&nfs4_renew_ops, data);
}

static int nfs4_proc_renew(struct nfs_client *clp, const struct cred *cred)
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

int nfs4_buf_to_pages_noslab(const void *buf, size_t buflen,
		struct page **pages)
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
	char data[];
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
	struct page **pages;
	struct nfs_getaclargs args = {
		.fh = NFS_FH(inode),
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
	unsigned int npages;
	int ret = -ENOMEM, i;
	struct nfs_server *server = NFS_SERVER(inode);

	if (buflen == 0)
		buflen = server->rsize;

	npages = DIV_ROUND_UP(buflen, PAGE_SIZE) + 1;
	pages = kmalloc_array(npages, sizeof(struct page *), GFP_NOFS);
	if (!pages)
		return -ENOMEM;

	args.acl_pages = pages;

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
	kfree(pages);
	return ret;
}

static ssize_t nfs4_get_acl_uncached(struct inode *inode, void *buf, size_t buflen)
{
	struct nfs4_exception exception = {
		.interruptible = true,
	};
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

	/* You can't remove system.nfs4_acl: */
	if (buflen == 0)
		return -EINVAL;
	if (!nfs4_server_supports_acls(server))
		return -EOPNOTSUPP;
	if (npages > ARRAY_SIZE(pages))
		return -ERANGE;
	i = nfs4_buf_to_pages_noslab(buf, buflen, arg.acl_pages);
	if (i < 0)
		return i;
	nfs4_inode_make_writeable(inode);
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
	NFS_I(inode)->cache_validity |= NFS_INO_INVALID_CHANGE
		| NFS_INO_INVALID_CTIME
		| NFS_INO_REVAL_FORCED;
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
		if (err == -NFS4ERR_BADOWNER || err == -NFS4ERR_BADNAME) {
			/*
			 * no need to retry since the kernel
			 * isn't involved in encoding the ACEs.
			 */
			err = -EINVAL;
			break;
		}
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
	return label.len;
}

static int nfs4_get_security_label(struct inode *inode, void *buf,
					size_t buflen)
{
	struct nfs4_exception exception = {
		.interruptible = true,
	};
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
		.fh		= NFS_FH(inode),
		.iap		= &sattr,
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
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_SETATTR],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
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
nfs4_set_security_label(struct inode *inode, const void *buf, size_t buflen)
{
	struct nfs4_label ilabel, *olabel = NULL;
	struct nfs_fattr fattr;
	int status;

	if (!nfs_server_capable(inode, NFS_CAP_SECURITY_LABEL))
		return -EOPNOTSUPP;

	nfs_fattr_init(&fattr);

	ilabel.pi = 0;
	ilabel.lfs = 0;
	ilabel.label = (char *)buf;
	ilabel.len = buflen;

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
	return status;
}
#endif	/* CONFIG_NFS_V4_SECURITY_LABEL */


static void nfs4_init_boot_verifier(const struct nfs_client *clp,
				    nfs4_verifier *bootverf)
{
	__be32 verf[2];

	if (test_bit(NFS4CLNT_PURGE_STATE, &clp->cl_state)) {
		/* An impossible timestamp guarantees this value
		 * will never match a generated boot time. */
		verf[0] = cpu_to_be32(U32_MAX);
		verf[1] = cpu_to_be32(U32_MAX);
	} else {
		struct nfs_net *nn = net_generic(clp->cl_net, nfs_net_id);
		u64 ns = ktime_to_ns(nn->boot_time);

		verf[0] = cpu_to_be32(ns >> 32);
		verf[1] = cpu_to_be32(ns);
	}
	memcpy(bootverf->data, verf, sizeof(bootverf->data));
}

static size_t
nfs4_get_uniquifier(struct nfs_client *clp, char *buf, size_t buflen)
{
	struct nfs_net *nn = net_generic(clp->cl_net, nfs_net_id);
	struct nfs_netns_client *nn_clp = nn->nfs_client;
	const char *id;

	buf[0] = '\0';

	if (nn_clp) {
		rcu_read_lock();
		id = rcu_dereference(nn_clp->identifier);
		if (id)
			strscpy(buf, id, buflen);
		rcu_read_unlock();
	}

	if (nfs4_client_id_uniquifier[0] != '\0' && buf[0] == '\0')
		strscpy(buf, nfs4_client_id_uniquifier, buflen);

	return strlen(buf);
}

static int
nfs4_init_nonuniform_client_string(struct nfs_client *clp)
{
	char buf[NFS4_CLIENT_ID_UNIQ_LEN];
	size_t buflen;
	size_t len;
	char *str;

	if (clp->cl_owner_id != NULL)
		return 0;

	rcu_read_lock();
	len = 14 +
		strlen(clp->cl_rpcclient->cl_nodename) +
		1 +
		strlen(rpc_peeraddr2str(clp->cl_rpcclient, RPC_DISPLAY_ADDR)) +
		1;
	rcu_read_unlock();

	buflen = nfs4_get_uniquifier(clp, buf, sizeof(buf));
	if (buflen)
		len += buflen + 1;

	if (len > NFS4_OPAQUE_LIMIT + 1)
		return -EINVAL;

	/*
	 * Since this string is allocated at mount time, and held until the
	 * nfs_client is destroyed, we can use GFP_KERNEL here w/o worrying
	 * about a memory-reclaim deadlock.
	 */
	str = kmalloc(len, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	rcu_read_lock();
	if (buflen)
		scnprintf(str, len, "Linux NFSv4.0 %s/%s/%s",
			  clp->cl_rpcclient->cl_nodename, buf,
			  rpc_peeraddr2str(clp->cl_rpcclient,
					   RPC_DISPLAY_ADDR));
	else
		scnprintf(str, len, "Linux NFSv4.0 %s/%s",
			  clp->cl_rpcclient->cl_nodename,
			  rpc_peeraddr2str(clp->cl_rpcclient,
					   RPC_DISPLAY_ADDR));
	rcu_read_unlock();

	clp->cl_owner_id = str;
	return 0;
}

static int
nfs4_init_uniform_client_string(struct nfs_client *clp)
{
	char buf[NFS4_CLIENT_ID_UNIQ_LEN];
	size_t buflen;
	size_t len;
	char *str;

	if (clp->cl_owner_id != NULL)
		return 0;

	len = 10 + 10 + 1 + 10 + 1 +
		strlen(clp->cl_rpcclient->cl_nodename) + 1;

	buflen = nfs4_get_uniquifier(clp, buf, sizeof(buf));
	if (buflen)
		len += buflen + 1;

	if (len > NFS4_OPAQUE_LIMIT + 1)
		return -EINVAL;

	/*
	 * Since this string is allocated at mount time, and held until the
	 * nfs_client is destroyed, we can use GFP_KERNEL here w/o worrying
	 * about a memory-reclaim deadlock.
	 */
	str = kmalloc(len, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	if (buflen)
		scnprintf(str, len, "Linux NFSv%u.%u %s/%s",
			  clp->rpc_ops->version, clp->cl_minorversion,
			  buf, clp->cl_rpcclient->cl_nodename);
	else
		scnprintf(str, len, "Linux NFSv%u.%u %s",
			  clp->rpc_ops->version, clp->cl_minorversion,
			  clp->cl_rpcclient->cl_nodename);
	clp->cl_owner_id = str;
	return 0;
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
 * @cred: credential to use for this call
 * @res: where to place the result
 *
 * Returns zero, a negative errno, or a negative NFS4ERR status code.
 */
int nfs4_proc_setclientid(struct nfs_client *clp, u32 program,
		unsigned short port, const struct cred *cred,
		struct nfs4_setclientid_res *res)
{
	nfs4_verifier sc_verifier;
	struct nfs4_setclientid setclientid = {
		.sc_verifier = &sc_verifier,
		.sc_prog = program,
		.sc_clnt = clp,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SETCLIENTID],
		.rpc_argp = &setclientid,
		.rpc_resp = res,
		.rpc_cred = cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = clp->cl_rpcclient,
		.rpc_message = &msg,
		.callback_ops = &nfs4_setclientid_ops,
		.callback_data = &setclientid,
		.flags = RPC_TASK_TIMEOUT | RPC_TASK_NO_ROUND_ROBIN,
	};
	unsigned long now = jiffies;
	int status;

	/* nfs_client_id4 */
	nfs4_init_boot_verifier(clp, &sc_verifier);

	if (test_bit(NFS_CS_MIGRATION, &clp->cl_flags))
		status = nfs4_init_uniform_client_string(clp);
	else
		status = nfs4_init_nonuniform_client_string(clp);

	if (status)
		goto out;

	/* cb_client4 */
	setclientid.sc_netid_len =
				nfs4_init_callback_netid(clp,
						setclientid.sc_netid,
						sizeof(setclientid.sc_netid));
	setclientid.sc_uaddr_len = scnprintf(setclientid.sc_uaddr,
				sizeof(setclientid.sc_uaddr), "%s.%u.%u",
				clp->cl_ipaddr, port >> 8, port & 255);

	dprintk("NFS call  setclientid auth=%s, '%s'\n",
		clp->cl_rpcclient->cl_auth->au_ops->au_name,
		clp->cl_owner_id);

	status = nfs4_call_sync_custom(&task_setup_data);
	if (setclientid.sc_cred) {
		kfree(clp->cl_acceptor);
		clp->cl_acceptor = rpcauth_stringify_acceptor(setclientid.sc_cred);
		put_rpccred(setclientid.sc_cred);
	}

	if (status == 0)
		do_renew_lease(clp, now);
out:
	trace_nfs4_setclientid(clp, status);
	dprintk("NFS reply setclientid: %d\n", status);
	return status;
}

/**
 * nfs4_proc_setclientid_confirm - Confirm client ID
 * @clp: state data structure
 * @arg: result of a previous SETCLIENTID
 * @cred: credential to use for this call
 *
 * Returns zero, a negative errno, or a negative NFS4ERR status code.
 */
int nfs4_proc_setclientid_confirm(struct nfs_client *clp,
		struct nfs4_setclientid_res *arg,
		const struct cred *cred)
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
	status = rpc_call_sync(clp->cl_rpcclient, &msg,
			       RPC_TASK_TIMEOUT | RPC_TASK_NO_ROUND_ROBIN);
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
	struct {
		struct nfs4_layoutreturn_args arg;
		struct nfs4_layoutreturn_res res;
		struct nfs4_xdr_opaque_data ld_private;
		u32 roc_barrier;
		bool roc;
	} lr;
	struct nfs_fattr fattr;
	int rpc_status;
	struct inode *inode;
};

static void nfs4_delegreturn_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_delegreturndata *data = calldata;
	struct nfs4_exception exception = {
		.inode = data->inode,
		.stateid = &data->stateid,
		.task_is_privileged = data->args.seq_args.sa_privileged,
	};

	if (!nfs4_sequence_done(task, &data->res.seq_res))
		return;

	trace_nfs4_delegreturn_exit(&data->args, &data->res, task->tk_status);

	/* Handle Layoutreturn errors */
	if (pnfs_roc_done(task, &data->args.lr_args, &data->res.lr_res,
			  &data->res.lr_ret) == -EAGAIN)
		goto out_restart;

	switch (task->tk_status) {
	case 0:
		renew_lease(data->res.server, data->timestamp);
		break;
	case -NFS4ERR_ADMIN_REVOKED:
	case -NFS4ERR_DELEG_REVOKED:
	case -NFS4ERR_EXPIRED:
		nfs4_free_revoked_stateid(data->res.server,
				data->args.stateid,
				task->tk_msg.rpc_cred);
		fallthrough;
	case -NFS4ERR_BAD_STATEID:
	case -NFS4ERR_STALE_STATEID:
	case -ETIMEDOUT:
		task->tk_status = 0;
		break;
	case -NFS4ERR_OLD_STATEID:
		if (!nfs4_refresh_delegation_stateid(&data->stateid, data->inode))
			nfs4_stateid_seqid_inc(&data->stateid);
		if (data->args.bitmask) {
			data->args.bitmask = NULL;
			data->res.fattr = NULL;
		}
		goto out_restart;
	case -NFS4ERR_ACCESS:
		if (data->args.bitmask) {
			data->args.bitmask = NULL;
			data->res.fattr = NULL;
			goto out_restart;
		}
		fallthrough;
	default:
		task->tk_status = nfs4_async_handle_exception(task,
				data->res.server, task->tk_status,
				&exception);
		if (exception.retry)
			goto out_restart;
	}
	nfs_delegation_mark_returned(data->inode, data->args.stateid);
	data->rpc_status = task->tk_status;
	return;
out_restart:
	task->tk_status = 0;
	rpc_restart_call_prepare(task);
}

static void nfs4_delegreturn_release(void *calldata)
{
	struct nfs4_delegreturndata *data = calldata;
	struct inode *inode = data->inode;

	if (data->lr.roc)
		pnfs_roc_release(&data->lr.arg, &data->lr.res,
				 data->res.lr_ret);
	if (inode) {
		nfs4_fattr_set_prechange(&data->fattr,
					 inode_peek_iversion_raw(inode));
		nfs_refresh_inode(inode, &data->fattr);
		nfs_iput_and_deactive(inode);
	}
	kfree(calldata);
}

static void nfs4_delegreturn_prepare(struct rpc_task *task, void *data)
{
	struct nfs4_delegreturndata *d_data;
	struct pnfs_layout_hdr *lo;

	d_data = (struct nfs4_delegreturndata *)data;

	if (!d_data->lr.roc && nfs4_wait_on_layoutreturn(d_data->inode, task)) {
		nfs4_sequence_done(task, &d_data->res.seq_res);
		return;
	}

	lo = d_data->args.lr_args ? d_data->args.lr_args->layout : NULL;
	if (lo && !pnfs_layout_is_valid(lo)) {
		d_data->args.lr_args = NULL;
		d_data->res.lr_res = NULL;
	}

	nfs4_setup_sequence(d_data->res.server->nfs_client,
			&d_data->args.seq_args,
			&d_data->res.seq_res,
			task);
}

static const struct rpc_call_ops nfs4_delegreturn_ops = {
	.rpc_call_prepare = nfs4_delegreturn_prepare,
	.rpc_call_done = nfs4_delegreturn_done,
	.rpc_release = nfs4_delegreturn_release,
};

static int _nfs4_proc_delegreturn(struct inode *inode, const struct cred *cred, const nfs4_stateid *stateid, int issync)
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
		.flags = RPC_TASK_ASYNC | RPC_TASK_TIMEOUT,
	};
	int status = 0;

	data = kzalloc(sizeof(*data), GFP_NOFS);
	if (data == NULL)
		return -ENOMEM;

	nfs4_state_protect(server->nfs_client,
			NFS_SP4_MACH_CRED_CLEANUP,
			&task_setup_data.rpc_client, &msg);

	data->args.fhandle = &data->fh;
	data->args.stateid = &data->stateid;
	nfs4_bitmask_set(data->args.bitmask_store,
			 server->cache_consistency_bitmask, inode, server,
			 NULL);
	data->args.bitmask = data->args.bitmask_store;
	nfs_copy_fh(&data->fh, NFS_FH(inode));
	nfs4_stateid_copy(&data->stateid, stateid);
	data->res.fattr = &data->fattr;
	data->res.server = server;
	data->res.lr_ret = -NFS4ERR_NOMATCHING_LAYOUT;
	data->lr.arg.ld_private = &data->lr.ld_private;
	nfs_fattr_init(data->res.fattr);
	data->timestamp = jiffies;
	data->rpc_status = 0;
	data->inode = nfs_igrab_and_active(inode);
	if (data->inode || issync) {
		data->lr.roc = pnfs_roc(inode, &data->lr.arg, &data->lr.res,
					cred);
		if (data->lr.roc) {
			data->args.lr_args = &data->lr.arg;
			data->res.lr_res = &data->lr.res;
		}
	}

	if (!data->inode)
		nfs4_init_sequence(&data->args.seq_args, &data->res.seq_res, 1,
				   1);
	else
		nfs4_init_sequence(&data->args.seq_args, &data->res.seq_res, 1,
				   0);
	task_setup_data.callback_data = data;
	msg.rpc_argp = &data->args;
	msg.rpc_resp = &data->res;
	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	if (!issync)
		goto out;
	status = rpc_wait_for_completion_task(task);
	if (status != 0)
		goto out;
	status = data->rpc_status;
out:
	rpc_put_task(task);
	return status;
}

int nfs4_proc_delegreturn(struct inode *inode, const struct cred *cred, const nfs4_stateid *stateid, int issync)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs4_exception exception = { };
	int err;
	do {
		err = _nfs4_proc_delegreturn(inode, cred, stateid, issync);
		trace_nfs4_delegreturn(inode, stateid, err);
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
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
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
	struct nfs4_exception exception = {
		.interruptible = true,
	};
	int err;

	do {
		err = _nfs4_proc_getlk(state, cmd, request);
		trace_nfs4_get_lock(request, state, cmd, err);
		err = nfs4_handle_exception(NFS_SERVER(state->inode), err,
				&exception);
	} while (exception.retry);
	return err;
}

/*
 * Update the seqid of a lock stateid after receiving
 * NFS4ERR_OLD_STATEID
 */
static bool nfs4_refresh_lock_old_stateid(nfs4_stateid *dst,
		struct nfs4_lock_state *lsp)
{
	struct nfs4_state *state = lsp->ls_state;
	bool ret = false;

	spin_lock(&state->state_lock);
	if (!nfs4_stateid_match_other(dst, &lsp->ls_stateid))
		goto out;
	if (!nfs4_stateid_is_newer(&lsp->ls_stateid, dst))
		nfs4_stateid_seqid_inc(dst);
	else
		dst->seqid = lsp->ls_stateid.seqid;
	ret = true;
out:
	spin_unlock(&state->state_lock);
	return ret;
}

static bool nfs4_sync_lock_stateid(nfs4_stateid *dst,
		struct nfs4_lock_state *lsp)
{
	struct nfs4_state *state = lsp->ls_state;
	bool ret;

	spin_lock(&state->state_lock);
	ret = !nfs4_stateid_match_other(dst, &lsp->ls_stateid);
	nfs4_stateid_copy(dst, &lsp->ls_stateid);
	spin_unlock(&state->state_lock);
	return ret;
}

struct nfs4_unlockdata {
	struct nfs_locku_args arg;
	struct nfs_locku_res res;
	struct nfs4_lock_state *lsp;
	struct nfs_open_context *ctx;
	struct nfs_lock_context *l_ctx;
	struct file_lock fl;
	struct nfs_server *server;
	unsigned long timestamp;
};

static struct nfs4_unlockdata *nfs4_alloc_unlockdata(struct file_lock *fl,
		struct nfs_open_context *ctx,
		struct nfs4_lock_state *lsp,
		struct nfs_seqid *seqid)
{
	struct nfs4_unlockdata *p;
	struct nfs4_state *state = lsp->ls_state;
	struct inode *inode = state->inode;

	p = kzalloc(sizeof(*p), GFP_NOFS);
	if (p == NULL)
		return NULL;
	p->arg.fh = NFS_FH(inode);
	p->arg.fl = &p->fl;
	p->arg.seqid = seqid;
	p->res.seqid = seqid;
	p->lsp = lsp;
	/* Ensure we don't close file until we're done freeing locks! */
	p->ctx = get_nfs_open_context(ctx);
	p->l_ctx = nfs_get_lock_context(ctx);
	locks_init_lock(&p->fl);
	locks_copy_lock(&p->fl, fl);
	p->server = NFS_SERVER(inode);
	spin_lock(&state->state_lock);
	nfs4_stateid_copy(&p->arg.stateid, &lsp->ls_stateid);
	spin_unlock(&state->state_lock);
	return p;
}

static void nfs4_locku_release_calldata(void *data)
{
	struct nfs4_unlockdata *calldata = data;
	nfs_free_seqid(calldata->arg.seqid);
	nfs4_put_lock_state(calldata->lsp);
	nfs_put_lock_context(calldata->l_ctx);
	put_nfs_open_context(calldata->ctx);
	kfree(calldata);
}

static void nfs4_locku_done(struct rpc_task *task, void *data)
{
	struct nfs4_unlockdata *calldata = data;
	struct nfs4_exception exception = {
		.inode = calldata->lsp->ls_state->inode,
		.stateid = &calldata->arg.stateid,
	};

	if (!nfs4_sequence_done(task, &calldata->res.seq_res))
		return;
	switch (task->tk_status) {
		case 0:
			renew_lease(calldata->server, calldata->timestamp);
			locks_lock_inode_wait(calldata->lsp->ls_state->inode, &calldata->fl);
			if (nfs4_update_lock_stateid(calldata->lsp,
					&calldata->res.stateid))
				break;
			fallthrough;
		case -NFS4ERR_ADMIN_REVOKED:
		case -NFS4ERR_EXPIRED:
			nfs4_free_revoked_stateid(calldata->server,
					&calldata->arg.stateid,
					task->tk_msg.rpc_cred);
			fallthrough;
		case -NFS4ERR_BAD_STATEID:
		case -NFS4ERR_STALE_STATEID:
			if (nfs4_sync_lock_stateid(&calldata->arg.stateid,
						calldata->lsp))
				rpc_restart_call_prepare(task);
			break;
		case -NFS4ERR_OLD_STATEID:
			if (nfs4_refresh_lock_old_stateid(&calldata->arg.stateid,
						calldata->lsp))
				rpc_restart_call_prepare(task);
			break;
		default:
			task->tk_status = nfs4_async_handle_exception(task,
					calldata->server, task->tk_status,
					&exception);
			if (exception.retry)
				rpc_restart_call_prepare(task);
	}
	nfs_release_seqid(calldata->arg.seqid);
}

static void nfs4_locku_prepare(struct rpc_task *task, void *data)
{
	struct nfs4_unlockdata *calldata = data;

	if (test_bit(NFS_CONTEXT_UNLOCK, &calldata->l_ctx->open_context->flags) &&
		nfs_async_iocounter_wait(task, calldata->l_ctx))
		return;

	if (nfs_wait_on_sequence(calldata->arg.seqid, task) != 0)
		goto out_wait;
	if (test_bit(NFS_LOCK_INITIALIZED, &calldata->lsp->ls_flags) == 0) {
		/* Note: exit _without_ running nfs4_locku_done */
		goto out_no_action;
	}
	calldata->timestamp = jiffies;
	if (nfs4_setup_sequence(calldata->server->nfs_client,
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
	if (fl->fl_flags & FL_CLOSE)
		set_bit(NFS_CONTEXT_UNLOCK, &ctx->flags);

	data = nfs4_alloc_unlockdata(fl, ctx, lsp, seqid);
	if (data == NULL) {
		nfs_free_seqid(seqid);
		return ERR_PTR(-ENOMEM);
	}

	nfs4_init_sequence(&data->arg.seq_args, &data->res.seq_res, 1, 0);
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
	struct nfs_seqid *(*alloc_seqid)(struct nfs_seqid_counter *, gfp_t);
	int status = 0;
	unsigned char fl_flags = request->fl_flags;

	status = nfs4_set_lock_state(state, request);
	/* Unlock _before_ we do the RPC call */
	request->fl_flags |= FL_EXISTS;
	/* Exclude nfs_delegation_claim_locks() */
	mutex_lock(&sp->so_delegreturn_mutex);
	/* Exclude nfs4_reclaim_open_stateid() - note nesting! */
	down_read(&nfsi->rwsem);
	if (locks_lock_inode_wait(inode, request) == -ENOENT) {
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
	alloc_seqid = NFS_SERVER(inode)->nfs_client->cl_mvops->alloc_seqid;
	seqid = alloc_seqid(&lsp->ls_seqid, GFP_KERNEL);
	status = -ENOMEM;
	if (IS_ERR(seqid))
		goto out;
	task = nfs4_do_unlck(request, nfs_file_open_context(request->fl_file), lsp, seqid);
	status = PTR_ERR(task);
	if (IS_ERR(task))
		goto out;
	status = rpc_wait_for_completion_task(task);
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
	struct nfs_seqid *(*alloc_seqid)(struct nfs_seqid_counter *, gfp_t);

	p = kzalloc(sizeof(*p), gfp_mask);
	if (p == NULL)
		return NULL;

	p->arg.fh = NFS_FH(inode);
	p->arg.fl = &p->fl;
	p->arg.open_seqid = nfs_alloc_seqid(&lsp->ls_state->owner->so_seqid, gfp_mask);
	if (IS_ERR(p->arg.open_seqid))
		goto out_free;
	alloc_seqid = server->nfs_client->cl_mvops->alloc_seqid;
	p->arg.lock_seqid = alloc_seqid(&lsp->ls_seqid, gfp_mask);
	if (IS_ERR(p->arg.lock_seqid))
		goto out_free_seqid;
	p->arg.lock_owner.clientid = server->nfs_client->cl_clientid;
	p->arg.lock_owner.id = lsp->ls_seqid.owner_id;
	p->arg.lock_owner.s_dev = server->s_dev;
	p->res.lock_seqid = p->arg.lock_seqid;
	p->lsp = lsp;
	p->server = server;
	p->ctx = get_nfs_open_context(ctx);
	locks_init_lock(&p->fl);
	locks_copy_lock(&p->fl, fl);
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
	if (!test_bit(NFS_LOCK_INITIALIZED, &data->lsp->ls_flags)) {
		if (nfs_wait_on_sequence(data->arg.open_seqid, task) != 0) {
			goto out_release_lock_seqid;
		}
		nfs4_stateid_copy(&data->arg.open_stateid,
				&state->open_stateid);
		data->arg.new_lock_owner = 1;
		data->res.open_seqid = data->arg.open_seqid;
	} else {
		data->arg.new_lock_owner = 0;
		nfs4_stateid_copy(&data->arg.lock_stateid,
				&data->lsp->ls_stateid);
	}
	if (!nfs4_valid_open_stateid(state)) {
		data->rpc_status = -EBADF;
		task->tk_action = NULL;
		goto out_release_open_seqid;
	}
	data->timestamp = jiffies;
	if (nfs4_setup_sequence(data->server->nfs_client,
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
	struct nfs4_lock_state *lsp = data->lsp;

	dprintk("%s: begin!\n", __func__);

	if (!nfs4_sequence_done(task, &data->res.seq_res))
		return;

	data->rpc_status = task->tk_status;
	switch (task->tk_status) {
	case 0:
		renew_lease(NFS_SERVER(d_inode(data->ctx->dentry)),
				data->timestamp);
		if (data->arg.new_lock && !data->cancelled) {
			data->fl.fl_flags &= ~(FL_SLEEP | FL_ACCESS);
			if (locks_lock_inode_wait(lsp->ls_state->inode, &data->fl) < 0)
				goto out_restart;
		}
		if (data->arg.new_lock_owner != 0) {
			nfs_confirm_seqid(&lsp->ls_seqid, 0);
			nfs4_stateid_copy(&lsp->ls_stateid, &data->res.stateid);
			set_bit(NFS_LOCK_INITIALIZED, &lsp->ls_flags);
		} else if (!nfs4_update_lock_stateid(lsp, &data->res.stateid))
			goto out_restart;
		break;
	case -NFS4ERR_BAD_STATEID:
	case -NFS4ERR_OLD_STATEID:
	case -NFS4ERR_STALE_STATEID:
	case -NFS4ERR_EXPIRED:
		if (data->arg.new_lock_owner != 0) {
			if (!nfs4_stateid_match(&data->arg.open_stateid,
						&lsp->ls_state->open_stateid))
				goto out_restart;
		} else if (!nfs4_stateid_match(&data->arg.lock_stateid,
						&lsp->ls_stateid))
				goto out_restart;
	}
out_done:
	dprintk("%s: done, ret = %d!\n", __func__, data->rpc_status);
	return;
out_restart:
	if (!data->cancelled)
		rpc_restart_call_prepare(task);
	goto out_done;
}

static void nfs4_lock_release(void *calldata)
{
	struct nfs4_lockdata *data = calldata;

	dprintk("%s: begin!\n", __func__);
	nfs_free_seqid(data->arg.open_seqid);
	if (data->cancelled && data->rpc_status == 0) {
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
	case -NFS4ERR_EXPIRED:
	case -NFS4ERR_BAD_STATEID:
		lsp->ls_seqid.flags &= ~NFS_SEQID_CONFIRMED;
		if (new_lock_owner != 0 ||
		   test_bit(NFS_LOCK_INITIALIZED, &lsp->ls_flags) != 0)
			nfs4_schedule_stateid_recovery(server, lsp->ls_state);
		break;
	case -NFS4ERR_STALE_STATEID:
		lsp->ls_seqid.flags &= ~NFS_SEQID_CONFIRMED;
		nfs4_schedule_lease_recovery(server->nfs_client);
	}
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
		.flags = RPC_TASK_ASYNC | RPC_TASK_CRED_NOREF,
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
	nfs4_init_sequence(&data->arg.seq_args, &data->res.seq_res, 1,
				recovery_type > NFS_LOCK_NEW);
	msg.rpc_argp = &data->arg;
	msg.rpc_resp = &data->res;
	task_setup_data.callback_data = data;
	if (recovery_type > NFS_LOCK_NEW) {
		if (recovery_type == NFS_LOCK_RECLAIM)
			data->arg.reclaim = NFS_LOCK_RECLAIM;
	} else
		data->arg.new_lock = 1;
	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	ret = rpc_wait_for_completion_task(task);
	if (ret == 0) {
		ret = data->rpc_status;
		if (ret)
			nfs4_handle_setlk_error(data->server, data->lsp,
					data->arg.new_lock_owner, ret);
	} else
		data->cancelled = true;
	trace_nfs4_set_lock(fl, state, &data->res.stateid, cmd, ret);
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
	if (!recover_lost_locks) {
		set_bit(NFS_LOCK_LOST, &request->fl_u.nfs4_fl.owner->ls_flags);
		return 0;
	}
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

#if defined(CONFIG_NFS_V4_1)
static int nfs41_lock_expired(struct nfs4_state *state, struct file_lock *request)
{
	struct nfs4_lock_state *lsp;
	int status;

	status = nfs4_set_lock_state(state, request);
	if (status != 0)
		return status;
	lsp = request->fl_u.nfs4_fl.owner;
	if (test_bit(NFS_LOCK_INITIALIZED, &lsp->ls_flags) ||
	    test_bit(NFS_LOCK_LOST, &lsp->ls_flags))
		return 0;
	return nfs4_lock_expired(state, request);
}
#endif

static int _nfs4_proc_setlk(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct nfs_inode *nfsi = NFS_I(state->inode);
	struct nfs4_state_owner *sp = state->owner;
	unsigned char fl_flags = request->fl_flags;
	int status;

	request->fl_flags |= FL_ACCESS;
	status = locks_lock_inode_wait(state->inode, request);
	if (status < 0)
		goto out;
	mutex_lock(&sp->so_delegreturn_mutex);
	down_read(&nfsi->rwsem);
	if (test_bit(NFS_DELEGATED_STATE, &state->flags)) {
		/* Yes: cache locks! */
		/* ...but avoid races with delegation recall... */
		request->fl_flags = fl_flags & ~FL_SLEEP;
		status = locks_lock_inode_wait(state->inode, request);
		up_read(&nfsi->rwsem);
		mutex_unlock(&sp->so_delegreturn_mutex);
		goto out;
	}
	up_read(&nfsi->rwsem);
	mutex_unlock(&sp->so_delegreturn_mutex);
	status = _nfs4_do_setlk(state, cmd, request, NFS_LOCK_NEW);
out:
	request->fl_flags = fl_flags;
	return status;
}

static int nfs4_proc_setlk(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct nfs4_exception exception = {
		.state = state,
		.inode = state->inode,
		.interruptible = true,
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

#define NFS4_LOCK_MINTIMEOUT (1 * HZ)
#define NFS4_LOCK_MAXTIMEOUT (30 * HZ)

static int
nfs4_retry_setlk_simple(struct nfs4_state *state, int cmd,
			struct file_lock *request)
{
	int		status = -ERESTARTSYS;
	unsigned long	timeout = NFS4_LOCK_MINTIMEOUT;

	while(!signalled()) {
		status = nfs4_proc_setlk(state, cmd, request);
		if ((status != -EAGAIN) || IS_SETLK(cmd))
			break;
		freezable_schedule_timeout_interruptible(timeout);
		timeout *= 2;
		timeout = min_t(unsigned long, NFS4_LOCK_MAXTIMEOUT, timeout);
		status = -ERESTARTSYS;
	}
	return status;
}

#ifdef CONFIG_NFS_V4_1
struct nfs4_lock_waiter {
	struct task_struct	*task;
	struct inode		*inode;
	struct nfs_lowner	*owner;
};

static int
nfs4_wake_lock_waiter(wait_queue_entry_t *wait, unsigned int mode, int flags, void *key)
{
	int ret;
	struct nfs4_lock_waiter	*waiter	= wait->private;

	/* NULL key means to wake up everyone */
	if (key) {
		struct cb_notify_lock_args	*cbnl = key;
		struct nfs_lowner		*lowner = &cbnl->cbnl_owner,
						*wowner = waiter->owner;

		/* Only wake if the callback was for the same owner. */
		if (lowner->id != wowner->id || lowner->s_dev != wowner->s_dev)
			return 0;

		/* Make sure it's for the right inode */
		if (nfs_compare_fh(NFS_FH(waiter->inode), &cbnl->cbnl_fh))
			return 0;
	}

	/* override "private" so we can use default_wake_function */
	wait->private = waiter->task;
	ret = woken_wake_function(wait, mode, flags, key);
	if (ret)
		list_del_init(&wait->entry);
	wait->private = waiter;
	return ret;
}

static int
nfs4_retry_setlk(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	int status = -ERESTARTSYS;
	struct nfs4_lock_state *lsp = request->fl_u.nfs4_fl.owner;
	struct nfs_server *server = NFS_SERVER(state->inode);
	struct nfs_client *clp = server->nfs_client;
	wait_queue_head_t *q = &clp->cl_lock_waitq;
	struct nfs_lowner owner = { .clientid = clp->cl_clientid,
				    .id = lsp->ls_seqid.owner_id,
				    .s_dev = server->s_dev };
	struct nfs4_lock_waiter waiter = { .task  = current,
					   .inode = state->inode,
					   .owner = &owner};
	wait_queue_entry_t wait;

	/* Don't bother with waitqueue if we don't expect a callback */
	if (!test_bit(NFS_STATE_MAY_NOTIFY_LOCK, &state->flags))
		return nfs4_retry_setlk_simple(state, cmd, request);

	init_wait(&wait);
	wait.private = &waiter;
	wait.func = nfs4_wake_lock_waiter;

	while(!signalled()) {
		add_wait_queue(q, &wait);
		status = nfs4_proc_setlk(state, cmd, request);
		if ((status != -EAGAIN) || IS_SETLK(cmd)) {
			finish_wait(q, &wait);
			break;
		}

		status = -ERESTARTSYS;
		freezer_do_not_count();
		wait_woken(&wait, TASK_INTERRUPTIBLE, NFS4_LOCK_MAXTIMEOUT);
		freezer_count();
		finish_wait(q, &wait);
	}

	return status;
}
#else /* !CONFIG_NFS_V4_1 */
static inline int
nfs4_retry_setlk(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	return nfs4_retry_setlk_simple(state, cmd, request);
}
#endif

static int
nfs4_proc_lock(struct file *filp, int cmd, struct file_lock *request)
{
	struct nfs_open_context *ctx;
	struct nfs4_state *state;
	int status;

	/* verify open state */
	ctx = nfs_file_open_context(filp);
	state = ctx->state;

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

	if ((request->fl_flags & FL_POSIX) &&
	    !test_bit(NFS_STATE_POSIX_LOCKS, &state->flags))
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

	status = nfs4_set_lock_state(state, request);
	if (status != 0)
		return status;

	return nfs4_retry_setlk(state, cmd, request);
}

int nfs4_lock_delegation_recall(struct file_lock *fl, struct nfs4_state *state, const nfs4_stateid *stateid)
{
	struct nfs_server *server = NFS_SERVER(state->inode);
	int err;

	err = nfs4_set_lock_state(state, fl);
	if (err != 0)
		return err;
	do {
		err = _nfs4_do_setlk(state, F_SETLK, fl, NFS_LOCK_NEW);
		if (err != -NFS4ERR_DELAY)
			break;
		ssleep(1);
	} while (err == -NFS4ERR_DELAY);
	return nfs4_handle_delegation_recall_error(server, state, stateid, fl, err);
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
	nfs4_setup_sequence(server->nfs_client, &data->args.seq_args,
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
	nfs4_init_sequence(&data->args.seq_args, &data->res.seq_res, 0, 0);
	rpc_call_async(server->client, &msg, 0, &nfs4_release_lockowner_ops, data);
}

#define XATTR_NAME_NFSV4_ACL "system.nfs4_acl"

static int nfs4_xattr_set_nfs4_acl(const struct xattr_handler *handler,
				   struct dentry *unused, struct inode *inode,
				   const char *key, const void *buf,
				   size_t buflen, int flags)
{
	return nfs4_proc_set_acl(inode, buf, buflen);
}

static int nfs4_xattr_get_nfs4_acl(const struct xattr_handler *handler,
				   struct dentry *unused, struct inode *inode,
				   const char *key, void *buf, size_t buflen,
				   int flags)
{
	return nfs4_proc_get_acl(inode, buf, buflen);
}

static bool nfs4_xattr_list_nfs4_acl(struct dentry *dentry)
{
	return nfs4_server_supports_acls(NFS_SERVER(d_inode(dentry)));
}

#ifdef CONFIG_NFS_V4_SECURITY_LABEL

static int nfs4_xattr_set_nfs4_label(const struct xattr_handler *handler,
				     struct dentry *unused, struct inode *inode,
				     const char *key, const void *buf,
				     size_t buflen, int flags)
{
	if (security_ismaclabel(key))
		return nfs4_set_security_label(inode, buf, buflen);

	return -EOPNOTSUPP;
}

static int nfs4_xattr_get_nfs4_label(const struct xattr_handler *handler,
				     struct dentry *unused, struct inode *inode,
				     const char *key, void *buf, size_t buflen,
				     int flags)
{
	if (security_ismaclabel(key))
		return nfs4_get_security_label(inode, buf, buflen);
	return -EOPNOTSUPP;
}

static ssize_t
nfs4_listxattr_nfs4_label(struct inode *inode, char *list, size_t list_len)
{
	int len = 0;

	if (nfs_server_capable(inode, NFS_CAP_SECURITY_LABEL)) {
		len = security_inode_listsecurity(inode, list, list_len);
		if (len >= 0 && list_len && len > list_len)
			return -ERANGE;
	}
	return len;
}

static const struct xattr_handler nfs4_xattr_nfs4_label_handler = {
	.prefix = XATTR_SECURITY_PREFIX,
	.get	= nfs4_xattr_get_nfs4_label,
	.set	= nfs4_xattr_set_nfs4_label,
};

#else

static ssize_t
nfs4_listxattr_nfs4_label(struct inode *inode, char *list, size_t list_len)
{
	return 0;
}

#endif

#ifdef CONFIG_NFS_V4_2
static int nfs4_xattr_set_nfs4_user(const struct xattr_handler *handler,
				    struct dentry *unused, struct inode *inode,
				    const char *key, const void *buf,
				    size_t buflen, int flags)
{
	u32 mask;
	int ret;

	if (!nfs_server_capable(inode, NFS_CAP_XATTR))
		return -EOPNOTSUPP;

	/*
	 * There is no mapping from the MAY_* flags to the NFS_ACCESS_XA*
	 * flags right now. Handling of xattr operations use the normal
	 * file read/write permissions.
	 *
	 * Just in case the server has other ideas (which RFC 8276 allows),
	 * do a cached access check for the XA* flags to possibly avoid
	 * doing an RPC and getting EACCES back.
	 */
	if (!nfs_access_get_cached(inode, current_cred(), &mask, true)) {
		if (!(mask & NFS_ACCESS_XAWRITE))
			return -EACCES;
	}

	if (buf == NULL) {
		ret = nfs42_proc_removexattr(inode, key);
		if (!ret)
			nfs4_xattr_cache_remove(inode, key);
	} else {
		ret = nfs42_proc_setxattr(inode, key, buf, buflen, flags);
		if (!ret)
			nfs4_xattr_cache_add(inode, key, buf, NULL, buflen);
	}

	return ret;
}

static int nfs4_xattr_get_nfs4_user(const struct xattr_handler *handler,
				    struct dentry *unused, struct inode *inode,
				    const char *key, void *buf, size_t buflen,
				    int flags)
{
	u32 mask;
	ssize_t ret;

	if (!nfs_server_capable(inode, NFS_CAP_XATTR))
		return -EOPNOTSUPP;

	if (!nfs_access_get_cached(inode, current_cred(), &mask, true)) {
		if (!(mask & NFS_ACCESS_XAREAD))
			return -EACCES;
	}

	ret = nfs_revalidate_inode(NFS_SERVER(inode), inode);
	if (ret)
		return ret;

	ret = nfs4_xattr_cache_get(inode, key, buf, buflen);
	if (ret >= 0 || (ret < 0 && ret != -ENOENT))
		return ret;

	ret = nfs42_proc_getxattr(inode, key, buf, buflen);

	return ret;
}

static ssize_t
nfs4_listxattr_nfs4_user(struct inode *inode, char *list, size_t list_len)
{
	u64 cookie;
	bool eof;
	ssize_t ret, size;
	char *buf;
	size_t buflen;
	u32 mask;

	if (!nfs_server_capable(inode, NFS_CAP_XATTR))
		return 0;

	if (!nfs_access_get_cached(inode, current_cred(), &mask, true)) {
		if (!(mask & NFS_ACCESS_XALIST))
			return 0;
	}

	ret = nfs_revalidate_inode(NFS_SERVER(inode), inode);
	if (ret)
		return ret;

	ret = nfs4_xattr_cache_list(inode, list, list_len);
	if (ret >= 0 || (ret < 0 && ret != -ENOENT))
		return ret;

	cookie = 0;
	eof = false;
	buflen = list_len ? list_len : XATTR_LIST_MAX;
	buf = list_len ? list : NULL;
	size = 0;

	while (!eof) {
		ret = nfs42_proc_listxattrs(inode, buf, buflen,
		    &cookie, &eof);
		if (ret < 0)
			return ret;

		if (list_len) {
			buf += ret;
			buflen -= ret;
		}
		size += ret;
	}

	if (list_len)
		nfs4_xattr_cache_set_list(inode, list, size);

	return size;
}

#else

static ssize_t
nfs4_listxattr_nfs4_user(struct inode *inode, char *list, size_t list_len)
{
	return 0;
}
#endif /* CONFIG_NFS_V4_2 */

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
	u32 bitmask[3];
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

	bitmask[0] = nfs4_fattr_bitmap[0] | FATTR4_WORD0_FS_LOCATIONS;
	bitmask[1] = nfs4_fattr_bitmap[1];

	/* Ask for the fileid of the absent filesystem if mounted_on_fileid
	 * is not supported */
	if (NFS_SERVER(dir)->attr_bitmask[1] & FATTR4_WORD1_MOUNTED_ON_FILEID)
		bitmask[0] &= ~FATTR4_WORD0_FILEID;
	else
		bitmask[1] &= ~FATTR4_WORD1_MOUNTED_ON_FILEID;

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
	struct nfs4_exception exception = {
		.interruptible = true,
	};
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
				     struct page *page, const struct cred *cred)
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

	nfs4_init_sequence(&args.seq_args, &res.seq_res, 0, 1);
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
				     struct page *page, const struct cred *cred)
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

	nfs4_init_sequence(&args.seq_args, &res.seq_res, 0, 1);
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
			    struct page *page, const struct cred *cred)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_client *clp = server->nfs_client;
	const struct nfs4_mig_recovery_ops *ops =
					clp->cl_mvops->mig_recovery_ops;
	struct nfs4_exception exception = {
		.interruptible = true,
	};
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
static int _nfs40_proc_fsid_present(struct inode *inode, const struct cred *cred)
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

	nfs4_init_sequence(&args.seq_args, &res.seq_res, 0, 1);
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
static int _nfs41_proc_fsid_present(struct inode *inode, const struct cred *cred)
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

	nfs4_init_sequence(&args.seq_args, &res.seq_res, 0, 1);
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
int nfs4_proc_fsid_present(struct inode *inode, const struct cred *cred)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_client *clp = server->nfs_client;
	const struct nfs4_mig_recovery_ops *ops =
					clp->cl_mvops->mig_recovery_ops;
	struct nfs4_exception exception = {
		.interruptible = true,
	};
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

/*
 * If 'use_integrity' is true and the state managment nfs_client
 * cl_rpcclient is using krb5i/p, use the integrity protected cl_rpcclient
 * and the machine credential as per RFC3530bis and RFC5661 Security
 * Considerations sections. Otherwise, just use the user cred with the
 * filesystem's rpc_client.
 */
static int _nfs4_proc_secinfo(struct inode *dir, const struct qstr *name, struct nfs4_secinfo_flavors *flavors, bool use_integrity)
{
	int status;
	struct rpc_clnt *clnt = NFS_SERVER(dir)->client;
	struct nfs_client *clp = NFS_SERVER(dir)->nfs_client;
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
	struct nfs4_call_sync_data data = {
		.seq_server = NFS_SERVER(dir),
		.seq_args = &args.seq_args,
		.seq_res = &res.seq_res,
	};
	struct rpc_task_setup task_setup = {
		.rpc_client = clnt,
		.rpc_message = &msg,
		.callback_ops = clp->cl_mvops->call_sync_ops,
		.callback_data = &data,
		.flags = RPC_TASK_NO_ROUND_ROBIN,
	};
	const struct cred *cred = NULL;

	if (use_integrity) {
		clnt = clp->cl_rpcclient;
		task_setup.rpc_client = clnt;

		cred = nfs4_get_clid_cred(clp);
		msg.rpc_cred = cred;
	}

	dprintk("NFS call  secinfo %s\n", name->name);

	nfs4_state_protect(clp, NFS_SP4_MACH_CRED_SECINFO, &clnt, &msg);
	nfs4_init_sequence(&args.seq_args, &res.seq_res, 0, 0);
	status = nfs4_call_sync_custom(&task_setup);

	dprintk("NFS reply  secinfo: %d\n", status);

	put_cred(cred);
	return status;
}

int nfs4_proc_secinfo(struct inode *dir, const struct qstr *name,
		      struct nfs4_secinfo_flavors *flavors)
{
	struct nfs4_exception exception = {
		.interruptible = true,
	};
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
static int nfs4_check_cl_exchange_flags(u32 flags, u32 version)
{
	if (version >= 2 && (flags & ~EXCHGID4_2_FLAG_MASK_R))
		goto out_inval;
	else if (version < 2 && (flags & ~EXCHGID4_FLAG_MASK_R))
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
	if (a->server_scope_sz != b->server_scope_sz)
		return false;
	return memcmp(a->server_scope, b->server_scope, a->server_scope_sz) == 0;
}

static void
nfs4_bind_one_conn_to_session_done(struct rpc_task *task, void *calldata)
{
	struct nfs41_bind_conn_to_session_args *args = task->tk_msg.rpc_argp;
	struct nfs41_bind_conn_to_session_res *res = task->tk_msg.rpc_resp;
	struct nfs_client *clp = args->client;

	switch (task->tk_status) {
	case -NFS4ERR_BADSESSION:
	case -NFS4ERR_DEADSESSION:
		nfs4_schedule_session_recovery(clp->cl_session,
				task->tk_status);
		return;
	}
	if (args->dir == NFS4_CDFC4_FORE_OR_BOTH &&
			res->dir != NFS4_CDFS4_BOTH) {
		rpc_task_close_connection(task);
		if (args->retries++ < MAX_BIND_CONN_TO_SESSION_RETRIES)
			rpc_restart_call(task);
	}
}

static const struct rpc_call_ops nfs4_bind_one_conn_to_session_ops = {
	.rpc_call_done =  nfs4_bind_one_conn_to_session_done,
};

/*
 * nfs4_proc_bind_one_conn_to_session()
 *
 * The 4.1 client currently uses the same TCP connection for the
 * fore and backchannel.
 */
static
int nfs4_proc_bind_one_conn_to_session(struct rpc_clnt *clnt,
		struct rpc_xprt *xprt,
		struct nfs_client *clp,
		const struct cred *cred)
{
	int status;
	struct nfs41_bind_conn_to_session_args args = {
		.client = clp,
		.dir = NFS4_CDFC4_FORE_OR_BOTH,
		.retries = 0,
	};
	struct nfs41_bind_conn_to_session_res res;
	struct rpc_message msg = {
		.rpc_proc =
			&nfs4_procedures[NFSPROC4_CLNT_BIND_CONN_TO_SESSION],
		.rpc_argp = &args,
		.rpc_resp = &res,
		.rpc_cred = cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = clnt,
		.rpc_xprt = xprt,
		.callback_ops = &nfs4_bind_one_conn_to_session_ops,
		.rpc_message = &msg,
		.flags = RPC_TASK_TIMEOUT,
	};
	struct rpc_task *task;

	nfs4_copy_sessionid(&args.sessionid, &clp->cl_session->sess_id);
	if (!(clp->cl_session->flags & SESSION4_BACK_CHAN))
		args.dir = NFS4_CDFC4_FORE;

	/* Do not set the backchannel flag unless this is clnt->cl_xprt */
	if (xprt != rcu_access_pointer(clnt->cl_xprt))
		args.dir = NFS4_CDFC4_FORE;

	task = rpc_run_task(&task_setup_data);
	if (!IS_ERR(task)) {
		status = task->tk_status;
		rpc_put_task(task);
	} else
		status = PTR_ERR(task);
	trace_nfs4_bind_conn_to_session(clp, status);
	if (status == 0) {
		if (memcmp(res.sessionid.data,
		    clp->cl_session->sess_id.data, NFS4_MAX_SESSIONID_LEN)) {
			dprintk("NFS: %s: Session ID mismatch\n", __func__);
			return -EIO;
		}
		if ((res.dir & args.dir) != res.dir || res.dir == 0) {
			dprintk("NFS: %s: Unexpected direction from server\n",
				__func__);
			return -EIO;
		}
		if (res.use_conn_in_rdma_mode != args.use_conn_in_rdma_mode) {
			dprintk("NFS: %s: Server returned RDMA mode = true\n",
				__func__);
			return -EIO;
		}
	}

	return status;
}

struct rpc_bind_conn_calldata {
	struct nfs_client *clp;
	const struct cred *cred;
};

static int
nfs4_proc_bind_conn_to_session_callback(struct rpc_clnt *clnt,
		struct rpc_xprt *xprt,
		void *calldata)
{
	struct rpc_bind_conn_calldata *p = calldata;

	return nfs4_proc_bind_one_conn_to_session(clnt, xprt, p->clp, p->cred);
}

int nfs4_proc_bind_conn_to_session(struct nfs_client *clp, const struct cred *cred)
{
	struct rpc_bind_conn_calldata data = {
		.clp = clp,
		.cred = cred,
	};
	return rpc_clnt_iterate_for_each_xprt(clp->cl_rpcclient,
			nfs4_proc_bind_conn_to_session_callback, &data);
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
		      1 << (OP_OPEN_DOWNGRADE) |
		      1 << (OP_LOCKU) |
		      1 << (OP_DELEGRETURN) |
		      1 << (OP_COMMIT),
		[1] = 1 << (OP_SECINFO - 32) |
		      1 << (OP_SECINFO_NO_NAME - 32) |
		      1 << (OP_LAYOUTRETURN - 32) |
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
	unsigned long flags = 0;
	unsigned int i;
	int ret = 0;

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
				ret = -EINVAL;
				goto out;
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
			__set_bit(NFS_SP4_MACH_CRED_MINIMAL, &flags);
		} else {
			dfprintk(MOUNT, "sp4_mach_cred: disabled\n");
			ret = -EINVAL;
			goto out;
		}

		if (test_bit(OP_CLOSE, sp->allow.u.longs) &&
		    test_bit(OP_OPEN_DOWNGRADE, sp->allow.u.longs) &&
		    test_bit(OP_DELEGRETURN, sp->allow.u.longs) &&
		    test_bit(OP_LOCKU, sp->allow.u.longs)) {
			dfprintk(MOUNT, "  cleanup mode enabled\n");
			__set_bit(NFS_SP4_MACH_CRED_CLEANUP, &flags);
		}

		if (test_bit(OP_LAYOUTRETURN, sp->allow.u.longs)) {
			dfprintk(MOUNT, "  pnfs cleanup mode enabled\n");
			__set_bit(NFS_SP4_MACH_CRED_PNFS_CLEANUP, &flags);
		}

		if (test_bit(OP_SECINFO, sp->allow.u.longs) &&
		    test_bit(OP_SECINFO_NO_NAME, sp->allow.u.longs)) {
			dfprintk(MOUNT, "  secinfo mode enabled\n");
			__set_bit(NFS_SP4_MACH_CRED_SECINFO, &flags);
		}

		if (test_bit(OP_TEST_STATEID, sp->allow.u.longs) &&
		    test_bit(OP_FREE_STATEID, sp->allow.u.longs)) {
			dfprintk(MOUNT, "  stateid mode enabled\n");
			__set_bit(NFS_SP4_MACH_CRED_STATEID, &flags);
		}

		if (test_bit(OP_WRITE, sp->allow.u.longs)) {
			dfprintk(MOUNT, "  write mode enabled\n");
			__set_bit(NFS_SP4_MACH_CRED_WRITE, &flags);
		}

		if (test_bit(OP_COMMIT, sp->allow.u.longs)) {
			dfprintk(MOUNT, "  commit mode enabled\n");
			__set_bit(NFS_SP4_MACH_CRED_COMMIT, &flags);
		}
	}
out:
	clp->cl_sp4_flags = flags;
	return ret;
}

struct nfs41_exchange_id_data {
	struct nfs41_exchange_id_res res;
	struct nfs41_exchange_id_args args;
};

static void nfs4_exchange_id_release(void *data)
{
	struct nfs41_exchange_id_data *cdata =
					(struct nfs41_exchange_id_data *)data;

	nfs_put_client(cdata->args.client);
	kfree(cdata->res.impl_id);
	kfree(cdata->res.server_scope);
	kfree(cdata->res.server_owner);
	kfree(cdata);
}

static const struct rpc_call_ops nfs4_exchange_id_call_ops = {
	.rpc_release = nfs4_exchange_id_release,
};

/*
 * _nfs4_proc_exchange_id()
 *
 * Wrapper for EXCHANGE_ID operation.
 */
static struct rpc_task *
nfs4_run_exchange_id(struct nfs_client *clp, const struct cred *cred,
			u32 sp4_how, struct rpc_xprt *xprt)
{
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_EXCHANGE_ID],
		.rpc_cred = cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = clp->cl_rpcclient,
		.callback_ops = &nfs4_exchange_id_call_ops,
		.rpc_message = &msg,
		.flags = RPC_TASK_TIMEOUT | RPC_TASK_NO_ROUND_ROBIN,
	};
	struct nfs41_exchange_id_data *calldata;
	int status;

	if (!refcount_inc_not_zero(&clp->cl_count))
		return ERR_PTR(-EIO);

	status = -ENOMEM;
	calldata = kzalloc(sizeof(*calldata), GFP_NOFS);
	if (!calldata)
		goto out;

	nfs4_init_boot_verifier(clp, &calldata->args.verifier);

	status = nfs4_init_uniform_client_string(clp);
	if (status)
		goto out_calldata;

	calldata->res.server_owner = kzalloc(sizeof(struct nfs41_server_owner),
						GFP_NOFS);
	status = -ENOMEM;
	if (unlikely(calldata->res.server_owner == NULL))
		goto out_calldata;

	calldata->res.server_scope = kzalloc(sizeof(struct nfs41_server_scope),
					GFP_NOFS);
	if (unlikely(calldata->res.server_scope == NULL))
		goto out_server_owner;

	calldata->res.impl_id = kzalloc(sizeof(struct nfs41_impl_id), GFP_NOFS);
	if (unlikely(calldata->res.impl_id == NULL))
		goto out_server_scope;

	switch (sp4_how) {
	case SP4_NONE:
		calldata->args.state_protect.how = SP4_NONE;
		break;

	case SP4_MACH_CRED:
		calldata->args.state_protect = nfs4_sp4_mach_cred_request;
		break;

	default:
		/* unsupported! */
		WARN_ON_ONCE(1);
		status = -EINVAL;
		goto out_impl_id;
	}
	if (xprt) {
		task_setup_data.rpc_xprt = xprt;
		task_setup_data.flags |= RPC_TASK_SOFTCONN;
		memcpy(calldata->args.verifier.data, clp->cl_confirm.data,
				sizeof(calldata->args.verifier.data));
	}
	calldata->args.client = clp;
	calldata->args.flags = EXCHGID4_FLAG_SUPP_MOVED_REFER |
	EXCHGID4_FLAG_BIND_PRINC_STATEID;
#ifdef CONFIG_NFS_V4_1_MIGRATION
	calldata->args.flags |= EXCHGID4_FLAG_SUPP_MOVED_MIGR;
#endif
	msg.rpc_argp = &calldata->args;
	msg.rpc_resp = &calldata->res;
	task_setup_data.callback_data = calldata;

	return rpc_run_task(&task_setup_data);

out_impl_id:
	kfree(calldata->res.impl_id);
out_server_scope:
	kfree(calldata->res.server_scope);
out_server_owner:
	kfree(calldata->res.server_owner);
out_calldata:
	kfree(calldata);
out:
	nfs_put_client(clp);
	return ERR_PTR(status);
}

/*
 * _nfs4_proc_exchange_id()
 *
 * Wrapper for EXCHANGE_ID operation.
 */
static int _nfs4_proc_exchange_id(struct nfs_client *clp, const struct cred *cred,
			u32 sp4_how)
{
	struct rpc_task *task;
	struct nfs41_exchange_id_args *argp;
	struct nfs41_exchange_id_res *resp;
	unsigned long now = jiffies;
	int status;

	task = nfs4_run_exchange_id(clp, cred, sp4_how, NULL);
	if (IS_ERR(task))
		return PTR_ERR(task);

	argp = task->tk_msg.rpc_argp;
	resp = task->tk_msg.rpc_resp;
	status = task->tk_status;
	if (status  != 0)
		goto out;

	status = nfs4_check_cl_exchange_flags(resp->flags,
			clp->cl_mvops->minor_version);
	if (status  != 0)
		goto out;

	status = nfs4_sp4_select_mode(clp, &resp->state_protect);
	if (status != 0)
		goto out;

	do_renew_lease(clp, now);

	clp->cl_clientid = resp->clientid;
	clp->cl_exchange_flags = resp->flags;
	clp->cl_seqid = resp->seqid;
	/* Client ID is not confirmed */
	if (!(resp->flags & EXCHGID4_FLAG_CONFIRMED_R))
		clear_bit(NFS4_SESSION_ESTABLISHED,
			  &clp->cl_session->session_state);

	if (clp->cl_serverscope != NULL &&
	    !nfs41_same_server_scope(clp->cl_serverscope,
				resp->server_scope)) {
		dprintk("%s: server_scope mismatch detected\n",
			__func__);
		set_bit(NFS4CLNT_SERVER_SCOPE_MISMATCH, &clp->cl_state);
	}

	swap(clp->cl_serverowner, resp->server_owner);
	swap(clp->cl_serverscope, resp->server_scope);
	swap(clp->cl_implid, resp->impl_id);

	/* Save the EXCHANGE_ID verifier session trunk tests */
	memcpy(clp->cl_confirm.data, argp->verifier.data,
	       sizeof(clp->cl_confirm.data));
out:
	trace_nfs4_exchange_id(clp, status);
	rpc_put_task(task);
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
int nfs4_proc_exchange_id(struct nfs_client *clp, const struct cred *cred)
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

/**
 * nfs4_test_session_trunk
 *
 * This is an add_xprt_test() test function called from
 * rpc_clnt_setup_test_and_add_xprt.
 *
 * The rpc_xprt_switch is referrenced by rpc_clnt_setup_test_and_add_xprt
 * and is dereferrenced in nfs4_exchange_id_release
 *
 * Upon success, add the new transport to the rpc_clnt
 *
 * @clnt: struct rpc_clnt to get new transport
 * @xprt: the rpc_xprt to test
 * @data: call data for _nfs4_proc_exchange_id.
 */
void nfs4_test_session_trunk(struct rpc_clnt *clnt, struct rpc_xprt *xprt,
			    void *data)
{
	struct nfs4_add_xprt_data *adata = (struct nfs4_add_xprt_data *)data;
	struct rpc_task *task;
	int status;

	u32 sp4_how;

	dprintk("--> %s try %s\n", __func__,
		xprt->address_strings[RPC_DISPLAY_ADDR]);

	sp4_how = (adata->clp->cl_sp4_flags == 0 ? SP4_NONE : SP4_MACH_CRED);

	/* Test connection for session trunking. Async exchange_id call */
	task = nfs4_run_exchange_id(adata->clp, adata->cred, sp4_how, xprt);
	if (IS_ERR(task))
		return;

	status = task->tk_status;
	if (status == 0)
		status = nfs4_detect_session_trunking(adata->clp,
				task->tk_msg.rpc_resp, xprt);

	if (status == 0)
		rpc_clnt_xprt_switch_add_xprt(clnt, xprt);

	rpc_put_task(task);
}
EXPORT_SYMBOL_GPL(nfs4_test_session_trunk);

static int _nfs4_proc_destroy_clientid(struct nfs_client *clp,
		const struct cred *cred)
{
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_DESTROY_CLIENTID],
		.rpc_argp = clp,
		.rpc_cred = cred,
	};
	int status;

	status = rpc_call_sync(clp->cl_rpcclient, &msg,
			       RPC_TASK_TIMEOUT | RPC_TASK_NO_ROUND_ROBIN);
	trace_nfs4_destroy_clientid(clp, status);
	if (status)
		dprintk("NFS: Got error %d from the server %s on "
			"DESTROY_CLIENTID.", status, clp->cl_hostname);
	return status;
}

static int nfs4_proc_destroy_clientid(struct nfs_client *clp,
		const struct cred *cred)
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
	const struct cred *cred;
	int ret = 0;

	if (clp->cl_mvops->minor_version < 1)
		goto out;
	if (clp->cl_exchange_flags == 0)
		goto out;
	if (clp->cl_preserve_clid)
		goto out;
	cred = nfs4_get_clid_cred(clp);
	ret = nfs4_proc_destroy_clientid(clp, cred);
	put_cred(cred);
	switch (ret) {
	case 0:
	case -NFS4ERR_STALE_CLIENTID:
		clp->cl_exchange_flags = 0;
	}
out:
	return ret;
}

#endif /* CONFIG_NFS_V4_1 */

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
	nfs4_setup_sequence(data->clp,
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
	if (!nfs4_sequence_done(task, &data->res->lr_seq_res))
		return;
	switch (task->tk_status) {
	case -NFS4ERR_DELAY:
	case -NFS4ERR_GRACE:
		dprintk("%s Retry: tk_status %d\n", __func__, task->tk_status);
		rpc_delay(task, NFS4_POLL_RETRY_MIN);
		task->tk_status = 0;
		fallthrough;
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

	nfs4_init_sequence(&args.la_seq_args, &res.lr_seq_res, 0, 1);
	return nfs4_call_sync_custom(&task_setup);
}

#ifdef CONFIG_NFS_V4_1

/*
 * Initialize the values to be used by the client in CREATE_SESSION
 * If nfs4_init_session set the fore channel request and response sizes,
 * use them.
 *
 * Set the back channel max_resp_sz_cached to zero to force the client to
 * always set csa_cachethis to FALSE because the current implementation
 * of the back channel DRC only supports caching the CB_SEQUENCE operation.
 */
static void nfs4_init_channel_attrs(struct nfs41_create_session_args *args,
				    struct rpc_clnt *clnt)
{
	unsigned int max_rqst_sz, max_resp_sz;
	unsigned int max_bc_payload = rpc_max_bc_payload(clnt);
	unsigned int max_bc_slots = rpc_num_bc_slots(clnt);

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
	args->bc_attrs.max_rqst_sz = max_bc_payload;
	args->bc_attrs.max_resp_sz = max_bc_payload;
	args->bc_attrs.max_resp_sz_cached = 0;
	args->bc_attrs.max_ops = NFS4_MAX_BACK_CHANNEL_OPS;
	args->bc_attrs.max_reqs = max_t(unsigned short, max_session_cb_slots, 1);
	if (args->bc_attrs.max_reqs > max_bc_slots)
		args->bc_attrs.max_reqs = max_bc_slots;

	dprintk("%s: Back Channel : max_rqst_sz=%u max_resp_sz=%u "
		"max_resp_sz_cached=%u max_ops=%u max_reqs=%u\n",
		__func__,
		args->bc_attrs.max_rqst_sz, args->bc_attrs.max_resp_sz,
		args->bc_attrs.max_resp_sz_cached, args->bc_attrs.max_ops,
		args->bc_attrs.max_reqs);
}

static int nfs4_verify_fore_channel_attrs(struct nfs41_create_session_args *args,
		struct nfs41_create_session_res *res)
{
	struct nfs4_channel_attrs *sent = &args->fc_attrs;
	struct nfs4_channel_attrs *rcvd = &res->fc_attrs;

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

static int nfs4_verify_back_channel_attrs(struct nfs41_create_session_args *args,
		struct nfs41_create_session_res *res)
{
	struct nfs4_channel_attrs *sent = &args->bc_attrs;
	struct nfs4_channel_attrs *rcvd = &res->bc_attrs;

	if (!(res->flags & SESSION4_BACK_CHAN))
		goto out;
	if (rcvd->max_rqst_sz > sent->max_rqst_sz)
		return -EINVAL;
	if (rcvd->max_resp_sz < sent->max_resp_sz)
		return -EINVAL;
	if (rcvd->max_resp_sz_cached > sent->max_resp_sz_cached)
		return -EINVAL;
	if (rcvd->max_ops > sent->max_ops)
		return -EINVAL;
	if (rcvd->max_reqs > sent->max_reqs)
		return -EINVAL;
out:
	return 0;
}

static int nfs4_verify_channel_attrs(struct nfs41_create_session_args *args,
				     struct nfs41_create_session_res *res)
{
	int ret;

	ret = nfs4_verify_fore_channel_attrs(args, res);
	if (ret)
		return ret;
	return nfs4_verify_back_channel_attrs(args, res);
}

static void nfs4_update_session(struct nfs4_session *session,
		struct nfs41_create_session_res *res)
{
	nfs4_copy_sessionid(&session->sess_id, &res->sessionid);
	/* Mark client id and session as being confirmed */
	session->clp->cl_exchange_flags |= EXCHGID4_FLAG_CONFIRMED_R;
	set_bit(NFS4_SESSION_ESTABLISHED, &session->session_state);
	session->flags = res->flags;
	memcpy(&session->fc_attrs, &res->fc_attrs, sizeof(session->fc_attrs));
	if (res->flags & SESSION4_BACK_CHAN)
		memcpy(&session->bc_attrs, &res->bc_attrs,
				sizeof(session->bc_attrs));
}

static int _nfs4_proc_create_session(struct nfs_client *clp,
		const struct cred *cred)
{
	struct nfs4_session *session = clp->cl_session;
	struct nfs41_create_session_args args = {
		.client = clp,
		.clientid = clp->cl_clientid,
		.seqid = clp->cl_seqid,
		.cb_program = NFS4_CALLBACK,
	};
	struct nfs41_create_session_res res;

	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_CREATE_SESSION],
		.rpc_argp = &args,
		.rpc_resp = &res,
		.rpc_cred = cred,
	};
	int status;

	nfs4_init_channel_attrs(&args, clp->cl_rpcclient);
	args.flags = (SESSION4_PERSIST | SESSION4_BACK_CHAN);

	status = rpc_call_sync(session->clp->cl_rpcclient, &msg,
			       RPC_TASK_TIMEOUT | RPC_TASK_NO_ROUND_ROBIN);
	trace_nfs4_create_session(clp, status);

	switch (status) {
	case -NFS4ERR_STALE_CLIENTID:
	case -NFS4ERR_DELAY:
	case -ETIMEDOUT:
	case -EACCES:
	case -EAGAIN:
		goto out;
	}

	clp->cl_seqid++;
	if (!status) {
		/* Verify the session's negotiated channel_attrs values */
		status = nfs4_verify_channel_attrs(&args, &res);
		/* Increment the clientid slot sequence id */
		if (status)
			goto out;
		nfs4_update_session(session, &res);
	}
out:
	return status;
}

/*
 * Issues a CREATE_SESSION operation to the server.
 * It is the responsibility of the caller to verify the session is
 * expired before calling this routine.
 */
int nfs4_proc_create_session(struct nfs_client *clp, const struct cred *cred)
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
		const struct cred *cred)
{
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_DESTROY_SESSION],
		.rpc_argp = session,
		.rpc_cred = cred,
	};
	int status = 0;

	dprintk("--> nfs4_proc_destroy_session\n");

	/* session is still being setup */
	if (!test_and_clear_bit(NFS4_SESSION_ESTABLISHED, &session->session_state))
		return 0;

	status = rpc_call_sync(session->clp->cl_rpcclient, &msg,
			       RPC_TASK_TIMEOUT | RPC_TASK_NO_ROUND_ROBIN);
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

	if (refcount_read(&clp->cl_count) > 1)
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
		if (refcount_read(&clp->cl_count) == 1)
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

	nfs4_setup_sequence(clp, args, res, task);
}

static const struct rpc_call_ops nfs41_sequence_ops = {
	.rpc_call_done = nfs41_sequence_call_done,
	.rpc_call_prepare = nfs41_sequence_prepare,
	.rpc_release = nfs41_sequence_release,
};

static struct rpc_task *_nfs41_proc_sequence(struct nfs_client *clp,
		const struct cred *cred,
		struct nfs4_slot *slot,
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
	struct rpc_task *ret;

	ret = ERR_PTR(-EIO);
	if (!refcount_inc_not_zero(&clp->cl_count))
		goto out_err;

	ret = ERR_PTR(-ENOMEM);
	calldata = kzalloc(sizeof(*calldata), GFP_NOFS);
	if (calldata == NULL)
		goto out_put_clp;
	nfs4_init_sequence(&calldata->args, &calldata->res, 0, is_privileged);
	nfs4_sequence_attach_slot(&calldata->args, &calldata->res, slot);
	msg.rpc_argp = &calldata->args;
	msg.rpc_resp = &calldata->res;
	calldata->clp = clp;
	task_setup_data.callback_data = calldata;

	ret = rpc_run_task(&task_setup_data);
	if (IS_ERR(ret))
		goto out_err;
	return ret;
out_put_clp:
	nfs_put_client(clp);
out_err:
	nfs41_release_slot(slot);
	return ret;
}

static int nfs41_proc_async_sequence(struct nfs_client *clp, const struct cred *cred, unsigned renew_flags)
{
	struct rpc_task *task;
	int ret = 0;

	if ((renew_flags & NFS4_RENEW_TIMEOUT) == 0)
		return -EAGAIN;
	task = _nfs41_proc_sequence(clp, cred, NULL, false);
	if (IS_ERR(task))
		ret = PTR_ERR(task);
	else
		rpc_put_task_async(task);
	dprintk("<-- %s status=%d\n", __func__, ret);
	return ret;
}

static int nfs4_proc_sequence(struct nfs_client *clp, const struct cred *cred)
{
	struct rpc_task *task;
	int ret;

	task = _nfs41_proc_sequence(clp, cred, NULL, true);
	if (IS_ERR(task)) {
		ret = PTR_ERR(task);
		goto out;
	}
	ret = rpc_wait_for_completion_task(task);
	if (!ret)
		ret = task->tk_status;
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

	nfs4_setup_sequence(calldata->clp,
			&calldata->arg.seq_args,
			&calldata->res.seq_res,
			task);
}

static int nfs41_reclaim_complete_handle_errors(struct rpc_task *task, struct nfs_client *clp)
{
	switch(task->tk_status) {
	case 0:
		wake_up_all(&clp->cl_lock_waitq);
		fallthrough;
	case -NFS4ERR_COMPLETE_ALREADY:
	case -NFS4ERR_WRONG_CRED: /* What to do here? */
		break;
	case -NFS4ERR_DELAY:
		rpc_delay(task, NFS4_POLL_RETRY_MAX);
		fallthrough;
	case -NFS4ERR_RETRY_UNCACHED_REP:
		return -EAGAIN;
	case -NFS4ERR_BADSESSION:
	case -NFS4ERR_DEADSESSION:
	case -NFS4ERR_CONN_NOT_BOUND_TO_SESSION:
		break;
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
		const struct cred *cred)
{
	struct nfs4_reclaim_complete_data *calldata;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_RECLAIM_COMPLETE],
		.rpc_cred = cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = clp->cl_rpcclient,
		.rpc_message = &msg,
		.callback_ops = &nfs4_reclaim_complete_call_ops,
		.flags = RPC_TASK_NO_ROUND_ROBIN,
	};
	int status = -ENOMEM;

	dprintk("--> %s\n", __func__);
	calldata = kzalloc(sizeof(*calldata), GFP_NOFS);
	if (calldata == NULL)
		goto out;
	calldata->clp = clp;
	calldata->arg.one_fs = 0;

	nfs4_init_sequence(&calldata->arg.seq_args, &calldata->res.seq_res, 0, 1);
	msg.rpc_argp = &calldata->arg;
	msg.rpc_resp = &calldata->res;
	task_setup_data.callback_data = calldata;
	status = nfs4_call_sync_custom(&task_setup_data);
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
	nfs4_setup_sequence(server->nfs_client, &lgp->args.seq_args,
				&lgp->res.seq_res, task);
	dprintk("<-- %s\n", __func__);
}

static void nfs4_layoutget_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_layoutget *lgp = calldata;

	dprintk("--> %s\n", __func__);
	nfs41_sequence_process(task, &lgp->res.seq_res);
	dprintk("<-- %s\n", __func__);
}

static int
nfs4_layoutget_handle_exception(struct rpc_task *task,
		struct nfs4_layoutget *lgp, struct nfs4_exception *exception)
{
	struct inode *inode = lgp->args.inode;
	struct nfs_server *server = NFS_SERVER(inode);
	struct pnfs_layout_hdr *lo;
	int nfs4err = task->tk_status;
	int err, status = 0;
	LIST_HEAD(head);

	dprintk("--> %s tk_status => %d\n", __func__, -task->tk_status);

	nfs4_sequence_free_slot(&lgp->res.seq_res);

	switch (nfs4err) {
	case 0:
		goto out;

	/*
	 * NFS4ERR_LAYOUTUNAVAILABLE means we are not supposed to use pnfs
	 * on the file. set tk_status to -ENODATA to tell upper layer to
	 * retry go inband.
	 */
	case -NFS4ERR_LAYOUTUNAVAILABLE:
		status = -ENODATA;
		goto out;
	/*
	 * NFS4ERR_BADLAYOUT means the MDS cannot return a layout of
	 * length lgp->args.minlength != 0 (see RFC5661 section 18.43.3).
	 */
	case -NFS4ERR_BADLAYOUT:
		status = -EOVERFLOW;
		goto out;
	/*
	 * NFS4ERR_LAYOUTTRYLATER is a conflict with another client
	 * (or clients) writing to the same RAID stripe except when
	 * the minlength argument is 0 (see RFC5661 section 18.43.3).
	 *
	 * Treat it like we would RECALLCONFLICT -- we retry for a little
	 * while, and then eventually give up.
	 */
	case -NFS4ERR_LAYOUTTRYLATER:
		if (lgp->args.minlength == 0) {
			status = -EOVERFLOW;
			goto out;
		}
		status = -EBUSY;
		break;
	case -NFS4ERR_RECALLCONFLICT:
		status = -ERECALLCONFLICT;
		break;
	case -NFS4ERR_DELEG_REVOKED:
	case -NFS4ERR_ADMIN_REVOKED:
	case -NFS4ERR_EXPIRED:
	case -NFS4ERR_BAD_STATEID:
		exception->timeout = 0;
		spin_lock(&inode->i_lock);
		lo = NFS_I(inode)->layout;
		/* If the open stateid was bad, then recover it. */
		if (!lo || test_bit(NFS_LAYOUT_INVALID_STID, &lo->plh_flags) ||
		    !nfs4_stateid_match_other(&lgp->args.stateid, &lo->plh_stateid)) {
			spin_unlock(&inode->i_lock);
			exception->state = lgp->args.ctx->state;
			exception->stateid = &lgp->args.stateid;
			break;
		}

		/*
		 * Mark the bad layout state as invalid, then retry
		 */
		pnfs_mark_layout_stateid_invalid(lo, &head);
		spin_unlock(&inode->i_lock);
		nfs_commit_inode(inode, 0);
		pnfs_free_lseg_list(&head);
		status = -EAGAIN;
		goto out;
	}

	err = nfs4_handle_exception(server, nfs4err, exception);
	if (!status) {
		if (exception->retry)
			status = -EAGAIN;
		else
			status = err;
	}
out:
	dprintk("<-- %s\n", __func__);
	return status;
}

size_t max_response_pages(struct nfs_server *server)
{
	u32 max_resp_sz = server->nfs_client->cl_session->fc_attrs.max_resp_sz;
	return nfs_page_array_len(0, max_resp_sz);
}

static void nfs4_layoutget_release(void *calldata)
{
	struct nfs4_layoutget *lgp = calldata;

	dprintk("--> %s\n", __func__);
	nfs4_sequence_free_slot(&lgp->res.seq_res);
	pnfs_layoutget_free(lgp);
	dprintk("<-- %s\n", __func__);
}

static const struct rpc_call_ops nfs4_layoutget_call_ops = {
	.rpc_call_prepare = nfs4_layoutget_prepare,
	.rpc_call_done = nfs4_layoutget_done,
	.rpc_release = nfs4_layoutget_release,
};

struct pnfs_layout_segment *
nfs4_proc_layoutget(struct nfs4_layoutget *lgp, long *timeout)
{
	struct inode *inode = lgp->args.inode;
	struct nfs_server *server = NFS_SERVER(inode);
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
		.flags = RPC_TASK_ASYNC | RPC_TASK_CRED_NOREF,
	};
	struct pnfs_layout_segment *lseg = NULL;
	struct nfs4_exception exception = {
		.inode = inode,
		.timeout = *timeout,
	};
	int status = 0;

	dprintk("--> %s\n", __func__);

	/* nfs4_layoutget_release calls pnfs_put_layout_hdr */
	pnfs_get_layout_hdr(NFS_I(inode)->layout);

	nfs4_init_sequence(&lgp->args.seq_args, &lgp->res.seq_res, 0, 0);

	task = rpc_run_task(&task_setup_data);

	status = rpc_wait_for_completion_task(task);
	if (status != 0)
		goto out;

	if (task->tk_status < 0) {
		status = nfs4_layoutget_handle_exception(task, lgp, &exception);
		*timeout = exception.timeout;
	} else if (lgp->res.layoutp->len == 0) {
		status = -EAGAIN;
		*timeout = nfs4_update_delay(&exception.timeout);
	} else
		lseg = pnfs_layout_process(lgp);
out:
	trace_nfs4_layoutget(lgp->args.ctx,
			&lgp->args.range,
			&lgp->res.range,
			&lgp->res.stateid,
			status);

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
	nfs4_setup_sequence(lrp->clp,
			&lrp->args.seq_args,
			&lrp->res.seq_res,
			task);
	if (!pnfs_layout_is_valid(lrp->args.layout))
		rpc_exit(task, 0);
}

static void nfs4_layoutreturn_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_layoutreturn *lrp = calldata;
	struct nfs_server *server;

	dprintk("--> %s\n", __func__);

	if (!nfs41_sequence_process(task, &lrp->res.seq_res))
		return;

	/*
	 * Was there an RPC level error? Assume the call succeeded,
	 * and that we need to release the layout
	 */
	if (task->tk_rpc_status != 0 && RPC_WAS_SENT(task)) {
		lrp->res.lrs_present = 0;
		return;
	}

	server = NFS_SERVER(lrp->args.inode);
	switch (task->tk_status) {
	case -NFS4ERR_OLD_STATEID:
		if (nfs4_layout_refresh_old_stateid(&lrp->args.stateid,
					&lrp->args.range,
					lrp->args.inode))
			goto out_restart;
		fallthrough;
	default:
		task->tk_status = 0;
		fallthrough;
	case 0:
		break;
	case -NFS4ERR_DELAY:
		if (nfs4_async_handle_error(task, server, NULL, NULL) != -EAGAIN)
			break;
		goto out_restart;
	}
	dprintk("<-- %s\n", __func__);
	return;
out_restart:
	task->tk_status = 0;
	nfs4_sequence_free_slot(&lrp->res.seq_res);
	rpc_restart_call_prepare(task);
}

static void nfs4_layoutreturn_release(void *calldata)
{
	struct nfs4_layoutreturn *lrp = calldata;
	struct pnfs_layout_hdr *lo = lrp->args.layout;

	dprintk("--> %s\n", __func__);
	pnfs_layoutreturn_free_lsegs(lo, &lrp->args.stateid, &lrp->args.range,
			lrp->res.lrs_present ? &lrp->res.stateid : NULL);
	nfs4_sequence_free_slot(&lrp->res.seq_res);
	if (lrp->ld_private.ops && lrp->ld_private.ops->free)
		lrp->ld_private.ops->free(&lrp->ld_private);
	pnfs_put_layout_hdr(lrp->args.layout);
	nfs_iput_and_deactive(lrp->inode);
	put_cred(lrp->cred);
	kfree(calldata);
	dprintk("<-- %s\n", __func__);
}

static const struct rpc_call_ops nfs4_layoutreturn_call_ops = {
	.rpc_call_prepare = nfs4_layoutreturn_prepare,
	.rpc_call_done = nfs4_layoutreturn_done,
	.rpc_release = nfs4_layoutreturn_release,
};

int nfs4_proc_layoutreturn(struct nfs4_layoutreturn *lrp, bool sync)
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
	int status = 0;

	nfs4_state_protect(NFS_SERVER(lrp->args.inode)->nfs_client,
			NFS_SP4_MACH_CRED_PNFS_CLEANUP,
			&task_setup_data.rpc_client, &msg);

	dprintk("--> %s\n", __func__);
	lrp->inode = nfs_igrab_and_active(lrp->args.inode);
	if (!sync) {
		if (!lrp->inode) {
			nfs4_layoutreturn_release(lrp);
			return -EAGAIN;
		}
		task_setup_data.flags |= RPC_TASK_ASYNC;
	}
	if (!lrp->inode)
		nfs4_init_sequence(&lrp->args.seq_args, &lrp->res.seq_res, 1,
				   1);
	else
		nfs4_init_sequence(&lrp->args.seq_args, &lrp->res.seq_res, 1,
				   0);
	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	if (sync)
		status = task->tk_status;
	trace_nfs4_layoutreturn(lrp->args.inode, &lrp->args.stateid, status);
	dprintk("<-- %s status=%d\n", __func__, status);
	rpc_put_task(task);
	return status;
}

static int
_nfs4_proc_getdeviceinfo(struct nfs_server *server,
		struct pnfs_device *pdev,
		const struct cred *cred)
{
	struct nfs4_getdeviceinfo_args args = {
		.pdev = pdev,
		.notify_types = NOTIFY_DEVICEID4_CHANGE |
			NOTIFY_DEVICEID4_DELETE,
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
	if (res.notification & ~args.notify_types)
		dprintk("%s: unsupported notification\n", __func__);
	if (res.notification != args.notify_types)
		pdev->nocache = 1;

	dprintk("<-- %s status=%d\n", __func__, status);

	return status;
}

int nfs4_proc_getdeviceinfo(struct nfs_server *server,
		struct pnfs_device *pdev,
		const struct cred *cred)
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

	nfs4_setup_sequence(server->nfs_client,
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
	put_cred(data->cred);
	nfs_iput_and_deactive(data->inode);
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
	};
	struct rpc_task *task;
	int status = 0;

	dprintk("NFS: initiating layoutcommit call. sync %d "
		"lbw: %llu inode %lu\n", sync,
		data->args.lastbytewritten,
		data->args.inode->i_ino);

	if (!sync) {
		data->inode = nfs_igrab_and_active(data->args.inode);
		if (data->inode == NULL) {
			nfs4_layoutcommit_release(data);
			return -EAGAIN;
		}
		task_setup_data.flags = RPC_TASK_ASYNC;
	}
	nfs4_init_sequence(&data->args.seq_args, &data->res.seq_res, 1, 0);
	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	if (sync)
		status = task->tk_status;
	trace_nfs4_layoutcommit(data->args.inode, &data->args.stateid, status);
	dprintk("%s: status %d\n", __func__, status);
	rpc_put_task(task);
	return status;
}

/*
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
	struct nfs4_call_sync_data data = {
		.seq_server = server,
		.seq_args = &args.seq_args,
		.seq_res = &res.seq_res,
	};
	struct rpc_task_setup task_setup = {
		.rpc_client = server->client,
		.rpc_message = &msg,
		.callback_ops = server->nfs_client->cl_mvops->call_sync_ops,
		.callback_data = &data,
		.flags = RPC_TASK_NO_ROUND_ROBIN,
	};
	const struct cred *cred = NULL;
	int status;

	if (use_integrity) {
		task_setup.rpc_client = server->nfs_client->cl_rpcclient;

		cred = nfs4_get_clid_cred(server->nfs_client);
		msg.rpc_cred = cred;
	}

	dprintk("--> %s\n", __func__);
	nfs4_init_sequence(&args.seq_args, &res.seq_res, 0, 0);
	status = nfs4_call_sync_custom(&task_setup);
	dprintk("<-- %s status=%d\n", __func__, status);

	put_cred(cred);

	return status;
}

static int
nfs41_proc_secinfo_no_name(struct nfs_server *server, struct nfs_fh *fhandle,
			   struct nfs_fsinfo *info, struct nfs4_secinfo_flavors *flavors)
{
	struct nfs4_exception exception = {
		.interruptible = true,
	};
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
		const struct cred *cred)
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
	nfs4_init_sequence(&args.seq_args, &res.seq_res, 0, 1);
	status = nfs4_call_sync_sequence(rpc_client, server, &msg,
			&args.seq_args, &res.seq_res);
	if (status != NFS_OK) {
		dprintk("NFS reply test_stateid: failed, %d\n", status);
		return status;
	}
	dprintk("NFS reply test_stateid: succeeded, %d\n", -res.status);
	return -res.status;
}

static void nfs4_handle_delay_or_session_error(struct nfs_server *server,
		int err, struct nfs4_exception *exception)
{
	exception->retry = 0;
	switch(err) {
	case -NFS4ERR_DELAY:
	case -NFS4ERR_RETRY_UNCACHED_REP:
		nfs4_handle_exception(server, err, exception);
		break;
	case -NFS4ERR_BADSESSION:
	case -NFS4ERR_BADSLOT:
	case -NFS4ERR_BAD_HIGH_SLOT:
	case -NFS4ERR_CONN_NOT_BOUND_TO_SESSION:
	case -NFS4ERR_DEADSESSION:
		nfs4_do_handle_exception(server, err, exception);
	}
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
		const struct cred *cred)
{
	struct nfs4_exception exception = {
		.interruptible = true,
	};
	int err;
	do {
		err = _nfs41_test_stateid(server, stateid, cred);
		nfs4_handle_delay_or_session_error(server, err, &exception);
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
	nfs4_setup_sequence(data->server->nfs_client,
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

/**
 * nfs41_free_stateid - perform a FREE_STATEID operation
 *
 * @server: server / transport on which to perform the operation
 * @stateid: state ID to release
 * @cred: credential
 * @privileged: set to true if this call needs to be privileged
 *
 * Note: this function is always asynchronous.
 */
static int nfs41_free_stateid(struct nfs_server *server,
		const nfs4_stateid *stateid,
		const struct cred *cred,
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
	struct rpc_task *task;

	nfs4_state_protect(server->nfs_client, NFS_SP4_MACH_CRED_STATEID,
		&task_setup.rpc_client, &msg);

	dprintk("NFS call  free_stateid %p\n", stateid);
	data = kmalloc(sizeof(*data), GFP_NOFS);
	if (!data)
		return -ENOMEM;
	data->server = server;
	nfs4_stateid_copy(&data->args.stateid, stateid);

	task_setup.callback_data = data;

	msg.rpc_argp = &data->args;
	msg.rpc_resp = &data->res;
	nfs4_init_sequence(&data->args.seq_args, &data->res.seq_res, 1, privileged);
	task = rpc_run_task(&task_setup);
	if (IS_ERR(task))
		return PTR_ERR(task);
	rpc_put_task(task);
	return 0;
}

static void
nfs41_free_lock_state(struct nfs_server *server, struct nfs4_lock_state *lsp)
{
	const struct cred *cred = lsp->ls_state->owner->so_cred;

	nfs41_free_stateid(server, &lsp->ls_stateid, cred, false);
	nfs4_free_lock_state(server, lsp);
}

static bool nfs41_match_stateid(const nfs4_stateid *s1,
		const nfs4_stateid *s2)
{
	if (s1->type != s2->type)
		return false;

	if (memcmp(s1->other, s2->other, sizeof(s1->other)) != 0)
		return false;

	if (s1->seqid == s2->seqid)
		return true;

	return s1->seqid == 0 || s2->seqid == 0;
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
	.get_state_renewal_cred = nfs4_get_renew_cred,
	.renew_lease = nfs4_proc_renew,
};

#if defined(CONFIG_NFS_V4_1)
static const struct nfs4_state_maintenance_ops nfs41_state_renewal_ops = {
	.sched_state_renewal = nfs41_proc_async_sequence,
	.get_state_renewal_cred = nfs4_get_machine_cred,
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
		| NFS_CAP_POSIX_LOCK,
	.init_client = nfs40_init_client,
	.shutdown_client = nfs40_shutdown_client,
	.match_stateid = nfs4_match_stateid,
	.find_root_sec = nfs4_find_root_sec,
	.free_lock_state = nfs4_release_lockowner,
	.test_and_free_expired = nfs40_test_and_free_expired_stateid,
	.alloc_seqid = nfs_alloc_seqid,
	.call_sync_ops = &nfs40_call_sync_ops,
	.reboot_recovery_ops = &nfs40_reboot_recovery_ops,
	.nograce_recovery_ops = &nfs40_nograce_recovery_ops,
	.state_renewal_ops = &nfs40_state_renewal_ops,
	.mig_recovery_ops = &nfs40_mig_recovery_ops,
};

#if defined(CONFIG_NFS_V4_1)
static struct nfs_seqid *
nfs_alloc_no_seqid(struct nfs_seqid_counter *arg1, gfp_t arg2)
{
	return NULL;
}

static const struct nfs4_minor_version_ops nfs_v4_1_minor_ops = {
	.minor_version = 1,
	.init_caps = NFS_CAP_READDIRPLUS
		| NFS_CAP_ATOMIC_OPEN
		| NFS_CAP_POSIX_LOCK
		| NFS_CAP_STATEID_NFSV41
		| NFS_CAP_ATOMIC_OPEN_V1
		| NFS_CAP_LGOPEN,
	.init_client = nfs41_init_client,
	.shutdown_client = nfs41_shutdown_client,
	.match_stateid = nfs41_match_stateid,
	.find_root_sec = nfs41_find_root_sec,
	.free_lock_state = nfs41_free_lock_state,
	.test_and_free_expired = nfs41_test_and_free_expired_stateid,
	.alloc_seqid = nfs_alloc_no_seqid,
	.session_trunk = nfs4_test_session_trunk,
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
		| NFS_CAP_POSIX_LOCK
		| NFS_CAP_STATEID_NFSV41
		| NFS_CAP_ATOMIC_OPEN_V1
		| NFS_CAP_LGOPEN
		| NFS_CAP_ALLOCATE
		| NFS_CAP_COPY
		| NFS_CAP_OFFLOAD_CANCEL
		| NFS_CAP_COPY_NOTIFY
		| NFS_CAP_DEALLOCATE
		| NFS_CAP_SEEK
		| NFS_CAP_LAYOUTSTATS
		| NFS_CAP_CLONE
		| NFS_CAP_LAYOUTERROR
		| NFS_CAP_READ_PLUS,
	.init_client = nfs41_init_client,
	.shutdown_client = nfs41_shutdown_client,
	.match_stateid = nfs41_match_stateid,
	.find_root_sec = nfs41_find_root_sec,
	.free_lock_state = nfs41_free_lock_state,
	.call_sync_ops = &nfs41_call_sync_ops,
	.test_and_free_expired = nfs41_test_and_free_expired_stateid,
	.alloc_seqid = nfs_alloc_no_seqid,
	.session_trunk = nfs4_test_session_trunk,
	.reboot_recovery_ops = &nfs41_reboot_recovery_ops,
	.nograce_recovery_ops = &nfs41_nograce_recovery_ops,
	.state_renewal_ops = &nfs41_state_renewal_ops,
	.mig_recovery_ops = &nfs41_mig_recovery_ops,
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

static ssize_t nfs4_listxattr(struct dentry *dentry, char *list, size_t size)
{
	ssize_t error, error2, error3;

	error = generic_listxattr(dentry, list, size);
	if (error < 0)
		return error;
	if (list) {
		list += error;
		size -= error;
	}

	error2 = nfs4_listxattr_nfs4_label(d_inode(dentry), list, size);
	if (error2 < 0)
		return error2;

	if (list) {
		list += error2;
		size -= error2;
	}

	error3 = nfs4_listxattr_nfs4_user(d_inode(dentry), list, size);
	if (error3 < 0)
		return error3;

	return error + error2 + error3;
}

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
	.listxattr	= nfs4_listxattr,
};

static const struct inode_operations nfs4_file_inode_operations = {
	.permission	= nfs_permission,
	.getattr	= nfs_getattr,
	.setattr	= nfs_setattr,
	.listxattr	= nfs4_listxattr,
};

const struct nfs_rpc_ops nfs_v4_clientops = {
	.version	= 4,			/* protocol version */
	.dentry_ops	= &nfs4_dentry_operations,
	.dir_inode_ops	= &nfs4_dir_inode_operations,
	.file_inode_ops	= &nfs4_file_inode_operations,
	.file_ops	= &nfs4_file_operations,
	.getroot	= nfs4_proc_get_root,
	.submount	= nfs4_submount,
	.try_get_tree	= nfs4_try_get_tree,
	.getattr	= nfs4_proc_getattr,
	.setattr	= nfs4_proc_setattr,
	.lookup		= nfs4_proc_lookup,
	.lookupp	= nfs4_proc_lookupp,
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
	.rmdir		= nfs4_proc_rmdir,
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
	.alloc_client	= nfs4_alloc_client,
	.init_client	= nfs4_init_client,
	.free_client	= nfs4_free_client,
	.create_server	= nfs4_create_server,
	.clone_server	= nfs_clone_server,
};

static const struct xattr_handler nfs4_xattr_nfs4_acl_handler = {
	.name	= XATTR_NAME_NFSV4_ACL,
	.list	= nfs4_xattr_list_nfs4_acl,
	.get	= nfs4_xattr_get_nfs4_acl,
	.set	= nfs4_xattr_set_nfs4_acl,
};

#ifdef CONFIG_NFS_V4_2
static const struct xattr_handler nfs4_xattr_nfs4_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.get	= nfs4_xattr_get_nfs4_user,
	.set	= nfs4_xattr_set_nfs4_user,
};
#endif

const struct xattr_handler *nfs4_xattr_handlers[] = {
	&nfs4_xattr_nfs4_acl_handler,
#ifdef CONFIG_NFS_V4_SECURITY_LABEL
	&nfs4_xattr_nfs4_label_handler,
#endif
#ifdef CONFIG_NFS_V4_2
	&nfs4_xattr_nfs4_user_handler,
#endif
	NULL
};

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */

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
#include <linux/utsname.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/smp_lock.h>
#include <linux/namei.h>
#include <linux/mount.h>

#include "nfs4_fs.h"
#include "delegation.h"
#include "iostat.h"

#define NFSDBG_FACILITY		NFSDBG_PROC

#define NFS4_POLL_RETRY_MIN	(HZ/10)
#define NFS4_POLL_RETRY_MAX	(15*HZ)

struct nfs4_opendata;
static int _nfs4_proc_open(struct nfs4_opendata *data);
static int nfs4_do_fsinfo(struct nfs_server *, struct nfs_fh *, struct nfs_fsinfo *);
static int nfs4_async_handle_error(struct rpc_task *, const struct nfs_server *);
static int _nfs4_proc_access(struct inode *inode, struct nfs_access_entry *entry);
static int nfs4_handle_exception(const struct nfs_server *server, int errorcode, struct nfs4_exception *exception);
static int nfs4_wait_clnt_recover(struct rpc_clnt *clnt, struct nfs_client *clp);

/* Prevent leaks of NFSv4 errors into userland */
int nfs4_map_errors(int err)
{
	if (err < -1000) {
		dprintk("%s could not handle NFSv4 error %d\n",
				__FUNCTION__, -err);
		return -EIO;
	}
	return err;
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
			0
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
		p = xdr_encode_hyper(p, dentry->d_inode->i_ino);
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
	p = xdr_encode_hyper(p, dentry->d_parent->d_inode->i_ino);

	readdir->pgbase = (char *)p - (char *)start;
	readdir->count -= readdir->pgbase;
	kunmap_atomic(start, KM_USER0);
}

static void renew_lease(const struct nfs_server *server, unsigned long timestamp)
{
	struct nfs_client *clp = server->nfs_client;
	spin_lock(&clp->cl_lock);
	if (time_before(clp->cl_last_renewal,timestamp))
		clp->cl_last_renewal = timestamp;
	spin_unlock(&clp->cl_lock);
}

static void update_changeattr(struct inode *dir, struct nfs4_change_info *cinfo)
{
	struct nfs_inode *nfsi = NFS_I(dir);

	spin_lock(&dir->i_lock);
	nfsi->cache_validity |= NFS_INO_INVALID_ATTR|NFS_INO_REVAL_PAGECACHE|NFS_INO_INVALID_DATA;
	if (cinfo->before == nfsi->change_attr && cinfo->atomic)
		nfsi->change_attr = cinfo->after;
	spin_unlock(&dir->i_lock);
}

struct nfs4_opendata {
	atomic_t count;
	struct nfs_openargs o_arg;
	struct nfs_openres o_res;
	struct nfs_open_confirmargs c_arg;
	struct nfs_open_confirmres c_res;
	struct nfs_fattr f_attr;
	struct nfs_fattr dir_attr;
	struct dentry *dentry;
	struct dentry *dir;
	struct nfs4_state_owner *owner;
	struct iattr attrs;
	unsigned long timestamp;
	int rpc_status;
	int cancelled;
};

static struct nfs4_opendata *nfs4_opendata_alloc(struct dentry *dentry,
		struct nfs4_state_owner *sp, int flags,
		const struct iattr *attrs)
{
	struct dentry *parent = dget_parent(dentry);
	struct inode *dir = parent->d_inode;
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs4_opendata *p;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (p == NULL)
		goto err;
	p->o_arg.seqid = nfs_alloc_seqid(&sp->so_seqid);
	if (p->o_arg.seqid == NULL)
		goto err_free;
	atomic_set(&p->count, 1);
	p->dentry = dget(dentry);
	p->dir = parent;
	p->owner = sp;
	atomic_inc(&sp->so_count);
	p->o_arg.fh = NFS_FH(dir);
	p->o_arg.open_flags = flags,
	p->o_arg.clientid = server->nfs_client->cl_clientid;
	p->o_arg.id = sp->so_id;
	p->o_arg.name = &dentry->d_name;
	p->o_arg.server = server;
	p->o_arg.bitmask = server->attr_bitmask;
	p->o_arg.claim = NFS4_OPEN_CLAIM_NULL;
	p->o_res.f_attr = &p->f_attr;
	p->o_res.dir_attr = &p->dir_attr;
	p->o_res.server = server;
	nfs_fattr_init(&p->f_attr);
	nfs_fattr_init(&p->dir_attr);
	if (flags & O_EXCL) {
		u32 *s = (u32 *) p->o_arg.u.verifier.data;
		s[0] = jiffies;
		s[1] = current->pid;
	} else if (flags & O_CREAT) {
		p->o_arg.u.attrs = &p->attrs;
		memcpy(&p->attrs, attrs, sizeof(p->attrs));
	}
	p->c_arg.fh = &p->o_res.fh;
	p->c_arg.stateid = &p->o_res.stateid;
	p->c_arg.seqid = p->o_arg.seqid;
	return p;
err_free:
	kfree(p);
err:
	dput(parent);
	return NULL;
}

static void nfs4_opendata_free(struct nfs4_opendata *p)
{
	if (p != NULL && atomic_dec_and_test(&p->count)) {
		nfs_free_seqid(p->o_arg.seqid);
		nfs4_put_state_owner(p->owner);
		dput(p->dir);
		dput(p->dentry);
		kfree(p);
	}
}

/* Helper for asynchronous RPC calls */
static int nfs4_call_async(struct rpc_clnt *clnt,
		const struct rpc_call_ops *tk_ops, void *calldata)
{
	struct rpc_task *task;

	if (!(task = rpc_new_task(clnt, RPC_TASK_ASYNC, tk_ops, calldata)))
		return -ENOMEM;
	rpc_execute(task);
	return 0;
}

static int nfs4_wait_for_completion_rpc_task(struct rpc_task *task)
{
	sigset_t oldset;
	int ret;

	rpc_clnt_sigmask(task->tk_client, &oldset);
	ret = rpc_wait_for_completion_task(task);
	rpc_clnt_sigunmask(task->tk_client, &oldset);
	return ret;
}

static inline void update_open_stateflags(struct nfs4_state *state, mode_t open_flags)
{
	switch (open_flags) {
		case FMODE_WRITE:
			state->n_wronly++;
			break;
		case FMODE_READ:
			state->n_rdonly++;
			break;
		case FMODE_READ|FMODE_WRITE:
			state->n_rdwr++;
	}
}

static void update_open_stateid(struct nfs4_state *state, nfs4_stateid *stateid, int open_flags)
{
	struct inode *inode = state->inode;

	open_flags &= (FMODE_READ|FMODE_WRITE);
	/* Protect against nfs4_find_state_byowner() */
	spin_lock(&state->owner->so_lock);
	spin_lock(&inode->i_lock);
	memcpy(&state->stateid, stateid, sizeof(state->stateid));
	update_open_stateflags(state, open_flags);
	nfs4_state_set_mode_locked(state, state->state | open_flags);
	spin_unlock(&inode->i_lock);
	spin_unlock(&state->owner->so_lock);
}

static struct nfs4_state *nfs4_opendata_to_nfs4_state(struct nfs4_opendata *data)
{
	struct inode *inode;
	struct nfs4_state *state = NULL;

	if (!(data->f_attr.valid & NFS_ATTR_FATTR))
		goto out;
	inode = nfs_fhget(data->dir->d_sb, &data->o_res.fh, &data->f_attr);
	if (IS_ERR(inode))
		goto out;
	state = nfs4_get_open_state(inode, data->owner);
	if (state == NULL)
		goto put_inode;
	update_open_stateid(state, &data->o_res.stateid, data->o_arg.open_flags);
put_inode:
	iput(inode);
out:
	return state;
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

static int nfs4_open_recover_helper(struct nfs4_opendata *opendata, mode_t openflags, nfs4_stateid *stateid)
{
	int ret;

	opendata->o_arg.open_flags = openflags;
	ret = _nfs4_proc_open(opendata);
	if (ret != 0)
		return ret; 
	memcpy(stateid->data, opendata->o_res.stateid.data,
			sizeof(stateid->data));
	return 0;
}

static int nfs4_open_recover(struct nfs4_opendata *opendata, struct nfs4_state *state)
{
	nfs4_stateid stateid;
	struct nfs4_state *newstate;
	int mode = 0;
	int delegation = 0;
	int ret;

	/* memory barrier prior to reading state->n_* */
	smp_rmb();
	if (state->n_rdwr != 0) {
		ret = nfs4_open_recover_helper(opendata, FMODE_READ|FMODE_WRITE, &stateid);
		if (ret != 0)
			return ret;
		mode |= FMODE_READ|FMODE_WRITE;
		if (opendata->o_res.delegation_type != 0)
			delegation = opendata->o_res.delegation_type;
		smp_rmb();
	}
	if (state->n_wronly != 0) {
		ret = nfs4_open_recover_helper(opendata, FMODE_WRITE, &stateid);
		if (ret != 0)
			return ret;
		mode |= FMODE_WRITE;
		if (opendata->o_res.delegation_type != 0)
			delegation = opendata->o_res.delegation_type;
		smp_rmb();
	}
	if (state->n_rdonly != 0) {
		ret = nfs4_open_recover_helper(opendata, FMODE_READ, &stateid);
		if (ret != 0)
			return ret;
		mode |= FMODE_READ;
	}
	clear_bit(NFS_DELEGATED_STATE, &state->flags);
	if (mode == 0)
		return 0;
	if (opendata->o_res.delegation_type == 0)
		opendata->o_res.delegation_type = delegation;
	opendata->o_arg.open_flags |= mode;
	newstate = nfs4_opendata_to_nfs4_state(opendata);
	if (newstate != NULL) {
		if (opendata->o_res.delegation_type != 0) {
			struct nfs_inode *nfsi = NFS_I(newstate->inode);
			int delegation_flags = 0;
			if (nfsi->delegation)
				delegation_flags = nfsi->delegation->flags;
			if (!(delegation_flags & NFS_DELEGATION_NEED_RECLAIM))
				nfs_inode_set_delegation(newstate->inode,
						opendata->owner->so_cred,
						&opendata->o_res);
			else
				nfs_inode_reclaim_delegation(newstate->inode,
						opendata->owner->so_cred,
						&opendata->o_res);
		}
		nfs4_close_state(newstate, opendata->o_arg.open_flags);
	}
	if (newstate != state)
		return -ESTALE;
	return 0;
}

/*
 * OPEN_RECLAIM:
 * 	reclaim state on the server after a reboot.
 */
static int _nfs4_do_open_reclaim(struct nfs4_state_owner *sp, struct nfs4_state *state, struct dentry *dentry)
{
	struct nfs_delegation *delegation = NFS_I(state->inode)->delegation;
	struct nfs4_opendata *opendata;
	int delegation_type = 0;
	int status;

	if (delegation != NULL) {
		if (!(delegation->flags & NFS_DELEGATION_NEED_RECLAIM)) {
			memcpy(&state->stateid, &delegation->stateid,
					sizeof(state->stateid));
			set_bit(NFS_DELEGATED_STATE, &state->flags);
			return 0;
		}
		delegation_type = delegation->type;
	}
	opendata = nfs4_opendata_alloc(dentry, sp, 0, NULL);
	if (opendata == NULL)
		return -ENOMEM;
	opendata->o_arg.claim = NFS4_OPEN_CLAIM_PREVIOUS;
	opendata->o_arg.fh = NFS_FH(state->inode);
	nfs_copy_fh(&opendata->o_res.fh, opendata->o_arg.fh);
	opendata->o_arg.u.delegation_type = delegation_type;
	status = nfs4_open_recover(opendata, state);
	nfs4_opendata_free(opendata);
	return status;
}

static int nfs4_do_open_reclaim(struct nfs4_state_owner *sp, struct nfs4_state *state, struct dentry *dentry)
{
	struct nfs_server *server = NFS_SERVER(state->inode);
	struct nfs4_exception exception = { };
	int err;
	do {
		err = _nfs4_do_open_reclaim(sp, state, dentry);
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
	ret = nfs4_do_open_reclaim(sp, state, ctx->dentry);
	put_nfs_open_context(ctx);
	return ret;
}

static int _nfs4_open_delegation_recall(struct dentry *dentry, struct nfs4_state *state)
{
	struct nfs4_state_owner  *sp  = state->owner;
	struct nfs4_opendata *opendata;
	int ret;

	if (!test_bit(NFS_DELEGATED_STATE, &state->flags))
		return 0;
	opendata = nfs4_opendata_alloc(dentry, sp, 0, NULL);
	if (opendata == NULL)
		return -ENOMEM;
	opendata->o_arg.claim = NFS4_OPEN_CLAIM_DELEGATE_CUR;
	memcpy(opendata->o_arg.u.delegation.data, state->stateid.data,
			sizeof(opendata->o_arg.u.delegation.data));
	ret = nfs4_open_recover(opendata, state);
	nfs4_opendata_free(opendata);
	return ret;
}

int nfs4_open_delegation_recall(struct dentry *dentry, struct nfs4_state *state)
{
	struct nfs4_exception exception = { };
	struct nfs_server *server = NFS_SERVER(dentry->d_inode);
	int err;
	do {
		err = _nfs4_open_delegation_recall(dentry, state);
		switch (err) {
			case 0:
				return err;
			case -NFS4ERR_STALE_CLIENTID:
			case -NFS4ERR_STALE_STATEID:
			case -NFS4ERR_EXPIRED:
				/* Don't recall a delegation if it was lost */
				nfs4_schedule_state_recovery(server->nfs_client);
				return err;
		}
		err = nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);
	return err;
}

static void nfs4_open_confirm_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs4_opendata *data = calldata;
	struct  rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_OPEN_CONFIRM],
		.rpc_argp = &data->c_arg,
		.rpc_resp = &data->c_res,
		.rpc_cred = data->owner->so_cred,
	};
	data->timestamp = jiffies;
	rpc_call_setup(task, &msg, 0);
}

static void nfs4_open_confirm_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_opendata *data = calldata;

	data->rpc_status = task->tk_status;
	if (RPC_ASSASSINATED(task))
		return;
	if (data->rpc_status == 0) {
		memcpy(data->o_res.stateid.data, data->c_res.stateid.data,
				sizeof(data->o_res.stateid.data));
		renew_lease(data->o_res.server, data->timestamp);
	}
	nfs_increment_open_seqid(data->rpc_status, data->c_arg.seqid);
	nfs_confirm_seqid(&data->owner->so_seqid, data->rpc_status);
}

static void nfs4_open_confirm_release(void *calldata)
{
	struct nfs4_opendata *data = calldata;
	struct nfs4_state *state = NULL;

	/* If this request hasn't been cancelled, do nothing */
	if (data->cancelled == 0)
		goto out_free;
	/* In case of error, no cleanup! */
	if (data->rpc_status != 0)
		goto out_free;
	nfs_confirm_seqid(&data->owner->so_seqid, 0);
	state = nfs4_opendata_to_nfs4_state(data);
	if (state != NULL)
		nfs4_close_state(state, data->o_arg.open_flags);
out_free:
	nfs4_opendata_free(data);
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
	int status;

	atomic_inc(&data->count);
	/*
	 * If rpc_run_task() ends up calling ->rpc_release(), we
	 * want to ensure that it takes the 'error' code path.
	 */
	data->rpc_status = -ENOMEM;
	task = rpc_run_task(server->client, RPC_TASK_ASYNC, &nfs4_open_confirm_ops, data);
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
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_OPEN],
		.rpc_argp = &data->o_arg,
		.rpc_resp = &data->o_res,
		.rpc_cred = sp->so_cred,
	};
	
	if (nfs_wait_on_sequence(data->o_arg.seqid, task) != 0)
		return;
	/* Update sequence id. */
	data->o_arg.id = sp->so_id;
	data->o_arg.clientid = sp->so_client->cl_clientid;
	if (data->o_arg.claim == NFS4_OPEN_CLAIM_PREVIOUS)
		msg.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_OPEN_NOATTR];
	data->timestamp = jiffies;
	rpc_call_setup(task, &msg, 0);
}

static void nfs4_open_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_opendata *data = calldata;

	data->rpc_status = task->tk_status;
	if (RPC_ASSASSINATED(task))
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
	}
	nfs_increment_open_seqid(data->rpc_status, data->o_arg.seqid);
}

static void nfs4_open_release(void *calldata)
{
	struct nfs4_opendata *data = calldata;
	struct nfs4_state *state = NULL;

	/* If this request hasn't been cancelled, do nothing */
	if (data->cancelled == 0)
		goto out_free;
	/* In case of error, no cleanup! */
	if (data->rpc_status != 0)
		goto out_free;
	/* In case we need an open_confirm, no cleanup! */
	if (data->o_res.rflags & NFS4_OPEN_RESULT_CONFIRM)
		goto out_free;
	nfs_confirm_seqid(&data->owner->so_seqid, 0);
	state = nfs4_opendata_to_nfs4_state(data);
	if (state != NULL)
		nfs4_close_state(state, data->o_arg.open_flags);
out_free:
	nfs4_opendata_free(data);
}

static const struct rpc_call_ops nfs4_open_ops = {
	.rpc_call_prepare = nfs4_open_prepare,
	.rpc_call_done = nfs4_open_done,
	.rpc_release = nfs4_open_release,
};

/*
 * Note: On error, nfs4_proc_open will free the struct nfs4_opendata
 */
static int _nfs4_proc_open(struct nfs4_opendata *data)
{
	struct inode *dir = data->dir->d_inode;
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs_openargs *o_arg = &data->o_arg;
	struct nfs_openres *o_res = &data->o_res;
	struct rpc_task *task;
	int status;

	atomic_inc(&data->count);
	/*
	 * If rpc_run_task() ends up calling ->rpc_release(), we
	 * want to ensure that it takes the 'error' code path.
	 */
	data->rpc_status = -ENOMEM;
	task = rpc_run_task(server->client, RPC_TASK_ASYNC, &nfs4_open_ops, data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	status = nfs4_wait_for_completion_rpc_task(task);
	if (status != 0) {
		data->cancelled = 1;
		smp_wmb();
	} else
		status = data->rpc_status;
	rpc_put_task(task);
	if (status != 0)
		return status;

	if (o_arg->open_flags & O_CREAT) {
		update_changeattr(dir, &o_res->cinfo);
		nfs_post_op_update_inode(dir, o_res->dir_attr);
	} else
		nfs_refresh_inode(dir, o_res->dir_attr);
	if(o_res->rflags & NFS4_OPEN_RESULT_CONFIRM) {
		status = _nfs4_proc_open_confirm(data);
		if (status != 0)
			return status;
	}
	nfs_confirm_seqid(&data->owner->so_seqid, 0);
	if (!(o_res->f_attr->valid & NFS_ATTR_FATTR))
		return server->nfs_client->rpc_ops->getattr(server, &o_res->fh, o_res->f_attr);
	return 0;
}

static int _nfs4_do_access(struct inode *inode, struct rpc_cred *cred, int openflags)
{
	struct nfs_access_entry cache;
	int mask = 0;
	int status;

	if (openflags & FMODE_READ)
		mask |= MAY_READ;
	if (openflags & FMODE_WRITE)
		mask |= MAY_WRITE;
	status = nfs_access_get_cached(inode, cred, &cache);
	if (status == 0)
		goto out;

	/* Be clever: ask server to check for all possible rights */
	cache.mask = MAY_EXEC | MAY_WRITE | MAY_READ;
	cache.cred = cred;
	cache.jiffies = jiffies;
	status = _nfs4_proc_access(inode, &cache);
	if (status != 0)
		return status;
	nfs_access_add_cache(inode, &cache);
out:
	if ((cache.mask & mask) == mask)
		return 0;
	return -EACCES;
}

int nfs4_recover_expired_lease(struct nfs_server *server)
{
	struct nfs_client *clp = server->nfs_client;
	int ret;

	for (;;) {
		ret = nfs4_wait_clnt_recover(server->client, clp);
		if (ret != 0)
			return ret;
		if (!test_and_clear_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state))
			break;
		nfs4_schedule_state_recovery(clp);
	}
	return 0;
}

/*
 * OPEN_EXPIRED:
 * 	reclaim state on the server after a network partition.
 * 	Assumes caller holds the appropriate lock
 */
static int _nfs4_open_expired(struct nfs4_state_owner *sp, struct nfs4_state *state, struct dentry *dentry)
{
	struct inode *inode = state->inode;
	struct nfs_delegation *delegation = NFS_I(inode)->delegation;
	struct nfs4_opendata *opendata;
	int openflags = state->state & (FMODE_READ|FMODE_WRITE);
	int ret;

	if (delegation != NULL && !(delegation->flags & NFS_DELEGATION_NEED_RECLAIM)) {
		ret = _nfs4_do_access(inode, sp->so_cred, openflags);
		if (ret < 0)
			return ret;
		memcpy(&state->stateid, &delegation->stateid, sizeof(state->stateid));
		set_bit(NFS_DELEGATED_STATE, &state->flags);
		return 0;
	}
	opendata = nfs4_opendata_alloc(dentry, sp, openflags, NULL);
	if (opendata == NULL)
		return -ENOMEM;
	ret = nfs4_open_recover(opendata, state);
	if (ret == -ESTALE) {
		/* Invalidate the state owner so we don't ever use it again */
		nfs4_drop_state_owner(sp);
		d_drop(dentry);
	}
	nfs4_opendata_free(opendata);
	return ret;
}

static inline int nfs4_do_open_expired(struct nfs4_state_owner *sp, struct nfs4_state *state, struct dentry *dentry)
{
	struct nfs_server *server = NFS_SERVER(dentry->d_inode);
	struct nfs4_exception exception = { };
	int err;

	do {
		err = _nfs4_open_expired(sp, state, dentry);
		if (err == -NFS4ERR_DELAY)
			nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);
	return err;
}

static int nfs4_open_expired(struct nfs4_state_owner *sp, struct nfs4_state *state)
{
	struct nfs_open_context *ctx;
	int ret;

	ctx = nfs4_state_find_open_context(state);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);
	ret = nfs4_do_open_expired(sp, state, ctx->dentry);
	put_nfs_open_context(ctx);
	return ret;
}

/*
 * Returns a referenced nfs4_state if there is an open delegation on the file
 */
static int _nfs4_open_delegated(struct inode *inode, int flags, struct rpc_cred *cred, struct nfs4_state **res)
{
	struct nfs_delegation *delegation;
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_client *clp = server->nfs_client;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs4_state_owner *sp = NULL;
	struct nfs4_state *state = NULL;
	int open_flags = flags & (FMODE_READ|FMODE_WRITE);
	int err;

	err = -ENOMEM;
	if (!(sp = nfs4_get_state_owner(server, cred))) {
		dprintk("%s: nfs4_get_state_owner failed!\n", __FUNCTION__);
		return err;
	}
	err = nfs4_recover_expired_lease(server);
	if (err != 0)
		goto out_put_state_owner;
	/* Protect against reboot recovery - NOTE ORDER! */
	down_read(&clp->cl_sem);
	/* Protect against delegation recall */
	down_read(&nfsi->rwsem);
	delegation = NFS_I(inode)->delegation;
	err = -ENOENT;
	if (delegation == NULL || (delegation->type & open_flags) != open_flags)
		goto out_err;
	err = -ENOMEM;
	state = nfs4_get_open_state(inode, sp);
	if (state == NULL)
		goto out_err;

	err = -ENOENT;
	if ((state->state & open_flags) == open_flags) {
		spin_lock(&inode->i_lock);
		update_open_stateflags(state, open_flags);
		spin_unlock(&inode->i_lock);
		goto out_ok;
	} else if (state->state != 0)
		goto out_put_open_state;

	lock_kernel();
	err = _nfs4_do_access(inode, cred, open_flags);
	unlock_kernel();
	if (err != 0)
		goto out_put_open_state;
	set_bit(NFS_DELEGATED_STATE, &state->flags);
	update_open_stateid(state, &delegation->stateid, open_flags);
out_ok:
	nfs4_put_state_owner(sp);
	up_read(&nfsi->rwsem);
	up_read(&clp->cl_sem);
	*res = state;
	return 0;
out_put_open_state:
	nfs4_put_open_state(state);
out_err:
	up_read(&nfsi->rwsem);
	up_read(&clp->cl_sem);
	if (err != -EACCES)
		nfs_inode_return_delegation(inode);
out_put_state_owner:
	nfs4_put_state_owner(sp);
	return err;
}

static struct nfs4_state *nfs4_open_delegated(struct inode *inode, int flags, struct rpc_cred *cred)
{
	struct nfs4_exception exception = { };
	struct nfs4_state *res = ERR_PTR(-EIO);
	int err;

	do {
		err = _nfs4_open_delegated(inode, flags, cred, &res);
		if (err == 0)
			break;
		res = ERR_PTR(nfs4_handle_exception(NFS_SERVER(inode),
					err, &exception));
	} while (exception.retry);
	return res;
}

/*
 * Returns a referenced nfs4_state
 */
static int _nfs4_do_open(struct inode *dir, struct dentry *dentry, int flags, struct iattr *sattr, struct rpc_cred *cred, struct nfs4_state **res)
{
	struct nfs4_state_owner  *sp;
	struct nfs4_state     *state = NULL;
	struct nfs_server       *server = NFS_SERVER(dir);
	struct nfs_client *clp = server->nfs_client;
	struct nfs4_opendata *opendata;
	int                     status;

	/* Protect against reboot recovery conflicts */
	status = -ENOMEM;
	if (!(sp = nfs4_get_state_owner(server, cred))) {
		dprintk("nfs4_do_open: nfs4_get_state_owner failed!\n");
		goto out_err;
	}
	status = nfs4_recover_expired_lease(server);
	if (status != 0)
		goto err_put_state_owner;
	down_read(&clp->cl_sem);
	status = -ENOMEM;
	opendata = nfs4_opendata_alloc(dentry, sp, flags, sattr);
	if (opendata == NULL)
		goto err_release_rwsem;

	status = _nfs4_proc_open(opendata);
	if (status != 0)
		goto err_opendata_free;

	status = -ENOMEM;
	state = nfs4_opendata_to_nfs4_state(opendata);
	if (state == NULL)
		goto err_opendata_free;
	if (opendata->o_res.delegation_type != 0)
		nfs_inode_set_delegation(state->inode, cred, &opendata->o_res);
	nfs4_opendata_free(opendata);
	nfs4_put_state_owner(sp);
	up_read(&clp->cl_sem);
	*res = state;
	return 0;
err_opendata_free:
	nfs4_opendata_free(opendata);
err_release_rwsem:
	up_read(&clp->cl_sem);
err_put_state_owner:
	nfs4_put_state_owner(sp);
out_err:
	*res = NULL;
	return status;
}


static struct nfs4_state *nfs4_do_open(struct inode *dir, struct dentry *dentry, int flags, struct iattr *sattr, struct rpc_cred *cred)
{
	struct nfs4_exception exception = { };
	struct nfs4_state *res;
	int status;

	do {
		status = _nfs4_do_open(dir, dentry, flags, sattr, cred, &res);
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
			printk(KERN_WARNING "NFS: v4 server returned a bad sequence-id error!\n");
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
		res = ERR_PTR(nfs4_handle_exception(NFS_SERVER(dir),
					status, &exception));
	} while (exception.retry);
	return res;
}

static int _nfs4_do_setattr(struct inode *inode, struct nfs_fattr *fattr,
                struct iattr *sattr, struct nfs4_state *state)
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
                .rpc_proc       = &nfs4_procedures[NFSPROC4_CLNT_SETATTR],
                .rpc_argp       = &arg,
                .rpc_resp       = &res,
        };
	unsigned long timestamp = jiffies;
	int status;

	nfs_fattr_init(fattr);

	if (nfs4_copy_delegation_stateid(&arg.stateid, inode)) {
		/* Use that stateid */
	} else if (state != NULL) {
		msg.rpc_cred = state->owner->so_cred;
		nfs4_copy_stateid(&arg.stateid, state, current->files);
	} else
		memcpy(&arg.stateid, &zero_stateid, sizeof(arg.stateid));

	status = rpc_call_sync(server->client, &msg, 0);
	if (status == 0 && state != NULL)
		renew_lease(server, timestamp);
	return status;
}

static int nfs4_do_setattr(struct inode *inode, struct nfs_fattr *fattr,
                struct iattr *sattr, struct nfs4_state *state)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(server,
				_nfs4_do_setattr(inode, fattr, sattr, state),
				&exception);
	} while (exception.retry);
	return err;
}

struct nfs4_closedata {
	struct inode *inode;
	struct nfs4_state *state;
	struct nfs_closeargs arg;
	struct nfs_closeres res;
	struct nfs_fattr fattr;
	unsigned long timestamp;
};

static void nfs4_free_closedata(void *data)
{
	struct nfs4_closedata *calldata = data;
	struct nfs4_state_owner *sp = calldata->state->owner;

	nfs4_put_open_state(calldata->state);
	nfs_free_seqid(calldata->arg.seqid);
	nfs4_put_state_owner(sp);
	kfree(calldata);
}

static void nfs4_close_done(struct rpc_task *task, void *data)
{
	struct nfs4_closedata *calldata = data;
	struct nfs4_state *state = calldata->state;
	struct nfs_server *server = NFS_SERVER(calldata->inode);

	if (RPC_ASSASSINATED(task))
		return;
        /* hmm. we are done with the inode, and in the process of freeing
	 * the state_owner. we keep this around to process errors
	 */
	nfs_increment_open_seqid(task->tk_status, calldata->arg.seqid);
	switch (task->tk_status) {
		case 0:
			memcpy(&state->stateid, &calldata->res.stateid,
					sizeof(state->stateid));
			renew_lease(server, calldata->timestamp);
			break;
		case -NFS4ERR_STALE_STATEID:
		case -NFS4ERR_EXPIRED:
			break;
		default:
			if (nfs4_async_handle_error(task, server) == -EAGAIN) {
				rpc_restart_call(task);
				return;
			}
	}
	nfs_refresh_inode(calldata->inode, calldata->res.fattr);
}

static void nfs4_close_prepare(struct rpc_task *task, void *data)
{
	struct nfs4_closedata *calldata = data;
	struct nfs4_state *state = calldata->state;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_CLOSE],
		.rpc_argp = &calldata->arg,
		.rpc_resp = &calldata->res,
		.rpc_cred = state->owner->so_cred,
	};
	int mode = 0, old_mode;

	if (nfs_wait_on_sequence(calldata->arg.seqid, task) != 0)
		return;
	/* Recalculate the new open mode in case someone reopened the file
	 * while we were waiting in line to be scheduled.
	 */
	spin_lock(&state->owner->so_lock);
	spin_lock(&calldata->inode->i_lock);
	mode = old_mode = state->state;
	if (state->n_rdwr == 0) {
		if (state->n_rdonly == 0)
			mode &= ~FMODE_READ;
		if (state->n_wronly == 0)
			mode &= ~FMODE_WRITE;
	}
	nfs4_state_set_mode_locked(state, mode);
	spin_unlock(&calldata->inode->i_lock);
	spin_unlock(&state->owner->so_lock);
	if (mode == old_mode || test_bit(NFS_DELEGATED_STATE, &state->flags)) {
		/* Note: exit _without_ calling nfs4_close_done */
		task->tk_action = NULL;
		return;
	}
	nfs_fattr_init(calldata->res.fattr);
	if (mode != 0)
		msg.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_OPEN_DOWNGRADE];
	calldata->arg.open_flags = mode;
	calldata->timestamp = jiffies;
	rpc_call_setup(task, &msg, 0);
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
int nfs4_do_close(struct inode *inode, struct nfs4_state *state) 
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs4_closedata *calldata;
	int status = -ENOMEM;

	calldata = kmalloc(sizeof(*calldata), GFP_KERNEL);
	if (calldata == NULL)
		goto out;
	calldata->inode = inode;
	calldata->state = state;
	calldata->arg.fh = NFS_FH(inode);
	calldata->arg.stateid = &state->stateid;
	/* Serialization for the sequence id */
	calldata->arg.seqid = nfs_alloc_seqid(&state->owner->so_seqid);
	if (calldata->arg.seqid == NULL)
		goto out_free_calldata;
	calldata->arg.bitmask = server->attr_bitmask;
	calldata->res.fattr = &calldata->fattr;
	calldata->res.server = server;

	status = nfs4_call_async(server->client, &nfs4_close_ops, calldata);
	if (status == 0)
		goto out;

	nfs_free_seqid(calldata->arg.seqid);
out_free_calldata:
	kfree(calldata);
out:
	return status;
}

static int nfs4_intent_set_file(struct nameidata *nd, struct dentry *dentry, struct nfs4_state *state)
{
	struct file *filp;

	filp = lookup_instantiate_filp(nd, dentry, NULL);
	if (!IS_ERR(filp)) {
		struct nfs_open_context *ctx;
		ctx = (struct nfs_open_context *)filp->private_data;
		ctx->state = state;
		return 0;
	}
	nfs4_close_state(state, nd->intent.open.flags);
	return PTR_ERR(filp);
}

struct dentry *
nfs4_atomic_open(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	struct iattr attr;
	struct rpc_cred *cred;
	struct nfs4_state *state;
	struct dentry *res;

	if (nd->flags & LOOKUP_CREATE) {
		attr.ia_mode = nd->intent.open.create_mode;
		attr.ia_valid = ATTR_MODE;
		if (!IS_POSIXACL(dir))
			attr.ia_mode &= ~current->fs->umask;
	} else {
		attr.ia_valid = 0;
		BUG_ON(nd->intent.open.flags & O_CREAT);
	}

	cred = rpcauth_lookupcred(NFS_CLIENT(dir)->cl_auth, 0);
	if (IS_ERR(cred))
		return (struct dentry *)cred;
	state = nfs4_do_open(dir, dentry, nd->intent.open.flags, &attr, cred);
	put_rpccred(cred);
	if (IS_ERR(state)) {
		if (PTR_ERR(state) == -ENOENT)
			d_add(dentry, NULL);
		return (struct dentry *)state;
	}
	res = d_add_unique(dentry, igrab(state->inode));
	if (res != NULL)
		dentry = res;
	nfs4_intent_set_file(nd, dentry, state);
	return res;
}

int
nfs4_open_revalidate(struct inode *dir, struct dentry *dentry, int openflags, struct nameidata *nd)
{
	struct rpc_cred *cred;
	struct nfs4_state *state;

	cred = rpcauth_lookupcred(NFS_CLIENT(dir)->cl_auth, 0);
	if (IS_ERR(cred))
		return PTR_ERR(cred);
	state = nfs4_open_delegated(dentry->d_inode, openflags, cred);
	if (IS_ERR(state))
		state = nfs4_do_open(dir, dentry, openflags, NULL, cred);
	put_rpccred(cred);
	if (IS_ERR(state)) {
		switch (PTR_ERR(state)) {
			case -EPERM:
			case -EACCES:
			case -EDQUOT:
			case -ENOSPC:
			case -EROFS:
				lookup_instantiate_filp(nd, (struct dentry *)state, NULL);
				return 1;
			default:
				goto out_drop;
		}
	}
	if (state->inode == dentry->d_inode) {
		nfs4_intent_set_file(nd, dentry, state);
		return 1;
	}
	nfs4_close_state(state, openflags);
out_drop:
	d_drop(dentry);
	return 0;
}


static int _nfs4_server_capabilities(struct nfs_server *server, struct nfs_fh *fhandle)
{
	struct nfs4_server_caps_res res = {};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SERVER_CAPS],
		.rpc_argp = fhandle,
		.rpc_resp = &res,
	};
	int status;

	status = rpc_call_sync(server->client, &msg, 0);
	if (status == 0) {
		memcpy(server->attr_bitmask, res.attr_bitmask, sizeof(server->attr_bitmask));
		if (res.attr_bitmask[0] & FATTR4_WORD0_ACL)
			server->caps |= NFS_CAP_ACLS;
		if (res.has_links != 0)
			server->caps |= NFS_CAP_HARDLINKS;
		if (res.has_symlinks != 0)
			server->caps |= NFS_CAP_SYMLINKS;
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
	return rpc_call_sync(server->client, &msg, 0);
}

static int nfs4_lookup_root(struct nfs_server *server, struct nfs_fh *fhandle,
		struct nfs_fsinfo *info)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(server,
				_nfs4_lookup_root(server, fhandle, info),
				&exception);
	} while (exception.retry);
	return err;
}

/*
 * get the file handle for the "/" directory on the server
 */
static int nfs4_proc_get_root(struct nfs_server *server, struct nfs_fh *fhandle,
			      struct nfs_fsinfo *info)
{
	int status;

	status = nfs4_lookup_root(server, fhandle, info);
	if (status == 0)
		status = nfs4_server_capabilities(server, fhandle);
	if (status == 0)
		status = nfs4_do_fsinfo(server, fhandle, info);
	return nfs4_map_errors(status);
}

/*
 * Get locations and (maybe) other attributes of a referral.
 * Note that we'll actually follow the referral later when
 * we detect fsid mismatch in inode revalidation
 */
static int nfs4_get_referral(struct inode *dir, struct qstr *name, struct nfs_fattr *fattr, struct nfs_fh *fhandle)
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
		dprintk("%s: server did not return a different fsid for a referral at %s\n", __FUNCTION__, name->name);
		status = -EIO;
		goto out;
	}

	memcpy(fattr, &locations->fattr, sizeof(struct nfs_fattr));
	fattr->valid |= NFS_ATTR_FATTR_V4_REFERRAL;
	if (!fattr->mode)
		fattr->mode = S_IFDIR;
	memset(fhandle, 0, sizeof(struct nfs_fh));
out:
	if (page)
		__free_page(page);
	if (locations)
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
	return rpc_call_sync(server->client, &msg, 0);
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
	struct rpc_cred *cred;
	struct inode *inode = dentry->d_inode;
	struct nfs_open_context *ctx;
	struct nfs4_state *state = NULL;
	int status;

	nfs_fattr_init(fattr);
	
	cred = rpcauth_lookupcred(NFS_CLIENT(inode)->cl_auth, 0);
	if (IS_ERR(cred))
		return PTR_ERR(cred);

	/* Search for an existing open(O_WRITE) file */
	ctx = nfs_find_open_context(inode, cred, FMODE_WRITE);
	if (ctx != NULL)
		state = ctx->state;

	status = nfs4_do_setattr(inode, fattr, sattr, state);
	if (status == 0)
		nfs_setattr_update_inode(inode, sattr);
	if (ctx != NULL)
		put_nfs_open_context(ctx);
	put_rpccred(cred);
	return status;
}

static int _nfs4_proc_lookupfh(struct nfs_server *server, struct nfs_fh *dirfh,
		struct qstr *name, struct nfs_fh *fhandle,
		struct nfs_fattr *fattr)
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
	status = rpc_call_sync(server->client, &msg, 0);
	dprintk("NFS reply lookupfh: %d\n", status);
	if (status == -NFS4ERR_MOVED)
		status = -EREMOTE;
	return status;
}

static int nfs4_proc_lookupfh(struct nfs_server *server, struct nfs_fh *dirfh,
			      struct qstr *name, struct nfs_fh *fhandle,
			      struct nfs_fattr *fattr)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(server,
				_nfs4_proc_lookupfh(server, dirfh, name,
						    fhandle, fattr),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_lookup(struct inode *dir, struct qstr *name,
		struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	int		       status;
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs4_lookup_arg args = {
		.bitmask = server->attr_bitmask,
		.dir_fh = NFS_FH(dir),
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
	
	dprintk("NFS call  lookup %s\n", name->name);
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	if (status == -NFS4ERR_MOVED)
		status = nfs4_get_referral(dir, name, fattr, fhandle);
	dprintk("NFS reply lookup: %d\n", status);
	return status;
}

static int nfs4_proc_lookup(struct inode *dir, struct qstr *name, struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(dir),
				_nfs4_proc_lookup(dir, name, fhandle, fattr),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_access(struct inode *inode, struct nfs_access_entry *entry)
{
	struct nfs4_accessargs args = {
		.fh = NFS_FH(inode),
	};
	struct nfs4_accessres res = { 0 };
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
	status = rpc_call_sync(NFS_CLIENT(inode), &msg, 0);
	if (!status) {
		entry->mask = 0;
		if (res.access & NFS4_ACCESS_READ)
			entry->mask |= MAY_READ;
		if (res.access & (NFS4_ACCESS_MODIFY | NFS4_ACCESS_EXTEND | NFS4_ACCESS_DELETE))
			entry->mask |= MAY_WRITE;
		if (res.access & (NFS4_ACCESS_LOOKUP|NFS4_ACCESS_EXECUTE))
			entry->mask |= MAY_EXEC;
	}
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
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_READLINK],
		.rpc_argp = &args,
		.rpc_resp = NULL,
	};

	return rpc_call_sync(NFS_CLIENT(inode), &msg, 0);
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
                 int flags, struct nameidata *nd)
{
	struct nfs4_state *state;
	struct rpc_cred *cred;
	int status = 0;

	cred = rpcauth_lookupcred(NFS_CLIENT(dir)->cl_auth, 0);
	if (IS_ERR(cred)) {
		status = PTR_ERR(cred);
		goto out;
	}
	state = nfs4_do_open(dir, dentry, flags, sattr, cred);
	put_rpccred(cred);
	if (IS_ERR(state)) {
		status = PTR_ERR(state);
		goto out;
	}
	d_instantiate(dentry, igrab(state->inode));
	if (flags & O_EXCL) {
		struct nfs_fattr fattr;
		status = nfs4_do_setattr(state->inode, &fattr, sattr, state);
		if (status == 0)
			nfs_setattr_update_inode(state->inode, sattr);
	}
	if (status == 0 && nd != NULL && (nd->flags & LOOKUP_OPEN))
		status = nfs4_intent_set_file(nd, dentry, state);
	else
		nfs4_close_state(state, flags);
out:
	return status;
}

static int _nfs4_proc_remove(struct inode *dir, struct qstr *name)
{
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs4_remove_arg args = {
		.fh = NFS_FH(dir),
		.name = name,
		.bitmask = server->attr_bitmask,
	};
	struct nfs_fattr dir_attr;
	struct nfs4_remove_res	res = {
		.server = server,
		.dir_attr = &dir_attr,
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_REMOVE],
		.rpc_argp	= &args,
		.rpc_resp	= &res,
	};
	int			status;

	nfs_fattr_init(res.dir_attr);
	status = rpc_call_sync(server->client, &msg, 0);
	if (status == 0) {
		update_changeattr(dir, &res.cinfo);
		nfs_post_op_update_inode(dir, res.dir_attr);
	}
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

struct unlink_desc {
	struct nfs4_remove_arg	args;
	struct nfs4_remove_res	res;
	struct nfs_fattr dir_attr;
};

static int nfs4_proc_unlink_setup(struct rpc_message *msg, struct dentry *dir,
		struct qstr *name)
{
	struct nfs_server *server = NFS_SERVER(dir->d_inode);
	struct unlink_desc *up;

	up = kmalloc(sizeof(*up), GFP_KERNEL);
	if (!up)
		return -ENOMEM;
	
	up->args.fh = NFS_FH(dir->d_inode);
	up->args.name = name;
	up->args.bitmask = server->attr_bitmask;
	up->res.server = server;
	up->res.dir_attr = &up->dir_attr;
	
	msg->rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_REMOVE];
	msg->rpc_argp = &up->args;
	msg->rpc_resp = &up->res;
	return 0;
}

static int nfs4_proc_unlink_done(struct dentry *dir, struct rpc_task *task)
{
	struct rpc_message *msg = &task->tk_msg;
	struct unlink_desc *up;
	
	if (msg->rpc_resp != NULL) {
		up = container_of(msg->rpc_resp, struct unlink_desc, res);
		update_changeattr(dir->d_inode, &up->res.cinfo);
		nfs_post_op_update_inode(dir->d_inode, up->res.dir_attr);
		kfree(up);
		msg->rpc_resp = NULL;
		msg->rpc_argp = NULL;
	}
	return 0;
}

static int _nfs4_proc_rename(struct inode *old_dir, struct qstr *old_name,
		struct inode *new_dir, struct qstr *new_name)
{
	struct nfs_server *server = NFS_SERVER(old_dir);
	struct nfs4_rename_arg arg = {
		.old_dir = NFS_FH(old_dir),
		.new_dir = NFS_FH(new_dir),
		.old_name = old_name,
		.new_name = new_name,
		.bitmask = server->attr_bitmask,
	};
	struct nfs_fattr old_fattr, new_fattr;
	struct nfs4_rename_res res = {
		.server = server,
		.old_fattr = &old_fattr,
		.new_fattr = &new_fattr,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_RENAME],
		.rpc_argp = &arg,
		.rpc_resp = &res,
	};
	int			status;
	
	nfs_fattr_init(res.old_fattr);
	nfs_fattr_init(res.new_fattr);
	status = rpc_call_sync(server->client, &msg, 0);

	if (!status) {
		update_changeattr(old_dir, &res.old_cinfo);
		nfs_post_op_update_inode(old_dir, res.old_fattr);
		update_changeattr(new_dir, &res.new_cinfo);
		nfs_post_op_update_inode(new_dir, res.new_fattr);
	}
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
	struct nfs_fattr fattr, dir_attr;
	struct nfs4_link_res res = {
		.server = server,
		.fattr = &fattr,
		.dir_attr = &dir_attr,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LINK],
		.rpc_argp = &arg,
		.rpc_resp = &res,
	};
	int			status;

	nfs_fattr_init(res.fattr);
	nfs_fattr_init(res.dir_attr);
	status = rpc_call_sync(server->client, &msg, 0);
	if (!status) {
		update_changeattr(dir, &res.cinfo);
		nfs_post_op_update_inode(dir, res.dir_attr);
		nfs_post_op_update_inode(inode, res.fattr);
	}

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

static int _nfs4_proc_symlink(struct inode *dir, struct dentry *dentry,
		struct page *page, unsigned int len, struct iattr *sattr)
{
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs_fh fhandle;
	struct nfs_fattr fattr, dir_fattr;
	struct nfs4_create_arg arg = {
		.dir_fh = NFS_FH(dir),
		.server = server,
		.name = &dentry->d_name,
		.attrs = sattr,
		.ftype = NF4LNK,
		.bitmask = server->attr_bitmask,
	};
	struct nfs4_create_res res = {
		.server = server,
		.fh = &fhandle,
		.fattr = &fattr,
		.dir_fattr = &dir_fattr,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SYMLINK],
		.rpc_argp = &arg,
		.rpc_resp = &res,
	};
	int			status;

	if (len > NFS4_MAXPATHLEN)
		return -ENAMETOOLONG;

	arg.u.symlink.pages = &page;
	arg.u.symlink.len = len;
	nfs_fattr_init(&fattr);
	nfs_fattr_init(&dir_fattr);
	
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	if (!status) {
		update_changeattr(dir, &res.dir_cinfo);
		nfs_post_op_update_inode(dir, res.dir_fattr);
		status = nfs_instantiate(dentry, &fhandle, &fattr);
	}
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
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs_fh fhandle;
	struct nfs_fattr fattr, dir_fattr;
	struct nfs4_create_arg arg = {
		.dir_fh = NFS_FH(dir),
		.server = server,
		.name = &dentry->d_name,
		.attrs = sattr,
		.ftype = NF4DIR,
		.bitmask = server->attr_bitmask,
	};
	struct nfs4_create_res res = {
		.server = server,
		.fh = &fhandle,
		.fattr = &fattr,
		.dir_fattr = &dir_fattr,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_CREATE],
		.rpc_argp = &arg,
		.rpc_resp = &res,
	};
	int			status;

	nfs_fattr_init(&fattr);
	nfs_fattr_init(&dir_fattr);
	
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	if (!status) {
		update_changeattr(dir, &res.dir_cinfo);
		nfs_post_op_update_inode(dir, res.dir_fattr);
		status = nfs_instantiate(dentry, &fhandle, &fattr);
	}
	return status;
}

static int nfs4_proc_mkdir(struct inode *dir, struct dentry *dentry,
		struct iattr *sattr)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(dir),
				_nfs4_proc_mkdir(dir, dentry, sattr),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_readdir(struct dentry *dentry, struct rpc_cred *cred,
                  u64 cookie, struct page *page, unsigned int count, int plus)
{
	struct inode		*dir = dentry->d_inode;
	struct nfs4_readdir_arg args = {
		.fh = NFS_FH(dir),
		.pages = &page,
		.pgbase = 0,
		.count = count,
		.bitmask = NFS_SERVER(dentry->d_inode)->attr_bitmask,
	};
	struct nfs4_readdir_res res;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_READDIR],
		.rpc_argp = &args,
		.rpc_resp = &res,
		.rpc_cred = cred,
	};
	int			status;

	dprintk("%s: dentry = %s/%s, cookie = %Lu\n", __FUNCTION__,
			dentry->d_parent->d_name.name,
			dentry->d_name.name,
			(unsigned long long)cookie);
	nfs4_setup_readdir(cookie, NFS_COOKIEVERF(dir), dentry, &args);
	res.pgbase = args.pgbase;
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	if (status == 0)
		memcpy(NFS_COOKIEVERF(dir), res.verifier.data, NFS4_VERIFIER_SIZE);
	dprintk("%s: returns %d\n", __FUNCTION__, status);
	return status;
}

static int nfs4_proc_readdir(struct dentry *dentry, struct rpc_cred *cred,
                  u64 cookie, struct page *page, unsigned int count, int plus)
{
	struct nfs4_exception exception = { };
	int err;
	do {
		err = nfs4_handle_exception(NFS_SERVER(dentry->d_inode),
				_nfs4_proc_readdir(dentry, cred, cookie,
					page, count, plus),
				&exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_mknod(struct inode *dir, struct dentry *dentry,
		struct iattr *sattr, dev_t rdev)
{
	struct nfs_server *server = NFS_SERVER(dir);
	struct nfs_fh fh;
	struct nfs_fattr fattr, dir_fattr;
	struct nfs4_create_arg arg = {
		.dir_fh = NFS_FH(dir),
		.server = server,
		.name = &dentry->d_name,
		.attrs = sattr,
		.bitmask = server->attr_bitmask,
	};
	struct nfs4_create_res res = {
		.server = server,
		.fh = &fh,
		.fattr = &fattr,
		.dir_fattr = &dir_fattr,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_CREATE],
		.rpc_argp = &arg,
		.rpc_resp = &res,
	};
	int			status;
	int                     mode = sattr->ia_mode;

	nfs_fattr_init(&fattr);
	nfs_fattr_init(&dir_fattr);

	BUG_ON(!(sattr->ia_valid & ATTR_MODE));
	BUG_ON(!S_ISFIFO(mode) && !S_ISBLK(mode) && !S_ISCHR(mode) && !S_ISSOCK(mode));
	if (S_ISFIFO(mode))
		arg.ftype = NF4FIFO;
	else if (S_ISBLK(mode)) {
		arg.ftype = NF4BLK;
		arg.u.device.specdata1 = MAJOR(rdev);
		arg.u.device.specdata2 = MINOR(rdev);
	}
	else if (S_ISCHR(mode)) {
		arg.ftype = NF4CHR;
		arg.u.device.specdata1 = MAJOR(rdev);
		arg.u.device.specdata2 = MINOR(rdev);
	}
	else
		arg.ftype = NF4SOCK;
	
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	if (status == 0) {
		update_changeattr(dir, &res.dir_cinfo);
		nfs_post_op_update_inode(dir, res.dir_fattr);
		status = nfs_instantiate(dentry, &fh, &fattr);
	}
	return status;
}

static int nfs4_proc_mknod(struct inode *dir, struct dentry *dentry,
		struct iattr *sattr, dev_t rdev)
{
	struct nfs4_exception exception = { };
	int err;
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
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_STATFS],
		.rpc_argp = &args,
		.rpc_resp = fsstat,
	};

	nfs_fattr_init(fsstat->fattr);
	return rpc_call_sync(server->client, &msg, 0);
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
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_FSINFO],
		.rpc_argp = &args,
		.rpc_resp = fsinfo,
	};

	return rpc_call_sync(server->client, &msg, 0);
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
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_PATHCONF],
		.rpc_argp = &args,
		.rpc_resp = pathconf,
	};

	/* None of the pathconf attributes are mandatory to implement */
	if ((args.bitmask[0] & nfs4_pathconf_bitmap[0]) == 0) {
		memset(pathconf, 0, sizeof(*pathconf));
		return 0;
	}

	nfs_fattr_init(pathconf->fattr);
	return rpc_call_sync(server->client, &msg, 0);
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

static int nfs4_read_done(struct rpc_task *task, struct nfs_read_data *data)
{
	struct nfs_server *server = NFS_SERVER(data->inode);

	if (nfs4_async_handle_error(task, server) == -EAGAIN) {
		rpc_restart_call(task);
		return -EAGAIN;
	}
	if (task->tk_status > 0)
		renew_lease(server, data->timestamp);
	return 0;
}

static void nfs4_proc_read_setup(struct nfs_read_data *data)
{
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_READ],
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = data->cred,
	};

	data->timestamp   = jiffies;

	rpc_call_setup(&data->task, &msg, 0);
}

static int nfs4_write_done(struct rpc_task *task, struct nfs_write_data *data)
{
	struct inode *inode = data->inode;
	
	if (nfs4_async_handle_error(task, NFS_SERVER(inode)) == -EAGAIN) {
		rpc_restart_call(task);
		return -EAGAIN;
	}
	if (task->tk_status >= 0) {
		renew_lease(NFS_SERVER(inode), data->timestamp);
		nfs_post_op_update_inode(inode, data->res.fattr);
	}
	return 0;
}

static void nfs4_proc_write_setup(struct nfs_write_data *data, int how)
{
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_WRITE],
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = data->cred,
	};
	struct inode *inode = data->inode;
	struct nfs_server *server = NFS_SERVER(inode);
	int stable;
	
	if (how & FLUSH_STABLE) {
		if (!NFS_I(inode)->ncommit)
			stable = NFS_FILE_SYNC;
		else
			stable = NFS_DATA_SYNC;
	} else
		stable = NFS_UNSTABLE;
	data->args.stable = stable;
	data->args.bitmask = server->attr_bitmask;
	data->res.server = server;

	data->timestamp   = jiffies;

	/* Finalize the task. */
	rpc_call_setup(&data->task, &msg, 0);
}

static int nfs4_commit_done(struct rpc_task *task, struct nfs_write_data *data)
{
	struct inode *inode = data->inode;
	
	if (nfs4_async_handle_error(task, NFS_SERVER(inode)) == -EAGAIN) {
		rpc_restart_call(task);
		return -EAGAIN;
	}
	if (task->tk_status >= 0)
		nfs_post_op_update_inode(inode, data->res.fattr);
	return 0;
}

static void nfs4_proc_commit_setup(struct nfs_write_data *data, int how)
{
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_COMMIT],
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = data->cred,
	};	
	struct nfs_server *server = NFS_SERVER(data->inode);
	
	data->args.bitmask = server->attr_bitmask;
	data->res.server = server;

	rpc_call_setup(&data->task, &msg, 0);
}

/*
 * nfs4_proc_async_renew(): This is not one of the nfs_rpc_ops; it is a special
 * standalone procedure for queueing an asynchronous RENEW.
 */
static void nfs4_renew_done(struct rpc_task *task, void *data)
{
	struct nfs_client *clp = (struct nfs_client *)task->tk_msg.rpc_argp;
	unsigned long timestamp = (unsigned long)data;

	if (task->tk_status < 0) {
		switch (task->tk_status) {
			case -NFS4ERR_STALE_CLIENTID:
			case -NFS4ERR_EXPIRED:
			case -NFS4ERR_CB_PATH_DOWN:
				nfs4_schedule_state_recovery(clp);
		}
		return;
	}
	spin_lock(&clp->cl_lock);
	if (time_before(clp->cl_last_renewal,timestamp))
		clp->cl_last_renewal = timestamp;
	spin_unlock(&clp->cl_lock);
}

static const struct rpc_call_ops nfs4_renew_ops = {
	.rpc_call_done = nfs4_renew_done,
};

int nfs4_proc_async_renew(struct nfs_client *clp, struct rpc_cred *cred)
{
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_RENEW],
		.rpc_argp	= clp,
		.rpc_cred	= cred,
	};

	return rpc_call_async(clp->cl_rpcclient, &msg, RPC_TASK_SOFT,
			&nfs4_renew_ops, (void *)jiffies);
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
	spin_lock(&clp->cl_lock);
	if (time_before(clp->cl_last_renewal,now))
		clp->cl_last_renewal = now;
	spin_unlock(&clp->cl_lock);
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

static void buf_to_pages(const void *buf, size_t buflen,
		struct page **pages, unsigned int *pgbase)
{
	const void *p = buf;

	*pgbase = offset_in_page(buf);
	p -= *pgbase;
	while (p < buf + buflen) {
		*(pages++) = virt_to_page(p);
		p += PAGE_CACHE_SIZE;
	}
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

static ssize_t __nfs4_get_acl_uncached(struct inode *inode, void *buf, size_t buflen)
{
	struct page *pages[NFS4ACL_MAXPAGES];
	struct nfs_getaclargs args = {
		.fh = NFS_FH(inode),
		.acl_pages = pages,
		.acl_len = buflen,
	};
	size_t resp_len = buflen;
	void *resp_buf;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_GETACL],
		.rpc_argp = &args,
		.rpc_resp = &resp_len,
	};
	struct page *localpage = NULL;
	int ret;

	if (buflen < PAGE_SIZE) {
		/* As long as we're doing a round trip to the server anyway,
		 * let's be prepared for a page of acl data. */
		localpage = alloc_page(GFP_KERNEL);
		resp_buf = page_address(localpage);
		if (localpage == NULL)
			return -ENOMEM;
		args.acl_pages[0] = localpage;
		args.acl_pgbase = 0;
		resp_len = args.acl_len = PAGE_SIZE;
	} else {
		resp_buf = buf;
		buf_to_pages(buf, buflen, args.acl_pages, &args.acl_pgbase);
	}
	ret = rpc_call_sync(NFS_CLIENT(inode), &msg, 0);
	if (ret)
		goto out_free;
	if (resp_len > args.acl_len)
		nfs4_write_cached_acl(inode, NULL, resp_len);
	else
		nfs4_write_cached_acl(inode, resp_buf, resp_len);
	if (buf) {
		ret = -ERANGE;
		if (resp_len > buflen)
			goto out_free;
		if (localpage)
			memcpy(buf, resp_buf, resp_len);
	}
	ret = resp_len;
out_free:
	if (localpage)
		__free_page(localpage);
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
	ret = nfs4_read_cached_acl(inode, buf, buflen);
	if (ret != -ENOENT)
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
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_SETACL],
		.rpc_argp	= &arg,
		.rpc_resp	= NULL,
	};
	int ret;

	if (!nfs4_server_supports_acls(server))
		return -EOPNOTSUPP;
	nfs_inode_return_delegation(inode);
	buf_to_pages(buf, buflen, arg.acl_pages, &arg.acl_pgbase);
	ret = rpc_call_sync(NFS_CLIENT(inode), &msg, 0);
	nfs_zap_caches(inode);
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
nfs4_async_handle_error(struct rpc_task *task, const struct nfs_server *server)
{
	struct nfs_client *clp = server->nfs_client;

	if (!clp || task->tk_status >= 0)
		return 0;
	switch(task->tk_status) {
		case -NFS4ERR_STALE_CLIENTID:
		case -NFS4ERR_STALE_STATEID:
		case -NFS4ERR_EXPIRED:
			rpc_sleep_on(&clp->cl_rpcwaitq, task, NULL, NULL);
			nfs4_schedule_state_recovery(clp);
			if (test_bit(NFS4CLNT_STATE_RECOVER, &clp->cl_state) == 0)
				rpc_wake_up_task(task);
			task->tk_status = 0;
			return -EAGAIN;
		case -NFS4ERR_DELAY:
			nfs_inc_server_stats((struct nfs_server *) server,
						NFSIOS_DELAY);
		case -NFS4ERR_GRACE:
			rpc_delay(task, NFS4_POLL_RETRY_MAX);
			task->tk_status = 0;
			return -EAGAIN;
		case -NFS4ERR_OLD_STATEID:
			task->tk_status = 0;
			return -EAGAIN;
	}
	task->tk_status = nfs4_map_errors(task->tk_status);
	return 0;
}

static int nfs4_wait_bit_interruptible(void *word)
{
	if (signal_pending(current))
		return -ERESTARTSYS;
	schedule();
	return 0;
}

static int nfs4_wait_clnt_recover(struct rpc_clnt *clnt, struct nfs_client *clp)
{
	sigset_t oldset;
	int res;

	might_sleep();

	rwsem_acquire(&clp->cl_sem.dep_map, 0, 0, _RET_IP_);

	rpc_clnt_sigmask(clnt, &oldset);
	res = wait_on_bit(&clp->cl_state, NFS4CLNT_STATE_RECOVER,
			nfs4_wait_bit_interruptible,
			TASK_INTERRUPTIBLE);
	rpc_clnt_sigunmask(clnt, &oldset);

	rwsem_release(&clp->cl_sem.dep_map, 1, _RET_IP_);
	return res;
}

static int nfs4_delay(struct rpc_clnt *clnt, long *timeout)
{
	sigset_t oldset;
	int res = 0;

	might_sleep();

	if (*timeout <= 0)
		*timeout = NFS4_POLL_RETRY_MIN;
	if (*timeout > NFS4_POLL_RETRY_MAX)
		*timeout = NFS4_POLL_RETRY_MAX;
	rpc_clnt_sigmask(clnt, &oldset);
	if (clnt->cl_intr) {
		schedule_timeout_interruptible(*timeout);
		if (signalled())
			res = -ERESTARTSYS;
	} else
		schedule_timeout_uninterruptible(*timeout);
	rpc_clnt_sigunmask(clnt, &oldset);
	*timeout <<= 1;
	return res;
}

/* This is the error handling routine for processes that are allowed
 * to sleep.
 */
int nfs4_handle_exception(const struct nfs_server *server, int errorcode, struct nfs4_exception *exception)
{
	struct nfs_client *clp = server->nfs_client;
	int ret = errorcode;

	exception->retry = 0;
	switch(errorcode) {
		case 0:
			return 0;
		case -NFS4ERR_STALE_CLIENTID:
		case -NFS4ERR_STALE_STATEID:
		case -NFS4ERR_EXPIRED:
			nfs4_schedule_state_recovery(clp);
			ret = nfs4_wait_clnt_recover(server->client, clp);
			if (ret == 0)
				exception->retry = 1;
			break;
		case -NFS4ERR_FILE_OPEN:
		case -NFS4ERR_GRACE:
		case -NFS4ERR_DELAY:
			ret = nfs4_delay(server->client, &exception->timeout);
			if (ret != 0)
				break;
		case -NFS4ERR_OLD_STATEID:
			exception->retry = 1;
	}
	/* We failed to handle the error */
	return nfs4_map_errors(ret);
}

int nfs4_proc_setclientid(struct nfs_client *clp, u32 program, unsigned short port, struct rpc_cred *cred)
{
	nfs4_verifier sc_verifier;
	struct nfs4_setclientid setclientid = {
		.sc_verifier = &sc_verifier,
		.sc_prog = program,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SETCLIENTID],
		.rpc_argp = &setclientid,
		.rpc_resp = clp,
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
				sizeof(setclientid.sc_name), "%s/%u.%u.%u.%u %s %u",
				clp->cl_ipaddr, NIPQUAD(clp->cl_addr.sin_addr),
				cred->cr_ops->cr_name,
				clp->cl_id_uniquifier);
		setclientid.sc_netid_len = scnprintf(setclientid.sc_netid,
				sizeof(setclientid.sc_netid), "tcp");
		setclientid.sc_uaddr_len = scnprintf(setclientid.sc_uaddr,
				sizeof(setclientid.sc_uaddr), "%s.%d.%d",
				clp->cl_ipaddr, port >> 8, port & 255);

		status = rpc_call_sync(clp->cl_rpcclient, &msg, 0);
		if (status != -NFS4ERR_CLID_INUSE)
			break;
		if (signalled())
			break;
		if (loop++ & 1)
			ssleep(clp->cl_lease_time + 1);
		else
			if (++clp->cl_id_uniquifier == 0)
				break;
	}
	return status;
}

static int _nfs4_proc_setclientid_confirm(struct nfs_client *clp, struct rpc_cred *cred)
{
	struct nfs_fsinfo fsinfo;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SETCLIENTID_CONFIRM],
		.rpc_argp = clp,
		.rpc_resp = &fsinfo,
		.rpc_cred = cred,
	};
	unsigned long now;
	int status;

	now = jiffies;
	status = rpc_call_sync(clp->cl_rpcclient, &msg, 0);
	if (status == 0) {
		spin_lock(&clp->cl_lock);
		clp->cl_lease_time = fsinfo.lease_time * HZ;
		clp->cl_last_renewal = now;
		clear_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state);
		spin_unlock(&clp->cl_lock);
	}
	return status;
}

int nfs4_proc_setclientid_confirm(struct nfs_client *clp, struct rpc_cred *cred)
{
	long timeout;
	int err;
	do {
		err = _nfs4_proc_setclientid_confirm(clp, cred);
		switch (err) {
			case 0:
				return err;
			case -NFS4ERR_RESOURCE:
				/* The IBM lawyers misread another document! */
			case -NFS4ERR_DELAY:
				err = nfs4_delay(clp->cl_rpcclient, &timeout);
		}
	} while (err == 0);
	return err;
}

struct nfs4_delegreturndata {
	struct nfs4_delegreturnargs args;
	struct nfs4_delegreturnres res;
	struct nfs_fh fh;
	nfs4_stateid stateid;
	struct rpc_cred *cred;
	unsigned long timestamp;
	struct nfs_fattr fattr;
	int rpc_status;
};

static void nfs4_delegreturn_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs4_delegreturndata *data = calldata;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_DELEGRETURN],
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = data->cred,
	};
	nfs_fattr_init(data->res.fattr);
	rpc_call_setup(task, &msg, 0);
}

static void nfs4_delegreturn_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_delegreturndata *data = calldata;
	data->rpc_status = task->tk_status;
	if (data->rpc_status == 0)
		renew_lease(data->res.server, data->timestamp);
}

static void nfs4_delegreturn_release(void *calldata)
{
	struct nfs4_delegreturndata *data = calldata;

	put_rpccred(data->cred);
	kfree(calldata);
}

static const struct rpc_call_ops nfs4_delegreturn_ops = {
	.rpc_call_prepare = nfs4_delegreturn_prepare,
	.rpc_call_done = nfs4_delegreturn_done,
	.rpc_release = nfs4_delegreturn_release,
};

static int _nfs4_proc_delegreturn(struct inode *inode, struct rpc_cred *cred, const nfs4_stateid *stateid)
{
	struct nfs4_delegreturndata *data;
	struct nfs_server *server = NFS_SERVER(inode);
	struct rpc_task *task;
	int status;

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;
	data->args.fhandle = &data->fh;
	data->args.stateid = &data->stateid;
	data->args.bitmask = server->attr_bitmask;
	nfs_copy_fh(&data->fh, NFS_FH(inode));
	memcpy(&data->stateid, stateid, sizeof(data->stateid));
	data->res.fattr = &data->fattr;
	data->res.server = server;
	data->cred = get_rpccred(cred);
	data->timestamp = jiffies;
	data->rpc_status = 0;

	task = rpc_run_task(NFS_CLIENT(inode), RPC_TASK_ASYNC, &nfs4_delegreturn_ops, data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	status = nfs4_wait_for_completion_rpc_task(task);
	if (status == 0) {
		status = data->rpc_status;
		if (status == 0)
			nfs_post_op_update_inode(inode, &data->fattr);
	}
	rpc_put_task(task);
	return status;
}

int nfs4_proc_delegreturn(struct inode *inode, struct rpc_cred *cred, const nfs4_stateid *stateid)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs4_exception exception = { };
	int err;
	do {
		err = _nfs4_proc_delegreturn(inode, cred, stateid);
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
	schedule_timeout_interruptible(timeout);
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

	down_read(&clp->cl_sem);
	arg.lock_owner.clientid = clp->cl_clientid;
	status = nfs4_set_lock_state(state, request);
	if (status != 0)
		goto out;
	lsp = request->fl_u.nfs4_fl.owner;
	arg.lock_owner.id = lsp->ls_id; 
	status = rpc_call_sync(server->client, &msg, 0);
	switch (status) {
		case 0:
			request->fl_type = F_UNLCK;
			break;
		case -NFS4ERR_DENIED:
			status = 0;
	}
out:
	up_read(&clp->cl_sem);
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

	p = kmalloc(sizeof(*p), GFP_KERNEL);
	if (p == NULL)
		return NULL;
	p->arg.fh = NFS_FH(inode);
	p->arg.fl = &p->fl;
	p->arg.seqid = seqid;
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

	if (RPC_ASSASSINATED(task))
		return;
	nfs_increment_lock_seqid(task->tk_status, calldata->arg.seqid);
	switch (task->tk_status) {
		case 0:
			memcpy(calldata->lsp->ls_stateid.data,
					calldata->res.stateid.data,
					sizeof(calldata->lsp->ls_stateid.data));
			renew_lease(calldata->server, calldata->timestamp);
			break;
		case -NFS4ERR_STALE_STATEID:
		case -NFS4ERR_EXPIRED:
			break;
		default:
			if (nfs4_async_handle_error(task, calldata->server) == -EAGAIN)
				rpc_restart_call(task);
	}
}

static void nfs4_locku_prepare(struct rpc_task *task, void *data)
{
	struct nfs4_unlockdata *calldata = data;
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_LOCKU],
		.rpc_argp       = &calldata->arg,
		.rpc_resp       = &calldata->res,
		.rpc_cred	= calldata->lsp->ls_state->owner->so_cred,
	};

	if (nfs_wait_on_sequence(calldata->arg.seqid, task) != 0)
		return;
	if ((calldata->lsp->ls_flags & NFS_LOCK_INITIALIZED) == 0) {
		/* Note: exit _without_ running nfs4_locku_done */
		task->tk_action = NULL;
		return;
	}
	calldata->timestamp = jiffies;
	rpc_call_setup(task, &msg, 0);
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

	data = nfs4_alloc_unlockdata(fl, ctx, lsp, seqid);
	if (data == NULL) {
		nfs_free_seqid(seqid);
		return ERR_PTR(-ENOMEM);
	}

	return rpc_run_task(NFS_CLIENT(lsp->ls_state->inode), RPC_TASK_ASYNC, &nfs4_locku_ops, data);
}

static int nfs4_proc_unlck(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct nfs_seqid *seqid;
	struct nfs4_lock_state *lsp;
	struct rpc_task *task;
	int status = 0;

	status = nfs4_set_lock_state(state, request);
	/* Unlock _before_ we do the RPC call */
	request->fl_flags |= FL_EXISTS;
	if (do_vfs_lock(request->fl_file, request) == -ENOENT)
		goto out;
	if (status != 0)
		goto out;
	/* Is this a delegated lock? */
	if (test_bit(NFS_DELEGATED_STATE, &state->flags))
		goto out;
	lsp = request->fl_u.nfs4_fl.owner;
	seqid = nfs_alloc_seqid(&lsp->ls_seqid);
	status = -ENOMEM;
	if (seqid == NULL)
		goto out;
	task = nfs4_do_unlck(request, request->fl_file->private_data, lsp, seqid);
	status = PTR_ERR(task);
	if (IS_ERR(task))
		goto out;
	status = nfs4_wait_for_completion_rpc_task(task);
	rpc_put_task(task);
out:
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
};

static struct nfs4_lockdata *nfs4_alloc_lockdata(struct file_lock *fl,
		struct nfs_open_context *ctx, struct nfs4_lock_state *lsp)
{
	struct nfs4_lockdata *p;
	struct inode *inode = lsp->ls_state->inode;
	struct nfs_server *server = NFS_SERVER(inode);

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (p == NULL)
		return NULL;

	p->arg.fh = NFS_FH(inode);
	p->arg.fl = &p->fl;
	p->arg.lock_seqid = nfs_alloc_seqid(&lsp->ls_seqid);
	if (p->arg.lock_seqid == NULL)
		goto out_free;
	p->arg.lock_stateid = &lsp->ls_stateid;
	p->arg.lock_owner.clientid = server->nfs_client->cl_clientid;
	p->arg.lock_owner.id = lsp->ls_id;
	p->lsp = lsp;
	atomic_inc(&lsp->ls_count);
	p->ctx = get_nfs_open_context(ctx);
	memcpy(&p->fl, fl, sizeof(p->fl));
	return p;
out_free:
	kfree(p);
	return NULL;
}

static void nfs4_lock_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs4_lockdata *data = calldata;
	struct nfs4_state *state = data->lsp->ls_state;
	struct nfs4_state_owner *sp = state->owner;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LOCK],
		.rpc_argp = &data->arg,
		.rpc_resp = &data->res,
		.rpc_cred = sp->so_cred,
	};

	if (nfs_wait_on_sequence(data->arg.lock_seqid, task) != 0)
		return;
	dprintk("%s: begin!\n", __FUNCTION__);
	/* Do we need to do an open_to_lock_owner? */
	if (!(data->arg.lock_seqid->sequence->flags & NFS_SEQID_CONFIRMED)) {
		data->arg.open_seqid = nfs_alloc_seqid(&sp->so_seqid);
		if (data->arg.open_seqid == NULL) {
			data->rpc_status = -ENOMEM;
			task->tk_action = NULL;
			goto out;
		}
		data->arg.open_stateid = &state->stateid;
		data->arg.new_lock_owner = 1;
	}
	data->timestamp = jiffies;
	rpc_call_setup(task, &msg, 0);
out:
	dprintk("%s: done!, ret = %d\n", __FUNCTION__, data->rpc_status);
}

static void nfs4_lock_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_lockdata *data = calldata;

	dprintk("%s: begin!\n", __FUNCTION__);

	data->rpc_status = task->tk_status;
	if (RPC_ASSASSINATED(task))
		goto out;
	if (data->arg.new_lock_owner != 0) {
		nfs_increment_open_seqid(data->rpc_status, data->arg.open_seqid);
		if (data->rpc_status == 0)
			nfs_confirm_seqid(&data->lsp->ls_seqid, 0);
		else
			goto out;
	}
	if (data->rpc_status == 0) {
		memcpy(data->lsp->ls_stateid.data, data->res.stateid.data,
					sizeof(data->lsp->ls_stateid.data));
		data->lsp->ls_flags |= NFS_LOCK_INITIALIZED;
		renew_lease(NFS_SERVER(data->ctx->dentry->d_inode), data->timestamp);
	}
	nfs_increment_lock_seqid(data->rpc_status, data->arg.lock_seqid);
out:
	dprintk("%s: done, ret = %d!\n", __FUNCTION__, data->rpc_status);
}

static void nfs4_lock_release(void *calldata)
{
	struct nfs4_lockdata *data = calldata;

	dprintk("%s: begin!\n", __FUNCTION__);
	if (data->arg.open_seqid != NULL)
		nfs_free_seqid(data->arg.open_seqid);
	if (data->cancelled != 0) {
		struct rpc_task *task;
		task = nfs4_do_unlck(&data->fl, data->ctx, data->lsp,
				data->arg.lock_seqid);
		if (!IS_ERR(task))
			rpc_put_task(task);
		dprintk("%s: cancelling lock!\n", __FUNCTION__);
	} else
		nfs_free_seqid(data->arg.lock_seqid);
	nfs4_put_lock_state(data->lsp);
	put_nfs_open_context(data->ctx);
	kfree(data);
	dprintk("%s: done!\n", __FUNCTION__);
}

static const struct rpc_call_ops nfs4_lock_ops = {
	.rpc_call_prepare = nfs4_lock_prepare,
	.rpc_call_done = nfs4_lock_done,
	.rpc_release = nfs4_lock_release,
};

static int _nfs4_do_setlk(struct nfs4_state *state, int cmd, struct file_lock *fl, int reclaim)
{
	struct nfs4_lockdata *data;
	struct rpc_task *task;
	int ret;

	dprintk("%s: begin!\n", __FUNCTION__);
	data = nfs4_alloc_lockdata(fl, fl->fl_file->private_data,
			fl->fl_u.nfs4_fl.owner);
	if (data == NULL)
		return -ENOMEM;
	if (IS_SETLKW(cmd))
		data->arg.block = 1;
	if (reclaim != 0)
		data->arg.reclaim = 1;
	task = rpc_run_task(NFS_CLIENT(state->inode), RPC_TASK_ASYNC,
			&nfs4_lock_ops, data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	ret = nfs4_wait_for_completion_rpc_task(task);
	if (ret == 0) {
		ret = data->rpc_status;
		if (ret == -NFS4ERR_DENIED)
			ret = -EAGAIN;
	} else
		data->cancelled = 1;
	rpc_put_task(task);
	dprintk("%s: done, ret = %d!\n", __FUNCTION__, ret);
	return ret;
}

static int nfs4_lock_reclaim(struct nfs4_state *state, struct file_lock *request)
{
	struct nfs_server *server = NFS_SERVER(state->inode);
	struct nfs4_exception exception = { };
	int err;

	do {
		/* Cache the lock if possible... */
		if (test_bit(NFS_DELEGATED_STATE, &state->flags) != 0)
			return 0;
		err = _nfs4_do_setlk(state, F_SETLK, request, 1);
		if (err != -NFS4ERR_DELAY)
			break;
		nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);
	return err;
}

static int nfs4_lock_expired(struct nfs4_state *state, struct file_lock *request)
{
	struct nfs_server *server = NFS_SERVER(state->inode);
	struct nfs4_exception exception = { };
	int err;

	err = nfs4_set_lock_state(state, request);
	if (err != 0)
		return err;
	do {
		if (test_bit(NFS_DELEGATED_STATE, &state->flags) != 0)
			return 0;
		err = _nfs4_do_setlk(state, F_SETLK, request, 0);
		if (err != -NFS4ERR_DELAY)
			break;
		nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);
	return err;
}

static int _nfs4_proc_setlk(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct nfs_client *clp = state->owner->so_client;
	unsigned char fl_flags = request->fl_flags;
	int status;

	/* Is this a delegated open? */
	status = nfs4_set_lock_state(state, request);
	if (status != 0)
		goto out;
	request->fl_flags |= FL_ACCESS;
	status = do_vfs_lock(request->fl_file, request);
	if (status < 0)
		goto out;
	down_read(&clp->cl_sem);
	if (test_bit(NFS_DELEGATED_STATE, &state->flags)) {
		struct nfs_inode *nfsi = NFS_I(state->inode);
		/* Yes: cache locks! */
		down_read(&nfsi->rwsem);
		/* ...but avoid races with delegation recall... */
		if (test_bit(NFS_DELEGATED_STATE, &state->flags)) {
			request->fl_flags = fl_flags & ~FL_SLEEP;
			status = do_vfs_lock(request->fl_file, request);
			up_read(&nfsi->rwsem);
			goto out_unlock;
		}
		up_read(&nfsi->rwsem);
	}
	status = _nfs4_do_setlk(state, cmd, request, 0);
	if (status != 0)
		goto out_unlock;
	/* Note: we always want to sleep here! */
	request->fl_flags = fl_flags | FL_SLEEP;
	if (do_vfs_lock(request->fl_file, request) < 0)
		printk(KERN_WARNING "%s: VFS is out of sync with lock manager!\n", __FUNCTION__);
out_unlock:
	up_read(&clp->cl_sem);
out:
	request->fl_flags = fl_flags;
	return status;
}

static int nfs4_proc_setlk(struct nfs4_state *state, int cmd, struct file_lock *request)
{
	struct nfs4_exception exception = { };
	int err;

	do {
		err = nfs4_handle_exception(NFS_SERVER(state->inode),
				_nfs4_proc_setlk(state, cmd, request),
				&exception);
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
	ctx = (struct nfs_open_context *)filp->private_data;
	state = ctx->state;

	if (request->fl_start < 0 || request->fl_end < 0)
		return -EINVAL;

	if (IS_GETLK(cmd))
		return nfs4_proc_getlk(state, F_GETLK, request);

	if (!(IS_SETLK(cmd) || IS_SETLKW(cmd)))
		return -EINVAL;

	if (request->fl_type == F_UNLCK)
		return nfs4_proc_unlck(state, cmd, request);

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
		err = _nfs4_do_setlk(state, F_SETLK, fl, 0);
		if (err != -NFS4ERR_DELAY)
			break;
		err = nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);
out:
	return err;
}

#define XATTR_NAME_NFSV4_ACL "system.nfs4_acl"

int nfs4_setxattr(struct dentry *dentry, const char *key, const void *buf,
		size_t buflen, int flags)
{
	struct inode *inode = dentry->d_inode;

	if (strcmp(key, XATTR_NAME_NFSV4_ACL) != 0)
		return -EOPNOTSUPP;

	if (!S_ISREG(inode->i_mode) &&
	    (!S_ISDIR(inode->i_mode) || inode->i_mode & S_ISVTX))
		return -EPERM;

	return nfs4_proc_set_acl(inode, buf, buflen);
}

/* The getxattr man page suggests returning -ENODATA for unknown attributes,
 * and that's what we'll do for e.g. user attributes that haven't been set.
 * But we'll follow ext2/ext3's lead by returning -EOPNOTSUPP for unsupported
 * attributes in kernel-managed attribute namespaces. */
ssize_t nfs4_getxattr(struct dentry *dentry, const char *key, void *buf,
		size_t buflen)
{
	struct inode *inode = dentry->d_inode;

	if (strcmp(key, XATTR_NAME_NFSV4_ACL) != 0)
		return -EOPNOTSUPP;

	return nfs4_proc_get_acl(inode, buf, buflen);
}

ssize_t nfs4_listxattr(struct dentry *dentry, char *buf, size_t buflen)
{
	size_t len = strlen(XATTR_NAME_NFSV4_ACL) + 1;

	if (!nfs4_server_supports_acls(NFS_SERVER(dentry->d_inode)))
		return 0;
	if (buf && buflen < len)
		return -ERANGE;
	if (buf)
		memcpy(buf, XATTR_NAME_NFSV4_ACL, len);
	return len;
}

int nfs4_proc_fs_locations(struct inode *dir, struct qstr *name,
		struct nfs4_fs_locations *fs_locations, struct page *page)
{
	struct nfs_server *server = NFS_SERVER(dir);
	u32 bitmask[2] = {
		[0] = FATTR4_WORD0_FSID | FATTR4_WORD0_FS_LOCATIONS,
		[1] = FATTR4_WORD1_MOUNTED_ON_FILEID,
	};
	struct nfs4_fs_locations_arg args = {
		.dir_fh = NFS_FH(dir),
		.name = name,
		.page = page,
		.bitmask = bitmask,
	};
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_FS_LOCATIONS],
		.rpc_argp = &args,
		.rpc_resp = fs_locations,
	};
	int status;

	dprintk("%s: start\n", __FUNCTION__);
	nfs_fattr_init(&fs_locations->fattr);
	fs_locations->server = server;
	fs_locations->nlocations = 0;
	status = rpc_call_sync(server->client, &msg, 0);
	dprintk("%s: returned status = %d\n", __FUNCTION__, status);
	return status;
}

struct nfs4_state_recovery_ops nfs4_reboot_recovery_ops = {
	.recover_open	= nfs4_open_reclaim,
	.recover_lock	= nfs4_lock_reclaim,
};

struct nfs4_state_recovery_ops nfs4_network_partition_recovery_ops = {
	.recover_open	= nfs4_open_expired,
	.recover_lock	= nfs4_lock_expired,
};

static const struct inode_operations nfs4_file_inode_operations = {
	.permission	= nfs_permission,
	.getattr	= nfs_getattr,
	.setattr	= nfs_setattr,
	.getxattr	= nfs4_getxattr,
	.setxattr	= nfs4_setxattr,
	.listxattr	= nfs4_listxattr,
};

const struct nfs_rpc_ops nfs_v4_clientops = {
	.version	= 4,			/* protocol version */
	.dentry_ops	= &nfs4_dentry_operations,
	.dir_inode_ops	= &nfs4_dir_inode_operations,
	.file_inode_ops	= &nfs4_file_inode_operations,
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
	.file_open      = nfs_open,
	.file_release   = nfs_release,
	.lock		= nfs4_proc_lock,
	.clear_acl_cache = nfs4_zap_acl_attr,
};

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */

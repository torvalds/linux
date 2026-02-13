/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/nfs4.h>
#include <linux/nfs.h>
#include <linux/sunrpc/sched.h>
#include <linux/nfs_fs.h>
#include "internal.h"
#include "nfs4_fs.h"
#include "nfs40.h"
#include "nfs4session.h"
#include "nfs4trace.h"

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

static int nfs40_test_and_free_expired_stateid(struct nfs_server *server,
					       nfs4_stateid *stateid,
					       const struct cred *cred)
{
	return -NFS4ERR_BAD_STATEID;
}

/*
 * This operation also signals the server that this client is
 * performing migration recovery.  The server can stop returning
 * NFS4ERR_LEASE_MOVED to this client.  A RENEW operation is
 * appended to this compound to identify the client ID which is
 * performing recovery.
 */
static int _nfs40_proc_get_locations(struct nfs_server *server,
				     struct nfs_fh *fhandle,
				     struct nfs4_fs_locations *locations,
				     struct page *page, const struct cred *cred)
{
	struct rpc_clnt *clnt = server->client;
	struct nfs_client *clp = server->nfs_client;
	u32 bitmask[2] = {
		[0] = FATTR4_WORD0_FSID | FATTR4_WORD0_FS_LOCATIONS,
	};
	struct nfs4_fs_locations_arg args = {
		.clientid	= clp->cl_clientid,
		.fh		= fhandle,
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

	nfs_fattr_init(locations->fattr);
	locations->server = server;
	locations->nlocations = 0;

	nfs4_init_sequence(clp, &args.seq_args, &res.seq_res, 0, 1);
	status = nfs4_call_sync_sequence(clnt, server, &msg,
					&args.seq_args, &res.seq_res);
	if (status)
		return status;

	renew_lease(server, now);
	return 0;
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

	nfs4_init_sequence(clp, &args.seq_args, &res.seq_res, 0, 1);
	status = nfs4_call_sync_sequence(clnt, server, &msg,
						&args.seq_args, &res.seq_res);
	nfs_free_fhandle(res.fh);
	if (status)
		return status;

	do_renew_lease(clp, now);
	return 0;
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
	struct nfs_client *clp = server->nfs_client;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_RELEASE_LOCKOWNER],
	};

	if (clp->cl_mvops->minor_version != 0)
		return;

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return;
	data->lsp = lsp;
	data->server = server;
	data->args.lock_owner.clientid = clp->cl_clientid;
	data->args.lock_owner.id = lsp->ls_seqid.owner_id;
	data->args.lock_owner.s_dev = server->s_dev;

	msg.rpc_argp = &data->args;
	msg.rpc_resp = &data->res;
	nfs4_init_sequence(clp, &data->args.seq_args, &data->res.seq_res, 0, 0);
	rpc_call_async(server->client, &msg, 0, &nfs4_release_lockowner_ops, data);
}

static const struct rpc_call_ops nfs40_call_sync_ops = {
	.rpc_call_prepare = nfs40_call_sync_prepare,
	.rpc_call_done = nfs40_call_sync_done,
};

static const struct nfs4_sequence_slot_ops nfs40_sequence_slot_ops = {
	.process = nfs40_sequence_done,
	.done = nfs40_sequence_done,
	.free_slot = nfs40_sequence_free_slot,
};

static const struct nfs4_state_recovery_ops nfs40_reboot_recovery_ops = {
	.owner_flag_bit = NFS_OWNER_RECLAIM_REBOOT,
	.state_flag_bit	= NFS_STATE_RECLAIM_REBOOT,
	.recover_open	= nfs4_open_reclaim,
	.recover_lock	= nfs4_lock_reclaim,
	.establish_clid = nfs4_init_clientid,
	.detect_trunking = nfs40_discover_server_trunking,
};

static const struct nfs4_state_recovery_ops nfs40_nograce_recovery_ops = {
	.owner_flag_bit = NFS_OWNER_RECLAIM_NOGRACE,
	.state_flag_bit	= NFS_STATE_RECLAIM_NOGRACE,
	.recover_open	= nfs40_open_expired,
	.recover_lock	= nfs4_lock_expired,
	.establish_clid = nfs4_init_clientid,
};

static const struct nfs4_state_maintenance_ops nfs40_state_renewal_ops = {
	.sched_state_renewal = nfs4_proc_async_renew,
	.get_state_renewal_cred = nfs4_get_renew_cred,
	.renew_lease = nfs4_proc_renew,
};

static const struct nfs4_mig_recovery_ops nfs40_mig_recovery_ops = {
	.get_locations = _nfs40_proc_get_locations,
	.fsid_present = _nfs40_proc_fsid_present,
};

const struct nfs4_minor_version_ops nfs_v4_0_minor_ops = {
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
	.sequence_slot_ops = &nfs40_sequence_slot_ops,
	.reboot_recovery_ops = &nfs40_reboot_recovery_ops,
	.nograce_recovery_ops = &nfs40_nograce_recovery_ops,
	.state_renewal_ops = &nfs40_state_renewal_ops,
	.mig_recovery_ops = &nfs40_mig_recovery_ops,
};

/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/nfs4.h>
#include <linux/nfs.h>
#include <linux/sunrpc/sched.h>
#include <linux/nfs_fs.h>
#include "nfs4_fs.h"

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

const struct rpc_call_ops nfs40_call_sync_ops = {
	.rpc_call_prepare = nfs40_call_sync_prepare,
	.rpc_call_done = nfs40_call_sync_done,
};

const struct nfs4_state_recovery_ops nfs40_reboot_recovery_ops = {
	.owner_flag_bit = NFS_OWNER_RECLAIM_REBOOT,
	.state_flag_bit	= NFS_STATE_RECLAIM_REBOOT,
	.recover_open	= nfs4_open_reclaim,
	.recover_lock	= nfs4_lock_reclaim,
	.establish_clid = nfs4_init_clientid,
	.detect_trunking = nfs40_discover_server_trunking,
};

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

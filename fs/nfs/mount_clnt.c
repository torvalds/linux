/*
 * In-kernel MOUNT protocol client
 *
 * Copyright (C) 1997, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/uio.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/sched.h>
#include <linux/nfs_fs.h>
#include "internal.h"

#ifdef RPC_DEBUG
# define NFSDBG_FACILITY	NFSDBG_MOUNT
#endif

static struct rpc_program	mnt_program;

struct mnt_fhstatus {
	u32 status;
	struct nfs_fh *fh;
};

/**
 * nfs_mount - Obtain an NFS file handle for the given host and path
 * @info: pointer to mount request arguments
 *
 * Uses default timeout parameters specified by underlying transport.
 */
int nfs_mount(struct nfs_mount_request *info)
{
	struct mnt_fhstatus	result = {
		.fh		= info->fh
	};
	struct rpc_message msg	= {
		.rpc_argp	= info->dirpath,
		.rpc_resp	= &result,
	};
	struct rpc_create_args args = {
		.protocol	= info->protocol,
		.address	= info->sap,
		.addrsize	= info->salen,
		.servername	= info->hostname,
		.program	= &mnt_program,
		.version	= info->version,
		.authflavor	= RPC_AUTH_UNIX,
	};
	struct rpc_clnt		*mnt_clnt;
	int			status;

	dprintk("NFS: sending MNT request for %s:%s\n",
		(info->hostname ? info->hostname : "server"),
			info->dirpath);

	if (info->noresvport)
		args.flags |= RPC_CLNT_CREATE_NONPRIVPORT;

	mnt_clnt = rpc_create(&args);
	if (IS_ERR(mnt_clnt))
		goto out_clnt_err;

	if (info->version == NFS_MNT3_VERSION)
		msg.rpc_proc = &mnt_clnt->cl_procinfo[MOUNTPROC3_MNT];
	else
		msg.rpc_proc = &mnt_clnt->cl_procinfo[MNTPROC_MNT];

	status = rpc_call_sync(mnt_clnt, &msg, 0);
	rpc_shutdown_client(mnt_clnt);

	if (status < 0)
		goto out_call_err;
	if (result.status != 0)
		goto out_mnt_err;

	dprintk("NFS: MNT request succeeded\n");
	status = 0;

out:
	return status;

out_clnt_err:
	status = PTR_ERR(mnt_clnt);
	dprintk("NFS: failed to create RPC client, status=%d\n", status);
	goto out;

out_call_err:
	dprintk("NFS: failed to start MNT request, status=%d\n", status);
	goto out;

out_mnt_err:
	dprintk("NFS: MNT server returned result %d\n", result.status);
	status = nfs_stat_to_errno(result.status);
	goto out;
}

/*
 * XDR encode/decode functions for MOUNT
 */
static int xdr_encode_dirpath(struct rpc_rqst *req, __be32 *p,
			      const char *path)
{
	p = xdr_encode_string(p, path);

	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

static int xdr_decode_fhstatus(struct rpc_rqst *req, __be32 *p,
			       struct mnt_fhstatus *res)
{
	struct nfs_fh *fh = res->fh;

	if ((res->status = ntohl(*p++)) == 0) {
		fh->size = NFS2_FHSIZE;
		memcpy(fh->data, p, NFS2_FHSIZE);
	}
	return 0;
}

static int xdr_decode_fhstatus3(struct rpc_rqst *req, __be32 *p,
				struct mnt_fhstatus *res)
{
	struct nfs_fh *fh = res->fh;
	unsigned size;

	if ((res->status = ntohl(*p++)) == 0) {
		size = ntohl(*p++);
		if (size <= NFS3_FHSIZE && size != 0) {
			fh->size = size;
			memcpy(fh->data, p, size);
		} else
			res->status = -EBADHANDLE;
	}
	return 0;
}

#define MNT_dirpath_sz		(1 + 256)
#define MNT_fhstatus_sz		(1 + 8)
#define MNT_fhstatus3_sz	(1 + 16)

static struct rpc_procinfo mnt_procedures[] = {
	[MNTPROC_MNT] = {
		.p_proc		= MNTPROC_MNT,
		.p_encode	= (kxdrproc_t) xdr_encode_dirpath,
		.p_decode	= (kxdrproc_t) xdr_decode_fhstatus,
		.p_arglen	= MNT_dirpath_sz,
		.p_replen	= MNT_fhstatus_sz,
		.p_statidx	= MNTPROC_MNT,
		.p_name		= "MOUNT",
	},
};

static struct rpc_procinfo mnt3_procedures[] = {
	[MOUNTPROC3_MNT] = {
		.p_proc		= MOUNTPROC3_MNT,
		.p_encode	= (kxdrproc_t) xdr_encode_dirpath,
		.p_decode	= (kxdrproc_t) xdr_decode_fhstatus3,
		.p_arglen	= MNT_dirpath_sz,
		.p_replen	= MNT_fhstatus3_sz,
		.p_statidx	= MOUNTPROC3_MNT,
		.p_name		= "MOUNT",
	},
};


static struct rpc_version mnt_version1 = {
	.number		= 1,
	.nrprocs	= 2,
	.procs		= mnt_procedures,
};

static struct rpc_version mnt_version3 = {
	.number		= 3,
	.nrprocs	= 2,
	.procs		= mnt3_procedures,
};

static struct rpc_version *mnt_version[] = {
	NULL,
	&mnt_version1,
	NULL,
	&mnt_version3,
};

static struct rpc_stat mnt_stats;

static struct rpc_program mnt_program = {
	.name		= "mount",
	.number		= NFS_MNT_PROGRAM,
	.nrvers		= ARRAY_SIZE(mnt_version),
	.version	= mnt_version,
	.stats		= &mnt_stats,
};

// SPDX-License-Identifier: GPL-2.0-only
/*
 * NFS server support for local clients to bypass network stack
 *
 * Copyright (C) 2014 Weston Andros Adamson <dros@primarydata.com>
 * Copyright (C) 2019 Trond Myklebust <trond.myklebust@hammerspace.com>
 * Copyright (C) 2024 Mike Snitzer <snitzer@hammerspace.com>
 * Copyright (C) 2024 NeilBrown <neilb@suse.de>
 */

#include <linux/exportfs.h>
#include <linux/sunrpc/svcauth.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs_common.h>
#include <linux/nfslocalio.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_xdr.h>
#include <linux/string.h>

#include "nfsd.h"
#include "vfs.h"
#include "netns.h"
#include "filecache.h"
#include "cache.h"

static const struct nfsd_localio_operations nfsd_localio_ops = {
	.nfsd_net_try_get  = nfsd_net_try_get,
	.nfsd_net_put  = nfsd_net_put,
	.nfsd_open_local_fh = nfsd_open_local_fh,
	.nfsd_file_put_local = nfsd_file_put_local,
	.nfsd_file_get = nfsd_file_get,
	.nfsd_file_put = nfsd_file_put,
	.nfsd_file_file = nfsd_file_file,
};

void nfsd_localio_ops_init(void)
{
	nfs_to = &nfsd_localio_ops;
}

/**
 * nfsd_open_local_fh - lookup a local filehandle @nfs_fh and map to nfsd_file
 *
 * @net: 'struct net' to get the proper nfsd_net required for LOCALIO access
 * @dom: 'struct auth_domain' required for LOCALIO access
 * @rpc_clnt: rpc_clnt that the client established
 * @cred: cred that the client established
 * @nfs_fh: filehandle to lookup
 * @fmode: fmode_t to use for open
 *
 * This function maps a local fh to a path on a local filesystem.
 * This is useful when the nfs client has the local server mounted - it can
 * avoid all the NFS overhead with reads, writes and commits.
 *
 * On successful return, returned nfsd_file will have its nf_net member
 * set. Caller (NFS client) is responsible for calling nfsd_net_put and
 * nfsd_file_put (via nfs_to_nfsd_file_put_local).
 */
struct nfsd_file *
nfsd_open_local_fh(struct net *net, struct auth_domain *dom,
		   struct rpc_clnt *rpc_clnt, const struct cred *cred,
		   const struct nfs_fh *nfs_fh, const fmode_t fmode)
{
	int mayflags = NFSD_MAY_LOCALIO;
	struct svc_cred rq_cred;
	struct svc_fh fh;
	struct nfsd_file *localio;
	__be32 beres;

	if (nfs_fh->size > NFS4_FHSIZE)
		return ERR_PTR(-EINVAL);

	/* nfs_fh -> svc_fh */
	fh_init(&fh, NFS4_FHSIZE);
	fh.fh_handle.fh_size = nfs_fh->size;
	memcpy(fh.fh_handle.fh_raw, nfs_fh->data, nfs_fh->size);

	if (fmode & FMODE_READ)
		mayflags |= NFSD_MAY_READ;
	if (fmode & FMODE_WRITE)
		mayflags |= NFSD_MAY_WRITE;

	svcauth_map_clnt_to_svc_cred_local(rpc_clnt, cred, &rq_cred);

	beres = nfsd_file_acquire_local(net, &rq_cred, dom,
					&fh, mayflags, &localio);
	if (beres)
		localio = ERR_PTR(nfs_stat_to_errno(be32_to_cpu(beres)));

	fh_put(&fh);
	if (rq_cred.cr_group_info)
		put_group_info(rq_cred.cr_group_info);

	return localio;
}
EXPORT_SYMBOL_GPL(nfsd_open_local_fh);

/*
 * UUID_IS_LOCAL XDR functions
 */

static __be32 localio_proc_null(struct svc_rqst *rqstp)
{
	return rpc_success;
}

struct localio_uuidarg {
	uuid_t			uuid;
};

static __be32 localio_proc_uuid_is_local(struct svc_rqst *rqstp)
{
	struct localio_uuidarg *argp = rqstp->rq_argp;
	struct net *net = SVC_NET(rqstp);
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	nfs_uuid_is_local(&argp->uuid, &nn->local_clients,
			  &nn->local_clients_lock,
			  net, rqstp->rq_client, THIS_MODULE);

	return rpc_success;
}

static bool localio_decode_uuidarg(struct svc_rqst *rqstp,
				   struct xdr_stream *xdr)
{
	struct localio_uuidarg *argp = rqstp->rq_argp;
	u8 uuid[UUID_SIZE];

	if (decode_opaque_fixed(xdr, uuid, UUID_SIZE))
		return false;
	import_uuid(&argp->uuid, uuid);

	return true;
}

static const struct svc_procedure localio_procedures1[] = {
	[LOCALIOPROC_NULL] = {
		.pc_func = localio_proc_null,
		.pc_decode = nfssvc_decode_voidarg,
		.pc_encode = nfssvc_encode_voidres,
		.pc_argsize = sizeof(struct nfsd_voidargs),
		.pc_ressize = sizeof(struct nfsd_voidres),
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = 0,
		.pc_name = "NULL",
	},
	[LOCALIOPROC_UUID_IS_LOCAL] = {
		.pc_func = localio_proc_uuid_is_local,
		.pc_decode = localio_decode_uuidarg,
		.pc_encode = nfssvc_encode_voidres,
		.pc_argsize = sizeof(struct localio_uuidarg),
		.pc_argzero = sizeof(struct localio_uuidarg),
		.pc_ressize = sizeof(struct nfsd_voidres),
		.pc_cachetype = RC_NOCACHE,
		.pc_name = "UUID_IS_LOCAL",
	},
};

#define LOCALIO_NR_PROCEDURES ARRAY_SIZE(localio_procedures1)
static DEFINE_PER_CPU_ALIGNED(unsigned long,
			      localio_count[LOCALIO_NR_PROCEDURES]);
const struct svc_version localio_version1 = {
	.vs_vers	= 1,
	.vs_nproc	= LOCALIO_NR_PROCEDURES,
	.vs_proc	= localio_procedures1,
	.vs_dispatch	= nfsd_dispatch,
	.vs_count	= localio_count,
	.vs_xdrsize	= XDR_QUADLEN(UUID_SIZE),
	.vs_hidden	= true,
};

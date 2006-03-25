/*
 * linux/fs/nfs/mount_clnt.c
 *
 * MOUNT client to support NFSroot.
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
#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/sched.h>
#include <linux/nfs_fs.h>

#ifdef RPC_DEBUG
# define NFSDBG_FACILITY	NFSDBG_ROOT
#endif

/*
#define MOUNT_PROGRAM		100005
#define MOUNT_VERSION		1
#define MOUNT_MNT		1
#define MOUNT_UMNT		3
 */

static struct rpc_clnt *	mnt_create(char *, struct sockaddr_in *,
								int, int);
static struct rpc_program	mnt_program;

struct mnt_fhstatus {
	unsigned int		status;
	struct nfs_fh *		fh;
};

/*
 * Obtain an NFS file handle for the given host and path
 */
int
nfsroot_mount(struct sockaddr_in *addr, char *path, struct nfs_fh *fh,
		int version, int protocol)
{
	struct rpc_clnt		*mnt_clnt;
	struct mnt_fhstatus	result = {
		.fh		= fh
	};
	struct rpc_message msg	= {
		.rpc_argp	= path,
		.rpc_resp	= &result,
	};
	char			hostname[32];
	int			status;

	dprintk("NFS:      nfs_mount(%08x:%s)\n",
			(unsigned)ntohl(addr->sin_addr.s_addr), path);

	sprintf(hostname, "%u.%u.%u.%u", NIPQUAD(addr->sin_addr.s_addr));
	mnt_clnt = mnt_create(hostname, addr, version, protocol);
	if (IS_ERR(mnt_clnt))
		return PTR_ERR(mnt_clnt);

	if (version == NFS_MNT3_VERSION)
		msg.rpc_proc = &mnt_clnt->cl_procinfo[MOUNTPROC3_MNT];
	else
		msg.rpc_proc = &mnt_clnt->cl_procinfo[MNTPROC_MNT];

	status = rpc_call_sync(mnt_clnt, &msg, 0);
	return status < 0? status : (result.status? -EACCES : 0);
}

static struct rpc_clnt *
mnt_create(char *hostname, struct sockaddr_in *srvaddr, int version,
		int protocol)
{
	struct rpc_xprt	*xprt;
	struct rpc_clnt	*clnt;

	xprt = xprt_create_proto(protocol, srvaddr, NULL);
	if (IS_ERR(xprt))
		return (struct rpc_clnt *)xprt;

	clnt = rpc_create_client(xprt, hostname,
				&mnt_program, version,
				RPC_AUTH_UNIX);
	if (!IS_ERR(clnt)) {
		clnt->cl_softrtry = 1;
		clnt->cl_oneshot  = 1;
		clnt->cl_intr = 1;
	}
	return clnt;
}

/*
 * XDR encode/decode functions for MOUNT
 */
static int
xdr_encode_dirpath(struct rpc_rqst *req, u32 *p, const char *path)
{
	p = xdr_encode_string(p, path);

	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

static int
xdr_decode_fhstatus(struct rpc_rqst *req, u32 *p, struct mnt_fhstatus *res)
{
	struct nfs_fh *fh = res->fh;

	if ((res->status = ntohl(*p++)) == 0) {
		fh->size = NFS2_FHSIZE;
		memcpy(fh->data, p, NFS2_FHSIZE);
	}
	return 0;
}

static int
xdr_decode_fhstatus3(struct rpc_rqst *req, u32 *p, struct mnt_fhstatus *res)
{
	struct nfs_fh *fh = res->fh;

	if ((res->status = ntohl(*p++)) == 0) {
		int size = ntohl(*p++);
		if (size <= NFS3_FHSIZE) {
			fh->size = size;
			memcpy(fh->data, p, size);
		} else
			res->status = -EBADHANDLE;
	}
	return 0;
}

#define MNT_dirpath_sz		(1 + 256)
#define MNT_fhstatus_sz		(1 + 8)

static struct rpc_procinfo	mnt_procedures[] = {
[MNTPROC_MNT] = {
	  .p_proc		= MNTPROC_MNT,
	  .p_encode		= (kxdrproc_t) xdr_encode_dirpath,	
	  .p_decode		= (kxdrproc_t) xdr_decode_fhstatus,
	  .p_bufsiz		= MNT_dirpath_sz << 2,
	  .p_statidx		= MNTPROC_MNT,
	  .p_name		= "MOUNT",
	},
};

static struct rpc_procinfo mnt3_procedures[] = {
[MOUNTPROC3_MNT] = {
	  .p_proc		= MOUNTPROC3_MNT,
	  .p_encode		= (kxdrproc_t) xdr_encode_dirpath,
	  .p_decode		= (kxdrproc_t) xdr_decode_fhstatus3,
	  .p_bufsiz		= MNT_dirpath_sz << 2,
	  .p_statidx		= MOUNTPROC3_MNT,
	  .p_name		= "MOUNT",
	},
};


static struct rpc_version	mnt_version1 = {
		.number		= 1,
		.nrprocs 	= 2,
		.procs 		= mnt_procedures
};

static struct rpc_version       mnt_version3 = {
		.number		= 3,
		.nrprocs	= 2,
		.procs		= mnt3_procedures
};

static struct rpc_version *	mnt_version[] = {
	NULL,
	&mnt_version1,
	NULL,
	&mnt_version3,
};

static struct rpc_stat		mnt_stats;

static struct rpc_program	mnt_program = {
	.name		= "mount",
	.number		= NFS_MNT_PROGRAM,
	.nrvers		= ARRAY_SIZE(mnt_version),
	.version	= mnt_version,
	.stats		= &mnt_stats,
};

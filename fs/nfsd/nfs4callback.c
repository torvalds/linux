/*
 *  linux/fs/nfsd/nfs4callback.c
 *
 *  Copyright (c) 2001 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Kendrick Smith <kmsmith@umich.edu>
 *  Andy Adamson <andros@umich.edu>
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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/inet.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/state.h>
#include <linux/sunrpc/sched.h>
#include <linux/nfs4.h>

#define NFSDDBG_FACILITY                NFSDDBG_PROC

#define NFSPROC4_CB_NULL 0
#define NFSPROC4_CB_COMPOUND 1

/* declarations */
static const struct rpc_call_ops nfs4_cb_null_ops;

/* Index of predefined Linux callback client operations */

enum {
        NFSPROC4_CLNT_CB_NULL = 0,
	NFSPROC4_CLNT_CB_RECALL,
};

enum nfs_cb_opnum4 {
	OP_CB_RECALL            = 4,
};

#define NFS4_MAXTAGLEN		20

#define NFS4_enc_cb_null_sz		0
#define NFS4_dec_cb_null_sz		0
#define cb_compound_enc_hdr_sz		4
#define cb_compound_dec_hdr_sz		(3 + (NFS4_MAXTAGLEN >> 2))
#define op_enc_sz			1
#define op_dec_sz			2
#define enc_nfs4_fh_sz			(1 + (NFS4_FHSIZE >> 2))
#define enc_stateid_sz			16
#define NFS4_enc_cb_recall_sz		(cb_compound_enc_hdr_sz +       \
					1 + enc_stateid_sz +            \
					enc_nfs4_fh_sz)

#define NFS4_dec_cb_recall_sz		(cb_compound_dec_hdr_sz  +      \
					op_dec_sz)

/*
* Generic encode routines from fs/nfs/nfs4xdr.c
*/
static inline u32 *
xdr_writemem(u32 *p, const void *ptr, int nbytes)
{
	int tmp = XDR_QUADLEN(nbytes);
	if (!tmp)
		return p;
	p[tmp-1] = 0;
	memcpy(p, ptr, nbytes);
	return p + tmp;
}

#define WRITE32(n)               *p++ = htonl(n)
#define WRITEMEM(ptr,nbytes)     do {                           \
	p = xdr_writemem(p, ptr, nbytes);                       \
} while (0)
#define RESERVE_SPACE(nbytes)   do {                            \
	p = xdr_reserve_space(xdr, nbytes);                     \
	if (!p) dprintk("NFSD: RESERVE_SPACE(%d) failed in function %s\n", (int) (nbytes), __FUNCTION__); \
	BUG_ON(!p);                                             \
} while (0)

/*
 * Generic decode routines from fs/nfs/nfs4xdr.c
 */
#define DECODE_TAIL                             \
	status = 0;                             \
out:                                            \
	return status;                          \
xdr_error:                                      \
	dprintk("NFSD: xdr error! (%s:%d)\n", __FILE__, __LINE__); \
	status = -EIO;                          \
	goto out

#define READ32(x)         (x) = ntohl(*p++)
#define READ64(x)         do {                  \
	(x) = (u64)ntohl(*p++) << 32;           \
	(x) |= ntohl(*p++);                     \
} while (0)
#define READTIME(x)       do {                  \
	p++;                                    \
	(x.tv_sec) = ntohl(*p++);               \
	(x.tv_nsec) = ntohl(*p++);              \
} while (0)
#define READ_BUF(nbytes)  do { \
	p = xdr_inline_decode(xdr, nbytes); \
	if (!p) { \
		dprintk("NFSD: %s: reply buffer overflowed in line %d.", \
			__FUNCTION__, __LINE__); \
		return -EIO; \
	} \
} while (0)

struct nfs4_cb_compound_hdr {
	int		status;
	u32		ident;
	u32		nops;
	u32		taglen;
	char *		tag;
};

static struct {
int stat;
int errno;
} nfs_cb_errtbl[] = {
	{ NFS4_OK,		0               },
	{ NFS4ERR_PERM,		EPERM           },
	{ NFS4ERR_NOENT,	ENOENT          },
	{ NFS4ERR_IO,		EIO             },
	{ NFS4ERR_NXIO,		ENXIO           },
	{ NFS4ERR_ACCESS,	EACCES          },
	{ NFS4ERR_EXIST,	EEXIST          },
	{ NFS4ERR_XDEV,		EXDEV           },
	{ NFS4ERR_NOTDIR,	ENOTDIR         },
	{ NFS4ERR_ISDIR,	EISDIR          },
	{ NFS4ERR_INVAL,	EINVAL          },
	{ NFS4ERR_FBIG,		EFBIG           },
	{ NFS4ERR_NOSPC,	ENOSPC          },
	{ NFS4ERR_ROFS,		EROFS           },
	{ NFS4ERR_MLINK,	EMLINK          },
	{ NFS4ERR_NAMETOOLONG,	ENAMETOOLONG    },
	{ NFS4ERR_NOTEMPTY,	ENOTEMPTY       },
	{ NFS4ERR_DQUOT,	EDQUOT          },
	{ NFS4ERR_STALE,	ESTALE          },
	{ NFS4ERR_BADHANDLE,	EBADHANDLE      },
	{ NFS4ERR_BAD_COOKIE,	EBADCOOKIE      },
	{ NFS4ERR_NOTSUPP,	ENOTSUPP        },
	{ NFS4ERR_TOOSMALL,	ETOOSMALL       },
	{ NFS4ERR_SERVERFAULT,	ESERVERFAULT    },
	{ NFS4ERR_BADTYPE,	EBADTYPE        },
	{ NFS4ERR_LOCKED,	EAGAIN          },
	{ NFS4ERR_RESOURCE,	EREMOTEIO       },
	{ NFS4ERR_SYMLINK,	ELOOP           },
	{ NFS4ERR_OP_ILLEGAL,	EOPNOTSUPP      },
	{ NFS4ERR_DEADLOCK,	EDEADLK         },
	{ -1,                   EIO             }
};

static int
nfs_cb_stat_to_errno(int stat)
{
	int i;
	for (i = 0; nfs_cb_errtbl[i].stat != -1; i++) {
		if (nfs_cb_errtbl[i].stat == stat)
			return nfs_cb_errtbl[i].errno;
	}
	/* If we cannot translate the error, the recovery routines should
	* handle it.
	* Note: remaining NFSv4 error codes have values > 10000, so should
	* not conflict with native Linux error codes.
	*/
	return stat;
}

/*
 * XDR encode
 */

static int
encode_cb_compound_hdr(struct xdr_stream *xdr, struct nfs4_cb_compound_hdr *hdr)
{
	u32 * p;

	RESERVE_SPACE(16);
	WRITE32(0);            /* tag length is always 0 */
	WRITE32(NFS4_MINOR_VERSION);
	WRITE32(hdr->ident);
	WRITE32(hdr->nops);
	return 0;
}

static int
encode_cb_recall(struct xdr_stream *xdr, struct nfs4_cb_recall *cb_rec)
{
	u32 *p;
	int len = cb_rec->cbr_fhlen;

	RESERVE_SPACE(12+sizeof(cb_rec->cbr_stateid) + len);
	WRITE32(OP_CB_RECALL);
	WRITEMEM(&cb_rec->cbr_stateid, sizeof(stateid_t));
	WRITE32(cb_rec->cbr_trunc);
	WRITE32(len);
	WRITEMEM(cb_rec->cbr_fhval, len);
	return 0;
}

static int
nfs4_xdr_enc_cb_null(struct rpc_rqst *req, u32 *p)
{
	struct xdr_stream xdrs, *xdr = &xdrs;

	xdr_init_encode(&xdrs, &req->rq_snd_buf, p);
        RESERVE_SPACE(0);
	return 0;
}

static int
nfs4_xdr_enc_cb_recall(struct rpc_rqst *req, u32 *p, struct nfs4_cb_recall *args)
{
	struct xdr_stream xdr;
	struct nfs4_cb_compound_hdr hdr = {
		.ident = args->cbr_ident,
		.nops   = 1,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_cb_compound_hdr(&xdr, &hdr);
	return (encode_cb_recall(&xdr, args));
}


static int
decode_cb_compound_hdr(struct xdr_stream *xdr, struct nfs4_cb_compound_hdr *hdr){
        u32 *p;

        READ_BUF(8);
        READ32(hdr->status);
        READ32(hdr->taglen);
        READ_BUF(hdr->taglen + 4);
        hdr->tag = (char *)p;
        p += XDR_QUADLEN(hdr->taglen);
        READ32(hdr->nops);
        return 0;
}

static int
decode_cb_op_hdr(struct xdr_stream *xdr, enum nfs_opnum4 expected)
{
	u32 *p;
	u32 op;
	int32_t nfserr;

	READ_BUF(8);
	READ32(op);
	if (op != expected) {
		dprintk("NFSD: decode_cb_op_hdr: Callback server returned "
		         " operation %d but we issued a request for %d\n",
		         op, expected);
		return -EIO;
	}
	READ32(nfserr);
	if (nfserr != NFS_OK)
		return -nfs_cb_stat_to_errno(nfserr);
	return 0;
}

static int
nfs4_xdr_dec_cb_null(struct rpc_rqst *req, u32 *p)
{
	return 0;
}

static int
nfs4_xdr_dec_cb_recall(struct rpc_rqst *rqstp, u32 *p)
{
	struct xdr_stream xdr;
	struct nfs4_cb_compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_cb_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_cb_op_hdr(&xdr, OP_CB_RECALL);
out:
	return status;
}

/*
 * RPC procedure tables
 */
#ifndef MAX
# define MAX(a, b)      (((a) > (b))? (a) : (b))
#endif

#define PROC(proc, call, argtype, restype)                              \
[NFSPROC4_CLNT_##proc] = {                                      	\
        .p_proc   = NFSPROC4_CB_##call,					\
        .p_encode = (kxdrproc_t) nfs4_xdr_##argtype,                    \
        .p_decode = (kxdrproc_t) nfs4_xdr_##restype,                    \
        .p_bufsiz = MAX(NFS4_##argtype##_sz,NFS4_##restype##_sz) << 2,  \
}

static struct rpc_procinfo     nfs4_cb_procedures[] = {
    PROC(CB_NULL,      NULL,     enc_cb_null,     dec_cb_null),
    PROC(CB_RECALL,    COMPOUND,   enc_cb_recall,      dec_cb_recall),
};

static struct rpc_version       nfs_cb_version4 = {
        .number                 = 1,
        .nrprocs                = ARRAY_SIZE(nfs4_cb_procedures),
        .procs                  = nfs4_cb_procedures
};

static struct rpc_version *	nfs_cb_version[] = {
	NULL,
	&nfs_cb_version4,
};

/*
 * Use the SETCLIENTID credential
 */
static struct rpc_cred *
nfsd4_lookupcred(struct nfs4_client *clp, int taskflags)
{
        struct auth_cred acred;
	struct rpc_clnt *clnt = clp->cl_callback.cb_client;
	struct rpc_cred *ret;

        get_group_info(clp->cl_cred.cr_group_info);
        acred.uid = clp->cl_cred.cr_uid;
        acred.gid = clp->cl_cred.cr_gid;
        acred.group_info = clp->cl_cred.cr_group_info;

        dprintk("NFSD:     looking up %s cred\n",
                clnt->cl_auth->au_ops->au_name);
        ret = rpcauth_lookup_credcache(clnt->cl_auth, &acred, taskflags);
        put_group_info(clp->cl_cred.cr_group_info);
        return ret;
}

/*
 * Set up the callback client and put a NFSPROC4_CB_NULL on the wire...
 */
void
nfsd4_probe_callback(struct nfs4_client *clp)
{
	struct sockaddr_in	addr;
	struct nfs4_callback    *cb = &clp->cl_callback;
	struct rpc_timeout	timeparms;
	struct rpc_xprt *	xprt;
	struct rpc_program *	program = &cb->cb_program;
	struct rpc_stat *	stat = &cb->cb_stat;
	struct rpc_clnt *	clnt;
	struct rpc_message msg = {
		.rpc_proc       = &nfs4_cb_procedures[NFSPROC4_CLNT_CB_NULL],
		.rpc_argp       = clp,
	};
	char                    hostname[32];
	int status;

	if (atomic_read(&cb->cb_set))
		return;

	/* Initialize address */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(cb->cb_port);
	addr.sin_addr.s_addr = htonl(cb->cb_addr);

	/* Initialize timeout */
	timeparms.to_initval = (NFSD_LEASE_TIME/4) * HZ;
	timeparms.to_retries = 0;
	timeparms.to_maxval = (NFSD_LEASE_TIME/2) * HZ;
	timeparms.to_exponential = 1;

	/* Create RPC transport */
	xprt = xprt_create_proto(IPPROTO_TCP, &addr, &timeparms);
	if (IS_ERR(xprt)) {
		dprintk("NFSD: couldn't create callback transport!\n");
		goto out_err;
	}

	/* Initialize rpc_program */
	program->name = "nfs4_cb";
	program->number = cb->cb_prog;
	program->nrvers = ARRAY_SIZE(nfs_cb_version);
	program->version = nfs_cb_version;
	program->stats = stat;

	/* Initialize rpc_stat */
	memset(stat, 0, sizeof(struct rpc_stat));
	stat->program = program;

	/* Create RPC client
 	 *
	 * XXX AUTH_UNIX only - need AUTH_GSS....
	 */
	sprintf(hostname, "%u.%u.%u.%u", NIPQUAD(addr.sin_addr.s_addr));
	clnt = rpc_new_client(xprt, hostname, program, 1, RPC_AUTH_UNIX);
	if (IS_ERR(clnt)) {
		dprintk("NFSD: couldn't create callback client\n");
		goto out_err;
	}
	clnt->cl_intr = 0;
	clnt->cl_softrtry = 1;

	/* Kick rpciod, put the call on the wire. */

	if (rpciod_up() != 0) {
		dprintk("nfsd: couldn't start rpciod for callbacks!\n");
		goto out_clnt;
	}

	/* the task holds a reference to the nfs4_client struct */
	cb->cb_client = clnt;
	atomic_inc(&clp->cl_count);

	msg.rpc_cred = nfsd4_lookupcred(clp,0);
	if (IS_ERR(msg.rpc_cred))
		goto out_rpciod;
	status = rpc_call_async(clnt, &msg, RPC_TASK_ASYNC, &nfs4_cb_null_ops, NULL);
	put_rpccred(msg.rpc_cred);

	if (status != 0) {
		dprintk("NFSD: asynchronous NFSPROC4_CB_NULL failed!\n");
		goto out_rpciod;
	}
	return;

out_rpciod:
	atomic_dec(&clp->cl_count);
	rpciod_down();
out_clnt:
	rpc_shutdown_client(clnt);
	goto out_err;
out_err:
	dprintk("NFSD: warning: no callback path to client %.*s\n",
		(int)clp->cl_name.len, clp->cl_name.data);
	cb->cb_client = NULL;
}

static void
nfs4_cb_null(struct rpc_task *task, void *dummy)
{
	struct nfs4_client *clp = (struct nfs4_client *)task->tk_msg.rpc_argp;
	struct nfs4_callback *cb = &clp->cl_callback;
	u32 addr = htonl(cb->cb_addr);

	dprintk("NFSD: nfs4_cb_null task->tk_status %d\n", task->tk_status);

	if (task->tk_status < 0) {
		dprintk("NFSD: callback establishment to client %.*s failed\n",
			(int)clp->cl_name.len, clp->cl_name.data);
		goto out;
	}
	atomic_set(&cb->cb_set, 1);
	dprintk("NFSD: callback set to client %u.%u.%u.%u\n", NIPQUAD(addr));
out:
	put_nfs4_client(clp);
}

static const struct rpc_call_ops nfs4_cb_null_ops = {
	.rpc_call_done = nfs4_cb_null,
};

/*
 * called with dp->dl_count inc'ed.
 * nfs4_lock_state() may or may not have been called.
 */
void
nfsd4_cb_recall(struct nfs4_delegation *dp)
{
	struct nfs4_client *clp = dp->dl_client;
	struct rpc_clnt *clnt = clp->cl_callback.cb_client;
	struct nfs4_cb_recall *cbr = &dp->dl_recall;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_cb_procedures[NFSPROC4_CLNT_CB_RECALL],
		.rpc_argp = cbr,
	};
	int retries = 1;
	int status = 0;

	if ((!atomic_read(&clp->cl_callback.cb_set)) || !clnt)
		return;

	msg.rpc_cred = nfsd4_lookupcred(clp, 0);
	if (IS_ERR(msg.rpc_cred))
		goto out;

	cbr->cbr_trunc = 0; /* XXX need to implement truncate optimization */
	cbr->cbr_dp = dp;

	status = rpc_call_sync(clnt, &msg, RPC_TASK_SOFT);
	while (retries--) {
		switch (status) {
			case -EIO:
				/* Network partition? */
			case -EBADHANDLE:
			case -NFS4ERR_BAD_STATEID:
				/* Race: client probably got cb_recall
				 * before open reply granting delegation */
				break;
			default:
				goto out_put_cred;
		}
		ssleep(2);
		status = rpc_call_sync(clnt, &msg, RPC_TASK_SOFT);
	}
out_put_cred:
	put_rpccred(msg.rpc_cred);
out:
	if (status == -EIO)
		atomic_set(&clp->cl_callback.cb_set, 0);
	/* Success or failure, now we're either waiting for lease expiration
	 * or deleg_return. */
	dprintk("NFSD: nfs4_cb_recall: dp %p dl_flock %p dl_count %d\n",dp, dp->dl_flock, atomic_read(&dp->dl_count));
	nfs4_put_delegation(dp);
	return;
}

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

#include <linux/module.h>
#include <linux/list.h>
#include <linux/inet.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/kthread.h>
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
#define enc_stateid_sz			(NFS4_STATEID_SIZE >> 2)
#define NFS4_enc_cb_recall_sz		(cb_compound_enc_hdr_sz +       \
					1 + enc_stateid_sz +            \
					enc_nfs4_fh_sz)

#define NFS4_dec_cb_recall_sz		(cb_compound_dec_hdr_sz  +      \
					op_dec_sz)

/*
* Generic encode routines from fs/nfs/nfs4xdr.c
*/
static inline __be32 *
xdr_writemem(__be32 *p, const void *ptr, int nbytes)
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
	if (!p) dprintk("NFSD: RESERVE_SPACE(%d) failed in function %s\n", (int) (nbytes), __func__); \
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
		dprintk("NFSD: %s: reply buffer overflowed in line %d.\n", \
			__func__, __LINE__); \
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
	__be32 * p;

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
	__be32 *p;
	int len = cb_rec->cbr_fhlen;

	RESERVE_SPACE(12+sizeof(cb_rec->cbr_stateid) + len);
	WRITE32(OP_CB_RECALL);
	WRITE32(cb_rec->cbr_stateid.si_generation);
	WRITEMEM(&cb_rec->cbr_stateid.si_opaque, sizeof(stateid_opaque_t));
	WRITE32(cb_rec->cbr_trunc);
	WRITE32(len);
	WRITEMEM(cb_rec->cbr_fhval, len);
	return 0;
}

static int
nfs4_xdr_enc_cb_null(struct rpc_rqst *req, __be32 *p)
{
	struct xdr_stream xdrs, *xdr = &xdrs;

	xdr_init_encode(&xdrs, &req->rq_snd_buf, p);
        RESERVE_SPACE(0);
	return 0;
}

static int
nfs4_xdr_enc_cb_recall(struct rpc_rqst *req, __be32 *p, struct nfs4_cb_recall *args)
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
        __be32 *p;

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
	__be32 *p;
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
nfs4_xdr_dec_cb_null(struct rpc_rqst *req, __be32 *p)
{
	return 0;
}

static int
nfs4_xdr_dec_cb_recall(struct rpc_rqst *rqstp, __be32 *p)
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
#define PROC(proc, call, argtype, restype)                              \
[NFSPROC4_CLNT_##proc] = {                                      	\
        .p_proc   = NFSPROC4_CB_##call,					\
        .p_encode = (kxdrproc_t) nfs4_xdr_##argtype,                    \
        .p_decode = (kxdrproc_t) nfs4_xdr_##restype,                    \
        .p_arglen = NFS4_##argtype##_sz,                                \
        .p_replen = NFS4_##restype##_sz,                                \
        .p_statidx = NFSPROC4_CB_##call,				\
	.p_name   = #proc,                                              \
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

static struct rpc_program cb_program;

static struct rpc_stat cb_stats = {
		.program	= &cb_program
};

#define NFS4_CALLBACK 0x40000000
static struct rpc_program cb_program = {
		.name 		= "nfs4_cb",
		.number		= NFS4_CALLBACK,
		.nrvers		= ARRAY_SIZE(nfs_cb_version),
		.version	= nfs_cb_version,
		.stats		= &cb_stats,
};

/* Reference counting, callback cleanup, etc., all look racy as heck.
 * And why is cb_set an atomic? */

static int do_probe_callback(void *data)
{
	struct nfs4_client *clp = data;
	struct sockaddr_in	addr;
	struct nfs4_callback    *cb = &clp->cl_callback;
	struct rpc_timeout	timeparms = {
		.to_initval	= (NFSD_LEASE_TIME/4) * HZ,
		.to_retries	= 5,
		.to_maxval	= (NFSD_LEASE_TIME/2) * HZ,
		.to_exponential	= 1,
	};
	struct rpc_create_args args = {
		.protocol	= IPPROTO_TCP,
		.address	= (struct sockaddr *)&addr,
		.addrsize	= sizeof(addr),
		.timeout	= &timeparms,
		.program	= &cb_program,
		.prognumber	= cb->cb_prog,
		.version	= nfs_cb_version[1]->number,
		.authflavor	= RPC_AUTH_UNIX, /* XXX: need AUTH_GSS... */
		.flags		= (RPC_CLNT_CREATE_NOPING | RPC_CLNT_CREATE_QUIET),
	};
	struct rpc_message msg = {
		.rpc_proc       = &nfs4_cb_procedures[NFSPROC4_CLNT_CB_NULL],
		.rpc_argp       = clp,
	};
	struct rpc_clnt *client;
	int status;

	/* Initialize address */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(cb->cb_port);
	addr.sin_addr.s_addr = htonl(cb->cb_addr);

	/* Create RPC client */
	client = rpc_create(&args);
	if (IS_ERR(client)) {
		dprintk("NFSD: couldn't create callback client\n");
		status = PTR_ERR(client);
		goto out_err;
	}

	status = rpc_call_sync(client, &msg, RPC_TASK_SOFT);

	if (status)
		goto out_release_client;

	cb->cb_client = client;
	atomic_set(&cb->cb_set, 1);
	put_nfs4_client(clp);
	return 0;
out_release_client:
	rpc_shutdown_client(client);
out_err:
	dprintk("NFSD: warning: no callback path to client %.*s\n",
		(int)clp->cl_name.len, clp->cl_name.data);
	put_nfs4_client(clp);
	return status;
}

/*
 * Set up the callback client and put a NFSPROC4_CB_NULL on the wire...
 */
void
nfsd4_probe_callback(struct nfs4_client *clp)
{
	struct task_struct *t;

	BUG_ON(atomic_read(&clp->cl_callback.cb_set));

	/* the task holds a reference to the nfs4_client struct */
	atomic_inc(&clp->cl_count);

	t = kthread_run(do_probe_callback, clp, "nfs4_cb_probe");

	if (IS_ERR(t))
		atomic_dec(&clp->cl_count);

	return;
}

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

	cbr->cbr_trunc = 0; /* XXX need to implement truncate optimization */
	cbr->cbr_dp = dp;

	status = rpc_call_sync(clnt, &msg, RPC_TASK_SOFT);
	while (retries--) {
		switch (status) {
			case -EIO:
				/* Network partition? */
				atomic_set(&clp->cl_callback.cb_set, 0);
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
	/*
	 * Success or failure, now we're either waiting for lease expiration
	 * or deleg_return.
	 */
	put_nfs4_client(clp);
	nfs4_put_delegation(dp);
	return;
}

/*
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

#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svc_xprt.h>
#include <linux/slab.h>
#include "nfsd.h"
#include "state.h"

#define NFSDDBG_FACILITY                NFSDDBG_PROC

#define NFSPROC4_CB_NULL 0
#define NFSPROC4_CB_COMPOUND 1

/* Index of predefined Linux callback client operations */

enum {
	NFSPROC4_CLNT_CB_NULL = 0,
	NFSPROC4_CLNT_CB_RECALL,
	NFSPROC4_CLNT_CB_SEQUENCE,
};

enum nfs_cb_opnum4 {
	OP_CB_RECALL            = 4,
	OP_CB_SEQUENCE          = 11,
};

#define NFS4_MAXTAGLEN		20

#define NFS4_enc_cb_null_sz		0
#define NFS4_dec_cb_null_sz		0
#define cb_compound_enc_hdr_sz		4
#define cb_compound_dec_hdr_sz		(3 + (NFS4_MAXTAGLEN >> 2))
#define sessionid_sz			(NFS4_MAX_SESSIONID_LEN >> 2)
#define cb_sequence_enc_sz		(sessionid_sz + 4 +             \
					1 /* no referring calls list yet */)
#define cb_sequence_dec_sz		(op_dec_sz + sessionid_sz + 4)

#define op_enc_sz			1
#define op_dec_sz			2
#define enc_nfs4_fh_sz			(1 + (NFS4_FHSIZE >> 2))
#define enc_stateid_sz			(NFS4_STATEID_SIZE >> 2)
#define NFS4_enc_cb_recall_sz		(cb_compound_enc_hdr_sz +       \
					cb_sequence_enc_sz +            \
					1 + enc_stateid_sz +            \
					enc_nfs4_fh_sz)

#define NFS4_dec_cb_recall_sz		(cb_compound_dec_hdr_sz  +      \
					cb_sequence_dec_sz +            \
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
	/* args */
	u32		ident;	/* minorversion 0 only */
	u32		nops;
	__be32		*nops_p;
	u32		minorversion;
	/* res */
	int		status;
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

static void
encode_stateid(struct xdr_stream *xdr, stateid_t *sid)
{
	__be32 *p;

	RESERVE_SPACE(sizeof(stateid_t));
	WRITE32(sid->si_generation);
	WRITEMEM(&sid->si_opaque, sizeof(stateid_opaque_t));
}

static void
encode_cb_compound_hdr(struct xdr_stream *xdr, struct nfs4_cb_compound_hdr *hdr)
{
	__be32 * p;

	RESERVE_SPACE(16);
	WRITE32(0);            /* tag length is always 0 */
	WRITE32(hdr->minorversion);
	WRITE32(hdr->ident);
	hdr->nops_p = p;
	WRITE32(hdr->nops);
}

static void encode_cb_nops(struct nfs4_cb_compound_hdr *hdr)
{
	*hdr->nops_p = htonl(hdr->nops);
}

static void
encode_cb_recall(struct xdr_stream *xdr, struct nfs4_delegation *dp,
		struct nfs4_cb_compound_hdr *hdr)
{
	__be32 *p;
	int len = dp->dl_fh.fh_size;

	RESERVE_SPACE(4);
	WRITE32(OP_CB_RECALL);
	encode_stateid(xdr, &dp->dl_stateid);
	RESERVE_SPACE(8 + (XDR_QUADLEN(len) << 2));
	WRITE32(0); /* truncate optimization not implemented */
	WRITE32(len);
	WRITEMEM(&dp->dl_fh.fh_base, len);
	hdr->nops++;
}

static void
encode_cb_sequence(struct xdr_stream *xdr, struct nfs4_rpc_args *args,
		   struct nfs4_cb_compound_hdr *hdr)
{
	__be32 *p;

	if (hdr->minorversion == 0)
		return;

	RESERVE_SPACE(1 + NFS4_MAX_SESSIONID_LEN + 20);

	WRITE32(OP_CB_SEQUENCE);
	WRITEMEM(args->args_clp->cl_sessionid.data, NFS4_MAX_SESSIONID_LEN);
	WRITE32(args->args_clp->cl_cb_seq_nr);
	WRITE32(0);		/* slotid, always 0 */
	WRITE32(0);		/* highest slotid always 0 */
	WRITE32(0);		/* cachethis always 0 */
	WRITE32(0); /* FIXME: support referring_call_lists */
	hdr->nops++;
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
nfs4_xdr_enc_cb_recall(struct rpc_rqst *req, __be32 *p,
		struct nfs4_rpc_args *rpc_args)
{
	struct xdr_stream xdr;
	struct nfs4_delegation *args = rpc_args->args_op;
	struct nfs4_cb_compound_hdr hdr = {
		.ident = args->dl_ident,
		.minorversion = rpc_args->args_minorversion,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_cb_compound_hdr(&xdr, &hdr);
	encode_cb_sequence(&xdr, rpc_args, &hdr);
	encode_cb_recall(&xdr, args, &hdr);
	encode_cb_nops(&hdr);
	return 0;
}


static int
decode_cb_compound_hdr(struct xdr_stream *xdr, struct nfs4_cb_compound_hdr *hdr){
        __be32 *p;
	u32 taglen;

        READ_BUF(8);
        READ32(hdr->status);
	/* We've got no use for the tag; ignore it: */
        READ32(taglen);
        READ_BUF(taglen + 4);
        p += XDR_QUADLEN(taglen);
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

/*
 * Our current back channel implmentation supports a single backchannel
 * with a single slot.
 */
static int
decode_cb_sequence(struct xdr_stream *xdr, struct nfs4_rpc_args *res,
		   struct rpc_rqst *rqstp)
{
	struct nfs4_sessionid id;
	int status;
	u32 dummy;
	__be32 *p;

	if (res->args_minorversion == 0)
		return 0;

	status = decode_cb_op_hdr(xdr, OP_CB_SEQUENCE);
	if (status)
		return status;

	/*
	 * If the server returns different values for sessionID, slotID or
	 * sequence number, the server is looney tunes.
	 */
	status = -ESERVERFAULT;

	READ_BUF(NFS4_MAX_SESSIONID_LEN + 16);
	memcpy(id.data, p, NFS4_MAX_SESSIONID_LEN);
	p += XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN);
	if (memcmp(id.data, res->args_clp->cl_sessionid.data,
		   NFS4_MAX_SESSIONID_LEN)) {
		dprintk("%s Invalid session id\n", __func__);
		goto out;
	}
	READ32(dummy);
	if (dummy != res->args_clp->cl_cb_seq_nr) {
		dprintk("%s Invalid sequence number\n", __func__);
		goto out;
	}
	READ32(dummy); 	/* slotid must be 0 */
	if (dummy != 0) {
		dprintk("%s Invalid slotid\n", __func__);
		goto out;
	}
	/* FIXME: process highest slotid and target highest slotid */
	status = 0;
out:
	return status;
}


static int
nfs4_xdr_dec_cb_null(struct rpc_rqst *req, __be32 *p)
{
	return 0;
}

static int
nfs4_xdr_dec_cb_recall(struct rpc_rqst *rqstp, __be32 *p,
		struct nfs4_rpc_args *args)
{
	struct xdr_stream xdr;
	struct nfs4_cb_compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_cb_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	if (args) {
		status = decode_cb_sequence(&xdr, args, rqstp);
		if (status)
			goto out;
	}
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
/*
 * Note on the callback rpc program version number: despite language in rfc
 * 5661 section 18.36.3 requiring servers to use 4 in this field, the
 * official xdr descriptions for both 4.0 and 4.1 specify version 1, and
 * in practice that appears to be what implementations use.  The section
 * 18.36.3 language is expected to be fixed in an erratum.
 */
        .number                 = 1,
        .nrprocs                = ARRAY_SIZE(nfs4_cb_procedures),
        .procs                  = nfs4_cb_procedures
};

static struct rpc_version *	nfs_cb_version[] = {
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
		.pipe_dir_name  = "/nfsd4_cb",
};

static int max_cb_time(void)
{
	return max(nfsd4_lease/10, (time_t)1) * HZ;
}

/* Reference counting, callback cleanup, etc., all look racy as heck.
 * And why is cl_cb_set an atomic? */

int setup_callback_client(struct nfs4_client *clp, struct nfs4_cb_conn *conn)
{
	struct rpc_timeout	timeparms = {
		.to_initval	= max_cb_time(),
		.to_retries	= 0,
	};
	struct rpc_create_args args = {
		.net		= &init_net,
		.protocol	= XPRT_TRANSPORT_TCP,
		.address	= (struct sockaddr *) &conn->cb_addr,
		.addrsize	= conn->cb_addrlen,
		.timeout	= &timeparms,
		.program	= &cb_program,
		.prognumber	= conn->cb_prog,
		.version	= 0,
		.authflavor	= clp->cl_flavor,
		.flags		= (RPC_CLNT_CREATE_NOPING | RPC_CLNT_CREATE_QUIET),
		.client_name    = clp->cl_principal,
	};
	struct rpc_clnt *client;

	if (!clp->cl_principal && (clp->cl_flavor >= RPC_AUTH_GSS_KRB5))
		return -EINVAL;
	if (conn->cb_minorversion) {
		args.bc_xprt = conn->cb_xprt;
		args.protocol = XPRT_TRANSPORT_BC_TCP;
	}
	/* Create RPC client */
	client = rpc_create(&args);
	if (IS_ERR(client)) {
		dprintk("NFSD: couldn't create callback client: %ld\n",
			PTR_ERR(client));
		return PTR_ERR(client);
	}
	nfsd4_set_callback_client(clp, client);
	return 0;

}

static void warn_no_callback_path(struct nfs4_client *clp, int reason)
{
	dprintk("NFSD: warning: no callback path to client %.*s: error %d\n",
		(int)clp->cl_name.len, clp->cl_name.data, reason);
}

static void nfsd4_cb_probe_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_client *clp = calldata;

	if (task->tk_status)
		warn_no_callback_path(clp, task->tk_status);
	else
		atomic_set(&clp->cl_cb_set, 1);
}

static const struct rpc_call_ops nfsd4_cb_probe_ops = {
	.rpc_call_done = nfsd4_cb_probe_done,
};

static struct rpc_cred *callback_cred;

int set_callback_cred(void)
{
	if (callback_cred)
		return 0;
	callback_cred = rpc_lookup_machine_cred();
	if (!callback_cred)
		return -ENOMEM;
	return 0;
}


void do_probe_callback(struct nfs4_client *clp)
{
	struct rpc_message msg = {
		.rpc_proc       = &nfs4_cb_procedures[NFSPROC4_CLNT_CB_NULL],
		.rpc_argp       = clp,
		.rpc_cred	= callback_cred
	};
	int status;

	status = rpc_call_async(clp->cl_cb_client, &msg,
				RPC_TASK_SOFT | RPC_TASK_SOFTCONN,
				&nfsd4_cb_probe_ops, (void *)clp);
	if (status)
		warn_no_callback_path(clp, status);
}

/*
 * Set up the callback client and put a NFSPROC4_CB_NULL on the wire...
 */
void nfsd4_probe_callback(struct nfs4_client *clp, struct nfs4_cb_conn *conn)
{
	int status;

	BUG_ON(atomic_read(&clp->cl_cb_set));

	status = setup_callback_client(clp, conn);
	if (status) {
		warn_no_callback_path(clp, status);
		return;
	}
	do_probe_callback(clp);
}

/*
 * There's currently a single callback channel slot.
 * If the slot is available, then mark it busy.  Otherwise, set the
 * thread for sleeping on the callback RPC wait queue.
 */
static int nfsd41_cb_setup_sequence(struct nfs4_client *clp,
		struct rpc_task *task)
{
	struct nfs4_rpc_args *args = task->tk_msg.rpc_argp;
	u32 *ptr = (u32 *)clp->cl_sessionid.data;
	int status = 0;

	dprintk("%s: %u:%u:%u:%u\n", __func__,
		ptr[0], ptr[1], ptr[2], ptr[3]);

	if (test_and_set_bit(0, &clp->cl_cb_slot_busy) != 0) {
		rpc_sleep_on(&clp->cl_cb_waitq, task, NULL);
		dprintk("%s slot is busy\n", __func__);
		status = -EAGAIN;
		goto out;
	}

	/*
	 * We'll need the clp during XDR encoding and decoding,
	 * and the sequence during decoding to verify the reply
	 */
	args->args_clp = clp;
	task->tk_msg.rpc_resp = args;

out:
	dprintk("%s status=%d\n", __func__, status);
	return status;
}

/*
 * TODO: cb_sequence should support referring call lists, cachethis, multiple
 * slots, and mark callback channel down on communication errors.
 */
static void nfsd4_cb_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs4_delegation *dp = calldata;
	struct nfs4_client *clp = dp->dl_client;
	struct nfs4_rpc_args *args = task->tk_msg.rpc_argp;
	u32 minorversion = clp->cl_cb_conn.cb_minorversion;
	int status = 0;

	args->args_minorversion = minorversion;
	if (minorversion) {
		status = nfsd41_cb_setup_sequence(clp, task);
		if (status) {
			if (status != -EAGAIN) {
				/* terminate rpc task */
				task->tk_status = status;
				task->tk_action = NULL;
			}
			return;
		}
	}
	rpc_call_start(task);
}

static void nfsd4_cb_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_delegation *dp = calldata;
	struct nfs4_client *clp = dp->dl_client;

	dprintk("%s: minorversion=%d\n", __func__,
		clp->cl_cb_conn.cb_minorversion);

	if (clp->cl_cb_conn.cb_minorversion) {
		/* No need for lock, access serialized in nfsd4_cb_prepare */
		++clp->cl_cb_seq_nr;
		clear_bit(0, &clp->cl_cb_slot_busy);
		rpc_wake_up_next(&clp->cl_cb_waitq);
		dprintk("%s: freed slot, new seqid=%d\n", __func__,
			clp->cl_cb_seq_nr);

		/* We're done looking into the sequence information */
		task->tk_msg.rpc_resp = NULL;
	}
}


static void nfsd4_cb_recall_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_delegation *dp = calldata;
	struct nfs4_client *clp = dp->dl_client;
	struct rpc_clnt *current_rpc_client = clp->cl_cb_client;

	nfsd4_cb_done(task, calldata);

	if (current_rpc_client == NULL) {
		/* We're shutting down; give up. */
		/* XXX: err, or is it ok just to fall through
		 * and rpc_restart_call? */
		return;
	}

	switch (task->tk_status) {
	case 0:
		return;
	case -EBADHANDLE:
	case -NFS4ERR_BAD_STATEID:
		/* Race: client probably got cb_recall
		 * before open reply granting delegation */
		break;
	default:
		/* Network partition? */
		atomic_set(&clp->cl_cb_set, 0);
		warn_no_callback_path(clp, task->tk_status);
		if (current_rpc_client != task->tk_client) {
			/* queue a callback on the new connection: */
			atomic_inc(&dp->dl_count);
			nfsd4_cb_recall(dp);
			return;
		}
	}
	if (dp->dl_retries--) {
		rpc_delay(task, 2*HZ);
		task->tk_status = 0;
		rpc_restart_call_prepare(task);
		return;
	} else {
		atomic_set(&clp->cl_cb_set, 0);
		warn_no_callback_path(clp, task->tk_status);
	}
}

static void nfsd4_cb_recall_release(void *calldata)
{
	struct nfs4_delegation *dp = calldata;

	nfs4_put_delegation(dp);
}

static const struct rpc_call_ops nfsd4_cb_recall_ops = {
	.rpc_call_prepare = nfsd4_cb_prepare,
	.rpc_call_done = nfsd4_cb_recall_done,
	.rpc_release = nfsd4_cb_recall_release,
};

static struct workqueue_struct *callback_wq;

int nfsd4_create_callback_queue(void)
{
	callback_wq = create_singlethread_workqueue("nfsd4_callbacks");
	if (!callback_wq)
		return -ENOMEM;
	return 0;
}

void nfsd4_destroy_callback_queue(void)
{
	destroy_workqueue(callback_wq);
}

/* must be called under the state lock */
void nfsd4_set_callback_client(struct nfs4_client *clp, struct rpc_clnt *new)
{
	struct rpc_clnt *old = clp->cl_cb_client;

	clp->cl_cb_client = new;
	/*
	 * After this, any work that saw the old value of cl_cb_client will
	 * be gone:
	 */
	flush_workqueue(callback_wq);
	/* So we can safely shut it down: */
	if (old)
		rpc_shutdown_client(old);
}

/*
 * called with dp->dl_count inc'ed.
 */
static void _nfsd4_cb_recall(struct nfs4_delegation *dp)
{
	struct nfs4_client *clp = dp->dl_client;
	struct rpc_clnt *clnt = clp->cl_cb_client;
	struct nfs4_rpc_args *args = &dp->dl_recall.cb_args;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_cb_procedures[NFSPROC4_CLNT_CB_RECALL],
		.rpc_cred = callback_cred
	};

	if (clnt == NULL) {
		nfs4_put_delegation(dp);
		return; /* Client is shutting down; give up. */
	}

	args->args_op = dp;
	msg.rpc_argp = args;
	dp->dl_retries = 1;
	rpc_call_async(clnt, &msg, RPC_TASK_SOFT, &nfsd4_cb_recall_ops, dp);
}

void nfsd4_do_callback_rpc(struct work_struct *w)
{
	/* XXX: for now, just send off delegation recall. */
	/* In future, generalize to handle any sort of callback. */
	struct nfsd4_callback *c = container_of(w, struct nfsd4_callback, cb_work);
	struct nfs4_delegation *dp = container_of(c, struct nfs4_delegation, dl_recall);

	_nfsd4_cb_recall(dp);
}


void nfsd4_cb_recall(struct nfs4_delegation *dp)
{
	queue_work(callback_wq, &dp->dl_recall.cb_work);
}

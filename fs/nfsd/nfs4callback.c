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
encode_cb_sequence(struct xdr_stream *xdr, struct nfsd4_callback *cb,
		   struct nfs4_cb_compound_hdr *hdr)
{
	__be32 *p;
	struct nfsd4_session *ses = cb->cb_clp->cl_cb_session;

	if (hdr->minorversion == 0)
		return;

	RESERVE_SPACE(1 + NFS4_MAX_SESSIONID_LEN + 20);

	WRITE32(OP_CB_SEQUENCE);
	WRITEMEM(ses->se_sessionid.data, NFS4_MAX_SESSIONID_LEN);
	WRITE32(ses->se_cb_seq_nr);
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
		struct nfsd4_callback *cb)
{
	struct xdr_stream xdr;
	struct nfs4_delegation *args = cb->cb_op;
	struct nfs4_cb_compound_hdr hdr = {
		.ident = cb->cb_clp->cl_cb_ident,
		.minorversion = cb->cb_minorversion,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_cb_compound_hdr(&xdr, &hdr);
	encode_cb_sequence(&xdr, cb, &hdr);
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
decode_cb_sequence(struct xdr_stream *xdr, struct nfsd4_callback *cb,
		   struct rpc_rqst *rqstp)
{
	struct nfsd4_session *ses = cb->cb_clp->cl_cb_session;
	struct nfs4_sessionid id;
	int status;
	u32 dummy;
	__be32 *p;

	if (cb->cb_minorversion == 0)
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
	if (memcmp(id.data, ses->se_sessionid.data, NFS4_MAX_SESSIONID_LEN)) {
		dprintk("%s Invalid session id\n", __func__);
		goto out;
	}
	READ32(dummy);
	if (dummy != ses->se_cb_seq_nr) {
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
		struct nfsd4_callback *cb)
{
	struct xdr_stream xdr;
	struct nfs4_cb_compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_cb_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	if (cb) {
		status = decode_cb_sequence(&xdr, cb, rqstp);
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


static int setup_callback_client(struct nfs4_client *clp, struct nfs4_cb_conn *conn, struct nfsd4_session *ses)
{
	struct rpc_timeout	timeparms = {
		.to_initval	= max_cb_time(),
		.to_retries	= 0,
	};
	struct rpc_create_args args = {
		.net		= &init_net,
		.address	= (struct sockaddr *) &conn->cb_addr,
		.addrsize	= conn->cb_addrlen,
		.saddress	= (struct sockaddr *) &conn->cb_saddr,
		.timeout	= &timeparms,
		.program	= &cb_program,
		.version	= 0,
		.authflavor	= clp->cl_flavor,
		.flags		= (RPC_CLNT_CREATE_NOPING | RPC_CLNT_CREATE_QUIET),
	};
	struct rpc_clnt *client;

	if (clp->cl_minorversion == 0) {
		if (!clp->cl_principal && (clp->cl_flavor >= RPC_AUTH_GSS_KRB5))
			return -EINVAL;
		args.client_name = clp->cl_principal;
		args.prognumber	= conn->cb_prog,
		args.protocol = XPRT_TRANSPORT_TCP;
		clp->cl_cb_ident = conn->cb_ident;
	} else {
		if (!conn->cb_xprt)
			return -EINVAL;
		clp->cl_cb_conn.cb_xprt = conn->cb_xprt;
		clp->cl_cb_session = ses;
		args.bc_xprt = conn->cb_xprt;
		args.prognumber = clp->cl_cb_session->se_cb_prog;
		args.protocol = XPRT_TRANSPORT_BC_TCP;
	}
	/* Create RPC client */
	client = rpc_create(&args);
	if (IS_ERR(client)) {
		dprintk("NFSD: couldn't create callback client: %ld\n",
			PTR_ERR(client));
		return PTR_ERR(client);
	}
	clp->cl_cb_client = client;
	return 0;

}

static void warn_no_callback_path(struct nfs4_client *clp, int reason)
{
	dprintk("NFSD: warning: no callback path to client %.*s: error %d\n",
		(int)clp->cl_name.len, clp->cl_name.data, reason);
}

static void nfsd4_mark_cb_down(struct nfs4_client *clp, int reason)
{
	clp->cl_cb_state = NFSD4_CB_DOWN;
	warn_no_callback_path(clp, reason);
}

static void nfsd4_cb_probe_done(struct rpc_task *task, void *calldata)
{
	struct nfs4_client *clp = container_of(calldata, struct nfs4_client, cl_cb_null);

	if (task->tk_status)
		nfsd4_mark_cb_down(clp, task->tk_status);
	else
		clp->cl_cb_state = NFSD4_CB_UP;
}

static const struct rpc_call_ops nfsd4_cb_probe_ops = {
	/* XXX: release method to ensure we set the cb channel down if
	 * necessary on early failure? */
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

static struct workqueue_struct *callback_wq;

static void run_nfsd4_cb(struct nfsd4_callback *cb)
{
	queue_work(callback_wq, &cb->cb_work);
}

static void do_probe_callback(struct nfs4_client *clp)
{
	struct nfsd4_callback *cb = &clp->cl_cb_null;

	cb->cb_op = NULL;
	cb->cb_clp = clp;

	cb->cb_msg.rpc_proc = &nfs4_cb_procedures[NFSPROC4_CLNT_CB_NULL];
	cb->cb_msg.rpc_argp = NULL;
	cb->cb_msg.rpc_resp = NULL;
	cb->cb_msg.rpc_cred = callback_cred;

	cb->cb_ops = &nfsd4_cb_probe_ops;

	run_nfsd4_cb(cb);
}

/*
 * Poke the callback thread to process any updates to the callback
 * parameters, and send a null probe.
 */
void nfsd4_probe_callback(struct nfs4_client *clp)
{
	/* XXX: atomicity?  Also, should we be using cl_cb_flags? */
	clp->cl_cb_state = NFSD4_CB_UNKNOWN;
	set_bit(NFSD4_CLIENT_CB_UPDATE, &clp->cl_cb_flags);
	do_probe_callback(clp);
}

void nfsd4_probe_callback_sync(struct nfs4_client *clp)
{
	nfsd4_probe_callback(clp);
	flush_workqueue(callback_wq);
}

void nfsd4_change_callback(struct nfs4_client *clp, struct nfs4_cb_conn *conn)
{
	clp->cl_cb_state = NFSD4_CB_UNKNOWN;
	spin_lock(&clp->cl_lock);
	memcpy(&clp->cl_cb_conn, conn, sizeof(struct nfs4_cb_conn));
	spin_unlock(&clp->cl_lock);
}

/*
 * There's currently a single callback channel slot.
 * If the slot is available, then mark it busy.  Otherwise, set the
 * thread for sleeping on the callback RPC wait queue.
 */
static int nfsd41_cb_setup_sequence(struct nfs4_client *clp,
		struct rpc_task *task)
{
	u32 *ptr = (u32 *)clp->cl_cb_session->se_sessionid.data;
	int status = 0;

	dprintk("%s: %u:%u:%u:%u\n", __func__,
		ptr[0], ptr[1], ptr[2], ptr[3]);

	if (test_and_set_bit(0, &clp->cl_cb_slot_busy) != 0) {
		rpc_sleep_on(&clp->cl_cb_waitq, task, NULL);
		dprintk("%s slot is busy\n", __func__);
		status = -EAGAIN;
		goto out;
	}
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
	struct nfsd4_callback *cb = calldata;
	struct nfs4_delegation *dp = container_of(cb, struct nfs4_delegation, dl_recall);
	struct nfs4_client *clp = dp->dl_client;
	u32 minorversion = clp->cl_minorversion;
	int status = 0;

	cb->cb_minorversion = minorversion;
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
	struct nfsd4_callback *cb = calldata;
	struct nfs4_delegation *dp = container_of(cb, struct nfs4_delegation, dl_recall);
	struct nfs4_client *clp = dp->dl_client;

	dprintk("%s: minorversion=%d\n", __func__,
		clp->cl_minorversion);

	if (clp->cl_minorversion) {
		/* No need for lock, access serialized in nfsd4_cb_prepare */
		++clp->cl_cb_session->se_cb_seq_nr;
		clear_bit(0, &clp->cl_cb_slot_busy);
		rpc_wake_up_next(&clp->cl_cb_waitq);
		dprintk("%s: freed slot, new seqid=%d\n", __func__,
			clp->cl_cb_session->se_cb_seq_nr);

		/* We're done looking into the sequence information */
		task->tk_msg.rpc_resp = NULL;
	}
}


static void nfsd4_cb_recall_done(struct rpc_task *task, void *calldata)
{
	struct nfsd4_callback *cb = calldata;
	struct nfs4_delegation *dp = container_of(cb, struct nfs4_delegation, dl_recall);
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
		nfsd4_mark_cb_down(clp, task->tk_status);
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
	} else
		nfsd4_mark_cb_down(clp, task->tk_status);
}

static void nfsd4_cb_recall_release(void *calldata)
{
	struct nfsd4_callback *cb = calldata;
	struct nfs4_delegation *dp = container_of(cb, struct nfs4_delegation, dl_recall);

	nfs4_put_delegation(dp);
}

static const struct rpc_call_ops nfsd4_cb_recall_ops = {
	.rpc_call_prepare = nfsd4_cb_prepare,
	.rpc_call_done = nfsd4_cb_recall_done,
	.rpc_release = nfsd4_cb_recall_release,
};

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
void nfsd4_shutdown_callback(struct nfs4_client *clp)
{
	set_bit(NFSD4_CLIENT_KILL, &clp->cl_cb_flags);
	/*
	 * Note this won't actually result in a null callback;
	 * instead, nfsd4_do_callback_rpc() will detect the killed
	 * client, destroy the rpc client, and stop:
	 */
	do_probe_callback(clp);
	flush_workqueue(callback_wq);
}

static void nfsd4_release_cb(struct nfsd4_callback *cb)
{
	if (cb->cb_ops->rpc_release)
		cb->cb_ops->rpc_release(cb);
}

/* requires cl_lock: */
static struct nfsd4_conn * __nfsd4_find_backchannel(struct nfs4_client *clp)
{
	struct nfsd4_session *s;
	struct nfsd4_conn *c;

	list_for_each_entry(s, &clp->cl_sessions, se_perclnt) {
		list_for_each_entry(c, &s->se_conns, cn_persession) {
			if (c->cn_flags & NFS4_CDFC4_BACK)
				return c;
		}
	}
	return NULL;
}

static void nfsd4_process_cb_update(struct nfsd4_callback *cb)
{
	struct nfs4_cb_conn conn;
	struct nfs4_client *clp = cb->cb_clp;
	struct nfsd4_session *ses = NULL;
	struct nfsd4_conn *c;
	int err;

	/*
	 * This is either an update, or the client dying; in either case,
	 * kill the old client:
	 */
	if (clp->cl_cb_client) {
		rpc_shutdown_client(clp->cl_cb_client);
		clp->cl_cb_client = NULL;
	}
	if (clp->cl_cb_conn.cb_xprt) {
		svc_xprt_put(clp->cl_cb_conn.cb_xprt);
		clp->cl_cb_conn.cb_xprt = NULL;
	}
	if (test_bit(NFSD4_CLIENT_KILL, &clp->cl_cb_flags))
		return;
	spin_lock(&clp->cl_lock);
	/*
	 * Only serialized callback code is allowed to clear these
	 * flags; main nfsd code can only set them:
	 */
	BUG_ON(!clp->cl_cb_flags);
	clear_bit(NFSD4_CLIENT_CB_UPDATE, &clp->cl_cb_flags);
	memcpy(&conn, &cb->cb_clp->cl_cb_conn, sizeof(struct nfs4_cb_conn));
	c = __nfsd4_find_backchannel(clp);
	if (c) {
		svc_xprt_get(c->cn_xprt);
		conn.cb_xprt = c->cn_xprt;
		ses = c->cn_session;
	}
	spin_unlock(&clp->cl_lock);

	err = setup_callback_client(clp, &conn, ses);
	if (err)
		warn_no_callback_path(clp, err);
}

void nfsd4_do_callback_rpc(struct work_struct *w)
{
	struct nfsd4_callback *cb = container_of(w, struct nfsd4_callback, cb_work);
	struct nfs4_client *clp = cb->cb_clp;
	struct rpc_clnt *clnt;

	if (clp->cl_cb_flags)
		nfsd4_process_cb_update(cb);

	clnt = clp->cl_cb_client;
	if (!clnt) {
		/* Callback channel broken, or client killed; give up: */
		nfsd4_release_cb(cb);
		return;
	}
	rpc_call_async(clnt, &cb->cb_msg, RPC_TASK_SOFT | RPC_TASK_SOFTCONN,
			cb->cb_ops, cb);
}

void nfsd4_cb_recall(struct nfs4_delegation *dp)
{
	struct nfsd4_callback *cb = &dp->dl_recall;

	dp->dl_retries = 1;
	cb->cb_op = dp;
	cb->cb_clp = dp->dl_client;
	cb->cb_msg.rpc_proc = &nfs4_cb_procedures[NFSPROC4_CLNT_CB_RECALL];
	cb->cb_msg.rpc_argp = cb;
	cb->cb_msg.rpc_resp = cb;
	cb->cb_msg.rpc_cred = callback_cred;

	cb->cb_ops = &nfsd4_cb_recall_ops;
	dp->dl_retries = 1;

	run_nfsd4_cb(&dp->dl_recall);
}

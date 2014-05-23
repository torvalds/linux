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
#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/svc_xprt.h>
#include <linux/slab.h>
#include "nfsd.h"
#include "state.h"
#include "netns.h"
#include "xdr4cb.h"

#define NFSDDBG_FACILITY                NFSDDBG_PROC

static void nfsd4_mark_cb_fault(struct nfs4_client *, int reason);

#define NFSPROC4_CB_NULL 0
#define NFSPROC4_CB_COMPOUND 1

/* Index of predefined Linux callback client operations */

enum {
	NFSPROC4_CLNT_CB_NULL = 0,
	NFSPROC4_CLNT_CB_RECALL,
	NFSPROC4_CLNT_CB_SEQUENCE,
};

struct nfs4_cb_compound_hdr {
	/* args */
	u32		ident;	/* minorversion 0 only */
	u32		nops;
	__be32		*nops_p;
	u32		minorversion;
	/* res */
	int		status;
};

/*
 * Handle decode buffer overflows out-of-line.
 */
static void print_overflow_msg(const char *func, const struct xdr_stream *xdr)
{
	dprintk("NFS: %s prematurely hit the end of our receive buffer. "
		"Remaining buffer length is %tu words.\n",
		func, xdr->end - xdr->p);
}

static __be32 *xdr_encode_empty_array(__be32 *p)
{
	*p++ = xdr_zero;
	return p;
}

/*
 * Encode/decode NFSv4 CB basic data types
 *
 * Basic NFSv4 callback data types are defined in section 15 of RFC
 * 3530: "Network File System (NFS) version 4 Protocol" and section
 * 20 of RFC 5661: "Network File System (NFS) Version 4 Minor Version
 * 1 Protocol"
 */

/*
 *	nfs_cb_opnum4
 *
 *	enum nfs_cb_opnum4 {
 *		OP_CB_GETATTR		= 3,
 *		  ...
 *	};
 */
enum nfs_cb_opnum4 {
	OP_CB_GETATTR			= 3,
	OP_CB_RECALL			= 4,
	OP_CB_LAYOUTRECALL		= 5,
	OP_CB_NOTIFY			= 6,
	OP_CB_PUSH_DELEG		= 7,
	OP_CB_RECALL_ANY		= 8,
	OP_CB_RECALLABLE_OBJ_AVAIL	= 9,
	OP_CB_RECALL_SLOT		= 10,
	OP_CB_SEQUENCE			= 11,
	OP_CB_WANTS_CANCELLED		= 12,
	OP_CB_NOTIFY_LOCK		= 13,
	OP_CB_NOTIFY_DEVICEID		= 14,
	OP_CB_ILLEGAL			= 10044
};

static void encode_nfs_cb_opnum4(struct xdr_stream *xdr, enum nfs_cb_opnum4 op)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, 4);
	*p = cpu_to_be32(op);
}

/*
 * nfs_fh4
 *
 *	typedef opaque nfs_fh4<NFS4_FHSIZE>;
 */
static void encode_nfs_fh4(struct xdr_stream *xdr, const struct knfsd_fh *fh)
{
	u32 length = fh->fh_size;
	__be32 *p;

	BUG_ON(length > NFS4_FHSIZE);
	p = xdr_reserve_space(xdr, 4 + length);
	xdr_encode_opaque(p, &fh->fh_base, length);
}

/*
 * stateid4
 *
 *	struct stateid4 {
 *		uint32_t	seqid;
 *		opaque		other[12];
 *	};
 */
static void encode_stateid4(struct xdr_stream *xdr, const stateid_t *sid)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, NFS4_STATEID_SIZE);
	*p++ = cpu_to_be32(sid->si_generation);
	xdr_encode_opaque_fixed(p, &sid->si_opaque, NFS4_STATEID_OTHER_SIZE);
}

/*
 * sessionid4
 *
 *	typedef opaque sessionid4[NFS4_SESSIONID_SIZE];
 */
static void encode_sessionid4(struct xdr_stream *xdr,
			      const struct nfsd4_session *session)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, NFS4_MAX_SESSIONID_LEN);
	xdr_encode_opaque_fixed(p, session->se_sessionid.data,
					NFS4_MAX_SESSIONID_LEN);
}

/*
 * nfsstat4
 */
static const struct {
	int stat;
	int errno;
} nfs_cb_errtbl[] = {
	{ NFS4_OK,		0		},
	{ NFS4ERR_PERM,		-EPERM		},
	{ NFS4ERR_NOENT,	-ENOENT		},
	{ NFS4ERR_IO,		-EIO		},
	{ NFS4ERR_NXIO,		-ENXIO		},
	{ NFS4ERR_ACCESS,	-EACCES		},
	{ NFS4ERR_EXIST,	-EEXIST		},
	{ NFS4ERR_XDEV,		-EXDEV		},
	{ NFS4ERR_NOTDIR,	-ENOTDIR	},
	{ NFS4ERR_ISDIR,	-EISDIR		},
	{ NFS4ERR_INVAL,	-EINVAL		},
	{ NFS4ERR_FBIG,		-EFBIG		},
	{ NFS4ERR_NOSPC,	-ENOSPC		},
	{ NFS4ERR_ROFS,		-EROFS		},
	{ NFS4ERR_MLINK,	-EMLINK		},
	{ NFS4ERR_NAMETOOLONG,	-ENAMETOOLONG	},
	{ NFS4ERR_NOTEMPTY,	-ENOTEMPTY	},
	{ NFS4ERR_DQUOT,	-EDQUOT		},
	{ NFS4ERR_STALE,	-ESTALE		},
	{ NFS4ERR_BADHANDLE,	-EBADHANDLE	},
	{ NFS4ERR_BAD_COOKIE,	-EBADCOOKIE	},
	{ NFS4ERR_NOTSUPP,	-ENOTSUPP	},
	{ NFS4ERR_TOOSMALL,	-ETOOSMALL	},
	{ NFS4ERR_SERVERFAULT,	-ESERVERFAULT	},
	{ NFS4ERR_BADTYPE,	-EBADTYPE	},
	{ NFS4ERR_LOCKED,	-EAGAIN		},
	{ NFS4ERR_RESOURCE,	-EREMOTEIO	},
	{ NFS4ERR_SYMLINK,	-ELOOP		},
	{ NFS4ERR_OP_ILLEGAL,	-EOPNOTSUPP	},
	{ NFS4ERR_DEADLOCK,	-EDEADLK	},
	{ -1,			-EIO		}
};

/*
 * If we cannot translate the error, the recovery routines should
 * handle it.
 *
 * Note: remaining NFSv4 error codes have values > 10000, so should
 * not conflict with native Linux error codes.
 */
static int nfs_cb_stat_to_errno(int status)
{
	int i;

	for (i = 0; nfs_cb_errtbl[i].stat != -1; i++) {
		if (nfs_cb_errtbl[i].stat == status)
			return nfs_cb_errtbl[i].errno;
	}

	dprintk("NFSD: Unrecognized NFS CB status value: %u\n", status);
	return -status;
}

static int decode_cb_op_status(struct xdr_stream *xdr, enum nfs_opnum4 expected,
			       enum nfsstat4 *status)
{
	__be32 *p;
	u32 op;

	p = xdr_inline_decode(xdr, 4 + 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	op = be32_to_cpup(p++);
	if (unlikely(op != expected))
		goto out_unexpected;
	*status = be32_to_cpup(p);
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
out_unexpected:
	dprintk("NFSD: Callback server returned operation %d but "
		"we issued a request for %d\n", op, expected);
	return -EIO;
}

/*
 * CB_COMPOUND4args
 *
 *	struct CB_COMPOUND4args {
 *		utf8str_cs	tag;
 *		uint32_t	minorversion;
 *		uint32_t	callback_ident;
 *		nfs_cb_argop4	argarray<>;
 *	};
*/
static void encode_cb_compound4args(struct xdr_stream *xdr,
				    struct nfs4_cb_compound_hdr *hdr)
{
	__be32 * p;

	p = xdr_reserve_space(xdr, 4 + 4 + 4 + 4);
	p = xdr_encode_empty_array(p);		/* empty tag */
	*p++ = cpu_to_be32(hdr->minorversion);
	*p++ = cpu_to_be32(hdr->ident);

	hdr->nops_p = p;
	*p = cpu_to_be32(hdr->nops);		/* argarray element count */
}

/*
 * Update argarray element count
 */
static void encode_cb_nops(struct nfs4_cb_compound_hdr *hdr)
{
	BUG_ON(hdr->nops > NFS4_MAX_BACK_CHANNEL_OPS);
	*hdr->nops_p = cpu_to_be32(hdr->nops);
}

/*
 * CB_COMPOUND4res
 *
 *	struct CB_COMPOUND4res {
 *		nfsstat4	status;
 *		utf8str_cs	tag;
 *		nfs_cb_resop4	resarray<>;
 *	};
 */
static int decode_cb_compound4res(struct xdr_stream *xdr,
				  struct nfs4_cb_compound_hdr *hdr)
{
	u32 length;
	__be32 *p;

	p = xdr_inline_decode(xdr, 4 + 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	hdr->status = be32_to_cpup(p++);
	/* Ignore the tag */
	length = be32_to_cpup(p++);
	p = xdr_inline_decode(xdr, length + 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	hdr->nops = be32_to_cpup(p);
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

/*
 * CB_RECALL4args
 *
 *	struct CB_RECALL4args {
 *		stateid4	stateid;
 *		bool		truncate;
 *		nfs_fh4		fh;
 *	};
 */
static void encode_cb_recall4args(struct xdr_stream *xdr,
				  const struct nfs4_delegation *dp,
				  struct nfs4_cb_compound_hdr *hdr)
{
	__be32 *p;

	encode_nfs_cb_opnum4(xdr, OP_CB_RECALL);
	encode_stateid4(xdr, &dp->dl_stid.sc_stateid);

	p = xdr_reserve_space(xdr, 4);
	*p++ = xdr_zero;			/* truncate */

	encode_nfs_fh4(xdr, &dp->dl_fh);

	hdr->nops++;
}

/*
 * CB_SEQUENCE4args
 *
 *	struct CB_SEQUENCE4args {
 *		sessionid4		csa_sessionid;
 *		sequenceid4		csa_sequenceid;
 *		slotid4			csa_slotid;
 *		slotid4			csa_highest_slotid;
 *		bool			csa_cachethis;
 *		referring_call_list4	csa_referring_call_lists<>;
 *	};
 */
static void encode_cb_sequence4args(struct xdr_stream *xdr,
				    const struct nfsd4_callback *cb,
				    struct nfs4_cb_compound_hdr *hdr)
{
	struct nfsd4_session *session = cb->cb_clp->cl_cb_session;
	__be32 *p;

	if (hdr->minorversion == 0)
		return;

	encode_nfs_cb_opnum4(xdr, OP_CB_SEQUENCE);
	encode_sessionid4(xdr, session);

	p = xdr_reserve_space(xdr, 4 + 4 + 4 + 4 + 4);
	*p++ = cpu_to_be32(session->se_cb_seq_nr);	/* csa_sequenceid */
	*p++ = xdr_zero;			/* csa_slotid */
	*p++ = xdr_zero;			/* csa_highest_slotid */
	*p++ = xdr_zero;			/* csa_cachethis */
	xdr_encode_empty_array(p);		/* csa_referring_call_lists */

	hdr->nops++;
}

/*
 * CB_SEQUENCE4resok
 *
 *	struct CB_SEQUENCE4resok {
 *		sessionid4	csr_sessionid;
 *		sequenceid4	csr_sequenceid;
 *		slotid4		csr_slotid;
 *		slotid4		csr_highest_slotid;
 *		slotid4		csr_target_highest_slotid;
 *	};
 *
 *	union CB_SEQUENCE4res switch (nfsstat4 csr_status) {
 *	case NFS4_OK:
 *		CB_SEQUENCE4resok	csr_resok4;
 *	default:
 *		void;
 *	};
 *
 * Our current back channel implmentation supports a single backchannel
 * with a single slot.
 */
static int decode_cb_sequence4resok(struct xdr_stream *xdr,
				    struct nfsd4_callback *cb)
{
	struct nfsd4_session *session = cb->cb_clp->cl_cb_session;
	struct nfs4_sessionid id;
	int status;
	__be32 *p;
	u32 dummy;

	status = -ESERVERFAULT;

	/*
	 * If the server returns different values for sessionID, slotID or
	 * sequence number, the server is looney tunes.
	 */
	p = xdr_inline_decode(xdr, NFS4_MAX_SESSIONID_LEN + 4 + 4 + 4 + 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	memcpy(id.data, p, NFS4_MAX_SESSIONID_LEN);
	if (memcmp(id.data, session->se_sessionid.data,
					NFS4_MAX_SESSIONID_LEN) != 0) {
		dprintk("NFS: %s Invalid session id\n", __func__);
		goto out;
	}
	p += XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN);

	dummy = be32_to_cpup(p++);
	if (dummy != session->se_cb_seq_nr) {
		dprintk("NFS: %s Invalid sequence number\n", __func__);
		goto out;
	}

	dummy = be32_to_cpup(p++);
	if (dummy != 0) {
		dprintk("NFS: %s Invalid slotid\n", __func__);
		goto out;
	}

	/*
	 * FIXME: process highest slotid and target highest slotid
	 */
	status = 0;
out:
	if (status)
		nfsd4_mark_cb_fault(cb->cb_clp, status);
	return status;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_cb_sequence4res(struct xdr_stream *xdr,
				  struct nfsd4_callback *cb)
{
	enum nfsstat4 nfserr;
	int status;

	if (cb->cb_minorversion == 0)
		return 0;

	status = decode_cb_op_status(xdr, OP_CB_SEQUENCE, &nfserr);
	if (unlikely(status))
		goto out;
	if (unlikely(nfserr != NFS4_OK))
		goto out_default;
	status = decode_cb_sequence4resok(xdr, cb);
out:
	return status;
out_default:
	return nfs_cb_stat_to_errno(nfserr);
}

/*
 * NFSv4.0 and NFSv4.1 XDR encode functions
 *
 * NFSv4.0 callback argument types are defined in section 15 of RFC
 * 3530: "Network File System (NFS) version 4 Protocol" and section 20
 * of RFC 5661:  "Network File System (NFS) Version 4 Minor Version 1
 * Protocol".
 */

/*
 * NB: Without this zero space reservation, callbacks over krb5p fail
 */
static void nfs4_xdr_enc_cb_null(struct rpc_rqst *req, struct xdr_stream *xdr,
				 void *__unused)
{
	xdr_reserve_space(xdr, 0);
}

/*
 * 20.2. Operation 4: CB_RECALL - Recall a Delegation
 */
static void nfs4_xdr_enc_cb_recall(struct rpc_rqst *req, struct xdr_stream *xdr,
				   const struct nfsd4_callback *cb)
{
	const struct nfs4_delegation *args = cb->cb_op;
	struct nfs4_cb_compound_hdr hdr = {
		.ident = cb->cb_clp->cl_cb_ident,
		.minorversion = cb->cb_minorversion,
	};

	encode_cb_compound4args(xdr, &hdr);
	encode_cb_sequence4args(xdr, cb, &hdr);
	encode_cb_recall4args(xdr, args, &hdr);
	encode_cb_nops(&hdr);
}


/*
 * NFSv4.0 and NFSv4.1 XDR decode functions
 *
 * NFSv4.0 callback result types are defined in section 15 of RFC
 * 3530: "Network File System (NFS) version 4 Protocol" and section 20
 * of RFC 5661:  "Network File System (NFS) Version 4 Minor Version 1
 * Protocol".
 */

static int nfs4_xdr_dec_cb_null(struct rpc_rqst *req, struct xdr_stream *xdr,
				void *__unused)
{
	return 0;
}

/*
 * 20.2. Operation 4: CB_RECALL - Recall a Delegation
 */
static int nfs4_xdr_dec_cb_recall(struct rpc_rqst *rqstp,
				  struct xdr_stream *xdr,
				  struct nfsd4_callback *cb)
{
	struct nfs4_cb_compound_hdr hdr;
	enum nfsstat4 nfserr;
	int status;

	status = decode_cb_compound4res(xdr, &hdr);
	if (unlikely(status))
		goto out;

	if (cb != NULL) {
		status = decode_cb_sequence4res(xdr, cb);
		if (unlikely(status))
			goto out;
	}

	status = decode_cb_op_status(xdr, OP_CB_RECALL, &nfserr);
	if (unlikely(status))
		goto out;
	if (unlikely(nfserr != NFS4_OK))
		status = nfs_cb_stat_to_errno(nfserr);
out:
	return status;
}

/*
 * RPC procedure tables
 */
#define PROC(proc, call, argtype, restype)				\
[NFSPROC4_CLNT_##proc] = {						\
	.p_proc    = NFSPROC4_CB_##call,				\
	.p_encode  = (kxdreproc_t)nfs4_xdr_enc_##argtype,		\
	.p_decode  = (kxdrdproc_t)nfs4_xdr_dec_##restype,		\
	.p_arglen  = NFS4_enc_##argtype##_sz,				\
	.p_replen  = NFS4_dec_##restype##_sz,				\
	.p_statidx = NFSPROC4_CB_##call,				\
	.p_name    = #proc,						\
}

static struct rpc_procinfo nfs4_cb_procedures[] = {
	PROC(CB_NULL,	NULL,		cb_null,	cb_null),
	PROC(CB_RECALL,	COMPOUND,	cb_recall,	cb_recall),
};

static struct rpc_version nfs_cb_version4 = {
/*
 * Note on the callback rpc program version number: despite language in rfc
 * 5661 section 18.36.3 requiring servers to use 4 in this field, the
 * official xdr descriptions for both 4.0 and 4.1 specify version 1, and
 * in practice that appears to be what implementations use.  The section
 * 18.36.3 language is expected to be fixed in an erratum.
 */
	.number			= 1,
	.nrprocs		= ARRAY_SIZE(nfs4_cb_procedures),
	.procs			= nfs4_cb_procedures
};

static const struct rpc_version *nfs_cb_version[] = {
	&nfs_cb_version4,
};

static const struct rpc_program cb_program;

static struct rpc_stat cb_stats = {
	.program		= &cb_program
};

#define NFS4_CALLBACK 0x40000000
static const struct rpc_program cb_program = {
	.name			= "nfs4_cb",
	.number			= NFS4_CALLBACK,
	.nrvers			= ARRAY_SIZE(nfs_cb_version),
	.version		= nfs_cb_version,
	.stats			= &cb_stats,
	.pipe_dir_name		= "nfsd4_cb",
};

static int max_cb_time(struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	return max(nn->nfsd4_lease/10, (time_t)1) * HZ;
}

static struct rpc_cred *callback_cred;

int set_callback_cred(void)
{
	if (callback_cred)
		return 0;
	callback_cred = rpc_lookup_machine_cred("nfs");
	if (!callback_cred)
		return -ENOMEM;
	return 0;
}

static struct rpc_cred *get_backchannel_cred(struct nfs4_client *clp, struct rpc_clnt *client, struct nfsd4_session *ses)
{
	if (clp->cl_minorversion == 0) {
		return get_rpccred(callback_cred);
	} else {
		struct rpc_auth *auth = client->cl_auth;
		struct auth_cred acred = {};

		acred.uid = ses->se_cb_sec.uid;
		acred.gid = ses->se_cb_sec.gid;
		return auth->au_ops->lookup_cred(client->cl_auth, &acred, 0);
	}
}

static struct rpc_clnt *create_backchannel_client(struct rpc_create_args *args)
{
	struct rpc_xprt *xprt;

	if (args->protocol != XPRT_TRANSPORT_BC_TCP)
		return rpc_create(args);

	xprt = args->bc_xprt->xpt_bc_xprt;
	if (xprt) {
		xprt_get(xprt);
		return rpc_create_xprt(args, xprt);
	}

	return rpc_create(args);
}

static int setup_callback_client(struct nfs4_client *clp, struct nfs4_cb_conn *conn, struct nfsd4_session *ses)
{
	int maxtime = max_cb_time(clp->net);
	struct rpc_timeout	timeparms = {
		.to_initval	= maxtime,
		.to_retries	= 0,
		.to_maxval	= maxtime,
	};
	struct rpc_create_args args = {
		.net		= clp->net,
		.address	= (struct sockaddr *) &conn->cb_addr,
		.addrsize	= conn->cb_addrlen,
		.saddress	= (struct sockaddr *) &conn->cb_saddr,
		.timeout	= &timeparms,
		.program	= &cb_program,
		.version	= 0,
		.flags		= (RPC_CLNT_CREATE_NOPING | RPC_CLNT_CREATE_QUIET),
	};
	struct rpc_clnt *client;
	struct rpc_cred *cred;

	if (clp->cl_minorversion == 0) {
		if (!clp->cl_cred.cr_principal &&
				(clp->cl_cred.cr_flavor >= RPC_AUTH_GSS_KRB5))
			return -EINVAL;
		args.client_name = clp->cl_cred.cr_principal;
		args.prognumber	= conn->cb_prog,
		args.protocol = XPRT_TRANSPORT_TCP;
		args.authflavor = clp->cl_cred.cr_flavor;
		clp->cl_cb_ident = conn->cb_ident;
	} else {
		if (!conn->cb_xprt)
			return -EINVAL;
		clp->cl_cb_conn.cb_xprt = conn->cb_xprt;
		clp->cl_cb_session = ses;
		args.bc_xprt = conn->cb_xprt;
		args.prognumber = clp->cl_cb_session->se_cb_prog;
		args.protocol = XPRT_TRANSPORT_BC_TCP;
		args.authflavor = ses->se_cb_sec.flavor;
	}
	/* Create RPC client */
	client = create_backchannel_client(&args);
	if (IS_ERR(client)) {
		dprintk("NFSD: couldn't create callback client: %ld\n",
			PTR_ERR(client));
		return PTR_ERR(client);
	}
	cred = get_backchannel_cred(clp, client, ses);
	if (IS_ERR(cred)) {
		rpc_shutdown_client(client);
		return PTR_ERR(cred);
	}
	clp->cl_cb_client = client;
	clp->cl_cb_cred = cred;
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

static void nfsd4_mark_cb_fault(struct nfs4_client *clp, int reason)
{
	clp->cl_cb_state = NFSD4_CB_FAULT;
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

	cb->cb_ops = &nfsd4_cb_probe_ops;

	run_nfsd4_cb(cb);
}

/*
 * Poke the callback thread to process any updates to the callback
 * parameters, and send a null probe.
 */
void nfsd4_probe_callback(struct nfs4_client *clp)
{
	clp->cl_cb_state = NFSD4_CB_UNKNOWN;
	set_bit(NFSD4_CLIENT_CB_UPDATE, &clp->cl_flags);
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
static bool nfsd41_cb_get_slot(struct nfs4_client *clp, struct rpc_task *task)
{
	if (test_and_set_bit(0, &clp->cl_cb_slot_busy) != 0) {
		rpc_sleep_on(&clp->cl_cb_waitq, task, NULL);
		dprintk("%s slot is busy\n", __func__);
		return false;
	}
	return true;
}

/*
 * TODO: cb_sequence should support referring call lists, cachethis, multiple
 * slots, and mark callback channel down on communication errors.
 */
static void nfsd4_cb_prepare(struct rpc_task *task, void *calldata)
{
	struct nfsd4_callback *cb = calldata;
	struct nfs4_client *clp = cb->cb_clp;
	u32 minorversion = clp->cl_minorversion;

	cb->cb_minorversion = minorversion;
	if (minorversion) {
		if (!nfsd41_cb_get_slot(clp, task))
			return;
	}
	spin_lock(&clp->cl_lock);
	if (list_empty(&cb->cb_per_client)) {
		/* This is the first call, not a restart */
		cb->cb_done = false;
		list_add(&cb->cb_per_client, &clp->cl_callbacks);
	}
	spin_unlock(&clp->cl_lock);
	rpc_call_start(task);
}

static void nfsd4_cb_done(struct rpc_task *task, void *calldata)
{
	struct nfsd4_callback *cb = calldata;
	struct nfs4_client *clp = cb->cb_clp;

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
	struct nfs4_client *clp = cb->cb_clp;
	struct rpc_clnt *current_rpc_client = clp->cl_cb_client;

	nfsd4_cb_done(task, calldata);

	if (current_rpc_client != task->tk_client) {
		/* We're shutting down or changing cl_cb_client; leave
		 * it to nfsd4_process_cb_update to restart the call if
		 * necessary. */
		return;
	}

	if (cb->cb_done)
		return;
	switch (task->tk_status) {
	case 0:
		cb->cb_done = true;
		return;
	case -EBADHANDLE:
	case -NFS4ERR_BAD_STATEID:
		/* Race: client probably got cb_recall
		 * before open reply granting delegation */
		break;
	default:
		/* Network partition? */
		nfsd4_mark_cb_down(clp, task->tk_status);
	}
	if (dp->dl_retries--) {
		rpc_delay(task, 2*HZ);
		task->tk_status = 0;
		rpc_restart_call_prepare(task);
		return;
	}
	nfsd4_mark_cb_down(clp, task->tk_status);
	cb->cb_done = true;
}

static void nfsd4_cb_recall_release(void *calldata)
{
	struct nfsd4_callback *cb = calldata;
	struct nfs4_client *clp = cb->cb_clp;
	struct nfs4_delegation *dp = container_of(cb, struct nfs4_delegation, dl_recall);

	if (cb->cb_done) {
		spin_lock(&clp->cl_lock);
		list_del(&cb->cb_per_client);
		spin_unlock(&clp->cl_lock);
		nfs4_put_delegation(dp);
	}
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
	set_bit(NFSD4_CLIENT_CB_KILL, &clp->cl_flags);
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
		put_rpccred(clp->cl_cb_cred);
		clp->cl_cb_cred = NULL;
	}
	if (clp->cl_cb_conn.cb_xprt) {
		svc_xprt_put(clp->cl_cb_conn.cb_xprt);
		clp->cl_cb_conn.cb_xprt = NULL;
	}
	if (test_bit(NFSD4_CLIENT_CB_KILL, &clp->cl_flags))
		return;
	spin_lock(&clp->cl_lock);
	/*
	 * Only serialized callback code is allowed to clear these
	 * flags; main nfsd code can only set them:
	 */
	BUG_ON(!(clp->cl_flags & NFSD4_CLIENT_CB_FLAG_MASK));
	clear_bit(NFSD4_CLIENT_CB_UPDATE, &clp->cl_flags);
	memcpy(&conn, &cb->cb_clp->cl_cb_conn, sizeof(struct nfs4_cb_conn));
	c = __nfsd4_find_backchannel(clp);
	if (c) {
		svc_xprt_get(c->cn_xprt);
		conn.cb_xprt = c->cn_xprt;
		ses = c->cn_session;
	}
	spin_unlock(&clp->cl_lock);

	err = setup_callback_client(clp, &conn, ses);
	if (err) {
		nfsd4_mark_cb_down(clp, err);
		return;
	}
	/* Yay, the callback channel's back! Restart any callbacks: */
	list_for_each_entry(cb, &clp->cl_callbacks, cb_per_client)
		run_nfsd4_cb(cb);
}

static void nfsd4_do_callback_rpc(struct work_struct *w)
{
	struct nfsd4_callback *cb = container_of(w, struct nfsd4_callback, cb_work);
	struct nfs4_client *clp = cb->cb_clp;
	struct rpc_clnt *clnt;

	if (clp->cl_flags & NFSD4_CLIENT_CB_FLAG_MASK)
		nfsd4_process_cb_update(cb);

	clnt = clp->cl_cb_client;
	if (!clnt) {
		/* Callback channel broken, or client killed; give up: */
		nfsd4_release_cb(cb);
		return;
	}
	cb->cb_msg.rpc_cred = clp->cl_cb_cred;
	rpc_call_async(clnt, &cb->cb_msg, RPC_TASK_SOFT | RPC_TASK_SOFTCONN,
			cb->cb_ops, cb);
}

void nfsd4_init_callback(struct nfsd4_callback *cb)
{
	INIT_WORK(&cb->cb_work, nfsd4_do_callback_rpc);
}

void nfsd4_cb_recall(struct nfs4_delegation *dp)
{
	struct nfsd4_callback *cb = &dp->dl_recall;
	struct nfs4_client *clp = dp->dl_stid.sc_client;

	dp->dl_retries = 1;
	cb->cb_op = dp;
	cb->cb_clp = clp;
	cb->cb_msg.rpc_proc = &nfs4_cb_procedures[NFSPROC4_CLNT_CB_RECALL];
	cb->cb_msg.rpc_argp = cb;
	cb->cb_msg.rpc_resp = cb;

	cb->cb_ops = &nfsd4_cb_recall_ops;

	INIT_LIST_HEAD(&cb->cb_per_client);
	cb->cb_done = true;

	run_nfsd4_cb(&dp->dl_recall);
}

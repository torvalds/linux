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
#include "xdr4.h"

#define NFSDDBG_FACILITY                NFSDDBG_PROC

static void nfsd4_mark_cb_fault(struct nfs4_client *, int reason);

#define NFSPROC4_CB_NULL 0
#define NFSPROC4_CB_COMPOUND 1

/* Index of predefined Linux callback client operations */

struct nfs4_cb_compound_hdr {
	/* args */
	u32		ident;	/* minorversion 0 only */
	u32		nops;
	__be32		*nops_p;
	u32		minorversion;
	/* res */
	int		status;
};

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
	OP_CB_OFFLOAD			= 15,
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

static int decode_cb_op_status(struct xdr_stream *xdr,
			       enum nfs_cb_opnum4 expected, int *status)
{
	__be32 *p;
	u32 op;

	p = xdr_inline_decode(xdr, 4 + 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	op = be32_to_cpup(p++);
	if (unlikely(op != expected))
		goto out_unexpected;
	*status = nfs_cb_stat_to_errno(be32_to_cpup(p));
	return 0;
out_overflow:
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
	p += XDR_QUADLEN(length);
	hdr->nops = be32_to_cpup(p);
	return 0;
out_overflow:
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

	encode_nfs_fh4(xdr, &dp->dl_stid.sc_file->fi_fhandle);

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
	int status = -ESERVERFAULT;
	__be32 *p;
	u32 dummy;

	/*
	 * If the server returns different values for sessionID, slotID or
	 * sequence number, the server is looney tunes.
	 */
	p = xdr_inline_decode(xdr, NFS4_MAX_SESSIONID_LEN + 4 + 4 + 4 + 4);
	if (unlikely(p == NULL))
		goto out_overflow;

	if (memcmp(p, session->se_sessionid.data, NFS4_MAX_SESSIONID_LEN)) {
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
	cb->cb_seq_status = status;
	return status;
out_overflow:
	status = -EIO;
	goto out;
}

static int decode_cb_sequence4res(struct xdr_stream *xdr,
				  struct nfsd4_callback *cb)
{
	int status;

	if (cb->cb_clp->cl_minorversion == 0)
		return 0;

	status = decode_cb_op_status(xdr, OP_CB_SEQUENCE, &cb->cb_seq_status);
	if (unlikely(status || cb->cb_seq_status))
		return status;

	return decode_cb_sequence4resok(xdr, cb);
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
				 const void *__unused)
{
	xdr_reserve_space(xdr, 0);
}

/*
 * 20.2. Operation 4: CB_RECALL - Recall a Delegation
 */
static void nfs4_xdr_enc_cb_recall(struct rpc_rqst *req, struct xdr_stream *xdr,
				   const void *data)
{
	const struct nfsd4_callback *cb = data;
	const struct nfs4_delegation *dp = cb_to_delegation(cb);
	struct nfs4_cb_compound_hdr hdr = {
		.ident = cb->cb_clp->cl_cb_ident,
		.minorversion = cb->cb_clp->cl_minorversion,
	};

	encode_cb_compound4args(xdr, &hdr);
	encode_cb_sequence4args(xdr, cb, &hdr);
	encode_cb_recall4args(xdr, dp, &hdr);
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
				  void *data)
{
	struct nfsd4_callback *cb = data;
	struct nfs4_cb_compound_hdr hdr;
	int status;

	status = decode_cb_compound4res(xdr, &hdr);
	if (unlikely(status))
		return status;

	if (cb != NULL) {
		status = decode_cb_sequence4res(xdr, cb);
		if (unlikely(status || cb->cb_seq_status))
			return status;
	}

	return decode_cb_op_status(xdr, OP_CB_RECALL, &cb->cb_status);
}

#ifdef CONFIG_NFSD_PNFS
/*
 * CB_LAYOUTRECALL4args
 *
 *	struct layoutrecall_file4 {
 *		nfs_fh4         lor_fh;
 *		offset4         lor_offset;
 *		length4         lor_length;
 *		stateid4        lor_stateid;
 *	};
 *
 *	union layoutrecall4 switch(layoutrecall_type4 lor_recalltype) {
 *	case LAYOUTRECALL4_FILE:
 *		layoutrecall_file4 lor_layout;
 *	case LAYOUTRECALL4_FSID:
 *		fsid4              lor_fsid;
 *	case LAYOUTRECALL4_ALL:
 *		void;
 *	};
 *
 *	struct CB_LAYOUTRECALL4args {
 *		layouttype4             clora_type;
 *		layoutiomode4           clora_iomode;
 *		bool                    clora_changed;
 *		layoutrecall4           clora_recall;
 *	};
 */
static void encode_cb_layout4args(struct xdr_stream *xdr,
				  const struct nfs4_layout_stateid *ls,
				  struct nfs4_cb_compound_hdr *hdr)
{
	__be32 *p;

	BUG_ON(hdr->minorversion == 0);

	p = xdr_reserve_space(xdr, 5 * 4);
	*p++ = cpu_to_be32(OP_CB_LAYOUTRECALL);
	*p++ = cpu_to_be32(ls->ls_layout_type);
	*p++ = cpu_to_be32(IOMODE_ANY);
	*p++ = cpu_to_be32(1);
	*p = cpu_to_be32(RETURN_FILE);

	encode_nfs_fh4(xdr, &ls->ls_stid.sc_file->fi_fhandle);

	p = xdr_reserve_space(xdr, 2 * 8);
	p = xdr_encode_hyper(p, 0);
	xdr_encode_hyper(p, NFS4_MAX_UINT64);

	encode_stateid4(xdr, &ls->ls_recall_sid);

	hdr->nops++;
}

static void nfs4_xdr_enc_cb_layout(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   const void *data)
{
	const struct nfsd4_callback *cb = data;
	const struct nfs4_layout_stateid *ls =
		container_of(cb, struct nfs4_layout_stateid, ls_recall);
	struct nfs4_cb_compound_hdr hdr = {
		.ident = 0,
		.minorversion = cb->cb_clp->cl_minorversion,
	};

	encode_cb_compound4args(xdr, &hdr);
	encode_cb_sequence4args(xdr, cb, &hdr);
	encode_cb_layout4args(xdr, ls, &hdr);
	encode_cb_nops(&hdr);
}

static int nfs4_xdr_dec_cb_layout(struct rpc_rqst *rqstp,
				  struct xdr_stream *xdr,
				  void *data)
{
	struct nfsd4_callback *cb = data;
	struct nfs4_cb_compound_hdr hdr;
	int status;

	status = decode_cb_compound4res(xdr, &hdr);
	if (unlikely(status))
		return status;

	if (cb) {
		status = decode_cb_sequence4res(xdr, cb);
		if (unlikely(status || cb->cb_seq_status))
			return status;
	}
	return decode_cb_op_status(xdr, OP_CB_LAYOUTRECALL, &cb->cb_status);
}
#endif /* CONFIG_NFSD_PNFS */

static void encode_stateowner(struct xdr_stream *xdr, struct nfs4_stateowner *so)
{
	__be32	*p;

	p = xdr_reserve_space(xdr, 8 + 4 + so->so_owner.len);
	p = xdr_encode_opaque_fixed(p, &so->so_client->cl_clientid, 8);
	xdr_encode_opaque(p, so->so_owner.data, so->so_owner.len);
}

static void nfs4_xdr_enc_cb_notify_lock(struct rpc_rqst *req,
					struct xdr_stream *xdr,
					const void *data)
{
	const struct nfsd4_callback *cb = data;
	const struct nfsd4_blocked_lock *nbl =
		container_of(cb, struct nfsd4_blocked_lock, nbl_cb);
	struct nfs4_lockowner *lo = (struct nfs4_lockowner *)nbl->nbl_lock.fl_owner;
	struct nfs4_cb_compound_hdr hdr = {
		.ident = 0,
		.minorversion = cb->cb_clp->cl_minorversion,
	};

	__be32 *p;

	BUG_ON(hdr.minorversion == 0);

	encode_cb_compound4args(xdr, &hdr);
	encode_cb_sequence4args(xdr, cb, &hdr);

	p = xdr_reserve_space(xdr, 4);
	*p = cpu_to_be32(OP_CB_NOTIFY_LOCK);
	encode_nfs_fh4(xdr, &nbl->nbl_fh);
	encode_stateowner(xdr, &lo->lo_owner);
	hdr.nops++;

	encode_cb_nops(&hdr);
}

static int nfs4_xdr_dec_cb_notify_lock(struct rpc_rqst *rqstp,
					struct xdr_stream *xdr,
					void *data)
{
	struct nfsd4_callback *cb = data;
	struct nfs4_cb_compound_hdr hdr;
	int status;

	status = decode_cb_compound4res(xdr, &hdr);
	if (unlikely(status))
		return status;

	if (cb) {
		status = decode_cb_sequence4res(xdr, cb);
		if (unlikely(status || cb->cb_seq_status))
			return status;
	}
	return decode_cb_op_status(xdr, OP_CB_NOTIFY_LOCK, &cb->cb_status);
}

/*
 * struct write_response4 {
 *	stateid4	wr_callback_id<1>;
 *	length4		wr_count;
 *	stable_how4	wr_committed;
 *	verifier4	wr_writeverf;
 * };
 * union offload_info4 switch (nfsstat4 coa_status) {
 *	case NFS4_OK:
 *		write_response4	coa_resok4;
 *	default:
 *	length4		coa_bytes_copied;
 * };
 * struct CB_OFFLOAD4args {
 *	nfs_fh4		coa_fh;
 *	stateid4	coa_stateid;
 *	offload_info4	coa_offload_info;
 * };
 */
static void encode_offload_info4(struct xdr_stream *xdr,
				 __be32 nfserr,
				 const struct nfsd4_copy *cp)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, 4);
	*p++ = nfserr;
	if (!nfserr) {
		p = xdr_reserve_space(xdr, 4 + 8 + 4 + NFS4_VERIFIER_SIZE);
		p = xdr_encode_empty_array(p);
		p = xdr_encode_hyper(p, cp->cp_res.wr_bytes_written);
		*p++ = cpu_to_be32(cp->cp_res.wr_stable_how);
		p = xdr_encode_opaque_fixed(p, cp->cp_res.wr_verifier.data,
					    NFS4_VERIFIER_SIZE);
	} else {
		p = xdr_reserve_space(xdr, 8);
		/* We always return success if bytes were written */
		p = xdr_encode_hyper(p, 0);
	}
}

static void encode_cb_offload4args(struct xdr_stream *xdr,
				   __be32 nfserr,
				   const struct knfsd_fh *fh,
				   const struct nfsd4_copy *cp,
				   struct nfs4_cb_compound_hdr *hdr)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, 4);
	*p++ = cpu_to_be32(OP_CB_OFFLOAD);
	encode_nfs_fh4(xdr, fh);
	encode_stateid4(xdr, &cp->cp_res.cb_stateid);
	encode_offload_info4(xdr, nfserr, cp);

	hdr->nops++;
}

static void nfs4_xdr_enc_cb_offload(struct rpc_rqst *req,
				    struct xdr_stream *xdr,
				    const void *data)
{
	const struct nfsd4_callback *cb = data;
	const struct nfsd4_copy *cp =
		container_of(cb, struct nfsd4_copy, cp_cb);
	struct nfs4_cb_compound_hdr hdr = {
		.ident = 0,
		.minorversion = cb->cb_clp->cl_minorversion,
	};

	encode_cb_compound4args(xdr, &hdr);
	encode_cb_sequence4args(xdr, cb, &hdr);
	encode_cb_offload4args(xdr, cp->nfserr, &cp->fh, cp, &hdr);
	encode_cb_nops(&hdr);
}

static int nfs4_xdr_dec_cb_offload(struct rpc_rqst *rqstp,
				   struct xdr_stream *xdr,
				   void *data)
{
	struct nfsd4_callback *cb = data;
	struct nfs4_cb_compound_hdr hdr;
	int status;

	status = decode_cb_compound4res(xdr, &hdr);
	if (unlikely(status))
		return status;

	if (cb) {
		status = decode_cb_sequence4res(xdr, cb);
		if (unlikely(status || cb->cb_seq_status))
			return status;
	}
	return decode_cb_op_status(xdr, OP_CB_OFFLOAD, &cb->cb_status);
}
/*
 * RPC procedure tables
 */
#define PROC(proc, call, argtype, restype)				\
[NFSPROC4_CLNT_##proc] = {						\
	.p_proc    = NFSPROC4_CB_##call,				\
	.p_encode  = nfs4_xdr_enc_##argtype,		\
	.p_decode  = nfs4_xdr_dec_##restype,				\
	.p_arglen  = NFS4_enc_##argtype##_sz,				\
	.p_replen  = NFS4_dec_##restype##_sz,				\
	.p_statidx = NFSPROC4_CB_##call,				\
	.p_name    = #proc,						\
}

static const struct rpc_procinfo nfs4_cb_procedures[] = {
	PROC(CB_NULL,	NULL,		cb_null,	cb_null),
	PROC(CB_RECALL,	COMPOUND,	cb_recall,	cb_recall),
#ifdef CONFIG_NFSD_PNFS
	PROC(CB_LAYOUT,	COMPOUND,	cb_layout,	cb_layout),
#endif
	PROC(CB_NOTIFY_LOCK,	COMPOUND,	cb_notify_lock,	cb_notify_lock),
	PROC(CB_OFFLOAD,	COMPOUND,	cb_offload,	cb_offload),
};

static unsigned int nfs4_cb_counts[ARRAY_SIZE(nfs4_cb_procedures)];
static const struct rpc_version nfs_cb_version4 = {
/*
 * Note on the callback rpc program version number: despite language in rfc
 * 5661 section 18.36.3 requiring servers to use 4 in this field, the
 * official xdr descriptions for both 4.0 and 4.1 specify version 1, and
 * in practice that appears to be what implementations use.  The section
 * 18.36.3 language is expected to be fixed in an erratum.
 */
	.number			= 1,
	.nrprocs		= ARRAY_SIZE(nfs4_cb_procedures),
	.procs			= nfs4_cb_procedures,
	.counts			= nfs4_cb_counts,
};

static const struct rpc_version *nfs_cb_version[2] = {
	[1] = &nfs_cb_version4,
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

static const struct cred *get_backchannel_cred(struct nfs4_client *clp, struct rpc_clnt *client, struct nfsd4_session *ses)
{
	if (clp->cl_minorversion == 0) {
		client->cl_principal = clp->cl_cred.cr_targ_princ ?
			clp->cl_cred.cr_targ_princ : "nfs";

		return get_cred(rpc_machine_cred());
	} else {
		struct cred *kcred;

		kcred = prepare_kernel_cred(NULL);
		if (!kcred)
			return NULL;

		kcred->uid = ses->se_cb_sec.uid;
		kcred->gid = ses->se_cb_sec.gid;
		return kcred;
	}
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
		.version	= 1,
		.flags		= (RPC_CLNT_CREATE_NOPING | RPC_CLNT_CREATE_QUIET),
		.cred		= current_cred(),
	};
	struct rpc_clnt *client;
	const struct cred *cred;

	if (clp->cl_minorversion == 0) {
		if (!clp->cl_cred.cr_principal &&
				(clp->cl_cred.cr_flavor >= RPC_AUTH_GSS_KRB5))
			return -EINVAL;
		args.client_name = clp->cl_cred.cr_principal;
		args.prognumber	= conn->cb_prog;
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
		args.protocol = conn->cb_xprt->xpt_class->xcl_ident |
				XPRT_TRANSPORT_BC;
		args.authflavor = ses->se_cb_sec.flavor;
	}
	/* Create RPC client */
	client = rpc_create(&args);
	if (IS_ERR(client)) {
		dprintk("NFSD: couldn't create callback client: %ld\n",
			PTR_ERR(client));
		return PTR_ERR(client);
	}
	cred = get_backchannel_cred(clp, client, ses);
	if (!cred) {
		rpc_shutdown_client(client);
		return -ENOMEM;
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
	if (test_bit(NFSD4_CLIENT_CB_UPDATE, &clp->cl_flags))
		return;
	clp->cl_cb_state = NFSD4_CB_DOWN;
	warn_no_callback_path(clp, reason);
}

static void nfsd4_mark_cb_fault(struct nfs4_client *clp, int reason)
{
	if (test_bit(NFSD4_CLIENT_CB_UPDATE, &clp->cl_flags))
		return;
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

/*
 * Poke the callback thread to process any updates to the callback
 * parameters, and send a null probe.
 */
void nfsd4_probe_callback(struct nfs4_client *clp)
{
	clp->cl_cb_state = NFSD4_CB_UNKNOWN;
	set_bit(NFSD4_CLIENT_CB_UPDATE, &clp->cl_flags);
	nfsd4_run_cb(&clp->cl_cb_null);
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
		/* Race breaker */
		if (test_and_set_bit(0, &clp->cl_cb_slot_busy) != 0) {
			dprintk("%s slot is busy\n", __func__);
			return false;
		}
		rpc_wake_up_queued_task(&clp->cl_cb_waitq, task);
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

	/*
	 * cb_seq_status is only set in decode_cb_sequence4res,
	 * and so will remain 1 if an rpc level failure occurs.
	 */
	cb->cb_seq_status = 1;
	cb->cb_status = 0;
	if (minorversion) {
		if (!cb->cb_holds_slot && !nfsd41_cb_get_slot(clp, task))
			return;
		cb->cb_holds_slot = true;
	}
	rpc_call_start(task);
}

static bool nfsd4_cb_sequence_done(struct rpc_task *task, struct nfsd4_callback *cb)
{
	struct nfs4_client *clp = cb->cb_clp;
	struct nfsd4_session *session = clp->cl_cb_session;
	bool ret = true;

	if (!clp->cl_minorversion) {
		/*
		 * If the backchannel connection was shut down while this
		 * task was queued, we need to resubmit it after setting up
		 * a new backchannel connection.
		 *
		 * Note that if we lost our callback connection permanently
		 * the submission code will error out, so we don't need to
		 * handle that case here.
		 */
		if (RPC_SIGNALLED(task))
			goto need_restart;

		return true;
	}

	if (!cb->cb_holds_slot)
		goto need_restart;

	switch (cb->cb_seq_status) {
	case 0:
		/*
		 * No need for lock, access serialized in nfsd4_cb_prepare
		 *
		 * RFC5661 20.9.3
		 * If CB_SEQUENCE returns an error, then the state of the slot
		 * (sequence ID, cached reply) MUST NOT change.
		 */
		++session->se_cb_seq_nr;
		break;
	case -ESERVERFAULT:
		++session->se_cb_seq_nr;
		/* Fall through */
	case 1:
	case -NFS4ERR_BADSESSION:
		nfsd4_mark_cb_fault(cb->cb_clp, cb->cb_seq_status);
		ret = false;
		break;
	case -NFS4ERR_DELAY:
		if (!rpc_restart_call(task))
			goto out;

		rpc_delay(task, 2 * HZ);
		return false;
	case -NFS4ERR_BADSLOT:
		goto retry_nowait;
	case -NFS4ERR_SEQ_MISORDERED:
		if (session->se_cb_seq_nr != 1) {
			session->se_cb_seq_nr = 1;
			goto retry_nowait;
		}
		break;
	default:
		dprintk("%s: unprocessed error %d\n", __func__,
			cb->cb_seq_status);
	}

	cb->cb_holds_slot = false;
	clear_bit(0, &clp->cl_cb_slot_busy);
	rpc_wake_up_next(&clp->cl_cb_waitq);
	dprintk("%s: freed slot, new seqid=%d\n", __func__,
		clp->cl_cb_session->se_cb_seq_nr);

	if (RPC_SIGNALLED(task))
		goto need_restart;
out:
	return ret;
retry_nowait:
	if (rpc_restart_call_prepare(task))
		ret = false;
	goto out;
need_restart:
	task->tk_status = 0;
	cb->cb_need_restart = true;
	return false;
}

static void nfsd4_cb_done(struct rpc_task *task, void *calldata)
{
	struct nfsd4_callback *cb = calldata;
	struct nfs4_client *clp = cb->cb_clp;

	dprintk("%s: minorversion=%d\n", __func__,
		clp->cl_minorversion);

	if (!nfsd4_cb_sequence_done(task, cb))
		return;

	if (cb->cb_status) {
		WARN_ON_ONCE(task->tk_status);
		task->tk_status = cb->cb_status;
	}

	switch (cb->cb_ops->done(cb, task)) {
	case 0:
		task->tk_status = 0;
		rpc_restart_call_prepare(task);
		return;
	case 1:
		switch (task->tk_status) {
		case -EIO:
		case -ETIMEDOUT:
			nfsd4_mark_cb_down(clp, task->tk_status);
		}
		break;
	default:
		BUG();
	}
}

static void nfsd4_cb_release(void *calldata)
{
	struct nfsd4_callback *cb = calldata;

	if (cb->cb_need_restart)
		nfsd4_run_cb(cb);
	else
		cb->cb_ops->release(cb);

}

static const struct rpc_call_ops nfsd4_cb_ops = {
	.rpc_call_prepare = nfsd4_cb_prepare,
	.rpc_call_done = nfsd4_cb_done,
	.rpc_release = nfsd4_cb_release,
};

int nfsd4_create_callback_queue(void)
{
	callback_wq = alloc_ordered_workqueue("nfsd4_callbacks", 0);
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
	 * instead, nfsd4_run_cb_null() will detect the killed
	 * client, destroy the rpc client, and stop:
	 */
	nfsd4_run_cb(&clp->cl_cb_null);
	flush_workqueue(callback_wq);
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
		put_cred(clp->cl_cb_cred);
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
}

static void
nfsd4_run_cb_work(struct work_struct *work)
{
	struct nfsd4_callback *cb =
		container_of(work, struct nfsd4_callback, cb_work);
	struct nfs4_client *clp = cb->cb_clp;
	struct rpc_clnt *clnt;

	if (cb->cb_need_restart) {
		cb->cb_need_restart = false;
	} else {
		if (cb->cb_ops && cb->cb_ops->prepare)
			cb->cb_ops->prepare(cb);
	}

	if (clp->cl_flags & NFSD4_CLIENT_CB_FLAG_MASK)
		nfsd4_process_cb_update(cb);

	clnt = clp->cl_cb_client;
	if (!clnt) {
		/* Callback channel broken, or client killed; give up: */
		if (cb->cb_ops && cb->cb_ops->release)
			cb->cb_ops->release(cb);
		return;
	}

	/*
	 * Don't send probe messages for 4.1 or later.
	 */
	if (!cb->cb_ops && clp->cl_minorversion) {
		clp->cl_cb_state = NFSD4_CB_UP;
		return;
	}

	cb->cb_msg.rpc_cred = clp->cl_cb_cred;
	rpc_call_async(clnt, &cb->cb_msg, RPC_TASK_SOFT | RPC_TASK_SOFTCONN,
			cb->cb_ops ? &nfsd4_cb_ops : &nfsd4_cb_probe_ops, cb);
}

void nfsd4_init_cb(struct nfsd4_callback *cb, struct nfs4_client *clp,
		const struct nfsd4_callback_ops *ops, enum nfsd4_cb_op op)
{
	cb->cb_clp = clp;
	cb->cb_msg.rpc_proc = &nfs4_cb_procedures[op];
	cb->cb_msg.rpc_argp = cb;
	cb->cb_msg.rpc_resp = cb;
	cb->cb_ops = ops;
	INIT_WORK(&cb->cb_work, nfsd4_run_cb_work);
	cb->cb_seq_status = 1;
	cb->cb_status = 0;
	cb->cb_need_restart = false;
	cb->cb_holds_slot = false;
}

void nfsd4_run_cb(struct nfsd4_callback *cb)
{
	queue_work(callback_wq, &cb->cb_work);
}

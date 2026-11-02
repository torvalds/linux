/* SPDX-License-Identifier: GPL-2.0 */
/* XDR types for nfsd. This is mainly a typing exercise. */

#ifndef _LINUX_NFSD_XDR_H
#define _LINUX_NFSD_XDR_H

#include <linux/vfs.h>

#include "vfs.h"

struct nfsd_attrstat {
	__be32			status;
	struct svc_fh		fh;
	struct kstat		stat;
};

/*
 * Declared here rather than in nfsproc.c with the other proc wrappers
 * because READDIR's encode hooks in nfsxdr.c reference it. The xdrgen
 * field must be first so the struct can be cast to its XDR type for the
 * RPC dispatch layer.
 *
 * The file handle lives here rather than in an argument wrapper: the
 * RPC layer zeroes the result before decode, so ->pc_release can run on
 * a request that failed to decode.
 */
struct readdirres_wrapper {
	struct readdirres	xdrgen;
	struct svc_fh		fh;

	/* Streaming state for encoding the reply's entry list */
	struct nfsd_readdir_iter iter;		/* directory reader */
	u32			count;		/* client's reply size hint */
	u32			space_left;	/* remaining entry budget */
	unsigned int		cookie_offset;	/* prev entry's cookie slot */
};

static_assert(offsetof(struct readdirres_wrapper, xdrgen) == 0);

bool nfssvc_decode_fhandleargs(struct svc_rqst *rqstp, struct xdr_stream *xdr);

bool nfssvc_encode_attrstatres(struct svc_rqst *rqstp, struct xdr_stream *xdr);

void nfssvc_encode_nfscookie(struct xdr_stream *xdr, unsigned int pos,
			     u32 cookie);

void nfssvc_release_readdirres(struct svc_rqst *rqstp);

void nfssvc_release_attrstat(struct svc_rqst *rqstp);

/* Helper functions for NFSv2 ACL code */
bool svcxdr_decode_fhandle(struct xdr_stream *xdr, struct svc_fh *fhp);
bool svcxdr_encode_stat(struct xdr_stream *xdr, __be32 status);
bool svcxdr_encode_fattr(struct svc_rqst *rqstp, struct xdr_stream *xdr,
			 const struct svc_fh *fhp, const struct kstat *stat);

#endif /* _LINUX_NFSD_XDR_H */

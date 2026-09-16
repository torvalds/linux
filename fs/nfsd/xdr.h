/* SPDX-License-Identifier: GPL-2.0 */
/* XDR types for nfsd. This is mainly a typing exercise. */

#ifndef _LINUX_NFSD_XDR_H
#define _LINUX_NFSD_XDR_H

#include <linux/vfs.h>

#include "vfs.h"

struct nfsd_readdirargs {
	struct svc_fh		fh;
	__u32			cookie;
	__u32			count;
};

struct nfsd_attrstat {
	__be32			status;
	struct svc_fh		fh;
	struct kstat		stat;
};

struct nfsd_readdirres {
	/* Components of the reply */
	__be32			status;

	int			count;

	/* Used to encode the reply's entry list */
	struct xdr_stream	xdr;
	struct xdr_buf		dirlist;
	struct readdir_cd	common;
	unsigned int		cookie_offset;
};

bool nfssvc_decode_fhandleargs(struct svc_rqst *rqstp, struct xdr_stream *xdr);
bool nfssvc_decode_readdirargs(struct svc_rqst *rqstp, struct xdr_stream *xdr);

bool nfssvc_encode_attrstatres(struct svc_rqst *rqstp, struct xdr_stream *xdr);
bool nfssvc_encode_readdirres(struct svc_rqst *rqstp, struct xdr_stream *xdr);

void nfssvc_encode_nfscookie(struct nfsd_readdirres *resp, u32 offset);
int nfssvc_encode_entry(void *data, const char *name, int namlen,
			loff_t offset, u64 ino, unsigned int d_type);

void nfssvc_release_attrstat(struct svc_rqst *rqstp);

/* Helper functions for NFSv2 ACL code */
bool svcxdr_decode_fhandle(struct xdr_stream *xdr, struct svc_fh *fhp);
bool svcxdr_encode_stat(struct xdr_stream *xdr, __be32 status);
bool svcxdr_encode_fattr(struct svc_rqst *rqstp, struct xdr_stream *xdr,
			 const struct svc_fh *fhp, const struct kstat *stat);

#endif /* _LINUX_NFSD_XDR_H */

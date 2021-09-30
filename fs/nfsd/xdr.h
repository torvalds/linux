/* SPDX-License-Identifier: GPL-2.0 */
/* XDR types for nfsd. This is mainly a typing exercise. */

#ifndef LINUX_NFSD_H
#define LINUX_NFSD_H

#include <linux/vfs.h>
#include "nfsd.h"
#include "nfsfh.h"

struct nfsd_fhandle {
	struct svc_fh		fh;
};

struct nfsd_sattrargs {
	struct svc_fh		fh;
	struct iattr		attrs;
};

struct nfsd_diropargs {
	struct svc_fh		fh;
	char *			name;
	unsigned int		len;
};

struct nfsd_readargs {
	struct svc_fh		fh;
	__u32			offset;
	__u32			count;
};

struct nfsd_writeargs {
	svc_fh			fh;
	__u32			offset;
	int			len;
	struct xdr_buf		payload;
};

struct nfsd_createargs {
	struct svc_fh		fh;
	char *			name;
	unsigned int		len;
	struct iattr		attrs;
};

struct nfsd_renameargs {
	struct svc_fh		ffh;
	char *			fname;
	unsigned int		flen;
	struct svc_fh		tfh;
	char *			tname;
	unsigned int		tlen;
};

struct nfsd_linkargs {
	struct svc_fh		ffh;
	struct svc_fh		tfh;
	char *			tname;
	unsigned int		tlen;
};

struct nfsd_symlinkargs {
	struct svc_fh		ffh;
	char *			fname;
	unsigned int		flen;
	char *			tname;
	unsigned int		tlen;
	struct iattr		attrs;
	struct kvec		first;
};

struct nfsd_readdirargs {
	struct svc_fh		fh;
	__u32			cookie;
	__u32			count;
};

struct nfsd_stat {
	__be32			status;
};

struct nfsd_attrstat {
	__be32			status;
	struct svc_fh		fh;
	struct kstat		stat;
};

struct nfsd_diropres  {
	__be32			status;
	struct svc_fh		fh;
	struct kstat		stat;
};

struct nfsd_readlinkres {
	__be32			status;
	int			len;
	struct page		*page;
};

struct nfsd_readres {
	__be32			status;
	struct svc_fh		fh;
	unsigned long		count;
	struct kstat		stat;
	struct page		**pages;
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

struct nfsd_statfsres {
	__be32			status;
	struct kstatfs		stats;
};

/*
 * Storage requirements for XDR arguments and results.
 */
union nfsd_xdrstore {
	struct nfsd_sattrargs	sattr;
	struct nfsd_diropargs	dirop;
	struct nfsd_readargs	read;
	struct nfsd_writeargs	write;
	struct nfsd_createargs	create;
	struct nfsd_renameargs	rename;
	struct nfsd_linkargs	link;
	struct nfsd_symlinkargs	symlink;
	struct nfsd_readdirargs	readdir;
};

#define NFS2_SVC_XDRSIZE	sizeof(union nfsd_xdrstore)


int nfssvc_decode_fhandleargs(struct svc_rqst *, __be32 *);
int nfssvc_decode_sattrargs(struct svc_rqst *, __be32 *);
int nfssvc_decode_diropargs(struct svc_rqst *, __be32 *);
int nfssvc_decode_readargs(struct svc_rqst *, __be32 *);
int nfssvc_decode_writeargs(struct svc_rqst *, __be32 *);
int nfssvc_decode_createargs(struct svc_rqst *, __be32 *);
int nfssvc_decode_renameargs(struct svc_rqst *, __be32 *);
int nfssvc_decode_linkargs(struct svc_rqst *, __be32 *);
int nfssvc_decode_symlinkargs(struct svc_rqst *, __be32 *);
int nfssvc_decode_readdirargs(struct svc_rqst *, __be32 *);
int nfssvc_encode_statres(struct svc_rqst *, __be32 *);
int nfssvc_encode_attrstatres(struct svc_rqst *, __be32 *);
int nfssvc_encode_diropres(struct svc_rqst *, __be32 *);
int nfssvc_encode_readlinkres(struct svc_rqst *, __be32 *);
int nfssvc_encode_readres(struct svc_rqst *, __be32 *);
int nfssvc_encode_statfsres(struct svc_rqst *, __be32 *);
int nfssvc_encode_readdirres(struct svc_rqst *, __be32 *);

void nfssvc_encode_nfscookie(struct nfsd_readdirres *resp, u32 offset);
int nfssvc_encode_entry(void *data, const char *name, int namlen,
			loff_t offset, u64 ino, unsigned int d_type);

void nfssvc_release_attrstat(struct svc_rqst *rqstp);
void nfssvc_release_diropres(struct svc_rqst *rqstp);
void nfssvc_release_readres(struct svc_rqst *rqstp);

/* Helper functions for NFSv2 ACL code */
bool svcxdr_decode_fhandle(struct xdr_stream *xdr, struct svc_fh *fhp);
bool svcxdr_encode_stat(struct xdr_stream *xdr, __be32 status);
bool svcxdr_encode_fattr(struct svc_rqst *rqstp, struct xdr_stream *xdr,
			 const struct svc_fh *fhp, const struct kstat *stat);

#endif /* LINUX_NFSD_H */

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * XDR types for NFSv3 in nfsd.
 *
 * Copyright (C) 1996-1998, Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_NFSD_XDR3_H
#define _LINUX_NFSD_XDR3_H

#include "xdr.h"

struct nfsd3_sattrargs {
	struct svc_fh		fh;
	struct iattr		attrs;
	int			check_guard;
	time_t			guardtime;
};

struct nfsd3_diropargs {
	struct svc_fh		fh;
	char *			name;
	unsigned int		len;
};

struct nfsd3_accessargs {
	struct svc_fh		fh;
	unsigned int		access;
};

struct nfsd3_readargs {
	struct svc_fh		fh;
	__u64			offset;
	__u32			count;
	int			vlen;
};

struct nfsd3_writeargs {
	svc_fh			fh;
	__u64			offset;
	__u32			count;
	int			stable;
	__u32			len;
	int			vlen;
};

struct nfsd3_createargs {
	struct svc_fh		fh;
	char *			name;
	unsigned int		len;
	int			createmode;
	struct iattr		attrs;
	__be32 *		verf;
};

struct nfsd3_mknodargs {
	struct svc_fh		fh;
	char *			name;
	unsigned int		len;
	__u32			ftype;
	__u32			major, minor;
	struct iattr		attrs;
};

struct nfsd3_renameargs {
	struct svc_fh		ffh;
	char *			fname;
	unsigned int		flen;
	struct svc_fh		tfh;
	char *			tname;
	unsigned int		tlen;
};

struct nfsd3_readlinkargs {
	struct svc_fh		fh;
	char *			buffer;
};

struct nfsd3_linkargs {
	struct svc_fh		ffh;
	struct svc_fh		tfh;
	char *			tname;
	unsigned int		tlen;
};

struct nfsd3_symlinkargs {
	struct svc_fh		ffh;
	char *			fname;
	unsigned int		flen;
	char *			tname;
	unsigned int		tlen;
	struct iattr		attrs;
};

struct nfsd3_readdirargs {
	struct svc_fh		fh;
	__u64			cookie;
	__u32			dircount;
	__u32			count;
	__be32 *		verf;
	__be32 *		buffer;
};

struct nfsd3_commitargs {
	struct svc_fh		fh;
	__u64			offset;
	__u32			count;
};

struct nfsd3_getaclargs {
	struct svc_fh		fh;
	int			mask;
};

struct posix_acl;
struct nfsd3_setaclargs {
	struct svc_fh		fh;
	int			mask;
	struct posix_acl	*acl_access;
	struct posix_acl	*acl_default;
};

struct nfsd3_attrstat {
	__be32			status;
	struct svc_fh		fh;
	struct kstat            stat;
};

/* LOOKUP, CREATE, MKDIR, SYMLINK, MKNOD */
struct nfsd3_diropres  {
	__be32			status;
	struct svc_fh		dirfh;
	struct svc_fh		fh;
};

struct nfsd3_accessres {
	__be32			status;
	struct svc_fh		fh;
	__u32			access;
	struct kstat		stat;
};

struct nfsd3_readlinkres {
	__be32			status;
	struct svc_fh		fh;
	__u32			len;
};

struct nfsd3_readres {
	__be32			status;
	struct svc_fh		fh;
	unsigned long		count;
	int			eof;
};

struct nfsd3_writeres {
	__be32			status;
	struct svc_fh		fh;
	unsigned long		count;
	int			committed;
};

struct nfsd3_renameres {
	__be32			status;
	struct svc_fh		ffh;
	struct svc_fh		tfh;
};

struct nfsd3_linkres {
	__be32			status;
	struct svc_fh		tfh;
	struct svc_fh		fh;
};

struct nfsd3_readdirres {
	__be32			status;
	struct svc_fh		fh;
	/* Just to save kmalloc on every readdirplus entry (svc_fh is a
	 * little large for the stack): */
	struct svc_fh		scratch;
	int			count;
	__be32			verf[2];

	struct readdir_cd	common;
	__be32 *		buffer;
	int			buflen;
	__be32 *		offset;
	__be32 *		offset1;
	struct svc_rqst *	rqstp;

};

struct nfsd3_fsstatres {
	__be32			status;
	struct kstatfs		stats;
	__u32			invarsec;
};

struct nfsd3_fsinfores {
	__be32			status;
	__u32			f_rtmax;
	__u32			f_rtpref;
	__u32			f_rtmult;
	__u32			f_wtmax;
	__u32			f_wtpref;
	__u32			f_wtmult;
	__u32			f_dtpref;
	__u64			f_maxfilesize;
	__u32			f_properties;
};

struct nfsd3_pathconfres {
	__be32			status;
	__u32			p_link_max;
	__u32			p_name_max;
	__u32			p_no_trunc;
	__u32			p_chown_restricted;
	__u32			p_case_insensitive;
	__u32			p_case_preserving;
};

struct nfsd3_commitres {
	__be32			status;
	struct svc_fh		fh;
};

struct nfsd3_getaclres {
	__be32			status;
	struct svc_fh		fh;
	int			mask;
	struct posix_acl	*acl_access;
	struct posix_acl	*acl_default;
	struct kstat		stat;
};

/* dummy type for release */
struct nfsd3_fhandle_pair {
	__u32			dummy;
	struct svc_fh		fh1;
	struct svc_fh		fh2;
};

/*
 * Storage requirements for XDR arguments and results.
 */
union nfsd3_xdrstore {
	struct nfsd3_sattrargs		sattrargs;
	struct nfsd3_diropargs		diropargs;
	struct nfsd3_readargs		readargs;
	struct nfsd3_writeargs		writeargs;
	struct nfsd3_createargs		createargs;
	struct nfsd3_renameargs		renameargs;
	struct nfsd3_linkargs		linkargs;
	struct nfsd3_symlinkargs	symlinkargs;
	struct nfsd3_readdirargs	readdirargs;
	struct nfsd3_diropres 		diropres;
	struct nfsd3_accessres		accessres;
	struct nfsd3_readlinkres	readlinkres;
	struct nfsd3_readres		readres;
	struct nfsd3_writeres		writeres;
	struct nfsd3_renameres		renameres;
	struct nfsd3_linkres		linkres;
	struct nfsd3_readdirres		readdirres;
	struct nfsd3_fsstatres		fsstatres;
	struct nfsd3_fsinfores		fsinfores;
	struct nfsd3_pathconfres	pathconfres;
	struct nfsd3_commitres		commitres;
	struct nfsd3_getaclres		getaclres;
};

#define NFS3_SVC_XDRSIZE		sizeof(union nfsd3_xdrstore)

int nfs3svc_decode_fhandle(struct svc_rqst *, __be32 *);
int nfs3svc_decode_sattrargs(struct svc_rqst *, __be32 *);
int nfs3svc_decode_diropargs(struct svc_rqst *, __be32 *);
int nfs3svc_decode_accessargs(struct svc_rqst *, __be32 *);
int nfs3svc_decode_readargs(struct svc_rqst *, __be32 *);
int nfs3svc_decode_writeargs(struct svc_rqst *, __be32 *);
int nfs3svc_decode_createargs(struct svc_rqst *, __be32 *);
int nfs3svc_decode_mkdirargs(struct svc_rqst *, __be32 *);
int nfs3svc_decode_mknodargs(struct svc_rqst *, __be32 *);
int nfs3svc_decode_renameargs(struct svc_rqst *, __be32 *);
int nfs3svc_decode_readlinkargs(struct svc_rqst *, __be32 *);
int nfs3svc_decode_linkargs(struct svc_rqst *, __be32 *);
int nfs3svc_decode_symlinkargs(struct svc_rqst *, __be32 *);
int nfs3svc_decode_readdirargs(struct svc_rqst *, __be32 *);
int nfs3svc_decode_readdirplusargs(struct svc_rqst *, __be32 *);
int nfs3svc_decode_commitargs(struct svc_rqst *, __be32 *);
int nfs3svc_encode_voidres(struct svc_rqst *, __be32 *);
int nfs3svc_encode_attrstat(struct svc_rqst *, __be32 *);
int nfs3svc_encode_wccstat(struct svc_rqst *, __be32 *);
int nfs3svc_encode_diropres(struct svc_rqst *, __be32 *);
int nfs3svc_encode_accessres(struct svc_rqst *, __be32 *);
int nfs3svc_encode_readlinkres(struct svc_rqst *, __be32 *);
int nfs3svc_encode_readres(struct svc_rqst *, __be32 *);
int nfs3svc_encode_writeres(struct svc_rqst *, __be32 *);
int nfs3svc_encode_createres(struct svc_rqst *, __be32 *);
int nfs3svc_encode_renameres(struct svc_rqst *, __be32 *);
int nfs3svc_encode_linkres(struct svc_rqst *, __be32 *);
int nfs3svc_encode_readdirres(struct svc_rqst *, __be32 *);
int nfs3svc_encode_fsstatres(struct svc_rqst *, __be32 *);
int nfs3svc_encode_fsinfores(struct svc_rqst *, __be32 *);
int nfs3svc_encode_pathconfres(struct svc_rqst *, __be32 *);
int nfs3svc_encode_commitres(struct svc_rqst *, __be32 *);

void nfs3svc_release_fhandle(struct svc_rqst *);
void nfs3svc_release_fhandle2(struct svc_rqst *);
int nfs3svc_encode_entry(void *, const char *name,
				int namlen, loff_t offset, u64 ino,
				unsigned int);
int nfs3svc_encode_entry_plus(void *, const char *name,
				int namlen, loff_t offset, u64 ino,
				unsigned int);
/* Helper functions for NFSv3 ACL code */
__be32 *nfs3svc_encode_post_op_attr(struct svc_rqst *rqstp, __be32 *p,
				struct svc_fh *fhp);
__be32 *nfs3svc_decode_fh(__be32 *p, struct svc_fh *fhp);


#endif /* _LINUX_NFSD_XDR3_H */

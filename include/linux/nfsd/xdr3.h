/*
 * linux/include/linux/nfsd/xdr3.h
 *
 * XDR types for NFSv3 in nfsd.
 *
 * Copyright (C) 1996-1998, Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_NFSD_XDR3_H
#define _LINUX_NFSD_XDR3_H

#include <linux/nfsd/xdr.h>

struct nfsd3_sattrargs {
	struct svc_fh		fh;
	struct iattr		attrs;
	int			check_guard;
	time_t			guardtime;
};

struct nfsd3_diropargs {
	struct svc_fh		fh;
	char *			name;
	int			len;
};

struct nfsd3_accessargs {
	struct svc_fh		fh;
	unsigned int		access;
};

struct nfsd3_readargs {
	struct svc_fh		fh;
	__u64			offset;
	__u32			count;
	struct kvec		vec[RPCSVC_MAXPAGES];
	int			vlen;
};

struct nfsd3_writeargs {
	svc_fh			fh;
	__u64			offset;
	__u32			count;
	int			stable;
	__u32			len;
	struct kvec		vec[RPCSVC_MAXPAGES];
	int			vlen;
};

struct nfsd3_createargs {
	struct svc_fh		fh;
	char *			name;
	int			len;
	int			createmode;
	struct iattr		attrs;
	__u32 *			verf;
};

struct nfsd3_mknodargs {
	struct svc_fh		fh;
	char *			name;
	int			len;
	__u32			ftype;
	__u32			major, minor;
	struct iattr		attrs;
};

struct nfsd3_renameargs {
	struct svc_fh		ffh;
	char *			fname;
	int			flen;
	struct svc_fh		tfh;
	char *			tname;
	int			tlen;
};

struct nfsd3_readlinkargs {
	struct svc_fh		fh;
	char *			buffer;
};

struct nfsd3_linkargs {
	struct svc_fh		ffh;
	struct svc_fh		tfh;
	char *			tname;
	int			tlen;
};

struct nfsd3_symlinkargs {
	struct svc_fh		ffh;
	char *			fname;
	int			flen;
	char *			tname;
	int			tlen;
	struct iattr		attrs;
};

struct nfsd3_readdirargs {
	struct svc_fh		fh;
	__u64			cookie;
	__u32			dircount;
	__u32			count;
	__u32 *			verf;
	u32 *			buffer;
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
	__u32			status;
	struct svc_fh		fh;
	struct kstat            stat;
};

/* LOOKUP, CREATE, MKDIR, SYMLINK, MKNOD */
struct nfsd3_diropres  {
	__u32			status;
	struct svc_fh		dirfh;
	struct svc_fh		fh;
};

struct nfsd3_accessres {
	__u32			status;
	struct svc_fh		fh;
	__u32			access;
};

struct nfsd3_readlinkres {
	__u32			status;
	struct svc_fh		fh;
	__u32			len;
};

struct nfsd3_readres {
	__u32			status;
	struct svc_fh		fh;
	unsigned long		count;
	int			eof;
};

struct nfsd3_writeres {
	__u32			status;
	struct svc_fh		fh;
	unsigned long		count;
	int			committed;
};

struct nfsd3_renameres {
	__u32			status;
	struct svc_fh		ffh;
	struct svc_fh		tfh;
};

struct nfsd3_linkres {
	__u32			status;
	struct svc_fh		tfh;
	struct svc_fh		fh;
};

struct nfsd3_readdirres {
	__u32			status;
	struct svc_fh		fh;
	int			count;
	__u32			verf[2];

	struct readdir_cd	common;
	u32 *			buffer;
	int			buflen;
	u32 *			offset;
	u32 *			offset1;
	struct svc_rqst *	rqstp;

};

struct nfsd3_fsstatres {
	__u32			status;
	struct kstatfs		stats;
	__u32			invarsec;
};

struct nfsd3_fsinfores {
	__u32			status;
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
	__u32			status;
	__u32			p_link_max;
	__u32			p_name_max;
	__u32			p_no_trunc;
	__u32			p_chown_restricted;
	__u32			p_case_insensitive;
	__u32			p_case_preserving;
};

struct nfsd3_commitres {
	__u32			status;
	struct svc_fh		fh;
};

struct nfsd3_getaclres {
	__u32			status;
	struct svc_fh		fh;
	int			mask;
	struct posix_acl	*acl_access;
	struct posix_acl	*acl_default;
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

int nfs3svc_decode_fhandle(struct svc_rqst *, u32 *, struct nfsd_fhandle *);
int nfs3svc_decode_sattrargs(struct svc_rqst *, u32 *,
				struct nfsd3_sattrargs *);
int nfs3svc_decode_diropargs(struct svc_rqst *, u32 *,
				struct nfsd3_diropargs *);
int nfs3svc_decode_accessargs(struct svc_rqst *, u32 *,
				struct nfsd3_accessargs *);
int nfs3svc_decode_readargs(struct svc_rqst *, u32 *,
				struct nfsd3_readargs *);
int nfs3svc_decode_writeargs(struct svc_rqst *, u32 *,
				struct nfsd3_writeargs *);
int nfs3svc_decode_createargs(struct svc_rqst *, u32 *,
				struct nfsd3_createargs *);
int nfs3svc_decode_mkdirargs(struct svc_rqst *, u32 *,
				struct nfsd3_createargs *);
int nfs3svc_decode_mknodargs(struct svc_rqst *, u32 *,
				struct nfsd3_mknodargs *);
int nfs3svc_decode_renameargs(struct svc_rqst *, u32 *,
				struct nfsd3_renameargs *);
int nfs3svc_decode_readlinkargs(struct svc_rqst *, u32 *,
				struct nfsd3_readlinkargs *);
int nfs3svc_decode_linkargs(struct svc_rqst *, u32 *,
				struct nfsd3_linkargs *);
int nfs3svc_decode_symlinkargs(struct svc_rqst *, u32 *,
				struct nfsd3_symlinkargs *);
int nfs3svc_decode_readdirargs(struct svc_rqst *, u32 *,
				struct nfsd3_readdirargs *);
int nfs3svc_decode_readdirplusargs(struct svc_rqst *, u32 *,
				struct nfsd3_readdirargs *);
int nfs3svc_decode_commitargs(struct svc_rqst *, u32 *,
				struct nfsd3_commitargs *);
int nfs3svc_encode_voidres(struct svc_rqst *, u32 *, void *);
int nfs3svc_encode_attrstat(struct svc_rqst *, u32 *,
				struct nfsd3_attrstat *);
int nfs3svc_encode_wccstat(struct svc_rqst *, u32 *,
				struct nfsd3_attrstat *);
int nfs3svc_encode_diropres(struct svc_rqst *, u32 *,
				struct nfsd3_diropres *);
int nfs3svc_encode_accessres(struct svc_rqst *, u32 *,
				struct nfsd3_accessres *);
int nfs3svc_encode_readlinkres(struct svc_rqst *, u32 *,
				struct nfsd3_readlinkres *);
int nfs3svc_encode_readres(struct svc_rqst *, u32 *, struct nfsd3_readres *);
int nfs3svc_encode_writeres(struct svc_rqst *, u32 *, struct nfsd3_writeres *);
int nfs3svc_encode_createres(struct svc_rqst *, u32 *,
				struct nfsd3_diropres *);
int nfs3svc_encode_renameres(struct svc_rqst *, u32 *,
				struct nfsd3_renameres *);
int nfs3svc_encode_linkres(struct svc_rqst *, u32 *,
				struct nfsd3_linkres *);
int nfs3svc_encode_readdirres(struct svc_rqst *, u32 *,
				struct nfsd3_readdirres *);
int nfs3svc_encode_fsstatres(struct svc_rqst *, u32 *,
				struct nfsd3_fsstatres *);
int nfs3svc_encode_fsinfores(struct svc_rqst *, u32 *,
				struct nfsd3_fsinfores *);
int nfs3svc_encode_pathconfres(struct svc_rqst *, u32 *,
				struct nfsd3_pathconfres *);
int nfs3svc_encode_commitres(struct svc_rqst *, u32 *,
				struct nfsd3_commitres *);

int nfs3svc_release_fhandle(struct svc_rqst *, u32 *,
				struct nfsd3_attrstat *);
int nfs3svc_release_fhandle2(struct svc_rqst *, u32 *,
				struct nfsd3_fhandle_pair *);
int nfs3svc_encode_entry(struct readdir_cd *, const char *name,
				int namlen, loff_t offset, ino_t ino,
				unsigned int);
int nfs3svc_encode_entry_plus(struct readdir_cd *, const char *name,
				int namlen, loff_t offset, ino_t ino,
				unsigned int);
/* Helper functions for NFSv3 ACL code */
u32 *nfs3svc_encode_post_op_attr(struct svc_rqst *rqstp, u32 *p,
				struct svc_fh *fhp);
u32 *nfs3svc_decode_fh(u32 *p, struct svc_fh *fhp);


#endif /* _LINUX_NFSD_XDR3_H */

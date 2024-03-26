// SPDX-License-Identifier: GPL-2.0
/*
 * XDR support for nfsd/protocol version 3.
 *
 * Copyright (C) 1995, 1996, 1997 Olaf Kirch <okir@monad.swb.de>
 *
 * 2003-08-09 Jamie Lokier: Use htonl() for nanoseconds, not htons()!
 */

#include <linux/namei.h>
#include <linux/sunrpc/svc_xprt.h>
#include "xdr3.h"
#include "auth.h"
#include "netns.h"
#include "vfs.h"

/*
 * Force construction of an empty post-op attr
 */
static const struct svc_fh nfs3svc_null_fh = {
	.fh_no_wcc	= true,
};

/*
 * time_delta. {1, 0} means the server is accurate only
 * to the nearest second.
 */
static const struct timespec64 nfs3svc_time_delta = {
	.tv_sec		= 1,
	.tv_nsec	= 0,
};

/*
 * Mapping of S_IF* types to NFS file types
 */
static const u32 nfs3_ftypes[] = {
	NF3NON,  NF3FIFO, NF3CHR, NF3BAD,
	NF3DIR,  NF3BAD,  NF3BLK, NF3BAD,
	NF3REG,  NF3BAD,  NF3LNK, NF3BAD,
	NF3SOCK, NF3BAD,  NF3LNK, NF3BAD,
};


/*
 * Basic NFSv3 data types (RFC 1813 Sections 2.5 and 2.6)
 */

static __be32 *
encode_nfstime3(__be32 *p, const struct timespec64 *time)
{
	*p++ = cpu_to_be32((u32)time->tv_sec);
	*p++ = cpu_to_be32(time->tv_nsec);

	return p;
}

static bool
svcxdr_decode_nfstime3(struct xdr_stream *xdr, struct timespec64 *timep)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, XDR_UNIT * 2);
	if (!p)
		return false;
	timep->tv_sec = be32_to_cpup(p++);
	timep->tv_nsec = be32_to_cpup(p);

	return true;
}

/**
 * svcxdr_decode_nfs_fh3 - Decode an NFSv3 file handle
 * @xdr: XDR stream positioned at an undecoded NFSv3 FH
 * @fhp: OUT: filled-in server file handle
 *
 * Return values:
 *  %false: The encoded file handle was not valid
 *  %true: @fhp has been initialized
 */
bool
svcxdr_decode_nfs_fh3(struct xdr_stream *xdr, struct svc_fh *fhp)
{
	__be32 *p;
	u32 size;

	if (xdr_stream_decode_u32(xdr, &size) < 0)
		return false;
	if (size == 0 || size > NFS3_FHSIZE)
		return false;
	p = xdr_inline_decode(xdr, size);
	if (!p)
		return false;
	fh_init(fhp, NFS3_FHSIZE);
	fhp->fh_handle.fh_size = size;
	memcpy(&fhp->fh_handle.fh_raw, p, size);

	return true;
}

/**
 * svcxdr_encode_nfsstat3 - Encode an NFSv3 status code
 * @xdr: XDR stream
 * @status: status value to encode
 *
 * Return values:
 *   %false: Send buffer space was exhausted
 *   %true: Success
 */
bool
svcxdr_encode_nfsstat3(struct xdr_stream *xdr, __be32 status)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, sizeof(status));
	if (!p)
		return false;
	*p = status;

	return true;
}

static bool
svcxdr_encode_nfs_fh3(struct xdr_stream *xdr, const struct svc_fh *fhp)
{
	u32 size = fhp->fh_handle.fh_size;
	__be32 *p;

	p = xdr_reserve_space(xdr, XDR_UNIT + size);
	if (!p)
		return false;
	*p++ = cpu_to_be32(size);
	if (size)
		p[XDR_QUADLEN(size) - 1] = 0;
	memcpy(p, &fhp->fh_handle.fh_raw, size);

	return true;
}

static bool
svcxdr_encode_post_op_fh3(struct xdr_stream *xdr, const struct svc_fh *fhp)
{
	if (xdr_stream_encode_item_present(xdr) < 0)
		return false;
	if (!svcxdr_encode_nfs_fh3(xdr, fhp))
		return false;

	return true;
}

static bool
svcxdr_encode_cookieverf3(struct xdr_stream *xdr, const __be32 *verf)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, NFS3_COOKIEVERFSIZE);
	if (!p)
		return false;
	memcpy(p, verf, NFS3_COOKIEVERFSIZE);

	return true;
}

static bool
svcxdr_encode_writeverf3(struct xdr_stream *xdr, const __be32 *verf)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, NFS3_WRITEVERFSIZE);
	if (!p)
		return false;
	memcpy(p, verf, NFS3_WRITEVERFSIZE);

	return true;
}

static bool
svcxdr_decode_filename3(struct xdr_stream *xdr, char **name, unsigned int *len)
{
	u32 size, i;
	__be32 *p;
	char *c;

	if (xdr_stream_decode_u32(xdr, &size) < 0)
		return false;
	if (size == 0 || size > NFS3_MAXNAMLEN)
		return false;
	p = xdr_inline_decode(xdr, size);
	if (!p)
		return false;

	*len = size;
	*name = (char *)p;
	for (i = 0, c = *name; i < size; i++, c++) {
		if (*c == '\0' || *c == '/')
			return false;
	}

	return true;
}

static bool
svcxdr_decode_diropargs3(struct xdr_stream *xdr, struct svc_fh *fhp,
			 char **name, unsigned int *len)
{
	return svcxdr_decode_nfs_fh3(xdr, fhp) &&
		svcxdr_decode_filename3(xdr, name, len);
}

static bool
svcxdr_decode_sattr3(struct svc_rqst *rqstp, struct xdr_stream *xdr,
		     struct iattr *iap)
{
	u32 set_it;

	iap->ia_valid = 0;

	if (xdr_stream_decode_bool(xdr, &set_it) < 0)
		return false;
	if (set_it) {
		u32 mode;

		if (xdr_stream_decode_u32(xdr, &mode) < 0)
			return false;
		iap->ia_valid |= ATTR_MODE;
		iap->ia_mode = mode;
	}
	if (xdr_stream_decode_bool(xdr, &set_it) < 0)
		return false;
	if (set_it) {
		u32 uid;

		if (xdr_stream_decode_u32(xdr, &uid) < 0)
			return false;
		iap->ia_uid = make_kuid(nfsd_user_namespace(rqstp), uid);
		if (uid_valid(iap->ia_uid))
			iap->ia_valid |= ATTR_UID;
	}
	if (xdr_stream_decode_bool(xdr, &set_it) < 0)
		return false;
	if (set_it) {
		u32 gid;

		if (xdr_stream_decode_u32(xdr, &gid) < 0)
			return false;
		iap->ia_gid = make_kgid(nfsd_user_namespace(rqstp), gid);
		if (gid_valid(iap->ia_gid))
			iap->ia_valid |= ATTR_GID;
	}
	if (xdr_stream_decode_bool(xdr, &set_it) < 0)
		return false;
	if (set_it) {
		u64 newsize;

		if (xdr_stream_decode_u64(xdr, &newsize) < 0)
			return false;
		iap->ia_valid |= ATTR_SIZE;
		iap->ia_size = newsize;
	}
	if (xdr_stream_decode_u32(xdr, &set_it) < 0)
		return false;
	switch (set_it) {
	case DONT_CHANGE:
		break;
	case SET_TO_SERVER_TIME:
		iap->ia_valid |= ATTR_ATIME;
		break;
	case SET_TO_CLIENT_TIME:
		if (!svcxdr_decode_nfstime3(xdr, &iap->ia_atime))
			return false;
		iap->ia_valid |= ATTR_ATIME | ATTR_ATIME_SET;
		break;
	default:
		return false;
	}
	if (xdr_stream_decode_u32(xdr, &set_it) < 0)
		return false;
	switch (set_it) {
	case DONT_CHANGE:
		break;
	case SET_TO_SERVER_TIME:
		iap->ia_valid |= ATTR_MTIME;
		break;
	case SET_TO_CLIENT_TIME:
		if (!svcxdr_decode_nfstime3(xdr, &iap->ia_mtime))
			return false;
		iap->ia_valid |= ATTR_MTIME | ATTR_MTIME_SET;
		break;
	default:
		return false;
	}

	return true;
}

static bool
svcxdr_decode_sattrguard3(struct xdr_stream *xdr, struct nfsd3_sattrargs *args)
{
	u32 check;

	if (xdr_stream_decode_bool(xdr, &check) < 0)
		return false;
	if (check) {
		if (!svcxdr_decode_nfstime3(xdr, &args->guardtime))
			return false;
		args->check_guard = 1;
	} else
		args->check_guard = 0;

	return true;
}

static bool
svcxdr_decode_specdata3(struct xdr_stream *xdr, struct nfsd3_mknodargs *args)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, XDR_UNIT * 2);
	if (!p)
		return false;
	args->major = be32_to_cpup(p++);
	args->minor = be32_to_cpup(p);

	return true;
}

static bool
svcxdr_decode_devicedata3(struct svc_rqst *rqstp, struct xdr_stream *xdr,
			  struct nfsd3_mknodargs *args)
{
	return svcxdr_decode_sattr3(rqstp, xdr, &args->attrs) &&
		svcxdr_decode_specdata3(xdr, args);
}

static bool
svcxdr_encode_fattr3(struct svc_rqst *rqstp, struct xdr_stream *xdr,
		     const struct svc_fh *fhp, const struct kstat *stat)
{
	struct user_namespace *userns = nfsd_user_namespace(rqstp);
	__be32 *p;
	u64 fsid;

	p = xdr_reserve_space(xdr, XDR_UNIT * 21);
	if (!p)
		return false;

	*p++ = cpu_to_be32(nfs3_ftypes[(stat->mode & S_IFMT) >> 12]);
	*p++ = cpu_to_be32((u32)(stat->mode & S_IALLUGO));
	*p++ = cpu_to_be32((u32)stat->nlink);
	*p++ = cpu_to_be32((u32)from_kuid_munged(userns, stat->uid));
	*p++ = cpu_to_be32((u32)from_kgid_munged(userns, stat->gid));
	if (S_ISLNK(stat->mode) && stat->size > NFS3_MAXPATHLEN)
		p = xdr_encode_hyper(p, (u64)NFS3_MAXPATHLEN);
	else
		p = xdr_encode_hyper(p, (u64)stat->size);

	/* used */
	p = xdr_encode_hyper(p, ((u64)stat->blocks) << 9);

	/* rdev */
	*p++ = cpu_to_be32((u32)MAJOR(stat->rdev));
	*p++ = cpu_to_be32((u32)MINOR(stat->rdev));

	switch(fsid_source(fhp)) {
	case FSIDSOURCE_FSID:
		fsid = (u64)fhp->fh_export->ex_fsid;
		break;
	case FSIDSOURCE_UUID:
		fsid = ((u64 *)fhp->fh_export->ex_uuid)[0];
		fsid ^= ((u64 *)fhp->fh_export->ex_uuid)[1];
		break;
	default:
		fsid = (u64)huge_encode_dev(fhp->fh_dentry->d_sb->s_dev);
	}
	p = xdr_encode_hyper(p, fsid);

	/* fileid */
	p = xdr_encode_hyper(p, stat->ino);

	p = encode_nfstime3(p, &stat->atime);
	p = encode_nfstime3(p, &stat->mtime);
	encode_nfstime3(p, &stat->ctime);

	return true;
}

static bool
svcxdr_encode_wcc_attr(struct xdr_stream *xdr, const struct svc_fh *fhp)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, XDR_UNIT * 6);
	if (!p)
		return false;
	p = xdr_encode_hyper(p, (u64)fhp->fh_pre_size);
	p = encode_nfstime3(p, &fhp->fh_pre_mtime);
	encode_nfstime3(p, &fhp->fh_pre_ctime);

	return true;
}

static bool
svcxdr_encode_pre_op_attr(struct xdr_stream *xdr, const struct svc_fh *fhp)
{
	if (!fhp->fh_pre_saved) {
		if (xdr_stream_encode_item_absent(xdr) < 0)
			return false;
		return true;
	}

	if (xdr_stream_encode_item_present(xdr) < 0)
		return false;
	return svcxdr_encode_wcc_attr(xdr, fhp);
}

/**
 * svcxdr_encode_post_op_attr - Encode NFSv3 post-op attributes
 * @rqstp: Context of a completed RPC transaction
 * @xdr: XDR stream
 * @fhp: File handle to encode
 *
 * Return values:
 *   %false: Send buffer space was exhausted
 *   %true: Success
 */
bool
svcxdr_encode_post_op_attr(struct svc_rqst *rqstp, struct xdr_stream *xdr,
			   const struct svc_fh *fhp)
{
	struct dentry *dentry = fhp->fh_dentry;
	struct kstat stat;

	/*
	 * The inode may be NULL if the call failed because of a
	 * stale file handle. In this case, no attributes are
	 * returned.
	 */
	if (fhp->fh_no_wcc || !dentry || !d_really_is_positive(dentry))
		goto no_post_op_attrs;
	if (fh_getattr(fhp, &stat) != nfs_ok)
		goto no_post_op_attrs;

	if (xdr_stream_encode_item_present(xdr) < 0)
		return false;
	lease_get_mtime(d_inode(dentry), &stat.mtime);
	if (!svcxdr_encode_fattr3(rqstp, xdr, fhp, &stat))
		return false;

	return true;

no_post_op_attrs:
	return xdr_stream_encode_item_absent(xdr) > 0;
}

/*
 * Encode weak cache consistency data
 */
static bool
svcxdr_encode_wcc_data(struct svc_rqst *rqstp, struct xdr_stream *xdr,
		       const struct svc_fh *fhp)
{
	struct dentry *dentry = fhp->fh_dentry;

	if (!dentry || !d_really_is_positive(dentry) || !fhp->fh_post_saved)
		goto neither;

	/* before */
	if (!svcxdr_encode_pre_op_attr(xdr, fhp))
		return false;

	/* after */
	if (xdr_stream_encode_item_present(xdr) < 0)
		return false;
	if (!svcxdr_encode_fattr3(rqstp, xdr, fhp, &fhp->fh_post_attr))
		return false;

	return true;

neither:
	if (xdr_stream_encode_item_absent(xdr) < 0)
		return false;
	if (!svcxdr_encode_post_op_attr(rqstp, xdr, fhp))
		return false;

	return true;
}

/*
 * XDR decode functions
 */

bool
nfs3svc_decode_fhandleargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd_fhandle *args = rqstp->rq_argp;

	return svcxdr_decode_nfs_fh3(xdr, &args->fh);
}

bool
nfs3svc_decode_sattrargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_sattrargs *args = rqstp->rq_argp;

	return svcxdr_decode_nfs_fh3(xdr, &args->fh) &&
		svcxdr_decode_sattr3(rqstp, xdr, &args->attrs) &&
		svcxdr_decode_sattrguard3(xdr, args);
}

bool
nfs3svc_decode_diropargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_diropargs *args = rqstp->rq_argp;

	return svcxdr_decode_diropargs3(xdr, &args->fh, &args->name, &args->len);
}

bool
nfs3svc_decode_accessargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_accessargs *args = rqstp->rq_argp;

	if (!svcxdr_decode_nfs_fh3(xdr, &args->fh))
		return false;
	if (xdr_stream_decode_u32(xdr, &args->access) < 0)
		return false;

	return true;
}

bool
nfs3svc_decode_readargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_readargs *args = rqstp->rq_argp;

	if (!svcxdr_decode_nfs_fh3(xdr, &args->fh))
		return false;
	if (xdr_stream_decode_u64(xdr, &args->offset) < 0)
		return false;
	if (xdr_stream_decode_u32(xdr, &args->count) < 0)
		return false;

	return true;
}

bool
nfs3svc_decode_writeargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_writeargs *args = rqstp->rq_argp;
	u32 max_blocksize = svc_max_payload(rqstp);

	if (!svcxdr_decode_nfs_fh3(xdr, &args->fh))
		return false;
	if (xdr_stream_decode_u64(xdr, &args->offset) < 0)
		return false;
	if (xdr_stream_decode_u32(xdr, &args->count) < 0)
		return false;
	if (xdr_stream_decode_u32(xdr, &args->stable) < 0)
		return false;

	/* opaque data */
	if (xdr_stream_decode_u32(xdr, &args->len) < 0)
		return false;

	/* request sanity */
	if (args->count != args->len)
		return false;
	if (args->count > max_blocksize) {
		args->count = max_blocksize;
		args->len = max_blocksize;
	}

	return xdr_stream_subsegment(xdr, &args->payload, args->count);
}

bool
nfs3svc_decode_createargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_createargs *args = rqstp->rq_argp;

	if (!svcxdr_decode_diropargs3(xdr, &args->fh, &args->name, &args->len))
		return false;
	if (xdr_stream_decode_u32(xdr, &args->createmode) < 0)
		return false;
	switch (args->createmode) {
	case NFS3_CREATE_UNCHECKED:
	case NFS3_CREATE_GUARDED:
		return svcxdr_decode_sattr3(rqstp, xdr, &args->attrs);
	case NFS3_CREATE_EXCLUSIVE:
		args->verf = xdr_inline_decode(xdr, NFS3_CREATEVERFSIZE);
		if (!args->verf)
			return false;
		break;
	default:
		return false;
	}
	return true;
}

bool
nfs3svc_decode_mkdirargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_createargs *args = rqstp->rq_argp;

	return svcxdr_decode_diropargs3(xdr, &args->fh,
					&args->name, &args->len) &&
		svcxdr_decode_sattr3(rqstp, xdr, &args->attrs);
}

bool
nfs3svc_decode_symlinkargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_symlinkargs *args = rqstp->rq_argp;
	struct kvec *head = rqstp->rq_arg.head;

	if (!svcxdr_decode_diropargs3(xdr, &args->ffh, &args->fname, &args->flen))
		return false;
	if (!svcxdr_decode_sattr3(rqstp, xdr, &args->attrs))
		return false;
	if (xdr_stream_decode_u32(xdr, &args->tlen) < 0)
		return false;

	/* symlink_data */
	args->first.iov_len = head->iov_len - xdr_stream_pos(xdr);
	args->first.iov_base = xdr_inline_decode(xdr, args->tlen);
	return args->first.iov_base != NULL;
}

bool
nfs3svc_decode_mknodargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_mknodargs *args = rqstp->rq_argp;

	if (!svcxdr_decode_diropargs3(xdr, &args->fh, &args->name, &args->len))
		return false;
	if (xdr_stream_decode_u32(xdr, &args->ftype) < 0)
		return false;
	switch (args->ftype) {
	case NF3CHR:
	case NF3BLK:
		return svcxdr_decode_devicedata3(rqstp, xdr, args);
	case NF3SOCK:
	case NF3FIFO:
		return svcxdr_decode_sattr3(rqstp, xdr, &args->attrs);
	case NF3REG:
	case NF3DIR:
	case NF3LNK:
		/* Valid XDR but illegal file types */
		break;
	default:
		return false;
	}

	return true;
}

bool
nfs3svc_decode_renameargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_renameargs *args = rqstp->rq_argp;

	return svcxdr_decode_diropargs3(xdr, &args->ffh,
					&args->fname, &args->flen) &&
		svcxdr_decode_diropargs3(xdr, &args->tfh,
					 &args->tname, &args->tlen);
}

bool
nfs3svc_decode_linkargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_linkargs *args = rqstp->rq_argp;

	return svcxdr_decode_nfs_fh3(xdr, &args->ffh) &&
		svcxdr_decode_diropargs3(xdr, &args->tfh,
					 &args->tname, &args->tlen);
}

bool
nfs3svc_decode_readdirargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_readdirargs *args = rqstp->rq_argp;

	if (!svcxdr_decode_nfs_fh3(xdr, &args->fh))
		return false;
	if (xdr_stream_decode_u64(xdr, &args->cookie) < 0)
		return false;
	args->verf = xdr_inline_decode(xdr, NFS3_COOKIEVERFSIZE);
	if (!args->verf)
		return false;
	if (xdr_stream_decode_u32(xdr, &args->count) < 0)
		return false;

	return true;
}

bool
nfs3svc_decode_readdirplusargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_readdirargs *args = rqstp->rq_argp;
	u32 dircount;

	if (!svcxdr_decode_nfs_fh3(xdr, &args->fh))
		return false;
	if (xdr_stream_decode_u64(xdr, &args->cookie) < 0)
		return false;
	args->verf = xdr_inline_decode(xdr, NFS3_COOKIEVERFSIZE);
	if (!args->verf)
		return false;
	/* dircount is ignored */
	if (xdr_stream_decode_u32(xdr, &dircount) < 0)
		return false;
	if (xdr_stream_decode_u32(xdr, &args->count) < 0)
		return false;

	return true;
}

bool
nfs3svc_decode_commitargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_commitargs *args = rqstp->rq_argp;

	if (!svcxdr_decode_nfs_fh3(xdr, &args->fh))
		return false;
	if (xdr_stream_decode_u64(xdr, &args->offset) < 0)
		return false;
	if (xdr_stream_decode_u32(xdr, &args->count) < 0)
		return false;

	return true;
}

/*
 * XDR encode functions
 */

/* GETATTR */
bool
nfs3svc_encode_getattrres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_attrstat *resp = rqstp->rq_resp;

	if (!svcxdr_encode_nfsstat3(xdr, resp->status))
		return false;
	switch (resp->status) {
	case nfs_ok:
		lease_get_mtime(d_inode(resp->fh.fh_dentry), &resp->stat.mtime);
		if (!svcxdr_encode_fattr3(rqstp, xdr, &resp->fh, &resp->stat))
			return false;
		break;
	}

	return true;
}

/* SETATTR, REMOVE, RMDIR */
bool
nfs3svc_encode_wccstat(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_attrstat *resp = rqstp->rq_resp;

	return svcxdr_encode_nfsstat3(xdr, resp->status) &&
		svcxdr_encode_wcc_data(rqstp, xdr, &resp->fh);
}

/* LOOKUP */
bool
nfs3svc_encode_lookupres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_diropres *resp = rqstp->rq_resp;

	if (!svcxdr_encode_nfsstat3(xdr, resp->status))
		return false;
	switch (resp->status) {
	case nfs_ok:
		if (!svcxdr_encode_nfs_fh3(xdr, &resp->fh))
			return false;
		if (!svcxdr_encode_post_op_attr(rqstp, xdr, &resp->fh))
			return false;
		if (!svcxdr_encode_post_op_attr(rqstp, xdr, &resp->dirfh))
			return false;
		break;
	default:
		if (!svcxdr_encode_post_op_attr(rqstp, xdr, &resp->dirfh))
			return false;
	}

	return true;
}

/* ACCESS */
bool
nfs3svc_encode_accessres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_accessres *resp = rqstp->rq_resp;

	if (!svcxdr_encode_nfsstat3(xdr, resp->status))
		return false;
	switch (resp->status) {
	case nfs_ok:
		if (!svcxdr_encode_post_op_attr(rqstp, xdr, &resp->fh))
			return false;
		if (xdr_stream_encode_u32(xdr, resp->access) < 0)
			return false;
		break;
	default:
		if (!svcxdr_encode_post_op_attr(rqstp, xdr, &resp->fh))
			return false;
	}

	return true;
}

/* READLINK */
bool
nfs3svc_encode_readlinkres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_readlinkres *resp = rqstp->rq_resp;
	struct kvec *head = rqstp->rq_res.head;

	if (!svcxdr_encode_nfsstat3(xdr, resp->status))
		return false;
	switch (resp->status) {
	case nfs_ok:
		if (!svcxdr_encode_post_op_attr(rqstp, xdr, &resp->fh))
			return false;
		if (xdr_stream_encode_u32(xdr, resp->len) < 0)
			return false;
		svcxdr_encode_opaque_pages(rqstp, xdr, resp->pages, 0,
					   resp->len);
		if (svc_encode_result_payload(rqstp, head->iov_len, resp->len) < 0)
			return false;
		break;
	default:
		if (!svcxdr_encode_post_op_attr(rqstp, xdr, &resp->fh))
			return false;
	}

	return true;
}

/* READ */
bool
nfs3svc_encode_readres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_readres *resp = rqstp->rq_resp;
	struct kvec *head = rqstp->rq_res.head;

	if (!svcxdr_encode_nfsstat3(xdr, resp->status))
		return false;
	switch (resp->status) {
	case nfs_ok:
		if (!svcxdr_encode_post_op_attr(rqstp, xdr, &resp->fh))
			return false;
		if (xdr_stream_encode_u32(xdr, resp->count) < 0)
			return false;
		if (xdr_stream_encode_bool(xdr, resp->eof) < 0)
			return false;
		if (xdr_stream_encode_u32(xdr, resp->count) < 0)
			return false;
		svcxdr_encode_opaque_pages(rqstp, xdr, resp->pages,
					   rqstp->rq_res.page_base,
					   resp->count);
		if (svc_encode_result_payload(rqstp, head->iov_len, resp->count) < 0)
			return false;
		break;
	default:
		if (!svcxdr_encode_post_op_attr(rqstp, xdr, &resp->fh))
			return false;
	}

	return true;
}

/* WRITE */
bool
nfs3svc_encode_writeres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_writeres *resp = rqstp->rq_resp;

	if (!svcxdr_encode_nfsstat3(xdr, resp->status))
		return false;
	switch (resp->status) {
	case nfs_ok:
		if (!svcxdr_encode_wcc_data(rqstp, xdr, &resp->fh))
			return false;
		if (xdr_stream_encode_u32(xdr, resp->count) < 0)
			return false;
		if (xdr_stream_encode_u32(xdr, resp->committed) < 0)
			return false;
		if (!svcxdr_encode_writeverf3(xdr, resp->verf))
			return false;
		break;
	default:
		if (!svcxdr_encode_wcc_data(rqstp, xdr, &resp->fh))
			return false;
	}

	return true;
}

/* CREATE, MKDIR, SYMLINK, MKNOD */
bool
nfs3svc_encode_createres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_diropres *resp = rqstp->rq_resp;

	if (!svcxdr_encode_nfsstat3(xdr, resp->status))
		return false;
	switch (resp->status) {
	case nfs_ok:
		if (!svcxdr_encode_post_op_fh3(xdr, &resp->fh))
			return false;
		if (!svcxdr_encode_post_op_attr(rqstp, xdr, &resp->fh))
			return false;
		if (!svcxdr_encode_wcc_data(rqstp, xdr, &resp->dirfh))
			return false;
		break;
	default:
		if (!svcxdr_encode_wcc_data(rqstp, xdr, &resp->dirfh))
			return false;
	}

	return true;
}

/* RENAME */
bool
nfs3svc_encode_renameres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_renameres *resp = rqstp->rq_resp;

	return svcxdr_encode_nfsstat3(xdr, resp->status) &&
		svcxdr_encode_wcc_data(rqstp, xdr, &resp->ffh) &&
		svcxdr_encode_wcc_data(rqstp, xdr, &resp->tfh);
}

/* LINK */
bool
nfs3svc_encode_linkres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_linkres *resp = rqstp->rq_resp;

	return svcxdr_encode_nfsstat3(xdr, resp->status) &&
		svcxdr_encode_post_op_attr(rqstp, xdr, &resp->fh) &&
		svcxdr_encode_wcc_data(rqstp, xdr, &resp->tfh);
}

/* READDIR */
bool
nfs3svc_encode_readdirres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_readdirres *resp = rqstp->rq_resp;
	struct xdr_buf *dirlist = &resp->dirlist;

	if (!svcxdr_encode_nfsstat3(xdr, resp->status))
		return false;
	switch (resp->status) {
	case nfs_ok:
		if (!svcxdr_encode_post_op_attr(rqstp, xdr, &resp->fh))
			return false;
		if (!svcxdr_encode_cookieverf3(xdr, resp->verf))
			return false;
		svcxdr_encode_opaque_pages(rqstp, xdr, dirlist->pages, 0,
					   dirlist->len);
		/* no more entries */
		if (xdr_stream_encode_item_absent(xdr) < 0)
			return false;
		if (xdr_stream_encode_bool(xdr, resp->common.err == nfserr_eof) < 0)
			return false;
		break;
	default:
		if (!svcxdr_encode_post_op_attr(rqstp, xdr, &resp->fh))
			return false;
	}

	return true;
}

static __be32
compose_entry_fh(struct nfsd3_readdirres *cd, struct svc_fh *fhp,
		 const char *name, int namlen, u64 ino)
{
	struct svc_export	*exp;
	struct dentry		*dparent, *dchild;
	__be32 rv = nfserr_noent;

	dparent = cd->fh.fh_dentry;
	exp  = cd->fh.fh_export;

	if (isdotent(name, namlen)) {
		if (namlen == 2) {
			dchild = dget_parent(dparent);
			/*
			 * Don't return filehandle for ".." if we're at
			 * the filesystem or export root:
			 */
			if (dchild == dparent)
				goto out;
			if (dparent == exp->ex_path.dentry)
				goto out;
		} else
			dchild = dget(dparent);
	} else
		dchild = lookup_positive_unlocked(name, dparent, namlen);
	if (IS_ERR(dchild))
		return rv;
	if (d_mountpoint(dchild))
		goto out;
	if (dchild->d_inode->i_ino != ino)
		goto out;
	rv = fh_compose(fhp, exp, dchild, &cd->fh);
out:
	dput(dchild);
	return rv;
}

/**
 * nfs3svc_encode_cookie3 - Encode a directory offset cookie
 * @resp: readdir result context
 * @offset: offset cookie to encode
 *
 * The buffer space for the offset cookie has already been reserved
 * by svcxdr_encode_entry3_common().
 */
void nfs3svc_encode_cookie3(struct nfsd3_readdirres *resp, u64 offset)
{
	__be64 cookie = cpu_to_be64(offset);

	if (!resp->cookie_offset)
		return;
	write_bytes_to_xdr_buf(&resp->dirlist, resp->cookie_offset, &cookie,
			       sizeof(cookie));
	resp->cookie_offset = 0;
}

static bool
svcxdr_encode_entry3_common(struct nfsd3_readdirres *resp, const char *name,
			    int namlen, loff_t offset, u64 ino)
{
	struct xdr_buf *dirlist = &resp->dirlist;
	struct xdr_stream *xdr = &resp->xdr;

	if (xdr_stream_encode_item_present(xdr) < 0)
		return false;
	/* fileid */
	if (xdr_stream_encode_u64(xdr, ino) < 0)
		return false;
	/* name */
	if (xdr_stream_encode_opaque(xdr, name, min(namlen, NFS3_MAXNAMLEN)) < 0)
		return false;
	/* cookie */
	resp->cookie_offset = dirlist->len;
	if (xdr_stream_encode_u64(xdr, OFFSET_MAX) < 0)
		return false;

	return true;
}

/**
 * nfs3svc_encode_entry3 - encode one NFSv3 READDIR entry
 * @data: directory context
 * @name: name of the object to be encoded
 * @namlen: length of that name, in bytes
 * @offset: the offset of the previous entry
 * @ino: the fileid of this entry
 * @d_type: unused
 *
 * Return values:
 *   %0: Entry was successfully encoded.
 *   %-EINVAL: An encoding problem occured, secondary status code in resp->common.err
 *
 * On exit, the following fields are updated:
 *   - resp->xdr
 *   - resp->common.err
 *   - resp->cookie_offset
 */
int nfs3svc_encode_entry3(void *data, const char *name, int namlen,
			  loff_t offset, u64 ino, unsigned int d_type)
{
	struct readdir_cd *ccd = data;
	struct nfsd3_readdirres *resp = container_of(ccd,
						     struct nfsd3_readdirres,
						     common);
	unsigned int starting_length = resp->dirlist.len;

	/* The offset cookie for the previous entry */
	nfs3svc_encode_cookie3(resp, offset);

	if (!svcxdr_encode_entry3_common(resp, name, namlen, offset, ino))
		goto out_toosmall;

	xdr_commit_encode(&resp->xdr);
	resp->common.err = nfs_ok;
	return 0;

out_toosmall:
	resp->cookie_offset = 0;
	resp->common.err = nfserr_toosmall;
	resp->dirlist.len = starting_length;
	return -EINVAL;
}

static bool
svcxdr_encode_entry3_plus(struct nfsd3_readdirres *resp, const char *name,
			  int namlen, u64 ino)
{
	struct xdr_stream *xdr = &resp->xdr;
	struct svc_fh *fhp = &resp->scratch;
	bool result;

	result = false;
	fh_init(fhp, NFS3_FHSIZE);
	if (compose_entry_fh(resp, fhp, name, namlen, ino) != nfs_ok)
		goto out_noattrs;

	if (!svcxdr_encode_post_op_attr(resp->rqstp, xdr, fhp))
		goto out;
	if (!svcxdr_encode_post_op_fh3(xdr, fhp))
		goto out;
	result = true;

out:
	fh_put(fhp);
	return result;

out_noattrs:
	if (xdr_stream_encode_item_absent(xdr) < 0)
		return false;
	if (xdr_stream_encode_item_absent(xdr) < 0)
		return false;
	return true;
}

/**
 * nfs3svc_encode_entryplus3 - encode one NFSv3 READDIRPLUS entry
 * @data: directory context
 * @name: name of the object to be encoded
 * @namlen: length of that name, in bytes
 * @offset: the offset of the previous entry
 * @ino: the fileid of this entry
 * @d_type: unused
 *
 * Return values:
 *   %0: Entry was successfully encoded.
 *   %-EINVAL: An encoding problem occured, secondary status code in resp->common.err
 *
 * On exit, the following fields are updated:
 *   - resp->xdr
 *   - resp->common.err
 *   - resp->cookie_offset
 */
int nfs3svc_encode_entryplus3(void *data, const char *name, int namlen,
			      loff_t offset, u64 ino, unsigned int d_type)
{
	struct readdir_cd *ccd = data;
	struct nfsd3_readdirres *resp = container_of(ccd,
						     struct nfsd3_readdirres,
						     common);
	unsigned int starting_length = resp->dirlist.len;

	/* The offset cookie for the previous entry */
	nfs3svc_encode_cookie3(resp, offset);

	if (!svcxdr_encode_entry3_common(resp, name, namlen, offset, ino))
		goto out_toosmall;
	if (!svcxdr_encode_entry3_plus(resp, name, namlen, ino))
		goto out_toosmall;

	xdr_commit_encode(&resp->xdr);
	resp->common.err = nfs_ok;
	return 0;

out_toosmall:
	resp->cookie_offset = 0;
	resp->common.err = nfserr_toosmall;
	resp->dirlist.len = starting_length;
	return -EINVAL;
}

static bool
svcxdr_encode_fsstat3resok(struct xdr_stream *xdr,
			   const struct nfsd3_fsstatres *resp)
{
	const struct kstatfs *s = &resp->stats;
	u64 bs = s->f_bsize;
	__be32 *p;

	p = xdr_reserve_space(xdr, XDR_UNIT * 13);
	if (!p)
		return false;
	p = xdr_encode_hyper(p, bs * s->f_blocks);	/* total bytes */
	p = xdr_encode_hyper(p, bs * s->f_bfree);	/* free bytes */
	p = xdr_encode_hyper(p, bs * s->f_bavail);	/* user available bytes */
	p = xdr_encode_hyper(p, s->f_files);		/* total inodes */
	p = xdr_encode_hyper(p, s->f_ffree);		/* free inodes */
	p = xdr_encode_hyper(p, s->f_ffree);		/* user available inodes */
	*p = cpu_to_be32(resp->invarsec);		/* mean unchanged time */

	return true;
}

/* FSSTAT */
bool
nfs3svc_encode_fsstatres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_fsstatres *resp = rqstp->rq_resp;

	if (!svcxdr_encode_nfsstat3(xdr, resp->status))
		return false;
	switch (resp->status) {
	case nfs_ok:
		if (!svcxdr_encode_post_op_attr(rqstp, xdr, &nfs3svc_null_fh))
			return false;
		if (!svcxdr_encode_fsstat3resok(xdr, resp))
			return false;
		break;
	default:
		if (!svcxdr_encode_post_op_attr(rqstp, xdr, &nfs3svc_null_fh))
			return false;
	}

	return true;
}

static bool
svcxdr_encode_fsinfo3resok(struct xdr_stream *xdr,
			   const struct nfsd3_fsinfores *resp)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, XDR_UNIT * 12);
	if (!p)
		return false;
	*p++ = cpu_to_be32(resp->f_rtmax);
	*p++ = cpu_to_be32(resp->f_rtpref);
	*p++ = cpu_to_be32(resp->f_rtmult);
	*p++ = cpu_to_be32(resp->f_wtmax);
	*p++ = cpu_to_be32(resp->f_wtpref);
	*p++ = cpu_to_be32(resp->f_wtmult);
	*p++ = cpu_to_be32(resp->f_dtpref);
	p = xdr_encode_hyper(p, resp->f_maxfilesize);
	p = encode_nfstime3(p, &nfs3svc_time_delta);
	*p = cpu_to_be32(resp->f_properties);

	return true;
}

/* FSINFO */
bool
nfs3svc_encode_fsinfores(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_fsinfores *resp = rqstp->rq_resp;

	if (!svcxdr_encode_nfsstat3(xdr, resp->status))
		return false;
	switch (resp->status) {
	case nfs_ok:
		if (!svcxdr_encode_post_op_attr(rqstp, xdr, &nfs3svc_null_fh))
			return false;
		if (!svcxdr_encode_fsinfo3resok(xdr, resp))
			return false;
		break;
	default:
		if (!svcxdr_encode_post_op_attr(rqstp, xdr, &nfs3svc_null_fh))
			return false;
	}

	return true;
}

static bool
svcxdr_encode_pathconf3resok(struct xdr_stream *xdr,
			     const struct nfsd3_pathconfres *resp)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, XDR_UNIT * 6);
	if (!p)
		return false;
	*p++ = cpu_to_be32(resp->p_link_max);
	*p++ = cpu_to_be32(resp->p_name_max);
	p = xdr_encode_bool(p, resp->p_no_trunc);
	p = xdr_encode_bool(p, resp->p_chown_restricted);
	p = xdr_encode_bool(p, resp->p_case_insensitive);
	xdr_encode_bool(p, resp->p_case_preserving);

	return true;
}

/* PATHCONF */
bool
nfs3svc_encode_pathconfres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_pathconfres *resp = rqstp->rq_resp;

	if (!svcxdr_encode_nfsstat3(xdr, resp->status))
		return false;
	switch (resp->status) {
	case nfs_ok:
		if (!svcxdr_encode_post_op_attr(rqstp, xdr, &nfs3svc_null_fh))
			return false;
		if (!svcxdr_encode_pathconf3resok(xdr, resp))
			return false;
		break;
	default:
		if (!svcxdr_encode_post_op_attr(rqstp, xdr, &nfs3svc_null_fh))
			return false;
	}

	return true;
}

/* COMMIT */
bool
nfs3svc_encode_commitres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_commitres *resp = rqstp->rq_resp;

	if (!svcxdr_encode_nfsstat3(xdr, resp->status))
		return false;
	switch (resp->status) {
	case nfs_ok:
		if (!svcxdr_encode_wcc_data(rqstp, xdr, &resp->fh))
			return false;
		if (!svcxdr_encode_writeverf3(xdr, resp->verf))
			return false;
		break;
	default:
		if (!svcxdr_encode_wcc_data(rqstp, xdr, &resp->fh))
			return false;
	}

	return true;
}

/*
 * XDR release functions
 */
void
nfs3svc_release_fhandle(struct svc_rqst *rqstp)
{
	struct nfsd3_attrstat *resp = rqstp->rq_resp;

	fh_put(&resp->fh);
}

void
nfs3svc_release_fhandle2(struct svc_rqst *rqstp)
{
	struct nfsd3_fhandle_pair *resp = rqstp->rq_resp;

	fh_put(&resp->fh1);
	fh_put(&resp->fh2);
}

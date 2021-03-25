// SPDX-License-Identifier: GPL-2.0
/*
 * XDR support for nfsd
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include "vfs.h"
#include "xdr.h"
#include "auth.h"

#define NFSDDBG_FACILITY		NFSDDBG_XDR

/*
 * Mapping of S_IF* types to NFS file types
 */
static u32	nfs_ftypes[] = {
	NFNON,  NFCHR,  NFCHR, NFBAD,
	NFDIR,  NFBAD,  NFBLK, NFBAD,
	NFREG,  NFBAD,  NFLNK, NFBAD,
	NFSOCK, NFBAD,  NFLNK, NFBAD,
};


/*
 * Basic NFSv2 data types (RFC 1094 Section 2.3)
 */

/**
 * svcxdr_decode_fhandle - Decode an NFSv2 file handle
 * @xdr: XDR stream positioned at an encoded NFSv2 FH
 * @fhp: OUT: filled-in server file handle
 *
 * Return values:
 *  %false: The encoded file handle was not valid
 *  %true: @fhp has been initialized
 */
bool
svcxdr_decode_fhandle(struct xdr_stream *xdr, struct svc_fh *fhp)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, NFS_FHSIZE);
	if (!p)
		return false;
	fh_init(fhp, NFS_FHSIZE);
	memcpy(&fhp->fh_handle.fh_base, p, NFS_FHSIZE);
	fhp->fh_handle.fh_size = NFS_FHSIZE;

	return true;
}

static __be32 *
encode_fh(__be32 *p, struct svc_fh *fhp)
{
	memcpy(p, &fhp->fh_handle.fh_base, NFS_FHSIZE);
	return p + (NFS_FHSIZE>> 2);
}

static bool
svcxdr_decode_filename(struct xdr_stream *xdr, char **name, unsigned int *len)
{
	u32 size, i;
	__be32 *p;
	char *c;

	if (xdr_stream_decode_u32(xdr, &size) < 0)
		return false;
	if (size == 0 || size > NFS_MAXNAMLEN)
		return false;
	p = xdr_inline_decode(xdr, size);
	if (!p)
		return false;

	*len = size;
	*name = (char *)p;
	for (i = 0, c = *name; i < size; i++, c++)
		if (*c == '\0' || *c == '/')
			return false;

	return true;
}

static bool
svcxdr_decode_diropargs(struct xdr_stream *xdr, struct svc_fh *fhp,
			char **name, unsigned int *len)
{
	return svcxdr_decode_fhandle(xdr, fhp) &&
		svcxdr_decode_filename(xdr, name, len);
}

static bool
svcxdr_decode_sattr(struct svc_rqst *rqstp, struct xdr_stream *xdr,
		    struct iattr *iap)
{
	u32 tmp1, tmp2;
	__be32 *p;

	p = xdr_inline_decode(xdr, XDR_UNIT * 8);
	if (!p)
		return false;

	iap->ia_valid = 0;

	/*
	 * Some Sun clients put 0xffff in the mode field when they
	 * mean 0xffffffff.
	 */
	tmp1 = be32_to_cpup(p++);
	if (tmp1 != (u32)-1 && tmp1 != 0xffff) {
		iap->ia_valid |= ATTR_MODE;
		iap->ia_mode = tmp1;
	}

	tmp1 = be32_to_cpup(p++);
	if (tmp1 != (u32)-1) {
		iap->ia_uid = make_kuid(nfsd_user_namespace(rqstp), tmp1);
		if (uid_valid(iap->ia_uid))
			iap->ia_valid |= ATTR_UID;
	}

	tmp1 = be32_to_cpup(p++);
	if (tmp1 != (u32)-1) {
		iap->ia_gid = make_kgid(nfsd_user_namespace(rqstp), tmp1);
		if (gid_valid(iap->ia_gid))
			iap->ia_valid |= ATTR_GID;
	}

	tmp1 = be32_to_cpup(p++);
	if (tmp1 != (u32)-1) {
		iap->ia_valid |= ATTR_SIZE;
		iap->ia_size = tmp1;
	}

	tmp1 = be32_to_cpup(p++);
	tmp2 = be32_to_cpup(p++);
	if (tmp1 != (u32)-1 && tmp2 != (u32)-1) {
		iap->ia_valid |= ATTR_ATIME | ATTR_ATIME_SET;
		iap->ia_atime.tv_sec = tmp1;
		iap->ia_atime.tv_nsec = tmp2 * NSEC_PER_USEC;
	}

	tmp1 = be32_to_cpup(p++);
	tmp2 = be32_to_cpup(p++);
	if (tmp1 != (u32)-1 && tmp2 != (u32)-1) {
		iap->ia_valid |= ATTR_MTIME | ATTR_MTIME_SET;
		iap->ia_mtime.tv_sec = tmp1;
		iap->ia_mtime.tv_nsec = tmp2 * NSEC_PER_USEC;
		/*
		 * Passing the invalid value useconds=1000000 for mtime
		 * is a Sun convention for "set both mtime and atime to
		 * current server time".  It's needed to make permissions
		 * checks for the "touch" program across v2 mounts to
		 * Solaris and Irix boxes work correctly. See description of
		 * sattr in section 6.1 of "NFS Illustrated" by
		 * Brent Callaghan, Addison-Wesley, ISBN 0-201-32750-5
		 */
		if (tmp2 == 1000000)
			iap->ia_valid &= ~(ATTR_ATIME_SET|ATTR_MTIME_SET);
	}

	return true;
}

static __be32 *
encode_fattr(struct svc_rqst *rqstp, __be32 *p, struct svc_fh *fhp,
	     struct kstat *stat)
{
	struct user_namespace *userns = nfsd_user_namespace(rqstp);
	struct dentry	*dentry = fhp->fh_dentry;
	int type;
	struct timespec64 time;
	u32 f;

	type = (stat->mode & S_IFMT);

	*p++ = htonl(nfs_ftypes[type >> 12]);
	*p++ = htonl((u32) stat->mode);
	*p++ = htonl((u32) stat->nlink);
	*p++ = htonl((u32) from_kuid_munged(userns, stat->uid));
	*p++ = htonl((u32) from_kgid_munged(userns, stat->gid));

	if (S_ISLNK(type) && stat->size > NFS_MAXPATHLEN) {
		*p++ = htonl(NFS_MAXPATHLEN);
	} else {
		*p++ = htonl((u32) stat->size);
	}
	*p++ = htonl((u32) stat->blksize);
	if (S_ISCHR(type) || S_ISBLK(type))
		*p++ = htonl(new_encode_dev(stat->rdev));
	else
		*p++ = htonl(0xffffffff);
	*p++ = htonl((u32) stat->blocks);
	switch (fsid_source(fhp)) {
	default:
	case FSIDSOURCE_DEV:
		*p++ = htonl(new_encode_dev(stat->dev));
		break;
	case FSIDSOURCE_FSID:
		*p++ = htonl((u32) fhp->fh_export->ex_fsid);
		break;
	case FSIDSOURCE_UUID:
		f = ((u32*)fhp->fh_export->ex_uuid)[0];
		f ^= ((u32*)fhp->fh_export->ex_uuid)[1];
		f ^= ((u32*)fhp->fh_export->ex_uuid)[2];
		f ^= ((u32*)fhp->fh_export->ex_uuid)[3];
		*p++ = htonl(f);
		break;
	}
	*p++ = htonl((u32) stat->ino);
	*p++ = htonl((u32) stat->atime.tv_sec);
	*p++ = htonl(stat->atime.tv_nsec ? stat->atime.tv_nsec / 1000 : 0);
	time = stat->mtime;
	lease_get_mtime(d_inode(dentry), &time); 
	*p++ = htonl((u32) time.tv_sec);
	*p++ = htonl(time.tv_nsec ? time.tv_nsec / 1000 : 0); 
	*p++ = htonl((u32) stat->ctime.tv_sec);
	*p++ = htonl(stat->ctime.tv_nsec ? stat->ctime.tv_nsec / 1000 : 0);

	return p;
}

/* Helper function for NFSv2 ACL code */
__be32 *nfs2svc_encode_fattr(struct svc_rqst *rqstp, __be32 *p, struct svc_fh *fhp, struct kstat *stat)
{
	return encode_fattr(rqstp, p, fhp, stat);
}

/*
 * XDR decode functions
 */

int
nfssvc_decode_fhandleargs(struct svc_rqst *rqstp, __be32 *p)
{
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	struct nfsd_fhandle *args = rqstp->rq_argp;

	return svcxdr_decode_fhandle(xdr, &args->fh);
}

int
nfssvc_decode_sattrargs(struct svc_rqst *rqstp, __be32 *p)
{
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	struct nfsd_sattrargs *args = rqstp->rq_argp;

	return svcxdr_decode_fhandle(xdr, &args->fh) &&
		svcxdr_decode_sattr(rqstp, xdr, &args->attrs);
}

int
nfssvc_decode_diropargs(struct svc_rqst *rqstp, __be32 *p)
{
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	struct nfsd_diropargs *args = rqstp->rq_argp;

	return svcxdr_decode_diropargs(xdr, &args->fh, &args->name, &args->len);
}

int
nfssvc_decode_readargs(struct svc_rqst *rqstp, __be32 *p)
{
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	struct nfsd_readargs *args = rqstp->rq_argp;
	u32 totalcount;

	if (!svcxdr_decode_fhandle(xdr, &args->fh))
		return 0;
	if (xdr_stream_decode_u32(xdr, &args->offset) < 0)
		return 0;
	if (xdr_stream_decode_u32(xdr, &args->count) < 0)
		return 0;
	/* totalcount is ignored */
	if (xdr_stream_decode_u32(xdr, &totalcount) < 0)
		return 0;

	return 1;
}

int
nfssvc_decode_writeargs(struct svc_rqst *rqstp, __be32 *p)
{
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	struct nfsd_writeargs *args = rqstp->rq_argp;
	struct kvec *head = rqstp->rq_arg.head;
	struct kvec *tail = rqstp->rq_arg.tail;
	u32 beginoffset, totalcount;
	size_t remaining;

	if (!svcxdr_decode_fhandle(xdr, &args->fh))
		return 0;
	/* beginoffset is ignored */
	if (xdr_stream_decode_u32(xdr, &beginoffset) < 0)
		return 0;
	if (xdr_stream_decode_u32(xdr, &args->offset) < 0)
		return 0;
	/* totalcount is ignored */
	if (xdr_stream_decode_u32(xdr, &totalcount) < 0)
		return 0;

	/* opaque data */
	if (xdr_stream_decode_u32(xdr, &args->len) < 0)
		return 0;
	if (args->len > NFSSVC_MAXBLKSIZE_V2)
		return 0;
	remaining = head->iov_len + rqstp->rq_arg.page_len + tail->iov_len;
	remaining -= xdr_stream_pos(xdr);
	if (remaining < xdr_align_size(args->len))
		return 0;
	args->first.iov_base = xdr->p;
	args->first.iov_len = head->iov_len - xdr_stream_pos(xdr);

	return 1;
}

int
nfssvc_decode_createargs(struct svc_rqst *rqstp, __be32 *p)
{
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	struct nfsd_createargs *args = rqstp->rq_argp;

	return svcxdr_decode_diropargs(xdr, &args->fh,
				       &args->name, &args->len) &&
		svcxdr_decode_sattr(rqstp, xdr, &args->attrs);
}

int
nfssvc_decode_renameargs(struct svc_rqst *rqstp, __be32 *p)
{
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	struct nfsd_renameargs *args = rqstp->rq_argp;

	return svcxdr_decode_diropargs(xdr, &args->ffh,
				       &args->fname, &args->flen) &&
		svcxdr_decode_diropargs(xdr, &args->tfh,
					&args->tname, &args->tlen);
}

int
nfssvc_decode_linkargs(struct svc_rqst *rqstp, __be32 *p)
{
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	struct nfsd_linkargs *args = rqstp->rq_argp;

	return svcxdr_decode_fhandle(xdr, &args->ffh) &&
		svcxdr_decode_diropargs(xdr, &args->tfh,
					&args->tname, &args->tlen);
}

int
nfssvc_decode_symlinkargs(struct svc_rqst *rqstp, __be32 *p)
{
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	struct nfsd_symlinkargs *args = rqstp->rq_argp;
	struct kvec *head = rqstp->rq_arg.head;

	if (!svcxdr_decode_diropargs(xdr, &args->ffh, &args->fname, &args->flen))
		return 0;
	if (xdr_stream_decode_u32(xdr, &args->tlen) < 0)
		return 0;
	if (args->tlen == 0)
		return 0;

	args->first.iov_len = head->iov_len - xdr_stream_pos(xdr);
	args->first.iov_base = xdr_inline_decode(xdr, args->tlen);
	if (!args->first.iov_base)
		return 0;
	return svcxdr_decode_sattr(rqstp, xdr, &args->attrs);
}

int
nfssvc_decode_readdirargs(struct svc_rqst *rqstp, __be32 *p)
{
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	struct nfsd_readdirargs *args = rqstp->rq_argp;

	if (!svcxdr_decode_fhandle(xdr, &args->fh))
		return 0;
	if (xdr_stream_decode_u32(xdr, &args->cookie) < 0)
		return 0;
	if (xdr_stream_decode_u32(xdr, &args->count) < 0)
		return 0;

	return 1;
}

/*
 * XDR encode functions
 */

int
nfssvc_encode_stat(struct svc_rqst *rqstp, __be32 *p)
{
	struct nfsd_stat *resp = rqstp->rq_resp;

	*p++ = resp->status;
	return xdr_ressize_check(rqstp, p);
}

int
nfssvc_encode_attrstat(struct svc_rqst *rqstp, __be32 *p)
{
	struct nfsd_attrstat *resp = rqstp->rq_resp;

	*p++ = resp->status;
	if (resp->status != nfs_ok)
		goto out;
	p = encode_fattr(rqstp, p, &resp->fh, &resp->stat);
out:
	return xdr_ressize_check(rqstp, p);
}

int
nfssvc_encode_diropres(struct svc_rqst *rqstp, __be32 *p)
{
	struct nfsd_diropres *resp = rqstp->rq_resp;

	*p++ = resp->status;
	if (resp->status != nfs_ok)
		goto out;
	p = encode_fh(p, &resp->fh);
	p = encode_fattr(rqstp, p, &resp->fh, &resp->stat);
out:
	return xdr_ressize_check(rqstp, p);
}

int
nfssvc_encode_readlinkres(struct svc_rqst *rqstp, __be32 *p)
{
	struct nfsd_readlinkres *resp = rqstp->rq_resp;
	struct kvec *head = rqstp->rq_res.head;

	*p++ = resp->status;
	if (resp->status != nfs_ok)
		return xdr_ressize_check(rqstp, p);

	*p++ = htonl(resp->len);
	xdr_ressize_check(rqstp, p);
	rqstp->rq_res.page_len = resp->len;
	if (resp->len & 3) {
		/* need to pad the tail */
		rqstp->rq_res.tail[0].iov_base = p;
		*p = 0;
		rqstp->rq_res.tail[0].iov_len = 4 - (resp->len&3);
	}
	if (svc_encode_result_payload(rqstp, head->iov_len, resp->len))
		return 0;
	return 1;
}

int
nfssvc_encode_readres(struct svc_rqst *rqstp, __be32 *p)
{
	struct nfsd_readres *resp = rqstp->rq_resp;
	struct kvec *head = rqstp->rq_res.head;

	*p++ = resp->status;
	if (resp->status != nfs_ok)
		return xdr_ressize_check(rqstp, p);

	p = encode_fattr(rqstp, p, &resp->fh, &resp->stat);
	*p++ = htonl(resp->count);
	xdr_ressize_check(rqstp, p);

	/* now update rqstp->rq_res to reflect data as well */
	rqstp->rq_res.page_len = resp->count;
	if (resp->count & 3) {
		/* need to pad the tail */
		rqstp->rq_res.tail[0].iov_base = p;
		*p = 0;
		rqstp->rq_res.tail[0].iov_len = 4 - (resp->count&3);
	}
	if (svc_encode_result_payload(rqstp, head->iov_len, resp->count))
		return 0;
	return 1;
}

int
nfssvc_encode_readdirres(struct svc_rqst *rqstp, __be32 *p)
{
	struct nfsd_readdirres *resp = rqstp->rq_resp;

	*p++ = resp->status;
	if (resp->status != nfs_ok)
		return xdr_ressize_check(rqstp, p);

	xdr_ressize_check(rqstp, p);
	p = resp->buffer;
	*p++ = 0;			/* no more entries */
	*p++ = htonl((resp->common.err == nfserr_eof));
	rqstp->rq_res.page_len = (((unsigned long)p-1) & ~PAGE_MASK)+1;

	return 1;
}

int
nfssvc_encode_statfsres(struct svc_rqst *rqstp, __be32 *p)
{
	struct nfsd_statfsres *resp = rqstp->rq_resp;
	struct kstatfs	*stat = &resp->stats;

	*p++ = resp->status;
	if (resp->status != nfs_ok)
		return xdr_ressize_check(rqstp, p);

	*p++ = htonl(NFSSVC_MAXBLKSIZE_V2);	/* max transfer size */
	*p++ = htonl(stat->f_bsize);
	*p++ = htonl(stat->f_blocks);
	*p++ = htonl(stat->f_bfree);
	*p++ = htonl(stat->f_bavail);
	return xdr_ressize_check(rqstp, p);
}

int
nfssvc_encode_entry(void *ccdv, const char *name,
		    int namlen, loff_t offset, u64 ino, unsigned int d_type)
{
	struct readdir_cd *ccd = ccdv;
	struct nfsd_readdirres *cd = container_of(ccd, struct nfsd_readdirres, common);
	__be32	*p = cd->buffer;
	int	buflen, slen;

	/*
	dprintk("nfsd: entry(%.*s off %ld ino %ld)\n",
			namlen, name, offset, ino);
	 */

	if (offset > ~((u32) 0)) {
		cd->common.err = nfserr_fbig;
		return -EINVAL;
	}
	if (cd->offset)
		*cd->offset = htonl(offset);

	/* truncate filename */
	namlen = min(namlen, NFS2_MAXNAMLEN);
	slen = XDR_QUADLEN(namlen);

	if ((buflen = cd->buflen - slen - 4) < 0) {
		cd->common.err = nfserr_toosmall;
		return -EINVAL;
	}
	if (ino > ~((u32) 0)) {
		cd->common.err = nfserr_fbig;
		return -EINVAL;
	}
	*p++ = xdr_one;				/* mark entry present */
	*p++ = htonl((u32) ino);		/* file id */
	p    = xdr_encode_array(p, name, namlen);/* name length & name */
	cd->offset = p;			/* remember pointer */
	*p++ = htonl(~0U);		/* offset of next entry */

	cd->buflen = buflen;
	cd->buffer = p;
	cd->common.err = nfs_ok;
	return 0;
}

/*
 * XDR release functions
 */
void nfssvc_release_attrstat(struct svc_rqst *rqstp)
{
	struct nfsd_attrstat *resp = rqstp->rq_resp;

	fh_put(&resp->fh);
}

void nfssvc_release_diropres(struct svc_rqst *rqstp)
{
	struct nfsd_diropres *resp = rqstp->rq_resp;

	fh_put(&resp->fh);
}

void nfssvc_release_readres(struct svc_rqst *rqstp)
{
	struct nfsd_readres *resp = rqstp->rq_resp;

	fh_put(&resp->fh);
}

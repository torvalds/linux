// SPDX-License-Identifier: GPL-2.0
/*
 * XDR support for nfsd
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include "vfs.h"
#include "xdr.h"
#include "auth.h"

/*
 * Mapping of S_IF* types to NFS file types
 */
static const u32 nfs_ftypes[] = {
	NFNON,  NFCHR,  NFCHR, NFBAD,
	NFDIR,  NFBAD,  NFBLK, NFBAD,
	NFREG,  NFBAD,  NFLNK, NFBAD,
	NFSOCK, NFBAD,  NFLNK, NFBAD,
};


/*
 * Basic NFSv2 data types (RFC 1094 Section 2.3)
 */

/**
 * svcxdr_encode_stat - Encode an NFSv2 status code
 * @xdr: XDR stream
 * @status: status value to encode
 *
 * Return values:
 *   %false: Send buffer space was exhausted
 *   %true: Success
 */
bool
svcxdr_encode_stat(struct xdr_stream *xdr, __be32 status)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, sizeof(status));
	if (!p)
		return false;
	*p = status;

	return true;
}

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

static bool
svcxdr_encode_fhandle(struct xdr_stream *xdr, const struct svc_fh *fhp)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, NFS_FHSIZE);
	if (!p)
		return false;
	memcpy(p, &fhp->fh_handle.fh_base, NFS_FHSIZE);

	return true;
}

static __be32 *
encode_timeval(__be32 *p, const struct timespec64 *time)
{
	*p++ = cpu_to_be32((u32)time->tv_sec);
	if (time->tv_nsec)
		*p++ = cpu_to_be32(time->tv_nsec / NSEC_PER_USEC);
	else
		*p++ = xdr_zero;
	return p;
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

/**
 * svcxdr_encode_fattr - Encode NFSv2 file attributes
 * @rqstp: Context of a completed RPC transaction
 * @xdr: XDR stream
 * @fhp: File handle to encode
 * @stat: Attributes to encode
 *
 * Return values:
 *   %false: Send buffer space was exhausted
 *   %true: Success
 */
bool
svcxdr_encode_fattr(struct svc_rqst *rqstp, struct xdr_stream *xdr,
		    const struct svc_fh *fhp, const struct kstat *stat)
{
	struct user_namespace *userns = nfsd_user_namespace(rqstp);
	struct dentry *dentry = fhp->fh_dentry;
	int type = stat->mode & S_IFMT;
	struct timespec64 time;
	__be32 *p;
	u32 fsid;

	p = xdr_reserve_space(xdr, XDR_UNIT * 17);
	if (!p)
		return false;

	*p++ = cpu_to_be32(nfs_ftypes[type >> 12]);
	*p++ = cpu_to_be32((u32)stat->mode);
	*p++ = cpu_to_be32((u32)stat->nlink);
	*p++ = cpu_to_be32((u32)from_kuid_munged(userns, stat->uid));
	*p++ = cpu_to_be32((u32)from_kgid_munged(userns, stat->gid));

	if (S_ISLNK(type) && stat->size > NFS_MAXPATHLEN)
		*p++ = cpu_to_be32(NFS_MAXPATHLEN);
	else
		*p++ = cpu_to_be32((u32) stat->size);
	*p++ = cpu_to_be32((u32) stat->blksize);
	if (S_ISCHR(type) || S_ISBLK(type))
		*p++ = cpu_to_be32(new_encode_dev(stat->rdev));
	else
		*p++ = cpu_to_be32(0xffffffff);
	*p++ = cpu_to_be32((u32)stat->blocks);

	switch (fsid_source(fhp)) {
	case FSIDSOURCE_FSID:
		fsid = (u32)fhp->fh_export->ex_fsid;
		break;
	case FSIDSOURCE_UUID:
		fsid = ((u32 *)fhp->fh_export->ex_uuid)[0];
		fsid ^= ((u32 *)fhp->fh_export->ex_uuid)[1];
		fsid ^= ((u32 *)fhp->fh_export->ex_uuid)[2];
		fsid ^= ((u32 *)fhp->fh_export->ex_uuid)[3];
		break;
	default:
		fsid = new_encode_dev(stat->dev);
		break;
	}
	*p++ = cpu_to_be32(fsid);

	*p++ = cpu_to_be32((u32)stat->ino);
	p = encode_timeval(p, &stat->atime);
	time = stat->mtime;
	lease_get_mtime(d_inode(dentry), &time);
	p = encode_timeval(p, &time);
	encode_timeval(p, &stat->ctime);

	return true;
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
	u32 beginoffset, totalcount;

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
	if (!xdr_stream_subsegment(xdr, &args->payload, args->len))
		return 0;

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
nfssvc_encode_statres(struct svc_rqst *rqstp, __be32 *p)
{
	struct xdr_stream *xdr = &rqstp->rq_res_stream;
	struct nfsd_stat *resp = rqstp->rq_resp;

	return svcxdr_encode_stat(xdr, resp->status);
}

int
nfssvc_encode_attrstatres(struct svc_rqst *rqstp, __be32 *p)
{
	struct xdr_stream *xdr = &rqstp->rq_res_stream;
	struct nfsd_attrstat *resp = rqstp->rq_resp;

	if (!svcxdr_encode_stat(xdr, resp->status))
		return 0;
	switch (resp->status) {
	case nfs_ok:
		if (!svcxdr_encode_fattr(rqstp, xdr, &resp->fh, &resp->stat))
			return 0;
		break;
	}

	return 1;
}

int
nfssvc_encode_diropres(struct svc_rqst *rqstp, __be32 *p)
{
	struct xdr_stream *xdr = &rqstp->rq_res_stream;
	struct nfsd_diropres *resp = rqstp->rq_resp;

	if (!svcxdr_encode_stat(xdr, resp->status))
		return 0;
	switch (resp->status) {
	case nfs_ok:
		if (!svcxdr_encode_fhandle(xdr, &resp->fh))
			return 0;
		if (!svcxdr_encode_fattr(rqstp, xdr, &resp->fh, &resp->stat))
			return 0;
		break;
	}

	return 1;
}

int
nfssvc_encode_readlinkres(struct svc_rqst *rqstp, __be32 *p)
{
	struct xdr_stream *xdr = &rqstp->rq_res_stream;
	struct nfsd_readlinkres *resp = rqstp->rq_resp;
	struct kvec *head = rqstp->rq_res.head;

	if (!svcxdr_encode_stat(xdr, resp->status))
		return 0;
	switch (resp->status) {
	case nfs_ok:
		if (xdr_stream_encode_u32(xdr, resp->len) < 0)
			return 0;
		xdr_write_pages(xdr, &resp->page, 0, resp->len);
		if (svc_encode_result_payload(rqstp, head->iov_len, resp->len) < 0)
			return 0;
		break;
	}

	return 1;
}

int
nfssvc_encode_readres(struct svc_rqst *rqstp, __be32 *p)
{
	struct xdr_stream *xdr = &rqstp->rq_res_stream;
	struct nfsd_readres *resp = rqstp->rq_resp;
	struct kvec *head = rqstp->rq_res.head;

	if (!svcxdr_encode_stat(xdr, resp->status))
		return 0;
	switch (resp->status) {
	case nfs_ok:
		if (!svcxdr_encode_fattr(rqstp, xdr, &resp->fh, &resp->stat))
			return 0;
		if (xdr_stream_encode_u32(xdr, resp->count) < 0)
			return 0;
		xdr_write_pages(xdr, resp->pages, rqstp->rq_res.page_base,
				resp->count);
		if (svc_encode_result_payload(rqstp, head->iov_len, resp->count) < 0)
			return 0;
		break;
	}

	return 1;
}

int
nfssvc_encode_readdirres(struct svc_rqst *rqstp, __be32 *p)
{
	struct xdr_stream *xdr = &rqstp->rq_res_stream;
	struct nfsd_readdirres *resp = rqstp->rq_resp;
	struct xdr_buf *dirlist = &resp->dirlist;

	if (!svcxdr_encode_stat(xdr, resp->status))
		return 0;
	switch (resp->status) {
	case nfs_ok:
		xdr_write_pages(xdr, dirlist->pages, 0, dirlist->len);
		/* no more entries */
		if (xdr_stream_encode_item_absent(xdr) < 0)
			return 0;
		if (xdr_stream_encode_bool(xdr, resp->common.err == nfserr_eof) < 0)
			return 0;
		break;
	}

	return 1;
}

int
nfssvc_encode_statfsres(struct svc_rqst *rqstp, __be32 *p)
{
	struct xdr_stream *xdr = &rqstp->rq_res_stream;
	struct nfsd_statfsres *resp = rqstp->rq_resp;
	struct kstatfs	*stat = &resp->stats;

	if (!svcxdr_encode_stat(xdr, resp->status))
		return 0;
	switch (resp->status) {
	case nfs_ok:
		p = xdr_reserve_space(xdr, XDR_UNIT * 5);
		if (!p)
			return 0;
		*p++ = cpu_to_be32(NFSSVC_MAXBLKSIZE_V2);
		*p++ = cpu_to_be32(stat->f_bsize);
		*p++ = cpu_to_be32(stat->f_blocks);
		*p++ = cpu_to_be32(stat->f_bfree);
		*p = cpu_to_be32(stat->f_bavail);
		break;
	}

	return 1;
}

/**
 * nfssvc_encode_nfscookie - Encode a directory offset cookie
 * @resp: readdir result context
 * @offset: offset cookie to encode
 *
 * The buffer space for the offset cookie has already been reserved
 * by svcxdr_encode_entry_common().
 */
void nfssvc_encode_nfscookie(struct nfsd_readdirres *resp, u32 offset)
{
	__be32 cookie = cpu_to_be32(offset);

	if (!resp->cookie_offset)
		return;

	write_bytes_to_xdr_buf(&resp->dirlist, resp->cookie_offset, &cookie,
			       sizeof(cookie));
	resp->cookie_offset = 0;
}

static bool
svcxdr_encode_entry_common(struct nfsd_readdirres *resp, const char *name,
			   int namlen, loff_t offset, u64 ino)
{
	struct xdr_buf *dirlist = &resp->dirlist;
	struct xdr_stream *xdr = &resp->xdr;

	if (xdr_stream_encode_item_present(xdr) < 0)
		return false;
	/* fileid */
	if (xdr_stream_encode_u32(xdr, (u32)ino) < 0)
		return false;
	/* name */
	if (xdr_stream_encode_opaque(xdr, name, min(namlen, NFS2_MAXNAMLEN)) < 0)
		return false;
	/* cookie */
	resp->cookie_offset = dirlist->len;
	if (xdr_stream_encode_u32(xdr, ~0U) < 0)
		return false;

	return true;
}

/**
 * nfssvc_encode_entry - encode one NFSv2 READDIR entry
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
int nfssvc_encode_entry(void *data, const char *name, int namlen,
			loff_t offset, u64 ino, unsigned int d_type)
{
	struct readdir_cd *ccd = data;
	struct nfsd_readdirres *resp = container_of(ccd,
						    struct nfsd_readdirres,
						    common);
	unsigned int starting_length = resp->dirlist.len;

	/* The offset cookie for the previous entry */
	nfssvc_encode_nfscookie(resp, offset);

	if (!svcxdr_encode_entry_common(resp, name, namlen, offset, ino))
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

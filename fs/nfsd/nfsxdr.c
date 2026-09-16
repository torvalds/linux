// SPDX-License-Identifier: GPL-2.0
/*
 * XDR support for nfsd
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/filelock.h>

#include "vfs.h"
#include "nfserr.h"
#include "xdr.h"
#include "nfs2xdr_gen.h"
#include "auth.h"

/*
 * Linux-internal ftype values for socket and unknown inodes, not
 * in RFC 1094's wire enum; values match enum nfs_ftype.
 */
#define NFBAD	(7)
#define NFSOCK	(6)

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
	memcpy(&fhp->fh_handle.fh_raw, p, NFS_FHSIZE);
	fhp->fh_handle.fh_size = NFS_FHSIZE;

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

bool
nfssvc_decode_fhandleargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd_fhandle *args = rqstp->rq_argp;

	return svcxdr_decode_fhandle(xdr, &args->fh);
}

/*
 * XDR encode functions
 */

bool
nfssvc_encode_attrstatres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd_attrstat *resp = rqstp->rq_resp;

	if (!svcxdr_encode_stat(xdr, resp->status))
		return false;
	switch (resp->status) {
	case nfs_ok:
		if (!svcxdr_encode_fattr(rqstp, xdr, &resp->fh, &resp->stat))
			return false;
		break;
	}

	return true;
}

bool
nfssvc_encode_readdirres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd_readdirres *resp = rqstp->rq_resp;
	struct xdr_buf *dirlist = &resp->dirlist;

	if (!svcxdr_encode_stat(xdr, resp->status))
		return false;
	switch (resp->status) {
	case nfs_ok:
		svcxdr_encode_opaque_pages(rqstp, xdr, dirlist->pages, 0,
					   dirlist->len);
		/* no more entries */
		if (xdr_stream_encode_item_absent(xdr) < 0)
			return false;
		if (xdr_stream_encode_bool(xdr, resp->common.err == nfserr_eof) < 0)
			return false;
		break;
	}

	return true;
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
	if (xdr_stream_encode_opaque(xdr, name, min(namlen, NFS_MAXNAMLEN)) < 0)
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
 *   %-EINVAL: An encoding problem occurred, secondary status code in resp->common.err
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

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
#include "trace.h"

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

/*
 * READDIR reply entry list (RFC 1094).  The entry list is the
 * value-follows form of "entry *entries": each entry prefixed by TRUE,
 * the sequence closed by FALSE.  xdrgen's aggregate codec owns that
 * framing; the hooks below stream one entry at a time straight from the
 * directory into the live reply, mirroring nfsd4_encode_dirlist4.  The
 * directory was opened in nfsd_proc_readdir(), so reading and encoding
 * happen together here during reply encoding.
 */

/**
 * nfssvc_encode_nfscookie - Back-patch a directory entry cookie
 * @xdr: stream holding the reserved cookie slot
 * @pos: byte position of the cookie slot, or 0 when there is none
 * @cookie: cookie value to write
 *
 * An NFSv2 entry's cookie is the offset at which the following entry is
 * read.  That offset is not known until the following entry is pulled,
 * so each entry is encoded with a placeholder cookie that a later call
 * overwrites once the resume offset is known.
 */
void nfssvc_encode_nfscookie(struct xdr_stream *xdr, unsigned int pos,
			     u32 cookie)
{
	__be32 wire_cookie = cpu_to_be32(cookie);

	if (!pos)
		return;
	write_bytes_to_xdr_buf(xdr->buf, pos, &wire_cookie, XDR_UNIT);
}

/**
 * nfs2_readdirok_encode_begin - size the streaming entry budget
 * @c: aggregate cursor for the entry list
 *
 * Derive the entry byte budget from the space left in the live reply
 * and the client's count hint, reserving room for the list terminator
 * and the eof flag that follow the entries.
 *
 * Return: true.
 */
bool nfs2_readdirok_encode_begin(struct xdrgen_aggregate_cursor *c)
{
	struct svc_rqst *rqstp = c->ctx;
	struct readdirres_wrapper *resp = rqstp->rq_resp;
	struct xdr_stream *xdr = c->xdr;
	int bytes_left;

	/* Reserve the terminator FALSE and the eof bool (two words). */
	bytes_left = xdr->buf->buflen - xdr->buf->len - XDR_UNIT * 2;
	if (bytes_left < 0)
		bytes_left = 0;
	resp->space_left = min_t(u32, bytes_left,
				 clamp(resp->count, (u32)(XDR_UNIT * 2),
				       (u32)PAGE_SIZE) - XDR_UNIT * 2);
	resp->cookie_offset = 0;
	return true;
}

/**
 * nfs2_readdirok_encode - stream the next READDIR entry
 * @c: aggregate cursor for the entry list
 * @out: OUT: entry the framing encodes when one is produced
 *
 * Back-patch the previous entry's placeholder cookie with this entry's
 * resume offset, then, if the reply budget allows, project the next
 * directory entry into @out for the generated per-entry encoder.
 *
 * Return: true when @out holds an entry to encode; false to end the
 * list -- because the directory is exhausted, a host error struck, or
 * the reply budget filled.  On budget exhaustion the previous entry's
 * cookie already points at the rejected entry, the client's resume
 * point.
 */
bool nfs2_readdirok_encode(struct xdrgen_aggregate_cursor *c,
			   struct entry *out)
{
	struct svc_rqst *rqstp = c->ctx;
	struct readdirres_wrapper *resp = rqstp->rq_resp;
	struct xdr_stream *xdr = c->xdr;
	struct buffered_dirent *de;
	int namlen;
	u32 need;

	/*
	 * The previous entry was just encoded; commit the stream and note
	 * its cookie slot -- the entry's last XDR word -- so it can be
	 * back-patched once this entry's resume offset is known.
	 */
	if (c->index) {
		xdr_commit_encode(xdr);
		resp->cookie_offset = xdr->buf->len - XDR_UNIT;
	}

	de = nfsd_readdir_next(&resp->iter);
	if (!de)
		return false;

	/* The previous entry's cookie is this entry's resume offset. */
	nfssvc_encode_nfscookie(xdr, resp->cookie_offset, (u32)de->offset);

	namlen = min_t(int, de->namlen, NFS_MAXNAMLEN);

	/* value-follows + fileid + name (length + data) + cookie */
	need = XDR_UNIT * (4 + XDR_QUADLEN(namlen));
	if (need > resp->space_left)
		return false;
	resp->space_left -= need;

	out->fileid = (u32)de->ino;
	out->name.len = namlen;
	out->name.data = (unsigned char *)de->name;
	memset(out->cookie, 0, sizeof(out->cookie));	/* back-patched later */

	trace_nfsd_dirent(resp->iter.fhp, de->ino, de->name, namlen);
	return true;
}

/**
 * nfs2_readdirok_encode_end - finish the streamed entry list
 * @c: aggregate cursor for the entry list
 * @ok: false if the framing hit a wire error while encoding the list
 *
 * Back-patch the final entry's cookie with the directory's resume
 * offset and report eof.  A list cut short by the reply budget or a
 * host error reports eof false, so the client reads the rest with a
 * follow-up request.
 *
 * Return: true.
 */
bool nfs2_readdirok_encode_end(struct xdrgen_aggregate_cursor *c, bool ok)
{
	struct svc_rqst *rqstp = c->ctx;
	struct readdirres_wrapper *resp = rqstp->rq_resp;
	struct xdr_stream *xdr = c->xdr;

	if (ok)
		nfssvc_encode_nfscookie(xdr, resp->cookie_offset,
					(u32)resp->iter.offset);
	resp->xdrgen.u.readdirok.eof = resp->iter.eof;

	/*
	 * The xdr_stream primitives don't manage rq_next_page, and
	 * svcrdma retains only the pages below it for Send completion.
	 * The eof word that follows starts a new page when this one is
	 * full.
	 */
	rqstp->rq_next_page = xdr->page_ptr + 1;
	if (xdr->p == xdr->end)
		rqstp->rq_next_page++;
	return true;
}

/*
 * A server never decodes a READDIR result; these satisfy the linkage
 * of the generated (unused) decoder.
 */
bool nfs2_readdirok_decode_begin(struct xdrgen_aggregate_cursor *c)
{
	return false;
}

bool nfs2_readdirok_decode(struct xdrgen_aggregate_cursor *c,
			   const struct entry *in)
{
	return false;
}

bool nfs2_readdirok_decode_end(struct xdrgen_aggregate_cursor *c, bool ok)
{
	return false;
}

/*
 * XDR release functions
 */
void nfssvc_release_attrstat(struct svc_rqst *rqstp)
{
	struct nfsd_attrstat *resp = rqstp->rq_resp;

	fh_put(&resp->fh);
}

void nfssvc_release_readdirres(struct svc_rqst *rqstp)
{
	struct readdirres_wrapper *resp = rqstp->rq_resp;

	nfsd_readdir_close(&resp->iter);
	fh_put(&resp->fh);
}

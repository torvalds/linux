/*
 * linux/fs/nfs/nfs2xdr.c
 *
 * XDR functions to encode/decode NFS RPC arguments and results.
 *
 * Copyright (C) 1992, 1993, 1994  Rick Sladkey
 * Copyright (C) 1996 Olaf Kirch
 * 04 Aug 1998  Ion Badulescu <ionut@cs.columbia.edu>
 * 		FIFO's need special handling in NFSv2
 */

#include <linux/param.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/utsname.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs2.h>
#include <linux/nfs_fs.h>
#include "internal.h"

#define NFSDBG_FACILITY		NFSDBG_XDR
/* #define NFS_PARANOIA 1 */

/* Mapping from NFS error code to "errno" error code. */
#define errno_NFSERR_IO		EIO

/*
 * Declare the space requirements for NFS arguments and replies as
 * number of 32bit-words
 */
#define NFS_fhandle_sz		(8)
#define NFS_sattr_sz		(8)
#define NFS_filename_sz		(1+(NFS2_MAXNAMLEN>>2))
#define NFS_path_sz		(1+(NFS2_MAXPATHLEN>>2))
#define NFS_fattr_sz		(17)
#define NFS_info_sz		(5)
#define NFS_entry_sz		(NFS_filename_sz+3)

#define NFS_diropargs_sz	(NFS_fhandle_sz+NFS_filename_sz)
#define NFS_sattrargs_sz	(NFS_fhandle_sz+NFS_sattr_sz)
#define NFS_readlinkargs_sz	(NFS_fhandle_sz)
#define NFS_readargs_sz		(NFS_fhandle_sz+3)
#define NFS_writeargs_sz	(NFS_fhandle_sz+4)
#define NFS_createargs_sz	(NFS_diropargs_sz+NFS_sattr_sz)
#define NFS_renameargs_sz	(NFS_diropargs_sz+NFS_diropargs_sz)
#define NFS_linkargs_sz		(NFS_fhandle_sz+NFS_diropargs_sz)
#define NFS_symlinkargs_sz	(NFS_diropargs_sz+1+NFS_sattr_sz)
#define NFS_readdirargs_sz	(NFS_fhandle_sz+2)

#define NFS_attrstat_sz		(1+NFS_fattr_sz)
#define NFS_diropres_sz		(1+NFS_fhandle_sz+NFS_fattr_sz)
#define NFS_readlinkres_sz	(2)
#define NFS_readres_sz		(1+NFS_fattr_sz+1)
#define NFS_writeres_sz         (NFS_attrstat_sz)
#define NFS_stat_sz		(1)
#define NFS_readdirres_sz	(1)
#define NFS_statfsres_sz	(1+NFS_info_sz)

/*
 * Common NFS XDR functions as inlines
 */
static inline __be32 *
xdr_encode_fhandle(__be32 *p, struct nfs_fh *fhandle)
{
	memcpy(p, fhandle->data, NFS2_FHSIZE);
	return p + XDR_QUADLEN(NFS2_FHSIZE);
}

static inline __be32 *
xdr_decode_fhandle(__be32 *p, struct nfs_fh *fhandle)
{
	/* NFSv2 handles have a fixed length */
	fhandle->size = NFS2_FHSIZE;
	memcpy(fhandle->data, p, NFS2_FHSIZE);
	return p + XDR_QUADLEN(NFS2_FHSIZE);
}

static inline __be32*
xdr_encode_time(__be32 *p, struct timespec *timep)
{
	*p++ = htonl(timep->tv_sec);
	/* Convert nanoseconds into microseconds */
	*p++ = htonl(timep->tv_nsec ? timep->tv_nsec / 1000 : 0);
	return p;
}

static inline __be32*
xdr_encode_current_server_time(__be32 *p, struct timespec *timep)
{
	/*
	 * Passing the invalid value useconds=1000000 is a
	 * Sun convention for "set to current server time".
	 * It's needed to make permissions checks for the
	 * "touch" program across v2 mounts to Solaris and
	 * Irix boxes work correctly. See description of
	 * sattr in section 6.1 of "NFS Illustrated" by
	 * Brent Callaghan, Addison-Wesley, ISBN 0-201-32750-5
	 */
	*p++ = htonl(timep->tv_sec);
	*p++ = htonl(1000000);
	return p;
}

static inline __be32*
xdr_decode_time(__be32 *p, struct timespec *timep)
{
	timep->tv_sec = ntohl(*p++);
	/* Convert microseconds into nanoseconds */
	timep->tv_nsec = ntohl(*p++) * 1000;
	return p;
}

static __be32 *
xdr_decode_fattr(__be32 *p, struct nfs_fattr *fattr)
{
	u32 rdev;
	fattr->type = (enum nfs_ftype) ntohl(*p++);
	fattr->mode = ntohl(*p++);
	fattr->nlink = ntohl(*p++);
	fattr->uid = ntohl(*p++);
	fattr->gid = ntohl(*p++);
	fattr->size = ntohl(*p++);
	fattr->du.nfs2.blocksize = ntohl(*p++);
	rdev = ntohl(*p++);
	fattr->du.nfs2.blocks = ntohl(*p++);
	fattr->fsid.major = ntohl(*p++);
	fattr->fsid.minor = 0;
	fattr->fileid = ntohl(*p++);
	p = xdr_decode_time(p, &fattr->atime);
	p = xdr_decode_time(p, &fattr->mtime);
	p = xdr_decode_time(p, &fattr->ctime);
	fattr->valid |= NFS_ATTR_FATTR;
	fattr->rdev = new_decode_dev(rdev);
	if (fattr->type == NFCHR && rdev == NFS2_FIFO_DEV) {
		fattr->type = NFFIFO;
		fattr->mode = (fattr->mode & ~S_IFMT) | S_IFIFO;
		fattr->rdev = 0;
	}
	return p;
}

static inline __be32 *
xdr_encode_sattr(__be32 *p, struct iattr *attr)
{
	const __be32 not_set = __constant_htonl(0xFFFFFFFF);

	*p++ = (attr->ia_valid & ATTR_MODE) ? htonl(attr->ia_mode) : not_set;
	*p++ = (attr->ia_valid & ATTR_UID) ? htonl(attr->ia_uid) : not_set;
	*p++ = (attr->ia_valid & ATTR_GID) ? htonl(attr->ia_gid) : not_set;
	*p++ = (attr->ia_valid & ATTR_SIZE) ? htonl(attr->ia_size) : not_set;

	if (attr->ia_valid & ATTR_ATIME_SET) {
		p = xdr_encode_time(p, &attr->ia_atime);
	} else if (attr->ia_valid & ATTR_ATIME) {
		p = xdr_encode_current_server_time(p, &attr->ia_atime);
	} else {
		*p++ = not_set;
		*p++ = not_set;
	}

	if (attr->ia_valid & ATTR_MTIME_SET) {
		p = xdr_encode_time(p, &attr->ia_mtime);
	} else if (attr->ia_valid & ATTR_MTIME) {
		p = xdr_encode_current_server_time(p, &attr->ia_mtime);
	} else {
		*p++ = not_set;	
		*p++ = not_set;
	}
  	return p;
}

/*
 * NFS encode functions
 */
/*
 * Encode file handle argument
 * GETATTR, READLINK, STATFS
 */
static int
nfs_xdr_fhandle(struct rpc_rqst *req, __be32 *p, struct nfs_fh *fh)
{
	p = xdr_encode_fhandle(p, fh);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

/*
 * Encode SETATTR arguments
 */
static int
nfs_xdr_sattrargs(struct rpc_rqst *req, __be32 *p, struct nfs_sattrargs *args)
{
	p = xdr_encode_fhandle(p, args->fh);
	p = xdr_encode_sattr(p, args->sattr);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

/*
 * Encode directory ops argument
 * LOOKUP, REMOVE, RMDIR
 */
static int
nfs_xdr_diropargs(struct rpc_rqst *req, __be32 *p, struct nfs_diropargs *args)
{
	p = xdr_encode_fhandle(p, args->fh);
	p = xdr_encode_array(p, args->name, args->len);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

/*
 * Arguments to a READ call. Since we read data directly into the page
 * cache, we also set up the reply iovec here so that iov[1] points
 * exactly to the page we want to fetch.
 */
static int
nfs_xdr_readargs(struct rpc_rqst *req, __be32 *p, struct nfs_readargs *args)
{
	struct rpc_auth	*auth = req->rq_task->tk_auth;
	unsigned int replen;
	u32 offset = (u32)args->offset;
	u32 count = args->count;

	p = xdr_encode_fhandle(p, args->fh);
	*p++ = htonl(offset);
	*p++ = htonl(count);
	*p++ = htonl(count);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);

	/* Inline the page array */
	replen = (RPC_REPHDRSIZE + auth->au_rslack + NFS_readres_sz) << 2;
	xdr_inline_pages(&req->rq_rcv_buf, replen,
			 args->pages, args->pgbase, count);
	return 0;
}

/*
 * Decode READ reply
 */
static int
nfs_xdr_readres(struct rpc_rqst *req, __be32 *p, struct nfs_readres *res)
{
	struct kvec *iov = req->rq_rcv_buf.head;
	int	status, count, recvd, hdrlen;

	if ((status = ntohl(*p++)))
		return -nfs_stat_to_errno(status);
	p = xdr_decode_fattr(p, res->fattr);

	count = ntohl(*p++);
	res->eof = 0;
	hdrlen = (u8 *) p - (u8 *) iov->iov_base;
	if (iov->iov_len < hdrlen) {
		printk(KERN_WARNING "NFS: READ reply header overflowed:"
				"length %d > %Zu\n", hdrlen, iov->iov_len);
		return -errno_NFSERR_IO;
	} else if (iov->iov_len != hdrlen) {
		dprintk("NFS: READ header is short. iovec will be shifted.\n");
		xdr_shift_buf(&req->rq_rcv_buf, iov->iov_len - hdrlen);
	}

	recvd = req->rq_rcv_buf.len - hdrlen;
	if (count > recvd) {
		printk(KERN_WARNING "NFS: server cheating in read reply: "
			"count %d > recvd %d\n", count, recvd);
		count = recvd;
	}

	dprintk("RPC:      readres OK count %d\n", count);
	if (count < res->count)
		res->count = count;

	return count;
}


/*
 * Write arguments. Splice the buffer to be written into the iovec.
 */
static int
nfs_xdr_writeargs(struct rpc_rqst *req, __be32 *p, struct nfs_writeargs *args)
{
	struct xdr_buf *sndbuf = &req->rq_snd_buf;
	u32 offset = (u32)args->offset;
	u32 count = args->count;

	p = xdr_encode_fhandle(p, args->fh);
	*p++ = htonl(offset);
	*p++ = htonl(offset);
	*p++ = htonl(count);
	*p++ = htonl(count);
	sndbuf->len = xdr_adjust_iovec(sndbuf->head, p);

	/* Copy the page array */
	xdr_encode_pages(sndbuf, args->pages, args->pgbase, count);
	return 0;
}

/*
 * Encode create arguments
 * CREATE, MKDIR
 */
static int
nfs_xdr_createargs(struct rpc_rqst *req, __be32 *p, struct nfs_createargs *args)
{
	p = xdr_encode_fhandle(p, args->fh);
	p = xdr_encode_array(p, args->name, args->len);
	p = xdr_encode_sattr(p, args->sattr);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

/*
 * Encode RENAME arguments
 */
static int
nfs_xdr_renameargs(struct rpc_rqst *req, __be32 *p, struct nfs_renameargs *args)
{
	p = xdr_encode_fhandle(p, args->fromfh);
	p = xdr_encode_array(p, args->fromname, args->fromlen);
	p = xdr_encode_fhandle(p, args->tofh);
	p = xdr_encode_array(p, args->toname, args->tolen);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

/*
 * Encode LINK arguments
 */
static int
nfs_xdr_linkargs(struct rpc_rqst *req, __be32 *p, struct nfs_linkargs *args)
{
	p = xdr_encode_fhandle(p, args->fromfh);
	p = xdr_encode_fhandle(p, args->tofh);
	p = xdr_encode_array(p, args->toname, args->tolen);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

/*
 * Encode SYMLINK arguments
 */
static int
nfs_xdr_symlinkargs(struct rpc_rqst *req, __be32 *p, struct nfs_symlinkargs *args)
{
	struct xdr_buf *sndbuf = &req->rq_snd_buf;
	size_t pad;

	p = xdr_encode_fhandle(p, args->fromfh);
	p = xdr_encode_array(p, args->fromname, args->fromlen);
	*p++ = htonl(args->pathlen);
	sndbuf->len = xdr_adjust_iovec(sndbuf->head, p);

	xdr_encode_pages(sndbuf, args->pages, 0, args->pathlen);

	/*
	 * xdr_encode_pages may have added a few bytes to ensure the
	 * pathname ends on a 4-byte boundary.  Start encoding the
	 * attributes after the pad bytes.
	 */
	pad = sndbuf->tail->iov_len;
	if (pad > 0)
		p++;
	p = xdr_encode_sattr(p, args->sattr);
	sndbuf->len += xdr_adjust_iovec(sndbuf->tail, p) - pad;
	return 0;
}

/*
 * Encode arguments to readdir call
 */
static int
nfs_xdr_readdirargs(struct rpc_rqst *req, __be32 *p, struct nfs_readdirargs *args)
{
	struct rpc_task	*task = req->rq_task;
	struct rpc_auth	*auth = task->tk_auth;
	unsigned int replen;
	u32 count = args->count;

	p = xdr_encode_fhandle(p, args->fh);
	*p++ = htonl(args->cookie);
	*p++ = htonl(count); /* see above */
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);

	/* Inline the page array */
	replen = (RPC_REPHDRSIZE + auth->au_rslack + NFS_readdirres_sz) << 2;
	xdr_inline_pages(&req->rq_rcv_buf, replen, args->pages, 0, count);
	return 0;
}

/*
 * Decode the result of a readdir call.
 * We're not really decoding anymore, we just leave the buffer untouched
 * and only check that it is syntactically correct.
 * The real decoding happens in nfs_decode_entry below, called directly
 * from nfs_readdir for each entry.
 */
static int
nfs_xdr_readdirres(struct rpc_rqst *req, __be32 *p, void *dummy)
{
	struct xdr_buf *rcvbuf = &req->rq_rcv_buf;
	struct kvec *iov = rcvbuf->head;
	struct page **page;
	int hdrlen, recvd;
	int status, nr;
	unsigned int len, pglen;
	__be32 *end, *entry, *kaddr;

	if ((status = ntohl(*p++)))
		return -nfs_stat_to_errno(status);

	hdrlen = (u8 *) p - (u8 *) iov->iov_base;
	if (iov->iov_len < hdrlen) {
		printk(KERN_WARNING "NFS: READDIR reply header overflowed:"
				"length %d > %Zu\n", hdrlen, iov->iov_len);
		return -errno_NFSERR_IO;
	} else if (iov->iov_len != hdrlen) {
		dprintk("NFS: READDIR header is short. iovec will be shifted.\n");
		xdr_shift_buf(rcvbuf, iov->iov_len - hdrlen);
	}

	pglen = rcvbuf->page_len;
	recvd = rcvbuf->len - hdrlen;
	if (pglen > recvd)
		pglen = recvd;
	page = rcvbuf->pages;
	kaddr = p = kmap_atomic(*page, KM_USER0);
	end = (__be32 *)((char *)p + pglen);
	entry = p;
	for (nr = 0; *p++; nr++) {
		if (p + 2 > end)
			goto short_pkt;
		p++; /* fileid */
		len = ntohl(*p++);
		p += XDR_QUADLEN(len) + 1;	/* name plus cookie */
		if (len > NFS2_MAXNAMLEN) {
			printk(KERN_WARNING "NFS: giant filename in readdir (len 0x%x)!\n",
						len);
			goto err_unmap;
		}
		if (p + 2 > end)
			goto short_pkt;
		entry = p;
	}
	if (!nr && (entry[0] != 0 || entry[1] == 0))
		goto short_pkt;
 out:
	kunmap_atomic(kaddr, KM_USER0);
	return nr;
 short_pkt:
	entry[0] = entry[1] = 0;
	/* truncate listing ? */
	if (!nr) {
		printk(KERN_NOTICE "NFS: readdir reply truncated!\n");
		entry[1] = 1;
	}
	goto out;
err_unmap:
	nr = -errno_NFSERR_IO;
	goto out;
}

__be32 *
nfs_decode_dirent(__be32 *p, struct nfs_entry *entry, int plus)
{
	if (!*p++) {
		if (!*p)
			return ERR_PTR(-EAGAIN);
		entry->eof = 1;
		return ERR_PTR(-EBADCOOKIE);
	}

	entry->ino	  = ntohl(*p++);
	entry->len	  = ntohl(*p++);
	entry->name	  = (const char *) p;
	p		 += XDR_QUADLEN(entry->len);
	entry->prev_cookie	  = entry->cookie;
	entry->cookie	  = ntohl(*p++);
	entry->eof	  = !p[0] && p[1];

	return p;
}

/*
 * NFS XDR decode functions
 */
/*
 * Decode simple status reply
 */
static int
nfs_xdr_stat(struct rpc_rqst *req, __be32 *p, void *dummy)
{
	int	status;

	if ((status = ntohl(*p++)) != 0)
		status = -nfs_stat_to_errno(status);
	return status;
}

/*
 * Decode attrstat reply
 * GETATTR, SETATTR, WRITE
 */
static int
nfs_xdr_attrstat(struct rpc_rqst *req, __be32 *p, struct nfs_fattr *fattr)
{
	int	status;

	if ((status = ntohl(*p++)))
		return -nfs_stat_to_errno(status);
	xdr_decode_fattr(p, fattr);
	return 0;
}

/*
 * Decode diropres reply
 * LOOKUP, CREATE, MKDIR
 */
static int
nfs_xdr_diropres(struct rpc_rqst *req, __be32 *p, struct nfs_diropok *res)
{
	int	status;

	if ((status = ntohl(*p++)))
		return -nfs_stat_to_errno(status);
	p = xdr_decode_fhandle(p, res->fh);
	xdr_decode_fattr(p, res->fattr);
	return 0;
}

/*
 * Encode READLINK args
 */
static int
nfs_xdr_readlinkargs(struct rpc_rqst *req, __be32 *p, struct nfs_readlinkargs *args)
{
	struct rpc_auth *auth = req->rq_task->tk_auth;
	unsigned int replen;

	p = xdr_encode_fhandle(p, args->fh);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);

	/* Inline the page array */
	replen = (RPC_REPHDRSIZE + auth->au_rslack + NFS_readlinkres_sz) << 2;
	xdr_inline_pages(&req->rq_rcv_buf, replen, args->pages, args->pgbase, args->pglen);
	return 0;
}

/*
 * Decode READLINK reply
 */
static int
nfs_xdr_readlinkres(struct rpc_rqst *req, __be32 *p, void *dummy)
{
	struct xdr_buf *rcvbuf = &req->rq_rcv_buf;
	struct kvec *iov = rcvbuf->head;
	int hdrlen, len, recvd;
	char	*kaddr;
	int	status;

	if ((status = ntohl(*p++)))
		return -nfs_stat_to_errno(status);
	/* Convert length of symlink */
	len = ntohl(*p++);
	if (len >= rcvbuf->page_len || len <= 0) {
		dprintk(KERN_WARNING "nfs: server returned giant symlink!\n");
		return -ENAMETOOLONG;
	}
	hdrlen = (u8 *) p - (u8 *) iov->iov_base;
	if (iov->iov_len < hdrlen) {
		printk(KERN_WARNING "NFS: READLINK reply header overflowed:"
				"length %d > %Zu\n", hdrlen, iov->iov_len);
		return -errno_NFSERR_IO;
	} else if (iov->iov_len != hdrlen) {
		dprintk("NFS: READLINK header is short. iovec will be shifted.\n");
		xdr_shift_buf(rcvbuf, iov->iov_len - hdrlen);
	}
	recvd = req->rq_rcv_buf.len - hdrlen;
	if (recvd < len) {
		printk(KERN_WARNING "NFS: server cheating in readlink reply: "
				"count %u > recvd %u\n", len, recvd);
		return -EIO;
	}

	/* NULL terminate the string we got */
	kaddr = (char *)kmap_atomic(rcvbuf->pages[0], KM_USER0);
	kaddr[len+rcvbuf->page_base] = '\0';
	kunmap_atomic(kaddr, KM_USER0);
	return 0;
}

/*
 * Decode WRITE reply
 */
static int
nfs_xdr_writeres(struct rpc_rqst *req, __be32 *p, struct nfs_writeres *res)
{
	res->verf->committed = NFS_FILE_SYNC;
	return nfs_xdr_attrstat(req, p, res->fattr);
}

/*
 * Decode STATFS reply
 */
static int
nfs_xdr_statfsres(struct rpc_rqst *req, __be32 *p, struct nfs2_fsstat *res)
{
	int	status;

	if ((status = ntohl(*p++)))
		return -nfs_stat_to_errno(status);

	res->tsize  = ntohl(*p++);
	res->bsize  = ntohl(*p++);
	res->blocks = ntohl(*p++);
	res->bfree  = ntohl(*p++);
	res->bavail = ntohl(*p++);
	return 0;
}

/*
 * We need to translate between nfs status return values and
 * the local errno values which may not be the same.
 */
static struct {
	int stat;
	int errno;
} nfs_errtbl[] = {
	{ NFS_OK,		0		},
	{ NFSERR_PERM,		EPERM		},
	{ NFSERR_NOENT,		ENOENT		},
	{ NFSERR_IO,		errno_NFSERR_IO	},
	{ NFSERR_NXIO,		ENXIO		},
/*	{ NFSERR_EAGAIN,	EAGAIN		}, */
	{ NFSERR_ACCES,		EACCES		},
	{ NFSERR_EXIST,		EEXIST		},
	{ NFSERR_XDEV,		EXDEV		},
	{ NFSERR_NODEV,		ENODEV		},
	{ NFSERR_NOTDIR,	ENOTDIR		},
	{ NFSERR_ISDIR,		EISDIR		},
	{ NFSERR_INVAL,		EINVAL		},
	{ NFSERR_FBIG,		EFBIG		},
	{ NFSERR_NOSPC,		ENOSPC		},
	{ NFSERR_ROFS,		EROFS		},
	{ NFSERR_MLINK,		EMLINK		},
	{ NFSERR_NAMETOOLONG,	ENAMETOOLONG	},
	{ NFSERR_NOTEMPTY,	ENOTEMPTY	},
	{ NFSERR_DQUOT,		EDQUOT		},
	{ NFSERR_STALE,		ESTALE		},
	{ NFSERR_REMOTE,	EREMOTE		},
#ifdef EWFLUSH
	{ NFSERR_WFLUSH,	EWFLUSH		},
#endif
	{ NFSERR_BADHANDLE,	EBADHANDLE	},
	{ NFSERR_NOT_SYNC,	ENOTSYNC	},
	{ NFSERR_BAD_COOKIE,	EBADCOOKIE	},
	{ NFSERR_NOTSUPP,	ENOTSUPP	},
	{ NFSERR_TOOSMALL,	ETOOSMALL	},
	{ NFSERR_SERVERFAULT,	ESERVERFAULT	},
	{ NFSERR_BADTYPE,	EBADTYPE	},
	{ NFSERR_JUKEBOX,	EJUKEBOX	},
	{ -1,			EIO		}
};

/*
 * Convert an NFS error code to a local one.
 * This one is used jointly by NFSv2 and NFSv3.
 */
int
nfs_stat_to_errno(int stat)
{
	int i;

	for (i = 0; nfs_errtbl[i].stat != -1; i++) {
		if (nfs_errtbl[i].stat == stat)
			return nfs_errtbl[i].errno;
	}
	printk(KERN_ERR "nfs_stat_to_errno: bad nfs status return value: %d\n", stat);
	return nfs_errtbl[i].errno;
}

#ifndef MAX
# define MAX(a, b)	(((a) > (b))? (a) : (b))
#endif

#define PROC(proc, argtype, restype, timer)				\
[NFSPROC_##proc] = {							\
	.p_proc	    =  NFSPROC_##proc,					\
	.p_encode   =  (kxdrproc_t) nfs_xdr_##argtype,			\
	.p_decode   =  (kxdrproc_t) nfs_xdr_##restype,			\
	.p_bufsiz   =  MAX(NFS_##argtype##_sz,NFS_##restype##_sz) << 2,	\
	.p_timer    =  timer,						\
	.p_statidx  =  NFSPROC_##proc,					\
	.p_name     =  #proc,						\
	}
struct rpc_procinfo	nfs_procedures[] = {
    PROC(GETATTR,	fhandle,	attrstat, 1),
    PROC(SETATTR,	sattrargs,	attrstat, 0),
    PROC(LOOKUP,	diropargs,	diropres, 2),
    PROC(READLINK,	readlinkargs,	readlinkres, 3),
    PROC(READ,		readargs,	readres, 3),
    PROC(WRITE,		writeargs,	writeres, 4),
    PROC(CREATE,	createargs,	diropres, 0),
    PROC(REMOVE,	diropargs,	stat, 0),
    PROC(RENAME,	renameargs,	stat, 0),
    PROC(LINK,		linkargs,	stat, 0),
    PROC(SYMLINK,	symlinkargs,	stat, 0),
    PROC(MKDIR,		createargs,	diropres, 0),
    PROC(RMDIR,		diropargs,	stat, 0),
    PROC(READDIR,	readdirargs,	readdirres, 3),
    PROC(STATFS,	fhandle,	statfsres, 0),
};

struct rpc_version		nfs_version2 = {
	.number			= 2,
	.nrprocs		= ARRAY_SIZE(nfs_procedures),
	.procs			= nfs_procedures
};

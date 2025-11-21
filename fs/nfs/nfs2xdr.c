// SPDX-License-Identifier: GPL-2.0
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
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs2.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_common.h>
#include "internal.h"
#include "nfstrace.h"

#define NFSDBG_FACILITY		NFSDBG_XDR

/*
 * Declare the space requirements for NFS arguments and replies as
 * number of 32bit-words
 */
#define NFS_pagepad_sz		(1) /* Page padding */
#define NFS_fhandle_sz		(8)
#define NFS_sattr_sz		(8)
#define NFS_filename_sz		(1+(NFS2_MAXNAMLEN>>2))
#define NFS_path_sz		(1+(NFS2_MAXPATHLEN>>2))
#define NFS_fattr_sz		(17)
#define NFS_info_sz		(5)
#define NFS_entry_sz		(NFS_filename_sz+3)

#define NFS_diropargs_sz	(NFS_fhandle_sz+NFS_filename_sz)
#define NFS_removeargs_sz	(NFS_fhandle_sz+NFS_filename_sz)
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
#define NFS_readlinkres_sz	(2+NFS_pagepad_sz)
#define NFS_readres_sz		(1+NFS_fattr_sz+1+NFS_pagepad_sz)
#define NFS_writeres_sz         (NFS_attrstat_sz)
#define NFS_stat_sz		(1)
#define NFS_readdirres_sz	(1+NFS_pagepad_sz)
#define NFS_statfsres_sz	(1+NFS_info_sz)

/*
 * Encode/decode NFSv2 basic data types
 *
 * Basic NFSv2 data types are defined in section 2.3 of RFC 1094:
 * "NFS: Network File System Protocol Specification".
 *
 * Not all basic data types have their own encoding and decoding
 * functions.  For run-time efficiency, some data types are encoded
 * or decoded inline.
 */

static struct user_namespace *rpc_userns(const struct rpc_clnt *clnt)
{
	if (clnt && clnt->cl_cred)
		return clnt->cl_cred->user_ns;
	return &init_user_ns;
}

static struct user_namespace *rpc_rqst_userns(const struct rpc_rqst *rqstp)
{
	if (rqstp->rq_task)
		return rpc_userns(rqstp->rq_task->tk_client);
	return &init_user_ns;
}

/*
 *	typedef opaque	nfsdata<>;
 */
static int decode_nfsdata(struct xdr_stream *xdr, struct nfs_pgio_res *result)
{
	u32 recvd, count;
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	count = be32_to_cpup(p);
	recvd = xdr_read_pages(xdr, count);
	if (unlikely(count > recvd))
		goto out_cheating;
out:
	result->eof = 0;	/* NFSv2 does not pass EOF flag on the wire. */
	result->count = count;
	return count;
out_cheating:
	dprintk("NFS: server cheating in read result: "
		"count %u > recvd %u\n", count, recvd);
	count = recvd;
	goto out;
}

/*
 *	enum stat {
 *		NFS_OK = 0,
 *		NFSERR_PERM = 1,
 *		NFSERR_NOENT = 2,
 *		NFSERR_IO = 5,
 *		NFSERR_NXIO = 6,
 *		NFSERR_ACCES = 13,
 *		NFSERR_EXIST = 17,
 *		NFSERR_NODEV = 19,
 *		NFSERR_NOTDIR = 20,
 *		NFSERR_ISDIR = 21,
 *		NFSERR_FBIG = 27,
 *		NFSERR_NOSPC = 28,
 *		NFSERR_ROFS = 30,
 *		NFSERR_NAMETOOLONG = 63,
 *		NFSERR_NOTEMPTY = 66,
 *		NFSERR_DQUOT = 69,
 *		NFSERR_STALE = 70,
 *		NFSERR_WFLUSH = 99
 *	};
 */
static int decode_stat(struct xdr_stream *xdr, enum nfs_stat *status)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	if (unlikely(*p != cpu_to_be32(NFS_OK)))
		goto out_status;
	*status = 0;
	return 0;
out_status:
	*status = be32_to_cpup(p);
	trace_nfs_xdr_status(xdr, (int)*status);
	return 0;
}

/*
 * 2.3.2.  ftype
 *
 *	enum ftype {
 *		NFNON = 0,
 *		NFREG = 1,
 *		NFDIR = 2,
 *		NFBLK = 3,
 *		NFCHR = 4,
 *		NFLNK = 5
 *	};
 *
 */
static __be32 *xdr_decode_ftype(__be32 *p, u32 *type)
{
	*type = be32_to_cpup(p++);
	if (unlikely(*type > NF2FIFO))
		*type = NFBAD;
	return p;
}

/*
 * 2.3.3.  fhandle
 *
 *	typedef opaque fhandle[FHSIZE];
 */
static void encode_fhandle(struct xdr_stream *xdr, const struct nfs_fh *fh)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, NFS2_FHSIZE);
	memcpy(p, fh->data, NFS2_FHSIZE);
}

static int decode_fhandle(struct xdr_stream *xdr, struct nfs_fh *fh)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, NFS2_FHSIZE);
	if (unlikely(!p))
		return -EIO;
	fh->size = NFS2_FHSIZE;
	memcpy(fh->data, p, NFS2_FHSIZE);
	return 0;
}

/*
 * 2.3.4.  timeval
 *
 *	struct timeval {
 *		unsigned int seconds;
 *		unsigned int useconds;
 *	};
 */
static __be32 *xdr_encode_time(__be32 *p, const struct timespec64 *timep)
{
	*p++ = cpu_to_be32((u32)timep->tv_sec);
	if (timep->tv_nsec != 0)
		*p++ = cpu_to_be32(timep->tv_nsec / NSEC_PER_USEC);
	else
		*p++ = cpu_to_be32(0);
	return p;
}

/*
 * Passing the invalid value useconds=1000000 is a Sun convention for
 * "set to current server time".  It's needed to make permissions checks
 * for the "touch" program across v2 mounts to Solaris and Irix servers
 * work correctly.  See description of sattr in section 6.1 of "NFS
 * Illustrated" by Brent Callaghan, Addison-Wesley, ISBN 0-201-32750-5.
 */
static __be32 *xdr_encode_current_server_time(__be32 *p,
					      const struct timespec64 *timep)
{
	*p++ = cpu_to_be32(timep->tv_sec);
	*p++ = cpu_to_be32(1000000);
	return p;
}

static __be32 *xdr_decode_time(__be32 *p, struct timespec64 *timep)
{
	timep->tv_sec = be32_to_cpup(p++);
	timep->tv_nsec = be32_to_cpup(p++) * NSEC_PER_USEC;
	return p;
}

/*
 * 2.3.5.  fattr
 *
 *	struct fattr {
 *		ftype		type;
 *		unsigned int	mode;
 *		unsigned int	nlink;
 *		unsigned int	uid;
 *		unsigned int	gid;
 *		unsigned int	size;
 *		unsigned int	blocksize;
 *		unsigned int	rdev;
 *		unsigned int	blocks;
 *		unsigned int	fsid;
 *		unsigned int	fileid;
 *		timeval		atime;
 *		timeval		mtime;
 *		timeval		ctime;
 *	};
 *
 */
static int decode_fattr(struct xdr_stream *xdr, struct nfs_fattr *fattr,
		struct user_namespace *userns)
{
	u32 rdev, type;
	__be32 *p;

	p = xdr_inline_decode(xdr, NFS_fattr_sz << 2);
	if (unlikely(!p))
		return -EIO;

	fattr->valid |= NFS_ATTR_FATTR_V2;

	p = xdr_decode_ftype(p, &type);

	fattr->mode = be32_to_cpup(p++);
	fattr->nlink = be32_to_cpup(p++);
	fattr->uid = make_kuid(userns, be32_to_cpup(p++));
	if (!uid_valid(fattr->uid))
		goto out_uid;
	fattr->gid = make_kgid(userns, be32_to_cpup(p++));
	if (!gid_valid(fattr->gid))
		goto out_gid;
		
	fattr->size = be32_to_cpup(p++);
	fattr->du.nfs2.blocksize = be32_to_cpup(p++);

	rdev = be32_to_cpup(p++);
	fattr->rdev = new_decode_dev(rdev);
	if (type == (u32)NFCHR && rdev == (u32)NFS2_FIFO_DEV) {
		fattr->mode = (fattr->mode & ~S_IFMT) | S_IFIFO;
		fattr->rdev = 0;
	}

	fattr->du.nfs2.blocks = be32_to_cpup(p++);
	fattr->fsid.major = be32_to_cpup(p++);
	fattr->fsid.minor = 0;
	fattr->fileid = be32_to_cpup(p++);

	p = xdr_decode_time(p, &fattr->atime);
	p = xdr_decode_time(p, &fattr->mtime);
	xdr_decode_time(p, &fattr->ctime);
	fattr->change_attr = nfs_timespec_to_change_attr(&fattr->ctime);

	return 0;
out_uid:
	dprintk("NFS: returned invalid uid\n");
	return -EINVAL;
out_gid:
	dprintk("NFS: returned invalid gid\n");
	return -EINVAL;
}

/*
 * 2.3.6.  sattr
 *
 *	struct sattr {
 *		unsigned int	mode;
 *		unsigned int	uid;
 *		unsigned int	gid;
 *		unsigned int	size;
 *		timeval		atime;
 *		timeval		mtime;
 *	};
 */

#define NFS2_SATTR_NOT_SET	(0xffffffff)

static __be32 *xdr_time_not_set(__be32 *p)
{
	*p++ = cpu_to_be32(NFS2_SATTR_NOT_SET);
	*p++ = cpu_to_be32(NFS2_SATTR_NOT_SET);
	return p;
}

static void encode_sattr(struct xdr_stream *xdr, const struct iattr *attr,
		struct user_namespace *userns)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, NFS_sattr_sz << 2);

	if (attr->ia_valid & ATTR_MODE)
		*p++ = cpu_to_be32(attr->ia_mode);
	else
		*p++ = cpu_to_be32(NFS2_SATTR_NOT_SET);
	if (attr->ia_valid & ATTR_UID)
		*p++ = cpu_to_be32(from_kuid_munged(userns, attr->ia_uid));
	else
		*p++ = cpu_to_be32(NFS2_SATTR_NOT_SET);
	if (attr->ia_valid & ATTR_GID)
		*p++ = cpu_to_be32(from_kgid_munged(userns, attr->ia_gid));
	else
		*p++ = cpu_to_be32(NFS2_SATTR_NOT_SET);
	if (attr->ia_valid & ATTR_SIZE)
		*p++ = cpu_to_be32((u32)attr->ia_size);
	else
		*p++ = cpu_to_be32(NFS2_SATTR_NOT_SET);

	if (attr->ia_valid & ATTR_ATIME_SET)
		p = xdr_encode_time(p, &attr->ia_atime);
	else if (attr->ia_valid & ATTR_ATIME)
		p = xdr_encode_current_server_time(p, &attr->ia_atime);
	else
		p = xdr_time_not_set(p);
	if (attr->ia_valid & ATTR_MTIME_SET)
		xdr_encode_time(p, &attr->ia_mtime);
	else if (attr->ia_valid & ATTR_MTIME)
		xdr_encode_current_server_time(p, &attr->ia_mtime);
	else
		xdr_time_not_set(p);
}

/*
 * 2.3.7.  filename
 *
 *	typedef string filename<MAXNAMLEN>;
 */
static void encode_filename(struct xdr_stream *xdr,
			    const char *name, u32 length)
{
	__be32 *p;

	WARN_ON_ONCE(length > NFS2_MAXNAMLEN);
	p = xdr_reserve_space(xdr, 4 + length);
	xdr_encode_opaque(p, name, length);
}

static int decode_filename_inline(struct xdr_stream *xdr,
				  const char **name, u32 *length)
{
	__be32 *p;
	u32 count;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	count = be32_to_cpup(p);
	if (count > NFS3_MAXNAMLEN)
		goto out_nametoolong;
	p = xdr_inline_decode(xdr, count);
	if (unlikely(!p))
		return -EIO;
	*name = (const char *)p;
	*length = count;
	return 0;
out_nametoolong:
	dprintk("NFS: returned filename too long: %u\n", count);
	return -ENAMETOOLONG;
}

/*
 * 2.3.8.  path
 *
 *	typedef string path<MAXPATHLEN>;
 */
static void encode_path(struct xdr_stream *xdr, struct page **pages, u32 length)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, 4);
	*p = cpu_to_be32(length);
	xdr_write_pages(xdr, pages, 0, length);
}

static int decode_path(struct xdr_stream *xdr)
{
	u32 length, recvd;
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	length = be32_to_cpup(p);
	if (unlikely(length >= xdr->buf->page_len || length > NFS_MAXPATHLEN))
		goto out_size;
	recvd = xdr_read_pages(xdr, length);
	if (unlikely(length > recvd))
		goto out_cheating;
	xdr_terminate_string(xdr->buf, length);
	return 0;
out_size:
	dprintk("NFS: returned pathname too long: %u\n", length);
	return -ENAMETOOLONG;
out_cheating:
	dprintk("NFS: server cheating in pathname result: "
		"length %u > received %u\n", length, recvd);
	return -EIO;
}

/*
 * 2.3.9.  attrstat
 *
 *	union attrstat switch (stat status) {
 *	case NFS_OK:
 *		fattr attributes;
 *	default:
 *		void;
 *	};
 */
static int decode_attrstat(struct xdr_stream *xdr, struct nfs_fattr *result,
			   __u32 *op_status,
			   struct user_namespace *userns)
{
	enum nfs_stat status;
	int error;

	error = decode_stat(xdr, &status);
	if (unlikely(error))
		goto out;
	if (op_status)
		*op_status = status;
	if (status != NFS_OK)
		goto out_default;
	error = decode_fattr(xdr, result, userns);
out:
	return error;
out_default:
	return nfs_stat_to_errno(status);
}

/*
 * 2.3.10.  diropargs
 *
 *	struct diropargs {
 *		fhandle  dir;
 *		filename name;
 *	};
 */
static void encode_diropargs(struct xdr_stream *xdr, const struct nfs_fh *fh,
			     const char *name, u32 length)
{
	encode_fhandle(xdr, fh);
	encode_filename(xdr, name, length);
}

/*
 * 2.3.11.  diropres
 *
 *	union diropres switch (stat status) {
 *	case NFS_OK:
 *		struct {
 *			fhandle file;
 *			fattr   attributes;
 *		} diropok;
 *	default:
 *		void;
 *	};
 */
static int decode_diropok(struct xdr_stream *xdr, struct nfs_diropok *result,
		struct user_namespace *userns)
{
	int error;

	error = decode_fhandle(xdr, result->fh);
	if (unlikely(error))
		goto out;
	error = decode_fattr(xdr, result->fattr, userns);
out:
	return error;
}

static int decode_diropres(struct xdr_stream *xdr, struct nfs_diropok *result,
		struct user_namespace *userns)
{
	enum nfs_stat status;
	int error;

	error = decode_stat(xdr, &status);
	if (unlikely(error))
		goto out;
	if (status != NFS_OK)
		goto out_default;
	error = decode_diropok(xdr, result, userns);
out:
	return error;
out_default:
	return nfs_stat_to_errno(status);
}


/*
 * NFSv2 XDR encode functions
 *
 * NFSv2 argument types are defined in section 2.2 of RFC 1094:
 * "NFS: Network File System Protocol Specification".
 */

static void nfs2_xdr_enc_fhandle(struct rpc_rqst *req,
				 struct xdr_stream *xdr,
				 const void *data)
{
	const struct nfs_fh *fh = data;

	encode_fhandle(xdr, fh);
}

/*
 * 2.2.3.  sattrargs
 *
 *	struct sattrargs {
 *		fhandle file;
 *		sattr attributes;
 *	};
 */
static void nfs2_xdr_enc_sattrargs(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   const void *data)
{
	const struct nfs_sattrargs *args = data;

	encode_fhandle(xdr, args->fh);
	encode_sattr(xdr, args->sattr, rpc_rqst_userns(req));
}

static void nfs2_xdr_enc_diropargs(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   const void *data)
{
	const struct nfs_diropargs *args = data;

	encode_diropargs(xdr, args->fh, args->name, args->len);
}

static void nfs2_xdr_enc_readlinkargs(struct rpc_rqst *req,
				      struct xdr_stream *xdr,
				      const void *data)
{
	const struct nfs_readlinkargs *args = data;

	encode_fhandle(xdr, args->fh);
	rpc_prepare_reply_pages(req, args->pages, args->pgbase, args->pglen,
				NFS_readlinkres_sz - NFS_pagepad_sz);
}

/*
 * 2.2.7.  readargs
 *
 *	struct readargs {
 *		fhandle file;
 *		unsigned offset;
 *		unsigned count;
 *		unsigned totalcount;
 *	};
 */
static void encode_readargs(struct xdr_stream *xdr,
			    const struct nfs_pgio_args *args)
{
	u32 offset = args->offset;
	u32 count = args->count;
	__be32 *p;

	encode_fhandle(xdr, args->fh);

	p = xdr_reserve_space(xdr, 4 + 4 + 4);
	*p++ = cpu_to_be32(offset);
	*p++ = cpu_to_be32(count);
	*p = cpu_to_be32(count);
}

static void nfs2_xdr_enc_readargs(struct rpc_rqst *req,
				  struct xdr_stream *xdr,
				  const void *data)
{
	const struct nfs_pgio_args *args = data;

	encode_readargs(xdr, args);
	rpc_prepare_reply_pages(req, args->pages, args->pgbase, args->count,
				NFS_readres_sz - NFS_pagepad_sz);
	req->rq_rcv_buf.flags |= XDRBUF_READ;
}

/*
 * 2.2.9.  writeargs
 *
 *	struct writeargs {
 *		fhandle file;
 *		unsigned beginoffset;
 *		unsigned offset;
 *		unsigned totalcount;
 *		nfsdata data;
 *	};
 */
static void encode_writeargs(struct xdr_stream *xdr,
			     const struct nfs_pgio_args *args)
{
	u32 offset = args->offset;
	u32 count = args->count;
	__be32 *p;

	encode_fhandle(xdr, args->fh);

	p = xdr_reserve_space(xdr, 4 + 4 + 4 + 4);
	*p++ = cpu_to_be32(offset);
	*p++ = cpu_to_be32(offset);
	*p++ = cpu_to_be32(count);

	/* nfsdata */
	*p = cpu_to_be32(count);
	xdr_write_pages(xdr, args->pages, args->pgbase, count);
}

static void nfs2_xdr_enc_writeargs(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   const void *data)
{
	const struct nfs_pgio_args *args = data;

	encode_writeargs(xdr, args);
	xdr->buf->flags |= XDRBUF_WRITE;
}

/*
 * 2.2.10.  createargs
 *
 *	struct createargs {
 *		diropargs where;
 *		sattr attributes;
 *	};
 */
static void nfs2_xdr_enc_createargs(struct rpc_rqst *req,
				    struct xdr_stream *xdr,
				    const void *data)
{
	const struct nfs_createargs *args = data;

	encode_diropargs(xdr, args->fh, args->name, args->len);
	encode_sattr(xdr, args->sattr, rpc_rqst_userns(req));
}

static void nfs2_xdr_enc_removeargs(struct rpc_rqst *req,
				    struct xdr_stream *xdr,
				    const void *data)
{
	const struct nfs_removeargs *args = data;

	encode_diropargs(xdr, args->fh, args->name.name, args->name.len);
}

/*
 * 2.2.12.  renameargs
 *
 *	struct renameargs {
 *		diropargs from;
 *		diropargs to;
 *	};
 */
static void nfs2_xdr_enc_renameargs(struct rpc_rqst *req,
				    struct xdr_stream *xdr,
				    const void *data)
{
	const struct nfs_renameargs *args = data;
	const struct qstr *old = args->old_name;
	const struct qstr *new = args->new_name;

	encode_diropargs(xdr, args->old_dir, old->name, old->len);
	encode_diropargs(xdr, args->new_dir, new->name, new->len);
}

/*
 * 2.2.13.  linkargs
 *
 *	struct linkargs {
 *		fhandle from;
 *		diropargs to;
 *	};
 */
static void nfs2_xdr_enc_linkargs(struct rpc_rqst *req,
				  struct xdr_stream *xdr,
				  const void *data)
{
	const struct nfs_linkargs *args = data;

	encode_fhandle(xdr, args->fromfh);
	encode_diropargs(xdr, args->tofh, args->toname, args->tolen);
}

/*
 * 2.2.14.  symlinkargs
 *
 *	struct symlinkargs {
 *		diropargs from;
 *		path to;
 *		sattr attributes;
 *	};
 */
static void nfs2_xdr_enc_symlinkargs(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const struct nfs_symlinkargs *args = data;

	encode_diropargs(xdr, args->fromfh, args->fromname, args->fromlen);
	encode_path(xdr, args->pages, args->pathlen);
	encode_sattr(xdr, args->sattr, rpc_rqst_userns(req));
}

/*
 * 2.2.17.  readdirargs
 *
 *	struct readdirargs {
 *		fhandle dir;
 *		nfscookie cookie;
 *		unsigned count;
 *	};
 */
static void encode_readdirargs(struct xdr_stream *xdr,
			       const struct nfs_readdirargs *args)
{
	__be32 *p;

	encode_fhandle(xdr, args->fh);

	p = xdr_reserve_space(xdr, 4 + 4);
	*p++ = cpu_to_be32(args->cookie);
	*p = cpu_to_be32(args->count);
}

static void nfs2_xdr_enc_readdirargs(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const struct nfs_readdirargs *args = data;

	encode_readdirargs(xdr, args);
	rpc_prepare_reply_pages(req, args->pages, 0, args->count,
				NFS_readdirres_sz - NFS_pagepad_sz);
}

/*
 * NFSv2 XDR decode functions
 *
 * NFSv2 result types are defined in section 2.2 of RFC 1094:
 * "NFS: Network File System Protocol Specification".
 */

static int nfs2_xdr_dec_stat(struct rpc_rqst *req, struct xdr_stream *xdr,
			     void *__unused)
{
	enum nfs_stat status;
	int error;

	error = decode_stat(xdr, &status);
	if (unlikely(error))
		goto out;
	if (status != NFS_OK)
		goto out_default;
out:
	return error;
out_default:
	return nfs_stat_to_errno(status);
}

static int nfs2_xdr_dec_attrstat(struct rpc_rqst *req, struct xdr_stream *xdr,
				 void *result)
{
	return decode_attrstat(xdr, result, NULL, rpc_rqst_userns(req));
}

static int nfs2_xdr_dec_diropres(struct rpc_rqst *req, struct xdr_stream *xdr,
				 void *result)
{
	return decode_diropres(xdr, result, rpc_rqst_userns(req));
}

/*
 * 2.2.6.  readlinkres
 *
 *	union readlinkres switch (stat status) {
 *	case NFS_OK:
 *		path data;
 *	default:
 *		void;
 *	};
 */
static int nfs2_xdr_dec_readlinkres(struct rpc_rqst *req,
				    struct xdr_stream *xdr, void *__unused)
{
	enum nfs_stat status;
	int error;

	error = decode_stat(xdr, &status);
	if (unlikely(error))
		goto out;
	if (status != NFS_OK)
		goto out_default;
	error = decode_path(xdr);
out:
	return error;
out_default:
	return nfs_stat_to_errno(status);
}

/*
 * 2.2.7.  readres
 *
 *	union readres switch (stat status) {
 *	case NFS_OK:
 *		fattr attributes;
 *		nfsdata data;
 *	default:
 *		void;
 *	};
 */
static int nfs2_xdr_dec_readres(struct rpc_rqst *req, struct xdr_stream *xdr,
				void *data)
{
	struct nfs_pgio_res *result = data;
	enum nfs_stat status;
	int error;

	error = decode_stat(xdr, &status);
	if (unlikely(error))
		goto out;
	result->op_status = status;
	if (status != NFS_OK)
		goto out_default;
	error = decode_fattr(xdr, result->fattr, rpc_rqst_userns(req));
	if (unlikely(error))
		goto out;
	error = decode_nfsdata(xdr, result);
out:
	return error;
out_default:
	return nfs_stat_to_errno(status);
}

static int nfs2_xdr_dec_writeres(struct rpc_rqst *req, struct xdr_stream *xdr,
				 void *data)
{
	struct nfs_pgio_res *result = data;

	/* All NFSv2 writes are "file sync" writes */
	result->verf->committed = NFS_FILE_SYNC;
	return decode_attrstat(xdr, result->fattr, &result->op_status,
			rpc_rqst_userns(req));
}

/**
 * nfs2_decode_dirent - Decode a single NFSv2 directory entry stored in
 *                      the local page cache.
 * @xdr: XDR stream where entry resides
 * @entry: buffer to fill in with entry data
 * @plus: boolean indicating whether this should be a readdirplus entry
 *
 * Returns zero if successful, otherwise a negative errno value is
 * returned.
 *
 * This function is not invoked during READDIR reply decoding, but
 * rather whenever an application invokes the getdents(2) system call
 * on a directory already in our cache.
 *
 * 2.2.17.  entry
 *
 *	struct entry {
 *		unsigned	fileid;
 *		filename	name;
 *		nfscookie	cookie;
 *		entry		*nextentry;
 *	};
 */
int nfs2_decode_dirent(struct xdr_stream *xdr, struct nfs_entry *entry,
		       bool plus)
{
	__be32 *p;
	int error;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EAGAIN;
	if (*p++ == xdr_zero) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EAGAIN;
		if (*p++ == xdr_zero)
			return -EAGAIN;
		entry->eof = 1;
		return -EBADCOOKIE;
	}

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EAGAIN;
	entry->ino = be32_to_cpup(p);

	error = decode_filename_inline(xdr, &entry->name, &entry->len);
	if (unlikely(error))
		return error == -ENAMETOOLONG ? -ENAMETOOLONG : -EAGAIN;

	/*
	 * The type (size and byte order) of nfscookie isn't defined in
	 * RFC 1094.  This implementation assumes that it's an XDR uint32.
	 */
	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EAGAIN;
	entry->cookie = be32_to_cpup(p);

	entry->d_type = DT_UNKNOWN;

	return 0;
}

/*
 * 2.2.17.  readdirres
 *
 *	union readdirres switch (stat status) {
 *	case NFS_OK:
 *		struct {
 *			entry *entries;
 *			bool eof;
 *		} readdirok;
 *	default:
 *		void;
 *	};
 *
 * Read the directory contents into the page cache, but don't
 * touch them.  The actual decoding is done by nfs2_decode_dirent()
 * during subsequent nfs_readdir() calls.
 */
static int decode_readdirok(struct xdr_stream *xdr)
{
	return xdr_read_pages(xdr, xdr->buf->page_len);
}

static int nfs2_xdr_dec_readdirres(struct rpc_rqst *req,
				   struct xdr_stream *xdr, void *__unused)
{
	enum nfs_stat status;
	int error;

	error = decode_stat(xdr, &status);
	if (unlikely(error))
		goto out;
	if (status != NFS_OK)
		goto out_default;
	error = decode_readdirok(xdr);
out:
	return error;
out_default:
	return nfs_stat_to_errno(status);
}

/*
 * 2.2.18.  statfsres
 *
 *	union statfsres (stat status) {
 *	case NFS_OK:
 *		struct {
 *			unsigned tsize;
 *			unsigned bsize;
 *			unsigned blocks;
 *			unsigned bfree;
 *			unsigned bavail;
 *		} info;
 *	default:
 *		void;
 *	};
 */
static int decode_info(struct xdr_stream *xdr, struct nfs2_fsstat *result)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, NFS_info_sz << 2);
	if (unlikely(!p))
		return -EIO;
	result->tsize  = be32_to_cpup(p++);
	result->bsize  = be32_to_cpup(p++);
	result->blocks = be32_to_cpup(p++);
	result->bfree  = be32_to_cpup(p++);
	result->bavail = be32_to_cpup(p);
	return 0;
}

static int nfs2_xdr_dec_statfsres(struct rpc_rqst *req, struct xdr_stream *xdr,
				  void *result)
{
	enum nfs_stat status;
	int error;

	error = decode_stat(xdr, &status);
	if (unlikely(error))
		goto out;
	if (status != NFS_OK)
		goto out_default;
	error = decode_info(xdr, result);
out:
	return error;
out_default:
	return nfs_stat_to_errno(status);
}

#define PROC(proc, argtype, restype, timer)				\
[NFSPROC_##proc] = {							\
	.p_proc	    =  NFSPROC_##proc,					\
	.p_encode   =  nfs2_xdr_enc_##argtype,				\
	.p_decode   =  nfs2_xdr_dec_##restype,				\
	.p_arglen   =  NFS_##argtype##_sz,				\
	.p_replen   =  NFS_##restype##_sz,				\
	.p_timer    =  timer,						\
	.p_statidx  =  NFSPROC_##proc,					\
	.p_name     =  #proc,						\
	}
const struct rpc_procinfo nfs_procedures[] = {
	PROC(GETATTR,	fhandle,	attrstat,	1),
	PROC(SETATTR,	sattrargs,	attrstat,	0),
	PROC(LOOKUP,	diropargs,	diropres,	2),
	PROC(READLINK,	readlinkargs,	readlinkres,	3),
	PROC(READ,	readargs,	readres,	3),
	PROC(WRITE,	writeargs,	writeres,	4),
	PROC(CREATE,	createargs,	diropres,	0),
	PROC(REMOVE,	removeargs,	stat,		0),
	PROC(RENAME,	renameargs,	stat,		0),
	PROC(LINK,	linkargs,	stat,		0),
	PROC(SYMLINK,	symlinkargs,	stat,		0),
	PROC(MKDIR,	createargs,	diropres,	0),
	PROC(RMDIR,	diropargs,	stat,		0),
	PROC(READDIR,	readdirargs,	readdirres,	3),
	PROC(STATFS,	fhandle,	statfsres,	0),
};

static unsigned int nfs_version2_counts[ARRAY_SIZE(nfs_procedures)];
const struct rpc_version nfs_version2 = {
	.number			= 2,
	.nrprocs		= ARRAY_SIZE(nfs_procedures),
	.procs			= nfs_procedures,
	.counts			= nfs_version2_counts,
};

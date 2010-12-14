/*
 * linux/fs/nfs/nfs3xdr.c
 *
 * XDR functions to encode/decode NFSv3 RPC arguments and results.
 *
 * Copyright (C) 1996, 1997 Olaf Kirch
 */

#include <linux/param.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/kdev_t.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs3.h>
#include <linux/nfs_fs.h>
#include <linux/nfsacl.h>
#include "internal.h"

#define NFSDBG_FACILITY		NFSDBG_XDR

/* Mapping from NFS error code to "errno" error code. */
#define errno_NFSERR_IO		EIO

/*
 * Declare the space requirements for NFS arguments and replies as
 * number of 32bit-words
 */
#define NFS3_fhandle_sz		(1+16)
#define NFS3_fh_sz		(NFS3_fhandle_sz)	/* shorthand */
#define NFS3_sattr_sz		(15)
#define NFS3_filename_sz	(1+(NFS3_MAXNAMLEN>>2))
#define NFS3_path_sz		(1+(NFS3_MAXPATHLEN>>2))
#define NFS3_fattr_sz		(21)
#define NFS3_cookieverf_sz	(NFS3_COOKIEVERFSIZE>>2)
#define NFS3_wcc_attr_sz		(6)
#define NFS3_pre_op_attr_sz	(1+NFS3_wcc_attr_sz)
#define NFS3_post_op_attr_sz	(1+NFS3_fattr_sz)
#define NFS3_wcc_data_sz		(NFS3_pre_op_attr_sz+NFS3_post_op_attr_sz)
#define NFS3_fsstat_sz		
#define NFS3_fsinfo_sz		
#define NFS3_pathconf_sz		
#define NFS3_entry_sz		(NFS3_filename_sz+3)
#define NFS3_diropargs_sz	(NFS3_fh_sz+NFS3_filename_sz)

#define NFS3_getattrargs_sz	(NFS3_fh_sz)
#define NFS3_setattrargs_sz	(NFS3_fh_sz+NFS3_sattr_sz+3)
#define NFS3_lookupargs_sz	(NFS3_fh_sz+NFS3_filename_sz)
#define NFS3_accessargs_sz	(NFS3_fh_sz+1)
#define NFS3_readlinkargs_sz	(NFS3_fh_sz)
#define NFS3_readargs_sz	(NFS3_fh_sz+3)
#define NFS3_writeargs_sz	(NFS3_fh_sz+5)
#define NFS3_createargs_sz	(NFS3_diropargs_sz+NFS3_sattr_sz)
#define NFS3_mkdirargs_sz	(NFS3_diropargs_sz+NFS3_sattr_sz)
#define NFS3_symlinkargs_sz	(NFS3_diropargs_sz+1+NFS3_sattr_sz)
#define NFS3_mknodargs_sz	(NFS3_diropargs_sz+2+NFS3_sattr_sz)
#define NFS3_removeargs_sz	(NFS3_fh_sz+NFS3_filename_sz)
#define NFS3_renameargs_sz	(NFS3_diropargs_sz+NFS3_diropargs_sz)
#define NFS3_linkargs_sz		(NFS3_fh_sz+NFS3_diropargs_sz)
#define NFS3_readdirargs_sz	(NFS3_fh_sz+NFS3_cookieverf_sz+3)
#define NFS3_readdirplusargs_sz	(NFS3_fh_sz+NFS3_cookieverf_sz+4)
#define NFS3_commitargs_sz	(NFS3_fh_sz+3)

#define NFS3_attrstat_sz	(1+NFS3_fattr_sz)
#define NFS3_wccstat_sz		(1+NFS3_wcc_data_sz)
#define NFS3_removeres_sz	(NFS3_wccstat_sz)
#define NFS3_lookupres_sz	(1+NFS3_fh_sz+(2 * NFS3_post_op_attr_sz))
#define NFS3_accessres_sz	(1+NFS3_post_op_attr_sz+1)
#define NFS3_readlinkres_sz	(1+NFS3_post_op_attr_sz+1)
#define NFS3_readres_sz		(1+NFS3_post_op_attr_sz+3)
#define NFS3_writeres_sz	(1+NFS3_wcc_data_sz+4)
#define NFS3_createres_sz	(1+NFS3_fh_sz+NFS3_post_op_attr_sz+NFS3_wcc_data_sz)
#define NFS3_renameres_sz	(1+(2 * NFS3_wcc_data_sz))
#define NFS3_linkres_sz		(1+NFS3_post_op_attr_sz+NFS3_wcc_data_sz)
#define NFS3_readdirres_sz	(1+NFS3_post_op_attr_sz+2)
#define NFS3_fsstatres_sz	(1+NFS3_post_op_attr_sz+13)
#define NFS3_fsinfores_sz	(1+NFS3_post_op_attr_sz+12)
#define NFS3_pathconfres_sz	(1+NFS3_post_op_attr_sz+6)
#define NFS3_commitres_sz	(1+NFS3_wcc_data_sz+2)

#define ACL3_getaclargs_sz	(NFS3_fh_sz+1)
#define ACL3_setaclargs_sz	(NFS3_fh_sz+1+ \
				XDR_QUADLEN(NFS_ACL_INLINE_BUFSIZE))
#define ACL3_getaclres_sz	(1+NFS3_post_op_attr_sz+1+ \
				XDR_QUADLEN(NFS_ACL_INLINE_BUFSIZE))
#define ACL3_setaclres_sz	(1+NFS3_post_op_attr_sz)

/*
 * Map file type to S_IFMT bits
 */
static const umode_t nfs_type2fmt[] = {
	[NF3BAD] = 0,
	[NF3REG] = S_IFREG,
	[NF3DIR] = S_IFDIR,
	[NF3BLK] = S_IFBLK,
	[NF3CHR] = S_IFCHR,
	[NF3LNK] = S_IFLNK,
	[NF3SOCK] = S_IFSOCK,
	[NF3FIFO] = S_IFIFO,
};

static void print_overflow_msg(const char *func, const struct xdr_stream *xdr)
{
	dprintk("nfs: %s: prematurely hit end of receive buffer. "
		"Remaining buffer length is %tu words.\n",
		func, xdr->end - xdr->p);
}

/*
 * While encoding arguments, set up the reply buffer in advance to
 * receive reply data directly into the page cache.
 */
static void prepare_reply_buffer(struct rpc_rqst *req, struct page **pages,
				 unsigned int base, unsigned int len,
				 unsigned int bufsize)
{
	struct rpc_auth	*auth = req->rq_cred->cr_auth;
	unsigned int replen;

	replen = RPC_REPHDRSIZE + auth->au_rslack + bufsize;
	xdr_inline_pages(&req->rq_rcv_buf, replen << 2, pages, base, len);
}


/*
 * Common NFS XDR functions as inlines
 */
static inline __be32 *
xdr_decode_fhandle(__be32 *p, struct nfs_fh *fh)
{
	if ((fh->size = ntohl(*p++)) <= NFS3_FHSIZE) {
		memcpy(fh->data, p, fh->size);
		return p + XDR_QUADLEN(fh->size);
	}
	return NULL;
}

static inline __be32 *
xdr_decode_fhandle_stream(struct xdr_stream *xdr, struct nfs_fh *fh)
{
	__be32 *p;
	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	fh->size = ntohl(*p++);

	if (fh->size <= NFS3_FHSIZE) {
		p = xdr_inline_decode(xdr, fh->size);
		if (unlikely(!p))
			goto out_overflow;
		memcpy(fh->data, p, fh->size);
		return p + XDR_QUADLEN(fh->size);
	}
	return NULL;

out_overflow:
	print_overflow_msg(__func__, xdr);
	return ERR_PTR(-EIO);
}

/*
 * Encode/decode time.
 */
static inline __be32 *
xdr_decode_time3(__be32 *p, struct timespec *timep)
{
	timep->tv_sec = ntohl(*p++);
	timep->tv_nsec = ntohl(*p++);
	return p;
}

static __be32 *
xdr_decode_fattr(__be32 *p, struct nfs_fattr *fattr)
{
	unsigned int	type, major, minor;
	umode_t		fmode;

	type = ntohl(*p++);
	if (type > NF3FIFO)
		type = NF3NON;
	fmode = nfs_type2fmt[type];
	fattr->mode = (ntohl(*p++) & ~S_IFMT) | fmode;
	fattr->nlink = ntohl(*p++);
	fattr->uid = ntohl(*p++);
	fattr->gid = ntohl(*p++);
	p = xdr_decode_hyper(p, &fattr->size);
	p = xdr_decode_hyper(p, &fattr->du.nfs3.used);

	/* Turn remote device info into Linux-specific dev_t */
	major = ntohl(*p++);
	minor = ntohl(*p++);
	fattr->rdev = MKDEV(major, minor);
	if (MAJOR(fattr->rdev) != major || MINOR(fattr->rdev) != minor)
		fattr->rdev = 0;

	p = xdr_decode_hyper(p, &fattr->fsid.major);
	fattr->fsid.minor = 0;
	p = xdr_decode_hyper(p, &fattr->fileid);
	p = xdr_decode_time3(p, &fattr->atime);
	p = xdr_decode_time3(p, &fattr->mtime);
	p = xdr_decode_time3(p, &fattr->ctime);

	/* Update the mode bits */
	fattr->valid |= NFS_ATTR_FATTR_V3;
	return p;
}

static inline __be32 *
xdr_decode_wcc_attr(__be32 *p, struct nfs_fattr *fattr)
{
	p = xdr_decode_hyper(p, &fattr->pre_size);
	p = xdr_decode_time3(p, &fattr->pre_mtime);
	p = xdr_decode_time3(p, &fattr->pre_ctime);
	fattr->valid |= NFS_ATTR_FATTR_PRESIZE
		| NFS_ATTR_FATTR_PREMTIME
		| NFS_ATTR_FATTR_PRECTIME;
	return p;
}

static inline __be32 *
xdr_decode_post_op_attr(__be32 *p, struct nfs_fattr *fattr)
{
	if (*p++)
		p = xdr_decode_fattr(p, fattr);
	return p;
}

static inline __be32 *
xdr_decode_post_op_attr_stream(struct xdr_stream *xdr, struct nfs_fattr *fattr)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	if (ntohl(*p++)) {
		p = xdr_inline_decode(xdr, 84);
		if (unlikely(!p))
			goto out_overflow;
		p = xdr_decode_fattr(p, fattr);
	}
	return p;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return ERR_PTR(-EIO);
}

static inline __be32 *
xdr_decode_pre_op_attr(__be32 *p, struct nfs_fattr *fattr)
{
	if (*p++)
		return xdr_decode_wcc_attr(p, fattr);
	return p;
}


static inline __be32 *
xdr_decode_wcc_data(__be32 *p, struct nfs_fattr *fattr)
{
	p = xdr_decode_pre_op_attr(p, fattr);
	return xdr_decode_post_op_attr(p, fattr);
}


/*
 * Encode/decode NFSv3 basic data types
 *
 * Basic NFSv3 data types are defined in section 2.5 of RFC 1813:
 * "NFS Version 3 Protocol Specification".
 *
 * Not all basic data types have their own encoding and decoding
 * functions.  For run-time efficiency, some data types are encoded
 * or decoded inline.
 */

static void encode_uint32(struct xdr_stream *xdr, u32 value)
{
	__be32 *p = xdr_reserve_space(xdr, 4);
	*p = cpu_to_be32(value);
}

/*
 * filename3
 *
 *	typedef string filename3<>;
 */
static void encode_filename3(struct xdr_stream *xdr,
			     const char *name, u32 length)
{
	__be32 *p;

	BUG_ON(length > NFS3_MAXNAMLEN);
	p = xdr_reserve_space(xdr, 4 + length);
	xdr_encode_opaque(p, name, length);
}

/*
 * nfspath3
 *
 *	typedef string nfspath3<>;
 */
static void encode_nfspath3(struct xdr_stream *xdr, struct page **pages,
			    const u32 length)
{
	BUG_ON(length > NFS3_MAXPATHLEN);
	encode_uint32(xdr, length);
	xdr_write_pages(xdr, pages, 0, length);
}

/*
 * cookie3
 *
 *	typedef uint64 cookie3
 */
static __be32 *xdr_encode_cookie3(__be32 *p, u64 cookie)
{
	return xdr_encode_hyper(p, cookie);
}

/*
 * cookieverf3
 *
 *	typedef opaque cookieverf3[NFS3_COOKIEVERFSIZE];
 */
static __be32 *xdr_encode_cookieverf3(__be32 *p, const __be32 *verifier)
{
	memcpy(p, verifier, NFS3_COOKIEVERFSIZE);
	return p + XDR_QUADLEN(NFS3_COOKIEVERFSIZE);
}

/*
 * createverf3
 *
 *	typedef opaque createverf3[NFS3_CREATEVERFSIZE];
 */
static void encode_createverf3(struct xdr_stream *xdr, const __be32 *verifier)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, NFS3_CREATEVERFSIZE);
	memcpy(p, verifier, NFS3_CREATEVERFSIZE);
}

/*
 * ftype3
 *
 *	enum ftype3 {
 *		NF3REG	= 1,
 *		NF3DIR	= 2,
 *		NF3BLK	= 3,
 *		NF3CHR	= 4,
 *		NF3LNK	= 5,
 *		NF3SOCK	= 6,
 *		NF3FIFO	= 7
 *	};
 */
static void encode_ftype3(struct xdr_stream *xdr, const u32 type)
{
	BUG_ON(type > NF3FIFO);
	encode_uint32(xdr, type);
}

/*
 * specdata3
 *
 *     struct specdata3 {
 *             uint32  specdata1;
 *             uint32  specdata2;
 *     };
 */
static void encode_specdata3(struct xdr_stream *xdr, const dev_t rdev)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, 8);
	*p++ = cpu_to_be32(MAJOR(rdev));
	*p = cpu_to_be32(MINOR(rdev));
}

/*
 * nfs_fh3
 *
 *	struct nfs_fh3 {
 *		opaque       data<NFS3_FHSIZE>;
 *	};
 */
static void encode_nfs_fh3(struct xdr_stream *xdr, const struct nfs_fh *fh)
{
	__be32 *p;

	BUG_ON(fh->size > NFS3_FHSIZE);
	p = xdr_reserve_space(xdr, 4 + fh->size);
	xdr_encode_opaque(p, fh->data, fh->size);
}

/*
 * nfstime3
 *
 *	struct nfstime3 {
 *		uint32	seconds;
 *		uint32	nseconds;
 *	};
 */
static __be32 *xdr_encode_nfstime3(__be32 *p, const struct timespec *timep)
{
	*p++ = cpu_to_be32(timep->tv_sec);
	*p++ = cpu_to_be32(timep->tv_nsec);
	return p;
}

/*
 * sattr3
 *
 *	enum time_how {
 *		DONT_CHANGE		= 0,
 *		SET_TO_SERVER_TIME	= 1,
 *		SET_TO_CLIENT_TIME	= 2
 *	};
 *
 *	union set_mode3 switch (bool set_it) {
 *	case TRUE:
 *		mode3	mode;
 *	default:
 *		void;
 *	};
 *
 *	union set_uid3 switch (bool set_it) {
 *	case TRUE:
 *		uid3	uid;
 *	default:
 *		void;
 *	};
 *
 *	union set_gid3 switch (bool set_it) {
 *	case TRUE:
 *		gid3	gid;
 *	default:
 *		void;
 *	};
 *
 *	union set_size3 switch (bool set_it) {
 *	case TRUE:
 *		size3	size;
 *	default:
 *		void;
 *	};
 *
 *	union set_atime switch (time_how set_it) {
 *	case SET_TO_CLIENT_TIME:
 *		nfstime3	atime;
 *	default:
 *		void;
 *	};
 *
 *	union set_mtime switch (time_how set_it) {
 *	case SET_TO_CLIENT_TIME:
 *		nfstime3  mtime;
 *	default:
 *		void;
 *	};
 *
 *	struct sattr3 {
 *		set_mode3	mode;
 *		set_uid3	uid;
 *		set_gid3	gid;
 *		set_size3	size;
 *		set_atime	atime;
 *		set_mtime	mtime;
 *	};
 */
static void encode_sattr3(struct xdr_stream *xdr, const struct iattr *attr)
{
	u32 nbytes;
	__be32 *p;

	/*
	 * In order to make only a single xdr_reserve_space() call,
	 * pre-compute the total number of bytes to be reserved.
	 * Six boolean values, one for each set_foo field, are always
	 * present in the encoded result, so start there.
	 */
	nbytes = 6 * 4;
	if (attr->ia_valid & ATTR_MODE)
		nbytes += 4;
	if (attr->ia_valid & ATTR_UID)
		nbytes += 4;
	if (attr->ia_valid & ATTR_GID)
		nbytes += 4;
	if (attr->ia_valid & ATTR_SIZE)
		nbytes += 8;
	if (attr->ia_valid & ATTR_ATIME_SET)
		nbytes += 8;
	if (attr->ia_valid & ATTR_MTIME_SET)
		nbytes += 8;
	p = xdr_reserve_space(xdr, nbytes);

	if (attr->ia_valid & ATTR_MODE) {
		*p++ = xdr_one;
		*p++ = cpu_to_be32(attr->ia_mode & S_IALLUGO);
	} else
		*p++ = xdr_zero;

	if (attr->ia_valid & ATTR_UID) {
		*p++ = xdr_one;
		*p++ = cpu_to_be32(attr->ia_uid);
	} else
		*p++ = xdr_zero;

	if (attr->ia_valid & ATTR_GID) {
		*p++ = xdr_one;
		*p++ = cpu_to_be32(attr->ia_gid);
	} else
		*p++ = xdr_zero;

	if (attr->ia_valid & ATTR_SIZE) {
		*p++ = xdr_one;
		p = xdr_encode_hyper(p, (u64)attr->ia_size);
	} else
		*p++ = xdr_zero;

	if (attr->ia_valid & ATTR_ATIME_SET) {
		*p++ = xdr_two;
		p = xdr_encode_nfstime3(p, &attr->ia_atime);
	} else if (attr->ia_valid & ATTR_ATIME) {
		*p++ = xdr_one;
	} else
		*p++ = xdr_zero;

	if (attr->ia_valid & ATTR_MTIME_SET) {
		*p++ = xdr_two;
		xdr_encode_nfstime3(p, &attr->ia_mtime);
	} else if (attr->ia_valid & ATTR_MTIME) {
		*p = xdr_one;
	} else
		*p = xdr_zero;
}

/*
 * diropargs3
 *
 *	struct diropargs3 {
 *		nfs_fh3		dir;
 *		filename3	name;
 *	};
 */
static void encode_diropargs3(struct xdr_stream *xdr, const struct nfs_fh *fh,
			      const char *name, u32 length)
{
	encode_nfs_fh3(xdr, fh);
	encode_filename3(xdr, name, length);
}


/*
 * NFSv3 XDR encode functions
 *
 * NFSv3 argument types are defined in section 3.3 of RFC 1813:
 * "NFS Version 3 Protocol Specification".
 */

/*
 * 3.3.1  GETATTR3args
 *
 *	struct GETATTR3args {
 *		nfs_fh3  object;
 *	};
 */
static int nfs3_xdr_enc_getattr3args(struct rpc_rqst *req, __be32 *p,
				     const struct nfs_fh *fh)
{
	struct xdr_stream xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_nfs_fh3(&xdr, fh);
	return 0;
}

/*
 * 3.3.2  SETATTR3args
 *
 *	union sattrguard3 switch (bool check) {
 *	case TRUE:
 *		nfstime3  obj_ctime;
 *	case FALSE:
 *		void;
 *	};
 *
 *	struct SETATTR3args {
 *		nfs_fh3		object;
 *		sattr3		new_attributes;
 *		sattrguard3	guard;
 *	};
 */
static void encode_sattrguard3(struct xdr_stream *xdr,
			       const struct nfs3_sattrargs *args)
{
	__be32 *p;

	if (args->guard) {
		p = xdr_reserve_space(xdr, 4 + 8);
		*p++ = xdr_one;
		xdr_encode_nfstime3(p, &args->guardtime);
	} else {
		p = xdr_reserve_space(xdr, 4);
		*p = xdr_zero;
	}
}

static int nfs3_xdr_enc_setattr3args(struct rpc_rqst *req, __be32 *p,
				     const struct nfs3_sattrargs *args)
{
	struct xdr_stream xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_nfs_fh3(&xdr, args->fh);
	encode_sattr3(&xdr, args->sattr);
	encode_sattrguard3(&xdr, args);
	return 0;
}

/*
 * 3.3.3  LOOKUP3args
 *
 *	struct LOOKUP3args {
 *		diropargs3  what;
 *	};
 */
static int nfs3_xdr_enc_lookup3args(struct rpc_rqst *req, __be32 *p,
				    const struct nfs3_diropargs *args)
{
	struct xdr_stream xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_diropargs3(&xdr, args->fh, args->name, args->len);
	return 0;
}

/*
 * 3.3.4  ACCESS3args
 *
 *	struct ACCESS3args {
 *		nfs_fh3		object;
 *		uint32		access;
 *	};
 */
static void encode_access3args(struct xdr_stream *xdr,
			       const struct nfs3_accessargs *args)
{
	encode_nfs_fh3(xdr, args->fh);
	encode_uint32(xdr, args->access);
}

static int nfs3_xdr_enc_access3args(struct rpc_rqst *req, __be32 *p,
				    const struct nfs3_accessargs *args)
{
	struct xdr_stream xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_access3args(&xdr, args);
	return 0;
}

/*
 * 3.3.5  READLINK3args
 *
 *	struct READLINK3args {
 *		nfs_fh3	symlink;
 *	};
 */
static int nfs3_xdr_enc_readlink3args(struct rpc_rqst *req, __be32 *p,
				      const struct nfs3_readlinkargs *args)
{
	struct xdr_stream xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_nfs_fh3(&xdr, args->fh);
	prepare_reply_buffer(req, args->pages, args->pgbase,
					args->pglen, NFS3_readlinkres_sz);
	return 0;
}

/*
 * 3.3.6  READ3args
 *
 *	struct READ3args {
 *		nfs_fh3		file;
 *		offset3		offset;
 *		count3		count;
 *	};
 */
static void encode_read3args(struct xdr_stream *xdr,
			     const struct nfs_readargs *args)
{
	__be32 *p;

	encode_nfs_fh3(xdr, args->fh);

	p = xdr_reserve_space(xdr, 8 + 4);
	p = xdr_encode_hyper(p, args->offset);
	*p = cpu_to_be32(args->count);
}

static int nfs3_xdr_enc_read3args(struct rpc_rqst *req, __be32 *p,
				  const struct nfs_readargs *args)
{
	struct xdr_stream xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_read3args(&xdr, args);
	prepare_reply_buffer(req, args->pages, args->pgbase,
					args->count, NFS3_readres_sz);
	req->rq_rcv_buf.flags |= XDRBUF_READ;
	return 0;
}

/*
 * 3.3.7  WRITE3args
 *
 *	enum stable_how {
 *		UNSTABLE  = 0,
 *		DATA_SYNC = 1,
 *		FILE_SYNC = 2
 *	};
 *
 *	struct WRITE3args {
 *		nfs_fh3		file;
 *		offset3		offset;
 *		count3		count;
 *		stable_how	stable;
 *		opaque		data<>;
 *	};
 */
static void encode_write3args(struct xdr_stream *xdr,
			      const struct nfs_writeargs *args)
{
	__be32 *p;

	encode_nfs_fh3(xdr, args->fh);

	p = xdr_reserve_space(xdr, 8 + 4 + 4 + 4);
	p = xdr_encode_hyper(p, args->offset);
	*p++ = cpu_to_be32(args->count);

	BUG_ON(args->stable > NFS_FILE_SYNC);
	*p++ = cpu_to_be32(args->stable);

	*p = cpu_to_be32(args->count);
	xdr_write_pages(xdr, args->pages, args->pgbase, args->count);
}

static int nfs3_xdr_enc_write3args(struct rpc_rqst *req, __be32 *p,
				   const struct nfs_writeargs *args)
{
	struct xdr_stream xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_write3args(&xdr, args);
	xdr.buf->flags |= XDRBUF_WRITE;
	return 0;
}

/*
 * 3.3.8  CREATE3args
 *
 *	enum createmode3 {
 *		UNCHECKED = 0,
 *		GUARDED   = 1,
 *		EXCLUSIVE = 2
 *	};
 *
 *	union createhow3 switch (createmode3 mode) {
 *	case UNCHECKED:
 *	case GUARDED:
 *		sattr3       obj_attributes;
 *	case EXCLUSIVE:
 *		createverf3  verf;
 *	};
 *
 *	struct CREATE3args {
 *		diropargs3	where;
 *		createhow3	how;
 *	};
 */
static void encode_createhow3(struct xdr_stream *xdr,
			      const struct nfs3_createargs *args)
{
	encode_uint32(xdr, args->createmode);
	switch (args->createmode) {
	case NFS3_CREATE_UNCHECKED:
	case NFS3_CREATE_GUARDED:
		encode_sattr3(xdr, args->sattr);
		break;
	case NFS3_CREATE_EXCLUSIVE:
		encode_createverf3(xdr, args->verifier);
		break;
	default:
		BUG();
	}
}

static int nfs3_xdr_enc_create3args(struct rpc_rqst *req, __be32 *p,
				    const struct nfs3_createargs *args)
{
	struct xdr_stream xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_diropargs3(&xdr, args->fh, args->name, args->len);
	encode_createhow3(&xdr, args);
	return 0;
}

/*
 * 3.3.9  MKDIR3args
 *
 *	struct MKDIR3args {
 *		diropargs3	where;
 *		sattr3		attributes;
 *	};
 */
static int nfs3_xdr_enc_mkdir3args(struct rpc_rqst *req, __be32 *p,
				   const struct nfs3_mkdirargs *args)
{
	struct xdr_stream xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_diropargs3(&xdr, args->fh, args->name, args->len);
	encode_sattr3(&xdr, args->sattr);
	return 0;
}

/*
 * 3.3.10  SYMLINK3args
 *
 *	struct symlinkdata3 {
 *		sattr3		symlink_attributes;
 *		nfspath3	symlink_data;
 *	};
 *
 *	struct SYMLINK3args {
 *		diropargs3	where;
 *		symlinkdata3	symlink;
 *	};
 */
static void encode_symlinkdata3(struct xdr_stream *xdr,
				const struct nfs3_symlinkargs *args)
{
	encode_sattr3(xdr, args->sattr);
	encode_nfspath3(xdr, args->pages, args->pathlen);
}

static int nfs3_xdr_enc_symlink3args(struct rpc_rqst *req, __be32 *p,
				     const struct nfs3_symlinkargs *args)
{
	struct xdr_stream xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_diropargs3(&xdr, args->fromfh, args->fromname, args->fromlen);
	encode_symlinkdata3(&xdr, args);
	return 0;
}

/*
 * 3.3.11  MKNOD3args
 *
 *	struct devicedata3 {
 *		sattr3		dev_attributes;
 *		specdata3	spec;
 *	};
 *
 *	union mknoddata3 switch (ftype3 type) {
 *	case NF3CHR:
 *	case NF3BLK:
 *		devicedata3	device;
 *	case NF3SOCK:
 *	case NF3FIFO:
 *		sattr3		pipe_attributes;
 *	default:
 *		void;
 *	};
 *
 *	struct MKNOD3args {
 *		diropargs3	where;
 *		mknoddata3	what;
 *	};
 */
static void encode_devicedata3(struct xdr_stream *xdr,
			       const struct nfs3_mknodargs *args)
{
	encode_sattr3(xdr, args->sattr);
	encode_specdata3(xdr, args->rdev);
}

static void encode_mknoddata3(struct xdr_stream *xdr,
			      const struct nfs3_mknodargs *args)
{
	encode_ftype3(xdr, args->type);
	switch (args->type) {
	case NF3CHR:
	case NF3BLK:
		encode_devicedata3(xdr, args);
		break;
	case NF3SOCK:
	case NF3FIFO:
		encode_sattr3(xdr, args->sattr);
		break;
	case NF3REG:
	case NF3DIR:
		break;
	default:
		BUG();
	}
}

static int nfs3_xdr_enc_mknod3args(struct rpc_rqst *req, __be32 *p,
				   const struct nfs3_mknodargs *args)
{
	struct xdr_stream xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_diropargs3(&xdr, args->fh, args->name, args->len);
	encode_mknoddata3(&xdr, args);
	return 0;
}

/*
 * 3.3.12  REMOVE3args
 *
 *	struct REMOVE3args {
 *		diropargs3  object;
 *	};
 */
static int nfs3_xdr_enc_remove3args(struct rpc_rqst *req, __be32 *p,
				    const struct nfs_removeargs *args)
{
	struct xdr_stream xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_diropargs3(&xdr, args->fh, args->name.name, args->name.len);
	return 0;
}

/*
 * 3.3.14  RENAME3args
 *
 *	struct RENAME3args {
 *		diropargs3	from;
 *		diropargs3	to;
 *	};
 */
static int nfs3_xdr_enc_rename3args(struct rpc_rqst *req, __be32 *p,
				    const struct nfs_renameargs *args)
{
	const struct qstr *old = args->old_name;
	const struct qstr *new = args->new_name;
	struct xdr_stream xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_diropargs3(&xdr, args->old_dir, old->name, old->len);
	encode_diropargs3(&xdr, args->new_dir, new->name, new->len);
	return 0;
}

/*
 * 3.3.15  LINK3args
 *
 *	struct LINK3args {
 *		nfs_fh3		file;
 *		diropargs3	link;
 *	};
 */
static int nfs3_xdr_enc_link3args(struct rpc_rqst *req, __be32 *p,
				  const struct nfs3_linkargs *args)
{
	struct xdr_stream xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_nfs_fh3(&xdr, args->fromfh);
	encode_diropargs3(&xdr, args->tofh, args->toname, args->tolen);
	return 0;
}

/*
 * 3.3.16  READDIR3args
 *
 *	struct READDIR3args {
 *		nfs_fh3		dir;
 *		cookie3		cookie;
 *		cookieverf3	cookieverf;
 *		count3		count;
 *	};
 */
static void encode_readdir3args(struct xdr_stream *xdr,
				const struct nfs3_readdirargs *args)
{
	__be32 *p;

	encode_nfs_fh3(xdr, args->fh);

	p = xdr_reserve_space(xdr, 8 + NFS3_COOKIEVERFSIZE + 4);
	p = xdr_encode_cookie3(p, args->cookie);
	p = xdr_encode_cookieverf3(p, args->verf);
	*p = cpu_to_be32(args->count);
}

static int nfs3_xdr_enc_readdir3args(struct rpc_rqst *req, __be32 *p,
				     const struct nfs3_readdirargs *args)
{
	struct xdr_stream xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_readdir3args(&xdr, args);
	prepare_reply_buffer(req, args->pages, 0,
				args->count, NFS3_readdirres_sz);
	return 0;
}

/*
 * 3.3.17  READDIRPLUS3args
 *
 *	struct READDIRPLUS3args {
 *		nfs_fh3		dir;
 *		cookie3		cookie;
 *		cookieverf3	cookieverf;
 *		count3		dircount;
 *		count3		maxcount;
 *	};
 */
static void encode_readdirplus3args(struct xdr_stream *xdr,
				    const struct nfs3_readdirargs *args)
{
	__be32 *p;

	encode_nfs_fh3(xdr, args->fh);

	p = xdr_reserve_space(xdr, 8 + NFS3_COOKIEVERFSIZE + 4 + 4);
	p = xdr_encode_cookie3(p, args->cookie);
	p = xdr_encode_cookieverf3(p, args->verf);

	/*
	 * readdirplus: need dircount + buffer size.
	 * We just make sure we make dircount big enough
	 */
	*p++ = cpu_to_be32(args->count >> 3);

	*p = cpu_to_be32(args->count);
}

static int nfs3_xdr_enc_readdirplus3args(struct rpc_rqst *req, __be32 *p,
					 const struct nfs3_readdirargs *args)
{
	struct xdr_stream xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_readdirplus3args(&xdr, args);
	prepare_reply_buffer(req, args->pages, 0,
				args->count, NFS3_readdirres_sz);
	return 0;
}

/*
 * Decode the result of a readdir call.
 * We just check for syntactical correctness.
 */
static int
nfs3_xdr_readdirres(struct rpc_rqst *req, __be32 *p, struct nfs3_readdirres *res)
{
	struct xdr_buf *rcvbuf = &req->rq_rcv_buf;
	struct kvec *iov = rcvbuf->head;
	struct page **page;
	size_t hdrlen;
	u32 recvd, pglen;
	int status;

	status = ntohl(*p++);
	/* Decode post_op_attrs */
	p = xdr_decode_post_op_attr(p, res->dir_attr);
	if (status)
		return nfs_stat_to_errno(status);
	/* Decode verifier cookie */
	if (res->verf) {
		res->verf[0] = *p++;
		res->verf[1] = *p++;
	} else {
		p += 2;
	}

	hdrlen = (u8 *) p - (u8 *) iov->iov_base;
	if (iov->iov_len < hdrlen) {
		dprintk("NFS: READDIR reply header overflowed:"
				"length %Zu > %Zu\n", hdrlen, iov->iov_len);
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

	return pglen;
}

__be32 *
nfs3_decode_dirent(struct xdr_stream *xdr, struct nfs_entry *entry, struct nfs_server *server, int plus)
{
	__be32 *p;
	struct nfs_entry old = *entry;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	if (!ntohl(*p++)) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			goto out_overflow;
		if (!ntohl(*p++))
			return ERR_PTR(-EAGAIN);
		entry->eof = 1;
		return ERR_PTR(-EBADCOOKIE);
	}

	p = xdr_inline_decode(xdr, 12);
	if (unlikely(!p))
		goto out_overflow;
	p = xdr_decode_hyper(p, &entry->ino);
	entry->len  = ntohl(*p++);

	p = xdr_inline_decode(xdr, entry->len + 8);
	if (unlikely(!p))
		goto out_overflow;
	entry->name = (const char *) p;
	p += XDR_QUADLEN(entry->len);
	entry->prev_cookie = entry->cookie;
	p = xdr_decode_hyper(p, &entry->cookie);

	entry->d_type = DT_UNKNOWN;
	if (plus) {
		entry->fattr->valid = 0;
		p = xdr_decode_post_op_attr_stream(xdr, entry->fattr);
		if (IS_ERR(p))
			goto out_overflow_exit;
		entry->d_type = nfs_umode_to_dtype(entry->fattr->mode);
		/* In fact, a post_op_fh3: */
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			goto out_overflow;
		if (*p++) {
			p = xdr_decode_fhandle_stream(xdr, entry->fh);
			if (IS_ERR(p))
				goto out_overflow_exit;
			/* Ugh -- server reply was truncated */
			if (p == NULL) {
				dprintk("NFS: FH truncated\n");
				*entry = old;
				return ERR_PTR(-EAGAIN);
			}
		} else
			memset((u8*)(entry->fh), 0, sizeof(*entry->fh));
	}

	p = xdr_inline_peek(xdr, 8);
	if (p != NULL)
		entry->eof = !p[0] && p[1];
	else
		entry->eof = 0;

	return p;

out_overflow:
	print_overflow_msg(__func__, xdr);
out_overflow_exit:
	return ERR_PTR(-EAGAIN);
}

/*
 * 3.3.21  COMMIT3args
 *
 *	struct COMMIT3args {
 *		nfs_fh3		file;
 *		offset3		offset;
 *		count3		count;
 *	};
 */
static void encode_commit3args(struct xdr_stream *xdr,
			       const struct nfs_writeargs *args)
{
	__be32 *p;

	encode_nfs_fh3(xdr, args->fh);

	p = xdr_reserve_space(xdr, 8 + 4);
	p = xdr_encode_hyper(p, args->offset);
	*p = cpu_to_be32(args->count);
}

static int nfs3_xdr_enc_commit3args(struct rpc_rqst *req, __be32 *p,
				    const struct nfs_writeargs *args)
{
	struct xdr_stream xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_commit3args(&xdr, args);
	return 0;
}

#ifdef CONFIG_NFS_V3_ACL

static int nfs3_xdr_enc_getacl3args(struct rpc_rqst *req, __be32 *p,
				    const struct nfs3_getaclargs *args)
{
	struct xdr_stream xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_nfs_fh3(&xdr, args->fh);
	encode_uint32(&xdr, args->mask);
	if (args->mask & (NFS_ACL | NFS_DFACL))
		prepare_reply_buffer(req, args->pages, 0,
					NFSACL_MAXPAGES << PAGE_SHIFT,
					ACL3_getaclres_sz);
	return 0;
}

static int nfs3_xdr_enc_setacl3args(struct rpc_rqst *req, __be32 *p,
				    const struct nfs3_setaclargs *args)
{
	struct xdr_stream xdr;
	unsigned int base;
	int error;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_nfs_fh3(&xdr, NFS_FH(args->inode));
	encode_uint32(&xdr, args->mask);
	if (args->npages != 0)
		xdr_write_pages(&xdr, args->pages, 0, args->len);

	base = req->rq_slen;
	error = nfsacl_encode(xdr.buf, base, args->inode,
			    (args->mask & NFS_ACL) ?
			    args->acl_access : NULL, 1, 0);
	BUG_ON(error < 0);
	error = nfsacl_encode(xdr.buf, base + error, args->inode,
			    (args->mask & NFS_DFACL) ?
			    args->acl_default : NULL, 1,
			    NFS_ACL_DEFAULT);
	BUG_ON(error < 0);
	return 0;
}

#endif  /* CONFIG_NFS_V3_ACL */

/*
 * NFS XDR decode functions
 */

/*
 * Decode attrstat reply.
 */
static int
nfs3_xdr_attrstat(struct rpc_rqst *req, __be32 *p, struct nfs_fattr *fattr)
{
	int	status;

	if ((status = ntohl(*p++)))
		return nfs_stat_to_errno(status);
	xdr_decode_fattr(p, fattr);
	return 0;
}

/*
 * Decode status+wcc_data reply
 * SATTR, REMOVE, RMDIR
 */
static int
nfs3_xdr_wccstat(struct rpc_rqst *req, __be32 *p, struct nfs_fattr *fattr)
{
	int	status;

	if ((status = ntohl(*p++)))
		status = nfs_stat_to_errno(status);
	xdr_decode_wcc_data(p, fattr);
	return status;
}

static int
nfs3_xdr_removeres(struct rpc_rqst *req, __be32 *p, struct nfs_removeres *res)
{
	return nfs3_xdr_wccstat(req, p, res->dir_attr);
}

/*
 * Decode LOOKUP reply
 */
static int
nfs3_xdr_lookupres(struct rpc_rqst *req, __be32 *p, struct nfs3_diropres *res)
{
	int	status;

	if ((status = ntohl(*p++))) {
		status = nfs_stat_to_errno(status);
	} else {
		if (!(p = xdr_decode_fhandle(p, res->fh)))
			return -errno_NFSERR_IO;
		p = xdr_decode_post_op_attr(p, res->fattr);
	}
	xdr_decode_post_op_attr(p, res->dir_attr);
	return status;
}

/*
 * Decode ACCESS reply
 */
static int
nfs3_xdr_accessres(struct rpc_rqst *req, __be32 *p, struct nfs3_accessres *res)
{
	int	status = ntohl(*p++);

	p = xdr_decode_post_op_attr(p, res->fattr);
	if (status)
		return nfs_stat_to_errno(status);
	res->access = ntohl(*p++);
	return 0;
}

/*
 * Decode READLINK reply
 */
static int
nfs3_xdr_readlinkres(struct rpc_rqst *req, __be32 *p, struct nfs_fattr *fattr)
{
	struct xdr_buf *rcvbuf = &req->rq_rcv_buf;
	struct kvec *iov = rcvbuf->head;
	size_t hdrlen;
	u32 len, recvd;
	int	status;

	status = ntohl(*p++);
	p = xdr_decode_post_op_attr(p, fattr);

	if (status != 0)
		return nfs_stat_to_errno(status);

	/* Convert length of symlink */
	len = ntohl(*p++);
	if (len >= rcvbuf->page_len) {
		dprintk("nfs: server returned giant symlink!\n");
		return -ENAMETOOLONG;
	}

	hdrlen = (u8 *) p - (u8 *) iov->iov_base;
	if (iov->iov_len < hdrlen) {
		dprintk("NFS: READLINK reply header overflowed:"
				"length %Zu > %Zu\n", hdrlen, iov->iov_len);
		return -errno_NFSERR_IO;
	} else if (iov->iov_len != hdrlen) {
		dprintk("NFS: READLINK header is short. "
			"iovec will be shifted.\n");
		xdr_shift_buf(rcvbuf, iov->iov_len - hdrlen);
	}
	recvd = req->rq_rcv_buf.len - hdrlen;
	if (recvd < len) {
		dprintk("NFS: server cheating in readlink reply: "
				"count %u > recvd %u\n", len, recvd);
		return -EIO;
	}

	xdr_terminate_string(rcvbuf, len);
	return 0;
}

/*
 * Decode READ reply
 */
static int
nfs3_xdr_readres(struct rpc_rqst *req, __be32 *p, struct nfs_readres *res)
{
	struct kvec *iov = req->rq_rcv_buf.head;
	size_t hdrlen;
	u32 count, ocount, recvd;
	int status;

	status = ntohl(*p++);
	p = xdr_decode_post_op_attr(p, res->fattr);

	if (status != 0)
		return nfs_stat_to_errno(status);

	/* Decode reply count and EOF flag. NFSv3 is somewhat redundant
	 * in that it puts the count both in the res struct and in the
	 * opaque data count. */
	count    = ntohl(*p++);
	res->eof = ntohl(*p++);
	ocount   = ntohl(*p++);

	if (ocount != count) {
		dprintk("NFS: READ count doesn't match RPC opaque count.\n");
		return -errno_NFSERR_IO;
	}

	hdrlen = (u8 *) p - (u8 *) iov->iov_base;
	if (iov->iov_len < hdrlen) {
		dprintk("NFS: READ reply header overflowed:"
				"length %Zu > %Zu\n", hdrlen, iov->iov_len);
       		return -errno_NFSERR_IO;
	} else if (iov->iov_len != hdrlen) {
		dprintk("NFS: READ header is short. iovec will be shifted.\n");
		xdr_shift_buf(&req->rq_rcv_buf, iov->iov_len - hdrlen);
	}

	recvd = req->rq_rcv_buf.len - hdrlen;
	if (count > recvd) {
		dprintk("NFS: server cheating in read reply: "
			"count %u > recvd %u\n", count, recvd);
		count = recvd;
		res->eof = 0;
	}

	if (count < res->count)
		res->count = count;

	return count;
}

/*
 * Decode WRITE response
 */
static int
nfs3_xdr_writeres(struct rpc_rqst *req, __be32 *p, struct nfs_writeres *res)
{
	int	status;

	status = ntohl(*p++);
	p = xdr_decode_wcc_data(p, res->fattr);

	if (status != 0)
		return nfs_stat_to_errno(status);

	res->count = ntohl(*p++);
	res->verf->committed = (enum nfs3_stable_how)ntohl(*p++);
	res->verf->verifier[0] = *p++;
	res->verf->verifier[1] = *p++;

	return res->count;
}

/*
 * Decode a CREATE response
 */
static int
nfs3_xdr_createres(struct rpc_rqst *req, __be32 *p, struct nfs3_diropres *res)
{
	int	status;

	status = ntohl(*p++);
	if (status == 0) {
		if (*p++) {
			if (!(p = xdr_decode_fhandle(p, res->fh)))
				return -errno_NFSERR_IO;
			p = xdr_decode_post_op_attr(p, res->fattr);
		} else {
			memset(res->fh, 0, sizeof(*res->fh));
			/* Do decode post_op_attr but set it to NULL */
			p = xdr_decode_post_op_attr(p, res->fattr);
			res->fattr->valid = 0;
		}
	} else {
		status = nfs_stat_to_errno(status);
	}
	p = xdr_decode_wcc_data(p, res->dir_attr);
	return status;
}

/*
 * Decode RENAME reply
 */
static int
nfs3_xdr_renameres(struct rpc_rqst *req, __be32 *p, struct nfs_renameres *res)
{
	int	status;

	if ((status = ntohl(*p++)) != 0)
		status = nfs_stat_to_errno(status);
	p = xdr_decode_wcc_data(p, res->old_fattr);
	p = xdr_decode_wcc_data(p, res->new_fattr);
	return status;
}

/*
 * Decode LINK reply
 */
static int
nfs3_xdr_linkres(struct rpc_rqst *req, __be32 *p, struct nfs3_linkres *res)
{
	int	status;

	if ((status = ntohl(*p++)) != 0)
		status = nfs_stat_to_errno(status);
	p = xdr_decode_post_op_attr(p, res->fattr);
	p = xdr_decode_wcc_data(p, res->dir_attr);
	return status;
}

/*
 * Decode FSSTAT reply
 */
static int
nfs3_xdr_fsstatres(struct rpc_rqst *req, __be32 *p, struct nfs_fsstat *res)
{
	int		status;

	status = ntohl(*p++);

	p = xdr_decode_post_op_attr(p, res->fattr);
	if (status != 0)
		return nfs_stat_to_errno(status);

	p = xdr_decode_hyper(p, &res->tbytes);
	p = xdr_decode_hyper(p, &res->fbytes);
	p = xdr_decode_hyper(p, &res->abytes);
	p = xdr_decode_hyper(p, &res->tfiles);
	p = xdr_decode_hyper(p, &res->ffiles);
	p = xdr_decode_hyper(p, &res->afiles);

	/* ignore invarsec */
	return 0;
}

/*
 * Decode FSINFO reply
 */
static int
nfs3_xdr_fsinfores(struct rpc_rqst *req, __be32 *p, struct nfs_fsinfo *res)
{
	int		status;

	status = ntohl(*p++);

	p = xdr_decode_post_op_attr(p, res->fattr);
	if (status != 0)
		return nfs_stat_to_errno(status);

	res->rtmax  = ntohl(*p++);
	res->rtpref = ntohl(*p++);
	res->rtmult = ntohl(*p++);
	res->wtmax  = ntohl(*p++);
	res->wtpref = ntohl(*p++);
	res->wtmult = ntohl(*p++);
	res->dtpref = ntohl(*p++);
	p = xdr_decode_hyper(p, &res->maxfilesize);
	p = xdr_decode_time3(p, &res->time_delta);

	/* ignore properties */
	res->lease_time = 0;
	return 0;
}

/*
 * Decode PATHCONF reply
 */
static int
nfs3_xdr_pathconfres(struct rpc_rqst *req, __be32 *p, struct nfs_pathconf *res)
{
	int		status;

	status = ntohl(*p++);

	p = xdr_decode_post_op_attr(p, res->fattr);
	if (status != 0)
		return nfs_stat_to_errno(status);
	res->max_link = ntohl(*p++);
	res->max_namelen = ntohl(*p++);

	/* ignore remaining fields */
	return 0;
}

/*
 * Decode COMMIT reply
 */
static int
nfs3_xdr_commitres(struct rpc_rqst *req, __be32 *p, struct nfs_writeres *res)
{
	int		status;

	status = ntohl(*p++);
	p = xdr_decode_wcc_data(p, res->fattr);
	if (status != 0)
		return nfs_stat_to_errno(status);

	res->verf->verifier[0] = *p++;
	res->verf->verifier[1] = *p++;
	return 0;
}

#ifdef CONFIG_NFS_V3_ACL
/*
 * Decode GETACL reply
 */
static int
nfs3_xdr_getaclres(struct rpc_rqst *req, __be32 *p,
		   struct nfs3_getaclres *res)
{
	struct xdr_buf *buf = &req->rq_rcv_buf;
	int status = ntohl(*p++);
	struct posix_acl **acl;
	unsigned int *aclcnt;
	int err, base;

	if (status != 0)
		return nfs_stat_to_errno(status);
	p = xdr_decode_post_op_attr(p, res->fattr);
	res->mask = ntohl(*p++);
	if (res->mask & ~(NFS_ACL|NFS_ACLCNT|NFS_DFACL|NFS_DFACLCNT))
		return -EINVAL;
	base = (char *)p - (char *)req->rq_rcv_buf.head->iov_base;

	acl = (res->mask & NFS_ACL) ? &res->acl_access : NULL;
	aclcnt = (res->mask & NFS_ACLCNT) ? &res->acl_access_count : NULL;
	err = nfsacl_decode(buf, base, aclcnt, acl);

	acl = (res->mask & NFS_DFACL) ? &res->acl_default : NULL;
	aclcnt = (res->mask & NFS_DFACLCNT) ? &res->acl_default_count : NULL;
	if (err > 0)
		err = nfsacl_decode(buf, base + err, aclcnt, acl);
	return (err > 0) ? 0 : err;
}

/*
 * Decode setacl reply.
 */
static int
nfs3_xdr_setaclres(struct rpc_rqst *req, __be32 *p, struct nfs_fattr *fattr)
{
	int status = ntohl(*p++);

	if (status)
		return nfs_stat_to_errno(status);
	xdr_decode_post_op_attr(p, fattr);
	return 0;
}
#endif  /* CONFIG_NFS_V3_ACL */

#define PROC(proc, argtype, restype, timer)				\
[NFS3PROC_##proc] = {							\
	.p_proc      = NFS3PROC_##proc,					\
	.p_encode    = (kxdrproc_t)nfs3_xdr_enc_##argtype##3args,	\
	.p_decode    = (kxdrproc_t) nfs3_xdr_##restype,			\
	.p_arglen    = NFS3_##argtype##args_sz,				\
	.p_replen    = NFS3_##restype##_sz,				\
	.p_timer     = timer,						\
	.p_statidx   = NFS3PROC_##proc,					\
	.p_name      = #proc,						\
	}

struct rpc_procinfo	nfs3_procedures[] = {
	PROC(GETATTR,		getattr,	attrstat,	1),
	PROC(SETATTR,		setattr,	wccstat,	0),
	PROC(LOOKUP,		lookup,		lookupres,	2),
	PROC(ACCESS,		access,		accessres,	1),
	PROC(READLINK,		readlink,	readlinkres,	3),
	PROC(READ,		read,		readres,	3),
	PROC(WRITE,		write,		writeres,	4),
	PROC(CREATE,		create,		createres,	0),
	PROC(MKDIR,		mkdir,		createres,	0),
	PROC(SYMLINK,		symlink,	createres,	0),
	PROC(MKNOD,		mknod,		createres,	0),
	PROC(REMOVE,		remove,		removeres,	0),
	PROC(RMDIR,		lookup,		wccstat,	0),
	PROC(RENAME,		rename,		renameres,	0),
	PROC(LINK,		link,		linkres,	0),
	PROC(READDIR,		readdir,	readdirres,	3),
	PROC(READDIRPLUS,	readdirplus,	readdirres,	3),
	PROC(FSSTAT,		getattr,	fsstatres,	0),
	PROC(FSINFO,		getattr,	fsinfores,	0),
	PROC(PATHCONF,		getattr,	pathconfres,	0),
	PROC(COMMIT,		commit,		commitres,	5),
};

struct rpc_version		nfs_version3 = {
	.number			= 3,
	.nrprocs		= ARRAY_SIZE(nfs3_procedures),
	.procs			= nfs3_procedures
};

#ifdef CONFIG_NFS_V3_ACL
static struct rpc_procinfo	nfs3_acl_procedures[] = {
	[ACLPROC3_GETACL] = {
		.p_proc = ACLPROC3_GETACL,
		.p_encode = (kxdrproc_t)nfs3_xdr_enc_getacl3args,
		.p_decode = (kxdrproc_t) nfs3_xdr_getaclres,
		.p_arglen = ACL3_getaclargs_sz,
		.p_replen = ACL3_getaclres_sz,
		.p_timer = 1,
		.p_name = "GETACL",
	},
	[ACLPROC3_SETACL] = {
		.p_proc = ACLPROC3_SETACL,
		.p_encode = (kxdrproc_t)nfs3_xdr_enc_setacl3args,
		.p_decode = (kxdrproc_t) nfs3_xdr_setaclres,
		.p_arglen = ACL3_setaclargs_sz,
		.p_replen = ACL3_setaclres_sz,
		.p_timer = 0,
		.p_name = "SETACL",
	},
};

struct rpc_version		nfsacl_version3 = {
	.number			= 3,
	.nrprocs		= sizeof(nfs3_acl_procedures)/
				  sizeof(nfs3_acl_procedures[0]),
	.procs			= nfs3_acl_procedures,
};
#endif  /* CONFIG_NFS_V3_ACL */

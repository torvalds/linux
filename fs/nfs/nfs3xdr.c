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
#define NFS3_wcc_attr_sz	(6)
#define NFS3_pre_op_attr_sz	(1+NFS3_wcc_attr_sz)
#define NFS3_post_op_attr_sz	(1+NFS3_fattr_sz)
#define NFS3_wcc_data_sz	(NFS3_pre_op_attr_sz+NFS3_post_op_attr_sz)
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

#define NFS3_getattrres_sz	(1+NFS3_fattr_sz)
#define NFS3_setattrres_sz	(1+NFS3_wcc_data_sz)
#define NFS3_removeres_sz	(NFS3_setattrres_sz)
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

static int nfs3_stat_to_errno(enum nfs_stat);

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
 * Handle decode buffer overflows out-of-line.
 */
static void print_overflow_msg(const char *func, const struct xdr_stream *xdr)
{
	dprintk("NFS: %s prematurely hit the end of our receive buffer. "
		"Remaining buffer length is %tu words.\n",
		func, xdr->end - xdr->p);
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

static int decode_uint32(struct xdr_stream *xdr, u32 *value)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	*value = be32_to_cpup(p);
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_uint64(struct xdr_stream *xdr, u64 *value)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 8);
	if (unlikely(p == NULL))
		goto out_overflow;
	xdr_decode_hyper(p, value);
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

/*
 * fileid3
 *
 *	typedef uint64 fileid3;
 */
static __be32 *xdr_decode_fileid3(__be32 *p, u64 *fileid)
{
	return xdr_decode_hyper(p, fileid);
}

static int decode_fileid3(struct xdr_stream *xdr, u64 *fileid)
{
	return decode_uint64(xdr, fileid);
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

	WARN_ON_ONCE(length > NFS3_MAXNAMLEN);
	p = xdr_reserve_space(xdr, 4 + length);
	xdr_encode_opaque(p, name, length);
}

static int decode_inline_filename3(struct xdr_stream *xdr,
				   const char **name, u32 *length)
{
	__be32 *p;
	u32 count;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	count = be32_to_cpup(p);
	if (count > NFS3_MAXNAMLEN)
		goto out_nametoolong;
	p = xdr_inline_decode(xdr, count);
	if (unlikely(p == NULL))
		goto out_overflow;
	*name = (const char *)p;
	*length = count;
	return 0;

out_nametoolong:
	dprintk("NFS: returned filename too long: %u\n", count);
	return -ENAMETOOLONG;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

/*
 * nfspath3
 *
 *	typedef string nfspath3<>;
 */
static void encode_nfspath3(struct xdr_stream *xdr, struct page **pages,
			    const u32 length)
{
	encode_uint32(xdr, length);
	xdr_write_pages(xdr, pages, 0, length);
}

static int decode_nfspath3(struct xdr_stream *xdr)
{
	u32 recvd, count;
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	count = be32_to_cpup(p);
	if (unlikely(count >= xdr->buf->page_len || count > NFS3_MAXPATHLEN))
		goto out_nametoolong;
	recvd = xdr_read_pages(xdr, count);
	if (unlikely(count > recvd))
		goto out_cheating;
	xdr_terminate_string(xdr->buf, count);
	return 0;

out_nametoolong:
	dprintk("NFS: returned pathname too long: %u\n", count);
	return -ENAMETOOLONG;
out_cheating:
	dprintk("NFS: server cheating in pathname result: "
		"count %u > recvd %u\n", count, recvd);
	return -EIO;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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

static int decode_cookie3(struct xdr_stream *xdr, u64 *cookie)
{
	return decode_uint64(xdr, cookie);
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

static int decode_cookieverf3(struct xdr_stream *xdr, __be32 *verifier)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, NFS3_COOKIEVERFSIZE);
	if (unlikely(p == NULL))
		goto out_overflow;
	memcpy(verifier, p, NFS3_COOKIEVERFSIZE);
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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

static int decode_writeverf3(struct xdr_stream *xdr, struct nfs_write_verifier *verifier)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, NFS3_WRITEVERFSIZE);
	if (unlikely(p == NULL))
		goto out_overflow;
	memcpy(verifier->data, p, NFS3_WRITEVERFSIZE);
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

/*
 * size3
 *
 *	typedef uint64 size3;
 */
static __be32 *xdr_decode_size3(__be32 *p, u64 *size)
{
	return xdr_decode_hyper(p, size);
}

/*
 * nfsstat3
 *
 *	enum nfsstat3 {
 *		NFS3_OK = 0,
 *		...
 *	}
 */
#define NFS3_OK		NFS_OK

static int decode_nfsstat3(struct xdr_stream *xdr, enum nfs_stat *status)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	*status = be32_to_cpup(p);
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
	encode_uint32(xdr, type);
}

static __be32 *xdr_decode_ftype3(__be32 *p, umode_t *mode)
{
	u32 type;

	type = be32_to_cpup(p++);
	if (type > NF3FIFO)
		type = NF3NON;
	*mode = nfs_type2fmt[type];
	return p;
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

static __be32 *xdr_decode_specdata3(__be32 *p, dev_t *rdev)
{
	unsigned int major, minor;

	major = be32_to_cpup(p++);
	minor = be32_to_cpup(p++);
	*rdev = MKDEV(major, minor);
	if (MAJOR(*rdev) != major || MINOR(*rdev) != minor)
		*rdev = 0;
	return p;
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

	WARN_ON_ONCE(fh->size > NFS3_FHSIZE);
	p = xdr_reserve_space(xdr, 4 + fh->size);
	xdr_encode_opaque(p, fh->data, fh->size);
}

static int decode_nfs_fh3(struct xdr_stream *xdr, struct nfs_fh *fh)
{
	u32 length;
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	length = be32_to_cpup(p++);
	if (unlikely(length > NFS3_FHSIZE))
		goto out_toobig;
	p = xdr_inline_decode(xdr, length);
	if (unlikely(p == NULL))
		goto out_overflow;
	fh->size = length;
	memcpy(fh->data, p, length);
	return 0;
out_toobig:
	dprintk("NFS: file handle size (%u) too big\n", length);
	return -E2BIG;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static void zero_nfs_fh3(struct nfs_fh *fh)
{
	memset(fh, 0, sizeof(*fh));
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

static __be32 *xdr_decode_nfstime3(__be32 *p, struct timespec *timep)
{
	timep->tv_sec = be32_to_cpup(p++);
	timep->tv_nsec = be32_to_cpup(p++);
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
		*p++ = cpu_to_be32(from_kuid(&init_user_ns, attr->ia_uid));
	} else
		*p++ = xdr_zero;

	if (attr->ia_valid & ATTR_GID) {
		*p++ = xdr_one;
		*p++ = cpu_to_be32(from_kgid(&init_user_ns, attr->ia_gid));
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
 * fattr3
 *
 *	struct fattr3 {
 *		ftype3		type;
 *		mode3		mode;
 *		uint32		nlink;
 *		uid3		uid;
 *		gid3		gid;
 *		size3		size;
 *		size3		used;
 *		specdata3	rdev;
 *		uint64		fsid;
 *		fileid3		fileid;
 *		nfstime3	atime;
 *		nfstime3	mtime;
 *		nfstime3	ctime;
 *	};
 */
static int decode_fattr3(struct xdr_stream *xdr, struct nfs_fattr *fattr)
{
	umode_t fmode;
	__be32 *p;

	p = xdr_inline_decode(xdr, NFS3_fattr_sz << 2);
	if (unlikely(p == NULL))
		goto out_overflow;

	p = xdr_decode_ftype3(p, &fmode);

	fattr->mode = (be32_to_cpup(p++) & ~S_IFMT) | fmode;
	fattr->nlink = be32_to_cpup(p++);
	fattr->uid = make_kuid(&init_user_ns, be32_to_cpup(p++));
	if (!uid_valid(fattr->uid))
		goto out_uid;
	fattr->gid = make_kgid(&init_user_ns, be32_to_cpup(p++));
	if (!gid_valid(fattr->gid))
		goto out_gid;

	p = xdr_decode_size3(p, &fattr->size);
	p = xdr_decode_size3(p, &fattr->du.nfs3.used);
	p = xdr_decode_specdata3(p, &fattr->rdev);

	p = xdr_decode_hyper(p, &fattr->fsid.major);
	fattr->fsid.minor = 0;

	p = xdr_decode_fileid3(p, &fattr->fileid);
	p = xdr_decode_nfstime3(p, &fattr->atime);
	p = xdr_decode_nfstime3(p, &fattr->mtime);
	xdr_decode_nfstime3(p, &fattr->ctime);
	fattr->change_attr = nfs_timespec_to_change_attr(&fattr->ctime);

	fattr->valid |= NFS_ATTR_FATTR_V3;
	return 0;
out_uid:
	dprintk("NFS: returned invalid uid\n");
	return -EINVAL;
out_gid:
	dprintk("NFS: returned invalid gid\n");
	return -EINVAL;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

/*
 * post_op_attr
 *
 *	union post_op_attr switch (bool attributes_follow) {
 *	case TRUE:
 *		fattr3	attributes;
 *	case FALSE:
 *		void;
 *	};
 */
static int decode_post_op_attr(struct xdr_stream *xdr, struct nfs_fattr *fattr)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	if (*p != xdr_zero)
		return decode_fattr3(xdr, fattr);
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

/*
 * wcc_attr
 *	struct wcc_attr {
 *		size3		size;
 *		nfstime3	mtime;
 *		nfstime3	ctime;
 *	};
 */
static int decode_wcc_attr(struct xdr_stream *xdr, struct nfs_fattr *fattr)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, NFS3_wcc_attr_sz << 2);
	if (unlikely(p == NULL))
		goto out_overflow;

	fattr->valid |= NFS_ATTR_FATTR_PRESIZE
		| NFS_ATTR_FATTR_PRECHANGE
		| NFS_ATTR_FATTR_PREMTIME
		| NFS_ATTR_FATTR_PRECTIME;

	p = xdr_decode_size3(p, &fattr->pre_size);
	p = xdr_decode_nfstime3(p, &fattr->pre_mtime);
	xdr_decode_nfstime3(p, &fattr->pre_ctime);
	fattr->pre_change_attr = nfs_timespec_to_change_attr(&fattr->pre_ctime);

	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

/*
 * pre_op_attr
 *	union pre_op_attr switch (bool attributes_follow) {
 *	case TRUE:
 *		wcc_attr	attributes;
 *	case FALSE:
 *		void;
 *	};
 *
 * wcc_data
 *
 *	struct wcc_data {
 *		pre_op_attr	before;
 *		post_op_attr	after;
 *	};
 */
static int decode_pre_op_attr(struct xdr_stream *xdr, struct nfs_fattr *fattr)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	if (*p != xdr_zero)
		return decode_wcc_attr(xdr, fattr);
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_wcc_data(struct xdr_stream *xdr, struct nfs_fattr *fattr)
{
	int error;

	error = decode_pre_op_attr(xdr, fattr);
	if (unlikely(error))
		goto out;
	error = decode_post_op_attr(xdr, fattr);
out:
	return error;
}

/*
 * post_op_fh3
 *
 *	union post_op_fh3 switch (bool handle_follows) {
 *	case TRUE:
 *		nfs_fh3  handle;
 *	case FALSE:
 *		void;
 *	};
 */
static int decode_post_op_fh3(struct xdr_stream *xdr, struct nfs_fh *fh)
{
	__be32 *p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	if (*p != xdr_zero)
		return decode_nfs_fh3(xdr, fh);
	zero_nfs_fh3(fh);
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
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
static void nfs3_xdr_enc_getattr3args(struct rpc_rqst *req,
				      struct xdr_stream *xdr,
				      const void *data)
{
	const struct nfs_fh *fh = data;

	encode_nfs_fh3(xdr, fh);
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

static void nfs3_xdr_enc_setattr3args(struct rpc_rqst *req,
				      struct xdr_stream *xdr,
				      const void *data)
{
	const struct nfs3_sattrargs *args = data;
	encode_nfs_fh3(xdr, args->fh);
	encode_sattr3(xdr, args->sattr);
	encode_sattrguard3(xdr, args);
}

/*
 * 3.3.3  LOOKUP3args
 *
 *	struct LOOKUP3args {
 *		diropargs3  what;
 *	};
 */
static void nfs3_xdr_enc_lookup3args(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const struct nfs3_diropargs *args = data;

	encode_diropargs3(xdr, args->fh, args->name, args->len);
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

static void nfs3_xdr_enc_access3args(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const struct nfs3_accessargs *args = data;

	encode_access3args(xdr, args);
}

/*
 * 3.3.5  READLINK3args
 *
 *	struct READLINK3args {
 *		nfs_fh3	symlink;
 *	};
 */
static void nfs3_xdr_enc_readlink3args(struct rpc_rqst *req,
				       struct xdr_stream *xdr,
				       const void *data)
{
	const struct nfs3_readlinkargs *args = data;

	encode_nfs_fh3(xdr, args->fh);
	prepare_reply_buffer(req, args->pages, args->pgbase,
					args->pglen, NFS3_readlinkres_sz);
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
			     const struct nfs_pgio_args *args)
{
	__be32 *p;

	encode_nfs_fh3(xdr, args->fh);

	p = xdr_reserve_space(xdr, 8 + 4);
	p = xdr_encode_hyper(p, args->offset);
	*p = cpu_to_be32(args->count);
}

static void nfs3_xdr_enc_read3args(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   const void *data)
{
	const struct nfs_pgio_args *args = data;

	encode_read3args(xdr, args);
	prepare_reply_buffer(req, args->pages, args->pgbase,
					args->count, NFS3_readres_sz);
	req->rq_rcv_buf.flags |= XDRBUF_READ;
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
			      const struct nfs_pgio_args *args)
{
	__be32 *p;

	encode_nfs_fh3(xdr, args->fh);

	p = xdr_reserve_space(xdr, 8 + 4 + 4 + 4);
	p = xdr_encode_hyper(p, args->offset);
	*p++ = cpu_to_be32(args->count);
	*p++ = cpu_to_be32(args->stable);
	*p = cpu_to_be32(args->count);
	xdr_write_pages(xdr, args->pages, args->pgbase, args->count);
}

static void nfs3_xdr_enc_write3args(struct rpc_rqst *req,
				    struct xdr_stream *xdr,
				    const void *data)
{
	const struct nfs_pgio_args *args = data;

	encode_write3args(xdr, args);
	xdr->buf->flags |= XDRBUF_WRITE;
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

static void nfs3_xdr_enc_create3args(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const struct nfs3_createargs *args = data;

	encode_diropargs3(xdr, args->fh, args->name, args->len);
	encode_createhow3(xdr, args);
}

/*
 * 3.3.9  MKDIR3args
 *
 *	struct MKDIR3args {
 *		diropargs3	where;
 *		sattr3		attributes;
 *	};
 */
static void nfs3_xdr_enc_mkdir3args(struct rpc_rqst *req,
				    struct xdr_stream *xdr,
				    const void *data)
{
	const struct nfs3_mkdirargs *args = data;

	encode_diropargs3(xdr, args->fh, args->name, args->len);
	encode_sattr3(xdr, args->sattr);
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
				const void *data)
{
	const struct nfs3_symlinkargs *args = data;

	encode_sattr3(xdr, args->sattr);
	encode_nfspath3(xdr, args->pages, args->pathlen);
}

static void nfs3_xdr_enc_symlink3args(struct rpc_rqst *req,
				      struct xdr_stream *xdr,
				      const void *data)
{
	const struct nfs3_symlinkargs *args = data;

	encode_diropargs3(xdr, args->fromfh, args->fromname, args->fromlen);
	encode_symlinkdata3(xdr, args);
	xdr->buf->flags |= XDRBUF_WRITE;
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

static void nfs3_xdr_enc_mknod3args(struct rpc_rqst *req,
				    struct xdr_stream *xdr,
				    const void *data)
{
	const struct nfs3_mknodargs *args = data;

	encode_diropargs3(xdr, args->fh, args->name, args->len);
	encode_mknoddata3(xdr, args);
}

/*
 * 3.3.12  REMOVE3args
 *
 *	struct REMOVE3args {
 *		diropargs3  object;
 *	};
 */
static void nfs3_xdr_enc_remove3args(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const struct nfs_removeargs *args = data;

	encode_diropargs3(xdr, args->fh, args->name.name, args->name.len);
}

/*
 * 3.3.14  RENAME3args
 *
 *	struct RENAME3args {
 *		diropargs3	from;
 *		diropargs3	to;
 *	};
 */
static void nfs3_xdr_enc_rename3args(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const struct nfs_renameargs *args = data;
	const struct qstr *old = args->old_name;
	const struct qstr *new = args->new_name;

	encode_diropargs3(xdr, args->old_dir, old->name, old->len);
	encode_diropargs3(xdr, args->new_dir, new->name, new->len);
}

/*
 * 3.3.15  LINK3args
 *
 *	struct LINK3args {
 *		nfs_fh3		file;
 *		diropargs3	link;
 *	};
 */
static void nfs3_xdr_enc_link3args(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   const void *data)
{
	const struct nfs3_linkargs *args = data;

	encode_nfs_fh3(xdr, args->fromfh);
	encode_diropargs3(xdr, args->tofh, args->toname, args->tolen);
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

static void nfs3_xdr_enc_readdir3args(struct rpc_rqst *req,
				      struct xdr_stream *xdr,
				      const void *data)
{
	const struct nfs3_readdirargs *args = data;

	encode_readdir3args(xdr, args);
	prepare_reply_buffer(req, args->pages, 0,
				args->count, NFS3_readdirres_sz);
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

static void nfs3_xdr_enc_readdirplus3args(struct rpc_rqst *req,
					  struct xdr_stream *xdr,
					  const void *data)
{
	const struct nfs3_readdirargs *args = data;

	encode_readdirplus3args(xdr, args);
	prepare_reply_buffer(req, args->pages, 0,
				args->count, NFS3_readdirres_sz);
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
			       const struct nfs_commitargs *args)
{
	__be32 *p;

	encode_nfs_fh3(xdr, args->fh);

	p = xdr_reserve_space(xdr, 8 + 4);
	p = xdr_encode_hyper(p, args->offset);
	*p = cpu_to_be32(args->count);
}

static void nfs3_xdr_enc_commit3args(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const struct nfs_commitargs *args = data;

	encode_commit3args(xdr, args);
}

#ifdef CONFIG_NFS_V3_ACL

static void nfs3_xdr_enc_getacl3args(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const struct nfs3_getaclargs *args = data;

	encode_nfs_fh3(xdr, args->fh);
	encode_uint32(xdr, args->mask);
	if (args->mask & (NFS_ACL | NFS_DFACL))
		prepare_reply_buffer(req, args->pages, 0,
					NFSACL_MAXPAGES << PAGE_SHIFT,
					ACL3_getaclres_sz);
}

static void nfs3_xdr_enc_setacl3args(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const struct nfs3_setaclargs *args = data;
	unsigned int base;
	int error;

	encode_nfs_fh3(xdr, NFS_FH(args->inode));
	encode_uint32(xdr, args->mask);

	base = req->rq_slen;
	if (args->npages != 0)
		xdr_write_pages(xdr, args->pages, 0, args->len);
	else
		xdr_reserve_space(xdr, args->len);

	error = nfsacl_encode(xdr->buf, base, args->inode,
			    (args->mask & NFS_ACL) ?
			    args->acl_access : NULL, 1, 0);
	/* FIXME: this is just broken */
	BUG_ON(error < 0);
	error = nfsacl_encode(xdr->buf, base + error, args->inode,
			    (args->mask & NFS_DFACL) ?
			    args->acl_default : NULL, 1,
			    NFS_ACL_DEFAULT);
	BUG_ON(error < 0);
}

#endif  /* CONFIG_NFS_V3_ACL */

/*
 * NFSv3 XDR decode functions
 *
 * NFSv3 result types are defined in section 3.3 of RFC 1813:
 * "NFS Version 3 Protocol Specification".
 */

/*
 * 3.3.1  GETATTR3res
 *
 *	struct GETATTR3resok {
 *		fattr3		obj_attributes;
 *	};
 *
 *	union GETATTR3res switch (nfsstat3 status) {
 *	case NFS3_OK:
 *		GETATTR3resok  resok;
 *	default:
 *		void;
 *	};
 */
static int nfs3_xdr_dec_getattr3res(struct rpc_rqst *req,
				    struct xdr_stream *xdr,
				    void *result)
{
	enum nfs_stat status;
	int error;

	error = decode_nfsstat3(xdr, &status);
	if (unlikely(error))
		goto out;
	if (status != NFS3_OK)
		goto out_default;
	error = decode_fattr3(xdr, result);
out:
	return error;
out_default:
	return nfs3_stat_to_errno(status);
}

/*
 * 3.3.2  SETATTR3res
 *
 *	struct SETATTR3resok {
 *		wcc_data  obj_wcc;
 *	};
 *
 *	struct SETATTR3resfail {
 *		wcc_data  obj_wcc;
 *	};
 *
 *	union SETATTR3res switch (nfsstat3 status) {
 *	case NFS3_OK:
 *		SETATTR3resok   resok;
 *	default:
 *		SETATTR3resfail resfail;
 *	};
 */
static int nfs3_xdr_dec_setattr3res(struct rpc_rqst *req,
				    struct xdr_stream *xdr,
				    void *result)
{
	enum nfs_stat status;
	int error;

	error = decode_nfsstat3(xdr, &status);
	if (unlikely(error))
		goto out;
	error = decode_wcc_data(xdr, result);
	if (unlikely(error))
		goto out;
	if (status != NFS3_OK)
		goto out_status;
out:
	return error;
out_status:
	return nfs3_stat_to_errno(status);
}

/*
 * 3.3.3  LOOKUP3res
 *
 *	struct LOOKUP3resok {
 *		nfs_fh3		object;
 *		post_op_attr	obj_attributes;
 *		post_op_attr	dir_attributes;
 *	};
 *
 *	struct LOOKUP3resfail {
 *		post_op_attr	dir_attributes;
 *	};
 *
 *	union LOOKUP3res switch (nfsstat3 status) {
 *	case NFS3_OK:
 *		LOOKUP3resok	resok;
 *	default:
 *		LOOKUP3resfail	resfail;
 *	};
 */
static int nfs3_xdr_dec_lookup3res(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   void *data)
{
	struct nfs3_diropres *result = data;
	enum nfs_stat status;
	int error;

	error = decode_nfsstat3(xdr, &status);
	if (unlikely(error))
		goto out;
	if (status != NFS3_OK)
		goto out_default;
	error = decode_nfs_fh3(xdr, result->fh);
	if (unlikely(error))
		goto out;
	error = decode_post_op_attr(xdr, result->fattr);
	if (unlikely(error))
		goto out;
	error = decode_post_op_attr(xdr, result->dir_attr);
out:
	return error;
out_default:
	error = decode_post_op_attr(xdr, result->dir_attr);
	if (unlikely(error))
		goto out;
	return nfs3_stat_to_errno(status);
}

/*
 * 3.3.4  ACCESS3res
 *
 *	struct ACCESS3resok {
 *		post_op_attr	obj_attributes;
 *		uint32		access;
 *	};
 *
 *	struct ACCESS3resfail {
 *		post_op_attr	obj_attributes;
 *	};
 *
 *	union ACCESS3res switch (nfsstat3 status) {
 *	case NFS3_OK:
 *		ACCESS3resok	resok;
 *	default:
 *		ACCESS3resfail	resfail;
 *	};
 */
static int nfs3_xdr_dec_access3res(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   void *data)
{
	struct nfs3_accessres *result = data;
	enum nfs_stat status;
	int error;

	error = decode_nfsstat3(xdr, &status);
	if (unlikely(error))
		goto out;
	error = decode_post_op_attr(xdr, result->fattr);
	if (unlikely(error))
		goto out;
	if (status != NFS3_OK)
		goto out_default;
	error = decode_uint32(xdr, &result->access);
out:
	return error;
out_default:
	return nfs3_stat_to_errno(status);
}

/*
 * 3.3.5  READLINK3res
 *
 *	struct READLINK3resok {
 *		post_op_attr	symlink_attributes;
 *		nfspath3	data;
 *	};
 *
 *	struct READLINK3resfail {
 *		post_op_attr	symlink_attributes;
 *	};
 *
 *	union READLINK3res switch (nfsstat3 status) {
 *	case NFS3_OK:
 *		READLINK3resok	resok;
 *	default:
 *		READLINK3resfail resfail;
 *	};
 */
static int nfs3_xdr_dec_readlink3res(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     void *result)
{
	enum nfs_stat status;
	int error;

	error = decode_nfsstat3(xdr, &status);
	if (unlikely(error))
		goto out;
	error = decode_post_op_attr(xdr, result);
	if (unlikely(error))
		goto out;
	if (status != NFS3_OK)
		goto out_default;
	error = decode_nfspath3(xdr);
out:
	return error;
out_default:
	return nfs3_stat_to_errno(status);
}

/*
 * 3.3.6  READ3res
 *
 *	struct READ3resok {
 *		post_op_attr	file_attributes;
 *		count3		count;
 *		bool		eof;
 *		opaque		data<>;
 *	};
 *
 *	struct READ3resfail {
 *		post_op_attr	file_attributes;
 *	};
 *
 *	union READ3res switch (nfsstat3 status) {
 *	case NFS3_OK:
 *		READ3resok	resok;
 *	default:
 *		READ3resfail	resfail;
 *	};
 */
static int decode_read3resok(struct xdr_stream *xdr,
			     struct nfs_pgio_res *result)
{
	u32 eof, count, ocount, recvd;
	__be32 *p;

	p = xdr_inline_decode(xdr, 4 + 4 + 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	count = be32_to_cpup(p++);
	eof = be32_to_cpup(p++);
	ocount = be32_to_cpup(p++);
	if (unlikely(ocount != count))
		goto out_mismatch;
	recvd = xdr_read_pages(xdr, count);
	if (unlikely(count > recvd))
		goto out_cheating;
out:
	result->eof = eof;
	result->count = count;
	return count;
out_mismatch:
	dprintk("NFS: READ count doesn't match length of opaque: "
		"count %u != ocount %u\n", count, ocount);
	return -EIO;
out_cheating:
	dprintk("NFS: server cheating in read result: "
		"count %u > recvd %u\n", count, recvd);
	count = recvd;
	eof = 0;
	goto out;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int nfs3_xdr_dec_read3res(struct rpc_rqst *req, struct xdr_stream *xdr,
				 void *data)
{
	struct nfs_pgio_res *result = data;
	enum nfs_stat status;
	int error;

	error = decode_nfsstat3(xdr, &status);
	if (unlikely(error))
		goto out;
	error = decode_post_op_attr(xdr, result->fattr);
	if (unlikely(error))
		goto out;
	result->op_status = status;
	if (status != NFS3_OK)
		goto out_status;
	error = decode_read3resok(xdr, result);
out:
	return error;
out_status:
	return nfs3_stat_to_errno(status);
}

/*
 * 3.3.7  WRITE3res
 *
 *	enum stable_how {
 *		UNSTABLE  = 0,
 *		DATA_SYNC = 1,
 *		FILE_SYNC = 2
 *	};
 *
 *	struct WRITE3resok {
 *		wcc_data	file_wcc;
 *		count3		count;
 *		stable_how	committed;
 *		writeverf3	verf;
 *	};
 *
 *	struct WRITE3resfail {
 *		wcc_data	file_wcc;
 *	};
 *
 *	union WRITE3res switch (nfsstat3 status) {
 *	case NFS3_OK:
 *		WRITE3resok	resok;
 *	default:
 *		WRITE3resfail	resfail;
 *	};
 */
static int decode_write3resok(struct xdr_stream *xdr,
			      struct nfs_pgio_res *result)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 4 + 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	result->count = be32_to_cpup(p++);
	result->verf->committed = be32_to_cpup(p++);
	if (unlikely(result->verf->committed > NFS_FILE_SYNC))
		goto out_badvalue;
	if (decode_writeverf3(xdr, &result->verf->verifier))
		goto out_eio;
	return result->count;
out_badvalue:
	dprintk("NFS: bad stable_how value: %u\n", result->verf->committed);
	return -EIO;
out_overflow:
	print_overflow_msg(__func__, xdr);
out_eio:
	return -EIO;
}

static int nfs3_xdr_dec_write3res(struct rpc_rqst *req, struct xdr_stream *xdr,
				  void *data)
{
	struct nfs_pgio_res *result = data;
	enum nfs_stat status;
	int error;

	error = decode_nfsstat3(xdr, &status);
	if (unlikely(error))
		goto out;
	error = decode_wcc_data(xdr, result->fattr);
	if (unlikely(error))
		goto out;
	result->op_status = status;
	if (status != NFS3_OK)
		goto out_status;
	error = decode_write3resok(xdr, result);
out:
	return error;
out_status:
	return nfs3_stat_to_errno(status);
}

/*
 * 3.3.8  CREATE3res
 *
 *	struct CREATE3resok {
 *		post_op_fh3	obj;
 *		post_op_attr	obj_attributes;
 *		wcc_data	dir_wcc;
 *	};
 *
 *	struct CREATE3resfail {
 *		wcc_data	dir_wcc;
 *	};
 *
 *	union CREATE3res switch (nfsstat3 status) {
 *	case NFS3_OK:
 *		CREATE3resok	resok;
 *	default:
 *		CREATE3resfail	resfail;
 *	};
 */
static int decode_create3resok(struct xdr_stream *xdr,
			       struct nfs3_diropres *result)
{
	int error;

	error = decode_post_op_fh3(xdr, result->fh);
	if (unlikely(error))
		goto out;
	error = decode_post_op_attr(xdr, result->fattr);
	if (unlikely(error))
		goto out;
	/* The server isn't required to return a file handle.
	 * If it didn't, force the client to perform a LOOKUP
	 * to determine the correct file handle and attribute
	 * values for the new object. */
	if (result->fh->size == 0)
		result->fattr->valid = 0;
	error = decode_wcc_data(xdr, result->dir_attr);
out:
	return error;
}

static int nfs3_xdr_dec_create3res(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   void *data)
{
	struct nfs3_diropres *result = data;
	enum nfs_stat status;
	int error;

	error = decode_nfsstat3(xdr, &status);
	if (unlikely(error))
		goto out;
	if (status != NFS3_OK)
		goto out_default;
	error = decode_create3resok(xdr, result);
out:
	return error;
out_default:
	error = decode_wcc_data(xdr, result->dir_attr);
	if (unlikely(error))
		goto out;
	return nfs3_stat_to_errno(status);
}

/*
 * 3.3.12  REMOVE3res
 *
 *	struct REMOVE3resok {
 *		wcc_data    dir_wcc;
 *	};
 *
 *	struct REMOVE3resfail {
 *		wcc_data    dir_wcc;
 *	};
 *
 *	union REMOVE3res switch (nfsstat3 status) {
 *	case NFS3_OK:
 *		REMOVE3resok   resok;
 *	default:
 *		REMOVE3resfail resfail;
 *	};
 */
static int nfs3_xdr_dec_remove3res(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   void *data)
{
	struct nfs_removeres *result = data;
	enum nfs_stat status;
	int error;

	error = decode_nfsstat3(xdr, &status);
	if (unlikely(error))
		goto out;
	error = decode_wcc_data(xdr, result->dir_attr);
	if (unlikely(error))
		goto out;
	if (status != NFS3_OK)
		goto out_status;
out:
	return error;
out_status:
	return nfs3_stat_to_errno(status);
}

/*
 * 3.3.14  RENAME3res
 *
 *	struct RENAME3resok {
 *		wcc_data	fromdir_wcc;
 *		wcc_data	todir_wcc;
 *	};
 *
 *	struct RENAME3resfail {
 *		wcc_data	fromdir_wcc;
 *		wcc_data	todir_wcc;
 *	};
 *
 *	union RENAME3res switch (nfsstat3 status) {
 *	case NFS3_OK:
 *		RENAME3resok   resok;
 *	default:
 *		RENAME3resfail resfail;
 *	};
 */
static int nfs3_xdr_dec_rename3res(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   void *data)
{
	struct nfs_renameres *result = data;
	enum nfs_stat status;
	int error;

	error = decode_nfsstat3(xdr, &status);
	if (unlikely(error))
		goto out;
	error = decode_wcc_data(xdr, result->old_fattr);
	if (unlikely(error))
		goto out;
	error = decode_wcc_data(xdr, result->new_fattr);
	if (unlikely(error))
		goto out;
	if (status != NFS3_OK)
		goto out_status;
out:
	return error;
out_status:
	return nfs3_stat_to_errno(status);
}

/*
 * 3.3.15  LINK3res
 *
 *	struct LINK3resok {
 *		post_op_attr	file_attributes;
 *		wcc_data	linkdir_wcc;
 *	};
 *
 *	struct LINK3resfail {
 *		post_op_attr	file_attributes;
 *		wcc_data	linkdir_wcc;
 *	};
 *
 *	union LINK3res switch (nfsstat3 status) {
 *	case NFS3_OK:
 *		LINK3resok	resok;
 *	default:
 *		LINK3resfail	resfail;
 *	};
 */
static int nfs3_xdr_dec_link3res(struct rpc_rqst *req, struct xdr_stream *xdr,
				 void *data)
{
	struct nfs3_linkres *result = data;
	enum nfs_stat status;
	int error;

	error = decode_nfsstat3(xdr, &status);
	if (unlikely(error))
		goto out;
	error = decode_post_op_attr(xdr, result->fattr);
	if (unlikely(error))
		goto out;
	error = decode_wcc_data(xdr, result->dir_attr);
	if (unlikely(error))
		goto out;
	if (status != NFS3_OK)
		goto out_status;
out:
	return error;
out_status:
	return nfs3_stat_to_errno(status);
}

/**
 * nfs3_decode_dirent - Decode a single NFSv3 directory entry stored in
 *			the local page cache
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
 * 3.3.16  entry3
 *
 *	struct entry3 {
 *		fileid3		fileid;
 *		filename3	name;
 *		cookie3		cookie;
 *		fhandle3	filehandle;
 *		post_op_attr3	attributes;
 *		entry3		*nextentry;
 *	};
 *
 * 3.3.17  entryplus3
 *	struct entryplus3 {
 *		fileid3		fileid;
 *		filename3	name;
 *		cookie3		cookie;
 *		post_op_attr	name_attributes;
 *		post_op_fh3	name_handle;
 *		entryplus3	*nextentry;
 *	};
 */
int nfs3_decode_dirent(struct xdr_stream *xdr, struct nfs_entry *entry,
		       int plus)
{
	struct nfs_entry old = *entry;
	__be32 *p;
	int error;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	if (*p == xdr_zero) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(p == NULL))
			goto out_overflow;
		if (*p == xdr_zero)
			return -EAGAIN;
		entry->eof = 1;
		return -EBADCOOKIE;
	}

	error = decode_fileid3(xdr, &entry->ino);
	if (unlikely(error))
		return error;

	error = decode_inline_filename3(xdr, &entry->name, &entry->len);
	if (unlikely(error))
		return error;

	entry->prev_cookie = entry->cookie;
	error = decode_cookie3(xdr, &entry->cookie);
	if (unlikely(error))
		return error;

	entry->d_type = DT_UNKNOWN;

	if (plus) {
		entry->fattr->valid = 0;
		error = decode_post_op_attr(xdr, entry->fattr);
		if (unlikely(error))
			return error;
		if (entry->fattr->valid & NFS_ATTR_FATTR_V3)
			entry->d_type = nfs_umode_to_dtype(entry->fattr->mode);

		if (entry->fattr->fileid != entry->ino) {
			entry->fattr->mounted_on_fileid = entry->ino;
			entry->fattr->valid |= NFS_ATTR_FATTR_MOUNTED_ON_FILEID;
		}

		/* In fact, a post_op_fh3: */
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(p == NULL))
			goto out_overflow;
		if (*p != xdr_zero) {
			error = decode_nfs_fh3(xdr, entry->fh);
			if (unlikely(error)) {
				if (error == -E2BIG)
					goto out_truncated;
				return error;
			}
		} else
			zero_nfs_fh3(entry->fh);
	}

	return 0;

out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EAGAIN;
out_truncated:
	dprintk("NFS: directory entry contains invalid file handle\n");
	*entry = old;
	return -EAGAIN;
}

/*
 * 3.3.16  READDIR3res
 *
 *	struct dirlist3 {
 *		entry3		*entries;
 *		bool		eof;
 *	};
 *
 *	struct READDIR3resok {
 *		post_op_attr	dir_attributes;
 *		cookieverf3	cookieverf;
 *		dirlist3	reply;
 *	};
 *
 *	struct READDIR3resfail {
 *		post_op_attr	dir_attributes;
 *	};
 *
 *	union READDIR3res switch (nfsstat3 status) {
 *	case NFS3_OK:
 *		READDIR3resok	resok;
 *	default:
 *		READDIR3resfail	resfail;
 *	};
 *
 * Read the directory contents into the page cache, but otherwise
 * don't touch them.  The actual decoding is done by nfs3_decode_entry()
 * during subsequent nfs_readdir() calls.
 */
static int decode_dirlist3(struct xdr_stream *xdr)
{
	return xdr_read_pages(xdr, xdr->buf->page_len);
}

static int decode_readdir3resok(struct xdr_stream *xdr,
				struct nfs3_readdirres *result)
{
	int error;

	error = decode_post_op_attr(xdr, result->dir_attr);
	if (unlikely(error))
		goto out;
	/* XXX: do we need to check if result->verf != NULL ? */
	error = decode_cookieverf3(xdr, result->verf);
	if (unlikely(error))
		goto out;
	error = decode_dirlist3(xdr);
out:
	return error;
}

static int nfs3_xdr_dec_readdir3res(struct rpc_rqst *req,
				    struct xdr_stream *xdr,
				    void *data)
{
	struct nfs3_readdirres *result = data;
	enum nfs_stat status;
	int error;

	error = decode_nfsstat3(xdr, &status);
	if (unlikely(error))
		goto out;
	if (status != NFS3_OK)
		goto out_default;
	error = decode_readdir3resok(xdr, result);
out:
	return error;
out_default:
	error = decode_post_op_attr(xdr, result->dir_attr);
	if (unlikely(error))
		goto out;
	return nfs3_stat_to_errno(status);
}

/*
 * 3.3.18  FSSTAT3res
 *
 *	struct FSSTAT3resok {
 *		post_op_attr	obj_attributes;
 *		size3		tbytes;
 *		size3		fbytes;
 *		size3		abytes;
 *		size3		tfiles;
 *		size3		ffiles;
 *		size3		afiles;
 *		uint32		invarsec;
 *	};
 *
 *	struct FSSTAT3resfail {
 *		post_op_attr	obj_attributes;
 *	};
 *
 *	union FSSTAT3res switch (nfsstat3 status) {
 *	case NFS3_OK:
 *		FSSTAT3resok	resok;
 *	default:
 *		FSSTAT3resfail	resfail;
 *	};
 */
static int decode_fsstat3resok(struct xdr_stream *xdr,
			       struct nfs_fsstat *result)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 8 * 6 + 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	p = xdr_decode_size3(p, &result->tbytes);
	p = xdr_decode_size3(p, &result->fbytes);
	p = xdr_decode_size3(p, &result->abytes);
	p = xdr_decode_size3(p, &result->tfiles);
	p = xdr_decode_size3(p, &result->ffiles);
	xdr_decode_size3(p, &result->afiles);
	/* ignore invarsec */
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int nfs3_xdr_dec_fsstat3res(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   void *data)
{
	struct nfs_fsstat *result = data;
	enum nfs_stat status;
	int error;

	error = decode_nfsstat3(xdr, &status);
	if (unlikely(error))
		goto out;
	error = decode_post_op_attr(xdr, result->fattr);
	if (unlikely(error))
		goto out;
	if (status != NFS3_OK)
		goto out_status;
	error = decode_fsstat3resok(xdr, result);
out:
	return error;
out_status:
	return nfs3_stat_to_errno(status);
}

/*
 * 3.3.19  FSINFO3res
 *
 *	struct FSINFO3resok {
 *		post_op_attr	obj_attributes;
 *		uint32		rtmax;
 *		uint32		rtpref;
 *		uint32		rtmult;
 *		uint32		wtmax;
 *		uint32		wtpref;
 *		uint32		wtmult;
 *		uint32		dtpref;
 *		size3		maxfilesize;
 *		nfstime3	time_delta;
 *		uint32		properties;
 *	};
 *
 *	struct FSINFO3resfail {
 *		post_op_attr	obj_attributes;
 *	};
 *
 *	union FSINFO3res switch (nfsstat3 status) {
 *	case NFS3_OK:
 *		FSINFO3resok	resok;
 *	default:
 *		FSINFO3resfail	resfail;
 *	};
 */
static int decode_fsinfo3resok(struct xdr_stream *xdr,
			       struct nfs_fsinfo *result)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 4 * 7 + 8 + 8 + 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	result->rtmax  = be32_to_cpup(p++);
	result->rtpref = be32_to_cpup(p++);
	result->rtmult = be32_to_cpup(p++);
	result->wtmax  = be32_to_cpup(p++);
	result->wtpref = be32_to_cpup(p++);
	result->wtmult = be32_to_cpup(p++);
	result->dtpref = be32_to_cpup(p++);
	p = xdr_decode_size3(p, &result->maxfilesize);
	xdr_decode_nfstime3(p, &result->time_delta);

	/* ignore properties */
	result->lease_time = 0;
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int nfs3_xdr_dec_fsinfo3res(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   void *data)
{
	struct nfs_fsinfo *result = data;
	enum nfs_stat status;
	int error;

	error = decode_nfsstat3(xdr, &status);
	if (unlikely(error))
		goto out;
	error = decode_post_op_attr(xdr, result->fattr);
	if (unlikely(error))
		goto out;
	if (status != NFS3_OK)
		goto out_status;
	error = decode_fsinfo3resok(xdr, result);
out:
	return error;
out_status:
	return nfs3_stat_to_errno(status);
}

/*
 * 3.3.20  PATHCONF3res
 *
 *	struct PATHCONF3resok {
 *		post_op_attr	obj_attributes;
 *		uint32		linkmax;
 *		uint32		name_max;
 *		bool		no_trunc;
 *		bool		chown_restricted;
 *		bool		case_insensitive;
 *		bool		case_preserving;
 *	};
 *
 *	struct PATHCONF3resfail {
 *		post_op_attr	obj_attributes;
 *	};
 *
 *	union PATHCONF3res switch (nfsstat3 status) {
 *	case NFS3_OK:
 *		PATHCONF3resok	resok;
 *	default:
 *		PATHCONF3resfail resfail;
 *	};
 */
static int decode_pathconf3resok(struct xdr_stream *xdr,
				 struct nfs_pathconf *result)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 4 * 6);
	if (unlikely(p == NULL))
		goto out_overflow;
	result->max_link = be32_to_cpup(p++);
	result->max_namelen = be32_to_cpup(p);
	/* ignore remaining fields */
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int nfs3_xdr_dec_pathconf3res(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     void *data)
{
	struct nfs_pathconf *result = data;
	enum nfs_stat status;
	int error;

	error = decode_nfsstat3(xdr, &status);
	if (unlikely(error))
		goto out;
	error = decode_post_op_attr(xdr, result->fattr);
	if (unlikely(error))
		goto out;
	if (status != NFS3_OK)
		goto out_status;
	error = decode_pathconf3resok(xdr, result);
out:
	return error;
out_status:
	return nfs3_stat_to_errno(status);
}

/*
 * 3.3.21  COMMIT3res
 *
 *	struct COMMIT3resok {
 *		wcc_data	file_wcc;
 *		writeverf3	verf;
 *	};
 *
 *	struct COMMIT3resfail {
 *		wcc_data	file_wcc;
 *	};
 *
 *	union COMMIT3res switch (nfsstat3 status) {
 *	case NFS3_OK:
 *		COMMIT3resok	resok;
 *	default:
 *		COMMIT3resfail	resfail;
 *	};
 */
static int nfs3_xdr_dec_commit3res(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   void *data)
{
	struct nfs_commitres *result = data;
	enum nfs_stat status;
	int error;

	error = decode_nfsstat3(xdr, &status);
	if (unlikely(error))
		goto out;
	error = decode_wcc_data(xdr, result->fattr);
	if (unlikely(error))
		goto out;
	result->op_status = status;
	if (status != NFS3_OK)
		goto out_status;
	error = decode_writeverf3(xdr, &result->verf->verifier);
out:
	return error;
out_status:
	return nfs3_stat_to_errno(status);
}

#ifdef CONFIG_NFS_V3_ACL

static inline int decode_getacl3resok(struct xdr_stream *xdr,
				      struct nfs3_getaclres *result)
{
	struct posix_acl **acl;
	unsigned int *aclcnt;
	size_t hdrlen;
	int error;

	error = decode_post_op_attr(xdr, result->fattr);
	if (unlikely(error))
		goto out;
	error = decode_uint32(xdr, &result->mask);
	if (unlikely(error))
		goto out;
	error = -EINVAL;
	if (result->mask & ~(NFS_ACL|NFS_ACLCNT|NFS_DFACL|NFS_DFACLCNT))
		goto out;

	hdrlen = xdr_stream_pos(xdr);

	acl = NULL;
	if (result->mask & NFS_ACL)
		acl = &result->acl_access;
	aclcnt = NULL;
	if (result->mask & NFS_ACLCNT)
		aclcnt = &result->acl_access_count;
	error = nfsacl_decode(xdr->buf, hdrlen, aclcnt, acl);
	if (unlikely(error <= 0))
		goto out;

	acl = NULL;
	if (result->mask & NFS_DFACL)
		acl = &result->acl_default;
	aclcnt = NULL;
	if (result->mask & NFS_DFACLCNT)
		aclcnt = &result->acl_default_count;
	error = nfsacl_decode(xdr->buf, hdrlen + error, aclcnt, acl);
	if (unlikely(error <= 0))
		return error;
	error = 0;
out:
	return error;
}

static int nfs3_xdr_dec_getacl3res(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   void *result)
{
	enum nfs_stat status;
	int error;

	error = decode_nfsstat3(xdr, &status);
	if (unlikely(error))
		goto out;
	if (status != NFS3_OK)
		goto out_default;
	error = decode_getacl3resok(xdr, result);
out:
	return error;
out_default:
	return nfs3_stat_to_errno(status);
}

static int nfs3_xdr_dec_setacl3res(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   void *result)
{
	enum nfs_stat status;
	int error;

	error = decode_nfsstat3(xdr, &status);
	if (unlikely(error))
		goto out;
	if (status != NFS3_OK)
		goto out_default;
	error = decode_post_op_attr(xdr, result);
out:
	return error;
out_default:
	return nfs3_stat_to_errno(status);
}

#endif  /* CONFIG_NFS_V3_ACL */


/*
 * We need to translate between nfs status return values and
 * the local errno values which may not be the same.
 */
static const struct {
	int stat;
	int errno;
} nfs_errtbl[] = {
	{ NFS_OK,		0		},
	{ NFSERR_PERM,		-EPERM		},
	{ NFSERR_NOENT,		-ENOENT		},
	{ NFSERR_IO,		-errno_NFSERR_IO},
	{ NFSERR_NXIO,		-ENXIO		},
/*	{ NFSERR_EAGAIN,	-EAGAIN		}, */
	{ NFSERR_ACCES,		-EACCES		},
	{ NFSERR_EXIST,		-EEXIST		},
	{ NFSERR_XDEV,		-EXDEV		},
	{ NFSERR_NODEV,		-ENODEV		},
	{ NFSERR_NOTDIR,	-ENOTDIR	},
	{ NFSERR_ISDIR,		-EISDIR		},
	{ NFSERR_INVAL,		-EINVAL		},
	{ NFSERR_FBIG,		-EFBIG		},
	{ NFSERR_NOSPC,		-ENOSPC		},
	{ NFSERR_ROFS,		-EROFS		},
	{ NFSERR_MLINK,		-EMLINK		},
	{ NFSERR_NAMETOOLONG,	-ENAMETOOLONG	},
	{ NFSERR_NOTEMPTY,	-ENOTEMPTY	},
	{ NFSERR_DQUOT,		-EDQUOT		},
	{ NFSERR_STALE,		-ESTALE		},
	{ NFSERR_REMOTE,	-EREMOTE	},
#ifdef EWFLUSH
	{ NFSERR_WFLUSH,	-EWFLUSH	},
#endif
	{ NFSERR_BADHANDLE,	-EBADHANDLE	},
	{ NFSERR_NOT_SYNC,	-ENOTSYNC	},
	{ NFSERR_BAD_COOKIE,	-EBADCOOKIE	},
	{ NFSERR_NOTSUPP,	-ENOTSUPP	},
	{ NFSERR_TOOSMALL,	-ETOOSMALL	},
	{ NFSERR_SERVERFAULT,	-EREMOTEIO	},
	{ NFSERR_BADTYPE,	-EBADTYPE	},
	{ NFSERR_JUKEBOX,	-EJUKEBOX	},
	{ -1,			-EIO		}
};

/**
 * nfs3_stat_to_errno - convert an NFS status code to a local errno
 * @status: NFS status code to convert
 *
 * Returns a local errno value, or -EIO if the NFS status code is
 * not recognized.  This function is used jointly by NFSv2 and NFSv3.
 */
static int nfs3_stat_to_errno(enum nfs_stat status)
{
	int i;

	for (i = 0; nfs_errtbl[i].stat != -1; i++) {
		if (nfs_errtbl[i].stat == (int)status)
			return nfs_errtbl[i].errno;
	}
	dprintk("NFS: Unrecognized nfs status value: %u\n", status);
	return nfs_errtbl[i].errno;
}


#define PROC(proc, argtype, restype, timer)				\
[NFS3PROC_##proc] = {							\
	.p_proc      = NFS3PROC_##proc,					\
	.p_encode    = nfs3_xdr_enc_##argtype##3args,			\
	.p_decode    = nfs3_xdr_dec_##restype##3res,			\
	.p_arglen    = NFS3_##argtype##args_sz,				\
	.p_replen    = NFS3_##restype##res_sz,				\
	.p_timer     = timer,						\
	.p_statidx   = NFS3PROC_##proc,					\
	.p_name      = #proc,						\
	}

struct rpc_procinfo	nfs3_procedures[] = {
	PROC(GETATTR,		getattr,	getattr,	1),
	PROC(SETATTR,		setattr,	setattr,	0),
	PROC(LOOKUP,		lookup,		lookup,		2),
	PROC(ACCESS,		access,		access,		1),
	PROC(READLINK,		readlink,	readlink,	3),
	PROC(READ,		read,		read,		3),
	PROC(WRITE,		write,		write,		4),
	PROC(CREATE,		create,		create,		0),
	PROC(MKDIR,		mkdir,		create,		0),
	PROC(SYMLINK,		symlink,	create,		0),
	PROC(MKNOD,		mknod,		create,		0),
	PROC(REMOVE,		remove,		remove,		0),
	PROC(RMDIR,		lookup,		setattr,	0),
	PROC(RENAME,		rename,		rename,		0),
	PROC(LINK,		link,		link,		0),
	PROC(READDIR,		readdir,	readdir,	3),
	PROC(READDIRPLUS,	readdirplus,	readdir,	3),
	PROC(FSSTAT,		getattr,	fsstat,		0),
	PROC(FSINFO,		getattr,	fsinfo,		0),
	PROC(PATHCONF,		getattr,	pathconf,	0),
	PROC(COMMIT,		commit,		commit,		5),
};

static unsigned int nfs_version3_counts[ARRAY_SIZE(nfs3_procedures)];
const struct rpc_version nfs_version3 = {
	.number			= 3,
	.nrprocs		= ARRAY_SIZE(nfs3_procedures),
	.procs			= nfs3_procedures,
	.counts			= nfs_version3_counts,
};

#ifdef CONFIG_NFS_V3_ACL
static struct rpc_procinfo	nfs3_acl_procedures[] = {
	[ACLPROC3_GETACL] = {
		.p_proc = ACLPROC3_GETACL,
		.p_encode = nfs3_xdr_enc_getacl3args,
		.p_decode = nfs3_xdr_dec_getacl3res,
		.p_arglen = ACL3_getaclargs_sz,
		.p_replen = ACL3_getaclres_sz,
		.p_timer = 1,
		.p_name = "GETACL",
	},
	[ACLPROC3_SETACL] = {
		.p_proc = ACLPROC3_SETACL,
		.p_encode = nfs3_xdr_enc_setacl3args,
		.p_decode = nfs3_xdr_dec_setacl3res,
		.p_arglen = ACL3_setaclargs_sz,
		.p_replen = ACL3_setaclres_sz,
		.p_timer = 0,
		.p_name = "SETACL",
	},
};

static unsigned int nfs3_acl_counts[ARRAY_SIZE(nfs3_acl_procedures)];
const struct rpc_version nfsacl_version3 = {
	.number			= 3,
	.nrprocs		= ARRAY_SIZE(nfs3_acl_procedures),
	.procs			= nfs3_acl_procedures,
	.counts			= nfs3_acl_counts,
};
#endif  /* CONFIG_NFS_V3_ACL */

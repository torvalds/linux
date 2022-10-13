// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014 Anna Schumaker <Anna.Schumaker@Netapp.com>
 */
#ifndef __LINUX_FS_NFS_NFS4_2XDR_H
#define __LINUX_FS_NFS_NFS4_2XDR_H

#include "nfs42.h"

#define encode_fallocate_maxsz		(encode_stateid_maxsz + \
					 2 /* offset */ + \
					 2 /* length */)
#define NFS42_WRITE_RES_SIZE		(1 /* wr_callback_id size */ +\
					 XDR_QUADLEN(NFS4_STATEID_SIZE) + \
					 2 /* wr_count */ + \
					 1 /* wr_committed */ + \
					 XDR_QUADLEN(NFS4_VERIFIER_SIZE))
#define encode_allocate_maxsz		(op_encode_hdr_maxsz + \
					 encode_fallocate_maxsz)
#define decode_allocate_maxsz		(op_decode_hdr_maxsz)
#define encode_copy_maxsz		(op_encode_hdr_maxsz +          \
					 XDR_QUADLEN(NFS4_STATEID_SIZE) + \
					 XDR_QUADLEN(NFS4_STATEID_SIZE) + \
					 2 + 2 + 2 + 1 + 1 + 1 +\
					 1 + /* One cnr_source_server */\
					 1 + /* nl4_type */ \
					 1 + XDR_QUADLEN(NFS4_OPAQUE_LIMIT))
#define decode_copy_maxsz		(op_decode_hdr_maxsz + \
					 NFS42_WRITE_RES_SIZE + \
					 1 /* cr_consecutive */ + \
					 1 /* cr_synchronous */)
#define encode_offload_cancel_maxsz	(op_encode_hdr_maxsz + \
					 XDR_QUADLEN(NFS4_STATEID_SIZE))
#define decode_offload_cancel_maxsz	(op_decode_hdr_maxsz)
#define encode_copy_notify_maxsz	(op_encode_hdr_maxsz + \
					 XDR_QUADLEN(NFS4_STATEID_SIZE) + \
					 1 + /* nl4_type */ \
					 1 + XDR_QUADLEN(NFS4_OPAQUE_LIMIT))
#define decode_copy_notify_maxsz	(op_decode_hdr_maxsz + \
					 3 + /* cnr_lease_time */\
					 XDR_QUADLEN(NFS4_STATEID_SIZE) + \
					 1 + /* Support 1 cnr_source_server */\
					 1 + /* nl4_type */ \
					 1 + XDR_QUADLEN(NFS4_OPAQUE_LIMIT))
#define encode_deallocate_maxsz		(op_encode_hdr_maxsz + \
					 encode_fallocate_maxsz)
#define decode_deallocate_maxsz		(op_decode_hdr_maxsz)
#define encode_read_plus_maxsz		(op_encode_hdr_maxsz + \
					 encode_stateid_maxsz + 3)
#define NFS42_READ_PLUS_SEGMENT_SIZE	(1 /* data_content4 */ + \
					 2 /* data_info4.di_offset */ + \
					 2 /* data_info4.di_length */)
#define decode_read_plus_maxsz		(op_decode_hdr_maxsz + \
					 1 /* rpr_eof */ + \
					 1 /* rpr_contents count */ + \
					 2 * NFS42_READ_PLUS_SEGMENT_SIZE)
#define encode_seek_maxsz		(op_encode_hdr_maxsz + \
					 encode_stateid_maxsz + \
					 2 /* offset */ + \
					 1 /* whence */)
#define decode_seek_maxsz		(op_decode_hdr_maxsz + \
					 1 /* eof */ + \
					 1 /* whence */ + \
					 2 /* offset */ + \
					 2 /* length */)
#define encode_io_info_maxsz		4
#define encode_layoutstats_maxsz	(op_decode_hdr_maxsz + \
					2 /* offset */ + \
					2 /* length */ + \
					encode_stateid_maxsz + \
					encode_io_info_maxsz + \
					encode_io_info_maxsz + \
					1 /* opaque devaddr4 length */ + \
					XDR_QUADLEN(PNFS_LAYOUTSTATS_MAXSIZE))
#define decode_layoutstats_maxsz	(op_decode_hdr_maxsz)
#define encode_device_error_maxsz	(XDR_QUADLEN(NFS4_DEVICEID4_SIZE) + \
					1 /* status */ + 1 /* opnum */)
#define encode_layouterror_maxsz	(op_decode_hdr_maxsz + \
					2 /* offset */ + \
					2 /* length */ + \
					encode_stateid_maxsz + \
					1 /* Array size */ + \
					encode_device_error_maxsz)
#define decode_layouterror_maxsz	(op_decode_hdr_maxsz)
#define encode_clone_maxsz		(encode_stateid_maxsz + \
					encode_stateid_maxsz + \
					2 /* src offset */ + \
					2 /* dst offset */ + \
					2 /* count */)
#define decode_clone_maxsz		(op_decode_hdr_maxsz)

#define NFS4_enc_allocate_sz		(compound_encode_hdr_maxsz + \
					 encode_sequence_maxsz + \
					 encode_putfh_maxsz + \
					 encode_allocate_maxsz + \
					 encode_getattr_maxsz)
#define NFS4_dec_allocate_sz		(compound_decode_hdr_maxsz + \
					 decode_sequence_maxsz + \
					 decode_putfh_maxsz + \
					 decode_allocate_maxsz + \
					 decode_getattr_maxsz)
#define NFS4_enc_copy_sz		(compound_encode_hdr_maxsz + \
					 encode_sequence_maxsz + \
					 encode_putfh_maxsz + \
					 encode_savefh_maxsz + \
					 encode_putfh_maxsz + \
					 encode_copy_maxsz + \
					 encode_commit_maxsz)
#define NFS4_dec_copy_sz		(compound_decode_hdr_maxsz + \
					 decode_sequence_maxsz + \
					 decode_putfh_maxsz + \
					 decode_savefh_maxsz + \
					 decode_putfh_maxsz + \
					 decode_copy_maxsz + \
					 decode_commit_maxsz)
#define NFS4_enc_offload_cancel_sz	(compound_encode_hdr_maxsz + \
					 encode_sequence_maxsz + \
					 encode_putfh_maxsz + \
					 encode_offload_cancel_maxsz)
#define NFS4_dec_offload_cancel_sz	(compound_decode_hdr_maxsz + \
					 decode_sequence_maxsz + \
					 decode_putfh_maxsz + \
					 decode_offload_cancel_maxsz)
#define NFS4_enc_copy_notify_sz		(compound_encode_hdr_maxsz + \
					 encode_putfh_maxsz + \
					 encode_copy_notify_maxsz)
#define NFS4_dec_copy_notify_sz		(compound_decode_hdr_maxsz + \
					 decode_putfh_maxsz + \
					 decode_copy_notify_maxsz)
#define NFS4_enc_deallocate_sz		(compound_encode_hdr_maxsz + \
					 encode_sequence_maxsz + \
					 encode_putfh_maxsz + \
					 encode_deallocate_maxsz + \
					 encode_getattr_maxsz)
#define NFS4_dec_deallocate_sz		(compound_decode_hdr_maxsz + \
					 decode_sequence_maxsz + \
					 decode_putfh_maxsz + \
					 decode_deallocate_maxsz + \
					 decode_getattr_maxsz)
#define NFS4_enc_read_plus_sz		(compound_encode_hdr_maxsz + \
					 encode_sequence_maxsz + \
					 encode_putfh_maxsz + \
					 encode_read_plus_maxsz)
#define NFS4_dec_read_plus_sz		(compound_decode_hdr_maxsz + \
					 decode_sequence_maxsz + \
					 decode_putfh_maxsz + \
					 decode_read_plus_maxsz)
#define NFS4_enc_seek_sz		(compound_encode_hdr_maxsz + \
					 encode_sequence_maxsz + \
					 encode_putfh_maxsz + \
					 encode_seek_maxsz)
#define NFS4_dec_seek_sz		(compound_decode_hdr_maxsz + \
					 decode_sequence_maxsz + \
					 decode_putfh_maxsz + \
					 decode_seek_maxsz)
#define NFS4_enc_layoutstats_sz		(compound_encode_hdr_maxsz + \
					 encode_sequence_maxsz + \
					 encode_putfh_maxsz + \
					 PNFS_LAYOUTSTATS_MAXDEV * encode_layoutstats_maxsz)
#define NFS4_dec_layoutstats_sz		(compound_decode_hdr_maxsz + \
					 decode_sequence_maxsz + \
					 decode_putfh_maxsz + \
					 PNFS_LAYOUTSTATS_MAXDEV * decode_layoutstats_maxsz)
#define NFS4_enc_layouterror_sz		(compound_encode_hdr_maxsz + \
					 encode_sequence_maxsz + \
					 encode_putfh_maxsz + \
					 NFS42_LAYOUTERROR_MAX * \
					 encode_layouterror_maxsz)
#define NFS4_dec_layouterror_sz		(compound_decode_hdr_maxsz + \
					 decode_sequence_maxsz + \
					 decode_putfh_maxsz + \
					 NFS42_LAYOUTERROR_MAX * \
					 decode_layouterror_maxsz)
#define NFS4_enc_clone_sz		(compound_encode_hdr_maxsz + \
					 encode_sequence_maxsz + \
					 encode_putfh_maxsz + \
					 encode_savefh_maxsz + \
					 encode_putfh_maxsz + \
					 encode_clone_maxsz + \
					 encode_getattr_maxsz)
#define NFS4_dec_clone_sz		(compound_decode_hdr_maxsz + \
					 decode_sequence_maxsz + \
					 decode_putfh_maxsz + \
					 decode_savefh_maxsz + \
					 decode_putfh_maxsz + \
					 decode_clone_maxsz + \
					 decode_getattr_maxsz)

/* Not limited by NFS itself, limited by the generic xattr code */
#define nfs4_xattr_name_maxsz   XDR_QUADLEN(XATTR_NAME_MAX)

#define encode_getxattr_maxsz   (op_encode_hdr_maxsz + 1 + \
				 nfs4_xattr_name_maxsz)
#define decode_getxattr_maxsz   (op_decode_hdr_maxsz + 1 + pagepad_maxsz)
#define encode_setxattr_maxsz   (op_encode_hdr_maxsz + \
				 1 + nfs4_xattr_name_maxsz + 1)
#define decode_setxattr_maxsz   (op_decode_hdr_maxsz + decode_change_info_maxsz)
#define encode_listxattrs_maxsz  (op_encode_hdr_maxsz + 2 + 1)
#define decode_listxattrs_maxsz  (op_decode_hdr_maxsz + 2 + 1 + 1 + 1)
#define encode_removexattr_maxsz (op_encode_hdr_maxsz + 1 + \
				  nfs4_xattr_name_maxsz)
#define decode_removexattr_maxsz (op_decode_hdr_maxsz + \
				  decode_change_info_maxsz)

#define NFS4_enc_getxattr_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_getxattr_maxsz)
#define NFS4_dec_getxattr_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_getxattr_maxsz)
#define NFS4_enc_setxattr_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_setxattr_maxsz)
#define NFS4_dec_setxattr_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_setxattr_maxsz)
#define NFS4_enc_listxattrs_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_listxattrs_maxsz)
#define NFS4_dec_listxattrs_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_listxattrs_maxsz)
#define NFS4_enc_removexattr_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_removexattr_maxsz)
#define NFS4_dec_removexattr_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_removexattr_maxsz)

/*
 * These values specify the maximum amount of data that is not
 * associated with the extended attribute name or extended
 * attribute list in the SETXATTR, GETXATTR and LISTXATTR
 * respectively.
 */
const u32 nfs42_maxsetxattr_overhead = ((RPC_MAX_HEADER_WITH_AUTH +
					compound_encode_hdr_maxsz +
					encode_sequence_maxsz +
					encode_putfh_maxsz + 1 +
					nfs4_xattr_name_maxsz)
					* XDR_UNIT);

const u32 nfs42_maxgetxattr_overhead = ((RPC_MAX_HEADER_WITH_AUTH +
					compound_decode_hdr_maxsz +
					decode_sequence_maxsz +
					decode_putfh_maxsz + 1) * XDR_UNIT);

const u32 nfs42_maxlistxattrs_overhead = ((RPC_MAX_HEADER_WITH_AUTH +
					compound_decode_hdr_maxsz +
					decode_sequence_maxsz +
					decode_putfh_maxsz + 3) * XDR_UNIT);

static void encode_fallocate(struct xdr_stream *xdr,
			     const struct nfs42_falloc_args *args)
{
	encode_nfs4_stateid(xdr, &args->falloc_stateid);
	encode_uint64(xdr, args->falloc_offset);
	encode_uint64(xdr, args->falloc_length);
}

static void encode_allocate(struct xdr_stream *xdr,
			    const struct nfs42_falloc_args *args,
			    struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_ALLOCATE, decode_allocate_maxsz, hdr);
	encode_fallocate(xdr, args);
}

static void encode_nl4_server(struct xdr_stream *xdr,
			      const struct nl4_server *ns)
{
	encode_uint32(xdr, ns->nl4_type);
	switch (ns->nl4_type) {
	case NL4_NAME:
	case NL4_URL:
		encode_string(xdr, ns->u.nl4_str_sz, ns->u.nl4_str);
		break;
	case NL4_NETADDR:
		encode_string(xdr, ns->u.nl4_addr.netid_len,
			      ns->u.nl4_addr.netid);
		encode_string(xdr, ns->u.nl4_addr.addr_len,
			      ns->u.nl4_addr.addr);
		break;
	default:
		WARN_ON_ONCE(1);
	}
}

static void encode_copy(struct xdr_stream *xdr,
			const struct nfs42_copy_args *args,
			struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_COPY, decode_copy_maxsz, hdr);
	encode_nfs4_stateid(xdr, &args->src_stateid);
	encode_nfs4_stateid(xdr, &args->dst_stateid);

	encode_uint64(xdr, args->src_pos);
	encode_uint64(xdr, args->dst_pos);
	encode_uint64(xdr, args->count);

	encode_uint32(xdr, 1); /* consecutive = true */
	encode_uint32(xdr, args->sync);
	if (args->cp_src == NULL) { /* intra-ssc */
		encode_uint32(xdr, 0); /* no src server list */
		return;
	}
	encode_uint32(xdr, 1); /* supporting 1 server */
	encode_nl4_server(xdr, args->cp_src);
}

static void encode_offload_cancel(struct xdr_stream *xdr,
				  const struct nfs42_offload_status_args *args,
				  struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_OFFLOAD_CANCEL, decode_offload_cancel_maxsz, hdr);
	encode_nfs4_stateid(xdr, &args->osa_stateid);
}

static void encode_copy_notify(struct xdr_stream *xdr,
			       const struct nfs42_copy_notify_args *args,
			       struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_COPY_NOTIFY, decode_copy_notify_maxsz, hdr);
	encode_nfs4_stateid(xdr, &args->cna_src_stateid);
	encode_nl4_server(xdr, &args->cna_dst);
}

static void encode_deallocate(struct xdr_stream *xdr,
			      const struct nfs42_falloc_args *args,
			      struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_DEALLOCATE, decode_deallocate_maxsz, hdr);
	encode_fallocate(xdr, args);
}

static void encode_read_plus(struct xdr_stream *xdr,
			     const struct nfs_pgio_args *args,
			     struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_READ_PLUS, decode_read_plus_maxsz, hdr);
	encode_nfs4_stateid(xdr, &args->stateid);
	encode_uint64(xdr, args->offset);
	encode_uint32(xdr, args->count);
}

static void encode_seek(struct xdr_stream *xdr,
			const struct nfs42_seek_args *args,
			struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_SEEK, decode_seek_maxsz, hdr);
	encode_nfs4_stateid(xdr, &args->sa_stateid);
	encode_uint64(xdr, args->sa_offset);
	encode_uint32(xdr, args->sa_what);
}

static void encode_layoutstats(struct xdr_stream *xdr,
			       const struct nfs42_layoutstat_args *args,
			       struct nfs42_layoutstat_devinfo *devinfo,
			       struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_LAYOUTSTATS, decode_layoutstats_maxsz, hdr);
	p = reserve_space(xdr, 8 + 8);
	p = xdr_encode_hyper(p, devinfo->offset);
	p = xdr_encode_hyper(p, devinfo->length);
	encode_nfs4_stateid(xdr, &args->stateid);
	p = reserve_space(xdr, 4*8 + NFS4_DEVICEID4_SIZE + 4);
	p = xdr_encode_hyper(p, devinfo->read_count);
	p = xdr_encode_hyper(p, devinfo->read_bytes);
	p = xdr_encode_hyper(p, devinfo->write_count);
	p = xdr_encode_hyper(p, devinfo->write_bytes);
	p = xdr_encode_opaque_fixed(p, devinfo->dev_id.data,
			NFS4_DEVICEID4_SIZE);
	/* Encode layoutupdate4 */
	*p++ = cpu_to_be32(devinfo->layout_type);
	if (devinfo->ld_private.ops)
		devinfo->ld_private.ops->encode(xdr, args,
				&devinfo->ld_private);
	else
		encode_uint32(xdr, 0);
}

static void encode_clone(struct xdr_stream *xdr,
			 const struct nfs42_clone_args *args,
			 struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_CLONE, decode_clone_maxsz, hdr);
	encode_nfs4_stateid(xdr, &args->src_stateid);
	encode_nfs4_stateid(xdr, &args->dst_stateid);
	p = reserve_space(xdr, 3*8);
	p = xdr_encode_hyper(p, args->src_offset);
	p = xdr_encode_hyper(p, args->dst_offset);
	xdr_encode_hyper(p, args->count);
}

static void encode_device_error(struct xdr_stream *xdr,
				const struct nfs42_device_error *error)
{
	__be32 *p;

	p = reserve_space(xdr, NFS4_DEVICEID4_SIZE + 2*4);
	p = xdr_encode_opaque_fixed(p, error->dev_id.data,
			NFS4_DEVICEID4_SIZE);
	*p++ = cpu_to_be32(error->status);
	*p = cpu_to_be32(error->opnum);
}

static void encode_layouterror(struct xdr_stream *xdr,
			       const struct nfs42_layout_error *args,
			       struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_LAYOUTERROR, decode_layouterror_maxsz, hdr);
	p = reserve_space(xdr, 8 + 8);
	p = xdr_encode_hyper(p, args->offset);
	p = xdr_encode_hyper(p, args->length);
	encode_nfs4_stateid(xdr, &args->stateid);
	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(1);
	encode_device_error(xdr, &args->errors[0]);
}

static void encode_setxattr(struct xdr_stream *xdr,
			    const struct nfs42_setxattrargs *arg,
			    struct compound_hdr *hdr)
{
	__be32 *p;

	BUILD_BUG_ON(XATTR_CREATE != SETXATTR4_CREATE);
	BUILD_BUG_ON(XATTR_REPLACE != SETXATTR4_REPLACE);

	encode_op_hdr(xdr, OP_SETXATTR, decode_setxattr_maxsz, hdr);
	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(arg->xattr_flags);
	encode_string(xdr, strlen(arg->xattr_name), arg->xattr_name);
	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(arg->xattr_len);
	if (arg->xattr_len)
		xdr_write_pages(xdr, arg->xattr_pages, 0, arg->xattr_len);
}

static int decode_setxattr(struct xdr_stream *xdr,
			   struct nfs4_change_info *cinfo)
{
	int status;

	status = decode_op_hdr(xdr, OP_SETXATTR);
	if (status)
		goto out;
	status = decode_change_info(xdr, cinfo);
out:
	return status;
}


static void encode_getxattr(struct xdr_stream *xdr, const char *name,
			    struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_GETXATTR, decode_getxattr_maxsz, hdr);
	encode_string(xdr, strlen(name), name);
}

static int decode_getxattr(struct xdr_stream *xdr,
			   struct nfs42_getxattrres *res,
			   struct rpc_rqst *req)
{
	int status;
	__be32 *p;
	u32 len, rdlen;

	status = decode_op_hdr(xdr, OP_GETXATTR);
	if (status)
		return status;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;

	len = be32_to_cpup(p);

	/*
	 * Only check against the page length here. The actual
	 * requested length may be smaller, but that is only
	 * checked against after possibly caching a valid reply.
	 */
	if (len > req->rq_rcv_buf.page_len)
		return -ERANGE;

	res->xattr_len = len;

	if (len > 0) {
		rdlen = xdr_read_pages(xdr, len);
		if (rdlen < len)
			return -EIO;
	}

	return 0;
}

static void encode_removexattr(struct xdr_stream *xdr, const char *name,
			       struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_REMOVEXATTR, decode_removexattr_maxsz, hdr);
	encode_string(xdr, strlen(name), name);
}


static int decode_removexattr(struct xdr_stream *xdr,
			   struct nfs4_change_info *cinfo)
{
	int status;

	status = decode_op_hdr(xdr, OP_REMOVEXATTR);
	if (status)
		goto out;

	status = decode_change_info(xdr, cinfo);
out:
	return status;
}

static void encode_listxattrs(struct xdr_stream *xdr,
			     const struct nfs42_listxattrsargs *arg,
			     struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_LISTXATTRS, decode_listxattrs_maxsz, hdr);

	p = reserve_space(xdr, 12);
	if (unlikely(!p))
		return;

	p = xdr_encode_hyper(p, arg->cookie);
	/*
	 * RFC 8276 says to specify the full max length of the LISTXATTRS
	 * XDR reply. Count is set to the XDR length of the names array
	 * plus the EOF marker. So, add the cookie and the names count.
	 */
	*p = cpu_to_be32(arg->count + 8 + 4);
}

static int decode_listxattrs(struct xdr_stream *xdr,
			    struct nfs42_listxattrsres *res)
{
	int status;
	__be32 *p;
	u32 count, len, ulen;
	size_t left, copied;
	char *buf;

	status = decode_op_hdr(xdr, OP_LISTXATTRS);
	if (status) {
		/*
		 * Special case: for LISTXATTRS, NFS4ERR_TOOSMALL
		 * should be translated to ERANGE.
		 */
		if (status == -ETOOSMALL)
			status = -ERANGE;
		/*
		 * Special case: for LISTXATTRS, NFS4ERR_NOXATTR
		 * should be translated to success with zero-length reply.
		 */
		if (status == -ENODATA) {
			res->eof = true;
			status = 0;
		}
		goto out;
	}

	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		return -EIO;

	xdr_decode_hyper(p, &res->cookie);

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;

	left = res->xattr_len;
	buf = res->xattr_buf;

	count = be32_to_cpup(p);
	copied = 0;

	/*
	 * We have asked for enough room to encode the maximum number
	 * of possible attribute names, so everything should fit.
	 *
	 * But, don't rely on that assumption. Just decode entries
	 * until they don't fit anymore, just in case the server did
	 * something odd.
	 */
	while (count--) {
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(!p))
			return -EIO;

		len = be32_to_cpup(p);
		if (len > (XATTR_NAME_MAX - XATTR_USER_PREFIX_LEN)) {
			status = -ERANGE;
			goto out;
		}

		p = xdr_inline_decode(xdr, len);
		if (unlikely(!p))
			return -EIO;

		ulen = len + XATTR_USER_PREFIX_LEN + 1;
		if (buf) {
			if (ulen > left) {
				status = -ERANGE;
				goto out;
			}

			memcpy(buf, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN);
			memcpy(buf + XATTR_USER_PREFIX_LEN, p, len);

			buf[ulen - 1] = 0;
			buf += ulen;
			left -= ulen;
		}
		copied += ulen;
	}

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;

	res->eof = be32_to_cpup(p);
	res->copied = copied;

out:
	if (status == -ERANGE && res->xattr_len == XATTR_LIST_MAX)
		status = -E2BIG;

	return status;
}

/*
 * Encode ALLOCATE request
 */
static void nfs4_xdr_enc_allocate(struct rpc_rqst *req,
				  struct xdr_stream *xdr,
				  const void *data)
{
	const struct nfs42_falloc_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->falloc_fh, &hdr);
	encode_allocate(xdr, args, &hdr);
	encode_getfattr(xdr, args->falloc_bitmask, &hdr);
	encode_nops(&hdr);
}

static void encode_copy_commit(struct xdr_stream *xdr,
			  const struct nfs42_copy_args *args,
			  struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_COMMIT, decode_commit_maxsz, hdr);
	p = reserve_space(xdr, 12);
	p = xdr_encode_hyper(p, args->dst_pos);
	*p = cpu_to_be32(args->count);
}

/*
 * Encode COPY request
 */
static void nfs4_xdr_enc_copy(struct rpc_rqst *req,
			      struct xdr_stream *xdr,
			      const void *data)
{
	const struct nfs42_copy_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->src_fh, &hdr);
	encode_savefh(xdr, &hdr);
	encode_putfh(xdr, args->dst_fh, &hdr);
	encode_copy(xdr, args, &hdr);
	if (args->sync)
		encode_copy_commit(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode OFFLOAD_CANEL request
 */
static void nfs4_xdr_enc_offload_cancel(struct rpc_rqst *req,
					struct xdr_stream *xdr,
					const void *data)
{
	const struct nfs42_offload_status_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->osa_seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->osa_seq_args, &hdr);
	encode_putfh(xdr, args->osa_src_fh, &hdr);
	encode_offload_cancel(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode COPY_NOTIFY request
 */
static void nfs4_xdr_enc_copy_notify(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const struct nfs42_copy_notify_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->cna_seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->cna_seq_args, &hdr);
	encode_putfh(xdr, args->cna_src_fh, &hdr);
	encode_copy_notify(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode DEALLOCATE request
 */
static void nfs4_xdr_enc_deallocate(struct rpc_rqst *req,
				    struct xdr_stream *xdr,
				    const void *data)
{
	const struct nfs42_falloc_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->falloc_fh, &hdr);
	encode_deallocate(xdr, args, &hdr);
	encode_getfattr(xdr, args->falloc_bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode READ_PLUS request
 */
static void nfs4_xdr_enc_read_plus(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   const void *data)
{
	const struct nfs_pgio_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_read_plus(xdr, args, &hdr);

	rpc_prepare_reply_pages(req, args->pages, args->pgbase,
				args->count, hdr.replen);
	encode_nops(&hdr);
}

/*
 * Encode SEEK request
 */
static void nfs4_xdr_enc_seek(struct rpc_rqst *req,
			      struct xdr_stream *xdr,
			      const void *data)
{
	const struct nfs42_seek_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->sa_fh, &hdr);
	encode_seek(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode LAYOUTSTATS request
 */
static void nfs4_xdr_enc_layoutstats(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const struct nfs42_layoutstat_args *args = data;
	int i;

	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	WARN_ON(args->num_dev > PNFS_LAYOUTSTATS_MAXDEV);
	for (i = 0; i < args->num_dev; i++)
		encode_layoutstats(xdr, args, &args->devinfo[i], &hdr);
	encode_nops(&hdr);
}

/*
 * Encode CLONE request
 */
static void nfs4_xdr_enc_clone(struct rpc_rqst *req,
			       struct xdr_stream *xdr,
			       const void *data)
{
	const struct nfs42_clone_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->src_fh, &hdr);
	encode_savefh(xdr, &hdr);
	encode_putfh(xdr, args->dst_fh, &hdr);
	encode_clone(xdr, args, &hdr);
	encode_getfattr(xdr, args->dst_bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode LAYOUTERROR request
 */
static void nfs4_xdr_enc_layouterror(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const struct nfs42_layouterror_args *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};
	int i;

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, NFS_FH(args->inode), &hdr);
	for (i = 0; i < args->num_errors; i++)
		encode_layouterror(xdr, &args->errors[i], &hdr);
	encode_nops(&hdr);
}

static int decode_allocate(struct xdr_stream *xdr, struct nfs42_falloc_res *res)
{
	return decode_op_hdr(xdr, OP_ALLOCATE);
}

static int decode_write_response(struct xdr_stream *xdr,
				 struct nfs42_write_res *res)
{
	__be32 *p;
	int status, count;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	count = be32_to_cpup(p);
	if (count > 1)
		return -EREMOTEIO;
	else if (count == 1) {
		status = decode_opaque_fixed(xdr, &res->stateid,
				NFS4_STATEID_SIZE);
		if (unlikely(status))
			return -EIO;
	}
	p = xdr_inline_decode(xdr, 8 + 4);
	if (unlikely(!p))
		return -EIO;
	p = xdr_decode_hyper(p, &res->count);
	res->verifier.committed = be32_to_cpup(p);
	return decode_verifier(xdr, &res->verifier.verifier);
}

static int decode_nl4_server(struct xdr_stream *xdr, struct nl4_server *ns)
{
	struct nfs42_netaddr *naddr;
	uint32_t dummy;
	char *dummy_str;
	__be32 *p;
	int status;

	/* nl_type */
	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;
	ns->nl4_type = be32_to_cpup(p);
	switch (ns->nl4_type) {
	case NL4_NAME:
	case NL4_URL:
		status = decode_opaque_inline(xdr, &dummy, &dummy_str);
		if (unlikely(status))
			return status;
		if (unlikely(dummy > NFS4_OPAQUE_LIMIT))
			return -EIO;
		memcpy(&ns->u.nl4_str, dummy_str, dummy);
		ns->u.nl4_str_sz = dummy;
		break;
	case NL4_NETADDR:
		naddr = &ns->u.nl4_addr;

		/* netid string */
		status = decode_opaque_inline(xdr, &dummy, &dummy_str);
		if (unlikely(status))
			return status;
		if (unlikely(dummy > RPCBIND_MAXNETIDLEN))
			return -EIO;
		naddr->netid_len = dummy;
		memcpy(naddr->netid, dummy_str, naddr->netid_len);

		/* uaddr string */
		status = decode_opaque_inline(xdr, &dummy, &dummy_str);
		if (unlikely(status))
			return status;
		if (unlikely(dummy > RPCBIND_MAXUADDRLEN))
			return -EIO;
		naddr->addr_len = dummy;
		memcpy(naddr->addr, dummy_str, naddr->addr_len);
		break;
	default:
		WARN_ON_ONCE(1);
		return -EIO;
	}
	return 0;
}

static int decode_copy_requirements(struct xdr_stream *xdr,
				    struct nfs42_copy_res *res) {
	__be32 *p;

	p = xdr_inline_decode(xdr, 4 + 4);
	if (unlikely(!p))
		return -EIO;

	res->consecutive = be32_to_cpup(p++);
	res->synchronous = be32_to_cpup(p++);
	return 0;
}

static int decode_copy(struct xdr_stream *xdr, struct nfs42_copy_res *res)
{
	int status;

	status = decode_op_hdr(xdr, OP_COPY);
	if (status == NFS4ERR_OFFLOAD_NO_REQS) {
		status = decode_copy_requirements(xdr, res);
		if (status)
			return status;
		return NFS4ERR_OFFLOAD_NO_REQS;
	} else if (status)
		return status;

	status = decode_write_response(xdr, &res->write_res);
	if (status)
		return status;

	return decode_copy_requirements(xdr, res);
}

static int decode_offload_cancel(struct xdr_stream *xdr,
				 struct nfs42_offload_status_res *res)
{
	return decode_op_hdr(xdr, OP_OFFLOAD_CANCEL);
}

static int decode_copy_notify(struct xdr_stream *xdr,
			      struct nfs42_copy_notify_res *res)
{
	__be32 *p;
	int status, count;

	status = decode_op_hdr(xdr, OP_COPY_NOTIFY);
	if (status)
		return status;
	/* cnr_lease_time */
	p = xdr_inline_decode(xdr, 12);
	if (unlikely(!p))
		return -EIO;
	p = xdr_decode_hyper(p, &res->cnr_lease_time.seconds);
	res->cnr_lease_time.nseconds = be32_to_cpup(p);

	status = decode_opaque_fixed(xdr, &res->cnr_stateid, NFS4_STATEID_SIZE);
	if (unlikely(status))
		return -EIO;

	/* number of source addresses */
	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		return -EIO;

	count = be32_to_cpup(p);
	if (count > 1)
		pr_warn("NFS: %s: nsvr %d > Supported. Use first servers\n",
			 __func__, count);

	status = decode_nl4_server(xdr, &res->cnr_src);
	if (unlikely(status))
		return -EIO;
	return 0;
}

static int decode_deallocate(struct xdr_stream *xdr, struct nfs42_falloc_res *res)
{
	return decode_op_hdr(xdr, OP_DEALLOCATE);
}

struct read_plus_segment {
	enum data_content4 type;
	uint64_t offset;
	union {
		struct {
			uint64_t length;
		} hole;

		struct {
			uint32_t length;
			unsigned int from;
		} data;
	};
};

static inline uint64_t read_plus_segment_length(struct read_plus_segment *seg)
{
	return seg->type == NFS4_CONTENT_DATA ? seg->data.length : seg->hole.length;
}

static int decode_read_plus_segment(struct xdr_stream *xdr,
				    struct read_plus_segment *seg)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (!p)
		return -EIO;
	seg->type = be32_to_cpup(p++);

	p = xdr_inline_decode(xdr, seg->type == NFS4_CONTENT_DATA ? 12 : 16);
	if (!p)
		return -EIO;
	p = xdr_decode_hyper(p, &seg->offset);

	if (seg->type == NFS4_CONTENT_DATA) {
		struct xdr_buf buf;
		uint32_t len = be32_to_cpup(p);

		seg->data.length = len;
		seg->data.from = xdr_stream_pos(xdr);

		if (!xdr_stream_subsegment(xdr, &buf, xdr_align_size(len)))
			return -EIO;
	} else if (seg->type == NFS4_CONTENT_HOLE) {
		xdr_decode_hyper(p, &seg->hole.length);
	} else
		return -EINVAL;
	return 0;
}

static int process_read_plus_segment(struct xdr_stream *xdr,
				     struct nfs_pgio_args *args,
				     struct nfs_pgio_res *res,
				     struct read_plus_segment *seg)
{
	unsigned long offset = seg->offset;
	unsigned long length = read_plus_segment_length(seg);
	unsigned int bufpos;

	if (offset + length < args->offset)
		return 0;
	else if (offset > args->offset + args->count) {
		res->eof = 0;
		return 0;
	} else if (offset < args->offset) {
		length -= (args->offset - offset);
		offset = args->offset;
	} else if (offset + length > args->offset + args->count) {
		length = (args->offset + args->count) - offset;
		res->eof = 0;
	}

	bufpos = xdr->buf->head[0].iov_len + (offset - args->offset);
	if (seg->type == NFS4_CONTENT_HOLE)
		return xdr_stream_zero(xdr, bufpos, length);
	else
		return xdr_stream_move_subsegment(xdr, seg->data.from, bufpos, length);
}

static int decode_read_plus(struct xdr_stream *xdr, struct nfs_pgio_res *res)
{
	struct nfs_pgio_header *hdr =
		container_of(res, struct nfs_pgio_header, res);
	struct nfs_pgio_args *args = &hdr->args;
	uint32_t segments;
	struct read_plus_segment *segs;
	int status, i;
	char scratch_buf[16];
	__be32 *p;

	status = decode_op_hdr(xdr, OP_READ_PLUS);
	if (status)
		return status;

	p = xdr_inline_decode(xdr, 4 + 4);
	if (unlikely(!p))
		return -EIO;

	res->count = 0;
	res->eof = be32_to_cpup(p++);
	segments = be32_to_cpup(p++);
	if (segments == 0)
		return status;

	segs = kmalloc_array(segments, sizeof(*segs), GFP_KERNEL);
	if (!segs)
		return -ENOMEM;

	xdr_set_scratch_buffer(xdr, &scratch_buf, 32);
	status = -EIO;
	for (i = 0; i < segments; i++) {
		status = decode_read_plus_segment(xdr, &segs[i]);
		if (status < 0)
			goto out;
	}

	xdr_set_pagelen(xdr, xdr_align_size(args->count));
	for (i = segments; i > 0; i--)
		res->count += process_read_plus_segment(xdr, args, res, &segs[i-1]);
	status = 0;

out:
	kfree(segs);
	return status;
}

static int decode_seek(struct xdr_stream *xdr, struct nfs42_seek_res *res)
{
	int status;
	__be32 *p;

	status = decode_op_hdr(xdr, OP_SEEK);
	if (status)
		return status;

	p = xdr_inline_decode(xdr, 4 + 8);
	if (unlikely(!p))
		return -EIO;

	res->sr_eof = be32_to_cpup(p++);
	p = xdr_decode_hyper(p, &res->sr_offset);
	return 0;
}

static int decode_layoutstats(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_LAYOUTSTATS);
}

static int decode_clone(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_CLONE);
}

static int decode_layouterror(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_LAYOUTERROR);
}

/*
 * Decode ALLOCATE request
 */
static int nfs4_xdr_dec_allocate(struct rpc_rqst *rqstp,
				 struct xdr_stream *xdr,
				 void *data)
{
	struct nfs42_falloc_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_allocate(xdr, res);
	if (status)
		goto out;
	decode_getfattr(xdr, res->falloc_fattr, res->falloc_server);
out:
	return status;
}

/*
 * Decode COPY response
 */
static int nfs4_xdr_dec_copy(struct rpc_rqst *rqstp,
			     struct xdr_stream *xdr,
			     void *data)
{
	struct nfs42_copy_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_savefh(xdr);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_copy(xdr, res);
	if (status)
		goto out;
	if (res->commit_res.verf)
		status = decode_commit(xdr, &res->commit_res);
out:
	return status;
}

/*
 * Decode OFFLOAD_CANCEL response
 */
static int nfs4_xdr_dec_offload_cancel(struct rpc_rqst *rqstp,
				       struct xdr_stream *xdr,
				       void *data)
{
	struct nfs42_offload_status_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->osr_seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_offload_cancel(xdr, res);

out:
	return status;
}

/*
 * Decode COPY_NOTIFY response
 */
static int nfs4_xdr_dec_copy_notify(struct rpc_rqst *rqstp,
				    struct xdr_stream *xdr,
				    void *data)
{
	struct nfs42_copy_notify_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->cnr_seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_copy_notify(xdr, res);

out:
	return status;
}

/*
 * Decode DEALLOCATE request
 */
static int nfs4_xdr_dec_deallocate(struct rpc_rqst *rqstp,
				   struct xdr_stream *xdr,
				   void *data)
{
	struct nfs42_falloc_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_deallocate(xdr, res);
	if (status)
		goto out;
	decode_getfattr(xdr, res->falloc_fattr, res->falloc_server);
out:
	return status;
}

/*
 * Decode READ_PLUS request
 */
static int nfs4_xdr_dec_read_plus(struct rpc_rqst *rqstp,
				  struct xdr_stream *xdr,
				  void *data)
{
	struct nfs_pgio_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_read_plus(xdr, res);
	if (!status)
		status = res->count;
out:
	return status;
}

/*
 * Decode SEEK request
 */
static int nfs4_xdr_dec_seek(struct rpc_rqst *rqstp,
			     struct xdr_stream *xdr,
			     void *data)
{
	struct nfs42_seek_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_seek(xdr, res);
out:
	return status;
}

/*
 * Decode LAYOUTSTATS request
 */
static int nfs4_xdr_dec_layoutstats(struct rpc_rqst *rqstp,
				    struct xdr_stream *xdr,
				    void *data)
{
	struct nfs42_layoutstat_res *res = data;
	struct compound_hdr hdr;
	int status, i;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	WARN_ON(res->num_dev > PNFS_LAYOUTSTATS_MAXDEV);
	for (i = 0; i < res->num_dev; i++) {
		status = decode_layoutstats(xdr);
		if (status)
			goto out;
	}
out:
	res->rpc_status = status;
	return status;
}

/*
 * Decode CLONE request
 */
static int nfs4_xdr_dec_clone(struct rpc_rqst *rqstp,
			      struct xdr_stream *xdr,
			      void *data)
{
	struct nfs42_clone_res *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_savefh(xdr);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_clone(xdr);
	if (status)
		goto out;
	decode_getfattr(xdr, res->dst_fattr, res->server);
out:
	res->rpc_status = status;
	return status;
}

/*
 * Decode LAYOUTERROR request
 */
static int nfs4_xdr_dec_layouterror(struct rpc_rqst *rqstp,
				    struct xdr_stream *xdr,
				    void *data)
{
	struct nfs42_layouterror_res *res = data;
	struct compound_hdr hdr;
	int status, i;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);

	for (i = 0; i < res->num_errors && status == 0; i++)
		status = decode_layouterror(xdr);
out:
	res->rpc_status = status;
	return status;
}

#ifdef CONFIG_NFS_V4_2
static void nfs4_xdr_enc_setxattr(struct rpc_rqst *req, struct xdr_stream *xdr,
				  const void *data)
{
	const struct nfs42_setxattrargs *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_setxattr(xdr, args, &hdr);
	encode_nops(&hdr);
}

static int nfs4_xdr_dec_setxattr(struct rpc_rqst *req, struct xdr_stream *xdr,
				 void *data)
{
	struct nfs42_setxattrres *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, req);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;

	status = decode_setxattr(xdr, &res->cinfo);
out:
	return status;
}

static void nfs4_xdr_enc_getxattr(struct rpc_rqst *req, struct xdr_stream *xdr,
				  const void *data)
{
	const struct nfs42_getxattrargs *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};
	uint32_t replen;

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	replen = hdr.replen + op_decode_hdr_maxsz + 1;
	encode_getxattr(xdr, args->xattr_name, &hdr);

	rpc_prepare_reply_pages(req, args->xattr_pages, 0, args->xattr_len,
				replen);

	encode_nops(&hdr);
}

static int nfs4_xdr_dec_getxattr(struct rpc_rqst *rqstp,
				 struct xdr_stream *xdr, void *data)
{
	struct nfs42_getxattrres *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_getxattr(xdr, res, rqstp);
out:
	return status;
}

static void nfs4_xdr_enc_listxattrs(struct rpc_rqst *req,
				    struct xdr_stream *xdr, const void *data)
{
	const struct nfs42_listxattrsargs *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};
	uint32_t replen;

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	replen = hdr.replen + op_decode_hdr_maxsz + 2 + 1;
	encode_listxattrs(xdr, args, &hdr);

	rpc_prepare_reply_pages(req, args->xattr_pages, 0, args->count, replen);

	encode_nops(&hdr);
}

static int nfs4_xdr_dec_listxattrs(struct rpc_rqst *rqstp,
				   struct xdr_stream *xdr, void *data)
{
	struct nfs42_listxattrsres *res = data;
	struct compound_hdr hdr;
	int status;

	xdr_set_scratch_page(xdr, res->scratch);

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, rqstp);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;
	status = decode_listxattrs(xdr, res);
out:
	return status;
}

static void nfs4_xdr_enc_removexattr(struct rpc_rqst *req,
				     struct xdr_stream *xdr, const void *data)
{
	const struct nfs42_removexattrargs *args = data;
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_removexattr(xdr, args->xattr_name, &hdr);
	encode_nops(&hdr);
}

static int nfs4_xdr_dec_removexattr(struct rpc_rqst *req,
				    struct xdr_stream *xdr, void *data)
{
	struct nfs42_removexattrres *res = data;
	struct compound_hdr hdr;
	int status;

	status = decode_compound_hdr(xdr, &hdr);
	if (status)
		goto out;
	status = decode_sequence(xdr, &res->seq_res, req);
	if (status)
		goto out;
	status = decode_putfh(xdr);
	if (status)
		goto out;

	status = decode_removexattr(xdr, &res->cinfo);
out:
	return status;
}
#endif
#endif /* __LINUX_FS_NFS_NFS4_2XDR_H */

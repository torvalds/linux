/*
 * Copyright (c) 2014 Anna Schumaker <Anna.Schumaker@Netapp.com>
 */
#ifndef __LINUX_FS_NFS_NFS4_2XDR_H
#define __LINUX_FS_NFS_NFS4_2XDR_H

#define encode_fallocate_maxsz		(encode_stateid_maxsz + \
					 2 /* offset */ + \
					 2 /* length */)
#define encode_allocate_maxsz		(op_encode_hdr_maxsz + \
					 encode_fallocate_maxsz)
#define decode_allocate_maxsz		(op_decode_hdr_maxsz)
#define encode_deallocate_maxsz		(op_encode_hdr_maxsz + \
					 encode_fallocate_maxsz)
#define decode_deallocate_maxsz		(op_decode_hdr_maxsz)
#define encode_seek_maxsz		(op_encode_hdr_maxsz + \
					 encode_stateid_maxsz + \
					 2 /* offset */ + \
					 1 /* whence */)
#define decode_seek_maxsz		(op_decode_hdr_maxsz + \
					 1 /* eof */ + \
					 1 /* whence */ + \
					 2 /* offset */ + \
					 2 /* length */)

#define NFS4_enc_allocate_sz		(compound_encode_hdr_maxsz + \
					 encode_putfh_maxsz + \
					 encode_allocate_maxsz)
#define NFS4_dec_allocate_sz		(compound_decode_hdr_maxsz + \
					 decode_putfh_maxsz + \
					 decode_allocate_maxsz)
#define NFS4_enc_deallocate_sz		(compound_encode_hdr_maxsz + \
					 encode_putfh_maxsz + \
					 encode_deallocate_maxsz)
#define NFS4_dec_deallocate_sz		(compound_decode_hdr_maxsz + \
					 decode_putfh_maxsz + \
					 decode_deallocate_maxsz)
#define NFS4_enc_seek_sz		(compound_encode_hdr_maxsz + \
					 encode_putfh_maxsz + \
					 encode_seek_maxsz)
#define NFS4_dec_seek_sz		(compound_decode_hdr_maxsz + \
					 decode_putfh_maxsz + \
					 decode_seek_maxsz)


static void encode_fallocate(struct xdr_stream *xdr,
			     struct nfs42_falloc_args *args)
{
	encode_nfs4_stateid(xdr, &args->falloc_stateid);
	encode_uint64(xdr, args->falloc_offset);
	encode_uint64(xdr, args->falloc_length);
}

static void encode_allocate(struct xdr_stream *xdr,
			    struct nfs42_falloc_args *args,
			    struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_ALLOCATE, decode_allocate_maxsz, hdr);
	encode_fallocate(xdr, args);
}

static void encode_deallocate(struct xdr_stream *xdr,
			      struct nfs42_falloc_args *args,
			      struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_DEALLOCATE, decode_deallocate_maxsz, hdr);
	encode_fallocate(xdr, args);
}

static void encode_seek(struct xdr_stream *xdr,
			struct nfs42_seek_args *args,
			struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_SEEK, decode_seek_maxsz, hdr);
	encode_nfs4_stateid(xdr, &args->sa_stateid);
	encode_uint64(xdr, args->sa_offset);
	encode_uint32(xdr, args->sa_what);
}

/*
 * Encode ALLOCATE request
 */
static void nfs4_xdr_enc_allocate(struct rpc_rqst *req,
				  struct xdr_stream *xdr,
				  struct nfs42_falloc_args *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->falloc_fh, &hdr);
	encode_allocate(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode DEALLOCATE request
 */
static void nfs4_xdr_enc_deallocate(struct rpc_rqst *req,
				    struct xdr_stream *xdr,
				    struct nfs42_falloc_args *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->falloc_fh, &hdr);
	encode_deallocate(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode SEEK request
 */
static void nfs4_xdr_enc_seek(struct rpc_rqst *req,
			      struct xdr_stream *xdr,
			      struct nfs42_seek_args *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->sa_fh, &hdr);
	encode_seek(xdr, args, &hdr);
	encode_nops(&hdr);
}

static int decode_allocate(struct xdr_stream *xdr, struct nfs42_falloc_res *res)
{
	return decode_op_hdr(xdr, OP_ALLOCATE);
}

static int decode_deallocate(struct xdr_stream *xdr, struct nfs42_falloc_res *res)
{
	return decode_op_hdr(xdr, OP_DEALLOCATE);
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
		goto out_overflow;

	res->sr_eof = be32_to_cpup(p++);
	p = xdr_decode_hyper(p, &res->sr_offset);
	return 0;

out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

/*
 * Decode ALLOCATE request
 */
static int nfs4_xdr_dec_allocate(struct rpc_rqst *rqstp,
				 struct xdr_stream *xdr,
				 struct nfs42_falloc_res *res)
{
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
out:
	return status;
}

/*
 * Decode DEALLOCATE request
 */
static int nfs4_xdr_dec_deallocate(struct rpc_rqst *rqstp,
				   struct xdr_stream *xdr,
				   struct nfs42_falloc_res *res)
{
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
out:
	return status;
}

/*
 * Decode SEEK request
 */
static int nfs4_xdr_dec_seek(struct rpc_rqst *rqstp,
			     struct xdr_stream *xdr,
			     struct nfs42_seek_res *res)
{
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
#endif /* __LINUX_FS_NFS_NFS4_2XDR_H */

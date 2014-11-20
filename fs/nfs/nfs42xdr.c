/*
 * Copyright (c) 2014 Anna Schumaker <Anna.Schumaker@Netapp.com>
 */
#ifndef __LINUX_FS_NFS_NFS4_2XDR_H
#define __LINUX_FS_NFS_NFS4_2XDR_H

#define encode_seek_maxsz		(op_encode_hdr_maxsz + \
					 encode_stateid_maxsz + \
					 2 /* offset */ + \
					 1 /* whence */)
#define decode_seek_maxsz		(op_decode_hdr_maxsz + \
					 1 /* eof */ + \
					 1 /* whence */ + \
					 2 /* offset */ + \
					 2 /* length */)

#define NFS4_enc_seek_sz		(compound_encode_hdr_maxsz + \
					 encode_putfh_maxsz + \
					 encode_seek_maxsz)
#define NFS4_dec_seek_sz		(compound_decode_hdr_maxsz + \
					 decode_putfh_maxsz + \
					 decode_seek_maxsz)


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

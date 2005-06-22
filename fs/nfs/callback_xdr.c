/*
 * linux/fs/nfs/callback_xdr.c
 *
 * Copyright (C) 2004 Trond Myklebust
 *
 * NFSv4 callback encode/decode procedures
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sunrpc/svc.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include "nfs4_fs.h"
#include "callback.h"

#define CB_OP_TAGLEN_MAXSZ	(512)
#define CB_OP_HDR_RES_MAXSZ	(2 + CB_OP_TAGLEN_MAXSZ)
#define CB_OP_GETATTR_BITMAP_MAXSZ	(4)
#define CB_OP_GETATTR_RES_MAXSZ	(CB_OP_HDR_RES_MAXSZ + \
				CB_OP_GETATTR_BITMAP_MAXSZ + \
				2 + 2 + 3 + 3)
#define CB_OP_RECALL_RES_MAXSZ	(CB_OP_HDR_RES_MAXSZ)

#define NFSDBG_FACILITY NFSDBG_CALLBACK

typedef unsigned (*callback_process_op_t)(void *, void *);
typedef unsigned (*callback_decode_arg_t)(struct svc_rqst *, struct xdr_stream *, void *);
typedef unsigned (*callback_encode_res_t)(struct svc_rqst *, struct xdr_stream *, void *);


struct callback_op {
	callback_process_op_t process_op;
	callback_decode_arg_t decode_args;
	callback_encode_res_t encode_res;
	long res_maxsize;
};

static struct callback_op callback_ops[];

static int nfs4_callback_null(struct svc_rqst *rqstp, void *argp, void *resp)
{
	return htonl(NFS4_OK);
}

static int nfs4_decode_void(struct svc_rqst *rqstp, uint32_t *p, void *dummy)
{
	return xdr_argsize_check(rqstp, p);
}

static int nfs4_encode_void(struct svc_rqst *rqstp, uint32_t *p, void *dummy)
{
	return xdr_ressize_check(rqstp, p);
}

static uint32_t *read_buf(struct xdr_stream *xdr, int nbytes)
{
	uint32_t *p;

	p = xdr_inline_decode(xdr, nbytes);
	if (unlikely(p == NULL))
		printk(KERN_WARNING "NFSv4 callback reply buffer overflowed!\n");
	return p;
}

static unsigned decode_string(struct xdr_stream *xdr, unsigned int *len, const char **str)
{
	uint32_t *p;

	p = read_buf(xdr, 4);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_RESOURCE);
	*len = ntohl(*p);

	if (*len != 0) {
		p = read_buf(xdr, *len);
		if (unlikely(p == NULL))
			return htonl(NFS4ERR_RESOURCE);
		*str = (const char *)p;
	} else
		*str = NULL;

	return 0;
}

static unsigned decode_fh(struct xdr_stream *xdr, struct nfs_fh *fh)
{
	uint32_t *p;

	p = read_buf(xdr, 4);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_RESOURCE);
	fh->size = ntohl(*p);
	if (fh->size > NFS4_FHSIZE)
		return htonl(NFS4ERR_BADHANDLE);
	p = read_buf(xdr, fh->size);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_RESOURCE);
	memcpy(&fh->data[0], p, fh->size);
	memset(&fh->data[fh->size], 0, sizeof(fh->data) - fh->size);
	return 0;
}

static unsigned decode_bitmap(struct xdr_stream *xdr, uint32_t *bitmap)
{
	uint32_t *p;
	unsigned int attrlen;

	p = read_buf(xdr, 4);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_RESOURCE);
	attrlen = ntohl(*p);
	p = read_buf(xdr, attrlen << 2);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_RESOURCE);
	if (likely(attrlen > 0))
		bitmap[0] = ntohl(*p++);
	if (attrlen > 1)
		bitmap[1] = ntohl(*p);
	return 0;
}

static unsigned decode_stateid(struct xdr_stream *xdr, nfs4_stateid *stateid)
{
	uint32_t *p;

	p = read_buf(xdr, 16);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_RESOURCE);
	memcpy(stateid->data, p, 16);
	return 0;
}

static unsigned decode_compound_hdr_arg(struct xdr_stream *xdr, struct cb_compound_hdr_arg *hdr)
{
	uint32_t *p;
	unsigned int minor_version;
	unsigned status;

	status = decode_string(xdr, &hdr->taglen, &hdr->tag);
	if (unlikely(status != 0))
		return status;
	/* We do not like overly long tags! */
	if (hdr->taglen > CB_OP_TAGLEN_MAXSZ-12 || hdr->taglen < 0) {
		printk("NFSv4 CALLBACK %s: client sent tag of length %u\n",
				__FUNCTION__, hdr->taglen);
		return htonl(NFS4ERR_RESOURCE);
	}
	p = read_buf(xdr, 12);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_RESOURCE);
	minor_version = ntohl(*p++);
	/* Check minor version is zero. */
	if (minor_version != 0) {
		printk(KERN_WARNING "%s: NFSv4 server callback with illegal minor version %u!\n",
				__FUNCTION__, minor_version);
		return htonl(NFS4ERR_MINOR_VERS_MISMATCH);
	}
	hdr->callback_ident = ntohl(*p++);
	hdr->nops = ntohl(*p);
	return 0;
}

static unsigned decode_op_hdr(struct xdr_stream *xdr, unsigned int *op)
{
	uint32_t *p;
	p = read_buf(xdr, 4);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_RESOURCE);
	*op = ntohl(*p);
	return 0;
}

static unsigned decode_getattr_args(struct svc_rqst *rqstp, struct xdr_stream *xdr, struct cb_getattrargs *args)
{
	unsigned status;

	status = decode_fh(xdr, &args->fh);
	if (unlikely(status != 0))
		goto out;
	args->addr = &rqstp->rq_addr;
	status = decode_bitmap(xdr, args->bitmap);
out:
	dprintk("%s: exit with status = %d\n", __FUNCTION__, status);
	return status;
}

static unsigned decode_recall_args(struct svc_rqst *rqstp, struct xdr_stream *xdr, struct cb_recallargs *args)
{
	uint32_t *p;
	unsigned status;

	args->addr = &rqstp->rq_addr;
	status = decode_stateid(xdr, &args->stateid);
	if (unlikely(status != 0))
		goto out;
	p = read_buf(xdr, 4);
	if (unlikely(p == NULL)) {
		status = htonl(NFS4ERR_RESOURCE);
		goto out;
	}
	args->truncate = ntohl(*p);
	status = decode_fh(xdr, &args->fh);
out:
	dprintk("%s: exit with status = %d\n", __FUNCTION__, status);
	return 0;
}

static unsigned encode_string(struct xdr_stream *xdr, unsigned int len, const char *str)
{
	uint32_t *p;

	p = xdr_reserve_space(xdr, 4 + len);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_RESOURCE);
	xdr_encode_opaque(p, str, len);
	return 0;
}

#define CB_SUPPORTED_ATTR0 (FATTR4_WORD0_CHANGE|FATTR4_WORD0_SIZE)
#define CB_SUPPORTED_ATTR1 (FATTR4_WORD1_TIME_METADATA|FATTR4_WORD1_TIME_MODIFY)
static unsigned encode_attr_bitmap(struct xdr_stream *xdr, const uint32_t *bitmap, uint32_t **savep)
{
	uint32_t bm[2];
	uint32_t *p;

	bm[0] = htonl(bitmap[0] & CB_SUPPORTED_ATTR0);
	bm[1] = htonl(bitmap[1] & CB_SUPPORTED_ATTR1);
	if (bm[1] != 0) {
		p = xdr_reserve_space(xdr, 16);
		if (unlikely(p == NULL))
			return htonl(NFS4ERR_RESOURCE);
		*p++ = htonl(2);
		*p++ = bm[0];
		*p++ = bm[1];
	} else if (bm[0] != 0) {
		p = xdr_reserve_space(xdr, 12);
		if (unlikely(p == NULL))
			return htonl(NFS4ERR_RESOURCE);
		*p++ = htonl(1);
		*p++ = bm[0];
	} else {
		p = xdr_reserve_space(xdr, 8);
		if (unlikely(p == NULL))
			return htonl(NFS4ERR_RESOURCE);
		*p++ = htonl(0);
	}
	*savep = p;
	return 0;
}

static unsigned encode_attr_change(struct xdr_stream *xdr, const uint32_t *bitmap, uint64_t change)
{
	uint32_t *p;

	if (!(bitmap[0] & FATTR4_WORD0_CHANGE))
		return 0;
	p = xdr_reserve_space(xdr, 8);
	if (unlikely(p == 0))
		return htonl(NFS4ERR_RESOURCE);
	p = xdr_encode_hyper(p, change);
	return 0;
}

static unsigned encode_attr_size(struct xdr_stream *xdr, const uint32_t *bitmap, uint64_t size)
{
	uint32_t *p;

	if (!(bitmap[0] & FATTR4_WORD0_SIZE))
		return 0;
	p = xdr_reserve_space(xdr, 8);
	if (unlikely(p == 0))
		return htonl(NFS4ERR_RESOURCE);
	p = xdr_encode_hyper(p, size);
	return 0;
}

static unsigned encode_attr_time(struct xdr_stream *xdr, const struct timespec *time)
{
	uint32_t *p;

	p = xdr_reserve_space(xdr, 12);
	if (unlikely(p == 0))
		return htonl(NFS4ERR_RESOURCE);
	p = xdr_encode_hyper(p, time->tv_sec);
	*p = htonl(time->tv_nsec);
	return 0;
}

static unsigned encode_attr_ctime(struct xdr_stream *xdr, const uint32_t *bitmap, const struct timespec *time)
{
	if (!(bitmap[1] & FATTR4_WORD1_TIME_METADATA))
		return 0;
	return encode_attr_time(xdr,time);
}

static unsigned encode_attr_mtime(struct xdr_stream *xdr, const uint32_t *bitmap, const struct timespec *time)
{
	if (!(bitmap[1] & FATTR4_WORD1_TIME_MODIFY))
		return 0;
	return encode_attr_time(xdr,time);
}

static unsigned encode_compound_hdr_res(struct xdr_stream *xdr, struct cb_compound_hdr_res *hdr)
{
	unsigned status;

	hdr->status = xdr_reserve_space(xdr, 4);
	if (unlikely(hdr->status == NULL))
		return htonl(NFS4ERR_RESOURCE);
	status = encode_string(xdr, hdr->taglen, hdr->tag);
	if (unlikely(status != 0))
		return status;
	hdr->nops = xdr_reserve_space(xdr, 4);
	if (unlikely(hdr->nops == NULL))
		return htonl(NFS4ERR_RESOURCE);
	return 0;
}

static unsigned encode_op_hdr(struct xdr_stream *xdr, uint32_t op, uint32_t res)
{
	uint32_t *p;
	
	p = xdr_reserve_space(xdr, 8);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_RESOURCE);
	*p++ = htonl(op);
	*p = res;
	return 0;
}

static unsigned encode_getattr_res(struct svc_rqst *rqstp, struct xdr_stream *xdr, const struct cb_getattrres *res)
{
	uint32_t *savep;
	unsigned status = res->status;
	
	if (unlikely(status != 0))
		goto out;
	status = encode_attr_bitmap(xdr, res->bitmap, &savep);
	if (unlikely(status != 0))
		goto out;
	status = encode_attr_change(xdr, res->bitmap, res->change_attr);
	if (unlikely(status != 0))
		goto out;
	status = encode_attr_size(xdr, res->bitmap, res->size);
	if (unlikely(status != 0))
		goto out;
	status = encode_attr_ctime(xdr, res->bitmap, &res->ctime);
	if (unlikely(status != 0))
		goto out;
	status = encode_attr_mtime(xdr, res->bitmap, &res->mtime);
	*savep = htonl((unsigned int)((char *)xdr->p - (char *)(savep+1)));
out:
	dprintk("%s: exit with status = %d\n", __FUNCTION__, status);
	return status;
}

static unsigned process_op(struct svc_rqst *rqstp,
		struct xdr_stream *xdr_in, void *argp,
		struct xdr_stream *xdr_out, void *resp)
{
	struct callback_op *op;
	unsigned int op_nr;
	unsigned int status = 0;
	long maxlen;
	unsigned res;

	dprintk("%s: start\n", __FUNCTION__);
	status = decode_op_hdr(xdr_in, &op_nr);
	if (unlikely(status != 0)) {
		op_nr = OP_CB_ILLEGAL;
		op = &callback_ops[0];
	} else if (unlikely(op_nr != OP_CB_GETATTR && op_nr != OP_CB_RECALL)) {
		op_nr = OP_CB_ILLEGAL;
		op = &callback_ops[0];
		status = htonl(NFS4ERR_OP_ILLEGAL);
	} else
		op = &callback_ops[op_nr];

	maxlen = xdr_out->end - xdr_out->p;
	if (maxlen > 0 && maxlen < PAGE_SIZE) {
		if (likely(status == 0 && op->decode_args != NULL))
			status = op->decode_args(rqstp, xdr_in, argp);
		if (likely(status == 0 && op->process_op != NULL))
			status = op->process_op(argp, resp);
	} else
		status = htonl(NFS4ERR_RESOURCE);

	res = encode_op_hdr(xdr_out, op_nr, status);
	if (status == 0)
		status = res;
	if (op->encode_res != NULL && status == 0)
		status = op->encode_res(rqstp, xdr_out, resp);
	dprintk("%s: done, status = %d\n", __FUNCTION__, status);
	return status;
}

/*
 * Decode, process and encode a COMPOUND
 */
static int nfs4_callback_compound(struct svc_rqst *rqstp, void *argp, void *resp)
{
	struct cb_compound_hdr_arg hdr_arg;
	struct cb_compound_hdr_res hdr_res;
	struct xdr_stream xdr_in, xdr_out;
	uint32_t *p;
	unsigned int status;
	unsigned int nops = 1;

	dprintk("%s: start\n", __FUNCTION__);

	xdr_init_decode(&xdr_in, &rqstp->rq_arg, rqstp->rq_arg.head[0].iov_base);

	p = (uint32_t*)((char *)rqstp->rq_res.head[0].iov_base + rqstp->rq_res.head[0].iov_len);
	xdr_init_encode(&xdr_out, &rqstp->rq_res, p);

	decode_compound_hdr_arg(&xdr_in, &hdr_arg);
	hdr_res.taglen = hdr_arg.taglen;
	hdr_res.tag = hdr_arg.tag;
	encode_compound_hdr_res(&xdr_out, &hdr_res);

	for (;;) {
		status = process_op(rqstp, &xdr_in, argp, &xdr_out, resp);
		if (status != 0)
			break;
		if (nops == hdr_arg.nops)
			break;
		nops++;
	}
	*hdr_res.status = status;
	*hdr_res.nops = htonl(nops);
	dprintk("%s: done, status = %u\n", __FUNCTION__, status);
	return rpc_success;
}

/*
 * Define NFS4 callback COMPOUND ops.
 */
static struct callback_op callback_ops[] = {
	[0] = {
		.res_maxsize = CB_OP_HDR_RES_MAXSZ,
	},
	[OP_CB_GETATTR] = {
		.process_op = (callback_process_op_t)nfs4_callback_getattr,
		.decode_args = (callback_decode_arg_t)decode_getattr_args,
		.encode_res = (callback_encode_res_t)encode_getattr_res,
		.res_maxsize = CB_OP_GETATTR_RES_MAXSZ,
	},
	[OP_CB_RECALL] = {
		.process_op = (callback_process_op_t)nfs4_callback_recall,
		.decode_args = (callback_decode_arg_t)decode_recall_args,
		.res_maxsize = CB_OP_RECALL_RES_MAXSZ,
	}
};

/*
 * Define NFS4 callback procedures
 */
static struct svc_procedure nfs4_callback_procedures1[] = {
	[CB_NULL] = {
		.pc_func = nfs4_callback_null,
		.pc_decode = (kxdrproc_t)nfs4_decode_void,
		.pc_encode = (kxdrproc_t)nfs4_encode_void,
		.pc_xdrressize = 1,
	},
	[CB_COMPOUND] = {
		.pc_func = nfs4_callback_compound,
		.pc_encode = (kxdrproc_t)nfs4_encode_void,
		.pc_argsize = 256,
		.pc_ressize = 256,
		.pc_xdrressize = NFS4_CALLBACK_BUFSIZE,
	}
};

struct svc_version nfs4_callback_version1 = {
	.vs_vers = 1,
	.vs_nproc = ARRAY_SIZE(nfs4_callback_procedures1),
	.vs_proc = nfs4_callback_procedures1,
	.vs_xdrsize = NFS4_CALLBACK_XDRSIZE,
	.vs_dispatch = NULL,
};


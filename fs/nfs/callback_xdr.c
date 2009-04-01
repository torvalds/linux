/*
 * linux/fs/nfs/callback_xdr.c
 *
 * Copyright (C) 2004 Trond Myklebust
 *
 * NFSv4 callback encode/decode procedures
 */
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

#if defined(CONFIG_NFS_V4_1)
#define CB_OP_SEQUENCE_RES_MAXSZ	(CB_OP_HDR_RES_MAXSZ + \
					4 + 1 + 3)
#endif /* CONFIG_NFS_V4_1 */

#define NFSDBG_FACILITY NFSDBG_CALLBACK

typedef __be32 (*callback_process_op_t)(void *, void *);
typedef __be32 (*callback_decode_arg_t)(struct svc_rqst *, struct xdr_stream *, void *);
typedef __be32 (*callback_encode_res_t)(struct svc_rqst *, struct xdr_stream *, void *);


struct callback_op {
	callback_process_op_t process_op;
	callback_decode_arg_t decode_args;
	callback_encode_res_t encode_res;
	long res_maxsize;
};

static struct callback_op callback_ops[];

static __be32 nfs4_callback_null(struct svc_rqst *rqstp, void *argp, void *resp)
{
	return htonl(NFS4_OK);
}

static int nfs4_decode_void(struct svc_rqst *rqstp, __be32 *p, void *dummy)
{
	return xdr_argsize_check(rqstp, p);
}

static int nfs4_encode_void(struct svc_rqst *rqstp, __be32 *p, void *dummy)
{
	return xdr_ressize_check(rqstp, p);
}

static __be32 *read_buf(struct xdr_stream *xdr, int nbytes)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, nbytes);
	if (unlikely(p == NULL))
		printk(KERN_WARNING "NFSv4 callback reply buffer overflowed!\n");
	return p;
}

static __be32 decode_string(struct xdr_stream *xdr, unsigned int *len, const char **str)
{
	__be32 *p;

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

static __be32 decode_fh(struct xdr_stream *xdr, struct nfs_fh *fh)
{
	__be32 *p;

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

static __be32 decode_bitmap(struct xdr_stream *xdr, uint32_t *bitmap)
{
	__be32 *p;
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

static __be32 decode_stateid(struct xdr_stream *xdr, nfs4_stateid *stateid)
{
	__be32 *p;

	p = read_buf(xdr, 16);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_RESOURCE);
	memcpy(stateid->data, p, 16);
	return 0;
}

static __be32 decode_compound_hdr_arg(struct xdr_stream *xdr, struct cb_compound_hdr_arg *hdr)
{
	__be32 *p;
	__be32 status;

	status = decode_string(xdr, &hdr->taglen, &hdr->tag);
	if (unlikely(status != 0))
		return status;
	/* We do not like overly long tags! */
	if (hdr->taglen > CB_OP_TAGLEN_MAXSZ - 12) {
		printk("NFSv4 CALLBACK %s: client sent tag of length %u\n",
				__func__, hdr->taglen);
		return htonl(NFS4ERR_RESOURCE);
	}
	p = read_buf(xdr, 12);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_RESOURCE);
	hdr->minorversion = ntohl(*p++);
	/* Check minor version is zero or one. */
	if (hdr->minorversion <= 1) {
		p++;	/* skip callback_ident */
	} else {
		printk(KERN_WARNING "%s: NFSv4 server callback with "
			"illegal minor version %u!\n",
			__func__, hdr->minorversion);
		return htonl(NFS4ERR_MINOR_VERS_MISMATCH);
	}
	hdr->nops = ntohl(*p);
	dprintk("%s: minorversion %d nops %d\n", __func__,
		hdr->minorversion, hdr->nops);
	return 0;
}

static __be32 decode_op_hdr(struct xdr_stream *xdr, unsigned int *op)
{
	__be32 *p;
	p = read_buf(xdr, 4);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_RESOURCE);
	*op = ntohl(*p);
	return 0;
}

static __be32 decode_getattr_args(struct svc_rqst *rqstp, struct xdr_stream *xdr, struct cb_getattrargs *args)
{
	__be32 status;

	status = decode_fh(xdr, &args->fh);
	if (unlikely(status != 0))
		goto out;
	args->addr = svc_addr(rqstp);
	status = decode_bitmap(xdr, args->bitmap);
out:
	dprintk("%s: exit with status = %d\n", __func__, ntohl(status));
	return status;
}

static __be32 decode_recall_args(struct svc_rqst *rqstp, struct xdr_stream *xdr, struct cb_recallargs *args)
{
	__be32 *p;
	__be32 status;

	args->addr = svc_addr(rqstp);
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
	dprintk("%s: exit with status = %d\n", __func__, ntohl(status));
	return status;
}

#if defined(CONFIG_NFS_V4_1)

static unsigned decode_sessionid(struct xdr_stream *xdr,
				 struct nfs4_sessionid *sid)
{
	uint32_t *p;
	int len = NFS4_MAX_SESSIONID_LEN;

	p = read_buf(xdr, len);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_RESOURCE);;

	memcpy(sid->data, p, len);
	return 0;
}

static unsigned decode_rc_list(struct xdr_stream *xdr,
			       struct referring_call_list *rc_list)
{
	uint32_t *p;
	int i;
	unsigned status;

	status = decode_sessionid(xdr, &rc_list->rcl_sessionid);
	if (status)
		goto out;

	status = htonl(NFS4ERR_RESOURCE);
	p = read_buf(xdr, sizeof(uint32_t));
	if (unlikely(p == NULL))
		goto out;

	rc_list->rcl_nrefcalls = ntohl(*p++);
	if (rc_list->rcl_nrefcalls) {
		p = read_buf(xdr,
			     rc_list->rcl_nrefcalls * 2 * sizeof(uint32_t));
		if (unlikely(p == NULL))
			goto out;
		rc_list->rcl_refcalls = kmalloc(rc_list->rcl_nrefcalls *
						sizeof(*rc_list->rcl_refcalls),
						GFP_KERNEL);
		if (unlikely(rc_list->rcl_refcalls == NULL))
			goto out;
		for (i = 0; i < rc_list->rcl_nrefcalls; i++) {
			rc_list->rcl_refcalls[i].rc_sequenceid = ntohl(*p++);
			rc_list->rcl_refcalls[i].rc_slotid = ntohl(*p++);
		}
	}
	status = 0;

out:
	return status;
}

static unsigned decode_cb_sequence_args(struct svc_rqst *rqstp,
					struct xdr_stream *xdr,
					struct cb_sequenceargs *args)
{
	uint32_t *p;
	int i;
	unsigned status;

	status = decode_sessionid(xdr, &args->csa_sessionid);
	if (status)
		goto out;

	status = htonl(NFS4ERR_RESOURCE);
	p = read_buf(xdr, 5 * sizeof(uint32_t));
	if (unlikely(p == NULL))
		goto out;

	args->csa_addr = svc_addr(rqstp);
	args->csa_sequenceid = ntohl(*p++);
	args->csa_slotid = ntohl(*p++);
	args->csa_highestslotid = ntohl(*p++);
	args->csa_cachethis = ntohl(*p++);
	args->csa_nrclists = ntohl(*p++);
	args->csa_rclists = NULL;
	if (args->csa_nrclists) {
		args->csa_rclists = kmalloc(args->csa_nrclists *
					    sizeof(*args->csa_rclists),
					    GFP_KERNEL);
		if (unlikely(args->csa_rclists == NULL))
			goto out;

		for (i = 0; i < args->csa_nrclists; i++) {
			status = decode_rc_list(xdr, &args->csa_rclists[i]);
			if (status)
				goto out_free;
		}
	}
	status = 0;

	dprintk("%s: sessionid %x:%x:%x:%x sequenceid %u slotid %u "
		"highestslotid %u cachethis %d nrclists %u\n",
		__func__,
		((u32 *)&args->csa_sessionid)[0],
		((u32 *)&args->csa_sessionid)[1],
		((u32 *)&args->csa_sessionid)[2],
		((u32 *)&args->csa_sessionid)[3],
		args->csa_sequenceid, args->csa_slotid,
		args->csa_highestslotid, args->csa_cachethis,
		args->csa_nrclists);
out:
	dprintk("%s: exit with status = %d\n", __func__, ntohl(status));
	return status;

out_free:
	for (i = 0; i < args->csa_nrclists; i++)
		kfree(args->csa_rclists[i].rcl_refcalls);
	kfree(args->csa_rclists);
	goto out;
}

#endif /* CONFIG_NFS_V4_1 */

static __be32 encode_string(struct xdr_stream *xdr, unsigned int len, const char *str)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, 4 + len);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_RESOURCE);
	xdr_encode_opaque(p, str, len);
	return 0;
}

#define CB_SUPPORTED_ATTR0 (FATTR4_WORD0_CHANGE|FATTR4_WORD0_SIZE)
#define CB_SUPPORTED_ATTR1 (FATTR4_WORD1_TIME_METADATA|FATTR4_WORD1_TIME_MODIFY)
static __be32 encode_attr_bitmap(struct xdr_stream *xdr, const uint32_t *bitmap, __be32 **savep)
{
	__be32 bm[2];
	__be32 *p;

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

static __be32 encode_attr_change(struct xdr_stream *xdr, const uint32_t *bitmap, uint64_t change)
{
	__be32 *p;

	if (!(bitmap[0] & FATTR4_WORD0_CHANGE))
		return 0;
	p = xdr_reserve_space(xdr, 8);
	if (unlikely(!p))
		return htonl(NFS4ERR_RESOURCE);
	p = xdr_encode_hyper(p, change);
	return 0;
}

static __be32 encode_attr_size(struct xdr_stream *xdr, const uint32_t *bitmap, uint64_t size)
{
	__be32 *p;

	if (!(bitmap[0] & FATTR4_WORD0_SIZE))
		return 0;
	p = xdr_reserve_space(xdr, 8);
	if (unlikely(!p))
		return htonl(NFS4ERR_RESOURCE);
	p = xdr_encode_hyper(p, size);
	return 0;
}

static __be32 encode_attr_time(struct xdr_stream *xdr, const struct timespec *time)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, 12);
	if (unlikely(!p))
		return htonl(NFS4ERR_RESOURCE);
	p = xdr_encode_hyper(p, time->tv_sec);
	*p = htonl(time->tv_nsec);
	return 0;
}

static __be32 encode_attr_ctime(struct xdr_stream *xdr, const uint32_t *bitmap, const struct timespec *time)
{
	if (!(bitmap[1] & FATTR4_WORD1_TIME_METADATA))
		return 0;
	return encode_attr_time(xdr,time);
}

static __be32 encode_attr_mtime(struct xdr_stream *xdr, const uint32_t *bitmap, const struct timespec *time)
{
	if (!(bitmap[1] & FATTR4_WORD1_TIME_MODIFY))
		return 0;
	return encode_attr_time(xdr,time);
}

static __be32 encode_compound_hdr_res(struct xdr_stream *xdr, struct cb_compound_hdr_res *hdr)
{
	__be32 status;

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

static __be32 encode_op_hdr(struct xdr_stream *xdr, uint32_t op, __be32 res)
{
	__be32 *p;
	
	p = xdr_reserve_space(xdr, 8);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_RESOURCE);
	*p++ = htonl(op);
	*p = res;
	return 0;
}

static __be32 encode_getattr_res(struct svc_rqst *rqstp, struct xdr_stream *xdr, const struct cb_getattrres *res)
{
	__be32 *savep = NULL;
	__be32 status = res->status;
	
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
	dprintk("%s: exit with status = %d\n", __func__, ntohl(status));
	return status;
}

#if defined(CONFIG_NFS_V4_1)

static unsigned encode_sessionid(struct xdr_stream *xdr,
				 const struct nfs4_sessionid *sid)
{
	uint32_t *p;
	int len = NFS4_MAX_SESSIONID_LEN;

	p = xdr_reserve_space(xdr, len);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_RESOURCE);

	memcpy(p, sid, len);
	return 0;
}

static unsigned encode_cb_sequence_res(struct svc_rqst *rqstp,
				       struct xdr_stream *xdr,
				       const struct cb_sequenceres *res)
{
	uint32_t *p;
	unsigned status = res->csr_status;

	if (unlikely(status != 0))
		goto out;

	encode_sessionid(xdr, &res->csr_sessionid);

	p = xdr_reserve_space(xdr, 4 * sizeof(uint32_t));
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_RESOURCE);

	*p++ = htonl(res->csr_sequenceid);
	*p++ = htonl(res->csr_slotid);
	*p++ = htonl(res->csr_highestslotid);
	*p++ = htonl(res->csr_target_highestslotid);
out:
	dprintk("%s: exit with status = %d\n", __func__, ntohl(status));
	return status;
}

static __be32
preprocess_nfs41_op(int nop, unsigned int op_nr, struct callback_op **op)
{
	if (op_nr == OP_CB_SEQUENCE) {
		if (nop != 0)
			return htonl(NFS4ERR_SEQUENCE_POS);
	} else {
		if (nop == 0)
			return htonl(NFS4ERR_OP_NOT_IN_SESSION);
	}

	switch (op_nr) {
	case OP_CB_GETATTR:
	case OP_CB_RECALL:
	case OP_CB_SEQUENCE:
		*op = &callback_ops[op_nr];
		break;

	case OP_CB_LAYOUTRECALL:
	case OP_CB_NOTIFY_DEVICEID:
	case OP_CB_NOTIFY:
	case OP_CB_PUSH_DELEG:
	case OP_CB_RECALL_ANY:
	case OP_CB_RECALLABLE_OBJ_AVAIL:
	case OP_CB_RECALL_SLOT:
	case OP_CB_WANTS_CANCELLED:
	case OP_CB_NOTIFY_LOCK:
		return htonl(NFS4ERR_NOTSUPP);

	default:
		return htonl(NFS4ERR_OP_ILLEGAL);
	}

	return htonl(NFS_OK);
}

#else /* CONFIG_NFS_V4_1 */

static __be32
preprocess_nfs41_op(int nop, unsigned int op_nr, struct callback_op **op)
{
	return htonl(NFS4ERR_MINOR_VERS_MISMATCH);
}

#endif /* CONFIG_NFS_V4_1 */

static __be32
preprocess_nfs4_op(unsigned int op_nr, struct callback_op **op)
{
	switch (op_nr) {
	case OP_CB_GETATTR:
	case OP_CB_RECALL:
		*op = &callback_ops[op_nr];
		break;
	default:
		return htonl(NFS4ERR_OP_ILLEGAL);
	}

	return htonl(NFS_OK);
}

static __be32 process_op(uint32_t minorversion, int nop,
		struct svc_rqst *rqstp,
		struct xdr_stream *xdr_in, void *argp,
		struct xdr_stream *xdr_out, void *resp)
{
	struct callback_op *op = &callback_ops[0];
	unsigned int op_nr = OP_CB_ILLEGAL;
	__be32 status;
	long maxlen;
	__be32 res;

	dprintk("%s: start\n", __func__);
	status = decode_op_hdr(xdr_in, &op_nr);
	if (unlikely(status)) {
		status = htonl(NFS4ERR_OP_ILLEGAL);
		goto out;
	}

	dprintk("%s: minorversion=%d nop=%d op_nr=%u\n",
		__func__, minorversion, nop, op_nr);

	status = minorversion ? preprocess_nfs41_op(nop, op_nr, &op) :
				preprocess_nfs4_op(op_nr, &op);
	if (status == htonl(NFS4ERR_OP_ILLEGAL))
		op_nr = OP_CB_ILLEGAL;
out:
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
	dprintk("%s: done, status = %d\n", __func__, ntohl(status));
	return status;
}

/*
 * Decode, process and encode a COMPOUND
 */
static __be32 nfs4_callback_compound(struct svc_rqst *rqstp, void *argp, void *resp)
{
	struct cb_compound_hdr_arg hdr_arg = { 0 };
	struct cb_compound_hdr_res hdr_res = { NULL };
	struct xdr_stream xdr_in, xdr_out;
	__be32 *p;
	__be32 status;
	unsigned int nops = 0;

	dprintk("%s: start\n", __func__);

	xdr_init_decode(&xdr_in, &rqstp->rq_arg, rqstp->rq_arg.head[0].iov_base);

	p = (__be32*)((char *)rqstp->rq_res.head[0].iov_base + rqstp->rq_res.head[0].iov_len);
	xdr_init_encode(&xdr_out, &rqstp->rq_res, p);

	status = decode_compound_hdr_arg(&xdr_in, &hdr_arg);
	if (status == __constant_htonl(NFS4ERR_RESOURCE))
		return rpc_garbage_args;

	hdr_res.taglen = hdr_arg.taglen;
	hdr_res.tag = hdr_arg.tag;
	if (encode_compound_hdr_res(&xdr_out, &hdr_res) != 0)
		return rpc_system_err;

	while (status == 0 && nops != hdr_arg.nops) {
		status = process_op(hdr_arg.minorversion, nops,
				    rqstp, &xdr_in, argp, &xdr_out, resp);
		nops++;
	}

	*hdr_res.status = status;
	*hdr_res.nops = htonl(nops);
	dprintk("%s: done, status = %u\n", __func__, ntohl(status));
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
	},
#if defined(CONFIG_NFS_V4_1)
	[OP_CB_SEQUENCE] = {
		.process_op = (callback_process_op_t)nfs4_callback_sequence,
		.decode_args = (callback_decode_arg_t)decode_cb_sequence_args,
		.encode_res = (callback_encode_res_t)encode_cb_sequence_res,
		.res_maxsize = CB_OP_SEQUENCE_RES_MAXSZ,
	},
#endif /* CONFIG_NFS_V4_1 */
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


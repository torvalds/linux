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
#include <linux/slab.h>
#include <linux/sunrpc/bc_xprt.h>
#include "nfs4_fs.h"
#include "callback.h"
#include "internal.h"

#define CB_OP_TAGLEN_MAXSZ	(512)
#define CB_OP_HDR_RES_MAXSZ	(2 + CB_OP_TAGLEN_MAXSZ)
#define CB_OP_GETATTR_BITMAP_MAXSZ	(4)
#define CB_OP_GETATTR_RES_MAXSZ	(CB_OP_HDR_RES_MAXSZ + \
				CB_OP_GETATTR_BITMAP_MAXSZ + \
				2 + 2 + 3 + 3)
#define CB_OP_RECALL_RES_MAXSZ	(CB_OP_HDR_RES_MAXSZ)

#if defined(CONFIG_NFS_V4_1)
#define CB_OP_LAYOUTRECALL_RES_MAXSZ	(CB_OP_HDR_RES_MAXSZ)
#define CB_OP_SEQUENCE_RES_MAXSZ	(CB_OP_HDR_RES_MAXSZ + \
					4 + 1 + 3)
#define CB_OP_RECALLANY_RES_MAXSZ	(CB_OP_HDR_RES_MAXSZ)
#define CB_OP_RECALLSLOT_RES_MAXSZ	(CB_OP_HDR_RES_MAXSZ)
#endif /* CONFIG_NFS_V4_1 */

#define NFSDBG_FACILITY NFSDBG_CALLBACK

/* Internal error code */
#define NFS4ERR_RESOURCE_HDR	11050

typedef __be32 (*callback_process_op_t)(void *, void *,
					struct cb_process_state *);
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
		hdr->cb_ident = ntohl(*p++); /* ignored by v4.1 */
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
		return htonl(NFS4ERR_RESOURCE_HDR);
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

static __be32 decode_layoutrecall_args(struct svc_rqst *rqstp,
				       struct xdr_stream *xdr,
				       struct cb_layoutrecallargs *args)
{
	__be32 *p;
	__be32 status = 0;
	uint32_t iomode;

	args->cbl_addr = svc_addr(rqstp);
	p = read_buf(xdr, 4 * sizeof(uint32_t));
	if (unlikely(p == NULL)) {
		status = htonl(NFS4ERR_BADXDR);
		goto out;
	}

	args->cbl_layout_type = ntohl(*p++);
	/* Depite the spec's xdr, iomode really belongs in the FILE switch,
	 * as it is unuseable and ignored with the other types.
	 */
	iomode = ntohl(*p++);
	args->cbl_layoutchanged = ntohl(*p++);
	args->cbl_recall_type = ntohl(*p++);

	if (args->cbl_recall_type == RETURN_FILE) {
		args->cbl_range.iomode = iomode;
		status = decode_fh(xdr, &args->cbl_fh);
		if (unlikely(status != 0))
			goto out;

		p = read_buf(xdr, 2 * sizeof(uint64_t));
		if (unlikely(p == NULL)) {
			status = htonl(NFS4ERR_BADXDR);
			goto out;
		}
		p = xdr_decode_hyper(p, &args->cbl_range.offset);
		p = xdr_decode_hyper(p, &args->cbl_range.length);
		status = decode_stateid(xdr, &args->cbl_stateid);
		if (unlikely(status != 0))
			goto out;
	} else if (args->cbl_recall_type == RETURN_FSID) {
		p = read_buf(xdr, 2 * sizeof(uint64_t));
		if (unlikely(p == NULL)) {
			status = htonl(NFS4ERR_BADXDR);
			goto out;
		}
		p = xdr_decode_hyper(p, &args->cbl_fsid.major);
		p = xdr_decode_hyper(p, &args->cbl_fsid.minor);
	} else if (args->cbl_recall_type != RETURN_ALL) {
		status = htonl(NFS4ERR_BADXDR);
		goto out;
	}
	dprintk("%s: ltype 0x%x iomode %d changed %d recall_type %d\n",
		__func__,
		args->cbl_layout_type, iomode,
		args->cbl_layoutchanged, args->cbl_recall_type);
out:
	dprintk("%s: exit with status = %d\n", __func__, ntohl(status));
	return status;
}

static __be32 decode_sessionid(struct xdr_stream *xdr,
				 struct nfs4_sessionid *sid)
{
	__be32 *p;
	int len = NFS4_MAX_SESSIONID_LEN;

	p = read_buf(xdr, len);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_RESOURCE);

	memcpy(sid->data, p, len);
	return 0;
}

static __be32 decode_rc_list(struct xdr_stream *xdr,
			       struct referring_call_list *rc_list)
{
	__be32 *p;
	int i;
	__be32 status;

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

static __be32 decode_cb_sequence_args(struct svc_rqst *rqstp,
					struct xdr_stream *xdr,
					struct cb_sequenceargs *args)
{
	__be32 *p;
	int i;
	__be32 status;

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

static __be32 decode_recallany_args(struct svc_rqst *rqstp,
				      struct xdr_stream *xdr,
				      struct cb_recallanyargs *args)
{
	__be32 *p;

	args->craa_addr = svc_addr(rqstp);
	p = read_buf(xdr, 4);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_BADXDR);
	args->craa_objs_to_keep = ntohl(*p++);
	p = read_buf(xdr, 4);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_BADXDR);
	args->craa_type_mask = ntohl(*p);

	return 0;
}

static __be32 decode_recallslot_args(struct svc_rqst *rqstp,
					struct xdr_stream *xdr,
					struct cb_recallslotargs *args)
{
	__be32 *p;

	args->crsa_addr = svc_addr(rqstp);
	p = read_buf(xdr, 4);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_BADXDR);
	args->crsa_target_max_slots = ntohl(*p++);
	return 0;
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
		return htonl(NFS4ERR_RESOURCE_HDR);
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

static __be32 encode_sessionid(struct xdr_stream *xdr,
				 const struct nfs4_sessionid *sid)
{
	__be32 *p;
	int len = NFS4_MAX_SESSIONID_LEN;

	p = xdr_reserve_space(xdr, len);
	if (unlikely(p == NULL))
		return htonl(NFS4ERR_RESOURCE);

	memcpy(p, sid, len);
	return 0;
}

static __be32 encode_cb_sequence_res(struct svc_rqst *rqstp,
				       struct xdr_stream *xdr,
				       const struct cb_sequenceres *res)
{
	__be32 *p;
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
	case OP_CB_RECALL_ANY:
	case OP_CB_RECALL_SLOT:
	case OP_CB_LAYOUTRECALL:
		*op = &callback_ops[op_nr];
		break;

	case OP_CB_NOTIFY_DEVICEID:
	case OP_CB_NOTIFY:
	case OP_CB_PUSH_DELEG:
	case OP_CB_RECALLABLE_OBJ_AVAIL:
	case OP_CB_WANTS_CANCELLED:
	case OP_CB_NOTIFY_LOCK:
		return htonl(NFS4ERR_NOTSUPP);

	default:
		return htonl(NFS4ERR_OP_ILLEGAL);
	}

	return htonl(NFS_OK);
}

static void nfs4_callback_free_slot(struct nfs4_session *session)
{
	struct nfs4_slot_table *tbl = &session->bc_slot_table;

	spin_lock(&tbl->slot_tbl_lock);
	/*
	 * Let the state manager know callback processing done.
	 * A single slot, so highest used slotid is either 0 or -1
	 */
	tbl->highest_used_slotid--;
	nfs4_check_drain_bc_complete(session);
	spin_unlock(&tbl->slot_tbl_lock);
}

static void nfs4_cb_free_slot(struct nfs_client *clp)
{
	if (clp && clp->cl_session)
		nfs4_callback_free_slot(clp->cl_session);
}

/* A single slot, so highest used slotid is either 0 or -1 */
void nfs4_cb_take_slot(struct nfs_client *clp)
{
	struct nfs4_slot_table *tbl = &clp->cl_session->bc_slot_table;

	spin_lock(&tbl->slot_tbl_lock);
	tbl->highest_used_slotid++;
	BUG_ON(tbl->highest_used_slotid != 0);
	spin_unlock(&tbl->slot_tbl_lock);
}

#else /* CONFIG_NFS_V4_1 */

static __be32
preprocess_nfs41_op(int nop, unsigned int op_nr, struct callback_op **op)
{
	return htonl(NFS4ERR_MINOR_VERS_MISMATCH);
}

static void nfs4_cb_free_slot(struct nfs_client *clp)
{
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
		struct xdr_stream *xdr_out, void *resp,
		struct cb_process_state *cps)
{
	struct callback_op *op = &callback_ops[0];
	unsigned int op_nr;
	__be32 status;
	long maxlen;
	__be32 res;

	dprintk("%s: start\n", __func__);
	status = decode_op_hdr(xdr_in, &op_nr);
	if (unlikely(status))
		return status;

	dprintk("%s: minorversion=%d nop=%d op_nr=%u\n",
		__func__, minorversion, nop, op_nr);

	status = minorversion ? preprocess_nfs41_op(nop, op_nr, &op) :
				preprocess_nfs4_op(op_nr, &op);
	if (status == htonl(NFS4ERR_OP_ILLEGAL))
		op_nr = OP_CB_ILLEGAL;
	if (status)
		goto encode_hdr;

	if (cps->drc_status) {
		status = cps->drc_status;
		goto encode_hdr;
	}

	maxlen = xdr_out->end - xdr_out->p;
	if (maxlen > 0 && maxlen < PAGE_SIZE) {
		status = op->decode_args(rqstp, xdr_in, argp);
		if (likely(status == 0))
			status = op->process_op(argp, resp, cps);
	} else
		status = htonl(NFS4ERR_RESOURCE);

encode_hdr:
	res = encode_op_hdr(xdr_out, op_nr, status);
	if (unlikely(res))
		return res;
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
	__be32 *p, status;
	struct cb_process_state cps = {
		.drc_status = 0,
		.clp = NULL,
	};
	unsigned int nops = 0;

	dprintk("%s: start\n", __func__);

	xdr_init_decode(&xdr_in, &rqstp->rq_arg, rqstp->rq_arg.head[0].iov_base);

	p = (__be32*)((char *)rqstp->rq_res.head[0].iov_base + rqstp->rq_res.head[0].iov_len);
	xdr_init_encode(&xdr_out, &rqstp->rq_res, p);

	status = decode_compound_hdr_arg(&xdr_in, &hdr_arg);
	if (status == __constant_htonl(NFS4ERR_RESOURCE))
		return rpc_garbage_args;

	if (hdr_arg.minorversion == 0) {
		cps.clp = nfs4_find_client_ident(hdr_arg.cb_ident);
		if (!cps.clp)
			return rpc_drop_reply;
	} else
		cps.svc_sid = bc_xprt_sid(rqstp);

	hdr_res.taglen = hdr_arg.taglen;
	hdr_res.tag = hdr_arg.tag;
	if (encode_compound_hdr_res(&xdr_out, &hdr_res) != 0)
		return rpc_system_err;

	while (status == 0 && nops != hdr_arg.nops) {
		status = process_op(hdr_arg.minorversion, nops, rqstp,
				    &xdr_in, argp, &xdr_out, resp, &cps);
		nops++;
	}

	/* Buffer overflow in decode_ops_hdr or encode_ops_hdr. Return
	* resource error in cb_compound status without returning op */
	if (unlikely(status == htonl(NFS4ERR_RESOURCE_HDR))) {
		status = htonl(NFS4ERR_RESOURCE);
		nops--;
	}

	*hdr_res.status = status;
	*hdr_res.nops = htonl(nops);
	nfs4_cb_free_slot(cps.clp);
	nfs_put_client(cps.clp);
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
	[OP_CB_LAYOUTRECALL] = {
		.process_op = (callback_process_op_t)nfs4_callback_layoutrecall,
		.decode_args =
			(callback_decode_arg_t)decode_layoutrecall_args,
		.res_maxsize = CB_OP_LAYOUTRECALL_RES_MAXSZ,
	},
	[OP_CB_SEQUENCE] = {
		.process_op = (callback_process_op_t)nfs4_callback_sequence,
		.decode_args = (callback_decode_arg_t)decode_cb_sequence_args,
		.encode_res = (callback_encode_res_t)encode_cb_sequence_res,
		.res_maxsize = CB_OP_SEQUENCE_RES_MAXSZ,
	},
	[OP_CB_RECALL_ANY] = {
		.process_op = (callback_process_op_t)nfs4_callback_recallany,
		.decode_args = (callback_decode_arg_t)decode_recallany_args,
		.res_maxsize = CB_OP_RECALLANY_RES_MAXSZ,
	},
	[OP_CB_RECALL_SLOT] = {
		.process_op = (callback_process_op_t)nfs4_callback_recallslot,
		.decode_args = (callback_decode_arg_t)decode_recallslot_args,
		.res_maxsize = CB_OP_RECALLSLOT_RES_MAXSZ,
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
	.vs_hidden = 1,
};

struct svc_version nfs4_callback_version4 = {
	.vs_vers = 4,
	.vs_nproc = ARRAY_SIZE(nfs4_callback_procedures1),
	.vs_proc = nfs4_callback_procedures1,
	.vs_xdrsize = NFS4_CALLBACK_XDRSIZE,
	.vs_dispatch = NULL,
};

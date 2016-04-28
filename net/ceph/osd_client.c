
#include <linux/ceph/ceph_debug.h>

#include <linux/module.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#ifdef CONFIG_BLOCK
#include <linux/bio.h>
#endif

#include <linux/ceph/libceph.h>
#include <linux/ceph/osd_client.h>
#include <linux/ceph/messenger.h>
#include <linux/ceph/decode.h>
#include <linux/ceph/auth.h>
#include <linux/ceph/pagelist.h>

#define OSD_OPREPLY_FRONT_LEN	512

static struct kmem_cache	*ceph_osd_request_cache;

static const struct ceph_connection_operations osd_con_ops;

static void __send_queued(struct ceph_osd_client *osdc);
static int __reset_osd(struct ceph_osd_client *osdc, struct ceph_osd *osd);
static void __register_request(struct ceph_osd_client *osdc,
			       struct ceph_osd_request *req);
static void __unregister_request(struct ceph_osd_client *osdc,
				 struct ceph_osd_request *req);
static void __unregister_linger_request(struct ceph_osd_client *osdc,
					struct ceph_osd_request *req);
static void __enqueue_request(struct ceph_osd_request *req);

/*
 * Implement client access to distributed object storage cluster.
 *
 * All data objects are stored within a cluster/cloud of OSDs, or
 * "object storage devices."  (Note that Ceph OSDs have _nothing_ to
 * do with the T10 OSD extensions to SCSI.)  Ceph OSDs are simply
 * remote daemons serving up and coordinating consistent and safe
 * access to storage.
 *
 * Cluster membership and the mapping of data objects onto storage devices
 * are described by the osd map.
 *
 * We keep track of pending OSD requests (read, write), resubmit
 * requests to different OSDs when the cluster topology/data layout
 * change, or retry the affected requests when the communications
 * channel with an OSD is reset.
 */

/*
 * calculate the mapping of a file extent onto an object, and fill out the
 * request accordingly.  shorten extent as necessary if it crosses an
 * object boundary.
 *
 * fill osd op in request message.
 */
static int calc_layout(struct ceph_file_layout *layout, u64 off, u64 *plen,
			u64 *objnum, u64 *objoff, u64 *objlen)
{
	u64 orig_len = *plen;
	int r;

	/* object extent? */
	r = ceph_calc_file_object_mapping(layout, off, orig_len, objnum,
					  objoff, objlen);
	if (r < 0)
		return r;
	if (*objlen < orig_len) {
		*plen = *objlen;
		dout(" skipping last %llu, final file extent %llu~%llu\n",
		     orig_len - *plen, off, *plen);
	}

	dout("calc_layout objnum=%llx %llu~%llu\n", *objnum, *objoff, *objlen);

	return 0;
}

static void ceph_osd_data_init(struct ceph_osd_data *osd_data)
{
	memset(osd_data, 0, sizeof (*osd_data));
	osd_data->type = CEPH_OSD_DATA_TYPE_NONE;
}

static void ceph_osd_data_pages_init(struct ceph_osd_data *osd_data,
			struct page **pages, u64 length, u32 alignment,
			bool pages_from_pool, bool own_pages)
{
	osd_data->type = CEPH_OSD_DATA_TYPE_PAGES;
	osd_data->pages = pages;
	osd_data->length = length;
	osd_data->alignment = alignment;
	osd_data->pages_from_pool = pages_from_pool;
	osd_data->own_pages = own_pages;
}

static void ceph_osd_data_pagelist_init(struct ceph_osd_data *osd_data,
			struct ceph_pagelist *pagelist)
{
	osd_data->type = CEPH_OSD_DATA_TYPE_PAGELIST;
	osd_data->pagelist = pagelist;
}

#ifdef CONFIG_BLOCK
static void ceph_osd_data_bio_init(struct ceph_osd_data *osd_data,
			struct bio *bio, size_t bio_length)
{
	osd_data->type = CEPH_OSD_DATA_TYPE_BIO;
	osd_data->bio = bio;
	osd_data->bio_length = bio_length;
}
#endif /* CONFIG_BLOCK */

#define osd_req_op_data(oreq, whch, typ, fld)				\
({									\
	struct ceph_osd_request *__oreq = (oreq);			\
	unsigned int __whch = (whch);					\
	BUG_ON(__whch >= __oreq->r_num_ops);				\
	&__oreq->r_ops[__whch].typ.fld;					\
})

static struct ceph_osd_data *
osd_req_op_raw_data_in(struct ceph_osd_request *osd_req, unsigned int which)
{
	BUG_ON(which >= osd_req->r_num_ops);

	return &osd_req->r_ops[which].raw_data_in;
}

struct ceph_osd_data *
osd_req_op_extent_osd_data(struct ceph_osd_request *osd_req,
			unsigned int which)
{
	return osd_req_op_data(osd_req, which, extent, osd_data);
}
EXPORT_SYMBOL(osd_req_op_extent_osd_data);

void osd_req_op_raw_data_in_pages(struct ceph_osd_request *osd_req,
			unsigned int which, struct page **pages,
			u64 length, u32 alignment,
			bool pages_from_pool, bool own_pages)
{
	struct ceph_osd_data *osd_data;

	osd_data = osd_req_op_raw_data_in(osd_req, which);
	ceph_osd_data_pages_init(osd_data, pages, length, alignment,
				pages_from_pool, own_pages);
}
EXPORT_SYMBOL(osd_req_op_raw_data_in_pages);

void osd_req_op_extent_osd_data_pages(struct ceph_osd_request *osd_req,
			unsigned int which, struct page **pages,
			u64 length, u32 alignment,
			bool pages_from_pool, bool own_pages)
{
	struct ceph_osd_data *osd_data;

	osd_data = osd_req_op_data(osd_req, which, extent, osd_data);
	ceph_osd_data_pages_init(osd_data, pages, length, alignment,
				pages_from_pool, own_pages);
}
EXPORT_SYMBOL(osd_req_op_extent_osd_data_pages);

void osd_req_op_extent_osd_data_pagelist(struct ceph_osd_request *osd_req,
			unsigned int which, struct ceph_pagelist *pagelist)
{
	struct ceph_osd_data *osd_data;

	osd_data = osd_req_op_data(osd_req, which, extent, osd_data);
	ceph_osd_data_pagelist_init(osd_data, pagelist);
}
EXPORT_SYMBOL(osd_req_op_extent_osd_data_pagelist);

#ifdef CONFIG_BLOCK
void osd_req_op_extent_osd_data_bio(struct ceph_osd_request *osd_req,
			unsigned int which, struct bio *bio, size_t bio_length)
{
	struct ceph_osd_data *osd_data;

	osd_data = osd_req_op_data(osd_req, which, extent, osd_data);
	ceph_osd_data_bio_init(osd_data, bio, bio_length);
}
EXPORT_SYMBOL(osd_req_op_extent_osd_data_bio);
#endif /* CONFIG_BLOCK */

static void osd_req_op_cls_request_info_pagelist(
			struct ceph_osd_request *osd_req,
			unsigned int which, struct ceph_pagelist *pagelist)
{
	struct ceph_osd_data *osd_data;

	osd_data = osd_req_op_data(osd_req, which, cls, request_info);
	ceph_osd_data_pagelist_init(osd_data, pagelist);
}

void osd_req_op_cls_request_data_pagelist(
			struct ceph_osd_request *osd_req,
			unsigned int which, struct ceph_pagelist *pagelist)
{
	struct ceph_osd_data *osd_data;

	osd_data = osd_req_op_data(osd_req, which, cls, request_data);
	ceph_osd_data_pagelist_init(osd_data, pagelist);
	osd_req->r_ops[which].cls.indata_len += pagelist->length;
	osd_req->r_ops[which].indata_len += pagelist->length;
}
EXPORT_SYMBOL(osd_req_op_cls_request_data_pagelist);

void osd_req_op_cls_request_data_pages(struct ceph_osd_request *osd_req,
			unsigned int which, struct page **pages, u64 length,
			u32 alignment, bool pages_from_pool, bool own_pages)
{
	struct ceph_osd_data *osd_data;

	osd_data = osd_req_op_data(osd_req, which, cls, request_data);
	ceph_osd_data_pages_init(osd_data, pages, length, alignment,
				pages_from_pool, own_pages);
	osd_req->r_ops[which].cls.indata_len += length;
	osd_req->r_ops[which].indata_len += length;
}
EXPORT_SYMBOL(osd_req_op_cls_request_data_pages);

void osd_req_op_cls_response_data_pages(struct ceph_osd_request *osd_req,
			unsigned int which, struct page **pages, u64 length,
			u32 alignment, bool pages_from_pool, bool own_pages)
{
	struct ceph_osd_data *osd_data;

	osd_data = osd_req_op_data(osd_req, which, cls, response_data);
	ceph_osd_data_pages_init(osd_data, pages, length, alignment,
				pages_from_pool, own_pages);
}
EXPORT_SYMBOL(osd_req_op_cls_response_data_pages);

static u64 ceph_osd_data_length(struct ceph_osd_data *osd_data)
{
	switch (osd_data->type) {
	case CEPH_OSD_DATA_TYPE_NONE:
		return 0;
	case CEPH_OSD_DATA_TYPE_PAGES:
		return osd_data->length;
	case CEPH_OSD_DATA_TYPE_PAGELIST:
		return (u64)osd_data->pagelist->length;
#ifdef CONFIG_BLOCK
	case CEPH_OSD_DATA_TYPE_BIO:
		return (u64)osd_data->bio_length;
#endif /* CONFIG_BLOCK */
	default:
		WARN(true, "unrecognized data type %d\n", (int)osd_data->type);
		return 0;
	}
}

static void ceph_osd_data_release(struct ceph_osd_data *osd_data)
{
	if (osd_data->type == CEPH_OSD_DATA_TYPE_PAGES && osd_data->own_pages) {
		int num_pages;

		num_pages = calc_pages_for((u64)osd_data->alignment,
						(u64)osd_data->length);
		ceph_release_page_vector(osd_data->pages, num_pages);
	}
	ceph_osd_data_init(osd_data);
}

static void osd_req_op_data_release(struct ceph_osd_request *osd_req,
			unsigned int which)
{
	struct ceph_osd_req_op *op;

	BUG_ON(which >= osd_req->r_num_ops);
	op = &osd_req->r_ops[which];

	switch (op->op) {
	case CEPH_OSD_OP_READ:
	case CEPH_OSD_OP_WRITE:
	case CEPH_OSD_OP_WRITEFULL:
		ceph_osd_data_release(&op->extent.osd_data);
		break;
	case CEPH_OSD_OP_CALL:
		ceph_osd_data_release(&op->cls.request_info);
		ceph_osd_data_release(&op->cls.request_data);
		ceph_osd_data_release(&op->cls.response_data);
		break;
	case CEPH_OSD_OP_SETXATTR:
	case CEPH_OSD_OP_CMPXATTR:
		ceph_osd_data_release(&op->xattr.osd_data);
		break;
	case CEPH_OSD_OP_STAT:
		ceph_osd_data_release(&op->raw_data_in);
		break;
	default:
		break;
	}
}

/*
 * Assumes @t is zero-initialized.
 */
static void target_init(struct ceph_osd_request_target *t)
{
	ceph_oid_init(&t->base_oid);
	ceph_oloc_init(&t->base_oloc);
	ceph_oid_init(&t->target_oid);
	ceph_oloc_init(&t->target_oloc);

	ceph_osds_init(&t->acting);
	ceph_osds_init(&t->up);
	t->size = -1;
	t->min_size = -1;

	t->osd = CEPH_HOMELESS_OSD;
}

static void target_destroy(struct ceph_osd_request_target *t)
{
	ceph_oid_destroy(&t->base_oid);
	ceph_oid_destroy(&t->target_oid);
}

/*
 * requests
 */
static void ceph_osdc_release_request(struct kref *kref)
{
	struct ceph_osd_request *req = container_of(kref,
					    struct ceph_osd_request, r_kref);
	unsigned int which;

	dout("%s %p (r_request %p r_reply %p)\n", __func__, req,
	     req->r_request, req->r_reply);
	WARN_ON(!RB_EMPTY_NODE(&req->r_node));
	WARN_ON(!list_empty(&req->r_req_lru_item));
	WARN_ON(!list_empty(&req->r_osd_item));
	WARN_ON(!list_empty(&req->r_linger_item));
	WARN_ON(!list_empty(&req->r_linger_osd_item));
	WARN_ON(req->r_osd);

	if (req->r_request)
		ceph_msg_put(req->r_request);
	if (req->r_reply) {
		ceph_msg_revoke_incoming(req->r_reply);
		ceph_msg_put(req->r_reply);
	}

	for (which = 0; which < req->r_num_ops; which++)
		osd_req_op_data_release(req, which);

	target_destroy(&req->r_t);
	ceph_put_snap_context(req->r_snapc);

	if (req->r_mempool)
		mempool_free(req, req->r_osdc->req_mempool);
	else if (req->r_num_ops <= CEPH_OSD_SLAB_OPS)
		kmem_cache_free(ceph_osd_request_cache, req);
	else
		kfree(req);
}

void ceph_osdc_get_request(struct ceph_osd_request *req)
{
	dout("%s %p (was %d)\n", __func__, req,
	     atomic_read(&req->r_kref.refcount));
	kref_get(&req->r_kref);
}
EXPORT_SYMBOL(ceph_osdc_get_request);

void ceph_osdc_put_request(struct ceph_osd_request *req)
{
	if (req) {
		dout("%s %p (was %d)\n", __func__, req,
		     atomic_read(&req->r_kref.refcount));
		kref_put(&req->r_kref, ceph_osdc_release_request);
	}
}
EXPORT_SYMBOL(ceph_osdc_put_request);

struct ceph_osd_request *ceph_osdc_alloc_request(struct ceph_osd_client *osdc,
					       struct ceph_snap_context *snapc,
					       unsigned int num_ops,
					       bool use_mempool,
					       gfp_t gfp_flags)
{
	struct ceph_osd_request *req;

	if (use_mempool) {
		BUG_ON(num_ops > CEPH_OSD_SLAB_OPS);
		req = mempool_alloc(osdc->req_mempool, gfp_flags);
	} else if (num_ops <= CEPH_OSD_SLAB_OPS) {
		req = kmem_cache_alloc(ceph_osd_request_cache, gfp_flags);
	} else {
		BUG_ON(num_ops > CEPH_OSD_MAX_OPS);
		req = kmalloc(sizeof(*req) + num_ops * sizeof(req->r_ops[0]),
			      gfp_flags);
	}
	if (unlikely(!req))
		return NULL;

	/* req only, each op is zeroed in _osd_req_op_init() */
	memset(req, 0, sizeof(*req));

	req->r_osdc = osdc;
	req->r_mempool = use_mempool;
	req->r_num_ops = num_ops;
	req->r_snapid = CEPH_NOSNAP;
	req->r_snapc = ceph_get_snap_context(snapc);

	kref_init(&req->r_kref);
	init_completion(&req->r_completion);
	init_completion(&req->r_safe_completion);
	RB_CLEAR_NODE(&req->r_node);
	INIT_LIST_HEAD(&req->r_unsafe_item);
	INIT_LIST_HEAD(&req->r_linger_item);
	INIT_LIST_HEAD(&req->r_linger_osd_item);
	INIT_LIST_HEAD(&req->r_req_lru_item);
	INIT_LIST_HEAD(&req->r_osd_item);

	target_init(&req->r_t);

	dout("%s req %p\n", __func__, req);
	return req;
}
EXPORT_SYMBOL(ceph_osdc_alloc_request);

int ceph_osdc_alloc_messages(struct ceph_osd_request *req, gfp_t gfp)
{
	struct ceph_osd_client *osdc = req->r_osdc;
	struct ceph_msg *msg;
	int msg_size;

	WARN_ON(ceph_oid_empty(&req->r_base_oid));

	/* create request message */
	msg_size = 4 + 4 + 4; /* client_inc, osdmap_epoch, flags */
	msg_size += 4 + 4 + 4 + 8; /* mtime, reassert_version */
	msg_size += 2 + 4 + 8 + 4 + 4; /* oloc */
	msg_size += 1 + 8 + 4 + 4; /* pgid */
	msg_size += 4 + req->r_base_oid.name_len; /* oid */
	msg_size += 2 + req->r_num_ops * sizeof(struct ceph_osd_op);
	msg_size += 8; /* snapid */
	msg_size += 8; /* snap_seq */
	msg_size += 4 + 8 * (req->r_snapc ? req->r_snapc->num_snaps : 0);
	msg_size += 4; /* retry_attempt */

	if (req->r_mempool)
		msg = ceph_msgpool_get(&osdc->msgpool_op, 0);
	else
		msg = ceph_msg_new(CEPH_MSG_OSD_OP, msg_size, gfp, true);
	if (!msg)
		return -ENOMEM;

	memset(msg->front.iov_base, 0, msg->front.iov_len);
	req->r_request = msg;

	/* create reply message */
	msg_size = OSD_OPREPLY_FRONT_LEN;
	msg_size += req->r_base_oid.name_len;
	msg_size += req->r_num_ops * sizeof(struct ceph_osd_op);

	if (req->r_mempool)
		msg = ceph_msgpool_get(&osdc->msgpool_op_reply, 0);
	else
		msg = ceph_msg_new(CEPH_MSG_OSD_OPREPLY, msg_size, gfp, true);
	if (!msg)
		return -ENOMEM;

	req->r_reply = msg;

	return 0;
}
EXPORT_SYMBOL(ceph_osdc_alloc_messages);

static bool osd_req_opcode_valid(u16 opcode)
{
	switch (opcode) {
#define GENERATE_CASE(op, opcode, str)	case CEPH_OSD_OP_##op: return true;
__CEPH_FORALL_OSD_OPS(GENERATE_CASE)
#undef GENERATE_CASE
	default:
		return false;
	}
}

/*
 * This is an osd op init function for opcodes that have no data or
 * other information associated with them.  It also serves as a
 * common init routine for all the other init functions, below.
 */
static struct ceph_osd_req_op *
_osd_req_op_init(struct ceph_osd_request *osd_req, unsigned int which,
		 u16 opcode, u32 flags)
{
	struct ceph_osd_req_op *op;

	BUG_ON(which >= osd_req->r_num_ops);
	BUG_ON(!osd_req_opcode_valid(opcode));

	op = &osd_req->r_ops[which];
	memset(op, 0, sizeof (*op));
	op->op = opcode;
	op->flags = flags;

	return op;
}

void osd_req_op_init(struct ceph_osd_request *osd_req,
		     unsigned int which, u16 opcode, u32 flags)
{
	(void)_osd_req_op_init(osd_req, which, opcode, flags);
}
EXPORT_SYMBOL(osd_req_op_init);

void osd_req_op_extent_init(struct ceph_osd_request *osd_req,
				unsigned int which, u16 opcode,
				u64 offset, u64 length,
				u64 truncate_size, u32 truncate_seq)
{
	struct ceph_osd_req_op *op = _osd_req_op_init(osd_req, which,
						      opcode, 0);
	size_t payload_len = 0;

	BUG_ON(opcode != CEPH_OSD_OP_READ && opcode != CEPH_OSD_OP_WRITE &&
	       opcode != CEPH_OSD_OP_WRITEFULL && opcode != CEPH_OSD_OP_ZERO &&
	       opcode != CEPH_OSD_OP_TRUNCATE);

	op->extent.offset = offset;
	op->extent.length = length;
	op->extent.truncate_size = truncate_size;
	op->extent.truncate_seq = truncate_seq;
	if (opcode == CEPH_OSD_OP_WRITE || opcode == CEPH_OSD_OP_WRITEFULL)
		payload_len += length;

	op->indata_len = payload_len;
}
EXPORT_SYMBOL(osd_req_op_extent_init);

void osd_req_op_extent_update(struct ceph_osd_request *osd_req,
				unsigned int which, u64 length)
{
	struct ceph_osd_req_op *op;
	u64 previous;

	BUG_ON(which >= osd_req->r_num_ops);
	op = &osd_req->r_ops[which];
	previous = op->extent.length;

	if (length == previous)
		return;		/* Nothing to do */
	BUG_ON(length > previous);

	op->extent.length = length;
	op->indata_len -= previous - length;
}
EXPORT_SYMBOL(osd_req_op_extent_update);

void osd_req_op_extent_dup_last(struct ceph_osd_request *osd_req,
				unsigned int which, u64 offset_inc)
{
	struct ceph_osd_req_op *op, *prev_op;

	BUG_ON(which + 1 >= osd_req->r_num_ops);

	prev_op = &osd_req->r_ops[which];
	op = _osd_req_op_init(osd_req, which + 1, prev_op->op, prev_op->flags);
	/* dup previous one */
	op->indata_len = prev_op->indata_len;
	op->outdata_len = prev_op->outdata_len;
	op->extent = prev_op->extent;
	/* adjust offset */
	op->extent.offset += offset_inc;
	op->extent.length -= offset_inc;

	if (op->op == CEPH_OSD_OP_WRITE || op->op == CEPH_OSD_OP_WRITEFULL)
		op->indata_len -= offset_inc;
}
EXPORT_SYMBOL(osd_req_op_extent_dup_last);

void osd_req_op_cls_init(struct ceph_osd_request *osd_req, unsigned int which,
			u16 opcode, const char *class, const char *method)
{
	struct ceph_osd_req_op *op = _osd_req_op_init(osd_req, which,
						      opcode, 0);
	struct ceph_pagelist *pagelist;
	size_t payload_len = 0;
	size_t size;

	BUG_ON(opcode != CEPH_OSD_OP_CALL);

	pagelist = kmalloc(sizeof (*pagelist), GFP_NOFS);
	BUG_ON(!pagelist);
	ceph_pagelist_init(pagelist);

	op->cls.class_name = class;
	size = strlen(class);
	BUG_ON(size > (size_t) U8_MAX);
	op->cls.class_len = size;
	ceph_pagelist_append(pagelist, class, size);
	payload_len += size;

	op->cls.method_name = method;
	size = strlen(method);
	BUG_ON(size > (size_t) U8_MAX);
	op->cls.method_len = size;
	ceph_pagelist_append(pagelist, method, size);
	payload_len += size;

	osd_req_op_cls_request_info_pagelist(osd_req, which, pagelist);

	op->indata_len = payload_len;
}
EXPORT_SYMBOL(osd_req_op_cls_init);

int osd_req_op_xattr_init(struct ceph_osd_request *osd_req, unsigned int which,
			  u16 opcode, const char *name, const void *value,
			  size_t size, u8 cmp_op, u8 cmp_mode)
{
	struct ceph_osd_req_op *op = _osd_req_op_init(osd_req, which,
						      opcode, 0);
	struct ceph_pagelist *pagelist;
	size_t payload_len;

	BUG_ON(opcode != CEPH_OSD_OP_SETXATTR && opcode != CEPH_OSD_OP_CMPXATTR);

	pagelist = kmalloc(sizeof(*pagelist), GFP_NOFS);
	if (!pagelist)
		return -ENOMEM;

	ceph_pagelist_init(pagelist);

	payload_len = strlen(name);
	op->xattr.name_len = payload_len;
	ceph_pagelist_append(pagelist, name, payload_len);

	op->xattr.value_len = size;
	ceph_pagelist_append(pagelist, value, size);
	payload_len += size;

	op->xattr.cmp_op = cmp_op;
	op->xattr.cmp_mode = cmp_mode;

	ceph_osd_data_pagelist_init(&op->xattr.osd_data, pagelist);
	op->indata_len = payload_len;
	return 0;
}
EXPORT_SYMBOL(osd_req_op_xattr_init);

void osd_req_op_watch_init(struct ceph_osd_request *osd_req,
				unsigned int which, u16 opcode,
				u64 cookie, u64 version, int flag)
{
	struct ceph_osd_req_op *op = _osd_req_op_init(osd_req, which,
						      opcode, 0);

	BUG_ON(opcode != CEPH_OSD_OP_NOTIFY_ACK && opcode != CEPH_OSD_OP_WATCH);

	op->watch.cookie = cookie;
	op->watch.ver = version;
	if (opcode == CEPH_OSD_OP_WATCH && flag)
		op->watch.flag = (u8)1;
}
EXPORT_SYMBOL(osd_req_op_watch_init);

void osd_req_op_alloc_hint_init(struct ceph_osd_request *osd_req,
				unsigned int which,
				u64 expected_object_size,
				u64 expected_write_size)
{
	struct ceph_osd_req_op *op = _osd_req_op_init(osd_req, which,
						      CEPH_OSD_OP_SETALLOCHINT,
						      0);

	op->alloc_hint.expected_object_size = expected_object_size;
	op->alloc_hint.expected_write_size = expected_write_size;

	/*
	 * CEPH_OSD_OP_SETALLOCHINT op is advisory and therefore deemed
	 * not worth a feature bit.  Set FAILOK per-op flag to make
	 * sure older osds don't trip over an unsupported opcode.
	 */
	op->flags |= CEPH_OSD_OP_FLAG_FAILOK;
}
EXPORT_SYMBOL(osd_req_op_alloc_hint_init);

static void ceph_osdc_msg_data_add(struct ceph_msg *msg,
				struct ceph_osd_data *osd_data)
{
	u64 length = ceph_osd_data_length(osd_data);

	if (osd_data->type == CEPH_OSD_DATA_TYPE_PAGES) {
		BUG_ON(length > (u64) SIZE_MAX);
		if (length)
			ceph_msg_data_add_pages(msg, osd_data->pages,
					length, osd_data->alignment);
	} else if (osd_data->type == CEPH_OSD_DATA_TYPE_PAGELIST) {
		BUG_ON(!length);
		ceph_msg_data_add_pagelist(msg, osd_data->pagelist);
#ifdef CONFIG_BLOCK
	} else if (osd_data->type == CEPH_OSD_DATA_TYPE_BIO) {
		ceph_msg_data_add_bio(msg, osd_data->bio, length);
#endif
	} else {
		BUG_ON(osd_data->type != CEPH_OSD_DATA_TYPE_NONE);
	}
}

static u32 osd_req_encode_op(struct ceph_osd_op *dst,
			     const struct ceph_osd_req_op *src)
{
	if (WARN_ON(!osd_req_opcode_valid(src->op))) {
		pr_err("unrecognized osd opcode %d\n", src->op);

		return 0;
	}

	switch (src->op) {
	case CEPH_OSD_OP_STAT:
		break;
	case CEPH_OSD_OP_READ:
	case CEPH_OSD_OP_WRITE:
	case CEPH_OSD_OP_WRITEFULL:
	case CEPH_OSD_OP_ZERO:
	case CEPH_OSD_OP_TRUNCATE:
		dst->extent.offset = cpu_to_le64(src->extent.offset);
		dst->extent.length = cpu_to_le64(src->extent.length);
		dst->extent.truncate_size =
			cpu_to_le64(src->extent.truncate_size);
		dst->extent.truncate_seq =
			cpu_to_le32(src->extent.truncate_seq);
		break;
	case CEPH_OSD_OP_CALL:
		dst->cls.class_len = src->cls.class_len;
		dst->cls.method_len = src->cls.method_len;
		dst->cls.indata_len = cpu_to_le32(src->cls.indata_len);
		break;
	case CEPH_OSD_OP_STARTSYNC:
		break;
	case CEPH_OSD_OP_NOTIFY_ACK:
	case CEPH_OSD_OP_WATCH:
		dst->watch.cookie = cpu_to_le64(src->watch.cookie);
		dst->watch.ver = cpu_to_le64(src->watch.ver);
		dst->watch.flag = src->watch.flag;
		break;
	case CEPH_OSD_OP_SETALLOCHINT:
		dst->alloc_hint.expected_object_size =
		    cpu_to_le64(src->alloc_hint.expected_object_size);
		dst->alloc_hint.expected_write_size =
		    cpu_to_le64(src->alloc_hint.expected_write_size);
		break;
	case CEPH_OSD_OP_SETXATTR:
	case CEPH_OSD_OP_CMPXATTR:
		dst->xattr.name_len = cpu_to_le32(src->xattr.name_len);
		dst->xattr.value_len = cpu_to_le32(src->xattr.value_len);
		dst->xattr.cmp_op = src->xattr.cmp_op;
		dst->xattr.cmp_mode = src->xattr.cmp_mode;
		break;
	case CEPH_OSD_OP_CREATE:
	case CEPH_OSD_OP_DELETE:
		break;
	default:
		pr_err("unsupported osd opcode %s\n",
			ceph_osd_op_name(src->op));
		WARN_ON(1);

		return 0;
	}

	dst->op = cpu_to_le16(src->op);
	dst->flags = cpu_to_le32(src->flags);
	dst->payload_len = cpu_to_le32(src->indata_len);

	return src->indata_len;
}

/*
 * build new request AND message, calculate layout, and adjust file
 * extent as needed.
 *
 * if the file was recently truncated, we include information about its
 * old and new size so that the object can be updated appropriately.  (we
 * avoid synchronously deleting truncated objects because it's slow.)
 *
 * if @do_sync, include a 'startsync' command so that the osd will flush
 * data quickly.
 */
struct ceph_osd_request *ceph_osdc_new_request(struct ceph_osd_client *osdc,
					       struct ceph_file_layout *layout,
					       struct ceph_vino vino,
					       u64 off, u64 *plen,
					       unsigned int which, int num_ops,
					       int opcode, int flags,
					       struct ceph_snap_context *snapc,
					       u32 truncate_seq,
					       u64 truncate_size,
					       bool use_mempool)
{
	struct ceph_osd_request *req;
	u64 objnum = 0;
	u64 objoff = 0;
	u64 objlen = 0;
	int r;

	BUG_ON(opcode != CEPH_OSD_OP_READ && opcode != CEPH_OSD_OP_WRITE &&
	       opcode != CEPH_OSD_OP_ZERO && opcode != CEPH_OSD_OP_TRUNCATE &&
	       opcode != CEPH_OSD_OP_CREATE && opcode != CEPH_OSD_OP_DELETE);

	req = ceph_osdc_alloc_request(osdc, snapc, num_ops, use_mempool,
					GFP_NOFS);
	if (!req) {
		r = -ENOMEM;
		goto fail;
	}

	/* calculate max write size */
	r = calc_layout(layout, off, plen, &objnum, &objoff, &objlen);
	if (r)
		goto fail;

	if (opcode == CEPH_OSD_OP_CREATE || opcode == CEPH_OSD_OP_DELETE) {
		osd_req_op_init(req, which, opcode, 0);
	} else {
		u32 object_size = le32_to_cpu(layout->fl_object_size);
		u32 object_base = off - objoff;
		if (!(truncate_seq == 1 && truncate_size == -1ULL)) {
			if (truncate_size <= object_base) {
				truncate_size = 0;
			} else {
				truncate_size -= object_base;
				if (truncate_size > object_size)
					truncate_size = object_size;
			}
		}
		osd_req_op_extent_init(req, which, opcode, objoff, objlen,
				       truncate_size, truncate_seq);
	}

	req->r_flags = flags;
	req->r_base_oloc.pool = ceph_file_layout_pg_pool(*layout);
	ceph_oid_printf(&req->r_base_oid, "%llx.%08llx", vino.ino, objnum);

	req->r_snapid = vino.snap;
	if (flags & CEPH_OSD_FLAG_WRITE)
		req->r_data_offset = off;

	r = ceph_osdc_alloc_messages(req, GFP_NOFS);
	if (r)
		goto fail;

	return req;

fail:
	ceph_osdc_put_request(req);
	return ERR_PTR(r);
}
EXPORT_SYMBOL(ceph_osdc_new_request);

/*
 * We keep osd requests in an rbtree, sorted by ->r_tid.
 */
DEFINE_RB_FUNCS(request, struct ceph_osd_request, r_tid, r_node)

static struct ceph_osd_request *
__lookup_request_ge(struct ceph_osd_client *osdc,
		    u64 tid)
{
	struct ceph_osd_request *req;
	struct rb_node *n = osdc->requests.rb_node;

	while (n) {
		req = rb_entry(n, struct ceph_osd_request, r_node);
		if (tid < req->r_tid) {
			if (!n->rb_left)
				return req;
			n = n->rb_left;
		} else if (tid > req->r_tid) {
			n = n->rb_right;
		} else {
			return req;
		}
	}
	return NULL;
}

static void __kick_linger_request(struct ceph_osd_request *req)
{
	struct ceph_osd_client *osdc = req->r_osdc;
	struct ceph_osd *osd = req->r_osd;

	/*
	 * Linger requests need to be resent with a new tid to avoid
	 * the dup op detection logic on the OSDs.  Achieve this with
	 * a re-register dance instead of open-coding.
	 */
	ceph_osdc_get_request(req);
	if (!list_empty(&req->r_linger_item))
		__unregister_linger_request(osdc, req);
	else
		__unregister_request(osdc, req);
	__register_request(osdc, req);
	ceph_osdc_put_request(req);

	/*
	 * Unless request has been registered as both normal and
	 * lingering, __unregister{,_linger}_request clears r_osd.
	 * However, here we need to preserve r_osd to make sure we
	 * requeue on the same OSD.
	 */
	WARN_ON(req->r_osd || !osd);
	req->r_osd = osd;

	dout("%s requeueing %p tid %llu\n", __func__, req, req->r_tid);
	__enqueue_request(req);
}

/*
 * Resubmit requests pending on the given osd.
 */
static void __kick_osd_requests(struct ceph_osd_client *osdc,
				struct ceph_osd *osd)
{
	struct ceph_osd_request *req, *nreq;
	LIST_HEAD(resend);
	LIST_HEAD(resend_linger);
	int err;

	dout("%s osd%d\n", __func__, osd->o_osd);
	err = __reset_osd(osdc, osd);
	if (err)
		return;

	/*
	 * Build up a list of requests to resend by traversing the
	 * osd's list of requests.  Requests for a given object are
	 * sent in tid order, and that is also the order they're
	 * kept on this list.  Therefore all requests that are in
	 * flight will be found first, followed by all requests that
	 * have not yet been sent.  And to resend requests while
	 * preserving this order we will want to put any sent
	 * requests back on the front of the osd client's unsent
	 * list.
	 *
	 * So we build a separate ordered list of already-sent
	 * requests for the affected osd and splice it onto the
	 * front of the osd client's unsent list.  Once we've seen a
	 * request that has not yet been sent we're done.  Those
	 * requests are already sitting right where they belong.
	 */
	list_for_each_entry(req, &osd->o_requests, r_osd_item) {
		if (!req->r_sent)
			break;

		if (!req->r_linger) {
			dout("%s requeueing %p tid %llu\n", __func__, req,
			     req->r_tid);
			list_move_tail(&req->r_req_lru_item, &resend);
			req->r_flags |= CEPH_OSD_FLAG_RETRY;
		} else {
			list_move_tail(&req->r_req_lru_item, &resend_linger);
		}
	}
	list_splice(&resend, &osdc->req_unsent);

	/*
	 * Both registered and not yet registered linger requests are
	 * enqueued with a new tid on the same OSD.  We add/move them
	 * to req_unsent/o_requests at the end to keep things in tid
	 * order.
	 */
	list_for_each_entry_safe(req, nreq, &osd->o_linger_requests,
				 r_linger_osd_item) {
		WARN_ON(!list_empty(&req->r_req_lru_item));
		__kick_linger_request(req);
	}

	list_for_each_entry_safe(req, nreq, &resend_linger, r_req_lru_item)
		__kick_linger_request(req);
}

/*
 * If the osd connection drops, we need to resubmit all requests.
 */
static void osd_reset(struct ceph_connection *con)
{
	struct ceph_osd *osd = con->private;
	struct ceph_osd_client *osdc;

	if (!osd)
		return;
	dout("osd_reset osd%d\n", osd->o_osd);
	osdc = osd->o_osdc;
	down_read(&osdc->map_sem);
	mutex_lock(&osdc->request_mutex);
	__kick_osd_requests(osdc, osd);
	__send_queued(osdc);
	mutex_unlock(&osdc->request_mutex);
	up_read(&osdc->map_sem);
}

/*
 * Track open sessions with osds.
 */
static struct ceph_osd *create_osd(struct ceph_osd_client *osdc, int onum)
{
	struct ceph_osd *osd;

	osd = kzalloc(sizeof(*osd), GFP_NOFS);
	if (!osd)
		return NULL;

	atomic_set(&osd->o_ref, 1);
	osd->o_osdc = osdc;
	osd->o_osd = onum;
	RB_CLEAR_NODE(&osd->o_node);
	INIT_LIST_HEAD(&osd->o_requests);
	INIT_LIST_HEAD(&osd->o_linger_requests);
	INIT_LIST_HEAD(&osd->o_osd_lru);
	osd->o_incarnation = 1;

	ceph_con_init(&osd->o_con, osd, &osd_con_ops, &osdc->client->msgr);

	INIT_LIST_HEAD(&osd->o_keepalive_item);
	return osd;
}

static struct ceph_osd *get_osd(struct ceph_osd *osd)
{
	if (atomic_inc_not_zero(&osd->o_ref)) {
		dout("get_osd %p %d -> %d\n", osd, atomic_read(&osd->o_ref)-1,
		     atomic_read(&osd->o_ref));
		return osd;
	} else {
		dout("get_osd %p FAIL\n", osd);
		return NULL;
	}
}

static void put_osd(struct ceph_osd *osd)
{
	dout("put_osd %p %d -> %d\n", osd, atomic_read(&osd->o_ref),
	     atomic_read(&osd->o_ref) - 1);
	if (atomic_dec_and_test(&osd->o_ref)) {
		if (osd->o_auth.authorizer)
			ceph_auth_destroy_authorizer(osd->o_auth.authorizer);
		kfree(osd);
	}
}

DEFINE_RB_FUNCS(osd, struct ceph_osd, o_osd, o_node)

/*
 * remove an osd from our map
 */
static void __remove_osd(struct ceph_osd_client *osdc, struct ceph_osd *osd)
{
	dout("%s %p osd%d\n", __func__, osd, osd->o_osd);
	WARN_ON(!list_empty(&osd->o_requests));
	WARN_ON(!list_empty(&osd->o_linger_requests));

	list_del_init(&osd->o_osd_lru);
	erase_osd(&osdc->osds, osd);
}

static void remove_osd(struct ceph_osd_client *osdc, struct ceph_osd *osd)
{
	dout("%s %p osd%d\n", __func__, osd, osd->o_osd);

	if (!RB_EMPTY_NODE(&osd->o_node)) {
		ceph_con_close(&osd->o_con);
		__remove_osd(osdc, osd);
		put_osd(osd);
	}
}

static void __move_osd_to_lru(struct ceph_osd_client *osdc,
			      struct ceph_osd *osd)
{
	dout("%s %p\n", __func__, osd);
	BUG_ON(!list_empty(&osd->o_osd_lru));

	list_add_tail(&osd->o_osd_lru, &osdc->osd_lru);
	osd->lru_ttl = jiffies + osdc->client->options->osd_idle_ttl;
}

static void maybe_move_osd_to_lru(struct ceph_osd_client *osdc,
				  struct ceph_osd *osd)
{
	dout("%s %p\n", __func__, osd);

	if (list_empty(&osd->o_requests) &&
	    list_empty(&osd->o_linger_requests))
		__move_osd_to_lru(osdc, osd);
}

static void __remove_osd_from_lru(struct ceph_osd *osd)
{
	dout("__remove_osd_from_lru %p\n", osd);
	if (!list_empty(&osd->o_osd_lru))
		list_del_init(&osd->o_osd_lru);
}

/*
 * reset osd connect
 */
static int __reset_osd(struct ceph_osd_client *osdc, struct ceph_osd *osd)
{
	struct ceph_entity_addr *peer_addr;

	dout("__reset_osd %p osd%d\n", osd, osd->o_osd);
	if (list_empty(&osd->o_requests) &&
	    list_empty(&osd->o_linger_requests)) {
		remove_osd(osdc, osd);
		return -ENODEV;
	}

	peer_addr = &osdc->osdmap->osd_addr[osd->o_osd];
	if (!memcmp(peer_addr, &osd->o_con.peer_addr, sizeof (*peer_addr)) &&
			!ceph_con_opened(&osd->o_con)) {
		struct ceph_osd_request *req;

		dout("osd addr hasn't changed and connection never opened, "
		     "letting msgr retry\n");
		/* touch each r_stamp for handle_timeout()'s benfit */
		list_for_each_entry(req, &osd->o_requests, r_osd_item)
			req->r_stamp = jiffies;

		return -EAGAIN;
	}

	ceph_con_close(&osd->o_con);
	ceph_con_open(&osd->o_con, CEPH_ENTITY_TYPE_OSD, osd->o_osd, peer_addr);
	osd->o_incarnation++;

	return 0;
}

static void __schedule_osd_timeout(struct ceph_osd_client *osdc)
{
	schedule_delayed_work(&osdc->timeout_work,
			      osdc->client->options->osd_keepalive_timeout);
}

static void __cancel_osd_timeout(struct ceph_osd_client *osdc)
{
	cancel_delayed_work(&osdc->timeout_work);
}

/*
 * Register request, assign tid.  If this is the first request, set up
 * the timeout event.
 */
static void __register_request(struct ceph_osd_client *osdc,
			       struct ceph_osd_request *req)
{
	req->r_tid = ++osdc->last_tid;
	req->r_request->hdr.tid = cpu_to_le64(req->r_tid);
	dout("__register_request %p tid %lld\n", req, req->r_tid);
	insert_request(&osdc->requests, req);
	ceph_osdc_get_request(req);
	osdc->num_requests++;
	if (osdc->num_requests == 1) {
		dout(" first request, scheduling timeout\n");
		__schedule_osd_timeout(osdc);
	}
}

/*
 * called under osdc->request_mutex
 */
static void __unregister_request(struct ceph_osd_client *osdc,
				 struct ceph_osd_request *req)
{
	if (RB_EMPTY_NODE(&req->r_node)) {
		dout("__unregister_request %p tid %lld not registered\n",
			req, req->r_tid);
		return;
	}

	dout("__unregister_request %p tid %lld\n", req, req->r_tid);
	erase_request(&osdc->requests, req);
	osdc->num_requests--;

	if (req->r_osd) {
		/* make sure the original request isn't in flight. */
		ceph_msg_revoke(req->r_request);

		list_del_init(&req->r_osd_item);
		maybe_move_osd_to_lru(osdc, req->r_osd);
		if (list_empty(&req->r_linger_osd_item))
			req->r_osd = NULL;
	}

	list_del_init(&req->r_req_lru_item);
	ceph_osdc_put_request(req);

	if (osdc->num_requests == 0) {
		dout(" no requests, canceling timeout\n");
		__cancel_osd_timeout(osdc);
	}
}

/*
 * Cancel a previously queued request message
 */
static void __cancel_request(struct ceph_osd_request *req)
{
	if (req->r_sent && req->r_osd) {
		ceph_msg_revoke(req->r_request);
		req->r_sent = 0;
	}
}

static void __register_linger_request(struct ceph_osd_client *osdc,
				    struct ceph_osd_request *req)
{
	dout("%s %p tid %llu\n", __func__, req, req->r_tid);
	WARN_ON(!req->r_linger);

	ceph_osdc_get_request(req);
	list_add_tail(&req->r_linger_item, &osdc->req_linger);
	if (req->r_osd)
		list_add_tail(&req->r_linger_osd_item,
			      &req->r_osd->o_linger_requests);
}

static void __unregister_linger_request(struct ceph_osd_client *osdc,
					struct ceph_osd_request *req)
{
	WARN_ON(!req->r_linger);

	if (list_empty(&req->r_linger_item)) {
		dout("%s %p tid %llu not registered\n", __func__, req,
		     req->r_tid);
		return;
	}

	dout("%s %p tid %llu\n", __func__, req, req->r_tid);
	list_del_init(&req->r_linger_item);

	if (req->r_osd) {
		list_del_init(&req->r_linger_osd_item);
		maybe_move_osd_to_lru(osdc, req->r_osd);
		if (list_empty(&req->r_osd_item))
			req->r_osd = NULL;
	}
	ceph_osdc_put_request(req);
}

void ceph_osdc_set_request_linger(struct ceph_osd_client *osdc,
				  struct ceph_osd_request *req)
{
	if (!req->r_linger) {
		dout("set_request_linger %p\n", req);
		req->r_linger = 1;
	}
}
EXPORT_SYMBOL(ceph_osdc_set_request_linger);

static bool __pool_full(struct ceph_pg_pool_info *pi)
{
	return pi->flags & CEPH_POOL_FLAG_FULL;
}

/*
 * Returns whether a request should be blocked from being sent
 * based on the current osdmap and osd_client settings.
 *
 * Caller should hold map_sem for read.
 */
static bool target_should_be_paused(struct ceph_osd_client *osdc,
				    const struct ceph_osd_request_target *t,
				    struct ceph_pg_pool_info *pi)
{
	bool pauserd = ceph_osdmap_flag(osdc->osdmap, CEPH_OSDMAP_PAUSERD);
	bool pausewr = ceph_osdmap_flag(osdc->osdmap, CEPH_OSDMAP_PAUSEWR) ||
		       ceph_osdmap_flag(osdc->osdmap, CEPH_OSDMAP_FULL) ||
		       __pool_full(pi);

	WARN_ON(pi->id != t->base_oloc.pool);
	return (t->flags & CEPH_OSD_FLAG_READ && pauserd) ||
	       (t->flags & CEPH_OSD_FLAG_WRITE && pausewr);
}

enum calc_target_result {
	CALC_TARGET_NO_ACTION = 0,
	CALC_TARGET_NEED_RESEND,
	CALC_TARGET_POOL_DNE,
};

static enum calc_target_result calc_target(struct ceph_osd_client *osdc,
					   struct ceph_osd_request_target *t,
					   u32 *last_force_resend,
					   bool any_change)
{
	struct ceph_pg_pool_info *pi;
	struct ceph_pg pgid, last_pgid;
	struct ceph_osds up, acting;
	bool force_resend = false;
	bool need_check_tiering = false;
	bool need_resend = false;
	bool sort_bitwise = ceph_osdmap_flag(osdc->osdmap,
					     CEPH_OSDMAP_SORTBITWISE);
	enum calc_target_result ct_res;
	int ret;

	pi = ceph_pg_pool_by_id(osdc->osdmap, t->base_oloc.pool);
	if (!pi) {
		t->osd = CEPH_HOMELESS_OSD;
		ct_res = CALC_TARGET_POOL_DNE;
		goto out;
	}

	if (osdc->osdmap->epoch == pi->last_force_request_resend) {
		if (last_force_resend &&
		    *last_force_resend < pi->last_force_request_resend) {
			*last_force_resend = pi->last_force_request_resend;
			force_resend = true;
		} else if (!last_force_resend) {
			force_resend = true;
		}
	}
	if (ceph_oid_empty(&t->target_oid) || force_resend) {
		ceph_oid_copy(&t->target_oid, &t->base_oid);
		need_check_tiering = true;
	}
	if (ceph_oloc_empty(&t->target_oloc) || force_resend) {
		ceph_oloc_copy(&t->target_oloc, &t->base_oloc);
		need_check_tiering = true;
	}

	if (need_check_tiering &&
	    (t->flags & CEPH_OSD_FLAG_IGNORE_OVERLAY) == 0) {
		if (t->flags & CEPH_OSD_FLAG_READ && pi->read_tier >= 0)
			t->target_oloc.pool = pi->read_tier;
		if (t->flags & CEPH_OSD_FLAG_WRITE && pi->write_tier >= 0)
			t->target_oloc.pool = pi->write_tier;
	}

	ret = ceph_object_locator_to_pg(osdc->osdmap, &t->target_oid,
					&t->target_oloc, &pgid);
	if (ret) {
		WARN_ON(ret != -ENOENT);
		t->osd = CEPH_HOMELESS_OSD;
		ct_res = CALC_TARGET_POOL_DNE;
		goto out;
	}
	last_pgid.pool = pgid.pool;
	last_pgid.seed = ceph_stable_mod(pgid.seed, t->pg_num, t->pg_num_mask);

	ceph_pg_to_up_acting_osds(osdc->osdmap, &pgid, &up, &acting);
	if (any_change &&
	    ceph_is_new_interval(&t->acting,
				 &acting,
				 &t->up,
				 &up,
				 t->size,
				 pi->size,
				 t->min_size,
				 pi->min_size,
				 t->pg_num,
				 pi->pg_num,
				 t->sort_bitwise,
				 sort_bitwise,
				 &last_pgid))
		force_resend = true;

	if (t->paused && !target_should_be_paused(osdc, t, pi)) {
		t->paused = false;
		need_resend = true;
	}

	if (ceph_pg_compare(&t->pgid, &pgid) ||
	    ceph_osds_changed(&t->acting, &acting, any_change) ||
	    force_resend) {
		t->pgid = pgid; /* struct */
		ceph_osds_copy(&t->acting, &acting);
		ceph_osds_copy(&t->up, &up);
		t->size = pi->size;
		t->min_size = pi->min_size;
		t->pg_num = pi->pg_num;
		t->pg_num_mask = pi->pg_num_mask;
		t->sort_bitwise = sort_bitwise;

		t->osd = acting.primary;
		need_resend = true;
	}

	ct_res = need_resend ? CALC_TARGET_NEED_RESEND : CALC_TARGET_NO_ACTION;
out:
	dout("%s t %p -> ct_res %d osd %d\n", __func__, t, ct_res, t->osd);
	return ct_res;
}

static void __enqueue_request(struct ceph_osd_request *req)
{
	struct ceph_osd_client *osdc = req->r_osdc;

	dout("%s %p tid %llu to osd%d\n", __func__, req, req->r_tid,
	     req->r_osd ? req->r_osd->o_osd : -1);

	if (req->r_osd) {
		__remove_osd_from_lru(req->r_osd);
		list_add_tail(&req->r_osd_item, &req->r_osd->o_requests);
		list_move_tail(&req->r_req_lru_item, &osdc->req_unsent);
	} else {
		list_move_tail(&req->r_req_lru_item, &osdc->req_notarget);
	}
}

/*
 * Pick an osd (the first 'up' osd in the pg), allocate the osd struct
 * (as needed), and set the request r_osd appropriately.  If there is
 * no up osd, set r_osd to NULL.  Move the request to the appropriate list
 * (unsent, homeless) or leave on in-flight lru.
 *
 * Return 0 if unchanged, 1 if changed, or negative on error.
 *
 * Caller should hold map_sem for read and request_mutex.
 */
static int __map_request(struct ceph_osd_client *osdc,
			 struct ceph_osd_request *req, int force_resend)
{
	enum calc_target_result ct_res;
	int err;

	dout("map_request %p tid %lld\n", req, req->r_tid);

	ct_res = calc_target(osdc, &req->r_t, NULL, force_resend);
	switch (ct_res) {
	case CALC_TARGET_POOL_DNE:
		list_move(&req->r_req_lru_item, &osdc->req_notarget);
		return -EIO;
	case CALC_TARGET_NO_ACTION:
		return 0;  /* no change */
	default:
		BUG_ON(ct_res != CALC_TARGET_NEED_RESEND);
	}

	dout("map_request tid %llu pgid %lld.%x osd%d (was osd%d)\n",
	     req->r_tid, req->r_t.pgid.pool, req->r_t.pgid.seed, req->r_t.osd,
	     req->r_osd ? req->r_osd->o_osd : -1);

	if (req->r_osd) {
		__cancel_request(req);
		list_del_init(&req->r_osd_item);
		list_del_init(&req->r_linger_osd_item);
		req->r_osd = NULL;
	}

	req->r_osd = lookup_osd(&osdc->osds, req->r_t.osd);
	if (!req->r_osd && req->r_t.osd >= 0) {
		err = -ENOMEM;
		req->r_osd = create_osd(osdc, req->r_t.osd);
		if (!req->r_osd) {
			list_move(&req->r_req_lru_item, &osdc->req_notarget);
			goto out;
		}

		dout("map_request osd %p is osd%d\n", req->r_osd,
		     req->r_osd->o_osd);
		insert_osd(&osdc->osds, req->r_osd);

		ceph_con_open(&req->r_osd->o_con,
			      CEPH_ENTITY_TYPE_OSD, req->r_osd->o_osd,
			      &osdc->osdmap->osd_addr[req->r_osd->o_osd]);
	}

	__enqueue_request(req);
	err = 1;   /* osd or pg changed */

out:
	return err;
}

static void setup_request_data(struct ceph_osd_request *req,
			       struct ceph_msg *msg)
{
	u32 data_len = 0;
	int i;

	if (!list_empty(&msg->data))
		return;

	WARN_ON(msg->data_length);
	for (i = 0; i < req->r_num_ops; i++) {
		struct ceph_osd_req_op *op = &req->r_ops[i];

		switch (op->op) {
		/* request */
		case CEPH_OSD_OP_WRITE:
		case CEPH_OSD_OP_WRITEFULL:
			WARN_ON(op->indata_len != op->extent.length);
			ceph_osdc_msg_data_add(msg, &op->extent.osd_data);
			break;
		case CEPH_OSD_OP_SETXATTR:
		case CEPH_OSD_OP_CMPXATTR:
			WARN_ON(op->indata_len != op->xattr.name_len +
						  op->xattr.value_len);
			ceph_osdc_msg_data_add(msg, &op->xattr.osd_data);
			break;

		/* reply */
		case CEPH_OSD_OP_STAT:
			ceph_osdc_msg_data_add(req->r_reply,
					       &op->raw_data_in);
			break;
		case CEPH_OSD_OP_READ:
			ceph_osdc_msg_data_add(req->r_reply,
					       &op->extent.osd_data);
			break;

		/* both */
		case CEPH_OSD_OP_CALL:
			WARN_ON(op->indata_len != op->cls.class_len +
						  op->cls.method_len +
						  op->cls.indata_len);
			ceph_osdc_msg_data_add(msg, &op->cls.request_info);
			/* optional, can be NONE */
			ceph_osdc_msg_data_add(msg, &op->cls.request_data);
			/* optional, can be NONE */
			ceph_osdc_msg_data_add(req->r_reply,
					       &op->cls.response_data);
			break;
		}

		data_len += op->indata_len;
	}

	WARN_ON(data_len != msg->data_length);
}

static void encode_request(struct ceph_osd_request *req, struct ceph_msg *msg)
{
	void *p = msg->front.iov_base;
	void *const end = p + msg->front_alloc_len;
	u32 data_len = 0;
	int i;

	if (req->r_flags & CEPH_OSD_FLAG_WRITE) {
		/* snapshots aren't writeable */
		WARN_ON(req->r_snapid != CEPH_NOSNAP);
	} else {
		WARN_ON(req->r_mtime.tv_sec || req->r_mtime.tv_nsec ||
			req->r_data_offset || req->r_snapc);
	}

	setup_request_data(req, msg);

	ceph_encode_32(&p, 1); /* client_inc, always 1 */
	ceph_encode_32(&p, req->r_osdc->osdmap->epoch);
	ceph_encode_32(&p, req->r_flags);
	ceph_encode_timespec(p, &req->r_mtime);
	p += sizeof(struct ceph_timespec);
	/* aka reassert_version */
	memcpy(p, &req->r_replay_version, sizeof(req->r_replay_version));
	p += sizeof(req->r_replay_version);

	/* oloc */
	ceph_encode_8(&p, 4);
	ceph_encode_8(&p, 4);
	ceph_encode_32(&p, 8 + 4 + 4);
	ceph_encode_64(&p, req->r_t.target_oloc.pool);
	ceph_encode_32(&p, -1); /* preferred */
	ceph_encode_32(&p, 0); /* key len */

	/* pgid */
	ceph_encode_8(&p, 1);
	ceph_encode_64(&p, req->r_t.pgid.pool);
	ceph_encode_32(&p, req->r_t.pgid.seed);
	ceph_encode_32(&p, -1); /* preferred */

	/* oid */
	ceph_encode_32(&p, req->r_t.target_oid.name_len);
	memcpy(p, req->r_t.target_oid.name, req->r_t.target_oid.name_len);
	p += req->r_t.target_oid.name_len;

	/* ops, can imply data */
	ceph_encode_16(&p, req->r_num_ops);
	for (i = 0; i < req->r_num_ops; i++) {
		data_len += osd_req_encode_op(p, &req->r_ops[i]);
		p += sizeof(struct ceph_osd_op);
	}

	ceph_encode_64(&p, req->r_snapid); /* snapid */
	if (req->r_snapc) {
		ceph_encode_64(&p, req->r_snapc->seq);
		ceph_encode_32(&p, req->r_snapc->num_snaps);
		for (i = 0; i < req->r_snapc->num_snaps; i++)
			ceph_encode_64(&p, req->r_snapc->snaps[i]);
	} else {
		ceph_encode_64(&p, 0); /* snap_seq */
		ceph_encode_32(&p, 0); /* snaps len */
	}

	ceph_encode_32(&p, req->r_attempts); /* retry_attempt */

	BUG_ON(p > end);
	msg->front.iov_len = p - msg->front.iov_base;
	msg->hdr.version = cpu_to_le16(4); /* MOSDOp v4 */
	msg->hdr.front_len = cpu_to_le32(msg->front.iov_len);
	msg->hdr.data_len = cpu_to_le32(data_len);
	/*
	 * The header "data_off" is a hint to the receiver allowing it
	 * to align received data into its buffers such that there's no
	 * need to re-copy it before writing it to disk (direct I/O).
	 */
	msg->hdr.data_off = cpu_to_le16(req->r_data_offset);

	dout("%s req %p oid %*pE oid_len %d front %zu data %u\n", __func__,
	     req, req->r_t.target_oid.name_len, req->r_t.target_oid.name,
	     req->r_t.target_oid.name_len, msg->front.iov_len, data_len);
}

/*
 * @req has to be assigned a tid and registered.
 */
static void send_request(struct ceph_osd_request *req)
{
	struct ceph_osd *osd = req->r_osd;

	WARN_ON(osd->o_osd != req->r_t.osd);

	req->r_flags |= CEPH_OSD_FLAG_KNOWN_REDIR;
	if (req->r_attempts)
		req->r_flags |= CEPH_OSD_FLAG_RETRY;
	else
		WARN_ON(req->r_flags & CEPH_OSD_FLAG_RETRY);

	encode_request(req, req->r_request);

	dout("%s req %p tid %llu to pg %llu.%x osd%d flags 0x%x attempt %d\n",
	     __func__, req, req->r_tid, req->r_t.pgid.pool, req->r_t.pgid.seed,
	     req->r_t.osd, req->r_flags, req->r_attempts);

	req->r_t.paused = false;
	req->r_stamp = jiffies;
	req->r_attempts++;

	req->r_sent = osd->o_incarnation;
	req->r_request->hdr.tid = cpu_to_le64(req->r_tid);
	ceph_con_send(&osd->o_con, ceph_msg_get(req->r_request));
}

/*
 * Send any requests in the queue (req_unsent).
 */
static void __send_queued(struct ceph_osd_client *osdc)
{
	struct ceph_osd_request *req, *tmp;

	dout("__send_queued\n");
	list_for_each_entry_safe(req, tmp, &osdc->req_unsent, r_req_lru_item) {
		list_move_tail(&req->r_req_lru_item, &osdc->req_lru);
		send_request(req);
	}
}

/*
 * Caller should hold map_sem for read and request_mutex.
 */
static int __ceph_osdc_start_request(struct ceph_osd_client *osdc,
				     struct ceph_osd_request *req,
				     bool nofail)
{
	int rc;

	__register_request(osdc, req);
	req->r_sent = 0;
	req->r_got_reply = 0;
	rc = __map_request(osdc, req, 0);
	if (rc < 0) {
		if (nofail) {
			dout("osdc_start_request failed map, "
				" will retry %lld\n", req->r_tid);
			rc = 0;
		} else {
			__unregister_request(osdc, req);
		}
		return rc;
	}

	if (req->r_osd == NULL) {
		dout("send_request %p no up osds in pg\n", req);
		ceph_monc_request_next_osdmap(&osdc->client->monc);
	} else {
		__send_queued(osdc);
	}

	return 0;
}

/*
 * Timeout callback, called every N seconds when 1 or more osd
 * requests has been active for more than N seconds.  When this
 * happens, we ping all OSDs with requests who have timed out to
 * ensure any communications channel reset is detected.  Reset the
 * request timeouts another N seconds in the future as we go.
 * Reschedule the timeout event another N seconds in future (unless
 * there are no open requests).
 */
static void handle_timeout(struct work_struct *work)
{
	struct ceph_osd_client *osdc =
		container_of(work, struct ceph_osd_client, timeout_work.work);
	struct ceph_options *opts = osdc->client->options;
	struct ceph_osd_request *req;
	struct ceph_osd *osd;
	struct list_head slow_osds;
	dout("timeout\n");
	down_read(&osdc->map_sem);

	ceph_monc_request_next_osdmap(&osdc->client->monc);

	mutex_lock(&osdc->request_mutex);

	/*
	 * ping osds that are a bit slow.  this ensures that if there
	 * is a break in the TCP connection we will notice, and reopen
	 * a connection with that osd (from the fault callback).
	 */
	INIT_LIST_HEAD(&slow_osds);
	list_for_each_entry(req, &osdc->req_lru, r_req_lru_item) {
		if (time_before(jiffies,
				req->r_stamp + opts->osd_keepalive_timeout))
			break;

		osd = req->r_osd;
		BUG_ON(!osd);
		dout(" tid %llu is slow, will send keepalive on osd%d\n",
		     req->r_tid, osd->o_osd);
		list_move_tail(&osd->o_keepalive_item, &slow_osds);
	}
	while (!list_empty(&slow_osds)) {
		osd = list_entry(slow_osds.next, struct ceph_osd,
				 o_keepalive_item);
		list_del_init(&osd->o_keepalive_item);
		ceph_con_keepalive(&osd->o_con);
	}

	__schedule_osd_timeout(osdc);
	__send_queued(osdc);
	mutex_unlock(&osdc->request_mutex);
	up_read(&osdc->map_sem);
}

static void handle_osds_timeout(struct work_struct *work)
{
	struct ceph_osd_client *osdc =
		container_of(work, struct ceph_osd_client,
			     osds_timeout_work.work);
	unsigned long delay = osdc->client->options->osd_idle_ttl / 4;
	struct ceph_osd *osd, *nosd;

	dout("%s osdc %p\n", __func__, osdc);
	down_read(&osdc->map_sem);
	mutex_lock(&osdc->request_mutex);

	list_for_each_entry_safe(osd, nosd, &osdc->osd_lru, o_osd_lru) {
		if (time_before(jiffies, osd->lru_ttl))
			break;

		remove_osd(osdc, osd);
	}

	mutex_unlock(&osdc->request_mutex);
	up_read(&osdc->map_sem);
	schedule_delayed_work(&osdc->osds_timeout_work,
			      round_jiffies_relative(delay));
}

static int ceph_oloc_decode(void **p, void *end,
			    struct ceph_object_locator *oloc)
{
	u8 struct_v, struct_cv;
	u32 len;
	void *struct_end;
	int ret = 0;

	ceph_decode_need(p, end, 1 + 1 + 4, e_inval);
	struct_v = ceph_decode_8(p);
	struct_cv = ceph_decode_8(p);
	if (struct_v < 3) {
		pr_warn("got v %d < 3 cv %d of ceph_object_locator\n",
			struct_v, struct_cv);
		goto e_inval;
	}
	if (struct_cv > 6) {
		pr_warn("got v %d cv %d > 6 of ceph_object_locator\n",
			struct_v, struct_cv);
		goto e_inval;
	}
	len = ceph_decode_32(p);
	ceph_decode_need(p, end, len, e_inval);
	struct_end = *p + len;

	oloc->pool = ceph_decode_64(p);
	*p += 4; /* skip preferred */

	len = ceph_decode_32(p);
	if (len > 0) {
		pr_warn("ceph_object_locator::key is set\n");
		goto e_inval;
	}

	if (struct_v >= 5) {
		len = ceph_decode_32(p);
		if (len > 0) {
			pr_warn("ceph_object_locator::nspace is set\n");
			goto e_inval;
		}
	}

	if (struct_v >= 6) {
		s64 hash = ceph_decode_64(p);
		if (hash != -1) {
			pr_warn("ceph_object_locator::hash is set\n");
			goto e_inval;
		}
	}

	/* skip the rest */
	*p = struct_end;
out:
	return ret;

e_inval:
	ret = -EINVAL;
	goto out;
}

static int ceph_redirect_decode(void **p, void *end,
				struct ceph_request_redirect *redir)
{
	u8 struct_v, struct_cv;
	u32 len;
	void *struct_end;
	int ret;

	ceph_decode_need(p, end, 1 + 1 + 4, e_inval);
	struct_v = ceph_decode_8(p);
	struct_cv = ceph_decode_8(p);
	if (struct_cv > 1) {
		pr_warn("got v %d cv %d > 1 of ceph_request_redirect\n",
			struct_v, struct_cv);
		goto e_inval;
	}
	len = ceph_decode_32(p);
	ceph_decode_need(p, end, len, e_inval);
	struct_end = *p + len;

	ret = ceph_oloc_decode(p, end, &redir->oloc);
	if (ret)
		goto out;

	len = ceph_decode_32(p);
	if (len > 0) {
		pr_warn("ceph_request_redirect::object_name is set\n");
		goto e_inval;
	}

	len = ceph_decode_32(p);
	*p += len; /* skip osd_instructions */

	/* skip the rest */
	*p = struct_end;
out:
	return ret;

e_inval:
	ret = -EINVAL;
	goto out;
}

static void complete_request(struct ceph_osd_request *req)
{
	complete_all(&req->r_safe_completion);  /* fsync waiter */
}

/*
 * handle osd op reply.  either call the callback if it is specified,
 * or do the completion to wake up the waiting thread.
 */
static void handle_reply(struct ceph_osd_client *osdc, struct ceph_msg *msg)
{
	void *p, *end;
	struct ceph_osd_request *req;
	struct ceph_request_redirect redir;
	u64 tid;
	int object_len;
	unsigned int numops;
	int payload_len, flags;
	s32 result;
	s32 retry_attempt;
	struct ceph_pg pg;
	int err;
	u32 reassert_epoch;
	u64 reassert_version;
	u32 osdmap_epoch;
	int already_completed;
	u32 bytes;
	u8 decode_redir;
	unsigned int i;

	tid = le64_to_cpu(msg->hdr.tid);
	dout("handle_reply %p tid %llu\n", msg, tid);

	p = msg->front.iov_base;
	end = p + msg->front.iov_len;

	ceph_decode_need(&p, end, 4, bad);
	object_len = ceph_decode_32(&p);
	ceph_decode_need(&p, end, object_len, bad);
	p += object_len;

	err = ceph_decode_pgid(&p, end, &pg);
	if (err)
		goto bad;

	ceph_decode_need(&p, end, 8 + 4 + 4 + 8 + 4, bad);
	flags = ceph_decode_64(&p);
	result = ceph_decode_32(&p);
	reassert_epoch = ceph_decode_32(&p);
	reassert_version = ceph_decode_64(&p);
	osdmap_epoch = ceph_decode_32(&p);

	/* lookup */
	down_read(&osdc->map_sem);
	mutex_lock(&osdc->request_mutex);
	req = lookup_request(&osdc->requests, tid);
	if (req == NULL) {
		dout("handle_reply tid %llu dne\n", tid);
		goto bad_mutex;
	}
	ceph_osdc_get_request(req);

	dout("handle_reply %p tid %llu req %p result %d\n", msg, tid,
	     req, result);

	ceph_decode_need(&p, end, 4, bad_put);
	numops = ceph_decode_32(&p);
	if (numops > CEPH_OSD_MAX_OPS)
		goto bad_put;
	if (numops != req->r_num_ops)
		goto bad_put;
	payload_len = 0;
	ceph_decode_need(&p, end, numops * sizeof(struct ceph_osd_op), bad_put);
	for (i = 0; i < numops; i++) {
		struct ceph_osd_op *op = p;
		int len;

		len = le32_to_cpu(op->payload_len);
		req->r_ops[i].outdata_len = len;
		dout(" op %d has %d bytes\n", i, len);
		payload_len += len;
		p += sizeof(*op);
	}
	bytes = le32_to_cpu(msg->hdr.data_len);
	if (payload_len != bytes) {
		pr_warn("sum of op payload lens %d != data_len %d\n",
			payload_len, bytes);
		goto bad_put;
	}

	ceph_decode_need(&p, end, 4 + numops * 4, bad_put);
	retry_attempt = ceph_decode_32(&p);
	for (i = 0; i < numops; i++)
		req->r_ops[i].rval = ceph_decode_32(&p);

	if (le16_to_cpu(msg->hdr.version) >= 6) {
		p += 8 + 4; /* skip replay_version */
		p += 8; /* skip user_version */

		if (le16_to_cpu(msg->hdr.version) >= 7)
			ceph_decode_8_safe(&p, end, decode_redir, bad_put);
		else
			decode_redir = 1;
	} else {
		decode_redir = 0;
	}

	if (decode_redir) {
		err = ceph_redirect_decode(&p, end, &redir);
		if (err)
			goto bad_put;
	} else {
		redir.oloc.pool = -1;
	}

	if (!ceph_oloc_empty(&redir.oloc)) {
		dout("redirect pool %lld\n", redir.oloc.pool);

		__unregister_request(osdc, req);

		ceph_oloc_copy(&req->r_t.target_oloc, &redir.oloc);

		/*
		 * Start redirect requests with nofail=true.  If
		 * mapping fails, request will end up on the notarget
		 * list, waiting for the new osdmap (which can take
		 * a while), even though the original request mapped
		 * successfully.  In the future we might want to follow
		 * original request's nofail setting here.
		 */
		err = __ceph_osdc_start_request(osdc, req, true);
		BUG_ON(err);

		goto out_unlock;
	}

	already_completed = req->r_got_reply;
	if (!req->r_got_reply) {
		req->r_result = result;
		dout("handle_reply result %d bytes %d\n", req->r_result,
		     bytes);
		if (req->r_result == 0)
			req->r_result = bytes;

		/* in case this is a write and we need to replay, */
		req->r_replay_version.epoch = cpu_to_le32(reassert_epoch);
		req->r_replay_version.version = cpu_to_le64(reassert_version);

		req->r_got_reply = 1;
	} else if ((flags & CEPH_OSD_FLAG_ONDISK) == 0) {
		dout("handle_reply tid %llu dup ack\n", tid);
		goto out_unlock;
	}

	dout("handle_reply tid %llu flags %d\n", tid, flags);

	if (req->r_linger && (flags & CEPH_OSD_FLAG_ONDISK))
		__register_linger_request(osdc, req);

	/* either this is a read, or we got the safe response */
	if (result < 0 ||
	    (flags & CEPH_OSD_FLAG_ONDISK) ||
	    ((flags & CEPH_OSD_FLAG_WRITE) == 0))
		__unregister_request(osdc, req);

	mutex_unlock(&osdc->request_mutex);
	up_read(&osdc->map_sem);

	if (!already_completed) {
		if (req->r_unsafe_callback &&
		    result >= 0 && !(flags & CEPH_OSD_FLAG_ONDISK))
			req->r_unsafe_callback(req, true);
		if (req->r_callback)
			req->r_callback(req);
		else
			complete_all(&req->r_completion);
	}

	if (flags & CEPH_OSD_FLAG_ONDISK) {
		if (req->r_unsafe_callback && already_completed)
			req->r_unsafe_callback(req, false);
		complete_request(req);
	}

out:
	dout("req=%p req->r_linger=%d\n", req, req->r_linger);
	ceph_osdc_put_request(req);
	return;
out_unlock:
	mutex_unlock(&osdc->request_mutex);
	up_read(&osdc->map_sem);
	goto out;

bad_put:
	req->r_result = -EIO;
	__unregister_request(osdc, req);
	if (req->r_callback)
		req->r_callback(req);
	else
		complete_all(&req->r_completion);
	complete_request(req);
	ceph_osdc_put_request(req);
bad_mutex:
	mutex_unlock(&osdc->request_mutex);
	up_read(&osdc->map_sem);
bad:
	pr_err("corrupt osd_op_reply got %d %d\n",
	       (int)msg->front.iov_len, le32_to_cpu(msg->hdr.front_len));
	ceph_msg_dump(msg);
}

static void reset_changed_osds(struct ceph_osd_client *osdc)
{
	struct rb_node *p, *n;

	dout("%s %p\n", __func__, osdc);
	for (p = rb_first(&osdc->osds); p; p = n) {
		struct ceph_osd *osd = rb_entry(p, struct ceph_osd, o_node);

		n = rb_next(p);
		if (!ceph_osd_is_up(osdc->osdmap, osd->o_osd) ||
		    memcmp(&osd->o_con.peer_addr,
			   ceph_osd_addr(osdc->osdmap,
					 osd->o_osd),
			   sizeof(struct ceph_entity_addr)) != 0)
			__reset_osd(osdc, osd);
	}
}

/*
 * Requeue requests whose mapping to an OSD has changed.  If requests map to
 * no osd, request a new map.
 *
 * Caller should hold map_sem for read.
 */
static void kick_requests(struct ceph_osd_client *osdc, bool force_resend,
			  bool force_resend_writes)
{
	struct ceph_osd_request *req, *nreq;
	struct rb_node *p;
	int needmap = 0;
	int err;
	bool force_resend_req;

	dout("kick_requests %s %s\n", force_resend ? " (force resend)" : "",
		force_resend_writes ? " (force resend writes)" : "");
	mutex_lock(&osdc->request_mutex);
	for (p = rb_first(&osdc->requests); p; ) {
		req = rb_entry(p, struct ceph_osd_request, r_node);
		p = rb_next(p);

		/*
		 * For linger requests that have not yet been
		 * registered, move them to the linger list; they'll
		 * be sent to the osd in the loop below.  Unregister
		 * the request before re-registering it as a linger
		 * request to ensure the __map_request() below
		 * will decide it needs to be sent.
		 */
		if (req->r_linger && list_empty(&req->r_linger_item)) {
			dout("%p tid %llu restart on osd%d\n",
			     req, req->r_tid,
			     req->r_osd ? req->r_osd->o_osd : -1);
			ceph_osdc_get_request(req);
			__unregister_request(osdc, req);
			__register_linger_request(osdc, req);
			ceph_osdc_put_request(req);
			continue;
		}

		force_resend_req = force_resend ||
			(force_resend_writes &&
				req->r_flags & CEPH_OSD_FLAG_WRITE);
		err = __map_request(osdc, req, force_resend_req);
		if (err < 0)
			continue;  /* error */
		if (req->r_osd == NULL) {
			dout("%p tid %llu maps to no osd\n", req, req->r_tid);
			needmap++;  /* request a newer map */
		} else if (err > 0) {
			if (!req->r_linger) {
				dout("%p tid %llu requeued on osd%d\n", req,
				     req->r_tid,
				     req->r_osd ? req->r_osd->o_osd : -1);
				req->r_flags |= CEPH_OSD_FLAG_RETRY;
			}
		}
	}

	list_for_each_entry_safe(req, nreq, &osdc->req_linger,
				 r_linger_item) {
		dout("linger req=%p req->r_osd=%p\n", req, req->r_osd);

		err = __map_request(osdc, req,
				    force_resend || force_resend_writes);
		dout("__map_request returned %d\n", err);
		if (err < 0)
			continue;  /* hrm! */
		if (req->r_osd == NULL || err > 0) {
			if (req->r_osd == NULL) {
				dout("lingering %p tid %llu maps to no osd\n",
				     req, req->r_tid);
				/*
				 * A homeless lingering request makes
				 * no sense, as it's job is to keep
				 * a particular OSD connection open.
				 * Request a newer map and kick the
				 * request, knowing that it won't be
				 * resent until we actually get a map
				 * that can tell us where to send it.
				 */
				needmap++;
			}

			dout("kicking lingering %p tid %llu osd%d\n", req,
			     req->r_tid, req->r_osd ? req->r_osd->o_osd : -1);
			__register_request(osdc, req);
			__unregister_linger_request(osdc, req);
		}
	}
	reset_changed_osds(osdc);
	mutex_unlock(&osdc->request_mutex);

	if (needmap) {
		dout("%d requests for down osds, need new map\n", needmap);
		ceph_monc_request_next_osdmap(&osdc->client->monc);
	}
}


/*
 * Process updated osd map.
 *
 * The message contains any number of incremental and full maps, normally
 * indicating some sort of topology change in the cluster.  Kick requests
 * off to different OSDs as needed.
 */
void ceph_osdc_handle_map(struct ceph_osd_client *osdc, struct ceph_msg *msg)
{
	void *p, *end, *next;
	u32 nr_maps, maplen;
	u32 epoch;
	struct ceph_osdmap *newmap = NULL, *oldmap;
	int err;
	struct ceph_fsid fsid;
	bool was_full;

	dout("handle_map have %u\n", osdc->osdmap ? osdc->osdmap->epoch : 0);
	p = msg->front.iov_base;
	end = p + msg->front.iov_len;

	/* verify fsid */
	ceph_decode_need(&p, end, sizeof(fsid), bad);
	ceph_decode_copy(&p, &fsid, sizeof(fsid));
	if (ceph_check_fsid(osdc->client, &fsid) < 0)
		return;

	down_write(&osdc->map_sem);

	was_full = ceph_osdmap_flag(osdc->osdmap, CEPH_OSDMAP_FULL);

	/* incremental maps */
	ceph_decode_32_safe(&p, end, nr_maps, bad);
	dout(" %d inc maps\n", nr_maps);
	while (nr_maps > 0) {
		ceph_decode_need(&p, end, 2*sizeof(u32), bad);
		epoch = ceph_decode_32(&p);
		maplen = ceph_decode_32(&p);
		ceph_decode_need(&p, end, maplen, bad);
		next = p + maplen;
		if (osdc->osdmap && osdc->osdmap->epoch+1 == epoch) {
			dout("applying incremental map %u len %d\n",
			     epoch, maplen);
			newmap = osdmap_apply_incremental(&p, next,
							  osdc->osdmap);
			if (IS_ERR(newmap)) {
				err = PTR_ERR(newmap);
				goto bad;
			}
			BUG_ON(!newmap);
			if (newmap != osdc->osdmap) {
				ceph_osdmap_destroy(osdc->osdmap);
				osdc->osdmap = newmap;
			}
			was_full = was_full ||
				ceph_osdmap_flag(osdc->osdmap,
						 CEPH_OSDMAP_FULL);
			kick_requests(osdc, 0, was_full);
		} else {
			dout("ignoring incremental map %u len %d\n",
			     epoch, maplen);
		}
		p = next;
		nr_maps--;
	}
	if (newmap)
		goto done;

	/* full maps */
	ceph_decode_32_safe(&p, end, nr_maps, bad);
	dout(" %d full maps\n", nr_maps);
	while (nr_maps) {
		ceph_decode_need(&p, end, 2*sizeof(u32), bad);
		epoch = ceph_decode_32(&p);
		maplen = ceph_decode_32(&p);
		ceph_decode_need(&p, end, maplen, bad);
		if (nr_maps > 1) {
			dout("skipping non-latest full map %u len %d\n",
			     epoch, maplen);
		} else if (osdc->osdmap && osdc->osdmap->epoch >= epoch) {
			dout("skipping full map %u len %d, "
			     "older than our %u\n", epoch, maplen,
			     osdc->osdmap->epoch);
		} else {
			int skipped_map = 0;

			dout("taking full map %u len %d\n", epoch, maplen);
			newmap = ceph_osdmap_decode(&p, p+maplen);
			if (IS_ERR(newmap)) {
				err = PTR_ERR(newmap);
				goto bad;
			}
			BUG_ON(!newmap);
			oldmap = osdc->osdmap;
			osdc->osdmap = newmap;
			if (oldmap) {
				if (oldmap->epoch + 1 < newmap->epoch)
					skipped_map = 1;
				ceph_osdmap_destroy(oldmap);
			}
			was_full = was_full ||
				ceph_osdmap_flag(osdc->osdmap,
						 CEPH_OSDMAP_FULL);
			kick_requests(osdc, skipped_map, was_full);
		}
		p += maplen;
		nr_maps--;
	}

	if (!osdc->osdmap)
		goto bad;
done:
	downgrade_write(&osdc->map_sem);
	ceph_monc_got_map(&osdc->client->monc, CEPH_SUB_OSDMAP,
			  osdc->osdmap->epoch);

	/*
	 * subscribe to subsequent osdmap updates if full to ensure
	 * we find out when we are no longer full and stop returning
	 * ENOSPC.
	 */
	if (ceph_osdmap_flag(osdc->osdmap, CEPH_OSDMAP_FULL) ||
		ceph_osdmap_flag(osdc->osdmap, CEPH_OSDMAP_PAUSERD) ||
		ceph_osdmap_flag(osdc->osdmap, CEPH_OSDMAP_PAUSEWR))
		ceph_monc_request_next_osdmap(&osdc->client->monc);

	mutex_lock(&osdc->request_mutex);
	__send_queued(osdc);
	mutex_unlock(&osdc->request_mutex);
	up_read(&osdc->map_sem);
	wake_up_all(&osdc->client->auth_wq);
	return;

bad:
	pr_err("osdc handle_map corrupt msg\n");
	ceph_msg_dump(msg);
	up_write(&osdc->map_sem);
}

/*
 * watch/notify callback event infrastructure
 *
 * These callbacks are used both for watch and notify operations.
 */
static void __release_event(struct kref *kref)
{
	struct ceph_osd_event *event =
		container_of(kref, struct ceph_osd_event, kref);

	dout("__release_event %p\n", event);
	kfree(event);
}

static void get_event(struct ceph_osd_event *event)
{
	kref_get(&event->kref);
}

void ceph_osdc_put_event(struct ceph_osd_event *event)
{
	kref_put(&event->kref, __release_event);
}
EXPORT_SYMBOL(ceph_osdc_put_event);

static void __insert_event(struct ceph_osd_client *osdc,
			     struct ceph_osd_event *new)
{
	struct rb_node **p = &osdc->event_tree.rb_node;
	struct rb_node *parent = NULL;
	struct ceph_osd_event *event = NULL;

	while (*p) {
		parent = *p;
		event = rb_entry(parent, struct ceph_osd_event, node);
		if (new->cookie < event->cookie)
			p = &(*p)->rb_left;
		else if (new->cookie > event->cookie)
			p = &(*p)->rb_right;
		else
			BUG();
	}

	rb_link_node(&new->node, parent, p);
	rb_insert_color(&new->node, &osdc->event_tree);
}

static struct ceph_osd_event *__find_event(struct ceph_osd_client *osdc,
					        u64 cookie)
{
	struct rb_node **p = &osdc->event_tree.rb_node;
	struct rb_node *parent = NULL;
	struct ceph_osd_event *event = NULL;

	while (*p) {
		parent = *p;
		event = rb_entry(parent, struct ceph_osd_event, node);
		if (cookie < event->cookie)
			p = &(*p)->rb_left;
		else if (cookie > event->cookie)
			p = &(*p)->rb_right;
		else
			return event;
	}
	return NULL;
}

static void __remove_event(struct ceph_osd_event *event)
{
	struct ceph_osd_client *osdc = event->osdc;

	if (!RB_EMPTY_NODE(&event->node)) {
		dout("__remove_event removed %p\n", event);
		rb_erase(&event->node, &osdc->event_tree);
		ceph_osdc_put_event(event);
	} else {
		dout("__remove_event didn't remove %p\n", event);
	}
}

int ceph_osdc_create_event(struct ceph_osd_client *osdc,
			   void (*event_cb)(u64, u64, u8, void *),
			   void *data, struct ceph_osd_event **pevent)
{
	struct ceph_osd_event *event;

	event = kmalloc(sizeof(*event), GFP_NOIO);
	if (!event)
		return -ENOMEM;

	dout("create_event %p\n", event);
	event->cb = event_cb;
	event->one_shot = 0;
	event->data = data;
	event->osdc = osdc;
	INIT_LIST_HEAD(&event->osd_node);
	RB_CLEAR_NODE(&event->node);
	kref_init(&event->kref);   /* one ref for us */
	kref_get(&event->kref);    /* one ref for the caller */

	spin_lock(&osdc->event_lock);
	event->cookie = ++osdc->event_count;
	__insert_event(osdc, event);
	spin_unlock(&osdc->event_lock);

	*pevent = event;
	return 0;
}
EXPORT_SYMBOL(ceph_osdc_create_event);

void ceph_osdc_cancel_event(struct ceph_osd_event *event)
{
	struct ceph_osd_client *osdc = event->osdc;

	dout("cancel_event %p\n", event);
	spin_lock(&osdc->event_lock);
	__remove_event(event);
	spin_unlock(&osdc->event_lock);
	ceph_osdc_put_event(event); /* caller's */
}
EXPORT_SYMBOL(ceph_osdc_cancel_event);


static void do_event_work(struct work_struct *work)
{
	struct ceph_osd_event_work *event_work =
		container_of(work, struct ceph_osd_event_work, work);
	struct ceph_osd_event *event = event_work->event;
	u64 ver = event_work->ver;
	u64 notify_id = event_work->notify_id;
	u8 opcode = event_work->opcode;

	dout("do_event_work completing %p\n", event);
	event->cb(ver, notify_id, opcode, event->data);
	dout("do_event_work completed %p\n", event);
	ceph_osdc_put_event(event);
	kfree(event_work);
}


/*
 * Process osd watch notifications
 */
static void handle_watch_notify(struct ceph_osd_client *osdc,
				struct ceph_msg *msg)
{
	void *p, *end;
	u8 proto_ver;
	u64 cookie, ver, notify_id;
	u8 opcode;
	struct ceph_osd_event *event;
	struct ceph_osd_event_work *event_work;

	p = msg->front.iov_base;
	end = p + msg->front.iov_len;

	ceph_decode_8_safe(&p, end, proto_ver, bad);
	ceph_decode_8_safe(&p, end, opcode, bad);
	ceph_decode_64_safe(&p, end, cookie, bad);
	ceph_decode_64_safe(&p, end, ver, bad);
	ceph_decode_64_safe(&p, end, notify_id, bad);

	spin_lock(&osdc->event_lock);
	event = __find_event(osdc, cookie);
	if (event) {
		BUG_ON(event->one_shot);
		get_event(event);
	}
	spin_unlock(&osdc->event_lock);
	dout("handle_watch_notify cookie %lld ver %lld event %p\n",
	     cookie, ver, event);
	if (event) {
		event_work = kmalloc(sizeof(*event_work), GFP_NOIO);
		if (!event_work) {
			pr_err("couldn't allocate event_work\n");
			ceph_osdc_put_event(event);
			return;
		}
		INIT_WORK(&event_work->work, do_event_work);
		event_work->event = event;
		event_work->ver = ver;
		event_work->notify_id = notify_id;
		event_work->opcode = opcode;

		queue_work(osdc->notify_wq, &event_work->work);
	}

	return;

bad:
	pr_err("osdc handle_watch_notify corrupt msg\n");
}

/*
 * Register request, send initial attempt.
 */
int ceph_osdc_start_request(struct ceph_osd_client *osdc,
			    struct ceph_osd_request *req,
			    bool nofail)
{
	int rc;

	down_read(&osdc->map_sem);
	mutex_lock(&osdc->request_mutex);

	rc = __ceph_osdc_start_request(osdc, req, nofail);

	mutex_unlock(&osdc->request_mutex);
	up_read(&osdc->map_sem);

	return rc;
}
EXPORT_SYMBOL(ceph_osdc_start_request);

/*
 * Unregister a registered request.  The request is not completed (i.e.
 * no callbacks or wakeups) - higher layers are supposed to know what
 * they are canceling.
 */
void ceph_osdc_cancel_request(struct ceph_osd_request *req)
{
	struct ceph_osd_client *osdc = req->r_osdc;

	mutex_lock(&osdc->request_mutex);
	if (req->r_linger)
		__unregister_linger_request(osdc, req);
	__unregister_request(osdc, req);
	mutex_unlock(&osdc->request_mutex);

	dout("%s %p tid %llu canceled\n", __func__, req, req->r_tid);
}
EXPORT_SYMBOL(ceph_osdc_cancel_request);

/*
 * wait for a request to complete
 */
int ceph_osdc_wait_request(struct ceph_osd_client *osdc,
			   struct ceph_osd_request *req)
{
	int rc;

	dout("%s %p tid %llu\n", __func__, req, req->r_tid);

	rc = wait_for_completion_interruptible(&req->r_completion);
	if (rc < 0) {
		dout("%s %p tid %llu interrupted\n", __func__, req, req->r_tid);
		ceph_osdc_cancel_request(req);
		complete_request(req);
		return rc;
	}

	dout("%s %p tid %llu result %d\n", __func__, req, req->r_tid,
	     req->r_result);
	return req->r_result;
}
EXPORT_SYMBOL(ceph_osdc_wait_request);

/*
 * sync - wait for all in-flight requests to flush.  avoid starvation.
 */
void ceph_osdc_sync(struct ceph_osd_client *osdc)
{
	struct ceph_osd_request *req;
	u64 last_tid, next_tid = 0;

	mutex_lock(&osdc->request_mutex);
	last_tid = osdc->last_tid;
	while (1) {
		req = __lookup_request_ge(osdc, next_tid);
		if (!req)
			break;
		if (req->r_tid > last_tid)
			break;

		next_tid = req->r_tid + 1;
		if ((req->r_flags & CEPH_OSD_FLAG_WRITE) == 0)
			continue;

		ceph_osdc_get_request(req);
		mutex_unlock(&osdc->request_mutex);
		dout("sync waiting on tid %llu (last is %llu)\n",
		     req->r_tid, last_tid);
		wait_for_completion(&req->r_safe_completion);
		mutex_lock(&osdc->request_mutex);
		ceph_osdc_put_request(req);
	}
	mutex_unlock(&osdc->request_mutex);
	dout("sync done (thru tid %llu)\n", last_tid);
}
EXPORT_SYMBOL(ceph_osdc_sync);

/*
 * Call all pending notify callbacks - for use after a watch is
 * unregistered, to make sure no more callbacks for it will be invoked
 */
void ceph_osdc_flush_notifies(struct ceph_osd_client *osdc)
{
	flush_workqueue(osdc->notify_wq);
}
EXPORT_SYMBOL(ceph_osdc_flush_notifies);


/*
 * init, shutdown
 */
int ceph_osdc_init(struct ceph_osd_client *osdc, struct ceph_client *client)
{
	int err;

	dout("init\n");
	osdc->client = client;
	osdc->osdmap = NULL;
	init_rwsem(&osdc->map_sem);
	mutex_init(&osdc->request_mutex);
	osdc->last_tid = 0;
	osdc->osds = RB_ROOT;
	INIT_LIST_HEAD(&osdc->osd_lru);
	osdc->requests = RB_ROOT;
	INIT_LIST_HEAD(&osdc->req_lru);
	INIT_LIST_HEAD(&osdc->req_unsent);
	INIT_LIST_HEAD(&osdc->req_notarget);
	INIT_LIST_HEAD(&osdc->req_linger);
	osdc->num_requests = 0;
	INIT_DELAYED_WORK(&osdc->timeout_work, handle_timeout);
	INIT_DELAYED_WORK(&osdc->osds_timeout_work, handle_osds_timeout);
	spin_lock_init(&osdc->event_lock);
	osdc->event_tree = RB_ROOT;
	osdc->event_count = 0;

	schedule_delayed_work(&osdc->osds_timeout_work,
	    round_jiffies_relative(osdc->client->options->osd_idle_ttl));

	err = -ENOMEM;
	osdc->req_mempool = mempool_create_slab_pool(10,
						     ceph_osd_request_cache);
	if (!osdc->req_mempool)
		goto out;

	err = ceph_msgpool_init(&osdc->msgpool_op, CEPH_MSG_OSD_OP,
				PAGE_SIZE, 10, true, "osd_op");
	if (err < 0)
		goto out_mempool;
	err = ceph_msgpool_init(&osdc->msgpool_op_reply, CEPH_MSG_OSD_OPREPLY,
				PAGE_SIZE, 10, true, "osd_op_reply");
	if (err < 0)
		goto out_msgpool;

	err = -ENOMEM;
	osdc->notify_wq = create_singlethread_workqueue("ceph-watch-notify");
	if (!osdc->notify_wq)
		goto out_msgpool_reply;

	return 0;

out_msgpool_reply:
	ceph_msgpool_destroy(&osdc->msgpool_op_reply);
out_msgpool:
	ceph_msgpool_destroy(&osdc->msgpool_op);
out_mempool:
	mempool_destroy(osdc->req_mempool);
out:
	return err;
}

void ceph_osdc_stop(struct ceph_osd_client *osdc)
{
	flush_workqueue(osdc->notify_wq);
	destroy_workqueue(osdc->notify_wq);
	cancel_delayed_work_sync(&osdc->timeout_work);
	cancel_delayed_work_sync(&osdc->osds_timeout_work);

	mutex_lock(&osdc->request_mutex);
	while (!RB_EMPTY_ROOT(&osdc->osds)) {
		struct ceph_osd *osd = rb_entry(rb_first(&osdc->osds),
						struct ceph_osd, o_node);
		remove_osd(osdc, osd);
	}
	mutex_unlock(&osdc->request_mutex);

	if (osdc->osdmap) {
		ceph_osdmap_destroy(osdc->osdmap);
		osdc->osdmap = NULL;
	}
	mempool_destroy(osdc->req_mempool);
	ceph_msgpool_destroy(&osdc->msgpool_op);
	ceph_msgpool_destroy(&osdc->msgpool_op_reply);
}

/*
 * Read some contiguous pages.  If we cross a stripe boundary, shorten
 * *plen.  Return number of bytes read, or error.
 */
int ceph_osdc_readpages(struct ceph_osd_client *osdc,
			struct ceph_vino vino, struct ceph_file_layout *layout,
			u64 off, u64 *plen,
			u32 truncate_seq, u64 truncate_size,
			struct page **pages, int num_pages, int page_align)
{
	struct ceph_osd_request *req;
	int rc = 0;

	dout("readpages on ino %llx.%llx on %llu~%llu\n", vino.ino,
	     vino.snap, off, *plen);
	req = ceph_osdc_new_request(osdc, layout, vino, off, plen, 0, 1,
				    CEPH_OSD_OP_READ, CEPH_OSD_FLAG_READ,
				    NULL, truncate_seq, truncate_size,
				    false);
	if (IS_ERR(req))
		return PTR_ERR(req);

	/* it may be a short read due to an object boundary */
	osd_req_op_extent_osd_data_pages(req, 0,
				pages, *plen, page_align, false, false);

	dout("readpages  final extent is %llu~%llu (%llu bytes align %d)\n",
	     off, *plen, *plen, page_align);

	rc = ceph_osdc_start_request(osdc, req, false);
	if (!rc)
		rc = ceph_osdc_wait_request(osdc, req);

	ceph_osdc_put_request(req);
	dout("readpages result %d\n", rc);
	return rc;
}
EXPORT_SYMBOL(ceph_osdc_readpages);

/*
 * do a synchronous write on N pages
 */
int ceph_osdc_writepages(struct ceph_osd_client *osdc, struct ceph_vino vino,
			 struct ceph_file_layout *layout,
			 struct ceph_snap_context *snapc,
			 u64 off, u64 len,
			 u32 truncate_seq, u64 truncate_size,
			 struct timespec *mtime,
			 struct page **pages, int num_pages)
{
	struct ceph_osd_request *req;
	int rc = 0;
	int page_align = off & ~PAGE_MASK;

	req = ceph_osdc_new_request(osdc, layout, vino, off, &len, 0, 1,
				    CEPH_OSD_OP_WRITE,
				    CEPH_OSD_FLAG_ONDISK | CEPH_OSD_FLAG_WRITE,
				    snapc, truncate_seq, truncate_size,
				    true);
	if (IS_ERR(req))
		return PTR_ERR(req);

	/* it may be a short write due to an object boundary */
	osd_req_op_extent_osd_data_pages(req, 0, pages, len, page_align,
				false, false);
	dout("writepages %llu~%llu (%llu bytes)\n", off, len, len);

	req->r_mtime = *mtime;
	rc = ceph_osdc_start_request(osdc, req, true);
	if (!rc)
		rc = ceph_osdc_wait_request(osdc, req);

	ceph_osdc_put_request(req);
	if (rc == 0)
		rc = len;
	dout("writepages result %d\n", rc);
	return rc;
}
EXPORT_SYMBOL(ceph_osdc_writepages);

int ceph_osdc_setup(void)
{
	size_t size = sizeof(struct ceph_osd_request) +
	    CEPH_OSD_SLAB_OPS * sizeof(struct ceph_osd_req_op);

	BUG_ON(ceph_osd_request_cache);
	ceph_osd_request_cache = kmem_cache_create("ceph_osd_request", size,
						   0, 0, NULL);

	return ceph_osd_request_cache ? 0 : -ENOMEM;
}
EXPORT_SYMBOL(ceph_osdc_setup);

void ceph_osdc_cleanup(void)
{
	BUG_ON(!ceph_osd_request_cache);
	kmem_cache_destroy(ceph_osd_request_cache);
	ceph_osd_request_cache = NULL;
}
EXPORT_SYMBOL(ceph_osdc_cleanup);

/*
 * handle incoming message
 */
static void dispatch(struct ceph_connection *con, struct ceph_msg *msg)
{
	struct ceph_osd *osd = con->private;
	struct ceph_osd_client *osdc;
	int type = le16_to_cpu(msg->hdr.type);

	if (!osd)
		goto out;
	osdc = osd->o_osdc;

	switch (type) {
	case CEPH_MSG_OSD_MAP:
		ceph_osdc_handle_map(osdc, msg);
		break;
	case CEPH_MSG_OSD_OPREPLY:
		handle_reply(osdc, msg);
		break;
	case CEPH_MSG_WATCH_NOTIFY:
		handle_watch_notify(osdc, msg);
		break;

	default:
		pr_err("received unknown message type %d %s\n", type,
		       ceph_msg_type_name(type));
	}
out:
	ceph_msg_put(msg);
}

/*
 * Lookup and return message for incoming reply.  Don't try to do
 * anything about a larger than preallocated data portion of the
 * message at the moment - for now, just skip the message.
 */
static struct ceph_msg *get_reply(struct ceph_connection *con,
				  struct ceph_msg_header *hdr,
				  int *skip)
{
	struct ceph_osd *osd = con->private;
	struct ceph_osd_client *osdc = osd->o_osdc;
	struct ceph_msg *m;
	struct ceph_osd_request *req;
	int front_len = le32_to_cpu(hdr->front_len);
	int data_len = le32_to_cpu(hdr->data_len);
	u64 tid;

	tid = le64_to_cpu(hdr->tid);
	mutex_lock(&osdc->request_mutex);
	req = lookup_request(&osdc->requests, tid);
	if (!req) {
		dout("%s osd%d tid %llu unknown, skipping\n", __func__,
		     osd->o_osd, tid);
		m = NULL;
		*skip = 1;
		goto out;
	}

	ceph_msg_revoke_incoming(req->r_reply);

	if (front_len > req->r_reply->front_alloc_len) {
		pr_warn("%s osd%d tid %llu front %d > preallocated %d\n",
			__func__, osd->o_osd, req->r_tid, front_len,
			req->r_reply->front_alloc_len);
		m = ceph_msg_new(CEPH_MSG_OSD_OPREPLY, front_len, GFP_NOFS,
				 false);
		if (!m)
			goto out;
		ceph_msg_put(req->r_reply);
		req->r_reply = m;
	}

	if (data_len > req->r_reply->data_length) {
		pr_warn("%s osd%d tid %llu data %d > preallocated %zu, skipping\n",
			__func__, osd->o_osd, req->r_tid, data_len,
			req->r_reply->data_length);
		m = NULL;
		*skip = 1;
		goto out;
	}

	m = ceph_msg_get(req->r_reply);
	dout("get_reply tid %lld %p\n", tid, m);

out:
	mutex_unlock(&osdc->request_mutex);
	return m;
}

static struct ceph_msg *alloc_msg(struct ceph_connection *con,
				  struct ceph_msg_header *hdr,
				  int *skip)
{
	struct ceph_osd *osd = con->private;
	int type = le16_to_cpu(hdr->type);
	int front = le32_to_cpu(hdr->front_len);

	*skip = 0;
	switch (type) {
	case CEPH_MSG_OSD_MAP:
	case CEPH_MSG_WATCH_NOTIFY:
		return ceph_msg_new(type, front, GFP_NOFS, false);
	case CEPH_MSG_OSD_OPREPLY:
		return get_reply(con, hdr, skip);
	default:
		pr_info("alloc_msg unexpected msg type %d from osd%d\n", type,
			osd->o_osd);
		*skip = 1;
		return NULL;
	}
}

/*
 * Wrappers to refcount containing ceph_osd struct
 */
static struct ceph_connection *get_osd_con(struct ceph_connection *con)
{
	struct ceph_osd *osd = con->private;
	if (get_osd(osd))
		return con;
	return NULL;
}

static void put_osd_con(struct ceph_connection *con)
{
	struct ceph_osd *osd = con->private;
	put_osd(osd);
}

/*
 * authentication
 */
/*
 * Note: returned pointer is the address of a structure that's
 * managed separately.  Caller must *not* attempt to free it.
 */
static struct ceph_auth_handshake *get_authorizer(struct ceph_connection *con,
					int *proto, int force_new)
{
	struct ceph_osd *o = con->private;
	struct ceph_osd_client *osdc = o->o_osdc;
	struct ceph_auth_client *ac = osdc->client->monc.auth;
	struct ceph_auth_handshake *auth = &o->o_auth;

	if (force_new && auth->authorizer) {
		ceph_auth_destroy_authorizer(auth->authorizer);
		auth->authorizer = NULL;
	}
	if (!auth->authorizer) {
		int ret = ceph_auth_create_authorizer(ac, CEPH_ENTITY_TYPE_OSD,
						      auth);
		if (ret)
			return ERR_PTR(ret);
	} else {
		int ret = ceph_auth_update_authorizer(ac, CEPH_ENTITY_TYPE_OSD,
						     auth);
		if (ret)
			return ERR_PTR(ret);
	}
	*proto = ac->protocol;

	return auth;
}


static int verify_authorizer_reply(struct ceph_connection *con, int len)
{
	struct ceph_osd *o = con->private;
	struct ceph_osd_client *osdc = o->o_osdc;
	struct ceph_auth_client *ac = osdc->client->monc.auth;

	return ceph_auth_verify_authorizer_reply(ac, o->o_auth.authorizer, len);
}

static int invalidate_authorizer(struct ceph_connection *con)
{
	struct ceph_osd *o = con->private;
	struct ceph_osd_client *osdc = o->o_osdc;
	struct ceph_auth_client *ac = osdc->client->monc.auth;

	ceph_auth_invalidate_authorizer(ac, CEPH_ENTITY_TYPE_OSD);
	return ceph_monc_validate_auth(&osdc->client->monc);
}

static int osd_sign_message(struct ceph_msg *msg)
{
	struct ceph_osd *o = msg->con->private;
	struct ceph_auth_handshake *auth = &o->o_auth;

	return ceph_auth_sign_message(auth, msg);
}

static int osd_check_message_signature(struct ceph_msg *msg)
{
	struct ceph_osd *o = msg->con->private;
	struct ceph_auth_handshake *auth = &o->o_auth;

	return ceph_auth_check_message_signature(auth, msg);
}

static const struct ceph_connection_operations osd_con_ops = {
	.get = get_osd_con,
	.put = put_osd_con,
	.dispatch = dispatch,
	.get_authorizer = get_authorizer,
	.verify_authorizer_reply = verify_authorizer_reply,
	.invalidate_authorizer = invalidate_authorizer,
	.alloc_msg = alloc_msg,
	.sign_message = osd_sign_message,
	.check_message_signature = osd_check_message_signature,
	.fault = osd_reset,
};


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

#define OSD_OP_FRONT_LEN	4096
#define OSD_OPREPLY_FRONT_LEN	512

static struct kmem_cache	*ceph_osd_request_cache;

static const struct ceph_connection_operations osd_con_ops;

static void __send_queued(struct ceph_osd_client *osdc);
static int __reset_osd(struct ceph_osd_client *osdc, struct ceph_osd *osd);
static void __register_request(struct ceph_osd_client *osdc,
			       struct ceph_osd_request *req);
static void __unregister_linger_request(struct ceph_osd_client *osdc,
					struct ceph_osd_request *req);
static void __send_request(struct ceph_osd_client *osdc,
			   struct ceph_osd_request *req);

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

#define osd_req_op_data(oreq, whch, typ, fld)	\
	({						\
		BUG_ON(whch >= (oreq)->r_num_ops);	\
		&(oreq)->r_ops[whch].typ.fld;		\
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

struct ceph_osd_data *
osd_req_op_cls_response_data(struct ceph_osd_request *osd_req,
			unsigned int which)
{
	return osd_req_op_data(osd_req, which, cls, response_data);
}
EXPORT_SYMBOL(osd_req_op_cls_response_data);	/* ??? */

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
		ceph_osd_data_release(&op->extent.osd_data);
		break;
	case CEPH_OSD_OP_CALL:
		ceph_osd_data_release(&op->cls.request_info);
		ceph_osd_data_release(&op->cls.request_data);
		ceph_osd_data_release(&op->cls.response_data);
		break;
	default:
		break;
	}
}

/*
 * requests
 */
void ceph_osdc_release_request(struct kref *kref)
{
	struct ceph_osd_request *req;
	unsigned int which;

	req = container_of(kref, struct ceph_osd_request, r_kref);
	if (req->r_request)
		ceph_msg_put(req->r_request);
	if (req->r_reply) {
		ceph_msg_revoke_incoming(req->r_reply);
		ceph_msg_put(req->r_reply);
	}

	for (which = 0; which < req->r_num_ops; which++)
		osd_req_op_data_release(req, which);

	ceph_put_snap_context(req->r_snapc);
	if (req->r_mempool)
		mempool_free(req, req->r_osdc->req_mempool);
	else
		kmem_cache_free(ceph_osd_request_cache, req);

}
EXPORT_SYMBOL(ceph_osdc_release_request);

struct ceph_osd_request *ceph_osdc_alloc_request(struct ceph_osd_client *osdc,
					       struct ceph_snap_context *snapc,
					       unsigned int num_ops,
					       bool use_mempool,
					       gfp_t gfp_flags)
{
	struct ceph_osd_request *req;
	struct ceph_msg *msg;
	size_t msg_size;

	BUILD_BUG_ON(CEPH_OSD_MAX_OP > U16_MAX);
	BUG_ON(num_ops > CEPH_OSD_MAX_OP);

	msg_size = 4 + 4 + 8 + 8 + 4+8;
	msg_size += 2 + 4 + 8 + 4 + 4; /* oloc */
	msg_size += 1 + 8 + 4 + 4;     /* pg_t */
	msg_size += 4 + CEPH_MAX_OID_NAME_LEN; /* oid */
	msg_size += 2 + num_ops*sizeof(struct ceph_osd_op);
	msg_size += 8;  /* snapid */
	msg_size += 8;  /* snap_seq */
	msg_size += 8 * (snapc ? snapc->num_snaps : 0);  /* snaps */
	msg_size += 4;

	if (use_mempool) {
		req = mempool_alloc(osdc->req_mempool, gfp_flags);
		memset(req, 0, sizeof(*req));
	} else {
		req = kmem_cache_zalloc(ceph_osd_request_cache, gfp_flags);
	}
	if (req == NULL)
		return NULL;

	req->r_osdc = osdc;
	req->r_mempool = use_mempool;
	req->r_num_ops = num_ops;

	kref_init(&req->r_kref);
	init_completion(&req->r_completion);
	init_completion(&req->r_safe_completion);
	RB_CLEAR_NODE(&req->r_node);
	INIT_LIST_HEAD(&req->r_unsafe_item);
	INIT_LIST_HEAD(&req->r_linger_item);
	INIT_LIST_HEAD(&req->r_linger_osd_item);
	INIT_LIST_HEAD(&req->r_req_lru_item);
	INIT_LIST_HEAD(&req->r_osd_item);

	req->r_base_oloc.pool = -1;
	req->r_target_oloc.pool = -1;

	/* create reply message */
	if (use_mempool)
		msg = ceph_msgpool_get(&osdc->msgpool_op_reply, 0);
	else
		msg = ceph_msg_new(CEPH_MSG_OSD_OPREPLY,
				   OSD_OPREPLY_FRONT_LEN, gfp_flags, true);
	if (!msg) {
		ceph_osdc_put_request(req);
		return NULL;
	}
	req->r_reply = msg;

	/* create request message; allow space for oid */
	if (use_mempool)
		msg = ceph_msgpool_get(&osdc->msgpool_op, 0);
	else
		msg = ceph_msg_new(CEPH_MSG_OSD_OP, msg_size, gfp_flags, true);
	if (!msg) {
		ceph_osdc_put_request(req);
		return NULL;
	}

	memset(msg->front.iov_base, 0, msg->front.iov_len);

	req->r_request = msg;

	return req;
}
EXPORT_SYMBOL(ceph_osdc_alloc_request);

static bool osd_req_opcode_valid(u16 opcode)
{
	switch (opcode) {
	case CEPH_OSD_OP_READ:
	case CEPH_OSD_OP_STAT:
	case CEPH_OSD_OP_MAPEXT:
	case CEPH_OSD_OP_MASKTRUNC:
	case CEPH_OSD_OP_SPARSE_READ:
	case CEPH_OSD_OP_NOTIFY:
	case CEPH_OSD_OP_NOTIFY_ACK:
	case CEPH_OSD_OP_ASSERT_VER:
	case CEPH_OSD_OP_WRITE:
	case CEPH_OSD_OP_WRITEFULL:
	case CEPH_OSD_OP_TRUNCATE:
	case CEPH_OSD_OP_ZERO:
	case CEPH_OSD_OP_DELETE:
	case CEPH_OSD_OP_APPEND:
	case CEPH_OSD_OP_STARTSYNC:
	case CEPH_OSD_OP_SETTRUNC:
	case CEPH_OSD_OP_TRIMTRUNC:
	case CEPH_OSD_OP_TMAPUP:
	case CEPH_OSD_OP_TMAPPUT:
	case CEPH_OSD_OP_TMAPGET:
	case CEPH_OSD_OP_CREATE:
	case CEPH_OSD_OP_ROLLBACK:
	case CEPH_OSD_OP_WATCH:
	case CEPH_OSD_OP_OMAPGETKEYS:
	case CEPH_OSD_OP_OMAPGETVALS:
	case CEPH_OSD_OP_OMAPGETHEADER:
	case CEPH_OSD_OP_OMAPGETVALSBYKEYS:
	case CEPH_OSD_OP_OMAPSETVALS:
	case CEPH_OSD_OP_OMAPSETHEADER:
	case CEPH_OSD_OP_OMAPCLEAR:
	case CEPH_OSD_OP_OMAPRMKEYS:
	case CEPH_OSD_OP_OMAP_CMP:
	case CEPH_OSD_OP_SETALLOCHINT:
	case CEPH_OSD_OP_CLONERANGE:
	case CEPH_OSD_OP_ASSERT_SRC_VERSION:
	case CEPH_OSD_OP_SRC_CMPXATTR:
	case CEPH_OSD_OP_GETXATTR:
	case CEPH_OSD_OP_GETXATTRS:
	case CEPH_OSD_OP_CMPXATTR:
	case CEPH_OSD_OP_SETXATTR:
	case CEPH_OSD_OP_SETXATTRS:
	case CEPH_OSD_OP_RESETXATTRS:
	case CEPH_OSD_OP_RMXATTR:
	case CEPH_OSD_OP_PULL:
	case CEPH_OSD_OP_PUSH:
	case CEPH_OSD_OP_BALANCEREADS:
	case CEPH_OSD_OP_UNBALANCEREADS:
	case CEPH_OSD_OP_SCRUB:
	case CEPH_OSD_OP_SCRUB_RESERVE:
	case CEPH_OSD_OP_SCRUB_UNRESERVE:
	case CEPH_OSD_OP_SCRUB_STOP:
	case CEPH_OSD_OP_SCRUB_MAP:
	case CEPH_OSD_OP_WRLOCK:
	case CEPH_OSD_OP_WRUNLOCK:
	case CEPH_OSD_OP_RDLOCK:
	case CEPH_OSD_OP_RDUNLOCK:
	case CEPH_OSD_OP_UPLOCK:
	case CEPH_OSD_OP_DNLOCK:
	case CEPH_OSD_OP_CALL:
	case CEPH_OSD_OP_PGLS:
	case CEPH_OSD_OP_PGLS_FILTER:
		return true;
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
				u16 opcode)
{
	struct ceph_osd_req_op *op;

	BUG_ON(which >= osd_req->r_num_ops);
	BUG_ON(!osd_req_opcode_valid(opcode));

	op = &osd_req->r_ops[which];
	memset(op, 0, sizeof (*op));
	op->op = opcode;

	return op;
}

void osd_req_op_init(struct ceph_osd_request *osd_req,
				unsigned int which, u16 opcode)
{
	(void)_osd_req_op_init(osd_req, which, opcode);
}
EXPORT_SYMBOL(osd_req_op_init);

void osd_req_op_extent_init(struct ceph_osd_request *osd_req,
				unsigned int which, u16 opcode,
				u64 offset, u64 length,
				u64 truncate_size, u32 truncate_seq)
{
	struct ceph_osd_req_op *op = _osd_req_op_init(osd_req, which, opcode);
	size_t payload_len = 0;

	BUG_ON(opcode != CEPH_OSD_OP_READ && opcode != CEPH_OSD_OP_WRITE &&
	       opcode != CEPH_OSD_OP_DELETE && opcode != CEPH_OSD_OP_ZERO &&
	       opcode != CEPH_OSD_OP_TRUNCATE);

	op->extent.offset = offset;
	op->extent.length = length;
	op->extent.truncate_size = truncate_size;
	op->extent.truncate_seq = truncate_seq;
	if (opcode == CEPH_OSD_OP_WRITE)
		payload_len += length;

	op->payload_len = payload_len;
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
	op->payload_len -= previous - length;
}
EXPORT_SYMBOL(osd_req_op_extent_update);

void osd_req_op_cls_init(struct ceph_osd_request *osd_req, unsigned int which,
			u16 opcode, const char *class, const char *method)
{
	struct ceph_osd_req_op *op = _osd_req_op_init(osd_req, which, opcode);
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

	op->cls.argc = 0;	/* currently unused */

	op->payload_len = payload_len;
}
EXPORT_SYMBOL(osd_req_op_cls_init);

void osd_req_op_watch_init(struct ceph_osd_request *osd_req,
				unsigned int which, u16 opcode,
				u64 cookie, u64 version, int flag)
{
	struct ceph_osd_req_op *op = _osd_req_op_init(osd_req, which, opcode);

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
						      CEPH_OSD_OP_SETALLOCHINT);

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

static u64 osd_req_encode_op(struct ceph_osd_request *req,
			      struct ceph_osd_op *dst, unsigned int which)
{
	struct ceph_osd_req_op *src;
	struct ceph_osd_data *osd_data;
	u64 request_data_len = 0;
	u64 data_length;

	BUG_ON(which >= req->r_num_ops);
	src = &req->r_ops[which];
	if (WARN_ON(!osd_req_opcode_valid(src->op))) {
		pr_err("unrecognized osd opcode %d\n", src->op);

		return 0;
	}

	switch (src->op) {
	case CEPH_OSD_OP_STAT:
		osd_data = &src->raw_data_in;
		ceph_osdc_msg_data_add(req->r_reply, osd_data);
		break;
	case CEPH_OSD_OP_READ:
	case CEPH_OSD_OP_WRITE:
	case CEPH_OSD_OP_ZERO:
	case CEPH_OSD_OP_DELETE:
	case CEPH_OSD_OP_TRUNCATE:
		if (src->op == CEPH_OSD_OP_WRITE)
			request_data_len = src->extent.length;
		dst->extent.offset = cpu_to_le64(src->extent.offset);
		dst->extent.length = cpu_to_le64(src->extent.length);
		dst->extent.truncate_size =
			cpu_to_le64(src->extent.truncate_size);
		dst->extent.truncate_seq =
			cpu_to_le32(src->extent.truncate_seq);
		osd_data = &src->extent.osd_data;
		if (src->op == CEPH_OSD_OP_WRITE)
			ceph_osdc_msg_data_add(req->r_request, osd_data);
		else
			ceph_osdc_msg_data_add(req->r_reply, osd_data);
		break;
	case CEPH_OSD_OP_CALL:
		dst->cls.class_len = src->cls.class_len;
		dst->cls.method_len = src->cls.method_len;
		osd_data = &src->cls.request_info;
		ceph_osdc_msg_data_add(req->r_request, osd_data);
		BUG_ON(osd_data->type != CEPH_OSD_DATA_TYPE_PAGELIST);
		request_data_len = osd_data->pagelist->length;

		osd_data = &src->cls.request_data;
		data_length = ceph_osd_data_length(osd_data);
		if (data_length) {
			BUG_ON(osd_data->type == CEPH_OSD_DATA_TYPE_NONE);
			dst->cls.indata_len = cpu_to_le32(data_length);
			ceph_osdc_msg_data_add(req->r_request, osd_data);
			src->payload_len += data_length;
			request_data_len += data_length;
		}
		osd_data = &src->cls.response_data;
		ceph_osdc_msg_data_add(req->r_reply, osd_data);
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
	default:
		pr_err("unsupported osd opcode %s\n",
			ceph_osd_op_name(src->op));
		WARN_ON(1);

		return 0;
	}

	dst->op = cpu_to_le16(src->op);
	dst->flags = cpu_to_le32(src->flags);
	dst->payload_len = cpu_to_le32(src->payload_len);

	return request_data_len;
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
					       u64 off, u64 *plen, int num_ops,
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
	u32 object_size;
	u64 object_base;
	int r;

	BUG_ON(opcode != CEPH_OSD_OP_READ && opcode != CEPH_OSD_OP_WRITE &&
	       opcode != CEPH_OSD_OP_DELETE && opcode != CEPH_OSD_OP_ZERO &&
	       opcode != CEPH_OSD_OP_TRUNCATE);

	req = ceph_osdc_alloc_request(osdc, snapc, num_ops, use_mempool,
					GFP_NOFS);
	if (!req)
		return ERR_PTR(-ENOMEM);

	req->r_flags = flags;

	/* calculate max write size */
	r = calc_layout(layout, off, plen, &objnum, &objoff, &objlen);
	if (r < 0) {
		ceph_osdc_put_request(req);
		return ERR_PTR(r);
	}

	object_size = le32_to_cpu(layout->fl_object_size);
	object_base = off - objoff;
	if (!(truncate_seq == 1 && truncate_size == -1ULL)) {
		if (truncate_size <= object_base) {
			truncate_size = 0;
		} else {
			truncate_size -= object_base;
			if (truncate_size > object_size)
				truncate_size = object_size;
		}
	}

	osd_req_op_extent_init(req, 0, opcode, objoff, objlen,
				truncate_size, truncate_seq);

	/*
	 * A second op in the ops array means the caller wants to
	 * also issue a include a 'startsync' command so that the
	 * osd will flush data quickly.
	 */
	if (num_ops > 1)
		osd_req_op_init(req, 1, CEPH_OSD_OP_STARTSYNC);

	req->r_base_oloc.pool = ceph_file_layout_pg_pool(*layout);

	snprintf(req->r_base_oid.name, sizeof(req->r_base_oid.name),
		 "%llx.%08llx", vino.ino, objnum);
	req->r_base_oid.name_len = strlen(req->r_base_oid.name);

	return req;
}
EXPORT_SYMBOL(ceph_osdc_new_request);

/*
 * We keep osd requests in an rbtree, sorted by ->r_tid.
 */
static void __insert_request(struct ceph_osd_client *osdc,
			     struct ceph_osd_request *new)
{
	struct rb_node **p = &osdc->requests.rb_node;
	struct rb_node *parent = NULL;
	struct ceph_osd_request *req = NULL;

	while (*p) {
		parent = *p;
		req = rb_entry(parent, struct ceph_osd_request, r_node);
		if (new->r_tid < req->r_tid)
			p = &(*p)->rb_left;
		else if (new->r_tid > req->r_tid)
			p = &(*p)->rb_right;
		else
			BUG();
	}

	rb_link_node(&new->r_node, parent, p);
	rb_insert_color(&new->r_node, &osdc->requests);
}

static struct ceph_osd_request *__lookup_request(struct ceph_osd_client *osdc,
						 u64 tid)
{
	struct ceph_osd_request *req;
	struct rb_node *n = osdc->requests.rb_node;

	while (n) {
		req = rb_entry(n, struct ceph_osd_request, r_node);
		if (tid < req->r_tid)
			n = n->rb_left;
		else if (tid > req->r_tid)
			n = n->rb_right;
		else
			return req;
	}
	return NULL;
}

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

/*
 * Resubmit requests pending on the given osd.
 */
static void __kick_osd_requests(struct ceph_osd_client *osdc,
				struct ceph_osd *osd)
{
	struct ceph_osd_request *req, *nreq;
	LIST_HEAD(resend);
	int err;

	dout("__kick_osd_requests osd%d\n", osd->o_osd);
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
		list_move_tail(&req->r_req_lru_item, &resend);
		dout("requeueing %p tid %llu osd%d\n", req, req->r_tid,
		     osd->o_osd);
		if (!req->r_linger)
			req->r_flags |= CEPH_OSD_FLAG_RETRY;
	}
	list_splice(&resend, &osdc->req_unsent);

	/*
	 * Linger requests are re-registered before sending, which
	 * sets up a new tid for each.  We add them to the unsent
	 * list at the end to keep things in tid order.
	 */
	list_for_each_entry_safe(req, nreq, &osd->o_linger_requests,
				 r_linger_osd_item) {
		/*
		 * reregister request prior to unregistering linger so
		 * that r_osd is preserved.
		 */
		BUG_ON(!list_empty(&req->r_req_lru_item));
		__register_request(osdc, req);
		list_add_tail(&req->r_req_lru_item, &osdc->req_unsent);
		list_add_tail(&req->r_osd_item, &req->r_osd->o_requests);
		__unregister_linger_request(osdc, req);
		dout("requeued lingering %p tid %llu osd%d\n", req, req->r_tid,
		     osd->o_osd);
	}
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
	if (atomic_dec_and_test(&osd->o_ref) && osd->o_auth.authorizer) {
		struct ceph_auth_client *ac = osd->o_osdc->client->monc.auth;

		ceph_auth_destroy_authorizer(ac, osd->o_auth.authorizer);
		kfree(osd);
	}
}

/*
 * remove an osd from our map
 */
static void __remove_osd(struct ceph_osd_client *osdc, struct ceph_osd *osd)
{
	dout("__remove_osd %p\n", osd);
	BUG_ON(!list_empty(&osd->o_requests));
	rb_erase(&osd->o_node, &osdc->osds);
	list_del_init(&osd->o_osd_lru);
	ceph_con_close(&osd->o_con);
	put_osd(osd);
}

static void remove_all_osds(struct ceph_osd_client *osdc)
{
	dout("%s %p\n", __func__, osdc);
	mutex_lock(&osdc->request_mutex);
	while (!RB_EMPTY_ROOT(&osdc->osds)) {
		struct ceph_osd *osd = rb_entry(rb_first(&osdc->osds),
						struct ceph_osd, o_node);
		__remove_osd(osdc, osd);
	}
	mutex_unlock(&osdc->request_mutex);
}

static void __move_osd_to_lru(struct ceph_osd_client *osdc,
			      struct ceph_osd *osd)
{
	dout("%s %p\n", __func__, osd);
	BUG_ON(!list_empty(&osd->o_osd_lru));

	list_add_tail(&osd->o_osd_lru, &osdc->osd_lru);
	osd->lru_ttl = jiffies + osdc->client->options->osd_idle_ttl * HZ;
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

static void remove_old_osds(struct ceph_osd_client *osdc)
{
	struct ceph_osd *osd, *nosd;

	dout("__remove_old_osds %p\n", osdc);
	mutex_lock(&osdc->request_mutex);
	list_for_each_entry_safe(osd, nosd, &osdc->osd_lru, o_osd_lru) {
		if (time_before(jiffies, osd->lru_ttl))
			break;
		__remove_osd(osdc, osd);
	}
	mutex_unlock(&osdc->request_mutex);
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
		__remove_osd(osdc, osd);

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

static void __insert_osd(struct ceph_osd_client *osdc, struct ceph_osd *new)
{
	struct rb_node **p = &osdc->osds.rb_node;
	struct rb_node *parent = NULL;
	struct ceph_osd *osd = NULL;

	dout("__insert_osd %p osd%d\n", new, new->o_osd);
	while (*p) {
		parent = *p;
		osd = rb_entry(parent, struct ceph_osd, o_node);
		if (new->o_osd < osd->o_osd)
			p = &(*p)->rb_left;
		else if (new->o_osd > osd->o_osd)
			p = &(*p)->rb_right;
		else
			BUG();
	}

	rb_link_node(&new->o_node, parent, p);
	rb_insert_color(&new->o_node, &osdc->osds);
}

static struct ceph_osd *__lookup_osd(struct ceph_osd_client *osdc, int o)
{
	struct ceph_osd *osd;
	struct rb_node *n = osdc->osds.rb_node;

	while (n) {
		osd = rb_entry(n, struct ceph_osd, o_node);
		if (o < osd->o_osd)
			n = n->rb_left;
		else if (o > osd->o_osd)
			n = n->rb_right;
		else
			return osd;
	}
	return NULL;
}

static void __schedule_osd_timeout(struct ceph_osd_client *osdc)
{
	schedule_delayed_work(&osdc->timeout_work,
			osdc->client->options->osd_keepalive_timeout * HZ);
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
	__insert_request(osdc, req);
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
	rb_erase(&req->r_node, &osdc->requests);
	osdc->num_requests--;

	if (req->r_osd) {
		/* make sure the original request isn't in flight. */
		ceph_msg_revoke(req->r_request);

		list_del_init(&req->r_osd_item);
		maybe_move_osd_to_lru(osdc, req->r_osd);
		if (list_empty(&req->r_linger_item))
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
	dout("__register_linger_request %p\n", req);
	ceph_osdc_get_request(req);
	list_add_tail(&req->r_linger_item, &osdc->req_linger);
	if (req->r_osd)
		list_add_tail(&req->r_linger_osd_item,
			      &req->r_osd->o_linger_requests);
}

static void __unregister_linger_request(struct ceph_osd_client *osdc,
					struct ceph_osd_request *req)
{
	dout("__unregister_linger_request %p\n", req);
	list_del_init(&req->r_linger_item);
	if (req->r_osd) {
		list_del_init(&req->r_linger_osd_item);
		maybe_move_osd_to_lru(osdc, req->r_osd);
		if (list_empty(&req->r_osd_item))
			req->r_osd = NULL;
	}
	ceph_osdc_put_request(req);
}

void ceph_osdc_unregister_linger_request(struct ceph_osd_client *osdc,
					 struct ceph_osd_request *req)
{
	mutex_lock(&osdc->request_mutex);
	if (req->r_linger) {
		req->r_linger = 0;
		__unregister_linger_request(osdc, req);
	}
	mutex_unlock(&osdc->request_mutex);
}
EXPORT_SYMBOL(ceph_osdc_unregister_linger_request);

void ceph_osdc_set_request_linger(struct ceph_osd_client *osdc,
				  struct ceph_osd_request *req)
{
	if (!req->r_linger) {
		dout("set_request_linger %p\n", req);
		req->r_linger = 1;
	}
}
EXPORT_SYMBOL(ceph_osdc_set_request_linger);

/*
 * Returns whether a request should be blocked from being sent
 * based on the current osdmap and osd_client settings.
 *
 * Caller should hold map_sem for read.
 */
static bool __req_should_be_paused(struct ceph_osd_client *osdc,
				   struct ceph_osd_request *req)
{
	bool pauserd = ceph_osdmap_flag(osdc->osdmap, CEPH_OSDMAP_PAUSERD);
	bool pausewr = ceph_osdmap_flag(osdc->osdmap, CEPH_OSDMAP_PAUSEWR) ||
		ceph_osdmap_flag(osdc->osdmap, CEPH_OSDMAP_FULL);
	return (req->r_flags & CEPH_OSD_FLAG_READ && pauserd) ||
		(req->r_flags & CEPH_OSD_FLAG_WRITE && pausewr);
}

/*
 * Calculate mapping of a request to a PG.  Takes tiering into account.
 */
static int __calc_request_pg(struct ceph_osdmap *osdmap,
			     struct ceph_osd_request *req,
			     struct ceph_pg *pg_out)
{
	bool need_check_tiering;

	need_check_tiering = false;
	if (req->r_target_oloc.pool == -1) {
		req->r_target_oloc = req->r_base_oloc; /* struct */
		need_check_tiering = true;
	}
	if (req->r_target_oid.name_len == 0) {
		ceph_oid_copy(&req->r_target_oid, &req->r_base_oid);
		need_check_tiering = true;
	}

	if (need_check_tiering &&
	    (req->r_flags & CEPH_OSD_FLAG_IGNORE_OVERLAY) == 0) {
		struct ceph_pg_pool_info *pi;

		pi = ceph_pg_pool_by_id(osdmap, req->r_target_oloc.pool);
		if (pi) {
			if ((req->r_flags & CEPH_OSD_FLAG_READ) &&
			    pi->read_tier >= 0)
				req->r_target_oloc.pool = pi->read_tier;
			if ((req->r_flags & CEPH_OSD_FLAG_WRITE) &&
			    pi->write_tier >= 0)
				req->r_target_oloc.pool = pi->write_tier;
		}
		/* !pi is caught in ceph_oloc_oid_to_pg() */
	}

	return ceph_oloc_oid_to_pg(osdmap, &req->r_target_oloc,
				   &req->r_target_oid, pg_out);
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
	struct ceph_pg pgid;
	int acting[CEPH_PG_MAX_SIZE];
	int num, o;
	int err;
	bool was_paused;

	dout("map_request %p tid %lld\n", req, req->r_tid);

	err = __calc_request_pg(osdc->osdmap, req, &pgid);
	if (err) {
		list_move(&req->r_req_lru_item, &osdc->req_notarget);
		return err;
	}
	req->r_pgid = pgid;

	num = ceph_calc_pg_acting(osdc->osdmap, pgid, acting, &o);
	if (num < 0)
		num = 0;

	was_paused = req->r_paused;
	req->r_paused = __req_should_be_paused(osdc, req);
	if (was_paused && !req->r_paused)
		force_resend = 1;

	if ((!force_resend &&
	     req->r_osd && req->r_osd->o_osd == o &&
	     req->r_sent >= req->r_osd->o_incarnation &&
	     req->r_num_pg_osds == num &&
	     memcmp(req->r_pg_osds, acting, sizeof(acting[0])*num) == 0) ||
	    (req->r_osd == NULL && o == -1) ||
	    req->r_paused)
		return 0;  /* no change */

	dout("map_request tid %llu pgid %lld.%x osd%d (was osd%d)\n",
	     req->r_tid, pgid.pool, pgid.seed, o,
	     req->r_osd ? req->r_osd->o_osd : -1);

	/* record full pg acting set */
	memcpy(req->r_pg_osds, acting, sizeof(acting[0]) * num);
	req->r_num_pg_osds = num;

	if (req->r_osd) {
		__cancel_request(req);
		list_del_init(&req->r_osd_item);
		req->r_osd = NULL;
	}

	req->r_osd = __lookup_osd(osdc, o);
	if (!req->r_osd && o >= 0) {
		err = -ENOMEM;
		req->r_osd = create_osd(osdc, o);
		if (!req->r_osd) {
			list_move(&req->r_req_lru_item, &osdc->req_notarget);
			goto out;
		}

		dout("map_request osd %p is osd%d\n", req->r_osd, o);
		__insert_osd(osdc, req->r_osd);

		ceph_con_open(&req->r_osd->o_con,
			      CEPH_ENTITY_TYPE_OSD, o,
			      &osdc->osdmap->osd_addr[o]);
	}

	if (req->r_osd) {
		__remove_osd_from_lru(req->r_osd);
		list_add_tail(&req->r_osd_item, &req->r_osd->o_requests);
		list_move_tail(&req->r_req_lru_item, &osdc->req_unsent);
	} else {
		list_move_tail(&req->r_req_lru_item, &osdc->req_notarget);
	}
	err = 1;   /* osd or pg changed */

out:
	return err;
}

/*
 * caller should hold map_sem (for read) and request_mutex
 */
static void __send_request(struct ceph_osd_client *osdc,
			   struct ceph_osd_request *req)
{
	void *p;

	dout("send_request %p tid %llu to osd%d flags %d pg %lld.%x\n",
	     req, req->r_tid, req->r_osd->o_osd, req->r_flags,
	     (unsigned long long)req->r_pgid.pool, req->r_pgid.seed);

	/* fill in message content that changes each time we send it */
	put_unaligned_le32(osdc->osdmap->epoch, req->r_request_osdmap_epoch);
	put_unaligned_le32(req->r_flags, req->r_request_flags);
	put_unaligned_le64(req->r_target_oloc.pool, req->r_request_pool);
	p = req->r_request_pgid;
	ceph_encode_64(&p, req->r_pgid.pool);
	ceph_encode_32(&p, req->r_pgid.seed);
	put_unaligned_le64(1, req->r_request_attempts);  /* FIXME */
	memcpy(req->r_request_reassert_version, &req->r_reassert_version,
	       sizeof(req->r_reassert_version));

	req->r_stamp = jiffies;
	list_move_tail(&req->r_req_lru_item, &osdc->req_lru);

	ceph_msg_get(req->r_request); /* send consumes a ref */

	req->r_sent = req->r_osd->o_incarnation;

	ceph_con_send(&req->r_osd->o_con, req->r_request);
}

/*
 * Send any requests in the queue (req_unsent).
 */
static void __send_queued(struct ceph_osd_client *osdc)
{
	struct ceph_osd_request *req, *tmp;

	dout("__send_queued\n");
	list_for_each_entry_safe(req, tmp, &osdc->req_unsent, r_req_lru_item)
		__send_request(osdc, req);
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
	struct ceph_osd_request *req;
	struct ceph_osd *osd;
	unsigned long keepalive =
		osdc->client->options->osd_keepalive_timeout * HZ;
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
		if (time_before(jiffies, req->r_stamp + keepalive))
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
	unsigned long delay =
		osdc->client->options->osd_idle_ttl * HZ >> 2;

	dout("osds timeout\n");
	down_read(&osdc->map_sem);
	remove_old_osds(osdc);
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
static void handle_reply(struct ceph_osd_client *osdc, struct ceph_msg *msg,
			 struct ceph_connection *con)
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
	req = __lookup_request(osdc, tid);
	if (req == NULL) {
		dout("handle_reply tid %llu dne\n", tid);
		goto bad_mutex;
	}
	ceph_osdc_get_request(req);

	dout("handle_reply %p tid %llu req %p result %d\n", msg, tid,
	     req, result);

	ceph_decode_need(&p, end, 4, bad_put);
	numops = ceph_decode_32(&p);
	if (numops > CEPH_OSD_MAX_OP)
		goto bad_put;
	if (numops != req->r_num_ops)
		goto bad_put;
	payload_len = 0;
	ceph_decode_need(&p, end, numops * sizeof(struct ceph_osd_op), bad_put);
	for (i = 0; i < numops; i++) {
		struct ceph_osd_op *op = p;
		int len;

		len = le32_to_cpu(op->payload_len);
		req->r_reply_op_len[i] = len;
		dout(" op %d has %d bytes\n", i, len);
		payload_len += len;
		p += sizeof(*op);
	}
	bytes = le32_to_cpu(msg->hdr.data_len);
	if (payload_len != bytes) {
		pr_warning("sum of op payload lens %d != data_len %d",
			   payload_len, bytes);
		goto bad_put;
	}

	ceph_decode_need(&p, end, 4 + numops * 4, bad_put);
	retry_attempt = ceph_decode_32(&p);
	for (i = 0; i < numops; i++)
		req->r_reply_op_result[i] = ceph_decode_32(&p);

	if (le16_to_cpu(msg->hdr.version) >= 6) {
		p += 8 + 4; /* skip replay_version */
		p += 8; /* skip user_version */

		err = ceph_redirect_decode(&p, end, &redir);
		if (err)
			goto bad_put;
	} else {
		redir.oloc.pool = -1;
	}

	if (redir.oloc.pool != -1) {
		dout("redirect pool %lld\n", redir.oloc.pool);

		__unregister_request(osdc, req);

		req->r_target_oloc = redir.oloc; /* struct */

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
		req->r_reassert_version.epoch = cpu_to_le32(reassert_epoch);
		req->r_reassert_version.version = cpu_to_le64(reassert_version);

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
			req->r_callback(req, msg);
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
		req->r_callback(req, msg);
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
		if (err == 0)
			continue;  /* no change and no osd was specified */
		if (err < 0)
			continue;  /* hrm! */
		if (req->r_osd == NULL) {
			dout("tid %llu maps to no valid osd\n", req->r_tid);
			needmap++;  /* request a newer map */
			continue;
		}

		dout("kicking lingering %p tid %llu osd%d\n", req, req->r_tid,
		     req->r_osd ? req->r_osd->o_osd : -1);
		__register_request(osdc, req);
		__unregister_linger_request(osdc, req);
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
							  osdc->osdmap,
							  &osdc->client->msgr);
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
	ceph_monc_got_osdmap(&osdc->client->monc, osdc->osdmap->epoch);

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
			dout("ERROR: could not allocate event_work\n");
			goto done_err;
		}
		INIT_WORK(&event_work->work, do_event_work);
		event_work->event = event;
		event_work->ver = ver;
		event_work->notify_id = notify_id;
		event_work->opcode = opcode;
		if (!queue_work(osdc->notify_wq, &event_work->work)) {
			dout("WARNING: failed to queue notify event work\n");
			goto done_err;
		}
	}

	return;

done_err:
	ceph_osdc_put_event(event);
	return;

bad:
	pr_err("osdc handle_watch_notify corrupt msg\n");
}

/*
 * build new request AND message
 *
 */
void ceph_osdc_build_request(struct ceph_osd_request *req, u64 off,
				struct ceph_snap_context *snapc, u64 snap_id,
				struct timespec *mtime)
{
	struct ceph_msg *msg = req->r_request;
	void *p;
	size_t msg_size;
	int flags = req->r_flags;
	u64 data_len;
	unsigned int i;

	req->r_snapid = snap_id;
	req->r_snapc = ceph_get_snap_context(snapc);

	/* encode request */
	msg->hdr.version = cpu_to_le16(4);

	p = msg->front.iov_base;
	ceph_encode_32(&p, 1);   /* client_inc  is always 1 */
	req->r_request_osdmap_epoch = p;
	p += 4;
	req->r_request_flags = p;
	p += 4;
	if (req->r_flags & CEPH_OSD_FLAG_WRITE)
		ceph_encode_timespec(p, mtime);
	p += sizeof(struct ceph_timespec);
	req->r_request_reassert_version = p;
	p += sizeof(struct ceph_eversion); /* will get filled in */

	/* oloc */
	ceph_encode_8(&p, 4);
	ceph_encode_8(&p, 4);
	ceph_encode_32(&p, 8 + 4 + 4);
	req->r_request_pool = p;
	p += 8;
	ceph_encode_32(&p, -1);  /* preferred */
	ceph_encode_32(&p, 0);   /* key len */

	ceph_encode_8(&p, 1);
	req->r_request_pgid = p;
	p += 8 + 4;
	ceph_encode_32(&p, -1);  /* preferred */

	/* oid */
	ceph_encode_32(&p, req->r_base_oid.name_len);
	memcpy(p, req->r_base_oid.name, req->r_base_oid.name_len);
	dout("oid '%.*s' len %d\n", req->r_base_oid.name_len,
	     req->r_base_oid.name, req->r_base_oid.name_len);
	p += req->r_base_oid.name_len;

	/* ops--can imply data */
	ceph_encode_16(&p, (u16)req->r_num_ops);
	data_len = 0;
	for (i = 0; i < req->r_num_ops; i++) {
		data_len += osd_req_encode_op(req, p, i);
		p += sizeof(struct ceph_osd_op);
	}

	/* snaps */
	ceph_encode_64(&p, req->r_snapid);
	ceph_encode_64(&p, req->r_snapc ? req->r_snapc->seq : 0);
	ceph_encode_32(&p, req->r_snapc ? req->r_snapc->num_snaps : 0);
	if (req->r_snapc) {
		for (i = 0; i < snapc->num_snaps; i++) {
			ceph_encode_64(&p, req->r_snapc->snaps[i]);
		}
	}

	req->r_request_attempts = p;
	p += 4;

	/* data */
	if (flags & CEPH_OSD_FLAG_WRITE) {
		u16 data_off;

		/*
		 * The header "data_off" is a hint to the receiver
		 * allowing it to align received data into its
		 * buffers such that there's no need to re-copy
		 * it before writing it to disk (direct I/O).
		 */
		data_off = (u16) (off & 0xffff);
		req->r_request->hdr.data_off = cpu_to_le16(data_off);
	}
	req->r_request->hdr.data_len = cpu_to_le32(data_len);

	BUG_ON(p > msg->front.iov_base + msg->front.iov_len);
	msg_size = p - msg->front.iov_base;
	msg->front.iov_len = msg_size;
	msg->hdr.front_len = cpu_to_le32(msg_size);

	dout("build_request msg_size was %d\n", (int)msg_size);
}
EXPORT_SYMBOL(ceph_osdc_build_request);

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
 * wait for a request to complete
 */
int ceph_osdc_wait_request(struct ceph_osd_client *osdc,
			   struct ceph_osd_request *req)
{
	int rc;

	rc = wait_for_completion_interruptible(&req->r_completion);
	if (rc < 0) {
		mutex_lock(&osdc->request_mutex);
		__cancel_request(req);
		__unregister_request(osdc, req);
		mutex_unlock(&osdc->request_mutex);
		complete_request(req);
		dout("wait_request tid %llu canceled/timed out\n", req->r_tid);
		return rc;
	}

	dout("wait_request tid %llu result %d\n", req->r_tid, req->r_result);
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
	init_completion(&osdc->map_waiters);
	osdc->last_requested_map = 0;
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
	   round_jiffies_relative(osdc->client->options->osd_idle_ttl * HZ));

	err = -ENOMEM;
	osdc->req_mempool = mempool_create_kmalloc_pool(10,
					sizeof(struct ceph_osd_request));
	if (!osdc->req_mempool)
		goto out;

	err = ceph_msgpool_init(&osdc->msgpool_op, CEPH_MSG_OSD_OP,
				OSD_OP_FRONT_LEN, 10, true,
				"osd_op");
	if (err < 0)
		goto out_mempool;
	err = ceph_msgpool_init(&osdc->msgpool_op_reply, CEPH_MSG_OSD_OPREPLY,
				OSD_OPREPLY_FRONT_LEN, 10, true,
				"osd_op_reply");
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
	if (osdc->osdmap) {
		ceph_osdmap_destroy(osdc->osdmap);
		osdc->osdmap = NULL;
	}
	remove_all_osds(osdc);
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
	req = ceph_osdc_new_request(osdc, layout, vino, off, plen, 1,
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

	ceph_osdc_build_request(req, off, NULL, vino.snap, NULL);

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

	BUG_ON(vino.snap != CEPH_NOSNAP);	/* snapshots aren't writeable */
	req = ceph_osdc_new_request(osdc, layout, vino, off, &len, 1,
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

	ceph_osdc_build_request(req, off, snapc, CEPH_NOSNAP, mtime);

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
	BUG_ON(ceph_osd_request_cache);
	ceph_osd_request_cache = kmem_cache_create("ceph_osd_request",
					sizeof (struct ceph_osd_request),
					__alignof__(struct ceph_osd_request),
					0, NULL);

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
		handle_reply(osdc, msg, con);
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
 * lookup and return message for incoming reply.  set up reply message
 * pages.
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
	req = __lookup_request(osdc, tid);
	if (!req) {
		*skip = 1;
		m = NULL;
		dout("get_reply unknown tid %llu from osd%d\n", tid,
		     osd->o_osd);
		goto out;
	}

	if (req->r_reply->con)
		dout("%s revoking msg %p from old con %p\n", __func__,
		     req->r_reply, req->r_reply->con);
	ceph_msg_revoke_incoming(req->r_reply);

	if (front_len > req->r_reply->front_alloc_len) {
		pr_warning("get_reply front %d > preallocated %d (%u#%llu)\n",
			   front_len, req->r_reply->front_alloc_len,
			   (unsigned int)con->peer_name.type,
			   le64_to_cpu(con->peer_name.num));
		m = ceph_msg_new(CEPH_MSG_OSD_OPREPLY, front_len, GFP_NOFS,
				 false);
		if (!m)
			goto out;
		ceph_msg_put(req->r_reply);
		req->r_reply = m;
	}
	m = ceph_msg_get(req->r_reply);

	if (data_len > 0) {
		struct ceph_osd_data *osd_data;

		/*
		 * XXX This is assuming there is only one op containing
		 * XXX page data.  Probably OK for reads, but this
		 * XXX ought to be done more generally.
		 */
		osd_data = osd_req_op_extent_osd_data(req, 0);
		if (osd_data->type == CEPH_OSD_DATA_TYPE_PAGES) {
			if (osd_data->pages &&
				unlikely(osd_data->length < data_len)) {

				pr_warning("tid %lld reply has %d bytes "
					"we had only %llu bytes ready\n",
					tid, data_len, osd_data->length);
				*skip = 1;
				ceph_msg_put(m);
				m = NULL;
				goto out;
			}
		}
	}
	*skip = 0;
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
		ceph_auth_destroy_authorizer(ac, auth->authorizer);
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

static const struct ceph_connection_operations osd_con_ops = {
	.get = get_osd_con,
	.put = put_osd_con,
	.dispatch = dispatch,
	.get_authorizer = get_authorizer,
	.verify_authorizer_reply = verify_authorizer_reply,
	.invalidate_authorizer = invalidate_authorizer,
	.alloc_msg = alloc_msg,
	.fault = osd_reset,
};

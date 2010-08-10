#include "ceph_debug.h"

#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "super.h"
#include "osd_client.h"
#include "messenger.h"
#include "decode.h"
#include "auth.h"

#define OSD_OP_FRONT_LEN	4096
#define OSD_OPREPLY_FRONT_LEN	512

static const struct ceph_connection_operations osd_con_ops;
static int __kick_requests(struct ceph_osd_client *osdc,
			  struct ceph_osd *kickosd);

static void kick_requests(struct ceph_osd_client *osdc, struct ceph_osd *osd);

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
static void calc_layout(struct ceph_osd_client *osdc,
			struct ceph_vino vino, struct ceph_file_layout *layout,
			u64 off, u64 *plen,
			struct ceph_osd_request *req)
{
	struct ceph_osd_request_head *reqhead = req->r_request->front.iov_base;
	struct ceph_osd_op *op = (void *)(reqhead + 1);
	u64 orig_len = *plen;
	u64 objoff, objlen;    /* extent in object */
	u64 bno;

	reqhead->snapid = cpu_to_le64(vino.snap);

	/* object extent? */
	ceph_calc_file_object_mapping(layout, off, plen, &bno,
				      &objoff, &objlen);
	if (*plen < orig_len)
		dout(" skipping last %llu, final file extent %llu~%llu\n",
		     orig_len - *plen, off, *plen);

	sprintf(req->r_oid, "%llx.%08llx", vino.ino, bno);
	req->r_oid_len = strlen(req->r_oid);

	op->extent.offset = cpu_to_le64(objoff);
	op->extent.length = cpu_to_le64(objlen);
	req->r_num_pages = calc_pages_for(off, *plen);

	dout("calc_layout %s (%d) %llu~%llu (%d pages)\n",
	     req->r_oid, req->r_oid_len, objoff, objlen, req->r_num_pages);
}

/*
 * requests
 */
void ceph_osdc_release_request(struct kref *kref)
{
	struct ceph_osd_request *req = container_of(kref,
						    struct ceph_osd_request,
						    r_kref);

	if (req->r_request)
		ceph_msg_put(req->r_request);
	if (req->r_reply)
		ceph_msg_put(req->r_reply);
	if (req->r_con_filling_msg) {
		dout("release_request revoking pages %p from con %p\n",
		     req->r_pages, req->r_con_filling_msg);
		ceph_con_revoke_message(req->r_con_filling_msg,
				      req->r_reply);
		ceph_con_put(req->r_con_filling_msg);
	}
	if (req->r_own_pages)
		ceph_release_page_vector(req->r_pages,
					 req->r_num_pages);
	ceph_put_snap_context(req->r_snapc);
	if (req->r_mempool)
		mempool_free(req, req->r_osdc->req_mempool);
	else
		kfree(req);
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
					       int opcode, int flags,
					       struct ceph_snap_context *snapc,
					       int do_sync,
					       u32 truncate_seq,
					       u64 truncate_size,
					       struct timespec *mtime,
					       bool use_mempool, int num_reply)
{
	struct ceph_osd_request *req;
	struct ceph_msg *msg;
	struct ceph_osd_request_head *head;
	struct ceph_osd_op *op;
	void *p;
	int num_op = 1 + do_sync;
	size_t msg_size = sizeof(*head) + num_op*sizeof(*op);
	int i;

	if (use_mempool) {
		req = mempool_alloc(osdc->req_mempool, GFP_NOFS);
		memset(req, 0, sizeof(*req));
	} else {
		req = kzalloc(sizeof(*req), GFP_NOFS);
	}
	if (req == NULL)
		return NULL;

	req->r_osdc = osdc;
	req->r_mempool = use_mempool;
	kref_init(&req->r_kref);
	init_completion(&req->r_completion);
	init_completion(&req->r_safe_completion);
	INIT_LIST_HEAD(&req->r_unsafe_item);
	req->r_flags = flags;

	WARN_ON((flags & (CEPH_OSD_FLAG_READ|CEPH_OSD_FLAG_WRITE)) == 0);

	/* create reply message */
	if (use_mempool)
		msg = ceph_msgpool_get(&osdc->msgpool_op_reply, 0);
	else
		msg = ceph_msg_new(CEPH_MSG_OSD_OPREPLY,
				   OSD_OPREPLY_FRONT_LEN, GFP_NOFS);
	if (!msg) {
		ceph_osdc_put_request(req);
		return NULL;
	}
	req->r_reply = msg;

	/* create request message; allow space for oid */
	msg_size += 40;
	if (snapc)
		msg_size += sizeof(u64) * snapc->num_snaps;
	if (use_mempool)
		msg = ceph_msgpool_get(&osdc->msgpool_op, 0);
	else
		msg = ceph_msg_new(CEPH_MSG_OSD_OP, msg_size, GFP_NOFS);
	if (!msg) {
		ceph_osdc_put_request(req);
		return NULL;
	}
	msg->hdr.type = cpu_to_le16(CEPH_MSG_OSD_OP);
	memset(msg->front.iov_base, 0, msg->front.iov_len);
	head = msg->front.iov_base;
	op = (void *)(head + 1);
	p = (void *)(op + num_op);

	req->r_request = msg;
	req->r_snapc = ceph_get_snap_context(snapc);

	head->client_inc = cpu_to_le32(1); /* always, for now. */
	head->flags = cpu_to_le32(flags);
	if (flags & CEPH_OSD_FLAG_WRITE)
		ceph_encode_timespec(&head->mtime, mtime);
	head->num_ops = cpu_to_le16(num_op);
	op->op = cpu_to_le16(opcode);

	/* calculate max write size */
	calc_layout(osdc, vino, layout, off, plen, req);
	req->r_file_layout = *layout;  /* keep a copy */

	if (flags & CEPH_OSD_FLAG_WRITE) {
		req->r_request->hdr.data_off = cpu_to_le16(off);
		req->r_request->hdr.data_len = cpu_to_le32(*plen);
		op->payload_len = cpu_to_le32(*plen);
	}
	op->extent.truncate_size = cpu_to_le64(truncate_size);
	op->extent.truncate_seq = cpu_to_le32(truncate_seq);

	/* fill in oid */
	head->object_len = cpu_to_le32(req->r_oid_len);
	memcpy(p, req->r_oid, req->r_oid_len);
	p += req->r_oid_len;

	if (do_sync) {
		op++;
		op->op = cpu_to_le16(CEPH_OSD_OP_STARTSYNC);
	}
	if (snapc) {
		head->snap_seq = cpu_to_le64(snapc->seq);
		head->num_snaps = cpu_to_le32(snapc->num_snaps);
		for (i = 0; i < snapc->num_snaps; i++) {
			put_unaligned_le64(snapc->snaps[i], p);
			p += sizeof(u64);
		}
	}

	BUG_ON(p > msg->front.iov_base + msg->front.iov_len);
	msg_size = p - msg->front.iov_base;
	msg->front.iov_len = msg_size;
	msg->hdr.front_len = cpu_to_le32(msg_size);
	return req;
}

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
	kick_requests(osdc, osd);
	up_read(&osdc->map_sem);
}

/*
 * Track open sessions with osds.
 */
static struct ceph_osd *create_osd(struct ceph_osd_client *osdc)
{
	struct ceph_osd *osd;

	osd = kzalloc(sizeof(*osd), GFP_NOFS);
	if (!osd)
		return NULL;

	atomic_set(&osd->o_ref, 1);
	osd->o_osdc = osdc;
	INIT_LIST_HEAD(&osd->o_requests);
	INIT_LIST_HEAD(&osd->o_osd_lru);
	osd->o_incarnation = 1;

	ceph_con_init(osdc->client->msgr, &osd->o_con);
	osd->o_con.private = osd;
	osd->o_con.ops = &osd_con_ops;
	osd->o_con.peer_name.type = CEPH_ENTITY_TYPE_OSD;

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
		struct ceph_auth_client *ac = osd->o_osdc->client->monc.auth;

		if (osd->o_authorizer)
			ac->ops->destroy_authorizer(ac, osd->o_authorizer);
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

static void __move_osd_to_lru(struct ceph_osd_client *osdc,
			      struct ceph_osd *osd)
{
	dout("__move_osd_to_lru %p\n", osd);
	BUG_ON(!list_empty(&osd->o_osd_lru));
	list_add_tail(&osd->o_osd_lru, &osdc->osd_lru);
	osd->lru_ttl = jiffies + osdc->client->mount_args->osd_idle_ttl * HZ;
}

static void __remove_osd_from_lru(struct ceph_osd *osd)
{
	dout("__remove_osd_from_lru %p\n", osd);
	if (!list_empty(&osd->o_osd_lru))
		list_del_init(&osd->o_osd_lru);
}

static void remove_old_osds(struct ceph_osd_client *osdc, int remove_all)
{
	struct ceph_osd *osd, *nosd;

	dout("__remove_old_osds %p\n", osdc);
	mutex_lock(&osdc->request_mutex);
	list_for_each_entry_safe(osd, nosd, &osdc->osd_lru, o_osd_lru) {
		if (!remove_all && time_before(jiffies, osd->lru_ttl))
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
	struct ceph_osd_request *req;
	int ret = 0;

	dout("__reset_osd %p osd%d\n", osd, osd->o_osd);
	if (list_empty(&osd->o_requests)) {
		__remove_osd(osdc, osd);
	} else if (memcmp(&osdc->osdmap->osd_addr[osd->o_osd],
			  &osd->o_con.peer_addr,
			  sizeof(osd->o_con.peer_addr)) == 0 &&
		   !ceph_con_opened(&osd->o_con)) {
		dout(" osd addr hasn't changed and connection never opened,"
		     " letting msgr retry");
		/* touch each r_stamp for handle_timeout()'s benfit */
		list_for_each_entry(req, &osd->o_requests, r_osd_item)
			req->r_stamp = jiffies;
		ret = -EAGAIN;
	} else {
		ceph_con_close(&osd->o_con);
		ceph_con_open(&osd->o_con, &osdc->osdmap->osd_addr[osd->o_osd]);
		osd->o_incarnation++;
	}
	return ret;
}

static void __insert_osd(struct ceph_osd_client *osdc, struct ceph_osd *new)
{
	struct rb_node **p = &osdc->osds.rb_node;
	struct rb_node *parent = NULL;
	struct ceph_osd *osd = NULL;

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
			osdc->client->mount_args->osd_keepalive_timeout * HZ);
}

static void __cancel_osd_timeout(struct ceph_osd_client *osdc)
{
	cancel_delayed_work(&osdc->timeout_work);
}

/*
 * Register request, assign tid.  If this is the first request, set up
 * the timeout event.
 */
static void register_request(struct ceph_osd_client *osdc,
			     struct ceph_osd_request *req)
{
	mutex_lock(&osdc->request_mutex);
	req->r_tid = ++osdc->last_tid;
	req->r_request->hdr.tid = cpu_to_le64(req->r_tid);
	INIT_LIST_HEAD(&req->r_req_lru_item);

	dout("register_request %p tid %lld\n", req, req->r_tid);
	__insert_request(osdc, req);
	ceph_osdc_get_request(req);
	osdc->num_requests++;

	if (osdc->num_requests == 1) {
		dout(" first request, scheduling timeout\n");
		__schedule_osd_timeout(osdc);
	}
	mutex_unlock(&osdc->request_mutex);
}

/*
 * called under osdc->request_mutex
 */
static void __unregister_request(struct ceph_osd_client *osdc,
				 struct ceph_osd_request *req)
{
	dout("__unregister_request %p tid %lld\n", req, req->r_tid);
	rb_erase(&req->r_node, &osdc->requests);
	osdc->num_requests--;

	if (req->r_osd) {
		/* make sure the original request isn't in flight. */
		ceph_con_revoke(&req->r_osd->o_con, req->r_request);

		list_del_init(&req->r_osd_item);
		if (list_empty(&req->r_osd->o_requests))
			__move_osd_to_lru(osdc, req->r_osd);
		req->r_osd = NULL;
	}

	ceph_osdc_put_request(req);

	list_del_init(&req->r_req_lru_item);
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
	if (req->r_sent) {
		ceph_con_revoke(&req->r_osd->o_con, req->r_request);
		req->r_sent = 0;
	}
	list_del_init(&req->r_req_lru_item);
}

/*
 * Pick an osd (the first 'up' osd in the pg), allocate the osd struct
 * (as needed), and set the request r_osd appropriately.  If there is
 * no up osd, set r_osd to NULL.
 *
 * Return 0 if unchanged, 1 if changed, or negative on error.
 *
 * Caller should hold map_sem for read and request_mutex.
 */
static int __map_osds(struct ceph_osd_client *osdc,
		      struct ceph_osd_request *req)
{
	struct ceph_osd_request_head *reqhead = req->r_request->front.iov_base;
	struct ceph_pg pgid;
	int acting[CEPH_PG_MAX_SIZE];
	int o = -1, num = 0;
	int err;

	dout("map_osds %p tid %lld\n", req, req->r_tid);
	err = ceph_calc_object_layout(&reqhead->layout, req->r_oid,
				      &req->r_file_layout, osdc->osdmap);
	if (err)
		return err;
	pgid = reqhead->layout.ol_pgid;
	req->r_pgid = pgid;

	err = ceph_calc_pg_acting(osdc->osdmap, pgid, acting);
	if (err > 0) {
		o = acting[0];
		num = err;
	}

	if ((req->r_osd && req->r_osd->o_osd == o &&
	     req->r_sent >= req->r_osd->o_incarnation &&
	     req->r_num_pg_osds == num &&
	     memcmp(req->r_pg_osds, acting, sizeof(acting[0])*num) == 0) ||
	    (req->r_osd == NULL && o == -1))
		return 0;  /* no change */

	dout("map_osds tid %llu pgid %d.%x osd%d (was osd%d)\n",
	     req->r_tid, le32_to_cpu(pgid.pool), le16_to_cpu(pgid.ps), o,
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
		req->r_osd = create_osd(osdc);
		if (!req->r_osd)
			goto out;

		dout("map_osds osd %p is osd%d\n", req->r_osd, o);
		req->r_osd->o_osd = o;
		req->r_osd->o_con.peer_name.num = cpu_to_le64(o);
		__insert_osd(osdc, req->r_osd);

		ceph_con_open(&req->r_osd->o_con, &osdc->osdmap->osd_addr[o]);
	}

	if (req->r_osd) {
		__remove_osd_from_lru(req->r_osd);
		list_add(&req->r_osd_item, &req->r_osd->o_requests);
	}
	err = 1;   /* osd or pg changed */

out:
	return err;
}

/*
 * caller should hold map_sem (for read) and request_mutex
 */
static int __send_request(struct ceph_osd_client *osdc,
			  struct ceph_osd_request *req)
{
	struct ceph_osd_request_head *reqhead;
	int err;

	err = __map_osds(osdc, req);
	if (err < 0)
		return err;
	if (req->r_osd == NULL) {
		dout("send_request %p no up osds in pg\n", req);
		ceph_monc_request_next_osdmap(&osdc->client->monc);
		return 0;
	}

	dout("send_request %p tid %llu to osd%d flags %d\n",
	     req, req->r_tid, req->r_osd->o_osd, req->r_flags);

	reqhead = req->r_request->front.iov_base;
	reqhead->osdmap_epoch = cpu_to_le32(osdc->osdmap->epoch);
	reqhead->flags |= cpu_to_le32(req->r_flags);  /* e.g., RETRY */
	reqhead->reassert_version = req->r_reassert_version;

	req->r_stamp = jiffies;
	list_move_tail(&osdc->req_lru, &req->r_req_lru_item);

	ceph_msg_get(req->r_request); /* send consumes a ref */
	ceph_con_send(&req->r_osd->o_con, req->r_request);
	req->r_sent = req->r_osd->o_incarnation;
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
	struct ceph_osd_request *req, *last_req = NULL;
	struct ceph_osd *osd;
	unsigned long timeout = osdc->client->mount_args->osd_timeout * HZ;
	unsigned long keepalive =
		osdc->client->mount_args->osd_keepalive_timeout * HZ;
	unsigned long last_stamp = 0;
	struct rb_node *p;
	struct list_head slow_osds;

	dout("timeout\n");
	down_read(&osdc->map_sem);

	ceph_monc_request_next_osdmap(&osdc->client->monc);

	mutex_lock(&osdc->request_mutex);
	for (p = rb_first(&osdc->requests); p; p = rb_next(p)) {
		req = rb_entry(p, struct ceph_osd_request, r_node);

		if (req->r_resend) {
			int err;

			dout("osdc resending prev failed %lld\n", req->r_tid);
			err = __send_request(osdc, req);
			if (err)
				dout("osdc failed again on %lld\n", req->r_tid);
			else
				req->r_resend = false;
			continue;
		}
	}

	/*
	 * reset osds that appear to be _really_ unresponsive.  this
	 * is a failsafe measure.. we really shouldn't be getting to
	 * this point if the system is working properly.  the monitors
	 * should mark the osd as failed and we should find out about
	 * it from an updated osd map.
	 */
	while (timeout && !list_empty(&osdc->req_lru)) {
		req = list_entry(osdc->req_lru.next, struct ceph_osd_request,
				 r_req_lru_item);

		if (time_before(jiffies, req->r_stamp + timeout))
			break;

		BUG_ON(req == last_req && req->r_stamp == last_stamp);
		last_req = req;
		last_stamp = req->r_stamp;

		osd = req->r_osd;
		BUG_ON(!osd);
		pr_warning(" tid %llu timed out on osd%d, will reset osd\n",
			   req->r_tid, osd->o_osd);
		__kick_requests(osdc, osd);
	}

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
	mutex_unlock(&osdc->request_mutex);

	up_read(&osdc->map_sem);
}

static void handle_osds_timeout(struct work_struct *work)
{
	struct ceph_osd_client *osdc =
		container_of(work, struct ceph_osd_client,
			     osds_timeout_work.work);
	unsigned long delay =
		osdc->client->mount_args->osd_idle_ttl * HZ >> 2;

	dout("osds timeout\n");
	down_read(&osdc->map_sem);
	remove_old_osds(osdc, 0);
	up_read(&osdc->map_sem);

	schedule_delayed_work(&osdc->osds_timeout_work,
			      round_jiffies_relative(delay));
}

/*
 * handle osd op reply.  either call the callback if it is specified,
 * or do the completion to wake up the waiting thread.
 */
static void handle_reply(struct ceph_osd_client *osdc, struct ceph_msg *msg,
			 struct ceph_connection *con)
{
	struct ceph_osd_reply_head *rhead = msg->front.iov_base;
	struct ceph_osd_request *req;
	u64 tid;
	int numops, object_len, flags;
	s32 result;

	tid = le64_to_cpu(msg->hdr.tid);
	if (msg->front.iov_len < sizeof(*rhead))
		goto bad;
	numops = le32_to_cpu(rhead->num_ops);
	object_len = le32_to_cpu(rhead->object_len);
	result = le32_to_cpu(rhead->result);
	if (msg->front.iov_len != sizeof(*rhead) + object_len +
	    numops * sizeof(struct ceph_osd_op))
		goto bad;
	dout("handle_reply %p tid %llu result %d\n", msg, tid, (int)result);

	/* lookup */
	mutex_lock(&osdc->request_mutex);
	req = __lookup_request(osdc, tid);
	if (req == NULL) {
		dout("handle_reply tid %llu dne\n", tid);
		mutex_unlock(&osdc->request_mutex);
		return;
	}
	ceph_osdc_get_request(req);
	flags = le32_to_cpu(rhead->flags);

	/*
	 * if this connection filled our message, drop our reference now, to
	 * avoid a (safe but slower) revoke later.
	 */
	if (req->r_con_filling_msg == con && req->r_reply == msg) {
		dout(" dropping con_filling_msg ref %p\n", con);
		req->r_con_filling_msg = NULL;
		ceph_con_put(con);
	}

	if (!req->r_got_reply) {
		unsigned bytes;

		req->r_result = le32_to_cpu(rhead->result);
		bytes = le32_to_cpu(msg->hdr.data_len);
		dout("handle_reply result %d bytes %d\n", req->r_result,
		     bytes);
		if (req->r_result == 0)
			req->r_result = bytes;

		/* in case this is a write and we need to replay, */
		req->r_reassert_version = rhead->reassert_version;

		req->r_got_reply = 1;
	} else if ((flags & CEPH_OSD_FLAG_ONDISK) == 0) {
		dout("handle_reply tid %llu dup ack\n", tid);
		mutex_unlock(&osdc->request_mutex);
		goto done;
	}

	dout("handle_reply tid %llu flags %d\n", tid, flags);

	/* either this is a read, or we got the safe response */
	if (result < 0 ||
	    (flags & CEPH_OSD_FLAG_ONDISK) ||
	    ((flags & CEPH_OSD_FLAG_WRITE) == 0))
		__unregister_request(osdc, req);

	mutex_unlock(&osdc->request_mutex);

	if (req->r_callback)
		req->r_callback(req, msg);
	else
		complete_all(&req->r_completion);

	if (flags & CEPH_OSD_FLAG_ONDISK) {
		if (req->r_safe_callback)
			req->r_safe_callback(req, msg);
		complete_all(&req->r_safe_completion);  /* fsync waiter */
	}

done:
	ceph_osdc_put_request(req);
	return;

bad:
	pr_err("corrupt osd_op_reply got %d %d expected %d\n",
	       (int)msg->front.iov_len, le32_to_cpu(msg->hdr.front_len),
	       (int)sizeof(*rhead));
	ceph_msg_dump(msg);
}


static int __kick_requests(struct ceph_osd_client *osdc,
			  struct ceph_osd *kickosd)
{
	struct ceph_osd_request *req;
	struct rb_node *p, *n;
	int needmap = 0;
	int err;

	dout("kick_requests osd%d\n", kickosd ? kickosd->o_osd : -1);
	if (kickosd) {
		err = __reset_osd(osdc, kickosd);
		if (err == -EAGAIN)
			return 1;
	} else {
		for (p = rb_first(&osdc->osds); p; p = n) {
			struct ceph_osd *osd =
				rb_entry(p, struct ceph_osd, o_node);

			n = rb_next(p);
			if (!ceph_osd_is_up(osdc->osdmap, osd->o_osd) ||
			    memcmp(&osd->o_con.peer_addr,
				   ceph_osd_addr(osdc->osdmap,
						 osd->o_osd),
				   sizeof(struct ceph_entity_addr)) != 0)
				__reset_osd(osdc, osd);
		}
	}

	for (p = rb_first(&osdc->requests); p; p = rb_next(p)) {
		req = rb_entry(p, struct ceph_osd_request, r_node);

		if (req->r_resend) {
			dout(" r_resend set on tid %llu\n", req->r_tid);
			__cancel_request(req);
			goto kick;
		}
		if (req->r_osd && kickosd == req->r_osd) {
			__cancel_request(req);
			goto kick;
		}

		err = __map_osds(osdc, req);
		if (err == 0)
			continue;  /* no change */
		if (err < 0) {
			/*
			 * FIXME: really, we should set the request
			 * error and fail if this isn't a 'nofail'
			 * request, but that's a fair bit more
			 * complicated to do.  So retry!
			 */
			dout(" setting r_resend on %llu\n", req->r_tid);
			req->r_resend = true;
			continue;
		}
		if (req->r_osd == NULL) {
			dout("tid %llu maps to no valid osd\n", req->r_tid);
			needmap++;  /* request a newer map */
			continue;
		}

kick:
		dout("kicking %p tid %llu osd%d\n", req, req->r_tid,
		     req->r_osd ? req->r_osd->o_osd : -1);
		req->r_flags |= CEPH_OSD_FLAG_RETRY;
		err = __send_request(osdc, req);
		if (err) {
			dout(" setting r_resend on %llu\n", req->r_tid);
			req->r_resend = true;
		}
	}

	return needmap;
}

/*
 * Resubmit osd requests whose osd or osd address has changed.  Request
 * a new osd map if osds are down, or we are otherwise unable to determine
 * how to direct a request.
 *
 * Close connections to down osds.
 *
 * If @who is specified, resubmit requests for that specific osd.
 *
 * Caller should hold map_sem for read and request_mutex.
 */
static void kick_requests(struct ceph_osd_client *osdc,
			  struct ceph_osd *kickosd)
{
	int needmap;

	mutex_lock(&osdc->request_mutex);
	needmap = __kick_requests(osdc, kickosd);
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

	dout("handle_map have %u\n", osdc->osdmap ? osdc->osdmap->epoch : 0);
	p = msg->front.iov_base;
	end = p + msg->front.iov_len;

	/* verify fsid */
	ceph_decode_need(&p, end, sizeof(fsid), bad);
	ceph_decode_copy(&p, &fsid, sizeof(fsid));
	if (ceph_check_fsid(osdc->client, &fsid) < 0)
		return;

	down_write(&osdc->map_sem);

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
							  osdc->client->msgr);
			if (IS_ERR(newmap)) {
				err = PTR_ERR(newmap);
				goto bad;
			}
			BUG_ON(!newmap);
			if (newmap != osdc->osdmap) {
				ceph_osdmap_destroy(osdc->osdmap);
				osdc->osdmap = newmap;
			}
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
			dout("taking full map %u len %d\n", epoch, maplen);
			newmap = osdmap_decode(&p, p+maplen);
			if (IS_ERR(newmap)) {
				err = PTR_ERR(newmap);
				goto bad;
			}
			BUG_ON(!newmap);
			oldmap = osdc->osdmap;
			osdc->osdmap = newmap;
			if (oldmap)
				ceph_osdmap_destroy(oldmap);
		}
		p += maplen;
		nr_maps--;
	}

done:
	downgrade_write(&osdc->map_sem);
	ceph_monc_got_osdmap(&osdc->client->monc, osdc->osdmap->epoch);
	if (newmap)
		kick_requests(osdc, NULL);
	up_read(&osdc->map_sem);
	wake_up_all(&osdc->client->auth_wq);
	return;

bad:
	pr_err("osdc handle_map corrupt msg\n");
	ceph_msg_dump(msg);
	up_write(&osdc->map_sem);
	return;
}

/*
 * Register request, send initial attempt.
 */
int ceph_osdc_start_request(struct ceph_osd_client *osdc,
			    struct ceph_osd_request *req,
			    bool nofail)
{
	int rc = 0;

	req->r_request->pages = req->r_pages;
	req->r_request->nr_pages = req->r_num_pages;

	register_request(osdc, req);

	down_read(&osdc->map_sem);
	mutex_lock(&osdc->request_mutex);
	/*
	 * a racing kick_requests() may have sent the message for us
	 * while we dropped request_mutex above, so only send now if
	 * the request still han't been touched yet.
	 */
	if (req->r_sent == 0) {
		rc = __send_request(osdc, req);
		if (rc) {
			if (nofail) {
				dout("osdc_start_request failed send, "
				     " marking %lld\n", req->r_tid);
				req->r_resend = true;
				rc = 0;
			} else {
				__unregister_request(osdc, req);
			}
		}
	}
	mutex_unlock(&osdc->request_mutex);
	up_read(&osdc->map_sem);
	return rc;
}

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
		dout("wait_request tid %llu canceled/timed out\n", req->r_tid);
		return rc;
	}

	dout("wait_request tid %llu result %d\n", req->r_tid, req->r_result);
	return req->r_result;
}

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
	osdc->num_requests = 0;
	INIT_DELAYED_WORK(&osdc->timeout_work, handle_timeout);
	INIT_DELAYED_WORK(&osdc->osds_timeout_work, handle_osds_timeout);

	schedule_delayed_work(&osdc->osds_timeout_work,
	   round_jiffies_relative(osdc->client->mount_args->osd_idle_ttl * HZ));

	err = -ENOMEM;
	osdc->req_mempool = mempool_create_kmalloc_pool(10,
					sizeof(struct ceph_osd_request));
	if (!osdc->req_mempool)
		goto out;

	err = ceph_msgpool_init(&osdc->msgpool_op, OSD_OP_FRONT_LEN, 10, true,
				"osd_op");
	if (err < 0)
		goto out_mempool;
	err = ceph_msgpool_init(&osdc->msgpool_op_reply,
				OSD_OPREPLY_FRONT_LEN, 10, true,
				"osd_op_reply");
	if (err < 0)
		goto out_msgpool;
	return 0;

out_msgpool:
	ceph_msgpool_destroy(&osdc->msgpool_op);
out_mempool:
	mempool_destroy(osdc->req_mempool);
out:
	return err;
}

void ceph_osdc_stop(struct ceph_osd_client *osdc)
{
	cancel_delayed_work_sync(&osdc->timeout_work);
	cancel_delayed_work_sync(&osdc->osds_timeout_work);
	if (osdc->osdmap) {
		ceph_osdmap_destroy(osdc->osdmap);
		osdc->osdmap = NULL;
	}
	remove_old_osds(osdc, 1);
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
			struct page **pages, int num_pages)
{
	struct ceph_osd_request *req;
	int rc = 0;

	dout("readpages on ino %llx.%llx on %llu~%llu\n", vino.ino,
	     vino.snap, off, *plen);
	req = ceph_osdc_new_request(osdc, layout, vino, off, plen,
				    CEPH_OSD_OP_READ, CEPH_OSD_FLAG_READ,
				    NULL, 0, truncate_seq, truncate_size, NULL,
				    false, 1);
	if (!req)
		return -ENOMEM;

	/* it may be a short read due to an object boundary */
	req->r_pages = pages;
	num_pages = calc_pages_for(off, *plen);
	req->r_num_pages = num_pages;

	dout("readpages  final extent is %llu~%llu (%d pages)\n",
	     off, *plen, req->r_num_pages);

	rc = ceph_osdc_start_request(osdc, req, false);
	if (!rc)
		rc = ceph_osdc_wait_request(osdc, req);

	ceph_osdc_put_request(req);
	dout("readpages result %d\n", rc);
	return rc;
}

/*
 * do a synchronous write on N pages
 */
int ceph_osdc_writepages(struct ceph_osd_client *osdc, struct ceph_vino vino,
			 struct ceph_file_layout *layout,
			 struct ceph_snap_context *snapc,
			 u64 off, u64 len,
			 u32 truncate_seq, u64 truncate_size,
			 struct timespec *mtime,
			 struct page **pages, int num_pages,
			 int flags, int do_sync, bool nofail)
{
	struct ceph_osd_request *req;
	int rc = 0;

	BUG_ON(vino.snap != CEPH_NOSNAP);
	req = ceph_osdc_new_request(osdc, layout, vino, off, &len,
				    CEPH_OSD_OP_WRITE,
				    flags | CEPH_OSD_FLAG_ONDISK |
					    CEPH_OSD_FLAG_WRITE,
				    snapc, do_sync,
				    truncate_seq, truncate_size, mtime,
				    nofail, 1);
	if (!req)
		return -ENOMEM;

	/* it may be a short write due to an object boundary */
	req->r_pages = pages;
	req->r_num_pages = calc_pages_for(off, len);
	dout("writepages %llu~%llu (%d pages)\n", off, len,
	     req->r_num_pages);

	rc = ceph_osdc_start_request(osdc, req, nofail);
	if (!rc)
		rc = ceph_osdc_wait_request(osdc, req);

	ceph_osdc_put_request(req);
	if (rc == 0)
		rc = len;
	dout("writepages result %d\n", rc);
	return rc;
}

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
	int front = le32_to_cpu(hdr->front_len);
	int data_len = le32_to_cpu(hdr->data_len);
	u64 tid;

	tid = le64_to_cpu(hdr->tid);
	mutex_lock(&osdc->request_mutex);
	req = __lookup_request(osdc, tid);
	if (!req) {
		*skip = 1;
		m = NULL;
		pr_info("get_reply unknown tid %llu from osd%d\n", tid,
			osd->o_osd);
		goto out;
	}

	if (req->r_con_filling_msg) {
		dout("get_reply revoking msg %p from old con %p\n",
		     req->r_reply, req->r_con_filling_msg);
		ceph_con_revoke_message(req->r_con_filling_msg, req->r_reply);
		ceph_con_put(req->r_con_filling_msg);
		req->r_con_filling_msg = NULL;
	}

	if (front > req->r_reply->front.iov_len) {
		pr_warning("get_reply front %d > preallocated %d\n",
			   front, (int)req->r_reply->front.iov_len);
		m = ceph_msg_new(CEPH_MSG_OSD_OPREPLY, front, GFP_NOFS);
		if (!m)
			goto out;
		ceph_msg_put(req->r_reply);
		req->r_reply = m;
	}
	m = ceph_msg_get(req->r_reply);

	if (data_len > 0) {
		unsigned data_off = le16_to_cpu(hdr->data_off);
		int want = calc_pages_for(data_off & ~PAGE_MASK, data_len);

		if (unlikely(req->r_num_pages < want)) {
			pr_warning("tid %lld reply %d > expected %d pages\n",
				   tid, want, m->nr_pages);
			*skip = 1;
			ceph_msg_put(m);
			m = NULL;
			goto out;
		}
		m->pages = req->r_pages;
		m->nr_pages = req->r_num_pages;
	}
	*skip = 0;
	req->r_con_filling_msg = ceph_con_get(con);
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

	switch (type) {
	case CEPH_MSG_OSD_MAP:
		return ceph_msg_new(type, front, GFP_NOFS);
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
static int get_authorizer(struct ceph_connection *con,
	                  void **buf, int *len, int *proto,
	                  void **reply_buf, int *reply_len, int force_new)
{
	struct ceph_osd *o = con->private;
	struct ceph_osd_client *osdc = o->o_osdc;
	struct ceph_auth_client *ac = osdc->client->monc.auth;
	int ret = 0;

	if (force_new && o->o_authorizer) {
		ac->ops->destroy_authorizer(ac, o->o_authorizer);
		o->o_authorizer = NULL;
	}
	if (o->o_authorizer == NULL) {
		ret = ac->ops->create_authorizer(
			ac, CEPH_ENTITY_TYPE_OSD,
			&o->o_authorizer,
			&o->o_authorizer_buf,
			&o->o_authorizer_buf_len,
			&o->o_authorizer_reply_buf,
			&o->o_authorizer_reply_buf_len);
		if (ret)
		return ret;
	}

	*proto = ac->protocol;
	*buf = o->o_authorizer_buf;
	*len = o->o_authorizer_buf_len;
	*reply_buf = o->o_authorizer_reply_buf;
	*reply_len = o->o_authorizer_reply_buf_len;
	return 0;
}


static int verify_authorizer_reply(struct ceph_connection *con, int len)
{
	struct ceph_osd *o = con->private;
	struct ceph_osd_client *osdc = o->o_osdc;
	struct ceph_auth_client *ac = osdc->client->monc.auth;

	return ac->ops->verify_authorizer_reply(ac, o->o_authorizer, len);
}

static int invalidate_authorizer(struct ceph_connection *con)
{
	struct ceph_osd *o = con->private;
	struct ceph_osd_client *osdc = o->o_osdc;
	struct ceph_auth_client *ac = osdc->client->monc.auth;

	if (ac->ops->invalidate_authorizer)
		ac->ops->invalidate_authorizer(ac, CEPH_ENTITY_TYPE_OSD);

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

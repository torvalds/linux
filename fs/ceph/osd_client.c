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

const static struct ceph_connection_operations osd_con_ops;

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
void ceph_osdc_put_request(struct ceph_osd_request *req)
{
	dout("osdc put_request %p %d -> %d\n", req, atomic_read(&req->r_ref),
	     atomic_read(&req->r_ref)-1);
	BUG_ON(atomic_read(&req->r_ref) <= 0);
	if (atomic_dec_and_test(&req->r_ref)) {
		if (req->r_request)
			ceph_msg_put(req->r_request);
		if (req->r_reply)
			ceph_msg_put(req->r_reply);
		if (req->r_own_pages)
			ceph_release_page_vector(req->r_pages,
						 req->r_num_pages);
		ceph_put_snap_context(req->r_snapc);
		if (req->r_mempool)
			mempool_free(req, req->r_osdc->req_mempool);
		else
			kfree(req);
	}
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
	int do_trunc = truncate_seq && (off + *plen > truncate_size);
	int num_op = 1 + do_sync + do_trunc;
	size_t msg_size = sizeof(*head) + num_op*sizeof(*op);
	int err, i;
	u64 prevofs;

	if (use_mempool) {
		req = mempool_alloc(osdc->req_mempool, GFP_NOFS);
		memset(req, 0, sizeof(*req));
	} else {
		req = kzalloc(sizeof(*req), GFP_NOFS);
	}
	if (req == NULL)
		return ERR_PTR(-ENOMEM);

	err = ceph_msgpool_resv(&osdc->msgpool_op_reply, num_reply);
	if (err) {
		ceph_osdc_put_request(req);
		return ERR_PTR(-ENOMEM);
	}

	req->r_osdc = osdc;
	req->r_mempool = use_mempool;
	atomic_set(&req->r_ref, 1);
	init_completion(&req->r_completion);
	init_completion(&req->r_safe_completion);
	INIT_LIST_HEAD(&req->r_unsafe_item);
	req->r_flags = flags;

	WARN_ON((flags & (CEPH_OSD_FLAG_READ|CEPH_OSD_FLAG_WRITE)) == 0);

	/* create message; allow space for oid */
	msg_size += 40;
	if (snapc)
		msg_size += sizeof(u64) * snapc->num_snaps;
	if (use_mempool)
		msg = ceph_msgpool_get(&osdc->msgpool_op, 0);
	else
		msg = ceph_msg_new(CEPH_MSG_OSD_OP, msg_size, 0, 0, NULL);
	if (IS_ERR(msg)) {
		ceph_msgpool_resv(&osdc->msgpool_op_reply, num_reply);
		ceph_osdc_put_request(req);
		return ERR_PTR(PTR_ERR(msg));
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

	/* fill in oid */
	head->object_len = cpu_to_le32(req->r_oid_len);
	memcpy(p, req->r_oid, req->r_oid_len);
	p += req->r_oid_len;

	/* additional ops */
	if (do_trunc) {
		op++;
		op->op = cpu_to_le16(opcode == CEPH_OSD_OP_READ ?
			     CEPH_OSD_OP_MASKTRUNC : CEPH_OSD_OP_SETTRUNC);
		op->trunc.truncate_seq = cpu_to_le32(truncate_seq);
		prevofs = le64_to_cpu((op-1)->extent.offset);
		op->trunc.truncate_size = cpu_to_le64(truncate_size -
						      (off-prevofs));
	}
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
	osd->o_incarnation++;
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
	osd->o_incarnation = 1;

	ceph_con_init(osdc->client->msgr, &osd->o_con);
	osd->o_con.private = osd;
	osd->o_con.ops = &osd_con_ops;
	osd->o_con.peer_name.type = CEPH_ENTITY_TYPE_OSD;
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
		ceph_con_shutdown(&osd->o_con);
		kfree(osd);
	}
}

/*
 * remove an osd from our map
 */
static void remove_osd(struct ceph_osd_client *osdc, struct ceph_osd *osd)
{
	dout("remove_osd %p\n", osd);
	BUG_ON(!list_empty(&osd->o_requests));
	rb_erase(&osd->o_node, &osdc->osds);
	ceph_con_close(&osd->o_con);
	put_osd(osd);
}

/*
 * reset osd connect
 */
static int reset_osd(struct ceph_osd_client *osdc, struct ceph_osd *osd)
{
	int ret = 0;

	dout("reset_osd %p osd%d\n", osd, osd->o_osd);
	if (list_empty(&osd->o_requests)) {
		remove_osd(osdc, osd);
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


/*
 * Register request, assign tid.  If this is the first request, set up
 * the timeout event.
 */
static void register_request(struct ceph_osd_client *osdc,
			     struct ceph_osd_request *req)
{
	struct ceph_osd_request_head *head = req->r_request->front.iov_base;

	mutex_lock(&osdc->request_mutex);
	req->r_tid = ++osdc->last_tid;
	head->tid = cpu_to_le64(req->r_tid);

	dout("register_request %p tid %lld\n", req, req->r_tid);
	__insert_request(osdc, req);
	ceph_osdc_get_request(req);
	osdc->num_requests++;

	req->r_timeout_stamp =
		jiffies + osdc->client->mount_args->osd_timeout*HZ;

	if (osdc->num_requests == 1) {
		osdc->timeout_tid = req->r_tid;
		dout("  timeout on tid %llu at %lu\n", req->r_tid,
		     req->r_timeout_stamp);
		schedule_delayed_work(&osdc->timeout_work,
		      round_jiffies_relative(req->r_timeout_stamp - jiffies));
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
			remove_osd(osdc, req->r_osd);
		req->r_osd = NULL;
	}

	ceph_osdc_put_request(req);

	if (req->r_tid == osdc->timeout_tid) {
		if (osdc->num_requests == 0) {
			dout("no requests, canceling timeout\n");
			osdc->timeout_tid = 0;
			cancel_delayed_work(&osdc->timeout_work);
		} else {
			req = rb_entry(rb_first(&osdc->requests),
				       struct ceph_osd_request, r_node);
			osdc->timeout_tid = req->r_tid;
			dout("rescheduled timeout on tid %llu at %lu\n",
			     req->r_tid, req->r_timeout_stamp);
			schedule_delayed_work(&osdc->timeout_work,
			      round_jiffies_relative(req->r_timeout_stamp -
						     jiffies));
		}
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
	int o = -1;
	int err;
	struct ceph_osd *newosd = NULL;

	dout("map_osds %p tid %lld\n", req, req->r_tid);
	err = ceph_calc_object_layout(&reqhead->layout, req->r_oid,
				      &req->r_file_layout, osdc->osdmap);
	if (err)
		return err;
	pgid = reqhead->layout.ol_pgid;
	o = ceph_calc_pg_primary(osdc->osdmap, pgid);

	if ((req->r_osd && req->r_osd->o_osd == o &&
	     req->r_sent >= req->r_osd->o_incarnation) ||
	    (req->r_osd == NULL && o == -1))
		return 0;  /* no change */

	dout("map_osds tid %llu pgid %d.%x osd%d (was osd%d)\n",
	     req->r_tid, le32_to_cpu(pgid.pool), le16_to_cpu(pgid.ps), o,
	     req->r_osd ? req->r_osd->o_osd : -1);

	if (req->r_osd) {
		__cancel_request(req);
		list_del_init(&req->r_osd_item);
		if (list_empty(&req->r_osd->o_requests)) {
			/* try to re-use r_osd if possible */
			newosd = get_osd(req->r_osd);
			remove_osd(osdc, newosd);
		}
		req->r_osd = NULL;
	}

	req->r_osd = __lookup_osd(osdc, o);
	if (!req->r_osd && o >= 0) {
		if (newosd) {
			req->r_osd = newosd;
			newosd = NULL;
		} else {
			err = -ENOMEM;
			req->r_osd = create_osd(osdc);
			if (!req->r_osd)
				goto out;
		}

		dout("map_osds osd %p is osd%d\n", req->r_osd, o);
		req->r_osd->o_osd = o;
		req->r_osd->o_con.peer_name.num = cpu_to_le64(o);
		__insert_osd(osdc, req->r_osd);

		ceph_con_open(&req->r_osd->o_con, &osdc->osdmap->osd_addr[o]);
	}

	if (req->r_osd)
		list_add(&req->r_osd_item, &req->r_osd->o_requests);
	err = 1;   /* osd changed */

out:
	if (newosd)
		put_osd(newosd);
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

	req->r_timeout_stamp = jiffies+osdc->client->mount_args->osd_timeout*HZ;

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
	struct ceph_osd_request *req;
	struct ceph_osd *osd;
	unsigned long timeout = osdc->client->mount_args->osd_timeout * HZ;
	unsigned long next_timeout = timeout + jiffies;
	struct rb_node *p;

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
	for (p = rb_first(&osdc->osds); p; p = rb_next(p)) {
		osd = rb_entry(p, struct ceph_osd, o_node);
		if (list_empty(&osd->o_requests))
			continue;
		req = list_first_entry(&osd->o_requests,
				       struct ceph_osd_request, r_osd_item);
		if (time_before(jiffies, req->r_timeout_stamp))
			continue;

		dout(" tid %llu (at least) timed out on osd%d\n",
		     req->r_tid, osd->o_osd);
		req->r_timeout_stamp = next_timeout;
		ceph_con_keepalive(&osd->o_con);
	}

	if (osdc->timeout_tid)
		schedule_delayed_work(&osdc->timeout_work,
				      round_jiffies_relative(timeout));

	mutex_unlock(&osdc->request_mutex);

	up_read(&osdc->map_sem);
}

/*
 * handle osd op reply.  either call the callback if it is specified,
 * or do the completion to wake up the waiting thread.
 */
static void handle_reply(struct ceph_osd_client *osdc, struct ceph_msg *msg)
{
	struct ceph_osd_reply_head *rhead = msg->front.iov_base;
	struct ceph_osd_request *req;
	u64 tid;
	int numops, object_len, flags;

	if (msg->front.iov_len < sizeof(*rhead))
		goto bad;
	tid = le64_to_cpu(rhead->tid);
	numops = le32_to_cpu(rhead->num_ops);
	object_len = le32_to_cpu(rhead->object_len);
	if (msg->front.iov_len != sizeof(*rhead) + object_len +
	    numops * sizeof(struct ceph_osd_op))
		goto bad;
	dout("handle_reply %p tid %llu\n", msg, tid);

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

	if (req->r_reply) {
		/*
		 * once we see the message has been received, we don't
		 * need a ref (which is only needed for revoking
		 * pages)
		 */
		ceph_msg_put(req->r_reply);
		req->r_reply = NULL;
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
		goto done;
	}

	dout("handle_reply tid %llu flags %d\n", tid, flags);

	/* either this is a read, or we got the safe response */
	if ((flags & CEPH_OSD_FLAG_ONDISK) ||
	    ((flags & CEPH_OSD_FLAG_WRITE) == 0))
		__unregister_request(osdc, req);

	mutex_unlock(&osdc->request_mutex);

	if (req->r_callback)
		req->r_callback(req, msg);
	else
		complete(&req->r_completion);

	if (flags & CEPH_OSD_FLAG_ONDISK) {
		if (req->r_safe_callback)
			req->r_safe_callback(req, msg);
		complete(&req->r_safe_completion);  /* fsync waiter */
	}

done:
	ceph_osdc_put_request(req);
	return;

bad:
	pr_err("corrupt osd_op_reply got %d %d expected %d\n",
	       (int)msg->front.iov_len, le32_to_cpu(msg->hdr.front_len),
	       (int)sizeof(*rhead));
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
	struct ceph_osd_request *req;
	struct rb_node *p, *n;
	int needmap = 0;
	int err;

	dout("kick_requests osd%d\n", kickosd ? kickosd->o_osd : -1);
	mutex_lock(&osdc->request_mutex);
	if (!kickosd) {
		for (p = rb_first(&osdc->osds); p; p = n) {
			struct ceph_osd *osd =
				rb_entry(p, struct ceph_osd, o_node);

			n = rb_next(p);
			if (!ceph_osd_is_up(osdc->osdmap, osd->o_osd) ||
			    !ceph_entity_addr_equal(&osd->o_con.peer_addr,
					    ceph_osd_addr(osdc->osdmap,
							  osd->o_osd)))
				reset_osd(osdc, osd);
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
		     req->r_osd->o_osd);
		req->r_flags |= CEPH_OSD_FLAG_RETRY;
		err = __send_request(osdc, req);
		if (err) {
			dout(" setting r_resend on %llu\n", req->r_tid);
			req->r_resend = true;
		}
	}
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
	if (ceph_fsid_compare(&fsid, &osdc->client->monc.monmap->fsid)) {
		pr_err("got osdmap with wrong fsid, ignoring\n");
		return;
	}

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
	return;

bad:
	pr_err("osdc handle_map corrupt msg\n");
	up_write(&osdc->map_sem);
	return;
}


/*
 * A read request prepares specific pages that data is to be read into.
 * When a message is being read off the wire, we call prepare_pages to
 * find those pages.
 *  0 = success, -1 failure.
 */
static int prepare_pages(struct ceph_connection *con, struct ceph_msg *m,
			 int want)
{
	struct ceph_osd *osd = con->private;
	struct ceph_osd_client *osdc;
	struct ceph_osd_reply_head *rhead = m->front.iov_base;
	struct ceph_osd_request *req;
	u64 tid;
	int ret = -1;
	int type = le16_to_cpu(m->hdr.type);

	if (!osd)
		return -1;
	osdc = osd->o_osdc;

	dout("prepare_pages on msg %p want %d\n", m, want);
	if (unlikely(type != CEPH_MSG_OSD_OPREPLY))
		return -1;  /* hmm! */

	tid = le64_to_cpu(rhead->tid);
	mutex_lock(&osdc->request_mutex);
	req = __lookup_request(osdc, tid);
	if (!req) {
		dout("prepare_pages unknown tid %llu\n", tid);
		goto out;
	}
	dout("prepare_pages tid %llu has %d pages, want %d\n",
	     tid, req->r_num_pages, want);
	if (likely(req->r_num_pages >= want && !req->r_prepared_pages)) {
		m->pages = req->r_pages;
		m->nr_pages = req->r_num_pages;
		req->r_reply = m;  /* only for duration of read over socket */
		ceph_msg_get(m);
		req->r_prepared_pages = 1;
		ret = 0; /* success */
	}
out:
	mutex_unlock(&osdc->request_mutex);
	return ret;
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
		mutex_unlock(&osdc->request_mutex);
		dout("wait_request tid %llu timed out\n", req->r_tid);
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
	osdc->timeout_tid = 0;
	osdc->last_tid = 0;
	osdc->osds = RB_ROOT;
	osdc->requests = RB_ROOT;
	osdc->num_requests = 0;
	INIT_DELAYED_WORK(&osdc->timeout_work, handle_timeout);

	osdc->req_mempool = mempool_create_kmalloc_pool(10,
					sizeof(struct ceph_osd_request));
	if (!osdc->req_mempool)
		return -ENOMEM;

	err = ceph_msgpool_init(&osdc->msgpool_op, 4096, 10, true);
	if (err < 0)
		return -ENOMEM;
	err = ceph_msgpool_init(&osdc->msgpool_op_reply, 512, 0, false);
	if (err < 0)
		return -ENOMEM;

	return 0;
}

void ceph_osdc_stop(struct ceph_osd_client *osdc)
{
	cancel_delayed_work_sync(&osdc->timeout_work);
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
	if (IS_ERR(req))
		return PTR_ERR(req);

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
	if (IS_ERR(req))
		return PTR_ERR(req);

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
	struct ceph_osd_client *osdc = osd->o_osdc;
	int type = le16_to_cpu(msg->hdr.type);

	if (!osd)
		return;

	switch (type) {
	case CEPH_MSG_OSD_MAP:
		ceph_osdc_handle_map(osdc, msg);
		break;
	case CEPH_MSG_OSD_OPREPLY:
		handle_reply(osdc, msg);
		break;

	default:
		pr_err("received unknown message type %d %s\n", type,
		       ceph_msg_type_name(type));
	}
	ceph_msg_put(msg);
}

static struct ceph_msg *alloc_msg(struct ceph_connection *con,
				  struct ceph_msg_header *hdr)
{
	struct ceph_osd *osd = con->private;
	struct ceph_osd_client *osdc = osd->o_osdc;
	int type = le16_to_cpu(hdr->type);
	int front = le32_to_cpu(hdr->front_len);

	switch (type) {
	case CEPH_MSG_OSD_OPREPLY:
		return ceph_msgpool_get(&osdc->msgpool_op_reply, front);
	}
	return ceph_alloc_msg(con, hdr);
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

const static struct ceph_connection_operations osd_con_ops = {
	.get = get_osd_con,
	.put = put_osd_con,
	.dispatch = dispatch,
	.alloc_msg = alloc_msg,
	.fault = osd_reset,
	.alloc_middle = ceph_alloc_middle,
	.prepare_pages = prepare_pages,
};

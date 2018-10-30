// SPDX-License-Identifier: GPL-2.0
#include <linux/ceph/ceph_debug.h>

#include <linux/err.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#include <linux/ceph/messenger.h>
#include <linux/ceph/msgpool.h>

static void *msgpool_alloc(gfp_t gfp_mask, void *arg)
{
	struct ceph_msgpool *pool = arg;
	struct ceph_msg *msg;

	msg = ceph_msg_new(pool->type, pool->front_len, gfp_mask, true);
	if (!msg) {
		dout("msgpool_alloc %s failed\n", pool->name);
	} else {
		dout("msgpool_alloc %s %p\n", pool->name, msg);
		msg->pool = pool;
	}
	return msg;
}

static void msgpool_free(void *element, void *arg)
{
	struct ceph_msgpool *pool = arg;
	struct ceph_msg *msg = element;

	dout("msgpool_release %s %p\n", pool->name, msg);
	msg->pool = NULL;
	ceph_msg_put(msg);
}

int ceph_msgpool_init(struct ceph_msgpool *pool, int type,
		      int front_len, int size, bool blocking, const char *name)
{
	dout("msgpool %s init\n", name);
	pool->type = type;
	pool->front_len = front_len;
	pool->pool = mempool_create(size, msgpool_alloc, msgpool_free, pool);
	if (!pool->pool)
		return -ENOMEM;
	pool->name = name;
	return 0;
}

void ceph_msgpool_destroy(struct ceph_msgpool *pool)
{
	dout("msgpool %s destroy\n", pool->name);
	mempool_destroy(pool->pool);
}

struct ceph_msg *ceph_msgpool_get(struct ceph_msgpool *pool,
				  int front_len)
{
	struct ceph_msg *msg;

	if (front_len > pool->front_len) {
		dout("msgpool_get %s need front %d, pool size is %d\n",
		       pool->name, front_len, pool->front_len);
		WARN_ON(1);

		/* try to alloc a fresh message */
		return ceph_msg_new(pool->type, front_len, GFP_NOFS, false);
	}

	msg = mempool_alloc(pool->pool, GFP_NOFS);
	dout("msgpool_get %s %p\n", pool->name, msg);
	return msg;
}

void ceph_msgpool_put(struct ceph_msgpool *pool, struct ceph_msg *msg)
{
	dout("msgpool_put %s %p\n", pool->name, msg);

	/* reset msg front_len; user may have changed it */
	msg->front.iov_len = pool->front_len;
	msg->hdr.front_len = cpu_to_le32(pool->front_len);

	kref_init(&msg->kref);  /* retake single ref */
	mempool_free(msg, pool->pool);
}

#include "ceph_debug.h"

#include <linux/err.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#include "msgpool.h"

static void *alloc_fn(gfp_t gfp_mask, void *arg)
{
	struct ceph_msgpool *pool = arg;

	return ceph_msg_new(0, pool->front_len, 0, 0, NULL);
}

static void free_fn(void *element, void *arg)
{
	ceph_msg_put(element);
}

int ceph_msgpool_init(struct ceph_msgpool *pool,
		      int front_len, int size, bool blocking)
{
	pool->front_len = front_len;
	pool->pool = mempool_create(size, alloc_fn, free_fn, pool);
	if (!pool->pool)
		return -ENOMEM;
	return 0;
}

void ceph_msgpool_destroy(struct ceph_msgpool *pool)
{
	mempool_destroy(pool->pool);
}

struct ceph_msg *ceph_msgpool_get(struct ceph_msgpool *pool,
				  int front_len)
{
	if (front_len > pool->front_len) {
		pr_err("msgpool_get pool %p need front %d, pool size is %d\n",
		       pool, front_len, pool->front_len);
		WARN_ON(1);

		/* try to alloc a fresh message */
		return ceph_msg_new(0, front_len, 0, 0, NULL);
	}

	return mempool_alloc(pool->pool, GFP_NOFS);
}

void ceph_msgpool_put(struct ceph_msgpool *pool, struct ceph_msg *msg)
{
	/* reset msg front_len; user may have changed it */
	msg->front.iov_len = pool->front_len;
	msg->hdr.front_len = cpu_to_le32(pool->front_len);

	kref_init(&msg->kref);  /* retake single ref */
}

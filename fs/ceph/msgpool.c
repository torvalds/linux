#include "ceph_debug.h"

#include <linux/err.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#include "msgpool.h"

/*
 * We use msg pools to preallocate memory for messages we expect to
 * receive over the wire, to avoid getting ourselves into OOM
 * conditions at unexpected times.  We take use a few different
 * strategies:
 *
 *  - for request/response type interactions, we preallocate the
 * memory needed for the response when we generate the request.
 *
 *  - for messages we can receive at any time from the MDS, we preallocate
 * a pool of messages we can re-use.
 *
 *  - for writeback, we preallocate some number of messages to use for
 * requests and their replies, so that we always make forward
 * progress.
 *
 * The msgpool behaves like a mempool_t, but keeps preallocated
 * ceph_msgs strung together on a list_head instead of using a pointer
 * vector.  This avoids vector reallocation when we adjust the number
 * of preallocated items (which happens frequently).
 */


/*
 * Allocate or release as necessary to meet our target pool size.
 */
static int __fill_msgpool(struct ceph_msgpool *pool)
{
	struct ceph_msg *msg;

	while (pool->num < pool->min) {
		dout("fill_msgpool %p %d/%d allocating\n", pool, pool->num,
		     pool->min);
		spin_unlock(&pool->lock);
		msg = ceph_msg_new(0, pool->front_len, 0, 0, NULL);
		spin_lock(&pool->lock);
		if (IS_ERR(msg))
			return PTR_ERR(msg);
		msg->pool = pool;
		list_add(&msg->list_head, &pool->msgs);
		pool->num++;
	}
	while (pool->num > pool->min) {
		msg = list_first_entry(&pool->msgs, struct ceph_msg, list_head);
		dout("fill_msgpool %p %d/%d releasing %p\n", pool, pool->num,
		     pool->min, msg);
		list_del_init(&msg->list_head);
		pool->num--;
		ceph_msg_kfree(msg);
	}
	return 0;
}

int ceph_msgpool_init(struct ceph_msgpool *pool,
		      int front_len, int min, bool blocking)
{
	int ret;

	dout("msgpool_init %p front_len %d min %d\n", pool, front_len, min);
	spin_lock_init(&pool->lock);
	pool->front_len = front_len;
	INIT_LIST_HEAD(&pool->msgs);
	pool->num = 0;
	pool->min = min;
	pool->blocking = blocking;
	init_waitqueue_head(&pool->wait);

	spin_lock(&pool->lock);
	ret = __fill_msgpool(pool);
	spin_unlock(&pool->lock);
	return ret;
}

void ceph_msgpool_destroy(struct ceph_msgpool *pool)
{
	dout("msgpool_destroy %p\n", pool);
	spin_lock(&pool->lock);
	pool->min = 0;
	__fill_msgpool(pool);
	spin_unlock(&pool->lock);
}

int ceph_msgpool_resv(struct ceph_msgpool *pool, int delta)
{
	int ret;

	spin_lock(&pool->lock);
	dout("msgpool_resv %p delta %d\n", pool, delta);
	pool->min += delta;
	ret = __fill_msgpool(pool);
	spin_unlock(&pool->lock);
	return ret;
}

struct ceph_msg *ceph_msgpool_get(struct ceph_msgpool *pool, int front_len)
{
	wait_queue_t wait;
	struct ceph_msg *msg;

	if (front_len && front_len > pool->front_len) {
		pr_err("msgpool_get pool %p need front %d, pool size is %d\n",
		       pool, front_len, pool->front_len);
		WARN_ON(1);

		/* try to alloc a fresh message */
		msg = ceph_msg_new(0, front_len, 0, 0, NULL);
		if (!IS_ERR(msg))
			return msg;
	}

	if (!front_len)
		front_len = pool->front_len;

	if (pool->blocking) {
		/* mempool_t behavior; first try to alloc */
		msg = ceph_msg_new(0, front_len, 0, 0, NULL);
		if (!IS_ERR(msg))
			return msg;
	}

	while (1) {
		spin_lock(&pool->lock);
		if (likely(pool->num)) {
			msg = list_entry(pool->msgs.next, struct ceph_msg,
					 list_head);
			list_del_init(&msg->list_head);
			pool->num--;
			dout("msgpool_get %p got %p, now %d/%d\n", pool, msg,
			     pool->num, pool->min);
			spin_unlock(&pool->lock);
			return msg;
		}
		pr_err("msgpool_get %p now %d/%d, %s\n", pool, pool->num,
		       pool->min, pool->blocking ? "waiting" : "may fail");
		spin_unlock(&pool->lock);

		if (!pool->blocking) {
			WARN_ON(1);

			/* maybe we can allocate it now? */
			msg = ceph_msg_new(0, front_len, 0, 0, NULL);
			if (!IS_ERR(msg))
				return msg;

			pr_err("msgpool_get %p empty + alloc failed\n", pool);
			return ERR_PTR(-ENOMEM);
		}

		init_wait(&wait);
		prepare_to_wait(&pool->wait, &wait, TASK_UNINTERRUPTIBLE);
		schedule();
		finish_wait(&pool->wait, &wait);
	}
}

void ceph_msgpool_put(struct ceph_msgpool *pool, struct ceph_msg *msg)
{
	spin_lock(&pool->lock);
	if (pool->num < pool->min) {
		/* reset msg front_len; user may have changed it */
		msg->front.iov_len = pool->front_len;
		msg->hdr.front_len = cpu_to_le32(pool->front_len);

		kref_set(&msg->kref, 1);  /* retake a single ref */
		list_add(&msg->list_head, &pool->msgs);
		pool->num++;
		dout("msgpool_put %p reclaim %p, now %d/%d\n", pool, msg,
		     pool->num, pool->min);
		spin_unlock(&pool->lock);
		wake_up(&pool->wait);
	} else {
		dout("msgpool_put %p drop %p, at %d/%d\n", pool, msg,
		     pool->num, pool->min);
		spin_unlock(&pool->lock);
		ceph_msg_kfree(msg);
	}
}

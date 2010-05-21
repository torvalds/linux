#ifndef _FS_CEPH_MSGPOOL
#define _FS_CEPH_MSGPOOL

#include "messenger.h"

/*
 * we use memory pools for preallocating messages we may receive, to
 * avoid unexpected OOM conditions.
 */
struct ceph_msgpool {
	spinlock_t lock;
	int front_len;          /* preallocated payload size */
	struct list_head msgs;  /* msgs in the pool; each has 1 ref */
	int num, min;           /* cur, min # msgs in the pool */
	bool blocking;
	wait_queue_head_t wait;
};

extern int ceph_msgpool_init(struct ceph_msgpool *pool,
			     int front_len, int size, bool blocking);
extern void ceph_msgpool_destroy(struct ceph_msgpool *pool);
extern int ceph_msgpool_resv(struct ceph_msgpool *, int delta);
extern struct ceph_msg *ceph_msgpool_get(struct ceph_msgpool *,
					 int front_len);
extern void ceph_msgpool_put(struct ceph_msgpool *, struct ceph_msg *);

#endif

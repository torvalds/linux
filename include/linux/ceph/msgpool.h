/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FS_CEPH_MSGPOOL
#define _FS_CEPH_MSGPOOL

#include <linux/mempool.h>

/*
 * we use memory pools for preallocating messages we may receive, to
 * avoid unexpected OOM conditions.
 */
struct ceph_msgpool {
	const char *name;
	mempool_t *pool;
	int type;               /* preallocated message type */
	int front_len;          /* preallocated payload size */
	int max_data_items;
};

int ceph_msgpool_init(struct ceph_msgpool *pool, int type,
		      int front_len, int max_data_items, int size,
		      const char *name);
extern void ceph_msgpool_destroy(struct ceph_msgpool *pool);
struct ceph_msg *ceph_msgpool_get(struct ceph_msgpool *pool, int front_len,
				  int max_data_items);
extern void ceph_msgpool_put(struct ceph_msgpool *, struct ceph_msg *);

#endif

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FS_CEPH_MDS_METRIC_H
#define _FS_CEPH_MDS_METRIC_H

#include <linux/types.h>
#include <linux/percpu_counter.h>

/* This is the global metrics */
struct ceph_client_metric {
	atomic64_t            total_dentries;
	struct percpu_counter d_lease_hit;
	struct percpu_counter d_lease_mis;
};

extern int ceph_metric_init(struct ceph_client_metric *m);
extern void ceph_metric_destroy(struct ceph_client_metric *m);
#endif /* _FS_CEPH_MDS_METRIC_H */

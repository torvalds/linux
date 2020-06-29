/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FS_CEPH_MDS_METRIC_H
#define _FS_CEPH_MDS_METRIC_H

#include <linux/types.h>
#include <linux/percpu_counter.h>
#include <linux/ktime.h>

/* This is the global metrics */
struct ceph_client_metric {
	atomic64_t            total_dentries;
	struct percpu_counter d_lease_hit;
	struct percpu_counter d_lease_mis;

	struct percpu_counter i_caps_hit;
	struct percpu_counter i_caps_mis;

	spinlock_t read_latency_lock;
	u64 total_reads;
	ktime_t read_latency_sum;
	ktime_t read_latency_sq_sum;
	ktime_t read_latency_min;
	ktime_t read_latency_max;

	spinlock_t write_latency_lock;
	u64 total_writes;
	ktime_t write_latency_sum;
	ktime_t write_latency_sq_sum;
	ktime_t write_latency_min;
	ktime_t write_latency_max;

	spinlock_t metadata_latency_lock;
	u64 total_metadatas;
	ktime_t metadata_latency_sum;
	ktime_t metadata_latency_sq_sum;
	ktime_t metadata_latency_min;
	ktime_t metadata_latency_max;
};

extern int ceph_metric_init(struct ceph_client_metric *m);
extern void ceph_metric_destroy(struct ceph_client_metric *m);

static inline void ceph_update_cap_hit(struct ceph_client_metric *m)
{
	percpu_counter_inc(&m->i_caps_hit);
}

static inline void ceph_update_cap_mis(struct ceph_client_metric *m)
{
	percpu_counter_inc(&m->i_caps_mis);
}

extern void ceph_update_read_latency(struct ceph_client_metric *m,
				     ktime_t r_start, ktime_t r_end,
				     int rc);
extern void ceph_update_write_latency(struct ceph_client_metric *m,
				      ktime_t r_start, ktime_t r_end,
				      int rc);
extern void ceph_update_metadata_latency(struct ceph_client_metric *m,
				         ktime_t r_start, ktime_t r_end,
					 int rc);
#endif /* _FS_CEPH_MDS_METRIC_H */

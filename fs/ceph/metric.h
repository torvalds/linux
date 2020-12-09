/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FS_CEPH_MDS_METRIC_H
#define _FS_CEPH_MDS_METRIC_H

#include <linux/types.h>
#include <linux/percpu_counter.h>
#include <linux/ktime.h>

extern bool disable_send_metrics;

enum ceph_metric_type {
	CLIENT_METRIC_TYPE_CAP_INFO,
	CLIENT_METRIC_TYPE_READ_LATENCY,
	CLIENT_METRIC_TYPE_WRITE_LATENCY,
	CLIENT_METRIC_TYPE_METADATA_LATENCY,
	CLIENT_METRIC_TYPE_DENTRY_LEASE,

	CLIENT_METRIC_TYPE_MAX = CLIENT_METRIC_TYPE_DENTRY_LEASE,
};

/*
 * This will always have the highest metric bit value
 * as the last element of the array.
 */
#define CEPHFS_METRIC_SPEC_CLIENT_SUPPORTED {	\
	CLIENT_METRIC_TYPE_CAP_INFO,		\
	CLIENT_METRIC_TYPE_READ_LATENCY,	\
	CLIENT_METRIC_TYPE_WRITE_LATENCY,	\
	CLIENT_METRIC_TYPE_METADATA_LATENCY,	\
						\
	CLIENT_METRIC_TYPE_MAX,			\
}

/* metric caps header */
struct ceph_metric_cap {
	__le32 type;     /* ceph metric type */

	__u8  ver;
	__u8  compat;

	__le32 data_len; /* length of sizeof(hit + mis + total) */
	__le64 hit;
	__le64 mis;
	__le64 total;
} __packed;

/* metric read latency header */
struct ceph_metric_read_latency {
	__le32 type;     /* ceph metric type */

	__u8  ver;
	__u8  compat;

	__le32 data_len; /* length of sizeof(sec + nsec) */
	__le32 sec;
	__le32 nsec;
} __packed;

/* metric write latency header */
struct ceph_metric_write_latency {
	__le32 type;     /* ceph metric type */

	__u8  ver;
	__u8  compat;

	__le32 data_len; /* length of sizeof(sec + nsec) */
	__le32 sec;
	__le32 nsec;
} __packed;

/* metric metadata latency header */
struct ceph_metric_metadata_latency {
	__le32 type;     /* ceph metric type */

	__u8  ver;
	__u8  compat;

	__le32 data_len; /* length of sizeof(sec + nsec) */
	__le32 sec;
	__le32 nsec;
} __packed;

struct ceph_metric_head {
	__le32 num;	/* the number of metrics that will be sent */
} __packed;

/* This is the global metrics */
struct ceph_client_metric {
	atomic64_t            total_dentries;
	struct percpu_counter d_lease_hit;
	struct percpu_counter d_lease_mis;

	atomic64_t            total_caps;
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

	/* The total number of directories and files that are opened */
	atomic64_t opened_files;

	/* The total number of inodes that have opened files or directories */
	struct percpu_counter opened_inodes;
	struct percpu_counter total_inodes;

	struct ceph_mds_session *session;
	struct delayed_work delayed_work;  /* delayed work */
};

static inline void metric_schedule_delayed(struct ceph_client_metric *m)
{
	if (disable_send_metrics)
		return;

	/* per second */
	schedule_delayed_work(&m->delayed_work, round_jiffies_relative(HZ));
}

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

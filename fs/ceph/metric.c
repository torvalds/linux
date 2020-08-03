/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/types.h>
#include <linux/percpu_counter.h>
#include <linux/math64.h>

#include "metric.h"

int ceph_metric_init(struct ceph_client_metric *m)
{
	int ret;

	if (!m)
		return -EINVAL;

	atomic64_set(&m->total_dentries, 0);
	ret = percpu_counter_init(&m->d_lease_hit, 0, GFP_KERNEL);
	if (ret)
		return ret;

	ret = percpu_counter_init(&m->d_lease_mis, 0, GFP_KERNEL);
	if (ret)
		goto err_d_lease_mis;

	ret = percpu_counter_init(&m->i_caps_hit, 0, GFP_KERNEL);
	if (ret)
		goto err_i_caps_hit;

	ret = percpu_counter_init(&m->i_caps_mis, 0, GFP_KERNEL);
	if (ret)
		goto err_i_caps_mis;

	spin_lock_init(&m->read_latency_lock);
	m->read_latency_sq_sum = 0;
	m->read_latency_min = KTIME_MAX;
	m->read_latency_max = 0;
	m->total_reads = 0;
	m->read_latency_sum = 0;

	spin_lock_init(&m->write_latency_lock);
	m->write_latency_sq_sum = 0;
	m->write_latency_min = KTIME_MAX;
	m->write_latency_max = 0;
	m->total_writes = 0;
	m->write_latency_sum = 0;

	spin_lock_init(&m->metadata_latency_lock);
	m->metadata_latency_sq_sum = 0;
	m->metadata_latency_min = KTIME_MAX;
	m->metadata_latency_max = 0;
	m->total_metadatas = 0;
	m->metadata_latency_sum = 0;

	return 0;

err_i_caps_mis:
	percpu_counter_destroy(&m->i_caps_hit);
err_i_caps_hit:
	percpu_counter_destroy(&m->d_lease_mis);
err_d_lease_mis:
	percpu_counter_destroy(&m->d_lease_hit);

	return ret;
}

void ceph_metric_destroy(struct ceph_client_metric *m)
{
	if (!m)
		return;

	percpu_counter_destroy(&m->i_caps_mis);
	percpu_counter_destroy(&m->i_caps_hit);
	percpu_counter_destroy(&m->d_lease_mis);
	percpu_counter_destroy(&m->d_lease_hit);
}

static inline void __update_latency(ktime_t *totalp, ktime_t *lsump,
				    ktime_t *min, ktime_t *max,
				    ktime_t *sq_sump, ktime_t lat)
{
	ktime_t total, avg, sq, lsum;

	total = ++(*totalp);
	lsum = (*lsump += lat);

	if (unlikely(lat < *min))
		*min = lat;
	if (unlikely(lat > *max))
		*max = lat;

	if (unlikely(total == 1))
		return;

	/* the sq is (lat - old_avg) * (lat - new_avg) */
	avg = DIV64_U64_ROUND_CLOSEST((lsum - lat), (total - 1));
	sq = lat - avg;
	avg = DIV64_U64_ROUND_CLOSEST(lsum, total);
	sq = sq * (lat - avg);
	*sq_sump += sq;
}

void ceph_update_read_latency(struct ceph_client_metric *m,
			      ktime_t r_start, ktime_t r_end,
			      int rc)
{
	ktime_t lat = ktime_sub(r_end, r_start);

	if (unlikely(rc < 0 && rc != -ENOENT && rc != -ETIMEDOUT))
		return;

	spin_lock(&m->read_latency_lock);
	__update_latency(&m->total_reads, &m->read_latency_sum,
			 &m->read_latency_min, &m->read_latency_max,
			 &m->read_latency_sq_sum, lat);
	spin_unlock(&m->read_latency_lock);
}

void ceph_update_write_latency(struct ceph_client_metric *m,
			       ktime_t r_start, ktime_t r_end,
			       int rc)
{
	ktime_t lat = ktime_sub(r_end, r_start);

	if (unlikely(rc && rc != -ETIMEDOUT))
		return;

	spin_lock(&m->write_latency_lock);
	__update_latency(&m->total_writes, &m->write_latency_sum,
			 &m->write_latency_min, &m->write_latency_max,
			 &m->write_latency_sq_sum, lat);
	spin_unlock(&m->write_latency_lock);
}

void ceph_update_metadata_latency(struct ceph_client_metric *m,
				  ktime_t r_start, ktime_t r_end,
				  int rc)
{
	ktime_t lat = ktime_sub(r_end, r_start);

	if (unlikely(rc && rc != -ENOENT))
		return;

	spin_lock(&m->metadata_latency_lock);
	__update_latency(&m->total_metadatas, &m->metadata_latency_sum,
			 &m->metadata_latency_min, &m->metadata_latency_max,
			 &m->metadata_latency_sq_sum, lat);
	spin_unlock(&m->metadata_latency_lock);
}

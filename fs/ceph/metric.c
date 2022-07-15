/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/ceph/ceph_debug.h>

#include <linux/types.h>
#include <linux/percpu_counter.h>
#include <linux/math64.h>

#include "metric.h"
#include "mds_client.h"

static bool ceph_mdsc_send_metrics(struct ceph_mds_client *mdsc,
				   struct ceph_mds_session *s)
{
	struct ceph_metric_head *head;
	struct ceph_metric_cap *cap;
	struct ceph_metric_read_latency *read;
	struct ceph_metric_write_latency *write;
	struct ceph_metric_metadata_latency *meta;
	struct ceph_client_metric *m = &mdsc->metric;
	u64 nr_caps = atomic64_read(&m->total_caps);
	struct ceph_msg *msg;
	struct timespec64 ts;
	s64 sum;
	s32 items = 0;
	s32 len;

	len = sizeof(*head) + sizeof(*cap) + sizeof(*read) + sizeof(*write)
	      + sizeof(*meta);

	msg = ceph_msg_new(CEPH_MSG_CLIENT_METRICS, len, GFP_NOFS, true);
	if (!msg) {
		pr_err("send metrics to mds%d, failed to allocate message\n",
		       s->s_mds);
		return false;
	}

	head = msg->front.iov_base;

	/* encode the cap metric */
	cap = (struct ceph_metric_cap *)(head + 1);
	cap->type = cpu_to_le32(CLIENT_METRIC_TYPE_CAP_INFO);
	cap->ver = 1;
	cap->compat = 1;
	cap->data_len = cpu_to_le32(sizeof(*cap) - 10);
	cap->hit = cpu_to_le64(percpu_counter_sum(&mdsc->metric.i_caps_hit));
	cap->mis = cpu_to_le64(percpu_counter_sum(&mdsc->metric.i_caps_mis));
	cap->total = cpu_to_le64(nr_caps);
	items++;

	/* encode the read latency metric */
	read = (struct ceph_metric_read_latency *)(cap + 1);
	read->type = cpu_to_le32(CLIENT_METRIC_TYPE_READ_LATENCY);
	read->ver = 1;
	read->compat = 1;
	read->data_len = cpu_to_le32(sizeof(*read) - 10);
	sum = m->read_latency_sum;
	jiffies_to_timespec64(sum, &ts);
	read->sec = cpu_to_le32(ts.tv_sec);
	read->nsec = cpu_to_le32(ts.tv_nsec);
	items++;

	/* encode the write latency metric */
	write = (struct ceph_metric_write_latency *)(read + 1);
	write->type = cpu_to_le32(CLIENT_METRIC_TYPE_WRITE_LATENCY);
	write->ver = 1;
	write->compat = 1;
	write->data_len = cpu_to_le32(sizeof(*write) - 10);
	sum = m->write_latency_sum;
	jiffies_to_timespec64(sum, &ts);
	write->sec = cpu_to_le32(ts.tv_sec);
	write->nsec = cpu_to_le32(ts.tv_nsec);
	items++;

	/* encode the metadata latency metric */
	meta = (struct ceph_metric_metadata_latency *)(write + 1);
	meta->type = cpu_to_le32(CLIENT_METRIC_TYPE_METADATA_LATENCY);
	meta->ver = 1;
	meta->compat = 1;
	meta->data_len = cpu_to_le32(sizeof(*meta) - 10);
	sum = m->metadata_latency_sum;
	jiffies_to_timespec64(sum, &ts);
	meta->sec = cpu_to_le32(ts.tv_sec);
	meta->nsec = cpu_to_le32(ts.tv_nsec);
	items++;

	put_unaligned_le32(items, &head->num);
	msg->front.iov_len = len;
	msg->hdr.version = cpu_to_le16(1);
	msg->hdr.compat_version = cpu_to_le16(1);
	msg->hdr.front_len = cpu_to_le32(msg->front.iov_len);
	dout("client%llu send metrics to mds%d\n",
	     ceph_client_gid(mdsc->fsc->client), s->s_mds);
	ceph_con_send(&s->s_con, msg);

	return true;
}


static void metric_get_session(struct ceph_mds_client *mdsc)
{
	struct ceph_mds_session *s;
	int i;

	mutex_lock(&mdsc->mutex);
	for (i = 0; i < mdsc->max_sessions; i++) {
		s = __ceph_lookup_mds_session(mdsc, i);
		if (!s)
			continue;

		/*
		 * Skip it if MDS doesn't support the metric collection,
		 * or the MDS will close the session's socket connection
		 * directly when it get this message.
		 */
		if (check_session_state(s) &&
		    test_bit(CEPHFS_FEATURE_METRIC_COLLECT, &s->s_features)) {
			mdsc->metric.session = s;
			break;
		}

		ceph_put_mds_session(s);
	}
	mutex_unlock(&mdsc->mutex);
}

static void metric_delayed_work(struct work_struct *work)
{
	struct ceph_client_metric *m =
		container_of(work, struct ceph_client_metric, delayed_work.work);
	struct ceph_mds_client *mdsc =
		container_of(m, struct ceph_mds_client, metric);

	if (mdsc->stopping)
		return;

	if (!m->session || !check_session_state(m->session)) {
		if (m->session) {
			ceph_put_mds_session(m->session);
			m->session = NULL;
		}
		metric_get_session(mdsc);
	}
	if (m->session) {
		ceph_mdsc_send_metrics(mdsc, m->session);
		metric_schedule_delayed(m);
	}
}

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

	atomic64_set(&m->total_caps, 0);
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

	atomic64_set(&m->opened_files, 0);
	ret = percpu_counter_init(&m->opened_inodes, 0, GFP_KERNEL);
	if (ret)
		goto err_opened_inodes;
	ret = percpu_counter_init(&m->total_inodes, 0, GFP_KERNEL);
	if (ret)
		goto err_total_inodes;

	m->session = NULL;
	INIT_DELAYED_WORK(&m->delayed_work, metric_delayed_work);

	return 0;

err_total_inodes:
	percpu_counter_destroy(&m->opened_inodes);
err_opened_inodes:
	percpu_counter_destroy(&m->i_caps_mis);
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

	cancel_delayed_work_sync(&m->delayed_work);

	percpu_counter_destroy(&m->total_inodes);
	percpu_counter_destroy(&m->opened_inodes);
	percpu_counter_destroy(&m->i_caps_mis);
	percpu_counter_destroy(&m->i_caps_hit);
	percpu_counter_destroy(&m->d_lease_mis);
	percpu_counter_destroy(&m->d_lease_hit);

	ceph_put_mds_session(m->session);
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

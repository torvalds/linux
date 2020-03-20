/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/types.h>
#include <linux/percpu_counter.h>

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

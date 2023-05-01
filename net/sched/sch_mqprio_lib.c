// SPDX-License-Identifier: GPL-2.0-only

#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <linux/types.h>
#include <net/pkt_sched.h>

#include "sch_mqprio_lib.h"

/* Returns true if the intervals [a, b) and [c, d) overlap. */
static bool intervals_overlap(int a, int b, int c, int d)
{
	int left = max(a, c), right = min(b, d);

	return left < right;
}

static int mqprio_validate_queue_counts(struct net_device *dev,
					const struct tc_mqprio_qopt *qopt,
					bool allow_overlapping_txqs,
					struct netlink_ext_ack *extack)
{
	int i, j;

	for (i = 0; i < qopt->num_tc; i++) {
		unsigned int last = qopt->offset[i] + qopt->count[i];

		if (!qopt->count[i]) {
			NL_SET_ERR_MSG_FMT_MOD(extack, "No queues for TC %d",
					       i);
			return -EINVAL;
		}

		/* Verify the queue count is in tx range being equal to the
		 * real_num_tx_queues indicates the last queue is in use.
		 */
		if (qopt->offset[i] >= dev->real_num_tx_queues ||
		    last > dev->real_num_tx_queues) {
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "Queues %d:%d for TC %d exceed the %d TX queues available",
					       qopt->count[i], qopt->offset[i],
					       i, dev->real_num_tx_queues);
			return -EINVAL;
		}

		if (allow_overlapping_txqs)
			continue;

		/* Verify that the offset and counts do not overlap */
		for (j = i + 1; j < qopt->num_tc; j++) {
			if (intervals_overlap(qopt->offset[i], last,
					      qopt->offset[j],
					      qopt->offset[j] +
					      qopt->count[j])) {
				NL_SET_ERR_MSG_FMT_MOD(extack,
						       "TC %d queues %d@%d overlap with TC %d queues %d@%d",
						       i, qopt->count[i], qopt->offset[i],
						       j, qopt->count[j], qopt->offset[j]);
				return -EINVAL;
			}
		}
	}

	return 0;
}

int mqprio_validate_qopt(struct net_device *dev, struct tc_mqprio_qopt *qopt,
			 bool validate_queue_counts,
			 bool allow_overlapping_txqs,
			 struct netlink_ext_ack *extack)
{
	int i, err;

	/* Verify num_tc is not out of max range */
	if (qopt->num_tc > TC_MAX_QUEUE) {
		NL_SET_ERR_MSG(extack,
			       "Number of traffic classes is outside valid range");
		return -EINVAL;
	}

	/* Verify priority mapping uses valid tcs */
	for (i = 0; i <= TC_BITMASK; i++) {
		if (qopt->prio_tc_map[i] >= qopt->num_tc) {
			NL_SET_ERR_MSG(extack,
				       "Invalid traffic class in priority to traffic class mapping");
			return -EINVAL;
		}
	}

	if (validate_queue_counts) {
		err = mqprio_validate_queue_counts(dev, qopt,
						   allow_overlapping_txqs,
						   extack);
		if (err)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mqprio_validate_qopt);

void mqprio_qopt_reconstruct(struct net_device *dev, struct tc_mqprio_qopt *qopt)
{
	int tc, num_tc = netdev_get_num_tc(dev);

	qopt->num_tc = num_tc;
	memcpy(qopt->prio_tc_map, dev->prio_tc_map, sizeof(qopt->prio_tc_map));

	for (tc = 0; tc < num_tc; tc++) {
		qopt->count[tc] = dev->tc_to_txq[tc].count;
		qopt->offset[tc] = dev->tc_to_txq[tc].offset;
	}
}
EXPORT_SYMBOL_GPL(mqprio_qopt_reconstruct);

MODULE_LICENSE("GPL");

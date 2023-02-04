/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SCH_MQPRIO_LIB_H
#define __SCH_MQPRIO_LIB_H

#include <linux/types.h>

struct net_device;
struct netlink_ext_ack;
struct tc_mqprio_qopt;

int mqprio_validate_qopt(struct net_device *dev, struct tc_mqprio_qopt *qopt,
			 bool validate_queue_counts,
			 bool allow_overlapping_txqs,
			 struct netlink_ext_ack *extack);
void mqprio_qopt_reconstruct(struct net_device *dev,
			     struct tc_mqprio_qopt *qopt);

#endif

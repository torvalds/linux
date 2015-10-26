/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef _QED_ETH_IF_H
#define _QED_ETH_IF_H

#include <linux/list.h>
#include <linux/if_link.h>
#include <linux/qed/eth_common.h>
#include <linux/qed/qed_if.h>

struct qed_dev_eth_info {
	struct qed_dev_info common;

	u8	num_queues;
	u8	num_tc;

	u8	port_mac[ETH_ALEN];
	u8	num_vlan_filters;
};

struct qed_eth_ops {
	const struct qed_common_ops *common;

	int (*fill_dev_info)(struct qed_dev *cdev,
			     struct qed_dev_eth_info *info);

};

const struct qed_eth_ops *qed_get_eth_ops(u32 version);
void qed_put_eth_ops(void);

#endif

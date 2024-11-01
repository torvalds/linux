/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __NET_MCTP_TEST_UTILS_H
#define __NET_MCTP_TEST_UTILS_H

#include <kunit/test.h>

#define MCTP_DEV_TEST_MTU	68

struct mctp_test_dev {
	struct net_device *ndev;
	struct mctp_dev *mdev;
};

struct mctp_test_dev;

struct mctp_test_dev *mctp_test_create_dev(void);
void mctp_test_destroy_dev(struct mctp_test_dev *dev);

#endif /* __NET_MCTP_TEST_UTILS_H */

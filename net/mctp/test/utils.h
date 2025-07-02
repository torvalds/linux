/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __NET_MCTP_TEST_UTILS_H
#define __NET_MCTP_TEST_UTILS_H

#include <uapi/linux/netdevice.h>

#include <kunit/test.h>

#define MCTP_DEV_TEST_MTU	68

struct mctp_test_dev {
	struct net_device *ndev;
	struct mctp_dev *mdev;

	unsigned short lladdr_len;
	unsigned char lladdr[MAX_ADDR_LEN];
};

struct mctp_test_dev;

struct mctp_test_dev *mctp_test_create_dev(void);
struct mctp_test_dev *mctp_test_create_dev_lladdr(unsigned short lladdr_len,
						  const unsigned char *lladdr);
void mctp_test_destroy_dev(struct mctp_test_dev *dev);

#endif /* __NET_MCTP_TEST_UTILS_H */

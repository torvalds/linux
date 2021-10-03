// SPDX-License-Identifier: GPL-2.0

#include <linux/netdevice.h>
#include <linux/mctp.h>
#include <linux/if_arp.h>

#include <net/mctpdevice.h>
#include <net/pkt_sched.h>

#include "utils.h"

static netdev_tx_t mctp_test_dev_tx(struct sk_buff *skb,
				    struct net_device *ndev)
{
	kfree(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops mctp_test_netdev_ops = {
	.ndo_start_xmit = mctp_test_dev_tx,
};

static void mctp_test_dev_setup(struct net_device *ndev)
{
	ndev->type = ARPHRD_MCTP;
	ndev->mtu = MCTP_DEV_TEST_MTU;
	ndev->hard_header_len = 0;
	ndev->addr_len = 0;
	ndev->tx_queue_len = DEFAULT_TX_QUEUE_LEN;
	ndev->flags = IFF_NOARP;
	ndev->netdev_ops = &mctp_test_netdev_ops;
	ndev->needs_free_netdev = true;
}

struct mctp_test_dev *mctp_test_create_dev(void)
{
	struct mctp_test_dev *dev;
	struct net_device *ndev;
	int rc;

	ndev = alloc_netdev(sizeof(*dev), "mctptest%d", NET_NAME_ENUM,
			    mctp_test_dev_setup);
	if (!ndev)
		return NULL;

	dev = netdev_priv(ndev);
	dev->ndev = ndev;

	rc = register_netdev(ndev);
	if (rc) {
		free_netdev(ndev);
		return NULL;
	}

	rcu_read_lock();
	dev->mdev = __mctp_dev_get(ndev);
	mctp_dev_hold(dev->mdev);
	rcu_read_unlock();

	return dev;
}

void mctp_test_destroy_dev(struct mctp_test_dev *dev)
{
	mctp_dev_put(dev->mdev);
	unregister_netdev(dev->ndev);
}

// SPDX-License-Identifier: GPL-2.0

#include <linux/netdevice.h>
#include <linux/mctp.h>
#include <linux/if_arp.h>

#include <net/mctp.h>
#include <net/mctpdevice.h>
#include <net/pkt_sched.h>

#include "utils.h"

static netdev_tx_t mctp_test_dev_tx(struct sk_buff *skb,
				    struct net_device *ndev)
{
	kfree_skb(skb);
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
	ndev->tx_queue_len = DEFAULT_TX_QUEUE_LEN;
	ndev->flags = IFF_NOARP;
	ndev->netdev_ops = &mctp_test_netdev_ops;
	ndev->needs_free_netdev = true;
}

static struct mctp_test_dev *__mctp_test_create_dev(unsigned short lladdr_len,
						    const unsigned char *lladdr)
{
	struct mctp_test_dev *dev;
	struct net_device *ndev;
	int rc;

	if (WARN_ON(lladdr_len > MAX_ADDR_LEN))
		return NULL;

	ndev = alloc_netdev(sizeof(*dev), "mctptest%d", NET_NAME_ENUM,
			    mctp_test_dev_setup);
	if (!ndev)
		return NULL;

	dev = netdev_priv(ndev);
	dev->ndev = ndev;
	ndev->addr_len = lladdr_len;
	dev_addr_set(ndev, lladdr);

	rc = register_netdev(ndev);
	if (rc) {
		free_netdev(ndev);
		return NULL;
	}

	rcu_read_lock();
	dev->mdev = __mctp_dev_get(ndev);
	dev->mdev->net = mctp_default_net(dev_net(ndev));
	rcu_read_unlock();

	return dev;
}

struct mctp_test_dev *mctp_test_create_dev(void)
{
	return __mctp_test_create_dev(0, NULL);
}

struct mctp_test_dev *mctp_test_create_dev_lladdr(unsigned short lladdr_len,
						  const unsigned char *lladdr)
{
	return __mctp_test_create_dev(lladdr_len, lladdr);
}

void mctp_test_destroy_dev(struct mctp_test_dev *dev)
{
	mctp_dev_put(dev->mdev);
	unregister_netdev(dev->ndev);
}

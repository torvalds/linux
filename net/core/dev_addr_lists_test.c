// SPDX-License-Identifier: GPL-2.0-or-later

#include <kunit/test.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>

static const struct net_device_ops dummy_netdev_ops = {
};

struct dev_addr_test_priv {
	u32 addr_seen;
};

static int dev_addr_test_sync(struct net_device *netdev, const unsigned char *a)
{
	struct dev_addr_test_priv *datp = netdev_priv(netdev);

	if (a[0] < 31 && !memchr_inv(a, a[0], ETH_ALEN))
		datp->addr_seen |= 1 << a[0];
	return 0;
}

static int dev_addr_test_unsync(struct net_device *netdev,
				const unsigned char *a)
{
	struct dev_addr_test_priv *datp = netdev_priv(netdev);

	if (a[0] < 31 && !memchr_inv(a, a[0], ETH_ALEN))
		datp->addr_seen &= ~(1 << a[0]);
	return 0;
}

static int dev_addr_test_init(struct kunit *test)
{
	struct dev_addr_test_priv *datp;
	struct net_device *netdev;
	int err;

	netdev = alloc_etherdev(sizeof(*datp));
	KUNIT_ASSERT_TRUE(test, !!netdev);

	test->priv = netdev;
	netdev->netdev_ops = &dummy_netdev_ops;

	err = register_netdev(netdev);
	if (err) {
		free_netdev(netdev);
		KUNIT_FAIL(test, "Can't register netdev %d", err);
	}

	rtnl_lock();
	return 0;
}

static void dev_addr_test_exit(struct kunit *test)
{
	struct net_device *netdev = test->priv;

	rtnl_unlock();
	unregister_netdev(netdev);
	free_netdev(netdev);
}

static void dev_addr_test_basic(struct kunit *test)
{
	struct net_device *netdev = test->priv;
	u8 addr[ETH_ALEN];

	KUNIT_EXPECT_TRUE(test, !!netdev->dev_addr);

	memset(addr, 2, sizeof(addr));
	eth_hw_addr_set(netdev, addr);
	KUNIT_EXPECT_MEMEQ(test, netdev->dev_addr, addr, sizeof(addr));

	memset(addr, 3, sizeof(addr));
	dev_addr_set(netdev, addr);
	KUNIT_EXPECT_MEMEQ(test, netdev->dev_addr, addr, sizeof(addr));
}

static void dev_addr_test_sync_one(struct kunit *test)
{
	struct net_device *netdev = test->priv;
	struct dev_addr_test_priv *datp;
	u8 addr[ETH_ALEN];

	datp = netdev_priv(netdev);

	memset(addr, 1, sizeof(addr));
	eth_hw_addr_set(netdev, addr);

	__hw_addr_sync_dev(&netdev->dev_addrs, netdev, dev_addr_test_sync,
			   dev_addr_test_unsync);
	KUNIT_EXPECT_EQ(test, 2, datp->addr_seen);

	memset(addr, 2, sizeof(addr));
	eth_hw_addr_set(netdev, addr);

	datp->addr_seen = 0;
	__hw_addr_sync_dev(&netdev->dev_addrs, netdev, dev_addr_test_sync,
			   dev_addr_test_unsync);
	/* It's not going to sync anything because the main address is
	 * considered synced and we overwrite in place.
	 */
	KUNIT_EXPECT_EQ(test, 0, datp->addr_seen);
}

static void dev_addr_test_add_del(struct kunit *test)
{
	struct net_device *netdev = test->priv;
	struct dev_addr_test_priv *datp;
	u8 addr[ETH_ALEN];
	int i;

	datp = netdev_priv(netdev);

	for (i = 1; i < 4; i++) {
		memset(addr, i, sizeof(addr));
		KUNIT_EXPECT_EQ(test, 0, dev_addr_add(netdev, addr,
						      NETDEV_HW_ADDR_T_LAN));
	}
	/* Add 3 again */
	KUNIT_EXPECT_EQ(test, 0, dev_addr_add(netdev, addr,
					      NETDEV_HW_ADDR_T_LAN));

	__hw_addr_sync_dev(&netdev->dev_addrs, netdev, dev_addr_test_sync,
			   dev_addr_test_unsync);
	KUNIT_EXPECT_EQ(test, 0xf, datp->addr_seen);

	KUNIT_EXPECT_EQ(test, 0, dev_addr_del(netdev, addr,
					      NETDEV_HW_ADDR_T_LAN));

	__hw_addr_sync_dev(&netdev->dev_addrs, netdev, dev_addr_test_sync,
			   dev_addr_test_unsync);
	KUNIT_EXPECT_EQ(test, 0xf, datp->addr_seen);

	for (i = 1; i < 4; i++) {
		memset(addr, i, sizeof(addr));
		KUNIT_EXPECT_EQ(test, 0, dev_addr_del(netdev, addr,
						      NETDEV_HW_ADDR_T_LAN));
	}

	__hw_addr_sync_dev(&netdev->dev_addrs, netdev, dev_addr_test_sync,
			   dev_addr_test_unsync);
	KUNIT_EXPECT_EQ(test, 1, datp->addr_seen);
}

static void dev_addr_test_del_main(struct kunit *test)
{
	struct net_device *netdev = test->priv;
	u8 addr[ETH_ALEN];

	memset(addr, 1, sizeof(addr));
	eth_hw_addr_set(netdev, addr);

	KUNIT_EXPECT_EQ(test, -ENOENT, dev_addr_del(netdev, addr,
						    NETDEV_HW_ADDR_T_LAN));
	KUNIT_EXPECT_EQ(test, 0, dev_addr_add(netdev, addr,
					      NETDEV_HW_ADDR_T_LAN));
	KUNIT_EXPECT_EQ(test, 0, dev_addr_del(netdev, addr,
					      NETDEV_HW_ADDR_T_LAN));
	KUNIT_EXPECT_EQ(test, -ENOENT, dev_addr_del(netdev, addr,
						    NETDEV_HW_ADDR_T_LAN));
}

static void dev_addr_test_add_set(struct kunit *test)
{
	struct net_device *netdev = test->priv;
	struct dev_addr_test_priv *datp;
	u8 addr[ETH_ALEN];
	int i;

	datp = netdev_priv(netdev);

	/* There is no external API like dev_addr_add_excl(),
	 * so shuffle the tree a little bit and exploit aliasing.
	 */
	for (i = 1; i < 16; i++) {
		memset(addr, i, sizeof(addr));
		KUNIT_EXPECT_EQ(test, 0, dev_addr_add(netdev, addr,
						      NETDEV_HW_ADDR_T_LAN));
	}

	memset(addr, i, sizeof(addr));
	eth_hw_addr_set(netdev, addr);
	KUNIT_EXPECT_EQ(test, 0, dev_addr_add(netdev, addr,
					      NETDEV_HW_ADDR_T_LAN));
	memset(addr, 0, sizeof(addr));
	eth_hw_addr_set(netdev, addr);

	__hw_addr_sync_dev(&netdev->dev_addrs, netdev, dev_addr_test_sync,
			   dev_addr_test_unsync);
	KUNIT_EXPECT_EQ(test, 0xffff, datp->addr_seen);
}

static void dev_addr_test_add_excl(struct kunit *test)
{
	struct net_device *netdev = test->priv;
	u8 addr[ETH_ALEN];
	int i;

	for (i = 0; i < 10; i++) {
		memset(addr, i, sizeof(addr));
		KUNIT_EXPECT_EQ(test, 0, dev_uc_add_excl(netdev, addr));
	}
	KUNIT_EXPECT_EQ(test, -EEXIST, dev_uc_add_excl(netdev, addr));

	for (i = 0; i < 10; i += 2) {
		memset(addr, i, sizeof(addr));
		KUNIT_EXPECT_EQ(test, 0, dev_uc_del(netdev, addr));
	}
	for (i = 1; i < 10; i += 2) {
		memset(addr, i, sizeof(addr));
		KUNIT_EXPECT_EQ(test, -EEXIST, dev_uc_add_excl(netdev, addr));
	}
}

static struct kunit_case dev_addr_test_cases[] = {
	KUNIT_CASE(dev_addr_test_basic),
	KUNIT_CASE(dev_addr_test_sync_one),
	KUNIT_CASE(dev_addr_test_add_del),
	KUNIT_CASE(dev_addr_test_del_main),
	KUNIT_CASE(dev_addr_test_add_set),
	KUNIT_CASE(dev_addr_test_add_excl),
	{}
};

static struct kunit_suite dev_addr_test_suite = {
	.name = "dev-addr-list-test",
	.test_cases = dev_addr_test_cases,
	.init = dev_addr_test_init,
	.exit = dev_addr_test_exit,
};
kunit_test_suite(dev_addr_test_suite);

MODULE_DESCRIPTION("KUnit tests for struct netdev_hw_addr_list");
MODULE_LICENSE("GPL");

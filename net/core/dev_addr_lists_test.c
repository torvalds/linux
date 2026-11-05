// SPDX-License-Identifier: GPL-2.0-or-later

#include <kunit/test.h>
#include <linux/etherdevice.h>
#include <linux/math64.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>

static const struct net_device_ops dummy_netdev_ops = {
};

#define ADDR_A	1
#define ADDR_B	2
#define ADDR_C	3

struct dev_addr_test_priv {
	u32 addr_seen;
	u32 addr_synced;
	u32 addr_unsynced;
};

static int dev_addr_test_sync(struct net_device *netdev, const unsigned char *a)
{
	struct dev_addr_test_priv *datp = netdev_priv(netdev);

	if (a[0] < 31 && !memchr_inv(a, a[0], ETH_ALEN)) {
		datp->addr_seen |= 1 << a[0];
		datp->addr_synced |= 1 << a[0];
	}
	return 0;
}

static int dev_addr_test_unsync(struct net_device *netdev,
				const unsigned char *a)
{
	struct dev_addr_test_priv *datp = netdev_priv(netdev);

	if (a[0] < 31 && !memchr_inv(a, a[0], ETH_ALEN)) {
		datp->addr_seen &= ~(1 << a[0]);
		datp->addr_unsynced |= 1 << a[0];
	}
	return 0;
}

static void dev_addr_test_reset(struct net_device *netdev)
{
	struct dev_addr_test_priv *datp = netdev_priv(netdev);

	datp->addr_seen = 0;
	datp->addr_synced = 0;
	datp->addr_unsynced = 0;
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

	return 0;
}

static void dev_addr_test_exit(struct kunit *test)
{
	struct net_device *netdev = test->priv;

	unregister_netdev(netdev);
	free_netdev(netdev);
}

static void dev_addr_test_basic(struct kunit *test)
{
	struct net_device *netdev = test->priv;
	u8 addr[ETH_ALEN];

	rtnl_lock();
	KUNIT_EXPECT_TRUE(test, !!netdev->dev_addr);

	memset(addr, 2, sizeof(addr));
	eth_hw_addr_set(netdev, addr);
	KUNIT_EXPECT_MEMEQ(test, netdev->dev_addr, addr, sizeof(addr));

	memset(addr, 3, sizeof(addr));
	dev_addr_set(netdev, addr);
	KUNIT_EXPECT_MEMEQ(test, netdev->dev_addr, addr, sizeof(addr));
	rtnl_unlock();
}

static void dev_addr_test_sync_one(struct kunit *test)
{
	struct net_device *netdev = test->priv;
	struct dev_addr_test_priv *datp;
	u8 addr[ETH_ALEN];

	datp = netdev_priv(netdev);

	rtnl_lock();
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
	rtnl_unlock();
}

static void dev_addr_test_add_del(struct kunit *test)
{
	struct net_device *netdev = test->priv;
	struct dev_addr_test_priv *datp;
	u8 addr[ETH_ALEN];
	int i;

	datp = netdev_priv(netdev);

	rtnl_lock();
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
	rtnl_unlock();
}

static void dev_addr_test_del_main(struct kunit *test)
{
	struct net_device *netdev = test->priv;
	u8 addr[ETH_ALEN];

	rtnl_lock();
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
	rtnl_unlock();
}

static void dev_addr_test_add_set(struct kunit *test)
{
	struct net_device *netdev = test->priv;
	struct dev_addr_test_priv *datp;
	u8 addr[ETH_ALEN];
	int i;

	datp = netdev_priv(netdev);

	rtnl_lock();
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
	rtnl_unlock();
}

static void dev_addr_test_add_excl(struct kunit *test)
{
	struct net_device *netdev = test->priv;
	u8 addr[ETH_ALEN];
	int i;

	rtnl_lock();
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
	rtnl_unlock();
}

/* Snapshot test: basic sync with no concurrent modifications.
 * Add one address, snapshot, driver syncs it, reconcile propagates
 * sync_cnt delta back to real list.
 */
static void dev_addr_test_snapshot_sync(struct kunit *test)
{
	struct netdev_hw_addr_list snap, ref, cache;
	struct net_device *netdev = test->priv;
	struct dev_addr_test_priv *datp;
	struct netdev_hw_addr *ha;
	u8 addr[ETH_ALEN];

	datp = netdev_priv(netdev);

	rtnl_lock();

	memset(addr, ADDR_A, sizeof(addr));
	KUNIT_EXPECT_EQ(test, 0, dev_uc_add(netdev, addr));

	/* Snapshot: ADDR_A has sync_cnt=0, refcount=1 (new) */
	netif_addr_lock_bh(netdev);
	__hw_addr_init(&snap);
	__hw_addr_init(&ref);
	__hw_addr_init(&cache);
	KUNIT_EXPECT_EQ(test, 0,
			__hw_addr_list_snapshot(&snap, &netdev->uc, ETH_ALEN,
						&cache));
	KUNIT_EXPECT_EQ(test, 0,
			__hw_addr_list_snapshot(&ref, &netdev->uc, ETH_ALEN,
						&cache));
	netif_addr_unlock_bh(netdev);

	/* Driver syncs ADDR_A to hardware */
	dev_addr_test_reset(netdev);
	__hw_addr_sync_dev(&snap, netdev, dev_addr_test_sync,
			   dev_addr_test_unsync);
	KUNIT_EXPECT_EQ(test, 1 << ADDR_A, datp->addr_synced);
	KUNIT_EXPECT_EQ(test, 0, datp->addr_unsynced);

	/* Reconcile: delta=+1 applied to real entry */
	netif_addr_lock_bh(netdev);
	__hw_addr_list_reconcile(&netdev->uc, &snap, &ref, ETH_ALEN,
				 &cache);
	netif_addr_unlock_bh(netdev);

	/* Real entry should now reflect the sync: sync_cnt=1, refcount=2 */
	KUNIT_EXPECT_EQ(test, 1, netdev_uc_count(netdev));
	ha = list_first_entry(&netdev->uc.list, struct netdev_hw_addr, list);
	KUNIT_EXPECT_MEMEQ(test, ha->addr, addr, ETH_ALEN);
	KUNIT_EXPECT_EQ(test, 1, ha->sync_cnt);
	KUNIT_EXPECT_EQ(test, 2, ha->refcount);

	/* Second work run: already synced, nothing to do */
	dev_addr_test_reset(netdev);
	__hw_addr_sync_dev(&netdev->uc, netdev, dev_addr_test_sync,
			   dev_addr_test_unsync);
	KUNIT_EXPECT_EQ(test, 0, datp->addr_synced);
	KUNIT_EXPECT_EQ(test, 0, datp->addr_unsynced);
	KUNIT_EXPECT_EQ(test, 1, netdev_uc_count(netdev));

	__hw_addr_flush(&cache);
	rtnl_unlock();
}

/* Snapshot test: ADDR_A synced to hardware, then concurrently removed
 * from the real list before reconcile runs. Reconcile re-inserts ADDR_A as
 * a stale entry so the next work run unsyncs it from hardware.
 */
static void dev_addr_test_snapshot_remove_during_sync(struct kunit *test)
{
	struct netdev_hw_addr_list snap, ref, cache;
	struct net_device *netdev = test->priv;
	struct dev_addr_test_priv *datp;
	struct netdev_hw_addr *ha;
	u8 addr[ETH_ALEN];

	datp = netdev_priv(netdev);

	rtnl_lock();

	memset(addr, ADDR_A, sizeof(addr));
	KUNIT_EXPECT_EQ(test, 0, dev_uc_add(netdev, addr));

	/* Snapshot: ADDR_A is new (sync_cnt=0, refcount=1) */
	netif_addr_lock_bh(netdev);
	__hw_addr_init(&snap);
	__hw_addr_init(&ref);
	__hw_addr_init(&cache);
	KUNIT_EXPECT_EQ(test, 0,
			__hw_addr_list_snapshot(&snap, &netdev->uc, ETH_ALEN,
						&cache));
	KUNIT_EXPECT_EQ(test, 0,
			__hw_addr_list_snapshot(&ref, &netdev->uc, ETH_ALEN,
						&cache));
	netif_addr_unlock_bh(netdev);

	/* Driver syncs ADDR_A to hardware */
	dev_addr_test_reset(netdev);
	__hw_addr_sync_dev(&snap, netdev, dev_addr_test_sync,
			   dev_addr_test_unsync);
	KUNIT_EXPECT_EQ(test, 1 << ADDR_A, datp->addr_synced);
	KUNIT_EXPECT_EQ(test, 0, datp->addr_unsynced);

	/* Concurrent removal: user deletes ADDR_A while driver was working */
	memset(addr, ADDR_A, sizeof(addr));
	KUNIT_EXPECT_EQ(test, 0, dev_uc_del(netdev, addr));
	KUNIT_EXPECT_EQ(test, 0, netdev_uc_count(netdev));

	/* Reconcile: ADDR_A gone from real list but driver synced it,
	 * so it gets re-inserted as stale (sync_cnt=1, refcount=1).
	 */
	netif_addr_lock_bh(netdev);
	__hw_addr_list_reconcile(&netdev->uc, &snap, &ref, ETH_ALEN,
				 &cache);
	netif_addr_unlock_bh(netdev);

	KUNIT_EXPECT_EQ(test, 1, netdev_uc_count(netdev));
	ha = list_first_entry(&netdev->uc.list, struct netdev_hw_addr, list);
	KUNIT_EXPECT_MEMEQ(test, ha->addr, addr, ETH_ALEN);
	KUNIT_EXPECT_EQ(test, 1, ha->sync_cnt);
	KUNIT_EXPECT_EQ(test, 1, ha->refcount);

	/* Second work run: stale entry gets unsynced from HW and removed */
	dev_addr_test_reset(netdev);
	__hw_addr_sync_dev(&netdev->uc, netdev, dev_addr_test_sync,
			   dev_addr_test_unsync);
	KUNIT_EXPECT_EQ(test, 0, datp->addr_synced);
	KUNIT_EXPECT_EQ(test, 1 << ADDR_A, datp->addr_unsynced);
	KUNIT_EXPECT_EQ(test, 0, netdev_uc_count(netdev));

	__hw_addr_flush(&cache);
	rtnl_unlock();
}

/* Snapshot test: ADDR_A was stale (unsynced from hardware by driver),
 * but concurrently re-added by the user. The re-add bumps refcount of
 * the existing stale entry. Reconcile applies delta=-1, leaving ADDR_A
 * as a fresh entry (sync_cnt=0, refcount=1) for the next work run.
 */
static void dev_addr_test_snapshot_readd_during_unsync(struct kunit *test)
{
	struct netdev_hw_addr_list snap, ref, cache;
	struct net_device *netdev = test->priv;
	struct dev_addr_test_priv *datp;
	struct netdev_hw_addr *ha;
	u8 addr[ETH_ALEN];

	datp = netdev_priv(netdev);

	rtnl_lock();

	memset(addr, ADDR_A, sizeof(addr));
	KUNIT_EXPECT_EQ(test, 0, dev_uc_add(netdev, addr));

	/* Sync ADDR_A to hardware: sync_cnt=1, refcount=2 */
	dev_addr_test_reset(netdev);
	__hw_addr_sync_dev(&netdev->uc, netdev, dev_addr_test_sync,
			   dev_addr_test_unsync);
	KUNIT_EXPECT_EQ(test, 1 << ADDR_A, datp->addr_synced);
	KUNIT_EXPECT_EQ(test, 0, datp->addr_unsynced);

	/* User removes ADDR_A: refcount=1, sync_cnt=1 -> stale */
	KUNIT_EXPECT_EQ(test, 0, dev_uc_del(netdev, addr));

	/* Snapshot: ADDR_A is stale (sync_cnt=1, refcount=1) */
	netif_addr_lock_bh(netdev);
	__hw_addr_init(&snap);
	__hw_addr_init(&ref);
	__hw_addr_init(&cache);
	KUNIT_EXPECT_EQ(test, 0,
			__hw_addr_list_snapshot(&snap, &netdev->uc, ETH_ALEN,
						&cache));
	KUNIT_EXPECT_EQ(test, 0,
			__hw_addr_list_snapshot(&ref, &netdev->uc, ETH_ALEN,
						&cache));
	netif_addr_unlock_bh(netdev);

	/* Driver unsyncs stale ADDR_A from hardware */
	dev_addr_test_reset(netdev);
	__hw_addr_sync_dev(&snap, netdev, dev_addr_test_sync,
			   dev_addr_test_unsync);
	KUNIT_EXPECT_EQ(test, 0, datp->addr_synced);
	KUNIT_EXPECT_EQ(test, 1 << ADDR_A, datp->addr_unsynced);

	/* Concurrent: user re-adds ADDR_A.  dev_uc_add finds the existing
	 * stale entry and bumps refcount from 1 -> 2.  sync_cnt stays 1.
	 */
	KUNIT_EXPECT_EQ(test, 0, dev_uc_add(netdev, addr));
	KUNIT_EXPECT_EQ(test, 1, netdev_uc_count(netdev));

	/* Reconcile: ref sync_cnt=1 matches real sync_cnt=1, delta=-1
	 * applied. Result: sync_cnt=0, refcount=1 (fresh).
	 */
	netif_addr_lock_bh(netdev);
	__hw_addr_list_reconcile(&netdev->uc, &snap, &ref, ETH_ALEN,
				 &cache);
	netif_addr_unlock_bh(netdev);

	/* Entry survives as fresh: needs re-sync to HW */
	KUNIT_EXPECT_EQ(test, 1, netdev_uc_count(netdev));
	ha = list_first_entry(&netdev->uc.list, struct netdev_hw_addr, list);
	KUNIT_EXPECT_MEMEQ(test, ha->addr, addr, ETH_ALEN);
	KUNIT_EXPECT_EQ(test, 0, ha->sync_cnt);
	KUNIT_EXPECT_EQ(test, 1, ha->refcount);

	/* Second work run: fresh entry gets synced to HW */
	dev_addr_test_reset(netdev);
	__hw_addr_sync_dev(&netdev->uc, netdev, dev_addr_test_sync,
			   dev_addr_test_unsync);
	KUNIT_EXPECT_EQ(test, 1 << ADDR_A, datp->addr_synced);
	KUNIT_EXPECT_EQ(test, 0, datp->addr_unsynced);

	__hw_addr_flush(&cache);
	rtnl_unlock();
}

/* Snapshot test: ADDR_A is new (synced by driver), and independent ADDR_B
 * is concurrently removed from the real list. A's sync delta propagates
 * normally; B's absence doesn't interfere.
 */
static void dev_addr_test_snapshot_add_and_remove(struct kunit *test)
{
	struct netdev_hw_addr_list snap, ref, cache;
	struct net_device *netdev = test->priv;
	struct dev_addr_test_priv *datp;
	struct netdev_hw_addr *ha;
	u8 addr[ETH_ALEN];

	datp = netdev_priv(netdev);

	rtnl_lock();

	/* Add ADDR_A and ADDR_B (will be synced then removed) */
	memset(addr, ADDR_A, sizeof(addr));
	KUNIT_EXPECT_EQ(test, 0, dev_uc_add(netdev, addr));
	memset(addr, ADDR_B, sizeof(addr));
	KUNIT_EXPECT_EQ(test, 0, dev_uc_add(netdev, addr));

	/* Sync both to hardware: sync_cnt=1, refcount=2 */
	__hw_addr_sync_dev(&netdev->uc, netdev, dev_addr_test_sync,
			   dev_addr_test_unsync);

	/* Add ADDR_C (new, will be synced by snapshot) */
	memset(addr, ADDR_C, sizeof(addr));
	KUNIT_EXPECT_EQ(test, 0, dev_uc_add(netdev, addr));

	/* Snapshot: A,B synced (sync_cnt=1,refcount=2); C new (0,1) */
	netif_addr_lock_bh(netdev);
	__hw_addr_init(&snap);
	__hw_addr_init(&ref);
	__hw_addr_init(&cache);
	KUNIT_EXPECT_EQ(test, 0,
			__hw_addr_list_snapshot(&snap, &netdev->uc, ETH_ALEN,
						&cache));
	KUNIT_EXPECT_EQ(test, 0,
			__hw_addr_list_snapshot(&ref, &netdev->uc, ETH_ALEN,
						&cache));
	netif_addr_unlock_bh(netdev);

	/* Driver syncs snapshot: ADDR_C is new -> synced; A,B already synced */
	dev_addr_test_reset(netdev);
	__hw_addr_sync_dev(&snap, netdev, dev_addr_test_sync,
			   dev_addr_test_unsync);
	KUNIT_EXPECT_EQ(test, 1 << ADDR_C, datp->addr_synced);
	KUNIT_EXPECT_EQ(test, 0, datp->addr_unsynced);

	/* Concurrent: user removes addr B while driver was working */
	memset(addr, ADDR_B, sizeof(addr));
	KUNIT_EXPECT_EQ(test, 0, dev_uc_del(netdev, addr));

	/* Reconcile: ADDR_C's delta=+1 applied to real list.
	 * ADDR_B's delta=0 (unchanged in snapshot),
	 * so nothing to apply to ADDR_B.
	 */
	netif_addr_lock_bh(netdev);
	__hw_addr_list_reconcile(&netdev->uc, &snap, &ref, ETH_ALEN,
				 &cache);
	netif_addr_unlock_bh(netdev);

	/* ADDR_A: unchanged (sync_cnt=1, refcount=2)
	 * ADDR_B: refcount went from 2->1 via dev_uc_del (still present, stale)
	 * ADDR_C: sync propagated (sync_cnt=1, refcount=2)
	 */
	KUNIT_EXPECT_EQ(test, 3, netdev_uc_count(netdev));
	netdev_hw_addr_list_for_each(ha, &netdev->uc) {
		u8 id = ha->addr[0];

		if (!memchr_inv(ha->addr, id, ETH_ALEN)) {
			if (id == ADDR_A) {
				KUNIT_EXPECT_EQ(test, 1, ha->sync_cnt);
				KUNIT_EXPECT_EQ(test, 2, ha->refcount);
			} else if (id == ADDR_B) {
				/* B: still present but now stale */
				KUNIT_EXPECT_EQ(test, 1, ha->sync_cnt);
				KUNIT_EXPECT_EQ(test, 1, ha->refcount);
			} else if (id == ADDR_C) {
				KUNIT_EXPECT_EQ(test, 1, ha->sync_cnt);
				KUNIT_EXPECT_EQ(test, 2, ha->refcount);
			}
		}
	}

	/* Second work run: ADDR_B is stale, gets unsynced and removed */
	dev_addr_test_reset(netdev);
	__hw_addr_sync_dev(&netdev->uc, netdev, dev_addr_test_sync,
			   dev_addr_test_unsync);
	KUNIT_EXPECT_EQ(test, 0, datp->addr_synced);
	KUNIT_EXPECT_EQ(test, 1 << ADDR_B, datp->addr_unsynced);
	KUNIT_EXPECT_EQ(test, 2, netdev_uc_count(netdev));

	__hw_addr_flush(&cache);
	rtnl_unlock();
}

static void dev_addr_test_snapshot_benchmark(struct kunit *test)
{
	struct net_device *netdev = test->priv;
	struct netdev_hw_addr_list snap, cache;
	u8 addr[ETH_ALEN];
	s64 duration = 0;
	ktime_t start;
	int i, iter;

	rtnl_lock();

	for (i = 0; i < 1024; i++) {
		memset(addr, 0, sizeof(addr));
		addr[0] = (i >> 8) & 0xff;
		addr[1] = i & 0xff;
		KUNIT_EXPECT_EQ(test, 0, dev_uc_add(netdev, addr));
	}

	__hw_addr_init(&cache);

	for (iter = 0; iter < 1000; iter++) {
		netif_addr_lock_bh(netdev);
		__hw_addr_init(&snap);

		start = ktime_get();
		KUNIT_EXPECT_EQ(test, 0,
				__hw_addr_list_snapshot(&snap, &netdev->uc,
							ETH_ALEN, &cache));
		duration += ktime_to_ns(ktime_sub(ktime_get(), start));

		netif_addr_unlock_bh(netdev);
		__hw_addr_flush(&snap);
	}

	__hw_addr_flush(&cache);

	kunit_info(test,
		   "1024 addrs x 1000 snapshots: %lld ns total, %lld ns/iter",
		   duration, div_s64(duration, 1000));

	rtnl_unlock();
}

static struct kunit_case dev_addr_test_cases[] = {
	KUNIT_CASE(dev_addr_test_basic),
	KUNIT_CASE(dev_addr_test_sync_one),
	KUNIT_CASE(dev_addr_test_add_del),
	KUNIT_CASE(dev_addr_test_del_main),
	KUNIT_CASE(dev_addr_test_add_set),
	KUNIT_CASE(dev_addr_test_add_excl),
	KUNIT_CASE(dev_addr_test_snapshot_sync),
	KUNIT_CASE(dev_addr_test_snapshot_remove_during_sync),
	KUNIT_CASE(dev_addr_test_snapshot_readd_during_unsync),
	KUNIT_CASE(dev_addr_test_snapshot_add_and_remove),
	KUNIT_CASE_SLOW(dev_addr_test_snapshot_benchmark),
	{}
};

static struct kunit_suite dev_addr_test_suite = {
	.name = "dev-addr-list-test",
	.test_cases = dev_addr_test_cases,
	.init = dev_addr_test_init,
	.exit = dev_addr_test_exit,
};
kunit_test_suite(dev_addr_test_suite);

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
MODULE_DESCRIPTION("KUnit tests for struct netdev_hw_addr_list");
MODULE_LICENSE("GPL");

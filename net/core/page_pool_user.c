// SPDX-License-Identifier: GPL-2.0

#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/xarray.h>
#include <net/net_debug.h>
#include <net/page_pool/types.h>

#include "page_pool_priv.h"

static DEFINE_XARRAY_FLAGS(page_pools, XA_FLAGS_ALLOC1);
/* Protects: page_pools, netdevice->page_pools, pool->slow.netdev, pool->user.
 * Ordering: inside rtnl_lock
 */
static DEFINE_MUTEX(page_pools_lock);

/* Page pools are only reachable from user space (via netlink) if they are
 * linked to a netdev at creation time. Following page pool "visibility"
 * states are possible:
 *  - normal
 *    - user.list: linked to real netdev, netdev: real netdev
 *  - orphaned - real netdev has disappeared
 *    - user.list: linked to lo, netdev: lo
 *  - invisible - either (a) created without netdev linking, (b) unlisted due
 *      to error, or (c) the entire namespace which owned this pool disappeared
 *    - user.list: unhashed, netdev: unknown
 */

int page_pool_list(struct page_pool *pool)
{
	static u32 id_alloc_next;
	int err;

	mutex_lock(&page_pools_lock);
	err = xa_alloc_cyclic(&page_pools, &pool->user.id, pool, xa_limit_32b,
			      &id_alloc_next, GFP_KERNEL);
	if (err < 0)
		goto err_unlock;

	if (pool->slow.netdev)
		hlist_add_head(&pool->user.list,
			       &pool->slow.netdev->page_pools);

	mutex_unlock(&page_pools_lock);
	return 0;

err_unlock:
	mutex_unlock(&page_pools_lock);
	return err;
}

void page_pool_unlist(struct page_pool *pool)
{
	mutex_lock(&page_pools_lock);
	xa_erase(&page_pools, pool->user.id);
	hlist_del(&pool->user.list);
	mutex_unlock(&page_pools_lock);
}

static void page_pool_unreg_netdev_wipe(struct net_device *netdev)
{
	struct page_pool *pool;
	struct hlist_node *n;

	mutex_lock(&page_pools_lock);
	hlist_for_each_entry_safe(pool, n, &netdev->page_pools, user.list) {
		hlist_del_init(&pool->user.list);
		pool->slow.netdev = NET_PTR_POISON;
	}
	mutex_unlock(&page_pools_lock);
}

static void page_pool_unreg_netdev(struct net_device *netdev)
{
	struct page_pool *pool, *last;
	struct net_device *lo;

	lo = dev_net(netdev)->loopback_dev;

	mutex_lock(&page_pools_lock);
	last = NULL;
	hlist_for_each_entry(pool, &netdev->page_pools, user.list) {
		pool->slow.netdev = lo;
		last = pool;
	}
	if (last)
		hlist_splice_init(&netdev->page_pools, &last->user.list,
				  &lo->page_pools);
	mutex_unlock(&page_pools_lock);
}

static int
page_pool_netdevice_event(struct notifier_block *nb,
			  unsigned long event, void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);

	if (event != NETDEV_UNREGISTER)
		return NOTIFY_DONE;

	if (hlist_empty(&netdev->page_pools))
		return NOTIFY_OK;

	if (netdev->ifindex != LOOPBACK_IFINDEX)
		page_pool_unreg_netdev(netdev);
	else
		page_pool_unreg_netdev_wipe(netdev);
	return NOTIFY_OK;
}

static struct notifier_block page_pool_netdevice_nb = {
	.notifier_call = page_pool_netdevice_event,
};

static int __init page_pool_user_init(void)
{
	return register_netdevice_notifier(&page_pool_netdevice_nb);
}

subsys_initcall(page_pool_user_init);

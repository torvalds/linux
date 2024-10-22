// SPDX-License-Identifier: GPL-2.0

#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/xarray.h>
#include <net/net_debug.h>
#include <net/netdev_rx_queue.h>
#include <net/page_pool/helpers.h>
#include <net/page_pool/types.h>
#include <net/sock.h>

#include "devmem.h"
#include "page_pool_priv.h"
#include "netdev-genl-gen.h"

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

typedef int (*pp_nl_fill_cb)(struct sk_buff *rsp, const struct page_pool *pool,
			     const struct genl_info *info);

static int
netdev_nl_page_pool_get_do(struct genl_info *info, u32 id, pp_nl_fill_cb fill)
{
	struct page_pool *pool;
	struct sk_buff *rsp;
	int err;

	mutex_lock(&page_pools_lock);
	pool = xa_load(&page_pools, id);
	if (!pool || hlist_unhashed(&pool->user.list) ||
	    !net_eq(dev_net(pool->slow.netdev), genl_info_net(info))) {
		err = -ENOENT;
		goto err_unlock;
	}

	rsp = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!rsp) {
		err = -ENOMEM;
		goto err_unlock;
	}

	err = fill(rsp, pool, info);
	if (err)
		goto err_free_msg;

	mutex_unlock(&page_pools_lock);

	return genlmsg_reply(rsp, info);

err_free_msg:
	nlmsg_free(rsp);
err_unlock:
	mutex_unlock(&page_pools_lock);
	return err;
}

struct page_pool_dump_cb {
	unsigned long ifindex;
	u32 pp_id;
};

static int
netdev_nl_page_pool_get_dump(struct sk_buff *skb, struct netlink_callback *cb,
			     pp_nl_fill_cb fill)
{
	struct page_pool_dump_cb *state = (void *)cb->ctx;
	const struct genl_info *info = genl_info_dump(cb);
	struct net *net = sock_net(skb->sk);
	struct net_device *netdev;
	struct page_pool *pool;
	int err = 0;

	rtnl_lock();
	mutex_lock(&page_pools_lock);
	for_each_netdev_dump(net, netdev, state->ifindex) {
		hlist_for_each_entry(pool, &netdev->page_pools, user.list) {
			if (state->pp_id && state->pp_id < pool->user.id)
				continue;

			state->pp_id = pool->user.id;
			err = fill(skb, pool, info);
			if (err)
				goto out;
		}

		state->pp_id = 0;
	}
out:
	mutex_unlock(&page_pools_lock);
	rtnl_unlock();

	return err;
}

static int
page_pool_nl_stats_fill(struct sk_buff *rsp, const struct page_pool *pool,
			const struct genl_info *info)
{
#ifdef CONFIG_PAGE_POOL_STATS
	struct page_pool_stats stats = {};
	struct nlattr *nest;
	void *hdr;

	if (!page_pool_get_stats(pool, &stats))
		return 0;

	hdr = genlmsg_iput(rsp, info);
	if (!hdr)
		return -EMSGSIZE;

	nest = nla_nest_start(rsp, NETDEV_A_PAGE_POOL_STATS_INFO);

	if (nla_put_uint(rsp, NETDEV_A_PAGE_POOL_ID, pool->user.id) ||
	    (pool->slow.netdev->ifindex != LOOPBACK_IFINDEX &&
	     nla_put_u32(rsp, NETDEV_A_PAGE_POOL_IFINDEX,
			 pool->slow.netdev->ifindex)))
		goto err_cancel_nest;

	nla_nest_end(rsp, nest);

	if (nla_put_uint(rsp, NETDEV_A_PAGE_POOL_STATS_ALLOC_FAST,
			 stats.alloc_stats.fast) ||
	    nla_put_uint(rsp, NETDEV_A_PAGE_POOL_STATS_ALLOC_SLOW,
			 stats.alloc_stats.slow) ||
	    nla_put_uint(rsp, NETDEV_A_PAGE_POOL_STATS_ALLOC_SLOW_HIGH_ORDER,
			 stats.alloc_stats.slow_high_order) ||
	    nla_put_uint(rsp, NETDEV_A_PAGE_POOL_STATS_ALLOC_EMPTY,
			 stats.alloc_stats.empty) ||
	    nla_put_uint(rsp, NETDEV_A_PAGE_POOL_STATS_ALLOC_REFILL,
			 stats.alloc_stats.refill) ||
	    nla_put_uint(rsp, NETDEV_A_PAGE_POOL_STATS_ALLOC_WAIVE,
			 stats.alloc_stats.waive) ||
	    nla_put_uint(rsp, NETDEV_A_PAGE_POOL_STATS_RECYCLE_CACHED,
			 stats.recycle_stats.cached) ||
	    nla_put_uint(rsp, NETDEV_A_PAGE_POOL_STATS_RECYCLE_CACHE_FULL,
			 stats.recycle_stats.cache_full) ||
	    nla_put_uint(rsp, NETDEV_A_PAGE_POOL_STATS_RECYCLE_RING,
			 stats.recycle_stats.ring) ||
	    nla_put_uint(rsp, NETDEV_A_PAGE_POOL_STATS_RECYCLE_RING_FULL,
			 stats.recycle_stats.ring_full) ||
	    nla_put_uint(rsp, NETDEV_A_PAGE_POOL_STATS_RECYCLE_RELEASED_REFCNT,
			 stats.recycle_stats.released_refcnt))
		goto err_cancel_msg;

	genlmsg_end(rsp, hdr);

	return 0;
err_cancel_nest:
	nla_nest_cancel(rsp, nest);
err_cancel_msg:
	genlmsg_cancel(rsp, hdr);
	return -EMSGSIZE;
#else
	GENL_SET_ERR_MSG(info, "kernel built without CONFIG_PAGE_POOL_STATS");
	return -EOPNOTSUPP;
#endif
}

int netdev_nl_page_pool_stats_get_doit(struct sk_buff *skb,
				       struct genl_info *info)
{
	struct nlattr *tb[ARRAY_SIZE(netdev_page_pool_info_nl_policy)];
	struct nlattr *nest;
	int err;
	u32 id;

	if (GENL_REQ_ATTR_CHECK(info, NETDEV_A_PAGE_POOL_STATS_INFO))
		return -EINVAL;

	nest = info->attrs[NETDEV_A_PAGE_POOL_STATS_INFO];
	err = nla_parse_nested(tb, ARRAY_SIZE(tb) - 1, nest,
			       netdev_page_pool_info_nl_policy,
			       info->extack);
	if (err)
		return err;

	if (NL_REQ_ATTR_CHECK(info->extack, nest, tb, NETDEV_A_PAGE_POOL_ID))
		return -EINVAL;
	if (tb[NETDEV_A_PAGE_POOL_IFINDEX]) {
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[NETDEV_A_PAGE_POOL_IFINDEX],
				    "selecting by ifindex not supported");
		return -EINVAL;
	}

	id = nla_get_uint(tb[NETDEV_A_PAGE_POOL_ID]);

	return netdev_nl_page_pool_get_do(info, id, page_pool_nl_stats_fill);
}

int netdev_nl_page_pool_stats_get_dumpit(struct sk_buff *skb,
					 struct netlink_callback *cb)
{
	return netdev_nl_page_pool_get_dump(skb, cb, page_pool_nl_stats_fill);
}

static int
page_pool_nl_fill(struct sk_buff *rsp, const struct page_pool *pool,
		  const struct genl_info *info)
{
	struct net_devmem_dmabuf_binding *binding = pool->mp_priv;
	size_t inflight, refsz;
	void *hdr;

	hdr = genlmsg_iput(rsp, info);
	if (!hdr)
		return -EMSGSIZE;

	if (nla_put_uint(rsp, NETDEV_A_PAGE_POOL_ID, pool->user.id))
		goto err_cancel;

	if (pool->slow.netdev->ifindex != LOOPBACK_IFINDEX &&
	    nla_put_u32(rsp, NETDEV_A_PAGE_POOL_IFINDEX,
			pool->slow.netdev->ifindex))
		goto err_cancel;
	if (pool->user.napi_id &&
	    nla_put_uint(rsp, NETDEV_A_PAGE_POOL_NAPI_ID, pool->user.napi_id))
		goto err_cancel;

	inflight = page_pool_inflight(pool, false);
	refsz =	PAGE_SIZE << pool->p.order;
	if (nla_put_uint(rsp, NETDEV_A_PAGE_POOL_INFLIGHT, inflight) ||
	    nla_put_uint(rsp, NETDEV_A_PAGE_POOL_INFLIGHT_MEM,
			 inflight * refsz))
		goto err_cancel;
	if (pool->user.detach_time &&
	    nla_put_uint(rsp, NETDEV_A_PAGE_POOL_DETACH_TIME,
			 pool->user.detach_time))
		goto err_cancel;

	if (binding && nla_put_u32(rsp, NETDEV_A_PAGE_POOL_DMABUF, binding->id))
		goto err_cancel;

	genlmsg_end(rsp, hdr);

	return 0;
err_cancel:
	genlmsg_cancel(rsp, hdr);
	return -EMSGSIZE;
}

static void netdev_nl_page_pool_event(const struct page_pool *pool, u32 cmd)
{
	struct genl_info info;
	struct sk_buff *ntf;
	struct net *net;

	lockdep_assert_held(&page_pools_lock);

	/* 'invisible' page pools don't matter */
	if (hlist_unhashed(&pool->user.list))
		return;
	net = dev_net(pool->slow.netdev);

	if (!genl_has_listeners(&netdev_nl_family, net, NETDEV_NLGRP_PAGE_POOL))
		return;

	genl_info_init_ntf(&info, &netdev_nl_family, cmd);

	ntf = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!ntf)
		return;

	if (page_pool_nl_fill(ntf, pool, &info)) {
		nlmsg_free(ntf);
		return;
	}

	genlmsg_multicast_netns(&netdev_nl_family, net, ntf,
				0, NETDEV_NLGRP_PAGE_POOL, GFP_KERNEL);
}

int netdev_nl_page_pool_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	u32 id;

	if (GENL_REQ_ATTR_CHECK(info, NETDEV_A_PAGE_POOL_ID))
		return -EINVAL;

	id = nla_get_uint(info->attrs[NETDEV_A_PAGE_POOL_ID]);

	return netdev_nl_page_pool_get_do(info, id, page_pool_nl_fill);
}

int netdev_nl_page_pool_get_dumpit(struct sk_buff *skb,
				   struct netlink_callback *cb)
{
	return netdev_nl_page_pool_get_dump(skb, cb, page_pool_nl_fill);
}

int page_pool_list(struct page_pool *pool)
{
	static u32 id_alloc_next;
	int err;

	mutex_lock(&page_pools_lock);
	err = xa_alloc_cyclic(&page_pools, &pool->user.id, pool, xa_limit_32b,
			      &id_alloc_next, GFP_KERNEL);
	if (err < 0)
		goto err_unlock;

	INIT_HLIST_NODE(&pool->user.list);
	if (pool->slow.netdev) {
		hlist_add_head(&pool->user.list,
			       &pool->slow.netdev->page_pools);
		pool->user.napi_id = pool->p.napi ? pool->p.napi->napi_id : 0;

		netdev_nl_page_pool_event(pool, NETDEV_CMD_PAGE_POOL_ADD_NTF);
	}

	mutex_unlock(&page_pools_lock);
	return 0;

err_unlock:
	mutex_unlock(&page_pools_lock);
	return err;
}

void page_pool_detached(struct page_pool *pool)
{
	mutex_lock(&page_pools_lock);
	pool->user.detach_time = ktime_get_boottime_seconds();
	netdev_nl_page_pool_event(pool, NETDEV_CMD_PAGE_POOL_CHANGE_NTF);
	mutex_unlock(&page_pools_lock);
}

void page_pool_unlist(struct page_pool *pool)
{
	mutex_lock(&page_pools_lock);
	netdev_nl_page_pool_event(pool, NETDEV_CMD_PAGE_POOL_DEL_NTF);
	xa_erase(&page_pools, pool->user.id);
	if (!hlist_unhashed(&pool->user.list))
		hlist_del(&pool->user.list);
	mutex_unlock(&page_pools_lock);
}

int page_pool_check_memory_provider(struct net_device *dev,
				    struct netdev_rx_queue *rxq)
{
	struct net_devmem_dmabuf_binding *binding = rxq->mp_params.mp_priv;
	struct page_pool *pool;
	struct hlist_node *n;

	if (!binding)
		return 0;

	mutex_lock(&page_pools_lock);
	hlist_for_each_entry_safe(pool, n, &dev->page_pools, user.list) {
		if (pool->mp_priv != binding)
			continue;

		if (pool->slow.queue_idx == get_netdev_rx_queue_index(rxq)) {
			mutex_unlock(&page_pools_lock);
			return 0;
		}
	}
	mutex_unlock(&page_pools_lock);
	return -ENODATA;
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
		netdev_nl_page_pool_event(pool,
					  NETDEV_CMD_PAGE_POOL_CHANGE_NTF);
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

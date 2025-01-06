// SPDX-License-Identifier: GPL-2.0-only

#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/rtnetlink.h>
#include <net/busy_poll.h>
#include <net/net_namespace.h>
#include <net/netdev_queues.h>
#include <net/netdev_rx_queue.h>
#include <net/sock.h>
#include <net/xdp.h>
#include <net/xdp_sock.h>

#include "dev.h"
#include "devmem.h"
#include "netdev-genl-gen.h"

struct netdev_nl_dump_ctx {
	unsigned long	ifindex;
	unsigned int	rxq_idx;
	unsigned int	txq_idx;
	unsigned int	napi_id;
};

static struct netdev_nl_dump_ctx *netdev_dump_ctx(struct netlink_callback *cb)
{
	NL_ASSERT_DUMP_CTX_FITS(struct netdev_nl_dump_ctx);

	return (struct netdev_nl_dump_ctx *)cb->ctx;
}

static int
netdev_nl_dev_fill(struct net_device *netdev, struct sk_buff *rsp,
		   const struct genl_info *info)
{
	u64 xsk_features = 0;
	u64 xdp_rx_meta = 0;
	void *hdr;

	hdr = genlmsg_iput(rsp, info);
	if (!hdr)
		return -EMSGSIZE;

#define XDP_METADATA_KFUNC(_, flag, __, xmo) \
	if (netdev->xdp_metadata_ops && netdev->xdp_metadata_ops->xmo) \
		xdp_rx_meta |= flag;
XDP_METADATA_KFUNC_xxx
#undef XDP_METADATA_KFUNC

	if (netdev->xsk_tx_metadata_ops) {
		if (netdev->xsk_tx_metadata_ops->tmo_fill_timestamp)
			xsk_features |= NETDEV_XSK_FLAGS_TX_TIMESTAMP;
		if (netdev->xsk_tx_metadata_ops->tmo_request_checksum)
			xsk_features |= NETDEV_XSK_FLAGS_TX_CHECKSUM;
	}

	if (nla_put_u32(rsp, NETDEV_A_DEV_IFINDEX, netdev->ifindex) ||
	    nla_put_u64_64bit(rsp, NETDEV_A_DEV_XDP_FEATURES,
			      netdev->xdp_features, NETDEV_A_DEV_PAD) ||
	    nla_put_u64_64bit(rsp, NETDEV_A_DEV_XDP_RX_METADATA_FEATURES,
			      xdp_rx_meta, NETDEV_A_DEV_PAD) ||
	    nla_put_u64_64bit(rsp, NETDEV_A_DEV_XSK_FEATURES,
			      xsk_features, NETDEV_A_DEV_PAD))
		goto err_cancel_msg;

	if (netdev->xdp_features & NETDEV_XDP_ACT_XSK_ZEROCOPY) {
		if (nla_put_u32(rsp, NETDEV_A_DEV_XDP_ZC_MAX_SEGS,
				netdev->xdp_zc_max_segs))
			goto err_cancel_msg;
	}

	genlmsg_end(rsp, hdr);

	return 0;

err_cancel_msg:
	genlmsg_cancel(rsp, hdr);
	return -EMSGSIZE;
}

static void
netdev_genl_dev_notify(struct net_device *netdev, int cmd)
{
	struct genl_info info;
	struct sk_buff *ntf;

	if (!genl_has_listeners(&netdev_nl_family, dev_net(netdev),
				NETDEV_NLGRP_MGMT))
		return;

	genl_info_init_ntf(&info, &netdev_nl_family, cmd);

	ntf = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!ntf)
		return;

	if (netdev_nl_dev_fill(netdev, ntf, &info)) {
		nlmsg_free(ntf);
		return;
	}

	genlmsg_multicast_netns(&netdev_nl_family, dev_net(netdev), ntf,
				0, NETDEV_NLGRP_MGMT, GFP_KERNEL);
}

int netdev_nl_dev_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *netdev;
	struct sk_buff *rsp;
	u32 ifindex;
	int err;

	if (GENL_REQ_ATTR_CHECK(info, NETDEV_A_DEV_IFINDEX))
		return -EINVAL;

	ifindex = nla_get_u32(info->attrs[NETDEV_A_DEV_IFINDEX]);

	rsp = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!rsp)
		return -ENOMEM;

	rtnl_lock();

	netdev = __dev_get_by_index(genl_info_net(info), ifindex);
	if (netdev)
		err = netdev_nl_dev_fill(netdev, rsp, info);
	else
		err = -ENODEV;

	rtnl_unlock();

	if (err)
		goto err_free_msg;

	return genlmsg_reply(rsp, info);

err_free_msg:
	nlmsg_free(rsp);
	return err;
}

int netdev_nl_dev_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct netdev_nl_dump_ctx *ctx = netdev_dump_ctx(cb);
	struct net *net = sock_net(skb->sk);
	struct net_device *netdev;
	int err = 0;

	rtnl_lock();
	for_each_netdev_dump(net, netdev, ctx->ifindex) {
		err = netdev_nl_dev_fill(netdev, skb, genl_info_dump(cb));
		if (err < 0)
			break;
	}
	rtnl_unlock();

	return err;
}

static int
netdev_nl_napi_fill_one(struct sk_buff *rsp, struct napi_struct *napi,
			const struct genl_info *info)
{
	void *hdr;
	pid_t pid;

	if (!(napi->dev->flags & IFF_UP))
		return 0;

	hdr = genlmsg_iput(rsp, info);
	if (!hdr)
		return -EMSGSIZE;

	if (nla_put_u32(rsp, NETDEV_A_NAPI_ID, napi->napi_id))
		goto nla_put_failure;

	if (nla_put_u32(rsp, NETDEV_A_NAPI_IFINDEX, napi->dev->ifindex))
		goto nla_put_failure;

	if (napi->irq >= 0 && nla_put_u32(rsp, NETDEV_A_NAPI_IRQ, napi->irq))
		goto nla_put_failure;

	if (napi->thread) {
		pid = task_pid_nr(napi->thread);
		if (nla_put_u32(rsp, NETDEV_A_NAPI_PID, pid))
			goto nla_put_failure;
	}

	genlmsg_end(rsp, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(rsp, hdr);
	return -EMSGSIZE;
}

int netdev_nl_napi_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct napi_struct *napi;
	struct sk_buff *rsp;
	u32 napi_id;
	int err;

	if (GENL_REQ_ATTR_CHECK(info, NETDEV_A_NAPI_ID))
		return -EINVAL;

	napi_id = nla_get_u32(info->attrs[NETDEV_A_NAPI_ID]);

	rsp = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!rsp)
		return -ENOMEM;

	rtnl_lock();
	rcu_read_lock();

	napi = netdev_napi_by_id(genl_info_net(info), napi_id);
	if (napi) {
		err = netdev_nl_napi_fill_one(rsp, napi, info);
	} else {
		NL_SET_BAD_ATTR(info->extack, info->attrs[NETDEV_A_NAPI_ID]);
		err = -ENOENT;
	}

	rcu_read_unlock();
	rtnl_unlock();

	if (err) {
		goto err_free_msg;
	} else if (!rsp->len) {
		err = -ENOENT;
		goto err_free_msg;
	}

	return genlmsg_reply(rsp, info);

err_free_msg:
	nlmsg_free(rsp);
	return err;
}

static int
netdev_nl_napi_dump_one(struct net_device *netdev, struct sk_buff *rsp,
			const struct genl_info *info,
			struct netdev_nl_dump_ctx *ctx)
{
	struct napi_struct *napi;
	int err = 0;

	if (!(netdev->flags & IFF_UP))
		return err;

	list_for_each_entry(napi, &netdev->napi_list, dev_list) {
		if (napi->napi_id < MIN_NAPI_ID)
			continue;
		if (ctx->napi_id && napi->napi_id >= ctx->napi_id)
			continue;

		err = netdev_nl_napi_fill_one(rsp, napi, info);
		if (err)
			return err;
		ctx->napi_id = napi->napi_id;
	}
	return err;
}

int netdev_nl_napi_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct netdev_nl_dump_ctx *ctx = netdev_dump_ctx(cb);
	const struct genl_info *info = genl_info_dump(cb);
	struct net *net = sock_net(skb->sk);
	struct net_device *netdev;
	u32 ifindex = 0;
	int err = 0;

	if (info->attrs[NETDEV_A_NAPI_IFINDEX])
		ifindex = nla_get_u32(info->attrs[NETDEV_A_NAPI_IFINDEX]);

	rtnl_lock();
	if (ifindex) {
		netdev = __dev_get_by_index(net, ifindex);
		if (netdev)
			err = netdev_nl_napi_dump_one(netdev, skb, info, ctx);
		else
			err = -ENODEV;
	} else {
		for_each_netdev_dump(net, netdev, ctx->ifindex) {
			err = netdev_nl_napi_dump_one(netdev, skb, info, ctx);
			if (err < 0)
				break;
			ctx->napi_id = 0;
		}
	}
	rtnl_unlock();

	return err;
}

static int
netdev_nl_queue_fill_one(struct sk_buff *rsp, struct net_device *netdev,
			 u32 q_idx, u32 q_type, const struct genl_info *info)
{
	struct net_devmem_dmabuf_binding *binding;
	struct netdev_rx_queue *rxq;
	struct netdev_queue *txq;
	void *hdr;

	hdr = genlmsg_iput(rsp, info);
	if (!hdr)
		return -EMSGSIZE;

	if (nla_put_u32(rsp, NETDEV_A_QUEUE_ID, q_idx) ||
	    nla_put_u32(rsp, NETDEV_A_QUEUE_TYPE, q_type) ||
	    nla_put_u32(rsp, NETDEV_A_QUEUE_IFINDEX, netdev->ifindex))
		goto nla_put_failure;

	switch (q_type) {
	case NETDEV_QUEUE_TYPE_RX:
		rxq = __netif_get_rx_queue(netdev, q_idx);
		if (rxq->napi && nla_put_u32(rsp, NETDEV_A_QUEUE_NAPI_ID,
					     rxq->napi->napi_id))
			goto nla_put_failure;

		binding = rxq->mp_params.mp_priv;
		if (binding &&
		    nla_put_u32(rsp, NETDEV_A_QUEUE_DMABUF, binding->id))
			goto nla_put_failure;

		break;
	case NETDEV_QUEUE_TYPE_TX:
		txq = netdev_get_tx_queue(netdev, q_idx);
		if (txq->napi && nla_put_u32(rsp, NETDEV_A_QUEUE_NAPI_ID,
					     txq->napi->napi_id))
			goto nla_put_failure;
	}

	genlmsg_end(rsp, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(rsp, hdr);
	return -EMSGSIZE;
}

static int netdev_nl_queue_validate(struct net_device *netdev, u32 q_id,
				    u32 q_type)
{
	switch (q_type) {
	case NETDEV_QUEUE_TYPE_RX:
		if (q_id >= netdev->real_num_rx_queues)
			return -EINVAL;
		return 0;
	case NETDEV_QUEUE_TYPE_TX:
		if (q_id >= netdev->real_num_tx_queues)
			return -EINVAL;
	}
	return 0;
}

static int
netdev_nl_queue_fill(struct sk_buff *rsp, struct net_device *netdev, u32 q_idx,
		     u32 q_type, const struct genl_info *info)
{
	int err;

	if (!(netdev->flags & IFF_UP))
		return -ENOENT;

	err = netdev_nl_queue_validate(netdev, q_idx, q_type);
	if (err)
		return err;

	return netdev_nl_queue_fill_one(rsp, netdev, q_idx, q_type, info);
}

int netdev_nl_queue_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	u32 q_id, q_type, ifindex;
	struct net_device *netdev;
	struct sk_buff *rsp;
	int err;

	if (GENL_REQ_ATTR_CHECK(info, NETDEV_A_QUEUE_ID) ||
	    GENL_REQ_ATTR_CHECK(info, NETDEV_A_QUEUE_TYPE) ||
	    GENL_REQ_ATTR_CHECK(info, NETDEV_A_QUEUE_IFINDEX))
		return -EINVAL;

	q_id = nla_get_u32(info->attrs[NETDEV_A_QUEUE_ID]);
	q_type = nla_get_u32(info->attrs[NETDEV_A_QUEUE_TYPE]);
	ifindex = nla_get_u32(info->attrs[NETDEV_A_QUEUE_IFINDEX]);

	rsp = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!rsp)
		return -ENOMEM;

	rtnl_lock();

	netdev = __dev_get_by_index(genl_info_net(info), ifindex);
	if (netdev)
		err = netdev_nl_queue_fill(rsp, netdev, q_id, q_type, info);
	else
		err = -ENODEV;

	rtnl_unlock();

	if (err)
		goto err_free_msg;

	return genlmsg_reply(rsp, info);

err_free_msg:
	nlmsg_free(rsp);
	return err;
}

static int
netdev_nl_queue_dump_one(struct net_device *netdev, struct sk_buff *rsp,
			 const struct genl_info *info,
			 struct netdev_nl_dump_ctx *ctx)
{
	int err = 0;

	if (!(netdev->flags & IFF_UP))
		return err;

	for (; ctx->rxq_idx < netdev->real_num_rx_queues; ctx->rxq_idx++) {
		err = netdev_nl_queue_fill_one(rsp, netdev, ctx->rxq_idx,
					       NETDEV_QUEUE_TYPE_RX, info);
		if (err)
			return err;
	}
	for (; ctx->txq_idx < netdev->real_num_tx_queues; ctx->txq_idx++) {
		err = netdev_nl_queue_fill_one(rsp, netdev, ctx->txq_idx,
					       NETDEV_QUEUE_TYPE_TX, info);
		if (err)
			return err;
	}

	return err;
}

int netdev_nl_queue_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct netdev_nl_dump_ctx *ctx = netdev_dump_ctx(cb);
	const struct genl_info *info = genl_info_dump(cb);
	struct net *net = sock_net(skb->sk);
	struct net_device *netdev;
	u32 ifindex = 0;
	int err = 0;

	if (info->attrs[NETDEV_A_QUEUE_IFINDEX])
		ifindex = nla_get_u32(info->attrs[NETDEV_A_QUEUE_IFINDEX]);

	rtnl_lock();
	if (ifindex) {
		netdev = __dev_get_by_index(net, ifindex);
		if (netdev)
			err = netdev_nl_queue_dump_one(netdev, skb, info, ctx);
		else
			err = -ENODEV;
	} else {
		for_each_netdev_dump(net, netdev, ctx->ifindex) {
			err = netdev_nl_queue_dump_one(netdev, skb, info, ctx);
			if (err < 0)
				break;
			ctx->rxq_idx = 0;
			ctx->txq_idx = 0;
		}
	}
	rtnl_unlock();

	return err;
}

#define NETDEV_STAT_NOT_SET		(~0ULL)

static void netdev_nl_stats_add(void *_sum, const void *_add, size_t size)
{
	const u64 *add = _add;
	u64 *sum = _sum;

	while (size) {
		if (*add != NETDEV_STAT_NOT_SET && *sum != NETDEV_STAT_NOT_SET)
			*sum += *add;
		sum++;
		add++;
		size -= 8;
	}
}

static int netdev_stat_put(struct sk_buff *rsp, unsigned int attr_id, u64 value)
{
	if (value == NETDEV_STAT_NOT_SET)
		return 0;
	return nla_put_uint(rsp, attr_id, value);
}

static int
netdev_nl_stats_write_rx(struct sk_buff *rsp, struct netdev_queue_stats_rx *rx)
{
	if (netdev_stat_put(rsp, NETDEV_A_QSTATS_RX_PACKETS, rx->packets) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_RX_BYTES, rx->bytes) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_RX_ALLOC_FAIL, rx->alloc_fail) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_RX_HW_DROPS, rx->hw_drops) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_RX_HW_DROP_OVERRUNS, rx->hw_drop_overruns) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_RX_CSUM_UNNECESSARY, rx->csum_unnecessary) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_RX_CSUM_NONE, rx->csum_none) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_RX_CSUM_BAD, rx->csum_bad) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_RX_HW_GRO_PACKETS, rx->hw_gro_packets) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_RX_HW_GRO_BYTES, rx->hw_gro_bytes) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_RX_HW_GRO_WIRE_PACKETS, rx->hw_gro_wire_packets) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_RX_HW_GRO_WIRE_BYTES, rx->hw_gro_wire_bytes) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_RX_HW_DROP_RATELIMITS, rx->hw_drop_ratelimits))
		return -EMSGSIZE;
	return 0;
}

static int
netdev_nl_stats_write_tx(struct sk_buff *rsp, struct netdev_queue_stats_tx *tx)
{
	if (netdev_stat_put(rsp, NETDEV_A_QSTATS_TX_PACKETS, tx->packets) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_TX_BYTES, tx->bytes) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_TX_HW_DROPS, tx->hw_drops) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_TX_HW_DROP_ERRORS, tx->hw_drop_errors) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_TX_CSUM_NONE, tx->csum_none) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_TX_NEEDS_CSUM, tx->needs_csum) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_TX_HW_GSO_PACKETS, tx->hw_gso_packets) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_TX_HW_GSO_BYTES, tx->hw_gso_bytes) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_TX_HW_GSO_WIRE_PACKETS, tx->hw_gso_wire_packets) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_TX_HW_GSO_WIRE_BYTES, tx->hw_gso_wire_bytes) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_TX_HW_DROP_RATELIMITS, tx->hw_drop_ratelimits) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_TX_STOP, tx->stop) ||
	    netdev_stat_put(rsp, NETDEV_A_QSTATS_TX_WAKE, tx->wake))
		return -EMSGSIZE;
	return 0;
}

static int
netdev_nl_stats_queue(struct net_device *netdev, struct sk_buff *rsp,
		      u32 q_type, int i, const struct genl_info *info)
{
	const struct netdev_stat_ops *ops = netdev->stat_ops;
	struct netdev_queue_stats_rx rx;
	struct netdev_queue_stats_tx tx;
	void *hdr;

	hdr = genlmsg_iput(rsp, info);
	if (!hdr)
		return -EMSGSIZE;
	if (nla_put_u32(rsp, NETDEV_A_QSTATS_IFINDEX, netdev->ifindex) ||
	    nla_put_u32(rsp, NETDEV_A_QSTATS_QUEUE_TYPE, q_type) ||
	    nla_put_u32(rsp, NETDEV_A_QSTATS_QUEUE_ID, i))
		goto nla_put_failure;

	switch (q_type) {
	case NETDEV_QUEUE_TYPE_RX:
		memset(&rx, 0xff, sizeof(rx));
		ops->get_queue_stats_rx(netdev, i, &rx);
		if (!memchr_inv(&rx, 0xff, sizeof(rx)))
			goto nla_cancel;
		if (netdev_nl_stats_write_rx(rsp, &rx))
			goto nla_put_failure;
		break;
	case NETDEV_QUEUE_TYPE_TX:
		memset(&tx, 0xff, sizeof(tx));
		ops->get_queue_stats_tx(netdev, i, &tx);
		if (!memchr_inv(&tx, 0xff, sizeof(tx)))
			goto nla_cancel;
		if (netdev_nl_stats_write_tx(rsp, &tx))
			goto nla_put_failure;
		break;
	}

	genlmsg_end(rsp, hdr);
	return 0;

nla_cancel:
	genlmsg_cancel(rsp, hdr);
	return 0;
nla_put_failure:
	genlmsg_cancel(rsp, hdr);
	return -EMSGSIZE;
}

static int
netdev_nl_stats_by_queue(struct net_device *netdev, struct sk_buff *rsp,
			 const struct genl_info *info,
			 struct netdev_nl_dump_ctx *ctx)
{
	const struct netdev_stat_ops *ops = netdev->stat_ops;
	int i, err;

	if (!(netdev->flags & IFF_UP))
		return 0;

	i = ctx->rxq_idx;
	while (ops->get_queue_stats_rx && i < netdev->real_num_rx_queues) {
		err = netdev_nl_stats_queue(netdev, rsp, NETDEV_QUEUE_TYPE_RX,
					    i, info);
		if (err)
			return err;
		ctx->rxq_idx = ++i;
	}
	i = ctx->txq_idx;
	while (ops->get_queue_stats_tx && i < netdev->real_num_tx_queues) {
		err = netdev_nl_stats_queue(netdev, rsp, NETDEV_QUEUE_TYPE_TX,
					    i, info);
		if (err)
			return err;
		ctx->txq_idx = ++i;
	}

	ctx->rxq_idx = 0;
	ctx->txq_idx = 0;
	return 0;
}

static int
netdev_nl_stats_by_netdev(struct net_device *netdev, struct sk_buff *rsp,
			  const struct genl_info *info)
{
	struct netdev_queue_stats_rx rx_sum, rx;
	struct netdev_queue_stats_tx tx_sum, tx;
	const struct netdev_stat_ops *ops;
	void *hdr;
	int i;

	ops = netdev->stat_ops;
	/* Netdev can't guarantee any complete counters */
	if (!ops->get_base_stats)
		return 0;

	memset(&rx_sum, 0xff, sizeof(rx_sum));
	memset(&tx_sum, 0xff, sizeof(tx_sum));

	ops->get_base_stats(netdev, &rx_sum, &tx_sum);

	/* The op was there, but nothing reported, don't bother */
	if (!memchr_inv(&rx_sum, 0xff, sizeof(rx_sum)) &&
	    !memchr_inv(&tx_sum, 0xff, sizeof(tx_sum)))
		return 0;

	hdr = genlmsg_iput(rsp, info);
	if (!hdr)
		return -EMSGSIZE;
	if (nla_put_u32(rsp, NETDEV_A_QSTATS_IFINDEX, netdev->ifindex))
		goto nla_put_failure;

	for (i = 0; i < netdev->real_num_rx_queues; i++) {
		memset(&rx, 0xff, sizeof(rx));
		if (ops->get_queue_stats_rx)
			ops->get_queue_stats_rx(netdev, i, &rx);
		netdev_nl_stats_add(&rx_sum, &rx, sizeof(rx));
	}
	for (i = 0; i < netdev->real_num_tx_queues; i++) {
		memset(&tx, 0xff, sizeof(tx));
		if (ops->get_queue_stats_tx)
			ops->get_queue_stats_tx(netdev, i, &tx);
		netdev_nl_stats_add(&tx_sum, &tx, sizeof(tx));
	}

	if (netdev_nl_stats_write_rx(rsp, &rx_sum) ||
	    netdev_nl_stats_write_tx(rsp, &tx_sum))
		goto nla_put_failure;

	genlmsg_end(rsp, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(rsp, hdr);
	return -EMSGSIZE;
}

static int
netdev_nl_qstats_get_dump_one(struct net_device *netdev, unsigned int scope,
			      struct sk_buff *skb, const struct genl_info *info,
			      struct netdev_nl_dump_ctx *ctx)
{
	if (!netdev->stat_ops)
		return 0;

	switch (scope) {
	case 0:
		return netdev_nl_stats_by_netdev(netdev, skb, info);
	case NETDEV_QSTATS_SCOPE_QUEUE:
		return netdev_nl_stats_by_queue(netdev, skb, info, ctx);
	}

	return -EINVAL;	/* Should not happen, per netlink policy */
}

int netdev_nl_qstats_get_dumpit(struct sk_buff *skb,
				struct netlink_callback *cb)
{
	struct netdev_nl_dump_ctx *ctx = netdev_dump_ctx(cb);
	const struct genl_info *info = genl_info_dump(cb);
	struct net *net = sock_net(skb->sk);
	struct net_device *netdev;
	unsigned int ifindex;
	unsigned int scope;
	int err = 0;

	scope = 0;
	if (info->attrs[NETDEV_A_QSTATS_SCOPE])
		scope = nla_get_uint(info->attrs[NETDEV_A_QSTATS_SCOPE]);

	ifindex = 0;
	if (info->attrs[NETDEV_A_QSTATS_IFINDEX])
		ifindex = nla_get_u32(info->attrs[NETDEV_A_QSTATS_IFINDEX]);

	rtnl_lock();
	if (ifindex) {
		netdev = __dev_get_by_index(net, ifindex);
		if (netdev && netdev->stat_ops) {
			err = netdev_nl_qstats_get_dump_one(netdev, scope, skb,
							    info, ctx);
		} else {
			NL_SET_BAD_ATTR(info->extack,
					info->attrs[NETDEV_A_QSTATS_IFINDEX]);
			err = netdev ? -EOPNOTSUPP : -ENODEV;
		}
	} else {
		for_each_netdev_dump(net, netdev, ctx->ifindex) {
			err = netdev_nl_qstats_get_dump_one(netdev, scope, skb,
							    info, ctx);
			if (err < 0)
				break;
		}
	}
	rtnl_unlock();

	return err;
}

int netdev_nl_bind_rx_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *tb[ARRAY_SIZE(netdev_queue_id_nl_policy)];
	struct net_devmem_dmabuf_binding *binding;
	struct list_head *sock_binding_list;
	u32 ifindex, dmabuf_fd, rxq_idx;
	struct net_device *netdev;
	struct sk_buff *rsp;
	struct nlattr *attr;
	int rem, err = 0;
	void *hdr;

	if (GENL_REQ_ATTR_CHECK(info, NETDEV_A_DEV_IFINDEX) ||
	    GENL_REQ_ATTR_CHECK(info, NETDEV_A_DMABUF_FD) ||
	    GENL_REQ_ATTR_CHECK(info, NETDEV_A_DMABUF_QUEUES))
		return -EINVAL;

	ifindex = nla_get_u32(info->attrs[NETDEV_A_DEV_IFINDEX]);
	dmabuf_fd = nla_get_u32(info->attrs[NETDEV_A_DMABUF_FD]);

	sock_binding_list = genl_sk_priv_get(&netdev_nl_family,
					     NETLINK_CB(skb).sk);
	if (IS_ERR(sock_binding_list))
		return PTR_ERR(sock_binding_list);

	rsp = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!rsp)
		return -ENOMEM;

	hdr = genlmsg_iput(rsp, info);
	if (!hdr) {
		err = -EMSGSIZE;
		goto err_genlmsg_free;
	}

	rtnl_lock();

	netdev = __dev_get_by_index(genl_info_net(info), ifindex);
	if (!netdev || !netif_device_present(netdev)) {
		err = -ENODEV;
		goto err_unlock;
	}

	if (dev_xdp_prog_count(netdev)) {
		NL_SET_ERR_MSG(info->extack, "unable to bind dmabuf to device with XDP program attached");
		err = -EEXIST;
		goto err_unlock;
	}

	binding = net_devmem_bind_dmabuf(netdev, dmabuf_fd, info->extack);
	if (IS_ERR(binding)) {
		err = PTR_ERR(binding);
		goto err_unlock;
	}

	nla_for_each_attr_type(attr, NETDEV_A_DMABUF_QUEUES,
			       genlmsg_data(info->genlhdr),
			       genlmsg_len(info->genlhdr), rem) {
		err = nla_parse_nested(
			tb, ARRAY_SIZE(netdev_queue_id_nl_policy) - 1, attr,
			netdev_queue_id_nl_policy, info->extack);
		if (err < 0)
			goto err_unbind;

		if (NL_REQ_ATTR_CHECK(info->extack, attr, tb, NETDEV_A_QUEUE_ID) ||
		    NL_REQ_ATTR_CHECK(info->extack, attr, tb, NETDEV_A_QUEUE_TYPE)) {
			err = -EINVAL;
			goto err_unbind;
		}

		if (nla_get_u32(tb[NETDEV_A_QUEUE_TYPE]) != NETDEV_QUEUE_TYPE_RX) {
			NL_SET_BAD_ATTR(info->extack, tb[NETDEV_A_QUEUE_TYPE]);
			err = -EINVAL;
			goto err_unbind;
		}

		rxq_idx = nla_get_u32(tb[NETDEV_A_QUEUE_ID]);

		err = net_devmem_bind_dmabuf_to_queue(netdev, rxq_idx, binding,
						      info->extack);
		if (err)
			goto err_unbind;
	}

	list_add(&binding->list, sock_binding_list);

	nla_put_u32(rsp, NETDEV_A_DMABUF_ID, binding->id);
	genlmsg_end(rsp, hdr);

	err = genlmsg_reply(rsp, info);
	if (err)
		goto err_unbind;

	rtnl_unlock();

	return 0;

err_unbind:
	net_devmem_unbind_dmabuf(binding);
err_unlock:
	rtnl_unlock();
err_genlmsg_free:
	nlmsg_free(rsp);
	return err;
}

void netdev_nl_sock_priv_init(struct list_head *priv)
{
	INIT_LIST_HEAD(priv);
}

void netdev_nl_sock_priv_destroy(struct list_head *priv)
{
	struct net_devmem_dmabuf_binding *binding;
	struct net_devmem_dmabuf_binding *temp;

	list_for_each_entry_safe(binding, temp, priv, list) {
		rtnl_lock();
		net_devmem_unbind_dmabuf(binding);
		rtnl_unlock();
	}
}

static int netdev_genl_netdevice_event(struct notifier_block *nb,
				       unsigned long event, void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);

	switch (event) {
	case NETDEV_REGISTER:
		netdev_genl_dev_notify(netdev, NETDEV_CMD_DEV_ADD_NTF);
		break;
	case NETDEV_UNREGISTER:
		netdev_genl_dev_notify(netdev, NETDEV_CMD_DEV_DEL_NTF);
		break;
	case NETDEV_XDP_FEAT_CHANGE:
		netdev_genl_dev_notify(netdev, NETDEV_CMD_DEV_CHANGE_NTF);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block netdev_genl_nb = {
	.notifier_call	= netdev_genl_netdevice_event,
};

static int __init netdev_genl_init(void)
{
	int err;

	err = register_netdevice_notifier(&netdev_genl_nb);
	if (err)
		return err;

	err = genl_register_family(&netdev_nl_family);
	if (err)
		goto err_unreg_ntf;

	return 0;

err_unreg_ntf:
	unregister_netdevice_notifier(&netdev_genl_nb);
	return err;
}

subsys_initcall(netdev_genl_init);

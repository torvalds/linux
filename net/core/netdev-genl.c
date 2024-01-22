// SPDX-License-Identifier: GPL-2.0-only

#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/rtnetlink.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/xdp.h>
#include <net/xdp_sock.h>
#include <net/netdev_rx_queue.h>
#include <net/busy_poll.h>

#include "netdev-genl-gen.h"
#include "dev.h"

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
			      xsk_features, NETDEV_A_DEV_PAD)) {
		genlmsg_cancel(rsp, hdr);
		return -EINVAL;
	}

	if (netdev->xdp_features & NETDEV_XDP_ACT_XSK_ZEROCOPY) {
		if (nla_put_u32(rsp, NETDEV_A_DEV_XDP_ZC_MAX_SEGS,
				netdev->xdp_zc_max_segs)) {
			genlmsg_cancel(rsp, hdr);
			return -EINVAL;
		}
	}

	genlmsg_end(rsp, hdr);

	return 0;
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

	if (err != -EMSGSIZE)
		return err;

	return skb->len;
}

static int
netdev_nl_napi_fill_one(struct sk_buff *rsp, struct napi_struct *napi,
			const struct genl_info *info)
{
	void *hdr;
	pid_t pid;

	if (WARN_ON_ONCE(!napi->dev))
		return -EINVAL;
	if (!(napi->dev->flags & IFF_UP))
		return 0;

	hdr = genlmsg_iput(rsp, info);
	if (!hdr)
		return -EMSGSIZE;

	if (napi->napi_id >= MIN_NAPI_ID &&
	    nla_put_u32(rsp, NETDEV_A_NAPI_ID, napi->napi_id))
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

	napi = napi_by_id(napi_id);
	if (napi)
		err = netdev_nl_napi_fill_one(rsp, napi, info);
	else
		err = -EINVAL;

	rtnl_unlock();

	if (err)
		goto err_free_msg;

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

	if (err != -EMSGSIZE)
		return err;

	return skb->len;
}

static int
netdev_nl_queue_fill_one(struct sk_buff *rsp, struct net_device *netdev,
			 u32 q_idx, u32 q_type, const struct genl_info *info)
{
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
	int err = 0;

	if (!(netdev->flags & IFF_UP))
		return err;

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
	int i;

	if (!(netdev->flags & IFF_UP))
		return err;

	for (i = ctx->rxq_idx; i < netdev->real_num_rx_queues;) {
		err = netdev_nl_queue_fill_one(rsp, netdev, i,
					       NETDEV_QUEUE_TYPE_RX, info);
		if (err)
			return err;
		ctx->rxq_idx = i++;
	}
	for (i = ctx->txq_idx; i < netdev->real_num_tx_queues;) {
		err = netdev_nl_queue_fill_one(rsp, netdev, i,
					       NETDEV_QUEUE_TYPE_TX, info);
		if (err)
			return err;
		ctx->txq_idx = i++;
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

	if (err != -EMSGSIZE)
		return err;

	return skb->len;
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

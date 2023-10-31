// SPDX-License-Identifier: GPL-2.0-only

#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/rtnetlink.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/xdp.h>

#include "netdev-genl-gen.h"

static int
netdev_nl_dev_fill(struct net_device *netdev, struct sk_buff *rsp,
		   const struct genl_info *info)
{
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

	if (nla_put_u32(rsp, NETDEV_A_DEV_IFINDEX, netdev->ifindex) ||
	    nla_put_u64_64bit(rsp, NETDEV_A_DEV_XDP_FEATURES,
			      netdev->xdp_features, NETDEV_A_DEV_PAD) ||
	    nla_put_u64_64bit(rsp, NETDEV_A_DEV_XDP_RX_METADATA_FEATURES,
			      xdp_rx_meta, NETDEV_A_DEV_PAD)) {
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
	struct net *net = sock_net(skb->sk);
	struct net_device *netdev;
	int err = 0;

	rtnl_lock();
	for_each_netdev_dump(net, netdev, cb->args[0]) {
		err = netdev_nl_dev_fill(netdev, skb, genl_info_dump(cb));
		if (err < 0)
			break;
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

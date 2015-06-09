/*
 * Netlink interface for IEEE 802.15.4 stack
 *
 * Copyright 2007, 2008 Siemens AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Written by:
 * Sergey Lapin <slapin@ossfans.org>
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Maxim Osipov <maxim.osipov@siemens.com>
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/if_arp.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <net/cfg802154.h>
#include <net/af_ieee802154.h>
#include <net/ieee802154_netdev.h>
#include <net/rtnetlink.h> /* for rtnl_{un,}lock */
#include <linux/nl802154.h>

#include "ieee802154.h"
#include "rdev-ops.h"
#include "core.h"

static int ieee802154_nl_fill_phy(struct sk_buff *msg, u32 portid,
				  u32 seq, int flags, struct wpan_phy *phy)
{
	void *hdr;
	int i, pages = 0;
	uint32_t *buf = kzalloc(32 * sizeof(uint32_t), GFP_KERNEL);

	pr_debug("%s\n", __func__);

	if (!buf)
		return -EMSGSIZE;

	hdr = genlmsg_put(msg, 0, seq, &nl802154_family, flags,
			  IEEE802154_LIST_PHY);
	if (!hdr)
		goto out;

	mutex_lock(&phy->pib_lock);
	if (nla_put_string(msg, IEEE802154_ATTR_PHY_NAME, wpan_phy_name(phy)) ||
	    nla_put_u8(msg, IEEE802154_ATTR_PAGE, phy->current_page) ||
	    nla_put_u8(msg, IEEE802154_ATTR_CHANNEL, phy->current_channel))
		goto nla_put_failure;
	for (i = 0; i < 32; i++) {
		if (phy->channels_supported[i])
			buf[pages++] = phy->channels_supported[i] | (i << 27);
	}
	if (pages &&
	    nla_put(msg, IEEE802154_ATTR_CHANNEL_PAGE_LIST,
		    pages * sizeof(uint32_t), buf))
		goto nla_put_failure;
	mutex_unlock(&phy->pib_lock);
	kfree(buf);
	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	mutex_unlock(&phy->pib_lock);
	genlmsg_cancel(msg, hdr);
out:
	kfree(buf);
	return -EMSGSIZE;
}

int ieee802154_list_phy(struct sk_buff *skb, struct genl_info *info)
{
	/* Request for interface name, index, type, IEEE address,
	 * PAN Id, short address
	 */
	struct sk_buff *msg;
	struct wpan_phy *phy;
	const char *name;
	int rc = -ENOBUFS;

	pr_debug("%s\n", __func__);

	if (!info->attrs[IEEE802154_ATTR_PHY_NAME])
		return -EINVAL;

	name = nla_data(info->attrs[IEEE802154_ATTR_PHY_NAME]);
	if (name[nla_len(info->attrs[IEEE802154_ATTR_PHY_NAME]) - 1] != '\0')
		return -EINVAL; /* phy name should be null-terminated */

	phy = wpan_phy_find(name);
	if (!phy)
		return -ENODEV;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		goto out_dev;

	rc = ieee802154_nl_fill_phy(msg, info->snd_portid, info->snd_seq,
				    0, phy);
	if (rc < 0)
		goto out_free;

	wpan_phy_put(phy);

	return genlmsg_reply(msg, info);
out_free:
	nlmsg_free(msg);
out_dev:
	wpan_phy_put(phy);
	return rc;
}

struct dump_phy_data {
	struct sk_buff *skb;
	struct netlink_callback *cb;
	int idx, s_idx;
};

static int ieee802154_dump_phy_iter(struct wpan_phy *phy, void *_data)
{
	int rc;
	struct dump_phy_data *data = _data;

	pr_debug("%s\n", __func__);

	if (data->idx++ < data->s_idx)
		return 0;

	rc = ieee802154_nl_fill_phy(data->skb,
				    NETLINK_CB(data->cb->skb).portid,
				    data->cb->nlh->nlmsg_seq,
				    NLM_F_MULTI,
				    phy);

	if (rc < 0) {
		data->idx--;
		return rc;
	}

	return 0;
}

int ieee802154_dump_phy(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct dump_phy_data data = {
		.cb = cb,
		.skb = skb,
		.s_idx = cb->args[0],
		.idx = 0,
	};

	pr_debug("%s\n", __func__);

	wpan_phy_for_each(ieee802154_dump_phy_iter, &data);

	cb->args[0] = data.idx;

	return skb->len;
}

int ieee802154_add_iface(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *msg;
	struct wpan_phy *phy;
	const char *name;
	const char *devname;
	int rc = -ENOBUFS;
	struct net_device *dev;
	int type = __IEEE802154_DEV_INVALID;
	unsigned char name_assign_type;

	pr_debug("%s\n", __func__);

	if (!info->attrs[IEEE802154_ATTR_PHY_NAME])
		return -EINVAL;

	name = nla_data(info->attrs[IEEE802154_ATTR_PHY_NAME]);
	if (name[nla_len(info->attrs[IEEE802154_ATTR_PHY_NAME]) - 1] != '\0')
		return -EINVAL; /* phy name should be null-terminated */

	if (info->attrs[IEEE802154_ATTR_DEV_NAME]) {
		devname = nla_data(info->attrs[IEEE802154_ATTR_DEV_NAME]);
		if (devname[nla_len(info->attrs[IEEE802154_ATTR_DEV_NAME]) - 1]
				!= '\0')
			return -EINVAL; /* phy name should be null-terminated */
		name_assign_type = NET_NAME_USER;
	} else  {
		devname = "wpan%d";
		name_assign_type = NET_NAME_ENUM;
	}

	if (strlen(devname) >= IFNAMSIZ)
		return -ENAMETOOLONG;

	phy = wpan_phy_find(name);
	if (!phy)
		return -ENODEV;

	msg = ieee802154_nl_new_reply(info, 0, IEEE802154_ADD_IFACE);
	if (!msg)
		goto out_dev;

	if (info->attrs[IEEE802154_ATTR_HW_ADDR] &&
	    nla_len(info->attrs[IEEE802154_ATTR_HW_ADDR]) !=
			IEEE802154_ADDR_LEN) {
		rc = -EINVAL;
		goto nla_put_failure;
	}

	if (info->attrs[IEEE802154_ATTR_DEV_TYPE]) {
		type = nla_get_u8(info->attrs[IEEE802154_ATTR_DEV_TYPE]);
		if (type >= __IEEE802154_DEV_MAX) {
			rc = -EINVAL;
			goto nla_put_failure;
		}
	}

	dev = rdev_add_virtual_intf_deprecated(wpan_phy_to_rdev(phy), devname,
					       name_assign_type, type);
	if (IS_ERR(dev)) {
		rc = PTR_ERR(dev);
		goto nla_put_failure;
	}
	dev_hold(dev);

	if (info->attrs[IEEE802154_ATTR_HW_ADDR]) {
		struct sockaddr addr;

		addr.sa_family = ARPHRD_IEEE802154;
		nla_memcpy(&addr.sa_data, info->attrs[IEEE802154_ATTR_HW_ADDR],
			   IEEE802154_ADDR_LEN);

		/* strangely enough, some callbacks (inetdev_event) from
		 * dev_set_mac_address require RTNL_LOCK
		 */
		rtnl_lock();
		rc = dev_set_mac_address(dev, &addr);
		rtnl_unlock();
		if (rc)
			goto dev_unregister;
	}

	if (nla_put_string(msg, IEEE802154_ATTR_PHY_NAME, wpan_phy_name(phy)) ||
	    nla_put_string(msg, IEEE802154_ATTR_DEV_NAME, dev->name))
		goto nla_put_failure;
	dev_put(dev);

	wpan_phy_put(phy);

	return ieee802154_nl_reply(msg, info);

dev_unregister:
	rtnl_lock(); /* del_iface must be called with RTNL lock */
	rdev_del_virtual_intf_deprecated(wpan_phy_to_rdev(phy), dev);
	dev_put(dev);
	rtnl_unlock();
nla_put_failure:
	nlmsg_free(msg);
out_dev:
	wpan_phy_put(phy);
	return rc;
}

int ieee802154_del_iface(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *msg;
	struct wpan_phy *phy;
	const char *name;
	int rc;
	struct net_device *dev;

	pr_debug("%s\n", __func__);

	if (!info->attrs[IEEE802154_ATTR_DEV_NAME])
		return -EINVAL;

	name = nla_data(info->attrs[IEEE802154_ATTR_DEV_NAME]);
	if (name[nla_len(info->attrs[IEEE802154_ATTR_DEV_NAME]) - 1] != '\0')
		return -EINVAL; /* name should be null-terminated */

	dev = dev_get_by_name(genl_info_net(info), name);
	if (!dev)
		return -ENODEV;

	phy = dev->ieee802154_ptr->wpan_phy;
	BUG_ON(!phy);
	get_device(&phy->dev);

	rc = -EINVAL;
	/* phy name is optional, but should be checked if it's given */
	if (info->attrs[IEEE802154_ATTR_PHY_NAME]) {
		struct wpan_phy *phy2;

		const char *pname =
			nla_data(info->attrs[IEEE802154_ATTR_PHY_NAME]);
		if (pname[nla_len(info->attrs[IEEE802154_ATTR_PHY_NAME]) - 1]
				!= '\0')
			/* name should be null-terminated */
			goto out_dev;

		phy2 = wpan_phy_find(pname);
		if (!phy2)
			goto out_dev;

		if (phy != phy2) {
			wpan_phy_put(phy2);
			goto out_dev;
		}
	}

	rc = -ENOBUFS;

	msg = ieee802154_nl_new_reply(info, 0, IEEE802154_DEL_IFACE);
	if (!msg)
		goto out_dev;

	rtnl_lock();
	rdev_del_virtual_intf_deprecated(wpan_phy_to_rdev(phy), dev);

	/* We don't have device anymore */
	dev_put(dev);
	dev = NULL;

	rtnl_unlock();

	if (nla_put_string(msg, IEEE802154_ATTR_PHY_NAME, wpan_phy_name(phy)) ||
	    nla_put_string(msg, IEEE802154_ATTR_DEV_NAME, name))
		goto nla_put_failure;
	wpan_phy_put(phy);

	return ieee802154_nl_reply(msg, info);

nla_put_failure:
	nlmsg_free(msg);
out_dev:
	wpan_phy_put(phy);
	if (dev)
		dev_put(dev);

	return rc;
}

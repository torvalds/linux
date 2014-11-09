/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors:
 * Alexander Aring <aar@pengutronix.de>
 *
 * Based on: net/wireless/nl80211.c
 */

#include <linux/rtnetlink.h>

#include <net/cfg802154.h>
#include <net/genetlink.h>
#include <net/mac802154.h>
#include <net/netlink.h>
#include <net/nl802154.h>
#include <net/sock.h>

#include "nl802154.h"
#include "core.h"

static int nl802154_pre_doit(const struct genl_ops *ops, struct sk_buff *skb,
			     struct genl_info *info);

static void nl802154_post_doit(const struct genl_ops *ops, struct sk_buff *skb,
			       struct genl_info *info);

/* the netlink family */
static struct genl_family nl802154_fam = {
	.id = GENL_ID_GENERATE,		/* don't bother with a hardcoded ID */
	.name = NL802154_GENL_NAME,	/* have users key off the name instead */
	.hdrsize = 0,			/* no private header */
	.version = 1,			/* no particular meaning now */
	.maxattr = NL802154_ATTR_MAX,
	.netnsok = true,
	.pre_doit = nl802154_pre_doit,
	.post_doit = nl802154_post_doit,
};

/* multicast groups */
enum nl802154_multicast_groups {
	NL802154_MCGRP_CONFIG,
};

static const struct genl_multicast_group nl802154_mcgrps[] = {
	[NL802154_MCGRP_CONFIG] = { .name = "config", },
};

/* returns ERR_PTR values */
static struct wpan_dev *
__cfg802154_wpan_dev_from_attrs(struct net *netns, struct nlattr **attrs)
{
	struct cfg802154_registered_device *rdev;
	struct wpan_dev *result = NULL;
	bool have_ifidx = attrs[NL802154_ATTR_IFINDEX];
	bool have_wpan_dev_id = attrs[NL802154_ATTR_WPAN_DEV];
	u64 wpan_dev_id;
	int wpan_phy_idx = -1;
	int ifidx = -1;

	ASSERT_RTNL();

	if (!have_ifidx && !have_wpan_dev_id)
		return ERR_PTR(-EINVAL);

	if (have_ifidx)
		ifidx = nla_get_u32(attrs[NL802154_ATTR_IFINDEX]);
	if (have_wpan_dev_id) {
		wpan_dev_id = nla_get_u64(attrs[NL802154_ATTR_WPAN_DEV]);
		wpan_phy_idx = wpan_dev_id >> 32;
	}

	list_for_each_entry(rdev, &cfg802154_rdev_list, list) {
		struct wpan_dev *wpan_dev;

		/* TODO netns compare */

		if (have_wpan_dev_id && rdev->wpan_phy_idx != wpan_phy_idx)
			continue;

		list_for_each_entry(wpan_dev, &rdev->wpan_dev_list, list) {
			if (have_ifidx && wpan_dev->netdev &&
			    wpan_dev->netdev->ifindex == ifidx) {
				result = wpan_dev;
				break;
			}
			if (have_wpan_dev_id &&
			    wpan_dev->identifier == (u32)wpan_dev_id) {
				result = wpan_dev;
				break;
			}
		}

		if (result)
			break;
	}

	if (result)
		return result;

	return ERR_PTR(-ENODEV);
}

static struct cfg802154_registered_device *
__cfg802154_rdev_from_attrs(struct net *netns, struct nlattr **attrs)
{
	struct cfg802154_registered_device *rdev = NULL, *tmp;
	struct net_device *netdev;

	ASSERT_RTNL();

	if (!attrs[NL802154_ATTR_WPAN_PHY] &&
	    !attrs[NL802154_ATTR_IFINDEX] &&
	    !attrs[NL802154_ATTR_WPAN_DEV])
		return ERR_PTR(-EINVAL);

	if (attrs[NL802154_ATTR_WPAN_PHY])
		rdev = cfg802154_rdev_by_wpan_phy_idx(
				nla_get_u32(attrs[NL802154_ATTR_WPAN_PHY]));

	if (attrs[NL802154_ATTR_WPAN_DEV]) {
		u64 wpan_dev_id = nla_get_u64(attrs[NL802154_ATTR_WPAN_DEV]);
		struct wpan_dev *wpan_dev;
		bool found = false;

		tmp = cfg802154_rdev_by_wpan_phy_idx(wpan_dev_id >> 32);
		if (tmp) {
			/* make sure wpan_dev exists */
			list_for_each_entry(wpan_dev, &tmp->wpan_dev_list, list) {
				if (wpan_dev->identifier != (u32)wpan_dev_id)
					continue;
				found = true;
				break;
			}

			if (!found)
				tmp = NULL;

			if (rdev && tmp != rdev)
				return ERR_PTR(-EINVAL);
			rdev = tmp;
		}
	}

	if (attrs[NL802154_ATTR_IFINDEX]) {
		int ifindex = nla_get_u32(attrs[NL802154_ATTR_IFINDEX]);

		netdev = __dev_get_by_index(netns, ifindex);
		if (netdev) {
			if (netdev->ieee802154_ptr)
				tmp = wpan_phy_to_rdev(
						netdev->ieee802154_ptr->wpan_phy);
			else
				tmp = NULL;

			/* not wireless device -- return error */
			if (!tmp)
				return ERR_PTR(-EINVAL);

			/* mismatch -- return error */
			if (rdev && tmp != rdev)
				return ERR_PTR(-EINVAL);

			rdev = tmp;
		}
	}

	if (!rdev)
		return ERR_PTR(-ENODEV);

	/* TODO netns compare */

	return rdev;
}

/* This function returns a pointer to the driver
 * that the genl_info item that is passed refers to.
 *
 * The result of this can be a PTR_ERR and hence must
 * be checked with IS_ERR() for errors.
 */
static struct cfg802154_registered_device *
cfg802154_get_dev_from_info(struct net *netns, struct genl_info *info)
{
	return __cfg802154_rdev_from_attrs(netns, info->attrs);
}

/* policy for the attributes */
static const struct nla_policy nl802154_policy[NL802154_ATTR_MAX+1] = {
};

/* message building helper */
static inline void *nl802154hdr_put(struct sk_buff *skb, u32 portid, u32 seq,
				    int flags, u8 cmd)
{
	/* since there is no private header just add the generic one */
	return genlmsg_put(skb, portid, seq, &nl802154_fam, flags, cmd);
}

#define NL802154_FLAG_NEED_WPAN_PHY	0x01
#define NL802154_FLAG_NEED_NETDEV	0x02
#define NL802154_FLAG_NEED_RTNL		0x04
#define NL802154_FLAG_CHECK_NETDEV_UP	0x08
#define NL802154_FLAG_NEED_NETDEV_UP	(NL802154_FLAG_NEED_NETDEV |\
					 NL802154_FLAG_CHECK_NETDEV_UP)
#define NL802154_FLAG_NEED_WPAN_DEV	0x10
#define NL802154_FLAG_NEED_WPAN_DEV_UP	(NL802154_FLAG_NEED_WPAN_DEV |\
					 NL802154_FLAG_CHECK_NETDEV_UP)

static int nl802154_pre_doit(const struct genl_ops *ops, struct sk_buff *skb,
			     struct genl_info *info)
{
	struct cfg802154_registered_device *rdev;
	struct wpan_dev *wpan_dev;
	struct net_device *dev;
	bool rtnl = ops->internal_flags & NL802154_FLAG_NEED_RTNL;

	if (rtnl)
		rtnl_lock();

	if (ops->internal_flags & NL802154_FLAG_NEED_WPAN_PHY) {
		rdev = cfg802154_get_dev_from_info(genl_info_net(info), info);
		if (IS_ERR(rdev)) {
			if (rtnl)
				rtnl_unlock();
			return PTR_ERR(rdev);
		}
		info->user_ptr[0] = rdev;
	} else if (ops->internal_flags & NL802154_FLAG_NEED_NETDEV ||
		   ops->internal_flags & NL802154_FLAG_NEED_WPAN_DEV) {
		ASSERT_RTNL();
		wpan_dev = __cfg802154_wpan_dev_from_attrs(genl_info_net(info),
							   info->attrs);
		if (IS_ERR(wpan_dev)) {
			if (rtnl)
				rtnl_unlock();
			return PTR_ERR(wpan_dev);
		}

		dev = wpan_dev->netdev;
		rdev = wpan_phy_to_rdev(wpan_dev->wpan_phy);

		if (ops->internal_flags & NL802154_FLAG_NEED_NETDEV) {
			if (!dev) {
				if (rtnl)
					rtnl_unlock();
				return -EINVAL;
			}

			info->user_ptr[1] = dev;
		} else {
			info->user_ptr[1] = wpan_dev;
		}

		if (dev) {
			if (ops->internal_flags & NL802154_FLAG_CHECK_NETDEV_UP &&
			    !netif_running(dev)) {
				if (rtnl)
					rtnl_unlock();
				return -ENETDOWN;
			}

			dev_hold(dev);
		}

		info->user_ptr[0] = rdev;
	}

	return 0;
}

static void nl802154_post_doit(const struct genl_ops *ops, struct sk_buff *skb,
			       struct genl_info *info)
{
	if (info->user_ptr[1]) {
		if (ops->internal_flags & NL802154_FLAG_NEED_WPAN_DEV) {
			struct wpan_dev *wpan_dev = info->user_ptr[1];

			if (wpan_dev->netdev)
				dev_put(wpan_dev->netdev);
		} else {
			dev_put(info->user_ptr[1]);
		}
	}

	if (ops->internal_flags & NL802154_FLAG_NEED_RTNL)
		rtnl_unlock();
}

static const struct genl_ops nl802154_ops[] = {
};

/* initialisation/exit functions */
int nl802154_init(void)
{
	return genl_register_family_with_ops_groups(&nl802154_fam, nl802154_ops,
						    nl802154_mcgrps);
}

void nl802154_exit(void)
{
	genl_unregister_family(&nl802154_fam);
}

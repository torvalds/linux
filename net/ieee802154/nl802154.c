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
#include "rdev-ops.h"
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
	[NL802154_ATTR_WPAN_PHY] = { .type = NLA_U32 },
	[NL802154_ATTR_WPAN_PHY_NAME] = { .type = NLA_NUL_STRING,
					  .len = 20-1 },

	[NL802154_ATTR_IFINDEX] = { .type = NLA_U32 },
	[NL802154_ATTR_IFTYPE] = { .type = NLA_U32 },
	[NL802154_ATTR_IFNAME] = { .type = NLA_NUL_STRING, .len = IFNAMSIZ-1 },

	[NL802154_ATTR_WPAN_DEV] = { .type = NLA_U64 },

	[NL802154_ATTR_PAGE] = { .type = NLA_U8, },
	[NL802154_ATTR_CHANNEL] = { .type = NLA_U8, },

	[NL802154_ATTR_TX_POWER] = { .type = NLA_S8, },

	[NL802154_ATTR_CCA_MODE] = { .type = NLA_U32, },
	[NL802154_ATTR_CCA_OPT] = { .type = NLA_U32, },

	[NL802154_ATTR_SUPPORTED_CHANNEL] = { .type = NLA_U32, },

	[NL802154_ATTR_PAN_ID] = { .type = NLA_U16, },
	[NL802154_ATTR_EXTENDED_ADDR] = { .type = NLA_U64 },
	[NL802154_ATTR_SHORT_ADDR] = { .type = NLA_U16, },

	[NL802154_ATTR_MIN_BE] = { .type = NLA_U8, },
	[NL802154_ATTR_MAX_BE] = { .type = NLA_U8, },
	[NL802154_ATTR_MAX_CSMA_BACKOFFS] = { .type = NLA_U8, },

	[NL802154_ATTR_MAX_FRAME_RETRIES] = { .type = NLA_S8, },

	[NL802154_ATTR_LBT_MODE] = { .type = NLA_U8, },
};

/* message building helper */
static inline void *nl802154hdr_put(struct sk_buff *skb, u32 portid, u32 seq,
				    int flags, u8 cmd)
{
	/* since there is no private header just add the generic one */
	return genlmsg_put(skb, portid, seq, &nl802154_fam, flags, cmd);
}

static int
nl802154_send_wpan_phy_channels(struct cfg802154_registered_device *rdev,
				struct sk_buff *msg)
{
	struct nlattr *nl_page;
	unsigned long page;

	nl_page = nla_nest_start(msg, NL802154_ATTR_CHANNELS_SUPPORTED);
	if (!nl_page)
		return -ENOBUFS;

	for (page = 0; page <= IEEE802154_MAX_PAGE; page++) {
		if (nla_put_u32(msg, NL802154_ATTR_SUPPORTED_CHANNEL,
				rdev->wpan_phy.channels_supported[page]))
			return -ENOBUFS;
	}
	nla_nest_end(msg, nl_page);

	return 0;
}

static int nl802154_send_wpan_phy(struct cfg802154_registered_device *rdev,
				  enum nl802154_commands cmd,
				  struct sk_buff *msg, u32 portid, u32 seq,
				  int flags)
{
	void *hdr;

	hdr = nl802154hdr_put(msg, portid, seq, flags, cmd);
	if (!hdr)
		return -ENOBUFS;

	if (nla_put_u32(msg, NL802154_ATTR_WPAN_PHY, rdev->wpan_phy_idx) ||
	    nla_put_string(msg, NL802154_ATTR_WPAN_PHY_NAME,
			   wpan_phy_name(&rdev->wpan_phy)) ||
	    nla_put_u32(msg, NL802154_ATTR_GENERATION,
			cfg802154_rdev_list_generation))
		goto nla_put_failure;

	if (cmd != NL802154_CMD_NEW_WPAN_PHY)
		goto finish;

	/* DUMP PHY PIB */

	/* current channel settings */
	if (nla_put_u8(msg, NL802154_ATTR_PAGE,
		       rdev->wpan_phy.current_page) ||
	    nla_put_u8(msg, NL802154_ATTR_CHANNEL,
		       rdev->wpan_phy.current_channel))
		goto nla_put_failure;

	/* supported channels array */
	if (nl802154_send_wpan_phy_channels(rdev, msg))
		goto nla_put_failure;

	/* cca mode */
	if (nla_put_u32(msg, NL802154_ATTR_CCA_MODE,
			rdev->wpan_phy.cca.mode))
		goto nla_put_failure;

	if (rdev->wpan_phy.cca.mode == NL802154_CCA_ENERGY_CARRIER) {
		if (nla_put_u32(msg, NL802154_ATTR_CCA_OPT,
				rdev->wpan_phy.cca.opt))
			goto nla_put_failure;
	}

	if (nla_put_s8(msg, NL802154_ATTR_TX_POWER,
		       rdev->wpan_phy.transmit_power))
		goto nla_put_failure;

finish:
	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

struct nl802154_dump_wpan_phy_state {
	s64 filter_wpan_phy;
	long start;

};

static int nl802154_dump_wpan_phy_parse(struct sk_buff *skb,
					struct netlink_callback *cb,
					struct nl802154_dump_wpan_phy_state *state)
{
	struct nlattr **tb = nl802154_fam.attrbuf;
	int ret = nlmsg_parse(cb->nlh, GENL_HDRLEN + nl802154_fam.hdrsize,
			      tb, nl802154_fam.maxattr, nl802154_policy);

	/* TODO check if we can handle error here,
	 * we have no backward compatibility
	 */
	if (ret)
		return 0;

	if (tb[NL802154_ATTR_WPAN_PHY])
		state->filter_wpan_phy = nla_get_u32(tb[NL802154_ATTR_WPAN_PHY]);
	if (tb[NL802154_ATTR_WPAN_DEV])
		state->filter_wpan_phy = nla_get_u64(tb[NL802154_ATTR_WPAN_DEV]) >> 32;
	if (tb[NL802154_ATTR_IFINDEX]) {
		struct net_device *netdev;
		struct cfg802154_registered_device *rdev;
		int ifidx = nla_get_u32(tb[NL802154_ATTR_IFINDEX]);

		/* TODO netns */
		netdev = __dev_get_by_index(&init_net, ifidx);
		if (!netdev)
			return -ENODEV;
		if (netdev->ieee802154_ptr) {
			rdev = wpan_phy_to_rdev(
					netdev->ieee802154_ptr->wpan_phy);
			state->filter_wpan_phy = rdev->wpan_phy_idx;
		}
	}

	return 0;
}

static int
nl802154_dump_wpan_phy(struct sk_buff *skb, struct netlink_callback *cb)
{
	int idx = 0, ret;
	struct nl802154_dump_wpan_phy_state *state = (void *)cb->args[0];
	struct cfg802154_registered_device *rdev;

	rtnl_lock();
	if (!state) {
		state = kzalloc(sizeof(*state), GFP_KERNEL);
		if (!state) {
			rtnl_unlock();
			return -ENOMEM;
		}
		state->filter_wpan_phy = -1;
		ret = nl802154_dump_wpan_phy_parse(skb, cb, state);
		if (ret) {
			kfree(state);
			rtnl_unlock();
			return ret;
		}
		cb->args[0] = (long)state;
	}

	list_for_each_entry(rdev, &cfg802154_rdev_list, list) {
		/* TODO net ns compare */
		if (++idx <= state->start)
			continue;
		if (state->filter_wpan_phy != -1 &&
		    state->filter_wpan_phy != rdev->wpan_phy_idx)
			continue;
		/* attempt to fit multiple wpan_phy data chunks into the skb */
		ret = nl802154_send_wpan_phy(rdev,
					     NL802154_CMD_NEW_WPAN_PHY,
					     skb,
					     NETLINK_CB(cb->skb).portid,
					     cb->nlh->nlmsg_seq, NLM_F_MULTI);
		if (ret < 0) {
			if ((ret == -ENOBUFS || ret == -EMSGSIZE) &&
			    !skb->len && cb->min_dump_alloc < 4096) {
				cb->min_dump_alloc = 4096;
				rtnl_unlock();
				return 1;
			}
			idx--;
			break;
		}
		break;
	}
	rtnl_unlock();

	state->start = idx;

	return skb->len;
}

static int nl802154_dump_wpan_phy_done(struct netlink_callback *cb)
{
	kfree((void *)cb->args[0]);
	return 0;
}

static int nl802154_get_wpan_phy(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *msg;
	struct cfg802154_registered_device *rdev = info->user_ptr[0];

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	if (nl802154_send_wpan_phy(rdev, NL802154_CMD_NEW_WPAN_PHY, msg,
				   info->snd_portid, info->snd_seq, 0) < 0) {
		nlmsg_free(msg);
		return -ENOBUFS;
	}

	return genlmsg_reply(msg, info);
}

static inline u64 wpan_dev_id(struct wpan_dev *wpan_dev)
{
	return (u64)wpan_dev->identifier |
	       ((u64)wpan_phy_to_rdev(wpan_dev->wpan_phy)->wpan_phy_idx << 32);
}

static int
nl802154_send_iface(struct sk_buff *msg, u32 portid, u32 seq, int flags,
		    struct cfg802154_registered_device *rdev,
		    struct wpan_dev *wpan_dev)
{
	struct net_device *dev = wpan_dev->netdev;
	void *hdr;

	hdr = nl802154hdr_put(msg, portid, seq, flags,
			      NL802154_CMD_NEW_INTERFACE);
	if (!hdr)
		return -1;

	if (dev &&
	    (nla_put_u32(msg, NL802154_ATTR_IFINDEX, dev->ifindex) ||
	     nla_put_string(msg, NL802154_ATTR_IFNAME, dev->name)))
		goto nla_put_failure;

	if (nla_put_u32(msg, NL802154_ATTR_WPAN_PHY, rdev->wpan_phy_idx) ||
	    nla_put_u32(msg, NL802154_ATTR_IFTYPE, wpan_dev->iftype) ||
	    nla_put_u64(msg, NL802154_ATTR_WPAN_DEV, wpan_dev_id(wpan_dev)) ||
	    nla_put_u32(msg, NL802154_ATTR_GENERATION,
			rdev->devlist_generation ^
			(cfg802154_rdev_list_generation << 2)))
		goto nla_put_failure;

	/* address settings */
	if (nla_put_le64(msg, NL802154_ATTR_EXTENDED_ADDR,
			 wpan_dev->extended_addr) ||
	    nla_put_le16(msg, NL802154_ATTR_SHORT_ADDR,
			 wpan_dev->short_addr) ||
	    nla_put_le16(msg, NL802154_ATTR_PAN_ID, wpan_dev->pan_id))
		goto nla_put_failure;

	/* ARET handling */
	if (nla_put_s8(msg, NL802154_ATTR_MAX_FRAME_RETRIES,
		       wpan_dev->frame_retries) ||
	    nla_put_u8(msg, NL802154_ATTR_MAX_BE, wpan_dev->max_be) ||
	    nla_put_u8(msg, NL802154_ATTR_MAX_CSMA_BACKOFFS,
		       wpan_dev->csma_retries) ||
	    nla_put_u8(msg, NL802154_ATTR_MIN_BE, wpan_dev->min_be))
		goto nla_put_failure;

	/* listen before transmit */
	if (nla_put_u8(msg, NL802154_ATTR_LBT_MODE, wpan_dev->lbt))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int
nl802154_dump_interface(struct sk_buff *skb, struct netlink_callback *cb)
{
	int wp_idx = 0;
	int if_idx = 0;
	int wp_start = cb->args[0];
	int if_start = cb->args[1];
	struct cfg802154_registered_device *rdev;
	struct wpan_dev *wpan_dev;

	rtnl_lock();
	list_for_each_entry(rdev, &cfg802154_rdev_list, list) {
		/* TODO netns compare */
		if (wp_idx < wp_start) {
			wp_idx++;
			continue;
		}
		if_idx = 0;

		list_for_each_entry(wpan_dev, &rdev->wpan_dev_list, list) {
			if (if_idx < if_start) {
				if_idx++;
				continue;
			}
			if (nl802154_send_iface(skb, NETLINK_CB(cb->skb).portid,
						cb->nlh->nlmsg_seq, NLM_F_MULTI,
						rdev, wpan_dev) < 0) {
				goto out;
			}
			if_idx++;
		}

		wp_idx++;
	}
out:
	rtnl_unlock();

	cb->args[0] = wp_idx;
	cb->args[1] = if_idx;

	return skb->len;
}

static int nl802154_get_interface(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *msg;
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct wpan_dev *wdev = info->user_ptr[1];

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	if (nl802154_send_iface(msg, info->snd_portid, info->snd_seq, 0,
				rdev, wdev) < 0) {
		nlmsg_free(msg);
		return -ENOBUFS;
	}

	return genlmsg_reply(msg, info);
}

static int nl802154_new_interface(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	enum nl802154_iftype type = NL802154_IFTYPE_UNSPEC;
	__le64 extended_addr = cpu_to_le64(0x0000000000000000ULL);

	/* TODO avoid failing a new interface
	 * creation due to pending removal?
	 */

	if (!info->attrs[NL802154_ATTR_IFNAME])
		return -EINVAL;

	if (info->attrs[NL802154_ATTR_IFTYPE]) {
		type = nla_get_u32(info->attrs[NL802154_ATTR_IFTYPE]);
		if (type > NL802154_IFTYPE_MAX)
			return -EINVAL;
	}

	/* TODO add nla_get_le64 to netlink */
	if (info->attrs[NL802154_ATTR_EXTENDED_ADDR])
		extended_addr = (__force __le64)nla_get_u64(
				info->attrs[NL802154_ATTR_EXTENDED_ADDR]);

	if (!rdev->ops->add_virtual_intf)
		return -EOPNOTSUPP;

	return rdev_add_virtual_intf(rdev,
				     nla_data(info->attrs[NL802154_ATTR_IFNAME]),
				     type, extended_addr);
}

static int nl802154_del_interface(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct wpan_dev *wpan_dev = info->user_ptr[1];

	if (!rdev->ops->del_virtual_intf)
		return -EOPNOTSUPP;

	/* If we remove a wpan device without a netdev then clear
	 * user_ptr[1] so that nl802154_post_doit won't dereference it
	 * to check if it needs to do dev_put(). Otherwise it crashes
	 * since the wpan_dev has been freed, unlike with a netdev where
	 * we need the dev_put() for the netdev to really be freed.
	 */
	if (!wpan_dev->netdev)
		info->user_ptr[1] = NULL;

	return rdev_del_virtual_intf(rdev, wpan_dev);
}

static int nl802154_set_channel(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	u8 channel, page;

	if (!info->attrs[NL802154_ATTR_PAGE] ||
	    !info->attrs[NL802154_ATTR_CHANNEL])
		return -EINVAL;

	page = nla_get_u8(info->attrs[NL802154_ATTR_PAGE]);
	channel = nla_get_u8(info->attrs[NL802154_ATTR_CHANNEL]);

	/* check 802.15.4 constraints */
	if (page > IEEE802154_MAX_PAGE || channel > IEEE802154_MAX_CHANNEL)
		return -EINVAL;

	return rdev_set_channel(rdev, page, channel);
}

static int nl802154_set_cca_mode(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct wpan_phy_cca cca;

	if (!info->attrs[NL802154_ATTR_CCA_MODE])
		return -EINVAL;

	cca.mode = nla_get_u32(info->attrs[NL802154_ATTR_CCA_MODE]);
	/* checking 802.15.4 constraints */
	if (cca.mode < NL802154_CCA_ENERGY || cca.mode > NL802154_CCA_ATTR_MAX)
		return -EINVAL;

	if (cca.mode == NL802154_CCA_ENERGY_CARRIER) {
		if (!info->attrs[NL802154_ATTR_CCA_OPT])
			return -EINVAL;

		cca.opt = nla_get_u32(info->attrs[NL802154_ATTR_CCA_OPT]);
		if (cca.opt > NL802154_CCA_OPT_ATTR_MAX)
			return -EINVAL;
	}

	return rdev_set_cca_mode(rdev, &cca);
}

static int nl802154_set_pan_id(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wpan_dev *wpan_dev = dev->ieee802154_ptr;
	__le16 pan_id;

	/* conflict here while tx/rx calls */
	if (netif_running(dev))
		return -EBUSY;

	/* don't change address fields on monitor */
	if (wpan_dev->iftype == NL802154_IFTYPE_MONITOR)
		return -EINVAL;

	if (!info->attrs[NL802154_ATTR_PAN_ID])
		return -EINVAL;

	pan_id = nla_get_le16(info->attrs[NL802154_ATTR_PAN_ID]);

	return rdev_set_pan_id(rdev, wpan_dev, pan_id);
}

static int nl802154_set_short_addr(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wpan_dev *wpan_dev = dev->ieee802154_ptr;
	__le16 short_addr;

	/* conflict here while tx/rx calls */
	if (netif_running(dev))
		return -EBUSY;

	/* don't change address fields on monitor */
	if (wpan_dev->iftype == NL802154_IFTYPE_MONITOR)
		return -EINVAL;

	if (!info->attrs[NL802154_ATTR_SHORT_ADDR])
		return -EINVAL;

	short_addr = nla_get_le16(info->attrs[NL802154_ATTR_SHORT_ADDR]);

	return rdev_set_short_addr(rdev, wpan_dev, short_addr);
}

static int
nl802154_set_backoff_exponent(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wpan_dev *wpan_dev = dev->ieee802154_ptr;
	u8 min_be, max_be;

	/* should be set on netif open inside phy settings */
	if (netif_running(dev))
		return -EBUSY;

	if (!info->attrs[NL802154_ATTR_MIN_BE] ||
	    !info->attrs[NL802154_ATTR_MAX_BE])
		return -EINVAL;

	min_be = nla_get_u8(info->attrs[NL802154_ATTR_MIN_BE]);
	max_be = nla_get_u8(info->attrs[NL802154_ATTR_MAX_BE]);

	/* check 802.15.4 constraints */
	if (max_be < 3 || max_be > 8 || min_be > max_be)
		return -EINVAL;

	return rdev_set_backoff_exponent(rdev, wpan_dev, min_be, max_be);
}

static int
nl802154_set_max_csma_backoffs(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wpan_dev *wpan_dev = dev->ieee802154_ptr;
	u8 max_csma_backoffs;

	/* conflict here while other running iface settings */
	if (netif_running(dev))
		return -EBUSY;

	if (!info->attrs[NL802154_ATTR_MAX_CSMA_BACKOFFS])
		return -EINVAL;

	max_csma_backoffs = nla_get_u8(
			info->attrs[NL802154_ATTR_MAX_CSMA_BACKOFFS]);

	/* check 802.15.4 constraints */
	if (max_csma_backoffs > 5)
		return -EINVAL;

	return rdev_set_max_csma_backoffs(rdev, wpan_dev, max_csma_backoffs);
}

static int
nl802154_set_max_frame_retries(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wpan_dev *wpan_dev = dev->ieee802154_ptr;
	s8 max_frame_retries;

	if (netif_running(dev))
		return -EBUSY;

	if (!info->attrs[NL802154_ATTR_MAX_FRAME_RETRIES])
		return -EINVAL;

	max_frame_retries = nla_get_s8(
			info->attrs[NL802154_ATTR_MAX_FRAME_RETRIES]);

	/* check 802.15.4 constraints */
	if (max_frame_retries < -1 || max_frame_retries > 7)
		return -EINVAL;

	return rdev_set_max_frame_retries(rdev, wpan_dev, max_frame_retries);
}

static int nl802154_set_lbt_mode(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wpan_dev *wpan_dev = dev->ieee802154_ptr;
	bool mode;

	if (netif_running(dev))
		return -EBUSY;

	if (!info->attrs[NL802154_ATTR_LBT_MODE])
		return -EINVAL;

	mode = !!nla_get_u8(info->attrs[NL802154_ATTR_LBT_MODE]);
	return rdev_set_lbt_mode(rdev, wpan_dev, mode);
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
	{
		.cmd = NL802154_CMD_GET_WPAN_PHY,
		.doit = nl802154_get_wpan_phy,
		.dumpit = nl802154_dump_wpan_phy,
		.done = nl802154_dump_wpan_phy_done,
		.policy = nl802154_policy,
		/* can be retrieved by unprivileged users */
		.internal_flags = NL802154_FLAG_NEED_WPAN_PHY |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_GET_INTERFACE,
		.doit = nl802154_get_interface,
		.dumpit = nl802154_dump_interface,
		.policy = nl802154_policy,
		/* can be retrieved by unprivileged users */
		.internal_flags = NL802154_FLAG_NEED_WPAN_DEV |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_NEW_INTERFACE,
		.doit = nl802154_new_interface,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_WPAN_PHY |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_DEL_INTERFACE,
		.doit = nl802154_del_interface,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_WPAN_DEV |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_SET_CHANNEL,
		.doit = nl802154_set_channel,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_WPAN_PHY |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_SET_CCA_MODE,
		.doit = nl802154_set_cca_mode,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_WPAN_PHY |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_SET_PAN_ID,
		.doit = nl802154_set_pan_id,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_NETDEV |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_SET_SHORT_ADDR,
		.doit = nl802154_set_short_addr,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_NETDEV |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_SET_BACKOFF_EXPONENT,
		.doit = nl802154_set_backoff_exponent,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_NETDEV |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_SET_MAX_CSMA_BACKOFFS,
		.doit = nl802154_set_max_csma_backoffs,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_NETDEV |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_SET_MAX_FRAME_RETRIES,
		.doit = nl802154_set_max_frame_retries,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_NETDEV |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_SET_LBT_MODE,
		.doit = nl802154_set_lbt_mode,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_NETDEV |
				  NL802154_FLAG_NEED_RTNL,
	},
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

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

	[NL802154_ATTR_TX_POWER] = { .type = NLA_S32, },

	[NL802154_ATTR_CCA_MODE] = { .type = NLA_U32, },
	[NL802154_ATTR_CCA_OPT] = { .type = NLA_U32, },
	[NL802154_ATTR_CCA_ED_LEVEL] = { .type = NLA_S32, },

	[NL802154_ATTR_SUPPORTED_CHANNEL] = { .type = NLA_U32, },

	[NL802154_ATTR_PAN_ID] = { .type = NLA_U16, },
	[NL802154_ATTR_EXTENDED_ADDR] = { .type = NLA_U64 },
	[NL802154_ATTR_SHORT_ADDR] = { .type = NLA_U16, },

	[NL802154_ATTR_MIN_BE] = { .type = NLA_U8, },
	[NL802154_ATTR_MAX_BE] = { .type = NLA_U8, },
	[NL802154_ATTR_MAX_CSMA_BACKOFFS] = { .type = NLA_U8, },

	[NL802154_ATTR_MAX_FRAME_RETRIES] = { .type = NLA_S8, },

	[NL802154_ATTR_LBT_MODE] = { .type = NLA_U8, },

	[NL802154_ATTR_WPAN_PHY_CAPS] = { .type = NLA_NESTED },

	[NL802154_ATTR_SUPPORTED_COMMANDS] = { .type = NLA_NESTED },

	[NL802154_ATTR_ACKREQ_DEFAULT] = { .type = NLA_U8 },

#ifdef CONFIG_IEEE802154_NL802154_EXPERIMENTAL
	[NL802154_ATTR_SEC_ENABLED] = { .type = NLA_U8, },
	[NL802154_ATTR_SEC_OUT_LEVEL] = { .type = NLA_U32, },
	[NL802154_ATTR_SEC_OUT_KEY_ID] = { .type = NLA_NESTED, },
	[NL802154_ATTR_SEC_FRAME_COUNTER] = { .type = NLA_U32 },

	[NL802154_ATTR_SEC_LEVEL] = { .type = NLA_NESTED },
	[NL802154_ATTR_SEC_DEVICE] = { .type = NLA_NESTED },
	[NL802154_ATTR_SEC_DEVKEY] = { .type = NLA_NESTED },
	[NL802154_ATTR_SEC_KEY] = { .type = NLA_NESTED },
#endif /* CONFIG_IEEE802154_NL802154_EXPERIMENTAL */
};

#ifdef CONFIG_IEEE802154_NL802154_EXPERIMENTAL
static int
nl802154_prepare_wpan_dev_dump(struct sk_buff *skb,
			       struct netlink_callback *cb,
			       struct cfg802154_registered_device **rdev,
			       struct wpan_dev **wpan_dev)
{
	int err;

	rtnl_lock();

	if (!cb->args[0]) {
		err = nlmsg_parse(cb->nlh, GENL_HDRLEN + nl802154_fam.hdrsize,
				  nl802154_fam.attrbuf, nl802154_fam.maxattr,
				  nl802154_policy);
		if (err)
			goto out_unlock;

		*wpan_dev = __cfg802154_wpan_dev_from_attrs(sock_net(skb->sk),
							    nl802154_fam.attrbuf);
		if (IS_ERR(*wpan_dev)) {
			err = PTR_ERR(*wpan_dev);
			goto out_unlock;
		}
		*rdev = wpan_phy_to_rdev((*wpan_dev)->wpan_phy);
		/* 0 is the first index - add 1 to parse only once */
		cb->args[0] = (*rdev)->wpan_phy_idx + 1;
		cb->args[1] = (*wpan_dev)->identifier;
	} else {
		/* subtract the 1 again here */
		struct wpan_phy *wpan_phy = wpan_phy_idx_to_wpan_phy(cb->args[0] - 1);
		struct wpan_dev *tmp;

		if (!wpan_phy) {
			err = -ENODEV;
			goto out_unlock;
		}
		*rdev = wpan_phy_to_rdev(wpan_phy);
		*wpan_dev = NULL;

		list_for_each_entry(tmp, &(*rdev)->wpan_dev_list, list) {
			if (tmp->identifier == cb->args[1]) {
				*wpan_dev = tmp;
				break;
			}
		}

		if (!*wpan_dev) {
			err = -ENODEV;
			goto out_unlock;
		}
	}

	return 0;
 out_unlock:
	rtnl_unlock();
	return err;
}

static void
nl802154_finish_wpan_dev_dump(struct cfg802154_registered_device *rdev)
{
	rtnl_unlock();
}
#endif /* CONFIG_IEEE802154_NL802154_EXPERIMENTAL */

/* message building helper */
static inline void *nl802154hdr_put(struct sk_buff *skb, u32 portid, u32 seq,
				    int flags, u8 cmd)
{
	/* since there is no private header just add the generic one */
	return genlmsg_put(skb, portid, seq, &nl802154_fam, flags, cmd);
}

static int
nl802154_put_flags(struct sk_buff *msg, int attr, u32 mask)
{
	struct nlattr *nl_flags = nla_nest_start(msg, attr);
	int i;

	if (!nl_flags)
		return -ENOBUFS;

	i = 0;
	while (mask) {
		if ((mask & 1) && nla_put_flag(msg, i))
			return -ENOBUFS;

		mask >>= 1;
		i++;
	}

	nla_nest_end(msg, nl_flags);
	return 0;
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
				rdev->wpan_phy.supported.channels[page]))
			return -ENOBUFS;
	}
	nla_nest_end(msg, nl_page);

	return 0;
}

static int
nl802154_put_capabilities(struct sk_buff *msg,
			  struct cfg802154_registered_device *rdev)
{
	const struct wpan_phy_supported *caps = &rdev->wpan_phy.supported;
	struct nlattr *nl_caps, *nl_channels;
	int i;

	nl_caps = nla_nest_start(msg, NL802154_ATTR_WPAN_PHY_CAPS);
	if (!nl_caps)
		return -ENOBUFS;

	nl_channels = nla_nest_start(msg, NL802154_CAP_ATTR_CHANNELS);
	if (!nl_channels)
		return -ENOBUFS;

	for (i = 0; i <= IEEE802154_MAX_PAGE; i++) {
		if (caps->channels[i]) {
			if (nl802154_put_flags(msg, i, caps->channels[i]))
				return -ENOBUFS;
		}
	}

	nla_nest_end(msg, nl_channels);

	if (rdev->wpan_phy.flags & WPAN_PHY_FLAG_CCA_ED_LEVEL) {
		struct nlattr *nl_ed_lvls;

		nl_ed_lvls = nla_nest_start(msg,
					    NL802154_CAP_ATTR_CCA_ED_LEVELS);
		if (!nl_ed_lvls)
			return -ENOBUFS;

		for (i = 0; i < caps->cca_ed_levels_size; i++) {
			if (nla_put_s32(msg, i, caps->cca_ed_levels[i]))
				return -ENOBUFS;
		}

		nla_nest_end(msg, nl_ed_lvls);
	}

	if (rdev->wpan_phy.flags & WPAN_PHY_FLAG_TXPOWER) {
		struct nlattr *nl_tx_pwrs;

		nl_tx_pwrs = nla_nest_start(msg, NL802154_CAP_ATTR_TX_POWERS);
		if (!nl_tx_pwrs)
			return -ENOBUFS;

		for (i = 0; i < caps->tx_powers_size; i++) {
			if (nla_put_s32(msg, i, caps->tx_powers[i]))
				return -ENOBUFS;
		}

		nla_nest_end(msg, nl_tx_pwrs);
	}

	if (rdev->wpan_phy.flags & WPAN_PHY_FLAG_CCA_MODE) {
		if (nl802154_put_flags(msg, NL802154_CAP_ATTR_CCA_MODES,
				       caps->cca_modes) ||
		    nl802154_put_flags(msg, NL802154_CAP_ATTR_CCA_OPTS,
				       caps->cca_opts))
			return -ENOBUFS;
	}

	if (nla_put_u8(msg, NL802154_CAP_ATTR_MIN_MINBE, caps->min_minbe) ||
	    nla_put_u8(msg, NL802154_CAP_ATTR_MAX_MINBE, caps->max_minbe) ||
	    nla_put_u8(msg, NL802154_CAP_ATTR_MIN_MAXBE, caps->min_maxbe) ||
	    nla_put_u8(msg, NL802154_CAP_ATTR_MAX_MAXBE, caps->max_maxbe) ||
	    nla_put_u8(msg, NL802154_CAP_ATTR_MIN_CSMA_BACKOFFS,
		       caps->min_csma_backoffs) ||
	    nla_put_u8(msg, NL802154_CAP_ATTR_MAX_CSMA_BACKOFFS,
		       caps->max_csma_backoffs) ||
	    nla_put_s8(msg, NL802154_CAP_ATTR_MIN_FRAME_RETRIES,
		       caps->min_frame_retries) ||
	    nla_put_s8(msg, NL802154_CAP_ATTR_MAX_FRAME_RETRIES,
		       caps->max_frame_retries) ||
	    nl802154_put_flags(msg, NL802154_CAP_ATTR_IFTYPES,
			       caps->iftypes) ||
	    nla_put_u32(msg, NL802154_CAP_ATTR_LBT, caps->lbt))
		return -ENOBUFS;

	nla_nest_end(msg, nl_caps);

	return 0;
}

static int nl802154_send_wpan_phy(struct cfg802154_registered_device *rdev,
				  enum nl802154_commands cmd,
				  struct sk_buff *msg, u32 portid, u32 seq,
				  int flags)
{
	struct nlattr *nl_cmds;
	void *hdr;
	int i;

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

	/* TODO remove this behaviour, we still keep support it for a while
	 * so users can change the behaviour to the new one.
	 */
	if (nl802154_send_wpan_phy_channels(rdev, msg))
		goto nla_put_failure;

	/* cca mode */
	if (rdev->wpan_phy.flags & WPAN_PHY_FLAG_CCA_MODE) {
		if (nla_put_u32(msg, NL802154_ATTR_CCA_MODE,
				rdev->wpan_phy.cca.mode))
			goto nla_put_failure;

		if (rdev->wpan_phy.cca.mode == NL802154_CCA_ENERGY_CARRIER) {
			if (nla_put_u32(msg, NL802154_ATTR_CCA_OPT,
					rdev->wpan_phy.cca.opt))
				goto nla_put_failure;
		}
	}

	if (rdev->wpan_phy.flags & WPAN_PHY_FLAG_TXPOWER) {
		if (nla_put_s32(msg, NL802154_ATTR_TX_POWER,
				rdev->wpan_phy.transmit_power))
			goto nla_put_failure;
	}

	if (rdev->wpan_phy.flags & WPAN_PHY_FLAG_CCA_ED_LEVEL) {
		if (nla_put_s32(msg, NL802154_ATTR_CCA_ED_LEVEL,
				rdev->wpan_phy.cca_ed_level))
			goto nla_put_failure;
	}

	if (nl802154_put_capabilities(msg, rdev))
		goto nla_put_failure;

	nl_cmds = nla_nest_start(msg, NL802154_ATTR_SUPPORTED_COMMANDS);
	if (!nl_cmds)
		goto nla_put_failure;

	i = 0;
#define CMD(op, n)							\
	do {								\
		if (rdev->ops->op) {					\
			i++;						\
			if (nla_put_u32(msg, i, NL802154_CMD_ ## n))	\
				goto nla_put_failure;			\
		}							\
	} while (0)

	CMD(add_virtual_intf, NEW_INTERFACE);
	CMD(del_virtual_intf, DEL_INTERFACE);
	CMD(set_channel, SET_CHANNEL);
	CMD(set_pan_id, SET_PAN_ID);
	CMD(set_short_addr, SET_SHORT_ADDR);
	CMD(set_backoff_exponent, SET_BACKOFF_EXPONENT);
	CMD(set_max_csma_backoffs, SET_MAX_CSMA_BACKOFFS);
	CMD(set_max_frame_retries, SET_MAX_FRAME_RETRIES);
	CMD(set_lbt_mode, SET_LBT_MODE);
	CMD(set_ackreq_default, SET_ACKREQ_DEFAULT);

	if (rdev->wpan_phy.flags & WPAN_PHY_FLAG_TXPOWER)
		CMD(set_tx_power, SET_TX_POWER);

	if (rdev->wpan_phy.flags & WPAN_PHY_FLAG_CCA_ED_LEVEL)
		CMD(set_cca_ed_level, SET_CCA_ED_LEVEL);

	if (rdev->wpan_phy.flags & WPAN_PHY_FLAG_CCA_MODE)
		CMD(set_cca_mode, SET_CCA_MODE);

#undef CMD
	nla_nest_end(msg, nl_cmds);

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

#ifdef CONFIG_IEEE802154_NL802154_EXPERIMENTAL
#include <net/ieee802154_netdev.h>

static int
ieee802154_llsec_send_key_id(struct sk_buff *msg,
			     const struct ieee802154_llsec_key_id *desc)
{
	struct nlattr *nl_dev_addr;

	if (nla_put_u32(msg, NL802154_KEY_ID_ATTR_MODE, desc->mode))
		return -ENOBUFS;

	switch (desc->mode) {
	case NL802154_KEY_ID_MODE_IMPLICIT:
		nl_dev_addr = nla_nest_start(msg, NL802154_KEY_ID_ATTR_IMPLICIT);
		if (!nl_dev_addr)
			return -ENOBUFS;

		if (nla_put_le16(msg, NL802154_DEV_ADDR_ATTR_PAN_ID,
				 desc->device_addr.pan_id) ||
		    nla_put_u32(msg,  NL802154_DEV_ADDR_ATTR_MODE,
				desc->device_addr.mode))
			return -ENOBUFS;

		switch (desc->device_addr.mode) {
		case NL802154_DEV_ADDR_SHORT:
			if (nla_put_le16(msg, NL802154_DEV_ADDR_ATTR_SHORT,
					 desc->device_addr.short_addr))
				return -ENOBUFS;
			break;
		case NL802154_DEV_ADDR_EXTENDED:
			if (nla_put_le64(msg, NL802154_DEV_ADDR_ATTR_EXTENDED,
					 desc->device_addr.extended_addr,
					 NL802154_DEV_ADDR_ATTR_PAD))
				return -ENOBUFS;
			break;
		default:
			/* userspace should handle unknown */
			break;
		}

		nla_nest_end(msg, nl_dev_addr);
		break;
	case NL802154_KEY_ID_MODE_INDEX:
		break;
	case NL802154_KEY_ID_MODE_INDEX_SHORT:
		/* TODO renmae short_source? */
		if (nla_put_le32(msg, NL802154_KEY_ID_ATTR_SOURCE_SHORT,
				 desc->short_source))
			return -ENOBUFS;
		break;
	case NL802154_KEY_ID_MODE_INDEX_EXTENDED:
		if (nla_put_le64(msg, NL802154_KEY_ID_ATTR_SOURCE_EXTENDED,
				 desc->extended_source,
				 NL802154_KEY_ID_ATTR_PAD))
			return -ENOBUFS;
		break;
	default:
		/* userspace should handle unknown */
		break;
	}

	/* TODO key_id to key_idx ? Check naming */
	if (desc->mode != NL802154_KEY_ID_MODE_IMPLICIT) {
		if (nla_put_u8(msg, NL802154_KEY_ID_ATTR_INDEX, desc->id))
			return -ENOBUFS;
	}

	return 0;
}

static int nl802154_get_llsec_params(struct sk_buff *msg,
				     struct cfg802154_registered_device *rdev,
				     struct wpan_dev *wpan_dev)
{
	struct nlattr *nl_key_id;
	struct ieee802154_llsec_params params;
	int ret;

	ret = rdev_get_llsec_params(rdev, wpan_dev, &params);
	if (ret < 0)
		return ret;

	if (nla_put_u8(msg, NL802154_ATTR_SEC_ENABLED, params.enabled) ||
	    nla_put_u32(msg, NL802154_ATTR_SEC_OUT_LEVEL, params.out_level) ||
	    nla_put_be32(msg, NL802154_ATTR_SEC_FRAME_COUNTER,
			 params.frame_counter))
		return -ENOBUFS;

	nl_key_id = nla_nest_start(msg, NL802154_ATTR_SEC_OUT_KEY_ID);
	if (!nl_key_id)
		return -ENOBUFS;

	ret = ieee802154_llsec_send_key_id(msg, &params.out_key);
	if (ret < 0)
		return ret;

	nla_nest_end(msg, nl_key_id);

	return 0;
}
#endif /* CONFIG_IEEE802154_NL802154_EXPERIMENTAL */

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
	    nla_put_u64_64bit(msg, NL802154_ATTR_WPAN_DEV,
			      wpan_dev_id(wpan_dev), NL802154_ATTR_PAD) ||
	    nla_put_u32(msg, NL802154_ATTR_GENERATION,
			rdev->devlist_generation ^
			(cfg802154_rdev_list_generation << 2)))
		goto nla_put_failure;

	/* address settings */
	if (nla_put_le64(msg, NL802154_ATTR_EXTENDED_ADDR,
			 wpan_dev->extended_addr,
			 NL802154_ATTR_PAD) ||
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

	/* ackreq default behaviour */
	if (nla_put_u8(msg, NL802154_ATTR_ACKREQ_DEFAULT, wpan_dev->ackreq))
		goto nla_put_failure;

#ifdef CONFIG_IEEE802154_NL802154_EXPERIMENTAL
	if (nl802154_get_llsec_params(msg, rdev, wpan_dev) < 0)
		goto nla_put_failure;
#endif /* CONFIG_IEEE802154_NL802154_EXPERIMENTAL */

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
		if (type > NL802154_IFTYPE_MAX ||
		    !(rdev->wpan_phy.supported.iftypes & BIT(type)))
			return -EINVAL;
	}

	if (info->attrs[NL802154_ATTR_EXTENDED_ADDR])
		extended_addr = nla_get_le64(info->attrs[NL802154_ATTR_EXTENDED_ADDR]);

	if (!rdev->ops->add_virtual_intf)
		return -EOPNOTSUPP;

	return rdev_add_virtual_intf(rdev,
				     nla_data(info->attrs[NL802154_ATTR_IFNAME]),
				     NET_NAME_USER, type, extended_addr);
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
	if (page > IEEE802154_MAX_PAGE || channel > IEEE802154_MAX_CHANNEL ||
	    !(rdev->wpan_phy.supported.channels[page] & BIT(channel)))
		return -EINVAL;

	return rdev_set_channel(rdev, page, channel);
}

static int nl802154_set_cca_mode(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct wpan_phy_cca cca;

	if (!(rdev->wpan_phy.flags & WPAN_PHY_FLAG_CCA_MODE))
		return -EOPNOTSUPP;

	if (!info->attrs[NL802154_ATTR_CCA_MODE])
		return -EINVAL;

	cca.mode = nla_get_u32(info->attrs[NL802154_ATTR_CCA_MODE]);
	/* checking 802.15.4 constraints */
	if (cca.mode < NL802154_CCA_ENERGY ||
	    cca.mode > NL802154_CCA_ATTR_MAX ||
	    !(rdev->wpan_phy.supported.cca_modes & BIT(cca.mode)))
		return -EINVAL;

	if (cca.mode == NL802154_CCA_ENERGY_CARRIER) {
		if (!info->attrs[NL802154_ATTR_CCA_OPT])
			return -EINVAL;

		cca.opt = nla_get_u32(info->attrs[NL802154_ATTR_CCA_OPT]);
		if (cca.opt > NL802154_CCA_OPT_ATTR_MAX ||
		    !(rdev->wpan_phy.supported.cca_opts & BIT(cca.opt)))
			return -EINVAL;
	}

	return rdev_set_cca_mode(rdev, &cca);
}

static int nl802154_set_cca_ed_level(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	s32 ed_level;
	int i;

	if (!(rdev->wpan_phy.flags & WPAN_PHY_FLAG_CCA_ED_LEVEL))
		return -EOPNOTSUPP;

	if (!info->attrs[NL802154_ATTR_CCA_ED_LEVEL])
		return -EINVAL;

	ed_level = nla_get_s32(info->attrs[NL802154_ATTR_CCA_ED_LEVEL]);

	for (i = 0; i < rdev->wpan_phy.supported.cca_ed_levels_size; i++) {
		if (ed_level == rdev->wpan_phy.supported.cca_ed_levels[i])
			return rdev_set_cca_ed_level(rdev, ed_level);
	}

	return -EINVAL;
}

static int nl802154_set_tx_power(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	s32 power;
	int i;

	if (!(rdev->wpan_phy.flags & WPAN_PHY_FLAG_TXPOWER))
		return -EOPNOTSUPP;

	if (!info->attrs[NL802154_ATTR_TX_POWER])
		return -EINVAL;

	power = nla_get_s32(info->attrs[NL802154_ATTR_TX_POWER]);

	for (i = 0; i < rdev->wpan_phy.supported.tx_powers_size; i++) {
		if (power == rdev->wpan_phy.supported.tx_powers[i])
			return rdev_set_tx_power(rdev, power);
	}

	return -EINVAL;
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

	if (wpan_dev->lowpan_dev) {
		if (netif_running(wpan_dev->lowpan_dev))
			return -EBUSY;
	}

	/* don't change address fields on monitor */
	if (wpan_dev->iftype == NL802154_IFTYPE_MONITOR ||
	    !info->attrs[NL802154_ATTR_PAN_ID])
		return -EINVAL;

	pan_id = nla_get_le16(info->attrs[NL802154_ATTR_PAN_ID]);

	/* TODO
	 * I am not sure about to check here on broadcast pan_id.
	 * Broadcast is a valid setting, comment from 802.15.4:
	 * If this value is 0xffff, the device is not associated.
	 *
	 * This could useful to simple deassociate an device.
	 */
	if (pan_id == cpu_to_le16(IEEE802154_PAN_ID_BROADCAST))
		return -EINVAL;

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

	if (wpan_dev->lowpan_dev) {
		if (netif_running(wpan_dev->lowpan_dev))
			return -EBUSY;
	}

	/* don't change address fields on monitor */
	if (wpan_dev->iftype == NL802154_IFTYPE_MONITOR ||
	    !info->attrs[NL802154_ATTR_SHORT_ADDR])
		return -EINVAL;

	short_addr = nla_get_le16(info->attrs[NL802154_ATTR_SHORT_ADDR]);

	/* TODO
	 * I am not sure about to check here on broadcast short_addr.
	 * Broadcast is a valid setting, comment from 802.15.4:
	 * A value of 0xfffe indicates that the device has
	 * associated but has not been allocated an address. A
	 * value of 0xffff indicates that the device does not
	 * have a short address.
	 *
	 * I think we should allow to set these settings but
	 * don't allow to allow socket communication with it.
	 */
	if (short_addr == cpu_to_le16(IEEE802154_ADDR_SHORT_UNSPEC) ||
	    short_addr == cpu_to_le16(IEEE802154_ADDR_SHORT_BROADCAST))
		return -EINVAL;

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
	if (min_be < rdev->wpan_phy.supported.min_minbe ||
	    min_be > rdev->wpan_phy.supported.max_minbe ||
	    max_be < rdev->wpan_phy.supported.min_maxbe ||
	    max_be > rdev->wpan_phy.supported.max_maxbe ||
	    min_be > max_be)
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
	if (max_csma_backoffs < rdev->wpan_phy.supported.min_csma_backoffs ||
	    max_csma_backoffs > rdev->wpan_phy.supported.max_csma_backoffs)
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
	if (max_frame_retries < rdev->wpan_phy.supported.min_frame_retries ||
	    max_frame_retries > rdev->wpan_phy.supported.max_frame_retries)
		return -EINVAL;

	return rdev_set_max_frame_retries(rdev, wpan_dev, max_frame_retries);
}

static int nl802154_set_lbt_mode(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wpan_dev *wpan_dev = dev->ieee802154_ptr;
	int mode;

	if (netif_running(dev))
		return -EBUSY;

	if (!info->attrs[NL802154_ATTR_LBT_MODE])
		return -EINVAL;

	mode = nla_get_u8(info->attrs[NL802154_ATTR_LBT_MODE]);

	if (mode != 0 && mode != 1)
		return -EINVAL;

	if (!wpan_phy_supported_bool(mode, rdev->wpan_phy.supported.lbt))
		return -EINVAL;

	return rdev_set_lbt_mode(rdev, wpan_dev, mode);
}

static int
nl802154_set_ackreq_default(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wpan_dev *wpan_dev = dev->ieee802154_ptr;
	int ackreq;

	if (netif_running(dev))
		return -EBUSY;

	if (!info->attrs[NL802154_ATTR_ACKREQ_DEFAULT])
		return -EINVAL;

	ackreq = nla_get_u8(info->attrs[NL802154_ATTR_ACKREQ_DEFAULT]);

	if (ackreq != 0 && ackreq != 1)
		return -EINVAL;

	return rdev_set_ackreq_default(rdev, wpan_dev, ackreq);
}

#ifdef CONFIG_IEEE802154_NL802154_EXPERIMENTAL
static const struct nla_policy nl802154_dev_addr_policy[NL802154_DEV_ADDR_ATTR_MAX + 1] = {
	[NL802154_DEV_ADDR_ATTR_PAN_ID] = { .type = NLA_U16 },
	[NL802154_DEV_ADDR_ATTR_MODE] = { .type = NLA_U32 },
	[NL802154_DEV_ADDR_ATTR_SHORT] = { .type = NLA_U16 },
	[NL802154_DEV_ADDR_ATTR_EXTENDED] = { .type = NLA_U64 },
};

static int
ieee802154_llsec_parse_dev_addr(struct nlattr *nla,
				struct ieee802154_addr *addr)
{
	struct nlattr *attrs[NL802154_DEV_ADDR_ATTR_MAX + 1];

	if (!nla || nla_parse_nested(attrs, NL802154_DEV_ADDR_ATTR_MAX, nla,
				     nl802154_dev_addr_policy))
		return -EINVAL;

	if (!attrs[NL802154_DEV_ADDR_ATTR_PAN_ID] ||
	    !attrs[NL802154_DEV_ADDR_ATTR_MODE] ||
	    !(attrs[NL802154_DEV_ADDR_ATTR_SHORT] ||
	      attrs[NL802154_DEV_ADDR_ATTR_EXTENDED]))
		return -EINVAL;

	addr->pan_id = nla_get_le16(attrs[NL802154_DEV_ADDR_ATTR_PAN_ID]);
	addr->mode = nla_get_u32(attrs[NL802154_DEV_ADDR_ATTR_MODE]);
	switch (addr->mode) {
	case NL802154_DEV_ADDR_SHORT:
		addr->short_addr = nla_get_le16(attrs[NL802154_DEV_ADDR_ATTR_SHORT]);
		break;
	case NL802154_DEV_ADDR_EXTENDED:
		addr->extended_addr = nla_get_le64(attrs[NL802154_DEV_ADDR_ATTR_EXTENDED]);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct nla_policy nl802154_key_id_policy[NL802154_KEY_ID_ATTR_MAX + 1] = {
	[NL802154_KEY_ID_ATTR_MODE] = { .type = NLA_U32 },
	[NL802154_KEY_ID_ATTR_INDEX] = { .type = NLA_U8 },
	[NL802154_KEY_ID_ATTR_IMPLICIT] = { .type = NLA_NESTED },
	[NL802154_KEY_ID_ATTR_SOURCE_SHORT] = { .type = NLA_U32 },
	[NL802154_KEY_ID_ATTR_SOURCE_EXTENDED] = { .type = NLA_U64 },
};

static int
ieee802154_llsec_parse_key_id(struct nlattr *nla,
			      struct ieee802154_llsec_key_id *desc)
{
	struct nlattr *attrs[NL802154_KEY_ID_ATTR_MAX + 1];

	if (!nla || nla_parse_nested(attrs, NL802154_KEY_ID_ATTR_MAX, nla,
				     nl802154_key_id_policy))
		return -EINVAL;

	if (!attrs[NL802154_KEY_ID_ATTR_MODE])
		return -EINVAL;

	desc->mode = nla_get_u32(attrs[NL802154_KEY_ID_ATTR_MODE]);
	switch (desc->mode) {
	case NL802154_KEY_ID_MODE_IMPLICIT:
		if (!attrs[NL802154_KEY_ID_ATTR_IMPLICIT])
			return -EINVAL;

		if (ieee802154_llsec_parse_dev_addr(attrs[NL802154_KEY_ID_ATTR_IMPLICIT],
						    &desc->device_addr) < 0)
			return -EINVAL;
		break;
	case NL802154_KEY_ID_MODE_INDEX:
		break;
	case NL802154_KEY_ID_MODE_INDEX_SHORT:
		if (!attrs[NL802154_KEY_ID_ATTR_SOURCE_SHORT])
			return -EINVAL;

		desc->short_source = nla_get_le32(attrs[NL802154_KEY_ID_ATTR_SOURCE_SHORT]);
		break;
	case NL802154_KEY_ID_MODE_INDEX_EXTENDED:
		if (!attrs[NL802154_KEY_ID_ATTR_SOURCE_EXTENDED])
			return -EINVAL;

		desc->extended_source = nla_get_le64(attrs[NL802154_KEY_ID_ATTR_SOURCE_EXTENDED]);
		break;
	default:
		return -EINVAL;
	}

	if (desc->mode != NL802154_KEY_ID_MODE_IMPLICIT) {
		if (!attrs[NL802154_KEY_ID_ATTR_INDEX])
			return -EINVAL;

		/* TODO change id to idx */
		desc->id = nla_get_u8(attrs[NL802154_KEY_ID_ATTR_INDEX]);
	}

	return 0;
}

static int nl802154_set_llsec_params(struct sk_buff *skb,
				     struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wpan_dev *wpan_dev = dev->ieee802154_ptr;
	struct ieee802154_llsec_params params;
	u32 changed = 0;
	int ret;

	if (info->attrs[NL802154_ATTR_SEC_ENABLED]) {
		u8 enabled;

		enabled = nla_get_u8(info->attrs[NL802154_ATTR_SEC_ENABLED]);
		if (enabled != 0 && enabled != 1)
			return -EINVAL;

		params.enabled = nla_get_u8(info->attrs[NL802154_ATTR_SEC_ENABLED]);
		changed |= IEEE802154_LLSEC_PARAM_ENABLED;
	}

	if (info->attrs[NL802154_ATTR_SEC_OUT_KEY_ID]) {
		ret = ieee802154_llsec_parse_key_id(info->attrs[NL802154_ATTR_SEC_OUT_KEY_ID],
						    &params.out_key);
		if (ret < 0)
			return ret;

		changed |= IEEE802154_LLSEC_PARAM_OUT_KEY;
	}

	if (info->attrs[NL802154_ATTR_SEC_OUT_LEVEL]) {
		params.out_level = nla_get_u32(info->attrs[NL802154_ATTR_SEC_OUT_LEVEL]);
		if (params.out_level > NL802154_SECLEVEL_MAX)
			return -EINVAL;

		changed |= IEEE802154_LLSEC_PARAM_OUT_LEVEL;
	}

	if (info->attrs[NL802154_ATTR_SEC_FRAME_COUNTER]) {
		params.frame_counter = nla_get_be32(info->attrs[NL802154_ATTR_SEC_FRAME_COUNTER]);
		changed |= IEEE802154_LLSEC_PARAM_FRAME_COUNTER;
	}

	return rdev_set_llsec_params(rdev, wpan_dev, &params, changed);
}

static int nl802154_send_key(struct sk_buff *msg, u32 cmd, u32 portid,
			     u32 seq, int flags,
			     struct cfg802154_registered_device *rdev,
			     struct net_device *dev,
			     const struct ieee802154_llsec_key_entry *key)
{
	void *hdr;
	u32 commands[NL802154_CMD_FRAME_NR_IDS / 32];
	struct nlattr *nl_key, *nl_key_id;

	hdr = nl802154hdr_put(msg, portid, seq, flags, cmd);
	if (!hdr)
		return -1;

	if (nla_put_u32(msg, NL802154_ATTR_IFINDEX, dev->ifindex))
		goto nla_put_failure;

	nl_key = nla_nest_start(msg, NL802154_ATTR_SEC_KEY);
	if (!nl_key)
		goto nla_put_failure;

	nl_key_id = nla_nest_start(msg, NL802154_KEY_ATTR_ID);
	if (!nl_key_id)
		goto nla_put_failure;

	if (ieee802154_llsec_send_key_id(msg, &key->id) < 0)
		goto nla_put_failure;

	nla_nest_end(msg, nl_key_id);

	if (nla_put_u8(msg, NL802154_KEY_ATTR_USAGE_FRAMES,
		       key->key->frame_types))
		goto nla_put_failure;

	if (key->key->frame_types & BIT(NL802154_FRAME_CMD)) {
		/* TODO for each nested */
		memset(commands, 0, sizeof(commands));
		commands[7] = key->key->cmd_frame_ids;
		if (nla_put(msg, NL802154_KEY_ATTR_USAGE_CMDS,
			    sizeof(commands), commands))
			goto nla_put_failure;
	}

	if (nla_put(msg, NL802154_KEY_ATTR_BYTES, NL802154_KEY_SIZE,
		    key->key->key))
		goto nla_put_failure;

	nla_nest_end(msg, nl_key);
	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int
nl802154_dump_llsec_key(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct cfg802154_registered_device *rdev = NULL;
	struct ieee802154_llsec_key_entry *key;
	struct ieee802154_llsec_table *table;
	struct wpan_dev *wpan_dev;
	int err;

	err = nl802154_prepare_wpan_dev_dump(skb, cb, &rdev, &wpan_dev);
	if (err)
		return err;

	if (!wpan_dev->netdev) {
		err = -EINVAL;
		goto out_err;
	}

	rdev_lock_llsec_table(rdev, wpan_dev);
	rdev_get_llsec_table(rdev, wpan_dev, &table);

	/* TODO make it like station dump */
	if (cb->args[2])
		goto out;

	list_for_each_entry(key, &table->keys, list) {
		if (nl802154_send_key(skb, NL802154_CMD_NEW_SEC_KEY,
				      NETLINK_CB(cb->skb).portid,
				      cb->nlh->nlmsg_seq, NLM_F_MULTI,
				      rdev, wpan_dev->netdev, key) < 0) {
			/* TODO */
			err = -EIO;
			rdev_unlock_llsec_table(rdev, wpan_dev);
			goto out_err;
		}
	}

	cb->args[2] = 1;

out:
	rdev_unlock_llsec_table(rdev, wpan_dev);
	err = skb->len;
out_err:
	nl802154_finish_wpan_dev_dump(rdev);

	return err;
}

static const struct nla_policy nl802154_key_policy[NL802154_KEY_ATTR_MAX + 1] = {
	[NL802154_KEY_ATTR_ID] = { NLA_NESTED },
	/* TODO handle it as for_each_nested and NLA_FLAG? */
	[NL802154_KEY_ATTR_USAGE_FRAMES] = { NLA_U8 },
	/* TODO handle it as for_each_nested, not static array? */
	[NL802154_KEY_ATTR_USAGE_CMDS] = { .len = NL802154_CMD_FRAME_NR_IDS / 8 },
	[NL802154_KEY_ATTR_BYTES] = { .len = NL802154_KEY_SIZE },
};

static int nl802154_add_llsec_key(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wpan_dev *wpan_dev = dev->ieee802154_ptr;
	struct nlattr *attrs[NL802154_KEY_ATTR_MAX + 1];
	struct ieee802154_llsec_key key = { };
	struct ieee802154_llsec_key_id id = { };
	u32 commands[NL802154_CMD_FRAME_NR_IDS / 32] = { };

	if (nla_parse_nested(attrs, NL802154_KEY_ATTR_MAX,
			     info->attrs[NL802154_ATTR_SEC_KEY],
			     nl802154_key_policy))
		return -EINVAL;

	if (!attrs[NL802154_KEY_ATTR_USAGE_FRAMES] ||
	    !attrs[NL802154_KEY_ATTR_BYTES])
		return -EINVAL;

	if (ieee802154_llsec_parse_key_id(attrs[NL802154_KEY_ATTR_ID], &id) < 0)
		return -ENOBUFS;

	key.frame_types = nla_get_u8(attrs[NL802154_KEY_ATTR_USAGE_FRAMES]);
	if (key.frame_types > BIT(NL802154_FRAME_MAX) ||
	    ((key.frame_types & BIT(NL802154_FRAME_CMD)) &&
	     !attrs[NL802154_KEY_ATTR_USAGE_CMDS]))
		return -EINVAL;

	if (attrs[NL802154_KEY_ATTR_USAGE_CMDS]) {
		/* TODO for each nested */
		nla_memcpy(commands, attrs[NL802154_KEY_ATTR_USAGE_CMDS],
			   NL802154_CMD_FRAME_NR_IDS / 8);

		/* TODO understand the -EINVAL logic here? last condition */
		if (commands[0] || commands[1] || commands[2] || commands[3] ||
		    commands[4] || commands[5] || commands[6] ||
		    commands[7] > BIT(NL802154_CMD_FRAME_MAX))
			return -EINVAL;

		key.cmd_frame_ids = commands[7];
	} else {
		key.cmd_frame_ids = 0;
	}

	nla_memcpy(key.key, attrs[NL802154_KEY_ATTR_BYTES], NL802154_KEY_SIZE);

	if (ieee802154_llsec_parse_key_id(attrs[NL802154_KEY_ATTR_ID], &id) < 0)
		return -ENOBUFS;

	return rdev_add_llsec_key(rdev, wpan_dev, &id, &key);
}

static int nl802154_del_llsec_key(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wpan_dev *wpan_dev = dev->ieee802154_ptr;
	struct nlattr *attrs[NL802154_KEY_ATTR_MAX + 1];
	struct ieee802154_llsec_key_id id;

	if (nla_parse_nested(attrs, NL802154_KEY_ATTR_MAX,
			     info->attrs[NL802154_ATTR_SEC_KEY],
			     nl802154_key_policy))
		return -EINVAL;

	if (ieee802154_llsec_parse_key_id(attrs[NL802154_KEY_ATTR_ID], &id) < 0)
		return -ENOBUFS;

	return rdev_del_llsec_key(rdev, wpan_dev, &id);
}

static int nl802154_send_device(struct sk_buff *msg, u32 cmd, u32 portid,
				u32 seq, int flags,
				struct cfg802154_registered_device *rdev,
				struct net_device *dev,
				const struct ieee802154_llsec_device *dev_desc)
{
	void *hdr;
	struct nlattr *nl_device;

	hdr = nl802154hdr_put(msg, portid, seq, flags, cmd);
	if (!hdr)
		return -1;

	if (nla_put_u32(msg, NL802154_ATTR_IFINDEX, dev->ifindex))
		goto nla_put_failure;

	nl_device = nla_nest_start(msg, NL802154_ATTR_SEC_DEVICE);
	if (!nl_device)
		goto nla_put_failure;

	if (nla_put_u32(msg, NL802154_DEV_ATTR_FRAME_COUNTER,
			dev_desc->frame_counter) ||
	    nla_put_le16(msg, NL802154_DEV_ATTR_PAN_ID, dev_desc->pan_id) ||
	    nla_put_le16(msg, NL802154_DEV_ATTR_SHORT_ADDR,
			 dev_desc->short_addr) ||
	    nla_put_le64(msg, NL802154_DEV_ATTR_EXTENDED_ADDR,
			 dev_desc->hwaddr, NL802154_DEV_ATTR_PAD) ||
	    nla_put_u8(msg, NL802154_DEV_ATTR_SECLEVEL_EXEMPT,
		       dev_desc->seclevel_exempt) ||
	    nla_put_u32(msg, NL802154_DEV_ATTR_KEY_MODE, dev_desc->key_mode))
		goto nla_put_failure;

	nla_nest_end(msg, nl_device);
	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int
nl802154_dump_llsec_dev(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct cfg802154_registered_device *rdev = NULL;
	struct ieee802154_llsec_device *dev;
	struct ieee802154_llsec_table *table;
	struct wpan_dev *wpan_dev;
	int err;

	err = nl802154_prepare_wpan_dev_dump(skb, cb, &rdev, &wpan_dev);
	if (err)
		return err;

	if (!wpan_dev->netdev) {
		err = -EINVAL;
		goto out_err;
	}

	rdev_lock_llsec_table(rdev, wpan_dev);
	rdev_get_llsec_table(rdev, wpan_dev, &table);

	/* TODO make it like station dump */
	if (cb->args[2])
		goto out;

	list_for_each_entry(dev, &table->devices, list) {
		if (nl802154_send_device(skb, NL802154_CMD_NEW_SEC_LEVEL,
					 NETLINK_CB(cb->skb).portid,
					 cb->nlh->nlmsg_seq, NLM_F_MULTI,
					 rdev, wpan_dev->netdev, dev) < 0) {
			/* TODO */
			err = -EIO;
			rdev_unlock_llsec_table(rdev, wpan_dev);
			goto out_err;
		}
	}

	cb->args[2] = 1;

out:
	rdev_unlock_llsec_table(rdev, wpan_dev);
	err = skb->len;
out_err:
	nl802154_finish_wpan_dev_dump(rdev);

	return err;
}

static const struct nla_policy nl802154_dev_policy[NL802154_DEV_ATTR_MAX + 1] = {
	[NL802154_DEV_ATTR_FRAME_COUNTER] = { NLA_U32 },
	[NL802154_DEV_ATTR_PAN_ID] = { .type = NLA_U16 },
	[NL802154_DEV_ATTR_SHORT_ADDR] = { .type = NLA_U16 },
	[NL802154_DEV_ATTR_EXTENDED_ADDR] = { .type = NLA_U64 },
	[NL802154_DEV_ATTR_SECLEVEL_EXEMPT] = { NLA_U8 },
	[NL802154_DEV_ATTR_KEY_MODE] = { NLA_U32 },
};

static int
ieee802154_llsec_parse_device(struct nlattr *nla,
			      struct ieee802154_llsec_device *dev)
{
	struct nlattr *attrs[NL802154_DEV_ATTR_MAX + 1];

	if (!nla || nla_parse_nested(attrs, NL802154_DEV_ATTR_MAX, nla,
				     nl802154_dev_policy))
		return -EINVAL;

	memset(dev, 0, sizeof(*dev));

	if (!attrs[NL802154_DEV_ATTR_FRAME_COUNTER] ||
	    !attrs[NL802154_DEV_ATTR_PAN_ID] ||
	    !attrs[NL802154_DEV_ATTR_SHORT_ADDR] ||
	    !attrs[NL802154_DEV_ATTR_EXTENDED_ADDR] ||
	    !attrs[NL802154_DEV_ATTR_SECLEVEL_EXEMPT] ||
	    !attrs[NL802154_DEV_ATTR_KEY_MODE])
		return -EINVAL;

	/* TODO be32 */
	dev->frame_counter = nla_get_u32(attrs[NL802154_DEV_ATTR_FRAME_COUNTER]);
	dev->pan_id = nla_get_le16(attrs[NL802154_DEV_ATTR_PAN_ID]);
	dev->short_addr = nla_get_le16(attrs[NL802154_DEV_ATTR_SHORT_ADDR]);
	/* TODO rename hwaddr to extended_addr */
	dev->hwaddr = nla_get_le64(attrs[NL802154_DEV_ATTR_EXTENDED_ADDR]);
	dev->seclevel_exempt = nla_get_u8(attrs[NL802154_DEV_ATTR_SECLEVEL_EXEMPT]);
	dev->key_mode = nla_get_u32(attrs[NL802154_DEV_ATTR_KEY_MODE]);

	if (dev->key_mode > NL802154_DEVKEY_MAX ||
	    (dev->seclevel_exempt != 0 && dev->seclevel_exempt != 1))
		return -EINVAL;

	return 0;
}

static int nl802154_add_llsec_dev(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wpan_dev *wpan_dev = dev->ieee802154_ptr;
	struct ieee802154_llsec_device dev_desc;

	if (ieee802154_llsec_parse_device(info->attrs[NL802154_ATTR_SEC_DEVICE],
					  &dev_desc) < 0)
		return -EINVAL;

	return rdev_add_device(rdev, wpan_dev, &dev_desc);
}

static int nl802154_del_llsec_dev(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wpan_dev *wpan_dev = dev->ieee802154_ptr;
	struct nlattr *attrs[NL802154_DEV_ATTR_MAX + 1];
	__le64 extended_addr;

	if (nla_parse_nested(attrs, NL802154_DEV_ATTR_MAX,
			     info->attrs[NL802154_ATTR_SEC_DEVICE],
			     nl802154_dev_policy))
		return -EINVAL;

	if (!attrs[NL802154_DEV_ATTR_EXTENDED_ADDR])
		return -EINVAL;

	extended_addr = nla_get_le64(attrs[NL802154_DEV_ATTR_EXTENDED_ADDR]);
	return rdev_del_device(rdev, wpan_dev, extended_addr);
}

static int nl802154_send_devkey(struct sk_buff *msg, u32 cmd, u32 portid,
				u32 seq, int flags,
				struct cfg802154_registered_device *rdev,
				struct net_device *dev, __le64 extended_addr,
				const struct ieee802154_llsec_device_key *devkey)
{
	void *hdr;
	struct nlattr *nl_devkey, *nl_key_id;

	hdr = nl802154hdr_put(msg, portid, seq, flags, cmd);
	if (!hdr)
		return -1;

	if (nla_put_u32(msg, NL802154_ATTR_IFINDEX, dev->ifindex))
		goto nla_put_failure;

	nl_devkey = nla_nest_start(msg, NL802154_ATTR_SEC_DEVKEY);
	if (!nl_devkey)
		goto nla_put_failure;

	if (nla_put_le64(msg, NL802154_DEVKEY_ATTR_EXTENDED_ADDR,
			 extended_addr, NL802154_DEVKEY_ATTR_PAD) ||
	    nla_put_u32(msg, NL802154_DEVKEY_ATTR_FRAME_COUNTER,
			devkey->frame_counter))
		goto nla_put_failure;

	nl_key_id = nla_nest_start(msg, NL802154_DEVKEY_ATTR_ID);
	if (!nl_key_id)
		goto nla_put_failure;

	if (ieee802154_llsec_send_key_id(msg, &devkey->key_id) < 0)
		goto nla_put_failure;

	nla_nest_end(msg, nl_key_id);
	nla_nest_end(msg, nl_devkey);
	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int
nl802154_dump_llsec_devkey(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct cfg802154_registered_device *rdev = NULL;
	struct ieee802154_llsec_device_key *kpos;
	struct ieee802154_llsec_device *dpos;
	struct ieee802154_llsec_table *table;
	struct wpan_dev *wpan_dev;
	int err;

	err = nl802154_prepare_wpan_dev_dump(skb, cb, &rdev, &wpan_dev);
	if (err)
		return err;

	if (!wpan_dev->netdev) {
		err = -EINVAL;
		goto out_err;
	}

	rdev_lock_llsec_table(rdev, wpan_dev);
	rdev_get_llsec_table(rdev, wpan_dev, &table);

	/* TODO make it like station dump */
	if (cb->args[2])
		goto out;

	/* TODO look if remove devkey and do some nested attribute */
	list_for_each_entry(dpos, &table->devices, list) {
		list_for_each_entry(kpos, &dpos->keys, list) {
			if (nl802154_send_devkey(skb,
						 NL802154_CMD_NEW_SEC_LEVEL,
						 NETLINK_CB(cb->skb).portid,
						 cb->nlh->nlmsg_seq,
						 NLM_F_MULTI, rdev,
						 wpan_dev->netdev,
						 dpos->hwaddr,
						 kpos) < 0) {
				/* TODO */
				err = -EIO;
				rdev_unlock_llsec_table(rdev, wpan_dev);
				goto out_err;
			}
		}
	}

	cb->args[2] = 1;

out:
	rdev_unlock_llsec_table(rdev, wpan_dev);
	err = skb->len;
out_err:
	nl802154_finish_wpan_dev_dump(rdev);

	return err;
}

static const struct nla_policy nl802154_devkey_policy[NL802154_DEVKEY_ATTR_MAX + 1] = {
	[NL802154_DEVKEY_ATTR_FRAME_COUNTER] = { NLA_U32 },
	[NL802154_DEVKEY_ATTR_EXTENDED_ADDR] = { NLA_U64 },
	[NL802154_DEVKEY_ATTR_ID] = { NLA_NESTED },
};

static int nl802154_add_llsec_devkey(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wpan_dev *wpan_dev = dev->ieee802154_ptr;
	struct nlattr *attrs[NL802154_DEVKEY_ATTR_MAX + 1];
	struct ieee802154_llsec_device_key key;
	__le64 extended_addr;

	if (!info->attrs[NL802154_ATTR_SEC_DEVKEY] ||
	    nla_parse_nested(attrs, NL802154_DEVKEY_ATTR_MAX,
			     info->attrs[NL802154_ATTR_SEC_DEVKEY],
			     nl802154_devkey_policy) < 0)
		return -EINVAL;

	if (!attrs[NL802154_DEVKEY_ATTR_FRAME_COUNTER] ||
	    !attrs[NL802154_DEVKEY_ATTR_EXTENDED_ADDR])
		return -EINVAL;

	/* TODO change key.id ? */
	if (ieee802154_llsec_parse_key_id(attrs[NL802154_DEVKEY_ATTR_ID],
					  &key.key_id) < 0)
		return -ENOBUFS;

	/* TODO be32 */
	key.frame_counter = nla_get_u32(attrs[NL802154_DEVKEY_ATTR_FRAME_COUNTER]);
	/* TODO change naming hwaddr -> extended_addr
	 * check unique identifier short+pan OR extended_addr
	 */
	extended_addr = nla_get_le64(attrs[NL802154_DEVKEY_ATTR_EXTENDED_ADDR]);
	return rdev_add_devkey(rdev, wpan_dev, extended_addr, &key);
}

static int nl802154_del_llsec_devkey(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wpan_dev *wpan_dev = dev->ieee802154_ptr;
	struct nlattr *attrs[NL802154_DEVKEY_ATTR_MAX + 1];
	struct ieee802154_llsec_device_key key;
	__le64 extended_addr;

	if (nla_parse_nested(attrs, NL802154_DEVKEY_ATTR_MAX,
			     info->attrs[NL802154_ATTR_SEC_DEVKEY],
			     nl802154_devkey_policy))
		return -EINVAL;

	if (!attrs[NL802154_DEVKEY_ATTR_EXTENDED_ADDR])
		return -EINVAL;

	/* TODO change key.id ? */
	if (ieee802154_llsec_parse_key_id(attrs[NL802154_DEVKEY_ATTR_ID],
					  &key.key_id) < 0)
		return -ENOBUFS;

	/* TODO change naming hwaddr -> extended_addr
	 * check unique identifier short+pan OR extended_addr
	 */
	extended_addr = nla_get_le64(attrs[NL802154_DEVKEY_ATTR_EXTENDED_ADDR]);
	return rdev_del_devkey(rdev, wpan_dev, extended_addr, &key);
}

static int nl802154_send_seclevel(struct sk_buff *msg, u32 cmd, u32 portid,
				  u32 seq, int flags,
				  struct cfg802154_registered_device *rdev,
				  struct net_device *dev,
				  const struct ieee802154_llsec_seclevel *sl)
{
	void *hdr;
	struct nlattr *nl_seclevel;

	hdr = nl802154hdr_put(msg, portid, seq, flags, cmd);
	if (!hdr)
		return -1;

	if (nla_put_u32(msg, NL802154_ATTR_IFINDEX, dev->ifindex))
		goto nla_put_failure;

	nl_seclevel = nla_nest_start(msg, NL802154_ATTR_SEC_LEVEL);
	if (!nl_seclevel)
		goto nla_put_failure;

	if (nla_put_u32(msg, NL802154_SECLEVEL_ATTR_FRAME, sl->frame_type) ||
	    nla_put_u32(msg, NL802154_SECLEVEL_ATTR_LEVELS, sl->sec_levels) ||
	    nla_put_u8(msg, NL802154_SECLEVEL_ATTR_DEV_OVERRIDE,
		       sl->device_override))
		goto nla_put_failure;

	if (sl->frame_type == NL802154_FRAME_CMD) {
		if (nla_put_u32(msg, NL802154_SECLEVEL_ATTR_CMD_FRAME,
				sl->cmd_frame_id))
			goto nla_put_failure;
	}

	nla_nest_end(msg, nl_seclevel);
	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int
nl802154_dump_llsec_seclevel(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct cfg802154_registered_device *rdev = NULL;
	struct ieee802154_llsec_seclevel *sl;
	struct ieee802154_llsec_table *table;
	struct wpan_dev *wpan_dev;
	int err;

	err = nl802154_prepare_wpan_dev_dump(skb, cb, &rdev, &wpan_dev);
	if (err)
		return err;

	if (!wpan_dev->netdev) {
		err = -EINVAL;
		goto out_err;
	}

	rdev_lock_llsec_table(rdev, wpan_dev);
	rdev_get_llsec_table(rdev, wpan_dev, &table);

	/* TODO make it like station dump */
	if (cb->args[2])
		goto out;

	list_for_each_entry(sl, &table->security_levels, list) {
		if (nl802154_send_seclevel(skb, NL802154_CMD_NEW_SEC_LEVEL,
					   NETLINK_CB(cb->skb).portid,
					   cb->nlh->nlmsg_seq, NLM_F_MULTI,
					   rdev, wpan_dev->netdev, sl) < 0) {
			/* TODO */
			err = -EIO;
			rdev_unlock_llsec_table(rdev, wpan_dev);
			goto out_err;
		}
	}

	cb->args[2] = 1;

out:
	rdev_unlock_llsec_table(rdev, wpan_dev);
	err = skb->len;
out_err:
	nl802154_finish_wpan_dev_dump(rdev);

	return err;
}

static const struct nla_policy nl802154_seclevel_policy[NL802154_SECLEVEL_ATTR_MAX + 1] = {
	[NL802154_SECLEVEL_ATTR_LEVELS] = { .type = NLA_U8 },
	[NL802154_SECLEVEL_ATTR_FRAME] = { .type = NLA_U32 },
	[NL802154_SECLEVEL_ATTR_CMD_FRAME] = { .type = NLA_U32 },
	[NL802154_SECLEVEL_ATTR_DEV_OVERRIDE] = { .type = NLA_U8 },
};

static int
llsec_parse_seclevel(struct nlattr *nla, struct ieee802154_llsec_seclevel *sl)
{
	struct nlattr *attrs[NL802154_SECLEVEL_ATTR_MAX + 1];

	if (!nla || nla_parse_nested(attrs, NL802154_SECLEVEL_ATTR_MAX, nla,
				     nl802154_seclevel_policy))
		return -EINVAL;

	memset(sl, 0, sizeof(*sl));

	if (!attrs[NL802154_SECLEVEL_ATTR_LEVELS] ||
	    !attrs[NL802154_SECLEVEL_ATTR_FRAME] ||
	    !attrs[NL802154_SECLEVEL_ATTR_DEV_OVERRIDE])
		return -EINVAL;

	sl->sec_levels = nla_get_u8(attrs[NL802154_SECLEVEL_ATTR_LEVELS]);
	sl->frame_type = nla_get_u32(attrs[NL802154_SECLEVEL_ATTR_FRAME]);
	sl->device_override = nla_get_u8(attrs[NL802154_SECLEVEL_ATTR_DEV_OVERRIDE]);
	if (sl->frame_type > NL802154_FRAME_MAX ||
	    (sl->device_override != 0 && sl->device_override != 1))
		return -EINVAL;

	if (sl->frame_type == NL802154_FRAME_CMD) {
		if (!attrs[NL802154_SECLEVEL_ATTR_CMD_FRAME])
			return -EINVAL;

		sl->cmd_frame_id = nla_get_u32(attrs[NL802154_SECLEVEL_ATTR_CMD_FRAME]);
		if (sl->cmd_frame_id > NL802154_CMD_FRAME_MAX)
			return -EINVAL;
	}

	return 0;
}

static int nl802154_add_llsec_seclevel(struct sk_buff *skb,
				       struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wpan_dev *wpan_dev = dev->ieee802154_ptr;
	struct ieee802154_llsec_seclevel sl;

	if (llsec_parse_seclevel(info->attrs[NL802154_ATTR_SEC_LEVEL],
				 &sl) < 0)
		return -EINVAL;

	return rdev_add_seclevel(rdev, wpan_dev, &sl);
}

static int nl802154_del_llsec_seclevel(struct sk_buff *skb,
				       struct genl_info *info)
{
	struct cfg802154_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wpan_dev *wpan_dev = dev->ieee802154_ptr;
	struct ieee802154_llsec_seclevel sl;

	if (!info->attrs[NL802154_ATTR_SEC_LEVEL] ||
	    llsec_parse_seclevel(info->attrs[NL802154_ATTR_SEC_LEVEL],
				 &sl) < 0)
		return -EINVAL;

	return rdev_del_seclevel(rdev, wpan_dev, &sl);
}
#endif /* CONFIG_IEEE802154_NL802154_EXPERIMENTAL */

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
		.cmd = NL802154_CMD_SET_CCA_ED_LEVEL,
		.doit = nl802154_set_cca_ed_level,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_WPAN_PHY |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_SET_TX_POWER,
		.doit = nl802154_set_tx_power,
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
	{
		.cmd = NL802154_CMD_SET_ACKREQ_DEFAULT,
		.doit = nl802154_set_ackreq_default,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_NETDEV |
				  NL802154_FLAG_NEED_RTNL,
	},
#ifdef CONFIG_IEEE802154_NL802154_EXPERIMENTAL
	{
		.cmd = NL802154_CMD_SET_SEC_PARAMS,
		.doit = nl802154_set_llsec_params,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_NETDEV |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_GET_SEC_KEY,
		/* TODO .doit by matching key id? */
		.dumpit = nl802154_dump_llsec_key,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_NETDEV |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_NEW_SEC_KEY,
		.doit = nl802154_add_llsec_key,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_NETDEV |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_DEL_SEC_KEY,
		.doit = nl802154_del_llsec_key,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_NETDEV |
				  NL802154_FLAG_NEED_RTNL,
	},
	/* TODO unique identifier must short+pan OR extended_addr */
	{
		.cmd = NL802154_CMD_GET_SEC_DEV,
		/* TODO .doit by matching extended_addr? */
		.dumpit = nl802154_dump_llsec_dev,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_NETDEV |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_NEW_SEC_DEV,
		.doit = nl802154_add_llsec_dev,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_NETDEV |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_DEL_SEC_DEV,
		.doit = nl802154_del_llsec_dev,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_NETDEV |
				  NL802154_FLAG_NEED_RTNL,
	},
	/* TODO remove complete devkey, put it as nested? */
	{
		.cmd = NL802154_CMD_GET_SEC_DEVKEY,
		/* TODO doit by matching ??? */
		.dumpit = nl802154_dump_llsec_devkey,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_NETDEV |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_NEW_SEC_DEVKEY,
		.doit = nl802154_add_llsec_devkey,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_NETDEV |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_DEL_SEC_DEVKEY,
		.doit = nl802154_del_llsec_devkey,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_NETDEV |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_GET_SEC_LEVEL,
		/* TODO .doit by matching frame_type? */
		.dumpit = nl802154_dump_llsec_seclevel,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_NETDEV |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_NEW_SEC_LEVEL,
		.doit = nl802154_add_llsec_seclevel,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_NETDEV |
				  NL802154_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL802154_CMD_DEL_SEC_LEVEL,
		/* TODO match frame_type only? */
		.doit = nl802154_del_llsec_seclevel,
		.policy = nl802154_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL802154_FLAG_NEED_NETDEV |
				  NL802154_FLAG_NEED_RTNL,
	},
#endif /* CONFIG_IEEE802154_NL802154_EXPERIMENTAL */
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

// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"
#include "bitset.h"

struct privflags_req_info {
	struct ethnl_req_info		base;
};

struct privflags_reply_data {
	struct ethnl_reply_data		base;
	const char			(*priv_flag_names)[ETH_GSTRING_LEN];
	unsigned int			n_priv_flags;
	u32				priv_flags;
};

#define PRIVFLAGS_REPDATA(__reply_base) \
	container_of(__reply_base, struct privflags_reply_data, base)

const struct nla_policy ethnl_privflags_get_policy[] = {
	[ETHTOOL_A_PRIVFLAGS_HEADER]		= { .type = NLA_NESTED },
};

static int ethnl_get_priv_flags_info(struct net_device *dev,
				     unsigned int *count,
				     const char (**names)[ETH_GSTRING_LEN])
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	int nflags;

	nflags = ops->get_sset_count(dev, ETH_SS_PRIV_FLAGS);
	if (nflags < 0)
		return nflags;

	if (names) {
		*names = kcalloc(nflags, ETH_GSTRING_LEN, GFP_KERNEL);
		if (!*names)
			return -ENOMEM;
		ops->get_strings(dev, ETH_SS_PRIV_FLAGS, (u8 *)*names);
	}

	/* We can pass more than 32 private flags to userspace via netlink but
	 * we cannot get more with ethtool_ops::get_priv_flags(). Note that we
	 * must not adjust nflags before allocating the space for flag names
	 * as the buffer must be large enough for all flags.
	 */
	if (WARN_ONCE(nflags > 32,
		      "device %s reports more than 32 private flags (%d)\n",
		      netdev_name(dev), nflags))
		nflags = 32;
	*count = nflags;

	return 0;
}

static int privflags_prepare_data(const struct ethnl_req_info *req_base,
				  struct ethnl_reply_data *reply_base,
				  struct genl_info *info)
{
	struct privflags_reply_data *data = PRIVFLAGS_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	const char (*names)[ETH_GSTRING_LEN];
	const struct ethtool_ops *ops;
	unsigned int nflags;
	int ret;

	ops = dev->ethtool_ops;
	if (!ops->get_priv_flags || !ops->get_sset_count || !ops->get_strings)
		return -EOPNOTSUPP;
	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;

	ret = ethnl_get_priv_flags_info(dev, &nflags, &names);
	if (ret < 0)
		goto out_ops;
	data->priv_flags = ops->get_priv_flags(dev);
	data->priv_flag_names = names;
	data->n_priv_flags = nflags;

out_ops:
	ethnl_ops_complete(dev);
	return ret;
}

static int privflags_reply_size(const struct ethnl_req_info *req_base,
				const struct ethnl_reply_data *reply_base)
{
	const struct privflags_reply_data *data = PRIVFLAGS_REPDATA(reply_base);
	bool compact = req_base->flags & ETHTOOL_FLAG_COMPACT_BITSETS;
	const u32 all_flags = ~(u32)0 >> (32 - data->n_priv_flags);

	return ethnl_bitset32_size(&data->priv_flags, &all_flags,
				   data->n_priv_flags,
				   data->priv_flag_names, compact);
}

static int privflags_fill_reply(struct sk_buff *skb,
				const struct ethnl_req_info *req_base,
				const struct ethnl_reply_data *reply_base)
{
	const struct privflags_reply_data *data = PRIVFLAGS_REPDATA(reply_base);
	bool compact = req_base->flags & ETHTOOL_FLAG_COMPACT_BITSETS;
	const u32 all_flags = ~(u32)0 >> (32 - data->n_priv_flags);

	return ethnl_put_bitset32(skb, ETHTOOL_A_PRIVFLAGS_FLAGS,
				  &data->priv_flags, &all_flags,
				  data->n_priv_flags, data->priv_flag_names,
				  compact);
}

static void privflags_cleanup_data(struct ethnl_reply_data *reply_data)
{
	struct privflags_reply_data *data = PRIVFLAGS_REPDATA(reply_data);

	kfree(data->priv_flag_names);
}

const struct ethnl_request_ops ethnl_privflags_request_ops = {
	.request_cmd		= ETHTOOL_MSG_PRIVFLAGS_GET,
	.reply_cmd		= ETHTOOL_MSG_PRIVFLAGS_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_PRIVFLAGS_HEADER,
	.req_info_size		= sizeof(struct privflags_req_info),
	.reply_data_size	= sizeof(struct privflags_reply_data),

	.prepare_data		= privflags_prepare_data,
	.reply_size		= privflags_reply_size,
	.fill_reply		= privflags_fill_reply,
	.cleanup_data		= privflags_cleanup_data,
};

/* PRIVFLAGS_SET */

const struct nla_policy ethnl_privflags_set_policy[] = {
	[ETHTOOL_A_PRIVFLAGS_HEADER]		= { .type = NLA_NESTED },
	[ETHTOOL_A_PRIVFLAGS_FLAGS]		= { .type = NLA_NESTED },
};

int ethnl_set_privflags(struct sk_buff *skb, struct genl_info *info)
{
	const char (*names)[ETH_GSTRING_LEN] = NULL;
	struct ethnl_req_info req_info = {};
	struct nlattr **tb = info->attrs;
	const struct ethtool_ops *ops;
	struct net_device *dev;
	unsigned int nflags;
	bool mod = false;
	bool compact;
	u32 flags;
	int ret;

	if (!tb[ETHTOOL_A_PRIVFLAGS_FLAGS])
		return -EINVAL;
	ret = ethnl_bitset_is_compact(tb[ETHTOOL_A_PRIVFLAGS_FLAGS], &compact);
	if (ret < 0)
		return ret;
	ret = ethnl_parse_header_dev_get(&req_info,
					 tb[ETHTOOL_A_PRIVFLAGS_HEADER],
					 genl_info_net(info), info->extack,
					 true);
	if (ret < 0)
		return ret;
	dev = req_info.dev;
	ops = dev->ethtool_ops;
	ret = -EOPNOTSUPP;
	if (!ops->get_priv_flags || !ops->set_priv_flags ||
	    !ops->get_sset_count || !ops->get_strings)
		goto out_dev;

	rtnl_lock();
	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		goto out_rtnl;
	ret = ethnl_get_priv_flags_info(dev, &nflags, compact ? NULL : &names);
	if (ret < 0)
		goto out_ops;
	flags = ops->get_priv_flags(dev);

	ret = ethnl_update_bitset32(&flags, nflags,
				    tb[ETHTOOL_A_PRIVFLAGS_FLAGS], names,
				    info->extack, &mod);
	if (ret < 0 || !mod)
		goto out_free;
	ret = ops->set_priv_flags(dev, flags);
	if (ret < 0)
		goto out_free;
	ethtool_notify(dev, ETHTOOL_MSG_PRIVFLAGS_NTF, NULL);

out_free:
	kfree(names);
out_ops:
	ethnl_ops_complete(dev);
out_rtnl:
	rtnl_unlock();
out_dev:
	dev_put(dev);
	return ret;
}

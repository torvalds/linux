// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"
#include "bitset.h"

struct features_req_info {
	struct ethnl_req_info	base;
};

struct features_reply_data {
	struct ethnl_reply_data	base;
	u32			hw[ETHTOOL_DEV_FEATURE_WORDS];
	u32			wanted[ETHTOOL_DEV_FEATURE_WORDS];
	u32			active[ETHTOOL_DEV_FEATURE_WORDS];
	u32			nochange[ETHTOOL_DEV_FEATURE_WORDS];
	u32			all[ETHTOOL_DEV_FEATURE_WORDS];
};

#define FEATURES_REPDATA(__reply_base) \
	container_of(__reply_base, struct features_reply_data, base)

static const struct nla_policy
features_get_policy[ETHTOOL_A_FEATURES_MAX + 1] = {
	[ETHTOOL_A_FEATURES_UNSPEC]	= { .type = NLA_REJECT },
	[ETHTOOL_A_FEATURES_HEADER]	= { .type = NLA_NESTED },
	[ETHTOOL_A_FEATURES_HW]		= { .type = NLA_REJECT },
	[ETHTOOL_A_FEATURES_WANTED]	= { .type = NLA_REJECT },
	[ETHTOOL_A_FEATURES_ACTIVE]	= { .type = NLA_REJECT },
	[ETHTOOL_A_FEATURES_NOCHANGE]	= { .type = NLA_REJECT },
};

static void ethnl_features_to_bitmap32(u32 *dest, netdev_features_t src)
{
	unsigned int i;

	for (i = 0; i < ETHTOOL_DEV_FEATURE_WORDS; i++)
		dest[i] = src >> (32 * i);
}

static int features_prepare_data(const struct ethnl_req_info *req_base,
				 struct ethnl_reply_data *reply_base,
				 struct genl_info *info)
{
	struct features_reply_data *data = FEATURES_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	netdev_features_t all_features;

	ethnl_features_to_bitmap32(data->hw, dev->hw_features);
	ethnl_features_to_bitmap32(data->wanted, dev->wanted_features);
	ethnl_features_to_bitmap32(data->active, dev->features);
	ethnl_features_to_bitmap32(data->nochange, NETIF_F_NEVER_CHANGE);
	all_features = GENMASK_ULL(NETDEV_FEATURE_COUNT - 1, 0);
	ethnl_features_to_bitmap32(data->all, all_features);

	return 0;
}

static int features_reply_size(const struct ethnl_req_info *req_base,
			       const struct ethnl_reply_data *reply_base)
{
	const struct features_reply_data *data = FEATURES_REPDATA(reply_base);
	bool compact = req_base->flags & ETHTOOL_FLAG_COMPACT_BITSETS;
	unsigned int len = 0;
	int ret;

	ret = ethnl_bitset32_size(data->hw, data->all, NETDEV_FEATURE_COUNT,
				  netdev_features_strings, compact);
	if (ret < 0)
		return ret;
	len += ret;
	ret = ethnl_bitset32_size(data->wanted, NULL, NETDEV_FEATURE_COUNT,
				  netdev_features_strings, compact);
	if (ret < 0)
		return ret;
	len += ret;
	ret = ethnl_bitset32_size(data->active, NULL, NETDEV_FEATURE_COUNT,
				  netdev_features_strings, compact);
	if (ret < 0)
		return ret;
	len += ret;
	ret = ethnl_bitset32_size(data->nochange, NULL, NETDEV_FEATURE_COUNT,
				  netdev_features_strings, compact);
	if (ret < 0)
		return ret;
	len += ret;

	return len;
}

static int features_fill_reply(struct sk_buff *skb,
			       const struct ethnl_req_info *req_base,
			       const struct ethnl_reply_data *reply_base)
{
	const struct features_reply_data *data = FEATURES_REPDATA(reply_base);
	bool compact = req_base->flags & ETHTOOL_FLAG_COMPACT_BITSETS;
	int ret;

	ret = ethnl_put_bitset32(skb, ETHTOOL_A_FEATURES_HW, data->hw,
				 data->all, NETDEV_FEATURE_COUNT,
				 netdev_features_strings, compact);
	if (ret < 0)
		return ret;
	ret = ethnl_put_bitset32(skb, ETHTOOL_A_FEATURES_WANTED, data->wanted,
				 NULL, NETDEV_FEATURE_COUNT,
				 netdev_features_strings, compact);
	if (ret < 0)
		return ret;
	ret = ethnl_put_bitset32(skb, ETHTOOL_A_FEATURES_ACTIVE, data->active,
				 NULL, NETDEV_FEATURE_COUNT,
				 netdev_features_strings, compact);
	if (ret < 0)
		return ret;
	return ethnl_put_bitset32(skb, ETHTOOL_A_FEATURES_NOCHANGE,
				  data->nochange, NULL, NETDEV_FEATURE_COUNT,
				  netdev_features_strings, compact);
}

const struct ethnl_request_ops ethnl_features_request_ops = {
	.request_cmd		= ETHTOOL_MSG_FEATURES_GET,
	.reply_cmd		= ETHTOOL_MSG_FEATURES_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_FEATURES_HEADER,
	.max_attr		= ETHTOOL_A_FEATURES_MAX,
	.req_info_size		= sizeof(struct features_req_info),
	.reply_data_size	= sizeof(struct features_reply_data),
	.request_policy		= features_get_policy,

	.prepare_data		= features_prepare_data,
	.reply_size		= features_reply_size,
	.fill_reply		= features_fill_reply,
};

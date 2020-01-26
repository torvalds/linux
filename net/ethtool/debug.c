// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"
#include "bitset.h"

struct debug_req_info {
	struct ethnl_req_info		base;
};

struct debug_reply_data {
	struct ethnl_reply_data		base;
	u32				msg_mask;
};

#define DEBUG_REPDATA(__reply_base) \
	container_of(__reply_base, struct debug_reply_data, base)

static const struct nla_policy
debug_get_policy[ETHTOOL_A_DEBUG_MAX + 1] = {
	[ETHTOOL_A_DEBUG_UNSPEC]	= { .type = NLA_REJECT },
	[ETHTOOL_A_DEBUG_HEADER]	= { .type = NLA_NESTED },
	[ETHTOOL_A_DEBUG_MSGMASK]	= { .type = NLA_REJECT },
};

static int debug_prepare_data(const struct ethnl_req_info *req_base,
			      struct ethnl_reply_data *reply_base,
			      struct genl_info *info)
{
	struct debug_reply_data *data = DEBUG_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	if (!dev->ethtool_ops->get_msglevel)
		return -EOPNOTSUPP;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;
	data->msg_mask = dev->ethtool_ops->get_msglevel(dev);
	ethnl_ops_complete(dev);

	return 0;
}

static int debug_reply_size(const struct ethnl_req_info *req_base,
			    const struct ethnl_reply_data *reply_base)
{
	const struct debug_reply_data *data = DEBUG_REPDATA(reply_base);
	bool compact = req_base->flags & ETHTOOL_FLAG_COMPACT_BITSETS;

	return ethnl_bitset32_size(&data->msg_mask, NULL, NETIF_MSG_CLASS_COUNT,
				   netif_msg_class_names, compact);
}

static int debug_fill_reply(struct sk_buff *skb,
			    const struct ethnl_req_info *req_base,
			    const struct ethnl_reply_data *reply_base)
{
	const struct debug_reply_data *data = DEBUG_REPDATA(reply_base);
	bool compact = req_base->flags & ETHTOOL_FLAG_COMPACT_BITSETS;

	return ethnl_put_bitset32(skb, ETHTOOL_A_DEBUG_MSGMASK, &data->msg_mask,
				  NULL, NETIF_MSG_CLASS_COUNT,
				  netif_msg_class_names, compact);
}

const struct ethnl_request_ops ethnl_debug_request_ops = {
	.request_cmd		= ETHTOOL_MSG_DEBUG_GET,
	.reply_cmd		= ETHTOOL_MSG_DEBUG_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_DEBUG_HEADER,
	.max_attr		= ETHTOOL_A_DEBUG_MAX,
	.req_info_size		= sizeof(struct debug_req_info),
	.reply_data_size	= sizeof(struct debug_reply_data),
	.request_policy		= debug_get_policy,

	.prepare_data		= debug_prepare_data,
	.reply_size		= debug_reply_size,
	.fill_reply		= debug_fill_reply,
};

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

const struct nla_policy ethnl_debug_get_policy[] = {
	[ETHTOOL_A_DEBUG_HEADER]	=
		NLA_POLICY_NESTED(ethnl_header_policy),
};

static int debug_prepare_data(const struct ethnl_req_info *req_base,
			      struct ethnl_reply_data *reply_base,
			      const struct genl_info *info)
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

/* DEBUG_SET */

const struct nla_policy ethnl_debug_set_policy[] = {
	[ETHTOOL_A_DEBUG_HEADER]	=
		NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_DEBUG_MSGMASK]	= { .type = NLA_NESTED },
};

static int
ethnl_set_debug_validate(struct ethnl_req_info *req_info,
			 struct genl_info *info)
{
	const struct ethtool_ops *ops = req_info->dev->ethtool_ops;

	return ops->get_msglevel && ops->set_msglevel ? 1 : -EOPNOTSUPP;
}

static int
ethnl_set_debug(struct ethnl_req_info *req_info, struct genl_info *info)
{
	struct net_device *dev = req_info->dev;
	struct nlattr **tb = info->attrs;
	bool mod = false;
	u32 msg_mask;
	int ret;

	msg_mask = dev->ethtool_ops->get_msglevel(dev);
	ret = ethnl_update_bitset32(&msg_mask, NETIF_MSG_CLASS_COUNT,
				    tb[ETHTOOL_A_DEBUG_MSGMASK],
				    netif_msg_class_names, info->extack, &mod);
	if (ret < 0 || !mod)
		return ret;

	dev->ethtool_ops->set_msglevel(dev, msg_mask);
	return 1;
}

const struct ethnl_request_ops ethnl_debug_request_ops = {
	.request_cmd		= ETHTOOL_MSG_DEBUG_GET,
	.reply_cmd		= ETHTOOL_MSG_DEBUG_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_DEBUG_HEADER,
	.req_info_size		= sizeof(struct debug_req_info),
	.reply_data_size	= sizeof(struct debug_reply_data),

	.prepare_data		= debug_prepare_data,
	.reply_size		= debug_reply_size,
	.fill_reply		= debug_fill_reply,

	.set_validate		= ethnl_set_debug_validate,
	.set			= ethnl_set_debug,
	.set_ntf_cmd		= ETHTOOL_MSG_DEBUG_NTF,
};

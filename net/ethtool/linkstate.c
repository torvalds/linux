// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"

struct linkstate_req_info {
	struct ethnl_req_info		base;
};

struct linkstate_reply_data {
	struct ethnl_reply_data		base;
	int				link;
};

#define LINKSTATE_REPDATA(__reply_base) \
	container_of(__reply_base, struct linkstate_reply_data, base)

static const struct nla_policy
linkstate_get_policy[ETHTOOL_A_LINKSTATE_MAX + 1] = {
	[ETHTOOL_A_LINKSTATE_UNSPEC]		= { .type = NLA_REJECT },
	[ETHTOOL_A_LINKSTATE_HEADER]		= { .type = NLA_NESTED },
	[ETHTOOL_A_LINKSTATE_LINK]		= { .type = NLA_REJECT },
};

static int linkstate_prepare_data(const struct ethnl_req_info *req_base,
				  struct ethnl_reply_data *reply_base,
				  struct genl_info *info)
{
	struct linkstate_reply_data *data = LINKSTATE_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;
	data->link = __ethtool_get_link(dev);
	ethnl_ops_complete(dev);

	return 0;
}

static int linkstate_reply_size(const struct ethnl_req_info *req_base,
				const struct ethnl_reply_data *reply_base)
{
	return nla_total_size(sizeof(u8)) /* LINKSTATE_LINK */
		+ 0;
}

static int linkstate_fill_reply(struct sk_buff *skb,
				const struct ethnl_req_info *req_base,
				const struct ethnl_reply_data *reply_base)
{
	struct linkstate_reply_data *data = LINKSTATE_REPDATA(reply_base);

	if (data->link >= 0 &&
	    nla_put_u8(skb, ETHTOOL_A_LINKSTATE_LINK, !!data->link))
		return -EMSGSIZE;

	return 0;
}

const struct ethnl_request_ops ethnl_linkstate_request_ops = {
	.request_cmd		= ETHTOOL_MSG_LINKSTATE_GET,
	.reply_cmd		= ETHTOOL_MSG_LINKSTATE_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_LINKSTATE_HEADER,
	.max_attr		= ETHTOOL_A_LINKSTATE_MAX,
	.req_info_size		= sizeof(struct linkstate_req_info),
	.reply_data_size	= sizeof(struct linkstate_reply_data),
	.request_policy		= linkstate_get_policy,

	.prepare_data		= linkstate_prepare_data,
	.reply_size		= linkstate_reply_size,
	.fill_reply		= linkstate_fill_reply,
};

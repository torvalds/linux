// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021 NXP
 */
#include "netlink.h"
#include "common.h"

struct phc_vclocks_req_info {
	struct ethnl_req_info		base;
};

struct phc_vclocks_reply_data {
	struct ethnl_reply_data		base;
	int				num;
	int				*index;
};

#define PHC_VCLOCKS_REPDATA(__reply_base) \
	container_of(__reply_base, struct phc_vclocks_reply_data, base)

const struct nla_policy ethnl_phc_vclocks_get_policy[] = {
	[ETHTOOL_A_PHC_VCLOCKS_HEADER] = NLA_POLICY_NESTED(ethnl_header_policy),
};

static int phc_vclocks_prepare_data(const struct ethnl_req_info *req_base,
				    struct ethnl_reply_data *reply_base,
				    struct genl_info *info)
{
	struct phc_vclocks_reply_data *data = PHC_VCLOCKS_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;
	data->num = ethtool_get_phc_vclocks(dev, &data->index);
	ethnl_ops_complete(dev);

	return ret;
}

static int phc_vclocks_reply_size(const struct ethnl_req_info *req_base,
				  const struct ethnl_reply_data *reply_base)
{
	const struct phc_vclocks_reply_data *data =
		PHC_VCLOCKS_REPDATA(reply_base);
	int len = 0;

	if (data->num > 0) {
		len += nla_total_size(sizeof(u32));
		len += nla_total_size(sizeof(s32) * data->num);
	}

	return len;
}

static int phc_vclocks_fill_reply(struct sk_buff *skb,
				  const struct ethnl_req_info *req_base,
				  const struct ethnl_reply_data *reply_base)
{
	const struct phc_vclocks_reply_data *data =
		PHC_VCLOCKS_REPDATA(reply_base);

	if (data->num <= 0)
		return 0;

	if (nla_put_u32(skb, ETHTOOL_A_PHC_VCLOCKS_NUM, data->num) ||
	    nla_put(skb, ETHTOOL_A_PHC_VCLOCKS_INDEX,
		    sizeof(s32) * data->num, data->index))
		return -EMSGSIZE;

	return 0;
}

static void phc_vclocks_cleanup_data(struct ethnl_reply_data *reply_base)
{
	const struct phc_vclocks_reply_data *data =
		PHC_VCLOCKS_REPDATA(reply_base);

	kfree(data->index);
}

const struct ethnl_request_ops ethnl_phc_vclocks_request_ops = {
	.request_cmd		= ETHTOOL_MSG_PHC_VCLOCKS_GET,
	.reply_cmd		= ETHTOOL_MSG_PHC_VCLOCKS_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_PHC_VCLOCKS_HEADER,
	.req_info_size		= sizeof(struct phc_vclocks_req_info),
	.reply_data_size	= sizeof(struct phc_vclocks_reply_data),

	.prepare_data		= phc_vclocks_prepare_data,
	.reply_size		= phc_vclocks_reply_size,
	.fill_reply		= phc_vclocks_fill_reply,
	.cleanup_data		= phc_vclocks_cleanup_data,
};

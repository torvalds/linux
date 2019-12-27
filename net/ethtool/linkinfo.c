// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"

struct linkinfo_req_info {
	struct ethnl_req_info		base;
};

struct linkinfo_reply_data {
	struct ethnl_reply_data		base;
	struct ethtool_link_ksettings	ksettings;
	struct ethtool_link_settings	*lsettings;
};

#define LINKINFO_REPDATA(__reply_base) \
	container_of(__reply_base, struct linkinfo_reply_data, base)

static const struct nla_policy
linkinfo_get_policy[ETHTOOL_A_LINKINFO_MAX + 1] = {
	[ETHTOOL_A_LINKINFO_UNSPEC]		= { .type = NLA_REJECT },
	[ETHTOOL_A_LINKINFO_HEADER]		= { .type = NLA_NESTED },
	[ETHTOOL_A_LINKINFO_PORT]		= { .type = NLA_REJECT },
	[ETHTOOL_A_LINKINFO_PHYADDR]		= { .type = NLA_REJECT },
	[ETHTOOL_A_LINKINFO_TP_MDIX]		= { .type = NLA_REJECT },
	[ETHTOOL_A_LINKINFO_TP_MDIX_CTRL]	= { .type = NLA_REJECT },
	[ETHTOOL_A_LINKINFO_TRANSCEIVER]	= { .type = NLA_REJECT },
};

static int linkinfo_prepare_data(const struct ethnl_req_info *req_base,
				 struct ethnl_reply_data *reply_base,
				 struct genl_info *info)
{
	struct linkinfo_reply_data *data = LINKINFO_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	data->lsettings = &data->ksettings.base;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;
	ret = __ethtool_get_link_ksettings(dev, &data->ksettings);
	if (ret < 0 && info)
		GENL_SET_ERR_MSG(info, "failed to retrieve link settings");
	ethnl_ops_complete(dev);

	return ret;
}

static int linkinfo_reply_size(const struct ethnl_req_info *req_base,
			       const struct ethnl_reply_data *reply_base)
{
	return nla_total_size(sizeof(u8)) /* LINKINFO_PORT */
		+ nla_total_size(sizeof(u8)) /* LINKINFO_PHYADDR */
		+ nla_total_size(sizeof(u8)) /* LINKINFO_TP_MDIX */
		+ nla_total_size(sizeof(u8)) /* LINKINFO_TP_MDIX_CTRL */
		+ nla_total_size(sizeof(u8)) /* LINKINFO_TRANSCEIVER */
		+ 0;
}

static int linkinfo_fill_reply(struct sk_buff *skb,
			       const struct ethnl_req_info *req_base,
			       const struct ethnl_reply_data *reply_base)
{
	const struct linkinfo_reply_data *data = LINKINFO_REPDATA(reply_base);

	if (nla_put_u8(skb, ETHTOOL_A_LINKINFO_PORT, data->lsettings->port) ||
	    nla_put_u8(skb, ETHTOOL_A_LINKINFO_PHYADDR,
		       data->lsettings->phy_address) ||
	    nla_put_u8(skb, ETHTOOL_A_LINKINFO_TP_MDIX,
		       data->lsettings->eth_tp_mdix) ||
	    nla_put_u8(skb, ETHTOOL_A_LINKINFO_TP_MDIX_CTRL,
		       data->lsettings->eth_tp_mdix_ctrl) ||
	    nla_put_u8(skb, ETHTOOL_A_LINKINFO_TRANSCEIVER,
		       data->lsettings->transceiver))
		return -EMSGSIZE;

	return 0;
}

const struct ethnl_request_ops ethnl_linkinfo_request_ops = {
	.request_cmd		= ETHTOOL_MSG_LINKINFO_GET,
	.reply_cmd		= ETHTOOL_MSG_LINKINFO_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_LINKINFO_HEADER,
	.max_attr		= ETHTOOL_A_LINKINFO_MAX,
	.req_info_size		= sizeof(struct linkinfo_req_info),
	.reply_data_size	= sizeof(struct linkinfo_reply_data),
	.request_policy		= linkinfo_get_policy,

	.prepare_data		= linkinfo_prepare_data,
	.reply_size		= linkinfo_reply_size,
	.fill_reply		= linkinfo_fill_reply,
};

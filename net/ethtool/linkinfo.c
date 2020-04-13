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

/* LINKINFO_SET */

static const struct nla_policy
linkinfo_set_policy[ETHTOOL_A_LINKINFO_MAX + 1] = {
	[ETHTOOL_A_LINKINFO_UNSPEC]		= { .type = NLA_REJECT },
	[ETHTOOL_A_LINKINFO_HEADER]		= { .type = NLA_NESTED },
	[ETHTOOL_A_LINKINFO_PORT]		= { .type = NLA_U8 },
	[ETHTOOL_A_LINKINFO_PHYADDR]		= { .type = NLA_U8 },
	[ETHTOOL_A_LINKINFO_TP_MDIX]		= { .type = NLA_REJECT },
	[ETHTOOL_A_LINKINFO_TP_MDIX_CTRL]	= { .type = NLA_U8 },
	[ETHTOOL_A_LINKINFO_TRANSCEIVER]	= { .type = NLA_REJECT },
};

int ethnl_set_linkinfo(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *tb[ETHTOOL_A_LINKINFO_MAX + 1];
	struct ethtool_link_ksettings ksettings = {};
	struct ethtool_link_settings *lsettings;
	struct ethnl_req_info req_info = {};
	struct net_device *dev;
	bool mod = false;
	int ret;

	ret = nlmsg_parse(info->nlhdr, GENL_HDRLEN, tb,
			  ETHTOOL_A_LINKINFO_MAX, linkinfo_set_policy,
			  info->extack);
	if (ret < 0)
		return ret;
	ret = ethnl_parse_header_dev_get(&req_info,
					 tb[ETHTOOL_A_LINKINFO_HEADER],
					 genl_info_net(info), info->extack,
					 true);
	if (ret < 0)
		return ret;
	dev = req_info.dev;
	ret = -EOPNOTSUPP;
	if (!dev->ethtool_ops->get_link_ksettings ||
	    !dev->ethtool_ops->set_link_ksettings)
		goto out_dev;

	rtnl_lock();
	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		goto out_rtnl;

	ret = __ethtool_get_link_ksettings(dev, &ksettings);
	if (ret < 0) {
		if (info)
			GENL_SET_ERR_MSG(info, "failed to retrieve link settings");
		goto out_ops;
	}
	lsettings = &ksettings.base;

	ethnl_update_u8(&lsettings->port, tb[ETHTOOL_A_LINKINFO_PORT], &mod);
	ethnl_update_u8(&lsettings->phy_address, tb[ETHTOOL_A_LINKINFO_PHYADDR],
			&mod);
	ethnl_update_u8(&lsettings->eth_tp_mdix_ctrl,
			tb[ETHTOOL_A_LINKINFO_TP_MDIX_CTRL], &mod);
	ret = 0;
	if (!mod)
		goto out_ops;

	ret = dev->ethtool_ops->set_link_ksettings(dev, &ksettings);
	if (ret < 0)
		GENL_SET_ERR_MSG(info, "link settings update failed");
	else
		ethtool_notify(dev, ETHTOOL_MSG_LINKINFO_NTF, NULL);

out_ops:
	ethnl_ops_complete(dev);
out_rtnl:
	rtnl_unlock();
out_dev:
	dev_put(dev);
	return ret;
}

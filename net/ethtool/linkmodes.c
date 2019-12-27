// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"
#include "bitset.h"

struct linkmodes_req_info {
	struct ethnl_req_info		base;
};

struct linkmodes_reply_data {
	struct ethnl_reply_data		base;
	struct ethtool_link_ksettings	ksettings;
	struct ethtool_link_settings	*lsettings;
	bool				peer_empty;
};

#define LINKMODES_REPDATA(__reply_base) \
	container_of(__reply_base, struct linkmodes_reply_data, base)

static const struct nla_policy
linkmodes_get_policy[ETHTOOL_A_LINKMODES_MAX + 1] = {
	[ETHTOOL_A_LINKMODES_UNSPEC]		= { .type = NLA_REJECT },
	[ETHTOOL_A_LINKMODES_HEADER]		= { .type = NLA_NESTED },
	[ETHTOOL_A_LINKMODES_AUTONEG]		= { .type = NLA_REJECT },
	[ETHTOOL_A_LINKMODES_OURS]		= { .type = NLA_REJECT },
	[ETHTOOL_A_LINKMODES_PEER]		= { .type = NLA_REJECT },
	[ETHTOOL_A_LINKMODES_SPEED]		= { .type = NLA_REJECT },
	[ETHTOOL_A_LINKMODES_DUPLEX]		= { .type = NLA_REJECT },
};

static int linkmodes_prepare_data(const struct ethnl_req_info *req_base,
				  struct ethnl_reply_data *reply_base,
				  struct genl_info *info)
{
	struct linkmodes_reply_data *data = LINKMODES_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	data->lsettings = &data->ksettings.base;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;

	ret = __ethtool_get_link_ksettings(dev, &data->ksettings);
	if (ret < 0 && info) {
		GENL_SET_ERR_MSG(info, "failed to retrieve link settings");
		goto out;
	}

	data->peer_empty =
		bitmap_empty(data->ksettings.link_modes.lp_advertising,
			     __ETHTOOL_LINK_MODE_MASK_NBITS);

out:
	ethnl_ops_complete(dev);
	return ret;
}

static int linkmodes_reply_size(const struct ethnl_req_info *req_base,
				const struct ethnl_reply_data *reply_base)
{
	const struct linkmodes_reply_data *data = LINKMODES_REPDATA(reply_base);
	const struct ethtool_link_ksettings *ksettings = &data->ksettings;
	bool compact = req_base->flags & ETHTOOL_FLAG_COMPACT_BITSETS;
	int len, ret;

	len = nla_total_size(sizeof(u8)) /* LINKMODES_AUTONEG */
		+ nla_total_size(sizeof(u32)) /* LINKMODES_SPEED */
		+ nla_total_size(sizeof(u8)) /* LINKMODES_DUPLEX */
		+ 0;
	ret = ethnl_bitset_size(ksettings->link_modes.advertising,
				ksettings->link_modes.supported,
				__ETHTOOL_LINK_MODE_MASK_NBITS,
				link_mode_names, compact);
	if (ret < 0)
		return ret;
	len += ret;
	if (!data->peer_empty) {
		ret = ethnl_bitset_size(ksettings->link_modes.lp_advertising,
					NULL, __ETHTOOL_LINK_MODE_MASK_NBITS,
					link_mode_names, compact);
		if (ret < 0)
			return ret;
		len += ret;
	}

	return len;
}

static int linkmodes_fill_reply(struct sk_buff *skb,
				const struct ethnl_req_info *req_base,
				const struct ethnl_reply_data *reply_base)
{
	const struct linkmodes_reply_data *data = LINKMODES_REPDATA(reply_base);
	const struct ethtool_link_ksettings *ksettings = &data->ksettings;
	const struct ethtool_link_settings *lsettings = &ksettings->base;
	bool compact = req_base->flags & ETHTOOL_FLAG_COMPACT_BITSETS;
	int ret;

	if (nla_put_u8(skb, ETHTOOL_A_LINKMODES_AUTONEG, lsettings->autoneg))
		return -EMSGSIZE;

	ret = ethnl_put_bitset(skb, ETHTOOL_A_LINKMODES_OURS,
			       ksettings->link_modes.advertising,
			       ksettings->link_modes.supported,
			       __ETHTOOL_LINK_MODE_MASK_NBITS, link_mode_names,
			       compact);
	if (ret < 0)
		return -EMSGSIZE;
	if (!data->peer_empty) {
		ret = ethnl_put_bitset(skb, ETHTOOL_A_LINKMODES_PEER,
				       ksettings->link_modes.lp_advertising,
				       NULL, __ETHTOOL_LINK_MODE_MASK_NBITS,
				       link_mode_names, compact);
		if (ret < 0)
			return -EMSGSIZE;
	}

	if (nla_put_u32(skb, ETHTOOL_A_LINKMODES_SPEED, lsettings->speed) ||
	    nla_put_u8(skb, ETHTOOL_A_LINKMODES_DUPLEX, lsettings->duplex))
		return -EMSGSIZE;

	return 0;
}

const struct ethnl_request_ops ethnl_linkmodes_request_ops = {
	.request_cmd		= ETHTOOL_MSG_LINKMODES_GET,
	.reply_cmd		= ETHTOOL_MSG_LINKMODES_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_LINKMODES_HEADER,
	.max_attr		= ETHTOOL_A_LINKMODES_MAX,
	.req_info_size		= sizeof(struct linkmodes_req_info),
	.reply_data_size	= sizeof(struct linkmodes_reply_data),
	.request_policy		= linkmodes_get_policy,

	.prepare_data		= linkmodes_prepare_data,
	.reply_size		= linkmodes_reply_size,
	.fill_reply		= linkmodes_fill_reply,
};

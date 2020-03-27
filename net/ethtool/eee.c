// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"
#include "bitset.h"

#define EEE_MODES_COUNT \
	(sizeof_field(struct ethtool_eee, supported) * BITS_PER_BYTE)

struct eee_req_info {
	struct ethnl_req_info		base;
};

struct eee_reply_data {
	struct ethnl_reply_data		base;
	struct ethtool_eee		eee;
};

#define EEE_REPDATA(__reply_base) \
	container_of(__reply_base, struct eee_reply_data, base)

static const struct nla_policy
eee_get_policy[ETHTOOL_A_EEE_MAX + 1] = {
	[ETHTOOL_A_EEE_UNSPEC]		= { .type = NLA_REJECT },
	[ETHTOOL_A_EEE_HEADER]		= { .type = NLA_NESTED },
	[ETHTOOL_A_EEE_MODES_OURS]	= { .type = NLA_REJECT },
	[ETHTOOL_A_EEE_MODES_PEER]	= { .type = NLA_REJECT },
	[ETHTOOL_A_EEE_ACTIVE]		= { .type = NLA_REJECT },
	[ETHTOOL_A_EEE_ENABLED]		= { .type = NLA_REJECT },
	[ETHTOOL_A_EEE_TX_LPI_ENABLED]	= { .type = NLA_REJECT },
	[ETHTOOL_A_EEE_TX_LPI_TIMER]	= { .type = NLA_REJECT },
};

static int eee_prepare_data(const struct ethnl_req_info *req_base,
			    struct ethnl_reply_data *reply_base,
			    struct genl_info *info)
{
	struct eee_reply_data *data = EEE_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	if (!dev->ethtool_ops->get_eee)
		return -EOPNOTSUPP;
	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;
	ret = dev->ethtool_ops->get_eee(dev, &data->eee);
	ethnl_ops_complete(dev);

	return ret;
}

static int eee_reply_size(const struct ethnl_req_info *req_base,
			  const struct ethnl_reply_data *reply_base)
{
	bool compact = req_base->flags & ETHTOOL_FLAG_COMPACT_BITSETS;
	const struct eee_reply_data *data = EEE_REPDATA(reply_base);
	const struct ethtool_eee *eee = &data->eee;
	int len = 0;
	int ret;

	BUILD_BUG_ON(sizeof(eee->advertised) * BITS_PER_BYTE !=
		     EEE_MODES_COUNT);
	BUILD_BUG_ON(sizeof(eee->lp_advertised) * BITS_PER_BYTE !=
		     EEE_MODES_COUNT);

	/* MODES_OURS */
	ret = ethnl_bitset32_size(&eee->advertised, &eee->supported,
				  EEE_MODES_COUNT, link_mode_names, compact);
	if (ret < 0)
		return ret;
	len += ret;
	/* MODES_PEERS */
	ret = ethnl_bitset32_size(&eee->lp_advertised, NULL,
				  EEE_MODES_COUNT, link_mode_names, compact);
	if (ret < 0)
		return ret;
	len += ret;

	len += nla_total_size(sizeof(u8)) +	/* _EEE_ACTIVE */
	       nla_total_size(sizeof(u8)) +	/* _EEE_ENABLED */
	       nla_total_size(sizeof(u8)) +	/* _EEE_TX_LPI_ENABLED */
	       nla_total_size(sizeof(u32));	/* _EEE_TX_LPI_TIMER */

	return len;
}

static int eee_fill_reply(struct sk_buff *skb,
			  const struct ethnl_req_info *req_base,
			  const struct ethnl_reply_data *reply_base)
{
	bool compact = req_base->flags & ETHTOOL_FLAG_COMPACT_BITSETS;
	const struct eee_reply_data *data = EEE_REPDATA(reply_base);
	const struct ethtool_eee *eee = &data->eee;
	int ret;

	ret = ethnl_put_bitset32(skb, ETHTOOL_A_EEE_MODES_OURS,
				 &eee->advertised, &eee->supported,
				 EEE_MODES_COUNT, link_mode_names, compact);
	if (ret < 0)
		return ret;
	ret = ethnl_put_bitset32(skb, ETHTOOL_A_EEE_MODES_PEER,
				 &eee->lp_advertised, NULL, EEE_MODES_COUNT,
				 link_mode_names, compact);
	if (ret < 0)
		return ret;

	if (nla_put_u8(skb, ETHTOOL_A_EEE_ACTIVE, !!eee->eee_active) ||
	    nla_put_u8(skb, ETHTOOL_A_EEE_ENABLED, !!eee->eee_enabled) ||
	    nla_put_u8(skb, ETHTOOL_A_EEE_TX_LPI_ENABLED,
		       !!eee->tx_lpi_enabled) ||
	    nla_put_u32(skb, ETHTOOL_A_EEE_TX_LPI_TIMER, eee->tx_lpi_timer))
		return -EMSGSIZE;

	return 0;
}

const struct ethnl_request_ops ethnl_eee_request_ops = {
	.request_cmd		= ETHTOOL_MSG_EEE_GET,
	.reply_cmd		= ETHTOOL_MSG_EEE_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_EEE_HEADER,
	.max_attr		= ETHTOOL_A_EEE_MAX,
	.req_info_size		= sizeof(struct eee_req_info),
	.reply_data_size	= sizeof(struct eee_reply_data),
	.request_policy		= eee_get_policy,

	.prepare_data		= eee_prepare_data,
	.reply_size		= eee_reply_size,
	.fill_reply		= eee_fill_reply,
};

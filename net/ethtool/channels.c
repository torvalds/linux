// SPDX-License-Identifier: GPL-2.0-only

#include <net/xdp_sock_drv.h>

#include "netlink.h"
#include "common.h"

struct channels_req_info {
	struct ethnl_req_info		base;
};

struct channels_reply_data {
	struct ethnl_reply_data		base;
	struct ethtool_channels		channels;
};

#define CHANNELS_REPDATA(__reply_base) \
	container_of(__reply_base, struct channels_reply_data, base)

const struct nla_policy ethnl_channels_get_policy[] = {
	[ETHTOOL_A_CHANNELS_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy),
};

static int channels_prepare_data(const struct ethnl_req_info *req_base,
				 struct ethnl_reply_data *reply_base,
				 const struct genl_info *info)
{
	struct channels_reply_data *data = CHANNELS_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	if (!dev->ethtool_ops->get_channels)
		return -EOPNOTSUPP;
	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;
	dev->ethtool_ops->get_channels(dev, &data->channels);
	ethnl_ops_complete(dev);

	return 0;
}

static int channels_reply_size(const struct ethnl_req_info *req_base,
			       const struct ethnl_reply_data *reply_base)
{
	return nla_total_size(sizeof(u32)) +	/* _CHANNELS_RX_MAX */
	       nla_total_size(sizeof(u32)) +	/* _CHANNELS_TX_MAX */
	       nla_total_size(sizeof(u32)) +	/* _CHANNELS_OTHER_MAX */
	       nla_total_size(sizeof(u32)) +	/* _CHANNELS_COMBINED_MAX */
	       nla_total_size(sizeof(u32)) +	/* _CHANNELS_RX_COUNT */
	       nla_total_size(sizeof(u32)) +	/* _CHANNELS_TX_COUNT */
	       nla_total_size(sizeof(u32)) +	/* _CHANNELS_OTHER_COUNT */
	       nla_total_size(sizeof(u32));	/* _CHANNELS_COMBINED_COUNT */
}

static int channels_fill_reply(struct sk_buff *skb,
			       const struct ethnl_req_info *req_base,
			       const struct ethnl_reply_data *reply_base)
{
	const struct channels_reply_data *data = CHANNELS_REPDATA(reply_base);
	const struct ethtool_channels *channels = &data->channels;

	if ((channels->max_rx &&
	     (nla_put_u32(skb, ETHTOOL_A_CHANNELS_RX_MAX,
			  channels->max_rx) ||
	      nla_put_u32(skb, ETHTOOL_A_CHANNELS_RX_COUNT,
			  channels->rx_count))) ||
	    (channels->max_tx &&
	     (nla_put_u32(skb, ETHTOOL_A_CHANNELS_TX_MAX,
			  channels->max_tx) ||
	      nla_put_u32(skb, ETHTOOL_A_CHANNELS_TX_COUNT,
			  channels->tx_count))) ||
	    (channels->max_other &&
	     (nla_put_u32(skb, ETHTOOL_A_CHANNELS_OTHER_MAX,
			  channels->max_other) ||
	      nla_put_u32(skb, ETHTOOL_A_CHANNELS_OTHER_COUNT,
			  channels->other_count))) ||
	    (channels->max_combined &&
	     (nla_put_u32(skb, ETHTOOL_A_CHANNELS_COMBINED_MAX,
			  channels->max_combined) ||
	      nla_put_u32(skb, ETHTOOL_A_CHANNELS_COMBINED_COUNT,
			  channels->combined_count))))
		return -EMSGSIZE;

	return 0;
}

/* CHANNELS_SET */

const struct nla_policy ethnl_channels_set_policy[] = {
	[ETHTOOL_A_CHANNELS_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_CHANNELS_RX_COUNT]		= { .type = NLA_U32 },
	[ETHTOOL_A_CHANNELS_TX_COUNT]		= { .type = NLA_U32 },
	[ETHTOOL_A_CHANNELS_OTHER_COUNT]	= { .type = NLA_U32 },
	[ETHTOOL_A_CHANNELS_COMBINED_COUNT]	= { .type = NLA_U32 },
};

static int
ethnl_set_channels_validate(struct ethnl_req_info *req_info,
			    struct genl_info *info)
{
	const struct ethtool_ops *ops = req_info->dev->ethtool_ops;

	return ops->get_channels && ops->set_channels ? 1 : -EOPNOTSUPP;
}

static int
ethnl_set_channels(struct ethnl_req_info *req_info, struct genl_info *info)
{
	unsigned int from_channel, old_total, i;
	bool mod = false, mod_combined = false;
	struct net_device *dev = req_info->dev;
	struct ethtool_channels channels = {};
	struct nlattr **tb = info->attrs;
	u32 err_attr, max_rxfh_in_use;
	u64 max_rxnfc_in_use;
	int ret;

	dev->ethtool_ops->get_channels(dev, &channels);
	old_total = channels.combined_count +
		    max(channels.rx_count, channels.tx_count);

	ethnl_update_u32(&channels.rx_count, tb[ETHTOOL_A_CHANNELS_RX_COUNT],
			 &mod);
	ethnl_update_u32(&channels.tx_count, tb[ETHTOOL_A_CHANNELS_TX_COUNT],
			 &mod);
	ethnl_update_u32(&channels.other_count,
			 tb[ETHTOOL_A_CHANNELS_OTHER_COUNT], &mod);
	ethnl_update_u32(&channels.combined_count,
			 tb[ETHTOOL_A_CHANNELS_COMBINED_COUNT], &mod_combined);
	mod |= mod_combined;
	if (!mod)
		return 0;

	/* ensure new channel counts are within limits */
	if (channels.rx_count > channels.max_rx)
		err_attr = ETHTOOL_A_CHANNELS_RX_COUNT;
	else if (channels.tx_count > channels.max_tx)
		err_attr = ETHTOOL_A_CHANNELS_TX_COUNT;
	else if (channels.other_count > channels.max_other)
		err_attr = ETHTOOL_A_CHANNELS_OTHER_COUNT;
	else if (channels.combined_count > channels.max_combined)
		err_attr = ETHTOOL_A_CHANNELS_COMBINED_COUNT;
	else
		err_attr = 0;
	if (err_attr) {
		NL_SET_ERR_MSG_ATTR(info->extack, tb[err_attr],
				    "requested channel count exceeds maximum");
		return -EINVAL;
	}

	/* ensure there is at least one RX and one TX channel */
	if (!channels.combined_count && !channels.rx_count)
		err_attr = ETHTOOL_A_CHANNELS_RX_COUNT;
	else if (!channels.combined_count && !channels.tx_count)
		err_attr = ETHTOOL_A_CHANNELS_TX_COUNT;
	else
		err_attr = 0;
	if (err_attr) {
		if (mod_combined)
			err_attr = ETHTOOL_A_CHANNELS_COMBINED_COUNT;
		NL_SET_ERR_MSG_ATTR(info->extack, tb[err_attr],
				    "requested channel counts would result in no RX or TX channel being configured");
		return -EINVAL;
	}

	/* ensure the new Rx count fits within the configured Rx flow
	 * indirection table/rxnfc settings
	 */
	if (ethtool_get_max_rxnfc_channel(dev, &max_rxnfc_in_use))
		max_rxnfc_in_use = 0;
	max_rxfh_in_use = ethtool_get_max_rxfh_channel(dev);
	if (channels.combined_count + channels.rx_count <= max_rxfh_in_use) {
		GENL_SET_ERR_MSG_FMT(info, "requested channel counts are too low for existing indirection table (%d)", max_rxfh_in_use);
		return -EINVAL;
	}
	if (channels.combined_count + channels.rx_count <= max_rxnfc_in_use) {
		GENL_SET_ERR_MSG(info, "requested channel counts are too low for existing ntuple filter settings");
		return -EINVAL;
	}

	/* Disabling channels, query zero-copy AF_XDP sockets */
	from_channel = channels.combined_count +
		       min(channels.rx_count, channels.tx_count);
	for (i = from_channel; i < old_total; i++)
		if (xsk_get_pool_from_qid(dev, i)) {
			GENL_SET_ERR_MSG(info, "requested channel counts are too low for existing zerocopy AF_XDP sockets");
			return -EINVAL;
		}

	ret = dev->ethtool_ops->set_channels(dev, &channels);
	return ret < 0 ? ret : 1;
}

const struct ethnl_request_ops ethnl_channels_request_ops = {
	.request_cmd		= ETHTOOL_MSG_CHANNELS_GET,
	.reply_cmd		= ETHTOOL_MSG_CHANNELS_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_CHANNELS_HEADER,
	.req_info_size		= sizeof(struct channels_req_info),
	.reply_data_size	= sizeof(struct channels_reply_data),

	.prepare_data		= channels_prepare_data,
	.reply_size		= channels_reply_size,
	.fill_reply		= channels_fill_reply,

	.set_validate		= ethnl_set_channels_validate,
	.set			= ethnl_set_channels,
	.set_ntf_cmd		= ETHTOOL_MSG_CHANNELS_NTF,
};

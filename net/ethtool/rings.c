// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"

struct rings_req_info {
	struct ethnl_req_info		base;
};

struct rings_reply_data {
	struct ethnl_reply_data		base;
	struct ethtool_ringparam	ringparam;
	struct kernel_ethtool_ringparam	kernel_ringparam;
};

#define RINGS_REPDATA(__reply_base) \
	container_of(__reply_base, struct rings_reply_data, base)

const struct nla_policy ethnl_rings_get_policy[] = {
	[ETHTOOL_A_RINGS_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy),
};

static int rings_prepare_data(const struct ethnl_req_info *req_base,
			      struct ethnl_reply_data *reply_base,
			      struct genl_info *info)
{
	struct rings_reply_data *data = RINGS_REPDATA(reply_base);
	struct netlink_ext_ack *extack = info ? info->extack : NULL;
	struct net_device *dev = reply_base->dev;
	int ret;

	if (!dev->ethtool_ops->get_ringparam)
		return -EOPNOTSUPP;
	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;
	dev->ethtool_ops->get_ringparam(dev, &data->ringparam,
					&data->kernel_ringparam, extack);
	ethnl_ops_complete(dev);

	return 0;
}

static int rings_reply_size(const struct ethnl_req_info *req_base,
			    const struct ethnl_reply_data *reply_base)
{
	return nla_total_size(sizeof(u32)) +	/* _RINGS_RX_MAX */
	       nla_total_size(sizeof(u32)) +	/* _RINGS_RX_MINI_MAX */
	       nla_total_size(sizeof(u32)) +	/* _RINGS_RX_JUMBO_MAX */
	       nla_total_size(sizeof(u32)) +	/* _RINGS_TX_MAX */
	       nla_total_size(sizeof(u32)) +	/* _RINGS_RX */
	       nla_total_size(sizeof(u32)) +	/* _RINGS_RX_MINI */
	       nla_total_size(sizeof(u32)) +	/* _RINGS_RX_JUMBO */
	       nla_total_size(sizeof(u32)) +	/* _RINGS_TX */
	       nla_total_size(sizeof(u32)) +	/* _RINGS_RX_BUF_LEN */
	       nla_total_size(sizeof(u8))  +	/* _RINGS_TCP_DATA_SPLIT */
	       nla_total_size(sizeof(u32)  +	/* _RINGS_CQE_SIZE */
	       nla_total_size(sizeof(u8)));	/* _RINGS_TX_PUSH */
}

static int rings_fill_reply(struct sk_buff *skb,
			    const struct ethnl_req_info *req_base,
			    const struct ethnl_reply_data *reply_base)
{
	const struct rings_reply_data *data = RINGS_REPDATA(reply_base);
	const struct kernel_ethtool_ringparam *kr = &data->kernel_ringparam;
	const struct ethtool_ringparam *ringparam = &data->ringparam;

	WARN_ON(kr->tcp_data_split > ETHTOOL_TCP_DATA_SPLIT_ENABLED);

	if ((ringparam->rx_max_pending &&
	     (nla_put_u32(skb, ETHTOOL_A_RINGS_RX_MAX,
			  ringparam->rx_max_pending) ||
	      nla_put_u32(skb, ETHTOOL_A_RINGS_RX,
			  ringparam->rx_pending))) ||
	    (ringparam->rx_mini_max_pending &&
	     (nla_put_u32(skb, ETHTOOL_A_RINGS_RX_MINI_MAX,
			  ringparam->rx_mini_max_pending) ||
	      nla_put_u32(skb, ETHTOOL_A_RINGS_RX_MINI,
			  ringparam->rx_mini_pending))) ||
	    (ringparam->rx_jumbo_max_pending &&
	     (nla_put_u32(skb, ETHTOOL_A_RINGS_RX_JUMBO_MAX,
			  ringparam->rx_jumbo_max_pending) ||
	      nla_put_u32(skb, ETHTOOL_A_RINGS_RX_JUMBO,
			  ringparam->rx_jumbo_pending))) ||
	    (ringparam->tx_max_pending &&
	     (nla_put_u32(skb, ETHTOOL_A_RINGS_TX_MAX,
			  ringparam->tx_max_pending) ||
	      nla_put_u32(skb, ETHTOOL_A_RINGS_TX,
			  ringparam->tx_pending)))  ||
	    (kr->rx_buf_len &&
	     (nla_put_u32(skb, ETHTOOL_A_RINGS_RX_BUF_LEN, kr->rx_buf_len))) ||
	    (kr->tcp_data_split &&
	     (nla_put_u8(skb, ETHTOOL_A_RINGS_TCP_DATA_SPLIT,
			 kr->tcp_data_split))) ||
	    (kr->cqe_size &&
	     (nla_put_u32(skb, ETHTOOL_A_RINGS_CQE_SIZE, kr->cqe_size))) ||
	    nla_put_u8(skb, ETHTOOL_A_RINGS_TX_PUSH, !!kr->tx_push))
		return -EMSGSIZE;

	return 0;
}

const struct ethnl_request_ops ethnl_rings_request_ops = {
	.request_cmd		= ETHTOOL_MSG_RINGS_GET,
	.reply_cmd		= ETHTOOL_MSG_RINGS_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_RINGS_HEADER,
	.req_info_size		= sizeof(struct rings_req_info),
	.reply_data_size	= sizeof(struct rings_reply_data),

	.prepare_data		= rings_prepare_data,
	.reply_size		= rings_reply_size,
	.fill_reply		= rings_fill_reply,
};

/* RINGS_SET */

const struct nla_policy ethnl_rings_set_policy[] = {
	[ETHTOOL_A_RINGS_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_RINGS_RX]			= { .type = NLA_U32 },
	[ETHTOOL_A_RINGS_RX_MINI]		= { .type = NLA_U32 },
	[ETHTOOL_A_RINGS_RX_JUMBO]		= { .type = NLA_U32 },
	[ETHTOOL_A_RINGS_TX]			= { .type = NLA_U32 },
	[ETHTOOL_A_RINGS_RX_BUF_LEN]            = NLA_POLICY_MIN(NLA_U32, 1),
	[ETHTOOL_A_RINGS_CQE_SIZE]		= NLA_POLICY_MIN(NLA_U32, 1),
	[ETHTOOL_A_RINGS_TX_PUSH]		= NLA_POLICY_MAX(NLA_U8, 1),
};

int ethnl_set_rings(struct sk_buff *skb, struct genl_info *info)
{
	struct kernel_ethtool_ringparam kernel_ringparam = {};
	struct ethtool_ringparam ringparam = {};
	struct ethnl_req_info req_info = {};
	struct nlattr **tb = info->attrs;
	const struct nlattr *err_attr;
	const struct ethtool_ops *ops;
	struct net_device *dev;
	bool mod = false;
	int ret;

	ret = ethnl_parse_header_dev_get(&req_info,
					 tb[ETHTOOL_A_RINGS_HEADER],
					 genl_info_net(info), info->extack,
					 true);
	if (ret < 0)
		return ret;
	dev = req_info.dev;
	ops = dev->ethtool_ops;
	ret = -EOPNOTSUPP;
	if (!ops->get_ringparam || !ops->set_ringparam)
		goto out_dev;

	if (tb[ETHTOOL_A_RINGS_RX_BUF_LEN] &&
	    !(ops->supported_ring_params & ETHTOOL_RING_USE_RX_BUF_LEN)) {
		ret = -EOPNOTSUPP;
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[ETHTOOL_A_RINGS_RX_BUF_LEN],
				    "setting rx buf len not supported");
		goto out_dev;
	}

	if (tb[ETHTOOL_A_RINGS_CQE_SIZE] &&
	    !(ops->supported_ring_params & ETHTOOL_RING_USE_CQE_SIZE)) {
		ret = -EOPNOTSUPP;
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[ETHTOOL_A_RINGS_CQE_SIZE],
				    "setting cqe size not supported");
		goto out_dev;
	}

	if (tb[ETHTOOL_A_RINGS_TX_PUSH] &&
	    !(ops->supported_ring_params & ETHTOOL_RING_USE_TX_PUSH)) {
		ret = -EOPNOTSUPP;
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[ETHTOOL_A_RINGS_TX_PUSH],
				    "setting tx push not supported");
		goto out_dev;
	}

	rtnl_lock();
	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		goto out_rtnl;
	ops->get_ringparam(dev, &ringparam, &kernel_ringparam, info->extack);

	ethnl_update_u32(&ringparam.rx_pending, tb[ETHTOOL_A_RINGS_RX], &mod);
	ethnl_update_u32(&ringparam.rx_mini_pending,
			 tb[ETHTOOL_A_RINGS_RX_MINI], &mod);
	ethnl_update_u32(&ringparam.rx_jumbo_pending,
			 tb[ETHTOOL_A_RINGS_RX_JUMBO], &mod);
	ethnl_update_u32(&ringparam.tx_pending, tb[ETHTOOL_A_RINGS_TX], &mod);
	ethnl_update_u32(&kernel_ringparam.rx_buf_len,
			 tb[ETHTOOL_A_RINGS_RX_BUF_LEN], &mod);
	ethnl_update_u32(&kernel_ringparam.cqe_size,
			 tb[ETHTOOL_A_RINGS_CQE_SIZE], &mod);
	ethnl_update_u8(&kernel_ringparam.tx_push,
			tb[ETHTOOL_A_RINGS_TX_PUSH], &mod);
	ret = 0;
	if (!mod)
		goto out_ops;

	/* ensure new ring parameters are within limits */
	if (ringparam.rx_pending > ringparam.rx_max_pending)
		err_attr = tb[ETHTOOL_A_RINGS_RX];
	else if (ringparam.rx_mini_pending > ringparam.rx_mini_max_pending)
		err_attr = tb[ETHTOOL_A_RINGS_RX_MINI];
	else if (ringparam.rx_jumbo_pending > ringparam.rx_jumbo_max_pending)
		err_attr = tb[ETHTOOL_A_RINGS_RX_JUMBO];
	else if (ringparam.tx_pending > ringparam.tx_max_pending)
		err_attr = tb[ETHTOOL_A_RINGS_TX];
	else
		err_attr = NULL;
	if (err_attr) {
		ret = -EINVAL;
		NL_SET_ERR_MSG_ATTR(info->extack, err_attr,
				    "requested ring size exceeds maximum");
		goto out_ops;
	}

	ret = dev->ethtool_ops->set_ringparam(dev, &ringparam,
					      &kernel_ringparam, info->extack);
	if (ret < 0)
		goto out_ops;
	ethtool_notify(dev, ETHTOOL_MSG_RINGS_NTF, NULL);

out_ops:
	ethnl_ops_complete(dev);
out_rtnl:
	rtnl_unlock();
out_dev:
	ethnl_parse_header_dev_put(&req_info);
	return ret;
}

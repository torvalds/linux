// SPDX-License-Identifier: GPL-2.0-only

#include <net/netdev_lock.h>

#include "netlink.h"
#include "common.h"

struct rss_req_info {
	struct ethnl_req_info		base;
	u32				rss_context;
};

struct rss_reply_data {
	struct ethnl_reply_data		base;
	bool				has_flow_hash;
	bool				no_key_fields;
	u32				indir_size;
	u32				hkey_size;
	u32				hfunc;
	u32				input_xfrm;
	u32				*indir_table;
	u8				*hkey;
	int				flow_hash[__ETHTOOL_A_FLOW_CNT];
};

static const u8 ethtool_rxfh_ft_nl2ioctl[] = {
	[ETHTOOL_A_FLOW_ETHER]		= ETHER_FLOW,
	[ETHTOOL_A_FLOW_IP4]		= IPV4_FLOW,
	[ETHTOOL_A_FLOW_IP6]		= IPV6_FLOW,
	[ETHTOOL_A_FLOW_TCP4]		= TCP_V4_FLOW,
	[ETHTOOL_A_FLOW_UDP4]		= UDP_V4_FLOW,
	[ETHTOOL_A_FLOW_SCTP4]		= SCTP_V4_FLOW,
	[ETHTOOL_A_FLOW_AH_ESP4]	= AH_ESP_V4_FLOW,
	[ETHTOOL_A_FLOW_TCP6]		= TCP_V6_FLOW,
	[ETHTOOL_A_FLOW_UDP6]		= UDP_V6_FLOW,
	[ETHTOOL_A_FLOW_SCTP6]		= SCTP_V6_FLOW,
	[ETHTOOL_A_FLOW_AH_ESP6]	= AH_ESP_V6_FLOW,
	[ETHTOOL_A_FLOW_AH4]		= AH_V4_FLOW,
	[ETHTOOL_A_FLOW_ESP4]		= ESP_V4_FLOW,
	[ETHTOOL_A_FLOW_AH6]		= AH_V6_FLOW,
	[ETHTOOL_A_FLOW_ESP6]		= ESP_V6_FLOW,
	[ETHTOOL_A_FLOW_GTPU4]		= GTPU_V4_FLOW,
	[ETHTOOL_A_FLOW_GTPU6]		= GTPU_V6_FLOW,
	[ETHTOOL_A_FLOW_GTPC4]		= GTPC_V4_FLOW,
	[ETHTOOL_A_FLOW_GTPC6]		= GTPC_V6_FLOW,
	[ETHTOOL_A_FLOW_GTPC_TEID4]	= GTPC_TEID_V4_FLOW,
	[ETHTOOL_A_FLOW_GTPC_TEID6]	= GTPC_TEID_V6_FLOW,
	[ETHTOOL_A_FLOW_GTPU_EH4]	= GTPU_EH_V4_FLOW,
	[ETHTOOL_A_FLOW_GTPU_EH6]	= GTPU_EH_V6_FLOW,
	[ETHTOOL_A_FLOW_GTPU_UL4]	= GTPU_UL_V4_FLOW,
	[ETHTOOL_A_FLOW_GTPU_UL6]	= GTPU_UL_V6_FLOW,
	[ETHTOOL_A_FLOW_GTPU_DL4]	= GTPU_DL_V4_FLOW,
	[ETHTOOL_A_FLOW_GTPU_DL6]	= GTPU_DL_V6_FLOW,
};

#define RSS_REQINFO(__req_base) \
	container_of(__req_base, struct rss_req_info, base)

#define RSS_REPDATA(__reply_base) \
	container_of(__reply_base, struct rss_reply_data, base)

const struct nla_policy ethnl_rss_get_policy[] = {
	[ETHTOOL_A_RSS_HEADER] = NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_RSS_CONTEXT] = { .type = NLA_U32 },
	[ETHTOOL_A_RSS_START_CONTEXT] = { .type = NLA_U32 },
};

static int
rss_parse_request(struct ethnl_req_info *req_info, struct nlattr **tb,
		  struct netlink_ext_ack *extack)
{
	struct rss_req_info *request = RSS_REQINFO(req_info);

	if (tb[ETHTOOL_A_RSS_CONTEXT])
		request->rss_context = nla_get_u32(tb[ETHTOOL_A_RSS_CONTEXT]);
	if (tb[ETHTOOL_A_RSS_START_CONTEXT]) {
		NL_SET_BAD_ATTR(extack, tb[ETHTOOL_A_RSS_START_CONTEXT]);
		return -EINVAL;
	}

	return 0;
}

static void
rss_prepare_flow_hash(const struct rss_req_info *req, struct net_device *dev,
		      struct rss_reply_data *data, const struct genl_info *info)
{
	int i;

	data->has_flow_hash = false;

	if (!dev->ethtool_ops->get_rxfh_fields)
		return;
	if (req->rss_context && !dev->ethtool_ops->rxfh_per_ctx_fields)
		return;

	mutex_lock(&dev->ethtool->rss_lock);
	for (i = 1; i < __ETHTOOL_A_FLOW_CNT; i++) {
		struct ethtool_rxfh_fields fields = {
			.flow_type	= ethtool_rxfh_ft_nl2ioctl[i],
			.rss_context	= req->rss_context,
		};

		if (dev->ethtool_ops->get_rxfh_fields(dev, &fields)) {
			data->flow_hash[i] = -1; /* Unsupported */
			continue;
		}

		data->flow_hash[i] = fields.data;
		data->has_flow_hash = true;
	}
	mutex_unlock(&dev->ethtool->rss_lock);
}

static int
rss_prepare_get(const struct rss_req_info *request, struct net_device *dev,
		struct rss_reply_data *data, const struct genl_info *info)
{
	struct ethtool_rxfh_param rxfh = {};
	const struct ethtool_ops *ops;
	u32 total_size, indir_bytes;
	u8 *rss_config;
	int ret;

	ops = dev->ethtool_ops;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;
	mutex_lock(&dev->ethtool->rss_lock);

	data->indir_size = 0;
	data->hkey_size = 0;
	if (ops->get_rxfh_indir_size)
		data->indir_size = ops->get_rxfh_indir_size(dev);
	if (ops->get_rxfh_key_size)
		data->hkey_size = ops->get_rxfh_key_size(dev);

	indir_bytes = data->indir_size * sizeof(u32);
	total_size = indir_bytes + data->hkey_size;
	rss_config = kzalloc(total_size, GFP_KERNEL);
	if (!rss_config) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	if (data->indir_size)
		data->indir_table = (u32 *)rss_config;
	if (data->hkey_size)
		data->hkey = rss_config + indir_bytes;

	rxfh.indir_size = data->indir_size;
	rxfh.indir = data->indir_table;
	rxfh.key_size = data->hkey_size;
	rxfh.key = data->hkey;

	ret = ops->get_rxfh(dev, &rxfh);
	if (ret)
		goto out_unlock;

	data->hfunc = rxfh.hfunc;
	data->input_xfrm = rxfh.input_xfrm;
out_unlock:
	mutex_unlock(&dev->ethtool->rss_lock);
	ethnl_ops_complete(dev);
	return ret;
}

static int
rss_prepare_ctx(const struct rss_req_info *request, struct net_device *dev,
		struct rss_reply_data *data, const struct genl_info *info)
{
	struct ethtool_rxfh_context *ctx;
	u32 total_size, indir_bytes;
	u8 *rss_config;
	int ret;

	data->no_key_fields = !dev->ethtool_ops->rxfh_per_ctx_key;

	mutex_lock(&dev->ethtool->rss_lock);
	ctx = xa_load(&dev->ethtool->rss_ctx, request->rss_context);
	if (!ctx) {
		ret = -ENOENT;
		goto out_unlock;
	}

	data->indir_size = ctx->indir_size;
	data->hkey_size = ctx->key_size;
	data->hfunc = ctx->hfunc;
	data->input_xfrm = ctx->input_xfrm;

	indir_bytes = data->indir_size * sizeof(u32);
	total_size = indir_bytes + data->hkey_size;
	rss_config = kzalloc(total_size, GFP_KERNEL);
	if (!rss_config) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	data->indir_table = (u32 *)rss_config;
	memcpy(data->indir_table, ethtool_rxfh_context_indir(ctx), indir_bytes);

	if (data->hkey_size) {
		data->hkey = rss_config + indir_bytes;
		memcpy(data->hkey, ethtool_rxfh_context_key(ctx),
		       data->hkey_size);
	}

	ret = 0;
out_unlock:
	mutex_unlock(&dev->ethtool->rss_lock);
	return ret;
}

static int
rss_prepare(const struct rss_req_info *request, struct net_device *dev,
	    struct rss_reply_data *data, const struct genl_info *info)
{
	rss_prepare_flow_hash(request, dev, data, info);

	/* Coming from RSS_SET, driver may only have flow_hash_fields ops */
	if (!dev->ethtool_ops->get_rxfh)
		return 0;

	if (request->rss_context)
		return rss_prepare_ctx(request, dev, data, info);
	return rss_prepare_get(request, dev, data, info);
}

static int
rss_prepare_data(const struct ethnl_req_info *req_base,
		 struct ethnl_reply_data *reply_base,
		 const struct genl_info *info)
{
	struct rss_reply_data *data = RSS_REPDATA(reply_base);
	struct rss_req_info *request = RSS_REQINFO(req_base);
	struct net_device *dev = reply_base->dev;
	const struct ethtool_ops *ops;

	ops = dev->ethtool_ops;
	if (!ops->get_rxfh)
		return -EOPNOTSUPP;

	/* Some drivers don't handle rss_context */
	if (request->rss_context && !ops->create_rxfh_context)
		return -EOPNOTSUPP;

	return rss_prepare(request, dev, data, info);
}

static int
rss_reply_size(const struct ethnl_req_info *req_base,
	       const struct ethnl_reply_data *reply_base)
{
	const struct rss_reply_data *data = RSS_REPDATA(reply_base);
	int len;

	len = nla_total_size(sizeof(u32)) +	/* _RSS_CONTEXT */
	      nla_total_size(sizeof(u32)) +	/* _RSS_HFUNC */
	      nla_total_size(sizeof(u32)) +	/* _RSS_INPUT_XFRM */
	      nla_total_size(sizeof(u32) * data->indir_size) + /* _RSS_INDIR */
	      nla_total_size(data->hkey_size) + /* _RSS_HKEY */
	      nla_total_size(0) +		/* _RSS_FLOW_HASH */
		nla_total_size(sizeof(u32)) * ETHTOOL_A_FLOW_MAX +
	      0;

	return len;
}

static int
rss_fill_reply(struct sk_buff *skb, const struct ethnl_req_info *req_base,
	       const struct ethnl_reply_data *reply_base)
{
	const struct rss_reply_data *data = RSS_REPDATA(reply_base);
	struct rss_req_info *request = RSS_REQINFO(req_base);

	if (request->rss_context &&
	    nla_put_u32(skb, ETHTOOL_A_RSS_CONTEXT, request->rss_context))
		return -EMSGSIZE;

	if ((data->indir_size &&
	     nla_put(skb, ETHTOOL_A_RSS_INDIR,
		     sizeof(u32) * data->indir_size, data->indir_table)))
		return -EMSGSIZE;

	if (!data->no_key_fields &&
	    ((data->hfunc &&
	      nla_put_u32(skb, ETHTOOL_A_RSS_HFUNC, data->hfunc)) ||
	     (data->input_xfrm &&
	      nla_put_u32(skb, ETHTOOL_A_RSS_INPUT_XFRM, data->input_xfrm)) ||
	     (data->hkey_size &&
	      nla_put(skb, ETHTOOL_A_RSS_HKEY, data->hkey_size, data->hkey))))
		return -EMSGSIZE;

	if (data->has_flow_hash) {
		struct nlattr *nest;
		int i;

		nest = nla_nest_start(skb, ETHTOOL_A_RSS_FLOW_HASH);
		if (!nest)
			return -EMSGSIZE;

		for (i = 1; i < __ETHTOOL_A_FLOW_CNT; i++) {
			if (data->flow_hash[i] >= 0 &&
			    nla_put_uint(skb, i, data->flow_hash[i])) {
				nla_nest_cancel(skb, nest);
				return -EMSGSIZE;
			}
		}

		nla_nest_end(skb, nest);
	}

	return 0;
}

static void rss_cleanup_data(struct ethnl_reply_data *reply_base)
{
	const struct rss_reply_data *data = RSS_REPDATA(reply_base);

	kfree(data->indir_table);
}

struct rss_nl_dump_ctx {
	unsigned long		ifindex;
	unsigned long		ctx_idx;

	/* User wants to only dump contexts from given ifindex */
	unsigned int		match_ifindex;
	unsigned int		start_ctx;
};

static struct rss_nl_dump_ctx *rss_dump_ctx(struct netlink_callback *cb)
{
	NL_ASSERT_CTX_FITS(struct rss_nl_dump_ctx);

	return (struct rss_nl_dump_ctx *)cb->ctx;
}

int ethnl_rss_dump_start(struct netlink_callback *cb)
{
	const struct genl_info *info = genl_info_dump(cb);
	struct rss_nl_dump_ctx *ctx = rss_dump_ctx(cb);
	struct ethnl_req_info req_info = {};
	struct nlattr **tb = info->attrs;
	int ret;

	/* Filtering by context not supported */
	if (tb[ETHTOOL_A_RSS_CONTEXT]) {
		NL_SET_BAD_ATTR(info->extack, tb[ETHTOOL_A_RSS_CONTEXT]);
		return -EINVAL;
	}
	if (tb[ETHTOOL_A_RSS_START_CONTEXT]) {
		ctx->start_ctx = nla_get_u32(tb[ETHTOOL_A_RSS_START_CONTEXT]);
		ctx->ctx_idx = ctx->start_ctx;
	}

	ret = ethnl_parse_header_dev_get(&req_info,
					 tb[ETHTOOL_A_RSS_HEADER],
					 sock_net(cb->skb->sk), cb->extack,
					 false);
	if (req_info.dev) {
		ctx->match_ifindex = req_info.dev->ifindex;
		ctx->ifindex = ctx->match_ifindex;
		ethnl_parse_header_dev_put(&req_info);
		req_info.dev = NULL;
	}

	return ret;
}

static int
rss_dump_one_ctx(struct sk_buff *skb, struct netlink_callback *cb,
		 struct net_device *dev, u32 rss_context)
{
	const struct genl_info *info = genl_info_dump(cb);
	struct rss_reply_data data = {};
	struct rss_req_info req = {};
	void *ehdr;
	int ret;

	req.rss_context = rss_context;

	ehdr = ethnl_dump_put(skb, cb, ETHTOOL_MSG_RSS_GET_REPLY);
	if (!ehdr)
		return -EMSGSIZE;

	ret = ethnl_fill_reply_header(skb, dev, ETHTOOL_A_RSS_HEADER);
	if (ret < 0)
		goto err_cancel;

	ret = rss_prepare(&req, dev, &data, info);
	if (ret)
		goto err_cancel;

	ret = rss_fill_reply(skb, &req.base, &data.base);
	if (ret)
		goto err_cleanup;
	genlmsg_end(skb, ehdr);

	rss_cleanup_data(&data.base);
	return 0;

err_cleanup:
	rss_cleanup_data(&data.base);
err_cancel:
	genlmsg_cancel(skb, ehdr);
	return ret;
}

static int
rss_dump_one_dev(struct sk_buff *skb, struct netlink_callback *cb,
		 struct net_device *dev)
{
	struct rss_nl_dump_ctx *ctx = rss_dump_ctx(cb);
	int ret;

	if (!dev->ethtool_ops->get_rxfh)
		return 0;

	if (!ctx->ctx_idx) {
		ret = rss_dump_one_ctx(skb, cb, dev, 0);
		if (ret)
			return ret;
		ctx->ctx_idx++;
	}

	for (; xa_find(&dev->ethtool->rss_ctx, &ctx->ctx_idx,
		       ULONG_MAX, XA_PRESENT); ctx->ctx_idx++) {
		ret = rss_dump_one_ctx(skb, cb, dev, ctx->ctx_idx);
		if (ret)
			return ret;
	}
	ctx->ctx_idx = ctx->start_ctx;

	return 0;
}

int ethnl_rss_dumpit(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct rss_nl_dump_ctx *ctx = rss_dump_ctx(cb);
	struct net *net = sock_net(skb->sk);
	struct net_device *dev;
	int ret = 0;

	rtnl_lock();
	for_each_netdev_dump(net, dev, ctx->ifindex) {
		if (ctx->match_ifindex && ctx->match_ifindex != ctx->ifindex)
			break;

		netdev_lock_ops(dev);
		ret = rss_dump_one_dev(skb, cb, dev);
		netdev_unlock_ops(dev);
		if (ret)
			break;
	}
	rtnl_unlock();

	return ret;
}

/* RSS_NTF */

void ethtool_rss_notify(struct net_device *dev, u32 rss_context)
{
	struct rss_req_info req_info = {
		.rss_context = rss_context,
	};

	ethnl_notify(dev, ETHTOOL_MSG_RSS_NTF, &req_info.base);
}

/* RSS_SET */

const struct nla_policy ethnl_rss_set_policy[ETHTOOL_A_RSS_START_CONTEXT + 1] = {
	[ETHTOOL_A_RSS_HEADER] = NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_RSS_CONTEXT] = { .type = NLA_U32, },
	[ETHTOOL_A_RSS_INDIR] = { .type = NLA_BINARY, },
};

static int
ethnl_rss_set_validate(struct ethnl_req_info *req_info, struct genl_info *info)
{
	const struct ethtool_ops *ops = req_info->dev->ethtool_ops;
	struct rss_req_info *request = RSS_REQINFO(req_info);
	struct nlattr **tb = info->attrs;
	struct nlattr *bad_attr = NULL;

	if (request->rss_context && !ops->create_rxfh_context)
		bad_attr = bad_attr ?: tb[ETHTOOL_A_RSS_CONTEXT];

	if (bad_attr) {
		NL_SET_BAD_ATTR(info->extack, bad_attr);
		return -EOPNOTSUPP;
	}

	return 1;
}

static int
rss_set_prep_indir(struct net_device *dev, struct genl_info *info,
		   struct rss_reply_data *data, struct ethtool_rxfh_param *rxfh,
		   bool *reset, bool *mod)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct netlink_ext_ack *extack = info->extack;
	struct nlattr **tb = info->attrs;
	struct ethtool_rxnfc rx_rings;
	size_t alloc_size;
	u32 user_size;
	int i, err;

	if (!tb[ETHTOOL_A_RSS_INDIR])
		return 0;
	if (!data->indir_size || !ops->get_rxnfc)
		return -EOPNOTSUPP;

	rx_rings.cmd = ETHTOOL_GRXRINGS;
	err = ops->get_rxnfc(dev, &rx_rings, NULL);
	if (err)
		return err;

	if (nla_len(tb[ETHTOOL_A_RSS_INDIR]) % 4) {
		NL_SET_BAD_ATTR(info->extack, tb[ETHTOOL_A_RSS_INDIR]);
		return -EINVAL;
	}
	user_size = nla_len(tb[ETHTOOL_A_RSS_INDIR]) / 4;
	if (!user_size) {
		if (rxfh->rss_context) {
			NL_SET_ERR_MSG_ATTR(extack, tb[ETHTOOL_A_RSS_INDIR],
					    "can't reset table for a context");
			return -EINVAL;
		}
		*reset = true;
	} else if (data->indir_size % user_size) {
		NL_SET_ERR_MSG_ATTR_FMT(extack, tb[ETHTOOL_A_RSS_INDIR],
					"size (%d) mismatch with device indir table (%d)",
					user_size, data->indir_size);
		return -EINVAL;
	}

	rxfh->indir_size = data->indir_size;
	alloc_size = array_size(data->indir_size, sizeof(rxfh->indir[0]));
	rxfh->indir = kzalloc(alloc_size, GFP_KERNEL);
	if (!rxfh->indir)
		return -ENOMEM;

	nla_memcpy(rxfh->indir, tb[ETHTOOL_A_RSS_INDIR], alloc_size);
	for (i = 0; i < user_size; i++) {
		if (rxfh->indir[i] < rx_rings.data)
			continue;

		NL_SET_ERR_MSG_ATTR_FMT(extack, tb[ETHTOOL_A_RSS_INDIR],
					"entry %d: queue out of range (%d)",
					i, rxfh->indir[i]);
		err = -EINVAL;
		goto err_free;
	}

	if (user_size) {
		/* Replicate the user-provided table to fill the device table */
		for (i = user_size; i < data->indir_size; i++)
			rxfh->indir[i] = rxfh->indir[i % user_size];
	} else {
		for (i = 0; i < data->indir_size; i++)
			rxfh->indir[i] =
				ethtool_rxfh_indir_default(i, rx_rings.data);
	}

	*mod |= memcmp(rxfh->indir, data->indir_table, data->indir_size);

	return 0;

err_free:
	kfree(rxfh->indir);
	rxfh->indir = NULL;
	return err;
}

static void
rss_set_ctx_update(struct ethtool_rxfh_context *ctx, struct nlattr **tb,
		   struct rss_reply_data *data, struct ethtool_rxfh_param *rxfh)
{
	int i;

	if (rxfh->indir) {
		for (i = 0; i < data->indir_size; i++)
			ethtool_rxfh_context_indir(ctx)[i] = rxfh->indir[i];
		ctx->indir_configured = !!nla_len(tb[ETHTOOL_A_RSS_INDIR]);
	}
}

static int
ethnl_rss_set(struct ethnl_req_info *req_info, struct genl_info *info)
{
	struct rss_req_info *request = RSS_REQINFO(req_info);
	struct ethtool_rxfh_context *ctx = NULL;
	struct net_device *dev = req_info->dev;
	struct ethtool_rxfh_param rxfh = {};
	bool indir_reset = false, indir_mod;
	struct nlattr **tb = info->attrs;
	struct rss_reply_data data = {};
	const struct ethtool_ops *ops;
	bool mod = false;
	int ret;

	ops = dev->ethtool_ops;
	data.base.dev = dev;

	ret = rss_prepare(request, dev, &data, info);
	if (ret)
		return ret;

	rxfh.rss_context = request->rss_context;

	ret = rss_set_prep_indir(dev, info, &data, &rxfh, &indir_reset, &mod);
	if (ret)
		goto exit_clean_data;
	indir_mod = !!tb[ETHTOOL_A_RSS_INDIR];

	rxfh.hfunc = ETH_RSS_HASH_NO_CHANGE;
	rxfh.input_xfrm = RXH_XFRM_NO_CHANGE;

	mutex_lock(&dev->ethtool->rss_lock);
	if (request->rss_context) {
		ctx = xa_load(&dev->ethtool->rss_ctx, request->rss_context);
		if (!ctx) {
			ret = -ENOENT;
			goto exit_unlock;
		}
	}

	if (!mod)
		ret = 0; /* nothing to tell the driver */
	else if (!ops->set_rxfh)
		ret = -EOPNOTSUPP;
	else if (!rxfh.rss_context)
		ret = ops->set_rxfh(dev, &rxfh, info->extack);
	else
		ret = ops->modify_rxfh_context(dev, ctx, &rxfh, info->extack);
	if (ret)
		goto exit_unlock;

	if (ctx)
		rss_set_ctx_update(ctx, tb, &data, &rxfh);
	else if (indir_reset)
		dev->priv_flags &= ~IFF_RXFH_CONFIGURED;
	else if (indir_mod)
		dev->priv_flags |= IFF_RXFH_CONFIGURED;

exit_unlock:
	mutex_unlock(&dev->ethtool->rss_lock);
	kfree(rxfh.indir);
exit_clean_data:
	rss_cleanup_data(&data.base);

	return ret ?: mod;
}

const struct ethnl_request_ops ethnl_rss_request_ops = {
	.request_cmd		= ETHTOOL_MSG_RSS_GET,
	.reply_cmd		= ETHTOOL_MSG_RSS_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_RSS_HEADER,
	.req_info_size		= sizeof(struct rss_req_info),
	.reply_data_size	= sizeof(struct rss_reply_data),

	.parse_request		= rss_parse_request,
	.prepare_data		= rss_prepare_data,
	.reply_size		= rss_reply_size,
	.fill_reply		= rss_fill_reply,
	.cleanup_data		= rss_cleanup_data,

	.set_validate		= ethnl_rss_set_validate,
	.set			= ethnl_rss_set,
	.set_ntf_cmd		= ETHTOOL_MSG_RSS_NTF,
};
